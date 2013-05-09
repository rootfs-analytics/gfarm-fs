SHELL = /bin/sh

CC = gcc
CPPFLAGS =
CFLAGS = -Wall -O2
LDFLAGS =
CCLD = $(CC)

.PHONY: all clean

all: malloc

malloc: malloc.o
	$(CCLD) -o $@ $(CFLAGS) malloc.o $(LDFLAGS)

malloc.o: malloc.c
	$(CC) -c -o $@ $(CFLAGS) $(CPPFLAGS) malloc.c

clean:
	rm -f malloc
	rm -f malloc.o
