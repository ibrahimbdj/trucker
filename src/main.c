#define _GNU_SOURCE
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <sys/wait.h>
#include "delivery.h"

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

    if(strcmp(argv[1], "deliver") == 0) {
        deliver_launcher(env_path, argv[3], container_argv);
        free(env_path);
    }
    else {
        printf("command unknown: \"trucker help\" to get commands list\n");
        exit(EXIT_FAILURE);
    }

    return 0;
}