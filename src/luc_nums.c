/* Extra list, search, string and time APIs exposed by the nums system module. */

static Table *nums_list_api;
static Table *nums_search_api;
static Table *nums_time_api;
static Table *nums_text_api;
static Table *nums_matrix_api;
static Table *nums_sets_api;
static Table *nums_finance_api;

static Table *nums_list_arg(LucState *L,int base,int nargs,int i,const char *fn){
    Value v=AR(i);
    if(v.t!=LT_LIST) luc_error("%s: expected a list (got %s)",fn,type_name(v));
    return AS_TAB(v);
}

static int nums_int_arg(LucState *L,int base,int nargs,int i,const char *fn){
    return checkint(L,base,nargs,i,fn);
}

static int nums_nonnegative(int n,const char *fn){
    if(n<0){ luc_error("%s: size must be >= 0",fn); return 0; }
    return n;
}

static Table *nums_list_copy(Table *src){
    Table *dst=tab_new(1);
    for(int i=0;i<src->alen;i++) list_push(dst,src->arr[i]);
    return dst;
}

static Table *nums_list_from_value(Value v,const char *fn){
    Table *out=tab_new(1);
    if(v.t==LT_LIST){
        Table *src=AS_TAB(v);
        for(int i=0;i<src->alen;i++) list_push(out,src->arr[i]);
    }else if(v.t==LT_STR){
        Str *s=AS_STR(v);
        for(int i=0;i<s->len;i++) list_push(out,strv(s->s+i,1));
    }else if(v.t==LT_TABLE){
        Value key=NIL,kval,val;
        while(tab_next(AS_TAB(v),key,&kval,&val)){key=kval;list_push(out,val);}
    }else if(v.t!=LT_NIL) list_push(out,v);
    else if(!strcmp(fn,"fromIterator")) luc_error("list.fromIterator: expected an iterable value");
    return out;
}

static void nums_flatten_into(Table *src,Table *dst,int depth){
    if(depth>128) luc_error("list.flatten: nesting exceeds 128 levels");
    for(int i=0;i<src->alen;i++){
        Value v=src->arr[i];
        if(v.t==LT_LIST) nums_flatten_into(AS_TAB(v),dst,depth+1);
        else list_push(dst,v);
    }
}

static int nums_shape_value(Value value,int *shape,int depth){
    if(value.t!=LT_LIST) return depth;
    if(depth>=16) luc_error("list: nesting exceeds 16 dimensions");
    Table *t=AS_TAB(value); shape[depth]=t->alen;
    if(t->alen==0) return depth+1;
    int rank=nums_shape_value(t->arr[0],shape,depth+1);
    for(int i=1;i<t->alen;i++){
        int other[16]={0};
        int other_rank=nums_shape_value(t->arr[i],other,depth+1);
        if(other_rank!=rank) luc_error("list: nested list must be rectangular");
        for(int d=depth+1;d<rank;d++)
            if(other[d]!=shape[d]) luc_error("list: nested list must be rectangular");
    }
    return rank;
}

static int nums_list_product(const int *dims,int ndims){
    int total=1;
    for(int i=0;i<ndims;i++){
        if(dims[i]<0) luc_error("list.reshape: dimensions must be >= 0");
        if(dims[i] && total>10000000/dims[i])
            luc_error("list.reshape: result is too large");
        total*=dims[i];
    }
    return total;
}

static Table *nums_reshape_level(Table *flat,const int *dims,int ndims,int level,int *at){
    Table *out=tab_new(1);
    int n=dims[level];
    for(int i=0;i<n;i++){
        if(level+1==ndims) list_push(out,flat->arr[(*at)++]);
        else list_push(out,mkobj(LT_LIST,nums_reshape_level(flat,dims,ndims,level+1,at)));
    }
    return out;
}

static int nums_value_less(Value a,Value b){
    if(a.t==LT_NUM&&b.t==LT_NUM) return a.u.n<b.u.n;
    if(a.t==LT_STR&&b.t==LT_STR) return vm_lessthan(a,b,0);
    if(a.t==LT_BOOL&&b.t==LT_BOOL) return !a.u.b&&b.u.b;
    return vm_lessthan(a,b,0);
}

static void nums_sort_table(Table *t,int descending){
    for(int i=1;i<t->alen;i++){
        Value key=t->arr[i]; int j=i-1;
        while(j>=0&&(descending?nums_value_less(key,t->arr[j]):nums_value_less(t->arr[j],key))){
            t->arr[j+1]=t->arr[j]; j--;
        }
        t->arr[j+1]=key;
    }
}

static void nums_list_register(Table *t,const char *name,CFn fn){
    char lower[64],alllower[64],upper[64];
    size_t n=strlen(name);
    if(n>=sizeof lower){ luc_error("nums.list: function name too long"); return; }
    memcpy(lower,name,n+1);
    memcpy(alllower,name,n+1);
    memcpy(upper,name,n+1);
    lower[0]=(char)tolower((unsigned char)lower[0]);
    upper[0]=(char)toupper((unsigned char)upper[0]);
    for(size_t i=0;i<n;i++) alllower[i]=(char)tolower((unsigned char)alllower[i]);
    CFunc *cf=cfunc_new(fn,name,0);
    tab_set(t,cstrv(name),mkobj(LT_CFUNC,cf));
    if(strcmp(name,lower)){ cf=cfunc_new(fn,name,0); tab_set(t,cstrv(lower),mkobj(LT_CFUNC,cf)); }
    if(strcmp(name,upper)&&strcmp(lower,upper)){ cf=cfunc_new(fn,name,0); tab_set(t,cstrv(upper),mkobj(LT_CFUNC,cf)); }
    if(strcmp(name,alllower)&&strcmp(lower,alllower)){
        cf=cfunc_new(fn,name,0); tab_set(t,cstrv(alllower),mkobj(LT_CFUNC,cf));
    }
}

static int nums_write_value(FILE *f,Value v){
    if(v.t==LT_STR){
        static const char hex[]="0123456789abcdef"; Str *s=AS_STR(v);
        if(fputc('s',f)==EOF) return 0;
        for(int i=0;i<s->len;i++){
            unsigned char c=(unsigned char)s->s[i];
            if(fputc(hex[c>>4],f)==EOF||fputc(hex[c&15],f)==EOF) return 0;
        }
        return fputc('\n',f)!=EOF;
    }
    if(v.t==LT_NUM){ Str *s=tostr(v); return fprintf(f,"n%.*s\n",s->len,s->s)>=0; }
    if(v.t==LT_BOOL) return fprintf(f,"b%d\n",v.u.b)!=EOF;
    if(v.t==LT_NIL) return fputs("z\n",f)!=EOF;
    if(v.t==LT_LIST){
        Table *t=AS_TAB(v);
        if(fputs("l\n",f)==EOF) return 0;
        for(int i=0;i<t->alen;i++) if(!nums_write_value(f,t->arr[i])) return 0;
        return fputs("e\n",f)!=EOF;
    }
    return 0;
}

static int nums_read_value(FILE *f,Value *out,int depth){
    if(depth>128){ luc_error("list.readList: nesting exceeds 128 levels"); return 0; }
    char *line=NULL; size_t cap=0; long got;
#if defined(_WIN32)
    char buf[65536];
    if(!fgets(buf,sizeof buf,f)) return 0;
    got=(long)strlen(buf);
    line=(char*)malloc((size_t)got+1);
    if(!line) luc_error("list.readList: out of memory");
    memcpy(line,buf,(size_t)got+1);
#else
    got=getline(&line,&cap,f);
    if(got<0){ free(line); return 0; }
#endif
    while(got>0&&(line[got-1]=='\n'||line[got-1]=='\r')) line[--got]=0;
    if(got==0){ free(line); luc_error("list.readList: malformed value"); return 0; }
    if(line[0]=='e'&&got==1){ free(line); return -1; }
    if(line[0]=='s'){
        int n=(int)got-1;
        if(n%2){free(line);luc_error("list.readList: malformed string encoding");return 0;}
        char *decoded=(char*)malloc((size_t)(n/2?n/2:1));
        if(!decoded){free(line);luc_error("list.readList: out of memory");return 0;}
        for(int i=0;i<n;i+=2){
            int hi=hexval((unsigned char)line[i+1]),lo=hexval((unsigned char)line[i+2]);
            if(hi<0||lo<0){free(decoded);free(line);luc_error("list.readList: malformed string encoding");return 0;}
            decoded[i/2]=(char)((hi<<4)|lo);
        }
        *out=strv(decoded,n/2);free(decoded);
    }
    else if(line[0]=='n'){
        double d;
        if(!str2num(line+1,(int)got-1,&d)){ free(line); luc_error("list.readList: invalid number"); return 0; }
        *out=mknum(d);
    }else if(line[0]=='b'&&(got==2&&(line[1]=='0'||line[1]=='1'))) *out=mkbool(line[1]=='1');
    else if(line[0]=='z'&&got==1) *out=NIL;
    else if(line[0]=='l'&&got==1){
        Table *list=tab_new(1); Value v;
        free(line);
        int state;
        while((state=nums_read_value(f,&v,depth+1))>0){
            list_push(list,v);
        }
        if(state==0){ luc_error("list.readList: unterminated nested list"); return 0; }
        *out=mkobj(LT_LIST,list); return 1;
    }else { free(line); luc_error("list.readList: malformed value"); return 0; }
    free(line); return 1;
}

LFN(f_nums_file_api){
    const char *fn=self->name;
    if(!strcmp(fn,"readText")||!strcmp(fn,"readBinary")){
        Str *path=checkstr(L,base,nargs,0,fn); FILE *f=fopen(path->s,"rb");
        if(!f) luc_error("%s: cannot open file",fn);
        Str *data=read_all_str(f); int failed=ferror(f); fclose(f);
        if(failed) luc_error("%s: read failed",fn);
        RET(0,mkobj(LT_STR,data)); return 1;
    }
    if(!strcmp(fn,"writeText")||!strcmp(fn,"writeBinary")){
        Str *path=checkstr(L,base,nargs,0,fn); Str *data=checkstr(L,base,nargs,1,fn);
        FILE *f=fopen(path->s,"wb"); if(!f) luc_error("%s: cannot open file",fn);
        int ok=fwrite(data->s,1,(size_t)data->len,f)==(size_t)data->len;
        if(fclose(f)!=0) ok=0;
        if(!ok) luc_error("%s: write failed",fn);
        RET(0,AR(1)); return 1;
    }
    if(!strcmp(fn,"readList")||!strcmp(fn,"readData")){
        Str *path=checkstr(L,base,nargs,0,fn); FILE *f=fopen(path->s,"rb");
        if(!f) luc_error("%s: cannot open file",fn);
        Table *out=tab_new(1); Value v;
        int state;
        while((state=nums_read_value(f,&v,0))>0) list_push(out,v);
        if(state<0){fclose(f);luc_error("%s: unexpected list terminator",fn);}
        int failed=ferror(f); fclose(f);
        if(failed) luc_error("%s: read failed",fn);
        RET(0,mkobj(LT_LIST,out)); return 1;
    }
    if(!strcmp(fn,"writeCompressed")){
        Str *path=checkstr(L,base,nargs,0,fn); FILE *raw=tmpfile();
        if(!raw) luc_error("list.writeCompressed: cannot create temporary data");
        int ok=1;
        for(int a=1;a<nargs&&ok;a++){
            Table *t=nums_list_arg(L,base,nargs,a,fn);
            for(int i=0;i<t->alen&&ok;i++) ok=nums_write_value(raw,t->arr[i]);
        }
        if(!ok||fflush(raw)!=0||fseek(raw,0,SEEK_END)!=0){fclose(raw);luc_error("list.writeCompressed: staging failed");}
        long length=ftell(raw);
        if(length<0||fseek(raw,0,SEEK_SET)!=0){fclose(raw);luc_error("list.writeCompressed: staging failed");}
        unsigned char *src=(unsigned char*)malloc((size_t)(length?length:1));
        if(!src){fclose(raw);luc_error("list.writeCompressed: out of memory");}
        if(fread(src,1,(size_t)length,raw)!=(size_t)length){free(src);fclose(raw);luc_error("list.writeCompressed: staging read failed");}
        fclose(raw);
        FILE *dst=fopen(path->s,"wb");
        if(!dst){free(src);luc_error("list.writeCompressed: cannot open file");}
        ok=fwrite("LUC-RLE-1\n",1,10,dst)==10;
        for(long i=0;i<length&&ok;){
            long run=1;
            while(i+run<length&&src[i+run]==src[i]&&run<255) run++;
            unsigned char pair[2]={(unsigned char)run,src[i]};
            ok=fwrite(pair,1,2,dst)==2; i+=run;
        }
        free(src); if(fclose(dst)!=0) ok=0;
        if(!ok) luc_error("list.writeCompressed: write failed");
        RET(0,mkbool(1));return 1;
    }
    if(!strcmp(fn,"writeList")||!strcmp(fn,"writeBundle")){
        Str *path=checkstr(L,base,nargs,0,fn); FILE *f=fopen(path->s,"wb");
        if(!f) luc_error("%s: cannot open file",fn);
        int first=!strcmp(fn,"writeList")?1:1, ok=1;
        if(!strcmp(fn,"writeBundle")||!strcmp(fn,"writeCompressed"))
            ok=fputs("LUC-BUNDLE-1\n",f)!=EOF;
        for(int a=first;a<nargs&&ok;a++){
            Table *t=nums_list_arg(L,base,nargs,a,fn);
            if(!strcmp(fn,"writeBundle")||!strcmp(fn,"writeCompressed"))
                ok=fprintf(f,"A%u\n",(unsigned)t->alen)>=0;
            for(int i=0;i<t->alen&&ok;i++) ok=nums_write_value(f,t->arr[i]);
            if(!strcmp(fn,"writeBundle")||!strcmp(fn,"writeCompressed"))
                ok=ok&&fputs(".\n",f)!=EOF;
        }
        if(fclose(f)!=0) ok=0;
        if(!ok) luc_error("%s: write failed",fn);
        RET(0,mkbool(1)); return 1;
    }
    if(!strcmp(fn,"parseList")){
        Str *src=checkstr(L,base,nargs,0,fn); Table *out=tab_new(1);
        int i=0;
        while(i<src->len){
            while(i<src->len&&(isspace((unsigned char)src->s[i])||src->s[i]==','||src->s[i]=='['||src->s[i]==']')) i++;
            int start=i;
            while(i<src->len&&!isspace((unsigned char)src->s[i])&&src->s[i]!=','&&src->s[i]!=']') i++;
            if(i==start) continue;
            double d;
            int width=i-start;
            if(width==4&&!memcmp(src->s+start,"true",4)) list_push(out,mkbool(1));
            else if(width==5&&!memcmp(src->s+start,"false",5)) list_push(out,mkbool(0));
            else if(width==3&&!memcmp(src->s+start,"nil",3)) list_push(out,NIL);
            else if(str2num(src->s+start,width,&d)) list_push(out,mknum(d));
            else list_push(out,strv(src->s+start,i-start));
        }
        RET(0,mkobj(LT_LIST,out)); return 1;
    }
    if(!strcmp(fn,"listString")){
        Table *t=nums_list_arg(L,base,nargs,0,fn);
        Str *sep=nargs>=2?checkstr(L,base,nargs,1,fn):str_fromc(",");
        size_t cap=64,len=0; char *buf=(char*)malloc(cap);
        if(!buf) luc_error("list.listString: out of memory");
        for(int i=0;i<t->alen;i++){
            Str *s=tostr(t->arr[i]); size_t need=len+(size_t)s->len+(i?(size_t)sep->len:0)+1;
            if(need>cap){ while(need>cap) cap*=2; buf=(char*)realloc(buf,cap); if(!buf){luc_error("list.listString: out of memory");} }
            if(i){memcpy(buf+len,sep->s,(size_t)sep->len);len+=(size_t)sep->len;}
            memcpy(buf+len,s->s,(size_t)s->len);len+=(size_t)s->len;
        }
        Value result=strv(buf,(int)len); free(buf); RET(0,result); return 1;
    }
    luc_error("list.%s: not implemented",fn); return 0;
}

static int nums_prefix(Str *s,Str *p){
    return p->len<=s->len&&!memcmp(s->s,p->s,(size_t)p->len);
}
static int nums_suffix(Str *s,Str *p){
    return p->len<=s->len&&!memcmp(s->s+s->len-p->len,p->s,(size_t)p->len);
}
LFN(f_nums_text_api){
    const char *fn=self->name;
    Str *s,*a,*b; char *buf; size_t len,cap;
    if(!strcmp(fn,"concat")||!strcmp(fn,"joinText")){
        Table *t=NULL; Str *sep=str_fromc("");
        int first=0;
        if(!strcmp(fn,"joinText")){
            t=nums_list_arg(L,base,nargs,0,fn); first=1;
            if(nargs>=2) sep=checkstr(L,base,nargs,1,fn);
        }
        cap=64; len=0; buf=(char*)malloc(cap);
        if(!buf) luc_error("string.concat: out of memory");
        int n=t?t->alen:nargs;
        for(int i=0;i<n;i++){
            Value v=t?t->arr[i]:AR(i);
            Str *part=tostr(v); size_t need=len+(size_t)part->len+(i?(size_t)sep->len:0)+1;
            if(need>cap){ while(need>cap) cap*=2; buf=(char*)realloc(buf,cap); if(!buf) luc_error("string.concat: out of memory"); }
            if(i){memcpy(buf+len,sep->s,(size_t)sep->len);len+=(size_t)sep->len;}
            memcpy(buf+len,part->s,(size_t)part->len);len+=(size_t)part->len;
        }
        (void)first;
        Value result=strv(buf,(int)len); free(buf); RET(0,result); return 1;
    }
    if(!strcmp(fn,"repeatText")){
        s=checkstr(L,base,nargs,0,fn); int n=nums_nonnegative(checkint(L,base,nargs,1,fn),fn);
        if((int64_t)s->len*n>100000000) luc_error("string.repeatText: result is too large");
        size_t total=(size_t)s->len*(size_t)n; buf=(char*)malloc(total?total:1);
        if(!buf) luc_error("string.repeatText: out of memory");
        for(int i=0;i<n;i++) memcpy(buf+(size_t)i*s->len,s->s,(size_t)s->len);
        Value v=strv(buf,(int)total); free(buf); RET(0,v); return 1;
    }
    if(!strcmp(fn,"capitalize")||!strcmp(fn,"lowercase")||!strcmp(fn,"uppercase")||
       !strcmp(fn,"titleCase")||!strcmp(fn,"reverseCase")){
        s=checkstr(L,base,nargs,0,fn); buf=(char*)malloc((size_t)s->len+1);
        if(!buf) luc_error("string: out of memory");
        int word=1;
        for(int i=0;i<s->len;i++){
            unsigned char c=(unsigned char)s->s[i];
            if(!strcmp(fn,"lowercase")) buf[i]=(char)tolower(c);
            else if(!strcmp(fn,"uppercase")) buf[i]=(char)toupper(c);
            else if(!strcmp(fn,"reverseCase")) buf[i]=(char)(islower(c)?toupper(c):isupper(c)?tolower(c):c);
            else if(!strcmp(fn,"capitalize")) buf[i]=(char)(i==0?toupper(c):c);
            else { buf[i]=(char)(word?toupper(c):c); word=isspace(c)!=0; }
        }
        buf[s->len]=0; Value v=strv(buf,s->len); free(buf); RET(0,v); return 1;
    }
    if(!strcmp(fn,"alignCenter")){
        s=checkstr(L,base,nargs,0,fn); int width=nums_nonnegative(checkint(L,base,nargs,1,fn),fn);
        if(width<=s->len){ RET(0,AR(0)); return 1; }
        int left=(width-s->len)/2,right=width-s->len-left;
        len=(size_t)width; buf=(char*)malloc(len?len:1); if(!buf) luc_error("string.alignCenter: out of memory");
        memset(buf,' ',(size_t)left); memcpy(buf+left,s->s,(size_t)s->len); memset(buf+left+s->len,' ',(size_t)right);
        Value v=strv(buf,width); free(buf); RET(0,v); return 1;
    }
    if(!strcmp(fn,"trim")||!strcmp(fn,"trimLeft")||!strcmp(fn,"trimRight")){
        s=checkstr(L,base,nargs,0,fn); int start=0,end=s->len;
        if(strcmp(fn,"trimRight")) while(start<end&&isspace((unsigned char)s->s[start])) start++;
        if(strcmp(fn,"trimLeft")) while(end>start&&isspace((unsigned char)s->s[end-1])) end--;
        RET(0,strv(s->s+start,end-start)); return 1;
    }
    if(!strcmp(fn,"replaceText")){
        s=checkstr(L,base,nargs,0,fn); a=checkstr(L,base,nargs,1,fn); b=checkstr(L,base,nargs,2,fn);
        if(a->len==0) luc_error("string.replaceText: search text must not be empty");
        cap=(size_t)s->len+1; len=0; buf=(char*)malloc(cap); if(!buf) luc_error("string.replaceText: out of memory");
        for(int i=0;i<s->len;){
            if(i+a->len<=s->len&&!memcmp(s->s+i,a->s,(size_t)a->len)){
                size_t need=len+(size_t)b->len+1;
                if(need>cap){while(need>cap)cap*=2;buf=(char*)realloc(buf,cap);if(!buf)luc_error("string.replaceText: out of memory");}
                memcpy(buf+len,b->s,(size_t)b->len);len+=(size_t)b->len;i+=a->len;
            }else{
                if(len+2>cap){cap*=2;buf=(char*)realloc(buf,cap);if(!buf)luc_error("string.replaceText: out of memory");}
                buf[len++]=s->s[i++];
            }
        }
        Value v=strv(buf,(int)len);free(buf);RET(0,v);return 1;
    }
    if(!strcmp(fn,"findText")){
        s=checkstr(L,base,nargs,0,fn); a=checkstr(L,base,nargs,1,fn);
        if(a->len==0){RET(0,mknum(1));return 1;}
        for(int i=0;i+a->len<=s->len;i++) if(!memcmp(s->s+i,a->s,(size_t)a->len)){RET(0,mknum(i+1));return 1;}
        RET(0,NIL);return 1;
    }
    if(!strcmp(fn,"countText")){
        s=checkstr(L,base,nargs,0,fn); a=checkstr(L,base,nargs,1,fn);
        if(a->len==0) luc_error("string.countText: search text must not be empty");
        int count=0; for(int i=0;i+a->len<=s->len;) if(!memcmp(s->s+i,a->s,(size_t)a->len)){count++;i+=a->len;}else i++;
        RET(0,mknum(count));return 1;
    }
    if(!strcmp(fn,"startsWith")||!strcmp(fn,"endsWith")){
        s=checkstr(L,base,nargs,0,fn); a=checkstr(L,base,nargs,1,fn);
        RET(0,mkbool(!strcmp(fn,"startsWith")?nums_prefix(s,a):nums_suffix(s,a)));return 1;
    }
    if(!strcmp(fn,"splitText")){
        s=checkstr(L,base,nargs,0,fn); a=checkstr(L,base,nargs,1,fn);
        if(a->len==0) luc_error("string.splitText: separator must not be empty");
        Table *out=tab_new(1); int at=0;
        for(int i=0;i+a->len<=s->len;){
            if(!memcmp(s->s+i,a->s,(size_t)a->len)){list_push(out,strv(s->s+at,i-at));i+=a->len;at=i;}
            else i++;
        }
        list_push(out,strv(s->s+at,s->len-at));RET(0,mkobj(LT_LIST,out));return 1;
    }
    if(!strcmp(fn,"isLetters")||!strcmp(fn,"isDigits")||!strcmp(fn,"isAlphanumeric")){
        s=checkstr(L,base,nargs,0,fn); int ok=s->len>0;
        for(int i=0;i<s->len;i++){
            unsigned char c=(unsigned char)s->s[i];
            if(!strcmp(fn,"isLetters")) ok=ok&&isalpha(c);
            else if(!strcmp(fn,"isDigits")) ok=ok&&isdigit(c);
            else ok=ok&&isalnum(c);
        }
        RET(0,mkbool(ok));return 1;
    }
    if(!strcmp(fn,"zeroPad")){
        s=checkstr(L,base,nargs,0,fn); int width=checkint(L,base,nargs,1,fn);
        if(width<0||width>10000000) luc_error("string.zeroPad: width out of range");
        if(width<=s->len){RET(0,AR(0));return 1;}
        buf=(char*)malloc((size_t)width);if(!buf)luc_error("string.zeroPad: out of memory");
        memset(buf,'0',(size_t)(width-s->len));memcpy(buf+width-s->len,s->s,(size_t)s->len);
        Value v=strv(buf,width);free(buf);RET(0,v);return 1;
    }
    if(!strcmp(fn,"encodeText")||!strcmp(fn,"decodeText")){
        s=checkstr(L,base,nargs,0,fn);
        static const char hex[]="0123456789abcdef";
        if(!strcmp(fn,"encodeText")){
            buf=(char*)malloc((size_t)s->len*2+1);if(!buf)luc_error("string.encodeText: out of memory");
            for(int i=0;i<s->len;i++){unsigned char c=(unsigned char)s->s[i];buf[2*i]=hex[c>>4];buf[2*i+1]=hex[c&15];}
            Value v=strv(buf,s->len*2);free(buf);RET(0,v);return 1;
        }
        if(s->len%2) luc_error("string.decodeText: hex string must have even length");
        buf=(char*)malloc((size_t)s->len/2+1);if(!buf)luc_error("string.decodeText: out of memory");
        for(int i=0;i<s->len;i+=2){int hi=hexval((unsigned char)s->s[i]),lo=hexval((unsigned char)s->s[i+1]);
            if(hi<0||lo<0){free(buf);luc_error("string.decodeText: invalid hex string");return 0;}
            buf[i/2]=(char)((hi<<4)|lo);}
        Value v=strv(buf,s->len/2);free(buf);RET(0,v);return 1;
    }
    luc_error("string.%s: not implemented",fn);return 0;
}

static int nums_timestamp(Value v,time_t *out){
    if(v.t==LT_TABLE) v=tab_get(AS_TAB(v),cstrv("timestamp"));
    if(v.t!=LT_NUM||!isfinite(v.u.n)) return 0;
    if(v.u.n<-9223372036854775808.0||v.u.n>=9223372036854775808.0) return 0;
    *out=(time_t)(int64_t)v.u.n;
    return (double)*out==v.u.n;
}

static Table *nums_datetime_table(time_t stamp){
    struct tm tmv;
#if defined(_WIN32)
    if(gmtime_s(&tmv,&stamp)!=0) return NULL;
#else
    if(!gmtime_r(&stamp,&tmv)) return NULL;
#endif
    Table *d=tab_new(0);
    tab_set(d,cstrv("timestamp"),mknum((double)stamp));
    tab_set(d,cstrv("year"),mknum(tmv.tm_year+1900));
    tab_set(d,cstrv("month"),mknum(tmv.tm_mon+1));
    tab_set(d,cstrv("day"),mknum(tmv.tm_mday));
    tab_set(d,cstrv("hour"),mknum(tmv.tm_hour));
    tab_set(d,cstrv("minute"),mknum(tmv.tm_min));
    tab_set(d,cstrv("second"),mknum(tmv.tm_sec));
    tab_set(d,cstrv("weekday"),mknum(tmv.tm_wday));
    return d;
}

LFN(f_nums_time_api){
    const char *fn=self->name; time_t stamp;
    if(!strcmp(fn,"dateTime")){
        if(nargs==0) stamp=time(NULL);
        else if(!nums_timestamp(AR(0),&stamp)){RET(0,NIL);return 1;}
        Table *d=nums_datetime_table(stamp);
        if(!d){RET(0,NIL);return 1;}
        RET(0,mkobj(LT_TABLE,d));return 1;
    }
    if(!strcmp(fn,"timeSpan")){
        double start=checknum(L,base,nargs,0,fn),end=nargs>=2?checknum(L,base,nargs,1,fn):start;
        RET(0,mknum(end-start));return 1;
    }
    if(!strcmp(fn,"isInvalidDate")){
        time_t t; RET(0,mkbool(!nums_timestamp(AR(0),&t)||nums_datetime_table(t)==NULL));return 1;
    }
    if(!strcmp(fn,"dateString")){
        if(!nums_timestamp(AR(0),&stamp)){RET(0,NIL);return 1;}
        struct tm tmv;
#if defined(_WIN32)
        if(gmtime_s(&tmv,&stamp)!=0){RET(0,NIL);return 1;}
#else
        if(!gmtime_r(&stamp,&tmv)){RET(0,NIL);return 1;}
#endif
        Str *fmt=nargs>=2?checkstr(L,base,nargs,1,fn):str_fromc("%Y-%m-%d %H:%M:%S");
        char buf[512]; size_t n=strftime(buf,sizeof buf,fmt->s,&tmv);
        if(!n){RET(0,NIL);return 1;}
        RET(0,strv(buf,(int)n));return 1;
    }
    if(!strcmp(fn,"isWorkDay")){
        if(!nums_timestamp(AR(0),&stamp)){RET(0,mkbool(0));return 1;}
        struct tm tmv;
#if defined(_WIN32)
        if(gmtime_s(&tmv,&stamp)!=0){RET(0,mkbool(0));return 1;}
#else
        if(!gmtime_r(&stamp,&tmv)){RET(0,mkbool(0));return 1;}
#endif
        RET(0,mkbool(tmv.tm_wday!=0&&tmv.tm_wday!=6));return 1;
    }
    if(!strcmp(fn,"workDaysBetween")){
        time_t a,b;
        if(!nums_timestamp(AR(0),&a)||!nums_timestamp(AR(1),&b))
            luc_error("time.workDaysBetween: expected valid Unix timestamps");
        int64_t start_day=(int64_t)floorl((long double)a/86400.0L);
        int64_t end_day=(int64_t)floorl((long double)b/86400.0L);
        if(fabsl((long double)end_day-(long double)start_day)>1000000.0L)
            luc_error("time.workDaysBetween: range is too large");
        int count=0;
        if(start_day<end_day){
            for(int64_t day=start_day+1;day<=end_day;day++){
                int weekday=(int)((day+4)%7); if(weekday<0) weekday+=7;
                if(weekday!=0&&weekday!=6) count++;
            }
        }else{
            for(int64_t day=start_day;day>end_day;day--){
                int weekday=(int)((day+4)%7); if(weekday<0) weekday+=7;
                if(weekday!=0&&weekday!=6) count--;
            }
        }
        RET(0,mknum((double)count));return 1;
    }
    if(!strcmp(fn,"workDayOffset")){
        if(!nums_timestamp(AR(0),&stamp)) luc_error("time.workDayOffset: expected a valid Unix timestamp");
        int days=checkint(L,base,nargs,1,fn),dir=days<0?-1:1;
        if(days==INT_MIN) luc_error("time.workDayOffset: offset is out of range");
        if(days>1000000||days< -1000000) luc_error("time.workDayOffset: offset is too large");
        int left=days<0?-days:days; int64_t x=(int64_t)stamp;
        while(left>0){
            x+=(int64_t)dir*86400; time_t day=(time_t)x; struct tm tmv;
#if defined(_WIN32)
            if(gmtime_s(&tmv,&day)!=0) luc_error("time.workDayOffset: timestamp out of range");
#else
            if(!gmtime_r(&day,&tmv)) luc_error("time.workDayOffset: timestamp out of range");
#endif
            if(tmv.tm_wday!=0&&tmv.tm_wday!=6) left--;
        }
        RET(0,mknum((double)x));return 1;
    }
    if(!strcmp(fn,"workCalendar")){
        time_t a,b;
        if(!nums_timestamp(AR(0),&a)||!nums_timestamp(AR(1),&b))
            luc_error("time.workCalendar: expected valid Unix timestamps");
        int64_t day_a=(int64_t)floorl((long double)a/86400.0L);
        int64_t day_b=(int64_t)floorl((long double)b/86400.0L);
        int64_t lo=day_a<day_b?day_a:day_b,hi=day_a<day_b?day_b:day_a;
        if((long double)hi-(long double)lo>1000000.0L)
            luc_error("time.workCalendar: requested range is too large");
        Table *days=tab_new(1);
        for(int64_t day=lo;;day++){
            int weekday=(int)((day+4)%7); if(weekday<0) weekday+=7;
            Table *item=tab_new(0);
            tab_set(item,cstrv("timestamp"),mknum((double)day*86400.0));
            tab_set(item,cstrv("workday"),mkbool(weekday!=0&&weekday!=6));
            list_push(days,mkobj(LT_TABLE,item));
            if(day>=hi) break;
        }
        RET(0,mkobj(LT_LIST,days));return 1;
    }
    luc_error("time.%s: not implemented",fn);return 0;
}

typedef struct { int rows,cols; double *v; } NumsMatrix;

static void nums_matrix_free(NumsMatrix *m){free(m->v);m->v=NULL;m->rows=m->cols=0;}

static int nums_matrix_read(LucState *L,int base,int nargs,int arg,const char *fn,NumsMatrix *m){
    Value value=AR(arg);
    if(value.t!=LT_LIST){luc_error("matrix.%s: expected a nested list",fn);return 0;}
    int shape[16]={0},rank=nums_shape_value(value,shape,0);
    if(rank!=2){luc_error("matrix.%s: expected a 2D matrix",fn);return 0;}
    if(shape[0]>1000000||shape[1]>1000000||
       (shape[0]&&shape[1]>10000000/shape[0])){
        luc_error("matrix.%s: matrix is too large",fn);return 0;
    }
    Table *flat=tab_new(1);nums_flatten_into(AS_TAB(value),flat,0);
    m->rows=shape[0];m->cols=shape[1];
    m->v=(double*)malloc((size_t)(flat->alen?flat->alen:1)*sizeof(double));
    if(!m->v){luc_error("matrix.%s: out of memory",fn);return 0;}
    for(int i=0;i<flat->alen;i++){
        if(flat->arr[i].t!=LT_NUM||!isfinite(flat->arr[i].u.n)){
            nums_matrix_free(m);luc_error("matrix.%s: entries must be finite numbers",fn);return 0;
        }
        m->v[i]=flat->arr[i].u.n;
    }
    return 1;
}

static Table *nums_matrix_list(const double *v,int rows,int cols){
    Table *out=tab_new(1);
    for(int i=0;i<rows;i++){
        Table *row=tab_new(1);
        for(int j=0;j<cols;j++) list_push(row,mknum(v[i*cols+j]));
        list_push(out,mkobj(LT_LIST,row));
    }
    return out;
}

static int nums_vector_read(LucState *L,int base,int nargs,int arg,const char *fn,double **out,int *n){
    Value value=AR(arg);
    if(value.t!=LT_LIST){luc_error("matrix.%s: expected a vector list",fn);return 0;}
    Table *t=AS_TAB(value);
    if(t->alen>10000000){luc_error("matrix.%s: vector is too large",fn);return 0;}
    double *v=(double*)malloc((size_t)(t->alen?t->alen:1)*sizeof(double));
    if(!v){luc_error("matrix.%s: out of memory",fn);return 0;}
    for(int i=0;i<t->alen;i++){
        if(t->arr[i].t!=LT_NUM||!isfinite(t->arr[i].u.n)){
            free(v);luc_error("matrix.%s: vector entries must be finite numbers",fn);return 0;
        }
        v[i]=t->arr[i].u.n;
    }
    *out=v;*n=t->alen;return 1;
}

static void nums_matmul(const double *a,const double *b,double *out,int m,int k,int n){
    for(int i=0;i<m;i++) for(int j=0;j<n;j++){
        double sum=0;
        for(int x=0;x<k;x++) sum+=a[i*k+x]*b[x*n+j];
        out[i*n+j]=sum;
    }
}

static int nums_matrix_inverse(const double *a,double *out,int n){
    if(n==0) return 0;
    double scale=0;
    for(int i=0;i<n*n;i++)if(fabs(a[i])>scale)scale=fabs(a[i]);
    double tolerance=scale*(2.2204460492503131e-16)*(double)n*16.0;
    double *aug=(double*)calloc((size_t)n*(size_t)(2*n),sizeof(double));
    if(!aug) return 0;
    int width=2*n;
    for(int i=0;i<n;i++) for(int j=0;j<n;j++){
        aug[i*width+j]=a[i*n+j];aug[i*width+n+j]=(i==j?1.0:0.0);
    }
    for(int c=0;c<n;c++){
        int pivot=c;
        for(int r=c+1;r<n;r++) if(fabs(aug[r*width+c])>fabs(aug[pivot*width+c]))pivot=r;
        if(fabs(aug[pivot*width+c])<=tolerance){free(aug);return 0;}
        if(pivot!=c) for(int j=0;j<width;j++){
            double tmp=aug[c*width+j];aug[c*width+j]=aug[pivot*width+j];aug[pivot*width+j]=tmp;
        }
        double p=aug[c*width+c];
        for(int j=0;j<width;j++)aug[c*width+j]/=p;
        for(int r=0;r<n;r++) if(r!=c){
            double factor=aug[r*width+c];
            for(int j=0;j<width;j++)aug[r*width+j]-=factor*aug[c*width+j];
        }
    }
    for(int i=0;i<n;i++)for(int j=0;j<n;j++)out[i*n+j]=aug[i*width+n+j];
    free(aug);return 1;
}

static int nums_symmetric_eigen(const double *a,int n,double *eval,double *evec){
    double *d=(double*)malloc((size_t)n*(size_t)n*sizeof(double));
    if(!d)return 0;
    memcpy(d,a,(size_t)n*(size_t)n*sizeof(double));
    memset(evec,0,(size_t)n*(size_t)n*sizeof(double));
    double scale=0;
    for(int i=0;i<n*n;i++)if(fabs(d[i])>scale)scale=fabs(d[i]);
    double tolerance=scale*1e-13;
    for(int i=0;i<n;i++)evec[i*n+i]=1.0;
    for(int i=0;i<n;i++)for(int j=i+1;j<n;j++)
        if(fabs(a[i*n+j]-a[j*n+i])>1e-10*(1.0+fabs(a[i*n+j])+fabs(a[j*n+i]))){free(d);return -1;}
    int converged=0;
    for(int iter=0;iter<100*n*n+1;iter++){
        int p=0,q=0;double largest=0;
        for(int i=0;i<n;i++)for(int j=i+1;j<n;j++)
            if(fabs(d[i*n+j])>largest){largest=fabs(d[i*n+j]);p=i;q=j;}
        if(largest<=tolerance){converged=1;break;}
        double app=d[p*n+p],aqq=d[q*n+q],apq=d[p*n+q];
        double tau=(aqq-app)/(2.0*apq);
        double t=(tau>=0?1.0:-1.0)/(fabs(tau)+sqrt(1.0+tau*tau));
        double c=1.0/sqrt(1.0+t*t),s=t*c;
        for(int k=0;k<n;k++)if(k!=p&&k!=q){
            double dkp=d[k*n+p],dkq=d[k*n+q];
            d[k*n+p]=d[p*n+k]=c*dkp-s*dkq;
            d[k*n+q]=d[q*n+k]=s*dkp+c*dkq;
        }
        d[p*n+p]=app-t*apq;d[q*n+q]=aqq+t*apq;
        d[p*n+q]=d[q*n+p]=0.0;
        for(int k=0;k<n;k++){
            double vkp=evec[k*n+p],vkq=evec[k*n+q];
            evec[k*n+p]=c*vkp-s*vkq;evec[k*n+q]=s*vkp+c*vkq;
        }
    }
    if(!converged){free(d);return 0;}
    for(int i=0;i<n;i++)eval[i]=d[i*n+i];
    for(int i=0;i<n;i++)for(int j=i+1;j<n;j++)if(eval[j]>eval[i]){
        double x=eval[i];eval[i]=eval[j];eval[j]=x;
        for(int r=0;r<n;r++){x=evec[r*n+i];evec[r*n+i]=evec[r*n+j];evec[r*n+j]=x;}
    }
    free(d);return 1;
}

LFN(f_nums_matrix_api){
    const char *fn=self->name;
    NumsMatrix a={0,0,NULL},b={0,0,NULL};
    double *x=NULL,*y=NULL,*out=NULL;
    int n=0,m=0,k=0;
    if(!strcmp(fn,"dot")){
        if(!nums_vector_read(L,base,nargs,0,fn,&x,&n)||
           !nums_vector_read(L,base,nargs,1,fn,&y,&m)){free(x);free(y);return 0;}
        if(n!=m){free(x);free(y);luc_error("matrix.dot: vector lengths must match");return 0;}
        double result=0;for(int i=0;i<n;i++)result+=x[i]*y[i];
        free(x);free(y);RET(0,mknum(result));return 1;
    }
    if(!strcmp(fn,"cross")){
        if(!nums_vector_read(L,base,nargs,0,fn,&x,&n)||
           !nums_vector_read(L,base,nargs,1,fn,&y,&m)){free(x);free(y);return 0;}
        if(n!=3||m!=3){free(x);free(y);luc_error("matrix.cross: both vectors must have length 3");return 0;}
        out=(double*)malloc(3*sizeof(double));if(!out){free(x);free(y);luc_error("matrix.cross: out of memory");return 0;}
        out[0]=x[1]*y[2]-x[2]*y[1];out[1]=x[2]*y[0]-x[0]*y[2];out[2]=x[0]*y[1]-x[1]*y[0];
        Table *v=tab_new(1);for(int i=0;i<3;i++)list_push(v,mknum(out[i]));
        free(x);free(y);free(out);RET(0,mkobj(LT_LIST,v));return 1;
    }
    if(!strcmp(fn,"matMul")){
        if(!nums_matrix_read(L,base,nargs,0,fn,&a)||!nums_matrix_read(L,base,nargs,1,fn,&b)){
            nums_matrix_free(&a);nums_matrix_free(&b);return 0;
        }
        if(a.cols!=b.rows){nums_matrix_free(&a);nums_matrix_free(&b);luc_error("matrix.matMul: inner dimensions must match");return 0;}
        if(a.rows&&b.cols>10000000/a.rows){nums_matrix_free(&a);nums_matrix_free(&b);luc_error("matrix.matMul: result is too large");return 0;}
        out=(double*)calloc((size_t)(a.rows?a.rows:1)*(size_t)(b.cols?b.cols:1),sizeof(double));
        if(!out){nums_matrix_free(&a);nums_matrix_free(&b);luc_error("matrix.matMul: out of memory");return 0;}
        nums_matmul(a.v,b.v,out,a.rows,a.cols,b.cols);
        Table *result=nums_matrix_list(out,a.rows,b.cols);
        nums_matrix_free(&a);nums_matrix_free(&b);free(out);RET(0,mkobj(LT_LIST,result));return 1;
    }
    if(!strcmp(fn,"transpose")){
        if(!nums_matrix_read(L,base,nargs,0,fn,&a))return 0;
        out=(double*)malloc((size_t)(a.rows*a.cols?a.rows*a.cols:1)*sizeof(double));
        if(!out){nums_matrix_free(&a);luc_error("matrix.transpose: out of memory");return 0;}
        for(int i=0;i<a.rows;i++)for(int j=0;j<a.cols;j++)out[j*a.rows+i]=a.v[i*a.cols+j];
        Table *result=nums_matrix_list(out,a.cols,a.rows);
        nums_matrix_free(&a);free(out);RET(0,mkobj(LT_LIST,result));return 1;
    }
    if(!strcmp(fn,"norm")){
        Value v=AR(0);Table *flat=tab_new(1);
        if(v.t!=LT_LIST){luc_error("matrix.norm: expected a vector or matrix list");return 0;}
        nums_flatten_into(AS_TAB(v),flat,0);double sum=0;
        for(int i=0;i<flat->alen;i++){
            if(flat->arr[i].t!=LT_NUM||!isfinite(flat->arr[i].u.n)){luc_error("matrix.norm: entries must be finite numbers");return 0;}
            sum+=flat->arr[i].u.n*flat->arr[i].u.n;
        }
        RET(0,mknum(sqrt(sum)));return 1;
    }
    if(!strcmp(fn,"det")||!strcmp(fn,"trace")||!strcmp(fn,"inverse")||
       !strcmp(fn,"matrixPower")||!strcmp(fn,"eigen")||!strcmp(fn,"cholesky")){
        if(!nums_matrix_read(L,base,nargs,0,fn,&a))return 0;
        if(a.rows!=a.cols){nums_matrix_free(&a);luc_error("matrix.%s: matrix must be square",fn);return 0;}
        n=a.rows;
        if(!strcmp(fn,"trace")){
            double tr=0;for(int i=0;i<n;i++)tr+=a.v[i*n+i];
            nums_matrix_free(&a);RET(0,mknum(tr));return 1;
        }
        if(!strcmp(fn,"det")){
            out=(double*)malloc((size_t)(n*n?n*n:1)*sizeof(double));
            if(!out){nums_matrix_free(&a);luc_error("matrix.det: out of memory");return 0;}
            memcpy(out,a.v,(size_t)n*(size_t)n*sizeof(double));
            double scale=0;for(int i=0;i<n*n;i++)if(fabs(out[i])>scale)scale=fabs(out[i]);
            double tolerance=scale*(2.2204460492503131e-16)*(double)n*16.0;
            double d=1;int sign=1;
            for(int c=0;c<n;c++){
                int p=c;for(int r=c+1;r<n;r++)if(fabs(out[r*n+c])>fabs(out[p*n+c]))p=r;
                if(fabs(out[p*n+c])<=tolerance){d=0;break;}
                if(p!=c){for(int j=c;j<n;j++){double z=out[c*n+j];out[c*n+j]=out[p*n+j];out[p*n+j]=z;}sign=-sign;}
                double pivot=out[c*n+c];d*=pivot;
                for(int r=c+1;r<n;r++){double f=out[r*n+c]/pivot;for(int j=c+1;j<n;j++)out[r*n+j]-=f*out[c*n+j];}
            }
            free(out);nums_matrix_free(&a);RET(0,mknum(d*sign));return 1;
        }
        if(!strcmp(fn,"inverse")){
            out=(double*)malloc((size_t)(n*n?n*n:1)*sizeof(double));
            if(!out){nums_matrix_free(&a);luc_error("matrix.inverse: out of memory");return 0;}
            if(!nums_matrix_inverse(a.v,out,n)){free(out);nums_matrix_free(&a);luc_error("matrix.inverse: matrix is singular");return 0;}
            Table *result=nums_matrix_list(out,n,n);free(out);nums_matrix_free(&a);
            RET(0,mkobj(LT_LIST,result));return 1;
        }
        if(!strcmp(fn,"matrixPower")){
            int exponent=checkint(L,base,nargs,1,fn);
            out=(double*)calloc((size_t)(n*n?n*n:1),sizeof(double));
            double *basev=(double*)malloc((size_t)(n*n?n*n:1)*sizeof(double));
            double *tmp=(double*)malloc((size_t)(n*n?n*n:1)*sizeof(double));
            if(!out||!basev||!tmp){free(out);free(basev);free(tmp);nums_matrix_free(&a);luc_error("matrix.matrixPower: out of memory");return 0;}
            for(int i=0;i<n;i++)out[i*n+i]=1;
            if(exponent<0){
                if(!nums_matrix_inverse(a.v,basev,n)){free(out);free(basev);free(tmp);nums_matrix_free(&a);luc_error("matrix.matrixPower: matrix is singular");return 0;}
                if(exponent==INT_MIN){free(out);free(basev);free(tmp);nums_matrix_free(&a);luc_error("matrix.matrixPower: exponent out of range");return 0;}
                exponent=-exponent;
            }else memcpy(basev,a.v,(size_t)n*(size_t)n*sizeof(double));
            while(exponent){
                if(exponent&1){nums_matmul(out,basev,tmp,n,n,n);memcpy(out,tmp,(size_t)n*(size_t)n*sizeof(double));}
                exponent>>=1;
                if(exponent){nums_matmul(basev,basev,tmp,n,n,n);memcpy(basev,tmp,(size_t)n*(size_t)n*sizeof(double));}
            }
            Table *result=nums_matrix_list(out,n,n);free(out);free(basev);free(tmp);nums_matrix_free(&a);
            RET(0,mkobj(LT_LIST,result));return 1;
        }
        if(!strcmp(fn,"cholesky")){
            for(int i=0;i<n;i++)for(int j=i+1;j<n;j++)
                if(fabs(a.v[i*n+j]-a.v[j*n+i])>1e-10*(1.0+fabs(a.v[i*n+j])+fabs(a.v[j*n+i]))){
                    nums_matrix_free(&a);luc_error("matrix.cholesky: matrix must be symmetric");return 0;
                }
            out=(double*)calloc((size_t)(n*n?n*n:1),sizeof(double));
            if(!out){nums_matrix_free(&a);luc_error("matrix.cholesky: out of memory");return 0;}
            for(int i=0;i<n;i++)for(int j=0;j<=i;j++){
                double sum=a.v[i*n+j];
                for(int k=0;k<j;k++)sum-=out[i*n+k]*out[j*n+k];
                if(i==j){if(sum<=0){free(out);nums_matrix_free(&a);luc_error("matrix.cholesky: matrix must be positive definite");return 0;}out[i*n+j]=sqrt(sum);}
                else out[i*n+j]=sum/out[j*n+j];
            }
            Table *result=nums_matrix_list(out,n,n);free(out);nums_matrix_free(&a);
            RET(0,mkobj(LT_LIST,result));return 1;
        }
        if(!strcmp(fn,"eigen")){
            if(n>32){nums_matrix_free(&a);luc_error("matrix.eigen: matrix size is limited to 32");return 0;}
            double *eval=(double*)malloc((size_t)(n?n:1)*sizeof(double));
            double *evec=(double*)malloc((size_t)(n*n?n*n:1)*sizeof(double));
            if(!eval||!evec){free(eval);free(evec);nums_matrix_free(&a);luc_error("matrix.eigen: out of memory");return 0;}
            int status=nums_symmetric_eigen(a.v,n,eval,evec);
            if(status<=0){free(eval);free(evec);nums_matrix_free(&a);luc_error(status<0?"matrix.eigen: matrix must be symmetric":"matrix.eigen: solver did not converge");return 0;}
            Table *values=tab_new(1);for(int i=0;i<n;i++)list_push(values,mknum(eval[i]));
            Table *vectors=nums_matrix_list(evec,n,n);
            free(eval);free(evec);nums_matrix_free(&a);
            RET(0,mkobj(LT_LIST,values));RET(1,mkobj(LT_LIST,vectors));return 2;
        }
    }
    if(!strcmp(fn,"solve")){
        if(!nums_matrix_read(L,base,nargs,0,fn,&a)||
           !nums_vector_read(L,base,nargs,1,fn,&x,&n)){nums_matrix_free(&a);free(x);return 0;}
        if(a.rows!=a.cols||a.rows!=n){nums_matrix_free(&a);free(x);luc_error("matrix.solve: square matrix and matching vector required");return 0;}
        out=(double*)malloc((size_t)(n?n:1)*sizeof(double));
        double *mat=(double*)malloc((size_t)(n*n?n*n:1)*sizeof(double));
        if(!out||!mat){free(out);free(mat);free(x);nums_matrix_free(&a);luc_error("matrix.solve: out of memory");return 0;}
        memcpy(mat,a.v,(size_t)n*(size_t)n*sizeof(double));memcpy(out,x,(size_t)n*sizeof(double));
        double scale=0;for(int i=0;i<n*n;i++)if(fabs(mat[i])>scale)scale=fabs(mat[i]);
        double tolerance=scale*(2.2204460492503131e-16)*(double)n*16.0;
        for(int c=0;c<n;c++){
            int p=c;for(int r=c+1;r<n;r++)if(fabs(mat[r*n+c])>fabs(mat[p*n+c]))p=r;
            if(fabs(mat[p*n+c])<=tolerance){free(out);free(mat);free(x);nums_matrix_free(&a);luc_error("matrix.solve: matrix is singular");return 0;}
            if(p!=c){for(int j=c;j<n;j++){double z=mat[c*n+j];mat[c*n+j]=mat[p*n+j];mat[p*n+j]=z;}double z=out[c];out[c]=out[p];out[p]=z;}
            double pivot=mat[c*n+c];for(int j=c;j<n;j++)mat[c*n+j]/=pivot;out[c]/=pivot;
            for(int r=0;r<n;r++)if(r!=c){double f=mat[r*n+c];for(int j=c;j<n;j++)mat[r*n+j]-=f*mat[c*n+j];out[r]-=f*out[c];}
        }
        Table *result=tab_new(1);for(int i=0;i<n;i++)list_push(result,mknum(out[i]));
        free(out);free(mat);free(x);nums_matrix_free(&a);RET(0,mkobj(LT_LIST,result));return 1;
    }
    if(!strcmp(fn,"rank")){
        if(!nums_matrix_read(L,base,nargs,0,fn,&a))return 0;
        double tol=nargs>=2?checknum(L,base,nargs,1,fn):1e-10;
        if(!isfinite(tol)||tol<0){nums_matrix_free(&a);luc_error("matrix.rank: tolerance must be finite and >= 0");return 0;}
        double *d=(double*)malloc((size_t)(a.rows*a.cols?a.rows*a.cols:1)*sizeof(double));
        if(!d){nums_matrix_free(&a);luc_error("matrix.rank: out of memory");return 0;}
        memcpy(d,a.v,(size_t)a.rows*(size_t)a.cols*sizeof(double));
        if(nargs<2){double scale=0;for(int i=0;i<a.rows*a.cols;i++)if(fabs(d[i])>scale)scale=fabs(d[i]);tol*=scale;}
        int rank=0,row=0;
        for(int c=0;c<a.cols&&row<a.rows;c++){
            int p=row;for(int r=row+1;r<a.rows;r++)if(fabs(d[r*a.cols+c])>fabs(d[p*a.cols+c]))p=r;
            if(fabs(d[p*a.cols+c])<=tol)continue;
            if(p!=row)for(int j=c;j<a.cols;j++){double z=d[row*a.cols+j];d[row*a.cols+j]=d[p*a.cols+j];d[p*a.cols+j]=z;}
            double pivot=d[row*a.cols+c];
            for(int r=row+1;r<a.rows;r++){double f=d[r*a.cols+c]/pivot;for(int j=c;j<a.cols;j++)d[r*a.cols+j]-=f*d[row*a.cols+j];}
            row++;rank++;
        }
        free(d);nums_matrix_free(&a);RET(0,mknum(rank));return 1;
    }
    if(!strcmp(fn,"qr")){
        if(!nums_matrix_read(L,base,nargs,0,fn,&a))return 0;
        int k=a.rows<a.cols?a.rows:a.cols;
        double *q=(double*)calloc((size_t)(a.rows*k?a.rows*k:1),sizeof(double));
        double *r=(double*)calloc((size_t)(k*a.cols?k*a.cols:1),sizeof(double));
        double *col=(double*)malloc((size_t)(a.rows?a.rows:1)*sizeof(double));
        if(!q||!r||!col){free(q);free(r);free(col);nums_matrix_free(&a);luc_error("matrix.qr: out of memory");return 0;}
        for(int j=0;j<a.cols;j++){
            for(int i=0;i<a.rows;i++)col[i]=a.v[i*a.cols+j];
            int upto=j<k?j:k;
            for(int c=0;c<upto;c++){
                double dot=0;for(int i=0;i<a.rows;i++)dot+=q[i*k+c]*col[i];
                r[c*a.cols+j]=dot;for(int i=0;i<a.rows;i++)col[i]-=dot*q[i*k+c];
            }
            if(j<k){
                double norm=0;for(int i=0;i<a.rows;i++)norm+=col[i]*col[i];norm=sqrt(norm);
                r[j*a.cols+j]=norm;
                if(norm>1e-14)for(int i=0;i<a.rows;i++)q[i*k+j]=col[i]/norm;
            }
        }
        Table *qout=nums_matrix_list(q,a.rows,k),*rout=nums_matrix_list(r,k,a.cols);
        free(q);free(r);free(col);nums_matrix_free(&a);
        RET(0,mkobj(LT_LIST,qout));RET(1,mkobj(LT_LIST,rout));return 2;
    }
    if(!strcmp(fn,"svd")){
        if(!nums_matrix_read(L,base,nargs,0,fn,&a))return 0;
        int m=a.rows,cols=a.cols,k=m<cols?m:cols;
        int nn=cols;
        if(nn>32){nums_matrix_free(&a);luc_error("matrix.svd: column count is limited to 32");return 0;}
        if(k==0){
            Table *uout=nums_matrix_list(NULL,m,0),*sv=tab_new(1),*vto=nums_matrix_list(NULL,0,nn);
            nums_matrix_free(&a);RET(0,mkobj(LT_LIST,uout));RET(1,mkobj(LT_LIST,sv));RET(2,mkobj(LT_LIST,vto));return 3;
        }
        double *ata=(double*)calloc((size_t)(nn*nn?nn*nn:1),sizeof(double));
        double *eval=(double*)malloc((size_t)(nn?nn:1)*sizeof(double));
        double *v=(double*)malloc((size_t)(nn*nn?nn*nn:1)*sizeof(double));
        double *u=(double*)calloc((size_t)(m*k?m*k:1),sizeof(double));
        double *vt=(double*)malloc((size_t)(k*nn?k*nn:1)*sizeof(double));
        double *svals=(double*)malloc((size_t)(k?k:1)*sizeof(double));
        if(!ata||!eval||!v||!u||!vt||!svals){free(ata);free(eval);free(v);free(u);free(vt);free(svals);nums_matrix_free(&a);luc_error("matrix.svd: out of memory");return 0;}
        for(int i=0;i<nn;i++)for(int j=0;j<nn;j++)for(int r=0;r<m;r++)ata[i*nn+j]+=a.v[r*nn+i]*a.v[r*nn+j];
        int status=nums_symmetric_eigen(ata,nn,eval,v);
        if(status<=0){free(ata);free(eval);free(v);free(u);free(vt);free(svals);nums_matrix_free(&a);luc_error("matrix.svd: eigensolver did not converge");return 0;}
        for(int c=0;c<k;c++){
            svals[c]=sqrt(eval[c]>0?eval[c]:0);
            for(int j=0;j<nn;j++)vt[c*nn+j]=v[j*nn+c];
            if(svals[c]>svals[0]*1e-14)for(int i=0;i<m;i++){
                double sum=0;for(int j=0;j<nn;j++)sum+=a.v[i*nn+j]*v[j*nn+c];
                u[i*k+c]=sum/svals[c];
            }
        }
        Table *uout=nums_matrix_list(u,m,k),*sv=tab_new(1),*vto=nums_matrix_list(vt,k,nn);
        for(int i=0;i<k;i++)list_push(sv,mknum(svals[i]));
        free(ata);free(eval);free(v);free(u);free(vt);free(svals);nums_matrix_free(&a);
        RET(0,mkobj(LT_LIST,uout));RET(1,mkobj(LT_LIST,sv));RET(2,mkobj(LT_LIST,vto));return 3;
    }
    luc_error("matrix.%s: not implemented",fn);return 0;
}

static int nums_list_has(Table *list,Value value){
    for(int i=0;i<list->alen;i++)if(val_rawequal(list->arr[i],value))return 1;
    return 0;
}

LFN(f_nums_sets_api){
    const char *fn=self->name;
    Table *a=nums_list_arg(L,base,nargs,0,fn);
    Table *b=NULL,*out=tab_new(1);
    if(nargs>=2)b=nums_list_arg(L,base,nargs,1,fn);
    if(!strcmp(fn,"union")){
        if(!b){luc_error("sets.union: expected two lists");return 0;}
        for(int i=0;i<a->alen;i++)if(!nums_list_has(out,a->arr[i]))list_push(out,a->arr[i]);
        for(int i=0;i<b->alen;i++){
            if(!nums_list_has(out,b->arr[i]))list_push(out,b->arr[i]);
        }
        RET(0,mkobj(LT_LIST,out));return 1;
    }
    if(!b){luc_error("sets.%s: expected two lists",fn);return 0;}
    if(!strcmp(fn,"intersect")||!strcmp(fn,"difference")||!strcmp(fn,"symmetricDiff")){
        for(int i=0;i<a->alen;i++){
            int seen=0,already=0;
            for(int j=0;j<b->alen;j++)if(val_rawequal(a->arr[i],b->arr[j])){seen=1;break;}
            for(int j=0;j<out->alen;j++)if(val_rawequal(a->arr[i],out->arr[j])){already=1;break;}
            if(!already&&((!strcmp(fn,"intersect")&&seen)||(!strcmp(fn,"difference")&&!seen)||
               (!strcmp(fn,"symmetricDiff")&&!seen)))list_push(out,a->arr[i]);
        }
        if(!strcmp(fn,"symmetricDiff"))for(int i=0;i<b->alen;i++){
            int seen=0,already=0;
            for(int j=0;j<a->alen;j++)if(val_rawequal(b->arr[i],a->arr[j])){seen=1;break;}
            for(int j=0;j<out->alen;j++)if(val_rawequal(b->arr[i],out->arr[j])){already=1;break;}
            if(!seen&&!already)list_push(out,b->arr[i]);
        }
        RET(0,mkobj(LT_LIST,out));return 1;
    }
    int subset=1,disjoint=1;
    for(int i=0;i<a->alen;i++){
        int seen=nums_list_has(b,a->arr[i]);
        if(!seen)subset=0;else disjoint=0;
    }
    if(!strcmp(fn,"isSubset"))RET(0,mkbool(subset));
    else if(!strcmp(fn,"isSuperset")){
        int superset=1;
        for(int i=0;i<b->alen;i++)if(!nums_list_has(a,b->arr[i])){superset=0;break;}
        RET(0,mkbool(superset));
    }else if(!strcmp(fn,"isDisjoint"))RET(0,mkbool(disjoint));
    else if(!strcmp(fn,"setEqual")){
        int equal=subset;
        for(int i=0;i<b->alen&&equal;i++)if(!nums_list_has(a,b->arr[i]))equal=0;
        RET(0,mkbool(equal));
    }
    else {luc_error("sets.%s: not implemented",fn);return 0;}
    return 1;
}

static double finance_factor(double rate,double periods,double type){
    if(rate==0)return periods;
    return (1.0+rate*type)*expm1(periods*log1p(rate))/rate;
}
static double finance_payment_value(double rate,double periods,double present,double future,double type){
    if(rate==0)return -(present+future)/periods;
    return -(future+present*pow(1.0+rate,periods))/finance_factor(rate,periods,type);
}
static int finance_check(double rate,double periods,double type,const char *fn){
    if(!isfinite(rate)||rate<=-1.0){luc_error("finance.%s: rate must be finite and > -1",fn);return 0;}
    if(!isfinite(periods)||periods<=0){luc_error("finance.%s: periods must be finite and > 0",fn);return 0;}
    if(type!=0.0&&type!=1.0){luc_error("finance.%s: payment timing must be 0 (end) or 1 (beginning)",fn);return 0;}
    return 1;
}
static double finance_npv_values(double rate,const double *flows,int n,int first_period){
    double sum=0,discount=pow(1.0+rate,(double)first_period);
    for(int i=0;i<n;i++){
        if(i)discount*=1.0+rate;
        sum+=flows[i]/discount;
    }
    return sum;
}
LFN(f_nums_finance_api){
    const char *fn=self->name;
    if(!strcmp(fn,"presentValue")||!strcmp(fn,"futureValue")||!strcmp(fn,"payment")){
        double rate=checknum(L,base,nargs,0,fn),periods=checknum(L,base,nargs,1,fn);
        double third=checknum(L,base,nargs,2,fn);
        double future=0,type=0,result;
        if(!finance_check(rate,periods,nargs>=5?checknum(L,base,nargs,4,fn):0,fn))return 0;
        type=nargs>=5?AR(4).u.n:0;
        if(!strcmp(fn,"presentValue")){
            double pmt=third;future=nargs>=4?checknum(L,base,nargs,3,fn):0;
            result=rate==0?-(future+pmt*periods):
                -(future+pmt*finance_factor(rate,periods,type))/pow(1.0+rate,periods);
        }else if(!strcmp(fn,"futureValue")){
            double pmt=third,present=nargs>=4?checknum(L,base,nargs,3,fn):0;
            result=-(present*pow(1.0+rate,periods)+pmt*finance_factor(rate,periods,type));
        }else{
            double present=third;future=nargs>=4?checknum(L,base,nargs,3,fn):0;
            result=finance_payment_value(rate,periods,present,future,type);
        }
        RET(0,mknum(result));return 1;
    }
    if(!strcmp(fn,"netPresentValue")){
        double rate=checknum(L,base,nargs,0,fn);double *flows=NULL;int n=0;
        if(!isfinite(rate)||rate<=-1.0){luc_error("finance.netPresentValue: rate must be finite and > -1");return 0;}
        if(!nums_vector_read(L,base,nargs,1,fn,&flows,&n))return 0;
        double value=finance_npv_values(rate,flows,n,1);
        free(flows);RET(0,mknum(value));return 1;
    }
    if(!strcmp(fn,"internalRate")){
        double *flows=NULL;int n=0;
        if(!nums_vector_read(L,base,nargs,0,fn,&flows,&n))return 0;
        if(n<2){free(flows);luc_error("finance.internalRate: need at least two cash flows");return 0;}
        int positive=0,negative=0;
        for(int i=0;i<n;i++){if(flows[i]>0)positive=1;else if(flows[i]<0)negative=1;}
        if(!positive||!negative){free(flows);luc_error("finance.internalRate: cash flows must include both signs");return 0;}
        double rate=nargs>=2?checknum(L,base,nargs,1,fn):0.1;
        if(!isfinite(rate)||rate<=-1.0){free(flows);luc_error("finance.internalRate: guess must be finite and > -1");return 0;}
        int converged=0;
        for(int iter=0;iter<100;iter++){
            double value=finance_npv_values(rate,flows,n,0),derivative=0;
            double base=1.0+rate,disc=1.0;
            for(int i=1;i<n;i++){disc*=base;derivative-=(double)i*flows[i]/(disc*base);}
            if(fabs(value)<1e-10){converged=1;break;}
            if(!isfinite(derivative)||fabs(derivative)<1e-14)break;
            double next=rate-value/derivative;
            if(!isfinite(next)||next<=-1.0||fabs(next-rate)>100.0)break;
            if(fabs(next-rate)<1e-12){rate=next;converged=1;break;}
            rate=next;
        }
        free(flows);
        if(!converged){luc_error("finance.internalRate: failed to converge");return 0;}
        RET(0,mknum(rate));return 1;
    }
    if(!strcmp(fn,"periodCount")){
        double rate=checknum(L,base,nargs,0,fn),pmt=checknum(L,base,nargs,1,fn);
        double present=checknum(L,base,nargs,2,fn),future=nargs>=4?checknum(L,base,nargs,3,fn):0;
        double type=nargs>=5?checknum(L,base,nargs,4,fn):0;
        if(!isfinite(rate)||rate<=-1||!isfinite(type)||(type!=0&&type!=1)){
            luc_error("finance.periodCount: invalid rate or payment timing");return 0;
        }
        double result;
        if(rate==0){
            if(pmt==0){luc_error("finance.periodCount: payment cannot be zero when rate is zero");return 0;}
            result=-(present+future)/pmt;
        }else{
            double factor=pmt*(1.0+rate*type)/rate;
            double denominator=present+factor;
            double growth=(factor-future)/denominator;
            if(denominator==0||growth<=0||!isfinite(growth)){
                luc_error("finance.periodCount: no real period count for these values");return 0;
            }
            result=log(growth)/log1p(rate);
        }
        if(!isfinite(result)||result<=0){luc_error("finance.periodCount: result must be positive");return 0;}
        RET(0,mknum(result));return 1;
    }
    if(!strcmp(fn,"interestRate")){
        double periods=checknum(L,base,nargs,0,fn),pmt=checknum(L,base,nargs,1,fn);
        double present=checknum(L,base,nargs,2,fn),future=nargs>=4?checknum(L,base,nargs,3,fn):0;
        double type=nargs>=5?checknum(L,base,nargs,4,fn):0;
        double rate=nargs>=6?checknum(L,base,nargs,5,fn):0.1;
        if(!isfinite(periods)||periods<=0||!isfinite(type)||(type!=0&&type!=1)||!isfinite(rate)||rate<=-1){
            luc_error("finance.interestRate: invalid periods, timing or guess");return 0;
        }
        int converged=0;
        for(int iter=0;iter<100;iter++){
            if(rate<=-1)break;
            double value=present*pow(1.0+rate,periods)+pmt*finance_factor(rate,periods,type)+future;
            if(fabs(value)<1e-10*(1.0+fabs(present)+fabs(future))){converged=1;break;}
            double h=1e-6*(1.0+fabs(rate));
            double rp=rate+h;
            double vp=present*pow(1.0+rp,periods)+pmt*finance_factor(rp,periods,type)+future;
            double derivative=(vp-value)/h;
            if(!isfinite(derivative)||fabs(derivative)<1e-14)break;
            double next=rate-value/derivative;
            if(!isfinite(next)||next<=-1||fabs(next-rate)>100)break;
            if(fabs(next-rate)<1e-12){rate=next;converged=1;break;}
            rate=next;
        }
        if(!converged){luc_error("finance.interestRate: failed to converge");return 0;}
        RET(0,mknum(rate));return 1;
    }
    if(!strcmp(fn,"principal")||!strcmp(fn,"interest")){
        double rate=checknum(L,base,nargs,0,fn);
        int period=checkint(L,base,nargs,1,fn);
        double periods=checknum(L,base,nargs,2,fn),present=checknum(L,base,nargs,3,fn);
        double future=nargs>=5?checknum(L,base,nargs,4,fn):0;
        double type=nargs>=6?checknum(L,base,nargs,5,fn):0;
        if(!finance_check(rate,periods,type,fn))return 0;
        if(period<1||period>periods){luc_error("finance.%s: period must be an integer in 1..periods",fn);return 0;}
        double pmt=finance_payment_value(rate,periods,present,future,type);
        double balance=present,previous_begin=present,interest_part=0;
        for(int p=1;p<=period;p++){
            if(type==0){
                interest_part=-balance*rate;
                balance=balance*(1.0+rate)+pmt;
            }else{
                interest_part=p==1?0.0:-previous_begin*rate;
                balance+=pmt;previous_begin=balance;balance*=1.0+rate;
            }
        }
        RET(0,mknum(!strcmp(fn,"interest")?interest_part:pmt-interest_part));return 1;
    }
    if(!strcmp(fn,"compound")){
        double principal=checknum(L,base,nargs,0,fn),rate=checknum(L,base,nargs,1,fn);
        double periods=checknum(L,base,nargs,2,fn),frequency=nargs>=4?checknum(L,base,nargs,3,fn):1;
        if(!isfinite(rate)||!isfinite(periods)||periods<0||!isfinite(frequency)||frequency<=0||
           1.0+rate/frequency<=0){luc_error("finance.compound: invalid principal, rate, periods or frequency");return 0;}
        RET(0,mknum(principal*pow(1.0+rate/frequency,frequency*periods)));return 1;
    }
    if(!strcmp(fn,"discount")){
        double future=checknum(L,base,nargs,0,fn),rate=checknum(L,base,nargs,1,fn),periods=checknum(L,base,nargs,2,fn);
        if(!isfinite(rate)||rate<=-1||!isfinite(periods)||periods<0){luc_error("finance.discount: rate must be > -1 and periods >= 0");return 0;}
        RET(0,mknum(future/pow(1.0+rate,periods)));return 1;
    }
    luc_error("finance.%s: not implemented",fn);return 0;
}

LFN(f_nums_list_api){
    const char *fn=self->name;
    Table *t,*out;
    int n,i,j;
    if(!strcmp(fn,"make")){
        out=tab_new(1); for(i=0;i<nargs;i++) list_push(out,AR(i));
        RET(0,mkobj(LT_LIST,out)); return 1;
    }
    if(!strcmp(fn,"toList")){
        out=nums_list_from_value(AR(0),fn);
        RET(0,mkobj(LT_LIST,out)); return 1;
    }
    if(!strcmp(fn,"rangeList")){
        int start=nums_int_arg(L,base,nargs,0,"list.rangeList");
        int end=nums_int_arg(L,base,nargs,1,"list.rangeList");
        int step=nargs>=3?nums_int_arg(L,base,nargs,2,"list.rangeList"):(start<=end?1:-1);
        if(!step) luc_error("list.rangeList: step cannot be zero");
        out=tab_new(1);
        for(int64_t x=start;step>0?x<=end:x>=end;x+=step){
            if(out->alen>=10000000) luc_error("list.rangeList: result is too large");
            list_push(out,mknum((double)x));
        }
        RET(0,mkobj(LT_LIST,out)); return 1;
    }
    if(!strcmp(fn,"spread")){
        double start=checknum(L,base,nargs,0,"list.spread");
        double end=checknum(L,base,nargs,1,"list.spread");
        n=nums_nonnegative(nums_int_arg(L,base,nargs,2,"list.spread"),"list.spread");
        if(n>10000000) luc_error("list.spread: result is too large");
        out=tab_new(1);
        for(i=0;i<n;i++) list_push(out,mknum(n==1?start:start+(end-start)*(double)i/(double)(n-1)));
        RET(0,mkobj(LT_LIST,out)); return 1;
    }
    if(!strcmp(fn,"zeroList")||!strcmp(fn,"oneList")||!strcmp(fn,"blankList")||
       !strcmp(fn,"fillList")){
        n=nums_nonnegative(nums_int_arg(L,base,nargs,0,"list.fillList"),"list.fillList");
        if(n>10000000) luc_error("list.fillList: result is too large");
        Value v=!strcmp(fn,"oneList")?mknum(1):
                !strcmp(fn,"blankList")?NIL:
                !strcmp(fn,"fillList")?AR(1):mknum(0);
        out=tab_new(1); for(i=0;i<n;i++) list_push(out,v);
        RET(0,mkobj(LT_LIST,out)); return 1;
    }
    if(!strcmp(fn,"identity")){
        n=nums_nonnegative(nums_int_arg(L,base,nargs,0,"list.identity"),"list.identity");
        if(n>10000) luc_error("list.identity: matrix is too large");
        out=tab_new(1);
        for(i=0;i<n;i++){
            Table *row=tab_new(1);
            for(j=0;j<n;j++) list_push(row,mknum(i==j?1:0));
            list_push(out,mkobj(LT_LIST,row));
        }
        RET(0,mkobj(LT_LIST,out)); return 1;
    }
    if(!strcmp(fn,"diagonal")){
        t=nums_list_arg(L,base,nargs,0,"list.diagonal"); n=t->alen;
        out=tab_new(1);
        for(i=0;i<n;i++){
            Table *row=tab_new(1);
            for(j=0;j<n;j++) list_push(row,i==j?t->arr[i]:mknum(0));
            list_push(out,mkobj(LT_LIST,row));
        }
        RET(0,mkobj(LT_LIST,out)); return 1;
    }
    if(!strcmp(fn,"gridList")){
        int rows=nums_nonnegative(checkint(L,base,nargs,0,"list.gridList"),"list.gridList");
        int cols=nums_nonnegative(checkint(L,base,nargs,1,"list.gridList"),"list.gridList");
        if((int64_t)rows*cols>10000000) luc_error("list.gridList: grid is too large");
        out=tab_new(1);
        for(i=0;i<rows;i++){
            Table *row=tab_new(1);
            for(j=0;j<cols;j++){
                Table *coord=tab_new(1);
                list_push(coord,mknum((double)i)); list_push(coord,mknum((double)j));
                list_push(row,mkobj(LT_LIST,coord));
            }
            list_push(out,mkobj(LT_LIST,row));
        }
        RET(0,mkobj(LT_LIST,out)); return 1;
    }
    if(!strcmp(fn,"fromIterator")){
        Value iter=AR(0);
        if(iter.t==LT_FUNC||iter.t==LT_CFUNC){
            out=tab_new(1); int scratch=base+nargs+4;
            for(i=0;i<10000000;i++){
                ensure_stack(L,scratch+4); L->stack[scratch]=iter;
                vm_call(L,scratch,0,1);
                Value item=L->stack[scratch];
                if(item.t==LT_NIL) break;
                list_push(out,item);
            }
            if(i==10000000) luc_error("list.fromIterator: iterator did not finish");
        }else out=nums_list_from_value(iter,fn);
        RET(0,mkobj(LT_LIST,out)); return 1;
    }
    if(!strcmp(fn,"flatten")||!strcmp(fn,"flatCopy")){
        t=nums_list_arg(L,base,nargs,0,"list.flatten"); out=tab_new(1);
        nums_flatten_into(t,out,0);
        RET(0,mkobj(LT_LIST,out)); return 1;
    }
    if(!strcmp(fn,"reshape")){
        t=nums_list_arg(L,base,nargs,0,"list.reshape");
        Table *dims=nums_list_arg(L,base,nargs,1,"list.reshape");
        if(dims->alen<1||dims->alen>16) luc_error("list.reshape: provide 1 to 16 dimensions");
        int shape[16];
        for(i=0;i<dims->alen;i++){
            if(dims->arr[i].t!=LT_NUM||dims->arr[i].u.n!=floor(dims->arr[i].u.n)||
               dims->arr[i].u.n<0||dims->arr[i].u.n>10000000)
                luc_error("list.reshape: dimensions must be nonnegative integers");
            shape[i]=(int)dims->arr[i].u.n;
        }
        Table *flat=tab_new(1); nums_flatten_into(t,flat,0);
        if(nums_list_product(shape,dims->alen)!=flat->alen)
            luc_error("list.reshape: dimensions do not match the number of values");
        i=0; out=nums_reshape_level(flat,shape,dims->alen,0,&i);
        RET(0,mkobj(LT_LIST,out)); return 1;
    }
    if(!strcmp(fn,"transpose")||!strcmp(fn,"axisSwap")||!strcmp(fn,"axisMove")){
        t=nums_list_arg(L,base,nargs,0,"list.transpose");
        int shape[16]={0},newshape[16]={0},perm[16],coords[16]={0},oldcoords[16]={0};
        int rank=nums_shape_value(AR(0),shape,0);
        if(rank<2) luc_error("list.%s: expected a list with at least two dimensions",fn);
        for(i=0;i<rank;i++) perm[i]=i;
        int a=0,b=1;
        if(!strcmp(fn,"axisSwap")){
            a=checkint(L,base,nargs,1,fn); b=checkint(L,base,nargs,2,fn);
        }else if(!strcmp(fn,"axisMove")){
            a=checkint(L,base,nargs,1,fn); b=checkint(L,base,nargs,2,fn);
        }
        if(a<0) a+=rank;
        if(b<0) b+=rank;
        if(a<0||a>=rank||b<0||b>=rank) luc_error("list.%s: axis out of range",fn);
        if(!strcmp(fn,"axisMove")){
            int moved=perm[a];
            if(a<b) for(i=a;i<b;i++) perm[i]=perm[i+1];
            else for(i=a;i>b;i--) perm[i]=perm[i-1];
            perm[b]=moved;
        }else{
            int temp=perm[a];perm[a]=perm[b];perm[b]=temp;
        }
        for(i=0;i<rank;i++) newshape[i]=shape[perm[i]];
        Table *flat=tab_new(1); nums_flatten_into(t,flat,0);
        out=tab_new(1);
        int total=nums_list_product(newshape,rank);
        for(int linear=0;linear<total;linear++){
            int rem=linear; 
            for(i=rank-1;i>=0;i--){
                int d=newshape[i]; coords[i]=d?rem%d:0; if(d) rem/=d;
                oldcoords[perm[i]]=coords[i];
            }
            int old_linear=0,stride=1;
            for(i=rank-1;i>=0;i--){
                old_linear+=oldcoords[i]*stride;
                stride*=shape[i];
            }
            list_push(out,flat->arr[old_linear]);
        }
        int at=0;
        Table *reshaped=nums_reshape_level(out,newshape,rank,0,&at);
        RET(0,mkobj(LT_LIST,reshaped));return 1;
    }
    if(!strcmp(fn,"join")){
        out=tab_new(1);
        if(nargs==1&&AR(0).t==LT_LIST&&AS_TAB(AR(0))->alen>0&&AS_TAB(AR(0))->arr[0].t==LT_LIST){
            Table *parts=AS_TAB(AR(0));
            for(i=0;i<parts->alen;i++){
                if(parts->arr[i].t!=LT_LIST) luc_error("list.join: expected only lists");
                t=AS_TAB(parts->arr[i]); for(j=0;j<t->alen;j++) list_push(out,t->arr[j]);
            }
        }else{
            for(i=0;i<nargs;i++){
                if(AR(i).t==LT_LIST){t=AS_TAB(AR(i));for(j=0;j<t->alen;j++)list_push(out,t->arr[j]);}
                else list_push(out,AR(i));
            }
        }
        RET(0,mkobj(LT_LIST,out)); return 1;
    }
    if(!strcmp(fn,"stack")||!strcmp(fn,"depthJoin")){
        out=tab_new(1);
        for(i=0;i<nargs;i++){
            if(AR(i).t==LT_LIST) list_push(out,mkobj(LT_LIST,nums_list_copy(AS_TAB(AR(i)))));
            else luc_error("list.stack: expected lists");
        }
        RET(0,mkobj(LT_LIST,out)); return 1;
    }
    if(!strcmp(fn,"horizontalJoin")||!strcmp(fn,"verticalJoin")){
        if(nargs<2) luc_error("list.%s: expected at least two matrices",fn);
        Table *first=nums_list_arg(L,base,nargs,0,fn); int rows=first->alen,cols=0;
        if(rows){
            if(first->arr[0].t!=LT_LIST) luc_error("list.%s: matrices must be nested lists",fn);
            cols=AS_TAB(first->arr[0])->alen;
            for(i=1;i<rows;i++)
                if(first->arr[i].t!=LT_LIST||AS_TAB(first->arr[i])->alen!=cols)
                    luc_error("list.%s: matrices must be rectangular",fn);
        }
        for(int m=0;m<nargs;m++){
            Table *mat=nums_list_arg(L,base,nargs,m,fn);
            if(!strcmp(fn,"horizontalJoin")&&mat->alen!=rows) luc_error("list.horizontalJoin: row count mismatch");
            for(i=0;i<mat->alen;i++){
                if(mat->arr[i].t!=LT_LIST) luc_error("list.%s: matrices must be nested lists",fn);
                if(AS_TAB(mat->arr[i])->alen!=(mat->alen?AS_TAB(mat->arr[0])->alen:0))
                    luc_error("list.%s: matrices must be rectangular",fn);
            }
            if(!strcmp(fn,"verticalJoin")&&mat->alen&&AS_TAB(mat->arr[0])->alen!=cols)
                luc_error("list.verticalJoin: column count mismatch");
        }
        out=tab_new(1);
        if(!strcmp(fn,"verticalJoin")){
            for(int m=0;m<nargs;m++){
                Table *mat=AS_TAB(AR(m));
                for(i=0;i<mat->alen;i++) list_push(out,mkobj(LT_LIST,nums_list_copy(AS_TAB(mat->arr[i]))));
            }
        }else{
            for(i=0;i<rows;i++){
                Table *row=tab_new(1);
                for(int m=0;m<nargs;m++){
                    Table *mat=AS_TAB(AR(m));
                    if(mat->arr[i].t!=LT_LIST) luc_error("list.horizontalJoin: matrices must be rectangular");
                    Table *part=AS_TAB(mat->arr[i]);
                    for(j=0;j<part->alen;j++) list_push(row,part->arr[j]);
                }
                list_push(out,mkobj(LT_LIST,row));
            }
        }
        RET(0,mkobj(LT_LIST,out));return 1;
    }
    if(!strcmp(fn,"append")){
        t=nums_list_arg(L,base,nargs,0,"list.append");
        for(i=1;i<nargs;i++) list_push(t,AR(i));
        RET(0,AR(0)); return 1;
    }
    if(!strcmp(fn,"insertAt")){
        t=nums_list_arg(L,base,nargs,0,"list.insertAt");
        i=checkint(L,base,nargs,1,"list.insertAt");
        if(i<0) i+=t->alen;
        if(i<0||i>t->alen) luc_error("list.insertAt: index out of range");
        list_insert(t,i+1,AR(2)); RET(0,AR(0)); return 1;
    }
    if(!strcmp(fn,"removeAt")){
        t=nums_list_arg(L,base,nargs,0,"list.removeAt");
        i=checkint(L,base,nargs,1,"list.removeAt");
        if(i<0) i+=t->alen;
        if(i<0||i>=t->alen) luc_error("list.removeAt: index out of range");
        RET(0,list_removeat(t,i+1)); return 1;
    }
    if(!strcmp(fn,"repeat")){
        Value v=AR(0); n=nums_nonnegative(checkint(L,base,nargs,1,"list.repeat"),"list.repeat");
        if(n>10000000) luc_error("list.repeat: result is too large");
        out=tab_new(1); for(i=0;i<n;i++) list_push(out,v);
        RET(0,mkobj(LT_LIST,out)); return 1;
    }
    if(!strcmp(fn,"duplicateGrid")){
        t=nums_list_arg(L,base,nargs,0,"list.duplicateGrid");
        int rows=nums_nonnegative(checkint(L,base,nargs,1,"list.duplicateGrid"),"list.duplicateGrid");
        int cols=nums_nonnegative(checkint(L,base,nargs,2,"list.duplicateGrid"),"list.duplicateGrid");
        out=tab_new(1);
        for(i=0;i<rows;i++){
            Table *row=tab_new(1);
            for(j=0;j<cols;j++) list_push(row,mkobj(LT_LIST,nums_list_copy(t)));
            list_push(out,mkobj(LT_LIST,row));
        }
        RET(0,mkobj(LT_LIST,out)); return 1;
    }
    if(!strcmp(fn,"resize")){
        t=nums_list_arg(L,base,nargs,0,"list.resize");
        n=nums_nonnegative(checkint(L,base,nargs,1,"list.resize"),"list.resize");
        if(n>10000000) luc_error("list.resize: result is too large");
        out=nums_list_copy(t);
        while(out->alen>n) list_removeat(out,out->alen);
        while(out->alen<n) list_push(out,nargs>=3?AR(2):NIL);
        RET(0,mkobj(LT_LIST,out)); return 1;
    }
    if(!strcmp(fn,"horizontalSplit")){
        t=nums_list_arg(L,base,nargs,0,"list.horizontalSplit");
        n=nums_nonnegative(checkint(L,base,nargs,1,"list.horizontalSplit"),"list.horizontalSplit");
        if(n==0) luc_error("list.horizontalSplit: number of parts must be > 0");
        int rows=t->alen,cols=rows&&t->arr[0].t==LT_LIST?AS_TAB(t->arr[0])->alen:0;
        for(i=0;i<rows;i++) if(t->arr[i].t!=LT_LIST||AS_TAB(t->arr[i])->alen!=cols)
            luc_error("list.horizontalSplit: matrix must be rectangular");
        out=tab_new(1);
        for(i=0;i<n;i++){
            int from=(int)((int64_t)cols*i/n),to=(int)((int64_t)cols*(i+1)/n);
            Table *part=tab_new(1);
            for(j=0;j<rows;j++){
                Table *row=tab_new(1),*src=AS_TAB(t->arr[j]);
                for(int c=from;c<to;c++) list_push(row,src->arr[c]);
                list_push(part,mkobj(LT_LIST,row));
            }
            list_push(out,mkobj(LT_LIST,part));
        }
        RET(0,mkobj(LT_LIST,out));return 1;
    }
    if(!strcmp(fn,"divideList")||!strcmp(fn,"verticalSplit")){
        t=nums_list_arg(L,base,nargs,0,"list.divideList");
        n=nums_nonnegative(checkint(L,base,nargs,1,"list.divideList"),"list.divideList");
        if(n==0) luc_error("list.divideList: number of parts must be > 0");
        out=tab_new(1);
        int base_size=t->alen/n, extra=t->alen%n, at=0;
        for(i=0;i<n;i++){
            int take=base_size+(i<extra);
            Table *part=tab_new(1);
            for(j=0;j<take;j++) list_push(part,t->arr[at++]);
            list_push(out,mkobj(LT_LIST,part));
        }
        RET(0,mkobj(LT_LIST,out)); return 1;
    }
    if(!strcmp(fn,"order")){
        t=nums_list_arg(L,base,nargs,0,"list.order");
        nums_sort_table(t,nargs>=2&&truthy(AR(1)));
        RET(0,AR(0)); return 1;
    }
    if(!strcmp(fn,"orderIndex")||!strcmp(fn,"rankIndex")){
        t=nums_list_arg(L,base,nargs,0,"list.orderIndex");
        int *idx=(int*)malloc((size_t)(t->alen?t->alen:1)*sizeof(int));
        if(!idx) luc_error("list.orderIndex: out of memory");
        for(i=0;i<t->alen;i++) idx[i]=i;
        for(i=1;i<t->alen;i++){
            int key=idx[i],k=i-1;
            while(k>=0&&nums_value_less(t->arr[key],t->arr[idx[k]])){idx[k+1]=idx[k];k--;}
            idx[k+1]=key;
        }
        out=tab_new(1);
        if(!strcmp(fn,"orderIndex")){
            for(i=0;i<t->alen;i++) list_push(out,mknum((double)idx[i]));
        }else{
                for(i=0;i<t->alen;i++) tab_set(out,mknum((double)idx[i]+1),mknum((double)i));
        }
        free(idx); RET(0,mkobj(LT_LIST,out)); return 1;
    }
    if(!strcmp(fn,"multiOrder")){
        t=nums_list_arg(L,base,nargs,0,"list.multiOrder");
        Table *keys=nums_list_arg(L,base,nargs,1,"list.multiOrder");
        if(t->alen>1){
            for(i=1;i<t->alen;i++){
                Value row=t->arr[i]; j=i-1;
                while(j>=0){
                    Value prev=t->arr[j]; int less=0;
                    if(row.t!=LT_LIST||prev.t!=LT_LIST) luc_error("list.multiOrder: rows must be lists");
                    Table *ra=AS_TAB(row),*rb=AS_TAB(prev);
                    for(int k=0;k<keys->alen;k++){
                        if(keys->arr[k].t!=LT_NUM) luc_error("list.multiOrder: keys must be integer indices");
                        int col=(int)keys->arr[k].u.n;
                        if(col<0||col>=ra->alen||col>=rb->alen) luc_error("list.multiOrder: key index out of range");
                        if(nums_value_less(ra->arr[col],rb->arr[col])){less=1;break;}
                        if(nums_value_less(rb->arr[col],ra->arr[col])) break;
                    }
                    if(!less) break;
                    t->arr[j+1]=t->arr[j]; j--;
                }
                t->arr[j+1]=row;
            }
        }
        RET(0,AR(0)); return 1;
    }
    if(!strcmp(fn,"splitByRank")){
        t=nums_list_arg(L,base,nargs,0,"list.splitByRank");
        n=checkint(L,base,nargs,1,"list.splitByRank");
        out=tab_new(1);
        Table *sorted=nums_list_copy(t); nums_sort_table(sorted,0);
        if(n<0) n+=sorted->alen;
        if(n<0||n>sorted->alen) luc_error("list.splitByRank: rank out of range");
        Table *left=tab_new(1),*right=tab_new(1);
        for(i=0;i<n;i++) list_push(left,sorted->arr[i]);
        for(i=n;i<sorted->alen;i++) list_push(right,sorted->arr[i]);
        list_push(out,mkobj(LT_LIST,left)); list_push(out,mkobj(LT_LIST,right));
        RET(0,mkobj(LT_LIST,out)); return 1;
    }
    if(!strcmp(fn,"insertIndex")){
        t=nums_list_arg(L,base,nargs,0,"list.insertIndex");
        Value value=AR(1); i=0; j=t->alen;
        while(i<j){ int mid=i+(j-i)/2;
            if(nums_value_less(t->arr[mid],value)) i=mid+1; else j=mid; }
        RET(0,mknum((double)i)); return 1;
    }
    if(!strcmp(fn,"maxIndex")||!strcmp(fn,"minIndex")){
        t=nums_list_arg(L,base,nargs,0,"list.index");
        if(!t->alen){ RET(0,NIL); return 1; }
        i=0;
        for(j=1;j<t->alen;j++){
            if(!strcmp(fn,"maxIndex")?nums_value_less(t->arr[i],t->arr[j]):nums_value_less(t->arr[j],t->arr[i])) i=j;
        }
        RET(0,mknum((double)i)); return 1;
    }
    if(!strcmp(fn,"select")){
        t=nums_list_arg(L,base,nargs,0,"list.select");
        Table *indices=nums_list_arg(L,base,nargs,1,"list.select"); out=tab_new(1);
        if(indices->alen==t->alen){
            int mask=1;
            for(i=0;i<indices->alen;i++) if(indices->arr[i].t!=LT_BOOL){mask=0;break;}
            if(mask){
                for(i=0;i<indices->alen;i++) if(indices->arr[i].u.b) list_push(out,t->arr[i]);
                RET(0,mkobj(LT_LIST,out));return 1;
            }
        }
        for(i=0;i<indices->alen;i++){
            if(indices->arr[i].t!=LT_NUM) luc_error("list.select: indices must be integers");
            j=(int)indices->arr[i].u.n; if(j<0) j+=t->alen;
            if(j<0||j>=t->alen) luc_error("list.select: index out of range");
            list_push(out,t->arr[j]);
        }
        RET(0,mkobj(LT_LIST,out)); return 1;
    }
    if(!strcmp(fn,"findNonzero")||!strcmp(fn,"countNonzero")){
        t=nums_list_arg(L,base,nargs,0,"list.findNonzero"); out=tab_new(1);
        for(i=0;i<t->alen;i++){
            Value v=t->arr[i]; int nonzero=v.t!=LT_NIL&&!(v.t==LT_NUM&&v.u.n==0)&&!(v.t==LT_BOOL&&!v.u.b);
            if(nonzero) list_push(out,mknum((double)i));
        }
        RET(0,!strcmp(fn,"countNonzero")?mknum((double)out->alen):mkobj(LT_LIST,out)); return 1;
    }
    if(!strcmp(fn,"distinct")){
        t=nums_list_arg(L,base,nargs,0,"list.distinct"); out=tab_new(1);
        for(i=0;i<t->alen;i++){
            for(j=0;j<out->alen;j++) if(val_rawequal(t->arr[i],out->arr[j])) break;
            if(j==out->alen) list_push(out,t->arr[i]);
        }
        RET(0,mkobj(LT_LIST,out)); return 1;
    }
    if(!strcmp(fn,"filter")){
        t=nums_list_arg(L,base,nargs,0,"list.filter"); out=tab_new(1);
        Value pred=AR(1); int scratch=base+nargs+4;
        for(i=0;i<t->alen;i++){
            ensure_stack(L,scratch+4); L->stack[scratch]=pred;
            L->stack[scratch+1]=t->arr[i]; L->stack[scratch+2]=mknum((double)i);
            vm_call(L,scratch,2,1);
            if(truthy(L->stack[scratch])) list_push(out,t->arr[i]);
        }
        RET(0,mkobj(LT_LIST,out)); return 1;
    }
    luc_error("list.%s: not implemented",fn);
    return 0;
}

static void luc_nums_open(Table *math,Table *random){
    nums_list_api=tab_new(0); nums_search_api=tab_new(0);
    nums_time_api=tab_new(0); nums_text_api=V.stringlib;
    nums_matrix_api=tab_new(0); nums_sets_api=tab_new(0); nums_finance_api=tab_new(0);
    nums_list_register(nums_list_api,"make",f_nums_list_api);
    nums_list_register(nums_list_api,"toList",f_nums_list_api);
    nums_list_register(nums_list_api,"rangeList",f_nums_list_api);
    nums_list_register(nums_list_api,"spread",f_nums_list_api);
    nums_list_register(nums_list_api,"zeroList",f_nums_list_api);
    nums_list_register(nums_list_api,"oneList",f_nums_list_api);
    nums_list_register(nums_list_api,"fillList",f_nums_list_api);
    nums_list_register(nums_list_api,"blankList",f_nums_list_api);
    nums_list_register(nums_list_api,"identity",f_nums_list_api);
    nums_list_register(nums_list_api,"diagonal",f_nums_list_api);
    nums_list_register(nums_list_api,"fromIterator",f_nums_list_api);
    nums_list_register(nums_list_api,"gridList",f_nums_list_api);
    nums_list_register(nums_list_api,"reshape",f_nums_list_api);
    nums_list_register(nums_list_api,"flatten",f_nums_list_api);
    nums_list_register(nums_list_api,"flatCopy",f_nums_list_api);
    nums_list_register(nums_list_api,"transpose",f_nums_list_api);
    nums_list_register(nums_list_api,"axisSwap",f_nums_list_api);
    nums_list_register(nums_list_api,"axisMove",f_nums_list_api);
    nums_list_register(nums_list_api,"join",f_nums_list_api);
    nums_list_register(nums_list_api,"stack",f_nums_list_api);
    nums_list_register(nums_list_api,"horizontalJoin",f_nums_list_api);
    nums_list_register(nums_list_api,"verticalJoin",f_nums_list_api);
    nums_list_register(nums_list_api,"depthJoin",f_nums_list_api);
    nums_list_register(nums_list_api,"divideList",f_nums_list_api);
    nums_list_register(nums_list_api,"horizontalSplit",f_nums_list_api);
    nums_list_register(nums_list_api,"verticalSplit",f_nums_list_api);
    nums_list_register(nums_list_api,"resize",f_nums_list_api);
    nums_list_register(nums_list_api,"append",f_nums_list_api);
    nums_list_register(nums_list_api,"insertAt",f_nums_list_api);
    nums_list_register(nums_list_api,"removeAt",f_nums_list_api);
    nums_list_register(nums_list_api,"repeat",f_nums_list_api);
    nums_list_register(nums_list_api,"duplicateGrid",f_nums_list_api);
    nums_list_register(nums_list_api,"readList",f_nums_file_api);
    nums_list_register(nums_list_api,"writeList",f_nums_file_api);
    nums_list_register(nums_list_api,"writeBundle",f_nums_file_api);
    nums_list_register(nums_list_api,"writeCompressed",f_nums_file_api);
    nums_list_register(nums_list_api,"readText",f_nums_file_api);
    nums_list_register(nums_list_api,"writeText",f_nums_file_api);
    nums_list_register(nums_list_api,"readData",f_nums_file_api);
    nums_list_register(nums_list_api,"readBinary",f_nums_file_api);
    nums_list_register(nums_list_api,"writeBinary",f_nums_file_api);
    nums_list_register(nums_list_api,"parseList",f_nums_file_api);
    nums_list_register(nums_list_api,"listString",f_nums_file_api);
    nums_list_register(nums_search_api,"order",f_nums_list_api);
    nums_list_register(nums_search_api,"orderIndex",f_nums_list_api);
    nums_list_register(nums_search_api,"multiOrder",f_nums_list_api);
    nums_list_register(nums_search_api,"splitByRank",f_nums_list_api);
    nums_list_register(nums_search_api,"rankIndex",f_nums_list_api);
    nums_list_register(nums_search_api,"insertIndex",f_nums_list_api);
    nums_list_register(nums_search_api,"maxIndex",f_nums_list_api);
    nums_list_register(nums_search_api,"minIndex",f_nums_list_api);
    nums_list_register(nums_search_api,"select",f_nums_list_api);
    nums_list_register(nums_search_api,"findNonzero",f_nums_list_api);
    nums_list_register(nums_search_api,"countNonzero",f_nums_list_api);
    nums_list_register(nums_search_api,"distinct",f_nums_list_api);
    nums_list_register(nums_search_api,"filter",f_nums_list_api);
    nums_list_register(nums_time_api,"dateTime",f_nums_time_api);
    nums_list_register(nums_time_api,"timeSpan",f_nums_time_api);
    nums_list_register(nums_time_api,"isInvalidDate",f_nums_time_api);
    nums_list_register(nums_time_api,"dateString",f_nums_time_api);
    nums_list_register(nums_time_api,"workDaysBetween",f_nums_time_api);
    nums_list_register(nums_time_api,"workDayOffset",f_nums_time_api);
    nums_list_register(nums_time_api,"isWorkDay",f_nums_time_api);
    nums_list_register(nums_time_api,"workCalendar",f_nums_time_api);
    nums_list_register(nums_text_api,"concat",f_nums_text_api);
    nums_list_register(nums_text_api,"repeatText",f_nums_text_api);
    nums_list_register(nums_text_api,"capitalize",f_nums_text_api);
    nums_list_register(nums_text_api,"alignCenter",f_nums_text_api);
    nums_list_register(nums_text_api,"lowercase",f_nums_text_api);
    nums_list_register(nums_text_api,"uppercase",f_nums_text_api);
    nums_list_register(nums_text_api,"titleCase",f_nums_text_api);
    nums_list_register(nums_text_api,"reverseCase",f_nums_text_api);
    nums_list_register(nums_text_api,"trimLeft",f_nums_text_api);
    nums_list_register(nums_text_api,"trimRight",f_nums_text_api);
    nums_list_register(nums_text_api,"replaceText",f_nums_text_api);
    nums_list_register(nums_text_api,"findText",f_nums_text_api);
    nums_list_register(nums_text_api,"countText",f_nums_text_api);
    nums_list_register(nums_text_api,"startsWith",f_nums_text_api);
    nums_list_register(nums_text_api,"endsWith",f_nums_text_api);
    nums_list_register(nums_text_api,"splitText",f_nums_text_api);
    nums_list_register(nums_text_api,"joinText",f_nums_text_api);
    nums_list_register(nums_text_api,"isLetters",f_nums_text_api);
    nums_list_register(nums_text_api,"isDigits",f_nums_text_api);
    nums_list_register(nums_text_api,"isAlphanumeric",f_nums_text_api);
    nums_list_register(nums_text_api,"zeroPad",f_nums_text_api);
    nums_list_register(nums_text_api,"encodeText",f_nums_text_api);
    nums_list_register(nums_text_api,"decodeText",f_nums_text_api);
    tab_set(V.globals,cstrv("time"),mkobj(LT_TABLE,nums_time_api));
    nums_list_register(nums_matrix_api,"dot",f_nums_matrix_api);
    nums_list_register(nums_matrix_api,"matMul",f_nums_matrix_api);
    nums_list_register(nums_matrix_api,"cross",f_nums_matrix_api);
    nums_list_register(nums_matrix_api,"det",f_nums_matrix_api);
    nums_list_register(nums_matrix_api,"inverse",f_nums_matrix_api);
    nums_list_register(nums_matrix_api,"transpose",f_nums_list_api);
    nums_list_register(nums_matrix_api,"trace",f_nums_matrix_api);
    nums_list_register(nums_matrix_api,"solve",f_nums_matrix_api);
    nums_list_register(nums_matrix_api,"eigen",f_nums_matrix_api);
    nums_list_register(nums_matrix_api,"norm",f_nums_matrix_api);
    nums_list_register(nums_matrix_api,"rank",f_nums_matrix_api);
    nums_list_register(nums_matrix_api,"qr",f_nums_matrix_api);
    nums_list_register(nums_matrix_api,"svd",f_nums_matrix_api);
    nums_list_register(nums_matrix_api,"cholesky",f_nums_matrix_api);
    nums_list_register(nums_matrix_api,"matrixPower",f_nums_matrix_api);
    nums_list_register(nums_sets_api,"union",f_nums_sets_api);
    nums_list_register(nums_sets_api,"intersect",f_nums_sets_api);
    nums_list_register(nums_sets_api,"difference",f_nums_sets_api);
    nums_list_register(nums_sets_api,"symmetricDiff",f_nums_sets_api);
    nums_list_register(nums_sets_api,"isSubset",f_nums_sets_api);
    nums_list_register(nums_sets_api,"isSuperset",f_nums_sets_api);
    nums_list_register(nums_sets_api,"isDisjoint",f_nums_sets_api);
    nums_list_register(nums_sets_api,"setEqual",f_nums_sets_api);
    {
        Value unique=tab_get(nums_search_api,cstrv("distinct"));
        tab_set(nums_sets_api,cstrv("unique"),unique);
        tab_set(nums_sets_api,cstrv("Unique"),unique);
        tab_set(nums_sets_api,cstrv("UNIQUE"),unique);
    }
    nums_list_register(nums_finance_api,"presentValue",f_nums_finance_api);
    nums_list_register(nums_finance_api,"futureValue",f_nums_finance_api);
    nums_list_register(nums_finance_api,"netPresentValue",f_nums_finance_api);
    nums_list_register(nums_finance_api,"internalRate",f_nums_finance_api);
    nums_list_register(nums_finance_api,"payment",f_nums_finance_api);
    nums_list_register(nums_finance_api,"interestRate",f_nums_finance_api);
    nums_list_register(nums_finance_api,"periodCount",f_nums_finance_api);
    nums_list_register(nums_finance_api,"principal",f_nums_finance_api);
    nums_list_register(nums_finance_api,"interest",f_nums_finance_api);
    nums_list_register(nums_finance_api,"compound",f_nums_finance_api);
    nums_list_register(nums_finance_api,"discount",f_nums_finance_api);
    Table *nums=tab_new(0);
    tab_set(nums,cstrv("math"),mkobj(LT_TABLE,math));
    tab_set(nums,cstrv("random"),mkobj(LT_TABLE,random));
    tab_set(nums,cstrv("list"),mkobj(LT_TABLE,nums_list_api));
    tab_set(nums,cstrv("search"),mkobj(LT_TABLE,nums_search_api));
    tab_set(nums,cstrv("string"),mkobj(LT_TABLE,nums_text_api));
    tab_set(nums,cstrv("time"),mkobj(LT_TABLE,nums_time_api));
    tab_set(nums,cstrv("matrix"),mkobj(LT_TABLE,nums_matrix_api));
    tab_set(nums,cstrv("sets"),mkobj(LT_TABLE,nums_sets_api));
    tab_set(nums,cstrv("finance"),mkobj(LT_TABLE,nums_finance_api));
    tab_set(V.loaded,cstrv("system:nums"),mkobj(LT_TABLE,nums));
    tab_set(V.globals,cstrv("search"),mkobj(LT_TABLE,nums_search_api));
}

static void luc_nums_bind(Table *nums){
    Value v;
    for(int i=0;i<6;i++){
        const char *name=i==0?"random":i==1?"search":i==2?"time":i==3?"matrix":i==4?"sets":"finance";
        v=tab_get(nums,cstrv(name));
        if(v.t==LT_TABLE) tab_set(V.globals,cstrv(name),v);
    }
    Table *global=AS_TAB(tab_get(V.globals,cstrv("list")));
    Value k,val; k=NIL;
    while(tab_next(nums_list_api,k,&k,&val)) tab_set(global,k,val);
    tab_set(nums,cstrv("list"),mkobj(LT_TABLE,global));
}

