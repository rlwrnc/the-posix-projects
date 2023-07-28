CC = gcc# compiler
CFLAGS = -Wall -g# compile flags
LIBS = -lpthread -lrt# libs

all: client server

client: client.o
	$(CC) -o client client.o $(LIBS)

server: server.o
	$(CC) -o server server.o $(LIBS)

%.o:%.c
	$(CC) $(CFLAGS) -c $*.c

clean:
	rm -f server client *.o *~
