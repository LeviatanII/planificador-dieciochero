#define _POSIX_C_SOURCE 200809L

#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>
#include "ejecutor.h"
#include "ipc.h"

// Relaciona un proceso hijo con la actividad que esta ejecutando
typedef struct {
    pid_t pid;
    size_t actividad;
    int lectura;
} Hijo;

static Mensaje crear_mensaje(const Plan *plan, size_t indice)
{
    Mensaje mensaje = {0};

    // Acotamos el ID y conservamos el indice para distinguir cada actividad
    snprintf(mensaje.texto, TAM_MENSAJE, "OK id=%.40s nodo=%zu",
             plan->actividades[indice].id, indice);

    return mensaje;
}

static void simular_actividad(const Plan *plan, const Grafo *grafo,
                             size_t indice, int entrada, int salida)
{
    const ListaIndices *dependencias = &grafo->nodos[indice].dependencias;

    // El hijo recibe por el pipe un mensaje por cada requisito
    for (size_t i = 0; i < dependencias->cantidad; ++i) {
        Mensaje recibido;
        Mensaje esperado = crear_mensaje(plan, dependencias->datos[i]);

        if (!recibir_mensaje(entrada, &recibido) ||
            memcmp(recibido.texto, esperado.texto, TAM_MENSAJE) != 0) {
            _exit(EXIT_FAILURE);
        }
    }

    close(entrada);

    const Actividad *actividad = &plan->actividades[indice];

    // Convertimos los milisegundos a segundos y nanosegundos
    struct timespec espera = {
        .tv_sec = actividad->tiempo_ms / 1000,
        .tv_nsec = (long)(actividad->tiempo_ms % 1000) * 1000000L
    };

    // Si una senal interrumpe la espera continuamos con el tiempo restante
    while (nanosleep(&espera, &espera) == -1) {
        if (errno != EINTR) {
            perror("nanosleep");
            _exit(EXIT_FAILURE);
        }
    }

    // El resultado real del hijo viaja por su pipe de salida
    Mensaje resultado = crear_mensaje(plan, indice);

    if (!enviar_mensaje(salida, &resultado)) {
        _exit(EXIT_FAILURE);
    }

    close(salida);
    _exit(EXIT_SUCCESS);
}

static void detener_hijos(const Hijo *hijos, size_t cantidad)
{
    // Ante un error de esta etapa detenemos a los hijos que quedan
    for (size_t i = 0; i < cantidad; ++i) {
        if (kill(hijos[i].pid, SIGKILL) == -1 && errno != ESRCH) {
            perror("kill");
        }
    }

    // Recogemos a cada hijo para no dejar procesos zombies
    for (size_t i = 0; i < cantidad; ++i) {
        while (waitpid(hijos[i].pid, NULL, 0) == -1 && errno == EINTR) {
        }

        close(hijos[i].lectura);
    }
}

int ejecutar_plan(const Plan *plan, const Grafo *grafo, int k)
{
    if (k <= 0 || plan->cantidad == 0 || plan->cantidad != grafo->cantidad) {
        fprintf(stderr, "Error: datos invalidos para ejecutar el plan\n");
        return 0;
    }

    // Si un lector desaparece write devuelve un error y podemos hacer limpieza
    struct sigaction ignorar = {0};
    struct sigaction anterior;

    ignorar.sa_handler = SIG_IGN;
    sigemptyset(&ignorar.sa_mask);

    if (sigaction(SIGPIPE, &ignorar, &anterior) == -1) {
        perror("sigaction SIGPIPE");
        return 0;
    }

    // Reservamos espacio solo para los hijos que podrian estar activos
    size_t limite = (size_t)k;

    if (limite > plan->cantidad) {
        limite = plan->cantidad;
    }

    size_t *pendientes = calloc(plan->cantidad, sizeof(*pendientes));
    size_t *cola = calloc(plan->cantidad, sizeof(*cola));
    Hijo *hijos = calloc(limite, sizeof(*hijos));
    Mensaje *resultados = calloc(plan->cantidad, sizeof(*resultados));

    size_t inicio = 0;
    size_t fin = 0;
    size_t activos = 0;
    size_t completadas = 0;
    int correcto = 0;

    if (pendientes == NULL || cola == NULL ||
        hijos == NULL || resultados == NULL) {
        perror("Memoria para ejecutar el plan");
        goto salir;
    }

    // Al comienzo solo pueden ejecutarse las actividades sin dependencias
    for (size_t i = 0; i < plan->cantidad; ++i) {
        pendientes[i] = grafo->nodos[i].dependencias.cantidad;

        if (pendientes[i] == 0) {
            cola[fin++] = i;
        }
    }

    while (completadas < plan->cantidad) {
        // Creamos hijos mientras haya actividades listas y cupo disponible
        while (activos < limite && inicio < fin) {
            size_t indice = cola[inicio++];

            // Creamos un pipe de entrada y otro para el resultado del hijo
            int entrada[2];
            int salida[2];

            if (pipe(entrada) == -1) {
                perror("pipe de entrada");
                goto salir;
            }

            if (pipe(salida) == -1) {
                perror("pipe de salida");
                close(entrada[0]);
                close(entrada[1]);
                goto salir;
            }

            pid_t pid = fork();

            if (pid == -1) {
                perror("fork");
                close(entrada[0]);
                close(entrada[1]);
                close(salida[0]);
                close(salida[1]);
                goto salir;
            }

            if (pid == 0) {
                // El hijo conserva solo los extremos que utiliza
                close(entrada[1]);
                close(salida[0]);

                for (size_t i = 0; i < activos; ++i) {
                    close(hijos[i].lectura);
                }

                simular_actividad(plan, grafo, indice, entrada[0], salida[1]);
            }

            close(entrada[0]);
            close(salida[1]);

            // Solo el padre registra al hijo y continua planificando
            hijos[activos++] = (Hijo){pid, indice, salida[0]};

            printf("[INICIO] ID=%s PID=%ld Activos=%zu/%d\n",
                   plan->actividades[indice].id, (long)pid, activos, k);

            fflush(stdout);

            // Reenviamos los resultados guardados mientras el hijo los lee
            const ListaIndices *deps = &grafo->nodos[indice].dependencias;
            int envio_correcto = 1;

            for (size_t i = 0; i < deps->cantidad; ++i) {
                size_t origen = deps->datos[i];

                if (!enviar_mensaje(entrada[1], &resultados[origen])) {
                    perror("Envio de insumos");
                    envio_correcto = 0;
                    break;
                }

                printf("[PIPE] origen=%s destino=%s mensaje=%s\n",
                       plan->actividades[origen].id,
                       plan->actividades[indice].id,
                       resultados[origen].texto);
            }

            close(entrada[1]);
            fflush(stdout);

            if (!envio_correcto) {
                goto salir;
            }
        }

        if (activos == 0) {
            fprintf(stderr, "Error: no hay actividades que puedan avanzar\n");
            goto salir;
        }

        int estado;
        pid_t terminado;

        // El padre se bloquea hasta que termine cualquiera de sus hijos
        do {
            terminado = waitpid(-1, &estado, 0);
        } while (terminado == -1 && errno == EINTR);

        if (terminado == -1) {
            perror("waitpid");
            goto salir;
        }

        // Buscamos que actividad correspondia al proceso que termino
        size_t posicion = 0;

        while (posicion < activos && hijos[posicion].pid != terminado) {
            ++posicion;
        }

        if (posicion == activos) {
            fprintf(stderr, "Error: termino un hijo no registrado\n");
            goto salir;
        }

        Hijo hijo = hijos[posicion];
        size_t indice = hijo.actividad;

        // Cubrimos el espacio libre con el ultimo hijo registrado
        hijos[posicion] = hijos[--activos];

        // Leemos despues de waitpid porque el unico resultado cabe en el pipe
        int resultado_correcto = WIFEXITED(estado) &&
                                 WEXITSTATUS(estado) == EXIT_SUCCESS;

        if (resultado_correcto) {
            Mensaje esperado = crear_mensaje(plan, indice);

            resultado_correcto =
                recibir_mensaje(hijo.lectura, &resultados[indice]) &&
                memcmp(resultados[indice].texto,
                       esperado.texto, TAM_MENSAJE) == 0;
        }

        close(hijo.lectura);

        if (!resultado_correcto) {
            fprintf(stderr,
                    "Error: la actividad %s fallo o envio un resultado invalido\n",
                    plan->actividades[indice].id);
            goto salir;
        }

        ++completadas;

        printf("[FIN] ID=%s PID=%ld Activos=%zu/%d\n",
               plan->actividades[indice].id, (long)terminado, activos, k);

        fflush(stdout);

        const ListaIndices *siguientes = &grafo->nodos[indice].siguientes;

        // Una actividad queda lista cuando todos sus requisitos terminaron
        for (size_t j = 0; j < siguientes->cantidad; ++j) {
            size_t siguiente = siguientes->datos[j];

            if (--pendientes[siguiente] == 0) {
                cola[fin++] = siguiente;
            }
        }
    }

    printf("Plan completado: %zu actividades\n", completadas);
    correcto = 1;

salir:
    if (!correcto) {
        detener_hijos(hijos, activos);
    }

    free(hijos);
    free(resultados);
    free(cola);
    free(pendientes);

    sigaction(SIGPIPE, &anterior, NULL);

    return correcto;
}
