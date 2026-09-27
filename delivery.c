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
#include <sys/mount.h>
#include <sys/syscall.h>
#include <time.h>
#include <fcntl.h>
#include <dirent.h>
#include <errno.h>
#include "options.h"

struct chargs {
    pid_t ppid;
    uid_t rluid;
    gid_t rlgid;
};

int fn_id_map(unsigned int id, char* type){

    if(strcmp(type, "uid") != 0 && strcmp(type, "gid") != 0) return -1;

    char id_map_path[64];
    char id_map_entry[64];  

    sprintf(id_map_path, "/proc/self/%s_map", type);
    sprintf(id_map_entry, "0 %d 1", id);

    FILE* id_map = fopen(id_map_path, "w");
    
    fwrite(id_map_entry, 1, strlen(id_map_entry), id_map);
    fclose(id_map);

    return 0;
}

char* hname_gen(pid_t pid){
    unsigned int ihname = 0;
    char* chname = malloc(37*sizeof(char));

    if(chname == NULL){
        fprintf(stderr, "malloc failed: %s\n", strerror(errno));
        return NULL;
    }

    srand(time(NULL) + pid);

    for(int i = 3; i >= 0; i--){
        unsigned int hname_section_i = rand() % 255;
        ihname = ihname | (hname_section_i << (i*8));
    }
    
    if(sprintf(chname, "ctr-%08x", ihname) < 0){
        fprintf(stderr, "malloc failed: %s\n", strerror(errno));
        return NULL;
    }

    return chname;
}

int sethname(pid_t pid){
    char* hname = hname_gen(pid);
    if(hname == NULL) return -1;

    if(sethostname(hname, strlen(hname)) < 0){
        fprintf(stderr, "sethostname failed: %s\n", strerror(errno));
    }

    free(hname);

    return 0;
}

int mount_newfs(const char* source, 
                    const char* target, const char* filesystemtype, 
                    unsigned long mountflags,
                    const void* data){

                        if(mount(source, target, filesystemtype, mountflags, data) < 0){
                            fprintf(stderr, "mount %s failed: %s\n", target, strerror(errno));
                            return -1;
                        }

                        return 0;
}

int config_uts_ns(pid_t ppid){
    return sethname(ppid);
}

int config_mount_ns(/*path vers la racine du conteneur à mettre*/){
    const char* container_root = "/home/ibrahimbdj/trucker/conteneur-root";

    if(mount(NULL, "/", NULL, MS_PRIVATE | MS_REC, NULL) < 0){
        fprintf(stderr, "Mount ns propagation change failed: %s\n", strerror(errno));
        return -1;
    }
    
    if(mount(container_root, container_root, NULL, MS_BIND, NULL)){
        fprintf(stderr, "Mount container root on container root failed: %s\n", strerror(errno));
        return -1;
    }

    if(chdir(container_root) < 0){
        fprintf(stderr, "chdir to new root failed: %s\n", strerror(errno));
        return -1;
    }

    if(syscall(SYS_pivot_root, container_root, container_root)  < 0){
        fprintf(stderr, "pivot_root failed: %s\n", strerror(errno));
        return -1;
    }

    if(umount2(".", MNT_DETACH)  < 0){
        fprintf(stderr, "umount2 old root failed: %s\n", strerror(errno));
        return -1;
    }

    if(mount_newfs("tmp", "/tmp", "tmpfs", MS_NODEV | MS_NOSUID, "mode=1777") < 0) return -1;
    if(mount_newfs("run", "/run", "tmpfs", MS_NODEV | MS_NOSUID, "mode=755") < 0) return -1;
    if(mount_newfs("dev", "/dev", "tmpfs", MS_NOSUID | MS_STRICTATIME, "mode=755") < 0) return -1;
    if(mount_newfs("sys", "/sys", "sysfs", MS_NODEV | MS_NOSUID | MS_NOEXEC | MS_RDONLY, NULL) < 0) return -1;

    return 0;
}

int config_pid_ns(){
    return mount_newfs("proc", "/proc", "proc", MS_NODEV | MS_NOSUID | MS_NOEXEC,  NULL);
}

int config_ipc_ns(){
    if(mount_newfs("shm", "/dev/shm", "tmpfs", MS_NODEV | MS_NOSUID | MS_NOEXEC,  "mode=1777") < 0) return -1;
    if(mount_newfs("mqueue", "/dev/mqueue", "mqueue", MS_NODEV | MS_NOSUID | MS_NOEXEC,  NULL) < 0) return -1;

    return 0;
}

int config_user_ns(uid_t uid, gid_t gid){
    FILE* setgroups = fopen("/proc/self/setgroups", "w");
    fwrite("deny", 1, strlen("deny"), setgroups);
    fclose(setgroups);

    fn_id_map(uid, "uid");
    fn_id_map(gid, "gid");

    return 0;
}

int fchild(void* arg){
    printf("debut enfant\n");
    struct chargs* charg = arg;

    if(config_user_ns(charg->rluid, charg->rlgid) < 0) return -1;
    if(config_uts_ns(charg->ppid) < 0) return -1;  
    if(config_mount_ns() < 0) return -1; 
    if(config_pid_ns() < 0) return -1; 
    //if(config_ipc_ns() < 0) return -1;

    char* argv[2];
    argv[0] = "sh";
    argv[1] = NULL;

    if(execve("/bin/sh", argv, NULL) < 0){
        fprintf(stderr, "execve failed: %s\n", strerror(errno));
        return -1;
    }

    printf("fin enfant\n");
    return 0;
}

void truckerpid(pid_t chpid, pid_t ppid){
    char* pidc = malloc(20*sizeof(char));
    sprintf(pidc, "%d\n%d\n", chpid, ppid);
    FILE* pidlist = fopen("pidlist", "a");
    fwrite(pidc, 1, strlen(pidc), pidlist);
    fclose(pidlist);
    free(pidc);
}

int deliver(char* envPath, char* exePath, struct options** opts){
    printf("debut parent\n");

    int status;
    exePath = "/bin/bash";
    
    size_t stacksize = (1024*1024);
    char* stack = malloc(1024*1024);

    if(stack == NULL){
        fprintf(stderr, "malloc child stack failed: %s\n", strerror(errno));
        exit(EXIT_FAILURE);
    }

    struct chargs* charg = malloc(sizeof(struct chargs));
    charg->ppid = getpid();
    charg->rluid = getuid();
    charg->rlgid = getgid();

    //user and time ns config ok
    if(unshare(CLONE_NEWUSER | CLONE_NEWTIME) < 0){
        fprintf(stderr, "unshare timens failed: %s\n", strerror(errno));
        exit(EXIT_FAILURE);
    }
    
    //uts, cgroup, pid, ipc ns config ok
    pid_t chpid = clone(&fchild, stack+stacksize,
        CLONE_NEWUTS |
        CLONE_NEWIPC |
        CLONE_NEWCGROUP |
        CLONE_NEWNS |
        CLONE_NEWPID |
        CLONE_NEWNET |
        SIGCHLD, charg);

    if(chpid < 0){
        fprintf(stderr, "clone failed: %s\n", strerror(errno));
        exit(EXIT_FAILURE);
    };

    truckerpid(chpid, charg->ppid);
    printf("pid enfant: %d\n", chpid);

    waitpid(chpid, &status, 0);
    if(status < 0){
        printf("container failed\n");
    } else printf("container succeeded\n");
    //nettoyage
    printf("fin parent\n");
    return 0;
}