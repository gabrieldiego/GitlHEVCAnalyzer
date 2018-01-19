#!/bin/bash

qmake -qt=qt5 VideoAnalyzer.pro -r "CONFIG+=Release"

mkdir -p reference_decoders/av1_build
cd reference_decoders/av1_build/
cmake ../av1
cd -

