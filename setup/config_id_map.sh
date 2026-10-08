#!/bin/sh

#newuidmap et newgidmap a faire une fois les options presentes ou alors cas par defaut

newuidmap "$1" 0 100000 65536

if [ "$?" -eq 1 ]; then
    echo "newidmap failed"
fi

newgidmap "$1" 0 100000 65536

if [ "$?" -eq 1 ]; then
    echo "newgidmap failed"
fi
