#define _POSIX_C_SOURCE 200809L //habilita funciones POSIX para utilizar getline

#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>

#include "plan.h"

int mostrar_plan(const char *ruta)
{
    FILE *archivo = fopen(ruta, "r"); //fopen abre el archivo en modo lectura, si no devuelve NULL
    if (archivo == NULL) {
        perror(ruta);       //motivo del error en caso de existir
        return 0;
    }

    char *linea = NULL;
    size_t capacidad = 0;
    size_t numero_linea = 0;
    ssize_t leidos;

    while ((leidos = getline(&linea, &capacidad, archivo)) != -1) { //lee una linea del archivo y reserva o amplia la memoria necesaria para guardarla
        ++numero_linea;                 //guarda en leidos el numero de caracteres leidos
        printf("%zu | %s", numero_linea, linea);

        if (leidos > 0 && linea[leidos - 1] != '\n') {
            putchar('\n');
        }
    }

    int correcto = 1;
    if (ferror(archivo) || !feof(archivo)) {
        perror("Error al leer el archivo");
        correcto = 0;
    }

    free(linea);        //libera la memoria reservada por getline
    if (fclose(archivo) == EOF) {       //cierra el archivo cuando se termina y verifica si hubo un error
        perror("Error al cerrar el archivo");
        correcto = 0;
    }

    return correcto;
}
