#define FUSE_USE_VERSION 31
#define MAX_USERS 1000
#include <fuse3/fuse.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <dirent.h>
#include <sys/types.h>
#include <pwd.h>
#include <errno.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <signal.h>

static int vfs_pid = -1;


int get_users_list() {
}


void free_users_list() {
}


static int users_readdir(
    const char *path, 
    void *buf, 
    fuse_fill_dir_t filler,
    off_t offset,
    struct fuse_file_info *fi,
    enum fuse_readdir_flags flags
) {
}


static int users_open(const char *path, struct fuse_file_info *fi) {
}


static int users_read(
    const char *path, 
    char *buf, 
    size_t size,
    off_t offset,
    struct fuse_file_info *fi
) {
}


static int users_getattr(const char *path, struct stat *stbuf,
                         struct fuse_file_info *fi) {
}


static int users_mkdir(const char *path, mode_t mode) {
    int ret;
    struct stat st;
    uid_t current_uid = getuid();
    gid_t current_gid = getgid();

    if(stat(path, &st) == 0){
        errno = EEXIST;
        return -1;
    }

    char *parent_dir = strdup(path);
    if(!parent_dir){
        errno = ENOMEM;
        return -1;
    }

    char *last_slash = strrchr(parent_dir, '/');
    if(last_slash){
        *last_slash = '\0';
        
        if(strlen(parent_dir) > 0) {
            if(stat(parent_dir, &st) != 0){
                free(parent_dir);
                errno = ENOENT;
                return -1;
            }
        }
    }

    free(parent_dir);

    return 0;
}


static struct fuse_operations users_oper = {
    .getattr = users_getattr,
    .open = users_open,
    .read = users_read,
    .readdir = users_readdir,
    .mkdir = users_mkdir,
};


int start_users_vfs(const char *mount_point) {
    int pid = fork();    
    if (pid == 0) {
        char *fuse_argv[] = {
            "users_vfs",    // имя программы
            "-f",           // foreground mode
            "-s",           // single-threaded
            (char*)mount_point, // точка монтирования
            NULL
        };
        
        if (get_users_list() <= 0) {
            fprintf(stderr, "Не удалось получить список пользователей\n");
            exit(1);
        }
        
        int ret = fuse_main(4, fuse_argv, &users_oper, NULL);
        
        free_users_list();
        exit(ret);
    } else { // Родительский процесс
        vfs_pid = pid;
        printf("VFS запущена в процессе %d, монтирована в %s\n", pid, mount_point);
        return 0;
    }
}

void stop_users_vfs() {
    if (vfs_pid != -1) {
        kill(vfs_pid, SIGTERM);
        waitpid(vfs_pid, NULL, 0);
        vfs_pid = -1;
        printf("VFS остановлена\n");
    }
}
