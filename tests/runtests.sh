#! /bin/bash

export GCONFTOOL=`pwd`/../gconf/gconftool
LOGFILE=runtests.log
TESTS='testgconf testlisteners testschemas'

echo "Logging to $LOGFILE"

echo "Log file for GConf test programs." > $LOGFILE
echo "" >> $LOGFILE

for I in $TESTS
do
    echo -n "Running test program \"$I\", please wait:"
    echo "" >> $LOGFILE
    echo "Output of $I:" >> $LOGFILE
    if ./$I >>$LOGFILE 2>&1; then
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



