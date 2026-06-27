CXX=g++
CFLAGS=-Wall -Wextra -std=c++20 -O2 -ggdb -lreadline -fpermissive

install: bk
	./install

bk: bk.cpp
	$(CXX) $(CFLAGS) bk.cpp -o bk

.PHONY: install
