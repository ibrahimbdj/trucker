#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <getopt.h>
#include "delivery.h"
#include "options.h"

int main(int argc, char* argv[]){
    if(argc < 4){
        printf("Use: trucker <command> <environment> <program> <options>\n");
        exit(EXIT_FAILURE);
    }

    char* container_argv[argc - 2];
    int last = sizeof(container_argv)/sizeof(container_argv[0]) - 1;

    for(int i = 0; i < last; i++){
        container_argv[i] = argv[3+i];
    }

    container_argv[last] = NULL;

    char* env_path = realpath(argv[2], NULL);

    // const char* optstring = "t:n:";

    // int opt_cur = getopt(argc, argv, optstring);
    // struct options** opts = malloc(16*sizeof(struct options*)); 

    // int i = 0;
    // while(opt_cur != -1){
    //     if(opt_cur == *"?" || opt_cur == *":") exit(EXIT_FAILURE);

    //     struct options* option = malloc(sizeof(struct options));
    //     option->opt = opt_cur;
    //     option->opt_arg = optarg;
    //     *(opts+i) = option;

    //     opt_cur = getopt(argc, argv, optstring);
    //     i++;
    // }

    if(strcmp(argv[1/*optind*/], "deliver") == 0) {
        deliver_launcher(env_path, argv[3], container_argv/*, opts*/);
        free(env_path);
    }
    else {
        printf("command unknown: \"trucker help\" to get commands list\n");
        exit(EXIT_FAILURE);
    }
    return 0;
}