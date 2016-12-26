#
#

SOURCES= README Makefile LICENSE plug.1 plug.c plug.h config.h includes.h
VER=2.5.3
T=plugdaemon-$(VER)

# dietlibc with al
#CC=/opt/diet/bin/diet -v -Os cc
#CFLAGS=-O2 -Wall -s -static
#LFLAGS=-s -static
# gcc
CC=cc
CFLAGS=-O -g -Wall
LFLAGS=
# cc
#CC=cc
#CFLAGS=-O -g
#LFLAGS=
LIBS= 

all: plug plug.doc

plug: plug.o
	$(CC) $(LFLAGS) plug.o -o plug $(LIBS)

plug.o: plug.h config.h Makefile

plug.doc: plug.1 Makefile
	nroff -man plug.1 | col > plug.doc

clean:
	rm -f plug plug.o plug.doc

$T.tgz: $(SOURCES)
	mkdir $T
	cp -p $(SOURCES) $T
	tar cvf - $T | gzip > $T.tgz
	rm -r $T

tar: $T.tgz

install: plug
	install -C -m 0755 plug /usr/local/sbin
	install -C -m 0644 plug.1 /usr/local/man/man1

