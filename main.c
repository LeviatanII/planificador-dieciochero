#include <stdio.h>

int main(int argc, char *argv[]) {
    if (argc != 3) {
        fprintf(stderr, "Uso: %s plan.txt K\n", argv[0]);
        return 2;
    }

    printf("Planificador Dieciochero\n");
    printf("Archivo de actividades: %s\n", argv[1]);
    printf("Limite de concurrencia: %s\n", argv[2]);

    return 0;

}
