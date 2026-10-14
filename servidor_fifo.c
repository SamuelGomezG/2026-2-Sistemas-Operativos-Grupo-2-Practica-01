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

/* Envía la respuesta a una consulta: un único mensaje ERROR si la búsqueda
 * falló, o un REGISTRO por cada línea encontrada seguido de un FIN con el
 * total. Devuelve 1 si se enviaron todos los mensajes y 0 si alguno falló. */
static int responder(int fd, const struct resultado *res, int fallo)
{
    struct respuesta resp;

    memset(&resp, 0, sizeof resp);

    if (fallo) {
        resp.tipo = ERROR;
        return enviar(fd, &resp);
    }

    resp.tipo = REGISTRO;
    for (long i = 0; i < res->total && i < MAX_RESULTADOS; i++) {
        snprintf(resp.linea, sizeof resp.linea, "%s", res->lineas[i]);

        if (!enviar(fd, &resp))
            return 0;
    }

    resp.tipo = FIN;
    resp.total = res->total;
    return enviar(fd, &resp);
}

/* Atiende a un cliente hasta que se desconecte o pida terminar.
 * Devuelve 1 si el servidor debe seguir esperando clientes y 0 si no. */
static int atender(int fd_sol, int fd_resp, const char *ruta)
{
    static struct resultado res; /* static: ocupa ~10 KB, mejor fuera de la pila */
    struct solicitud sol;
    int r;

    /* Cada vuelta atiende una consulta. Aquí el servidor queda bloqueado
     * mientras el usuario escribe; el ciclo termina cuando el cliente se
     * va, falla la lectura o llega la orden de terminar. */
    while ((r = leer_todo(fd_sol, &sol, sizeof sol)) == 1 && sol.tipo != TERMINAR) {
        printf("Consulta: entidad='%s' fecha='%s' municipio='%s'\n",
               sol.entidad, sol.fecha, sol.municipio);
        int fallo = buscar(ruta, &sol, &res) == -1;

        if (terminar) /* búsqueda interrumpida: el cliente verá EOF */
            return 0;

        if (fallo)
            perror(ruta);

        if (!responder(fd_resp, &res, fallo)) { /* EPIPE: el cliente ya no está leyendo */
            perror("write");
            return 1;
        }
    }

    /* Se salió del ciclo: se informa por qué y se decide si el servidor sigue */
    if (r == 0) { /* EOF: el cliente cerró su extremo de la FIFO */
        printf("Cliente desconectado; esperando otro cliente.\n");
        return 1;
    }
    if (r == -1) {
        if (!terminar)
            perror("read");
        return 0;
    }

    printf("Orden de terminación recibida.\n");
    return 0;
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
