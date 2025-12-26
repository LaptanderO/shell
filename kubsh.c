#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <dirent.h>
#include <pwd.h>
#include <readline/readline.h>
#include <readline/history.h>

#define HISTORY_FILE ".kubsh_history"

// В kubsh.c, после #include и до main()
int start_users_vfs(const char *mount_point);
void stop_users_vfs(void);

void sig_handler(int sig) {
    printf("Configuration reloaded\n");
}

int is_executable(const char *path) {
    return access(path, X_OK) == 0;
}

void create_user(const char *path){
    char *argv[3];
    
}

char *find_in_path(const char *command) {
    char *path_env = getenv("PATH");
    if (path_env == NULL) {
        return NULL;
    }
    
    char *path_copy = strdup(path_env);
    char *dir = strtok(path_copy, ":");
    
    while (dir != NULL) {
       
        char full_path[256];
        snprintf(full_path, sizeof(full_path), "%s/%s", dir, command);
        
        if (is_executable(full_path)) {
            free(path_copy);
            return strdup(full_path);
        }
        
        dir = strtok(NULL, ":");
    }
    
    free(path_copy);
    return NULL;
}

void print_env_variable(char *input) {
    char *var_name = input + 3;
    if (var_name[0] == '$') {
        var_name++;
    }

    if (strcmp(var_name, "HOME") == 0) {
        uid_t uid = getuid();
        struct passwd *pw = getpwuid(uid);
        if (pw && pw->pw_dir) {
            printf("HOME=%s\n", pw->pw_dir);
        } else {
            printf("Cannot determine HOME directory\n");
        }
        return;
    }
    
    char *value = getenv(var_name);
    if (value != NULL) {
        if (strcmp(var_name, "PATH") == 0) {
            printf("PATH directories:\n");
            char *path_copy = strdup(value);
            char *dir = strtok(path_copy, ":");
            int count = 1;
            
            while (dir != NULL) {
                printf("%d. %s\n", count++, dir);
                dir = strtok(NULL, ":");
            }
            free(path_copy);
        } else {
            printf("%s=%s\n", var_name, value);
        }
    } else {
        printf("Environment variable not found: %s\n", var_name);
    }
}

void fork_exec(char *full_path, char **argv) {
    int pid = fork();
    if (pid == 0) {
        execv(full_path, argv);
        perror("execv");
        exit(1);
    } else if (pid > 0) {
        int status;
        waitpid(pid, &status, 0);
    } else {
        perror("fork");
    }
}

void echo(char* input){
    printf("%s\n", input + 6);
}

void disk_info_command(char *input) {
    char *device = input + 3;
    
    if (strncmp(device, "/dev/", 5) == 0) {
        device += 5;
    }
    
    FILE *file = fopen("/proc/partitions", "r");
    if (file == NULL) {
        printf("Cannot read disk info\n");
        return;
    }
    
    printf("Partitions for %s:\n", device);
    
    char line[256];
    fgets(line, sizeof(line), file);
    fgets(line, sizeof(line), file);
    
    int found = 0;
    while (fgets(line, sizeof(line), file) != NULL) {
        char *pos = strrchr(line, ' ');
        if (pos != NULL) {
            pos++;
            if (strncmp(pos, device, strlen(device)) == 0) {
                found = 1;
                
                int major, minor, blocks;
                char name[32];
                sscanf(line, "%d %d %d %s", &major, &minor, &blocks, name);
                printf("%s: %d blocks (~%d MB)\n", name, blocks, blocks / 2048);
            }
        }
    }
    
    if (!found) {
        printf("No partitions found for device %s\n", device);
    }
    
    fclose(file);
}

void setup_mount_point(char *mount_point, size_t size) {
    char *username = getenv("SUDO_USER");
    struct passwd *pw = NULL;
    
    if (username) {
        pw = getpwnam(username);
    }
    
    if (!pw) {
        pw = getpwuid(getuid());
    }
    
    if (pw && pw->pw_dir) {
        snprintf(mount_point, size, "%s/users", pw->pw_dir);
    } else {
        const char *home = getenv("HOME");
        if (!home) home = "/tmp";
        snprintf(mount_point, size, "%s/users", home);
    }
}

int main() {
    rl_clear_signals();
    signal(SIGHUP, sig_handler);
    
    read_history(HISTORY_FILE);

    char *input;

    char mount_point[256];
    setup_mount_point(mount_point, sizeof(mount_point));
    printf("VFS будет смонтирована в: %s\n", mount_point);
    
    printf("\n=== Preparing mount point ===\n");
    
    struct stat st;
    if (stat(mount_point, &st) == 0) {
        printf("Directory exists\n");
        
        if (st.st_uid != getuid()) {
            printf("Warning: Directory owned by uid=%d, not current user uid=%d\n", 
                   st.st_uid, getuid());
            printf("Trying to fix permissions...\n");
            
            DIR *dir = opendir(mount_point);
            if (dir) {
                int empty = 1;
                struct dirent *entry;
                while ((entry = readdir(dir)) != NULL) {
                    if (strcmp(entry->d_name, ".") != 0 && 
                        strcmp(entry->d_name, "..") != 0) {
                        empty = 0;
                        break;
                    }
                }
                closedir(dir);
                
                if (empty) {
                    if (rmdir(mount_point) == 0) {
                        printf("✓ Removed empty directory\n");
                    } else {
                        printf("✗ Cannot remove, need sudo or manual cleanup\n");
                        printf("Please run: sudo rmdir %s\n", mount_point);
                    }
                }
            }
        }
    } else {
        printf("Creating directory...\n");
        if (mkdir(mount_point, 0755) != 0) {
            perror("mkdir");
            printf("Falling back to /tmp\n");
            snprintf(mount_point, sizeof(mount_point), "/tmp/kubsh_users_%d", getuid());
            mkdir(mount_point, 0755);
        }
    }
    
    if (stat(mount_point, &st) != 0) {
        printf("Creating mount directory...\n");
        if (mkdir(mount_point, 0755) != 0) {
            perror("Failed to create directory");
            return 1;
        }
    }
    
    printf("Setting permissions...\n");
    if (chmod(mount_point, 0755) != 0) {
        perror("chmod");
    }
    
    if (chown(mount_point, getuid(), getgid()) != 0) {
        printf("Note: Could not change owner (need sudo)\n");
        printf("Directory owned by: uid=%d, gid=%d\n", st.st_uid, st.st_gid);
    }
    
    stat(mount_point, &st);
    printf("Final permissions: uid=%d, mode=%o\n", st.st_uid, st.st_mode);
    
    printf("\n=== Mounting VFS ===\n");
    if (start_users_vfs(mount_point) == 0) {
        printf("✓ Users VFS mounted at %s\n", mount_point);
    } else {
        printf("✗ Failed to mount VFS\n");
        printf("If permission denied, try:\n");
        printf("  sudo chown -R %d:%d %s\n", getuid(), getgid(), mount_point);
        printf("Or run kubsh with sudo once to fix permissions\n");
    }
    
    sleep(1);
    
    printf("\n=== Shell ready ===\n");

    while (1) {
        input = readline("#> ");
        
        if (input == NULL) {
            printf("exit\n");
            break;
        }
        
        if (strlen(input) == 0) {
            free(input);
            continue;
        }
        
        add_history(input);
        
        if (strcmp(input, "\\q") == 0) {
            free(input);
            break;
        }
        else if(strncmp(input, "echo ", 6) == 0){
            echo(input);
        }
        else if(strncmp(input, "\\e ", 3) == 0){
            print_env_variable(input);
        }
        else if(strncmp(input, "\\l ", 3) == 0){
            disk_info_command(input);
        }
        else{
            char *argv[10];
            int argc = 0;
        
            char *token = strtok(input, " ");
            while (token != NULL && argc < 9) {
                argv[argc++] = token;
                token = strtok(NULL, " ");
            }
            argv[argc] = NULL;
        
            if (argc > 0) {
                char *full_path = find_in_path(argv[0]);
                if (full_path != NULL) {
                    fork_exec(full_path, argv);
                    free(full_path);
                } else {
                    printf("Command not found: %s\n", argv[0]);
                }
            }
        }   
        free(input);
     }

    
    write_history(HISTORY_FILE);
    stop_users_vfs();
    
    return 0;
}
