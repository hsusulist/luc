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

