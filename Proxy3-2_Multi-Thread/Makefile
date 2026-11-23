# Makefile for Proxy Assignment 3-2

CC = gcc
CFLAGS = -Wall -g
LIBS = -lssl -lcrypto -lpthread
TARGET = proxy_cache
SRC = proxy_cache.c

all: $(TARGET)

$(TARGET): $(SRC)
	$(CC) $(CFLAGS) -o $@ $^ $(LIBS)

clean:
	rm -f $(TARGET)

