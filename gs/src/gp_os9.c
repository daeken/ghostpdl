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
/* OSK-specific routines for Ghostscript */
#include "pipe_.h"
#include "string_.h"
#include "time_.h"
#include "gx.h"
#include "gp.h"
#include <signal.h>
#include <stdlib.h>		/* for exit */
#include <sys/param.h>		/* for MAXPATHLEN */

int interrupted;

/* Forward declarations */
private void signalhandler(P1(int));
private FILE *rbfopen(P2(char *, char *));

/* Do platform-dependent initialization */
void
gp_init(void)
{
    intercept(signalhandler);
}

/* Do platform-dependent cleanup. */
void
gp_exit(int exit_status, int code)
{
}

/* Exit the program. */
void
gp_do_exit(int exit_status)
{
}

private void
signalhandler(int sig)
{
    clearerr(stdin);
    switch (sig) {
	case SIGINT:
	case SIGQUIT:
	    interrupted = 1;
	    break;
	case SIGFPE:
	    interrupted = 2;
	    break;
	default:
	    break;
    }
}

/* ------ Date and time ------ */

/* Read the current time (in seconds since Jan. 1, 1980) */
/* and fraction (in nanoseconds). */
#define PS_YEAR_0 80
#define PS_MONTH_0 1
#define PS_DAY_0 1
void
gp_get_realtime(long *pdt)
{
    long date, time, pstime, psdate, tick;
    short day;

    _sysdate(0, &time, &date, &day, &tick);
    _julian(&time, &date);

    pstime = 0;
    psdate = (PS_YEAR_0 << 16) + (PS_MONTH_0 << 8) + PS_DAY_0;
    _julian(&pstime, &psdate);

    pdt[0] = (date - psdate) * 86400 + time;
    pdt[1] = 0;

#ifdef DEBUG_CLOCK
    printf("pdt[0] = %ld  pdt[1] = %ld\n", pdt[0], pdt[1]);
#endif
}

/* Read the current user CPU time (in seconds) */
/* and fraction (in nanoseconds).  */
void
gp_get_usertime(long *pdt)
{
    return gp_get_realtime(pdt);	/* not yet implemented */
}

/* ------ Printer accessing ------ */

/* Open a connection to a printer.  A null file name means use the */
/* standard printer connected to the machine, if any. */
/* "|command" opens an output pipe. */
/* Return NULL if the connection could not be opened. */
FILE *
gp_open_printer(char fname[gp_file_name_sizeof], int binary_mode)
{
    return
	(strlen(fname) == 0 ? 0 :
	 fname[0] == '|' ? popen(fname + 1, "w") :
	 rbfopen(fname, "w"));
}

FILE *
rbfopen(char *fname, char *perm)
{
    FILE *file = fopen(fname, perm);

    file->_flag |= _RBF;
    return file;
}

/* Close the connection to the printer. */
void
gp_close_printer(FILE * pfile, const char *fname)
{
    if (fname[0] == '|')
	pclose(pfile);
    else
	fclose(pfile);
}

/* ------ File accessing -------- */

/* Set a file into binary or text mode. */
int
gp_setmode_binary(FILE * pfile, bool binary)
{
    if (binary)
	file->_flag |= _RBF;
    else
	file->_flag &= ~_RBF;
    return 0;
}
