#define _POSIX_C_SOURCE 200809L

#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/select.h>
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

typedef enum { PENDIENTE, EJECUTANDO, EXITO, FALLIDA, CANCELADA } Estado;

static volatile sig_atomic_t g_interrumpido = 0;

static void manejador_sigint(int sig)
{
    (void)sig;
    g_interrumpido = 1;
}

static void manejador_sigchld(int sig)
{
    (void)sig;
}

// SIGINT permanece bloqueada fuera de las esperas atomicas
static int cancelacion_solicitada(void)
{
    sigset_t pendientes;
    if (sigpending(&pendientes) == -1) {
        g_interrumpido = 1;
    } else if (sigismember(&pendientes, SIGINT)) {
        g_interrumpido = 1;
    }
    return g_interrumpido != 0;
}

// pselect desbloquea las senales y duerme en una sola operacion
static int esperar_evento(int escritura, const sigset_t *mascara)
{
    if (cancelacion_solicitada()) return 0;
    fd_set listos;
    FD_ZERO(&listos);
    if (escritura >= FD_SETSIZE) {
        errno = EMFILE;
        return 0;
    }
    if (escritura >= 0) FD_SET(escritura, &listos);
    int resultado = pselect(escritura + 1, NULL,
                            escritura >= 0 ? &listos : NULL,
                            NULL, NULL, mascara);
    if (resultado == -1 && errno != EINTR) return 0;
    return !cancelacion_solicitada();
}

// El extremo no bloqueante permite atender SIGINT incluso si el pipe se llena
static int enviar_insumo(int descriptor, const Mensaje *mensaje,
                         const sigset_t *mascara)
{
    for (;;) {
        if (cancelacion_solicitada()) return 0;
        if (enviar_mensaje(descriptor, mensaje)) return 1;
        if (errno != EAGAIN && errno != EWOULDBLOCK) return 0;
        if (!esperar_evento(descriptor, mascara)) return 0;
    }
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

const char *fallar_id = getenv("FALLAR_ID");

if (fallar_id != NULL && strcmp(fallar_id, actividad->id) == 0) {
    fprintf(stderr, "[PRUEBA] Actividad ID=%s provocando fallo intencional\n",
            actividad->id);
    _exit(EXIT_FAILURE);
}

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

// Marcamos al insertar para contar una sola vez incluso si convergen ramas
static size_t cancelar_subrama(const Plan *plan, const Grafo *grafo,
                               size_t indice, Estado *estados, size_t *pila)
{
    size_t usados = 0, canceladas = 0;
    pila[usados++] = indice;
    while (usados > 0) {
        size_t actual = pila[--usados];
        const ListaIndices *sig = &grafo->nodos[actual].siguientes;
        for (size_t i = 0; i < sig->cantidad; ++i) {
            size_t nodo = sig->datos[i];
            if (estados[nodo] != PENDIENTE) continue;
            estados[nodo] = CANCELADA;
            ++canceladas;
            pila[usados++] = nodo;
            printf("[CANCELADA] ID=%s por fallo en una dependencia\n",
                   plan->actividades[nodo].id);
        }
    }
    return canceladas;
}

int ejecutar_plan(const Plan *plan, const Grafo *grafo, int k)
{
    if (k <= 0 || plan->cantidad == 0 || plan->cantidad != grafo->cantidad) {
        fprintf(stderr, "Error: datos invalidos para ejecutar el plan\n");
        return 0;
    }

    g_interrumpido = 0;
    sigset_t bloqueadas, mascara_anterior, mascara_espera;
    sigemptyset(&bloqueadas);
    sigaddset(&bloqueadas, SIGINT);
    sigaddset(&bloqueadas, SIGCHLD);
    if (sigprocmask(SIG_BLOCK, &bloqueadas, &mascara_anterior) == -1) {
        perror("sigprocmask");
        return 0;
    }
    mascara_espera = mascara_anterior;
    sigdelset(&mascara_espera, SIGINT);
    sigdelset(&mascara_espera, SIGCHLD);

    struct sigaction accion = {0}, anterior_int, anterior_chld, anterior_pipe;
    int instalado_int = 0, instalado_chld = 0, instalado_pipe = 0;
    int correcto = 0;
    size_t activos = 0, inicio = 0, fin = 0;
    size_t exitosas = 0, fallidas = 0, canceladas = 0;
    size_t *pendientes = NULL, *cola = NULL, *pila = NULL;
    Hijo *hijos = NULL;
    Mensaje *resultados = NULL;
    Estado *estados = NULL;
    size_t limite = (size_t)k > plan->cantidad ? plan->cantidad : (size_t)k;

    sigemptyset(&accion.sa_mask);
    accion.sa_handler = manejador_sigint;
    if (sigaction(SIGINT, &accion, &anterior_int) == -1) goto error_senal;
    instalado_int = 1;
    accion.sa_handler = manejador_sigchld;
    accion.sa_flags = SA_NOCLDSTOP;
    if (sigaction(SIGCHLD, &accion, &anterior_chld) == -1) goto error_senal;
    instalado_chld = 1;
    accion.sa_handler = SIG_IGN;
    accion.sa_flags = 0;
    if (sigaction(SIGPIPE, &accion, &anterior_pipe) == -1) goto error_senal;
    instalado_pipe = 1;

    pendientes = calloc(plan->cantidad, sizeof(*pendientes));
    cola = calloc(plan->cantidad, sizeof(*cola));
    pila = calloc(plan->cantidad, sizeof(*pila));
    hijos = calloc(limite, sizeof(*hijos));
    resultados = calloc(plan->cantidad, sizeof(*resultados));
    estados = calloc(plan->cantidad, sizeof(*estados));
    if (!pendientes || !cola || !pila || !hijos || !resultados || !estados) {
        perror("Memoria para ejecutar el plan");
        goto salir;
    }

    for (size_t i = 0; i < plan->cantidad; ++i) {
        pendientes[i] = grafo->nodos[i].dependencias.cantidad;
        if (pendientes[i] == 0) cola[fin++] = i;
    }
    fflush(stdout);

    while (exitosas + fallidas + canceladas < plan->cantidad) {
        if (cancelacion_solicitada()) goto salir;
        while (activos < limite && inicio < fin) {
            if (cancelacion_solicitada()) goto salir;
            size_t indice = cola[inicio++];
            if (estados[indice] != PENDIENTE) continue;

            int entrada[2], salida[2];
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
            int flags = fcntl(entrada[1], F_GETFL);
            if (flags == -1 || fcntl(entrada[1], F_SETFL, flags | O_NONBLOCK) == -1) {
                perror("fcntl");
                close(entrada[0]); close(entrada[1]);
                close(salida[0]); close(salida[1]);
                goto salir;
            }
            if (cancelacion_solicitada()) {
                close(entrada[0]); close(entrada[1]);
                close(salida[0]); close(salida[1]);
                goto salir;
            }

            // Las senales siguen bloqueadas hasta registrar al hijo en el padre
            pid_t pid = fork();
            if (pid == -1) {
                perror("fork");
                close(entrada[0]); close(entrada[1]);
                close(salida[0]); close(salida[1]);
                goto salir;
            }
            if (pid == 0) {
                struct sigaction defecto = {0};
                defecto.sa_handler = SIG_DFL;
                sigemptyset(&defecto.sa_mask);
                if (sigaction(SIGINT, &defecto, NULL) == -1 ||
                    sigaction(SIGCHLD, &defecto, NULL) == -1 ||
                    sigprocmask(SIG_SETMASK, &mascara_espera, NULL) == -1) {
                    _exit(EXIT_FAILURE);
                }
                close(entrada[1]);
                close(salida[0]);
                for (size_t i = 0; i < activos; ++i) close(hijos[i].lectura);
                simular_actividad(plan, grafo, indice, entrada[0], salida[1]);
            }
            close(entrada[0]);
            close(salida[1]);
            hijos[activos++] = (Hijo){pid, indice, salida[0]};
            estados[indice] = EJECUTANDO;
            printf("[INICIO] ID=%s PID=%ld Activos=%zu/%d\n",
                   plan->actividades[indice].id, (long)pid, activos, k);
            fflush(stdout);

            const ListaIndices *deps = &grafo->nodos[indice].dependencias;
            int envio_correcto = 1;
            for (size_t i = 0; i < deps->cantidad; ++i) {
                if (!enviar_insumo(entrada[1], &resultados[deps->datos[i]],
                                   &mascara_espera)) {
                    envio_correcto = 0;
                    break;
                }
            }
            close(entrada[1]);
            if (cancelacion_solicitada()) goto salir;
            if (!envio_correcto) {
                // Recogemos despues al hijo y aislamos su rama como cualquier fallo
                if (kill(pid, SIGKILL) == -1 && errno != ESRCH) {
                    perror("kill");
                    goto salir;
                }
            }
        }

        if (cancelacion_solicitada()) goto salir;
        if (activos == 0) {
            fprintf(stderr, "Error: no hay actividades que puedan avanzar\n");
            goto salir;
        }

        int estado;
        pid_t terminado;
        for (;;) {
            if (cancelacion_solicitada()) goto salir;
            terminado = waitpid(-1, &estado, WNOHANG);
            if (terminado > 0) break;
            if (terminado == -1) {
                if (errno == EINTR) continue;
                perror("waitpid");
                goto salir;
            }
            // Si no termino ningun hijo dormimos hasta recibir una senal
            if (!esperar_evento(-1, &mascara_espera)) goto salir;
        }

        size_t pos = 0;
        while (pos < activos && hijos[pos].pid != terminado) ++pos;
        if (pos == activos) {
            fprintf(stderr, "Error: termino un hijo no registrado\n");
            goto salir;
        }
        Hijo hijo = hijos[pos];
        size_t indice = hijo.actividad;
        hijos[pos] = hijos[--activos];

        // Quitamos al hijo recogido antes de cualquier salida por cancelacion
        if (cancelacion_solicitada()) {
            close(hijo.lectura);
            goto salir;
        }
        int es_exito = WIFEXITED(estado) && WEXITSTATUS(estado) == EXIT_SUCCESS;
        if (es_exito) {
            Mensaje esperado = crear_mensaje(plan, indice);
            es_exito = recibir_mensaje(hijo.lectura, &resultados[indice]) &&
                       memcmp(resultados[indice].texto, esperado.texto, TAM_MENSAJE) == 0;
        }
        close(hijo.lectura);

        if (es_exito) {
            estados[indice] = EXITO;
            ++exitosas;
            printf("[FIN OK] ID=%s PID=%ld Activos=%zu/%d\n",
                   plan->actividades[indice].id, (long)terminado, activos, k);
            const ListaIndices *sig = &grafo->nodos[indice].siguientes;
            for (size_t i = 0; i < sig->cantidad; ++i) {
                size_t nodo = sig->datos[i];
                if (estados[nodo] == PENDIENTE && --pendientes[nodo] == 0) {
                    cola[fin++] = nodo;
                }
            }
        } else {
            estados[indice] = FALLIDA;
            ++fallidas;
            printf("[ERROR EN RAMA] ID=%s\n", plan->actividades[indice].id);
            canceladas += cancelar_subrama(plan, grafo, indice, estados, pila);
        }
        fflush(stdout);
    }

    printf("Simulacion finalizada. Procesadas: %zu/%zu actividades. "
           "Exitosas: %zu. Fallidas: %zu. Canceladas: %zu.\n",
           exitosas + fallidas + canceladas, plan->cantidad,
           exitosas, fallidas, canceladas);
    correcto = fallidas == 0;
    goto salir;

error_senal:
    perror("sigaction");
salir:
    detener_hijos(hijos, activos);
    free(hijos);
    free(resultados);
    free(cola);
    free(pila);
    free(pendientes);
    free(estados);

    // Entregamos las senales pendientes con nuestros manejadores aun instalados
    if (instalado_int && cancelacion_solicitada()) {
        printf("\n[SEREMI] Inspeccion detectada (SIGINT). Actividades abortadas\n");
        correcto = 0;
    }
    if (sigprocmask(SIG_SETMASK, &mascara_espera, NULL) == -1) correcto = 0;
    if (g_interrumpido) correcto = 0;
    if (instalado_pipe) sigaction(SIGPIPE, &anterior_pipe, NULL);
    if (instalado_chld) sigaction(SIGCHLD, &anterior_chld, NULL);
    if (instalado_int) sigaction(SIGINT, &anterior_int, NULL);
    if (sigprocmask(SIG_SETMASK, &mascara_anterior, NULL) == -1) correcto = 0;
    return correcto;
}
