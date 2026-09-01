/*
** ============================================================================
**  LUC 0.1  --  an independent, register-based scripting language
**
**  luc.h  --  shared header: the contract between luc_core.c and the libs
**
**  Layout of the project:
**    luc_core.c        language core: platform, values, GC, tables, lexer,
**                      parser, compiler, VM, coroutines, scheduler, driver
**    luc_lib_base.c    base library  (print, pcall, require, xpcall, ...)
**    luc_lib_string.c  string library + Lua-style pattern matching
**    luc_lib_list.c    list methods + table library
**    luc_lib_math.c    math library + bit32
**    luc_lib_os.c      os library
**    luc_lib_io.c      io library + file methods
**    luc_lib_buffer.c  buffer library
**    luc_lib_coro.c    coroutine + task libraries
**    luc_lib_json.c    json module          (require "json")
**    luc_lib_window.c  window module (SDL2) (require "window", -DLUC_WINDOW)
**
**  Each lib exports exactly ONE entry point: lucL_open_xxx() (or
**  lucL_xxx_module() for require-only modules).  Everything else inside a
**  lib file stays static.
** ============================================================================
*/
#ifndef LUC_H
#define LUC_H

#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <math.h>
#include <ctype.h>
#include <setjmp.h>
#include <time.h>
#include <stdint.h>
#include <stddef.h>

#define LUC_VERSION   "LUC 0.1"
#define LUC_MAXREG    200
#define LUC_MAXUPVAL  60
#define LUC_MAXCI     220
#define LUC_MAXSTACK  1000000
#define LUC_YIELDCODE (-1)

/* ==========================================================================
** 1. values & objects
** ========================================================================== */

typedef struct Obj      Obj;
typedef struct Str      Str;
typedef struct Table    Table;
typedef struct Proto    Proto;
typedef struct Closure  Closure;
typedef struct CFunc    CFunc;
typedef struct Buffer   Buffer;
typedef struct FileH    FileH;
typedef struct Upval    Upval;
typedef struct LucState LucState;

enum {
    LT_NIL=0, LT_BOOL, LT_NUM, LT_STR, LT_TABLE, LT_LIST, LT_FUNC,
    LT_CFUNC, LT_BUFFER, LT_CORO, LT_FILE, LT_PROTO, LT_UPVAL
};

typedef struct {
    int t;
    union { double n; int b; Obj *o; } u;
} Value;

struct Obj { unsigned char type, marked; Obj *next; };

struct Str  { Obj o; Str *snext; int len; unsigned hash; char s[1]; };

typedef struct { Value k, v; } Entry;
struct Table {
    Obj o;
    Value *arr;  int alen, acap;      /* array part: indices 1..alen        */
    Entry *ents; int ecap, ecount;    /* hash part: open addressing          */
    unsigned tid, ver;               /* inline cache identity + version     */
    struct Table *meta;              /* metatable-lite: __index/__call/...  */
};

typedef struct { unsigned char instack; unsigned char idx; } UpvalDesc;
struct Proto {
    Obj o;
    uint32_t *code; int ncode, ccap;
    int      *lines;
    Value    *k;    int nk, kcap;
    Proto   **p;    int np, pcap;
    UpvalDesc upvals[LUC_MAXUPVAL];
    int nparams, isvararg, maxstack, nup;
    Str *name, *source;
};

struct Upval {
    Obj o; LucState *L; int idx; int isclosed; Value closed; Upval *next;
};
#define UPVAL_PTR(u) ((u)->isclosed ? &(u)->closed : &(u)->L->stack[(u)->idx])

struct Closure { Obj o; Proto *p; int nup; Upval **up; };

typedef int (*CFn)(LucState *L,int base,int nargs,CFunc *self);
struct CFunc { Obj o; CFn fn; const char *name; int nup; Value *up; };

struct Buffer { Obj o; int len; unsigned char *b; };
struct FileH  { Obj o; FILE *f; int closed, isstd, ispipe; };

typedef struct CallInfo {
    Closure *cl;
    int func, base, nresults;
    uint32_t *savedpc;
} CallInfo;

enum { CO_START=0, CO_SUSPENDED, CO_RUNNING, CO_NORMAL, CO_DEAD };

struct LucState {
    Obj o;
    Value *stack; int stacksize, top;
    CallInfo *ci; int nci, cicap;
    Upval *openupv;
    int status;
    LucState *resumer;
    int yield_A, yield_C;      /* where to place values on resume */
    int yieldbase, nyield;     /* where the yielded values live   */
    int npending;              /* args pre-pushed for first resume */
    double waketime; int scheduled;
    Str *cursource; int curline;
};

extern Value NIL;

#define AS_STR(v)   ((Str*)(v).u.o)
#define AS_TAB(v)   ((Table*)(v).u.o)
#define AS_CL(v)    ((Closure*)(v).u.o)
#define AS_CF(v)    ((CFunc*)(v).u.o)
#define AS_BUF(v)   ((Buffer*)(v).u.o)
#define AS_CO(v)    ((LucState*)(v).u.o)
#define AS_FILE(v)  ((FileH*)(v).u.o)

/* ==========================================================================
** 2. global VM state (defined in luc_core.c)
** ========================================================================== */

typedef struct ErrJmp { jmp_buf jb; struct ErrJmp *prev; } ErrJmp;

typedef struct { LucState *co; double wake; } SchedEntry;

typedef struct LucV {
    Obj  *objects;
    Str **strtab; int strcap, nstr;
    size_t nalloc, gcthresh;
    int    gcoff;

    Table *globals;
    Table *stringlib, *listmeta, *bufferlib, *filelib;

    LucState *mainco, *cur;

    ErrJmp *errjmp;
    Value   errval;

    SchedEntry *sched; int nsched, schedcap;
    Table *loaded;           /* module cache for require() */
    unsigned tidcounter;     /* inline cache table identity */
} LucV;

extern LucV V;

/* VM yield machinery (defined in luc_core.c, used by coroutine/task libs) */
typedef struct YieldPt { jmp_buf jb; struct YieldPt *prev; LucState *co; int cdepth; } YieldPt;
extern YieldPt *g_yp;

/* ==========================================================================
** core functions shared with the libs
** ========================================================================== */

/* platform (luc_core.c) */
double luc_now(void);
void   luc_sleep(double s);
void  *lmalloc(size_t n);
void  *lrealloc(void *p,size_t n);
void  *lcalloc(size_t n);

/* values (small helpers are static inline below) */
int truthy(Value v);
const char *type_name(Value v);

/* errors */
void luc_error(const char *fmt,...);
void luc_throw(Value err);

/* strings */
Str *str_new(const char *s,int len);
void num2str(double n,char *buf,size_t sz);
int  str2num(const char *s,int len,double *out);
Str *tostr(Value v);
extern const char *const HEXD;          /* "0123456789abcdef" (string+buffer) */
int  hexval(int c);                     /* hex digit -> value (buffer/json/window) */

static inline Value mknum(double d){ Value v; v.t=LT_NUM; v.u.n=d; return v; }
static inline Value mkbool(int b){ Value v; v.t=LT_BOOL; v.u.b=(b!=0); return v; }
static inline Value mkobj(int t,void*o){ Value v; v.t=t; v.u.o=(Obj*)o; return v; }
static inline Str *str_fromc(const char *s){ return str_new(s,(int)strlen(s)); }
static inline Value strv(const char *s,int n){ return mkobj(LT_STR,str_new(s,n)); }
static inline Value cstrv(const char *s){ return mkobj(LT_STR,str_fromc(s)); }

/* tables & lists */
Table *tab_new(int islist);
Value  tab_get(Table *t,Value k);
void   tab_set(Table *t,Value k,Value v);
int    tab_len(Table *t);
int    tab_next(Table *t,Value key,Value *ok,Value *ov);
void   list_push(Table *t,Value v);
void   list_insert(Table *t,int pos,Value v);
Value  list_removeat(Table *t,int pos);
int    val_rawequal(Value a,Value b);

/* object constructors */
Buffer   *buf_new(int n);
CFunc    *cfunc_new(CFn fn,const char *name,int nup);
FileH    *file_new(FILE *f,int isstd);
LucState *state_new(int stacksize);
void      ensure_stack(LucState *L,int need);

/* GC */
void gc_collect(void);

/* VM internals used by libs */
int  vm_call(LucState *L,int func,int nargs,int nres);
int  vm_len(Value v);
int  vm_lessthan(Value a,Value b,int orequal);
int  vm_in(Value x,Value c);
void close_upvals(LucState *L,int level);
int  co_resume(LucState *co,Value *args,int nargs,Value *res,int *nres);
void sched_add(LucState *co,double wake);
void sched_remove(int i);
void sched_run(void);

/* compiler + module system (used by require) */
Closure *luc_compile(const char *src,int len,const char *chunkname);
char *find_module(const char *name,int *len,char *found,size_t fcap);

/* library registration helpers (defined in luc_core.c) */
void   reg(Table *t,const char *name,CFn fn);
Table *newlib(const char *name);

/* argument-check helpers (defined in luc_core.c) */
double   checknum (LucState *L,int base,int nargs,int i,const char *fn);
int      checkint (LucState *L,int base,int nargs,int i,const char *fn);
Str     *checkstr (LucState *L,int base,int nargs,int i,const char *fn);
Table   *checktab (LucState *L,int base,int nargs,int i,const char *fn);
Buffer  *checkbuf (LucState *L,int base,int nargs,int i,const char *fn);
uint32_t checku32 (LucState *L,int base,int nargs,int i,const char *fn);

/* ==========================================================================
** lib-side helpers
** ========================================================================== */

#define LFN(name) static int name(LucState *L,int base,int nargs,CFunc *self)
#define UNUSED_SELF (void)self

static inline Value argv_(LucState *L,int base,int nargs,int i){
    return i<nargs? L->stack[base+i] : NIL;
}
#define AR(i) argv_(L,base,nargs,(i))
#define RET(i,v) do{ ensure_stack(L,base+(i)+2); L->stack[base+(i)]=(v); }while(0)

/* shared across base <-> table libs (defined in luc_lib_base.c) */
int f_unpack(LucState *L,int base,int nargs,CFunc *self);

/* ==========================================================================
** module entry points (each lib exports exactly one)
** ========================================================================== */

void  lucL_open_base(void);                 /* luc_lib_base.c   */
void  lucL_open_string(void);               /* luc_lib_string.c */
void  lucL_open_list(void);                 /* luc_lib_list.c   */
void  lucL_open_math(void);                 /* luc_lib_math.c   */
void  lucL_open_os(void);                   /* luc_lib_os.c     */
void  lucL_open_io(void);                   /* luc_lib_io.c     */
void  lucL_open_buffer(void);               /* luc_lib_buffer.c */
void  lucL_open_coro(void);                 /* luc_lib_coro.c   */
Value lucL_json_module(void);               /* luc_lib_json.c   (require)  */
Value lucL_window_module(void);             /* luc_lib_window.c (require)  */

#endif /* LUC_H */
