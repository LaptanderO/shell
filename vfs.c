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

// Прототипы
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

// Освобождение списка пользователей
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

// Получение списка пользователей
int get_users_list(void) {
    setpwent();
    
    struct passwd *pwd;
    users_count = 0;
    
    while ((pwd = getpwent()) != NULL && users_count < MAX_USERS) {
        struct passwd *copy = malloc(sizeof(struct passwd));
        if (!copy) {
            free_users_list();
            return -1;
        }
        
        copy->pw_name = strdup(pwd->pw_name);
        copy->pw_uid = pwd->pw_uid;
        copy->pw_gid = pwd->pw_gid;
        copy->pw_dir = strdup(pwd->pw_dir);
        copy->pw_shell = strdup(pwd->pw_shell);
        
        users_list[users_count++] = copy;
    }
    
    endpwent();
    return users_count;
}

// Поиск пользователя по имени
static struct passwd *find_user(const char *name) {
    for (int i = 0; i < users_count; i++) {
        if (strcmp(users_list[i]->pw_name, name) == 0) {
            return users_list[i];
        }
    }
    return NULL;
}

// Обработка чтения директории
static int users_readdir(
    const char *path, 
    void *buf, 
    fuse_fill_dir_t filler,
    off_t offset,
    struct fuse_file_info *fi,
    enum fuse_readdir_flags flags
) {
    (void) offset;
    (void) fi;
    (void) flags;
    
    filler(buf, ".", NULL, 0, 0);
    filler(buf, "..", NULL, 0, 0);
    
    if (strcmp(path, "/") == 0) {
        // Корневая директория - список пользователей
        for (int i = 0; i < users_count; i++) {
            filler(buf, users_list[i]->pw_name, NULL, 0, 0);
        }
    } else {
        // Директория пользователя
        char *user_name = strrchr(path, '/');
        if (user_name) {
            user_name++; // Пропускаем '/'
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

// Открытие файла
static int users_open(const char *path, struct fuse_file_info *fi) {
    // Проверяем существование файла
    struct stat st;
    if (users_getattr(path, &st, fi) != 0) {
        return -ENOENT;
    }
    
    if (S_ISDIR(st.st_mode)) {
        return -EISDIR;
    }
    
    return 0;
}

// Чтение из файла
static int users_read(
    const char *path, 
    char *buf, 
    size_t size,
    off_t offset,
    struct fuse_file_info *fi
) {
    (void) fi;
    
    // Определяем пользователя
    char *slash = strrchr(path, '/');
    if (!slash) return -ENOENT;
    
    char *file_name = slash + 1;
    *slash = '\0'; // Теперь path содержит путь к директории пользователя
    
    char *user_name = strrchr(path, '/');
    if (!user_name) {
        *slash = '/'; // Восстанавливаем
        return -ENOENT;
    }
    user_name++; // Пропускаем '/'
    
    *slash = '/'; // Восстанавливаем исходный путь
    
    struct passwd *user = find_user(user_name);
    if (!user) return -ENOENT;
    
    // Определяем содержимое файла
    const char *content = NULL;
    char content_buffer[256];
    
    if (strcmp(file_name, "id") == 0) {
        snprintf(content_buffer, sizeof(content_buffer), "%d\n", user->pw_uid);
        content = content_buffer;
    } else if (strcmp(file_name, "home") == 0) {
        snprintf(content_buffer, sizeof(content_buffer), "%s\n", user->pw_dir);
        content = content_buffer;
    } else if (strcmp(file_name, "shell") == 0) {
        snprintf(content_buffer, sizeof(content_buffer), "%s\n", user->pw_shell);
        content = content_buffer;
    } else {
        return -ENOENT;
    }
    
    size_t content_len = strlen(content);
    
    if (offset >= content_len) {
        return 0; // Конец файла
    }
    
    if (offset + size > content_len) {
        size = content_len - offset;
    }
    
    memcpy(buf, content + offset, size);
    return size;
}

// Получение атрибутов файла/директории
static int users_getattr(const char *path, struct stat *stbuf,
                         struct fuse_file_info *fi) {
    (void) fi;
    
    memset(stbuf, 0, sizeof(struct stat));
    
    if (strcmp(path, "/") == 0) {
        // Корневая директория
        stbuf->st_mode = S_IFDIR | 0755;
        stbuf->st_nlink = 2;
        stbuf->st_uid = getuid();
        stbuf->st_gid = getgid();
        return 0;
    }
    
    char *path_copy = strdup(path);
    char *slash = strrchr(path_copy, '/');
    
    if (slash && slash != path_copy) {
        *slash = '\0'; // Получаем путь к родительской директории
        
        if (strcmp(path_copy, "/") == 0) {
            // Это пользователь в корневой директории
            struct passwd *user = find_user(slash + 1);
            if (user) {
                stbuf->st_mode = S_IFDIR | 0755;
                stbuf->st_nlink = 2;
                stbuf->st_uid = user->pw_uid;
                stbuf->st_gid = user->pw_gid;
                free(path_copy);
                return 0;
            }
        } else {
            // Это файл внутри директории пользователя
            char *user_name = strrchr(path_copy, '/');
            if (user_name) {
                user_name++; // Пропускаем '/'
                struct passwd *user = find_user(user_name);
                if (user) {
                    char *file_name = slash + 1;
                    if (strcmp(file_name, "id") == 0 || 
                        strcmp(file_name, "home") == 0 || 
                        strcmp(file_name, "shell") == 0) {
                        stbuf->st_mode = S_IFREG | 0644;
                        stbuf->st_nlink = 1;
                        stbuf->st_uid = user->pw_uid;
                        stbuf->st_gid = user->pw_gid;
                        
                        // Устанавливаем размер файла
                        if (strcmp(file_name, "id") == 0) {
                            stbuf->st_size = snprintf(NULL, 0, "%d\n", user->pw_uid);
                        } else if (strcmp(file_name, "home") == 0) {
                            stbuf->st_size = strlen(user->pw_dir) + 1;
                        } else if (strcmp(file_name, "shell") == 0) {
                            stbuf->st_size = strlen(user->pw_shell) + 1;
                        }
                        free(path_copy);
                        return 0;
                    }
                }
            }
        }
    }
    
    free(path_copy);
    return -ENOENT;
}

// Создание директории (добавление пользователя)
static int users_mkdir(const char *path, mode_t mode) {
    char *user_name = strrchr(path, '/');
    if (!user_name) return -EINVAL;
    user_name++; // Пропускаем '/'
    
    // Проверяем, не существует ли уже такой пользователь
    if (find_user(user_name)) {
        return -EEXIST;
    }
    
    // Запускаем adduser
    char command[256];
    snprintf(command, sizeof(command), "sudo adduser --disabled-password --gecos '' %s", user_name);
    
    int ret = system(command);
    if (ret != 0) {
        return -EACCES;
    }
    
    // Обновляем список пользователей
    get_users_list();
    
    return 0;
}

// Удаление директории (удаление пользователя)
static int users_rmdir(const char *path) {
    char *user_name = strrchr(path, '/');
    if (!user_name) return -EINVAL;
    user_name++; // Пропускаем '/'
    
    struct passwd *user = find_user(user_name);
    if (!user) {
        return -ENOENT;
    }
    
    // Нельзя удалить текущего пользователя
    if (user->pw_uid == getuid()) {
        return -EPERM;
    }
    
    // Запускаем userdel
    char command[256];
    snprintf(command, sizeof(command), "sudo userdel -r %s", user_name);
    
    int ret = system(command);
    if (ret != 0) {
        return -EACCES;
    }
    
    // Обновляем список пользователей
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
    // Создаем директорию если не существует
    struct stat st;
    if (stat(mount_point, &st) != 0) {
        if (mkdir(mount_point, 0755) != 0) {
            perror("mkdir");
            return -1;
        }
    }
    
    int pid = fork();    
    if (pid == 0) {
        // Дочерний процесс
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
    } else if (pid > 0) { 
        // Родительский процесс
        vfs_pid = pid;
        printf("VFS запущена в процессе %d, монтирована в %s\n", pid, mount_point);
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
        printf("VFS остановлена\n");
    }
}
