# Planificador de Dieciochero

## Estado actual
Por ahora recibe la ruta del archivo de actividades y el limite de concurrencia K. Valida que K sea un entero positivo y muestra el contenido del archivo linea por linea

## Compilacion
gcc -Wall -Wextra -std=c17 main.c plan.c -o planificador -lpthread

## Ejecucion
./planificador plan.txt 2
