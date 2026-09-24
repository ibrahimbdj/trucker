#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <unistd.h>
#include <sched.h>
#include <signal.h>
#include <string.h>
#include <sys/wait.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <time.h>
#include <fcntl.h>
#include "options.h"

struct chargs {
    pid_t ppid;
};

char* hname_gen(pid_t pid){
    unsigned int ihname = 0;
    char* chname = malloc(37*sizeof(char));

    srand(time(NULL) + pid);

    for(int i = 3; i >= 0; i--){
        unsigned int hname_section_i = rand() % 255;
        ihname = ihname | (hname_section_i << (i*8));
    }
    
    sprintf(chname, "ctr-%08x", ihname);

    return chname;
}

int sethname(pid_t pid){
    char* hname = hname_gen(pid);
    return sethostname(hname, strlen(hname));
}

int fchild(void* arg){
    printf("debut enfant\n");
    struct chargs* charg = arg;
    if(sethname(charg->ppid) < 0){
        printf("sethostname failed\n");
        exit(EXIT_FAILURE);
    }
    sleep(1000);
    printf("fin enfant\n");
    return 0;
}

void truckerpid(pid_t chpid, pid_t ppid){
    char* pidc = malloc(20*sizeof(char));
    sprintf(pidc, "%d\n%d\n", chpid, ppid);
    FILE* pidlist = fopen("pidlist", "a");
    fwrite(pidc, 1, strlen(pidc), pidlist);
    fclose(pidlist);
}

int fn_id_map(unsigned int id, char* type){

    if(strcmp(type, "uid") != 0 && strcmp(type, "gid") != 0) return -1;

    char* id_map_path = malloc(32*sizeof(char));
    char* id_map_entry = malloc(16*sizeof(char));  

    sprintf(id_map_path, "/proc/self/%s_map", type);
    sprintf(id_map_entry, "0 %d 1", id);

    FILE* id_map = fopen(id_map_path, "w");

    if(id_map == NULL){
        printf("fopen id_map failed\n");
        exit(EXIT_FAILURE);
    }
    
    fwrite(id_map_entry, 1, strlen(id_map_entry), id_map);
    fclose(id_map);

    free(id_map_path);
    free(id_map_entry);

    return 0;
}

int deliver(char* envPath, char* exePath, struct options** opts){
    printf("debut parent\n");

    uid_t uid = getuid();
    gid_t gid = getgid();
    printf("gid: %d\n", gid);
    int status;
    exePath = "/bin/bash";

    size_t stacksize = (1024*1024);
    char* stack = malloc(1024*1024);

    if(stack == NULL){
        printf("malloc child stack failed\n");
        exit(EXIT_FAILURE);
    }

    struct chargs* charg = malloc(sizeof(struct chargs));
    charg->ppid = getpid();

    //user and time ns config ok
    if(unshare(CLONE_NEWUSER | CLONE_NEWTIME) < 0){
        printf("unshare timens failed\n");
        exit(EXIT_FAILURE);
    }
    
    FILE* setgroups = fopen("/proc/self/setgroups", "w");
    fwrite("deny", 1, strlen("deny"), setgroups);
    fclose(setgroups);

    fn_id_map(uid, "uid");
    fn_id_map(gid, "gid");
    
    //uts ns config ok
    pid_t chpid = clone(&fchild, stack+stacksize,
        CLONE_NEWUTS |
        CLONE_NEWIPC |
        CLONE_NEWCGROUP |
        CLONE_NEWNS |
        CLONE_NEWPID |
        CLONE_NEWNET |
        SIGCHLD, charg);

    if(chpid < 0){
        printf("clone failed\n");
        exit(EXIT_FAILURE);
    };

    truckerpid(chpid, charg->ppid);
    printf("pid enfant: %d\n", chpid);

    waitpid(chpid, &status, 0);
    printf("fin parent\n");
    return 0;
}