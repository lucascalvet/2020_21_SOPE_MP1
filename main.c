#include <stdlib.h>
#include <stdio.h>
#include <stdbool.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <time.h>

#define SHOW_INFO
#define OK 0
#define BIT(N) (1 << N)

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

//time_t t = time(NULL);
static clock_t start = clock();

void print_usage(int error)
{
    char *st_error;
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
    if (error != 0)
        printf("\n %s \n", st_error);
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

void write_log(char *event, char *info, int log_file)
{
    write(log_file, sprintf("%s ; %s", event, info));
}

int main(int argc, char *argv[], char *envp[])
{

    if (argc <= 1)
    {
        print_usage(22);
        exit(0);
    }

    double time_spent = 0.0;
    enum Verbosity verbosity = OFF; // TODO: Change to static global?
    enum Mode mode;
    bool recursive = false;
    bool log;
    char *path;
    char *e_mode;
    struct stat f_stat;
    mode_t i_mode;
    mode_t f_mode;
    unsigned arg;

    char *log_filename = getenv("LOG_FILENAME");
    int log_file;
    if (log_filename == NULL)
    {
        log = false;
        printf("Not logging\n");
    }
    else
    {
        log = true;
        printf("Logging!\n")
        log_file = open(log_filename, O_WRONLY | O_CREAT | O_TRUNC);
    }

    for (arg = 1; arg < argc && argv[arg][0] == '-'; arg++)
    {
        if (argv[arg][1] == '\0' || argv[arg][2] != '\0')
        {
            print_usage(22);
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
            print_usage(22);
            break;
        }
    }

#ifdef SHOW_INFO
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
    }
#endif

    e_mode = argv[arg];

    if (arg == argc || arg == argc - 1)
    {
        print_usage(22);
    }

    switch (e_mode[0])
    {
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
            print_usage(22);
        }

        break;

    case '0':
        mode = OCTAL_MODE;
        if (e_mode[1] == '\0' || e_mode[2] == '\0' || e_mode[3] == '\0' || e_mode[4] != '\0' ||
            e_mode[1] < '0' || e_mode[1] > '7' || e_mode[2] < '0' || e_mode[2] > '7' || e_mode[3] < '0' || e_mode[3] > '7')
        {
            print_usage(22);
        }
        break;

    default:
        print_usage(22);
        break;
    }

    path = argv[arg + 1];

    if (stat(path, &f_stat) != 0)
    {
        if (errno == ENOENT)
        {
            printf("The file %s does not exist!\n", path);
            print_usage(ENOENT);
        }
    };

    i_mode = f_stat.st_mode;
    printf("Mode bits: %x \n", i_mode);

    //Beginning to handle directory

    if (mode == ALPHA_MODE)
    {
        printf("Using alpha mode!\n");
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

        printf("Perm_mask: %x\n", perms);

        mode_t users = 0;

        if (e_mode[0] == 'u' || e_mode[0] == 'a')
            users = users | BIT(6) | BIT(7) | BIT(8);
        if (e_mode[0] == 'g' || e_mode[0] == 'a')
            users = users | BIT(3) | BIT(4) | BIT(5);
        if (e_mode[0] == 'o' || e_mode[0] == 'a')
            users = users | BIT(0) | BIT(1) | BIT(2);

        printf("User_mask: %x\n", users);

        if (e_mode[1] == '-')
            f_mode = i_mode & ~(perms & users);
        if (e_mode[1] == '+')
            f_mode = i_mode | (perms & users);
        if (e_mode[1] == '=')
            f_mode = (i_mode & ~users) | (perms & users);

        printf("Final mode: %x\n", f_mode);
    }

    else if (mode == OCTAL_MODE)
    {
        printf("Using octal mode!\n");
        f_mode = i_mode;
        f_mode = (f_mode >> 9) << 9;
        printf("Striped mode: %x\n", f_mode);
        f_mode = f_mode | strtol(e_mode, NULL, 8);
        printf("Final mode: %x\n", f_mode);
    }

    chmod(path, f_mode);

    if (log)
    {
        write_log("FILE_MODF", sprintf("%s : %o : %o",realpath(path), i_mode, f_mode), log_file);
    }

    //Time End
    clock_t end = clock();
    time_spent = ((double)(end - start) / CLOCKS_PER_SEC) * 1000;
    printf("\nEND Tempo de execucao do modulo: %.2f ms \n", time_spent);
}
