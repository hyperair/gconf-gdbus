#! /bin/bash

export GCONFTOOL=`pwd`/../gconf/gconftool
LOGFILE=runtests.log
TESTS='testgconf testlisteners testschemas'

echo "Logging to $LOGFILE"

for I in $TESTS
do
    echo -n "Running test program \"$I\", please wait:"
    if ./$I >$LOGFILE 2>&1; then
        echo " passed"
    else
        echo
        echo
        echo '***'
        echo " Test failed: $I"
        echo " See $LOGFILE for errors"
        echo 
        exit 1
    fi
done

echo 
echo "All tests passed."



