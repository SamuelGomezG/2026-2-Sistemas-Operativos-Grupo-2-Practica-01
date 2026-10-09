/*
 * servidor_shm.c - Parte B: servidor que atiende consultas por memoria
 * compartida POSIX, sincronizada con semáforos.
 * Uso: ./servidor_shm [ruta_csv]
 */
#include <errno.h>
#include <fcntl.h>
#include <sys/mman.h>

#include "servidor.h"

int main(int argc, char *argv[])
{
    const char *ruta = argc > 1 ? argv[1] : RUTA_CSV;

    if (access(ruta, R_OK) == -1) {
        perror(ruta);
        return 1;
    }
    if (instalar_senales() == -1) {
        perror("sigaction");
        return 1;
    }

    /* O_EXCL: falla si la región ya existe, para no pisar la de otro
     * servidor ni reutilizar semáforos en un estado desconocido */
    int fd = shm_open(NOMBRE_SHM, O_CREAT | O_EXCL | O_RDWR, 0666);
    if (fd == -1) {
        if (errno == EEXIST)
            fprintf(stderr, "La región ya existe: hay otro servidor en ejecución o quedó "
                            "de una ejecución incorrecta (rm /dev/shm%s).\n", NOMBRE_SHM);
        else
            perror("shm_open");
        return 1;
    }
    /* La región nace con tamaño 0: hay que darle el de la estructura */
    if (ftruncate(fd, sizeof(struct region)) == -1) {
        perror("ftruncate");
        close(fd);
        shm_unlink(NOMBRE_SHM);
        return 1;
    }
    struct region *reg = mmap(NULL, sizeof *reg, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    close(fd); /* el mapeo sigue siendo válido sin el descriptor */
    if (reg == MAP_FAILED) {
        perror("mmap");
        shm_unlink(NOMBRE_SHM);
        return 1;
    }
    /* Semáforos dentro de la región, compartidos entre procesos (pshared=1)
     * y en 0: quien haga sem_wait se bloquea hasta que el otro haga sem_post */
    if (sem_init(&reg->sem_solicitud, 1, 0) == -1 || sem_init(&reg->sem_respuesta, 1, 0) == -1) {
        perror("sem_init");
        munmap(reg, sizeof *reg);
        shm_unlink(NOMBRE_SHM);
        return 1;
    }
    printf("Servidor SHM listo (PID %d). Esperando solicitudes...\n", getpid());

    while (!terminar) {
        /* Bloqueado sin consumir CPU hasta que el cliente publique una solicitud */
        if (sem_wait(&reg->sem_solicitud) == -1) {
            if (errno == EINTR) /* llegó una señal: se revisa `terminar` */
                continue;
            perror("sem_wait");
            break;
        }
        if (reg->sol.tipo == TERMINAR) {
            printf("Orden de terminación recibida.\n");
            break;
        }

        printf("Consulta: entidad='%s' fecha='%s' municipio='%s'\n",
               reg->sol.entidad, reg->sol.fecha, reg->sol.municipio);
        reg->total = buscar(ruta, &reg->sol, reg->lineas);
        if (terminar) /* búsqueda interrumpida: no hay respuesta válida */
            break;
        if (reg->total == -1)
            perror(ruta);
        reg->estado = reg->total == -1 ? RESPUESTA_ERROR : RESPUESTA_LISTA;

        /* Solo ahora, con la respuesta completa, se despierta al cliente */
        if (sem_post(&reg->sem_respuesta) == -1) {
            perror("sem_post");
            break;
        }
    }

    if (reg->sol.tipo == TERMINAR && !terminar) {
        /* Salida ordenada: el cliente ya no usa los semáforos */
        sem_destroy(&reg->sem_solicitud);
        sem_destroy(&reg->sem_respuesta);
    } else {
        /* Salida por señal o error: se avisa al cliente, que podría estar
         * bloqueado esperando una respuesta que nunca llegaría */
        reg->estado = SERVIDOR_TERMINADO;
        sem_post(&reg->sem_respuesta);
    }
    if (munmap(reg, sizeof *reg) == -1)
        perror("munmap");
    /* Se elimina el nombre; la memoria se libera cuando nadie la tenga mapeada */
    if (shm_unlink(NOMBRE_SHM) == -1)
        perror("shm_unlink");
    printf("Servidor terminado.\n");
    return 0;
}
