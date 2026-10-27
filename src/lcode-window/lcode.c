/* LCode implementation; included by luc_core.c to share editor internals. */
#if defined(_WIN32)
/* Tiny editor (`edit file.luc`); .luc/.lua/.py, UTF-8 safe */

typedef struct { char *b; size_t len, cap; } EdLine;

/* Parked buffer for unsaved file switch */
typedef struct { EdLine *ln; size_t n, cap, cx, cy, top; int dirty; char *saved; size_t saved_len; char path[1024]; int is_binary; } EdStash;

typedef struct { const char *label; int id, disabled; } EdMenuItem;

/* lcode plugin: a plugin/ folder with plugin.cfg (example: the built-in luc) */
#define ED_PLUGMAX 8
#define ED_SETMAX 8
typedef struct {
    char name[64], author[64], desc[128];
    int enabled, locked;
    char cfg[1024];   /* plugin.cfg path, "" for the built-in fallback */
    char set_id[ED_SETMAX][32], set_label[ED_SETMAX][64];
    int set_val[ED_SETMAX], nset;
} Plug;

/* Configurable LCode shortcut: modifiers plus a virtual-key code */
#define ED_BINDMAX 16
typedef struct { int ctrl, alt, shift; WORD vk; } EdBind;

/* One undo step: whole-buffer snapshot (small files, so this stays cheap) */
typedef struct { char *data; size_t len, cy, cx, top; int dirty; } EdUndo;
#define ED_UNDOMAX 50   /* steps kept per buffer */

#define ED_LOGMAX 64  /* kept log lines (panel shows 1, or 3 when expanded) */
#define ED_SHHIST 32  /* shell command history */
#define ED_PFIND_MAX 1000
#define ED_DIAG_MAX 32

typedef struct {
    char text[192];
    char file[512];
    int line;
} EdPFind;

#define ED_FND_MAX 800
typedef struct {
    char text[160];   /* snippet of the line, or the file name */
    char file[300];   /* shown label (relative path or file name) */
    char full[520];   /* path to open */
    int line;
} EdFnd;

typedef struct {
    int line;
    char msg[128];
} EdDiag;

typedef struct {
    EdLine *ln; size_t n, cap;
    size_t cx, cy;            /* Cursor: byte col, line */
    size_t top;               /* First visible line */
    size_t dleft;             /* H-scroll in display cols */
    int dirty, quit_arm;
    char *saved; size_t saved_len;
    DWORD last_edit_tick;
    int opt_autosave;
    int diag_pending, recovery_pending, view_detached;
    char *clip; size_t cliplen;
    char status[160];
    char path[1024];
    int numw;                 /* Gutter width */
    /* Explorer (left pane) */
    struct ExItem *ex; size_t exn, excap;
    size_t exsel, extop;
    int exsel_ok;             /* 1 = a tree row was explicitly selected */
    char exdir[1024];
    int focus;                /* 0 code, 1 files, 2 terminal */
    int show_ex;              /* Explorer visible */
    /* Explorer bar + terminal panel layout (plugin/config driven) */
    int exw;                  /* Explorer pane width, includes the 1-col divider */
    int ex_side;              /* 0 left, 1 right */
    char ex_title[64];        /* Explorer header text */
    char ex_content[16];      /* What the bar shows: "tree" */
    int show_panel;           /* Terminal panel visible */
    int panel_h;              /* Panel height in rows (title + output + input) */
    char panel_title[64];     /* Terminal panel header text */
    char panel_content[24];   /* What the panel shows: "terminal" */
    int drag_ex, drag_panel, drag_scroll, drag_scroll_off;  /* Live divider/scrollbar drag */
    int vw, vh;               /* Last drawn size */
    unsigned short surr_hi;   /* Pending lead surrogate */
    char **rsav; size_t rsavn, rsavcap;  /* Collapse-all restore set */
    int root_shut;            /* Folder header shut */
    /* Parked dirty buffers */
    EdStash *stash; size_t nstash, scap;
    /* Popup menu */
    int menu; EdMenuItem mitems[8]; int nitems, msel;
    int click_eat;        /* Swallow double-click after pick */
    /* Inline naming input (VSCode-style row inside the tree) */
    int naming; /* 0 off, 1 new file, 2 new dir, 3 rename */
    char namb[256]; size_t namlen, namcur;
    char namdir[1024];   /* Target or parent dir */
    char namold[1024];   /* Rename source path */
    int namidx;           /* Tree row being named (-1 = none) */
    int namsel;           /* Whole name selected (typing replaces it) */
    int namxcol, namyrow; /* Screen cell for the inline caret (1-based) */
    int namtx0;           /* Display col where the name text starts */
    size_t namoff;        /* Left-scroll offset into namb (bytes) */
    size_t namsel0, namtop0;  /* Selection/scroll saved for cancel */
    int namok0;           /* exsel_ok saved for cancel */
    /* File clipboard */
    char fcb[1024]; int fcb_dir, fcb_ok;
    /* Popup anchored at mouse */
    int menu_anch, menu_ax, menu_ay;
    char menutitle[320];
    char menudir[1024];   /* target folder captured when the menu opened */
    /* Pending delete confirm */
    char deltarget[1024]; int delisdir;
    int is_binary;        /* current buffer is a binary placeholder (no save) */
    /* Text selection: anchor (byte col, line) + live end = cursor */
    int sel_on, sel_armed;
    size_t sel_ax, sel_ay;
    int icons_nerd;       /* 1 = Nerd Font devicons, 0 = no icons */
    /* Bottom shell: input line, history, and the log panel above it */
    char shbuf[1024]; size_t shlen, shcur;
    char shhist[ED_SHHIST][1024]; int shn, shpos;
    char shdraft[1024];
    size_t sh_promptw;       /* display width of the shell prompt (for cursor) */
    int sh_quit;             /* shell asked to quit (exit/quit command) */
    char logs[ED_LOGMAX][240]; int logn;
    char lastlog[160];      /* last status already copied into logs */
    int log_expand, log_scroll; /* 1 = 3-row log view, scroll offset into history */
    int last_expand;      /* layout key of the last painted frame */
    /* Undo/redo stacks (cleared on file switch) */
    EdUndo *un, *rn; int un_n, un_cap, rn_n, rn_cap;
    int un_last_op; double un_last_time;
    int opt_guides;       /* tree indent guides (Appearance setting, default off) */
    /* Plugins: built-in luc + plugin.cfg folders, toggled in the :plugin UI */
    Plug plugs[ED_PLUGMAX]; int nplugs, plug_sel, plug_top, show_plugins;
    char plugdir[1024];
    /* Settings modal: left tabs, right content */
    int show_settings, set_tab, set_row, set_left, set_tab_top, set_row_top;
    int set_edit;             /* 1 = editing the focused text setting */
    char set_buf[64]; size_t set_len, set_cur;
    int set_cur_x, set_cur_y; /* caret cell while editing (1-based) */
    int set_full;             /* one full-frame repaint pending after a change */
    /* Keybind editor: 1 = waiting for a combo, 2 = combo captured, Enter confirms */
    int set_key_cap;
    EdBind set_key_pending;
    EdBind binds[ED_BINDMAX];
    /* Suggestions: filtered word list under the cursor (Tab accepts) */
    char sug_items[8][64], sug_tag[8][16], sug_px[64];
    int sug_n, sug_sel, sug_on;
    /* Find bar (Ctrl+F): borderless input + live match count */
    int find_on;             /* matches highlighted */
    int find_bar;            /* bar visible and owns the caret */
    char find_buf[128]; size_t find_len, find_cur;
    int find_n, find_idx;    /* total matches, 1-based current match */
    int find_cx, find_cy;    /* caret cell (1-based) */
    int pf_on, pf_n, pf_total, pf_trunc, pf_sel, pf_top, pf_scanned;
    char pf_buf[256]; size_t pf_len, pf_cur;
    EdPFind *pf_res; int pf_cap;
    /* Find panel (:find = text in file+project, :ff = file names) */
    int fnd_on, fnd_mode, fnd_n, fnd_cap, fnd_total, fnd_scanned, fnd_trunc;
    int fnd_sel, fnd_top, fnd_nofolder, fnd_cur_x, fnd_cur_y, fnd_vis;
    int cmdh_sel;             /* selected ':' command hint */
    char fnd_q[256]; size_t fnd_len, fnd_cur;
    EdFnd *fnd_res;
    char fnd_repl[256];
    int fnd_replace_armed, fnd_replace_valid;
    int diag_n, diag_sel;
    EdDiag diag[ED_DIAG_MAX];
    int tab_n, tab_x0[64], tab_x1[64];
    char tab_path[64][1024];
} Editor;

typedef struct ExItem { char *name; char *full; int isdir; int depth; int expanded; } ExItem;
#define ED_EX_MAX 2000   /* visible tree cap */

#define ED_EXW 28        /* default explorer pane width */
#define ED_EXW_MIN 14    /* keep the tree readable while dragging */
#define ED_PANEL_DEF 4   /* title + 2 output rows + input */
#define ED_PANEL_MIN 3   /* title + 1 output row + input */
#define ED_PANEL_MAX 20

/* Layout geometry: one shared source of truth for draw + mouse */
static int ed_ex_visible(Editor *e){ return e->show_ex && e->exw>1; }
static int ed_ex_width(Editor *e){ return ed_ex_visible(e)?e->exw-1:0; }
static int ed_ex_x(Editor *e,int W){
    return ed_ex_visible(e) ? (e->ex_side ? W-e->exw+1 : 0) : 0;
}
static int ed_div_x(Editor *e,int W){
    return e->ex_side ? W-e->exw : e->exw-1;
}
static int ed_code_x(Editor *e){
    return ed_ex_visible(e) && !e->ex_side ? e->exw : 0;
}
static int ed_scroll_x(Editor *e,int W){
    return ed_ex_visible(e) && e->ex_side ? W-e->exw-1 : W-1;
}
static int ed_code_w(Editor *e,int W){
    int cw=ed_scroll_x(e,W)-ed_code_x(e)-(int)e->numw;
    return cw>8?cw:8;
}
static int ed_panel_rows(Editor *e){ return e->show_panel?e->panel_h:0; }
static int ed_panel_top(int H,Editor *e){ return H-ed_panel_rows(e); }

static void ed_sel_delete(Editor *e);   /* defined with the copy helpers below */
static void ed_undo_push(Editor *e,int op);   /* defined with the undo engine below */
static void ed_refresh_dirty(Editor *e);
static void ed_note_edit(Editor *e);
static char *ed_baseline_from_disk(const char *path,size_t *len);
static void ex_free(Editor *e);   /* defined below */
static int ed_sys_copy(const char *s,size_t len);   /* Windows clipboard below */
static char *ed_sys_paste(size_t *outlen);   /* Windows clipboard below */

static void ed_line_reserve(EdLine *l,size_t extra){
    if(l->len+extra+1>l->cap){
        size_t nc=l->cap?l->cap*2:64;
        while(nc<l->len+extra+1) nc*=2;
        l->b=(char*)lrealloc(l->b,nc);
        l->cap=nc;
    }
}

static void ed_ins_line(Editor *e,size_t at){
    if(e->n==e->cap){ e->cap=e->cap?e->cap*2:16; e->ln=(EdLine*)lrealloc(e->ln,sizeof(EdLine)*e->cap); }
    memmove(&e->ln[at+1],&e->ln[at],(e->n-at)*sizeof(EdLine));
    e->ln[at].b=NULL; e->ln[at].len=0; e->ln[at].cap=0;
    e->n++;
}

static int ed_is_cont(unsigned char c){ return (c&0xC0)==0x80; }

static void ed_clamp(Editor *e){
    if(e->n==0){ ed_ins_line(e,0); }
    if(e->cy>=e->n) e->cy=e->n-1;
    if(e->cx>e->ln[e->cy].len) e->cx=e->ln[e->cy].len;
}

#include "editor/lcode_buffer.c"
#include "language/lcode_syntax.c"
#include "features/lcode_config.c"
#include "features/lcode_search.c"
#include "editor/lcode_render.c"
#include "explorer/lcode_explorer.c"
#include "features/lcode_settings.c"
#include "features/lcode_shell.c"
#include "editor/lcode_input.c"
#else
static void lc_code(const char *start){
    (void)start;
    printf("lcode needs a Windows console for now.\n");
}
#endif
