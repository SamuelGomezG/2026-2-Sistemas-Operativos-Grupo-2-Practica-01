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

/* Muestra el menú y llena la solicitud con lo que elija el usuario.
 * Si se cierra la entrada estándar (Ctrl+D) se toma como "terminar". */
static inline void leer_solicitud(struct solicitud *sol)
{
    char opcion[8];

    memset(sol, 0, sizeof *sol);
    for (;;) {
        printf("\n=== Tasas de interés activas por tipo de crédito ===\n"
               "1. Consultar registros\n"
               "2. Terminar cliente y servidor\n");
        leer_texto("Opción: ", opcion, sizeof opcion);
        if (opcion[0] == '2' || feof(stdin)) {
            sol->tipo = TERMINAR;
            return;
        }
        if (opcion[0] == '1')
            break;
        printf("Opción inválida.\n");
    }

    sol->tipo = CONSULTA;
    printf("Deje un criterio vacío para no filtrar por él.\n");
    leer_texto("Código de entidad (ej. 7 = Bancolombia): ", sol->entidad, MAX_CAMPO);
    leer_texto("Fecha de corte (dd/mm/aaaa): ", sol->fecha, MAX_CAMPO);
    leer_texto("Código de municipio (ej. 11001 = Bogotá): ", sol->municipio, MAX_CAMPO);
}

/* Imprime los campos más relevantes de una línea del CSV */
static inline void imprimir_registro(int numero, const char *linea)
{
    static const struct {
        int columna;
        const char *nombre;
    } campos[] = {
        { 3, "Entidad" },          { 4, "Fecha de corte" }, { 22, "Municipio" },
        { 8, "Tipo de crédito" },  { 10, "Producto" },      { 11, "Plazo" },
        { 12, "Tasa efectiva %" }, { 14, "Monto" },         { 15, "Créditos" },
    };
    char valor[128];

    printf("\n--- Registro %d ---\n", numero);
    for (size_t i = 0; i < sizeof campos / sizeof campos[0]; i++) {
        campo(linea, campos[i].columna, valor, sizeof valor);
        printf("  %s: %s\n", campos[i].nombre, valor);
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
