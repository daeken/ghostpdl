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

/* Stack operations for the PDF interpreter */

#include "ghostpdf.h"
#include "pdf_types.h"
#include "pdf_int.h"
#include "pdf_stack.h"

int pdfi_pop(pdf_context *ctx, int num)
{
    while(num) {
        if (ctx->stack_top > ctx->stack_bot) {
            pdfi_countdown(ctx->stack_top[-1]);
            ctx->stack_top--;
        } else {
            return_error(gs_error_stackunderflow);
        }
        num--;
    }
    return 0;
}

int pdfi_push(pdf_context *ctx, pdf_obj *o)
{
    pdf_obj **new_stack;
    uint32_t entries = 0;

    if (ctx->stack_top < ctx->stack_bot)
        ctx->stack_top = ctx->stack_bot;

    if (ctx->stack_top >= ctx->stack_limit) {
        if (ctx->stack_size >= MAX_STACK_SIZE)
            return_error(gs_error_stackoverflow);

        new_stack = (pdf_obj **)gs_alloc_bytes(ctx->memory, (ctx->stack_size + INITIAL_STACK_SIZE) * sizeof (pdf_obj *), "pdfi_push_increase_interp_stack");
        if (new_stack == NULL)
            return_error(gs_error_VMerror);

        memcpy(new_stack, ctx->stack_bot, ctx->stack_size * sizeof(pdf_obj *));
        gs_free_object(ctx->memory, ctx->stack_bot, "pdfi_push_increase_interp_stack");

        entries = ctx->stack_top - ctx->stack_bot;

        ctx->stack_bot = new_stack;
        ctx->stack_top = ctx->stack_bot + entries;
        ctx->stack_size += INITIAL_STACK_SIZE;
        ctx->stack_limit = ctx->stack_bot + ctx->stack_size;
    }

    *ctx->stack_top = o;
    ctx->stack_top++;
    pdfi_countup(o);

    return 0;
}

int pdfi_mark_stack(pdf_context *ctx, pdf_obj_type type)
{
    pdf_obj *o;
    int code;

    if (type != PDF_ARRAY_MARK && type != PDF_DICT_MARK && type != PDF_PROC_MARK)
        return_error(gs_error_typecheck);

    o = (pdf_obj *)gs_alloc_bytes(ctx->memory, sizeof(pdf_obj), "Allocate a stack mark");
    if (o == NULL)
        return_error(gs_error_VMerror);

    memset(o, 0x00, sizeof(pdf_obj));
    o->memory = ctx->memory;
    o->type = type;
    o->refcnt = 1;
#if REFCNT_DEBUG
    o->UID = ctx->UID++;
#endif
    code = pdfi_push(ctx, o);
    pdfi_countdown(o);
    return code;
}

void pdfi_clearstack(pdf_context *ctx)
{
    pdfi_pop(ctx, ctx->stack_top - ctx->stack_bot);
}

int pdfi_count_to_mark(pdf_context *ctx, uint64_t *count)
{
    pdf_obj *o = ctx->stack_top[- 1];
    int index = -1;

    *count = 0;
    while (&ctx->stack_top[index] >= ctx->stack_bot) {
        if (o->type == PDF_ARRAY_MARK || o->type == PDF_DICT_MARK)
            return 0;
        o = ctx->stack_top[--index];
        (*count)++;
    }
    return_error(gs_error_unmatchedmark);
}

int pdfi_clear_to_mark(pdf_context *ctx)
{
    int code;
    uint64_t count;

    code = pdfi_count_to_mark(ctx, &count);
    if (code < 0)
        return code;
    return pdfi_pop(ctx, count + 1);
}