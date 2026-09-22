##
## SPDX-License-Identifier: GPL-2.0-only

PROGRAM   = cbmem
TOP      ?= $(abspath ../..)
ROOT      = $(TOP)/src
COMMONLIB = $(ROOT)/commonlib
CC       ?= $(CROSS_COMPILE)gcc
INSTALL  ?= /usr/bin/env install
PREFIX   ?= /usr/local
CFLAGS   ?= -O2
WERROR=-Werror
CFLAGS   += -Wall -Wextra -Wmissing-prototypes -Wshadow $(WERROR)
CFLAGS   += -std=gnu23
CPPFLAGS += -I . -I $(ROOT)/commonlib/include -I $(ROOT)/commonlib/bsd/include
CPPFLAGS += -include $(ROOT)/commonlib/bsd/include/commonlib/bsd/compiler.h

OBJS = $(PROGRAM).o cbmem_drv.o devmem_drv.o sysfs_drv.o $(COMMONLIB)/bsd/ipchksum.o

all: $(PROGRAM)

timestamp_test: cdk2_timestamp_test.c cbmem.c cbmem_drv.o devmem_drv.o sysfs_drv.o $(COMMONLIB)/bsd/ipchksum.o
	$(CC) $(CFLAGS) $(CPPFLAGS) -fsanitize=address,undefined -fno-omit-frame-pointer \
		$< $(filter %.o,$^) -o $@

check: timestamp_test
	./timestamp_test

$(PROGRAM): $(OBJS)

clean:
	rm -f $(PROGRAM) timestamp_test $(OBJS:.c=.o) .dependencies *~ junit.xml

install: $(PROGRAM)
	$(INSTALL) -d $(DESTDIR)$(PREFIX)/sbin/
	$(INSTALL) cbmem $(DESTDIR)$(PREFIX)/sbin/

distclean: clean

.dependencies:
	@$(CC) $(CFLAGS) $(CPPFLAGS) -MM *.c > .dependencies

help:
	@echo "${PROGRAM}: View machine's cbmem contents"
	@echo "Targets: all, clean, distclean, help, install"
	@echo "To disable warnings as errors, run make as:"
	@echo "  make all WERROR=\"\""

.PHONY: all check clean distclean install help

-include .dependencies
