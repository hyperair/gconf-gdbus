#! /bin/bash

export GCONFTOOL=`pwd`/../gconf/gconftool

for I in test_*
do
    if ./$I; then
        echo -n "."
    else
        echo
        echo "Test failed: $I"
        exit 1
    fi
done

echo 
echo "All tests passed."