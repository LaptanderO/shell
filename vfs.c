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
#include <grp.h>

static int vfs_pid = -1;
static struct passwd *users_list[MAX_USERS];
static int users_count = 0;

void free_users_list(void);
int get_users_list(void);
static struct passwd *find_user(const char *name);
static int users_readdir(const char *path, void *buf, fuse_fill_dir_t filler,
                        off_t offset, struct fuse_file_info *fi,
                        enum fuse_readdir_flags flags);
static int users_open(const char *path, struct fuse_file_info *fi);
static int users_read(const char *path, char *buf, size_t size,
                     off_t offset, struct fuse_file_info *fi);
static int users_getattr(const char *path, struct stat *stbuf,
                        struct fuse_file_info *fi);
static int users_mkdir(const char *path, mode_t mode);
static int users_rmdir(const char *path);

void free_users_list(void) {
    for (int i = 0; i < users_count; i++) {
        if (users_list[i]) {
            free(users_list[i]->pw_name);
            free(users_list[i]->pw_dir);
            free(users_list[i]->pw_shell);
            free(users_list[i]);
        }
    }
    users_count = 0;
}

int get_users_list(void) {
    free_users_list();
    
    setpwent();
    
    struct passwd *pwd;
    users_count = 0;
    
    while ((pwd = getpwent()) != NULL && users_count < MAX_USERS) {
        if (pwd->pw_shell == NULL) continue;
        
        size_t shell_len = strlen(pwd->pw_shell);
        if (shell_len < 2) continue;
        
        if (strcmp(pwd->pw_shell + shell_len - 2, "sh") != 0) {
            continue;
        }
        
        struct passwd *copy = malloc(sizeof(struct passwd));
        if (!copy) {
            free_users_list();
            return -1;
        }
        
        copy->pw_name = strdup(pwd->pw_name);
        copy->pw_uid = pwd->pw_uid;
        copy->pw_gid = pwd->pw_gid;
        copy->pw_dir = pwd->pw_dir ? strdup(pwd->pw_dir) : strdup("");
        copy->pw_shell = strdup(pwd->pw_shell);
        
        users_list[users_count++] = copy;
    }
    
    endpwent();
    
    if (users_count == 0) {
        struct passwd *root = malloc(sizeof(struct passwd));
        root->pw_name = strdup("root");
        root->pw_uid = 0;
        root->pw_gid = 0;
        root->pw_dir = strdup("/root");
        root->pw_shell = strdup("/bin/bash");
        users_list[0] = root;
        users_count = 1;
    }
    
    return users_count;
}

static struct passwd *find_user(const char *name) {
    for (int i = 0; i < users_count; i++) {
        if (strcmp(users_list[i]->pw_name, name) == 0) {
            return users_list[i];
        }
    }
    return NULL;
}

static int users_readdir(
    const char *path, 
    void *buf, 
    fuse_fill_dir_t filler,
    off_t offset,
    struct fuse_file_info *fi,
    enum fuse_readdir_flags flags
) {
    filler(buf, ".", NULL, 0, 0);
    filler(buf, "..", NULL, 0, 0);
    
    if (strcmp(path, "/") == 0) {
        for (int i = 0; i < users_count; i++) {
            filler(buf, users_list[i]->pw_name, NULL, 0, 0);
        }
    } else {
        char *user_name = strrchr(path, '/');
        if (user_name) {
            user_name++;
            struct passwd *user = find_user(user_name);
            if (user) {
                filler(buf, "id", NULL, 0, 0);
                filler(buf, "home", NULL, 0, 0);
                filler(buf, "shell", NULL, 0, 0);
            }
        }
    }
    
    return 0;
}

static int users_open(const char *path, struct fuse_file_info *fi) {
    struct stat st;
    if (users_getattr(path, &st, fi) != 0) {
        return -ENOENT;
    }
    
    if (S_ISDIR(st.st_mode)) {
        return -EISDIR;
    }
    
    return 0;
}

static int users_read(
    const char *path, 
    char *buf, 
    size_t size,
    off_t offset,
    struct fuse_file_info *fi
) {
    (void) fi;
    
    char path_copy[256];
    strncpy(path_copy, path, sizeof(path_copy) - 1);
    path_copy[sizeof(path_copy) - 1] = '\0';
    
    char *name = path_copy;
    if (name[0] == '/') {
        name++;
    }
    
    char *slash = strchr(name, '/');
    if (!slash) {
        return -ENOENT;
    }
    
    *slash = '\0';
    char *username = name;
    char *filename = slash + 1;
    
    struct passwd *user = find_user(username);
    if (!user) {
        return -ENOENT;
    }
    
    char content[256];
    content[0] = '\0';
    
    if (strcmp(filename, "id") == 0) {
        snprintf(content, sizeof(content), "%d", user->pw_uid);
    } else if (strcmp(filename, "home") == 0) {
        snprintf(content, sizeof(content), "%s", user->pw_dir);
    } else if (strcmp(filename, "shell") == 0) {
        snprintf(content, sizeof(content), "%s", user->pw_shell);
    } else {
        return -ENOENT;
    }
    
    size_t content_len = strlen(content);
    
    if (offset >= content_len) {
        return 0; 
    }
    
    if (offset + size > content_len) {
        size = content_len - offset;
    }
    
    memcpy(buf, content + offset, size);
    return size;
}

static int users_getattr(const char *path, struct stat *stbuf,
                         struct fuse_file_info *fi) {
    (void) fi;
    
    memset(stbuf, 0, sizeof(struct stat));
    
    if (strcmp(path, "/") == 0) {
        stbuf->st_mode = S_IFDIR | 0755;
        stbuf->st_nlink = 2;
        stbuf->st_uid = getuid();
        stbuf->st_gid = getgid();
        return 0;
    }
    
    char path_copy[256];
    strncpy(path_copy, path, sizeof(path_copy) - 1);
    path_copy[sizeof(path_copy) - 1] = '\0';
    
    char *name = path_copy;
    if (name[0] == '/') {
        name++;
    }
    
    char *slash = strchr(name, '/');
    
    if (slash == NULL) {
        struct passwd *user = find_user(name);
        if (user) {
            stbuf->st_mode = S_IFDIR | 0755;
            stbuf->st_nlink = 2;
            stbuf->st_uid = user->pw_uid;
            stbuf->st_gid = user->pw_gid;
            return 0;
        }
    } else {
        *slash = '\0';
        char *username = name;
        char *filename = slash + 1;
        
        struct passwd *user = find_user(username);
        if (user) {
            if (strcmp(filename, "id") == 0 || 
                strcmp(filename, "home") == 0 || 
                strcmp(filename, "shell") == 0) {
                
                stbuf->st_mode = S_IFREG | 0644;
                stbuf->st_nlink = 1;
                stbuf->st_uid = user->pw_uid;
                stbuf->st_gid = user->pw_gid;
                
                if (strcmp(filename, "id") == 0) {
                    stbuf->st_size = snprintf(NULL, 0, "%d", user->pw_uid);
                } else if (strcmp(filename, "home") == 0) {
                    stbuf->st_size = strlen(user->pw_dir);
                } else if (strcmp(filename, "shell") == 0) {
                    stbuf->st_size = strlen(user->pw_shell);
                }
                return 0;
            }
        }
    }
    return -ENOENT;
}

static int users_mkdir(const char *path, mode_t mode) {
    char *user_name = strrchr(path, '/');
    if (!user_name) return -EINVAL;
    user_name++;
    
    if (find_user(user_name)) {
        return -EEXIST;
    }
    
    if (getuid() == 0) {
        if (users_count < MAX_USERS) {
            struct passwd *new_user = malloc(sizeof(struct passwd));
            if (!new_user) return -ENOMEM;
            
            new_user->pw_name = strdup(user_name);
            new_user->pw_uid = 1000 + users_count;
            new_user->pw_gid = 1000 + users_count;
            new_user->pw_dir = malloc(256);
            snprintf(new_user->pw_dir, 256, "/home/%s", user_name);
            new_user->pw_shell = strdup("/bin/bash");
            
            users_list[users_count++] = new_user;
            
            FILE *passwd = fopen("/etc/passwd", "a");
            if (passwd) {
                fprintf(passwd, "%s:x:%d:%d::/home/%s:/bin/bash",
                        user_name, new_user->pw_uid, new_user->pw_gid, user_name);
                fclose(passwd);
            }
            
            return 0;
        }
        return -ENOSPC;
    }
    
    char command[256];
    snprintf(command, sizeof(command), "sudo adduser --disabled-password --gecos '' %s", user_name);
    
    int ret = system(command);
    if (ret != 0) {
        return -EACCES;
    }
    
    get_users_list();
    return 0;
}

static int users_rmdir(const char *path) {
    char *user_name = strrchr(path, '/');
    if (!user_name) return -EINVAL;
    user_name++;
    
    struct passwd *user = find_user(user_name);
    if (!user) {
        return -ENOENT;
    }
    
    if (user->pw_uid == getuid()) {
        return -EPERM;
    }
    
    char command[256];
    snprintf(command, sizeof(command), "sudo userdel -r %s", user_name);
    
    int ret = system(command);
    if (ret != 0) {
        return -EACCES;
    }
    
    get_users_list();
    
    return 0;
}

static struct fuse_operations users_oper = {
    .getattr = users_getattr,
    .open = users_open,
    .read = users_read,
    .readdir = users_readdir,
    .mkdir = users_mkdir,
    .rmdir = users_rmdir,
};

int start_users_vfs(const char *mount_point) {
    struct stat st;
    if (stat(mount_point, &st) != 0) {
        if (mkdir(mount_point, 0755) != 0) {
            perror("mkdir");
            return -1;
        }
    }

    if (chown(mount_point, getuid(), getgid()) != 0) {
        perror("chown mount_point");
    }

    int pid = fork();
    if (pid == 0) {
        if (get_users_list() <= 0) {
            fprintf(stderr, "Не удалось получить список пользователей\n");
            exit(1);
        }
        
        char *fuse_argv[] = {
            "users_vfs",
            "-f",
            "-s",
            (char*)mount_point,
            NULL
        };
        
        int ret = fuse_main(4, fuse_argv, &users_oper, NULL);
        
        free_users_list();
        exit(ret);
    } else if (pid > 0) { 
        vfs_pid = pid;
        sleep(1);
        return 0;
    } else {
        perror("fork");
        return -1;
    }
}

void stop_users_vfs() {
    if (vfs_pid != -1) {
        kill(vfs_pid, SIGTERM);
        waitpid(vfs_pid, NULL, 0);
        vfs_pid = -1;
    }
}
