#define _POSIX_C_SOURCE 200809L

#include <errno.h>
#include <limits.h>
#include <stddef.h>
#include <string.h>
#include <unistd.h>
#include "ipc.h"

// Un resultado debe caber completo en un pipe vacio
_Static_assert(TAM_MENSAJE <= _POSIX_PIPE_BUF, "Mensaje demasiado grande");

int enviar_mensaje(int descriptor, const Mensaje *mensaje)
{
    size_t enviados = 0;

    // Repetimos si la escritura se interrumpe o envia solo una parte
    while (enviados < TAM_MENSAJE) {
        ssize_t cantidad = write(descriptor, mensaje->texto + enviados,
                                 TAM_MENSAJE - enviados);

        if (cantidad == -1 && errno == EINTR) {
            continue;
        }

        if (cantidad <= 0) {
            if (cantidad == 0) {
                errno = EIO;
            }
            return 0;
        }

        enviados += (size_t)cantidad;
    }

    return 1;
}

int recibir_mensaje(int descriptor, Mensaje *mensaje)
{
    size_t recibidos = 0;

    // Los pipes transportan bytes y una lectura puede llegar incompleta
    while (recibidos < TAM_MENSAJE) {
        ssize_t cantidad = read(descriptor, mensaje->texto + recibidos,
                                TAM_MENSAJE - recibidos);

        if (cantidad == -1 && errno == EINTR) {
            continue;
        }

        if (cantidad <= 0) {
            if (cantidad == 0) {
                errno = EIO;
            }
            return 0;
        }

        recibidos += (size_t)cantidad;
    }

    // Comprobamos que el texto tenga un final dentro del bloque recibido
    if (memchr(mensaje->texto, '\0', TAM_MENSAJE) == NULL) {
        errno = EIO;
        return 0;
    }

    return 1;
}
