/*
 * servidor.h - Funcionalidad común de los dos servidores:
 * búsqueda secuencial en el CSV y manejo de señales de terminación.
 */
#ifndef SERVIDOR_H
#define SERVIDOR_H

#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "comun.h"

/* La pone en 1 el manejador de señales para pedir una salida ordenada */
static volatile sig_atomic_t terminar = 0;

static void manejar_senal(int senal) {
    (void)senal;
    terminar = 1;
}

/* SIGINT (Ctrl+C) y SIGTERM solo marcan la bandera, para que el servidor
 * alcance a liberar sus recursos IPC. No se usa SA_RESTART: así las
 * llamadas bloqueadas (open, read, sem_wait) retornan con EINTR.
 * SIGPIPE se ignora para que escribir a una FIFO sin lector devuelva
 * el error EPIPE en vez de matar al proceso. */
static inline int instalar_senales(void) {
    struct sigaction sa;

    memset(&sa, 0, sizeof sa);
    sa.sa_handler = manejar_senal;

    if (sigaction(SIGINT, &sa, NULL) == -1 || sigaction(SIGTERM, &sa, NULL) == -1)
        return -1;
    
    return signal(SIGPIPE, SIG_IGN) == SIG_ERR ? -1 : 0;
}

/* Indica si una línea del CSV cumple todos los criterios no vacíos */
static inline int coincide(const char *linea, const struct solicitud *sol) {
    const char *criterios[] = { sol->entidad, sol->fecha, sol->municipio };
    const int columnas[] = { COL_ENTIDAD, COL_FECHA, COL_MUNICIPIO };
    char valor[MAX_CAMPO];

    for (int i = 0; i < 3; i++) {
        if (criterios[i][0] == '\0')
            continue;

        campo(linea, columnas[i], valor, sizeof valor);

        if (strcmp(valor, criterios[i]) != 0)
            return 0;
    }

    return 1;
}

/* Recorre el CSV completo desde disco, línea por línea. Copia en `lineas`
 * los primeros MAX_RESULTADOS registros coincidentes y devuelve el total
 * de coincidencias, o -1 si el archivo no se pudo leer. Si llega una señal
 * de terminación abandona el recorrido (quien llama debe revisar `terminar`).
 * En memoria solo hay una línea a la vez (el buffer de getline). */
static inline long buscar(const char *ruta, const struct solicitud *sol,
                          char lineas[][MAX_LINEA])
{
    FILE *archivo = fopen(ruta, "r");
    char *linea = NULL; /* buffer dinámico que reserva getline() */
    size_t capacidad = 0;
    long total = 0;

    if (archivo == NULL)
        return -1;

    /* El primer getline lee el encabezado, que se descarta */
    if (getline(&linea, &capacidad, archivo) == -1)
        total = -1;

    while (total != -1 && !terminar && getline(&linea, &capacidad, archivo) != -1) {
        if (!coincide(linea, sol))
            continue;

        if (total < MAX_RESULTADOS) {
            linea[strcspn(linea, "\r\n")] = '\0';
            snprintf(lineas[total], MAX_LINEA, "%s", linea);
        }

        total++;
    }

    if (ferror(archivo))
        total = -1;

    free(linea);

    if (fclose(archivo) == EOF)
        total = -1;

    return total;
}

#endif
