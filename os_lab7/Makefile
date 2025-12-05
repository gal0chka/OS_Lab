CC      := gcc
CFLAGS  := -O2 -Wall -Wextra -pedantic
LDFLAGS := -pthread

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
