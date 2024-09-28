.DEFAULT_GOAL := all

CROSS_COMPILE ?=
CC := $(CROSS_COMPILE)gcc
INCLUDE_DIRS := include
SRC_FILES := src/aesd_server.c src/aesdsocket.c
CFLAGS := -std=c99 -Wall -Wextra -pedantic -Wunused -Wconversion -I $(INCLUDE_DIRS)

.PHONY: all
all: aesdsocket

aesdsocket: $(SRC_FILES)
	$(CC) $(CFLAGS) -o $@ $^

.PHONY: clean
clean:
	rm -f aesdsocket
