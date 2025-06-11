ifeq ($(CC),)
	CC = $(CROSS_COMPILE)gcc
endif

ifeq ($(CFLAGS),)
	CFLAGS = -g -Wall -
endif

ifeq ($(LDFLAGS),)
	LDFLAGS = -pthread -lrt
endif

all: aesdsocket

default: aesdsocket

aesdsocket: aesdsocket.c
	$(CC) $(CFLAGS) -o aesdsocket aesdsocket.c $(LDFLAGS)

clean:
	rm -f *.o aesdsocket
