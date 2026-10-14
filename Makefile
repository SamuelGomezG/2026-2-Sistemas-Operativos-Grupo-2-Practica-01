CC = gcc
CFLAGS = -Wall -Wextra -O2
# Necesarias en glibc antiguas para shm_open y los semáforos POSIX
LDLIBS = -lrt -pthread

PROGRAMAS = servidor_fifo cliente_fifo servidor_shm cliente_shm

all: $(PROGRAMAS)

servidor_%: servidor_%.c servidor.h comun.h
	$(CC) $(CFLAGS) -o $@ $< $(LDLIBS)

cliente_%: cliente_%.c cliente.h comun.h
	$(CC) $(CFLAGS) -o $@ $< $(LDLIBS)

clean:
	rm -f $(PROGRAMAS)

.PHONY: all clean
