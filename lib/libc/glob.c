/* POSIX filename matching and component-wise pathname expansion. */
#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <fnmatch.h>
#include <glob.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

static int character_class(const char* name, size_t length, unsigned char c) {
#define CLASS(text, predicate) if (length == sizeof(text)-1 && !memcmp(name,text,length)) return predicate(c)
    CLASS("alnum", isalnum); CLASS("alpha", isalpha); CLASS("blank", isblank);
    CLASS("cntrl", iscntrl); CLASS("digit", isdigit); CLASS("graph", isgraph);
    CLASS("lower", islower); CLASS("print", isprint); CLASS("punct", ispunct);
    CLASS("space", isspace); CLASS("upper", isupper); CLASS("xdigit", isxdigit);
#undef CLASS
    return 0;
}
static int bracket(const char** pattern, unsigned char c, int flags) {
    const char* p = *pattern;
    int negate = *p == '!' || *p == '^', matched = 0, first = 1;
    if (negate) ++p;
    for (; *p && (*p != ']' || first); first = 0) {
        unsigned char low;
        if (p[0] == '[' && p[1] == ':') {
            const char* end = strstr(p+2, ":]");
            if (end) { matched |= character_class(p+2, (size_t)(end-p-2), c); p = end+2; continue; }
        }
        if (*p == '\\' && !(flags & FNM_NOESCAPE) && p[1]) ++p;
        low = (unsigned char)*p++;
        if (*p == '-' && p[1] && p[1] != ']') {
            unsigned char high; ++p;
            if (*p == '\\' && !(flags & FNM_NOESCAPE) && p[1]) ++p;
            high = (unsigned char)*p++; matched |= c >= low && c <= high;
        } else matched |= c == low;
    }
    if (*p != ']') return -1;
    *pattern = p+1; return negate ? !matched : matched;
}
static int match(const char* p, const char* s, const char* start, int flags) {
    while (*p) {
        unsigned char c = (unsigned char)*p++;
        if ((c == '*' || c == '?' || c == '[') && *s == '.' && (flags & FNM_PERIOD) &&
            (s == start || ((flags & FNM_PATHNAME) && s[-1] == '/'))) return 0;
        if (c == '*') {
            while (*p == '*') ++p;
            do {
                if (match(p, s, start, flags)) return 1;
                if (!*s || ((flags & FNM_PATHNAME) && *s == '/')) break;
            } while (*s++);
            return 0;
        }
        if (!*s) return 0;
        if (c == '?') { if ((flags & FNM_PATHNAME) && *s == '/') return 0; }
        else if (c == '[') {
            int result;
            if ((flags & FNM_PATHNAME) && *s == '/') return 0;
            result = bracket(&p, (unsigned char)*s, flags);
            if (!result || (result < 0 && *s != '[')) return 0;
        } else {
            if (c == '\\' && !(flags & FNM_NOESCAPE) && *p) c = (unsigned char)*p++;
            if (c != (unsigned char)*s) return 0;
        }
        ++s;
    }
    return !*s;
}
int fnmatch(const char* pattern, const char* string, int flags) {
    return pattern && string && match(pattern, string, string, flags) ? 0 : FNM_NOMATCH;
}
static int append_path(glob_t* result, const char* path, int directory, int flags) {
    const size_t length = strlen(path);
    char* name = malloc(length + 2);
    char** vector;
    if (!name) return GLOB_NOSPACE;
    memcpy(name, path, length+1);
    if (directory && (flags & GLOB_MARK) && length && name[length-1] != '/') { name[length] = '/'; name[length+1] = 0; }
    vector = realloc(result->gl_pathv, (result->gl_offs+result->gl_pathc+2)*sizeof(char*));
    if (!vector) { free(name); return GLOB_NOSPACE; }
    result->gl_pathv = vector;
    if (!result->gl_pathc) memset(vector, 0, result->gl_offs*sizeof(char*));
    vector[result->gl_offs+result->gl_pathc++] = name;
    vector[result->gl_offs+result->gl_pathc] = NULL; return 0;
}
static int expand(const char*, const char*, int, int (*)(const char*,int), glob_t*, unsigned);
static int expand_frame(const char* pattern, const char* prefix, int flags,
                  int (*error)(const char*,int), glob_t* result, unsigned depth,
                  char* component, char* path) {
    const char* rest = pattern;
    size_t length = 0; int wildcard = 0, status = 0, trailing;
    DIR* directory; struct dirent* entry;
    if (depth > 64) return GLOB_NOSPACE;
    while (*rest && *rest != '/') {
        if (length+1 >= 384) return GLOB_NOSPACE;
        if (*rest == '\\' && !(flags & GLOB_NOESCAPE) && rest[1]) {
            component[length++] = *rest++;
            if (length+1 >= 384) return GLOB_NOSPACE;
        } else if (*rest == '*' || *rest == '?' || *rest == '[') wildcard = 1;
        component[length++] = *rest++;
    }
    component[length] = 0; trailing = *rest == '/'; while (*rest == '/') ++rest;
    if (!wildcard) {
        size_t used = strlen(prefix), i;
        if (used + length + 2 > 384) return GLOB_NOSPACE;
        strcpy(path, prefix); if (used && path[used-1] != '/') path[used++] = '/';
        for (i=0; i<length; ++i) { if (component[i] == '\\' && !(flags&GLOB_NOESCAPE) && i+1<length) ++i; path[used++] = component[i]; }
        path[used] = 0;
        if (*rest) return expand(rest, path, flags, error, result, depth+1);
        { struct stat value; if (stat(*path ? path : ".", &value) || (trailing && !S_ISDIR(value.st_mode))) return 0;
          return append_path(result, *path ? path : ".", S_ISDIR(value.st_mode), flags | (trailing ? GLOB_MARK : 0)); }
    }
    directory = opendir(*prefix ? prefix : ".");
    if (!directory) { int saved = errno; return ((error && error(*prefix?prefix:".", saved)) || (flags&GLOB_ERR)) ? GLOB_ABORTED : 0; }
    while ((entry = readdir(directory))) {
        struct stat value; size_t used = strlen(prefix), nameLength = strlen(entry->d_name);
        if (fnmatch(component, entry->d_name, FNM_PERIOD | ((flags&GLOB_NOESCAPE)?FNM_NOESCAPE:0))) continue;
        if (used + nameLength + 2 > 384) { status = GLOB_NOSPACE; break; }
        strcpy(path, prefix); if (used && path[used-1] != '/') path[used++] = '/'; strcpy(path+used, entry->d_name);
        if (stat(path, &value)) continue;
        if (*rest) { if (S_ISDIR(value.st_mode)) status = expand(rest,path,flags,error,result,depth+1); }
        else if (!trailing || S_ISDIR(value.st_mode)) status = append_path(result,path,S_ISDIR(value.st_mode),flags|(trailing?GLOB_MARK:0));
        if (status) break;
    }
    closedir(directory); return status;
}
static int expand(const char* pattern, const char* prefix, int flags,
                  int (*error)(const char*,int), glob_t* result, unsigned depth) {
    if (depth > 64) return GLOB_NOSPACE;
    char* frame = malloc(768);
    if (!frame) return GLOB_NOSPACE;
    int status = expand_frame(pattern,prefix,flags,error,result,depth,frame,frame+384);
    free(frame);
    return status;
}
int glob(const char* restrict pattern, int flags, int (*error)(const char*,int), glob_t* restrict result) {
    size_t previous, i; int status;
    if (!pattern || !result) { errno = EINVAL; return GLOB_ABORTED; }
    if (!(flags&GLOB_APPEND)) { result->gl_pathc=0; result->gl_pathv=NULL; if (!(flags&GLOB_DOOFFS)) result->gl_offs=0; }
    previous=result->gl_pathc;
    status=expand(pattern[0]=='/' ? pattern+1 : pattern, pattern[0]=='/' ? "/" : "", flags,error,result,0);
    if (status) return status;
    if (result->gl_pathc==previous) {
        if (!(flags&GLOB_NOCHECK)) return GLOB_NOMATCH;
        status=append_path(result,pattern,0,flags); if (status) return status;
    }
    if (!(flags&GLOB_NOSORT)) for(i=previous+1;i<result->gl_pathc;++i) {
        size_t j=i; char* value=result->gl_pathv[result->gl_offs+i];
        while(j>previous && strcmp(result->gl_pathv[result->gl_offs+j-1],value)>0) { result->gl_pathv[result->gl_offs+j]=result->gl_pathv[result->gl_offs+j-1]; --j; }
        result->gl_pathv[result->gl_offs+j]=value;
    }
    return 0;
}
void globfree(glob_t* result) {
    size_t i; if (!result) return;
    for(i=0;i<result->gl_pathc;++i) free(result->gl_pathv[result->gl_offs+i]);
    free(result->gl_pathv); result->gl_pathv=NULL; result->gl_pathc=0;
}
