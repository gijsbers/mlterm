/* -*- c-basic-offset:2; tab-width:2; indent-tabs-mode:nil -*- */

#ifndef ___UI_FONT_H__
#define ___UI_FONT_H__

#include "../ui_font.h"

int ui_modify_bitmaps(XFontStruct *xfont, u_char **bitmaps, u_int num,
                      u_int *height, u_int *ascent);

u_char *ui_get_bitmap(XFontStruct *xfont, u_char *ch, size_t len, int use_ot_layout,
                      int get_space_glyph, XFontStruct **compl_xfont);

#define ui_get_bitmap_line(xfont, bitmap, offset_bytes, bitmap_line) \
  ((bitmap) &&                                                       \
   memcmp(((bitmap_line) = (bitmap) + (offset_bytes)), "\x0\x0\x0",  \
          (xfont)->glyph_width_bytes) != 0)

/* x & 7 == x % 8 */
#define ui_get_bitmap_cell(bitmap_line, x) ((bitmap_line)[(x) / 8] & (1 << (8 - ((x)&7) - 1)))

#endif
