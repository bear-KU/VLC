CC = gcc
CFLAGS = -Wall -pthread
TARGET = server4
SRCS = server4.c cJSON.c

all: $(TARGET)

$(TARGET): $(SRCS)
	$(CC) $(CFLAGS) -o $(TARGET) $(SRCS)

clean:
	rm -f $(TARGET)
