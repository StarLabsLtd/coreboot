# SPDX-License-Identifier: GPL-2.0-only

PROGRAM := capsule-tpm-provision
CC ?= cc
PKG_CONFIG ?= pkg-config
CFLAGS ?= -O2
CFLAGS += -std=gnu11 -Wall -Wextra -Werror
CFLAGS += $(shell $(PKG_CONFIG) --cflags libcrypto)
LDLIBS += $(shell $(PKG_CONFIG) --libs libcrypto)

.PHONY: all clean test

all: $(PROGRAM)

$(PROGRAM): main.o policy.o
	$(CC) $(LDFLAGS) -o $@ $^ $(LDLIBS)

%.o: %.c policy.h
	$(CC) $(CPPFLAGS) $(CFLAGS) -c -o $@ $<

clean:
	$(RM) $(PROGRAM) main.o policy.o

test:
	./tests/run.sh
