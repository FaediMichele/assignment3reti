CFLAGS=-Wpedantic -Wall -D_REENTRANT

all:	server client

debug: server.c
	gcc ${CFLAGS} server.c -o server
	gcc ${CFLAGS} client.c -o client

server: server.c
	gcc ${CFLAGS} server.c -o server

client: client.c
	gcc ${CFLAGS} client.c -o client

.PHONY: clean

clean:
	rm -f server client

