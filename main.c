#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <limits.h>
#include <time.h>
#include "plan.h"
#include "grafo.h"
#include "ejecutor.h"

int main(int argc, char *argv[])
{
    // Comprobamos que se reciban la ruta del archivo y el valor de K
    if (argc != 3) {
        fprintf(stderr, "Uso: %s plan.txt K\n", argv[0]);
        return 2;
    }

    char *fin;
    errno = 0;
    long valor_k = strtol(argv[2], &fin, 10);

    // Rechazamos valores que no sean enteros positivos validos
    if (errno == ERANGE || fin == argv[2] || *fin != '\0' ||
        valor_k <= 0 || valor_k > INT_MAX) {
        fprintf(stderr, "Error: K debe ser un entero positivo valido\n");
        return 2;
    }

    int k = (int)valor_k;

    // Inicializamos la semilla para los tiempos que no vengan en el archivo
    srand((unsigned int)time(NULL));

    Plan plan = {0};
    Grafo grafo = {0};

    if (!cargar_plan(argv[1], &plan)) {
        return 2;
    }

    // Conectamos las actividades y comprobamos que no existan ciclos
    if (!construir_grafo(&plan, &grafo)) {
        liberar_plan(&plan);
        return 2;
    }

    printf("Limite de concurrencia: %d\n", k);
    printf("Actividades cargadas: %zu\n", plan.cantidad);

    mostrar_plan(&plan);
    printf("Grafo validado: no hay ciclos\n");

    // Ejecutamos las actividades respetando las dependencias y el limite K
    int correcto = ejecutar_plan(&plan, &grafo, k);

    // Liberamos toda la memoria reservada antes de terminar
    liberar_grafo(&grafo);
    liberar_plan(&plan);

    // Diferenciamos una ejecucion exitosa de un error durante la ejecucion
    return correcto ? 0 : 1;
}
