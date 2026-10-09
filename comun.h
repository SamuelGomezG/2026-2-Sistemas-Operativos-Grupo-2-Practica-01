/*
 * comun.h - Definiciones compartidas por clientes y servidores:
 * nombres de los recursos IPC, formato de los mensajes (protocolo)
 * y utilidades que usan ambos lados.
 */
#ifndef COMUN_H
#define COMUN_H

#include <semaphore.h>
#include <stddef.h>
#include <unistd.h>

/* Archivo CSV por defecto (el servidor acepta otra ruta como argumento) */
#define RUTA_CSV "Tasas_de_interés_activas_por_tipo_de_crédito_–_Últimos_dos_meses_20261009_part_000.csv"

/* Recursos IPC */
#define FIFO_SOLICITUD "/tmp/p1_fifo_solicitud" /* cliente -> servidor */
#define FIFO_RESPUESTA "/tmp/p1_fifo_respuesta" /* servidor -> cliente */
#define NOMBRE_SHM "/p1_shm"                    /* aparece en /dev/shm/p1_shm */

#define MAX_LINEA 512     /* la línea más larga del CSV mide 457 bytes */
#define MAX_RESULTADOS 20 /* registros que se devuelven por consulta */
#define MAX_CAMPO 16      /* tamaño de cada criterio de búsqueda */

/* Columnas del CSV usadas como criterios de búsqueda (empiezan en 0) */
#define COL_ENTIDAD 2
#define COL_FECHA 4
#define COL_MUNICIPIO 22

/* Tipos de solicitud */
enum { CONSULTA, TERMINAR };

/* Solicitud del cliente. Un criterio vacío ("") significa "cualquiera". */
struct solicitud {
    int tipo;
    char entidad[MAX_CAMPO];
    char fecha[MAX_CAMPO];
    char municipio[MAX_CAMPO];
};

/* ---------- Parte A: mensajes de la FIFO de respuesta ---------- */

/* Por cada consulta el servidor envía de 0 a MAX_RESULTADOS mensajes
 * REGISTRO seguidos de un FIN (o un único ERROR si no pudo leer el CSV). */
enum { REGISTRO, FIN, ERROR };

/* Ambas estructuras miden menos que PIPE_BUF (4096 bytes), así que el
 * kernel garantiza que cada write() a la FIFO es atómico. */
struct respuesta {
    int tipo;
    long total;            /* solo en FIN: total de registros coincidentes */
    char linea[MAX_LINEA]; /* solo en REGISTRO: línea del CSV */
};

/* ---------- Parte B: región de memoria compartida ---------- */

/* Estado de la comunicación; lo escribe el servidor antes de avisar */
enum { RESPUESTA_LISTA, RESPUESTA_ERROR, SERVIDOR_TERMINADO };

struct region {
    sem_t sem_solicitud; /* el cliente avisa que hay una solicitud */
    sem_t sem_respuesta; /* el servidor avisa que la respuesta está completa */
    int estado;
    struct solicitud sol;
    long total; /* total de registros coincidentes */
    char lineas[MAX_RESULTADOS][MAX_LINEA];
};

/* ---------- Utilidades ---------- */

/* Copia en `salida` el campo número `n` de una línea CSV. Las comas que
 * están dentro de comillas (ej. "500,000,000") no separan campos. */
static inline void campo(const char *linea, int n, char *salida, size_t tam)
{
    int en_comillas = 0;
    size_t j = 0;

    for (const char *p = linea; *p && *p != '\n' && *p != '\r'; p++) {
        if (*p == '"') {
            en_comillas = !en_comillas;
        } else if (*p == ',' && !en_comillas) {
            if (n-- == 0) /* terminó el campo buscado */
                break;
        } else if (n == 0 && j + 1 < tam) {
            salida[j++] = *p;
        }
    }
    salida[j] = '\0';
}

/* Lee exactamente `n` bytes, porque read() puede entregar menos de los
 * pedidos. Devuelve 1 si leyó todo, 0 si el otro extremo cerró (EOF)
 * y -1 si hubo error. */
static inline int leer_todo(int fd, void *buf, size_t n)
{
    size_t leidos = 0;

    while (leidos < n) {
        ssize_t r = read(fd, (char *)buf + leidos, n - leidos);
        if (r <= 0)
            return (int)r;
        leidos += (size_t)r;
    }
    return 1;
}

#endif
