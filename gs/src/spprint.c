/* Portions Copyright (C) 2001 artofcode LLC.
   Portions Copyright (C) 1996, 2001 Artifex Software Inc.
   Portions Copyright (C) 1988, 2000 Aladdin Enterprises.
   This software is based in part on the work of the Independent JPEG Group.
   All Rights Reserved.

   This software is distributed under license and may not be copied, modified
   or distributed except as expressly authorized under the terms of that
   license.  Refer to licensing information at http://www.artifex.com/ or
   contact Artifex Software, Inc., 101 Lucas Valley Road #110,
   San Rafael, CA  94903, (415)492-9861, for further information. */

/*$RCSfile$ $Revision$ */
/* Print values in ASCII form on a stream */
#include "math_.h"		/* for fabs */
#include "stdio_.h"		/* for stream.h */
#include "string_.h"		/* for strchr */
#include "stream.h"
#include "spprint.h"

/* ------ Output ------ */

/* Put a byte array on a stream. */
int
stream_write(stream * s, const void *ptr, uint count)
{
    uint used;

    sputs(s, (const byte *)ptr, count, &used);
    return (int)used;
}

/* Put a string on a stream. */
int
stream_puts(stream * s, const char *str)
{
    uint len = strlen(str);
    uint used;
    int status = sputs(s, (const byte *)str, len, &used);

    return (status >= 0 && used == len ? 0 : EOF);
}

/* Print a format string up to the first variable substitution. */
/* Return a pointer to the %, or to the terminating 0 if no % found. */
private const char *
pprintf_scan(stream * s, const char *format)
{
    const char *fp = format;

    for (; *fp != 0; ++fp) {
	if (*fp == '%') {
	    if (fp[1] != '%')
		break;
	    ++fp;
	}
	sputc(s, *fp);
    }
    return fp;
}

/* Print (an) int value(s) using a format. */
const char *
pprintd1(stream * s, const char *format, int v)
{
    const char *fp = pprintf_scan(s, format);
    char str[25];

#ifdef DEBUG
    if (*fp == 0 || fp[1] != 'd')	/* shouldn't happen! */
	lprintf1("Bad format in pprintd1: %s\n", format);
#endif
    sprintf(str, "%d", v);
    stream_puts(s, str);
    return pprintf_scan(s, fp + 2);
}
const char *
pprintd2(stream * s, const char *format, int v1, int v2)
{
    return pprintd1(s, pprintd1(s, format, v1), v2);
}
const char *
pprintd3(stream * s, const char *format, int v1, int v2, int v3)
{
    return pprintd2(s, pprintd1(s, format, v1), v2, v3);
}
const char *
pprintd4(stream * s, const char *format, int v1, int v2, int v3, int v4)
{
    return pprintd2(s, pprintd2(s, format, v1, v2), v3, v4);
}

/* Print (a) floating point number(s) using a format. */
/* See gdevpdfx.h for why this is needed. */
const char *
pprintg1(stream * s, const char *format, floatp v)
{
    const char *fp = pprintf_scan(s, format);
    char str[50];

#ifdef DEBUG
    if (*fp == 0 || fp[1] != 'g')	/* shouldn't happen! */
	lprintf1("Bad format in pprintg: %s\n", format);
#endif
    sprintf(str, "%g", v);
    if (strchr(str, 'e')) {
	/* Bad news.  Try again using f-format. */
	sprintf(str, (fabs(v) > 1 ? "%1.1f" : "%1.8f"), v);
    }
    stream_puts(s, str);
    return pprintf_scan(s, fp + 2);
}
const char *
pprintg2(stream * s, const char *format, floatp v1, floatp v2)
{
    return pprintg1(s, pprintg1(s, format, v1), v2);
}
const char *
pprintg3(stream * s, const char *format, floatp v1, floatp v2, floatp v3)
{
    return pprintg2(s, pprintg1(s, format, v1), v2, v3);
}
const char *
pprintg4(stream * s, const char *format, floatp v1, floatp v2, floatp v3,
	 floatp v4)
{
    return pprintg2(s, pprintg2(s, format, v1, v2), v3, v4);
}
const char *
pprintg6(stream * s, const char *format, floatp v1, floatp v2, floatp v3,
	 floatp v4, floatp v5, floatp v6)
{
    return pprintg3(s, pprintg3(s, format, v1, v2, v3), v4, v5, v6);
}

/* Print a long value using a format. */
const char *
pprintld1(stream * s, const char *format, long v)
{
    const char *fp = pprintf_scan(s, format);
    char str[25];

#ifdef DEBUG
    if (*fp == 0 || fp[1] != 'l' || fp[2] != 'd')	/* shouldn't happen! */
	lprintf1("Bad format in pprintld: %s\n", format);
#endif
    sprintf(str, "%ld", v);
    stream_puts(s, str);
    return pprintf_scan(s, fp + 3);
}
const char *
pprintld2(stream * s, const char *format, long v1, long v2)
{
    return pprintld1(s, pprintld1(s, format, v1), v2);
}
const char *
pprintld3(stream * s, const char *format, long v1, long v2, long v3)
{
    return pprintld2(s, pprintld1(s, format, v1), v2, v3);
}

/* Print (a) string(s) using a format. */
const char *
pprints1(stream * s, const char *format, const char *str)
{
    const char *fp = pprintf_scan(s, format);

#ifdef DEBUG
    if (*fp == 0 || fp[1] != 's')	/* shouldn't happen! */
	lprintf1("Bad format in pprints: %s\n", format);
#endif
    stream_puts(s, str);
    return pprintf_scan(s, fp + 2);
}
const char *
pprints2(stream * s, const char *format, const char *str1, const char *str2)
{
    return pprints1(s, pprints1(s, format, str1), str2);
}
const char *
pprints3(stream * s, const char *format, const char *str1, const char *str2,
	 const char *str3)
{
    return pprints2(s, pprints1(s, format, str1), str2, str3);
}
