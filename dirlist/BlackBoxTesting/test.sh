#!/bin/bash

rm *.txt
make clean
make

DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" && pwd )"
./dirlist "$DIR/files/final-src" sout_final-src.txt

sed 's,replace,'"$DIR"',' files/correct_final-src.txt > correct_final-src.txt

if diff -w sout_final-src.txt correct_final-src.txt; then
    echo Test 1 - Success--------------------final-src----------------------------Success
else
    echo Test 1 - Fail-----------------------final-src----------------------------Fail
fi

./dirlist "$DIR/files/linux-master" sout_linux-master.txt

sed 's,replace,'"$DIR"',' files/correct_linux-master.txt > correct_linux-master.txt

if diff -w sout_linux-master.txt correct_linux-master.txt; then
    echo Test 2 - Success------------------linux-master---------------------------Success
else
    echo Test 2 - Fail---------------------linux-master---------------------------Fail
fi
