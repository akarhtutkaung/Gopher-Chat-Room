CFLAGS 	= -Wall -g
CC		= gcc $(CFLAGS)

PROGRAMS = client server

server : tcp_server.c server_funcs.c
	$(CC) -o $@ $^

client : tcp_client.c -lpthread
	$(CC) -o $@ $^

all : server client

clean :
	rm -f *.o $(PROGRAMS)
