#include <stdio.h>
#include <stdarg.h>
#include <stddef.h>
#include <string.h>

#define LEFT        1  /* 左对齐标志 */
#define PLUS        2  /* 显示加号标志 */
#define SPACE       4  /* 空格填充标志 */
#define SPECIAL     8  /* 特殊格式标志（如0x前缀） */
#define ZEROPAD    16  /* 零填充标志 */
#define SIGN       32  /* 有符号数标志 */
#define SMALL      64  /* 小写字母标志 */


/* 判断字符是否为数字 */
static int is_digit(int c)
{
    return c >= '0' && c <= '9';
}

/* 跳过数字并将其转换为整数 */
static int skip_atoi(const char **s)
{
    int i = 0;

    while (is_digit(**s))
    {
        i = i * 10 + *((*s)++) - '0';
    }

    return i;
}

/* 将数字转换为字符串并填充格式 */
static char *number(char *str, unsigned long num, int base, int size, int precision, int flags)
{
    char c, sign, tmp[66];
    const char *digits = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ";
    int i;

    if (flags & SMALL)
    {
        digits = "0123456789abcdefghijklmnopqrstuvwxyz";
    }

    if (flags & LEFT)
    {
        flags &= ~ZEROPAD;
    }

    if (base < 2 || base > 36)
    {
        return 0;
    }

    c = (flags & ZEROPAD) ? '0' : ' ';
    sign = 0;

    if (flags & SIGN)
    {
        if ((long)num < 0)
        {
            sign = '-';
            num = -num;
            size--;
        }
        else if (flags & PLUS)
        {
            sign = '+';
            size--;
        }
        else if (flags & SPACE)
        {
            sign = ' ';
            size--;
        }
    }

    if (flags & SPECIAL)
    {
        if (base == 8)
        {
            size--;
        }
        else if (base == 16)
        {
            size -= 2;
        }
    }

    i = 0;
    if (num == 0)
    {
        tmp[i++] = '0';
    }
    else
    {
        while (num != 0)
        {
            tmp[i++] = digits[((unsigned long)num) % base];
            num = ((unsigned long)num) / base;
        }
    }

    if (i > precision)
    {
        precision = i;
    }

    size -= precision;

    if (!(flags & (ZEROPAD | LEFT)))
    {
        while (size-- > 0)
        {
            *str++ = ' ';
        }
    }

    if (sign)
    {
        *str++ = sign;
    }

    if (flags & SPECIAL)
    {
        if (base == 8)
        {
            *str++ = '0';
        }
        else if (base == 16)
        {
            *str++ = '0';
            *str++ = (flags & SMALL) ? 'x' : 'X';
        }
    }

    if (!(flags & LEFT))
    {
        while (size-- > 0)
        {
            *str++ = c;
        }
    }

    while (i < precision--)
    {
        *str++ = '0';
    }

    while (i-- > 0)
    {
        *str++ = tmp[i];
    }

    while (size-- > 0)
    {
        *str++ = ' ';
    }

    return str;
}

int vsprintf(char *buf, const char *fmt, va_list args)
{
    int len, i;
    char *str, *s;
    int *ip;
    int flags, field_width, precision;
//    int qualifier;

    for (str = buf; *fmt; ++fmt)
    {
        if (*fmt != '%')
        {
            *str++ = *fmt;
            continue;
        }

        flags = 0;
    repeat:
        ++fmt;
        switch (*fmt)
        {
        case '-':
            flags |= LEFT;
            goto repeat;
        case '+':
            flags |= PLUS;
            goto repeat;
        case ' ':
            flags |= SPACE;
            goto repeat;
        case '#':
            flags |= SPECIAL;
            goto repeat;
        case '0':
            flags |= ZEROPAD;
            goto repeat;
        }

        field_width = -1;
        if (is_digit(*fmt))
            field_width = skip_atoi(&fmt);
        else if (*fmt == '*')
        {
            field_width = va_arg(args, int);
            if (field_width < 0)
            {
                field_width = -field_width;
                flags |= LEFT;
            }
        }

        precision = -1;
        if (*fmt == '.')
        {
            ++fmt;
            if (is_digit(*fmt))
                precision = skip_atoi(&fmt);
            else if (*fmt == '*')
            {
                precision = va_arg(args, int);
            }
            if (precision < 0)
                precision = 0;
        }

//        qualifier = -1;
        if (*fmt == 'h' || *fmt == 'l' || *fmt == 'L')
        {
//            qualifier = *fmt;
            ++fmt;
        }

        switch (*fmt) {
        case 'c':
            if (!(flags & LEFT))
                while (--field_width > 0)
                    *str++ = ' ';
            *str++ = (unsigned char)va_arg(args, int);
            while (--field_width > 0)
                *str++ = ' ';
            break;
        case 's':
            s = va_arg(args, char *);
            len = strlen(s);
            if (precision < 0)
                precision = len;
            else if (len > precision)
                len = precision;
            if (!(flags & LEFT))
                while (len < field_width--)
                    *str++ = ' ';
            for (i = 0; i < len; ++i)
                *str++ = *s++;
            while (len < field_width--)
                *str++ = ' ';
            break;
        case 'o':
            str = number(str, va_arg(args, unsigned long), 8, field_width, precision, flags);
            break;
        case 'p':
            if (field_width == -1)
            {
                field_width = 8;
                flags |= ZEROPAD;
            }
            str = number(str, (unsigned long long)va_arg(args, void *), 16, field_width, precision, flags);
            break;
        case 'x':
            flags |= SMALL;
            str = number(str, va_arg(args, unsigned long), 16, field_width, precision, flags);
            break;
        case 'X':
            str = number(str, va_arg(args, unsigned long), 16, field_width, precision, flags);
            break;
        case 'd':
        case 'i':
            flags |= SIGN;
            str = number(str, va_arg(args, unsigned long), 10, field_width, precision, flags);
            break;
        case 'u':
            str = number(str, va_arg(args, unsigned long), 10, field_width, precision, flags);
            break;
        case 'n':
            ip = va_arg(args, int *);
            *ip = (str - buf);
            break;
        default:
            if (*fmt != '%')
                *str++ = '%';
            if (*fmt)
                *str++ = *fmt;
            else
                --fmt;
            break;
        }
    }
    *str = '\0';
    return str - buf;
}


int sprintf(char *str, const char *format, ...)
{
    va_list args;
    int ret;
    
    va_start(args, format);
    ret = vsprintf(str, format, args);
    va_end(args);
    
    return ret;
}


/*
 * vsnprintf - Format a bufing into a strfer with size limitation using va_list
 * Returns the number of characters that would have been written if the strfer was large enough
 */
int vsnprintf(char *str, size_t size, const char *format, va_list args)
{
    int len, i;
    char *buf, *s;
    int *ip;
    int flags, field_width, precision;
//    int qualifier;

    for (buf = str; *format; ++format)
    {
        if (buf - str >= (ssize_t)(size - 1))
        {
            break;
        }

        if (*format != '%')
        {
            *buf++ = *format;
            continue;
        }

        flags = 0;
    repeat:
        ++format;
        switch (*format)
        {
        case '-':
            flags |= LEFT;
            goto repeat;
        case '+':
            flags |= PLUS;
            goto repeat;
        case ' ':
            flags |= SPACE;
            goto repeat;
        case '#':
            flags |= SPECIAL;
            goto repeat;
        case '0':
            flags |= ZEROPAD;
            goto repeat;
        }

        field_width = -1;
        if (is_digit(*format))
            field_width = skip_atoi(&format);
        else if (*format == '*')
        {
            field_width = va_arg(args, int);
            if (field_width < 0)
            {
                field_width = -field_width;
                flags |= LEFT;
            }
        }

        precision = -1;
        if (*format == '.')
        {
            ++format;
            if (is_digit(*format))
                precision = skip_atoi(&format);
            else if (*format == '*')
            {
                precision = va_arg(args, int);
            }
            if (precision < 0)
                precision = 0;
        }

//        qualifier = -1;
        if (*format == 'h' || *format == 'l' || *format == 'L')
        {
//            qualifier = *format;
            ++format;
        }

        switch (*format) {
        case 'c':
            if (!(flags & LEFT))
                while (--field_width > 0)
                    *buf++ = ' ';
            *buf++ = (unsigned char)va_arg(args, int);
            while (--field_width > 0)
                *buf++ = ' ';
            break;
        case 's':
            s = va_arg(args, char *);
            len = strlen(s);
            if (precision < 0)
                precision = len;
            else if (len > precision)
                len = precision;
            if (!(flags & LEFT))
                while (len < field_width--)
                    *buf++ = ' ';
            for (i = 0; i < len; ++i)
                *buf++ = *s++;
            while (len < field_width--)
                *buf++ = ' ';
            break;
        case 'o':
            buf = number(buf, va_arg(args, unsigned long), 8, field_width, precision, flags);
            break;
        case 'p':
            if (field_width == -1)
            {
                field_width = 8;
                flags |= ZEROPAD;
            }
            buf = number(buf, (unsigned long long)va_arg(args, void *), 16, field_width, precision, flags);
            break;
        case 'x':
            flags |= SMALL;
            buf = number(buf, va_arg(args, unsigned long), 16, field_width, precision, flags);
            break;
        case 'X':
            buf = number(buf, va_arg(args, unsigned long), 16, field_width, precision, flags);
            break;
        case 'd':
        case 'i':
            flags |= SIGN;
            buf = number(buf, va_arg(args, unsigned long), 10, field_width, precision, flags);
            break;
        case 'u':
            buf = number(buf, va_arg(args, unsigned long), 10, field_width, precision, flags);
            break;
        case 'n':
            ip = va_arg(args, int *);
            *ip = (buf - str);
            break;
        default:
            if (*format != '%')
                *buf++ = '%';
            if (*format)
                *buf++ = *format;
            else
                --format;
            break;
        }
    }
    *buf = '\0';
    return buf - str;
}

/*
 * snprintf - Format a bufing into a strfer with size limitation
 * Returns the number of characters that would have been written if the strfer was large enough
 */
int snprintf(char *buf, size_t size, const char *format, ...)
{
    va_list args;
    int ret;
    
    va_start(args, format);
    ret = vsnprintf(buf, size, format, args);
    va_end(args);
    
    return ret;
}

