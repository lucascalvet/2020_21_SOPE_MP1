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

static char *path = "";
static enum Mode mode;
static char *e_mode;
static int nftot = 0;
static int nfmod = 0;
static int log_file = 0;
static bool log = false;
static clock_t base_time = 0;
static enum Verbosity verbosity = OFF;
static bool recursive = false;
static struct stat f_stat;

typedef void sigfunc(int);
sigfunc *signal(int signo, sigfunc *func);

void write_log(char *event, char *info)
{
    clock_t instant = clock() + base_time;
    unsigned log_time = (unsigned)((instant * 1000 / CLOCKS_PER_SEC));
    pid_t pid = getpid();
    char *log_message;
    if (asprintf(&log_message, "%u ; %d ; %s ; %s\n", log_time, pid, event, info) == -1)
    {
        exit(EXIT_FAILURE);
    }
    write(log_file, log_message, strlen(log_message));
    free(log_message);
}

void log_exit(int exit_n)
{
    if (log)
    {
        char *log_message;
        if (asprintf(&log_message, "%d", exit_n) == -1)
        {
            exit(EXIT_FAILURE);
        }
        write_log("PROC_EXIT", log_message);
        free(log_message);
        close(log_file);
    }
    exit(exit_n);
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

    log_exit(EXIT_SUCCESS);
}

void sig_handler(int r_signal)
{
    if (r_signal == SIGINT)
    {
        if (log)
            write_log("SIGNAL_RECV", "SIGINT");

        killpg(getpgrp(), SIGUSR1);

        if (log)
        {
            char *sigusr1_msg;
            if (asprintf(&sigusr1_msg, "SIGUSR1 : %d", getpgrp()) == -1)
            {
                exit(EXIT_FAILURE);
            }
            write_log("SIGNAL_SENT", sigusr1_msg);
            free(sigusr1_msg);
        }

        char *buffer = NULL;
        bool answer = false;
        size_t n = 0;
        usleep(100000);
        do
        {
            printf("Are you sure you want to terminate the program? (Y/N)\n");

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
            killpg(getpgrp(), SIGUSR2); // Kill parent and all children
            if (log)
            {
                char *sigusr2_msg;
                if (asprintf(&sigusr2_msg, "SIGUSR2 : %d", getpgrp()) == -1)
                {
                    exit(EXIT_SUCCESS);
                }
                write_log("SIGNAL_SENT", sigusr2_msg);
                free(sigusr2_msg);
            }
        }
        else
        {
            killpg(getpgrp(), SIGCONT);
        }
    }
    else if (r_signal == SIGUSR1)
    {
        if (log)
            write_log("SIGNAL_RECV", "SIGUSR1");
        pid_t pid = getpid();
        char *real_path = realpath(path, NULL);
        printf("%d ; %s ; %d ; %d\n", pid, real_path, nftot, nfmod);
        free(real_path);
        if (getpid() != getpgrp())
            raise(SIGTSTP);
    }
    else if (r_signal == SIGUSR2)
    {
        if (log)
            write_log("SIGNAL_RECV", "SIGUSR2");
        log_exit(EXIT_SUCCESS);
    }
}

int get_mode_string(mode_t mode, char **result)
{
    *result = malloc(10 * sizeof(char));
    if (*result == NULL)
        return -1;
    mode = mode & MODE_MASK;
    snprintf(*result, 10 * sizeof(char), "rwxrwxrwx");
    for (int i = 0; i < 9; i++)
    {
        if (!(mode & BIT(8 - i)))
            (*result)[i] = '-';
    }

    return OK;
}

mode_t get_mode(mode_t i_mode)
{
    mode_t f_mode = 0;
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
    nftot++;
    //Check if FILE/DIR is valid and get its mode
    struct stat f_stat;
    if (stat(actual_path, &f_stat) != OK)
    {
        error(0, errno, "cannot access '%s'", actual_path);
        return 0;
    }
    mode_t i_mode = f_stat.st_mode;
    mode_t f_mode = get_mode(i_mode);

    //Change the FILE/DIR mode
    if (chmod(actual_path, f_mode) != OK)
    {
        error(0, errno, "changing permissions of '%s'", actual_path);
        return 0;
    }

    //Confirm the new FILE/DIR mode
    if (stat(actual_path, &f_stat) != OK)
    {
        error(0, errno, "cannot access '%s'", actual_path);
        return 0;
    }

    //Log FILE_MODF
    if (log)
    {
        char *log_message;
        char *real_path = realpath(actual_path, NULL);
        if (asprintf(&log_message, "%s : %o : %o", real_path, i_mode & MODE_MASK, f_stat.st_mode & MODE_MASK) == -1)
        {
            exit(EXIT_FAILURE);
        }
        write_log("FILE_MODF", log_message);
        free(real_path);
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
                log_exit(EXIT_FAILURE);
            }
            printf("mode of '%s' retained as %04o (%s)\n", actual_path, i_mode & MODE_MASK, mode_str);
            free(mode_str);
        }
    }
    else
    {
        nfmod++;
        if (verbosity != OFF)
        {
            char *i_mode_str;
            char *f_mode_str;
            if (get_mode_string(i_mode, &i_mode_str) != OK)
            {
                log_exit(EXIT_FAILURE);
            }
            if (get_mode_string(f_stat.st_mode, &f_mode_str) != OK)
            {
                log_exit(EXIT_FAILURE);
            }
            printf("mode of '%s' changed from %04o (%s) to %04o (%s)\n", actual_path, i_mode & MODE_MASK, i_mode_str, f_stat.st_mode & MODE_MASK, f_mode_str);
            free(i_mode_str);
            free(f_mode_str);
        }
    }
    return f_stat.st_mode;
}

int main(int argc, char *argv[], char *envp[])
{
    if (getpid() == getpgrp())
        signal(SIGINT, sig_handler);
    signal(SIGUSR1, sig_handler);
    signal(SIGUSR2, sig_handler);

    char *log_filename;
    char *base_time_text;

    //Check for a logging file. If there is one, open it.
    base_time_text = getenv("BASE_TIME");
    if (base_time_text == NULL)
    {
        base_time = 0;
    }
    else
    {
        sscanf(base_time_text, "%ld", &base_time);
    }

    log_filename = getenv("LOG_FILENAME");
    if (log_filename == NULL)
    {
        log = false;
    }
    else
    {
        int flags = O_WRONLY | O_CREAT | O_APPEND;
        if (getpid() == getpgrp())
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
    if (log)
    {
        int arglen = 0;
        for (int i = 0; i < argc; i++)
        {
            arglen += strlen(argv[i]);
        }

        char *log_message = malloc((arglen + argc) * sizeof(char));
        char *build_message = malloc((arglen + argc) * sizeof(char));
        if (log_message == NULL || build_message == NULL)
        {
            log_exit(EXIT_FAILURE);
        }
        log_message[0] = '\0';

        for (int i = 0; i < argc; i++)
        {
            snprintf(build_message, arglen + argc, "%s", log_message);
            snprintf(log_message, arglen + argc, "%s%s", build_message, argv[i]);
            if (i != argc - 1)
            {
                snprintf(build_message, arglen + argc, "%s", log_message);
                snprintf(log_message, arglen + argc, "%s ", build_message);
            }
        }
        write_log("PROC_CREAT", log_message);
        free(log_message);
        free(build_message);
    }

    if (argc <= 1)
    {
        print_usage();
    }

    unsigned arg;
    //Parse command line options ('-v', '-c' or '-R')
    for (arg = 1; arg < argc && argv[arg][0] == '-'; arg++)
    {
        if (argv[arg][1] == '\0' || argv[arg][2] != '\0')
        {
            error(0, EINVAL, "unable to read options");
            log_exit(EXIT_FAILURE);
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
            error(0, EINVAL, "unable to read options");
            log_exit(EXIT_FAILURE);
            break;
        }
    }

    if (arg != argc - 2)
    {
        error(0, EINVAL, "incorrect number of arguments");
        log_exit(EXIT_FAILURE);
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
            error(0, EINVAL, "unable to read mode");
            log_exit(EXIT_FAILURE);
        }
        break;

    //OCTAL-MODE
    case '0':
        mode = OCTAL_MODE;
        if (e_mode[1] == '\0' || e_mode[2] == '\0' || e_mode[3] == '\0' || e_mode[4] != '\0' ||
            e_mode[1] < '0' || e_mode[1] > '7' || e_mode[2] < '0' || e_mode[2] > '7' || e_mode[3] < '0' || e_mode[3] > '7')
        {
            error(0, EINVAL, "unable to read mode");
            log_exit(EXIT_FAILURE);
        }
        break;

    default:
        error(0, EINVAL, "unable to read mode");
        log_exit(EXIT_FAILURE);
        break;
    }

    path = argv[arg + 1]; //Set inputted path
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
                    if (asprintf(&actual_path, "%s%s", path, dir_entry->d_name) == -1)
                    {
                        exit(EXIT_FAILURE);
                    }
                }
                else
                {
                    if (asprintf(&actual_path, "%s/%s", path, dir_entry->d_name) == -1)
                    {
                        exit(EXIT_FAILURE);
                    }
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
                    clock_t total_time = base_time + clock();
                    child_pid = fork();

                    if (child_pid == -1)
                    {
                        error(0, errno, "unable to create child process (aborting)");
                        log_exit(EXIT_FAILURE);
                    }
                    else if (child_pid == 0)
                    {
                        char **arguments;
                        arguments = malloc(sizeof(char *) * (argc + 1));
                        if (arguments == NULL)
                        {
                            log_exit(EXIT_FAILURE);
                        }
                        memcpy(arguments, argv, (argc - 1) * sizeof(char *));
                        arguments[argc - 1] = actual_path;
                        arguments[argc] = NULL;
                        char *envs[3];
                        char *base_time_str;
                        char *env_log_filename;
                        total_time = total_time + clock();
                        if (asprintf(&base_time_str, "BASE_TIME=%ld", total_time) == -1)
                        {
                            exit(EXIT_FAILURE);
                        }
                        envs[0] = base_time_str;
                        envs[1] = NULL;
                        envs[2] = NULL;
                        if (log)
                        {
                            if (asprintf(&env_log_filename, "LOG_FILENAME=%s", log_filename) == -1)
                            {
                                exit(EXIT_FAILURE);
                            }
                            envs[1] = env_log_filename;
                        }

                        execvpe(argv[0], arguments, envs);
                        free(arguments);
                        free(base_time_str);
                        free(env_log_filename);
                        free(actual_path);
                        log_exit(EXIT_SUCCESS);
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

    if (log)
    {
        log_exit(EXIT_SUCCESS);
    }
}
