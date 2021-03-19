# XMOD
### _A tool used to change access permission modes of files and directories._

## Compile
       make xmod

## Run

       Usage: xmod [OPTIONS] MODE FILE/DIR
              xmod [OPTIONS] OCTAL-MODE FILE/DIR

       OPTIONS:
       -v    output a diagnostic for every file processed
       -c    like verbose but report only when a change is made
       -R    change files and directories recursively

       MODE: <u|g|o|a><-|+|=><rwx>

       OCTAL-MODE: 0<0-7><0-7><0-7>


### Regarding mode:

`<u|g|o|a>` : refers to the user in question.
- `u` refers to the owner of the file
- `g` refers to the group to which the user belongs
- `o` refers to all the remaining users (others)
- `a` all of the above

`<-|+|=>` : the permissions that follow will:
- `-` be removed
- `+` be granted
- `=` replace the ones that already exist, removing the ones that aren't mentioned

`<rwx>` : indicates which permissions should be modified
- `r` read
- `w` write
- `x` execution


### Regarding octal-mode:

A sequence of 4 digits starting with '0'. Each following digit is associated to each type of user in the order u, g, o, and 
in a binary interpretation of the positions, r, w, x correspond to a bit (1 if the permission is present, 0 otherwise).
For example 0755, only the owner of the file has writing permission, all others have reading and execution permission.


## Notes

- If compiling without the Makefile, make sure to add `-D_GNU_SOURCE` (needed for the use of _`asprintf`_ and _`execvpe`_ functions).

- Files or directories containing spaces must be enclosed in quotes or escaped with a backslash for each blank space.

- For error handling, we use the _`error()`_ function to print to stderr a description of the error.

- The signal `SIGUSR1` is used for all created processes to print their information and `SIGUSR2` is used for all child processes to terminate.

- For testing with the shell script, run `./testMP1.sh ./xmod dummy_dir teste.txt` .


## Authorship / Participation

| Name                     | Email                      | Participation  |
|:-------------------------|:---------------------------|:--------------:|
| José Frederico Rodrigues | <up201807626@edu.fe.up.pt> | 33%            |
| José Pedro Ferreira      | <up201904515@fe.up.pt>     | 33%            |
| Lucas Calvet Santos      | <up201904517@fe.up.pt>     | 34%            |
