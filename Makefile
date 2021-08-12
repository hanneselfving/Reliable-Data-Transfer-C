CC = gcc
CFLAGS = -Wall
PROGRAMS = client server

ALL: ${PROGRAMS}

client: client.c
	${CC} ${CFLAGS} -o client client.c -lpthread

server: server.c
	${CC} ${CFLAGS} -o server server.c -lpthread

clean:
	rm -f ${PROGRAMS}
