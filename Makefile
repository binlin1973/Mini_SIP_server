CC = gcc
CFLAGS = -Wall -g -pthread
OBJ = main.o sip_server.o network_utils.o
DEPS = sip_server.h network_utils.h
TARGET = sip_server

%.o: %.c $(DEPS)
	$(CC) -c -o $@ $< $(CFLAGS)

$(TARGET): $(OBJ)
	$(CC) -o $@ $^ $(CFLAGS)

.PHONY: clean

clean:
	rm -f *.o $(TARGET)
