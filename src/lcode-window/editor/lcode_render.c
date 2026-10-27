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

