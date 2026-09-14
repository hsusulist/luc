/* lua2luc.c - mechanical Lua -> LUC source converter (lanternl porting aid)
 *
 * usage: lua2luc <in.lua> <out.luc>
 *
 * syntax-only transforms.  Lua array tables are KEPT 1-based by emitting
 * LUC dicts with explicit numeric keys, so index arithmetic is preserved:
 *   local x                 -> create x        (local function -> create function)
 *   ~=                      -> !=
 *   #expr                   -> len(expr)
 *   {k = v, [e] = v}        -> { "k": v, (e): v }
 *   {a, b, c}               -> { 1: a, 2: b, 3: c }
 *   for i = a, b [,s] do    -> do create __a/__b/__s/__n + repeat 1, __n as __j
 *   for k [,v] in pairs(t)  -> repeat k [, v] in (t) do
 *   for v in ipairs(t)      -> repeat __k, v in (t) do
 *   for i, v in ipairs(t)   -> do .. create i/v = __at(__L, __i) .. (injects __at)
 *   for x[,y] in gmatch     -> do create __g .. while true do .. (extra end)
 *   repeat <b> until c      -> while true do <b> if (c) then break end end
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <stdarg.h>

static char *SRC; static int SLEN;
static char *OUT; static int OLEN, OCAP;
static int   W_LINE;
static int   USED_IPAIRS2;
static int   WKEPT;                 /* count of left-as-is warnings */

static void die(const char *m){ fprintf(stderr,"lua2luc: %s\n",m); exit(1); }
static void oput(const char *s,int n){
    if(OLEN+n+1>OCAP){ OCAP=(OCAP+n+1)*2; OUT=(char*)realloc(OUT,(size_t)OCAP); if(!OUT) die("oom"); }
    memcpy(OUT+OLEN,s,(size_t)n); OLEN+=n; OUT[OLEN]=0;
}
static void oputs(const char *s){ oput(s,(int)strlen(s)); }
static void ofmt(const char *fmt,...){
    char b[2048]; va_list ap;
    va_start(ap,fmt); vsnprintf(b,sizeof b,fmt,ap); va_end(ap);
    oputs(b);
}

static int isw(int c){ return isalnum(c)||c=='_'; }
static char sc(int i){ return (i>=0&&i<SLEN)?SRC[i]:'\0'; }

/* long bracket level at i ([=*[) or -1 */
static int long_level(int i){
    if(sc(i)!='[') return -1;
    int j=i+1,lvl=0;
    while(sc(j)=='='){ lvl++; j++; }
    if(sc(j)=='[') return lvl;
    return -1;
}
static int scan_long(int i,int lvl){
    i+=2+lvl;
    while(i<SLEN){
        if(SRC[i]==']'){
            int j=i+1,n=0;
            while(sc(j)=='='){ n++; j++; }
            if(n==lvl&&sc(j)==']') return j+1;
        }
        if(SRC[i]=='\n') W_LINE++;
        i++;
    }
    die("unterminated long string");
    return SLEN;
}
static int scan_quote(int i){
    char q=SRC[i]; i++;
    while(i<SLEN){
        if(SRC[i]=='\\'){ i+=2; continue; }
        if(SRC[i]=='\n') W_LINE++;
        if(SRC[i]==q) return i+1;
        i++;
    }
    die("unterminated string");
    return SLEN;
}
static int scan_comment(int i){
    i+=2;
    int lvl=long_level(i);
    if(lvl>=0) return scan_long(i,lvl);
    while(i<SLEN&&SRC[i]!='\n') i++;
    return i<SLEN? i+1 : SLEN;
}
static int readword(int i,int *ws){
    *ws=i; i++;
    while(i<SLEN&&isw((unsigned char)SRC[i])) i++;
    return i;
}
static int skipws(int i){
    for(;;){
        while(i<SLEN&&(SRC[i]==' '||SRC[i]=='\t'||SRC[i]=='\r'||SRC[i]=='\n')){ if(SRC[i]=='\n') W_LINE++; i++; }
        if(i+1<SLEN&&SRC[i]=='-'&&SRC[i+1]=='-'){ int j=scan_comment(i); i=j; continue; }
        return i;
    }
}
static int peekword(int i,int *ws,int *we){
    i=skipws(i); if(i>=SLEN||!isw((unsigned char)SRC[i])) return 0;
    *ws=i; *we=readword(i,ws); return 1;
}
static int wse(int ws,int we,const char *w){
    int n=(int)strlen(w);
    return we-ws==n&&strncmp(SRC+ws,w,(size_t)n)==0;
}

/* block scan: from i (after an opener) find matching 'end'(want=0) or 'until'(want=1)
   word start index, or -1. */
static int match_block(int i,int b,int want){
    int depth=0;
    while(i<b){
        int c=(unsigned char)SRC[i];
        if(c=='"'||c=='\''){ i=scan_quote(i); continue; }
        if(c=='['){ int l=long_level(i); if(l>=0){ i=scan_long(i,l); continue; } }
        if(c=='-'&&i+1<SLEN&&SRC[i+1]=='-'){ i=scan_comment(i); continue; }
        if(isw(c)){
            int ws,we; we=readword(i,&ws);
            if(wse(ws,we,"function")||wse(ws,we,"if")||wse(ws,we,"do")) depth++;
            else if(wse(ws,we,"while")||wse(ws,we,"for")){
                depth++;                            /* the block itself */
                int d=0,j=we;
                while(j<b){                          /* skip header to its 'do' */
                    int cc=(unsigned char)SRC[j];
                    if(cc=='"'||cc=='\''){ j=scan_quote(j); continue; }
                    if(cc=='['){ int l=long_level(j); if(l>=0){ j=scan_long(j,l); continue; } }
                    if(cc=='-'&&j+1<b&&SRC[j+1]=='-'){ j=scan_comment(j); continue; }
                    if(cc=='('||cc=='['||cc=='{'){ d++; j++; continue; }
                    if(cc==')'||cc==']'||cc=='}'){ d--; j++; continue; }
                    if(d==0&&isw(cc)){
                        int w2s,w2e; w2e=readword(j,&w2s);
                        if(wse(w2s,w2e,"do")){ j=w2e; break; }
                        j=w2e; continue;
                    }
                    j++;
                }
                i=j; continue;
            }
            else if(wse(ws,we,"repeat")){
                int u=match_block(we,b,1);
                if(u<0) return -1;
                int k=u; while(k<b&&SRC[k]!='\n') k++;   /* skip until <cond> line */
                i=k+1<=b?k+1:b; continue;
            }
            else if(wse(ws,we,"end")){
                if(depth==0){ if(want) return -1; return ws; }
                depth--;
            }
            else if(wse(ws,we,"until")){
                if(depth==0){ if(want) return ws; return -1; }
            }
            i=we; continue;
        }
        i++;
    }
    return -1;
}

static struct Pend { int pos; } pend[512];
static int npend=0;
static void pend_add(int pos){ if(npend<512) pend[npend++].pos=pos; else die("too many loops"); }
static int pend_take(int pos){
    for(int k=0;k<npend;k++) if(pend[k].pos==pos){
        for(int j=k;j<npend-1;j++) pend[j]=pend[j+1];
        npend--; return 1;
    }
    return 0;
}

static void tr(int a,int b);
static int len_transform(int i,int b);
static int table_transform(int i,int b);
static int for_transform(int i,int b,int we);
static int repeat_transform(int i,int b,int we);

static void tr(int a,int b){
    int i=a;
    while(i<b){
        int c=(unsigned char)SRC[i];
        if(c=='"'||c=='\''){ int j=scan_quote(i); oput(SRC+i,j-i); i=j; continue; }
        if(c=='['){ int l=long_level(i); if(l>=0){ int j=scan_long(i,l); oput(SRC+i,j-i); i=j; continue; } }
        if(c=='-'&&i+1<b&&SRC[i+1]=='-'){
            int j=scan_comment(i); if(j>b) j=b;
            oput(SRC+i,j-i); i=j; continue;
        }
        if(c=='~'&&i+1<b&&SRC[i+1]=='='){ oputs("!="); i+=2; continue; }
        if(c=='#'){ i=len_transform(i,b); continue; }
        if(c=='{'){ i=table_transform(i,b); continue; }
        if(isw(c)){
            int ws,we; we=readword(i,&ws);
            if(wse(ws,we,"end")&&pend_take(i)){ oputs("end\nend"); i=we; continue; }
            if(wse(ws,we,"local")){
                int w2s,w2e;
                if(peekword(we,&w2s,&w2e)&&wse(w2s,w2e,"function")){ oputs("create function"); i=w2e; }
                else { oputs("create"); i=we; }
                continue;
            }
            if(wse(ws,we,"for")){ i=for_transform(i,b,we); continue; }
            if(wse(ws,we,"repeat")){ i=repeat_transform(i,b,we); continue; }
            if(wse(ws,we,"table")){
                int dot=skipws(we),w2s,w2e;
                if(dot<SLEN&&SRC[dot]=='.'){
                    if(peekword(dot+1,&w2s,&w2e)&&wse(w2s,w2e,"unpack")){ oputs("unpack"); i=w2e; continue; }
                }
                oputs("table"); i=we; continue;
            }
            oput(SRC+ws,we-ws); i=we; continue;
        }
        if(c=='\n') W_LINE++;
        oput(SRC+i,1); i++;
    }
}

/* ---------- #expr -> len(expr) ---------- */
static int balanced(int i,int b,char open,char close){
    int d=0,k=i;
    while(k<b){
        int c=(unsigned char)SRC[k];
        if(c=='"'||c=='\''){ k=scan_quote(k); continue; }
        if(c=='['){ int l=long_level(k); if(l>=0){ k=scan_long(k,l); continue; } }
        if(c=='-'&&k+1<b&&SRC[k+1]=='-'){ k=scan_comment(k); continue; }
        if(c==open){ d++; k++; continue; }
        if(c==close){ d--; k++; if(d==0) return k; continue; }
        k++;
    }
    return -1;
}
static int len_transform(int i,int b){
    int j=skipws(i+1);
    if(j>=b){ oputs("#"); return i+1; }
    int c=(unsigned char)SRC[j];
    if(c=='('){
        int k=balanced(j,b,'(',')');
        if(k<0){ oputs("#"); return i+1; }
        oputs("len("); tr(j+1,k-1); oputs(")");
        return k;
    }
    if(c=='"'||c=='\''){ int e=scan_quote(j); oputs("len("); oput(SRC+j,e-j); oputs(")"); return e; }
    if(c=='['){ int l=long_level(j); if(l>=0){ int e=scan_long(j,l); oputs("len("); oput(SRC+j,e-j); oputs(")"); return e; } }
    if(isw(c)){
        int ws,we; we=readword(j,&ws);
        int k=we;
        for(;;){
            int p=skipws(k);
            if(p>=b) break;
            char pc=SRC[p];
            if(pc=='.'){
                int w2s,w2e;
                if(peekword(p+1,&w2s,&w2e)){ k=w2e; continue; }
                break;
            }
            if(pc=='['){
                int m=balanced(p,b,'[',']');
                if(m<0) break;
                k=m; continue;
            }
            if(pc=='('){
                int m=balanced(p,b,'(',')');
                if(m<0) break;
                k=m; continue;
            }
            if(pc==':'){                     /* method call in operand: #s:sub(2) */
                int w2s,w2e;
                if(!peekword(p+1,&w2s,&w2e)) break;
                k=w2e;
                int q=skipws(k);
                if(q<b&&SRC[q]=='('){
                    int m=balanced(q,b,'(',')');
                    if(m<0) break;
                    k=m;
                }
                continue;
            }
            break;
        }
        oputs("len("); tr(j,k); oputs(")");
        return k;
    }
    oputs("#"); return i+1;
}

/* ---------- table constructor ---------- */
typedef struct { int s,e,kind; } Item;
#define CTOR_MAX 32
static Item items[CTOR_MAX][512];      /* per recursion depth (nested ctors) */
static int ctor_depth=0;

static int match_brace(int i,int b){
    int d=0,k=i;
    while(k<b){
        int c=(unsigned char)SRC[k];
        if(c=='"'||c=='\''){ k=scan_quote(k); continue; }
        if(c=='['){ int l=long_level(k); if(l>=0){ k=scan_long(k,l); continue; } }
        if(c=='-'&&k+1<b&&SRC[k+1]=='-'){ k=scan_comment(k); continue; }
        if(c=='{'){ d++; k++; continue; }
        if(c=='}'){ d--; k++; if(d==0) return k-1; continue; }
        k++;
    }
    die("unbalanced braces in table constructor");
    return b;
}
/* classify item: 0 plain, 1 name=, 2 [e]= */
static int classify(int s,int e,int *ks,int *ke,int *vs){
    int p=skipws(s);
    if(p>=e) return -1;
    if(SRC[p]=='['){
        int k=balanced(p,e,'[',']');
        if(k>0){
            int q=skipws(k);
            if(q<e&&SRC[q]=='='&&sc(q+1)!='='){ *ks=p; *ke=k-1; *vs=q+1; return 2; }
        }
        return 0;
    }
    if(isw((unsigned char)SRC[p])){
        int ws,we; we=readword(p,&ws);
        int q=skipws(we);
        if(q<e&&SRC[q]=='='&&sc(q+1)!='='){ *ks=ws; *ke=we; *vs=q+1; return 1; }
    }
    return 0;
}
/* skip a function..end (atomic item) starting at 'function' word end we */
static int skip_function(int we,int b){
    int p=skipws(we);
    if(p<b&&SRC[p]=='('){
        int e=balanced(p,b,'(',')');
        if(e<0) die("bad function params");
        int lend=match_block(e,b,0);
        if(lend<0) die("function end not found");
        int ws; we=readword(lend,&ws);
        return we;
    }
    return we;
}
static int table_transform(int i,int b){
    if(ctor_depth>=CTOR_MAX) die("table constructors nested too deep");
    Item *it=items[ctor_depth++];
    int close=match_brace(i,b);
    int inner_s=i+1, inner_e=close;
    int n=0,d=0,k=inner_s,istart=inner_s;
    while(k<inner_e){
        int c=(unsigned char)SRC[k];
        if(c=='"'||c=='\''){ k=scan_quote(k); continue; }
        if(c=='['){ int l=long_level(k); if(l>=0){ k=scan_long(k,l); continue; } }
        if(c=='-'&&k+1<inner_e&&SRC[k+1]=='-'){ k=scan_comment(k); continue; }
        if(isw(c)){
            int ws,we; we=readword(k,&ws);
            if(wse(ws,we,"function")){ k=skip_function(we,inner_e); continue; }
            k=we; continue;
        }
        if(c=='('||c=='['||c=='{'){ d++; k++; continue; }
        if(c==')'||c==']'||c=='}'){ d--; k++; continue; }
        if(d==0&&(c==','||c==';')){
            if(n<512){ it[n].s=istart; it[n].e=k; n++; }
            k++; istart=k; continue;
        }
        k++;
    }
    if(n<512){ it[n].s=istart; it[n].e=inner_e; n++; }
    for(int t=0;t<n;t++){
        int ks,ke,vs;
        it[t].kind=classify(it[t].s,it[t].e,&ks,&ke,&vs);   /* -1 empty item */
    }
    int nreal=0;
    for(int t=0;t<n;t++) if(it[t].kind!=-1) nreal++;
    if(nreal==0){ oputs("{}"); ctor_depth--; return close+1; }
    oputs("{ ");
    int autonum=0,first=1;
    for(int t=0;t<n;t++){
        if(it[t].kind==-1) continue;
        if(!first) oputs(", ");
        first=0;
        int ks,ke,vs;
        if(it[t].kind==1){
            classify(it[t].s,it[t].e,&ks,&ke,&vs);
            oputs("\""); oput(SRC+ks,ke-ks); oputs("\": ");
            tr(vs,it[t].e);
        } else if(it[t].kind==2){
            classify(it[t].s,it[t].e,&ks,&ke,&vs);
            oputs("("); tr(ks+1,ke); oputs("): ");
            tr(vs,it[t].e);
        } else {
            autonum++;
            ofmt("%d: ",autonum);
            tr(it[t].s,it[t].e);
        }
    }
    oputs(" }");
    ctor_depth--;
    return close+1;
}

/* ---------- for ---------- */
static int expr_end(int i,int b,int stopat_do){
    int d=0;
    while(i<b){
        int c=(unsigned char)SRC[i];
        if(c=='"'||c=='\''){ i=scan_quote(i); continue; }
        if(c=='['){ int l=long_level(i); if(l>=0){ i=scan_long(i,l); continue; } }
        if(c=='-'&&i+1<b&&SRC[i+1]=='-'){ i=scan_comment(i); continue; }
        if(c=='('||c=='['||c=='{'){ d++; i++; continue; }
        if(c==')'||c==']'||c=='}'){ d--; i++; continue; }
        if(d==0){
            if(c==',') return i;
            if(isw((unsigned char)c)){
                int ws,we; we=readword(i,&ws);
                if(wse(ws,we,"do")&&stopat_do) return i;
                i=we; continue;
            }
        }
        i++;
    }
    return b;
}
/* split call-args region [s,e) into first/second arg on a top-level comma */
static void split_args(int s,int e,int *a1s,int *a1e,int *a2s,int *a2e){
    int d=0,k=s;
    *a1s=s; *a1e=e; *a2s=-1; *a2e=-1;
    while(k<e){
        int c=(unsigned char)SRC[k];
        if(c=='"'||c=='\''){ k=scan_quote(k); continue; }
        if(c=='['){ int l=long_level(k); if(l>=0){ k=scan_long(k,l); continue; } }
        if(c=='-'&&k+1<e&&SRC[k+1]=='-'){ k=scan_comment(k); continue; }
        if(c=='('||c=='['||c=='{'){ d++; k++; continue; }
        if(c==')'||c==']'||c=='}'){ d--; k++; continue; }
        if(d==0&&c==','){ *a1e=k; *a2s=skipws(k+1); *a2e=e; return; }
        k++;
    }
}
static int for_transform(int i,int b,int we){
    int p=skipws(we);
    int nv=0; int names[4][2];
    for(;;){
        if(p>=b||!isw((unsigned char)SRC[p])) break;
        int n2s,n2e; n2e=readword(p,&n2s);
        if(nv<4){ names[nv][0]=n2s; names[nv][1]=n2e; }
        nv++;
        p=skipws(n2e);
        if(p<b&&SRC[p]==','){ p=skipws(p+1); continue; }
        break;
    }
    if(nv==0||nv>2){ oputs("for"); ofmt(" --[[lua2luc: bad for var list (line %d)]]",W_LINE); return we; }
    if(p<b&&SRC[p]=='='){
        if(nv!=1){ oputs("for"); ofmt(" --[[lua2luc: multi-var numeric for (line %d)]]",W_LINE); return we; }
        p++;
        int e1s=skipws(p);
        int e1e=expr_end(e1s,b,1);
        if(e1e>=b||SRC[e1e]!=','){ oputs("for"); ofmt(" --[[lua2luc: numeric for needs 2 exprs (line %d)]]",W_LINE); return we; }
        int e2s=skipws(e1e+1);
        int e2e=expr_end(e2s,b,1);
        int e3s=-1,e3e=-1,after=e2e;
        if(e2e<b&&SRC[e2e]==','){ e3s=skipws(e2e+1); e3e=expr_end(e3s,b,1); after=e3e; }
        int dws,dwe;
        if(!peekword(after,&dws,&dwe)||!wse(dws,dwe,"do")){
            oputs("for"); ofmt(" --[[lua2luc: no do in numeric for (line %d)]]",W_LINE); return we;
        }
        int lend=match_block(dwe,b,0);
        if(lend<0){ oputs("for"); ofmt(" --[[lua2luc: loop end not found (line %d)]]",W_LINE); return we; }
        oputs("do\n    create __fa = ("); tr(e1s,e1e); oputs(")\n");
        oputs("    create __fb = ("); tr(e2s,e2e); oputs(")\n");
        if(e3s>=0){ oputs("    create __fs = ("); tr(e3s,e3e); oputs(")\n"); }
        else        oputs("    create __fs = 1\n");
        oputs("    create __fn = math.floor(((__fb) - (__fa)) / __fs) + 1\n");
        oputs("    repeat 1, __fn as __j do\n");
        oputs("        create "); oput(SRC+names[0][0],names[0][1]-names[0][0]);
        oputs(" = __fa + __j * __fs\n");
        pend_add(lend);
        return dwe;
    }
    if(p<b&&SRC[p]=='i'){
        int i2s,i2e;
        if(!peekword(p,&i2s,&i2e)||!wse(i2s,i2e,"in")){
            oputs("for"); ofmt(" --[[lua2luc: bad for header (line %d)]]",W_LINE); return we;
        }
        int es=skipws(i2e);
        int fws,fwe;
        if(!peekword(es,&fws,&fwe)){ oputs("for"); ofmt(" --[[lua2luc: no iterator (line %d)]]",W_LINE); return we; }
        int header_end=expr_end(es,b,1);
        if(header_end<b&&SRC[header_end]==','){
            ofmt("for --[[lua2luc: multi-expr iterator unsupported (line %d)]] ",W_LINE);
            oput(SRC+es,header_end-es);
            WKEPT++;
            return header_end;
        }
        int dws,dwe;
        if(!peekword(header_end,&dws,&dwe)||!wse(dws,dwe,"do")){
            oputs("for"); ofmt(" --[[lua2luc: no do (line %d)]]",W_LINE); return we;
        }
        int lend=match_block(dwe,b,0);
        if(lend<0){ oputs("for"); ofmt(" --[[lua2luc: loop end not found (line %d)]]",W_LINE); return we; }
        /* pairs(T) / ipairs(T) */
        if(wse(fws,fwe,"pairs")||wse(fws,fwe,"ipairs")){
            int ps=skipws(fwe);
            if(ps>=header_end||SRC[ps]!='('){
                oputs("for"); ofmt(" --[[lua2luc: expected ( (line %d)]]",W_LINE); return we;
            }
            int pe=balanced(ps,header_end,'(',')');
            if(pe<0){ oputs("for"); ofmt(" --[[lua2luc: unbalanced ( (line %d)]]",W_LINE); return we; }
            int ispairs=wse(fws,fwe,"ipairs");
            if(nv==1){
                if(ispairs){
                    oputs("repeat __k, "); oput(SRC+names[0][0],names[0][1]-names[0][0]);
                    oputs(" in "); tr(ps+1,pe-1); oputs(" do");
                } else {
                    oputs("repeat "); oput(SRC+names[0][0],names[0][1]-names[0][0]);
                    oputs(", __v in "); tr(ps+1,pe-1); oputs(" do");
                }
            } else {
                if(ispairs){
                    USED_IPAIRS2=1;
                    oputs("do\n    create __L = ("); tr(ps+1,pe-1); oputs(")\n");
                    oputs("    repeat 1, len(__L) as __i do\n");
                    oputs("        create "); oput(SRC+names[0][0],names[0][1]-names[0][0]);
                    oputs(" = __i + 1\n");
                    oputs("        create "); oput(SRC+names[1][0],names[1][1]-names[1][0]);
                    oputs(" = __at(__L, __i + 1)\n");
                } else {
                    oputs("repeat "); oput(SRC+names[0][0],names[0][1]-names[0][0]);
                    oputs(", "); oput(SRC+names[1][0],names[1][1]-names[1][0]);
                    oputs(" in "); tr(ps+1,pe-1); oputs(" do");
                }
            }
            if(ispairs&&nv==2) pend_add(lend);
            return dwe;
        }
        /* string.gmatch(s, p) or expr:gmatch(p) */
        int isgmatch=0;
        int a1s=-1,a1e=-1,a2s=-1,a2e=-1,recv_s=-1,recv_e=-1;
        if(wse(fws,fwe,"string")){
            int ds=skipws(fwe),w2s,w2e;
            if(ds<header_end&&SRC[ds]=='.'){
                if(peekword(ds+1,&w2s,&w2e)&&wse(w2s,w2e,"gmatch")){
                    int ps=skipws(w2e);
                    if(ps<header_end&&SRC[ps]=='('){
                        int pe=balanced(ps,header_end,'(',')');
                        if(pe>0){ isgmatch=1; split_args(ps+1,pe-1,&a1s,&a1e,&a2s,&a2e); }
                    }
                }
            }
        }
        if(!isgmatch){
            int q=es;
            while(q<header_end){
                int c=(unsigned char)SRC[q];
                if(c=='"'||c=='\''){ q=scan_quote(q); continue; }
                if(c=='-'&&q+1<header_end&&SRC[q+1]=='-'){ q=scan_comment(q); continue; }
                if(c==':'){
                    int w2s,w2e;
                    if(peekword(q+1,&w2s,&w2e)&&wse(w2s,w2e,"gmatch")){
                        int ps=skipws(w2e);
                        if(ps<header_end&&SRC[ps]=='('){
                            int pe=balanced(ps,header_end,'(',')');
                            if(pe>0){
                                isgmatch=2; recv_s=es; recv_e=q;
                                split_args(ps+1,pe-1,&a1s,&a1e,&a2s,&a2e);
                            }
                        }
                        break;
                    }
                }
                q++;
            }
        }
        if(isgmatch && ((isgmatch==2 && a1s>=0) || (isgmatch==1 && a2s>=0))){
            oputs("do\n    create __g = string.gmatch(");
            if(isgmatch==2){ tr(recv_s,recv_e); oputs(", "); tr(a1s,a1e); }
            else { tr(a1s,a1e); oputs(", "); tr(a2s,a2e); }
            oputs(")\n");
            oputs("    while true do\n        create ");
            oput(SRC+names[0][0],names[0][1]-names[0][0]);
            if(nv==2){
                oputs(", "); oput(SRC+names[1][0],names[1][1]-names[1][0]);
            }
            oputs(" = __g()\n        if ");
            oput(SRC+names[0][0],names[0][1]-names[0][0]);
            oputs(" == nil then break end\n");
            pend_add(lend);
            return dwe;
        }
        ofmt("for --[[lua2luc: unsupported iterator (line %d)]]",W_LINE);
        WKEPT++;
        oput(SRC+es,header_end-es);
        return header_end;
    }
    oputs("for"); ofmt(" --[[lua2luc: unparsed for (line %d)]]",W_LINE);
    return we;
}

/* ---------- repeat .. until ---------- */
static int repeat_transform(int i,int b,int we){
    int body_s=skipws(we);
    int u=match_block(body_s,b,1);
    if(u<0){ oputs("repeat"); ofmt(" --[[lua2luc: no matching until (line %d)]]",W_LINE); WKEPT++; return we; }
    int cs=skipws(u+5);
    int ce=cs;
    while(ce<b&&SRC[ce]!='\n') ce++;
    while(ce>cs&&(SRC[ce-1]==' '||SRC[ce-1]=='\r')) ce--;
    oputs("while true do\n");
    tr(body_s,u);
    oputs("\n    -- lua2luc: until-cond runs outside the loop body scope; check body locals\n");
    oputs("    if ("); tr(cs,ce); oputs(") then break end\nend");
    return ce;
}

/* ---------- driver ---------- */
static char *readall(const char *path){
    FILE *f=fopen(path,"rb"); if(!f) die("cannot open input");
    fseek(f,0,SEEK_END); long n=ftell(f); fseek(f,0,SEEK_SET);
    char *b=(char*)malloc((size_t)n+2); if(!b) die("oom");
    if(fread(b,1,(size_t)n,f)!=(size_t)n) die("read error");
    b[n]=0; fclose(f); SLEN=(int)n; return b;
}
int main(int argc,char **argv){
    if(argc<3){ fprintf(stderr,"usage: lua2luc <in> <out>\n"); return 1; }
    SRC=readall(argv[1]);
    OCAP=SLEN*2+8192; OUT=(char*)malloc((size_t)OCAP); OLEN=0; OUT[0]=0;
    W_LINE=1;
    int start=0;
    if(SLEN>1&&SRC[0]=='#'&&SRC[1]=='!'){
        while(start<SLEN&&SRC[start]!='\n') start++;
        if(start<SLEN) start++;
        oput(SRC,start);
    }
    tr(start,SLEN);
    FILE *f=fopen(argv[2],"wb"); if(!f) die("cannot open output");
    if(USED_IPAIRS2){
        const char *helper=
        "-- lua2luc: 1-based access helper (works for LUC dicts and lists)\n"
        "create function __at(L, i)\n"
        "    if type(L) == \"list\" then\n"
        "        return L[i - 1]\n"
        "    end\n"
        "    return L[i]\n"
        "end\n\n";
        fwrite(helper,1,strlen(helper),f);
    }
    fwrite(OUT,1,(size_t)OLEN,f);
    fclose(f);
    if(npend>0) fprintf(stderr,"lua2luc: WARNING %d pending extra end(s) in %s\n",npend,argv[1]);
    if(WKEPT>0) fprintf(stderr,"lua2luc: %d constructs left as-is in %s\n",WKEPT,argv[1]);
    return 0;
}
