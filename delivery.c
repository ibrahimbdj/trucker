#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <sched.h>
#include <signal.h>
#include <unistd.h>
#include <string.h>
#include <sys/wait.h>

struct chargs {
    pid_t ppid;
};

int sethname(){
    const char hostname[64] = "conteneur1";
    return sethostname(hostname, strlen(hostname));
}

int fchild(void* charg){
    printf("debut enfant\n");

    if(sethname() < 0){
        printf("sethostname failed\n");
        exit(EXIT_FAILURE);
    }
    sleep(1);
    printf("fin enfant\n");
    return 0;
}

int deliver(char* envPath, char* exePath){
    int status;
    exePath = "/bin/bash";

    size_t stacksize = (1024*1024);
    char* stack = malloc(1024*1024);

    struct chargs* charg = malloc(sizeof(struct chargs));
    charg->ppid = 10;

    if(stack == NULL){
        printf("malloc child stack failed\n");
        exit(EXIT_FAILURE);
    }

    if(unshare(CLONE_NEWUSER | CLONE_NEWTIME) < 0){
        printf("unshare timens failed\n");
        exit(EXIT_FAILURE);
    }

    
    pid_t pid = clone(&fchild, stack+stacksize,
        CLONE_NEWUTS |
        CLONE_NEWIPC |
        CLONE_NEWCGROUP |
        CLONE_NEWNS |
        CLONE_NEWPID |
        CLONE_NEWNET |
        SIGCHLD, charg);

    if(pid < 0){
        printf("clone failed\n");
        exit(EXIT_FAILURE);
    };
    printf("pid enfant: %d\n", pid);
    waitpid(pid, &status, 0);
    printf("fin parent\n");
    return 0;
}