CFLAGS=-Wpedantic -Wall -D_REENTRANT

all:	server client

debug: server.c
	gcc -DDEBUG ${CFLAGS} server.c -o server
	gcc -DDEBUG ${CFLAGS} client.c -o client

server: server.c
	gcc ${CFLAGS} server.c -o server

client: client.c
	gcc ${CLFLAGS} client.c -o client

.PHONY: clean

clean:
	rm -f server client

