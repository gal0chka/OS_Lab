CC      := gcc
CFLAGS  := -Wall -Wextra -O2 -std=c11
LDFLAGS :=

PROG := ipc_lab

.PHONY: all clean

all: $(PROG)

$(PROG): main.c
	$(CC) $(CFLAGS) main.c -o $(PROG) $(LDFLAGS)

clean:
	rm -f $(PROG)
