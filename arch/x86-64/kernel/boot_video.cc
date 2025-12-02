#include <entry.h>
#include <multiboot2.h>

#include <stdint.h>
#include <stdarg.h>

namespace boot {
	static int xpos = 0;
	static int ypos = 0;

	/*  Clear the screen and initialize VIDEO, XPOS and YPOS. */
	void video_init() {
		video_addr = (uint8_t *)VIDEO_ADDR;
		int i;
		for (i = 0; i < VIDEO_HEIGHT * VIDEO_WIDTH * 2; i++)
		{
			*(video_addr + i) = 0;
		}
		xpos = 0;
		ypos = 0;
	}

	/*  Put a character on the screen. */
	void putchar(char c) {
		if (c == '\n' || c == '\r')
		{
		newline:
			xpos = 0;
			ypos++;
			if (ypos >= VIDEO_HEIGHT)
				ypos = 0;
			return;
		}

		*(video_addr + (xpos + ypos * VIDEO_WIDTH) * 2) = c & 0xFF;
		*(video_addr + (xpos + ypos * VIDEO_WIDTH) * 2 + 1) = ATTRIBUTE;

		xpos++;
		if (xpos >= VIDEO_WIDTH)
			goto newline;
	}

	static void puts(const char *s) {
		while (*s)
			putchar(*s++);
	}

	/*  Format a string and print it on the screen, just like the libc
	function printf. */
	int printf(const char *fmt, ...) {
		va_list argp;
		char str[50];
		int a;
		va_start(argp, fmt);		  /*开始使用可变参数*/
		a = vsprintf(str, fmt, argp); /*格式化输出*/
		puts(str);					  /*输出格式化后的字符串*/
		va_end(argp);				  /*停止使用可变参数*/
		return (a);
	}

	static size_t strlen(const char *s)
	{
		const char *p = s;

		while (*p != '\0')
		{
			p++;
		}

		return (size_t)(p - s);
	}

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

	static int vsprintf(char *buf, const char *fmt, va_list args)
	{
		int len, i;
		char *str, *s;
		int *ip;
		int flags, field_width, precision, qualifier;

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

			qualifier = -1;
			if (*fmt == 'h' || *fmt == 'l' || *fmt == 'L')
			{
				qualifier = *fmt;
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
			case 'X':
				str = number(str, va_arg(args, unsigned long), 16, field_width, precision, flags);
				break;
			case 'd':
			case 'i':
				flags |= SIGN;
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

}
