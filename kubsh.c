#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <sys/wait.h>
#include <readline/readline.h>
#include <readline/history.h>

#define HISTORY_FILE ".kubsh_history"

void sig_handler(int sig) {
    printf("Configuration reloaded\n");
}

int is_executable(const char *path) {
    return access(path, X_OK) == 0;
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

// Запуск команды
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

int main() {
    rl_clear_signals();
    signal(SIGHUP, sig_handler);
    
    read_history(HISTORY_FILE);

    char *input;

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
        
        free(input);
    }
    
    write_history(HISTORY_FILE);
    
    return 0;
}
