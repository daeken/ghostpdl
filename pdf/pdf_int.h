/* Copyright (C) 2001-2018 Artifex Software, Inc.
   All Rights Reserved.

   This software is provided AS-IS with no warranty, either express or
   implied.

   This software is distributed under license and may not be copied,
   modified or distributed except as expressly authorized under the terms
   of the license contained in the file LICENSE in this distribution.

   Refer to licensing information at http://www.artifex.com or contact
   Artifex Software, Inc.,  1305 Grant Avenue - Suite 200, Novato,
   CA 94945, U.S.A., +1(415)492-9861, for further information.
*/

#include "ghostpdf.h"
#include "pdf_types.h"

#ifndef PDF_INTERPRETER
#define PDF_INTERPRETER

int pdf_read_token(pdf_context *ctx, pdf_stream *s);
int pdf_read_object(pdf_context *ctx, pdf_stream *s, bool compressed_object);
void pdf_free_object(pdf_obj *o);

int pdf_make_name(pdf_context *ctx, byte *key, uint32_t size, pdf_obj **o);
int pdf_dict_put(pdf_dict *d, pdf_obj *Key, pdf_obj *value);
int pdf_dict_get(pdf_dict *d, char *Key, pdf_obj **o);
int pdf_array_get(pdf_array *a, uint64_t index, pdf_obj **o);
int pdf_dereference(pdf_context *ctx, uint64_t obj, uint64_t gen, pdf_obj **object);

static inline pdf_countup(pdf_obj *o)
{
    o->refcnt++;
}

static inline pdf_countdown(pdf_obj *o)
{
#ifdef DEBUG
    if (o->refcnt == 0)
        emprintf(o->memory, "Decrementing objct with recount at 0!\n");
#endif
    o->refcnt--;
    if (o->refcnt == 0)
        pdf_free_object(o);
}


#endif