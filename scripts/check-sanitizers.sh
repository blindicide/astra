#!/bin/sh
set -eu

work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT HUP INT TERM
cp -a Makefile src include vendor tests bootstrap "$work/"

flags='-O0 -g -fsanitize=address,undefined -fno-omit-frame-pointer'
make -C "$work" -j2 CXXFLAGS="$flags" CFLAGS="$flags" \
    LDLIBS='-ldl -lpthread -ljson-c -lcrypto -fsanitize=address,undefined'
ASAN_OPTIONS='detect_leaks=1:halt_on_error=1' \
UBSAN_OPTIONS='halt_on_error=1:print_stacktrace=1' \
    make -C "$work" check
