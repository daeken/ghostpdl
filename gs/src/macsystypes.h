/* Copyright (C) 1994, 1995, 1997 Aladdin Enterprises.  All rights reserved.
  
  This file is part of Aladdin Ghostscript.
  
  Aladdin Ghostscript is distributed with NO WARRANTY OF ANY KIND.  No author
  or distributor accepts any responsibility for the consequences of using it,
  or for whether it serves any particular purpose or works at all, unless he
  or she says so in writing.  Refer to the Aladdin Ghostscript Free Public
  License (the "License") for full details.
  
  Every copy of Aladdin Ghostscript must include a copy of the License,
  normally in a plain ASCII text file named PUBLIC.  The License grants you
  the right to copy, modify and redistribute Aladdin Ghostscript, but only
  under certain conditions described in the License.  Among other things, the
  License requires that the copyright notice and this notice be preserved on
  all copies.
*/

/* $Id$ */

#ifndef __sys_types_h__
#define __sys_types_h__

#include <MacTypes.h>
#include <unix.h>
#define CHECK_INTERRUPTS

/* use a 64 bit type for color vectors. (from MacTypes.h)
   this is important for devicen support, but can be safely
   undef'd to fallback to a 32 bit representation  */
#define GX_COLOR_INDEX_TYPE UInt64

#define main gs_main

#if (0)
#define fprintf myfprintf
#define fputs myfputs
#define getenv mygetenv
int myfprintf(FILE *file, const char *fmt, ...);
int myfputs(const char *string, FILE *file);
#endif

/* Metrowerks CodeWarrior should define this */
#ifndef __MACOS__
#define __MACOS__
#endif

#endif /* __sys_types_h__ */
