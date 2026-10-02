# Planificador de Dieciochero

## Estado actual
El programa carga las actividades, valida el grafo y ejecuta cada actividad mediante un proceso hijo, respetando el limite K

Cada hijo recibe por un pipe los mensajes de sus dependencias

Al terminar, envia su resultado al padre mediante otro pipe

El padre conserva los resultados recibidos y los reenvia a las actividades dependientes cuando estas comienzan

Los mensajes se transmiten en bloques de 128 bytes y se comprueba que correspondan a las actividades esperadas

Ante un error de ejecucion, esta version detiene y recoge los hijos restantes

Quedan pendientes el aislamiento de errores por ramas y el manejo controlado de Ctrl+C

## Funciones implementadas

- cargar_plan(): lee y almacena las actividades
- mostrar_plan(): muestra los datos cargados
- liberar_plan(): libera la memoria del plan
- construir_grafo(): conecta las actividades y valida sus dependencias
- validar_dag(): comprueba internamente que no existan ciclos
- liberar_grafo(): libera la memoria del grafo
- simular_actividad(): recibe insumos, simula la actividad y envia su resultado
- crear_mensaje(): construye el texto acotado que identifica una actividad
- enviar_mensaje(): escribe un mensaje completo en un pipe
- recibir_mensaje(): lee un mensaje completo desde un pipe

## Decisiones de diseño

Se utilizan dos pipes por hijo: uno para recibir insumos y otro para entregar su resultado

El padre actua como intermediario y conserva los mensajes hasta que pueda crear los procesos dependientes respetando K

Cada hijo escribe un unico resultado de 128 bytes en su pipe de salida

Este resultado cabe completo en el pipe, por lo que el padre puede recoger al hijo antes de leerlo

Los mensajes incluyen el indice interno de la actividad para distinguir incluso IDs largos que compartan el mismo prefijo

## Compilacion
gcc -Wall -Wextra -std=c17 main.c plan.c grafo.c ejecutor.c ipc.c -o planificador -lpthread

## Ejecucion
./planificador plan.txt 2
