#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include "delivery.h"

int main(int argc, char* argv[]){

    if(argc < 2){
        printf("Use: trucker <command> <args> <options>\n");
        exit(EXIT_FAILURE);
    }

    if(strcmp(argv[1], "deliver") == 0) deliver(argv[2], argv[3]);
    else {
        printf("command unknown: trucker -h to get commands list\n");
        exit(EXIT_FAILURE);
    }
    return 0;
}