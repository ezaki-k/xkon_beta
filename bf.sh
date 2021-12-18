#!/bin/bash
riscv32-unknown-elf-g++ -O2 xkon_bf.cpp -g3 -fno-operator-names -std=c++14 -Wall -march=rv32g -o  a.out
spike --isa=rv32gc pk a.out

