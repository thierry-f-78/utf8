/*
 * Copyright 2019-2019 Thierry FOURNIER
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License.
 *
 */
#include <ctype.h>
#include <netdb.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#define UTF8_CODE_OK       0x00000000
#define UTF8_CODE_OVERLONG 0x10000000
#define UTF8_CODE_INVRANGE 0x20000000
#define UTF8_CODE_BADSEQ   0x40000000

static inline unsigned int utf8_return_code(unsigned int code)
{
	return code & 0xffff0000;
}

static inline unsigned int utf8_return_length(unsigned int code)
{
	return code & 0x0000ffff;
}

/* This function read the next valid utf8 char.
 * <s> is the byte srray to be decode, <len> is its length.
 * The function returns decoded char encoded like this:
 * The 16 msb are the return code (UTF8_CODE_*), the 16 lsb
 * are the length read. The decoded character is stored in <c>.
 */
unsigned int utf8_next(const char *s, int len, unsigned int *c)
{
	const unsigned char *p = (unsigned char *)s;
	int dec;
	unsigned int code = UTF8_CODE_OK;

	if (len < 1)
		return UTF8_CODE_OK;

	/* Check the type of UTF8 sequence
	 *
	 * 0... ....  0x00 <= x <= 0x7f : 1 byte: ascii char
	 * 10.. ....  0x80 <= x <= 0xbf : invalid sequence
	 * 110. ....  0xc0 <= x <= 0xdf : 2 bytes
	 * 1110 ....  0xe0 <= x <= 0xef : 3 bytes
	 * 1111 0...  0xf0 <= x <= 0xf7 : 4 bytes
	 * 1111 10..  0xf8 <= x <= 0xfb : 5 bytes
	 * 1111 110.  0xfc <= x <= 0xfd : 6 bytes
	 * 1111 111.  0xfe <= x <= 0xff : invalid sequence
	 */
	switch (*p) {
	case 0x00 ... 0x7f:
		*c = *p;
		return UTF8_CODE_OK | 1;

	case 0x80 ... 0xbf:
		*c = *p;
		return UTF8_CODE_BADSEQ | 1;

	case 0xc0 ... 0xdf:
		if (len < 2) {
			*c = *p;
			return UTF8_CODE_BADSEQ | 1;
		}
		*c = *p & 0x1f;
		dec = 1;
		break;

	case 0xe0 ... 0xef:
		if (len < 3) {
			*c = *p;
			return UTF8_CODE_BADSEQ | 1;
		}
		*c = *p & 0x0f;
		dec = 2;
		break;

	case 0xf0 ... 0xf7:
		if (len < 4) {
			*c = *p;
			return UTF8_CODE_BADSEQ | 1;
		}
		*c = *p & 0x07;
		dec = 3;
		break;

	case 0xf8 ... 0xfb:
		if (len < 5) {
			*c = *p;
			return UTF8_CODE_BADSEQ | 1;
		}
		*c = *p & 0x03;
		dec = 4;
		break;

	case 0xfc ... 0xfd:
		if (len < 6) {
			*c = *p;
			return UTF8_CODE_BADSEQ | 1;
		}
		*c = *p & 0x01;
		dec = 5;
		break;

	case 0xfe ... 0xff:
	default:
		*c = *p;
		return UTF8_CODE_BADSEQ | 1;
	}

	p++;

	while (dec > 0) {

		/* need 0x10 for the 2 first bits */
		if ( ( *p & 0xc0 ) != 0x80 )
			return UTF8_CODE_BADSEQ | ((p-(unsigned char *)s)&0xffff);

		/* add data at char */
		*c = ( *c << 6 ) | ( *p & 0x3f );

		dec--;
		p++;
	}

	/* Check ovelong encoding.
	 * 1 byte  : 5 + 6         : 11 : 0x80    ... 0x7ff
	 * 2 bytes : 4 + 6 + 6     : 16 : 0x800   ... 0xffff
	 * 3 bytes : 3 + 6 + 6 + 6 : 21 : 0x10000 ... 0x1fffff
	 */
	if ((*c >= 0x00    && *c <= 0x7f     && (p-(unsigned char *)s) > 1) ||
	    (*c >= 0x80    && *c <= 0x7ff    && (p-(unsigned char *)s) > 2) ||
	    (*c >= 0x800   && *c <= 0xffff   && (p-(unsigned char *)s) > 3) ||
	    (*c >= 0x10000 && *c <= 0x1fffff && (p-(unsigned char *)s) > 4))
		code |= UTF8_CODE_OVERLONG;

	/* Check invalid UTF8 range. */
	if ((*c >= 0xd800 && *c <= 0xdfff) ||
	    (*c >= 0xfffe && *c <= 0xffff))
		code |= UTF8_CODE_INVRANGE;

	return code | ((p-(unsigned char *)s)&0xffff);
}

int main(void)
{
	char buffer[1024*1024];
	int len = 0;
	int ret;
	int i;
	unsigned int c;
	unsigned int retc;

	while (1) {
		ret = read(0, buffer + len, (1024*1024)-len);
		if (ret == -1)
			exit(1);
		if (ret == 0)
			break;
		len += ret;
	}

	i = 0;
	while (i < len) {
		retc = utf8_next(buffer+i, len-i, &c);

		switch(utf8_return_code(retc)) {
		case UTF8_CODE_OK:
		case UTF8_CODE_OVERLONG:
			fwrite((unsigned char *)&c, 1, 1, stdout);
			break;
		case UTF8_CODE_INVRANGE:
		case UTF8_CODE_BADSEQ:
			fwrite(buffer+i, utf8_return_length(retc), 1, stdout);
			break;
		}
		
		i += utf8_return_length(retc);
	}
}
