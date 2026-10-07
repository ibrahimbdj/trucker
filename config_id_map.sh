#!/bin/sh

#newuidmap et newgidmap a faire une fois les options presentes ou alors cas par defaut

newuidmap "$1" 0 1000 1 1 100000 65536
newgidmap "$1" 0 1000 1 1 100000 65536
