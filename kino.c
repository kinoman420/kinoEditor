/*** includes ***/

// multiplatform support
#define _DEFAULT_SOURCE
#define _BSD_SOURCE
#define _GNU_SOURCE


#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/ioctl.h>
#include <termios.h>
#include <unistd.h>

/***defines***/

#define CTRL_KEY(k) ((k) & 0x1f)
#define KINO_VERSION "0.0.1"
enum editorKey {
  ARROW_LEFT = 1000,
  ARROW_RIGHT,
  ARROW_UP,
  ARROW_DOWN,
  DEL_KEY,
  HOME_KEY,
  END_KEY,
  PAGE_UP,
  PAGE_DOWN

};





/*** data ***/


typedef struct erow {
    // stores a line of text as pointer

    int size;
    int rsize;
    char *chars;
    char *render;
    
}erow;

struct editorConfig {
    int cx, cy;
    int rowoff; // row offset
    int coloff; // col offset   
    int screenrows;
    int screencols;
    int numrows;
    erow *row;
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


// simplified main, this function waits for keypress and returns it
// if the read character is an escape sequence we read to more bytes into seq
// if it times then the user inputted esc.otherwise we check if the escape sequence is arrow key
// then return the corresponding w,a,s,d key

int editorReadKey() {
    int nread;
    char c;
    while (( nread = read(STDIN_FILENO, &c, 1)) != 1) {
        if (nread == -1 && errno != EAGAIN) die("read");
    }

    if (c == '\x1b') {
        char seq[3];

        if (read(STDIN_FILENO, &seq[0], 1) != 1) return '\x1b';
        if (read(STDIN_FILENO, &seq[1], 1) != 1) return '\x1b';

        if (seq[0] == '[') {
            if (seq[1] >= '0' && seq[1] <= '9') {
                if (read(STDIN_FILENO, &seq[2], 1) != 1)return '\x1b';
                if (seq[2] == '~') {
                    switch (seq[1]) {
                        case '1': return HOME_KEY;
                        case '3': return DEL_KEY;
                        case '4': return END_KEY;
                        case '5': return PAGE_UP;
                        case '6': return PAGE_DOWN;
                        case '7': return HOME_KEY;
                        case '8': return END_KEY;
                    }
                }
            } else {
                switch (seq[1]) {
                    case 'A': return ARROW_UP;
                    case 'B': return ARROW_DOWN;
                    case 'C': return ARROW_RIGHT;
                    case 'D': return ARROW_LEFT;
                    case 'H': return HOME_KEY;
                    case 'F': return END_KEY;
                }

            }
            
        } else if (seq[0] == '0') {
            switch (seq[1]) {
                case 'H': return HOME_KEY;
                case 'F': return END_KEY;
            }
        }

        return '\x1b';

    } else {
        return c;
    }
    
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


/*** row operations ***/

void editorUpdateRow(erow *row) {
    int tabs = 0;
    int j;
    for (j = 0; j < row->size; j++)
        if (row->chars[j] == '\t') tabs++;
        
    free(row->render);
    row->render = malloc(row->size + tabs*7 + 1);
    int idx = 0;
    for (j = 0; j < row->size; j++) {
        if (row->chars[j] == '\t') {
        row->render[idx++] = ' ';
        while (idx % 8 != 0) row->render[idx++] = ' ';
        } else {
        row->render[idx++] = row->chars[j];
        }
    }
    row->render[idx] = '\0';
    row->rsize = idx;
}

void editorAppendRow(char *s, size_t len) {
    E.row = realloc(E.row, sizeof(erow) * (E.numrows + 1));

    int at = E.numrows;
    E.row[at].size = len;
    E.row[at].chars = malloc(len + 1);
    memcpy(E.row[at].chars, s, len);
    E.row[at].chars[len] = '\0';

    E.row[at].rsize = 0;
    E.row[at].render = NULL;
    E.numrows ++;
}

 
/*** file i/o ***/

void editorOpen(char *filename) {
    FILE *fp = fopen(filename, "r");
    if (!fp) die("fopen");

    char *line = NULL;
    size_t linecap = 0;
    ssize_t linelen;

    while ((linelen = getline(&line, &linecap, fp)) != -1) {
        while (linelen > 0 && (line[linelen - 1] == '\n' ||
                            line[linelen - 1] == '\r'))
        linelen--;
        editorAppendRow(line, linelen);
  }
    free(line);
    fclose(fp);

    
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

void editorScroll() {

    // checks if cursor is above the visible window

  if (E.cy < E.rowoff) {
    E.rowoff = E.cy;
  }

  // checks if cursor past the bottom visible window
  if (E.cy >= E.rowoff + E.screenrows) {
    E.rowoff = E.cy - E.screenrows + 1;
  }
  if (E.cx < E.coloff) {
    E.coloff = E.cx;
  }
  if (E.cx >= E.coloff + E.screencols) {
    E.coloff = E.cx - E.screencols + 1;
  }
}


void editorDrawRows(struct abuf *ab) {
    int y;
    for ( y = 0; y < E.screenrows; y++) {
        int filerow = y + E.rowoff;
        if ( filerow >= E.numrows ) {

            if (E.numrows == 0 && y == E.screenrows / 3) {
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

        } else {
            int len = E.row[filerow].rsize - E.coloff;
            if (len < 0) len = 0;
            if (len > E.screencols) len = E.screencols;
            abAppend(ab, &E.row[filerow].render[E.coloff], len);
        }
        

        abAppend(ab, "\x1b[K", 3);
        if ( y < E.screenrows - 1) {
            abAppend(ab, "\r\n", 2);
        }
        

    }
}

void editorRefreshScreen(){
    editorScroll();

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
    snprintf(buf, sizeof(buf), "\x1b[%d;%dH", (E.cy - E.rowoff) + 1, (E.cx - E.coloff) + 1);
    abAppend(&ab, buf, strlen(buf));

    abAppend(&ab, "\x1b[?25h", 6);

    write(STDOUT_FILENO, ab.b, ab.len);
    abFree(&ab);
}




/*** input ***/

void editorMoveCursor(int key) {
    erow *row = (E.cy >= E.numrows) ? NULL : &E.row[E.cy];


    switch(key) {
        case ARROW_LEFT:
            if (E.cx != 0) {
                E.cx--;
            } else if (E.cy > 0) {
                E.cy--;
                E.cx = E.row[E.cy].size;
            }
            break;
        case ARROW_RIGHT:
            if (row && E.cx < row->size) {
                E.cx++;
            } else if (row && E.cx == row->size) {
                E.cy++;
                E.cx = 0;
            }
            break;
        case ARROW_UP:
            if (E.cy != 0) {
                E.cy--;
            }
            break;
        case ARROW_DOWN:
            if (E.cy < E.numrows) {
                E.cy++;
            }
            break;  
    }

    row = (E.cy >= E.numrows) ? NULL : &E.row[E.cy];
    int rowlen = row ? row->size : 0;
    if (E.cx > rowlen) {
        E.cx = rowlen;
    }
}


//handles the input from editorReadkey
void editorProcessKeyPress() {
    int c = editorReadKey();

    switch (c) {
        case CTRL_KEY('q'):
            write(STDOUT_FILENO, "\x1b[2J", 4);
            write(STDOUT_FILENO, "\x1b[H", 3);
            exit(0);
            break;

        case HOME_KEY:
            E.cx = 0;
            break;

        case END_KEY:
            E.cx = E.screencols - 1;
            break;


        case PAGE_UP:
        case PAGE_DOWN:
            {
                int times = E.screenrows;
                while(times--)
                    editorMoveCursor(c == PAGE_UP ? ARROW_UP : ARROW_DOWN);
            }
            break;


        case ARROW_UP:
        case ARROW_DOWN:
        case ARROW_LEFT:
        case ARROW_RIGHT:
            editorMoveCursor(c);
            break;
    }
}


/*** init ***/


// initializes row and col sizes

void initEditor() {

    E.cx = 0;
    E.cy = 0;
    E.rowoff = 0;
    E.coloff = 0;
    E.numrows = 0;
    E.row = NULL;

    if (getWindowSize(&E.screenrows, &E.screencols) == -1)die("getWindowSize");
}

int main(int argc, char *argv[]) {
    enableRawMode();
    initEditor();
    if (argc >= 2) {
        editorOpen(argv[1]);
    }

    while(1) {
        editorRefreshScreen();
        editorProcessKeyPress();
    }



    return 0;
}