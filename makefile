CXXFLAGS += -std=c++11 -O3

all: bwtsearch

test :
	g++ -o bwtsearch bwtsearch.cpp

clean :
	rm bwtsearch