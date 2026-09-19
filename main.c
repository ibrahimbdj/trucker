#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include "delivery.h"

int main(int argc, char* argv[]){
    if(strcmp(argv[1], "deliver") == 0) deliver(argv[2], argv[3]);
    return 0;
}