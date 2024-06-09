CROSS_COMPILE ?=
CC := $(CROSS_COMPILE)gcc
CFLAGS := -Wall -Wextra -pedantic -Wunused -Wconversion

.DEFAULT_GOAL := build

build: writer

.PHONY: clean
clean:
	rm -f writer

writer: writer.c
	$(CC) $(CFLAGS) -o $@ $<
