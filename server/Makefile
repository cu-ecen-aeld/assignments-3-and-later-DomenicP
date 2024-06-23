CROSS_COMPILE ?=
CC := $(CROSS_COMPILE)gcc
CFLAGS := -std=c99 -Wall -Wextra -pedantic -Wunused -Wconversion

.DEFAULT_GOAL := all

.PHONY: all
all: aesdsocket

aesdsocket: aesdsocket.c
	$(CC) $(CFLAGS) -o $@ $<

.PHONY: clean
clean:
	rm -f aesdsocket
