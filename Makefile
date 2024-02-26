CC=gcc
CFLAGS=-std=c89 -Wpedantic

TARGET1 = master
TARGET2 = port
TARGET3 = ship

OBJ1 = master.o
OBJ2 = port.o
OBJ3 = ship.o

$(TARGET1): $(OBJ1)
	$(CC) $(CFLAGS) $(OBJ1) -o $(TARGET1)

$(TARGET2): $(OBJ2)
	$(CC) $(CFLAGS) $(OBJ2) -o $(TARGET2)

$(TARGET3): $(OBJ3)
	$(CC) $(CFLAGS) $(OBJ3) -o $(TARGET3) -lm

all: $(TARGET1) $(TARGET2) $(TARGET3)

clean: 
	rm $(TARGET1) $(TARGET2) $(TARGET3) *.o
	clear

run:
	./master
