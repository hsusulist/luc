/*
** luc_libs.c -- ALL standard libraries merged into ONE translation unit
** (base, buffer, coro, io, json, list, math, os, string, window)
** Auto-merged from the original luc_lib_*.c files - each original file
** is delimited by a ==================== banner below.
*/
#include "luc.h"


/* ==================== luc_lib_base.c ==================== */
/*
** luc_lib_base.c -- base library: print, tostring, pcall, require, xpcall...
*/
/* ---- base ---- */

LFN(f_print){ UNUSED_SELF;
    for(int i=0;i<nargs;i++){
        Str *s=tostr(L->stack[base+i]);
        if(i) fputc('\t',stdout);
        fwrite(s->s,1,(size_t)s->len,stdout);
    }
    fputc('\n',stdout);
    return 0;
}
LFN(f_tostring){ UNUSED_SELF; RET(0,mkobj(LT_STR,tostr(AR(0)))); return 1; }
LFN(f_tonumber){ UNUSED_SELF;
    Value v=AR(0);
    if(nargs>=2){
        int b=checkint(L,base,nargs,1,"tonumber");
        Str *s=checkstr(L,base,nargs,0,"tonumber");
        char *end; long long r=strtoll(s->s,&end,b);
        if(end==s->s){ RET(0,NIL); } else RET(0,mknum((double)r));
        return 1;
    }
    if(v.t==LT_NUM){ RET(0,v); return 1; }
    if(v.t==LT_STR){ double d;
        if(str2num(AS_STR(v)->s,AS_STR(v)->len,&d)) RET(0,mknum(d)); else RET(0,NIL);
        return 1; }
    RET(0,NIL); return 1;
}
LFN(f_type){ UNUSED_SELF; RET(0,cstrv(type_name(AR(0)))); return 1; }
LFN(f_rawlen){ UNUSED_SELF; RET(0,mknum((double)vm_len(AR(0)))); return 1; }
LFN(f_error){ UNUSED_SELF;
    Value v=AR(0);
    if(v.t==LT_STR && (nargs<2 || checknum(L,base,nargs,1,"error")!=0)){
        char b[1200];
        snprintf(b,sizeof b,"%s:%d: %s",
                 L->cursource?L->cursource->s:"?",L->curline,AS_STR(v)->s);
        luc_throw(cstrv(b));
    }
    luc_throw(v);
    return 0;
}
LFN(f_assert){ UNUSED_SELF;
    if(!truthy(AR(0))){
        Value m=AR(1);
        if(m.t==LT_NIL) luc_error("assertion failed!");
        luc_throw(m);
    }
    return nargs;
}
LFN(f_pcall){ UNUSED_SELF;
    if(nargs<1) luc_error("bad argument #1 to 'pcall' (value expected)");
    ErrJmp ej; ej.prev=V.errjmp; V.errjmp=&ej;
    volatile int savenci=L->nci, savetop=L->top;
    if(setjmp(ej.jb)==0){
        int n=vm_call(L,base,nargs-1,-1);
        V.errjmp=ej.prev;
        for(int i=n;i>0;i--) L->stack[base+i]=L->stack[base+i-1];
        L->stack[base]=mkbool(1);
        return n+1;
    }
    V.errjmp=ej.prev;
    L->nci=savenci; L->top=savetop;
    close_upvals(L,base);
    L->stack[base]=mkbool(0); L->stack[base+1]=V.errval;
    return 2;
}
LFN(f_select){ UNUSED_SELF;
    Value v=AR(0);
    if(v.t==LT_STR && AS_STR(v)->len==1 && AS_STR(v)->s[0]=='#'){
        RET(0,mknum((double)(nargs-1))); return 1;
    }
    int n=(int)checknum(L,base,nargs,0,"select");
    if(n<0) n=nargs+n;
    if(n<1) luc_error("bad argument #1 to 'select' (index out of range)");
    int cnt=nargs-n;
    if(cnt<0) cnt=0;
    for(int i=0;i<cnt;i++) L->stack[base+i]=L->stack[base+n+i];
    return cnt;
}
LFN(f_next){ UNUSED_SELF;
    Table *t=checktab(L,base,nargs,0,"next");
    Value k,v2;
    if(tab_next(t,AR(1),&k,&v2)){ RET(0,k); RET(1,v2); return 2; }
    RET(0,NIL); return 1;
}
LFN(f_inext){ UNUSED_SELF;
    Table *t=checktab(L,base,nargs,0,"ipairs");
    int i=(int)checknum(L,base,nargs,1,"ipairs")+1;
    Value v=tab_get(t,mknum((double)i));
    if(v.t==LT_NIL){ RET(0,NIL); return 1; }
    RET(0,mknum((double)i)); RET(1,v);
    return 2;
}
LFN(f_ipairs){ UNUSED_SELF;
    checktab(L,base,nargs,0,"ipairs");
    Value t=AR(0);
    RET(0,mkobj(LT_CFUNC,cfunc_new(f_inext,"inext",0)));
    RET(1,t); RET(2,mknum(0));
    return 3;
}
LFN(f_pairs){ UNUSED_SELF;
    checktab(L,base,nargs,0,"pairs");
    Value t=AR(0);
    RET(0,mkobj(LT_CFUNC,cfunc_new(f_next,"next",0)));
    RET(1,t); RET(2,NIL);
    return 3;
}
int f_unpack(LucState *L,int base,int nargs,CFunc *self){ UNUSED_SELF;
    Table *t=checktab(L,base,nargs,0,"unpack");
    int i = nargs>=2? checkint(L,base,nargs,1,"unpack") : 1;
    int j = nargs>=3? checkint(L,base,nargs,2,"unpack") : tab_len(t);
    if(t->o.type==LT_LIST && nargs<3) j=t->alen;
    int n=j-i+1; if(n<0) n=0;
    ensure_stack(L,base+n+8);
    for(int x=0;x<n;x++) L->stack[base+x]=tab_get(t,mknum((double)(i+x)));
    return n;
}
LFN(f_rawget){ UNUSED_SELF; RET(0,tab_get(checktab(L,base,nargs,0,"rawget"),AR(1))); return 1; }
LFN(f_rawset){ UNUSED_SELF; tab_set(checktab(L,base,nargs,0,"rawset"),AR(1),AR(2)); RET(0,AR(0)); return 1; }
LFN(f_rawequal){ UNUSED_SELF; RET(0,mkbool(val_rawequal(AR(0),AR(1)))); return 1; }
LFN(f_collectgarbage){ UNUSED_SELF; gc_collect(); RET(0,mknum((double)V.nalloc)); return 1; }

/* ---- require (rewritten: window/json branches now go through the
        lucL_*_module() entry points, no #ifdef needed here) ------------ */
LFN(f_require){ UNUSED_SELF;
    Str *name=checkstr(L,base,nargs,0,"require");
    Value key=mkobj(LT_STR,name);
    Value cached=tab_get(V.loaded,key);
    if(cached.t!=LT_NIL){ RET(0,cached); return 1; }
    if(strcmp(name->s,"json")==0){
        Value m=lucL_json_module();
        tab_set(V.loaded,key,m);
        RET(0,m); return 1;
    }
    if(strcmp(name->s,"window")==0){
        Value m=lucL_window_module();      /* throws when built without SDL2 */
        tab_set(V.loaded,key,m);
        RET(0,m); return 1;
    }
    int len=0; char found[1024];
    char *src=find_module(name->s,&len,found,sizeof found);
    if(!src) luc_error("module '%s' not found\ncheck LUC_PATH or luc_modules/",name->s);
    int scratch=base+nargs+2;
    ensure_stack(L,scratch+16);
    Closure *cl=luc_compile(src,len,found);
    free(src);
    L->stack[scratch]=mkobj(LT_FUNC,cl);
    vm_call(L,scratch,0,1);
    Value res=L->stack[scratch];
    if(res.t==LT_NIL) res=mkbool(1);
    tab_set(V.loaded,key,res);
    RET(0,res); return 1;
}
LFN(f_xpcall){ UNUSED_SELF;
    if(nargs<2) luc_error("bad argument #2 to 'xpcall' (value expected)");
    Value h=AR(1);
    int na=nargs-2;
    int scratch=base+nargs+2;
    ensure_stack(L,scratch+na+32);
    L->stack[scratch]=AR(0);
    for(int i=0;i<na;i++) L->stack[scratch+1+i]=L->stack[base+2+i];
    ErrJmp ej; ej.prev=V.errjmp; V.errjmp=&ej;
    volatile int savenci=L->nci, savetop=L->top;
    if(setjmp(ej.jb)==0){
        int n=vm_call(L,scratch,na,-1);
        V.errjmp=ej.prev;
        for(int i=n-1;i>=0;i--) L->stack[base+1+i]=L->stack[scratch+i];
        L->stack[base]=mkbool(1);
        return n+1;
    }
    V.errjmp=ej.prev;
    L->nci=savenci; L->top=savetop;
    close_upvals(L,scratch);
    Value err=V.errval;
    ErrJmp hj; hj.prev=V.errjmp; V.errjmp=&hj;
    Value hres=err;
    if(setjmp(hj.jb)==0){
        L->stack[scratch]=h; L->stack[scratch+1]=err;
        vm_call(L,scratch,1,1);
        hres=L->stack[scratch];
    } else {
        L->nci=savenci; L->top=savetop;
        hres=cstrv("error in error handling");
    }
    V.errjmp=hj.prev;
    L->stack[base]=mkbool(0); L->stack[base+1]=hres;
    return 2;
}

void lucL_open_base(void){
    Table *g=V.globals;
    reg(g,"print",f_print);          reg(g,"tostring",f_tostring);
    reg(g,"tonumber",f_tonumber);    reg(g,"type",f_type);
    reg(g,"ipairs",f_ipairs);        reg(g,"pairs",f_pairs);
    reg(g,"next",f_next);            reg(g,"select",f_select);
    reg(g,"error",f_error);          reg(g,"assert",f_assert);
    reg(g,"pcall",f_pcall);          reg(g,"unpack",f_unpack);
    reg(g,"rawget",f_rawget);        reg(g,"rawset",f_rawset);
    reg(g,"rawequal",f_rawequal);    reg(g,"rawlen",f_rawlen);
    reg(g,"collectgarbage",f_collectgarbage);
    reg(g,"require",f_require);
    reg(g,"xpcall",f_xpcall);
    tab_set(g,cstrv("_VERSION"),cstrv(LUC_VERSION));
    tab_set(g,cstrv("_G"),mkobj(LT_TABLE,g));
    tab_set(g,cstrv("package"),mkobj(LT_TABLE,V.loaded));
}


/* ==================== luc_lib_buffer.c ==================== */
/*
** luc_lib_buffer.c -- buffer library (binary data)
*/
/* ---- buffer ---------------------------------------------------------- */
static void bufrange(Buffer *b,int off,int n){
    if(off<0||n<0||off>b->len-n)
        luc_error("buffer access out of bounds (offset %d, %d byte(s), size %d)",off,n,b->len);
}
LFN(f_buf_create){ UNUSED_SELF;
    int n=checkint(L,base,nargs,0,"create");
    if(n<0) luc_error("buffer.create: size must be non-negative");
    RET(0,mkobj(LT_BUFFER,buf_new(n))); return 1;
}
LFN(f_buf_fromstring){ UNUSED_SELF;
    Str *s=checkstr(L,base,nargs,0,"fromstring");
    Buffer *b=buf_new(s->len);
    memcpy(b->b,s->s,(size_t)s->len);
    RET(0,mkobj(LT_BUFFER,b)); return 1;
}
LFN(f_buf_tostring){ UNUSED_SELF;
    Buffer *b=checkbuf(L,base,nargs,0,"tostring");
    RET(0,strv((char*)b->b,b->len)); return 1;
}
LFN(f_buf_len){ UNUSED_SELF;
    RET(0,mknum((double)checkbuf(L,base,nargs,0,"len")->len)); return 1;
}
LFN(f_buf_fill){ UNUSED_SELF;
    Buffer *b=checkbuf(L,base,nargs,0,"fill");
    int off=checkint(L,base,nargs,1,"fill");
    int val=checkint(L,base,nargs,2,"fill");
    int cnt=nargs>=4?checkint(L,base,nargs,3,"fill"):b->len-off;
    bufrange(b,off,cnt);
    memset(b->b+off,val&0xFF,(size_t)cnt);
    return 0;
}
LFN(f_buf_copy){ UNUSED_SELF;
    Buffer *d=checkbuf(L,base,nargs,0,"copy");
    int doff=checkint(L,base,nargs,1,"copy");
    Buffer *s=checkbuf(L,base,nargs,2,"copy");
    int soff=nargs>=4?checkint(L,base,nargs,3,"copy"):0;
    int cnt =nargs>=5?checkint(L,base,nargs,4,"copy"):s->len-soff;
    bufrange(s,soff,cnt); bufrange(d,doff,cnt);
    memmove(d->b+doff,s->b+soff,(size_t)cnt);
    return 0;
}
#define BUFRD(nm,ctype,sz,conv) LFN(nm){ UNUSED_SELF;                      \
    Buffer *b=checkbuf(L,base,nargs,0,"read");                             \
    int off=checkint(L,base,nargs,1,"read");                               \
    bufrange(b,off,sz);                                                    \
    ctype x; memcpy(&x,b->b+off,sz);                                       \
    RET(0,mknum((double)(conv)));  return 1; }
#define BUFWR(nm,ctype,sz,cast) LFN(nm){ UNUSED_SELF;                      \
    Buffer *b=checkbuf(L,base,nargs,0,"write");                            \
    int off=checkint(L,base,nargs,1,"write");                              \
    double d=checknum(L,base,nargs,2,"write");                             \
    bufrange(b,off,sz);                                                    \
    ctype x=(ctype)(cast); memcpy(b->b+off,&x,sz); return 0; }

BUFRD(f_buf_readu8 ,uint8_t ,1,x) BUFRD(f_buf_readi8 ,int8_t ,1,x)
BUFRD(f_buf_readu16,uint16_t,2,x) BUFRD(f_buf_readi16,int16_t,2,x)
BUFRD(f_buf_readu32,uint32_t,4,x) BUFRD(f_buf_readi32,int32_t,4,x)
BUFRD(f_buf_readf32,float   ,4,x) BUFRD(f_buf_readf64,double ,8,x)
BUFWR(f_buf_writeu8 ,uint8_t ,1,(int64_t)d) BUFWR(f_buf_writei8 ,int8_t ,1,(int64_t)d)
BUFWR(f_buf_writeu16,uint16_t,2,(int64_t)d) BUFWR(f_buf_writei16,int16_t,2,(int64_t)d)
BUFWR(f_buf_writeu32,uint32_t,4,(int64_t)d) BUFWR(f_buf_writei32,int32_t,4,(int64_t)d)
BUFWR(f_buf_writef32,float   ,4,d)          BUFWR(f_buf_writef64,double ,8,d)

LFN(f_buf_writestring){ UNUSED_SELF;
    Buffer *b=checkbuf(L,base,nargs,0,"writestring");
    int off=checkint(L,base,nargs,1,"writestring");
    Str *s=checkstr(L,base,nargs,2,"writestring");
    int n=nargs>=4?checkint(L,base,nargs,3,"writestring"):s->len;
    if(n>s->len) n=s->len;
    bufrange(b,off,n);
    memcpy(b->b+off,s->s,(size_t)n);
    return 0;
}
LFN(f_buf_readstring){ UNUSED_SELF;
    Buffer *b=checkbuf(L,base,nargs,0,"readstring");
    int off=checkint(L,base,nargs,1,"readstring");
    int n=nargs>=3?checkint(L,base,nargs,2,"readstring"):b->len-off;
    bufrange(b,off,n);
    RET(0,strv((char*)b->b+off,n)); return 1;
}
LFN(f_buf_writehex){ UNUSED_SELF;
    Buffer *b=checkbuf(L,base,nargs,0,"writehex");
    int off=checkint(L,base,nargs,1,"writehex");
    Str *h=checkstr(L,base,nargs,2,"writehex");
    if(h->len%2) luc_error("buffer.writehex: hex string must have even length");
    int n=h->len/2;
    bufrange(b,off,n);
    for(int i=0;i<n;i++){
        int hi=hexval((unsigned char)h->s[i*2]), lo=hexval((unsigned char)h->s[i*2+1]);
        if(hi<0||lo<0) luc_error("buffer.writehex: invalid hex digit");
        b->b[off+i]=(unsigned char)((hi<<4)|lo);
    }
    RET(0,mknum((double)n)); return 1;
}
LFN(f_buf_readhex){ UNUSED_SELF;
    Buffer *b=checkbuf(L,base,nargs,0,"readhex");
    int off=checkint(L,base,nargs,1,"readhex");
    int n=nargs>=3?checkint(L,base,nargs,2,"readhex"):b->len-off;
    bufrange(b,off,n);
    char *o=(char*)lmalloc((size_t)n*2+1);
    for(int i=0;i<n;i++){
        unsigned char c=b->b[off+i];
        o[i*2]=HEXD[c>>4]; o[i*2+1]=HEXD[c&15];
    }
    RET(0,strv(o,n*2)); free(o); return 1;
}

void lucL_open_buffer(void){
    Table *bf=newlib("buffer"); V.bufferlib=bf;
    reg(bf,"create",f_buf_create); reg(bf,"len",f_buf_len);
    reg(bf,"fill",f_buf_fill);     reg(bf,"copy",f_buf_copy);
    reg(bf,"fromstring",f_buf_fromstring); reg(bf,"tostring",f_buf_tostring);
    reg(bf,"readu8",f_buf_readu8);   reg(bf,"writeu8",f_buf_writeu8);
    reg(bf,"readi8",f_buf_readi8);   reg(bf,"writei8",f_buf_writei8);
    reg(bf,"readu16",f_buf_readu16); reg(bf,"writeu16",f_buf_writeu16);
    reg(bf,"readi16",f_buf_readi16); reg(bf,"writei16",f_buf_writei16);
    reg(bf,"readu32",f_buf_readu32); reg(bf,"writeu32",f_buf_writeu32);
    reg(bf,"readi32",f_buf_readi32); reg(bf,"writei32",f_buf_writei32);
    reg(bf,"readf32",f_buf_readf32); reg(bf,"writef32",f_buf_writef32);
    reg(bf,"readf64",f_buf_readf64); reg(bf,"writef64",f_buf_writef64);
    reg(bf,"readstring",f_buf_readstring); reg(bf,"writestring",f_buf_writestring);
    reg(bf,"readhex",f_buf_readhex);       reg(bf,"writehex",f_buf_writehex);
}


/* ==================== luc_lib_coro.c ==================== */
/*
** luc_lib_coro.c -- coroutine library + task library
*/
/* ---- coroutine ------------------------------------------------------- */
LFN(f_co_create){ UNUSED_SELF;
    Value f=AR(0);
    if(f.t!=LT_FUNC&&f.t!=LT_CFUNC)
        luc_error("bad argument #1 to 'create' (function expected, got %s)",type_name(f));
    LucState *co=state_new(64);
    co->stack[0]=f;
    co->cursource=L->cursource;
    RET(0,mkobj(LT_CORO,co)); return 1;
}
LFN(f_co_yield){ UNUSED_SELF;
    if(!g_yp || g_yp->co!=L) luc_error("attempt to yield from outside a coroutine");
    L->yieldbase=base; L->nyield=nargs;
    L->status=CO_SUSPENDED;
    longjmp(g_yp->jb,1);
    return 0;                      /* not reached */
}
LFN(f_co_resume){ UNUSED_SELF;
    Value cv=AR(0);
    if(cv.t!=LT_CORO) luc_error("bad argument #1 to 'resume' (thread expected, got %s)",type_name(cv));
    LucState *co=AS_CO(cv);
    Value args[32]; int na=nargs-1; if(na<0) na=0; if(na>32) na=32;
    for(int i=0;i<na;i++) args[i]=L->stack[base+1+i];
    Value res[32]; int nres=0;
    if(co_resume(co,args,na,res,&nres)){ RET(0,mkbool(0)); RET(1,V.errval); return 2; }
    RET(0,mkbool(1));
    for(int i=0;i<nres;i++) RET(1+i,res[i]);
    return nres+1;
}
LFN(f_co_wrapped){
    LucState *co=AS_CO(self->up[0]);
    Value args[32]; int na=nargs>32?32:nargs;
    for(int i=0;i<na;i++) args[i]=L->stack[base+i];
    Value res[32]; int nres=0;
    if(co_resume(co,args,na,res,&nres)) luc_throw(V.errval);
    for(int i=0;i<nres;i++) RET(i,res[i]);
    return nres;
}
LFN(f_co_wrap){ UNUSED_SELF;
    Value f=AR(0);
    if(f.t!=LT_FUNC&&f.t!=LT_CFUNC) luc_error("bad argument #1 to 'wrap' (function expected)");
    LucState *co=state_new(64);
    co->stack[0]=f; co->cursource=L->cursource;
    CFunc *c=cfunc_new(f_co_wrapped,"wrapped",1);
    c->up[0]=mkobj(LT_CORO,co);
    RET(0,mkobj(LT_CFUNC,c)); return 1;
}
LFN(f_co_status){ UNUSED_SELF;
    Value cv=AR(0);
    if(cv.t!=LT_CORO) luc_error("bad argument #1 to 'status' (thread expected)");
    LucState *co=AS_CO(cv);
    const char *s = co==V.cur? "running" :
                    (co->status==CO_DEAD? "dead" :
                    (co->status==CO_NORMAL? "normal" : "suspended"));
    RET(0,cstrv(s)); return 1;
}
LFN(f_co_running){ UNUSED_SELF; (void)nargs;
    RET(0,mkobj(LT_CORO,V.cur));
    RET(1,mkbool(V.cur==V.mainco));
    return 2;
}
LFN(f_co_isyieldable){ UNUSED_SELF; (void)nargs;
    RET(0,mkbool(g_yp && g_yp->co==L)); return 1;
}

/* ---- task ------------------------------------------------------------ */
/* trampoline: up[0]=function, up[1..] = captured arguments */
LFN(f_task_trampoline){
    int n=self->nup-1;
    ensure_stack(L,base+n+8);
    L->stack[base]=self->up[0];
    for(int i=0;i<n;i++) L->stack[base+1+i]=self->up[1+i];
    return vm_call(L,base,n,-1);
}
static LucState *make_task(LucState *L,int base,int nargs,int firstarg){
    Value f=L->stack[base+firstarg];
    if(f.t!=LT_FUNC&&f.t!=LT_CFUNC)
        luc_error("bad argument #%d to 'task' (function expected, got %s)",
                  firstarg+1,type_name(f));
    int na=nargs-firstarg-1; if(na<0) na=0;
    CFunc *tr=cfunc_new(f_task_trampoline,"task",na+1);
    tr->up[0]=f;
    for(int i=0;i<na;i++) tr->up[1+i]=L->stack[base+firstarg+1+i];
    LucState *co=state_new(64);
    co->stack[0]=mkobj(LT_CFUNC,tr);
    co->cursource=L->cursource;
    return co;
}
LFN(f_task_wait){ UNUSED_SELF;
    double n = nargs>=1? checknum(L,base,nargs,0,"wait") : 0;
    if(n<0) n=0;
    if(!g_yp || g_yp->co!=L){        /* main thread: block */
        double t0=luc_now(); luc_sleep(n);
        RET(0,mknum(luc_now()-t0)); return 1;
    }
    L->waketime=n;
    sched_add(L,luc_now()+n);
    L->yieldbase=base; L->nyield=0;
    L->status=CO_SUSPENDED;
    longjmp(g_yp->jb,1);
    return 0;
}
LFN(f_task_spawn){ UNUSED_SELF;
    LucState *co=make_task(L,base,nargs,0);
    Value cv=mkobj(LT_CORO,co);
    RET(0,cv);                                  /* root before resuming */
    Value res[32]; int nres=0;
    if(co_resume(co,NULL,0,res,&nres)){
        Str *s=tostr(V.errval);
        fprintf(stderr,"luc: error in task.spawn: %s\n",s->s);
    }
    RET(0,cv); return 1;
}
LFN(f_task_defer){ UNUSED_SELF;
    LucState *co=make_task(L,base,nargs,0);
    Value cv=mkobj(LT_CORO,co);
    RET(0,cv);
    co->waketime=0;
    sched_add(co,luc_now());
    RET(0,cv); return 1;
}
LFN(f_task_delay){ UNUSED_SELF;
    double d=checknum(L,base,nargs,0,"delay");
    if(d<0) d=0;
    LucState *co=make_task(L,base,nargs,1);
    Value cv=mkobj(LT_CORO,co);
    RET(0,cv);
    co->waketime=d;
    sched_add(co,luc_now()+d);
    RET(0,cv); return 1;
}
LFN(f_task_cancel){ UNUSED_SELF;
    Value cv=AR(0);
    if(cv.t!=LT_CORO) luc_error("bad argument #1 to 'cancel' (thread expected)");
    LucState *co=AS_CO(cv);
    if(co==V.cur) luc_error("cannot cancel the running thread");
    co->status=CO_DEAD;
    for(int i=0;i<V.nsched;i++) if(V.sched[i].co==co){ sched_remove(i); break; }
    return 0;
}

void lucL_open_coro(void){
    Table *c=newlib("coroutine");
    reg(c,"create",f_co_create); reg(c,"resume",f_co_resume);
    reg(c,"yield",f_co_yield);   reg(c,"status",f_co_status);
    reg(c,"wrap",f_co_wrap);     reg(c,"running",f_co_running);
    reg(c,"isyieldable",f_co_isyieldable);
    /* --- task --- */
    Table *tk=newlib("task");
    reg(tk,"wait",f_task_wait);   reg(tk,"spawn",f_task_spawn);
    reg(tk,"delay",f_task_delay); reg(tk,"defer",f_task_defer);
    reg(tk,"cancel",f_task_cancel);
}


/* ==================== luc_lib_io.c ==================== */
/*
** luc_lib_io.c -- io library + file methods (io.popen moved here)
*/
/* ---- io -------------------------------------------------------------- */
static FileH *checkfile(LucState *L,int base,int nargs,int i,const char *fn){
    Value v=AR(i);
    if(v.t!=LT_FILE) luc_error("bad argument #%d to '%s' (file expected, got %s)",
                               i+1,fn,type_name(v));
    FileH *h=AS_FILE(v);
    if(h->closed || !h->f) luc_error("attempt to use a closed file");
    return h;
}
static Str *read_line_str(FILE *f,int keepnl){
    size_t cap=128,len=0; char *b=(char*)lmalloc(cap); int c=EOF;
    while((c=fgetc(f))!=EOF){
        if(len+2>cap){ cap*=2; b=(char*)lrealloc(b,cap); }
        if(c=='\n'){ if(keepnl) b[len++]=(char)c; break; }
        b[len++]=(char)c;
    }
    if(c==EOF && len==0){ free(b); return NULL; }
    Str *s=str_new(b,(int)len); free(b); return s;
}
static Str *read_all_str(FILE *f){
    size_t cap=1024,len=0; char *b=(char*)lmalloc(cap); size_t n;
    for(;;){
        if(len+512>cap){ cap*=2; b=(char*)lrealloc(b,cap); }
        n=fread(b+len,1,cap-len,f);
        len+=n;
        if(n==0) break;
    }
    Str *s=str_new(b,(int)len); free(b); return s;
}
static Str *read_count_str(FILE *f,int count){
    if(count<=0){ int c=fgetc(f); if(c==EOF) return NULL; ungetc(c,f); return str_new("",0); }
    char *b=(char*)lmalloc((size_t)count+1);
    size_t n=fread(b,1,(size_t)count,f);
    if(n==0){ free(b); return NULL; }
    Str *s=str_new(b,(int)n); free(b); return s;
}
/* read according to format arguments starting at argument index `first` */
static int io_read_aux(LucState *L,int base,int nargs,FILE *f,int first){
    int out=0;
    if(first>=nargs){
        Str *s=read_line_str(f,0);
        if(s) RET(0,mkobj(LT_STR,s)); else RET(0,NIL);
        return 1;
    }
    for(int i=first;i<nargs;i++){
        Value fmt=AR(i);
        if(fmt.t==LT_NUM){
            Str *s=read_count_str(f,(int)fmt.u.n);
            if(s) RET(out,mkobj(LT_STR,s)); else RET(out,NIL);
            out++; continue;
        }
        Str *fs=checkstr(L,base,nargs,i,"read");
        const char *p=fs->s;
        if(*p=='*') p++;
        switch(*p){
            case 'l': case 'L': {
                Str *s=read_line_str(f,*p=='L');
                if(s) RET(out,mkobj(LT_STR,s)); else RET(out,NIL);
                break; }
            case 'a': RET(out,mkobj(LT_STR,read_all_str(f))); break;
            case 'n': {
                double d;
                if(fscanf(f,"%lf",&d)==1) RET(out,mknum(d)); else RET(out,NIL);
                break; }
            default: luc_error("bad argument #%d to 'read' (invalid format)",i+1);
        }
        out++;
    }
    return out;
}
LFN(f_io_write){ UNUSED_SELF;
    for(int i=0;i<nargs;i++){
        Value v=L->stack[base+i];
        if(v.t!=LT_STR && v.t!=LT_NUM)
            luc_error("bad argument #%d to 'write' (string expected, got %s)",i+1,type_name(v));
        Str *s=tostr(v);
        fwrite(s->s,1,(size_t)s->len,stdout);
    }
    fflush(stdout);              /* so '\r' progress lines show up at once */
    return 0;
}
LFN(f_io_replace){ UNUSED_SELF;
    fputs("\r\033[2K",stdout);    /* return home and erase the current line */
    for(int i=0;i<nargs;i++){
        Value v=L->stack[base+i];
        if(v.t!=LT_STR && v.t!=LT_NUM)
            luc_error("bad argument #%d to 'replace' (string expected, got %s)",i+1,type_name(v));
        Str *s=tostr(v);
        fwrite(s->s,1,(size_t)s->len,stdout);
    }
    fflush(stdout);
    return 0;
}
LFN(f_io_clearline){ UNUSED_SELF; (void)L; (void)base; (void)nargs;
    fputs("\r\033[2K",stdout);
    fflush(stdout);
    return 0;
}
LFN(f_io_eraseline){ UNUSED_SELF; (void)L; (void)base; (void)nargs;
    fputs("\033[1A\r\033[2K",stdout); /* move up, return home, erase */
    fflush(stdout);
    return 0;
}
LFN(f_io_clear){ UNUSED_SELF; (void)L; (void)base; (void)nargs;
    fputs("\033[2J\033[H",stdout); /* erase screen and move cursor home */
    fflush(stdout);
    return 0;
}
LFN(f_io_read){ UNUSED_SELF; return io_read_aux(L,base,nargs,stdin,0); }
LFN(f_io_open){ UNUSED_SELF;
    Str *path=checkstr(L,base,nargs,0,"open");
    const char *mode = nargs>=2? checkstr(L,base,nargs,1,"open")->s : "r";
    FILE *f=fopen(path->s,mode);
    if(!f){ RET(0,NIL); RET(1,cstrv("cannot open file")); RET(2,mknum(2)); return 3; }
    RET(0,mkobj(LT_FILE,file_new(f,0)));
    return 1;
}
LFN(f_io_close){ UNUSED_SELF;
    if(nargs==0){ return 0; }
    FileH *h=checkfile(L,base,nargs,0,"close");
    if(!h->isstd) fclose(h->f);
    h->closed=1; h->f=NULL;
    RET(0,mkbool(1)); return 1;
}
LFN(f_file_write){ UNUSED_SELF;
    FileH *h=checkfile(L,base,nargs,0,"write");
    for(int i=1;i<nargs;i++){
        Str *s=tostr(L->stack[base+i]);
        fwrite(s->s,1,(size_t)s->len,h->f);
    }
    RET(0,AR(0)); return 1;
}
LFN(f_file_read){ UNUSED_SELF;
    FileH *h=checkfile(L,base,nargs,0,"read");
    return io_read_aux(L,base,nargs,h->f,1);
}
LFN(f_file_flush){ UNUSED_SELF;
    FileH *h=checkfile(L,base,nargs,0,"flush"); fflush(h->f); RET(0,AR(0)); return 1;
}
LFN(f_file_seek){ UNUSED_SELF;
    FileH *h=checkfile(L,base,nargs,0,"seek");
    const char *wh = nargs>=2? checkstr(L,base,nargs,1,"seek")->s : "cur";
    long off = nargs>=3? (long)checknum(L,base,nargs,2,"seek") : 0;
    int w = strcmp(wh,"set")==0?SEEK_SET : (strcmp(wh,"end")==0?SEEK_END:SEEK_CUR);
    if(fseek(h->f,off,w)!=0){ RET(0,NIL); RET(1,cstrv("seek failed")); return 2; }
    RET(0,mknum((double)ftell(h->f))); return 1;
}
LFN(f_lines_iter){
    Value fv=self->up[0];
    FileH *h=AS_FILE(fv);
    if(h->closed||!h->f){ RET(0,NIL); return 1; }
    Str *s=read_line_str(h->f,0);
    if(!s){
        if(!h->isstd && (int)self->up[1].u.n){ fclose(h->f); h->f=NULL; h->closed=1; }
        RET(0,NIL); return 1;
    }
    RET(0,mkobj(LT_STR,s)); return 1;
}
LFN(f_io_lines){ UNUSED_SELF;
    FileH *h; int autoclose=0;
    if(nargs==0 || AR(0).t==LT_NIL){ h=file_new(stdin,1); }
    else {
        Str *p=checkstr(L,base,nargs,0,"lines");
        FILE *f=fopen(p->s,"r");
        if(!f) luc_error("cannot open '%s'",p->s);
        h=file_new(f,0); autoclose=1;
    }
    CFunc *c=cfunc_new(f_lines_iter,"lines",2);
    c->up[0]=mkobj(LT_FILE,h);
    c->up[1]=mknum(autoclose);
    RET(0,mkobj(LT_CFUNC,c));
    return 1;
}
LFN(f_file_lines){ UNUSED_SELF;
    FileH *h=checkfile(L,base,nargs,0,"lines");
    CFunc *c=cfunc_new(f_lines_iter,"lines",2);
    c->up[0]=AR(0); c->up[1]=mknum(0); (void)h;
    RET(0,mkobj(LT_CFUNC,c));
    return 1;
}
LFN(f_io_popen){ UNUSED_SELF;
    Str *cmd=checkstr(L,base,nargs,0,"popen");
    const char *mode=nargs>=2?checkstr(L,base,nargs,1,"popen")->s:"r";
#if defined(_WIN32)
    FILE *fp=_popen(cmd->s,mode);
#else
    FILE *fp=popen(cmd->s,mode);
#endif
    if(!fp){ RET(0,NIL); RET(1,cstrv("cannot start process")); return 2; }
    FileH *h=file_new(fp,0);
    h->ispipe=1;
    RET(0,mkobj(LT_FILE,h)); return 1;
}

/* ==========================================================================
** 15. library registration
** ========================================================================== */

void lucL_open_io(void){
    Table *io=newlib("io");
    reg(io,"write",f_io_write); reg(io,"read",f_io_read);
    reg(io,"replace",f_io_replace); reg(io,"clearline",f_io_clearline);
    reg(io,"eraseline",f_io_eraseline); reg(io,"clear",f_io_clear);
    reg(io,"open",f_io_open);   reg(io,"close",f_io_close);
    reg(io,"lines",f_io_lines);
    reg(io,"popen",f_io_popen);
    V.filelib=tab_new(0);
    reg(V.filelib,"read",f_file_read);   reg(V.filelib,"write",f_file_write);
    reg(V.filelib,"close",f_io_close);   reg(V.filelib,"lines",f_file_lines);
    reg(V.filelib,"seek",f_file_seek);   reg(V.filelib,"flush",f_file_flush);
    tab_set(io,cstrv("stdout"),mkobj(LT_FILE,file_new(stdout,1)));
    tab_set(io,cstrv("stderr"),mkobj(LT_FILE,file_new(stderr,1)));
    tab_set(io,cstrv("stdin"), mkobj(LT_FILE,file_new(stdin ,1)));
}


/* ==================== luc_lib_json.c ==================== */
/*
** luc_lib_json.c -- JSON module (loaded with require "json")
*/
/* ---- JSON ---------------------------------------------------------------- */
typedef struct { char *b; size_t len,cap; } SBuf;
static void sb_init(SBuf *s){ s->cap=256; s->len=0; s->b=(char*)lmalloc(s->cap); }
static void sb_put(SBuf *s,const char *p,size_t n){
    if(s->len+n+1>s->cap){ while(s->len+n+1>s->cap) s->cap*=2;
                           s->b=(char*)lrealloc(s->b,s->cap); }
    memcpy(s->b+s->len,p,n); s->len+=n; s->b[s->len]=0;
}
static void sb_puts(SBuf *s,const char *p){ sb_put(s,p,strlen(p)); }
static void sb_putc(SBuf *s,char c){ sb_put(s,&c,1); }

static void json_str(SBuf *o,Str *s){
    sb_putc(o,'"');
    for(int i=0;i<s->len;i++){
        unsigned char c=(unsigned char)s->s[i];
        switch(c){
            case '"':  sb_puts(o,"\""); break;
            case '\\': sb_puts(o,"\\\\"); break;
            case '\n': sb_puts(o,"\\n"); break;
            case '\r': sb_puts(o,"\\r"); break;
            case '\t': sb_puts(o,"\\t"); break;
            case '\b': sb_puts(o,"\\b"); break;
            case '\f': sb_puts(o,"\\f"); break;
            default:
                if(c<0x20){ char u[8]; snprintf(u,sizeof u,"\\u%04x",c); sb_puts(o,u); }
                else sb_putc(o,(char)c);
        }
    }
    sb_putc(o,'"');
}
static void json_indent(SBuf *o,int pretty,int depth){
    if(!pretty) return;
    sb_putc(o,'\n');
    for(int i=0;i<depth;i++) sb_puts(o,"  ");
}
static int table_is_seq(Table *t){ return t->alen>0 && t->ecount==0; }

static void json_encode_val(Value v,SBuf *o,int pretty,int depth){
    char nb[64];
    if(depth>100) luc_error("json.encode: nested too deeply");
    switch(v.t){
        case LT_NIL:  sb_puts(o,"null"); return;
        case LT_BOOL: sb_puts(o,v.u.b?"true":"false"); return;
        case LT_NUM:
            if(v.u.n!=v.u.n||v.u.n==HUGE_VAL||v.u.n==-HUGE_VAL)
                luc_error("json.encode: cannot encode nan/inf");
            num2str(v.u.n,nb,sizeof nb); sb_puts(o,nb); return;
        case LT_STR: json_str(o,AS_STR(v)); return;
        case LT_LIST: case LT_TABLE: break;
        default: luc_error("json.encode: cannot encode %s",type_name(v));
    }
    Table *t=AS_TAB(v);
    if(v.t==LT_LIST || table_is_seq(t)){
        int n=(v.t==LT_LIST)?t->alen:tab_len(t);
        if(n==0){ sb_puts(o,"[]"); return; }
        sb_putc(o,'[');
        for(int i=1;i<=n;i++){
            if(i>1) sb_putc(o,',');
            json_indent(o,pretty,depth+1);
            json_encode_val(tab_get(t,mknum((double)i)),o,pretty,depth+1);
        }
        json_indent(o,pretty,depth); sb_putc(o,']');
        return;
    }
    int cap=16,n=0; Str **keys=(Str**)lmalloc(sizeof(Str*)*(size_t)cap);
    Value k=NIL,val;
    while(tab_next(t,k,&k,&val)){
        if(k.t!=LT_STR && k.t!=LT_NUM) continue;
        if(n==cap){ cap*=2; keys=(Str**)lrealloc(keys,sizeof(Str*)*(size_t)cap); }
        keys[n++]=tostr(k);
    }
    for(int i=1;i<n;i++){
        Str *key=keys[i]; int j=i-1;
        while(j>=0 && strcmp(keys[j]->s,key->s)>0){ keys[j+1]=keys[j]; j--; }
        keys[j+1]=key;
    }
    if(n==0){ free(keys); sb_puts(o,"{}"); return; }
    sb_putc(o,'{');
    for(int i=0;i<n;i++){
        if(i) sb_putc(o,',');
        json_indent(o,pretty,depth+1);
        json_str(o,keys[i]);
        sb_putc(o,':'); if(pretty) sb_putc(o,' ');
        json_encode_val(tab_get(t,mkobj(LT_STR,keys[i])),o,pretty,depth+1);
    }
    json_indent(o,pretty,depth); sb_putc(o,'}');
    free(keys);
}

LFN(f_json_encode){ UNUSED_SELF;
    int pretty=nargs>=2 && truthy(AR(1));
    SBuf o; sb_init(&o);
    json_encode_val(AR(0),&o,pretty,0);
    RET(0,strv(o.b,(int)o.len));
    free(o.b);
    return 1;
}

typedef struct { const char *p,*end; int depth; } JParse;
static Value json_parse(JParse *j);

static Value json_parse_string(JParse *j){
    j->p++;
    SBuf o; sb_init(&o);
    while(j->p<j->end && *j->p!='"'){
        if(*j->p=='\\'){
            j->p++;
            if(j->p>=j->end) break;
            char c=*j->p++;
            switch(c){
                case 'n': sb_putc(&o,'\n'); break; case 't': sb_putc(&o,'\t'); break;
                case 'r': sb_putc(&o,'\r'); break; case 'b': sb_putc(&o,'\b'); break;
                case 'f': sb_putc(&o,'\f'); break; case '/': sb_putc(&o,'/');   break;
                case '"': sb_putc(&o,'"');   break; case '\\':sb_putc(&o,'\\'); break;
                case 'u': {
                    unsigned cp=0;
                    for(int i=0;i<4 && j->p<j->end;i++){
                        int h=hexval((unsigned char)*j->p++);
                        if(h<0){ free(o.b); luc_error("json.decode: bad \\u escape"); }
                        cp=cp*16+(unsigned)h;
                    }
                    if(cp<0x80) sb_putc(&o,(char)cp);
                    else if(cp<0x800){ sb_putc(&o,(char)(0xC0|(cp>>6))); sb_putc(&o,(char)(0x80|(cp&0x3F))); }
                    else { sb_putc(&o,(char)(0xE0|(cp>>12))); sb_putc(&o,(char)(0x80|((cp>>6)&0x3F))); sb_putc(&o,(char)(0x80|(cp&0x3F))); }
                    break; }
                default: free(o.b); luc_error("json.decode: bad escape");
            }
        } else sb_putc(&o,*j->p++);
    }
    if(j->p>=j->end){ free(o.b); luc_error("json.decode: unterminated string"); }
    j->p++;
    Value v=strv(o.b,(int)o.len); free(o.b);
    return v;
}

static void jskip(JParse *j){
    while(j->p<j->end && (*j->p==' '||*j->p=='\t'||*j->p=='\n'||*j->p=='\r')) j->p++;
}

static Value json_parse(JParse *j){
    jskip(j);
    if(j->p>=j->end) luc_error("json.decode: unexpected end");
    if(j->depth++>200) luc_error("json.decode: nested too deeply");
    Value out=NIL;
    char c=*j->p;
    if(c=='{'){
        Table *t=tab_new(0); out=mkobj(LT_TABLE,t);
        j->p++; jskip(j);
        if(j->p<j->end && *j->p=='}'){ j->p++; j->depth--; return out; }
        for(;;){
            jskip(j);
            if(j->p>=j->end||*j->p!='"') luc_error("json.decode: expected key");
            Value k=json_parse_string(j);
            jskip(j);
            if(j->p>=j->end||*j->p!=':') luc_error("json.decode: expected ':'");
            j->p++;
            Value v=json_parse(j);
            if(v.t!=LT_NIL) tab_set(t,k,v);
            jskip(j);
            if(j->p<j->end&&*j->p==','){ j->p++; continue; }
            if(j->p<j->end&&*j->p=='}'){ j->p++; break; }
            luc_error("json.decode: expected ',' or '}'");
        }
    } else if(c=='['){
        Table *t=tab_new(1); out=mkobj(LT_LIST,t);
        j->p++; jskip(j);
        if(j->p<j->end&&*j->p==']'){ j->p++; j->depth--; return out; }
        for(;;){
            list_push(t,json_parse(j));
            jskip(j);
            if(j->p<j->end&&*j->p==','){ j->p++; continue; }
            if(j->p<j->end&&*j->p==']'){ j->p++; break; }
            luc_error("json.decode: expected ',' or ']'");
        }
    } else if(c=='"'){
        out=json_parse_string(j);
    } else if(!strncmp(j->p,"true",4)&&j->end-j->p>=4){ out=mkbool(1); j->p+=4; }
      else if(!strncmp(j->p,"false",5)&&j->end-j->p>=5){ out=mkbool(0); j->p+=5; }
      else if(!strncmp(j->p,"null",4)&&j->end-j->p>=4){ out=NIL; j->p+=4; }
      else {
        char *endp; double d=strtod(j->p,&endp);
        if(endp==j->p) luc_error("json.decode: unexpected char '%c'",c);
        out=mknum(d); j->p=endp;
    }
    j->depth--;
    return out;
}

LFN(f_json_decode){ UNUSED_SELF;
    Str *s=checkstr(L,base,nargs,0,"decode");
    JParse j; j.p=s->s; j.end=s->s+s->len; j.depth=0;
    Value v=json_parse(&j);
    jskip(&j);
    if(j.p!=j.end) luc_error("json.decode: trailing garbage");
    RET(0,v); return 1;
}

Value lucL_json_module(void){
    Table *t=tab_new(0);
    tab_set(t,cstrv("encode"),mkobj(LT_CFUNC,cfunc_new(f_json_encode,"encode",0)));
    tab_set(t,cstrv("decode"),mkobj(LT_CFUNC,cfunc_new(f_json_decode,"decode",0)));
    return mkobj(LT_TABLE,t);
}


/* ==================== luc_lib_list.c ==================== */
/*
** luc_lib_list.c -- list methods + table library (they share sort/concat)
*/
/* ---- list methods ---------------------------------------------------- */
LFN(f_list_append){ UNUSED_SELF;
    Table *t=checktab(L,base,nargs,0,"append");
    for(int i=1;i<nargs;i++) list_push(t,L->stack[base+i]);
    RET(0,AR(0)); return 1;
}
LFN(f_list_pop){ UNUSED_SELF;
    Table *t=checktab(L,base,nargs,0,"pop");
    int pos = nargs>=2? checkint(L,base,nargs,1,"pop") : t->alen;
    RET(0,list_removeat(t,pos)); return 1;
}
LFN(f_list_insert){ UNUSED_SELF;
    Table *t=checktab(L,base,nargs,0,"insert");
    if(nargs>=3) list_insert(t,checkint(L,base,nargs,1,"insert"),AR(2));
    else list_push(t,AR(1));
    return 0;
}
LFN(f_list_remove){ UNUSED_SELF;
    Table *t=checktab(L,base,nargs,0,"remove");
    int pos = nargs>=2? checkint(L,base,nargs,1,"remove") : t->alen;
    RET(0,list_removeat(t,pos)); return 1;
}
LFN(f_list_len){ UNUSED_SELF;
    Table *t=checktab(L,base,nargs,0,"len");
    RET(0,mknum((double)(t->o.type==LT_LIST? t->alen : tab_len(t)))); return 1;
}
LFN(f_list_contains){ UNUSED_SELF;
    Table *t=checktab(L,base,nargs,0,"contains");
    RET(0,mkbool(vm_in(AR(1),AR(0)))); (void)t; return 1;
}
LFN(f_list_indexof){ UNUSED_SELF;
    Table *t=checktab(L,base,nargs,0,"indexof");
    for(int i=0;i<t->alen;i++)
        if(val_rawequal(t->arr[i],AR(1))){ RET(0,mknum((double)(i+1))); return 1; }
    RET(0,NIL); return 1;
}
LFN(f_list_clear){ UNUSED_SELF;
    Table *t=checktab(L,base,nargs,0,"clear");
    for(int i=0;i<t->alen;i++) t->arr[i]=NIL;
    t->alen=0; return 0;
}
LFN(f_list_extend){ UNUSED_SELF;
    Table *t=checktab(L,base,nargs,0,"extend");
    Table *o=checktab(L,base,nargs,1,"extend");
    int n=o->o.type==LT_LIST?o->alen:tab_len(o);
    for(int i=1;i<=n;i++) list_push(t,tab_get(o,mknum((double)i)));
    RET(0,AR(0)); return 1;
}
LFN(f_list_reverse){ UNUSED_SELF;
    Table *t=checktab(L,base,nargs,0,"reverse");
    for(int i=0,j=t->alen-1;i<j;i++,j--){ Value tmp=t->arr[i]; t->arr[i]=t->arr[j]; t->arr[j]=tmp; }
    return 0;
}
static int sort_less(LucState *L,int scratch,Value cmp,Value a,Value b){
    if(cmp.t==LT_NIL) return vm_lessthan(a,b,0);
    ensure_stack(L,scratch+8);
    L->stack[scratch]=cmp; L->stack[scratch+1]=a; L->stack[scratch+2]=b;
    vm_call(L,scratch,2,1);
    return truthy(L->stack[scratch]);
}
LFN(f_list_sort){ UNUSED_SELF;
    Table *t=checktab(L,base,nargs,0,"sort");
    Value cmp=AR(1);
    int n=t->o.type==LT_LIST? t->alen : tab_len(t);
    int scratch=base+nargs+2;
    for(int i=1;i<n;i++){                       /* insertion sort (stable) */
        Value key=t->arr[i]; int j=i-1;
        while(j>=0 && sort_less(L,scratch,cmp,key,t->arr[j])){ t->arr[j+1]=t->arr[j]; j--; }
        t->arr[j+1]=key;
    }
    return 0;
}
LFN(f_list_tostring){ UNUSED_SELF;
    RET(0,mkobj(LT_STR,tostr(AR(0)))); return 1;
}

/* ---- table ----------------------------------------------------------- */
LFN(f_tbl_insert){ UNUSED_SELF;
    Table *t=checktab(L,base,nargs,0,"insert");
    if(nargs>=3){
        int pos=checkint(L,base,nargs,1,"insert");
        int n=t->o.type==LT_LIST?t->alen:tab_len(t);
        if(t->o.type==LT_LIST) list_insert(t,pos,AR(2));
        else {
            for(int i=n;i>=pos;i--) tab_set(t,mknum((double)(i+1)),tab_get(t,mknum((double)i)));
            tab_set(t,mknum((double)pos),AR(2));
        }
    } else {
        int n=t->o.type==LT_LIST?t->alen:tab_len(t);
        tab_set(t,mknum((double)(n+1)),AR(1));
    }
    return 0;
}
LFN(f_tbl_remove){ UNUSED_SELF;
    Table *t=checktab(L,base,nargs,0,"remove");
    int n=t->o.type==LT_LIST?t->alen:tab_len(t);
    int pos = nargs>=2? checkint(L,base,nargs,1,"remove") : n;
    if(n==0){ RET(0,NIL); return 1; }
    if(t->o.type==LT_LIST){ RET(0,list_removeat(t,pos)); return 1; }
    Value v=tab_get(t,mknum((double)pos));
    for(int i=pos;i<n;i++) tab_set(t,mknum((double)i),tab_get(t,mknum((double)(i+1))));
    tab_set(t,mknum((double)n),NIL);
    RET(0,v); return 1;
}
LFN(f_tbl_concat){ UNUSED_SELF;
    Table *t=checktab(L,base,nargs,0,"concat");
    Str *sep = nargs>=2 && AR(1).t!=LT_NIL? checkstr(L,base,nargs,1,"concat") : NULL;
    int n=t->o.type==LT_LIST?t->alen:tab_len(t);
    int i = nargs>=3? checkint(L,base,nargs,2,"concat") : 1;
    int j = nargs>=4? checkint(L,base,nargs,3,"concat") : n;
    size_t cap=64,len=0; char *b=(char*)lmalloc(cap);
    for(int x=i;x<=j;x++){
        Value v=tab_get(t,mknum((double)x));
        if(v.t!=LT_STR&&v.t!=LT_NUM) luc_error("invalid value (at index %d) in table.concat",x);
        Str *s=tostr(v);
        size_t need=len+(size_t)s->len+(sep?(size_t)sep->len:0)+1;
        if(need>cap){ while(need>cap) cap*=2; b=(char*)lrealloc(b,cap); }
        memcpy(b+len,s->s,(size_t)s->len); len+=(size_t)s->len;
        if(sep && x<j){ memcpy(b+len,sep->s,(size_t)sep->len); len+=(size_t)sep->len; }
    }
    RET(0,strv(b,(int)len)); free(b); return 1;
}
LFN(f_tbl_move){ UNUSED_SELF;
    Table *a1=checktab(L,base,nargs,0,"move");
    int f=checkint(L,base,nargs,1,"move");
    int e=checkint(L,base,nargs,2,"move");
    int t=checkint(L,base,nargs,3,"move");
    Table *a2=nargs>=5?checktab(L,base,nargs,4,"move"):a1;
    if(e>=f){
        if(t>e||t<=f||a1!=a2)
            for(int i=0;i<=e-f;i++)
                tab_set(a2,mknum((double)(t+i)),tab_get(a1,mknum((double)(f+i))));
        else
            for(int i=e-f;i>=0;i--)
                tab_set(a2,mknum((double)(t+i)),tab_get(a1,mknum((double)(f+i))));
    }
    RET(0,nargs>=5?AR(4):AR(0)); return 1;
}

void lucL_open_list(void){
    /* list methods double as the method table for [] values */
    Table *li=newlib("list"); V.listmeta=li;
    reg(li,"append",f_list_append);   reg(li,"pop",f_list_pop);
    reg(li,"insert",f_list_insert);   reg(li,"remove",f_list_remove);
    reg(li,"len",f_list_len);         reg(li,"contains",f_list_contains);
    reg(li,"indexof",f_list_indexof); reg(li,"clear",f_list_clear);
    reg(li,"extend",f_list_extend);   reg(li,"reverse",f_list_reverse);
    reg(li,"sort",f_list_sort);       reg(li,"concat",f_tbl_concat);
    reg(li,"tostring",f_list_tostring);
    /* --- table --- */
    Table *t=newlib("table");
    reg(t,"insert",f_tbl_insert);  reg(t,"remove",f_tbl_remove);
    reg(t,"concat",f_tbl_concat);  reg(t,"unpack",f_unpack);
    reg(t,"sort",f_list_sort);
    reg(t,"move",f_tbl_move);
}


/* ==================== luc_lib_math.c ==================== */
/*
** luc_lib_math.c -- math library + bit32 (+ RNG seeding, moved from luc_init)
*/
/* ---- math ------------------------------------------------------------ */
static uint64_t rngstate=0x2545F4914F6CDD1DULL;
static double rnd(void){
    rngstate^=rngstate>>12; rngstate^=rngstate<<25; rngstate^=rngstate>>27;
    return (double)((rngstate*2685821657736338717ULL)>>11)/9007199254740992.0;
}
#define MATH1(nm,expr) LFN(nm){ UNUSED_SELF; double x=checknum(L,base,nargs,0,"math"); RET(0,mknum(expr)); return 1; }
MATH1(f_m_floor,floor(x)) MATH1(f_m_ceil,ceil(x)) MATH1(f_m_sqrt,sqrt(x))
MATH1(f_m_abs,fabs(x))    MATH1(f_m_sin,sin(x))   MATH1(f_m_cos,cos(x))
MATH1(f_m_tan,tan(x))     MATH1(f_m_asin,asin(x)) MATH1(f_m_acos,acos(x))
MATH1(f_m_exp,exp(x))     MATH1(f_m_atan,atan(x))
LFN(f_m_log){ UNUSED_SELF;
    double x=checknum(L,base,nargs,0,"log");
    if(nargs>=2){ double b=checknum(L,base,nargs,1,"log"); RET(0,mknum(log(x)/log(b))); }
    else RET(0,mknum(log(x)));
    return 1;
}
LFN(f_m_pow){ UNUSED_SELF;
    RET(0,mknum(pow(checknum(L,base,nargs,0,"pow"),checknum(L,base,nargs,1,"pow")))); return 1; }
LFN(f_m_fmod){ UNUSED_SELF;
    RET(0,mknum(fmod(checknum(L,base,nargs,0,"fmod"),checknum(L,base,nargs,1,"fmod")))); return 1; }
LFN(f_m_modf){ UNUSED_SELF;
    double ip; double fp=modf(checknum(L,base,nargs,0,"modf"),&ip);
    RET(0,mknum(ip)); RET(1,mknum(fp)); return 2; }
LFN(f_m_max){ UNUSED_SELF;
    double m=checknum(L,base,nargs,0,"max");
    for(int i=1;i<nargs;i++){ double d=checknum(L,base,nargs,i,"max"); if(d>m) m=d; }
    RET(0,mknum(m)); return 1; }
LFN(f_m_min){ UNUSED_SELF;
    double m=checknum(L,base,nargs,0,"min");
    for(int i=1;i<nargs;i++){ double d=checknum(L,base,nargs,i,"min"); if(d<m) m=d; }
    RET(0,mknum(m)); return 1; }
LFN(f_m_random){ UNUSED_SELF;
    double r=rnd();
    if(nargs==0){ RET(0,mknum(r)); return 1; }
    if(nargs==1){ int u=checkint(L,base,nargs,0,"random");
        RET(0,mknum((double)(1+(int)(r*u)))); return 1; }
    int lo=checkint(L,base,nargs,0,"random"), hi=checkint(L,base,nargs,1,"random");
    RET(0,mknum((double)(lo+(int)(r*(hi-lo+1))))); return 1;
}
LFN(f_m_randomseed){ UNUSED_SELF;
    rngstate=(uint64_t)(int64_t)checknum(L,base,nargs,0,"randomseed")|1ULL; return 0; }

/* ---- bit32 ----------------------------------------------------------- */
LFN(f_b_band){ UNUSED_SELF;
    uint32_t r=0xFFFFFFFFu;
    for(int i=0;i<nargs;i++) r&=checku32(L,base,nargs,i,"band");
    RET(0,mknum((double)r)); return 1; }
LFN(f_b_bor){ UNUSED_SELF;
    uint32_t r=0;
    for(int i=0;i<nargs;i++) r|=checku32(L,base,nargs,i,"bor");
    RET(0,mknum((double)r)); return 1; }
LFN(f_b_bxor){ UNUSED_SELF;
    uint32_t r=0;
    for(int i=0;i<nargs;i++) r^=checku32(L,base,nargs,i,"bxor");
    RET(0,mknum((double)r)); return 1; }
LFN(f_b_bnot){ UNUSED_SELF;
    RET(0,mknum((double)(uint32_t)~checku32(L,base,nargs,0,"bnot"))); return 1; }
LFN(f_b_lshift){ UNUSED_SELF;
    uint32_t a=checku32(L,base,nargs,0,"lshift"); int n=checkint(L,base,nargs,1,"lshift");
    RET(0,mknum((double)(uint32_t)(n<=-32||n>=32?0:(n>=0? a<<n : a>>(-n))))); return 1; }
LFN(f_b_rshift){ UNUSED_SELF;
    uint32_t a=checku32(L,base,nargs,0,"rshift"); int n=checkint(L,base,nargs,1,"rshift");
    RET(0,mknum((double)(uint32_t)(n<=-32||n>=32?0:(n>=0? a>>n : a<<(-n))))); return 1; }
LFN(f_b_arshift){ UNUSED_SELF;
    uint32_t a=checku32(L,base,nargs,0,"arshift"); int n=checkint(L,base,nargs,1,"arshift");
    if(n<0){ RET(0,mknum((double)(uint32_t)(-n>=32?0:a<<(-n)))); return 1; }
    if(n>=32){ RET(0,mknum((double)(uint32_t)((a&0x80000000u)?0xFFFFFFFFu:0u))); return 1; }
    uint32_t r=a>>n;
    if(a&0x80000000u) r|=(uint32_t)(0xFFFFFFFFu<<(32-n));
    RET(0,mknum((double)r)); return 1; }
LFN(f_b_btest){ UNUSED_SELF;
    uint32_t r=0xFFFFFFFFu;
    for(int i=0;i<nargs;i++) r&=checku32(L,base,nargs,i,"btest");
    RET(0,mkbool(r!=0)); return 1; }
LFN(f_b_bswap){ UNUSED_SELF;
    uint32_t a=checku32(L,base,nargs,0,"bswap");
    a=((a&0xFFu)<<24)|((a&0xFF00u)<<8)|((a>>8)&0xFF00u)|((a>>24)&0xFFu);
    RET(0,mknum((double)a)); return 1; }
LFN(f_b_extract){ UNUSED_SELF;
    uint32_t a=checku32(L,base,nargs,0,"extract");
    int f=checkint(L,base,nargs,1,"extract");
    int w=nargs>=3?checkint(L,base,nargs,2,"extract"):1;
    if(f<0||w<1||f+w>32) luc_error("bit32.extract: field out of range");
    uint32_t mask = (w==32)?0xFFFFFFFFu:((1u<<w)-1u);
    RET(0,mknum((double)((a>>f)&mask))); return 1; }
LFN(f_b_replace){ UNUSED_SELF;
    uint32_t a=checku32(L,base,nargs,0,"replace");
    uint32_t v=checku32(L,base,nargs,1,"replace");
    int f=checkint(L,base,nargs,2,"replace");
    int w=nargs>=4?checkint(L,base,nargs,3,"replace"):1;
    if(f<0||w<1||f+w>32) luc_error("bit32.replace: field out of range");
    uint32_t mask=((w==32)?0xFFFFFFFFu:((1u<<w)-1u))<<f;
    RET(0,mknum((double)((a&~mask)|((v<<f)&mask)))); return 1; }
LFN(f_m_tointeger){ UNUSED_SELF;
    Value v=AR(0);
    if(v.t==LT_NUM && v.u.n==floor(v.u.n) && fabs(v.u.n)<=9007199254740992.0)
        RET(0,v);
    else RET(0,NIL);
    return 1;
}
LFN(f_m_type){ UNUSED_SELF;
    Value v=AR(0);
    if(v.t!=LT_NUM){ RET(0,NIL); return 1; }
    RET(0,cstrv((v.u.n==floor(v.u.n)&&fabs(v.u.n)<=9007199254740992.0)?"integer":"float"));
    return 1;
}

void lucL_open_math(void){
    rngstate ^= (uint64_t)time(NULL)*2654435761u | 1ULL;   /* moved from luc_init */
    Table *m=newlib("math");
    reg(m,"floor",f_m_floor); reg(m,"ceil",f_m_ceil);  reg(m,"sqrt",f_m_sqrt);
    reg(m,"abs",f_m_abs);     reg(m,"sin",f_m_sin);    reg(m,"cos",f_m_cos);
    reg(m,"tan",f_m_tan);     reg(m,"asin",f_m_asin);  reg(m,"acos",f_m_acos);
    reg(m,"atan",f_m_atan);   reg(m,"exp",f_m_exp);    reg(m,"log",f_m_log);
    reg(m,"pow",f_m_pow);     reg(m,"fmod",f_m_fmod);  reg(m,"modf",f_m_modf);
    reg(m,"max",f_m_max);     reg(m,"min",f_m_min);
    reg(m,"random",f_m_random); reg(m,"randomseed",f_m_randomseed);
    tab_set(m,cstrv("pi"),mknum(3.14159265358979323846));
    tab_set(m,cstrv("huge"),mknum(HUGE_VAL));
    reg(m,"tointeger",f_m_tointeger); reg(m,"type",f_m_type);
    tab_set(m,cstrv("maxinteger"),mknum(9007199254740992.0));
    tab_set(m,cstrv("mininteger"),mknum(-9007199254740992.0));
    /* --- bit32 --- */
    Table *b=newlib("bit32");
    reg(b,"band",f_b_band);   reg(b,"bor",f_b_bor);     reg(b,"bxor",f_b_bxor);
    reg(b,"bnot",f_b_bnot);   reg(b,"lshift",f_b_lshift);reg(b,"rshift",f_b_rshift);
    reg(b,"arshift",f_b_arshift); reg(b,"btest",f_b_btest);
    reg(b,"bswap",f_b_bswap); reg(b,"extract",f_b_extract); reg(b,"replace",f_b_replace);
}


/* ==================== luc_lib_os.c ==================== */
/*
** luc_lib_os.c -- os library (os.execute moved here from the window section)
*/
/* ---- os -------------------------------------------------------------- */
LFN(f_os_clock){ UNUSED_SELF; (void)base;(void)nargs;
    RET(0,mknum((double)clock()/(double)CLOCKS_PER_SEC)); return 1; }
LFN(f_os_time){ UNUSED_SELF; (void)base;(void)nargs;
    RET(0,mknum((double)time(NULL))); return 1; }
LFN(f_os_date){ UNUSED_SELF;
    const char *fmt = nargs>=1? checkstr(L,base,nargs,0,"date")->s : "%c";
    time_t t = nargs>=2? (time_t)checknum(L,base,nargs,1,"date") : time(NULL);
    if(*fmt=='!'||*fmt=='*') fmt++;
    char buf[256];
    struct tm *tmv=localtime(&t);
    if(!tmv || strftime(buf,sizeof buf,fmt,tmv)==0) buf[0]=0;
    RET(0,cstrv(buf)); return 1;
}
LFN(f_os_exit){ UNUSED_SELF;
    int c = 0;
    if(nargs>=1){
        Value v=AR(0);
        c = (v.t==LT_BOOL)? (v.u.b?0:1) : checkint(L,base,nargs,0,"exit");
    }
    fflush(stdout); fflush(stderr);
    exit(c);
    return 0;
}
LFN(f_os_getenv){ UNUSED_SELF;
    const char *e=getenv(checkstr(L,base,nargs,0,"getenv")->s);
    if(e) RET(0,cstrv(e)); else RET(0,NIL);
    return 1;
}
LFN(f_os_remove){ UNUSED_SELF;
    Str *p=checkstr(L,base,nargs,0,"remove");
    if(remove(p->s)==0){ RET(0,mkbool(1)); return 1; }
    RET(0,NIL); RET(1,cstrv("could not remove file")); return 2;
}
LFN(f_os_rename){ UNUSED_SELF;
    Str *a=checkstr(L,base,nargs,0,"rename"), *b=checkstr(L,base,nargs,1,"rename");
    if(rename(a->s,b->s)==0){ RET(0,mkbool(1)); return 1; }
    RET(0,NIL); RET(1,cstrv("could not rename file")); return 2;
}
LFN(f_os_sleep){ UNUSED_SELF;
    luc_sleep(checknum(L,base,nargs,0,"sleep")); return 0;
}
LFN(f_os_execute){ UNUSED_SELF;
    if(nargs==0){ RET(0,mkbool(system(NULL)!=0)); return 1; }
    int rc=system(checkstr(L,base,nargs,0,"execute")->s);
    RET(0,mkbool(rc==0)); RET(1,cstrv("exit")); RET(2,mknum((double)rc));
    return 3;
}
void lucL_open_os(void){
    Table *o=newlib("os");
    reg(o,"clock",f_os_clock); reg(o,"time",f_os_time);  reg(o,"date",f_os_date);
    reg(o,"exit",f_os_exit);   reg(o,"getenv",f_os_getenv);
    reg(o,"remove",f_os_remove);reg(o,"rename",f_os_rename);
    reg(o,"sleep",f_os_sleep);
    reg(o,"execute",f_os_execute);
}


/* ==================== luc_lib_string.c ==================== */
/*
** luc_lib_string.c -- string library + Lua-style pattern matching
*/
/* ---- string ----------------------------------------------------------- */
static int posrelat(int pos,int len){
    if(pos>=0) return pos;
    if(-pos>len) return 0;
    return len+pos+1;
}
LFN(f_str_len){ UNUSED_SELF; RET(0,mknum((double)checkstr(L,base,nargs,0,"len")->len)); return 1; }
LFN(f_str_sub){ UNUSED_SELF;
    Str *s=checkstr(L,base,nargs,0,"sub");
    int i=posrelat(nargs>=2?checkint(L,base,nargs,1,"sub"):1,s->len);
    int j=posrelat(nargs>=3?checkint(L,base,nargs,2,"sub"):-1,s->len);
    if(i<1) i=1;
    if(j>s->len) j=s->len;
    if(i>j){ RET(0,cstrv("")); return 1; }
    RET(0,strv(s->s+i-1,j-i+1)); return 1;
}
LFN(f_str_upper){ UNUSED_SELF;
    Str *s=checkstr(L,base,nargs,0,"upper");
    char *b=(char*)lmalloc((size_t)s->len+1);
    for(int i=0;i<s->len;i++) b[i]=(char)toupper((unsigned char)s->s[i]);
    RET(0,strv(b,s->len)); free(b); return 1;
}
LFN(f_str_lower){ UNUSED_SELF;
    Str *s=checkstr(L,base,nargs,0,"lower");
    char *b=(char*)lmalloc((size_t)s->len+1);
    for(int i=0;i<s->len;i++) b[i]=(char)tolower((unsigned char)s->s[i]);
    RET(0,strv(b,s->len)); free(b); return 1;
}
LFN(f_str_rep){ UNUSED_SELF;
    Str *s=checkstr(L,base,nargs,0,"rep");
    int n=checkint(L,base,nargs,1,"rep");
    Str *sep = nargs>=3? checkstr(L,base,nargs,2,"rep") : NULL;
    if(n<=0){ RET(0,cstrv("")); return 1; }
    int sl=sep?sep->len:0;
    size_t total=(size_t)s->len*(size_t)n+(size_t)sl*(size_t)(n-1);
    char *b=(char*)lmalloc(total+1); size_t o=0;
    for(int i=0;i<n;i++){
        if(i&&sl){ memcpy(b+o,sep->s,(size_t)sl); o+=(size_t)sl; }
        memcpy(b+o,s->s,(size_t)s->len); o+=(size_t)s->len;
    }
    RET(0,strv(b,(int)o)); free(b); return 1;
}
LFN(f_str_reverse){ UNUSED_SELF;
    Str *s=checkstr(L,base,nargs,0,"reverse");
    char *b=(char*)lmalloc((size_t)s->len+1);
    for(int i=0;i<s->len;i++) b[i]=s->s[s->len-1-i];
    RET(0,strv(b,s->len)); free(b); return 1;
}
LFN(f_str_byte){ UNUSED_SELF;
    Str *s=checkstr(L,base,nargs,0,"byte");
    int i=posrelat(nargs>=2?checkint(L,base,nargs,1,"byte"):1,s->len);
    int j=posrelat(nargs>=3?checkint(L,base,nargs,2,"byte"):i,s->len);
    if(i<1)i=1;
    if(j>s->len)j=s->len;
    int n=0;
    for(int x=i;x<=j;x++) RET(n++,mknum((double)(unsigned char)s->s[x-1]));
    return n;
}
LFN(f_str_char){ UNUSED_SELF;
    char *b=(char*)lmalloc((size_t)nargs+1);
    for(int i=0;i<nargs;i++) b[i]=(char)(int)checknum(L,base,nargs,i,"char");
    RET(0,strv(b,nargs)); free(b); return 1;
}
LFN(f_str_format){ UNUSED_SELF;
    Str *f=checkstr(L,base,nargs,0,"format");
    size_t cap=256,len=0; char *out=(char*)lmalloc(cap);
    #define OUTC(c) do{ if(len+2>cap){cap*=2;out=(char*)lrealloc(out,cap);} out[len++]=(char)(c);}while(0)
    #define OUTS(p,n) do{ size_t _n=(size_t)(n); if(len+_n+1>cap){while(len+_n+1>cap)cap*=2;out=(char*)lrealloc(out,cap);} memcpy(out+len,(p),_n); len+=_n;}while(0)
    int argi=1;
    for(int i=0;i<f->len;i++){
        char c=f->s[i];
        if(c!='%'){ OUTC(c); continue; }
        i++;
        if(i>=f->len) break;
        if(f->s[i]=='%'){ OUTC('%'); continue; }
        char spec[32]; int sn=0; spec[sn++]='%';
        while(i<f->len && strchr("-+ #0",f->s[i]) && sn<20) spec[sn++]=f->s[i++];
        while(i<f->len && isdigit((unsigned char)f->s[i]) && sn<24) spec[sn++]=f->s[i++];
        if(i<f->len && f->s[i]=='.'){ spec[sn++]=f->s[i++];
            while(i<f->len && isdigit((unsigned char)f->s[i]) && sn<28) spec[sn++]=f->s[i++]; }
        char conv= i<f->len? f->s[i] : 's';
        char tmp[512];
        switch(conv){
            case 'd': case 'i': {
                spec[sn++]='l'; spec[sn++]='l'; spec[sn++]='d'; spec[sn]=0;
                snprintf(tmp,sizeof tmp,spec,(long long)checknum(L,base,nargs,argi++,"format"));
                OUTS(tmp,strlen(tmp)); break; }
            case 'u': case 'x': case 'X': case 'o': case 'c': {
                spec[sn++]='l'; spec[sn++]='l'; spec[sn++]= conv=='c'?'d':conv;
                if(conv=='c'){ sn-=3; spec[sn++]='c'; }
                spec[sn]=0;
                long long iv=(long long)checknum(L,base,nargs,argi++,"format");
                if(conv=='c') snprintf(tmp,sizeof tmp,spec,(int)iv);
                else snprintf(tmp,sizeof tmp,spec,iv);
                OUTS(tmp,strlen(tmp)); break; }
            case 'f': case 'F': case 'e': case 'E': case 'g': case 'G': {
                spec[sn++]=conv; spec[sn]=0;
                snprintf(tmp,sizeof tmp,spec,checknum(L,base,nargs,argi++,"format"));
                OUTS(tmp,strlen(tmp)); break; }
            case 'q': {
                Str *s=tostr(AR(argi++));
                OUTC('"');
                for(int x=0;x<s->len;x++){
                    char ch=s->s[x];
                    if(ch=='"'||ch=='\\'){ OUTC('\\'); OUTC(ch); }
                    else if(ch=='\n'){ OUTC('\\'); OUTC('n'); }
                    else if(ch=='\r'){ OUTC('\\'); OUTC('r'); }
                    else if(ch==0){ OUTC('\\'); OUTC('0'); }
                    else OUTC(ch);
                }
                OUTC('"'); break; }
            case 's': default: {
                Str *s=tostr(AR(argi++));
                spec[sn++]='s'; spec[sn]=0;
                if(sn>2 && s->len<400){ snprintf(tmp,sizeof tmp,spec,s->s); OUTS(tmp,strlen(tmp)); }
                else OUTS(s->s,s->len);
                break; }
        }
    }
    RET(0,strv(out,(int)len)); free(out);
    #undef OUTC
    #undef OUTS
    return 1;
}
/* --- extensions --- */
LFN(f_str_split){ UNUSED_SELF;
    Str *s=checkstr(L,base,nargs,0,"split");
    Str *sep = nargs>=2? checkstr(L,base,nargs,1,"split") : NULL;
    Table *l=tab_new(1);
    Value lv=mkobj(LT_LIST,l);
    RET(0,lv);                                   /* root it immediately */
    if(!sep || sep->len==0){
        for(int i=0;i<s->len;i++) list_push(l,strv(s->s+i,1));
        return 1;
    }
    int start=0;
    for(int i=0;i+sep->len<=s->len;){
        if(memcmp(s->s+i,sep->s,(size_t)sep->len)==0){
            list_push(l,strv(s->s+start,i-start));
            i+=sep->len; start=i;
        } else i++;
    }
    list_push(l,strv(s->s+start,s->len-start));
    return 1;
}
LFN(f_str_trim){ UNUSED_SELF;
    Str *s=checkstr(L,base,nargs,0,"trim");
    int i=0,j=s->len-1;
    while(i<=j && isspace((unsigned char)s->s[i])) i++;
    while(j>=i && isspace((unsigned char)s->s[j])) j--;
    RET(0,strv(s->s+i,j-i+1)); return 1;
}
LFN(f_str_startswith){ UNUSED_SELF;
    Str *s=checkstr(L,base,nargs,0,"startswith"), *p=checkstr(L,base,nargs,1,"startswith");
    RET(0,mkbool(p->len<=s->len && memcmp(s->s,p->s,(size_t)p->len)==0)); return 1;
}
LFN(f_str_endswith){ UNUSED_SELF;
    Str *s=checkstr(L,base,nargs,0,"endswith"), *p=checkstr(L,base,nargs,1,"endswith");
    RET(0,mkbool(p->len<=s->len && memcmp(s->s+s->len-p->len,p->s,(size_t)p->len)==0)); return 1;
}
LFN(f_str_contains){ UNUSED_SELF;
    Str *s=checkstr(L,base,nargs,0,"contains");
    RET(0,mkbool(vm_in(AR(1),mkobj(LT_STR,s)))); return 1;
}
LFN(f_str_tohex){ UNUSED_SELF;
    Str *s=checkstr(L,base,nargs,0,"tohex");
    char *b=(char*)lmalloc((size_t)s->len*2+1);
    for(int i=0;i<s->len;i++){
        unsigned char c=(unsigned char)s->s[i];
        b[i*2]=HEXD[c>>4]; b[i*2+1]=HEXD[c&15];
    }
    RET(0,strv(b,s->len*2)); free(b); return 1;
}
LFN(f_str_fromhex){ UNUSED_SELF;
    Str *s=checkstr(L,base,nargs,0,"fromhex");
    if(s->len%2) luc_error("string.fromhex: hex string must have even length");
    char *b=(char*)lmalloc((size_t)s->len/2+1);
    for(int i=0;i<s->len;i+=2){
        int hi=hexval((unsigned char)s->s[i]), lo=hexval((unsigned char)s->s[i+1]);
        if(hi<0||lo<0){ free(b); luc_error("string.fromhex: invalid hex digit"); }
        b[i/2]=(char)((hi<<4)|lo);
    }
    RET(0,strv(b,s->len/2)); free(b); return 1;
}

/* ---- Lua-style pattern matching -------------------------------------- */
#define L_ESC '%'
#define MAXCAPT 32
typedef struct MatchState {
    const char *src_init,*src_end,*p_end;
    int level, depth;
    struct { const char *init; ptrdiff_t len; } capture[MAXCAPT];
} MatchState;
#define CAP_UNF (-1)
#define CAP_POS (-2)
static const char *do_match(MatchState *ms,const char *s,const char *p);

static const char *classend(MatchState *ms,const char *p){
    switch(*p++){
        case L_ESC:
            if(p==ms->p_end) luc_error("malformed pattern (ends with '%%')");
            return p+1;
        case '[':
            if(p<ms->p_end && *p=='^') p++;
            do{
                if(p==ms->p_end) luc_error("malformed pattern (missing ']')");
                if(*(p++)==L_ESC && p<ms->p_end) p++;
            }while(p>=ms->p_end || *p!=']');
            return p+1;
        default: return p;
    }
}
static int match_class(int c,int cl){
    int res;
    switch(tolower(cl)){
        case 'a': res=isalpha(c); break;  case 'c': res=iscntrl(c); break;
        case 'd': res=isdigit(c); break;  case 'l': res=islower(c); break;
        case 'p': res=ispunct(c); break;  case 's': res=isspace(c); break;
        case 'u': res=isupper(c); break;  case 'w': res=isalnum(c); break;
        case 'x': res=isxdigit(c); break;
        default: return cl==c;
    }
    if(isupper(cl)) res=!res;
    return res;
}
static int matchbracket(int c,const char *p,const char *ec){
    int sig=1;
    if(*(p+1)=='^'){ sig=0; p++; }
    while(++p<ec){
        if(*p==L_ESC){ p++; if(match_class(c,(unsigned char)*p)) return sig; }
        else if(*(p+1)=='-' && p+2<ec){
            p+=2;
            if((unsigned char)*(p-2)<=c && c<=(unsigned char)*p) return sig;
        } else if((unsigned char)*p==c) return sig;
    }
    return !sig;
}
static int singlematch(MatchState *ms,const char *s,const char *p,const char *ep){
    if(s>=ms->src_end) return 0;
    int c=(unsigned char)*s;
    switch(*p){
        case '.': return 1;
        case L_ESC: return match_class(c,(unsigned char)*(p+1));
        case '[': return matchbracket(c,p,ep-1);
        default: return (unsigned char)*p==c;
    }
}
static const char *max_expand(MatchState *ms,const char *s,const char *p,const char *ep){
    ptrdiff_t i=0;
    while(singlematch(ms,s+i,p,ep)) i++;
    while(i>=0){
        const char *r=do_match(ms,s+i,ep+1);
        if(r) return r;
        i--;
    }
    return NULL;
}
static const char *min_expand(MatchState *ms,const char *s,const char *p,const char *ep){
    for(;;){
        const char *r=do_match(ms,s,ep+1);
        if(r) return r;
        if(singlematch(ms,s,p,ep)) s++;
        else return NULL;
    }
}
static const char *start_capture(MatchState *ms,const char *s,const char *p,int what){
    int l=ms->level;
    if(l>=MAXCAPT) luc_error("too many captures");
    ms->capture[l].len=what; ms->capture[l].init=s;
    ms->level=l+1;
    const char *r=do_match(ms,s,p);
    if(!r) ms->level--;
    return r;
}
static const char *end_capture(MatchState *ms,const char *s,const char *p){
    int l=-1;
    for(int i=ms->level-1;i>=0;i--) if(ms->capture[i].len==CAP_UNF){ l=i; break; }
    if(l<0) luc_error("invalid pattern capture");
    ms->capture[l].len=s-ms->capture[l].init;
    const char *r=do_match(ms,s,p);
    if(!r) ms->capture[l].len=CAP_UNF;
    return r;
}
static const char *match_capture(MatchState *ms,const char *s,int ll){
    ll-='1';
    if(ll<0||ll>=ms->level||ms->capture[ll].len==CAP_UNF) luc_error("invalid capture index");
    ptrdiff_t len=ms->capture[ll].len;
    if((ms->src_end-s)>=len && memcmp(ms->capture[ll].init,s,(size_t)len)==0) return s+len;
    return NULL;
}
static const char *do_match(MatchState *ms,const char *s,const char *p){
    if(ms->depth++ > 200){ ms->depth--; luc_error("pattern too complex"); }
    while(p!=ms->p_end){
        switch(*p){
            case '(':
                ms->depth--;
                return (*(p+1)==')')? start_capture(ms,s,p+2,CAP_POS)
                                    : start_capture(ms,s,p+1,CAP_UNF);
            case ')': ms->depth--; return end_capture(ms,s,p+1);
            case '$':
                if(p+1==ms->p_end){ ms->depth--; return (s==ms->src_end)?s:NULL; }
                goto dflt;
            case L_ESC:
                if(isdigit((unsigned char)*(p+1))){
                    s=match_capture(ms,s,(unsigned char)*(p+1));
                    if(!s){ ms->depth--; return NULL; }
                    p+=2; continue;
                }
                goto dflt;
            default: dflt: {
                const char *ep=classend(ms,p);
                if(!singlematch(ms,s,p,ep)){
                    if(ep<ms->p_end && (*ep=='*'||*ep=='?'||*ep=='-')){ p=ep+1; continue; }
                    ms->depth--; return NULL;
                }
                if(ep<ms->p_end){
                    switch(*ep){
                        case '?': {
                            const char *r=do_match(ms,s+1,ep+1);
                            if(r){ ms->depth--; return r; }
                            p=ep+1; continue; }
                        case '+': ms->depth--; return max_expand(ms,s+1,p,ep);
                        case '*': ms->depth--; return max_expand(ms,s,p,ep);
                        case '-': ms->depth--; return min_expand(ms,s,p,ep);
                    }
                }
                s++; p=ep; continue; }
        }
    }
    ms->depth--;
    return s;
}
static int push_captures(LucState *L,int base,MatchState *ms,const char *s,const char *e,int slot){
    int n = (ms->level==0 && s)? 1 : ms->level;
    for(int i=0;i<n;i++){
        if(ms->level==0) RET(slot+i,strv(s,(int)(e-s)));
        else if(ms->capture[i].len==CAP_POS)
            RET(slot+i,mknum((double)(ms->capture[i].init-ms->src_init+1)));
        else RET(slot+i,strv(ms->capture[i].init,(int)ms->capture[i].len));
    }
    return n;
}
static int str_find_aux(LucState *L,int base,int nargs,int find){
    Str *s=checkstr(L,base,nargs,0,find?"find":"match");
    Str *p=checkstr(L,base,nargs,1,find?"find":"match");
    int init=posrelat(nargs>=3?checkint(L,base,nargs,2,"find"):1,s->len);
    if(init<1) init=1;
    if(init>s->len+1){ RET(0,NIL); return 1; }
    int plain = nargs>=4 && truthy(AR(3));
    if(find && (plain || !strpbrk(p->s,"^$*+?.([%-"))){
        for(const char *s1=s->s+init-1; s1+p->len<=s->s+s->len; s1++){
            if(memcmp(s1,p->s,(size_t)p->len)==0){
                RET(0,mknum((double)(s1-s->s+1)));
                RET(1,mknum((double)(s1-s->s+p->len)));
                return 2;
            }
        }
        RET(0,NIL); return 1;
    }
    MatchState ms; ms.src_init=s->s; ms.src_end=s->s+s->len;
    ms.p_end=p->s+p->len; ms.level=0; ms.depth=0;
    const char *pp=p->s;
    int anchor=(*pp=='^'); if(anchor) pp++;
    const char *s1=s->s+init-1;
    do{
        ms.level=0; ms.depth=0;
        const char *res=do_match(&ms,s1,pp);
        if(res){
            if(find){
                RET(0,mknum((double)(s1-s->s+1)));
                RET(1,mknum((double)(res-s->s)));
                return 2+push_captures(L,base,&ms,NULL,NULL,2);
            }
            return push_captures(L,base,&ms,s1,res,0);
        }
    }while(s1++ < ms.src_end && !anchor);
    RET(0,NIL); return 1;
}
LFN(f_str_find){ UNUSED_SELF; return str_find_aux(L,base,nargs,1); }
LFN(f_str_match){ UNUSED_SELF; return str_find_aux(L,base,nargs,0); }
LFN(f_gmatch_iter){
    Str *s=AS_STR(self->up[0]), *p=AS_STR(self->up[1]);
    int pos=(int)self->up[2].u.n;
    MatchState ms; ms.src_init=s->s; ms.src_end=s->s+s->len;
    ms.p_end=p->s+p->len;
    for(const char *s1=s->s+pos; s1<=ms.src_end; s1++){
        ms.level=0; ms.depth=0;
        const char *e=do_match(&ms,s1,p->s);
        if(e){
            int newpos=(int)(e-s->s);
            if(e==s1) newpos++;
            self->up[2]=mknum((double)newpos);
            return push_captures(L,base,&ms,s1,e,0);
        }
    }
    RET(0,NIL); return 1;
}
LFN(f_str_gmatch){ UNUSED_SELF;
    Str *s=checkstr(L,base,nargs,0,"gmatch"), *p=checkstr(L,base,nargs,1,"gmatch");
    CFunc *c=cfunc_new(f_gmatch_iter,"gmatch",3);
    c->up[0]=mkobj(LT_STR,s); c->up[1]=mkobj(LT_STR,p); c->up[2]=mknum(0);
    RET(0,mkobj(LT_CFUNC,c));
    return 1;
}
LFN(f_str_gsub){ UNUSED_SELF;
    Str *s=checkstr(L,base,nargs,0,"gsub"), *p=checkstr(L,base,nargs,1,"gsub");
    Value repl=AR(2);
    int maxn = nargs>=4? checkint(L,base,nargs,3,"gsub") : -1;
    size_t cap=(size_t)s->len+32,len=0; char *out=(char*)lmalloc(cap);
    #define OUTS(ptr,n) do{ size_t _n=(size_t)(n); if(len+_n+1>cap){while(len+_n+1>cap)cap*=2;out=(char*)lrealloc(out,cap);} memcpy(out+len,(ptr),_n); len+=_n;}while(0)
    MatchState ms; ms.src_init=s->s; ms.src_end=s->s+s->len; ms.p_end=p->s+p->len;
    const char *pp=p->s;
    int anchor=(*pp=='^'); if(anchor) pp++;
    const char *s1=s->s; int count=0;
    int scratch=base+nargs+2;
    while(maxn<0 || count<maxn){
        ms.level=0; ms.depth=0;
        const char *e=do_match(&ms,s1,pp);
        if(e){
            count++;
            /* build replacement */
            if(repl.t==LT_STR||repl.t==LT_NUM){
                Str *r=tostr(repl);
                for(int i=0;i<r->len;i++){
                    if(r->s[i]==L_ESC && i+1<r->len){
                        i++;
                        if(r->s[i]=='0'){ OUTS(s1,e-s1); }
                        else if(isdigit((unsigned char)r->s[i])){
                            int idx=r->s[i]-'1';
                            if(ms.level==0 && idx==0){ OUTS(s1,e-s1); }
                            else if(idx>=0&&idx<ms.level){
                                if(ms.capture[idx].len==CAP_POS){
                                    char nb[32];
                                    snprintf(nb,sizeof nb,"%d",(int)(ms.capture[idx].init-s->s+1));
                                    OUTS(nb,strlen(nb));
                                } else OUTS(ms.capture[idx].init,ms.capture[idx].len);
                            }
                        } else { OUTS(&r->s[i],1); }
                    } else OUTS(&r->s[i],1);
                }
            } else if(repl.t==LT_TABLE||repl.t==LT_LIST){
                ensure_stack(L,scratch+8);
                int n=push_captures(L,scratch,&ms,s1,e,0);
                Value v=tab_get(AS_TAB(repl),L->stack[scratch]);
                (void)n;
                if(truthy(v)){ Str *r=tostr(v); OUTS(r->s,r->len); }
                else OUTS(s1,e-s1);
            } else if(repl.t==LT_FUNC||repl.t==LT_CFUNC){
                ensure_stack(L,scratch+MAXCAPT+8);
                L->stack[scratch]=repl;
                int n=push_captures(L,scratch+1,&ms,s1,e,0);
                int nr=vm_call(L,scratch,n,1);
                (void)nr;
                Value v=L->stack[scratch];
                if(truthy(v)){ Str *r=tostr(v); OUTS(r->s,r->len); }
                else OUTS(s1,e-s1);
            } else luc_error("bad argument #3 to 'gsub' (string/table/function expected)");
        }
        if(e && e>s1) s1=e;
        else if(s1<ms.src_end){ OUTS(s1,1); s1++; }
        else break;
        if(anchor) break;
    }
    if(s1<=ms.src_end) OUTS(s1,ms.src_end-s1);
    RET(0,strv(out,(int)len));
    RET(1,mknum((double)count));
    free(out);
    #undef OUTS
    return 2;
}

void lucL_open_string(void){
    Table *s=newlib("string"); V.stringlib=s;
    reg(s,"len",f_str_len);        reg(s,"sub",f_str_sub);
    reg(s,"upper",f_str_upper);    reg(s,"lower",f_str_lower);
    reg(s,"rep",f_str_rep);        reg(s,"reverse",f_str_reverse);
    reg(s,"byte",f_str_byte);      reg(s,"char",f_str_char);
    reg(s,"format",f_str_format);  reg(s,"find",f_str_find);
    reg(s,"match",f_str_match);    reg(s,"gmatch",f_str_gmatch);
    reg(s,"gsub",f_str_gsub);
    /* LUC extensions */
    reg(s,"split",f_str_split);          reg(s,"trim",f_str_trim);
    reg(s,"startswith",f_str_startswith);reg(s,"endswith",f_str_endswith);
    reg(s,"contains",f_str_contains);    reg(s,"tohex",f_str_tohex);
    reg(s,"fromhex",f_str_fromhex);
}


/* ==================== luc_lib_window.c ==================== */
/*
** luc_lib_window.c -- SDL2 window module (loaded with require "window")
**                     build with -DLUC_WINDOW (see Makefile target luc-window)
*/
#ifdef LUC_WINDOW
#  define SDL_MAIN_HANDLED
#  include <SDL2/SDL.h>
#  ifndef LUC_NO_TTF
#    include <SDL2/SDL_ttf.h>
#  endif
#  ifndef LUC_NO_IMAGE
#    include <SDL2/SDL_image.h>
#  endif
#define W_KEYS        SDL_NUM_SCANCODES
#define W_IMGCACHE    64
#define W_TXTCACHE    96
#define W_FONTSLOTS   12
#define W_MAXPOLY     256

typedef struct { char path[512]; SDL_Texture *tex; int w,h; unsigned age; } WImg;
typedef struct { char txt[64]; int size; Uint32 col; SDL_Texture *tex;
                 int w,h; unsigned age; } WTxt;
#ifndef LUC_NO_TTF
typedef struct { int size; TTF_Font *f; unsigned age; } WFont;
#endif

static struct {
    SDL_Window   *win;
    SDL_Renderer *ren;
    int w,h;
    int running, started;
    Uint32 last_frame;
    int target_fps;
    double start_time, delta;
    Uint8 keys_prev[W_KEYS], keys_curr[W_KEYS];
    int mouse_x, mouse_y;
    Uint32 mouse_state, mouse_prev;
    int wheel_dy, wheel_dx;
    int vsync, fullscreen;
    unsigned clock;
    char textbuf[256]; int textlen;
    WImg img[W_IMGCACHE];
    WTxt txt[W_TXTCACHE];
#ifndef LUC_NO_TTF
    int   ttf_ok;
    char  fontpath[512];
    int   fontsize;
    WFont fonts[W_FONTSLOTS];
#endif
} W;

/* ---- embedded 5x7 fallback font (ASCII 32..126, column major, LSB = top) - */
static const unsigned char W_FONT5x7[95][5] = {
{0x00,0x00,0x00,0x00,0x00},{0x00,0x00,0x5F,0x00,0x00},{0x00,0x07,0x00,0x07,0x00},
{0x14,0x7F,0x14,0x7F,0x14},{0x24,0x2A,0x7F,0x2A,0x12},{0x23,0x13,0x08,0x64,0x62},
{0x36,0x49,0x55,0x22,0x50},{0x00,0x05,0x03,0x00,0x00},{0x00,0x1C,0x22,0x41,0x00},
{0x00,0x41,0x22,0x1C,0x00},{0x14,0x08,0x3E,0x08,0x14},{0x08,0x08,0x3E,0x08,0x08},
{0x00,0x50,0x30,0x00,0x00},{0x08,0x08,0x08,0x08,0x08},{0x00,0x60,0x60,0x00,0x00},
{0x20,0x10,0x08,0x04,0x02},{0x3E,0x51,0x49,0x45,0x3E},{0x00,0x42,0x7F,0x40,0x00},
{0x42,0x61,0x51,0x49,0x46},{0x21,0x41,0x45,0x4B,0x31},{0x18,0x14,0x12,0x7F,0x10},
{0x27,0x45,0x45,0x45,0x39},{0x3C,0x4A,0x49,0x49,0x30},{0x01,0x71,0x09,0x05,0x03},
{0x36,0x49,0x49,0x49,0x36},{0x06,0x49,0x49,0x29,0x1E},{0x00,0x36,0x36,0x00,0x00},
{0x00,0x56,0x36,0x00,0x00},{0x08,0x14,0x22,0x41,0x00},{0x14,0x14,0x14,0x14,0x14},
{0x00,0x41,0x22,0x14,0x08},{0x02,0x01,0x51,0x09,0x06},{0x32,0x49,0x79,0x41,0x3E},
{0x7E,0x11,0x11,0x11,0x7E},{0x7F,0x49,0x49,0x49,0x36},{0x3E,0x41,0x41,0x41,0x22},
{0x7F,0x41,0x41,0x22,0x1C},{0x7F,0x49,0x49,0x49,0x41},{0x7F,0x09,0x09,0x09,0x01},
{0x3E,0x41,0x49,0x49,0x7A},{0x7F,0x08,0x08,0x08,0x7F},{0x00,0x41,0x7F,0x41,0x00},
{0x20,0x40,0x41,0x3F,0x01},{0x7F,0x08,0x14,0x22,0x41},{0x7F,0x40,0x40,0x40,0x40},
{0x7F,0x02,0x0C,0x02,0x7F},{0x7F,0x04,0x08,0x10,0x7F},{0x3E,0x41,0x41,0x41,0x3E},
{0x7F,0x09,0x09,0x09,0x06},{0x3E,0x41,0x51,0x21,0x5E},{0x7F,0x09,0x19,0x29,0x46},
{0x46,0x49,0x49,0x49,0x31},{0x01,0x01,0x7F,0x01,0x01},{0x3F,0x40,0x40,0x40,0x3F},
{0x1F,0x20,0x40,0x20,0x1F},{0x3F,0x40,0x38,0x40,0x3F},{0x63,0x14,0x08,0x14,0x63},
{0x07,0x08,0x70,0x08,0x07},{0x61,0x51,0x49,0x45,0x43},{0x00,0x7F,0x41,0x41,0x00},
{0x02,0x04,0x08,0x10,0x20},{0x00,0x41,0x41,0x7F,0x00},{0x04,0x02,0x01,0x02,0x04},
{0x40,0x40,0x40,0x40,0x40},{0x00,0x01,0x02,0x04,0x00},{0x20,0x54,0x54,0x54,0x78},
{0x7F,0x48,0x44,0x44,0x38},{0x38,0x44,0x44,0x44,0x20},{0x38,0x44,0x44,0x48,0x7F},
{0x38,0x54,0x54,0x54,0x18},{0x08,0x7E,0x09,0x01,0x02},{0x0C,0x52,0x52,0x52,0x3E},
{0x7F,0x08,0x04,0x04,0x78},{0x00,0x44,0x7D,0x40,0x00},{0x20,0x40,0x44,0x3D,0x00},
{0x7F,0x10,0x28,0x44,0x00},{0x00,0x41,0x7F,0x40,0x00},{0x7C,0x04,0x18,0x04,0x78},
{0x7C,0x08,0x04,0x04,0x78},{0x38,0x44,0x44,0x44,0x38},{0x7C,0x14,0x14,0x14,0x08},
{0x08,0x14,0x14,0x18,0x7C},{0x7C,0x08,0x04,0x04,0x08},{0x48,0x54,0x54,0x54,0x20},
{0x04,0x3F,0x44,0x40,0x20},{0x3C,0x40,0x40,0x20,0x7C},{0x1C,0x20,0x40,0x20,0x1C},
{0x3C,0x40,0x30,0x40,0x3C},{0x44,0x28,0x10,0x28,0x44},{0x0C,0x50,0x50,0x50,0x3C},
{0x44,0x64,0x54,0x4C,0x44},{0x00,0x08,0x36,0x41,0x00},{0x00,0x00,0x7F,0x00,0x00},
{0x00,0x41,0x36,0x08,0x00},{0x08,0x08,0x2A,0x1C,0x08}
};

/* ---- helpers ------------------------------------------------------------ */
static void w_need(void){
    if(!W.started || !W.ren)
        luc_error("window: call window.start(title,w,h) first");
}

typedef struct { const char *name; Uint8 r,g,b,a; } WNamed;
static const WNamed W_COLORS[] = {
    {"black",0,0,0,255},          {"white",255,255,255,255},
    {"red",255,0,0,255},          {"green",0,200,60,255},
    {"lime",0,255,0,255},         {"blue",40,90,255,255},
    {"navy",0,0,128,255},         {"yellow",255,235,60,255},
    {"orange",255,150,0,255},     {"purple",160,60,220,255},
    {"magenta",255,0,255,255},    {"pink",255,120,190,255},
    {"gray",128,128,128,255},     {"grey",128,128,128,255},
    {"lightgray",200,200,200,255},{"darkgray",60,60,60,255},
    {"cyan",0,225,255,255},       {"teal",0,128,128,255},
    {"brown",139,90,43,255},      {"gold",255,205,0,255},
    {"silver",192,192,192,255},   {"maroon",128,0,0,255},
    {"olive",128,128,0,255},      {"transparent",0,0,0,0},
    {NULL,0,0,0,0}
};

static int w_hex2(const char *s){
    int a=hexval((unsigned char)s[0]), b=hexval((unsigned char)s[1]);
    if(a<0||b<0) return -1;
    return a*16+b;
}
static SDL_Color w_color(Value v){
    SDL_Color c; c.r=255; c.g=255; c.b=255; c.a=255;
    if(v.t==LT_NIL) return c;
    if(v.t==LT_NUM){
        unsigned long u=(unsigned long)(long long)v.u.n;
        c.r=(Uint8)((u>>16)&255); c.g=(Uint8)((u>>8)&255); c.b=(Uint8)(u&255);
        return c;
    }
    if(v.t==LT_TABLE||v.t==LT_LIST){
        Table *t=AS_TAB(v);
        Value r=tab_get(t,mknum(1)), g=tab_get(t,mknum(2));
        Value b=tab_get(t,mknum(3)), a=tab_get(t,mknum(4));
        if(r.t==LT_NIL){ r=tab_get(t,cstrv("r")); g=tab_get(t,cstrv("g"));
                         b=tab_get(t,cstrv("b")); a=tab_get(t,cstrv("a")); }
        c.r=(Uint8)(r.t==LT_NUM?r.u.n:0);
        c.g=(Uint8)(g.t==LT_NUM?g.u.n:0);
        c.b=(Uint8)(b.t==LT_NUM?b.u.n:0);
        c.a=(Uint8)(a.t==LT_NUM?a.u.n:255);
        return c;
    }
    if(v.t==LT_STR){
        Str *s=AS_STR(v);
        const char *p=s->s; int len=s->len;
        if(len>0 && p[0]=='#'){ p++; len--; }
        else if(len>1 && p[0]=='0' && (p[1]=='x'||p[1]=='X')){ p+=2; len-=2; }
        else {
            for(int i=0;W_COLORS[i].name;i++)
                if(strcmp(W_COLORS[i].name,p)==0){
                    c.r=W_COLORS[i].r; c.g=W_COLORS[i].g;
                    c.b=W_COLORS[i].b; c.a=W_COLORS[i].a;
                    return c;
                }
            luc_error("window: unknown color '%s'",p);
        }
        if(len==3){
            int r=hexval((unsigned char)p[0]),g=hexval((unsigned char)p[1]),
                b=hexval((unsigned char)p[2]);
            if(r<0||g<0||b<0) luc_error("window: bad hex color");
            c.r=(Uint8)(r*17); c.g=(Uint8)(g*17); c.b=(Uint8)(b*17);
            return c;
        }
        if(len==6||len==8){
            int r=w_hex2(p),g=w_hex2(p+2),b=w_hex2(p+4);
            if(r<0||g<0||b<0) luc_error("window: bad hex color");
            c.r=(Uint8)r; c.g=(Uint8)g; c.b=(Uint8)b;
            if(len==8){ int a=w_hex2(p+6); if(a<0) luc_error("window: bad hex color");
                        c.a=(Uint8)a; }
            return c;
        }
        luc_error("window: bad color string '%s'",s->s);
    }
    luc_error("window: color must be a string, table or number (got %s)",type_name(v));
    return c;
}
static Value w_argc(LucState *L,int base,int nargs,int i){
    return i<nargs? L->stack[base+i] : NIL;
}
static void w_setcolor(SDL_Color c){
    SDL_SetRenderDrawBlendMode(W.ren,SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(W.ren,c.r,c.g,c.b,c.a);
}

/* ---- key names ---------------------------------------------------------- */
static int w_scancodes(const char *n,SDL_Scancode *out){
    size_t len=strlen(n);
    if(len==1){
        char c=(char)tolower((unsigned char)n[0]);
        if(c>='a'&&c<='z'){ out[0]=(SDL_Scancode)(SDL_SCANCODE_A+(c-'a')); return 1; }
        if(c>='1'&&c<='9'){ out[0]=(SDL_Scancode)(SDL_SCANCODE_1+(c-'1')); return 1; }
        if(c=='0'){ out[0]=SDL_SCANCODE_0; return 1; }
        switch(c){
            case ' ': out[0]=SDL_SCANCODE_SPACE; return 1;
            case '-': out[0]=SDL_SCANCODE_MINUS; return 1;
            case '=': out[0]=SDL_SCANCODE_EQUALS; return 1;
            case '[': out[0]=SDL_SCANCODE_LEFTBRACKET; return 1;
            case ']': out[0]=SDL_SCANCODE_RIGHTBRACKET; return 1;
            case ';': out[0]=SDL_SCANCODE_SEMICOLON; return 1;
            case '\'':out[0]=SDL_SCANCODE_APOSTROPHE; return 1;
            case ',': out[0]=SDL_SCANCODE_COMMA; return 1;
            case '.': out[0]=SDL_SCANCODE_PERIOD; return 1;
            case '/': out[0]=SDL_SCANCODE_SLASH; return 1;
            case '\\':out[0]=SDL_SCANCODE_BACKSLASH; return 1;
            case '`': out[0]=SDL_SCANCODE_GRAVE; return 1;
        }
    }
    if((n[0]=='f'||n[0]=='F') && isdigit((unsigned char)n[1])){
        int k=atoi(n+1);
        if(k>=1&&k<=12){ out[0]=(SDL_Scancode)(SDL_SCANCODE_F1+k-1); return 1; }
    }
    #define KA(s,c) if(strcmp(n,s)==0){ out[0]=c; return 1; }
    KA("space",SDL_SCANCODE_SPACE) KA("enter",SDL_SCANCODE_RETURN)
    KA("return",SDL_SCANCODE_RETURN) KA("escape",SDL_SCANCODE_ESCAPE)
    KA("esc",SDL_SCANCODE_ESCAPE) KA("tab",SDL_SCANCODE_TAB)
    KA("backspace",SDL_SCANCODE_BACKSPACE) KA("delete",SDL_SCANCODE_DELETE)
    KA("insert",SDL_SCANCODE_INSERT) KA("home",SDL_SCANCODE_HOME)
    KA("end",SDL_SCANCODE_END) KA("pageup",SDL_SCANCODE_PAGEUP)
    KA("pagedown",SDL_SCANCODE_PAGEDOWN) KA("capslock",SDL_SCANCODE_CAPSLOCK)
    KA("up",SDL_SCANCODE_UP) KA("down",SDL_SCANCODE_DOWN)
    KA("left",SDL_SCANCODE_LEFT) KA("right",SDL_SCANCODE_RIGHT)
    #undef KA
    #define K2(s,a,b) if(strcmp(n,s)==0){ out[0]=a; out[1]=b; return 2; }
    K2("shift",SDL_SCANCODE_LSHIFT,SDL_SCANCODE_RSHIFT)
    K2("ctrl",SDL_SCANCODE_LCTRL,SDL_SCANCODE_RCTRL)
    K2("control",SDL_SCANCODE_LCTRL,SDL_SCANCODE_RCTRL)
    K2("alt",SDL_SCANCODE_LALT,SDL_SCANCODE_RALT)
    K2("super",SDL_SCANCODE_LGUI,SDL_SCANCODE_RGUI)
    K2("win",SDL_SCANCODE_LGUI,SDL_SCANCODE_RGUI)
    K2("cmd",SDL_SCANCODE_LGUI,SDL_SCANCODE_RGUI)
    #undef K2
    { SDL_Scancode sc=SDL_GetScancodeFromName(n);
      if(sc!=SDL_SCANCODE_UNKNOWN){ out[0]=sc; return 1; } }
    luc_error("window: unknown key name '%s'",n);
    return 0;
}

/* ---- fonts -------------------------------------------------------------- */
#ifndef LUC_NO_TTF
static const char *W_FONTPATHS[] = {
    "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
    "/usr/share/fonts/TTF/DejaVuSans.ttf",
    "/usr/share/fonts/dejavu/DejaVuSans.ttf",
    "/usr/share/fonts/truetype/liberation/LiberationSans-Regular.ttf",
    "/usr/share/fonts/liberation-sans/LiberationSans-Regular.ttf",
    "/usr/share/fonts/truetype/freefont/FreeSans.ttf",
    "/usr/share/fonts/noto/NotoSans-Regular.ttf",
    "/System/Library/Fonts/Supplemental/Arial.ttf",
    "C:\\Windows\\Fonts\\segoeui.ttf",
    "C:\\Windows\\Fonts\\arial.ttf",
    "C:\\Windows\\Fonts\\consola.ttf",
    NULL
};
static void w_find_font(void){
    if(W.fontpath[0]) return;
    for(int i=0;W_FONTPATHS[i];i++){
        FILE *f=fopen(W_FONTPATHS[i],"rb");
        if(f){ fclose(f); snprintf(W.fontpath,sizeof W.fontpath,"%s",W_FONTPATHS[i]); return; }
    }
}
static TTF_Font *w_font(int size){
    if(!W.ttf_ok || !W.fontpath[0]) return NULL;
    if(size<4) size=4; if(size>256) size=256;
    for(int i=0;i<W_FONTSLOTS;i++)
        if(W.fonts[i].f && W.fonts[i].size==size){ W.fonts[i].age=++W.clock; return W.fonts[i].f; }
    TTF_Font *f=TTF_OpenFont(W.fontpath,size);
    if(!f) return NULL;
    int slot=-1;
    for(int i=0;i<W_FONTSLOTS;i++) if(!W.fonts[i].f){ slot=i; break; }
    if(slot<0){ slot=0;
        for(int i=1;i<W_FONTSLOTS;i++) if(W.fonts[i].age<W.fonts[slot].age) slot=i;
        TTF_CloseFont(W.fonts[slot].f); }
    W.fonts[slot].f=f; W.fonts[slot].size=size; W.fonts[slot].age=++W.clock;
    return f;
}
static void w_drop_fonts(void){
    for(int i=0;i<W_FONTSLOTS;i++){ if(W.fonts[i].f) TTF_CloseFont(W.fonts[i].f);
                                    W.fonts[i].f=NULL; W.fonts[i].size=0; }
}
#endif

/* ---- texture caches ------------------------------------------------------ */
static void w_drop_text_cache(void){
    for(int i=0;i<W_TXTCACHE;i++){
        if(W.txt[i].tex) SDL_DestroyTexture(W.txt[i].tex);
        W.txt[i].tex=NULL; W.txt[i].txt[0]=0;
    }
}
static void w_drop_img_cache(void){
    for(int i=0;i<W_IMGCACHE;i++){
        if(W.img[i].tex) SDL_DestroyTexture(W.img[i].tex);
        W.img[i].tex=NULL; W.img[i].path[0]=0;
    }
}
static SDL_Texture *w_image(const char *path,int *ow,int *oh){
    for(int i=0;i<W_IMGCACHE;i++)
        if(W.img[i].tex && strcmp(W.img[i].path,path)==0){
            W.img[i].age=++W.clock;
            if(ow)*ow=W.img[i].w; if(oh)*oh=W.img[i].h;
            return W.img[i].tex;
        }
    SDL_Surface *s;
#ifndef LUC_NO_IMAGE
    s=IMG_Load(path);
#else
    s=SDL_LoadBMP(path);
#endif
    if(!s) return NULL;
    int iw=s->w, ih=s->h;
    SDL_Texture *t=SDL_CreateTextureFromSurface(W.ren,s);
    SDL_FreeSurface(s);
    if(!t) return NULL;
    SDL_SetTextureBlendMode(t,SDL_BLENDMODE_BLEND);
    int slot=-1;
    for(int i=0;i<W_IMGCACHE;i++) if(!W.img[i].tex){ slot=i; break; }
    if(slot<0){ slot=0;
        for(int i=1;i<W_IMGCACHE;i++) if(W.img[i].age<W.img[slot].age) slot=i;
        SDL_DestroyTexture(W.img[slot].tex); }
    snprintf(W.img[slot].path,sizeof W.img[slot].path,"%s",path);
    W.img[slot].tex=t; W.img[slot].w=iw; W.img[slot].h=ih; W.img[slot].age=++W.clock;
    if(ow)*ow=iw; if(oh)*oh=ih;
    return t;
}

/* ---- text rendering ------------------------------------------------------ */
static void w_bitmap_text(const char *s,int len,int x,int y,SDL_Color c,int size){
    int scale=size/8; if(scale<1) scale=1;
    w_setcolor(c);
    int cx=x;
    for(int i=0;i<len;i++){
        unsigned char ch=(unsigned char)s[i];
        if(ch=='\n'){ cx=x; y+=8*scale; continue; }
        if(ch<32||ch>126){ cx+=6*scale; continue; }
        const unsigned char *g=W_FONT5x7[ch-32];
        for(int col=0;col<5;col++){
            for(int row=0;row<7;row++){
                if(g[col]&(1u<<row)){
                    SDL_Rect r; r.x=cx+col*scale; r.y=y+row*scale;
                    r.w=scale; r.h=scale;
                    SDL_RenderFillRect(W.ren,&r);
                }
            }
        }
        cx+=6*scale;
    }
}
static void w_bitmap_size(const char *s,int len,int size,int *ow,int *oh){
    int scale=size/8; if(scale<1) scale=1;
    int line=0,best=0,rows=1;
    for(int i=0;i<len;i++){
        if(s[i]=='\n'){ if(line>best) best=line; line=0; rows++; }
        else line++;
    }
    if(line>best) best=line;
    *ow=best*6*scale; *oh=rows*8*scale;
}
static void w_text(const char *s,int len,int x,int y,SDL_Color c,int size){
#ifndef LUC_NO_TTF
    TTF_Font *f=w_font(size);
    if(f){
        Uint32 key=((Uint32)c.r<<24)|((Uint32)c.g<<16)|((Uint32)c.b<<8)|c.a;
        SDL_Texture *tex=NULL; int tw=0,th=0;
        int cacheable = (len<63);
        if(cacheable){
            for(int i=0;i<W_TXTCACHE;i++)
                if(W.txt[i].tex && W.txt[i].size==size && W.txt[i].col==key
                   && strcmp(W.txt[i].txt,s)==0){
                    W.txt[i].age=++W.clock;
                    tex=W.txt[i].tex; tw=W.txt[i].w; th=W.txt[i].h; break;
                }
        }
        if(!tex){
            SDL_Surface *sf=TTF_RenderUTF8_Blended(f,s,c);
            if(!sf) return;
            tw=sf->w; th=sf->h;
            tex=SDL_CreateTextureFromSurface(W.ren,sf);
            SDL_FreeSurface(sf);
            if(!tex) return;
            SDL_SetTextureBlendMode(tex,SDL_BLENDMODE_BLEND);
            if(cacheable){
                int slot=-1;
                for(int i=0;i<W_TXTCACHE;i++) if(!W.txt[i].tex){ slot=i; break; }
                if(slot<0){ slot=0;
                    for(int i=1;i<W_TXTCACHE;i++) if(W.txt[i].age<W.txt[slot].age) slot=i;
                    SDL_DestroyTexture(W.txt[slot].tex); }
                snprintf(W.txt[slot].txt,sizeof W.txt[slot].txt,"%s",s);
                W.txt[slot].size=size; W.txt[slot].col=key;
                W.txt[slot].tex=tex; W.txt[slot].w=tw; W.txt[slot].h=th;
                W.txt[slot].age=++W.clock;
            } else {
                SDL_Rect d; d.x=x; d.y=y; d.w=tw; d.h=th;
                SDL_RenderCopy(W.ren,tex,NULL,&d);
                SDL_DestroyTexture(tex);
                return;
            }
        }
        { SDL_Rect d; d.x=x; d.y=y; d.w=tw; d.h=th;
          SDL_RenderCopy(W.ren,tex,NULL,&d); }
        return;
    }
#endif
    w_bitmap_text(s,len,x,y,c,size);
}

/* ---- geometry ------------------------------------------------------------ */
static void w_fill_circle(int cx,int cy,int r){
    if(r<0) return;
    for(int dy=-r;dy<=r;dy++){
        int dx=(int)(sqrt((double)r*(double)r-(double)dy*(double)dy)+0.5);
        SDL_RenderDrawLine(W.ren,cx-dx,cy+dy,cx+dx,cy+dy);
    }
}
static void w_circle_outline(int cx,int cy,int r){
    int x=r,y=0,err=1-r;
    while(x>=y){
        SDL_RenderDrawPoint(W.ren,cx+x,cy+y); SDL_RenderDrawPoint(W.ren,cx+y,cy+x);
        SDL_RenderDrawPoint(W.ren,cx-y,cy+x); SDL_RenderDrawPoint(W.ren,cx-x,cy+y);
        SDL_RenderDrawPoint(W.ren,cx-x,cy-y); SDL_RenderDrawPoint(W.ren,cx-y,cy-x);
        SDL_RenderDrawPoint(W.ren,cx+y,cy-x); SDL_RenderDrawPoint(W.ren,cx+x,cy-y);
        y++;
        if(err<0) err+=2*y+1;
        else { x--; err+=2*(y-x)+1; }
    }
}
static void w_fill_poly(const double *pts,int n){
    if(n<3) return;
    double miny=pts[1],maxy=pts[1];
    for(int i=1;i<n;i++){ if(pts[i*2+1]<miny) miny=pts[i*2+1];
                          if(pts[i*2+1]>maxy) maxy=pts[i*2+1]; }
    double xs[W_MAXPOLY];
    for(int y=(int)floor(miny);y<=(int)ceil(maxy);y++){
        int cnt=0;
        double yy=y+0.5;
        for(int i=0,j=n-1;i<n;j=i++){
            double y1=pts[j*2+1], y2=pts[i*2+1];
            if((y1<=yy && y2>yy)||(y2<=yy && y1>yy)){
                double t=(yy-y1)/(y2-y1);
                if(cnt<W_MAXPOLY) xs[cnt++]=pts[j*2]+t*(pts[i*2]-pts[j*2]);
            }
        }
        for(int a=1;a<cnt;a++){ double k=xs[a]; int b=a-1;
            while(b>=0&&xs[b]>k){ xs[b+1]=xs[b]; b--; } xs[b+1]=k; }
        for(int a=0;a+1<cnt;a+=2)
            SDL_RenderDrawLine(W.ren,(int)(xs[a]+0.5),y,(int)(xs[a+1]+0.5),y);
    }
}

/* ---- lifecycle ----------------------------------------------------------- */
static void w_shutdown(void){
    if(!W.started) return;
    w_drop_text_cache();
    w_drop_img_cache();
#ifndef LUC_NO_TTF
    w_drop_fonts();
#endif
    if(W.ren){ SDL_DestroyRenderer(W.ren); W.ren=NULL; }
    if(W.win){ SDL_DestroyWindow(W.win); W.win=NULL; }
    W.started=0; W.running=0;
}
static void w_atexit(void){ w_shutdown(); }

/* ==========================  LUC-facing functions  ======================== */

LFN(f_w_start){ UNUSED_SELF;
    if(W.started) luc_error("window: already started");
    const char *title = nargs>=1? checkstr(L,base,nargs,0,"start")->s : "LUC";
    int ww = nargs>=2? checkint(L,base,nargs,1,"start") : 800;
    int hh = nargs>=3? checkint(L,base,nargs,2,"start") : 600;
    if(ww<1) ww=1; if(hh<1) hh=1;
    W.win=SDL_CreateWindow(title,SDL_WINDOWPOS_CENTERED,SDL_WINDOWPOS_CENTERED,
                           ww,hh,SDL_WINDOW_SHOWN);
    if(!W.win) luc_error("window: cannot create window (%s)",SDL_GetError());
    W.ren=SDL_CreateRenderer(W.win,-1,SDL_RENDERER_ACCELERATED|SDL_RENDERER_PRESENTVSYNC);
    if(!W.ren) W.ren=SDL_CreateRenderer(W.win,-1,SDL_RENDERER_SOFTWARE);
    if(!W.ren){ SDL_DestroyWindow(W.win); W.win=NULL;
                luc_error("window: cannot create renderer (%s)",SDL_GetError()); }
    SDL_SetRenderDrawBlendMode(W.ren,SDL_BLENDMODE_BLEND);
    W.w=ww; W.h=hh; W.running=1; W.started=1; W.vsync=1;
    W.target_fps=0; W.delta=1.0/60.0;
    W.last_frame=SDL_GetTicks();
    W.start_time=luc_now();
    W.clock=0; W.wheel_dy=0; W.wheel_dx=0; W.textlen=0; W.textbuf[0]=0;
    memset(W.keys_prev,0,sizeof W.keys_prev);
    memset(W.keys_curr,0,sizeof W.keys_curr);
    W.mouse_state=0; W.mouse_prev=0;
    SDL_StartTextInput();
#ifndef LUC_NO_TTF
    W.fontsize=16; w_find_font();
#endif
    { static int once=0; if(!once){ once=1; atexit(w_atexit); } }
    RET(0,mkbool(1)); return 1;
}

LFN(f_w_close){ UNUSED_SELF; (void)base;(void)nargs;(void)L;
    w_shutdown(); return 0;
}

LFN(f_w_running){ UNUSED_SELF; (void)base;(void)nargs;
    if(!W.started){ RET(0,mkbool(0)); return 1; }
    SDL_Event e;
    while(SDL_PollEvent(&e)){
        switch(e.type){
            case SDL_QUIT: W.running=0; break;
            case SDL_WINDOWEVENT:
                if(e.window.event==SDL_WINDOWEVENT_CLOSE) W.running=0;
                else if(e.window.event==SDL_WINDOWEVENT_SIZE_CHANGED||
                        e.window.event==SDL_WINDOWEVENT_RESIZED){
                    W.w=e.window.data1; W.h=e.window.data2;
                }
                break;
            case SDL_MOUSEWHEEL:
                W.wheel_dy+=e.wheel.y; W.wheel_dx+=e.wheel.x; break;
            case SDL_TEXTINPUT: {
                int n=(int)strlen(e.text.text);
                if(W.textlen+n < (int)sizeof W.textbuf-1){
                    memcpy(W.textbuf+W.textlen,e.text.text,(size_t)n);
                    W.textlen+=n; W.textbuf[W.textlen]=0;
                }
                break; }
            default: break;
        }
    }
    { const Uint8 *ks=SDL_GetKeyboardState(NULL);
      memcpy(W.keys_curr,ks,W_KEYS); }
    W.mouse_state=SDL_GetMouseState(&W.mouse_x,&W.mouse_y);
    SDL_GetWindowSize(W.win,&W.w,&W.h);
    RET(0,mkbool(W.running)); return 1;
}

LFN(f_w_update){ UNUSED_SELF; (void)base;(void)nargs;(void)L;
    w_need();
    SDL_RenderPresent(W.ren);
    Uint32 now=SDL_GetTicks();
    if(W.target_fps>0){
        Uint32 want=(Uint32)(1000/W.target_fps);
        Uint32 el=now-W.last_frame;
        if(el<want){ SDL_Delay(want-el); now=SDL_GetTicks(); }
    }
    double d=(double)(now-W.last_frame)/1000.0;
    if(d<=0) d=0.0001; if(d>0.25) d=0.25;
    W.delta=d; W.last_frame=now;
    memcpy(W.keys_prev,W.keys_curr,W_KEYS);
    W.mouse_prev=W.mouse_state;
    W.wheel_dy=0; W.wheel_dx=0;
    W.textlen=0; W.textbuf[0]=0;
    W.clock++;
    return 0;
}

LFN(f_w_title){ UNUSED_SELF;
    w_need();
    SDL_SetWindowTitle(W.win,checkstr(L,base,nargs,0,"title")->s);
    return 0;
}
LFN(f_w_size){ UNUSED_SELF; (void)nargs;
    w_need(); RET(0,mknum(W.w)); RET(1,mknum(W.h)); return 2;
}
LFN(f_w_resize){ UNUSED_SELF;
    w_need();
    int ww=checkint(L,base,nargs,0,"resize"), hh=checkint(L,base,nargs,1,"resize");
    if(ww<1)ww=1; if(hh<1)hh=1;
    SDL_SetWindowSize(W.win,ww,hh); W.w=ww; W.h=hh;
    return 0;
}
LFN(f_w_quit){ UNUSED_SELF; (void)L;(void)base;(void)nargs;
    W.running=0; return 0;
}

/* ---- drawing ------------------------------------------------------------- */
LFN(f_w_clear){ UNUSED_SELF;
    w_need();
    SDL_Color c = nargs>=1? w_color(w_argc(L,base,nargs,0)) : (SDL_Color){0,0,0,255};
    SDL_SetRenderDrawBlendMode(W.ren,SDL_BLENDMODE_NONE);
    SDL_SetRenderDrawColor(W.ren,c.r,c.g,c.b,255);
    SDL_RenderClear(W.ren);
    SDL_SetRenderDrawBlendMode(W.ren,SDL_BLENDMODE_BLEND);
    return 0;
}
LFN(f_w_pixel){ UNUSED_SELF;
    w_need();
    int x=checkint(L,base,nargs,0,"pixel"), y=checkint(L,base,nargs,1,"pixel");
    w_setcolor(w_color(w_argc(L,base,nargs,2)));
    SDL_RenderDrawPoint(W.ren,x,y); return 0;
}
LFN(f_w_line){ UNUSED_SELF;
    w_need();
    int x1=checkint(L,base,nargs,0,"line"), y1=checkint(L,base,nargs,1,"line");
    int x2=checkint(L,base,nargs,2,"line"), y2=checkint(L,base,nargs,3,"line");
    w_setcolor(w_color(w_argc(L,base,nargs,4)));
    SDL_RenderDrawLine(W.ren,x1,y1,x2,y2); return 0;
}
LFN(f_w_rect){ UNUSED_SELF;
    w_need();
    SDL_Rect r;
    r.x=checkint(L,base,nargs,0,"rect"); r.y=checkint(L,base,nargs,1,"rect");
    r.w=checkint(L,base,nargs,2,"rect"); r.h=checkint(L,base,nargs,3,"rect");
    w_setcolor(w_color(w_argc(L,base,nargs,4)));
    SDL_RenderFillRect(W.ren,&r); return 0;
}
LFN(f_w_rect_outline){ UNUSED_SELF;
    w_need();
    SDL_Rect r;
    r.x=checkint(L,base,nargs,0,"rect_outline"); r.y=checkint(L,base,nargs,1,"rect_outline");
    r.w=checkint(L,base,nargs,2,"rect_outline"); r.h=checkint(L,base,nargs,3,"rect_outline");
    w_setcolor(w_color(w_argc(L,base,nargs,4)));
    int th = nargs>=6? checkint(L,base,nargs,5,"rect_outline") : 1;
    for(int i=0;i<(th<1?1:th);i++){
        SDL_Rect q; q.x=r.x+i; q.y=r.y+i; q.w=r.w-2*i; q.h=r.h-2*i;
        if(q.w<=0||q.h<=0) break;
        SDL_RenderDrawRect(W.ren,&q);
    }
    return 0;
}
LFN(f_w_circle){ UNUSED_SELF;
    w_need();
    int x=checkint(L,base,nargs,0,"circle"), y=checkint(L,base,nargs,1,"circle");
    int r=checkint(L,base,nargs,2,"circle");
    w_setcolor(w_color(w_argc(L,base,nargs,3)));
    w_fill_circle(x,y,r); return 0;
}
LFN(f_w_circle_outline){ UNUSED_SELF;
    w_need();
    int x=checkint(L,base,nargs,0,"circle_outline"), y=checkint(L,base,nargs,1,"circle_outline");
    int r=checkint(L,base,nargs,2,"circle_outline");
    w_setcolor(w_color(w_argc(L,base,nargs,3)));
    w_circle_outline(x,y,r); return 0;
}
LFN(f_w_triangle){ UNUSED_SELF;
    w_need();
    double p[6];
    for(int i=0;i<6;i++) p[i]=checknum(L,base,nargs,i,"triangle");
    w_setcolor(w_color(w_argc(L,base,nargs,6)));
    w_fill_poly(p,3); return 0;
}
LFN(f_w_polygon){ UNUSED_SELF;
    w_need();
    Table *t=checktab(L,base,nargs,0,"polygon");
    int n=t->o.type==LT_LIST? t->alen : tab_len(t);
    if(n<6 || (n&1)) luc_error("window.polygon: need a flat list [x1,y1,x2,y2,...]");
    int np=n/2; if(np>W_MAXPOLY) np=W_MAXPOLY;
    double *p=(double*)lmalloc(sizeof(double)*(size_t)np*2);
    for(int i=0;i<np*2;i++){
        Value v=tab_get(t,mknum((double)(i+1)));
        p[i]= v.t==LT_NUM? v.u.n : 0;
    }
    w_setcolor(w_color(w_argc(L,base,nargs,1)));
    int outline = nargs>=3 && truthy(w_argc(L,base,nargs,2));
    if(outline){
        for(int i=0;i<np;i++){
            int j=(i+1)%np;
            SDL_RenderDrawLine(W.ren,(int)p[i*2],(int)p[i*2+1],(int)p[j*2],(int)p[j*2+1]);
        }
    } else w_fill_poly(p,np);
    free(p);
    return 0;
}
LFN(f_w_text){ UNUSED_SELF;
    w_need();
    Str *s=checkstr(L,base,nargs,0,"text");
    int x=checkint(L,base,nargs,1,"text"), y=checkint(L,base,nargs,2,"text");
    SDL_Color c=w_color(w_argc(L,base,nargs,3));
    int size = nargs>=5? checkint(L,base,nargs,4,"text") : 16;
#ifndef LUC_NO_TTF
    if(nargs<5) size=W.fontsize;
#endif
    if(size<4) size=4;
    w_text(s->s,s->len,x,y,c,size);
    return 0;
}
LFN(f_w_text_size){ UNUSED_SELF;
    Str *s=checkstr(L,base,nargs,0,"text_size");
    int size = nargs>=2? checkint(L,base,nargs,1,"text_size") : 16;
#ifndef LUC_NO_TTF
    if(nargs<2) size=W.fontsize;
    { TTF_Font *f=w_font(size);
      if(f){ int tw=0,th=0; TTF_SizeUTF8(f,s->s,&tw,&th);
             RET(0,mknum(tw)); RET(1,mknum(th)); return 2; } }
#endif
    { int tw,th; w_bitmap_size(s->s,s->len,size,&tw,&th);
      RET(0,mknum(tw)); RET(1,mknum(th)); }
    return 2;
}
LFN(f_w_font){ UNUSED_SELF;
#ifdef LUC_NO_TTF
    (void)L;(void)base;(void)nargs;
    RET(0,mkbool(0)); RET(1,cstrv("built without SDL2_ttf")); return 2;
#else
    if(nargs>=1 && w_argc(L,base,nargs,0).t==LT_STR){
        Str *p=checkstr(L,base,nargs,0,"font");
        FILE *fp=fopen(p->s,"rb");
        if(!fp){ RET(0,mkbool(0)); RET(1,cstrv("cannot open font file")); return 2; }
        fclose(fp);
        w_drop_fonts(); w_drop_text_cache();
        snprintf(W.fontpath,sizeof W.fontpath,"%s",p->s);
    }
    if(nargs>=2) W.fontsize=checkint(L,base,nargs,1,"font");
    else if(nargs==1 && w_argc(L,base,nargs,0).t==LT_NUM)
        W.fontsize=checkint(L,base,nargs,0,"font");
    if(W.fontsize<4) W.fontsize=4;
    RET(0,mkbool(1)); return 1;
#endif
}
LFN(f_w_image){ UNUSED_SELF;
    w_need();
    Str *p=checkstr(L,base,nargs,0,"image");
    int x=checkint(L,base,nargs,1,"image"), y=checkint(L,base,nargs,2,"image");
    int iw=0,ih=0;
    SDL_Texture *t=w_image(p->s,&iw,&ih);
    if(!t){
#ifndef LUC_NO_IMAGE
        luc_error("window.image: cannot load '%s' (%s)",p->s,IMG_GetError());
#else
        luc_error("window.image: cannot load '%s' (built without SDL2_image; "
                  "only .bmp is supported)",p->s);
#endif
    }
    SDL_Rect d; d.x=x; d.y=y;
    d.w = nargs>=4? checkint(L,base,nargs,3,"image") : iw;
    d.h = nargs>=5? checkint(L,base,nargs,4,"image") : ih;
    if(nargs>=6){
        double ang=checknum(L,base,nargs,5,"image");
        SDL_RenderCopyEx(W.ren,t,NULL,&d,ang,NULL,SDL_FLIP_NONE);
    } else SDL_RenderCopy(W.ren,t,NULL,&d);
    return 0;
}
LFN(f_w_image_size){ UNUSED_SELF;
    w_need();
    Str *p=checkstr(L,base,nargs,0,"image_size");
    int iw=0,ih=0;
    if(!w_image(p->s,&iw,&ih)){ RET(0,NIL); RET(1,cstrv("cannot load image")); return 2; }
    RET(0,mknum(iw)); RET(1,mknum(ih)); return 2;
}
LFN(f_w_clip){ UNUSED_SELF;
    w_need();
    if(nargs==0){ SDL_RenderSetClipRect(W.ren,NULL); return 0; }
    SDL_Rect r;
    r.x=checkint(L,base,nargs,0,"clip"); r.y=checkint(L,base,nargs,1,"clip");
    r.w=checkint(L,base,nargs,2,"clip"); r.h=checkint(L,base,nargs,3,"clip");
    SDL_RenderSetClipRect(W.ren,&r); return 0;
}

/* ---- input --------------------------------------------------------------- */
static int w_keystate(LucState *L,int base,int nargs,int mode){
    const char *n=checkstr(L,base,nargs,0,"key")->s;
    SDL_Scancode sc[2]; int k=w_scancodes(n,sc);
    for(int i=0;i<k;i++){
        int cur=W.keys_curr[sc[i]], prv=W.keys_prev[sc[i]];
        if(mode==0 && cur) return 1;
        if(mode==1 && cur && !prv) return 1;
        if(mode==2 && !cur && prv) return 1;
    }
    return 0;
}
LFN(f_w_key){ UNUSED_SELF; RET(0,mkbool(w_keystate(L,base,nargs,0))); return 1; }
LFN(f_w_key_pressed){ UNUSED_SELF; RET(0,mkbool(w_keystate(L,base,nargs,1))); return 1; }
LFN(f_w_key_released){ UNUSED_SELF; RET(0,mkbool(w_keystate(L,base,nargs,2))); return 1; }

LFN(f_w_mouse){ UNUSED_SELF; (void)nargs;
    RET(0,mknum(W.mouse_x)); RET(1,mknum(W.mouse_y));
    RET(2,mkbool(W.mouse_state&SDL_BUTTON(SDL_BUTTON_LEFT)));
    RET(3,mkbool(W.mouse_state&SDL_BUTTON(SDL_BUTTON_RIGHT)));
    RET(4,mkbool(W.mouse_state&SDL_BUTTON(SDL_BUTTON_MIDDLE)));
    return 5;
}
static Uint32 w_mbutton(LucState *L,int base,int nargs){
    if(nargs<1) return SDL_BUTTON(SDL_BUTTON_LEFT);
    const char *b=checkstr(L,base,nargs,0,"mouse")->s;
    if(strcmp(b,"right")==0)  return SDL_BUTTON(SDL_BUTTON_RIGHT);
    if(strcmp(b,"middle")==0) return SDL_BUTTON(SDL_BUTTON_MIDDLE);
    return SDL_BUTTON(SDL_BUTTON_LEFT);
}
LFN(f_w_mouse_pressed){ UNUSED_SELF;
    Uint32 m=w_mbutton(L,base,nargs);
    RET(0,mkbool((W.mouse_state&m)&&!(W.mouse_prev&m))); return 1;
}
LFN(f_w_mouse_released){ UNUSED_SELF;
    Uint32 m=w_mbutton(L,base,nargs);
    RET(0,mkbool(!(W.mouse_state&m)&&(W.mouse_prev&m))); return 1;
}
LFN(f_w_mouse_wheel){ UNUSED_SELF; (void)nargs;
    RET(0,mknum(W.wheel_dy)); RET(1,mknum(W.wheel_dx)); return 2;
}
LFN(f_w_text_input){ UNUSED_SELF; (void)nargs;
    RET(0,strv(W.textbuf,W.textlen)); return 1;
}
LFN(f_w_cursor){ UNUSED_SELF;
    int show = nargs<1 || truthy(w_argc(L,base,nargs,0));
    SDL_ShowCursor(show?SDL_ENABLE:SDL_DISABLE); return 0;
}

/* ---- timing -------------------------------------------------------------- */
LFN(f_w_fps){ UNUSED_SELF;
    if(nargs==0){ RET(0,mknum(W.delta>0?1.0/W.delta:0)); return 1; }
    int n=checkint(L,base,nargs,0,"fps");
    W.target_fps = n>0? n : 0;
    return 0;
}
LFN(f_w_delta){ UNUSED_SELF; (void)base;(void)nargs;
    RET(0,mknum(W.delta)); return 1;
}
LFN(f_w_time){ UNUSED_SELF; (void)base;(void)nargs;
    RET(0,mknum(W.started? luc_now()-W.start_time : 0)); return 1;
}

/* ---- advanced ------------------------------------------------------------ */
LFN(f_w_fullscreen){ UNUSED_SELF;
    w_need();
    int on = nargs<1 || truthy(w_argc(L,base,nargs,0));
    if(SDL_SetWindowFullscreen(W.win,on?SDL_WINDOW_FULLSCREEN_DESKTOP:0)!=0){
        RET(0,mkbool(0)); RET(1,cstrv(SDL_GetError())); return 2;
    }
    W.fullscreen=on;
    SDL_GetWindowSize(W.win,&W.w,&W.h);
    RET(0,mkbool(1)); return 1;
}
LFN(f_w_vsync){ UNUSED_SELF;
    w_need();
    int on = nargs<1 || truthy(w_argc(L,base,nargs,0));
#if SDL_VERSION_ATLEAST(2,0,18)
    if(SDL_RenderSetVSync(W.ren,on)!=0){
        RET(0,mkbool(0)); RET(1,cstrv(SDL_GetError())); return 2;
    }
    W.vsync=on; RET(0,mkbool(1)); return 1;
#else
    (void)on;
    RET(0,mkbool(0));
    RET(1,cstrv("vsync toggling needs SDL 2.0.18+"));
    return 2;
#endif
}
LFN(f_w_icon){ UNUSED_SELF;
    w_need();
    Str *p=checkstr(L,base,nargs,0,"icon");
    SDL_Surface *s;
#ifndef LUC_NO_IMAGE
    s=IMG_Load(p->s);
#else
    s=SDL_LoadBMP(p->s);
#endif
    if(!s){ RET(0,mkbool(0)); RET(1,cstrv("cannot load icon")); return 2; }
    SDL_SetWindowIcon(W.win,s);
    SDL_FreeSurface(s);
    RET(0,mkbool(1)); return 1;
}
LFN(f_w_screenshot){ UNUSED_SELF;
    w_need();
    Str *p=checkstr(L,base,nargs,0,"screenshot");
    SDL_Surface *s=SDL_CreateRGBSurfaceWithFormat(0,W.w,W.h,32,SDL_PIXELFORMAT_ARGB8888);
    if(!s){ RET(0,mkbool(0)); RET(1,cstrv(SDL_GetError())); return 2; }
    if(SDL_RenderReadPixels(W.ren,NULL,SDL_PIXELFORMAT_ARGB8888,s->pixels,s->pitch)!=0){
        SDL_FreeSurface(s);
        RET(0,mkbool(0)); RET(1,cstrv(SDL_GetError())); return 2;
    }
    int rc;
#ifndef LUC_NO_IMAGE
    if(p->len>4 && strcmp(p->s+p->len-4,".bmp")==0) rc=SDL_SaveBMP(s,p->s);
    else rc=IMG_SavePNG(s,p->s);
#else
    rc=SDL_SaveBMP(s,p->s);
#endif
    SDL_FreeSurface(s);
    if(rc!=0){ RET(0,mkbool(0)); RET(1,cstrv(SDL_GetError())); return 2; }
    RET(0,mkbool(1)); return 1;
}

/* ---- module table -------------------------------------------------------- */
Value lucL_window_module(void){
    static int sdl_ready=0;
    if(!sdl_ready){
        SDL_SetMainReady();
        if(SDL_Init(SDL_INIT_VIDEO|SDL_INIT_TIMER)!=0)
            luc_error("window: SDL2 could not initialise (%s)",SDL_GetError());
#ifndef LUC_NO_TTF
        if(TTF_Init()==0) W.ttf_ok=1;
#endif
#ifndef LUC_NO_IMAGE
        IMG_Init(IMG_INIT_PNG|IMG_INIT_JPG);
#endif
        sdl_ready=1;
    }
    Table *t=tab_new(0);
    reg(t,"start",f_w_start);         reg(t,"close",f_w_close);
    reg(t,"running",f_w_running);     reg(t,"update",f_w_update);
    reg(t,"title",f_w_title);         reg(t,"size",f_w_size);
    reg(t,"resize",f_w_resize);       reg(t,"quit",f_w_quit);

    reg(t,"clear",f_w_clear);         reg(t,"pixel",f_w_pixel);
    reg(t,"line",f_w_line);           reg(t,"rect",f_w_rect);
    reg(t,"rect_outline",f_w_rect_outline);
    reg(t,"circle",f_w_circle);       reg(t,"circle_outline",f_w_circle_outline);
    reg(t,"triangle",f_w_triangle);   reg(t,"polygon",f_w_polygon);
    reg(t,"text",f_w_text);           reg(t,"text_size",f_w_text_size);
    reg(t,"font",f_w_font);           reg(t,"image",f_w_image);
    reg(t,"image_size",f_w_image_size); reg(t,"clip",f_w_clip);

    reg(t,"key",f_w_key);             reg(t,"key_pressed",f_w_key_pressed);
    reg(t,"key_released",f_w_key_released);
    reg(t,"mouse",f_w_mouse);         reg(t,"mouse_pressed",f_w_mouse_pressed);
    reg(t,"mouse_released",f_w_mouse_released);
    reg(t,"mouse_wheel",f_w_mouse_wheel);
    reg(t,"text_input",f_w_text_input); reg(t,"cursor",f_w_cursor);

    reg(t,"fps",f_w_fps);             reg(t,"delta",f_w_delta);
    reg(t,"time",f_w_time);

    reg(t,"fullscreen",f_w_fullscreen); reg(t,"vsync",f_w_vsync);
    reg(t,"icon",f_w_icon);           reg(t,"screenshot",f_w_screenshot);

    tab_set(t,cstrv("_VERSION"),cstrv("luc.window 1.0 (SDL2)"));
#ifndef LUC_NO_TTF
    tab_set(t,cstrv("has_ttf"),mkbool(W.ttf_ok));
#else
    tab_set(t,cstrv("has_ttf"),mkbool(0));
#endif
#ifndef LUC_NO_IMAGE
    tab_set(t,cstrv("has_image"),mkbool(1));
#else
    tab_set(t,cstrv("has_image"),mkbool(0));
#endif
    return mkobj(LT_TABLE,t);
}
#else  /* !LUC_WINDOW: stub that reports the build-time error */

Value lucL_window_module(void){
    luc_error("module 'window' is not available: this build of LUC has no "
              "SDL2 support.\nRebuild with: make luc-window");
    return NIL;
}
#endif /* LUC_WINDOW */
