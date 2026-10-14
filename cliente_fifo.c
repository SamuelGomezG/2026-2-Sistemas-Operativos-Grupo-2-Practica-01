/*
 * cliente_fifo.c - Parte A: cliente que envía consultas por FIFO.
 * Uso: ./cliente_fifo   (con el servidor ya en ejecución)
 */
#include <errno.h>
#include <fcntl.h>
#include <signal.h>

#include "cliente.h"

int main(void)
{
    struct respuesta resp;
    int estado = 0;

    /* Si el servidor desaparece, write() debe fallar con EPIPE en vez de
     * matar al cliente con la señal SIGPIPE */
    if (signal(SIGPIPE, SIG_IGN) == SIG_ERR) {
        perror("signal");
        return 1;
    }

    /* Con O_NONBLOCK, abrir para escritura una FIFO sin lector falla de
     * inmediato (ENXIO) en lugar de bloquearse; así se detecta que no hay
     * servidor. ENOENT indica que la FIFO ni siquiera existe. */
    int fd_sol = open(FIFO_SOLICITUD, O_WRONLY | O_NONBLOCK);
    if (fd_sol == -1) {
        if (errno == ENOENT || errno == ENXIO)
            fprintf(stderr, "El servidor no está en ejecución.\n");
        else
            perror(FIFO_SOLICITUD);
        return 1;
    }
    /* Ya conectados, se vuelve al modo bloqueante normal */
    if (fcntl(fd_sol, F_SETFL, O_WRONLY) == -1) {
        perror("fcntl");
        close(fd_sol);
        return 1;
    }
    int fd_resp = open(FIFO_RESPUESTA, O_RDONLY);
    if (fd_resp == -1) {
        perror(FIFO_RESPUESTA);
        close(fd_sol);
        return 1;
    }

    for (;;) {
        struct solicitud sol = leer_solicitud();

        if (write(fd_sol, &sol, sizeof sol) != (ssize_t)sizeof sol) {
            perror("No se pudo enviar la solicitud (¿servidor ausente?)");
            estado = 1;
            break;
        }
        if (sol.tipo == TERMINAR)
            break;

        /* Se leen mensajes REGISTRO hasta que llegue FIN o ERROR.
         * read() se bloquea mientras el servidor recorre el CSV. */
        int mostrados = 0, r;
        while ((r = leer_todo(fd_resp, &resp, sizeof resp)) == 1 && resp.tipo == REGISTRO)
            imprimir_registro(++mostrados, resp.linea);

        if (r != 1) { /* EOF o error: el servidor cerró su extremo */
            fprintf(stderr, "El servidor terminó inesperadamente.\n");
            estado = 1;
            break;
        }
        if (resp.tipo == ERROR)
            fprintf(stderr, "El servidor no pudo leer el archivo CSV.\n");
        else
            imprimir_fin(resp.total, mostrados);
    }

    close(fd_sol);
    close(fd_resp);
    return estado;
}
