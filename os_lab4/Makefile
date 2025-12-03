CC      := gcc
CFLAGS  := -Wall -Wextra -O2 -std=c11
LDFLAGS :=

PROG := mychmod

.PHONY: all clean

all: $(PROG)

$(PROG): mychmod.c
	$(CC) $(CFLAGS) mychmod.c -o $(PROG) $(LDFLAGS)

clean:
	rm -f $(PROG)
