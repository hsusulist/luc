/* luc_posix_console.h - tiny Win32 console/file shim so the LCode editor
   (written against the Win32 console API) builds and runs on Linux/macOS.

   Only the subset the editor uses is provided:
     - console: GetStdHandle / Get|SetConsoleMode / GetConsoleScreenBufferInfo /
       ReadConsoleInputW  (termios raw mode + xterm SGR mouse + escape parser)
     - files:   GetFileAttributesA / FindFirstFileA ... / CreateDirectoryA /
       RemoveDirectoryA / DeleteFileA / MoveFileA / CopyFileA / GetFullPathNameA /
       Get|SetCurrentDirectoryA / GetEnvironmentVariableA / GetModuleFileNameA
   Included from luc_core.c (after g_exepath) on non-Windows builds only. */
#ifndef LUC_POSIX_CONSOLE_H
#define LUC_POSIX_CONSOLE_H
#if !defined(_WIN32)

#include <stdint.h>
#include <string.h>
#include <strings.h>
#include <errno.h>
#include <signal.h>
#include <termios.h>
#include <unistd.h>
#include <fcntl.h>
#include <dirent.h>
#include <poll.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <stdlib.h>
/* realpath may be hidden by a strict feature-test macro set elsewhere */
extern char *realpath(const char *restrict path,char *restrict resolved);

/* ---- basic Win32 types ---- */
typedef uint32_t DWORD;
typedef uint16_t WORD;
typedef uint8_t  BYTE;
typedef int32_t  LONG;
typedef int16_t  SHORT;
typedef uint16_t WCHAR;
typedef int      BOOL;
typedef void    *HANDLE;
#ifndef TRUE
#  define TRUE 1
#endif
#ifndef FALSE
#  define FALSE 0
#endif
#define INVALID_HANDLE_VALUE ((HANDLE)(intptr_t)-1)
#define STD_INPUT_HANDLE  ((DWORD)-10)
#define STD_OUTPUT_HANDLE ((DWORD)-11)
#define STD_ERROR_HANDLE  ((DWORD)-12)

/* ---- console modes ---- */
#define ENABLE_PROCESSED_INPUT  0x0001
#define ENABLE_LINE_INPUT       0x0002
#define ENABLE_ECHO_INPUT       0x0004
#define ENABLE_WINDOW_INPUT     0x0008
#define ENABLE_MOUSE_INPUT      0x0010
#define ENABLE_QUICK_EDIT_MODE  0x0040
#define ENABLE_EXTENDED_FLAGS   0x0080

/* ---- input records ---- */
typedef struct { SHORT X, Y; } COORD;
typedef struct { SHORT Left, Top, Right, Bottom; } SMALL_RECT;
typedef struct {
    COORD dwSize, dwCursorPosition; WORD wAttributes;
    SMALL_RECT srWindow; COORD dwMaximumWindowSize;
} CONSOLE_SCREEN_BUFFER_INFO;
typedef struct {
    BOOL bKeyDown; WORD wRepeatCount, wVirtualKeyCode, wVirtualScanCode;
    union { WCHAR UnicodeChar; char AsciiChar; } uChar;
    DWORD dwControlKeyState;
} KEY_EVENT_RECORD;
typedef struct {
    COORD dwMousePosition; DWORD dwButtonState, dwControlKeyState, dwEventFlags;
} MOUSE_EVENT_RECORD;
typedef struct { COORD dwSize; } WINDOW_BUFFER_SIZE_RECORD;
typedef struct {
    WORD EventType;
    union {
        KEY_EVENT_RECORD KeyEvent;
        MOUSE_EVENT_RECORD MouseEvent;
        WINDOW_BUFFER_SIZE_RECORD WindowBufferSizeEvent;
    } Event;
} INPUT_RECORD;
#define KEY_EVENT                0x0001
#define MOUSE_EVENT              0x0002
#define WINDOW_BUFFER_SIZE_EVENT 0x0004
#define FROM_LEFT_1ST_BUTTON_PRESSED 0x0001
#define RIGHTMOST_BUTTON_PRESSED     0x0002
#define FROM_LEFT_2ND_BUTTON_PRESSED 0x0004
#define MOUSE_MOVED   0x0001
#define DOUBLE_CLICK  0x0002
#define MOUSE_WHEELED 0x0004
#define RIGHT_ALT_PRESSED  0x0001
#define LEFT_ALT_PRESSED   0x0002
#define RIGHT_CTRL_PRESSED 0x0004
#define LEFT_CTRL_PRESSED  0x0008
#define SHIFT_PRESSED      0x0010

/* ---- virtual keys ---- */
#define VK_PRIOR  0x21
#define VK_NEXT   0x22
#define VK_END    0x23
#define VK_HOME   0x24
#define VK_LEFT   0x25
#define VK_UP     0x26
#define VK_RIGHT  0x27
#define VK_DOWN   0x28
#define VK_INSERT 0x2D
#define VK_DELETE 0x2E
#define VK_F1     0x70
#define VK_F2     0x71
#define VK_F3     0x72
#define VK_F4     0x73
#define VK_F5     0x74
#define VK_F6     0x75
#define VK_F7     0x76
#define VK_F8     0x77
#define VK_F9     0x78
#define VK_F10    0x79
#define VK_F11    0x7A
#define VK_F12    0x7B
#define VK_OEM_3  0xC0

/* ---- files ---- */
#define INVALID_FILE_ATTRIBUTES      ((DWORD)-1)
#define FILE_ATTRIBUTE_READONLY      0x0001
#define FILE_ATTRIBUTE_DIRECTORY     0x0010
#define FILE_ATTRIBUTE_NORMAL        0x0080
#define FILE_ATTRIBUTE_REPARSE_POINT 0x0400
#define ERROR_SUCCESS        0
#define ERROR_FILE_NOT_FOUND 2
#define ERROR_ACCESS_DENIED  5
#define ERROR_NO_MORE_FILES  18
#define ERROR_ALREADY_EXISTS 183
typedef struct {
    DWORD dwFileAttributes;
    char  cFileName[1024];
} WIN32_FIND_DATAA;

#define _stricmp strcasecmp
#define _popen   popen
#define _pclose  pclose

static DWORD lpc_lasterr = 0;
static DWORD GetLastError(void){ return lpc_lasterr; }
static void lpc_set_err(void){
    lpc_lasterr = errno==EEXIST ? ERROR_ALREADY_EXISTS :
                  errno==ENOENT ? ERROR_FILE_NOT_FOUND :
                  errno==EACCES||errno==EPERM ? ERROR_ACCESS_DENIED : (DWORD)errno;
}

static DWORD lpc_attrs(const char *p){
    struct stat st, lst;
    if(stat(p,&st)!=0){
        if(lstat(p,&lst)==0) return FILE_ATTRIBUTE_NORMAL|FILE_ATTRIBUTE_REPARSE_POINT; /* dangling link */
        lpc_set_err(); return INVALID_FILE_ATTRIBUTES;
    }
    DWORD a = S_ISDIR(st.st_mode) ? FILE_ATTRIBUTE_DIRECTORY : FILE_ATTRIBUTE_NORMAL;
    if(lstat(p,&lst)==0 && S_ISLNK(lst.st_mode)) a |= FILE_ATTRIBUTE_REPARSE_POINT;
    if(access(p,W_OK)!=0) a |= FILE_ATTRIBUTE_READONLY;
    return a;
}
static DWORD GetFileAttributesA(const char *p){ return lpc_attrs(p); }
static BOOL  SetFileAttributesA(const char *p,DWORD a){ (void)p; (void)a; return TRUE; }

typedef struct { DIR *d; char dir[1024]; } LpcFind;
static BOOL lpc_find_fill(LpcFind *f,WIN32_FIND_DATAA *fd){
    struct dirent *de = readdir(f->d);
    char full[2100];
    if(!de){ lpc_lasterr = ERROR_NO_MORE_FILES; return FALSE; }
    snprintf(fd->cFileName,sizeof fd->cFileName,"%s",de->d_name);
    snprintf(full,sizeof full,"%s/%s",f->dir,de->d_name);
    fd->dwFileAttributes = lpc_attrs(full);
    if(fd->dwFileAttributes==INVALID_FILE_ATTRIBUTES) fd->dwFileAttributes = FILE_ATTRIBUTE_NORMAL;
    return TRUE;
}
/* pattern is "<dir>/" + "*" or "<dir>\\" + "*" */
static HANDLE FindFirstFileA(const char *pat,WIN32_FIND_DATAA *fd){
    LpcFind *f = (LpcFind*)calloc(1,sizeof *f);
    size_t n;
    if(!f) return INVALID_HANDLE_VALUE;
    snprintf(f->dir,sizeof f->dir,"%s",pat);
    n = strlen(f->dir);
    if(n>0 && f->dir[n-1]=='*') f->dir[--n]=0;
    while(n>1 && (f->dir[n-1]=='/'||f->dir[n-1]=='\\')) f->dir[--n]=0;
    if(!n) snprintf(f->dir,sizeof f->dir,".");
    f->d = opendir(f->dir);
    if(!f->d){ lpc_set_err(); free(f); return INVALID_HANDLE_VALUE; }
    if(!lpc_find_fill(f,fd)){ closedir(f->d); free(f); return INVALID_HANDLE_VALUE; }
    return (HANDLE)f;
}
static BOOL FindNextFileA(HANDLE h,WIN32_FIND_DATAA *fd){ return lpc_find_fill((LpcFind*)h,fd); }
static BOOL FindClose(HANDLE h){
    LpcFind *f=(LpcFind*)h;
    if(!f||h==INVALID_HANDLE_VALUE) return FALSE;
    closedir(f->d); free(f); return TRUE;
}
static BOOL CreateDirectoryA(const char *p,void *sa){
    (void)sa;
    if(mkdir(p,0777)==0) return TRUE;
    lpc_set_err(); return FALSE;
}
static BOOL RemoveDirectoryA(const char *p){
    struct stat lst;
    if(lstat(p,&lst)==0 && S_ISLNK(lst.st_mode)){   /* link to a dir: drop the link */
        if(unlink(p)==0) return TRUE;
    } else if(rmdir(p)==0) return TRUE;
    lpc_set_err(); return FALSE;
}
static BOOL DeleteFileA(const char *p){ if(unlink(p)==0) return TRUE; lpc_set_err(); return FALSE; }
static BOOL MoveFileA(const char *a,const char *b){
    struct stat st;
    if(lstat(b,&st)==0){ lpc_lasterr=ERROR_ALREADY_EXISTS; return FALSE; }
    if(rename(a,b)==0) return TRUE;
    lpc_set_err(); return FALSE;
}
static BOOL CopyFileA(const char *src,const char *dst,BOOL fail_if_exists){
    struct stat st; int in, out; char buf[65536]; ssize_t r;
    if(fail_if_exists && lstat(dst,&st)==0){ lpc_lasterr=ERROR_ALREADY_EXISTS; return FALSE; }
    in = open(src,O_RDONLY);
    if(in<0){ lpc_set_err(); return FALSE; }
    if(fstat(in,&st)!=0){ lpc_set_err(); close(in); return FALSE; }
    out = open(dst,O_WRONLY|O_CREAT|O_TRUNC,st.st_mode&0777);
    if(out<0){ lpc_set_err(); close(in); return FALSE; }
    while((r=read(in,buf,sizeof buf))>0){
        char *p=buf;
        while(r>0){
            ssize_t w=write(out,p,(size_t)r);
            if(w<0){ if(errno==EINTR) continue; lpc_set_err(); close(in); close(out); return FALSE; }
            p+=w; r-=w;
        }
    }
    close(in);
    if(close(out)!=0){ lpc_set_err(); return FALSE; }
    return TRUE;
}
static DWORD GetFullPathNameA(const char *in,DWORD cap,char *out,char **filepart){
    char tmp[4096];
    if(filepart) *filepart=NULL;
    if(!in||!*in) in=".";
    if(realpath(in,tmp)==NULL){
        /* not existing yet: make it absolute by hand */
        if(in[0]=='/') snprintf(tmp,sizeof tmp,"%s",in);
        else {
            char cwd[2048];
            if(!getcwd(cwd,sizeof cwd)) return 0;
            snprintf(tmp,sizeof tmp,"%s/%s",cwd,in);
        }
    }
    if(strlen(tmp)+1>cap) return (DWORD)strlen(tmp)+1;
    snprintf(out,cap,"%s",tmp);
    return (DWORD)strlen(out);
}
static DWORD GetCurrentDirectoryA(DWORD cap,char *buf){
    if(!getcwd(buf,cap)) return 0;
    return (DWORD)strlen(buf);
}
static BOOL SetCurrentDirectoryA(const char *p){ return chdir(p)==0; }
/* USERPROFILE -> $HOME; APPDATA -> $XDG_CONFIG_HOME or ~/.config */
static DWORD GetEnvironmentVariableA(const char *name,char *buf,DWORD cap){
    const char *v=NULL; char tmp[2048];
    if(!strcmp(name,"USERPROFILE")) v=getenv("HOME");
    else if(!strcmp(name,"APPDATA")){
        v=getenv("XDG_CONFIG_HOME");
        if(!v||!*v){
            const char *h=getenv("HOME");
            if(h&&*h){ snprintf(tmp,sizeof tmp,"%s/.config",h); mkdir(tmp,0755); v=tmp; }
        }
    } else v=getenv(name);
    if(!v||!*v) return 0;
    if(strlen(v)+1>cap) return (DWORD)strlen(v)+1;
    snprintf(buf,cap,"%s",v);
    return (DWORD)strlen(buf);
}
static DWORD GetModuleFileNameA(void *mod,char *out,DWORD cap){
    ssize_t n;
    (void)mod;
    n=readlink("/proc/self/exe",out,cap-1);
    if(n<=0){
        if(!g_exepath[0]) return 0;
        if(realpath(g_exepath,out)==NULL) snprintf(out,cap,"%s",g_exepath);
        return (DWORD)strlen(out);
    }
    out[n]=0;
    return (DWORD)n;
}

/* ---- console ---- */
#define LPC_HIN  ((HANDLE)(intptr_t)1)
#define LPC_HOUT ((HANDLE)(intptr_t)2)
#define LPC_HERR ((HANDLE)(intptr_t)3)
static HANDLE GetStdHandle(DWORD which){
    if(which==STD_INPUT_HANDLE)  return LPC_HIN;
    if(which==STD_OUTPUT_HANDLE) return LPC_HOUT;
    return LPC_HERR;
}
static int lpc_fd(HANDLE h){ return h==LPC_HIN?0 : h==LPC_HOUT?1 : 2; }

static struct termios lpc_saved;
static int   lpc_have_saved=0, lpc_is_raw=0, lpc_atexit=0;
static DWORD lpc_mode = ENABLE_PROCESSED_INPUT|ENABLE_LINE_INPUT|ENABLE_ECHO_INPUT;
static volatile sig_atomic_t lpc_winch=0;
static void lpc_on_winch(int s){ (void)s; lpc_winch=1; }

static void lpc_mouse(int on){
    /* 1000 press/release, 1002 drag motion, 1006 SGR coordinates */
    const char *s = on ? "\x1b[?1000h\x1b[?1002h\x1b[?1006h" : "\x1b[?1006l\x1b[?1002l\x1b[?1000l";
    fputs(s,stdout); fflush(stdout);
}
static void lpc_cooked(void){
    if(lpc_is_raw){
        lpc_mouse(0);
        if(lpc_have_saved) tcsetattr(0,TCSAFLUSH,&lpc_saved);
        lpc_is_raw=0;
    }
}
static void lpc_atexit_restore(void){ lpc_cooked(); }

static BOOL GetConsoleMode(HANDLE h,DWORD *m){
    if(!isatty(lpc_fd(h))) return FALSE;
    if(m) *m = (h==LPC_HIN) ? lpc_mode : 0;
    return TRUE;
}
static BOOL SetConsoleMode(HANDLE h,DWORD m){
    if(h!=LPC_HIN) return TRUE;                 /* output modes: VT is always on */
    if(!isatty(0)) return FALSE;
    lpc_mode=m;
    if(!(m&ENABLE_LINE_INPUT)){
        struct termios t;
        if(!lpc_have_saved){ if(tcgetattr(0,&lpc_saved)!=0) return FALSE; lpc_have_saved=1; }
        t=lpc_saved;
        t.c_iflag &= ~(tcflag_t)(BRKINT|ICRNL|INPCK|ISTRIP|IXON);
        t.c_oflag |= OPOST;                     /* keep \n -> \r\n for printf */
        t.c_cflag |= CS8;
        t.c_lflag &= ~(tcflag_t)(ECHO|ICANON|IEXTEN|ISIG);
        t.c_cc[VMIN]=1; t.c_cc[VTIME]=0;
        tcsetattr(0,TCSAFLUSH,&t);
        if(!lpc_is_raw){
            struct sigaction sa; memset(&sa,0,sizeof sa);
            sa.sa_handler=lpc_on_winch; sigemptyset(&sa.sa_mask);
            sigaction(SIGWINCH,&sa,NULL);
            if(m&ENABLE_MOUSE_INPUT) lpc_mouse(1);
            if(!lpc_atexit){ atexit(lpc_atexit_restore); lpc_atexit=1; }
        }
        lpc_is_raw=1;
    } else lpc_cooked();
    return TRUE;
}
static BOOL GetConsoleScreenBufferInfo(HANDLE h,CONSOLE_SCREEN_BUFFER_INFO *bi){
    struct winsize ws;
    if(ioctl(lpc_fd(h),TIOCGWINSZ,&ws)!=0 || ws.ws_col==0 || ws.ws_row==0) return FALSE;
    memset(bi,0,sizeof *bi);
    bi->dwSize.X=(SHORT)ws.ws_col; bi->dwSize.Y=(SHORT)ws.ws_row;
    bi->srWindow.Left=0; bi->srWindow.Top=0;
    bi->srWindow.Right=(SHORT)(ws.ws_col-1); bi->srWindow.Bottom=(SHORT)(ws.ws_row-1);
    bi->dwMaximumWindowSize=bi->dwSize;
    return TRUE;
}

/* event queue (one escape sequence / UTF-8 char can yield 2 records) */
static INPUT_RECORD lpc_q[8]; static int lpc_qh=0, lpc_qn=0;
static DWORD lpc_btn=0;     /* mouse buttons currently held */
static void lpc_push(const INPUT_RECORD *r){
    if(lpc_qn<8){ lpc_q[(lpc_qh+lpc_qn)%8]=*r; lpc_qn++; }
}
static void lpc_key(WORD vk,WCHAR ch,DWORD ctl){
    INPUT_RECORD r; memset(&r,0,sizeof r);
    r.EventType=KEY_EVENT;
    r.Event.KeyEvent.bKeyDown=TRUE; r.Event.KeyEvent.wRepeatCount=1;
    r.Event.KeyEvent.wVirtualKeyCode=vk;
    r.Event.KeyEvent.uChar.UnicodeChar=ch;
    r.Event.KeyEvent.dwControlKeyState=ctl;
    lpc_push(&r);
}
static void lpc_cp(unsigned long cp,DWORD ctl){
    if(cp>=0x10000UL){
        cp-=0x10000UL;
        lpc_key(0,(WCHAR)(0xD800+(cp>>10)),ctl);
        lpc_key(0,(WCHAR)(0xDC00+(cp&0x3FF)),ctl);
    } else {
        WORD vk=0;
        if(cp>='a'&&cp<='z') vk=(WORD)(cp-32);
        else if((cp>='A'&&cp<='Z')||(cp>='0'&&cp<='9')) vk=(WORD)cp;
        lpc_key(vk,(WCHAR)cp,ctl);
    }
}
/* read one byte, waiting at most ms (-1 = forever, wakes on SIGWINCH); -1 none */
static int lpc_getb(int ms){
    unsigned char c;
    for(;;){
        struct pollfd pf; int r;
        pf.fd=0; pf.events=POLLIN; pf.revents=0;
        r=poll(&pf,1,ms<0?250:ms);
        if(r<0){ if(errno==EINTR){ if(ms<0 && lpc_winch) return -2; continue; } return -3; }
        if(r==0){ if(ms<0){ if(lpc_winch) return -2; continue; } return -1; }
        r=(int)read(0,&c,1);
        if(r==1) return c;
        if(r==0) return -3;                      /* EOF */
        if(errno!=EINTR && errno!=EAGAIN) return -3;
    }
}
static DWORD lpc_mods(int m){   /* xterm modifier param: 1 + shift|alt<<1|ctrl<<2 */
    DWORD c=0; if(m<=1) return 0; m-=1;
    if(m&1) c|=SHIFT_PRESSED;
    if(m&2) c|=LEFT_ALT_PRESSED;
    if(m&4) c|=LEFT_CTRL_PRESSED;
    return c;
}
static void lpc_parse_esc(void){
    int c=lpc_getb(30);
    if(c<0){ lpc_key(0,27,0); return; }          /* lone Esc */
    if(c=='['||c=='O'){
        int ss3=(c=='O'), p[4]={0,0,0,0}, np=0, priv=0, fin;
        for(;;){
            fin=lpc_getb(50);
            if(fin<0){ lpc_key(0,27,0); return; }
            if(fin=='<'){ priv=1; continue; }
            if(fin>='0'&&fin<='9'){ if(np==0) np=1; p[np-1]=p[np-1]*10+(fin-'0'); continue; }
            if(fin==';'){ if(np==0) np=1; if(np<4) np++; continue; }
            break;
        }
        if(priv && (fin=='M'||fin=='m')){        /* SGR mouse: b;x;y */
            INPUT_RECORD r; int b=p[0];
            memset(&r,0,sizeof r);
            r.EventType=MOUSE_EVENT;
            r.Event.MouseEvent.dwMousePosition.X=(SHORT)(p[1]-1);
            r.Event.MouseEvent.dwMousePosition.Y=(SHORT)(p[2]-1);
            if(b&4)  r.Event.MouseEvent.dwControlKeyState|=SHIFT_PRESSED;
            if(b&8)  r.Event.MouseEvent.dwControlKeyState|=LEFT_ALT_PRESSED;
            if(b&16) r.Event.MouseEvent.dwControlKeyState|=LEFT_CTRL_PRESSED;
            if(b&64){                            /* wheel */
                r.Event.MouseEvent.dwEventFlags=MOUSE_WHEELED;
                r.Event.MouseEvent.dwButtonState=((b&1)? (DWORD)(-120)<<16 : (DWORD)120<<16);
            } else {
                int which=b&3;
                DWORD bit= which==0?FROM_LEFT_1ST_BUTTON_PRESSED : which==2?RIGHTMOST_BUTTON_PRESSED
                         : which==1?FROM_LEFT_2ND_BUTTON_PRESSED : 0;
                if(b&32) r.Event.MouseEvent.dwEventFlags=MOUSE_MOVED;
                else if(fin=='M') lpc_btn|=bit;
                else lpc_btn&=~bit;
                r.Event.MouseEvent.dwButtonState=lpc_btn;
            }
            lpc_push(&r);
            return;
        }
        {
            DWORD ctl=lpc_mods(np>=2?p[1]:0);
            if(ss3) ctl=0;
            switch(fin){
            case 'A': lpc_key(VK_UP,0,ctl); return;
            case 'B': lpc_key(VK_DOWN,0,ctl); return;
            case 'C': lpc_key(VK_RIGHT,0,ctl); return;
            case 'D': lpc_key(VK_LEFT,0,ctl); return;
            case 'H': lpc_key(VK_HOME,0,ctl); return;
            case 'F': lpc_key(VK_END,0,ctl); return;
            case 'P': lpc_key(VK_F1,0,ctl); return;
            case 'Q': lpc_key(VK_F2,0,ctl); return;
            case 'R': lpc_key(VK_F3,0,ctl); return;
            case 'S': lpc_key(VK_F4,0,ctl); return;
            case 'Z': lpc_key(0,9,SHIFT_PRESSED); return;     /* shift-tab */
            case '~':
                switch(p[0]){
                case 1: case 7: lpc_key(VK_HOME,0,ctl); return;
                case 4: case 8: lpc_key(VK_END,0,ctl); return;
                case 2:  lpc_key(VK_INSERT,0,ctl); return;
                case 3:  lpc_key(VK_DELETE,0,ctl); return;
                case 5:  lpc_key(VK_PRIOR,0,ctl); return;
                case 6:  lpc_key(VK_NEXT,0,ctl); return;
                case 11: lpc_key(VK_F1,0,ctl); return;
                case 12: lpc_key(VK_F2,0,ctl); return;
                case 13: lpc_key(VK_F3,0,ctl); return;
                case 14: lpc_key(VK_F4,0,ctl); return;
                case 15: lpc_key(VK_F5,0,ctl); return;
                case 17: lpc_key(VK_F6,0,ctl); return;
                case 18: lpc_key(VK_F7,0,ctl); return;
                case 19: lpc_key(VK_F8,0,ctl); return;
                case 20: lpc_key(VK_F9,0,ctl); return;
                case 21: lpc_key(VK_F10,0,ctl); return;
                case 23: lpc_key(VK_F11,0,ctl); return;
                case 24: lpc_key(VK_F12,0,ctl); return;
                }
                return;
            }
        }
        return;                                  /* unknown sequence: drop */
    }
    /* Alt+key */
    if(c>=32 && c<127){ lpc_cp((unsigned long)c,LEFT_ALT_PRESSED); return; }
    lpc_key(0,27,0);
}
static void lpc_parse_byte(int c){
    if(c==27){ lpc_parse_esc(); return; }
    if(c==13||c==10){ lpc_key(0x0D,13,0); return; }
    if(c==127||c==8){ lpc_key(0x08,8,0); return; }
    if(c==9){ lpc_key(0x09,9,0); return; }
    if(c==0){ lpc_key(VK_OEM_3,0,LEFT_CTRL_PRESSED); return; }   /* Ctrl+` / Ctrl+Space */
    if(c<32){ lpc_key((WORD)('A'+c-1),(WCHAR)c,LEFT_CTRL_PRESSED); return; }
    if(c<128){ lpc_cp((unsigned long)c,0); return; }
    {   /* UTF-8 */
        int need = (c>=0xF0)?3 : (c>=0xE0)?2 : (c>=0xC0)?1 : 0;
        unsigned long cp = (c>=0xF0)?(unsigned long)(c&7) : (c>=0xE0)?(unsigned long)(c&15) : (unsigned long)(c&31);
        int k;
        if(!need){ lpc_cp(0xFFFD,0); return; }
        for(k=0;k<need;k++){
            int d=lpc_getb(50);
            if(d<0 || (d&0xC0)!=0x80){ lpc_cp(0xFFFD,0); return; }
            cp=(cp<<6)|(unsigned long)(d&0x3F);
        }
        lpc_cp(cp,0);
    }
}
static BOOL ReadConsoleInputW(HANDLE h,INPUT_RECORD *out,DWORD n,DWORD *got){
    (void)h;
    if(got) *got=0;
    if(n<1) return TRUE;
    while(lpc_qn==0){
        int c;
        if(lpc_winch){
            INPUT_RECORD r; memset(&r,0,sizeof r);
            lpc_winch=0; r.EventType=WINDOW_BUFFER_SIZE_EVENT;
            lpc_push(&r); break;
        }
        c=lpc_getb(-1);
        if(c==-2) continue;                      /* resize: loop emits the event */
        if(c<0) return FALSE;                    /* EOF / error */
        lpc_parse_byte(c);
    }
    *out=lpc_q[lpc_qh]; lpc_qh=(lpc_qh+1)%8; lpc_qn--;
    if(got) *got=1;
    return TRUE;
}

#endif /* !_WIN32 */
#endif /* LUC_POSIX_CONSOLE_H */
