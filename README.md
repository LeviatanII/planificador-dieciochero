# Planificador de Dieciochero

## Estado actual
Por ahora el programa valida el límite de concurrencia K y lee el archivo de actividades

Cada linea se separa en ID, nombre, tiempo y dependencias
Se comprueban los campos básicos y se asigna una duración entre 100 y 5000 ms cuando el tiempo está vacio

Las actividades se almacenan en una lista dinámica
Por ahora, las dependencias se conservan como texto

## Funciones implementadas

- cargar_plan(): lee, separa y almacena las actividades
- mostrar_plan(): muestra las actividades almacenadas
- liberar_plan(): libera la memoria reservada

## Decisiones de diseño

Se utiliza una estructura Actividad para agrupar los datos de cada actividad y una estructura Plan para almacenar la lista completa

La lista aumenta su capacidad cuando se llena

Los campos de texto se copian para conservarlos después de leer la siguiente línea

## Compilacion
gcc -Wall -Wextra -std=c17 main.c plan.c -o planificador -lpthread

## Ejecucion
./planificador plan.txt 2
