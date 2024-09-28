.DEFAULT_GOAL := all

CROSS_COMPILE ?=
CC := $(CROSS_COMPILE)gcc
INCLUDE_DIRS := include
CFLAGS := -std=c99 -Wall -Wextra -pedantic -Wunused -Wconversion -I $(INCLUDE_DIRS)
LDFLAGS := -pthread

SRC_FILES :=
SRC_FILES += src/aesd_server.c
SRC_FILES += src/aesd_worker.c
SRC_FILES += src/aesdsocket.c

.PHONY: all
all: aesdsocket

aesdsocket: $(SRC_FILES)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

.PHONY: clean
clean:
	rm -f aesdsocket
