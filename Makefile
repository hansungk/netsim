CC=gcc
CFLAGS=-g -Wall -fsanitize=address,leak

all: cpu

cpu: main.o
	$(CC) $(CFLAGS) -o cpu $^

%.o: %.c
	$(CC) $(CFLAGS) -c $<

clean:
	rm -f *.o cpu

.PHONY: clean
