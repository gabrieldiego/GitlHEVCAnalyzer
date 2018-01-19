#!/bin/bash

# Build the qmake part
make

# Build the av1 sources
rm decoders/av1
cd reference_decoders/av1_build/
make
cd -
cp ./reference_decoders/av1_build/aomdec decoders/av1
