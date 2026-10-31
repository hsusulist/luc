/* Initial POSIX terminal editor backend for LUC's Linux build. */
#if !defined(_WIN32)
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/select.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/ioctl.h>
#include <termios.h>
#include <unistd.h>

typedef struct {
    char *data;
    size_t len, cap, cursor, preferred_col;
    int dirty;
    char path[1024];
    char status[128];
} LcLinuxEditor;

static int lc_linux_reserve(LcLinuxEditor *e, size_t extra) {
    size_t need = e->len + extra + 1;
    size_t cap = e->cap ? e->cap : 256;
    char *p;
    if (need <= e->cap) return 1;
    while (cap < need) {
        if (cap > (size_t)-1 / 2) return 0;
        cap *= 2;
    }
    p = (char *)realloc(e->data, cap);
    if (!p) return 0;
    e->data = p;
    e->cap = cap;
    return 1;
}

static size_t lc_linux_line_start(const LcLinuxEditor *e, size_t pos) {
    while (pos > 0 && e->data[pos - 1] != '\n') pos--;
    return pos;
}

static size_t lc_linux_line_end(const LcLinuxEditor *e, size_t pos) {
    while (pos < e->len && e->data[pos] != '\n') pos++;
    return pos;
}

static size_t lc_linux_line_number(const LcLinuxEditor *e, size_t pos) {
    size_t line = 0, i;
    for (i = 0; i < pos; i++) if (e->data[i] == '\n') line++;
    return line;
}

static void lc_linux_insert(LcLinuxEditor *e, char c) {
    if (!lc_linux_reserve(e, 1)) {
        snprintf(e->status, sizeof e->status, "out of memory");
        return;
    }
    memmove(e->data + e->cursor + 1, e->data + e->cursor,
            e->len - e->cursor + 1);
    e->data[e->cursor++] = c;
    e->len++;
    e->dirty = 1;
}

static int lc_linux_save(LcLinuxEditor *e) {
    FILE *f;
    if (!e->path[0]) {
        int suffix = 0;
        do {
            if (suffix == 0) snprintf(e->path, sizeof e->path, "untitled.luc");
            else snprintf(e->path, sizeof e->path, "untitled-%d.luc", suffix);
            suffix++;
        } while (access(e->path, F_OK) == 0 && suffix < 10000);
        if (access(e->path, F_OK) == 0) {
            errno = EEXIST;
            return 0;
        }
    }
    f = fopen(e->path, "wb");
    if (!f) return 0;
    if (e->len && fwrite(e->data, 1, e->len, f) != e->len) {
        fclose(f);
        return 0;
    }
    if (fclose(f) != 0) return 0;
    e->dirty = 0;
    return 1;
}

static int lc_linux_load(LcLinuxEditor *e, const char *path) {
    FILE *f = fopen(path, "rb");
    long size;
    if (!f) {
        if (errno != ENOENT || !lc_linux_reserve(e, 0)) return 0;
        e->data[0] = 0;
        return 1;
    }
    if (fseek(f, 0, SEEK_END) != 0 || (size = ftell(f)) < 0 ||
        fseek(f, 0, SEEK_SET) != 0 || !lc_linux_reserve(e, (size_t)size)) {
        fclose(f);
        return 0;
    }
    e->len = fread(e->data, 1, (size_t)size, f);
    if (ferror(f)) {
        fclose(f);
        return 0;
    }
    fclose(f);
    e->data[e->len] = 0;
    e->cursor = e->len;
    return 1;
}

static void lc_linux_draw(const LcLinuxEditor *e, size_t top, int rows, int cols) {
    size_t line = 0, i = 0, cursor_line, cursor_col = 0, scan = 0;
    int row = 0;
    cursor_line = lc_linux_line_number(e, e->cursor);
    while (scan < e->cursor && e->data[scan] != '\n') { cursor_col++; scan++; }

    printf("\x1b[H\x1b[2J");
    while (row < rows - 1 && i < e->len + 1) {
        size_t end = lc_linux_line_end(e, i), j;
        if (line >= top) {
            printf("\x1b[%d;1H", row + 1);
            for (j = i; j < end && j - i < (size_t)cols; j++) {
                unsigned char c = (unsigned char)e->data[j];
                putchar(c >= 32 && c != 127 ? c : ' ');
            }
            row++;
        }
        if (end >= e->len) break;
        i = end + 1;
        line++;
    }
    while (row < rows - 1) {
        printf("\x1b[%d;1H~", row + 1);
        row++;
    }
    printf("\x1b[%d;1H%s%s  %zu:%zu  Ctrl-S save  Ctrl-Q save & quit  %s",
           rows, e->path[0] ? e->path : "[No Name]",
           e->dirty ? " [+]" : "", cursor_line + 1, cursor_col + 1,
           e->status);
    {
        size_t screen_line = cursor_line >= top ? cursor_line - top : 0;
        printf("\x1b[%zu;%zuH", screen_line + 1,
               cursor_col + 1 > (size_t)cols ? (size_t)cols : cursor_col + 1);
    }
    fflush(stdout);
}

static int lc_linux_key(void) {
    unsigned char c;
    if (read(STDIN_FILENO, &c, 1) != 1) return -1;
    if (c != 27) return c;
    {
        struct timeval tv = {0, 100000};
        fd_set fds;
        unsigned char seq[2];
        FD_ZERO(&fds);
        FD_SET(STDIN_FILENO, &fds);
        if (select(STDIN_FILENO + 1, &fds, NULL, NULL, &tv) <= 0) return 27;
        if (read(STDIN_FILENO, &seq[0], 1) != 1) return 27;
        if (seq[0] != '[' || read(STDIN_FILENO, &seq[1], 1) != 1) return 27;
        switch (seq[1]) {
            case 'A': return 1001;
            case 'B': return 1002;
            case 'C': return 1003;
            case 'D': return 1004;
            case 'H': return 1005;
            case 'F': return 1006;
            default: return 0;
        }
    }
}

static void lc_code(const char *start) {
    LcLinuxEditor e;
    struct termios old_term, raw;
    struct winsize ws;
    size_t top = 0;
    int done = 0;
    memset(&e, 0, sizeof e);
    if (!isatty(STDIN_FILENO) || !isatty(STDOUT_FILENO)) {
        printf("lcode needs an interactive terminal.\n");
        return;
    }
    if (start && *start) {
        struct stat st;
        if (stat(start, &st) == 0 && S_ISDIR(st.st_mode)) {
            fprintf(stderr, "lcode: pass a file path, not a directory.\n");
            return;
        }
        snprintf(e.path, sizeof e.path, "%s", start);
        if (!lc_linux_load(&e, start)) {
            fprintf(stderr, "lcode: cannot read '%s'.\n", start);
            free(e.data);
            return;
        }
    } else if (!lc_linux_reserve(&e, 0)) {
        fprintf(stderr, "lcode: out of memory.\n");
        return;
    }
    if (tcgetattr(STDIN_FILENO, &old_term) != 0) {
        fprintf(stderr, "lcode: cannot read terminal settings.\n");
        free(e.data);
        return;
    }
    raw = old_term;
    raw.c_lflag &= (tcflag_t)~(ECHO | ICANON | IEXTEN | ISIG);
    raw.c_iflag &= (tcflag_t)~(IXON | ICRNL);
    raw.c_oflag &= (tcflag_t)~(OPOST);
    raw.c_cc[VMIN] = 1;
    raw.c_cc[VTIME] = 0;
    if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw) != 0) {
        fprintf(stderr, "lcode: cannot enable terminal input mode.\n");
        free(e.data);
        return;
    }
    printf("\x1b[?1049h\x1b[?25l");
    while (!done) {
        int rows = 24, cols = 80, key;
        size_t cursor_line = lc_linux_line_number(&e, e.cursor);
        if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == 0) {
            if (ws.ws_row > 0) rows = ws.ws_row;
            if (ws.ws_col > 0) cols = ws.ws_col;
        }
        if (rows < 3) rows = 3;
        if (cursor_line < top) top = cursor_line;
        if (cursor_line >= top + (size_t)(rows - 1))
            top = cursor_line - (size_t)(rows - 2);
        lc_linux_draw(&e, top, rows, cols);
        key = lc_linux_key();
        if (key < 0) break;
        if (key == 17) {
            if (!e.dirty) done = 1;
            else if (lc_linux_save(&e)) done = 1;
            else snprintf(e.status, sizeof e.status, "save failed: %s", strerror(errno));
        } else if (key == 19) {
            if (lc_linux_save(&e)) e.status[0] = 0;
            else snprintf(e.status, sizeof e.status, "save failed: %s", strerror(errno));
        } else if (key == 1001 || key == 1002) {
            size_t start_line = lc_linux_line_start(&e, e.cursor);
            size_t end_line = lc_linux_line_end(&e, e.cursor);
            size_t col = e.cursor - start_line;
            if (key == 1001 && start_line > 0) {
                size_t prev_end = start_line - 1;
                size_t prev_start = lc_linux_line_start(&e, prev_end);
                size_t prev_len = prev_end - prev_start;
                e.cursor = prev_start + (col < prev_len ? col : prev_len);
            } else if (key == 1002 && end_line < e.len) {
                size_t next_start = end_line + 1;
                size_t next_end = lc_linux_line_end(&e, next_start);
                size_t next_len = next_end - next_start;
                e.cursor = next_start + (col < next_len ? col : next_len);
            }
        } else if (key == 1003) {
            if (e.cursor < e.len) e.cursor++;
        } else if (key == 1004) {
            if (e.cursor > 0) e.cursor--;
        } else if (key == 1005) {
            e.cursor = lc_linux_line_start(&e, e.cursor);
        } else if (key == 1006) {
            e.cursor = lc_linux_line_end(&e, e.cursor);
        } else if (key == 127 || key == 8) {
            if (e.cursor > 0) {
                memmove(e.data + e.cursor - 1, e.data + e.cursor,
                        e.len - e.cursor + 1);
                e.cursor--;
                e.len--;
                e.dirty = 1;
            }
        } else if (key == 13) {
            lc_linux_insert(&e, '\n');
        } else if (key >= 32 && key <= 255) {
            lc_linux_insert(&e, (char)key);
        }
    }
    tcsetattr(STDIN_FILENO, TCSAFLUSH, &old_term);
    printf("\x1b[?25h\x1b[?1049l");
    if (e.dirty)
        fprintf(stderr, "lcode: unsaved changes were discarded.\n");
    free(e.data);
}
#else
static void lc_code(const char *start) {
    (void)start;
    printf("lcode-linux is only for Linux builds.\n");
}
#endif
