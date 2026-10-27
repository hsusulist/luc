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

