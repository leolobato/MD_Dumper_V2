#!/bin/bash

cmake ./
make LIBRARY_PATH=/usr/local/lib
rm -rf CMakeFiles
rm CMakeCache.txt
rm cmake_install.cmake
rm Makefile
