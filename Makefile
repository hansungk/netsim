# CC=gcc
CFLAGS=-g -Wall -fsanitize=address

all: cpu

cpu: main.o decode.o
	$(CXX) $(CFLAGS) -o cpu $^

%.o: %.cc
	$(CXX) $(CFLAGS) -c $<

decode.o: decode.cc cpu.h

clean:
	rm -f *.o cpu

.PHONY: clean
