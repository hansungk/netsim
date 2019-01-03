# CC=gcc
CFLAGS=-g -Wall -fsanitize=address

all: cpu

cpu: main.o
	$(CXX) $(CFLAGS) -o cpu $^

%.o: %.cc
	$(CXX) $(CFLAGS) -c $<

clean:
	rm -f *.o cpu

.PHONY: clean
