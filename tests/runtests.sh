#! /bin/bash

export GCONFTOOL=`pwd`/../gconf/gconftool
LOGFILE=runtests.log
POTENTIAL_TESTS='testdirlist testgconf testlisteners testschemas testpersistence testaddress'

for I in $POTENTIAL_TESTS
do
    GOOD=yes
    test -f $I || {
        echo "WARNING: test program $I not found, not running"
        GOOD=no
    }

    if test x$GOOD = xyes; then
        test -x $I || {
            echo "WARNING: test program $I is not executable, not running"
            GOOD=no
        }
    fi
    
    if test x$GOOD = xyes; then
        TESTS="$TESTS$I "
    fi
done

echo "Logging to $LOGFILE"

echo "Log file for GConf test programs." > $LOGFILE
echo "" >> $LOGFILE
echo "Tests are: "$TESTS >> $LOGFILE
echo "" >> $LOGFILE

for I in $TESTS
do
    echo -n "Running test program \"$I\", please wait:"
    echo "" >> $LOGFILE
    echo "Output of $I:" >> $LOGFILE
    LOCALES="C en_US ja_JP ja_JP:en_US:C"
    for L in $LOCALES
    do
        if LANG=$L ./$I >>$LOGFILE 2>&1; then
            echo " passed in $L locale"
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
done

echo 
echo "All tests passed."



