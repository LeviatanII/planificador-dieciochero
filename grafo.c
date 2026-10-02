#define _POSIX_C_SOURCE 200809L

#include <ctype.h>
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "grafo.h"

// Relaciona cada ID con su posicion original en el plan
typedef struct {
    const char *id;
    size_t indice;
} ReferenciaId;

static int comparar_ids(const void *a, const void *b)
{
    const ReferenciaId *primero = a;
    const ReferenciaId *segundo = b;
    return strcmp(primero->id, segundo->id);
}

static char *recortar(char *texto)
{
    while (isspace((unsigned char)*texto)) {
        ++texto;
    }

    size_t largo = strlen(texto);

    while (largo > 0 && isspace((unsigned char)texto[largo - 1])) {
        texto[--largo] = '\0';
    }

    return texto;
}

static int agregar_indice(ListaIndices *lista, size_t indice)
{
    // Aumentamos la capacidad solo cuando la lista se llena
    if (lista->cantidad == lista->capacidad) {
        size_t nueva = lista->capacidad == 0 ? 4 : lista->capacidad * 2;

        if (nueva < lista->capacidad || nueva > SIZE_MAX / sizeof(size_t)) {
            errno = ENOMEM;
            return 0;
        }

        size_t *temporal = realloc(lista->datos, nueva * sizeof(size_t));

        if (temporal == NULL) {
            return 0;
        }

        lista->datos = temporal;
        lista->capacidad = nueva;
    }

    lista->datos[lista->cantidad++] = indice;
    return 1;
}

void liberar_grafo(Grafo *grafo)
{
    // Primero liberamos las listas de cada nodo y luego el arreglo de nodos
    for (size_t i = 0; i < grafo->cantidad; ++i) {
        free(grafo->nodos[i].dependencias.datos);
        free(grafo->nodos[i].siguientes.datos);
    }

    free(grafo->nodos);
    *grafo = (Grafo){0};
}

static int validar_dag(const Grafo *grafo)
{
    size_t *pendientes = calloc(grafo->cantidad, sizeof(size_t));
    size_t *cola = calloc(grafo->cantidad, sizeof(size_t));

    if (pendientes == NULL || cola == NULL) {
        perror("Memoria para validar el grafo");
        free(pendientes);
        free(cola);
        return 0;
    }

    size_t inicio = 0;
    size_t fin = 0;

    for (size_t i = 0; i < grafo->cantidad; ++i) {
        // Copiamos los contadores para conservar el grafo original
        pendientes[i] = grafo->nodos[i].dependencias.cantidad;

        if (pendientes[i] == 0) {
            cola[fin++] = i;
        }
    }

    // Kahn recorre primero los nodos que no tienen requisitos pendientes
    while (inicio < fin) {
        size_t actual = cola[inicio++];
        const ListaIndices *siguientes = &grafo->nodos[actual].siguientes;

        for (size_t j = 0; j < siguientes->cantidad; ++j) {
            size_t siguiente = siguientes->datos[j];

            if (--pendientes[siguiente] == 0) {
                cola[fin++] = siguiente;
            }
        }
    }

    // Si quedaron nodos sin revisar existe al menos un ciclo
    int correcto = inicio == grafo->cantidad;

    free(pendientes);
    free(cola);

    if (!correcto) {
        fprintf(stderr, "Error: el plan contiene un ciclo de dependencias\n");
    }

    return correcto;
}

// Recibe un grafo inicializado con {0} y lo libera si ocurre un error
int construir_grafo(const Plan *plan, Grafo *grafo)
{
    ReferenciaId *tabla = NULL;
    size_t *vistos = NULL;
    char *copia = NULL;
    int correcto = 0;

    if (plan->cantidad == 0) {
        fprintf(stderr, "Error: el plan esta vacio\n");
        return 0;
    }

    tabla = calloc(plan->cantidad, sizeof(*tabla));
    vistos = calloc(plan->cantidad, sizeof(*vistos));
    grafo->nodos = calloc(plan->cantidad, sizeof(*grafo->nodos));

    if (tabla == NULL || vistos == NULL || grafo->nodos == NULL) {
        perror("Memoria para construir el grafo");
        goto salir;
    }

    grafo->cantidad = plan->cantidad;

    // Ordenamos una tabla auxiliar sin cambiar el orden de las actividades
    for (size_t i = 0; i < plan->cantidad; ++i) {
        tabla[i] = (ReferenciaId){plan->actividades[i].id, i};
    }

    qsort(tabla, plan->cantidad, sizeof(*tabla), comparar_ids);

    for (size_t i = 1; i < plan->cantidad; ++i) {
        if (strcmp(tabla[i - 1].id, tabla[i].id) == 0) {
            fprintf(stderr, "Error: ID repetido: %s\n", tabla[i].id);
            goto salir;
        }
    }

    for (size_t i = 0; i < plan->cantidad; ++i) {
        const Actividad *actividad = &plan->actividades[i];

        // Trabajamos sobre una copia para conservar el texto original
        copia = strdup(actividad->dependencias);

        if (copia == NULL) {
            perror("Memoria para leer dependencias");
            goto salir;
        }

        char *texto = recortar(copia);
        size_t largo = strlen(texto);

        // Aceptamos tanto A,B como [A,B] y tambien una lista vacia
        if (*texto == '[') {
            if (largo < 2 || texto[largo - 1] != ']') {
                fprintf(stderr, "Error: corchetes invalidos en %s\n",
                        actividad->id);
                goto salir;
            }

            texto[largo - 1] = '\0';
            texto = recortar(texto + 1);
        }

        if (*texto != '\0') {
            for (;;) {
                // Cortamos por comas sin ocultar dependencias vacias
                char *coma = strchr(texto, ',');

                if (coma != NULL) {
                    *coma = '\0';
                }

                char *id = recortar(texto);

                if (*id == '\0') {
                    fprintf(stderr, "Error: dependencia vacia en %s\n",
                            actividad->id);
                    goto salir;
                }

                // La busqueda binaria encuentra el ID en la tabla ordenada
                ReferenciaId clave = {id, 0};

                const ReferenciaId *encontrada = bsearch(
                    &clave, tabla, plan->cantidad,
                    sizeof(*tabla), comparar_ids);

                if (encontrada == NULL) {
                    fprintf(stderr,
                            "Error: %s depende de un ID inexistente: %s\n",
                            actividad->id, id);
                    goto salir;
                }

                size_t requisito = encontrada->indice;

                if (requisito == i) {
                    fprintf(stderr, "Error: %s depende de si misma\n",
                            actividad->id);
                    goto salir;
                }

                // La marca i + 1 detecta repeticiones dentro de esta actividad
                if (vistos[requisito] == i + 1) {
                    fprintf(stderr,
                            "Error: dependencia repetida en %s: %s\n",
                            actividad->id, id);
                    goto salir;
                }

                vistos[requisito] = i + 1;

                // Guardamos quien necesita a quien en ambos sentidos
                if (!agregar_indice(&grafo->nodos[i].dependencias, requisito) ||
                    !agregar_indice(&grafo->nodos[requisito].siguientes, i)) {
                    perror("Memoria para conectar actividades");
                    goto salir;
                }

                if (coma == NULL) {
                    break;
                }

                texto = coma + 1;
            }
        }

        free(copia);
        copia = NULL;
    }

    correcto = validar_dag(grafo);

salir:
    // Todos los caminos de salida liberan la memoria auxiliar
    free(copia);
    free(tabla);
    free(vistos);

    if (!correcto) {
        liberar_grafo(grafo);
    }

    return correcto;
}
