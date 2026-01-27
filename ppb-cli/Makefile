CC = clang
CFLAGS = -Wall -Wextra -Werror -O2 -std=c99 -Ivendor
LDFLAGS = -lcurl -lm

TARGET = put
SOURCES = put.c vendor/cJSON.c
OBJECTS = $(SOURCES:.c=.o)

all: $(TARGET)

$(TARGET): $(OBJECTS)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -f $(OBJECTS) $(TARGET)

install: $(TARGET)
	mkdir -p ~/.local/bin
	cp $(TARGET) ~/.local/bin/$(TARGET)
	@echo "Installed to ~/.local/bin/$(TARGET)"

.PHONY: all clean install
