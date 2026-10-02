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

typedef struct {
    pid_t pid;
    size_t actividad;
    int lectura;
} Hijo;

static volatile sig_atomic_t g_interrumpido = 0;

static void manejador_sigint(int sig) {
    (void)sig;
    g_interrumpido = 1;
}

static Mensaje crear_mensaje(const Plan *plan, size_t indice) {
    Mensaje mensaje = {0};
    snprintf(mensaje.texto, TAM_MENSAJE, "OK id=%.40s nodo=%zu",
             plan->actividades[indice].id, indice);
    return mensaje;
}

static void simular_actividad(const Plan *plan, const Grafo *grafo,
                              size_t indice, int entrada, int salida) {
    const ListaIndices *dependencias = &grafo->nodos[indice].dependencias;

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

    struct timespec espera = {
        .tv_sec = actividad->tiempo_ms / 1000,
        .tv_nsec = (long)(actividad->tiempo_ms % 1000) * 1000000L
    };

    while (nanosleep(&espera, &espera) == -1) {
        if (errno != EINTR) {
            _exit(EXIT_FAILURE);
        }
    }

    Mensaje resultado = crear_mensaje(plan, indice);

    if (!enviar_mensaje(salida, &resultado)) {
        _exit(EXIT_FAILURE);
    }

    close(salida);
    _exit(EXIT_SUCCESS);
}

static void detener_hijos(const Hijo *hijos, size_t cantidad) {
    for (size_t i = 0; i < cantidad; ++i) {
        if (kill(hijos[i].pid, SIGKILL) == -1 && errno != ESRCH) {
            perror("kill");
        }
    }

    for (size_t i = 0; i < cantidad; ++i) {
        while (waitpid(hijos[i].pid, NULL, 0) == -1 && errno == EINTR) {
        }
        close(hijos[i].lectura);
    }
}

// Auxiliar para propagar fallas
static void cancelar_subrama(const Grafo *grafo, size_t indice, int *estado_nodos) {
    const ListaIndices *siguientes = &grafo->nodos[indice].siguientes;

    for (size_t i = 0; i < siguientes->cantidad; ++i) {
        size_t sig = siguientes->datos[i];
        if (estado_nodos[sig] == 0) { // Si estaba pendiente
            estado_nodos[sig] = 2;   // Marcamos como fallido/cancelado
            printf("[AISLAMIENTO] Rama cancelada: actividad index %zu omitida por fallo en antecesor.\n", sig);
            cancelar_subrama(grafo, sig, estado_nodos);
        }
    }
}

int ejecutar_plan(const Plan *plan, const Grafo *grafo, int k) {
    if (k <= 0 || plan->cantidad == 0 || plan->cantidad != grafo->cantidad) {
        fprintf(stderr, "Error: datos invalidos para ejecutar el plan\n");
        return 0;
    }

    // Configuración de SIGINT (Seremi)
    struct sigaction sa_int = {0};
    struct sigaction sa_int_anterior;
    sa_int.sa_handler = manejador_sigint;
    sigemptyset(&sa_int.sa_mask);
    if (sigaction(SIGINT, &sa_int, &sa_int_anterior) == -1) {
        perror("sigaction SIGINT");
        return 0;
    }

    // Ignorar SIGPIPE
    struct sigaction ignorar = {0};
    struct sigaction anterior_pipe;
    ignorar.sa_handler = SIG_IGN;
    sigemptyset(&ignorar.sa_mask);
    sigaction(SIGPIPE, &ignorar, &anterior_pipe);

    size_t limite = (size_t)k > plan->cantidad ? plan->cantidad : (size_t)k;

    size_t *pendientes = calloc(plan->cantidad, sizeof(*pendientes));
    size_t *cola = calloc(plan->cantidad, sizeof(*cola));
    Hijo *hijos = calloc(limite, sizeof(*hijos));
    Mensaje *resultados = calloc(plan->cantidad, sizeof(*resultados));
    
    // 0: Pendiente, 1: Exito, 2: Cancelado/Fallido
    int *estado_nodos = calloc(plan->cantidad, sizeof(*estado_nodos));

    size_t inicio = 0, fin = 0, activos = 0, procesados = 0;
    int correcto = 1;

    if (!pendientes || !cola || !hijos || !resultados || !estado_nodos) {
        perror("Memoria para ejecutar el plan");
        correcto = 0;
        goto salir;
    }

    for (size_t i = 0; i < plan->cantidad; ++i) {
        pendientes[i] = grafo->nodos[i].dependencias.cantidad;
        if (pendientes[i] == 0) {
            cola[fin++] = i;
        }
    }

    while (procesados < plan->cantidad) {
        // Verificar si la Seremi llegó (Ctrl+C)
        if (g_interrumpido) {
            printf("\n[SEREMI] ¡Inspeccion de la Seremi detectada (SIGINT)! Abortando actividades...\n");
            correcto = 0;
            goto salir;
        }

        // Crear hijos para tareas listas (omitir canceladas)
        while (activos < limite && inicio < fin) {
            size_t indice = cola[inicio++];

            if (estado_nodos[indice] == 2) {
                // Si la tarea fue cancelada previamente por un fallo en la rama
                procesados++;
                continue;
            }

            int entrada[2], salida[2];
            if (pipe(entrada) == -1 || pipe(salida) == -1) {
                perror("pipe");
                correcto = 0;
                goto salir;
            }

            pid_t pid = fork();
            if (pid == -1) {
                perror("fork");
                close(entrada[0]); close(entrada[1]);
                close(salida[0]); close(salida[1]);
                correcto = 0;
                goto salir;
            }

            if (pid == 0) {
                close(entrada[1]);
                close(salida[0]);
                for (size_t i = 0; i < activos; ++i) close(hijos[i].lectura);
                simular_actividad(plan, grafo, indice, entrada[0], salida[1]);
            }

            close(entrada[0]);
            close(salida[1]);

            hijos[activos++] = (Hijo){pid, indice, salida[0]};

            printf("[INICIO] ID=%s PID=%ld Activos=%zu/%d\n",
                   plan->actividades[indice].id, (long)pid, activos, k);
            fflush(stdout);

            const ListaIndices *deps = &grafo->nodos[indice].dependencias;
            for (size_t i = 0; i < deps->cantidad; ++i) {
                size_t origen = deps->datos[i];
                enviar_mensaje(entrada[1], &resultados[origen]);
            }
            close(entrada[1]);
        }

        if (activos == 0) {
            // Si no hay activos pero no hemos procesado todo, significa que el resto de los nodos se cancelaron por error en ramas
            if (procesados < plan->cantidad) {
                break;
            }
            fprintf(stderr, "Error: no hay actividades que puedan avanzar\n");
            correcto = 0;
            goto salir;
        }

        int estado;
        pid_t terminado;
        do {
            terminado = waitpid(-1, &estado, 0);
        } while (terminado == -1 && errno == EINTR && !g_interrumpido);

        if (g_interrumpido) {
            printf("\n[SEREMI] ¡Inspeccion de la Seremi detectada (SIGINT)! Abortando...\n");
            correcto = 0;
            goto salir;
        }

        if (terminado == -1) {
            perror("waitpid");
            correcto = 0;
            goto salir;
        }

        size_t pos = 0;
        while (pos < activos && hijos[pos].pid != terminado) ++pos;

        Hijo hijo = hijos[pos];
        size_t indice = hijo.actividad;
        hijos[pos] = hijos[--activos];

        int es_exito = WIFEXITED(estado) && WEXITSTATUS(estado) == EXIT_SUCCESS;

        if (es_exito) {
            Mensaje esperado = crear_mensaje(plan, indice);
            if (recibir_mensaje(hijo.lectura, &resultados[indice]) &&
                memcmp(resultados[indice].texto, esperado.texto, TAM_MENSAJE) == 0) {
                
                estado_nodos[indice] = 1; // Éxito
                procesados++;

                printf("[FIN OK] ID=%s PID=%ld Activos=%zu/%d\n",
                       plan->actividades[indice].id, (long)terminado, activos, k);

                const ListaIndices *siguientes = &grafo->nodos[indice].siguientes;
                for (size_t j = 0; j < siguientes->cantidad; ++j) {
                    size_t sig = siguientes->datos[j];
                    if (--pendientes[sig] == 0) {
                        cola[fin++] = sig;
                    }
                }
            } else {
                es_exito = 0;
            }
        }

        if (!es_exito) {
            printf("[ERROR EN RAMA] La actividad %s fallo. Aislando la rama...\n", plan->actividades[indice].id);
            estado_nodos[indice] = 2; // Fallido
            procesados++;

            // Cancelar en cascada solo las actividades que dependían de esta
            cancelar_subrama(grafo, indice, estado_nodos);

            // Descontar dependencias de los nodos afectados para no bloquear el bucle
            const ListaIndices *siguientes = &grafo->nodos[indice].siguientes;
            for (size_t j = 0; j < siguientes->cantidad; ++j) {
                size_t sig = siguientes->datos[j];
                if (--pendientes[sig] == 0) {
                    cola[fin++] = sig; // Se agrega a la cola pero el loop la omitirá por tener estado == 2
                }
            }
        }

        close(hijo.lectura);
        fflush(stdout);
    }

    printf("Simulacion finalizada. Procesadas: %zu/%zu actividades.\n", procesados, plan->cantidad);

salir:
    detener_hijos(hijos, activos);
    free(hijos);
    free(resultados);
    free(cola);
    free(pendientes);
    free(estado_nodos);

    sigaction(SIGPIPE, &anterior_pipe, NULL);
    sigaction(SIGINT, &sa_int_anterior, NULL);

    return correcto;
}