CXX := g++
CXXFLAGS := -std=c++17 -O2 -pipe -Wall -Wextra -Wshadow

.PHONY: all clean

all: code

code: main.cpp
	$(CXX) $(CXXFLAGS) main.cpp -o code

clean:
	rm -f code
