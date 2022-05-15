CC = gcc
CFLAGS = -pthread -std=gnu99

all: stockclient stockserver

stockclient: stockclient.c
	$(CC) -o stockclient stockclient.c $(CFLAGS) 

stockserver: stockserver.c
	$(CC) -o stockserver stockserver.c $(CFLAGS)