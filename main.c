#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <limits.h>
#include <time.h>

#include "plan.h"

int main(int argc, char *argv[])
{
    if (argc != 3) {
        fprintf(stderr, "Uso: %s plan.txt K\n", argv[0]);
        return 2;
    }

    char *fin;
    errno = 0;
    long valor_k = strtol(argv[2], &fin, 10);

    if (errno == ERANGE || fin == argv[2] || *fin != '\0' ||
        valor_k <= 0 || valor_k > INT_MAX) {
        fprintf(stderr, "Error: K debe ser un entero positivo valido.\n");
        return 2;
    }

    int k = (int)valor_k;
    srand((unsigned int)time(NULL));

    Plan plan = {0};

    if (!cargar_plan(argv[1], &plan)) {
        return 2;
    }

    printf("Limite de concurrencia: %d\n", k);
    printf("Actividades cargadas: %zu\n", plan.cantidad);
    mostrar_plan(&plan);

    liberar_plan(&plan);
    return 0;
}
