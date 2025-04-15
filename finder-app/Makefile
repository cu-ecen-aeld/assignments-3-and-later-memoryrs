# CROSS_COMPILE=aarch64-none-linux-gnu-
CC = $(CROSS_COMPILE)gcc
CFFLAGS = -g -Wall -Werror

all: writer

writer: writer.c
	$(CC) $(CFFLAGS) -o writer writer.c

clean:
	rm -f *.o writer
