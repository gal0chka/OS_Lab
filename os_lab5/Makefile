CC      := gcc
CFLAGS  := -Wall -Wextra -O2 -std=c11
LDFLAGS :=

PROG := archiver

.PHONY: all clean

all: $(PROG)

$(PROG): archiver.c
	$(CC) $(CFLAGS) archiver.c -o $(PROG) $(LDFLAGS)

clean:
	rm -f $(PROG)
