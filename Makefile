CC=gcc
CFLAGS=-g -Wall -std=gnu99

all: mysmtpd mypopd

mysmtpd: mysmtpd.o netbuffer.o mailuser.o server.o
mypopd: mypopd.o netbuffer.o mailuser.o server.o

mysmtpd.o: mysmtpd.c netbuffer.h mailuser.h server.h
mypopd.o: mypopd.c netbuffer.h mailuser.h server.h

netbuffer.o: netbuffer.c netbuffer.h
mailuser.o: mailuser.c mailuser.h
server.o: server.c server.h

clean:
	-rm -rf mysmtpd mypopd mysmtpd.o mypopd.o netbuffer.o mailuser.o server.o
cleanall: clean
	-rm -rf *~
