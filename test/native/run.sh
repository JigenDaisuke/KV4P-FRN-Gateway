#!/bin/sh
# Build and run the native tests on a PC (Linux/macOS, gcc or clang).
set -e
cd "$(dirname "$0")/../.."
mkdir -p .build-test
cc -c -O2 -w -Ilib/md5/src lib/md5/src/md5.c -o .build-test/md5.o
for f in lib/gsm/src/*.c; do
  cc -c -O2 -DWAV49 -DSASR -DNeedFunctionPrototypes=1 -w -Ilib/gsm/src "$f" -o ".build-test/$(basename "$f" .c).o"
done
c++ -std=gnu++17 -O2 -Wall -Wextra -Isrc -Ilib/gsm/src -Ilib/md5/src test/native/test_main.cpp src/frn_client.cpp src/frn_codec.cpp src/echolink.cpp \
    .build-test/*.o -o .build-test/tests
./.build-test/tests
