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

