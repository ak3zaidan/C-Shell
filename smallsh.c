#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <dirent.h>
#include <limits.h>
#include <fcntl.h>
#include <unistd.h>
#include <signal.h>
#include <sys/wait.h>

int last_exit_status = 0;
int last_termination = 0;
int backgroundChildrenRunning = 0;
int backgroundPid = 0;
int ignoreBackground = 0;
int lastProcess = 0;

void sigtstp_handler(int signum) { //Signal handler for SIGTSTP (Ctrl-Z)
    if (ignoreBackground == 0) {
        if (backgroundChildrenRunning == 0) {
            write(1, "Entering foreground-only mode (& is now ignored)\n", 49);
            write(1, ": ", 2);
            ignoreBackground = 1;
        }
    } else {
        write(1, "Exiting foreground-only mode\n", 29);
        write(1, ": ", 2);
        ignoreBackground = 0;
    }
    fflush(stdout);
}

void check_background() {
    if (backgroundChildrenRunning > 0) {        //this function is to print the background process termination signal
        int currentStatus;
        int completedChild = waitpid(-1, &currentStatus, WNOHANG);      //use WHOHANG so it does not block

        if (completedChild > 0) {
            if (WIFEXITED(currentStatus)) {
                printf("Background process with ID %d has been completed. Exit value: %d.\n", completedChild, WEXITSTATUS(currentStatus));
                last_termination = WEXITSTATUS(currentStatus);
            } else if (WIFSIGNALED(currentStatus)) {
                printf("Background process with ID %d has been completed. Terminated by signal: %d.\n", completedChild, WTERMSIG(currentStatus));
                last_termination = WTERMSIG(currentStatus);
            } else if (completedChild == backgroundPid) {
                printf("Background process with ID %d has been completed. Terminated by signal: %d.\n", completedChild, WTERMSIG(currentStatus));
                last_termination = WTERMSIG(currentStatus);
            }
            backgroundPid = 0;
            backgroundChildrenRunning -= 1;
        }
    }
}

//function protypes
char* prompt(int pid);
char* replaceStringWithInt(char *str, int value);

//main func
int main() {
    int x = 0;

    signal(SIGTSTP, sigtstp_handler);   //handle control z


    struct sigaction SIGINT_action = { 0 };     //make sigint handler to ignore
    SIGINT_action.sa_handler = SIG_IGN;
    sigfillset(&SIGINT_action.sa_mask);
    SIGINT_action.sa_flags = 0;
    sigaction(SIGINT, &SIGINT_action, NULL);


    while(x == 0) {
        check_background();
        int pid = getpid();
        char* command = prompt(pid);   //get user input
        if (command != NULL) {
            if (command[0] == '\0' || command[0] == '#') { //empty or comment
                free(command); // Free the memory
                continue;
            }
            command = replaceStringWithInt(command, pid); //expansion
            if (strcmp("exit", command) == 0) {
                if (backgroundChildrenRunning > 0) {    //kill all backhroudn before exit
                    kill(backgroundPid, SIGKILL); 
                }
                return 0;
            } else if (strstr(command, "status") != NULL) {
                if (lastProcess == 0) { //foreground
                    printf("exit value %d\n", last_exit_status);
                } else {    //background
                    printf("terminated by signal %d\n", last_termination);
                }
            } else if (strstr(command, "cd") != NULL) {  
                if (strlen(command) == 2) { // no command
                    chdir(getenv("HOME"));
                } else {
                    char* args = command + 3; //Skip cd and white space
                    chdir(args);
                }
            } else {
                char *token;
                char *argv[1024]; // Array to store command and arguments
                char *argvSec[1024];
                int i = 0;

                int childExitMethod;
                pid_t spwanPID = fork();

                token = strtok(command, " "); // Split the command into tokens
                while (token != NULL) {//add in all tokens into arr
                    argv[i] = token;
                    token = strtok(NULL, " ");
                    i++;
                }
                argv[i] = NULL;
                int hasSymbolEnd = 0;
                if (strcmp(argv[i - 1], "&") == 0){     //checks to see if background or foreground process
                    if (ignoreBackground == 0) {
                        hasSymbolEnd = 1;
                    }
                    argv[i - 1] = NULL; // Remove the '&' symbol
                }

                switch (spwanPID){
                case -1:
                    last_exit_status = 1;
                case 0:
                    
                    if (hasSymbolEnd == 0) {        //if foreground proccess make defulat action for sigint
                        SIGINT_action.sa_handler = SIG_DFL;
                        sigaction(SIGINT, &SIGINT_action, NULL);
                    }
                    
                    if (hasSymbolEnd == 1) {
                        backgroundPid = getpid();
                    }

                    char* redirect_arg = NULL;
                    char* redirect_arg2 = NULL;
                    
                    for (int i = 0; argv[i] != NULL; i++) {//Check if array contains any redirection
                        if (strcmp(argv[i], ">") == 0) {
                            redirect_arg = argv[i + 1];
                        } else if (strcmp(argv[i], "<") == 0) {
                            redirect_arg2 = argv[i + 1];
                        } else {
                            if(i == 0){
                                argvSec[i] = argv[i];
                            }
                        }
                    }
                    argvSec[1] = NULL;

                    if (redirect_arg2 != NULL && redirect_arg != NULL) {    //if both input and output redirection
                        int fd_in = open(redirect_arg2, O_RDONLY);//Handle input redirection
                        if (fd_in == -1) {
                            printf("File %s could not be opened.", redirect_arg2);
                            last_exit_status = 1;
                            exit(1);
                        }
                        dup2(fd_in, STDIN_FILENO);
                        close(fd_in);

                        int fd_out = open(redirect_arg, O_WRONLY | O_CREAT | O_TRUNC, 0666);//Handle output redirection
                        if (fd_out == -1) {
                            printf("File %s could not be opened.", redirect_arg);
                            last_exit_status = 1;
                            exit(1);
                        }
                        dup2(fd_out, STDOUT_FILENO);
                        close(fd_out);

                        execvp(argvSec[0], argvSec);
                        last_exit_status = 1;
                        exit(1);
                    } else if (redirect_arg2 != NULL) { //only output redirection
                        int fd_out;
                        if (hasSymbolEnd == 1) {//only for background
                            // Redirect standard output to /dev/null
                            fd_out = open("/dev/null", O_WRONLY);
                            if (fd_out == -1) {
                                last_exit_status = 1;
                                exit(1);
                            }
                            dup2(fd_out, STDOUT_FILENO);
                            close(fd_out);
                        }

                        int fd_in = open(redirect_arg2, O_RDONLY);   //open the file
                        if (fd_in == -1) {
                            last_exit_status = 1;
                            exit(1);
                        }
                        dup2(fd_in, STDIN_FILENO);  //redirect output
                        close(fd_in);

                        execvp(argvSec[0], argvSec);
                        last_exit_status = 1;
                        exit(1);
                    } else if (redirect_arg != NULL) {  //only input redirection
                        int fd_in;
                        if (hasSymbolEnd == 1) { // only for background
                            // Redirect standard input from /dev/null
                            fd_in = open("/dev/null", O_RDONLY);
                            if (fd_in == -1) {
                                last_exit_status = 1;
                                exit(1);
                            }
                            dup2(fd_in, STDIN_FILENO);
                            close(fd_in);
                        }

                        int fd_out = open(redirect_arg, O_WRONLY | O_CREAT | O_TRUNC, 0666);
                        if (fd_out == -1) {
                            last_exit_status = 1;
                            exit(1);
                        }
                        dup2(fd_out, STDOUT_FILENO);    //redirect output
                        close(fd_out);

                        execvp(argvSec[0], argvSec);
                        last_exit_status = 1;
                        exit(1);
                    } else {
                        int fd_in, fd_out;
                        if (hasSymbolEnd == 1) {//only for background
                            // Redirect standard input from /dev/null
                            fd_in = open("/dev/null", O_RDONLY);
                            if (fd_in == -1) {
                                last_exit_status = 1;
                                exit(1);
                            }
                            dup2(fd_in, STDIN_FILENO);
                            close(fd_in);

                            // Redirect standard output to /dev/null
                            fd_out = open("/dev/null", O_WRONLY);
                            if (fd_out == -1) {
                                last_exit_status = 1;
                                exit(1);
                            }
                            dup2(fd_out, STDOUT_FILENO);
                            close(fd_out);
                        }

                        execvp(argv[0], argv);
                        last_exit_status = 1;
                        exit(1);
                    }
                default:
                    if (hasSymbolEnd == 0){
                        lastProcess = 0;
                        waitpid(spwanPID, &childExitMethod, 0); // Wait for the child only if background

                        if (WIFEXITED(childExitMethod)) {
                            int exitStatus = WEXITSTATUS(childExitMethod);
                            if (exitStatus == 0) {//no error
                                last_exit_status = 0;
                            } else {
                                printf("Error, exit Value: %d\n", exitStatus);
                                last_exit_status = exitStatus;
                            }
                        } else if (WIFSIGNALED(childExitMethod)) {   //Check if the child was terminated by a signal
                            int terminatedBySignal = WTERMSIG(childExitMethod);
                            printf("Terminated by signal %d\n", terminatedBySignal);
                        } else {
                            last_exit_status = 1;
                            printf("exit status 1.\n");
                        }
                    } else {
                        lastProcess = 1;
                        printf("background pid is %d\n", spwanPID);      //print the child pid as requeste
                        backgroundChildrenRunning += 1;
                    }
                }
            }
            free(command); // Free the dynamically allocated memory
        }
    }
    return 0;
}

char* prompt(int pid) {
    printf("%d:", pid);
    fflush(stdout);

    char* input = NULL;
    size_t input_size = 0;
    ssize_t read_bytes = getline(&input, &input_size, stdin);// Use getline to read user input

    if (read_bytes == -1) {
        free(input); // Free the memory
        return NULL;
    }

    if (input[read_bytes - 1] == '\n') {// Remove the newline character at the end
        input[read_bytes - 1] = '\0';
    }
    return input;
}

char* replaceStringWithInt(char *str, int value) {
    char *result = malloc(strlen(str) * 2 + 1);  // Allocate memory for the result string
    if (result == NULL) {
        return str;
    }

    const char *src = str;
    char *dest = result;

    while (*src) {
        if (src[0] == '$' && src[1] == '$') {
            int written = snprintf(dest, 12, "%d", value);
            dest += written;
            src += 2; // Skip the "$$"
        } else {
            *dest++ = *src++;
        }
    }
    *dest = '\0';//complete the string

    return result;
}