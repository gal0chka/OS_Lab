CC      := gcc
CFLAGS  := -O2 -Wall -Wextra -std=c11
LDFLAGS :=

TARGETS := sender receiver

.PHONY: all clean rebuild run-sender run-receiver

all: $(TARGETS)

sender: sender.c
	$(CC) $(CFLAGS) $< -o $@ $(LDFLAGS)

receiver: receiver.c
	$(CC) $(CFLAGS) $< -o $@ $(LDFLAGS)

clean:
	rm -f $(TARGETS)

rebuild: clean all

run-sender: sender
	./sender

run-receiver: receiver
	./receiver
