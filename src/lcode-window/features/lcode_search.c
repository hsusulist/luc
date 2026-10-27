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

