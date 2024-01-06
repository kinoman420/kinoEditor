A Very Simple Text editor written in C

it is written in a single file. uses bitwise operators and VT-100 for logic.

quit = CTRL + Q
save = CTRL + S

How to run:

1) on windows:

    >install cygwin. put "kino.c" file in cygwin home directory />
 then:
    >run "cc kino.c -o kino" />
then:
    >run "./kino" />

2) this program should work on linux and WSL since it uses POSIX and termios Header file which are supported natively on UNIX, but this is not tested
