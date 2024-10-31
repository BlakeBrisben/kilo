/*** INCLUDES ***/
#include <asm-generic/ioctls.h>
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <termios.h>
#include <errno.h>
#include <sys/ioctl.h>

/*** DEFINES ***/
#define CTRL_KEY(k) ((k) & 0x1f)

/*** DATA ***/
struct editorConfig {
    int screenrows;
    int screencols;
    struct termios orig_termios;
};

struct editorConfig E;

/*** TERMINAL ***/
// prints error message and returns from program
void die(const char *s) {
    write(STDOUT_FILENO, "\x1b[2J", 4);
    write(STDOUT_FILENO, "\x1b[H", 3);
    perror(s);
    exit(1);
}

// reset terminal to exactly as we found it
void disableRawMode() {
    if(tcsetattr(STDIN_FILENO, TCSAFLUSH, &E.orig_termios) == -1) {
        die("tcsetattr");
    }
}

// stop the terminal from printing everything that is inputted
void enableRawMode() {
    if(tcgetattr(STDIN_FILENO, &E.orig_termios) == -1) {
        die("tcgetattr");
    }

    // make sure that the terminal is reset to original values no matter where exit is from
    atexit(disableRawMode);

    struct termios raw = E.orig_termios;

    // input flag field changes
    // IXON makes sure that ctrl-s and ctrl-q don't stop the transmission of data
    // ICRNL disables ctrl-m and makes it the carriage return(\r) instead of translating to newline(\n)
    // BRKINT, INPCK, ISTRIP all legacy traditions and likely don't do anything in modern applications
    raw.c_iflag &= ~(IXON | ICRNL | BRKINT | INPCK | ISTRIP);

    // output flag field changes
    // OPOST turns off all output processing features like translating \n to \r\n
    raw.c_oflag &= ~(OPOST);

    // sets character size to 8 (Probably already the default)
    raw.c_cflag |= (CS8);

    // local flag field changes
    // ICANON makes it so we read byte by byte instead of by line
    // ISIG turns off ctrl-c and ctrl-z so pressing them does not interfere
    // IEXTEN disables ctrl-v
    raw.c_lflag &= ~(ECHO | ICANON | ISIG | IEXTEN);

    // sets the minimum number of bytes needed for read to 0
    raw.c_cc[VMIN] = 0;
    // sets the maximum amount of time to wait on a read is 1 tenth of a second
    raw.c_cc[VTIME] = 1;

    if(tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw) == -1) {
        die("tcsetattr");
    }
}

char editorReadKey() {
    int nread;
    char c;
    while((nread = read(STDIN_FILENO, &c, 1)) != 1) {
        if(nread == -1 && errno != EAGAIN) die("read");
    }
    return c;
}

int getCursorPosition(int *rows, int *cols) {
    char buf[32];
    unsigned int i = 0;

    // get cursor position by using n command
    if(write(STDOUT_FILENO, "\x1b[6n", 4) != 4) return -1;

    while(i < sizeof(buf) - 1) {
        if(read(STDIN_FILENO, &buf[i], 1) != 1) break;
        if(buf[i] == 'R') break;
        i++;
    }
    buf[i] = '\0';

    if(buf[0] != '\x1b' || buf[1] != '[') return -1;
    if(sscanf(&buf[2], "%d;%d", rows, cols) != 2) return -1;

    return 0;
}

int getWindowSize(int *rows, int *cols) {
    struct winsize ws;

    if(ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == -1 || ws.ws_col == 0) {
        if(write(STDOUT_FILENO, "\x1b[999C\x1b[999B", 12) != 12) return -1;
        return getCursorPosition(rows, cols);
    } else {
        *cols = ws.ws_col;
        *rows = ws.ws_row;
        return 0;
    }
}

/*** OUTPUT ***/
void editorDrawRows() {
    int y;
    for(y = 0; y < E.screenrows; y++) {
        write(STDOUT_FILENO, "~", 1);

        if(y < E.screenrows - 1) {
            write(STDOUT_FILENO, "\r\n", 2);
        }
    }
}

void editorRefreshScreen() {
    // \x1b is the start of an escape sequence (27)
    // [2J means clear the entire screen (1 would be clear up to the cursor and 0 would be clear from the cursor to the end of the screen
    write(STDOUT_FILENO, "\x1b[2J", 4);
    // Moves the cursor to the top left
    write(STDOUT_FILENO, "\x1b[H", 3);

    editorDrawRows();

    write(STDOUT_FILENO, "\x1b[H", 3);
}


/*** INPUT ***/
void editorProcessKeypress() {
    char c = editorReadKey();
    switch (c) {
        case CTRL_KEY('q'):
            write(STDOUT_FILENO, "\x1b[2J", 4);
            write(STDOUT_FILENO, "\x1b[H", 3);
            exit(0);
            break;
    }
}

/*** INIT ***/
void initEditor() {
    if(getWindowSize(&E.screenrows, &E.screencols) == -1) die("getWindowSize");
}

// At 03 Append Buffer
int main() {
    enableRawMode();
    initEditor();

    while(1) {
        editorRefreshScreen();
        editorProcessKeypress();
    }
    return 0;
}
