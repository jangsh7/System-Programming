CC = gcc
TARGET = proxy_cache

all: $(TARGET)

$(TARGET): proxy_cache.c
	$(CC) -o $@ $^ -lssl -lcrypto

clean:
	rm -f $(TARGET)

