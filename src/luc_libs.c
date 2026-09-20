/* luc_libs.c - all standard libraries merged into one translation unit */
/* update 2026-09-01: comment cleanup */
#if defined(_WIN32) && !defined(WIN32_LEAN_AND_MEAN)
#define WIN32_LEAN_AND_MEAN      /* keep windows.h lean: no winsock.h v1, so
                                    luc_lib_net can include winsock2 below */
#endif
#include "luc.h"


/* luc_lib_base.c */
/* luc_lib_base.c -- base library: print, tostring, pcall, require, xpcall... */
/* base */

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

/* require loads third-party modules from disk (script dir, cwd, luc_modules, LUC_PATH).
   The built-in system libraries (window, ai, json) are NOT served here:
   require("window") always picks up the user's own window module, never the system one. */
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
/* import loads the system libraries that ship with LUC itself:
     import window        import ai        import json        import net
   plus the short-name form:  import window("w")
   Third-party modules never go through import - they use require:
     create mywin = require("mywin")                                     */
LFN(f_import){ UNUSED_SELF;
    Str *name=checkstr(L,base,nargs,0,"import");
    char keybuf[512]; snprintf(keybuf,sizeof keybuf,"system:%s",name->s);
    Value key=cstrv(keybuf);            /* separate cache slot from require() */
    Value cached=tab_get(V.loaded,key);
    if(cached.t!=LT_NIL){ RET(0,cached); return 1; }
    Value m=NIL;
    if(strcmp(name->s,"window")==0){
        m=lucL_window_module();         /* throws when built without SDL2 */
    }else if(strcmp(name->s,"json")==0){
        m=lucL_json_module();
    }else if(strcmp(name->s,"net")==0){
        m=lucL_net_module(L);
    }else if(strcmp(name->s,"discord")==0){
        int len=0; char found[1024];
        char *src=find_system_module(name->s,&len,found,sizeof found);
        if(!src) luc_error("module 'discord' not found\ninstall it with: luc install discord");
        int scratch=base+nargs+2;
        ensure_stack(L,scratch+16);
        Closure *cl=luc_compile(src,len,found);
        free(src);
        L->stack[scratch]=mkobj(LT_FUNC,cl);
        vm_call(L,scratch,0,1);
        m=L->stack[scratch];
        if(m.t==LT_NIL) m=mkbool(1);
    }else if(strcmp(name->s,"ai")==0){
        int len=0; char found[1024];
        char *src=find_system_module(name->s,&len,found,sizeof found);
        if(!src) luc_error("module 'ai' not found\ninstall it with: luc install ai");
        int scratch=base+nargs+2;
        ensure_stack(L,scratch+16);
        Closure *cl=luc_compile(src,len,found);
        free(src);
        L->stack[scratch]=mkobj(LT_FUNC,cl);
        vm_call(L,scratch,0,1);
        m=L->stack[scratch];
        if(m.t==LT_NIL) m=mkbool(1);
    }else{
        luc_error("module '%s' is not a LUC system library (system: window, ai, json, net, discord)\n"
                  "third-party modules use: create %s = require(\"%s\")",
                  name->s,name->s,name->s);
    }
    tab_set(V.loaded,key,m);
    RET(0,m); return 1;
}
LFN(f_setmetatable){ UNUSED_SELF;
    Value t=AR(0);
    if(t.t!=LT_TABLE && t.t!=LT_LIST)
        luc_error("bad argument #1 to 'setmetatable' (table expected)");
    Table *mt=NULL;
    if(AR(1).t==LT_TABLE || AR(1).t==LT_LIST) mt=AS_TAB(AR(1));
    AS_TAB(t)->meta=mt;
    RET(0,t); return 1;
}
LFN(f_getmetatable){ UNUSED_SELF;
    Value t=AR(0);
    if((t.t!=LT_TABLE && t.t!=LT_LIST) || !AS_TAB(t)->meta){ RET(0,NIL); return 1; }
    RET(0,mkobj(t.t,AS_TAB(t)->meta)); return 1;
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
    reg(g,"__import",f_import);
    reg(g,"setmetatable",f_setmetatable);
    reg(g,"getmetatable",f_getmetatable);
    reg(g,"xpcall",f_xpcall);
    tab_set(g,cstrv("_VERSION"),cstrv(LUC_VERSION));
    tab_set(g,cstrv("_G"),mkobj(LT_TABLE,g));
    tab_set(g,cstrv("package"),mkobj(LT_TABLE,V.loaded));
}


/* luc_lib_buffer.c */
/* luc_lib_buffer.c -- buffer library (binary data) */
/* buffer */
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


/* luc_lib_coro.c */
/* luc_lib_coro.c -- coroutine library + task library */
/* coroutine */
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

/* task */
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
    if(!g_yp || g_yp->co!=L){
        /* main thread: sleep in slices, pumping due tasks so spawned
         * tasks keep running while main waits (servers/bots need this) */
        double t0=luc_now(), end=t0+n;
        for(;;){
            sched_poll();
            double now=luc_now();
            if(now>=end) break;
            double left=end-now;
            luc_sleep(left<0.01?left:0.01);
        }
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
/* task */
    Table *tk=newlib("task");
    reg(tk,"wait",f_task_wait);   reg(tk,"spawn",f_task_spawn);
    reg(tk,"delay",f_task_delay); reg(tk,"defer",f_task_defer);
    reg(tk,"cancel",f_task_cancel);
}


/* luc_lib_io.c */
/* luc_lib_io.c -- io library + file methods (io.popen moved here) */
/* io */
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

/* 15. library registration */

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


/* luc_lib_json.c */
/* luc_lib_json.c -- JSON module (loaded with require "json") */
/* JSON */
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


/* luc_lib_list.c */
/* luc_lib_list.c -- list methods + table library (they share sort/concat) */
/* list methods */
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

/* table */
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
/* table */
    Table *t=newlib("table");
    reg(t,"insert",f_tbl_insert);  reg(t,"remove",f_tbl_remove);
    reg(t,"concat",f_tbl_concat);  reg(t,"unpack",f_unpack);
    reg(t,"sort",f_list_sort);
    reg(t,"move",f_tbl_move);
}


/* luc_lib_math.c */
/* luc_lib_math.c -- math library + bit32 (+ RNG seeding, moved from luc_init) */
/* math */
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

/* bit32 */
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
/* bit32 */
    Table *b=newlib("bit32");
    reg(b,"band",f_b_band);   reg(b,"bor",f_b_bor);     reg(b,"bxor",f_b_bxor);
    reg(b,"bnot",f_b_bnot);   reg(b,"lshift",f_b_lshift);reg(b,"rshift",f_b_rshift);
    reg(b,"arshift",f_b_arshift); reg(b,"btest",f_b_btest);
    reg(b,"bswap",f_b_bswap); reg(b,"extract",f_b_extract); reg(b,"replace",f_b_replace);
}


/* luc_lib_os.c */
/* luc_lib_os.c -- os library (os.execute moved here from the window section) */
/* os */
#if defined(_WIN32)
#  include <windows.h>
#else
#  include <sys/ioctl.h>
#  include <unistd.h>
#endif
LFN(f_os_clock){ UNUSED_SELF; (void)base;(void)nargs;
    RET(0,mknum((double)clock()/(double)CLOCKS_PER_SEC)); return 1; }
LFN(f_os_time){ UNUSED_SELF; (void)base;(void)nargs;
    RET(0,mknum((double)time(NULL))); return 1; }
LFN(f_os_difftime){ UNUSED_SELF;
    double t2=checknum(L,base,nargs,0,"difftime");
    double t1=nargs>=2?checknum(L,base,nargs,1,"difftime"):0;
    RET(0,mknum(t2-t1)); return 1; }
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
LFN(f_os_termwidth){ UNUSED_SELF; (void)base;(void)nargs;
    int cols=80;
#if defined(_WIN32)
    CONSOLE_SCREEN_BUFFER_INFO csbi;
    HANDLE h=GetStdHandle(STD_OUTPUT_HANDLE);
    if(h && h!=INVALID_HANDLE_VALUE && GetConsoleScreenBufferInfo(h,&csbi))
        cols=(int)(csbi.srWindow.Right-csbi.srWindow.Left+1);
#else
    struct winsize ws;
    if(ioctl(1,TIOCGWINSZ,&ws)==0 && ws.ws_col>0) cols=ws.ws_col;
#endif
    RET(0,mknum((double)cols)); return 1;
}
void lucL_open_os(void){
    Table *o=newlib("os");
    reg(o,"clock",f_os_clock); reg(o,"time",f_os_time);  reg(o,"date",f_os_date);
    reg(o,"difftime",f_os_difftime);
    reg(o,"termwidth",f_os_termwidth);
    reg(o,"exit",f_os_exit);   reg(o,"getenv",f_os_getenv);
    reg(o,"remove",f_os_remove);reg(o,"rename",f_os_rename);
    reg(o,"sleep",f_os_sleep);
    reg(o,"execute",f_os_execute);
}


/* luc_lib_string.c */
/* luc_lib_string.c -- string library + Lua-style pattern matching */
/* string */
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
    for(int x=i;x<=j;x++){ RET(n,mknum((double)(unsigned char)s->s[x-1])); n++; }  /* RET() expands its index twice ??? never pass n++ */
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
/* extensions */
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

/* Lua-style pattern matching */
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


/* luc_lib_window.c */
/* luc_lib_window.c -- SDL2 window module (loaded with require "window") build with -DLUC_WINDOW (see Makefile target luc-window) */
#ifdef LUC_WINDOW
#  define SDL_MAIN_HANDLED
#  include <SDL2/SDL.h>
#  ifndef LUC_NO_TTF
#    include <SDL2/SDL_ttf.h>
#  endif
#  ifndef LUC_NO_IMAGE
#    include <SDL2/SDL_image.h>
#  endif
#  ifndef LUC_NO_MIXER
#    include <SDL2/SDL_mixer.h>
#  endif
/* Satellite DLLs (SDL2_ttf / SDL2_image / SDL2_mixer) bind at RUNTIME via
   LoadLibrary/GetProcAddress so the exe starts without them; missing DLLs
   degrade gracefully (bitmap font, BMP-only, no sound) with clean errors.
   dlopen fallback for non-Windows builds. */
#ifdef _WIN32
#  include <windows.h>
#  define W_LIB_H  HMODULE
#  define W_LIB_OPEN(n)  LoadLibraryA(n)
#  define W_LIB_SYM(h,n) GetProcAddress(h,n)
#else
#  include <dlfcn.h>
#  define W_LIB_H  void*
#  define W_LIB_OPEN(n)  dlopen(n,RTLD_NOW)
#  define W_LIB_SYM(h,n) dlsym(h,n)
#endif
#ifndef LUC_NO_TTF
typedef int (*w_TTF_Init_t)(void);
typedef TTF_Font TTF_Font_opaque;
typedef TTF_Font_opaque TTF_Font_w;
typedef TTF_Font_opaque* (*w_TTF_OpenFont_t)(const char*,int);
typedef void (*w_TTF_CloseFont_t)(TTF_Font_opaque*);
typedef SDL_Surface SDL_Surface_w;
typedef SDL_Color SDL_Color_w;
typedef SDL_Surface_w* (*w_TTF_RenderUTF8_Blended_t)(TTF_Font_opaque*,const char*,SDL_Color_w);
typedef int (*w_TTF_SizeUTF8_t)(TTF_Font_opaque*,const char*,int*,int*);
static W_LIB_H w_hTTF=NULL;
static w_TTF_Init_t pTTF_Init=NULL;
static w_TTF_OpenFont_t pTTF_OpenFont=NULL;
static w_TTF_CloseFont_t pTTF_CloseFont=NULL;
static w_TTF_RenderUTF8_Blended_t pTTF_RenderUTF8_Blended=NULL;
static w_TTF_SizeUTF8_t pTTF_SizeUTF8=NULL;
#endif
#ifndef LUC_NO_IMAGE
typedef SDL_Surface_w* (*w_IMG_Load_t)(const char*);

typedef int (*w_IMG_Init_t)(int);
typedef int (*w_IMG_SavePNG_t)(SDL_Surface_w*,const char*);
static W_LIB_H w_hIMG=NULL;
static w_IMG_Load_t pIMG_Load=NULL;

static w_IMG_Init_t pIMG_Init=NULL;
static w_IMG_SavePNG_t pIMG_SavePNG=NULL;
#endif
#ifndef LUC_NO_MIXER
typedef Mix_Chunk Mix_Chunk_opaque;
typedef Mix_Music Mix_Music_opaque;
typedef int (*w_Mix_Init_t)(int);
typedef int (*w_Mix_OpenAudio_t)(int,unsigned short,int,int);
typedef void (*w_Mix_CloseAudio_t)(void);
typedef Mix_Chunk_opaque* (*w_Mix_LoadWAV_t)(const char*);
typedef void (*w_Mix_FreeChunk_t)(Mix_Chunk_opaque*);
typedef int (*w_Mix_PlayChannel_t)(int,Mix_Chunk_opaque*,int);
typedef int (*w_Mix_Volume_t)(int,int);
typedef int (*w_Mix_HaltChannel_t)(int);
typedef Mix_Music_opaque* (*w_Mix_LoadMUS_t)(const char*);
typedef void (*w_Mix_FreeMusic_t)(Mix_Music_opaque*);
typedef int (*w_Mix_PlayMusic_t)(Mix_Music_opaque*,int);
typedef int (*w_Mix_HaltMusic_t)(void);
typedef int (*w_Mix_PauseMusic_t)(void);
typedef void (*w_Mix_ResumeMusic_t)(void);
typedef int (*w_Mix_VolumeMusic_t)(int);

static W_LIB_H w_hMIX=NULL;
static w_Mix_Init_t pMix_Init=NULL;
static w_Mix_OpenAudio_t pMix_OpenAudio=NULL;
static w_Mix_CloseAudio_t pMix_CloseAudio=NULL;
static w_Mix_LoadWAV_t pMix_LoadWAV=NULL;
static w_Mix_FreeChunk_t pMix_FreeChunk=NULL;
static w_Mix_PlayChannel_t pMix_PlayChannel=NULL;
static w_Mix_Volume_t pMix_Volume=NULL;
static w_Mix_HaltChannel_t pMix_HaltChannel=NULL;
static w_Mix_LoadMUS_t pMix_LoadMUS=NULL;
static w_Mix_FreeMusic_t pMix_FreeMusic=NULL;
static w_Mix_PlayMusic_t pMix_PlayMusic=NULL;
static w_Mix_HaltMusic_t pMix_HaltMusic=NULL;
static w_Mix_PauseMusic_t pMix_PauseMusic=NULL;
static w_Mix_ResumeMusic_t pMix_ResumeMusic=NULL;
static w_Mix_VolumeMusic_t pMix_VolumeMusic=NULL;

#endif
#define W_KEYS        SDL_NUM_SCANCODES
#define W_IMGCACHE    64
#define W_TXTCACHE    96
#define W_FONTSLOTS   12
#define W_SNDCACHE    32
#define W_MAXPOLY     256

typedef struct { char path[512]; SDL_Texture *tex; int w,h; unsigned age; } WImg;
#ifndef LUC_NO_MIXER
typedef struct { char path[512]; Mix_Chunk *chunk; unsigned age; } WSnd;
#endif
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
    int has_ttf, has_img, has_mix;   /* satellite DLLs present */
#ifndef LUC_NO_MIXER
    int mix_ok;
    WSnd snd[W_SNDCACHE];
    Mix_Music *music; char musicpath[512];
#endif
#ifndef LUC_NO_TTF
    int   ttf_ok;
    char  fontpath[512];
    int   fontsize;
    WFont fonts[W_FONTSLOTS];
#endif
} W;

/* embedded 5x7 fallback font (ASCII 32..126, column major, LSB = top) */
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

/* helpers */
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

/* key names */
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

/* fonts */
#ifndef LUC_NO_TTF
static const char *W_FONTPATHS[] = {
    "DejaVuSans.ttf",       /* shipped next to luc.exe by the installer */
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
    TTF_Font *f=pTTF_OpenFont(W.fontpath,size);
    if(!f) return NULL;
    int slot=-1;
    for(int i=0;i<W_FONTSLOTS;i++) if(!W.fonts[i].f){ slot=i; break; }
    if(slot<0){ slot=0;
        for(int i=1;i<W_FONTSLOTS;i++) if(W.fonts[i].age<W.fonts[slot].age) slot=i;
        pTTF_CloseFont(W.fonts[slot].f); }
    W.fonts[slot].f=f; W.fonts[slot].size=size; W.fonts[slot].age=++W.clock;
    return f;
}
static void w_drop_fonts(void){
    for(int i=0;i<W_FONTSLOTS;i++){ if(W.fonts[i].f) pTTF_CloseFont(W.fonts[i].f);
                                    W.fonts[i].f=NULL; W.fonts[i].size=0; }
}
#endif

/* texture caches */
static void w_drop_sounds(void);   /* defined in the sound section below */
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
/* AVIF/JPEG-XL decoder DLLs are not shipped with LUC (too heavy for beginners) */
static int w_unshipped_format(const char *path){
    size_t pl=strlen(path);
    if(pl>4){
        char e4[8]; int k;
        for(k=0;k<4 && path[pl-4+k];k++){ int ch=(unsigned char)path[pl-4+k];
            e4[k]=(char)(ch>='A'&&ch<='Z'? ch+32 : ch); }
        e4[k]=0;
        char e5[8];
        if(pl>5){ for(k=0;k<5 && path[pl-5+k];k++){ int ch=(unsigned char)path[pl-5+k];
            e5[k]=(char)(ch>='A'&&ch<='Z'? ch+32 : ch); } e5[k]=0; }
        else e5[0]=0;
        if(!strcmp(e5,".avif") || !strcmp(e4,".jxl") || !strcmp(e4,".jxs")) return 1;
    }
    return 0;
}
static SDL_Texture *w_image(const char *path,int *ow,int *oh){    for(int i=0;i<W_IMGCACHE;i++)
        if(W.img[i].tex && strcmp(W.img[i].path,path)==0){
            W.img[i].age=++W.clock;
            if(ow)*ow=W.img[i].w; if(oh)*oh=W.img[i].h;
            return W.img[i].tex;
        }
    SDL_Surface *s;
#ifndef LUC_NO_IMAGE
    if(w_unshipped_format(path)) return NULL;   /* fail cleanly, caller reports */
    if(W.has_img) s=pIMG_Load(path);
    else s=SDL_LoadBMP(path);                   /* degraded: BMP only */
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

/* text rendering */
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
            SDL_Surface *sf=pTTF_RenderUTF8_Blended(f,s,c);
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

/* geometry */
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

/* lifecycle */
static void w_shutdown(void){
    if(!W.started) return;
    w_drop_text_cache();
    w_drop_img_cache();
#ifndef LUC_NO_MIXER
    w_drop_sounds();
    if(W.mix_ok){ pMix_CloseAudio(); W.mix_ok=0; }
#endif
#ifndef LUC_NO_TTF
    w_drop_fonts();
#endif
    if(W.ren){ SDL_DestroyRenderer(W.ren); W.ren=NULL; }
    if(W.win){ SDL_DestroyWindow(W.win); W.win=NULL; }
    W.started=0; W.running=0;
}
static void w_atexit(void){ w_shutdown(); }

/* LUC-facing functions */

#ifndef LUC_NO_MIXER
static int w_mix_init_done=0;
static void w_audio_init(void){
    if(!W.has_mix) return;
    if(!w_mix_init_done){
        w_mix_init_done=1;
        pMix_Init(MIX_INIT_MP3|MIX_INIT_OGG|MIX_INIT_FLAC|MIX_INIT_MOD|MIX_INIT_MID);
    }
    if(!W.mix_ok && pMix_OpenAudio(44100,MIX_DEFAULT_FORMAT,2,2048)==0) W.mix_ok=1;
}
#endif
static void w_start_impl(const char *title,int ww,int hh){
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
#ifndef LUC_NO_MIXER
    w_audio_init();
#endif
    { static int once=0; if(!once){ once=1; atexit(w_atexit); } }
}
LFN(f_w_start){ UNUSED_SELF;
    if(W.started) luc_error("window: already started");
    const char *title = nargs>=1? checkstr(L,base,nargs,0,"start")->s : "LUC";
    int ww = nargs>=2? checkint(L,base,nargs,1,"start") : 800;
    int hh = nargs>=3? checkint(L,base,nargs,2,"start") : 600;
    w_start_impl(title,ww,hh);
    RET(0,mkbool(1)); return 1;
}

LFN(f_w_close){ UNUSED_SELF; (void)base;(void)nargs;(void)L;
    w_shutdown(); return 0;
}

/* event pump: shared by running() and go() */
static void w_pump(void){
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
}
/* present + fps pacing + delta clock */
static void w_present(void){
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
}
/* per-frame input edge resets */
static void w_frame_end(void){
    memcpy(W.keys_prev,W.keys_curr,W_KEYS);
    W.mouse_prev=W.mouse_state;
    W.wheel_dy=0; W.wheel_dx=0;
    W.textlen=0; W.textbuf[0]=0;
    W.clock++;
}
LFN(f_w_running){ UNUSED_SELF; (void)base;(void)nargs;
    if(!W.started){ RET(0,mkbool(0)); return 1; }
    w_pump();
    RET(0,mkbool(W.running)); return 1;
}

LFN(f_w_update){ UNUSED_SELF; (void)base;(void)nargs;(void)L;
    w_need();
    w_present();
    w_frame_end();
    return 0;
}

/* beginner game loop: go(title, w, h, draw_fn)
   starts the window (unless already started), calls draw_fn(dt) every frame
   at 60 fps, presents, and closes on exit.  The classic
   while/running/update loop keeps working untouched. */
LFN(f_w_go){ UNUSED_SELF;
    const char *title = nargs>=1? checkstr(L,base,nargs,0,"go")->s : "LUC";
    int ww = nargs>=2? checkint(L,base,nargs,1,"go") : 800;
    int hh = nargs>=3? checkint(L,base,nargs,2,"go") : 600;
    Value fn = nargs>=4? AR(3) : NIL;
    if(fn.t!=LT_FUNC && fn.t!=LT_CFUNC)
        luc_error("window.go: argument #4 must be a function like function(dt) ... end");
    int was_started=W.started;
    if(!was_started) w_start_impl(title,ww,hh);
    if(W.target_fps==0) W.target_fps=60;
    int scratch=base+nargs+2;
    ensure_stack(L,scratch+8);
    while(W.started && W.running){
        w_pump();
        if(!W.running) break;
        L->stack[scratch]=fn;
        L->stack[scratch+1]=mknum(W.delta);
        vm_call(L,scratch,1,0);
        w_present();
        w_frame_end();
    }
    if(!was_started) w_shutdown();
    RET(0,mkbool(1)); return 1;
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

/* drawing */
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
static void w_measure(const char *s,int len,int size,int *ow,int *oh){
#ifndef LUC_NO_TTF
    { TTF_Font *f=w_font(size);
      if(f){ pTTF_SizeUTF8(f,s,ow,oh); return; } }
#endif
    w_bitmap_size(s,len,size,ow,oh);
}
LFN(f_w_text_size){ UNUSED_SELF;
    Str *s=checkstr(L,base,nargs,0,"text_size");
    int size = nargs>=2? checkint(L,base,nargs,1,"text_size") : 16;
#ifndef LUC_NO_TTF
    if(nargs<2) size=W.fontsize;
#endif
    { int tw,th; w_measure(s->s,s->len,size,&tw,&th);
      RET(0,mknum(tw)); RET(1,mknum(th)); }
    return 2;
}
/* centered text for beginners: text_center(s, y [, color [, size]]) */
LFN(f_w_text_center){ UNUSED_SELF;
    w_need();
    Str *s=checkstr(L,base,nargs,0,"text_center");
    int y=checkint(L,base,nargs,1,"text_center");
    SDL_Color c=w_color(w_argc(L,base,nargs,2));
    int size = nargs>=4? checkint(L,base,nargs,3,"text_center") : 16;
#ifndef LUC_NO_TTF
    if(nargs<4) size=W.fontsize;
#endif
    if(size<4) size=4;
    int tw=0,th=0; w_measure(s->s,s->len,size,&tw,&th);
    w_text(s->s,s->len,(W.w-tw)/2,y,c,size);
    return 0;
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
        if(w_unshipped_format(p->s))
            luc_error("window.image: '%s' uses AVIF/JPEG-XL, which LUC does not ship - convert it to PNG or JPG",p->s);
#ifndef LUC_NO_IMAGE
        luc_error("window.image: cannot load '%s' (%s)",p->s,W.has_img?SDL_GetError():"SDL2_image not installed (BMP only)");
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
/* beginner sprites: spr = window.sprite(path); window.draw(spr, x, y [, opts])
   opts = { scale = 2, rotate = 45, flip = "x"/"y"/"xy", alpha = 128, center = true } */
LFN(f_w_sprite){ UNUSED_SELF;
    /* preload-friendly: no window needed yet, the texture loads on first draw */
    Str *p=checkstr(L,base,nargs,0,"sprite");
    if(w_unshipped_format(p->s))
        luc_error("window.sprite: '%s' uses AVIF/JPEG-XL, which LUC does not ship - convert it to PNG or JPG",p->s);
    FILE *fp=fopen(p->s,"rb");
    if(!fp) luc_error("window.sprite: cannot open '%s'",p->s);
    fclose(fp);
    Table *t=tab_new(0);
    tab_set(t,cstrv("path"),mkobj(LT_STR,p));
    tab_set(t,cstrv("w"),mknum(0));
    tab_set(t,cstrv("h"),mknum(0));
    RET(0,mkobj(LT_TABLE,t)); return 1;
}
LFN(f_w_draw){ UNUSED_SELF;
    w_need();
    Table *t=checktab(L,base,nargs,0,"draw");
    int x=checkint(L,base,nargs,1,"draw"), y=checkint(L,base,nargs,2,"draw");
    Value pv=tab_get(t,cstrv("path"));
    if(pv.t!=LT_STR)
        luc_error("window.draw: argument #1 is not a sprite (make one with window.sprite(path))");
    int iw=0,ih=0;
    SDL_Texture *tex=w_image(AS_STR(pv)->s,&iw,&ih);
    if(!tex){
        if(w_unshipped_format(AS_STR(pv)->s))
            luc_error("window.draw: '%s' uses AVIF/JPEG-XL, which LUC does not ship - convert it to PNG or JPG",AS_STR(pv)->s);
#ifndef LUC_NO_IMAGE
        luc_error("window.draw: cannot load '%s' (%s)",AS_STR(pv)->s,W.has_img?SDL_GetError():"SDL2_image not installed (BMP only)");
#else
        luc_error("window.draw: cannot load '%s' (built without SDL2_image; only .bmp is supported)",AS_STR(pv)->s);
#endif
    }
    /* fill in sprite size on first draw (sprite() preloads without a window) */
    tab_set(t,cstrv("w"),mknum((double)iw));
    tab_set(t,cstrv("h"),mknum((double)ih));
    double scale=1.0, angle=0.0; int alpha=255, flipm=0, centered=0;
    if(nargs>=4 && AR(3).t==LT_TABLE){
        Table *o=AS_TAB(AR(3)); Value v;
        v=tab_get(o,cstrv("scale"));  if(v.t==LT_NUM) scale=v.u.n;
        v=tab_get(o,cstrv("rotate")); if(v.t==LT_NUM) angle=v.u.n;
        v=tab_get(o,cstrv("alpha"));
        if(v.t==LT_NUM){ alpha=(int)v.u.n; if(alpha<0)alpha=0; if(alpha>255)alpha=255; }
        v=tab_get(o,cstrv("flip"));
        if(v.t==LT_STR){
            if(!strcmp(AS_STR(v)->s,"x")) flipm|=SDL_FLIP_HORIZONTAL;
            else if(!strcmp(AS_STR(v)->s,"y")) flipm|=SDL_FLIP_VERTICAL;
            else if(!strcmp(AS_STR(v)->s,"xy")) flipm|=SDL_FLIP_HORIZONTAL|SDL_FLIP_VERTICAL;
        }
        v=tab_get(o,cstrv("center")); if(truthy(v)) centered=1;
    }
    SDL_Rect d;
    d.w=(int)(iw*scale); d.h=(int)(ih*scale);
    if(d.w<1)d.w=1; if(d.h<1)d.h=1;
    d.x=centered? x-d.w/2 : x;
    d.y=centered? y-d.h/2 : y;
    if(alpha<255) SDL_SetTextureAlphaMod(tex,(Uint8)alpha);
    if(angle!=0.0 || flipm || centered){
        SDL_Point c; c.x=d.w/2; c.y=d.h/2;
        SDL_RenderCopyEx(W.ren,tex,NULL,&d,angle,&c,(SDL_RendererFlip)flipm);
    } else SDL_RenderCopy(W.ren,tex,NULL,&d);
    if(alpha<255) SDL_SetTextureAlphaMod(tex,255);
    return 0;
}
LFN(f_w_clip){ UNUSED_SELF;
    w_need();
    if(nargs==0){ SDL_RenderSetClipRect(W.ren,NULL); return 0; }
    SDL_Rect r;
    r.x=checkint(L,base,nargs,0,"clip"); r.y=checkint(L,base,nargs,1,"clip");
    r.w=checkint(L,base,nargs,2,"clip"); r.h=checkint(L,base,nargs,3,"clip");
    SDL_RenderSetClipRect(W.ren,&r); return 0;
}

/* input */
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

/* timing */
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

/* advanced */
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
    if(W.has_img) s=pIMG_Load(p->s);
    else s=SDL_LoadBMP(p->s);
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
    else if(W.has_img) rc=pIMG_SavePNG(s,p->s);
    else { SDL_FreeSurface(s); luc_error("window.screenshot: PNG needs SDL2_image (not installed) - use a .bmp path"); }
#else
    rc=SDL_SaveBMP(s,p->s);
#endif
    SDL_FreeSurface(s);
    if(rc!=0){ RET(0,mkbool(0)); RET(1,cstrv(SDL_GetError())); return 2; }
    RET(0,mkbool(1)); return 1;
}

/* sound + music (SDL_mixer): WAV always works, OGG/MP3/FLAC need the mixer DLLs */
#ifndef LUC_NO_MIXER
static void w_drop_sounds(void){
    for(int i=0;i<W_SNDCACHE;i++){
        if(W.snd[i].chunk){ pMix_FreeChunk(W.snd[i].chunk); W.snd[i].chunk=NULL; }
        W.snd[i].path[0]=0;
    }
    if(W.music){ pMix_FreeMusic(W.music); W.music=NULL; }
    W.musicpath[0]=0;
}
static Mix_Chunk *w_sound(const char *path){
    for(int i=0;i<W_SNDCACHE;i++)
        if(W.snd[i].chunk && strcmp(W.snd[i].path,path)==0){
            W.snd[i].age=++W.clock;
            return W.snd[i].chunk;
        }
    Mix_Chunk *c=pMix_LoadWAV(path);
    if(!c) return NULL;
    (void)c;
    int slot=-1;
    for(int i=0;i<W_SNDCACHE;i++) if(!W.snd[i].chunk){ slot=i; break; }
    if(slot<0){ slot=0;
        for(int i=1;i<W_SNDCACHE;i++) if(W.snd[i].age<W.snd[slot].age) slot=i;
        pMix_FreeChunk(W.snd[slot].chunk); }
    snprintf(W.snd[slot].path,sizeof W.snd[slot].path,"%s",path);
    W.snd[slot].chunk=c; W.snd[slot].age=++W.clock;
    return c;
}
static void w_need_mix(const char *fn){
    if(!W.mix_ok){
#ifndef LUC_NO_MIXER
        w_audio_init();   /* reopen after a window close shut it down */
#endif
    }
    if(!W.mix_ok)
        luc_error("window.%s: no audio device (sound support unavailable)",fn);
}
static int w_opt_volume(Table *o){
    Value v=tab_get(o,cstrv("volume"));
    if(v.t==LT_NUM){ int p=(int)v.u.n; if(p<0)p=0; if(p>100)p=100; return p*128/100; }
    return -1;
}
/* loop opt: true/-1 = forever, N>=1 = N plays total, else once */
static int w_opt_loops(Table *o){
    Value v=tab_get(o,cstrv("loop"));
    if(v.t==LT_BOOL) return v.u.b? -1 : 0;
    if(v.t==LT_NUM){
        if(v.u.n<0) return -1;
        int n=(int)v.u.n;
        return n>=1? n-1 : 0;
    }
    return 0;
}
static Table *w_sound_handle(const char *path){
    Table *t=tab_new(0);
    tab_set(t,cstrv("kind"),cstrv("sound"));
    tab_set(t,cstrv("path"),cstrv(path));
    return t;
}
static Mix_Chunk *w_sound_arg(LucState *L,int base,int nargs,int i,const char *fn){
    Value a=AR(i);
    const char *path=NULL;
    if(a.t==LT_STR) path=AS_STR(a)->s;
    else if(a.t==LT_TABLE || a.t==LT_LIST){
        Value pv=tab_get(AS_TAB(a),cstrv("path"));
        if(pv.t==LT_STR) path=AS_STR(pv)->s;
    }
    if(!path) luc_error("window.%s: expected a sound (window.sound(path) or \"path\")",fn);
    Mix_Chunk *c=w_sound(path);
    if(!c) luc_error("window.%s: cannot load '%s' (%s)",fn,path,SDL_GetError());
    return c;
}
LFN(f_w_sound){ UNUSED_SELF;
    w_need_mix("sound");
    Str *p=checkstr(L,base,nargs,0,"sound");
    if(!w_sound(p->s)) luc_error("window.sound: cannot load '%s' (%s)",p->s,SDL_GetError());
    RET(0,mkobj(LT_TABLE,w_sound_handle(p->s))); return 1;
}
/* play(snd [, opts]): opts = { loop = 2, volume = 80 }. returns true/false. */
LFN(f_w_play){ UNUSED_SELF;
    w_need_mix("play");
    Mix_Chunk *c=w_sound_arg(L,base,nargs,0,"play");
    int loops=0, vol=-1;
    if(nargs>=2 && AR(1).t==LT_TABLE){
        Table *o=AS_TAB(AR(1));
        loops=w_opt_loops(o); vol=w_opt_volume(o);
    }
    int ch=pMix_PlayChannel(-1,c,loops);
    if(ch<0){ RET(0,mkbool(0)); return 1; }
    if(vol>=0) pMix_Volume(ch,vol);
    RET(0,mkbool(1)); return 1;
}
LFN(f_w_stop){ UNUSED_SELF;
    (void)L;(void)base;(void)nargs;
#ifndef LUC_NO_MIXER
    if(W.mix_ok) pMix_HaltChannel(-1);
#endif
    return 0;
}
static Table *w_music_handle(const char *path){
    Table *t=tab_new(0);
    tab_set(t,cstrv("kind"),cstrv("music"));
    tab_set(t,cstrv("path"),cstrv(path));
    return t;
}
static const char *w_music_path(LucState *L,int base,int nargs,int i,const char *fn){
    Value a=AR(i);
    if(a.t==LT_STR) return AS_STR(a)->s;
    if(a.t==LT_TABLE || a.t==LT_LIST){
        Value pv=tab_get(AS_TAB(a),cstrv("path"));
        if(pv.t==LT_STR) return AS_STR(pv)->s;
    }
    luc_error("window.%s: expected music (window.music(path) or \"path\")",fn);
    return NULL;
}
LFN(f_w_music){ UNUSED_SELF;
    w_need_mix("music");
    Str *p=checkstr(L,base,nargs,0,"music");
    /* validate now so typos fail fast */
    Mix_Music *m=pMix_LoadMUS(p->s);
    if(!m) luc_error("window.music: cannot load '%s' (%s)",p->s,SDL_GetError());
    pMix_FreeMusic(m);
    RET(0,mkobj(LT_TABLE,w_music_handle(p->s))); return 1;
}
/* play_music(m [, opts]): opts = { loop = -1 (default: forever), volume = 80 } */
LFN(f_w_play_music){ UNUSED_SELF;
    w_need_mix("play_music");
    const char *path=w_music_path(L,base,nargs,0,"play_music");
    int loops=-1, vol=-1;
    if(nargs>=2 && AR(1).t==LT_TABLE){
        Table *o=AS_TAB(AR(1));
        Value lv=tab_get(o,cstrv("loop"));
        if(lv.t==LT_BOOL) loops=lv.u.b? -1 : 0;
        else if(lv.t==LT_NUM) loops=(int)lv.u.n;
        vol=w_opt_volume(o);
    }
    if(!W.music || strcmp(W.musicpath,path)!=0){
        if(W.music){ pMix_FreeMusic(W.music); W.music=NULL; }
        W.music=pMix_LoadMUS(path);
        if(!W.music) luc_error("window.play_music: cannot load '%s' (%s)",path,SDL_GetError());
        snprintf(W.musicpath,sizeof W.musicpath,"%s",path);
    }
    if(pMix_PlayMusic(W.music,loops)!=0)
        luc_error("window.play_music: cannot play '%s' (%s)",path,SDL_GetError());
    if(vol>=0) pMix_VolumeMusic(vol);
    RET(0,mkbool(1)); return 1;
}
LFN(f_w_stop_music){ UNUSED_SELF;
    (void)L;(void)base;(void)nargs;
#ifndef LUC_NO_MIXER
    if(W.mix_ok) pMix_HaltMusic();
#endif
    return 0;
}
LFN(f_w_pause_music){ UNUSED_SELF;
    (void)L;(void)base;(void)nargs;
#ifndef LUC_NO_MIXER
    if(W.mix_ok) pMix_PauseMusic();
#endif
    return 0;
}
LFN(f_w_resume_music){ UNUSED_SELF;
    (void)L;(void)base;(void)nargs;
#ifndef LUC_NO_MIXER
    if(W.mix_ok) pMix_ResumeMusic();
#endif
    return 0;
}
LFN(f_w_music_volume){ UNUSED_SELF;
    int v=nargs>=1? checkint(L,base,nargs,0,"music_volume") : 100;
    if(v<0)v=0; if(v>100)v=100;
#ifndef LUC_NO_MIXER
    if(W.mix_ok) pMix_VolumeMusic(v*128/100);
#endif
    return 0;
}
LFN(f_w_sound_volume){ UNUSED_SELF;
    int v=nargs>=1? checkint(L,base,nargs,0,"sound_volume") : 100;
    if(v<0)v=0; if(v>100)v=100;
#ifndef LUC_NO_MIXER
    if(W.mix_ok) pMix_Volume(-1,v*128/100);
#endif
    return 0;
}
#endif /* LUC_NO_MIXER */

/* load satellite DLLs at runtime; missing ones degrade gracefully */
static void w_load_satellites(void){
    static int done=0; if(done) return; done=1;
#ifndef LUC_NO_TTF
    w_hTTF=W_LIB_OPEN(
#ifdef _WIN32
        "SDL2_ttf.dll"
#else
        "libSDL2_ttf-2.0.so.0"
#endif
    );
    if(w_hTTF){
        pTTF_Init=(w_TTF_Init_t)W_LIB_SYM(w_hTTF,"TTF_Init");
        pTTF_OpenFont=(w_TTF_OpenFont_t)W_LIB_SYM(w_hTTF,"TTF_OpenFont");
        pTTF_CloseFont=(w_TTF_CloseFont_t)W_LIB_SYM(w_hTTF,"TTF_CloseFont");
        pTTF_RenderUTF8_Blended=(w_TTF_RenderUTF8_Blended_t)W_LIB_SYM(w_hTTF,"TTF_RenderUTF8_Blended");
        pTTF_SizeUTF8=(w_TTF_SizeUTF8_t)W_LIB_SYM(w_hTTF,"TTF_SizeUTF8");
        if(pTTF_Init&&pTTF_OpenFont&&pTTF_CloseFont&&pTTF_RenderUTF8_Blended&&pTTF_SizeUTF8)
            W.has_ttf=1;
    }
#endif
#ifndef LUC_NO_IMAGE
    w_hIMG=W_LIB_OPEN(
#ifdef _WIN32
        "SDL2_image.dll"
#else
        "libSDL2_image-2.0.so.0"
#endif
    );
    if(w_hIMG){
        pIMG_Init=(w_IMG_Init_t)W_LIB_SYM(w_hIMG,"IMG_Init");
        pIMG_Load=(w_IMG_Load_t)W_LIB_SYM(w_hIMG,"IMG_Load");

        pIMG_SavePNG=(w_IMG_SavePNG_t)W_LIB_SYM(w_hIMG,"IMG_SavePNG");
        if(pIMG_Init&&pIMG_Load&&pIMG_SavePNG)
            W.has_img=1;
    }
#endif
#ifndef LUC_NO_MIXER
    w_hMIX=W_LIB_OPEN(
#ifdef _WIN32
        "SDL2_mixer.dll"
#else
        "libSDL2_mixer-2.0.so.0"
#endif
    );
    if(w_hMIX){
        pMix_Init=(w_Mix_Init_t)W_LIB_SYM(w_hMIX,"Mix_Init");
        pMix_OpenAudio=(w_Mix_OpenAudio_t)W_LIB_SYM(w_hMIX,"Mix_OpenAudio");
        pMix_CloseAudio=(w_Mix_CloseAudio_t)W_LIB_SYM(w_hMIX,"Mix_CloseAudio");
        pMix_LoadWAV=(w_Mix_LoadWAV_t)W_LIB_SYM(w_hMIX,"Mix_LoadWAV");
        pMix_FreeChunk=(w_Mix_FreeChunk_t)W_LIB_SYM(w_hMIX,"Mix_FreeChunk");
        pMix_PlayChannel=(w_Mix_PlayChannel_t)W_LIB_SYM(w_hMIX,"Mix_PlayChannel");
        pMix_Volume=(w_Mix_Volume_t)W_LIB_SYM(w_hMIX,"Mix_Volume");
        pMix_HaltChannel=(w_Mix_HaltChannel_t)W_LIB_SYM(w_hMIX,"Mix_HaltChannel");
        pMix_LoadMUS=(w_Mix_LoadMUS_t)W_LIB_SYM(w_hMIX,"Mix_LoadMUS");
        pMix_FreeMusic=(w_Mix_FreeMusic_t)W_LIB_SYM(w_hMIX,"Mix_FreeMusic");
        pMix_PlayMusic=(w_Mix_PlayMusic_t)W_LIB_SYM(w_hMIX,"Mix_PlayMusic");
        pMix_HaltMusic=(w_Mix_HaltMusic_t)W_LIB_SYM(w_hMIX,"Mix_HaltMusic");
        pMix_PauseMusic=(w_Mix_PauseMusic_t)W_LIB_SYM(w_hMIX,"Mix_PauseMusic");
        pMix_ResumeMusic=(w_Mix_ResumeMusic_t)W_LIB_SYM(w_hMIX,"Mix_ResumeMusic");
        pMix_VolumeMusic=(w_Mix_VolumeMusic_t)W_LIB_SYM(w_hMIX,"Mix_VolumeMusic");

        if(pMix_Init&&pMix_OpenAudio&&pMix_CloseAudio&&pMix_LoadWAV&&pMix_FreeChunk&&
           pMix_PlayChannel&&pMix_Volume&&pMix_HaltChannel&&pMix_LoadMUS&&pMix_FreeMusic&&
           pMix_PlayMusic&&pMix_HaltMusic&&pMix_PauseMusic&&pMix_ResumeMusic&&
           pMix_VolumeMusic)
            W.has_mix=1;
    }
#endif
}
/* module table */
Value lucL_window_module(void){
    static int sdl_ready=0;
    if(!sdl_ready){
        SDL_SetMainReady();
        if(SDL_Init(SDL_INIT_VIDEO|SDL_INIT_TIMER)!=0)
            luc_error("window: SDL2 could not initialise (%s)",SDL_GetError());
        w_load_satellites();
#ifndef LUC_NO_TTF
        if(W.has_ttf && pTTF_Init()==0) W.ttf_ok=1;
#endif
#ifndef LUC_NO_IMAGE
        if(W.has_img) pIMG_Init(IMG_INIT_PNG|IMG_INIT_JPG);
#endif
#ifndef LUC_NO_MIXER
        w_audio_init();
#endif
        sdl_ready=1;
    }
    Table *t=tab_new(0);
    reg(t,"start",f_w_start);         reg(t,"close",f_w_close);
    reg(t,"running",f_w_running);     reg(t,"update",f_w_update);
    reg(t,"go",f_w_go);
    reg(t,"title",f_w_title);         reg(t,"size",f_w_size);
    reg(t,"resize",f_w_resize);       reg(t,"quit",f_w_quit);

    reg(t,"clear",f_w_clear);         reg(t,"pixel",f_w_pixel);
    reg(t,"line",f_w_line);           reg(t,"rect",f_w_rect);
    reg(t,"rect_outline",f_w_rect_outline);
    reg(t,"circle",f_w_circle);       reg(t,"circle_outline",f_w_circle_outline);
    reg(t,"triangle",f_w_triangle);   reg(t,"polygon",f_w_polygon);
    reg(t,"text",f_w_text);           reg(t,"text_size",f_w_text_size);
    reg(t,"text_center",f_w_text_center);
    reg(t,"font",f_w_font);           reg(t,"image",f_w_image);
    reg(t,"image_size",f_w_image_size); reg(t,"clip",f_w_clip);
    reg(t,"sprite",f_w_sprite);       reg(t,"draw",f_w_draw);

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

#ifndef LUC_NO_MIXER
    reg(t,"sound",f_w_sound);         reg(t,"play",f_w_play);
    reg(t,"stop",f_w_stop);
    reg(t,"music",f_w_music);         reg(t,"play_music",f_w_play_music);
    reg(t,"stop_music",f_w_stop_music);
    reg(t,"pause_music",f_w_pause_music);
    reg(t,"resume_music",f_w_resume_music);
    reg(t,"music_volume",f_w_music_volume);
    reg(t,"sound_volume",f_w_sound_volume);
#endif

    tab_set(t,cstrv("_VERSION"),cstrv("luc.window 0.1 (SDL2)"));
#ifndef LUC_NO_TTF
    tab_set(t,cstrv("has_ttf"),mkbool(W.ttf_ok));
#else
    tab_set(t,cstrv("has_ttf"),mkbool(0));
#endif
#ifndef LUC_NO_IMAGE
    tab_set(t,cstrv("has_image"),mkbool(W.has_img));
#else
    tab_set(t,cstrv("has_image"),mkbool(0));
#endif
#ifndef LUC_NO_MIXER
    tab_set(t,cstrv("has_sound"),mkbool(W.mix_ok));
#else
    tab_set(t,cstrv("has_sound"),mkbool(0));
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


/* luc_lib_net.c */
/* luc_lib_net -- TCP + HTTP client/server (import net).
 *
 * Sockets are always non-blocking. The raw CFuncs (__recv_try /
 * __accept_try) attempt exactly once and never wait; the user-facing
 * recv / accept are LUC closures (built once at first import) that loop
 * the raw attempt with task.wait between tries. Because the retry lives in
 * ordinary LUC code, coroutine yield/resume works by construction and the
 * plain blocking-style API stays concurrent across task.spawn clients:
 *
 *   import net("n")
 *   create s = n.serve(8000)
 *   while true do
 *     create cli = s:accept()
 *     task.spawn(function()
 *       while true do
 *         create m = cli:recv()
 *         if m == nil then break end
 *         cli:send("echo:" .. m)
 *       end
 *     end)
 *   end
 *
 * Error contract: success returns the value; failure returns nil + message.
 * recv distinguishes empty (nil, no error: retry) from closed (nil +
 * "closed") from timeout (nil + "timeout"). Only http:// URLs (no TLS).
 * Windows needs ws2_32 at link time. */
#if defined(_WIN32)
#  include <winsock2.h>
#  include <ws2tcpip.h>
   typedef SOCKET sock_t;
#  define SOCK_INVALID INVALID_SOCKET
#  define sock_closefd closesocket
#  define sock_wouldblock() (WSAGetLastError()==WSAEWOULDBLOCK)
#  define sock_eintr() (WSAGetLastError()==WSAEINTR)
#  define sock_errcode() ((int)WSAGetLastError())
#else
#  include <sys/types.h>
#  include <sys/socket.h>
#  include <netdb.h>
#  include <unistd.h>
#  include <fcntl.h>
#  include <errno.h>
#  include <string.h>
   typedef int sock_t;
#  define SOCK_INVALID (-1)
#  define sock_closefd close
#  define sock_wouldblock() (errno==EWOULDBLOCK||errno==EAGAIN)
#  define sock_eintr() (errno==EINTR)
#  define sock_errcode() (errno)
#endif

static int net_started=0;
static void net_startup(void){
    if(net_started) return;
#if defined(_WIN32)
    WSADATA wd;
    if(WSAStartup(MAKEWORD(2,2),&wd)!=0) luc_error("net: WSAStartup failed");
#endif
    srand((unsigned)time(NULL)^(unsigned)luc_now());
    net_started=1;
}
static Socket *checksock(LucState *L,int base,int nargs,int i,const char *fn){
    Value v=AR(i);
    if(v.t!=LT_SOCKET) luc_error("bad argument #%d to '%s' (socket expected, got %s)",i+1,fn,type_name(v));
    Socket *s=AS_SOCK(v);
    if(s->closed) luc_error("'%s' on a closed socket",fn);
    return s;
}
static int ws_build_frame(const char *p,size_t n,char **out,size_t *outlen);
static int ws_fill(Socket *s,sock_t fd,void *tls);
static int ws_pull_message(Socket *s,sock_t fd,void *tls,char **outp,size_t *outn,
                           size_t maxmsg,char *err,size_t errcap);
static void sock_set_nonblock(sock_t fd){
#if defined(_WIN32)
    u_long m=1; ioctlsocket(fd,FIONBIO,&m);
#else
    int f=fcntl(fd,F_GETFL,0); if(f>=0) fcntl(fd,F_SETFL,f|O_NONBLOCK);
#endif
}
/* block-or-timeout helpers for one-shot operations (connect, HTTP):
 * single select(), no scheduler involvement (they complete fast). */
static int sock_wait_writable(sock_t fd,double timeout){
    fd_set w; FD_ZERO(&w);
#if defined(_WIN32)
    FD_SET(fd,&w);
#else
    if(fd<0||fd>=FD_SETSIZE) return 0;
    FD_SET(fd,&w);
#endif
    struct timeval tv;
    tv.tv_sec=(long)timeout; tv.tv_usec=(long)((timeout-(long)timeout)*1000000);
    return select((int)(fd+1),NULL,&w,NULL,&tv)>0;
}
static void sock_set_timeout(sock_t fd,int isrecv,double sec){
    if(sec<0) return;
#if defined(_WIN32)
    DWORD ms=(DWORD)(sec*1000);
    setsockopt(fd,SOL_SOCKET,isrecv?SO_RCVTIMEO:SO_SNDTIMEO,(const char*)&ms,sizeof ms);
#else
    struct timeval tv;
    tv.tv_sec=(long)sec; tv.tv_usec=(long)((sec-(long)sec)*1000000);
    setsockopt(fd,SOL_SOCKET,isrecv?SO_RCVTIMEO:SO_SNDTIMEO,&tv,sizeof tv);
#endif
}
static void sock_set_blocking(sock_t fd,int blocking){
#if defined(_WIN32)
    u_long m=blocking?0:1; ioctlsocket(fd,FIONBIO,&m);
#else
    int f=fcntl(fd,F_GETFL,0);
    if(f>=0) fcntl(fd,F_SETFL,blocking?(f&~O_NONBLOCK):(f|O_NONBLOCK));
#endif
}
static void net_errmsg(char *buf,size_t sz,const char *what){
    snprintf(buf,sz,"%s (network error %d)",what,sock_errcode());
}
/* ---- TLS client via Schannel (Windows). POSIX builds: unavailable. ----
 * Used with BLOCKING sockets only (the HTTP path flips to blocking after
 * connect, with 30s timeouts as backstop). Certificate chain + hostname
 * are validated against the system store by default. */
#if defined(_WIN32)
#  ifndef SECURITY_WIN32
#  define SECURITY_WIN32
#  endif
#  include <sspi.h>
#  include <schannel.h>
typedef struct {
    CredHandle cred; CtxtHandle ctx;
    SecPkgContext_StreamSizes sizes;
    int cred_ok, ctx_ok;
    char *enc;   size_t enclen, enccap;   /* undecrypted leftover bytes */
    char *pend;  size_t pendlen, pendcap; /* decrypted, unread bytes    */
} TLSSession;
static void tls_free_session(TLSSession *s){
    if(!s) return;
    if(s->ctx_ok) DeleteSecurityContext(&s->ctx);
    if(s->cred_ok) FreeCredentialsHandle(&s->cred);
    free(s->enc); free(s->pend); free(s);
}
static int tls_send_all(sock_t fd,const char *p,size_t n){
    while(n>0){
        int r=send(fd,p,(int)(n>16384?16384:n),0);
        if(r<=0) return 0;
        p+=r; n-=(size_t)r;
    }
    return 1;
}
/* full client handshake on a blocking socket. 0 ok, -1 error (err set). */
static int tls_connect_fd(sock_t fd,const char *hostname,TLSSession **out,
                          char *err,size_t errcap){
    TLSSession *s=(TLSSession*)calloc(1,sizeof(TLSSession));
    if(!s){ snprintf(err,errcap,"out of memory"); return -1; }
    *out=NULL;
    TimeStamp expiry;
    SECURITY_STATUS st=AcquireCredentialsHandleA(NULL,UNISP_NAME,
        SECPKG_CRED_OUTBOUND,NULL,NULL,NULL,NULL,&s->cred,&expiry);
    if(st!=SEC_E_OK){ snprintf(err,errcap,"tls: no credentials (%ld)",(long)st); free(s); return -1; }
    s->cred_ok=1;
    DWORD flags=ISC_REQ_SEQUENCE_DETECT|ISC_REQ_REPLAY_DETECT|
                ISC_REQ_CONFIDENTIALITY|ISC_REQ_ALLOCATE_MEMORY|ISC_REQ_STREAM;
    SecBufferDesc inb, outb;
    SecBuffer inbuf[2], outbuf[1];
    int first=1, done=0, nread=0;
    char rbuf[16384];
    while(!done){
        outbuf[0].pvBuffer=NULL; outbuf[0].cbBuffer=0;
        outbuf[0].BufferType=SECBUFFER_TOKEN;
        outb.ulVersion=SECBUFFER_VERSION; outb.cBuffers=1; outb.pBuffers=outbuf;
        inb.ulVersion=SECBUFFER_VERSION;
        if(first){
            inb.cBuffers=0; inb.pBuffers=NULL;
        } else {
            inbuf[0].pvBuffer=rbuf; inbuf[0].cbBuffer=(unsigned long)nread;
            inbuf[0].BufferType=SECBUFFER_TOKEN;
            inbuf[1].pvBuffer=NULL; inbuf[1].cbBuffer=0;
            inbuf[1].BufferType=SECBUFFER_EMPTY;
            inb.cBuffers=2; inb.pBuffers=inbuf;
        }
        DWORD outf=0;
        /* NOTE: this MinGW header declares InitializeSecurityContextA with
         * 12 params including phNewContext (like AcceptSecurityContext).
         * phNewContext MUST receive &s->ctx: it carries the partial/new
         * context handle across calls. Passing NULL here "works" for the
         * first call but every later call fails with SEC_E_QOP_NOT_SUPPORTED
         * (0x80090301) because s->ctx was never filled in. */
        st=InitializeSecurityContextA(&s->cred,first?NULL:&s->ctx,
            (SEC_CHAR*)hostname,flags,0,0,first?NULL:&inb,0,&s->ctx,&outb,&outf,NULL);
        s->ctx_ok=1;
        if(outbuf[0].cbBuffer>0){
            if(!tls_send_all(fd,(const char*)outbuf[0].pvBuffer,outbuf[0].cbBuffer)){
                snprintf(err,errcap,"tls: handshake send failed");
                FreeContextBuffer(outbuf[0].pvBuffer);
                tls_free_session(s); return -1;
            }
            FreeContextBuffer(outbuf[0].pvBuffer);
        }
        if(st==SEC_E_OK){ done=1; break; }
        if(st!=SEC_I_CONTINUE_NEEDED){
            snprintf(err,errcap,"tls: handshake failed (%ld)",(long)st);
            tls_free_session(s); return -1;
        }
        first=0;
        nread=recv(fd,rbuf,sizeof rbuf,0);
        if(nread<=0){
            snprintf(err,errcap,"tls: handshake cut off");
            tls_free_session(s); return -1;
        }
    }
    if(QueryContextAttributesA(&s->ctx,SECPKG_ATTR_STREAM_SIZES,&s->sizes)!=SEC_E_OK){
        snprintf(err,errcap,"tls: cannot query stream sizes");
        tls_free_session(s); return -1;
    }
    *out=s;
    return 0;
}
static int tls_send(TLSSession *s,sock_t fd,const char *p,size_t n){
    size_t chunk=s->sizes.cbMaximumMessage;
    if(chunk<1024) chunk=1024;
    size_t total=s->sizes.cbHeader+chunk+s->sizes.cbTrailer;
    char *b=(char*)malloc(total);
    if(!b) return -1;
    while(n>0){
        size_t k=n>chunk?chunk:n;
        SecBuffer bufs[4];
        SecBufferDesc d;
        bufs[0].BufferType=SECBUFFER_STREAM_HEADER;
        bufs[0].pvBuffer=b; bufs[0].cbBuffer=s->sizes.cbHeader;
        bufs[1].BufferType=SECBUFFER_DATA;
        bufs[1].pvBuffer=b+s->sizes.cbHeader; bufs[1].cbBuffer=(unsigned long)k;
        memcpy(bufs[1].pvBuffer,p,k);
        bufs[2].BufferType=SECBUFFER_STREAM_TRAILER;
        bufs[2].pvBuffer=b+s->sizes.cbHeader+k; bufs[2].cbBuffer=s->sizes.cbTrailer;
        bufs[3].BufferType=SECBUFFER_EMPTY; bufs[3].pvBuffer=NULL; bufs[3].cbBuffer=0;
        d.ulVersion=SECBUFFER_VERSION; d.cBuffers=4; d.pBuffers=bufs;
        if(EncryptMessage(&s->ctx,0,&d,0)!=SEC_E_OK){ free(b); return -1; }
        size_t w=bufs[0].cbBuffer+bufs[1].cbBuffer+bufs[2].cbBuffer;
        if(!tls_send_all(fd,b,w)){ free(b); return -1; }
        p+=k; n-=k;
    }
    free(b);
    return 0;
}
/* returns >0 bytes out, 0 on orderly close, -1 on error */
static int tls_recv(TLSSession *s,sock_t fd,char *out,size_t max){
    if(s->pendlen>0){
        size_t k=s->pendlen>max?max:s->pendlen;
        memcpy(out,s->pend,k);
        memmove(s->pend,s->pend+k,s->pendlen-k);
        s->pendlen-=k;
        return (int)k;
    }
    char rbuf[16384];
    for(;;){
        size_t off=s->enclen;
        if(off+sizeof rbuf>s->enccap){
            size_t nc=s->enccap?s->enccap*2:32768;
            while(nc<off+sizeof rbuf) nc*=2;
            char *np=(char*)realloc(s->enc,nc);
            if(!np) return -1;
            s->enc=np; s->enccap=nc;
        }
        int n=recv(fd,rbuf,sizeof rbuf,0);
        if(n==0) return 0;
        if(n<0) return sock_wouldblock()?-2:-1;
        memcpy(s->enc+off,rbuf,(size_t)n);
        s->enclen=off+(size_t)n;
        for(;;){
            SecBuffer bufs[4];
            bufs[0].pvBuffer=s->enc; bufs[0].cbBuffer=(unsigned long)s->enclen;
            bufs[0].BufferType=SECBUFFER_DATA;
            bufs[1].BufferType=SECBUFFER_EMPTY; bufs[1].pvBuffer=NULL; bufs[1].cbBuffer=0;
            bufs[2].BufferType=SECBUFFER_EMPTY; bufs[2].pvBuffer=NULL; bufs[2].cbBuffer=0;
            bufs[3].BufferType=SECBUFFER_EMPTY; bufs[3].pvBuffer=NULL; bufs[3].cbBuffer=0;
            SecBufferDesc d;
            d.ulVersion=SECBUFFER_VERSION; d.cBuffers=4; d.pBuffers=bufs;
            SECURITY_STATUS st=DecryptMessage(&s->ctx,&d,0,NULL);
            if(st==SEC_E_INCOMPLETE_MESSAGE) break;   /* recv more above */
            if(st!=SEC_E_OK&&st!=SEC_I_RENEGOTIATE) return -1;
            {
                char *data=NULL; size_t dlen=0, extralen=0;
                char *extra=NULL;
                for(int i=0;i<4;i++){
                    if(bufs[i].BufferType==SECBUFFER_DATA&&bufs[i].cbBuffer>0){
                        data=(char*)bufs[i].pvBuffer; dlen=bufs[i].cbBuffer;
                    }
                    if(bufs[i].BufferType==SECBUFFER_EXTRA&&bufs[i].cbBuffer>0){
                        extra=(char*)bufs[i].pvBuffer; extralen=bufs[i].cbBuffer;
                    }
                }
                /* NOTE: data/extra both alias s->enc: copy data OUT first,
                 * then compact EXTRA. (The old order moved EXTRA first and
                 * clobbered data -> garbage frames on coalesced bursts.) */
                if(dlen>0){
                    size_t k=dlen>max?max:dlen;
                    memcpy(out,data,k);
                    if(dlen>k){   /* stash the rest decrypted */
                        if(s->pendlen+dlen-k>s->pendcap){
                            size_t nc=s->pendcap?s->pendcap*2:8192;
                            while(nc<s->pendlen+dlen-k) nc*=2;
                            char *np=(char*)realloc(s->pend,nc);
                            if(!np) return -1;
                            s->pend=np; s->pendcap=nc;
                        }
                        memcpy(s->pend+s->pendlen,data+k,dlen-k);
                        s->pendlen+=dlen-k;
                    }
                    if(extra) memmove(s->enc,extra,extralen);
                    s->enclen=extralen;
                    return (int)k;
                }
                if(extra) memmove(s->enc,extra,extralen);
                s->enclen=extralen;
                if(st==SEC_I_RENEGOTIATE) continue;
                return 0;
            }
        }
    }
}
static Socket *sock_wrap(sock_t fd,int isserver){
    return sock_new((intptr_t)fd,isserver);
}
void net_socket_close_fd(Socket *s){
    if(!s||s->closed) return;
    s->closed=1;
    if(s->isws && (sock_t)s->fd!=SOCK_INVALID){
        /* best-effort closing handshake (ignored when it fails).
         * Client frames MUST be masked: header + 4-byte mask + masked empty payload. */
        unsigned char cf[6]={0x88,0x80,0x12,0x34,0x56,0x78};
        send((sock_t)s->fd,(const char*)cf,6,0);
    }
    if((sock_t)s->fd!=SOCK_INVALID) sock_closefd((sock_t)s->fd);
    s->fd=(intptr_t)SOCK_INVALID;
    tls_free_session((TLSSession*)s->tlsctx); s->tlsctx=NULL;
    free(s->rbuf); s->rbuf=NULL; s->rlen=s->rcap=0;
    free(s->frag); s->frag=NULL; s->fraglen=s->fragcap=0;
}
#else
typedef struct { int unused; } TLSSession;
static void tls_free_session(TLSSession *s){ (void)s; }
static int tls_connect_fd(sock_t fd,const char *hostname,TLSSession **out,
                          char *err,size_t errcap){
    (void)fd; (void)hostname; (void)out;
    snprintf(err,errcap,"tls: https needs a Windows build in this version");
    return -1;
}
static int tls_send(TLSSession *s,sock_t fd,const char *p,size_t n){
    (void)s; (void)fd; (void)p; (void)n; return -1;
}
static int tls_recv(TLSSession *s,sock_t fd,char *out,size_t max){
    (void)s; (void)fd; (void)out; (void)max; return -1;
}
#endif
/* blocking-style connect with timeout, scheduler-pumped. */
static sock_t net_connect_to(const char *host,int port,double timeout,char *err,size_t errcap){
    char ports[16]; snprintf(ports,sizeof ports,"%d",port);
    struct addrinfo hints, *list=NULL, *ai;
    memset(&hints,0,sizeof hints);
    hints.ai_family=AF_UNSPEC; hints.ai_socktype=SOCK_STREAM;
    if(getaddrinfo(host,ports,&hints,&list)!=0 || !list){
        snprintf(err,errcap,"cannot resolve '%s'",host);
        return SOCK_INVALID;
    }
    sock_t out=SOCK_INVALID;
    for(ai=list;ai;ai=ai->ai_next){
        sock_t fd=socket(ai->ai_family,ai->ai_socktype,ai->ai_protocol);
        if(fd==SOCK_INVALID) continue;
        sock_set_nonblock(fd);
        if(connect(fd,ai->ai_addr,(int)ai->ai_addrlen)==0){ out=fd; break; }
#if defined(_WIN32)
        if(WSAGetLastError()!=WSAEWOULDBLOCK){ sock_closefd(fd); continue; }
#else
        if(errno!=EINPROGRESS){ sock_closefd(fd); continue; }
#endif
        /* one-shot wait (connect completes fast; caller picks timeout) */
        int ok=0;
        if(sock_wait_writable(fd,timeout<0?5:timeout)){
            int e=0; socklen_t el=sizeof e;
            getsockopt(fd,SOL_SOCKET,SO_ERROR,(char*)&e,&el);
            if(e==0) ok=1;
        }
        if(ok){ out=fd; break; }
        sock_closefd(fd);
    }
    freeaddrinfo(list);
    if(out==SOCK_INVALID) snprintf(err,errcap,"cannot connect to %s:%d",host,port);
    return out;
}
LFN(f_net_connect){
    Str *host=checkstr(L,base,nargs,0,"connect");
    int port=nargs>=2?checkint(L,base,nargs,1,"connect"):80;
    double timeout=nargs>=3?checknum(L,base,nargs,2,"connect"):5;
    char *h=(char*)malloc((size_t)host->len+1);
    if(!h) luc_error("net: out of memory");
    memcpy(h,host->s,(size_t)host->len); h[host->len]=0;
    char err[160]; err[0]=0;
    sock_t fd=net_connect_to(h,port,timeout,err,sizeof err);
    free(h);
    if(fd==SOCK_INVALID){ RET(0,NIL); RET(1,cstrv(err)); return 2; }
    RET(0,mkobj(LT_SOCKET,sock_wrap(fd,0))); return 1;
}
LFN(f_net_serve){
    int port=nargs>=1?checkint(L,base,nargs,0,"serve"):8000;
    int backlog=nargs>=2?checkint(L,base,nargs,1,"serve"):16;
    if(backlog<1) backlog=1; if(backlog>128) backlog=128;
    sock_t fd=socket(AF_INET,SOCK_STREAM,0);
    if(fd==SOCK_INVALID){ char e[96]; net_errmsg(e,sizeof e,"serve: socket"); RET(0,NIL); RET(1,cstrv(e)); return 2; }
    {
        int one=1;
        setsockopt(fd,SOL_SOCKET,SO_REUSEADDR,(const char*)&one,sizeof one);
    }
    struct sockaddr_in a; memset(&a,0,sizeof a);
    a.sin_family=AF_INET; a.sin_port=htons((unsigned short)port);
    a.sin_addr.s_addr=htonl(INADDR_ANY);
    if(bind(fd,(struct sockaddr*)&a,sizeof a)!=0||listen(fd,backlog)!=0){
        char e[96]; net_errmsg(e,sizeof e,"serve: bind/listen");
        sock_closefd(fd); RET(0,NIL); RET(1,cstrv(e)); return 2;
    }
    sock_set_nonblock(fd);
    RET(0,mkobj(LT_SOCKET,sock_wrap(fd,1))); return 1;
}
/* single non-blocking attempt: client socket, or nil (empty for now) */
LFN(f_sock_accept_try){
    Socket *s=checksock(L,base,nargs,0,"accept");
    if(!s->isserver) luc_error("'accept' on a client socket (serve() first)");
    sock_t c=accept((sock_t)s->fd,NULL,NULL);
    if(c!=SOCK_INVALID){
        sock_set_nonblock(c);
        RET(0,mkobj(LT_SOCKET,sock_wrap(c,0))); return 1;
    }
    if(!sock_wouldblock()){ char e[96]; net_errmsg(e,sizeof e,"accept"); RET(0,NIL); RET(1,cstrv(e)); return 2; }
    RET(0,NIL); return 1;
}
LFN(f_sock_send){
    Socket *s=checksock(L,base,nargs,0,"send");
    Str *d=nargs>=2?checkstr(L,base,nargs,1,"send"):NULL;
    if(!d||d->len<=0){ RET(0,mknum(0)); return 1; }
    const char *p=d->s; size_t total=(size_t)d->len;
    char *frame=NULL; size_t framelen=0;
    if(s->isws){
        /* flip to blocking around the frame write (single-threaded: atomic
         * w.r.t. other tasks), backstop via the 30s timeouts below */
        if(ws_build_frame(d->s,(size_t)d->len,&frame,&framelen)!=0){
            RET(0,NIL); RET(1,cstrv("message too large")); return 2;
        }
        p=frame; total=framelen;
        sock_set_blocking((sock_t)s->fd,1);
    }
    int sent=0;
    double t0=luc_now();
    while((size_t)sent<total){
        if(s->tlsctx){
            /* blocking socket here: all-or-nothing per call */
            if(tls_send((TLSSession*)s->tlsctx,(sock_t)s->fd,p+sent,total-(size_t)sent)!=0){
                char e[96];
                snprintf(e,sizeof e,"send: %s",luc_now()-t0>20?"timeout":"failed");
                free(frame);
                if(s->isws) sock_set_blocking((sock_t)s->fd,0);
                RET(0,NIL); RET(1,cstrv(e)); return 2;
            }
            sent=(int)total; break;
        }
#if defined(_WIN32)
        int n=send((sock_t)s->fd,p+sent,(int)(total-(size_t)sent),0);
#else
        ssize_t n=send((sock_t)s->fd,p+sent,total-(size_t)sent,0);
#endif
        if(n>0){ sent+=n; continue; }
        if(n==0||!sock_wouldblock()){ char e[96]; net_errmsg(e,sizeof e,"send"); free(frame);
            if(s->isws) sock_set_blocking((sock_t)s->fd,0);
            RET(0,NIL); RET(1,cstrv(e)); return 2; }
        if(luc_now()-t0>30){ free(frame);
            if(s->isws) sock_set_blocking((sock_t)s->fd,0);
            RET(0,NIL); RET(1,cstrv("send: timeout")); return 2; }
        luc_sleep(0.005);
    }
    free(frame);
    if(s->isws) sock_set_blocking((sock_t)s->fd,0);
    RET(0,mknum((double)(d->len))); return 1;
}
/* single non-blocking attempt: data | nil,"closed" | nil (empty for now).
 * The user-facing recv is a LUC closure looping this with task.wait. */
LFN(f_sock_recv_try){
    if(getenv("LUC_RAWLOG")){
        fprintf(stderr,"RAW nargs=%d t0=%d t1=%d\n",nargs,
            nargs>0?AR(0).t:-9,nargs>1?AR(1).t:-9);
    }
    Socket *s=checksock(L,base,nargs,0,"recv");
    int max=8192;
    if(nargs>=2 && AR(1).t!=LT_NIL) max=checkint(L,base,nargs,1,"recv");
    if(max<1) max=1; if(max>65536) max=65536;
    if(s->isws){
        int fr=ws_fill(s,(sock_t)s->fd,s->tlsctx);
        if(fr<0){ RET(0,NIL); RET(1,cstrv("closed")); return 2; }
        char *msg=NULL; size_t mlen=0; char err[128]; err[0]=0;
        int pr=ws_pull_message(s,(sock_t)s->fd,(TLSSession*)s->tlsctx,
                               &msg,&mlen,(size_t)max,err,sizeof err);
        if(pr>0){ Value v=strv(msg,mlen); free(msg); RET(0,v); return 1; }
        if(pr<0){ free(msg); RET(0,NIL); RET(1,cstrv(err[0]?err:"closed")); return 2; }
        RET(0,NIL); return 1;
    }
    char *buf=(char*)malloc((size_t)max+1);
    if(!buf) luc_error("net: out of memory");
#if defined(_WIN32)
    int n=recv((sock_t)s->fd,buf,max,0);
#else
    ssize_t n=recv((sock_t)s->fd,buf,(size_t)max,0);
#endif
    if(n>0){ Value v=strv(buf,(int)n); free(buf); RET(0,v); return 1; }
    if(n==0){ free(buf); RET(0,NIL); RET(1,cstrv("closed")); return 2; }
    if(!sock_wouldblock()){ char e[96]; net_errmsg(e,sizeof e,"recv"); free(buf); RET(0,NIL); RET(1,cstrv(e)); return 2; }
    free(buf);
    RET(0,NIL); return 1;
}
LFN(f_sock_close){
    Value v=AR(0);
    if(v.t!=LT_SOCKET) luc_error("bad argument #1 to 'close' (socket expected, got %s)",type_name(v));
    net_socket_close_fd(AS_SOCK(v));
    RET(0,mkbool(1)); return 1;
}
/* ---- minimal HTTP/1.0 over the above (no TLS: https:// rejected) ---- */
static int http_has_chunked(const char *h,size_t n){
    /* case-insensitive search for "transfer-encoding" containing "chunked" */
    for(size_t i=0;i+17<n;i++){
        size_t k=0;
        const char *needle="transfer-encoding";
        while(k<17 && i+k<n &&
              (h[i+k]==needle[k]||h[i+k]==needle[k]-32)) k++;
        if(k==17){
            size_t j=i+17;
            while(j<n && h[j]!='\r' && h[j]!='\n') j++;
            for(size_t t=i;t<j;t++){
                const char *c="chunked"; size_t q=0;
                while(q<7 && t+q<j && (h[t+q]==c[q]||h[t+q]==c[q]-32)) q++;
                if(q==7) return 1;
            }
            return 0;
        }
    }
    return 0;
}
static int http_status(const char *h,size_t n){
    /* "HTTP/1.x CODE ..." */
    size_t i=0;
    while(i<n && h[i]!=' ') i++;
    int code=0;
    while(i<n && h[i]==' ') i++;
    while(i<n && h[i]>='0' && h[i]<='9'){ code=code*10+(h[i]-'0'); i++; }
    return code;
}
/* decode chunked body in place; returns new length */
static size_t http_dechunk(char *p,size_t n){
    size_t r=0, w=0;
    while(r<n){
        while(r<n && (p[r]=='\r' || p[r]=='\n')) r++;
        unsigned sz=0;
        while(r<n && p[r]!='\r' && p[r]!='\n'){
            char c=p[r];
            sz*=16;
            if(c>='0'&&c<='9') sz+=(unsigned)(c-'0');
            else if(c>='a'&&c<='f') sz+=(unsigned)(c-'a'+10);
            else if(c>='A'&&c<='F') sz+=(unsigned)(c-'A'+10);
            else break;
            r++;
        }
        while(r<n && p[r]!='\n') r++;
        if(r<n) r++;
        if(sz==0) break;
        if(r+sz>n) sz=n-r;
        memmove(p+w,p+r,sz); r+=sz; w+=sz;
    }
    return w;
}
/* one transport for plain and TLS bytes inside net_http */
static int hsend(TLSSession *tls,sock_t fd,const char *p,size_t n,
                 char *err,size_t errcap){
    size_t off=0;
    while(off<n){
        int r;
        if(tls) r=(tls_send(tls,fd,p+off,n-off)==0)?(int)(n-off):-1;
#if defined(_WIN32)
        else r=send(fd,p+off,(int)(n-off),0);
#else
        else r=(int)send(fd,p+off,n-off,0);
#endif
        if(r>0){ off+=(size_t)r; continue; }
        snprintf(err,errcap,"http: send failed"); return 0;
    }
    return 1;
}
static int net_http(const char *url,const char *method,const char *body,int bodylen,
                    const char *ctype,const char *xhdrs,
                    char **out_body,size_t *out_len,int *out_code,
                    char *err,size_t errcap){
    const char *p=url;
    int ishttps=0;
    if(strncmp(p,"http://",7)==0) p+=7;
    else if(strncmp(p,"https://",8)==0){ p+=8; ishttps=1; }
    else { snprintf(err,errcap,"bad url (need http(s)://host/path)"); return 0; }
    const char *slash=strchr(p,'/');
    const char *hostend=slash?slash:p+strlen(p);
    char host[256]; int port=ishttps?443:80;
    const char *colon=strchr(p,':');
    if(colon && colon<hostend){
        size_t hl=(size_t)(colon-p);
        if(hl>=sizeof host) hl=sizeof host-1;
        memcpy(host,p,hl); host[hl]=0;
        port=atoi(colon+1); if(port<=0||port>65535) port=ishttps?443:80;
    } else {
        size_t hl=(size_t)(hostend-p);
        if(hl>=sizeof host) hl=sizeof host-1;
        memcpy(host,p,hl); host[hl]=0;
    }
    const char *path=slash?slash:"/";
    char req[4096];
    int rn;
    if(body && bodylen>0)
        rn=snprintf(req,sizeof req,"%s %s HTTP/1.0\r\nHost: %s\r\nConnection: close\r\nContent-Type: %s\r\nContent-Length: %d\r\n%s\r\n\r\n",
                    method,path,host,ctype?ctype:"application/json",bodylen,
                    xhdrs?xhdrs:"");
    else
        rn=snprintf(req,sizeof req,"%s %s HTTP/1.0\r\nHost: %s\r\nConnection: close\r\n%s\r\n\r\n",
                    method,path,host,xhdrs?xhdrs:"");
    if(rn<=0||rn>=(int)sizeof req-1){ snprintf(err,errcap,"url too long"); return 0; }
    sock_t fd=net_connect_to(host,port,10,err,errcap);
    if(fd==SOCK_INVALID) return 0;
    /* HTTP is one-shot: flip to blocking with backstop timeouts */
    sock_set_blocking(fd,1);
    sock_set_timeout(fd,1,30); sock_set_timeout(fd,0,30);
    TLSSession *tls=NULL;
    if(ishttps){
        if(tls_connect_fd(fd,host,&tls,err,errcap)!=0){ sock_closefd(fd); return 0; }
    }
    if(!hsend(tls,fd,req,(size_t)rn,err,errcap)){ tls_free_session(tls); sock_closefd(fd); return 0; }
    if(body && bodylen>0){
        if(!hsend(tls,fd,body,(size_t)bodylen,err,errcap)){ tls_free_session(tls); sock_closefd(fd); return 0; }
    }
    size_t cap=16384, len=0;
    char *resp=(char*)malloc(cap);
    if(!resp){ tls_free_session(tls); sock_closefd(fd); snprintf(err,errcap,"out of memory"); return 0; }
    for(;;){
        if(len+4096>cap){ cap*=2; char *np=(char*)realloc(resp,cap); if(!np){ free(resp); tls_free_session(tls); sock_closefd(fd); snprintf(err,errcap,"out of memory"); return 0; } resp=np; }
        int n;
        if(tls) n=tls_recv(tls,fd,resp+len,4096);
#if defined(_WIN32)
        else n=recv(fd,resp+len,4096,0);
#else
        else n=(int)recv(fd,resp+len,4096,0);
#endif
        if(n>0){ len+=(size_t)n; continue; }
        break;
    }
    tls_free_session(tls);
    sock_closefd(fd);
    size_t hs=0;
    while(hs+3<len && !(resp[hs]=='\r'&&resp[hs+1]=='\n'&&resp[hs+2]=='\r'&&resp[hs+3]=='\n')) hs++;
    if(hs+3>=len){ free(resp); snprintf(err,errcap,"http: bad response"); return 0; }
    size_t hlen=hs, blen=len-(hs+4);
    char *bpart=resp+hs+4;
    *out_code=http_status(resp,hlen);
    if(http_has_chunked(resp,hlen)) blen=http_dechunk(bpart,blen);
    char *out=(char*)malloc(blen+1);
    if(!out){ free(resp); snprintf(err,errcap,"out of memory"); return 0; }
    memcpy(out,bpart,blen); out[blen]=0;
    free(resp);
    *out_body=out; *out_len=blen;
    return 1;
}
/* ---- WebSocket client (RFC 6455). Only the client role: we always mask.
 * Control frames: ping is auto-answered, pong ignored, close ends the
 * stream. Fragmented messages are reassembled. wire format knowledge stays
 * here; Socket carries rbuf (unparsed bytes) + frag (partial message). */
static unsigned ws_rotl(unsigned x,int n){ return (x<<n)|(x>>(32-n)); }
static void ws_sha1(const unsigned char *msg,size_t len,unsigned char out[20]){
    unsigned h0=0x67452301,h1=0xEFCDAB89,h2=0x98BADCFE,h3=0x10325476,h4=0xC3D2E1F0;
    unsigned long long bitlen=(unsigned long long)len*8;
    size_t newlen=len+1;
    while(newlen%64!=56) newlen++;
    unsigned char *m=(unsigned char*)malloc(newlen+8);
    if(!m){ memset(out,0,20); return; }
    memcpy(m,msg,len); m[len]=0x80;
    memset(m+len+1,0,newlen-len-1);
    for(int i=0;i<8;i++) m[newlen+i]=(unsigned char)(bitlen>>(56-8*i));
    newlen+=8;
    for(size_t off=0;off<newlen;off+=64){
        unsigned w[80];
        for(int i=0;i<16;i++)
            w[i]=((unsigned)m[off+4*i]<<24)|((unsigned)m[off+4*i+1]<<16)|
                 ((unsigned)m[off+4*i+2]<<8)|(unsigned)m[off+4*i+3];
        for(int i=16;i<80;i++)
            w[i]=ws_rotl(w[i-3]^w[i-8]^w[i-14]^w[i-16],1);
        unsigned a=h0,b=h1,c=h2,d=h3,e=h4;
        for(int i=0;i<80;i++){
            unsigned f,k;
            if(i<20){ f=(b&c)|((~b)&d); k=0x5A827999; }
            else if(i<40){ f=b^c^d; k=0x6ED9EBA1; }
            else if(i<60){ f=(b&c)|(b&d)|(c&d); k=0x8F1BBCDC; }
            else { f=b^c^d; k=0xCA62C1D6; }
            unsigned t=ws_rotl(a,5)+f+e+k+w[i];
            e=d; d=c; c=ws_rotl(b,30); b=a; a=t;
        }
        h0+=a; h1+=b; h2+=c; h3+=d; h4+=e;
    }
    free(m);
    unsigned hs[5]={h0,h1,h2,h3,h4};
    for(int i=0;i<5;i++){
        out[4*i]=(unsigned char)(hs[i]>>24); out[4*i+1]=(unsigned char)(hs[i]>>16);
        out[4*i+2]=(unsigned char)(hs[i]>>8); out[4*i+3]=(unsigned char)hs[i];
    }
}
static void ws_b64(const unsigned char *in,size_t n,char *out){
    static const char *A="ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    size_t i=0,o=0;
    while(i+3<=n){ out[o++]=A[in[i]>>2]; out[o++]=A[((in[i]&3)<<4)|(in[i+1]>>4)];
        out[o++]=A[((in[i+1]&15)<<2)|(in[i+2]>>6)]; out[o++]=A[in[i+2]&63]; i+=3; }
    if(i<n){ out[o++]=A[in[i]>>2];
        if(i+1<n){ out[o++]=A[((in[i]&3)<<4)|(in[i+1]>>4)]; out[o++]=A[(in[i+1]&15)<<2]; }
        else { out[o++]=A[(in[i]&3)<<4]; out[o++]='='; }
        out[o++]='=';
        if(i+1>=n) out[o-2]='=';
    }
    out[o]=0;
}
/* send one masked text frame over fd (+tls). Blocking, like HTTP. */
static int ws_send_frame(TLSSession *tls,sock_t fd,const char *p,size_t n,
                         char *err,size_t errcap){
    unsigned char h[10]; size_t hl=2;
    h[0]=0x81;   /* FIN + text */
    unsigned char mask[4];
    unsigned rnd=(unsigned)rand()*2654435761u ^ (unsigned)luc_now();
    mask[0]=(unsigned char)(rnd>>24); mask[1]=(unsigned char)(rnd>>16);
    mask[2]=(unsigned char)(rnd>>8); mask[3]=(unsigned char)rnd;
    if(n<126){ h[1]=(unsigned char)(0x80|n); }
    else if(n<65536){ h[1]=0x80|126; h[2]=(unsigned char)(n>>8); h[3]=(unsigned char)(n&255); hl=4; }
    else return 0;   /* messages bigger than 64K are split by callers */
    char *frame=(char*)malloc(hl+4+n);
    if(!frame) return 0;
    memcpy(frame,h,hl); memcpy(frame+hl,mask,4);
    for(size_t i=0;i<n;i++) frame[hl+4+i]=p[i]^mask[i&3];
    int ok=hsend(tls,fd,frame,hl+4+n,err,errcap);
    free(frame);
    return ok;
}
/* try to extract one message from s->rbuf (non-blocking).
 * Returns: 1 message ready (*outp/*outn malloc'd), 0 need more data,
 * -1 fatal protocol error, -2 orderly close. Ping is auto-answered. */
/* hexdump of the offending bytes when LUC_WSDEBUG is set */
static void ws_debug_dump(Socket *s,const char *why){
    if(!getenv("LUC_WSDEBUG")) return;
    fprintf(stderr,"ws-debug [%s] rlen=%d head:",why,(int)s->rlen);
    size_t n=s->rlen>48?48:s->rlen;
    for(size_t i=0;i<n;i++) fprintf(stderr," %02x",(unsigned char)s->rbuf[i]);
    fprintf(stderr,"\n");
}
static int ws_pull_message(Socket *s,sock_t fd,void *tlsv,char **outp,size_t *outn,
                           size_t maxmsg,char *err,size_t errcap){
    TLSSession *tls=(TLSSession*)tlsv;
    for(;;){
        if(s->rlen<2) return 0;
        unsigned char *b=(unsigned char*)s->rbuf;
        int fin=(b[0]&0x80)!=0, op=b[0]&0x0F;
        if(b[1]&0x80){ snprintf(err,errcap,"ws: server sent masked frame"); ws_debug_dump(s,"masked"); return -1; }
        unsigned long long pay=b[1]&0x7F;
        size_t hlen=2;
        if(pay==126){
            if(s->rlen<4) return 0;
            pay=((unsigned long long)b[2]<<8)|b[3]; hlen=4;
        } else if(pay==127){
            if(s->rlen<10) return 0;
            pay=0;
            for(int i=0;i<8;i++) pay=(pay<<8)|b[2+i];
            hlen=10;
            if(pay>16*1024*1024){ snprintf(err,errcap,"ws: frame too large"); return -1; }
        }
        if(op>=0x8 && (!fin || pay>125)){
            snprintf(err,errcap,"ws: bad control frame"); ws_debug_dump(s,"badctl"); return -1;
        }
        if((unsigned long long)(s->rlen-hlen)<pay) return 0;  /* partial: wait */
        const char *payload=(const char*)(b+hlen);
        if(op==0x8){   /* close: report code+reason, consume the frame */
            if(pay>=2){
                int code=((unsigned char)payload[0]<<8)|(unsigned char)payload[1];
                size_t rlen=(size_t)pay-2;
                if(rlen>120) rlen=120;
                if(rlen>0) snprintf(err,errcap,"ws closed (%d) %.*s",code,(int)rlen,payload+2);
                else snprintf(err,errcap,"ws closed (%d)",code);
            } else snprintf(err,errcap,"ws closed");
            memmove(s->rbuf,s->rbuf+hlen+(size_t)pay,s->rlen-hlen-(size_t)pay);
            s->rlen-=hlen+(size_t)pay;
            return -2;
        }
        if(op==0x9){   /* ping -> pong, then continue with next frame */
            unsigned char pong[130]; size_t pl=pay>125?125:(size_t)pay;
            pong[0]=0x8A; pong[1]=(unsigned char)(0x80|pl);
            unsigned char mk[4]={0x12,0x34,0x56,0x78};
            memcpy(pong+2,mk,4);
            for(size_t i=0;i<pl;i++) pong[6+i]=payload[i]^mk[i&3];
            if(tls) tls_send(tls,fd,(const char*)pong,6+pl);
            else send(fd,(const char*)pong,(int)(6+pl),0);
            memmove(s->rbuf,s->rbuf+hlen+(size_t)pay,s->rlen-hlen-(size_t)pay);
            s->rlen-=hlen+(size_t)pay;
            continue;
        }
        if(op==0xA){   /* pong: drop */
            memmove(s->rbuf,s->rbuf+hlen+(size_t)pay,s->rlen-hlen-(size_t)pay);
            s->rlen-=hlen+(size_t)pay;
            continue;
        }
        if(op!=0x0 && op!=0x1 && op!=0x2){
            snprintf(err,errcap,"ws: bad opcode %d",op); ws_debug_dump(s,"badop"); return -1;
        }
        /* text/binary/continuation: append to reassembly */
        if(s->fraglen+(size_t)pay>maxmsg){ snprintf(err,errcap,"ws: message too large"); return -1; }
        if(s->fraglen+(size_t)pay>s->fragcap){
            size_t nc=s->fragcap?s->fragcap*2:4096;
            while(nc<s->fraglen+(size_t)pay) nc*=2;
            char *np=(char*)realloc(s->frag,nc);
            if(!np){ snprintf(err,errcap,"out of memory"); return -1; }
            s->frag=np; s->fragcap=nc;
        }
        memcpy(s->frag+s->fraglen,payload,(size_t)pay);
        s->fraglen+=(size_t)pay;
        memmove(s->rbuf,s->rbuf+hlen+(size_t)pay,s->rlen-hlen-(size_t)pay);
        s->rlen-=hlen+(size_t)pay;
        if(fin){
            *outp=s->frag; *outn=s->fraglen;
            s->frag=NULL; s->fraglen=s->fragcap=0;
            return 1;
        }
    }
}
/* build one masked client text frame; caller frees *out. 0 ok. */
static int ws_build_frame(const char *p,size_t n,char **out,size_t *outlen){
    if(n>65535) return -1;
    size_t hl=n<126?2:4;
    char *f=(char*)malloc(hl+4+n);
    if(!f) return -1;
    unsigned char mask[4];
    unsigned rnd=(unsigned)rand()*2654435761u ^ (unsigned)luc_now();
    mask[0]=(unsigned char)(rnd>>24); mask[1]=(unsigned char)(rnd>>16);
    mask[2]=(unsigned char)(rnd>>8); mask[3]=(unsigned char)rnd;
    f[0]=(char)0x81;
    if(n<126){ f[1]=(char)(0x80|n); }
    else { f[1]=(char)(0x80|126); f[2]=(char)(n>>8); f[3]=(char)(n&255); }
    memcpy(f+hl,mask,4);
    for(size_t i=0;i<n;i++) f[hl+4+i]=p[i]^mask[i&3];
    *out=f; *outlen=hl+4+n;
    return 0;
}
/* read available bytes into rbuf once (non-blocking).
 * 1 = new data, 0 = none right now, -1 = dead/closed. */
static int ws_fill(Socket *s,sock_t fd,void *tlsv){
    if(s->rlen+4096>s->rcap){
        size_t nc=s->rcap?s->rcap*2:16384;
        while(nc<s->rlen+4096) nc*=2;
        char *np=(char*)realloc(s->rbuf,nc);
        if(!np) return -1;
        s->rbuf=np; s->rcap=nc;
    }
    TLSSession *tls=(TLSSession*)tlsv;
    int n;
    int rl0=(int)s->rlen;
    if(tls){
        n=tls_recv(tls,fd,s->rbuf+s->rlen,4096);
        if(n==-2) return 0;
        if(n==0) return -1;
    } else {
#if defined(_WIN32)
        n=recv(fd,s->rbuf+s->rlen,4096,0);
#else
        n=(int)recv(fd,s->rbuf+s->rlen,4096,0);
#endif
        if(n==0) return -1;
        if(n<0) return sock_wouldblock()?0:-1;
    }
    if(n<0) return -1;
    if(n>0) s->rlen+=(size_t)n;
    if(getenv("LUC_TLSDEBUG")) fprintf(stderr,"tlsfill: n=%d rlen=%d->%d\n",n,rl0,(int)s->rlen);
    return n>0?1:0;
}
LFN(f_net_ws_connect){
    Str *u=checkstr(L,base,nargs,0,"ws_connect");
    double timeout=nargs>=2?checknum(L,base,nargs,1,"ws_connect"):10;
    char *url=(char*)malloc((size_t)u->len+1);
    if(!url) luc_error("net: out of memory");
    memcpy(url,u->s,(size_t)u->len); url[u->len]=0;
    char err[192]; err[0]=0;
    const char *p=url;
    int iswss=0;
    if(strncmp(p,"ws://",5)==0) p+=5;
    else if(strncmp(p,"wss://",6)==0){ p+=6; iswss=1; }
    else { snprintf(err,sizeof err,"bad url (need ws(s)://host/path)"); goto failurl; }
    {
        const char *slash=strchr(p,'/');
        const char *hostend=slash?slash:p+strlen(p);
        char host[256]; int port=iswss?443:80;
        const char *colon=strchr(p,':');
        if(colon && colon<hostend){
            size_t hl=(size_t)(colon-p);
            if(hl>=sizeof host) hl=sizeof host-1;
            memcpy(host,p,hl); host[hl]=0;
            port=atoi(colon+1); if(port<=0||port>65535) port=iswss?443:80;
        } else {
            size_t hl=(size_t)(hostend-p);
            if(hl>=sizeof host) hl=sizeof host-1;
            memcpy(host,p,hl); host[hl]=0;
        }
        const char *path=slash?slash:"/";
        sock_t fd=net_connect_to(host,port,timeout,err,sizeof err);
        if(fd==SOCK_INVALID) goto failurl;
        sock_set_blocking(fd,1);
        sock_set_timeout(fd,1,30); sock_set_timeout(fd,0,30);
        TLSSession *tls=NULL;
        if(iswss){
            if(tls_connect_fd(fd,host,&tls,err,sizeof err)!=0){ sock_closefd(fd); goto failurl; }
        }
        /* handshake */
        unsigned char nonce[16];
        for(int i=0;i<16;i++) nonce[i]=(unsigned char)(rand()&255);
        char key[32]; ws_b64(nonce,16,key);
        char hs[1024];
        int hn=snprintf(hs,sizeof hs,
            "GET %s HTTP/1.1\r\nHost: %s\r\nUpgrade: websocket\r\nConnection: Upgrade\r\nSec-WebSocket-Key: %s\r\nSec-WebSocket-Version: 13\r\n\r\n",
            path,host,key);
        if(hn<=0||hn>=(int)sizeof hs-1){ snprintf(err,sizeof err,"url too long"); tls_free_session(tls); sock_closefd(fd); goto failurl; }
        if(!hsend(tls,fd,hs,(size_t)hn,err,sizeof err)){ tls_free_session(tls); sock_closefd(fd); goto failurl; }
        char resp[4096]; size_t rlen=0;
        int got101=0;
        for(;;){
            int n;
            if(tls) n=tls_recv(tls,fd,resp+rlen,sizeof resp-rlen-1);
#if defined(_WIN32)
            else n=recv(fd,resp+rlen,(int)(sizeof resp-rlen-1),0);
#else
            else n=(int)recv(fd,resp+rlen,sizeof resp-rlen-1,0);
#endif
            if(n<=0) break;
            rlen+=(size_t)n;
            resp[rlen]=0;
            if(strstr(resp,"\r\n\r\n")){ got101=strstr(resp," 101 ")!=NULL; break; }
            if(rlen>sizeof resp-512) break;
        }
        if(!got101){ snprintf(err,sizeof err,"ws: handshake rejected"); tls_free_session(tls); sock_closefd(fd); goto failurl; }
        /* verify Sec-WebSocket-Accept */
        {
            char combo[128]; unsigned char digest[20]; char expect[32];
            snprintf(combo,sizeof combo,"%s258EAFA5-E914-47DA-95CA-C5AB0DC85B11",key);
            ws_sha1((unsigned char*)combo,strlen(combo),digest);
            ws_b64(digest,20,expect);
            if(!strstr(resp,expect)){ snprintf(err,sizeof err,"ws: bad accept key"); tls_free_session(tls); sock_closefd(fd); goto failurl; }
        }
        sock_set_blocking(fd,0);
        Socket *s=sock_wrap(fd,0);
        s->isws=1; s->tlsctx=tls;
        free(url);
        RET(0,mkobj(LT_SOCKET,s)); return 1;
    }
failurl:
    free(url);
    RET(0,NIL); RET(1,cstrv(err)); return 2;
}
LFN(f_net_get){
    Str *u=checkstr(L,base,nargs,0,"get");
    Str *h=nargs>=2&&AR(1).t!=LT_NIL?checkstr(L,base,nargs,1,"get"):NULL;
    char *url=(char*)malloc((size_t)u->len+1);
    if(!url) luc_error("net: out of memory");
    memcpy(url,u->s,(size_t)u->len); url[u->len]=0;
    char *hdrs=NULL;
    if(h){ hdrs=(char*)malloc((size_t)h->len+1); if(hdrs){ memcpy(hdrs,h->s,(size_t)h->len); hdrs[h->len]=0; } }
    char err[192]; err[0]=0;
    char *body=NULL; size_t blen=0; int code=0;
    int ok=net_http(url,"GET",NULL,0,NULL,hdrs,&body,&blen,&code,err,sizeof err);
    free(url); free(hdrs);
    if(!ok){ RET(0,NIL); RET(1,cstrv(err)); return 2; }
    Value v=strv(body,blen); free(body);
    RET(0,v); RET(1,mknum((double)code)); return 2;
}
LFN(f_net_post){
    Str *u=checkstr(L,base,nargs,0,"post");
    Str *b=(nargs>=2&&AR(1).t!=LT_NIL)?checkstr(L,base,nargs,1,"post"):NULL;
    Str *ct=(nargs>=3&&AR(2).t!=LT_NIL)?checkstr(L,base,nargs,2,"post"):NULL;
    Str *hd=(nargs>=4&&AR(3).t!=LT_NIL)?checkstr(L,base,nargs,3,"post"):NULL;
    char *url=(char*)malloc((size_t)u->len+1);
    if(!url) luc_error("net: out of memory");
    memcpy(url,u->s,(size_t)u->len); url[u->len]=0;
    char err[192]; err[0]=0;
    char *rbody=NULL; size_t rlen=0; int code=0;
    char *hdrs=NULL;
    if(hd){ hdrs=(char*)malloc((size_t)hd->len+1); if(hdrs){ memcpy(hdrs,hd->s,(size_t)hd->len); hdrs[hd->len]=0; } }
    int ok=net_http(url,"POST",b?b->s:NULL,b?(int)b->len:0,ct?ct->s:NULL,hdrs,
                    &rbody,&rlen,&code,err,sizeof err);
    free(url); free(hdrs);
    if(!ok){ RET(0,NIL); RET(1,cstrv(err)); return 2; }
    Value v=strv(rbody,rlen); free(rbody);
    RET(0,v); RET(1,mknum((double)code)); return 2;
}
/* The user-facing recv/accept retry in ordinary LUC (so task.wait yields
 * correctly and the call transparently retries). Raw attempts ride in as
 * factory parameters, becoming upvalues of the two methods. */
static const char *net_methods_src =
"return function(raw_recv, raw_accept)\n"
"  create methods = {}\n"
"  methods.recv = function(s, max, timeout)\n"
"    create t0 = os.clock()\n"
"    while true do\n"
"      create m, e = raw_recv(s, max)\n"
"      if m != nil then return m end\n"
"      if e != nil then return nil, e end\n"
"      if timeout != nil and os.clock() - t0 >= timeout then return nil, \"timeout\" end\n"
"      task.wait(0.01)\n"
"    end\n"
"  end\n"
"  methods.accept = function(s, timeout)\n"
"    create t0 = os.clock()\n"
"    while true do\n"
"      create c = raw_accept(s)\n"
"      if c != nil then return c end\n"
"      if timeout != nil and os.clock() - t0 >= timeout then return nil, \"timeout\" end\n"
"      task.wait(0.01)\n"
"    end\n"
"  end\n"
"  return methods\n"
"end\n";
static int net_methods_built=0;
static void net_build_methods(LucState *L){
    if(net_methods_built) return;
    net_methods_built=1;
    Closure *cl=luc_compile(net_methods_src,(int)strlen(net_methods_src),"net methods");
    int sc=L->top;
    ensure_stack(L,sc+16);
    L->stack[sc]=mkobj(LT_FUNC,cl);
    L->top=sc+1;
    vm_call(L,sc,0,1);             /* outer() -> inner factory at [sc] */
    L->stack[sc+1]=mkobj(LT_CFUNC,cfunc_new(f_sock_recv_try,"__recv_try",0));
    L->stack[sc+2]=mkobj(LT_CFUNC,cfunc_new(f_sock_accept_try,"__accept_try",0));
    L->top=sc+3;
    vm_call(L,sc,2,1);             /* inner(raw1,raw2) -> methods at [sc] */
    Value methods=L->stack[sc];
    if(methods.t!=LT_TABLE) luc_error("net: internal error building methods");
    L->top=sc+3;   /* keep everything rooted while interning below */
    tab_set(V.socklib,cstrv("__recv_try"),L->stack[sc+1]);
    tab_set(V.socklib,cstrv("__accept_try"),L->stack[sc+2]);
    tab_set(V.socklib,cstrv("recv"),tab_get(AS_TAB(methods),cstrv("recv")));
    tab_set(V.socklib,cstrv("accept"),tab_get(AS_TAB(methods),cstrv("accept")));
    L->top=sc;
}
Value lucL_net_module(LucState *L){
    net_startup();
    Table *t=tab_new(0);
    tab_set(t,cstrv("connect"),mkobj(LT_CFUNC,cfunc_new(f_net_connect,"connect",0)));
    tab_set(t,cstrv("serve"),mkobj(LT_CFUNC,cfunc_new(f_net_serve,"serve",0)));
    tab_set(t,cstrv("get"),mkobj(LT_CFUNC,cfunc_new(f_net_get,"get",0)));
    tab_set(t,cstrv("post"),mkobj(LT_CFUNC,cfunc_new(f_net_post,"post",0)));
    tab_set(t,cstrv("ws_connect"),mkobj(LT_CFUNC,cfunc_new(f_net_ws_connect,"ws_connect",0)));
    tab_set(t,cstrv("raw_recv"),mkobj(LT_CFUNC,cfunc_new(f_sock_recv_try,"raw_recv",0)));
    tab_set(t,cstrv("raw_accept"),mkobj(LT_CFUNC,cfunc_new(f_sock_accept_try,"raw_accept",0)));
    net_build_methods(L);
    return mkobj(LT_TABLE,t);
}
void lucL_open_net(void){
    net_startup();
    V.socklib=tab_new(0);
    reg(V.socklib,"send",f_sock_send); reg(V.socklib,"close",f_sock_close);
}
