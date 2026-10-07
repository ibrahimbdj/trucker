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

struct child_args {
    pid_t ppid;
    uid_t real_uid;
    gid_t real_gid;
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

struct symlink_args {
    const char* target;
    const char* linkpath;
};

struct sync_pipes {
    int container_go[2];
    int container_back[2];
    int getcldpid_pipe[2];
    int ret[2];
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

static struct symlink_args default_symlink[] = {
    {"/proc/self/fd", "/dev/fd"},
    {"/proc/self/fd/0", "/dev/stdin"},
    {"/proc/self/fd/1", "/dev/stdout"},
    {"/proc/self/fd/2", "/dev/stderr"},
    {"/dev/pts/ptmx", "/dev/ptmx"}
};

static struct sync_pipes cfgpipes;

static char* ok_pipe = "ok";

static size_t size_ok_pipe = 3;


static char* get_trucker_path(){
    static char* trucker_var = "";
    trucker_var = secure_getenv("TRUCKER");
    return trucker_var;
}

static int close_pipe(int pipefd[]){

    if(close(pipefd[0]) < 0){
        fprintf(stderr, "close pipefd[0]  failed: %s\n", strerror(errno));
        return -1;
    }

    if(close(pipefd[1]) < 0){
        fprintf(stderr, "close pipefd[1]  failed: %s\n", strerror(errno));
        return -1;
    }

    return 0;
}

static void* malloc_wrapper(size_t malloc_size){
    void* ptr = malloc(malloc_size);

    if(ptr == NULL){
        fprintf(stderr, "malloc failed: %s\n", strerror(errno));
        return NULL;
    }

    return ptr;
}

static int write_wrapper(int fd, void* buf, size_t count){
    if(write(fd, buf, count) < 0){
        fprintf(stderr, "Write to pipe failed: %s\n", strerror(errno));
        return -1;
    }
    return 0;
}

static int read_wrapper(int fd, void* buf, size_t count){
    if(read(fd, buf, count) < 0){
        fprintf(stderr, "Read from pipe failed: %s\n", strerror(errno));
        return -1;
    }
    return 0;
}

static int fn_id_map(unsigned int id, char* type, char* proc_pid_path){

    if(strcmp(type, "uid") != 0 && strcmp(type, "gid") != 0) {
        fprintf(stderr, "fn_id_map wrong argument\n");
        return -1;
    }

    char id_map_path[64];
    char id_map_entry[64];  

    sprintf(id_map_path, "%s/%s_map", proc_pid_path, type);
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
        fprintf(stderr, "sprintf failed: %s\n", strerror(errno));
        return NULL;
    }

    return hostname;
}

static int sethostname_rand(pid_t pid){
    char* hostname = hostname_gen(pid);
    if(hostname == NULL) return -1;

    if(sethostname(hostname, strlen(hostname)) < 0){
        fprintf(stderr, "sethostname failed: %s\n", strerror(errno));
    }

    free(hostname);

    return 0;
}

static char* concat_path(const char* path, const char* root_path){
    char* new_path = malloc((strlen(path) + strlen(root_path) + 1) * sizeof(char));
    strcpy(new_path, root_path);
    strcat(new_path, path);

    return new_path;
}

static int symlink_wrapper(struct symlink_args* args, const char* root_path){
    char* real_link_path = concat_path(args->linkpath, root_path);

    if(symlink(args->target , real_link_path) < 0){
        fprintf(stderr, "symlink %s -> %s failed: %s\n", args->linkpath, args->target, strerror(errno));
        return -1;
    }
    return 0;
}

static void truckerpid(pid_t child_pid, pid_t ppid){
    char* pidc = malloc(20*sizeof(char));
    sprintf(pidc, "%d\n%d\n", child_pid, ppid);
    FILE* pidlist = fopen("pidlist", "a");
    fwrite(pidc, 1, strlen(pidc), pidlist);
    fclose(pidlist);
    free(pidc);
}

static int mount_bind_file(struct mount_args* args, const char* root_path){
    char* real_target = concat_path(args->target, root_path);

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
    char* real_target = concat_path(args->target, root_path);

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

    char* real_target = concat_path(args->target, root_path);

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

static int init_root(const char* container_root){
    if(mount(NULL, "/", NULL, MS_PRIVATE | MS_REC, NULL) < 0){
        fprintf(stderr, "Mount ns propagation change failed: %s\n", strerror(errno));
        return -1;
    }

    return mount_bind_dir(&((struct mount_args){container_root, container_root, NULL, MS_BIND, NULL}), "");
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
    return sethostname_rand(ppid);
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
        if(symlink_wrapper(&(default_symlink[i]), container_root) < 0) return -1;
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

static int config_user_ns(pid_t pid){

    char* str_pid = malloc(12*sizeof(char));
    sprintf(str_pid, "%d", pid);
    char* proc_pid_path = concat_path(str_pid, "/proc/");

    pid_t child_pid = fork();

    if(child_pid < 0){
        fprintf(stderr, "fork failed: %s\n", strerror(errno));
        return -1;
    }

    if(child_pid == 0){
        char* config_id_map_sh = concat_path("/config_id_map.sh", get_trucker_path());

        char* argv[2];
        argv[0] = config_id_map_sh;
        argv[1] = str_pid;
        argv[2] = NULL;

        if(execve(config_id_map_sh, argv, NULL) < 0){
            fprintf(stderr, "execve failed: %s\n", strerror(errno));
            _exit(EXIT_FAILURE);
        }
    }

    int status;
    if(waitpid(child_pid, &status, 0) < 0){
        fprintf(stderr, "waitpid failed: %s\n", strerror(errno));
    }

    fn_id_map(getgid(), "gid", proc_pid_path);

    return 0;
}

static int config_user_ns_wrapper(){
    char read_pipe_buf[size_ok_pipe];

    if(write_wrapper(cfgpipes.container_go[1], ok_pipe, size_ok_pipe) < 0) return -1;
    if(read(cfgpipes.container_back[0], read_pipe_buf, size_ok_pipe) < 0)  return -1;

    return 0;
}

// static int config_user_ns_test(uid_t uid, gid_t gid){
//     FILE* setgroups = fopen("/proc/self/setgroups", "w");
//     fwrite("deny", 1, strlen("deny"), setgroups);
//     fclose(setgroups);

//     fn_id_map(uid, "uid");
//     fn_id_map(gid, "gid");

//     return 0;
// }

static int config_net_ns(){
    pid_t pasta_pid = fork();

    if(pasta_pid < 0){
        fprintf(stderr, "fork failed: %s\n", strerror(errno));
        return -1;
    } 

    else if(pasta_pid == 0){
        char read_pipe_buf[12];
        if(read_wrapper(cfgpipes.getcldpid_pipe[0], read_pipe_buf, sizeof(read_pipe_buf)) < 0)
        _exit(EXIT_FAILURE);

        char* cfg_net_ns_sh = concat_path("/config_net_ns.sh", get_trucker_path());

        char* argv[3];
        argv[0] = cfg_net_ns_sh;
        argv[1] = read_pipe_buf;
        argv[2] = NULL;

        execve(cfg_net_ns_sh, argv, NULL);
        fprintf(stderr, "execve failed: %s\n", strerror(errno));
        _exit(EXIT_FAILURE);
    } 
    
    else {
        int pasta_status;
        if(waitpid(pasta_pid, &pasta_status, 0) < 0){
            fprintf(stderr, "waitpid failed: %s\n", strerror(errno));
            return -1;
        }
    }

    return 0;
}

static int config_net_ns_wrapper(int container_go[], int container_back[]){
    if(write_wrapper(container_go[1], ok_pipe, size_ok_pipe) < 0) return -1;
    if(close_pipe(container_go) < 0) return -1;

    char read_pipe_buf[size_ok_pipe];
    if(read_wrapper(container_back[0], read_pipe_buf, size_ok_pipe) < 0) 
    return -1;
    if(close_pipe(container_back) < 0) 
    return -1;

    return 0;
}

static int fn_child(void* arg){
    struct child_args* child_arg = arg;

    close_pipe(cfgpipes.getcldpid_pipe);
    close_pipe(cfgpipes.ret);

    if(config_user_ns_wrapper() < 0) return -1;
    // if(config_user_ns_test(child_arg->real_uid, child_arg->real_gid) < 0) return -1;
    if(init_root(child_arg->root) < 0) return -1;
    if(config_uts_ns(child_arg->ppid) < 0) return -1;
    if(config_mount_ns(child_arg->root) < 0) return -1;
    if(config_pid_ns(child_arg->root) < 0) return -1;
    if(config_ipc_ns(child_arg->root) < 0) return -1;
    if(config_cgroup_ns(child_arg->root) < 0) return -1;
    if(pivot_root_wrapper(child_arg->root) < 0) return -1;
    if(config_net_ns_wrapper(cfgpipes.container_go, cfgpipes.container_back) < 0) return -1;

    execve(child_arg->prog, child_arg->argv, NULL);
    fprintf(stderr, "execve failed: %s\n", strerror(errno));
    _exit(EXIT_FAILURE);
}

static int deliver(const char* container_root, 
            const char* container_prog, 
            char* container_argv[]/*,
            struct options** opts*/){
    
    int status;
    size_t stacksize = (1024*1024);

    char* stack = malloc_wrapper(stacksize*sizeof(char));
    if(stack == NULL) return 125;

    struct child_args* child_arg = malloc_wrapper(sizeof(struct child_args));
    if(child_arg == NULL) return 125;

    child_arg->ppid = getpid();
    child_arg->real_uid = getuid();
    child_arg->real_gid = getgid();
    child_arg->root = container_root;
    child_arg->prog = container_prog;
    child_arg->argv = container_argv;

    if(unshare(CLONE_NEWUSER | CLONE_NEWTIME) < 0){
        fprintf(stderr, "unshare time ns failed: %s\n", strerror(errno));
        return 125;
    }
    
    pid_t child_pid = clone(&fn_child, stack+stacksize,
        CLONE_NEWUTS |
        CLONE_NEWIPC |
        CLONE_NEWCGROUP |
        CLONE_NEWNS |
        CLONE_NEWPID |
        CLONE_NEWNET |
        SIGCHLD, child_arg);

    if(child_pid < 0){
        fprintf(stderr, "clone failed: %s\n", strerror(errno));
        return 125;
    };

    char* str_child_pid = malloc(12*sizeof(char));
    sprintf(str_child_pid, "%d", child_pid);

    if(write_wrapper(cfgpipes.getcldpid_pipe[1], str_child_pid, strlen(str_child_pid)+1) < 0) 
    return 125;

    if(close_pipe(cfgpipes.getcldpid_pipe) < 0) return 125;
    if(close_pipe(cfgpipes.container_go) < 0) return 125;
    if(close_pipe(cfgpipes.container_back) < 0) return 125;

    free(str_child_pid);

    truckerpid(child_pid, child_arg->ppid);

    if(waitpid(child_pid, &status, 0) < 0) {
        fprintf(stderr, "waitpid failed: %s\n", strerror(errno));
        return 125;
    }

    if(write_wrapper(cfgpipes.ret[1], &status, sizeof(status)) < 0) return 125;

    return 0;
}

int deliver_launcher(const char* container_root, 
            const char* container_prog, 
            char* container_argv[]/*, 
            struct options** opts*/){

    pipe(cfgpipes.container_go);
    pipe(cfgpipes.container_back);
    pipe(cfgpipes.getcldpid_pipe);
    pipe(cfgpipes.ret);
    
    pid_t deliver_pid = fork();

    if(deliver_pid < 0){
        fprintf(stderr, "fork failed: %s\n", strerror(errno));
        return -1;
    }
    
    if(deliver_pid == 0){
        int ret = deliver(container_root, 
                        container_prog, 
                        container_argv/*, 
                        opts,*/);
        
        if(ret == 125) fprintf(stderr, "Delivery error: It's possible that the container correctly launched\n");
        return ret;
    }

    char read_pipe_buf[size_ok_pipe];

    if(read_wrapper(cfgpipes.container_go[0], read_pipe_buf, size_ok_pipe) < 0) return -1;
    if(config_user_ns(deliver_pid) < 0) return -1;
    if(write_wrapper(cfgpipes.container_back[1], ok_pipe, size_ok_pipe) < 0) return -1;

    if(read_wrapper(cfgpipes.container_go[0], read_pipe_buf, size_ok_pipe) < 0) return -1;
    if(config_net_ns() < 0) return -1;
    if(write_wrapper(cfgpipes.container_back[1], ok_pipe, size_ok_pipe) < 0) return -1;
    
    if(close_pipe(cfgpipes.container_go) < 0) return -1;
    if(close_pipe(cfgpipes.container_back) < 0) return -1;

    int container_status;
    if(read_wrapper(cfgpipes.ret[0], &container_status, sizeof(container_status)) < 0){
        fprintf(stderr, "exit code can't be return\n");
    }
    if(close_pipe(cfgpipes.ret) < 0){
        fprintf(stderr, "exit code can't be return\n");
    }

    int deliver_status;
    if(waitpid(deliver_pid, &deliver_status, 0) < 0){
        fprintf(stderr, "waitpid failed: %s\n", strerror(errno));
        return -1;
    }

    return 0;
}