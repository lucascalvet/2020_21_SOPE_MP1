CC=gcc
CFLAGS=-g -Wall

RM=rm -f

SRCS=$(wildcard *.c)

all: xmod

xmod: $(SRCS)
	$(CC) $(CFLAGS) -o xmod $(SRCS)

clean:
	$(RM) xmod
