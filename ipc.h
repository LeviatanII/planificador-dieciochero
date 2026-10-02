#ifndef IPC_H
#define IPC_H

// Todos los mensajes ocupan un bloque fijo de 128 bytes
#define TAM_MENSAJE 128

typedef struct {
    char texto[TAM_MENSAJE];
} Mensaje;

int enviar_mensaje(int descriptor, const Mensaje *mensaje);
int recibir_mensaje(int descriptor, Mensaje *mensaje);

#endif
