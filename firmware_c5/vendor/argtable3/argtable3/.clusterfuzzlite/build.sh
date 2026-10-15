#!/bin/bash
mkdir fuzz-build
cd fuzz-build
cmake -DCMAKE_CXX_COMPILER=$CXX -DCMAKE_CXX_FLAGS="$CXXFLAGS" ../
make

$CC $CFLAGS $LIB_FUZZING_ENGINE $SRC/fuzzer.c \
  -Wl,--whole-archive $SRC/argtable3/fuzz-build$SRC/libargtable3_static.a \
  -Wl,--allow-multiple-definition \
  -I$SRC/argtable3/src \
  -o $OUT/fuzzer
