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
    ed_free(&e);
    printf("\x1b[?25h\x1b[?7h\x1b[?1049l\n");
    fflush(stdout);
    setvbuf(stdout,NULL,_IONBF,0);
}
