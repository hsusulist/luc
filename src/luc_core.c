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
            if(isfinite(d) && d >= -0x1p63 && d < 0x1p63 && d == (double)(long long)d)
                return (unsigned)((long long)d)*2654435761u;
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
    int nc;
    if(t->ecap==0) nc=8;
    else {
        if(t->ecap > INT_MAX/2) luc_error("out of memory");
        nc=t->ecap*2;
    }
    Entry *old=t->ents; int oc=t->ecap;
    if((size_t)nc > ((size_t)-1)/sizeof(Entry)) luc_error("out of memory");
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
    if(n<0) luc_error("out of memory");
    int nc;
    if(t->acap==0) nc=8;
    else {
        if(t->acap > INT_MAX/2) luc_error("out of memory");
        nc=t->acap*2;
    }
    while(nc < n){
        if(nc > INT_MAX/2) luc_error("out of memory");
        nc *= 2;
    }
    if((size_t)nc > ((size_t)-1)/sizeof(Value)) luc_error("out of memory");
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
            const char *hex=s+2;
            lx->p+=2;
            if(lx->p>=lx->end||!isxdigit((unsigned char)*lx->p))
                lex_error(lx,"malformed hex number");
            while(lx->p<lx->end&&isxdigit((unsigned char)*lx->p)) lx->p++;
            errno=0;
            char *hend=NULL;
            unsigned long long hv=strtoull(hex,&hend,16);
            if(hend!=lx->p) lex_error(lx,"malformed hex number");
            if(errno==ERANGE) lex_error(lx,"hex number out of range");
            *num=(double)hv;
            return TK_NUMBER;
        }
        while(lx->p<lx->end&&isdigit((unsigned char)*lx->p)) lx->p++;
        if(lx->p<lx->end&&*lx->p=='.'){ lx->p++;
            while(lx->p<lx->end&&isdigit((unsigned char)*lx->p)) lx->p++; }
        if(lx->p<lx->end&&(*lx->p=='e'||*lx->p=='E')){
            const char *es=lx->p;
            lx->p++;
            if(lx->p<lx->end&&(*lx->p=='+'||*lx->p=='-')) lx->p++;
            int hasdig=0;
            while(lx->p<lx->end&&isdigit((unsigned char)*lx->p)){ lx->p++; hasdig=1; }
            if(!hasdig) lex_error(lx,"malformed number");
        }
        char tmp[64]; int len=(int)(lx->p-s);
        if(len>=(int)sizeof tmp) lex_error(lx,"malformed number");
        memcpy(tmp,s,(size_t)len); tmp[len]=0;
        char *tnumend=NULL;
        *num=strtod(tmp,&tnumend);
        if(tnumend!=tmp+len) lex_error(lx,"malformed number");
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
                    ps->inkey=1;
                    if(ps->lx.t!=']') sl->c=parse_expr(ps);
                    ps->inkey=0;
                    expect(ps,']'); e=sl; break;
                }
                ps->inkey=1;
                Expr *k=parse_expr(ps);
                ps->inkey=0;
                if(ps->lx.t==':'){                  /* t[a:b] */
                    lx_next(&ps->lx);
                    Expr *sl=new_expr(E_SLICE,line); sl->a=e; sl->b=k;
                    ps->inkey=1;
                    if(ps->lx.t!=']') sl->c=parse_expr(ps);
                    ps->inkey=0;
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
                if(!isfinite(d) || d!=floor(d)) return NIL;
                if(d < (double)INT_MIN || d > (double)INT_MAX) return NIL;
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
                if(!isfinite(d) || d!=floor(d)) return NIL;
                if(d < (double)INT_MIN || d > (double)INT_MAX) return NIL;
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
        if(!isfinite(d) || d!=floor(d)) luc_error("list index is not an integer");
        if(d < (double)INT_MIN || d > (double)INT_MAX) luc_error("list index out of range");
        int i=(int)d, n=AS_TAB(t)->alen;
        if(i<0) i+=n;
        if(i<0 || i>n || i==INT_MAX) luc_error("list index out of range");
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
    double d=checknum(L,base,nargs,i,fn);
    if(!isfinite(d) || d!=floor(d) || d < (double)INT_MIN || d > (double)INT_MAX)
        luc_error("%s: integer expected", fn);
    return (int)d;
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
    if(!isfinite(d) || d < 0.0 || d > 4294967295.0)
        luc_error("%s: value out of uint32 range", fn);
    return (uint32_t)d;
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

static char *try_file(const char *dir,const char *filename,int *len,char *found,size_t fcap);
/* Install fallback: <exe dir>/luc_modules and <exe dir>/../share/luc/luc_modules
   so `luc install ai` works without LUC_PATH (Linux has no installer). */
static int luc_exe_dir_path(char *out,size_t cap);
static char *try_exe_moddirs(const char *rel,int *len,char *found,size_t fcap,
                             char *(*tryfn)(const char*,const char*,int*,char*,size_t)){
    char ed[1024], d[1200]; char *src;
    if(!luc_exe_dir_path(ed,sizeof ed)) return NULL;
    snprintf(d,sizeof d,"%s/luc_modules",ed);
    if((src=tryfn(d,rel,len,found,fcap))) return src;
    snprintf(d,sizeof d,"%s/../share/luc/luc_modules",ed);
    if((src=tryfn(d,rel,len,found,fcap))) return src;
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
    if((src=try_exe_moddirs(rel,len,found,fcap,try_dir))) return src;
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
    if((src=try_exe_moddirs(rel,len,found,fcap,try_dir))) return src;
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
    if((src=try_exe_moddirs(rel,len,found,fcap,try_file))) return src;
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

static int luc_exe_dir_path(char *out,size_t cap){ return pkg_exe_dir(out,cap); }

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
                            if(!WinHttpQueryDataAvailable(hr,&avail)){ ok=0; break; }
                            if(avail==0) break;
                            char *buf=(char*)malloc(avail);
                            if(!buf){ ok=0; break; }
                            if(!WinHttpReadData(hr,buf,avail,&rd)){ ok=0; free(buf); break; }
                            if(rd==0) break;
                            if(fwrite(buf,1,rd,f)!=rd){ ok=0; free(buf); break; }
                            if(outsize) *outsize+=rd;
                            free(buf);
                        }
                        if(fclose(f)!=0) ok=0;
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

#if !defined(_WIN32)
/* POSIX: window support is compiled in (make WINDOW=1), not downloaded. */
static int pkg_install_window(int force){
    (void)force;
#ifdef LUC_WINDOW
    printf("window support is built into this luc binary (SDL2).\n");
    printf("optional extras: SDL2_ttf (text), SDL2_image (PNG/JPG), SDL2_mixer (sound)\n");
    printf("  Debian/Ubuntu: sudo apt install libsdl2-2.0-0 libsdl2-ttf-2.0-0 libsdl2-image-2.0-0 libsdl2-mixer-2.0-0\n");
    printf("try: luc demos/window_libaries/pong.luc\n");
    return 0;
#else
    printf("this luc binary was built without window support.\n");
    printf("on Linux/macOS, rebuild it with SDL2:\n");
    printf("  Debian/Ubuntu: sudo apt install libsdl2-dev libsdl2-ttf-dev libsdl2-image-dev libsdl2-mixer-dev\n");
    printf("  Fedora:        sudo dnf install SDL2-devel SDL2_ttf-devel SDL2_image-devel SDL2_mixer-devel\n");
    printf("  Arch:          sudo pacman -S sdl2 sdl2_ttf sdl2_image sdl2_mixer\n");
    printf("  macOS:         brew install sdl2 sdl2_ttf sdl2_image sdl2_mixer\n");
    printf("then: make WINDOW=1 && sudo make install\n");
    return 1;
#endif
}
#else
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

#endif /* _WIN32 pkg_install_window */

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
            if(cur){ if(fclose(cur)!=0){ free(src); return -1; } cur=NULL; }
            char name[256];
            size_t n2=wl-11;
            if(n2==0 || n2>=sizeof name){ free(src); if(cur) fclose(cur); return -1; }
            memcpy(name,line+11,n2); name[n2]=0;
            if(strchr(name,'/')||strchr(name,'\\')||strchr(name,':')||
               !strcmp(name,".")||!strcmp(name,"..")){
                free(src); if(cur) fclose(cur); return -1; }
            char out[1400];
            snprintf(out,sizeof out,"%s/%s",outdir,name);
            cur=fopen(out,"wb");
            if(!cur){ free(src); return -1; }
            count++;
        } else if(wl>=8 && !strncmp(line,"#=endpkg",8)){
            if(cur){ if(fclose(cur)!=0){ free(src); return -1; } cur=NULL; }
        } else if(wl>=9 && !strncmp(line,"#=lucpkg:",9)){
/* header, ignore */
        } else if(cur){
            if(fwrite(line,1,wl,cur)!=wl || fputc('\n',cur)==EOF){ /* track error */
                /* close and mark failure */
                fclose(cur); cur=NULL;
                free(src); return -1;
            }
        }
        i+=ll+(nl?1:0);
    }
    if(cur){ if(fclose(cur)!=0){ free(src); return -1; } cur=NULL; }
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
            fprintf(stderr,"  packages/ai.lucpkg folder next to the luc binary)\n");
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
            fprintf(stderr,"  packages/discord.lucpkg folder next to the luc binary)\n");
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
                if(!strcmp(PKGS[k].name,"window")){
#if defined(_WIN32)
                    snprintf(p,sizeof p,"%s/SDL2.dll",dir);
#elif defined(LUC_WINDOW)
                    snprintf(p,sizeof p,"/proc/self/exe");     /* compiled in */
#else
                    p[0]=0;
#endif
                }
                else if(!strcmp(PKGS[k].name,"ai")) snprintf(p,sizeof p,"%s/luc_modules/ai.luc",dir);
                else snprintf(p,sizeof p,"%s/luc_modules/discord.luc",dir);
                inst=p[0] && pkg_file_exists(p);
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
"  luc --install   open the installer (components, license, folder)\n"
"  luc install     packages (window, ai, discord)\n"
"  luc --help      full help\n");
}

/* ASCII banner synced with ascii2.txt; plain ASCII for all fonts */
static void print_ascii_banner(void){
    printf(
"██╗     ██╗   ██╗ ██████╗  Version: 0.2.0-beta-1\n"
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
"⠀⠀⠀⠀⠀⠀⣠⣶⣶⣿⣿⣿⣷⣶⣤⡀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀Luc 0.2.0-beta-1\n"
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
    "  --install           open the interactive installer\n"
    "  --install -c window,ai -d FOLDER -y   unattended install\n"
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
#define luc_win_utf8_console() ((void)0)
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
 "Ctrl+S saves, bottom shell runs commands, Esc quits."},
{"lcode",
 "lcode [path] opens LCode on ONE project folder (like VSCode).\n"
 "No path opens the current folder, except home/drive roots (refused).\n"
 "Tree: Enter expands, Left/Right collapse, click opens, wheel scrolls.\n"
 "Files: N new, D folder, F2 rename, ^C/^V copy/paste, right-click menu.\n"
 "Code gets colors. ^E switches panes, ^B hides the tree, ^F finds text.\n"
 "Drag the divider or panel title to resize. ^Shift+E moves the tree left/right.\n"
 "Bottom shell runs commands (Enter runs, Up/Down history, ^` shows/focuses/hides).\n"
 ":plugin lists plugins, :setting opens settings, :theme [dark|light] colors.\n"
        ":find <text> highlights every match in the open file (:find alone opens the Ctrl+F bar), :pfind searches all files, :run runs it.\n"
 ":help in the shell lists commands. Words complete with Tab as you type.\n"
 "^Z/^Y undo/redo, ^C copies selection first, else the line.\n"
 "F5 saves and runs the current file, F6 jumps to the next error, F8 toggles icons.\n"
 "Change shortcuts in :setting > Keybinds (Enter, press a combo, Enter).\n"
 "Nerd icons need the FiraCode Nerd Font Mono terminal font (bundled with the installer)."},
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

/* Tiny editor (`edit file.luc`); .luc/.lua/.py, UTF-8 safe.
   Win32 console API; on Linux/macOS luc_posix_console.h provides it. */
#if !defined(_WIN32)
#  include "luc_posix_console.h"
#  define ED_SEP  "/"
#  define ED_SEPC '/'
#else
#  define ED_SEP  "\\"
#  define ED_SEPC '\\'
#endif

typedef struct { char *b; size_t len, cap; } EdLine;

/* Parked buffer for unsaved file switch */
typedef struct { EdLine *ln; size_t n, cap, cx, cy, top; int dirty; char path[1024]; int is_binary; } EdStash;

typedef struct { const char *label; int id; } EdMenuItem;

/* lcode plugin: a plugin/ folder with plugin.cfg (example: the built-in luc) */
#define ED_PLUGMAX 8
#define ED_SETMAX 8
typedef struct {
    char name[64], author[64], desc[128];
    int enabled, locked;
    char cfg[1024];   /* plugin.cfg path, "" for the built-in fallback */
    char set_id[ED_SETMAX][32], set_label[ED_SETMAX][64];
    int set_val[ED_SETMAX], nset;
} Plug;

/* Configurable LCode shortcut: modifiers plus a virtual-key code */
#define ED_BINDMAX 16
typedef struct { int ctrl, alt, shift; WORD vk; } EdBind;

/* One undo step: whole-buffer snapshot (small files, so this stays cheap) */
typedef struct { char *data; size_t len, cy, cx, top; int dirty; } EdUndo;
#define ED_UNDOMAX 50   /* steps kept per buffer */

#define ED_LOGMAX 64  /* kept log lines (panel shows 1, or 3 when expanded) */
#define ED_SHHIST 32  /* shell command history */
#define ED_PFIND_MAX 1000
#define ED_DIAG_MAX 32

typedef struct {
    char text[192];
    char file[512];
    int line;
} EdPFind;

#define ED_FND_MAX 800
typedef struct {
    char text[160];   /* snippet of the line, or the file name */
    char file[300];   /* shown label (relative path or file name) */
    char full[520];   /* path to open */
    int line;
} EdFnd;

typedef struct {
    int line;
    char msg[128];
} EdDiag;

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
    int exsel_ok;             /* 1 = a tree row was explicitly selected */
    char exdir[1024];
    int focus;                /* 0 code, 1 files, 2 terminal */
    int show_ex;              /* Explorer visible */
    /* Explorer bar + terminal panel layout (plugin/config driven) */
    int exw;                  /* Explorer pane width, includes the 1-col divider */
    int ex_side;              /* 0 left, 1 right */
    char ex_title[64];        /* Explorer header text */
    char ex_content[16];      /* What the bar shows: "tree" */
    int show_panel;           /* Terminal panel visible */
    int panel_h;              /* Panel height in rows (title + output + input) */
    char panel_title[64];     /* Terminal panel header text */
    char panel_content[24];   /* What the panel shows: "terminal" */
    int drag_ex, drag_panel;  /* Live divider drag state */
    int vw, vh;               /* Last drawn size */
    unsigned short surr_hi;   /* Pending lead surrogate */
    char **rsav; size_t rsavn, rsavcap;  /* Collapse-all restore set */
    int root_shut;            /* Folder header shut */
    /* Parked dirty buffers */
    EdStash *stash; size_t nstash, scap;
    /* Popup menu */
    int menu; EdMenuItem mitems[8]; int nitems, msel;
    int click_eat;        /* Swallow double-click after pick */
    /* Inline naming input (VSCode-style row inside the tree) */
    int naming; /* 0 off, 1 new file, 2 new dir, 3 rename */
    char namb[256]; size_t namlen, namcur;
    char namdir[1024];   /* Target or parent dir */
    char namold[1024];   /* Rename source path */
    int namidx;           /* Tree row being named (-1 = none) */
    int namsel;           /* Whole name selected (typing replaces it) */
    int namxcol, namyrow; /* Screen cell for the inline caret (1-based) */
    int namtx0;           /* Display col where the name text starts */
    size_t namoff;        /* Left-scroll offset into namb (bytes) */
    size_t namsel0, namtop0;  /* Selection/scroll saved for cancel */
    int namok0;           /* exsel_ok saved for cancel */
    /* File clipboard */
    char fcb[1024]; int fcb_dir, fcb_ok;
    /* Popup anchored at mouse */
    int menu_anch, menu_ax, menu_ay;
    char menutitle[320];
    char menudir[1024];   /* target folder captured when the menu opened */
    /* Pending delete confirm */
    char deltarget[1024]; int delisdir;
    int is_binary;        /* current buffer is a binary placeholder (no save) */
    /* Text selection: anchor (byte col, line) + live end = cursor */
    int sel_on, sel_armed;
    size_t sel_ax, sel_ay;
    int icons_nerd;       /* 1 = Nerd Font devicons, 0 = no icons */
    /* Bottom shell: input line, history, and the log panel above it */
    char shbuf[1024]; size_t shlen, shcur;
    char shhist[ED_SHHIST][1024]; int shn, shpos;
    size_t sh_promptw;       /* display width of the shell prompt (for cursor) */
    int sh_quit;             /* shell asked to quit (exit/quit command) */
    char logs[ED_LOGMAX][240]; int logn;
    char lastlog[160];      /* last status already copied into logs */
    int log_expand, log_scroll; /* 1 = 3-row log view, scroll offset into history */
    int last_expand;      /* layout key of the last painted frame */
    /* Undo/redo stacks (cleared on file switch) */
    EdUndo *un, *rn; int un_n, un_cap, rn_n, rn_cap;
    int un_last_op; double un_last_time;
    int opt_guides;       /* tree indent guides (Appearance setting, default off) */
    /* Plugins: built-in luc + plugin.cfg folders, toggled in the :plugin UI */
    Plug plugs[ED_PLUGMAX]; int nplugs, plug_sel, plug_top, show_plugins;
    char plugdir[1024];
    /* Settings modal: left tabs, right content */
    int show_settings, set_tab, set_row, set_left, set_tab_top, set_row_top;
    int set_edit;             /* 1 = editing the focused text setting */
    char set_buf[64]; size_t set_len, set_cur;
    int set_cur_x, set_cur_y; /* caret cell while editing (1-based) */
    int set_full;             /* one full-frame repaint pending after a change */
    /* Keybind editor: 1 = waiting for a combo, 2 = combo captured, Enter confirms */
    int set_key_cap;
    EdBind set_key_pending;
    EdBind binds[ED_BINDMAX];
    /* Suggestions: filtered word list under the cursor (Tab accepts) */
    char sug_items[8][64], sug_tag[8][16], sug_px[64];
    int sug_n, sug_sel, sug_on;
    /* Find bar (Ctrl+F): borderless input + live match count */
    int find_on;             /* matches highlighted */
    int find_bar;            /* bar visible and owns the caret */
    char find_buf[128]; size_t find_len, find_cur;
    int find_n, find_idx;    /* total matches, 1-based current match */
    int find_cx, find_cy;    /* caret cell (1-based) */
    int pf_on, pf_n, pf_total, pf_trunc, pf_sel, pf_top, pf_scanned;
    char pf_buf[256]; size_t pf_len, pf_cur;
    EdPFind *pf_res; int pf_cap;
    /* Find panel (:find = text in file+project, :ff = file names) */
    int fnd_on, fnd_mode, fnd_n, fnd_cap, fnd_total, fnd_scanned, fnd_trunc;
    int fnd_sel, fnd_top, fnd_nofolder, fnd_cur_x, fnd_cur_y, fnd_vis;
    int cmdh_sel;             /* selected ':' command hint */
    char fnd_q[256]; size_t fnd_len, fnd_cur;
    EdFnd *fnd_res;
    int diag_n, diag_sel;
    EdDiag diag[ED_DIAG_MAX];
} Editor;

typedef struct ExItem { char *name; char *full; int isdir; int depth; int expanded; } ExItem;
#define ED_EX_MAX 2000   /* visible tree cap */

#define ED_EXW 28        /* default explorer pane width */
#define ED_EXW_MIN 14    /* keep the tree readable while dragging */
#define ED_PANEL_DEF 4   /* title + 2 output rows + input */
#define ED_PANEL_MIN 3   /* title + 1 output row + input */
#define ED_PANEL_MAX 20

/* Layout geometry: one shared source of truth for draw + mouse */
static int ed_ex_visible(Editor *e){ return e->show_ex && e->exw>1; }
static int ed_ex_width(Editor *e){ return ed_ex_visible(e)?e->exw-1:0; }
static int ed_ex_x(Editor *e,int W){
    return ed_ex_visible(e) ? (e->ex_side ? W-e->exw+1 : 0) : 0;
}
static int ed_div_x(Editor *e,int W){
    return e->ex_side ? W-e->exw : e->exw-1;
}
static int ed_code_x(Editor *e){
    return ed_ex_visible(e) && !e->ex_side ? e->exw : 0;
}
static int ed_scroll_x(Editor *e,int W){
    return ed_ex_visible(e) && e->ex_side ? W-e->exw-1 : W-1;
}
static int ed_code_w(Editor *e,int W){
    int cw=ed_scroll_x(e,W)-ed_code_x(e)-(int)e->numw;
    return cw>8?cw:8;
}
static int ed_panel_rows(Editor *e){ return e->show_panel?e->panel_h:0; }
static int ed_panel_top(int H,Editor *e){ return H-ed_panel_rows(e); }

static void ed_sel_delete(Editor *e);   /* defined with the copy helpers below */
static void ed_undo_push(Editor *e,int op);   /* defined with the undo engine below */
static void ex_free(Editor *e);   /* defined below */
static int ed_sys_copy(const char *s,size_t len);   /* Windows clipboard below */
static char *ed_sys_paste(size_t *outlen);   /* Windows clipboard below */

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

/* Keep scroll in range (code rows exclude title/log/shell) */
static int ed_logshow(Editor *e){
    int ph=ed_panel_rows(e);
    return ph>2?ph-2:0;
}
static int ed_crows(int H,Editor *e){
    int r=H-1-ed_panel_rows(e);
    return r>1?r:1;
}
static void ed_clamp_view(Editor *e,int H){
    size_t rows=(size_t)ed_crows(H,e);
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
    if(e->is_binary){ snprintf(e->status,sizeof e->status,"binary file is read-only"); return; }
    ed_undo_push(e,m<=4?1:0);
    if(e->sel_on) ed_sel_delete(e);
    if(m==0) return;
    { EdLine *l=&e->ln[e->cy];
      ed_line_reserve(l,m);
      memmove(l->b+e->cx+m,l->b+e->cx,l->len-e->cx+1);
      memcpy(l->b+e->cx,s,m);
      l->len+=m; e->cx+=m; e->dirty=1; e->status[0]=0; }
}

static void ed_backspace(Editor *e){
    ed_clamp(e);
    if(e->is_binary){ snprintf(e->status,sizeof e->status,"binary file is read-only"); return; }
    ed_undo_push(e,1);
    if(e->sel_on){ ed_sel_delete(e); return; }
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
    if(e->is_binary){ snprintf(e->status,sizeof e->status,"binary file is read-only"); return; }
    ed_undo_push(e,0);
    if(e->sel_on) ed_sel_delete(e);
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
      snprintf(e->status,sizeof e->status,"copied line %d",(int)e->cy+1);
      if(e->clip) ed_sys_copy(e->clip,e->cliplen); }
}

/* Text selection: ordered (sy,sx)-(ey,ex) between anchor and cursor */
static void ed_sel_norm(Editor *e,size_t *sy,size_t *sx,size_t *ey,size_t *ex){
    *sy=e->sel_ay; *sx=e->sel_ax; *ey=e->cy; *ex=e->cx;
    if(*ey<*sy||(*ey==*sy&&*ex<*sx)){
        size_t t=*sy;*sy=*ey;*ey=t; t=*sx;*sx=*ex;*ex=t;
    }
    /* stale anchor (file shrank underneath): clamp into range */
    if(e->n==0){ *sy=*ey=0; *sx=*ex=0; return; }
    if(*sy>=e->n) *sy=e->n-1;
    if(*ey>=e->n) *ey=e->n-1;
    if(*sx>e->ln[*sy].len) *sx=e->ln[*sy].len;
    if(*ex>e->ln[*ey].len) *ex=e->ln[*ey].len;
}
static int ed_sel_empty(Editor *e){
    size_t sy,sx,ey,ex;
    if(!e->sel_on) return 1;
    ed_sel_norm(e,&sy,&sx,&ey,&ex);
    return sy==ey&&sx==ex;
}
/* Delete the selected range; cursor lands on its start */
static void ed_sel_delete(Editor *e){
    size_t sy,sx,ey,ex,k;
    if(ed_sel_empty(e)) return;
    ed_sel_norm(e,&sy,&sx,&ey,&ex);
    if(sy==ey){
        EdLine *l=&e->ln[sy];
        memmove(l->b+sx,l->b+ex,l->len-ex+1);
        l->len-=ex-sx;
    } else {
        EdLine *a=&e->ln[sy], *b=&e->ln[ey];
        ed_line_reserve(a,b->len-ex);
        memcpy(a->b+sx,b->b+ex,b->len-ex);
        a->len=sx+(b->len-ex); a->b[a->len]=0;
        for(k=sy+1;k<=ey;k++) free(e->ln[k].b);
        memmove(&e->ln[sy+1],&e->ln[ey+1],(e->n-ey-1)*sizeof(EdLine));
        e->n-=ey-sy;
    }
    e->cy=sy; e->cx=sx; e->sel_on=0;
    e->dirty=1; e->status[0]=0;
}
/* Copy selection (with newlines) into the line clipboard */static void ed_copy_sel(Editor *e){
    size_t sy,sx,ey,ex,k,total=0;
    char *p;
    ed_sel_norm(e,&sy,&sx,&ey,&ex);
    for(k=sy;k<=ey;k++){
        EdLine *l=&e->ln[k];
        size_t a=(k==sy)?sx:0, b=(k==ey)?ex:l->len;
        if(b>a) total+=b-a;
        if(k<ey) total++;
    }
    free(e->clip); e->clip=NULL; e->cliplen=0;
    if(!total){ snprintf(e->status,sizeof e->status,"nothing selected"); return; }
    p=e->clip=(char*)lmalloc(total+1);
    for(k=sy;k<=ey;k++){
        EdLine *l=&e->ln[k];
        size_t a=(k==sy)?sx:0, b=(k==ey)?ex:l->len;
        if(b>a){ memcpy(p,l->b+a,b-a); p+=b-a; }
        if(k<ey) *p++='\n';
    }
    *p=0; e->cliplen=total;
    snprintf(e->status,sizeof e->status,"copied %d char(s)",(int)total);
    if(e->clip) ed_sys_copy(e->clip,e->cliplen);
}

#if defined(_WIN32)
/* Windows clipboard (UTF-8); lets ^C/^V cross into other apps */
static int ed_sys_copy(const char *s,size_t len){
    int wlen;
    HGLOBAL h; wchar_t *w;
    if(len==0||len>0x7FFFFF00) return 0;
    wlen=MultiByteToWideChar(CP_UTF8,0,s,(int)len,NULL,0);
    if(wlen<=0) return 0;
    h=GlobalAlloc(GMEM_MOVEABLE,(wlen+1)*sizeof(wchar_t));
    if(!h) return 0;
    w=(wchar_t*)GlobalLock(h);
    MultiByteToWideChar(CP_UTF8,0,s,(int)len,w,wlen); w[wlen]=0;
    GlobalUnlock(h);
    if(!OpenClipboard(NULL)){ GlobalFree(h); return 0; }
    EmptyClipboard();
    SetClipboardData(CF_UNICODETEXT,h);   /* system owns h now */
    CloseClipboard();
    return 1;
}
/* Returns malloc'd UTF-8 text or NULL; caller frees */
static char *ed_sys_paste(size_t *outlen){
    HGLOBAL h; const wchar_t *w; int n; char *s;
    *outlen=0;
    if(!OpenClipboard(NULL)) return NULL;
    h=GetClipboardData(CF_UNICODETEXT);
    if(!h){ CloseClipboard(); return NULL; }
    w=(const wchar_t*)GlobalLock(h);
    if(!w){ CloseClipboard(); return NULL; }
    n=WideCharToMultiByte(CP_UTF8,0,w,-1,NULL,0,NULL,NULL);
    if(n<=1){ GlobalUnlock(h); CloseClipboard(); return NULL; }
    s=(char*)malloc((size_t)n);
    if(!s){ GlobalUnlock(h); CloseClipboard(); return NULL; }
    WideCharToMultiByte(CP_UTF8,0,w,-1,s,n,NULL,NULL);
    GlobalUnlock(h); CloseClipboard();
    *outlen=(size_t)(n-1);
    return s;
}

#else
/* POSIX clipboard: wl-copy / xclip / xsel / pbcopy when present */
static const char *ed_clip_tool(int paste){
    static const char *cp[]={"wl-copy","xclip -selection clipboard -in","xsel --clipboard --input","pbcopy",NULL};
    static const char *ps[]={"wl-paste --no-newline","xclip -selection clipboard -out","xsel --clipboard --output","pbpaste",NULL};
    static const char *bin[]={"wl-copy","xclip","xsel","pbcopy"};
    static int pick=-2;
    if(pick==-2){
        int k; pick=-1;
        for(k=0;k<4;k++){
            char t[96];
            if(k==0 && !getenv("WAYLAND_DISPLAY")) continue;
            if((k==1||k==2) && !getenv("DISPLAY")) continue;
            snprintf(t,sizeof t,"command -v %s >/dev/null 2>&1",bin[k]);
            if(system(t)==0){ pick=k; break; }
        }
    }
    if(pick<0) return NULL;
    return paste?ps[pick]:cp[pick];
}
static int ed_sys_copy(const char *s,size_t len){
    const char *t=ed_clip_tool(0); FILE *p; char cmd[128];
    if(!t||len==0) return 0;
    snprintf(cmd,sizeof cmd,"%s 2>/dev/null",t);
    p=popen(cmd,"w"); if(!p) return 0;
    fwrite(s,1,len,p);
    return pclose(p)==0;
}
static char *ed_sys_paste(size_t *outlen){
    const char *t=ed_clip_tool(1); FILE *p; char *buf=NULL; size_t n=0,cap=0; char cmd[128];
    *outlen=0;
    if(!t) return NULL;
    snprintf(cmd,sizeof cmd,"%s 2>/dev/null",t);
    p=popen(cmd,"r"); if(!p) return NULL;
    for(;;){
        if(n+4096>cap){ char *nb; cap=cap?cap*2:8192; nb=(char*)realloc(buf,cap); if(!nb){ free(buf); pclose(p); return NULL; } buf=nb; }
        size_t r=fread(buf+n,1,4096,p);
        if(r==0) break;
        n+=r;
    }
    pclose(p);
    if(n==0){ free(buf); return NULL; }
    { size_t i,w=0; for(i=0;i<n;i++) if(buf[i]!='\r') buf[w++]=buf[i]; n=w; }   /* CRLF -> LF */
    buf[n]=0; *outlen=n;
    return buf;
}

#endif

/* Undo/redo: whole-buffer snapshots, typing coalesced by time */
static void ed_undo_ser(Editor *e,EdUndo *u){
    size_t total=0, k;
    char *p;
    for(k=0;k<e->n;k++) total+=e->ln[k].len+1;
    p=(char*)lmalloc(total+1);
    u->data=p; u->len=total;
    for(k=0;k<e->n;k++){
        memcpy(p,e->ln[k].b,e->ln[k].len); p+=e->ln[k].len; *p++='\n';
    }
    *p=0;
    u->cy=e->cy; u->cx=e->cx; u->top=e->top; u->dirty=e->dirty;
}
static void ed_undo_clear(Editor *e){
    int i;
    for(i=0;i<e->un_n;i++) free(e->un[i].data);
    for(i=0;i<e->rn_n;i++) free(e->rn[i].data);
    free(e->un); free(e->rn);
    e->un=e->rn=NULL; e->un_n=e->un_cap=e->rn_n=e->rn_cap=0;
    e->un_last_op=-1; e->un_last_time=0;
}
/* Snapshot before a mutation; op=1 typing-like (coalesced), op=0 always */
static void ed_undo_push(Editor *e,int op){
    EdUndo u;
    double now=luc_now();
    ed_clamp(e);
    if(op==1&&e->un_last_op==1&&now-e->un_last_time<1.5) return;
    ed_undo_ser(e,&u);
    if(e->un_n>=ED_UNDOMAX){
        free(e->un[0].data);
        memmove(&e->un[0],&e->un[1],sizeof(EdUndo)*(ED_UNDOMAX-1));
        e->un_n=ED_UNDOMAX-1;
    }
    if(e->un_n==e->un_cap){
        e->un_cap=e->un_cap?e->un_cap*2:16;
        e->un=(EdUndo*)lrealloc(e->un,sizeof(EdUndo)*(size_t)e->un_cap);
    }
    e->un[e->un_n++]=u;
    { int i; for(i=0;i<e->rn_n;i++) free(e->rn[i].data); e->rn_n=0; }
    e->un_last_op=op; e->un_last_time=now;
}
static void ed_undo_apply(Editor *e,EdUndo *u){
    size_t k, s=0, n=1, at=0;
    for(k=0;k<e->n;k++) free(e->ln[k].b);
    free(e->ln); e->ln=NULL; e->n=0; e->cap=0;
    for(k=0;k<u->len;k++) if(u->data[k]=='\n') n++;
    if(n>0&&u->len>0&&u->data[u->len-1]=='\n') n--;   /* drop trailing empty */
    if(n==0) n=1;
    e->ln=(EdLine*)lmalloc(sizeof(EdLine)*n); e->cap=n;
    for(k=0;k<n;k++){ e->ln[k].b=NULL; e->ln[k].len=0; e->ln[k].cap=0; }
    e->n=n;
    for(k=0;k<n;k++){
        size_t j=at;
        while(j<u->len&&u->data[j]!='\n') j++;
        { size_t el=j-at;
          ed_line_reserve(&e->ln[k],el);
          if(el) memcpy(e->ln[k].b,u->data+at,el);
          e->ln[k].b[el]=0; e->ln[k].len=el; }
        at=j+1;
    }
    e->cy=u->cy; e->cx=u->cx; e->top=u->top; e->dirty=u->dirty;
    e->sel_on=0; e->status[0]=0;
    ed_clamp(e);
}
static void ed_undo(Editor *e){
    EdUndo cur;
    if(!e->un_n){ snprintf(e->status,sizeof e->status,"nothing to undo"); return; }
    ed_undo_ser(e,&cur);
    if(e->rn_n==e->rn_cap){
        e->rn_cap=e->rn_cap?e->rn_cap*2:16;
        e->rn=(EdUndo*)lrealloc(e->rn,sizeof(EdUndo)*(size_t)e->rn_cap);
    }
    e->rn[e->rn_n++]=cur;
    { EdUndo u=e->un[--e->un_n];
      ed_undo_apply(e,&u); free(u.data); }
    e->un_last_op=-1;
}
static void ed_redo(Editor *e){
    EdUndo cur, u;
    if(!e->rn_n){ snprintf(e->status,sizeof e->status,"nothing to redo"); return; }
    ed_undo_ser(e,&cur);
    if(e->un_n>=ED_UNDOMAX){
        free(e->un[0].data);
        memmove(&e->un[0],&e->un[1],sizeof(EdUndo)*(ED_UNDOMAX-1));
        e->un_n=ED_UNDOMAX-1;
    }
    if(e->un_n==e->un_cap){
        e->un_cap=e->un_cap?e->un_cap*2:16;
        e->un=(EdUndo*)lrealloc(e->un,sizeof(EdUndo)*(size_t)e->un_cap);
    }
    e->un[e->un_n++]=cur;
    u=e->rn[--e->rn_n];
    ed_undo_apply(e,&u); free(u.data);
    e->un_last_op=-1;
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
    if(e->is_binary){ snprintf(e->status,sizeof e->status,"cannot save a binary file"); return 0; }
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
    s->dirty=e->dirty; s->is_binary=e->is_binary;
    snprintf(s->path,sizeof s->path,"%s",e->path);
    e->ln=NULL; e->n=0; e->cap=0; e->cx=0; e->cy=0; e->top=0; e->dleft=0; e->dirty=0;
    e->is_binary=0;
}

static int ed_unstash(Editor *e,const char *path){
    size_t i;
    for(i=0;i<e->nstash;i++) if(!strcmp(e->stash[i].path,path)){
        e->ln=e->stash[i].ln; e->n=e->stash[i].n; e->cap=e->stash[i].cap;
        e->cx=e->stash[i].cx; e->cy=e->stash[i].cy; e->top=e->stash[i].top;
        e->dirty=e->stash[i].dirty; e->is_binary=e->stash[i].is_binary;
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
    ed_undo_clear(e);   /* undo history belongs to the old buffer */
    e->sug_on=0;        /* suggestions belong to the old buffer too */
    e->find_on=0; e->find_bar=0; e->find_n=0; e->find_idx=0; e->find_cx=0; e->find_cy=0;
    e->diag_n=0; e->diag_sel=-1;
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
    if(e->dirty && !e->is_binary){
        if(e->path[0] && ed_save_to(e->ln,e->n,e->path)){ e->dirty=0; done++; }
        else unnamed++;
    }
    for(i=0;i<e->nstash;i++) if(e->stash[i].dirty && !e->stash[i].is_binary){
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
    e->n=0; e->cap=0; e->ln=NULL; e->is_binary=0;
    if(!src){ ed_ins_line(e,0); snprintf(e->status,sizeof e->status,"new file '%s'",e->path); return; }
    { int i;
      for(i=0;i<len;i++) if(!src[i]) break;
      if(i<len){
          ed_ins_line(e,0);
          { const char *msg="[binary file - not shown]";
            EdLine *l=&e->ln[0];
            ed_line_reserve(l,strlen(msg));
            memcpy(l->b,msg,strlen(msg)+1); l->len=strlen(msg); }
          e->is_binary=1;
          snprintf(e->status,sizeof e->status,"binary file '%s' - not opened",e->path);
          free(src);
          return;
      } }
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
    free(e->ln); free(e->clip); free(e->pf_res); e->pf_res=NULL; e->pf_cap=0;
    ex_free(e);
    ed_undo_clear(e);
    for(i=0;i<e->nstash;i++){ size_t k; for(k=0;k<e->stash[i].n;k++) free(e->stash[i].ln[k].b); free(e->stash[i].ln); }
    free(e->stash); e->stash=NULL; e->nstash=0; e->scap=0;
}

/* LC syntax colors */
#define EC_NONE 0
#define EC_KEY 1     /* Keywords */
#define EC_STR 2     /* Strings */
#define EC_NUM 3     /* Numbers */
#define EC_COM 4     /* Comments */
#define EC_FN 5      /* Calls */
#define EC_DIR 6     /* Directives */
static char EC_SEQ[7][32]={ "",
    "38;2;197;134;192", "38;2;206;145;120", "38;2;181;206;168",
    "38;2;106;153;85", "38;2;220;220;170", "38;2;86;156;214" };

/* Syntax languages gated by the luc plugin */
enum {
    L_NONE=0, L_LUC, L_C, L_PY, L_JS, L_LUA, L_PS, L_SH, L_JSON, L_MD,
    L_JAVA, L_GO, L_RUST, L_CS, L_KOTLIN, L_SWIFT, L_DART, L_SCALA,
    L_PHP, L_CSS, L_HTML, L_SQL, L_RUBY, L_PERL, L_R, L_YAML, L_TOML,
    L_DOCKER, L_MAKE, L_INI, L_HCL
};
static int ed_lang(const char *path){
    const char *d=strrchr(path,'.');
    const char *base=path, *p;
    char e[16]; size_t k;
    for(p=path;*p;p++) if(*p=='/'||*p=='\\') base=p+1;
    if(!_stricmp(base,"dockerfile")) return L_DOCKER;
    if(!_stricmp(base,"makefile")||!_stricmp(base,"gnumakefile")) return L_MAKE;
    if(!_stricmp(base,"cmakelists.txt")) return L_MAKE;
    if(!_stricmp(base,".gitignore")||!_stricmp(base,".env")) return L_SH;
    if(!d||d==path||!d[1]) return L_NONE;
    d++;
    for(k=0;k<sizeof e-1&&d[k];k++){
        char c=d[k]; e[k]=(c>='A'&&c<='Z')?(char)(c+32):c;
    }
    e[k]=0;
    if(!strcmp(e,"luc")) return L_LUC;
    if(!strcmp(e,"c")||!strcmp(e,"h")||!strcmp(e,"cpp")||!strcmp(e,"hpp")||
       !strcmp(e,"cc")||!strcmp(e,"cxx")) return L_C;
    if(!strcmp(e,"py")) return L_PY;
    if(!strcmp(e,"js")||!strcmp(e,"mjs")||!strcmp(e,"ts")||!strcmp(e,"tsx")||
       !strcmp(e,"jsx")) return L_JS;
    if(!strcmp(e,"lua")) return L_LUA;
    if(!strcmp(e,"ps1")) return L_PS;
    if(!strcmp(e,"sh")||!strcmp(e,"bash")||!strcmp(e,"zsh")) return L_SH;
    if(!strcmp(e,"json")) return L_JSON;
    if(!strcmp(e,"md")||!strcmp(e,"markdown")) return L_MD;
    if(!strcmp(e,"java")) return L_JAVA;
    if(!strcmp(e,"go")) return L_GO;
    if(!strcmp(e,"rs")) return L_RUST;
    if(!strcmp(e,"cs")) return L_CS;
    if(!strcmp(e,"kt")||!strcmp(e,"kts")) return L_KOTLIN;
    if(!strcmp(e,"swift")) return L_SWIFT;
    if(!strcmp(e,"dart")) return L_DART;
    if(!strcmp(e,"scala")||!strcmp(e,"sc")) return L_SCALA;
    if(!strcmp(e,"php")||!strcmp(e,"phtml")) return L_PHP;
    if(!strcmp(e,"css")||!strcmp(e,"scss")||!strcmp(e,"less")) return L_CSS;
    if(!strcmp(e,"html")||!strcmp(e,"htm")||!strcmp(e,"xml")||!strcmp(e,"svg")) return L_HTML;
    if(!strcmp(e,"sql")) return L_SQL;
    if(!strcmp(e,"rb")||!strcmp(e,"rake")||!strcmp(e,"gemspec")) return L_RUBY;
    if(!strcmp(e,"pl")||!strcmp(e,"pm")) return L_PERL;
    if(!strcmp(e,"r")) return L_R;
    if(!strcmp(e,"yaml")||!strcmp(e,"yml")) return L_YAML;
    if(!strcmp(e,"toml")) return L_TOML;
    if(!strcmp(e,"ini")||!strcmp(e,"cfg")||!strcmp(e,"conf")) return L_INI;
    if(!strcmp(e,"tf")||!strcmp(e,"hcl")) return L_HCL;
    return L_NONE;
}
static int ed_is_kw_lang(int lang,const char *s,size_t n){
    static const char *c_kw[]={
        "auto","break","case","char","const","continue","default","do","double",
        "else","enum","extern","float","for","goto","if","int","long","return",
        "short","signed","sizeof","static","struct","switch","typedef","union",
        "unsigned","void","volatile","while","NULL","true","false",NULL};
    static const char *py_kw[]={
        "False","None","True","and","as","assert","break","class","continue",
        "def","elif","else","except","finally","for","from","global","if",
        "import","in","is","lambda","not","or","pass","raise","return","try",
        "while","with","yield","self","print",NULL};
    static const char *js_kw[]={
        "await","break","case","catch","class","const","continue","debugger",
        "default","delete","do","else","export","extends","finally","for",
        "function","if","import","in","instanceof","let","new","of","return",
        "super","switch","this","throw","try","typeof","var","void","while",
        "with","yield","true","false","null","undefined",NULL};
    static const char *lua_kw[]={
        "and","break","do","else","elseif","end","false","for","function","if",
        "in","local","nil","not","or","repeat","return","then","true","until",
        "while","print","require",NULL};
    static const char *ps_kw[]={
        "function","param","return","if","else","elseif","foreach","for",
        "while","do","switch","break","continue","true","false","null","try",
        "catch","throw",NULL};
    static const char *json_kw[]={"true","false","null",NULL};
    static const char *jvm_kw[]={
        "abstract","as","assert","boolean","break","byte","case","catch","char",
        "class","const","continue","def","default","do","double","else","enum",
        "extends","false","final","finally","float","for","fun","if","implements",
        "import","in","instanceof","int","interface","is","long","new","null",
        "object","override","package","private","protected","public","return",
        "short","static","super","switch","synchronized","this","throw","throws",
        "trait","true","try","val","var","void","volatile","when","while",NULL};
    static const char *go_kw[]={
        "break","case","chan","const","continue","default","defer","else","fallthrough",
        "false","for","func","go","goto","if","import","interface","iota","map",
        "package","range","return","select","struct","switch","true","type","var",
        "nil","bool","byte","complex64","complex128","error","float32","float64",
        "int","int8","int16","int32","int64","rune","string","uint","uint8",
        "uint16","uint32","uint64","uintptr",NULL};
    static const char *rust_kw[]={
        "as","async","await","break","const","continue","crate","dyn","else","enum",
        "extern","false","fn","for","if","impl","in","let","loop","match","mod",
        "move","mut","pub","ref","return","self","Self","static","struct","super",
        "trait","true","type","unsafe","use","where","while","i8","i16","i32","i64",
        "i128","isize","u8","u16","u32","u64","u128","usize","f32","f64","bool",
        "char","str",NULL};
    static const char *cs_kw[]={
        "abstract","as","async","await","base","bool","break","byte","case","catch",
        "char","checked","class","const","continue","decimal","default","delegate",
        "do","double","else","enum","event","explicit","extern","false","finally",
        "fixed","float","for","foreach","goto","if","implicit","in","int","interface",
        "internal","is","lock","long","namespace","new","null","object","operator",
        "out","override","params","private","protected","public","readonly","ref",
        "return","sbyte","sealed","short","sizeof","stackalloc","static","string",
        "struct","switch","this","throw","true","try","typeof","uint","ulong",
        "unchecked","unsafe","ushort","using","var","virtual","void","volatile",
        "while",NULL};
    static const char *swift_kw[]={
        "associatedtype","as","async","await","break","case","catch","class","continue",
        "defer","do","else","enum","extension","fallthrough","false","for","func",
        "guard","if","import","in","init","inout","let","nil","operator","private",
        "protocol","public","repeat","return","self","static","struct","subscript",
        "super","switch","throw","throws","true","try","typealias","var","where","while",
        "Int","String","Bool","Double",NULL};
    static const char *dart_kw[]={
        "as","assert","async","await","break","case","catch","class","const","continue",
        "default","deferred","do","dynamic","else","enum","export","extends","false",
        "final","finally","for","Function","get","hide","if","implements","import",
        "in","is","late","library","mixin","new","null","of","on","operator","part",
        "required","rethrow","return","sealed","set","show","static","super","switch",
        "sync","this","throw","true","try","typedef","var","void","when","while","with",
        "yield",NULL};
    static const char *php_kw[]={
        "abstract","and","array","as","break","callable","case","catch","class","clone",
        "const","continue","declare","default","do","echo","else","elseif","empty",
        "endfor","endforeach","endif","endswitch","endwhile","eval","exit","extends",
        "false","final","finally","for","foreach","function","global","if","implements",
        "include","instanceof","interface","isset","list","namespace","new","null","or",
        "print","private","protected","public","require","return","static","switch",
        "throw","trait","true","try","unset","use","var","while","xor","yield",NULL};
    static const char *sql_kw[]={
        "add","all","alter","and","as","asc","begin","between","by","case","check",
        "column","commit","constraint","create","cross","database","delete","desc",
        "distinct","drop","else","end","exists","foreign","from","full","group","having",
        "inner","insert","into","is","join","key","left","like","limit","not","null",
        "offset","on","or","order","outer","primary","references","right","rollback",
        "select","set","table","then","true","false","union","unique","update","using",
        "values","view","when","where",NULL};
    static const char *ruby_kw[]={
        "alias","and","begin","break","case","class","def","defined","do","else","elsif",
        "end","ensure","false","for","if","in","module","next","nil","not","or","redo",
        "rescue","retry","return","self","super","then","true","undef","unless","until",
        "when","while","yield",NULL};
    static const char *perl_kw[]={
        "and","BEGIN","bless","case","continue","do","else","elsif","END","eval","for",
        "foreach","given","if","last","local","my","next","no","not","our","package",
        "redo","require","return","state","sub","unless","until","use","when","while",
        "eq","ne",NULL};
    static const char *r_kw[]={
        "break","else","FALSE","for","function","if","in","Inf","NA","NaN","next",
        "NULL","repeat","return","TRUE","while",NULL};
    static const char *yaml_kw[]={"false","null","true","yes","no","on","off",NULL};
    static const char *html_kw[]={
        "a","article","aside","audio","body","button","canvas","code","div","em","footer",
        "form","h1","h2","h3","head","header","html","iframe","img","input","label","li",
        "link","main","meta","nav","ol","option","p","script","section","select","small",
        "span","src","strong","style","table","td","textarea","th","title","tr","ul",
        "class","for","href","id","lang","name","rel","src","target","type","value",NULL};
    static const char *css_kw[]={
        "absolute","auto","block","bold","border","color","display","fixed","flex","grid",
        "hidden","inherit","inline","important","initial","margin","none","padding","relative",
        "static","transparent","unset","visible",NULL};
    const char **t=NULL;
    int i;
    if(lang==L_C) t=c_kw;
    else if(lang==L_PY) t=py_kw;
    else if(lang==L_JS) t=js_kw;
    else if(lang==L_LUA) t=lua_kw;
    else if(lang==L_PS) t=ps_kw;
    else if(lang==L_JSON) t=json_kw;
    else if(lang==L_JAVA||lang==L_KOTLIN||lang==L_SCALA) t=jvm_kw;
    else if(lang==L_GO) t=go_kw;
    else if(lang==L_RUST) t=rust_kw;
    else if(lang==L_CS) t=cs_kw;
    else if(lang==L_SWIFT) t=swift_kw;
    else if(lang==L_DART) t=dart_kw;
    else if(lang==L_PHP) t=php_kw;
    else if(lang==L_SQL) t=sql_kw;
    else if(lang==L_RUBY) t=ruby_kw;
    else if(lang==L_PERL) t=perl_kw;
    else if(lang==L_R) t=r_kw;
    else if(lang==L_YAML||lang==L_TOML) t=yaml_kw;
    else if(lang==L_HTML) t=html_kw;
    else if(lang==L_CSS) t=css_kw;
    else return 0;
    for(i=0;t[i];i++){ size_t k=0; while(k<n&&t[i][k]){
            unsigned char a=(unsigned char)t[i][k], b=(unsigned char)s[k];
            if(lang==L_SQL){
                if(a>='A'&&a<='Z') a=(unsigned char)(a+32);
                if(b>='A'&&b<='Z') b=(unsigned char)(b+32);
            }
            if(a!=b) break;
            k++;
        }
        if(k==n&&t[i][k]==0) return 1; }
    return 0;
}
/* Leading whitespace only before byte k (for # directives/headers) */
static int ed_is_lead(char *b,size_t k){
    while(k>0){ k--; if(b[k]!=' '&&b[k]!='\t') return 0; }
    return 1;
}
static int ed_is_cstyle_lang(int lang){
    return lang==L_C||lang==L_JS||lang==L_JAVA||lang==L_GO||lang==L_RUST||
        lang==L_CS||lang==L_KOTLIN||lang==L_SWIFT||lang==L_DART||
        lang==L_SCALA||lang==L_PHP||lang==L_CSS||lang==L_SQL||lang==L_HCL;
}
static int ed_is_hash_lang(int lang){
    return lang==L_PY||lang==L_SH||lang==L_PS||lang==L_RUBY||
        lang==L_PHP||lang==L_PERL||lang==L_R||lang==L_YAML||lang==L_TOML||
        lang==L_DOCKER||lang==L_MAKE||lang==L_INI||lang==L_HCL;
}
static int ed_is_dash_comment_lang(int lang){
    return lang==L_LUC||lang==L_LUA||lang==L_SQL;
}
static int ed_is_slash_comment_lang(int lang){
    return ed_is_cstyle_lang(lang)&&lang!=L_CSS;
}
static int ed_has_c_block_comment(int lang){
    return ed_is_cstyle_lang(lang);
}

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

/* Pane + selection backgrounds and bright/dim text (rewritten by :theme) */
static char EC_LINEBG[32]="\x1b[48;2;42;45;46m";   /* Cursor-line bg */
static char EC_CODEBG[32]="\x1b[48;2;30;30;30m";   /* Code pane bg */
static char EC_EXBG[32]="\x1b[48;2;17;17;17m";     /* Explorer pane bg */
static char EC_TOPBG[32]="\x1b[48;2;22;22;22m";    /* Top strip bg */
static char EC_LOGBG[32]="\x1b[48;2;37;37;37m";    /* Log panel bg */
static char EC_SELBG[32]="\x1b[48;2;38;79;120m";   /* Text selection bg */
static char TH_TCUR[32]="\x1b[97m";                /* Bright text (gutter current, title) */
static char TH_TDIM[32]="\x1b[90m";                /* Dim text (gutter, hints) */

typedef struct { const char *name;
    const char *code,*line,*ex,*top,*log,*sel;
    const char *key,*str,*num,*com,*fn,*dir;
    const char *tcur,*tdim; } ThemeDef;
static const ThemeDef THEMES[]={
    {"dark","48;2;30;30;30","48;2;42;45;46","48;2;17;17;17","48;2;22;22;22",
     "48;2;37;37;37","48;2;38;79;120",
     "38;2;197;134;192","38;2;206;145;120","38;2;181;206;168","38;2;106;153;85",
     "38;2;220;220;170","38;2;86;156;214","97","90"},
    {"light","48;2;255;255;255","48;2;240;240;240","48;2;243;243;243","48;2;225;225;226",
     "48;2;248;248;248","48;2;173;214;255",
     "38;2;175;0;192","38;2;163;21;21","38;2;9;134;88","38;2;0;128;0",
     "38;2;121;94;38","38;2;0;0;255","30","38;2;110;110;110"},
    {NULL,NULL,NULL,NULL,NULL,NULL,NULL,NULL,NULL,NULL,NULL,NULL,NULL,NULL,NULL}
};
#define ED_NTHEMES 2
static char ed_theme_cur[16]="dark";
static int ed_theme_apply(const char *name){
    int i;
    for(i=0;i<ED_NTHEMES;i++){
        const ThemeDef *t=&THEMES[i];
        if(strcmp(t->name,name)) continue;
        snprintf(EC_CODEBG,sizeof EC_CODEBG,"\x1b[%sm",t->code);
        snprintf(EC_LINEBG,sizeof EC_LINEBG,"\x1b[%sm",t->line);
        snprintf(EC_EXBG,sizeof EC_EXBG,"\x1b[%sm",t->ex);
        snprintf(EC_TOPBG,sizeof EC_TOPBG,"\x1b[%sm",t->top);
        snprintf(EC_LOGBG,sizeof EC_LOGBG,"\x1b[%sm",t->log);
        snprintf(EC_SELBG,sizeof EC_SELBG,"\x1b[%sm",t->sel);
        snprintf(EC_SEQ[1],sizeof EC_SEQ[1],"%s",t->key);
        snprintf(EC_SEQ[2],sizeof EC_SEQ[2],"%s",t->str);
        snprintf(EC_SEQ[3],sizeof EC_SEQ[3],"%s",t->num);
        snprintf(EC_SEQ[4],sizeof EC_SEQ[4],"%s",t->com);
        snprintf(EC_SEQ[5],sizeof EC_SEQ[5],"%s",t->fn);
        snprintf(EC_SEQ[6],sizeof EC_SEQ[6],"%s",t->dir);
        snprintf(TH_TCUR,sizeof TH_TCUR,"\x1b[%sm",t->tcur);
        snprintf(TH_TDIM,sizeof TH_TDIM,"\x1b[%sm",t->tdim);
        snprintf(ed_theme_cur,sizeof ed_theme_cur,"%s",t->name);
        return 1;
    }
    return 0;
}
static int ed_hl_bg = 0;   /* 0 plain, 1 code bg, 2 line bg */
static int ed_hl_wantsel = 0; /* current byte is inside the text selection */
static int ed_hl_selon = 0;   /* selection bg currently on */
#define ED_HL_FINDMAX 64
static int ed_hl_find_n = 0;          /* find matches on the line being drawn */
static size_t ed_hl_find_s[ED_HL_FINDMAX], ed_hl_find_e[ED_HL_FINDMAX];
static int ed_hl_find_cur = -1;       /* index of the active match, -1 none */
static int ed_hl_find_on = 0;         /* 0 none, 1 match, 2 active match */
static int ed_hl_findbg = 0;          /* active match line uses selection bg */
static size_t ed_hl_k = 0;            /* byte currently being emitted */
static const char EC_FINDALL[32]="\x1b[38;2;235;70;70m";
static const char EC_FINDCUR[32]="\x1b[1;38;2;235;70;70m";
static int ed_hl_find_state(size_t k){
    int i;
    for(i=0;i<ed_hl_find_n;i++)
        if(k>=ed_hl_find_s[i]&&k<ed_hl_find_e[i]) return (i==ed_hl_find_cur)?2:1;
    return 0;
}
static void ed_hl_emit(int code,int *cur){
    int st=ed_hl_find_n?ed_hl_find_state(ed_hl_k):0;
    if(code!=*cur||st!=ed_hl_find_on){
        if(st==2) printf("%s",EC_FINDCUR);
        else if(st==1) printf("%s",EC_FINDALL);
        else if(code) printf("\x1b[%sm",EC_SEQ[code]);
        else printf("\x1b[0m");
        if(ed_hl_findbg) printf(EC_SELBG);
        else if(ed_hl_bg==2) printf(EC_LINEBG);
        else if(ed_hl_bg==1) printf(EC_CODEBG);
        *cur=code; ed_hl_selon=0; ed_hl_find_on=st;
    }
    /* selection sits on top of syntax colors (bg swap keeps fg readable) */
    if(ed_hl_wantsel&&!ed_hl_selon){ printf(EC_SELBG); ed_hl_selon=1; }
    else if(!ed_hl_wantsel&&ed_hl_selon){
        if(ed_hl_findbg) printf(EC_SELBG);
        else if(ed_hl_bg==2) printf(EC_LINEBG);
        else if(ed_hl_bg==1) printf(EC_CODEBG);
        else printf("\x1b[49m");
        ed_hl_selon=0;
    }
}

/* Track block state per line; closer is ]=lvl] */
static void ed_scan_state(char *b,size_t len,int *cm,int *stt,int lang){
    size_t k=0;
    if(*cm==-2||*cm==-3){
        while(k<len){
            if(*cm==-2&&b[k]=='*'&&k+1<len&&b[k+1]=='/'){ k+=2; *cm=-1; break; }
            if(*cm==-3&&b[k]=='-'&&k+2<len&&b[k+1]=='-'&&b[k+2]=='>'){
                k+=3; *cm=-1; break;
            }
            k++;
        }
        if(*cm==-2||*cm==-3||*cm>=0||*stt>=0) return;
    }
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
    { int q=0, lead=1;
      while(k<len){
          unsigned char c=(unsigned char)b[k];
          if(q==1){ if(c=='\\'){k+=2;continue;} if(c=='"') q=0; k++; continue; }
          if(q==2){ if(c=='\\'){k+=2;continue;} if(c=='\'') q=0; k++; continue; }
          if(c=='"'){q=1;k++;continue;}
          if(c=='\''){q=2;k++;continue;}
          if(c!=' '&&c!='\t') lead=0;
          if(ed_is_slash_comment_lang(lang)&&c=='/'&&k+1<len&&b[k+1]=='/') return;
          if(ed_is_hash_lang(lang)&&c=='#') return;
          if(lang==L_INI&&c==';') return;
          if(lang==L_C&&c=='#'&&lead) return;
          if(lang==L_MD&&c=='#'&&lead) return;
          if(ed_has_c_block_comment(lang)&&c=='/'&&k+1<len&&b[k+1]=='*'){ *cm=-2; return; }
          if(lang==L_HTML&&c=='<'&&k+3<len&&b[k+1]=='!'&&b[k+2]=='-'&&b[k+3]=='-'){
              size_t j=k+4;
              while(j+2<len&&!(b[j]=='-'&&b[j+1]=='-'&&b[j+2]=='>')) j++;
              if(j+2<len){ k=j+3; continue; }
              *cm=-3; return;
          }
          if(ed_is_dash_comment_lang(lang)&&c=='-'&&k+1<len&&b[k+1]=='-'){
              if(lang==L_SQL) return;
              size_t j=k+2; int e=0;
              if(j<len&&b[j]=='['){ j++; while(j<len&&b[j]=='='&&e<64){e++;j++;}
                  if(j<len&&b[j]=='['){ *cm=e; return; } }
              return;
          }
          if((lang==L_LUC||lang==L_LUA)&&c=='['){
              size_t j=k+1; int e=0;
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
/* Render to end of line in one color (shared by --, // and # comments) */
static void ed_hl_eol(char *b,size_t len,size_t start,int maxw,size_t *k,int *col,
                       int *w,int *done,int has_sel,size_t sel_s,size_t sel_e,
                       int color,int *cur){
    while(*k<len){
        unsigned char c=(unsigned char)b[*k];
        if(*k>=start&&*w<maxw){
            ed_hl_wantsel=has_sel&&*k>=sel_s&&*k<sel_e;
            ed_hl_k=*k;
            ed_hl_emit(color,cur);
            if(c=='\t') ed_tab_out(w,col,maxw);
            else { fputc(c,stdout); (*w)++; (*col)++; }
        }
        else if(*k>=start){ *done=1; break; }
        else *col+=ed_chw(c,*col);
        (*k)++;
    }
}
static void ed_hl_line(char *b,size_t len,size_t start,int maxw,int lang,int cm,int stt,
                       int has_sel,size_t sel_s,size_t sel_e){
    size_t k=0;
    int w=0, col=0, cur=EC_NONE, done=0;
    int st=0, lvl=0;   /* 0 code, 1 dqs, 2 sqs, 3 block */
    if(cm==-2||cm==-3){ st=3; lvl=cm; }
    else if(cm>0){ st=3; lvl=cm-1; }
    else if(stt>0){ st=4; lvl=stt-1; }
    ed_hl_wantsel=0; ed_hl_selon=0; ed_hl_find_on=0;
    while(k<start){ col+=ed_chw((unsigned char)b[k],col); k++; }
    /* block entry color */
    ed_hl_k=start;
    if(st==3&&start==0) ed_hl_emit(EC_COM,&cur);
    if(st==4&&start==0) ed_hl_emit(EC_STR,&cur);
    while(k<len&&!done){
        ed_hl_wantsel = has_sel && k>=sel_s && k<sel_e;
        ed_hl_k = k;
        unsigned char c=(unsigned char)b[k];
        if(st==3||st==4){
            int want=(st==3)?EC_COM:EC_STR;
            if(lvl==-2){   /* C block: closer is star-slash */
                if(c=='*'&&k+1<len&&b[k+1]=='/'){
                    size_t m;
                    for(m=k;m<k+2;m++){ if(m>=start&&w<maxw){ ed_hl_wantsel=has_sel&&m>=sel_s&&m<sel_e; ed_hl_k=m; ed_hl_emit(want,&cur); fputc(b[m],stdout); w++; col++; } else if(m>=start) done=1; else col++; }
                    k+=2; st=0; continue;
                }
            }
            else if(lvl==-3&&c=='-'&&k+2<len&&b[k+1]=='-'&&b[k+2]=='>'){
                size_t m;
                for(m=k;m<k+3;m++){
                    if(m>=start&&w<maxw){ ed_hl_wantsel=has_sel&&m>=sel_s&&m<sel_e;
                        ed_hl_k=m; ed_hl_emit(want,&cur); fputc(b[m],stdout); w++; col++; }
                    else if(m>=start) done=1;
                    else col++;
                }
                k+=3; st=0; continue;
            }
            else if(c==']'){ size_t j=k+1; int e=0;
                while(j<len&&b[j]=='='&&e<lvl){e++;j++;}
                if(e==lvl&&j<len&&b[j]==']'){
                    size_t m;
                    for(m=k;m<=j;m++){ if(m>=start&&w<maxw){ ed_hl_wantsel=has_sel&&m>=sel_s&&m<sel_e; ed_hl_k=m; ed_hl_emit(want,&cur); fputc(b[m],stdout); w++; col++; } else if(m>=start) done=1; else col++; }
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
                if(k>=start&&w<maxw){ ed_hl_wantsel=has_sel&&k>=sel_s&&k<sel_e; ed_hl_k=k; ed_hl_emit(EC_STR,&cur); fputc(b[k],stdout); w++; col++; }
                else if(k>=start) done=1;
                else col++; }
            else if(c==q) st=0;
            k++; continue;
        }
        /* code mode */
        if(ed_is_dash_comment_lang(lang)&&c=='-'&&k+1<len&&b[k+1]=='-'){
            ed_hl_eol(b,len,start,maxw,&k,&col,&w,&done,has_sel,sel_s,sel_e,EC_COM,&cur);
            continue;
        }
        if(ed_is_slash_comment_lang(lang)&&c=='/'&&k+1<len&&b[k+1]=='/'){
            ed_hl_eol(b,len,start,maxw,&k,&col,&w,&done,has_sel,sel_s,sel_e,EC_COM,&cur);
            continue;
        }
        if(ed_is_hash_lang(lang)&&c=='#'){
            ed_hl_eol(b,len,start,maxw,&k,&col,&w,&done,has_sel,sel_s,sel_e,EC_COM,&cur);
            continue;
        }
        if(lang==L_INI&&c==';'){
            ed_hl_eol(b,len,start,maxw,&k,&col,&w,&done,has_sel,sel_s,sel_e,EC_COM,&cur);
            continue;
        }
        if((lang==L_C||lang==L_MD)&&c=='#'&&ed_is_lead(b,k)){
            ed_hl_eol(b,len,start,maxw,&k,&col,&w,&done,has_sel,sel_s,sel_e,EC_DIR,&cur);
            continue;
        }
        if(ed_has_c_block_comment(lang)&&c=='/'&&k+1<len&&b[k+1]=='*'){
            size_t m;
            for(m=k;m<k+2;m++){
                if(m>=start&&w<maxw){ ed_hl_wantsel=has_sel&&m>=sel_s&&m<sel_e; ed_hl_k=m; ed_hl_emit(EC_COM,&cur); fputc(b[m],stdout); w++; col++; }
                else if(m>=start){ done=1; break; }
                else col++;
            }
            k+=2; st=3; lvl=-2; continue;
        }
        if(lang==L_HTML&&c=='<'&&k+3<len&&b[k+1]=='!'&&b[k+2]=='-'&&b[k+3]=='-'){
            size_t m;
            for(m=k;m<k+4;m++){
                if(m>=start&&w<maxw){ ed_hl_wantsel=has_sel&&m>=sel_s&&m<sel_e;
                    ed_hl_k=m; ed_hl_emit(EC_COM,&cur); fputc(b[m],stdout); w++; col++; }
                else if(m>=start){ done=1; break; }
                else col++;
            }
            k+=4; st=3; lvl=-3; continue;
        }
        if((c=='"'||c=='\'')){
            st=(c=='"')?1:2;
            if(k>=start&&w<maxw){ ed_hl_emit(EC_STR,&cur); fputc(c,stdout); w++; col++; }
            else if(k>=start) done=1;
            else col++;
            k++; continue;
        }
        if((lang==L_LUC||lang==L_LUA)&&c=='['){
            size_t j=k+1; int e=0;
            while(j<len&&b[j]=='='&&e<64){e++;j++;}
            if(e>0&&j<len&&b[j]=='['){
                size_t m;
                for(m=k;m<=j;m++){ if(m>=start&&w<maxw){ ed_hl_wantsel=has_sel&&m>=sel_s&&m<sel_e; ed_hl_k=m; ed_hl_emit(EC_STR,&cur); fputc(b[m],stdout); w++; col++; } else if(m>=start) done=1; else col++; }
                k=j+1; st=4; lvl=e; continue;
            }
        }
        if(lang==L_PS&&c=='$'){
            size_t j=k+1;
            while(j<len){ unsigned char d=(unsigned char)b[j];
                if((d>='A'&&d<='Z')||(d>='a'&&d<='z')||(d>='0'&&d<='9')||d=='_'||d>=128) j++; else break; }
            if(j>k+1){
                size_t m;
                for(m=k;m<j;m++){ if(m>=start&&w<maxw){ ed_hl_wantsel=has_sel&&m>=sel_s&&m<sel_e; ed_hl_k=m; ed_hl_emit(EC_FN,&cur); fputc(b[m],stdout); w++; col++; } else if(m>=start) done=1; else col++; }
                if(j>k&&j-1>=start&&w>=maxw) done=1;
                k=j; continue;
            }
        }
        if((c>='A'&&c<='Z')||(c>='a'&&c<='z')||c=='_'||c>=128){
            size_t j=k;
            while(j<len){ unsigned char d=(unsigned char)b[j];
                if((d>='A'&&d<='Z')||(d>='a'&&d<='z')||(d>='0'&&d<='9')||d=='_'||d>=128) j++; else break; }
            { int iskw=(lang==L_LUC)?ed_is_kw((char*)b+k,j-k):ed_is_kw_lang(lang,(char*)b+k,j-k);
              int iscall=0;
              if(!iskw){ size_t m=j; while(m<len&&(b[m]==' '||b[m]=='\t')) m++;
                  if(m<len&&b[m]=='(') iscall=1; }
              { size_t m;
                for(m=k;m<j;m++){ if(m>=start&&w<maxw){ ed_hl_wantsel=has_sel&&m>=sel_s&&m<sel_e; ed_hl_k=m; ed_hl_emit(iskw?EC_KEY:(iscall?EC_FN:EC_NONE),&cur); fputc(b[m],stdout); w++; col++; } else if(m>=start) done=1; else col++; }
                if(j>k&&j-1>=start&&w>=maxw) done=1; }
              k=j; continue; }
        }
        if(c>='0'&&c<='9'&&(k==0||!(((b[k-1]>='A')&&(b[k-1]<='Z'))||((b[k-1]>='a')&&(b[k-1]<='z'))||((b[k-1]>='0')&&(b[k-1]<='9'))||b[k-1]=='_'))){
            size_t j=k;
            if(c=='0'&&j+1<len&&(b[j+1]=='x'||b[j+1]=='X')){ j+=2; while(j<len){ unsigned char d=(unsigned char)b[j];
                if((d>='0'&&d<='9')||(d>='a'&&d<='f')||(d>='A'&&d<='F')) j++; else break; } }
            else { while(j<len&&((b[j]>='0'&&b[j]<='9')||b[j]=='.')) j++; }
            { size_t m;
              for(m=k;m<j;m++){ if(m>=start&&w<maxw){ ed_hl_wantsel=has_sel&&m>=sel_s&&m<sel_e; ed_hl_k=m; ed_hl_emit(EC_NUM,&cur); fputc(b[m],stdout); w++; col++; } else if(m>=start) done=1; else col++; }
              if(j>k&&j-1>=start&&w>=maxw) done=1; }
            k=j; continue;
        }
        /* directives: !word at first token (LUC only) */
        if(c=='!'&&lang==L_LUC){
            size_t m=k; int lead=1, p;
            for(p=(int)k-1;p>=0;p--){ if(b[p]!=' '&&b[p]!='\t'){lead=0;break;} }
            if(lead){ m++; while(m<len){ unsigned char d=(unsigned char)b[m];
                if((d>='A'&&d<='Z')||(d>='a'&&d<='z')||d=='_') m++; else break; }
                if(m>k+1){ size_t q;
                    for(q=k;q<m;q++){ if(q>=start&&w<maxw){ ed_hl_wantsel=has_sel&&q>=sel_s&&q<sel_e; ed_hl_k=q; ed_hl_emit(EC_DIR,&cur); fputc(b[q],stdout); w++; col++; } else if(q>=start) done=1; else col++; }
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

#if defined(_WIN32)
/* 1 when the console font looks like a Nerd Font (auto-picks nerd icons) */
static int ed_console_has_nerd_font(void){
    HANDLE h=GetStdHandle(STD_OUTPUT_HANDLE);
    CONSOLE_FONT_INFOEX fi;
    const wchar_t *p;
    memset(&fi,0,sizeof fi); fi.cbSize=sizeof fi;
    if(!GetCurrentConsoleFontEx(h,FALSE,&fi)) return 0;
    for(p=fi.FaceName;*p;p++){
        if((p[0]=='N'||p[0]=='n')&&(p[1]=='E'||p[1]=='e')&&
           (p[2]=='R'||p[2]=='r')&&(p[3]=='D'||p[3]=='d')) return 1;
    }
    return 0;
}
/* 1 when a font family is registered with Windows */
static int ed_font_installed(const char *name){
    HKEY roots[2]={HKEY_LOCAL_MACHINE,HKEY_CURRENT_USER};
    int r;
    for(r=0;r<2;r++){
        HKEY k;
        DWORD idx=0;
        if(RegOpenKeyExA(roots[r],"SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion\\Fonts",
                         0,KEY_READ,&k)!=ERROR_SUCCESS) continue;
        for(;;){
            char vn[256]; DWORD vnlen=sizeof vn, type=0;
            BYTE data[512]; DWORD dlen=sizeof data;
            LONG rc=RegEnumValueA(k,idx,vn,&vnlen,NULL,&type,data,&dlen);
            if(rc!=ERROR_SUCCESS) break;
            idx++;
            if(type==REG_SZ&&strstr(vn,name)){ RegCloseKey(k); return 1; }
        }
        RegCloseKey(k);
    }
    return 0;
}
/* Point this console window at the bundled Nerd Font when it is installed
   but not active (conhost honors it; Windows Terminal ignores the call) */
static void ed_font_ensure(void){
    HANDLE h=GetStdHandle(STD_OUTPUT_HANDLE);
    CONSOLE_FONT_INFOEX fi;
    static const wchar_t want[]=L"FiraCode Nerd Font Mono";
    size_t k;
    if(ed_console_has_nerd_font()) return;
    if(!ed_font_installed("FiraCode Nerd Font Mono")) return;
    memset(&fi,0,sizeof fi); fi.cbSize=sizeof fi;
    if(!GetCurrentConsoleFontEx(h,FALSE,&fi)) return;
    for(k=0;k<LF_FACESIZE-1&&want[k];k++) fi.FaceName[k]=want[k];
    fi.FaceName[k]=0;
    SetCurrentConsoleFontEx(h,FALSE,&fi);
}

#else
/* POSIX: the terminal font cannot be queried; use the saved choice / F8.
   LCODE_NERD=1 forces icons on. */
static int ed_console_has_nerd_font(void){
    const char *v=getenv("LCODE_NERD");
    return v && *v && *v!='0';
}
static void ed_font_ensure(void){}

#endif
/* Nerd Font devicons (nvim-web-devicons codepoints + colors); 1 cell each */
typedef struct { const char *ext; unsigned cp; unsigned rgb; } DevIcon;
static const DevIcon DEVICONS[] = {
    {"luc",0xE620,0x51A0CF}, {"lua",0xE620,0x51A0CF},
    {"py",0xE606,0xFFBC03}, {"js",0xE60C,0xCBCB41},
    {"cjs",0xE60C,0xCBCB41}, {"mjs",0xE60C,0xF1E05A},
    {"ts",0xE628,0x519ABA}, {"mts",0xE628,0x519ABA},
    {"cts",0xE628,0x519ABA}, {"tsx",0xE7BA,0x1354BF},
    {"jsx",0xE625,0x20C2E3}, {"json",0xE60B,0xCBCB41},
    {"json5",0xE60B,0xCBCB41}, {"jsonc",0xE60B,0xCBCB41},
    {"md",0xE609,0xDDDDDD}, {"markdown",0xE609,0xDDDDDD},
    {"txt",0xF15B,0x89E051}, {"log",0xF15B,0xDDDDDD},
    {"html",0xE736,0xE44D26}, {"htm",0xE60E,0xE34C26},
    {"css",0xE749,0x663399}, {"scss",0xE749,0xF55385},
    {"less",0xE749,0x563D7C}, {"c",0xE61E,0x599EFF},
    {"h",0xE61E,0xA074C4}, {"hh",0xE61E,0xA074C4},
    {"hpp",0xE61E,0xA074C4}, {"cpp",0xE61D,0x519ABA},
    {"cc",0xE61D,0xF34B7D}, {"cxx",0xE61D,0x519ABA},
    {"rs",0xE7A8,0xDEA584}, {"go",0xE627,0x00ADD8},
    {"java",0xE738,0xCC3E44}, {"rb",0xE791,0x701516},
    {"php",0xE608,0xA074C4}, {"ps1",0xF120,0x4273CA},
    {"bat",0xE615,0xC1F12E}, {"sh",0xE795,0x4D5A5E},
    {"bash",0xE795,0x89E051}, {"exe",0xF15B,0x9F0500},
    {"dll",0xF15B,0x4D2C0B}, {"so",0xF15B,0xDCDDD6},
    {"png",0xE60D,0xA074C4}, {"jpg",0xE60D,0xA074C4},
    {"jpeg",0xE60D,0xA074C4}, {"gif",0xE60D,0xA074C4},
    {"bmp",0xE60D,0xA074C4}, {"ico",0xE60D,0xCBCB41},
    {"avif",0xE60D,0xA074C4}, {"webp",0xE60D,0xA074C4},
    {"svg",0xF1C5,0xFFB13B}, {"wav",0xF001,0x00AFFF},
    {"mp3",0xF001,0x00AFFF}, {"ogg",0xF001,0x0075AA},
    {"flac",0xF001,0x0075AA}, {"mp4",0xF008,0xFD971F},
    {"mkv",0xF008,0xFD971F}, {"avi",0xF008,0xFD971F},
    {"mov",0xF008,0xFD971F}, {"webm",0xF008,0xFD971F},
    {"zip",0xF1C6,0xECA517}, {"gz",0xF1C6,0xECA517},
    {"rar",0xF1C6,0xECA517}, {"7z",0xF1C6,0xECA517},
    {"tgz",0xF1C6,0xECA517}, {"pdf",0xF1C1,0xB30B00},
    {"ini",0xE615,0x6D8086}, {"cfg",0xE615,0x6D8086},
    {"conf",0xE615,0x6D8086}, {"toml",0xE6B2,0x9C4221},
    {"yml",0xE8EB,0xD70000}, {"yaml",0xE8EB,0xD70000},
    {"ttf",0xF031,0xECECEC}, {"otf",0xF031,0xECECEC},
    {"woff",0xF031,0xECECEC}, {"woff2",0xF031,0xECECEC},
    {"csv",0xE64A,0x89E051}, {"sql",0xE706,0xDAD8D8},
    {"swift",0xE755,0xE37933}, {"kt",0xE634,0x7F52FF},
    {"vue",0xE6A0,0x8DC149}, {"lock",0xF023,0xBBBBBB},
    {"makefile",0xE779,0x6D8086}, {"license",0xE60A,0xCBCB41},
    {"dockerfile",0xE7B0,0x458EE6},
    {NULL,0,0}
};
static const DevIcon DEVICON_DEF={"",0xF15B,0x6D8086};   /* generic file */
static const DevIcon *ed_dev_find(const char *key){
    int i;
    for(i=0;DEVICONS[i].ext;i++) if(!strcmp(DEVICONS[i].ext,key)) return &DEVICONS[i];
    return NULL;
}
/* Exact filenames first (makefile, dockerfile, license), then extension */
static const DevIcon *ed_devicon(const char *nm){
    char low[64]; size_t k;
    const char *d=strrchr(nm,'.');
    const DevIcon *f;
    for(k=0;k<sizeof low-1&&nm[k];k++){
        char c=nm[k]; low[k]=(c>='A'&&c<='Z')?(char)(c+32):c;
    }
    low[k]=0;
    if(!strcmp(low,"makefile")||!strcmp(low,"gnumakefile")) return ed_dev_find("makefile");
    if(!strcmp(low,"dockerfile")) return ed_dev_find("dockerfile");
    if(!strcmp(low,"license")||!strcmp(low,"licence")) return ed_dev_find("license");
    if(!d||d==nm||!d[1]) return NULL;
    { char e[16]; size_t j;
      d++;
      for(j=0;j<sizeof e-1&&d[j];j++){
          char c=d[j]; e[j]=(c>='A'&&c<='Z')?(char)(c+32):c;
      }
      e[j]=0;
      f=ed_dev_find(e);
      return f; }
}
/* Encode one codepoint as UTF-8; returns byte count */
static int ed_cp_utf8(unsigned cp,char *out){
    if(cp<0x80){ out[0]=(char)cp; return 1; }
    if(cp<0x800){
        out[0]=(char)(0xC0|(cp>>6)); out[1]=(char)(0x80|(cp&0x3F)); return 2;
    }
    if(cp<0x10000){
        out[0]=(char)(0xE0|(cp>>12)); out[1]=(char)(0x80|((cp>>6)&0x3F));
        out[2]=(char)(0x80|(cp&0x3F)); return 3;
    }
    out[0]=(char)(0xF0|(cp>>18)); out[1]=(char)(0x80|((cp>>12)&0x3F));
    out[2]=(char)(0x80|((cp>>6)&0x3F)); out[3]=(char)(0x80|(cp&0x3F)); return 4;
}
/* lcode config: %APPDATA%\LUC\lcode.cfg (falls back to %USERPROFILE%\.lcode.cfg) */
static void ed_cfg_path(char *out,size_t cap){
    char base[1024]; DWORD n;
    out[0]=0;
    n=GetEnvironmentVariableA("APPDATA",base,sizeof base);
    if(n>0&&n<sizeof base){ snprintf(out,cap,"%s" ED_SEP "LUC" ED_SEP "lcode.cfg",base); return; }
    n=GetEnvironmentVariableA("USERPROFILE",base,sizeof base);
    if(n>0&&n<sizeof base) snprintf(out,cap,"%s" ED_SEP ".lcode.cfg",base);
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
    KB_FIND=0, KB_PFIND, KB_SAVE, KB_UNDO, KB_REDO, KB_COPY, KB_PASTE,
    KB_RUN, KB_NEXT_ERR, KB_TERM, KB_EXPLORER, KB_FILES, KB_SIDE, KB_ICONS,
    KB_COUNT
};
static const char *ED_BIND_ID[KB_COUNT]={
    "find","pfind","save","undo","redo","copy","paste",
    "run","nexterr","terminal","explorer","files","side","icons"
};
static const char *ED_BIND_LABEL[KB_COUNT]={
    "Find in file","Find in files","Save","Undo","Redo","Copy","Paste",
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
            snprintf(out+k,cap-k,ED_SEP "plugin");
            if(GetFileAttributesA(out)!=INVALID_FILE_ATTRIBUTES) return;
        }
    }
    snprintf(out,cap,"." ED_SEP "plugin");
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
static void ed_plug_scan(Editor *e){
    char pat[1120]; WIN32_FIND_DATAA fd; HANDLE fh;
    e->nplugs=0; e->plug_sel=0; e->plug_top=0;
    { Plug *p=&e->plugs[e->nplugs++];
      snprintf(p->name,sizeof p->name,"luc");
      snprintf(p->author,sizeof p->author,"hsusulist");
      snprintf(p->desc,sizeof p->desc,"LUC suggestions, syntax colors, themes and layout");
      p->enabled=1; p->locked=1; p->cfg[0]=0; }
    ed_plug_dir(e,e->plugdir,sizeof e->plugdir);
    if(!e->plugdir[0]) return;
    snprintf(pat,sizeof pat,"%s" ED_SEP "*",e->plugdir);
    fh=FindFirstFileA(pat,&fd);
    if(fh==INVALID_HANDLE_VALUE) return;
    do{
        char sub[1120], cfg[1200];
        size_t j;
        if(!strcmp(fd.cFileName,".")||!strcmp(fd.cFileName,"..")) continue;
        if(!(fd.dwFileAttributes&FILE_ATTRIBUTE_DIRECTORY)) continue;
        snprintf(sub,sizeof sub,"%s" ED_SEP "%s",e->plugdir,fd.cFileName);
        snprintf(cfg,sizeof cfg,"%s" ED_SEP "plugin.cfg",sub);
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
    int rows=e->vh>4?e->vh-4:1;
    return rows>0?rows:1;
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
    int w=0;
    if(ql){
        for(i=0;s[i];i++){
            size_t k; int ok=1;
            for(k=0;k<ql;k++)
                if(!s[i+k]||!ed_find_ch_eq((unsigned char)s[i+k],(unsigned char)q[k])){ ok=0; break; }
            if(ok){ hl=i; break; }
        }
    }
    for(i=0;s[i];i++){
        int cw=ed_chw((unsigned char)s[i],w);
        if(cw<=0) cw=1;
        if(w+cw>maxw) break;
        if(ql&&i==hl) printf("%s",EC_FINDALL);
        fputc(s[i],stdout); w+=cw;
        if(ql&&i==hl) printf("\x1b[97m");
    }
    return w;
}
static void ed_pfind_draw(Editor *e,int W,int H){
    int fw=W/3,lw=6,tw,rows,i,r,qw,caretw=0,startw=0,drawn=0,first=0;
    size_t k;
    char cnt[80];
    if(!e->pf_on) return;
    if(fw<14) fw=14;
    if(fw>36) fw=36;
    tw=W-fw-lw-3;
    if(tw<10){ tw=10; fw=W-tw-lw-3; if(fw<8) fw=8; }
    rows=H>4?H-4:1;
    if(rows<1) rows=1;
    if(e->pf_sel<e->pf_top) e->pf_top=e->pf_sel;
    if(e->pf_sel>=e->pf_top+rows) e->pf_top=e->pf_sel-rows+1;
    if(e->pf_top<0) e->pf_top=0;
    if(e->pf_len){
        snprintf(cnt,sizeof cnt,"%d%s match%s / %d file%s",
            e->pf_total,e->pf_trunc?"+":"",e->pf_total==1?"":"es",
            e->pf_scanned,e->pf_scanned==1?"":"s");
    } else snprintf(cnt,sizeof cnt,"type to search all files");
    printf("\x1b[1;1H%s",EC_TOPBG);
    printf("%s PROJECT FIND  %s",TH_TCUR,TH_TDIM);
    { int cl=(int)strlen(cnt),mp=W-22-cl,used; if(mp<4) mp=4; used=ed_pfind_put(e->exdir,mp);
      for(i=used+14;i<W-cl-2;i++) fputc(' ',stdout);
      printf("%s%s",TH_TDIM,cnt); }
    printf("\x1b[K\x1b[0m");
    printf("\x1b[2;1H%s%s",EC_LOGBG,TH_TDIM);
    { int w0=ed_pfind_put("TEXT",tw); for(i=w0;i<tw;i++) fputc(' ',stdout); }
    printf(" %*s  ",lw,"LINE");
    ed_pfind_put("FILE",fw);
    printf("\x1b[K\x1b[0m");
    printf("\x1b[3;1H%s",TH_TDIM);
    for(i=0;i<W;i++) printf("\xe2\x94\x80");
    printf("\x1b[K\x1b[0m");
    for(r=0;r<rows;r++){
        int idx=e->pf_top+r;
        printf("\x1b[%d;1H%s",4+r,(idx==e->pf_sel&&idx<e->pf_n)?EC_SELBG:EC_CODEBG);
        if(idx<e->pf_n){
            EdPFind *p=&e->pf_res[idx];
            int tw2=ed_pfind_put_hl(p->text,e->pf_buf,tw);
            for(i=tw2;i<tw;i++) fputc(' ',stdout);
            printf(" %*d  ",lw,p->line);
            { int fw2=ed_pfind_put(p->file,fw); for(i=fw2;i<fw;i++) fputc(' ',stdout); }
        } else {
            for(i=0;i<W;i++) fputc(' ',stdout);
        }
        printf("\x1b[K\x1b[0m");
    }
    if(e->pf_len&&e->pf_n==0){
        printf("\x1b[4;1H%s%s",EC_CODEBG,TH_TDIM);
        printf("no matches for '%s'",e->pf_buf);
        printf("\x1b[K\x1b[0m");
    } else if(!e->pf_len){
        printf("\x1b[4;1H%s%s",EC_CODEBG,TH_TDIM);
        printf("type a query below; Up/Down scrolls, Enter opens");
        printf("\x1b[K\x1b[0m");
    }
    printf("\x1b[%d;1H%s",H,EC_TOPBG);
    printf("%s> %s",TH_TDIM,TH_TCUR);
    qw=W-4; if(qw<1) qw=1;
    for(k=0;k<e->pf_cur;k++) caretw+=ed_chw((unsigned char)e->pf_buf[k],caretw);
    if(caretw>=qw) startw=caretw-qw+1;
    { int col=0; size_t ff=0;
      while(ff<e->pf_len){ int w0=ed_chw((unsigned char)e->pf_buf[ff],col); if(col>=startw) break; if(col+w0>startw){startw=col;break;} col+=w0; ff++; first=(int)ff; } }
    for(k=(size_t)first;k<e->pf_len;k++){
        int w0=ed_chw((unsigned char)e->pf_buf[k],drawn);
        if(drawn+w0>qw) break;
        fputc(e->pf_buf[k],stdout); drawn+=w0;
    }
    for(;drawn<qw;drawn++) fputc(' ',stdout);
    printf("\x1b[K\x1b[0m");
    printf("\x1b[%d;%dH\x1b[?25h",H,3+(caretw-startw));
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
    if(e->path[0]&&!_stricmp(full,e->path)) return;   /* already listed from the buffer */
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
            size_t nl=strlen(fd.cFileName),k;
            e->fnd_scanned++;
            if(nl<e->fnd_len) continue;
            for(k=0;k<e->fnd_len;k++)
                if(!ed_find_ch_eq((unsigned char)fd.cFileName[k],(unsigned char)e->fnd_q[k])) break;
            if(k==e->fnd_len) ed_fnd_add(e,childrel,child,0,fd.cFileName);
        }
    }while(FindNextFileA(h,&fd));
    FindClose(h);
}
static void ed_fnd_refresh(Editor *e){
    int cur=0;
    ed_fnd_clear(e);
    if(!e->fnd_len) return;
    if(e->fnd_mode==1){
        if(!e->exdir[0]){ e->fnd_nofolder=1; return; }
        ed_fnd_scan_names(e,e->exdir,"",0);
        return;
    }
    ed_fnd_scan_buffer(e);            /* the open file comes first */
    cur=e->fnd_n;
    if(e->exdir[0]){
        ed_fnd_scan_dir(e,e->exdir,"",0);
        if(e->fnd_n>cur) qsort(e->fnd_res+cur,(size_t)(e->fnd_n-cur),sizeof(EdFnd),ed_fnd_cmp);
    }
}
static void ed_fnd_open(Editor *e,int mode,const char *query){
    e->fnd_on=1; e->fnd_mode=mode;
    e->fnd_q[0]=0; e->fnd_len=0; e->fnd_cur=0;
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
    int bw=W-4, bh;
    if(bw>88) bw=88;
    if(bw<40) bw=40;
    if(bw>W-2) bw=W-2;
    bh=H-6; if(bh<12) bh=12; if(bh>H-1) bh=H-1;
    *x0=(W-bw)/2; *y0=(H-bh)/2; if(*y0<0) *y0=0;
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
            ed_fnd_refresh(e);
        }
    }
    return 0;
}
/* print text, the query match inside it in red */
static int ed_fnd_put_hl(const char *s,const char *q,int maxw){
    size_t ql=q?strlen(q):0,i,hl=0;
    int w=0;
    if(ql){
        for(i=0;s[i];i++){
            size_t k; int ok=1;
            for(k=0;k<ql;k++)
                if(!s[i+k]||!ed_find_ch_eq((unsigned char)s[i+k],(unsigned char)q[k])){ ok=0; break; }
            if(ok){ hl=i; break; }
        }
    }
    for(i=0;s[i];i++){
        int cw=ed_chw((unsigned char)s[i],w);
        if(cw<=0) cw=1;
        if(w+cw>maxw) break;
        if(ql&&i==hl) printf("%s",EC_FINDALL);
        fputc(s[i],stdout); w+=cw;
        if(ql&&i==hl) printf("\x1b[97m");
    }
    return w;
}
static void ed_fnd_draw(Editor *e,int W,int H){
    int x0,y0,bw,bh,i,k,rows,r,caretw=0,startw=0,drawn=0,qx;
    size_t f;
    const char *label;
    char cnt[96];
    int inner;                     /* usable width between the borders */
    if(!e->fnd_on) return;
    ed_fnd_geom(e,W,H,&x0,&y0,&bw,&bh);
    rows=bh-5; if(rows<1) rows=1;
    e->fnd_vis=rows;
    inner=bw-2;
    label=e->fnd_mode==1?"file":"find";
    if(e->fnd_sel>=e->fnd_n) e->fnd_sel=e->fnd_n>0?e->fnd_n-1:0;
    if(e->fnd_sel<0) e->fnd_sel=0;
    ed_fnd_scroll(e,rows);
    /* top border */
    printf("\x1b[%d;%dH%s\xe2\x94\x8c",y0+1,x0+1,TH_TDIM);
    for(k=0;k<inner;k++) fputs("\xe2\x94\x80",stdout);
    printf("\xe2\x94\x90\x1b[0m");
    /* header */
    printf("\x1b[%d;%dH\x1b[48;2;45;45;45m\x1b[97m\xe2\x94\x82",y0+2,x0+1);
    { char title[64]; int tl;
      snprintf(title,sizeof title," %s ",e->fnd_mode==1?"find files":"find");
      tl=(int)strlen(title);
      fwrite(title,1,(size_t)tl,stdout);
      for(i=tl;i<inner;i++) fputc(' ',stdout); }
    printf("\x1b[0m%s\xe2\x94\x82\x1b[0m",TH_TDIM);
    /* input bar */
    printf("\x1b[%d;%dH%s\xe2\x94\x82",y0+3,x0+1,TH_TDIM);
    printf("\x1b[48;2;28;28;28m\x1b[97m");
    printf(" %s> ",label);
    qx=x0+2+(int)strlen(label)+4;   /* column of the first query char */
    for(f=0;f<e->fnd_cur;f++) caretw+=ed_chw((unsigned char)e->fnd_q[f],caretw);
    if(caretw>inner-20) startw=caretw-(inner-20)+1;
    for(f=0;f<e->fnd_len;f++){
        int w0=ed_chw((unsigned char)e->fnd_q[f],drawn);
        if(drawn+w0>inner-20) break;
        fputc(e->fnd_q[f],stdout); drawn+=w0;
    }
    printf("\x1b[48;2;30;30;30m");
    for(;drawn<inner-20;drawn++) fputc(' ',stdout);
    e->fnd_cur_x=qx+(caretw-startw);
    e->fnd_cur_y=y0+3;
    if(e->fnd_len==0) printf("\x1b[90m%s",e->fnd_mode==1?"search file names":"search text in file + project");
    /* count on the right of the input bar */
    if(e->fnd_mode==1&&e->fnd_nofolder) snprintf(cnt,sizeof cnt,"(no folder)");
    else if(e->fnd_len) snprintf(cnt,sizeof cnt,"%d%s match%s",e->fnd_total,e->fnd_trunc?"+":"",e->fnd_total==1?"":"es");
    else cnt[0]=0;
    if(cnt[0]){
        int cl=(int)strlen(cnt);
        printf("\x1b[%d;%dH\x1b[48;2;30;30;30m\x1b[90m%s",y0+3,x0+1+inner-cl,cnt);
    }
    printf("\x1b[0m%s\xe2\x94\x82\x1b[0m",TH_TDIM);
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
            int fw=inner/3, w0;
            if(fw<16) fw=16;
            if(fw>34) fw=34;
            w0=ed_fnd_put_hl(p->file,e->fnd_q,fw);
            for(i=w0;i<fw;i++) fputc(' ',stdout);
            used=fw;
            printf("\x1b[90m:"); used+=1;
            if(p->line>0) snprintf(cnt,sizeof cnt,"%d",p->line); else snprintf(cnt,sizeof cnt,"-");
            printf("%s",cnt); used+=(int)strlen(cnt);
            printf("\x1b[97m "); used+=1;
            used+=ed_fnd_put_hl(p->text,e->fnd_q,inner-used-1);
        } else if(e->fnd_n==0&&r==0){
            if(e->fnd_mode==1&&e->fnd_nofolder){ printf(" (no folder)"); used=12; }
            else if(e->fnd_len){ used=snprintf(cnt,sizeof cnt," no matches for '%s'",e->fnd_q); printf("%s",cnt); }
        }
        for(i=used;i<inner;i++) fputc(' ',stdout);
        printf("\x1b[0m");
        printf("\x1b[%d;%dH%s\xe2\x94\x82\x1b[0m",yy,x0+bw,TH_TDIM);
    }
    /* footer */
    printf("\x1b[%d;%dH%s\xe2\x94\x82\x1b[48;2;22;22;22m%s",y0+4+rows,x0+1,TH_TDIM,TH_TDIM);
    { const char *hint=" Up/Down select  Enter open  Esc close";
      int used=(int)strlen(hint);
      if(used>inner) used=inner;
      fwrite(hint,1,(size_t)used,stdout);
      for(k=used;k<inner;k++) fputc(' ',stdout); }
    printf("\x1b[%d;%dH%s\xe2\x94\x82\x1b[0m",y0+4+rows,x0+bw,TH_TDIM);
    /* bottom border */
    printf("\x1b[%d;%dH%s\xe2\x94\x94",y0+5+rows,x0+1,TH_TDIM);
    for(k=0;k<bw-2;k++) fputs("\xe2\x94\x80",stdout);
    printf("\xe2\x94\x98\x1b[0m");
    if(e->fnd_cur_x>0) printf("\x1b[%d;%dH\x1b[?25h",e->fnd_cur_y,e->fnd_cur_x);
}

/* ':' command hints in the shell (canonical commands only: no aliases) */
static const char *LUC_CMDS[]={
    "find","findfile","keybind","pf","pfind","plugin","run","setting","theme","help",NULL};
static int ed_cmd_hint_list(Editor *e,char items[10][32],int *sel){
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
            if(count>=10) break;
            snprintf(items[count],32,"%s",LUC_CMDS[i]);
            count++;
        }
    }
    if(sel&&*sel>=count) *sel=count>0?count-1:0;
    return count;
}
static void ed_cmd_hint_draw(Editor *e,int W,int H){
    char items[10][32];
    int n=ed_cmd_hint_list(e,items,&e->cmdh_sel);
    int i,bh=n>10?10:n;
    (void)W;
    if(bh<=0) return;
    for(i=0;i<bh;i++){
        int row=H-bh+i;
        if(row<1) continue;
        printf("\x1b[%d;1H",row);
        if(i==e->cmdh_sel) printf("\x1b[48;2;0;120;215m\x1b[97m");
        else printf("\x1b[48;2;28;28;28m\x1b[97m");
        printf(" \xe2\x97\x8f :%s",items[i]);
        { int used=(int)strlen(items[i])+5,k;
          for(k=used;k<26;k++) fputc(' ',stdout); }
        printf("\x1b[0m");
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
    if(!e->diag_n){ snprintf(e->status,sizeof e->status,"no run errors"); return; }
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
    if(e->cy<e->top) e->top=e->cy;
    if(e->cy>=e->top+(size_t)CROWS) e->top=e->cy-(CROWS-1);
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
      { char title[256];
        const char *b1=strrchr(e->path,'/'), *b2=strrchr(e->path,'\\');
        const char *b=b1; if(b2&&(!b||b2>b)) b=b2;
        if(e->path[0]&&b) snprintf(title,sizeof title,"%s - lcode",b+1);
        else if(e->path[0]) snprintf(title,sizeof title,"%s - lcode",e->path);
        else snprintf(title,sizeof title,"lcode");
        title[W-4]=0;
        printf("%s%s  %s",EC_TOPBG,TH_TCUR,title);
        printf("\x1b[K\x1b[0m\n"); }
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
          { int total=(int)e->n, rows=CROWS, isthumb=0;
            if(total>rows){ int a=(int)((long long)r*total/rows), b=(int)((long long)(r+1)*total/rows);
              int v0=(int)e->top, v1=(int)(e->top+rows); if(v1>total) v1=total;
              if(a<v1&&b>v0) isthumb=1; }
            printf("\x1b[%d;%dH",r+2,scrollx+1);
            if(isthumb) printf("\x1b[48;2;110;110;110m\x1b[97m\xe2\x94\x82\x1b[0m");
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
        { /* Command input row (click to focus, Enter runs, Up/Down history) */
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
        else if(e->path[0])
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
    snprintf(pat,sizeof pat,"%s" ED_SEP "*",e->exdir[0]?e->exdir:".");
    fh=FindFirstFileA(pat,&fd);
    if(fh==INVALID_HANDLE_VALUE){ e->exsel=0; e->extop=0; e->exsel_ok=0; return; }
    do{
        if(!strcmp(fd.cFileName,".")||!strcmp(fd.cFileName,"..")) continue;
        if(e->exn==e->excap){ e->excap=e->excap?e->excap*2:32;
            e->ex=(ExItem*)lrealloc(e->ex,sizeof(ExItem)*e->excap); }
        e->ex[e->exn].name=(char*)lmalloc(strlen(fd.cFileName)+1);
        memcpy(e->ex[e->exn].name,fd.cFileName,strlen(fd.cFileName)+1);
        { char fb[1088]; snprintf(fb,sizeof fb,"%s" ED_SEP "%s",e->exdir[0]?e->exdir:".",fd.cFileName);
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
        if(n>0 && e->exdir[n-1]!='\\' && e->exdir[n-1]!='/') e->exdir[n++]=ED_SEPC;
        memcpy(e->exdir+n,name,strlen(name)+1);
    }
    e->exsel=0; e->extop=0; e->exsel_ok=0;
    ex_refresh(e);
}

static void ex_full(Editor *e,const char *name,char *out,size_t cap){
    size_t n=strlen(e->exdir);
    if(n+1+strlen(name)+1>cap){ out[0]=0; return; }
    memcpy(out,e->exdir,n);
    if(n>0 && out[n-1]!='\\' && out[n-1]!='/') out[n++]=ED_SEPC;
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
    snprintf(pat,sizeof pat,"%s" ED_SEP "*",e->ex[idx].full);
    fh=FindFirstFileA(pat,&fd);
    if(fh==INVALID_HANDLE_VALUE) return;
    do{
        if(!strcmp(fd.cFileName,".")||!strcmp(fd.cFileName,"..")) continue;
        if(tn>=4096){ truncated=1; break; }
        if(tn==tcap){ tcap=tcap?tcap*2:64; tmp=(ExItem*)lrealloc(tmp,sizeof(ExItem)*tcap); }
        tmp[tn].name=(char*)lmalloc(strlen(fd.cFileName)+1);
        memcpy(tmp[tn].name,fd.cFileName,strlen(fd.cFileName)+1);
        { char fb[1120]; snprintf(fb,sizeof fb,"%s" ED_SEP "%s",e->ex[idx].full,fd.cFileName);
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

/* Open entry: dir toggles, file loads */
static void ex_open_idx(Editor *e,size_t idx){
    if(idx>=e->exn) return;
    e->exsel=idx; e->exsel_ok=1;
    if(e->ex[idx].isdir){
        if(e->ex[idx].expanded) ex_collapse(e,idx); else ex_expand(e,idx);
        return;
    }
    ed_open_path(e,e->ex[idx].full);
}

/* Popup menu and file ops */
enum { MA_OPEN=1, MA_NEWFILE, MA_NEWDIR, MA_SAVEALL, MA_COPY, MA_RENAME, MA_PASTE,
       MA_DELETE, MA_DELYES, MA_DELNO };

/* Target dir: selected dir, file parent, or project root when nothing is selected */
static void ex_target_dir(Editor *e,char *out,size_t cap){
    if(e->exsel_ok && e->exsel<e->exn && e->ex[e->exsel].isdir){
        snprintf(out,cap,"%s",e->ex[e->exsel].full);
        return;
    }
    if(e->exsel_ok && e->exsel<e->exn && !e->ex[e->exsel].isdir){
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

/* Kind 0 = root menu, 1 = context menu; ax/ay = mouse anchor (-1 = centered).
   dir overrides the creation target (NULL = follow the current selection). */
static void ed_menu_open(Editor *e,int kind,int ax,int ay,const char *dir){
    e->nitems=0; e->msel=0; e->menu=1; e->naming=0;
    e->menutitle[0]=0;
    if(dir&&dir[0]) snprintf(e->menudir,sizeof e->menudir,"%s",dir);
    else ex_target_dir(e,e->menudir,sizeof e->menudir);
    if(ax>=0&&ay>=0){ e->menu_anch=1; e->menu_ax=ax; e->menu_ay=ay; }
    else { e->menu_anch=0; e->menu_ax=0; e->menu_ay=0; }
    if(kind==0){
        ed_menu_add(e,"new file",MA_NEWFILE);
        ed_menu_add(e,"new folder",MA_NEWDIR);
        if(e->fcb_ok) ed_menu_add(e,"paste here",MA_PASTE);
        ed_menu_add(e,"save all files",MA_SAVEALL);
        return;
    }
    if(!e->exsel_ok||e->exsel>=e->exn){ e->menu=0; return; }
    /* No filename title: the bar shows just the 4 options */
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
    { size_t j; int found=0;
      for(j=0;j<e->exn;j++) if(!strcmp(e->ex[j].full,full)){ e->exsel=j; found=1; break; }
      e->exsel_ok=found; }
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
      snprintf(pat,sizeof pat,"%s" ED_SEP "*",src);
      fh=FindFirstFileA(pat,&fd);
      if(fh==INVALID_HANDLE_VALUE) return 0;
      i=0;
      do{
          if(!strcmp(fd.cFileName,".")||!strcmp(fd.cFileName,"..")) continue;
          { char s2[1120], d2[1120];
            snprintf(s2,sizeof s2,"%s" ED_SEP "%s",src,fd.cFileName);
            snprintf(d2,sizeof d2,"%s" ED_SEP "%s",dst,fd.cFileName);
            if(!ed_copy_tree(s2,d2,depth+1)){ FindClose(fh); return 0; } }
          if(++i>2000){ FindClose(fh); return 0; }
      }while(FindNextFileA(fh,&fd));
      if(GetLastError()!=ERROR_NO_MORE_FILES){ FindClose(fh); return 0; }
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
        { char full[1120]; snprintf(full,sizeof full,"%s" ED_SEP "%s",dir,cand);
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
            snprintf(pat,sizeof pat,"%s" ED_SEP "*",path);
            fh=FindFirstFileA(pat,&fd);
            if(fh!=INVALID_HANDLE_VALUE){
                do{
                    char sub[1120];
                    if(!strcmp(fd.cFileName,".")||!strcmp(fd.cFileName,"..")) continue;
                    snprintf(sub,sizeof sub,"%s" ED_SEP "%s",path,fd.cFileName);
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
        e->is_binary=0;
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
    e->exsel_ok=e->exn>0;
}

/* Confirm delete; Cancel preselected for safety */
static void ed_delete_confirm_open(Editor *e){
    if(!e->exsel_ok||e->exsel>=e->exn) return;
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

/* Insert the untitled placeholder row for a new file/folder (VSCode style).
   It sits right below the selected entry (first child when a folder) and is
   only materialized on disk when the name is confirmed. */
static void ed_nam_place(Editor *e,int isdir){
    size_t at; int dep; char full[1120];
    if(e->exsel_ok&&e->exsel<e->exn){
        if(e->ex[e->exsel].isdir){
            if(!e->ex[e->exsel].expanded) ex_expand(e,e->exsel);
            at=e->exsel+1; dep=e->ex[e->exsel].depth+1;
        } else { at=e->exsel+1; dep=e->ex[e->exsel].depth; }
    } else { at=0; dep=1; }
    if(at>e->exn) at=e->exn;
    if(e->exn>=ED_EX_MAX) return;   /* tree full: type blind, still works */
    snprintf(full,sizeof full,"%s\\untitled",e->namdir[0]?e->namdir:".");
    if(e->exn==e->excap){ e->excap=e->excap?e->excap*2:32;
        e->ex=(ExItem*)lrealloc(e->ex,sizeof(ExItem)*e->excap); }
    memmove(&e->ex[at+1],&e->ex[at],(e->exn-at)*sizeof(ExItem));
    e->ex[at].name=(char*)lmalloc(9); memcpy(e->ex[at].name,"untitled",9);
    e->ex[at].full=(char*)lmalloc(strlen(full)+1); memcpy(e->ex[at].full,full,strlen(full)+1);
    e->ex[at].isdir=isdir; e->ex[at].depth=dep; e->ex[at].expanded=0;
    e->exn++;
    e->namidx=(int)at;
}

static void ed_naming_start(Editor *e,int mode){
    e->naming=mode; e->namlen=0; e->namcur=0; e->namb[0]=0;
    e->namold[0]=0; e->namsel=0; e->namidx=-1;
    e->namoff=0; e->namtx0=0; e->namxcol=0; e->namyrow=0;
    e->namsel0=e->exsel; e->namtop0=e->extop; e->namok0=e->exsel_ok;
    if(mode==3){
        /* Edit the selected row in place (name preselected) */
        if(!e->exsel_ok||e->exsel>=e->exn){ e->naming=0; return; }
        { const char *f=e->ex[e->exsel].full, *b=NULL, *p;
          for(p=f;*p;p++) if(*p=='\\'||*p=='/') b=p;
          snprintf(e->namold,sizeof e->namold,"%s",f);
          if(b){ size_t n=(size_t)(b-f); if(n>=sizeof e->namdir) n=sizeof e->namdir-1;
              memcpy(e->namdir,f,n); e->namdir[n]=0; if(!e->namdir[0]) snprintf(e->namdir,sizeof e->namdir,"."); b++; }
          else { snprintf(e->namdir,sizeof e->namdir,"%s",e->exdir); b=f; }
          snprintf(e->namb,sizeof e->namb,"%s",b);
          e->namlen=strlen(e->namb); e->namcur=e->namlen; e->namsel=1; }
        e->namidx=(int)e->exsel;
    } else {
        if(!e->namdir[0]) ex_target_dir(e,e->namdir,sizeof e->namdir);
        ed_nam_place(e,mode==2);
    }
    e->menu=0;
}

static void ed_naming_cancel(Editor *e){
    if(e->naming&&e->naming!=3&&e->namidx>=0&&(size_t)e->namidx<e->exn){
        size_t at=(size_t)e->namidx;   /* drop the placeholder row */
        free(e->ex[at].name); free(e->ex[at].full);
        memmove(&e->ex[at],&e->ex[at+1],(e->exn-at-1)*sizeof(ExItem));
        e->exn--;
        e->exsel=e->namsel0; if(e->exsel>=e->exn) e->exsel=e->exn?e->exn-1:0;
        e->extop=e->namtop0; e->exsel_ok=e->namok0;
    }
    e->naming=0; e->namlen=0; e->namcur=0; e->namsel=0; e->namidx=-1;
}

static void ed_naming_confirm(Editor *e){
    size_t a=0, b=e->namlen;
    int mode=e->naming;
    char name[256], dest[1120];
    while(a<b && (e->namb[a]==' '||e->namb[a]=='\t')) a++;
    while(b>a && (e->namb[b-1]==' '||e->namb[b-1]=='\t')) b--;
    if(b-a==0 || b-a>=sizeof name){ snprintf(e->status,sizeof e->status,"bad name"); ed_naming_cancel(e); return; }
    memcpy(name,e->namb+a,b-a); name[b-a]=0;
    { size_t k; for(k=0;name[k];k++) if(name[k]=='/'||name[k]=='\\'){ snprintf(e->status,sizeof e->status,"bad name"); e->naming=0; return; }
      if(!strcmp(name,".")||!strcmp(name,"..")){ snprintf(e->status,sizeof e->status,"bad name"); e->naming=0; return; } }
    snprintf(dest,sizeof dest,"%s" ED_SEP "%s",e->namdir,name);
    if(e->naming==1){
        FILE *f=fopen(dest,"wb");
        if(!f){ snprintf(e->status,sizeof e->status,"cannot create '%s'",dest); ed_naming_cancel(e); return; }
        fclose(f);
        e->naming=0; e->namidx=-1;   /* the tree is rebuilt below */
        ex_reveal(e,dest);
        ed_open_path(e,dest);
        snprintf(e->status,sizeof e->status,"new file '%s'",name);
    } else if(mode==2){
        if(!CreateDirectoryA(dest,NULL)){ snprintf(e->status,sizeof e->status,"cannot create '%s'",dest); ed_naming_cancel(e); return; }
        e->naming=0; e->namidx=-1;
        ex_reveal(e,dest);
        snprintf(e->status,sizeof e->status,"new folder '%s'",name);
    } else {
        if(!strcmp(e->namold,dest)){ e->naming=0; e->namidx=-1; return; }
        if(GetFileAttributesA(dest)!=INVALID_FILE_ATTRIBUTES){ snprintf(e->status,sizeof e->status,"'%s' exists",name); return; }
        if(!MoveFileA(e->namold,dest)){ snprintf(e->status,sizeof e->status,"cannot rename"); ed_naming_cancel(e); return; }
        ed_rename_paths(e,e->namold,dest);
        e->naming=0; e->namidx=-1;
        ex_reveal(e,dest);
        snprintf(e->status,sizeof e->status,"renamed to '%s'",name);
    }
}

/* Click on the inline naming row: put the caret under the mouse */
static void ed_nam_click(Editor *e,int x){
    int want=x-e->namtx0;
    size_t base=e->namoff;
    e->namsel=0;
    if(want<=0){ e->namcur=base; return; }
    e->namcur=base+ed_byte_at(e->namb+base,e->namlen-base,(size_t)want);
    if(e->namcur>e->namlen) e->namcur=e->namlen;
}

/* Menu rows: top border, title, items, footer hint, bottom border (bh = nitems+4). */
static void ed_popup_geom(Editor *e,int W,int H,int *x0,int *y0,int *w,int *h){
    int bw, bh, i, maxw=0;
    { int tl=e->menutitle[0]?(int)strlen(e->menutitle):0;
      if(tl>maxw) maxw=tl; }
    for(i=0;i<e->nitems;i++){ int L=(int)strlen(e->mitems[i].label); if(L>maxw) maxw=L; }
    bw=maxw+4; if(bw>W-4) bw=W-4; if(bw<16) bw=16;
    bh=(e->menutitle[0]?1:0)+e->nitems; if(bh>H-1) bh=H-1; if(bh<1) bh=1;
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
    /* Borderless context menu: plain block, blue selection, title only for confirm */
    { int inner=bw, k, base=e->menutitle[0]?1:0, n;
      if(e->menutitle[0]){
          printf("\x1b[%d;%dH\x1b[48;2;28;28;28m",y0+1,x0+1);
          { int tl=(int)strlen(e->menutitle);
            if(tl>inner) tl=inner;
            printf("\x1b[90m%.*s",tl,e->menutitle);
            for(k=tl;k<inner;k++) fputc(' ',stdout); }
          printf("\x1b[0m");
      }
      n=e->nitems; if(n>bh-base) n=bh-base;
      for(i=0;i<n;i++){
          int yy=y0+base+i;
          printf("\x1b[%d;%dH",yy+1,x0+1);
          if(i==e->msel) printf("\x1b[48;2;0;120;215m\x1b[97m");
          else printf("\x1b[48;2;28;28;28m\x1b[37m");
          fputs(i==e->msel?"\xe2\x96\xb8 ":"  ",stdout);
          { int k2, L=(int)strlen(e->mitems[i].label);
            int maxl=inner-2;
            for(k2=0;k2<L&&k2<maxl;k2++) fputc(e->mitems[i].label[k2],stdout);
            for(;k2<maxl;k2++) fputc(' ',stdout); }
          printf("\x1b[0m");
      }
    }
}

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
    if(tab==0) return 11;   /* appearance + explorer bar + terminal panel */
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
        else { snprintf(label,lcap,"Terminal content"); snprintf(val,vcap,"%s",e->panel_content); }
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
        ed_unique(cand,sizeof cand,dir,base,e->fcb_dir);
        snprintf(dest,sizeof dest,"%s" ED_SEP "%s",dir,cand);
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
    if(!e->menu) return;
    ed_popup_geom(e,W,H,&x0,&y0,&bw,&bh);
    /* click outside -> dismiss (no action, like Cancel) */
    if(x<x0||x>=x0+bw||y<y0||y>=y0+bh){ e->menu=0; return; }
    /* items start at y0 (+1 when the confirm title is shown) */
    for(i=0;i<e->nitems;i++){
        int yy=y0+(e->menutitle[0]?1:0)+i;
        if(y==yy && x>=x0 && x<x0+bw){ e->msel=i; ed_menu_pick(e,e->mitems[i].id); return; }
    }
}

/* Wait for key after run */
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
#if defined(_WIN32)
static int ed_shell_has_ps(void){
    static int st=-1;
    if(st<0){
        char p[1024];
        st=(SearchPathA(NULL,"powershell.exe",NULL,sizeof p,p,NULL)>0)?1:0;
    }
    return st;
}
#endif
static void ed_shell_hist_push(Editor *e,const char *line){
    if(e->shn>=ED_SHHIST){
        memmove(e->shhist[0],e->shhist[1],sizeof e->shhist[0]*(ED_SHHIST-1));
        e->shn=ED_SHHIST-1;
    }
    snprintf(e->shhist[e->shn++],sizeof e->shhist[0],"%s",line);
    e->shpos=e->shn;
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
        ed_log(e,":run - run the current file (F5)");
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
#if defined(_WIN32)
    if(ed_shell_has_ps()) snprintf(cmd,sizeof cmd,"powershell -NoProfile -NonInteractive -Command %s 2>&1 <NUL",line);
    else snprintf(cmd,sizeof cmd,"cmd.exe /C %s 2>&1 <NUL",line);
#else
    /* POSIX: run through /bin/sh as a group so 2>&1 covers every command */
    snprintf(cmd,sizeof cmd,"{ %s\n} 2>&1 </dev/null",line);
#endif
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
    char hints[10][32];
    int nh=ed_cmd_hint_list(e,hints,&e->cmdh_sel);
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
            if(e->shpos>0) e->shpos--;
            snprintf(e->shbuf,sizeof e->shbuf,"%s",e->shhist[e->shpos]);
            e->shlen=strlen(e->shbuf); e->shcur=e->shlen;
            return 0;
        }
        if(vk==VK_DOWN){
            if(e->shpos+1<e->shn){
                e->shpos++;
                snprintf(e->shbuf,sizeof e->shbuf,"%s",e->shhist[e->shpos]);
            } else { e->shpos=e->shn; e->shbuf[0]=0; }
            e->shlen=strlen(e->shbuf); e->shcur=e->shlen;
            return 0;
        }
        if(ed_bind_match(e,KB_ICONS,vk,ctl)){ ed_icons_toggle(e); return 0; }
        return 0;
    }
    if(ch>=32){
        unsigned long cp;
        char tmp[4]; int m=0;
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

/* Keyboard: 1 quits editor */
static int ed_key(Editor *e,HANDLE hin,DWORD old,WORD vk,WCHAR ch,DWORD ctl){
    int ctrl=(ctl&(LEFT_CTRL_PRESSED|RIGHT_CTRL_PRESSED))!=0;
    int alt=(ctl&(LEFT_ALT_PRESSED|RIGHT_ALT_PRESSED))!=0;
    int shift=(ctl&SHIFT_PRESSED)!=0;
    (void)hin; (void)old;
    if(e->fnd_on) return ed_fnd_key(e,vk,ch,ctl);
    if(e->pf_on) return ed_pfind_key(e,vk,ch,ctl);
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
              snprintf(dest,sizeof dest,"%s" ED_SEP "%s",dir,cand);
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
        if(u>0&&!e->quit_arm){ snprintf(e->status,sizeof e->status,"%d unsaved - Esc again quits (loses them)",u); e->quit_arm=1; return 0; }
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
        rows=bh-5; if(rows<1) rows=1;
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
            r=my-(y0+4);          /* first result row sits on y0+5 */
            if(r>=0&&r<rows){
                int idx=e->fnd_top+r;
                if(idx<e->fnd_n){ e->fnd_sel=idx; e->fnd_top=idx; }
            }
        }
        return;
    }
    if(e->pf_on) return;
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
            /* keep cursor visible: wheel scrolls view + cursor together */
            if(e->n>0){
                if(e->cy<e->top) e->cy=e->top;
                if(e->cy>=e->top+(size_t)CROWS){
                    e->cy=e->top+(size_t)CROWS-1;
                    if(e->cy>=e->n) e->cy=e->n-1;
                }
                ed_clamp(e);
                e->focus=0;
            }
            e->status[0]=0;
        }
        return;
    }
    if(m->dwEventFlags==MOUSE_MOVED){
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
        if(e->drag_ex||e->drag_panel){
            e->drag_ex=0; e->drag_panel=0; ed_cfg_save(e);
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
        }
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
                    if((e.sel_armed||e.drag_ex||e.drag_panel) &&
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
        ed_clamp(&e);
    }
done:
    SetConsoleMode(hin,old);
    ex_free(&e);
    ed_free(&e);
    ed_undo_clear(&e);
    { size_t si; for(si=0;si<e.nstash;si++){ size_t k; for(k=0;k<e.stash[si].n;k++) free(e.stash[si].ln[k].b); free(e.stash[si].ln); }
      free(e.stash); e.stash=NULL; e.nstash=0; }
    printf("\x1b[?25h\x1b[?7h\x1b[?1049l\n");
    fflush(stdout);
    setvbuf(stdout,NULL,_IONBF,0);
}

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

#if defined(_WIN32)
static int luc_file_is_file(const char *path){
    DWORD attr=GetFileAttributesA(path);
    return attr!=INVALID_FILE_ATTRIBUTES && !(attr & FILE_ATTRIBUTE_DIRECTORY);
}

static int luc_exe_directory(char *out,size_t cap){
    DWORD n=GetModuleFileNameA(NULL,out,(DWORD)cap);
    if(n==0 || n>=cap){ out[0]=0; return 0; }
    char *slash=strrchr(out,'\\');
    char *slash2=strrchr(out,'/');
    if(slash2 && (!slash || slash2>slash)) slash=slash2;
    if(slash) *slash=0; else out[0]=0;
    return 1;
}

static int luc_installer_script(char *out,size_t cap){
    char dir[1024];
    if(luc_exe_directory(dir,sizeof dir)){
        snprintf(out,cap,"%s\\install.ps1",dir);
        if(luc_file_is_file(out)) return 1;
        snprintf(out,cap,"%s\\core\\install.ps1",dir);
        if(luc_file_is_file(out)) return 1;
    }
    const char *local=getenv("LOCALAPPDATA");
    if(local){
        snprintf(out,cap,"%s\\Programs\\LUC\\install.ps1",local);
        if(luc_file_is_file(out)) return 1;
        snprintf(out,cap,"%s\\LUC\\install.ps1",local);
        if(luc_file_is_file(out)) return 1;
    }
    out[0]=0;
    return 0;
}

static int luc_state_exists(void){
    const char *local=getenv("LOCALAPPDATA");
    if(!local) return 0;
    char path[1024];
    snprintf(path,sizeof path,"%s\\LUC\\state.json",local);
    return luc_file_is_file(path);
}

static int luc_installer_pending(void){
    if(getenv("LUC_NO_FIRST_RUN") || getenv("LUC_NO_INSTALLER")) return 0;
    if(luc_state_exists()) return 0;
    if(!repl_wanted()) return 0;
    char script[1024];
    return luc_installer_script(script,sizeof script);
}

static int append_quoted_arg(char *dst,size_t cap,const char *arg){
    size_t len=strlen(dst);
    int backslashes=0;
    if(len+2>=cap) return 0;
    dst[len++]='"';
    for(const char *p=arg; *p; p++){
        if(*p=='\\'){
            backslashes++;
            continue;
        }
        if(*p=='"'){
            for(int i=0;i<backslashes*2+1;i++){
                if(len+1>=cap) return 0;
                dst[len++]='\\';
            }
            if(len+1>=cap) return 0;
            dst[len++]='"';
        } else {
            for(int i=0;i<backslashes;i++){
                if(len+1>=cap) return 0;
                dst[len++]='\\';
            }
            if(len+1>=cap) return 0;
            dst[len++]=*p;
        }
        backslashes=0;
    }
    for(int i=0;i<backslashes*2;i++){
        if(len+1>=cap) return 0;
        dst[len++]='\\';
    }
    if(len+1>=cap) return 0;
    dst[len++]='"';
    dst[len]=0;
    return 1;
}

static int run_process_wait(const char *command_line){
    STARTUPINFOA si;
    PROCESS_INFORMATION pi;
    ZeroMemory(&si,sizeof si);
    si.cb=sizeof si;
    ZeroMemory(&pi,sizeof pi);
    char *buffer=(char*)malloc(strlen(command_line)+1);
    if(!buffer) return -1;
    strcpy(buffer,command_line);
    BOOL ok=CreateProcessA(NULL,buffer,NULL,NULL,TRUE,0,NULL,NULL,&si,&pi);
    free(buffer);
    if(!ok) return -1;
    WaitForSingleObject(pi.hProcess,INFINITE);
    DWORD code=1;
    GetExitCodeProcess(pi.hProcess,&code);
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    return (int)code;
}

static int install_option_value(const char *name,int argc,char **argv,int *index,const char **value){
    if(*index+1>=argc){
        fprintf(stderr,"luc --install: %s needs a value\n",name);
        return 0;
    }
    *value=argv[++(*index)];
    return 1;
}

static int cmd_installer(int argc,char **argv){
    char script[1024];
    if(!luc_installer_script(script,sizeof script)){
        fprintf(stderr,
            "luc --install: install.ps1 was not found next to luc.exe.\n"
            "Download it from:\n"
            "  https://raw.githubusercontent.com/hsusulist/luc/main/install.ps1\n");
        return 1;
    }
    const char *ps[64];
    int n=0;
    ps[n++]="powershell.exe";
    ps[n++]="-NoProfile";
    ps[n++]="-ExecutionPolicy";
    ps[n++]="Bypass";
    ps[n++]="-File";
    ps[n++]=script;
    for(int i=2;i<argc;i++){
        const char *a=argv[i];
        const char *value=NULL;
        if(!strcmp(a,"-c") || !strcmp(a,"--components")){
            if(!install_option_value(a,argc,argv,&i,&value)) return 1;
            ps[n++]="-Components"; ps[n++]=value;
        } else if(!strcmp(a,"-d") || !strcmp(a,"--dir") || !strcmp(a,"--install-path")){
            if(!install_option_value(a,argc,argv,&i,&value)) return 1;
            ps[n++]="-InstallPath"; ps[n++]=value;
        } else if(!strcmp(a,"--archive")){
            if(!install_option_value(a,argc,argv,&i,&value)) return 1;
            ps[n++]="-ArchivePath"; ps[n++]=value;
        } else if(!strcmp(a,"--source")){
            if(!install_option_value(a,argc,argv,&i,&value)) return 1;
            ps[n++]="-Source"; ps[n++]=value;
        } else if(!strcmp(a,"-y") || !strcmp(a,"--yes")){
            ps[n++]="-Yes";
        } else if(!strcmp(a,"--no-path")){
            ps[n++]="-NoPath";
        } else if(!strcmp(a,"--no-env")){
            ps[n++]="-NoEnvironmentChanges";
        } else if(!strcmp(a,"-f") || !strcmp(a,"--force")){
            ps[n++]="-Force";
        } else if(!strcmp(a,"--quiet")){
            ps[n++]="-Quiet";
        } else if(!strcmp(a,"--no-color")){
            ps[n++]="-NoColor";
        } else if(!strcmp(a,"--allow-unsigned")){
            ps[n++]="-AllowUnsigned";
        } else {
            fprintf(stderr,"luc --install: unknown option '%s'\n",a);
            return 1;
        }
        if(n>56){
            fprintf(stderr,"luc --install: too many arguments\n");
            return 1;
        }
    }
    ps[n]=NULL;
    char command[8192];
    command[0]=0;
    for(int i=0;i<n;i++){
        if(i>0) strncat(command," ",sizeof(command)-strlen(command)-1);
        if(!append_quoted_arg(command,sizeof command,ps[i])){
            fprintf(stderr,"luc --install: command line is too long\n");
            return 1;
        }
    }
    int rc=run_process_wait(command);
    if(rc==-1){
        fprintf(stderr,"luc --install: could not start powershell.exe\n");
        return 1;
    }
    return rc;
}
#else
static int luc_state_exists(void){ return 1; }
static int luc_installer_pending(void){ return 0; }
static int cmd_installer(int argc,char **argv){
    (void)argc; (void)argv;
    fprintf(stderr,"luc --install is only available on Windows.\n");
    return 1;
}
#endif

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

    if(argc<2){
        if(luc_installer_pending()){
            int rc=cmd_installer(argc,argv);
            if(rc!=0) return rc;
            if(!luc_state_exists()) return 0;
        }
        print_banner(); if(repl_wanted()) interactive_shell(); return 0;
    }
    

    if(strcmp(argv[1],"--version")==0||strcmp(argv[1],"-v")==0){
        printf("%s  [C99 register VM + x86-64 JIT]\n",LUC_VERSION);
        return 0;
    }
    if(strcmp(argv[1],"--help")==0||strcmp(argv[1],"-h")==0){
        if(repl_wanted()){ print_banner(); repl_help_loop(); return 0; }
        print_help();
        return 0;
    }
    if(strcmp(argv[1],"--install")==0)
        return cmd_installer(argc,argv);
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
