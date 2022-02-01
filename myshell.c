#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/wait.h>
#include <unistd.h>

#define PREPARE_RAN_PROPERLY 0
#define PREPARE_RAN_WITH_ERROR 1

#define COMMAND_RAN_WITH_ERROR 0
#define COMMAND_RAN_PROPERLY 1

#define STANDARD_COMMAND 1
#define BACKGROUND_COMMAND 2
#define PIPE_COMMAND 3
#define FILE_OUTPUT_COMMAND 4

// *** declartions ***
int process_arglist(int, char**);
int prepare(void);
int finalize(void);
int characterize_command(int, char**);
int run_standard_command(int, char**);
int run_backround_command(int, char**);
int run_pipe_command(int, char**);
int run_file_output_command(int, char**);
void child_process_execution(char**);
int wait_fot_it(int);
int find_first_index_of_pipe_symbol(char**);
struct sigaction sigaction_constructor();
void default_sigint_handler();
int ignore_type_sigint_handler();
int sigchild_handler();
void sigchild_sa_handler(int);

// *** implementations ***

int process_arglist(int count, char** arglist) {
	int command_property = characterize_command(count, arglist);
	switch(command_property) {
		case STANDARD_COMMAND:
			return run_standard_command(count, arglist);
			break;
        case BACKGROUND_COMMAND:
			return run_backround_command(count, arglist);
			break;
		case PIPE_COMMAND:
			return run_pipe_command(count, arglist);
			break;
		case FILE_OUTPUT_COMMAND:
			return run_file_output_command(count, arglist);
	}
	return COMMAND_RAN_WITH_ERROR;
}

int prepare(void) {
    // any child process will inherit these handlers (can be changed later).
	if (ignore_type_sigint_handler() != PREPARE_RAN_PROPERLY) {return PREPARE_RAN_WITH_ERROR;}
	if (sigchild_handler() != PREPARE_RAN_PROPERLY) {return PREPARE_RAN_WITH_ERROR;}
	return PREPARE_RAN_PROPERLY;
}

int finalize(void) {
    return 0;
}

int characterize_command(int count, char** arglist) {
    int i = 0;

	if (strcmp(arglist[count-1], "&") == 0) {
		return BACKGROUND_COMMAND;
	}
    if ((count > 1) && (strcmp(arglist[count-2], ">")) == 0) {
		return FILE_OUTPUT_COMMAND;
	}
    while (i < count) {
        if (strcmp(arglist[i], "|") == 0) {
            return PIPE_COMMAND;
        }
		i++;
    }
	return STANDARD_COMMAND;
}

int run_standard_command(int count, char** arglist) {
    pid_t pid = fork();
    
    //fork failure:
    if (pid < 0) {
		perror("fork() failed");
		return COMMAND_RAN_WITH_ERROR;
	}
    //child process:
	if (pid == 0) {
        default_sigint_handler();
		child_process_execution(arglist);
	}

    //parent process (waiting for child to finish):
	return wait_fot_it(pid);
}

int run_backround_command(int count, char** arglist) {
	pid_t pid= fork();

    //fork failure:
    if (pid < 0) {
		perror("fork() failed");
		return COMMAND_RAN_WITH_ERROR;
	}
    //child process:
	if (pid == 0) {
        arglist[count-1] = NULL; //'&' won't pass to execvp().
		child_process_execution(arglist);
	}
    //parent process:
	return COMMAND_RAN_PROPERLY;
}

int run_pipe_command(int count, char** arglist) {
	int pipefds[2];
    int readerfd;
    int writerfd;
    pid_t first_pid;
	pid_t second_pid;
    char** first_process;
	char** second_process;
    int pipe_index;
    int first_wait;
    int second_wait;
	
    pipe_index = find_first_index_of_pipe_symbol(arglist);
	arglist[pipe_index] = NULL;
	first_process = arglist;
	second_process = arglist + (pipe_index + 1);
	
	if (pipe(pipefds) == -1) {
        perror("pipe() failed");
		return COMMAND_RAN_WITH_ERROR;
	}
	readerfd = pipefds[0];
    writerfd = pipefds[1];
	
	first_pid = fork();
    //first fork failure:
	if (first_pid < 0) {
		perror("fork() failed");
        close(readerfd);
        close(writerfd);
		return COMMAND_RAN_WITH_ERROR;
	}
    //first child process:
	if(first_pid == 0) {
		default_sigint_handler();
		if (close(readerfd) == -1) {
            perror("close() failed");
			exit(1);
		}
		if (dup2(writerfd, 1) == -1) { //File Descriptor 1 reserved for STDOUT.
            perror("dup2() failed");
			exit(1);
		}
		if (close(writerfd) == -1) {
            perror("close() failed");
			exit(1);
		}
		child_process_execution(first_process);
	}
	
	//parent process, creates the second child.
	second_pid = fork();
	//second fork failure:
	if (second_pid < 0) {
		perror("fork() failed");
        close(readerfd);
        close(writerfd);
		return COMMAND_RAN_WITH_ERROR;
	}
	//second child process:
	if(second_pid == 0) {
		default_sigint_handler();
		if (close(writerfd) == -1) {
            perror("close() failed");
			exit(1);
		}
		if (dup2(readerfd, 0) == -1) { //File Descriptor 0 reserved for STDIN.
            perror("dup2() failed");
			exit(1);
		}
		if (close(readerfd) == -1) {
            perror("close() failed");
			exit(1);
		}
		child_process_execution(second_process);
	}
	//parent process (waiting for children to finish):
	if ((close(readerfd) == -1) || close(writerfd) == -1) {
		perror("close() failed");
		return COMMAND_RAN_WITH_ERROR;
	}

	first_wait = wait_fot_it(first_pid);
	second_wait = wait_fot_it(second_pid);
	if ((first_wait == COMMAND_RAN_PROPERLY) && (second_wait == COMMAND_RAN_PROPERLY)) {
		return COMMAND_RAN_PROPERLY;
	}
	return COMMAND_RAN_WITH_ERROR;
}

int run_file_output_command(int count, char** arglist) {
	arglist[count-2] = NULL; //count-2 is the '>' symbol index.
	char* output_file = arglist[count-1];
    int fd;

	pid_t pid = fork();
	//fork failure:
	if (pid < 0) {
        perror("fork() failed");
		return COMMAND_RAN_WITH_ERROR;
	}
	//child process:
	if(pid == 0) {
        default_sigint_handler();
		fd = open(output_file, O_WRONLY | O_CREAT | O_TRUNC, 0777);
		if (fd == -1) {
            perror("open() failed");
			exit(1);
		}
		if (dup2(fd ,1) == -1) { //File Descriptor 1 reserved for STDOUT.
            perror("dup2() failed");
			exit(1);
		}
		if (close(fd) < 0) {
            perror("close() failed");
			exit(1);
		}
		child_process_execution(arglist);
	}
    //parent process (waiting for child to finish):
	return wait_fot_it(pid);
}

void child_process_execution(char** arglist) {
	execvp(arglist[0], arglist); //arglist[0] is the program to run.
	//running the following code indicates that execvp() failed.
	perror("execvp() failed");
	exit(1);
}

int wait_fot_it(int pid) {
	int status = 0;
	if (waitpid(pid, &status, WUNTRACED) == -1) {
		//waitpid() returned -1, thus an error occurred.
		if (errno != EINTR && errno != ECHILD) { //if the error is EINTR or ECHILD than it's OK.
			perror("wait() failed");
			return COMMAND_RAN_WITH_ERROR;
		}
	}
	return COMMAND_RAN_PROPERLY;
}

int find_first_index_of_pipe_symbol(char** arglist) {
	int i = 0;
	while(arglist[i] != NULL) {
		if (strcmp(arglist[i], "|") == 0) {
			return i;
		}
		i++;
	}
	return -1;
}

struct sigaction sigaction_constructor() {
    struct sigaction new_action;
	memset(&new_action, 0, sizeof(new_action));
	new_action.sa_flags = SA_RESTART;
    return new_action;
}

void default_sigint_handler() {
	struct sigaction new_action = sigaction_constructor();
    new_action.sa_handler  = SIG_DFL;
	if (sigaction(SIGINT,&new_action, NULL) != 0) {
		perror("sigaction() failed");
		exit(1);
	}
}

//a prepare() helper function.
int ignore_type_sigint_handler() {
	struct sigaction new_action = sigaction_constructor();
	new_action.sa_handler  = SIG_IGN;
	if (sigaction(SIGINT,&new_action, NULL) != 0) {
		perror("sigaction() failed");
		return PREPARE_RAN_WITH_ERROR;
	}
	return PREPARE_RAN_PROPERLY;
}

//a prepare() helper function.
int sigchild_handler() {
	struct sigaction new_action = sigaction_constructor();
	new_action.sa_handler = sigchild_sa_handler;
	if (sigaction(SIGCHLD, &new_action, 0) != 0) {
        perror("sigaction() failed:");
        return PREPARE_RAN_WITH_ERROR;
	}
	return PREPARE_RAN_PROPERLY;
}

//this is the function of sa_handler in the sigation stuct.
void sigchild_sa_handler(int signal) {
    //waitpid() returns -1 when child process already wait()ed.
    //wait() from the parent process deletes it's zombie record.
	while(waitpid(-1, NULL, WNOHANG) > 0);
}