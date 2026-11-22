/* luc_ffi.c: lucffi, a small FFI for LUC (included from luc_libs.c).
   require("lucffi") gives a LuaJIT-style ffi:
     cdef load C new typeof cast sizeof offsetof metatype istype string copy fill gc os arch
   plus LUC extras: iscdata addressof totable fromtable.
   C data ("cdata") are LUC tables carrying hidden keys; their metatable reads and writes the
   C memory through __index/__newindex.  Pointers to plain types (char*, float*, void*, opaque
   structs) come back as plain numbers, so pointer arithmetic is ordinary `+` on bytes; pointers
   to declared structs come back as cdata views (p.field, p[i]).  NULL is nil.
   Calls: x86-64 SysV and Linux aarch64 pass ints and floats by class (32 integer slots, 8 fp
   slots); Win64 passes by position.  No varargs, no structs by value. */

#if defined(_WIN32)
#  include <windows.h>
#else
#  include <dlfcn.h>
#endif

#define FFI_NI 32    /* integer argument slots */
#define FFI_NF 8     /* fp argument slots (register-only by design) */

enum { FK_VOID, FK_I8, FK_U8, FK_I16, FK_U16, FK_I32, FK_U32, FK_I64, FK_U64,
       FK_F32, FK_F64, FK_BOOL, FK_PTR, FK_ARR, FK_STRUCT, FK_FUNC };

typedef struct { char *name; int type; size_t off; } FField;
typedef struct {
    int kind; size_t size, align;
    int base;              /* PTR: pointee   ARR: element   FUNC: return type */
    long count;            /* ARR: elements, -1 = [?] */
    char *name;            /* display / lookup name (heap, stable) */
    FField *f; int nf, complete, isunion;
    int mt;                /* REG slot of the user metatype table, 0 = none */
    int cmt;               /* REG slot of the cdata metatable, 0 = not built yet */
    int *args; int nargs, vararg;   /* FUNC */
} FType;

static FType *FT; static int nFT, capFT;
typedef struct { char *name; int type; } FName;        /* typedefs + "struct X" tags */
static FName *FN; static int nFN, capFN;
typedef struct { char *name; int type; } FSym;         /* declared functions / variables */
static FSym *FS; static int nFS, capFS;
typedef struct { char *name; long long v; } FConst;    /* enum constants */
static FConst *FC; static int nFC, capFC;

static Table *FREG;        /* rooted list: metatables, hidden key strings, cfuncs */
static Str *K_A,*K_T,*K_O,*K_N,*K_C,*K_CD,*K_H,*K_IDX,*K_NIDX,*K_TOS;
static Value FCF_INDEX, FCF_NEWINDEX, FCF_TOS, FCF_LIBINDEX, FCF_CTCALL, FCF_CTTOS;
static Table *F_LIBMT, *F_CTMT;
static int TY_VOID, TY_CHAR, TY_I32, TY_U64, TY_F32, TY_F64, TY_VOIDP;

static char *ffi_strdup(const char *s){ size_t n=strlen(s)+1; char *d=(char*)malloc(n); if(!d) luc_error("lucffi: out of memory"); memcpy(d,s,n); return d; }
static Value hk(Str *s){ return mkobj(LT_STR,s); }
static int freg_put(Value v){ list_push(FREG,v); return FREG->alen; }   /* slot = 1-based */
static Value freg_get(int slot){ return slot>0 && slot<=FREG->alen? FREG->arr[slot-1] : NIL; }
static Str *fkey(const char *s){ Str *k=str_fromc(s); list_push(FREG,mkobj(LT_STR,k)); return k; }

/* ---------- type table ---------- */
static int ft_new(int kind,size_t size,size_t align,const char *name){
    if(nFT==capFT){ capFT=capFT?capFT*2:128; FT=(FType*)realloc(FT,sizeof(FType)*(size_t)capFT); if(!FT) luc_error("lucffi: out of memory"); }
    FType *t=&FT[nFT]; memset(t,0,sizeof *t);
    t->kind=kind; t->size=size; t->align=align?align:1; t->name=ffi_strdup(name?name:"?");
    t->base=-1; t->count=0; t->complete=(kind!=FK_STRUCT);
    return nFT++;
}
static void fn_add(const char *name,int type){
    for(int i=0;i<nFN;i++) if(strcmp(FN[i].name,name)==0){ FN[i].type=type; return; }
    if(nFN==capFN){ capFN=capFN?capFN*2:128; FN=(FName*)realloc(FN,sizeof(FName)*(size_t)capFN); }
    FN[nFN].name=ffi_strdup(name); FN[nFN].type=type; nFN++;
}
static int fn_find(const char *name){
    for(int i=nFN-1;i>=0;i--) if(strcmp(FN[i].name,name)==0) return FN[i].type;
    return -1;
}
static int ft_ptr(int base){
    for(int i=0;i<nFT;i++) if(FT[i].kind==FK_PTR && FT[i].base==base) return i;
    char nm[256]; snprintf(nm,sizeof nm,"%s*",FT[base].name);
    int p=ft_new(FK_PTR,sizeof(void*),sizeof(void*),nm); FT[p].base=base; return p;
}
static int ft_arr(int base,long count){
    for(int i=0;i<nFT;i++) if(FT[i].kind==FK_ARR && FT[i].base==base && FT[i].count==count) return i;
    char nm[256];
    if(count<0) snprintf(nm,sizeof nm,"%s[?]",FT[base].name); else snprintf(nm,sizeof nm,"%s[%ld]",FT[base].name,count);
    size_t sz= count>0? FT[base].size*(size_t)count : 0;
    int a=ft_new(FK_ARR,sz,FT[base].align,nm); FT[a].base=base; FT[a].count=count; return a;
}
static int ft_struct_tag(const char *tag,int isunion){
    char nm[160]; snprintf(nm,sizeof nm,"%s %s",isunion?"union":"struct",tag);
    int t=fn_find(nm);
    if(t>=0) return t;
    t=ft_new(FK_STRUCT,0,1,nm); FT[t].isunion=isunion; fn_add(nm,t); return t;
}
static void ffi_types_init(void){
    if(nFT) return;
    static const struct { const char *n; int k; size_t s; } B[]={
        {"void",FK_VOID,0},{"char",FK_I8,1},{"int8_t",FK_I8,1},{"uint8_t",FK_U8,1},
        {"int16_t",FK_I16,2},{"uint16_t",FK_U16,2},{"int32_t",FK_I32,4},{"uint32_t",FK_U32,4},
        {"int64_t",FK_I64,8},{"uint64_t",FK_U64,8},{"float",FK_F32,4},{"double",FK_F64,8},
        {"bool",FK_BOOL,1},{"_Bool",FK_BOOL,1},{"size_t",FK_U64,8},{"ssize_t",FK_I64,8},
        {"ptrdiff_t",FK_I64,8},{"intptr_t",FK_I64,8},{"uintptr_t",FK_U64,8},
        {"int",FK_I32,4},{"unsigned char",FK_U8,1},{"signed char",FK_I8,1},{"short",FK_I16,2},
        {"unsigned short",FK_U16,2},{"unsigned int",FK_U32,4},{"long long",FK_I64,8},
        {"unsigned long long",FK_U64,8},
#if defined(_WIN32)
        {"long",FK_I32,4},{"unsigned long",FK_U32,4},
#else
        {"long",FK_I64,8},{"unsigned long",FK_U64,8},
#endif
        {"long double",FK_F64,8},
    };
    for(size_t i=0;i<sizeof B/sizeof B[0];i++){
        int id=ft_new(B[i].k,B[i].s,B[i].s?B[i].s:1,B[i].n); fn_add(B[i].n,id);
    }
    TY_VOID=fn_find("void"); TY_CHAR=fn_find("char"); TY_I32=fn_find("int32_t");
    TY_U64=fn_find("uint64_t"); TY_F32=fn_find("float"); TY_F64=fn_find("double");
    TY_VOIDP=ft_ptr(TY_VOID);
}

/* ---------- C declaration parser ---------- */
enum { TK_EOF, TK_ID, TK_NUM, TK_P };
typedef struct { const char *s; int n, i; int tt; char tok[128]; long long num; int line; } FP;

static void fp_err(FP *p,const char *msg){
    luc_error("lucffi.cdef: %s near '%s' (line %d of the declarations)",msg,p->tt==TK_EOF?"<end>":p->tok,p->line);
}
static void fp_next(FP *p){
    for(;;){
        while(p->i<p->n && isspace((unsigned char)p->s[p->i])){ if(p->s[p->i]=='\n') p->line++; p->i++; }
        if(p->i+1<p->n && p->s[p->i]=='/' && p->s[p->i+1]=='*'){
            p->i+=2; while(p->i+1<p->n && !(p->s[p->i]=='*'&&p->s[p->i+1]=='/')){ if(p->s[p->i]=='\n') p->line++; p->i++; }
            p->i+=2; continue;
        }
        if(p->i+1<p->n && p->s[p->i]=='/' && p->s[p->i+1]=='/'){ while(p->i<p->n && p->s[p->i]!='\n') p->i++; continue; }
        if(p->i<p->n && p->s[p->i]=='#'){ while(p->i<p->n && p->s[p->i]!='\n') p->i++; continue; }
        break;
    }
    if(p->i>=p->n){ p->tt=TK_EOF; p->tok[0]=0; return; }
    char c=p->s[p->i];
    if(isalpha((unsigned char)c)||c=='_'){
        int k=0; while(p->i<p->n && (isalnum((unsigned char)p->s[p->i])||p->s[p->i]=='_')){ if(k<127) p->tok[k++]=p->s[p->i]; p->i++; }
        p->tok[k]=0; p->tt=TK_ID; return;
    }
    if(isdigit((unsigned char)c)){
        int k=0; while(p->i<p->n && (isalnum((unsigned char)p->s[p->i]))){ if(k<127) p->tok[k++]=p->s[p->i]; p->i++; }
        p->tok[k]=0; p->tt=TK_NUM; p->num=(long long)strtoull(p->tok,NULL,0); return;
    }
    if(c=='.' && p->i+2<p->n && p->s[p->i+1]=='.' && p->s[p->i+2]=='.'){ strcpy(p->tok,"..."); p->i+=3; p->tt=TK_P; return; }
    if((c=='<'||c=='>') && p->i+1<p->n && p->s[p->i+1]==c){ p->tok[0]=c; p->tok[1]=c; p->tok[2]=0; p->i+=2; p->tt=TK_P; return; }
    p->tok[0]=c; p->tok[1]=0; p->i++; p->tt=TK_P;
}
static int fp_is(FP *p,const char *t){ return p->tt!=TK_EOF && strcmp(p->tok,t)==0; }
static void fp_expect(FP *p,const char *t){ if(!fp_is(p,t)){ char m[64]; snprintf(m,sizeof m,"expected '%s'",t); fp_err(p,m); } fp_next(p); }
static int fp_qual(FP *p){
    static const char *Q[]={"const","volatile","extern","static","inline","__inline","__inline__",
        "restrict","__restrict","__restrict__","register","__extension__","__cdecl","__stdcall",
        "__declspec","__attribute__","__host__","__device__","CUDARTAPI","CUBLASAPI","WINAPI",NULL};
    if(p->tt!=TK_ID) return 0;
    for(int i=0;Q[i];i++) if(strcmp(p->tok,Q[i])==0){
        fp_next(p);
        if((strcmp(Q[i],"__attribute__")==0||strcmp(Q[i],"__declspec")==0) && fp_is(p,"(")){
            int d=0; do{ if(fp_is(p,"(")) d++; else if(fp_is(p,")")) d--; fp_next(p); }while(d>0 && p->tt!=TK_EOF);
        }
        return 1;
    }
    return 0;
}
static long long fp_expr(FP *p);
static long long fp_prim(FP *p){
    if(fp_is(p,"-")){ fp_next(p); return -fp_prim(p); }
    if(fp_is(p,"~")){ fp_next(p); return ~fp_prim(p); }
    if(fp_is(p,"(")){ fp_next(p); long long v=fp_expr(p); fp_expect(p,")"); return v; }
    if(p->tt==TK_NUM){ long long v=p->num; fp_next(p); return v; }
    if(p->tt==TK_ID){
        for(int i=nFC-1;i>=0;i--) if(strcmp(FC[i].name,p->tok)==0){ fp_next(p); return FC[i].v; }
        if(strcmp(p->tok,"sizeof")==0) fp_err(p,"sizeof in constant expressions is not supported");
        fp_err(p,"unknown constant");
    }
    fp_err(p,"bad constant expression"); return 0;
}
static long long fp_expr(FP *p){
    long long v=fp_prim(p);
    for(;;){
        if(fp_is(p,"+")){ fp_next(p); v+=fp_prim(p); }
        else if(fp_is(p,"-")){ fp_next(p); v-=fp_prim(p); }
        else if(fp_is(p,"*")){ fp_next(p); v*=fp_prim(p); }
        else if(fp_is(p,"|")){ fp_next(p); v|=fp_prim(p); }
        else if(fp_is(p,"&")){ fp_next(p); v&=fp_prim(p); }
        else if(fp_is(p,"<<")){ fp_next(p); v<<=fp_prim(p); }
        else if(fp_is(p,">>")){ fp_next(p); v>>=fp_prim(p); }
        else return v;
    }
}
static void fc_add(const char *name,long long v){
    if(nFC==capFC){ capFC=capFC?capFC*2:64; FC=(FConst*)realloc(FC,sizeof(FConst)*(size_t)capFC); }
    FC[nFC].name=ffi_strdup(name); FC[nFC].v=v; nFC++;
}
static int fp_declarator(FP *p,int base,char *name,size_t ncap,int allow_noname);
static void ft_layout(int st){
    FType *t=&FT[st]; size_t off=0, al=1, mx=0;
    for(int i=0;i<t->nf;i++){
        FType *f=&FT[t->f[i].type];
        size_t a=f->align?f->align:1;
        if(a>al) al=a;
        if(t->isunion){ t->f[i].off=0; if(f->size>mx) mx=f->size; }
        else { off=(off+a-1)/a*a; t->f[i].off=off; off+=f->size; }
    }
    if(t->isunion) off=mx;
    t->align=al; t->size=(off+al-1)/al*al; t->complete=1;
}
/* base type (specifiers only).  Returns -1 if the token stream does not start a type. */
static int fp_base(FP *p){
    while(fp_qual(p)){}
    if(p->tt!=TK_ID) return -1;
    if(strcmp(p->tok,"struct")==0||strcmp(p->tok,"union")==0){
        int isunion=p->tok[0]=='u'; fp_next(p); while(fp_qual(p)){}
        char tag[96]="";
        if(p->tt==TK_ID){ snprintf(tag,sizeof tag,"%s",p->tok); fp_next(p); }
        int st;
        if(tag[0]) st=ft_struct_tag(tag,isunion);
        else { st=ft_new(FK_STRUCT,0,1,isunion?"union <anon>":"struct <anon>"); FT[st].isunion=isunion; }
        if(fp_is(p,"{")){
            fp_next(p);
            FType *t=&FT[st]; t->nf=0; free(t->f); t->f=NULL; int cap=0;
            while(!fp_is(p,"}")){
                if(p->tt==TK_EOF) fp_err(p,"unterminated struct");
                int fb=fp_base(p); if(fb<0) fp_err(p,"expected a field type");
                for(;;){
                    char fname[96]; int fty=fp_declarator(p,fb,fname,sizeof fname,0);
                    if(fp_is(p,":")) fp_err(p,"bit fields are not supported");
                    if(FT[fty].kind==FK_STRUCT && !FT[fty].complete) fp_err(p,"field of incomplete struct type");
                    t=&FT[st];
                    if(t->nf==cap){ cap=cap?cap*2:16; t->f=(FField*)realloc(t->f,sizeof(FField)*(size_t)cap); }
                    t->f[t->nf].name=ffi_strdup(fname); t->f[t->nf].type=fty; t->f[t->nf].off=0; t->nf++;
                    if(fp_is(p,",")){ fp_next(p); continue; }
                    break;
                }
                fp_expect(p,";");
            }
            fp_next(p);
            ft_layout(st);
        }
        return st;
    }
    if(strcmp(p->tok,"enum")==0){
        fp_next(p); if(p->tt==TK_ID && !fp_is(p,"{")) fp_next(p);
        if(fp_is(p,"{")){
            fp_next(p); long long v=0;
            while(!fp_is(p,"}")){
                if(p->tt!=TK_ID) fp_err(p,"expected an enum constant");
                char nm[128]; snprintf(nm,sizeof nm,"%s",p->tok); fp_next(p);
                if(fp_is(p,"=")){ fp_next(p); v=fp_expr(p); }
                fc_add(nm,v); v++;
                if(fp_is(p,",")) fp_next(p);
            }
            fp_next(p);
        }
        return fn_find("int");
    }
    /* builtin keyword combinations */
    int un=0,sg=0,sh=0,lg=0,ch=0,in=0,fl=0,db=0,vd=0,any=0,bo=0;
    for(;;){
        if(p->tt!=TK_ID) break;
        if(strcmp(p->tok,"unsigned")==0) un=1;
        else if(strcmp(p->tok,"signed")==0) sg=1;
        else if(strcmp(p->tok,"short")==0) sh=1;
        else if(strcmp(p->tok,"long")==0) lg++;
        else if(strcmp(p->tok,"char")==0) ch=1;
        else if(strcmp(p->tok,"int")==0) in=1;
        else if(strcmp(p->tok,"float")==0) fl=1;
        else if(strcmp(p->tok,"double")==0) db=1;
        else if(strcmp(p->tok,"void")==0) vd=1;
        else if(strcmp(p->tok,"_Bool")==0||strcmp(p->tok,"bool")==0) bo=1;
        else if(fp_qual(p)) continue;
        else break;
        any=1; fp_next(p);
    }
    (void)in; (void)sg;
    if(any){
        const char *n;
        if(vd) n="void"; else if(fl) n="float"; else if(db) n=lg?"long double":"double";
        else if(bo) n="bool";
        else if(ch) n=un?"unsigned char":(sg?"signed char":"char");
        else if(sh) n=un?"unsigned short":"short";
        else if(lg>=2) n=un?"unsigned long long":"long long";
        else if(lg==1) n=un?"unsigned long":"long";
        else n=un?"unsigned int":"int";
        return fn_find(n);
    }
    int t=fn_find(p->tok);
    if(t<0) return -1;
    fp_next(p); while(fp_qual(p)){}
    return t;
}
static int fp_params(FP *p,int **args,int *nargs,int *vararg){
    fp_expect(p,"(");
    int cap=0,n=0; int *a=NULL; *vararg=0;
    if(fp_is(p,"void")){
        int save=p->i, line=p->line; char tk[128]; snprintf(tk,sizeof tk,"%s",p->tok); int tt=p->tt;
        fp_next(p);
        if(fp_is(p,")")){ fp_next(p); *args=NULL; *nargs=0; return 0; }
        p->i=save; p->line=line; snprintf(p->tok,sizeof p->tok,"%s",tk); p->tt=tt;
    }
    while(!fp_is(p,")")){
        if(fp_is(p,"...")){ *vararg=1; fp_next(p); continue; }
        int b=fp_base(p); if(b<0) fp_err(p,"expected a parameter type");
        char nm[96]; int t=fp_declarator(p,b,nm,sizeof nm,1);
        if(FT[t].kind==FK_ARR) t=ft_ptr(FT[t].base);
        if(n==cap){ cap=cap?cap*2:8; a=(int*)realloc(a,sizeof(int)*(size_t)cap); }
        a[n++]=t;
        if(fp_is(p,",")) fp_next(p);
    }
    fp_next(p);
    *args=a; *nargs=n; return 0;
}
/* declarator: pointers, name, arrays, function params / function pointers. */
static int fp_declarator(FP *p,int base,char *name,size_t ncap,int allow_noname){
    int t=base; name[0]=0;
    for(;;){
        if(fp_qual(p)) continue;
        if(fp_is(p,"*")){ fp_next(p); t=ft_ptr(t); continue; }
        if(fp_is(p,"&")) fp_err(p,"references are not supported");
        break;
    }
    if(fp_is(p,"(")){
        /* function pointer: ( * name ) ( params ) -> treated as void* */
        fp_next(p); while(fp_is(p,"*")||fp_qual(p)){ if(fp_is(p,"*")) fp_next(p); }
        if(p->tt==TK_ID){ snprintf(name,ncap,"%s",p->tok); fp_next(p); }
        fp_expect(p,")");
        int *a; int na,va; fp_params(p,&a,&na,&va); free(a);
        return TY_VOIDP;
    }
    if(p->tt==TK_ID){ snprintf(name,ncap,"%s",p->tok); fp_next(p); }
    else if(!allow_noname) fp_err(p,"expected a name");
    if(fp_is(p,"(")){
        int *a; int na,va; fp_params(p,&a,&na,&va);
        int f=ft_new(FK_FUNC,0,1,name[0]?name:"function");
        FT[f].base=t; FT[f].args=a; FT[f].nargs=na; FT[f].vararg=va;
        while(fp_qual(p)){}
        return f;
    }
    long dims[8]; int nd=0;
    while(fp_is(p,"[")){
        fp_next(p); long c=-1;
        if(fp_is(p,"?")||fp_is(p,"]")){ if(fp_is(p,"?")) fp_next(p); c=-1; }
        else c=(long)fp_expr(p);
        fp_expect(p,"]");
        if(nd<8) dims[nd++]=c;
    }
    for(int i=nd-1;i>=0;i--) t=ft_arr(t,dims[i]);
    return t;
}
static void fs_add(const char *name,int type){
    for(int i=0;i<nFS;i++) if(strcmp(FS[i].name,name)==0){ FS[i].type=type; return; }
    if(nFS==capFS){ capFS=capFS?capFS*2:128; FS=(FSym*)realloc(FS,sizeof(FSym)*(size_t)capFS); }
    FS[nFS].name=ffi_strdup(name); FS[nFS].type=type; nFS++;
}
static void ffi_cdef_parse(const char *src,int len){
    FP P; memset(&P,0,sizeof P); P.s=src; P.n=len; P.line=1; fp_next(&P);
    while(P.tt!=TK_EOF){
        if(fp_is(&P,";")){ fp_next(&P); continue; }
        int istd=0;
        if(fp_is(&P,"typedef")){ istd=1; fp_next(&P); }
        int b=fp_base(&P);
        if(b<0) fp_err(&P,"unknown type");
        if(fp_is(&P,";")){ fp_next(&P); continue; }          /* struct X {...}; or enum {...}; */
        for(;;){
            char nm[96]; int t=fp_declarator(&P,b,nm,sizeof nm,0);
            if(istd){
                if(FT[t].kind==FK_STRUCT && strstr(FT[t].name,"<anon>")){ free(FT[t].name); FT[t].name=ffi_strdup(nm); }
                fn_add(nm,t);
            } else fs_add(nm,t);
            if(fp_is(&P,",")){ fp_next(&P); continue; }
            break;
        }
        fp_expect(&P,";");
    }
}
/* type string for new/cast/typeof/sizeof, e.g. "float[?]", "luaTL_tensor_t", "void*[1]" */
static char  *TC_s[64]; static int TC_t[64], TC_v[64], TC_n;
static int ffi_parse_type(const char *s,int *vla){
    for(int i=0;i<TC_n;i++) if(strcmp(TC_s[i],s)==0){ *vla=TC_v[i]; return TC_t[i]; }
    FP P; memset(&P,0,sizeof P); P.s=s; P.n=(int)strlen(s); P.line=1; fp_next(&P);
    int b=fp_base(&P);
    if(b<0) luc_error("lucffi: unknown C type '%s' (declare it with lucffi.cdef first)",s);
    char nm[96]; int t=fp_declarator(&P,b,nm,sizeof nm,1);
    if(P.tt!=TK_EOF) luc_error("lucffi: cannot parse C type '%s'",s);
    int v= FT[t].kind==FK_ARR && FT[t].count<0;
    if(TC_n<64){ TC_s[TC_n]=ffi_strdup(s); TC_t[TC_n]=t; TC_v[TC_n]=v; TC_n++; }
    *vla=v; return t;
}

/* ---------- cdata ---------- */
static int ffi_cdata(Value v,int *ty,uintptr_t *addr){
    if(v.t!=LT_TABLE) return 0;
    Table *t=AS_TAB(v);
    if(!t->meta || !truthy(tab_get(t->meta,hk(K_CD)))) return 0;
    Value a=tab_get(t,hk(K_A)), y=tab_get(t,hk(K_T));
    if(a.t!=LT_NUM||y.t!=LT_NUM) return 0;
    if(ty) *ty=(int)y.u.n;
    if(addr) *addr=(uintptr_t)(uint64_t)a.u.n;
    return 1;
}
static int ffi_mt_owner(int ty){   /* struct whose metatype applies to this cdata type */
    if(FT[ty].kind==FK_PTR && FT[ty].base>=0 && FT[FT[ty].base].kind==FK_STRUCT) return FT[ty].base;
    return ty;
}
static Table *ffi_meta(int ty){
    int own=ffi_mt_owner(ty);
    if(FT[ty].cmt) return AS_TAB(freg_get(FT[ty].cmt));
    Table *m=tab_new(0);
    int slot=freg_put(mkobj(LT_TABLE,m));
    FT[ty].cmt=slot;
    tab_set(m,hk(K_CD),mkbool(1));
    tab_set(m,hk(K_IDX),FCF_INDEX);
    tab_set(m,hk(K_NIDX),FCF_NEWINDEX);
    tab_set(m,hk(K_TOS),FCF_TOS);
    if(FT[own].mt){
        Table *u=AS_TAB(freg_get(FT[own].mt));
        static const char *mm[]={"__tostring","__add","__sub","__mul","__div","__mod","__pow",
                                 "__unm","__call","__concat","__eq","__lt","__le","__len",NULL};
        for(int i=0;mm[i];i++){ Value f=tab_get(u,cstrv(mm[i])); if(f.t!=LT_NIL) tab_set(m,cstrv(mm[i]),f); }
    }
    return m;
}
static Value ffi_mkcd(int ty,uintptr_t addr,Value owner,long n){
    Table *t=tab_new(0);
    tab_set(t,hk(K_A),mknum((double)addr));
    tab_set(t,hk(K_T),mknum((double)ty));
    if(owner.t!=LT_NIL) tab_set(t,hk(K_O),owner);
    if(n>=0) tab_set(t,hk(K_N),mknum((double)n));
    t->meta=ffi_meta(ty);
    return mkobj(LT_TABLE,t);
}
static Value ffi_ptrval(int pty,uintptr_t addr){
    if(!addr) return NIL;
    int b=FT[pty].base;
    if(b>=0 && FT[b].kind==FK_STRUCT && FT[b].complete) return ffi_mkcd(pty,addr,NIL,-1);
    return mknum((double)addr);
}
static uintptr_t ffi_toptr(Value v,const char *ctx){
    int ty; uintptr_t a;
    switch(v.t){
        case LT_NIL: return 0;
        case LT_NUM: return (uintptr_t)(uint64_t)v.u.n;
        case LT_BOOL: if(!v.u.b) return 0; break;
        case LT_BUFFER: return (uintptr_t)AS_BUF(v)->b;
        case LT_STR: return (uintptr_t)AS_STR(v)->s;
        case LT_TABLE: if(ffi_cdata(v,&ty,&a)) return a; break;
        default: break;
    }
    luc_error("lucffi: %s: cannot convert a %s to a C pointer",ctx,type_name(v));
    return 0;
}
static int64_t ffi_toint(Value v,int kind,const char *ctx){
    switch(v.t){
        case LT_NUM: {
            double d=v.u.n;
            if(kind==FK_U64 && d>=9223372036854775808.0) return (int64_t)(uint64_t)d;
            if(d!=d) return 0;
            return (int64_t)d;
        }
        case LT_BOOL: return v.u.b;
        case LT_NIL: return 0;
        default: {
            int ty; uintptr_t a;
            if(ffi_cdata(v,&ty,&a)) return (int64_t)a;
            luc_error("lucffi: %s: cannot convert a %s to a C integer",ctx,type_name(v));
        }
    }
    return 0;
}
static double ffi_tonum(Value v,const char *ctx){
    if(v.t==LT_NUM) return v.u.n;
    if(v.t==LT_BOOL) return v.u.b;
    if(v.t==LT_NIL) return 0;
    luc_error("lucffi: %s: cannot convert a %s to a C number",ctx,type_name(v));
    return 0;
}
static Value ffi_read(int ty,unsigned char *p){
    switch(FT[ty].kind){
        case FK_I8:  { int8_t x;   memcpy(&x,p,1); return mknum(x); }
        case FK_U8:  { uint8_t x;  memcpy(&x,p,1); return mknum(x); }
        case FK_I16: { int16_t x;  memcpy(&x,p,2); return mknum(x); }
        case FK_U16: { uint16_t x; memcpy(&x,p,2); return mknum(x); }
        case FK_I32: { int32_t x;  memcpy(&x,p,4); return mknum(x); }
        case FK_U32: { uint32_t x; memcpy(&x,p,4); return mknum(x); }
        case FK_I64: { int64_t x;  memcpy(&x,p,8); return mknum((double)x); }
        case FK_U64: { uint64_t x; memcpy(&x,p,8); return mknum((double)x); }
        case FK_F32: { float x;    memcpy(&x,p,4); return mknum(x); }
        case FK_F64: { double x;   memcpy(&x,p,8); return mknum(x); }
        case FK_BOOL: return mkbool(*p!=0);
        case FK_PTR: { void *q; memcpy(&q,p,sizeof q); return ffi_ptrval(ty,(uintptr_t)q); }
        case FK_ARR: return mknum((double)(uintptr_t)p);        /* array field: its address */
        case FK_STRUCT: return ffi_mkcd(ty,(uintptr_t)p,NIL,-1); /* nested struct: a view */
        default: luc_error("lucffi: cannot read a value of type '%s'",FT[ty].name);
    }
    return NIL;
}
static void ffi_write(int ty,unsigned char *p,Value v,const char *ctx){
    int k=FT[ty].kind;
    switch(k){
        case FK_I8: case FK_U8: { int64_t x=ffi_toint(v,k,ctx); uint8_t y=(uint8_t)x; memcpy(p,&y,1); return; }
        case FK_I16: case FK_U16: { int64_t x=ffi_toint(v,k,ctx); uint16_t y=(uint16_t)x; memcpy(p,&y,2); return; }
        case FK_I32: case FK_U32: { int64_t x=ffi_toint(v,k,ctx); uint32_t y=(uint32_t)x; memcpy(p,&y,4); return; }
        case FK_I64: case FK_U64: { int64_t x=ffi_toint(v,k,ctx); memcpy(p,&x,8); return; }
        case FK_F32: { float x=(float)ffi_tonum(v,ctx); memcpy(p,&x,4); return; }
        case FK_F64: { double x=ffi_tonum(v,ctx); memcpy(p,&x,8); return; }
        case FK_BOOL: *p=(unsigned char)(truthy(v)?1:0); return;
        case FK_PTR: { void *q=(void*)ffi_toptr(v,ctx); memcpy(p,&q,sizeof q); return; }
        case FK_STRUCT: case FK_ARR: {
            int sty; uintptr_t a;
            if(k==FK_ARR && v.t==LT_STR){
                size_t n=(size_t)AS_STR(v)->len+1; if(FT[ty].count>=0 && n>FT[ty].size) n=FT[ty].size;
                memcpy(p,AS_STR(v)->s,n); return;
            }
            if(ffi_cdata(v,&sty,&a)){ memcpy(p,(void*)a,FT[ty].size); return; }
            luc_error("lucffi: %s: cannot assign a %s to '%s'",ctx,type_name(v),FT[ty].name);
            return;
        }
        default: luc_error("lucffi: %s: cannot write type '%s'",ctx,FT[ty].name);
    }
}
static FField *ffi_field(int st,const char *name){
    FType *t=&FT[st];
    for(int i=0;i<t->nf;i++) if(strcmp(t->f[i].name,name)==0) return &t->f[i];
    return NULL;
}
static long ffi_cd_count(Table *t,int ty){
    if(FT[ty].kind==FK_ARR){
        if(FT[ty].count>=0) return FT[ty].count;
        Value n=tab_get(t,hk(K_N)); return n.t==LT_NUM? (long)n.u.n : 0;
    }
    return -1;
}
/* element address for cd[i] */
static unsigned char *ffi_elem(Value cd,Table *t,int ty,uintptr_t a,Value k,int *ety){
    double d=k.u.n; long i=(long)d;
    if((double)i!=d) luc_error("lucffi: index %g is not an integer",d);
    int kind=FT[ty].kind, e;
    if(kind==FK_ARR){
        e=FT[ty].base; long n=ffi_cd_count(t,ty);
        if(i<0||i>=n) luc_error("lucffi: index %ld out of range for '%s' (0..%ld)",i,FT[ty].name,n-1);
    } else if(kind==FK_PTR){
        e=FT[ty].base;
        if(FT[e].size==0) luc_error("lucffi: cannot index a '%s'",FT[ty].name);
    } else {
        if(i!=0) luc_error("lucffi: cannot index a '%s' with a number",FT[ty].name);
        e=ty;
    }
    (void)cd;
    *ety=e;
    return (unsigned char*)a+(size_t)i*FT[e].size;
}
LFN(f_cd_index){ UNUSED_SELF;
    Value cd=AR(0), k=AR(1); int ty; uintptr_t a;
    if(!ffi_cdata(cd,&ty,&a)) luc_error("lucffi: bad cdata");
    Table *t=AS_TAB(cd);
    if(k.t==LT_NUM){
        int e; unsigned char *p=ffi_elem(cd,t,ty,a,k,&e);
        RET(0,ffi_read(e,p)); return 1;
    }
    int st=-1;
    if(FT[ty].kind==FK_STRUCT) st=ty;
    else if(FT[ty].kind==FK_PTR && FT[ty].base>=0 && FT[FT[ty].base].kind==FK_STRUCT) st=FT[ty].base;
    if(k.t==LT_STR && st>=0){
        FField *f=ffi_field(st,AS_STR(k)->s);
        if(f){ RET(0,ffi_read(f->type,(unsigned char*)a+f->off)); return 1; }
    }
    int own=ffi_mt_owner(ty);
    if(FT[own].mt){
        Value h=tab_get(AS_TAB(freg_get(FT[own].mt)),cstrv("__index"));
        int depth=0;
        while(h.t==LT_TABLE && depth<16){
            Value r=tab_get(AS_TAB(h),k);
            if(r.t!=LT_NIL){ RET(0,r); return 1; }
            Table *hm=AS_TAB(h)->meta; if(!hm) break;
            h=tab_get(hm,cstrv("__index")); depth++;
        }
        if(h.t==LT_FUNC||h.t==LT_CFUNC){
            int slot=base+nargs+2; ensure_stack(L,slot+8);
            L->stack[slot]=h; L->stack[slot+1]=cd; L->stack[slot+2]=k;
            vm_call(L,slot,2,1);
            Value r=L->stack[slot]; RET(0,r); return 1;
        }
        RET(0,NIL); return 1;
    }
    if(k.t==LT_STR && st>=0) luc_error("lucffi: '%s' has no member named '%s'",FT[st].name,AS_STR(k)->s);
    luc_error("lucffi: cannot index '%s' with a %s",FT[ty].name,type_name(k));
    return 0;
}
LFN(f_cd_newindex){ UNUSED_SELF;
    Value cd=AR(0), k=AR(1), v=AR(2); int ty; uintptr_t a;
    if(!ffi_cdata(cd,&ty,&a)) luc_error("lucffi: bad cdata");
    Table *t=AS_TAB(cd);
    if(k.t==LT_NUM){
        int e; unsigned char *p=ffi_elem(cd,t,ty,a,k,&e);
        ffi_write(e,p,v,"element store"); return 0;
    }
    int st=-1;
    if(FT[ty].kind==FK_STRUCT) st=ty;
    else if(FT[ty].kind==FK_PTR && FT[ty].base>=0 && FT[FT[ty].base].kind==FK_STRUCT) st=FT[ty].base;
    if(k.t==LT_STR && st>=0){
        FField *f=ffi_field(st,AS_STR(k)->s);
        if(f){ ffi_write(f->type,(unsigned char*)a+f->off,v,f->name); return 0; }
        luc_error("lucffi: '%s' has no member named '%s'",FT[st].name,AS_STR(k)->s);
    }
    luc_error("lucffi: cannot store into '%s' with a %s key",FT[ty].name,type_name(k));
    return 0;
}
LFN(f_cd_tostring){ UNUSED_SELF;
    int ty; uintptr_t a; char b[256];
    if(!ffi_cdata(AR(0),&ty,&a)) luc_error("lucffi: bad cdata");
    snprintf(b,sizeof b,"cdata<%s>: %p",FT[ty].name,(void*)a);
    RET(0,cstrv(b)); return 1;
}

/* ---------- calls ---------- */
#if defined(_WIN32)
/* generated: Win64 call shapes (slot 0..3 int=u64 / fp=double) */
typedef uint64_t (*W64I_0)(uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t);
typedef double   (*W64D_0)(uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t);
typedef uint64_t (*W64I_1)(double,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t);
typedef double   (*W64D_1)(double,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t);
typedef uint64_t (*W64I_2)(uint64_t,double,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t);
typedef double   (*W64D_2)(uint64_t,double,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t);
typedef uint64_t (*W64I_3)(double,double,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t);
typedef double   (*W64D_3)(double,double,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t);
typedef uint64_t (*W64I_4)(uint64_t,uint64_t,double,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t);
typedef double   (*W64D_4)(uint64_t,uint64_t,double,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t);
typedef uint64_t (*W64I_5)(double,uint64_t,double,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t);
typedef double   (*W64D_5)(double,uint64_t,double,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t);
typedef uint64_t (*W64I_6)(uint64_t,double,double,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t);
typedef double   (*W64D_6)(uint64_t,double,double,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t);
typedef uint64_t (*W64I_7)(double,double,double,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t);
typedef double   (*W64D_7)(double,double,double,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t);
typedef uint64_t (*W64I_8)(uint64_t,uint64_t,uint64_t,double,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t);
typedef double   (*W64D_8)(uint64_t,uint64_t,uint64_t,double,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t);
typedef uint64_t (*W64I_9)(double,uint64_t,uint64_t,double,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t);
typedef double   (*W64D_9)(double,uint64_t,uint64_t,double,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t);
typedef uint64_t (*W64I_10)(uint64_t,double,uint64_t,double,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t);
typedef double   (*W64D_10)(uint64_t,double,uint64_t,double,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t);
typedef uint64_t (*W64I_11)(double,double,uint64_t,double,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t);
typedef double   (*W64D_11)(double,double,uint64_t,double,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t);
typedef uint64_t (*W64I_12)(uint64_t,uint64_t,double,double,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t);
typedef double   (*W64D_12)(uint64_t,uint64_t,double,double,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t);
typedef uint64_t (*W64I_13)(double,uint64_t,double,double,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t);
typedef double   (*W64D_13)(double,uint64_t,double,double,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t);
typedef uint64_t (*W64I_14)(uint64_t,double,double,double,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t);
typedef double   (*W64D_14)(uint64_t,double,double,double,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t);
typedef uint64_t (*W64I_15)(double,double,double,double,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t);
typedef double   (*W64D_15)(double,double,double,double,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t);
static void ffi_call_w64(void *fp,int mask,int retfp,const uint64_t *a,const double *d,uint64_t *ri,double *rd){
    switch(mask){
    case 0: if(retfp) *rd=((W64D_0)fp)(a[0],a[1],a[2],a[3],a[4],a[5],a[6],a[7],a[8],a[9],a[10],a[11],a[12],a[13],a[14],a[15],a[16],a[17],a[18],a[19],a[20],a[21],a[22],a[23],a[24],a[25],a[26],a[27],a[28],a[29],a[30],a[31]); else *ri=((W64I_0)fp)(a[0],a[1],a[2],a[3],a[4],a[5],a[6],a[7],a[8],a[9],a[10],a[11],a[12],a[13],a[14],a[15],a[16],a[17],a[18],a[19],a[20],a[21],a[22],a[23],a[24],a[25],a[26],a[27],a[28],a[29],a[30],a[31]); break;
    case 1: if(retfp) *rd=((W64D_1)fp)(d[0],a[1],a[2],a[3],a[4],a[5],a[6],a[7],a[8],a[9],a[10],a[11],a[12],a[13],a[14],a[15],a[16],a[17],a[18],a[19],a[20],a[21],a[22],a[23],a[24],a[25],a[26],a[27],a[28],a[29],a[30],a[31]); else *ri=((W64I_1)fp)(d[0],a[1],a[2],a[3],a[4],a[5],a[6],a[7],a[8],a[9],a[10],a[11],a[12],a[13],a[14],a[15],a[16],a[17],a[18],a[19],a[20],a[21],a[22],a[23],a[24],a[25],a[26],a[27],a[28],a[29],a[30],a[31]); break;
    case 2: if(retfp) *rd=((W64D_2)fp)(a[0],d[1],a[2],a[3],a[4],a[5],a[6],a[7],a[8],a[9],a[10],a[11],a[12],a[13],a[14],a[15],a[16],a[17],a[18],a[19],a[20],a[21],a[22],a[23],a[24],a[25],a[26],a[27],a[28],a[29],a[30],a[31]); else *ri=((W64I_2)fp)(a[0],d[1],a[2],a[3],a[4],a[5],a[6],a[7],a[8],a[9],a[10],a[11],a[12],a[13],a[14],a[15],a[16],a[17],a[18],a[19],a[20],a[21],a[22],a[23],a[24],a[25],a[26],a[27],a[28],a[29],a[30],a[31]); break;
    case 3: if(retfp) *rd=((W64D_3)fp)(d[0],d[1],a[2],a[3],a[4],a[5],a[6],a[7],a[8],a[9],a[10],a[11],a[12],a[13],a[14],a[15],a[16],a[17],a[18],a[19],a[20],a[21],a[22],a[23],a[24],a[25],a[26],a[27],a[28],a[29],a[30],a[31]); else *ri=((W64I_3)fp)(d[0],d[1],a[2],a[3],a[4],a[5],a[6],a[7],a[8],a[9],a[10],a[11],a[12],a[13],a[14],a[15],a[16],a[17],a[18],a[19],a[20],a[21],a[22],a[23],a[24],a[25],a[26],a[27],a[28],a[29],a[30],a[31]); break;
    case 4: if(retfp) *rd=((W64D_4)fp)(a[0],a[1],d[2],a[3],a[4],a[5],a[6],a[7],a[8],a[9],a[10],a[11],a[12],a[13],a[14],a[15],a[16],a[17],a[18],a[19],a[20],a[21],a[22],a[23],a[24],a[25],a[26],a[27],a[28],a[29],a[30],a[31]); else *ri=((W64I_4)fp)(a[0],a[1],d[2],a[3],a[4],a[5],a[6],a[7],a[8],a[9],a[10],a[11],a[12],a[13],a[14],a[15],a[16],a[17],a[18],a[19],a[20],a[21],a[22],a[23],a[24],a[25],a[26],a[27],a[28],a[29],a[30],a[31]); break;
    case 5: if(retfp) *rd=((W64D_5)fp)(d[0],a[1],d[2],a[3],a[4],a[5],a[6],a[7],a[8],a[9],a[10],a[11],a[12],a[13],a[14],a[15],a[16],a[17],a[18],a[19],a[20],a[21],a[22],a[23],a[24],a[25],a[26],a[27],a[28],a[29],a[30],a[31]); else *ri=((W64I_5)fp)(d[0],a[1],d[2],a[3],a[4],a[5],a[6],a[7],a[8],a[9],a[10],a[11],a[12],a[13],a[14],a[15],a[16],a[17],a[18],a[19],a[20],a[21],a[22],a[23],a[24],a[25],a[26],a[27],a[28],a[29],a[30],a[31]); break;
    case 6: if(retfp) *rd=((W64D_6)fp)(a[0],d[1],d[2],a[3],a[4],a[5],a[6],a[7],a[8],a[9],a[10],a[11],a[12],a[13],a[14],a[15],a[16],a[17],a[18],a[19],a[20],a[21],a[22],a[23],a[24],a[25],a[26],a[27],a[28],a[29],a[30],a[31]); else *ri=((W64I_6)fp)(a[0],d[1],d[2],a[3],a[4],a[5],a[6],a[7],a[8],a[9],a[10],a[11],a[12],a[13],a[14],a[15],a[16],a[17],a[18],a[19],a[20],a[21],a[22],a[23],a[24],a[25],a[26],a[27],a[28],a[29],a[30],a[31]); break;
    case 7: if(retfp) *rd=((W64D_7)fp)(d[0],d[1],d[2],a[3],a[4],a[5],a[6],a[7],a[8],a[9],a[10],a[11],a[12],a[13],a[14],a[15],a[16],a[17],a[18],a[19],a[20],a[21],a[22],a[23],a[24],a[25],a[26],a[27],a[28],a[29],a[30],a[31]); else *ri=((W64I_7)fp)(d[0],d[1],d[2],a[3],a[4],a[5],a[6],a[7],a[8],a[9],a[10],a[11],a[12],a[13],a[14],a[15],a[16],a[17],a[18],a[19],a[20],a[21],a[22],a[23],a[24],a[25],a[26],a[27],a[28],a[29],a[30],a[31]); break;
    case 8: if(retfp) *rd=((W64D_8)fp)(a[0],a[1],a[2],d[3],a[4],a[5],a[6],a[7],a[8],a[9],a[10],a[11],a[12],a[13],a[14],a[15],a[16],a[17],a[18],a[19],a[20],a[21],a[22],a[23],a[24],a[25],a[26],a[27],a[28],a[29],a[30],a[31]); else *ri=((W64I_8)fp)(a[0],a[1],a[2],d[3],a[4],a[5],a[6],a[7],a[8],a[9],a[10],a[11],a[12],a[13],a[14],a[15],a[16],a[17],a[18],a[19],a[20],a[21],a[22],a[23],a[24],a[25],a[26],a[27],a[28],a[29],a[30],a[31]); break;
    case 9: if(retfp) *rd=((W64D_9)fp)(d[0],a[1],a[2],d[3],a[4],a[5],a[6],a[7],a[8],a[9],a[10],a[11],a[12],a[13],a[14],a[15],a[16],a[17],a[18],a[19],a[20],a[21],a[22],a[23],a[24],a[25],a[26],a[27],a[28],a[29],a[30],a[31]); else *ri=((W64I_9)fp)(d[0],a[1],a[2],d[3],a[4],a[5],a[6],a[7],a[8],a[9],a[10],a[11],a[12],a[13],a[14],a[15],a[16],a[17],a[18],a[19],a[20],a[21],a[22],a[23],a[24],a[25],a[26],a[27],a[28],a[29],a[30],a[31]); break;
    case 10: if(retfp) *rd=((W64D_10)fp)(a[0],d[1],a[2],d[3],a[4],a[5],a[6],a[7],a[8],a[9],a[10],a[11],a[12],a[13],a[14],a[15],a[16],a[17],a[18],a[19],a[20],a[21],a[22],a[23],a[24],a[25],a[26],a[27],a[28],a[29],a[30],a[31]); else *ri=((W64I_10)fp)(a[0],d[1],a[2],d[3],a[4],a[5],a[6],a[7],a[8],a[9],a[10],a[11],a[12],a[13],a[14],a[15],a[16],a[17],a[18],a[19],a[20],a[21],a[22],a[23],a[24],a[25],a[26],a[27],a[28],a[29],a[30],a[31]); break;
    case 11: if(retfp) *rd=((W64D_11)fp)(d[0],d[1],a[2],d[3],a[4],a[5],a[6],a[7],a[8],a[9],a[10],a[11],a[12],a[13],a[14],a[15],a[16],a[17],a[18],a[19],a[20],a[21],a[22],a[23],a[24],a[25],a[26],a[27],a[28],a[29],a[30],a[31]); else *ri=((W64I_11)fp)(d[0],d[1],a[2],d[3],a[4],a[5],a[6],a[7],a[8],a[9],a[10],a[11],a[12],a[13],a[14],a[15],a[16],a[17],a[18],a[19],a[20],a[21],a[22],a[23],a[24],a[25],a[26],a[27],a[28],a[29],a[30],a[31]); break;
    case 12: if(retfp) *rd=((W64D_12)fp)(a[0],a[1],d[2],d[3],a[4],a[5],a[6],a[7],a[8],a[9],a[10],a[11],a[12],a[13],a[14],a[15],a[16],a[17],a[18],a[19],a[20],a[21],a[22],a[23],a[24],a[25],a[26],a[27],a[28],a[29],a[30],a[31]); else *ri=((W64I_12)fp)(a[0],a[1],d[2],d[3],a[4],a[5],a[6],a[7],a[8],a[9],a[10],a[11],a[12],a[13],a[14],a[15],a[16],a[17],a[18],a[19],a[20],a[21],a[22],a[23],a[24],a[25],a[26],a[27],a[28],a[29],a[30],a[31]); break;
    case 13: if(retfp) *rd=((W64D_13)fp)(d[0],a[1],d[2],d[3],a[4],a[5],a[6],a[7],a[8],a[9],a[10],a[11],a[12],a[13],a[14],a[15],a[16],a[17],a[18],a[19],a[20],a[21],a[22],a[23],a[24],a[25],a[26],a[27],a[28],a[29],a[30],a[31]); else *ri=((W64I_13)fp)(d[0],a[1],d[2],d[3],a[4],a[5],a[6],a[7],a[8],a[9],a[10],a[11],a[12],a[13],a[14],a[15],a[16],a[17],a[18],a[19],a[20],a[21],a[22],a[23],a[24],a[25],a[26],a[27],a[28],a[29],a[30],a[31]); break;
    case 14: if(retfp) *rd=((W64D_14)fp)(a[0],d[1],d[2],d[3],a[4],a[5],a[6],a[7],a[8],a[9],a[10],a[11],a[12],a[13],a[14],a[15],a[16],a[17],a[18],a[19],a[20],a[21],a[22],a[23],a[24],a[25],a[26],a[27],a[28],a[29],a[30],a[31]); else *ri=((W64I_14)fp)(a[0],d[1],d[2],d[3],a[4],a[5],a[6],a[7],a[8],a[9],a[10],a[11],a[12],a[13],a[14],a[15],a[16],a[17],a[18],a[19],a[20],a[21],a[22],a[23],a[24],a[25],a[26],a[27],a[28],a[29],a[30],a[31]); break;
    case 15: if(retfp) *rd=((W64D_15)fp)(d[0],d[1],d[2],d[3],a[4],a[5],a[6],a[7],a[8],a[9],a[10],a[11],a[12],a[13],a[14],a[15],a[16],a[17],a[18],a[19],a[20],a[21],a[22],a[23],a[24],a[25],a[26],a[27],a[28],a[29],a[30],a[31]); else *ri=((W64I_15)fp)(d[0],d[1],d[2],d[3],a[4],a[5],a[6],a[7],a[8],a[9],a[10],a[11],a[12],a[13],a[14],a[15],a[16],a[17],a[18],a[19],a[20],a[21],a[22],a[23],a[24],a[25],a[26],a[27],a[28],a[29],a[30],a[31]); break;
    }
}
#else
typedef uint64_t (*FCallI)(uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,
                           uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,
                           uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,
                           uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,
                           double,double,double,double,double,double,double,double);
typedef double   (*FCallD)(uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,
                           uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,
                           uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,
                           uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,
                           double,double,double,double,double,double,double,double);
#define FA32(a) a[0],a[1],a[2],a[3],a[4],a[5],a[6],a[7],a[8],a[9],a[10],a[11],a[12],a[13],a[14],a[15],\
                a[16],a[17],a[18],a[19],a[20],a[21],a[22],a[23],a[24],a[25],a[26],a[27],a[28],a[29],a[30],a[31]
#define FD8(d)  d[0],d[1],d[2],d[3],d[4],d[5],d[6],d[7]
#endif
static double ffi_fbits(float f){   /* a double register whose low 32 bits hold f */
    uint32_t u; memcpy(&u,&f,4); uint64_t w=u; double d; memcpy(&d,&w,8); return d;
}
LFN(f_ffi_callf){
    void *fp=(void*)(uintptr_t)(uint64_t)self->up[0].u.n;
    FType *ft=&FT[(int)self->up[1].u.n];
    const char *nm=self->name?self->name:"?";
    if(ft->vararg && nargs>ft->nargs) luc_error("lucffi: %s: variadic C functions are not supported",nm);
    if(nargs>ft->nargs) luc_error("lucffi: %s: too many arguments (%d, expected %d)",nm,nargs,ft->nargs);
    uint64_t a[FFI_NI]; double d[FFI_NF]; memset(a,0,sizeof a); memset(d,0,sizeof d);
#if defined(_WIN32)
    int mask=0;
    if(ft->nargs>FFI_NI) luc_error("lucffi: %s: more than %d arguments",nm,FFI_NI);
    for(int i=0;i<ft->nargs;i++){
        int at=ft->args[i], k=FT[at].kind; Value v=AR(i);
        if(k==FK_F32||k==FK_F64){
            double x=ffi_tonum(v,nm);
            double r= k==FK_F32? ffi_fbits((float)x) : x;
            memcpy(&a[i],&r,8); if(i<4){ d[i]=r; mask|=1<<i; }
        } else if(k==FK_PTR) a[i]=(uint64_t)ffi_toptr(v,nm);
        else if(k==FK_STRUCT||k==FK_ARR) luc_error("lucffi: %s: structs by value are not supported",nm);
        else a[i]=(uint64_t)ffi_toint(v,k,nm);
    }
#else
    int ni=0, nf=0;
    for(int i=0;i<ft->nargs;i++){
        int at=ft->args[i], k=FT[at].kind; Value v=AR(i);
        if(k==FK_F32||k==FK_F64){
            if(nf>=FFI_NF) luc_error("lucffi: %s: more than %d floating-point arguments",nm,FFI_NF);
            double x=ffi_tonum(v,nm);
            d[nf++]= k==FK_F32? ffi_fbits((float)x) : x;
        } else {
            if(ni>=FFI_NI) luc_error("lucffi: %s: more than %d integer/pointer arguments",nm,FFI_NI);
            if(k==FK_PTR) a[ni++]=(uint64_t)ffi_toptr(v,nm);
            else if(k==FK_STRUCT||k==FK_ARR) luc_error("lucffi: %s: structs by value are not supported",nm);
            else a[ni++]=(uint64_t)ffi_toint(v,k,nm);
        }
    }
#if defined(__APPLE__) && defined(__aarch64__)
    if(ni>8) luc_error("lucffi: %s: more than 8 integer arguments is not supported on Apple arm64",nm);
#endif
#endif
    int rt=ft->base, rk=FT[rt].kind;
    if(rk==FK_STRUCT||rk==FK_ARR) luc_error("lucffi: %s: struct return values are not supported",nm);
    uint64_t ri=0; double rd=0;
    int retfp= rk==FK_F32||rk==FK_F64;
#if defined(_WIN32)
    ffi_call_w64(fp,mask,retfp,a,d,&ri,&rd);
#else
    if(retfp) rd=((FCallD)fp)(FA32(a),FD8(d)); else ri=((FCallI)fp)(FA32(a),FD8(d));
#endif
    Value r;
    switch(rk){
        case FK_VOID: return 0;
        case FK_I8:  r=mknum((int8_t)ri); break;
        case FK_U8:  r=mknum((uint8_t)ri); break;
        case FK_I16: r=mknum((int16_t)ri); break;
        case FK_U16: r=mknum((uint16_t)ri); break;
        case FK_I32: r=mknum((int32_t)(uint32_t)ri); break;
        case FK_U32: r=mknum((uint32_t)ri); break;
        case FK_I64: r=mknum((double)(int64_t)ri); break;
        case FK_U64: r=mknum((double)ri); break;
        case FK_BOOL: r=mkbool((ri&0xFF)!=0); break;
        case FK_F64: r=mknum(rd); break;
        case FK_F32: { uint64_t w; memcpy(&w,&rd,8); uint32_t u=(uint32_t)w; float f; memcpy(&f,&u,4); r=mknum(f); break; }
        case FK_PTR: r=ffi_ptrval(rt,(uintptr_t)ri); break;
        default: r=NIL;
    }
    RET(0,r); return 1;
}

/* ---------- libraries ---------- */
static void *ffi_dlopen(const char *path,int global,char *err,size_t ecap){
#if defined(_WIN32)
    (void)global;
    HMODULE h=LoadLibraryA(path);
    if(!h) snprintf(err,ecap,"%s: error %lu",path,(unsigned long)GetLastError());
    return (void*)h;
#else
    void *h=dlopen(path,RTLD_NOW|(global?RTLD_GLOBAL:RTLD_LOCAL));
    if(!h){ const char *e=dlerror(); snprintf(err,ecap,"%s",e?e:path); }
    return h;
#endif
}
static void *ffi_dlsym(void *h,const char *name){
#if defined(_WIN32)
    if(h) return (void*)GetProcAddress((HMODULE)h,name);
    static const char *dl[]={"msvcrt.dll","ucrtbase.dll","kernel32.dll",NULL};
    void *p=(void*)GetProcAddress(GetModuleHandleA(NULL),name);
    for(int i=0;!p&&dl[i];i++){ HMODULE m=GetModuleHandleA(dl[i]); if(!m) m=LoadLibraryA(dl[i]); if(m) p=(void*)GetProcAddress(m,name); }
    return p;
#else
    return dlsym(h?h:RTLD_DEFAULT,name);
#endif
}
static Value ffi_mklib(void *h){
    Table *t=tab_new(0);
    tab_set(t,hk(K_H),mknum((double)(uintptr_t)h));
    t->meta=F_LIBMT;
    return mkobj(LT_TABLE,t);
}
LFN(f_lib_index){ UNUSED_SELF;
    Value lib=AR(0), k=AR(1);
    if(lib.t!=LT_TABLE||k.t!=LT_STR) { RET(0,NIL); return 1; }
    const char *name=AS_STR(k)->s;
    void *h=(void*)(uintptr_t)(uint64_t)tab_get(AS_TAB(lib),hk(K_H)).u.n;
    int ty=-1;
    for(int i=nFS-1;i>=0;i--) if(strcmp(FS[i].name,name)==0){ ty=FS[i].type; break; }
    if(ty<0){
        for(int i=nFC-1;i>=0;i--) if(strcmp(FC[i].name,name)==0){ RET(0,mknum((double)FC[i].v)); return 1; }
        luc_error("lucffi: missing declaration for symbol '%s' (add it with lucffi.cdef)",name);
    }
    void *addr=ffi_dlsym(h,name);
    if(!addr) luc_error("lucffi: cannot resolve symbol '%s' in the loaded library",name);
    if(FT[ty].kind==FK_FUNC){
        CFunc *cf=cfunc_new(f_ffi_callf,FT[ty].name,2);
        cf->up[0]=mknum((double)(uintptr_t)addr); cf->up[1]=mknum((double)ty);
        Value f=mkobj(LT_CFUNC,cf);
        tab_set(AS_TAB(lib),k,f);           /* cache: next lookup is a plain hit */
        RET(0,f); return 1;
    }
    RET(0,ffi_read(ty,(unsigned char*)addr)); return 1;
}
static int ffi_has_ext(const char *s){
    const char *dot=strrchr(s,'.'), *sl1=strrchr(s,'/'), *sl2=strrchr(s,'\\');
    const char *sl= sl1>sl2? sl1:sl2;
    return dot && (!sl || dot>sl);
}
LFN(f_ffi_load){ UNUSED_SELF;
    Str *nm=checkstr(L,base,nargs,0,"lucffi.load");
    int global=truthy(AR(1));
    const char *n=nm->s; char err[1024]=""; char cand[8][600]; int nc=0;
    int haspath= strchr(n,'/')||strchr(n,'\\');
    snprintf(cand[nc++],600,"%s",n);
    if(!ffi_has_ext(n)){
#if defined(_WIN32)
        snprintf(cand[nc++],600,"%s.dll",n);
#elif defined(__APPLE__)
        snprintf(cand[nc++],600,"lib%s.dylib",n); snprintf(cand[nc++],600,"%s.dylib",n);
#else
        snprintf(cand[nc++],600,"lib%s.so",n); snprintf(cand[nc++],600,"%s.so",n);
        snprintf(cand[nc++],600,"lib%s.so.12",n); snprintf(cand[nc++],600,"lib%s.so.11",n);
#endif
    }
    /* search: system loader, then the folders LUC looks in for modules */
    char dirs[6][700]; int nd=0;
    dirs[nd++][0]=0;
    if(!haspath){
        const char *sd=luc_scriptdir();
        if(sd && *sd){ snprintf(dirs[nd++],700,"%s",sd); snprintf(dirs[nd++],700,"%s/luc_modules",sd); }
        const char *lp=getenv("LUC_PATH"); if(lp && *lp) snprintf(dirs[nd++],700,"%s",lp);
        snprintf(dirs[nd++],700,"."); snprintf(dirs[nd++],700,"./luc_modules");
    }
    for(int di=0;di<nd;di++) for(int ci=0;ci<nc;ci++){
        char path[1400];
        if(!dirs[di][0]) snprintf(path,sizeof path,"%s",cand[ci]);
        else snprintf(path,sizeof path,"%s/%s",dirs[di],cand[ci]);
        if(dirs[di][0] && !haspath){ FILE *fx=fopen(path,"rb"); if(!fx) continue; fclose(fx); }
        char e[512]=""; void *h=ffi_dlopen(path,global,e,sizeof e);
        if(h){ RET(0,ffi_mklib(h)); return 1; }
        if(!err[0]) snprintf(err,sizeof err,"%s",e);
    }
    luc_error("lucffi.load: cannot load '%s' (%s)",n,err);
    return 0;
}

/* ---------- ctype objects ---------- */
static int ffi_ctarg(Value v,int *vla,const char *fn){
    *vla=0;
    if(v.t==LT_STR) return ffi_parse_type(AS_STR(v)->s,vla);
    if(v.t==LT_TABLE){
        Table *t=AS_TAB(v); Value c=tab_get(t,hk(K_C));
        if(c.t==LT_NUM){ int ty=(int)c.u.n; *vla= FT[ty].kind==FK_ARR && FT[ty].count<0; return ty; }
        int ty; if(ffi_cdata(v,&ty,NULL)) return ty;
    }
    luc_error("lucffi.%s: expected a C type (string or ctype), got %s",fn,type_name(v));
    return -1;
}
static Value ffi_mkctype(int ty){
    Table *t=tab_new(0);
    tab_set(t,hk(K_C),mknum((double)ty));
    t->meta=F_CTMT;
    return mkobj(LT_TABLE,t);
}
/* fill one element from an initializer */
static void ffi_init_from(int ty,unsigned char *p,Value v);
static void ffi_init_struct(int st,unsigned char *p,Value *iv,int ni){
    FType *t=&FT[st];
    if(ni==1 && (iv[0].t==LT_TABLE||iv[0].t==LT_LIST) && !ffi_cdata(iv[0],NULL,NULL)){
        Table *src=AS_TAB(iv[0]);
        for(int i=0;i<t->nf;i++){
            Value v=tab_get(src,cstrv(t->f[i].name));
            if(v.t==LT_NIL) v=tab_get(src,mknum(iv[0].t==LT_LIST? i : i+1));
            if(v.t!=LT_NIL) ffi_init_from(t->f[i].type,p+t->f[i].off,v);
        }
        return;
    }
    for(int i=0;i<ni && i<t->nf;i++) ffi_init_from(t->f[i].type,p+t->f[i].off,iv[i]);
}
static void ffi_init_from(int ty,unsigned char *p,Value v){
    if(FT[ty].kind==FK_STRUCT && !ffi_cdata(v,NULL,NULL)){ ffi_init_struct(ty,p,&v,1); return; }
    ffi_write(ty,p,v,"initializer");
}
static Value ffi_new(int ty,int vla,Value *args,int nargs,const char *fn){
    long n=-1; int ai=0;
    if(vla){
        if(nargs<1||args[0].t!=LT_NUM) luc_error("lucffi.%s: '%s' needs an element count",fn,FT[ty].name);
        n=(long)args[0].u.n; ai=1;
        if(n<0) luc_error("lucffi.%s: negative element count",fn);
    }
    size_t size;
    int e= FT[ty].kind==FK_ARR? FT[ty].base : -1;
    if(vla) size=FT[e].size*(size_t)n;
    else {
        if(FT[ty].kind==FK_STRUCT && !FT[ty].complete) luc_error("lucffi.%s: '%s' is an incomplete type",fn,FT[ty].name);
        if(FT[ty].kind==FK_VOID||FT[ty].kind==FK_FUNC) luc_error("lucffi.%s: cannot allocate '%s'",fn,FT[ty].name);
        size=FT[ty].size;
    }
    if(size>(size_t)INT_MAX-16) luc_error("lucffi.%s: allocation of %.0f bytes is too large",fn,(double)size);
    Buffer *b=buf_new((int)(size?size:1));
    Value owner=mkobj(LT_BUFFER,b);
    Value cd=ffi_mkcd(ty,(uintptr_t)b->b,owner,vla?n:-1);
    Value *iv=args+ai; int ni=nargs-ai;
    if(ni>0){
        if(FT[ty].kind==FK_ARR){
            long cnt= vla? n : FT[ty].count;
            if(ni==1 && (iv[0].t==LT_TABLE||iv[0].t==LT_LIST) && !ffi_cdata(iv[0],NULL,NULL)){
                Table *src=AS_TAB(iv[0]); int islist=iv[0].t==LT_LIST;
                for(long i=0;i<cnt;i++){
                    Value v= islist? (i<src->alen? src->arr[i] : NIL) : tab_get(src,mknum((double)(i+1)));
                    if(v.t==LT_NIL) break;
                    ffi_init_from(e,b->b+(size_t)i*FT[e].size,v);
                }
            } else if(ni==1 && iv[0].t==LT_STR && FT[e].size==1){
                size_t m=(size_t)AS_STR(iv[0])->len; if(m>(size_t)cnt) m=(size_t)cnt;
                memcpy(b->b,AS_STR(iv[0])->s,m);
            } else if(ni==1){
                for(long i=0;i<cnt;i++) ffi_init_from(e,b->b+(size_t)i*FT[e].size,iv[0]);
            } else {
                for(long i=0;i<cnt && i<ni;i++) ffi_init_from(e,b->b+(size_t)i*FT[e].size,iv[i]);
            }
        } else if(FT[ty].kind==FK_STRUCT) ffi_init_struct(ty,b->b,iv,ni);
        else ffi_init_from(ty,b->b,iv[0]);
    }
    return cd;
}
LFN(f_ffi_new){ UNUSED_SELF;
    int vla; int ty=ffi_ctarg(AR(0),&vla,"new");
    Value args[16]; int na=nargs-1; if(na>16) na=16; if(na<0) na=0;
    for(int i=0;i<na;i++) args[i]=AR(i+1);
    RET(0,ffi_new(ty,vla,args,na,"new")); return 1;
}
LFN(f_ctype_call){ UNUSED_SELF;
    int vla; int ty=ffi_ctarg(AR(0),&vla,"ctype()");
    Value args[16]; int na=nargs-1; if(na>16) na=16; if(na<0) na=0;
    for(int i=0;i<na;i++) args[i]=AR(i+1);
    RET(0,ffi_new(ty,vla,args,na,"ctype()")); return 1;
}
LFN(f_ctype_tostring){ UNUSED_SELF;
    int vla; int ty=ffi_ctarg(AR(0),&vla,"tostring"); char b[200];
    snprintf(b,sizeof b,"ctype<%s>",FT[ty].name); RET(0,cstrv(b)); return 1;
}
LFN(f_ffi_typeof){ UNUSED_SELF;
    int vla; int ty=ffi_ctarg(AR(0),&vla,"typeof");
    RET(0,ffi_mkctype(ty)); return 1;
}
LFN(f_ffi_cast){ UNUSED_SELF;
    int vla; int ty=ffi_ctarg(AR(0),&vla,"cast"); Value v=AR(1);
    int k=FT[ty].kind;
    if(k==FK_PTR){ RET(0,ffi_ptrval(ty,ffi_toptr(v,"cast"))); return 1; }
    if(k==FK_F32){ RET(0,mknum((float)ffi_tonum(v,"cast"))); return 1; }
    if(k==FK_F64){ RET(0,mknum(ffi_tonum(v,"cast"))); return 1; }
    if(k>=FK_I8 && k<=FK_U64){
        int64_t x=ffi_toint(v,k,"cast"); unsigned char tmp[8]; Value r;
        ffi_write(ty,tmp,mknum((double)x),"cast"); r=ffi_read(ty,tmp);
        if(k==FK_I64||k==FK_U64) r=mknum(k==FK_U64? (double)(uint64_t)x : (double)x);
        RET(0,r); return 1;
    }
    if(k==FK_BOOL){ RET(0,mkbool(truthy(v))); return 1; }
    luc_error("lucffi.cast: cannot cast to '%s'",FT[ty].name);
    return 0;
}
LFN(f_ffi_sizeof){ UNUSED_SELF;
    Value v=AR(0); int ty; uintptr_t a;
    if(ffi_cdata(v,&ty,&a)){
        long n=ffi_cd_count(AS_TAB(v),ty);
        if(FT[ty].kind==FK_ARR && FT[ty].count<0){ RET(0,mknum((double)(FT[FT[ty].base].size*(size_t)n))); return 1; }
        RET(0,mknum((double)FT[ty].size)); return 1;
    }
    int vla; ty=ffi_ctarg(v,&vla,"sizeof");
    if(vla){
        if(AR(1).t!=LT_NUM){ RET(0,NIL); return 1; }
        RET(0,mknum((double)(FT[FT[ty].base].size*(size_t)AR(1).u.n))); return 1;
    }
    if(FT[ty].kind==FK_STRUCT && !FT[ty].complete){ RET(0,NIL); return 1; }
    RET(0,mknum((double)FT[ty].size)); return 1;
}
LFN(f_ffi_offsetof){ UNUSED_SELF;
    int vla; int ty=ffi_ctarg(AR(0),&vla,"offsetof");
    Str *f=checkstr(L,base,nargs,1,"lucffi.offsetof");
    if(FT[ty].kind!=FK_STRUCT){ RET(0,NIL); return 1; }
    FField *ff=ffi_field(ty,f->s);
    RET(0,ff? mknum((double)ff->off) : NIL); return 1;
}
LFN(f_ffi_alignof){ UNUSED_SELF;
    int vla; int ty=ffi_ctarg(AR(0),&vla,"alignof");
    RET(0,mknum((double)FT[ty].align)); return 1;
}
LFN(f_ffi_cdef){ UNUSED_SELF;
    Str *s=checkstr(L,base,nargs,0,"lucffi.cdef");
    ffi_cdef_parse(s->s,s->len);
    return 0;
}
LFN(f_ffi_metatype){ UNUSED_SELF;
    int vla; int ty=ffi_ctarg(AR(0),&vla,"metatype");
    Table *mt=checktab(L,base,nargs,1,"lucffi.metatype");
    if(FT[ty].kind!=FK_STRUCT) luc_error("lucffi.metatype: '%s' is not a struct type",FT[ty].name);
    FT[ty].mt=freg_put(mkobj(LT_TABLE,mt));
    /* rebuild cached cdata metatables of the struct and of pointers to it */
    for(int i=0;i<nFT;i++) if(i==ty || (FT[i].kind==FK_PTR && FT[i].base==ty)) FT[i].cmt=0;
    RET(0,ffi_mkctype(ty)); return 1;
}
LFN(f_ffi_istype){ UNUSED_SELF;
    int vla; int ty=ffi_ctarg(AR(0),&vla,"istype"); int oty;
    if(!ffi_cdata(AR(1),&oty,NULL)){ RET(0,mkbool(0)); return 1; }
    int same= oty==ty || (FT[oty].kind==FK_PTR && FT[oty].base==ty) || (FT[ty].kind==FK_PTR && FT[ty].base==oty);
    RET(0,mkbool(same)); return 1;
}
LFN(f_ffi_iscdata){ UNUSED_SELF; RET(0,mkbool(ffi_cdata(AR(0),NULL,NULL))); return 1; }
LFN(f_ffi_addressof){ UNUSED_SELF; RET(0,mknum((double)ffi_toptr(AR(0),"addressof"))); return 1; }
LFN(f_ffi_typename){ UNUSED_SELF;
    int ty; if(!ffi_cdata(AR(0),&ty,NULL)){ RET(0,NIL); return 1; }
    RET(0,cstrv(FT[ty].name)); return 1;
}
LFN(f_ffi_string){ UNUSED_SELF;
    uintptr_t p=ffi_toptr(AR(0),"string");
    if(!p) luc_error("lucffi.string: NULL pointer");
    if(nargs>=2 && AR(1).t==LT_NUM){
        long n=(long)AR(1).u.n; if(n<0) n=0;
        RET(0,strv((const char*)p,(int)n)); return 1;
    }
    RET(0,cstrv((const char*)p)); return 1;
}
LFN(f_ffi_copy){ UNUSED_SELF;
    unsigned char *dst=(unsigned char*)ffi_toptr(AR(0),"copy");
    Value src=AR(1);
    size_t n=0;
    if(nargs>=3) n=(size_t)checknum(L,base,nargs,2,"lucffi.copy");
    else if(src.t==LT_STR) n=(size_t)AS_STR(src)->len+1;
    else luc_error("lucffi.copy: length required");
    const unsigned char *s=(const unsigned char*)ffi_toptr(src,"copy");
    if(n && (!dst||!s)) luc_error("lucffi.copy: NULL pointer");
    memmove(dst,s,n);
    return 0;
}
LFN(f_ffi_fill){ UNUSED_SELF;
    unsigned char *dst=(unsigned char*)ffi_toptr(AR(0),"fill");
    size_t n=(size_t)checknum(L,base,nargs,1,"lucffi.fill");
    int c= nargs>=3? (int)checknum(L,base,nargs,2,"lucffi.fill") : 0;
    if(n && !dst) luc_error("lucffi.fill: NULL pointer");
    memset(dst,c,n);
    return 0;
}
LFN(f_ffi_gc){ UNUSED_SELF; RET(0,AR(0)); return 1; }   /* LUC has no finalizers: free explicitly */
/* bulk helpers: elements of an array/pointer cdata <-> 1-based LUC table (or a LUC list) */
static void ffi_bulk_target(Value cd,int *ety,unsigned char **p,long *cap,const char *fn){
    int ty; uintptr_t a;
    if(!ffi_cdata(cd,&ty,&a)) luc_error("lucffi.%s: expected an array cdata",fn);
    int k=FT[ty].kind;
    if(k==FK_ARR){ *ety=FT[ty].base; *cap=ffi_cd_count(AS_TAB(cd),ty); }
    else if(k==FK_PTR){ *ety=FT[ty].base; *cap=LONG_MAX; }
    else luc_error("lucffi.%s: expected an array or pointer cdata",fn);
    int ek=FT[*ety].kind;
    if(!(ek>=FK_I8 && ek<=FK_BOOL)) luc_error("lucffi.%s: element type '%s' is not a number type",fn,FT[*ety].name);
    *p=(unsigned char*)a;
}
LFN(f_ffi_totable){ UNUSED_SELF;   /* totable(cd, n [, off [, add]]) -> {cd[off]+add .. cd[off+n-1]+add} */
    int e; unsigned char *p; long cap;
    ffi_bulk_target(AR(0),&e,&p,&cap,"totable");
    long n=(long)checknum(L,base,nargs,1,"lucffi.totable");
    long off= nargs>=3 && AR(2).t==LT_NUM? (long)AR(2).u.n : 0;
    double add= nargs>=4 && AR(3).t==LT_NUM? AR(3).u.n : 0;
    if(n<0||off<0||off+n>cap) luc_error("lucffi.totable: range %ld..%ld out of bounds",off,off+n-1);
    Table *t=tab_new(0);
    size_t es=FT[e].size;
    for(long i=0;i<n;i++){ Value v=ffi_read(e,p+(size_t)(off+i)*es); if(add!=0) v=mknum(v.u.n+add); tab_set(t,mknum((double)(i+1)),v); }
    RET(0,mkobj(LT_TABLE,t)); return 1;
}
LFN(f_ffi_fromtable){ UNUSED_SELF;   /* fromtable(cd, tbl, n [, off [, add [, first]]]) */
    int e; unsigned char *p; long cap;
    ffi_bulk_target(AR(0),&e,&p,&cap,"fromtable");
    Value src=AR(1);
    if(src.t!=LT_TABLE&&src.t!=LT_LIST) luc_error("lucffi.fromtable: expected a table or list");
    Table *t=AS_TAB(src); int islist=src.t==LT_LIST;
    long n= nargs>=3 && AR(2).t==LT_NUM? (long)AR(2).u.n : (islist? t->alen : tab_len(t));
    long off= nargs>=4 && AR(3).t==LT_NUM? (long)AR(3).u.n : 0;
    double add= nargs>=5 && AR(4).t==LT_NUM? AR(4).u.n : 0;
    long first= nargs>=6 && AR(5).t==LT_NUM? (long)AR(5).u.n : 1;
    if(n<0||off<0||off+n>cap) luc_error("lucffi.fromtable: range %ld..%ld out of bounds",off,off+n-1);
    size_t es=FT[e].size;
    for(long i=0;i<n;i++){
        Value v;
        if(islist){ long j=i+first-1; v= j>=0 && j<t->alen? t->arr[j] : NIL; }
        else { long j=i+first; v= j>=1 && j<=t->alen? t->arr[j-1] : tab_get(t,mknum((double)j)); }
        if(v.t!=LT_NUM && v.t!=LT_BOOL) luc_error("lucffi.fromtable: element %ld is not a number (got %s)",i+first,type_name(v));
        if(add!=0) v=mknum(v.u.n+add);
        ffi_write(e,p+(size_t)(off+i)*es,v,"fromtable");
    }
    return 0;
}

static Value lucL_ffi_module(void){
    if(sizeof(void*)!=8) luc_error("lucffi: only 64-bit builds are supported");
    if(!FREG){
        FREG=tab_new(1);
        tab_set(V.loaded,cstrv("\1lucffi.registry"),mkobj(LT_LIST,FREG));
        K_A=fkey("\1a"); K_T=fkey("\1t"); K_O=fkey("\1o"); K_N=fkey("\1n"); K_C=fkey("\1c");
        K_CD=fkey("\1cd"); K_H=fkey("\1h"); K_IDX=fkey("__index"); K_NIDX=fkey("__newindex"); K_TOS=fkey("__tostring");
        FCF_INDEX=mkobj(LT_CFUNC,cfunc_new(f_cd_index,"cdata.__index",0));       list_push(FREG,FCF_INDEX);
        FCF_NEWINDEX=mkobj(LT_CFUNC,cfunc_new(f_cd_newindex,"cdata.__newindex",0)); list_push(FREG,FCF_NEWINDEX);
        FCF_TOS=mkobj(LT_CFUNC,cfunc_new(f_cd_tostring,"cdata.__tostring",0));   list_push(FREG,FCF_TOS);
        FCF_LIBINDEX=mkobj(LT_CFUNC,cfunc_new(f_lib_index,"clib.__index",0));    list_push(FREG,FCF_LIBINDEX);
        FCF_CTCALL=mkobj(LT_CFUNC,cfunc_new(f_ctype_call,"ctype.__call",0));     list_push(FREG,FCF_CTCALL);
        FCF_CTTOS=mkobj(LT_CFUNC,cfunc_new(f_ctype_tostring,"ctype.__tostring",0)); list_push(FREG,FCF_CTTOS);
        F_LIBMT=tab_new(0); list_push(FREG,mkobj(LT_TABLE,F_LIBMT));
        tab_set(F_LIBMT,hk(K_IDX),FCF_LIBINDEX);
        F_CTMT=tab_new(0); list_push(FREG,mkobj(LT_TABLE,F_CTMT));
        tab_set(F_CTMT,cstrv("__call"),FCF_CTCALL); tab_set(F_CTMT,hk(K_TOS),FCF_CTTOS);
        ffi_types_init();
    }
    Table *m=tab_new(0);
    reg(m,"cdef",f_ffi_cdef);       reg(m,"load",f_ffi_load);       reg(m,"new",f_ffi_new);
    reg(m,"typeof",f_ffi_typeof);   reg(m,"cast",f_ffi_cast);       reg(m,"sizeof",f_ffi_sizeof);
    reg(m,"offsetof",f_ffi_offsetof); reg(m,"alignof",f_ffi_alignof); reg(m,"metatype",f_ffi_metatype);
    reg(m,"istype",f_ffi_istype);   reg(m,"iscdata",f_ffi_iscdata); reg(m,"addressof",f_ffi_addressof);
    reg(m,"typename",f_ffi_typename); reg(m,"string",f_ffi_string); reg(m,"copy",f_ffi_copy);
    reg(m,"fill",f_ffi_fill);       reg(m,"gc",f_ffi_gc);           reg(m,"totable",f_ffi_totable);
    reg(m,"fromtable",f_ffi_fromtable);
    tab_set(m,cstrv("C"),ffi_mklib(NULL));
#if defined(_WIN32)
    tab_set(m,cstrv("os"),cstrv("Windows"));
#elif defined(__APPLE__)
    tab_set(m,cstrv("os"),cstrv("OSX"));
#else
    tab_set(m,cstrv("os"),cstrv("Linux"));
#endif
#if defined(__aarch64__) || defined(_M_ARM64)
    tab_set(m,cstrv("arch"),cstrv("arm64"));
#else
    tab_set(m,cstrv("arch"),cstrv("x64"));
#endif
    tab_set(m,cstrv("version"),mknum(1));
    return mkobj(LT_TABLE,m);
}
