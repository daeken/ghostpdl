# Copyright (C) 2001-2018 Artifex Software, Inc.
# All Rights Reserved.
#
# This software is provided AS-IS with no warranty, either express or
# implied.
#
# This software is distributed under license and may not be copied,
# modified or distributed except as expressly authorized under the terms
# of the license contained in the file LICENSE in this distribution.
#
# Refer to licensing information at http://www.artifex.com or contact
# Artifex Software, Inc.,  1305 Grant Avenue - Suite 200, Novato,
# CA 94945, U.S.A., +1(415)492-9861, for further information.
#
# Platform-independent makefile for the PDF interpreter

PDFSRC      = $(PDFSRCDIR)$(D)
PDFGEN      = $(PDFGENDIR)$(D)
PDFOBJ      = $(PDFOBJDIR)$(D)
PDFO_       = $(O_)$(PDFOBJ)
PLOBJ       = $(PLOBJDIR)$(D)

PDFCCC  = $(CC_) $(JPX_CFLAGS) $(D_)PDF_INCLUDED$(_D) $(I_)$(PDFSRCDIR)$(_I) $(I_)$(PDFGENDIR)$(_I) \
	$(I_)$(PLSRCDIR)$(_I) $(I_)$(GLSRCDIR)$(_I) \
	$(I_)$(EXPATINCDIR)$(_I) $(I_)$(JPEGXR_SRCDIR)$(_I) $(I_)$(ZSRCDIR)$(_I) \
        $(I_)$(JPX_OPENJPEG_I_)$(_I) $(C_)

# Define the name of this makefile.
PDF_MAK     = $(PDFSRC)pdf.mak $(TOP_MAKEFILES)

PDF.clean: PDF.config-clean PDF.clean-not-config-clean

PDF.clean-not-config-clean:
	$(RM_) $(PDFOBJ)*.$(OBJ)

PDF.config-clean: clean_gs
	$(RM_) $(PDFOBJ)*.dev
	$(RM_) $(PDFOBJ)devs.tr5

PDF_TOP_OBJ=$(PDFOBJDIR)$(D)pdftop.$(OBJ)
PDF_TOP_OBJS= $(PDF_TOP_OBJ) $(PDFOBJDIR)$(D)pdfimpl.$(OBJ)

PDFINCLUDES=$(PDFSRC)*.h $(PDFOBJ)arch.h $(strmio_h) $(stream_h) $(gsmatrix_h) $(gslparam_h)\
	$(gstypes_h) $(szlibx_h) $(spngpx_h) $(sstring_h) $(sa85d_h) $(scfx_h) $(srlx_h)\
	$(jpeglib__h) $(sdct_h) $(spdiffx_h)

$(PDFOBJ)ghostpdf.$(OBJ): $(PDFSRC)ghostpdf.c $(PDFINCLUDES) $(plmain_h) $(PDF_MAK) $(MAKEDIRS)
	$(PDFCCC) $(PDFSRC)ghostpdf.c $(PDFO_)ghostpdf.$(OBJ)

$(PDFOBJ)pdf_dict.$(OBJ): $(PDFSRC)pdf_dict.c $(PDFINCLUDES) $(PDF_MAK) $(MAKEDIRS)
	$(PDFCCC) $(PDFSRC)pdf_dict.c $(PDFO_)pdf_dict.$(OBJ)

$(PDFOBJ)pdf_array.$(OBJ): $(PDFSRC)pdf_array.c $(PDFINCLUDES) $(PDF_MAK) $(MAKEDIRS)
	$(PDFCCC) $(PDFSRC)pdf_array.c $(PDFO_)pdf_array.$(OBJ)

$(PDFOBJ)pdf_xref.$(OBJ): $(PDFSRC)pdf_xref.c $(PDFINCLUDES) $(PDF_MAK) $(MAKEDIRS)
	$(PDFCCC) $(PDFSRC)pdf_xref.c $(PDFO_)pdf_xref.$(OBJ)

$(PDFOBJ)pdf_font.$(OBJ): $(PDFSRC)pdf_font.c $(PDFINCLUDES) $(PDF_MAK) $(MAKEDIRS)
	$(PDFCCC) $(PDFSRC)pdf_font.c $(PDFO_)pdf_font.$(OBJ)

$(PDFOBJ)pdf_text.$(OBJ): $(PDFSRC)pdf_text.c $(PDFINCLUDES) $(PDF_MAK) $(MAKEDIRS)
	$(PDFCCC) $(PDFSRC)pdf_text.c $(PDFO_)pdf_text.$(OBJ)

$(PDFOBJ)pdf_shading.$(OBJ): $(PDFSRC)pdf_shading.c $(PDFINCLUDES) $(PDF_MAK) $(MAKEDIRS)
	$(PDFCCC) $(PDFSRC)pdf_shading.c $(PDFO_)pdf_shading.$(OBJ)

$(PDFOBJ)pdf_func.$(OBJ): $(PDFSRC)pdf_func.c $(PDFINCLUDES) $(PDF_MAK) $(MAKEDIRS)
	$(PDFCCC) $(PDFSRC)pdf_func.c $(PDFO_)pdf_func.$(OBJ)

$(PDFOBJ)pdf_image.$(OBJ): $(PDFSRC)pdf_image.c $(PDFINCLUDES) $(PDF_MAK) $(MAKEDIRS)
	$(PDFCCC) $(PDFSRC)pdf_image.c $(PDFO_)pdf_image.$(OBJ)

$(PDFOBJ)pdf_stack.$(OBJ): $(PDFSRC)pdf_stack.c $(PDFINCLUDES) $(PDF_MAK) $(MAKEDIRS)
	$(PDFCCC) $(PDFSRC)pdf_stack.c $(PDFO_)pdf_stack.$(OBJ)

$(PDFOBJ)pdf_gstate.$(OBJ): $(PDFSRC)pdf_gstate.c $(PDFINCLUDES) $(PDF_MAK) $(MAKEDIRS)
	$(PDFCCC) $(PDFSRC)pdf_gstate.c $(PDFO_)pdf_gstate.$(OBJ)

$(PDFOBJ)pdf_colour.$(OBJ): $(PDFSRC)pdf_colour.c $(PDFINCLUDES) $(gscolor1_h) $(PDF_MAK) $(MAKEDIRS)
	$(PDFCCC) $(PDFSRC)pdf_colour.c $(PDFO_)pdf_colour.$(OBJ)

$(PDFOBJ)pdf_path.$(OBJ): $(PDFSRC)pdf_path.c $(PDFINCLUDES) $(PDF_MAK) $(MAKEDIRS)
	$(PDFCCC) $(PDFSRC)pdf_path.c $(PDFO_)pdf_path.$(OBJ)

$(PDFOBJ)pdf_loop_detect.$(OBJ): $(PDFSRC)pdf_loop_detect.c $(PDFINCLUDES) $(PDF_MAK) $(MAKEDIRS)
	$(PDFCCC) $(PDFSRC)pdf_loop_detect.c $(PDFO_)pdf_loop_detect.$(OBJ)

$(PDFOBJ)pdf_int.$(OBJ): $(PDFSRC)pdf_int.c $(PDFINCLUDES) $(plmain_h) $(PDF_MAK) $(MAKEDIRS)
	$(PDFCCC) $(PDFSRC)pdf_int.c $(PDFO_)pdf_int.$(OBJ)

$(PDFOBJ)pdf_file.$(OBJ): $(PDFSRC)pdf_file.c $(sjpeg_h) $(PDFINCLUDES) $(PDF_MAK) $(MAKEDIRS)
	$(PDFCCC) $(PDFSRC)pdf_file.c $(PDFO_)pdf_file.$(OBJ)

$(PDFGEN)pdfimpl.c: $(PLSRC)plimpl.c $(PDF_MAK) $(MAKEDIRS)
	$(CP_) $(PLSRC)plimpl.c $(PDFGEN)pdfimpl.c

$(PLOBJ)pdfimpl.$(OBJ): $(PDFGEN)pdfimpl.c          \
                        $(AK)                       \
                        $(memory__h)                \
                        $(scommon_h)                \
                        $(gxdevice_h)               \
                        $(pltop_h)                  \
                        $(PDF_MAK)                  \
                        $(MAKEDIRS)
	$(PDFCCC) $(PDFGEN)pdfimpl.c $(PDFO_)pdfimpl.$(OBJ)

$(PDF_TOP_OBJ): $(PDFSRC)pdftop.c $(plmain_h) $(pltop_h) $(PDFINCLUDES) $(GLOBJ)gconfig.$(OBJ) \
                $(pconfig_h) $(PDF_MAK) $(MAKEDIRS)
	$(PDFCCC) $(PDFSRC)pdftop.c $(PDFO_)pdftop.$(OBJ)

# sa85d is explicitly included here, which seems wrong, especially as none of
# the other decoders have to be. spdiff.c as well. sfilter1 is included in filters.dev, still needs to be here.

PDF_OBJS=\
    $(PDFOBJ)pdf_loop_detect.$(OBJ)\
    $(PDFOBJ)ghostpdf.$(OBJ)\
    $(PDFOBJ)pdf_dict.$(OBJ)\
    $(PDFOBJ)pdf_array.$(OBJ)\
    $(PDFOBJ)pdf_xref.$(OBJ)\
    $(PDFOBJ)pdf_int.$(OBJ)\
    $(PDFOBJ)pdf_file.$(OBJ)\
    $(PDFOBJ)pdf_path.$(OBJ)\
    $(PDFOBJ)pdf_colour.$(OBJ)\
    $(PDFOBJ)pdf_gstate.$(OBJ)\
    $(PDFOBJ)pdf_stack.$(OBJ)\
    $(PDFOBJ)pdf_image.$(OBJ)\
    $(PDFOBJ)pdf_font.$(OBJ)\
    $(PDFOBJ)pdf_text.$(OBJ)\
    $(PDFOBJ)pdf_shading.$(OBJ)\
    $(PDFOBJ)pdf_func.$(OBJ)\
    $(GLOBJ)sa85d.$(OBJ)\
    $(GLOBJ)spdiff.$(OBJ)\
    $(GLOBJ)sfilter1.$(OBJ)\
    $(GLOBJ)saes.$(OBJ)\
    $(GLOBJ)aes.$(OBJ)



# NB - note this is a bit squirrely.  Right now the pjl interpreter is
# required and shouldn't be and PLOBJ==PDFGEN is required.

$(PDFOBJ)gpdf.dev: $(ECHOGS_XE) $(PDF_OBJS)  $(PDFOBJDIR)$(D)expat.dev $(JPEGXR_GENDIR)$(D)jpegxr.dev \
                  $(PDFGEN)pl.dev $(PDFGEN)$(PL_SCALER).dev $(PDFGEN)pjl.dev $(PDF_MAK) $(MAKEDIRS)
	$(SETMOD) $(PDFOBJ)gpdf $(PDF_OBJS)
	$(ADDMOD) $(PDFOBJ)gpdf -include $(PDFGEN)$(PL_SCALER) $(PDFGEN)pjl.dev
