#
CC=cc

SOURCES= README Makefile LICENSE plug.1 plug.c plug.h proto.h

CFLAGS=-O
LIBS= 

all: plug plug.man

plug: plug.o
	$(CC) plug.o -o plug $(LIBS)

plug.o: plug.h proto.h Makefile

plug.man: plug.1
	nroff -man plug.1 > plug.man

clean:
	rm -f plug plug.o plug.man

plug.shar: $(SOURCES)
	shar $(SOURCES) > plug.shar
