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

int symlink_wrapper(const char* target, const char* linkpath){
    if(symlink(target , linkpath) < 0){
        fprintf(stderr, "symlink %s -> %s failed: %s\n", target, linkpath, strerror(errno));
        return -1;
    }
    return 0;
}

int mount_bind(const char* source, const char* target, const char* root_target, char* type){
    char* real_target = malloc((strlen(target) + strlen(root_target) + 1) * sizeof(char));
    strcpy(real_target, root_target);
    strcat(real_target, target);

    if(strcmp(type, "dir") == 0){
        DIR* dir = opendir(real_target);
        if(dir) closedir(dir);
        else {
            if(mkdir(real_target, 0755) < 0){
                fprintf(stderr, "mkdir %s failed: %s", real_target, strerror(errno));
                return -1;
            }
        }
    }else if(strcmp(type, "file") == 0){
        FILE* f = fopen(real_target, "w");
        if(f) fclose(f);
        else {
            fprintf(stderr, "file %s creation failed: %s\n", real_target, strerror(errno));
            return -1;
        }
    } else {
        fprintf(stderr, "mount_bind: wrong type\n");
        return -1;
    }

    if(mount(source, real_target, NULL, MS_BIND, NULL) < 0){
        fprintf(stderr, "Mount %s on %s failed: %s\n", source, real_target, strerror(errno));
        free(real_target);
        return -1;
    }
    free(real_target);
    return 0;
}

int mount_newfs(const char* source, 
                    const char* target,
                    const char* filesystemtype,
                    const char* root_target,
                    unsigned long mountflags,
                    const void* data, mode_t mode){

    char* real_target = malloc((strlen(target) + strlen(root_target) + 1) * sizeof(char));
    strcpy(real_target, root_target);
    strcat(real_target, target);

    DIR* dir = opendir(real_target);
    if(dir) closedir(dir);
    else{
        if (mkdir(real_target, mode) < 0){
            fprintf(stderr, "mkdir %s failed: %s\n", real_target, strerror(errno));
            return -1;
        }
    }
    if(mount(source, real_target, filesystemtype, mountflags, data) < 0){
        fprintf(stderr, "mount %s failed: %s\n", real_target, strerror(errno));
        return -1;
    }
    free(real_target);
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

    if(mount_bind(container_root, container_root, "", "dir") < 0) return -1;

    if(mount_newfs("tmp", "/tmp", "tmpfs", container_root, MS_NODEV | MS_NOSUID, "mode=1777", 1777) < 0) return -1;
    if(mount_newfs("run", "/run", "tmpfs", container_root, MS_NODEV | MS_NOSUID, "mode=755", 0755) < 0) return -1;
    if(mount_newfs("dev", "/dev", "tmpfs", container_root, MS_NOSUID | MS_STRICTATIME, "mode=755", 0755) < 0) return -1;
    if(mount_newfs("sys", "/sys", "sysfs", container_root, MS_NODEV | MS_NOSUID | MS_NOEXEC | MS_RDONLY, NULL, 0555) < 0) return -1;
    if(mount_newfs("devpts", "/dev/pts", "devpts", container_root, MS_NOSUID | MS_NOEXEC, "newinstance,mode=0620,ptmxmode=0666", 0755) < 0) return -1;
    
    if(mount_bind("/dev/null", "/dev/null", container_root, "file") < 0) return -1;
    if(mount_bind("/dev/zero", "/dev/zero", container_root, "file") < 0) return -1;
    if(mount_bind("/dev/full", "/dev/full", container_root, "file") < 0) return -1;
    if(mount_bind("/dev/random", "/dev/random", container_root, "file") < 0) return -1;
    if(mount_bind("/dev/urandom", "/dev/urandom", container_root, "file") < 0) return -1;
    if(mount_bind("/dev/tty", "/dev/tty", container_root, "file") < 0) return -1;

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

    if(symlink_wrapper("/proc/self/fd", "/dev/fd") < 0) return -1;
    if(symlink_wrapper("/proc/self/0", "/dev/stdin") < 0) return -1;
    if(symlink_wrapper("/proc/self/1", "/dev/stdout") < 0) return -1;
    if(symlink_wrapper("/proc/self/2", "/dev/stderr") < 0) return -1;



    return 0;
}

int config_pid_ns(){
    return mount_newfs("proc", "/proc", "proc", "", MS_NODEV | MS_NOSUID | MS_NOEXEC,  NULL, 0555);
}

int config_ipc_ns(){
    if(mount_newfs("shm", "/dev/shm", "tmpfs", "", MS_NODEV | MS_NOSUID | MS_NOEXEC,  "mode=1777", 1777) < 0) return -1;
    if(mount_newfs("mqueue", "/dev/mqueue", "mqueue", "", MS_NODEV | MS_NOSUID | MS_NOEXEC,  NULL, 1777) < 0) return -1;

    return 0;
}

int config_cgroup_ns(){
    return mount_newfs("cgroup", "/sys/fs/cgroup", "cgroup2", "", MS_NODEV | MS_NOSUID | MS_NOEXEC | MS_RDONLY,  NULL, 1777);
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
    struct chargs* charg = arg;

    if(config_user_ns(charg->rluid, charg->rlgid) < 0) return -1;
    if(config_uts_ns(charg->ppid) < 0) return -1;  
    if(config_mount_ns() < 0) return -1; 
    if(config_pid_ns() < 0) return -1; 
    if(config_ipc_ns() < 0) return -1;
    if(config_cgroup_ns() < 0) return -1;

    char* argv[2];
    argv[0] = "sh";
    argv[1] = NULL;

    if(execve("/bin/sh", argv, NULL) < 0){
        fprintf(stderr, "execve failed: %s\n", strerror(errno));
        return -1;
    }

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
        fprintf(stderr, "unshare time ns failed: %s\n", strerror(errno));
        exit(EXIT_FAILURE);
    }
    
    //uts, cgroup, pid, ipc, mount ns config ok
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
    printf("child pid: %d\n", chpid);

    waitpid(chpid, &status, 0);
    if(status < 0){
        printf("container failed\n");
    } else printf("container succeeded\n");
    //nettoyage
    return 0;
}