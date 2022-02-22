#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <readline/readline.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>
#include <sys/param.h>
#include <errno.h>

// default buffer sizes
#define SUBS_BUFSIZE 8
#define CWD_BUFSIZE 64

// error strings
#define MALLOC_ERR "Error while allocating memory"
#define REALLOC_ERR "Error while reallocating memory"
#define CLOSE_ERR "Error while closing a file descriptor"
#define WAIT_ERR "Error while waiting for a child process to terminate"
#define FORK_ERR "Error while forking process"
#define DUP_OUT_ERR "Dup error while redirecting output in a child process"
#define DUP_IN_ERR "Dup error while redirecting input in a child process"
#define SPRINTF_ERR "Error while creating a message"

// message for
#define OTHERSTATUS_MSG "A child process terminated with an unknown status\n"

// prompt colours
#define BOLDGREEN "\033[1m\033[32m"
#define BOLDBLUE "\033[1m\033[34m"
#define BOLDRED "\033[1m\033[31m"
#define RESET "\033[0m"

// function declarations
void sys_error(char* error_msg);
int check_spaces(char* string, const char end);
char* get_prompt(int last_exit_status);
int change_dir(char** arguments);
char** split_commands(char* command, int* command_no);
int check_redirection_argument(char* arg, char** redirect_path);
int get_arguments(char* command, char*** arguments, char** input_redirect_path, char** output_redirect_path);
int parse_line(char* command, char**** parsed_line, int* command_no, char** input_redirect_path, char** output_redirect_path);
int exec(char*** command, int command_no, char* input_redirect_path, char* output_redirect_path);
int my_system(char* command);

int main()
{
    int last_exit_status = 0;
    while(1)
    {
        char* prompt = get_prompt(last_exit_status);
        // read user input
        char* command = readline(prompt);
        if(command == NULL)
            exit(EXIT_SUCCESS);

        free(prompt);

        // if the command isn't empty
        if(check_spaces(command, '\0') == 0)
        {
            char empty_pipe = 0;
            // Check if there is a command made up of only spaces
            for(int i = 0; command[i] != '\0' && empty_pipe == 0; i++)
            {
                if(command[i] == '|')
                    empty_pipe = check_spaces(command + i + 1, '|');
            }

            // If no parsing errors were found, call exec and set last_exit_status
            if(empty_pipe == 0)
                last_exit_status = my_system(command);
            else
            {
                fprintf(stderr, "Can't execute an empty command\n");
                last_exit_status = 1;
            }

        }
        // else reset last_exit_status
        else
            last_exit_status = 0;

        free(command);
    }
}

// prints `error_msg` and `errno`, then exits with exit status 1
void sys_error(char* error_msg)
{
    perror(error_msg);
    exit(EXIT_FAILURE);
}

// returns true if `string` is made up of only ` ` until the delimiter `end`, false otherwise
int check_spaces(char* string, const char end)
{
    int i = 0;

    while(string[i] != end && string[i] != '\0')
    {
        if(string[i] != ' ')
            return 0;
        i++;
    }

    return 1;
}

// prints prompt and reads (and returns) user input
char* get_prompt(int last_exit_status)
{
    // initialize a buffer for the current working directory
    int current_size = CWD_BUFSIZE;
    char* cwd = (char*) malloc(current_size);
    if(cwd == NULL)
        sys_error(MALLOC_ERR);

    // while the syscall returns NULL (=> error)
    while(getcwd(cwd, current_size) == NULL)
    {
        // if the error was caused because the working directory was too long for the buffer, reallocate a bigger buffer and then try again
        if(errno == ENAMETOOLONG)
        {
            current_size += CWD_BUFSIZE;
            cwd = realloc(cwd, current_size);
            if(cwd == NULL)
                sys_error(REALLOC_ERR);
        }
        // if the buffer wasn't the cause of the error, print the error and exit
        else
            sys_error("Couldn't get current directory");
    }

    // get host name
    char hostname[HOST_NAME_MAX + 1];
    if(gethostname(hostname, HOST_NAME_MAX + 1) != 0)
        sys_error("Couldn't get hostname to show in the prompt");

    // size = current size of the cwd buffer + max host name size + max username size (see manpage for `useradd`) + ~size of the bigger prompt + `\0`
    char* prompt = (char*) malloc(current_size + HOST_NAME_MAX + 32 + 75 + 1);
    if(prompt == NULL)
        sys_error(MALLOC_ERR);
    int sprintf_res;
    if(last_exit_status == 0)
        sprintf_res = sprintf(prompt, BOLDRED "µsh:" RESET BOLDGREEN "%s@%s" RESET ":" BOLDBLUE "%s" RESET "$ ", getenv("USER"), hostname, cwd);
    else
        sprintf_res = sprintf(prompt, BOLDRED "µsh:" RESET BOLDGREEN "%s@%s" RESET ":" BOLDBLUE "%s" BOLDRED " %d " RESET "$ ", getenv("USER"), hostname, cwd, last_exit_status);
    if(sprintf_res < 0)
    {
        fprintf(stderr, "Error formatting the prompt\n");
        return NULL;
    }
    free(cwd);
    return prompt;
}

// builtin `cd` command
int change_dir(char** arguments)
{
    int arguments_size = 0;

    // check the number of arguments passed to the function, if more than one extra argument has been passed to the command, print an error and return
    while(arguments[arguments_size] != NULL)
    {
        if(arguments_size++ >= 2)
        {
            fprintf(stderr, "Too many arguments passed to cd\n");
            return 1;
        }
    }

    char* path;
    if(arguments_size == 2)
        path = arguments[1];
    else
        // no arguments -> cd $HOME
        path = getenv("HOME");

    // try to change dir, if the syscall fails, print an error string and return
    if(chdir(path) == -1)
    {
        // message error lenght (-1, not counting '\0') + lenght of the path variable + 1 ('\0')
        char* buf = (char*) malloc(36 + strlen(path) + 1);
        if(buf == NULL)
            sys_error(MALLOC_ERR);
        if(sprintf(buf, "cd error: can't change directory to %s", path) == -1)
        {
            fprintf(stderr, SPRINTF_ERR);
            exit(EXIT_FAILURE);
        }
        perror(buf);
        free(buf);
        return 1;
    }
    return 0;
}

// returns an array of commands splitting `command` on '|' and sets their number to command_no
char** split_commands(char* command, int* command_no)
{
    int bufsize = SUBS_BUFSIZE;
    // commands is an array of commands
    char** commands = (char**) malloc(bufsize * sizeof(char*));
    if(commands == NULL)
        sys_error(MALLOC_ERR);

    // rest is a buffer for the part of `command` that hasn't been split yet
    char* rest = NULL;
    char* curr_command;

    // split commands on "|"
    for(curr_command = strtok_r(command, "|", &rest); curr_command != NULL; curr_command = strtok_r(NULL, "|", &rest), (*command_no)++)
    {
        // reallocate the buffer if it isn't large enough
        if(*command_no >= bufsize)
        {
            bufsize += SUBS_BUFSIZE;

            commands = realloc(commands, bufsize * sizeof(char*));
            if(commands == NULL)
                sys_error(REALLOC_ERR);
        }
        commands[*command_no] = curr_command;
    }
    return commands;
}

// auxiliary function for get_arguments
// checks if arg is a suitable redirect path && checks if the redirection path hasn't already been changed from its default value
int check_redirection_argument(char* arg, char** redirect_path)
{
    // if there is no string after the `>` or `<` print an error string and return
    if(strlen(arg) == 0)
    {
        fprintf(stderr, "Spaces between \'>\' or \'<\' and the i/o redirection file path are not supported\n");
        return -1;
    }

    // if the current command has already redirected i/o print error string and return
    if(*redirect_path != NULL)
    {
        fprintf(stderr, "Multiple output redirections of the same type are not supported\n");
        return -1;
    }
    *redirect_path = (char*) malloc(strlen(arg) + 1);
    if(*redirect_path == NULL)
        sys_error(MALLOC_ERR);
    strcpy(*redirect_path, arg);
    return 0;
}

// splits the arguments found in `command` and sets the i/o redirection file paths
int get_arguments(char* command, char*** arguments, char** input_redirect_path, char** output_redirect_path)
{
    int size = 0;

    // initialize buffer
    int bufsize = SUBS_BUFSIZE;
    *arguments = (char**) malloc(bufsize * sizeof(char*));
    if(*arguments == NULL)
        sys_error(MALLOC_ERR);

    char* rest = NULL;
    char* arg;

    // make sure the i/o redirection paths are set to NULL
    free(*input_redirect_path);
    free(*output_redirect_path);

    // split `command` into arguments on " " and iterate over them
    for(arg = strtok_r(command, " ", &rest); arg != NULL; arg = strtok_r(NULL, " ", &rest))
    {
        // if the user is trying to redirect i/o, check the argument
        if(arg[0] == '>')
        {
            if(check_redirection_argument(arg + 1, output_redirect_path) != 0)
                return -1;
        }
        else if(arg[0] == '<')
        {
            if(check_redirection_argument(arg + 1, input_redirect_path) != 0)
                return -1;
        }
        else
        {
            // substitute environment variables into the arguments
            if(arg[0] == '$')
            {
                char* env_var = getenv(arg + 1);
                if(env_var == NULL)
                {
                    fprintf(stderr, "No match for env variable %s\n", arg + 1);
                    return -1;
                }
                arg = env_var;
            }

            // resize the buffer if needed
            if(size >= bufsize)
            {
                bufsize += SUBS_BUFSIZE;

                *arguments = realloc(*arguments, bufsize * sizeof(char*));
                if(*arguments == NULL)
                    sys_error(REALLOC_ERR);
            }

            (*arguments)[size] = arg;
            size++;
        }
    }

    // prevent commands with only redirection from bein run
    if(size == 0)
    {
        fprintf(stderr, "Can't run a command made of only redirection statements\n");
        return -1;
    }

    // execvp expects last parameter to be NULL
    // resize the buffer if needed
    if(size + 1 >= bufsize)
    {
        *arguments = realloc(*arguments, (size + 1) * sizeof(char*));
        if(*arguments == NULL)
            sys_error(REALLOC_ERR);
    }
    // and finally add null
    (*arguments)[size] = NULL;

    return 0;
}

// this function parses the `command` argument into arguments useful for the `exec` function
int parse_line(char* command, char**** parsed_line, int* command_no, char** input_redirect_path, char** output_redirect_path)
{
    // split `command` into sub-commands and set `command_no`
    char** commands = split_commands(command, command_no);

    // parsed_line is a pointer to an array of commands (which in turn are arrays of args)
    *parsed_line = (char***) malloc(*command_no * sizeof(char**));
    if(*parsed_line == NULL)
        sys_error(MALLOC_ERR);

    // iterate over the subcommands
    for(int i = 0; i < *command_no; i++)
    {
        char* tmp_in = NULL;
        char* tmp_out = NULL;
        //array of the i-th comamand's arguments
        int result = get_arguments(commands[i], *parsed_line + i, &tmp_in, &tmp_out);
        if(result == 0)
        {
            // making sure redirection only happens on first and last command (and that, when it happens, only the right type can happen)
            if(*command_no > 1)
            {
                int err = 0;
                if(i == 0 && tmp_out != NULL)
                {
                    fprintf(stderr, "Can't redirect output on the first command\n");
                    err = 1;
                }
                else if(i == *command_no - 1 && tmp_in != NULL)
                {
                    fprintf(stderr, "Can't redirect input on the last command\n");
                    err = 1;
                }
                else if((i != 0 && i != *command_no - 1) && (tmp_in != NULL || tmp_out != NULL))
                {
                    fprintf(stderr, "Can't redirect i/o in between commands\n");
                    err = 1;
                }
                // if there was an error release the resources, set the values and then return
                if(err)
                {
                    free(tmp_in);
                    free(tmp_out);
                    free(commands);
                    // set this in order not to segfault at label FREE 
                    *command_no = i + 1;
                    return -1;
                }
            }

            // if theere were no errors setting redirect files
            if(tmp_in != NULL)
            {
                *input_redirect_path = realloc(*input_redirect_path, strlen(tmp_in) + 1);
                if(*input_redirect_path == NULL)
                    sys_error(REALLOC_ERR);

                strcpy(*input_redirect_path, tmp_in);
            }
            if(tmp_out != NULL)
            {
                *output_redirect_path = realloc(*output_redirect_path, strlen(tmp_out) + 1);
                if(*output_redirect_path == NULL)
                    sys_error(REALLOC_ERR);

                strcpy(*output_redirect_path, tmp_out);
            }
        }

        free(tmp_in);
        free(tmp_out);

        // if there was an error free `commands` and exit
        if(result != 0)
        {
            // set this in order not to segfault at label FREE
            *command_no = i + 1;
            free(commands);
            return -1;
        }
    }
    free(commands);
    return 0;
}

int exec(char*** command, int command_no, char* input_redirect_path, char* output_redirect_path)
{
    int exit_status = 0;
    pid_t pid;

    int* pipe_fds = (int*) malloc(2 * (command_no - 1) * sizeof(int));
    if(pipe_fds == NULL)
        sys_error(MALLOC_ERR);

    for(int i = 0; i < command_no - 1; i++)
    {
        if(pipe(pipe_fds + i * 2) < 0)
            sys_error("Couldn't create pipe between two commands");
    }

    for(int i = 0; i < command_no * 2; i += 2)
    {
        pid = fork();
        if(pid == -1)
            sys_error(FORK_ERR);

        // child behaviour
        if(pid == 0)
        {
            // not first command
            if(i != 0)
            {
                // input is last command's output
                if(dup2(pipe_fds[i - 2], STDIN_FILENO) < 0)
                    sys_error(DUP_IN_ERR);
            }
            // first command with redirected input
            else if(input_redirect_path != NULL)
            {
                int input_fd = open(input_redirect_path, O_RDONLY);
                if(input_fd == -1)
                {
                    // message error length (-1, not counting '\0') + length of the path variable + 1 ('\0')
                    char* buf = (char*) malloc(45 + strlen(input_redirect_path) + 1);
                    if(buf == NULL)
                        sys_error(MALLOC_ERR);

                    if(sprintf(buf, "Error while opening input redirection file (%s)", input_redirect_path) < 0)
                    {
                        fprintf(stderr, SPRINTF_ERR);
                        exit(EXIT_FAILURE);
                    }
                    perror(buf);
                    free(buf);
                    return 1; // exit status code
                }
                // redirect input and close previous fd
                if(dup2(input_fd, STDIN_FILENO) == -1)
                    sys_error(DUP_IN_ERR);

                if(close(input_fd) == -1)
                    sys_error(CLOSE_ERR);
            }

            // if the current command is not the last redirect output to the pipe
            if(i / 2 < command_no - 1)
            {
                if(dup2(pipe_fds[i + 1], STDOUT_FILENO) == -1)
                    sys_error(DUP_OUT_ERR);
            }
            // else redirect it to the specified path (if it was specified) or to stdout
            else if(output_redirect_path != NULL)
            {
                // flags used by bash: rw for user and group and r for others (664)
                int output_fd = open(output_redirect_path, O_CREAT | O_APPEND | O_WRONLY, S_IROTH | S_IWGRP | S_IWUSR | S_IRUSR | S_IRGRP);
                if(output_fd == -1)
                {
                    // message error length (-1, not counting '\0') + length of the path variable + 1 ('\0')
                    char* buf = (char*) malloc(46 + strlen(output_redirect_path) + 1);
                    if(buf == NULL)
                        sys_error(MALLOC_ERR);

                    if(sprintf(buf, "Error while opening output redirection file (%s)", output_redirect_path) < 0)
                    {
                        fprintf(stderr, SPRINTF_ERR);
                        exit(EXIT_FAILURE);
                    }
                    perror(buf);
                    free(buf);
                    return 1; //error status code
                }

                if(dup2(output_fd, STDOUT_FILENO) == -1)
                    sys_error(DUP_OUT_ERR);

                if(close(output_fd) == -1)
                    sys_error(CLOSE_ERR);
            }

            for(int j = 0; j < 2 * (command_no - 1); j++)
                close(pipe_fds[j]);

            if(execvp(command[i / 2][0], command[i / 2]) == -1)
            {
                char* buf = (char*) malloc(35 + strlen(command[i / 2][0]) + 1);
                if(buf == NULL)
                    sys_error(MALLOC_ERR);
                if(sprintf(buf, "Execvp: couldn't execute command \'%s\'", command[i / 2][0]) == -1)
                {
                    fprintf(stderr, SPRINTF_ERR);
                    exit(EXIT_FAILURE);
                }
                // exit() in sys_error will deallocate buf and close the file descriptors
                sys_error(buf);
            }
        }
    }

    for(int i = 0; i < 2 * (command_no - 1); i++)
        close(pipe_fds[i]);

    free(pipe_fds);

    // parent behaviour
    for(int i = 0; i < command_no; i++)
    {
        // wait child process and check its exit value
        int status;
        if(wait(&status) == -1)
            sys_error(WAIT_ERR);
        else if(WIFEXITED(status))
            exit_status = WEXITSTATUS(status); // save the exit status if the child terminated normally
        // else print what happened to the child process on the screen
        else if(WIFSIGNALED(status))
        {
            int signo = WTERMSIG(status);
            // if a command tries to write to a closed pipe SIGPIPE is signaled
            if(signo != SIGPIPE)
                fprintf(stderr, "Child terminated by signal %d, (%s)\n", signo, strsignal(signo));
        }
        else
            fprintf(stderr, OTHERSTATUS_MSG);
    }

    return exit_status;
}

// executes `command` and returns its status code
int my_system(char* command)
{
    char*** line = NULL; // array made of commands (array of args)
    char* input_redirect = NULL;
    char* output_redirect = NULL;
    int command_no = 0;
    int exit_status = 0;

    int res = parse_line(command, &line, &command_no, &input_redirect, &output_redirect);
    if(res == 0)
    {
        // cd only if it's the only command in the line
        if(command_no == 1 && strcmp(line[0][0], "cd") == 0)
        {
            if(input_redirect == NULL && output_redirect == NULL)
                exit_status = change_dir(line[0]);
            else
            {
                fprintf(stderr, "Can't redirect i/o of the 'cd' command\n");
                exit_status = 1;
            }
        }
        else if(command_no > 0)
        {
            // check if cd is between the commands
            for(int i = 0; i < command_no; i++)
            {
                if(strcmp(line[i][0], "cd") == 0)
                {
                    // if cd is in the commands, print an error and goto free
                    fprintf(stderr, "\'cd\' must be used alone (too many commands).\n");
                    exit_status = 1;
                    goto FREE;
                }
            }
            // no need to check output as the function will return without doing anything else anyways
            exit_status = exec(line, command_no, input_redirect, output_redirect);
        }
    }
    else
        exit_status = 1;

FREE: // free reserved memory and return `command`'s exit_status
    for(int i = 0; i < command_no; i++)
        free(line[i]);
    free(line);

    free(input_redirect);
    free(output_redirect);
    return exit_status;
}