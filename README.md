# BWT-Search
Data Compression Course Assignment

My first C++ program, learning C++ while coding, good experience

Archive here as reference

./bwtsearch sample.bwt sample.idx "term1" "term2" "term3"

- Runtime memory constrain: 10M
- Index file cannot larger than bwt file
- Up to 3 search terms

to measure memory:
valgrind --tool=massif --pages-as-heap=yes PROGRAM

to measure time:
time PROGRAM

to compile:
make

to profile:
valgrind --tool=callgrind PROGRAM, kcachegrind CALLGRIND_FILE
