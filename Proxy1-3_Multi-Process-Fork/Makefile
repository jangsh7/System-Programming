CC = gcc
TARGET = proxy_cache
OBJS = proxy_cache.c

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CC) -o $(TARGET) $(OBJS) -lcrypto

clean:
	rm -f $(TARGET)

