CC=gcc
CFLAGS=-I.
LDFLAGS=-lm

all: oss user

oss: oss.o
	$(CC) -o oss $(LDFLAGS) oss.o

user: user.o
	$(CC) -o user user.o

clean:
	rm oss user oss.o user.o
