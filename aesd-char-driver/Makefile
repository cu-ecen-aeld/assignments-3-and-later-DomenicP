# See example Makefile from scull project
# Comment/uncomment the following line to disable/enable debugging
#DEBUG = y

# Add your debugging flag (or not) to CFLAGS
ifeq ($(DEBUG),y)
	DEBFLAGS = -O -g -DAESDCHAR # "-O" is needed to expand inlines
else
	DEBFLAGS = -O2
endif

EXTRA_CFLAGS += $(DEBFLAGS)
EXTRA_CFLAGS += -std=gnu11 -Wno-declaration-after-statement -Wno-unused-parameter

ifneq ($(KERNELRELEASE),)
# call from kernel build system
obj-m := aesdchar.o
aesdchar-y := aesd-circular-buffer.o main.o
else

KERNELDIR ?= /lib/modules/$(shell uname -r)/build
PWD := $(shell pwd)

modules:
	$(MAKE) -C $(KERNELDIR) M=$(PWD) modules

endif

clean:
	rm -rf \
		.*.cmd \
		.depend \
		.tmp_versions \
		*.d \
		*.ko \
		*.mod \
		*.mod.c \
		*.o \
		*.order \
		*.symvers \
		*~ \
		core

