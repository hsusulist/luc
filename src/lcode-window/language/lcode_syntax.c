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
