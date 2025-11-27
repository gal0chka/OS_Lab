# Makefile для Практической работы 1 (cat & grep)
CC      := gcc
CFLAGS  := -Wall -Wextra -O2 -std=c11
LDFLAGS :=

PROGS := mycat mygrep

.PHONY: all clean

all: $(PROGS)

mycat: mycat.c
	$(CC) $(CFLAGS) $< -o $@ $(LDFLAGS)

mygrep: mygrep.c
	$(CC) $(CFLAGS) $< -o $@ $(LDFLAGS)

clean:
	rm -f $(PROGS)