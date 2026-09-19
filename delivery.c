#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <sched.h>
#include <signal.h>
#include <unistd.h>
#include <string.h>
#include <sys/wait.h>

int sethname(){
    const char hostname[64] = "conteneur";
    return sethostname(hostname, strlen(hostname));
}

int fchild(void* chargs){
    
    if(sethname() < 0){
        printf("sethostname failed");
        exit(EXIT_FAILURE);
    }
    sleep(10000);
    return 0;
}

int deliver(char* envPath, char* exePath){
    int status;
    exePath = "/bin/bash";

    size_t stacksize = (1024*1024);
    char* stack = malloc(1024*1024);
    if(stack == NULL){
        printf("malloc child stack failed\n");
        exit(EXIT_FAILURE);
    }

    char* chargs[] = {"child", "arg1"};

    int pid = clone(&fchild, stack+stacksize, CLONE_NEWUSER | CLONE_NEWUTS | SIGCHLD, chargs);

    if(pid < 0){
        printf("clone failed\n");
        exit(EXIT_FAILURE);
    };
    printf("%d\n", pid);
    waitpid(pid, &status, 0);

    printf("fin parent\n");
    return 0;
}