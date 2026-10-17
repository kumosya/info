#include <ctype.h>

int isalpha(int character) {
    return (character >= 'A' && character <= 'Z') ||
           (character >= 'a' && character <= 'z');
}

int isdigit(int character) { return character >= '0' && character <= '9'; }
int isalnum(int character) { return isalpha(character) || isdigit(character); }
int isspace(int character) {
    return character == ' ' || character == '\t' || character == '\n' ||
           character == '\r' || character == '\f' || character == '\v';
}
int isprint(int character) { return character >= 32 && character <= 126; }
int isblank(int c) { return c == ' ' || c == '\t'; }
int iscntrl(int c) { return (c >= 0 && c < 32) || c == 127; }
int isgraph(int c) { return c > 32 && c < 127; }
int islower(int c) { return c >= 'a' && c <= 'z'; }
int isupper(int c) { return c >= 'A' && c <= 'Z'; }
int ispunct(int c) { return isgraph(c) && !isalnum(c); }
int isxdigit(int c) { return isdigit(c) || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F'); }
int tolower(int character) {
    return character >= 'A' && character <= 'Z' ? character - 'A' + 'a'
                                                 : character;
}
int toupper(int character) {
    return character >= 'a' && character <= 'z' ? character - 'a' + 'A'
                                                 : character;
}
