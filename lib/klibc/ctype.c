#include <ctype.h>

/*
 * isalnum - Check if character is alphanumeric
 * Returns non-zero if c is a letter or digit
 */
int isalnum(int c)
{
    return isalpha(c) || isdigit(c);
}

/*
 * isalpha - Check if character is alphabetic
 * Returns non-zero if c is a letter
 */
int isalpha(int c)
{
    return islower(c) || isupper(c);
}

/*
 * isblank - Check if character is a blank space
 * Returns non-zero if c is a space or tab
 */
int isblank(int c)
{
    return (c == ' ') || (c == '\t');
}

/*
 * iscntrl - Check if character is a control character
 * Returns non-zero if c is a control character
 */
int iscntrl(int c)
{
    return (c >= 0 && c <= 0x1F) || (c == 0x7F);
}

/*
 * isdigit - Check if character is a digit
 * Returns non-zero if c is a digit (0-9)
 */
int isdigit(int c)
{
    return (c >= '0') && (c <= '9');
}

/*
 * isgraph - Check if character is printable (except space)
 * Returns non-zero if c is a printable character (excluding space)
 */
int isgraph(int c)
{
    return (c >= '!') && (c <= '~');
}

/*
 * islower - Check if character is lowercase
 * Returns non-zero if c is a lowercase letter (a-z)
 */
int islower(int c)
{
    return (c >= 'a') && (c <= 'z');
}

/*
 * isprint - Check if character is printable
 * Returns non-zero if c is a printable character (including space)
 */
int isprint(int c)
{
    return (c >= ' ') && (c <= '~');
}

/*
 * ispunct - Check if character is punctuation
 * Returns non-zero if c is a punctuation character
 */
int ispunct(int c)
{
    return isgraph(c) && !isalnum(c);
}

/*
 * isspace - Check if character is a whitespace
 * Returns non-zero if c is a whitespace character
 */
int isspace(int c)
{
    return (c == ' ') || (c == '\t') || (c == '\n') || 
           (c == '\r') || (c == '\v') || (c == '\f');
}

/*
 * isupper - Check if character is uppercase
 * Returns non-zero if c is an uppercase letter (A-Z)
 */
int isupper(int c)
{
    return (c >= 'A') && (c <= 'Z');
}

/*
 * isxdigit - Check if character is a hexadecimal digit
 * Returns non-zero if c is a hexadecimal digit
 */
int isxdigit(int c)
{
    return (isdigit(c)) || 
           ((c >= 'a') && (c <= 'f')) || 
           ((c >= 'A') && (c <= 'F'));
}

/*
 * tolower - Convert character to lowercase
 * Returns lowercase equivalent if c is uppercase, otherwise c
 */
int tolower(int c)
{
    if (isupper(c)) {
        return c + 32;  /* 'A' - 'a' = 32 */
    }
    return c;
}

/*
 * toupper - Convert character to uppercase
 * Returns uppercase equivalent if c is lowercase, otherwise c
 */
int toupper(int c)
{
    if (islower(c)) {
        return c - 32;  /* 'a' - 'A' = 32 */
    }
    return c;
}