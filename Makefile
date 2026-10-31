CC ?= cc
CFLAGS ?= -std=c11 -O2 -Wall -Wextra -Wpedantic
CPPFLAGS ?= -Iinclude

SOURCES = src/main.c src/value.c src/dictionary.c src/parser.c src/vm.c src/repl.c
OBJECTS = $(SOURCES:.c=.o)
TARGET = forth

.PHONY: all clean test

all: $(TARGET)

$(TARGET): $(OBJECTS)
	$(CC) $(CFLAGS) -o $@ $(OBJECTS)

test: $(TARGET)
	$(CC) $(CPPFLAGS) $(CFLAGS) -o tests/test_forth tests/test_forth.c src/value.c src/dictionary.c src/parser.c src/vm.c src/repl.c
	./tests/test_forth

clean:
	$(RM) $(OBJECTS) $(TARGET) $(TARGET).exe tests/test_forth tests/test_forth.exe
