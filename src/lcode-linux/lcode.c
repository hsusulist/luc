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

static int lc_linux_word_char(unsigned char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
           (c >= '0' && c <= '9') || c == '_';
}

static unsigned char lc_linux_lower(unsigned char c) {
    return c >= 'A' && c <= 'Z' ? (unsigned char)(c + ('a' - 'A')) : c;
}

static int lc_linux_starts_word(const char *s, const char *word) {
    size_t i = 0;
    while (*s == ' ' || *s == '\t') s++;
    while (word[i] && lc_linux_lower((unsigned char)s[i]) == lc_linux_lower((unsigned char)word[i])) i++;
    return word[i] == 0 && !lc_linux_word_char((unsigned char)s[i]);
}

static int lc_linux_ends_word(const char *s, const char *word) {
    size_t n = strlen(s), m = strlen(word), i;
    while (n && (s[n - 1] == ' ' || s[n - 1] == '\t')) n--;
    if (n < m) return 0;
    for (i = 0; i < m; i++) {
        if (lc_linux_lower((unsigned char)s[n - m + i]) != lc_linux_lower((unsigned char)word[i])) return 0;
    }
    return n == m || !lc_linux_word_char((unsigned char)s[n - m - 1]);
}

static int lc_linux_has_word(const char *s, const char *word) {
    size_t i, n = strlen(s), m = strlen(word), j;
    if (!m || m > n) return 0;
    for (i = 0; i + m <= n; i++) {
        if (i && lc_linux_word_char((unsigned char)s[i - 1])) continue;
        if (i + m < n && lc_linux_word_char((unsigned char)s[i + m])) continue;
        for (j = 0; j < m; j++) {
            if (lc_linux_lower((unsigned char)s[i + j]) != lc_linux_lower((unsigned char)word[j])) break;
        }
        if (j == m) return 1;
    }
    return 0;
}

static int lc_linux_ext(const char *path, const char *ext) {
    const char *dot = strrchr(path, '.');
    size_t i, n = strlen(ext);
    if (!dot || strlen(dot + 1) != n) return 0;
    for (i = 0; i < n; i++) {
        if (lc_linux_lower((unsigned char)dot[i + 1]) != lc_linux_lower((unsigned char)ext[i])) return 0;
    }
    return 1;
}

static void lc_linux_strip_comment(char *s, const char *path) {
    int quote = 0, escape = 0;
    int hash = lc_linux_ext(path, "py") || lc_linux_ext(path, "sh") ||
               lc_linux_ext(path, "bash") || lc_linux_ext(path, "zsh") ||
               lc_linux_ext(path, "rb") || lc_linux_ext(path, "pl") ||
               lc_linux_ext(path, "r") || lc_linux_ext(path, "yaml") ||
               lc_linux_ext(path, "yml");
    int dash = !path[0] || lc_linux_ext(path, "luc") || lc_linux_ext(path, "lua") ||
               lc_linux_ext(path, "sql");
    int slash = lc_linux_ext(path, "c") || lc_linux_ext(path, "h") ||
                lc_linux_ext(path, "cpp") || lc_linux_ext(path, "hpp") ||
                lc_linux_ext(path, "cc") || lc_linux_ext(path, "cxx") ||
                lc_linux_ext(path, "js") || lc_linux_ext(path, "ts") ||
                lc_linux_ext(path, "java") || lc_linux_ext(path, "go") ||
                lc_linux_ext(path, "rs") || lc_linux_ext(path, "cs") ||
                lc_linux_ext(path, "kt") || lc_linux_ext(path, "swift") ||
                lc_linux_ext(path, "dart") || lc_linux_ext(path, "scala") ||
                lc_linux_ext(path, "php") || lc_linux_ext(path, "css") ||
                lc_linux_ext(path, "scss") || lc_linux_ext(path, "less") ||
                lc_linux_ext(path, "tf") || lc_linux_ext(path, "hcl");
    size_t i, n = strlen(s);
    for (i = 0; i < n; i++) {
        unsigned char c = (unsigned char)s[i];
        if (quote) {
            if (escape) escape = 0;
            else if (c == '\\') escape = 1;
            else if (c == (unsigned char)quote) quote = 0;
            continue;
        }
        if (c == '\'' || c == '"' || c == '`') { quote = c; continue; }
        if (hash && c == '#') { s[i] = 0; return; }
        if (dash && c == '-' && s[i + 1] == '-') { s[i] = 0; return; }
        if (slash && c == '/' && s[i + 1] == '/') { s[i] = 0; return; }
        if (slash && c == '/' && s[i + 1] == '*') { s[i] = 0; return; }
    }
}

static int lc_linux_html_block(const char *s, char *closer, size_t cap) {
    static const char *voids[] = {
        "area", "base", "br", "col", "embed", "hr", "img", "input",
        "link", "meta", "param", "source", "track", "wbr", NULL
    };
    char tag[128];
    size_t n = strlen(s), i = 1, j = 0, k;
    if (n < 3 || s[0] != '<' || s[1] == '/' || s[1] == '!' ||
        s[1] == '?' || s[n - 1] != '>') return 0;
    while (i < n - 1 && ((s[i] >= 'a' && s[i] <= 'z') ||
           (s[i] >= 'A' && s[i] <= 'Z') || (s[i] >= '0' && s[i] <= '9') ||
           s[i] == ':' || s[i] == '-')) {
        if (j + 1 >= sizeof tag) return 0;
        tag[j++] = s[i++];
    }
    if (!j || s[n - 2] == '/') return 0;
    tag[j] = 0;
    for (k = 0; voids[k]; k++) {
        size_t a = strlen(tag), b = strlen(voids[k]), x;
        if (a != b) continue;
        for (x = 0; x < a; x++) {
            if (lc_linux_lower((unsigned char)tag[x]) != lc_linux_lower((unsigned char)voids[k][x])) break;
        }
        if (x == a) return 0;
    }
    return snprintf(closer, cap, "</%s>", tag) < (int)cap;
}

static int lc_linux_block(const char *path, char *code, char *closer, size_t cap) {
    static const char *py_blocks[] = {
        "if", "elif", "else", "for", "while", "def", "class", "try",
        "except", "finally", "with", "match", "case", NULL
    };
    static const char *end_blocks[] = {
        "def", "class", "module", "if", "unless", "while", "until",
        "for", "begin", "case", NULL
    };
    size_t n = strlen(code), i;
    const char *p = code;
    closer[0] = 0;
    while (n && (code[n - 1] == ' ' || code[n - 1] == '\t' || code[n - 1] == '\r')) code[--n] = 0;
    if (!n) return 0;
    if (code[n - 1] == '{') return snprintf(closer, cap, "}") < (int)cap;
    if ((lc_linux_ext(path, "json") || lc_linux_ext(path, "js") ||
         lc_linux_ext(path, "py") || lc_linux_ext(path, "c") ||
         lc_linux_ext(path, "h") || lc_linux_ext(path, "go") ||
         lc_linux_ext(path, "rs") || lc_linux_ext(path, "java") ||
         lc_linux_ext(path, "cs") || lc_linux_ext(path, "ts") ||
         lc_linux_ext(path, "php")) && code[n - 1] == '[') {
        return snprintf(closer, cap, "]") < (int)cap;
    }
    if (lc_linux_ext(path, "html") || lc_linux_ext(path, "htm") ||
        lc_linux_ext(path, "xml") || lc_linux_ext(path, "svg")) {
        return lc_linux_html_block(code, closer, cap);
    }
    if (lc_linux_ext(path, "py")) {
        if (code[n - 1] != ':') return 0;
        if (lc_linux_starts_word(p, "async")) {
            p += 5;
            while (*p == ' ' || *p == '\t') p++;
        }
        for (i = 0; py_blocks[i]; i++) if (lc_linux_starts_word(p, py_blocks[i])) return 1;
        return 0;
    }
    if (lc_linux_ext(path, "yaml") || lc_linux_ext(path, "yml")) return code[n - 1] == ':';
    if (!path[0] || lc_linux_ext(path, "luc") || lc_linux_ext(path, "lua")) {
        if (lc_linux_starts_word(code, "else") || lc_linux_starts_word(code, "elseif")) return 1;
        if (lc_linux_ext(path, "lua") && lc_linux_ends_word(code, "repeat")) {
            return snprintf(closer, cap, "until") < (int)cap;
        }
        if (lc_linux_ends_word(code, "then") || lc_linux_ends_word(code, "do") ||
            (lc_linux_has_word(code, "function") && code[n - 1] == ')') ||
            (lc_linux_starts_word(code, "command") && code[n - 1] == ')')) {
            return snprintf(closer, cap, "end") < (int)cap;
        }
        return 0;
    }
    if (lc_linux_ext(path, "rb") || lc_linux_ext(path, "pl")) {
        if (lc_linux_starts_word(code, "else") || lc_linux_starts_word(code, "elsif") ||
            lc_linux_starts_word(code, "when") || lc_linux_starts_word(code, "rescue") ||
            lc_linux_starts_word(code, "ensure") || lc_linux_starts_word(code, "continue")) return 1;
        for (i = 0; end_blocks[i]; i++) {
            if (lc_linux_starts_word(code, end_blocks[i])) return snprintf(closer, cap, "end") < (int)cap;
        }
        if (lc_linux_ends_word(code, "do") || lc_linux_has_word(code, "do")) {
            return snprintf(closer, cap, "end") < (int)cap;
        }
        if ((lc_linux_ext(path, "pl") && lc_linux_starts_word(code, "sub")) ||
            (lc_linux_ext(path, "rb") && lc_linux_starts_word(code, "def"))) {
            return snprintf(closer, cap, "end") < (int)cap;
        }
    }
    if (lc_linux_ext(path, "sh") || lc_linux_ext(path, "bash") || lc_linux_ext(path, "zsh")) {
        if (lc_linux_starts_word(code, "else") || lc_linux_starts_word(code, "elif")) return 1;
        if (lc_linux_starts_word(code, "case") && lc_linux_ends_word(code, "in")) {
            return snprintf(closer, cap, "esac") < (int)cap;
        }
        if (lc_linux_ends_word(code, "then")) return snprintf(closer, cap, "fi") < (int)cap;
        if (lc_linux_ends_word(code, "do")) return snprintf(closer, cap, "done") < (int)cap;
    }
    if (lc_linux_ext(path, "sql")) {
        if (lc_linux_starts_word(code, "else") || lc_linux_starts_word(code, "when")) return 1;
        if (lc_linux_ends_word(code, "case") || lc_linux_ends_word(code, "begin")) {
            return snprintf(closer, cap, "END;") < (int)cap;
        }
        if (lc_linux_ends_word(code, "then")) {
            return snprintf(closer, cap, lc_linux_has_word(code, "if") ? "END IF;" : "END;") < (int)cap;
        }
        if (lc_linux_ends_word(code, "loop")) return snprintf(closer, cap, "END LOOP;") < (int)cap;
    }
    return 0;
}

static int lc_linux_tail_closer(const char *tail, const char *closer) {
    size_t i = 0, j, n = strlen(tail), m = strlen(closer);
    while (i < n && (tail[i] == ' ' || tail[i] == '\t' || tail[i] == '\r')) i++;
    if (n - i < m) return 0;
    for (j = 0; j < m; j++) {
        if (lc_linux_lower((unsigned char)tail[i + j]) != lc_linux_lower((unsigned char)closer[j])) return 0;
    }
    i += m;
    while (i < n && (tail[i] == ' ' || tail[i] == '\t' || tail[i] == '\r')) i++;
    if (i < n && tail[i] == ';') i++;
    while (i < n && (tail[i] == ' ' || tail[i] == '\t' || tail[i] == '\r')) i++;
    return i == n;
}

static void lc_linux_newline(LcLinuxEditor *e) {
    size_t start = lc_linux_line_start(e, e->cursor);
    size_t end = lc_linux_line_end(e, e->cursor);
    size_t prefix_len = e->cursor - start, tail_len = end - e->cursor;
    size_t base_len = 0, inner_len, close_len = 0, i, insert_len;
    char closer[256], *prefix, *code_buf, *code, *tail, *base, *insert;
    int block, has_closer, tail_blank = 1, make_closer;
    const char *path = e->path;
    prefix = (char *)malloc(prefix_len + 1);
    code_buf = (char *)malloc(prefix_len + 1);
    tail = (char *)malloc(tail_len + 1);
    if (!prefix || !code_buf || !tail) {
        free(prefix); free(code_buf); free(tail);
        snprintf(e->status, sizeof e->status, "out of memory");
        return;
    }
    memcpy(prefix, e->data + start, prefix_len); prefix[prefix_len] = 0;
    memcpy(code_buf, prefix, prefix_len + 1); code = code_buf;
    memcpy(tail, e->data + e->cursor, tail_len); tail[tail_len] = 0;
    lc_linux_strip_comment(code, path);
    while (*code == ' ' || *code == '\t') code++;
    block = lc_linux_block(path, code, closer, sizeof closer);
    while (e->data[start + base_len] == ' ' || e->data[start + base_len] == '\t') base_len++;
    base = (char *)malloc(base_len + 1);
    if (!base) {
        free(prefix); free(code_buf); free(tail);
        snprintf(e->status, sizeof e->status, "out of memory");
        return;
    }
    memcpy(base, e->data + start, base_len); base[base_len] = 0;
    for (i = 0; i < tail_len; i++) {
        if (tail[i] != ' ' && tail[i] != '\t' && tail[i] != '\r') { tail_blank = 0; break; }
    }
    has_closer = block && closer[0] && lc_linux_tail_closer(tail, closer);
    if (block && !tail_blank && !has_closer) block = 0;
    make_closer = block && closer[0];
    inner_len = base_len + (block ? 4 : 0);
    insert_len = 1 + inner_len;
    if (make_closer) {
        if (has_closer) {
            size_t first = 0, last = tail_len;
            while (first < last && (tail[first] == ' ' || tail[first] == '\t' || tail[first] == '\r')) first++;
            while (last > first && (tail[last - 1] == ' ' || tail[last - 1] == '\t' || tail[last - 1] == '\r')) last--;
            close_len = last - first;
            memmove(tail, tail + first, close_len);
        } else {
            close_len = strlen(closer);
        }
        insert_len += 1 + base_len + close_len;
    }
    insert = (char *)malloc(insert_len);
    if (!insert || !lc_linux_reserve(e, insert_len)) {
        free(prefix); free(code_buf); free(tail); free(base); free(insert);
        snprintf(e->status, sizeof e->status, "out of memory");
        return;
    }
    i = 0;
    insert[i++] = '\n';
    memcpy(insert + i, base, base_len); i += base_len;
    if (block) { memcpy(insert + i, "    ", 4); i += 4; }
    if (make_closer) {
        insert[i++] = '\n';
        memcpy(insert + i, base, base_len); i += base_len;
        memcpy(insert + i, has_closer ? tail : closer, close_len); i += close_len;
    }
    if (block && tail_len) {
        memmove(e->data + e->cursor, e->data + end, e->len - end + 1);
        e->len -= tail_len;
    }
    memmove(e->data + e->cursor + insert_len, e->data + e->cursor,
            e->len - e->cursor + 1);
    memcpy(e->data + e->cursor, insert, insert_len);
    e->cursor += 1 + inner_len;
    e->len += insert_len;
    e->dirty = 1;
    free(prefix); free(code_buf); free(tail); free(base); free(insert);
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
            lc_linux_newline(&e);
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
