/* Copyright (C) 1990, 2000 Aladdin Enterprises.  All rights reserved.

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

/*$Id$ */
/* Level 2 binary object sequence operators */
#include "memory_.h"
#include "ghost.h"
#include "gxalloc.h"		/* for names_array in allocator */
#include "oper.h"
#include "ialloc.h"
#include "istruct.h"
#include "btoken.h"
#include "store.h"

/*
 * Define the structure that holds the t_*array ref for the system or
 * user name table.
 */
typedef struct names_array_ref_s {
    ref names;
} names_array_ref_t;
gs_private_st_ref_struct(st_names_array_ref, names_array_ref_t,
			 "names_array_ref_t");

/* Initialize the binary token machinery. */
private int
zbseq_init(i_ctx_t *i_ctx_p)
{
    /* Initialize fake system and user name tables. */
    /* PostScript code will install the real system name table. */
    ref *psystem_names = (ref *)
	gs_alloc_struct(gs_memory_stable(imemory_global), names_array_ref_t,
			&st_names_array_ref, "zbseq_init(system_names)");
    ref *puser_names = (ref *)
	gs_alloc_struct(gs_memory_stable(imemory_local), names_array_ref_t,
			&st_names_array_ref, "zbseq_init(user_names)");

    if (psystem_names == 0 || puser_names == 0)
	return_error(e_VMerror);
    make_empty_array(psystem_names, a_readonly);
    make_empty_array(puser_names, a_all);
    system_names_p = psystem_names;
    user_names_p = puser_names;
    return 0;
}

/* <names> .installsystemnames - */
private int
zinstallsystemnames(i_ctx_t *i_ctx_p)
{
    os_ptr op = osp;

    if (r_space(op) != avm_global || imemory_save_level(iimemory_global) != 0)
	return_error(e_invalidaccess);
    check_read_type(*op, t_shortarray);
    ref_assign_old(NULL, system_names_p, op, ".installsystemnames");
    pop(1);
    return 0;
}

/* - currentobjectformat <int> */
private int
zcurrentobjectformat(i_ctx_t *i_ctx_p)
{
    os_ptr op = osp;

    push(1);
    *op = ref_binary_object_format;
    return 0;
}

/* <int> setobjectformat - */
private int
zsetobjectformat(i_ctx_t *i_ctx_p)
{
    os_ptr op = osp;
    ref cont;

    check_type(*op, t_integer);
    if (op->value.intval < 0 || op->value.intval > 4)
	return_error(e_rangecheck);
    make_struct(&cont, avm_local, ref_binary_object_format_container);
    ref_assign_old(&cont, &ref_binary_object_format, op, "setobjectformat");
    pop(1);
    return 0;
}

/* <ref_offset> <char_offset> <obj> <string8> .bosobject */
/*   <ref_offset'> <char_offset'> <string8> */
/*
 * This converts a single object to its binary object sequence
 * representation, doing the dirty work of printobject and writeobject.
 * (The main control is in PostScript code, so that we don't have to worry
 * about interrupts or callouts in the middle of writing the various data
 * items.)  Note that this may or may not modify the 'unused' field.
 */

private int
zbosobject(i_ctx_t *i_ctx_p)
{
    os_ptr op = osp;
    int code;

    check_type(op[-3], t_integer);
    check_type(op[-2], t_integer);
    check_write_type(*op, t_string);
    if (r_size(op) < 8)
	return_error(e_rangecheck);
    code = encode_binary_token(i_ctx_p, op - 1, &op[-3].value.intval,
			       &op[-2].value.intval, op->value.bytes);
    if (code < 0)
	return code;
    op[-1] = *op;
    r_set_size(op - 1, 8);
    pop(1);
    return 0;
}

/* ------ Initialization procedure ------ */

const op_def zbseq_l2_op_defs[] =
{
    op_def_begin_level2(),
    {"1.installsystemnames", zinstallsystemnames},
    {"0currentobjectformat", zcurrentobjectformat},
    {"1setobjectformat", zsetobjectformat},
    {"4.bosobject", zbosobject},
    op_def_end(zbseq_init)
};
