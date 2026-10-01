#define _POSIX_C_SOURCE 200809L

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <errno.h>
#include <limits.h>
#include <stdint.h>
#include <sys/types.h>

#include "plan.h"

// Quita espacios al principio y al final de un campo
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

// Conserva los campos vacios y exige exactamente cuatro campos
static int separar_linea(char *linea, char *campos[4], int *tiempo)
{
    char *inicio = linea;

    for (int i = 0; i < 3; ++i) {
        char *separador = strchr(inicio, ':');
        if (separador == NULL) {
            return 0;
        }

        *separador = '\0';
        campos[i] = recortar(inicio);
        inicio = separador + 1;
    }

    if (strchr(inicio, ':') != NULL) {
        return 0;
    }

    campos[3] = recortar(inicio);

    if (*campos[0] == '\0' || *campos[1] == '\0') {
        return 0;
    }

    for (const char *p = campos[0]; *p != '\0'; ++p) {
        if (!isalnum((unsigned char)*p)) {
            return 0;
        }
    }

    if (*campos[2] == '\0') {
        *tiempo = 100 + rand() % 4901;
    } else {
        char *fin;
        errno = 0;
        long valor = strtol(campos[2], &fin, 10);

        if (errno == ERANGE || fin == campos[2] || *fin != '\0' ||
            valor < 0 || valor > INT_MAX) {
            return 0;
        }

        *tiempo = (int)valor;
    }

    return 1;
}

// Amplia la lista cuando se llena y guarda copias de los campos
static int agregar_actividad(Plan *plan, char *campos[4], int tiempo)
{
    if (plan->cantidad == plan->capacidad) {
        size_t nueva = plan->capacidad == 0 ? 16 : plan->capacidad * 2;

        if (nueva < plan->capacidad || nueva > SIZE_MAX / sizeof(Actividad)) {
            errno = ENOMEM;
            return 0;
        }

        Actividad *temporal = realloc(plan->actividades,
                                      nueva * sizeof(Actividad));

        if (temporal == NULL) {
            return 0;
        }

        plan->actividades = temporal;
        plan->capacidad = nueva;
    }

    Actividad *actividad = &plan->actividades[plan->cantidad++];
    *actividad = (Actividad){0};

    actividad->id = strdup(campos[0]);
    actividad->nombre = strdup(campos[1]);
    actividad->tiempo_ms = tiempo;
    actividad->dependencias = strdup(campos[3]);

    return actividad->id != NULL &&
           actividad->nombre != NULL &&
           actividad->dependencias != NULL;
}

void liberar_plan(Plan *plan)
{
    for (size_t i = 0; i < plan->cantidad; ++i) {
        free(plan->actividades[i].id);
        free(plan->actividades[i].nombre);
        free(plan->actividades[i].dependencias);
    }

    free(plan->actividades);
    *plan = (Plan){0};
}

// Recibe un Plan inicializado con {0}. Si falla, libera lo cargado
int cargar_plan(const char *ruta, Plan *plan)
{
    FILE *archivo = fopen(ruta, "r");

    if (archivo == NULL) {
        perror(ruta);
        return 0;
    }

    char *linea = NULL;
    size_t capacidad_linea = 0;
    size_t numero_linea = 0;
    ssize_t leidos;
    int correcto = 1;

    while ((leidos = getline(&linea, &capacidad_linea, archivo)) != -1) {
        ++numero_linea;

        if (memchr(linea, '\0', (size_t)leidos) != NULL) {
            fprintf(stderr, "Linea %zu: contiene un byte nulo.\n",
                    numero_linea);
            correcto = 0;
            break;
        }

        char *texto = recortar(linea);

        if (*texto == '\0' || *texto == '#') {
            continue;
        }

        char *campos[4];
        int tiempo;

        if (!separar_linea(texto, campos, &tiempo)) {
            fprintf(stderr,
                    "Linea %zu invalida: revise ID, nombre, tiempo y campos.\n",
                    numero_linea);
            correcto = 0;
            break;
        }

        if (!agregar_actividad(plan, campos, tiempo)) {
            perror("No se pudo reservar memoria para la actividad");
            correcto = 0;
            break;
        }
    }

    if (correcto && (ferror(archivo) || !feof(archivo))) {
        perror("Error al leer el archivo");
        correcto = 0;
    }

    free(linea);

    if (fclose(archivo) == EOF) {
        perror("Error al cerrar el archivo");
        correcto = 0;
    }

    if (correcto && plan->cantidad == 0) {
        fprintf(stderr, "El archivo no contiene actividades.\n");
        correcto = 0;
    }

    if (!correcto) {
        liberar_plan(plan);
    }

    return correcto;
}

void mostrar_plan(const Plan *plan)
{
    for (size_t i = 0; i < plan->cantidad; ++i) {
        const Actividad *actividad = &plan->actividades[i];

        printf("ID=%s | Nombre=%s | Tiempo=%d ms | Dependencias=%s\n",
               actividad->id,
               actividad->nombre,
               actividad->tiempo_ms,
               *actividad->dependencias != '\0'
                   ? actividad->dependencias : "(ninguna)");
    }
}
