# CC=gcc
CXXFLAGS=-std=c++14 -g -Wall -fsanitize=address

all: cpu

cpu: main.o decode.o
	$(CXX) $(CXXFLAGS) -o cpu $^

%.o: %.cc
	$(CXX) $(CXXFLAGS) -c $<

main.o: main.cc cpu.h decode.h
decode.o: decode.cc decode.h

clean:
	rm -f *.o cpu

.PHONY: clean
