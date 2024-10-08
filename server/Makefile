.DEFAULT_GOAL := all

CROSS_COMPILE ?=
CC ?= $(CROSS_COMPILE)gcc
CFLAGS ?= -g -std=c11 -Wall -Wextra -pedantic -Wunused -Wconversion
LDFLAGS ?= -lrt -pthread

INCLUDE_FLAGS :=
INCLUDE_FLAGS += -I include/

SRC_FILES :=
SRC_FILES += src/aesd_server.c
SRC_FILES += src/aesd_worker.c
SRC_FILES += src/aesdsocket.c

.PHONY: all
all: aesdsocket

aesdsocket: $(SRC_FILES)
	$(CC) $(CFLAGS) $(INCLUDE_FLAGS) -o $@ $^ $(LDFLAGS)

.PHONY: clean
clean:
	rm -f aesdsocket
