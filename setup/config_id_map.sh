#!/bin/sh

mysubuid=$(grep "^$(id -un):" /etc/subuid)
mysubgid=$(grep "^$(id -un):" /etc/subgid)

newuidmap "$1" 0 "$(id -u)" 1 1 $(echo $mysubuid | cut -d":" -f2) $(echo $mysubuid | cut -d":" -f3)

if [ "$?" -eq 1 ]; then
    echo "newidmap failed"
    exit 1
fi

newgidmap "$1" 0 "$(id -g)" 1 1 $(echo $mysubgid | cut -d":" -f2) $(echo $mysubgid | cut -d":" -f3)

if [ "$?" -eq 1 ]; then
    echo "newgidmap failed"
    exit 1
fi
