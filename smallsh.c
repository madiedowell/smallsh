// Author: Madison Dowell
// CS 374
// 05-20-2024

#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#define MAX_COMMAND_LINE_LENGTH 2048

// Structure to represent background process IDs
struct BackgroundPID {
    pid_t background_pid;
    struct BackgroundPID* next;
};

// Global variable to store the heaf of the background process linked list
struct BackgroundPID* head = NULL;

// Flag to control background process execution
int allow_background = 1;


// Signal handler for SIGSTP
void handle_SIGSTP(int signo) {
    if (allow_background) {
        const char* foreground_message = "\nEntering foreground-only mode (& is now ignored)\n";
        write(STDOUT_FILENO, foreground_message, strlen(foreground_message));
        allow_background = 0;
    } else {
        const char* foreground_message = "\nExiting foreground-only mode\n";
        write(STDOUT_FILENO, foreground_message, strlen(foreground_message));
        allow_background = 1;
    }
}


// Signal handler for SIGINT
void handle_SIGINT(int signo) {
    // Do nothing in the parent process
}


// Function to expand $$ variable
void expand_var(char command[MAX_COMMAND_LINE_LENGTH]) {
    char *pid_str = strstr(command, "$$");
    if (pid_str != NULL) {
        // Replace $$ with the process ID
        pid_t smallsh_pid = getpid();
        sprintf(pid_str, "%d", smallsh_pid);
    }
}


// Function for built in command: cd
int cd_func (char command[MAX_COMMAND_LINE_LENGTH]) {
    char *path = command + 3; // Skip "cd "
    if (path[0] == '\0') {
        // Change to HOME directory if no path specified
        char* home_dir = getenv("HOME");
        chdir(home_dir);
    } else {
        char cwd[1024]; // Buffer to store the current working directory path
        // Change to the specified directory
        if (chdir(path) != 0) {
            perror("cd");
        }
    }
    return 0;
}


// Function to add a background process ID to the linked list
void add_background_pid(pid_t background_pid) {

    //Allocate memory for a new node to store the background PID
    struct BackgroundPID* new_pid = malloc(sizeof(struct BackgroundPID));

    new_pid->background_pid = background_pid;
    new_pid->next = head;
    head = new_pid;
}


// Function to remove a background process ID from the linked list
void remove_background_pid(pid_t background_pid) {

    // Pointer to traverse the linked list
    struct BackgroundPID** current = &head;

    // Loop through the linked list until the target PID is found
    while (*current) {
        if ((*current)->background_pid == background_pid) {
            struct BackgroundPID* temp = *current;
            *current = (*current)->next;
            free(temp);
            return;
        }
        current = &(*current)->next;
    }
}


// Function to hndle completed background processes
void handle_background_processes() {

    // Pointer to the current node in the linked list of background processes
    struct BackgroundPID** current = &head;

    // Loop through the linked list of background processes
    while(*current) {
        int status;
        // Check if the background process has completed
        pid_t value = waitpid((*current)->background_pid, &status, WNOHANG);

        // If the background process has completed
        if (value > 0) {
            if (WIFEXITED(status)) {
                printf("background pid %d is done: exit value %d\n", value, WEXITSTATUS(status));
            }
            else if (WIFSIGNALED(status)) {
                printf("background pid %d terminated by signal %d\n", value, WTERMSIG(status));
            }
            fflush(stdout);

            // Remove the completed background process from the linked list
            struct BackgroundPID* temp = *current;
            *current = (*current)->next;
            free(temp);
        } 
        else {
            current = &(*current)->next;
        }
    }
}


// Function to create a child process and execute the command
void create_child(char command[MAX_COMMAND_LINE_LENGTH], int in_background) {
    char *args[MAX_COMMAND_LINE_LENGTH];
    char *token = strtok(command, " "); // Tokenize the command string
    int i = 0;

    // Split the command string into arguements and store them in the args array
    while (token != NULL && i < MAX_COMMAND_LINE_LENGTH - 1) {
        args[i] = token;
        token = strtok(NULL, " ");
        i++;
    }

    args[i] = NULL; // Mark the end of the array

    // File descriptors
    int fd_in = -1;
    int fd_out = -1;

    // Handle input and output redirection
    for (int j = 0; args[j] != NULL; j++) {
        if (strcmp(args[j], "<") == 0) {
            char *input_file = args[j + 1];
            if (input_file != NULL) {
                fd_in = open(input_file, O_RDONLY);
                if (fd_in == -1) {
                    printf("Error: cannot open %s for input\n", input_file);
                    exit(1);
                }
                // Redirect the stdin to the input file
                dup2(fd_in, STDIN_FILENO);
                close(fd_in);
                // Shift remaining arguments left
                for (int k = j; args[k] != NULL; k++) {
                    args[k] = args[k + 2];
                }
                j--; // Stay on the same index to check the new args[j]
            }
        } else if (strcmp(args[j], ">") == 0) {
            char *output_file = args[j + 1];
            if (output_file != NULL) {
                fd_out = open(output_file, O_WRONLY | O_CREAT | O_TRUNC, 0666);
                if (fd_out == -1) {
                    printf("Error: cannot open %s for output\n", output_file);
                    exit(1);
                }
                // Redirect stdout to the output file
                dup2(fd_out, STDOUT_FILENO);
                close(fd_out);
                // Shift remaining arguments left
                for (int k = j; args[k] != NULL; k++) {
                    args[k] = args[k + 2];
                }
                j--; // Stay on the same index to check the new args[j]
            }
        }
    }

    // If command is running in background
    if (in_background) {
        // If input redirection is not specified, redirect stdin to /dev/null
        if (fd_in == -1) {
            fd_in = open("/dev/null", O_RDONLY);
            dup2(fd_in, STDIN_FILENO);
            close(fd_in);
        }
        // If output redirection is not specified, redirect stdout to /dev/null
        if (fd_out == -1) {
            fd_out = open("/dev/null", O_WRONLY);
            dup2(fd_out, STDOUT_FILENO);
            close(fd_out);
        }
    }

    // Execute the command
    execvp(args[0], args);

    // If execvp returns, an error occurred
    perror("execvp");
    exit(1);
}


// Main function
int main () {
    char command[MAX_COMMAND_LINE_LENGTH]; // Buffer to store the command entered by the user
    int current_status = 0; // Variable to store the status of the last foreground process

    // Set up signal handlers for SIGINT and SIGSTP
    struct sigaction SIGINT_action = {0};
    struct sigaction SIGSTP_action = {0};

    // Configure signal handlers
    SIGINT_action.sa_handler = handle_SIGINT;
    sigfillset(&SIGINT_action.sa_mask);
    SIGINT_action.sa_flags = 0;

    SIGSTP_action.sa_handler = handle_SIGSTP;
    sigfillset(&SIGSTP_action.sa_mask);
    SIGSTP_action.sa_flags = 0;

    // Register signal handlers
    sigaction(SIGINT, &SIGINT_action, NULL);
    sigaction(SIGTSTP, &SIGSTP_action, NULL);

    // Main shell loop
    while (1) {
        handle_background_processes();

        printf(": "); 
        fflush(stdout); 

        // Read command
        if (fgets(command, MAX_COMMAND_LINE_LENGTH, stdin) == NULL) {
            break;
        }

        command[strcspn(command, "\n")] = '\0';

        // Ignore command if its empty or a comment
        if (command[0] == '\0' || command[0] == '#') {
            continue;
        }

        // Expand $$ variable only once
        expand_var(command);

        // Remove trailing whitespace
        size_t len = strlen(command);
        while (len > 0 && (command[len - 1] == ' ' || command[len - 1] == '\t')) {
            command[--len] = '\0';
        }

        // Set background flag based on command
        int in_background = 0;
        if (len > 0 && command[len - 1] == '&') {
            if (allow_background) {
                in_background = 1;
                command[--len] = '\0';
            }else {
                command[--len] = '\0';
            } 
        }

        //Built in commands
        if (strcmp(command, "exit") == 0) {
            // Exit the shell
            break;
        } else if (strncmp(command, "cd", 2) == 0) {
            // Change directory
            cd_func(command);
        } else if (strncmp(command, "status", 6) == 0) {
            // Print status of last foreground process
            printf("exit value %d\n", current_status);
        }else {
            //Non built in commands
            pid_t childPid = fork();

            if (childPid == -1){
                perror("fork");
            }else if (childPid == 0){ // Child Process
                // Restore default behavior for SIGINT and ignore SIGSTP
                SIGINT_action.sa_handler = SIG_DFL;
                sigaction(SIGINT, &SIGINT_action, NULL);
                SIGSTP_action.sa_handler = SIG_IGN;
                sigaction(SIGTSTP, &SIGSTP_action, NULL);

                // Execute the command
                create_child(command, in_background);
            }else { // Parent Process

                if (in_background) {
                    printf("background pid is %d\n", childPid);
                    fflush(stdout);
                    add_background_pid(childPid);
                }else {
                    // Wait for foreground process to complete 
                    int status;
                    waitpid(childPid, &status, 0);

                    if (WIFEXITED(status)){
                        current_status = WEXITSTATUS(status);
                    }else if (WIFSIGNALED(status)) {
                        int terminate_signal = WTERMSIG(status);
                        printf("terminated by signal %d\n", terminate_signal);
                        current_status = 1;
                    }
                }
            }
        } 
    }

    // Free memory allocated for the linked list
    while (head) {
        struct BackgroundPID* temp = head;
        head = head->next;
        free(temp);
    }
    return 0;
}