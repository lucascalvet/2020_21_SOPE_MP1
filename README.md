# XMOD
## _A tool used to change access permissions of files and directories._


## Compile
```sh
make xmod
```
## Run
```sh
./xmod [OPTIONS] MODE FILE/DIR
./xmod [OPTIONS] OCTAL-MODE FILE/DIR
```
## How to use

           Usage: xmod [OPTIONS] MODE FILE/DIR\n
                  xmod [OPTIONS] OCTAL-MODE FILE/DIR\n
           
           OPTIONS:
            -v    output a diagnostic for every file processed
            -c    like verbose but report only when a change is made
            -R    change files and directories recursively
           
           MODE: <u|g|o|a><-|+|=><rwx>\n
           
           OCTAL-MODE: 0<0-7><0-7><0-7>\n;
Regarding mode:
<u|g|o|a>: refers to the user in question.
- <u> refers to the owner of the file
- <g> refers to the group which the user belongs to
- <o> refers to all the remaining users
- <a> all of the above

<-|+|=>: The permissions that follow will 
- <-> be removed
- <+> be granted
- <=> replace the ones that already exist, removing the ones that aren't mentioned

< rwx >: indicates which permissions should be modified
- <r> read
- <w> write
- <x> execution

Regarding octal-mode:
A sequence of 4 digits starting with '0'. Each following digit is associated to each type of user in the order u, g, o, and 
in a binary interpretation of the positions, r, w, x correspond to a bit (1 if the permission is present, 0 otherwise).
For example 0755, only the owner of the file has writing permission.
