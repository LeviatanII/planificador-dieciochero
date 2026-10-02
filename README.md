# Planificador Dieciochero

Tarea 1 de Sistemas Operativos.

Programa desarrollado en C que simula actividades con dependencias mediante procesos, tuberías y señales.

## Descripción

El programa lee un archivo de actividades, construye un grafo dirigido y comprueba que no existan ciclos antes de comenzar la ejecución.

Cada actividad se ejecuta en un proceso hijo creado mediante fork(). Su duración se simula utilizando nanosleep().

El proceso padre coordina las actividades y mantiene como máximo K hijos simultáneos. Una actividad comienza solamente cuando todas sus dependencias han terminado correctamente.

Los resultados se transmiten mediante pipes. Si una actividad falla, se cancelan sus dependientes directos e indirectos, mientras las ramas independientes continúan ejecutándose.

Al presionar Ctrl+C, el programa detiene las actividades, recoge los hijos, cierra los pipes y libera la memoria.

No se utilizan hilos ni mecanismos de sincronización de hilos.

## Compilación

Desde la carpeta del proyecto, ejecutar:

gcc -Wall -Wextra -std=c17 main.c plan.c grafo.c ejecutor.c ipc.c -o planificador -lpthread


## Ejecución

Ejecutar indicando la ruta del archivo y el límite de concurrencia:

./planificador plan.txt 2

En este ejemplo, plan.txt contiene las actividades y el número 2 permite mantener como máximo dos procesos hijos simultáneos.

El proceso padre realiza la coordinación y no se incluye en K.

Para interrumpir la simulación, presionar Ctrl+C en la terminal.

## Formato del archivo

Cada línea contiene cuatro campos separados por dos puntos:

ID : nombre : tiempo_ms : dependencias

El ID debe ser alfanumérico, único y no vacío. El nombre de la actividad tampoco puede estar vacío.

El tiempo se expresa en milisegundos y debe ser un entero entre 0 e INT_MAX. Si el campo está vacío, se asigna una duración aleatoria entre 100 y 5000 milisegundos.

Las dependencias se indican mediante sus IDs separados por comas. Se aceptan tanto 1, 2 como [1, 2]. Un campo vacío o con [] indica que la actividad no tiene requisitos.

Se permiten líneas vacías y comentarios que comiencen con el carácter numeral.

Se rechazan archivos vacíos, campos inválidos, IDs repetidos, dependencias inexistentes o repetidas, autodependencias y ciclos.

## Organización del código

main.c contiene el punto de entrada del programa. Valida los argumentos, carga el plan, construye el grafo, llama al ejecutor y libera las estructuras.

plan.c y plan.h definen las actividades y el plan. Implementan la lectura del archivo, el almacenamiento de los datos y la liberación de memoria.

grafo.c y grafo.h representan las conexiones entre actividades. Resuelven las dependencias y comprueban que el grafo no tenga ciclos.

ejecutor.c y ejecutor.h implementan la planificación, la creación y recogida de hijos, el límite K, el aislamiento de fallos y el manejo de señales.

ipc.c e ipc.h definen los mensajes e implementan su envío y recepción mediante pipes.

plan.txt contiene un plan de ejemplo.

## Funciones implementadas

cargar_plan(): lee el archivo, valida sus campos y almacena las actividades. Asigna una duración aleatoria cuando no se especifica.

mostrar_plan(): muestra las actividades cargadas, sus tiempos y sus dependencias.

liberar_plan(): libera la memoria utilizada por las actividades y el plan.

construir_grafo(): relaciona las dependencias con sus actividades. Detecta IDs repetidos, referencias inexistentes, autodependencias y dependencias duplicadas.

validar_dag(): comprueba mediante el algoritmo de Kahn que el grafo no tenga ciclos.

liberar_grafo(): libera las listas de conexiones y los nodos del grafo.

ejecutar_plan(): coordina la ejecución, administra la cola de actividades listas, respeta el límite K y procesa los resultados de los hijos.

simular_actividad(): recibe y comprueba los insumos en el hijo, simula la duración de la actividad y envía su resultado.

crear_mensaje(): construye un mensaje acotado con el ID y el índice interno de la actividad.

enviar_mensaje(): escribe un mensaje completo de 128 bytes y comprueba los errores de escritura.

recibir_mensaje(): lee un mensaje completo de 128 bytes y comprueba que contenga una terminación de texto válida.

cancelar_subrama(): recorre los descendientes de una actividad fallida, cancela los pendientes y devuelve la cantidad de nuevas cancelaciones.

detener_hijos(): envía SIGKILL a los hijos registrados, los recoge mediante waitpid() y cierra sus descriptores.

manejador_sigint(): marca que se recibió una solicitud de cancelación.

manejador_sigchld(): permite despertar la espera del padre cuando termina un hijo.

cancelacion_solicitada(): comprueba la marca de cancelación y la existencia de una señal SIGINT pendiente.

esperar_evento(): utiliza pselect() para esperar una señal o disponibilidad de escritura sin realizar espera activa.

enviar_insumo(): envía un resultado al hijo dependiente y espera espacio si el pipe está lleno, permitiendo atender SIGINT.

## Representación del grafo

Se utilizan listas de adyacencia en ambos sentidos. Cada nodo conoce sus requisitos y las actividades que dependen de él.

Los IDs se resuelven mediante una tabla ordenada y búsqueda binaria. El algoritmo de Kahn permite detectar ciclos antes de crear procesos.

Esta representación permite actualizar las actividades dependientes cuando termina un requisito y recorrer los descendientes cuando ocurre un fallo.

## Planificación y concurrencia

El padre mantiene una cola de actividades listas y un contador de requisitos pendientes para cada actividad.

Cuando un hijo termina correctamente, disminuyen los contadores de sus dependientes. Una actividad se incorpora a la cola cuando su contador llega a cero.

El cupo de un hijo se libera después de recogerlo mediante waitpid(). De esta forma, nunca se mantienen más de K hijos registrados simultáneamente.

## Comunicación mediante pipes

Cada hijo utiliza un pipe de entrada para recibir insumos y otro de salida para entregar su resultado.

El padre guarda los resultados recibidos y los reenvía cuando puede iniciar una actividad dependiente. Esta intermediación permite respetar K sin crear todos los procesos de antemano.

Los mensajes ocupan 128 bytes. Incluyen un ID acotado y el índice interno del nodo para distinguir actividades incluso cuando sus IDs largos comparten el mismo prefijo.

Cada hijo envía un único resultado que cabe completo en su pipe de salida. Por ello, el padre puede recoger al hijo antes de leer ese resultado.

Los extremos que no se utilizan se cierran. Los hijos también cierran los descriptores heredados correspondientes a otros hijos.

## Espera sin busy-waiting

El padre consulta las terminaciones mediante waitpid() con WNOHANG. Si no hay hijos terminados, se bloquea en pselect() hasta recibir una señal.

No se realizan consultas continuas ni esperas periódicas mediante temporizadores.

El extremo del pipe utilizado por el padre para enviar insumos es no bloqueante. Si está lleno, pselect() espera hasta que se pueda escribir o llegue una señal.

Como los mensajes son menores que PIPE_BUF, la escritura no bloqueante transmite el bloque completo o informa que no existe espacio suficiente.

## Manejo de Ctrl+C

El padre bloquea SIGINT y SIGCHLD mientras modifica sus estructuras y registra los hijos.

pselect() cambia temporalmente la máscara de señales y comienza la espera en una operación atómica. Esto evita perder una notificación entre comprobar el estado y quedar esperando.

El manejador de SIGINT solamente modifica una variable volatile sig_atomic_t. La terminación de procesos y la liberación de recursos se realizan fuera del manejador.

Al cancelar, el padre detiene y recoge todos los hijos registrados. Los hijos recuperan el comportamiento normal de SIGINT.

SIGPIPE se ignora para tratar la desaparición de un lector como un error de escritura y realizar la limpieza correspondiente.

Al salir se restauran los manejadores y la máscara de señales.

## Aislamiento de fallos

Cada actividad tiene uno de estos estados: pendiente, ejecutando, exitosa, fallida o cancelada.

Una actividad falla si termina anormalmente, devuelve un código de error o entrega un resultado inválido.

Sus descendientes pendientes se cancelan mediante un recorrido iterativo. Cada actividad se marca al incorporarse al recorrido, por lo que se cuenta una sola vez aunque varias ramas converjan en ella.

Las actividades independientes continúan ejecutándose.

La simulación termina cuando la suma de exitosas, fallidas y canceladas alcanza la cantidad total de actividades.

## Administración de recursos

Las estructuras se reservan dinámicamente y se liberan al finalizar.

Solo se crean los procesos y pipes necesarios para las actividades activas. Se verificaron planes de 10.000 actividades, tanto en cadena como con actividades independientes que convergen en una actividad final.

La propagación de cancelaciones utiliza una pila iterativa para evitar una recursión profunda.

Los errores globales, como falta de memoria o imposibilidad de crear procesos o pipes, provocan una terminación controlada con limpieza de los hijos existentes.

El valor utilizable de K depende también de los límites de procesos y descriptores del sistema. La espera de escritura mediante pselect() requiere un descriptor menor que FD_SETSIZE.

## Mensajes de salida

INICIO: indica que se creó una actividad. Muestra su ID, PID y la cantidad de hijos registrados.

FIN OK: indica que la actividad terminó correctamente.

ERROR EN RAMA: indica que una actividad falló.

CANCELADA: indica que una actividad no se ejecutará por un fallo en sus dependencias.

SEREMI: indica una cancelación global por SIGINT.

Cuando la simulación termina sin una interrupción global, se muestra el total de actividades procesadas y las cantidades de exitosas, fallidas y canceladas.

Una actividad procesada no necesariamente terminó con éxito: también puede haber fallado o haber sido cancelada.

## Códigos de retorno

0 indica que todas las actividades terminaron correctamente.

1 indica que hubo actividades fallidas, una interrupción o un error durante la ejecución.

2 indica que los argumentos, el archivo o el grafo son inválidos.

## Pruebas de ejecución

Se realizaron pruebas para verificar el funcionamiento del planificador de actividades, el control de concurrencia y el manejo de errores.

## Pruebas realizadas
Ejecución normal: se verificó la ejecución de actividades respetando sus dependencias con límites de concurrencia K = 1 y K = 2.

Pruebas de carga: se ejecutaron 50 actividades independientes utilizando distintos límites de concurrencia (K = 1, K = 2, K = 4, K = 8 y K = 50).

Fallo de actividades: se provocaron fallos intencionales mediante la variable de entorno FALLAR_ID, comprobando la detección de errores y la cancelación de las actividades dependientes.

Cancelación por señal: se probó la interrupción de la ejecución mediante Ctrl + C (SIGINT), verificando la terminación de los procesos hijos.

Limpieza de procesos: se comprobó mediante ps -ef que no quedaran procesos del planificador ejecutándose tras la finalización o cancelación.

Recursos limitados: se probó la ejecución con un límite reducido de descriptores de archivos.

## Resultados

Las pruebas realizadas presentaron el comportamiento esperado. El planificador respetó los límites de concurrencia, detectó los fallos intencionales, canceló las actividades afectadas por dependencias y gestionó la interrupción de la ejecución.