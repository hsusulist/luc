/*
** ============================================================================
**  LUC 0.1  --  an independent, register-based scripting language  (CORE)
**
**  luc_core.c  --  platform helpers, values, global state, tables, GC,
**                  conversions, lexer, AST, parser, bytecode, compiler, VM,
**                  coroutines, task scheduler, module finder, registration
**                  and the command-line driver.
**
**  build (no window):  make            (or: gcc -O2 -std=c99 -o luc *.c -lm)
**  build (window):     make luc-window (needs SDL2 + SDL2_ttf + SDL2_image)
**
**  optional: -DLUC_NO_TTF    build window lib without SDL2_ttf
**            -DLUC_NO_IMAGE  build window lib without SDL2_image (BMP only)
** ============================================================================
*/
#include "luc.h"

#if defined(_WIN32)
#  include <windows.h>
#else
#  include <sys/time.h>
#  include <unistd.h>
#endif

/* ======== platform helpers ======== */

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

/* ======== errors, allocation, strings, constructors ======== */

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

/* --- object allocation ---------------------------------------------------*/
static Obj *newobj(size_t sz,int type){
    Obj *o = (Obj*)lcalloc(sz);
    o->type=(unsigned char)type; o->marked=0;
    o->next=V.objects; V.objects=o;
    V.nalloc++;
    return o;
}

/* --- interned strings ----------------------------------------------------*/
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

/* --- constructors --------------------------------------------------------*/
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

/* ======== tables ======== */

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

/* iteration order: array part, then hash part */
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

/* ======== garbage collector ======== */

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
        default: break;
    }
    free(o);
}

void gc_collect(void){
    if(V.gcoff) return;
    /* --- mark roots --- */
    mark_obj((Obj*)V.globals);
    mark_obj((Obj*)V.stringlib); mark_obj((Obj*)V.listmeta);
    mark_obj((Obj*)V.bufferlib); mark_obj((Obj*)V.filelib);
    mark_obj((Obj*)V.mainco);
    if(V.loaded) mark_obj((Obj*)V.loaded);
    for(LucState *c=V.cur;c;c=c->resumer) mark_obj((Obj*)c);
    for(int i=0;i<V.nsched;i++) mark_obj((Obj*)V.sched[i].co);
    mark_value(V.errval);

    /* --- sweep non-string objects --- */
    Obj **pp=&V.objects;
    while(*pp){
        Obj *o=*pp;
        if(o->marked){ o->marked=0; pp=&o->next; }
        else { *pp=o->next; free_obj(o); V.nalloc--; }
    }
    /* --- sweep interned strings --- */
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

/* ======== conversions / printing ======== */

const char *type_name(Value v){
    switch(v.t){
        case LT_NIL:return "nil"; case LT_BOOL:return "boolean";
        case LT_NUM:return "number"; case LT_STR:return "string";
        case LT_TABLE:return "table"; case LT_LIST:return "list";
        case LT_FUNC: case LT_CFUNC:return "function";
        case LT_BUFFER:return "buffer"; case LT_CORO:return "thread";
        case LT_FILE:return "file";
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
            }
            snprintf(buf,sizeof buf,"%s: %p",type_name(v),(void*)v.u.o);
            return str_fromc(buf);
        }
    }
}
Str *tostr(Value v){ return v2str(v,0); }

/* ======== lexer, AST, parser, bytecode, compiler, VM ======== */

enum {
    TK_EOF=256, TK_NAME, TK_NUMBER, TK_STRING,
    TK_AND, TK_BREAK, TK_DO, TK_ELSE, TK_ELSEIF, TK_END, TK_FALSE, TK_FOR,
    TK_FUNCTION, TK_IF, TK_IN, TK_LOCAL, TK_NIL, TK_NOT, TK_OR, TK_REPEAT,
    TK_RETURN, TK_THEN, TK_TRUE, TK_UNTIL, TK_WHILE, TK_IMPORT,
    TK_CONCAT, TK_DOTS, TK_EQ, TK_NE, TK_LE, TK_GE
};

static const char *const kwnames[] = {
    "and","break","do","else","elseif","end","false","for","function","if",
    "in","local","nil","not","or","repeat","return","then","true","until","while","import"
};

typedef struct {
    const char *p, *end;
    int line;
    Str *source;
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

static int lx_check_kw(const char *s,int len){
    for(int i=0;i<22;i++)
        if((int)strlen(kwnames[i])==len && memcmp(kwnames[i],s,(size_t)len)==0)
            return TK_AND+i;
    return TK_NAME;
}

/* long bracket:  [=[ ... ]=]  (level >= 1).  Plain [[ ]] is reserved for
   list literals, so LUC long strings need at least one '='.               */
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
        int t=lx_check_kw(s,len);
        if(t==TK_NAME) *str=str_new(s,len);
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
        case '~': if(lx->p<lx->end&&*lx->p=='='){lx->p++;return TK_NE;} lex_error(lx,"unexpected '~'"); break;
        case '!': if(lx->p<lx->end&&*lx->p=='='){lx->p++;return TK_NE;} lex_error(lx,"unexpected '!'"); break;
        case '<': if(lx->p<lx->end&&*lx->p=='='){lx->p++;return TK_LE;} return '<';
        case '>': if(lx->p<lx->end&&*lx->p=='='){lx->p++;return TK_GE;} return '>';
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

/* ==========================================================================
** 7. AST
** ========================================================================== */

typedef enum {
    E_NIL,E_TRUE,E_FALSE,E_NUM,E_STR,E_VARARG,E_NAME,E_INDEX,
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
    Expr *a,*b;
    EList args;
    FieldList fields;
    FuncBody *fb;
};
struct Block { Stat **s; int n,cap; };
struct FuncBody { Str *name; Str **params; int nparams,isvararg,line; Block *body; };

typedef enum {
    S_LOCAL,S_ASSIGN,S_CALL,S_DO,S_WHILE,S_REPEAT,S_IF,
    S_NUMFOR,S_GENFOR,S_LOCALFUNC,S_RETURN,S_BREAK
} SKind;

struct Stat {
    SKind k; int line;
    Str **names; int nnames;
    EList lhs,rhs;
    Block *body,*body2;
    Expr *e1,*e2,*e3;
    struct { Expr **cond; Block **blk; int n,cap; } clauses;
    Block *elseblk;
    FuncBody *fb;
};

/* AST nodes live until process exit (scripts are compiled once). */
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

/* ==========================================================================
** 8. PARSER  (tokens -> AST)
** ========================================================================== */

typedef struct { Lexer lx; } Parser;

static Block *parse_block(Parser *ps);
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
        case TK_EQ:return "=="; case TK_NE:return "~=";
        case TK_LE:return "<="; case TK_GE:return ">=";
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

/* --- optional type annotations: parsed and discarded ------------------- */
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
            while(ps->lx.t=='.'){ lx_next(&ps->lx); expect_name(ps); }
        } else break;
        if(ps->lx.t=='?') lx_next(&ps->lx);
        if(ps->lx.t=='-'&&lx_peek(&ps->lx)=='>'){ lx_next(&ps->lx); lx_next(&ps->lx); continue; }
        if(ps->lx.t=='|'){ lx_next(&ps->lx); continue; }
        break;
    }
}
static void opt_type(Parser *ps){ if(ps->lx.t==':'){ lx_next(&ps->lx); parse_type(ps); } }

/* --- function body ------------------------------------------------------ */
static FuncBody *parse_funcbody(Parser *ps,Str *name,int ismethod){
    FuncBody *fb=(FuncBody*)anew(sizeof(FuncBody));
    fb->name=name; fb->line=ps->lx.tline;
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
    return fb;
}

/* --- expressions -------------------------------------------------------- */
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
        /* parenthesised expressions are truncated to one value */
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

static Expr *parse_suffixed(Parser *ps){
    Expr *e=parse_primary(ps);
    for(;;){
        int line=ps->lx.tline;
        switch(ps->lx.t){
            case '.': {
                lx_next(&ps->lx);
                Str *n=expect_name(ps);
                Expr *ix=new_expr(E_INDEX,line);
                ix->a=e; ix->b=new_expr(E_STR,line); ix->b->str=n;
                e=ix; break; }
            case '[': {
                lx_next(&ps->lx);
                Expr *k=parse_expr(ps);
                expect(ps,']');
                Expr *ix=new_expr(E_INDEX,line); ix->a=e; ix->b=k; e=ix; break; }
            case ':': {
                lx_next(&ps->lx);
                Str *n=expect_name(ps);
                Expr *c=new_expr(E_METHCALL,line); c->a=e; c->name=n;
                parse_args(ps,c); e=c; break; }
            case '(': case TK_STRING: case '{': {
                Expr *c=new_expr(E_CALL,line); c->a=e;
                parse_args(ps,c); e=c; break; }
            default: return e;
        }
    }
}

static Expr *parse_table(Parser *ps){
    int line=ps->lx.tline;
    Expr *e=new_expr(E_TABLE,line);
    expect(ps,'{');
    while(ps->lx.t!='}'){
        if(ps->lx.t=='['){
            lx_next(&ps->lx);
            Expr *k=parse_expr(ps); expect(ps,']'); expect(ps,'=');
            fl_add(&e->fields,k,parse_expr(ps));
        } else if(ps->lx.t==TK_NAME && lx_peek(&ps->lx)=='='){
            Expr *k=new_expr(E_STR,ps->lx.tline); k->str=ps->lx.str;
            lx_next(&ps->lx); lx_next(&ps->lx);
            fl_add(&e->fields,k,parse_expr(ps));
        } else {
            fl_add(&e->fields,NULL,parse_expr(ps));
        }
        if(!opt(ps,',') && !opt(ps,';')) break;
    }
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
        case TK_NUMBER: e=new_expr(E_NUM,line); e->num=ps->lx.num; lx_next(&ps->lx); return e;
        case TK_STRING: e=new_expr(E_STR,line); e->str=ps->lx.str; lx_next(&ps->lx); return e;
        case TK_DOTS:   e=new_expr(E_VARARG,line); lx_next(&ps->lx); return e;
        case '{':       return parse_table(ps);
        case '[':       return parse_list(ps);
        case TK_FUNCTION: {
            lx_next(&ps->lx);
            e=new_expr(E_FUNC,line); e->fb=parse_funcbody(ps,NULL,0); return e; }
        default: return parse_suffixed(ps);
    }
}

/* operator priorities (left, right) */
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
        if(!op) break;
        Prio pr=binprio(op);
        if(pr.left<=limit) break;
        int l2=ps->lx.tline;
        lx_next(&ps->lx);
        Expr *rhs=parse_subexpr(ps,pr.right);
        Expr *b=new_expr(op==TK_AND?E_AND:(op==TK_OR?E_OR:E_BIN),l2);
        b->op=op; b->a=e; b->b=rhs;
        e=b;
    }
    return e;
}
static Expr *parse_expr(Parser *ps){ return parse_subexpr(ps,0); }

/* --- statements --------------------------------------------------------- */
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
    return t==TK_EOF||t==TK_END||t==TK_ELSE||t==TK_ELSEIF||t==TK_UNTIL;
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

        case TK_REPEAT: {
            Stat *s=new_stat(S_REPEAT,line);
            lx_next(&ps->lx);
            s->body=parse_block(ps); expect(ps,TK_UNTIL);
            s->e1=parse_expr(ps);
            return s; }

        case TK_FOR: {
            lx_next(&ps->lx);
            Str *n1=expect_name(ps);
            opt_type(ps);
            if(ps->lx.t=='='){
                Stat *s=new_stat(S_NUMFOR,line);
                s->names=(Str**)anew(sizeof(Str*)); s->names[0]=n1; s->nnames=1;
                lx_next(&ps->lx);
                s->e1=parse_expr(ps); expect(ps,',');
                s->e2=parse_expr(ps);
                if(opt(ps,',')) s->e3=parse_expr(ps);
                expect(ps,TK_DO);
                s->body=parse_block(ps); expect(ps,TK_END);
                return s;
            } else {
                Stat *s=new_stat(S_GENFOR,line);
                s->names=(Str**)anew(sizeof(Str*)*32); s->names[0]=n1; s->nnames=1;
                while(opt(ps,',')){
                    if(s->nnames>=30) perr(ps,"too many loop variables");
                    s->names[s->nnames++]=expect_name(ps); opt_type(ps);
                }
                expect(ps,TK_IN);
                do{ el_add(&s->rhs,parse_expr(ps)); }while(opt(ps,','));
                expect(ps,TK_DO);
                s->body=parse_block(ps); expect(ps,TK_END);
                return s;
            } }

        case TK_FUNCTION: {
            lx_next(&ps->lx);
            Str *n=expect_name(ps);
            Expr *target=new_expr(E_NAME,line); target->name=n;
            int ismethod=0;
            Str *last=n;
            while(ps->lx.t=='.'){
                lx_next(&ps->lx);
                Str *f=expect_name(ps);
                Expr *ix=new_expr(E_INDEX,line);
                ix->a=target; ix->b=new_expr(E_STR,line); ix->b->str=f;
                target=ix; last=f;
            }
            if(ps->lx.t==':'){
                lx_next(&ps->lx);
                Str *f=expect_name(ps);
                Expr *ix=new_expr(E_INDEX,line);
                ix->a=target; ix->b=new_expr(E_STR,line); ix->b->str=f;
                target=ix; last=f; ismethod=1;
            }
            Stat *s=new_stat(S_ASSIGN,line);
            el_add(&s->lhs,target);
            Expr *fe=new_expr(E_FUNC,line);
            fe->fb=parse_funcbody(ps,last,ismethod);
            el_add(&s->rhs,fe);
            return s; }

        case TK_LOCAL: {
            lx_next(&ps->lx);
            if(opt(ps,TK_FUNCTION)){
                Stat *s=new_stat(S_LOCALFUNC,line);
                Str *n=expect_name(ps);
                s->names=(Str**)anew(sizeof(Str*)); s->names[0]=n; s->nnames=1;
                s->fb=parse_funcbody(ps,n,0);
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

        case TK_IMPORT: {
            /* import a, b  ==  do a = __import("a") b = __import("b") end */
            lx_next(&ps->lx);
            Stat *s=new_stat(S_DO,line);
            s->body=(Block*)anew(sizeof(Block));
            do{
                Str *n=expect_name(ps);
                Expr *target=new_expr(E_NAME,line); target->name=n;
                Expr *fn=new_expr(E_NAME,line); fn->name=str_fromc("__import");
                Expr *arg=new_expr(E_STR,line); arg->str=n;
                Expr *call=new_expr(E_CALL,line); call->a=fn; el_add(&call->args,arg);
                Stat *as=new_stat(S_ASSIGN,line);
                el_add(&as->lhs,target); el_add(&as->rhs,call);
                blk_add(s->body,as);
            }while(opt(ps,','));
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
            if(e->k!=E_CALL&&e->k!=E_METHCALL) perr(ps,"syntax error near unexpected expression");
            Stat *s=new_stat(S_CALL,line); s->e1=e;
            return s; }
    }
}

static Block *parse_block(Parser *ps){
    Block *b=(Block*)anew(sizeof(Block));
    while(!block_follow(ps->lx.t)){
        int isret = (ps->lx.t==TK_RETURN);
        Stat *s=parse_statement(ps);
        if(s) blk_add(b,s);
        if(isret) break;
    }
    return b;
}

/* ==========================================================================
** 9. BYTECODE
** ========================================================================== */

enum {
    OP_MOVE, OP_LOADK, OP_LOADNIL, OP_LOADBOOL,
    OP_GETGLOBAL, OP_SETGLOBAL, OP_GETUPVAL, OP_SETUPVAL,
    OP_GETTABLE, OP_SETTABLE, OP_NEWTABLE, OP_SETLIST, OP_SELF,
    OP_ADD, OP_SUB, OP_MUL, OP_DIV, OP_MOD, OP_POW,
    OP_UNM, OP_NOT, OP_LEN, OP_CONCAT,
    OP_EQ, OP_NE, OP_LT, OP_LE, OP_GT, OP_GE, OP_IN,
    OP_JMP, OP_JMPIF, OP_JMPIFNOT,
    OP_CALL, OP_RETURN, OP_CLOSURE, OP_VARARG, OP_CLOSE,
    OP_FORPREP, OP_FORLOOP, OP_TFORLOOP,
    OP_COUNT
};

#define I_ABC(o,a,b,c) (((uint32_t)(o)<<24)|(((uint32_t)(a)&0xFFu)<<16)|(((uint32_t)(b)&0xFFu)<<8)|((uint32_t)(c)&0xFFu))
#define I_ABx(o,a,bx)  (((uint32_t)(o)<<24)|(((uint32_t)(a)&0xFFu)<<16)|((uint32_t)(bx)&0xFFFFu))
#define I_AsBx(o,a,s)  I_ABx(o,a,(int)(s)+32767)

#define GET_OP(i)   ((int)((i)>>24))
#define GET_A(i)    ((int)(((i)>>16)&0xFFu))
#define GET_B(i)    ((int)(((i)>>8)&0xFFu))
#define GET_C(i)    ((int)((i)&0xFFu))
#define GET_Bx(i)   ((int)((i)&0xFFFFu))
#define GET_sBx(i)  (GET_Bx(i)-32767)

/* ==========================================================================
** 10. COMPILER (AST -> bytecode)
** ========================================================================== */

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

/* patch a jump at `pc` so that it continues at `target` */
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
static Proto *compile_proto(FuncState *parent,FuncBody *fb,Str *source);

static int multiret(Expr *e){ return e->k==E_CALL||e->k==E_METHCALL||e->k==E_VARARG; }

/* compile e producing `nres` results (nres<0 = all), returns first register */
static int comp_multi(FuncState *fs,Expr *e,int nres){
    if(e->k==E_CALL||e->k==E_METHCALL) return comp_call(fs,e,nres);
    /* vararg */
    int r=reserve(fs,1);
    emit(fs,I_ABC(OP_VARARG,r,nres<0?0:nres+1,0),e->line);
    if(nres>1) reserve(fs,nres-1);
    return r;
}

/* compile expression into a temporary or existing register, return that reg */
static int exprtmp(FuncState *fs,Expr *e){
    if(e->k==E_NAME){
        int r=findlocal(fs,e->name);
        if(r>=0) return r;
    }
    int r=reserve(fs,1);
    exprd(fs,e,r);
    return r;
}

static int comp_call(FuncState *fs,Expr *e,int nres){
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
            /* very long literal: fall back to explicit index stores */
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
        case E_FUNC: {
            Proto *np=compile_proto(fs,e->fb,fs->source);
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

/* compile a list of expressions into nvars consecutive registers at `base` */
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
            /* condition can see the body's locals -> evaluate before leaveblock */
            comp_block(fs,s->body);
            int save=fs->freereg;
            int r=reserve(fs,1);
            exprd(fs,s->e1,r);
            fs->freereg=save;
            int jf=emit(fs,I_AsBx(OP_JMPIFNOT,r,0),s->line);
            leaveblock(fs,s->line);
            patch(fs,jf,start);
            /* fallthrough when condition is true */
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
        case S_RETURN: {
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

static Proto *compile_proto(FuncState *parent,FuncBody *fb,Str *source){
    FuncState fs; memset(&fs,0,sizeof fs);
    fs.prev=parent;
    fs.p=proto_new();
    fs.p->source=source;
    fs.p->name=fb->name?fb->name:str_fromc("?");
    fs.p->nparams=fb->nparams;
    fs.p->isvararg=fb->isvararg;
    fs.source=source;
    fs.line=fb->line;
    for(int i=0;i<fb->nparams;i++) newlocal(&fs,fb->params[i]);
    fs.freereg=fs.nlocals;
    fs.p->maxstack = fs.nlocals+2;
    BlockCnt bl; enterblock(&fs,&bl,0);
    comp_block(&fs,fb->body);
    leaveblock(&fs,fb->line);
    emit(&fs,I_ABC(OP_RETURN,0,1,0),fb->line);
    if(fs.p->maxstack<2) fs.p->maxstack=2;
    return fs.p;
}

/* full compile of a source chunk -> closure for the main function */
Closure *luc_compile(const char *src,int len,const char *chunkname){
    Parser ps; memset(&ps,0,sizeof ps);
    ps.lx.p=src; ps.lx.end=src+len; ps.lx.line=1;
    ps.lx.source=str_fromc(chunkname);
    lx_next(&ps.lx);
    Block *b=parse_block(&ps);
    if(ps.lx.t!=TK_EOF) perr(&ps,"'<eof>' expected");
    FuncBody fb; memset(&fb,0,sizeof fb);
    fb.body=b; fb.isvararg=1; fb.line=0; fb.name=str_fromc("main chunk");
    fb.params=(Str**)anew(sizeof(Str*)*2);
    Proto *p=compile_proto(NULL,&fb,ps.lx.source);
    return closure_new(p);
}

/* ==========================================================================
** 11. VM
** ========================================================================== */

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
            return r;
        }
        case LT_LIST:
            if(k.t==LT_NUM) return tab_get(AS_TAB(t),k);
            if(k.t==LT_STR) return tab_get(V.listmeta,k);
            return tab_get(AS_TAB(t),k);
        case LT_STR:
            if(k.t==LT_STR) return tab_get(V.stringlib,k);
            return NIL;
        case LT_BUFFER: return k.t==LT_STR? tab_get(V.bufferlib,k):NIL;
        case LT_FILE:   return k.t==LT_STR? tab_get(V.filelib,k):NIL;
        default:
            luc_error("attempt to index a %s value",type_name(t));
    }
    return NIL;
}
static void vm_setindex(Value t,Value k,Value v){
    if(t.t==LT_TABLE||t.t==LT_LIST) tab_set(AS_TAB(t),k,v);
    else luc_error("attempt to index a %s value",type_name(t));
}
int vm_len(Value v){
    switch(v.t){
        case LT_STR: return AS_STR(v)->len;
        case LT_TABLE: return tab_len(AS_TAB(v));
        case LT_LIST: return AS_TAB(v)->alen;
        case LT_BUFFER: return AS_BUF(v)->len;
        default: luc_error("attempt to get length of a %s value",type_name(v));
    }
    return 0;
}
int vm_in(Value x,Value c){
    if(c.t==LT_LIST||c.t==LT_TABLE){
        Table *t=AS_TAB(c);
        for(int i=0;i<t->alen;i++) if(val_rawequal(t->arr[i],x)) return 1;
        for(int i=0;i<t->ecap;i++)
            if(t->ents[i].k.t!=LT_NIL && val_rawequal(t->ents[i].v,x)) return 1;
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

/* generic call usable from C; results are left at `func`, count returned */
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

/* --- metatable-lite runtime (after vm_call is available) ---------------- */

/* call f(args...) from deep inside VM/C helpers; single result on return.
   NOTE: may reallocate L->stack and L->ci — caller must refresh base/pc. */
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

/* lookup metamethod 'ev' for a binary op: x side first, then y side.
   returns 1 and fills *out when a handler fired. */
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
        &&vm_op_TFORLOOP
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
        VM_LABEL(SETTABLE) { vm_setindex(base[A],base[GET_B(ins)],base[GET_C(ins)]); } VM_NEXT;
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
                pushframe(L,func,na,nres);
                goto reentry;
            } else luc_error("attempt to call a %s value",type_name(f));
        } VM_NEXT;
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
            double init=arith_num(base[A]), lim=arith_num(base[A+1]), st=arith_num(base[A+2]);
            base[A]=mknum(init-st); base[A+1]=mknum(lim); base[A+2]=mknum(st);
            pc+=GET_sBx(ins);
        } VM_NEXT;
        VM_LABEL(FORLOOP) {
            double idx=base[A].u.n+base[A+2].u.n;
            double lim=base[A+1].u.n, st=base[A+2].u.n;
            if(st>0? idx<=lim : idx>=lim){
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
#if !defined(__GNUC__) && !defined(__clang__)
        default: luc_error("bad opcode %d",GET_OP(ins));
#endif
    }
#undef VM_LABEL
#undef VM_NEXT
}

/* ======== coroutines + task scheduler ======== */

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
                /* first start: function already at stack[0] */
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

/* ==========================================================================
** 13. task scheduler
** ========================================================================== */

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

const char *const HEXD="0123456789abcdef";

int hexval(int c){
    if(c>='0'&&c<='9') return c-'0';
    if(c>='a'&&c<='f') return c-'a'+10;
    if(c>='A'&&c<='F') return c-'A'+10;
    return -1;
}

/* ======== lib argument-check helpers ======== */

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
/* ======== module system ======== */

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
    /* fallback: also try CWD in case scriptdir is empty or relative */
    if((src=try_dir(".",rel,len,found,fcap))) return src;
    /* local bundle first: ./luc_modules shadows the installed LUC_PATH so a
       refreshed copy next to the project is always the one that loads */
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

/* ======== library registration ======== */

void reg(Table *t,const char *name,CFn fn){
    tab_set(t,cstrv(name),mkobj(LT_CFUNC,cfunc_new(fn,name,0)));
}
Table *newlib(const char *name){
    Table *t=tab_new(0);
    tab_set(V.globals,cstrv(name),mkobj(LT_TABLE,t));
    return t;
}

/* the standard libs live in luc_lib_*.c; each exports one open function */
static void luc_openlibs(void){
    lucL_open_base();
    lucL_open_string();
    lucL_open_list();
    lucL_open_math();
    lucL_open_os();
    lucL_open_io();
    lucL_open_buffer();
    lucL_open_coro();
}

/* ======== init + driver ======== */

static void luc_init(void){
    memset(&V,0,sizeof V);
    V.strcap=256;
    V.strtab=(Str**)lcalloc(sizeof(Str*)*(size_t)V.strcap);
    V.gcthresh=1u<<16;
    V.gcoff=1;                               /* no GC while bootstrapping */
    V.globals=tab_new(0);
    V.loaded=tab_new(0);                     /* module cache */
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
        sched_run();                      /* drain task.delay / task.wait */
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

static void print_help(void){
    printf(
    "%s  --  the LUC programming language\n\n"
    "usage: luc [options] [script [args...]]\n\n"
    "  script.luc        run a LUC source file\n"
    "  -e \"chunk\"        execute a chunk of LUC code given on the command line\n"
    "  -v, --version     print version information and exit\n"
    "  -h, --help        print this help and exit\n\n"
    "LUC is an independent, register-based language implemented in C99.\n"
    "Pipeline: lexer -> parser -> AST -> bytecode compiler -> VM (mark & sweep GC).\n"
    "Syntax follows Lua 5.1 (all 21 keywords) plus LUC additions:\n"
    "  import <name>       - load built-in or third-party modules (ai, json, ...)\n"
    "  setmetatable/metatable-lite: __index, __call, __add..__pow, __unm, __tostring\n"
    "  [1,2,3] list literals with :append/:pop/:insert/:remove/:len/:contains\n"
    "  the 'in' membership operator, optional type annotations (x: number)\n"
    "  string.split/trim/startswith/endswith/tohex/fromhex\n"
    "  io.replace/clearline/eraseline/clear for terminal output control\n"
    "  task.*, buffer.* (incl. hex helpers), bit32.*\n"
    "  require(\"window\") for SDL2 graphics (build with -DLUC_WINDOW)\n",
    LUC_VERSION);
}

/* -mwindows builds are GUI-subsystem: cmd does not attach stdout/stderr,
   so print()/errors are invisible when run from a terminal. Re-attach to
   the parent console if we have no standard handles (GUI subsystem run
   from cmd/PowerShell). Double-clicked from Explorer: stay silent. */
#if defined(_WIN32)
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
    if(out && out!=INVALID_HANDLE_VALUE){        /* console build or redirected */
        luc_win_enable_vt(out);                  /* io.replace ANSI erase works */
        return;
    }
    if(!AttachConsole(ATTACH_PARENT_PROCESS)) return;   /* double-click: quiet */
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

int main(int argc,char **argv){
    luc_win_attach_console();
    if(argc>0) snprintf(g_exepath,sizeof g_exepath,"%s",argv[0]);
    luc_init();

    if(argc<2){ print_help(); return 0; }

    if(strcmp(argv[1],"--version")==0||strcmp(argv[1],"-v")==0){
        printf("%s  [C99 register VM]\n",LUC_VERSION);
        return 0;
    }
    if(strcmp(argv[1],"--help")==0||strcmp(argv[1],"-h")==0){
        print_help();
        return 0;
    }
    if(strcmp(argv[1],"-e")==0){
        if(argc<3){ fprintf(stderr,"luc: '-e' needs an argument\n"); return 1; }
        return run_chunk(argv[2],(int)strlen(argv[2]),"=(command line)",argc,argv,3);
    }

    int len=0;
    char *src=read_file(argv[1],&len);
    if(!src){ fprintf(stderr,"luc: cannot open '%s'\n",argv[1]); return 1; }
    /* allow a #! line at the start of a script */
    int off=0;
    if(len>1 && src[0]=='#'){ while(off<len && src[off]!='\n') off++; }
    set_scriptdir(argv[1]);
    int rc=run_chunk(src+off,len-off,argv[1],argc,argv,2);
    free(src);
    return rc;
}