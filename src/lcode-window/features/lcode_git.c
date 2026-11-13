/* :git modal: left tabs (Commit / Sync), right rows. Same look and keys as :setting.
   Commit tab: type a message, press Commit (stages everything, then commits).
   Sync tab:   Pull / Push run immediately. Output also goes to the terminal log. */
#define ED_GITW 14   /* left column width */
static const char *ED_GIT_TABS[]={"Commit","Sync"};
#define ED_GIT_NTABS 2

/* Run "git <args>" in the project folder, capture output (lines joined by '\n').
   stdin is NUL and terminal prompts are off so a missing login can't hang the editor.
   Returns the exit code, or -1 when git could not be started. */
static void ed_git_emit(Editor *e,char *out,size_t cap,size_t *ow,const char *raw,int logit){
    char clean[512];
    ed_sanitize(clean,sizeof clean,raw);
    if(logit) ed_log(e,"%s",clean);
    if(out&&cap){
        size_t n=strlen(clean);
        if(*ow+n+2<cap){ memcpy(out+*ow,clean,n); *ow+=n; out[(*ow)++]='\n'; out[*ow]=0; }
    }
}
static int ed_git_exec(Editor *e,const char *args,char *out,size_t cap,int logit){
    char cmd[4096], cwd[1024], chunk[512], line[512], envold[8];
    SECURITY_ATTRIBUTES sa; STARTUPINFOA si; PROCESS_INFORMATION pi;
    HANDLE rd=NULL,wr=NULL,nul=INVALID_HANDLE_VALUE;
    DWORD got=0,code=1,n; size_t used=0,ow=0; int ok, setenv_=0;
    if(out&&cap) out[0]=0;
    if(!e->exdir[0]) return -1;
    n=GetFullPathNameA(e->exdir,sizeof cwd,cwd,NULL);
    if(!n||n>=sizeof cwd) return -1;
    if(snprintf(cmd,sizeof cmd,"git %s",args)>=(int)sizeof cmd) return -1;
    memset(&sa,0,sizeof sa); sa.nLength=sizeof sa; sa.bInheritHandle=TRUE;
    if(!CreatePipe(&rd,&wr,&sa,0)) return -1;
    SetHandleInformation(rd,HANDLE_FLAG_INHERIT,0);
    nul=CreateFileA("NUL",GENERIC_READ,FILE_SHARE_READ|FILE_SHARE_WRITE,&sa,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,NULL);
    memset(&si,0,sizeof si); si.cb=sizeof si; si.dwFlags=STARTF_USESTDHANDLES|STARTF_USESHOWWINDOW;
    si.wShowWindow=SW_HIDE; si.hStdOutput=wr; si.hStdError=wr; si.hStdInput=nul;
    memset(&pi,0,sizeof pi);
    if(GetEnvironmentVariableA("GIT_TERMINAL_PROMPT",envold,sizeof envold)==0){
        SetEnvironmentVariableA("GIT_TERMINAL_PROMPT","0"); setenv_=1;
    }
    ok=CreateProcessA(NULL,cmd,NULL,NULL,TRUE,CREATE_NO_WINDOW,NULL,cwd,&si,&pi);
    if(setenv_) SetEnvironmentVariableA("GIT_TERMINAL_PROMPT",NULL);
    CloseHandle(wr);
    if(nul!=INVALID_HANDLE_VALUE) CloseHandle(nul);
    if(!ok){ CloseHandle(rd); return -1; }
    if(logit) ed_log(e,"$ %s",cmd);
    while(ReadFile(rd,chunk,sizeof chunk,&got,NULL)&&got){
        DWORD i;
        for(i=0;i<got;i++){
            if(chunk[i]=='\n'||chunk[i]=='\r'){
                if(used){ line[used]=0; ed_git_emit(e,out,cap,&ow,line,logit); used=0; }
            } else if(used+1<sizeof line) line[used++]=chunk[i];
        }
    }
    if(used){ line[used]=0; ed_git_emit(e,out,cap,&ow,line,logit); }
    CloseHandle(rd);
    WaitForSingleObject(pi.hProcess,INFINITE);
    GetExitCodeProcess(pi.hProcess,&code);
    CloseHandle(pi.hThread); CloseHandle(pi.hProcess);
    return (int)code;
}

/* One-line result for the footer: prefer a fatal/error line, else the last line */
static void ed_git_result(Editor *e,const char *what,const char *out,int rc){
    char last[160]="", err[160]="";
    const char *p=out;
    while(p&&*p){
        const char *nl=strchr(p,'\n'); size_t n=nl?(size_t)(nl-p):strlen(p);
        if(n>0){
            size_t k=n<sizeof last-1?n:sizeof last-1;
            memcpy(last,p,k); last[k]=0;
            if(!strncmp(p,"fatal",5)||!strncmp(p,"error",5)||!strncmp(p,"CONFLICT",8)){
                k=n<sizeof err-1?n:sizeof err-1; memcpy(err,p,k); err[k]=0;
            }
        }
        p=nl?nl+1:NULL;
    }
    if(rc<0) snprintf(e->git_note,sizeof e->git_note,"%s: cannot run git (installed? folder open?)",what);
    else if(rc) snprintf(e->git_note,sizeof e->git_note,"%s failed: %s",what,err[0]?err:(last[0]?last:"see terminal"));
    else snprintf(e->git_note,sizeof e->git_note,"%s: %s",what,last[0]?last:"done");
    e->status[0]=0;
}

/* Branch line + changed files from `git status -sb` */
static void ed_git_refresh(Editor *e){
    char out[16384]; const char *p; int rc;
    e->git_nfiles=0; e->git_nchanged=0; e->git_branch[0]=0; e->git_err[0]=0;
    rc=ed_git_exec(e,"status -sb",out,sizeof out,0);
    if(rc<0){ snprintf(e->git_err,sizeof e->git_err,"git not found, or no folder is open"); return; }
    if(rc){
        char *nl=strchr(out,'\n'); if(nl) *nl=0;
        snprintf(e->git_err,sizeof e->git_err,"%s",out[0]?out:"not a git repository");
        return;
    }
    p=out;
    while(*p){
        const char *nl=strchr(p,'\n'); size_t n=nl?(size_t)(nl-p):strlen(p);
        if(n>=3&&!strncmp(p,"## ",3)){
            size_t k=n-3<sizeof e->git_branch-1?n-3:sizeof e->git_branch-1;
            memcpy(e->git_branch,p+3,k); e->git_branch[k]=0;
        } else if(n>0){
            e->git_nchanged++;
            if(e->git_nfiles<ED_GITFILES){
                size_t k=n<sizeof e->git_files[0]-1?n:sizeof e->git_files[0]-1;
                memcpy(e->git_files[e->git_nfiles],p,k); e->git_files[e->git_nfiles][k]=0;
                e->git_nfiles++;
            }
        }
        p=nl?nl+1:p+n;
    }
}

static void ed_git_edit_start(Editor *e){
    e->git_tab=0; e->git_row=0; e->git_left=0; e->git_edit=1;
    e->git_cur=e->git_len;
}
static void ed_git_open(Editor *e){
    e->show_git=1; e->git_row_top=0; e->git_note[0]=0;
    ed_git_refresh(e);
    ed_git_edit_start(e);
}

static void ed_git_commit(Editor *e){
    char out[4096], args[2400], quoted[2048]; int rc;
    if(!e->git_len){
        snprintf(e->git_note,sizeof e->git_note,"type a commit message first");
        ed_git_edit_start(e);
        return;
    }
    if(!ed_git_quote(quoted,sizeof quoted,e->git_msg)){
        snprintf(e->git_note,sizeof e->git_note,"commit message is too long"); return;
    }
    snprintf(e->git_note,sizeof e->git_note,"committing...");
    ed_draw_modal(e);
    rc=ed_git_exec(e,"add -A",out,sizeof out,1);
    if(rc){ ed_git_result(e,"stage",out,rc); ed_git_refresh(e); return; }
    snprintf(args,sizeof args,"commit -m %s",quoted);
    rc=ed_git_exec(e,args,out,sizeof out,1);
    ed_git_result(e,"commit",out,rc);
    if(rc==0){
        snprintf(e->git_note,sizeof e->git_note,"committed: %.120s",e->git_msg);
        e->git_msg[0]=0; e->git_len=0; e->git_cur=0; e->git_off=0;
    }
    ed_git_refresh(e);
    ex_refresh_keep(e);
}
static void ed_git_sync(Editor *e,int push){
    char out[8192]; int rc;
    snprintf(e->git_note,sizeof e->git_note,"%s...",push?"pushing":"pulling");
    ed_draw_modal(e);
    rc=ed_git_exec(e,push?"push":"pull --no-edit",out,sizeof out,1);
    ed_git_result(e,push?"push":"pull",out,rc);
    ed_git_refresh(e);
    ex_refresh_keep(e);   /* pull may add/remove files */
}

static int ed_git_nrows(Editor *e,int tab){
    if(tab==0) return 2+(e->git_nfiles?e->git_nfiles+(e->git_nchanged>e->git_nfiles):1);
    return 3;
}
static void ed_git_geom(Editor *e,int W,int H,int *x0,int *y0,int *w,int *h){
    int rows=12, bw;
    (void)e;
    { int maxrows=H-5; if(maxrows<1) maxrows=1; if(rows>maxrows) rows=maxrows; }
    if(rows<4) rows=4;
    bw=W-4; if(bw>68) bw=68; if(bw<36) bw=36;
    if(bw>W-2) bw=W-2;
    { int bh=rows+4;
      *x0=(W-bw)/2; *y0=(H-bh)/2; if(*y0<0) *y0=0;
      *w=bw; *h=bh; }
}
static void ed_git_row_text(Editor *e,int tab,int row,char *label,size_t lc,char *val,size_t vc,int *kind){
    *kind=SET_INFO; val[0]=0;
    if(tab==0){
        if(row==0){ snprintf(label,lc,"Message"); snprintf(val,vc,"%s",e->git_msg); *kind=SET_TEXT; }
        else if(row==1){ snprintf(label,lc,"Commit all changes"); snprintf(val,vc,"Enter"); *kind=SET_ACTION; }
        else if(e->git_nfiles==0){
            snprintf(label,lc,"%s",e->git_err[0]?e->git_err:"No changes");
        } else {
            int fi=row-2;
            if(fi<e->git_nfiles) snprintf(label,lc,"%s",e->git_files[fi]);
            else snprintf(label,lc,"... %d more",e->git_nchanged-e->git_nfiles);
        }
        return;
    }
    if(row==0){ snprintf(label,lc,"Pull"); snprintf(val,vc,"Enter"); *kind=SET_ACTION; }
    else if(row==1){ snprintf(label,lc,"Push"); snprintf(val,vc,"Enter"); *kind=SET_ACTION; }
    else snprintf(label,lc,"%s",e->git_err[0]?e->git_err:(e->git_branch[0]?e->git_branch:"no branch"));
}
/* Enter / click on a row */
static void ed_git_activate(Editor *e){
    int tab=e->git_tab,row=e->git_row;
    if(tab==0){
        if(row==0) ed_git_edit_start(e);
        else if(row==1) ed_git_commit(e);
    } else {
        if(row==0) ed_git_sync(e,0);
        else if(row==1) ed_git_sync(e,1);
    }
}

static void ed_git_draw(Editor *e,int W,int H){
    int x0,y0,bw,bh,i,k,rows,nrows,cw,cx0;
    if(!e->show_git) return;
    ed_git_geom(e,W,H,&x0,&y0,&bw,&bh);
    rows=bh-4; if(rows<1) rows=1;
    if(e->git_tab<0) e->git_tab=0;
    if(e->git_tab>=ED_GIT_NTABS) e->git_tab=ED_GIT_NTABS-1;
    nrows=ed_git_nrows(e,e->git_tab);
    if(e->git_row>=nrows) e->git_row=nrows-1;
    if(e->git_row<0) e->git_row=0;
    if(e->git_row_top>e->git_row) e->git_row_top=e->git_row;
    if(e->git_row>=e->git_row_top+rows) e->git_row_top=e->git_row-rows+1;
    if(e->git_row_top>nrows-rows) e->git_row_top=nrows-rows;
    if(e->git_row_top<0) e->git_row_top=0;
    cw=bw-ED_GITW-3;
    cx0=x0+3+ED_GITW;
    e->git_cur_x=0; e->git_cur_y=0;
    /* top border + header */
    printf("\x1b[%d;%dH%s\xe2\x94\x8c",y0+1,x0+1,TH_TDIM);
    for(k=0;k<bw-2;k++) fputs("\xe2\x94\x80",stdout);
    printf("\xe2\x94\x90\x1b[0m");
    printf("\x1b[%d;%dH\x1b[48;2;45;45;45m\x1b[97m\xe2\x94\x82",y0+2,x0+1);
    { char hb[160]; int L;
      if(e->git_branch[0]) snprintf(hb,sizeof hb," git  %s",e->git_branch);
      else snprintf(hb,sizeof hb," git");
      L=(int)strlen(hb); if(L>bw-2) L=bw-2;
      fwrite(hb,1,(size_t)L,stdout);
      for(k=L;k<bw-2;k++) fputc(' ',stdout); }
    printf("\x1b[0m%s\xe2\x94\x82\x1b[0m",TH_TDIM);
    for(i=0;i<rows;i++){
        int yy=y0+3+i;
        printf("\x1b[%d;%dH%s\xe2\x94\x82",yy,x0+1,TH_TDIM);
        if(i<ED_GIT_NTABS){
            int cur=(i==e->git_tab);
            if(cur) printf(e->git_left?"\x1b[48;2;0;120;215m\x1b[97m":"\x1b[48;2;0;70;120m\x1b[97m");
            else printf("\x1b[48;2;17;17;17m\x1b[97m");
            printf(" %-*.*s",ED_GITW-1,ED_GITW-1,ED_GIT_TABS[i]);
        } else {
            printf("\x1b[48;2;17;17;17m");
            for(k=0;k<ED_GITW;k++) fputc(' ',stdout);
        }
        printf("\x1b[48;2;30;30;30m%s\xe2\x94\x82",TH_TDIM);
        printf("\x1b[%d;%dH",yy,cx0);
        if(i<nrows){
            int ri=e->git_row_top+i, kind, sel;
            char label[200], val[300];
            if(ri>=nrows) ri=nrows-1;
            sel=(!e->git_left&&ri==e->git_row);
            ed_git_row_text(e,e->git_tab,ri,label,sizeof label,val,sizeof val,&kind);
            if(sel) printf("\x1b[48;2;0;120;215m\x1b[97m");
            else printf("\x1b[48;2;30;30;30m\x1b[97m");
            if(kind==SET_INFO){
                int L=(int)strlen(label); if(L>cw-1) L=cw-1;
                if(!sel) printf("\x1b[90m");
                fputc(' ',stdout);
                fwrite(label,1,(size_t)L,stdout);
                for(k=1+L;k<cw;k++) fputc(' ',stdout);
            } else if(kind==SET_TEXT){
                /* message field fills the row; scrolls so the caret stays visible */
                int Ll=(int)strlen(label), pad=1, fieldw, inner, vis=0, cw_=0;
                size_t q;
                fieldw=cw-1-Ll-pad-1; inner=fieldw-4; if(inner<4) inner=4;
                if(e->git_off>e->git_cur) e->git_off=e->git_cur;
                for(;;){
                    int w=0;
                    for(q=e->git_off;q<e->git_cur&&q<e->git_len;q++) w+=ed_chw((unsigned char)e->git_msg[q],w);
                    if(w<=inner-1||e->git_off>=e->git_cur) break;
                    do e->git_off++; while(e->git_off<e->git_len&&ed_is_cont((unsigned char)e->git_msg[e->git_off]));
                }
                fputc(' ',stdout); fwrite(label,1,(size_t)Ll,stdout);
                fputc(' ',stdout);
                printf("[ ");
                if(!e->git_len&&!e->git_edit){ printf("\x1b[90m"); fwrite("(Enter to type)",1,15>(size_t)inner?(size_t)inner:15,stdout); vis=15>inner?inner:15; }
                else {
                    printf("\x1b[38;2;120;200;255m");
                    for(q=e->git_off;q<e->git_len;q++){
                        int w=ed_chw((unsigned char)e->git_msg[q],vis);
                        if(vis+w>inner) break;
                        fputc((unsigned char)e->git_msg[q],stdout);
                        vis+=w;
                    }
                }
                if(sel) printf("\x1b[48;2;0;120;215m\x1b[97m");
                else printf("\x1b[48;2;30;30;30m\x1b[97m");
                for(k=vis;k<inner;k++) fputc(' ',stdout);
                printf(" ]");
                { int used=1+Ll+pad+fieldw; for(k=used;k<cw;k++) fputc(' ',stdout); }
                if(e->git_edit){
                    for(q=e->git_off;q<e->git_cur&&q<e->git_len;q++) cw_+=ed_chw((unsigned char)e->git_msg[q],cw_);
                    e->git_cur_x=cx0+1+Ll+pad+2+cw_;
                    e->git_cur_y=yy;
                }
            } else {
                int fieldw, Ll=(int)strlen(label), pad, vlen=(int)strlen(val);
                int maxlab=cw-8; if(maxlab<1) maxlab=1;
                if(Ll>maxlab) Ll=maxlab;
                fieldw=vlen+4;
                pad=cw-1-Ll-fieldw; if(pad<0) pad=0;
                fputc(' ',stdout);
                fwrite(label,1,(size_t)Ll,stdout);
                for(k=0;k<pad;k++) fputc(' ',stdout);
                printf("[ \x1b[96m");
                fwrite(val,1,(size_t)vlen,stdout);
                if(sel) printf("\x1b[48;2;0;120;215m\x1b[97m");
                else printf("\x1b[48;2;30;30;30m\x1b[97m");
                printf(" ]");
                { int used=1+Ll+pad+fieldw; for(k=used;k<cw;k++) fputc(' ',stdout); }
            }
        } else {
            printf("\x1b[48;2;30;30;30m");
            for(k=0;k<cw;k++) fputc(' ',stdout);
        }
        printf("\x1b[%d;%dH%s\xe2\x94\x82\x1b[0m",yy,x0+bw,TH_TDIM);
    }
    /* footer (result of the last action, else a key hint) + bottom border */
    printf("\x1b[%d;%dH%s\xe2\x94\x82\x1b[48;2;22;22;22m%s",y0+3+rows,x0+1,TH_TDIM,TH_TDIM);
    { const char *hint; char buf[200]; int used, kk;
      if(e->git_note[0]){ snprintf(buf,sizeof buf," %s",e->git_note); hint=buf; }
      else if(e->git_edit) hint=" Enter done  Esc stop typing";
      else hint=bw>=52?" Tab switch  Enter select  Esc close":" Tab / Enter / Esc";
      used=(int)strlen(hint); if(used>bw-2) used=bw-2;
      fwrite(hint,1,(size_t)used,stdout);
      for(kk=used;kk<bw-2;kk++) fputc(' ',stdout); }
    printf("\x1b[%d;%dH%s\xe2\x94\x82\x1b[0m",y0+3+rows,x0+bw,TH_TDIM);
    printf("\x1b[%d;%dH%s\xe2\x94\x94",y0+4+rows,x0+1,TH_TDIM);
    for(k=0;k<bw-2;k++) fputs("\xe2\x94\x80",stdout);
    printf("\xe2\x94\x98\x1b[0m");
    if(e->git_edit&&e->git_cur_x>0)
        printf("\x1b[%d;%dH\x1b[?25h",e->git_cur_y,e->git_cur_x);
}

static void ed_git_click(Editor *e,int x,int y,int W,int H){
    int x0,y0,bw,bh,i,nrows,rows;
    if(!e->show_git) return;
    ed_git_geom(e,W,H,&x0,&y0,&bw,&bh);
    if(x<x0||x>=x0+bw||y<y0||y>=y0+bh){ e->show_git=0; e->git_edit=0; return; }
    if(y<=y0+1) return;
    rows=bh-4; if(rows<1) rows=1;
    i=y-(y0+2);
    if(i<0||i>=rows) return;
    e->git_edit=0;
    if(x>=x0+1&&x<x0+1+ED_GITW){
        if(i<ED_GIT_NTABS){ e->git_tab=i; e->git_row=0; e->git_row_top=0; e->git_left=1; e->git_note[0]=0; }
        return;
    }
    if(x>=x0+2+ED_GITW){
        int ri=e->git_row_top+i;
        nrows=ed_git_nrows(e,e->git_tab);
        if(ri>=0&&ri<nrows){
            e->git_row=ri; e->git_left=0;
            ed_git_activate(e);
        }
    }
}
static void ed_git_wheel(Editor *e,int up){
    int nrows=ed_git_nrows(e,e->git_tab);
    e->git_edit=0;
    if(e->git_left){
        if(up){ if(e->git_tab>0){ e->git_tab--; e->git_row=0; e->git_row_top=0; e->git_note[0]=0; } }
        else if(e->git_tab+1<ED_GIT_NTABS){ e->git_tab++; e->git_row=0; e->git_row_top=0; e->git_note[0]=0; }
    } else {
        if(up){ if(e->git_row>0) e->git_row--; }
        else if(e->git_row+1<nrows) e->git_row++;
    }
}

/* Typing in the commit message field */
static int ed_git_edit_key(Editor *e,WORD vk,WCHAR ch){
    if(!e->git_edit) return 0;
    if(ch==27){ e->git_edit=0; return 1; }
    if(ch==13){ e->git_edit=0; e->git_row=1; return 1; }   /* Enter: on to the Commit button */
    if(ch==8){
        if(e->git_cur>0){
            size_t from=e->git_cur;
            do e->git_cur--; while(e->git_cur>0&&ed_is_cont((unsigned char)e->git_msg[e->git_cur]));
            memmove(&e->git_msg[e->git_cur],&e->git_msg[from],e->git_len-from+1);
            e->git_len-=from-e->git_cur;
        }
        return 1;
    }
    if(ch==0){
        if(vk==VK_LEFT&&e->git_cur>0){
            do e->git_cur--; while(e->git_cur>0&&ed_is_cont((unsigned char)e->git_msg[e->git_cur]));
        } else if(vk==VK_RIGHT&&e->git_cur<e->git_len){
            do e->git_cur++; while(e->git_cur<e->git_len&&ed_is_cont((unsigned char)e->git_msg[e->git_cur]));
        } else if(vk==VK_HOME) e->git_cur=0;
        else if(vk==VK_END) e->git_cur=e->git_len;
        else if(vk==VK_DELETE&&e->git_cur<e->git_len){
            size_t k=e->git_cur;
            do k++; while(k<e->git_len&&ed_is_cont((unsigned char)e->git_msg[k]));
            memmove(&e->git_msg[e->git_cur],&e->git_msg[k],e->git_len-k);
            e->git_len-=k-e->git_cur;
            e->git_msg[e->git_len]=0;
        }
        return 1;
    }
    if(ch>=32){
        unsigned long cp=(unsigned long)ch;
        char tmp[4]; int m=0, k;
        if(cp<0x80) tmp[m++]=(char)cp;
        else if(cp<0x800){ tmp[m++]=(char)(0xC0|(cp>>6)); tmp[m++]=(char)(0x80|(cp&0x3F)); }
        else { tmp[m++]=(char)(0xE0|(cp>>12)); tmp[m++]=(char)(0x80|((cp>>6)&0x3F)); tmp[m++]=(char)(0x80|(cp&0x3F)); }
        if(e->git_len+(size_t)m>=sizeof e->git_msg) return 1;
        memmove(&e->git_msg[e->git_cur+m],&e->git_msg[e->git_cur],e->git_len-e->git_cur);
        for(k=0;k<m;k++) e->git_msg[e->git_cur+k]=tmp[k];
        e->git_cur+=(size_t)m; e->git_len+=(size_t)m; e->git_msg[e->git_len]=0;
        e->git_note[0]=0;
        return 1;
    }
    return 1;
}
/* Modal keys (not editing): Esc closes, Tab / arrows move, Enter/Space act */
static void ed_git_key(Editor *e,WORD vk,WCHAR ch){
    int nrows=ed_git_nrows(e,e->git_tab);
    if(e->git_edit){ ed_git_edit_key(e,vk,ch); return; }
    if(ch==27){ e->show_git=0; return; }
    if(ch==13||ch==32){
        if(e->git_left){
            e->git_left=0;
            if(e->git_tab==0&&e->git_row==0) ed_git_edit_start(e);
        } else ed_git_activate(e);
        return;
    }
    if(ch==9){ e->git_left=!e->git_left; return; }
    if(ch==0){
        if(vk==VK_LEFT||vk==VK_RIGHT){ e->git_left=!e->git_left; }
        else if(vk==VK_UP||vk==VK_DOWN){
            if(e->git_left){
                if(vk==VK_UP&&e->git_tab>0){ e->git_tab--; e->git_row=0; e->git_row_top=0; e->git_note[0]=0; }
                else if(vk==VK_DOWN&&e->git_tab+1<ED_GIT_NTABS){ e->git_tab++; e->git_row=0; e->git_row_top=0; e->git_note[0]=0; }
            } else {
                if(vk==VK_UP&&e->git_row>0) e->git_row--;
                else if(vk==VK_DOWN&&e->git_row+1<nrows) e->git_row++;
            }
        }
    }
}
