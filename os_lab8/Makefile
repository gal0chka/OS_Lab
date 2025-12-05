CC      := gcc
CFLAGS  := -O2 -Wall -Wextra -pedantic
LDFLAGS := -pthread

TARGET  := rw
SRC     := main.c

.PHONY: all clean rebuild run

all: $(TARGET)

$(TARGET): $(SRC)
	$(CC) $(CFLAGS) $(SRC) -o $(TARGET) $(LDFLAGS)

run: $(TARGET)
	./$(TARGET)

clean:
	rm -f $(TARGET)

rebuild: clean all
