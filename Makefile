CXXFLAGS=-std=c++14 -g -Wall -fsanitize=address

all: cpu

cpu: main.o decode.o memory.o
	$(CXX) $(CXXFLAGS) -o cpu $^

%.o: %.cpp
	$(CXX) $(CXXFLAGS) -c $<

main.o: main.cpp decode.h cpu.h memory.h
decode.o: decode.cpp decode.h cpu.h memory.h
memory.o: memory.cpp memory.h

clean:
	rm -f *.o cpu

.PHONY: clean
