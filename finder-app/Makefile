CROSS_COMPILE ?=
CC := $(CROSS_COMPILE)gcc
CFLAGS := -Wall -Wextra -pedantic -Wunused -Wconversion

.DEFAULT_GOAL := all

.PHONY: all
all: writer

writer: writer.c
	$(CC) $(CFLAGS) -o $@ $<

.PHONY: clean
clean:
	rm -f writer
