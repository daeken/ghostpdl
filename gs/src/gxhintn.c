/* Copyright (C) 1990, 2000 Aladdin Enterprises.  All rights reserved.
  
  This software is provided AS-IS with no warranty, either express or
  implied.
  
  This software is distributed under license and may not be copied,
  modified or distributed except as expressly authorized under the terms
  of the license contained in the file LICENSE in this distribution.
  
  For more information about licensing, please refer to
  http://www.ghostscript.com/licensing/. For information on
  commercial licensing, go to http://www.artifex.com/licensing/ or
  contact Artifex Software, Inc., 101 Lucas Valley Road #110,
  San Rafael, CA  94903, U.S.A., +1(415)492-9861.
*/

/* $Id$ */
/* Type 1 hinter, a new algorithm */

#include <assert.h>
#include <limits.h>
#include <memory.h>
#include "stdpre.h"
#include "math_.h"
#include "gx.h"
#include "gxfixed.h"
#include "gxarith.h"
#include "gstypes.h"
#include "gxmatrix.h"
#include "gxpath.h"
#include "gxfont.h"
#include "gxfont1.h"
#include "gxtype1.h"
#include "gxhintn.h"
#include "gzpath.h"
#include "gserrors.h"
#include "vdtrace.h"

/*  todo :
    - Diagonal stems are not hinted;
    - Some fonts have no StdHW, StdWW. Adobe appears to autohint them.
    - Measure Adobe's flattness parameter.
    - Adobe looks adjusting the relative stem length.
    - Test Adobe compatibility for rotated/skewed glyphs.
    - Fix glyph placement along X-coordinate.
    - Test this code with fixed_shift != 12.
    - Remove rudiments of old code from gstype1.c, gstype2.c .
 */



/*  Stem processing basics :
    (See the glyph AE in Times-Roman by Adobe.)

    0. This supposes that glyph is transformed to device space
       with random matrix.

       All outline poles and all hint commands are stored in arrays
       before staring the exact processing.

       HR pole is pole before which stem replacement happens.

    1. Stem hints may be primary ones (defined in the beginning of charstring),
       and secondary ones (defined at HR poles). Consider that
       secondary stem hints may be redundant (see AE in Times-Roman).
       Secondary stems are HIGHER priority than basic ones.

    2. The range of secondary stem command is from its HR pole to next HR pole.
       The range of primary stem command is entire glyph.

    3. If a stem boundary corresponds to a pole aligned with alignment zone,
       pass aligned coordinate to the stem command. 
       Use stem boundary longitude middle point for alignment with
       skewed or rotated matrix. Use standard stem width for computing 
       opposite coordinates.

    4. Considering each set of repeating stem commands as stem complex, pass
       alignment coordinates to opposite boundaries of stem commands.

    5. Align stem3 stems each to another.

    6. Pass aligned boundary coordinate to poles within stem command range.
       Note that this will pass aligned coordinates back to poles,
       from which stem alignment was taken, meanwhile
       stem3 alignment may change them.

    7. Interpolate unaligned poles.

    8. After alignment is done, it is desirable to check for 
       anomalous negative contours and fix them, but we have no
       good algorithm for this. The rasterizer must be tolerant
       to such contours (which may have self-crosses, self-contacts,
       or may change to opposite direction).

*/

/*  Dotsection processing basics :

    If stem replacement occures, dotsection to be ignored.
    To check this properly, we test whether extremal poles of contour 
    were actually aligned with stem hints.

    If contour was aligned with stem hints by both X and Y,
    no special processing required.

    Otherwise if dotsection center falls near vstem axis,
    we align it by X with the axis. Otherwise we align
    it by X to half-pixel. Then we align the center by Y to
    half-pixel, and shift entire contour to satisfy 
    the alignment of the center.

*/

/*  vstem3/hstem3 processing basics :
    They are handled by the type 1,2 interpreters (gstype1.c, gstype2.c).
 */

/*  flex processing basics :
    With type 1 it is handled with t1_hinter__flex_* functions.
    With type 2 it is handled by gstype2.c .
 */

#define VD_DRAW_IMPORT 0 /* CAUTION: with 1 can't close DC on import error */
#define VD_SCALE  (0.2 / 4096.0)
#define VD_SHIFT_X 50
#define VD_SHIFT_Y 100
#define VD_PAINT_POLE_IDS 1
#define VD_IMPORT_COLOR RGB(255, 0, 0)

#define ADOBE_OVERSHOOT_COMPATIBILIY 0
#define ADOBE_SHIFT_CHARPATH 0

static const char *s_pole_array = "t1_hinter pole array";
static const char *s_zone_array = "t1_hinter zone array";
static const char *s_hint_array = "t1_hinter hint array";
static const char *s_contour_array = "t1_hinter contour array";
static const char *s_hint_range_array = "t1_hinter hint_range array";
static const char *s_stem_snap_array = "t1_hinter stem_snap array";

#define member_prt(type, ptr, offset) (type *)((char *)(ptr) + (offset))

typedef int32 int24;

private const unsigned int split_bits = 12;
private const unsigned int max_coord_bits = 24; /* = split_bits * 2 */
private const unsigned int matrix_bits = 19; /* <= sizeof(int) * 8 - 1 - split_bits */
private const unsigned int g2o_bitshift = 12; /* <= matrix_bits + max_coord_bits - (sizeof(int) * 8 + 1) */
private const int32 FFFFF000 = ~(int32)0xFFF; /* = ~(((int32)1 << split_bits) - 1) */
/* Constants above must satisfy expressions given in comments. */

private inline int32 mul_shift(int24 a, int19 b, unsigned int s) 
{   /* Computes (a*b)>>s, s <= 12 */
    if (sizeof(int32) == 4) { /* We believe that compiler optimizes this check. */
        int32 aa = a & FFFFF000, a0 = a - aa, a1 = aa >> s;

        return ((a0 * b) >> s) + a1 * b; /* unrounded result */
    } else if (sizeof(int32) == 8) {
        return (a * b) >> s; /* unrounded result */
    } else
        assert(("Unsupported platform.",0));
}

private inline int32 mul_shift_round(int24 a, int19 b, unsigned int s) 
{   /* Computes (a*b)>>s, s <= 12 */
    if (sizeof(int32) == 4) { /* We believe that compiler optimizes this check. */
        int32 aa = a & FFFFF000, a0 = a - aa, a1 = aa >> s;

        return ((((a0 * b) >> (s - 1)) + 1) >> 1) + a1 * b; /* rounded result */
    } else if (sizeof(int32) == 8) {
        return (((a * b) >> (s -1)) + 1) >> 1; /* rounded result */
    } else
        assert(("Unsupported platform.",0));
}

private inline int32 shift_rounded(int32 v, unsigned int s)
{   return ((v >> (s - 1)) + 1) >> 1;
}

private inline int32 import_shift(int32 v, unsigned int s)
{   return v >> s;
}

private inline int32 Max(int32 a, int32 b)
{   return a>b ? a : b;
}

private inline int32 Min(int32 a, int32 b)
{   return a<b ? a : b;
}

private inline double Maxd(double a, double b)
{   return a>b ? a : b;
}

/*---------------------- members of matrix classes -------------------------*/

private inline void double_matrix__set(double_matrix * this, const gs_matrix_fixed * m)
{   this->xx = m->xx;
    this->xy = m->xy;
    this->yx = m->yx;
    this->yy = m->yy;
}

private inline void double_matrix__scale(double_matrix * this, double sx, double sy)
{   this->xx *= sx;
    this->xy *= sy;
    this->yx *= sx;
    this->yy *= sy;
}

private inline int double_matrix__invert_to(const double_matrix * this, double_matrix * m)
{   double det = this->xx * this->yy - this->xy * this->yx;

    if (fabs(det) * 1000000 < fabs(this->xx) + fabs(this->xy) + fabs(this->yx) + fabs(this->yy))
	return_error(gs_error_rangecheck);
    m->xx =  this->yy / det;
    m->xy = -this->xy / det;
    m->yx = -this->yx / det;
    m->yy =  this->xx / det;
    return 0;
}

private void fraction_matrix__drop_bits(fraction_matrix * this, unsigned int bits)
{   this->xx = shift_rounded(this->xx, bits);
    this->xy = shift_rounded(this->xy, bits);
    this->yx = shift_rounded(this->yx, bits);
    this->yy = shift_rounded(this->yy, bits);       
    this->denominator >>= bits;
    this->bitshift -= bits;
}

private void fraction_matrix__set(fraction_matrix * this, const double_matrix * pmat)
{   double axx = fabs(pmat->xx), axy = fabs(pmat->xy);
    double ayx = fabs(pmat->xx), ayy = fabs(pmat->xy);
    double scale = max(axx + axy, ayx + ayy);
    int matrix_exp, m;
    double unused = frexp(scale,&matrix_exp);

    this->bitshift = matrix_bits - matrix_exp;
    this->denominator = 1 << this->bitshift;
    this->xx = (int32)(pmat->xx * this->denominator + 0.5);
    this->xy = (int32)(pmat->xy * this->denominator + 0.5);
    this->yx = (int32)(pmat->yx * this->denominator + 0.5);
    this->yy = (int32)(pmat->yy * this->denominator + 0.5);
    m = Max(Max(any_abs(this->xx), any_abs(this->xy)), Max(any_abs(this->yx), any_abs(this->yy)));
    unused = frexp(m,&matrix_exp);
    if (matrix_exp > matrix_bits)
        fraction_matrix__drop_bits(this, matrix_exp - matrix_bits);
}

private inline void fraction_matrix__to_double(const fraction_matrix * this, double_matrix * pmat)
{   pmat->xx = (double)this->xx / this->denominator;
    pmat->xy = (double)this->xy / this->denominator;
    pmat->yx = (double)this->yx / this->denominator;
    pmat->yy = (double)this->yy / this->denominator;
}

private int fraction_matrix__invert_to(const fraction_matrix * this, fraction_matrix * pmat)
{   double_matrix m, M;
    int code;

    fraction_matrix__to_double(this, &M);
    code = double_matrix__invert_to(&M, &m);
    if (code < 0)
	return code;
    fraction_matrix__set(pmat,&m);
    return 0;
}

private inline int32 fraction_matrix__transform_x(fraction_matrix *this, int24 x, int24 y, unsigned int s)
{   return mul_shift_round(x, this->xx, s) + mul_shift_round(y, this->yx, s);
}
private inline int32 fraction_matrix__transform_y(fraction_matrix *this, int24 x, int24 y, unsigned int s)
{   return mul_shift_round(x, this->xy, s) + mul_shift_round(y, this->yy, s);
}


/*--------------------------- friends ------------------------------*/

private inline int ranger_step_f(int i, int beg, int end)
{   return (i == end ? beg : i + 1);
}

private inline int ranger_step_b(int i, int beg, int end)
{   return (i == beg ? end : i - 1);
}

private inline fixed o2d(const t1_hinter *h, t1_hinter_space_coord v)
{   return ((v >> (h->g2o_fraction_bits - _fixed_shift - 1)) + 1) >> 1;
}

private inline fixed d2o(const t1_hinter *h, t1_hinter_space_coord v)
{   return v << (h->g2o_fraction_bits - _fixed_shift);
}

private inline void g2o(t1_hinter * h, t1_glyph_space_coord gx, t1_glyph_space_coord gy, t1_hinter_space_coord *ox, t1_hinter_space_coord *oy)
{   *ox = fraction_matrix__transform_x(&h->ctmf, gx, gy, g2o_bitshift);
    *oy = fraction_matrix__transform_y(&h->ctmf, gx, gy, g2o_bitshift);
}

private inline t1_hinter_space_coord g2o_dist(t1_glyph_space_coord gd, int19 coef)
{   return mul_shift(gd, coef, g2o_bitshift);
}

private inline void g2d(t1_hinter * h, t1_glyph_space_coord gx, t1_glyph_space_coord gy, fixed *dx, fixed *dy)
{   *dx = fraction_matrix__transform_x(&h->ctmf, gx, gy, g2o_bitshift);
    *dy = fraction_matrix__transform_y(&h->ctmf, gx, gy, g2o_bitshift);
    *dx = o2d(h, *dx);
    *dy = o2d(h, *dy);
    *dx += h->orig_dx;
    *dy += h->orig_dy;
}

private inline void o2g(t1_hinter * h, t1_hinter_space_coord ox, t1_hinter_space_coord oy, t1_glyph_space_coord *gx, t1_glyph_space_coord *gy)
{   *gx = fraction_matrix__transform_x(&h->ctmi, ox, oy, split_bits);
    *gy = fraction_matrix__transform_y(&h->ctmi, ox, oy, split_bits);
    *gx = shift_rounded(*gx, h->g2o_fraction_bits + h->ctmi.bitshift - _fixed_shift - split_bits);
    *gy = shift_rounded(*gy, h->g2o_fraction_bits + h->ctmi.bitshift - _fixed_shift - split_bits);
}

private inline t1_glyph_space_coord o2g_dist(t1_hinter * h, t1_hinter_space_coord od, int19 coef)
{   return shift_rounded(mul_shift(od, coef, split_bits), h->g2o_fraction_bits + h->ctmi.bitshift - _fixed_shift - split_bits);
}

private inline void o2g_float(t1_hinter * h, t1_hinter_space_coord ox, t1_hinter_space_coord oy, t1_glyph_space_coord *gx, t1_glyph_space_coord *gy)
{   *gx = (long)(((double)ox * h->ctmi.xx + (double)oy * h->ctmi.yx) * fixed_scale / h->g2o_fraction / h->ctmi.denominator);
    *gy = (long)(((double)ox * h->ctmi.xy + (double)oy * h->ctmi.yy) * fixed_scale / h->g2o_fraction / h->ctmi.denominator);
}

/* --------------------- t1_hint class members ---------------------*/

private void t1_hint__set_aligned_coord(t1_hint * this, t1_glyph_space_coord gc, t1_pole * pole, enum t1_align_type align)
{   if (this->g0 == (this->type == hstem ? pole->gy : pole->gx)) {
        if (this->aligned0 < align)
            this->ag0 = gc, this->aligned0 = align;
    } else {
        if (this->aligned1 < align)
            this->ag1 = gc, this->aligned1 = align;
    }
}

/* --------------------- t1_hinter class members - debug graphics --------------------*/

private void t1_hinter__paint_glyph(t1_hinter * this, bool aligned)
{
#ifdef VD_TRACE
#define X(j) *member_prt(t1_glyph_space_coord, &this->pole[j], offset_x)
#define Y(j) *member_prt(t1_glyph_space_coord, &this->pole[j], offset_y)
    t1_glyph_space_coord *p_x = (aligned ? &this->pole[0].ax : &this->pole[0].gx);
    t1_glyph_space_coord *p_y = (aligned ? &this->pole[0].ay : &this->pole[0].gy);
    int offset_x = (char *)p_x - (char *)&this->pole[0];
    int offset_y = (char *)p_y - (char *)&this->pole[0];
    int i, j;
    char buf[15];

    if (!vd_enabled)
	return;
#   if VD_PAINT_POLE_IDS
    for(i = 0; i < this->contour_count; i++) {
        int beg_pole = this->contour[i];
        int end_pole = this->contour[i + 1] - 2;

        for(j = beg_pole; j <= end_pole; j++) {
            vd_circle(X(j), Y(j), 3, RGB(0,0,255));
            sprintf(buf, "%d", j);
            vd_text(this->pole[j].gx, this->pole[j].gy, buf, RGB(0,0,0));
            if (this->pole[j + 1].type == offcurve)
                j+=2;
        }
    }
#   endif
    vd_setcolor(aligned ? RGB(0,255,0) : RGB(0,0,255));
    for(i = 0; i < this->contour_count; i++) {
        int beg_pole = this->contour[i];
        int end_pole = this->contour[i + 1] - 2;

        vd_moveto(X(beg_pole), Y(beg_pole));
        for(j = beg_pole + 1; j <= end_pole; j++) {
            if (this->pole[j].type == oncurve) {
                vd_lineto(X(j), Y(j));
            } else {
                int jj = (j + 2 > end_pole ? beg_pole : j + 2);
                vd_curveto(X(j), Y(j), X(j + 1), Y(j + 1), X(jj), Y(jj));
                j+=2;
            }
        }
        vd_lineto(X(beg_pole), Y(beg_pole));
    }
#undef X
#undef Y
#endif
}

private void  t1_hinter__paint_raster_grid(t1_hinter * this)
{
#ifdef VD_TRACE
    int i;
    double j; /* 'long' can overflow */
    unsigned long c0 = RGB(192, 192, 192), c1 = RGB(64, 64, 64);
    t1_hinter_space_coord min_ox, max_ox, min_oy, max_oy;
    long div_x = this->g2o_fraction, div_xx = div_x * this->subpixels_x; 
    long div_y = this->g2o_fraction, div_yy = div_y * this->subpixels_y; 
    long ext_x = div_x * 5;
    long ext_y = div_y * 5;

    if (!vd_enabled)
	return;
    g2o(this, this->pole[0].gx, this->pole[0].gy, &min_ox, &min_oy);
    max_ox = min_ox, max_oy = min_oy;
    /* Compute BBox in outliner's space : */
    for (i = 1; i < this->pole_count - 1; i++) {
        t1_hinter_space_coord ox, oy;

        g2o(this, this->pole[i].gx, this->pole[i].gy, &ox, &oy);
        min_ox = min(min_ox, ox);
        min_oy = min(min_oy, oy);
        max_ox = max(max_ox, ox);
        max_oy = max(max_oy, oy);
    }
    min_ox -= ext_x;
    min_oy -= ext_y;
    max_ox += ext_x;
    max_oy += ext_y;
    /* Paint columns : */
    for (j = min_ox / div_x * div_x; j < (double)max_ox + div_x; j += div_x) {
        t1_glyph_space_coord gx0, gy0, gx1, gy1;
	bool pix = ((int)j / div_xx * div_xx == (int)j);

        o2g_float(this, (int)j, min_oy, &gx0, &gy0); /* o2g may overflow here due to ext. */
        o2g_float(this, (int)j, max_oy, &gx1, &gy1);
        vd_bar(gx0, gy0, gx1, gy1, 1, (!j ? 0 : pix ? c1 : c0));
    }
    /* Paint rows : */
    for (j = min_oy / div_y * div_y; j < max_oy + div_y; j += div_y) {
        t1_glyph_space_coord gx0, gy0, gx1, gy1;
	bool pix = ((int)j / div_yy * div_yy == (int)j);

        o2g_float(this, min_ox, (int)j, &gx0, &gy0);
        o2g_float(this, max_ox, (int)j, &gx1, &gy1);
        vd_bar(gx0, gy0, gx1, gy1, 1, (!j ? 0 : pix ? c1 : c0));
    }
#endif
}

/* --------------------- t1_hinter class members - import --------------------*/

void t1_hinter__init(t1_hinter * this, gx_path *output_path)
{   this->stem_snap_count[0] = this->stem_snap_count[1] = 0;
    this->zone_count = 0;
    this->pole_count = 0;
    this->hint_count = 0;
    this->contour_count = 0;
    this->hint_range_count = 0;
    this->flex_count = 0;

    this->max_contour_count = count_of(this->contour0);
    this->max_zone_count = count_of(this->zone0);
    this->max_pole_count = count_of(this->pole0);
    this->max_hint_count = count_of(this->hint0);
    this->max_hint_range_count = count_of(this->hint_range0);
    this->max_stem_snap_count[0] = count_of(this->stem_snap[0]);
    this->max_stem_snap_count[1] = count_of(this->stem_snap[1]);
    
    this->pole = this->pole0;
    this->hint = this->hint0;
    this->zone = this->zone0;
    this->contour = this->contour0;
    this->hint_range = this->hint_range0;
    this->stem_snap[0] = this->stem_snap0[0];
    this->stem_snap[1] = this->stem_snap0[1];

    this->FontType = 1;
    this->ForceBold = false;
    this->base_font_scale = 0;
    this->resolution = 0;
    this->heigt_transform_coef = this->width_transform_coef = 0;
    this->heigt_transform_coef_rat = this->width_transform_coef_rat = 0;
    this->heigt_transform_coef_inv = this->width_transform_coef_inv = 0;
    this->cx = this->cy = 0;
    this->contour[0] = 0;
    this->seac_flag = 0;
    this->keep_stem_width = false;
    this->charpath_flag = false;
    this->grid_fit_x = this->grid_fit_y = true;
    this->import_shift = 0;
    this->output_path = output_path;
    this->memory = (output_path == 0 ? 0 : output_path->memory);
    this->disable_hinting = (this->memory == NULL);

    this->stem_snap[0][0] = this->stem_snap[1][0] = 100; /* default */
}

private inline void t1_hinter__free_arrays(t1_hinter * this)
{   if (this->pole != this->pole0)
	gs_free_object(this->memory, this->pole, s_pole_array);
    if (this->hint != this->hint0)
	gs_free_object(this->memory, this->hint, s_hint_array);
    if (this->zone != this->zone0)
	gs_free_object(this->memory, this->zone, s_zone_array);
    if (this->contour != this->contour0)
	gs_free_object(this->memory, this->contour, s_contour_array);
    if (this->hint_range != this->hint_range0)
	gs_free_object(this->memory, this->hint_range, s_hint_range_array);
    if (this->stem_snap[0] != this->stem_snap0[0])
	gs_free_object(this->memory, this->stem_snap[0], s_stem_snap_array);
    if (this->stem_snap[1] != this->stem_snap0[1])
	gs_free_object(this->memory, this->stem_snap[1], s_stem_snap_array);
    this->pole = 0;
    this->hint = 0;
    this->zone = 0;
    this->contour = 0;
    this->hint_range = 0;
    this->stem_snap[0] = this->stem_snap[1] = 0;
}

private inline void t1_hinter__init_outline(t1_hinter * this)
{   this->contour_count = 0;
    this->pole_count = 0;
    this->contour[0] = 0;
    this->seac_flag = 0;
    this->hint_count = 0;
    this->primary_hint_count = -1;
    this->suppress_overshoots = false;
    this->path_opened = false;
}

private inline void t1_hinter__set_origin(t1_hinter * this, fixed dx, fixed dy, fixed unit_x, fixed unit_y)
{   
    if (this->align_to_subpixels) {
	this->orig_dx = (dx + fixed_half) & ~(fixed_1 - 1);
	this->orig_dy = (dy + fixed_half) & ~(fixed_1 - 1);
    } else {
	this->orig_dx = (dx + unit_x / 2) & ~(unit_x - 1);
	this->orig_dy = (dy + unit_y / 2) & ~(unit_y - 1);
    }
    this->orig_ox = d2o(this, this->orig_dx);
    this->orig_oy = d2o(this, this->orig_dy);
#   if ADOBE_SHIFT_CHARPATH
        /*  Adobe CPSI rounds coordinates for 'charpath' :
            X to trunc(x+0.5)
            Y to trunc(y)+0.5
        */
        if (this->charpath_flag) {
            this->orig_dx += fixed_half;
            this->orig_dx &= ~(fixed_1 - 1);
            this->orig_dy &= ~(fixed_1 - 1);
            this->orig_dy += fixed_half;
        } else {
            this->orig_dy += fixed_1;
	    /* Adobe CPSI does this, not sure why. */
            /* fixme : check bbox of cached bitmap. */
        }
#   endif
}

int t1_hinter__set_mapping(t1_hinter * this, gs_matrix_fixed * ctm, gs_rect * FontBBox, 
		    gs_matrix * FontMatrix, gs_matrix * baseFontMatrix,
		    fixed unit_x, fixed unit_y,
		    fixed origin_x, fixed origin_y)
{   float axx = fabs(ctm->xx), axy = fabs(ctm->xy);
    float ayx = fabs(ctm->xx), ayy = fabs(ctm->xy);
    float scale = max(axx + axy, ayx + ayy);
    double size = Maxd(Maxd(fabs(FontBBox->p.x), fabs(FontBBox->p.y)), 
                       Maxd(fabs(FontBBox->q.x), fabs(FontBBox->q.y)));
    double_matrix CTM;
    int code;

    while (size >= (1 << split_bits))
	++this->import_shift, size = shift_rounded((int)ceil(size), 1);
    if (size == 0)
	size = 1024 * fixed_scale; /* Hack for fonts with no bbox. */
    if (scale == 0)
	return_error(gs_error_invalidfont);
    this->disable_hinting |= (scale < 1/1024. || scale > 4);
    this->subpixels_x = unit_x / fixed_1;
    this->subpixels_y = unit_y / fixed_1;
    double_matrix__set(&CTM, ctm);
    double_matrix__scale(&CTM, 1<<this->import_shift, 1<<this->import_shift);
    fraction_matrix__set(&this->ctmf, &CTM);
    this->g2o_fraction_bits = this->ctmf.bitshift - g2o_bitshift + _fixed_shift;
    if (this->g2o_fraction_bits > max_coord_bits) {
        fraction_matrix__drop_bits(&this->ctmf, this->g2o_fraction_bits - max_coord_bits);
        this->g2o_fraction_bits = max_coord_bits;
    }
    code = fraction_matrix__invert_to(&this->ctmf, &this->ctmi); /* Note: ctmi is inversion of ctmf, not ctm. */
    if (code < 0)
	return code;
    this->g2o_fraction = 1 << this->g2o_fraction_bits;
    if (!this->disable_hinting) {
        if (this->g2o_fraction == 0)
    	    return_error(gs_error_limitcheck);
        if (this->ctmf.denominator == 0 || this->ctmi.denominator == 0)
    	    return_error(gs_error_limitcheck); /* Must not pass here. */
    }
    {   /* height_transform_coef is scaling factor for the
           distance between horizontal lines while transformation.
           width_transform_coef defines similarly.
        */
        double_matrix m;
        double vp, sp, div_x, div_y;

        fraction_matrix__to_double(&this->ctmf, &m);
        vp = any_abs(m.xx * m.yy - m.yx * m.yx); 
        sp = any_abs(m.xx * m.yx + m.xy * m.yy);
        div_x = hypot(m.xx, m.xy);
        div_y = hypot(m.yx, m.yy);
        if (vp != 0 && div_x != 0 && div_y != 0) {
            this->heigt_transform_coef = vp / div_x;
            this->width_transform_coef = vp / div_y;
            this->heigt_transform_coef_rat = (int19)(this->heigt_transform_coef * this->ctmf.denominator + 0.5);
            this->width_transform_coef_rat = (int19)(this->width_transform_coef * this->ctmf.denominator + 0.5);
            this->heigt_transform_coef_inv = (int19)(this->ctmi.denominator / this->heigt_transform_coef + 0.5);
            this->width_transform_coef_inv = (int19)(this->ctmi.denominator / this->width_transform_coef + 0.5);
            this->keep_stem_width = (sp <= vp / 3); /* small skew */
        }
    }
    {   /* Compute font size and resolution : */
        gs_point p0, p1, p2;
        double d0, d1, d2;

        gs_distance_transform(0, 1, baseFontMatrix, &p0);
        gs_distance_transform(0, 1, FontMatrix, &p1);
        gs_distance_transform(0, 1, (gs_matrix *)ctm, &p2);
        d0 = hypot(p0.x, p0.y);
        d1 = hypot(p1.x, p1.y);
        d2 = hypot(p2.x, p2.y);
        this->base_font_scale = d0;
        this->font_size =  floor(d1 / d0 * 10000 + 0.5) / 10000;
        this->resolution = floor(d2 / (1 << this->import_shift) / d1 * 10000000 + 0.5) / 10000000;
    }
    {	/* Enable grid fitting setarately for axes : */
	this->grid_fit_x = (any_abs(this->ctmf.xy) * 10 < any_abs(this->ctmf.xx) ||
			    any_abs(this->ctmf.xx) * 10 < any_abs(this->ctmf.xy)); 
	this->grid_fit_y = (any_abs(this->ctmf.yx) * 10 < any_abs(this->ctmf.yy) ||
			    any_abs(this->ctmf.yy) * 10 < any_abs(this->ctmf.yx));
    }
    this->transposed = (any_abs(this->ctmf.xy) * 10 > any_abs(this->ctmf.xx));
    this->align_to_subpixels = false;
    t1_hinter__set_origin(this, origin_x, origin_y, unit_x, unit_y);
#   if VD_DRAW_IMPORT
    vd_get_dc('h');
    vd_set_shift(VD_SHIFT_X, VD_SHIFT_Y);
    vd_set_scale(VD_SCALE);
    vd_set_origin(0,0);
    vd_erase(RGB(255, 255, 255));
    t1_hinter__paint_raster_grid(this);
    vd_setcolor(VD_IMPORT_COLOR);
    vd_setlinewidth(0);
#   endif
    return 0;
}

private void t1_hinter__make_zone(t1_hinter * this, t1_zone *zone, float * blues, enum t1_zone_type type, t1_glyph_space_coord blue_fuzz)
{   t1_glyph_space_coord d = 0;

    zone->type = type;
    zone->y           = import_shift(float2fixed(blues[0] + d), this->import_shift);
    zone->overshoot_y = import_shift(float2fixed(blues[1] + d), this->import_shift);
    zone->y_min = min(zone->y, zone->overshoot_y) - blue_fuzz;
    zone->y_max = max(zone->y, zone->overshoot_y) + blue_fuzz;
    if (type == botzone ? zone->overshoot_y > zone->y : zone->overshoot_y < zone->y) {
        int v = zone->overshoot_y; zone->overshoot_y = zone->y; zone->y = v;
    }
}

private bool t1_hinter__realloc_array(gs_memory_t *mem, void **a, void *a0, int *max_count, int elem_size, int enhancement, const char *cname)
{
    void *aa = gs_alloc_bytes(mem, (*max_count + enhancement * 2) * elem_size, cname);

    if (aa == NULL)
	return true;
    memcpy(aa, *a, *max_count * elem_size);
    if (*a != a0)
	gs_free_object(mem, *a, cname);
    *a = aa;
    *max_count += enhancement * 2;
    return false;
}

private int t1_hinter__set_alignment_zones(t1_hinter * this, float * blues, int count, enum t1_zone_type type, bool family)
{   int count2 = count / 2, i, j;

    if (!family) {
        /* Store zones : */
        if (count2 + this->zone_count >= this->max_zone_count)
	    if(t1_hinter__realloc_array(this->memory, (void **)&this->zone, this->zone0, &this->max_zone_count, 
	                                sizeof(this->zone0) / count_of(this->zone0), 
					max(T1_MAX_ALIGNMENT_ZONES, count), s_zone_array))
    		return_error(gs_error_VMerror);
        for (i = 0; i < count2; i++)
            t1_hinter__make_zone(this, &this->zone[this->zone_count + i], blues + i + i, type, this->blue_fuzz);
        this->zone_count += count2;
    } else {
        /* Replace with family zones if allowed : */
        t1_zone zone;
        for (i = 0; i < count2; i++) {
            t1_hinter__make_zone(this, &zone, blues + i, type, this->blue_fuzz);
            for (j = 0; j<this->zone_count; j++) {
                t1_zone *zone1 = &this->zone[j];
                if (any_abs(zone.y -           zone1->y          ) * this->heigt_transform_coef <= 1 &&
                    any_abs(zone.overshoot_y - zone1->overshoot_y) * this->heigt_transform_coef <= 1)
                    *zone1 = zone;
            }
        }    
    }
    return 0;
}

private int t1_hinter__set_stem_snap(t1_hinter * this, float * value, int count, unsigned short hv)
{   int count0 = this->stem_snap_count[hv], i;

    if (count + count0 >= this->max_stem_snap_count[hv])
	if(t1_hinter__realloc_array(this->memory, (void **)&this->stem_snap[hv], this->stem_snap0[hv], &this->max_stem_snap_count[hv], 
	                                sizeof(this->stem_snap0[0]) / count_of(this->stem_snap0[0]), 
					max(T1_MAX_STEM_SNAPS, count), s_stem_snap_array))
    	    return_error(gs_error_VMerror);
    for (i = 0; i < count; i++)
        this->stem_snap[hv][count0 + i] = import_shift(float2fixed(value[i]), this->import_shift);
    this->stem_snap_count[hv] += count;
    return 0;
}

int t1_hinter__set_font_data(t1_hinter * this, int FontType, gs_type1_data *pdata, bool charpath_flag)
{   int code;

    t1_hinter__init_outline(this);
    this->FontType = FontType;
    this->BlueScale = pdata->BlueScale;
    this->blue_shift = float2fixed(pdata->BlueShift);
    this->blue_fuzz  = import_shift(float2fixed(pdata->BlueFuzz), this->import_shift);
    this->suppress_overshoots = (this->BlueScale > this->heigt_transform_coef / (1 << this->import_shift) - 0.00020417);
    this->overshoot_threshold = (this->heigt_transform_coef != 0 ? (t1_glyph_space_coord)(fixed_half / this->heigt_transform_coef) : 0);
    this->blue_rounding = (t1_hinter_space_coord)((this->BlueScale * 240) * (this->resolution * this->base_font_scale) * this->g2o_fraction);
    this->ForceBold = pdata->ForceBold;
    this->disable_hinting |= charpath_flag;
    this->charpath_flag = charpath_flag;
    if (this->disable_hinting)
	return 0;
    code = t1_hinter__set_alignment_zones(this, pdata->OtherBlues.values, pdata->OtherBlues.count, botzone, false);
    if (code >= 0)
	code = t1_hinter__set_alignment_zones(this, pdata->BlueValues.values, min(2, pdata->BlueValues.count), botzone, false);
    if (code >= 0)
	code = t1_hinter__set_alignment_zones(this, pdata->BlueValues.values + 2, pdata->BlueValues.count - 2, topzone, false);
    if (code >= 0)
	code = t1_hinter__set_alignment_zones(this, pdata->FamilyOtherBlues.values, pdata->FamilyOtherBlues.count, botzone, true);
    if (code >= 0)
	code = t1_hinter__set_alignment_zones(this, pdata->FamilyBlues.values, min(2, pdata->FamilyBlues.count), botzone, true);
    if (code >= 0)
	code = t1_hinter__set_alignment_zones(this, pdata->FamilyBlues.values + 2, pdata->FamilyBlues.count - 2, topzone, true);
    if (code >= 0)
	code = t1_hinter__set_stem_snap(this, pdata->StdHW.values, pdata->StdHW.count, 0);
    if (code >= 0)
	code = t1_hinter__set_stem_snap(this, pdata->StdVW.values, pdata->StdVW.count, 1);
    if (code >= 0)
	code = t1_hinter__set_stem_snap(this, pdata->StemSnapH.values, pdata->StemSnapH.count, 0);
    if (code >= 0)
	code = t1_hinter__set_stem_snap(this, pdata->StemSnapV.values, pdata->StemSnapV.count, 1);
    return code;
}

private inline int t1_hinter__can_add_pole(t1_hinter * this, t1_pole **pole)
{   if (this->pole_count >= this->max_pole_count)
        if(t1_hinter__realloc_array(this->memory, (void **)&this->pole, this->pole0, &this->max_pole_count, 
				    sizeof(this->pole0) / count_of(this->pole0), T1_MAX_POLES, s_pole_array))
	    return_error(gs_error_VMerror);
    *pole = &this->pole[this->pole_count];
    return 0;
}

private inline int t1_hinter__add_pole(t1_hinter * this, t1_glyph_space_coord xx, t1_glyph_space_coord yy, enum t1_pole_type type)
{   t1_pole *pole;
    int code = t1_hinter__can_add_pole(this, &pole);

    if (code < 0)
	return code;
    pole->gx = pole->ax = this->cx += import_shift(xx, this->import_shift);
    pole->gy = pole->ay = this->cy += import_shift(yy, this->import_shift);
    pole->ox = pole->oy = 0;
    pole->type = type;
    pole->contour_index = this->contour_count;
    pole->aligned_x = pole->aligned_y = unaligned;
    this->pole_count++;
    return 0;
}

int t1_hinter__sbw(t1_hinter * this, fixed sbx, fixed sby, fixed wx,  fixed wy)
{   this->cx = this->orig_gx = this->subglyph_orig_gx = import_shift(sbx, this->import_shift);
    this->cy = this->orig_gy = this->subglyph_orig_gy = import_shift(sby, this->import_shift);
    this->width_gx = import_shift(wx, this->import_shift);
    this->width_gy = import_shift(wy, this->import_shift);
    return 0;
}

int t1_hinter__sbw_seac(t1_hinter * this, fixed sbx, fixed sby)
{   this->cx = this->subglyph_orig_gx = this->orig_gx + import_shift(sbx, this->import_shift);
    this->cy = this->subglyph_orig_gy = this->orig_gy + import_shift(sby, this->import_shift);
    return 0;
}

int t1_hinter__rmoveto(t1_hinter * this, fixed xx, fixed yy)
{   int code;

    if (this->flex_count == 0) {
	if (this->disable_hinting) {
	    t1_glyph_space_coord gx = this->cx += import_shift(xx, this->import_shift);
	    t1_glyph_space_coord gy = this->cy += import_shift(yy, this->import_shift);
	    fixed fx, fy;

	    if (this->path_opened) {
		code = gx_path_close_subpath(this->output_path);
		if (code < 0)
		    return code;
		this->path_opened = false;
	    }
	    g2d(this, gx, gy, &fx, &fy);
	    return gx_path_add_point(this->output_path, fx, fy);
	}
	if (this->pole_count > 0 && this->pole[this->pole_count - 1].type == moveto)
	    this->pole_count--;
	if (this->pole_count > 0 && this->pole[this->pole_count - 1].type != closepath) {
	    code = t1_hinter__closepath(this);
	    if (code < 0)
		return code;
	}
    }
    code = t1_hinter__add_pole(this, xx, yy, moveto);
    if (this->flex_count == 0) {
	this->bx = this->cx;
	this->by = this->cy;
    }
    vd_circle(this->cx, this->cy, 2, RGB(255, 0, 0));
    vd_moveto(this->cx, this->cy);
    return code;
}

private inline void t1_hinter__skip_degenerate_segnment(t1_hinter * this, int npoles)
{   /* Degenerate segments amy appear due to import shift with bbox > 4096 */
    int contour_beg = this->contour[this->contour_count], i;

    if (contour_beg >= this->pole_count - npoles)
	return;
    for (i = this->pole_count - npoles - 1; i < this->pole_count - 1; i++)
	if (this->pole[i].ax != this->cx || this->pole[i].ay != this->cy)
	    return;
    this->pole_count -= npoles;
}

int t1_hinter__rlineto(t1_hinter * this, fixed xx, fixed yy)
{   
    if (this->disable_hinting) {
	t1_glyph_space_coord gx = this->cx += import_shift(xx, this->import_shift);
	t1_glyph_space_coord gy = this->cy += import_shift(yy, this->import_shift);
	fixed fx, fy;

	this->path_opened = true;
	g2d(this, gx, gy, &fx, &fy);
	return gx_path_add_line(this->output_path, fx, fy);
    } else {
	int code = t1_hinter__add_pole(this, xx, yy, oncurve);
    
	if (code < 0)
	    return code;
	vd_lineto(this->cx, this->cy);
	t1_hinter__skip_degenerate_segnment(this, 1);
	return 0;
    }
}

int t1_hinter__rcurveto(t1_hinter * this, fixed xx0, fixed yy0, fixed xx1, fixed yy1, fixed xx2, fixed yy2)
{   
    if (this->disable_hinting) {
	t1_glyph_space_coord gx0 = this->cx += import_shift(xx0, this->import_shift);
	t1_glyph_space_coord gy0 = this->cy += import_shift(yy0, this->import_shift);
	t1_glyph_space_coord gx1 = this->cx += import_shift(xx1, this->import_shift);
	t1_glyph_space_coord gy1 = this->cy += import_shift(yy1, this->import_shift);
	t1_glyph_space_coord gx2 = this->cx += import_shift(xx2, this->import_shift);
	t1_glyph_space_coord gy2 = this->cy += import_shift(yy2, this->import_shift);
	fixed fx0, fy0, fx1, fy1, fx2, fy2;

	this->path_opened = true;
	g2d(this, gx0, gy0, &fx0, &fy0);
	g2d(this, gx1, gy1, &fx1, &fy1);
	g2d(this, gx2, gy2, &fx2, &fy2);
	return gx_path_add_curve(this->output_path, fx0, fy0, fx1, fy1, fx2, fy2);
    } else {
	int code;

	code = t1_hinter__add_pole(this, xx0, yy0, offcurve);
	if (code < 0)
	    return code;
	code = t1_hinter__add_pole(this, xx1, yy1, offcurve);
	if (code < 0)
	    return code;
	code = t1_hinter__add_pole(this, xx2, yy2, oncurve);
	if (code < 0)
	    return code;
	vd_curveto(this->pole[this->pole_count - 3].gx, this->pole[this->pole_count - 3].gy,
		   this->pole[this->pole_count - 2].gx, this->pole[this->pole_count - 2].gy,
		   this->cx, this->cy);
	t1_hinter__skip_degenerate_segnment(this, 3);
	return 0;
    }
}

void t1_hinter__setcurrentpoint(t1_hinter * this, fixed xx, fixed yy)
{   this->cx += import_shift(xx, this->import_shift);
    this->cy += import_shift(yy, this->import_shift);
}

int t1_hinter__closepath(t1_hinter * this)
{   if (this->disable_hinting) {
	this->path_opened = false;
        return gx_path_close_subpath(this->output_path);
    } else {
	int contour_beg = this->contour[this->contour_count], code;

	if (contour_beg == this->pole_count)
	    return 0; /* maybe a single trailing moveto */
#	if VD_DRAW_IMPORT
	    vd_setcolor(VD_IMPORT_COLOR);
	    vd_setlinewidth(0);
	    vd_moveto(this->cx, this->cy);
	    vd_lineto(this->bx, this->by);
#	endif
	if (this->bx == this->cx && this->by == this->cy) {
	    /* Don't create degenerate segment */ 
	    this->pole[this->pole_count - 1].type = closepath;
	} else {
	    t1_glyph_space_coord cx = this->cx, cy = this->cy;

	    this->cx = this->bx;
	    this->cy = this->by;
	    code = t1_hinter__add_pole(this, 0, 0, closepath);
	    if (code < 0)
		return code;
	    this->cx = cx;
	    this->cy = cy;
	}
	this->contour_count++;
	if (this->contour_count >= this->max_contour_count)
	    if(t1_hinter__realloc_array(this->memory, (void **)&this->contour, this->contour0, &this->max_contour_count, 
					sizeof(this->contour0) / count_of(this->contour0), T1_MAX_CONTOURS, s_contour_array))
		return_error(gs_error_VMerror);
	this->contour[this->contour_count] = this->pole_count;
        return 0;
    }
}

private inline int t1_hinter__can_add_hint(t1_hinter * this, t1_hint **hint)
{   if (this->hint_count >= this->max_hint_count)
        if(t1_hinter__realloc_array(this->memory, (void **)&this->hint, this->hint0, &this->max_hint_count, 
				    sizeof(this->hint0) / count_of(this->hint0), T1_MAX_HINTS, s_hint_array))
	    return_error(gs_error_VMerror);
    *hint = &this->hint[this->hint_count];
    return 0;
}

int t1_hinter__flex_beg(t1_hinter * this)
{   if (this->flex_count != 0)
	return_error(gs_error_invalidfont);
    this->flex_count++;
    return 0;
}

int t1_hinter__flex_point(t1_hinter * this)
{   if (this->flex_count == 0)
	return_error(gs_error_invalidfont);
    this->flex_count++;
    return 0;
}

int t1_hinter__flex_end(t1_hinter * this, fixed flex_height)
{   t1_pole *pole0, *pole1, *pole4;
    t1_hinter_space_coord ox, oy;
    const int32 div_x = this->g2o_fraction * this->subpixels_x;
    const int32 div_y = this->g2o_fraction * this->subpixels_y;
    
    if (this->flex_count != 8)
	return_error(gs_error_invalidfont);
    /* We've got 8 poles accumulated in pole array. */
    pole0 = &this->pole[this->pole_count - 8];
    pole1 = &this->pole[this->pole_count - 7];
    pole4 = &this->pole[this->pole_count - 4];
    g2o(this, pole4->gx - pole1->gx, pole4->gy - pole1->gy, &ox, &oy);
    if (any_abs(ox) > div_x * fixed2float(flex_height) / 100 || 
	any_abs(oy) > div_y * fixed2float(flex_height) / 100) {
	/* do with curves */
	vd_moveto (pole0[0].gx, pole0[0].gy);
	vd_curveto(pole0[2].gx, pole0[2].gy, pole0[3].gx, pole0[3].gy, pole0[4].gx, pole0[4].gy);
	vd_curveto(pole0[5].gx, pole0[5].gy, pole0[6].gx, pole0[6].gy, pole0[7].gx, pole0[7].gy);
	if (this->disable_hinting) {
	    fixed fx0, fy0, fx1, fy1, fx2, fy2;
	    int code;

	    g2d(this, pole0[1].gx, pole0[1].gy, &fx0, &fy0);
	    g2d(this, pole0[2].gx, pole0[2].gy, &fx1, &fy1);
	    g2d(this, pole0[3].gx, pole0[3].gy, &fx2, &fy2);
	    code = gx_path_add_curve(this->output_path, fx0, fy0, fx1, fy1, fx2, fy2);
	    if (code < 0)
		return code;
	    g2d(this, pole0[4].gx, pole0[4].gy, &fx0, &fy0);
	    g2d(this, pole0[5].gx, pole0[5].gy, &fx1, &fy1);
	    g2d(this, pole0[6].gx, pole0[6].gy, &fx2, &fy2);
	    this->flex_count = 0;
	    this->pole_count = 0;
	    return gx_path_add_curve(this->output_path, fx0, fy0, fx1, fy1, fx2, fy2);
	} else {
	    memmove(pole1, pole1 + 1, (sizeof(this->pole0) / count_of(this->pole0)) * 7);
	    pole0[1].type = pole0[2].type = offcurve;
	    pole0[3].type = oncurve;
	    pole0[4].type = pole0[5].type = offcurve;
	    pole0[6].type = oncurve;
	    this->pole_count--;
	}
    } else {
	/* do with line */
	vd_moveto(pole0[0].gx, pole0[0].gy);
	vd_lineto(pole0[7].gx, pole0[7].gy);
	if (this->disable_hinting) {
	    fixed fx, fy;

	    g2d(this, pole0[7].gx, pole0[7].gy, &fx, &fy);
	    this->flex_count = 0;
	    this->pole_count = 0;
	    return gx_path_add_line(this->output_path, fx, fy);
	} else {
	    pole0[1] = pole0[7];
	    pole0[1].type = oncurve;
	    this->pole_count -= 6;
	}
    }
    this->flex_count = 0;
    return 0;
}

private inline int t1_hinter__can_add_hint_range(t1_hinter * this, t1_hint_range **hint_range)
{   if (this->hint_range_count >= this->max_hint_range_count)
        if(t1_hinter__realloc_array(this->memory, (void **)&this->hint_range, this->hint_range0, &this->max_hint_range_count, 
				    sizeof(this->hint_range0) / count_of(this->hint_range0), T1_MAX_HINTS, s_hint_range_array))
	    return_error(gs_error_VMerror);
    *hint_range = &this->hint_range[this->hint_range_count];
    return 0;
}

int t1_hinter__hint_mask(t1_hinter * this, byte *mask)
{   int hint_count, i;

    if (this->disable_hinting)
	return 0;
    hint_count = this->hint_count;

    for(i = 0; i < hint_count; i++) {
	bool activate = (mask != NULL && (mask[i >> 3] & (0x80 >> (i & 7))) != 0);
	t1_hint *hint = &this->hint[i];

	if (activate) {
	    if (hint->range_index != -1 &&
		(this->hint_range[hint->range_index].end_pole == -1 ||
		 this->hint_range[hint->range_index].end_pole == this->pole_count)) {
		 /* continie the range */
		this->hint_range[hint->range_index].end_pole = -1;
	    } else {
		/* add new range */
		t1_hint_range *hint_range;
		int code = t1_hinter__can_add_hint_range(this, &hint_range);

		if (code < 0)
		   return code;
		hint_range->beg_pole = this->pole_count;
		hint_range->end_pole = -1;
		hint_range->next = hint->range_index;
		hint->range_index = this->hint_range_count;
		this->hint_range_count++;
	    }
	} else { 
	    if (hint->range_index != -1 &&
		this->hint_range[hint->range_index].end_pole == -1) {
		/* deactivate */
		this->hint_range[hint->range_index].end_pole = this->pole_count;
	    } else
		DO_NOTHING;
	}
    }
    return 0;
}

int t1_hinter__drop_hints(t1_hinter * this)
{   if (this->disable_hinting)
	return 0;
    if (this->primary_hint_count == -1)
	this->primary_hint_count = this->hint_range_count;
    return t1_hinter__hint_mask(this, NULL);
}

private inline int t1_hinter__stem(t1_hinter * this, enum t1_hint_type type, unsigned short stem3_index
                                                  , fixed v0, fixed v1)
{   t1_hint *hint;
    t1_glyph_space_coord s = (type == hstem ? this->subglyph_orig_gy : this->subglyph_orig_gx);
    t1_glyph_space_coord g0 = s + import_shift(v0, this->import_shift);
    t1_glyph_space_coord g1 = s + import_shift(v0 + v1, this->import_shift);
    t1_hint_range *range;
    int i, code;

    for (i = 0; i < this->hint_count; i++)
	if (this->hint[i].type == type && 
	    this->hint[i].g0 == g0 && this->hint[i].g1 == g1)
		break;
    if (i < this->hint_count)
	hint = &this->hint[i];
    else {
	code = t1_hinter__can_add_hint(this, &hint);
	if (code < 0)
	    return code;
	hint->type = type;
	hint->g0 = hint->ag0 = g0;
	hint->g1 = hint->ag1 = g1;
	hint->aligned0 = hint->aligned1 = unaligned;
	hint->stem3_index = stem3_index;
	hint->range_index = -1;
    }
    code = t1_hinter__can_add_hint_range(this, &range);
    if (code < 0)
	return code;
    range->contour_index = this->contour_count;
    range->beg_pole = this->pole_count;
    range->end_pole = -1;
    range->next = hint->range_index;
    hint->range_index = range - this->hint_range;
    if (i >= this->hint_count)
	this->hint_count++;
    this->hint_range_count++;
    return 0;
}

int t1_hinter__dotsection(t1_hinter * this)
{   if (this->pole_count == 0 || this->pole[this->pole_count - 1].type != moveto)
        return 0; /* We store beginning dotsection hints only. */
    if (this->disable_hinting)
	return 0;
    return t1_hinter__stem(this, dot, 0, 0, 0);
}


int t1_hinter__hstem(t1_hinter * this, fixed x0, fixed x1)
{   if (this->disable_hinting)
	return 0;
    return t1_hinter__stem(this, hstem, 0, x0, x1);
}

int t1_hinter__vstem(t1_hinter * this, fixed y0, fixed y1)
{   if (this->disable_hinting)
	return 0;
    return t1_hinter__stem(this, vstem, 0, y0, y1);
}

int t1_hinter__hstem3(t1_hinter * this, fixed x0, fixed x1, fixed x2, fixed x3, fixed x4, fixed x5)
{   int code;

    if (this->disable_hinting)
	return 0;
    code = t1_hinter__stem(this, hstem, 1, x0, x1);
    if (code < 0)
	return code;
    code = t1_hinter__stem(this, hstem, 2, x2, x3);
    if (code < 0)
	return code;
    return t1_hinter__stem(this, hstem, 3, x4, x5);
}

int t1_hinter__vstem3(t1_hinter * this, fixed y0, fixed y1, fixed y2, fixed y3, fixed y4, fixed y5)
{   int code;

    if (this->disable_hinting)
	return 0;
    code = t1_hinter__stem(this, vstem, 1, y0, y1);
    if (code < 0)
	return code;
    code = t1_hinter__stem(this, vstem, 2, y2, y3);
    if (code < 0)
	return code;
    return t1_hinter__stem(this, vstem, 3, y4, y5);
}

int t1_hinter__endchar(t1_hinter * this, bool seac_flag)
{   this->seac_flag = seac_flag;
    return 0;
}

/* --------------------- t1_hinter class members - the hinting --------------------*/

private inline int t1_hinter__segment_beg(t1_hinter * this, int pole_index)
{   int contour_index = this->pole[pole_index].contour_index;
    int beg_contour_pole = this->contour[contour_index];
    int end_contour_pole = this->contour[contour_index + 1] - 2;
    int prev = ranger_step_b(pole_index, beg_contour_pole, end_contour_pole);

    while (this->pole[prev].type == offcurve)
        prev = ranger_step_b(prev, beg_contour_pole, end_contour_pole);
    return prev;
}

private inline int t1_hinter__segment_end(t1_hinter * this, int pole_index)
{   int contour_index = this->pole[pole_index].contour_index;
    int beg_contour_pole = this->contour[contour_index];
    int end_contour_pole = this->contour[contour_index + 1] - 2;
    int next = ranger_step_f(pole_index, beg_contour_pole, end_contour_pole);

    while (this->pole[next].type == offcurve)
        next = ranger_step_f(next, beg_contour_pole, end_contour_pole);
    return next;
}

private void t1_hinter__simplify_representation(t1_hinter * this)
{   int i, j;

    /*  moveto's were needed to decode path correctly.
        We don't need them so far.
        Replace 'moveto' with 'oncurve' :
    */
    for (i = 0; i < this->contour_count; i++)
        if (this->pole[this->contour[i]].type == moveto)
            this->pole[this->contour[i]].type = oncurve;
    /* Remove hints which are disabled with !grid_fit_x, !grid_fit_y.
     * We can't do before import is completed due to hint mask commands.
     */
    if (!this->grid_fit_x || !this->grid_fit_y) {
	for (i = j = 0; i < this->contour_count; i++)
	    if ((this->hint[i].type == vstem && !this->grid_fit_x) ||
		(this->hint[i].type == hstem && !this->grid_fit_y)) {
		continue; /* skip it. */
	    } else {
		this->hint[j] = this->hint[i];
		j++;
	    }
    }
}

private inline bool t1_hinter__is_small_angle(t1_hinter * this, int pole_index0, int pole_index1, long tan_x, long tan_y, int alpha)
{   long gx = this->pole[pole_index1].gx - this->pole[pole_index0].gx;
    long gy = this->pole[pole_index1].gy - this->pole[pole_index0].gy;
    long vp = mul_shift(gx, tan_y, _fixed_shift) - mul_shift(gy, tan_x, _fixed_shift);
    long sp = mul_shift(gx, tan_x, _fixed_shift) + mul_shift(gy, tan_y, _fixed_shift);
    long vp1 = any_abs(vp), sp1 = any_abs(sp);

    if (gx == 0 && gy == 0)
	return false;
    return vp1 <= sp1 / alpha;
}

private inline bool t1_hinter__is_conjugated(t1_hinter * this, int pole_index)
{   int prev = t1_hinter__segment_beg(this, pole_index);
    int next = t1_hinter__segment_end(this, pole_index);
    long gx0 = this->pole[prev].gx - this->pole[pole_index].gx;
    long gy0 = this->pole[prev].gy - this->pole[pole_index].gy;
    long gx1 = this->pole[next].gx - this->pole[pole_index].gx;
    long gy1 = this->pole[next].gy - this->pole[pole_index].gy;
    long vp = gx0 * gy1 - gy0 * gx1;
    long sp = gx0 * gy1 - gy0 * gx1;

    if (sp > 0)
        return false;
    if (vp == 0)
        return true;
    return any_abs(vp) < -sp / 1000; /* The threshold is taken from scratch. */
}

private inline bool t1_hinter__next_contour_pole(t1_hinter * this, int pole_index)
{   int contour_index = this->pole[pole_index].contour_index;
    int beg_contour_pole = this->contour[contour_index];
    int end_contour_pole = this->contour[contour_index + 1] - 2;

    return ranger_step_f(pole_index, beg_contour_pole, end_contour_pole);
}

private inline bool t1_hinter__is_good_tangent(t1_hinter * this, int pole_index, long tan_x, long tan_y)
{   int contour_index = this->pole[pole_index].contour_index;
    int beg_contour_pole = this->contour[contour_index];
    int end_contour_pole = this->contour[contour_index + 1] - 2, prev, next;
    int const alpha = 3;

    prev = ranger_step_b(pole_index, beg_contour_pole, end_contour_pole);
    if (t1_hinter__is_small_angle(this, prev, pole_index, tan_x, tan_y, alpha))
        return true;
    next = ranger_step_f(pole_index, beg_contour_pole, end_contour_pole);
    if (t1_hinter__is_small_angle(this, next, pole_index, tan_x, tan_y, alpha))
        return true;
    return false;
}

private void t1_hinter__compute_type1_stem_ranges(t1_hinter * this)
{   int j;
    int end_range_pole = this->pole_count - 3;
    int primary_hint_count = this->primary_hint_count;

    if (this->hint_count == 0)
	return;
    if (primary_hint_count == -1)
	primary_hint_count = this->hint_range_count;
    /*  After the decoding, hint commands refer to the last pole before HR occures.
	Move pointers to the beginning segment pole, so as they
	rerer to oncurve pole :
    */
    for (j = 0; j < this->hint_range_count; j++)
	if (this->hint_range[j].beg_pole > this->contour[this->hint_range[j].contour_index])
	    this->hint_range[j].beg_pole = t1_hinter__segment_beg(this, this->hint_range[j].beg_pole);
    /* Process primary hints - ranges are entire glyph : */
    for(j = 0; j < primary_hint_count; j++)      {
        this->hint_range[j].beg_pole = 0;
        this->hint_range[j].end_pole = end_range_pole;
    }
    /* Process secondary hints - ranges until HR or until contour end : */
    for(; j < this->hint_range_count; j++)      {
        if (this->hint_range[j].end_pole == -1)
	    this->hint_range[j].end_pole = this->contour[this->hint_range[j].contour_index + 1] - 1;
    }
    /*  Note that ranges of primary hints may include a tail of the hint array
        due to multiple contours. Primary hints have a lesser priority,
	so apply them first, and possibly recover later.
    */
}

private void t1_hinter__compute_type2_stem_ranges(t1_hinter * this)
{   int i;

    for (i = 0; i < this->hint_range_count; i++)
	if (this->hint_range[i].end_pole == -1)
	    this->hint_range[i].end_pole = this->pole_count - 2;
}


private bool t1_hinter__is_stem_hint_applicable(t1_hinter * this, t1_hint *hint, int pole_index)
{   if (hint->type == hstem 
            && (this->pole[pole_index].gy == hint->g0 || this->pole[pole_index].gy == hint->g1)
            && t1_hinter__is_good_tangent(this, pole_index, 1, 0))
        return true;
    if (hint->type == vstem  
            && (this->pole[pole_index].gx == hint->g0 || this->pole[pole_index].gx == hint->g1)
            && t1_hinter__is_good_tangent(this, pole_index, 0, 1)) 
        return true;
    return false;
}

private t1_zone * t1_hinter__find_zone(t1_hinter * this, t1_glyph_space_coord pole_y, bool curve, bool convex, bool concave)
{   bool maybe_top = !curve || convex;
    bool maybe_bot = !curve || concave;
    int i;

    for (i = 0; i < this->zone_count; i++) {
        t1_zone *zone = &this->zone[i];
        if ((maybe_top && zone->type == topzone) || (maybe_bot && zone->type == botzone))
            if (zone->y_min <= pole_y && pole_y <= zone->y_max)
                return zone;
    }
    return NULL;
    /*todo: optimize narrowing the search range */
}

private void t1_hinter__align_to_grid(t1_hinter * this, int32 unit, t1_glyph_space_coord *x, t1_glyph_space_coord *y)
{   if (unit > 0) {
	long div_x = unit * (this->align_to_subpixels ? 1 : this->subpixels_x);
	long div_y = unit * (this->align_to_subpixels ? 1 : this->subpixels_y);
        t1_glyph_space_coord gx = *x, gy = *y;
        t1_hinter_space_coord ox, oy;
        int32 dx, dy;

        g2o(this, gx, gy, &ox, &oy);
	if (this->align_to_subpixels) {
	    ox += this->orig_ox;
	    oy += this->orig_oy;
	}
        dx = ox % div_x;
        dy = oy % div_y; /* So far dx and dy are 19 bits */
        if (dx > div_x / 2 )
            dx = - div_x + dx;
        else if (dx < - div_x / 2)
            dx = div_x + dx;
        if (dy > div_y / 2)
            dy = - div_y + dy;
        else if (dy < - div_y / 2)
            dy = div_y + dy;
        {   t1_glyph_space_coord gxd, gyd;

            o2g(this, dx, dy, &gxd, &gyd);
	    if (this->grid_fit_x)
		*x -= gxd;
	    if (this->grid_fit_y)
		*y -= gyd;
            /* Round to suppress small noise : */
	    if (this->grid_fit_x)
		*x = (*x + 7) & ~15;
	    if (this->grid_fit_y)
		*y = (*y + 7) & ~15;
        }
    }
}

private inline t1_hinter_space_coord g2o_dist_blue(t1_hinter * h, t1_glyph_space_coord gw)
{   double W = fixed2float(gw);
    double w = W * (h->resolution * h->font_size * h->base_font_scale - h->BlueScale) + 1;

    return (t1_hinter_space_coord)(w * h->g2o_fraction);
    /* todo : exclude floating point */
}

private void t1_hinter__add_overshoot(t1_hinter * this, t1_zone * zone, t1_glyph_space_coord * x, t1_glyph_space_coord * y)
{   t1_glyph_space_coord gy = *y;
    /* t1_glyph_space_coord gw = any_abs(zone->overshoot_y - zone->y); */
    t1_glyph_space_coord gw = any_abs(gy - zone->y);
    t1_hinter_space_coord ow = g2o_dist_blue(this, gw);
    t1_hinter_space_coord ow1 = ow / this->g2o_fraction * this->g2o_fraction;
    t1_glyph_space_coord gw1 = o2g_dist(this, ow1, this->heigt_transform_coef_inv);

    *y = zone->y + (zone->type == topzone ? gw1 : -gw1);
}

private enum t1_align_type t1_hinter__compute_aligned_coord(t1_hinter * this, t1_glyph_space_coord * gc, int segment_index, fixed t, bool horiz)
{   /* Returns true, if alignment zone is applied. */
    /* t is 0 or 0.5, and it is always 0 for curves. */
    enum t1_align_type align = unaligned;
    t1_glyph_space_coord gx = this->pole[segment_index].gx;
    t1_glyph_space_coord gy = this->pole[segment_index].gy;

    /*  Compute point of specified segment by parameter t : */
    if (t) {
        int next = t1_hinter__segment_end(this, segment_index);
        t1_glyph_space_coord gx1 = this->pole[next].gx;
        t1_glyph_space_coord gy1 = this->pole[next].gy;

        gx = (gx + gx1) / 2;
        gy = (gy + gy1) / 2;
    }
    vd_circle(gx, gy, 7, RGB(255,0,0));
    if (horiz) {
        t1_pole * pole = &this->pole[segment_index];
        int contour_index = pole->contour_index;
        int beg_contour_pole = this->contour[contour_index];
        int end_contour_pole = this->contour[contour_index + 1] - 2;
        int prev1 = ranger_step_b(segment_index, beg_contour_pole, end_contour_pole);
        int prev2 = ranger_step_b(prev1        , beg_contour_pole, end_contour_pole);
        int next1 = ranger_step_f(segment_index, beg_contour_pole, end_contour_pole);
        int next2 = ranger_step_f(next1        , beg_contour_pole, end_contour_pole);
        bool forwd_horiz = (any_abs(this->pole[next1].gy - pole->gy) <= this->blue_fuzz);
        bool bckwd_horiz = (any_abs(this->pole[prev1].gy - pole->gy) <= this->blue_fuzz);

        if (forwd_horiz || bckwd_horiz) {
            bool forwd_curve = (this->pole[next1].type == offcurve);
            bool bckwd_curve = (this->pole[prev1].type == offcurve);
            bool curve = (bckwd_curve && forwd_curve);
            bool convex  = (curve && this->pole[prev2].gy <= pole->gy && 
                                     this->pole[next2].gy <= pole->gy);
            bool concave = (curve && this->pole[prev2].gy >= pole->gy && 
                                     this->pole[next2].gy >= pole->gy);
            t1_zone *zone = t1_hinter__find_zone(this, pole->gy, curve, convex, concave);

            if (zone != NULL) {
                if (this->suppress_overshoots)
#                   if ADOBE_OVERSHOOT_COMPATIBILIY
                        gy = (zone->type == topzone ? zone->overshoot_y : zone->y);
#                   else
                        gy = zone->y;
#                   endif
                else {
                    t1_glyph_space_coord s = zone->y - pole->gy;
                    if (zone->type == topzone)
                        s = -s;
                    if (!curve)
                        gy = zone->y;
                    else if (s > this->overshoot_threshold) {
                        t1_glyph_space_coord ss = this->overshoot_threshold * 2;

                        if (s < ss) /* Enforce overshoot : */
                            gy = (zone->type == topzone ? zone->y + ss : zone->y - ss);
                        else { 
#                           if ADOBE_OVERSHOOT_COMPATIBILIY
                                t1_hinter__add_overshoot(this, zone, &gx, &gy);
#                           else
                                gy = (zone->type == topzone ? zone->y + ss : zone->y - ss);
#                           endif
                        }
		    }
                }
                align = (zone->type == topzone ? topzn : botzn);
            }
        }
    }
    vd_circle(gx, gy, 7, RGB(0,255,0));
    t1_hinter__align_to_grid(this, this->g2o_fraction, &gx, &gy);
    vd_circle(gx, gy, 7, RGB(0,0,255));
    *gc = (horiz ? gy : gx);
    return (align == unaligned ? aligned : align);
}

private int t1_hinter__find_stem_middle(t1_hinter * this, fixed *t, int pole_index, bool horiz)
{   /* We assume proper glyphs, see Type 1 spec, chapter 4. */
    int next = t1_hinter__next_contour_pole(this, pole_index);
    const int alpha = 10;
    bool curve = this->pole[next].type == offcurve;
    bool continuing = (horiz ? t1_hinter__is_small_angle(this, next, pole_index, 1, 0, alpha)
                             : t1_hinter__is_small_angle(this, next, pole_index, 0, 1, alpha));

    *t = (!curve && continuing ? fixed_half : 0);
    return pole_index;
}

private int t1_hinter__skip_stem(t1_hinter * this, int pole_index, bool horiz)
{   /* We assume proper glyphs, see Type 1 spec, chapter 4. */
    int i = pole_index;
    int next_pole = t1_hinter__next_contour_pole(this, i);
    int next_segm = t1_hinter__segment_end(this, i);
    long tan_x = (horiz ? 1 : 0);
    long tan_y = (horiz ? 0 : 1);

    while (t1_hinter__is_small_angle(this, i, next_pole, tan_x, tan_y, 1000) && /* The threshold is taken from scratch. */
           t1_hinter__is_small_angle(this, i, next_segm, tan_x, tan_y, 1000)) {
        i = t1_hinter__segment_end(this, i);
        next_pole = t1_hinter__next_contour_pole(this, i);
        next_segm = t1_hinter__segment_end(this, i);
    }
    return i;
}

private void t1_hinter__align_stem_commands(t1_hinter * this)
{   int i, j, jj, k;

    for(i = 0; i < this->hint_count; i++) 
        if (this->hint[i].type == vstem || this->hint[i].type == hstem) 
	    for (k = this->hint[i].range_index; k != -1; k = this->hint_range[k].next) {
		int beg_range_pole = this->hint_range[k].beg_pole;        
		int end_range_pole = this->hint_range[k].end_pole;
		bool horiz = (this->hint[i].type == hstem);

		for (j = beg_range_pole; j <= end_range_pole;) {
		    if (t1_hinter__is_stem_hint_applicable(this, &this->hint[i], j)) {
			fixed t; /* Type 1 spec implies that it is 0 for curves, 0.5 for bars */
			int segment_index = t1_hinter__find_stem_middle(this, &t, j, horiz);
			t1_glyph_space_coord gc;
			enum t1_align_type align = t1_hinter__compute_aligned_coord(this, &gc, segment_index, t, horiz);

			vd_square(this->pole[segment_index].gx, this->pole[segment_index].gy, 
				    (horiz ? 7 : 9), (i < this->primary_hint_count ? RGB(0,0,255) : RGB(0,255,0)));
			/* todo: optimize: primary commands don't need to align, if suppressed by secondary ones. */
			t1_hint__set_aligned_coord(&this->hint[i], gc, &this->pole[j], align);
			jj = j;
			j = t1_hinter__skip_stem(this, j, horiz);
			if (j < jj) { /* Rolled over contour end ? */
			    j = this->contour[this->pole[j].contour_index + 1]; /* Go to the next contour. */
			    continue;
			}
		    }
		    {   /* Step to the next pole in the range : */
			jj = j;
			j = t1_hinter__segment_end(this, j);
			if (j <= jj) /* Rolled over contour end ? */
			    j = this->contour[this->pole[j].contour_index + 1]; /* Go to the next contour. */
		    }
		}
	    }
}

private int t1_hinter__find_best_standard_width(t1_hinter * this, t1_glyph_space_coord w, bool horiz)
{   int k = (horiz ? 0 : 1), m = 0, i;
    long d = any_abs(this->stem_snap[k][0] - w);

    for (i = 1; i < this->stem_snap_count[k]; i++) {
        long dd = any_abs(this->stem_snap[k][i] - w);

        if(d > dd) {
            d = dd;
            m = i;
        }
    }
    return 0;   
}

private void t1_hinter__compute_opposite_stem_coords(t1_hinter * this)
{   int32 pixel_o_x = this->g2o_fraction * (this->align_to_subpixels ? 1 : this->subpixels_x);
    int32 pixel_o_y = this->g2o_fraction * (this->align_to_subpixels ? 1 : this->subpixels_y);
    t1_glyph_space_coord pixel_gh = any_abs(o2g_dist(this, pixel_o_x, this->heigt_transform_coef_inv));
    t1_glyph_space_coord pixel_gw = any_abs(o2g_dist(this, pixel_o_y, this->width_transform_coef_inv));
    int i, j;

    for (i = 0; i < this->hint_count; i++)
        if ((this->hint[i].type == vstem || this->hint[i].type == hstem)) {
            bool horiz = (this->hint[i].type == hstem);
            t1_glyph_space_coord pixel_g = (horiz ? pixel_gh : pixel_gw);
            t1_glyph_space_coord ag0 = this->hint[i].ag0;
            t1_glyph_space_coord ag1 = this->hint[i].ag1;
            enum t1_align_type aligned0 = this->hint[i].aligned0;
            enum t1_align_type aligned1 = this->hint[i].aligned1;
            t1_glyph_space_coord gw;
            int19 cf = (horiz ? this->heigt_transform_coef_rat : this->width_transform_coef_rat);
            int19 ci = (horiz ? this->heigt_transform_coef_inv : this->width_transform_coef_inv);

            gw = any_abs(this->hint[i].g1 - this->hint[i].g0);
            if (this->keep_stem_width && cf != 0 && ci != 0) {
		fixed pixel_o = (this->transposed ^ horiz ? pixel_o_y : pixel_o_x);
                t1_hinter_space_coord ow = g2o_dist(gw, cf);
                int19 e = ow % pixel_o; /* Pixel rounding error */
                t1_glyph_space_coord ge0 = o2g_dist(this, -e, ci);
                t1_glyph_space_coord ge1 = o2g_dist(this, pixel_o - e, ci);
                t1_glyph_space_coord ww;

                if (ow < pixel_o)
                    ge0 = ge1; /* Never round to zero */
                ww = gw + (e < pixel_o / 2 ? ge0 : ge1);
                if (this->stem_snap_count[horiz ? 0 : 1] != 0) {
                    /* Try choosing standard stem width : */
                    /* todo: optimize: sort StdW for faster search; don't lookup StdW if obviousely inapplicable. */
                    t1_glyph_space_coord d = pixel_g;
                    int stdw_index0 = t1_hinter__find_best_standard_width(this, gw + ge0, horiz);
                    int stdw_index1 = t1_hinter__find_best_standard_width(this, gw + ge1, horiz);
                    t1_glyph_space_coord w0 = this->stem_snap[horiz ? 0 : 1][stdw_index0];
                    t1_glyph_space_coord w1 = this->stem_snap[horiz ? 0 : 1][stdw_index1];
                    t1_glyph_space_coord thr0 = pixel_g * 70 / 100, thr1 = pixel_g * 35 / 100;
                    t1_glyph_space_coord  W[4];
                    t1_hinter_space_coord E[4];
                    int k = 0;

                    if (gw - thr0 <= w0 && w0 <= gw + thr1) {
                        t1_hinter_space_coord ow0 = g2o_dist(w0, cf);
                        int19 e0 = ow0 % pixel_o;

                        W[0] = w0, W[1] = w0;
                        E[0]= - e0, E[1] = pixel_o - e0;
                        k=2; 
                    }
                    if (stdw_index0 != stdw_index1 && gw - thr0 <= w1 && w1 <= gw + thr1) {
                        t1_hinter_space_coord ow1 = g2o_dist(w1, cf);
                        int19 e1 = ow1 % pixel_o;

                        W[k] = w1, W[k + 1] = w1;
                        E[k]= - e1, E[k + 1] = pixel_o - e1;
                        k+=2; 
                    }
                    for (j = 0; j < k; j++) {
                        t1_glyph_space_coord D = o2g_dist(this, E[j], ci), DD = any_abs(D);

                        if (d >= DD && W[j] + D >= pixel_g) {
                            d = DD;
                            ww = W[j] + D;
                        }
                    }
                }
                gw = ww;
            }
            if (this->hint[i].g1 - this->hint[i].g0 < 0)
                gw = -gw;
            if (aligned0 > aligned1)
                ag1 = ag0 + gw;
            else if (aligned0 < aligned1)
                ag0 = ag1 - gw;
            else {
                t1_glyph_space_coord d0 = any_abs(ag0 - this->hint[i].g0);
                t1_glyph_space_coord d1 = any_abs(ag1 - this->hint[i].g1);

		if (aligned0 == topzn || aligned1 == topzn)
		    if (gw > 0)
			ag0 = ag1 - gw;
		    else
			ag1 = ag0 + gw;
		else if (aligned0 == botzn || aligned1 == botzn)
		    if (gw < 0)
			ag0 = ag1 - gw;
		    else
			ag1 = ag0 + gw;
		else {
		    if (d0 < d1)
			ag1 = ag0 + gw;
		    else
			ag0 = ag1 - gw;
		}
            }
	    this->hint[i].ag0 = ag0;
	    this->hint[i].ag1 = ag1;
        }
}

private void t1_hinter__align_stem_poles(t1_hinter * this)
{   int i, j, k;

    for (i = 0; i < this->hint_count; i++)
        if (this->hint[i].type == vstem || this->hint[i].type == hstem) 
	    for (k = this->hint[i].range_index; k != -1; k = this->hint_range[k].next) {
		t1_hint * hint = &this->hint[i];
		int beg_range_pole = this->hint_range[k].beg_pole;
		int end_range_pole = this->hint_range[k].end_pole;
		bool horiz = (hint->type == hstem);
		t1_glyph_space_coord ag0 = this->hint[i].ag0, ag1 = this->hint[i].ag1;
		enum t1_align_type aligned0 = hint->aligned0, aligned1 = hint->aligned1;

		for (j = beg_range_pole; j <= end_range_pole; j++) {
		    t1_pole * pole = &this->pole[j];

		    if (pole->type != oncurve)
			continue;
		    if (!horiz && pole->aligned_x > aligned0 && pole->gx == hint->g0)
			ag0 = pole->ax, aligned0 = pole->aligned_x;
		    else if (!horiz && pole->aligned_x > aligned1 && pole->gx == hint->g1)
			ag1 = pole->ax, aligned1 = pole->aligned_x;
		    else if ( horiz && pole->aligned_y > aligned0 && pole->gy == hint->g0)
			ag0 = pole->ay, aligned0 = pole->aligned_y;
		    else if ( horiz && pole->aligned_y > aligned1 && pole->gy == hint->g1)
			ag1 = pole->ay, aligned1 = pole->aligned_y;
		    /* We could check for horizontality/verticality here,
		       but some fonts have unaligned serifs.
		    */
		}
		if (ag0 == hint->ag0 || ag1 == hint->ag1) {
		    if (ag0 != hint->ag0)
			ag1 += ag0 - hint->ag0;
		    else 
			ag0 += ag1 - hint->ag1;
		}
		for (j = beg_range_pole; j <= end_range_pole; j++) {
		    t1_pole * pole = &this->pole[j];

		    if (pole->type != oncurve)
			continue;
		    if (!horiz && pole->gx == hint->g0)
			pole->ax = ag0, pole->aligned_x = aligned0;
		    else if (!horiz && pole->gx == hint->g1)
			pole->ax = ag1, pole->aligned_x = aligned1;
		    else if ( horiz && pole->gy == hint->g0)
			pole->ay = ag0, pole->aligned_y = aligned0;
		    else if ( horiz && pole->gy == hint->g1)
			pole->ay = ag1, pole->aligned_y = aligned1;
		}
	    }
}

private t1_hint * t1_hinter__find_vstem_by_center(t1_hinter * this, t1_glyph_space_coord gx)
{   /* Find vstem with axis near gx : */
    int i;
    t1_hint * hint = NULL;
    t1_glyph_space_coord dx = fixed_1;

    for (i = 0; i < this->hint_count; i++)
        if (this->hint[i].type == vstem) {
            t1_glyph_space_coord d = any_abs(gx - (this->hint[i].ag0 + this->hint[i].ag1) / 2);

            if (dx > d) {
                dx = d;
                hint = &this->hint[i];
            }
        }
    return hint;
}

private void t1_hinter__process_dotsection(t1_hinter * this, int beg_pole, int end_pole)
{   /*  Since source outline must have oncurve poles at XY extremes,
        we compute bounding box from poles.
    */
    int i;
    t1_glyph_space_coord min_gx = this->pole[beg_pole].gx, min_gy = this->pole[beg_pole].gy; 
    t1_glyph_space_coord max_gx = min_gx, max_gy = min_gy;
    t1_glyph_space_coord center_gx, center_gy, center_agx, center_agy;
    t1_glyph_space_coord sx, sy;
    bool aligned_min_x = false, aligned_min_y = false, aligned_max_x = false, aligned_max_y = false;
    bool aligned_x, aligned_y;

    for (i = beg_pole + 1; i <= end_pole; i++) {
        t1_glyph_space_coord gx = this->pole[i].gx, gy = this->pole[i].gy;

        min_gx = min(min_gx, gx);
        min_gy = min(min_gy, gy);
        max_gx = max(max_gx, gx);
        max_gy = max(max_gy, gy);
    }
    for (i = beg_pole; i <= end_pole; i++) {
        if (this->pole[i].gx == min_gx)
            aligned_min_x |= this->pole[i].aligned_x;
        if (this->pole[i].gy == min_gy)
            aligned_min_y |= this->pole[i].aligned_y;
        if (this->pole[i].gx == max_gx)
            aligned_max_x |= this->pole[i].aligned_x;
        if (this->pole[i].gy == max_gy)
            aligned_max_y |= this->pole[i].aligned_y;
    }
    aligned_x = aligned_min_x && aligned_max_x;
    aligned_y = aligned_min_y && aligned_max_y;
    if (aligned_x && aligned_y)
        return; /* The contour was aligned with stem commands - nothing to do. */ 
    center_gx = center_agx = (min_gx + max_gx) / 2;
    center_gy = center_agy = (min_gy + max_gy) / 2;
    vd_circle(center_agx, center_agy, 7, RGB(255,0,0));
    if (!aligned_x) {
        /* Heuristic : apply vstem if it is close to the center : */
        t1_hint * hint = t1_hinter__find_vstem_by_center(this, center_gx);
        if (hint != NULL) {
            center_agx = (hint->ag0 + hint->ag1) / 2; /* Align with vstem */
            aligned_x = true;
        }
    }
    vd_circle(center_agx, center_agy, 7, RGB(0,255,0));
    t1_hinter__align_to_grid(this, this->g2o_fraction / 2, &center_agx, &center_agy);
    vd_circle(center_agx, center_agy, 7, RGB(0,0,255));
    sx = center_agx - center_gx;
    sy = center_agy - center_gy;
    if (aligned_x)
        sx = 0;
    if (aligned_y)
        sy = 0;
    /* Shift the contour (sets alignment flags to prevent interpolation) : */
    for (i = beg_pole; i <= end_pole; i++) {
        this->pole[i].ax = this->pole[i].gx + sx;
        this->pole[i].ay = this->pole[i].gy + sy;
        this->pole[i].aligned_x |= !aligned_x; /* Prevent interpolation if we aligned it here. */
        this->pole[i].aligned_y |= !aligned_y;
    }
}

private void t1_hinter__process_dotsections(t1_hinter * this)
{   int i;

    for(i=0; i<this->hint_count; i++)
        if (this->hint[i].type == dot) {
            int contour_index = this->hint_range[this->hint[i].range_index].contour_index;
            int beg_pole = this->contour[contour_index];
            int end_pole = this->contour[contour_index] - 2;

            t1_hinter__process_dotsection(this, beg_pole, end_pole);
        }
}

private void t1_hinter__interpolate_other_poles(t1_hinter * this)
{   int i, j, k;

    for (k = 0; k<2; k++) { /* X, Y */
        t1_glyph_space_coord *p_gc = (!k ? &this->pole[0].gx : &this->pole[0].gy);
        t1_glyph_space_coord *p_ac = (!k ? &this->pole[0].ax : &this->pole[0].ay);
        enum t1_align_type *p_f = (!k ? &this->pole[0].aligned_x : &this->pole[0].aligned_y);
        int offset_gc = (char *)p_gc - (char *)&this->pole[0];
        int offset_ac = (char *)p_ac - (char *)&this->pole[0];
        int offset_f  = (char *)p_f -  (char *)&this->pole[0];

        for (i = 0; i < this->contour_count; i++) {
            int beg_contour_pole = this->contour[i];
            int end_contour_pole = this->contour[i + 1] - 2;
            int range_beg;

            for (j = beg_contour_pole; j <= end_contour_pole; j++)
                if (*member_prt(enum t1_align_type, &this->pole[j], offset_f))
                    break;
            if (j > end_contour_pole)
                continue;
            range_beg = j;
            do {
                int start_pole = j, stop_pole = -1;
                t1_glyph_space_coord min_a, max_a;
		t1_glyph_space_coord min_g, max_g, g0, g1;
		int min_i = start_pole, max_i = start_pole, cut_l, l;

		do {
		    int min_l = 0, max_l = 0;

		    g0 = *member_prt(t1_glyph_space_coord, &this->pole[start_pole], offset_gc);
		    min_g = g0;
		    max_g = g0;
		    min_i = start_pole;
		    max_i = start_pole;
		    for (j = ranger_step_f(start_pole,  beg_contour_pole, end_contour_pole), l = 1; 
		         j != start_pole;
			 j = ranger_step_f(j,  beg_contour_pole, end_contour_pole), l++) {
			t1_glyph_space_coord g = * member_prt(t1_glyph_space_coord, &this->pole[j], offset_gc);

			if (min_g > g)
			    min_g = g, min_i = j, min_l = l;
			if (max_g < g)
			    max_g = g, max_i = j, max_l = l;
			if (*member_prt(enum t1_align_type, &this->pole[j], offset_f))
			    break;
			if (j == stop_pole)
			    break;
		    }
		    stop_pole = j;
		    cut_l = l;
		    g1 = * member_prt(t1_glyph_space_coord, &this->pole[stop_pole], offset_gc);
		    if (min_i != start_pole && min_l < cut_l && min_g != g0 && min_g != g1)
			stop_pole = min_i, cut_l = min_l;
		    if (max_i != start_pole && max_l < cut_l && max_g != g0 && max_g != g1)
			stop_pole = max_i, cut_l = max_l;
		} while (cut_l < l);
		if (start_pole == stop_pole)
		    break; /* safety */
                /* Now start_pole and end_pole point to the contour interval to interpolate. */
		if (g0 < g1) {
		    min_g = g0;
		    max_g = g1;
		    min_i = start_pole;
		    max_i = stop_pole;
		} else {
		    min_g = g1;
		    max_g = g0;
		    min_i = stop_pole;
		    max_i = start_pole;
		}
                min_a = * member_prt(t1_glyph_space_coord, &this->pole[min_i], offset_ac);
                max_a = * member_prt(t1_glyph_space_coord, &this->pole[max_i], offset_ac);
#               if 0
                    /*  This was an attempt to keep relative stem lenght for stems,
                        which's longitude is not hinted (horizontal arms in "E", "F" in StoneSans-Bold).
                        This attempt was not successful.
                        Probably a smarter algorithm is useful.
                    */
                    if (!*member_prt(enum t1_align_type, &this->pole[min_i], offset_f))
                        min_a = min_g + Min(max_a-max_g, Min (* member_prt(t1_glyph_space_coord, &this->pole[start_pole], offset_ac) - 
                                                              * member_prt(t1_glyph_space_coord, &this->pole[start_pole], offset_gc),
                                                              * member_prt(t1_glyph_space_coord, &this->pole[stop_pole ], offset_ac) - 
                                                              * member_prt(t1_glyph_space_coord, &this->pole[stop_pole ], offset_gc)));
                    else if (!*member_prt(enum t1_align_type, &this->pole[max_i], offset_f))
                        max_a = max_g + Max(min_a-min_g, Max (* member_prt(t1_glyph_space_coord, &this->pole[start_pole], offset_ac) - 
                                                              * member_prt(t1_glyph_space_coord, &this->pole[start_pole], offset_gc),
                                                              * member_prt(t1_glyph_space_coord, &this->pole[stop_pole ], offset_ac) - 
                                                              * member_prt(t1_glyph_space_coord, &this->pole[stop_pole ], offset_gc)));
#               endif
                for (j = start_pole; ; 
                     j = ranger_step_f(j,  beg_contour_pole, end_contour_pole)) {
                    t1_glyph_space_coord g = * member_prt(t1_glyph_space_coord, &this->pole[j], offset_gc);

                    if (g == min_g)
                        * member_prt(t1_glyph_space_coord, &this->pole[j], offset_ac) = min_a;
                    if (g == max_g)
                        * member_prt(t1_glyph_space_coord, &this->pole[j], offset_ac) = max_a;
                    if(j == stop_pole)
                        break;
                }
                if (min_g < max_g) {
                    int24 div = max_g - min_g;
                    int24 mul = max_a - min_a;
                    /*  Due to glyph coordinate definition, div is not smaller than 2^12.

                        In the following cycle we need to compute x*mul/div for 24-bit integers,
                        We replace this expression with x*u/2^12 where u = mul*2^12/div
                        (note that it's an approximation with relative precision 2^-12).

                        If mul or div are big, we drop 5 bits to fit them into int19.
                        Note that it's another approximation with relative precision 2^-14.
                        Let now they are m0 and d.

                        Then we compute :

                        q1 = m0 / d, r1 = m0 % d, m1 = r1 << 12;   // r1 < 2^19, m0 < 2^12
                        q2 = m1 / d, r2 = m1 % d, m2 = r2 << 12;   // r2 < 2^19, m1 < 2^12
                        q3 = m2 / d, r3 = m2 % d, m3 = r3 << 12;   // r3 < 2^19, m2 < 2^12
                        and so on.

                        We have :

                        u = ((q1 + (q2 >> 12) + (q3 >> 24) + ...) << 12
                          = (q1 << 12) + q2 + (q3 >> 12) + ...
                          = (q1 << 12) + q2 .

                        Thus we got pretty nice formula without iterations. Implementing it below.
                    */
                    int24 m0 = mul, d = div, q1, q2, r1, m1, u;

                    if (m0 >= (1 << 19) || d >= (1 << 19))
                        m0 >>= 5, d >>= 5;
                    q1 = m0 / d, r1 = m0 % d, m1 = r1 << 12;
                    q2 = m1 / d;
                    u = (q1 << 12) + q2;
                    for (j = ranger_step_f(start_pole,  beg_contour_pole, end_contour_pole); j != stop_pole; 
                         j = ranger_step_f(j,  beg_contour_pole, end_contour_pole)) {
                        t1_glyph_space_coord g = *member_prt(t1_glyph_space_coord, &this->pole[j], offset_gc);

                        if (g != min_g && g !=max_g) {
                            t1_glyph_space_coord *a = member_prt(t1_glyph_space_coord, &this->pole[j], offset_ac);
                            t1_glyph_space_coord x = g - min_g;
                            t1_glyph_space_coord h = mul_shift(x, u, 12); /* It is x*u/2^12 */

                            /* h = (int24)(x * (double)mul / div + 0.5); Uncomment this to disable our tricks. */
                            *a = min_a + h;
                        }
                    }
                }
                j = stop_pole;
            } while (j != range_beg);
        }
    }
}

private int t1_hinter__export(t1_hinter * this)
{   int i, j, code;
    fixed fx, fy;

    for(i = 0; ; i++) {
        int beg_pole = this->contour[i];
        int end_pole = this->contour[i + 1] - 2;
        t1_pole *pole = & this->pole[beg_pole];

        g2d(this, pole->ax, pole->ay, &fx, &fy);
        code = gx_path_add_point(this->output_path, fx, fy);
	if (code < 0)
	    return code;
	if (i >= this->contour_count)
	    break;
	/* vd_setcolor(RGB(255,0,0)); 
           vd_moveto(fx,fy);
	*/
        for(j = beg_pole + 1; j <= end_pole; j++) {
            pole = & this->pole[j];
            g2d(this, pole->ax, pole->ay, &fx, &fy);
            if (pole->type == oncurve) {
                code = gx_path_add_line(this->output_path, fx, fy);
		if (code < 0)
		    return code;
                /* vd_setcolor(RGB(255,0,0));
                   vd_lineto(fx,fy);
		*/
            } else {
                int j1 = j + 1, j2 = (j + 2 > end_pole ? beg_pole : j + 2);
                fixed fx1, fy1, fx2, fy2;

                g2d(this, this->pole[j1].ax, this->pole[j1].ay, &fx1, &fy1);
                g2d(this, this->pole[j2].ax, this->pole[j2].ay, &fx2, &fy2);
                code = gx_path_add_curve(this->output_path, fx, fy, fx1, fy1, fx2, fy2);
		if (code < 0)
		    return code;
                /* vd_setcolor(RGB(255,0,0));
                   vd_curveto(fx,fy,fx1,fy1,fx2,fy2);
		*/
                j+=2;
            }
        }
        code = gx_path_close_subpath(this->output_path);
	if (code < 0)
	    return code;
    }
    return 0;
}

private int t1_hinter__add_trailing_moveto(t1_hinter * this)
{   t1_glyph_space_coord gx = this->width_gx, gy = this->width_gy;

#   if 0 /* Don't align because the old code desn't. Not sure though. */
	t1_hinter__align_to_grid(this, this->g2o_fraction, &gx, &gy);
#   endif
    return t1_hinter__rmoveto(this, gx - this->cx, gy - this->cy);
}

private int t1_hinter__add_full_width_hint(t1_hinter * this)
{   /*  This is a hewristic.
        This adds full width vstem being useful for characters like 'c', 'A',
        to fit their sloped tails to the rater. 
    */
    t1_glyph_space_coord min_gx, min_gy, max_gx, max_gy;
    t1_glyph_space_coord s = this->subglyph_orig_gx;
    t1_glyph_space_coord gx0, gx1;
    t1_hint hint;
    int i, code;

    if (this->pole_count == 0)
        return 0;
    /* Compute BBox : */
    min_gx = this->pole[0].gx, min_gy = this->pole[0].gy;
    max_gx = min_gx, max_gy = min_gy;
    for (i = 1; i < this->pole_count - 1; i++) {
        t1_hinter_space_coord gx = this->pole[i].gx, gy = this->pole[i].gy;

        min_gx = min(min_gx, gx);
        min_gy = min(min_gy, gy);
        max_gx = max(max_gx, gx);
        max_gy = max(max_gy, gy);
    }
    gx0 = min_gx - s, gx1 = max_gx - s;
    /* Check if such hint already presents : */
    for (i = 0; i < this->hint_count; i++)
        if (this->hint[i].type == vstem &&
            ((this->hint[i].g0 == gx0 && this->hint[i].g1 == gx1) || 
             (this->hint[i].g0 == gx1 && this->hint[i].g1 == gx0)))
            return 0;
    /* Insert new hint in very beginning : */
    code = t1_hinter__stem(this, vstem, 0, gx0, gx1);
    if (code < 0)
	return code;
    hint = this->hint[this->hint_count - 1];
    for (i = this->hint_count - 1; i > 0; i--)
        this->hint[i] = this->hint[i - 1];
    this->hint[0] = hint;
    return 0;
}

int t1_hinter__endglyph(t1_hinter * this)
{   int code;

    if (!vd_enabled) { /* Maybe enabled in t1_hinter__set_mapping. */
	vd_get_dc('h');
	vd_set_shift(VD_SHIFT_X, VD_SHIFT_Y);
	vd_set_scale(VD_SCALE);
	vd_set_origin(0, 0);
#	if !VD_DRAW_IMPORT
	vd_erase(RGB(255, 255, 255));
#	endif
    }
    t1_hinter__paint_raster_grid(this);
    t1_hinter__simplify_representation(this);
    code = t1_hinter__add_trailing_moveto(this);
    if (code < 0)
	goto exit;
    t1_hinter__paint_glyph(this, false);
    if (!this->disable_hinting && (this->grid_fit_x || this->grid_fit_y)) {
        /* t1_hinter__add_full_width_hint(this); Gives worse results. */
	if (this->FontType == 1)
	    t1_hinter__compute_type1_stem_ranges(this);
	else
	    t1_hinter__compute_type2_stem_ranges(this);
        t1_hinter__align_stem_commands(this);
        t1_hinter__compute_opposite_stem_coords(this);
        /* todo :  t1_hinter__align_stem3(this); */
        t1_hinter__align_stem_poles(this);
        t1_hinter__process_dotsections(this);
        t1_hinter__interpolate_other_poles(this);
        t1_hinter__paint_glyph(this, true);
    }
   if (vd_enabled) {
        double_matrix m;

        fraction_matrix__to_double(&this->ctmi, &m);
        vd_set_scaleXY(vd_get_scale_x * m.xx, vd_get_scale_y * m.yy);
        vd_set_origin(this->orig_dx, this->orig_dy);
        /*  fixme : general transform requires changes to vdtrace.
	    Current implementation paints exported rotated glyph in wrong coordinates.
	*/
    }
    code = t1_hinter__export(this);
exit:
    t1_hinter__free_arrays(this);
    vd_release_dc;
    return code;
}
