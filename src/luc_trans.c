/* luc_trans.c - LUC 0.1 native code translator: x86-64 JIT + AOT PE32+ writer.
 *
 * Windows x64 only for the native parts; compiles (as no-ops that request
 * interpretation) everywhere else, so the tree still builds on POSIX.
 *
 *   cl  /c /O2 luc_trans.c            (MSVC)
 *   gcc -c -O2 -std=c99 luc_trans.c   (MinGW-w64)
 *
 * -----------------------------------------------------------------------
 * luc_trans.h  (copy this block into luc_trans.h if you want a header)
 * -----------------------------------------------------------------------
 *   #define LUC_JIT_FALLBACK (-1)
 *   int  luc_jit_run(Closure *cl);
 *   int  luc_jit_call(LucState *L,int func,int nargs,int nres);
 *   void luc_jit_shutdown(void);
 *   int  luc_aot_build(const char *src,int srclen,const char *outpath);
 *   int  luc_aot_embedded(char **psrc,int *plen);
 * -----------------------------------------------------------------------
 *
 * DESIGN
 *
 * Translation unit is one Proto.  A Proto is translated only if every
 * instruction is in the whitelist below.  Numeric ops cannot touch the
 * heap; GETTABLE/SETTABLE on lists read/write array elements inline under
 * guards.  Nothing translated ever allocates, calls (except the leaf
 * MOD/POW helpers), or raises: that is what makes bail-out sound.  Numeric
 * code writes nothing observable until RETURN; table stores write
 * prefix-identical values, so at any guard failure we return -1 and the
 * caller re-runs the whole call through the interpreter to bit-identical
 * state.  No mid-function deoptimisation, no shadow interpreter state, no
 * GC safepoints (nothing allocates, so the collector can never run
 * mid-function and unrooted Table* values in registers/frame stay valid).
 *
 *   whitelist: MOVE LOADK LOADNIL LOADBOOL ADD SUB MUL DIV MOD POW
 *              UNM NOT EQ NE LT LE GT GE GETTABLE SETTABLE
 *              JMP JMPIF JMPIFNOT FORPREP FORLOOP CLOSE(no-op) RETURN(0/1)
 *
 * Kinds: K_INT (integral double) < K_NUM, K_BOOL (0.0/1.0 double), K_NIL
 * (no bits, never read), K_LIST (raw Table* bits in the 64-bit home).
 * Every kind is created by a guard (param checks, element-type checks), so
 * downstream constant folds (branch on list = truthy, not list = false,
 * x == nil = false) can never observe a mismatched value: a mismatch bails
 * first.  K_INT is created by integral LOADK constants, by closure of
 * +,-,*,unm over K_INT (integral doubles are closed under those; overflow
 * gives +-Inf, which fails the index range check and bails), and by
 * FORPREP when the step is K_INT and a runtime "step > 0" guard holds.
 *
 * Native ABI of translated code (Windows x64):
 *
 *   int fn(const Value *args (RCX), int nargs (EDX), Value *out (R8));
 *
 *   returns  -1  -> guard failed / step==0, nothing written, interpret
 *             n  -> n results (0 or 1) written to out[]
 *
 * Args are read through type guards; results are written as boxed Values by
 * the native code, but only into the caller-supplied out[] buffer - never
 * into LucState.stack.  The C wrapper does the LucState.stack stores, so
 * ensure_stack() reallocation can never be observed by native code and no
 * unrooted Obj* is ever held across anything.  out[] only ever receives
 * number / boolean / nil, so it needs no GC rooting either.
 *
 * Registers: RBX=args, RSI=out, R12D=nargs, RAX/RCX/RDX/R8 + XMM0-4 scratch.
 * Frame (RSP 16-aligned after the prologue, 16-aligned at every CALL):
 *   [0..31]                      shadow space
 *   [32 .. 32+16*nsave)          saved callee-saved XMMs we pin
 *   [slotoff + 8*i]              home of register i when it is not pinned
 *   [dscoff + 24*d]              list descriptor d: arr(8) alen(4) stok(4)
 * All memory homes and descriptors are zeroed in the prologue so an
 * unreachable read is deterministic rather than undefined; frames larger
 * than a page are stack-probed before RSP moves.
 *
 * Register homing (generalises the old all-or-nothing pinned mode): each
 * slot has exactly ONE home, either an XMM or a frame slot - never both.
 * Homes are handed to the registers with the highest loop-depth-weighted
 * use count, so a maxstack=23 proto still runs its innermost loop entirely
 * out of XMM registers ("mixed-pin").  XMM0-4 are always scratch; XMM5 is
 * only handed out when the proto emits no CALL (MOD/POW are the only ones);
 * pinned XMM6+ are saved/restored in the prologue/epilogue.
 *
 * List descriptors (the guard-hoisting mechanism): the first time a
 * register is proven to hold a list (parameter guard, GETTABLE returning a
 * list, MOVE from a list) we cache its arr pointer, alen, and a "stok" flag
 * (alen>0 and the tail element is non-nil) in the frame.  That is legal for
 * the whole native execution because (i) nothing we emit allocates or
 * calls into the runtime, so no other agent can touch the table, and (ii)
 * our own inlined stores only ever write in-bounds LT_NUM values, which
 * changes neither arr nor alen and can only turn a false stok true.  So
 * the base-is-list check, the arr/alen loads and the tail-nil check (which
 * is what makes an in-bounds store exactly tab_set's in-bounds branch, its
 * trailing trim being a proven no-op) all happen once per definition -
 * i.e. in the loop preheader for a loop-invariant base - and the inner
 * loop keeps only what actually varies: the key's exactness/range check,
 * and for reads the per-element type check.  A one-entry (base,key) cache
 * additionally lets "Ci[j] = Ci[j] + x" reuse the scaled, bounds-checked
 * index across the read/modify/write triple.
 */

#include "luc.h"
#include <stddef.h>

#define LUC_JIT_FALLBACK (-1)

int  luc_jit_run(Closure *cl);
int  luc_jit_call(LucState *L,int func,int nargs,int nres);
void luc_jit_shutdown(void);
int  luc_aot_build(const char *src,int srclen,const char *outpath);
int  luc_aot_embedded(char **psrc,int *plen);

/* Value layout is derived, never assumed. */
#define VSZ   ((int)sizeof(Value))
#define VOFT  ((int)offsetof(Value,t))
#define VOFU  ((int)offsetof(Value,u))
typedef char luc_trans_value_assert[(sizeof(Value)==16 && offsetof(Value,u)==8)?1:-1];

#if defined(_WIN32)

#include <windows.h>

/* ===================================================================== */
/* 1. byte buffer                                                         */
/* ===================================================================== */

typedef struct { unsigned char *p; int n, cap, bad; } Buf;

static void buf_init(Buf *b,int cap){
    b->p=(unsigned char*)malloc((size_t)cap);
    b->n=0; b->cap=b->p?cap:0; b->bad=b->p?0:1;
}
static void buf_free(Buf *b){ free(b->p); b->p=NULL; b->n=b->cap=0; }
static int buf_need(Buf *b,int k){
    if(b->bad) return 0;
    if(b->n+k<=b->cap) return 1;
    int nc=b->cap?b->cap:64;
    while(nc<b->n+k){
        if(nc>(1<<28)){ b->bad=1; return 0; }
        nc*=2;
    }
    unsigned char *np=(unsigned char*)realloc(b->p,(size_t)nc);
    if(!np){ b->bad=1; return 0; }
    b->p=np; b->cap=nc; return 1;
}
static void e1(Buf *b,int v){ if(buf_need(b,1)) b->p[b->n++]=(unsigned char)v; }
static void e2(Buf *b,unsigned v){ if(buf_need(b,2)){ b->p[b->n++]=(unsigned char)(v&0xFF); b->p[b->n++]=(unsigned char)((v>>8)&0xFF);} }
static void e4(Buf *b,unsigned v){
    if(buf_need(b,4)){ for(int i=0;i<4;i++) b->p[b->n++]=(unsigned char)((v>>(8*i))&0xFF); }
}
static void e8(Buf *b,unsigned long long v){
    if(buf_need(b,8)){ for(int i=0;i<8;i++) b->p[b->n++]=(unsigned char)((v>>(8*i))&0xFF); }
}
static void eblob(Buf *b,const void *src,int len){
    if(len>0 && buf_need(b,len)){ memcpy(b->p+b->n,src,(size_t)len); b->n+=len; }
}
static void patch32(Buf *b,int at,int val){
    if(b->bad || at<0 || at+4>b->n) return;
    unsigned u=(unsigned)val;
    for(int i=0;i<4;i++) b->p[at+i]=(unsigned char)((u>>(8*i))&0xFF);
}
static void patch_rel(Buf *b,int at,int target){ patch32(b,at,target-(at+4)); }

/* ===================================================================== */
/* 2. x86-64 emitter                                                      */
/* ===================================================================== */

enum { R_RAX=0,R_RCX=1,R_RDX=2,R_RBX=3,R_RSP=4,R_RBP=5,R_RSI=6,R_RDI=7 };
/* condition codes */
enum { CC_B=2,CC_AE=3,CC_E=4,CC_NE=5,CC_BE=6,CC_A=7,CC_S=8,CC_P=0xA,CC_NP=0xB,
       CC_L=0xC,CC_GE=0xD,CC_LE=0xE,CC_G=0xF };

/* modrm for [rsp+disp32] : mod=10 rm=100 + sib 0x24 */
static void mrm_rsp(Buf *b,int reg,int disp){
    e1(b,0x80|((reg&7)<<3)|4); e1(b,0x24); e4(b,(unsigned)disp);
}
/* modrm for [base+disp32], base must be rbx/rsi/rcx/rdx/rax (not rsp/rbp) */
static void mrm_b(Buf *b,int reg,int base,int disp){
    e1(b,0x80|((reg&7)<<3)|(base&7)); e4(b,(unsigned)disp);
}
/* modrm for [rip+disp32] : mod=00 rm=101 */
static void mrm_rip(Buf *b,int reg,int disp){
    e1(b,0x00|((reg&7)<<3)|5); e4(b,(unsigned)disp);
}
/* modrm reg,reg */
static void mrm_rr(Buf *b,int reg,int rm){ e1(b,0xC0|((reg&7)<<3)|(rm&7)); }

static void emit_push(Buf *b,int r){ if(r>=8) e1(b,0x41); e1(b,0x50+(r&7)); }
static void emit_pop (Buf *b,int r){ if(r>=8) e1(b,0x41); e1(b,0x58+(r&7)); }
static void emit_sub_rsp(Buf *b,int imm){ e1(b,0x48); e1(b,0x81); mrm_rr(b,5,R_RSP); e4(b,(unsigned)imm); }
static void emit_add_rsp(Buf *b,int imm){ e1(b,0x48); e1(b,0x81); mrm_rr(b,0,R_RSP); e4(b,(unsigned)imm); }
static void emit_mov_rr64(Buf *b,int dst,int src){            /* mov dst,src */
    int rex=0x48|((src>=8)?4:0)|((dst>=8)?1:0);
    e1(b,rex); e1(b,0x89); mrm_rr(b,src,dst);
}
static void emit_mov_rr32(Buf *b,int dst,int src){
    int rex=((src>=8)?4:0)|((dst>=8)?1:0);
    if(rex) e1(b,0x40|rex);
    e1(b,0x89); mrm_rr(b,src,dst);
}
static void emit_mov_r64_imm(Buf *b,int r,unsigned long long v){
    e1(b,0x48|((r>=8)?1:0)); e1(b,0xB8+(r&7)); e8(b,v);
}
static void emit_mov_r32_imm(Buf *b,int r,unsigned v){
    if(r>=8) e1(b,0x41);
    e1(b,0xB8+(r&7)); e4(b,v);
}
static void emit_xor_eax(Buf *b){ e1(b,0x31); mrm_rr(b,R_RAX,R_RAX); }
static void emit_ret(Buf *b){ e1(b,0xC3); }

/* ---- generic [rsp+disp32] moves (frame homes + descriptors) ---- */
static void emit_ld_r64_rsp(Buf *b,int r,int d){ e1(b,0x48|((r>=8)?4:0)); e1(b,0x8B); mrm_rsp(b,r,d); }
static void emit_st_r64_rsp(Buf *b,int r,int d){ e1(b,0x48|((r>=8)?4:0)); e1(b,0x89); mrm_rsp(b,r,d); }
static void emit_ld_r32_rsp(Buf *b,int r,int d){ if(r>=8) e1(b,0x44); e1(b,0x8B); mrm_rsp(b,r,d); }
static void emit_st_r32_rsp(Buf *b,int r,int d){ if(r>=8) e1(b,0x44); e1(b,0x89); mrm_rsp(b,r,d); }
static void emit_mov_m32_imm_rsp(Buf *b,int d,int imm){ e1(b,0xC7); mrm_rsp(b,0,d); e4(b,(unsigned)imm); }
static void emit_cmp_m32_imm_rsp(Buf *b,int d,int imm){ e1(b,0x81); mrm_rsp(b,7,d); e4(b,(unsigned)imm); }

/* mov rax,[base+d] (base is rbx/rsi, 64-bit load for table pointers) */
static void emit_ld_rax_mb(Buf *b,int base,int d){ e1(b,0x48); e1(b,0x8B); mrm_b(b,R_RAX,base,d); }
/* movq r64,xmmX / movq xmmX,r64 (raw 8 bytes, never interpreted as double) */
static void emit_movq_r_x(Buf *b,int r,int x){
    e1(b,0x66); e1(b,0x48|((r>=8)?1:0)|((x>=8)?4:0)); e1(b,0x0F); e1(b,0x7E); mrm_rr(b,x,r);
}
static void emit_movq_x_r(Buf *b,int x,int r){
    e1(b,0x66); e1(b,0x48|((x>=8)?4:0)|((r>=8)?1:0)); e1(b,0x0F); e1(b,0x6E); mrm_rr(b,x,r);
}

/* SSE: prefix + [REX] + 0F + opcode. REX.R extends the xmm field so
 * XMM8-15 work; when every reg is <8 no REX byte is emitted and the
 * encoding is byte-identical to the legacy path. */
static void sse_rsp(Buf *b,int pfx,int opc,int x,int d){
    if(pfx) e1(b,pfx);
    if(x>=8) e1(b,0x44);
    e1(b,0x0F); e1(b,opc); mrm_rsp(b,x,d);
}
static void sse_mb(Buf *b,int pfx,int opc,int x,int base,int d){
    if(pfx) e1(b,pfx);
    if(x>=8) e1(b,0x44);
    e1(b,0x0F); e1(b,opc); mrm_b(b,x,base,d);
}
static void sse_rip(Buf *b,int pfx,int opc,int x,int d){
    if(pfx) e1(b,pfx);
    if(x>=8) e1(b,0x44);
    e1(b,0x0F); e1(b,opc); mrm_rip(b,x,d);
}
static void sse_rr(Buf *b,int pfx,int opc,int xd,int xs){
    int rex=0x40|((xd>=8)?4:0)|((xs>=8)?1:0);
    if(pfx) e1(b,pfx);
    if(rex!=0x40) e1(b,rex);
    e1(b,0x0F); e1(b,opc); mrm_rr(b,xd,xs);
}
#define SSE_MOVSD_LD 0x10
#define SSE_MOVSD_ST 0x11
#define SSE_MOVAPD   0x28
#define SSE_ADD      0x58
#define SSE_SUB      0x5C
#define SSE_MUL      0x59
#define SSE_DIV      0x5E
#define SSE_COMISD   0x2F
#define SSE_XORPD    0x57

static void emit_cvtsi2sd_x_eax(Buf *b,int x){ e1(b,0xF2); if(x>=8) e1(b,0x44); e1(b,0x0F); e1(b,0x2A); mrm_rr(b,x,R_RAX); }
/* cvtsi2sd xmmX,r32 */
static void emit_cvtsi2sd_x_r32(Buf *b,int x,int r){
    e1(b,0xF2);
    { int rex=((x>=8)?4:0)|((r>=8)?1:0); if(rex) e1(b,0x40|rex); }
    e1(b,0x0F); e1(b,0x2A); mrm_rr(b,x,r);
}
/* cvttsd2si r32,xmmX  (32-bit form: out-of-range/NaN yields INT_MIN, which
 * the sign test below rejects, so no 64-bit key can slip through) */
static void emit_cvttsd2si_r32_x(Buf *b,int r,int x){
    e1(b,0xF2);
    { int rex=((r>=8)?4:0)|((x>=8)?1:0); if(rex) e1(b,0x40|rex); }
    e1(b,0x0F); e1(b,0x2C); mrm_rr(b,r,x);
}
/* cvttsd2si r32,[rsp+d] */
static void emit_cvttsd2si_r32_rsp(Buf *b,int r,int d){
    e1(b,0xF2); if(r>=8) e1(b,0x44); e1(b,0x0F); e1(b,0x2C); mrm_rsp(b,r,d);
}
/* movaps [rsp+d],xmmX / xmmX,[rsp+d] (d keeps 16B alignment; XMM saves) */
static void emit_movaps_st_rsp(Buf *b,int x,int d){
    if(x>=8) e1(b,0x44);
    e1(b,0x0F); e1(b,0x29); mrm_rsp(b,x,d);
}
static void emit_movaps_ld_rsp(Buf *b,int x,int d){
    if(x>=8) e1(b,0x44);
    e1(b,0x0F); e1(b,0x28); mrm_rsp(b,x,d);
}
static void emit_setcc_al(Buf *b,int cc){ e1(b,0x0F); e1(b,0x90+cc); mrm_rr(b,0,R_RAX); }
static void emit_setcc_cl(Buf *b,int cc){ e1(b,0x0F); e1(b,0x90+cc); mrm_rr(b,0,R_RCX); }
static void emit_movzx_eax_al(Buf *b){ e1(b,0x0F); e1(b,0xB6); mrm_rr(b,R_RAX,R_RAX); }
static void emit_and_al_cl(Buf *b){ e1(b,0x20); mrm_rr(b,R_RCX,R_RAX); }
static void emit_or_al_cl (Buf *b){ e1(b,0x08); mrm_rr(b,R_RCX,R_RAX); }

static int emit_jcc(Buf *b,int cc){ e1(b,0x0F); e1(b,0x80+cc); int at=b->n; e4(b,0); return at; }
static int emit_jmp (Buf *b){ e1(b,0xE9); int at=b->n; e4(b,0); return at; }

/* cmp dword [base+d], imm32 */
static void emit_cmp_m32_imm(Buf *b,int base,int d,int imm){
    e1(b,0x81); mrm_b(b,7,base,d); e4(b,(unsigned)imm);
}
/* cmp r32, imm32 (r may be r8-r15) */
static void emit_cmp_r32_imm(Buf *b,int r,int imm){
    if(r>=8) e1(b,0x41);
    e1(b,0x81); mrm_rr(b,7,r); e4(b,(unsigned)imm);
}
/* cmp r64,r64 : cmp a,bb */
static void emit_cmp_rr64(Buf *b,int a,int bb){
    e1(b,0x48|((bb>=8)?4:0)|((a>=8)?1:0)); e1(b,0x39); mrm_rr(b,bb,a);
}
/* mov dword [rsi+d], imm32 ; mov [rsi+d], eax */
static void emit_mov_m32_imm_rsi(Buf *b,int d,int imm){
    e1(b,0xC7); mrm_b(b,0,R_RSI,d); e4(b,(unsigned)imm);
}
static void emit_mov_m32_eax_rsi(Buf *b,int d){ e1(b,0x89); mrm_b(b,R_RAX,R_RSI,d); }

/* ---- table-indexing primitives ---- */
#define TAB_ARR_OFF  ((int)offsetof(Table,arr))
#define TAB_ALEN_OFF ((int)offsetof(Table,alen))
#define SIB_DX_CX 0x0A   /* scale0, index RCX, base RDX */
static void emit_ld_r64_raxoff(Buf *b,int r,int off){  /* mov r64,[rax+off] */
    e1(b,0x48|((r>=8)?4:0)); e1(b,0x8B); mrm_b(b,r,R_RAX,off);
}
static void emit_ld_r32_raxoff(Buf *b,int r,int off){  /* mov r32,[rax+off] */
    if(r>=8) e1(b,0x44); e1(b,0x8B); mrm_b(b,r,R_RAX,off);
}
static void emit_cmp_ecx_r8d(Buf *b){ e1(b,0x44); e1(b,0x39); e1(b,0xC1); }  /* cmp ecx,r8d */
static void emit_shl_rcx(Buf *b,int imm){ e1(b,0x48); e1(b,0xC1); e1(b,0xE1); e1(b,imm); }
static void emit_test_ecx(Buf *b){ e1(b,0x85); e1(b,0xC9); }                /* test ecx,ecx */
static void emit_mov_ecx_r8d(Buf *b){ e1(b,0x41); e1(b,0x8B); e1(b,0xC8); }  /* mov ecx,r8d */
static void emit_dec_ecx(Buf *b){ e1(b,0xFF); e1(b,0xC9); }                 /* dec ecx */
static void emit_ld_r32_sib(Buf *b,int r){   /* mov r32,[rdx+rcx] */
    if(r>=8) e1(b,0x44); e1(b,0x8B); e1(b,((r&7)<<3)|4); e1(b,SIB_DX_CX);
}
static void emit_ld_r64_sib8(Buf *b,int r){  /* mov r64,[rdx+rcx+8] */
    e1(b,0x48|((r>=8)?4:0)); e1(b,0x8B);
    e1(b,0x40|((r&7)<<3)|4); e1(b,SIB_DX_CX); e1(b,VOFU);
}
static void emit_movsd_ld_sib8(Buf *b,int x){  /* movsd xmmX,[rdx+rcx+8] */
    e1(b,0xF2); if(x>=8) e1(b,0x44); e1(b,0x0F); e1(b,0x10);
    e1(b,0x40|((x&7)<<3)|4); e1(b,SIB_DX_CX); e1(b,VOFU);
}
static void emit_mov_m32_imm_sib(Buf *b,int imm){  /* mov dword [rdx+rcx],imm */
    e1(b,0xC7); e1(b,0x04); e1(b,SIB_DX_CX); e4(b,(unsigned)imm);
}
static void emit_movsd_st_sib8(Buf *b,int x){  /* movsd [rdx+rcx+8],xmmX */
    e1(b,0xF2); if(x>=8) e1(b,0x44); e1(b,0x0F); e1(b,0x11);
    e1(b,0x40|((x&7)<<3)|4); e1(b,SIB_DX_CX); e1(b,VOFU);
}
static void emit_cmp_eax_imm32(Buf *b,int imm){ e1(b,0x3D); e4(b,(unsigned)imm); }  /* cmp eax,imm32 */
static void emit_cmp_r8d_0(Buf *b){ e1(b,0x41); e1(b,0x83); e1(b,0xF8); e1(b,0); }  /* cmp r8d,0 */

/* call an absolute host address, 32B shadow already reserved by the frame */
static void emit_call_abs(Buf *b,void *fnp){
    unsigned long long a; memcpy(&a,&fnp,sizeof a);
    emit_mov_r64_imm(b,R_RAX,a);
    e1(b,0xFF); mrm_rr(b,2,R_RAX);            /* call rax */
}

/* ===================================================================== */
/* 3. helpers the fast path may call (MOD / POW only)                     */
/* ===================================================================== */

/* Byte-for-byte the interpreter's semantics: x - floor(x/y)*y */
static double jit_mod(double x,double y){ return x-floor(x/y)*y; }
static double jit_pow(double x,double y){ return pow(x,y); }

/* ===================================================================== */
/* 4. abstract kinds + translation-time analysis                          */
/* ===================================================================== */

enum { K_NONE=0, K_INT, K_NUM, K_BOOL, K_NIL, K_LIST, K_BAD };
#define KISNUM(k) ((k)==K_INT||(k)==K_NUM)

/* EQ/NE lowering modes */
enum { CM_NUM=0, CM_PTR, CM_FALSE, CM_TRUE };

typedef struct {
    unsigned char kb,kc,ka,kr,vis,iguard,cmode;
} OpInfo;

typedef struct {
    Proto *p;
    int    nregs, nslots;
    int    framesz, slotoff, dscoff, nsave, npin, nused;
    int    pic;                 /* 1 = no absolute host addresses allowed  */
    OpInfo *info;
    unsigned char *istarget;
    unsigned char **bs;         /* block-entry kind vectors (targets only) */
    unsigned char *pinit;       /* initial kind per parameter (K_NUM/LIST) */
    unsigned char *asbase,*asnum,*askey,*needdsc;
    int   *dsc, ndsc;           /* per-register descriptor index or -1     */
    int   *home;                /* per-register XMM home or -1 (frame)     */
    int    savoff[16];          /* per physical XMM: save offset or -1     */
    int   *depth;
    int   *wl, nwl;
    char   err[160];
} Ana;

static int ana_fail(Ana *a,const char *what,int pc){
    if(!a->err[0]) snprintf(a->err,sizeof a->err,"%s at pc %d",what,pc);
    return 0;
}
/* lattice: NONE -> {INT,NUM,BOOL,NIL,LIST} -> NUM (from INT) -> BAD */
static int kmerge(unsigned char *dst,const unsigned char *src,int n){
    int changed=0;
    for(int i=0;i<n;i++){
        unsigned char d=dst[i], s=src[i], r;
        if(d==s) r=d;
        else if(d==K_NONE) r=s;
        else if(s==K_NONE) r=d;
        else if(KISNUM(d)&&KISNUM(s)) r=K_NUM;
        else r=K_BAD;
        if(r!=d){ dst[i]=r; changed=1; }
    }
    return changed;
}
static void wl_push(Ana *a,int pc){
    for(int i=0;i<a->nwl;i++) if(a->wl[i]==pc) return;
    a->wl[a->nwl++]=pc;
}
static int succ_to(Ana *a,int pc,const unsigned char *cur){
    if(pc<0 || pc>=a->p->ncode) return ana_fail(a,"branch out of range",pc);
    if(!a->bs[pc]){
        a->bs[pc]=(unsigned char*)malloc((size_t)a->nregs);
        if(!a->bs[pc]) return ana_fail(a,"out of memory",pc);
        memcpy(a->bs[pc],cur,(size_t)a->nregs);
        wl_push(a,pc);
    } else if(kmerge(a->bs[pc],cur,a->nregs)) wl_push(a,pc);
    return 1;
}

static int op_hasB(int op);
static int op_hasC(int op);
static int is_supported_op(int op){
    switch(op){
        case OP_MOVE: case OP_LOADK: case OP_LOADNIL: case OP_LOADBOOL:
        case OP_ADD: case OP_SUB: case OP_MUL: case OP_DIV: case OP_MOD:
        case OP_POW: case OP_UNM: case OP_NOT:
        case OP_EQ: case OP_NE: case OP_LT: case OP_LE: case OP_GT: case OP_GE:
        case OP_GETTABLE: case OP_SETTABLE:
        case OP_ADDEQ: case OP_SUBEQ: case OP_MULEQ: case OP_DIVEQ:
        case OP_JMP: case OP_JMPIF: case OP_JMPIFNOT:
        case OP_FORPREP: case OP_FORLOOP:
        case OP_CLOSE: case OP_RETURN:
            return 1;
        default: return 0;
    }
}
/* an integral, finite double: cvttsd2si round-trips it or overflows into
 * INT_MIN, which the index guards reject */
static int const_is_int(double d){
    if(!(d==d)) return 0;                       /* NaN */
    if(!(d>=-1.0e300 && d<=1.0e300)) return 0;  /* +-Inf and absurd magnitudes */
    return d==floor(d);
}
static void mark_list(Ana *a,unsigned char *cur,int r){
    cur[r]=K_LIST;
    if(r<a->nregs) a->needdsc[r]=1;
}

/* walk one straight-line region starting at pc, updating info[] */
static int ana_region(Ana *a,int startpc){
    Proto *p=a->p;
    int n=a->nregs;
    unsigned char *cur=(unsigned char*)malloc((size_t)n);
    if(!cur) return ana_fail(a,"out of memory",startpc);
    memcpy(cur,a->bs[startpc],(size_t)n);

    int pc=startpc, ok=1;
    for(;;){
        if(pc>=p->ncode){ ok=ana_fail(a,"fell off code",pc); break; }
        if(pc!=startpc && a->istarget[pc]){ ok=succ_to(a,pc,cur); break; }

        uint32_t ins=p->code[pc];
        int op=GET_OP(ins), A=GET_A(ins), B=GET_B(ins), C=GET_C(ins);
        OpInfo *in=&a->info[pc];
        in->vis=1; in->kb=K_NONE; in->kc=K_NONE; in->ka=K_NONE;
        in->iguard=0; in->cmode=CM_NUM;

        if(!is_supported_op(op)){ ok=ana_fail(a,"unsupported opcode",pc); break; }
        if(A>=n){ ok=ana_fail(a,"register out of frame",pc); break; }
        if((op_hasB(op)&&B>=n)||(op_hasC(op)&&C>=n)){ ok=ana_fail(a,"register out of frame",pc); break; }

        switch(op){
            case OP_MOVE:
                in->kb=cur[B];
                if(cur[B]==K_NONE||cur[B]==K_BAD){ ok=ana_fail(a,"MOVE of untyped reg",pc); break; }
                if(cur[B]==K_LIST) mark_list(a,cur,A);
                else cur[A]=cur[B];
                break;
            case OP_LOADK: {
                int bx=GET_Bx(ins);
                if(bx<0||bx>=p->nk||p->k[bx].t!=LT_NUM){ ok=ana_fail(a,"non-numeric constant",pc); break; }
                cur[A]=const_is_int(p->k[bx].u.n)?K_INT:K_NUM;
                break; }
            case OP_LOADNIL:  cur[A]=K_NIL;  break;
            case OP_LOADBOOL:
                /* LUC never emits the Lua "skip next" form; refuse it rather
                   than silently dropping a control-flow edge. */
                if(C!=0){ ok=ana_fail(a,"LOADBOOL with skip",pc); break; }
                cur[A]=K_BOOL;
                break;
            case OP_ADD: case OP_SUB: case OP_MUL: case OP_DIV:
            case OP_MOD: case OP_POW:
                if((op==OP_MOD||op==OP_POW) && a->pic){ ok=ana_fail(a,"MOD/POW needs a runtime call",pc); break; }
                if(!KISNUM(cur[B])||!KISNUM(cur[C])){ ok=ana_fail(a,"non-numeric arithmetic",pc); break; }
                in->kb=cur[B]; in->kc=cur[C];
                /* integral doubles are closed under + - * ; overflow gives
                   +-Inf, which fails the index range guard and bails */
                if((op==OP_ADD||op==OP_SUB||op==OP_MUL)&&cur[B]==K_INT&&cur[C]==K_INT) cur[A]=K_INT;
                else cur[A]=K_NUM;
                break;
            case OP_UNM:
                if(!KISNUM(cur[B])){ ok=ana_fail(a,"non-numeric negation",pc); break; }
                in->kb=cur[B]; cur[A]=(cur[B]==K_INT)?K_INT:K_NUM;
                break;
            case OP_NOT:
                if(cur[B]==K_NONE||cur[B]==K_BAD){ ok=ana_fail(a,"'not' of untyped reg",pc); break; }
                in->kb=cur[B]; cur[A]=K_BOOL;
                break;
            case OP_LT: case OP_LE: case OP_GT: case OP_GE:
                if(!KISNUM(cur[B])||!KISNUM(cur[C])){ ok=ana_fail(a,"non-numeric comparison",pc); break; }
                in->kb=cur[B]; in->kc=cur[C]; cur[A]=K_BOOL;
                break;
            case OP_EQ: case OP_NE: {
                /* class of a kind: 0 num, 1 bool, 2 nil, 3 list.  Different
                   classes are rawequal-unequal by type, so EQ folds to false
                   with no fallback (this is the common `x == nil` case).
                   Same class: numbers/bools compare as doubles (our bools
                   are exactly 0.0/1.0), lists compare as pointers, nil==nil
                   is a constant.  Sound only because every kind was created
                   by a runtime guard. */
                unsigned char kb=cur[B],kc=cur[C];
                if(kb==K_NONE||kb==K_BAD||kc==K_NONE||kc==K_BAD){ ok=ana_fail(a,"comparison of untyped reg",pc); break; }
                int cb = KISNUM(kb)?0 : kb==K_BOOL?1 : kb==K_NIL?2 : 3;
                int cc2= KISNUM(kc)?0 : kc==K_BOOL?1 : kc==K_NIL?2 : 3;
                if(cb!=cc2)      in->cmode=CM_FALSE;
                else if(cb==2)   in->cmode=CM_TRUE;
                else if(cb==3)   in->cmode=CM_PTR;
                else             in->cmode=CM_NUM;
                in->kb=kb; in->kc=kc; cur[A]=K_BOOL;
                break; }
            case OP_GETTABLE: {
                if(cur[B]!=K_LIST||!KISNUM(cur[C])){ ok=ana_fail(a,"gettable needs list+int",pc); break; }
                in->kb=K_LIST; in->kc=cur[C]; in->iguard=(cur[C]==K_INT);
                if(in->kr!=K_NUM&&in->kr!=K_LIST){ ok=ana_fail(a,"ambiguous gettable result",pc); break; }
                if(in->kr==K_LIST) mark_list(a,cur,A);
                else cur[A]=K_NUM;
                break; }
            case OP_SETTABLE:
                if(cur[A]!=K_LIST||!KISNUM(cur[B])||!KISNUM(cur[C])){ ok=ana_fail(a,"settable needs list+int+num",pc); break; }
                in->ka=K_LIST; in->kb=cur[B]; in->kc=cur[C]; in->iguard=(cur[B]==K_INT);
                break;
            case OP_ADDEQ: case OP_SUBEQ: case OP_MULEQ: case OP_DIVEQ:
                /* read-modify-write, no kind change: base stays K_LIST */
                if(cur[A]!=K_LIST||!KISNUM(cur[B])||!KISNUM(cur[C])){ ok=ana_fail(a,"compeq needs list+int+num",pc); break; }
                a->needdsc[A]=1;   /* A<nregs verified by the frame check above */
                in->ka=K_LIST; in->kb=cur[B]; in->kc=cur[C]; in->iguard=(cur[B]==K_INT);
                break;
            case OP_CLOSE: break;   /* no upvalues can exist in a whitelisted proto */
            case OP_JMP:
                ok=succ_to(a,pc+1+GET_sBx(ins),cur);
                goto region_done;
            case OP_JMPIF: case OP_JMPIFNOT: {
                if(cur[A]==K_NONE||cur[A]==K_BAD){ ok=ana_fail(a,"branch on untyped reg",pc); break; }
                in->ka=cur[A];
                int tgt=pc+1+GET_sBx(ins);
                int truthy = KISNUM(cur[A])||cur[A]==K_LIST;   /* 0.0 is truthy */
                int falsy  = (cur[A]==K_NIL);
                int always = (truthy&&op==OP_JMPIF)||(falsy&&op==OP_JMPIFNOT);
                int never  = (truthy&&op==OP_JMPIFNOT)||(falsy&&op==OP_JMPIF);
                if(always){ ok=succ_to(a,tgt,cur); goto region_done; }
                if(!never){ if(!succ_to(a,tgt,cur)){ ok=0; goto region_done; } }
                break; }
            case OP_FORPREP: {
                if(A+3>=n){ ok=ana_fail(a,"loop registers out of frame",pc); break; }
                if(!KISNUM(cur[A])||!KISNUM(cur[A+1])){ ok=ana_fail(a,"non-numeric loop bounds",pc); break; }
                /* -st, tg-st and 0 are integral when st and tg are */
                unsigned char kk=(cur[A]==K_INT&&cur[A+1]==K_INT)?K_INT:K_NUM;
                cur[A]=cur[A+1]=cur[A+2]=cur[A+3]=kk;
                ok=succ_to(a,pc+1+GET_sBx(ins),cur);
                goto region_done; }
            case OP_FORLOOP: {
                if(A+3>=n){ ok=ana_fail(a,"loop registers out of frame",pc); break; }
                if(!KISNUM(cur[A])||!KISNUM(cur[A+1])||!KISNUM(cur[A+2])){ ok=ana_fail(a,"loop state clobbered",pc); break; }
                unsigned char kk=(cur[A]==K_INT&&cur[A+2]==K_INT)?K_INT:K_NUM;
                cur[A]=kk; cur[A+3]=kk;
                if(!succ_to(a,pc+1+GET_sBx(ins),cur)){ ok=0; goto region_done; }
                break; }
            case OP_RETURN: {
                if(B==0){ ok=ana_fail(a,"variable-length return",pc); break; }
                int nr=B-1;
                if(nr>1){ ok=ana_fail(a,"multiple return values",pc); break; }
                if(nr==1){
                    if(cur[A]==K_NONE||cur[A]==K_BAD){ ok=ana_fail(a,"return of untyped reg",pc); break; }
                    if(cur[A]==K_LIST){ ok=ana_fail(a,"cannot return a table",pc); break; }
                    in->ka=KISNUM(cur[A])?K_NUM:cur[A];
                }
                goto region_done; }
            default: ok=ana_fail(a,"unsupported opcode",pc); break;
        }
        if(!ok) break;
        pc++;
    }
region_done:
    free(cur);
    return ok;
}

static int ana_run(Ana *a){
    Proto *p=a->p;
    int nc=p->ncode;
    for(int pc=0;pc<nc;pc++){
        int op=GET_OP(p->code[pc]);
        if(op==OP_JMP||op==OP_JMPIF||op==OP_JMPIFNOT||op==OP_FORPREP||op==OP_FORLOOP){
            int t=pc+1+GET_sBx(p->code[pc]);
            if(t<0||t>=nc) return ana_fail(a,"branch out of range",pc);
            a->istarget[t]=1;
        }
    }
    a->bs[0]=(unsigned char*)calloc((size_t)a->nregs,1);
    if(!a->bs[0]) return ana_fail(a,"out of memory",0);
    for(int i=0;i<p->nparams && i<a->nregs;i++){
        a->bs[0][i]=a->pinit[i];
        if(a->pinit[i]==K_LIST) a->needdsc[i]=1;
    }
    a->istarget[0]=1;
    a->nwl=0; wl_push(a,0);

    int guard=0;
    while(a->nwl>0){
        if(++guard>200000) return ana_fail(a,"analysis did not converge",0);
        int pc=a->wl[--a->nwl];
        if(!ana_region(a,pc)) return 0;
    }
    return 1;
}

/* ===================================================================== */
/* 5. code generation                                                     */
/* ===================================================================== */

#define DSCSZ 24                     /* arr(8) alen(4) stok(4) + padding */

typedef struct { int at, topc; } Fix;

typedef struct {
    Buf   b;
    Ana  *a;
    int  *pcoff;
    Fix  *fix; int nfix, fixcap;
    int  *bailfix, nbail, bailcap;
    int  *retfix, nret, retcap;
    int   kcv, kcr;      /* EDI caches the validated int key of register kcr */
} Gen;

static int gen_addfix(Gen *g,int at,int topc){
    if(g->nfix==g->fixcap){
        int nc=g->fixcap?g->fixcap*2:64;
        Fix *nf=(Fix*)realloc(g->fix,sizeof(Fix)*(size_t)nc);
        if(!nf) return 0;
        g->fix=nf; g->fixcap=nc;
    }
    g->fix[g->nfix].at=at; g->fix[g->nfix].topc=topc; g->nfix++;
    return 1;
}
static int gen_addlist(int **arr,int *n,int *cap,int at){
    if(*n==*cap){
        int nc=*cap?*cap*2:64;
        int *na=(int*)realloc(*arr,sizeof(int)*(size_t)nc);
        if(!na) return 0;
        *arr=na; *cap=nc;
    }
    (*arr)[(*n)++]=at; return 1;
}
#define SLOTO(g,i) ((g)->a->slotoff+8*(i))
#define DSCO(g,r)  ((g)->a->dscoff+DSCSZ*(g)->a->dsc[r])
#define BAIL()  do{ int _at=emit_jmp(&g->b); if(!gen_addlist(&g->bailfix,&g->nbail,&g->bailcap,_at)) return 0; }while(0)
#define BAILCC(cc) do{ int _at=emit_jcc(&g->b,(cc)); if(!gen_addlist(&g->bailfix,&g->nbail,&g->bailcap,_at)) return 0; }while(0)
#define RETJMP() do{ int _at=emit_jmp(&g->b); if(!gen_addlist(&g->retfix,&g->nret,&g->retcap,_at)) return 0; }while(0)
#define JUMPTO(pc) do{ int _at=emit_jmp(&g->b); if(!gen_addfix(g,_at,(pc))) return 0; }while(0)
#define JCCTO(cc,pc) do{ int _at=emit_jcc(&g->b,(cc)); if(!gen_addfix(g,_at,(pc))) return 0; }while(0)

/* cmp r32,[rsp+d] */
static void emit_cmp_r32_rsp(Buf *b,int r,int d){
    if(r>=8) e1(b,0x44); e1(b,0x3B); mrm_rsp(b,r,d);
}

/* ---- one home per slot: XMM(home[s]) or frame [rsp+slotoff+8*s] ---- */
static void g_ldx(Gen *g,int x,int s){
    int h=g->a->home[s];
    if(h>=0) sse_rr(&g->b,0x66,SSE_MOVAPD,x,h);
    else sse_rsp(&g->b,0xF2,SSE_MOVSD_LD,x,SLOTO(g,s));
}
static void g_stx(Gen *g,int s,int x){
    int h=g->a->home[s];
    if(h>=0) sse_rr(&g->b,0x66,SSE_MOVAPD,h,x);
    else sse_rsp(&g->b,0xF2,SSE_MOVSD_ST,x,SLOTO(g,s));
}
static void g_op0(Gen *g,int sop,int s){
    int h=g->a->home[s];
    if(h>=0) sse_rr(&g->b,0xF2,sop,0,h);
    else sse_rsp(&g->b,0xF2,sop,0,SLOTO(g,s));
}
static void g_cmp0(Gen *g,int s){
    int h=g->a->home[s];
    if(h>=0) sse_rr(&g->b,0x66,SSE_COMISD,0,h);
    else sse_rsp(&g->b,0x66,SSE_COMISD,0,SLOTO(g,s));
}
static void g_ldptr(Gen *g,int s){          /* RAX = raw bits of slot s */
    int h=g->a->home[s];
    if(h>=0) emit_movq_r_x(&g->b,R_RAX,h);
    else emit_ld_r64_rsp(&g->b,R_RAX,SLOTO(g,s));
}
static void g_stptr(Gen *g,int s){          /* slot s = RAX (raw bits) */
    int h=g->a->home[s];
    if(h>=0) emit_movq_x_r(&g->b,h,R_RAX);
    else emit_st_r64_rsp(&g->b,R_RAX,SLOTO(g,s));
}
static void g_const(Gen *g,int s,double d){
    unsigned long long bits; memcpy(&bits,&d,sizeof bits);
    emit_mov_r64_imm(&g->b,R_RAX,bits);
    g_stptr(g,s);
}
static void g_kill(Gen *g,int r){ if(g->kcv&&g->kcr==r) g->kcv=0; }

/* Build the list descriptor of register r from the Table* in RAX.
 * Clobbers RAX/RCX/RDX/R8.  stok = (alen>0 && arr[alen-1].t != LT_NIL):
 * the tail-non-nil precondition that makes an in-bounds numeric store
 * exactly tab_set's in-bounds branch (its trailing trim a proven no-op).
 * Legal to cache for the whole call: nothing we emit allocates or calls
 * into the runtime, and our own stores change neither arr nor alen and
 * can only turn stok from false to true. */
static void g_dscbuild(Gen *g,int r){
    Buf *b=&g->b; int d=DSCO(g,r);
    emit_ld_r64_raxoff(b,R_RDX,TAB_ARR_OFF);
    emit_ld_r32_raxoff(b,8,TAB_ALEN_OFF);
    emit_st_r64_rsp(b,R_RDX,d);
    emit_st_r32_rsp(b,8,d+8);
    emit_mov_m32_imm_rsp(b,d+12,0);
    emit_cmp_r8d_0(b);
    int j1=emit_jcc(b,CC_LE);
    emit_mov_ecx_r8d(b); emit_dec_ecx(b); emit_shl_rcx(b,4);
    emit_ld_r32_sib(b,R_RAX);               /* EAX: Table* is dead here */
    emit_cmp_eax_imm32(b,LT_NIL);
    int j2=emit_jcc(b,CC_E);
    emit_mov_m32_imm_rsp(b,d+12,1);
    patch_rel(b,j1,b->n); patch_rel(b,j2,b->n);
}

/* ECX = the key of slot s, guarded exact / non-negative int32.
 * cvttsd2si's 32-bit form yields INT_MIN for NaN, +-Inf and anything
 * outside int32, all rejected by the sign test, so keys >= 2^31 bail
 * instead of wrapping.  EDI caches the last validated key across the
 * read/modify/write triple of `Ci[j] = Ci[j] + ...` (EDI is callee-saved,
 * so even the MOD/POW helper calls preserve it). */
static int g_keyint(Gen *g,int s,int iguard){
    Buf *b=&g->b;
    if(g->kcv && g->kcr==s){ emit_mov_rr32(b,R_RCX,R_RDI); return 1; }
    int h=g->a->home[s], xk;
    if(h>=0) xk=h; else { g_ldx(g,0,s); xk=0; }
    emit_cvttsd2si_r32_x(b,R_RCX,xk);
    if(!iguard){                              /* K_INT already proves this */
        emit_cvtsi2sd_x_r32(b,1,R_RCX);
        sse_rr(b,0x66,SSE_COMISD,xk,1);
        BAILCC(CC_P); BAILCC(CC_NE);
    }
    emit_test_ecx(b); BAILCC(CC_S);           /* negative -> interpreter wrap */
    emit_mov_rr32(b,R_RDI,R_RCX);
    g->kcv=1; g->kcr=s;
    return 1;
}

/* dest = base[key] under the hoisted descriptor: only the key check, the
 * bounds check and the element-type check remain per element. */
static int gen_gettable(Gen *g,int A,int B,int C,int kr,int iguard){
    Buf *b=&g->b; Ana *a=g->a;
    if(a->dsc[B]<0) return 0;
    int d=DSCO(g,B);
    if(!g_keyint(g,C,iguard)) return 0;
    emit_cmp_r32_rsp(b,R_RCX,d+8); BAILCC(CC_AE);
    emit_ld_r64_rsp(b,R_RDX,d);
    emit_shl_rcx(b,4);
    emit_ld_r32_sib(b,R_RAX);                 /* EAX = arr[i].t (dead reg) */
    if(kr==K_NUM){
        emit_cmp_eax_imm32(b,LT_NUM); BAILCC(CC_NE);
        emit_movsd_ld_sib8(b,0);
        g_stx(g,A,0);
    } else {
        if(a->dsc[A]<0) return 0;
        emit_cmp_eax_imm32(b,LT_LIST); BAILCC(CC_NE);
        emit_ld_r64_sib8(b,R_RAX);            /* payload at +8, not +0 */
        g_stptr(g,A);
        g_dscbuild(g,A);
    }
    return !b->bad;
}

/* base[key] = num, with the base-is-list / arr / alen / tail-non-nil
 * guards already in the descriptor (i.e. in the loop preheader). */
static int gen_settable(Gen *g,int A,int B,int C,int iguard){
    Buf *b=&g->b; Ana *a=g->a;
    if(a->dsc[A]<0) return 0;
    int d=DSCO(g,A);
    emit_cmp_m32_imm_rsp(b,d+12,0); BAILCC(CC_E);     /* stok */
    if(!g_keyint(g,B,iguard)) return 0;
    emit_cmp_r32_rsp(b,R_RCX,d+8); BAILCC(CC_AE);
    emit_ld_r64_rsp(b,R_RDX,d);
    emit_shl_rcx(b,4);
    emit_mov_m32_imm_sib(b,LT_NUM);
    g_ldx(g,0,C);
    emit_movsd_st_sib8(b,0);
    return !b->bad;
}

/* base[key] <op>= num: read-modify-write under the hoisted descriptor.
 * One index computation serves the read and the write (vs GET+arith+SET);
 * old and new are both guarded numeric, so the op is pure XMM. XMM0 holds
 * old, XMM1 the value; EDI key cache untouched (RCX still the key after). */
static int gen_compeq(Gen *g,int A,int B,int C,int sop,int iguard){
    Buf *b=&g->b; Ana *a=g->a;
    if(a->dsc[A]<0) return 0;
    int d=DSCO(g,A);
    emit_cmp_m32_imm_rsp(b,d+12,0); BAILCC(CC_E);     /* stok */
    if(!g_keyint(g,B,iguard)) return 0;
    emit_cmp_r32_rsp(b,R_RCX,d+8); BAILCC(CC_AE);
    emit_ld_r64_rsp(b,R_RDX,d);
    emit_shl_rcx(b,4);
    emit_ld_r32_sib(b,R_RAX);                 /* EAX = arr[i].t (dead reg) */
    emit_cmp_eax_imm32(b,LT_NUM); BAILCC(CC_NE);
    emit_movsd_ld_sib8(b,0);
    g_ldx(g,1,C);
    sse_rr(b,0xF2,sop,0,1);
    emit_mov_m32_imm_sib(b,LT_NUM);
    emit_movsd_st_sib8(b,0);
    return !b->bad;
}

static void emit_neg_x0(Gen *g){
    emit_mov_r64_imm(&g->b,R_RAX,0x8000000000000000ULL);
    emit_movq_x_r(&g->b,1,R_RAX);
    sse_rr(&g->b,0x66,SSE_XORPD,0,1);
}
/* xmm0 := bool(cc after a comisd), stored to slot */
static void emit_setbool(Gen *g,int cc,int slot){
    emit_setcc_al(&g->b,cc);
    emit_movzx_eax_al(&g->b);
    emit_cvtsi2sd_x_eax(&g->b,0);
    g_stx(g,slot,0);
}
static void emit_stack_probe(Buf *b,int framesz){
    if(framesz<4096) return;
    for(int off=4096;off<framesz;off+=4096) emit_mov_m32_imm_rsp(b,-off,0);
    emit_mov_m32_imm_rsp(b,-(framesz-8),0);
}

static int gen_prologue(Gen *g){
    Buf *b=&g->b;
    Ana *a=g->a;
    emit_push(b,R_RBP); emit_push(b,R_RBX); emit_push(b,R_RSI); emit_push(b,R_RDI);
    emit_push(b,12); emit_push(b,13); emit_push(b,14); emit_push(b,15);
    emit_mov_rr64(b,R_RBX,R_RCX);          /* args */
    emit_mov_rr64(b,R_RSI,8);              /* out  (R8) */
    emit_mov_rr32(b,12,R_RDX);             /* nargs -> r12d */
    emit_stack_probe(b,a->framesz);
    emit_sub_rsp(b,a->framesz);
    for(int x=6;x<16;x++) if(a->savoff[x]>=0) emit_movaps_st_rsp(b,x,a->savoff[x]);

    /* deterministic state: pinned regs and frame homes all +0.0, every
       descriptor zeroed (an unreachable read is then defined, not UB) */
    sse_rr(b,0x66,SSE_XORPD,0,0);
    for(int i=0;i<a->nslots;i++){
        if(a->home[i]>=0) sse_rr(b,0x66,SSE_XORPD,a->home[i],a->home[i]);
        else sse_rsp(b,0xF2,SSE_MOVSD_ST,0,SLOTO(g,i));
    }
    if(a->ndsc>0){
        emit_mov_r64_imm(b,R_RAX,0);
        for(int i=0;i<a->ndsc;i++){
            int d=a->dscoff+DSCSZ*i;
            emit_st_r64_rsp(b,R_RAX,d);
            emit_st_r64_rsp(b,R_RAX,d+8);
            emit_st_r64_rsp(b,R_RAX,d+16);
        }
    }
    /* parameter guards: nargs > i and args[i].t matches pinit */
    for(int i=0;i<a->p->nparams;i++){
        emit_cmp_r32_imm(b,12,i);
        BAILCC(CC_BE);                                     /* nargs <= i */
        if(a->pinit[i]==K_LIST){
            emit_cmp_m32_imm(b,R_RBX,i*VSZ+VOFT,LT_LIST);
            BAILCC(CC_NE);
            emit_ld_rax_mb(b,R_RBX,i*VSZ+VOFU);
            g_stptr(g,i);
            if(a->dsc[i]<0) return 0;
            g_dscbuild(g,i);
        } else {
            emit_cmp_m32_imm(b,R_RBX,i*VSZ+VOFT,LT_NUM);
            BAILCC(CC_NE);
            sse_mb(b,0xF2,SSE_MOVSD_LD,0,R_RBX,i*VSZ+VOFU);
            g_stx(g,i,0);
        }
    }
    return !b->bad;
}
static void gen_epilogue(Gen *g){
    Buf *b=&g->b;
    Ana *a=g->a;
    for(int x=6;x<16;x++) if(a->savoff[x]>=0) emit_movaps_ld_rsp(b,x,a->savoff[x]);
    emit_add_rsp(b,a->framesz);
    emit_pop(b,15); emit_pop(b,14); emit_pop(b,13); emit_pop(b,12);
    emit_pop(b,R_RDI); emit_pop(b,R_RSI); emit_pop(b,R_RBX); emit_pop(b,R_RBP);
    emit_ret(b);
}

static int gen_body(Gen *g){
    Ana *a=g->a;
    Proto *p=a->p;
    Buf *b=&g->b;

    for(int pc=0;pc<p->ncode;pc++){
        g->pcoff[pc]=b->n;
        OpInfo *in=&a->info[pc];
        if(a->istarget[pc]) g->kcv=0;            /* joins: no cached key */
        if(!in->vis) continue;                   /* unreachable: no code */

        uint32_t ins=p->code[pc];
        int op=GET_OP(ins), A=GET_A(ins), B=GET_B(ins), C=GET_C(ins);

        switch(op){
            case OP_MOVE:
                g_kill(g,A);
                if(in->kb==K_NIL) break;         /* nil carries no bits */
                if(in->kb==K_LIST){
                    if(a->dsc[A]<0||a->dsc[B]<0) return 0;
                    g_ldptr(g,B); g_stptr(g,A);
                    emit_ld_r64_rsp(b,R_RAX,DSCO(g,B));      /* copy arr */
                    emit_st_r64_rsp(b,R_RAX,DSCO(g,A));
                    emit_ld_r64_rsp(b,R_RAX,DSCO(g,B)+8);    /* alen+stok */
                    emit_st_r64_rsp(b,R_RAX,DSCO(g,A)+8);
                } else if(a->home[A]>=0||a->home[B]>=0){
                    g_ldx(g,0,B); g_stx(g,A,0);
                } else {
                    emit_ld_r64_rsp(b,R_RAX,SLOTO(g,B));
                    emit_st_r64_rsp(b,R_RAX,SLOTO(g,A));
                }
                break;
            case OP_LOADK:
                g_kill(g,A);
                g_const(g,A,p->k[GET_Bx(ins)].u.n);
                break;
            case OP_LOADNIL:
                g_kill(g,A);
                break;
            case OP_LOADBOOL:
                g_kill(g,A);
                g_const(g,A,B?1.0:0.0);
                break;
            case OP_ADD: case OP_SUB: case OP_MUL: case OP_DIV: {
                int sop = op==OP_ADD?SSE_ADD : op==OP_SUB?SSE_SUB :
                          op==OP_MUL?SSE_MUL : SSE_DIV;
                g_ldx(g,0,B);
                g_op0(g,sop,C);
                g_kill(g,A);
                g_stx(g,A,0);
                break; }
            case OP_MOD: case OP_POW:
                g_ldx(g,0,B);
                g_ldx(g,1,C);
                emit_call_abs(b, op==OP_MOD? (void*)jit_mod : (void*)jit_pow);
                g_kill(g,A);
                g_stx(g,A,0);
                break;
            case OP_UNM:
                g_ldx(g,0,B);
                emit_neg_x0(g);
                g_kill(g,A);
                g_stx(g,A,0);
                break;
            case OP_NOT:
                g_kill(g,A);
                if(in->kb==K_NIL) g_const(g,A,1.0);
                else if(in->kb!=K_BOOL) g_const(g,A,0.0);  /* numbers/lists truthy */
                else {
                    g_ldx(g,0,B);
                    sse_rr(b,0x66,SSE_XORPD,1,1);
                    sse_rr(b,0x66,SSE_COMISD,0,1);
                    emit_setbool(g,CC_E,A);
                }
                break;
            /* COMISD sets CF/ZF like an unsigned compare and sets CF=ZF=PF=1
               when unordered, so LT/LE are emitted with swapped operands and
               A/AE, which yield false for NaN - matching the interpreter. */
            case OP_LT:
                g_ldx(g,0,C); g_cmp0(g,B); g_kill(g,A); emit_setbool(g,CC_A,A);
                break;
            case OP_LE:
                g_ldx(g,0,C); g_cmp0(g,B); g_kill(g,A); emit_setbool(g,CC_AE,A);
                break;
            case OP_GT:
                g_ldx(g,0,B); g_cmp0(g,C); g_kill(g,A); emit_setbool(g,CC_A,A);
                break;
            case OP_GE:
                g_ldx(g,0,B); g_cmp0(g,C); g_kill(g,A); emit_setbool(g,CC_AE,A);
                break;
            case OP_EQ: case OP_NE: {
                int iseq=(op==OP_EQ);
                if(in->cmode==CM_TRUE||in->cmode==CM_FALSE){
                    int t=(in->cmode==CM_TRUE)?1:0;
                    g_kill(g,A);
                    g_const(g,A,(iseq?t:!t)?1.0:0.0);
                    break;
                }
                if(in->cmode==CM_PTR){
                    g_ldptr(g,B);
                    emit_mov_rr64(b,R_RCX,R_RAX);
                    g_ldptr(g,C);
                    emit_cmp_rr64(b,R_RCX,R_RAX);
                    g->kcv=0;                       /* RCX clobbered, EDI kept */
                    g_kill(g,A);
                    emit_setcc_al(b,iseq?CC_E:CC_NE);
                    emit_movzx_eax_al(b);
                    emit_cvtsi2sd_x_eax(b,0);
                    g_stx(g,A,0);
                    break;
                }
                /* val_rawequal on numbers is ==, so NaN must compare unequal:
                   fold the parity flag in explicitly. */
                g_ldx(g,0,B);
                g_cmp0(g,C);
                g_kill(g,A);
                if(iseq){ emit_setcc_al(b,CC_E); emit_setcc_cl(b,CC_NP); emit_and_al_cl(b); }
                else    { emit_setcc_al(b,CC_NE); emit_setcc_cl(b,CC_P);  emit_or_al_cl(b);  }
                emit_movzx_eax_al(b);
                emit_cvtsi2sd_x_eax(b,0);
                g_stx(g,A,0);
                break; }
            case OP_GETTABLE:
                g_kill(g,A);
                if(!gen_gettable(g,A,B,C,in->kr,in->iguard)) return 0;
                break;
            case OP_SETTABLE:
                if(!gen_settable(g,A,B,C,in->iguard)) return 0;
                break;
            case OP_ADDEQ: case OP_SUBEQ: case OP_MULEQ: case OP_DIVEQ: {
                int sop = op==OP_ADDEQ?SSE_ADD : op==OP_SUBEQ?SSE_SUB :
                          op==OP_MULEQ?SSE_MUL : SSE_DIV;
                if(!gen_compeq(g,A,B,C,sop,in->iguard)) return 0;
                break; }
            case OP_CLOSE:
                break;
            case OP_JMP:
                JUMPTO(pc+1+GET_sBx(ins));
                break;
            case OP_JMPIF: case OP_JMPIFNOT: {
                int tgt=pc+1+GET_sBx(ins);
                if(in->ka==K_NIL){ if(op==OP_JMPIFNOT) JUMPTO(tgt); break; }
                if(in->ka!=K_BOOL){ if(op==OP_JMPIF) JUMPTO(tgt); break; }
                g_ldx(g,0,A);
                sse_rr(b,0x66,SSE_XORPD,1,1);
                sse_rr(b,0x66,SSE_COMISD,0,1);
                JCCTO(op==OP_JMPIF?CC_NE:CC_E,tgt);
                break; }
            case OP_FORPREP: {
                /* st=[A] tg=[A+1]; st>0 -> A=-st, A+1=tg ; st<0 -> A=tg-st,
                   A+1=0 ; A+2=st ; then jump.  step 0 / NaN is an interpreter
                   error, so both bail. */
                g_ldx(g,4,A);                                 /* xmm4 = st */
                sse_rr(b,0x66,SSE_XORPD,1,1);
                sse_rr(b,0x66,SSE_COMISD,4,1);
                BAILCC(CC_P);
                BAILCC(CC_E);
                int jpos=emit_jcc(b,CC_A);
                g_ldx(g,2,A+1);                               /* tg */
                sse_rr(b,0xF2,SSE_SUB,2,4);                   /* tg-st */
                g_stx(g,A,2);
                sse_rr(b,0x66,SSE_XORPD,1,1);
                g_stx(g,A+1,1);
                int jend=emit_jmp(b);
                patch_rel(b,jpos,b->n);
                sse_rr(b,0xF2,SSE_MOVSD_LD,0,4);
                emit_neg_x0(g);
                g_stx(g,A,0);                                 /* A = -st */
                patch_rel(b,jend,b->n);
                g_stx(g,A+2,4);
                g_const(g,A+3,0.0);
                for(int k=0;k<4;k++) g_kill(g,A+k);
                JUMPTO(pc+1+GET_sBx(ins));
                break; }
            case OP_FORLOOP: {
                g_ldx(g,0,A);
                g_op0(g,SSE_ADD,A+2);                         /* xmm0 = idx */
                g_ldx(g,2,A+2);
                sse_rr(b,0x66,SSE_XORPD,3,3);
                sse_rr(b,0x66,SSE_COMISD,2,3);                /* comisd st,0 */
                int jdown=emit_jcc(b,CC_BE);                  /* NaN too */
                g_ldx(g,1,A+1);
                sse_rr(b,0x66,SSE_COMISD,1,0);                /* lim vs idx */
                int jt1=emit_jcc(b,CC_A);
                int jo1=emit_jmp(b);
                patch_rel(b,jdown,b->n);
                g_ldx(g,1,A+1);
                sse_rr(b,0x66,SSE_COMISD,0,1);                /* idx vs lim */
                int jt2=emit_jcc(b,CC_AE);
                int jo2=emit_jmp(b);
                patch_rel(b,jt1,b->n);
                patch_rel(b,jt2,b->n);
                g_kill(g,A); g_kill(g,A+3);
                g_stx(g,A,0);
                g_stx(g,A+3,0);
                JUMPTO(pc+1+GET_sBx(ins));
                patch_rel(b,jo1,b->n);
                patch_rel(b,jo2,b->n);
                break; }
            case OP_RETURN: {
                int nr=GET_B(ins)-1;
                if(nr==0){ emit_xor_eax(b); RETJMP(); break; }
                if(in->ka==K_NUM){
                    emit_mov_m32_imm_rsi(b,VOFT,LT_NUM);
                    g_ldx(g,0,A);
                    sse_mb(b,0xF2,SSE_MOVSD_ST,0,R_RSI,VOFU);
                } else if(in->ka==K_BOOL){
                    int h=a->home[A];
                    if(h>=0) emit_cvttsd2si_r32_x(b,R_RAX,h);
                    else emit_cvttsd2si_r32_rsp(b,R_RAX,SLOTO(g,A));
                    emit_mov_m32_eax_rsi(b,VOFU);
                    emit_mov_m32_imm_rsi(b,VOFT,LT_BOOL);
                } else {
                    emit_mov_m32_imm_rsi(b,VOFT,LT_NIL);
                    emit_mov_m32_imm_rsi(b,VOFU,0);
                }
                emit_mov_r32_imm(b,R_RAX,1);
                RETJMP();
                break; }
            default:
                return 0;
        }
        if(b->bad) return 0;
    }
    /* falling off the end of the code behaves like "return no values" */
    emit_xor_eax(b);
    RETJMP();
    return !b->bad;
}

static int gen_finish(Gen *g){
    Buf *b=&g->b;
    int bail=b->n;
    emit_mov_r32_imm(b,R_RAX,0xFFFFFFFFu);      /* -1 */
    int tore=emit_jmp(b);
    int epi=b->n;
    gen_epilogue(g);
    patch_rel(b,tore,epi);
    for(int i=0;i<g->nbail;i++) patch_rel(b,g->bailfix[i],bail);
    for(int i=0;i<g->nret;i++)  patch_rel(b,g->retfix[i],epi);
    for(int i=0;i<g->nfix;i++){
        int topc=g->fix[i].topc;
        if(topc<0||topc>=g->a->p->ncode) return 0;
        int off=g->a->info[topc].vis? g->pcoff[topc] : bail;
        patch_rel(b,g->fix[i].at,off);
    }
    return !b->bad;
}

/* Which operand fields are registers (vs immediates/offsets).
 * AsBx ops (JMP/JMPIF/FORPREP/...) keep the jump offset in B/C, and
 * LOADK/LOADBOOL/LOADNIL/RETURN/CLOSE keep constants there -- only A
 * (plus A+3 for the loop pair) is a register for those. */
static int op_hasB(int op){
    switch(op){
        case OP_MOVE:
        case OP_ADD: case OP_SUB: case OP_MUL: case OP_DIV:
        case OP_MOD: case OP_POW:
        case OP_UNM: case OP_NOT:
        case OP_EQ: case OP_NE: case OP_LT: case OP_LE: case OP_GT: case OP_GE:
        case OP_GETTABLE: case OP_SETTABLE:
        case OP_ADDEQ: case OP_SUBEQ: case OP_MULEQ: case OP_DIVEQ:
            return 1;
        default: return 0;
    }
}
static int op_hasC(int op){
    switch(op){
        case OP_ADD: case OP_SUB: case OP_MUL: case OP_DIV:
        case OP_MOD: case OP_POW:
        case OP_EQ: case OP_NE: case OP_LT: case OP_LE: case OP_GT: case OP_GE:
        case OP_GETTABLE: case OP_SETTABLE:
        case OP_ADDEQ: case OP_SUBEQ: case OP_MULEQ: case OP_DIVEQ:
            return 1;
        default: return 0;
    }
}
static int op_hasA(int op){ return op!=OP_JMP && op!=OP_CLOSE; }

/* Pre-scan: initial kind of each parameter, and result kind of each
 * GETTABLE.  A slot read as a table base (GETTABLE-B / SETTABLE-A) wants
 * K_LIST; a slot used numerically wants K_NUM.  Both at once, or neither
 * decidable -> K_BAD/K_NUM default (analysis or guards sort it out; a
 * runtime mismatch just bails back to the interpreter).  MOVE propagates
 * both ways so `create m = param; m[i]` still types the parameter. */
static void prescan_kinds(Ana *a){
    Proto *p=a->p;
    unsigned char *asbase=a->asbase, *asnum=a->asnum, *askey=a->askey;
    for(int iter=0;iter<16;iter++){
    int changed=0;
    for(int pc=0;pc<p->ncode;pc++){
        uint32_t ins=p->code[pc];
        int op=GET_OP(ins), A=GET_A(ins), B=GET_B(ins), C=GET_C(ins);
        switch(op){
            case OP_GETTABLE:
                if(B<a->nregs&&!asbase[B]){ asbase[B]=1; changed=1; }
                if(C<a->nregs){ if(!asnum[C]){ asnum[C]=1; changed=1; } askey[C]=1; }
                break;
            case OP_SETTABLE:
                if(A<a->nregs&&!asbase[A]){ asbase[A]=1; changed=1; }
                if(B<a->nregs){ if(!asnum[B]){ asnum[B]=1; changed=1; } askey[B]=1; }
                if(C<a->nregs&&!asnum[C]){ asnum[C]=1; changed=1; }
                break;
            case OP_ADDEQ: case OP_SUBEQ: case OP_MULEQ: case OP_DIVEQ:
                if(A<a->nregs&&!asbase[A]){ asbase[A]=1; changed=1; }
                if(B<a->nregs){ if(!asnum[B]){ asnum[B]=1; changed=1; } askey[B]=1; }
                if(C<a->nregs&&!asnum[C]){ asnum[C]=1; changed=1; }
                break;
            case OP_MOVE:
                /* Forward only (B->A). Backward (A->B) is unsound here:
                   the compiler reuses dest slots as table temps later,
                   which would poison a numeric source (e.g. the repeat
                   bound N) into K_LIST and kill the whole function.
                   Aliased table params (`m = A; m[i]`) then default to
                   K_NUM and bail at the guard -- correct, just un-JITed. */
                if(B<a->nregs && A<a->nregs){
                    if(asbase[B]&&!asbase[A]){ asbase[A]=1; changed=1; }
                    if(asnum[B]&&!asnum[A]){ asnum[A]=1; changed=1; }
                }
                break;
            case OP_ADD: case OP_SUB: case OP_MUL: case OP_DIV:
            case OP_MOD: case OP_POW: case OP_UNM:
            case OP_EQ: case OP_NE: case OP_LT: case OP_LE: case OP_GT: case OP_GE:
                if(B<a->nregs&&!asnum[B]){ asnum[B]=1; changed=1; }
                if(op_hasC(op)&&C<a->nregs&&!asnum[C]){ asnum[C]=1; changed=1; }
                break;
            case OP_NOT: case OP_JMPIF: case OP_JMPIFNOT:
                if(A<a->nregs&&!asnum[A]){ asnum[A]=1; changed=1; }
                break;
            case OP_FORPREP: case OP_FORLOOP:
                /* NOTE: do NOT force asnum here. The compiler reuses
                   loop-reg slots for table temps elsewhere; forcing makes
                   them ambiguous and kills the whole function. Genuine
                   list-into-loop-state is still rejected by the
                   FORPREP/FORLOOP numeric checks in ana_region. */
                break;
            default: break;
        }
    }
    if(!changed) break;
    }
    for(int i=0;i<p->nparams && i<a->nregs;i++)
        a->pinit[i]=(asbase[i]&&!asnum[i])?K_LIST:K_NUM;
    for(int pc=0;pc<p->ncode;pc++){
        uint32_t ins=p->code[pc];
        if(GET_OP(ins)!=OP_GETTABLE) continue;
        int A=GET_A(ins);
        if(A>=a->nregs) continue;
        if(asbase[A]&&!asnum[A]) a->info[pc].kr=K_LIST;
        else if(asnum[A]&&!asbase[A]) a->info[pc].kr=K_NUM;
        else if(!asbase[A]&&!asnum[A]) a->info[pc].kr=K_NUM;
        else a->info[pc].kr=K_BAD;
    }
}

/* Loop depth per pc from backward branches, and a use weight per register
 * that is exponential in that depth: the homes go to whatever the inner
 * loops touch most, so a maxstack=23 proto still runs its inner loop out
 * of XMM registers ("mixed-pin") while cold registers stay in the frame. */
static void assign_homes(Ana *a){
    Proto *p=a->p;
    for(int pc=0;pc<p->ncode;pc++){
        int op=GET_OP(p->code[pc]);
        if(op==OP_JMP||op==OP_JMPIF||op==OP_JMPIFNOT||op==OP_FORLOOP){
            int t=pc+1+GET_sBx(p->code[pc]);
            if(t>=0&&t<=pc) for(int i=t;i<=pc;i++) a->depth[i]++;
        }
    }
    long long *w=(long long*)calloc((size_t)a->nregs,sizeof(long long));
    if(!w) return;
    int hasmp=0;
    for(int pc=0;pc<p->ncode;pc++){
        uint32_t ins=p->code[pc];
        int op=GET_OP(ins), A=GET_A(ins), B=GET_B(ins), C=GET_C(ins);
        if(!a->info[pc].vis) continue;
        if(op==OP_MOD||op==OP_POW) hasmp=1;
        int d=a->depth[pc]; if(d>10) d=10;
        long long wt=1LL<<d;
        if(op_hasA(op)&&A<a->nregs) w[A]+=wt;
        if(op_hasB(op)&&B<a->nregs) w[B]+=wt;
        if(op_hasC(op)&&C<a->nregs) w[C]+=wt;
        if(op==OP_FORPREP||op==OP_FORLOOP)
            for(int k=0;k<4;k++) if(A+k<a->nregs) w[A+k]+=wt;
    }
    /* XMM5 is volatile: only hand it out when the proto emits no CALL
       (MOD/POW helpers are the only ones this backend ever calls). */
    int base=hasmp?6:5, navail=16-base;
    for(int x=0;x<16;x++) a->savoff[x]=-1;
    for(int k=0;k<navail;k++){
        int best=-1; long long bw=0;
        for(int r=0;r<a->nregs;r++)
            if(a->home[r]<0 && w[r]>bw){ bw=w[r]; best=r; }
        if(best<0) break;
        a->home[best]=base+k;
        a->npin++;
    }
    for(int r=0;r<a->nregs;r++) if(w[r]>0) a->nused++;
    a->nsave=0;
    for(int x=6;x<16;x++){
        int used=0;
        for(int r=0;r<a->nregs;r++) if(a->home[r]==x) used=1;
        if(used) a->savoff[x]=32+16*(a->nsave++);
    }
    free(w);
}

/* Translate one Proto to machine code.  Returns a malloc'd byte image in
 * *out / *outlen (caller copies it into executable memory or a PE section).
 * pic=1 forbids absolute host addresses so the image can be embedded in a
 * standalone executable.  err[] receives a reason on failure. */
static int translate_proto(Proto *p,int pic,unsigned char **out,int *outlen,
                           char *err,size_t errcap)
{
    Ana a; Gen g;
    int okresult=0;
    memset(&a,0,sizeof a); memset(&g,0,sizeof g);
    if(err&&errcap) err[0]=0;

    if(!p||p->ncode<=0){ if(err) snprintf(err,errcap,"empty prototype"); return 0; }
    if(p->ncode>65536){ if(err) snprintf(err,errcap,"prototype too large (%d instructions)",p->ncode); return 0; }
    if(p->nup>0){ if(err) snprintf(err,errcap,"prototype captures upvalues"); return 0; }
    if(p->np>0){ if(err) snprintf(err,errcap,"prototype defines closures"); return 0; }
    if(p->isvararg){ if(err) snprintf(err,errcap,"vararg prototype"); return 0; }
    if(p->nparams<0){ if(err) snprintf(err,errcap,"bad parameter count"); return 0; }
    if(p->maxstack<1||p->maxstack>LUC_MAXREG){ if(err) snprintf(err,errcap,"bad frame size"); return 0; }

    a.p=p; a.pic=pic;
    a.nregs = p->maxstack+8;
    if(a.nregs>LUC_MAXREG+8) a.nregs=LUC_MAXREG+8;
    if(p->nparams>a.nregs){ if(err) snprintf(err,errcap,"more parameters than registers"); return 0; }
    a.nslots = a.nregs;

    a.info=(OpInfo*)calloc((size_t)p->ncode,sizeof(OpInfo));
    a.istarget=(unsigned char*)calloc((size_t)p->ncode,1);
    a.bs=(unsigned char**)calloc((size_t)p->ncode,sizeof(unsigned char*));
    a.wl=(int*)malloc(sizeof(int)*(size_t)p->ncode);
    a.depth=(int*)calloc((size_t)p->ncode,sizeof(int));
    a.pinit=(unsigned char*)calloc((size_t)(p->nparams>0?p->nparams:1),1);
    a.asbase=(unsigned char*)calloc((size_t)a.nregs,1);
    a.asnum=(unsigned char*)calloc((size_t)a.nregs,1);
    a.askey=(unsigned char*)calloc((size_t)a.nregs,1);
    a.needdsc=(unsigned char*)calloc((size_t)a.nregs,1);
    a.dsc=(int*)malloc(sizeof(int)*(size_t)a.nregs);
    a.home=(int*)malloc(sizeof(int)*(size_t)a.nregs);
    g.pcoff=(int*)calloc((size_t)p->ncode,sizeof(int));
    if(!a.info||!a.istarget||!a.bs||!a.wl||!a.depth||!a.pinit||!a.asbase||
       !a.asnum||!a.askey||!a.needdsc||!a.dsc||!a.home||!g.pcoff){
        if(err) snprintf(err,errcap,"out of memory");
        goto done;
    }
    for(int i=0;i<a.nregs;i++){ a.dsc[i]=-1; a.home[i]=-1; }
    for(int i=0;i<p->nparams;i++) a.pinit[i]=K_NUM;
    prescan_kinds(&a);
    if(!ana_run(&a)){
        if(err) snprintf(err,errcap,"%s",a.err[0]?a.err:"not translatable");
        goto done;
    }
    a.ndsc=0;
    for(int r=0;r<a.nregs;r++) if(a.needdsc[r]) a.dsc[r]=a.ndsc++;
    assign_homes(&a);

    /* frame: shadow | XMM saves | slot homes | list descriptors */
    a.slotoff = 32+16*a.nsave;
    a.dscoff  = a.slotoff+8*a.nslots;
    {   int s=a.dscoff+DSCSZ*a.ndsc;
        s=(s+15)&~15;
        a.framesz=s+8;                 /* keeps RSP 16-aligned at CALLs */
    }

    g.a=&a;
    buf_init(&g.b,65536);                 /* 64 KB, doubles on demand */
    if(g.b.bad){ if(err) snprintf(err,errcap,"out of memory"); goto done; }
    if(!gen_prologue(&g)||!gen_body(&g)||!gen_finish(&g)){
        if(err) snprintf(err,errcap,"code emission failed");
        goto done;
    }
    *out=(unsigned char*)malloc((size_t)g.b.n);
    if(!*out){ if(err) snprintf(err,errcap,"out of memory"); goto done; }
    memcpy(*out,g.b.p,(size_t)g.b.n);
    *outlen=g.b.n;
    okresult=1;
    if(getenv("LUC_JIT_LOG")){
        const char *mode = a.npin==0? "memory-slot"
                         : (a.npin>=a.nused? "pinned-XMM" : "mixed-pin");
        fprintf(stderr,"lucjit: %s mode, maxstack=%d, ncode=%d, bytes=%d\n",
            mode,p->maxstack,p->ncode,g.b.n);
    }
done:
    buf_free(&g.b);
    free(g.fix); free(g.bailfix); free(g.retfix); free(g.pcoff);
    if(a.bs){ for(int i=0;i<p->ncode;i++) free(a.bs[i]); free(a.bs); }
    free(a.info); free(a.istarget); free(a.wl); free(a.depth); free(a.pinit);
    free(a.asbase); free(a.asnum); free(a.askey); free(a.needdsc);
    free(a.dsc); free(a.home);
    return okresult;
}

/* ===================================================================== */
/* 6. JIT: executable memory + per-Proto cache                            */
/* ===================================================================== */

typedef int (*JitFn)(const Value *args,int nargs,Value *out);

typedef struct { Proto *p; void *mem; size_t memsz; JitFn fn; int failed; } JitEnt;

static JitEnt *g_jit=NULL;
static int g_jitn=0, g_jitcap=0;

static JitEnt *jit_find(Proto *p){
    for(int i=0;i<g_jitn;i++) if(g_jit[i].p==p) return &g_jit[i];
    return NULL;
}
static JitEnt *jit_add(Proto *p){
    if(g_jitn==g_jitcap){
        int nc=g_jitcap?g_jitcap*2:64;
        JitEnt *ne=(JitEnt*)realloc(g_jit,sizeof(JitEnt)*(size_t)nc);
        if(!ne) return NULL;
        g_jit=ne; g_jitcap=nc;
    }
    JitEnt *e=&g_jit[g_jitn++];
    e->p=p; e->mem=NULL; e->memsz=0; e->fn=NULL; e->failed=0;
    return e;
}
static void *jit_map(const unsigned char *code,int len,size_t *sz){
    SIZE_T n=(SIZE_T)len;
    void *m=VirtualAlloc(NULL,n,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE);
    if(!m) return NULL;
    memcpy(m,code,(size_t)len);
    DWORD old=0;
    if(!VirtualProtect(m,n,PAGE_EXECUTE_READ,&old)){ VirtualFree(m,0,MEM_RELEASE); return NULL; }
    FlushInstructionCache(GetCurrentProcess(),m,n);
    *sz=(size_t)n;
    return m;
}

/* Compile (once) and return the native entry for p, or NULL. */
static JitFn jit_get(Proto *p){
    JitEnt *e=jit_find(p);
    if(e){ return e->failed? NULL : e->fn; }
    e=jit_add(p);
    if(!e) return NULL;
    unsigned char *img=NULL; int imglen=0; char err[160]; err[0]=0;
    if(!translate_proto(p,0,&img,&imglen,err,sizeof err)){
        e->failed=1;
        if(getenv("LUC_JIT_LOG"))
            fprintf(stderr,"lucjit: fallback (%s), maxstack=%d, ncode=%d\n",
                err[0]?err:"?",p->maxstack,p->ncode);
        return NULL;
    }
    size_t sz=0;
    void *mem=jit_map(img,imglen,&sz);
    free(img);
    if(!mem){ e->failed=1; return NULL; }
    e->mem=mem; e->memsz=sz;
    memcpy(&e->fn,&mem,sizeof e->fn);       /* no function-pointer cast UB */
    return e->fn;
}

void luc_jit_shutdown(void){
    for(int i=0;i<g_jitn;i++) if(g_jit[i].mem) VirtualFree(g_jit[i].mem,0,MEM_RELEASE);
    free(g_jit); g_jit=NULL; g_jitn=g_jitcap=0;
}

/* Hook for vm_call: run L->stack[func] natively if we can.
 * Mirrors vm_call's LT_FUNC contract exactly: results land at
 * L->stack[func..], missing results are padded with nil when nres>=0,
 * L->top is left at func+n, and n is returned.  LUC_JIT_FALLBACK means
 * "nothing happened, interpret it". */
int luc_jit_call(LucState *L,int func,int nargs,int nres){
    if(!L||func<0) return LUC_JIT_FALLBACK;
    Value f=L->stack[func];
    if(f.t!=LT_FUNC) return LUC_JIT_FALLBACK;
    Closure *cl=AS_CL(f);
    JitFn fn=jit_get(cl->p);
    if(!fn) return LUC_JIT_FALLBACK;

    Value out[2];
    out[0]=NIL; out[1]=NIL;
    int n=fn(&L->stack[func+1],nargs,out);
    if(n<0) return LUC_JIT_FALLBACK;       /* guard failed, nothing written */

    int want = nres>=0? nres : n;
    ensure_stack(L,func+want+8);           /* may realloc; native part is done */
    for(int i=0;i<n && i<want;i++) L->stack[func+i]=out[i];
    for(int i=n;i<want;i++) L->stack[func+i]=NIL;
    L->top=func+want;
    return want;
}

/* Run one top-level closure natively.  The main chunk of a real script
 * almost always touches globals or calls functions, so this normally
 * reports LUC_JIT_FALLBACK and the caller interprets it; the win is in
 * luc_jit_call() on numeric leaf functions. */
int luc_jit_run(Closure *cl){
    if(!cl||!cl->p) return LUC_JIT_FALLBACK;
    JitFn fn=jit_get(cl->p);
    if(!fn) return LUC_JIT_FALLBACK;
    Value out[2]; out[0]=NIL; out[1]=NIL;
    int n=fn(NULL,0,out);
    if(n<0) return LUC_JIT_FALLBACK;
    if(n>=1 && out[0].t==LT_NUM) return (int)out[0].u.n;
    return 0;
}

/* ===================================================================== */
/* 7. PE32+ writer                                                        */
/* ===================================================================== */

#define PE_IMAGEBASE   0x140000000ULL
#define PE_SECALIGN    0x1000
#define PE_FILEALIGN   0x200

static unsigned pe_align(unsigned v,unsigned a){ return (v+a-1)&~(a-1); }

/* imported kernel32 entries, in IAT order */
static const char *const PE_IMPORTS[3]={ "ExitProcess", "WriteFile", "GetStdHandle" };
#define IMP_EXITPROCESS  0
#define IMP_WRITEFILE    1
#define IMP_GETSTDHANDLE 2

/* .rdata layout (offsets from the section's RVA) */
#define RD_DESC   0                  /* 2 x IMAGE_IMPORT_DESCRIPTOR (40) */
#define RD_ILT    40                 /* 4 qwords */
#define RD_IAT    72                 /* 4 qwords */
#define RD_NAMES  104                /* hint/name blobs, then "kernel32.dll" */

static void pe_build_rdata(Buf *rd,unsigned rva,unsigned *iat_rva){
    unsigned nameoff[3], dlloff;
    Buf nb; buf_init(&nb,256);
    for(int i=0;i<3;i++){
        if(nb.n&1) e1(&nb,0);
        nameoff[i]=RD_NAMES+(unsigned)nb.n;
        e2(&nb,0);
        eblob(&nb,PE_IMPORTS[i],(int)strlen(PE_IMPORTS[i])+1);
    }
    if(nb.n&1) e1(&nb,0);
    dlloff=RD_NAMES+(unsigned)nb.n;
    eblob(&nb,"kernel32.dll",13);

    /* descriptor[0] */
    e4(rd,rva+RD_ILT); e4(rd,0); e4(rd,0); e4(rd,rva+dlloff); e4(rd,rva+RD_IAT);
    /* descriptor[1] = terminator */
    for(int i=0;i<5;i++) e4(rd,0);
    /* ILT then IAT: both point at the hint/name entries */
    for(int t=0;t<2;t++){
        for(int i=0;i<3;i++) e8(rd,(unsigned long long)(rva+nameoff[i]));
        e8(rd,0);
    }
    eblob(rd,nb.p,nb.n);
    buf_free(&nb);
    *iat_rva=rva+RD_IAT;
}

/* call [rip + (iat slot)] */
static void emit_call_import(Buf *b,unsigned code_rva,unsigned iat_rva,int idx){
    e1(b,0xFF); e1(b,0x15);
    unsigned here=code_rva+(unsigned)b->n+4;
    e4(b,(unsigned)((int)(iat_rva+8u*(unsigned)idx)-(int)here));
}
static void emit_lea_rip(Buf *b,int reg,unsigned code_rva,unsigned target_rva){
    int rex=0x48|((reg>=8)?4:0);
    e1(b,rex); e1(b,0x8D); e1(b,0x05|((reg&7)<<3));
    unsigned here=code_rva+(unsigned)b->n+4;
    e4(b,(unsigned)((int)target_rva-(int)here));
}

/* Write a standalone console PE32+ whose .text is `native` (a translated
 * proto in PIC form) plus an entry stub that runs it and exits with the
 * program's numeric result. */
static int pe_write_exe(const char *path,const unsigned char *native,int nativelen,
                        char *err,size_t errcap)
{
    static const char msg[]="luc: native image bailed out (value out of the numeric subset)\r\n";
    Buf text, rdata, data;
    int rc=0;
    unsigned text_rva=PE_SECALIGN, rdata_rva, data_rva, iat_rva=0;
    unsigned textsz, rdatasz, datasz;
    unsigned char *dos=NULL;
    FILE *f=NULL;

    buf_init(&text,4096); buf_init(&rdata,1024); buf_init(&data,1024);
    if(text.bad||rdata.bad||data.bad){ snprintf(err,errcap,"out of memory"); goto out; }

    /* --- sizes must all be known before a single byte is written --- */
    /* stub size is fixed by construction; emit twice: once to measure with
       provisional RVAs, once for real.  Both passes emit identical lengths
       because every field is a fixed-width rel32/imm32. */
    for(int pass=0;pass<2;pass++){
        text.n=0; rdata.n=0; data.n=0;

        /* .data: out Value, bytes-written scratch, message */
        unsigned d_out=0, d_nw=16, d_msg=24;
        datasz = d_msg + (unsigned)sizeof msg;
        (void)d_out; (void)d_nw;

        if(pass==1){
            for(unsigned i=0;i<datasz;i++) e1(&data,0);
            memcpy(data.p+d_msg,msg,sizeof msg);
        }

        /* entry stub goes first in .text, then the native image */
        unsigned stub_rva = text_rva;
        unsigned nat_rva;
        /* stub length is deterministic: measure it in pass 0 */
        static int stublen=0;
        if(pass==0) nat_rva = stub_rva+256; else nat_rva = stub_rva+(unsigned)stublen;

        rdata_rva = pe_align(text_rva + (unsigned)(pass==0? 256+nativelen
                                                          : stublen+nativelen), PE_SECALIGN);
        {   Buf tmp; buf_init(&tmp,1024);
            if(tmp.bad){ snprintf(err,errcap,"out of memory"); goto out; }
            unsigned dummy=0;
            pe_build_rdata(&tmp,rdata_rva,&dummy);
            rdatasz=(unsigned)tmp.n;
            buf_free(&tmp);
        }
        data_rva = pe_align(rdata_rva+rdatasz,PE_SECALIGN);
        if(pass==1) pe_build_rdata(&rdata,rdata_rva,&iat_rva);
        else { Buf t2; buf_init(&t2,1024); pe_build_rdata(&t2,rdata_rva,&iat_rva); buf_free(&t2); }

        /* ---- entry stub ---- */
        emit_sub_rsp(&text,0x38);                       /* 56 == 8 (mod 16) */
        emit_xor_eax(&text);
        emit_mov_rr64(&text,R_RCX,R_RAX);               /* args = NULL */
        emit_mov_rr32(&text,R_RDX,R_RAX);               /* nargs = 0   */
        emit_lea_rip(&text,8,text_rva,data_rva+0);      /* r8 = &out   */
        e1(&text,0xE8);                                 /* call native */
        { unsigned here=text_rva+(unsigned)text.n+4;
          e4(&text,(unsigned)((int)nat_rva-(int)here)); }
        e1(&text,0x85); mrm_rr(&text,R_RAX,R_RAX);      /* test eax,eax */
        int jfail=emit_jcc(&text,CC_S);
        /* out[0].t == LT_NUM ? */
        e1(&text,0x81); e1(&text,0x3D);
        { unsigned here=text_rva+(unsigned)text.n+8;
          e4(&text,(unsigned)((int)(data_rva+0+(unsigned)VOFT)-(int)here)); }
        e4(&text,(unsigned)LT_NUM);
        int jnotnum=emit_jcc(&text,CC_NE);
        e1(&text,0xF2); e1(&text,0x0F); e1(&text,0x2C); e1(&text,0x0D);  /* cvttsd2si ecx,[rip+..] */
        { unsigned here=text_rva+(unsigned)text.n+4;
          e4(&text,(unsigned)((int)(data_rva+0+(unsigned)VOFU)-(int)here)); }
        int jexit=emit_jmp(&text);
        patch_rel(&text,jnotnum,text.n);
        e1(&text,0x31); mrm_rr(&text,R_RCX,R_RCX);      /* xor ecx,ecx */
        patch_rel(&text,jexit,text.n);
        emit_call_import(&text,text_rva,iat_rva,IMP_EXITPROCESS);
        /* failure path: write the message to stderr, exit 1 */
        patch_rel(&text,jfail,text.n);
        emit_mov_r32_imm(&text,R_RCX,0xFFFFFFF4u);      /* STD_ERROR_HANDLE (-12) */
        emit_call_import(&text,text_rva,iat_rva,IMP_GETSTDHANDLE);
        emit_mov_rr64(&text,R_RCX,R_RAX);
        emit_lea_rip(&text,R_RDX,text_rva,data_rva+24);
        emit_mov_r32_imm(&text,8,(unsigned)(sizeof msg-1));
        emit_lea_rip(&text,9,text_rva,data_rva+16);
        e1(&text,0x48); e1(&text,0xC7); mrm_rsp(&text,0,0x20); e4(&text,0); /* 5th arg = NULL */
        emit_call_import(&text,text_rva,iat_rva,IMP_WRITEFILE);
        emit_mov_r32_imm(&text,R_RCX,1);
        emit_call_import(&text,text_rva,iat_rva,IMP_EXITPROCESS);
        e1(&text,0xCC);                                  /* int3 backstop */

        if(pass==0){ stublen=text.n; continue; }

        eblob(&text,native,nativelen);
        textsz=(unsigned)text.n;
        if(text.bad||rdata.bad||data.bad){ snprintf(err,errcap,"out of memory"); goto out; }

        /* ---- headers ---- */
        unsigned hdrsz   = pe_align(0x80+4+20+240+3*40,PE_FILEALIGN);
        unsigned text_ptr  = hdrsz;
        unsigned text_raw  = pe_align(textsz,PE_FILEALIGN);
        unsigned rdata_ptr = text_ptr+text_raw;
        unsigned rdata_raw = pe_align(rdatasz,PE_FILEALIGN);
        unsigned data_ptr  = rdata_ptr+rdata_raw;
        unsigned data_raw  = pe_align(datasz,PE_FILEALIGN);
        unsigned imgsz     = data_rva+pe_align(datasz,PE_SECALIGN);

        Buf h; buf_init(&h,hdrsz+16);
        if(h.bad){ snprintf(err,errcap,"out of memory"); goto out; }
        /* DOS header + stub */
        e2(&h,0x5A4D); for(int i=0;i<29;i++) e2(&h,0);
        h.n=0x3C; e4(&h,0x80);
        h.n=0x40;
        {   static const unsigned char stub[]={
                0x0E,0x1F,0xBA,0x0E,0x00,0xB4,0x09,0xCD,0x21,0xB8,0x01,0x4C,0xCD,0x21,
                'T','h','i','s',' ','p','r','o','g','r','a','m',' ','c','a','n','n','o','t',
                ' ','b','e',' ','r','u','n',' ','i','n',' ','D','O','S',' ','m','o','d','e',
                '.','\r','\r','\n','$',0,0,0,0,0,0,0 };
            eblob(&h,stub,(int)sizeof stub);
        }
        while(h.n<0x80) e1(&h,0);
        /* PE signature + COFF header */
        e4(&h,0x00004550);
        e2(&h,0x8664);                 /* Machine = AMD64 */
        e2(&h,3);                      /* NumberOfSections */
        e4(&h,0); e4(&h,0); e4(&h,0);  /* timestamp, symtab, nsyms */
        e2(&h,240);                    /* SizeOfOptionalHeader */
        e2(&h,0x0022);                 /* EXECUTABLE_IMAGE | LARGE_ADDRESS_AWARE */
        /* optional header */
        e2(&h,0x020B);                 /* PE32+ */
        e1(&h,14); e1(&h,0);
        e4(&h,text_raw); e4(&h,rdata_raw+data_raw); e4(&h,0);
        e4(&h,text_rva);               /* AddressOfEntryPoint = stub */
        e4(&h,text_rva);               /* BaseOfCode */
        e8(&h,PE_IMAGEBASE);
        e4(&h,PE_SECALIGN); e4(&h,PE_FILEALIGN);
        e2(&h,6); e2(&h,0);            /* OS version */
        e2(&h,0); e2(&h,0);            /* image version */
        e2(&h,6); e2(&h,0);            /* subsystem version */
        e4(&h,0);
        e4(&h,imgsz); e4(&h,hdrsz);
        e4(&h,0);                      /* checksum */
        e2(&h,3);                      /* console subsystem */
        e2(&h,0x8160);                 /* dynamic base/NX/terminal-server aware */
        e8(&h,0x100000); e8(&h,0x1000);/* stack reserve/commit */
        e8(&h,0x100000); e8(&h,0x1000);/* heap  reserve/commit */
        e4(&h,0);
        e4(&h,16);                     /* NumberOfRvaAndSizes */
        for(int i=0;i<16;i++){
            if(i==1){ e4(&h,rdata_rva+RD_DESC); e4(&h,40); }
            else if(i==12){ e4(&h,iat_rva); e4(&h,32); }
            else { e4(&h,0); e4(&h,0); }
        }
        /* sections */
        struct { const char *nm; unsigned vs,va,rs,pr,ch; } sec[3]={
            {".text", textsz, text_rva, text_raw, text_ptr, 0x60000020u},
            {".rdata",rdatasz,rdata_rva,rdata_raw,rdata_ptr,0x40000040u},
            {".data", datasz, data_rva, data_raw, data_ptr, 0xC0000040u}
        };
        for(int i=0;i<3;i++){
            char nm[8]; memset(nm,0,8);
            memcpy(nm,sec[i].nm,strlen(sec[i].nm));
            eblob(&h,nm,8);
            e4(&h,sec[i].vs); e4(&h,sec[i].va);
            e4(&h,sec[i].rs); e4(&h,sec[i].pr);
            e4(&h,0); e4(&h,0); e2(&h,0); e2(&h,0);
            e4(&h,sec[i].ch);
        }
        while(h.n<(int)hdrsz) e1(&h,0);
        if(h.bad){ buf_free(&h); snprintf(err,errcap,"out of memory"); goto out; }

        f=fopen(path,"wb");
        if(!f){ buf_free(&h); snprintf(err,errcap,"cannot create '%s'",path); goto out; }
        fwrite(h.p,1,(size_t)h.n,f);
        buf_free(&h);
        fwrite(text.p,1,(size_t)textsz,f);
        for(unsigned i=textsz;i<text_raw;i++) fputc(0,f);
        fwrite(rdata.p,1,(size_t)rdatasz,f);
        for(unsigned i=rdatasz;i<rdata_raw;i++) fputc(0,f);
        fwrite(data.p,1,(size_t)datasz,f);
        for(unsigned i=datasz;i<data_raw;i++) fputc(0,f);
        if(ferror(f)){ snprintf(err,errcap,"write error on '%s'",path); fclose(f); f=NULL; goto out; }
        fclose(f); f=NULL;
        rc=1;
    }
out:
    if(f) fclose(f);
    free(dos);
    buf_free(&text); buf_free(&rdata); buf_free(&data);
    return rc;
}

/* ===================================================================== */
/* 8. AOT                                                                 */
/* ===================================================================== */

#define AOT_MAGIC "LUCAOT1"          /* 8 bytes including the NUL */

/* Self-contained executable without a linker: clone the already-linked
 * host image and append the script.  Works for every program, because the
 * clone contains the whole runtime. */
static int aot_selfclone(const char *outpath,const char *src,int srclen,char *err,size_t errcap){
    char self[MAX_PATH];
    DWORD n=GetModuleFileNameA(NULL,self,MAX_PATH);
    if(n==0||n>=MAX_PATH){ snprintf(err,errcap,"cannot locate the running luc executable"); return 0; }
    FILE *in=fopen(self,"rb");
    if(!in){ snprintf(err,errcap,"cannot read '%s'",self); return 0; }
    if(fseek(in,0,SEEK_END)!=0){ fclose(in); snprintf(err,errcap,"cannot size '%s'",self); return 0; }
    long total=ftell(in);
    if(total<=0){ fclose(in); snprintf(err,errcap,"cannot size '%s'",self); return 0; }

    /* if the host itself carries a payload, drop it so payloads never nest */
    long keep=total;
    if(total>12){
        char tr[12];
        if(fseek(in,total-12,SEEK_SET)==0 && fread(tr,1,12,in)==12 &&
           memcmp(tr+4,AOT_MAGIC,8)==0){
            unsigned pl; memcpy(&pl,tr,4);
            if((long)pl+12<=total) keep=total-12-(long)pl;
        }
    }
    unsigned char *img=(unsigned char*)malloc((size_t)keep);
    if(!img){ fclose(in); snprintf(err,errcap,"out of memory"); return 0; }
    if(fseek(in,0,SEEK_SET)!=0 || fread(img,1,(size_t)keep,in)!=(size_t)keep){
        free(img); fclose(in); snprintf(err,errcap,"cannot read '%s'",self); return 0;
    }
    fclose(in);

    FILE *out=fopen(outpath,"wb");
    if(!out){ free(img); snprintf(err,errcap,"cannot create '%s'",outpath); return 0; }
    fwrite(img,1,(size_t)keep,out);
    free(img);
    fwrite(src,1,(size_t)srclen,out);
    unsigned pl=(unsigned)srclen;
    fwrite(&pl,1,4,out);
    fwrite(AOT_MAGIC,1,8,out);
    if(ferror(out)){ fclose(out); snprintf(err,errcap,"write error on '%s'",outpath); return 0; }
    fclose(out);
    return 1;
}

/* Recover an appended script from our own image (called at startup). */
int luc_aot_embedded(char **psrc,int *plen){
    char self[MAX_PATH];
    DWORD n=GetModuleFileNameA(NULL,self,MAX_PATH);
    if(n==0||n>=MAX_PATH) return 0;
    FILE *f=fopen(self,"rb");
    if(!f) return 0;
    int ok=0;
    if(fseek(f,0,SEEK_END)==0){
        long total=ftell(f);
        char tr[12];
        if(total>12 && fseek(f,total-12,SEEK_SET)==0 && fread(tr,1,12,f)==12 &&
           memcmp(tr+4,AOT_MAGIC,8)==0){
            unsigned pl; memcpy(&pl,tr,4);
            if(pl>0 && (long)pl+12<=total && fseek(f,total-12-(long)pl,SEEK_SET)==0){
                char *s=(char*)malloc((size_t)pl+1);
                if(s){
                    if(fread(s,1,(size_t)pl,f)==(size_t)pl){
                        s[pl]=0; *psrc=s; *plen=(int)pl; ok=1;
                    } else free(s);
                }
            }
        }
    }
    fclose(f);
    return ok;
}

/* Compile src under a protected call so a syntax error is reported instead
 * of longjmp-ing out of the builder. */
static Closure *aot_compile(const char *src,int srclen,const char *name,char *err,size_t errcap){
    ErrJmp ej; ej.prev=V.errjmp; V.errjmp=&ej;
    Closure *cl=NULL;
    if(setjmp(ej.jb)==0) cl=luc_compile(src,srclen,name);
    else {
        Str *m=tostr(V.errval);
        snprintf(err,errcap,"%s",m?m->s:"compile error");
        cl=NULL;
    }
    V.errjmp=ej.prev;
    return cl;
}

int luc_aot_build(const char *src,int srclen,const char *outpath){
    char err[512]; err[0]=0;
    if(!src||srclen<=0||!outpath||!*outpath){
        fprintf(stderr,"luc build: nothing to build\n");
        return 1;
    }
    Closure *cl=aot_compile(src,srclen,outpath,err,sizeof err);
    if(!cl){ fprintf(stderr,"luc build: %s\n",err); return 1; }

    const char *want_pe=getenv("LUC_AOT_PE");
    if(want_pe && *want_pe=='1'){
        unsigned char *img=NULL; int imglen=0; char terr[160];
        if(!translate_proto(cl->p,1,&img,&imglen,terr,sizeof terr)){
            fprintf(stderr,
                "luc build: LUC_AOT_PE=1 emits a runtime-free executable, which only\n"
                "           supports the native numeric subset; this program needs the\n"
                "           runtime (%s).\n"
                "           Unset LUC_AOT_PE to build a self-contained executable.\n",terr);
            return 1;
        }
        int ok=pe_write_exe(outpath,img,imglen,err,sizeof err);
        free(img);
        if(!ok){ fprintf(stderr,"luc build: %s\n",err); return 1; }
        printf("luc build: wrote %s (native, runtime-free)\n",outpath);
        return 0;
    }
    if(!aot_selfclone(outpath,src,srclen,err,sizeof err)){
        fprintf(stderr,"luc build: %s\n",err);
        return 1;
    }
    printf("luc build: wrote %s (self-contained)\n",outpath);
    return 0;
}

#else /* !_WIN32 : keep the tree building; everything asks to be interpreted */

int  luc_jit_run(Closure *cl){ (void)cl; return LUC_JIT_FALLBACK; }
int  luc_jit_call(LucState *L,int func,int nargs,int nres){
    (void)L;(void)func;(void)nargs;(void)nres; return LUC_JIT_FALLBACK;
}
void luc_jit_shutdown(void){}
int  luc_aot_embedded(char **psrc,int *plen){ (void)psrc;(void)plen; return 0; }
int  luc_aot_build(const char *src,int srclen,const char *outpath){
    (void)src;(void)srclen;(void)outpath;
    fprintf(stderr,"luc build: AOT output is implemented for Windows x64 only\n");
    return 1;
}

#endif
