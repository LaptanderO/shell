#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <readline/readline.h>
#include <readline/history.h>

#define HISTORY_FILE ".kubsh_history"

void debug(char* input){
	printf("debug: %s \n", input);
}

int main() {
    char *input;
    
    read_history(HISTORY_FILE);
    
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
	else if(strncmp(input, "debug ", 6) == 0){
		debug(input + 6);
	}
	else{
		printf("Command not found \n");
	}
        free(input);
    }
    
    // Сохранение истории перед выходом
    write_history(HISTORY_FILE);
    
    return 0;
}
