CFLAGS=-Wpedantic -Wall -D_REENTRANT

all:	server client

debug: server.c
	gcc ${CFLAGS} server.c -o server
	gcc ${CFLAGS} client.c -o client

client: tcpClient.o udpClient.o

tcpClient.o: tcpClient.c
	gcc ${CFLAGS} tcpClient.c -o tcpClient.o

udpClient.o: udpClient.c
	gcc ${CFLAGS} udpClient.c -o udpClient.o

server: server.c
	gcc ${CFLAGS} server.c -o server

client: client.c
	gcc ${CFLAGS} client.c -o client

.PHONY: clean

clean:
	rm -f server client

