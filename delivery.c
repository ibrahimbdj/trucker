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

#define ARRAY_SIZE(a) ((int)(sizeof(a)/sizeof(a[0])))

struct chargs {
    pid_t ppid;
    uid_t rluid;
    gid_t rlgid;
    const char* root;
    const char* prog;
    char** argv;
};

struct mount_args {
    const char* source; 
    const char* target;
    const char* filesystemtype;
    unsigned long mountflags;
    const void* data;
};

static struct mount_args default_mount_newfs[] = {
    {"tmp", "/tmp", "tmpfs", MS_NODEV | MS_NOSUID, "mode=1777"},
    {"run", "/run", "tmpfs", MS_NODEV | MS_NOSUID, "mode=755"},
    {"dev", "/dev", "tmpfs", MS_NOSUID | MS_STRICTATIME, "mode=755"},
    {"sys", "/sys", "sysfs", MS_NODEV | MS_NOSUID | MS_NOEXEC | MS_RDONLY, NULL},
    {"devpts", "/dev/pts", "devpts", MS_NOSUID | MS_NOEXEC, "newinstance,mode=0620,ptmxmode=0666"}
};

static struct mount_args default_pid_newfs[] = {
    {"proc", "/proc", "proc", MS_NODEV | MS_NOSUID | MS_NOEXEC,  NULL}
};

static struct mount_args default_ipc_newfs[] = {
    {"shm", "/dev/shm", "tmpfs", MS_NODEV | MS_NOSUID | MS_NOEXEC,  "mode=1777"},
    {"mqueue", "/dev/mqueue", "mqueue", MS_NODEV | MS_NOSUID | MS_NOEXEC,  NULL}
};

static struct mount_args default_cgroup_newfs[] = {
    {"cgroup", "/sys/fs/cgroup", "cgroup2", MS_NODEV | MS_NOSUID | MS_NOEXEC | MS_RDONLY,  NULL}
};

static struct mount_args default_mount_bind[] = {
    {"/dev/null", "/dev/null", NULL, MS_BIND, NULL},
    {"/dev/zero", "/dev/zero", NULL, MS_BIND, NULL},
    {"/dev/full", "/dev/full", NULL, MS_BIND, NULL},
    {"/dev/random", "/dev/random", NULL, MS_BIND, NULL},
    {"/dev/urandom", "/dev/urandom", NULL, MS_BIND, NULL},
    {"/dev/tty", "/dev/tty", NULL, MS_BIND, NULL}
};

static char* default_symlink[][2] = {
    {"/proc/self/fd", "/dev/fd"},
    {"/proc/self/fd/0", "/dev/stdin"},
    {"/proc/self/fd/1", "/dev/stdout"},
    {"/proc/self/fd/2", "/dev/stderr"},
    {"/dev/pts/ptmx", "/dev/ptmx"}
};

static int fn_id_map(unsigned int id, char* type){

    if(strcmp(type, "uid") != 0 && strcmp(type, "gid") != 0) return -1;

    char id_map_path[64];
    char id_map_entry[64];  

    sprintf(id_map_path, "/proc/self/%s_map", type);
    sprintf(id_map_entry, "0 %u 1", id);

    FILE* id_map = fopen(id_map_path, "w");
    
    fwrite(id_map_entry, 1, strlen(id_map_entry), id_map);
    fclose(id_map);

    return 0;
}

static char* hostname_gen(pid_t pid){
    unsigned int ihname = 0;
    char* hostname = malloc(37*sizeof(char));

    if(hostname == NULL){
        fprintf(stderr, "malloc failed: %s\n", strerror(errno));
        return NULL;
    }

    srand(time(NULL) + pid);

    for(int i = 3; i >= 0; i--){
        unsigned int hostname_section_i = rand() % 256;
        ihname = ihname | (hostname_section_i << (i*8));
    }
    
    if(sprintf(hostname, "ctr-%08x", ihname) < 0){
        fprintf(stderr, "malloc failed: %s\n", strerror(errno));
        return NULL;
    }

    return hostname;
}

static int sethname(pid_t pid){
    char* hostname = hostname_gen(pid);
    if(hostname == NULL) return -1;

    if(sethostname(hostname, strlen(hostname)) < 0){
        fprintf(stderr, "sethostname failed: %s\n", strerror(errno));
    }

    free(hostname);

    return 0;
}

static int symlink_wrapper(const char* target, const char* link_path, const char* root_path){
    char* real_link_path = malloc((strlen(link_path) + strlen(root_path) + 1) * sizeof(char));
    strcpy(real_link_path, root_path);
    strcat(real_link_path, link_path);

    if(symlink(target , real_link_path) < 0){
        fprintf(stderr, "symlink %s -> %s failed: %s\n", link_path, target, strerror(errno));
        return -1;
    }
    return 0;
}

static int mount_bind_file(struct mount_args* args, const char* root_path){
    char* real_target = malloc((strlen(args->target) + strlen(root_path) + 1) * sizeof(char));
    strcpy(real_target, root_path);
    strcat(real_target, args->target);

    FILE* f = fopen(real_target, "w");
    if(f) fclose(f);
    else {
        fprintf(stderr, "file %s creation failed: %s\n", real_target, strerror(errno));
        return -1;
    }

    if(mount(args->source, real_target, NULL, MS_BIND, NULL) < 0){
        fprintf(stderr, "Mount %s on %s failed: %s\n", args->source, real_target, strerror(errno));
        free(real_target);
        return -1;
    }
    free(real_target);
    return 0;
}

static int mount_bind_dir(struct mount_args* args, const char* root_path){
    char* real_target = malloc((strlen(args->target) + strlen(root_path) + 1) * sizeof(char));
    strcpy(real_target, root_path);
    strcat(real_target, args->target);

    DIR* dir = opendir(real_target);
    if(dir) closedir(dir);
    else {
        if(mkdir(real_target, 0755) < 0){
            fprintf(stderr, "mkdir %s failed: %s", real_target, strerror(errno));
            return -1;
        }
    }

    if(mount(args->source, real_target, NULL, MS_BIND, NULL) < 0){
        fprintf(stderr, "Mount %s on %s failed: %s\n", args->source, real_target, strerror(errno));
        free(real_target);
        return -1;
    }
    free(real_target);
    return 0;
}

static int mount_newfs(struct mount_args* args, const char* root_path){

    char* real_target = malloc((strlen(args->target) + strlen(root_path) + 1) * sizeof(char));
    strcpy(real_target, root_path);
    strcat(real_target, args->target);

    DIR* dir = opendir(real_target);
    if(dir) closedir(dir);
    else{
        if (mkdir(real_target, 0700) < 0){
            fprintf(stderr, "mkdir %s failed: %s\n", real_target, strerror(errno));
            return -1;
        }
    }
    if(mount(args->source, real_target, args->filesystemtype, args->mountflags, args->data) < 0){
        fprintf(stderr, "mount %s failed: %s\n", real_target, strerror(errno));
        return -1;
    }
    free(real_target);
    return 0;
}

static int for_mount(  int mount_type_fn(struct mount_args* args, const char* root_path), 
                struct mount_args tab_args[], 
                int tab_args_size,
                const char* root_path){

    for(int i = 0; i < tab_args_size; i++){
        if(mount_type_fn(&(tab_args[i]), root_path) < 0) return -1;
    }
    return 0;
}

static int pivot_root_wrapper(const char*  container_root){
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

    return 0;
}

static int config_uts_ns(pid_t ppid){
    return sethname(ppid);
}

static int config_mount_ns(const char* container_root){
    
    if(for_mount(&mount_newfs, 
                default_mount_newfs, 
                ARRAY_SIZE(default_mount_newfs), 
                container_root) < 0) 
        return -1;

    if(for_mount(&mount_bind_file, 
                default_mount_bind, 
                ARRAY_SIZE(default_mount_bind), 
                container_root) < 0) 
        return -1;

    for(int i = 0; i < ARRAY_SIZE(default_symlink); i++){
        if(symlink_wrapper(default_symlink[i][0], default_symlink[i][1], container_root) < 0) return -1;
    }

    return 0;
}

static int config_pid_ns(const char* container_root){
    return for_mount(&mount_newfs, default_pid_newfs, ARRAY_SIZE(default_pid_newfs), container_root);
}

static int config_ipc_ns(const char* container_root){
    return for_mount(&mount_newfs, default_ipc_newfs, ARRAY_SIZE(default_ipc_newfs), container_root);
}

static int config_cgroup_ns(const char* container_root){
    return for_mount(&mount_newfs, default_cgroup_newfs, ARRAY_SIZE(default_cgroup_newfs), container_root);
}

static int config_user_ns(uid_t uid, gid_t gid){
    FILE* setgroups = fopen("/proc/self/setgroups", "w");
    fwrite("deny", 1, strlen("deny"), setgroups);
    fclose(setgroups);

    fn_id_map(uid, "uid");
    fn_id_map(gid, "gid");

    return 0;
}

static int init_root(const char* container_root){
    if(mount(NULL, "/", NULL, MS_PRIVATE | MS_REC, NULL) < 0){
        fprintf(stderr, "Mount ns propagation change failed: %s\n", strerror(errno));
        return -1;
    }

    return mount_bind_dir(&((struct mount_args){container_root, container_root, NULL, MS_BIND, NULL}), "");
}

static int fn_child(void* arg){
    struct chargs* charg = arg;

    if(config_user_ns(charg->rluid, charg->rlgid) < 0) return -1;
    if(init_root(charg->root) < 0) return -1;
    if(config_uts_ns(charg->ppid) < 0) return -1;
    if(config_mount_ns(charg->root) < 0) return -1;
    if(config_pid_ns(charg->root) < 0) return -1;
    if(config_ipc_ns(charg->root) < 0) return -1;
    if(config_cgroup_ns(charg->root) < 0) return -1;
    if(pivot_root_wrapper(charg->root) < 0) return -1;
    
    execve(charg->prog, charg->argv, NULL);
    fprintf(stderr, "execve failed: %s\n", strerror(errno));
    return -1;
}

static void truckerpid(pid_t chpid, pid_t ppid){
    char* pidc = malloc(20*sizeof(char));
    sprintf(pidc, "%d\n%d\n", chpid, ppid);
    FILE* pidlist = fopen("pidlist", "a");
    fwrite(pidc, 1, strlen(pidc), pidlist);
    fclose(pidlist);
    free(pidc);
}

int deliver(const char* container_root, 
            const char* container_prog, 
            char* container_argv[], 
            struct options** opts){
                
    int status;
    container_prog = "/bin/sh";
    container_root = "/home/ibrahimbdj/trucker/conteneur-root";
    
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
    charg->root = container_root;
    charg->prog = container_prog;
    charg->argv = container_argv;

    if(unshare(CLONE_NEWUSER | CLONE_NEWTIME) < 0){
        fprintf(stderr, "unshare time ns failed: %s\n", strerror(errno));
        exit(EXIT_FAILURE);
    }
    
    pid_t chpid = clone(&fn_child, stack+stacksize,
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

    int wait_ret = waitpid(chpid, &status, 0);
    if( wait_ret < 0) {
        fprintf(stderr, "waitpid failed\n");
        exit(EXIT_FAILURE);
    }

    int ret = 0;
    if(WIFEXITED(status)){
        ret =  WEXITSTATUS(status);
        printf("child exit status: %d\n", ret);
    }else if(WIFSIGNALED(status)){
        ret =  128 + WTERMSIG(status);
        printf("child end on signal: %d (%s)\n", WTERMSIG(status), strsignal(WTERMSIG(status)));
    }

    //return à refaire
    //nettoyage ressource à faire

    return ret;
}