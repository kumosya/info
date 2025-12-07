#include <entry.h>
#include <tty.h>
#include <serial.h>

#include <stdint.h>
#include <stdarg.h>
#include <stdio.h>

namespace tty {
	namespace video {
		static int xpos = 0;
		static int ypos = 0;

		/*  Clear the screen and initialize VIDEO, XPOS and YPOS. */
		void init() {
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
		void putchar(char c, uint8_t color) {
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
			*(video_addr + (xpos + ypos * VIDEO_WIDTH) * 2 + 1) = color;

			xpos++;
			if (xpos >= VIDEO_WIDTH)
				goto newline;
		}
	}

	static void puts(const char *s, uint8_t color) {
		while (*s)
#ifdef OUTPUT_TO_SERIAL
			serial::putc(*s++);
#else
			tty::video::putchar(*s++, color);
#endif	// OUTPUT_TO_SERIAL
	}

	/*  Format a string and print it on the screen, just like the libc
	function printf. */
	int printf(const char *fmt, ...) {
		va_list argp;
		char str[64];
		int a;
		va_start(argp, fmt);		  /*开始使用可变参数*/
		a = vsprintf(str, fmt, argp); /*格式化输出*/
		puts(str, ATTRIBUTE);					  /*输出格式化后的字符串*/
		va_end(argp);				  /*停止使用可变参数*/
		return a;
	}

	void panic(const char *fmt, ...) {
		va_list argp;
		char str[128];
		int a;
		va_start(argp, fmt);		  /*开始使用可变参数*/
		a = vsprintf(str, fmt, argp); /*格式化输出*/
		puts("KERNEL PANIC: ", 0x04);
		puts(str, 0x04);					  /*输出格式化后的字符串*/
		va_end(argp);				  /*停止使用可变参数*/
		while (true) { asm volatile("hlt"); }
	}
}
