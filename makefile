CC = gcc
CFLAGS = -Wall -Wextra -std=c99 -O2
TARGET = epoll_server
SOURCE = epoll_server.c

$(TARGET): $(SOURCE)
	$(CC) $(CFLAGS) -o $(TARGET) $(SOURCE)

clean:
	rm -f $(TARGET)

.PHONY: clean