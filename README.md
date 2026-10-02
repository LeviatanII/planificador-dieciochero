# Planificador de Dieciochero

## Estado actual
El programa valida el argumento K, carga las actividades y construye un grafo con sus dependencias

Detecta IDs repetidos, dependencias inexistentes, dependencias repetidas, actividades que dependen de si mismas y ciclos

Cuando una actividad no indica su tiempo, se asigna una duracion entre 100 y 5000 ms

Por ahora se realiza la carga y validacion del plan

La ejecucion mediante procesos y el control de concurrencia estan pendientes.

## Funciones implementadas

- cargar_plan(): lee y almacena las actividades
- mostrar_plan(): muestra los datos cargados
- liberar_plan(): libera la memoria del plan
- construir_grafo(): conecta las actividades y valida sus dependencias
- validar_dag(): comprueba internamente que no existan ciclos
- liberar_grafo(): libera la memoria del grafo

## Decisiones de diseño

La lectura del archivo se implementa en plan.c y la construccion del grafo en grafo.c

El grafo utiliza listas dinamicas para guardar las dependencias y las actividades siguientes de cada nodo

Los IDs se buscan mediante una tabla auxiliar ordenada

Esto permite resolver referencias a cualquier actividad del archivo

La deteccion de ciclos utiliza el algoritmo de Kahn

Sus contadores son temporales para conservar el grafo original

## Compilacion
gcc -Wall -Wextra -std=c17 main.c plan.c grafo.c -o planificador -lpthread

## Ejecucion
./planificador plan.txt 2
