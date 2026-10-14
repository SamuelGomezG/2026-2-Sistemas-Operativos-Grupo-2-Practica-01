/*
 * servidor_fifo.c - Parte A: servidor que atiende consultas por FIFO.
 * Uso: ./servidor_fifo [ruta_csv]
 */
#include <errno.h>
#include <fcntl.h>
#include <sys/stat.h>

#include "servidor.h"

/* Envía un mensaje completo; al ser menor que PIPE_BUF el write es atómico */
static int enviar(int fd, const struct respuesta *resp)
{
    return write(fd, resp, sizeof *resp) == (ssize_t)sizeof *resp;
}

/* Atiende a un cliente hasta que se desconecte o pida terminar.
 * Devuelve 1 si el servidor debe seguir esperando clientes y 0 si no. */
static int atender(int fd_sol, int fd_resp, const char *ruta)
{
    static struct resultado res; /* static: ocupa ~10 KB, mejor fuera de la pila */
    struct solicitud sol;
    struct respuesta resp;

    for (;;) {
        /* Aquí el servidor queda bloqueado mientras el usuario escribe */
        int r = leer_todo(fd_sol, &sol, sizeof sol);
        if (r == 0) { /* EOF: el cliente cerró su extremo de la FIFO */
            printf("Cliente desconectado; esperando otro cliente.\n");
            return 1;
        }
        if (r == -1) {
            if (!terminar)
                perror("read");
            return 0;
        }
        if (sol.tipo == TERMINAR) {
            printf("Orden de terminación recibida.\n");
            return 0;
        }

        printf("Consulta: entidad='%s' fecha='%s' municipio='%s'\n",
               sol.entidad, sol.fecha, sol.municipio);
        int fallo = buscar(ruta, &sol, &res) == -1;
        int ok = 1;

        if (terminar) /* búsqueda interrumpida: el cliente verá EOF */
            return 0;

        memset(&resp, 0, sizeof resp);
        if (fallo) {
            perror(ruta);
            resp.tipo = ERROR;
            ok = enviar(fd_resp, &resp);
        } else {
            resp.tipo = REGISTRO;
            for (long i = 0; ok && i < res.total && i < MAX_RESULTADOS; i++) {
                snprintf(resp.linea, sizeof resp.linea, "%s", res.lineas[i]);
                ok = enviar(fd_resp, &resp);
            }
            resp.tipo = FIN;
            resp.total = res.total;
            ok = ok && enviar(fd_resp, &resp);
        }
        if (!ok) { /* EPIPE: el cliente ya no está leyendo */
            perror("write");
            return 1;
        }
    }
}

int main(int argc, char *argv[])
{
    const char *ruta = argc > 1 ? argv[1] : RUTA_CSV;
    int seguir = 1;

    if (access(ruta, R_OK) == -1) {
        perror(ruta);
        return 1;
    }
    if (instalar_senales() == -1) {
        perror("sigaction");
        return 1;
    }

    /* Si las FIFO quedaron de una ejecución anterior se reutilizan */
    if (mkfifo(FIFO_SOLICITUD, 0666) == -1 && errno != EEXIST) {
        perror(FIFO_SOLICITUD);
        return 1;
    }
    if (mkfifo(FIFO_RESPUESTA, 0666) == -1 && errno != EEXIST) {
        perror(FIFO_RESPUESTA);
        unlink(FIFO_SOLICITUD);
        return 1;
    }
    printf("Servidor FIFO listo (PID %d). Esperando un cliente...\n", getpid());

    while (seguir && !terminar) {
        /* open() se bloquea hasta que un cliente abra el otro extremo */
        int fd_sol = open(FIFO_SOLICITUD, O_RDONLY);
        if (fd_sol == -1) {
            if (errno != EINTR)
                perror(FIFO_SOLICITUD);
            break;
        }
        int fd_resp = open(FIFO_RESPUESTA, O_WRONLY);
        if (fd_resp == -1) {
            if (errno != EINTR)
                perror(FIFO_RESPUESTA);
            close(fd_sol);
            break;
        }
        printf("Cliente conectado.\n");
        seguir = atender(fd_sol, fd_resp, ruta);
        close(fd_sol);
        close(fd_resp);
    }

    /* Se eliminan las FIFO del sistema de archivos */
    if (unlink(FIFO_SOLICITUD) == -1)
        perror(FIFO_SOLICITUD);
    if (unlink(FIFO_RESPUESTA) == -1)
        perror(FIFO_RESPUESTA);
    printf("Servidor terminado.\n");
    return 0;
}
