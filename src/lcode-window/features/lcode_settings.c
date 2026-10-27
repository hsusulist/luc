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
