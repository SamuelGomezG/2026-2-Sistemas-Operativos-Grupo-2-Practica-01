/*
 * cliente_shm.c - Parte B: cliente que envía consultas por memoria
 * compartida POSIX, sincronizada con semáforos.
 * Uso: ./cliente_shm   (con el servidor ya en ejecución)
 */
#include <errno.h>
#include <fcntl.h>
#include <sys/mman.h>

#include "cliente.h"

int main(void)
{
    int estado = 0;

    /* Sin O_CREAT: la región solo existe si el servidor la creó */
    int fd = shm_open(NOMBRE_SHM, O_RDWR, 0);
    if (fd == -1) {
        if (errno == ENOENT)
            fprintf(stderr, "El servidor no está en ejecución.\n");
        else
            perror("shm_open");
        return 1;
    }
    struct region *reg = mmap(NULL, sizeof *reg, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    close(fd); /* el mapeo sigue siendo válido sin el descriptor */
    if (reg == MAP_FAILED) {
        perror("mmap");
        return 1;
    }

    /* Se descarta un aviso de respuesta que haya dejado pendiente un
     * cliente anterior que murió en medio de una consulta */
    while (sem_trywait(&reg->sem_respuesta) == 0)
        ;

    for (;;) {
        /* La solicitud se escribe directamente en la región. Es seguro:
         * el servidor no la lee hasta que se haga el sem_post. */
        leer_solicitud(&reg->sol);
        if (sem_post(&reg->sem_solicitud) == -1) {
            perror("sem_post");
            estado = 1;
            break;
        }
        if (reg->sol.tipo == TERMINAR)
            break;

        /* Bloqueado sin consumir CPU hasta que el servidor termine de
         * escribir la respuesta; antes de eso no se lee nada de ella */
        if (sem_wait(&reg->sem_respuesta) == -1) {
            perror("sem_wait");
            estado = 1;
            break;
        }
        if (reg->estado == SERVIDOR_TERMINADO) {
            fprintf(stderr, "El servidor terminó inesperadamente.\n");
            estado = 1;
            break;
        }
        if (reg->estado == RESPUESTA_ERROR) {
            fprintf(stderr, "El servidor no pudo leer el archivo CSV.\n");
            continue;
        }

        int mostrados = 0;
        while (mostrados < reg->res.total && mostrados < MAX_RESULTADOS) {
            imprimir_registro(mostrados + 1, reg->res.lineas[mostrados]);
            mostrados++;
        }
        imprimir_fin(reg->res.total, mostrados);
    }

    if (munmap(reg, sizeof *reg) == -1)
        perror("munmap");
    return estado;
}
