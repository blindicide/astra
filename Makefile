CXX ?= g++
CC ?= cc
PREFIX ?= /usr/local
CXXFLAGS ?= -O2
CFLAGS ?= -O2
CPPFLAGS += -Iinclude -Ivendor/sqlite -D_POSIX_C_SOURCE=200809L
ASTRA_WARN := -Wall -Wextra -Wpedantic -Werror
LDLIBS := -ldl -lpthread -ljson-c -lcrypto

CPP_SOURCES := $(wildcard src/*.cpp)
CPP_OBJECTS := $(patsubst src/%.cpp,build/%.o,$(CPP_SOURCES))

.PHONY: all check check-sanitizers install clean
all: build/astra

build/astra: $(CPP_OBJECTS) build/sqlite3.o
	$(CXX) $(CXXFLAGS) $^ $(LDLIBS) -o $@

build/%.o: src/%.cpp
	mkdir -p build
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) $(ASTRA_WARN) -std=c++20 -MMD -MP -c $< -o $@

build/sqlite3.o: vendor/sqlite/sqlite3.c vendor/sqlite/sqlite3.h
	mkdir -p build
	$(CC) $(CPPFLAGS) $(CFLAGS) -DSQLITE_THREADSAFE=1 -DSQLITE_OMIT_LOAD_EXTENSION -c $< -o $@

check: build/astra
	sh tests/cli.sh ./build/astra
	sh tests/database.sh ./build/astra
	sh tests/repository.sh ./build/astra
	sh tests/diagnostics.sh ./build/astra
	sh tests/transactions.sh ./build/astra
	sh tests/fault_boundaries.sh ./build/astra
	sh tests/bootstrap.sh

check-sanitizers:
	sh scripts/check-sanitizers.sh

install: build/astra
	install -Dm755 build/astra $(DESTDIR)$(PREFIX)/bin/astra

clean:
	rm -f build/*.o build/*.d build/astra

-include $(CPP_OBJECTS:.o=.d)
