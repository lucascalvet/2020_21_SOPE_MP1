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

mode_t change_mode()
{
    //Check if the path is valid and get its mode
    struct stat f_stat;
    if (stat(path, &f_stat) != 0)
    {
        error(0, errno, "cannot access '%s'", path);
        return 0;
    }
    mode_t i_mode = f_stat.st_mode;
    mode_t f_mode = get_mode(i_mode);

    //Change the FILE/DIR mode
    chmod(path, f_mode); //TODO: Check for errors?

    //Confirm the new FILE/DIR mode
    if (stat(path, &f_stat) != 0)
    {
        error(0, errno, "cannot access '%s'", path);
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
        asprintf(&log_message, "%s : %o : %o", realpath(path, NULL), i_mode & MODE_MASK, f_stat.st_mode & MODE_MASK);
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
            printf("mode of '%s' retained as %o (%s)\n", path, i_mode & MODE_MASK, mode_str);
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
        printf("mode of '%s' changed from %o (%s) to %o (%s)\n", path, i_mode & MODE_MASK, i_mode_str, f_stat.st_mode & MODE_MASK, f_mode_str);
        free(i_mode_str);
        free(f_mode_str);
    }
    return f_stat.st_mode;
}

void change_mode_handler()
{
    mode_t mode = change_mode(NULL);

    if (recursive && S_ISDIR(mode))
    {
        DIR *dir;
        struct dirent *dir_entry;
        if ((dir = opendir(path)) == NULL)
        {
            error(0, errno, "cannot read directory %s", path);
            return;
        }
        chdir(path);
        pid_t child_pid;
        while ((dir_entry = readdir(dir)) != NULL)
        {
            path = dir_entry->d_name;
            stat(dir_entry->d_name, &f_stat); //TODO: Check for errors as already done above
            if (S_ISREG(f_stat.st_mode) && !S_ISLNK(f_stat.st_mode))
            {
                change_mode(dir_entry->d_name);
                //TODO: Log FILE_MODF
            }
            if (S_ISDIR(f_stat.st_mode) && strcmp(dir_entry->d_name, "..") && strcmp(dir_entry->d_name, "."))
            {
                child_pid = fork(); //TODO: Log PROC_CREAT

                if (child_pid == 0)
                {
                    change_mode_handler();
                    break;
                }
            }
        }
        if (child_pid != 0)
        {
            int wstatus;
            while (wait(&wstatus) > 0)
                ;
        }
    }
}

int main(int argc, char *argv[], char *envp[])
{
    //time_t start = time(0);
    start = clock();

    signal(SIGINT, sig_handler);

    if (argc <= 1)
    {
        print_usage();
    }

    double time_spent = 0.0;
    unsigned arg;
    char *log_filename;

    //Parse command line options ('-v', '-c' or '-R')
    for (arg = 1; arg < argc && argv[arg][0] == '-'; arg++)
    {
        if (argv[arg][1] == '\0' || argv[arg][2] != '\0')
        {
            print_error(EINVAL);
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
            print_error(EINVAL);
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

    if (arg == argc || arg == argc - 1)
    {
        print_error(EINVAL);
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
            print_error(EINVAL);
        }
        break;

    //OCTAL-MODE
    case '0':
        mode = OCTAL_MODE;
        if (e_mode[1] == '\0' || e_mode[2] == '\0' || e_mode[3] == '\0' || e_mode[4] != '\0' ||
            e_mode[1] < '0' || e_mode[1] > '7' || e_mode[2] < '0' || e_mode[2] > '7' || e_mode[3] < '0' || e_mode[3] > '7')
        {
            print_error(EINVAL);
        }
        break;

    default:
        print_error(EINVAL);
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
        log_file = open(log_filename, O_WRONLY | O_CREAT | O_TRUNC | O_APPEND, S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH);
        log = true;
    }

    change_mode_handler();

    //Time End
    //time_t end = time(0);
    clock_t end = clock();
    //time_spent = difftime(end, start) * 1000;
    time_spent = ((double)(end - start) / CLOCKS_PER_SEC) * 1000;
    //printf("\nEND Execution Time: %.2f ms \n", time_spent);
    close(log_file);
}
