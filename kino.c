/*** includes ***/
#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <termios.h>
#include <unistd.h>

/***defines***/

#define CTRL_KEY(k) ((k) & 0x1f)
#define KINO_VERSION "0.0.1"





/*** data ***/

struct editorConfig {
    int cx, cy;
    int screenrows;
    int screencols;
    struct termios orig_termios;

};

struct editorConfig E;

/*** terminal ***/

void die(const char *s) {

    write(STDOUT_FILENO, "\x1b[2J", 4);
    write(STDOUT_FILENO, "\x1b[H", 3);

    perror(s);
    exit(1);
}

void disableRawMode() {
    if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &E.orig_termios) == -1)
        die("tcsetattr");
}

void enableRawMode() {

    //Get Terminal State
    if (tcgetattr(STDIN_FILENO, &E.orig_termios) == -1)
        die("tcgetattr");

    atexit(disableRawMode);
    
    struct termios raw = E.orig_termios;

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


//simplified main, this function waits for keypress and returns it
char editorReadKey() {
    int nread;
    char c;
    while (( nread = read(STDIN_FILENO, &c, 1)) != 1) {
        if (nread == -1 && errno != EAGAIN) die("read");
    }
    return c;
}


int getCursorPosition(int *rows, int *cols) {

    char buf[32];
    unsigned int i = 0;

    // query terminal status using n command...

    if (write(STDOUT_FILENO, "\x1b[6n", 4) != 4) return -1;
        

    while (i < sizeof(buf) - 1) {
        if( read(STDIN_FILENO, &buf[i], 1) != 1) break;
        if (buf[i == 'R']) break;
        i++;
    }
    buf[i] = '\0';

    if( buf[0] != '\x1b' || buf[1] != '[') return -1;
    if( sscanf(&buf[2], "%d;%d", rows, cols) != 2) return -1;

    return 0;
}

// get window size using ioctl(), uses a fallback method if it fails ( we move the cursor to bottom right)
// then use escape sequences to get the number of rows and cols
// we use C and B command with 999 argument, C moves the cursor to right and B moves it downward

int getWindowSize(int *rows, int *cols) {
    struct winsize ws;

    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == -1 || ws.ws_col == 0 ) {
        if (write(STDOUT_FILENO, "\x1b[999C\x1b[999B", 12) != 12)return -1;
        return getCursorPosition(rows, cols);
            
        
    } else {
        *cols = ws.ws_col;
        *rows = ws.ws_row;
        return 0;
    }
}

/*** append buffer ***/
struct abuf {
    char *b;
    int len;
};

#define ABUF_INIT {NULL, 0}

//append string to buffer
void abAppend(struct abuf *ab, const char *s, int len) {
    // use realloce to get a block of memory with size of current str + str
    char *new = realloc(ab->b,ab->len + len);

    if ( new == NULL) return;
    memcpy(&new[ab->len], s, len);
    ab->b = new;
    ab->len =+len;
}

void abFree(struct abuf *ab) {
    free(ab->b);
}


/*** output ***/


void editorDrawRows(struct abuf *ab) {
    int y;
    for ( y = 0; y < E.screenrows; y++) {
        if ( y == E.screenrows / 3) {
            char welcome[80];
            int welcomelen = snprintf(welcome, sizeof(welcome), "Kino editor -- version %s", KINO_VERSION);
            if(welcomelen > E.screencols) welcomelen = E.screencols;
            int padding = (E.screencols - welcomelen) / 2;
            if (padding) {
                abAppend(ab, "~", 1);
                padding--;
            }
            while (padding--) abAppend(ab, " ", 1);
            abAppend(ab, welcome, welcomelen);
        } else {
            abAppend(ab, "~", 1);
        }

        abAppend(ab, "\x1b[K", 3);
        if ( y < E.screenrows - 1) {
            abAppend(ab, "\r\n", 2);
        }
        

    }
}

void editorRefreshScreen(){

    struct abuf ab = ABUF_INIT;

    // we write escape character using this. the J command clears the screen and the argument is 2
    // H command is cursor reposition
    // l is reset mode,h is set mode, ?25 argument is a newer VT100 protocol
    // J is removed because we added [K in editorDrawRows
    // K removes part of current line. the argument specifies the behaviour

    abAppend(&ab, "\x1b[?25l", 6);
    
    abAppend(&ab, "\x1b[H", 3);

    editorDrawRows(&ab);

    char buf[32];
    snprintf(buf, sizeof(buf), "\x1b[%d;%dH", E.cy + 1, E.cx + 1);
    abAppend(&ab, buf, strlen(buf));

    abAppend(&ab, "\x1b[?25h", 6);

    write(STDOUT_FILENO, ab.b, ab.len);
    abFree(&ab);
}




/*** input ***/

void editorMoveCursor(char key) {
    switch(key) {
        case 'a':
            E.cx--;
            break;
        case 'd':
            E.cx++;
            break;
        case 'w':
            E.cy--;
            break;
        case 's':
            E.cy++;
            break;  
    }
}


//handles the input from editorReadkey
void editorProcessKeyPress() {
    char c = editorReadKey();

    switch (c) {
        case CTRL_KEY('q'):
        write(STDOUT_FILENO, "\x1b[2J", 4);
        write(STDOUT_FILENO, "\x1b[H", 3);
        exit(0);
        break;

        case 'w':
        case 's':
        case 'a':
        case 'd':
            editorMoveCursor(c);
            break;
    }
}


/*** init ***/


// initializes row and col sizes

void initEditor() {

    E.cx = 0;
    E.cy = 0;
    if (getWindowSize(&E.screenrows, &E.screencols) == -1)die("getWindowSize");
}

int main() {
    enableRawMode();
    initEditor();

    while(1) {
        editorRefreshScreen();
        editorProcessKeyPress();
    }



    return 0;
}