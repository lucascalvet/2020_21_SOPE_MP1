#include <stdlib.h>
#include <stdio.h>
#include <stdbool.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#include <dirent.h>
#include <error.h>
#include <string.h>
#include <errno.h>
#include <signal.h>
#include <fcntl.h>
#include <limits.h>
#include <time.h>

#define OK 0
#define BIT(N) (1 << (N))
#define MODE_MASK 0x1ff

enum Verbosity
{
    OFF,
    CHANGES,
    ALL
};

enum Mode
{
    ALPHA_MODE,
    OCTAL_MODE
};

static char *path;
static enum Mode mode;
static char *e_mode;
static int nftot = 0;
static int nfmod = 0;
static int log_file = 0;
static bool log = false;
static clock_t start;
static enum Verbosity verbosity = OFF;
static bool recursive = false;
static struct stat f_stat;

typedef void sigfunc(int);
sigfunc *signal(int signo, sigfunc *func);

void print_error(int error)
{
    char *st_error = NULL;
    switch (error)
    {
    case EPERM:
        st_error = "Error Number 1  -> EPERM    -> Operation not permitted";
        break;
    case ENOENT:
        st_error = "Error Number 2  -> ENOENT   -> No such file or directory";
        break;
    case ESRCH:
        st_error = "Error Number 3  -> ESRCH    -> No such process";
        break;
    case EINTR:
        st_error = "Error Number 4  -> EINTR    -> Interrupted system call";
        break;
    case EIO:
        st_error = "Error Number 5  -> EIO      -> I/O error";
        break;
    case E2BIG:
        st_error = "Error Number 7  -> E2BIG    -> Argument list too long";
        break;
    case EINVAL:
        st_error = "Error Number 22 -> EINVAL   -> Invalid argument";
        break;

    default:
        st_error = "Unspecified Error";
        break;
    }

    printf("\n%s\n", st_error);

    exit(error); //TODO: Log program exits
}

void print_usage()
{
    printf("\n"
           "Usage: xmod [OPTIONS] MODE FILE/DIR\n"
           "       xmod [OPTIONS] OCTAL-MODE FILE/DIR\n"
           "\n"
           "OPTIONS:\n"
           " -v    output a diagnostic for every file processed\n"
           " -c    like verbose but report only when a change is made\n"
           " -R    change files and directories recursively\n"
           "\n"
           "MODE: <u|g|o|a><-|+|=><rwx>\n"
           "\n"
           "OCTAL-MODE: 0<0-7><0-7><0-7>\n");

    exit(0);
}

void write_log(char *event, char *info)
{
    //time_t instant = time(0);
    clock_t instant = clock();
    //unsigned log_time = difftime(instant, start) * 1000;
    unsigned log_time = (instant - start) * 1000 / CLOCKS_PER_SEC;
    pid_t pid = getpid();
    char *log;
    asprintf(&log, "%u ; %d ; %s ; %s\n", log_time, pid, event, info);
    write(log_file, log, 100);
    free(log);
}

void log_exit(int exit_n)
{
    if (log)
    {
        char *log_message;
        asprintf(&log_message, "Exit Code: %d", exit_n);
        write_log("PROC_EXIT", log_message);
        free(log_message);
    }

    exit(exit_n);
}

void sig_handler(int signal)
{
    if (signal == SIGINT)
    {
        pid_t pid = getpid();
        printf("\n%d ; %s ; %d ; %d", pid, path, nftot, nfmod);

        write_log("SIGNAL_RECV", "SIGINT");

        char *buffer = NULL;
        bool answer = false;
        size_t n = 0;

        do
        {
            printf("\nAre you sure you want to terminate the program? (Y/N)\n");

            getline(&buffer, &n, stdin);
            if (strcasecmp(buffer, "Y\n") == 0)
            {
                answer = true;
                break;
            }
            else if (strcasecmp(buffer, "N\n") == 0)
            {
                answer = false;
                break;
            }

        } while (true);

        free(buffer);

        if (answer)
        {
            exit(0);
        }
    }
}

int get_mode_string(mode_t mode, char **result)
{
    *result = malloc(10 * sizeof(char));
    if (*result == NULL)
        return -1;
    mode = mode & MODE_MASK;
    strcpy(*result, "rwxrwxrwx");
    for (int i = 0; i < 9; i++)
    {
        if (!(mode & BIT(8 - i)))
            (*result)[i] = '-';
    }

    return OK;
}

mode_t get_mode(mode_t i_mode)
{
    mode_t f_mode;
    if (mode == ALPHA_MODE)
    {
        mode_t perms = 0;
        for (int i = 2; i < 5 && e_mode[i] != '\0'; i++)
        {
            switch (e_mode[i])
            {
            case 'r':
                perms = perms | BIT(2) | BIT(5) | BIT(8);
                break;

            case 'w':
                perms = perms | BIT(1) | BIT(4) | BIT(7);
                break;

            case 'x':
                perms = perms | BIT(0) | BIT(3) | BIT(6);
                break;

            default:
                break;
            }
        }

        mode_t users = 0;

        if (e_mode[0] == 'u' || e_mode[0] == 'a')
            users = users | BIT(6) | BIT(7) | BIT(8);
        if (e_mode[0] == 'g' || e_mode[0] == 'a')
            users = users | BIT(3) | BIT(4) | BIT(5);
        if (e_mode[0] == 'o' || e_mode[0] == 'a')
            users = users | BIT(0) | BIT(1) | BIT(2);

        if (e_mode[1] == '-')
            f_mode = i_mode & ~(perms & users);
        if (e_mode[1] == '+')
            f_mode = i_mode | (perms & users);
        if (e_mode[1] == '=')
            f_mode = (i_mode & ~users) | (perms & users);
    }
    else if (mode == OCTAL_MODE)
    {
        f_mode = i_mode;
        f_mode = (f_mode >> 9) << 9;
        f_mode = f_mode | strtol(e_mode, NULL, 8);
    }

    return f_mode;
}

mode_t change_mode(char *actual_path)
{
    //Check if the path is valid and get its mode
    struct stat f_stat;
    if (stat(actual_path, &f_stat) != 0)
    {
        error(0, errno, "cannot access '%s'", actual_path);
        return 0;
    }
    mode_t i_mode = f_stat.st_mode;
    mode_t f_mode = get_mode(i_mode);

    //Change the FILE/DIR mode
    chmod(actual_path, f_mode); //TODO: Check for errors?

    //Confirm the new FILE/DIR mode
    if (stat(actual_path, &f_stat) != 0)
    {
        error(0, errno, "cannot access '%s'", actual_path);
        return 0;
    }

    /** //FOR TESTING TIME LOGGING
    char * test = NULL;
    size_t n = 0;
    getline(&test, &n, stdin);
    */

    //Log FILE_MODF
    if (log)
    {
        char *log_message;
        asprintf(&log_message, "%s : %o : %o", realpath(actual_path, NULL), i_mode & MODE_MASK, f_stat.st_mode & MODE_MASK);
        write_log("FILE_MODF", log_message);
        free(log_message);
    }

    //Print verbosity messages accordingly
    if (f_stat.st_mode == i_mode)
    {
        if (verbosity == ALL)
        {
            char *mode_str = NULL;
            if (get_mode_string(i_mode, &mode_str) != OK)
            {
                exit(1);
            }
            printf("mode of '%s' retained as %o (%s)\n", actual_path, i_mode & MODE_MASK, mode_str);
            free(mode_str);
        }
    }
    else if (verbosity != OFF)
    {
        char *i_mode_str;
        char *f_mode_str;
        if (get_mode_string(i_mode, &i_mode_str) != OK)
        {
            exit(1);
        }
        if (get_mode_string(f_stat.st_mode, &f_mode_str) != OK)
        {
            exit(1);
        }
        printf("mode of '%s' changed from %o (%s) to %o (%s)\n", actual_path, i_mode & MODE_MASK, i_mode_str, f_stat.st_mode & MODE_MASK, f_mode_str);
        free(i_mode_str);
        free(f_mode_str);
    }
    return f_stat.st_mode;
}

int main(int argc, char *argv[], char *envp[])
{
    /*char * test = NULL;
    size_t n = 0;
    getline(&test, &n, stdin);*/

    //time_t start = time(0);
    start = clock();

    signal(SIGINT, sig_handler);

    if (argc <= 1)
    {
        print_usage();
    }

    //double time_spent = 0.0;
    unsigned arg;
    char *log_filename;

    //Parse command line options ('-v', '-c' or '-R')
    for (arg = 1; arg < argc && argv[arg][0] == '-'; arg++)
    {
        if (argv[arg][1] == '\0' || argv[arg][2] != '\0')
        {
            error(0, EINVAL,"unable to read options");
            log_exit(EINVAL);
        }

        switch (argv[arg][1])
        {
        case 'v':
            verbosity = ALL;
            break;

        case 'c':
            verbosity = CHANGES;
            break;

        case 'R':
            recursive = true;
            break;

        default:
            error(0, EINVAL,"unable to read options");
            log_exit(EINVAL);
            break;
        }
    }

    /* TODO: Delete
    switch (verbosity)
    {
    case OFF:
        printf("No verbosity\n");
        break;

    case CHANGES:
        printf("Show changes only\n");
        break;

    case ALL:
        printf("Full verbosity!!!\n");
    }

    if (recursive)
    {
        printf("Recursive mode chosen\n");
    }
    else
    {
        printf("Non-recursive mode\n");
    } */

    if (arg != argc - 2)
    {
        error(0, EINVAL, "incorrect number of arguments");
        log_exit(EINVAL);
    }

    //Validate expected mode input (either <u|g|o|a><-|+|=><rwx> or 0<0-7><0-7><0-7>)
    e_mode = argv[arg];
    switch (e_mode[0])
    {
    //MODE
    case 'u':
    case 'g':
    case 'o':
    case 'a':
        mode = ALPHA_MODE;
        //NOT VALID
        if (e_mode[1] == '\0' || (e_mode[1] != '-' && e_mode[1] != '+' && e_mode[1] != '=') ||
            e_mode[2] == '\0' || (e_mode[2] != 'r' && e_mode[2] != 'w' && e_mode[2] != 'x') ||
            (e_mode[3] != '\0' && ((e_mode[3] != 'r' && e_mode[3] != 'w' && e_mode[3] != 'x') ||
                                   e_mode[3] == e_mode[2])) ||
            (e_mode[3] != '\0' && e_mode[4] != '\0' && ((e_mode[4] != 'r' && e_mode[4] != 'w' && e_mode[4] != 'x') || e_mode[4] == e_mode[2] || e_mode[4] == e_mode[3])) ||
            (e_mode[3] != '\0' && e_mode[4] != '\0' && e_mode[5] != '\0'))
        {
            error(0, EINVAL,"unable to read mode");
            log_exit(EINVAL);
        }
        break;

    //OCTAL-MODE
    case '0':
        mode = OCTAL_MODE;
        if (e_mode[1] == '\0' || e_mode[2] == '\0' || e_mode[3] == '\0' || e_mode[4] != '\0' ||
            e_mode[1] < '0' || e_mode[1] > '7' || e_mode[2] < '0' || e_mode[2] > '7' || e_mode[3] < '0' || e_mode[3] > '7')
        {
            error(0, EINVAL,"unable to read mode");
            log_exit(EINVAL);
        }
        break;

    default:
        error(0, EINVAL,"unable to read mode");
        log_exit(EINVAL);
        break;
    }

    path = argv[arg + 1]; //Set inputted path

    //Check for a logging file. If there is one, open it.
    log_filename = getenv("LOG_FILENAME");
    if (log_filename == NULL)
    {
        log = false;
    }
    else
    {
        int flags = O_WRONLY | O_CREAT | O_APPEND;
        printf("pid: %d, gid: %d\n", getpid(), getegid());
        if (getpid() == getgid())
        {
            flags |= O_TRUNC;
        }
        log_file = open(log_filename, flags, S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH);
        if (log_file == -1)
        {
            error(0, errno, "unable to open log file '%s'", log_filename);
        }
        else
        {
            log = true;
        }
    }

    mode_t mode = change_mode(path);

    if (recursive && S_ISDIR(mode))
    {
        DIR *dir;
        struct dirent *dir_entry;
        if ((dir = opendir(path)) == NULL)
        {
            error(0, errno, "cannot open directory %s", path);
        }
        else
        {
            pid_t child_pid = 0;
            while ((dir_entry = readdir(dir)) != NULL)
            {
                char *actual_path;
                if (path[strlen(path) - 1] == '/')
                {
                    asprintf(&actual_path, "%s%s", path, dir_entry->d_name);
                }
                else
                {
                    asprintf(&actual_path, "%s/%s", path, dir_entry->d_name);
                }
                if (stat(actual_path, &f_stat) != 0)
                {
                    error(0, errno, "cannot access '%s'", path);
                    free(actual_path);
                    continue;
                }
                if (S_ISLNK(f_stat.st_mode))
                {
                    printf("neither symbolic link '%s' nor referent has been changed\n", actual_path);
                }
                else if (S_ISREG(f_stat.st_mode))
                {
                    change_mode(actual_path);
                }
                else if (S_ISDIR(f_stat.st_mode) && strcmp(dir_entry->d_name, "..") && strcmp(dir_entry->d_name, "."))
                {
                    child_pid = fork();

                    if (child_pid == -1)
                    {
                        error(0, errno, "unable to create child process (aborting)");
                        log_exit(errno);
                    }
                    else if (child_pid == 0)
                    {
                        char **arguments;
                        arguments = malloc(sizeof(char *) * (argc + 1));
                        memcpy(arguments, argv, (argc - 1) * sizeof(char *));
                        arguments[argc - 1] = actual_path;
                        arguments[argc] = NULL;
                        execv(argv[0], arguments);
                        free(arguments);
                        free(actual_path);
                        exit(0);
                    }
                    else if (log)
                    {
                        int arglen = 0;
                        for (int i = 0; i < argc; i++)
                        {
                            arglen += strlen(argv[i]);
                        }

                        char *log_message = malloc((arglen + argc) * sizeof(char));

                        for (int i = 0; i < argc; i++)
                        {
                            strcat(log_message, argv[i]);
                            if (i != argc - 1)
                                strcat(log_message, " ");
                        }
                        write_log("PROC_CREAT", log_message);
                        free(log_message);
                    }
                }
                free(actual_path);
            }
            if (child_pid != 0)
            {
                int wstatus;
                while (wait(&wstatus) > 0)
                    ;
            }
        }
    }

    //Time End
    //time_t end = time(0);
    //clock_t end = clock();
    //time_spent = difftime(end, start) * 1000;
    //time_spent = ((double)(end - start) / CLOCKS_PER_SEC) * 1000;
    //printf("\nEND Execution Time: %.2f ms \n", time_spent);

    if (log)
        close(log_file);
}
