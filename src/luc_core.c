/* LUC core: platform, values, GC, tables, lexer, parser, compiler, VM, modules, CLI */

/* 2026-09-01: added luc install */

#include "luc.h"
#define LUC_JIT_FALLBACK (-1)
int  luc_jit_run(Closure *cl);
int  luc_jit_call(LucState *L,int func,int nargs,int nres);
int  luc_aot_build(const char *src,int srclen,const char *outpath);
int  luc_aot_embedded(char **psrc,int *plen);
#if defined(_WIN32)
#  include <windows.h>
#  include <winhttp.h>
#  include <process.h>
#else
#  include <sys/time.h>
#  include <sys/stat.h>
#  include <unistd.h>
#endif

/* platform helpers */

double luc_now(void){
#if defined(_WIN32)
    static LARGE_INTEGER f; static int init=0; LARGE_INTEGER c;
    if(!init){ QueryPerformanceFrequency(&f); init=1; }
    QueryPerformanceCounter(&c);
    return (double)c.QuadPart/(double)f.QuadPart;
#else
    struct timeval tv; gettimeofday(&tv,NULL);
    return (double)tv.tv_sec + (double)tv.tv_usec*1e-6;
#endif
}
void luc_sleep(double s){
    if(s<=0) return;
#if defined(_WIN32)
    Sleep((DWORD)(s*1000.0));
#else
    struct timespec ts;
    ts.tv_sec  = (time_t)s;
    ts.tv_nsec = (long)((s-(double)ts.tv_sec)*1e9);
    nanosleep(&ts,NULL);
#endif
}

void *lmalloc(size_t n){
    void *p = malloc(n?n:1);
    if(!p){ fprintf(stderr,"luc: out of memory\n"); exit(1); }
    return p;
}
void *lrealloc(void *p,size_t n){
    void *q = realloc(p,n?n:1);
    if(!q){ fprintf(stderr,"luc: out of memory\n"); exit(1); }
    return q;
}
void *lcalloc(size_t n){ void*p=lmalloc(n); memset(p,0,n); return p; }

Value NIL = { LT_NIL, {0} };
LucV V;                     /* global VM state (extern in luc.h) */

int truthy(Value v){ return !(v.t==LT_NIL || (v.t==LT_BOOL && !v.u.b)); }

/* errors, allocation, strings, constructors */

void luc_throw(Value err){
    V.errval = err;
    if(V.errjmp) longjmp(V.errjmp->jb,1);
    fprintf(stderr,"luc: unprotected error: %s\n",
            err.t==LT_STR?AS_STR(err)->s:type_name(err));
    exit(1);
}

void luc_error(const char *fmt,...){
    char buf[1024]; char msg[1200]; va_list ap;
    va_start(ap,fmt); vsnprintf(buf,sizeof buf,fmt,ap); va_end(ap);
    if(V.cur && V.cur->cursource)
        snprintf(msg,sizeof msg,"%s:%d: %s",V.cur->cursource->s,V.cur->curline,buf);
    else
        snprintf(msg,sizeof msg,"%s",buf);
    luc_throw(mkobj(LT_STR,str_fromc(msg)));
}

/* object allocation */
static Obj *newobj(size_t sz,int type){
    Obj *o = (Obj*)lcalloc(sz);
    o->type=(unsigned char)type; o->marked=0;
    o->next=V.objects; V.objects=o;
    V.nalloc++;
    return o;
}

/* interned strings */
static unsigned strhash(const char *s,int len){
    unsigned h=2166136261u;
    for(int i=0;i<len;i++){ h^=(unsigned char)s[i]; h*=16777619u; }
    return h;
}
static void strtab_grow(void){
    int nc = V.strcap*2, i;
    Str **nt = (Str**)lcalloc(sizeof(Str*)*nc);
    for(i=0;i<V.strcap;i++){
        Str *s=V.strtab[i];
        while(s){ Str *nx=s->snext; unsigned k=s->hash&(nc-1);
                  s->snext=nt[k]; nt[k]=s; s=nx; }
    }
    free(V.strtab); V.strtab=nt; V.strcap=nc;
}
Str *str_new(const char *s,int len){
    unsigned h=strhash(s,len);
    unsigned k=h&(unsigned)(V.strcap-1);
    for(Str *p=V.strtab[k];p;p=p->snext)
        if(p->len==len && memcmp(p->s,s,(size_t)len)==0) return p;
    Str *ns=(Str*)lmalloc(sizeof(Str)+(size_t)len);
    ns->o.type=LT_STR; ns->o.marked=0; ns->o.next=NULL;
    ns->len=len; ns->hash=h;
    if(len) memcpy(ns->s,s,(size_t)len);
    ns->s[len]=0;
    ns->snext=V.strtab[k]; V.strtab[k]=ns;
    V.nstr++; V.nalloc++;
    if(V.nstr > V.strcap) strtab_grow();
    return ns;
}

/* constructors */
Table *tab_new(int islist){
    Table *t=(Table*)newobj(sizeof(Table), islist?LT_LIST:LT_TABLE);
    t->tid=++V.tidcounter;
    t->meta=NULL;
    return t;
}
Buffer *buf_new(int n){
    Buffer *b=(Buffer*)newobj(sizeof(Buffer),LT_BUFFER);
    b->len=n; b->b=(unsigned char*)lcalloc((size_t)(n>0?n:1));
    return b;
}
static Proto *proto_new(void){
    Proto *p=(Proto*)newobj(sizeof(Proto),LT_PROTO);
    p->maxstack=2; return p;
}
static Closure *closure_new(Proto *p){
    Closure *c=(Closure*)newobj(sizeof(Closure),LT_FUNC);
    c->p=p; c->nup=p->nup;
    c->up=(Upval**)lcalloc(sizeof(Upval*)*(size_t)(p->nup>0?p->nup:1));
    return c;
}
CFunc *cfunc_new(CFn fn,const char *name,int nup){
    CFunc *c=(CFunc*)newobj(sizeof(CFunc),LT_CFUNC);
    c->fn=fn; c->name=name; c->nup=nup;
    c->up = nup? (Value*)lcalloc(sizeof(Value)*(size_t)nup) : NULL;
    return c;
}
FileH *file_new(FILE *f,int isstd){
    FileH *h=(FileH*)newobj(sizeof(FileH),LT_FILE);
    h->f=f; h->isstd=isstd; h->closed=0; return h;
}
Socket *sock_new(intptr_t fd,int isserver){
    Socket *s=(Socket*)newobj(sizeof(Socket),LT_SOCKET);
    s->fd=fd; s->closed=0; s->isserver=(unsigned char)(isserver!=0); s->tlsctx=NULL;
    s->isws=0; s->rbuf=NULL; s->frag=NULL;
    s->rlen=s->rcap=s->fraglen=s->fragcap=0; return s;
}
LucState *state_new(int stacksize){
    LucState *L=(LucState*)newobj(sizeof(LucState),LT_CORO);
    L->stacksize=stacksize;
    L->stack=(Value*)lcalloc(sizeof(Value)*(size_t)stacksize);
    L->cicap=16; L->ci=(CallInfo*)lcalloc(sizeof(CallInfo)*16);
    L->status=CO_START;
    return L;
}
void ensure_stack(LucState *L,int need){
    if(need <= L->stacksize) return;
    if(need > LUC_MAXSTACK) luc_error("stack overflow");
    int ns=L->stacksize*2; while(ns<need) ns*=2;
    if(ns>LUC_MAXSTACK) ns=LUC_MAXSTACK;
    L->stack=(Value*)lrealloc(L->stack,sizeof(Value)*(size_t)ns);
    for(int i=L->stacksize;i<ns;i++) L->stack[i]=NIL;
    L->stacksize=ns;
}

/* tables */

static unsigned val_hash(Value v){
    switch(v.t){
        case LT_NIL:  return 0;
        case LT_BOOL: return v.u.b?1u:2u;
        case LT_NUM: {
            double d=v.u.n;
            if(d==(double)(long long)d) return (unsigned)((long long)d)*2654435761u;
            unsigned char *p=(unsigned char*)&d; return strhash((char*)p,(int)sizeof d);
        }
        case LT_STR:  return AS_STR(v)->hash;
        default: { uintptr_t x=(uintptr_t)v.u.o; return (unsigned)(x>>3)*2654435761u; }
    }
}
int val_rawequal(Value a,Value b){
    if(a.t!=b.t) return 0;
    switch(a.t){
        case LT_NIL:  return 1;
        case LT_BOOL: return a.u.b==b.u.b;
        case LT_NUM:  return a.u.n==b.u.n;
        default:      return a.u.o==b.u.o;   /* strings are interned */
    }
}

static void hash_grow(Table *t);

static Entry *hash_find(Table *t,Value k){
    if(t->ecap==0) return NULL;
    unsigned i=val_hash(k)&(unsigned)(t->ecap-1);
    for(;;){
        Entry *e=&t->ents[i];
        if(e->k.t==LT_NIL) return NULL;              /* empty slot: not found */
        if(val_rawequal(e->k,k)) return e;
        i=(i+1)&(unsigned)(t->ecap-1);
    }
}
static void hash_set(Table *t,Value k,Value v){
    Entry *e=hash_find(t,k);
    if(e){ e->v=v; return; }
    if(v.t==LT_NIL) return;
    if(t->ecap==0 || (t->ecount+1)*4 >= t->ecap*3) hash_grow(t);
    unsigned i=val_hash(k)&(unsigned)(t->ecap-1);
    while(t->ents[i].k.t!=LT_NIL) i=(i+1)&(unsigned)(t->ecap-1);
    t->ents[i].k=k; t->ents[i].v=v; t->ecount++;
}
static void hash_grow(Table *t){
    int nc = t->ecap? t->ecap*2 : 8;
    Entry *old=t->ents; int oc=t->ecap;
    t->ents=(Entry*)lcalloc(sizeof(Entry)*(size_t)nc);
    t->ecap=nc; t->ecount=0;
    for(int i=0;i<oc;i++)
        if(old[i].k.t!=LT_NIL && old[i].v.t!=LT_NIL) hash_set(t,old[i].k,old[i].v);
    free(old);
}

static int arr_index(Value k,int *out){
    if(k.t!=LT_NUM) return 0;
    double d=k.u.n;
    if(d!=floor(d) || d<1 || d>2147483000.0) return 0;
    *out=(int)d; return 1;
}
static void arr_reserve(Table *t,int n){
    if(n<=t->acap) return;
    int nc=t->acap? t->acap*2 : 8; while(nc<n) nc*=2;
    t->arr=(Value*)lrealloc(t->arr,sizeof(Value)*(size_t)nc);
    for(int i=t->acap;i<nc;i++) t->arr[i]=NIL;
    t->acap=nc;
}

Value tab_get(Table *t,Value k){
    int i;
    if(arr_index(k,&i)){
        if(i>=1 && i<=t->alen) return t->arr[i-1];
    }
    if(k.t==LT_NIL) return NIL;
    Entry *e=hash_find(t,k);
    return e? e->v : NIL;
}
void tab_set(Table *t,Value k,Value v){
    int i;
    if(k.t==LT_NIL) luc_error("table index is nil");
    if(k.t==LT_NUM && k.u.n!=k.u.n) luc_error("table index is NaN");
    if(arr_index(k,&i)){
        if(i>=1 && i<=t->alen){ t->arr[i-1]=v;
            while(t->alen>0 && t->arr[t->alen-1].t==LT_NIL) t->alen--;
            return; }
        if(i==t->alen+1 && v.t!=LT_NIL){
            arr_reserve(t,i); t->arr[i-1]=v; t->alen=i;
/* migrate following integer keys out of the hash part */
            for(;;){
                Value nk=mknum((double)(t->alen+1));
                Entry *e=hash_find(t,nk);
                if(!e || e->v.t==LT_NIL) break;
                arr_reserve(t,t->alen+1);
                t->arr[t->alen]=e->v; t->alen++; e->v=NIL;
            }
            return;
        }
    }
    hash_set(t,k,v);
}
int tab_len(Table *t){
    int n=t->alen;
    while(n>0 && t->arr[n-1].t==LT_NIL) n--;
    return n;
}
/* list helpers */
void list_push(Table *t,Value v){
    arr_reserve(t,t->alen+1); t->arr[t->alen++]=v;
}
void list_insert(Table *t,int pos,Value v){
    int n=t->alen;
    if(pos<1) pos=1;
    if(pos>n+1) pos=n+1;
    arr_reserve(t,n+1);
    for(int i=n;i>=pos;i--) t->arr[i]=t->arr[i-1];
    t->arr[pos-1]=v; t->alen=n+1;
}
Value list_removeat(Table *t,int pos){
    int n=t->alen;
    if(n==0) return NIL;
    if(pos<1||pos>n) return NIL;
    Value v=t->arr[pos-1];
    for(int i=pos-1;i<n-1;i++) t->arr[i]=t->arr[i+1];
    t->arr[n-1]=NIL; t->alen=n-1;
    return v;
}

/* Iterate array then hash */
int tab_next(Table *t,Value key,Value *ok,Value *ov){
    int i, start=0;
    if(key.t==LT_NIL) start=0;
    else if(arr_index(key,&i) && i>=1 && i<=t->alen) start=i;
    else {
        Entry *e=hash_find(t,key);
        if(!e) return 0;
        start=t->alen+(int)(e-t->ents)+1;
    }
    for(i=start;i<t->alen;i++)
        if(t->arr[i].t!=LT_NIL){ *ok=mknum((double)(i+1)); *ov=t->arr[i]; return 1; }
    for(i=start-t->alen;i<t->ecap;i++){
        if(i<0) i=0;
        if(t->ents[i].k.t!=LT_NIL && t->ents[i].v.t!=LT_NIL){
            *ok=t->ents[i].k; *ov=t->ents[i].v; return 1; }
    }
    return 0;
}

/* garbage collector */

static void mark_value(Value v);

static void mark_obj(Obj *o){
    if(!o || o->marked) return;
    o->marked=1;
    switch(o->type){
        case LT_STR: break;
        case LT_TABLE: case LT_LIST: {
            Table *t=(Table*)o;
            for(int i=0;i<t->alen;i++) mark_value(t->arr[i]);
            for(int i=0;i<t->ecap;i++){ mark_value(t->ents[i].k); mark_value(t->ents[i].v); }
            if(t->meta) mark_obj((Obj*)t->meta);
            break; }
        case LT_PROTO: {
            Proto *p=(Proto*)o;
            for(int i=0;i<p->nk;i++) mark_value(p->k[i]);
            for(int i=0;i<p->np;i++) mark_obj((Obj*)p->p[i]);
            if(p->name)  mark_obj((Obj*)p->name);
            if(p->source)mark_obj((Obj*)p->source);
            break; }
        case LT_FUNC: {
            Closure *c=(Closure*)o;
            mark_obj((Obj*)c->p);
            for(int i=0;i<c->nup;i++) mark_obj((Obj*)c->up[i]);
            break; }
        case LT_CFUNC: {
            CFunc *c=(CFunc*)o;
            for(int i=0;i<c->nup;i++) mark_value(c->up[i]);
            break; }
        case LT_UPVAL: {
            Upval *u=(Upval*)o;
            if(u->isclosed) mark_value(u->closed);
            else mark_obj((Obj*)u->L);
            break; }
        case LT_CORO: {
            LucState *L=(LucState*)o;
            for(int i=0;i<L->stacksize;i++) mark_value(L->stack[i]);
            for(int i=0;i<L->nci;i++) mark_obj((Obj*)L->ci[i].cl);
            for(Upval *u=L->openupv;u;u=u->next) mark_obj((Obj*)u);
            if(L->resumer) mark_obj((Obj*)L->resumer);
            if(L->cursource) mark_obj((Obj*)L->cursource);
            break; }
        default: break;
    }
}
static void mark_value(Value v){
    if(v.t>=LT_STR && v.u.o) mark_obj(v.u.o);
}

static void free_obj(Obj *o){
    switch(o->type){
        case LT_TABLE: case LT_LIST: { Table*t=(Table*)o; free(t->arr); free(t->ents); break; }
        case LT_PROTO: { Proto*p=(Proto*)o; free(p->code); free(p->lines); free(p->k); free(p->p); break; }
        case LT_FUNC:  { Closure*c=(Closure*)o; free(c->up); break; }
        case LT_CFUNC: { CFunc*c=(CFunc*)o; free(c->up); break; }
        case LT_BUFFER:{ Buffer*b=(Buffer*)o; free(b->b); break; }
        case LT_CORO:  { LucState*L=(LucState*)o; free(L->stack); free(L->ci); break; }
        case LT_FILE:  { FileH*f=(FileH*)o; if(f->f && !f->isstd && !f->closed){ if(f->ispipe){
#if defined(_WIN32)
            _pclose(f->f);
#else
            pclose(f->f);
#endif
        } else fclose(f->f); } break; }
        case LT_SOCKET: { Socket*s=(Socket*)o; if(!s->closed) net_socket_close_fd(s); break; }
        default: break;
    }
    free(o);
}

void gc_collect(void){
    if(V.gcoff) return;
    if(getenv("LUC_NOGC")) return;   /* debug escape hatch, off by default */
/* mark roots */
    mark_obj((Obj*)V.globals);
    mark_obj((Obj*)V.stringlib); mark_obj((Obj*)V.listmeta);
    mark_obj((Obj*)V.bufferlib); mark_obj((Obj*)V.filelib);
    if(V.socklib) mark_obj((Obj*)V.socklib);
    if(V.listcore) mark_obj((Obj*)V.listcore);
    if(V.tabmeta) mark_obj((Obj*)V.tabmeta);
    mark_obj((Obj*)V.mainco);
    if(V.loaded) mark_obj((Obj*)V.loaded);
    for(LucState *c=V.cur;c;c=c->resumer) mark_obj((Obj*)c);
    for(int i=0;i<V.nsched;i++) mark_obj((Obj*)V.sched[i].co);
    mark_value(V.errval);

/* sweep non-string objects */
    Obj **pp=&V.objects;
    while(*pp){
        Obj *o=*pp;
        if(o->marked){ o->marked=0; pp=&o->next; }
        else { *pp=o->next; free_obj(o); V.nalloc--; }
    }
/* sweep interned strings */
    for(int i=0;i<V.strcap;i++){
        Str **sp=&V.strtab[i];
        while(*sp){
            Str *s=*sp;
            if(s->o.marked){ s->o.marked=0; sp=&s->snext; }
            else { *sp=s->snext; free(s); V.nstr--; V.nalloc--; }
        }
    }
    V.gcthresh = V.nalloc*2 + 4096;
}

/* conversions / printing */

const char *type_name(Value v){
    switch(v.t){
        case LT_NIL:return "nil"; case LT_BOOL:return "boolean";
        case LT_NUM:return "number"; case LT_STR:return "string";
        case LT_TABLE:return "table"; case LT_LIST:return "list";
        case LT_FUNC: case LT_CFUNC:return "function";
        case LT_BUFFER:return "buffer"; case LT_CORO:return "thread";
        case LT_FILE:return "file"; case LT_SOCKET:return "socket";
    }
    return "userdata";
}
void num2str(double n,char *buf,size_t sz){
    if(n!=n){ snprintf(buf,sz,"nan"); return; }
    if(n==HUGE_VAL){ snprintf(buf,sz,"inf"); return; }
    if(n==-HUGE_VAL){ snprintf(buf,sz,"-inf"); return; }
    if(n==floor(n) && fabs(n)<1e15) snprintf(buf,sz,"%lld",(long long)n);
    else snprintf(buf,sz,"%.17g",n);
}
int str2num(const char *s,int len,double *out){
    char tmp[128]; char *end;
    if(len<=0||len>=(int)sizeof tmp) return 0;
    memcpy(tmp,s,(size_t)len); tmp[len]=0;
    char *p=tmp; while(*p&&isspace((unsigned char)*p)) p++;
    if(!*p) return 0;
    double d;
    if((p[0]=='0')&&(p[1]=='x'||p[1]=='X')) d=(double)strtoll(p,&end,16);
    else if(p[0]=='-'&&p[1]=='0'&&(p[2]=='x'||p[2]=='X')) d=-(double)strtoll(p+1,&end,16);
    else d=strtod(p,&end);
    if(end==p) return 0;
    while(*end&&isspace((unsigned char)*end)) end++;
    if(*end) return 0;
    *out=d; return 1;
}

static Str *v2str(Value v,int depth);
static Value meta_callv(LucState *L,Value f,Value *args,int n);   /* fwd: VM */

static Str *list_tostr(Table *t,int depth){
/* pretty-print lists: [1, 2, 3] */
    size_t cap=64,len=0; char *b=(char*)lmalloc(cap);
    #define PUT(str,n) do{ size_t _n=(size_t)(n); if(len+_n+1>cap){ while(len+_n+1>cap) cap*=2; b=(char*)lrealloc(b,cap);} memcpy(b+len,(str),_n); len+=_n; }while(0)
    PUT("[",1);
    for(int i=0;i<t->alen;i++){
        if(i){ PUT(", ",2); }
        Value e=t->arr[i];
        if(e.t==LT_STR){ PUT("\"",1); PUT(AS_STR(e)->s,AS_STR(e)->len); PUT("\"",1); }
        else { Str *s=v2str(e,depth+1); PUT(s->s,s->len); }
    }
    PUT("]",1); b[len]=0;
    Str *r=str_new(b,(int)len); free(b);
    #undef PUT
    return r;
}

static Str *dict_tostr(Table *t,int depth){
/* pretty-print dicts: {key: value} */
    size_t cap=64,len=0; char *b=(char*)lmalloc(cap);
    #define PUTD(str,n) do{ size_t _n=(size_t)(n); if(len+_n+1>cap){ while(len+_n+1>cap) cap*=2; b=(char*)lrealloc(b,cap);} memcpy(b+len,(str),_n); len+=_n; }while(0)
    int first=1;
    PUTD("{",1);
    for(int i=0;i<t->alen;i++){
        if(!first) PUTD(", ",2); first=0;
        Str *ks=v2str(mknum((double)(i+1)),depth+1); PUTD(ks->s,ks->len);
        PUTD(": ",2);
        Value e=t->arr[i];
        if(e.t==LT_STR){ PUTD("\"",1); PUTD(AS_STR(e)->s,AS_STR(e)->len); PUTD("\"",1); }
        else { Str *s=v2str(e,depth+1); PUTD(s->s,s->len); }
    }
    for(int i=0;i<t->ecap;i++){
        if(t->ents[i].k.t==LT_NIL || t->ents[i].v.t==LT_NIL) continue;
        if(!first) PUTD(", ",2); first=0;
        Value k=t->ents[i].k;
        if(k.t==LT_STR){ PUTD("\"",1); PUTD(AS_STR(k)->s,AS_STR(k)->len); PUTD("\"",1); }
        else { Str *ks=v2str(k,depth+1); PUTD(ks->s,ks->len); }
        PUTD(": ",2);
        Value e=t->ents[i].v;
        if(e.t==LT_STR){ PUTD("\"",1); PUTD(AS_STR(e)->s,AS_STR(e)->len); PUTD("\"",1); }
        else { Str *s=v2str(e,depth+1); PUTD(s->s,s->len); }
    }
    PUTD("}",1); b[len]=0;
    Str *r=str_new(b,(int)len); free(b);
    #undef PUTD
    return r;
}

static Str *v2str(Value v,int depth){
    char buf[80];
    if(depth>6) return str_fromc("...");
    switch(v.t){
        case LT_NIL:  return str_fromc("nil");
        case LT_BOOL: return str_fromc(v.u.b?"true":"false");
        case LT_NUM:  num2str(v.u.n,buf,sizeof buf); return str_fromc(buf);
        case LT_STR:  return AS_STR(v);
        case LT_LIST: return list_tostr(AS_TAB(v),depth);
        default: {
            if(v.t==LT_TABLE){
                Table *t=AS_TAB(v);
                if(t->meta){
                    Value h=tab_get(t->meta,mkobj(LT_STR,str_fromc("__tostring")));
                    if(h.t==LT_FUNC||h.t==LT_CFUNC){
                        Value r=meta_callv(V.cur,h,(Value[]){v},1);
                        if(r.t==LT_STR) return AS_STR(r);
                        if(r.t==LT_NUM||r.t==LT_BOOL) return tostr(r);
                    }
                }
                return dict_tostr(t,depth);
            }
            snprintf(buf,sizeof buf,"%s: %p",type_name(v),(void*)v.u.o);
            return str_fromc(buf);
        }
    }
}
Str *tostr(Value v){ return v2str(v,0); }

/* lexer, AST, parser, bytecode, compiler, VM */

enum {
    TK_EOF=256, TK_NAME, TK_NUMBER, TK_STRING,
    TK_AND, TK_BREAK, TK_DO, TK_ELSE, TK_ELSEIF, TK_END, TK_FALSE, TK_FOR,
    TK_FUNCTION, TK_IF, TK_IN, TK_CREATE, TK_NIL, TK_NOT, TK_OR, TK_REPEAT,
    TK_RETURN, TK_THEN, TK_TRUE, TK_UNTIL, TK_WHILE, TK_IMPORT, TK_AS,
    TK_COMMAND, TK_RUN, TK_MAKE, TK_GET, TK_PACK,
    TK_CONCAT, TK_DOTS, TK_EQ, TK_NE, TK_LE, TK_GE,
    TK_ADDEQ, TK_SUBEQ, TK_MULEQ, TK_DIVEQ
};

static const char *const kwnames[] = {
    "and","break","do","else","elseif","end","false","for","function","if",
    "in","create","nil","not","or","repeat","return","then","true","until","while","import","as",
    "command","run","make","get","pack"
};

#define NKW 28

typedef struct {
    const char *p, *end;
    int line;
    Str *source;
    int strict;           /* !strict: lexer-level checks (True/False/None) */
/* current token */
    int t; double num; Str *str; int tline;
/* lookahead */
    int has_ahead; int at; double anum; Str *astr; int atline;
} Lexer;

static void lex_error(Lexer *lx,const char *msg){
    char b[512];
    snprintf(b,sizeof b,"%s:%d: %s",lx->source->s,lx->line,msg);
    luc_throw(mkobj(LT_STR,str_fromc(b)));
}

static int lx_check_kw(Lexer *lx,const char *s,int len){
    for(int i=0;i<NKW;i++)
        if((int)strlen(kwnames[i])==len && memcmp(kwnames[i],s,(size_t)len)==0)
            return TK_AND+i;
    if(len==5 && memcmp(s,"local",5)==0)
        lex_error(lx,"'local' is Lua syntax - LUC declares variables with 'create'");
    return TK_NAME;
}

/* Long bracket [=[ ]=]: plain [[ ]] is a list, so strings need '=' */
static int lx_long_level(Lexer *lx,int incomment){
    const char *p=lx->p;
    if(*p!='[') return -1;
    const char *q=p+1; int lvl=0;
    while(q<lx->end && *q=='='){ lvl++; q++; }
    if(q<lx->end && *q=='[' && (lvl>0 || incomment)) return lvl;
    return -1;
}
static Str *lx_long_string(Lexer *lx,int lvl){
    lx->p += 2+lvl;                       /* skip [===[ */
    if(lx->p<lx->end && *lx->p=='\n'){ lx->line++; lx->p++; }
    const char *start=lx->p;
    for(;;){
        if(lx->p>=lx->end) lex_error(lx,"unfinished long string");
        if(*lx->p==']'){
            const char *q=lx->p+1; int n=0;
            while(q<lx->end && *q=='='){ n++; q++; }
            if(n==lvl && q<lx->end && *q==']'){
                Str *s=str_new(start,(int)(lx->p-start));
                lx->p=q+1; return s;
            }
        }
        if(*lx->p=='\n') lx->line++;
        lx->p++;
    }
}

static int lx_scan(Lexer *lx,double *num,Str **str){
    for(;;){
        if(lx->p>=lx->end) return TK_EOF;
        char c=*lx->p;
        if(c=='\n'){ lx->line++; lx->p++; continue; }
        if(c==' '||c=='\t'||c=='\r'||c=='\f'||c=='\v'){ lx->p++; continue; }
        if(c=='-'&&lx->p+1<lx->end&&lx->p[1]=='-'){
            lx->p+=2;
            if(lx->p<lx->end&&*lx->p=='['){
                int lvl=lx_long_level(lx,1);
                if(lvl>=0){ lx_long_string(lx,lvl); continue; }
            }
            while(lx->p<lx->end&&*lx->p!='\n') lx->p++;
            continue;
        }
        break;
    }
    char c=*lx->p;
/* identifiers / keywords */
    if(isalpha((unsigned char)c)||c=='_'){
        const char *s=lx->p;
        while(lx->p<lx->end&&(isalnum((unsigned char)*lx->p)||*lx->p=='_')) lx->p++;
        int len=(int)(lx->p-s);
        int t=lx_check_kw(lx,s,len);
        if(t==TK_NAME){
            *str=str_new(s,len);
            if(lx->strict){
                /* !strict: reject True/False/None with hint, avoid silent nil */
                if((len==4 && memcmp(s,"True",4)==0) || (len==5 && memcmp(s,"False",5)==0))
                    lex_error(lx,"strict: use lowercase 'true'/'false', not 'True'/'False'");
                if(len==4 && memcmp(s,"None",4)==0)
                    lex_error(lx,"strict: use 'nil', not 'None'");
                if(len==9 && memcmp(s,"undefined",9)==0)
                    lex_error(lx,"strict: use 'nil', not 'undefined'");
            }
        }
        return t;
    }
/* numbers */
    if(isdigit((unsigned char)c)||(c=='.'&&lx->p+1<lx->end&&isdigit((unsigned char)lx->p[1]))){
        const char *s=lx->p;
        if(c=='0'&&lx->p+1<lx->end&&(lx->p[1]=='x'||lx->p[1]=='X')){
            lx->p+=2;
            while(lx->p<lx->end&&isxdigit((unsigned char)*lx->p)) lx->p++;
            *num=(double)strtoull(s+2,NULL,16);
            return TK_NUMBER;
        }
        while(lx->p<lx->end&&isdigit((unsigned char)*lx->p)) lx->p++;
        if(lx->p<lx->end&&*lx->p=='.'){ lx->p++;
            while(lx->p<lx->end&&isdigit((unsigned char)*lx->p)) lx->p++; }
        if(lx->p<lx->end&&(*lx->p=='e'||*lx->p=='E')){
            lx->p++;
            if(lx->p<lx->end&&(*lx->p=='+'||*lx->p=='-')) lx->p++;
            while(lx->p<lx->end&&isdigit((unsigned char)*lx->p)) lx->p++;
        }
        char tmp[64]; int len=(int)(lx->p-s);
        if(len>=(int)sizeof tmp) lex_error(lx,"malformed number");
        memcpy(tmp,s,(size_t)len); tmp[len]=0;
        *num=strtod(tmp,NULL);
        return TK_NUMBER;
    }
/* short strings */
    if(c=='"'||c=='\''){
        char quote=c; lx->p++;
        size_t cap=32,len=0; char *b=(char*)lmalloc(cap);
        #define ADD(ch) do{ if(len+1>=cap){cap*=2;b=(char*)lrealloc(b,cap);} b[len++]=(char)(ch);}while(0)
        while(lx->p<lx->end && *lx->p!=quote){
            char ch=*lx->p;
            if(ch=='\n') lex_error(lx,"unfinished string");
            if(ch=='\\'){
                lx->p++;
                if(lx->p>=lx->end) lex_error(lx,"unfinished string");
                char e=*lx->p++;
                switch(e){
                    case 'n': ADD('\n'); break;  case 't': ADD('\t'); break;
                    case 'r': ADD('\r'); break;  case 'a': ADD('\a'); break;
                    case 'b': ADD('\b'); break;  case 'f': ADD('\f'); break;
                    case 'v': ADD('\v'); break;  case '\\':ADD('\\'); break;
                    case '"': ADD('"');  break;  case '\'':ADD('\''); break;
                    case '\n': ADD('\n'); lx->line++; break;
                    case 'x': { int v0=0,i;
                        for(i=0;i<2&&lx->p<lx->end&&isxdigit((unsigned char)*lx->p);i++){
                            char h=*lx->p++;
                            v0=v0*16+(isdigit((unsigned char)h)?h-'0':(tolower(h)-'a'+10));
                        }
                        ADD(v0); break; }
                    case 'z': while(lx->p<lx->end&&isspace((unsigned char)*lx->p)){
                                  if(*lx->p=='\n') lx->line++;
                                  lx->p++; }
                              break;
                    default:
                        if(isdigit((unsigned char)e)){
                            int v0=e-'0',i;
                            for(i=0;i<2&&lx->p<lx->end&&isdigit((unsigned char)*lx->p);i++)
                                v0=v0*10+(*lx->p++-'0');
                            ADD(v0);
                        } else lex_error(lx,"invalid escape sequence");
                }
            } else { ADD(ch); lx->p++; }
        }
        if(lx->p>=lx->end) lex_error(lx,"unfinished string");
        lx->p++;
        *str=str_new(b,(int)len); free(b);
        #undef ADD
        return TK_STRING;
    }
/* long strings [=[ ]=] */
    if(c=='['){
        int lvl=lx_long_level(lx,0);
        if(lvl>0){ *str=lx_long_string(lx,lvl); return TK_STRING; }
    }
/* operators */
    lx->p++;
    switch(c){
        case '=': if(lx->p<lx->end&&*lx->p=='='){lx->p++;return TK_EQ;} return '=';
        case '~': if(lx->p<lx->end&&*lx->p=='=')
                      lex_error(lx,"'~=' is Lua syntax - LUC compares with '!='");
                  lex_error(lx,"unexpected '~'"); break;
        case '!': if(lx->p<lx->end&&*lx->p=='='){lx->p++;return TK_NE;} lex_error(lx,"unexpected '!' (not equal is '!=', negation is 'not')"); break;
        case '<': if(lx->p<lx->end&&*lx->p=='='){lx->p++;return TK_LE;} return '<';
        case '>': if(lx->p<lx->end&&*lx->p=='='){lx->p++;return TK_GE;} return '>';
        case '+': if(lx->p<lx->end&&*lx->p=='='){lx->p++;return TK_ADDEQ;} return '+';
        case '-': if(lx->p<lx->end&&*lx->p=='='){lx->p++;return TK_SUBEQ;} return '-';
        case '*': if(lx->p<lx->end&&*lx->p=='='){lx->p++;return TK_MULEQ;} return '*';
        case '/': if(lx->p<lx->end&&*lx->p=='='){lx->p++;return TK_DIVEQ;} return '/';
        case '#': lex_error(lx,"'#' is Lua syntax - LUC gets lengths with len(x)"); break;
        case '.':
            if(lx->p<lx->end&&*lx->p=='.'){
                lx->p++;
                if(lx->p<lx->end&&*lx->p=='.'){ lx->p++; return TK_DOTS; }
                return TK_CONCAT;
            }
            return '.';
        default: return (unsigned char)c;
    }
    return TK_EOF;
}

static void lx_next(Lexer *lx){
    lx->tline=lx->line;
    if(lx->has_ahead){
        lx->t=lx->at; lx->num=lx->anum; lx->str=lx->astr; lx->tline=lx->atline;
        lx->has_ahead=0; return;
    }
    lx->t=lx_scan(lx,&lx->num,&lx->str);
    lx->tline=lx->line;
}
static int lx_peek(Lexer *lx){
    if(!lx->has_ahead){
        int sl=lx->line;
        lx->at=lx_scan(lx,&lx->anum,&lx->astr);
        lx->atline=lx->line; lx->has_ahead=1; (void)sl;
    }
    return lx->at;
}

/* 7. AST */

typedef enum {
    E_NIL,E_TRUE,E_FALSE,E_NUM,E_STR,E_VARARG,E_NAME,E_INDEX,E_SLICE,
    E_CALL,E_METHCALL,E_FUNC,E_TABLE,E_LIST,E_BIN,E_UN,E_AND,E_OR
} EKind;

typedef struct Expr Expr;
typedef struct Stat Stat;
typedef struct Block Block;
typedef struct FuncBody FuncBody;

typedef struct { Expr **e; int n,cap; } EList;
typedef struct { Expr **k; Expr **v; int n,cap; } FieldList;

struct Expr {
    EKind k; int line, op;
    double num; Str *str, *name;
    Expr *a,*b,*c;
    EList args;
    FieldList fields;
    FuncBody *fb;
};
struct Block { Stat **s; int n,cap; };
struct FuncBody { Str *name; Str **params; int nparams,isvararg,line; Block *body; int is_command; };

typedef enum {
    S_LOCAL,S_ASSIGN,S_CALL,S_DO,S_WHILE,S_REPEAT,S_IF,
    S_NUMFOR,S_GENFOR,S_RANGE,S_ITER,S_LOCALFUNC,S_RETURN,S_BREAK
} SKind;

struct Stat {
    SKind k; int line;
    int is_import;        /* S_ASSIGN generated by `import` (allowed to bind globals in !strict) */
    int is_libs_import;   /* S_DO that is exactly `import libs` (may precede `make`) */
    Str **names; int nnames;
    EList lhs,rhs;
    Block *body,*body2;
    Expr *e1,*e2,*e3;
    struct { Expr **cond; Block **blk; int n,cap; } clauses;
    Block *elseblk;
    FuncBody *fb;
};

/* AST lives until exit, compiled once */
static void *anew(size_t n){ return lcalloc(n); }
static Expr *new_expr(EKind k,int line){ Expr *e=(Expr*)anew(sizeof(Expr)); e->k=k; e->line=line; return e; }
static void el_add(EList *l,Expr *e){
    if(l->n==l->cap){ l->cap=l->cap?l->cap*2:4; l->e=(Expr**)lrealloc(l->e,sizeof(Expr*)*(size_t)l->cap); }
    l->e[l->n++]=e;
}
static void fl_add(FieldList *l,Expr *k,Expr *v){
    if(l->n==l->cap){ l->cap=l->cap?l->cap*2:4;
        l->k=(Expr**)lrealloc(l->k,sizeof(Expr*)*(size_t)l->cap);
        l->v=(Expr**)lrealloc(l->v,sizeof(Expr*)*(size_t)l->cap); }
    l->k[l->n]=k; l->v[l->n]=v; l->n++;
}
static void blk_add(Block *b,Stat *s){
    if(b->n==b->cap){ b->cap=b->cap?b->cap*2:8; b->s=(Stat**)lrealloc(b->s,sizeof(Stat*)*(size_t)b->cap); }
    b->s[b->n++]=s;
}

/* 8. PARSER  (tokens -> AST) */

typedef struct { Lexer lx; int fndepth; int inkey; int strict;
    Str *makename;      /* `make <name>`: this chunk is a lib part */
    int nstats;         /* statements parsed (kept for diagnostics) */
    int intop, topdone; /* inside the main chunk's top-level block */
    Str **getlist; int nget, getcap;   /* static `get` names (feeds `pack`) */
    int libs_on;        /* seen `import libs`: make/get/pack/command unlocked */
    int saw_nonlib;     /* seen anything but `import libs` (`make` placement) */
} Parser;

static Block *parse_block(Parser *ps);
static void getlist_add(Parser *ps,Str *n);
static Expr  *parse_expr(Parser *ps);

static void perr(Parser *ps,const char *fmt,...){
    char b[512],m[600]; va_list ap;
    va_start(ap,fmt); vsnprintf(b,sizeof b,fmt,ap); va_end(ap);
    snprintf(m,sizeof m,"%s:%d: %s",ps->lx.source->s,ps->lx.tline,b);
    luc_throw(mkobj(LT_STR,str_fromc(m)));
}
static const char *tok2str(int t,char *buf){
    if(t<256){ buf[0]=(char)t; buf[1]=0; return buf; }
    switch(t){
        case TK_EOF:return "<eof>"; case TK_NAME:return "<name>";
        case TK_NUMBER:return "<number>"; case TK_STRING:return "<string>";
        case TK_CONCAT:return ".."; case TK_DOTS:return "...";
        case TK_EQ:return "=="; case TK_NE:return "!=";
        case TK_LE:return "<="; case TK_GE:return ">=";
        case TK_ADDEQ:return "+="; case TK_SUBEQ:return "-=";
        case TK_MULEQ:return "*="; case TK_DIVEQ:return "/=";
        default: return kwnames[t-TK_AND];
    }
}
static void expect(Parser *ps,int t){
    char b1[8],b2[8];
    if(ps->lx.t!=t) perr(ps,"'%s' expected near '%s'",tok2str(t,b1),tok2str(ps->lx.t,b2));
    lx_next(&ps->lx);
}
static int opt(Parser *ps,int t){ if(ps->lx.t==t){ lx_next(&ps->lx); return 1; } return 0; }
static Str *expect_name(Parser *ps){
    char b[8];
    if(ps->lx.t!=TK_NAME) perr(ps,"<name> expected near '%s'",tok2str(ps->lx.t,b));
    Str *s=ps->lx.str; lx_next(&ps->lx); return s;
}

/* After '.'/':' allow keywords, so t.create works */
static Str *expect_kwname(Parser *ps){
    char b[8];
    if(ps->lx.t==TK_NAME){ Str *s=ps->lx.str; lx_next(&ps->lx); return s; }
    if(ps->lx.t>=TK_AND && ps->lx.t<TK_AND+NKW){
        Str *s=str_fromc(kwnames[ps->lx.t-TK_AND]);
        lx_next(&ps->lx); return s;
    }
    perr(ps,"<name> expected near '%s'",tok2str(ps->lx.t,b));
    return str_fromc("");
}

/* Type annotations: parsed, then discarded */
static void parse_type(Parser *ps){
    for(;;){
        if(ps->lx.t=='{'){                 /* {number}, {[string]:number} */
            int d=0;
            do{ if(ps->lx.t=='{')d++; else if(ps->lx.t=='}')d--; lx_next(&ps->lx); }while(d>0&&ps->lx.t!=TK_EOF);
        } else if(ps->lx.t=='('){          /* (number)->string */
            int d=0;
            do{ if(ps->lx.t=='(')d++; else if(ps->lx.t==')')d--; lx_next(&ps->lx); }while(d>0&&ps->lx.t!=TK_EOF);
        } else if(ps->lx.t==TK_NAME||ps->lx.t==TK_NIL||ps->lx.t==TK_FUNCTION){
            lx_next(&ps->lx);
            while(ps->lx.t=='.'){ lx_next(&ps->lx); expect_kwname(ps); }
        } else break;
        if(ps->lx.t=='?') lx_next(&ps->lx);
        if(ps->lx.t=='-'&&lx_peek(&ps->lx)=='>'){ lx_next(&ps->lx); lx_next(&ps->lx); continue; }
        if(ps->lx.t=='|'){ lx_next(&ps->lx); continue; }
        break;
    }
}
static void opt_type(Parser *ps){ if(ps->lx.t==':'){ lx_next(&ps->lx); parse_type(ps); } }

/* function body */
static FuncBody *parse_funcbody(Parser *ps,Str *name,int ismethod,int iscmd){
    FuncBody *fb=(FuncBody*)anew(sizeof(FuncBody));
    ps->fndepth++;
    fb->name=name; fb->line=ps->lx.tline; fb->is_command=iscmd;
    fb->params=(Str**)anew(sizeof(Str*)*64);
    if(ismethod) fb->params[fb->nparams++]=str_fromc("self");
    expect(ps,'(');
    if(ps->lx.t!=')'){
        do{
            if(ps->lx.t==TK_DOTS){ lx_next(&ps->lx); fb->isvararg=1; break; }
            if(fb->nparams>=60) perr(ps,"too many parameters");
            fb->params[fb->nparams++]=expect_name(ps);
            opt_type(ps);
        } while(opt(ps,','));
    }
    expect(ps,')');
    opt_type(ps);                       /* return type annotation */
    fb->body=parse_block(ps);
    expect(ps,TK_END);
    ps->fndepth--;
    return fb;
}

/* expressions */
static void parse_args(Parser *ps,Expr *call){
    if(ps->lx.t==TK_STRING){
        Expr *s=new_expr(E_STR,ps->lx.tline); s->str=ps->lx.str;
        lx_next(&ps->lx); el_add(&call->args,s); return;
    }
    if(ps->lx.t=='{'||ps->lx.t=='['){ el_add(&call->args,parse_expr(ps)); return; }
    expect(ps,'(');
    if(ps->lx.t!=')') do{ el_add(&call->args,parse_expr(ps)); }while(opt(ps,','));
    expect(ps,')');
}

static Expr *parse_primary(Parser *ps){
    int line=ps->lx.tline;
    if(ps->lx.t=='('){
        lx_next(&ps->lx);
        Expr *e=parse_expr(ps);
        expect(ps,')');
/* Parens truncate to one value */
        if(e->k==E_CALL||e->k==E_METHCALL||e->k==E_VARARG){
            Expr *p=new_expr(E_UN,line); p->op='('; p->a=e; return p;
        }
        return e;
    }
    if(ps->lx.t==TK_NAME){
        Expr *e=new_expr(E_NAME,line); e->name=ps->lx.str; lx_next(&ps->lx); return e;
    }
    char b[8];
    perr(ps,"unexpected symbol near '%s'",tok2str(ps->lx.t,b));
    return NULL;
}

static Expr *parse_postfix(Parser *ps,Expr *e){
    for(;;){
        int line=ps->lx.tline;
        switch(ps->lx.t){
            case '.': {
                lx_next(&ps->lx);
                Str *n=expect_kwname(ps);
                Expr *ix=new_expr(E_INDEX,line);
                ix->a=e; ix->b=new_expr(E_STR,line); ix->b->str=n;
                e=ix; break; }
            case '[': {
                lx_next(&ps->lx);
                if(ps->lx.t==':'){                  /* t[:n] */
                    lx_next(&ps->lx);
                    Expr *sl=new_expr(E_SLICE,line); sl->a=e; sl->b=NULL;
                    if(ps->lx.t!=']') sl->c=parse_expr(ps);
                    expect(ps,']'); e=sl; break;
                }
                Expr *k=parse_expr(ps);
                if(ps->lx.t==':'){                  /* t[a:b] */
                    lx_next(&ps->lx);
                    Expr *sl=new_expr(E_SLICE,line); sl->a=e; sl->b=k;
                    if(ps->lx.t!=']') sl->c=parse_expr(ps);
                    expect(ps,']'); e=sl; break;
                }
                expect(ps,']');
                Expr *ix=new_expr(E_INDEX,line); ix->a=e; ix->b=k; e=ix; break; }
            case ':':
                if(ps->inkey) return e;       /* dict separator */
                /* ':' + newline starts libs block-call; same-line ':' stays method call */
                { int cl=ps->lx.tline; (void)lx_peek(&ps->lx);
                  if(ps->lx.atline!=cl) return e; }
                lx_next(&ps->lx);             /* obj:method(args), self passed implicitly */
                {
                    Str *n=expect_kwname(ps);
                    Expr *c=new_expr(E_METHCALL,line);
                    c->a=e; c->name=n;
                    parse_args(ps,c);
                    e=c; break;
                }
            case '(': case TK_STRING: case '{': {
                Expr *c=new_expr(E_CALL,line); c->a=e;
                parse_args(ps,c); e=c; break; }
            default: return e;
        }
    }
}

static Expr *parse_suffixed(Parser *ps){
    return parse_postfix(ps,parse_primary(ps));
}

/* Dict: {key: value}; keys are exprs, {} is empty */
static Expr *parse_table(Parser *ps){
    int line=ps->lx.tline;
    Expr *e=new_expr(E_TABLE,line);
    expect(ps,'{');
    while(ps->lx.t!='}'){
        ps->inkey=1;                        /* ':' after dict key is a separator */
        Expr *k=parse_expr(ps);
        ps->inkey=0;
        if(ps->strict && k->k==E_NAME)
            perr(ps,"strict: dict keys must be quoted - use {\"key\": value}, got {%s: ...}",
                 k->name?k->name->s:"?");
        if(ps->lx.t!=':')
            perr(ps,"':' expected - LUC dicts use {key: value}, lists use [a, b, c]");
        lx_next(&ps->lx);
        fl_add(&e->fields,k,parse_expr(ps));
        if(!opt(ps,',') && !opt(ps,';')) break;
    }
    ps->inkey=0;
    expect(ps,'}');
    return e;
}
static Expr *parse_list(Parser *ps){
    int line=ps->lx.tline;
    Expr *e=new_expr(E_LIST,line);
    expect(ps,'[');
    while(ps->lx.t!=']'){
        fl_add(&e->fields,NULL,parse_expr(ps));
        if(!opt(ps,',')) break;
    }
    expect(ps,']');
    return e;
}

static Expr *parse_simple(Parser *ps){
    int line=ps->lx.tline;
    Expr *e;
    switch(ps->lx.t){
        case TK_NIL:    e=new_expr(E_NIL,line);   lx_next(&ps->lx); return e;
        case TK_TRUE:   e=new_expr(E_TRUE,line);  lx_next(&ps->lx); return e;
        case TK_FALSE:  e=new_expr(E_FALSE,line); lx_next(&ps->lx); return e;
        case TK_NUMBER: {
            e=new_expr(E_NUM,line); e->num=ps->lx.num; lx_next(&ps->lx);
            return parse_postfix(ps,e);
        }
        case TK_DOTS:   e=new_expr(E_VARARG,line); lx_next(&ps->lx); return e;
        case '{':       return parse_postfix(ps,parse_table(ps));
        case '[':       return parse_postfix(ps,parse_list(ps));
        case TK_FUNCTION: {
            lx_next(&ps->lx);
            e=new_expr(E_FUNC,line); e->fb=parse_funcbody(ps,NULL,0,0);
            return parse_postfix(ps,e); }
        case TK_STRING: {
            e=new_expr(E_STR,line); e->str=ps->lx.str; lx_next(&ps->lx);
            return parse_postfix(ps,e); }
        default: return parse_suffixed(ps);
    }
}

/* Operator priority (left, right) */
typedef struct { unsigned char left,right; } Prio;
static int getbinop(int t){
    switch(t){
        case '+': case '-': case '*': case '/': case '%': case '^':
        case TK_CONCAT: case TK_EQ: case TK_NE: case '<': case '>':
        case TK_LE: case TK_GE: case TK_AND: case TK_OR: case TK_IN: return t;
        default: return 0;
    }
}
static Prio binprio(int op){
    Prio p;
    switch(op){
        case TK_OR:  p.left=1;p.right=1; break;
        case TK_AND: p.left=2;p.right=2; break;
        case '<': case '>': case TK_LE: case TK_GE: case TK_NE: case TK_EQ:
        case TK_IN:  p.left=3;p.right=3; break;
        case TK_CONCAT: p.left=9;p.right=8; break;      /* right assoc */
        case '+': case '-': p.left=10;p.right=10; break;
        case '*': case '/': case '%': p.left=11;p.right=11; break;
        case '^': p.left=14;p.right=13; break;          /* right assoc */
        default: p.left=0;p.right=0;
    }
    return p;
}
#define UNARY_PRIO 12

static Expr *parse_subexpr(Parser *ps,int limit){
    Expr *e; int line=ps->lx.tline;
    if(ps->lx.t==TK_NOT||ps->lx.t=='-'||ps->lx.t=='#'){
        int op=ps->lx.t; lx_next(&ps->lx);
        Expr *sub=parse_subexpr(ps,UNARY_PRIO);
        if(op=='-'&&sub->k==E_NUM){ sub->num=-sub->num; e=sub; }
        else { e=new_expr(E_UN,line); e->op=op; e->a=sub; }
    } else e=parse_simple(ps);

    for(;;){
        int op=getbinop(ps->lx.t);
        int notin=0;
        if(!op && ps->lx.t==TK_NOT && lx_peek(&ps->lx)==TK_IN) notin=1;
        if(!op && !notin) break;
        Prio pr; pr.left=3; pr.right=3;
        if(!notin){ pr=binprio(op); if(pr.left<=limit) break; }
        int l2=ps->lx.tline;
        lx_next(&ps->lx);
        if(notin) lx_next(&ps->lx);
        Expr *rhs=parse_subexpr(ps,notin?3:pr.right);
        if(notin){
            Expr *in=new_expr(E_BIN,l2); in->op=TK_IN; in->a=e; in->b=rhs;
            e=new_expr(E_UN,l2); e->op=TK_NOT; e->a=in;
        } else {
            Expr *b=new_expr(op==TK_AND?E_AND:(op==TK_OR?E_OR:E_BIN),l2);
            b->op=op; b->a=e; b->b=rhs;
            e=b;
        }
    }
    return e;
}
static Expr *parse_expr(Parser *ps){ return parse_subexpr(ps,0); }

/* statements */
static Stat *new_stat(SKind k,int line){ Stat *s=(Stat*)anew(sizeof(Stat)); s->k=k; s->line=line; return s; }

static void clause_add(Stat *s,Expr *c,Block *b){
    if(s->clauses.n==s->clauses.cap){
        s->clauses.cap=s->clauses.cap?s->clauses.cap*2:4;
        s->clauses.cond=(Expr**)lrealloc(s->clauses.cond,sizeof(Expr*)*(size_t)s->clauses.cap);
        s->clauses.blk =(Block**)lrealloc(s->clauses.blk ,sizeof(Block*)*(size_t)s->clauses.cap);
    }
    s->clauses.cond[s->clauses.n]=c; s->clauses.blk[s->clauses.n]=b; s->clauses.n++;
}

static int block_follow(int t){
    return t==TK_EOF||t==TK_END||t==TK_ELSE||t==TK_ELSEIF;
}

/* Shared fn/command def; iscmd=1 bans direct return (see S_RETURN) */
static Stat *parse_named_func(Parser *ps,int line,int iscmd){
    if(iscmd && !ps->libs_on)
        perr(ps,"'command' needs 'import libs' first (commands live in the libs system)");
    lx_next(&ps->lx);
    Str *n=expect_name(ps);
    Expr *target=new_expr(E_NAME,line); target->name=n;
    int ismethod=0;
    Str *last=n;
    while(ps->lx.t=='.'){
        lx_next(&ps->lx);
        Str *f=expect_kwname(ps);
        Expr *ix=new_expr(E_INDEX,line);
        ix->a=target; ix->b=new_expr(E_STR,line); ix->b->str=f;
        target=ix; last=f;
    }
    if(ps->lx.t==':'){                  /* command obj.name(args): self is implicit */
        lx_next(&ps->lx);
        Str *f=expect_kwname(ps);
        Expr *ix=new_expr(E_INDEX,line);
        ix->a=target; ix->b=new_expr(E_STR,line); ix->b->str=f;
        target=ix; last=f;
        ismethod=1;
    }
    if(ps->strict && target->k==E_NAME){
        if(iscmd)
            perr(ps,"strict: declare commands with 'create command %s()' - bare 'command %s()' creates a global",
                 n?n->s:"?", n?n->s:"?");
        perr(ps,"strict: declare functions with 'create function %s()' - bare 'function %s()' creates a global",
             n?n->s:"?", n?n->s:"?");
    }
    Stat *s=new_stat(S_ASSIGN,line);
    el_add(&s->lhs,target);
    Expr *fe=new_expr(E_FUNC,line);
    fe->fb=parse_funcbody(ps,last,ismethod,iscmd);
    el_add(&s->rhs,fe);
    return s;
}

/* Tokens starting an expr (bare calls: `print x`) */
static int tok_starts_expr(int t){
    return t==TK_NAME||t==TK_NUMBER||t==TK_STRING||t==TK_NIL||t==TK_TRUE||t==TK_FALSE||
           t==TK_FUNCTION||t==TK_DOTS||t==TK_NOT||t=='('||t=='{'||t=='['||t=='-';
}

/* Bare call: `name args` on one line is name(args); was syntax error */
static Expr *maybe_bare_call(Parser *ps,Expr *e,int line){
    if(e->k==E_NAME && ps->lx.tline==line && tok_starts_expr(ps->lx.t)){
        Expr *c=new_expr(E_CALL,line); c->a=e;
        do{ el_add(&c->args,parse_expr(ps)); }while(opt(ps,','));
        if(opt(ps,';')){}
        else if(ps->lx.t!=TK_DO && ps->lx.t!=':' && ps->lx.tline==line && !block_follow(ps->lx.t) && ps->lx.t!=TK_EOF)
            perr(ps,"unexpected text after arguments (one statement per line)");
        return c;
    }
    return e;
}

/* Libs block-call: `name:` + body + `end` is `name() do body end`; needs `import libs` */
static Expr *maybe_colon_block(Parser *ps,Expr *e,int line){
    if(ps->lx.t!=':') return e;
    if(e->k!=E_NAME && e->k!=E_INDEX && e->k!=E_CALL && e->k!=E_METHCALL)
        perr(ps,"unexpected ':' (dict literals use {key: value}, methods use obj:method(args))");
    if(!ps->libs_on)
        perr(ps,"':' blocks need 'import libs' first (block-calls live in the libs system)");
    lx_next(&ps->lx);
    FuncBody *fb=(FuncBody*)anew(sizeof(FuncBody));
    ps->fndepth++;
    fb->name=NULL; fb->line=ps->lx.tline;
    fb->params=(Str**)anew(sizeof(Str*)*64);
    fb->body=parse_block(ps);
    expect(ps,TK_END);
    ps->fndepth--;
    Expr *fn=new_expr(E_FUNC,fb->line); fn->fb=fb;
    if(e->k==E_CALL || e->k==E_METHCALL){ el_add(&e->args,fn); return e; }
    Expr *c=new_expr(E_CALL,line); c->a=e;
    el_add(&c->args,fn);
    return c;
}

/* Trailing-do: f(x) do end is f(x, function() end); same line only */
static Expr *maybe_trailing_do(Parser *ps,Expr *e){
    if((e->k==E_CALL||e->k==E_METHCALL) && ps->lx.t==TK_DO && ps->lx.tline==e->line){
        int bl=ps->lx.tline;
        lx_next(&ps->lx);
        FuncBody *fb=(FuncBody*)anew(sizeof(FuncBody));
        ps->fndepth++;
        fb->name=NULL; fb->line=bl;
        fb->params=(Str**)anew(sizeof(Str*)*64);
        fb->body=parse_block(ps);
        expect(ps,TK_END);
        ps->fndepth--;
        Expr *fn=new_expr(E_FUNC,bl); fn->fb=fb;
        el_add(&e->args,fn);
    }
    return e;
}

/* Repeat alias: `repeat 1("i"),10 do` is `repeat 1,10 as i do`; number-call only, so safe */
static Str *repeat_alias(Parser *ps,Expr *e,Expr **base){
    *base=e;
    Expr *num=NULL;
    if(e->k==E_CALL && e->args.n==1 && e->args.e[0]->k==E_STR && e->a->k==E_NUM)
        num=e->a;                       /* 1("i"): postfix made a call */
    else if(e->k==E_NUM && ps->lx.t=='(')
        num=e;                          /* -1("i"): unary fold left '(' behind */
    else return NULL;
    Str *al=NULL;
    if(num==e){
        lx_next(&ps->lx);
        if(ps->lx.t!=TK_STRING)
            perr(ps,"repeat index must be a quoted name - use repeat 1(\"i\"), 10 do");
        al=ps->lx.str; lx_next(&ps->lx);
        expect(ps,')');
    } else al=e->args.e[0]->str;
    /* Alias is a loop var, so must be a plain name */
    { const char *s=al->s; int n=al->len, ok=n>0 && (isalpha((unsigned char)s[0])||s[0]=='_');
      for(int i=1;ok && i<n;i++) ok=isalnum((unsigned char)s[i])||s[i]=='_';
      if(!ok) perr(ps,"repeat index must be a plain name, got \"%s\"", al->s); }
    *base=num;
    return al;
}

static Stat *parse_statement(Parser *ps){
    int line=ps->lx.tline;
    switch(ps->lx.t){
        case ';': lx_next(&ps->lx); return NULL;

        case TK_IF: {
            Stat *s=new_stat(S_IF,line);
            lx_next(&ps->lx);
            Expr *c=parse_expr(ps); expect(ps,TK_THEN);
            clause_add(s,c,parse_block(ps));
            while(ps->lx.t==TK_ELSEIF){
                lx_next(&ps->lx);
                Expr *c2=parse_expr(ps); expect(ps,TK_THEN);
                clause_add(s,c2,parse_block(ps));
            }
            if(opt(ps,TK_ELSE)) s->elseblk=parse_block(ps);
            expect(ps,TK_END);
            return s; }

        case TK_WHILE: {
            Stat *s=new_stat(S_WHILE,line);
            lx_next(&ps->lx);
            s->e1=parse_expr(ps); expect(ps,TK_DO);
            s->body=parse_block(ps); expect(ps,TK_END);
            return s; }

        case TK_DO: {
            Stat *s=new_stat(S_DO,line);
            lx_next(&ps->lx);
            s->body=parse_block(ps); expect(ps,TK_END);
            return s; }

        case TK_FOR:
            perr(ps,"'for' is Lua syntax - LUC loops with 'repeat'");
            return NULL;

        case TK_UNTIL:
            perr(ps,"'until' is Lua do-while syntax - LUC removed it, use 'while'");
            return NULL;

        case TK_REPEAT: {
/* repeat [step,] dest [as i] do: step>0 counts up to dest, step<0 counts down to 0 */
            Stat *s=NULL;
            lx_next(&ps->lx);
            Expr *e1=parse_subexpr(ps,3);
            if(ps->lx.t==TK_IN){
                if(e1->k!=E_NAME) perr(ps,"loop variable must be a plain name");
                s=new_stat(S_ITER,line);
                s->names=(Str**)anew(sizeof(Str*)*8);
                s->names[s->nnames++]=e1->name;
                while(opt(ps,',')){
                    if(s->nnames>=2) perr(ps,"too many loop variables (max 2)");
                    Expr *n2=parse_subexpr(ps,3);
                    if(n2->k!=E_NAME) perr(ps,"loop variable must be a plain name");
                    s->names[s->nnames++]=n2->name;
                }
                expect(ps,TK_IN);
                s->e1=parse_expr(ps);
            } else if(ps->lx.t==','){
                lx_next(&ps->lx);
                Expr *e2=parse_subexpr(ps,3);
                if(ps->lx.t==TK_IN){
                    if(e1->k!=E_NAME||e2->k!=E_NAME) perr(ps,"loop variables must be plain names");
                    s=new_stat(S_ITER,line);
                    s->names=(Str**)anew(sizeof(Str*)*8);
                    s->names[s->nnames++]=e1->name;
                    s->names[s->nnames++]=e2->name;
                    expect(ps,TK_IN);
                    s->e1=parse_expr(ps);
                } else {
                    s=new_stat(S_RANGE,line);
                    Expr *b1; Str *al=repeat_alias(ps,e1,&b1);
                    s->e1=b1; s->e2=e2;
                    if(ps->lx.t==',') perr(ps,"repeat takes at most two numbers (step, destination) - no third");
                    if(opt(ps,TK_AS)){
                        if(al) perr(ps,"repeat index is already named \"%s\" - drop 'as ...'", al->s);
                        s->names=(Str**)anew(sizeof(Str*));
                        s->names[0]=expect_name(ps); s->nnames=1;
                    } else if(al){
                        s->names=(Str**)anew(sizeof(Str*));
                        s->names[0]=al; s->nnames=1;
                    }
                }
            } else {
                s=new_stat(S_RANGE,line);
                Expr *b1; Str *al=repeat_alias(ps,e1,&b1);
                s->e2=b1;
                Expr *one=new_expr(E_NUM,line); one->num=1;
                s->e1=one;
                if(al){
                    s->names=(Str**)anew(sizeof(Str*));
                    s->names[0]=al; s->nnames=1;
                }
            }
            expect(ps,TK_DO);
            s->body=parse_block(ps);
            expect(ps,TK_END);
            return s; }

        case TK_FUNCTION: {
            return parse_named_func(ps,line,0); }

        case TK_COMMAND: {
            return parse_named_func(ps,line,1); }

        case TK_RUN: {
            lx_next(&ps->lx);
            Expr *e=parse_suffixed(ps);
            e=maybe_bare_call(ps,e,line);
            e=maybe_colon_block(ps,e,line);
            if(e->k!=E_CALL&&e->k!=E_METHCALL)
                perr(ps,"'run' must be followed by a call - use run name(args) or run name args");
            e=maybe_trailing_do(ps,e);
            Stat *s=new_stat(S_CALL,line); s->e1=e;
            return s; }

        case TK_CREATE: {
            lx_next(&ps->lx);
            if(opt(ps,TK_FUNCTION)){
                Stat *s=new_stat(S_LOCALFUNC,line);
                Str *n=expect_name(ps);
                s->names=(Str**)anew(sizeof(Str*)); s->names[0]=n; s->nnames=1;
                s->fb=parse_funcbody(ps,n,0,0);
                return s;
            }
            if(opt(ps,TK_COMMAND)){
                if(!ps->libs_on)
                    perr(ps,"'command' needs 'import libs' first (commands live in the libs system)");
                Stat *s=new_stat(S_LOCALFUNC,line);
                Str *n=expect_name(ps);
                s->names=(Str**)anew(sizeof(Str*)); s->names[0]=n; s->nnames=1;
                s->fb=parse_funcbody(ps,n,0,1);
                return s;
            }
            Stat *s=new_stat(S_LOCAL,line);
            s->names=(Str**)anew(sizeof(Str*)*64);
            do{
                if(s->nnames>=60) perr(ps,"too many local variables");
                s->names[s->nnames++]=expect_name(ps);
                opt_type(ps);
            }while(opt(ps,','));
            if(opt(ps,'='))
                do{ el_add(&s->rhs,parse_expr(ps)); }while(opt(ps,','));
            return s; }

        case TK_MAKE: {
/* make <name>: lib part; needs `import libs` first, top-level creates auto-exported */
            if(ps->fndepth>0) perr(ps,"'make' must be a top-level statement");
            if(!ps->libs_on) perr(ps,"'make' needs 'import libs' first (lib parts live in the libs system)");
            if(ps->saw_nonlib) perr(ps,"'make' must come before any code (only 'import libs' may precede it)");
            lx_next(&ps->lx);
            if(ps->makename) perr(ps,"duplicate 'make' (already 'make %s')", ps->makename->s);
            ps->makename=expect_name(ps);
            return NULL; }

        case TK_GET: {
/* get a, b: load lib parts by MAKE name; recorded for later `pack` */
            if(!ps->libs_on) perr(ps,"'get' needs 'import libs' first");
            lx_next(&ps->lx);
            Stat *s=new_stat(S_LOCAL,line);
            s->names=(Str**)anew(sizeof(Str*)*64);
            do{
                if(s->nnames>=60) perr(ps,"too many names in 'get'");
                Str *n=expect_name(ps);
                s->names[s->nnames++]=n;
                if(ps->fndepth==0) getlist_add(ps,n);
                Expr *fn=new_expr(E_NAME,line); fn->name=str_fromc("__get");
                Expr *an=new_expr(E_STR,line); an->str=n;
                Expr *c=new_expr(E_CALL,line); c->a=fn; el_add(&c->args,an);
                el_add(&s->rhs,c);
            }while(opt(ps,','));
            return s; }

        case TK_PACK: {
/* pack <lib>: bundle `get` parts into <lib>.luic */
            if(!ps->libs_on) perr(ps,"'pack' needs 'import libs' first");
            lx_next(&ps->lx);
            Str *lib=expect_name(ps);
            if(ps->nget==0)
                perr(ps,"'pack %s' needs at least one 'get' first (get a, b then pack %s)",
                     lib->s, lib->s);
            Expr *fn=new_expr(E_NAME,line); fn->name=str_fromc("__pack");
            Expr *an=new_expr(E_STR,line); an->str=lib;
            Expr *lst=new_expr(E_LIST,line);
            for(int i=0;i<ps->nget;i++){
                Expr *e=new_expr(E_STR,line); e->str=ps->getlist[i];
                fl_add(&lst->fields,NULL,e);
            }
            Expr *c=new_expr(E_CALL,line); c->a=fn;
            el_add(&c->args,an); el_add(&c->args,lst);
            Stat *s=new_stat(S_CALL,line); s->e1=c;
            return s; }

        case TK_IMPORT: {
/* import a, b("x"): alias binds short name; `from` is contextual */
            lx_next(&ps->lx);
            Str *bnames[64]; Str *balias[64]; int nn=0, has_alias=0;
            do{
                if(nn>=64) perr(ps,"too many import names");
                Str *n=expect_name(ps);
                Str *alias=n;
                if(opt(ps,'(')){
                    if(ps->lx.t!=TK_STRING)
                        perr(ps,"expected an alias string after the module name - use import window(\"w\")");
                    alias=ps->lx.str;
                    lx_next(&ps->lx);
                    expect(ps,')');
                    has_alias=1;
                }
                bnames[nn]=n; balias[nn]=alias; nn++;
            }while(opt(ps,','));
            if(ps->lx.t==TK_NAME && ps->lx.tline==line &&
               ps->lx.str->len==4 && !memcmp(ps->lx.str->s,"from",4)){
                if(!ps->libs_on)
                    perr(ps,"'import ... from ...' needs 'import libs' first");
                if(has_alias)
                    perr(ps,"from-imports take no aliases - use import a, b from lib");
                lx_next(&ps->lx);
                char libbuf[512]; int lp=0;
                for(;;){
                    Str *seg=expect_name(ps);
                    int w=snprintf(libbuf+lp,sizeof libbuf-(size_t)lp,
                                   "%s%s",lp?".":"",seg->s);
                    if(w<0||w>=(int)(sizeof libbuf-(size_t)lp))
                        perr(ps,"lib name too long");
                    lp+=w;
                    if(!opt(ps,'.')) break;
                }
                Str *lib=str_fromc(libbuf);
                /* Single S_LOCAL: do-block locals would die at `end` */
                Stat *fs=new_stat(S_LOCAL,line);
                fs->names=(Str**)anew(sizeof(Str*)*64);
                for(int i=0;i<nn;i++){
                    if(fs->nnames>=60) perr(ps,"too many import names");
                    fs->names[fs->nnames++]=bnames[i];
                    Expr *fn=new_expr(E_NAME,line); fn->name=str_fromc("__import_from");
                    Expr *a1=new_expr(E_STR,line); a1->str=lib;
                    Expr *a2=new_expr(E_STR,line); a2->str=bnames[i];
                    Expr *call=new_expr(E_CALL,line); call->a=fn;
                    el_add(&call->args,a1); el_add(&call->args,a2);
                    el_add(&fs->rhs,call);
                }
                return fs;
            }
            Stat *s=new_stat(S_DO,line);
            s->body=(Block*)anew(sizeof(Block));
            for(int i=0;i<nn;i++){
                if(!strcmp(bnames[i]->s,"libs")) ps->libs_on=1;
                Expr *target=new_expr(E_NAME,line); target->name=balias[i];
                Expr *fn=new_expr(E_NAME,line); fn->name=str_fromc("__import");
                Expr *arg=new_expr(E_STR,line); arg->str=bnames[i];
                Expr *call=new_expr(E_CALL,line); call->a=fn; el_add(&call->args,arg);
                Stat *as=new_stat(S_ASSIGN,line);
                as->is_import=1;      /* !strict allows import bindings */
                el_add(&as->lhs,target); el_add(&as->rhs,call);
                blk_add(s->body,as);
            }
            if(nn==1 && !strcmp(bnames[0]->s,"libs")) s->is_libs_import=1;
            return s; }

        case TK_RETURN: {
            Stat *s=new_stat(S_RETURN,line);
            lx_next(&ps->lx);
            if(!block_follow(ps->lx.t) && ps->lx.t!=';')
                do{ el_add(&s->rhs,parse_expr(ps)); }while(opt(ps,','));
            opt(ps,';');
            return s; }

        case TK_BREAK: { lx_next(&ps->lx); opt(ps,';'); return new_stat(S_BREAK,line); }

        default: {
            Expr *e=parse_suffixed(ps);
            int cop=0;
            if(ps->lx.t==TK_ADDEQ) cop='+';
            else if(ps->lx.t==TK_SUBEQ) cop='-';
            else if(ps->lx.t==TK_MULEQ) cop='*';
            else if(ps->lx.t==TK_DIVEQ) cop='/';
            if(cop){
                if(e->k!=E_NAME && e->k!=E_INDEX)
                    perr(ps,"cannot assign to this expression");
                lx_next(&ps->lx);
                Stat *s=new_stat(S_ASSIGN,line);
                el_add(&s->lhs,e);
                Expr *bin=new_expr(E_BIN,line); bin->op=cop; bin->a=e; bin->b=parse_expr(ps);
                el_add(&s->rhs,bin);
                return s;
            }
            if(ps->lx.t=='='||ps->lx.t==','){
                Stat *s=new_stat(S_ASSIGN,line);
                el_add(&s->lhs,e);
                while(opt(ps,',')) el_add(&s->lhs,parse_suffixed(ps));
                expect(ps,'=');
                do{ el_add(&s->rhs,parse_expr(ps)); }while(opt(ps,','));
                for(int i=0;i<s->lhs.n;i++)
                    if(s->lhs.e[i]->k!=E_NAME && s->lhs.e[i]->k!=E_INDEX)
                        perr(ps,"cannot assign to this expression");
                return s;
            }
            e=maybe_bare_call(ps,e,line);
            e=maybe_colon_block(ps,e,line);
            if(e->k!=E_CALL&&e->k!=E_METHCALL) perr(ps,"syntax error near unexpected expression");
            e=maybe_trailing_do(ps,e);
            Stat *s=new_stat(S_CALL,line); s->e1=e;
            return s; }
    }
}

static void getlist_add(Parser *ps,Str *n){
    if(ps->nget==ps->getcap){ ps->getcap=ps->getcap?ps->getcap*2:4;
        ps->getlist=(Str**)lrealloc(ps->getlist,sizeof(Str*)*(size_t)ps->getcap); }
    ps->getlist[ps->nget++]=n;
}

static int is_reserved_libname(Str *n){
    const char *s=n?n->s:"";
    return !strcmp(s,"__exports")||!strcmp(s,"__get")||!strcmp(s,"__pack")||
           !strcmp(s,"__import_from")||!strcmp(s,"__import");
}

static Stat *export_assign(Str *exps,Str *name,int line){
    Expr *t=new_expr(E_NAME,line); t->name=exps;
    Expr *k=new_expr(E_STR,line); k->str=name;
    Expr *ix=new_expr(E_INDEX,line); ix->a=t; ix->b=k;
    Expr *v=new_expr(E_NAME,line); v->name=name;
    Stat *s=new_stat(S_ASSIGN,line);
    el_add(&s->lhs,ix); el_add(&s->rhs,v);
    return s;
}

/* `make` export: top-level creates copy into __exports; globals not exported */
static void maybe_export(Parser *ps,Block *b,Stat *s,int line){
    if(!ps->intop || !ps->makename || !s) return;
    Str *exps=str_fromc("__exports");
    if(s->k==S_LOCAL || s->k==S_LOCALFUNC){
        for(int i=0;i<s->nnames;i++){
            if(is_reserved_libname(s->names[i]))
                perr(ps,"'%s' is reserved for the libs system", s->names[i]->s);
            blk_add(b,export_assign(exps,s->names[i],line));
        }
    } else if(s->k==S_ASSIGN && !s->is_import && s->lhs.n==1 && s->rhs.n==1 &&
              s->lhs.e[0]->k==E_NAME && s->rhs.e[0]->k==E_FUNC){
        Str *n=s->lhs.e[0]->name;
        if(is_reserved_libname(n)) perr(ps,"'%s' is reserved for the libs system", n->s);
        blk_add(b,export_assign(exps,n,line));
    }
}

static Block *parse_block(Parser *ps){
    Block *b=(Block*)anew(sizeof(Block));
    int saved_top=ps->intop;
    ps->intop=(ps->fndepth==0 && !ps->topdone);
    if(ps->intop) ps->topdone=1;
    while(!block_follow(ps->lx.t)){
        int isret = (ps->lx.t==TK_RETURN);
        Stat *s=parse_statement(ps);
        if(s){ blk_add(b,s); maybe_export(ps,b,s,s->line);
            if(!(s->k==S_DO && s->is_libs_import)) ps->saw_nonlib=1; }
        ps->nstats++;
        if(isret) break;
    }
    ps->intop=saved_top;
    return b;
}

/* 9. BYTECODE (opcodes in luc.h) */

/* 10. COMPILER (AST -> bytecode) */

typedef struct { Str *name; } LocalVar;
typedef struct BlockCnt {
    struct BlockCnt *prev;
    int firstlocal, isloop;
    int breaks[80], nbreaks;
} BlockCnt;

typedef struct FuncState {
    Proto *p;
    struct FuncState *prev;
    LocalVar locals[LUC_MAXREG];
    int nlocals, freereg;
    Str *upnames[LUC_MAXUPVAL];
    BlockCnt *bl;
    Str *source;
    int line;
    int is_command;   /* inside `command` body: direct `return` is forbidden */
} FuncState;

static void cerror(FuncState *fs,int line,const char *fmt,...){
    char b[400],m[500]; va_list ap;
    va_start(ap,fmt); vsnprintf(b,sizeof b,fmt,ap); va_end(ap);
    snprintf(m,sizeof m,"%s:%d: %s",fs->source->s,line,b);
    luc_throw(mkobj(LT_STR,str_fromc(m)));
}

static int emit(FuncState *fs,uint32_t ins,int line){
    Proto *p=fs->p;
    if(p->ncode==p->ccap){
        p->ccap=p->ccap?p->ccap*2:32;
        p->code =(uint32_t*)lrealloc(p->code ,sizeof(uint32_t)*(size_t)p->ccap);
        p->lines=(int*)lrealloc(p->lines,sizeof(int)*(size_t)p->ccap);
    }
    p->lines[p->ncode]=line;
    p->code[p->ncode]=ins;
    return p->ncode++;
}

/* Patch jump at `pc` to continue at `target` */
static void patch(FuncState *fs,int pc,int target){
    uint32_t ins=fs->p->code[pc];
    int op=GET_OP(ins), a=GET_A(ins);
    fs->p->code[pc]=I_AsBx(op,a,target-(pc+1));
}
static int here(FuncState *fs){ return fs->p->ncode; }

static int addk(FuncState *fs,Value v){
    Proto *p=fs->p;
    for(int i=0;i<p->nk;i++) if(p->k[i].t==v.t && val_rawequal(p->k[i],v)) return i;
    if(p->nk==p->kcap){
        p->kcap=p->kcap?p->kcap*2:8;
        p->k=(Value*)lrealloc(p->k,sizeof(Value)*(size_t)p->kcap);
    }
    p->k[p->nk]=v;
    if(p->nk>=65000) cerror(fs,fs->line,"too many constants");
    return p->nk++;
}
static int addkstr(FuncState *fs,Str *s){ return addk(fs,mkobj(LT_STR,s)); }

static void checkreg(FuncState *fs,int n){
    if(n>=LUC_MAXREG) cerror(fs,fs->line,"function or expression too complex");
    if(n>fs->p->maxstack) fs->p->maxstack=n;
}
static int reserve(FuncState *fs,int n){
    int r=fs->freereg; fs->freereg+=n; checkreg(fs,fs->freereg); return r;
}
static int newlocal(FuncState *fs,Str *name){
    if(fs->nlocals>=LUC_MAXREG-4) cerror(fs,fs->line,"too many local variables");
    fs->locals[fs->nlocals].name=name;
    checkreg(fs,fs->nlocals+1);
    return fs->nlocals++;
}
static int findlocal(FuncState *fs,Str *n){
    for(int i=fs->nlocals-1;i>=0;i--) if(fs->locals[i].name==n) return i;
    return -1;
}
static int findupval(FuncState *fs,Str *n){
    Proto *p=fs->p;
    for(int i=0;i<p->nup;i++) if(fs->upnames[i]==n) return i;
    if(!fs->prev) return -1;
    int r=findlocal(fs->prev,n);
    if(r>=0){
        if(p->nup>=LUC_MAXUPVAL) cerror(fs,fs->line,"too many upvalues");
        p->upvals[p->nup].instack=1; p->upvals[p->nup].idx=(unsigned char)r;
        fs->upnames[p->nup]=n; return p->nup++;
    }
    int u=findupval(fs->prev,n);
    if(u<0) return -1;
    if(p->nup>=LUC_MAXUPVAL) cerror(fs,fs->line,"too many upvalues");
    p->upvals[p->nup].instack=0; p->upvals[p->nup].idx=(unsigned char)u;
    fs->upnames[p->nup]=n; return p->nup++;
}

static void enterblock(FuncState *fs,BlockCnt *bl,int isloop){
    bl->prev=fs->bl; bl->firstlocal=fs->nlocals; bl->isloop=isloop; bl->nbreaks=0;
    fs->bl=bl;
}
static void leaveblock(FuncState *fs,int line){
    BlockCnt *bl=fs->bl;
    if(bl->firstlocal<fs->nlocals) emit(fs,I_ABC(OP_CLOSE,bl->firstlocal,0,0),line);
    fs->nlocals=bl->firstlocal; fs->freereg=fs->nlocals; fs->bl=bl->prev;
}
static void patch_breaks(FuncState *fs,BlockCnt *bl,int target){
    for(int i=0;i<bl->nbreaks;i++) patch(fs,bl->breaks[i],target);
}

/* forward decls */
static void exprd(FuncState *fs,Expr *e,int reg);
static int  comp_call(FuncState *fs,Expr *e,int nres);
static void comp_block(FuncState *fs,Block *b);
static Proto *compile_proto(FuncState *parent,FuncBody *fb,Str *source,int karatsuba,int strict,int mkmod);

static int multiret(Expr *e){ return e->k==E_CALL||e->k==E_METHCALL||e->k==E_VARARG; }

/* Compile e to `nres` results (nres<0 = all) */
static int comp_multi(FuncState *fs,Expr *e,int nres){
    if(e->k==E_CALL||e->k==E_METHCALL) return comp_call(fs,e,nres);
/* vararg */
    int r=reserve(fs,1);
    emit(fs,I_ABC(OP_VARARG,r,nres<0?0:nres+1,0),e->line);
    if(nres>1) reserve(fs,nres-1);
    return r;
}

/* Compile expr to temp reg */
static int exprtmp(FuncState *fs,Expr *e){
    if(e->k==E_NAME){
        int r=findlocal(fs,e->name);
        if(r>=0) return r;
    }
    int r=reserve(fs,1);
    exprd(fs,e,r);
    return r;
}

/* Fused x.append: dot-form only; colon-form keeps explicit self */
static int is_append_call(Expr *e,Expr **pself){
    if(e->k==E_CALL && e->a && e->a->k==E_INDEX && e->a->b && e->a->b->k==E_STR &&
       e->a->b->str && e->a->b->str->len==6 &&
       memcmp(e->a->b->str->s,"append",6)==0){ *pself=e->a->a; return 1; }
    return 0;
}

static int comp_call(FuncState *fs,Expr *e,int nres){
    Expr *aself=NULL;
    if(is_append_call(e,&aself)){
        /* APPEND layout: self at func, args after; B=count, C=nres as CALL */
        int func=fs->freereg;
        reserve(fs,1);
        exprd(fs,aself,func);
        int nargs=0, multi=0;
        for(int i=0;i<e->args.n;i++){
            Expr *a=e->args.e[i];
            if(i==e->args.n-1 && multiret(a)){ comp_multi(fs,a,-1); multi=1; }
            else { int r=reserve(fs,1); exprd(fs,a,r); nargs++; }
        }
        emit(fs,I_ABC(OP_APPEND,func,multi?0:nargs,nres<0?0:nres+1),e->line);
        fs->freereg = func + (nres<0?1:(nres>0?nres:0));
        checkreg(fs,fs->freereg+1);
        return func;
    }
    int func=fs->freereg;
    int nargs=0;
    if(e->k==E_METHCALL){
        reserve(fs,2);
        exprd(fs,e->a,func+1);                       /* self object */
        int t=reserve(fs,1);
        emit(fs,I_ABx(OP_LOADK,t,addkstr(fs,e->name)),e->line);
        emit(fs,I_ABC(OP_GETTABLE,func,func+1,t),e->line);
        fs->freereg=func+2;
        nargs=1;
    } else {
        reserve(fs,1);
        exprd(fs,e->a,func);
    }
    int multi=0;
    for(int i=0;i<e->args.n;i++){
        Expr *a=e->args.e[i];
        if(i==e->args.n-1 && multiret(a)){ comp_multi(fs,a,-1); multi=1; }
        else { int r=reserve(fs,1); exprd(fs,a,r); nargs++; }
    }
    emit(fs,I_ABC(OP_CALL,func,multi?0:nargs+1,nres<0?0:nres+1),e->line);
    fs->freereg = func + (nres<0?1:(nres>0?nres:0));
    checkreg(fs,fs->freereg+1);
    return func;
}

/* table / list constructor */
static void comp_ctor(FuncState *fs,Expr *e,int reg){
    int islist=(e->k==E_LIST);
    int save=fs->freereg;
    int tmp=reserve(fs,1);
    emit(fs,I_ABC(OP_NEWTABLE,tmp,islist,0),e->line);
    int pending=0, startidx=1;
    for(int i=0;i<e->fields.n;i++){
        Expr *k=e->fields.k[i], *v=e->fields.v[i];
        if(k){
            if(pending){ emit(fs,I_ABC(OP_SETLIST,tmp,pending,startidx),e->line);
                         startidx+=pending; pending=0; fs->freereg=tmp+1; }
            int rb=reserve(fs,1); exprd(fs,k,rb);
            int rc=reserve(fs,1); exprd(fs,v,rc);
            emit(fs,I_ABC(OP_SETTABLE,tmp,rb,rc),e->line);
            fs->freereg=tmp+1;
        } else if(i==e->fields.n-1 && multiret(v)){
            if(pending){ emit(fs,I_ABC(OP_SETLIST,tmp,pending,startidx),e->line);
                         startidx+=pending; pending=0; fs->freereg=tmp+1; }
            comp_multi(fs,v,-1);
            emit(fs,I_ABC(OP_SETLIST,tmp,0,startidx),e->line);
            fs->freereg=tmp+1;
        } else if(startidx+pending>240){
/* Long literal: use explicit index stores */
            if(pending){ emit(fs,I_ABC(OP_SETLIST,tmp,pending,startidx),e->line);
                         startidx+=pending; pending=0; fs->freereg=tmp+1; }
            int rb=reserve(fs,1);
            emit(fs,I_ABx(OP_LOADK,rb,addk(fs,mknum((double)startidx))),e->line);
            int rc=reserve(fs,1); exprd(fs,v,rc);
            emit(fs,I_ABC(OP_SETTABLE,tmp,rb,rc),e->line);
            fs->freereg=tmp+1; startidx++;
        } else {
            int r=reserve(fs,1); exprd(fs,v,r); pending++;
            if(pending>=40){ emit(fs,I_ABC(OP_SETLIST,tmp,pending,startidx),e->line);
                             startidx+=pending; pending=0; fs->freereg=tmp+1; }
        }
    }
    if(pending) emit(fs,I_ABC(OP_SETLIST,tmp,pending,startidx),e->line);
    if(tmp!=reg) emit(fs,I_ABC(OP_MOVE,reg,tmp,0),e->line);
    fs->freereg=save;
}

static int binop2op(int op){
    switch(op){
        case '+':return OP_ADD; case '-':return OP_SUB; case '*':return OP_MUL;
        case '/':return OP_DIV; case '%':return OP_MOD; case '^':return OP_POW;
        case TK_CONCAT:return OP_CONCAT;
        case TK_EQ:return OP_EQ;  case TK_NE:return OP_NE;
        case '<':return OP_LT;    case TK_LE:return OP_LE;
        case '>':return OP_GT;    case TK_GE:return OP_GE;
        case TK_IN:return OP_IN;
        default: return OP_ADD;
    }
}

static void exprd(FuncState *fs,Expr *e,int reg){
    fs->line=e->line;
    switch(e->k){
        case E_NIL:   emit(fs,I_ABC(OP_LOADNIL,reg,0,0),e->line); break;
        case E_TRUE:  emit(fs,I_ABC(OP_LOADBOOL,reg,1,0),e->line); break;
        case E_FALSE: emit(fs,I_ABC(OP_LOADBOOL,reg,0,0),e->line); break;
        case E_NUM:   emit(fs,I_ABx(OP_LOADK,reg,addk(fs,mknum(e->num))),e->line); break;
        case E_STR:   emit(fs,I_ABx(OP_LOADK,reg,addkstr(fs,e->str)),e->line); break;
        case E_VARARG:emit(fs,I_ABC(OP_VARARG,reg,2,0),e->line); break;
        case E_NAME: {
            int r=findlocal(fs,e->name);
            if(r>=0){ if(r!=reg) emit(fs,I_ABC(OP_MOVE,reg,r,0),e->line); break; }
            int u=findupval(fs,e->name);
            if(u>=0){ emit(fs,I_ABC(OP_GETUPVAL,reg,u,0),e->line); break; }
            emit(fs,I_ABx(OP_GETGLOBAL,reg,addkstr(fs,e->name)),e->line);
            break; }
        case E_INDEX: {
            int save=fs->freereg;
            int rb=exprtmp(fs,e->a), rc=exprtmp(fs,e->b);
            emit(fs,I_ABC(OP_GETTABLE,reg,rb,rc),e->line);
            fs->freereg=save; break; }
        case E_CALL: case E_METHCALL: {
            int save=fs->freereg;
            int f=comp_call(fs,e,1);
            if(f!=reg) emit(fs,I_ABC(OP_MOVE,reg,f,0),e->line);
            fs->freereg=save; break; }
        case E_TABLE: case E_LIST: comp_ctor(fs,e,reg); break;
        case E_SLICE: {
            int save=fs->freereg;
            int rt=exprtmp(fs,e->a);
            int rs=reserve(fs,2);
            if(e->b) exprd(fs,e->b,rs); else emit(fs,I_ABC(OP_LOADNIL,rs,0,0),e->line);
            if(e->c) exprd(fs,e->c,rs+1); else emit(fs,I_ABC(OP_LOADNIL,rs+1,0,0),e->line);
            emit(fs,I_ABC(OP_SLICE,reg,rt,rs),e->line);
            fs->freereg=save; break; }
        case E_FUNC: {
            Proto *np=compile_proto(fs,e->fb,fs->source,0,0,0);
            Proto *p=fs->p;
            if(p->np==p->pcap){ p->pcap=p->pcap?p->pcap*2:4;
                p->p=(Proto**)lrealloc(p->p,sizeof(Proto*)*(size_t)p->pcap); }
            p->p[p->np]=np;
            emit(fs,I_ABx(OP_CLOSURE,reg,p->np),e->line);
            p->np++;
            break; }
        case E_AND: {
            exprd(fs,e->a,reg);
            int j=emit(fs,I_AsBx(OP_JMPIFNOT,reg,0),e->line);
            exprd(fs,e->b,reg);
            patch(fs,j,here(fs));
            break; }
        case E_OR: {
            exprd(fs,e->a,reg);
            int j=emit(fs,I_AsBx(OP_JMPIF,reg,0),e->line);
            exprd(fs,e->b,reg);
            patch(fs,j,here(fs));
            break; }
        case E_UN: {
            if(e->op=='('){ exprd(fs,e->a,reg); break; }
            int save=fs->freereg;
            int rb=exprtmp(fs,e->a);
            int op = e->op=='-'?OP_UNM : (e->op=='#'?OP_LEN:OP_NOT);
            emit(fs,I_ABC(op,reg,rb,0),e->line);
            fs->freereg=save; break; }
        case E_BIN: {
            int save=fs->freereg;
            int rb=exprtmp(fs,e->a), rc=exprtmp(fs,e->b);
            emit(fs,I_ABC(binop2op(e->op),reg,rb,rc),e->line);
            fs->freereg=save; break; }
    }
}

/* Compile expr list into nvars regs at `base` */
static void adjust_assign(FuncState *fs,int nvars,EList *rhs,int base){
    int n=rhs->n;
    for(int i=0;i<n;i++){
        Expr *e=rhs->e[i];
        if(i==n-1 && i<nvars && multiret(e)){
            int want=nvars-i;
            fs->freereg=base+i;
            comp_multi(fs,e,want);
            fs->freereg=base+nvars;
            return;
        }
        if(i<nvars){ fs->freereg=base+i; reserve(fs,1); exprd(fs,e,base+i); }
        else { int t=reserve(fs,1); exprd(fs,e,t); fs->freereg=base+nvars>t?base+nvars:t; }
    }
    for(int i=n;i<nvars;i++){ fs->freereg=base+i; reserve(fs,1);
        emit(fs,I_ABC(OP_LOADNIL,base+i,0,0),fs->line); }
    fs->freereg=base+nvars;
    checkreg(fs,fs->freereg);
}

/* Pure index exprs: safe to eval once when fusing X[k] <op>= e */
static int pure_index_expr(Expr *e){
    switch(e->k){
        case E_NAME: case E_NUM: case E_STR:
        case E_NIL: case E_TRUE: case E_FALSE: return 1;
        case E_INDEX: return pure_index_expr(e->a)&&pure_index_expr(e->b);
        default: return 0;
    }
}
static int same_index_expr(Expr *a,Expr *b){
    if(!a||!b||a->k!=b->k) return 0;
    switch(a->k){
        case E_NAME: return a->name==b->name;      /* interned */
        case E_NUM: return a->num==b->num;
        case E_STR: return a->str==b->str;
        case E_NIL: case E_TRUE: case E_FALSE: return 1;
        case E_INDEX: return same_index_expr(a->a,b->a)&&same_index_expr(a->b,b->b);
        default: return 0;
    }
}
/* Fuse X[k] <op>= e with pure identical X,k; 1=fused, 0=generic */
static int try_compound(FuncState *fs,Stat *s){
    if(s->lhs.n!=1||s->rhs.n!=1) return 0;
    Expr *lhs=s->lhs.e[0], *rhs=s->rhs.e[0];
    if(!lhs||lhs->k!=E_INDEX||!rhs||rhs->k!=E_BIN) return 0;
    int op=0;
    if(rhs->op=='+') op=OP_ADDEQ; else if(rhs->op=='-') op=OP_SUBEQ;
    else if(rhs->op=='*') op=OP_MULEQ; else if(rhs->op=='/') op=OP_DIVEQ;
    else return 0;
    if(!rhs->a||rhs->a->k!=E_INDEX||!rhs->b) return 0;
    if(!same_index_expr(lhs,rhs->a)) return 0;
    if(!pure_index_expr(lhs->a)||!pure_index_expr(lhs->b)) return 0;
    int save=fs->freereg;
    int rt=exprtmp(fs,lhs->a), rk=exprtmp(fs,lhs->b);
    int rv=reserve(fs,1); exprd(fs,rhs->b,rv);
    emit(fs,I_ABC(op,rt,rk,rv),s->line);
    fs->freereg=save;
    return 1;
}

static void store_to(FuncState *fs,Expr *lhs,int valreg,int line){
    if(lhs->k==E_NAME){
        int r=findlocal(fs,lhs->name);
        if(r>=0){ if(r!=valreg) emit(fs,I_ABC(OP_MOVE,r,valreg,0),line); return; }
        int u=findupval(fs,lhs->name);
        if(u>=0){ emit(fs,I_ABC(OP_SETUPVAL,valreg,u,0),line); return; }
        emit(fs,I_ABx(OP_SETGLOBAL,valreg,addkstr(fs,lhs->name)),line);
        return;
    }
    int save=fs->freereg;
    int rt=exprtmp(fs,lhs->a), rk=exprtmp(fs,lhs->b);
    emit(fs,I_ABC(OP_SETTABLE,rt,rk,valreg),line);
    fs->freereg=save;
}

/* !strict decl check: true for locals or captured upvals; globals not declared */
static int strict_declared(FuncState *fs,Str *n){
    for(FuncState *f=fs; f; f=f->prev){
        for(int i=f->nlocals-1;i>=0;i--) if(f->locals[i].name==n) return 1;
        for(int i=0;i<f->p->nup;i++) if(f->upnames[i]==n) return 1;
    }
    return 0;
}

static void comp_stat(FuncState *fs,Stat *s){
    fs->line=s->line;
    switch(s->k){
        case S_LOCAL: {
            int base=fs->nlocals;
            fs->freereg=base;
            adjust_assign(fs,s->nnames,&s->rhs,base);
            for(int i=0;i<s->nnames;i++) newlocal(fs,s->names[i]);
            fs->freereg=fs->nlocals;
            break; }
        case S_LOCALFUNC: {
            int r=newlocal(fs,s->names[0]);
            fs->freereg=fs->nlocals;
            Expr fe; memset(&fe,0,sizeof fe);
            fe.k=E_FUNC; fe.line=s->line; fe.fb=s->fb;
            exprd(fs,&fe,r);
            fs->freereg=fs->nlocals;
            break; }
        case S_ASSIGN: {
            if(fs->p->uses_strict && !s->is_import){
                /* !strict: `name=` needs `create` local; t.k=v stays allowed */
                for(int i=0;i<s->lhs.n;i++){
                    Expr *lhs=s->lhs.e[i];
                    if(lhs->k==E_NAME && !strict_declared(fs,lhs->name))
                        cerror(fs,s->line,"strict: assignment to undeclared variable '%s' - declare with 'create %s = ...'",
                               lhs->name?lhs->name->s:"?", lhs->name?lhs->name->s:"?");
                }
            }
            if(try_compound(fs,s)){ fs->freereg=fs->nlocals; break; }
            int base=fs->freereg;
            adjust_assign(fs,s->lhs.n,&s->rhs,base);
            for(int i=s->lhs.n-1;i>=0;i--) store_to(fs,s->lhs.e[i],base+i,s->line);
            fs->freereg=fs->nlocals;
            break; }
        case S_CALL: {
            int save=fs->freereg;
            comp_call(fs,s->e1,0);
            fs->freereg=save;
            break; }
        case S_DO: {
            BlockCnt bl; enterblock(fs,&bl,0);
            comp_block(fs,s->body);
            leaveblock(fs,s->line);
            break; }
        case S_IF: {
            int endjmps[64],ne=0;
            for(int i=0;i<s->clauses.n;i++){
                int save=fs->freereg;
                int r=reserve(fs,1);
                exprd(fs,s->clauses.cond[i],r);
                fs->freereg=save;
                int jf=emit(fs,I_AsBx(OP_JMPIFNOT,r,0),s->line);
                BlockCnt bl; enterblock(fs,&bl,0);
                comp_block(fs,s->clauses.blk[i]);
                leaveblock(fs,s->line);
                if(i<s->clauses.n-1 || s->elseblk){
                    if(ne<64) endjmps[ne++]=emit(fs,I_AsBx(OP_JMP,0,0),s->line);
                }
                patch(fs,jf,here(fs));
            }
            if(s->elseblk){
                BlockCnt bl; enterblock(fs,&bl,0);
                comp_block(fs,s->elseblk);
                leaveblock(fs,s->line);
            }
            for(int i=0;i<ne;i++) patch(fs,endjmps[i],here(fs));
            break; }
        case S_WHILE: {
            int start=here(fs);
            int save=fs->freereg;
            int r=reserve(fs,1);
            exprd(fs,s->e1,r);
            fs->freereg=save;
            int jf=emit(fs,I_AsBx(OP_JMPIFNOT,r,0),s->line);
            BlockCnt bl; enterblock(fs,&bl,1);
            comp_block(fs,s->body);
            leaveblock(fs,s->line);
            patch(fs,emit(fs,I_AsBx(OP_JMP,0,0),s->line),start);
            patch(fs,jf,here(fs));
            patch_breaks(fs,&bl,here(fs));
            break; }
        case S_REPEAT: {
            int start=here(fs);
            BlockCnt bl; enterblock(fs,&bl,1);
/* Cond sees body locals, eval before leaveblock */
            comp_block(fs,s->body);
            int save=fs->freereg;
            int r=reserve(fs,1);
            exprd(fs,s->e1,r);
            fs->freereg=save;
            int jf=emit(fs,I_AsBx(OP_JMPIFNOT,r,0),s->line);
            leaveblock(fs,s->line);
            patch(fs,jf,start);
/* Fallthrough when true */
            patch_breaks(fs,&bl,here(fs));
            break; }
        case S_NUMFOR: {
            int base=fs->nlocals;
            fs->freereg=base;
            int r0=reserve(fs,1); exprd(fs,s->e1,r0);
            int r1=reserve(fs,1); exprd(fs,s->e2,r1);
            int r2=reserve(fs,1);
            if(s->e3) exprd(fs,s->e3,r2);
            else emit(fs,I_ABx(OP_LOADK,r2,addk(fs,mknum(1))),s->line);
            BlockCnt bl; enterblock(fs,&bl,1);
            newlocal(fs,str_fromc("(for state)"));
            newlocal(fs,str_fromc("(for limit)"));
            newlocal(fs,str_fromc("(for step)"));
            newlocal(fs,s->names[0]);
            fs->freereg=fs->nlocals;
            int prep=emit(fs,I_AsBx(OP_FORPREP,base,0),s->line);
            int body=here(fs);
            comp_block(fs,s->body);
            leaveblock(fs,s->line);
            int loop=emit(fs,I_AsBx(OP_FORLOOP,base,0),s->line);
            patch(fs,loop,body);
            patch(fs,prep,loop);
            patch_breaks(fs,&bl,here(fs));
            break; }
        case S_GENFOR: {
            int base=fs->nlocals;
            fs->freereg=base;
            adjust_assign(fs,3,&s->rhs,base);
            BlockCnt bl; enterblock(fs,&bl,1);
            newlocal(fs,str_fromc("(for gen)"));
            newlocal(fs,str_fromc("(for state)"));
            newlocal(fs,str_fromc("(for ctrl)"));
            for(int i=0;i<s->nnames;i++) newlocal(fs,s->names[i]);
            fs->freereg=fs->nlocals;
            checkreg(fs,fs->nlocals+3);
            int prep=emit(fs,I_AsBx(OP_JMP,0,0),s->line);
            int body=here(fs);
            comp_block(fs,s->body);
            leaveblock(fs,s->line);
            int tfor=emit(fs,I_ABC(OP_TFORLOOP,base,0,s->nnames),s->line);
            patch(fs,emit(fs,I_AsBx(OP_JMP,0,0),s->line),body);
            patch(fs,prep,tfor);
            patch_breaks(fs,&bl,here(fs));
            break; }
        case S_RANGE: {
/* repeat [step,] dest [as i]: FORPREP checks step, picks dir */
            int base=fs->nlocals;
            fs->freereg=base;
            int r0=reserve(fs,1); exprd(fs,s->e1,r0);
            int r1=reserve(fs,1); exprd(fs,s->e2,r1);
            BlockCnt bl; enterblock(fs,&bl,1);
            newlocal(fs,str_fromc("(for state)"));
            newlocal(fs,str_fromc("(for limit)"));
            newlocal(fs,str_fromc("(for step)"));
            newlocal(fs,s->nnames?s->names[0]:str_fromc("(i)"));
            fs->freereg=fs->nlocals;
            int prep=emit(fs,I_AsBx(OP_FORPREP,base,0),s->line);
            int body=here(fs);
            comp_block(fs,s->body);
            leaveblock(fs,s->line);
            int loop=emit(fs,I_AsBx(OP_FORLOOP,base,0),s->line);
            patch(fs,loop,body);
            patch(fs,prep,loop);
            patch_breaks(fs,&bl,here(fs));
            break; }
        case S_ITER: {
/* repeat a[,b] in expr: value/index or key/value */
            int base=fs->nlocals;
            fs->freereg=base;
            int r0=reserve(fs,1); exprd(fs,s->e1,r0);
            int r1=reserve(fs,1); emit(fs,I_ABC(OP_LOADNIL,r1,0,0),s->line);
            reserve(fs,2);                      /* out1/out2 slots */
            BlockCnt bl; enterblock(fs,&bl,1);
            newlocal(fs,str_fromc("(iter target)"));
            newlocal(fs,str_fromc("(iter state)"));
            newlocal(fs,s->names[0]);
            if(s->nnames>1) newlocal(fs,s->names[1]);
            fs->freereg=fs->nlocals;
            int prep=emit(fs,I_AsBx(OP_JMP,0,0),s->line);
            int body=here(fs);
            comp_block(fs,s->body);
            leaveblock(fs,s->line);
            int nxt=emit(fs,I_ABC(OP_NEXT,base,0,0),s->line);
            patch(fs,emit(fs,I_AsBx(OP_JMP,0,0),s->line),body);
            patch(fs,prep,nxt);
            patch_breaks(fs,&bl,here(fs));
            break; }
        case S_RETURN: {
            if(fs->is_command)
                cerror(fs,s->line,"'return' is not allowed inside 'command' (commands run, they don't return values - use 'function' for values)");
            int base=fs->freereg;
            int n=s->rhs.n, multi=0;
            for(int i=0;i<n;i++){
                Expr *e=s->rhs.e[i];
                if(i==n-1 && multiret(e)){ fs->freereg=base+i; comp_multi(fs,e,-1); multi=1; }
                else { fs->freereg=base+i; reserve(fs,1); exprd(fs,e,base+i); }
            }
            emit(fs,I_ABC(OP_RETURN,base,multi?0:n+1,0),s->line);
            fs->freereg=fs->nlocals;
            break; }
        case S_BREAK: {
            BlockCnt *b=fs->bl;
            while(b && !b->isloop) b=b->prev;
            if(!b) cerror(fs,s->line,"'break' outside a loop");
            emit(fs,I_ABC(OP_CLOSE,b->firstlocal,0,0),s->line);
            if(b->nbreaks<80) b->breaks[b->nbreaks++]=emit(fs,I_AsBx(OP_JMP,0,0),s->line);
            break; }
    }
}

static void comp_block(FuncState *fs,Block *b){
    for(int i=0;i<b->n;i++) comp_stat(fs,b->s[i]);
}

static Proto *compile_proto(FuncState *parent,FuncBody *fb,Str *source,int karatsuba,int strict,int mkmod){
    FuncState fs; memset(&fs,0,sizeof fs);
    fs.prev=parent;
    fs.p=proto_new();
    /* !karatsuba/!strict per chunk: children inherit, imports don't */
    fs.p->uses_karatsuba = karatsuba || (parent && parent->p->uses_karatsuba);
    fs.p->uses_strict = strict || (parent && parent->p->uses_strict);
    fs.is_command = fb->is_command ? 1 : 0;
    fs.p->source=source;
    fs.p->name=fb->name?fb->name:str_fromc("?");
    fs.p->nparams=fb->nparams;
    fs.p->isvararg=fb->isvararg;
    fs.source=source;
    fs.line=fb->line;
    for(int i=0;i<fb->nparams;i++) newlocal(&fs,fb->params[i]);
    fs.freereg=fs.nlocals;
    fs.p->maxstack = fs.nlocals+2;
    /* `make` parts return export table (run discards it) */
    int exreg=-1;
    if(mkmod && !parent){
        exreg=newlocal(&fs,str_fromc("__exports"));
        fs.freereg=fs.nlocals;
        int tmp=reserve(&fs,1);
        emit(&fs,I_ABC(OP_NEWTABLE,tmp,0,0),fb->line);
        emit(&fs,I_ABC(OP_MOVE,exreg,tmp,0),fb->line);
        fs.freereg=fs.nlocals;
    }
    BlockCnt bl; enterblock(&fs,&bl,0);
    comp_block(&fs,fb->body);
    leaveblock(&fs,fb->line);
    if(exreg>=0){
        int base=reserve(&fs,1);
        emit(&fs,I_ABC(OP_MOVE,base,exreg,0),fb->line);
        emit(&fs,I_ABC(OP_RETURN,base,2,0),fb->line);
    } else emit(&fs,I_ABC(OP_RETURN,0,1,0),fb->line);
    if(fs.p->maxstack<2) fs.p->maxstack=2;
    return fs.p;
}

/* Compile chunk to main closure */
Closure *luc_compile(const char *src,int len,const char *chunkname){
    Parser ps; memset(&ps,0,sizeof ps);
    int karatsuba=0, strict=0, ndir=0;
    /* !karatsuba/!strict: leading lines only, one per line */
    for(;;){
        int consumed=0;
        if(len>10 && !strncmp(src,"!karatsuba",10) &&
           (len==10||src[10]=='\n'||src[10]=='\r'||src[10]==' '||src[10]=='\t')){
            karatsuba=1;
            consumed=10;
        } else if(len>7 && !strncmp(src,"!strict",7) &&
           (len==7||src[7]=='\n'||src[7]=='\r'||src[7]==' '||src[7]=='\t')){
            strict=1;
            consumed=7;
        } else if(len>0 && src[0]=='!' &&
           (len==1||src[1]=='\n'||src[1]=='\r'||src[1]==' '||src[1]=='\t'||(src[1]>='a'&&src[1]<='z'))){
            /* Unknown !directive: fail loudly, avoid silent typo like !strcit */
            int e=1;
            while(e<len && src[e]!='\n' && src[e]!='\r' && e<32) e++;
            char b[32]; int n=e<31?e:31;
            memcpy(b,src,(size_t)n); b[n]=0;
            Str *s=str_fromc(chunkname);
            char m[96]; snprintf(m,sizeof m,"%s:1: unknown directive '%s' (expected '!strict' or '!karatsuba')",s->s,b);
            luc_throw(mkobj(LT_STR,str_fromc(m)));
        }
        if(!consumed) break;
        while(consumed<len && src[consumed]!='\n') consumed++;
        if(consumed<len) consumed++;
        src+=consumed; len-=consumed;
        ndir++;   /* each directive eats exactly one source line */
    }
    ps.strict=strict; ps.lx.strict=strict;
    ps.lx.p=src; ps.lx.end=src+len; ps.lx.line=1+ndir;
    ps.lx.source=str_fromc(chunkname);
    lx_next(&ps.lx);
    Block *b=parse_block(&ps);
    if(ps.lx.t!=TK_EOF) perr(&ps,"'<eof>' expected");
    FuncBody fb; memset(&fb,0,sizeof fb);
    fb.body=b; fb.isvararg=1; fb.line=0; fb.name=str_fromc("main chunk");
    fb.params=(Str**)anew(sizeof(Str*)*2);
    Proto *p=compile_proto(NULL,&fb,ps.lx.source,karatsuba,strict,ps.makename!=NULL);
    return closure_new(p);
}

/* 11. VM */

YieldPt *g_yp=NULL;
static int g_cdepth=0;

static void vm_execute(LucState *L,int baselevel);int  vm_call(LucState *L,int func,int nargs,int nres);

static Upval *find_upval(LucState *L,int idx){
    Upval **pp=&L->openupv;
    while(*pp && (*pp)->idx > idx) pp=&(*pp)->next;
    if(*pp && (*pp)->idx==idx) return *pp;
    Upval *u=(Upval*)newobj(sizeof(Upval),LT_UPVAL);
    u->L=L; u->idx=idx; u->isclosed=0; u->closed=NIL;
    u->next=*pp; *pp=u;
    return u;
}
void close_upvals(LucState *L,int level){
    while(L->openupv && L->openupv->idx>=level){
        Upval *u=L->openupv;
        L->openupv=u->next;
        u->closed=L->stack[u->idx];
        u->isclosed=1; u->next=NULL;
    }
}

static double arith_num(Value v){
    if(v.t==LT_NUM) return v.u.n;
    if(v.t==LT_STR){ double d; if(str2num(AS_STR(v)->s,AS_STR(v)->len,&d)) return d; }
    luc_error("attempt to perform arithmetic on a %s value",type_name(v));
    return 0;
}
static Value vm_concat(Value a,Value b){
    if((a.t==LT_STR||a.t==LT_NUM)&&(b.t==LT_STR||b.t==LT_NUM)){
        Str *x=tostr(a),*y=tostr(b);
        int n=x->len+y->len;
        char *buf=(char*)lmalloc((size_t)n+1);
        memcpy(buf,x->s,(size_t)x->len);
        memcpy(buf+x->len,y->s,(size_t)y->len);
        Str *r=str_new(buf,n); free(buf);
        return mkobj(LT_STR,r);
    }
    if(a.t==LT_LIST||b.t==LT_LIST||a.t==LT_TABLE||b.t==LT_TABLE){
        Str *x=tostr(a),*y=tostr(b);
        int n=x->len+y->len;
        char *buf=(char*)lmalloc((size_t)n+1);
        memcpy(buf,x->s,(size_t)x->len); memcpy(buf+x->len,y->s,(size_t)y->len);
        Str *r=str_new(buf,n); free(buf);
        return mkobj(LT_STR,r);
    }
    luc_error("attempt to concatenate a %s value",
              (a.t==LT_STR||a.t==LT_NUM)?type_name(b):type_name(a));
    return NIL;
}
int vm_lessthan(Value a,Value b,int orequal){
    if(a.t==LT_NUM&&b.t==LT_NUM) return orequal? a.u.n<=b.u.n : a.u.n<b.u.n;
    if(a.t==LT_STR&&b.t==LT_STR){
        Str *x=AS_STR(a),*y=AS_STR(b);
        int n=x->len<y->len?x->len:y->len;
        int c=memcmp(x->s,y->s,(size_t)n);
        if(c==0) c = x->len<y->len?-1:(x->len>y->len?1:0);
        return orequal? c<=0 : c<0;
    }
    luc_error("attempt to compare %s with %s",type_name(a),type_name(b));
    return 0;
}
/* Bound method: dot-form hides self; colon-form keeps explicit self */
static int meth_trampoline(LucState *L,int base,int nargs,CFunc *cf){
    if(cf->up[1].t==LT_FUNC){
        /* User override: rebuild frame in place, tail call stays yield-safe */
        Value meth=cf->up[1], self=cf->up[0];   /* Heap-stable across GC */
        int has_self=nargs>0 && val_rawequal(L->stack[base],self);
        if(has_self){
            ensure_stack(L,base+nargs+8);
            for(int i=nargs-1;i>0;i--) L->stack[base+i+1]=L->stack[base+i];
            L->stack[base]=meth; L->stack[base+1]=self;
            return vm_call(L,base,nargs,-1);
        } else {
            ensure_stack(L,base+nargs+16);
            for(int i=nargs-1;i>=0;i--) L->stack[base+i+2]=L->stack[base+i];
            L->stack[base]=meth; L->stack[base+1]=self;
            return vm_call(L,base,nargs+1,-1);
        }
    }
    CFunc *t=(CFunc*)cf->up[1].u.o;
    if(nargs>0 && val_rawequal(L->stack[base],cf->up[0]))
        return t->fn(L,base,nargs,t);   /* Self already explicit: use as-is */
    ensure_stack(L,base+nargs+8);
    for(int i=nargs;i>0;i--) L->stack[base+i]=L->stack[base+i-1];
    L->stack[base]=cf->up[0];
    return t->fn(L,base,nargs+1,t);   /* Results already land at base */
}
static Value bind_method(Value self,Value m){
    CFunc *cf=cfunc_new(meth_trampoline,"method",2);
    cf->up[0]=self; cf->up[1]=m;
    return mkobj(LT_CFUNC,cf);
}

/* Core list remove is by value */
static int f_core_remove(LucState *L,int base,int nargs,CFunc *self){
    (void)self;
    Table *t=checktab(L,base,nargs,0,"remove");
    Value v=AR(1);
    for(int i=0;i<t->alen;i++)
        if(val_rawequal(t->arr[i],v)){ list_removeat(t,i+1); RET(0,mkbool(1)); return 1; }
    RET(0,mkbool(0)); return 1;
}

static Value vm_index(Value t,Value k){
    switch(t.t){
        case LT_TABLE: {
            Table *tb=AS_TAB(t);
            Value r=tab_get(tb,k);
            int d=0;
            while(r.t==LT_NIL && tb->meta && d<16){
                Value h=tab_get(tb->meta,mkobj(LT_STR,str_fromc("__index")));
                if(h.t==LT_TABLE||h.t==LT_LIST){ tb=AS_TAB(h); r=tab_get(tb,k); d++; }
                else break;
            }
            if(r.t==LT_NIL && k.t==LT_STR && V.tabmeta){
                Value m=tab_get(V.tabmeta,k);
                if(m.t!=LT_NIL) return bind_method(t,m);
            }
            return r;
        }
        case LT_LIST:
            if(k.t==LT_NUM){
                double d=k.u.n;
                if(d!=d || d!=floor(d)) return NIL;
                int i=(int)d, n=AS_TAB(t)->alen;
                if(i<0) i+=n;
                if(i>=0 && i<n) return AS_TAB(t)->arr[i];
                return NIL;
            }
            if(k.t==LT_STR){
                Value m = V.listcore? tab_get(V.listcore,k) : NIL;
                if(m.t==LT_NIL) m=tab_get(V.listmeta,k);
                if(m.t==LT_NIL) return NIL;
                return bind_method(t,m);
            }
            return tab_get(AS_TAB(t),k);
        case LT_STR:
            if(k.t==LT_NUM){
                double d=k.u.n;
                if(d!=d || d!=floor(d)) return NIL;
                int i=(int)d, n=AS_STR(t)->len;
                if(i<0) i+=n;
                if(i>=0 && i<n) return strv(AS_STR(t)->s+i,1);
                return NIL;
            }
            if(k.t==LT_STR){
                Value m=tab_get(V.stringlib,k);
                if(m.t==LT_NIL) return NIL;
                return bind_method(t,m);
            }
            return NIL;
        case LT_BUFFER: {
            if(k.t!=LT_STR) return NIL;
            Value m=tab_get(V.bufferlib,k);
            return m.t==LT_NIL? NIL : bind_method(t,m);
        }
        case LT_FILE: {
            if(k.t!=LT_STR) return NIL;
            Value m=tab_get(V.filelib,k);
            return m.t==LT_NIL? NIL : bind_method(t,m);
        }
        case LT_SOCKET: {
            if(k.t!=LT_STR) return NIL;
            Value m=V.socklib? tab_get(V.socklib,k) : NIL;
            return m.t==LT_NIL? NIL : bind_method(t,m);
        }
        default:
            luc_error("attempt to index a %s value",type_name(t));
    }
    return NIL;
}
static void vm_setindex(Value t,Value k,Value v){
    if(t.t==LT_LIST && k.t==LT_NUM){
        double d=k.u.n;
        if(d!=d || d!=floor(d)) luc_error("list index is not an integer");
        int i=(int)d, n=AS_TAB(t)->alen;
        if(i<0) i+=n;
        if(i<0) luc_error("list index out of range");
        tab_set(AS_TAB(t),mknum((double)(i+1)),v);
        return;
    }
    if(t.t==LT_TABLE||t.t==LT_LIST) tab_set(AS_TAB(t),k,v);
    else luc_error("attempt to index a %s value",type_name(t));
}
int vm_len(Value v){
    switch(v.t){
        case LT_STR: return AS_STR(v)->len;
        case LT_TABLE: {
            Table *t=AS_TAB(v);
            int n=0; Value k=NIL,w;
            while(tab_next(t,k,&k,&w)) n++;
            return n;
        }
        case LT_LIST: return AS_TAB(v)->alen;
        case LT_BUFFER: return AS_BUF(v)->len;
        default: luc_error("attempt to get length of a %s value",type_name(v));
    }
    return 0;
}
int vm_in(Value x,Value c){
    if(c.t==LT_LIST){
        Table *t=AS_TAB(c);
        for(int i=0;i<t->alen;i++) if(val_rawequal(t->arr[i],x)) return 1;
        return 0;
    }
    if(c.t==LT_TABLE){
        Table *t=AS_TAB(c);
        if(x.t==LT_NUM){
            double d=x.u.n;
            if(d==floor(d) && d>=1 && d<=t->alen) return 1;
        }
        for(int i=0;i<t->ecap;i++)
            if(t->ents[i].k.t!=LT_NIL && val_rawequal(t->ents[i].k,x)) return 1;
        return 0;
    }
    if(c.t==LT_STR){
        Str *h=AS_STR(c); Str *n=tostr(x);
        if(n->len==0) return 1;
        if(n->len>h->len) return 0;
        for(int i=0;i+n->len<=h->len;i++)
            if(memcmp(h->s+i,n->s,(size_t)n->len)==0) return 1;
        return 0;
    }
    luc_error("attempt to use 'in' on a %s value",type_name(c));
    return 0;
}

/* Dict dot methods: t.keys(), t.values() */
static int f_dict_keys(LucState *L,int base,int nargs,CFunc *self){
    (void)self; (void)nargs;
    Table *t=checktab(L,base,nargs,0,"keys");
    Table *r=tab_new(1);
    Value k=NIL,v;
    while(tab_next(t,k,&k,&v)) list_push(r,k);
    RET(0,mkobj(LT_LIST,r)); return 1;
}
static int f_dict_values(LucState *L,int base,int nargs,CFunc *self){
    (void)self; (void)nargs;
    Table *t=checktab(L,base,nargs,0,"values");
    Table *r=tab_new(1);
    Value k=NIL,v;
    while(tab_next(t,k,&k,&v)) list_push(r,v);
    RET(0,mkobj(LT_LIST,r)); return 1;
}

/* len(x) replaces Lua '#' */
static int f_core_len(LucState *L,int base,int nargs,CFunc *self){
    (void)self;
    RET(0,mknum((double)vm_len(AR(0)))); return 1;
}

/* pairs/ipairs trap with migration hint */
static int f_lua_trap(LucState *L,int base,int nargs,CFunc *self){
    (void)L; (void)base; (void)nargs;
    if(!strcmp(self->name,"ipairs"))
        luc_error("'ipairs' is Lua syntax - LUC iterates lists with: repeat item, i in list do ... end");
    luc_error("'pairs' is Lua syntax - LUC iterates dicts with: repeat k, v in dict do ... end");
    return 0;
}

static void pushframe(LucState *L,int func,int nargs,int nres){
    Closure *cl=AS_CL(L->stack[func]);
    Proto *p=cl->p; int i,bse;
    if(L->nci>=LUC_MAXCI) luc_error("stack overflow (too much recursion)");
    if(p->isvararg){
        int actual=nargs>p->nparams?nargs:p->nparams;
        ensure_stack(L,func+2+actual+p->maxstack+8);
        for(i=nargs;i<p->nparams;i++) L->stack[func+1+i]=NIL;
        bse=func+1+actual;
        for(i=0;i<p->nparams;i++){ L->stack[bse+i]=L->stack[func+1+i]; L->stack[func+1+i]=NIL; }
        for(i=p->nparams;i<p->maxstack;i++) L->stack[bse+i]=NIL;
    } else {
        int room=nargs>p->maxstack?nargs:p->maxstack;
        ensure_stack(L,func+2+room+8);
        bse=func+1;
        for(i=p->nparams;i<p->maxstack;i++) L->stack[bse+i]=NIL;
        for(i=nargs;i<p->nparams;i++) L->stack[bse+i]=NIL;
    }
    if(L->nci==L->cicap){
        L->cicap*=2;
        L->ci=(CallInfo*)lrealloc(L->ci,sizeof(CallInfo)*(size_t)L->cicap);
    }
    CallInfo *nci=&L->ci[L->nci++];
    nci->cl=cl; nci->func=func; nci->base=bse; nci->nresults=nres; nci->savedpc=p->code;
    L->top=bse+p->maxstack;
}

/* Generic C call; results at `func` */
int vm_call(LucState *L,int func,int nargs,int nres){
    Value f=L->stack[func];
    if(f.t==LT_CFUNC){
        CFunc *cf=AS_CF(f);
        ensure_stack(L,func+nargs+64);
        int save=L->top;
        L->top=func+1+nargs;
        g_cdepth++;
        int n=cf->fn(L,func+1,nargs,cf);
        g_cdepth--;
        for(int i=0;i<n;i++) L->stack[func+i]=L->stack[func+1+i];
        if(nres>=0){ for(int i=n;i<nres;i++) L->stack[func+i]=NIL; n=nres; }
        L->top=save>func+n?save:func+n;
        return n;
    }
    if(f.t==LT_FUNC){
        int jn=luc_jit_call(L,func,nargs,nres);      /* JIT fast path */
        if(jn!=LUC_JIT_FALLBACK) return jn;
        int level=L->nci;
        pushframe(L,func,nargs,nres);
        g_cdepth++;
        vm_execute(L,level);
        g_cdepth--;
        int n=L->top-func;
        if(nres>=0) n=nres;
        return n;
    }
    luc_error("attempt to call a %s value",type_name(f));
    return 0;
}

/* Metatable-lite runtime */

/* Call f(args) for one result. NOTE: may grow stack/frames, refresh base/pc */
static Value meta_callv(LucState *L,Value f,Value *args,int n){
    ensure_stack(L,L->top+n+8);
    int slot=L->top;
    L->stack[slot]=f;
    for(int i=0;i<n;i++) L->stack[slot+1+i]=args[i];
    L->top=slot+n+1;
    vm_call(L,slot,n,1);
    Value r=L->stack[slot];
    L->top=slot;
    return r;
}

/* Find 'ev' metamethod, x then y; 1 with *out on hit */
static int vm_metabin(LucState *L,Value x,Value y,const char *ev,Value *out){
    if(x.t==LT_TABLE && AS_TAB(x)->meta){
        Value f=tab_get(AS_TAB(x)->meta,mkobj(LT_STR,str_fromc(ev)));
        if(f.t==LT_FUNC||f.t==LT_CFUNC){ Value a[2]={x,y}; *out=meta_callv(L,f,a,2); return 1; }
    }
    if(y.t==LT_TABLE && AS_TAB(y)->meta){
        Value f=tab_get(AS_TAB(y)->meta,mkobj(LT_STR,str_fromc(ev)));
        if(f.t==LT_FUNC||f.t==LT_CFUNC){ Value a[2]={x,y}; *out=meta_callv(L,f,a,2); return 1; }
    }
    return 0;
}

/* OP_APPEND check: stock append fast path, override uses generic path */
static Str *g_appendStr=NULL;
static Value g_origAppend;
static void append_cache_init(void){
    g_appendStr=str_fromc("append");
    g_origAppend=tab_get(V.listmeta,mkobj(LT_STR,g_appendStr));
}

/* Fused X[k] <op>= v (0:+,1:-,2:*,3:/); matches unfused behavior */
static void vm_compound(LucState *L,int which,uint32_t *pc,Value tv,Value kv,Value vv){
    static const char *mnames[4]={"__add","__sub","__mul","__div"};
    if(tv.t==LT_LIST && kv.t==LT_NUM && vv.t==LT_NUM){
        double d=kv.u.n;
        if(d>=-2147483648.0 && d<2147483648.0){
            int i=(int)d;
            if((double)i==d){
                Table *tb=AS_TAB(tv); int n=tb->alen;
                if(i<0) i+=n;
                if(i>=0 && i<n && n>0 && tb->arr[n-1].t!=LT_NIL){
                    Value old=tb->arr[i];
                    if(old.t==LT_NUM){
                        double r=which==0?old.u.n+vv.u.n : which==1?old.u.n-vv.u.n :
                                  which==2?old.u.n*vv.u.n : old.u.n/vv.u.n;
                        tb->arr[i]=mknum(r);
                        return;
                    }
                }
            }
        }
    }
    CallInfo *ci=&L->ci[L->nci-1]; Value *base=L->stack+ci->base;
    Value oldv=vm_index(tv,kv);
    if(oldv.t==LT_NIL && tv.t==LT_TABLE && AS_TAB(tv)->meta){
        Value h=tab_get(AS_TAB(tv)->meta,mkobj(LT_STR,str_fromc("__index")));
        if(h.t==LT_FUNC||h.t==LT_CFUNC){
            ci->savedpc=pc;
            oldv=meta_callv(L,h,(Value[]){tv,kv},2);
            ci=&L->ci[L->nci-1]; base=L->stack+ci->base;
            L->top=ci->base+ci->cl->p->maxstack;
        }
    }
    Value nv;
    if(oldv.t==LT_NUM && vv.t==LT_NUM){
        double r=which==0?oldv.u.n+vv.u.n : which==1?oldv.u.n-vv.u.n :
                  which==2?oldv.u.n*vv.u.n : oldv.u.n/vv.u.n;
        nv=mknum(r);
    } else if(which==0 && (oldv.t==LT_STR||oldv.t==LT_NUM)&&(vv.t==LT_STR||vv.t==LT_NUM)){
        nv=vm_concat(oldv,vv);
    } else {
        ci->savedpc=pc; Value mr;
        if(vm_metabin(L,oldv,vv,mnames[which],&mr)){
            ci=&L->ci[L->nci-1]; base=L->stack+ci->base; pc=ci->savedpc;
            L->top=ci->base+ci->cl->p->maxstack;
            nv=mr;
        } else if(which==2 && luc_try_bigmul(oldv,vv,L->ci[L->nci-1].cl->p->uses_karatsuba,&mr)){
            nv=mr;
        } else if(which==0) nv=mknum(arith_num(oldv)+arith_num(vv));
        else if(which==1) nv=mknum(arith_num(oldv)-arith_num(vv));
        else if(which==2) nv=mknum(arith_num(oldv)*arith_num(vv));
        else nv=mknum(arith_num(oldv)/arith_num(vv));
    }
    (void)base;
    vm_setindex(tv,kv,nv);
}

static void vm_execute(LucState *L,int baselevel){
    CallInfo *ci; Closure *cl; Proto *pr; uint32_t *pc; Value *base; Value *K;
    V.cur=L;
#if defined(__GNUC__) || defined(__clang__)
#define VM_LABEL(name) vm_op_##name:
#define VM_NEXT goto vm_dispatch
    void *dispatch[OP_COUNT] = {
        &&vm_op_MOVE, &&vm_op_LOADK, &&vm_op_LOADNIL, &&vm_op_LOADBOOL,
        &&vm_op_GETGLOBAL, &&vm_op_SETGLOBAL, &&vm_op_GETUPVAL, &&vm_op_SETUPVAL,
        &&vm_op_GETTABLE, &&vm_op_SETTABLE, &&vm_op_NEWTABLE, &&vm_op_SETLIST,
        &&vm_op_SELF, &&vm_op_ADD, &&vm_op_SUB, &&vm_op_MUL, &&vm_op_DIV,
        &&vm_op_MOD, &&vm_op_POW, &&vm_op_UNM, &&vm_op_NOT, &&vm_op_LEN,
        &&vm_op_CONCAT, &&vm_op_EQ, &&vm_op_NE, &&vm_op_LT, &&vm_op_LE,
        &&vm_op_GT, &&vm_op_GE, &&vm_op_IN, &&vm_op_JMP, &&vm_op_JMPIF,
        &&vm_op_JMPIFNOT, &&vm_op_CALL, &&vm_op_RETURN, &&vm_op_CLOSURE,
        &&vm_op_VARARG, &&vm_op_CLOSE, &&vm_op_FORPREP, &&vm_op_FORLOOP,
        &&vm_op_TFORLOOP, &&vm_op_SLICE, &&vm_op_NEXT, &&vm_op_APPEND,
        &&vm_op_ADDEQ, &&vm_op_SUBEQ, &&vm_op_MULEQ, &&vm_op_DIVEQ
    };
#else
#define VM_LABEL(name) case OP_##name:
#define VM_NEXT break
#endif
 reentry:
    ci=&L->ci[L->nci-1];
    cl=ci->cl; pr=cl->p; pc=ci->savedpc; base=L->stack+ci->base; K=pr->k;
    L->cursource=pr->source;
    for(;;){
#if defined(__GNUC__) || defined(__clang__)
    vm_dispatch:
        ;
#endif
        uint32_t ins=*pc++;
        int A=GET_A(ins);
        L->curline=pr->lines[(int)(pc-1-pr->code)];
#if defined(__GNUC__) || defined(__clang__)
        int op=GET_OP(ins);
        if((unsigned)op>=OP_COUNT) luc_error("bad opcode %d",op);
        goto *dispatch[op];
#else
        switch(GET_OP(ins)){
#endif
        VM_LABEL(MOVE)     { base[A]=base[GET_B(ins)]; } VM_NEXT;
        VM_LABEL(LOADK)    { base[A]=K[GET_Bx(ins)]; } VM_NEXT;
        VM_LABEL(LOADNIL)  { base[A]=NIL; } VM_NEXT;
        VM_LABEL(LOADBOOL) { base[A]=mkbool(GET_B(ins)); } VM_NEXT;
        VM_LABEL(GETGLOBAL) { base[A]=tab_get(V.globals,K[GET_Bx(ins)]); } VM_NEXT;
        VM_LABEL(SETGLOBAL) { tab_set(V.globals,K[GET_Bx(ins)],base[A]); } VM_NEXT;
        VM_LABEL(GETUPVAL) { base[A]=*UPVAL_PTR(cl->up[GET_B(ins)]); } VM_NEXT;
        VM_LABEL(SETUPVAL) { *UPVAL_PTR(cl->up[GET_B(ins)])=base[A]; } VM_NEXT;
        VM_LABEL(GETTABLE) {
            Value tv=base[GET_B(ins)], kv=base[GET_C(ins)];
            if(tv.t==LT_LIST && kv.t==LT_NUM){
                /* Fast path: in-range int key, else generic vm_index */
                double d=kv.u.n;
                if(d>=-2147483648.0 && d<2147483648.0){
                    int i=(int)d;
                    if((double)i==d){
                        Table *tb=AS_TAB(tv); int n=tb->alen;
                        if(i<0) i+=n;
                        if(i>=0 && i<n){ base[A]=tb->arr[i]; VM_NEXT; }
                    }
                }
            }
            Value r=vm_index(tv,kv);
            if(r.t==LT_NIL && tv.t==LT_TABLE && AS_TAB(tv)->meta){
                Value h=tab_get(AS_TAB(tv)->meta,mkobj(LT_STR,str_fromc("__index")));
                if(h.t==LT_FUNC||h.t==LT_CFUNC){
                    ci->savedpc=pc;
                    r=meta_callv(L,h,(Value[]){tv,kv},2);
                    ci=&L->ci[L->nci-1]; base=L->stack+ci->base; pc=ci->savedpc;
                    L->top=ci->base+pr->maxstack;
                }
            }
            base[A]=r;
        } VM_NEXT;
        VM_LABEL(SETTABLE) {
            Value tv=base[A], kv=base[GET_B(ins)], vv=base[GET_C(ins)];
            if(tv.t==LT_LIST && kv.t==LT_NUM && vv.t!=LT_NIL){
                /* Fast path: in-bounds non-nil store, else generic path */
                double d=kv.u.n;
                if(d>=-2147483648.0 && d<2147483648.0){
                    int i=(int)d;
                    if((double)i==d){
                        Table *tb=AS_TAB(tv); int n=tb->alen;
                        if(i<0) i+=n;
                        if(i>=0 && i<n && n>0 && tb->arr[n-1].t!=LT_NIL){
                            tb->arr[i]=vv; VM_NEXT;
                        }
                    }
                }
            }
            vm_setindex(base[A],base[GET_B(ins)],base[GET_C(ins)]);
        } VM_NEXT;
        VM_LABEL(NEWTABLE) {
            if(V.nalloc>V.gcthresh){ ci->savedpc=pc; gc_collect(); }
            base[A]=mkobj(GET_B(ins)?LT_LIST:LT_TABLE,tab_new(GET_B(ins)));
        } VM_NEXT;
        VM_LABEL(SETLIST) {
            int b=GET_B(ins), c=GET_C(ins);
            int n = b? b : (int)(L->top-(ci->base+A+1));
            Table *t=AS_TAB(base[A]);
            for(int i=0;i<n;i++) tab_set(t,mknum((double)(c+i)),base[A+1+i]);
            L->top=ci->base+pr->maxstack;
        } VM_NEXT;
        VM_LABEL(SELF) {
            int rb=GET_B(ins);
            base[A+1]=base[rb];
            Value tv=base[rb], kv=base[GET_C(ins)];
            Value r=vm_index(tv,kv);
            if(r.t==LT_NIL && tv.t==LT_TABLE && AS_TAB(tv)->meta){
                Value h=tab_get(AS_TAB(tv)->meta,mkobj(LT_STR,str_fromc("__index")));
                if(h.t==LT_FUNC||h.t==LT_CFUNC){
                    ci->savedpc=pc;
                    r=meta_callv(L,h,(Value[]){tv,kv},2);
                    ci=&L->ci[L->nci-1]; base=L->stack+ci->base; pc=ci->savedpc;
                    L->top=ci->base+pr->maxstack;
                }
            }
            base[A]=r;
        } VM_NEXT;
        VM_LABEL(ADD) {
            Value x=base[GET_B(ins)], y=base[GET_C(ins)];
            if(x.t==LT_NUM && y.t==LT_NUM){ base[A]=mknum(x.u.n+y.u.n); VM_NEXT; }
            if((x.t==LT_STR||x.t==LT_NUM)&&(y.t==LT_STR||y.t==LT_NUM)){ base[A]=vm_concat(x,y); VM_NEXT; }
            Value mr; ci->savedpc=pc;
            if(vm_metabin(L,x,y,"__add",&mr)){
                ci=&L->ci[L->nci-1]; base=L->stack+ci->base; pc=ci->savedpc;
                L->top=ci->base+pr->maxstack;
                base[A]=mr; VM_NEXT;
            }
            base[A]=mknum(arith_num(x)+arith_num(y));
        } VM_NEXT;
        VM_LABEL(SUB) {
            Value x=base[GET_B(ins)], y=base[GET_C(ins)];
            if(x.t==LT_NUM && y.t==LT_NUM){ base[A]=mknum(x.u.n-y.u.n); VM_NEXT; }
            Value mr; ci->savedpc=pc;
            if(vm_metabin(L,x,y,"__sub",&mr)){
                ci=&L->ci[L->nci-1]; base=L->stack+ci->base; pc=ci->savedpc;
                L->top=ci->base+pr->maxstack;
                base[A]=mr; VM_NEXT;
            }
            base[A]=mknum(arith_num(x)-arith_num(y));
        } VM_NEXT;
        VM_LABEL(MUL) {
            Value x=base[GET_B(ins)], y=base[GET_C(ins)];
            if(x.t==LT_NUM && y.t==LT_NUM){ base[A]=mknum(x.u.n*y.u.n); VM_NEXT; }
            Value mr; ci->savedpc=pc;
            if(vm_metabin(L,x,y,"__mul",&mr)){
                ci=&L->ci[L->nci-1]; base=L->stack+ci->base; pc=ci->savedpc;
                L->top=ci->base+pr->maxstack;
                base[A]=mr; VM_NEXT;
            }
            /* Exact big-int * for large int strings or !karatsuba */
            if(luc_try_bigmul(x,y,pr->uses_karatsuba,&mr)){ base[A]=mr; VM_NEXT; }
            base[A]=mknum(arith_num(x)*arith_num(y));
        } VM_NEXT;
        VM_LABEL(DIV) {
            Value x=base[GET_B(ins)], y=base[GET_C(ins)];
            if(x.t==LT_NUM && y.t==LT_NUM){ base[A]=mknum(x.u.n/y.u.n); VM_NEXT; }
            Value mr; ci->savedpc=pc;
            if(vm_metabin(L,x,y,"__div",&mr)){
                ci=&L->ci[L->nci-1]; base=L->stack+ci->base; pc=ci->savedpc;
                L->top=ci->base+pr->maxstack;
                base[A]=mr; VM_NEXT;
            }
            base[A]=mknum(arith_num(x)/arith_num(y));
        } VM_NEXT;
        VM_LABEL(MOD) {
            Value x=base[GET_B(ins)], y=base[GET_C(ins)];
            if(x.t==LT_NUM && y.t==LT_NUM)
                base[A]=mknum(x.u.n-floor(x.u.n/y.u.n)*y.u.n);
            else {
                double xn=arith_num(x), yn=arith_num(y);
                base[A]=mknum(xn-floor(xn/yn)*yn);
            }
        } VM_NEXT;
        VM_LABEL(POW) {
            Value x=base[GET_B(ins)], y=base[GET_C(ins)];
            if(x.t!=LT_NUM || y.t!=LT_NUM){
                Value mr; ci->savedpc=pc;
                if(vm_metabin(L,x,y,"__pow",&mr)){
                    ci=&L->ci[L->nci-1]; base=L->stack+ci->base; pc=ci->savedpc;
                    L->top=ci->base+pr->maxstack;
                    base[A]=mr; VM_NEXT;
                }
            }
            base[A]=mknum(pow(arith_num(x),arith_num(y)));
        } VM_NEXT;
        VM_LABEL(UNM) {
            Value x=base[GET_B(ins)];
            if(x.t==LT_NUM){ base[A]=mknum(-x.u.n); VM_NEXT; }
            if(x.t==LT_TABLE && AS_TAB(x)->meta){
                Value mr; ci->savedpc=pc;
                if(vm_metabin(L,x,x,"__unm",&mr)){
                    ci=&L->ci[L->nci-1]; base=L->stack+ci->base; pc=ci->savedpc;
                    L->top=ci->base+pr->maxstack;
                    base[A]=mr; VM_NEXT;
                }
            }
            base[A]=mknum(-arith_num(x));
        } VM_NEXT;
        VM_LABEL(NOT) { base[A]=mkbool(!truthy(base[GET_B(ins)])); } VM_NEXT;
        VM_LABEL(LEN) { base[A]=mknum((double)vm_len(base[GET_B(ins)])); } VM_NEXT;
        VM_LABEL(CONCAT) { base[A]=vm_concat(base[GET_B(ins)],base[GET_C(ins)]); } VM_NEXT;
        VM_LABEL(EQ) { base[A]=mkbool(val_rawequal(base[GET_B(ins)],base[GET_C(ins)])); } VM_NEXT;
        VM_LABEL(NE) { base[A]=mkbool(!val_rawequal(base[GET_B(ins)],base[GET_C(ins)])); } VM_NEXT;
        VM_LABEL(LT) { base[A]=mkbool(vm_lessthan(base[GET_B(ins)],base[GET_C(ins)],0)); } VM_NEXT;
        VM_LABEL(LE) { base[A]=mkbool(vm_lessthan(base[GET_B(ins)],base[GET_C(ins)],1)); } VM_NEXT;
        VM_LABEL(GT) { base[A]=mkbool(vm_lessthan(base[GET_C(ins)],base[GET_B(ins)],0)); } VM_NEXT;
        VM_LABEL(GE) { base[A]=mkbool(vm_lessthan(base[GET_C(ins)],base[GET_B(ins)],1)); } VM_NEXT;
        VM_LABEL(IN) { base[A]=mkbool(vm_in(base[GET_B(ins)],base[GET_C(ins)])); } VM_NEXT;
        VM_LABEL(JMP) { pc+=GET_sBx(ins); } VM_NEXT;
        VM_LABEL(JMPIF) { if(truthy(base[A])) pc+=GET_sBx(ins); } VM_NEXT;
        VM_LABEL(JMPIFNOT) { if(!truthy(base[A])) pc+=GET_sBx(ins); } VM_NEXT;
        VM_LABEL(CLOSE) { close_upvals(L,ci->base+A); } VM_NEXT;
        VM_LABEL(VARARG) {
            int b=GET_B(ins);
            int vabase=ci->func+1+pr->nparams;
            int nva=ci->base-vabase; if(nva<0) nva=0;
            if(b==0){
                ensure_stack(L,ci->base+A+nva+2);
                base=L->stack+ci->base;
                for(int i=0;i<nva;i++) base[A+i]=L->stack[vabase+i];
                L->top=ci->base+A+nva;
            } else {
                for(int i=0;i<b-1;i++) base[A+i]= i<nva? L->stack[vabase+i] : NIL;
            }
        } VM_NEXT;
        VM_LABEL(CLOSURE) {
            if(V.nalloc>V.gcthresh){ ci->savedpc=pc; gc_collect(); }
            Proto *np=pr->p[GET_Bx(ins)];
            Closure *nc=closure_new(np);
            for(int i=0;i<np->nup;i++){
                if(np->upvals[i].instack) nc->up[i]=find_upval(L,ci->base+np->upvals[i].idx);
                else nc->up[i]=cl->up[np->upvals[i].idx];
            }
            base[A]=mkobj(LT_FUNC,nc);
        } VM_NEXT;
        VM_LABEL(CALL) {
            int b=GET_B(ins), c=GET_C(ins);
            int func=ci->base+A;
            int na = b? b-1 : (int)(L->top-(func+1));
            int nres = c? c-1 : -1;
            Value f=L->stack[func];
            if(f.t==LT_TABLE){
                Table *mtb=AS_TAB(f)->meta;
                Value hf = mtb? tab_get(mtb,mkobj(LT_STR,str_fromc("__call"))) : NIL;
                if(hf.t==LT_FUNC||hf.t==LT_CFUNC){
                    ci->savedpc=pc;
                    ensure_stack(L,func+na+64);
                    base=L->stack+ci->base;
                    for(int i=na;i>0;i--) L->stack[func+1+i]=L->stack[func+i];
                    L->stack[func+1]=f;
                    L->stack[func]=hf;
                    f=hf; na=na+1;
                } else luc_error("attempt to call a %s value",type_name(f));
            }
            if(f.t==LT_CFUNC && AS_CF(f)->fn==meth_trampoline &&
               AS_CF(f)->up[1].t==LT_FUNC){
                /* Bound LUC closure: direct call, no C frame, stays yield-safe */
                CFunc *tr=AS_CF(f);
                Value meth=tr->up[1], self=tr->up[0];
                int has_self=na>0 && val_rawequal(L->stack[func+1],self);
                ci->savedpc=pc;
                if(has_self){
                    /* Args home, only callee slot needs method */
                    ensure_stack(L,func+na+8);
                    L->stack[func]=meth;
                    pushframe(L,func,na,nres);
                } else {
                    /* Shift args only, never callee slot */
                    ensure_stack(L,func+na+16);
                    for(int i=na;i>=1;i--) L->stack[func+i+1]=L->stack[func+i];
                    L->stack[func]=meth; L->stack[func+1]=self;
                    pushframe(L,func,na+1,nres);
                }
                goto reentry;
            }
            if(f.t==LT_CFUNC){
                CFunc *cf=AS_CF(f);
                ci->savedpc=pc; L->yield_A=A; L->yield_C=nres;
                ensure_stack(L,func+na+64);
                L->top=func+1+na;
                int n=cf->fn(L,func+1,na,cf);
                for(int i=0;i<n;i++) L->stack[func+i]=L->stack[func+1+i];
                if(nres>=0){ for(int i=n;i<nres;i++) L->stack[func+i]=NIL; }
                base=L->stack+ci->base;
                L->top = (nres<0)? func+n : ci->base+pr->maxstack;
            } else if(f.t==LT_FUNC){
                ci->savedpc=pc;
                int jn=luc_jit_call(L,func,na,nres);
                if(jn!=LUC_JIT_FALLBACK){
                    base=L->stack+ci->base;   /* Stack may have moved */
                    L->top = (nres<0)? func+jn : ci->base+pr->maxstack;
                } else {
                    pushframe(L,func,na,nres);
                    goto reentry;
                }
            } else luc_error("attempt to call a %s value",type_name(f));
        } VM_NEXT;
        VM_LABEL(APPEND) {
            /* Fused append: A=self, B=arg count, C=nres as CALL */
            int b=GET_B(ins), c=GET_C(ins);
            int n = b? b : (int)(L->top-(ci->base+A+1));
            int nres = c? c-1 : -1;
            Value self=base[A];
            if(self.t==LT_LIST &&
               val_rawequal(tab_get(V.listmeta,mkobj(LT_STR,g_appendStr)),g_origAppend)){
                /* Stock append: direct push, no alloc or error */
                Table *t=AS_TAB(self);
                for(int i=0;i<n;i++) list_push(t,base[A+1+i]);
                base[A]=self;
                if(nres>=0){ for(int i=1;i<nres;i++) base[A+i]=NIL; L->top=ci->base+pr->maxstack; }
                else L->top=ci->base+A+1;
                VM_NEXT;
            }
            /* Generic append: callee runs in place at A, tail-safe; self parked above top */
            ci->savedpc=pc; L->yield_A=A; L->yield_C=nres;
            ensure_stack(L,L->top+2*n+16);
            base=L->stack+ci->base;
            Value kv=mkobj(LT_STR,g_appendStr);
            Value m=vm_index(base[A],kv);
            if(m.t==LT_NIL && base[A].t==LT_TABLE && AS_TAB(base[A])->meta){
                Value h=tab_get(AS_TAB(base[A])->meta,mkobj(LT_STR,str_fromc("__index")));
                if(h.t==LT_FUNC||h.t==LT_CFUNC){
                    m=meta_callv(L,h,(Value[]){base[A],kv},2);
                    ci=&L->ci[L->nci-1]; base=L->stack+ci->base; pc=ci->savedpc;
                    L->top=ci->base+pr->maxstack;
                }
            }
            int got, nargs=n, plain=1;
            if(m.t==LT_TABLE){
                Table *mtb=AS_TAB(m)->meta;
                Value hf=mtb?tab_get(mtb,mkobj(LT_STR,str_fromc("__call"))):NIL;
                if(hf.t==LT_FUNC||hf.t==LT_CFUNC){
                    /* Shift [A..A+n] right: [A]=hf [A+1]=m */
                    for(int i=n;i>=0;i--) base[A+i+1]=base[A+i];
                    base[A]=hf; nargs=n+1; plain=0;
                }
            }
            if(plain){
                /* Park self above top, method at A */
                int keep=L->top;
                L->stack[keep]=base[A];
                L->top=keep+1;
                base[A]=m;
            }
            got=vm_call(L,ci->base+A,nargs,nres);
            ci=&L->ci[L->nci-1];   /* vm_call may grow frames */
            L->top=nres>=0?ci->base+pr->maxstack:ci->base+A+got;
        } VM_NEXT;
        VM_LABEL(ADDEQ) { vm_compound(L,0,pc,base[A],base[GET_B(ins)],base[GET_C(ins)]); } VM_NEXT;
        VM_LABEL(SUBEQ) { vm_compound(L,1,pc,base[A],base[GET_B(ins)],base[GET_C(ins)]); } VM_NEXT;
        VM_LABEL(MULEQ) { vm_compound(L,2,pc,base[A],base[GET_B(ins)],base[GET_C(ins)]); } VM_NEXT;
        VM_LABEL(DIVEQ) { vm_compound(L,3,pc,base[A],base[GET_B(ins)],base[GET_C(ins)]); } VM_NEXT;
        VM_LABEL(RETURN) {
            int b=GET_B(ins);
            int n = b? b-1 : (int)(L->top-(ci->base+A));
            close_upvals(L,ci->base);
            int func=ci->func, want=ci->nresults;
            for(int i=0;i<n;i++) L->stack[func+i]=base[A+i];
            L->nci--;
            if(want>=0){ for(int i=n;i<want;i++) L->stack[func+i]=NIL; L->top=func+want; }
            else L->top=func+n;
            if(L->nci<=baselevel) return;
            ci=&L->ci[L->nci-1];
            cl=ci->cl; pr=cl->p; pc=ci->savedpc; base=L->stack+ci->base; K=pr->k;
            L->cursource=pr->source;
            if(want>=0) L->top=ci->base+pr->maxstack;
        } VM_NEXT;
        VM_LABEL(FORPREP) {
/* repeat up stops before dest, down lands on 0 */
            double st=arith_num(base[A]), tg=arith_num(base[A+1]);
            if(st==0) luc_error("repeat: step cannot be zero");
            if(st>0){ base[A]=mknum(-st); base[A+1]=mknum(tg); }
            else { base[A]=mknum(tg-st); base[A+1]=mknum(0); }
            base[A+2]=mknum(st);
            pc+=GET_sBx(ins);
        } VM_NEXT;
        VM_LABEL(FORLOOP) {
            double idx=base[A].u.n+base[A+2].u.n;
            double lim=base[A+1].u.n, st=base[A+2].u.n;
            if(st>0? idx<lim : idx>=lim){
                base[A]=mknum(idx); base[A+3]=mknum(idx);
                pc+=GET_sBx(ins);
            }
        } VM_NEXT;
        VM_LABEL(TFORLOOP) {
            int nvars=GET_C(ins);
            int cb=ci->base+A+3;
            ensure_stack(L,cb+nvars+8);
            base=L->stack+ci->base;
            L->stack[cb]=base[A]; L->stack[cb+1]=base[A+1]; L->stack[cb+2]=base[A+2];
            ci->savedpc=pc;
            vm_call(L,cb,2,nvars);
            base=L->stack+ci->base;
            L->top=ci->base+pr->maxstack;
            if(L->stack[cb].t!=LT_NIL) base[A+2]=L->stack[cb];
            else pc++;
            for(int i=0;i<nvars;i++) base[A+3+i]=L->stack[cb+i];
        } VM_NEXT;
        VM_LABEL(SLICE) {
            Value tv=base[GET_B(ins)], sv=base[GET_C(ins)], ev=base[GET_C(ins)+1];
            int len;
            if(tv.t==LT_LIST) len=AS_TAB(tv)->alen;
            else if(tv.t==LT_STR) len=AS_STR(tv)->len;
            else { luc_error("cannot slice a %s value",type_name(tv)); len=0; }
            int st,en;
            if(sv.t==LT_NIL) st=0;
            else if(sv.t==LT_NUM){ st=(int)floor(sv.u.n); if(st<0) st+=len; }
            else { luc_error("slice indices must be numbers"); st=0; }
            if(ev.t==LT_NIL) en=len;
            else if(ev.t==LT_NUM){ en=(int)floor(ev.u.n); if(en<0) en+=len; }
            else { luc_error("slice indices must be numbers"); en=len; }
            if(st<0) st=0; if(st>len) st=len;
            if(en<0) en=0; if(en>len) en=len;
            if(en<st) en=st;
            if(tv.t==LT_LIST){
                if(V.nalloc>V.gcthresh){ ci->savedpc=pc; gc_collect(); }
                Table *r=tab_new(1);
                for(int i=st;i<en;i++) list_push(r,AS_TAB(tv)->arr[i]);
                base[A]=mkobj(LT_LIST,r);
            } else {
                Str *s=AS_STR(tv);
                base[A]=strv(s->s+st,en-st);
            }
        } VM_NEXT;
        VM_LABEL(NEXT) {
/* NEXT: in target/state, out value/index or key/value */
            Value tv=base[A], st=base[A+1];
            if(tv.t==LT_LIST||tv.t==LT_STR){
                int len=tv.t==LT_LIST? AS_TAB(tv)->alen : AS_STR(tv)->len;
                int idx;
                if(st.t==LT_NIL) idx=0;
                else if(st.t==LT_NUM) idx=(int)st.u.n+1;
                else { luc_error("invalid iteration state"); idx=0; }
                if(idx<len){
                    base[A+1]=mknum((double)idx);
                    if(tv.t==LT_LIST) base[A+2]=AS_TAB(tv)->arr[idx];
                    else { Str *s=AS_STR(tv); base[A+2]=strv(s->s+idx,1); }
                    base[A+3]=mknum((double)idx);
                } else pc++;
            } else if(tv.t==LT_TABLE){
                Value k,v;
                if(tab_next(AS_TAB(tv),st,&k,&v)){
                    base[A+1]=k; base[A+2]=k; base[A+3]=v;
                } else pc++;
            } else luc_error("cannot iterate a %s value",type_name(tv));
        } VM_NEXT;
#if !defined(__GNUC__) && !defined(__clang__)
        default: luc_error("bad opcode %d",GET_OP(ins));
#endif
    }
#undef VM_LABEL
#undef VM_NEXT
}

/* Coroutines and scheduler */

int co_resume(LucState *co,Value *args,int nargs,Value *res,int *nres){
    if(co->status==CO_DEAD){ V.errval=mkobj(LT_STR,str_fromc("cannot resume dead coroutine")); return 1; }
    if(co->status==CO_RUNNING||co->status==CO_NORMAL){
        V.errval=mkobj(LT_STR,str_fromc("cannot resume non-suspended coroutine")); return 1; }
    LucState *prev=V.cur;
    volatile int rc=0;
    YieldPt yp; yp.prev=g_yp; yp.co=co; yp.cdepth=g_cdepth; g_yp=&yp;
    ErrJmp ej; ej.prev=V.errjmp; V.errjmp=&ej;
    co->resumer=prev;
    V.cur=co; co->status=CO_RUNNING;
    if(prev) prev->status=CO_NORMAL;
    if(setjmp(yp.jb)==0){
        if(setjmp(ej.jb)==0){
            if(co->status==CO_RUNNING && co->nci==0){
/* First start: func already at stack[0] */
                ensure_stack(co,nargs+8);
                for(int i=0;i<nargs;i++) co->stack[1+i]=args[i];
                Value f=co->stack[0];
                if(f.t==LT_FUNC){
                    pushframe(co,0,nargs,-1);
                    vm_execute(co,0);
                } else {
                    int n=vm_call(co,0,nargs,-1);
                    co->top=n;
                }
            } else {
                CallInfo *ci=&co->ci[co->nci-1];
                int dst=ci->base+co->yield_A;
                ensure_stack(co,dst+nargs+8);
                int want=co->yield_C;
                if(want<0){ for(int i=0;i<nargs;i++) co->stack[dst+i]=args[i]; co->top=dst+nargs; }
                else {
                    for(int i=0;i<want;i++) co->stack[dst+i]= i<nargs? args[i] : NIL;
                    co->top=ci->base+ci->cl->p->maxstack;
                }
                vm_execute(co,0);
            }
            co->status=CO_DEAD;
            int n=co->top; if(n>32) n=32;
            for(int i=0;i<n;i++) res[i]=co->stack[i];
            *nres=n; rc=0;
        } else { co->status=CO_DEAD; rc=1; }
    } else {
        co->status=CO_SUSPENDED;
        int n=co->nyield; if(n>32) n=32;
        for(int i=0;i<n;i++) res[i]=co->stack[co->yieldbase+i];
        *nres=n; rc=0;
    }
    g_yp=yp.prev; V.errjmp=ej.prev;
    V.cur=prev; if(prev) prev->status=CO_RUNNING;
    return rc;
}

/* 13. task scheduler */

void sched_add(LucState *co,double wake){
    if(V.nsched==V.schedcap){
        V.schedcap=V.schedcap?V.schedcap*2:8;
        V.sched=(SchedEntry*)lrealloc(V.sched,sizeof(SchedEntry)*(size_t)V.schedcap);
    }
    V.sched[V.nsched].co=co; V.sched[V.nsched].wake=wake; V.nsched++;
    co->scheduled=1;
}
void sched_remove(int i){
    V.sched[i].co->scheduled=0;
    V.sched[i]=V.sched[--V.nsched];
}
void sched_run(void){
    Value res[32]; int nres;
    while(V.nsched>0){
        int best=0;
        for(int i=1;i<V.nsched;i++) if(V.sched[i].wake<V.sched[best].wake) best=i;
        LucState *co=V.sched[best].co;
        double wake=V.sched[best].wake;
        sched_remove(best);
        if(co->status==CO_DEAD) continue;
        double now=luc_now();
        if(wake>now) luc_sleep(wake-now);
        double elapsed=luc_now()-(wake-co->waketime);
        Value arg=mknum(elapsed>0?elapsed:0);
        if(co_resume(co,&arg,1,res,&nres)){
            Str *s=tostr(V.errval);
            fprintf(stderr,"luc: error in task: %s\n",s->s);
        }
    }
}
/* Run due tasks only, no sleep; lets top-level wait pump tasks */
void sched_poll(void){
    Value res[32]; int nres;
    double now=luc_now();
    for(int i=0;i<V.nsched;){
        if(V.sched[i].wake>now){ i++; continue; }
        LucState *co=V.sched[i].co;
        double wake=V.sched[i].wake;
        sched_remove(i);
        if(co->status==CO_DEAD) continue;
        double elapsed=now-(wake-co->waketime);
        Value arg=mknum(elapsed>0?elapsed:0);
        if(co_resume(co,&arg,1,res,&nres)){
            Str *s=tostr(V.errval);
            fprintf(stderr,"luc: error in task: %s\n",s->s);
        }
        now=luc_now();
    }
}


const char *const HEXD="0123456789abcdef";

int hexval(int c){
    if(c>='0'&&c<='9') return c-'0';
    if(c>='a'&&c<='f') return c-'a'+10;
    if(c>='A'&&c<='F') return c-'A'+10;
    return -1;
}

/* Arg check helpers */

double checknum(LucState *L,int base,int nargs,int i,const char *fn){
    Value v=AR(i);
    if(v.t==LT_NUM) return v.u.n;
    if(v.t==LT_STR){ double d; if(str2num(AS_STR(v)->s,AS_STR(v)->len,&d)) return d; }
    luc_error("bad argument #%d to '%s' (number expected, got %s)",i+1,fn,type_name(v));
    return 0;
}
int checkint(LucState *L,int base,int nargs,int i,const char *fn){
    return (int)checknum(L,base,nargs,i,fn);
}
Str *checkstr(LucState *L,int base,int nargs,int i,const char *fn){
    Value v=AR(i);
    if(v.t==LT_STR) return AS_STR(v);
    if(v.t==LT_NUM) return tostr(v);
    luc_error("bad argument #%d to '%s' (string expected, got %s)",i+1,fn,type_name(v));
    return NULL;
}
Table *checktab(LucState *L,int base,int nargs,int i,const char *fn){
    Value v=AR(i);
    if(v.t==LT_TABLE||v.t==LT_LIST) return AS_TAB(v);
    luc_error("bad argument #%d to '%s' (table expected, got %s)",i+1,fn,type_name(v));
    return NULL;
}
Buffer *checkbuf(LucState *L,int base,int nargs,int i,const char *fn){
    Value v=AR(i);
    if(v.t==LT_BUFFER) return AS_BUF(v);
    luc_error("bad argument #%d to '%s' (buffer expected, got %s)",i+1,fn,type_name(v));
    return NULL;
}
uint32_t checku32(LucState *L,int base,int nargs,int i,const char *fn){
    double d=checknum(L,base,nargs,i,fn);
    return (uint32_t)(int64_t)d;
}
/* module system */

static char g_scriptdir[1024] = "";
static char g_exepath[1024]   = "";

static void set_scriptdir(const char *path){
    size_t n=strlen(path);
    while(n>0 && path[n-1]!='/' && path[n-1]!='\\') n--;
    if(n>=sizeof g_scriptdir) n=sizeof g_scriptdir-1;
    memcpy(g_scriptdir,path,n); g_scriptdir[n]=0;
}

static void modname_to_path(const char *name,char *out,size_t cap){
    size_t i=0;
    for(;name[i] && i+1<cap;i++) out[i]=(name[i]=='.')?'/':name[i];
    out[i]=0;
}

static char *read_file(const char *path,int *outlen);

static char *try_dir(const char *dir,const char *rel,int *len,char *found,size_t fcap){
    char p[1024]; char *src;
    char sep=(dir[0] && dir[strlen(dir)-1]!='/' && dir[strlen(dir)-1]!='\\')? '/' : 0;
    if(sep) snprintf(p,sizeof p,"%s/%s.luc",dir,rel);
    else    snprintf(p,sizeof p,"%s%s.luc",dir,rel);
    if((src=read_file(p,len))){ snprintf(found,fcap,"%s",p); return src; }
    if(sep) snprintf(p,sizeof p,"%s/%s/init.luc",dir,rel);
    else    snprintf(p,sizeof p,"%s%s/init.luc",dir,rel);
    if((src=read_file(p,len))){ snprintf(found,fcap,"%s",p); return src; }
    return NULL;
}

char *find_module(const char *name,int *len,char *found,size_t fcap){
    char rel[512]; modname_to_path(name,rel,sizeof rel);
    char *src;
    if(*g_scriptdir && (src=try_dir(g_scriptdir,rel,len,found,fcap))) return src;
/* Fallback: try CWD if scriptdir empty */
    if((src=try_dir(".",rel,len,found,fcap))) return src;
/* Local bundle first: ./luc_modules shadows LUC_PATH */
    if((src=try_dir("luc_modules",rel,len,found,fcap))) return src;
    const char *lp=getenv("LUC_PATH");
    if(lp){
#if defined(_WIN32)
        const char sepc=';';
#else
        const char sepc=':';
#endif
        char dir[512]; const char *p=lp;
        while(*p){
            const char *q=strchr(p,sepc); if(!q) q=p+strlen(p);
            size_t n=(size_t)(q-p); if(n>=sizeof dir) n=sizeof dir-1;
            memcpy(dir,p,n); dir[n]=0;
            if(n && (src=try_dir(dir,rel,len,found,fcap))) return src;
            p = *q? q+1 : q;
        }
    }
    return NULL;
}

/* System libs only from bundle/LUC_PATH; script/cwd can't shadow them */char *find_system_module(const char *name,int *len,char *found,size_t fcap){
    char rel[512]; modname_to_path(name,rel,sizeof rel);
    char *src;
    if((src=try_dir("luc_modules",rel,len,found,fcap))) return src;
    const char *lp=getenv("LUC_PATH");
    if(lp){
#if defined(_WIN32)
        const char sepc=';';
#else
        const char sepc=':';
#endif
        char dir[512]; const char *p=lp;
        while(*p){
            const char *q=strchr(p,sepc); if(!q) q=p+strlen(p);
            size_t n=(size_t)(q-p); if(n>=sizeof dir) n=sizeof dir-1;
            memcpy(dir,p,n); dir[n]=0;
            if(n && (src=try_dir(dir,rel,len,found,fcap))) return src;
            p = *q? q+1 : q;
        }
    }
    return NULL;
}

/* Libs live by main script; packs land next to it */
const char *luc_scriptdir(void){ return g_scriptdir; }

/* First real line: 1 with `make <name>` if lib part */
int part_make_name(const char *path,char *out,size_t cap){
    FILE *f=fopen(path,"rb");
    if(!f) return 0;
    char line[512]; int ok=0;
    while(fgets(line,sizeof line,f)){
        char *p=line;
        while(*p==' '||*p=='\t'||*p=='\r'||*p=='\n') p++;
        if(!*p || (*p=='-' && p[1]=='-')) continue;
        if(*p=='!') continue;   /* !strict / !karatsuba may precede make */
        /* `import libs` precedes make: skip while scanning */
        if(!strncmp(p,"import",6) && (p[6]==' '||p[6]=='\t')){
            char *q=p+6; while(*q==' '||*q=='\t') q++;
            if(!strncmp(q,"libs",4) && (q[4]==' '||q[4]=='\t'||q[4]=='\r'||q[4]=='\n'||q[4]=='('||q[4]==0||q[4]==';'))
                continue;
        }
        if(!strncmp(p,"make",4) && (p[4]==' '||p[4]=='\t')){
            p+=4; while(*p==' '||*p=='\t') p++;
            size_t n=0;
            while(p[n] && (isalnum((unsigned char)p[n])||p[n]=='_')) n++;
            if(n && n<cap){ memcpy(out,p,n); out[n]=0; ok=1; }
        }
        break;  /* only the first meaningful line counts */
    }
    fclose(f);
    return ok;
}

static char *try_file(const char *dir,const char *filename,int *len,char *found,size_t fcap){
    char p[1024]; char *src;
    char sep=(dir[0] && dir[strlen(dir)-1]!='/' && dir[strlen(dir)-1]!='\\')? '/' : 0;
    if(sep) snprintf(p,sizeof p,"%s/%s",dir,filename);
    else    snprintf(p,sizeof p,"%s%s",dir,filename);
    if((src=read_file(p,len))){ snprintf(found,fcap,"%s",p); return src; }
    return NULL;
}

/* Find packed <name>.luic in script/cwd/bundle/PATH */
char *find_pack(const char *name,int *len,char *found,size_t fcap){
    char rel[512];
    snprintf(rel,sizeof rel,"%s.luic",name);
    char *src;
    if(*g_scriptdir && (src=try_file(g_scriptdir,rel,len,found,fcap))) return src;
    if((src=try_file(".",rel,len,found,fcap))) return src;
    if((src=try_file("luc_modules",rel,len,found,fcap))) return src;
    const char *lp=getenv("LUC_PATH");
    if(lp){
#if defined(_WIN32)
        const char sepc=';';
#else
        const char sepc=':';
#endif
        char dir[512]; const char *p=lp;
        while(*p){
            const char *q=strchr(p,sepc); if(!q) q=p+strlen(p);
            size_t n=(size_t)(q-p); if(n>=sizeof dir) n=sizeof dir-1;
            memcpy(dir,p,n); dir[n]=0;
            if(n && (src=try_file(dir,rel,len,found,fcap))) return src;
            p = *q? q+1 : q;
        }
    }
    return NULL;
}

/* library registration */

void reg(Table *t,const char *name,CFn fn){
    tab_set(t,cstrv(name),mkobj(LT_CFUNC,cfunc_new(fn,name,0)));
}
Table *newlib(const char *name){
    Table *t=tab_new(0);
    tab_set(V.globals,cstrv(name),mkobj(LT_TABLE,t));
    return t;
}

/* Std libs in luc_lib_*.c, one open fn each */
static void luc_openlibs(void){
    lucL_open_base();
    lucL_open_string();
    lucL_open_list();
    lucL_open_math();
    lucL_open_os();
    lucL_open_io();
    lucL_open_buffer();
    lucL_open_coro();
    lucL_open_net();
/* Core additions on libs */
    V.listcore=tab_new(0);
    reg(V.listcore,"remove",f_core_remove);       /* By value, returns bool */
    V.tabmeta=tab_new(0);
    append_cache_init();
    reg(V.tabmeta,"keys",f_dict_keys);
    reg(V.tabmeta,"values",f_dict_values);
    tab_set(V.globals,cstrv("len"),mkobj(LT_CFUNC,cfunc_new(f_core_len,"len",0)));
    tab_set(V.globals,cstrv("pairs"), mkobj(LT_CFUNC,cfunc_new(f_lua_trap,"pairs",0)));
    tab_set(V.globals,cstrv("ipairs"),mkobj(LT_CFUNC,cfunc_new(f_lua_trap,"ipairs",0)));
}

/* init + driver */

static void luc_init(void){
    memset(&V,0,sizeof V);
    V.strcap=256;
    V.strtab=(Str**)lcalloc(sizeof(Str*)*(size_t)V.strcap);
    V.gcthresh=1u<<16;
    V.gcoff=1;                               /* No GC while bootstrapping */
    V.globals=tab_new(0);
    V.loaded=tab_new(0);                     /* Module cache */
    V.mainco=state_new(256);
    V.mainco->status=CO_RUNNING;
    V.cur=V.mainco;
    luc_openlibs();
    V.gcoff=0;
}

static int run_chunk(const char *src,int len,const char *name,int argc,char **argv,int firstarg){
    ErrJmp ej; ej.prev=V.errjmp; V.errjmp=&ej;
    if(setjmp(ej.jb)==0){
        Closure *cl=luc_compile(src,len,name);
        LucState *L=V.mainco;
        int n=(argc>firstarg)? argc-firstarg : 0;
        ensure_stack(L,n+64);
        L->top=0;
        L->stack[0]=mkobj(LT_FUNC,cl);
        for(int i=0;i<n;i++) L->stack[1+i]=cstrv(argv[firstarg+i]);
        V.cur=L;
        vm_call(L,0,n,0);
        sched_run();                      /* Drain task.delay/wait */
        fflush(stdout);
        V.errjmp=ej.prev;
        return 0;
    }
    V.errjmp=ej.prev;
    fflush(stdout);
    Str *msg=tostr(V.errval);
    fprintf(stderr,"luc: %s\n",msg->s);
    return 1;
}

static char *read_file(const char *path,int *outlen){
    FILE *f=fopen(path,"rb");
    if(!f) return NULL;
    fseek(f,0,SEEK_END);
    long sz=ftell(f);
    fseek(f,0,SEEK_SET);
    if(sz<0){ fclose(f); return NULL; }
    char *b=(char*)lmalloc((size_t)sz+1);
    size_t n=fread(b,1,(size_t)sz,f);
    fclose(f);
    b[n]=0;
    *outlen=(int)n;
    return b;
}

/* Package installer */

/* Packages fetch raw from repo main; LUC_INSTALL_URL overrides */
#define LUC_REPO_URL "https://raw.githubusercontent.com/hsusulist/luc/main"

typedef struct { const char *name; const char *desc; } PkgInfo;
static const PkgInfo PKGS[]={
    {"window","SDL2 window support (2D graphics, PNG/JPG sprites, TTF text, WAV/OGG/MP3 sound)"},
    {"ai",    "lanternl AI library - import ai (tensor, nn, tokenizer)"},
    {"discord","Discord bot library - import discord (needs a bot token)"},
};

static const char *pkg_base_url(void){
    const char *e=getenv("LUC_INSTALL_URL");
    return (e && *e)? e : LUC_REPO_URL;
}

static void pkg_join_url(char *out,size_t cap,const char *rel){
    const char *b=pkg_base_url();
    size_t bl=strlen(b);
    if(bl && b[bl-1]=='/') snprintf(out,cap,"%s%s",b,rel);
    else                  snprintf(out,cap,"%s/%s",b,rel);
}

static int pkg_file_exists(const char *p){
    FILE *f=fopen(p,"rb");
    if(f) fclose(f);
    return f!=NULL;
}

static int pkg_mkdir(const char *path){
#if defined(_WIN32)
    if(CreateDirectoryA(path,NULL)) return 0;
    return GetLastError()==ERROR_ALREADY_EXISTS? 0 : -1;
#else
    return mkdir(path,0777);      /* EEXIST is fine too */
#endif
}

/* Install root: folder with luc binary */
static int pkg_exe_dir(char *out,size_t cap){
#if defined(_WIN32)
    DWORD n=GetModuleFileNameA(NULL,out,(DWORD)cap);
    if(n==0||n>=cap) return 0;
#else
    ssize_t n=readlink("/proc/self/exe",out,cap-1);
    if(n<=0){
        if(!*g_exepath) return 0;
        snprintf(out,cap,"%s",g_exepath);
    } else out[n]=0;
#endif
    char *p=strrchr(out,'/');
    char *q=strrchr(out,'\\');
    if(q && q>p) p=q;
    if(!p){ snprintf(out,cap,"."); return 1; }
    *p=0;
    return 1;
}

static void pkg_fmt_size(long bytes,char *out,size_t cap){
    if(bytes>=1024*1024) snprintf(out,cap,"%.1f MB",(double)bytes/(1024.0*1024.0));
    else if(bytes>=1024) snprintf(out,cap,"%.1f KB",(double)bytes/1024.0);
    else snprintf(out,cap,"%ld B",bytes);
}

#if defined(_WIN32)

/* GET url via WinHTTP, follows redirects */
static int pkg_http_get(const char *url,const char *outpath,long *outsize){
    const char *p=url;
    int secure=1;
    if(!strncmp(p,"https://",8)){ secure=1; p+=8; }
    else if(!strncmp(p,"http://",7)){ secure=0; p+=7; }
    else return 0;
    char host[256]; const char *slash=strchr(p,'/');
    if(slash){
        if((size_t)(slash-p)>=sizeof host) return 0;
        memcpy(host,p,slash-p); host[slash-p]=0;
    } else { snprintf(host,sizeof host,"%s",p); slash="/"; }
    wchar_t whost[256], wpath[1024];
    if(!MultiByteToWideChar(CP_UTF8,0,host,-1,whost,256)) return 0;
    if(!MultiByteToWideChar(CP_UTF8,0,slash,-1,wpath,1024)) return 0;
    int ok=0;
    HINTERNET hs=WinHttpOpen(L"luc-install",WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
                             WINHTTP_NO_PROXY_NAME,WINHTTP_NO_PROXY_BYPASS,0);
    if(!hs) return 0;
    WinHttpSetTimeouts(hs,10000,10000,10000,60000);
    HINTERNET hc=WinHttpConnect(hs,whost,secure?INTERNET_DEFAULT_HTTPS_PORT:INTERNET_DEFAULT_HTTP_PORT,0);
    if(hc){
        HINTERNET hr=WinHttpOpenRequest(hc,L"GET",wpath,NULL,WINHTTP_NO_REFERER,
                                        WINHTTP_DEFAULT_ACCEPT_TYPES,
                                        secure?WINHTTP_FLAG_SECURE:0);
        if(hr){
            DWORD pol=WINHTTP_OPTION_REDIRECT_POLICY_ALWAYS;
            WinHttpSetOption(hr,WINHTTP_OPTION_REDIRECT_POLICY,&pol,sizeof pol);
            if(WinHttpSendRequest(hr,WINHTTP_NO_ADDITIONAL_HEADERS,0,
                                  WINHTTP_NO_REQUEST_DATA,0,0,0) &&
               WinHttpReceiveResponse(hr,NULL)){
                DWORD status=0, dsz=sizeof status;
                WinHttpQueryHeaders(hr,WINHTTP_QUERY_STATUS_CODE|WINHTTP_QUERY_FLAG_NUMBER,
                                    WINHTTP_HEADER_NAME_BY_INDEX,&status,&dsz,
                                    WINHTTP_NO_HEADER_INDEX);
                if(status==200){
                    FILE *f=fopen(outpath,"wb");
                    if(f){
                        ok=1;
                        for(;;){
                            DWORD avail=0, rd=0;
                            if(!WinHttpQueryDataAvailable(hr,&avail)||!avail) break;
                            char *buf=(char*)malloc(avail);
                            if(!buf){ ok=0; break; }
                            if(WinHttpReadData(hr,buf,avail,&rd)&&rd){
                                if(fwrite(buf,1,rd,f)!=rd){ ok=0; free(buf); break; }
                                if(outsize) *outsize+=rd;
                            }
                            free(buf);
                            if(!rd) break;
                        }
                        fclose(f);
                        if(!ok) remove(outpath);
                    }
                }
            }
            WinHttpCloseHandle(hr);
        }
        WinHttpCloseHandle(hc);
    }
    WinHttpCloseHandle(hs);
    return ok;
}

#else

/* POSIX: curl handles TLS and redirects */
static int pkg_http_get(const char *url,const char *outpath,long *outsize){
    char cmd[1600];
    snprintf(cmd,sizeof cmd,"curl -fL --connect-timeout 10 -s -o '%s' '%s'",outpath,url);
    if(system(cmd)!=0){ remove(outpath); return 0; }
    if(outsize){
        FILE *f=fopen(outpath,"rb");
        if(f){ fseek(f,0,SEEK_END); *outsize=ftell(f); fclose(f); }
    }
    return 1;
}

#endif

static int pkg_has_pe_magic(const char *path){
    FILE *f=fopen(path,"rb");
    if(!f) return 0;
    int ok=fgetc(f)=='M' && fgetc(f)=='Z';
    fclose(f);
    return ok;
}

static void pkg_install_path(char *out,size_t cap,const char *sub){
    char dir[1024];
    if(!pkg_exe_dir(dir,sizeof dir)) snprintf(dir,sizeof dir,".");
    snprintf(out,cap,"%s/%s",dir,sub);
}

/* Swap tmp into place; running exe renamed to .old first on Windows */
static int pkg_put_file(const char *tmp,const char *dest){
#if defined(_WIN32)
    if(!MoveFileExA(tmp,dest,MOVEFILE_REPLACE_EXISTING)) return 0;
    return 1;
#else
    return rename(tmp,dest)==0;
#endif
}

static int pkg_install_window(int force){
    char dll[1200],dlltmp[1250],newexe[1250],exetmp[1300],oldexe[1250],url[1200];
    char pd[1200],pdll[1250],pexe[1250];
    long sz=0;
    int offline;
    pkg_install_path(dll,sizeof dll,"SDL2.dll");
    if(!force && pkg_file_exists(dll)){
        printf("window support is already installed (%s).\n",dll);
        printf("use 'luc install window --force' to download it again.\n");
        return 0;
    }
    snprintf(dlltmp,sizeof dlltmp,"%s.tmp",dll);
    pkg_install_path(newexe,sizeof newexe,"luc-new.exe");
    snprintf(exetmp,sizeof exetmp,"%s.tmp",newexe);
    pkg_install_path(oldexe,sizeof oldexe,"luc.exe.old");
    remove(oldexe);                       /* Sweep stale backup */

    /* Bundled packages/window next to exe: try first */
    pkg_install_path(pd,sizeof pd,"packages/window");
    snprintf(pdll,sizeof pdll,"%s/SDL2.dll",pd);
    snprintf(pexe,sizeof pexe,"%s/luc-win.exe",pd);
    offline=!force && pkg_file_exists(pdll) && pkg_file_exists(pexe);
    if(offline){
        printf("using bundled package: %s\n",pd);
        snprintf(dlltmp,sizeof dlltmp,"%s",pdll);
        snprintf(exetmp,sizeof exetmp,"%s",pexe);
    } else {
        pkg_join_url(url,sizeof url,"dist/app/SDL2.dll");
        printf("downloading %s\n",url);
        if(!pkg_http_get(url,dlltmp,&sz)||!pkg_has_pe_magic(dlltmp)){
            fprintf(stderr,"luc install: download failed for SDL2.dll\n");
            remove(dlltmp); return 1;
        }
        char hs[32]; pkg_fmt_size(sz,hs,sizeof hs);
        printf("  SDL2.dll  %s\n",hs);

        pkg_join_url(url,sizeof url,"dist/app/luc-win.exe");
        printf("downloading %s\n",url);
        sz=0;
        if(!pkg_http_get(url,exetmp,&sz)||!pkg_has_pe_magic(exetmp)){
            fprintf(stderr,"luc install: download failed for luc-win.exe\n");
            remove(exetmp); remove(dlltmp); return 1;
        }
        pkg_fmt_size(sz,hs,sizeof hs);
        printf("  luc-win.exe  %s\n",hs);
    }

    if(!pkg_put_file(dlltmp,dll)){
        fprintf(stderr,"luc install: cannot replace SDL2.dll (close other LUC programs)\n");
        if(!offline){ remove(dlltmp); remove(exetmp); }
        return 1;
    }
    char exe[1200]; pkg_install_path(exe,sizeof exe,"luc.exe");
    int moved_old=0;
    if(pkg_file_exists(exe)){
#if defined(_WIN32)
        moved_old=MoveFileExA(exe,oldexe,MOVEFILE_REPLACE_EXISTING)? 1:0;
#else
        remove(oldexe);
        moved_old=(rename(exe,oldexe)==0);
#endif
    }
    if(!pkg_put_file(exetmp,exe)){
        fprintf(stderr,"luc install: cannot replace luc.exe (close other LUC programs)\n");
        if(!offline) remove(exetmp);
        return 1;
    }
    /* Media extras: missing files only degrade features, warn only */
    { static const char *extra[]={
        "SDL2_ttf.dll","SDL2_image.dll","SDL2_mixer.dll",
        "libfreetype-6.dll","libharfbuzz-0.dll","libbz2-1.dll",
        "libpng16-16.dll","zlib1.dll","libbrotlidec.dll","libbrotlicommon.dll",
        "libgraphite2.dll","libglib-2.0-0.dll","libintl-8.dll","libpcre2-8-0.dll",
        "libiconv-2.dll","libjpeg-8.dll","libmpg123-0.dll","libogg-0.dll",
        "libopus-0.dll","libopusfile-0.dll","libvorbis-0.dll","libvorbisfile-3.dll",
        "libFLAC.dll","libgcc_s_seh-1.dll","libstdc++-6.dll","libwinpthread-1.dll",
        "DejaVuSans.ttf",NULL
    };
      char dest[1200],src[1300],u[1200]; long s2=0;
      char exedir[1200];
      if(!pkg_exe_dir(exedir,sizeof exedir)) snprintf(exedir,sizeof exedir,".");
      for(int k=0;extra[k];k++){
        snprintf(dest,sizeof dest,"%s/%s",exedir,extra[k]);
        if(!force && pkg_file_exists(dest)) continue;
        snprintf(src,sizeof src,"%s/%s",pd,extra[k]);
        if(offline && pkg_file_exists(src)){
            if(!pkg_put_file(src,dest))
                printf("luc install: warning: cannot install %s (file in use?)\n",extra[k]);
            else printf("  %s\n",extra[k]);
        } else if(!offline){
            pkg_join_url(u,sizeof u,"dist/app/");
            strncat(u,extra[k],sizeof u-strlen(u)-1);
            printf("downloading %s\n",u);
            s2=0;
            snprintf(src,sizeof src,"%s.tmp",dest);
            if(!pkg_http_get(u,src,&s2)){ remove(src);
                printf("luc install: warning: download failed for %s (skipped)\n",extra[k]);
                continue; }
            if(!pkg_put_file(src,dest)){
                printf("luc install: warning: cannot install %s (file in use?)\n",extra[k]);
                remove(src); continue;
            }
            { char hs[32]; pkg_fmt_size(s2,hs,sizeof hs); printf("  %s  %s\n",extra[k],hs); }
        }
      } }
    printf("installed window support.\n");
    if(moved_old) printf("note: the old interpreter is kept as luc.exe.old (safe to delete).\n");
    printf("try: luc demos/window_libaries/pong.luc\n");
    return 0;
}

/* Unpack bundle: '#=lucfile:' switches files, CRLF normalized */
static int pkg_unpack_bundle(const char *path,const char *outdir){
    int len; char *src=read_file(path,&len);
    if(!src) return -1;
    FILE *cur=NULL;
    int count=0;
    size_t i=0;
    while(i<(size_t)len){
        char *line=src+i;
        char *nl=(char*)memchr(line,'\n',(size_t)len-i);
        size_t ll=nl? (size_t)(nl-line) : (size_t)len-i;
        size_t wl=ll;                          /* write length w/o CR */
        if(wl>0 && line[wl-1]=='\r') wl--;
        if(wl>=11 && !strncmp(line,"#=lucfile: ",11)){
            if(cur){ fclose(cur); cur=NULL; }
            char name[256];
            size_t n2=wl-11;
            if(n2>=sizeof name) n2=sizeof name-1;
            memcpy(name,line+11,n2); name[n2]=0;
            char out[1400];
            snprintf(out,sizeof out,"%s/%s",outdir,name);
            cur=fopen(out,"wb");
            if(cur) count++;
        } else if(wl>=8 && !strncmp(line,"#=endpkg",8)){
            if(cur){ fclose(cur); cur=NULL; }
        } else if(wl>=9 && !strncmp(line,"#=lucpkg:",9)){
/* header, ignore */
        } else if(cur){
            fwrite(line,1,wl,cur);
            fputc('\n',cur);
        }
        i+=ll+(nl?1:0);
    }
    if(cur) fclose(cur);
    free(src);
    return count;
}

static int pkg_install_ai(void){
    char mods[1200],tmp[1300],url[1200],pkg[1300];
    long sz=0;
    int offline;
    pkg_install_path(mods,sizeof mods,"luc_modules");
    pkg_mkdir(mods);
    /* Bundled packages/ai.lucpkg next to exe: try first */
    pkg_install_path(pkg,sizeof pkg,"packages/ai.lucpkg");
    offline=pkg_file_exists(pkg);
    if(offline){
        printf("using bundled package: %s\n",pkg);
        snprintf(tmp,sizeof tmp,"%s",pkg);
    } else {
        snprintf(tmp,sizeof tmp,"%s/ai.lucpkg.tmp",mods);
        pkg_join_url(url,sizeof url,"packages/ai.lucpkg");
        printf("downloading %s\n",url);
        if(!pkg_http_get(url,tmp,&sz)){
            fprintf(stderr,"luc install: download failed (no internet, and no\n");
            fprintf(stderr,"  packages/ai.lucpkg folder next to luc.exe)\n");
            remove(tmp); return 1;
        }
        char hs[32]; pkg_fmt_size(sz,hs,sizeof hs);
        printf("  ai.lucpkg  %s\n",hs);
    }
    int n=pkg_unpack_bundle(tmp,mods);
    if(!offline) remove(tmp);
    if(n<=0){
        fprintf(stderr,"luc install: package file is empty or invalid\n");
        return 1;
    }
    printf("installed %d modules -> %s\n",n,mods);
    printf("try: luc -e \"import ai print(ai.Tensor)\"\n");
    return 0;
}

static int pkg_install_discord(void){
    char mods[1200],tmp[1300],url[1200],pkg[1300];
    long sz=0;
    int offline;
    pkg_install_path(mods,sizeof mods,"luc_modules");
    pkg_mkdir(mods);
    /* Bundled packages/discord.lucpkg next to exe: try first */
    pkg_install_path(pkg,sizeof pkg,"packages/discord.lucpkg");
    offline=pkg_file_exists(pkg);
    if(offline){
        printf("using bundled package: %s\n",pkg);
        snprintf(tmp,sizeof tmp,"%s",pkg);
    } else {
        snprintf(tmp,sizeof tmp,"%s/discord.lucpkg.tmp",mods);
        pkg_join_url(url,sizeof url,"packages/discord.lucpkg");
        printf("downloading %s\n",url);
        if(!pkg_http_get(url,tmp,&sz)){
            fprintf(stderr,"luc install: download failed (no internet, and no\n");
            fprintf(stderr,"  packages/discord.lucpkg folder next to luc.exe)\n");
            remove(tmp); return 1;
        }
        char hs[32]; pkg_fmt_size(sz,hs,sizeof hs);
        printf("  discord.lucpkg  %s\n",hs);
    }
    int n=pkg_unpack_bundle(tmp,mods);
    if(!offline) remove(tmp);
    if(n<=0){
        fprintf(stderr,"luc install: package file is empty or invalid\n");
        return 1;
    }
    printf("installed %d modules -> %s\n",n,mods);
    printf("get a bot token at https://discord.com/developers/applications\n");
    return 0;
}

static int cmd_install(int argc,char **argv){
    if(argc<3){
        char dir[1024];
        int have=pkg_exe_dir(dir,sizeof dir);
        printf("LUC package installer  (source: %s)\n",pkg_base_url());
        printf("  the bundled packages folder (from the installer) is used first\n");
        for(size_t k=0;k<sizeof PKGS/sizeof PKGS[0];k++){
            char p[1200];
            int inst=0;
            if(have){
                if(!strcmp(PKGS[k].name,"window")) snprintf(p,sizeof p,"%s/SDL2.dll",dir);
                else if(!strcmp(PKGS[k].name,"ai")) snprintf(p,sizeof p,"%s/luc_modules/ai.luc",dir);
                else snprintf(p,sizeof p,"%s/luc_modules/discord.luc",dir);
                inst=pkg_file_exists(p);
            }
            printf("  %-8s %-58s [%s]\n",PKGS[k].name,PKGS[k].desc,inst?"installed":"not installed");
        }
        printf("\nusage: luc install <name>            install a package (window, ai, discord)\n");
        printf("       luc install window --force    redownload window support\n");
        return 0;
    }
    const char *name=argv[2];
    int force=(argc>3 && (!strcmp(argv[3],"--force")||!strcmp(argv[3],"-f")));
    if(!strcmp(name,"window")) return pkg_install_window(force);
    if(!strcmp(name,"ai"))     return pkg_install_ai();
    if(!strcmp(name,"discord")) return pkg_install_discord();
    fprintf(stderr,"luc install: unknown package '%s' (available: window, ai, discord)\n",name);
    return 1;
}

static void print_banner_tail(void){
    printf(
"\n"
"  luc <file>      run a LUC script\n"
"  luc -e \"...\"    run code from the command line\n"
"  luc install     packages (window, ai, discord)\n"
"  luc --help      full help\n");
}

/* ASCII banner synced with ascii2.txt; plain ASCII for all fonts */
static void print_ascii_banner(void){
    printf(
"██╗     ██╗   ██╗ ██████╗  Version: 0.2 beta 1\n"
"██║     ██║   ██║██╔════╝  License: Apache\n"
"██║     ██║   ██║██║       Repo: https://github.com/hsusulist/luc\n"
"██║     ██║   ██║██║       Welcome to luc.\n"
"██████╗╚██████╔╝╚██████╗   Type \"help\", \"credits\" or \"license\" for more information.\n"
"╚══════╝ ╚═════╝  ╚═════╝  \n");
}

static void print_banner(void){
    printf("\n");   /* Prime line: some consoles mangle first bytes */
    /* ASCII default; braille opt-in via LUC_BRAILLE=1 */
    if(!getenv("LUC_BRAILLE")){ print_ascii_banner(); return; }
    printf(
"⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀\n"
"⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀\n"
"⠀⠀⠀⠀⠀⠀⣠⣶⣶⣿⣿⣿⣷⣶⣤⡀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀Luc 0.2 beta 1\n"
"⠀⠀⠀⠀⣠⣾⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣦\n"
"⠀⠀⠀⢠⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣧\n"
"⠀⠀⠀⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⡇\n"
"⠀⠀⠀⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⡇\n"
"⠀⠀⠀⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⠇\n"
"⠀⠀⠀⢻⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⡟\n"
"⠀⠀⠀⠀⠈⠿⣿⣿⣿⣿⣿⣿⣿⣿⣿⠿⠋⢠⣶⣶⣶⣶⣶⣄\n"
"⠀⠀⠀⠀⠀⠀⠈⠛⠿⠿⠿⠿⠿⠿⠛⠁ ⣾⣿⣿⣿⣿⣿⣿⣿⣧\n"
"⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀   ⣿⣿⣿⣿⣿⣿⣿⣿⣿\n"
"⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀  ⠀ ⢿⣿⣿⣿⣿⣿⣿⣿⡿\n"
"⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀   ⠻⢿⣿⣿⣿⡿⠟\n"
"⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀\n");
    print_banner_tail();
}

static void print_help(void){
    printf(
    "LUC %s\n\n"
    "usage: luc [options] [script [args...]]\n\n"
    "  script.luc          run a LUC source file\n"
    "  -e \"chunk\"          execute LUC code from the command line\n"
    "  lcode [path] / --lcode [path]  open the LCode editor\n"
    "  --edit [path]       same as --lcode (legacy alias)\n"
    "  install [pkg]       list or install a package  (window, ai, discord)\n"
    "  -v, --version       print version and exit\n"
    "  -h, --help          print this help and exit\n\n"
    "variables\n"
    "  create x = 10       local variable\n"
    "  x = 10              global variable (forbidden under !strict)\n"
    "  create function f() local function\n"
    "  command m()       procedure (no return, runs for effects)\n"
    "  run m(1, 2)       run it (bare: m 1, 2; block: m: ... end)\n\n"
    "directives (first line, each on its own line)\n"
    "  !strict             ultra-strict: 'create' required, dict keys quoted,\n"
    "                      bare 'function f()' forbidden, True/False/None rejected\n"
    "  !karatsuba          exact big-int '*' for integer strings in this chunk\n\n"
    "types\n"
    "  [1, 2, 3]           list  (0-based)\n"
    "  {key: value}        dict\n"
    "  \"text\" + num        string join\n"
    "  true  false  nil\n\n"
    "repeat\n"
    "  repeat 5 do                        5 times, i = 0..4\n"
    "  repeat 1, 10 as i do               i = 0..9  (stops before 10)\n"
    "  repeat 3, 10 as i do               i = 0, 3, 6, 9\n"
    "  repeat -1, 10 as i do              i = 10..0  (countdown)\n"
    "  repeat item, i in list do          iterate list\n"
    "  repeat k, v in dict do             iterate dict\n\n"
    "operators\n"
    "  == != < <= > >=     comparison\n"
    "  += -= *= /=         compound assignment\n"
    "  in  not in          membership\n"
    "  and  or  not        logic\n\n"
    "builtins\n"
    "  list.append / remove / sort\n"
    "  dict.keys() / values()\n"
    "  list[1:3]           slicing\n"
    "  tostring()  tonumber()  len()\n"
    "  string.split / trim / startswith / endswith / tohex / fromhex\n"
    "  io.replace / clearline / eraseline / clear\n"
    "  task.*   buffer.*   bit32.*\n\n"
    "modules\n"
    "  import window       system library (SDL2 graphics, build with -DLUC_WINDOW)\n"
    "  import window(\"w\")  same, bound to short name\n"
    "  import libs         user-library toolkit (unlocks make/get/pack/command)\n"
    "  require(\"name\")     third-party module from disk\n"
    "  make foo            this file is lib part foo (first line, auto-exports creates)\n"
    "  get a, b            load parts by make-name (locals)\n"
    "  pack mylib          bundle gets into mylib.luic\n"
    "  import x from lib   load part x (or symbol x) from lib.luic / lib.luc\n\n"
    "internals\n"
    "  lexer -> parser -> AST -> bytecode -> VM  (mark & sweep GC)\n"
    "  metatable-lite: __index __call __add..__pow __unm __tostring\n",
    LUC_VERSION);
}

/* GUI build: reattach parent console if no handles; double-click stays quiet */
#if defined(_WIN32)
static UINT g_saved_cp = 0;
static void luc_win_restore_cp(void){
    if(g_saved_cp) SetConsoleOutputCP(g_saved_cp);
}
/* UTF-8 console: avoid mojibake, restore codepage on exit */
static void luc_win_utf8_console(void){
    HANDLE out=GetStdHandle(STD_OUTPUT_HANDLE);
    DWORD mode=0;
    if(!out || out==INVALID_HANDLE_VALUE) return;
    if(!GetConsoleMode(out,&mode)) return;   /* Redirected: bytes already UTF-8 */
    g_saved_cp=GetConsoleOutputCP();
    SetConsoleOutputCP(65001);
    atexit(luc_win_restore_cp);
}
#ifndef ENABLE_VIRTUAL_TERMINAL_PROCESSING
#define ENABLE_VIRTUAL_TERMINAL_PROCESSING 0x0004u
#endif
static void luc_win_enable_vt(HANDLE out){
    DWORD mode=0;
    if(out && out!=INVALID_HANDLE_VALUE && GetConsoleMode(out,&mode))
        SetConsoleMode(out,mode|ENABLE_VIRTUAL_TERMINAL_PROCESSING);
}
static void luc_win_attach_console(void){
    HANDLE out=GetStdHandle(STD_OUTPUT_HANDLE);
    if(out && out!=INVALID_HANDLE_VALUE){        /* Console or redirected */
        luc_win_enable_vt(out);                  /* ANSI erase works */
        return;
    }
    if(!AttachConsole(ATTACH_PARENT_PROCESS)) return;   /* Double-click: quiet */
    freopen("CONOUT$","w",stdout);
    freopen("CONOUT$","w",stderr);
    freopen("CONIN$","r",stdin);
    SetConsoleOutputCP(65001);                   /* UTF-8 output */
    SetConsoleCP(65001);
    luc_win_enable_vt(GetStdHandle(STD_OUTPUT_HANDLE));
}
#else
#define luc_win_attach_console() ((void)0)
#endif

/* full Apache-2.0 text (root LICENCE) for the `license` help topic */
static const char LUC_LICENSE[] =
"                                 Apache License\n"
"                           Version 2.0, January 2004\n"
"                        http://www.apache.org/licenses/\n"
"\n"
"   TERMS AND CONDITIONS FOR USE, REPRODUCTION, AND DISTRIBUTION\n"
"\n"
"   1. Definitions.\n"
"\n"
"      \"License\" shall mean the terms and conditions for use, reproduction,\n"
"      and distribution as defined by Sections 1 through 9 of this document.\n"
"\n"
"      \"Licensor\" shall mean the copyright owner or entity authorized by\n"
"      the copyright owner that is granting the License.\n"
"\n"
"      \"Legal Entity\" shall mean the union of the acting entity and all\n"
"      other entities that control, are controlled by, or are under common\n"
"      control with that entity. For the purposes of this definition,\n"
"      \"control\" means (i) the power, direct or indirect, to cause the\n"
"      direction or management of such entity, whether by contract or\n"
"      otherwise, or (ii) ownership of fifty percent (50%) or more of the\n"
"      outstanding shares, or (iii) beneficial ownership of such entity.\n"
"\n"
"      \"You\" (or \"Your\") shall mean an individual or Legal Entity\n"
"      exercising permissions granted by this License.\n"
"\n"
"      \"Source\" form shall mean the preferred form for making modifications,\n"
"      including but not limited to software source code, documentation\n"
"      source, and configuration files.\n"
"\n"
"      \"Object\" form shall mean any form resulting from mechanical\n"
"      transformation or translation of a Source form, including but\n"
"      not limited to compiled object code, generated documentation,\n"
"      and conversions to other media types.\n"
"\n"
"      \"Work\" shall mean the work of authorship, whether in Source or\n"
"      Object form, made available under the License, as indicated by a\n"
"      copyright notice that is included in or attached to the work\n"
"      (an example is provided in the Appendix below).\n"
"\n"
"      \"Derivative Works\" shall mean any work, whether in Source or Object\n"
"      form, that is based on (or derived from) the Work and for which the\n"
"      editorial revisions, annotations, elaborations, or other modifications\n"
"      represent, as a whole, an original work of authorship. For the purposes\n"
"      of this License, Derivative Works shall not include works that remain\n"
"      separable from, or merely link (or bind by name) to the interfaces of,\n"
"      the Work and Derivative Works thereof.\n"
"\n"
"      \"Contribution\" shall mean any work of authorship, including\n"
"      the original version of the Work and any modifications or additions\n"
"      to that Work or Derivative Works thereof, that is intentionally\n"
"      submitted to Licensor for inclusion in the Work by the copyright owner\n"
"      or by an individual or Legal Entity authorized to submit on behalf of\n"
"      the copyright owner. For the purposes of this definition, \"submitted\"\n"
"      means any form of electronic, verbal, or written communication sent\n"
"      to the Licensor or its representatives, including but not limited to\n"
"      communication on electronic mailing lists, source code control systems,\n"
"      and issue tracking systems that are managed by, or on behalf of, the\n"
"      Licensor for the purpose of discussing and improving the Work, but\n"
"      excluding communication that is conspicuously marked or otherwise\n"
"      designated in writing by the copyright owner as \"Not a Contribution.\"\n"
"\n"
"      \"Contributor\" shall mean Licensor and any individual or Legal Entity\n"
"      on behalf of whom a Contribution has been received by Licensor and\n"
"      subsequently incorporated within the Work.\n"
"\n"
"   2. Grant of Copyright License. Subject to the terms and conditions of\n"
"      this License, each Contributor hereby grants to You a perpetual,\n"
"      worldwide, non-exclusive, no-charge, royalty-free, irrevocable\n"
"      copyright license to reproduce, prepare Derivative Works of,\n"
"      publicly display, publicly perform, sublicense, and distribute the\n"
"      Work and such Derivative Works in Source or Object form.\n"
"\n"
"   3. Grant of Patent License. Subject to the terms and conditions of\n"
"      this License, each Contributor hereby grants to You a perpetual,\n"
"      worldwide, non-exclusive, no-charge, royalty-free, irrevocable\n"
"      (except as stated in this section) patent license to make, have made,\n"
"      use, offer to sell, sell, import, and otherwise transfer the Work,\n"
"      where such license applies only to those patent claims licensable\n"
"      by such Contributor that are necessarily infringed by their\n"
"      Contribution(s) alone or by combination of their Contribution(s)\n"
"      with the Work to which such Contribution(s) was submitted. If You\n"
"      institute patent litigation against any entity (including a\n"
"      cross-claim or counterclaim in a lawsuit) alleging that the Work\n"
"      or a Contribution incorporated within the Work constitutes direct\n"
"      or contributory patent infringement, then any patent licenses\n"
"      granted to You under this License for that Work shall terminate\n"
"      as of the date such litigation is filed.\n"
"\n"
"   4. Redistribution. You may reproduce and distribute copies of the\n"
"      Work or Derivative Works thereof in any medium, with or without\n"
"      modifications, and in Source or Object form, provided that You\n"
"      meet the following conditions:\n"
"\n"
"      (a) You must give any other recipients of the Work or\n"
"          Derivative Works a copy of this License; and\n"
"\n"
"      (b) You must cause any modified files to carry prominent notices\n"
"          stating that You changed the files; and\n"
"\n"
"      (c) You must retain, in the Source form of any Derivative Works\n"
"          that You distribute, all copyright, patent, trademark, and\n"
"          attribution notices from the Source form of the Work,\n"
"          excluding those notices that do not pertain to any part of\n"
"          the Derivative Works; and\n"
"\n"
"      (d) If the Work includes a \"NOTICE\" text file as part of its\n"
"          distribution, then any Derivative Works that You distribute must\n"
"          include a readable copy of the attribution notices contained\n"
"          within such NOTICE file, excluding those notices that do not\n"
"          pertain to any part of the Derivative Works, in at least one\n"
"          of the following places: within a NOTICE text file distributed\n"
"          as part of the Derivative Works; within the Source form or\n"
"          documentation, if provided along with the Derivative Works; or,\n"
"          within a display generated by the Derivative Works, if and\n"
"          wherever such third-party notices normally appear. The contents\n"
"          of the NOTICE file are for informational purposes only and\n"
"          do not modify the License. You may add Your own attribution\n"
"          notices within Derivative Works that You distribute, alongside\n"
"          or as an addendum to the NOTICE text from the Work, provided\n"
"          that such additional attribution notices cannot be construed\n"
"          as modifying the License.\n"
"\n"
"      You may add Your own copyright statement to Your modifications and\n"
"      may provide additional or different license terms and conditions\n"
"      for use, reproduction, or distribution of Your modifications, or\n"
"      for any such Derivative Works as a whole, provided Your use,\n"
"      reproduction, and distribution of the Work otherwise complies with\n"
"      the conditions stated in this License.\n"
"\n"
"   5. Submission of Contributions. Unless You explicitly state otherwise,\n"
"      any Contribution intentionally submitted for inclusion in the Work\n"
"      by You to the Licensor shall be under the terms and conditions of\n"
"      this License, without any additional terms or conditions.\n"
"      Notwithstanding the above, nothing herein shall supersede or modify\n"
"      the terms of any separate license agreement you may have executed\n"
"      with Licensor regarding such Contributions.\n"
"\n"
"   6. Trademarks. This License does not grant permission to use the trade\n"
"      names, trademarks, service marks, or product names of the Licensor,\n"
"      except as required for reasonable and customary use in describing the\n"
"      origin of the Work and reproducing the content of the NOTICE file.\n"
"\n"
"   7. Disclaimer of Warranty. Unless required by applicable law or\n"
"      agreed to in writing, Licensor provides the Work (and each\n"
"      Contributor provides its Contributions) on an \"AS IS\" BASIS,\n"
"      WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or\n"
"      implied, including, without limitation, any warranties or conditions\n"
"      of TITLE, NON-INFRINGEMENT, MERCHANTABILITY, or FITNESS FOR A\n"
"      PARTICULAR PURPOSE. You are solely responsible for determining the\n"
"      appropriateness of using or redistributing the Work and assume any\n"
"      risks associated with Your exercise of permissions under this License.\n"
"\n"
"   8. Limitation of Liability. In no event and under no legal theory,\n"
"      whether in tort (including negligence), contract, or otherwise,\n"
"      unless required by applicable law (such as deliberate and grossly\n"
"      negligent acts) or agreed to in writing, shall any Contributor be\n"
"      liable to You for damages, including any direct, indirect, special,\n"
"      incidental, or consequential damages of any character arising as a\n"
"      result of this License or out of the use or inability to use the\n"
"      Work (including but not limited to damages for loss of goodwill,\n"
"      work stoppage, computer failure or malfunction, or any and all\n"
"      other commercial damages or losses), even if such Contributor\n"
"      has been advised of the possibility of such damages.\n"
"\n"
"   9. Accepting Warranty or Additional Liability. While redistributing\n"
"      the Work or Derivative Works thereof, You may choose to offer,\n"
"      and charge a fee for, acceptance of support, warranty, indemnity,\n"
"      or other liability obligations and/or rights consistent with this\n"
"      License. However, in accepting such obligations, You may act only\n"
"      on Your own behalf and on Your sole responsibility, not on behalf\n"
"      of any other Contributor, and only if You agree to indemnify,\n"
"      defend, and hold each Contributor harmless for any liability\n"
"      incurred by, or claims asserted against, such Contributor by reason\n"
"      of your accepting any such warranty or additional liability.\n"
"\n"
"   END OF TERMS AND CONDITIONS\n"
"\n"
"   APPENDIX: How to apply the Apache License to your work.\n"
"\n"
"      To apply the Apache License to your work, attach the following\n"
"      boilerplate notice, with the fields enclosed by brackets \"[]\"\n"
"      replaced with your own identifying information. (Don't include\n"
"      the brackets!)  The text should be enclosed in the appropriate\n"
"      comment syntax for the file format. We also recommend that a\n"
"      file or class name and description of purpose be included on the\n"
"      same \"printed page\" as the copyright notice for easier\n"
"      identification within third-party archives.\n"
"\n"
"   Copyright [2026] [Huỳnh Đỗ Minh Duy]\n"
"\n"
"   Licensed under the Apache License, Version 2.0 (the \"License\");\n"
"   you may not use this file except in compliance with the License.\n"
"   You may obtain a copy of the License at\n"
"\n"
"       http://www.apache.org/licenses/LICENSE-2.0\n"
"\n"
"   Unless required by applicable law or agreed to in writing, software\n"
"   distributed under the License is distributed on an \"AS IS\" BASIS,\n"
"   WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.\n"
"   See the License for the specific language governing permissions and\n"
"   limitations under the License.\n"
;
/* Interactive help shell */
#if defined(_WIN32)
static int luc_stdin_console(void){
    HANDLE h=GetStdHandle(STD_INPUT_HANDLE);
    DWORD m=0;
    return h && h!=INVALID_HANDLE_VALUE && GetConsoleMode(h,&m);
}
#else
static int luc_stdin_console(void){ return isatty(STDIN_FILENO); }
#endif
/* LUC_REPL=1 forces shell, even piped */
static int repl_wanted(void){ return luc_stdin_console() || getenv("LUC_REPL") != NULL; }

/* Prompt, read line; NULL on EOF */
static char *repl_readline(const char *prompt,char *buf,size_t cap){
    fputs(prompt,stdout); fflush(stdout);
    if(!fgets(buf,(int)cap,stdin)){ printf("\n"); return NULL; }
    size_t n=strlen(buf);
    while(n && (buf[n-1]=='\n'||buf[n-1]=='\r')) buf[--n]=0;
    return buf;
}

/* Query keyword: print(..) to print, keep !strict */
static void help_key(const char *in,char *out,size_t cap){
    size_t i=0,n=0;
    while(in[i]==' '||in[i]=='\t') i++;
    if(in[i]=='!'&&n+1<cap) out[n++]=in[i++];
    while(in[i]&&(isalnum((unsigned char)in[i])||in[i]=='_')&&n+1<cap) out[n++]=in[i++];
    out[n]=0;
}

typedef struct { const char *name, *info; } HelpTopic;
static const HelpTopic HELP_TOPICS[] = {
{"print",
 "print(\"hi\", x) shows values separated by tabs.\n"
 "io.write(\"hi\") writes with no newline."},
{"create",
 "create x = 10 makes a local. Plain x = 10 makes a global\n"
 "(forbidden under !strict)."},
{"function",
 "create function add(a, b)\n    return a + b\nend\n"
 "Call it: add(2, 3). return sends a value back."},
{"command",
 "command greet(name)\n    print(\"hi \" + name)\nend\n"
 "A procedure: it runs, it never returns. Call it: greet(\"bo\"),\n"
 "run greet(\"bo\"), bare greet \"bo\", or block greet: ... end.\n"
 "Needs `import libs` first."},
{"run",
 "run greet(\"bo\") runs the command greet.\n"
 "Same as greet(\"bo\") - just more explicit."},
{"import",
 "import ai / window / net / discord loads system libs.\n"
 "import window(\"w\") binds a short name.\n"
 "import add from mymath loads from a user lib (needs `import libs`)."},
{"libs",
 "import libs unlocks user libraries: make / get / pack / command.\n"
 "libs.info(\"lib\") lists parts. See demos/libs_libaries/."},
{"make",
 "make add (after `import libs`, first code line) declares this file a part.\n"
 "Every top-level create is auto-exported."},
{"get",
 "get add, sub loads parts by make-name into locals.\n"
 "Searches next to your script, cwd, luc_modules/, LUC_PATH."},
{"pack",
 "get add, sub then pack mymath writes mymath.luic next to your file.\n"
 "Share that one file; others load it with import-from."},
{"repeat",
 "repeat 5 do ... end runs 5 times.\n"
 "repeat 1, 10 as i do ... end counts with i (or 1(\"i\") import-style).\n"
 "repeat item, i in list do ... end iterates."},
{"if",
 "if x > 0 then ... elseif ... else ... end."},
{"while",
 "while x > 0 do ... end loops until the condition is false."},
{"task",
 "task.spawn(function() ... end) runs code in the background.\n"
 "task.wait(seconds) pauses the current task."},
{"string",
 "string.split(\"a,b\", \",\"), string.trim(s), string.upper(s).\n"
 "len(s) gives the length."},
{"math",
 "math.floor/ceil/sqrt/sin/cos/random, math.pi.\n"
 "math.karatsuba(\"huge\", \"huge\") multiplies exactly."},
{"len",
 "len(x) counts lists, dicts and strings: len([1,2]), len(\"hi\")."},
{"edit",
 "edit hello.luc opens LCode, the tiny built-in editor (.luc, .lua, .py).\n"
 "Type, arrows move, Enter splits, Ctrl+C copies the line, Ctrl+V pastes,\n"
 "Ctrl+S saves, ^R runs the .luc file, Esc quits."},
{"lcode",
 "lcode [path] opens LCode on ONE project folder (like VSCode).\n"
 "No path opens the current folder, except home/drive roots (refused).\n"
 "Tree: Enter expands, Left/Right collapse, click opens, wheel scrolls.\n"
 "Files: N new, D folder, F2 rename, ^C/^V copy/paste, right-click menu.\n"
 "Code gets colors. ^E switches panes, ^B hides the tree, ^R runs."},
{"lccode",
 "alias of lcode (legacy name)."},
{"require",
 "create m = require(\"name\") loads a third-party file module.\n"
 "System libs use import instead."},
{"!strict",
 "!strict on the first line: 'create' required, dict keys quoted,\n"
 "bare function/command forbidden, True/False/None rejected."},
{"!karatsuba",
 "!karatsuba on the first line: every integer-string * here is exact."},
{"ai",
 "import ai: lanternl mini-PyTorch. ai.Train for numbers, ai.LMTrain for text.\n"
 "Needs: luc install ai. See demos/ai_libaries/."},
{"net",
 "import net(\"n\"): TCP + HTTP. n.get(url), n.serve(port).\n"
 "See demos/net_libaries/."},
{"window",
 "import window(\"w\"): SDL2 graphics and sound.\n"
 "Draw: clear/pixel/line/rect/circle/arc/ellipse/triangle/polygon/text/image/sprite.\n"
 "Input: key/mouse + pressed/released, wheel, text_input, cursor, mouse_grab/set.\n"
 "Touch: touches/touch_pressed/released. Gamepad: pad_count/button/axis/name.\n"
 "System: clipboard, message, open_url, os_name. Window: fullscreen, minimize,\n"
 "maximize/restore, move_window, display_size. Sound: play/stop/pause_sound.\n"
 "Needs: luc install window. See demos/window_libaries/."},
{"discord",
 "import discord: chat bots with prefix/slash commands and buttons.\n"
 "Needs a token + luc install discord. See demos/discord_libaries/."},
{NULL,NULL}
};

static void repl_help_loop(void){
    char buf[512], key[64];
    printf("Welcome to the Luc help desk. Type any keyword (try: print) to learn it.\n'back' returns to the main prompt, 'exit' quits.\n");
    for(;;){
        if(!repl_readline("|  help \xe2\x9d\xaf ",buf,sizeof buf)) return;
        help_key(buf,key,sizeof key);
        if(!key[0]) continue;
        if(!strcmp(key,"exit")||!strcmp(key,"quit")) return;
        if(!strcmp(key,"back")) return;
        if(!strcmp(key,"help")){ printf("Type a keyword, e.g. print, create, import, command.\n"); continue; }
        const HelpTopic *t; for(t=HELP_TOPICS;t->name;t++) if(!strcmp(t->name,key)) break;
        if(t->name) printf("%s\n",t->info);
        else printf("No help for '%s'. Try: print create import command repeat function task math string.\n",key);
    }
}

#if defined(_WIN32)
/* Tiny editor (`edit file.luc`); .luc/.lua/.py, UTF-8 safe */

typedef struct { char *b; size_t len, cap; } EdLine;

/* Parked buffer for unsaved file switch */
typedef struct { EdLine *ln; size_t n, cap, cx, cy, top; int dirty; char path[1024]; } EdStash;

typedef struct { const char *label; int id; } EdMenuItem;

typedef struct {
    EdLine *ln; size_t n, cap;
    size_t cx, cy;            /* Cursor: byte col, line */
    size_t top;               /* First visible line */
    size_t dleft;             /* H-scroll in display cols */
    int dirty, quit_arm;
    char *clip; size_t cliplen;
    char status[160];
    char path[1024];
    int numw;                 /* Gutter width */
    /* Explorer (left pane) */
    struct ExItem *ex; size_t exn, excap;
    size_t exsel, extop;
    char exdir[1024];
    int focus;                /* 0 code, 1 files */
    int show_ex;              /* Explorer visible */
    int vw, vh;               /* Last drawn size */
    unsigned short surr_hi;   /* Pending lead surrogate */
    char **rsav; size_t rsavn, rsavcap;  /* Collapse-all restore set */
    int root_shut;            /* Folder header shut */
    /* Parked dirty buffers */
    EdStash *stash; size_t nstash, scap;
    /* Popup menu */
    int menu; EdMenuItem mitems[8]; int nitems, msel;
    int click_eat;        /* Swallow double-click after pick */
    /* Inline naming popup input */
    int naming; /* 0 off, 1 new file, 2 new dir, 3 rename */
    char namb[256]; size_t namlen, namcur;
    char namdir[1024];   /* Target or parent dir */
    char namold[1024];   /* Rename source path */
    /* File clipboard */
    char fcb[1024]; int fcb_dir, fcb_ok;
    /* Popup anchored at mouse */
    int menu_anch, menu_ax, menu_ay;
    char menutitle[320];
    /* Pending delete confirm */
    char deltarget[1024]; int delisdir;
} Editor;

typedef struct ExItem { char *name; char *full; int isdir; int depth; int expanded; } ExItem;
#define ED_EX_MAX 2000   /* visible tree cap */

#define ED_EXW 28   /* explorer pane width */

static void ed_line_reserve(EdLine *l,size_t extra){
    if(l->len+extra+1>l->cap){
        size_t nc=l->cap?l->cap*2:64;
        while(nc<l->len+extra+1) nc*=2;
        l->b=(char*)lrealloc(l->b,nc);
        l->cap=nc;
    }
}

static void ed_ins_line(Editor *e,size_t at){
    if(e->n==e->cap){ e->cap=e->cap?e->cap*2:16; e->ln=(EdLine*)lrealloc(e->ln,sizeof(EdLine)*e->cap); }
    memmove(&e->ln[at+1],&e->ln[at],(e->n-at)*sizeof(EdLine));
    e->ln[at].b=NULL; e->ln[at].len=0; e->ln[at].cap=0;
    e->n++;
}

static int ed_is_cont(unsigned char c){ return (c&0xC0)==0x80; }

static void ed_clamp(Editor *e){
    if(e->n==0){ ed_ins_line(e,0); }
    if(e->cy>=e->n) e->cy=e->n-1;
    if(e->cx>e->ln[e->cy].len) e->cx=e->ln[e->cy].len;
}

/* Keep scroll in range */
static void ed_clamp_view(Editor *e,int H){
    size_t rows=H>2?(size_t)(H-2):1;
    size_t maxtop=e->n>rows?e->n-rows:0;
    size_t erows=rows>2?rows-2:1;
    size_t maxex=e->exn>erows?e->exn-erows:0;
    if(e->top>maxtop) e->top=maxtop;
    if(e->extop>maxex) e->extop=maxex;
}

static void ed_move_left(Editor *e){
    ed_clamp(e);
    if(e->cx>0){ do e->cx--; while(e->cx>0 && ed_is_cont((unsigned char)e->ln[e->cy].b[e->cx])); }
}

static void ed_move_right(Editor *e){
    ed_clamp(e);
    { size_t L=e->ln[e->cy].len;
      if(e->cx<L){ do e->cx++; while(e->cx<L && ed_is_cont((unsigned char)e->ln[e->cy].b[e->cx])); } }
}

/* Byte width: tabs expand, controls 1, continuations 0 */
static int ed_chw(unsigned char c,int col){
    if(c=='\t') return 4-(col%4);
    if(c<32||c==127) return 1;
    if((c&0xC0)==0x80) return 0;
    return 1;
}

/* Printable form; binaries stay viewable */
static unsigned char ed_chshow(unsigned char c){
    if(c=='\t'||c>=32&&c!=127) return c;
    return '.';
}

/* Cursor display col */
static size_t ed_dcol(Editor *e){
    ed_clamp(e);
    { size_t d=0,col=0,k=0; char *b=e->ln[e->cy].b;
      while(k<e->cx){ int w=ed_chw((unsigned char)b[k],(int)col); d+=(size_t)w; col+=(size_t)w; k++; }
      return d; }
}

/* Byte offset for display col */
static size_t ed_byte_at(char *b,size_t len,size_t dc){
    size_t k=0,d=0,col=0;
    while(k<len&&d<dc){ int w=ed_chw((unsigned char)b[k],(int)col); d+=(size_t)w; col+=(size_t)w; k++; }
    while(k<len&&ed_is_cont((unsigned char)b[k])) k++;
    return k;
}

static void ed_insert_bytes(Editor *e,const char *s,size_t m){
    ed_clamp(e);
    if(m==0) return;
    { EdLine *l=&e->ln[e->cy];
      ed_line_reserve(l,m);
      memmove(l->b+e->cx+m,l->b+e->cx,l->len-e->cx+1);
      memcpy(l->b+e->cx,s,m);
      l->len+=m; e->cx+=m; e->dirty=1; e->status[0]=0; }
}

static void ed_backspace(Editor *e){
    ed_clamp(e);
    if(e->cx>0){
        size_t from=e->cx; ed_move_left(e);
        { EdLine *l=&e->ln[e->cy]; size_t cnt=from-e->cx;
          memmove(l->b+e->cx,l->b+from,l->len-from+1);
          l->len-=cnt; e->dirty=1; e->status[0]=0; }
    } else if(e->cy>0){
        size_t py=e->cy-1;
        EdLine *p=&e->ln[py], *c=&e->ln[e->cy];
        e->cx=p->len;
        ed_line_reserve(p,c->len);
        memcpy(p->b+p->len,c->b,c->len+1); p->len+=c->len;
        free(c->b);
        memmove(&e->ln[e->cy],&e->ln[e->cy+1],(e->n-e->cy-1)*sizeof(EdLine));
        e->n--; e->cy=py; e->dirty=1; e->status[0]=0;
    }
}

static void ed_newline(Editor *e){
    ed_clamp(e);
    { EdLine *l=&e->ln[e->cy];
      ed_ins_line(e,e->cy+1);
      l=&e->ln[e->cy];
      EdLine *nl=&e->ln[e->cy+1];
      size_t tail=l->len-e->cx;
      ed_line_reserve(nl,tail);
      memcpy(nl->b,l->b+e->cx,tail); nl->b[tail]=0; nl->len=tail;
      l->b[e->cx]=0; l->len=e->cx;
      e->cy++; e->cx=0; e->dirty=1; e->status[0]=0; }
}

static void ed_copy_line(Editor *e){
    ed_clamp(e);
    { EdLine *l=&e->ln[e->cy];
      free(e->clip); e->clip=NULL; e->cliplen=0;
      if(l->len){ e->clip=(char*)lmalloc(l->len+1); memcpy(e->clip,l->b,l->len+1); e->cliplen=l->len; }
      snprintf(e->status,sizeof e->status,"copied line %d",(int)e->cy+1); }
}

static void ed_load(Editor *e);
static int ed_save_to(EdLine *ln,size_t n,const char *path){
    FILE *f=fopen(path,"wb");
    size_t i;
    if(!f) return 0;
    for(i=0;i<n;i++){ if(ln[i].len) fwrite(ln[i].b,1,ln[i].len,f); fputc('\n',f); }
    fclose(f);
    return 1;
}

static int ed_save(Editor *e){
    if(!e->path[0]){ snprintf(e->status,sizeof e->status,"no file open"); return 0; }
    if(!ed_save_to(e->ln,e->n,e->path)){ snprintf(e->status,sizeof e->status,"cannot write '%s'",e->path); return 0; }
    e->dirty=0; e->quit_arm=0;
    snprintf(e->status,sizeof e->status,"saved %d line(s) to '%s'",(int)e->n,e->path);
    return 1;
}

/* Worth keeping: named or non-empty */
static int ed_cur_worth(Editor *e){
    return e->path[0] || e->n>1 || (e->n==1 && e->ln[0].len>0);
}

static void ed_stash_current(Editor *e){
    EdStash *s;
    if(!ed_cur_worth(e)) return;
    if(e->nstash==e->scap){ e->scap=e->scap?e->scap*2:4;
        e->stash=(EdStash*)lrealloc(e->stash,sizeof(EdStash)*e->scap); }
    s=&e->stash[e->nstash++];
    s->ln=e->ln; s->n=e->n; s->cap=e->cap; s->cx=e->cx; s->cy=e->cy; s->top=e->top;
    s->dirty=e->dirty;
    snprintf(s->path,sizeof s->path,"%s",e->path);
    e->ln=NULL; e->n=0; e->cap=0; e->cx=0; e->cy=0; e->top=0; e->dleft=0; e->dirty=0;
}

static int ed_unstash(Editor *e,const char *path){
    size_t i;
    for(i=0;i<e->nstash;i++) if(!strcmp(e->stash[i].path,path)){
        e->ln=e->stash[i].ln; e->n=e->stash[i].n; e->cap=e->stash[i].cap;
        e->cx=e->stash[i].cx; e->cy=e->stash[i].cy; e->top=e->stash[i].top;
        e->dirty=e->stash[i].dirty;
        snprintf(e->path,sizeof e->path,"%s",path);
        memmove(&e->stash[i],&e->stash[i+1],(e->nstash-i-1)*sizeof(EdStash));
        e->nstash--;
        return 1;
    }
    return 0;
}

/* Open path: reuse open, restore parked, else load */
static void ed_open_path(Editor *e,const char *full){
    if(e->path[0] && !strcmp(e->path,full)){ e->focus=0; return; }
    ed_stash_current(e);
    if(ed_unstash(e,full)){ e->focus=0; e->quit_arm=0; return; }
    snprintf(e->path,sizeof e->path,"%s",full);
    e->cx=0; e->cy=0; e->top=0; e->dleft=0; e->dirty=0; e->quit_arm=0;
    ed_load(e);
    e->focus=0;
}

static int ed_unsaved_count(Editor *e){
    size_t i; int c=(e->dirty&&e->path[0])?1:0;
    for(i=0;i<e->nstash;i++) if(e->stash[i].dirty) c++;
    return c;
}

static void ed_save_all(Editor *e){
    size_t i; int done=0, unnamed=0;
    if(e->dirty){
        if(e->path[0] && ed_save_to(e->ln,e->n,e->path)){ e->dirty=0; done++; }
        else unnamed++;
    }
    for(i=0;i<e->nstash;i++) if(e->stash[i].dirty){
        if(ed_save_to(e->stash[i].ln,e->stash[i].n,e->stash[i].path)){ e->stash[i].dirty=0; done++; }
        else unnamed++;
    }
    e->quit_arm=0;
    if(unnamed) snprintf(e->status,sizeof e->status,"saved %d, %d unnamed skipped",done,unnamed);
    else snprintf(e->status,sizeof e->status,"saved %d file(s)",done);
}

static int ed_path_dirty(Editor *e,const char *full){
    size_t i;
    if(e->dirty && e->path[0] && !strcmp(e->path,full)) return 1;
    for(i=0;i<e->nstash;i++) if(e->stash[i].dirty && !strcmp(e->stash[i].path,full)) return 1;
    return 0;
}

static int ed_dir_dirty(Editor *e,const char *full){
    size_t i, n=strlen(full);
    if(!n) return 0;
    if(e->dirty && e->path[0] && !strncmp(e->path,full,n)
       && (e->path[n]=='\\'||e->path[n]=='/')) return 1;
    for(i=0;i<e->nstash;i++) if(e->stash[i].dirty && !strncmp(e->stash[i].path,full,n)
       && (e->stash[i].path[n]=='\\'||e->stash[i].path[n]=='/')) return 1;
    return 0;
}

/* Fix open and parked paths after rename */
static void ed_rename_paths(Editor *e,const char *oldp,const char *newp){
    size_t i;
    if(e->path[0] && !strcmp(e->path,oldp)) snprintf(e->path,sizeof e->path,"%s",newp);
    for(i=0;i<e->nstash;i++) if(!strcmp(e->stash[i].path,oldp))
        snprintf(e->stash[i].path,sizeof e->stash[i].path,"%s",newp);
}

static void ed_load(Editor *e){
    int len=0;
    char *src=read_file(e->path,&len);
    e->n=0; e->cap=0; e->ln=NULL;
    if(!src){ ed_ins_line(e,0); snprintf(e->status,sizeof e->status,"new file '%s'",e->path); return; }
    { int i=0,s=0;
      for(i=0;i<=len;i++){
          if(i==len||src[i]=='\n'){
              int el=i-s;
              if(el>0&&src[i-1]=='\r') el--;
              ed_ins_line(e,e->n);
              EdLine *l=&e->ln[e->n-1];
              ed_line_reserve(l,(size_t)el);
              if(el) memcpy(l->b,src+s,(size_t)el);
              l->b[el]=0; l->len=(size_t)el;
              s=i+1;
          }
      }
      /* "a\n" is one line: drop phantom tail */
      if(e->n>1&&e->ln[e->n-1].len==0){ free(e->ln[e->n-1].b); e->n--; }
      if(e->n==0) ed_ins_line(e,0);
      snprintf(e->status,sizeof e->status,"loaded '%s' (%d line(s))",e->path,(int)e->n); }
    free(src);
}

static void ed_free(Editor *e){
    size_t i;
    for(i=0;i<e->n;i++) free(e->ln[i].b);
    free(e->ln); free(e->clip);
}

/* LC syntax colors */
#define EC_NONE 0
#define EC_KEY 1     /* Keywords */
#define EC_STR 2     /* Strings */
#define EC_NUM 3     /* Numbers */
#define EC_COM 4     /* Comments */
#define EC_FN 5      /* Calls */
#define EC_DIR 6     /* Directives */
static const char *EC_SEQ[]={ "",
    "38;2;197;134;192", "38;2;206;145;120", "38;2;181;206;168",
    "38;2;106;153;85", "38;2;220;220;170", "38;2;86;156;214" };

static int ed_is_kw(const char *s,size_t n){
    static const char *kw[]={
        "and","break","do","else","elseif","end","for","function","command",
        "if","in","not","or","repeat","return","then","until","while",
        "run","make","get","pack","as","create","import",
        "true","false","nil",NULL};
    int i;
    for(i=0;kw[i];i++){ size_t k=0; while(k<n&&kw[i][k]&&kw[i][k]==s[k]) k++;
        if(k==n&&kw[i][k]==0) return 1; }
    return 0;
}

#define EC_LINEBG "\x1b[48;2;42;45;46m"   /* Cursor-line bg */
#define EC_CODEBG "\x1b[48;2;30;30;30m"   /* Code pane bg */
#define EC_EXBG "\x1b[48;2;17;17;17m"     /* Explorer pane bg */
#define EC_TOPBG "\x1b[48;2;22;22;22m"    /* Top strip bg */
static int ed_hl_bg = 0;   /* 0 plain, 1 code bg, 2 line bg */
static void ed_hl_emit(int code,int *cur){
    if(code==*cur) return;
    if(code) printf("\x1b[%sm",EC_SEQ[code]);
    else if(ed_hl_bg==2) printf("\x1b[0m" EC_LINEBG);
    else if(ed_hl_bg==1) printf("\x1b[0m" EC_CODEBG);
    else printf("\x1b[0m");
    *cur=code;
}

/* Track block state per line; closer is ]=lvl] */
static void ed_scan_state(char *b,size_t len,int *cm,int *stt){
    size_t k=0;
    if(*cm>=0||*stt>=0){
        int lvl=(*cm>=0)?*cm:*stt;
        while(k<len){
            if(b[k]==']'){ size_t j=k+1; int e=0;
                while(j<len&&b[j]=='='&&e<lvl){e++;j++;}
                if(e==lvl&&j<len&&b[j]==']'){ k=j+1; *cm=-1; *stt=-1; break; } }
            k++;
        }
        if(*cm>=0||*stt>=0) return;
    }
    { int q=0;
      while(k<len){
          unsigned char c=(unsigned char)b[k];
          if(q==1){ if(c=='\\'){k+=2;continue;} if(c=='"') q=0; k++; continue; }
          if(q==2){ if(c=='\\'){k+=2;continue;} if(c=='\'') q=0; k++; continue; }
          if(c=='"'){q=1;k++;continue;}
          if(c=='\''){q=2;k++;continue;}
          if(c=='-'&&k+1<len&&b[k+1]=='-'){
              size_t j=k+2; int e=0;
              if(j<len&&b[j]=='['){ j++; while(j<len&&b[j]=='='&&e<64){e++;j++;}
                  if(j<len&&b[j]=='['){ *cm=e; return; } }
              return;
          }
          if(c=='['){ size_t j=k+1; int e=0;
              while(j<len&&b[j]=='='&&e<64){e++;j++;}
              if(e>0&&j<len&&b[j]=='['){ *stt=e; return; } }
          k++;
      } }
}

/* Render line; col = true display column for tab stops, w = clipped width. */
static void ed_tab_out(int *w,int *col,int maxw){
    int tw=4-((*col)%4), q;
    for(q=0;q<tw&&*w<maxw;q++){ fputc(' ',stdout); (*w)++; (*col)++; }
}
static void ed_hl_line(char *b,size_t len,size_t start,int maxw,int is_luc,int cm,int stt){
    size_t k=0;
    int w=0, col=0, cur=EC_NONE, done=0;
    int st=0, lvl=0;   /* 0 code, 1 dqs, 2 sqs, 3 block */
    if(cm>0){ st=3; lvl=cm-1; }
    else if(stt>0){ st=4; lvl=stt-1; }
    while(k<start){ col+=ed_chw((unsigned char)b[k],col); k++; }
    /* block entry color */
    if(st==3&&start==0) ed_hl_emit(EC_COM,&cur);
    if(st==4&&start==0) ed_hl_emit(EC_STR,&cur);
    while(k<len&&!done){
        unsigned char c=(unsigned char)b[k];
        if(st==3||st==4){
            int want=(st==3)?EC_COM:EC_STR;
            if(c==']'){ size_t j=k+1; int e=0;
                while(j<len&&b[j]=='='&&e<lvl){e++;j++;}
                if(e==lvl&&j<len&&b[j]==']'){
                    size_t m;
                    for(m=k;m<=j;m++){ if(m>=start&&w<maxw){ ed_hl_emit(want,&cur); fputc(b[m],stdout); w++; col++; } else if(m>=start) done=1; else col++; }
                    k=j+1; st=0; continue;
                } }
            if(k>=start&&w<maxw){
                ed_hl_emit(want,&cur);
                if(c=='\t') ed_tab_out(&w,&col,maxw);
                else { fputc(c,stdout); w++; col++; }
            }
            else if(k>=start) done=1;
            else col+=ed_chw(c,col);
            k++; continue;
        }
        if(st==1||st==2){
            char q=st==1?'"':'\'';
            if(k>=start&&w<maxw){
                ed_hl_emit(EC_STR,&cur);
                if(c=='\t') ed_tab_out(&w,&col,maxw);
                else { fputc(c,stdout); w++; col++; }
            }
            else if(k>=start) done=1;
            else col+=ed_chw(c,col);
            if(c=='\\'&&k+1<len){ k++; c=(unsigned char)b[k];
                if(k>=start&&w<maxw){ fputc(b[k],stdout); w++; col++; }
                else if(k>=start) done=1;
                else col++; }
            else if(c==q) st=0;
            k++; continue;
        }
        /* code mode */
        if(c=='-'&&k+1<len&&b[k+1]=='-'){
            while(k<len){ c=(unsigned char)b[k];
                if(k>=start&&w<maxw){ ed_hl_emit(EC_COM,&cur);
                    if(c=='\t') ed_tab_out(&w,&col,maxw);
                    else { fputc(c,stdout); w++; col++; } }
                else if(k>=start) { done=1; break; }
                else col+=ed_chw(c,col);
                k++; }
            continue;
        }
        if((c=='"'||c=='\'')){
            st=(c=='"')?1:2;
            if(k>=start&&w<maxw){ ed_hl_emit(EC_STR,&cur); fputc(c,stdout); w++; col++; }
            else if(k>=start) done=1;
            else col++;
            k++; continue;
        }
        if(is_luc&&c=='['){
            size_t j=k+1; int e=0;
            while(j<len&&b[j]=='='&&e<64){e++;j++;}
            if(e>0&&j<len&&b[j]=='['){
                size_t m;
                for(m=k;m<=j;m++){ if(m>=start&&w<maxw){ ed_hl_emit(EC_STR,&cur); fputc(b[m],stdout); w++; col++; } else if(m>=start) done=1; else col++; }
                k=j+1; st=4; lvl=e; continue;
            }
        }
        if((c>='A'&&c<='Z')||(c>='a'&&c<='z')||c=='_'||c>=128){
            size_t j=k;
            while(j<len){ unsigned char d=(unsigned char)b[j];
                if((d>='A'&&d<='Z')||(d>='a'&&d<='z')||(d>='0'&&d<='9')||d=='_'||d>=128) j++; else break; }
            { int iskw=is_luc&&ed_is_kw(b+k,j-k);
              int iscall=0;
              if(!iskw){ size_t m=j; while(m<len&&(b[m]==' '||b[m]=='\t')) m++;
                  if(m<len&&b[m]=='(') iscall=1; }
              { size_t m;
                for(m=k;m<j;m++){ if(m>=start&&w<maxw){ ed_hl_emit(iskw?EC_KEY:(iscall?EC_FN:EC_NONE),&cur); fputc(b[m],stdout); w++; col++; } else if(m>=start) done=1; else col++; }
                if(j>k&&j-1>=start&&w>=maxw) done=1; }
              k=j; continue; }
        }
        if(c>='0'&&c<='9'&&(k==0||!(((b[k-1]>='A')&&(b[k-1]<='Z'))||((b[k-1]>='a')&&(b[k-1]<='z'))||((b[k-1]>='0')&&(b[k-1]<='9'))||b[k-1]=='_'))){
            size_t j=k;
            if(c=='0'&&j+1<len&&(b[j+1]=='x'||b[j+1]=='X')){ j+=2; while(j<len){ unsigned char d=(unsigned char)b[j];
                if((d>='0'&&d<='9')||(d>='a'&&d<='f')||(d>='A'&&d<='F')) j++; else break; } }
            else { while(j<len&&((b[j]>='0'&&b[j]<='9')||b[j]=='.')) j++; }
            { size_t m;
              for(m=k;m<j;m++){ if(m>=start&&w<maxw){ ed_hl_emit(EC_NUM,&cur); fputc(b[m],stdout); w++; col++; } else if(m>=start) done=1; else col++; }
              if(j>k&&j-1>=start&&w>=maxw) done=1; }
            k=j; continue;
        }
        /* directives: !word at first token */
        if(c=='!'&&is_luc){
            size_t m=k; int lead=1, p;
            for(p=(int)k-1;p>=0;p--){ if(b[p]!=' '&&b[p]!='\t'){lead=0;break;} }
            if(lead){ m++; while(m<len){ unsigned char d=(unsigned char)b[m];
                if((d>='A'&&d<='Z')||(d>='a'&&d<='z')||d=='_') m++; else break; }
                if(m>k+1){ size_t q;
                    for(q=k;q<m;q++){ if(q>=start&&w<maxw){ ed_hl_emit(EC_DIR,&cur); fputc(b[q],stdout); w++; col++; } else if(q>=start) done=1; else col++; }
                    k=m; continue; } }
        }
        /* plain char (tabs expand, controls placeholder) */
        if(k>=start&&w<maxw){
            ed_hl_emit(EC_NONE,&cur);
            if(c=='\t'){ int tw=4-(col%4), q; for(q=0;q<tw&&w<maxw;q++){ fputc(' ',stdout); w++; col++; } }
            else { fputc(ed_chshow(c),stdout); w+=ed_chw(c,col); col+=ed_chw(c,col); }
        } else if(k>=start) done=1;
        else if(c=='\t'){ int tw=4-(col%4); col+=tw; }
        else col+=ed_chw(c,col);
        k++;
    }
    if(cur!=EC_NONE) printf("\x1b[0m");
}

static int ed_ex_color(const char *nm,int isdir){
    if(isdir) return 36;
    { const char *d=strrchr(nm,'.');
      if(!d||d==nm) return 0;
      d++;
      { char e[8]; size_t k;
        for(k=0;k<7&&d[k];k++){ char c=d[k]; e[k]=(c>='A'&&c<='Z')?(char)(c+32):c; }
        e[k]=0;
        if(!strcmp(e,"luc")) return 32;
        if(!strcmp(e,"lua")) return 34;
        if(!strcmp(e,"py")) return 33;
        if(!strcmp(e,"zip")||!strcmp(e,"exe")||!strcmp(e,"dll")||!strcmp(e,"so")) return 31; }
      return 0; }
}

static void ed_popup_draw(Editor *e,int W,int H);
static void ed_menu_open(Editor *e,int kind,int ax,int ay);
static void ed_naming_start(Editor *e,int mode);

static void ed_draw(Editor *e){
    int W=80,H=24,r;
    int digits=1;
    size_t CW;
    HANDLE h=GetStdHandle(STD_OUTPUT_HANDLE);
    CONSOLE_SCREEN_BUFFER_INFO bi;
    if(GetConsoleScreenBufferInfo(h,&bi)){
        W=bi.srWindow.Right-bi.srWindow.Left+1;
        H=bi.srWindow.Bottom-bi.srWindow.Top+1;
    }
    if(H<6) H=6; if(W<40) W=40;
    { size_t t=e->n>0?e->n:1; while(t>=10){ digits++; t/=10; } if(digits<2) digits=2; }
    e->numw=digits+4;               /* " 12    " (space gap, no bar) */
    int EXW=e->show_ex?ED_EXW:0;
    CW=(size_t)(W-EXW)-e->numw;
    if((int)CW<8) CW=8;
    e->vw=W; e->vh=H;
    ed_clamp(e);
    ed_clamp_view(e,H);
    if(e->cy<e->top) e->top=e->cy;
    if(e->cy>=e->top+(size_t)(H-2)) e->top=e->cy-(H-3);
    if(e->exn>0 && e->exsel>=e->exn) e->exsel=e->exn-1;
    if(e->exsel<e->extop) e->extop=e->exsel;
    if(e->exsel>=e->extop+(size_t)(H-2) && H-2>0) e->extop=e->exsel-(H-3);
    { size_t dc=ed_dcol(e);
      if(dc<e->dleft) e->dleft=dc;
      if(dc>=e->dleft+CW-1) e->dleft=dc-(CW-2);
      printf("\x1b[?25l\x1b[H");
      { char title[256];
        const char *b1=strrchr(e->path,'/'), *b2=strrchr(e->path,'\\');
        const char *b=b1; if(b2&&(!b||b2>b)) b=b2;
        if(e->path[0]&&b) snprintf(title,sizeof title,"%s - lcode",b+1);
        else if(e->path[0]) snprintf(title,sizeof title,"%s - lcode",e->path);
        else snprintf(title,sizeof title,"lcode");
        title[W-4]=0;
        printf(EC_TOPBG "\x1b[97m  %s",title);
        printf("\x1b[K\x1b[0m\n"); }
      /* .luc gets full highlight, others generic */
      { int is_luc=0;
        const char *dd=strrchr(e->path,'.');
        if(dd&&(!strcmp(dd,".luc")||!strcmp(dd,".LUC"))) is_luc=1;
        /* Block state from line 0 to view */
        int pcm=-1, pst=-1;
        { size_t pre; for(pre=0;pre<e->top&&pre<e->n;pre++)
            ed_scan_state(e->ln[pre].b,e->ln[pre].len,&pcm,&pst); }
        for(r=0;r<H-2;r++){
          size_t li=e->top+r;
          printf("\x1b[K");
          /* Explorer cell: header, then entries */
          if(EXW){
          printf(EC_EXBG);
          if(r==0){
              printf("\x1b[90mEXPLORER");
              { int kk; for(kk=8;kk<EXW-3;kk++) fputc(' ',stdout); }
              printf("...");
              printf("\x1b[0m");
          } else if(r==1){
              const char *dd=e->exdir;
              const char *bb=dd;
              { const char *p=dd; while(*p){ if(*p=='\\'||*p=='/') bb=p+1; p++; } }
              if(!*bb) bb=dd;
              printf(e->root_shut?"\xe2\x96\xb8":"\xe2\x96\xbe");
              fputc(' ',stdout);
              printf(e->root_shut?"\xf0\x9f\x93\x81":"\xf0\x9f\x93\x82");
              fputc(' ',stdout);
              { int cw=4, kk=0;
                while(bb[kk]&&cw<EXW){ fputc(bb[kk],stdout); cw++; kk++; }
                while(cw<EXW){ fputc(' ',stdout); cw++; } }
              printf("\x1b[0m");
          } else {
          { size_t ei=e->extop+(r-2);
            if(ei<e->exn){
                const char *nm=e->ex[ei].name;
                int cw=0, kk=0, i, isd=e->ex[ei].isdir;
                int dep=e->ex[ei].depth; if(dep<1) dep=1; if(dep>5) dep=5;
                /* Dirty names yellow, else extension color */
                int col=isd ? (ed_dir_dirty(e,e->ex[ei].full)?33:0)
                            : (ed_path_dirty(e,e->ex[ei].full)?33:ed_ex_color(nm,isd));
                int sel=(e->focus==1&&ei==e->exsel);
                if(sel) printf("\x1b[7m");
                else if(col) printf("\x1b[%dm",col);
                for(i=0;i<dep*2&&cw<EXW-6;i++){ fputc(' ',stdout); cw++; }
                if(isd){ fputs(e->ex[ei].expanded?"\xe2\x96\xbe":"\xe2\x96\xb8",stdout); cw++; }
                else { fputc(' ',stdout); cw++; }
                fputc(' ',stdout); cw++;
                if(isd){ fputs(e->ex[ei].expanded?"\xf0\x9f\x93\x82":"\xf0\x9f\x93\x81",stdout); cw+=2; }
                else { fputc(' ',stdout); fputc(' ',stdout); cw+=2; }
                fputc(' ',stdout); cw++;
                while(nm[kk]&&cw<EXW){ fputc(nm[kk],stdout); cw++; kk++; }
                while(cw<EXW){ fputc(' ',stdout); cw++; }
                printf("\x1b[0m");
            } else {
                int kk; for(kk=0;kk<EXW;kk++) fputc(' ',stdout);
                printf("\x1b[0m");
            } } }
          }
          /* Emoji width varies by font; re-anchor code pane so code never shifts. */
          if(EXW) printf("\x1b[%d;%dH",r+2,EXW+1);
          if(e->path[0] && li<e->n){
              char *lb=e->ln[li].b; size_t LL=e->ln[li].len, kk=0;
              int col=0;
              size_t dd=0;
              int iscur=(li==e->cy && e->focus==0);
              printf(iscur?EC_LINEBG:EC_CODEBG);
              if(iscur) printf("\x1b[97m%*d    ",digits,(int)li+1);
              else printf("\x1b[90m%*d    ",digits,(int)li+1);
              while(kk<LL&&dd<e->dleft){ int w0=ed_chw((unsigned char)lb[kk],col); dd+=(size_t)w0; col+=w0; kk++; }
              while(kk<LL&&ed_is_cont((unsigned char)lb[kk])) kk++;
              ed_hl_bg=iscur?2:1;
              ed_hl_line(lb,LL,kk,(int)CW,is_luc,pcm+1,pst+1);
              ed_hl_bg=0;
              ed_scan_state(lb,LL,&pcm,&pst);
              printf(iscur?EC_LINEBG:EC_CODEBG);
              printf("\x1b[K\x1b[0m");
          } else { printf(EC_CODEBG "\x1b[K\x1b[0m"); }
          { int total=(int)e->n, rows=H-2, isthumb=0;
            if(total>rows){ int a=(int)((long long)r*total/rows), b=(int)((long long)(r+1)*total/rows);
              int v0=(int)e->top, v1=(int)(e->top+rows); if(v1>total) v1=total;
              if(a<v1&&b>v0) isthumb=1; }
            printf("\x1b[%d;%dH",r+2,W);
            if(isthumb) printf("\x1b[97;7m \x1b[0m"); else printf("\x1b[90m|\x1b[0m");
            printf("\x1b[%d;1H",r+3); }
      }
      { /* Bottom strip: message or unsaved count */
        int u=0;
        if(e->status[0]){
            char msg[256];
            snprintf(msg,sizeof msg,"  %s",e->status);
            msg[W-1]=0;
            printf(EC_TOPBG "\x1b[97m%s",msg);
        } else if((u=ed_unsaved_count(e))>0){
            char ub[64];
            snprintf(ub,sizeof ub,"  \xe2\x97\x8f %d unsaved",u);
            printf(EC_TOPBG "\x1b[97m%s",ub);
        } else {
            printf(EC_TOPBG "  ");
        }
        printf("\x1b[K\x1b[0m"); }
      /* Popup draws last, owns cursor */
      if(e->menu||e->naming) ed_popup_draw(e,W,H);
      /* Cursor hidden with no file or open menu */
      { size_t dc2=ed_dcol(e);
        if(e->menu) { /* hidden */ }
        else if(e->naming) { /* positioned by ed_popup_draw */ }
        else if(e->focus==1)
            printf("\x1b[%d;%dH\x1b[?25h",(int)(e->exsel-e->extop+4),2);
        else if(e->path[0])
            printf("\x1b[%d;%dH\x1b[?25h",(int)(e->cy-e->top+2),(int)(EXW+e->numw+dc2-e->dleft+1)); }
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
    if(fh==INVALID_HANDLE_VALUE){ e->exsel=0; e->extop=0; return; }
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
    e->exsel=0; e->extop=0;
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
    ExItem tmp[512]; size_t tn=0, i;
    if(idx>=e->exn || !e->ex[idx].isdir || e->ex[idx].expanded) return;
    snprintf(pat,sizeof pat,"%s\\*",e->ex[idx].full);
    fh=FindFirstFileA(pat,&fd);
    if(fh==INVALID_HANDLE_VALUE) return;
    do{
        if(!strcmp(fd.cFileName,".")||!strcmp(fd.cFileName,"..")) continue;
        if(tn>=512) break;
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
        snprintf(e->status,sizeof e->status,"too many files (showing %d)",(int)e->exn);
        return;
    }
    if(e->exn+tn>e->excap){ while(e->excap<e->exn+tn) e->excap=e->excap?e->excap*2:32;
        e->ex=(ExItem*)lrealloc(e->ex,sizeof(ExItem)*e->excap); }
    memmove(&e->ex[idx+1+tn],&e->ex[idx+1],(e->exn-idx-1)*sizeof(ExItem));
    memcpy(&e->ex[idx+1],tmp,tn*sizeof(ExItem));
    e->exn+=tn;
    e->ex[idx].expanded=1;
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

/* Open entry: dir toggles, file loads */
static void ex_open_idx(Editor *e,size_t idx){
    if(idx>=e->exn) return;
    e->exsel=idx;
    if(e->ex[idx].isdir){
        if(e->ex[idx].expanded) ex_collapse(e,idx); else ex_expand(e,idx);
        return;
    }
    ed_open_path(e,e->ex[idx].full);
}

/* Popup menu and file ops */
enum { MA_OPEN=1, MA_NEWFILE, MA_NEWDIR, MA_SAVEALL, MA_COPY, MA_RENAME, MA_PASTE,
       MA_DELETE, MA_DELYES, MA_DELNO };

/* Target dir: selected dir, file parent, or root */
static void ex_target_dir(Editor *e,char *out,size_t cap){
    if(e->exsel<e->exn && e->ex[e->exsel].isdir){
        snprintf(out,cap,"%s",e->ex[e->exsel].full);
        return;
    }
    if(e->exsel<e->exn && !e->ex[e->exsel].isdir){
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
    e->nitems++;
}

/* Kind 0 = root menu, 1 = context menu; ax/ay = mouse anchor (-1 = centered). */
static void ed_menu_open(Editor *e,int kind,int ax,int ay){
    char dir[1024];
    e->nitems=0; e->msel=0; e->menu=1; e->naming=0;
    e->menutitle[0]=0;
    if(ax>=0&&ay>=0){ e->menu_anch=1; e->menu_ax=ax; e->menu_ay=ay; }
    else { e->menu_anch=0; e->menu_ax=0; e->menu_ay=0; }
    if(kind==0){
        ex_target_dir(e,dir,sizeof dir);
        ed_menu_add(e,"new file",MA_NEWFILE);
        ed_menu_add(e,"new folder",MA_NEWDIR);
        if(e->fcb_ok) ed_menu_add(e,"paste here",MA_PASTE);
        ed_menu_add(e,"save all files",MA_SAVEALL);
        return;
    }
    if(e->exsel>=e->exn){ e->menu=0; return; }
    snprintf(e->menutitle,sizeof e->menutitle,"%s",e->ex[e->exsel].name);
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
    { size_t j; for(j=0;j<e->exn;j++) if(!strcmp(e->ex[j].full,full)){ e->exsel=j; break; } }
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
      if(fh==INVALID_HANDLE_VALUE) return 1;
      i=0;
      do{
          if(!strcmp(fd.cFileName,".")||!strcmp(fd.cFileName,"..")) continue;
          { char s2[1120], d2[1120];
            snprintf(s2,sizeof s2,"%s\\%s",src,fd.cFileName);
            snprintf(d2,sizeof d2,"%s\\%s",dst,fd.cFileName);
            if(!ed_copy_tree(s2,d2,depth+1)){ FindClose(fh); return 0; } }
          if(++i>2000) break;
      }while(FindNextFileA(fh,&fd));
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
    }
    i=0;
    while(i<e->nstash){
        if(ed_under(e->stash[i].path,full,isdir)){
            for(k=0;k<e->stash[i].n;k++) free(e->stash[i].ln[k].b);
            free(e->stash[i].ln);
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
}

/* Confirm delete; Cancel preselected for safety */
static void ed_delete_confirm_open(Editor *e){
    if(e->exsel>=e->exn) return;
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

static void ed_naming_start(Editor *e,int mode){
    e->naming=mode; e->namlen=0; e->namcur=0; e->namb[0]=0;
    e->namold[0]=0;
    if(mode==3){
        /* F2 rename selected */
        if(e->exsel>=e->exn){ e->naming=0; return; }
        { const char *f=e->ex[e->exsel].full, *b=NULL, *p;
          for(p=f;*p;p++) if(*p=='\\'||*p=='/') b=p;
          snprintf(e->namold,sizeof e->namold,"%s",f);
          if(b){ size_t n=(size_t)(b-f); if(n>=sizeof e->namdir) n=sizeof e->namdir-1;
              memcpy(e->namdir,f,n); e->namdir[n]=0; if(!e->namdir[0]) snprintf(e->namdir,sizeof e->namdir,"."); b++; }
          else { snprintf(e->namdir,sizeof e->namdir,"%s",e->exdir); b=f; }
          snprintf(e->namb,sizeof e->namb,"%s",b);
          e->namlen=strlen(e->namb); e->namcur=e->namlen; }
    } else {
        ex_target_dir(e,e->namdir,sizeof e->namdir);
    }
    e->menu=0;
}

static void ed_naming_cancel(Editor *e){ e->naming=0; e->namlen=0; }

static void ed_naming_confirm(Editor *e){
    size_t a=0, b=e->namlen;
    char name[256], dest[1120];
    while(a<b && (e->namb[a]==' '||e->namb[a]=='\t')) a++;
    while(b>a && (e->namb[b-1]==' '||e->namb[b-1]=='\t')) b--;
    if(b-a==0 || b-a>=sizeof name){ snprintf(e->status,sizeof e->status,"bad name"); e->naming=0; return; }
    memcpy(name,e->namb+a,b-a); name[b-a]=0;
    { size_t k; for(k=0;name[k];k++) if(name[k]=='/'||name[k]=='\\'){ snprintf(e->status,sizeof e->status,"bad name"); e->naming=0; return; }
      if(!strcmp(name,".")||!strcmp(name,"..")){ snprintf(e->status,sizeof e->status,"bad name"); e->naming=0; return; } }
    snprintf(dest,sizeof dest,"%s\\%s",e->namdir,name);
    if(e->naming==1){
        FILE *f=fopen(dest,"wb");
        if(!f){ snprintf(e->status,sizeof e->status,"cannot create '%s'",dest); e->naming=0; return; }
        fclose(f);
        e->naming=0;
        ex_reveal(e,dest);
        ed_open_path(e,dest);
        snprintf(e->status,sizeof e->status,"new file '%s'",name);
    } else if(e->naming==2){
        if(!CreateDirectoryA(dest,NULL)){ snprintf(e->status,sizeof e->status,"cannot create '%s'",dest); e->naming=0; return; }
        e->naming=0;
        ex_reveal(e,dest);
        snprintf(e->status,sizeof e->status,"new folder '%s'",name);
    } else {
        if(!strcmp(e->namold,dest)){ e->naming=0; return; }
        if(GetFileAttributesA(dest)!=INVALID_FILE_ATTRIBUTES){ snprintf(e->status,sizeof e->status,"'%s' exists",name); return; }
        if(!MoveFileA(e->namold,dest)){ snprintf(e->status,sizeof e->status,"cannot rename"); e->naming=0; return; }
        ed_rename_paths(e,e->namold,dest);
        e->naming=0;
        ex_reveal(e,dest);
        snprintf(e->status,sizeof e->status,"renamed to '%s'",name);
    }
}

/* Menu rows: top border, title, items, footer hint, bottom border (bh = nitems+4). */
static void ed_popup_geom(Editor *e,int W,int H,int *x0,int *y0,int *w,int *h){
    int bw, bh, i, maxw=0;
    if(e->naming){
        const char *t=e->naming==1?"new file":e->naming==2?"new folder":"rename";
        bw=46;
        for(i=0;t[i];i++){}
        if(bw>W-4) bw=W-4;
        *x0=(W-bw)/2; *y0=H/2-2; *w=bw; *h=5;
        return;
    }
    { int tl=e->menutitle[0]?(int)strlen(e->menutitle):4;
      if(tl>maxw) maxw=tl; }
    for(i=0;i<e->nitems;i++){ int L=(int)strlen(e->mitems[i].label); if(L>maxw) maxw=L; }
    { const char *ft="Esc / click outside to close";
      int fl=(int)strlen(ft); if(fl>maxw) maxw=fl; }
    bw=maxw+6; if(bw>W-4) bw=W-4; if(bw<26) bw=26;
    bh=e->nitems+4; if(bh>H-1) bh=H-1; if(bh<5) bh=5;
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
    if(e->naming){
        const char *t=e->naming==1?"new file":e->naming==2?"new folder":"rename";
        /* border */
        printf("\x1b[%d;%dH+",y0+1,x0+1);
        for(i=0;i<bw-2;i++) fputc('-',stdout);
        fputc('+',stdout);
        printf("\x1b[%d;%dH| %s",y0+2,x0+1,t);
        printf("\x1b[%d;%dH| [",y0+3,x0+1);
        { int fw=bw-6, k, off=0;
          if((int)e->namcur>fw-1) off=(int)e->namcur-(fw-1);
          for(k=0;k<fw;k++){ size_t q=(size_t)off+k; fputc(q<e->namlen?e->namb[q]:' ',stdout); }
          fputc(']',stdout); }
        printf("\x1b[%d;%dH+",y0+4,x0+1);
        for(i=0;i<bw-2;i++) fputc('-',stdout);
        fputc('+',stdout);
        printf("\x1b[%d;%dH",y0+3,x0+4+(int)(e->namcur-((e->namcur>(size_t)(bw-6-1))?(e->namcur-(bw-6-1)):0)));
        printf("\x1b[?25h");
        return;
    }
    /* Modern context menu: rounded box, title, highlight, footer hint */
    { int inner=bw-2, k;
      /* top border */
      printf("\x1b[%d;%dH\x1b[48;2;28;28;28m\x1b[38;2;90;90;90m\xe2\x95\xad",y0+1,x0+1);
      for(k=0;k<inner;k++) fputs("\xe2\x94\x80",stdout);
      fputs("\xe2\x95\xae\x1b[0m",stdout);
      /* title */
      { char tb[256]; const char *tt=e->menutitle[0]?e->menutitle:"menu";
        snprintf(tb,sizeof tb,"%.200s",tt);
        printf("\x1b[%d;%dH\x1b[48;2;28;28;28m\x1b[38;2;90;90;90m\xe2\x94\x82\x1b[0m",y0+2,x0+1);
        printf("\x1b[48;2;28;28;28m\x1b[97m %-.*s",inner-1,tb);
        { int tl=(int)strlen(tb); if(tl>inner-1) tl=inner-1;
          for(k=tl;k<inner-1;k++) fputc(' ',stdout); }
        printf("\x1b[48;2;28;28;28m\x1b[38;2;90;90;90m\xe2\x94\x82\x1b[0m"); }
      /* items: │ marker+label │, left/right borders stay dim */
      for(i=0;i<e->nitems;i++){
          int yy=y0+3+i;
          if(yy>=y0+bh-2) break;
          printf("\x1b[%d;%dH\x1b[48;2;28;28;28m\x1b[38;2;90;90;90m\xe2\x94\x82\x1b[0m",yy+1,x0+1);
          if(i==e->msel)
              printf("\x1b[48;2;0;120;215m\x1b[97m");
          else
              printf("\x1b[48;2;28;28;28m\x1b[37m");
          fputs(i==e->msel?"\xe2\x96\xb8 ":"  ",stdout);
          { int k2, L=(int)strlen(e->mitems[i].label);
            int maxl=inner-3;
            for(k2=0;k2<L&&k2<maxl;k2++) fputc(e->mitems[i].label[k2],stdout);
            for(;k2<maxl;k2++) fputc(' ',stdout); }
          fputc(' ',stdout);
          printf("\x1b[0m\x1b[48;2;28;28;28m\x1b[38;2;90;90;90m\xe2\x94\x82\x1b[0m");
      }
      /* footer hint */
      { int fy=y0+3+e->nitems;
        if(fy<y0+bh-1){
            printf("\x1b[%d;%dH\x1b[48;2;28;28;28m\x1b[38;2;90;90;90m\xe2\x94\x82\x1b[0m",fy+1,x0+1);
            printf("\x1b[48;2;28;28;28m\x1b[90m %-.*s",inner-1,"Esc / click outside to close");
            { const char *ft="Esc / click outside to close"; int fl=(int)strlen(ft);
              if(fl>inner-1) fl=inner-1;
              for(k=fl;k<inner-1;k++) fputc(' ',stdout); }
            printf("\x1b[48;2;28;28;28m\x1b[38;2;90;90;90m\xe2\x94\x82\x1b[0m");
        } }
      /* bottom border */
      printf("\x1b[%d;%dH\x1b[48;2;28;28;28m\x1b[38;2;90;90;90m\xe2\x95\xb0",y0+bh,x0+1);
      for(k=0;k<inner;k++) fputs("\xe2\x94\x80",stdout);
      fputs("\xe2\x95\xaf\x1b[0m",stdout);
    }
}

/* Menu pick */
static void ed_menu_pick(Editor *e,int id){
    char dir[1024];
    e->menu=0;
    if(id==MA_OPEN){ if(e->exsel<e->exn) ex_open_idx(e,e->exsel); }
    else if(id==MA_NEWFILE){ ex_target_dir(e,dir,sizeof dir); snprintf(e->namdir,sizeof e->namdir,"%s",dir); ed_naming_start(e,1); }
    else if(id==MA_NEWDIR){ ex_target_dir(e,dir,sizeof dir); snprintf(e->namdir,sizeof e->namdir,"%s",dir); ed_naming_start(e,2); }
    else if(id==MA_SAVEALL){ ed_save_all(e); }
    else if(id==MA_COPY){
        if(e->exsel>=e->exn) return;
        snprintf(e->fcb,sizeof e->fcb,"%s",e->ex[e->exsel].full);
        e->fcb_dir=e->ex[e->exsel].isdir; e->fcb_ok=1;
        snprintf(e->status,sizeof e->status,"copied '%s'",e->ex[e->exsel].name);
    }
    else if(id==MA_RENAME){ ed_naming_start(e,3); }
    else if(id==MA_DELETE){ ed_delete_confirm_open(e); }
    else if(id==MA_DELYES){ ed_delete_do(e); }
    /* MA_DELNO: just closes */
    else if(id==MA_PASTE){
        char cand[512], dest[1120], base[256];
        const char *b;
        if(!e->fcb_ok) return;
        ex_target_dir(e,dir,sizeof dir);
        b=e->fcb; { const char *p=b, *l=NULL; while(*p){ if(*p=='\\'||*p=='/') l=p; p++; } if(l) b=l+1; }
        snprintf(base,sizeof base,"%s",b);
        ed_unique(cand,sizeof cand,dir,base,e->fcb_dir);
        snprintf(dest,sizeof dest,"%s\\%s",dir,cand);
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
        if(ch==8){ if(e->namcur>0){ e->namcur--; memmove(&e->namb[e->namcur],&e->namb[e->namcur+1],e->namlen-e->namcur); if(e->namlen>0) e->namlen--; } return 1; }
        if(ch==0){
            if(vk==VK_LEFT&&e->namcur>0){ e->namcur--; return 1; }
            if(vk==VK_RIGHT&&e->namcur<e->namlen){ e->namcur++; return 1; }
            return 1;
        }
        if(ch>=32&&e->namlen+1<sizeof e->namb){
            /* BMP to UTF-8; astral via surrogates */
            unsigned long cp=ch;
            char tmp[4]; int m=0, k;
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
            if(e->msel<e->nitems) ed_menu_pick(e,e->mitems[e->msel].id);
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
    if(!e->menu&&!e->naming) return;
    ed_popup_geom(e,W,H,&x0,&y0,&bw,&bh);
    if(e->naming){ return; }   /* Outside clicks cancel, handled by caller */
    /* click outside -> dismiss (no action, like Cancel) */
    if(x<x0||x>=x0+bw||y<y0||y>=y0+bh){ e->menu=0; return; }
    /* items start at y0+2 (after top border + title) */
    for(i=0;i<e->nitems;i++){
        if(y==y0+2+i && x>=x0 && x<x0+bw){ e->msel=i; ed_menu_pick(e,e->mitems[i].id); return; }
    }
    /* clicks on border/title/footer do nothing (stay open) */
}

/* Wait for key after run */
static void wait_key(HANDLE hin){    INPUT_RECORD ir; DWORD n=0;
    for(;;){
        if(!ReadConsoleInputW(hin,&ir,1,&n)) return;
        if(ir.EventType==KEY_EVENT && ir.Event.KeyEvent.bKeyDown) return;
    }
}

/* Run .luc: save, show output, wait key, redraw */
static void ed_run(Editor *e,HANDLE hin,DWORD oldmode){
    const char *dot=strrchr(e->path,'.');
    if(!dot||(strcmp(dot,".luc")&&strcmp(dot,".LUC"))){
        snprintf(e->status,sizeof e->status,"only .luc files run here");
        return;
    }
    if(e->dirty && !ed_save(e)) return;
    if(!g_exepath[0]){ snprintf(e->status,sizeof e->status,"cannot find luc"); return; }
    SetConsoleMode(hin,oldmode);
    printf("\x1b[?7h\x1b[2J\x1b[H--- lcode run: %s ---\n",e->path);
    fflush(stdout);
    { const char *av[3]; av[0]=g_exepath; av[1]=e->path; av[2]=NULL;
      intptr_t rc=_spawnv(_P_WAIT,g_exepath,av);
      printf("\n--- exit %d --- press any key\n",(int)rc); }
    fflush(stdout);
    printf("\x1b[?7l");
    fflush(stdout);
    SetConsoleMode(hin,oldmode & ~(DWORD)(ENABLE_LINE_INPUT|ENABLE_ECHO_INPUT|ENABLE_PROCESSED_INPUT));
    wait_key(hin);
    e->status[0]=0;
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

/* Keyboard: 1 quits editor */
static int ed_key(Editor *e,HANDLE hin,DWORD old,WORD vk,WCHAR ch,DWORD ctl){
    int ctrl=(ctl&(LEFT_CTRL_PRESSED|RIGHT_CTRL_PRESSED))!=0;
    int alt=(ctl&(LEFT_ALT_PRESSED|RIGHT_ALT_PRESSED))!=0;
    if(alt&&!ctrl) return 0;   /* Ignore menu keys */
    if(ch==2){ e->show_ex=!e->show_ex; return 0; }   /* ^B toggles tree */
    if(e->focus==1){
        if(ch==27||ch==5){ e->focus=0; return 0; }        /* Esc back to code */
        if(ch==13){ ex_open_idx(e,e->exsel); return 0; }
        if(ch==8){ ex_join(e,".."); return 0; }
        if(!ctrl&&(ch=='n'||ch=='N')){ ex_target_dir(e,e->namdir,sizeof e->namdir); ed_naming_start(e,1); return 0; }
        if(!ctrl&&(ch=='d'||ch=='D')){ ex_target_dir(e,e->namdir,sizeof e->namdir); ed_naming_start(e,2); return 0; }
        if(ctrl&&ch==3){
            if(e->exsel>=e->exn) return 0;
            snprintf(e->fcb,sizeof e->fcb,"%s",e->ex[e->exsel].full);
            e->fcb_dir=e->ex[e->exsel].isdir; e->fcb_ok=1;
            snprintf(e->status,sizeof e->status,"copied '%s'",e->ex[e->exsel].name);
            return 0;
        }
        if(ctrl&&ch==22){
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
            if(vk==VK_UP&&e->exsel>0){ e->exsel--; e->status[0]=0; }
            else if(vk==VK_DOWN&&e->exsel+1<e->exn){ e->exsel++; e->status[0]=0; }
            else if(vk==VK_F2&&e->exsel<e->exn){ ed_naming_start(e,3); }
            else if(vk==VK_LEFT&&e->exsel<e->exn){
                if(e->ex[e->exsel].isdir&&e->ex[e->exsel].expanded) ex_collapse(e,e->exsel);
                else { size_t i=e->exsel, d=(size_t)e->ex[i].depth;
                    while(i>0&&e->ex[i-1].depth>=(int)d) i--;
                    e->exsel=i; }
                e->status[0]=0;
            }
            else if(vk==VK_RIGHT&&e->exsel<e->exn){
                if(e->ex[e->exsel].isdir&&!e->ex[e->exsel].expanded) ex_expand(e,e->exsel);
                else ex_open_idx(e,e->exsel);
                e->status[0]=0;
            }
        }
        return 0;
    }
    if(ch==27){
        int u=ed_unsaved_count(e);
        if(u>0&&!e->quit_arm){ snprintf(e->status,sizeof e->status,"%d unsaved - Esc again quits (loses them)",u); e->quit_arm=1; return 0; }
        return 1;
    }
    e->quit_arm=0;
    if(!e->path[0] && (ch==13||ch==8||ch==9||ch>=32||(ctrl&&(ch==3||ch==22)))){
        snprintf(e->status,sizeof e->status,"open a file from the left first");
        return 0;
    }
    if(ch==0){
        if(vk==VK_UP&&e->cy>0){ e->cy--; e->status[0]=0; }
        else if(vk==VK_DOWN&&e->cy+1<e->n){ e->cy++; e->status[0]=0; }
        else if(vk==VK_LEFT){ ed_move_left(e); e->status[0]=0; }
        else if(vk==VK_RIGHT){ ed_move_right(e); e->status[0]=0; }
        else if(vk==VK_OEM_3&&ctrl) ed_run(e,hin,old);   /* ^` runs */
        return 0;
    }
    if(ch==13){ ed_newline(e); return 0; }
    if(ch==8){ ed_backspace(e); return 0; }
    if(ch==9){ ed_insert_bytes(e,"    ",4); return 0; }
    if(ctrl){
        if(ch==3){ ed_copy_line(e); return 0; }
        if(ch==22){ if(e->clip) ed_insert_bytes(e,e->clip,e->cliplen);
            else snprintf(e->status,sizeof e->status,"clipboard empty (^C copies the line)"); return 0; }
        if(ch==19){ ed_save(e); return 0; }
        if(ch==18){ ed_run(e,hin,old); return 0; }
        if(ch==5){ e->focus=1; e->status[0]=0; return 0; }
        return 0;
    }
    if(ch>=32){
        if(ch>=0xD800&&ch<=0xDBFF){ e->surr_hi=ch; return 0; }
        { unsigned long cp;
          if(ch>=0xDC00&&ch<=0xDFFF&&e->surr_hi){ cp=0x10000UL+(((unsigned long)(e->surr_hi-0xD800))<<10)+(unsigned long)(ch-0xDC00); e->surr_hi=0; }
          else { e->surr_hi=0; cp=(unsigned long)ch; }
          ed_insert_cp(e,cp); }
        return 0;
    }
    return 0;
}

/* Click/wheel in buffer coords */
static void ed_mouse(Editor *e,MOUSE_EVENT_RECORD *m){
    HANDLE h=GetStdHandle(STD_OUTPUT_HANDLE);
    CONSOLE_SCREEN_BUFFER_INFO bi;
    int wx=0,wy=0;
    if(GetConsoleScreenBufferInfo(h,&bi)){ wx=bi.srWindow.Left; wy=bi.srWindow.Top; }
    if(m->dwEventFlags==MOUSE_WHEELED){
        int up=((short)(m->dwButtonState>>16))>0;
        int x=(int)m->dwMousePosition.X-wx;
        int H=e->vh>0?e->vh:24;
        int EXW=e->show_ex?ED_EXW:0;
        int rows=H>2?H-2:1;
        if(EXW&&x<EXW){
            if(up){ if(e->extop>=3) e->extop-=3; else e->extop=0; }
            else e->extop+=3;
            ed_clamp_view(e,H);
            /* keep selection visible so next draw doesn't snap back */
            if(e->exn>0){
                int erows=rows>2?rows-2:1;
                if((int)e->exsel<(int)e->extop) e->exsel=e->extop;
                if((int)e->exsel>=(int)(e->extop+erows)){
                    e->exsel=e->extop+erows-1;
                    if(e->exsel>=e->exn) e->exsel=e->exn-1;
                }
                e->focus=1;
            }
            e->status[0]=0;
        } else {
            if(up){ if(e->top>=3) e->top-=3; else e->top=0; }
            else e->top+=3;
            ed_clamp_view(e,H);
            /* keep cursor visible: wheel scrolls view + cursor together */
            if(e->n>0){
                if(e->cy<e->top) e->cy=e->top;
                if(e->cy>=e->top+(size_t)rows){
                    e->cy=e->top+(size_t)rows-1;
                    if(e->cy>=e->n) e->cy=e->n-1;
                }
                ed_clamp(e);
                e->focus=0;
            }
            e->status[0]=0;
        }
        return;
    }
    if(m->dwEventFlags!=0 && m->dwEventFlags!=DOUBLE_CLICK) return;
    { int x=(int)m->dwMousePosition.X-wx, y=(int)m->dwMousePosition.Y-wy;
      int H=e->vh>0?e->vh:24, W=e->vw>0?e->vw:80;
      int EXW=e->show_ex?ED_EXW:0;
      size_t CW=(size_t)(W-EXW)-e->numw; if((int)CW<8) CW=8;
      if(y<1||y>H-2||x<0) return;
      if(m->dwButtonState&RIGHTMOST_BUTTON_PRESSED){
          /* Right-click: file menu anchored at mouse, else nothing */
          if(EXW&&x<EXW){
              int r=y-1;
              e->focus=1;
              if(r==1||(r>=2 && e->extop+(r-2)<e->exn)){
                  if(r>=2) e->exsel=e->extop+(r-2);
                  ed_menu_open(e,1,x,y);
              } else ed_menu_open(e,0,x,y);
          }
          return;
      }
      if(!(m->dwButtonState&FROM_LEFT_1ST_BUTTON_PRESSED)) return;
      if(EXW&&x<EXW){
          int r=y-1;
          if(r==0){
              e->focus=1;
              if(x>=EXW-3) ed_menu_open(e,0,x,y);
              return;
          }
          if(r==1){ e->focus=1; ex_toggle_all(e); return; }
          size_t idx=e->extop+(r-2);
          if(idx<e->exn){ e->focus=1; ex_open_idx(e,idx); }
          return;
      }
      { size_t li=e->top+(y-1);
        if(li>=e->n) return;
        e->focus=0; e->status[0]=0;
        e->cy=li; ed_clamp(e);
        { size_t want=(x>=EXW+e->numw)?(size_t)(x-EXW-e->numw):0;
          char *b=e->ln[li].b; size_t L=e->ln[li].len;
          size_t k=0,d=0,col=0;
          while(k<L&&d<e->dleft+want){ int w=ed_chw((unsigned char)b[k],(int)col); d+=(size_t)w; col+=(size_t)w; k++; }
          while(k<L&&ed_is_cont((unsigned char)b[k])) k++;
          e->cx=k; } } }
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
    if((!hin||hin==INVALID_HANDLE_VALUE||!GetConsoleMode(hin,&m)) && !getenv("LUC_TESTDRAW")){
        printf("lcode needs a console (stdin is redirected).\n");
        return;
    }
    old=m;
    SetConsoleMode(hin,(old|ENABLE_MOUSE_INPUT|ENABLE_EXTENDED_FLAGS)
        & ~(DWORD)(ENABLE_LINE_INPUT|ENABLE_ECHO_INPUT|ENABLE_PROCESSED_INPUT|ENABLE_QUICK_EDIT_MODE));
    memset(&e,0,sizeof e);
    e.show_ex=1;
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
    /* Alt screen, no wrap: long rows truncate */
    printf("\x1b[?1049h\x1b[?7l");
    fflush(stdout);
    /* One write per frame: avoids flicker */
    setvbuf(stdout,NULL,_IOFBF,65536);
    for(;;){
        ed_clamp(&e);
        ed_draw(&e);
        for(;;){
            INPUT_RECORD ir; DWORD n=0;
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
                                int x0,y0,bw,bh;
                                int W=e.vw>0?e.vw:80, H=e.vh>0?e.vh:24;
                                int x=(int)ir.Event.MouseEvent.dwMousePosition.X-wx;
                                int y=(int)ir.Event.MouseEvent.dwMousePosition.Y-wy;
                                ed_popup_geom(&e,W,H,&x0,&y0,&bw,&bh);
                                if(x<x0||x>=x0+bw||y<y0||y>=y0+bh){ e.naming=0; break; }
                            } else {
                                ed_popup_click(&e,
                                    (int)ir.Event.MouseEvent.dwMousePosition.X-wx,
                                    (int)ir.Event.MouseEvent.dwMousePosition.Y-wy,
                                    e.vw>0?e.vw:80, e.vh>0?e.vh:24);
                                if(!e.menu) e.click_eat=1;
                                break;
                            }
                        }
                        if(bs&RIGHTMOST_BUTTON_PRESSED){ e.menu=0; e.naming=0; break; }
                    }
                    continue;
                }
                if(ir.EventType==WINDOW_BUFFER_SIZE_EVENT) break;
                if(ir.EventType!=KEY_EVENT||!ir.Event.KeyEvent.bKeyDown) continue;
                ed_popup_key(&e,ir.Event.KeyEvent.wVirtualKeyCode,
                             ir.Event.KeyEvent.uChar.UnicodeChar);
                break;
            }
            if(ir.EventType==MOUSE_EVENT){
                WORD f=ir.Event.MouseEvent.dwEventFlags;
                if(f==MOUSE_MOVED) continue;   /* Hover redraw flickers */
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
        ed_clamp(&e);
    }
done:
    SetConsoleMode(hin,old);
    ex_free(&e);
    ed_free(&e);
    { size_t si; for(si=0;si<e.nstash;si++){ size_t k; for(k=0;k<e.stash[si].n;k++) free(e.stash[si].ln[k].b); free(e.stash[si].ln); }
      free(e.stash); e.stash=NULL; e.nstash=0; }
    printf("\x1b[?25h\x1b[?7h\x1b[?1049l\n");
    fflush(stdout);
    setvbuf(stdout,NULL,_IONBF,0);
}
#else
static void lc_code(const char *start){
    (void)start;
    printf("lcode needs a Windows console for now.\n");
}
#endif

static void lcode_cmd(const char *line);

static void interactive_shell(void){
    char buf[512], key[64];
    for(;;){
        if(!repl_readline("|  \xe2\x9d\xaf ",buf,sizeof buf)) return;
        help_key(buf,key,sizeof key);
        if(!key[0]) continue;
        if(!strcmp(key,"exit")||!strcmp(key,"quit")) return;
        if(!strcmp(key,"help")){ repl_help_loop(); continue; }
        if(!strcmp(key,"credit")||!strcmp(key,"credits")){ printf("made by hsusulist\n"); continue; }
        if(!strcmp(key,"license")||!strcmp(key,"licence")){ printf("%s",LUC_LICENSE); continue; }
        if(!strcmp(key,"lcode")||!strcmp(key,"lccode")){ lcode_cmd(buf); continue; }
        printf("I know: help, lcode, credit, license. (Type 'exit' to quit.)\n");
    }
}

/* lcode [path]: open file, dir, or cwd (lccode kept as legacy alias) */
static void lcode_cmd(const char *line){
    const char *p=line;
    /* skip command word (lcode or lccode), keep [path] */
    while(*p && *p!=' ' && *p!='\t' && *p!='\r' && *p!='\n') p++;
    char path[1024];
    size_t n;
    while(*p==' '||*p=='\t') p++;
    n=strlen(p);
    while(n && (p[n-1]==' '||p[n-1]=='\t'||p[n-1]=='\r'||p[n-1]=='\n')) n--;
    if(n>=2 && ((p[0]=='"'&&p[n-1]=='"')||(p[0]=='\''&&p[n-1]=='\''))){ p++; n-=2; }
    if(n==0){ lc_code(NULL); return; }
    if(n>=sizeof path){ printf("path too long.\n"); return; }
    memcpy(path,p,n); path[n]=0;
    lc_code(path);
}

int main(int argc,char **argv){
    luc_win_attach_console();
    luc_win_utf8_console();
    if(argc>0) snprintf(g_exepath,sizeof g_exepath,"%s",argv[0]);
    luc_init();
    {   char *esrc=NULL; int elen=0;
        if(luc_aot_embedded(&esrc,&elen)){          /* Built executable */
            set_scriptdir(argv[0]);
            int rc=run_chunk(esrc,elen,"=(embedded)",argc,argv,1);
            free(esrc);
            return rc;
        }
    }

    if(argc<2){ print_banner(); if(repl_wanted()) interactive_shell(); return 0; }
    

    if(strcmp(argv[1],"--version")==0||strcmp(argv[1],"-v")==0){
        printf("%s  [C99 register VM + x86-64 JIT]\n",LUC_VERSION);
        return 0;
    }
    if(strcmp(argv[1],"--help")==0||strcmp(argv[1],"-h")==0){
        if(repl_wanted()){ print_banner(); repl_help_loop(); return 0; }
        print_help();
        return 0;
    }
    if(strcmp(argv[1],"-e")==0){
        if(argc<3){ fprintf(stderr,"luc: '-e' needs an argument\n"); return 1; }
        return run_chunk(argv[2],(int)strlen(argv[2]),"=(command line)",argc,argv,3);
    }
    if(strcmp(argv[1],"--edit")==0||strcmp(argv[1],"--lcode")==0
       ||strcmp(argv[1],"lcode")==0||strcmp(argv[1],"lccode")==0){
        lc_code(argc>2?argv[2]:NULL);
        return 0;
    }

    if(strcmp(argv[1],"build")==0){
        if(argc<3){ fprintf(stderr,"luc build: usage: luc build in.luc -o out.exe\n"); return 1; }
        const char *outp="out.exe";
        for(int i=3;i+1<argc;i++)
            if(strcmp(argv[i],"-o")==0){ outp=argv[i+1]; break; }
        int blen=0; char *bsrc=read_file(argv[2],&blen);
        if(!bsrc){ fprintf(stderr,"luc: cannot open '%s'\n",argv[2]); return 1; }
        int off=0; if(blen>1 && bsrc[0]=='#'){ while(off<blen && bsrc[off]!='\n') off++; }
        set_scriptdir(argv[2]);
        int rc=luc_aot_build(bsrc+off,blen-off,outp);
        free(bsrc);
    return rc;
    }

    if(strcmp(argv[1],"install")==0)
        return cmd_install(argc,argv);

    int len=0;
    char *src=read_file(argv[1],&len);
    if(!src){ fprintf(stderr,"luc: cannot open '%s'\n",argv[1]); return 1; }
/* Allow leading #! line */
    int off=0;
    if(len>1 && src[0]=='#'){ while(off<len && src[off]!='\n') off++; }
    set_scriptdir(argv[1]);
    int rc=run_chunk(src+off,len-off,argv[1],argc,argv,2);
    free(src);
    return rc;
}