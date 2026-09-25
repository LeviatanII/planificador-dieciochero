# Planificador de Dieciochero

## Estado actual
Primera version, solo recibe la ruta del archivo de actividades y el limite de concurrencia K

## Compilacion
gcc -Wall -Wextra -std=c17 main.c plan.c -o planificador -lpthread

## Ejecucion
./planificador plan.txt 2
