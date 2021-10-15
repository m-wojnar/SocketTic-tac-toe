CC=gcc -Wall
LDLIBS=-pthread

all: server client

server: server.o

client: client.o

test:
	./server 40000 "./unix_path"
	./client "name1" "inet" "0.0.0.0" "40000"
	./client "name2" "unix" "./unix_path"

clean:
	rm -f client client.o
	rm -f server server.o
