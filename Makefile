#
CC=cc

SOURCES= README Makefile LICENSE plug.1 plug.c plug.h config.h

CFLAGS=-O -g
LIBS= 

all: plug plug.doc

plug: plug.o
	$(CC) plug.o -o plug $(LIBS)

plug.o: plug.h config.h Makefile

plug.doc: plug.1 Makefile
	nroff -man plug.1 | col > plug.doc

clean:
	rm -f plug plug.o plug.man

plug.shar: $(SOURCES)
	shar $(SOURCES) > plug.shar

plug.tgz: $(SOURCES)
	tar cvf - $(SOURCES) | gzip > plug.tgz

install: plug
	install -C -m 0755 plug /usr/local/sbin
	install -C -m 0644 plug.1 /usr/local/man/man1

