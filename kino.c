/*** includes ***/
#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <termios.h>
#include <unistd.h>

/***defines***/

#define CTRL_KEY(k) ((k) & 0x1f)




/*** data ***/
struct termios orig_termios;

/*** terminal ***/
void die(const char *s) {
    perror(s);
    exit(1);
}

void disableRawMode() {
    if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &orig_termios) == -1)
        die("tcsetattr");
}

void enableRawMode() {

    //Get Terminal State
    if (tcgetattr(STDIN_FILENO, &orig_termios) == -1)
        die("tcgetattr");

    atexit(disableRawMode);
    
    struct termios raw = orig_termios;

    //Turn off Output processing ( carriage return and newline \r\n )
    //since this disable carriage return, we shall add it manually to printf
    raw.c_oflag &= ~(OPOST);

    //CS8 is a bitmask it sets character size to 8 bits per byte ( default in almost all systems )
    raw.c_cflag |= (CS8);

    //legacy flags: BRKINT ( break condition 'ctrl-c' ), INPCK ( parity checking), ISTRIP ( strip 8th bit)
    
    //Turn off flags with bitwise AND operator (we turn all the bits except the desired flag to 1 using ~)
    //flags turned off: canoncial mode, echo, ctrl c/z (stop and suspend signal), ctrl s/q (software flow)
    // , ctrl-v , fix ctr-M not working (ICRNL flag) 
    raw.c_iflag &= ~(BRKINT | INPCK | ISTRIP | ICRNL | IXON);
    raw.c_lflag &= ~(ECHO | ICANON | IEXTEN | ISIG);

    //set minimum byte needed for read() to return and maximum timeout time
    raw.c_cc[VMIN] = 0;
    raw.c_cc[VTIME] = 1;

    if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw) == -1)
        die("tcsetattr");
}


/*** init ***/

int main() {
    enableRawMode();
    while (1) {
        char c = '\0';

        //cygwin return -1 with and errno of EAGAIN when read() times out
        //so we ignore it

        if (read(STDIN_FILENO, &c, 1) == -1 && errno != EAGAIN)die("read");

        if (iscntrl(c)) {
            printf("%d\r\n", c);
        } else {
            printf("%d ('%c')\r\n", c, c);
        }
        if (c == CTRL_KEY('q')) break;
    }


    return 0;
}