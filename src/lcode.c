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

/* Keep scroll in range (code rows exclude title/log/shell) */
static int ed_logshow(Editor *e){
    int ph=ed_panel_rows(e);
    return ph>2?ph-2:0;
}
static int ed_crows(int H,Editor *e){
    int r=H-1-ed_panel_rows(e);
    return r>1?r:1;
}
static void ed_clamp_view(Editor *e,int H){
    size_t rows=(size_t)ed_crows(H,e);
    size_t maxtop=e->n>rows?e->n-rows:0;
    size_t erows=rows>2?rows-2:1;
    size_t maxex=e->exn>erows?e->exn-erows:0;
    if(e->top>maxtop) e->top=maxtop;
    if(e->extop>maxex) e->extop=maxex;
}
static void ed_scrollbar_geom(Editor *e,int rows,int *start,int *length){
    size_t total=e->n, max_top;
    int len,slot;
    if(rows<1||total<=(size_t)rows){ *start=0; *length=rows; return; }
    len=(int)((long long)rows*rows/(long long)total);
    if(len<1) len=1;
    if(len>rows) len=rows;
    max_top=(size_t)rows<len?0:(size_t)(rows-len);
    slot=(int)((long long)e->top*(long long)max_top/(long long)(total-(size_t)rows));
    if(slot<0) slot=0;
    if(slot>(int)max_top) slot=(int)max_top;
    *start=slot; *length=len;
}
static void ed_scrollbar_set(Editor *e,int rows,int slot,int length){
    size_t oldtop=e->top, maxline, maxslot;
    size_t rel=e->cy>=oldtop?e->cy-oldtop:0;
    if(e->n<=(size_t)rows){ e->top=0; return; }
    maxline=e->n-(size_t)rows;
    maxslot=(size_t)(rows-length);
    if(slot<0) slot=0;
    if((size_t)slot>maxslot) slot=(int)maxslot;
    e->top=maxslot?(size_t)((long long)slot*(long long)maxline/(long long)maxslot):0;
    e->cy=e->top+rel;
    if(e->cy>=e->n) e->cy=e->n-1;
    if(e->cy>=e->top+(size_t)rows) e->cy=e->top+(size_t)rows-1;
    e->status[0]=0;
}

static void ed_move_left(Editor *e){
    ed_clamp(e);
    if(e->cx>0){ do e->cx--; while(e->cx>0 && ed_is_cont((unsigned char)e->ln[e->cy].b[e->cx])); }
}

static void ed_move_right(Editor *e){
    ed_clamp(e);
    { size_t L=e->ln[e->cy].len;
      if(e->cx<L){ do e->cx++; while(e->cx<L && ed_is_cont((unsigned char)e->ln[e->cy].b[e->cx])); } }
}

/* Byte width: tabs expand, controls 1, continuations 0 */
static int ed_chw(unsigned char c,int col){
    if(c=='\t') return 4-(col%4);
    if(c<32||c==127) return 1;
    if((c&0xC0)==0x80) return 0;
    return 1;
}

/* Printable form; binaries stay viewable */
static unsigned char ed_chshow(unsigned char c){
    if(c=='\t'||c>=32&&c!=127) return c;
    return '.';
}

/* Cursor display col */
static size_t ed_dcol(Editor *e){
    ed_clamp(e);
    { size_t d=0,col=0,k=0; char *b=e->ln[e->cy].b;
      while(k<e->cx){ int w=ed_chw((unsigned char)b[k],(int)col); d+=(size_t)w; col+=(size_t)w; k++; }
      return d; }
}

/* Byte offset for display col */
static size_t ed_byte_at(char *b,size_t len,size_t dc){
    size_t k=0,d=0,col=0;
    while(k<len&&d<dc){ int w=ed_chw((unsigned char)b[k],(int)col); d+=(size_t)w; col+=(size_t)w; k++; }
    while(k<len&&ed_is_cont((unsigned char)b[k])) k++;
    return k;
}

static void ed_insert_bytes(Editor *e,const char *s,size_t m){
    ed_clamp(e);
    if(e->is_binary){ snprintf(e->status,sizeof e->status,"binary file is read-only"); return; }
    ed_undo_push(e,m<=4?1:0);
    if(e->sel_on) ed_sel_delete(e);
    if(m==0) return;
    { EdLine *l=&e->ln[e->cy];
      ed_line_reserve(l,m);
      memmove(l->b+e->cx+m,l->b+e->cx,l->len-e->cx+1);
      memcpy(l->b+e->cx,s,m);
      l->len+=m; e->cx+=m; e->dirty=1; ed_note_edit(e); e->diag_pending=1; e->recovery_pending=1; e->status[0]=0; }
}

static void ed_backspace(Editor *e){
    ed_clamp(e);
    if(e->is_binary){ snprintf(e->status,sizeof e->status,"binary file is read-only"); return; }
    ed_undo_push(e,1);
    if(e->sel_on){ ed_sel_delete(e); return; }
    if(e->cx>0){
        size_t from=e->cx; ed_move_left(e);
        { EdLine *l=&e->ln[e->cy]; size_t cnt=from-e->cx;
          memmove(l->b+e->cx,l->b+from,l->len-from+1);
          l->len-=cnt; e->dirty=1; ed_note_edit(e); e->diag_pending=1; e->recovery_pending=1; e->status[0]=0; }
    } else if(e->cy>0){
        size_t py=e->cy-1;
        EdLine *p=&e->ln[py], *c=&e->ln[e->cy];
        e->cx=p->len;
        ed_line_reserve(p,c->len);
        memcpy(p->b+p->len,c->b,c->len+1); p->len+=c->len;
        free(c->b);
        memmove(&e->ln[e->cy],&e->ln[e->cy+1],(e->n-e->cy-1)*sizeof(EdLine));
        e->n--; e->cy=py; e->dirty=1; ed_note_edit(e); e->diag_pending=1; e->recovery_pending=1; e->status[0]=0;
    }
}

static void ed_newline(Editor *e){
    ed_clamp(e);
    if(e->is_binary){ snprintf(e->status,sizeof e->status,"binary file is read-only"); return; }
    ed_undo_push(e,0);
    if(e->sel_on) ed_sel_delete(e);
    { EdLine *l=&e->ln[e->cy];
      ed_ins_line(e,e->cy+1);
      l=&e->ln[e->cy];
      EdLine *nl=&e->ln[e->cy+1];
      size_t tail=l->len-e->cx;
      ed_line_reserve(nl,tail);
      memcpy(nl->b,l->b+e->cx,tail); nl->b[tail]=0; nl->len=tail;
      l->b[e->cx]=0; l->len=e->cx;
      e->cy++; e->cx=0; e->dirty=1; ed_note_edit(e); e->diag_pending=1; e->recovery_pending=1; e->status[0]=0; }
}

static void ed_copy_line(Editor *e){
    ed_clamp(e);
    { EdLine *l=&e->ln[e->cy];
      free(e->clip); e->clip=NULL; e->cliplen=0;
      if(l->len){ e->clip=(char*)lmalloc(l->len+1); memcpy(e->clip,l->b,l->len+1); e->cliplen=l->len; }
      snprintf(e->status,sizeof e->status,"copied line %d",(int)e->cy+1);
      if(e->clip) ed_sys_copy(e->clip,e->cliplen); }
}

/* Text selection: ordered (sy,sx)-(ey,ex) between anchor and cursor */
static void ed_sel_norm(Editor *e,size_t *sy,size_t *sx,size_t *ey,size_t *ex){
    *sy=e->sel_ay; *sx=e->sel_ax; *ey=e->cy; *ex=e->cx;
    if(*ey<*sy||(*ey==*sy&&*ex<*sx)){
        size_t t=*sy;*sy=*ey;*ey=t; t=*sx;*sx=*ex;*ex=t;
    }
    /* stale anchor (file shrank underneath): clamp into range */
    if(e->n==0){ *sy=*ey=0; *sx=*ex=0; return; }
    if(*sy>=e->n) *sy=e->n-1;
    if(*ey>=e->n) *ey=e->n-1;
    if(*sx>e->ln[*sy].len) *sx=e->ln[*sy].len;
    if(*ex>e->ln[*ey].len) *ex=e->ln[*ey].len;
}
static int ed_sel_empty(Editor *e){
    size_t sy,sx,ey,ex;
    if(!e->sel_on) return 1;
    ed_sel_norm(e,&sy,&sx,&ey,&ex);
    return sy==ey&&sx==ex;
}
/* Delete the selected range; cursor lands on its start */
static void ed_sel_delete(Editor *e){
    size_t sy,sx,ey,ex,k;
    if(ed_sel_empty(e)) return;
    ed_sel_norm(e,&sy,&sx,&ey,&ex);
    if(sy==ey){
        EdLine *l=&e->ln[sy];
        memmove(l->b+sx,l->b+ex,l->len-ex+1);
        l->len-=ex-sx;
    } else {
        EdLine *a=&e->ln[sy], *b=&e->ln[ey];
        ed_line_reserve(a,b->len-ex);
        memcpy(a->b+sx,b->b+ex,b->len-ex);
        a->len=sx+(b->len-ex); a->b[a->len]=0;
        for(k=sy+1;k<=ey;k++) free(e->ln[k].b);
        memmove(&e->ln[sy+1],&e->ln[ey+1],(e->n-ey-1)*sizeof(EdLine));
        e->n-=ey-sy;
    }
    e->cy=sy; e->cx=sx; e->sel_on=0;
    e->dirty=1; ed_note_edit(e); e->diag_pending=1; e->recovery_pending=1; e->status[0]=0;
}
/* Copy selection (with newlines) into the line clipboard */static void ed_copy_sel(Editor *e){
    size_t sy,sx,ey,ex,k,total=0;
    char *p;
    ed_sel_norm(e,&sy,&sx,&ey,&ex);
    for(k=sy;k<=ey;k++){
        EdLine *l=&e->ln[k];
        size_t a=(k==sy)?sx:0, b=(k==ey)?ex:l->len;
        if(b>a) total+=b-a;
        if(k<ey) total++;
    }
    free(e->clip); e->clip=NULL; e->cliplen=0;
    if(!total){ snprintf(e->status,sizeof e->status,"nothing selected"); return; }
    p=e->clip=(char*)lmalloc(total+1);
    for(k=sy;k<=ey;k++){
        EdLine *l=&e->ln[k];
        size_t a=(k==sy)?sx:0, b=(k==ey)?ex:l->len;
        if(b>a){ memcpy(p,l->b+a,b-a); p+=b-a; }
        if(k<ey) *p++='\n';
    }
    *p=0; e->cliplen=total;
    snprintf(e->status,sizeof e->status,"copied %d char(s)",(int)total);
    if(e->clip) ed_sys_copy(e->clip,e->cliplen);
}

/* Windows clipboard (UTF-8); lets ^C/^V cross into other apps */
static int ed_sys_copy(const char *s,size_t len){
    int wlen;
    HGLOBAL h; wchar_t *w;
    if(len==0||len>0x7FFFFF00) return 0;
    wlen=MultiByteToWideChar(CP_UTF8,0,s,(int)len,NULL,0);
    if(wlen<=0) return 0;
    h=GlobalAlloc(GMEM_MOVEABLE,(wlen+1)*sizeof(wchar_t));
    if(!h) return 0;
    w=(wchar_t*)GlobalLock(h);
    MultiByteToWideChar(CP_UTF8,0,s,(int)len,w,wlen); w[wlen]=0;
    GlobalUnlock(h);
    if(!OpenClipboard(NULL)){ GlobalFree(h); return 0; }
    EmptyClipboard();
    SetClipboardData(CF_UNICODETEXT,h);   /* system owns h now */
    CloseClipboard();
    return 1;
}
/* Returns malloc'd UTF-8 text or NULL; caller frees */
static char *ed_sys_paste(size_t *outlen){
    HGLOBAL h; const wchar_t *w; int n; char *s;
    *outlen=0;
    if(!OpenClipboard(NULL)) return NULL;
    h=GetClipboardData(CF_UNICODETEXT);
    if(!h){ CloseClipboard(); return NULL; }
    w=(const wchar_t*)GlobalLock(h);
    if(!w){ CloseClipboard(); return NULL; }
    n=WideCharToMultiByte(CP_UTF8,0,w,-1,NULL,0,NULL,NULL);
    if(n<=1){ GlobalUnlock(h); CloseClipboard(); return NULL; }
    s=(char*)malloc((size_t)n);
    if(!s){ GlobalUnlock(h); CloseClipboard(); return NULL; }
    WideCharToMultiByte(CP_UTF8,0,w,-1,s,n,NULL,NULL);
    GlobalUnlock(h); CloseClipboard();
    *outlen=(size_t)(n-1);
    return s;
}
static int ed_clip_has_text(Editor *e){
    size_t n=0;
    char *s=ed_sys_paste(&n);
    if(s){ free(s); return n>0; }
    return e->clip&&e->cliplen>0;
}

/* Undo/redo: whole-buffer snapshots, typing coalesced by time */
static void ed_undo_ser(Editor *e,EdUndo *u){
    size_t total=0, k;
    char *p;
    for(k=0;k<e->n;k++) total+=e->ln[k].len+1;
    p=(char*)lmalloc(total+1);
    u->data=p; u->len=total;
    for(k=0;k<e->n;k++){
        memcpy(p,e->ln[k].b,e->ln[k].len); p+=e->ln[k].len; *p++='\n';
    }
    *p=0;
    u->cy=e->cy; u->cx=e->cx; u->top=e->top; u->dirty=e->dirty;
}
static void ed_undo_clear(Editor *e){
    int i;
    for(i=0;i<e->un_n;i++) free(e->un[i].data);
    for(i=0;i<e->rn_n;i++) free(e->rn[i].data);
    free(e->un); free(e->rn);
    e->un=e->rn=NULL; e->un_n=e->un_cap=e->rn_n=e->rn_cap=0;
    e->un_last_op=-1; e->un_last_time=0;
}
/* Snapshot before a mutation; op=1 typing-like (coalesced), op=0 always */
static void ed_undo_push(Editor *e,int op){
    EdUndo u;
    double now=luc_now();
    ed_clamp(e);
    if(op==1&&e->un_last_op==1&&now-e->un_last_time<1.5) return;
    ed_undo_ser(e,&u);
    if(e->un_n>=ED_UNDOMAX){
        free(e->un[0].data);
        memmove(&e->un[0],&e->un[1],sizeof(EdUndo)*(ED_UNDOMAX-1));
        e->un_n=ED_UNDOMAX-1;
    }
    if(e->un_n==e->un_cap){
        e->un_cap=e->un_cap?e->un_cap*2:16;
        e->un=(EdUndo*)lrealloc(e->un,sizeof(EdUndo)*(size_t)e->un_cap);
    }
    e->un[e->un_n++]=u;
    { int i; for(i=0;i<e->rn_n;i++) free(e->rn[i].data); e->rn_n=0; }
    e->un_last_op=op; e->un_last_time=now;
}
static void ed_undo_apply(Editor *e,EdUndo *u){
    size_t k, s=0, n=1, at=0;
    for(k=0;k<e->n;k++) free(e->ln[k].b);
    free(e->ln); e->ln=NULL; e->n=0; e->cap=0;
    for(k=0;k<u->len;k++) if(u->data[k]=='\n') n++;
    if(n>0&&u->len>0&&u->data[u->len-1]=='\n') n--;   /* drop trailing empty */
    if(n==0) n=1;
    e->ln=(EdLine*)lmalloc(sizeof(EdLine)*n); e->cap=n;
    for(k=0;k<n;k++){ e->ln[k].b=NULL; e->ln[k].len=0; e->ln[k].cap=0; }
    e->n=n;
    for(k=0;k<n;k++){
        size_t j=at;
        while(j<u->len&&u->data[j]!='\n') j++;
        { size_t el=j-at;
          ed_line_reserve(&e->ln[k],el);
          if(el) memcpy(e->ln[k].b,u->data+at,el);
          e->ln[k].b[el]=0; e->ln[k].len=el; }
        at=j+1;
    }
    e->cy=u->cy; e->cx=u->cx; e->top=u->top; e->dirty=u->dirty;
    e->sel_on=0; e->status[0]=0;
    e->diag_pending=1; e->recovery_pending=1;
    ed_clamp(e);
    ed_note_edit(e);
}
static void ed_undo(Editor *e){
    EdUndo cur;
    if(!e->un_n){ snprintf(e->status,sizeof e->status,"nothing to undo"); return; }
    ed_undo_ser(e,&cur);
    if(e->rn_n==e->rn_cap){
        e->rn_cap=e->rn_cap?e->rn_cap*2:16;
        e->rn=(EdUndo*)lrealloc(e->rn,sizeof(EdUndo)*(size_t)e->rn_cap);
    }
    e->rn[e->rn_n++]=cur;
    { EdUndo u=e->un[--e->un_n];
      ed_undo_apply(e,&u); free(u.data); }
    e->un_last_op=-1;
}
static void ed_redo(Editor *e){
    EdUndo cur, u;
    if(!e->rn_n){ snprintf(e->status,sizeof e->status,"nothing to redo"); return; }
    ed_undo_ser(e,&cur);
    if(e->un_n>=ED_UNDOMAX){
        free(e->un[0].data);
        memmove(&e->un[0],&e->un[1],sizeof(EdUndo)*(ED_UNDOMAX-1));
        e->un_n=ED_UNDOMAX-1;
    }
    if(e->un_n==e->un_cap){
        e->un_cap=e->un_cap?e->un_cap*2:16;
        e->un=(EdUndo*)lrealloc(e->un,sizeof(EdUndo)*(size_t)e->un_cap);
    }
    e->un[e->un_n++]=cur;
    u=e->rn[--e->rn_n];
    ed_undo_apply(e,&u); free(u.data);
    e->un_last_op=-1;
}

static void ed_load(Editor *e);
static int ed_save_to(EdLine *ln,size_t n,const char *path){
    FILE *f=fopen(path,"wb");
    size_t i;
    if(!f) return 0;
    for(i=0;i<n;i++){ if(ln[i].len) fwrite(ln[i].b,1,ln[i].len,f); fputc('\n',f); }
    fclose(f);
    return 1;
}

static char *ed_snapshot(EdLine *ln,size_t n,size_t *outlen){
    size_t i,total=0; char *data,*p;
    for(i=0;i<n;i++){
        if(ln[i].len>SIZE_MAX-total-1) return NULL;
        total+=ln[i].len+1;
    }
    data=(char*)lmalloc(total+1); p=data;
    for(i=0;i<n;i++){
        if(ln[i].len){ memcpy(p,ln[i].b,ln[i].len); p+=ln[i].len; }
        *p++='\n';
    }
    *p=0;
    *outlen=total;
    return data;
}
static void ed_saved_set(Editor *e){
    char *data; size_t len=0;
    data=ed_snapshot(e->ln,e->n,&len);
    free(e->saved); e->saved=data; e->saved_len=len; e->dirty=0;
}
static void ed_refresh_dirty(Editor *e){
    char *data; size_t len=0;
    if(!e->saved){ e->dirty=1; return; }
    data=ed_snapshot(e->ln,e->n,&len);
    e->dirty=len!=e->saved_len||memcmp(data,e->saved,len)!=0;
    free(data);
}
static void ed_note_edit(Editor *e){
    e->last_edit_tick=GetTickCount();
    ed_refresh_dirty(e);
}

static int ed_save(Editor *e){
    if(!e->path[0]){ snprintf(e->status,sizeof e->status,"no file open"); return 0; }
    if(e->is_binary){ snprintf(e->status,sizeof e->status,"cannot save a binary file"); return 0; }
    if(!ed_save_to(e->ln,e->n,e->path)){ snprintf(e->status,sizeof e->status,"cannot write '%s'",e->path); return 0; }
    ed_saved_set(e); e->quit_arm=0;
    e->recovery_pending=1;
    snprintf(e->status,sizeof e->status,"saved %d line(s) to '%s'",(int)e->n,e->path);
    return 1;
}

/* Worth keeping: named or non-empty */
static int ed_cur_worth(Editor *e){
    return e->path[0] || e->n>1 || (e->n==1 && e->ln[0].len>0);
}

static void ed_stash_current(Editor *e){
    EdStash *s;
    if(!ed_cur_worth(e)){
        size_t i;
        for(i=0;i<e->n;i++) free(e->ln[i].b);
        free(e->ln); free(e->saved);
        e->ln=NULL; e->n=e->cap=0; e->saved=NULL; e->saved_len=0;
        return;
    }
    if(e->nstash==e->scap){ e->scap=e->scap?e->scap*2:4;
        e->stash=(EdStash*)lrealloc(e->stash,sizeof(EdStash)*e->scap); }
    s=&e->stash[e->nstash++];
    s->ln=e->ln; s->n=e->n; s->cap=e->cap; s->cx=e->cx; s->cy=e->cy; s->top=e->top;
    s->dirty=e->dirty; s->is_binary=e->is_binary;
    s->saved=e->saved; s->saved_len=e->saved_len;
    snprintf(s->path,sizeof s->path,"%s",e->path);
    e->ln=NULL; e->n=0; e->cap=0; e->cx=0; e->cy=0; e->top=0; e->dleft=0; e->dirty=0;
    e->saved=NULL; e->saved_len=0;
    e->is_binary=0;
    e->recovery_pending=1;
}

static int ed_unstash(Editor *e,const char *path){
    size_t i;
    for(i=0;i<e->nstash;i++) if(!strcmp(e->stash[i].path,path)){
        e->ln=e->stash[i].ln; e->n=e->stash[i].n; e->cap=e->stash[i].cap;
        e->cx=e->stash[i].cx; e->cy=e->stash[i].cy; e->top=e->stash[i].top;
        e->dirty=e->stash[i].dirty; e->is_binary=e->stash[i].is_binary;
        e->saved=e->stash[i].saved; e->saved_len=e->stash[i].saved_len;
        snprintf(e->path,sizeof e->path,"%s",path);
        memmove(&e->stash[i],&e->stash[i+1],(e->nstash-i-1)*sizeof(EdStash));
        e->nstash--;
        return 1;
    }
    return 0;
}

/* Open path: reuse open, restore parked, else load */
static void ed_open_path(Editor *e,const char *full){
    if(e->path[0] && !strcmp(e->path,full)){ e->focus=0; return; }
    ed_stash_current(e);
    ed_undo_clear(e);   /* undo history belongs to the old buffer */
    e->sug_on=0;        /* suggestions belong to the old buffer too */
    e->find_on=0; e->find_bar=0; e->find_n=0; e->find_idx=0; e->find_cx=0; e->find_cy=0;
    e->diag_n=0; e->diag_sel=-1;
    if(ed_unstash(e,full)){ e->focus=0; e->quit_arm=0; return; }
    snprintf(e->path,sizeof e->path,"%s",full);
    e->cx=0; e->cy=0; e->top=0; e->dleft=0; e->dirty=0; e->quit_arm=0;
    ed_load(e);
    ed_saved_set(e);
    e->focus=0;
    e->recovery_pending=1;
}

static int ed_recovery_path(char *out,size_t cap){
    char root[1024],dir[1100]; DWORD n;
    n=GetEnvironmentVariableA("LOCALAPPDATA",root,sizeof root);
    if(!n||n>=sizeof root||!root[0]) return 0;
    if(snprintf(dir,sizeof dir,"%s\\LUC",root)>=(int)sizeof dir||
       snprintf(out,cap,"%s\\LCode\\recovery.bin",dir)>=(int)cap) return 0;
    if(!CreateDirectoryA(dir,NULL)&&GetLastError()!=ERROR_ALREADY_EXISTS) return 0;
    if(snprintf(dir,sizeof dir,"%s\\LCode",root)>=(int)sizeof dir) return 0;
    if(!CreateDirectoryA(dir,NULL)&&GetLastError()!=ERROR_ALREADY_EXISTS) return 0;
    return 1;
}
static int ed_recovery_write(FILE *f,const char *path,EdLine *ln,size_t n,
                             size_t cy,size_t cx,size_t top){
    uint32_t pl=(uint32_t)strlen(path), nl=(uint32_t)n, a,b,c;
    size_t i;
    if(!pl||pl>=1024||!nl||n>200000||cy>UINT32_MAX||cx>UINT32_MAX||top>UINT32_MAX) return 0;
    a=(uint32_t)cy; b=(uint32_t)cx; c=(uint32_t)top;
    if(fwrite(&pl,sizeof pl,1,f)!=1||fwrite(&nl,sizeof nl,1,f)!=1||
       fwrite(&a,sizeof a,1,f)!=1||fwrite(&b,sizeof b,1,f)!=1||
       fwrite(&c,sizeof c,1,f)!=1||fwrite(path,1,pl,f)!=pl) return 0;
    for(i=0;i<n;i++){
        uint32_t len=(uint32_t)ln[i].len;
        if(ln[i].len>16u*1024u*1024u||
           fwrite(&len,sizeof len,1,f)!=1||
           (len&&fwrite(ln[i].b,1,len,f)!=len)) return 0;
    }
    return 1;
}
static void ed_recovery_save(Editor *e){
    char path[1200],tmp[1240];
    static const char magic[8]={'L','C','R','D','R','A','F','T'};
    FILE *f; size_t i; int any=0,ok=1;
    if(!e->recovery_pending) return;
    e->recovery_pending=0;
    if(!ed_recovery_path(path,sizeof path)){
        snprintf(e->status,sizeof e->status,"recovery: cannot locate Local AppData");
        return;
    }
    if((e->dirty&&e->path[0]&&!e->is_binary)) any=1;
    for(i=0;i<e->nstash;i++)
        if(e->stash[i].dirty&&e->stash[i].path[0]&&!e->stash[i].is_binary) any=1;
    if(!any){ DeleteFileA(path); return; }
    if(snprintf(tmp,sizeof tmp,"%s.tmp",path)>=(int)sizeof tmp){
        snprintf(e->status,sizeof e->status,"recovery: path is too long"); return;
    }
    f=fopen(tmp,"wb");
    if(!f){ snprintf(e->status,sizeof e->status,"recovery: cannot write draft journal"); return; }
    if(fwrite(magic,1,sizeof magic,f)!=sizeof magic) ok=0;
    if(ok&&e->dirty&&e->path[0]&&!e->is_binary)
        ok=ed_recovery_write(f,e->path,e->ln,e->n,e->cy,e->cx,e->top);
    for(i=0;ok&&i<e->nstash;i++)
        if(e->stash[i].dirty&&e->stash[i].path[0]&&!e->stash[i].is_binary)
            ok=ed_recovery_write(f,e->stash[i].path,e->stash[i].ln,e->stash[i].n,
                                 e->stash[i].cy,e->stash[i].cx,e->stash[i].top);
    { long size=ftell(f); if(size<0||size>64L*1024L*1024L) ok=0; }
    if(fclose(f)!=0) ok=0;
    if(!ok||!MoveFileExA(tmp,path,MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH)){
        DeleteFileA(tmp);
        snprintf(e->status,sizeof e->status,"recovery: could not save unsaved drafts");
    }
}
static int ed_recovery_read(FILE *f,EdStash *s){
    uint32_t pl,nl,cy,cx,top; size_t i,got;
    memset(s,0,sizeof *s);
    got=fread(&pl,1,sizeof pl,f);
    if(got==0&&feof(f)) return 0;
    if(got!=sizeof pl) return -1;
    if(pl==0||pl>=sizeof s->path||fread(&nl,sizeof nl,1,f)!=1||
       fread(&cy,sizeof cy,1,f)!=1||fread(&cx,sizeof cx,1,f)!=1||
       fread(&top,sizeof top,1,f)!=1||nl==0||nl>200000) return -1;
    if(fread(s->path,1,pl,f)!=pl) return -1;
    s->path[pl]=0;
    s->ln=(EdLine*)calloc(nl,sizeof(EdLine));
    if(!s->ln) return -1;
    s->n=s->cap=nl; s->cy=cy; s->cx=cx; s->top=top; s->dirty=1;
    for(i=0;i<nl;i++){
        uint32_t len;
        if(fread(&len,sizeof len,1,f)!=1||len>16u*1024u*1024u) return -1;
        if(len){
            s->ln[i].b=(char*)malloc((size_t)len+1);
            if(!s->ln[i].b||fread(s->ln[i].b,1,len,f)!=len) return -1;
            s->ln[i].b[len]=0; s->ln[i].len=s->ln[i].cap=len;
        } else {
            s->ln[i].b=(char*)calloc(1,1);
            if(!s->ln[i].b) return -1;
        }
    }
    return 1;
}
static void ed_recovery_set_current(Editor *e,EdStash *s){
    size_t i; int same_path=e->path[0]&&!_stricmp(e->path,s->path);
    for(i=0;i<e->n;i++) free(e->ln[i].b);
    free(e->ln);
    e->ln=s->ln; e->n=s->n; e->cap=s->cap;
    e->cx=s->cx; e->cy=s->cy; e->top=s->top; e->dirty=1; e->is_binary=0;
    snprintf(e->path,sizeof e->path,"%s",s->path);
    if(!same_path){
        free(e->saved);
        e->saved=ed_baseline_from_disk(e->path,&e->saved_len);
    }
    e->focus=0;
    e->diag_pending=1;
    e->last_edit_tick=GetTickCount();
    memset(s,0,sizeof *s);
    ed_clamp(e);
}
static void ed_recovery_load(Editor *e){
    char path[1200],magic[8]; FILE *f; int restored=0,active=0;
    if(!ed_recovery_path(path,sizeof path)) return;
    f=fopen(path,"rb"); if(!f) return;
    if(fseek(f,0,SEEK_END)!=0||ftell(f)<0||ftell(f)>64L*1024L*1024L||
       fseek(f,8,SEEK_SET)!=0){
        fclose(f); snprintf(e->status,sizeof e->status,"recovery: draft journal is too large"); return;
    }
    if(fread(magic,1,sizeof magic,f)!=sizeof magic||memcmp(magic,"LCRDRAFT",8)){
        fclose(f); snprintf(e->status,sizeof e->status,"recovery: invalid draft journal ignored"); return;
    }
    for(;;){
        EdStash s; int rc=ed_recovery_read(f,&s); size_t i; int duplicate=0;
        if(rc==0) break;
        if(rc<0){
            if(s.ln){ for(i=0;i<s.n;i++) free(s.ln[i].b); free(s.ln); }
            fclose(f); snprintf(e->status,sizeof e->status,"recovery: draft journal is incomplete"); return;
        }
        if(e->path[0]&&!_stricmp(e->path,s.path)){
            ed_recovery_set_current(e,&s); active=1;
        } else if(!e->path[0]&&!active){
            ed_recovery_set_current(e,&s); active=1;
        } else {
            for(i=0;i<e->nstash;i++) if(!_stricmp(e->stash[i].path,s.path)){ duplicate=1; break; }
            if(!duplicate){
                s.saved=ed_baseline_from_disk(s.path,&s.saved_len);
                if(e->nstash==e->scap){ e->scap=e->scap?e->scap*2:4; e->stash=(EdStash*)lrealloc(e->stash,sizeof(EdStash)*e->scap); }
                e->stash[e->nstash++]=s; memset(&s,0,sizeof s);
            }
        }
        if(s.ln){ for(i=0;i<s.n;i++) free(s.ln[i].b); free(s.ln); }
        restored++;
    }
    fclose(f);
    if(restored){
        e->recovery_pending=1;
        snprintf(e->status,sizeof e->status,"recovered %d unsaved draft(s)",restored);
    }
}

static int ed_unsaved_count(Editor *e){
    size_t i; int c=(e->dirty&&e->path[0])?1:0;
    for(i=0;i<e->nstash;i++) if(e->stash[i].dirty) c++;
    return c;
}

static void ed_save_all(Editor *e){
    size_t i; int done=0, unnamed=0;
    if(e->dirty && !e->is_binary){
        if(e->path[0] && ed_save_to(e->ln,e->n,e->path)){ ed_saved_set(e); done++; }
        else unnamed++;
    }
    for(i=0;i<e->nstash;i++) if(e->stash[i].dirty && !e->stash[i].is_binary){
        if(ed_save_to(e->stash[i].ln,e->stash[i].n,e->stash[i].path)){
            char *saved=ed_snapshot(e->stash[i].ln,e->stash[i].n,&e->stash[i].saved_len);
            free(e->stash[i].saved); e->stash[i].saved=saved; e->stash[i].dirty=0; done++;
        }
        else unnamed++;
    }
    e->quit_arm=0;
    e->recovery_pending=1;
    if(unnamed) snprintf(e->status,sizeof e->status,"saved %d, %d unnamed skipped",done,unnamed);
    else snprintf(e->status,sizeof e->status,"saved %d file(s)",done);
}

static int ed_path_dirty(Editor *e,const char *full){
    size_t i;
    if(e->dirty && e->path[0] && !strcmp(e->path,full)) return 1;
    for(i=0;i<e->nstash;i++) if(e->stash[i].dirty && !strcmp(e->stash[i].path,full)) return 1;
    return 0;
}

static int ed_dir_dirty(Editor *e,const char *full){
    size_t i, n=strlen(full);
    if(!n) return 0;
    if(e->dirty && e->path[0] && !strncmp(e->path,full,n)
       && (e->path[n]=='\\'||e->path[n]=='/')) return 1;
    for(i=0;i<e->nstash;i++) if(e->stash[i].dirty && !strncmp(e->stash[i].path,full,n)
       && (e->stash[i].path[n]=='\\'||e->stash[i].path[n]=='/')) return 1;
    return 0;
}

/* Fix open and parked paths after rename */
static void ed_rename_paths(Editor *e,const char *oldp,const char *newp){
    size_t i;
    if(e->path[0] && !strcmp(e->path,oldp)) snprintf(e->path,sizeof e->path,"%s",newp);
    for(i=0;i<e->nstash;i++) if(!strcmp(e->stash[i].path,oldp))
        snprintf(e->stash[i].path,sizeof e->stash[i].path,"%s",newp);
}

static void ed_load(Editor *e){
    int len=0;
    char *src=read_file(e->path,&len);
    e->n=0; e->cap=0; e->ln=NULL; e->is_binary=0;
    if(!src){ ed_ins_line(e,0); snprintf(e->status,sizeof e->status,"new file '%s'",e->path); return; }
    { int i;
      for(i=0;i<len;i++) if(!src[i]) break;
      if(i<len){
          ed_ins_line(e,0);
          { const char *msg="[binary file - not shown]";
            EdLine *l=&e->ln[0];
            ed_line_reserve(l,strlen(msg));
            memcpy(l->b,msg,strlen(msg)+1); l->len=strlen(msg); }
          e->is_binary=1;
          snprintf(e->status,sizeof e->status,"binary file '%s' - not opened",e->path);
          free(src);
          return;
      } }
    { int i=0,s=0;
      for(i=0;i<=len;i++){
          if(i==len||src[i]=='\n'){
              int el=i-s;
              if(el>0&&src[i-1]=='\r') el--;
              ed_ins_line(e,e->n);
              EdLine *l=&e->ln[e->n-1];
              ed_line_reserve(l,(size_t)el);
              if(el) memcpy(l->b,src+s,(size_t)el);
              l->b[el]=0; l->len=(size_t)el;
              s=i+1;
          }
      }
      /* "a\n" is one line: drop phantom tail */
      if(e->n>1&&e->ln[e->n-1].len==0){ free(e->ln[e->n-1].b); e->n--; }
      if(e->n==0) ed_ins_line(e,0);
      snprintf(e->status,sizeof e->status,"loaded '%s' (%d line(s))",e->path,(int)e->n); }
    free(src);
}
static char *ed_baseline_from_disk(const char *path,size_t *len){
    Editor base; size_t i; char *saved;
    memset(&base,0,sizeof base);
    snprintf(base.path,sizeof base.path,"%s",path);
    ed_load(&base);
    saved=ed_snapshot(base.ln,base.n,len);
    for(i=0;i<base.n;i++) free(base.ln[i].b);
    free(base.ln);
    return saved;
}

static void ed_free(Editor *e){
    size_t i;
    for(i=0;i<e->n;i++) free(e->ln[i].b);
    free(e->ln); free(e->saved); free(e->clip); free(e->pf_res); e->pf_res=NULL; e->pf_cap=0;
    ex_free(e);
    ed_undo_clear(e);
    for(i=0;i<e->nstash;i++){ size_t k; for(k=0;k<e->stash[i].n;k++) free(e->stash[i].ln[k].b); free(e->stash[i].ln); free(e->stash[i].saved); }
    free(e->stash); e->stash=NULL; e->nstash=0; e->scap=0;
}

/* LC syntax colors */
#define EC_NONE 0
#define EC_KEY 1     /* Keywords */
#define EC_STR 2     /* Strings */
#define EC_NUM 3     /* Numbers */
#define EC_COM 4     /* Comments */
#define EC_FN 5      /* Calls */
#define EC_DIR 6     /* Directives */
static char EC_SEQ[7][32]={ "",
    "38;2;197;134;192", "38;2;206;145;120", "38;2;181;206;168",
    "38;2;106;153;85", "38;2;220;220;170", "38;2;86;156;214" };

/* Syntax languages gated by the luc plugin */
enum {
    L_NONE=0, L_LUC, L_C, L_PY, L_JS, L_LUA, L_PS, L_SH, L_JSON, L_MD,
    L_JAVA, L_GO, L_RUST, L_CS, L_KOTLIN, L_SWIFT, L_DART, L_SCALA,
    L_PHP, L_CSS, L_HTML, L_SQL, L_RUBY, L_PERL, L_R, L_YAML, L_TOML,
    L_DOCKER, L_MAKE, L_INI, L_HCL
};
static int ed_lang(const char *path){
    const char *d=strrchr(path,'.');
    const char *base=path, *p;
    char e[16]; size_t k;
    for(p=path;*p;p++) if(*p=='/'||*p=='\\') base=p+1;
    if(!_stricmp(base,"dockerfile")) return L_DOCKER;
    if(!_stricmp(base,"makefile")||!_stricmp(base,"gnumakefile")) return L_MAKE;
    if(!_stricmp(base,"cmakelists.txt")) return L_MAKE;
    if(!_stricmp(base,".gitignore")||!_stricmp(base,".env")) return L_SH;
    if(!d||d==path||!d[1]) return L_NONE;
    d++;
    for(k=0;k<sizeof e-1&&d[k];k++){
        char c=d[k]; e[k]=(c>='A'&&c<='Z')?(char)(c+32):c;
    }
    e[k]=0;
    if(!strcmp(e,"luc")) return L_LUC;
    if(!strcmp(e,"c")||!strcmp(e,"h")||!strcmp(e,"cpp")||!strcmp(e,"hpp")||
       !strcmp(e,"cc")||!strcmp(e,"cxx")) return L_C;
    if(!strcmp(e,"py")) return L_PY;
    if(!strcmp(e,"js")||!strcmp(e,"mjs")||!strcmp(e,"ts")||!strcmp(e,"tsx")||
       !strcmp(e,"jsx")) return L_JS;
    if(!strcmp(e,"lua")) return L_LUA;
    if(!strcmp(e,"ps1")) return L_PS;
    if(!strcmp(e,"sh")||!strcmp(e,"bash")||!strcmp(e,"zsh")) return L_SH;
    if(!strcmp(e,"json")) return L_JSON;
    if(!strcmp(e,"md")||!strcmp(e,"markdown")) return L_MD;
    if(!strcmp(e,"java")) return L_JAVA;
    if(!strcmp(e,"go")) return L_GO;
    if(!strcmp(e,"rs")) return L_RUST;
    if(!strcmp(e,"cs")) return L_CS;
    if(!strcmp(e,"kt")||!strcmp(e,"kts")) return L_KOTLIN;
    if(!strcmp(e,"swift")) return L_SWIFT;
    if(!strcmp(e,"dart")) return L_DART;
    if(!strcmp(e,"scala")||!strcmp(e,"sc")) return L_SCALA;
    if(!strcmp(e,"php")||!strcmp(e,"phtml")) return L_PHP;
    if(!strcmp(e,"css")||!strcmp(e,"scss")||!strcmp(e,"less")) return L_CSS;
    if(!strcmp(e,"html")||!strcmp(e,"htm")||!strcmp(e,"xml")||!strcmp(e,"svg")) return L_HTML;
    if(!strcmp(e,"sql")) return L_SQL;
    if(!strcmp(e,"rb")||!strcmp(e,"rake")||!strcmp(e,"gemspec")) return L_RUBY;
    if(!strcmp(e,"pl")||!strcmp(e,"pm")) return L_PERL;
    if(!strcmp(e,"r")) return L_R;
    if(!strcmp(e,"yaml")||!strcmp(e,"yml")) return L_YAML;
    if(!strcmp(e,"toml")) return L_TOML;
    if(!strcmp(e,"ini")||!strcmp(e,"cfg")||!strcmp(e,"conf")) return L_INI;
    if(!strcmp(e,"tf")||!strcmp(e,"hcl")) return L_HCL;
    return L_NONE;
}
static int ed_is_kw_lang(int lang,const char *s,size_t n){
    static const char *c_kw[]={
        "auto","break","case","char","const","continue","default","do","double",
        "else","enum","extern","float","for","goto","if","int","long","return",
        "short","signed","sizeof","static","struct","switch","typedef","union",
        "unsigned","void","volatile","while","NULL","true","false",NULL};
    static const char *py_kw[]={
        "False","None","True","and","as","assert","break","class","continue",
        "def","elif","else","except","finally","for","from","global","if",
        "import","in","is","lambda","not","or","pass","raise","return","try",
        "while","with","yield","self","print",NULL};
    static const char *js_kw[]={
        "await","break","case","catch","class","const","continue","debugger",
        "default","delete","do","else","export","extends","finally","for",
        "function","if","import","in","instanceof","let","new","of","return",
        "super","switch","this","throw","try","typeof","var","void","while",
        "with","yield","true","false","null","undefined",NULL};
    static const char *lua_kw[]={
        "and","break","do","else","elseif","end","false","for","function","if",
        "in","local","nil","not","or","repeat","return","then","true","until",
        "while","print","require",NULL};
    static const char *ps_kw[]={
        "function","param","return","if","else","elseif","foreach","for",
        "while","do","switch","break","continue","true","false","null","try",
        "catch","throw",NULL};
    static const char *json_kw[]={"true","false","null",NULL};
    static const char *jvm_kw[]={
        "abstract","as","assert","boolean","break","byte","case","catch","char",
        "class","const","continue","def","default","do","double","else","enum",
        "extends","false","final","finally","float","for","fun","if","implements",
        "import","in","instanceof","int","interface","is","long","new","null",
        "object","override","package","private","protected","public","return",
        "short","static","super","switch","synchronized","this","throw","throws",
        "trait","true","try","val","var","void","volatile","when","while",NULL};
    static const char *go_kw[]={
        "break","case","chan","const","continue","default","defer","else","fallthrough",
        "false","for","func","go","goto","if","import","interface","iota","map",
        "package","range","return","select","struct","switch","true","type","var",
        "nil","bool","byte","complex64","complex128","error","float32","float64",
        "int","int8","int16","int32","int64","rune","string","uint","uint8",
        "uint16","uint32","uint64","uintptr",NULL};
    static const char *rust_kw[]={
        "as","async","await","break","const","continue","crate","dyn","else","enum",
        "extern","false","fn","for","if","impl","in","let","loop","match","mod",
        "move","mut","pub","ref","return","self","Self","static","struct","super",
        "trait","true","type","unsafe","use","where","while","i8","i16","i32","i64",
        "i128","isize","u8","u16","u32","u64","u128","usize","f32","f64","bool",
        "char","str",NULL};
    static const char *cs_kw[]={
        "abstract","as","async","await","base","bool","break","byte","case","catch",
        "char","checked","class","const","continue","decimal","default","delegate",
        "do","double","else","enum","event","explicit","extern","false","finally",
        "fixed","float","for","foreach","goto","if","implicit","in","int","interface",
        "internal","is","lock","long","namespace","new","null","object","operator",
        "out","override","params","private","protected","public","readonly","ref",
        "return","sbyte","sealed","short","sizeof","stackalloc","static","string",
        "struct","switch","this","throw","true","try","typeof","uint","ulong",
        "unchecked","unsafe","ushort","using","var","virtual","void","volatile",
        "while",NULL};
    static const char *swift_kw[]={
        "associatedtype","as","async","await","break","case","catch","class","continue",
        "defer","do","else","enum","extension","fallthrough","false","for","func",
        "guard","if","import","in","init","inout","let","nil","operator","private",
        "protocol","public","repeat","return","self","static","struct","subscript",
        "super","switch","throw","throws","true","try","typealias","var","where","while",
        "Int","String","Bool","Double",NULL};
    static const char *dart_kw[]={
        "as","assert","async","await","break","case","catch","class","const","continue",
        "default","deferred","do","dynamic","else","enum","export","extends","false",
        "final","finally","for","Function","get","hide","if","implements","import",
        "in","is","late","library","mixin","new","null","of","on","operator","part",
        "required","rethrow","return","sealed","set","show","static","super","switch",
        "sync","this","throw","true","try","typedef","var","void","when","while","with",
        "yield",NULL};
    static const char *php_kw[]={
        "abstract","and","array","as","break","callable","case","catch","class","clone",
        "const","continue","declare","default","do","echo","else","elseif","empty",
        "endfor","endforeach","endif","endswitch","endwhile","eval","exit","extends",
        "false","final","finally","for","foreach","function","global","if","implements",
        "include","instanceof","interface","isset","list","namespace","new","null","or",
        "print","private","protected","public","require","return","static","switch",
        "throw","trait","true","try","unset","use","var","while","xor","yield",NULL};
    static const char *sql_kw[]={
        "add","all","alter","and","as","asc","begin","between","by","case","check",
        "column","commit","constraint","create","cross","database","delete","desc",
        "distinct","drop","else","end","exists","foreign","from","full","group","having",
        "inner","insert","into","is","join","key","left","like","limit","not","null",
        "offset","on","or","order","outer","primary","references","right","rollback",
        "select","set","table","then","true","false","union","unique","update","using",
        "values","view","when","where",NULL};
    static const char *ruby_kw[]={
        "alias","and","begin","break","case","class","def","defined","do","else","elsif",
        "end","ensure","false","for","if","in","module","next","nil","not","or","redo",
        "rescue","retry","return","self","super","then","true","undef","unless","until",
        "when","while","yield",NULL};
    static const char *perl_kw[]={
        "and","BEGIN","bless","case","continue","do","else","elsif","END","eval","for",
        "foreach","given","if","last","local","my","next","no","not","our","package",
        "redo","require","return","state","sub","unless","until","use","when","while",
        "eq","ne",NULL};
    static const char *r_kw[]={
        "break","else","FALSE","for","function","if","in","Inf","NA","NaN","next",
        "NULL","repeat","return","TRUE","while",NULL};
    static const char *yaml_kw[]={"false","null","true","yes","no","on","off",NULL};
    static const char *html_kw[]={
        "a","article","aside","audio","body","button","canvas","code","div","em","footer",
        "form","h1","h2","h3","head","header","html","iframe","img","input","label","li",
        "link","main","meta","nav","ol","option","p","script","section","select","small",
        "span","src","strong","style","table","td","textarea","th","title","tr","ul",
        "class","for","href","id","lang","name","rel","src","target","type","value",NULL};
    static const char *css_kw[]={
        "absolute","auto","block","bold","border","color","display","fixed","flex","grid",
        "hidden","inherit","inline","important","initial","margin","none","padding","relative",
        "static","transparent","unset","visible",NULL};
    const char **t=NULL;
    int i;
    if(lang==L_C) t=c_kw;
    else if(lang==L_PY) t=py_kw;
    else if(lang==L_JS) t=js_kw;
    else if(lang==L_LUA) t=lua_kw;
    else if(lang==L_PS) t=ps_kw;
    else if(lang==L_JSON) t=json_kw;
    else if(lang==L_JAVA||lang==L_KOTLIN||lang==L_SCALA) t=jvm_kw;
    else if(lang==L_GO) t=go_kw;
    else if(lang==L_RUST) t=rust_kw;
    else if(lang==L_CS) t=cs_kw;
    else if(lang==L_SWIFT) t=swift_kw;
    else if(lang==L_DART) t=dart_kw;
    else if(lang==L_PHP) t=php_kw;
    else if(lang==L_SQL) t=sql_kw;
    else if(lang==L_RUBY) t=ruby_kw;
    else if(lang==L_PERL) t=perl_kw;
    else if(lang==L_R) t=r_kw;
    else if(lang==L_YAML||lang==L_TOML) t=yaml_kw;
    else if(lang==L_HTML) t=html_kw;
    else if(lang==L_CSS) t=css_kw;
    else return 0;
    for(i=0;t[i];i++){ size_t k=0; while(k<n&&t[i][k]){
            unsigned char a=(unsigned char)t[i][k], b=(unsigned char)s[k];
            if(lang==L_SQL){
                if(a>='A'&&a<='Z') a=(unsigned char)(a+32);
                if(b>='A'&&b<='Z') b=(unsigned char)(b+32);
            }
            if(a!=b) break;
            k++;
        }
        if(k==n&&t[i][k]==0) return 1; }
    return 0;
}
/* Leading whitespace only before byte k (for # directives/headers) */
static int ed_is_lead(char *b,size_t k){
    while(k>0){ k--; if(b[k]!=' '&&b[k]!='\t') return 0; }
    return 1;
}
static int ed_is_cstyle_lang(int lang){
    return lang==L_C||lang==L_JS||lang==L_JAVA||lang==L_GO||lang==L_RUST||
        lang==L_CS||lang==L_KOTLIN||lang==L_SWIFT||lang==L_DART||
        lang==L_SCALA||lang==L_PHP||lang==L_CSS||lang==L_SQL||lang==L_HCL;
}
static int ed_is_hash_lang(int lang){
    return lang==L_PY||lang==L_SH||lang==L_PS||lang==L_RUBY||
        lang==L_PHP||lang==L_PERL||lang==L_R||lang==L_YAML||lang==L_TOML||
        lang==L_DOCKER||lang==L_MAKE||lang==L_INI||lang==L_HCL;
}
static int ed_is_dash_comment_lang(int lang){
    return lang==L_LUC||lang==L_LUA||lang==L_SQL;
}
static int ed_is_slash_comment_lang(int lang){
    return ed_is_cstyle_lang(lang)&&lang!=L_CSS;
}
static int ed_has_c_block_comment(int lang){
    return ed_is_cstyle_lang(lang);
}

static int ed_is_kw(const char *s,size_t n){
    static const char *kw[]={
        "and","break","do","else","elseif","end","for","function","command",
        "if","in","not","or","repeat","return","then","until","while",
        "run","make","get","pack","as","create","import",
        "true","false","nil",NULL};
    int i;
    for(i=0;kw[i];i++){ size_t k=0; while(k<n&&kw[i][k]&&kw[i][k]==s[k]) k++;
        if(k==n&&kw[i][k]==0) return 1; }
    return 0;
}

/* Pane + selection backgrounds and bright/dim text (rewritten by :theme) */
static char EC_LINEBG[32]="\x1b[48;2;42;45;46m";   /* Cursor-line bg */
static char EC_CODEBG[32]="\x1b[48;2;30;30;30m";   /* Code pane bg */
static char EC_EXBG[32]="\x1b[48;2;17;17;17m";     /* Explorer pane bg */
static char EC_TOPBG[32]="\x1b[48;2;22;22;22m";    /* Top strip bg */
static char EC_LOGBG[32]="\x1b[48;2;37;37;37m";    /* Log panel bg */
static char EC_SELBG[32]="\x1b[48;2;38;79;120m";   /* Text selection bg */
static char TH_TCUR[32]="\x1b[97m";                /* Bright text (gutter current, title) */
static char TH_TDIM[32]="\x1b[90m";                /* Dim text (gutter, hints) */

typedef struct { const char *name;
    const char *code,*line,*ex,*top,*log,*sel;
    const char *key,*str,*num,*com,*fn,*dir;
    const char *tcur,*tdim; } ThemeDef;
static const ThemeDef THEMES[]={
    {"dark","48;2;30;30;30","48;2;42;45;46","48;2;17;17;17","48;2;22;22;22",
     "48;2;37;37;37","48;2;38;79;120",
     "38;2;197;134;192","38;2;206;145;120","38;2;181;206;168","38;2;106;153;85",
     "38;2;220;220;170","38;2;86;156;214","97","90"},
    {"light","48;2;255;255;255","48;2;240;240;240","48;2;243;243;243","48;2;225;225;226",
     "48;2;248;248;248","48;2;173;214;255",
     "38;2;175;0;192","38;2;163;21;21","38;2;9;134;88","38;2;0;128;0",
     "38;2;121;94;38","38;2;0;0;255","30","38;2;110;110;110"},
    {NULL,NULL,NULL,NULL,NULL,NULL,NULL,NULL,NULL,NULL,NULL,NULL,NULL,NULL,NULL}
};
#define ED_NTHEMES 2
static char ed_theme_cur[16]="dark";
static int ed_theme_apply(const char *name){
    int i;
    for(i=0;i<ED_NTHEMES;i++){
        const ThemeDef *t=&THEMES[i];
        if(strcmp(t->name,name)) continue;
        snprintf(EC_CODEBG,sizeof EC_CODEBG,"\x1b[%sm",t->code);
        snprintf(EC_LINEBG,sizeof EC_LINEBG,"\x1b[%sm",t->line);
        snprintf(EC_EXBG,sizeof EC_EXBG,"\x1b[%sm",t->ex);
        snprintf(EC_TOPBG,sizeof EC_TOPBG,"\x1b[%sm",t->top);
        snprintf(EC_LOGBG,sizeof EC_LOGBG,"\x1b[%sm",t->log);
        snprintf(EC_SELBG,sizeof EC_SELBG,"\x1b[%sm",t->sel);
        snprintf(EC_SEQ[1],sizeof EC_SEQ[1],"%s",t->key);
        snprintf(EC_SEQ[2],sizeof EC_SEQ[2],"%s",t->str);
        snprintf(EC_SEQ[3],sizeof EC_SEQ[3],"%s",t->num);
        snprintf(EC_SEQ[4],sizeof EC_SEQ[4],"%s",t->com);
        snprintf(EC_SEQ[5],sizeof EC_SEQ[5],"%s",t->fn);
        snprintf(EC_SEQ[6],sizeof EC_SEQ[6],"%s",t->dir);
        snprintf(TH_TCUR,sizeof TH_TCUR,"\x1b[%sm",t->tcur);
        snprintf(TH_TDIM,sizeof TH_TDIM,"\x1b[%sm",t->tdim);
        snprintf(ed_theme_cur,sizeof ed_theme_cur,"%s",t->name);
        return 1;
    }
    return 0;
}
static int ed_hl_bg = 0;   /* 0 plain, 1 code bg, 2 line bg */
static int ed_hl_wantsel = 0; /* current byte is inside the text selection */
static int ed_hl_selon = 0;   /* selection bg currently on */
#define ED_HL_FINDMAX 64
static int ed_hl_find_n = 0;          /* find matches on the line being drawn */
static size_t ed_hl_find_s[ED_HL_FINDMAX], ed_hl_find_e[ED_HL_FINDMAX];
static int ed_hl_find_cur = -1;       /* index of the active match, -1 none */
static int ed_hl_find_on = 0;         /* 0 none, 1 match, 2 active match */
static int ed_hl_findbg = 0;          /* active match line uses selection bg */
static size_t ed_hl_k = 0;            /* byte currently being emitted */
static const char EC_FINDALL[32]="\x1b[38;2;235;70;70m";
static const char EC_FINDCUR[32]="\x1b[1;38;2;235;70;70m";
static int ed_hl_find_state(size_t k){
    int i;
    for(i=0;i<ed_hl_find_n;i++)
        if(k>=ed_hl_find_s[i]&&k<ed_hl_find_e[i]) return (i==ed_hl_find_cur)?2:1;
    return 0;
}
static void ed_hl_emit(int code,int *cur){
    int st=ed_hl_find_n?ed_hl_find_state(ed_hl_k):0;
    if(code!=*cur||st!=ed_hl_find_on){
        if(st==2) printf("%s",EC_FINDCUR);
        else if(st==1) printf("%s",EC_FINDALL);
        else if(code) printf("\x1b[%sm",EC_SEQ[code]);
        else printf("\x1b[0m");
        if(ed_hl_findbg) printf(EC_SELBG);
        else if(ed_hl_bg==2) printf(EC_LINEBG);
        else if(ed_hl_bg==1) printf(EC_CODEBG);
        *cur=code; ed_hl_selon=0; ed_hl_find_on=st;
    }
    /* selection sits on top of syntax colors (bg swap keeps fg readable) */
    if(ed_hl_wantsel&&!ed_hl_selon){ printf(EC_SELBG); ed_hl_selon=1; }
    else if(!ed_hl_wantsel&&ed_hl_selon){
        if(ed_hl_findbg) printf(EC_SELBG);
        else if(ed_hl_bg==2) printf(EC_LINEBG);
        else if(ed_hl_bg==1) printf(EC_CODEBG);
        else printf("\x1b[49m");
        ed_hl_selon=0;
    }
}

/* Track block state per line; closer is ]=lvl] */
static void ed_scan_state(char *b,size_t len,int *cm,int *stt,int lang){
    size_t k=0;
    if(*cm==-2||*cm==-3){
        while(k<len){
            if(*cm==-2&&b[k]=='*'&&k+1<len&&b[k+1]=='/'){ k+=2; *cm=-1; break; }
            if(*cm==-3&&b[k]=='-'&&k+2<len&&b[k+1]=='-'&&b[k+2]=='>'){
                k+=3; *cm=-1; break;
            }
            k++;
        }
        if(*cm==-2||*cm==-3||*cm>=0||*stt>=0) return;
    }
    if(*cm>=0||*stt>=0){
        int lvl=(*cm>=0)?*cm:*stt;
        while(k<len){
            if(b[k]==']'){ size_t j=k+1; int e=0;
                while(j<len&&b[j]=='='&&e<lvl){e++;j++;}
                if(e==lvl&&j<len&&b[j]==']'){ k=j+1; *cm=-1; *stt=-1; break; } }
            k++;
        }
        if(*cm>=0||*stt>=0) return;
    }
    { int q=0, lead=1;
      while(k<len){
          unsigned char c=(unsigned char)b[k];
          if(q==1){ if(c=='\\'){k+=2;continue;} if(c=='"') q=0; k++; continue; }
          if(q==2){ if(c=='\\'){k+=2;continue;} if(c=='\'') q=0; k++; continue; }
          if(c=='"'){q=1;k++;continue;}
          if(c=='\''){q=2;k++;continue;}
          if(c!=' '&&c!='\t') lead=0;
          if(ed_is_slash_comment_lang(lang)&&c=='/'&&k+1<len&&b[k+1]=='/') return;
          if(ed_is_hash_lang(lang)&&c=='#') return;
          if(lang==L_INI&&c==';') return;
          if(lang==L_C&&c=='#'&&lead) return;
          if(lang==L_MD&&c=='#'&&lead) return;
          if(ed_has_c_block_comment(lang)&&c=='/'&&k+1<len&&b[k+1]=='*'){ *cm=-2; return; }
          if(lang==L_HTML&&c=='<'&&k+3<len&&b[k+1]=='!'&&b[k+2]=='-'&&b[k+3]=='-'){
              size_t j=k+4;
              while(j+2<len&&!(b[j]=='-'&&b[j+1]=='-'&&b[j+2]=='>')) j++;
              if(j+2<len){ k=j+3; continue; }
              *cm=-3; return;
          }
          if(ed_is_dash_comment_lang(lang)&&c=='-'&&k+1<len&&b[k+1]=='-'){
              if(lang==L_SQL) return;
              size_t j=k+2; int e=0;
              if(j<len&&b[j]=='['){ j++; while(j<len&&b[j]=='='&&e<64){e++;j++;}
                  if(j<len&&b[j]=='['){ *cm=e; return; } }
              return;
          }
          if((lang==L_LUC||lang==L_LUA)&&c=='['){
              size_t j=k+1; int e=0;
              while(j<len&&b[j]=='='&&e<64){e++;j++;}
              if(e>0&&j<len&&b[j]=='['){ *stt=e; return; } }
          k++;
      } }
}

/* Render line; col = true display column for tab stops, w = clipped width. */
static void ed_tab_out(int *w,int *col,int maxw){
    int tw=4-((*col)%4), q;
    for(q=0;q<tw&&*w<maxw;q++){ fputc(' ',stdout); (*w)++; (*col)++; }
}
/* Render to end of line in one color (shared by --, // and # comments) */
static void ed_hl_eol(char *b,size_t len,size_t start,int maxw,size_t *k,int *col,
                       int *w,int *done,int has_sel,size_t sel_s,size_t sel_e,
                       int color,int *cur){
    while(*k<len){
        unsigned char c=(unsigned char)b[*k];
        if(*k>=start&&*w<maxw){
            ed_hl_wantsel=has_sel&&*k>=sel_s&&*k<sel_e;
            ed_hl_k=*k;
            ed_hl_emit(color,cur);
            if(c=='\t') ed_tab_out(w,col,maxw);
            else { fputc(c,stdout); (*w)++; (*col)++; }
        }
        else if(*k>=start){ *done=1; break; }
        else *col+=ed_chw(c,*col);
        (*k)++;
    }
}
static void ed_hl_line(char *b,size_t len,size_t start,int maxw,int lang,int cm,int stt,
                       int has_sel,size_t sel_s,size_t sel_e){
    size_t k=0;
    int w=0, col=0, cur=EC_NONE, done=0;
    int st=0, lvl=0;   /* 0 code, 1 dqs, 2 sqs, 3 block */
    if(cm==-2||cm==-3){ st=3; lvl=cm; }
    else if(cm>0){ st=3; lvl=cm-1; }
    else if(stt>0){ st=4; lvl=stt-1; }
    ed_hl_wantsel=0; ed_hl_selon=0; ed_hl_find_on=0;
    while(k<start){ col+=ed_chw((unsigned char)b[k],col); k++; }
    /* block entry color */
    ed_hl_k=start;
    if(st==3&&start==0) ed_hl_emit(EC_COM,&cur);
    if(st==4&&start==0) ed_hl_emit(EC_STR,&cur);
    while(k<len&&!done){
        ed_hl_wantsel = has_sel && k>=sel_s && k<sel_e;
        ed_hl_k = k;
        unsigned char c=(unsigned char)b[k];
        if(st==3||st==4){
            int want=(st==3)?EC_COM:EC_STR;
            if(lvl==-2){   /* C block: closer is star-slash */
                if(c=='*'&&k+1<len&&b[k+1]=='/'){
                    size_t m;
                    for(m=k;m<k+2;m++){ if(m>=start&&w<maxw){ ed_hl_wantsel=has_sel&&m>=sel_s&&m<sel_e; ed_hl_k=m; ed_hl_emit(want,&cur); fputc(b[m],stdout); w++; col++; } else if(m>=start) done=1; else col++; }
                    k+=2; st=0; continue;
                }
            }
            else if(lvl==-3&&c=='-'&&k+2<len&&b[k+1]=='-'&&b[k+2]=='>'){
                size_t m;
                for(m=k;m<k+3;m++){
                    if(m>=start&&w<maxw){ ed_hl_wantsel=has_sel&&m>=sel_s&&m<sel_e;
                        ed_hl_k=m; ed_hl_emit(want,&cur); fputc(b[m],stdout); w++; col++; }
                    else if(m>=start) done=1;
                    else col++;
                }
                k+=3; st=0; continue;
            }
            else if(c==']'){ size_t j=k+1; int e=0;
                while(j<len&&b[j]=='='&&e<lvl){e++;j++;}
                if(e==lvl&&j<len&&b[j]==']'){
                    size_t m;
                    for(m=k;m<=j;m++){ if(m>=start&&w<maxw){ ed_hl_wantsel=has_sel&&m>=sel_s&&m<sel_e; ed_hl_k=m; ed_hl_emit(want,&cur); fputc(b[m],stdout); w++; col++; } else if(m>=start) done=1; else col++; }
                    k=j+1; st=0; continue;
                } }
            if(k>=start&&w<maxw){
                ed_hl_emit(want,&cur);
                if(c=='\t') ed_tab_out(&w,&col,maxw);
                else { fputc(c,stdout); w++; col++; }
            }
            else if(k>=start) done=1;
            else col+=ed_chw(c,col);
            k++; continue;
        }
        if(st==1||st==2){
            char q=st==1?'"':'\'';
            if(k>=start&&w<maxw){
                ed_hl_emit(EC_STR,&cur);
                if(c=='\t') ed_tab_out(&w,&col,maxw);
                else { fputc(c,stdout); w++; col++; }
            }
            else if(k>=start) done=1;
            else col+=ed_chw(c,col);
            if(c=='\\'&&k+1<len){ k++; c=(unsigned char)b[k];
                if(k>=start&&w<maxw){ ed_hl_wantsel=has_sel&&k>=sel_s&&k<sel_e; ed_hl_k=k; ed_hl_emit(EC_STR,&cur); fputc(b[k],stdout); w++; col++; }
                else if(k>=start) done=1;
                else col++; }
            else if(c==q) st=0;
            k++; continue;
        }
        /* code mode */
        if(ed_is_dash_comment_lang(lang)&&c=='-'&&k+1<len&&b[k+1]=='-'){
            ed_hl_eol(b,len,start,maxw,&k,&col,&w,&done,has_sel,sel_s,sel_e,EC_COM,&cur);
            continue;
        }
        if(ed_is_slash_comment_lang(lang)&&c=='/'&&k+1<len&&b[k+1]=='/'){
            ed_hl_eol(b,len,start,maxw,&k,&col,&w,&done,has_sel,sel_s,sel_e,EC_COM,&cur);
            continue;
        }
        if(ed_is_hash_lang(lang)&&c=='#'){
            ed_hl_eol(b,len,start,maxw,&k,&col,&w,&done,has_sel,sel_s,sel_e,EC_COM,&cur);
            continue;
        }
        if(lang==L_INI&&c==';'){
            ed_hl_eol(b,len,start,maxw,&k,&col,&w,&done,has_sel,sel_s,sel_e,EC_COM,&cur);
            continue;
        }
        if((lang==L_C||lang==L_MD)&&c=='#'&&ed_is_lead(b,k)){
            ed_hl_eol(b,len,start,maxw,&k,&col,&w,&done,has_sel,sel_s,sel_e,EC_DIR,&cur);
            continue;
        }
        if(ed_has_c_block_comment(lang)&&c=='/'&&k+1<len&&b[k+1]=='*'){
            size_t m;
            for(m=k;m<k+2;m++){
                if(m>=start&&w<maxw){ ed_hl_wantsel=has_sel&&m>=sel_s&&m<sel_e; ed_hl_k=m; ed_hl_emit(EC_COM,&cur); fputc(b[m],stdout); w++; col++; }
                else if(m>=start){ done=1; break; }
                else col++;
            }
            k+=2; st=3; lvl=-2; continue;
        }
        if(lang==L_HTML&&c=='<'&&k+3<len&&b[k+1]=='!'&&b[k+2]=='-'&&b[k+3]=='-'){
            size_t m;
            for(m=k;m<k+4;m++){
                if(m>=start&&w<maxw){ ed_hl_wantsel=has_sel&&m>=sel_s&&m<sel_e;
                    ed_hl_k=m; ed_hl_emit(EC_COM,&cur); fputc(b[m],stdout); w++; col++; }
                else if(m>=start){ done=1; break; }
                else col++;
            }
            k+=4; st=3; lvl=-3; continue;
        }
        if((c=='"'||c=='\'')){
            st=(c=='"')?1:2;
            if(k>=start&&w<maxw){ ed_hl_emit(EC_STR,&cur); fputc(c,stdout); w++; col++; }
            else if(k>=start) done=1;
            else col++;
            k++; continue;
        }
        if((lang==L_LUC||lang==L_LUA)&&c=='['){
            size_t j=k+1; int e=0;
            while(j<len&&b[j]=='='&&e<64){e++;j++;}
            if(e>0&&j<len&&b[j]=='['){
                size_t m;
                for(m=k;m<=j;m++){ if(m>=start&&w<maxw){ ed_hl_wantsel=has_sel&&m>=sel_s&&m<sel_e; ed_hl_k=m; ed_hl_emit(EC_STR,&cur); fputc(b[m],stdout); w++; col++; } else if(m>=start) done=1; else col++; }
                k=j+1; st=4; lvl=e; continue;
            }
        }
        if(lang==L_PS&&c=='$'){
            size_t j=k+1;
            while(j<len){ unsigned char d=(unsigned char)b[j];
                if((d>='A'&&d<='Z')||(d>='a'&&d<='z')||(d>='0'&&d<='9')||d=='_'||d>=128) j++; else break; }
            if(j>k+1){
                size_t m;
                for(m=k;m<j;m++){ if(m>=start&&w<maxw){ ed_hl_wantsel=has_sel&&m>=sel_s&&m<sel_e; ed_hl_k=m; ed_hl_emit(EC_FN,&cur); fputc(b[m],stdout); w++; col++; } else if(m>=start) done=1; else col++; }
                if(j>k&&j-1>=start&&w>=maxw) done=1;
                k=j; continue;
            }
        }
        if((c>='A'&&c<='Z')||(c>='a'&&c<='z')||c=='_'||c>=128){
            size_t j=k;
            while(j<len){ unsigned char d=(unsigned char)b[j];
                if((d>='A'&&d<='Z')||(d>='a'&&d<='z')||(d>='0'&&d<='9')||d=='_'||d>=128) j++; else break; }
            { int iskw=(lang==L_LUC)?ed_is_kw((char*)b+k,j-k):ed_is_kw_lang(lang,(char*)b+k,j-k);
              int iscall=0;
              if(!iskw){ size_t m=j; while(m<len&&(b[m]==' '||b[m]=='\t')) m++;
                  if(m<len&&b[m]=='(') iscall=1; }
              { size_t m;
                for(m=k;m<j;m++){ if(m>=start&&w<maxw){ ed_hl_wantsel=has_sel&&m>=sel_s&&m<sel_e; ed_hl_k=m; ed_hl_emit(iskw?EC_KEY:(iscall?EC_FN:EC_NONE),&cur); fputc(b[m],stdout); w++; col++; } else if(m>=start) done=1; else col++; }
                if(j>k&&j-1>=start&&w>=maxw) done=1; }
              k=j; continue; }
        }
        if(c>='0'&&c<='9'&&(k==0||!(((b[k-1]>='A')&&(b[k-1]<='Z'))||((b[k-1]>='a')&&(b[k-1]<='z'))||((b[k-1]>='0')&&(b[k-1]<='9'))||b[k-1]=='_'))){
            size_t j=k;
            if(c=='0'&&j+1<len&&(b[j+1]=='x'||b[j+1]=='X')){ j+=2; while(j<len){ unsigned char d=(unsigned char)b[j];
                if((d>='0'&&d<='9')||(d>='a'&&d<='f')||(d>='A'&&d<='F')) j++; else break; } }
            else { while(j<len&&((b[j]>='0'&&b[j]<='9')||b[j]=='.')) j++; }
            { size_t m;
              for(m=k;m<j;m++){ if(m>=start&&w<maxw){ ed_hl_wantsel=has_sel&&m>=sel_s&&m<sel_e; ed_hl_k=m; ed_hl_emit(EC_NUM,&cur); fputc(b[m],stdout); w++; col++; } else if(m>=start) done=1; else col++; }
              if(j>k&&j-1>=start&&w>=maxw) done=1; }
            k=j; continue;
        }
        /* directives: !word at first token (LUC only) */
        if(c=='!'&&lang==L_LUC){
            size_t m=k; int lead=1, p;
            for(p=(int)k-1;p>=0;p--){ if(b[p]!=' '&&b[p]!='\t'){lead=0;break;} }
            if(lead){ m++; while(m<len){ unsigned char d=(unsigned char)b[m];
                if((d>='A'&&d<='Z')||(d>='a'&&d<='z')||d=='_') m++; else break; }
                if(m>k+1){ size_t q;
                    for(q=k;q<m;q++){ if(q>=start&&w<maxw){ ed_hl_wantsel=has_sel&&q>=sel_s&&q<sel_e; ed_hl_k=q; ed_hl_emit(EC_DIR,&cur); fputc(b[q],stdout); w++; col++; } else if(q>=start) done=1; else col++; }
                    k=m; continue; } }
        }
        /* plain char (tabs expand, controls placeholder) */
        if(k>=start&&w<maxw){
            ed_hl_emit(EC_NONE,&cur);
            if(c=='\t'){ int tw=4-(col%4), q; for(q=0;q<tw&&w<maxw;q++){ fputc(' ',stdout); w++; col++; } }
            else { fputc(ed_chshow(c),stdout); w+=ed_chw(c,col); col+=ed_chw(c,col); }
        } else if(k>=start) done=1;
        else if(c=='\t'){ int tw=4-(col%4); col+=tw; }
        else col+=ed_chw(c,col);
        k++;
    }
    if(cur!=EC_NONE) printf("\x1b[0m");
}

static int ed_ex_color(const char *nm,int isdir){
    if(isdir) return 36;
    { const char *d=strrchr(nm,'.');
      if(!d||d==nm) return 0;
      d++;
      { char e[8]; size_t k;
        for(k=0;k<7&&d[k];k++){ char c=d[k]; e[k]=(c>='A'&&c<='Z')?(char)(c+32):c; }
        e[k]=0;
        if(!strcmp(e,"luc")) return 32;
        if(!strcmp(e,"lua")) return 34;
        if(!strcmp(e,"py")) return 33;
        if(!strcmp(e,"zip")||!strcmp(e,"exe")||!strcmp(e,"dll")||!strcmp(e,"so")) return 31; }
      return 0; }
}

/* 1 when the console font looks like a Nerd Font (auto-picks nerd icons) */
static int ed_console_has_nerd_font(void){
    HANDLE h=GetStdHandle(STD_OUTPUT_HANDLE);
    CONSOLE_FONT_INFOEX fi;
    const wchar_t *p;
    memset(&fi,0,sizeof fi); fi.cbSize=sizeof fi;
    if(!GetCurrentConsoleFontEx(h,FALSE,&fi)) return 0;
    for(p=fi.FaceName;*p;p++){
        if((p[0]=='N'||p[0]=='n')&&(p[1]=='E'||p[1]=='e')&&
           (p[2]=='R'||p[2]=='r')&&(p[3]=='D'||p[3]=='d')) return 1;
    }
    return 0;
}
/* 1 when a font family is registered with Windows */
static int ed_font_installed(const char *name){
    HKEY roots[2]={HKEY_LOCAL_MACHINE,HKEY_CURRENT_USER};
    int r;
    for(r=0;r<2;r++){
        HKEY k;
        DWORD idx=0;
        if(RegOpenKeyExA(roots[r],"SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion\\Fonts",
                         0,KEY_READ,&k)!=ERROR_SUCCESS) continue;
        for(;;){
            char vn[256]; DWORD vnlen=sizeof vn, type=0;
            BYTE data[512]; DWORD dlen=sizeof data;
            LONG rc=RegEnumValueA(k,idx,vn,&vnlen,NULL,&type,data,&dlen);
            if(rc!=ERROR_SUCCESS) break;
            idx++;
            if(type==REG_SZ&&strstr(vn,name)){ RegCloseKey(k); return 1; }
        }
        RegCloseKey(k);
    }
    return 0;
}
/* Point this console window at the bundled Nerd Font when it is installed
   but not active (conhost honors it; Windows Terminal ignores the call) */
static void ed_font_ensure(void){
    HANDLE h=GetStdHandle(STD_OUTPUT_HANDLE);
    CONSOLE_FONT_INFOEX fi;
    static const wchar_t want[]=L"FiraCode Nerd Font Mono";
    size_t k;
    if(ed_console_has_nerd_font()) return;
    if(!ed_font_installed("FiraCode Nerd Font Mono")) return;
    memset(&fi,0,sizeof fi); fi.cbSize=sizeof fi;
    if(!GetCurrentConsoleFontEx(h,FALSE,&fi)) return;
    for(k=0;k<LF_FACESIZE-1&&want[k];k++) fi.FaceName[k]=want[k];
    fi.FaceName[k]=0;
    SetCurrentConsoleFontEx(h,FALSE,&fi);
}

/* Nerd Font devicons (nvim-web-devicons codepoints + colors); 1 cell each */
typedef struct { const char *ext; unsigned cp; unsigned rgb; } DevIcon;
static const DevIcon DEVICONS[] = {
    {"luc",0xE620,0x51A0CF}, {"lua",0xE620,0x51A0CF},
    {"py",0xE606,0xFFBC03}, {"js",0xE60C,0xCBCB41},
    {"cjs",0xE60C,0xCBCB41}, {"mjs",0xE60C,0xF1E05A},
    {"ts",0xE628,0x519ABA}, {"mts",0xE628,0x519ABA},
    {"cts",0xE628,0x519ABA}, {"tsx",0xE7BA,0x1354BF},
    {"jsx",0xE625,0x20C2E3}, {"json",0xE60B,0xCBCB41},
    {"json5",0xE60B,0xCBCB41}, {"jsonc",0xE60B,0xCBCB41},
    {"md",0xE609,0xDDDDDD}, {"markdown",0xE609,0xDDDDDD},
    {"txt",0xF15B,0x89E051}, {"log",0xF15B,0xDDDDDD},
    {"html",0xE736,0xE44D26}, {"htm",0xE60E,0xE34C26},
    {"css",0xE749,0x663399}, {"scss",0xE749,0xF55385},
    {"less",0xE749,0x563D7C}, {"c",0xE61E,0x599EFF},
    {"h",0xE61E,0xA074C4}, {"hh",0xE61E,0xA074C4},
    {"hpp",0xE61E,0xA074C4}, {"cpp",0xE61D,0x519ABA},
    {"cc",0xE61D,0xF34B7D}, {"cxx",0xE61D,0x519ABA},
    {"rs",0xE7A8,0xDEA584}, {"go",0xE627,0x00ADD8},
    {"java",0xE738,0xCC3E44}, {"rb",0xE791,0x701516},
    {"php",0xE608,0xA074C4}, {"ps1",0xF120,0x4273CA},
    {"bat",0xE615,0xC1F12E}, {"sh",0xE795,0x4D5A5E},
    {"bash",0xE795,0x89E051}, {"exe",0xF15B,0x9F0500},
    {"dll",0xF15B,0x4D2C0B}, {"so",0xF15B,0xDCDDD6},
    {"png",0xE60D,0xA074C4}, {"jpg",0xE60D,0xA074C4},
    {"jpeg",0xE60D,0xA074C4}, {"gif",0xE60D,0xA074C4},
    {"bmp",0xE60D,0xA074C4}, {"ico",0xE60D,0xCBCB41},
    {"avif",0xE60D,0xA074C4}, {"webp",0xE60D,0xA074C4},
    {"svg",0xF1C5,0xFFB13B}, {"wav",0xF001,0x00AFFF},
    {"mp3",0xF001,0x00AFFF}, {"ogg",0xF001,0x0075AA},
    {"flac",0xF001,0x0075AA}, {"mp4",0xF008,0xFD971F},
    {"mkv",0xF008,0xFD971F}, {"avi",0xF008,0xFD971F},
    {"mov",0xF008,0xFD971F}, {"webm",0xF008,0xFD971F},
    {"zip",0xF1C6,0xECA517}, {"gz",0xF1C6,0xECA517},
    {"rar",0xF1C6,0xECA517}, {"7z",0xF1C6,0xECA517},
    {"tgz",0xF1C6,0xECA517}, {"pdf",0xF1C1,0xB30B00},
    {"ini",0xE615,0x6D8086}, {"cfg",0xE615,0x6D8086},
    {"conf",0xE615,0x6D8086}, {"toml",0xE6B2,0x9C4221},
    {"yml",0xE8EB,0xD70000}, {"yaml",0xE8EB,0xD70000},
    {"ttf",0xF031,0xECECEC}, {"otf",0xF031,0xECECEC},
    {"woff",0xF031,0xECECEC}, {"woff2",0xF031,0xECECEC},
    {"csv",0xE64A,0x89E051}, {"sql",0xE706,0xDAD8D8},
    {"swift",0xE755,0xE37933}, {"kt",0xE634,0x7F52FF},
    {"vue",0xE6A0,0x8DC149}, {"lock",0xF023,0xBBBBBB},
    {"makefile",0xE779,0x6D8086}, {"license",0xE60A,0xCBCB41},
    {"dockerfile",0xE7B0,0x458EE6},
    {NULL,0,0}
};
static const DevIcon DEVICON_DEF={"",0xF15B,0x6D8086};   /* generic file */
static const DevIcon *ed_dev_find(const char *key){
    int i;
    for(i=0;DEVICONS[i].ext;i++) if(!strcmp(DEVICONS[i].ext,key)) return &DEVICONS[i];
    return NULL;
}
/* Exact filenames first (makefile, dockerfile, license), then extension */
static const DevIcon *ed_devicon(const char *nm){
    char low[64]; size_t k;
    const char *d=strrchr(nm,'.');
    const DevIcon *f;
    for(k=0;k<sizeof low-1&&nm[k];k++){
        char c=nm[k]; low[k]=(c>='A'&&c<='Z')?(char)(c+32):c;
    }
    low[k]=0;
    if(!strcmp(low,"makefile")||!strcmp(low,"gnumakefile")) return ed_dev_find("makefile");
    if(!strcmp(low,"dockerfile")) return ed_dev_find("dockerfile");
    if(!strcmp(low,"license")||!strcmp(low,"licence")) return ed_dev_find("license");
    if(!d||d==nm||!d[1]) return NULL;
    { char e[16]; size_t j;
      d++;
      for(j=0;j<sizeof e-1&&d[j];j++){
          char c=d[j]; e[j]=(c>='A'&&c<='Z')?(char)(c+32):c;
      }
      e[j]=0;
      f=ed_dev_find(e);
      return f; }
}
/* Encode one codepoint as UTF-8; returns byte count */
static int ed_cp_utf8(unsigned cp,char *out){
    if(cp<0x80){ out[0]=(char)cp; return 1; }
    if(cp<0x800){
        out[0]=(char)(0xC0|(cp>>6)); out[1]=(char)(0x80|(cp&0x3F)); return 2;
    }
    if(cp<0x10000){
        out[0]=(char)(0xE0|(cp>>12)); out[1]=(char)(0x80|((cp>>6)&0x3F));
        out[2]=(char)(0x80|(cp&0x3F)); return 3;
    }
    out[0]=(char)(0xF0|(cp>>18)); out[1]=(char)(0x80|((cp>>12)&0x3F));
    out[2]=(char)(0x80|((cp>>6)&0x3F)); out[3]=(char)(0x80|(cp&0x3F)); return 4;
}
/* lcode config: %APPDATA%\LUC\lcode.cfg (falls back to %USERPROFILE%\.lcode.cfg) */
static void ed_cfg_path(char *out,size_t cap){
    char base[1024]; DWORD n;
    out[0]=0;
    n=GetEnvironmentVariableA("APPDATA",base,sizeof base);
    if(n>0&&n<sizeof base){ snprintf(out,cap,"%s\\LUC\\lcode.cfg",base); return; }
    n=GetEnvironmentVariableA("USERPROFILE",base,sizeof base);
    if(n>0&&n<sizeof base) snprintf(out,cap,"%s\\.lcode.cfg",base);
}
static void ed_cfg_split(char *line,char *key,size_t kcap,char *val,size_t vcap){
    char *eq=strchr(line,'=');
    size_t kn=0, vn=0, k=0;
    key[0]=0; val[0]=0;
    if(!eq) return;
    while(eq>line&&(eq[-1]==' '||eq[-1]=='\t')) eq--;
    while(line+k<eq&&kn+1<kcap){ key[kn++]=line[k++]; }
    key[kn]=0;
    eq=strchr(line,'=')+1;
    while(*eq==' '||*eq=='\t') eq++;
    while(*eq&&*eq!='\r'&&*eq!='\n'&&vn+1<vcap) val[vn++]=*eq++;
    val[vn]=0;
    while(vn>0&&(val[vn-1]==' '||val[vn-1]=='\t')) val[--vn]=0;
}
static int ed_cfg_bool(const char *v){
    return !strcmp(v,"1")||!strcmp(v,"true")||!strcmp(v,"on")||
           !strcmp(v,"yes");
}
static void ed_cfg_title(char *dst,size_t cap,const char *val){
    char tmp[192]; size_t n;
    snprintf(tmp,sizeof tmp,"%s",val);
    n=strlen(tmp);
    if(n>=2&&((tmp[0]=='"'&&tmp[n-1]=='"')||(tmp[0]=='\''&&tmp[n-1]=='\''))){
        memmove(tmp,tmp+1,n-2); n-=2; tmp[n]=0;
    }
    while(n>0&&(tmp[n-1]==' '||tmp[n-1]=='\t')) tmp[--n]=0;
    if(n==0) return;
    if(n>=cap) n=cap-1;
    memcpy(dst,tmp,n); dst[n]=0;
}
static void ed_layout_defaults(Editor *e){
    e->show_ex=1;
    e->exw=ED_EXW;
    e->ex_side=0;
    snprintf(e->ex_title,sizeof e->ex_title,"EXPLORER");
    snprintf(e->ex_content,sizeof e->ex_content,"tree");
    e->show_panel=1;
    e->panel_h=ED_PANEL_DEF;
    snprintf(e->panel_title,sizeof e->panel_title,"TERMINAL");
    snprintf(e->panel_content,sizeof e->panel_content,"terminal");
}
enum {
    KB_FIND=0, KB_PFIND, KB_SAVE, KB_UNDO, KB_REDO, KB_COPY, KB_PASTE, KB_CUT,
    KB_RUN, KB_NEXT_ERR, KB_TERM, KB_EXPLORER, KB_FILES, KB_SIDE, KB_ICONS,
    KB_COUNT
};
static const char *ED_BIND_ID[KB_COUNT]={
    "find","pfind","save","undo","redo","copy","paste","cut",
    "run","nexterr","terminal","explorer","files","side","icons"
};
static const char *ED_BIND_LABEL[KB_COUNT]={
    "Find in file","Find in files","Save","Undo","Redo","Copy","Paste","Cut",
    "Run file","Next error","Terminal","Explorer","Focus files","Move side","File icons"
};
static void ed_bind_set(EdBind *b,int ctrl,int alt,int shift,WORD vk){
    b->ctrl=ctrl; b->alt=alt; b->shift=shift; b->vk=vk;
}
static int ed_bind_eq(const EdBind *a,const EdBind *b){
    return a->vk==b->vk&&a->ctrl==b->ctrl&&a->alt==b->alt&&a->shift==b->shift;
}
static void ed_bind_defaults(Editor *e){
    int i;
    for(i=0;i<ED_BINDMAX;i++) ed_bind_set(&e->binds[i],0,0,0,0);
    ed_bind_set(&e->binds[KB_FIND],1,0,0,'F');
    ed_bind_set(&e->binds[KB_PFIND],1,0,1,'F');
    ed_bind_set(&e->binds[KB_SAVE],1,0,0,'S');
    ed_bind_set(&e->binds[KB_UNDO],1,0,0,'Z');
    ed_bind_set(&e->binds[KB_REDO],1,0,0,'Y');
    ed_bind_set(&e->binds[KB_COPY],1,0,0,'C');
    ed_bind_set(&e->binds[KB_PASTE],1,0,0,'V');
    ed_bind_set(&e->binds[KB_CUT],1,0,0,'X');
    ed_bind_set(&e->binds[KB_RUN],0,0,0,VK_F5);
    ed_bind_set(&e->binds[KB_NEXT_ERR],0,0,0,VK_F6);
    ed_bind_set(&e->binds[KB_TERM],1,0,0,VK_OEM_3);
    ed_bind_set(&e->binds[KB_EXPLORER],1,0,0,'B');
    ed_bind_set(&e->binds[KB_FILES],1,0,0,'E');
    ed_bind_set(&e->binds[KB_SIDE],1,0,1,'E');
    ed_bind_set(&e->binds[KB_ICONS],0,0,0,VK_F8);
}
static int ed_bind_index(const char *id){
    int i;
    for(i=0;i<KB_COUNT;i++) if(!strcmp(id,ED_BIND_ID[i])) return i;
    return -1;
}
static void ed_bind_parse(Editor *e,const char *id,const char *val){
    int i=ed_bind_index(id), vk=0, ctrl=0, alt=0, shift=0;
    if(i<0) return;
    if(sscanf(val,"%d|%d|%d|%d",&vk,&ctrl,&alt,&shift)<1) return;
    if(vk<=0||vk>0xFF) return;
    ed_bind_set(&e->binds[i],ctrl?1:0,alt?1:0,shift?1:0,(WORD)vk);
}
static void ed_bind_name(const EdBind *b,char *out,size_t cap){
    char k[24]; size_t n=0;
    if(b->vk==0){ snprintf(out,cap,"unbound"); return; }
    if(b->vk>=VK_F1&&b->vk<=VK_F24) snprintf(k,sizeof k,"F%d",b->vk-VK_F1+1);
    else if((b->vk>='A'&&b->vk<='Z')||(b->vk>='0'&&b->vk<='9')) snprintf(k,sizeof k,"%c",(char)b->vk);
    else if(b->vk==VK_OEM_3) snprintf(k,sizeof k,"`");
    else if(b->vk==VK_OEM_MINUS) snprintf(k,sizeof k,"-");
    else if(b->vk==VK_OEM_PLUS) snprintf(k,sizeof k,"=");
    else if(b->vk==VK_OEM_4) snprintf(k,sizeof k,"[");
    else if(b->vk==VK_OEM_5) snprintf(k,sizeof k,"\\");
    else if(b->vk==VK_OEM_6) snprintf(k,sizeof k,"]");
    else if(b->vk==VK_OEM_1) snprintf(k,sizeof k,";");
    else if(b->vk==VK_OEM_2) snprintf(k,sizeof k,"/");
    else if(b->vk==VK_OEM_7) snprintf(k,sizeof k,"'");
    else if(b->vk==VK_OEM_COMMA) snprintf(k,sizeof k,",");
    else if(b->vk==VK_OEM_PERIOD) snprintf(k,sizeof k,".");
    else if(b->vk==VK_SPACE) snprintf(k,sizeof k,"Space");
    else if(b->vk==VK_TAB) snprintf(k,sizeof k,"Tab");
    else if(b->vk==VK_RETURN) snprintf(k,sizeof k,"Enter");
    else if(b->vk==VK_ESCAPE) snprintf(k,sizeof k,"Esc");
    else if(b->vk==VK_LEFT) snprintf(k,sizeof k,"Left");
    else if(b->vk==VK_RIGHT) snprintf(k,sizeof k,"Right");
    else if(b->vk==VK_UP) snprintf(k,sizeof k,"Up");
    else if(b->vk==VK_DOWN) snprintf(k,sizeof k,"Down");
    else if(b->vk==VK_PRIOR) snprintf(k,sizeof k,"PgUp");
    else if(b->vk==VK_NEXT) snprintf(k,sizeof k,"PgDn");
    else snprintf(k,sizeof k,"0x%02X",b->vk);
    out[0]=0;
    #define ED_BIND_APP(s) do{ const char *_s=(s); while(*_s&&n+1<cap) out[n++]=*_s++; out[n]=0; }while(0)
    if(b->ctrl) ED_BIND_APP("Ctrl+");
    if(b->alt) ED_BIND_APP("Alt+");
    if(b->shift) ED_BIND_APP("Shift+");
    ED_BIND_APP(k);
    #undef ED_BIND_APP
}
static int ed_bind_match(const Editor *e,int act,WORD vk,DWORD ctl){
    const EdBind *b;
    int ctrl, alt, shift;
    if(act<0||act>=KB_COUNT) return 0;
    b=&e->binds[act];
    ctrl=(ctl&(LEFT_CTRL_PRESSED|RIGHT_CTRL_PRESSED))!=0;
    alt=(ctl&(LEFT_ALT_PRESSED|RIGHT_ALT_PRESSED))!=0;
    shift=(ctl&SHIFT_PRESSED)!=0;
    return b->vk==vk&&b->ctrl==ctrl&&b->alt==alt&&b->shift==shift;
}
/* One layout key; shared by plugin.cfg and the user's lcode.cfg */
static void ed_layout_key(Editor *e,const char *key,const char *val){
    if(!strcmp(key,"explorer")||!strcmp(key,"explorer.enabled")){
        e->show_ex=ed_cfg_bool(val);
    } else if(!strcmp(key,"explorer.title")){
        ed_cfg_title(e->ex_title,sizeof e->ex_title,val);
    } else if(!strcmp(key,"explorer.side")){
        if(!strcmp(val,"right")||!strcmp(val,"r")) e->ex_side=1;
        else if(!strcmp(val,"left")||!strcmp(val,"l")) e->ex_side=0;
    } else if(!strcmp(key,"explorer.width")){
        int n=atoi(val);
        if(n<ED_EXW_MIN) n=ED_EXW_MIN;
        if(n>80) n=80;
        e->exw=n;
    } else if(!strcmp(key,"explorer.content")){
        snprintf(e->ex_content,sizeof e->ex_content,"%s",val[0]?val:"tree");
    } else if(!strcmp(key,"terminal")||!strcmp(key,"terminal.enabled")){
        e->show_panel=ed_cfg_bool(val);
    } else if(!strcmp(key,"terminal.title")){
        ed_cfg_title(e->panel_title,sizeof e->panel_title,val);
    } else if(!strcmp(key,"terminal.height")){
        int n=atoi(val);
        if(n<ED_PANEL_MIN) n=ED_PANEL_MIN;
        if(n>ED_PANEL_MAX) n=ED_PANEL_MAX;
        e->panel_h=n;
    } else if(!strcmp(key,"terminal.content")){
        snprintf(e->panel_content,sizeof e->panel_content,"%s",val[0]?val:"terminal");
    }
}
/* Read only the layout keys a plugin.cfg carries (other keys are ignored) */
static void ed_layout_read_file(Editor *e,const char *path){
    FILE *f; char line[256],key[64],val[192];
    f=fopen(path,"r"); if(!f) return;
    while(fgets(line,sizeof line,f)){
        ed_cfg_split(line,key,sizeof key,val,sizeof val);
        if(key[0]) ed_layout_key(e,key,val);
    }
    fclose(f);
}
/* Enabled plugins may define the bar/panel layout (see plugin/luc) */
static void ed_layout_from_plugins(Editor *e){
    int i;
    for(i=0;i<e->nplugs;i++)
        if(e->plugs[i].enabled&&e->plugs[i].cfg[0])
            ed_layout_read_file(e,e->plugs[i].cfg);
}
/* Persist appearance + layout together in the user config */
static void ed_cfg_save(Editor *e){
    char p[1024]; FILE *f; char dir[1024]; size_t n; int i;
    ed_cfg_path(p,sizeof p);
    if(!p[0]) return;
    snprintf(dir,sizeof dir,"%s",p);
    n=strlen(dir);
    while(n>0&&dir[n-1]!='\\'&&dir[n-1]!='/') n--;
    if(n>0){ dir[n-1]=0; CreateDirectoryA(dir,NULL); }
    f=fopen(p,"w");
    if(f){
        fprintf(f,"icons=%s\ntheme=%s\nguides=%d\n",
            e->icons_nerd?"nerd":"none",ed_theme_cur,e->opt_guides?1:0);
        fprintf(f,"autosave=%d\n",e->opt_autosave?1:0);
        fprintf(f,"explorer=%d\nexplorer.title=%s\nexplorer.side=%s\n"
                 "explorer.width=%d\nexplorer.content=%s\n",
            e->show_ex?1:0,e->ex_title,e->ex_side?"right":"left",
            e->exw,e->ex_content);
        fprintf(f,"terminal=%d\nterminal.title=%s\nterminal.height=%d\n"
                 "terminal.content=%s\n",
            e->show_panel?1:0,e->panel_title,e->panel_h,e->panel_content);
        for(i=0;i<KB_COUNT;i++)
            fprintf(f,"key.%s=%d|%d|%d|%d\n",ED_BIND_ID[i],
                (int)e->binds[i].vk,e->binds[i].ctrl,e->binds[i].alt,e->binds[i].shift);
        fclose(f);
    }
}
/* Load user prefs; layout keys win over plugin defaults */
static void ed_cfg_load(Editor *e){
    char p[1024]; FILE *f; char line[256],key[64],val[192];
    ed_cfg_path(p,sizeof p);
    if(!p[0]) return;
    f=fopen(p,"r"); if(!f) return;
    while(fgets(line,sizeof line,f)){
        ed_cfg_split(line,key,sizeof key,val,sizeof val);
        if(!key[0]) continue;
        if(!strcmp(key,"theme")){ if(val[0]) ed_theme_apply(val); }
        else if(!strcmp(key,"icons")) e->icons_nerd=!strcmp(val,"nerd")||ed_cfg_bool(val);
        else if(!strcmp(key,"guides")) e->opt_guides=ed_cfg_bool(val);
        else if(!strcmp(key,"autosave")) e->opt_autosave=ed_cfg_bool(val);
        else if(!strncmp(key,"key.",4)) ed_bind_parse(e,key+4,val);
        else ed_layout_key(e,key,val);
    }
    fclose(f);
}
/* 1 on Windows Terminal (legacy console font APIs don't apply there) */
static int ed_in_wt(void){
    const char *s=getenv("WT_SESSION");
    return s&&*s;
}
/* F8: flip icon mode, persist it, and say where the font goes */
static void ed_icons_toggle(Editor *e){
    e->icons_nerd=!e->icons_nerd;
    ed_cfg_save(e);
    if(e->icons_nerd) snprintf(e->status,sizeof e->status,
        "icons: nerd on (saved) - boxes? set profile font to FiraCode Nerd Font Mono");
    else snprintf(e->status,sizeof e->status,"icons: none (saved)");
}

/* Plugins: built-in luc first, then plugin.cfg folders (exe dir, then cwd) */
static void ed_plug_dir(Editor *e,char *out,size_t cap){
    char exe[1024]; DWORD n;
    out[0]=0;
    n=GetModuleFileNameA(NULL,exe,sizeof exe);
    if(n>0&&n<sizeof exe){
        size_t k=n;
        while(k>0&&exe[k-1]!='\\'&&exe[k-1]!='/') k--;
        if(k>0&&k+8<cap){
            memcpy(out,exe,k); out[k]=0;
            snprintf(out+k,cap-k,"\\plugin");
            if(GetFileAttributesA(out)!=INVALID_FILE_ATTRIBUTES) return;
        }
    }
    snprintf(out,cap,".\\plugin");
    if(GetFileAttributesA(out)==INVALID_FILE_ATTRIBUTES) out[0]=0;
    (void)e;
}
static void ed_plug_cfg_read(const char *path,Plug *p){
    FILE *f; char line[256];
    f=fopen(path,"r"); if(!f) return;
    while(fgets(line,sizeof line,f)){
        char *eq=strchr(line,'=');
        char key[64], val[192];
        size_t kn=0, vn=0, k=0;
        if(!eq) continue;
        while(eq>line&&(eq[-1]==' '||eq[-1]=='\t')) eq--;
        while(line+k<eq&&kn+1<sizeof key){ key[kn++]=line[k++]; }
        key[kn]=0;
        eq=strchr(line,'=')+1;
        while(*eq==' '||*eq=='\t') eq++;
        while(*eq&&*eq!='\r'&&*eq!='\n'&&vn+1<sizeof val) val[vn++]=*eq++;
        val[vn]=0;
        while(vn>0&&(val[vn-1]==' '||val[vn-1]=='\t')) val[--vn]=0;
        if(!strcmp(key,"name")) snprintf(p->name,sizeof p->name,"%s",val);
        else if(!strcmp(key,"author")) snprintf(p->author,sizeof p->author,"%s",val);
        else if(!strcmp(key,"desc")) snprintf(p->desc,sizeof p->desc,"%s",val);
        else if(!strcmp(key,"enabled")) p->enabled=(!strcmp(val,"1")||!strcmp(val,"true")||!strcmp(val,"on"));
        else if(!strcmp(key,"locked")) p->locked=(!strcmp(val,"1")||!strcmp(val,"true")||!strcmp(val,"on"));
        else if(!strncmp(key,"setting.",8)&&p->nset<ED_SETMAX){
            /* setting.<id>=<Label>|<0|1> */
            char *bar=strchr(val,'|');
            size_t idn=strlen(key+8);
            if(bar&&idn>0&&idn<sizeof p->set_id[0]){
                size_t ll=(size_t)(bar-val);
                if(ll>=sizeof p->set_label[0]) ll=sizeof p->set_label[0]-1;
                memcpy(p->set_id[p->nset],key+8,idn+1);
                memcpy(p->set_label[p->nset],val,ll);
                p->set_label[p->nset][ll]=0;
                p->set_val[p->nset]=(bar[1]=='1'||bar[1]=='t'||bar[1]=='T');
                p->nset++;
            }
        }
    }
    fclose(f);
}
static void ed_plug_scan_dir(Editor *e,const char *plugdir){
    char pat[1120]; WIN32_FIND_DATAA fd; HANDLE fh;
    if(!plugdir[0]) return;
    snprintf(pat,sizeof pat,"%s\\*",plugdir);
    fh=FindFirstFileA(pat,&fd);
    if(fh==INVALID_HANDLE_VALUE) return;
    do{
        char sub[1120], cfg[1200];
        size_t j;
        if(!strcmp(fd.cFileName,".")||!strcmp(fd.cFileName,"..")) continue;
        if(!(fd.dwFileAttributes&FILE_ATTRIBUTE_DIRECTORY)) continue;
        snprintf(sub,sizeof sub,"%s\\%s",plugdir,fd.cFileName);
        snprintf(cfg,sizeof cfg,"%s\\plugin.cfg",sub);
        if(GetFileAttributesA(cfg)==INVALID_FILE_ATTRIBUTES) continue;
        /* same name overrides (e.g. plugin/luc tunes the built-in) */
        { Plug tmp; size_t j;
          memset(&tmp,0,sizeof tmp);
          ed_plug_cfg_read(cfg,&tmp);
          if(!tmp.name[0]) snprintf(tmp.name,sizeof tmp.name,"%s",fd.cFileName);
          for(j=0;j<(size_t)e->nplugs;j++) if(!strcmp(e->plugs[j].name,tmp.name)) break;
          if(j>=(size_t)e->nplugs){
              if(e->nplugs>=ED_PLUGMAX) continue;
              j=e->nplugs++;
              e->plugs[j].enabled=1;
          }
          { char keepcfg[1024];
            snprintf(keepcfg,sizeof keepcfg,"%s",cfg);
            e->plugs[j]=tmp;
            snprintf(e->plugs[j].cfg,sizeof e->plugs[j].cfg,"%s",keepcfg); }
        }
    }while(FindNextFileA(fh,&fd));
    FindClose(fh);
}
static void ed_plug_scan(Editor *e){
    char user[1024],root[1024]; const char *app=getenv("APPDATA");
    e->nplugs=0; e->plug_sel=0; e->plug_top=0;
    { Plug *p=&e->plugs[e->nplugs++];
      snprintf(p->name,sizeof p->name,"luc");
      snprintf(p->author,sizeof p->author,"hsusulist");
      snprintf(p->desc,sizeof p->desc,"LUC suggestions, syntax colors, themes and layout");
      p->enabled=1; p->locked=1; p->cfg[0]=0; }
    ed_plug_dir(e,e->plugdir,sizeof e->plugdir);
    user[0]=0;
    if(app&&*app) snprintf(user,sizeof user,"%s\\LUC\\plugin",app);
    if(user[0]) ed_plug_scan_dir(e,user);
    if(e->plugdir[0]&&(!user[0]||_stricmp(e->plugdir,user)))
        ed_plug_scan_dir(e,e->plugdir);
    if(GetCurrentDirectoryA(sizeof root,root)){
        size_t n=strlen(root);
        if(n+8<sizeof root){
            snprintf(root+n,sizeof root-n,"\\plugin");
            if(GetFileAttributesA(root)!=INVALID_FILE_ATTRIBUTES&&
               (!user[0]||_stricmp(root,user))&&
               (!e->plugdir[0]||_stricmp(root,e->plugdir)))
                ed_plug_scan_dir(e,root);
        }
    }
}
static int lc_copy_plugin_tree(const char *src,const char *dst,int depth){
    char pat[1200]; WIN32_FIND_DATAA fd; HANDLE fh;
    if(depth>16||snprintf(pat,sizeof pat,"%s\\*",src)>=(int)sizeof pat) return 0;
    if(!CreateDirectoryA(dst,NULL)&&GetLastError()!=ERROR_ALREADY_EXISTS) return 0;
    fh=FindFirstFileA(pat,&fd);
    if(fh==INVALID_HANDLE_VALUE) return 0;
    do{
        char from[1200],to[1200];
        if(!strcmp(fd.cFileName,".")||!strcmp(fd.cFileName,"..")) continue;
        if(fd.dwFileAttributes&FILE_ATTRIBUTE_REPARSE_POINT) continue;
        if(snprintf(from,sizeof from,"%s\\%s",src,fd.cFileName)>=(int)sizeof from||
           snprintf(to,sizeof to,"%s\\%s",dst,fd.cFileName)>=(int)sizeof to){
            FindClose(fh); return 0;
        }
        if(fd.dwFileAttributes&FILE_ATTRIBUTE_DIRECTORY){
            if(!lc_copy_plugin_tree(from,to,depth+1)){ FindClose(fh); return 0; }
        } else if(!CopyFileA(from,to,FALSE)){ FindClose(fh); return 0; }
    }while(FindNextFileA(fh,&fd));
    FindClose(fh);
    return 1;
}
static int lc_install_plugin_cfg(const char *cfg){
    char full[1200],source[1200],target[1200],root[1200],*slash,*base;
    const char *app=getenv("APPDATA"); DWORD n;
    DWORD attr;
    if(!cfg||!app||!*app){
        fprintf(stderr,"lcode: cannot determine the plugin install folder\n");
        return 0;
    }
    n=GetFullPathNameA(cfg,sizeof full,full,NULL);
    if(!n||n>=sizeof full){ fprintf(stderr,"lcode: invalid plugin config path\n"); return 0; }
    attr=GetFileAttributesA(full);
    if(attr==INVALID_FILE_ATTRIBUTES||(attr&FILE_ATTRIBUTE_DIRECTORY)){
        fprintf(stderr,"lcode: cannot open plugin config '%s'\n",full);
        return 0;
    }
    slash=strrchr(full,'\\');
    if(!slash) slash=strrchr(full,'/');
    if(!slash||_stricmp(slash+1,"plugin.cfg")){
        fprintf(stderr,"lcode: select the plugin's plugin.cfg file\n");
        return 0;
    }
    base=slash;
    while(base>full&&base[-1]!='\\'&&base[-1]!='/') base--;
    if(base==slash){
        fprintf(stderr,"lcode: plugin config must be inside its plugin folder\n");
        return 0;
    }
    snprintf(source,sizeof source,"%.*s",(int)(slash-full),full);
    snprintf(root,sizeof root,"%s\\LUC\\plugin",app);
    if(!luc_make_dirs(root)){
        fprintf(stderr,"lcode: cannot create plugin folder '%s'\n",root);
        return 0;
    }
    if(snprintf(target,sizeof target,"%s\\%.*s",root,(int)(slash-base),base)>=(int)sizeof target){
        fprintf(stderr,"lcode: plugin folder path is too long\n");
        return 0;
    }
    if(!lc_copy_plugin_tree(source,target,0)){
        fprintf(stderr,"lcode: cannot install plugin from '%s'\n",source);
        return 0;
    }
    printf("installed plugin -> %s\n",target);
    return 1;
}
/* The luc plugin gates suggestions + extra syntax colors */
static int ed_plug_luc_on(Editor *e){
    int i;
    for(i=0;i<e->nplugs;i++)
        if(!strcmp(e->plugs[i].name,"luc")) return e->plugs[i].enabled;
    return 1;
}
static void ed_plug_write(Editor *e,int i){
    /* Rewrite cfg but keep unknown lines (incl. setting.* we don't own here) */
    char lines[32][256]; int nl=0, k;
    FILE *f;
    if(i<0||i>=e->nplugs||!e->plugs[i].cfg[0]) return;
    f=fopen(e->plugs[i].cfg,"r");
    if(f){
        while(nl<32&&fgets(lines[nl],sizeof lines[0],f)){
            size_t n=strlen(lines[nl]);
            while(n>0&&(lines[nl][n-1]=='\n'||lines[nl][n-1]=='\r')) lines[nl][--n]=0;
            if(!strncmp(lines[nl],"name=",5)||!strncmp(lines[nl],"author=",7)||
               !strncmp(lines[nl],"desc=",5)||!strncmp(lines[nl],"enabled=",8)||
               !strncmp(lines[nl],"locked=",7)||!strncmp(lines[nl],"setting.",8))
                continue;   /* rewritten below */
            nl++;
        }
        fclose(f);
    }
    f=fopen(e->plugs[i].cfg,"w");
    if(!f) return;
    fprintf(f,"name=%s\nauthor=%s\ndesc=%s\nenabled=%d\nlocked=%d\n",
        e->plugs[i].name,e->plugs[i].author,e->plugs[i].desc,
        e->plugs[i].enabled?1:0,e->plugs[i].locked?1:0);
    for(k=0;k<nl;k++) fprintf(f,"%s\n",lines[k]);
    for(k=0;k<e->plugs[i].nset;k++)
        fprintf(f,"setting.%s=%s|%d\n",e->plugs[i].set_id[k],
            e->plugs[i].set_label[k],e->plugs[i].set_val[k]?1:0);
    fclose(f);
}
/* A plugin setting value by id (dflt when absent) */
static int ed_plug_setting(Editor *e,const char *plug,const char *id,int dflt){
    int i, k;
    for(i=0;i<e->nplugs;i++){
        if(strcmp(e->plugs[i].name,plug)) continue;
        for(k=0;k<e->plugs[i].nset;k++)
            if(!strcmp(e->plugs[i].set_id[k],id)) return e->plugs[i].set_val[k];
        return dflt;
    }
    return dflt;
}

/* Suggestions: LUC keywords + builtins + words from the buffer */
static const char *SUG_KW[]={
    "create","function","command","repeat","if","else","elseif","end","while",
    "do","then","return","break","for","in","import","as","make","get","pack",
    "run","and","or","not","true","false","nil",NULL};
static const char *SUG_BI[]={
    "print","tostring","tonumber","type","len","pairs","ipairs","require",
    "unpack","next","select","error","assert","pcall","xpcall","rawget",
    "rawset","setmetatable","getmetatable","collectgarbage","list","table",
    "string","math","io","os","task","buffer","window","self",NULL};
static int ed_is_ident(unsigned char c){
    return (c>='A'&&c<='Z')||(c>='a'&&c<='z')||(c>='0'&&c<='9')||c=='_';
}
static void ed_sug_add(Editor *e,const char *w,const char *tag){
    int i;
    if(e->sug_n>=6) return;
    for(i=0;i<e->sug_n;i++) if(!strcmp(e->sug_items[i],w)) return;
    snprintf(e->sug_items[e->sug_n],sizeof e->sug_items[0],"%s",w);
    snprintf(e->sug_tag[e->sug_n],sizeof e->sug_tag[0],"%s",tag);
    e->sug_n++;
}
/* Rebuild the list from the identifier before the cursor */
static void ed_sug_update(Editor *e){
    char *b; size_t L, k, s, i, j;
    e->sug_on=0; e->sug_n=0; e->sug_sel=0;
    if(!e->path[0]||e->focus!=0) return;
    if(!ed_plug_luc_on(e)) return;
    if(!ed_plug_setting(e,"luc","suggestions",1)) return;
    ed_clamp(e);
    b=e->ln[e->cy].b; L=e->ln[e->cy].len; k=e->cx; s=k;
    while(s>0&&ed_is_ident((unsigned char)b[s-1])) s--;
    if(k-s<1||k-s>=sizeof e->sug_px) return;
    memcpy(e->sug_px,b+s,k-s); e->sug_px[k-s]=0;
    for(i=0;SUG_KW[i];i++)
        if(!strncmp(SUG_KW[i],e->sug_px,k-s)&&strcmp(SUG_KW[i],e->sug_px))
            ed_sug_add(e,SUG_KW[i],"Keyword");
    for(i=0;SUG_BI[i];i++)
        if(!strncmp(SUG_BI[i],e->sug_px,k-s)&&strcmp(SUG_BI[i],e->sug_px))
            ed_sug_add(e,SUG_BI[i],"Function");
    for(i=0;i<e->n&&e->sug_n<6;i++){
        char *lb=e->ln[i].b; size_t LL=e->ln[i].len;
        for(j=0;j<LL&&e->sug_n<6;){
            size_t t=j;
            if(!ed_is_ident((unsigned char)lb[j])){ j++; continue; }
            while(j<LL&&ed_is_ident((unsigned char)lb[j])) j++;
            if(lb[t]>='0'&&lb[t]<='9') continue;   /* number, not a word */
            if(j-t==(k-s)&&!memcmp(lb+t,e->sug_px,k-s)) continue;
            if(j-t>k-s&&!memcmp(lb+t,e->sug_px,k-s)){
                char w[64]; size_t n=j-t;
                if(n>=sizeof w) continue;
                memcpy(w,lb+t,n); w[n]=0;
                ed_sug_add(e,w,"Text");
            }
        }
    }
    if(e->sug_n) e->sug_on=1;
}
/* Replace the prefix with the chosen word */
static void ed_sug_accept(Editor *e){
    size_t plen;
    EdLine *l;
    if(!e->sug_on||e->sug_sel>=e->sug_n){ e->sug_on=0; return; }
    plen=strlen(e->sug_px);
    if(e->cx<plen){ e->sug_on=0; return; }
    l=&e->ln[e->cy];
    memmove(l->b+e->cx-plen,l->b+e->cx,l->len-e->cx+1);
    l->len-=plen; e->cx-=plen;
    ed_insert_bytes(e,e->sug_items[e->sug_sel],strlen(e->sug_items[e->sug_sel]));
    e->sug_on=0; e->status[0]=0;
}

/* Popup geometry: under the cursor, flipped above when crowded */
static void ed_sug_geom(Editor *e,int W,int H,int *x0,int *y0,int *w,int *h){
    size_t dc=0, col=0, k=0, plen;
    int maxw=0, i, bw, bh;
    char *b;
    ed_clamp(e);
    b=e->ln[e->cy].b;
    while(k<e->cx){ int w0=ed_chw((unsigned char)b[k],(int)col); dc+=(size_t)w0; col+=(size_t)w0; k++; }
    plen=strlen(e->sug_px);
    for(i=0;i<e->sug_n;i++){
        int L=(int)strlen(e->sug_items[i])+6;
        if(L>maxw) maxw=L;
    }
    bw=maxw; if(bw>W-4) bw=W-4; if(bw<34) bw=34;
    bh=e->sug_n; if(bh>H-1) bh=H-1; if(bh<1) bh=1;
    *x0=ed_code_x(e)+(int)e->numw+(int)dc-(int)e->dleft-(int)plen;
    if(*x0<0) *x0=0;
    if(*x0+bw>W) *x0=W-bw;
    *y0=(int)(e->cy-e->top+1)+1;   /* 0-indexed row right below the cursor */
    if(*y0+bh>H-1){ *y0=(int)(e->cy-e->top+1)-bh; if(*y0<1) *y0=1; }
    *w=bw; *h=bh;
}
static void ed_sug_draw(Editor *e,int W,int H){
    int x0,y0,bw,bh,i;
    if(!e->sug_on||!e->sug_n) return;
    ed_sug_geom(e,W,H,&x0,&y0,&bw,&bh);
    for(i=0;i<e->sug_n&&i<bh;i++){
        int yy=y0+i;
        printf("\x1b[%d;%dH",yy+1,x0+1);
        if(i==e->sug_sel) printf("\x1b[48;2;0;120;215m\x1b[97m");
        else printf("\x1b[48;2;28;28;28m\x1b[97m");
        fputc(' ',stdout);
        if(!strcmp(e->sug_tag[i],"Keyword")) printf("\x1b[38;2;197;134;192m");
        else if(!strcmp(e->sug_tag[i],"Function")) printf("\x1b[38;2;220;220;170m");
        else printf("\x1b[38;2;150;150;150m");
        fputs("\xe2\x97\x8f ",stdout);
        if(i==e->sug_sel) printf("\x1b[48;2;0;120;215m\x1b[97m");
        else printf("\x1b[48;2;28;28;28m\x1b[97m");
        { int k2, L=(int)strlen(e->sug_items[i]), maxl=bw-3;
          if(maxl<1) maxl=1;
          for(k2=0;k2<L&&k2<maxl;k2++) fputc(e->sug_items[i][k2],stdout);
          for(;k2<maxl;k2++) fputc(' ',stdout); }
        printf("\x1b[0m");
    }
}

/* Ctrl+F find bar: borderless, suggestions-style */
#define ED_FINDMAX 1024
static int ed_find_ch_eq(unsigned char a,unsigned char b){
    if(a>='A'&&a<='Z') a=(unsigned char)(a+32);
    if(b>='A'&&b<='Z') b=(unsigned char)(b+32);
    return a==b;
}
static int ed_find_at(const Editor *e,const char *b,size_t L,size_t p){
    size_t k;
    if(!e->find_len||p+e->find_len>L) return 0;
    for(k=0;k<e->find_len;k++)
        if(!ed_find_ch_eq((unsigned char)b[p+k],(unsigned char)e->find_buf[k])) return 0;
    return 1;
}
static int ed_find_collect(Editor *e,size_t *ml,size_t *mc,int cap,int *total){
    size_t li; int n=0;
    *total=0;
    if(!e->find_len) return 0;
    for(li=0;li<e->n;li++){
        const char *b=e->ln[li].b; size_t L=e->ln[li].len, p=0;
        while(p+e->find_len<=L){
            if(ed_find_at(e,b,L,p)){
                if(n<cap){ ml[n]=li; mc[n]=p; n++; }
                (*total)++;
                p+=e->find_len;
            } else p++;
        }
    }
    return n;
}
static void ed_find_refresh(Editor *e){
    size_t ml[ED_FINDMAX], mc[ED_FINDMAX]; int total=0,n,i,target=-1;
    e->find_idx=0;
    if(!e->find_on||!e->find_len){ e->find_n=0; return; }
    n=ed_find_collect(e,ml,mc,ED_FINDMAX,&total);
    e->find_n=total;
    if(!n) return;
    for(i=0;i<n;i++)
        if(ml[i]==e->cy&&mc[i]==e->cx){ e->find_idx=i+1; return; }
    /* Keep one match selected while typing so it can be shown in red. */
    for(i=0;i<n;i++)
        if(ml[i]>e->cy||(ml[i]==e->cy&&mc[i]>=e->cx)){ target=i; break; }
    if(target<0) target=0;
    e->cy=ml[target]; e->cx=mc[target];
    e->find_idx=target+1;
    e->focus=0; e->sel_on=0; e->sel_armed=0; e->sug_on=0;
    ed_clamp(e);
}
static void ed_find_jump(Editor *e,int dir){
    size_t ml[ED_FINDMAX], mc[ED_FINDMAX]; int total=0,n,i,target=-1;
    if(!e->find_len) return;
    n=ed_find_collect(e,ml,mc,ED_FINDMAX,&total);
    e->find_n=total;
    if(!n){ e->find_idx=0; return; }
    for(i=0;i<n;i++)
        if(ml[i]==e->cy&&mc[i]==e->cx){ target=i; break; }
    if(target>=0){
        target+=dir;
        if(target<0) target=n-1;
        else if(target>=n) target=0;
    } else if(dir>0){
        for(i=0;i<n;i++)
            if(ml[i]>e->cy||(ml[i]==e->cy&&mc[i]>e->cx)){ target=i; break; }
        if(target<0) target=0;
    } else {
        for(i=n-1;i>=0;i--)
            if(ml[i]<e->cy||(ml[i]==e->cy&&mc[i]<e->cx)){ target=i; break; }
        if(target<0) target=n-1;
    }
    e->cy=ml[target]; e->cx=mc[target];
    e->find_idx=target+1;
    e->focus=0; e->sel_on=0; e->sel_armed=0; e->sug_on=0;
    ed_clamp(e);
}
static void ed_find_open(Editor *e){
    e->find_on=1; e->find_bar=1; e->focus=0; e->sug_on=0; e->menu=0;
    e->find_cur=e->find_len;
    e->find_idx=0; e->find_n=0;
    ed_find_refresh(e);
}
static void ed_find_close(Editor *e){
    e->find_on=0; e->find_bar=0; e->find_cx=0; e->find_cy=0;
}
/* :find <text> - highlight every match in the open file (red) */
static void ed_find_all(Editor *e,const char *q){
    size_t ml[ED_FINDMAX], mc[ED_FINDMAX];
    size_t n=strlen(q); int total=0;
    if(n>=sizeof e->find_buf) n=sizeof e->find_buf-1;
    memcpy(e->find_buf,q,n); e->find_buf[n]=0;
    e->find_len=n; e->find_cur=n;
    e->find_on=1; e->find_bar=0;
    e->find_idx=0; e->find_n=0;
    e->focus=0; e->sug_on=0; e->menu=0;
    if(!n) return;
    ed_find_collect(e,ml,mc,ED_FINDMAX,&total);
    e->find_n=total;
}
/* Prepare highlight ranges for one visible line. */
static void ed_find_hl_begin(Editor *e,size_t li){
    size_t p=0; int n=0,i;
    ed_hl_find_n=0; ed_hl_find_cur=-1; ed_hl_find_on=0; ed_hl_findbg=0;
    if(!e->find_on||!e->find_len||li>=e->n) return;
    { const char *b=e->ln[li].b; size_t L=e->ln[li].len;
      while(p+e->find_len<=L){
          if(ed_find_at(e,b,L,p)){
              if(n<ED_HL_FINDMAX){ ed_hl_find_s[n]=p; ed_hl_find_e[n]=p+e->find_len; n++; }
              p+=e->find_len;
          } else p++;
      } }
    ed_hl_find_n=n;
    if(e->find_idx>0&&e->cy==li)
        for(i=0;i<n;i++)
            if(e->cx==ed_hl_find_s[i]){ ed_hl_find_cur=i; break; }
    ed_hl_findbg=(ed_hl_find_cur>=0);
}
static int ed_find_key(Editor *e,WORD vk,WCHAR ch,DWORD ctl){
    int ctrl=(ctl&(LEFT_CTRL_PRESSED|RIGHT_CTRL_PRESSED))!=0;
    int alt=(ctl&(LEFT_ALT_PRESSED|RIGHT_ALT_PRESSED))!=0;
    int shift=(ctl&SHIFT_PRESSED)!=0;
    if(ed_bind_match(e,KB_FIND,vk,ctl)){ ed_find_close(e); return 0; }
    if(ch==27||(ch==0&&vk==VK_ESCAPE)){ ed_find_close(e); return 0; }
    if(ch==13||(ch==0&&vk==VK_RETURN)){ ed_find_jump(e,shift?-1:1); return 0; }
    if(ch==0){
        if(vk==VK_F3){ ed_find_jump(e,shift?-1:1); return 0; }
        if(vk==VK_UP){ ed_find_jump(e,-1); return 0; }
        if(vk==VK_DOWN){ ed_find_jump(e,1); return 0; }
        if(vk==VK_LEFT&&e->find_cur>0){
            do e->find_cur--; while(e->find_cur>0&&ed_is_cont((unsigned char)e->find_buf[e->find_cur]));
            return 0;
        }
        if(vk==VK_RIGHT&&e->find_cur<e->find_len){
            do e->find_cur++; while(e->find_cur<e->find_len&&ed_is_cont((unsigned char)e->find_buf[e->find_cur]));
            return 0;
        }
        if(vk==VK_HOME){ e->find_cur=0; return 0; }
        if(vk==VK_END){ e->find_cur=e->find_len; return 0; }
        if(vk==VK_BACK){
            if(e->find_cur>0){
                size_t a=e->find_cur-1;
                while(a>0&&ed_is_cont((unsigned char)e->find_buf[a])) a--;
                memmove(e->find_buf+a,e->find_buf+e->find_cur,e->find_len-e->find_cur+1);
                e->find_len-=e->find_cur-a; e->find_cur=a;
                ed_find_refresh(e);
            }
            return 0;
        }
        if(vk==VK_DELETE&&e->find_cur<e->find_len){
            size_t b=e->find_cur+1;
            while(b<e->find_len&&ed_is_cont((unsigned char)e->find_buf[b])) b++;
            memmove(e->find_buf+e->find_cur,e->find_buf+b,e->find_len-b+1);
            e->find_len-=b-e->find_cur;
            ed_find_refresh(e);
        }
        return 0;
    }
    if(ch==8){
        if(e->find_cur>0){
            size_t a=e->find_cur-1;
            while(a>0&&ed_is_cont((unsigned char)e->find_buf[a])) a--;
            memmove(e->find_buf+a,e->find_buf+e->find_cur,e->find_len-e->find_cur+1);
            e->find_len-=e->find_cur-a; e->find_cur=a;
            ed_find_refresh(e);
        }
        return 0;
    }
    if(ch>=32&&!ctrl&&!alt){
        char tmp[4]; int m=ed_cp_utf8((unsigned)ch,tmp);
        if(m>0&&e->find_len+(size_t)m<sizeof e->find_buf){
            memmove(e->find_buf+e->find_cur+m,e->find_buf+e->find_cur,e->find_len-e->find_cur+1);
            memcpy(e->find_buf+e->find_cur,tmp,(size_t)m);
            e->find_cur+=(size_t)m; e->find_len+=(size_t)m;
            ed_find_refresh(e);
        }
    }
    return 0;
}
static void ed_find_geom(Editor *e,int W,int H,int *x0,int *y0,int *w,int *h){
    int cx=ed_code_x(e), sx=ed_scroll_x(e,W), bw=36, avail=sx-cx;
    if(avail<12) avail=12;
    if(bw>avail-1) bw=avail-1;
    if(bw>W-4) bw=W-4;
    if(bw<24) bw=24;
    if(bw>avail-1) bw=avail-1;
    if(bw<8) bw=8;
    *x0=sx-bw;
    if(*x0<cx) *x0=cx;
    if(*x0+bw>W-1) *x0=W-1-bw;
    if(*x0<0) *x0=0;
    *y0=1;
    if(*y0+2>H-1) *y0=H-3;
    if(*y0<1) *y0=1;
    *w=bw; *h=2;
}
static void ed_find_draw(Editor *e,int W,int H){
    int x0,y0,bw,bh,prefix=6,qw,caretw=0,startw=0,drawn=0,col=0,i;
    size_t k,first=0;
    char cnt[48]; const char *hint="Enter next  Esc";
    if(!e->find_bar) return;
    ed_find_geom(e,W,H,&x0,&y0,&bw,&bh);
    qw=bw-prefix;
    if(qw<1){ prefix=1; qw=bw-prefix; }
    for(k=0;k<e->find_cur;k++) caretw+=ed_chw((unsigned char)e->find_buf[k],caretw);
    if(caretw>=qw) startw=caretw-qw+1;
    while(first<e->find_len){
        int w0=ed_chw((unsigned char)e->find_buf[first],col);
        if(col>=startw) break;
        if(col+w0>startw){ startw=col; break; }
        col+=w0; first++;
    }
    printf("\x1b[%d;%dH\x1b[48;2;28;28;28m",y0+1,x0+1);
    printf("\x1b[90m find \x1b[97m");
    for(k=first;k<e->find_len;k++){
        int w0=ed_chw((unsigned char)e->find_buf[k],col);
        if(drawn+w0>qw) break;
        fputc(e->find_buf[k],stdout);
        drawn+=w0; col+=w0;
    }
    for(;drawn<qw;drawn++) fputc(' ',stdout);
    e->find_cx=x0+1+prefix+(caretw-startw);
    e->find_cy=y0+1;
    if(e->find_n>0&&e->find_idx>0)
        snprintf(cnt,sizeof cnt," %d/%d found",e->find_idx,e->find_n);
    else snprintf(cnt,sizeof cnt," %d found",e->find_n);
    printf("\x1b[%d;%dH\x1b[48;2;22;22;22m\x1b[90m",y0+2,x0+1);
    fputs(cnt,stdout);
    { int lw=(int)strlen(cnt), hw=(int)strlen(hint), pad=bw-lw-hw;
      if(pad>=2){ for(i=0;i<pad;i++) fputc(' ',stdout); printf("\x1b[90m%s",hint); }
      else for(i=lw;i<bw;i++) fputc(' ',stdout); }
    printf("\x1b[0m");
}

static int ed_pfind_cmp(const void *a,const void *b){
    const EdPFind *x=(const EdPFind*)a, *y=(const EdPFind*)b;
    int c=_stricmp(x->file,y->file);
    if(c) return c;
    return x->line-y->line;
}
static int ed_pfind_match(const char *b,size_t L,const char *q,size_t qn,size_t *at){
    size_t p,k;
    if(!qn||qn>L) return 0;
    for(p=0;p+qn<=L;p++){
        for(k=0;k<qn;k++) if(!ed_find_ch_eq((unsigned char)b[p+k],(unsigned char)q[k])) break;
        if(k==qn){ if(at) *at=p; return 1; }
    }
    return 0;
}
static void ed_pfind_snippet(char *dst,size_t cap,const char *s,size_t len,size_t mpos){
    size_t a=0,b=len,start,end,o=0,k;
    while(a<b&&(s[a]==' '||s[a]=='\t'||s[a]=='\r')) a++;
    while(b>a&&(s[b-1]==' '||s[b-1]=='\t'||s[b-1]=='\r')) b--;
    start=a;
    if(mpos>start+90) start=mpos-50;
    end=b;
    if(end-start>150) end=start+150;
    if(start>a&&o+3<cap){ memcpy(dst+o,"...",3); o+=3; }
    for(k=start;k<end&&o+1<cap;k++){
        unsigned char c=(unsigned char)s[k];
        if(c=='\t'){
            if(o+4<cap){ memcpy(dst+o,"    ",4); o+=4; }
        } else if(c<32){
            dst[o++]=' ';
        } else dst[o++]=(char)c;
    }
    if(end<b&&o+3<cap){ memcpy(dst+o,"...",3); o+=3; }
    dst[o]=0;
}
static EdPFind *ed_pfind_slot(Editor *e){
    if(e->pf_n>=ED_PFIND_MAX){ e->pf_trunc=1; return NULL; }
    if(e->pf_n>=e->pf_cap){
        int cap=e->pf_cap?e->pf_cap*2:128;
        if(cap>ED_PFIND_MAX) cap=ED_PFIND_MAX;
        e->pf_res=(EdPFind*)lrealloc(e->pf_res,sizeof(EdPFind)*(size_t)cap);
        e->pf_cap=cap;
    }
    return &e->pf_res[e->pf_n];
}
static void ed_pfind_scan_file(Editor *e,const char *full,const char *rel){
    FILE *f; long sz; char *buf; size_t got=0,i,start=0,probe;
    int line=1;
    e->pf_scanned++;
    if(e->pf_n>=ED_PFIND_MAX){ e->pf_trunc=1; return; }
    f=fopen(full,"rb");
    if(!f) return;
    fseek(f,0,SEEK_END); sz=ftell(f); fseek(f,0,SEEK_SET);
    if(sz<=0||sz>2*1024*1024){ fclose(f); return; }
    buf=(char*)lmalloc((size_t)sz+1);
    got=fread(buf,1,(size_t)sz,f);
    fclose(f);
    buf[got]=0;
    probe=got<8192?got:8192;
    for(i=0;i<probe;i++) if(!buf[i]){ free(buf); return; }
    for(i=0;i<=got;i++){
        if(i==got||buf[i]=='\n'){
            size_t L=i-start,at=0;
            if(L>0&&buf[start+L-1]=='\r') L--;
            if(ed_pfind_match(buf+start,L,e->pf_buf,e->pf_len,&at)){
                EdPFind *r=ed_pfind_slot(e);
                e->pf_total++;
                if(r){
                    snprintf(r->file,sizeof r->file,"%s",rel);
                    r->line=line;
                    ed_pfind_snippet(r->text,sizeof r->text,buf+start,L,at);
                    e->pf_n++;
                } else { e->pf_trunc=1; free(buf); return; }
            }
            start=i+1; line++;
        }
    }
    free(buf);
}
static int ed_pfind_skip_dir(const char *name){
    return !_stricmp(name,".git")||!_stricmp(name,"node_modules")||
           !_stricmp(name,"__pycache__")||!_stricmp(name,".venv")||
           !_stricmp(name,"venv")||!_stricmp(name,".vs")||
           !_stricmp(name,"$recycle.bin");
}
static void ed_pfind_scan_dir(Editor *e,const char *dir,const char *rel,int depth){
    char pat[1300],child[1300],childrel[512];
    WIN32_FIND_DATAA fd;
    HANDLE h;
    if(depth>24||e->pf_n>=ED_PFIND_MAX){ if(e->pf_n>=ED_PFIND_MAX) e->pf_trunc=1; return; }
    if(strlen(dir)+3>=sizeof pat) return;
    snprintf(pat,sizeof pat,"%s\\*",dir);
    h=FindFirstFileA(pat,&fd);
    if(h==INVALID_HANDLE_VALUE) return;
    do{
        DWORD attr=fd.dwFileAttributes;
        if(!strcmp(fd.cFileName,".")||!strcmp(fd.cFileName,"..")) continue;
        if(attr&FILE_ATTRIBUTE_REPARSE_POINT) continue;
        if(rel[0]) snprintf(childrel,sizeof childrel,"%s\\%s",rel,fd.cFileName);
        else snprintf(childrel,sizeof childrel,"%s",fd.cFileName);
        if(strlen(dir)+strlen(fd.cFileName)+2>=sizeof child) continue;
        snprintf(child,sizeof child,"%s\\%s",dir,fd.cFileName);
        if(attr&FILE_ATTRIBUTE_DIRECTORY){
            if(ed_pfind_skip_dir(fd.cFileName)) continue;
            ed_pfind_scan_dir(e,child,childrel,depth+1);
        } else {
            ULONGLONG size=((ULONGLONG)fd.nFileSizeHigh<<32)|fd.nFileSizeLow;
            if(size>2*1024*1024) continue;
            ed_pfind_scan_file(e,child,childrel);
        }
    }while(FindNextFileA(h,&fd));
    FindClose(h);
}
static void ed_pfind_refresh(Editor *e){
    const char *root;
    e->pf_n=0; e->pf_total=0; e->pf_trunc=0; e->pf_scanned=0;
    e->pf_sel=0; e->pf_top=0;
    if(!e->pf_len) return;
    root=e->exdir[0]?e->exdir:".";
    ed_pfind_scan_dir(e,root,"",0);
    if(e->pf_n>1) qsort(e->pf_res,(size_t)e->pf_n,sizeof(EdPFind),ed_pfind_cmp);
    if(e->pf_n>0) e->pf_trunc=e->pf_trunc||(e->pf_total>e->pf_n);
}
static void ed_pfind_open(Editor *e){
    e->pf_on=1; e->pf_len=0; e->pf_cur=0; e->pf_buf[0]=0;
    e->pf_n=0; e->pf_total=0; e->pf_trunc=0; e->pf_sel=0; e->pf_top=0; e->pf_scanned=0;
    e->find_on=0; e->find_bar=0; e->sug_on=0; e->menu=0; e->focus=0;
    e->status[0]=0;
}
static void ed_pfind_close(Editor *e){
    e->pf_on=0; e->focus=0;
}
static void ed_pfind_place(Editor *e){
    size_t at;
    e->cx=0;
    if(!e->pf_len||e->cy>=e->n) return;
    if(ed_pfind_match(e->ln[e->cy].b,e->ln[e->cy].len,e->pf_buf,e->pf_len,&at)) e->cx=at;
}
static void ed_pfind_accept(Editor *e){
    EdPFind *r; char full[1300]; int rows;
    if(!e->pf_n||e->pf_sel<0||e->pf_sel>=e->pf_n) return;
    r=&e->pf_res[e->pf_sel];
    snprintf(full,sizeof full,"%s\\%s",e->exdir[0]?e->exdir:".",r->file);
    ed_open_path(e,full);
    e->cy=r->line>0?(size_t)(r->line-1):0;
    ed_clamp(e);
    ed_pfind_place(e);
    e->sel_on=0; e->sel_armed=0; e->sug_on=0; e->find_on=0; e->find_bar=0; e->focus=0;
    rows=e->vh>5?e->vh-4:1;
    if(e->cy>(size_t)(rows/2)) e->top=e->cy-(size_t)(rows/2); else e->top=0;
    e->pf_on=0;
    snprintf(e->status,sizeof e->status,"opened %s:%d",r->file,r->line);
}
static int ed_pfind_rows(Editor *e){
    int rows=e->vh>16?e->vh-16:(e->vh>6?e->vh-6:1);
    if(rows<1) rows=1;
    return rows;
}
static void ed_pfind_scroll(Editor *e){
    int rows=ed_pfind_rows(e);
    if(e->pf_sel<e->pf_top) e->pf_top=e->pf_sel;
    if(e->pf_sel>=e->pf_top+rows) e->pf_top=e->pf_sel-rows+1;
    if(e->pf_top<0) e->pf_top=0;
}
static int ed_pfind_key(Editor *e,WORD vk,WCHAR ch,DWORD ctl){
    int ctrl=(ctl&(LEFT_CTRL_PRESSED|RIGHT_CTRL_PRESSED))!=0;
    int alt=(ctl&(LEFT_ALT_PRESSED|RIGHT_ALT_PRESSED))!=0;
    int shift=(ctl&SHIFT_PRESSED)!=0;
    int rows=ed_pfind_rows(e);
    if(ed_bind_match(e,KB_PFIND,vk,ctl)){ ed_pfind_close(e); return 0; }
    if(ch==27||(ch==0&&vk==VK_ESCAPE)){ ed_pfind_close(e); return 0; }
    if(ch==13||(ch==0&&vk==VK_RETURN)){ ed_pfind_accept(e); return 0; }
    if(ch==0){
        if(vk==VK_UP&&e->pf_sel>0) e->pf_sel--;
        else if(vk==VK_DOWN&&e->pf_sel+1<e->pf_n) e->pf_sel++;
        else if(vk==VK_PRIOR) e->pf_sel=e->pf_sel-rows>0?e->pf_sel-rows:0;
        else if(vk==VK_NEXT) e->pf_sel=e->pf_sel+rows<e->pf_n?e->pf_sel+rows:(e->pf_n>0?e->pf_n-1:0);
        else if(vk==VK_LEFT){
            if(e->pf_cur>0){ e->pf_cur--; while(e->pf_cur>0&&ed_is_cont((unsigned char)e->pf_buf[e->pf_cur])) e->pf_cur--; }
        } else if(vk==VK_RIGHT){
            if(e->pf_cur<e->pf_len){ e->pf_cur++; while(e->pf_cur<e->pf_len&&ed_is_cont((unsigned char)e->pf_buf[e->pf_cur])) e->pf_cur++; }
        } else if(vk==VK_HOME) e->pf_cur=0;
        else if(vk==VK_END) e->pf_cur=e->pf_len;
        else if(vk==VK_DELETE&&e->pf_cur<e->pf_len){
            size_t a=e->pf_cur,b=a+1;
            while(b<e->pf_len&&ed_is_cont((unsigned char)e->pf_buf[b])) b++;
            memmove(e->pf_buf+a,e->pf_buf+b,e->pf_len-b+1);
            e->pf_len-=b-a;
            ed_pfind_refresh(e);
        }
        ed_pfind_scroll(e);
        return 0;
    }
    if(ch==8){
        if(e->pf_cur>0){
            size_t a=e->pf_cur-1;
            while(a>0&&ed_is_cont((unsigned char)e->pf_buf[a])) a--;
            memmove(e->pf_buf+a,e->pf_buf+e->pf_cur,e->pf_len-e->pf_cur+1);
            e->pf_len-=e->pf_cur-a; e->pf_cur=a;
            ed_pfind_refresh(e);
        }
        return 0;
    }
    if(ch>=32&&!ctrl&&!alt){
        char tmp[4]; int m=ed_cp_utf8((unsigned)ch,tmp);
        if(m>0&&e->pf_len+(size_t)m<sizeof e->pf_buf){
            memmove(e->pf_buf+e->pf_cur+(size_t)m,e->pf_buf+e->pf_cur,e->pf_len-e->pf_cur+1);
            memcpy(e->pf_buf+e->pf_cur,tmp,(size_t)m);
            e->pf_cur+=(size_t)m; e->pf_len+=(size_t)m;
            ed_pfind_refresh(e);
        }
    }
    return 0;
}
static int ed_pfind_put(const char *s,int maxw){
    int w=0;
    while(*s&&w<maxw){
        int cw=ed_chw((unsigned char)*s,w);
        if(cw<=0) cw=1;
        if(w+cw>maxw) break;
        fputc(*s++,stdout); w+=cw;
        while(*s&&ed_is_cont((unsigned char)*s)) fputc(*s++,stdout);
    }
    return w;
}
/* print text, the query match inside it in red */
static int ed_pfind_put_hl(const char *s,const char *q,int maxw){
    size_t ql=q?strlen(q):0,i,hl=0;
    int w=0, matched=0;
    if(ql){
        for(i=0;s[i];i++){
            size_t k; int ok=1;
            for(k=0;k<ql;k++)
                if(!s[i+k]||!ed_find_ch_eq((unsigned char)s[i+k],(unsigned char)q[k])){ ok=0; break; }
            if(ok){ hl=i; matched=1; break; }
        }
    }
    for(i=0;s[i];i++){
        int cw=ed_chw((unsigned char)s[i],w);
        if(cw<=0) cw=1;
        if(w+cw>maxw) break;
        if(ql&&matched&&i>=hl&&i<hl+ql) printf("%s",EC_FINDALL);
        fputc(s[i],stdout); w+=cw;
        if(ql&&matched&&i>=hl&&i<hl+ql) printf("\x1b[97m");
    }
    return w;
}
static void ed_pfind_geom(Editor *e,int W,int H,int *x0,int *y0,int *w,int *h){
    int bw,bh,rows;
    bw=W-10; if(bw<36) bw=36; if(bw>W-2) bw=W-2;
    bh=H-10; if(bh<12) bh=12; if(bh>H) bh=H;
    rows=bh-6; if(rows<3) rows=3; if(rows>H-6) rows=H-6; if(rows<1) rows=1;
    bh=rows+6; if(bh>H){ bh=H; rows=bh-6; if(rows<1) rows=1; }
    *x0=(W-bw)/2; if(*x0<5) *x0=5;
    *y0=(H-bh)/2; if(*y0<5) *y0=5;
    *w=bw; *h=bh;
}
static void ed_pfind_draw(Editor *e,int W,int H){
    int x0,y0,bw,bh,i,k,rows,r;
    size_t f;
    char cnt[80];
    int tw,lw=6,fw,inner;
    int qx,barrow,qwidth,statusw;
    size_t first=0;
    int caretw=0,drawn=0,startw=0;
    if(!e->pf_on) return;
    ed_pfind_geom(e,W,H,&x0,&y0,&bw,&bh);
    rows=bh-6; if(rows<1) rows=1;
    inner=bw-2;
    lw=6;
    fw=24;
    tw=inner-fw-12; if(tw<10){ tw=10; fw=inner-tw-12; if(fw<8) fw=8; }
    if(fw>inner-20) fw=inner-20; if(fw<8) fw=8;
    if(e->pf_sel<e->pf_top) e->pf_top=e->pf_sel;
    if(e->pf_sel>=e->pf_top+rows) e->pf_top=e->pf_sel-rows+1;
    if(e->pf_top<0) e->pf_top=0;
    if(e->pf_len)
        snprintf(cnt,sizeof cnt,"%d%s matches / %d files",e->pf_total,e->pf_trunc?"+":"",e->pf_scanned);
    else snprintf(cnt,sizeof cnt,"type to search");
    /* top border */
    printf("\x1b[%d;%dH%s\xe2\x94\x8c",y0+1,x0+1,TH_TDIM);
    for(k=0;k<inner;k++) fputs("\xe2\x94\x80",stdout);
    printf("\xe2\x94\x90\x1b[0m");
    /* title */
    printf("\x1b[%d;%dH%s\xe2\x94\x82",y0+2,x0+1,TH_TDIM);
    printf("\x1b[48;2;45;45;45m\x1b[97m");
    { const char *title=" PROJECT SEARCH ";
      const char *hint="Up/Down select  Enter open  Esc close";
      int tl=(int)strlen(title),hl=(int)strlen(hint),hintstart=inner-hl;
      ed_pfind_put(title,inner);
      for(i=tl;i<inner;i++)
          fputc(inner>tl+hl+1&&i>=hintstart?hint[i-hintstart]:' ',stdout);
    }
    printf("\x1b[0m%s\xe2\x94\x82\x1b[0m",TH_TDIM);
    /* column header */
    printf("\x1b[%d;%dH%s\xe2\x94\x82",y0+3,x0+1,TH_TDIM);
    printf("\x1b[48;2;30;30;30m\x1b[90m");
    printf(" TEXT");
    for(i=5;i<tw;i++) fputc(' ',stdout);
    printf(" \xe2\x94\x82 ");
    printf("LINE");
    for(i=4;i<lw;i++) fputc(' ',stdout);
    printf(" \xe2\x94\x82 ");
    printf("FILE");
    for(i=4;i<fw;i++) fputc(' ',stdout);
    printf("\x1b[0m");
    printf("\x1b[%d;%dH%s\xe2\x94\x82\x1b[0m",y0+3,x0+bw,TH_TDIM);
    /* separator */
    printf("\x1b[%d;%dH%s\xe2\x94\x82",y0+4,x0+1,TH_TDIM);
    for(k=0;k<inner;k++) fputs("\xe2\x94\x80",stdout);
    printf("\x1b[0m%s\xe2\x94\x82\x1b[0m",TH_TDIM);
    /* results */
    for(r=0;r<rows;r++){
        int idx=e->pf_top+r;
        int yy=y0+5+r,used=0;
        printf("\x1b[%d;%dH",yy,x0+1);
        if(idx==e->pf_sel&&idx<e->pf_n) printf("\x1b[48;2;0;120;215m\x1b[97m");
        else printf("\x1b[48;2;30;30;30m\x1b[97m");
        printf("\xe2\x94\x82");
        if(idx<e->pf_n){
            EdPFind *p=&e->pf_res[idx];
            int tw2=ed_pfind_put_hl(p->text,e->pf_buf,tw);
            for(i=tw2;i<tw;i++) fputc(' ',stdout);
            printf(" \xe2\x94\x82 ");
            printf("%*d",lw,p->line);
            printf(" \xe2\x94\x82 ");
            { int fw2=ed_pfind_put(p->file,fw); for(i=fw2;i<fw;i++) fputc(' ',stdout); }
            used=tw+lw+fw+12;
        } else {
            if(r==0){
                const char *empty=e->pf_len?"No matches for this query":"Type text to search all project files";
                printf("\x1b[90m"); used=ed_pfind_put(empty,inner); printf("\x1b[97m");
            }
        }
        for(i=used;i<inner;i++) fputc(' ',stdout);
        printf("\x1b[0m");
        printf("\x1b[%d;%dH%s\xe2\x94\x82\x1b[0m",yy,x0+bw,TH_TDIM);
    }
    /* input bar under results */
    barrow=y0+5+rows;
    printf("\x1b[%d;%dH%s\xe2\x94\x82",barrow,x0+1,TH_TDIM);
    printf("\x1b[48;2;28;28;28m\x1b[97m");
    printf(" text> ");
    qx=x0+9;
    statusw=(int)strlen(cnt);
    if(statusw>inner/2) statusw=inner/2;
    qwidth=inner-7-statusw-1;
    if(qwidth<1) qwidth=1;
    caretw=0; drawn=0; startw=0; first=0;
    for(f=0;f<e->pf_cur;f++) caretw+=ed_chw((unsigned char)e->pf_buf[f],caretw);
    if(caretw>qwidth) startw=caretw-qwidth+1;
        {
            int col=0; size_t ff=0;
            while(ff<e->pf_len){ int w0=ed_chw((unsigned char)e->pf_buf[ff],col); if(col>=startw) break; if(col+w0>startw){ startw=col; break; } col+=w0; ff++; first=(size_t)ff; }
    }
    for(f=first;f<e->pf_len;f++){
        int w0=ed_chw((unsigned char)e->pf_buf[f],drawn);
        if(drawn+w0>qwidth) break;
        fputc(e->pf_buf[f],stdout); drawn+=w0;
        while(f+1<e->pf_len && ed_is_cont((unsigned char)e->pf_buf[f+1])){ f++; fputc(e->pf_buf[f],stdout); }
    }
    if(e->pf_len==0){ printf("\x1b[90m"); drawn=ed_pfind_put("search project files",qwidth); }
    printf("\x1b[48;2;30;30;30m");
    for(;drawn<qwidth;drawn++) fputc(' ',stdout);
    printf("\x1b[%d;%dH\x1b[48;2;30;30;30m\x1b[90m",barrow,qx+qwidth+1);
    fwrite(cnt,1,(size_t)statusw,stdout);
    printf("\x1b[0m%s\xe2\x94\x82\x1b[0m",TH_TDIM);
    /* bottom border */
    printf("\x1b[%d;%dH%s\xe2\x94\x94",y0+6+rows,x0+1,TH_TDIM);
    for(k=0;k<inner;k++) fputs("\xe2\x94\x80",stdout);
    printf("\xe2\x94\x98\x1b[0m");
    printf("\x1b[%d;%dH\x1b[?25h",barrow,qx+(caretw-startw));
}

/* ---------------- Find panel (:find / :ff) ---------------- */
static void ed_fnd_clear(Editor *e){
    e->fnd_n=0; e->fnd_total=0; e->fnd_scanned=0; e->fnd_trunc=0;
    e->fnd_sel=0; e->fnd_top=0; e->fnd_nofolder=0;
}
static void ed_fnd_add(Editor *e,const char *label,const char *full,int line,const char *text){
    EdFnd *r;
    e->fnd_total++;
    if(e->fnd_n>=ED_FND_MAX){ e->fnd_trunc=1; return; }
    if(e->fnd_n>=e->fnd_cap){
        int cap=e->fnd_cap?e->fnd_cap*2:64;
        if(cap>ED_FND_MAX) cap=ED_FND_MAX;
        e->fnd_res=(EdFnd*)lrealloc(e->fnd_res,sizeof(EdFnd)*(size_t)cap);
        e->fnd_cap=cap;
    }
    if(e->fnd_n>=e->fnd_cap){ e->fnd_trunc=1; return; }
    r=&e->fnd_res[e->fnd_n];
    snprintf(r->file,sizeof r->file,"%s",label?label:"");
    snprintf(r->full,sizeof r->full,"%s",full?full:"");
    r->line=line;
    snprintf(r->text,sizeof r->text,"%s",text?text:"");
    e->fnd_n++;
}
static int ed_fnd_cmp(const void *a,const void *b){
    const EdFnd *x=(const EdFnd*)a,*y=(const EdFnd*)b;
    int c=_stricmp(x->file,y->file);
    if(c) return c;
    return x->line-y->line;
}
static int ed_same_path(const char *a,const char *b);
/* matches in the file that is open right now */
static void ed_fnd_scan_buffer(Editor *e){
    size_t li; int line=1;
    const char *bn; char label[300];
    if(!e->path[0]||!e->fnd_len) return;
    bn=strrchr(e->path,'\\'); if(!bn) bn=strrchr(e->path,'/');
    snprintf(label,sizeof label,"%s",bn?bn+1:e->path);
    for(li=0;li<e->n;li++){
        const char *b=e->ln[li].b; size_t L=e->ln[li].len,at=0;
        if(ed_pfind_match(b,L,e->fnd_q,e->fnd_len,&at)){
            char sn[160];
            ed_pfind_snippet(sn,sizeof sn,b,L,at);
            ed_fnd_add(e,label,e->path,line,sn);
        }
        line++;
    }
}
static void ed_fnd_scan_file(Editor *e,const char *full,const char *rel){
    FILE *f; long sz; char *buf; size_t got=0,i,start=0,probe; int line=1;
    if(e->fnd_n>=ED_FND_MAX){ e->fnd_trunc=1; return; }
    if(e->path[0]&&ed_same_path(full,e->path)) return;   /* already listed from the buffer */
    { size_t k; for(k=0;k<e->nstash;k++) if(e->stash[k].path[0]&&ed_same_path(full,e->stash[k].path)) return; }
    e->fnd_scanned++;
    f=fopen(full,"rb");
    if(!f) return;
    fseek(f,0,SEEK_END); sz=ftell(f); fseek(f,0,SEEK_SET);
    if(sz<=0||sz>2*1024*1024){ fclose(f); return; }
    buf=(char*)lmalloc((size_t)sz+1);
    got=fread(buf,1,(size_t)sz,f);
    fclose(f);
    buf[got]=0;
    probe=got<8192?got:8192;
    for(i=0;i<probe;i++) if(!buf[i]){ free(buf); return; }
    for(i=0;i<=got;i++){
        if(i==got||buf[i]=='\n'){
            size_t L=i-start,at=0;
            if(L>0&&buf[start+L-1]=='\r') L--;
            if(ed_pfind_match(buf+start,L,e->fnd_q,e->fnd_len,&at)){
                char sn[160];
                ed_pfind_snippet(sn,sizeof sn,buf+start,L,at);
                ed_fnd_add(e,rel,full,line,sn);
            }
            start=i+1; line++;
        }
    }
    free(buf);
}
static void ed_fnd_scan_dir(Editor *e,const char *dir,const char *rel,int depth){
    char pat[1300],child[1300],childrel[600];
    WIN32_FIND_DATAA fd;
    HANDLE h;
    if(depth>24) return;
    if(e->fnd_scanned>20000) return;
    if(strlen(dir)+3>=sizeof pat) return;
    snprintf(pat,sizeof pat,"%s\\*",dir);
    h=FindFirstFileA(pat,&fd);
    if(h==INVALID_HANDLE_VALUE) return;
    do{
        DWORD attr=fd.dwFileAttributes;
        if(!strcmp(fd.cFileName,".")||!strcmp(fd.cFileName,"..")) continue;
        if(attr&FILE_ATTRIBUTE_REPARSE_POINT) continue;
        if(rel[0]) snprintf(childrel,sizeof childrel,"%s\\%s",rel,fd.cFileName);
        else snprintf(childrel,sizeof childrel,"%s",fd.cFileName);
        if(strlen(dir)+strlen(fd.cFileName)+2>=sizeof child) continue;
        snprintf(child,sizeof child,"%s\\%s",dir,fd.cFileName);
        if(attr&FILE_ATTRIBUTE_DIRECTORY){
            if(ed_pfind_skip_dir(fd.cFileName)) continue;
            ed_fnd_scan_dir(e,child,childrel,depth+1);
        } else {
            ULONGLONG size=((ULONGLONG)fd.nFileSizeHigh<<32)|fd.nFileSizeLow;
            if(size>2*1024*1024) continue;
            ed_fnd_scan_file(e,child,childrel);
        }
    }while(FindNextFileA(h,&fd));
    FindClose(h);
}
/* file names that contain the query */
static void ed_fnd_scan_names(Editor *e,const char *dir,const char *rel,int depth){
    char pat[1300],child[1300],childrel[600];
    WIN32_FIND_DATAA fd;
    HANDLE h;
    if(depth>24) return;
    if(e->fnd_scanned>20000) return;
    if(strlen(dir)+3>=sizeof pat) return;
    snprintf(pat,sizeof pat,"%s\\*",dir);
    h=FindFirstFileA(pat,&fd);
    if(h==INVALID_HANDLE_VALUE) return;
    do{
        DWORD attr=fd.dwFileAttributes;
        if(!strcmp(fd.cFileName,".")||!strcmp(fd.cFileName,"..")) continue;
        if(attr&FILE_ATTRIBUTE_REPARSE_POINT) continue;
        if(rel[0]) snprintf(childrel,sizeof childrel,"%s\\%s",rel,fd.cFileName);
        else snprintf(childrel,sizeof childrel,"%s",fd.cFileName);
        if(strlen(dir)+strlen(fd.cFileName)+2>=sizeof child) continue;
        snprintf(child,sizeof child,"%s\\%s",dir,fd.cFileName);
        if(attr&FILE_ATTRIBUTE_DIRECTORY){
            if(ed_pfind_skip_dir(fd.cFileName)) continue;
            ed_fnd_scan_names(e,child,childrel,depth+1);
        } else {
            size_t nl=strlen(fd.cFileName),i,k;
            e->fnd_scanned++;
            if(nl<e->fnd_len) continue;
            for(i=0;i+e->fnd_len<=nl;i++){
                for(k=0;k<e->fnd_len;k++)
                    if(!ed_find_ch_eq((unsigned char)fd.cFileName[i+k],(unsigned char)e->fnd_q[k])) break;
                if(k==e->fnd_len){ ed_fnd_add(e,childrel,child,0,fd.cFileName); break; }
            }
        }
    }while(FindNextFileA(h,&fd));
    FindClose(h);
}
static void ed_fnd_refresh(Editor *e){
    int cur=0; size_t i;
    ed_fnd_clear(e);
    if(!e->fnd_len) return;
    if(e->fnd_mode==1){
        if(!e->exdir[0]){ e->fnd_nofolder=1; return; }
        ed_fnd_scan_names(e,e->exdir,"",0);
        return;
    }
    ed_fnd_scan_buffer(e);            /* the open file comes first */
    for(i=0;i<e->nstash;i++){
        EdStash *s=&e->stash[i]; const char *base=s->path,*p;
        for(p=s->path;*p;p++) if(*p=='\\'||*p=='/') base=p+1;
        if(e->exdir[0]){
            size_t root=strlen(e->exdir);
            if(strlen(s->path)>root&&!_strnicmp(s->path,e->exdir,root)&&
               (s->path[root]=='\\'||s->path[root]=='/')) base=s->path+root+1;
        }
        { size_t li; int line=1;
          for(li=0;li<s->n;li++){
              const char *b=s->ln[li].b; size_t L=s->ln[li].len,at=0;
              if(ed_pfind_match(b,L,e->fnd_q,e->fnd_len,&at)){
                  char sn[160]; ed_pfind_snippet(sn,sizeof sn,b,L,at);
                  ed_fnd_add(e,base,s->path,line,sn);
              }
              line++;
          } }
    }
    cur=e->fnd_n;
    if(e->exdir[0]){
        ed_fnd_scan_dir(e,e->exdir,"",0);
        if(e->fnd_n>cur) qsort(e->fnd_res+cur,(size_t)(e->fnd_n-cur),sizeof(EdFnd),ed_fnd_cmp);
    }
}
static int ed_replace_find(const char *s,size_t n,const char *q,size_t qn,size_t *at){
    size_t i,k;
    if(!qn) return 0;
    for(i=0;i+qn<=n;i++){
        for(k=0;k<qn;k++)
            if(!ed_find_ch_eq((unsigned char)s[i+k],(unsigned char)q[k])) break;
        if(k==qn){ *at=i; return 1; }
    }
    return 0;
}
static char *ed_replace_bytes(const char *src,size_t n,const char *q,size_t qn,
                              const char *r,size_t rn,size_t *outn,size_t *count){
    size_t i=0, hits=0, cap, used=0, at;
    char *out;
    if(!qn||n>16u*1024u*1024u) return NULL;
    while(i<n){
        if(ed_replace_find(src+i,n-i,q,qn,&at)){ hits++; i+=at+qn; }
        else break;
    }
    *count=hits; *outn=n;
    if(!hits) return NULL;
    if(rn>=qn){
        if(hits>(16u*1024u*1024u-n)/(rn-qn+1)) return NULL;
        cap=n+hits*(rn-qn);
    } else cap=n;
    out=(char*)malloc(cap+1);
    if(!out) return NULL;
    i=0;
    while(i<n){
        if(ed_replace_find(src+i,n-i,q,qn,&at)){
            size_t pre=at;
            if(pre){ memcpy(out+used,src+i,pre); used+=pre; }
            if(rn){ memcpy(out+used,r,rn); used+=rn; }
            i+=at+qn;
        } else {
            memcpy(out+used,src+i,n-i); used+=n-i; i=n;
        }
    }
    out[used]=0; *outn=used;
    return out;
}
static size_t ed_replace_lines(EdLine *ln,size_t n,const char *q,size_t qn,const char *r,size_t rn,int *failed){
    size_t i,total=0;
    for(i=0;i<n;i++){
        EdLine *l=&ln[i]; size_t count=0,newn=0;
        char *b=ed_replace_bytes(l->b,l->len,q,qn,r,rn,&newn,&count);
        if(!b){ if(count&&failed) *failed=1; continue; }
        if(newn>l->len) ed_line_reserve(l,newn-l->len);
        memcpy(l->b,b,newn+1); l->len=newn;
        total+=count; free(b);
    }
    return total;
}
static void ed_sanitize(char *d,size_t cap,const char *s);
static void ed_log(Editor *e,const char *fmt,...);
static int ed_git_quote(char *dst,size_t cap,const char *src){
    size_t i,w=0,slashes=0,j;
    if(cap<3) return 0;
    dst[w++]='"';
    for(i=0;src[i];i++){
        if(src[i]=='\\'){ slashes++; continue; }
        if(src[i]=='"'){
            for(j=0;j<slashes*2+1;j++){ if(w+1>=cap) return 0; dst[w++]='\\'; }
            if(w+1>=cap) return 0;
            dst[w++]=src[i];
        } else {
            for(j=0;j<slashes;j++){ if(w+1>=cap) return 0; dst[w++]='\\'; }
            if(w+1>=cap) return 0;
            dst[w++]=src[i];
        }
        slashes=0;
    }
    for(j=0;j<slashes*2;j++){ if(w+1>=cap) return 0; dst[w++]='\\'; }
    if(w+2>cap) return 0;
    dst[w++]='"'; dst[w]=0;
    return 1;
}
static void ed_git_run(Editor *e,const char *action,const char *message){
    char cmd[4096], quoted[2048], patharg[2300], chunk[512], line[512], cwd[1024],fullpath[1024];
    SECURITY_ATTRIBUTES sa; STARTUPINFOA si; PROCESS_INFORMATION pi;
    HANDLE rd=NULL,wr=NULL; DWORD got=0,exitcode=1,n; int nlines=0; size_t used=0;
    if(!e->exdir[0]){ snprintf(e->status,sizeof e->status,"git: no project folder"); return; }
    n=GetFullPathNameA(e->exdir,sizeof cwd,cwd,NULL);
    if(!n||n>=sizeof cwd){ snprintf(e->status,sizeof e->status,"git: invalid project folder"); return; }
    if(!strcmp(action,"stage")&&!e->path[0]){
        snprintf(e->status,sizeof e->status,"git stage: open a file first"); return;
    }
    if(e->path[0]&&(!strcmp(action,"stage")||!strcmp(action,"diff"))){
        n=GetFullPathNameA(e->path,sizeof fullpath,fullpath,NULL);
        if(!n||n>=sizeof fullpath||!ed_git_quote(patharg,sizeof patharg,fullpath)){
            snprintf(e->status,sizeof e->status,"git: file path is too long"); return;
        }
    }
    if(!strcmp(action,"stage")) snprintf(cmd,sizeof cmd,"git add -- %s",patharg);
    else if(!strcmp(action,"diff")){
        if(e->path[0]) snprintf(cmd,sizeof cmd,"git diff -- %s",patharg);
        else snprintf(cmd,sizeof cmd,"git diff");
    } else if(!strcmp(action,"commit")){
        if(!message||!*message){ snprintf(e->status,sizeof e->status,"usage: :git commit <message>"); return; }
        if(!ed_git_quote(quoted,sizeof quoted,message)){
            snprintf(e->status,sizeof e->status,"git: commit message is too long"); return;
        }
        snprintf(cmd,sizeof cmd,"git commit -m %s",quoted);
    } else snprintf(cmd,sizeof cmd,"git status --short");
    memset(&sa,0,sizeof sa); sa.nLength=sizeof sa; sa.bInheritHandle=TRUE;
    if(!CreatePipe(&rd,&wr,&sa,0)||!SetHandleInformation(rd,HANDLE_FLAG_INHERIT,0)){
        if(rd) CloseHandle(rd);
        if(wr) CloseHandle(wr);
        snprintf(e->status,sizeof e->status,"git: cannot create output pipe"); return;
    }
    memset(&si,0,sizeof si); si.cb=sizeof si; si.dwFlags=STARTF_USESTDHANDLES|STARTF_USESHOWWINDOW;
    si.wShowWindow=SW_HIDE; si.hStdOutput=wr; si.hStdError=wr; si.hStdInput=GetStdHandle(STD_INPUT_HANDLE);
    memset(&pi,0,sizeof pi);
    if(!CreateProcessA(NULL,cmd,NULL,NULL,TRUE,CREATE_NO_WINDOW,NULL,cwd,&si,&pi)){
        CloseHandle(rd); CloseHandle(wr);
        snprintf(e->status,sizeof e->status,"git: unable to start (is Git installed?)"); return;
    }
    CloseHandle(wr);
    e->log_expand=1; e->log_scroll=0;
    snprintf(line,sizeof line,"$ %s",cmd); ed_log(e,"%s",line);
    line[0]=0;
    while(ReadFile(rd,chunk,sizeof chunk,&got,NULL)&&got){
        DWORD i;
        for(i=0;i<got;i++){
            if(chunk[i]=='\n'||chunk[i]=='\r'){
                if(used){
                    line[used]=0; ed_sanitize(quoted,sizeof quoted,line);
                    ed_log(e,"%s",quoted); nlines++; used=0;
                }
            } else if(used+1<sizeof line) line[used++]=chunk[i];
        }
    }
    if(used){ line[used]=0; ed_sanitize(quoted,sizeof quoted,line); ed_log(e,"%s",quoted); nlines++; }
    CloseHandle(rd); WaitForSingleObject(pi.hProcess,INFINITE);
    GetExitCodeProcess(pi.hProcess,&exitcode);
    CloseHandle(pi.hThread); CloseHandle(pi.hProcess);
    snprintf(e->status,sizeof e->status,"git %s: %s%s",action,
        exitcode?"failed":"done",nlines?" (see terminal)":"");
}
static void ed_replace_apply(Editor *e){
    size_t i,changed=0; int files=0,fail=0;
    if(!e->fnd_len||!e->fnd_replace_valid){ snprintf(e->status,sizeof e->status,"replace needs search and replacement text"); return; }
    if(e->path[0]&&!e->is_binary){
        ed_undo_push(e,0);
        size_t n=ed_replace_lines(e->ln,e->n,e->fnd_q,e->fnd_len,e->fnd_repl,strlen(e->fnd_repl),&fail);
        if(n){ e->dirty=1; ed_note_edit(e); e->diag_pending=1; e->recovery_pending=1; changed+=n; files++; }
    }
    for(i=0;i<e->nstash;i++) if(!e->stash[i].is_binary){
        size_t n=ed_replace_lines(e->stash[i].ln,e->stash[i].n,e->fnd_q,e->fnd_len,e->fnd_repl,strlen(e->fnd_repl),&fail);
        if(n){
            size_t len=0; char *data;
            data=ed_snapshot(e->stash[i].ln,e->stash[i].n,&len);
            e->stash[i].dirty=!e->stash[i].saved||len!=e->stash[i].saved_len||
                memcmp(data,e->stash[i].saved,len)!=0;
            free(data); e->last_edit_tick=GetTickCount(); e->recovery_pending=1; changed+=n; files++;
        }
    }
    for(i=0;i<(size_t)e->fnd_n;i++){
        size_t j; int seen=0, len=0; char *src,*out,tmp[1100]; size_t outn=0,count=0;
        for(j=0;j<i;j++) if(ed_same_path(e->fnd_res[j].full,e->fnd_res[i].full)){ seen=1; break; }
        if(seen||!e->fnd_res[i].full[0]) continue;
        if((e->path[0]&&ed_same_path(e->path,e->fnd_res[i].full))) continue;
        for(j=0;j<e->nstash;j++) if(e->stash[j].path[0]&&ed_same_path(e->stash[j].path,e->fnd_res[i].full)){ seen=1; break; }
        if(seen) continue;
        src=read_file(e->fnd_res[i].full,&len);
        if(!src||len<0){
            if(src) free(src);
            fail++; continue;
        }
        if(memchr(src,0,(size_t)len)){
            free(src);
            continue;
        }
        out=ed_replace_bytes(src,(size_t)len,e->fnd_q,e->fnd_len,e->fnd_repl,
                             strlen(e->fnd_repl),&outn,&count);
        free(src);
        if(!count){ free(out); continue; }
        if(!out){ fail++; continue; }
        if(snprintf(tmp,sizeof tmp,"%s.lcode-%lu.tmp",e->fnd_res[i].full,
                    (unsigned long)GetCurrentProcessId())>=(int)sizeof tmp){
            free(out); fail++; continue;
        }
        { FILE *f=fopen(tmp,"wb"); int ok=0;
          if(f){ int wrote=fwrite(out,1,outn,f)==outn; int closed=fclose(f)==0;
              ok=wrote&&closed;
          }
          if(!ok||!MoveFileExA(tmp,e->fnd_res[i].full,MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH)){
              remove(tmp); fail++; free(out); continue;
          } }
        free(out); changed+=count; files++;
    }
    e->fnd_replace_armed=0;
    if(fail) snprintf(e->status,sizeof e->status,"replaced %d match(es) in %d file(s); %d file(s) failed", (int)changed,files,fail);
    else snprintf(e->status,sizeof e->status,"replaced %d match(es) in %d file(s)",(int)changed,files);
    if(changed) ed_fnd_refresh(e);
}
static void ed_fnd_open(Editor *e,int mode,const char *query){
    e->fnd_on=1; e->fnd_mode=mode;
    e->fnd_q[0]=0; e->fnd_len=0; e->fnd_cur=0;
    e->fnd_repl[0]=0; e->fnd_replace_armed=0; e->fnd_replace_valid=0;
    ed_fnd_clear(e);
    e->focus=0; e->sug_on=0; e->menu=0; e->status[0]=0;
    if(query&&*query){
        size_t n=strlen(query);
        if(n>=sizeof e->fnd_q) n=sizeof e->fnd_q-1;
        memcpy(e->fnd_q,query,n); e->fnd_q[n]=0;
        e->fnd_len=n; e->fnd_cur=n;
        ed_fnd_refresh(e);
    }
}
static void ed_fnd_close(Editor *e){ e->fnd_on=0; e->focus=0; }
static void ed_fnd_geom(Editor *e,int W,int H,int *x0,int *y0,int *w,int *h){
    int bw,bh,rows;
    bw=W-10; if(bw<36) bw=36; if(bw>W-2) bw=W-2;
    bh=H-10; if(bh<12) bh=12; if(bh>H) bh=H;
    rows=bh-6; if(rows<3) rows=3; if(rows>H-6) rows=H-6; if(rows<1) rows=1;
    bh=rows+6; if(bh>H){ bh=H; rows=bh-6; if(rows<1) rows=1; }
    *x0=(W-bw)/2; if(*x0<5) *x0=5;
    *y0=(H-bh)/2; if(*y0<5) *y0=5;
    *w=bw; *h=bh;
}
static int ed_fnd_rows(Editor *e){
    return e->fnd_vis>0?e->fnd_vis:10;
}
static void ed_fnd_scroll(Editor *e,int rows){
    if(rows<1) rows=1;
    if(e->fnd_sel<e->fnd_top) e->fnd_top=e->fnd_sel;
    if(e->fnd_sel>=e->fnd_top+rows) e->fnd_top=e->fnd_sel-rows+1;
    if(e->fnd_top<0) e->fnd_top=0;
}
static void ed_fnd_accept(Editor *e){
    EdFnd *r; char full[1300]; int rows;
    if(!e->fnd_n||e->fnd_sel<0||e->fnd_sel>=e->fnd_n) return;
    r=&e->fnd_res[e->fnd_sel];
    snprintf(full,sizeof full,"%s",r->full[0]?r->full:r->file);
    if(strcmp(full,e->path)) ed_open_path(e,full);
    if(r->line>0){
        size_t at=0;
        e->cy=(size_t)(r->line-1);
        ed_clamp(e);
        if(e->cy<e->n&&ed_pfind_match(e->ln[e->cy].b,e->ln[e->cy].len,e->fnd_q,e->fnd_len,&at))
            e->cx=at;
        rows=e->vh>5?e->vh-4:1;
        if(e->cy>(size_t)(rows/2)) e->top=e->cy-(size_t)(rows/2); else e->top=0;
    }
    e->fnd_on=0; e->focus=0;
    e->sel_on=0; e->sel_armed=0; e->sug_on=0;
    snprintf(e->status,sizeof e->status,"opened %s%s%d",r->file,r->line>0?":":"",r->line);
}
static int ed_fnd_key(Editor *e,WORD vk,WCHAR ch,DWORD ctl){
    int ctrl=(ctl&(LEFT_CTRL_PRESSED|RIGHT_CTRL_PRESSED))!=0;
    int alt=(ctl&(LEFT_ALT_PRESSED|RIGHT_ALT_PRESSED))!=0;
    int rows=ed_fnd_rows(e);
    if(ch==27||(ch==0&&vk==VK_ESCAPE)){ ed_fnd_close(e); return 0; }
    if(e->fnd_mode==2&&ch==0&&vk==VK_F4){
        if(!e->fnd_replace_armed){
            e->fnd_replace_armed=1;
            snprintf(e->status,sizeof e->status,"preview listed matches; press F4 again to apply replacement to the project");
        } else ed_replace_apply(e);
        return 0;
    }
    if(ch==13||(ch==0&&vk==VK_RETURN)){ ed_fnd_accept(e); return 0; }
    if(ch==0){
        if(vk==VK_UP&&e->fnd_sel>0) e->fnd_sel--;
        else if(vk==VK_DOWN&&e->fnd_sel+1<e->fnd_n) e->fnd_sel++;
        else if(vk==VK_PRIOR) e->fnd_sel=e->fnd_sel-rows>0?e->fnd_sel-rows:0;
        else if(vk==VK_NEXT) e->fnd_sel=e->fnd_sel+rows<e->fnd_n?e->fnd_sel+rows:(e->fnd_n>0?e->fnd_n-1:0);
        else if(vk==VK_LEFT){
            if(e->fnd_cur>0){ e->fnd_cur--; while(e->fnd_cur>0&&ed_is_cont((unsigned char)e->fnd_q[e->fnd_cur])) e->fnd_cur--; }
        } else if(vk==VK_RIGHT){
            if(e->fnd_cur<e->fnd_len){ e->fnd_cur++; while(e->fnd_cur<e->fnd_len&&ed_is_cont((unsigned char)e->fnd_q[e->fnd_cur])) e->fnd_cur++; }
        } else if(vk==VK_HOME) e->fnd_cur=0;
        else if(vk==VK_END) e->fnd_cur=e->fnd_len;
        else if(vk==VK_DELETE&&e->fnd_cur<e->fnd_len){
            size_t a=e->fnd_cur,b=a+1;
            while(b<e->fnd_len&&ed_is_cont((unsigned char)e->fnd_q[b])) b++;
            memmove(e->fnd_q+a,e->fnd_q+b,e->fnd_len-b+1);
            e->fnd_len-=b-a;
            e->fnd_replace_armed=0;
            ed_fnd_refresh(e);
        }
        ed_fnd_scroll(e,rows);
        return 0;
    }
    if(ch==8){
        if(e->fnd_cur>0){
            size_t a=e->fnd_cur-1;
            while(a>0&&ed_is_cont((unsigned char)e->fnd_q[a])) a--;
            memmove(e->fnd_q+a,e->fnd_q+e->fnd_cur,e->fnd_len-e->fnd_cur+1);
            e->fnd_len-=e->fnd_cur-a; e->fnd_cur=a;
            e->fnd_replace_armed=0;
            ed_fnd_refresh(e);
        }
        return 0;
    }
    if(ch>=32&&!ctrl&&!alt){
        char tmp[4]; int m=ed_cp_utf8((unsigned)ch,tmp);
        if(m>0&&e->fnd_len+(size_t)m<sizeof e->fnd_q){
            memmove(e->fnd_q+e->fnd_cur+(size_t)m,e->fnd_q+e->fnd_cur,e->fnd_len-e->fnd_cur+1);
            memcpy(e->fnd_q+e->fnd_cur,tmp,(size_t)m);
            e->fnd_cur+=(size_t)m; e->fnd_len+=(size_t)m;
            e->fnd_replace_armed=0;
            ed_fnd_refresh(e);
        }
    }
    return 0;
}
/* print text, the query match inside it in red */
static int ed_fnd_put_hl(const char *s,const char *q,int maxw){
    size_t ql=q?strlen(q):0,i,hl=0;
    int w=0, matched=0;
    if(ql){
        for(i=0;s[i];i++){
            size_t k; int ok=1;
            for(k=0;k<ql;k++)
                if(!s[i+k]||!ed_find_ch_eq((unsigned char)s[i+k],(unsigned char)q[k])){ ok=0; break; }
            if(ok){ hl=i; matched=1; break; }
        }
    }
    for(i=0;s[i];i++){
        int cw=ed_chw((unsigned char)s[i],w);
        if(cw<=0) cw=1;
        if(w+cw>maxw) break;
        if(ql&&matched&&i>=hl&&i<hl+ql) printf("%s",EC_FINDALL);
        fputc(s[i],stdout); w+=cw;
        if(ql&&matched&&i>=hl&&i<hl+ql) printf("\x1b[97m");
    }
    return w;
}
static void ed_fnd_draw(Editor *e,int W,int H){
    int x0,y0,bw,bh,i,k,rows,r,caretw=0,startw=0,drawn=0,qx,folderw,filew;
    size_t f;
    const char *label;
    char cnt[96];
    int inner;                     /* usable width between the borders */
    if(!e->fnd_on) return;
    ed_fnd_geom(e,W,H,&x0,&y0,&bw,&bh);
    rows=bh-6; if(rows<1) rows=1;
    inner=bw-2;
    e->fnd_vis=rows;
    folderw=inner/2; filew=inner-folderw-3;
    if(folderw<8) folderw=8;
    if(filew<8){ filew=8; folderw=inner-filew-3; if(folderw<8) folderw=8; }
    label=e->fnd_mode==1?"file":(e->fnd_mode==2?"replace":"find");
    if(e->fnd_sel>=e->fnd_n) e->fnd_sel=e->fnd_n>0?e->fnd_n-1:0;
    if(e->fnd_sel<0) e->fnd_sel=0;
    ed_fnd_scroll(e,rows);
    /* top border */
    printf("\x1b[%d;%dH%s\xe2\x94\x8c",y0+1,x0+1,TH_TDIM);
    for(k=0;k<inner;k++) fputs("\xe2\x94\x80",stdout);
    printf("\xe2\x94\x90\x1b[0m");
    /* title and keyboard guidance */
    printf("\x1b[%d;%dH%s\xe2\x94\x82",y0+2,x0+1,TH_TDIM);
    printf("\x1b[48;2;45;45;45m\x1b[97m");
    { char titlebuf[300];
      const char *title;
      const char *hint=e->fnd_mode==2?"Review preview; F4 twice applies  Esc close":"Up/Down select  Enter open  Esc close";
      if(e->fnd_mode==1) title=" FIND FILES ";
      else if(e->fnd_mode==2){
          snprintf(titlebuf,sizeof titlebuf," REPLACE %s -> %s ",e->fnd_q,e->fnd_repl);
          title=titlebuf;
      } else title=" FIND IN PROJECT ";
      int tl=(int)strlen(title),hl=(int)strlen(hint),hintstart=inner-hl;
      ed_pfind_put(title,inner);
      for(i=tl;i<inner;i++)
          fputc(inner>tl+hl+1&&i>=hintstart?hint[i-hintstart]:' ',stdout);
    }
    printf("\x1b[0m%s\xe2\x94\x82\x1b[0m",TH_TDIM);
    /* column header */
    printf("\x1b[%d;%dH%s\xe2\x94\x82",y0+3,x0+1,TH_TDIM);
    printf("\x1b[48;2;30;30;30m\x1b[90m");
    printf(" FOLDER");
    for(i=7;i<folderw;i++) fputc(' ',stdout);
    printf(" \xe2\x94\x82 ");
    printf("FILE");
    for(i=4;i<filew;i++) fputc(' ',stdout);
    printf("\x1b[0m");
    printf("\x1b[%d;%dH%s\xe2\x94\x82\x1b[0m",y0+3,x0+bw,TH_TDIM);
    /* separator */
    printf("\x1b[%d;%dH%s\xe2\x94\x82",y0+4,x0+1,TH_TDIM);
    for(k=0;k<inner;k++) fputs("\xe2\x94\x80",stdout);
    printf("\x1b[0m%s\xe2\x94\x82\x1b[0m",TH_TDIM);
    /* results */
    for(r=0;r<rows;r++){
        int idx=e->fnd_top+r;
        int yy=y0+5+r, used=0;
        printf("\x1b[%d;%dH",yy,x0+1);
        if(idx==e->fnd_sel&&idx<e->fnd_n) printf("\x1b[48;2;0;120;215m\x1b[97m");
        else printf("\x1b[48;2;30;30;30m\x1b[97m");
        printf("\xe2\x94\x82");
        if(idx<e->fnd_n){
            EdFnd *p=&e->fnd_res[idx];
            char folder[300], fname[160];
            char *slash;
            int w0,w1;
            snprintf(folder,sizeof folder,"%s",p->file);
            slash=strrchr(folder,'\\'); if(!slash) slash=strrchr(folder,'/');
            if(slash){ *slash=0; snprintf(fname,sizeof fname,"%s",slash+1); if(folder[0]==0) snprintf(folder,sizeof folder,"%s","."); }
            else { snprintf(fname,sizeof fname,"%s",folder); snprintf(folder,sizeof folder,"%s","."); }
            if(e->fnd_mode!=1){ snprintf(folder,sizeof folder,"%s",p->file); snprintf(fname,sizeof fname,"%s",p->text); }
            w0=ed_pfind_put(folder,folderw);
            for(i=w0;i<folderw;i++) fputc(' ',stdout);
            printf(" \xe2\x94\x82 ");
            if(e->fnd_mode==2){
                size_t pn=0,pc=0; char *preview=ed_replace_bytes(fname,strlen(fname),
                    e->fnd_q,e->fnd_len,e->fnd_repl,strlen(e->fnd_repl),&pn,&pc);
                if(preview){ w1=ed_pfind_put(preview,filew); free(preview); }
                else w1=ed_pfind_put(fname,filew);
            } else w1=ed_fnd_put_hl(fname,e->fnd_q,filew);
            for(i=w1;i<filew;i++) fputc(' ',stdout);
            used=inner;
        } else if(e->fnd_n==0&&r==0){
            if(e->fnd_mode==1&&e->fnd_nofolder){ printf(" (no folder)"); used=12; }
            else if(e->fnd_len){ snprintf(cnt,sizeof cnt,"No matches for '%s'",e->fnd_q); printf("%s",TH_TDIM); used=ed_pfind_put(cnt,inner); printf("\x1b[97m"); }
            else { used=ed_pfind_put(e->fnd_mode==1?"Type a name to search files":"Type text to search project files",inner); }
        }
        for(i=used;i<inner;i++) fputc(' ',stdout);
        printf("\x1b[0m");
        printf("\x1b[%d;%dH%s\xe2\x94\x82\x1b[0m",yy,x0+bw,TH_TDIM);
    }
    /* input bar at bottom */
    {
        int row=y0+5+rows, cl;
        size_t first=0;
        printf("\x1b[%d;%dH%s\xe2\x94\x82",row,x0+1,TH_TDIM);
        printf("\x1b[48;2;28;28;28m\x1b[97m");
        printf(" %s> ",label);
        qx=x0+5+(int)strlen(label); /* first query cell */
        caretw=0; drawn=0; startw=0;
        for(f=0;f<e->fnd_cur;f++) caretw+=ed_chw((unsigned char)e->fnd_q[f],caretw);
        if(caretw>inner-20) startw=caretw-(inner-20)+1;
        {
            int col=0; size_t ff=0;
            while(ff<e->fnd_len){
                int w0=ed_chw((unsigned char)e->fnd_q[ff],col);
                if(col>=startw) break;
                if(col+w0>startw){ startw=col; break; }
                col+=w0; ff++;
            }
            first=ff;
        }
        for(f=first;f<e->fnd_len;f++){
            int w0=ed_chw((unsigned char)e->fnd_q[f],drawn);
            if(drawn+w0>inner-20) break;
            fputc(e->fnd_q[f],stdout); drawn+=w0;
            while(f+1<e->fnd_len && ed_is_cont((unsigned char)e->fnd_q[f+1])){ f++; fputc(e->fnd_q[f],stdout); }
        }
        if(e->fnd_len==0) printf("\x1b[90m%s",e->fnd_mode==1?"search file names":"search text in file + project");
        printf("\x1b[48;2;30;30;30m");
        for(;drawn<inner-20;drawn++) fputc(' ',stdout);
        e->fnd_cur_x=qx+(caretw-startw);
        e->fnd_cur_y=row;
        if(e->fnd_mode==1&&e->fnd_nofolder) snprintf(cnt,sizeof cnt,"(no folder)");
        else if(e->fnd_len) snprintf(cnt,sizeof cnt,"%d%s match%s",e->fnd_total,e->fnd_trunc?"+":"",e->fnd_total==1?"":"es");
        else cnt[0]=0;
        if(cnt[0]){
            cl=(int)strlen(cnt);
            printf("\x1b[%d;%dH\x1b[48;2;30;30;30m\x1b[90m%s",row,x0+1+inner-cl,cnt);
        }
        printf("\x1b[0m%s\xe2\x94\x82\x1b[0m",TH_TDIM);
    }
    /* bottom border */
    printf("\x1b[%d;%dH%s\xe2\x94\x94",y0+6+rows,x0+1,TH_TDIM);
    for(k=0;k<inner;k++) fputs("\xe2\x94\x80",stdout);
    printf("\xe2\x94\x98\x1b[0m");
    if(e->fnd_cur_x>0) printf("\x1b[%d;%dH\x1b[?25h",e->fnd_cur_y,e->fnd_cur_x);
}

/* ':' command hints in the shell (canonical commands only: no aliases) */
static const char *LUC_CMDS[]={
    "pfind","find","findfile","git","help","plugin","run","setting",NULL};
static int ed_cmd_hint_list(Editor *e,char items[16][32],int *sel){
    char pre[64]; size_t n=0; int i,count=0;
    if(e->focus!=2||e->shlen<1||e->shbuf[0]!=':') return 0;
    for(i=1;i<e->shlen&&n+1<sizeof pre;i++){
        char c=e->shbuf[i];
        if(c==' '||c=='\t') break;
        pre[n++]=(char)tolower((unsigned char)c);
    }
    pre[n]=0;
    for(i=0;LUC_CMDS[i];i++){
        if(!n||!strncmp(LUC_CMDS[i],pre,n)){
            if(count>=16) break;
            snprintf(items[count],32,"%s",LUC_CMDS[i]);
            count++;
        }
    }
    if(sel&&*sel>=count) *sel=count>0?count-1:0;
    return count;
}
static void ed_cmd_hint_draw(Editor *e,int W,int H){
    char items[16][32];
    int n=ed_cmd_hint_list(e,items,&e->cmdh_sel);
    int i,bh=n,first=0,limit=H>1?H-1:1;
    (void)W;
    if(bh>limit) bh=limit;
    if(bh<=0) return;
    if(e->cmdh_sel>=bh) first=e->cmdh_sel-bh+1;
    for(i=0;i<bh;i++){
        int row=H-bh+i;
        int idx=first+i;
        if(row<1) continue;
        printf("\x1b[%d;1H",row);
        if(idx==e->cmdh_sel) printf("\x1b[48;2;0;120;215m\x1b[97m");
        else printf("\x1b[48;2;28;28;28m\x1b[97m");
        printf(" \xe2\x97\x8f :%s",items[idx]);
        { int used=(int)strlen(items[idx])+5,k;
          for(k=used;k<26;k++) fputc(' ',stdout); }
        printf("\x1b[K\x1b[0m");
    }
}

static int ed_diag_has(const Editor *e,size_t line){
    int i;
    for(i=0;i<e->diag_n;i++) if(e->diag[i].line==(int)line+1) return 1;
    return 0;
}
static void ed_diag_clear(Editor *e){ e->diag_n=0; e->diag_sel=-1; }
static void ed_diag_add(Editor *e,int line,const char *msg){
    int i;
    if(line<=0||e->diag_n>=ED_DIAG_MAX) return;
    for(i=0;i<e->diag_n;i++)
        if(e->diag[i].line==line&&!strcmp(e->diag[i].msg,msg)) return;
    e->diag[e->diag_n].line=line;
    snprintf(e->diag[e->diag_n].msg,sizeof e->diag[0].msg,"%s",msg&&*msg?msg:"error");
    e->diag_n++;
}
static void ed_diag_jump(Editor *e,int dir){
    if(!e->diag_n){ snprintf(e->status,sizeof e->status,"no diagnostics"); return; }
    if(e->diag_sel<0) e->diag_sel=dir>0?0:e->diag_n-1;
    else e->diag_sel=(e->diag_sel+dir+e->diag_n)%e->diag_n;
    e->cy=(size_t)(e->diag[e->diag_sel].line-1);
    e->cx=0; e->focus=0; e->top=e->cy>3?e->cy-3:0;
    ed_clamp(e);
    snprintf(e->status,sizeof e->status,"error %d/%d: line %d: %s",
        e->diag_sel+1,e->diag_n,e->diag[e->diag_sel].line,e->diag[e->diag_sel].msg);
}
static void ed_diag_parse(Editor *e,const char *line){
    const char *c=line;
    while((c=strchr(c,':'))) {
        if(c[1]>='0'&&c[1]<='9'){
            char *end; long ln=strtol(c+1,&end,10);
            if(*end==':'&&ln>0&&ln<1000000){
                size_t n=(size_t)(c-line);
                if(n<1024){
                    char pre[1024];
                    memcpy(pre,line,n); pre[n]=0;
                    if(strstr(pre,".luc")){
                        const char *m=end+1;
                        while(*m==' ') m++;
                        ed_diag_add(e,(int)ln,m);
                        return;
                    }
                }
            }
        }
        c++;
    }
}
static void ed_diag_live(Editor *e){
    char stack[256]; int stackline[256],depth=0,quote=0,qline=0,block=0;
    int lang=ed_lang(e->path);
    size_t y,x;
    if(!e->path[0]||lang==L_NONE||lang==L_MD||lang==L_HTML){ ed_diag_clear(e); return; }
    ed_diag_clear(e);
    for(y=0;y<e->n&&e->diag_n<ED_DIAG_MAX;y++){
        const char *s=e->ln[y].b; size_t n=e->ln[y].len;
        for(x=0;x<n;x++){
            char c=s[x];
            if(quote){
                if(c=='\\'&&x+1<n){ x++; continue; }
                if(c==quote) quote=0;
                continue;
            }
            if(block){
                if(c=='*'&&x+1<n&&s[x+1]=='/'){ block=0; x++; }
                continue;
            }
            if(ed_is_cstyle_lang(lang)&&c=='/'&&x+1<n){
                if(x+1<n&&s[x+1]=='*'){ block=1; x++; continue; }
                if(s[x+1]=='/') break;
            }
            if(ed_is_hash_lang(lang)&&c=='#') break;
            if(ed_is_dash_comment_lang(lang)&&c=='-'&&x+1<n&&s[x+1]=='-') break;
            if(c=='\''||c=='"'){ quote=c; qline=(int)y+1; continue; }
            if(c=='('||c=='['||c=='{'){
                if(depth<256){ stack[depth]=c; stackline[depth++]=(int)y+1; }
                continue;
            }
            if(c==')'||c==']'||c=='}'){
                char want=c==')'?'(':c==']'?'[':'{';
                if(depth==0||stack[depth-1]!=want){
                    char msg[64]; snprintf(msg,sizeof msg,"unmatched '%c'",c);
                    ed_diag_add(e,(int)y+1,msg);
                } else depth--;
            }
        }
    }
    if(quote&&e->diag_n<ED_DIAG_MAX) ed_diag_add(e,qline,"unterminated string");
    else if(block&&e->diag_n<ED_DIAG_MAX) ed_diag_add(e,(int)e->n,"unterminated comment");
    else if(depth>0&&e->diag_n<ED_DIAG_MAX){
        char msg[64]; snprintf(msg,sizeof msg,"unclosed '%c'",stack[depth-1]);
        ed_diag_add(e,stackline[depth-1],msg);
    }
    e->diag_sel=e->diag_n?0:-1;
}

/* Log panel: ring of recent messages (panel shows the tail) */
static void ed_log(Editor *e,const char *fmt,...){    char b[240]; va_list ap;
    va_start(ap,fmt); vsnprintf(b,sizeof b,fmt,ap); va_end(ap);
    if(e->logn>=ED_LOGMAX){
        memmove(e->logs[0],e->logs[1],sizeof e->logs[0]*(ED_LOGMAX-1));
        e->logn=ED_LOGMAX-1;
    }
    snprintf(e->logs[e->logn++],sizeof e->logs[0],"%s",b);
    e->log_scroll=0;   /* new output tails the view */
}

static void ed_popup_draw(Editor *e,int W,int H);
static void ed_plug_draw(Editor *e,int W,int H);
static void ed_set_draw(Editor *e,int W,int H);
static void ed_set_click(Editor *e,int x,int y,int W,int H);
static int ed_set_nrows(Editor *e,int tab);
static int ed_ex_item_draw(Editor *e,size_t ei,int cwmax);
static void ed_nam_draw(Editor *e,int W,int H);
/* Modal-only repaint: background is frozen while a modal owns input,
   so only the box is redrawn (no full-frame strobe over text) */
static void ed_draw_modal(Editor *e){
    int W=e->vw>0?e->vw:80, H=e->vh>0?e->vh:24, sync=0;
    HANDLE h=GetStdHandle(STD_OUTPUT_HANDLE);
    CONSOLE_SCREEN_BUFFER_INFO bi;
    if(GetConsoleScreenBufferInfo(h,&bi)) sync=1;
    printf("\x1b[?25l");
    if(sync) printf("\x1b[?2026h");
    if(e->menu) ed_popup_draw(e,W,H);
    else if(e->naming) ed_nam_draw(e,W,H);
    else if(e->show_plugins) ed_plug_draw(e,W,H);
    else if(e->show_settings) ed_set_draw(e,W,H);
    if(sync) printf("\x1b[?2026l");
    fflush(stdout);
}
static void ed_menu_open(Editor *e,int kind,int ax,int ay,const char *dir);
static void ed_naming_start(Editor *e,int mode);

static void ed_tabs_collect(Editor *e){
    size_t i;
    e->tab_n=0;
    if(e->path[0]){
        snprintf(e->tab_path[e->tab_n++],sizeof e->tab_path[0],"%s",e->path);
    }
    for(i=0;i<e->nstash&&e->tab_n<64;i++){
        size_t k; int duplicate=0;
        for(k=0;k<(size_t)e->tab_n;k++)
            if(!strcmp(e->tab_path[k],e->stash[i].path)){ duplicate=1; break; }
        if(!duplicate)
            snprintf(e->tab_path[e->tab_n++],sizeof e->tab_path[0],"%s",e->stash[i].path);
    }
}
static void ed_tab_cycle(Editor *e,int dir){
    int next;
    ed_tabs_collect(e);
    if(e->tab_n<2) return;
    if(!e->path[0]) next=dir<0?(int)e->tab_n-1:0;
    else next=dir<0?(int)e->tab_n-1:1;
    if(next>=0&&next<e->tab_n) ed_open_path(e,e->tab_path[next]);
}
static void ed_tab_click(Editor *e,int x){
    int i;
    for(i=0;i<e->tab_n;i++)
        if(x>=e->tab_x0[i]&&x<e->tab_x1[i]){
            char path[1024];
            snprintf(path,sizeof path,"%s",e->tab_path[i]);
            if(strcmp(path,e->path)) ed_open_path(e,path);
            return;
        }
}

/* One explorer row: indent, arrow, icon slot, name. When the row is the
   inline naming row its text comes from e->namb and the caret display col
   (0-based) is returned; -1 for every other row. */
static int ed_ex_item_draw(Editor *e,size_t ei,int cwmax){
    const char *nm;
    int cw=0, kk=0, i, isd, dep, col=0, sel, namrow;
    const DevIcon *ndi=NULL;
    if(ei>=e->exn) return -1;
    namrow=(e->naming&&(int)ei==e->namidx)?1:0;
    nm=namrow?e->namb:e->ex[ei].name;
    isd=e->ex[ei].isdir;
    dep=e->ex[ei].depth; if(dep<1) dep=1; if(dep>5) dep=5;
    if(!isd&&e->icons_nerd&&!namrow){
        ndi=ed_devicon(nm);
        if(!ndi) ndi=&DEVICON_DEF;
    }
    sel=(e->focus==1&&e->exsel_ok&&ei==e->exsel)||namrow;
    if(sel) printf("\x1b[48;2;0;120;215m\x1b[97m");
    else {
        col=isd||e->icons_nerd
                    ? ((ed_dir_dirty(e,e->ex[ei].full)||
                        (!isd&&ed_path_dirty(e,e->ex[ei].full)))?33:0)
                    : (ed_path_dirty(e,e->ex[ei].full)?33:ed_ex_color(nm,isd));
        if(col) printf("\x1b[%dm",col);
    }
    if(e->opt_guides){
        for(i=1;i<dep&&cw<cwmax-6;i++){ fputs("\x1b[90m\xe2\x94\x82\x1b[0m ",stdout); cw+=2; }
        if(cw<cwmax-6){ fputc(' ',stdout); fputc(' ',stdout); cw+=2; }
        if(sel) printf("\x1b[48;2;0;120;215m\x1b[97m");
        else if(col) printf("\x1b[%dm",col);
    } else {
        for(i=0;i<dep*2&&cw<cwmax-6;i++){ fputc(' ',stdout); cw++; }
    }
    if(isd){ fputs(e->ex[ei].expanded?"\xe2\x96\xbe":"\xe2\x96\xb8",stdout); cw++; }
    else { fputc(' ',stdout); cw++; }
    fputc(' ',stdout); cw++;
    if(isd){ fputs(e->ex[ei].expanded?"\xf0\x9f\x93\x82":"\xf0\x9f\x93\x81",stdout); cw+=2; }
    else if(ndi){ char ib[4]; int m=ed_cp_utf8(ndi->cp,ib);
        if(!sel) printf("\x1b[38;2;%u;%u;%um",
            (ndi->rgb>>16)&255,(ndi->rgb>>8)&255,ndi->rgb&255);
        fwrite(ib,1,(size_t)m,stdout);
        printf("\x1b[0m");
        if(sel) printf("\x1b[48;2;0;120;215m\x1b[97m");
        else if(col) printf("\x1b[%dm",col);
        cw+=1; }
    else { fputc(' ',stdout); fputc(' ',stdout); cw+=2; }
    fputc(' ',stdout); cw++;
    if(namrow){
        int tstart=cw, pw=0, offw=0, caret;
        size_t k2=0, off=0;
        { int cc=0;
          while(k2<e->namcur&&nm[k2]){ int ww=ed_chw((unsigned char)nm[k2],cc); pw+=ww; cc+=ww; k2++; } }
        e->namtx0=tstart;
        if(tstart+pw>cwmax-2){
            off=ed_byte_at((char*)nm,e->namlen,(size_t)(tstart+pw-(cwmax-2)));
            { int cc=0; size_t k3=0; while(k3<off&&nm[k3]){ int ww=ed_chw((unsigned char)nm[k3],cc); offw+=ww; cc+=ww; k3++; } }
        }
        e->namoff=off;
        if(e->namsel) printf("\x1b[7m");
        { size_t k3=off; while(nm[k3]&&cw<cwmax){ fputc(nm[k3],stdout); cw++; k3++; } }
        if(e->namsel) printf("\x1b[27m");
        caret=tstart+pw-offw;
        while(cw<cwmax){ fputc(' ',stdout); cw++; }
        printf("\x1b[0m");
        return caret;
    }
    while(nm[kk]&&cw<cwmax){ fputc(nm[kk],stdout); cw++; kk++; }
    while(cw<cwmax){ fputc(' ',stdout); cw++; }
    printf("\x1b[0m");
    return -1;
}

/* Repaint just the naming row (modal repaint while typing) + the caret */
static void ed_nam_draw(Editor *e,int W,int H){
    int ew=ed_ex_width(e), exx=ed_ex_x(e,W), y, nc;
    if(!ew||e->namidx<0) return;
    y=e->namidx-(int)e->extop+4;
    if(y<2||y>H-1) return;
    printf("\x1b[%d;%dH%s",y,exx+1,EC_EXBG);
    nc=ed_ex_item_draw(e,(size_t)e->namidx,ew);
    if(nc>=0){
        e->namxcol=exx+nc+1; e->namyrow=y;
        printf("\x1b[%d;%dH\x1b[?25h",e->namyrow,e->namxcol);
    }
}

static void ed_draw(Editor *e){
    int W=80,H=24,r,CROWS=1,sync=0;
    int digits=1, ph, outn, ptop, exvw, exx, codex, scrollx, divx;
    size_t CW;
    HANDLE h=GetStdHandle(STD_OUTPUT_HANDLE);
    CONSOLE_SCREEN_BUFFER_INFO bi;
    if(GetConsoleScreenBufferInfo(h,&bi)){
        W=bi.srWindow.Right-bi.srWindow.Left+1;
        H=bi.srWindow.Bottom-bi.srWindow.Top+1;
        sync=1;   /* real console: wrap the frame so it paints atomically */
    }
    if(H<6) H=6; if(W<40) W=40;
    /* Keep both panes usable at any console size */
    if(e->exw<ED_EXW_MIN) e->exw=ED_EXW_MIN;
    { int maxex=W-24; if(maxex<ED_EXW_MIN) maxex=ED_EXW_MIN;
      if(e->exw>maxex) e->exw=maxex; }
    if(e->panel_h<ED_PANEL_MIN) e->panel_h=ED_PANEL_MIN;
    if(e->panel_h>ED_PANEL_MAX) e->panel_h=ED_PANEL_MAX;
    { int maxph=H-3; if(maxph<ED_PANEL_MIN) maxph=ED_PANEL_MIN;
      if(e->panel_h>maxph) e->panel_h=maxph; }
    if(!e->show_panel && e->focus==2) e->focus=0;
    if(!e->show_ex && e->focus==1) e->focus=0;
    ph=ed_panel_rows(e);
    CROWS=ed_crows(H,e);   /* code+explorer rows; title, panel title/input take the rest */
    { size_t t=e->n>0?e->n:1; while(t>=10){ digits++; t/=10; } if(digits<2) digits=2; }
    e->numw=digits+4;               /* " 12    " (space gap, no bar) */
    exvw=ed_ex_width(e);
    exx=ed_ex_x(e,W);
    codex=ed_code_x(e);
    scrollx=ed_scroll_x(e,W);
    divx=ed_div_x(e,W);
    CW=(size_t)ed_code_w(e,W);
    e->vw=W; e->vh=H;
    ed_clamp(e);
    ed_clamp_view(e,H);
    if(!e->view_detached){
        if(e->cy<e->top) e->top=e->cy;
        if(e->cy>=e->top+(size_t)CROWS) e->top=e->cy-(CROWS-1);
    }
    if(e->exn>0 && e->exsel>=e->exn) e->exsel=e->exn-1;
    if(e->exsel_ok&&e->exn>0){
        size_t erows=(size_t)(CROWS>2?CROWS-2:1);
        if(e->exsel<e->extop) e->extop=e->exsel;
        if(e->exsel>=e->extop+erows) e->extop=e->exsel-(erows-1);
    }
    if(e->naming&&e->namidx>=0){   /* keep the inline naming row on screen */
        int erows=CROWS>2?CROWS-2:1;
        if((size_t)e->namidx<e->extop) e->extop=(size_t)e->namidx;
        if((size_t)e->namidx>=e->extop+(size_t)erows)
            e->extop=(size_t)e->namidx-(size_t)erows+1;
    }
    { size_t dc=ed_dcol(e);
      int layoutkey=(e->show_panel?1:0)|(e->panel_h<<1);
      if(dc<e->dleft) e->dleft=dc;
      if(dc>=e->dleft+CW-1) e->dleft=dc-(CW-2);
      if(e->last_expand!=layoutkey){
          /* Layout height changed: wipe once so no stale rows survive */
          printf("\x1b[2J");
          e->last_expand=layoutkey;
      }
      printf("\x1b[?25l");
      if(sync) printf("\x1b[?2026h");
      printf("\x1b[H");
      {
        int ti, tx=0;
        ed_tabs_collect(e);
        printf("%s",EC_TOPBG);
        for(ti=0;ti<e->tab_n&&tx<W-2;ti++){
            const char *b1=strrchr(e->tab_path[ti],'/'), *b2=strrchr(e->tab_path[ti],'\\');
            const char *b=b1; char label[28]; int width, start;
            if(b2&&(!b||b2>b)) b=b2;
            snprintf(label,sizeof label,"%s%s",e->tab_path[ti][0]?(b?b+1:e->tab_path[ti]):"untitled",
                ed_path_dirty(e,e->tab_path[ti])?" *":"");
            width=(int)strlen(label)+2;
            if(width>24){ label[20]=0; strcat(label,"..."); width=(int)strlen(label)+2; }
            if(tx+width>W-2) break;
            start=tx;
            e->tab_x0[ti]=start; e->tab_x1[ti]=start+width;
            if(!strcmp(e->tab_path[ti],e->path)) printf("\x1b[48;2;45;45;45m\x1b[97m");
            else printf("\x1b[48;2;22;22;22m%s",TH_TDIM);
            printf(" %s ",label);
            tx+=width;
        }
        e->tab_n=ti;
        if(e->tab_n<64&&e->tab_n==0){
            e->tab_x0[0]=0; e->tab_x1[0]=8; e->tab_n=1;
            printf("\x1b[48;2;45;45;45m\x1b[97m lcode ");
        }
        printf("\x1b[K\x1b[0m\n");
      }
      /* Syntax colors are part of the luc plugin. */
      { int lang=L_NONE;
        int detected=ed_lang(e->path);
        if(detected==L_LUC){
            if(!ed_plug_luc_on(e)||ed_plug_setting(e,"luc","syntax",1)) lang=L_LUC;
        } else if(detected>L_LUC){
            if(ed_plug_luc_on(e)&&ed_plug_setting(e,"luc","syntax",1)) lang=detected;
        }
        /* Block state from line 0 to view */
        int pcm=-1, pst=-1;
        { size_t pre; for(pre=0;pre<e->top&&pre<e->n;pre++)
            ed_scan_state(e->ln[pre].b,e->ln[pre].len,&pcm,&pst,lang); }
        for(r=0;r<CROWS;r++){
          size_t li=e->top+r;
          printf("\x1b[%d;1H\x1b[K",r+2);
          /* Code pane (anchored independently so the sidebar can sit on either side) */
          printf("\x1b[%d;%dH",r+2,codex+1);
          ed_find_hl_begin(e,li);
          if(e->path[0] && li<e->n){
              char *lb=e->ln[li].b; size_t LL=e->ln[li].len, kk=0;
              int col=0;
              size_t dd=0;
              int iscur=(li==e->cy && e->focus==0);
              int dmark=ed_diag_has(e,li);
              printf(ed_hl_findbg?EC_SELBG:(iscur?EC_LINEBG:EC_CODEBG));
              if(dmark) printf("\x1b[91m%*d  ! \x1b[0m",digits,(int)li+1);
              else if(iscur||ed_hl_findbg) printf("%s%*d    ",TH_TCUR,digits,(int)li+1);
              else printf("%s%*d    ",TH_TDIM,digits,(int)li+1);
              printf(ed_hl_findbg?EC_SELBG:(iscur?EC_LINEBG:EC_CODEBG));
              while(kk<LL&&dd<e->dleft){ int w0=ed_chw((unsigned char)lb[kk],col); dd+=(size_t)w0; col+=w0; kk++; }
              while(kk<LL&&ed_is_cont((unsigned char)lb[kk])) kk++;
              ed_hl_bg=iscur?2:1;
              { int has_sel=0; size_t sel_s=0, sel_e=0;
                if(!ed_sel_empty(e)){
                    size_t sy,sx,ey,ex;
                    ed_sel_norm(e,&sy,&sx,&ey,&ex);
                    if(li>=sy&&li<=ey){
                        has_sel=1;
                        sel_s=(li==sy)?sx:0;
                        sel_e=(li==ey)?ex:(size_t)-1;
                    }
                }
                ed_hl_line(lb,LL,kk,(int)CW,lang,pcm<=-2?pcm:pcm+1,pst+1,
                           has_sel,sel_s,sel_e); }
              ed_hl_bg=0;
              ed_scan_state(lb,LL,&pcm,&pst,lang);
              printf(ed_hl_findbg?EC_SELBG:(iscur?EC_LINEBG:EC_CODEBG));
              printf("\x1b[K\x1b[0m");
          } else { printf("%s\x1b[K\x1b[0m",EC_CODEBG); }
          { int thumb_start=0,thumb_len=0;
            ed_scrollbar_geom(e,CROWS,&thumb_start,&thumb_len);
            printf("\x1b[%d;%dH",r+2,scrollx+1);
            if(r>=thumb_start&&r<thumb_start+thumb_len)
                printf("\x1b[97m\xe2\x94\x82\x1b[0m");
            else printf("%s\xe2\x94\x82\x1b[0m",TH_TDIM); }
          /* Explorer bar: header, root, then entries (left or right of code) */
          if(exvw>0){
              printf("\x1b[%d;%dH%s",r+2,exx+1,EC_EXBG);
              if(r==0){
                  int tw=(int)strlen(e->ex_title), kk;
                  if(tw>exvw-4) tw=exvw-4; if(tw<0) tw=0;
                  printf("%s%.*s",TH_TDIM,tw,e->ex_title);
                  for(kk=tw;kk<exvw-4;kk++) fputc(' ',stdout);
                  if(exvw>=4) printf("... ");
                  else for(;kk<exvw;kk++) fputc(' ',stdout);
                  printf("\x1b[0m");
              } else if(r==1){
                  const char *dd=e->exdir, *bb=dd;
                  { const char *p=dd; while(*p){ if(*p=='\\'||*p=='/') bb=p+1; p++; } }
                  if(!*bb) bb=dd;
                  printf(e->root_shut?"\xe2\x96\xb8":"\xe2\x96\xbe");
                  fputc(' ',stdout);
                  printf(e->root_shut?"\xf0\x9f\x93\x81":"\xf0\x9f\x93\x82");
                  fputc(' ',stdout);
                  { int cw=4, kk=0;
                    while(bb[kk]&&cw<exvw){ fputc(bb[kk],stdout); cw++; kk++; }
                    while(cw<exvw){ fputc(' ',stdout); cw++; } }
                  printf("\x1b[0m");
              } else {
                  size_t ei=e->extop+(size_t)(r-2);
                  if(ei<e->exn){
                      int nc=ed_ex_item_draw(e,ei,exvw);
                      if(nc>=0){ e->namxcol=exx+nc+1; e->namyrow=r+2; }
                  } else {
                      int kk; for(kk=0;kk<exvw;kk++) fputc(' ',stdout);
                      printf("\x1b[0m");
                  }
              }
              /* One-column divider: drag it to resize the bar */
              printf("\x1b[%d;%dH%s%s\xe2\x94\x82\x1b[0m",r+2,divx+1,EC_EXBG,TH_TDIM);
          }
      }
      /* Status snapshot: every message also lands in the terminal panel */
      if(e->status[0] && strcmp(e->status,e->lastlog)!=0){
          ed_log(e,"%s",e->status);
          snprintf(e->lastlog,sizeof e->lastlog,"%s",e->status);
      }
      /* Terminal panel: title, tail of the log, command input */
      if(ph>0){
        ptop=H-ph;
        printf("\x1b[%d;1H\x1b[K%s",ptop+1,EC_TOPBG);
        { int ptw=(int)strlen(e->panel_title); if(ptw>W-2) ptw=W-2;
          printf("%s %.*s",TH_TCUR,ptw,e->panel_title); }
        { int used=1+(int)strlen(e->panel_title); if(used>W-2) used=W-2;
          const char *hint="drag title to resize  |  ^` toggle";
          int hl=(int)strlen(hint), kk;
          if(used+hl<W-2){
              for(kk=used;kk<W-2-hl;kk++) fputc(' ',stdout);
              printf("%s%s",TH_TDIM,hint);
          } }
        printf("\x1b[K\x1b[0m");
        outn=ph-2;
        { int max=e->logn-outn; if(max<0) max=0;
          if(e->log_scroll>max) e->log_scroll=max;
          if(e->log_scroll<0) e->log_scroll=0; }
        for(r=0;r<outn;r++){
            int idx=e->logn-outn-e->log_scroll+r;
            printf("\x1b[%d;1H\x1b[K%s",ptop+2+r,EC_LOGBG);
            if(idx<0){
                if(r==0) printf("%s%s output, shell messages, and status appear here",TH_TDIM,e->panel_title);
            } else if(idx==e->logn-1) printf("%s%.*s",TH_TCUR,W-2,e->logs[idx]);
            else printf("%s%.*s",TH_TDIM,W-2,e->logs[idx]);
            printf("\x1b[0m");
        }
        { /* Command input row (click to focus, Enter runs, Ctrl+Up/Down history) */
          const char *dd=e->exdir, *bb=dd;
          char pr[64]; int pc;
          while(*bb){ if(*bb=='\\'||*bb=='/') dd=bb+1; bb++; }
          if(!*dd) dd=e->exdir;
          snprintf(pr,sizeof pr,"%.*s>",(int)sizeof pr-2,dd);
          printf("\x1b[%d;1H\x1b[K",H);
          printf("%s%s%s ",EC_TOPBG,TH_TCUR,pr);
          pc=(int)strlen(pr)+1;
          if(e->shlen) fwrite(e->shbuf,1,e->shlen,stdout);
          else {
              int u=ed_unsaved_count(e);
              if(u>0) printf("%s(%d unsaved)",TH_TDIM,u);
              else if(!ed_sel_empty(e)) printf("%s(sel ^C copies)",TH_TDIM);
              else printf("%s(type a command)",TH_TDIM);
          }
          printf("\x1b[K\x1b[0m");
          e->sh_promptw=(size_t)pc;
        }
      }
      /* Popup draws last, owns cursor */
      if(e->sug_on&&e->focus==0) ed_sug_draw(e,W,H);
      if(e->find_bar) ed_find_draw(e,W,H);
      if(e->menu) ed_popup_draw(e,W,H);
      else if(e->naming) ed_nam_draw(e,W,H);
      if(e->show_plugins) ed_plug_draw(e,W,H);
      if(e->show_settings) ed_set_draw(e,W,H);
      if(e->fnd_on) ed_fnd_draw(e,W,H);
      if(e->pf_on) ed_pfind_draw(e,W,H);
      if(!e->fnd_on&&!e->pf_on&&!e->show_settings&&!e->show_plugins&&!e->menu)
          ed_cmd_hint_draw(e,W,H);
      /* Cursor hidden with no file or open menu */
      { size_t dc2=ed_dcol(e);
        if(e->menu||e->show_plugins||e->show_settings) { /* hidden */ }
        else if(e->fnd_on){ /* the find panel positions its own caret */ }
        else if(e->find_bar){ if(e->find_cx>0) printf("\x1b[%d;%dH\x1b[?25h",e->find_cy,e->find_cx); }
        else if(e->pf_on) { /* pfind draw positions the caret */ }
        else if(e->naming) { /* positioned by ed_nam_draw */ }
        else if(e->focus==2&&ph>0)
            printf("\x1b[%d;%dH\x1b[?25h",H,(int)(e->sh_promptw+e->shcur+1));
        else if(e->focus==1&&e->exsel_ok&&exvw>0)
            printf("\x1b[%d;%dH\x1b[?25h",(int)(e->exsel-e->extop+4),exx+2);
        else if(e->path[0]&&e->cy>=e->top&&e->cy<e->top+(size_t)CROWS)
            printf("\x1b[%d;%dH\x1b[?25h",(int)(e->cy-e->top+2),(int)(codex+e->numw+dc2-e->dleft+1)); }
      if(sync) printf("\x1b[?2026l");
      fflush(stdout); } }
}

static void ex_free(Editor *e){
    size_t i;
    for(i=0;i<e->exn;i++){ free(e->ex[i].name); free(e->ex[i].full); }
    free(e->ex); e->ex=NULL; e->exn=0; e->excap=0;
    for(i=0;i<e->rsavn;i++) free(e->rsav[i]);
    free(e->rsav); e->rsav=NULL; e->rsavn=0; e->rsavcap=0;
}

static int ex_cmp(const void *a,const void *b){
    const ExItem *x=(const ExItem*)a, *y=(const ExItem*)b;
    if(x->isdir!=y->isdir) return y->isdir-x->isdir;
    return _stricmp(x->name,y->name);
}

static void ex_refresh(Editor *e){
    char pat[1088];
    WIN32_FIND_DATAA fd;
    HANDLE fh;
    ex_free(e);
    snprintf(pat,sizeof pat,"%s\\*",e->exdir[0]?e->exdir:".");
    fh=FindFirstFileA(pat,&fd);
    if(fh==INVALID_HANDLE_VALUE){ e->exsel=0; e->extop=0; e->exsel_ok=0; return; }
    do{
        if(!strcmp(fd.cFileName,".")||!strcmp(fd.cFileName,"..")) continue;
        if(e->exn==e->excap){ e->excap=e->excap?e->excap*2:32;
            e->ex=(ExItem*)lrealloc(e->ex,sizeof(ExItem)*e->excap); }
        e->ex[e->exn].name=(char*)lmalloc(strlen(fd.cFileName)+1);
        memcpy(e->ex[e->exn].name,fd.cFileName,strlen(fd.cFileName)+1);
        { char fb[1088]; snprintf(fb,sizeof fb,"%s\\%s",e->exdir[0]?e->exdir:".",fd.cFileName);
          e->ex[e->exn].full=(char*)lmalloc(strlen(fb)+1); memcpy(e->ex[e->exn].full,fb,strlen(fb)+1); }
        e->ex[e->exn].isdir=(fd.dwFileAttributes&FILE_ATTRIBUTE_DIRECTORY)!=0;
        /* Top-level entries sit inside the root header (VSCode-style), so depth starts at 1. */
        e->ex[e->exn].depth=1; e->ex[e->exn].expanded=0;
        e->exn++;
    }while(FindNextFileA(fh,&fd));
    FindClose(fh);
    qsort(e->ex,e->exn,sizeof(ExItem),ex_cmp);
    if(e->exsel>=e->exn && e->exn>0) e->exsel=e->exn-1;
    if(e->exn==0) e->exsel_ok=0;
}

/* Re-list the folder but keep expansions + selection (picks up outside changes) */
static void ex_expand(Editor *e,size_t idx);
static void ex_refresh_keep(Editor *e){
    char sel[1120]={0};
    char exp[128][1120]; int nexp=0;
    size_t i, j;
    int hadsel=e->exsel_ok;
    if(hadsel&&e->exsel<e->exn) snprintf(sel,sizeof sel,"%s",e->ex[e->exsel].full);
    for(i=0;i<e->exn && nexp<128;i++)
        if(e->ex[i].isdir&&e->ex[i].expanded)
            snprintf(exp[nexp++],sizeof exp[0],"%s",e->ex[i].full);
    ex_refresh(e);
    for(i=0;i<(size_t)nexp;i++)
        for(j=0;j<e->exn;j++)
            if(e->ex[j].isdir&&!e->ex[j].expanded&&!strcmp(e->ex[j].full,exp[i])){
                ex_expand(e,j); break;
            }
    e->exsel=0; e->exsel_ok=0;
    if(hadsel&&sel[0])
        for(j=0;j<e->exn;j++)
            if(!strcmp(e->ex[j].full,sel)){ e->exsel=j; e->exsel_ok=1; break; }
}

/* Join exdir/name, .. goes up */
static void ex_join(Editor *e,const char *name){
    if(!strcmp(name,"..")){
        char *d=e->exdir; size_t n=strlen(d), k;
        while(n>1 && (d[n-1]=='\\'||d[n-1]=='/')){ d[--n]=0; }
        if(n==2 && d[1]==':'){ ex_refresh(e); return; }
        for(k=n;k>0;k--) if(d[k-1]=='\\'||d[k-1]=='/') break;
        if(k==0){ snprintf(e->exdir,sizeof e->exdir,"."); }
        else if(k==1){ d[1]=0; }
        else d[k-1]=0;
    } else {
        size_t n=strlen(e->exdir);
        if(n+1+strlen(name)+1>=sizeof e->exdir) return;
        if(n>0 && e->exdir[n-1]!='\\' && e->exdir[n-1]!='/') e->exdir[n++]='\\';
        memcpy(e->exdir+n,name,strlen(name)+1);
    }
    e->exsel=0; e->extop=0; e->exsel_ok=0;
    ex_refresh(e);
}

static void ex_full(Editor *e,const char *name,char *out,size_t cap){
    size_t n=strlen(e->exdir);
    if(n+1+strlen(name)+1>cap){ out[0]=0; return; }
    memcpy(out,e->exdir,n);
    if(n>0 && out[n-1]!='\\' && out[n-1]!='/') out[n++]='\\';
    memcpy(out+n,name,strlen(name)+1);
}

/* Expand dir: insert sorted children after it */
static void ex_expand(Editor *e,size_t idx){
    char pat[1120];
    WIN32_FIND_DATAA fd;
    HANDLE fh;
    ExItem *tmp=NULL; size_t tcap=0, tn=0, i;
    int truncated=0;
    if(idx>=e->exn || !e->ex[idx].isdir || e->ex[idx].expanded) return;
    snprintf(pat,sizeof pat,"%s\\*",e->ex[idx].full);
    fh=FindFirstFileA(pat,&fd);
    if(fh==INVALID_HANDLE_VALUE) return;
    do{
        if(!strcmp(fd.cFileName,".")||!strcmp(fd.cFileName,"..")) continue;
        if(tn>=4096){ truncated=1; break; }
        if(tn==tcap){ tcap=tcap?tcap*2:64; tmp=(ExItem*)lrealloc(tmp,sizeof(ExItem)*tcap); }
        tmp[tn].name=(char*)lmalloc(strlen(fd.cFileName)+1);
        memcpy(tmp[tn].name,fd.cFileName,strlen(fd.cFileName)+1);
        { char fb[1120]; snprintf(fb,sizeof fb,"%s\\%s",e->ex[idx].full,fd.cFileName);
          tmp[tn].full=(char*)lmalloc(strlen(fb)+1); memcpy(tmp[tn].full,fb,strlen(fb)+1); }
        tmp[tn].isdir=(fd.dwFileAttributes&FILE_ATTRIBUTE_DIRECTORY)!=0;
        tmp[tn].depth=e->ex[idx].depth+1;
        tmp[tn].expanded=0;
        tn++;
    }while(FindNextFileA(fh,&fd));
    FindClose(fh);
    qsort(tmp,tn,sizeof(ExItem),ex_cmp);
    if(e->exn+tn>ED_EX_MAX){
        for(i=0;i<tn;i++){ free(tmp[i].name); free(tmp[i].full); }
        free(tmp);
        snprintf(e->status,sizeof e->status,"too many files (showing %d)",(int)e->exn);
        return;
    }
    if(e->exn+tn>e->excap){ while(e->excap<e->exn+tn) e->excap=e->excap?e->excap*2:32;
        e->ex=(ExItem*)lrealloc(e->ex,sizeof(ExItem)*e->excap); }
    memmove(&e->ex[idx+1+tn],&e->ex[idx+1],(e->exn-idx-1)*sizeof(ExItem));
    memcpy(&e->ex[idx+1],tmp,tn*sizeof(ExItem));
    free(tmp);
    e->exn+=tn;
    if(truncated){ e->ex[idx].expanded=0; snprintf(e->status,sizeof e->status,"folder truncated at %d items",(int)tn); }
    else e->ex[idx].expanded=1;
    e->root_shut=0;
}

static void ex_collapse(Editor *e,size_t idx){
    size_t j, d;
    if(idx>=e->exn || !e->ex[idx].isdir || !e->ex[idx].expanded) return;
    d=(size_t)e->ex[idx].depth;
    j=idx+1;
    while(j<e->exn && e->ex[j].depth>d){ free(e->ex[j].name); free(e->ex[j].full); j++; }
    if(j>idx+1) memmove(&e->ex[idx+1],&e->ex[j],(e->exn-j)*sizeof(ExItem));
    e->exn-=j-idx-1;
    e->ex[idx].expanded=0;
    if(e->exn>0 && e->exsel>=e->exn) e->exsel=e->exn-1;
}

/* Header toggle: collapse all or restore expansion */
static void ex_toggle_all(Editor *e){
    size_t i;
    if(!e->root_shut){
        for(i=0;i<e->rsavn;i++) free(e->rsav[i]);
        e->rsavn=0;
        for(i=0;i<e->exn;i++) if(e->ex[i].isdir&&e->ex[i].expanded){
            if(e->rsavn==e->rsavcap){ e->rsavcap=e->rsavcap?e->rsavcap*2:32;
                e->rsav=(char**)lrealloc(e->rsav,sizeof(char*)*e->rsavcap); }
            if(e->rsavn<256){ e->rsav[e->rsavn]=(char*)lmalloc(strlen(e->ex[i].full)+1);
                memcpy(e->rsav[e->rsavn],e->ex[i].full,strlen(e->ex[i].full)+1); e->rsavn++; }
        }
        i=0;
        while(i<e->exn){ if(e->ex[i].isdir&&e->ex[i].expanded) ex_collapse(e,i); else i++; }
        e->root_shut=1;
    } else {
        for(i=0;i<e->rsavn;i++){
            size_t j;
            for(j=0;j<e->exn;j++) if(e->ex[j].isdir&&!e->ex[j].expanded&&!strcmp(e->ex[j].full,e->rsav[i])){ ex_expand(e,j); break; }
        }
        e->root_shut=0;
    }
    if(e->exn>0 && e->exsel>=e->exn) e->exsel=e->exn-1;
}

/* Open entry: dir toggles, file loads */
static void ex_open_idx(Editor *e,size_t idx){
    if(idx>=e->exn) return;
    e->exsel=idx; e->exsel_ok=1;
    if(e->ex[idx].isdir){
        if(e->ex[idx].expanded) ex_collapse(e,idx); else ex_expand(e,idx);
        return;
    }
    ed_open_path(e,e->ex[idx].full);
}

/* Popup menu and file ops */
enum { MA_OPEN=1, MA_NEWFILE, MA_NEWDIR, MA_SAVEALL, MA_COPY, MA_RENAME, MA_PASTE,
       MA_DELETE, MA_DELYES, MA_DELNO, MA_COPY_PATH, MA_TEXT_COPY, MA_TEXT_PASTE,
       MA_TEXT_CUT };

/* Target dir: selected dir, file parent, or project root when nothing is selected */
static void ex_target_dir(Editor *e,char *out,size_t cap){
    if(e->exsel_ok && e->exsel<e->exn && e->ex[e->exsel].isdir){
        snprintf(out,cap,"%s",e->ex[e->exsel].full);
        return;
    }
    if(e->exsel_ok && e->exsel<e->exn && !e->ex[e->exsel].isdir){
        const char *f=e->ex[e->exsel].full, *b=NULL, *p;
        for(p=f;*p;p++) if(*p=='\\'||*p=='/') b=p;
        if(b){ size_t n=(size_t)(b-f);
            if(n>=cap) n=cap-1; memcpy(out,f,n); out[n]=0;
            if(!out[0]) snprintf(out,cap,".");
            return; }
    }
    snprintf(out,cap,"%s",e->exdir[0]?e->exdir:".");
}

static void ed_menu_add(Editor *e,const char *label,int id){
    if(e->nitems>=8) return;
    e->mitems[e->nitems].label=label;
    e->mitems[e->nitems].id=id;
    e->mitems[e->nitems].disabled=0;
    e->nitems++;
}
static void ed_menu_add_disabled(Editor *e,const char *label,int id,int disabled){
    ed_menu_add(e,label,id);
    if(e->nitems) e->mitems[e->nitems-1].disabled=disabled;
}

/* Kind 0 = root, 1 = file, 2 = code selection; ax/ay = mouse anchor.
   dir overrides the creation target (NULL = follow the current selection). */
static void ed_menu_open(Editor *e,int kind,int ax,int ay,const char *dir){
    e->nitems=0; e->msel=0; e->menu=1; e->naming=0;
    e->menutitle[0]=0;
    if(dir&&dir[0]) snprintf(e->menudir,sizeof e->menudir,"%s",dir);
    else ex_target_dir(e,e->menudir,sizeof e->menudir);
    if(ax>=0&&ay>=0){ e->menu_anch=1; e->menu_ax=ax; e->menu_ay=ay; }
    else { e->menu_anch=0; e->menu_ax=0; e->menu_ay=0; }
    if(kind==0){
        ed_menu_add(e,"new file",MA_NEWFILE);
        ed_menu_add(e,"new folder",MA_NEWDIR);
        if(e->fcb_ok) ed_menu_add(e,"paste here",MA_PASTE);
        ed_menu_add(e,"save all files",MA_SAVEALL);
        return;
    }
    if(kind==2){
        ed_menu_add_disabled(e,"copy",MA_TEXT_COPY,e->is_binary);
        if(!ed_sel_empty(e)){
            ed_menu_add_disabled(e,"paste",MA_TEXT_PASTE,!e->path[0]||e->is_binary||!ed_clip_has_text(e));
            ed_menu_add_disabled(e,"cut",MA_TEXT_CUT,e->is_binary);
        } else {
            ed_menu_add_disabled(e,"paste",MA_TEXT_PASTE,!e->path[0]||e->is_binary||!ed_clip_has_text(e));
        }
        return;
    }
    if(!e->exsel_ok||e->exsel>=e->exn){ e->menu=0; return; }
    /* No filename title: the bar shows just the 4 options */
    if(e->ex[e->exsel].isdir){
        ed_menu_add(e,"open",MA_OPEN);
        ed_menu_add(e,"new file",MA_NEWFILE);
        ed_menu_add(e,"new folder",MA_NEWDIR);
        ed_menu_add(e,"copy",MA_COPY);
        ed_menu_add(e,"rename",MA_RENAME);
        if(e->fcb_ok) ed_menu_add(e,"paste here",MA_PASTE);
        ed_menu_add(e,"delete",MA_DELETE);
    } else {
        ed_menu_add(e,"open",MA_OPEN);
        ed_menu_add(e,"copy",MA_COPY);
        ed_menu_add(e,"copy file location",MA_COPY_PATH);
        ed_menu_add(e,"rename",MA_RENAME);
        ed_menu_add(e,"delete",MA_DELETE);
    }
    if(e->nitems==0) e->menu=0;
}

/* Reveal path: expand parents, then select */
static void ex_reveal(Editor *e,const char *full){
    char dir[1024], comp[1024];
    size_t dl, pos;
    snprintf(dir,sizeof dir,"%s",full);
    { char *b=NULL, *p;
      for(p=dir;*p;p++) if(*p=='\\'||*p=='/') b=p;
      if(b) *b=0; else snprintf(dir,sizeof dir,"."); }
    ex_refresh(e);
    /* Walk parts under exdir */
    dl=strlen(e->exdir);
    if(strncmp(full,e->exdir,dl)!=0) return;
    pos=dl;
    while(full[pos]=='\\'||full[pos]=='/') pos++;
    while(full[pos]){
        size_t q=pos;
        while(full[q]&&full[q]!='\\'&&full[q]!='/') q++;
        { size_t n=q-pos; if(n>=sizeof comp) n=sizeof comp-1;
          memcpy(comp,full+pos,n); comp[n]=0; }
        { size_t j; int found=0;
          for(j=0;j<e->exn;j++) if(e->ex[j].isdir){
              const char *bn=e->ex[j].full+strlen(e->ex[j].full);
              while(bn>e->ex[j].full&&bn[-1]!='\\'&&bn[-1]!='/') bn--;
              if(!strcmp(bn,comp)){ ex_expand(e,j); found=1; break; } }
          if(!found) break; }
        pos=q;
        while(full[pos]=='\\'||full[pos]=='/') pos++;
    }
    { size_t j; int found=0;
      for(j=0;j<e->exn;j++) if(!strcmp(e->ex[j].full,full)){ e->exsel=j; found=1; break; }
      e->exsel_ok=found; }
    e->focus=1;
}

/* Recursive copy */
static int ed_copy_tree(const char *src,const char *dst,int depth){
    DWORD a=GetFileAttributesA(src);
    size_t i;
    if(a==INVALID_FILE_ATTRIBUTES) return 0;
    if(!(a&FILE_ATTRIBUTE_DIRECTORY)){
        return CopyFileA(src,dst,FALSE)!=0;
    }
    if(depth>32) return 0;
    if(!CreateDirectoryA(dst,NULL) && GetLastError()!=ERROR_ALREADY_EXISTS) return 0;
    { char pat[1120]; WIN32_FIND_DATAA fd; HANDLE fh;
      snprintf(pat,sizeof pat,"%s\\*",src);
      fh=FindFirstFileA(pat,&fd);
      if(fh==INVALID_HANDLE_VALUE) return 0;
      i=0;
      do{
          if(!strcmp(fd.cFileName,".")||!strcmp(fd.cFileName,"..")) continue;
          { char s2[1120], d2[1120];
            snprintf(s2,sizeof s2,"%s\\%s",src,fd.cFileName);
            snprintf(d2,sizeof d2,"%s\\%s",dst,fd.cFileName);
            if(!ed_copy_tree(s2,d2,depth+1)){ FindClose(fh); return 0; } }
          if(++i>2000){ FindClose(fh); return 0; }
      }while(FindNextFileA(fh,&fd));
      if(GetLastError()!=ERROR_NO_MORE_FILES){ FindClose(fh); return 0; }
      FindClose(fh); }
    return 1;
}

/* Unique sibling name */
static void ed_unique(char *out,size_t cap,const char *dir,const char *name,int isdir){
    char stem[256], ext[64]="", cand[512];
    const char *d=strrchr(name,'.');
    int k;
    if(!isdir && d && d!=name){ size_t n=(size_t)(d-name); if(n>255) n=255;
        memcpy(stem,name,n); stem[n]=0; snprintf(ext,sizeof ext,"%s",d); }
    else { snprintf(stem,sizeof stem,"%s",name); }
    for(k=0;k<1000;k++){
        if(k==0) snprintf(cand,sizeof cand,"%s - copy%s",stem,ext);
        else snprintf(cand,sizeof cand,"%s - copy (%d)%s",stem,k+1,ext);
        { char full[1120]; snprintf(full,sizeof full,"%s\\%s",dir,cand);
          if(GetFileAttributesA(full)==INVALID_FILE_ATTRIBUTES){
              snprintf(out,cap,"%s",cand); return; } }
    }
    snprintf(out,cap,"%s",name);
}

/* Delete is permanent: always confirm */

/* True if p is path or below it */
static int ed_under(const char *p,const char *full,int isdir){
    size_t n=strlen(full);
    if(!strcmp(p,full)) return 1;
    return isdir && !strncmp(p,full,n) && (p[n]=='\\'||p[n]=='/');
}

/* Delete file/tree; links unlinked, read-only cleared */
static int ed_delete_tree(const char *path,int depth){
    DWORD a=GetFileAttributesA(path);
    if(a==INVALID_FILE_ATTRIBUTES) return 0;
    if(a&FILE_ATTRIBUTE_DIRECTORY){
        if(!(a&FILE_ATTRIBUTE_REPARSE_POINT)){
            char pat[1120]; WIN32_FIND_DATAA fd; HANDLE fh;
            if(depth>32) return 0;
            snprintf(pat,sizeof pat,"%s\\*",path);
            fh=FindFirstFileA(pat,&fd);
            if(fh!=INVALID_HANDLE_VALUE){
                do{
                    char sub[1120];
                    if(!strcmp(fd.cFileName,".")||!strcmp(fd.cFileName,"..")) continue;
                    snprintf(sub,sizeof sub,"%s\\%s",path,fd.cFileName);
                    if(!ed_delete_tree(sub,depth+1)){ FindClose(fh); return 0; }
                }while(FindNextFileA(fh,&fd));
                FindClose(fh);
            }
        }
        SetFileAttributesA(path,FILE_ATTRIBUTE_DIRECTORY);
        return RemoveDirectoryA(path)!=0;
    }
    SetFileAttributesA(path,FILE_ATTRIBUTE_NORMAL);
    return DeleteFileA(path)!=0;
}

/* Drop buffers under deleted path */
static void ed_forget_path(Editor *e,const char *full,int isdir){
    size_t i, k;
    if(e->path[0] && ed_under(e->path,full,isdir)){
        for(k=0;k<e->n;k++) free(e->ln[k].b);
        free(e->ln); e->ln=NULL; e->n=0; e->cap=0;
        e->cx=0; e->cy=0; e->top=0; e->dleft=0; e->dirty=0; e->path[0]=0;
        free(e->saved); e->saved=NULL; e->saved_len=0;
        e->is_binary=0;
    }
    i=0;
    while(i<e->nstash){
        if(ed_under(e->stash[i].path,full,isdir)){
            for(k=0;k<e->stash[i].n;k++) free(e->stash[i].ln[k].b);
            free(e->stash[i].ln);
            free(e->stash[i].saved);
            memmove(&e->stash[i],&e->stash[i+1],(e->nstash-i-1)*sizeof(EdStash));
            e->nstash--;
        } else i++;
    }
}

/* Remove entry and children from tree */
static void ex_remove_by_path(Editor *e,const char *full){
    size_t i;
    for(i=0;i<e->exn;i++) if(!strcmp(e->ex[i].full,full)) break;
    if(i>=e->exn) return;
    if(e->ex[i].isdir&&e->ex[i].expanded) ex_collapse(e,i);
    free(e->ex[i].name); free(e->ex[i].full);
    memmove(&e->ex[i],&e->ex[i+1],(e->exn-i-1)*sizeof(ExItem));
    e->exn--;
    e->exsel=(i<e->exn)?i:(e->exn?e->exn-1:0);
    e->exsel_ok=e->exn>0;
}

/* Confirm delete; Cancel preselected for safety */
static void ed_delete_confirm_open(Editor *e){
    if(!e->exsel_ok||e->exsel>=e->exn) return;
    snprintf(e->deltarget,sizeof e->deltarget,"%s",e->ex[e->exsel].full);
    e->delisdir=e->ex[e->exsel].isdir;
    e->nitems=0; e->msel=1; e->menu=1; e->naming=0; e->menu_anch=0;
    if(e->delisdir) snprintf(e->menutitle,sizeof e->menutitle,"Delete folder '%s' and its contents?",e->ex[e->exsel].name);
    else snprintf(e->menutitle,sizeof e->menutitle,"Delete file '%s'?",e->ex[e->exsel].name);
    ed_menu_add(e,"Delete",MA_DELYES);
    ed_menu_add(e,"Cancel",MA_DELNO);
}

static void ed_delete_do(Editor *e){
    char name[256];
    const char *b=e->deltarget, *p, *l=NULL;
    for(p=b;*p;p++) if(*p=='\\'||*p=='/') l=p;
    snprintf(name,sizeof name,"%s",l?l+1:b);
    if(ed_delete_tree(e->deltarget,0)){
        ed_forget_path(e,e->deltarget,e->delisdir);
        ex_remove_by_path(e,e->deltarget);
        snprintf(e->status,sizeof e->status,"deleted '%s'",name);
    } else {
        if(e->delisdir) ex_refresh(e);   /* Partly deleted: resync tree */
        snprintf(e->status,sizeof e->status,"cannot delete '%s' (in use?)",name);
    }
    e->focus=1;
}

/* Insert the untitled placeholder row for a new file/folder (VSCode style).
   It sits right below the selected entry (first child when a folder) and is
   only materialized on disk when the name is confirmed. */
static void ed_nam_place(Editor *e,int isdir){
    size_t at; int dep; char full[1120];
    if(e->exsel_ok&&e->exsel<e->exn){
        if(e->ex[e->exsel].isdir){
            if(!e->ex[e->exsel].expanded) ex_expand(e,e->exsel);
            at=e->exsel+1; dep=e->ex[e->exsel].depth+1;
        } else { at=e->exsel+1; dep=e->ex[e->exsel].depth; }
    } else { at=0; dep=1; }
    if(at>e->exn) at=e->exn;
    if(e->exn>=ED_EX_MAX) return;   /* tree full: type blind, still works */
    snprintf(full,sizeof full,"%s\\untitled",e->namdir[0]?e->namdir:".");
    if(e->exn==e->excap){ e->excap=e->excap?e->excap*2:32;
        e->ex=(ExItem*)lrealloc(e->ex,sizeof(ExItem)*e->excap); }
    memmove(&e->ex[at+1],&e->ex[at],(e->exn-at)*sizeof(ExItem));
    e->ex[at].name=(char*)lmalloc(9); memcpy(e->ex[at].name,"untitled",9);
    e->ex[at].full=(char*)lmalloc(strlen(full)+1); memcpy(e->ex[at].full,full,strlen(full)+1);
    e->ex[at].isdir=isdir; e->ex[at].depth=dep; e->ex[at].expanded=0;
    e->exn++;
    e->namidx=(int)at;
}

static void ed_naming_start(Editor *e,int mode){
    e->naming=mode; e->namlen=0; e->namcur=0; e->namb[0]=0;
    e->namold[0]=0; e->namsel=0; e->namidx=-1;
    e->namoff=0; e->namtx0=0; e->namxcol=0; e->namyrow=0;
    e->namsel0=e->exsel; e->namtop0=e->extop; e->namok0=e->exsel_ok;
    if(mode==3){
        /* Edit the selected row in place (name preselected) */
        if(!e->exsel_ok||e->exsel>=e->exn){ e->naming=0; return; }
        { const char *f=e->ex[e->exsel].full, *b=NULL, *p;
          for(p=f;*p;p++) if(*p=='\\'||*p=='/') b=p;
          snprintf(e->namold,sizeof e->namold,"%s",f);
          if(b){ size_t n=(size_t)(b-f); if(n>=sizeof e->namdir) n=sizeof e->namdir-1;
              memcpy(e->namdir,f,n); e->namdir[n]=0; if(!e->namdir[0]) snprintf(e->namdir,sizeof e->namdir,"."); b++; }
          else { snprintf(e->namdir,sizeof e->namdir,"%s",e->exdir); b=f; }
          snprintf(e->namb,sizeof e->namb,"%s",b);
          e->namlen=strlen(e->namb); e->namcur=e->namlen; e->namsel=1; }
        e->namidx=(int)e->exsel;
    } else {
        if(!e->namdir[0]) ex_target_dir(e,e->namdir,sizeof e->namdir);
        ed_nam_place(e,mode==2);
    }
    e->menu=0;
}

static void ed_naming_cancel(Editor *e){
    if(e->naming&&e->naming!=3&&e->namidx>=0&&(size_t)e->namidx<e->exn){
        size_t at=(size_t)e->namidx;   /* drop the placeholder row */
        free(e->ex[at].name); free(e->ex[at].full);
        memmove(&e->ex[at],&e->ex[at+1],(e->exn-at-1)*sizeof(ExItem));
        e->exn--;
        e->exsel=e->namsel0; if(e->exsel>=e->exn) e->exsel=e->exn?e->exn-1:0;
        e->extop=e->namtop0; e->exsel_ok=e->namok0;
    }
    e->naming=0; e->namlen=0; e->namcur=0; e->namsel=0; e->namidx=-1;
}

static void ed_naming_confirm(Editor *e){
    size_t a=0, b=e->namlen;
    int mode=e->naming;
    char name[256], dest[1120];
    while(a<b && (e->namb[a]==' '||e->namb[a]=='\t')) a++;
    while(b>a && (e->namb[b-1]==' '||e->namb[b-1]=='\t')) b--;
    if(b-a==0 || b-a>=sizeof name){ snprintf(e->status,sizeof e->status,"bad name"); ed_naming_cancel(e); return; }
    memcpy(name,e->namb+a,b-a); name[b-a]=0;
    { size_t k; for(k=0;name[k];k++) if(name[k]=='/'||name[k]=='\\'){ snprintf(e->status,sizeof e->status,"bad name"); ed_naming_cancel(e); return; }
      if(!strcmp(name,".")||!strcmp(name,"..")){ snprintf(e->status,sizeof e->status,"bad name"); ed_naming_cancel(e); return; } }
    snprintf(dest,sizeof dest,"%s\\%s",e->namdir,name);
    if(mode==1){
        FILE *f=fopen(dest,"wb");
        if(!f){ snprintf(e->status,sizeof e->status,"cannot create '%s'",dest); ed_naming_cancel(e); return; }
        fclose(f);
        e->naming=0; e->namidx=-1;   /* the tree is rebuilt below */
        ex_reveal(e,dest);
        ed_open_path(e,dest);
        snprintf(e->status,sizeof e->status,"new file '%s'",name);
    } else if(mode==2){
        if(!CreateDirectoryA(dest,NULL)){ snprintf(e->status,sizeof e->status,"cannot create '%s'",dest); ed_naming_cancel(e); return; }
        e->naming=0; e->namidx=-1;
        ex_reveal(e,dest);
        snprintf(e->status,sizeof e->status,"new folder '%s'",name);
    } else {
        if(!strcmp(e->namold,dest)){ e->naming=0; e->namidx=-1; return; }
        if(GetFileAttributesA(dest)!=INVALID_FILE_ATTRIBUTES){ snprintf(e->status,sizeof e->status,"'%s' exists",name); return; }
        if(!MoveFileA(e->namold,dest)){ snprintf(e->status,sizeof e->status,"cannot rename"); ed_naming_cancel(e); return; }
        ed_rename_paths(e,e->namold,dest);
        e->naming=0; e->namidx=-1;
        ex_reveal(e,dest);
        snprintf(e->status,sizeof e->status,"renamed to '%s'",name);
    }
}

/* Click on the inline naming row: put the caret under the mouse */
static void ed_nam_click(Editor *e,int x){
    int want=x-e->namtx0;
    size_t base=e->namoff;
    e->namsel=0;
    if(want<=0){ e->namcur=base; return; }
    e->namcur=base+ed_byte_at(e->namb+base,e->namlen-base,(size_t)want);
    if(e->namcur>e->namlen) e->namcur=e->namlen;
}

/* Menu rows: top border, title, items, footer hint, bottom border (bh = nitems+4). */
static void ed_popup_geom(Editor *e,int W,int H,int *x0,int *y0,int *w,int *h){
    int bw, bh, i, maxw=0;
    { int tl=e->menutitle[0]?(int)strlen(e->menutitle):0;
      if(tl>maxw) maxw=tl; }
    for(i=0;i<e->nitems;i++){ int L=(int)strlen(e->mitems[i].label); if(L>maxw) maxw=L; }
    bw=maxw+4; if(bw>W-4) bw=W-4; if(bw<16) bw=16;
    bh=(e->menutitle[0]?1:0)+e->nitems; if(bh>H-1) bh=H-1; if(bh<1) bh=1;
    if(e->menu_anch){
        int ax=e->menu_ax, ay=e->menu_ay;
        if(ax<0) ax=0; if(ay<1) ay=1;
        *x0=ax; *y0=ay;
        if(*x0+bw>W) *x0=W-bw;
        if(*x0<0) *x0=0;
        /* keep above the status line */
        if(*y0+bh>H-1) *y0=(H-1)-bh;
        if(*y0<1) *y0=1;
    } else {
        *x0=(W-bw)/2; *y0=(H-bh)/2; if(*y0<1) *y0=1;
    }
    *w=bw; *h=bh;
}

static void ed_popup_draw(Editor *e,int W,int H){
    int x0,y0,bw,bh,i;
    ed_popup_geom(e,W,H,&x0,&y0,&bw,&bh);
    /* Borderless context menu: plain block, blue selection, title only for confirm */
    { int inner=bw, k, base=e->menutitle[0]?1:0, n;
      if(e->menutitle[0]){
          printf("\x1b[%d;%dH\x1b[48;2;28;28;28m",y0+1,x0+1);
          { int tl=(int)strlen(e->menutitle);
            if(tl>inner) tl=inner;
            printf("\x1b[90m%.*s",tl,e->menutitle);
            for(k=tl;k<inner;k++) fputc(' ',stdout); }
          printf("\x1b[0m");
      }
      n=e->nitems; if(n>bh-base) n=bh-base;
      for(i=0;i<n;i++){
          int yy=y0+base+i;
          printf("\x1b[%d;%dH",yy+1,x0+1);
          if(e->mitems[i].disabled) printf("\x1b[48;2;28;28;28m\x1b[90m");
          else if(i==e->msel) printf("\x1b[48;2;0;120;215m\x1b[97m");
          else printf("\x1b[48;2;28;28;28m\x1b[37m");
          fputs(i==e->msel?"\xe2\x96\xb8 ":"  ",stdout);
          { int k2, L=(int)strlen(e->mitems[i].label);
            int maxl=inner-2;
            for(k2=0;k2<L&&k2<maxl;k2++) fputc(e->mitems[i].label[k2],stdout);
            for(;k2<maxl;k2++) fputc(' ',stdout); }
          printf("\x1b[0m");
      }
    }
}

/* :plugin modal: borderless block, light-black header + up to 5 rows, scrollable */
#define ED_PLUGSHOW 5
static void ed_plug_geom(Editor *e,int W,int H,int *x0,int *y0,int *w,int *h){
    int bw=44, bh=1+ED_PLUGSHOW;
    int n=e->nplugs<ED_PLUGSHOW?e->nplugs:ED_PLUGSHOW;
    if(n<1) n=1;
    bh=1+n;
    if(bw>W-4) bw=W-4; if(bw<24) bw=24;
    if(bh>H-2) bh=H-2; if(bh<2) bh=2;
    *x0=(W-bw)/2; *y0=(H-bh)/2; if(*y0<1) *y0=1;
    *w=bw; *h=bh;
}
static void ed_plug_draw(Editor *e,int W,int H){
    int x0,y0,bw,bh,i,inner,n;
    ed_plug_geom(e,W,H,&x0,&y0,&bw,&bh);
    inner=bw;
    n=e->nplugs<ED_PLUGSHOW?e->nplugs:ED_PLUGSHOW;
    if(n<1) n=1;
    if(e->plug_sel>=e->plug_top+n) e->plug_top=e->plug_sel-n+1;
    if(e->plug_sel<e->plug_top) e->plug_top=e->plug_sel;
    /* header: light black */
    printf("\x1b[%d;%dH\x1b[48;2;45;45;45m\x1b[97m",y0+1,x0+1);
    { char hb[128];
      snprintf(hb,sizeof hb," plugins (%d)",e->nplugs);
      printf("%-*.*s",inner,inner,hb); }
    printf("\x1b[0m");
    for(i=0;i<n;i++){
        int yy=y0+1+i, pi=e->plug_top+i;
        Plug *p=&e->plugs[pi];
        printf("\x1b[%d;%dH",yy+1,x0+1);
        if(pi==e->plug_sel) printf("\x1b[48;2;0;120;215m\x1b[97m");
        else if(!p->enabled) printf("\x1b[48;2;36;36;36m\x1b[90m");
        else printf("\x1b[48;2;36;36;36m\x1b[97m");
        { char row[128];
          snprintf(row,sizeof row,"  %-.12s  %-.12s",p->name,p->author);
          printf("%-*.*s",inner,inner,row); }
        printf("\x1b[0m");
    }
}
static void ed_plug_click(Editor *e,int x,int y,int W,int H){
    int x0,y0,bw,bh,i,n;
    if(!e->show_plugins) return;
    ed_plug_geom(e,W,H,&x0,&y0,&bw,&bh);
    if(x<x0||x>=x0+bw||y<y0||y>=y0+bh){ e->show_plugins=0; return; }
    n=e->nplugs<ED_PLUGSHOW?e->nplugs:ED_PLUGSHOW;
    for(i=0;i<n;i++){
        int pi=e->plug_top+i;
        if(y==y0+1+i&&x>=x0&&x<x0+bw){
            e->plug_sel=pi;
            e->plugs[pi].enabled=!e->plugs[pi].enabled;
            ed_plug_write(e,pi);
            return;
        }
    }
}
/* :setting modal: Appearance / Keybinds / plugin tabs */
#define ED_SETW 18   /* left column width */
enum { SET_INFO=0, SET_TOGGLE, SET_VALUE, SET_TEXT, SET_ACTION };
/* Plugins without settings get no tab; map tab index <-> plugin index */
static int ed_set_plug_tab(Editor *e,int tab){
    int i,k=2;
    for(i=0;i<e->nplugs;i++){
        if(e->plugs[i].nset<=0) continue;
        if(k==tab) return i;
        k++;
    }
    return -1;
}
static int ed_set_ntabs(Editor *e){
    int i,n=0; for(i=0;i<e->nplugs;i++) if(e->plugs[i].nset>0) n++;
    return 2+n;
}
static int ed_set_nrows(Editor *e,int tab){
    if(tab==0) return 12;   /* appearance + autosave + explorer bar + terminal panel */
    if(tab==1) return KB_COUNT;
    { int pi=ed_set_plug_tab(e,tab); if(pi>=0) return e->plugs[pi].nset; }
    return 1;
}
static void ed_set_geom(Editor *e,int W,int H,int *x0,int *y0,int *w,int *h){
    int i, rows=0, bw;
    for(i=0;i<ed_set_ntabs(e);i++){
        int r=ed_set_nrows(e,i);
        if(r>rows) rows=r;
    }
    if(rows<3) rows=3;
    { int maxrows=H-5; if(maxrows<1) maxrows=1; if(rows>maxrows) rows=maxrows; }
    bw=W-4; if(bw>68) bw=68; if(bw<36) bw=36;
    if(bw>W-2) bw=W-2;
    { int bh=rows+4;
      *x0=(W-bw)/2; *y0=(H-bh)/2; if(*y0<0) *y0=0;
      *w=bw; *h=bh; }
}
static void ed_set_row_text(Editor *e,int tab,int row,char *label,size_t lcap,
                            char *val,size_t vcap,int *kind){
    *kind=SET_INFO;
    if(tab==0){
        if(row==0){ snprintf(label,lcap,"Tree guides"); snprintf(val,vcap,e->opt_guides?"on":"off"); *kind=SET_TOGGLE; }
        else if(row==1){ snprintf(label,lcap,"File icons"); snprintf(val,vcap,e->icons_nerd?"on":"off"); *kind=SET_TOGGLE; }
        else if(row==2){ snprintf(label,lcap,"Theme"); snprintf(val,vcap,"%s",ed_theme_cur); *kind=SET_VALUE; }
        else if(row==3){ snprintf(label,lcap,"Explorer bar"); snprintf(val,vcap,e->show_ex?"on":"off"); *kind=SET_TOGGLE; }
        else if(row==4){ snprintf(label,lcap,"Explorer name");
            snprintf(val,vcap,"%s",e->set_edit==1?e->set_buf:e->ex_title); *kind=SET_TEXT; }
        else if(row==5){ snprintf(label,lcap,"Explorer side"); snprintf(val,vcap,e->ex_side?"right":"left"); *kind=SET_VALUE; }
        else if(row==6){ snprintf(label,lcap,"Explorer width"); snprintf(val,vcap,"%d cols",e->exw); *kind=SET_VALUE; }
        else if(row==7){ snprintf(label,lcap,"Terminal panel"); snprintf(val,vcap,e->show_panel?"on":"off"); *kind=SET_TOGGLE; }
        else if(row==8){ snprintf(label,lcap,"Terminal name");
            snprintf(val,vcap,"%s",e->set_edit==2?e->set_buf:e->panel_title); *kind=SET_TEXT; }
        else if(row==9){ snprintf(label,lcap,"Terminal height"); snprintf(val,vcap,"%d rows",e->panel_h); *kind=SET_VALUE; }
        else if(row==10){ snprintf(label,lcap,"Terminal content"); snprintf(val,vcap,"%s",e->panel_content); }
        else if(row==11){ snprintf(label,lcap,"Auto-save (30s idle)"); snprintf(val,vcap,e->opt_autosave?"on":"off"); *kind=SET_TOGGLE; }
        return;
    }
    if(tab==1){
        if(row>=0&&row<KB_COUNT){
            char name[48];
            snprintf(label,lcap,"%s",ED_BIND_LABEL[row]);
            if(e->set_key_cap&&row==e->set_row){
                if(e->set_key_cap==1) snprintf(val,vcap,"press keys...");
                else {
                    ed_bind_name(&e->set_key_pending,name,sizeof name);
                    snprintf(val,vcap,"%s (Enter)",name);
                }
                *kind=SET_ACTION;
            } else {
                ed_bind_name(&e->binds[row],name,sizeof name);
                snprintf(val,vcap,"%s",name);
                *kind=SET_VALUE;
            }
        } else snprintf(label,lcap,"(no keybind)");
        return;
    }
    { int pi=ed_set_plug_tab(e,tab);
      if(pi>=0){ Plug *p=&e->plugs[pi];
        if(row<p->nset){
            snprintf(label,lcap,"%s",p->set_label[row]);
            snprintf(val,vcap,p->set_val[row]?"on":"off");
            *kind=SET_TOGGLE;
            return;
        } }
      snprintf(label,lcap,"(no settings)"); }
}
static void ed_set_edit_start(Editor *e){
    const char *src=NULL;
    if(e->set_tab!=0) return;
    if(e->set_row==4){ src=e->ex_title; e->set_edit=1; }
    else if(e->set_row==8){ src=e->panel_title; e->set_edit=2; }
    if(!src) return;
    snprintf(e->set_buf,sizeof e->set_buf,"%s",src);
    e->set_len=strlen(e->set_buf); e->set_cur=e->set_len;
    e->set_left=0;
}
static void ed_set_edit_commit(Editor *e){
    size_t a=0, b;
    if(!e->set_edit) return;
    b=e->set_len;
    while(a<b&&(e->set_buf[a]==' '||e->set_buf[a]=='\t')) a++;
    while(b>a&&(e->set_buf[b-1]==' '||e->set_buf[b-1]=='\t')) b--;
    if(b>a){
        char tmp[64]; size_t n=b-a;
        if(n>=sizeof tmp) n=sizeof tmp-1;
        memcpy(tmp,e->set_buf+a,n); tmp[n]=0;
        if(e->set_edit==1) snprintf(e->ex_title,sizeof e->ex_title,"%s",tmp);
        else snprintf(e->panel_title,sizeof e->panel_title,"%s",tmp);
        e->set_full=1;
    }
    e->set_edit=0;
    ed_cfg_save(e);
}
static int ed_set_edit_key(Editor *e,WORD vk,WCHAR ch){
    if(!e->set_edit) return 0;
    if(ch==27){ e->set_edit=0; return 1; }
    if(ch==13){ ed_set_edit_commit(e); return 1; }
    if(ch==8){
        if(e->set_cur>0){
            size_t from=e->set_cur;
            do e->set_cur--; while(e->set_cur>0&&ed_is_cont((unsigned char)e->set_buf[e->set_cur]));
            memmove(&e->set_buf[e->set_cur],&e->set_buf[from],e->set_len-from+1);
            e->set_len-=from-e->set_cur;
        }
        return 1;
    }
    if(ch==0){
        if(vk==VK_LEFT&&e->set_cur>0){
            do e->set_cur--; while(e->set_cur>0&&ed_is_cont((unsigned char)e->set_buf[e->set_cur]));
        } else if(vk==VK_RIGHT&&e->set_cur<e->set_len){
            do e->set_cur++; while(e->set_cur<e->set_len&&ed_is_cont((unsigned char)e->set_buf[e->set_cur]));
        } else if(vk==VK_HOME) e->set_cur=0;
        else if(vk==VK_END) e->set_cur=e->set_len;
        else if(vk==VK_DELETE&&e->set_cur<e->set_len){
            size_t k=e->set_cur;
            do k++; while(k<e->set_len&&ed_is_cont((unsigned char)e->set_buf[k]));
            memmove(&e->set_buf[e->set_cur],&e->set_buf[k],e->set_len-k);
            e->set_len-=k-e->set_cur;
            e->set_buf[e->set_len]=0;
        }
        return 1;
    }
    if(ch>=32){
        unsigned long cp=(unsigned long)ch;
        char tmp[4]; int m=0, k;
        if(cp<0x80) tmp[m++]=(char)cp;
        else if(cp<0x800){ tmp[m++]=(char)(0xC0|(cp>>6)); tmp[m++]=(char)(0x80|(cp&0x3F)); }
        else { tmp[m++]=(char)(0xE0|(cp>>12)); tmp[m++]=(char)(0x80|((cp>>6)&0x3F)); tmp[m++]=(char)(0x80|(cp&0x3F)); }
        if(e->set_len+(size_t)m>=sizeof e->set_buf) return 1;
        memmove(&e->set_buf[e->set_cur+m],&e->set_buf[e->set_cur],e->set_len-e->set_cur);
        for(k=0;k<m;k++) e->set_buf[e->set_cur+k]=tmp[k];
        e->set_cur+=(size_t)m; e->set_len+=(size_t)m; e->set_buf[e->set_len]=0;
        return 1;
    }
    return 1;
}
static void ed_set_key_capture(Editor *e,WORD vk,WCHAR ch,DWORD ctl){
    int ctrl=(ctl&(LEFT_CTRL_PRESSED|RIGHT_CTRL_PRESSED))!=0;
    int alt=(ctl&(LEFT_ALT_PRESSED|RIGHT_ALT_PRESSED))!=0;
    int shift=(ctl&SHIFT_PRESSED)!=0;
    if(!e->set_key_cap) return;
    if(ch==27||(ch==0&&vk==VK_ESCAPE)){ e->set_key_cap=0; return; }
    if(e->set_key_cap==2&&(ch==13||(ch==0&&vk==VK_RETURN))&&!ctrl&&!alt&&!shift){
        if(e->set_row>=0&&e->set_row<KB_COUNT){
            int j;
            for(j=0;j<KB_COUNT;j++){
                if(j!=e->set_row&&ed_bind_eq(&e->binds[j],&e->set_key_pending)){
                    e->binds[j]=e->binds[e->set_row];
                    break;
                }
            }
            e->binds[e->set_row]=e->set_key_pending;
            ed_cfg_save(e);
        }
        e->set_key_cap=0;
        return;
    }
    if(vk==0||vk==VK_SHIFT||vk==VK_CONTROL||vk==VK_MENU||vk==VK_LWIN||vk==VK_RWIN||
       vk==VK_CAPITAL||vk==VK_NUMLOCK||vk==VK_SCROLL) return;
    if(e->set_key_cap==1&&(vk==VK_RETURN)&&!ctrl&&!alt&&!shift){
        e->set_key_cap=0;
        return;
    }
    ed_bind_set(&e->set_key_pending,ctrl,alt,shift,vk);
    e->set_key_cap=2;
}
/* Activate the focused content row (toggle/cycle/edit + persist) */
static void ed_set_activate(Editor *e){
    int tab=e->set_tab, row=e->set_row;
    static const int widths[]={16,20,24,28,32,36,44,52,60};
    static const int heights[]={3,4,5,6,8,10,12,16};
    if(tab==0){
        if(row==0){ e->opt_guides=!e->opt_guides; ed_cfg_save(e); e->set_full=1; }
        else if(row==1){ e->icons_nerd=!e->icons_nerd; ed_cfg_save(e); e->set_full=1; }
        else if(row==2){ ed_theme_apply(strcmp(ed_theme_cur,"dark")?"dark":"light"); ed_cfg_save(e); e->set_full=1; }
        else if(row==3){ e->show_ex=!e->show_ex; if(!e->show_ex) e->focus=0; ed_cfg_save(e); e->set_full=1; }
        else if(row==4){ ed_set_edit_start(e); }
        else if(row==5){ e->ex_side=!e->ex_side; ed_cfg_save(e); e->set_full=1; }
        else if(row==6){
            size_t i;
            for(i=0;i<sizeof widths/sizeof widths[0];i++)
                if(widths[i]>e->exw){ e->exw=widths[i]; break; }
            if(i==sizeof widths/sizeof widths[0]) e->exw=widths[0];
            ed_cfg_save(e); e->set_full=1;
        }
        else if(row==7){ e->show_panel=!e->show_panel; if(!e->show_panel) e->focus=0; ed_cfg_save(e); e->set_full=1; }
        else if(row==8){ ed_set_edit_start(e); }
        else if(row==9){
            size_t i;
            for(i=0;i<sizeof heights/sizeof heights[0];i++)
                if(heights[i]>e->panel_h){ e->panel_h=heights[i]; break; }
            if(i==sizeof heights/sizeof heights[0]) e->panel_h=heights[0];
            ed_cfg_save(e); e->set_full=1;
        }
        else if(row==11){ e->opt_autosave=!e->opt_autosave; e->last_edit_tick=GetTickCount(); ed_cfg_save(e); e->set_full=1; }
        return;
    }
    if(tab==1){
        if(row>=0&&row<KB_COUNT){
            e->set_key_cap=1;
            e->set_key_pending=e->binds[row];
            e->set_left=0;
        }
        return;
    }
    { int pi=ed_set_plug_tab(e,tab);
      if(pi>=0){ Plug *p=&e->plugs[pi];
        if(row<p->nset){ p->set_val[row]=!p->set_val[row]; ed_plug_write(e,pi); e->set_full=1; } } }
}
static void ed_set_draw(Editor *e,int W,int H){
    int x0,y0,bw,bh,i,k,rows,ntabs,nrows,cw,cx0;
    if(!e->show_settings) return;
    ed_set_geom(e,W,H,&x0,&y0,&bw,&bh);
    rows=bh-4; if(rows<1) rows=1;
    ntabs=ed_set_ntabs(e);
    if(e->set_tab>=ntabs) e->set_tab=ntabs-1;
    if(e->set_tab<0) e->set_tab=0;
    nrows=ed_set_nrows(e,e->set_tab);
    if(e->set_row>=nrows) e->set_row=nrows-1;
    if(e->set_row<0) e->set_row=0;
    if(e->set_row_top>e->set_row) e->set_row_top=e->set_row;
    if(e->set_row>=e->set_row_top+rows) e->set_row_top=e->set_row-rows+1;
    if(e->set_row_top>nrows-rows) e->set_row_top=nrows-rows;
    if(e->set_row_top<0) e->set_row_top=0;
    if(e->set_tab<e->set_tab_top) e->set_tab_top=e->set_tab;
    if(e->set_tab>=e->set_tab_top+rows) e->set_tab_top=e->set_tab-rows+1;
    if(e->set_tab_top>ntabs-rows) e->set_tab_top=ntabs-rows;
    if(e->set_tab_top<0) e->set_tab_top=0;
    cw=bw-ED_SETW-3;
    cx0=x0+3+ED_SETW;   /* first content cell, 1-based */
    /* top border + header */
    printf("\x1b[%d;%dH%s\xe2\x94\x8c",y0+1,x0+1,TH_TDIM);
    for(k=0;k<bw-2;k++) fputs("\xe2\x94\x80",stdout);
    printf("\xe2\x94\x90\x1b[0m");
    printf("\x1b[%d;%dH\x1b[48;2;45;45;45m\x1b[97m\xe2\x94\x82",y0+2,x0+1);
    printf(" settings");
    for(k=9;k<bw-2;k++) fputc(' ',stdout);
    printf("\x1b[0m%s\xe2\x94\x82\x1b[0m",TH_TDIM);
    for(i=0;i<rows;i++){
        int yy=y0+3+i;
        int ti=e->set_tab_top+i;
        printf("\x1b[%d;%dH%s\xe2\x94\x82",yy,x0+1,TH_TDIM);
        if(ti<ntabs){
            const char *t="?";
            if(ti==0) t="Appearance";
            else if(ti==1) t="Keybinds";
            else { int pi=ed_set_plug_tab(e,ti); if(pi>=0) t=e->plugs[pi].name; }
            int cur=ti==e->set_tab;
            if(cur) printf(e->set_left?"\x1b[48;2;0;120;215m\x1b[97m"
                                      :"\x1b[48;2;0;70;120m\x1b[97m");
            else printf("\x1b[48;2;17;17;17m\x1b[97m");
            printf(" %-*.*s",ED_SETW-1,ED_SETW-1,t);
        } else {
            printf("\x1b[48;2;17;17;17m");
            for(k=0;k<ED_SETW;k++) fputc(' ',stdout);
        }
        printf("\x1b[48;2;30;30;30m%s\xe2\x94\x82",TH_TDIM);
        printf("\x1b[%d;%dH",yy,cx0);
        if(i<nrows){
            int ri=e->set_row_top+i;
            char label[96], val[96]; int kind, sel=0;
            if(ri>=nrows) ri=nrows-1;
            sel=(!e->set_left&&ri==e->set_row);
            ed_set_row_text(e,e->set_tab,ri,label,sizeof label,val,sizeof val,&kind);
            if(sel) printf("\x1b[48;2;0;120;215m\x1b[97m");
            else printf("\x1b[48;2;30;30;30m\x1b[97m");
            if(kind==SET_INFO){
                char line[208]; int L;
                snprintf(line,sizeof line,"%s",label);
                if(strcmp(label,"(no settings)")){
                    size_t n=strlen(line);
                    snprintf(line+n,sizeof line-n,": %s",val);
                }
                L=(int)strlen(line); if(L>cw-1) L=cw-1;
                fputc(' ',stdout);
                fwrite(line,1,(size_t)L,stdout);
                for(k=1+L;k<cw;k++) fputc(' ',stdout);
            } else {
                int fieldw, Ll=(int)strlen(label), pad, vlen=(int)strlen(val);
                int maxlab=cw-8; if(maxlab<1) maxlab=1;
                if(Ll>maxlab) Ll=maxlab;
                { int maxval=cw-1-Ll-4-1;
                  if(maxval<1) maxval=1;
                  if(vlen>maxval){ vlen=maxval; val[vlen]=0; } }
                fieldw=vlen+4;
                maxlab=cw-1-fieldw-1; if(maxlab<1) maxlab=1;
                if(Ll>maxlab) Ll=maxlab;
                pad=cw-1-Ll-fieldw; if(pad<0) pad=0;
                fputc(' ',stdout);
                fwrite(label,1,(size_t)Ll,stdout);
                for(k=0;k<pad;k++) fputc(' ',stdout);
                printf("[ ");
                if(kind==SET_TOGGLE&&!strcmp(val,"on")) printf("\x1b[92m");
                else if(kind==SET_VALUE) printf("\x1b[38;2;120;200;255m");
                else if(kind==SET_ACTION) printf("\x1b[96m");
                else if(!sel) printf("\x1b[90m");
                fwrite(val,1,strlen(val),stdout);
                if(sel) printf("\x1b[48;2;0;120;215m\x1b[97m");
                else printf("\x1b[48;2;30;30;30m\x1b[97m");
                printf(" ]");
                { int used=1+Ll+pad+fieldw; for(k=used;k<cw;k++) fputc(' ',stdout); }
                if(kind==SET_TEXT&&e->set_edit&&
                   ((e->set_tab==0&&ri==4&&e->set_edit==1)||
                    (e->set_tab==0&&ri==8&&e->set_edit==2))){
                    int fx, pw=0; size_t q; int cc=0;
                    fx=cx0+1+Ll+pad;   /* '[' cell */
                    for(q=0;q<e->set_cur&&e->set_buf[q];q++){
                        int ww=ed_chw((unsigned char)e->set_buf[q],cc);
                        pw+=ww; cc+=ww;
                    }
                    if(pw>vlen) pw=vlen;
                    e->set_cur_x=fx+2+pw;
                    e->set_cur_y=yy;
                }
            }
        } else {
            printf("\x1b[48;2;30;30;30m");
            for(k=0;k<cw;k++) fputc(' ',stdout);
        }
        printf("\x1b[%d;%dH%s\xe2\x94\x82\x1b[0m",yy,x0+bw,TH_TDIM);
    }
    /* footer + bottom border */
    printf("\x1b[%d;%dH%s\xe2\x94\x82\x1b[48;2;22;22;22m%s",y0+3+rows,x0+1,TH_TDIM,TH_TDIM);
    { const char *hint;
      if(e->set_tab==1&&e->set_key_cap==1) hint=" Press keys  Esc cancel";
      else if(e->set_tab==1&&e->set_key_cap==2) hint=" Enter confirm  Esc cancel";
      else if(e->set_tab==1) hint=" Enter binds a key  Esc close";
      else hint=bw>=52?" Tab switch  Enter change  Esc close":" Tab / Enter / Esc";
      int used=(int)strlen(hint), kk;
      if(used>bw-2) used=bw-2;
      fwrite(hint,1,(size_t)used,stdout);
      for(kk=used;kk<bw-2;kk++) fputc(' ',stdout); }
    printf("\x1b[%d;%dH%s\xe2\x94\x82\x1b[0m",y0+3+rows,x0+bw,TH_TDIM);
    printf("\x1b[%d;%dH%s\xe2\x94\x94",y0+4+rows,x0+1,TH_TDIM);
    for(k=0;k<bw-2;k++) fputs("\xe2\x94\x80",stdout);
    printf("\xe2\x94\x98\x1b[0m");
    if(e->set_edit&&e->set_cur_x>0)
        printf("\x1b[%d;%dH\x1b[?25h",e->set_cur_y,e->set_cur_x);
}
static void ed_set_click(Editor *e,int x,int y,int W,int H){
    int x0,y0,bw,bh,i,nrows,rows;
    if(!e->show_settings) return;
    if(e->set_key_cap) e->set_key_cap=0;
    ed_set_geom(e,W,H,&x0,&y0,&bw,&bh);
    if(x<x0||x>=x0+bw||y<y0||y>=y0+bh){ e->show_settings=0; return; }
    if(y<=y0+1) return;   /* top border + header */
    rows=bh-4; if(rows<1) rows=1;
    nrows=ed_set_nrows(e,e->set_tab);
    i=y-(y0+2);
    if(i<0||i>=rows) return;
    if(x>=x0+1&&x<x0+1+ED_SETW){
        int t=e->set_tab_top+i;
        if(t>=0&&t<ed_set_ntabs(e)){ e->set_tab=t; e->set_row=0; e->set_row_top=0; e->set_left=1; }
        return;
    }
    if(x>=x0+2+ED_SETW){
        int ri=e->set_row_top+i;
        if(ri>=0&&ri<nrows){
            e->set_row=ri; e->set_left=0;
            ed_set_activate(e);
        }
    }
}
/* Menu pick */
static void ed_menu_pick(Editor *e,int id){
    e->menu=0;
    if(id==MA_OPEN){ if(e->exsel_ok&&e->exsel<e->exn) ex_open_idx(e,e->exsel); }
    else if(id==MA_NEWFILE){ if(!e->menudir[0]) ex_target_dir(e,e->menudir,sizeof e->menudir); snprintf(e->namdir,sizeof e->namdir,"%s",e->menudir); ed_naming_start(e,1); }
    else if(id==MA_NEWDIR){ if(!e->menudir[0]) ex_target_dir(e,e->menudir,sizeof e->menudir); snprintf(e->namdir,sizeof e->namdir,"%s",e->menudir); ed_naming_start(e,2); }
    else if(id==MA_SAVEALL){ ed_save_all(e); }
    else if(id==MA_COPY){
        if(!e->exsel_ok||e->exsel>=e->exn) return;
        snprintf(e->fcb,sizeof e->fcb,"%s",e->ex[e->exsel].full);
        e->fcb_dir=e->ex[e->exsel].isdir; e->fcb_ok=1;
        snprintf(e->status,sizeof e->status,"copied '%s'",e->ex[e->exsel].name);
    }
    else if(id==MA_COPY_PATH){
        if(!e->exsel_ok||e->exsel>=e->exn) return;
        { const char *p=e->ex[e->exsel].full;
          free(e->clip); e->clip=(char*)lmalloc(strlen(p)+1);
          strcpy(e->clip,p); e->cliplen=strlen(p);
          ed_sys_copy(e->clip,e->cliplen);
          snprintf(e->status,sizeof e->status,"copied file location"); }
    }
    else if(id==MA_TEXT_COPY){
        if(e->is_binary) return;
        if(!ed_sel_empty(e)) ed_copy_sel(e);
        else ed_copy_line(e);
    }
    else if(id==MA_TEXT_CUT){
        if(!ed_sel_empty(e)&&!e->is_binary){
            ed_copy_sel(e); ed_undo_push(e,0); ed_sel_delete(e);
        }
    }
    else if(id==MA_TEXT_PASTE){
        size_t n=0; char *s=ed_sys_paste(&n);
        if(s){ ed_insert_bytes(e,s,n); free(s); }
        else if(e->clip) ed_insert_bytes(e,e->clip,e->cliplen);
        else snprintf(e->status,sizeof e->status,"clipboard empty");
    }
    else if(id==MA_RENAME){ if(e->exsel_ok) ed_naming_start(e,3); }
    else if(id==MA_DELETE){ if(e->exsel_ok) ed_delete_confirm_open(e); }
    else if(id==MA_DELYES){ ed_delete_do(e); }
    /* MA_DELNO: just closes */
    else if(id==MA_PASTE){
        char cand[512], dest[1120], base[256];
        const char *b;
        if(!e->fcb_ok) return;
        if(!e->menudir[0]) ex_target_dir(e,e->menudir,sizeof e->menudir);
        b=e->fcb; { const char *p=b, *l=NULL; while(*p){ if(*p=='\\'||*p=='/') l=p; p++; } if(l) b=l+1; }
        snprintf(base,sizeof base,"%s",b);
        ed_unique(cand,sizeof cand,e->menudir,base,e->fcb_dir);
        snprintf(dest,sizeof dest,"%s\\%s",e->menudir,cand);
        if(ed_copy_tree(e->fcb,dest,0)){ ex_reveal(e,dest); snprintf(e->status,sizeof e->status,"pasted '%s'",cand); }
        else snprintf(e->status,sizeof e->status,"paste failed");
    }
}

/* Popup input: 1 when consumed */
static int ed_popup_key(Editor *e,WORD vk,WCHAR ch){
    int i;
    if(e->naming){
        if(ch==27){ ed_naming_cancel(e); return 1; }
        if(ch==13){ ed_naming_confirm(e); return 1; }
        if(ch==8){
            if(e->namsel){ e->namlen=0; e->namcur=0; e->namb[0]=0; e->namsel=0; }
            else if(e->namcur>0){
                size_t from=e->namcur;
                do e->namcur--; while(e->namcur>0&&ed_is_cont((unsigned char)e->namb[e->namcur]));
                memmove(&e->namb[e->namcur],&e->namb[from],e->namlen-from+1);
                e->namlen-=from-e->namcur;
            }
            return 1;
        }
        if(ch==0){
            if(vk==VK_LEFT){ e->namsel=0; if(e->namcur>0){ do e->namcur--; while(e->namcur>0&&ed_is_cont((unsigned char)e->namb[e->namcur])); } return 1; }
            if(vk==VK_RIGHT){ if(e->namsel){ e->namcur=e->namlen; e->namsel=0; } else if(e->namcur<e->namlen){ do e->namcur++; while(e->namcur<e->namlen&&ed_is_cont((unsigned char)e->namb[e->namcur])); } return 1; }
            if(vk==VK_HOME){ e->namcur=0; e->namsel=0; return 1; }
            if(vk==VK_END){ e->namcur=e->namlen; e->namsel=0; return 1; }
            if(vk==VK_DELETE){
                if(e->namsel){ e->namlen=0; e->namcur=0; e->namb[0]=0; e->namsel=0; }
                else if(e->namcur<e->namlen){
                    size_t k=e->namcur;
                    do k++; while(k<e->namlen&&ed_is_cont((unsigned char)e->namb[k]));
                    memmove(&e->namb[e->namcur],&e->namb[k],e->namlen-k);
                    e->namlen-=k-e->namcur; e->namb[e->namlen]=0;
                }
                return 1;
            }
            return 1;
        }
        if(ch>=32){
            /* BMP to UTF-8; astral via surrogates */
            unsigned long cp=ch;
            char tmp[4]; int m=0, k;
            if(e->namsel){ e->namlen=0; e->namcur=0; e->namb[0]=0; e->namsel=0; }
            if(cp<0x80) tmp[m++]=(char)cp;
            else if(cp<0x800){ tmp[m++]=(char)(0xC0|(cp>>6)); tmp[m++]=(char)(0x80|(cp&0x3F)); }
            else { tmp[m++]=(char)(0xE0|(cp>>12)); tmp[m++]=(char)(0x80|((cp>>6)&0x3F)); tmp[m++]=(char)(0x80|(cp&0x3F)); }
            if(e->namlen+(size_t)m>=sizeof e->namb) return 1;
            memmove(&e->namb[e->namcur+m],&e->namb[e->namcur],e->namlen-e->namcur);
            for(k=0;k<m;k++) e->namb[e->namcur+k]=tmp[k];
            e->namcur+=(size_t)m; e->namlen+=(size_t)m; e->namb[e->namlen]=0;
            return 1;
        }
        return 1;
    }
    if(e->menu){
        if(ch==27){ e->menu=0; return 1; }
        if(ch==13){
            if(e->msel<e->nitems&&!e->mitems[e->msel].disabled)
                ed_menu_pick(e,e->mitems[e->msel].id);
            else e->menu=0;
            return 1;
        }
        if(ch==0){
            if(vk==VK_UP&&e->msel>0){ e->msel--; return 1; }
            if(vk==VK_DOWN&&e->msel+1<e->nitems){ e->msel++; return 1; }
            return 1;
        }
        return 1;
    }
    return 0;
}

static void ed_popup_click(Editor *e,int x,int y,int W,int H){
    int x0,y0,bw,bh,i;
    if(!e->menu) return;
    ed_popup_geom(e,W,H,&x0,&y0,&bw,&bh);
    /* click outside -> dismiss (no action, like Cancel) */
    if(x<x0||x>=x0+bw||y<y0||y>=y0+bh){ e->menu=0; return; }
    /* items start at y0 (+1 when the confirm title is shown) */
    for(i=0;i<e->nitems;i++){
        int yy=y0+(e->menutitle[0]?1:0)+i;
        if(y==yy && x>=x0 && x<x0+bw){
            e->msel=i;
            if(!e->mitems[i].disabled) ed_menu_pick(e,e->mitems[i].id);
            return;
        }
    }
}

/* Wait for key after run */
/* Bottom shell: powershell when present, else COMSPEC; output goes to the log panel */
static void ed_console_raw(void){
    /* Child shells clobber console mode on exit (mouse dies, keys live).
       Re-assert editor mode from whatever is currently set. */
    HANDLE hin=GetStdHandle(STD_INPUT_HANDLE);
    DWORD m=0;
    if(!hin||hin==INVALID_HANDLE_VALUE||!GetConsoleMode(hin,&m)) return;
    SetConsoleMode(hin,(m|ENABLE_MOUSE_INPUT|ENABLE_EXTENDED_FLAGS)
        & ~(DWORD)(ENABLE_LINE_INPUT|ENABLE_ECHO_INPUT|ENABLE_PROCESSED_INPUT|ENABLE_QUICK_EDIT_MODE));
}
static int ed_shell_has_ps(void){
    static int st=-1;
    if(st<0){
        char p[1024];
        st=(SearchPathA(NULL,"powershell.exe",NULL,sizeof p,p,NULL)>0)?1:0;
    }
    return st;
}
static void ed_shell_hist_push(Editor *e,const char *line){
    if(e->shn>=ED_SHHIST){
        memmove(e->shhist[0],e->shhist[1],sizeof e->shhist[0]*(ED_SHHIST-1));
        e->shn=ED_SHHIST-1;
    }
    snprintf(e->shhist[e->shn++],sizeof e->shhist[0],"%s",line);
    e->shpos=e->shn;
}
static void ed_shell_hist_move(Editor *e,int dir){
    if(dir<0){
        if(e->shn<=0) return;
        if(e->shpos>=e->shn) snprintf(e->shdraft,sizeof e->shdraft,"%s",e->shbuf);
        if(e->shpos>0) e->shpos--;
        snprintf(e->shbuf,sizeof e->shbuf,"%s",e->shhist[e->shpos]);
    } else if(e->shpos<e->shn){
        e->shpos++;
        if(e->shpos<e->shn) snprintf(e->shbuf,sizeof e->shbuf,"%s",e->shhist[e->shpos]);
        else snprintf(e->shbuf,sizeof e->shbuf,"%s",e->shdraft);
    }
    e->shlen=strlen(e->shbuf);
    e->shcur=e->shlen;
}
/* Strip ANSI escapes + control junk so child output can't wreck the TUI */
static void ed_sanitize(char *d,size_t cap,const char *s){
    size_t r=0, w=0;
    if(cap==0) return;
    while(s[r]&&w+1<cap){
        unsigned char c=(unsigned char)s[r];
        if(c==0x1B){
            r++;
            if(s[r]=='['){   /* CSI ... final byte */
                r++;
                while(s[r]&&!((s[r]>='@'&&s[r]<='~'))) r++;
                if(s[r]) r++;
            } else if(s[r]==']'){   /* OSC ... BEL or ST */
                r++;
                while(s[r]&&s[r]!='\x07'&&!(s[r]=='\\'&&r>0&&s[r-1]==0x1B)) r++;
                if(s[r]=='\x07') r++;
                else if(s[r]) r++;
            } else if(s[r]) r++;
            continue;
        }
        if(c=='\t'){ if(w+4<cap){ d[w++]=' '; d[w++]=' '; d[w++]=' '; d[w++]=' '; } r++; continue; }
        if(c<32||c==127){ d[w++]='?'; r++; continue; }
        d[w++]=s[r++];
    }
    d[w]=0;
}
/* :commands run inside the shell instead of a subprocess */
static void ed_run_current(Editor *e){
    char exe[1024], cmd[2300], buf[1024], clean[600], back[1024];
    FILE *p;
    int nlines=0, rc=0, errs;
    DWORD n;
    if(!e->path[0]){ snprintf(e->status,sizeof e->status,"no file open"); return; }
    if(e->is_binary){ snprintf(e->status,sizeof e->status,"cannot run a binary file"); return; }
    if(e->dirty&&!ed_save(e)) return;
    ed_diag_clear(e);
    n=GetModuleFileNameA(NULL,exe,sizeof exe);
    if(n==0||n>=sizeof exe){ snprintf(e->status,sizeof e->status,"cannot locate luc executable"); return; }
    e->log_expand=1; e->log_scroll=0; e->show_panel=1;
    ed_log(e,"$ luc %s",e->path);
    snprintf(cmd,sizeof cmd,"\"\"%s\" \"%s\" 2>&1 <NUL\"",exe,e->path);
    GetCurrentDirectoryA(sizeof back,back);
    if(e->exdir[0]) SetCurrentDirectoryA(e->exdir);
    p=_popen(cmd,"r");
    SetCurrentDirectoryA(back);
    if(!p){ ed_log(e,"run: cannot start"); snprintf(e->status,sizeof e->status,"run: cannot start"); return; }
    while(fgets(buf,sizeof buf,p)){
        size_t bn=strlen(buf);
        while(bn>0&&(buf[bn-1]=='\n'||buf[bn-1]=='\r')) buf[--bn]=0;
        ed_sanitize(clean,sizeof clean,buf);
        if(nlines<200) ed_log(e,"%s",clean);
        ed_diag_parse(e,clean);
        nlines++;
    }
    if(nlines>=200) ed_log(e,"... truncated");
    rc=_pclose(p);
    if(rc) ed_log(e,"[exit %d]",rc);
    ed_console_raw();
    ex_refresh_keep(e);
    errs=e->diag_n;
    e->diag_sel=errs?0:-1;
    e->focus=0;
    snprintf(e->status,sizeof e->status,"run: %d line(s), %d error(s)%s",
        nlines,errs,rc?" (failed)":"");
}

static void ed_cmd_run(Editor *e,const char *cmd){
    while(*cmd==' '||*cmd=='\t') cmd++;
    if(!strncmp(cmd,"replace",7)&&(cmd[7]==0||cmd[7]==' '||cmd[7]=='\t')){
        const char *a=cmd+7,*sep; char query[256],repl[256]; size_t qn,rn;
        while(*a==' '||*a=='\t') a++;
        sep=strstr(a,"=>");
        if(!sep){ snprintf(e->status,sizeof e->status,"usage: :replace search text => replacement text"); return; }
        qn=(size_t)(sep-a); rn=strlen(sep+2);
        while(qn>0&&(a[qn-1]==' '||a[qn-1]=='\t')) qn--;
        sep+=2; while(*sep==' '||*sep=='\t') sep++; rn=strlen(sep);
        while(rn>0&&(sep[rn-1]==' '||sep[rn-1]=='\t')) rn--;
        if(!qn||qn>=sizeof query||rn>=sizeof repl){
            snprintf(e->status,sizeof e->status,"replace query must fit 255 bytes; replacement may be empty");
            return;
        }
        memcpy(query,a,qn); query[qn]=0;
        memcpy(repl,sep,rn); repl[rn]=0;
        ed_fnd_open(e,2,query);
        snprintf(e->fnd_repl,sizeof e->fnd_repl,"%s",repl);
        e->fnd_replace_valid=1;
        return;
    }
    if(!strncmp(cmd,"git",3)&&(cmd[3]==0||cmd[3]==' '||cmd[3]=='\t')){
        const char *a=cmd+3,*message=NULL; char action[32]="status";
        while(*a==' '||*a=='\t') a++;
        if(*a){
            size_t n=0;
            while(a[n]&&a[n]!=' '&&a[n]!='\t'&&n+1<sizeof action){ action[n]=a[n]; n++; }
            action[n]=0; a+=n; while(*a==' '||*a=='\t') a++;
            if(!strcmp(action,"commit")) message=a;
            else if(*a){ snprintf(e->status,sizeof e->status,"usage: :git [status|diff|stage|commit <message>]"); return; }
            if(strcmp(action,"status")&&strcmp(action,"diff")&&
               strcmp(action,"stage")&&strcmp(action,"commit")){
                snprintf(e->status,sizeof e->status,"usage: :git [status|diff|stage|commit <message>]"); return;
            }
        }
        ed_git_run(e,action,message);
        return;
    }
    if(!strcmp(cmd,"p")||!strcmp(cmd,"plugin")||!strcmp(cmd,"plugins")){
        ed_plug_scan(e);
        e->show_plugins=1; e->plug_sel=0;
        return;
    }
    if(!strcmp(cmd,"s")||!strcmp(cmd,"setting")||!strcmp(cmd,"settings")){
        ed_plug_scan(e);
        e->show_settings=1; e->set_tab=0; e->set_row=0; e->set_left=1;
        e->set_tab_top=0; e->set_row_top=0; e->set_edit=0; e->set_key_cap=0;
        return;
    }
    if(!strcmp(cmd,"keybind")||!strcmp(cmd,"keybinds")){
        ed_plug_scan(e);
        e->show_settings=1; e->set_tab=1; e->set_row=0; e->set_left=1;
        e->set_tab_top=0; e->set_row_top=0; e->set_edit=0; e->set_key_cap=0;
        return;
    }
    if(!strncmp(cmd,"findfile",8)&&(cmd[8]==0||cmd[8]==' '||cmd[8]=='\t')){
        const char *a=cmd+8;
        while(*a==' '||*a=='\t') a++;
        ed_fnd_open(e,1,a);
        return;
    }
    if(!strncmp(cmd,"filefind",8)&&(cmd[8]==0||cmd[8]==' '||cmd[8]=='\t')){
        const char *a=cmd+8;
        while(*a==' '||*a=='\t') a++;
        ed_fnd_open(e,1,a);
        return;
    }
    if(!strncmp(cmd,"ff",2)&&(cmd[2]==0||cmd[2]==' '||cmd[2]=='\t')){
        const char *a=cmd+2;
        while(*a==' '||*a=='\t') a++;
        ed_fnd_open(e,1,a);
        return;
    }
    if(!strncmp(cmd,"find",4)&&(cmd[4]==0||cmd[4]==' '||cmd[4]=='\t')){
        const char *a=cmd+4;
        while(*a==' '||*a=='\t') a++;
        if(*a){
            ed_find_all(e,a);
            snprintf(e->status,sizeof e->status,"%d found for '%s'",e->find_n,e->find_buf);
            ed_log(e,"%d found for '%s'",e->find_n,e->find_buf);
        } else ed_find_open(e);
        return;
    }
    if(!strncmp(cmd,"f",1)&&(cmd[1]==0||cmd[1]==' '||cmd[1]=='\t')){
        const char *a=cmd+1;
        while(*a==' '||*a=='\t') a++;
        if(*a){
            ed_find_all(e,a);
            snprintf(e->status,sizeof e->status,"%d found for '%s'",e->find_n,e->find_buf);
            ed_log(e,"%d found for '%s'",e->find_n,e->find_buf);
        } else ed_find_open(e);
        return;
    }
    if(!strcmp(cmd,"pf")||!strcmp(cmd,"pfind")||!strcmp(cmd,"psearch")){
        ed_pfind_open(e);
        return;
    }
    if(!strcmp(cmd,"run")){
        ed_run_current(e);
        return;
    }
    if(!strcmp(cmd,"help")||!strcmp(cmd,"?")){
        ed_log(e,":plugin - plugin list (Enter toggles)");
        ed_log(e,":setting - settings (Appearance, Keybinds, plugins)");
        ed_log(e,":keybind - keybind settings");
        ed_log(e,":theme [dark|light] - editor colors");
        ed_log(e,":find [text] - find in the open file (Ctrl+F)");
        ed_log(e,":ff [text] - find files by name");
        ed_log(e,":pf / :pfind - search all project files (Ctrl+Shift+F)");
        ed_log(e,":replace old => new - preview and replace throughout project (F4 twice)");
        ed_log(e,":git [status|diff|stage|commit <message>] - basic Git actions");
        ed_log(e,":run - run the current file (F5)");
        ed_log(e,"Ctrl+Tab / Ctrl+Shift+Tab - switch open file tabs");
        ed_log(e,"Ctrl+X/C/V - cut, copy, paste selected code");
        ed_log(e,"F6 - jump between live structural diagnostics");
        ed_log(e,"Ctrl+Up/Down - command history; Down restores your current input");
        ed_log(e,":help - this list");
        e->log_expand=1; e->log_scroll=0;
        return;
    }
    if(!strncmp(cmd,"theme",5)&&(cmd[5]==0||cmd[5]==' '||cmd[5]=='\t')){
        const char *a=cmd+5;
        while(*a==' '||*a=='\t') a++;
        if(!*a){ ed_log(e,"theme: %s (dark|light)",ed_theme_cur); return; }
        if(ed_theme_apply(a)){ ed_cfg_save(e); ed_log(e,"theme: %s",ed_theme_cur); }
        else ed_log(e,"theme: unknown '%s' (dark|light)",a);
        return;
    }
    ed_log(e,"unknown command ':%s' (try :help)",cmd);
}
static void ed_shell_run(Editor *e){
    char line[1024], cmd[1280], buf[512];
    FILE *p;
    int nlines=0, rc=0;
    size_t a=0, b;
    if(!e->shlen) return;
    memcpy(line,e->shbuf,e->shlen); line[e->shlen]=0;
    /* exit/quit leaves the editor (anything else runs as a shell command) */
    while(line[a]==' '||line[a]=='\t') a++;
    b=strlen(line);
    while(b>a&&(line[b-1]==' '||line[b-1]=='\t'||line[b-1]=='\r')) b--;
    line[b]=0;
    if(!strcmp(line+a,"exit")||!strcmp(line+a,"quit")||
       !strcmp(line+a,"EXIT")||!strcmp(line+a,"QUIT")){
        e->sh_quit=1; return;
    }
    ed_shell_hist_push(e,line);
    if(line[a]==':'){
        ed_cmd_run(e,line+a+1);
        e->shlen=0; e->shcur=0; e->shbuf[0]=0;
        e->shpos=e->shn; e->status[0]=0;
        return;
    }
    ed_log(e,"$ %s",line);
    e->log_expand=1; e->log_scroll=0;   /* open the 3-row view for the output */
    ed_console_raw();   /* heal mode in case it was already clobbered */
    /* <NUL stdin: interactive programs get EOF instead of stealing keys */
    if(ed_shell_has_ps()) snprintf(cmd,sizeof cmd,"powershell -NoProfile -NonInteractive -Command %s 2>&1 <NUL",line);
    else snprintf(cmd,sizeof cmd,"cmd.exe /C %s 2>&1 <NUL",line);
    /* Run inside the open folder so relative paths (luc core.luc) work */
    { char back[1024];
      GetCurrentDirectoryA(sizeof back,back);
      if(e->exdir[0]) SetCurrentDirectoryA(e->exdir);
      p=_popen(cmd,"r");
      SetCurrentDirectoryA(back);
    }
    if(!p){ ed_log(e,"shell: cannot start"); }
    else {
        char clean[256];
        while(fgets(buf,sizeof buf,p)){
            size_t n=strlen(buf);
            while(n>0&&(buf[n-1]=='\n'||buf[n-1]=='\r')) buf[--n]=0;
            ed_sanitize(clean,sizeof clean,buf);
            if(nlines<200) ed_log(e,"%s",clean);
            nlines++;
        }
        if(nlines>=200) ed_log(e,"... truncated");
        rc=_pclose(p);
        if(rc) ed_log(e,"[exit %d]",rc);
    }
    ed_console_raw();   /* child shells leave cooked mode behind: fix it */
    ex_refresh_keep(e);   /* the command may have created/deleted files */
    e->shlen=0; e->shcur=0; e->status[0]=0;
}
/* Shell line keys; returns 1 when the key was consumed */
static int ed_shell_key(Editor *e,WORD vk,WCHAR ch,DWORD ctl){
    char hints[16][32];
    int ctrl=(ctl&(LEFT_CTRL_PRESSED|RIGHT_CTRL_PRESSED))!=0;
    int nh=ed_cmd_hint_list(e,hints,&e->cmdh_sel);
    if(ch==0&&ctrl&&(vk==VK_UP||vk==VK_DOWN)){
        ed_shell_hist_move(e,vk==VK_UP?-1:1);
        return 0;
    }
    if(ch==9&&nh>0){   /* Tab accepts the ':' command hint */
        if(e->cmdh_sel<0) e->cmdh_sel=0;
        if(e->cmdh_sel>=nh) e->cmdh_sel=nh-1;
        snprintf(e->shbuf,sizeof e->shbuf,":%s",hints[e->cmdh_sel]);
        e->shlen=strlen(e->shbuf); e->shcur=e->shlen;
        return 0;
    }
    if(ch==0&&(vk==VK_UP||vk==VK_DOWN)&&nh>0&&!e->log_expand){
        if(vk==VK_UP){ if(e->cmdh_sel>0) e->cmdh_sel--; }
        else { if(e->cmdh_sel+1<nh) e->cmdh_sel++; }
        return 0;
    }
    if(ch==27){   /* Esc: shrink log first, then leave the shell */
        if(e->log_expand){ e->log_expand=0; e->log_scroll=0; return 0; }
        e->focus=0; return 0;
    }
    if(ch==13){ ed_shell_run(e); return e->sh_quit; }
    if(ch==8){                            /* Backspace */
        if(e->shcur>0){
            e->shpos=e->shn;
            e->shcur--;
            memmove(&e->shbuf[e->shcur],&e->shbuf[e->shcur+1],e->shlen-e->shcur);
            if(e->shlen>0) e->shlen--;
            e->shbuf[e->shlen]=0;
        }
        return 0;
    }
    if(ed_bind_match(e,KB_COPY,vk,ctl)){   /* stops the shell cold: clear, collapse, leave */
        e->shlen=0; e->shcur=0; e->shbuf[0]=0;
        e->log_expand=0; e->log_scroll=0; e->focus=0;
        return 0;
    }
    if(ed_bind_match(e,KB_PASTE,vk,ctl)){   /* system clipboard first, code clipboard next */
        size_t slen=0; char *st=ed_sys_paste(&slen);
        const char *s=st?st:e->clip;
        size_t m=st?slen:e->cliplen, k=0;
        char clean[1024]; size_t cn=0;
        /* shell takes one line: cut at the first newline */
        while(k<m&&cn+1<sizeof clean){
            if(s[k]=='\n'||s[k]=='\r') break;
            clean[cn++]=s[k++];
        }
        if(st) free(st);
        if(cn==0) return 0;
        if(e->shlen+cn>=sizeof e->shbuf) cn=sizeof e->shbuf-e->shlen-1;
        if(cn==0) return 0;
        e->shpos=e->shn;
        memmove(&e->shbuf[e->shcur+cn],&e->shbuf[e->shcur],e->shlen-e->shcur+1);
        memcpy(&e->shbuf[e->shcur],clean,cn);
        e->shcur+=cn; e->shlen+=cn;
        return 0;
    }
    if(ch==0){
        if(vk==VK_LEFT&&e->shcur>0){ e->shcur--; return 0; }
        if(vk==VK_RIGHT&&e->shcur<e->shlen){ e->shcur++; return 0; }
        if(vk==VK_HOME){ e->shcur=0; return 0; }
        if(vk==VK_END){ e->shcur=e->shlen; return 0; }
        if(vk==VK_UP||vk==VK_DOWN){
            int show=e->log_expand?3:1;
            int max=e->logn-show; if(max<0) max=0;
            if(e->log_expand){   /* look up through the output */
                if(vk==VK_UP&&e->log_scroll<max) e->log_scroll++;
                if(vk==VK_DOWN&&e->log_scroll>0) e->log_scroll--;
                return 0;
            }
        }
        if(vk==VK_UP&&e->shn>0){
            ed_shell_hist_move(e,-1);
            return 0;
        }
        if(vk==VK_DOWN){
            ed_shell_hist_move(e,1);
            return 0;
        }
        if(ed_bind_match(e,KB_ICONS,vk,ctl)){ ed_icons_toggle(e); return 0; }
        return 0;
    }
    if(ch>=32){
        unsigned long cp;
        char tmp[4]; int m=0;
        if(e->shlen==0&&ch==':') e->cmdh_sel=0;
        if(ch>=0xD800&&ch<=0xDBFF){ e->surr_hi=ch; return 0; }
        if(ch>=0xDC00&&ch<=0xDFFF&&e->surr_hi){
            cp=0x10000UL+(((unsigned long)(e->surr_hi-0xD800))<<10)+(unsigned long)(ch-0xDC00);
            e->surr_hi=0;
        } else { e->surr_hi=0; cp=(unsigned long)ch; }
        if(cp<0x80) tmp[m++]=(char)cp;
        else if(cp<0x800){ tmp[m++]=(char)(0xC0|(cp>>6)); tmp[m++]=(char)(0x80|(cp&0x3F)); }
        else if(cp<0x10000){ tmp[m++]=(char)(0xE0|(cp>>12)); tmp[m++]=(char)(0x80|((cp>>6)&0x3F)); tmp[m++]=(char)(0x80|(cp&0x3F)); }
        else { tmp[m++]=(char)(0xF0|(cp>>18)); tmp[m++]=(char)(0x80|((cp>>12)&0x3F)); tmp[m++]=(char)(0x80|((cp>>6)&0x3F)); tmp[m++]=(char)(0x80|(cp&0x3F)); }
        if(e->shlen+(size_t)m>=sizeof e->shbuf) return 0;
        e->shpos=e->shn;
        memmove(&e->shbuf[e->shcur+m],&e->shbuf[e->shcur],e->shlen-e->shcur+1);
        memcpy(&e->shbuf[e->shcur],tmp,(size_t)m);
        e->shcur+=(size_t)m; e->shlen+=(size_t)m;
        return 0;
    }
    return 0;
}

/* Insert codepoint as UTF-8 */
static void ed_insert_cp(Editor *e,unsigned long cp){
    char tmp[4]; int m=0;
    if(cp<0x80) tmp[m++]=(char)cp;
    else if(cp<0x800){ tmp[m++]=(char)(0xC0|(cp>>6)); tmp[m++]=(char)(0x80|(cp&0x3F)); }
    else if(cp<0x10000){ tmp[m++]=(char)(0xE0|(cp>>12)); tmp[m++]=(char)(0x80|((cp>>6)&0x3F)); tmp[m++]=(char)(0x80|(cp&0x3F)); }
    else { tmp[m++]=(char)(0xF0|(cp>>18)); tmp[m++]=(char)(0x80|((cp>>12)&0x3F)); tmp[m++]=(char)(0x80|((cp>>6)&0x3F)); tmp[m++]=(char)(0x80|(cp&0x3F)); }
    ed_insert_bytes(e,tmp,(size_t)m);
}

/* Keyboard: 1 quits editor */
static int ed_key(Editor *e,HANDLE hin,DWORD old,WORD vk,WCHAR ch,DWORD ctl){
    int ctrl=(ctl&(LEFT_CTRL_PRESSED|RIGHT_CTRL_PRESSED))!=0;
    int alt=(ctl&(LEFT_ALT_PRESSED|RIGHT_ALT_PRESSED))!=0;
    int shift=(ctl&SHIFT_PRESSED)!=0;
    (void)hin; (void)old;
    if(e->fnd_on) return ed_fnd_key(e,vk,ch,ctl);
    if(e->pf_on) return ed_pfind_key(e,vk,ch,ctl);
    if(e->focus!=2) e->view_detached=0;
    if(ctrl&&vk==VK_TAB){ ed_tab_cycle(e,shift?-1:1); return 0; }
    if(ed_bind_match(e,KB_PFIND,vk,ctl)){ ed_pfind_open(e); return 0; }
    if(e->find_bar) return ed_find_key(e,vk,ch,ctl);
    if(ed_bind_match(e,KB_RUN,vk,ctl)){ ed_run_current(e); return 0; }
    if(ed_bind_match(e,KB_NEXT_ERR,vk,ctl)){ ed_diag_jump(e,shift?-1:1); return 0; }
    if(ed_bind_match(e,KB_TERM,vk,ctl)){
        int was=e->show_panel;
        if(e->show_panel){
            if(e->focus==2){ e->show_panel=0; e->focus=0; }
            else e->focus=2;
        } else { e->show_panel=1; e->focus=2; }
        e->status[0]=0;
        if(e->show_panel!=was) ed_cfg_save(e);
        return 0;
    }
    if(ed_bind_match(e,KB_SIDE,vk,ctl)){
        e->ex_side=!e->ex_side;
        snprintf(e->status,sizeof e->status,"explorer moved to the %s",e->ex_side?"right":"left");
        ed_cfg_save(e); return 0;
    }
    if(ed_bind_match(e,KB_FIND,vk,ctl)){ ed_find_open(e); return 0; }
    if(e->focus==2) return ed_shell_key(e,vk,ch,ctl);   /* bottom shell owns keys */
    if(alt&&!ctrl) return 0;   /* Ignore menu keys */
    if(ed_bind_match(e,KB_EXPLORER,vk,ctl)){ e->show_ex=!e->show_ex; if(!e->show_ex) e->focus=0; ed_cfg_save(e); return 0; }
    if(e->focus==1){
        if(ch==27||ed_bind_match(e,KB_FILES,vk,ctl)){ e->focus=0; e->sug_on=0; return 0; }   /* Esc back to code */
        if(ch==13){ if(e->exsel_ok) ex_open_idx(e,e->exsel); return 0; }
        if(ch==8){ ex_join(e,".."); return 0; }
        if(!ctrl&&(ch=='n'||ch=='N')){ ex_target_dir(e,e->namdir,sizeof e->namdir); ed_naming_start(e,1); return 0; }
        if(!ctrl&&(ch=='d'||ch=='D')){ ex_target_dir(e,e->namdir,sizeof e->namdir); ed_naming_start(e,2); return 0; }
        if(ed_bind_match(e,KB_COPY,vk,ctl)){
            if(!e->exsel_ok||e->exsel>=e->exn) return 0;
            snprintf(e->fcb,sizeof e->fcb,"%s",e->ex[e->exsel].full);
            e->fcb_dir=e->ex[e->exsel].isdir; e->fcb_ok=1;
            snprintf(e->status,sizeof e->status,"copied '%s'",e->ex[e->exsel].name);
            return 0;
        }
        if(ed_bind_match(e,KB_PASTE,vk,ctl)){
            if(!e->fcb_ok){ snprintf(e->status,sizeof e->status,"nothing to paste"); return 0; }
            { char dir[1024], cand[512], dest[1120], base[256];
              const char *b=e->fcb, *p, *l=NULL;
              ex_target_dir(e,dir,sizeof dir);
              for(p=b;*p;p++) if(*p=='\\'||*p=='/') l=p;
              snprintf(base,sizeof base,"%s",l?l+1:b);
              ed_unique(cand,sizeof cand,dir,base,e->fcb_dir);
              snprintf(dest,sizeof dest,"%s\\%s",dir,cand);
              if(ed_copy_tree(e->fcb,dest,0)){ ex_reveal(e,dest); snprintf(e->status,sizeof e->status,"pasted '%s'",cand); }
              else snprintf(e->status,sizeof e->status,"paste failed"); }
            return 0;
        }
        if(ch==0){
            if(vk==VK_UP&&e->exn>0){
                if(!e->exsel_ok) e->exsel=0;
                else if(e->exsel>0) e->exsel--;
                e->exsel_ok=1; e->status[0]=0;
            }
            else if(vk==VK_DOWN&&e->exn>0){
                if(!e->exsel_ok) e->exsel=0;
                else if(e->exsel+1<e->exn) e->exsel++;
                e->exsel_ok=1; e->status[0]=0;
            }
            else if(vk==VK_F2&&e->exsel_ok&&e->exsel<e->exn){ ed_naming_start(e,3); }
            else if(vk==VK_LEFT&&e->exsel<e->exn){
                e->exsel_ok=1;
                if(e->ex[e->exsel].isdir&&e->ex[e->exsel].expanded) ex_collapse(e,e->exsel);
                else { size_t i=e->exsel, d=(size_t)e->ex[i].depth;
                    while(i>0&&e->ex[i-1].depth>=(int)d) i--;
                    e->exsel=i; }
                e->status[0]=0;
            }
            else if(vk==VK_RIGHT&&e->exsel<e->exn){
                e->exsel_ok=1;
                if(e->ex[e->exsel].isdir&&!e->ex[e->exsel].expanded) ex_expand(e,e->exsel);
                else ex_open_idx(e,e->exsel);
                e->status[0]=0;
            }
            else if(ed_bind_match(e,KB_ICONS,vk,ctl)){ ed_icons_toggle(e); }
        }
        return 0;
    }
    if(ch==27){
        if(e->find_on){ ed_find_close(e); return 0; }
        if(e->sug_on){ e->sug_on=0; return 0; }
        if(!ed_sel_empty(e)){ e->sel_on=0; e->status[0]=0; return 0; }
        int u=ed_unsaved_count(e);
        if(u>0&&!e->quit_arm){
            const char *name=e->path; size_t i;
            if(!e->dirty||!name[0]){
                name=NULL;
                for(i=0;i<e->nstash;i++) if(e->stash[i].dirty){ name=e->stash[i].path; break; }
            }
            if(name){
                const char *base=name,*p;
                for(p=name;*p;p++) if(*p=='\\'||*p=='/') base=p+1;
                snprintf(e->status,sizeof e->status,"'%s' unsaved (%d file(s)); Esc again quits",base,u);
            } else snprintf(e->status,sizeof e->status,"%d unsaved file(s); Esc again quits",u);
            e->quit_arm=1; return 0;
        }
        return 1;
    }
    e->quit_arm=0;
    if(!e->path[0] && (ch==13||ch==8||ch==9||ch>=32||(ctrl&&(ch==3||ch==22)))){
        snprintf(e->status,sizeof e->status,"open a file from the left first");
        return 0;
    }
    if(ch==0){
        if(e->sug_on&&(vk==VK_UP||vk==VK_DOWN)){
            if(vk==VK_UP) e->sug_sel=(e->sug_sel+e->sug_n-1)%e->sug_n;
            else e->sug_sel=(e->sug_sel+1)%e->sug_n;
            return 0;
        }
        if(vk==VK_UP||vk==VK_DOWN||vk==VK_LEFT||vk==VK_RIGHT){
            e->sug_on=0;
            if(ctl&SHIFT_PRESSED){
                if(!e->sel_on){ e->sel_on=1; e->sel_ay=e->cy; e->sel_ax=e->cx; }
            } else e->sel_on=0;
        }
        if(vk==VK_HOME||vk==VK_END){
            e->sug_on=0;
            if(shift){
                if(!e->sel_on){ e->sel_on=1; e->sel_ay=e->cy; e->sel_ax=e->cx; }
            } else e->sel_on=0;
            e->cx=(vk==VK_HOME)?0:e->ln[e->cy].len;
            e->status[0]=0;
            return 0;
        }
        if(vk==VK_UP&&e->cy>0){ e->cy--; e->status[0]=0; }
        else if(vk==VK_DOWN&&e->cy+1<e->n){ e->cy++; e->status[0]=0; }
        else if(vk==VK_LEFT){ ed_move_left(e); e->status[0]=0; }
        else if(vk==VK_RIGHT){ ed_move_right(e); e->status[0]=0; }
        return 0;
    }
    if(ch==13){ if(e->sug_on){ ed_sug_accept(e); return 0; } ed_newline(e); return 0; }
    if(ch==8){ ed_backspace(e); ed_sug_update(e); return 0; }
    if(ch==9){ if(e->sug_on){ ed_sug_accept(e); return 0; } ed_insert_bytes(e,"    ",4); ed_sug_update(e); return 0; }
    if(ed_bind_match(e,KB_COPY,vk,ctl)){ if(!ed_sel_empty(e)) ed_copy_sel(e); else ed_copy_line(e); return 0; }
    if(ed_bind_match(e,KB_CUT,vk,ctl)){
        if(!ed_sel_empty(e)&&!e->is_binary){
            ed_copy_sel(e); ed_undo_push(e,0); ed_sel_delete(e); ed_sug_update(e);
        }
        return 0;
    }
    if(ed_bind_match(e,KB_PASTE,vk,ctl)){
        size_t slen=0; char *st=ed_sys_paste(&slen);
        if(st){ ed_insert_bytes(e,st,slen); free(st); }
        else if(e->clip) ed_insert_bytes(e,e->clip,e->cliplen);
        else snprintf(e->status,sizeof e->status,"clipboard empty (^C copies first)");
        e->sug_on=0;
        return 0;
    }
    if(ed_bind_match(e,KB_SAVE,vk,ctl)){ ed_save(e); return 0; }
    if(ed_bind_match(e,KB_UNDO,vk,ctl)){ ed_undo(e); e->sug_on=0; return 0; }
    if(ed_bind_match(e,KB_REDO,vk,ctl)){ ed_redo(e); e->sug_on=0; return 0; }
    if(ed_bind_match(e,KB_FILES,vk,ctl)){ e->focus=1; ex_refresh_keep(e); e->status[0]=0; return 0; }
    if(ctrl){
        if(ch==1||vk=='A'){
            e->sel_on=1; e->sel_ax=0; e->sel_ay=0;
            e->cy=e->n?e->n-1:0;
            e->cx=e->n?e->ln[e->cy].len:0;
            e->top=e->cy>=(size_t)(e->vh>3?ed_crows(e->vh,e):1)
                ?e->cy-(size_t)(e->vh>3?ed_crows(e->vh,e):1)+1:0;
            e->view_detached=0;
            ed_clamp(e);
            e->sug_on=0; e->status[0]=0;
            return 0;
        }
        if(ch==14&&e->sug_on){ e->sug_sel=(e->sug_sel+1)%e->sug_n; return 0; }
        if(ch==16&&e->sug_on){ e->sug_sel=(e->sug_sel+e->sug_n-1)%e->sug_n; return 0; }
        return 0;
    }
    if(ch>=32){
        if(e->sug_on){
            int ident=(ch>='A'&&ch<='Z')||(ch>='a'&&ch<='z')||(ch>='0'&&ch<='9')||ch=='_';
            if(!ident){ if(ch=='(') ed_sug_accept(e); else e->sug_on=0; }
        }
        if(ch>=0xD800&&ch<=0xDBFF){ e->surr_hi=ch; return 0; }
        { unsigned long cp;
          if(ch>=0xDC00&&ch<=0xDFFF&&e->surr_hi){ cp=0x10000UL+(((unsigned long)(e->surr_hi-0xD800))<<10)+(unsigned long)(ch-0xDC00); e->surr_hi=0; }
          else { e->surr_hi=0; cp=(unsigned long)ch; }
          ed_insert_cp(e,cp); }
        ed_sug_update(e);
        return 0;
    }
    return 0;
}

/* Move the code cursor to window-relative (x,y); clamps into view */
static void ed_set_cursor_xy(Editor *e,int x,int y){
    int H=e->vh>0?e->vh:24, W=e->vw>0?e->vw:80;
    int cx=ed_code_x(e), cw=ed_code_w(e,W);
    int rows=ed_crows(H,e);
    long li=(long)e->top+(y-1);
    if(li<0) li=0;
    if(li>=(long)e->top+rows) li=(long)e->top+rows-1;
    if(li>=(long)e->n) li=(long)e->n-1;
    if(li<0) li=0;
    e->focus=0; e->status[0]=0;
    e->view_detached=0;
    e->cy=(size_t)li; ed_clamp(e);
    { size_t want=(x>=cx+e->numw)?(size_t)(x-cx-e->numw):0;
      if(cw>0&&want>(size_t)(cw-1)) want=(size_t)(cw-1);
      char *b=e->ln[e->cy].b; size_t L=e->ln[e->cy].len;
      size_t k=0,d=0,col=0;
      while(k<L&&d<e->dleft+want){ int w=ed_chw((unsigned char)b[k],(int)col); d+=(size_t)w; col+=(size_t)w; k++; }
      while(k<L&&ed_is_cont((unsigned char)b[k])) k++;
      e->cx=k; }
}

/* Click/wheel in buffer coords */
static void ed_mouse(Editor *e,MOUSE_EVENT_RECORD *m){
    if(e->fnd_on){
        HANDLE fh=GetStdHandle(STD_OUTPUT_HANDLE);
        CONSOLE_SCREEN_BUFFER_INFO fbi;
        int fx=0,fy=0,fW=e->vw>0?e->vw:80,fH=e->vh>0?e->vh:24;
        int x0,y0,bw,bh,rows,r;
        if(GetConsoleScreenBufferInfo(fh,&fbi)){ fx=fbi.srWindow.Left; fy=fbi.srWindow.Top; }
        ed_fnd_geom(e,fW,fH,&x0,&y0,&bw,&bh);
    rows=bh-6; if(rows<1) rows=1;
        if(m->dwEventFlags==MOUSE_WHEELED){
            int up=((short)(m->dwButtonState>>16))>0;
            if(up){ if(e->fnd_top>=3) e->fnd_top-=3; else e->fnd_top=0; }
            else e->fnd_top+=3;
            if(e->fnd_top>e->fnd_n-rows) e->fnd_top=e->fnd_n-rows;
            if(e->fnd_top<0) e->fnd_top=0;
            return;
        }
        if((m->dwEventFlags==0||m->dwEventFlags==DOUBLE_CLICK)&&
           (m->dwButtonState&FROM_LEFT_1ST_BUTTON_PRESSED)){
            int mx=(int)m->dwMousePosition.X-fx, my=(int)m->dwMousePosition.Y-fy;
            if(mx<x0||mx>=x0+bw||my<y0||my>=y0+bh){ e->fnd_on=0; return; }
            r=my-(y0+5);          /* first result row sits on y0+5 */
            if(r>=0&&r<rows){
                int idx=e->fnd_top+r;
                if(idx<e->fnd_n){ e->fnd_sel=idx; e->fnd_top=idx; }
            }
        }
        return;
    }
    if(e->pf_on){
        HANDLE fh=GetStdHandle(STD_OUTPUT_HANDLE);
        CONSOLE_SCREEN_BUFFER_INFO fbi;
        int fx=0,fy=0,fW=e->vw>0?e->vw:80,fH=e->vh>0?e->vh:24;
        int x0,y0,bw,bh,rows,r;
        if(GetConsoleScreenBufferInfo(fh,&fbi)){ fx=fbi.srWindow.Left; fy=fbi.srWindow.Top; }
        ed_pfind_geom(e,fW,fH,&x0,&y0,&bw,&bh);
        rows=bh-6; if(rows<1) rows=1;
        if(m->dwEventFlags==MOUSE_WHEELED){
            int up=((short)(m->dwButtonState>>16))>0;
            if(up){ if(e->pf_top>=3) e->pf_top-=3; else e->pf_top=0; }
            else e->pf_top+=3;
            if(e->pf_top>e->pf_n-rows) e->pf_top=e->pf_n-rows;
            if(e->pf_top<0) e->pf_top=0;
            return;
        }
        if((m->dwEventFlags==0||m->dwEventFlags==DOUBLE_CLICK)&&
           (m->dwButtonState&FROM_LEFT_1ST_BUTTON_PRESSED)){
            int mx=(int)m->dwMousePosition.X-fx, my=(int)m->dwMousePosition.Y-fy;
            if(mx<x0||mx>=x0+bw||my<y0||my>=y0+bh){ e->pf_on=0; return; }
            r=my-(y0+4);
            if(r>=0&&r<rows){
                int idx=e->pf_top+r;
                if(idx<e->pf_n){ e->pf_sel=idx; }
            }
        }
        return;
    }
    HANDLE h=GetStdHandle(STD_OUTPUT_HANDLE);
    CONSOLE_SCREEN_BUFFER_INFO bi;
    int wx=0,wy=0,x,y,H,W,ph,ptop,CROWS,exvw,exx,divx,codex,scrollx;
    int ctrl=(m->dwControlKeyState&(LEFT_CTRL_PRESSED|RIGHT_CTRL_PRESSED))!=0;
    if(GetConsoleScreenBufferInfo(h,&bi)){ wx=bi.srWindow.Left; wy=bi.srWindow.Top; }
    x=(int)m->dwMousePosition.X-wx;
    y=(int)m->dwMousePosition.Y-wy;
    H=e->vh>0?e->vh:24; W=e->vw>0?e->vw:80;
    ph=ed_panel_rows(e); ptop=ed_panel_top(H,e);
    CROWS=ed_crows(H,e);
    exvw=ed_ex_width(e); exx=ed_ex_x(e,W); divx=ed_div_x(e,W);
    codex=ed_code_x(e); scrollx=ed_scroll_x(e,W);
    if(m->dwEventFlags==MOUSE_WHEELED){
        int up=((short)(m->dwButtonState>>16))>0;
        if(ph>0&&y>=ptop){
            int outn=ph-2, max=e->logn-outn;
            if(max<0) max=0;
            if(up) e->log_scroll+=3; else e->log_scroll-=3;
            if(e->log_scroll>max) e->log_scroll=max;
            if(e->log_scroll<0) e->log_scroll=0;
        } else if(exvw>0&&x>=exx&&x<exx+exvw){
            if(up){ if(e->extop>=3) e->extop-=3; else e->extop=0; }
            else e->extop+=3;
            ed_clamp_view(e,H);
            /* keep selection visible so next draw doesn't snap back */
            if(e->exn>0){
                int erows=CROWS>2?CROWS-2:1;
                if((int)e->exsel<(int)e->extop) e->exsel=e->extop;
                if((int)e->exsel>=(int)(e->extop+erows)){
                    e->exsel=e->extop+erows-1;
                    if(e->exsel>=e->exn) e->exsel=e->exn-1;
                }
                e->exsel_ok=1;
                e->focus=1;
            }
            e->status[0]=0;
        } else {
            if(ctrl){
                if(e->n>0){
                    if(up){ e->cy=0; e->top=0; }
                    else {
                        e->cy=e->n-1;
                        e->top=e->cy>=(size_t)CROWS?e->cy-(size_t)CROWS+1:0;
                    }
                    ed_clamp(e);
                    ed_clamp_view(e,H);
                    e->focus=0;
                }
                return;
            }
            if(up){ if(e->top>=3) e->top-=3; else e->top=0; }
            else e->top+=3;
            ed_clamp_view(e,H);
            e->sug_on=0;
            e->view_detached=1;
            e->status[0]=0;
        }
        return;
    }
    if(m->dwEventFlags==MOUSE_MOVED){
        if(e->drag_scroll){
            int thumb_start,thumb_len,row=y-1;
            ed_scrollbar_geom(e,CROWS,&thumb_start,&thumb_len);
            (void)thumb_start;
            ed_scrollbar_set(e,CROWS,row-e->drag_scroll_off,thumb_len);
            e->view_detached=1;
            return;
        }
        if(e->drag_ex){
            int maxex=W-24, nw;
            if(maxex<ED_EXW_MIN) maxex=ED_EXW_MIN;
            nw=e->ex_side?W-x:x+1;
            if(nw<ED_EXW_MIN) nw=ED_EXW_MIN;
            if(nw>maxex) nw=maxex;
            e->exw=nw;
            return;
        }
        if(e->drag_panel){
            int maxph=H-3, nh=H-y;
            if(maxph>ED_PANEL_MAX) maxph=ED_PANEL_MAX;
            if(maxph<ED_PANEL_MIN) maxph=ED_PANEL_MIN;
            if(nh<ED_PANEL_MIN) nh=ED_PANEL_MIN;
            if(nh>maxph) nh=maxph;
            e->panel_h=nh;
            return;
        }
        /* drag-select: extend the code cursor while left is held */
        if(e->sel_armed && (m->dwButtonState&FROM_LEFT_1ST_BUTTON_PRESSED) && e->focus==0){
            if(x>=codex&&x<scrollx) ed_set_cursor_xy(e,x,y);
        }
        return;
    }
    if(m->dwEventFlags!=0 && m->dwEventFlags!=DOUBLE_CLICK) return;
    if(x<0||y<0) return;
    if(e->find_bar&&(m->dwButtonState&(FROM_LEFT_1ST_BUTTON_PRESSED|RIGHTMOST_BUTTON_PRESSED))){
        int fx0,fy0,fw,fh;
        ed_find_geom(e,W,H,&fx0,&fy0,&fw,&fh);
        if(x>=fx0&&x<fx0+fw&&y>=fy0&&y<fy0+fh) return;
        ed_find_close(e);
    }
    { int left=(m->dwButtonState&FROM_LEFT_1ST_BUTTON_PRESSED)!=0;
      int right=(m->dwButtonState&RIGHTMOST_BUTTON_PRESSED)!=0;
      if(!left&&!right){
        /* Button release: finish any drag (plain click clears the selection) */
        if(e->drag_ex||e->drag_panel||e->drag_scroll){
            e->drag_ex=0; e->drag_panel=0; e->drag_scroll=0; ed_cfg_save(e);
        } else if(e->sel_armed){
            e->sel_armed=0;
            if(e->sel_ay==e->cy && e->sel_ax==e->cx) e->sel_on=0;
        }
        return;
      }
      if(right){
        /* Right-click: select the row under the cursor, then menu it */
        if(y>=1&&y<=CROWS&&exvw>0&&x>=exx&&x<exx+exvw){
            int r=y-1;
            char clicked[1120]={0};
            if(r>=2&&e->extop+(size_t)(r-2)<e->exn)
                snprintf(clicked,sizeof clicked,"%s",e->ex[e->extop+(size_t)(r-2)].full);
            ex_refresh_keep(e);
            e->focus=1;
            if(clicked[0]){
                size_t j; int found=0;
                for(j=0;j<e->exn;j++)
                    if(!strcmp(e->ex[j].full,clicked)){ e->exsel=j; e->exsel_ok=1; found=1; break; }
                if(found) ed_menu_open(e,1,x,y,NULL);
                else ed_menu_open(e,0,x,y,NULL);
            } else if(r==1||r>=2){
                ed_menu_open(e,0,x,y,e->exdir);   /* project root row / empty area */
            } else {
                ed_menu_open(e,0,x,y,NULL);       /* header: current selection */
            }
        } else if(x>=codex&&x<scrollx&&y>=0&&y<=CROWS){
            e->focus=0;
            e->sug_on=0;
            ed_menu_open(e,2,x,y,NULL);
        }
        return;
      }
      if(y==0&&left){ ed_tab_click(e,x); return; }
      if(left&&x==scrollx&&y>=1&&y<=CROWS){
          int thumb_start,thumb_len,row=y-1;
          ed_scrollbar_geom(e,CROWS,&thumb_start,&thumb_len);
          if(row>=thumb_start&&row<thumb_start+thumb_len){
              e->drag_scroll=1;
              e->drag_scroll_off=row-thumb_start;
          } else {
              ed_scrollbar_set(e,CROWS,row-thumb_len/2,thumb_len);
          }
          e->view_detached=1;
          return;
      }
      if(!left) return;
    }
    /* Divider under the mouse: start resizing the explorer bar */
    if(exvw>0&&x==divx&&y>=1&&y<=CROWS){ e->drag_ex=1; return; }
    /* Terminal title row: drag to resize the panel */
    if(ph>0&&y==ptop){ e->drag_panel=1; return; }
    /* Terminal body: click the command row to focus the shell */
    if(ph>0&&y>=ptop+1){
        if(y==H-1){
            int pc=(int)e->sh_promptw;
            e->focus=2; e->status[0]=0;
            if(x>=pc){ size_t want=(size_t)(x-pc), k=0, d=0;
                while(k<e->shlen&&d<want){ int w0=ed_chw((unsigned char)e->shbuf[k],(int)d); d+=(size_t)w0; k++; }
                e->shcur=k;
                if(e->shcur>e->shlen) e->shcur=e->shlen;
            } else e->shcur=0;
        } else if(e->focus==2){
            e->focus=0; e->log_scroll=0;
        }
        return;
    }
    if(y<1||y>CROWS) return;
    if(exvw>0&&x>=exx&&x<exx+exvw){
        int r=y-1;
        if(r==0){
            e->focus=1;
            if(x>=exx+exvw-4) ed_menu_open(e,0,x,y,NULL);
            return;
        }
        if(r==1){ e->focus=1; ex_toggle_all(e); return; }
        if(r>=2){
            size_t idx=e->extop+(size_t)(r-2);
            if(idx<e->exn){ e->focus=1; ex_open_idx(e,idx); }
        }
        return;
    }
    if(e->sug_on&&e->focus==0){
        int x0,y0,bw,bh,i;
        ed_sug_geom(e,W,H,&x0,&y0,&bw,&bh);
        for(i=0;i<e->sug_n&&i<bh;i++){
            if(y==y0+i&&x>=x0&&x<x0+bw){ e->sug_sel=i; ed_sug_accept(e); return; }
        }
    }
    ed_set_cursor_xy(e,x,y);
    e->sel_on=1; e->sel_ay=e->cy; e->sel_ax=e->cx; e->sel_armed=1;
    e->sug_on=0;   /* suggestions only appear after typing */
}

static int ed_isdir(const char *p){
    DWORD a=GetFileAttributesA(p);
    return a!=INVALID_FILE_ATTRIBUTES && (a&FILE_ATTRIBUTE_DIRECTORY);
}

static int ed_isfile(const char *p){
    DWORD a=GetFileAttributesA(p);
    return a!=INVALID_FILE_ATTRIBUTES && !(a&FILE_ATTRIBUTE_DIRECTORY);
}

/* Case-insensitive path equality on canonical absolute paths */
static int ed_same_path(const char *a,const char *b){
    char fa[1024], fb[1024];
    DWORD na=GetFullPathNameA(a,sizeof fa,fa,NULL);
    DWORD nb=GetFullPathNameA(b,sizeof fb,fb,NULL);
    if(na==0||na>=sizeof fa||nb==0||nb>=sizeof fb)
        return _stricmp(a,b)==0;
    return _stricmp(fa,fb)==0;
}

/* Drive root like C:\ , C: , C:/ or UNC root \\srv\share */
static int ed_is_drive_root(const char *p){
    if(!p||!p[0]) return 1;
    if((p[0]=='\\'&&p[1]=='\\')){           /* \\srv\share[\] */
        const char *q=strchr(p+2,'\\');
        if(!q) q=strchr(p+2,'/');
        if(!q) return 1;
        { const char *r=strchr(q+1,'\\'); const char *s=strchr(q+1,'/');
          const char *e=r; if(s&&( !e||s<e)) e=s;
          if(!e) return 1;                  /* \\srv\share */
          if(!e[1]) return 1;               /* \\srv\share\ */
          return 0; }
    }
    if(((p[0]>='A'&&p[0]<='Z')||(p[0]>='a'&&p[0]<='z'))&&p[1]==':'){
        if(!p[2]) return 1;                 /* C: */
        if((p[2]=='\\'||p[2]=='/')&&!p[3]) return 1;  /* C:\ */
        return 0;
    }
    if(!strcmp(p,".")||!strcmp(p,".\\")||!strcmp(p,"./")) return 0; /* resolved later */
    return 0;
}

/* True when p is the user's home folder (%USERPROFILE%) */
static int ed_is_home_path(const char *p){
    char home[1024];
    DWORD n=GetEnvironmentVariableA("USERPROFILE",home,sizeof home);
    if(n==0||n>=sizeof home||!home[0]) return 0;
    return ed_same_path(p,home);
}

/* lcode edits one project folder; home/drive roots are refused without an explicit path. */
static int ed_is_bad_implicit_root(const char *p){
    char full[1024];
    DWORD n=GetFullPathNameA(p[0]?p:".",sizeof full,full,NULL);
    const char *q=(n>0&&n<sizeof full)?full:p;
    if(ed_is_drive_root(q)) return 1;
    if(ed_is_home_path(q)) return 1;
    return 0;
}

/* Entry: NULL=cwd, file, or dir */
static void lc_code(const char *start){
    HANDLE hin=GetStdHandle(STD_INPUT_HANDLE);
    DWORD m=0, old=0;
    Editor e;
    if(start&&luc_has_suffix(start,".cfg")){
        lc_install_plugin_cfg(start);
        return;
    }
    if((!hin||hin==INVALID_HANDLE_VALUE||!GetConsoleMode(hin,&m)) && !getenv("LUC_TESTDRAW")){
        printf("lcode needs a console (stdin is redirected).\n");
        return;
    }
    old=m;
    SetConsoleMode(hin,(old|ENABLE_MOUSE_INPUT|ENABLE_EXTENDED_FLAGS)
        & ~(DWORD)(ENABLE_LINE_INPUT|ENABLE_ECHO_INPUT|ENABLE_PROCESSED_INPUT|ENABLE_QUICK_EDIT_MODE));
    memset(&e,0,sizeof e);
    e.focus=1;
    ed_layout_defaults(&e);
    ed_bind_defaults(&e);
    ed_plug_scan(&e);              /* built-in luc + plugin/ folders */
    ed_layout_from_plugins(&e);    /* bars/panel from enabled plugin.cfg files */
    ed_cfg_load(&e);               /* saved theme + layout overrides */
    /* Icons are off for now: the tree stays plain on every terminal.
       F8 still forces them on when wanted (saved mode is ignored here). */
    e.icons_nerd=0;
    if(start&&*start){
        if(ed_isdir(start)){
            snprintf(e.exdir,sizeof e.exdir,"%s",start);
            e.focus=1;
        } else {
            snprintf(e.path,sizeof e.path,"%s",start);
            { const char *b1=strrchr(start,'/'),*b2=strrchr(start,'\\');
              const char *b=b1; if(b2&&(!b||b2>b)) b=b2;
              if(b){ size_t n=(size_t)(b-start);
                     if(n>=sizeof e.exdir) n=sizeof e.exdir-1;
                     memcpy(e.exdir,start,n); e.exdir[n]=0;
                     if(!e.exdir[0]) snprintf(e.exdir,sizeof e.exdir,"."); }
              else if(!GetCurrentDirectoryA(sizeof e.exdir,e.exdir))
                  snprintf(e.exdir,sizeof e.exdir,"."); }
            e.focus=0;
            ed_load(&e);
            ed_saved_set(&e);
        }
    } else {
        if(!GetCurrentDirectoryA(sizeof e.exdir,e.exdir))
            snprintf(e.exdir,sizeof e.exdir,".");
        if(ed_is_bad_implicit_root(e.exdir)){
            SetConsoleMode(hin,old);
            printf("lcode: refusing to open '%s' - that is %s, not a project folder.\n",
                e.exdir,
                ed_is_home_path(e.exdir)?"your home folder":"a drive root");
            printf("cd into a project first, or pass one explicitly:\n");
            printf("  luc --lcode <folder>   (e.g. luc --lcode .\\luc)\n");
            return;
        }
        e.focus=1;
    }
    ex_refresh(&e);
    e.last_edit_tick=GetTickCount();
    ed_recovery_load(&e);
    if(e.diag_pending){ e.diag_pending=0; ed_diag_live(&e); }
    /* Alt screen, no wrap: long rows truncate */
    printf("\x1b[?1049h\x1b[?7l");
    fflush(stdout);
    /* One write per frame: avoids flicker */
    setvbuf(stdout,NULL,_IOFBF,65536);
    for(;;){
        ed_clamp(&e);
        ed_draw(&e);
        for(;;){
            INPUT_RECORD ir; DWORD n=0, wait_ms=INFINITE, wait_rc;
            if(e.opt_autosave&&ed_unsaved_count(&e)>0){
                DWORD elapsed=GetTickCount()-e.last_edit_tick;
                wait_ms=elapsed>=30000?0:30000-elapsed;
            }
            wait_rc=WaitForSingleObject(hin,wait_ms);
            if(wait_rc==WAIT_TIMEOUT){
                ed_save_all(&e);
                e.last_edit_tick=GetTickCount();
                break;
            }
            if(wait_rc!=WAIT_OBJECT_0) goto done;
            if(!ReadConsoleInputW(hin,&ir,1,&n)) goto done;
            if(e.menu||e.naming){
                /* Popup owns input until dismissed */
                if(ir.EventType==MOUSE_EVENT){
                    WORD f=ir.Event.MouseEvent.dwEventFlags;
                    if(f==MOUSE_MOVED) continue;
                    if(f==0||f==DOUBLE_CLICK){
                        DWORD bs=ir.Event.MouseEvent.dwButtonState;
                        if(bs&FROM_LEFT_1ST_BUTTON_PRESSED){
                            HANDLE hh=GetStdHandle(STD_OUTPUT_HANDLE);
                            CONSOLE_SCREEN_BUFFER_INFO bb;
                            int wx=0,wy=0;
                            if(GetConsoleScreenBufferInfo(hh,&bb)){ wx=bb.srWindow.Left; wy=bb.srWindow.Top; }
                            if(e.naming){
                                /* Click on the inline row: place the caret;
                                   anywhere else: cancel (nothing is created) */
                                int W=e.vw>0?e.vw:80;
                                int exvw=ed_ex_width(&e), exx=ed_ex_x(&e,W);
                                int x=(int)ir.Event.MouseEvent.dwMousePosition.X-wx;
                                int y=(int)ir.Event.MouseEvent.dwMousePosition.Y-wy;
                                if(e.namidx>=0&&exvw>0&&x>=exx&&x<exx+exvw&&y==e.namidx-(int)e.extop+3){
                                    ed_nam_click(&e,x-exx);
                                    ed_draw_modal(&e); continue;
                                }
                                ed_naming_cancel(&e);
                                break;
                            } else {
                                ed_popup_click(&e,
                                    (int)ir.Event.MouseEvent.dwMousePosition.X-wx,
                                    (int)ir.Event.MouseEvent.dwMousePosition.Y-wy,
                                    e.vw>0?e.vw:80, e.vh>0?e.vh:24);
                                if(!e.menu) e.click_eat=1;
                                if(e.menu){ ed_draw_modal(&e); continue; }
                                break;
                            }
                        }
                        if(bs&RIGHTMOST_BUTTON_PRESSED){
                            if(e.naming) ed_naming_cancel(&e); else e.menu=0;
                            break;
                        }
                    }
                    continue;
                }
                if(ir.EventType==WINDOW_BUFFER_SIZE_EVENT) break;
                if(ir.EventType!=KEY_EVENT||!ir.Event.KeyEvent.bKeyDown) continue;
                { int wasnam=e.naming;
                  ed_popup_key(&e,ir.Event.KeyEvent.wVirtualKeyCode,
                               ir.Event.KeyEvent.uChar.UnicodeChar);
                  if(e.naming&&!wasnam) break;   /* naming began: full repaint */
                  if(e.menu||e.naming){ ed_draw_modal(&e); continue; }
                  break; }
            }
            if(e.show_plugins){
                /* :plugin modal owns input until dismissed */                if(ir.EventType==MOUSE_EVENT){
                    WORD f=ir.Event.MouseEvent.dwEventFlags;
                    if(f==MOUSE_MOVED) continue;
                    if(f==0||f==DOUBLE_CLICK){
                        DWORD bs=ir.Event.MouseEvent.dwButtonState;
                        HANDLE hh=GetStdHandle(STD_OUTPUT_HANDLE);
                        CONSOLE_SCREEN_BUFFER_INFO bb;
                        int wx=0,wy=0;
                        if(GetConsoleScreenBufferInfo(hh,&bb)){ wx=bb.srWindow.Left; wy=bb.srWindow.Top; }
                        if(bs&FROM_LEFT_1ST_BUTTON_PRESSED){
                            ed_plug_click(&e,
                                (int)ir.Event.MouseEvent.dwMousePosition.X-wx,
                                (int)ir.Event.MouseEvent.dwMousePosition.Y-wy,
                                e.vw>0?e.vw:80, e.vh>0?e.vh:24);
                            if(!e.show_plugins) e.click_eat=1;
                        } else e.show_plugins=0;
                        if(e.show_plugins){ ed_draw_modal(&e); continue; }
                        break;
                    }
                    continue;
                }
                if(ir.EventType==WINDOW_BUFFER_SIZE_EVENT) break;
                if(ir.EventType!=KEY_EVENT||!ir.Event.KeyEvent.bKeyDown) continue;
                { WORD vk=ir.Event.KeyEvent.wVirtualKeyCode;
                  WCHAR ch=ir.Event.KeyEvent.uChar.UnicodeChar;
                  if(ch==27){ e.show_plugins=0; }
                  else if(ch==13||ch==32){
                      if(e.plug_sel<e.nplugs){
                          e.plugs[e.plug_sel].enabled=!e.plugs[e.plug_sel].enabled;
                          ed_plug_write(&e,e.plug_sel);
                      }
                  }
                  else if(ch==0){
                      int n=e.nplugs<ED_PLUGSHOW?e.nplugs:ED_PLUGSHOW;
                      if(vk==VK_UP&&e.plug_sel>0) e.plug_sel--;
                      else if(vk==VK_DOWN&&e.plug_sel+1<e.nplugs) e.plug_sel++;
                      if(e.plug_sel<e.plug_top) e.plug_top=e.plug_sel;
                      if(e.plug_sel>=e.plug_top+n) e.plug_top=e.plug_sel-n+1;
                  } }
                if(e.show_plugins){ ed_draw_modal(&e); continue; }
                break;
            }
            if(e.show_settings){
                /* :setting modal owns input until dismissed */
                if(ir.EventType==MOUSE_EVENT){
                    WORD f=ir.Event.MouseEvent.dwEventFlags;
                    if(f==MOUSE_MOVED) continue;
                    if(f==MOUSE_WHEELED){
                        int up=((short)(ir.Event.MouseEvent.dwButtonState>>16))>0;
                        int nrows=ed_set_nrows(&e,e.set_tab);
                        e.set_key_cap=0;
                        if(e.set_left){
                            if(up){ if(e.set_tab>0){ e.set_tab--; e.set_row=0; e.set_row_top=0; } }
                            else if(e.set_tab+1<ed_set_ntabs(&e)){ e.set_tab++; e.set_row=0; e.set_row_top=0; }
                        } else {
                            if(up){ if(e.set_row>0) e.set_row--; }
                            else if(e.set_row+1<nrows) e.set_row++;
                        }
                        ed_draw_modal(&e); continue;
                    }
                    if(f==0||f==DOUBLE_CLICK){
                        DWORD bs=ir.Event.MouseEvent.dwButtonState;
                        HANDLE hh=GetStdHandle(STD_OUTPUT_HANDLE);
                        CONSOLE_SCREEN_BUFFER_INFO bb;
                        int wx=0,wy=0;
                        if(GetConsoleScreenBufferInfo(hh,&bb)){ wx=bb.srWindow.Left; wy=bb.srWindow.Top; }
                        if(!(bs&FROM_LEFT_1ST_BUTTON_PRESSED)) continue; /* ignore release/right */
                        if(e.set_edit) ed_set_edit_commit(&e);
                        ed_set_click(&e,
                            (int)ir.Event.MouseEvent.dwMousePosition.X-wx,
                            (int)ir.Event.MouseEvent.dwMousePosition.Y-wy,
                            e.vw>0?e.vw:80, e.vh>0?e.vh:24);
                        if(!e.show_settings) e.click_eat=1;
                        if(e.set_full){ e.set_full=0; ed_draw(&e); continue; }
                        if(e.show_settings){ ed_draw_modal(&e); continue; }
                        break;
                    }
                    continue;
                }
                if(ir.EventType==WINDOW_BUFFER_SIZE_EVENT) break;
                if(ir.EventType!=KEY_EVENT||!ir.Event.KeyEvent.bKeyDown) continue;
                { WORD vk=ir.Event.KeyEvent.wVirtualKeyCode;
                  WCHAR ch=ir.Event.KeyEvent.uChar.UnicodeChar;
                  int ntabs=ed_set_ntabs(&e), nrows=ed_set_nrows(&e,e.set_tab);
                  if(e.set_key_cap) ed_set_key_capture(&e,vk,ch,ir.Event.KeyEvent.dwControlKeyState);
                  else if(e.set_edit){ ed_set_edit_key(&e,vk,ch); }
                  else if(ch==27){ e.show_settings=0; }
                  else if(ch==13||ch==32){
                      if(e.set_left){ e.set_left=0; }
                      else ed_set_activate(&e);
                  }
                  else if(ch==9){ e.set_left=!e.set_left; }
                  else if(ch==0){
                      if(vk==VK_LEFT||vk==VK_RIGHT){ e.set_left=!e.set_left; }
                      else if(vk==VK_UP||vk==VK_DOWN){
                          if(e.set_left){
                              if(vk==VK_UP&&e.set_tab>0){ e.set_tab--; e.set_row=0; e.set_row_top=0; }
                              else if(vk==VK_DOWN&&e.set_tab+1<ntabs){ e.set_tab++; e.set_row=0; e.set_row_top=0; }
                          } else {
                              if(vk==VK_UP&&e.set_row>0) e.set_row--;
                              else if(vk==VK_DOWN&&e.set_row+1<nrows) e.set_row++;
                          }
                      }
                  } }
                if(e.set_full){ e.set_full=0; ed_draw(&e); continue; }
                if(e.show_settings){ ed_draw_modal(&e); continue; }
                break;
            }
            if(ir.EventType==MOUSE_EVENT){
                WORD f=ir.Event.MouseEvent.dwEventFlags;
                if(f==MOUSE_MOVED){
                    /* Only active drags redraw on move; plain hover stays cheap */
                    if((e.sel_armed||e.drag_ex||e.drag_panel||e.drag_scroll) &&
                       (ir.Event.MouseEvent.dwButtonState&FROM_LEFT_1ST_BUTTON_PRESSED)){
                        e.click_eat=0;
                        ed_mouse(&e,&ir.Event.MouseEvent);
                        break;
                    }
                    continue;
                }
                if((f==0||f==DOUBLE_CLICK)
                   && (ir.Event.MouseEvent.dwButtonState&FROM_LEFT_1ST_BUTTON_PRESSED)
                   && e.click_eat){ e.click_eat=0; continue; }
                e.click_eat=0;
                ed_mouse(&e,&ir.Event.MouseEvent);
                break;
            }
            if(ir.EventType==WINDOW_BUFFER_SIZE_EVENT) break;  /* resize */
            if(ir.EventType!=KEY_EVENT||!ir.Event.KeyEvent.bKeyDown) continue;
            if(ed_key(&e,hin,old,ir.Event.KeyEvent.wVirtualKeyCode,
                      ir.Event.KeyEvent.uChar.UnicodeChar,
                      ir.Event.KeyEvent.dwControlKeyState)) goto done;
            break;
        }
        if(e.diag_pending){ e.diag_pending=0; ed_diag_live(&e); }
        if(e.recovery_pending) ed_recovery_save(&e);
        ed_clamp(&e);
    }
done:
    if(e.diag_pending){ e.diag_pending=0; ed_diag_live(&e); }
    if(e.recovery_pending) ed_recovery_save(&e);
    SetConsoleMode(hin,old);
    ex_free(&e);
    ed_free(&e);
    ed_undo_clear(&e);
    { size_t si; for(si=0;si<e.nstash;si++){ size_t k; for(k=0;k<e.stash[si].n;k++) free(e.stash[si].ln[k].b); free(e.stash[si].ln); }
      free(e.stash); e.stash=NULL; e.nstash=0; }
    printf("\x1b[?25h\x1b[?7h\x1b[?1049l\n");
    fflush(stdout);
    setvbuf(stdout,NULL,_IONBF,0);
}
#else
static void lc_code(const char *start){
    (void)start;
    printf("lcode needs a Windows console for now.\n");
}
#endif
