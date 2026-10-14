/*
 * cliente.h - Interfaz de línea de comandos común a los dos clientes:
 * menú, lectura de criterios e impresión de resultados.
 */
#ifndef CLIENTE_H
#define CLIENTE_H

#include <stdio.h>
#include <string.h>

#include "comun.h"

/* Muestra un mensaje y lee una línea del teclado sin el salto de línea */
static inline void leer_texto(const char *mensaje, char *destino, size_t tam)
{
    char buf[64];

    printf("%s", mensaje);
    fflush(stdout);
    if (fgets(buf, sizeof buf, stdin) == NULL)
        buf[0] = '\0';

    buf[strcspn(buf, "\r\n")] = '\0';
    snprintf(destino, tam, "%s", buf);
}

/* Muestra el menú hasta que el usuario elija una opción válida y devuelve
 * CONSULTA o TERMINAR. Si se cierra la entrada estándar (Ctrl+D) se toma
 * como "terminar". */
static inline int leer_tipo(void)
{
    char opcion[8];

    while (1) {
        printf("\n=== Tasas de interés activas por tipo de crédito ===\n"
               "1. Consultar registros\n"
               "2. Terminar cliente y servidor\n");

        leer_texto("Opción: ", opcion, sizeof opcion);

        /* Se revisa primero el fin de entrada: una "1" sin salto de línea
         * final también deja la entrada en EOF y debe terminar */
        if (opcion[0] == '2' || feof(stdin))
            return TERMINAR;

        if (opcion[0] == '1')
            return CONSULTA;

        printf("Opción inválida.\n");
    }
}

/* Pide los criterios de búsqueda y los guarda en `sol`.
 * Un criterio vacío significa "cualquiera". */
static inline void leer_criterios(struct solicitud *sol)
{
    printf("Deje un criterio vacío para no filtrar por él.\n");
    leer_texto("Código de entidad (ej. 7 = Bancolombia): ", sol->entidad, MAX_CAMPO);
    leer_texto("Fecha de corte (dd/mm/aaaa): ", sol->fecha, MAX_CAMPO);
    leer_texto("Código de municipio (ej. 11001 = Bogotá): ", sol->municipio, MAX_CAMPO);
}

/* Arma la solicitud con lo que elija el usuario */
static inline struct solicitud leer_solicitud(void)
{
    struct solicitud sol = { 0 };

    sol.tipo = leer_tipo();
    if (sol.tipo == CONSULTA)
        leer_criterios(&sol);

    return sol;
}

/* Columnas del CSV que se muestran de cada registro, con su etiqueta */
static const struct {
    int columna;
    const char *nombre;
} campos_registro[] = {
    { 3, "Entidad" },          { COL_FECHA, "Fecha de corte" }, { COL_MUNICIPIO, "Municipio" },
    { 8, "Tipo de crédito" },  { 10, "Producto" },              { 11, "Plazo" },
    { 12, "Tasa efectiva %" }, { 14, "Monto" },                 { 15, "Créditos" },
};

/* Imprime los campos más relevantes de una línea del CSV */
static inline void imprimir_registro(int numero, const char *linea)
{
    char valor[128];

    printf("\n--- Registro %d ---\n", numero);

    for (size_t i = 0; i < sizeof campos_registro / sizeof campos_registro[0]; i++) {
        campo(linea, campos_registro[i].columna, valor, sizeof valor);
        printf("  %s: %s\n", campos_registro[i].nombre, valor);
    }
}

/* Resumen final de la consulta */
static inline void imprimir_fin(long total, int mostrados)
{
    if (total == 0)
        printf("\nNo se encontraron resultados.\n");
    else
        printf("\nSe muestran %d de %ld registros coincidentes.\n", mostrados, total);
}

#endif
