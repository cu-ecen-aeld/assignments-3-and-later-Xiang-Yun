CC = $(CROSS_COMPILE)gcc
CFLAGS = -Wall -Wextra -O2
TARGET = aesdsocket
SRCS = aesdsocket.c
OBJS = $(SRCS:.c=.o)

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) -o $@ $^

clean:
	rm -f $(TARGET) $(OBJS)

.PHONY: all clean
