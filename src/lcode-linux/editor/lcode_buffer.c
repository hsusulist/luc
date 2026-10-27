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

