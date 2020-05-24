#!/bin/bash

make
make bin
rm Sega_Dumper.elf
mkdir -p out
mv *.bin out/
