#! /bin/bash

export GCONFTOOL=`pwd`/../gconf/gconftool
LOGFILE=runtests.log

echo "Logging to $LOGFILE"

for I in testgconf testlisteners
do
    echo -n "Running test $I, please wait:"
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



