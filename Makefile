CC=gcc
CFLAGS=-g -Wall -D_GNU_SOURCE

RM=rm -f

SRCS=xmod.c

all: xmod

xmod: $(SRCS)
	$(CC) $(CFLAGS) -o xmod $(SRCS)

clean:
	$(RM) xmod
