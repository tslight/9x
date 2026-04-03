VERSION = 0.1
PREFIX = /usr/local
CFLAGS	 ?= -O2 -pedantic -std=c11
CFLAGS	 += -Wall -Wconversion -Wextra -Wshadow -Wunused
CFLAGS	 += -Wmissing-prototypes -Wstrict-prototypes
CFLAGS	 += -Wuninitialized -Wimplicit-fallthrough
CFLAGS	 += `pkg-config --cflags x11 xft`
CFLAGS	 += -DVERSION=\"${VERSION}\"
LDFLAGS   = `pkg-config --libs x11 xft`

SRC = 9x.c
OBJ = ${SRC:.c=.o}

all: 9x

.c.o:
	${CC} ${CFLAGS} -c $<

9x: ${OBJ}
	${CC} -o $@ ${OBJ} ${LDFLAGS} ${LIBS}

clean:
	rm -f 9x ${OBJ}

install: 9x
	install -s 9x ${DESTDIR}${PREFIX}/bin

uninstall:
	rm -f ${DESTDIR}${PREFIX}/bin/9x

.PHONY: all clean install uninstall