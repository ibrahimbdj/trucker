#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <getopt.h>
#include "delivery.h"
#include "options.h"

int main(int argc, char* argv[]){
    if(argc < 2){
        printf("Use: trucker <command> <argument> <options>\n");
        exit(EXIT_FAILURE);
    }
    
    const char* optstring = "t:n:";

    int opt_cur = getopt(argc, argv, optstring);
    struct options** opts = malloc(16*sizeof(struct options*)); 

    int i = 0;
    while(opt_cur != -1){
        if(opt_cur == *"?" || opt_cur == *":") exit(EXIT_FAILURE);

        struct options* option = malloc(sizeof(struct options*));
        option->opt = opt_cur;
        option->opt_arg = optarg;
        *(opts+i) = option;

        opt_cur = getopt(argc, argv, optstring);
        i++;
    }

    if(strcmp(argv[optind], "deliver") == 0) deliver(argv[optind+1], argv[optind+2], opts);
    else {
        printf("command unknown: \"trucker help\" to get commands list\n");
        exit(EXIT_FAILURE);
    }
    return 0;
}