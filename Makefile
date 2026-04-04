PROG	= 9x
VERSION = 0.1
PREFIX	?= /usr/local
MANDIR	?= ${PREFIX}/man/man1
CFLAGS	?= -O2 -pedantic -std=c11
CFLAGS	+= -Wall -Wconversion -Wextra -Wshadow -Wunused
CFLAGS	+= -Wmissing-prototypes -Wstrict-prototypes
CFLAGS	+= -Wuninitialized -Wimplicit-fallthrough
CFLAGS	+= `pkg-config --cflags x11 xft`
CFLAGS	+= -DVERSION=\"${VERSION}\"
LDFLAGS	= `pkg-config --libs x11 xft`

SRC = ${PROG}.c
OBJ = ${SRC:.c=.o}

all: ${PROG}

.c.o:
	${CC} ${CFLAGS} -c $<

${PROG}: ${OBJ}
	${CC} -o $@ ${OBJ} ${LDFLAGS} ${LIBS}

clean:
	rm -f ${PROG} ${OBJ}

install: ${PROG}
	install -s ${PROG} ${PREFIX}/bin
	install -m 644 ${PROG}.1 ${MANDIR}/${PROG}.1

uninstall:
	rm -f ${PREFIX}/bin/${PROG}
	rm -f ${MANDIR}/${PROG}.1

.PHONY: all clean install uninstall