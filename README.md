# 2026-2 - Sistemas Operativos - Grupo 2 - Práctica 1

Procesos, llamadas al sistema e IPC en Linux. Un cliente y un servidor con acceso a un CSV se ejecutan como procesos independientes
y se comunican de dos formas: con **FIFO** mediante tuberias (parte A) y con **memoria compartida
y semáforos POSIX** (parte B). El usuario ve el mismo resultado en ambas versiones a pesar de que su proceso de comunicación es diferente.

Integrantes: Samuel Gomez Guio,
	     Maikol Andres Ovalle Roa,
	     Leiner Esteban Reyes Agudelo.

## 1. Dataset

El dataset que se utiliza contiene información acerca de las Tasas de interés activas por tipo de crédito en los últimos dos meses tomados de la Superintendencia Financiera. Cada fila resume los créditos
desembolsados por una entidad en una fecha de corte, con su tasa, monto, plazo,
municipio, entre otros datos.

- Tiene un tamaño de 537 MB, 1'604.010 registros más el encabezado, en total son 23 columnas.
- Todos los campos van entre comillas y algunos contienen comas, debido a esto el separador solo cuenta fuera de las comillas.
- El archivo no se incluye en el repositorio, por lo cual toca cargarlo al momento de ejecutarlo.

Criterios de búsqueda (se combinan con Y; uno vacío significa "cualquiera"):

| Criterio | Columna | Ejemplo |
|---|---|---|
| Código de entidad | `Codigo_Entidad` (2) | `7` (Bancolombia), `1` (Banco de Bogotá) |
| Fecha de corte | `Fecha_Corte` (4) | `25/09/2026`, `12/09/2026` |
| Código de municipio | `Codigo_Municipio` (22) | `11001` (Bogotá), `05001` (Medellín) |

El servidor devuelve los primeros 20 registros coincidentes y el total de
coincidencias.

## 2. Compilación y ejecución

Inicialmente para compilar:
```sh
make          # genera y compila servidor_fifo, cliente_fifo, servidor_shm, cliente_shm
make clean    # elimina los archivos mencionados previamente
```

Para la ejecución, cabe aclarar que se requieren dos terminales distintas:

```sh
# Terminal 1                         # Terminal 2
./servidor_fifo                      ./cliente_fifo
./servidor_shm                       ./cliente_shm
```

El Menú del cliente cuenta con dos opciones: `1` consultar algún registro ó `2` terminar cliente y servidor, al ejecutar `1` el sistema le solicita que escriba los parámetros que desea buscar, si no selecciona ni la opción 1 ni la 2 el sistema envia un mensaje de error y le vuelve a solicitar una opción. Y en el servidor con el comando `Ctrl+C` también lo termina de forma ordenada. Si es el cliente el que se cierra con
`Ctrl+C`, el servidor sigue disponible para otro cliente.

## 3. Organización del código
A continuación se resume de que forma esta distribuido el código a través de los archivos.
| Archivo | Contenido |
|---|---|
| `comun.h` | Nombres de los recursos IPC, estructuras del protocolo, `campo()` y `leer_todo()` |
| `servidor.h` | Búsqueda secuencial en el CSV (`buscar`) y manejo de señales |
| `cliente.h` | Menú, lectura de criterios e impresión de resultados |
| `servidor_fifo.c`, `cliente_fifo.c` | Parte A FIFO |
| `servidor_shm.c`, `cliente_shm.c` | Parte B Memoria Compartida |

## 4. Protocolos de comunicación y recursos IPC
A continuación se explicaran los protocolos de comunicación de procesos que se utilizan, separándolos en dos grandes secciones Parte A y Parte B, esto debido a que cuentan con propiedades que valen la pena analizarlas a fondo.

## 4.1 Parte A: FIFO

**Recursos:** Se cuenta con dos tuberías FIFO, una por sentido, creadas por el servidor con `mkfifo` y no con `pipe` ya que este requiere que los procesos se encuentren emparentados:
`/tmp/p1_fifo_solicitud` (cliente → servidor) y `/tmp/p1_fifo_respuesta`
(servidor → cliente).

**Protocolo:** Los mensajes son binarios de tamaño fijo (definidos en `comun.h`).

- Solicitud: una `struct solicitud` (52 bytes) con `tipo` (`CONSULTA` o
  `TERMINAR`) y los tres criterios como cadenas.
- Respuesta a una consulta: de 0 a 20 `struct respuesta` (528 bytes) de tipo
  `REGISTRO`, cada una con una línea del CSV, y al final una de tipo `FIN` con
  el total de coincidencias. Si el servidor no puede leer el CSV envía un único
  mensaje `ERROR`.
- `TERMINAR` no tiene respuesta: el servidor cierra y elimina las FIFO.

Como los dos mensajes miden menos que `PIPE_BUF` (4096 bytes), el kernel
garantiza que cada `write()` es atómico: no hay escrituras parciales. El
receptor usa `leer_todo()`, que repite `read()` hasta completar el mensaje, y al
ser de tamaño fijo no hay ambigüedad sobre dónde termina cada uno.

## 4.2 Parte B: memoria compartida

**Recursos:** una región POSIX (`shm_open` + `ftruncate` + `mmap` con
`MAP_SHARED`) llamada `/p1_shm`, visible como `/dev/shm/p1_shm`, y dos semáforos
POSIX sin nombre guardados dentro de la misma región (`sem_init` con
`pshared = 1`), ambos iniciados en 0.

**Estructura** (`struct region`, 10.368 bytes): `sem_solicitud`,
`sem_respuesta`, `estado`, la solicitud (`sol`) y el resultado (`res`, con
`total` y `lineas[20][512]`).

**Protocolo:**

1. El cliente escribe la solicitud en `sol` y hace `sem_post(sem_solicitud)`.
2. El servidor, que dormía en `sem_wait(sem_solicitud)`, recorre el CSV, llena
   `res.lineas` y `res.total`, pone `estado` (`RESPUESTA_LISTA` o `RESPUESTA_ERROR`) y
   solo entonces hace `sem_post(sem_respuesta)`.
3. El cliente, que dormía en `sem_wait(sem_respuesta)`, lee la respuesta.

Si el servidor termina por una señal pone `estado = SERVIDOR_TERMINADO` y hace
un último `sem_post(sem_respuesta)` para que el cliente no quede esperando.


## 5. Manejo de errores

Se revisa el valor de retorno de las llamadas al sistema y de biblioteca; los
errores se informan mediante `perror`.

| Situación | Comportamiento |
|---|---|
| El CSV no existe o no se puede leer | El servidor no arranca (`access`); si falla durante una consulta, el cliente recibe `ERROR` |
| Cliente sin servidor | "El servidor no está en ejecución" y código de salida 1 |
| El servidor termina con el cliente activo | FIFO: `write` falla con `EPIPE` o `read` devuelve EOF. SHM: `estado = SERVIDOR_TERMINADO` |
| El cliente muere | FIFO: el servidor recibe EOF (o `EPIPE`) y espera otro cliente. SHM: el servidor sigue esperando solicitudes |
| `Ctrl+C` / `SIGTERM` al servidor | El manejador marca una bandera; las llamadas bloqueadas retornan `EINTR` y el servidor libera sus recursos |
| Ya existe la región compartida | El servidor SHM no arranca e indica cómo eliminarla |


## 6. Acceso al dataset y uso de memoria

El CSV nunca se carga completo. En cada consulta `buscar()` lo abre y lo lee
línea por línea con `getline()`, cuyo buffer dinámico (una sola línea) se libera
con `free()` al terminar. El servidor ocupa unos 1,7 MB de memoria residente
frente a los 537 MB del archivo.

## 7. Observaciones con herramientas de Linux

**a) Identificar PID y PPID.

<img width="485" height="135" alt="image" src="https://github.com/user-attachments/assets/f784c08b-dbd9-446c-9209-34b74c9bfdbf" />

**b) Estado y salud de un proceso.

<img width="218" height="170" alt="image" src="https://github.com/user-attachments/assets/e1d0260b-bf3d-4d3e-8004-a6afa0d37d23" />

**c) Identificar archivos abiertos.

<img width="466" height="174" alt="image" src="https://github.com/user-attachments/assets/4bb7dcf9-e0d1-410e-b067-be9d4a2028b6" />

**d) Llamadas al sistema realizadas por el cliente y el servidor.

<img width="390" height="184" alt="image" src="https://github.com/user-attachments/assets/7842557b-1088-47c6-9ad4-d99453e511b7" />

**e) Estado y consumo de recursos durante la ejecución.

<img width="521" height="177" alt="image" src="https://github.com/user-attachments/assets/1ae1cc09-1009-40d0-aad8-5ba49615ee8c" />



## 8. Uso de herramientas de IA

Se uso Claude para el soporte en la escritura del código, la interpretación y solución de errores y la explicación de conceptos teóricos de programación. La arquitectura del sistema, la integración de los procesos y la revisión del código  fueron realizadas por los miembros del equipo.
