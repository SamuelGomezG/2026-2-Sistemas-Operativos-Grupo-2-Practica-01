# 2026-2 - Sistemas Operativos - Grupo 2 - Práctica 1

Procesos, llamadas al sistema e IPC en Linux. Un cliente (interfaz de línea de
comandos) y un servidor (acceso al CSV) se ejecutan como procesos independientes
y se comunican de dos formas: con **FIFO** (parte A) y con **memoria compartida
y semáforos POSIX** (parte B). El usuario ve lo mismo en ambas versiones.

Integrantes: Samuel Gomez
	     Maikol Ovalle
	     Leiner Agudelo

## 1. Dataset

*Tasas de interés activas por tipo de crédito – Últimos dos meses*
(Superintendencia Financiera, datos abiertos). Cada fila resume los créditos
desembolsados por una entidad en una fecha de corte, con su tasa, monto, plazo,
municipio, etc.

- 537 MB, 1 604 010 registros más el encabezado, 23 columnas.
- Todos los campos van entre comillas y algunos contienen comas
  (`"500,000,000"`), por lo que el separador solo cuenta fuera de las comillas.
- El archivo no se incluye en el repositorio (ver `.gitignore`); debe copiarse
  en esta carpeta o indicarse su ruta al iniciar el servidor.

Criterios de búsqueda (se combinan con Y; uno vacío significa "cualquiera"):

| Criterio | Columna | Ejemplo |
|---|---|---|
| Código de entidad | `Codigo_Entidad` (2) | `7` (Bancolombia), `1` (Banco de Bogotá) |
| Fecha de corte | `Fecha_Corte` (4) | `25/09/2026` |
| Código de municipio | `Codigo_Municipio` (22) | `11001` (Bogotá), `05001` (Medellín) |

El servidor devuelve los primeros 20 registros coincidentes y el total de
coincidencias.

## 2. Compilación y ejecución

```sh
make          # genera servidor_fifo, cliente_fifo, servidor_shm, cliente_shm
make clean
```

En dos terminales distintas (primero el servidor):

```sh
# Terminal 1                         # Terminal 2
./servidor_fifo [ruta_csv]           ./cliente_fifo
./servidor_shm  [ruta_csv]           ./cliente_shm
```

Menú del cliente: `1` consultar, `2` terminar cliente y servidor. `Ctrl+C` en el
servidor también lo termina de forma ordenada. Si el cliente se cierra con
`Ctrl+C`, el servidor sigue disponible para otro cliente. Se atiende un cliente
a la vez.

## 3. Organización del código

| Archivo | Contenido |
|---|---|
| `comun.h` | Nombres de los recursos IPC, estructuras del protocolo, `campo()` y `leer_todo()` |
| `servidor.h` | Búsqueda secuencial en el CSV (`buscar`) y manejo de señales |
| `cliente.h` | Menú, lectura de criterios e impresión de resultados |
| `servidor_fifo.c`, `cliente_fifo.c` | Parte A |
| `servidor_shm.c`, `cliente_shm.c` | Parte B |

## 4. Parte A: FIFO

**Recursos:** dos FIFO, una por sentido, creadas por el servidor con `mkfifo`:
`/tmp/p1_fifo_solicitud` (cliente → servidor) y `/tmp/p1_fifo_respuesta`
(servidor → cliente).

**Protocolo:** mensajes binarios de tamaño fijo (definidos en `comun.h`).

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

**Aspectos a explicar**

- *Cliente antes que el servidor:* si la FIFO no existe, `open` falla con
  `ENOENT`. Si existe pero nadie la tiene abierta para lectura (quedó de un
  servidor que murió), un `open(O_WRONLY)` normal se quedaría bloqueado; por eso
  el cliente abre con `O_NONBLOCK`, que en ese caso falla de inmediato con
  `ENXIO`. En ambos casos informa "El servidor no está en ejecución".
- *Bloqueos:* `open()` de una FIFO se bloquea hasta que otro proceso abra el
  otro extremo (el servidor espera así a su cliente). `read()` se bloquea si la
  FIFO está vacía y hay algún escritor (el servidor mientras el usuario escribe;
  el cliente mientras el servidor recorre el CSV). `write()` se bloquearía si el
  buffer de la tubería (64 KiB) estuviera lleno.
- *Descriptores:* cada proceso tiene 0, 1 y 2, más el 3 y el 4 para las FIFO
  (servidor: 3 lectura, 4 escritura; cliente: al revés). Durante una búsqueda
  el servidor tiene además el 5, el CSV.
- *Recursos del sistema operativo:* los dos inodos de tipo FIFO en `/tmp`, el
  buffer de cada tubería en el kernel, las tablas de descriptores y las colas de
  espera donde duermen los procesos bloqueados.

## 5. Parte B: memoria compartida

**Recursos:** una región POSIX (`shm_open` + `ftruncate` + `mmap` con
`MAP_SHARED`) llamada `/p1_shm`, visible como `/dev/shm/p1_shm`, y dos semáforos
POSIX sin nombre guardados dentro de la misma región (`sem_init` con
`pshared = 1`), ambos iniciados en 0.

**Estructura** (`struct region`, 10 368 bytes): `sem_solicitud`,
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

**Aspectos a explicar**

- *Por qué se necesita sincronización:* en una FIFO el kernel hace de
  intermediario y bloquea al lector hasta que haya datos. En memoria compartida
  ambos procesos leen y escriben las mismas páginas sin pasar por el kernel, así
  que nada le indica a uno cuándo terminó el otro.
- *Condición de carrera sin semáforos:* el cliente podría leer `res.lineas` y
  `res.total` mientras el servidor aún los escribe (respuesta a medias o de la
  consulta anterior), o el servidor podría leer criterios a medio escribir.
  Aquí se evita porque en cada momento solo un proceso tiene "el turno": el
  cliente escribe mientras el servidor duerme en `sem_solicitud`, y el servidor
  escribe mientras el cliente duerme en `sem_respuesta`. No hay espera activa:
  `sem_wait` duerme al proceso (llamada `futex`).
- *FIFO frente a memoria compartida:* la FIFO copia los datos dos veces
  (proceso → kernel → proceso) con una llamada al sistema por mensaje, y trae
  sincronización y aviso de desconexión (EOF, `EPIPE`). La memoria compartida no
  copia ni entra al kernel para pasar los datos, pero la sincronización y la
  detección del otro proceso quedan a cargo del programa.
- *Terminación inesperada:* los objetos POSIX persisten hasta que alguien los
  elimina o se reinicia el equipo. Si el servidor muere con `kill -9`, queda
  `/dev/shm/p1_shm`; el siguiente servidor lo detecta (`O_EXCL` → `EEXIST`) y
  pide borrarlo con `rm /dev/shm/p1_shm`. Además, un cliente que estuviera en
  `sem_wait` se queda bloqueado, porque el kernel no avisa (a diferencia del EOF
  de la FIFO). Las FIFO huérfanas en `/tmp` se reutilizan sin problema.

## 6. Manejo de errores

Se revisa el valor de retorno de las llamadas al sistema y de biblioteca; los
errores se informan con `perror`.

| Situación | Comportamiento |
|---|---|
| El CSV no existe o no se puede leer | El servidor no arranca (`access`); si falla durante una consulta, el cliente recibe `ERROR` |
| Cliente sin servidor | "El servidor no está en ejecución" y código de salida 1 |
| El servidor termina con el cliente activo | FIFO: `write` falla con `EPIPE` o `read` devuelve EOF. SHM: `estado = SERVIDOR_TERMINADO` |
| El cliente muere | FIFO: el servidor recibe EOF (o `EPIPE`) y espera otro cliente. SHM: el servidor sigue esperando solicitudes |
| `Ctrl+C` / `SIGTERM` al servidor | El manejador marca una bandera; las llamadas bloqueadas retornan `EINTR` y el servidor libera sus recursos |
| Ya existe la región compartida | El servidor SHM no arranca e indica cómo eliminarla |

Limitaciones conocidas: un solo cliente a la vez; en la versión SHM, si el
servidor muere con `SIGKILL` el cliente no se entera.

## 7. Acceso al dataset y uso de memoria

El CSV nunca se carga completo. En cada consulta `buscar()` lo abre y lo lee
línea por línea con `getline()`, cuyo buffer dinámico (una sola línea) se libera
con `free()` al terminar. El servidor ocupa unos 1,7 MB de memoria residente
frente a los 537 MB del archivo.

## 8. Observaciones con herramientas de Linux

**a) Servidor FIFO bloqueado esperando un cliente** (`ps` y `/proc/<pid>/`):

```
$ ps -o pid,ppid,stat,wchan:20,rss,cmd -p 143787
    PID    PPID STAT WCHAN                  RSS CMD
 143787  143785 S    wait_for_partner      1612 ./servidor_fifo
$ grep -E 'State|VmRSS' /proc/143787/status
State:  S (sleeping)
VmRSS:      1612 kB
$ ls -l /proc/143787/fd        # solo 0, 1 y 2: el open() aún no ha retornado
```

El proceso está dormido (`S`) dentro de `open()`, en la función del kernel
`wait_for_partner`, hasta que alguien abra el otro extremo. Durante una búsqueda
pasa a `R` y su RSS sigue en ~1 700 kB.

**b) Descriptores abiertos con cliente conectado** (`lsof -a -p <pids> -d 0-9`):

```
COMMAND      PID  USER   FD   TYPE DEVICE SIZE/OFF    NODE NAME
servidor_ 143787 gguio    3r  FIFO  259,5      0t0 6161415 /tmp/p1_fifo_solicitud
servidor_ 143787 gguio    4w  FIFO  259,5      0t0 6161417 /tmp/p1_fifo_respuesta
cliente_f 143794 gguio    3w  FIFO  259,5      0t0 6161415 /tmp/p1_fifo_solicitud
cliente_f 143794 gguio    4r  FIFO  259,5      0t0 6161417 /tmp/p1_fifo_respuesta
```

Ambos procesos tienen el mismo PPID solo cuando se lanzan desde la misma shell;
ninguno es padre del otro.

**c) Llamadas al sistema del servidor FIFO** (`strace`, una consulta con 11
resultados y luego terminar):

```
mknodat(AT_FDCWD, "/tmp/p1_fifo_solicitud", S_IFIFO|0666) = 0
mknodat(AT_FDCWD, "/tmp/p1_fifo_respuesta", S_IFIFO|0666) = 0
openat(AT_FDCWD, "/tmp/p1_fifo_solicitud", O_RDONLY) = 3     <- bloqueado hasta que llega el cliente
openat(AT_FDCWD, "/tmp/p1_fifo_respuesta", O_WRONLY) = 4
read(3, "\0\0\0\00060\0\0...\00014/08/2026\0\0"..., 52) = 52  <- solicitud (CONSULTA)
openat(AT_FDCWD, "Tasas_de_inter...csv", O_RDONLY) = 5
read(5, "\"Tipo_Entidad\",\"Nombre_Tipo_Enti"..., 4096) = 4096   (131 074 lecturas de 4 KiB)
close(5)                                = 0
write(4, "\0\0\0\0...\"1\",\"BC-ESTABLEC"..., 528) = 528       <- 11 mensajes REGISTRO
write(4, "\1\0\0\0\0\0\0\0\v\0\0\0..."..., 528) = 528          <- FIN, total = 11 (\v)
read(3, "\1\0\0\0\0\0\0\0"..., 52)      = 52                   <- TERMINAR
close(3)                                = 0
close(4)                                = 0
unlink("/tmp/p1_fifo_solicitud")        = 0
unlink("/tmp/p1_fifo_respuesta")        = 0
```

`mkfifo` es en realidad la llamada `mknodat` con `S_IFIFO`. El archivo se lee
por bloques de 4 KiB: en memoria nunca hay más que ese buffer y una línea.

**d) Memoria compartida** (`ls /dev/shm`, `/proc/<pid>/maps`, `strace`):

```
$ ls -l /dev/shm/p1_shm
-rw-rw-r-- 1 gguio gguio 10368 Oct  9 18:26 /dev/shm/p1_shm
$ grep p1_shm /proc/143817/maps
7346a9bd2000-7346a9bd5000 rw-s 00000000 00:1e 11          /dev/shm/p1_shm
```

```
# servidor
openat(AT_FDCWD, "/dev/shm/p1_shm", O_RDWR|O_CREAT|O_EXCL|O_NOFOLLOW|O_CLOEXEC, 0666) = 3
ftruncate(3, 10368)                     = 0
mmap(NULL, 10368, PROT_READ|PROT_WRITE, MAP_SHARED, 3, 0) = 0x778841745000
close(3)                                = 0
futex(0x778841745000, FUTEX_WAIT_BITSET|FUTEX_CLOCK_REALTIME, 0, NULL, ...) = 0   <- sem_wait(sem_solicitud)
openat(AT_FDCWD, "Tasas_de_inter...csv", O_RDONLY) = 3
futex(0x778841745020, FUTEX_WAKE, 1)    = 1                                       <- sem_post(sem_respuesta)
futex(0x778841745000, FUTEX_WAIT_BITSET|FUTEX_CLOCK_REALTIME, 0, NULL, ...) = 0
munmap(0x778841745000, 10368)           = 0
unlink("/dev/shm/p1_shm")               = 0
```

`shm_open` es un `openat` sobre `/dev/shm` (un sistema de archivos en RAM), y la
`s` de `rw-s` en `maps` indica mapeo compartido. Los semáforos son llamadas
`futex` sobre direcciones de la propia región (desplazamientos 0 y 0x20). No
aparece ninguna llamada para pasar los datos: se copian directamente en memoria.
`ipcs -m` no muestra la región porque solo lista objetos System V; el
equivalente para POSIX es `ls /dev/shm`.

## 9. Uso de herramientas de IA

Se usó Claude (Anthropic) de forma significativa como apoyo para el diseño, la
escritura del código y de este documento, y las pruebas. Los integrantes
revisaron el resultado y deben poder explicar y modificar todo el código.
