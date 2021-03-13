#include <stdlib.h>
#include <stdio.h>
#include <stdbool.h>
#include <sys/stat.h>
#include <sys/types.h>

#define SHOW_INFO

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

void print_usage()
{
    printf("Invalid arguments!\n"
           "\n"
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

int main(int argc, char *argv[], char *envp[])
{
    if (argc <= 1)
    {
        print_usage();
        exit(0);
    }

    enum Verbosity verbosity = OFF; // TODO: Change to static global?
    enum Mode mode;
    bool recursive = false;
    char* path;

    unsigned arg;

    for (arg = 1; arg < argc && argv[arg][0] == '-'; arg++)
    {
        if (argv[arg][1] == '\0' || argv[arg][2] != '\0')
        {
            print_usage();
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
            print_usage();
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

    if (arg == argc || arg == argc - 1)
    {
        print_usage();
    }

    switch (argv[arg][0])
    {
    case 'u':
    case 'g':
    case 'o':
    case 'a':
        mode = ALPHA_MODE;
        //NOT VALID
        if (argv[arg][1] == '\0' || (argv[arg][1] != '-' && argv[arg][1] != '+' && argv[arg][1] != '=') ||
            argv[arg][2] == '\0' || (argv[arg][2] != 'r' && argv[arg][2] != 'w' && argv[arg][2] != 'x') ||
            (argv[arg][3] != '\0' && ((argv[arg][3] != 'r' && argv[arg][3] != 'w' && argv[arg][3] != 'x') ||
             argv[arg][3] == argv[arg][2])) ||
            (argv[arg][3] != '\0' && argv[arg][4] != '\0' && ((argv[arg][4] != 'r' && argv[arg][4] != 'w' && argv[arg][4] != 'x') ||
             argv[arg][4] == argv[arg][2] || argv[arg][4] == argv[arg][3])) ||
            (argv[arg][3] != '\0' && argv[arg][4] != '\0' && argv[arg][5] != '\0'))
        {
            print_usage();
        }
        break;

    case '0':
        mode = OCTAL_MODE;
        if (argv[arg][1] == '\0' || argv[arg][2] == '\0' || argv[arg][3] == '\0' || argv[arg][4] != '\0' ||
            argv[arg][1] < '0' || argv[arg][1] > '7' || argv[arg][2] < '0' || argv[arg][2] > '7' || argv[arg][3] < '0' || argv[arg][3] > '7')
        {
            print_usage();
        }
        break;

    default:
        print_usage();
        break;
    }

    if (mode == ALPHA_MODE)
    {
        printf ("Using alpha mode!\n"); 
    }
    
    else if (mode == OCTAL_MODE)
    {
        printf ("Using octal mode!\n");
    }
}