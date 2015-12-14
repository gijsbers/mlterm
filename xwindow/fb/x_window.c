/*
 *	$Id$
 */

#include  "../x_window.h"

#include  <string.h>
#include  <kiklib/kik_debug.h>
#include  <kiklib/kik_mem.h>
#include  <kiklib/kik_unistd.h>	/* kik_usleep */

#include  "x_display.h"
#include  "x_font.h"


#define  MAX_CLICK  3			/* max is triple click */

/* win->width is not multiples of (win)->width_inc in framebuffer. */
#define  RIGHT_MARGIN(win) \
	((win)->width_inc ? ((win)->width - (win)->min_width) % (win)->width_inc : 0)
#define  BOTTOM_MARGIN(win) \
	((win)->height_inc ? ((win)->height - (win)->min_height) % (win)->height_inc : 0)

#ifdef  USE_GRF
static x_color_t  black = { TP_COLOR , 0 , 0 , 0 , 0 } ;
#endif

#define  ParentRelative  (1L)
#define  DummyPixmap  (2L)


/* --- static variables --- */

static int  click_interval = 250 ;	/* millisecond, same as xterm. */


/* --- static functions --- */

static int
scroll_region(
	x_window_t *  win ,
	int  src_x ,
	int  src_y ,
	u_int  width ,
	u_int  height ,
	int  dst_x ,
	int  dst_y
	)
{
	if( ! win->is_mapped || ! x_window_is_scrollable( win))
	{
		return  0 ;
	}

	x_display_copy_lines(
			src_x + win->x + win->hmargin ,
			src_y + win->y + win->vmargin ,
			dst_x + win->x + win->hmargin ,
			dst_y + win->y + win->vmargin ,
			width , height) ;

	return  1 ;
}

/*
 * copy_pixel() is called more than twice, so if it is implemented as a function
 * it may be uninlined in compiling.
 * dst should be aligned.
 */
#define  copy_pixel( dst , pixel , bpp) \
	switch( bpp) \
	{ \
	case  1: \
		*(dst) = pixel ; \
		break ; \
	case  2: \
		*((u_int16_t*)(dst)) = (pixel) ; \
		break ; \
	/* case  4: */ \
	default: \
		*((u_int32_t*)(dst)) = (pixel) ; \
	} \

static inline u_int16_t *
memset16(
	u_int16_t *  dst ,
	u_int16_t  i ,
	u_int  len
	)
{
	u_int  count ;

	for( count = 0 ; count < len ; count++)
	{
		dst[count] = i ;
	}

	return  dst ;
}

static inline u_int32_t *
memset32(
	u_int32_t *  dst ,
	u_int32_t  i ,
	u_int  len
	)
{
	u_int  count ;

	for( count = 0 ; count < len ; count++)
	{
		dst[count] = i ;
	}

	return  dst ;
}

#ifdef  USE_FREETYPE
#define  BLEND(fg,bg,alpha)  ((bg) + ((fg) - (bg)) * (alpha) / 255)
static int
copy_blended_pixel(
	Display *  display ,
	u_char *  dst ,
	u_char **  bitmap ,
	u_long  fg ,
	u_long  bg ,
	u_int  bpp
	)
{
	int  a1 = *((*bitmap)++) ;
	int  a2 = *((*bitmap)++) ;
	int  a3 = *((*bitmap)++) ;

	/* "> 20" is to avoid garbages in screen. */
	if( a1 > 20 || a2 > 20 || a3 > 20)
	{
		int  r1 ;
		int  g1 ;
		int  b1 ;
		int  r2 ;
		int  g2 ;
		int  b2 ;

		r1 = PIXEL_RED(fg,display->rgbinfo) & 0xff ;
		g1 = PIXEL_GREEN(fg,display->rgbinfo) & 0xff ;
		b1 = PIXEL_BLUE(fg,display->rgbinfo) & 0xff ;
		r2 = PIXEL_RED(bg,display->rgbinfo) & 0xff ;
		g2 = PIXEL_GREEN(bg,display->rgbinfo) & 0xff ;
		b2 = PIXEL_BLUE(bg,display->rgbinfo) & 0xff ;

		copy_pixel( dst ,
			RGB_TO_PIXEL( BLEND(r1,r2,a1) , BLEND(g1,g2,a2) ,
				BLEND(b1,b2,a3) , display->rgbinfo) ,
			bpp) ;

		return  1 ;
	}
	else
	{
		return  0 ;
	}
}
#endif

static int
draw_string(
	x_window_t *  win ,
	x_font_t *  font ,
	x_color_t *  fg_color ,
	x_color_t *  bg_color ,	/* must be NULL if wall_picture_bg is 1 */
	int  x ,
	int  y ,
	u_char *  str ,	/* 'len * ch_len' bytes */
	u_int  len ,
	u_int  ch_len ,
	int  wall_picture_bg
	)
{
	u_int  bpp ;
	XFontStruct *  xfont ;
	u_char *  src ;
	u_char *  p ;
	u_char **  bitmaps ;
	size_t  size ;
	u_int  font_height ;
	u_int  font_ascent ;
	u_int  font_width ;
	int  y_off ;
	u_char *  picture ;
	size_t  picture_line_len ;
	u_int  count ;
	int  src_bg_is_set ;
	int  orig_x ;
	u_int  clip_bottom ;

	if( ! win->is_mapped)
	{
		return  0 ;
	}

	bpp = win->disp->display->bytes_per_pixel ;
	font_width = font->width ;
	xfont = font->xfont ;

#if  defined(USE_FREETYPE)
	if( xfont->is_aa && font->is_proportional)
	{
		u_int  width ;

		/* width_full == glyph_width_bytes / 3 */
		if( x + ( width = len * xfont->width_full) > win->width)
		{
			width = win->width - x ;
		}

		size = width * bpp ;
	}
	else
#endif
	{
		size = len * font_width * bpp ;
	}

	if( ! ( src = alloca( size)) ||
	    ! ( bitmaps = alloca( ( len * sizeof(*bitmaps)))))
	{
		return  0 ;
	}

	if( ch_len == 1)
	{
		for( count = 0 ; count < len ; count++)
		{
			bitmaps[count] = x_get_bitmap( xfont , str + count , 1) ;
		}
	}
	else /* if( ch_len == 2) */
	{
		for( count = 0 ; count < len ; count++)
		{
			if( 0xd8 <= str[0] && str[0] <= 0xdb)
			{
				len -- ;

				if( count >= len)
				{
					/* ignored */
					break ;
				}

				if( 0xdc <= str[2] && str[2] <= 0xdf)
				{
					/* surrogate pair */
					mkf_int_to_bytes( str , 4 ,
						(str[0] - 0xd8) * 0x100 * 0x400 + str[1] * 0x400 +
						(str[2] - 0xdc) * 0x100 + str[3] + 0x10000) ;
					ch_len = 4 ;
				}
				else
				{
					/* illegal, ignored. */
					len -- ;
					count -- ;
					str += 4 ;
					continue ;
				}
			}

			bitmaps[count] = x_get_bitmap( xfont , str , ch_len) ;

			str += ch_len ;
			ch_len = 2 ;
		}
	}

	/*
	 * Check if font->height or font->ascent excesses the display height,
	 * because font->height doesn't necessarily equals to the height of the US-ASCII font.
	 *
	 * XXX
	 * On the other hand, font->width is always the same (or exactly double) for now.
	 */

	font_ascent = font->ascent ;
	font_height = font->height ;

	if( win->clip_height == 0)
	{
		clip_bottom = win->height ;
	}
	else
	{
		clip_bottom = win->clip_y + win->clip_height ;
	}

	if( y >= clip_bottom + font_ascent)
	{
		return  0 ;
	}
	else if( y + font_height - font_ascent >= clip_bottom)
	{
		font_height -= (y + font_height - font_ascent - clip_bottom + 1) ;

		if( font_height < font_ascent)
		{
			/* XXX I don't know why but without -1 causes line gap */
			y -= (font_ascent - font_height - 1) ;
			font_ascent = font_height ;
		}
	}

	if( y + font_height - font_ascent < win->clip_y)
	{
		return  0 ;
	}
	else if( y < win->clip_y + font_ascent)
	{
		y_off = win->clip_y + font_ascent - y ;
	}
	else
	{
		y_off = 0 ;
	}

	/* Following check is done by the caller of this function. */
#if  0
	/*
	 * Check if font->width * len excesses the display height
	 * because full width fonts can be used for characters which console
	 * applications regard as half width.
	 */

	if( x + font_width * len > win->width)
	{
		len = (win->width - x) / font_width ;
	}
#endif

	x += (win->hmargin + win->x) ;
	y = y + (win->vmargin + win->y) - font_ascent ;

	if( wall_picture_bg)
	{
		/* bg_color is always NULL */
		Pixmap  pic ;
		int  pic_x ;
		int  pic_y ;

		if( win->wall_picture == ParentRelative)
		{
			pic = win->parent->wall_picture ;
			pic_x = x ;
			pic_y = y ;
		}
		else
		{
			pic = win->wall_picture ;
			pic_x = x - win->x ;
			pic_y = y - win->y ;
		}

		picture_line_len = pic->width * bpp ;
		picture = pic->image +
			  (pic_y + y_off) * picture_line_len +
			  /* - picture_line_len is for picture += picture_line_len below. */
			  pic_x * bpp - picture_line_len ;

		src_bg_is_set = 1 ;
	}
	else
	{
		picture = NULL ;

		if( bg_color)
		{
			src_bg_is_set = 1 ;
		}
		else
		{
			src_bg_is_set = 0 ;
		}
	}

#if  1
	/* Optimization for most cases */
	if( src_bg_is_set && ! font->double_draw_gap
	#ifdef  USE_FREETYPE
		&& ! xfont->face
	#endif
		)
	{
		u_char *  bitmap_line ;
		int  x_off ;
		u_int  glyph_width ;

		glyph_width = font_width - font->x_off ;

		switch( bpp)
		{
		case  1:
			for( ; y_off < font_height ; y_off++)
			{
				p = ( picture ?
					memcpy( src , (picture += picture_line_len) , size) :
					memset( src , bg_color->pixel , size)) ;

				for( count = 0 ; count < len ; count++)
				{
					if( ! x_get_bitmap_line( xfont ,
						bitmaps[count] , y_off , bitmap_line))
					{
						p += font_width ;
					}
					else
					{
						p += font->x_off ;

						for( x_off = 0 ; x_off < glyph_width ; x_off++)
						{
							if( x_get_bitmap_cell( bitmap_line ,
									x_off))
							{
								*p = fg_color->pixel ;
							}

							p ++ ;
						}
					}
				}

				x_display_put_image( x , y + y_off , src , p - src , 0) ;
			}

			return  1 ;

		case  2:
			for( ; y_off < font_height ; y_off++)
			{
				p = ( picture ?
					memcpy( src , (picture += picture_line_len) , size) :
					memset16( src , bg_color->pixel , size / 2)) ;

				for( count = 0 ; count < len ; count++)
				{
					if( ! x_get_bitmap_line( xfont ,
						bitmaps[count] , y_off , bitmap_line))
					{
						p += (font_width * 2) ;
					}
					else
					{
						p += (font->x_off * 2) ;

						for( x_off = 0 ; x_off < glyph_width ; x_off++)
						{
							if( x_get_bitmap_cell( bitmap_line ,
									x_off))
							{
								*((u_int16_t*)p) =
									fg_color->pixel ;
							}

							p += 2 ;
						}
					}
				}

				x_display_put_image( x , y + y_off , src , p - src , 0) ;
			}

			return  1 ;

		/* case  4: */
		default:
			for( ; y_off < font_height ; y_off++)
			{
				p = ( picture ?
					memcpy( src , (picture += picture_line_len) , size) :
					memset32( src , bg_color->pixel , size / 4)) ;

				for( count = 0 ; count < len ; count++)
				{
					if( ! x_get_bitmap_line( xfont ,
						bitmaps[count] , y_off , bitmap_line))
					{
						p += (font_width * 4) ;
					}
					else
					{
						p += (font->x_off * 4) ;

						for( x_off = 0 ; x_off < glyph_width ; x_off++)
						{
							if( x_get_bitmap_cell( bitmap_line ,
									x_off))
							{
								*((u_int32_t*)p) =
									fg_color->pixel ;
							}

							p += 4 ;
						}
					}
				}

				x_display_put_image( x , y + y_off , src , p - src , 0) ;
			}

			return  1 ;
		}
	}
#endif

	orig_x = x ;

	for( ; y_off < font_height ; y_off++)
	{
	#if  defined(USE_FREETYPE)
		int  prev_crowded_out = 0 ;
	#endif

		if( src_bg_is_set)
		{
			if( picture)
			{
				memcpy( src , (picture += picture_line_len) , size) ;
			}
			else
			{
				switch( bpp)
				{
				case  1:
					memset( src , bg_color->pixel , size) ;
					break ;

				case  2:
					memset16( src , bg_color->pixel , size / 2) ;
					break ;

				/* case  4: */
				default:
					memset32( src , bg_color->pixel , size / 4) ;
					break ;
				}
			}
		}

		p = src ;

		for( count = 0 ; count < len ; count++ , x += font_width)
		{
			u_char *  bitmap_line ;
			int  x_off ;

			if( ! x_get_bitmap_line( xfont , bitmaps[count] , y_off , bitmap_line))
			{
				if( src_bg_is_set)
				{
					p += (font_width * bpp) ;
				}
				else
				{
					for( x_off = 0 ; x_off < font_width ; x_off++ , p += bpp)
					{
						copy_pixel( p ,
							x_display_get_pixel( x + x_off ,
								y + y_off) ,
							bpp) ;
					}
				}
			}
		#if  defined(USE_FREETYPE)
			else if( xfont->is_aa)
			{
				if( font->is_proportional)
				{
					/*
					 * src_bg_is_set is always false
					 * (see #ifdef USE_FRAMEBUFFER #ifdef USE_FREETYPE
					 * #endif #endif in xcore_draw_str() in x_draw_str.c)
					 */
					int  retreat ;
					int  advance ;
					int  width ;

					if( ( retreat = bitmaps[count][xfont->glyph_size - 2]) > 0)
					{
						u_int  filled ;

						if( ( filled = (p - src) / bpp) < retreat)
						{
							bitmap_line += (retreat - filled) ;
							x_off = -filled ;
							p = src ;
						}
						else
						{
							x_off = -retreat ;
							p -= (retreat * bpp) ;
						}
					}
					else
					{
						x_off = 0 ;
					}

					width = bitmaps[count][xfont->glyph_size - 1] ;

					/* width - retreat */
					for( ; x_off < width - retreat ; x_off++ , p += bpp)
					{
						u_long  bg ;

						bg = x_display_get_pixel( x + x_off , y + y_off) ;

						if( copy_blended_pixel( win->disp->display ,
							p , &bitmap_line , fg_color->pixel ,
							bg , bpp))
						{
							continue ;
						}
						else if( count == 0 ||
						         x_off >= prev_crowded_out)
						{
							copy_pixel( p , bg , bpp) ;
						}
					}

					advance = bitmaps[count][xfont->glyph_size - 3] ;

					for( ; x_off < advance ; x_off++ , p += bpp)
					{
						if( count == 0 ||
						    x_off >= prev_crowded_out)
						{
							copy_pixel( p ,
								x_display_get_pixel(
									x + x_off , y + y_off) ,
								bpp) ;
						}
					}

					if( prev_crowded_out > advance)
					{
						prev_crowded_out -= advance ;
					}
					else
					{
						prev_crowded_out = 0 ;
					}

					if( advance > 0 && x_off != advance)
					{
						p += ((advance - x_off) * bpp) ;

						if( x_off > advance &&
						    prev_crowded_out < x_off - advance)
						{
							prev_crowded_out = x_off - advance ;
						}
					}

					x = x + advance - font_width ;
				}
				else
				{
					for( x_off = 0 ; x_off < font_width ; x_off++ , p += bpp)
					{
						u_long  pixel ;

						if( font->x_off <= x_off &&
						    x_off < font->x_off +
						            x_get_bitmap_width( font->xfont))
						{
							u_long  bg ;

							if( src_bg_is_set)
							{
								if( picture)
								{
									bg = (bpp == 2) ?
									     *((u_int16_t*)p) :
									     *((u_int32_t*)p) ;
								}
								else
								{
									bg = bg_color->pixel ;
								}
							}
							else
							{
								bg = x_display_get_pixel(
									x + x_off , y + y_off) ;
							}

							if( copy_blended_pixel(
								win->disp->display ,
								p , &bitmap_line ,
								fg_color->pixel ,
								bg , bpp))
							{
								continue ;
							}
						}

						if( ! src_bg_is_set)
						{
							pixel = x_display_get_pixel(
								x + x_off , y + y_off) ;
							copy_pixel( p , pixel , bpp) ;
						}
					}
				}
			}
		#endif
			else
			{
				int  force_fg ;

				force_fg = 0 ;

				for( x_off = 0 ; x_off < font_width ; x_off++ , p += bpp)
				{
					u_long  pixel ;

					if( font->x_off <= x_off &&
					    x_get_bitmap_cell( bitmap_line , x_off - font->x_off))
					{
						pixel = fg_color->pixel ;

						force_fg = font->double_draw_gap ;
					}
					else
					{
						if( force_fg)
						{
							pixel = fg_color->pixel ;
							force_fg = 0 ;
						}
						else if( src_bg_is_set)
						{
							continue ;
						}
						else
						{
							pixel = x_display_get_pixel(
									x + x_off ,
									y + y_off) ;
						}
					}

					copy_pixel( p , pixel , bpp) ;
				}
			}
		}

		x_display_put_image( (x = orig_x) , y + y_off , src , p - src , ! src_bg_is_set) ;
	}

	return  1 ;
}

static int
copy_area(
	x_window_t *  win ,
	Pixmap  src ,
	PixmapMask  mask ,
	int  src_x ,	/* can be minus */
	int  src_y ,	/* can be minus */
	u_int  width ,
	u_int  height ,
	int  dst_x ,	/* can be minus */
	int  dst_y ,	/* can be minus */
	int  accept_margin	/* x/y can be minus and over width/height */
	)
{
	int  hmargin ;
	int  vmargin ;
	int  right_margin ;
	int  bottom_margin ;
	int  y_off ;
	u_int  bpp ;
	u_char *  picture ;
	size_t  src_width_size ;

	if( ! win->is_mapped)
	{
		return  0 ;
	}

	if( accept_margin)
	{
		hmargin = win->hmargin ;
		vmargin = win->vmargin ;
		right_margin = bottom_margin = 0 ;
	}
	else
	{
		hmargin = vmargin = 0 ;
		right_margin = RIGHT_MARGIN(win) ;
		bottom_margin = BOTTOM_MARGIN(win) ;
	}

	if( dst_x >= (int)win->width + hmargin || dst_y >= (int)win->height + vmargin)
	{
		return  0 ;
	}

	if( dst_x + width > win->width + hmargin - right_margin)
	{
		width = win->width + hmargin - right_margin - dst_x ;
	}

	if( dst_y + height > win->height + vmargin - bottom_margin)
	{
		height = win->height + vmargin - bottom_margin - dst_y ;
	}

	bpp = win->disp->display->bytes_per_pixel ;
	src_width_size = src->width * bpp ;
	picture = src->image + src_width_size * (vmargin + src_y) +
			bpp * (hmargin + src_x) ;

	if( mask)
	{
		mask += ((vmargin + src_y) * src->width + hmargin + src_x) ;

		for( y_off = 0 ; y_off < height ; y_off++)
		{
			int  x_off ;
			u_int  w ;

			w = 0 ;
			for( x_off = 0 ; x_off < width ; x_off++)
			{
				if( mask[x_off])
				{
					w ++ ;

					if( x_off + 1 == width)
					{
						/* for x_off - w */
						x_off ++ ;
					}
					else
					{
						continue ;
					}
				}
				else if( w == 0)
				{
					continue ;
				}

				x_display_put_image(
					win->x + win->hmargin + dst_x + x_off - w ,
					win->y + win->vmargin + dst_y + y_off ,
					picture + bpp * ( x_off - w) ,
					w * bpp , 0) ;
				w = 0 ;
			}

			mask += src->width ;
			picture += src_width_size ;
		}
	}
	else
	{
		size_t  size ;

		size = width * bpp ;

		for( y_off = 0 ; y_off < height ; y_off++)
		{
			x_display_put_image(
				win->x + win->hmargin + dst_x ,
				win->y + win->vmargin + dst_y + y_off ,
				picture , size , 0) ;
			picture += src_width_size ;
		}
	}

	return  1 ;
}

static int
clear_margin_area(
	x_window_t *  win
	)
{
	u_int  right_margin ;
	u_int  bottom_margin ;

	right_margin = RIGHT_MARGIN(win) ;
	bottom_margin = BOTTOM_MARGIN(win) ;

	if( win->hmargin | win->vmargin | right_margin | bottom_margin)
	{
		x_window_clear( win , -(win->hmargin) , -(win->vmargin) ,
			win->hmargin , ACTUAL_HEIGHT(win)) ;
		x_window_clear( win , 0 , -(win->vmargin) , win->width , win->vmargin) ;
		x_window_clear( win , win->width - right_margin , -(win->vmargin) ,
			win->hmargin + right_margin , ACTUAL_HEIGHT(win)) ;
		x_window_clear( win , 0 , win->height - bottom_margin ,
			win->width , win->vmargin + bottom_margin) ;
	}

	/* XXX */
	if( win->num_of_children == 2 &&
	    ACTUAL_HEIGHT(win->children[0]) == ACTUAL_HEIGHT(win->children[1]) )
	{
		if( win->children[0]->x + ACTUAL_WIDTH(win->children[0]) <= win->children[1]->x)
		{
			x_window_clear( win ,
				win->children[0]->x + ACTUAL_WIDTH(win->children[0]) , 0 ,
				win->children[1]->x - win->children[0]->x -
					ACTUAL_WIDTH(win->children[0]) ,
				win->height) ;
		}
		else if( win->children[0]->x >=
			win->children[1]->x + ACTUAL_WIDTH(win->children[1]))
		{
			x_window_clear( win ,
				win->children[1]->x + ACTUAL_WIDTH(win->children[1]) , 0 ,
				win->children[0]->x - win->children[1]->x -
					ACTUAL_WIDTH(win->children[1]) ,
				win->height) ;
		}
	}

	return  1 ;
}

static int
fix_rl_boundary(
	x_window_t *  win ,
	int  boundary_start ,
	int *  boundary_end
	)
{
	int  margin ;

	margin = RIGHT_MARGIN(win) ;

	if( boundary_start > win->width - margin)
	{
		return  0 ;
	}

	if( *boundary_end > win->width - margin)
	{
		*boundary_end = win->width - margin ;
	}

	return  1 ;
}

static void
reset_input_focus(
	x_window_t *  win
	)
{
	u_int  count ;

	if( win->inputtable)
	{
		win->inputtable = -1 ;
	}
	else
	{
		win->inputtable = 0 ;
	}

	if( win->is_focused)
	{
		win->is_focused = 0 ;

		if( win->window_unfocused)
		{
			(*win->window_unfocused)( win) ;
		}
	}

	for( count = 0 ; count < win->num_of_children ; count++)
	{
		reset_input_focus( win->children[count]) ;
	}
}

#if  0
static int
check_child_window_area(
	x_window_t *  win
	)
{
	if( win->num_of_children > 0)
	{
		u_int  count ;
		u_int  sum ;

		for( sum = 0 , count = 1 ; count < win->num_of_children ; count++)
		{
			sum += (ACTUAL_WIDTH(win->children[count]) *
			        ACTUAL_HEIGHT(win->children[count])) ;
		}

		if( sum < win->disp->width * win->disp->height * 0.9)
		{
			return  0 ;
		}
	}

	return  1 ;
}
#endif


/* --- global functions --- */

int
x_window_init(
	x_window_t *  win ,
	u_int  width ,
	u_int  height ,
	u_int  min_width ,
	u_int  min_height ,
	u_int  width_inc ,
	u_int  height_inc ,
	u_int  hmargin ,
	u_int  vmargin ,
	int  create_gc ,
	int  inputtable
	)
{
	memset( win , 0 , sizeof( x_window_t)) ;

	/* If wall picture is set, scrollable will be 0. */
	win->is_scrollable = 1 ;

	win->is_focused = 1 ;
	win->inputtable = inputtable ;
	win->is_mapped = 1 ;

	win->create_gc = create_gc ;

	win->width = width ;
	win->height = height ;
	win->min_width = min_width ;
	win->min_height = min_height ;
	win->width_inc = width_inc ;
	win->height_inc = height_inc ;
	win->hmargin = hmargin ;
	win->vmargin = vmargin ;

	win->prev_clicked_button = -1 ;

	win->app_name = "mlterm" ;	/* Can be changed in x_display_show_root(). */

	return	1 ;
}

int
x_window_final(
	x_window_t *  win
	)
{
	u_int  count ;

	for( count = 0 ; count < win->num_of_children ; count ++)
	{
		x_window_final( win->children[count]) ;
	}

	free( win->children) ;

	if( win->window_finalized)
	{
		(*win->window_finalized)( win) ;
	}

	return  1 ;
}

int
x_window_set_type_engine(
	x_window_t *  win ,
	x_type_engine_t  type_engine
	)
{
	return  1 ;
}

int
x_window_add_event_mask(
	x_window_t *  win ,
	long  event_mask
	)
{
	win->event_mask |= event_mask ;

	return  1 ;
}

int
x_window_remove_event_mask(
	x_window_t *  win ,
	long  event_mask
	)
{
	win->event_mask &= ~event_mask ;

	return  1 ;
}

int
x_window_ungrab_pointer(
	x_window_t *  win
	)
{
	return  0 ;
}

int
x_window_set_wall_picture(
	x_window_t *  win ,
	Pixmap  pic ,
	int  do_expose
	)
{
	u_int  count ;
#ifdef  USE_GRF
	int  ret ;

	if( ( ret = x68k_tvram_set_wall_picture( pic->image , pic->width , pic->height)))
	{
		win->wall_picture = DummyPixmap ;	/* dummy */

		/* Don't set is_scrollable = 0. */

		/* If ret == 2, text vram was initialized just now. */
		if( ret == 2)
		{
			clear_margin_area( win) ;

			if( win->window_exposed)
			{
				(*win->window_exposed)( win , 0 , 0 , win->width , win->height) ;
			}
		}

		return  0 ;	/* to free pic memory. */
	}
#endif

	win->wall_picture = pic ;
	win->is_scrollable = 0 ;

	if( do_expose)
	{
		clear_margin_area( win) ;

		if( win->window_exposed)
		{
			(*win->window_exposed)( win , 0 , 0 , win->width , win->height) ;
		}
	#if  0
		else
		{
			x_window_clear_all( win) ;
		}
	#endif
	}

	for( count = 0 ; count < win->num_of_children ; count++)
	{
		x_window_set_wall_picture( win->children[count] , ParentRelative , do_expose) ;
	}

	return  1 ;
}

int
x_window_unset_wall_picture(
	x_window_t *  win ,
	int  do_expose
	)
{
	u_int  count ;

#ifdef  USE_GRF
	x68k_tvram_set_wall_picture( NULL , 0 , 0) ;
#endif

	win->wall_picture = None ;
	win->is_scrollable = 1 ;

	if( do_expose)
	{
		clear_margin_area( win) ;

		if( win->window_exposed)
		{
			(*win->window_exposed)( win , 0 , 0 , win->width , win->height) ;
		}
	#if  0
		else
		{
			x_window_clear_all( win) ;
		}
	#endif
	}

	for( count = 0 ; count < win->num_of_children ; count++)
	{
		x_window_unset_wall_picture( win->children[count] , do_expose) ;
	}

	return  1 ;
}

int
x_window_set_transparent(
	x_window_t *  win ,		/* Transparency is applied to all children recursively */
	x_picture_modifier_ptr_t  pic_mod
	)
{
	return  0 ;
}

int
x_window_unset_transparent(
	x_window_t *  win
	)
{
	return  0 ;
}

int
x_window_set_cursor(
	x_window_t *  win ,
	u_int  cursor_shape
	)
{
	win->cursor_shape = cursor_shape ;

	return  1 ;
}

int
x_window_set_fg_color(
	x_window_t *  win ,
	x_color_t *  fg_color
	)
{
	win->fg_color = *fg_color ;

	return  1 ;
}

int
x_window_set_bg_color(
	x_window_t *  win ,
	x_color_t *  bg_color
	)
{
	win->bg_color = *bg_color ;

	clear_margin_area( win) ;

	return  1 ;
}

int
x_window_add_child(
	x_window_t *  win ,
	x_window_t *  child ,
	int  x ,
	int  y ,
	int  map
	)
{
	void *  p ;

	if( win->parent)
	{
		/* Can't add a grand child window. */
		return  0 ;
	}

	if( ( p = realloc( win->children , sizeof( *win->children) * (win->num_of_children + 1)))
		== NULL)
	{
	#ifdef  DEBUG
		kik_warn_printf( KIK_DEBUG_TAG " realloc failed.\n") ;
	#endif

		return  0 ;
	}

	win->children = p ;

	child->parent = win ;
	child->x = x + win->hmargin ;
	child->y = y + win->vmargin ;
	child->is_focused = child->is_mapped = map ;

	win->children[ win->num_of_children ++] = child ;

	return  1 ;
}

int
x_window_remove_child(
	x_window_t *  win ,
	x_window_t *  child
	)
{
	u_int  count ;

	for( count = 0 ; count < win->num_of_children ; count++)
	{
		if( win->children[count] == child)
		{
			child->parent = NULL ;

			win->children[count] = win->children[--win->num_of_children] ;

			return  1 ;
		}
	}

	return  0 ;
}

x_window_t *
x_get_root_window(
	x_window_t *  win
	)
{
	while( win->parent != NULL)
	{
		win = win->parent ;
	}

	return  win ;
}

GC
x_window_get_fg_gc(
	x_window_t *  win
	)
{
	return  None ;
}

GC
x_window_get_bg_gc(
	x_window_t *  win
	)
{
	return  None ;
}

int
x_window_show(
	x_window_t *  win ,
	int  hint	/* If win->parent(_window) is None,
			   specify XValue|YValue to localte window at win->x/win->y. */
	)
{
	u_int  count ;

	if( win->my_window)
	{
		/* already shown */

		return  0 ;
	}

	if( win->parent)
	{
		win->disp = win->parent->disp ;
		win->parent_window = win->parent->my_window ;
		win->gc = win->parent->gc ;
	}

	win->my_window = win ;	/* Note that x_connect_dialog.c uses this. */

	if( win->parent && ! win->parent->is_transparent &&
	    win->parent->wall_picture)
	{
		x_window_set_wall_picture( win , ParentRelative , 0) ;
	}

	/*
	 * This should be called after Window Manager settings, because
	 * x_set_{window|icon}_name() can be called in win->window_realized().
	 */
	if( win->window_realized)
	{
		int  is_mapped ;

		/*
		 * Don't show anything until x_window_resize_with_margin() is called
		 * at the end of this function.
		 */
		is_mapped = win->is_mapped ;
		win->is_mapped = 0 ;	/* XXX x_window_set_wall_picture() depends on this. */
		(*win->window_realized)( win) ;
		win->is_mapped = is_mapped ;
	}

	/*
	 * showing child windows.
	 */

	for( count = 0 ; count < win->num_of_children ; count ++)
	{
		x_window_show( win->children[count] , 0) ;
	}

	if( ! win->parent && win->x == 0 && win->y == 0)
	{
		x_window_resize_with_margin( win , win->disp->width ,
			win->disp->height , NOTIFY_TO_MYSELF) ;
	}

	return  1 ;
}

int
x_window_map(
	x_window_t *  win
	)
{
	if( ! win->is_mapped)
	{
		win->is_mapped = 1 ;

		(*win->window_exposed)( win ,
			0 , 0 , ACTUAL_WIDTH(win) , ACTUAL_HEIGHT(win)) ;
		clear_margin_area( win) ;
	}

	return  1 ;
}

int
x_window_unmap(
	x_window_t *  win
	)
{
	win->is_mapped = 0 ;

	return  1 ;
}

int
x_window_resize(
	x_window_t *  win ,
	u_int  width ,		/* excluding margin */
	u_int  height ,		/* excluding margin */
	x_resize_flag_t  flag	/* NOTIFY_TO_PARENT , NOTIFY_TO_MYSELF */
	)
{
	if( (flag & NOTIFY_TO_PARENT) &&
	    /* XXX Check if win is input method window or not. */
	    (win->disp->num_of_roots == 1 || win != win->disp->roots[1]))
	{
		if( win->parent)
		{
			win = win->parent ;
		}

		/*
		 * XXX
		 * If Font size, screen_{width|height}_ratio or vertical_mode is changed
		 * and x_window_resize( NOTIFY_TO_PARENT) is called, ignore this call and
		 * resize windows with display size.
		 */
		win->width = 0 ;
		return  x_window_resize_with_margin( win ,
				win->disp->width , win->disp->height ,
				NOTIFY_TO_MYSELF) ;
	}

	if( width + win->hmargin * 2 > win->disp->width)
	{
		width = win->disp->width - win->hmargin * 2 ;
	}

	if( height + win->vmargin * 2 > win->disp->height)
	{
		height = win->disp->height - win->vmargin * 2 ;
	}

	if( win->width == width && win->height == height)
	{
		return  0 ;
	}

	win->width = width ;
	win->height = height ;

	if( flag & NOTIFY_TO_MYSELF)
	{
		if( win->window_resized)
		{
			(*win->window_resized)( win) ;
		}

		/*
		 * clear_margin_area() must be called after win->window_resized
		 * because wall_picture can be resized to fit to the new window
		 * size in win->window_resized.
		 *
		 * Don't clear_margin_area() if flag == 0 because x_window_resize()
		 * is called before x_window_move() in x_im_*_screen.c and could
		 * cause segfault.
		 */
		clear_margin_area( win) ;
	}

	return  1 ;
}

/*
 * !! Notice !!
 * This function is not recommended.
 * Use x_window_resize if at all possible.
 */
int
x_window_resize_with_margin(
	x_window_t *  win ,
	u_int  width ,
	u_int  height ,
	x_resize_flag_t  flag	/* NOTIFY_TO_PARENT , NOTIFY_TO_MYSELF */
	)
{
	return  x_window_resize( win , width - win->hmargin * 2 ,
			height - win->vmargin * 2 , flag) ;
}

int
x_window_set_normal_hints(
	x_window_t *  win ,
	u_int  min_width ,
	u_int  min_height ,
	u_int  width_inc ,
	u_int  height_inc
	)
{
	return  1 ;
}

int
x_window_set_override_redirect(
	x_window_t *  win ,
	int  flag
	)
{
	return  0 ;
}

int
x_window_set_borderless_flag(
	x_window_t *  win ,
	int  flag
	)
{
	return  0 ;
}

int
x_window_move(
	x_window_t *  win ,
	int  x ,
	int  y
	)
{
	if( win->parent)
	{
		x += win->parent->hmargin ;
		y += win->parent->vmargin ;
	}

	if( win->x == x && win->y == y)
	{
		return  0 ;
	}

	win->x = x ;
	win->y = y ;

	if( /* ! check_child_window_area( x_get_root_window( win)) || */
	    win->x + ACTUAL_WIDTH(win) > win->disp->width ||
	    win->y + ACTUAL_HEIGHT(win) > win->disp->height)
	{
		/*
		 * XXX Hack
		 * (Expect the caller to call x_window_resize() immediately after this.)
		 */
		return  1 ;
	}

	/*
	 * XXX
	 * Check if win is input method window or not, because x_window_move()
	 * can fall into the following infinite loop on framebuffer.
	 * 1) x_im_stat_screen::draw_screen() ->
	 *    x_window_move() ->
	 *    x_im_stat_screen::window_exposed() ->
	 *    x_im_stat_screen::draw_screen()
	 * 2) x_im_candidate_screen::draw_screen() ->
	 *    x_im_candidate_screen::resize() ->
	 *    x_window_move() ->
	 *    x_im_candidate_screen::window_exposed() ->
	 *    x_im_candidate_screen::draw_screen()
	 */
	if( ( win->disp->num_of_roots == 1 || win != win->disp->roots[1]))
	{
		clear_margin_area( win) ;

		if( win->window_exposed)
		{
			(*win->window_exposed)( win , 0 , 0 , win->width , win->height) ;
		}
	#if  0
		else
		{
			x_window_clear_all( win) ;
		}
	#endif

		/* XXX */
		if( win->parent)
		{
			clear_margin_area( win->parent) ;
		}
	}

	return  1 ;
}

int
x_window_clear(
	x_window_t *  win ,
	int  x ,
	int  y ,
	u_int  width ,
	u_int  height
	)
{
#ifdef  USE_GRF
	if( x68k_tvram_is_enabled())
	{
		return  x_window_fill_with( win , &black , x , y , width , height) ;
	}
	else
#endif
	if( ! win->wall_picture)
	{
		return  x_window_fill_with( win , &win->bg_color , x , y , width , height) ;
	}
	else
	{
		Pixmap  pic ;
		int  src_x ;
		int  src_y ;

		if( win->wall_picture == ParentRelative)
		{
			src_x = x + win->x ;
			src_y = y + win->y ;
			pic = win->parent->wall_picture ;
		}
		else
		{
			pic = win->wall_picture ;
			src_x = x ;
			src_y = y ;
		}

		return  copy_area( win , pic , None , src_x , src_y ,
				width , height , x , y , 1) ;
	}

	return  1 ;
}

int
x_window_clear_all(
	x_window_t *  win
	)
{
	return  x_window_clear( win , 0 , 0 , win->width , win->height) ;
}

int
x_window_fill(
	x_window_t *  win ,
	int  x ,
	int  y ,
	u_int	width ,
	u_int	height
	)
{
	return  x_window_fill_with( win , &win->fg_color , x , y , width , height) ;
}

int
x_window_fill_with(
	x_window_t *  win ,
	x_color_t *  color ,
	int  x ,
	int  y ,
	u_int	width ,
	u_int	height
	)
{
	u_char *  src ;
	size_t  size ;
	int  y_off ;
	u_int  bpp ;

	if( ! win->is_mapped)
	{
		return  0 ;
	}

	x += (win->x + win->hmargin) ;
	y += (win->y + win->vmargin) ;

	if( ( bpp = win->disp->display->bytes_per_pixel) == 1)
	{
		x_display_fill_with( x , y , width , height , (u_int8_t)color->pixel) ;
	}
	else
	{
		if( ! ( src = alloca( ( size = width * bpp))))
		{
			return  0 ;
		}

		for( y_off = 0 ; y_off < height ; y_off++)
		{
			u_char *  p ;
			int  x_off ;

			p = src ;

			for( x_off = 0 ; x_off < width ; x_off++)
			{
				if( bpp == 2)
				{
					*((u_int16_t*)p) = color->pixel ;
				}
				else /* if( bpp == 4) */
				{
					*((u_int32_t*)p) = color->pixel ;
				}

				p += bpp ;
			}

			x_display_put_image( x , y + y_off , src , size , 0) ;
		}
	}

	return  1 ;
}

int
x_window_blank(
	x_window_t *  win
	)
{
	return  x_window_fill_with( win , &win->fg_color , 0 , 0 ,
			win->width - RIGHT_MARGIN(win) ,
			win->height - BOTTOM_MARGIN(win)) ;
}

int
x_window_update(
	x_window_t *  win ,
	int  flag
	)
{
	if( ! win->is_mapped)
	{
		return  0 ;
	}

	if( win->update_window)
	{
		(*win->update_window)( win, flag) ;
	}

	return  1 ;
}

int
x_window_update_all(
	x_window_t *  win
	)
{
	u_int  count ;

	if( ! win->is_mapped)
	{
		return  0 ;
	}

	if( ! win->parent)
	{
		x_display_reset_cmap() ;
	}

	clear_margin_area( win) ;

	if( win->window_exposed)
	{
		(*win->window_exposed)( win , 0 , 0 , win->width , win->height) ;
	}

	for( count = 0 ; count < win->num_of_children ; count ++)
	{
		x_window_update_all( win->children[count]) ;
	}

	return  1 ;
}

void
x_window_idling(
	x_window_t *  win
	)
{
	u_int  count ;

	for( count = 0 ; count < win->num_of_children ; count ++)
	{
		x_window_idling( win->children[count]) ;
	}

#ifdef  __DEBUG
	if( win->button_is_pressing)
	{
		kik_debug_printf( KIK_DEBUG_TAG " button is pressing...\n") ;
	}
#endif

	if( win->button_is_pressing && win->button_press_continued)
	{
		(*win->button_press_continued)( win , &win->prev_button_press_event) ;
	}
	else if( win->idling)
	{
		(*win->idling)( win) ;
	}
}

/*
 * Return value: 0 => different window.
 *               1 => finished processing.
 */
int
x_window_receive_event(
	x_window_t *   win ,
	XEvent *  event
	)
{
#if  0
	u_int  count ;

	for( count = 0 ; count < win->num_of_children ; count ++)
	{
		if( x_window_receive_event( win->children[count] , event))
		{
			return  1 ;
		}
	}
#endif

	if( event->type == KeyPress)
	{
		if( win->key_pressed)
		{
			(*win->key_pressed)( win , &event->xkey) ;
		}
	}
	else if( event->type == MotionNotify)
	{
		if( win->button_is_pressing)
		{
			if( win->button_motion)
			{
				event->xmotion.x -= win->hmargin ;
				event->xmotion.y -= win->vmargin ;

				(*win->button_motion)( win , &event->xmotion) ;
			}

			/* following button motion ... */

			win->prev_button_press_event.x = event->xmotion.x ;
			win->prev_button_press_event.y = event->xmotion.y ;
			win->prev_button_press_event.time = event->xmotion.time ;
		}
		else if( (win->event_mask & PointerMotionMask) && win->pointer_motion)
		{
			event->xmotion.x -= win->hmargin ;
			event->xmotion.y -= win->vmargin ;

			(*win->pointer_motion)( win , &event->xmotion) ;
		}
	}
	else if( event->type == ButtonRelease)
	{
		if( win->button_released)
		{
			event->xbutton.x -= win->hmargin ;
			event->xbutton.y -= win->vmargin ;

			(*win->button_released)( win , &event->xbutton) ;
		}

		win->button_is_pressing = 0 ;
	}
	else if( event->type == ButtonPress)
	{
		if( win->button_pressed)
		{
			event->xbutton.x -= win->hmargin ;
			event->xbutton.y -= win->vmargin ;

			if( win->click_num == MAX_CLICK)
			{
				win->click_num = 0 ;
			}

			if( win->prev_clicked_time + click_interval >= event->xbutton.time &&
				event->xbutton.button == win->prev_clicked_button)
			{
				win->click_num ++ ;
				win->prev_clicked_time = event->xbutton.time ;
			}
			else
			{
				win->click_num = 1 ;
				win->prev_clicked_time = event->xbutton.time ;
				win->prev_clicked_button = event->xbutton.button ;
			}

			(*win->button_pressed)( win , &event->xbutton , win->click_num) ;
		}

		if( event->xbutton.button <= Button3)
		{
			/* button_is_pressing flag is on except wheel mouse (Button4/Button5). */
			win->button_is_pressing = 1 ;
			win->prev_button_press_event = event->xbutton ;
		}

		if( ! win->is_focused && win->inputtable && event->xbutton.button == Button1 &&
		    ! event->xbutton.state)
		{
			x_window_set_input_focus( win) ;
		}
	}

	return  1 ;
}

size_t
x_window_get_str(
	x_window_t *  win ,
	u_char *  seq ,
	size_t  seq_len ,
	mkf_parser_t **  parser ,
	KeySym *  keysym ,
	XKeyEvent *  event
	)
{
	u_char  ch ;

	if( seq_len == 0)
	{
		return  0 ;
	}

	*parser = NULL ;

	ch = event->ksym ;

#ifdef  __ANDROID__
	if( ch == 0)
	{
		return  x_display_get_str( seq , seq_len) ;
	}
#endif

	if( ( *keysym = event->ksym) >= 0x100)
	{
		switch( *keysym)
		{
		case  XK_KP_Multiply:
			ch = '*' ;
			break ;
		case  XK_KP_Add:
			ch = '+' ;
			break ;
		case  XK_KP_Separator:
			ch = ',' ;
			break ;
		case  XK_KP_Subtract:
			ch = '-' ;
			break ;
		case  XK_KP_Divide:
			ch = '/' ;
			break ;

		default:
			if( win->disp->display->lock_state & NLKED)
			{
				switch( *keysym)
				{
				case  XK_KP_Insert:
					ch = '0' ;
					break ;
				case  XK_KP_End:
					ch = '1' ;
					break ;
				case  XK_KP_Down:
					ch = '2' ;
					break ;
				case  XK_KP_Next:
					ch = '3' ;
					break ;
				case  XK_KP_Left:
					ch = '4' ;
					break ;
				case  XK_KP_Begin:
					ch = '5' ;
					break ;
				case  XK_KP_Right:
					ch = '6' ;
					break ;
				case  XK_KP_Home:
					ch = '7' ;
					break ;
				case  XK_KP_Up:
					ch = '8' ;
					break ;
				case  XK_KP_Prior:
					ch = '9' ;
					break ;
				case  XK_KP_Delete:
					ch = '.' ;
					break ;
				default:
					return  0 ;
				}

				*keysym = ch ;
			}
			else
			{
				return  0 ;
			}
		}
	}
	else if( *keysym == XK_Tab && (event->state & ShiftMask))
	{
		*keysym = XK_ISO_Left_Tab ;

		return  0 ;
	}

	/*
	 * Control + '@'(0x40) ... '_'(0x5f) -> 0x00 ... 0x1f
	 *
	 * Not "<= '_'" but "<= 'z'" because Control + 'a' is not
	 * distinguished from Control + 'A'.
	 */
	if( (event->state & ControlMask) &&
	    (ch == ' ' || ('@' <= ch && ch <= 'z')) )
	{
		seq[0] = (ch & 0x1f) ;
	}
	else
	{
		seq[0] = ch ;
	}

	return  1 ;
}

/*
 * Scroll functions.
 * The caller side should clear the scrolled area.
 */

int
x_window_scroll_upward(
	x_window_t *  win ,
	u_int  height
	)
{
	return  x_window_scroll_upward_region( win , 0 , win->height , height) ;
}

int
x_window_is_scrollable(
	x_window_t *  win
	)
{
	/* XXX If input method module is activated, don't scroll window. */
	if( win->is_scrollable &&
	    (win->disp->num_of_roots == 1 || ! win->disp->roots[1]->is_mapped) )
	{
		return  1 ;
	}
	else
	{
		return  0 ;
	}
}

int
x_window_scroll_upward_region(
	x_window_t *  win ,
	int  boundary_start ,
	int  boundary_end ,
	u_int  height
	)
{
	if( boundary_start < 0 || boundary_end > win->height ||
	    boundary_end <= boundary_start + height)
	{
	#ifdef  DEBUG
		kik_warn_printf( KIK_DEBUG_TAG
			" boundary start %d end %d height %d in window((h) %d (w) %d)\n" ,
			boundary_start , boundary_end , height , win->height , win->width) ;
	#endif

		return  0 ;
	}

	return  scroll_region( win ,
			0 , boundary_start + height ,	/* src */
			win->width , boundary_end - boundary_start - height ,	/* size */
			0 , boundary_start) ;		/* dst */
}

int
x_window_scroll_downward(
	x_window_t *  win ,
	u_int  height
	)
{
	return  x_window_scroll_downward_region( win , 0 , win->height , height) ;
}

int
x_window_scroll_downward_region(
	x_window_t *  win ,
	int  boundary_start ,
	int  boundary_end ,
	u_int  height
	)
{
	if( boundary_start < 0 || boundary_end > win->height ||
	    boundary_end <= boundary_start + height)
	{
	#ifdef  DEBUG
		kik_warn_printf( KIK_DEBUG_TAG " boundary start %d end %d height %d\n" ,
			boundary_start , boundary_end , height) ;
	#endif

		return  0 ;
	}

	return  scroll_region( win ,
			0 , boundary_start ,
			win->width , boundary_end - boundary_start - height ,
			0 , boundary_start + height) ;
}

int
x_window_scroll_leftward(
	x_window_t *  win ,
	u_int  width
	)
{
	return  x_window_scroll_leftward_region( win , 0 , win->width , width) ;
}

int
x_window_scroll_leftward_region(
	x_window_t *  win ,
	int  boundary_start ,
	int  boundary_end ,
	u_int  width
	)
{
	if( boundary_start < 0 || boundary_end > win->width ||
	    boundary_end <= boundary_start + width ||
	    ! fix_rl_boundary( win , boundary_start , &boundary_end))
	{
	#ifdef  DEBUG
		kik_warn_printf( KIK_DEBUG_TAG
			" boundary start %d end %d width %d in window((h) %d (w) %d)\n" ,
			boundary_start , boundary_end , width , win->height , win->width) ;
	#endif

		return  0 ;
	}

	scroll_region( win ,
		boundary_start + width , 0 ,	/* src */
		boundary_end - boundary_start - width , win->height ,	/* size */
		boundary_start , 0) ;		/* dst */

	return  1 ;
}

int
x_window_scroll_rightward(
	x_window_t *  win ,
	u_int  width
	)
{
	return  x_window_scroll_rightward_region( win , 0 , win->width , width) ;
}

int
x_window_scroll_rightward_region(
	x_window_t *  win ,
	int  boundary_start ,
	int  boundary_end ,
	u_int  width
	)
{
	if( boundary_start < 0 || boundary_end > win->width ||
	    boundary_end <= boundary_start + width ||
	    ! fix_rl_boundary( win , boundary_start , &boundary_end))
	{
	#ifdef  DEBUG
		kik_warn_printf( KIK_DEBUG_TAG " boundary start %d end %d width %d\n" ,
			boundary_start , boundary_end , width) ;
	#endif

		return  0 ;
	}

	scroll_region( win ,
		boundary_start , 0 ,
		boundary_end - boundary_start - width , win->height ,
		boundary_start + width , 0) ;

	return  1 ;
}

int
x_window_copy_area(
	x_window_t *  win ,
	Pixmap  src ,
	PixmapMask  mask ,
	int  src_x ,	/* >= 0 */
	int  src_y ,	/* >= 0 */
	u_int  width ,
	u_int  height ,
	int  dst_x ,	/* >= 0 */
	int  dst_y	/* >= 0 */
	)
{
	return  copy_area( win , src , mask , src_x , src_y , width , height , dst_x , dst_y , 0) ;
}

void
x_window_set_clip(
	x_window_t *  win ,
	int  x ,
	int  y ,
	u_int  width ,
	u_int  height
	)
{
	win->clip_y = y ;
	win->clip_height = height ;
}

void
x_window_unset_clip(
	x_window_t *  win
	)
{
	win->clip_y = win->clip_height = 0 ;
}

int
x_window_draw_decsp_string(
	x_window_t *  win ,
	x_font_t *  font ,
	x_color_t *  fg_color ,
	int  x ,
	int  y ,
	u_char *  str ,
	u_int  len
	)
{
	return  x_window_draw_string( win , font , fg_color , x , y , str , len) ;
}

int
x_window_draw_decsp_image_string(
	x_window_t *  win ,
	x_font_t *  font ,
	x_color_t *  fg_color ,
	x_color_t *  bg_color ,	/* If NULL is specified, use wall_picture for bg */
	int  x ,
	int  y ,
	u_char *  str ,
	u_int  len
	)
{
	return  x_window_draw_image_string( win , font , fg_color , bg_color ,
			x , y , str , len) ;
}

int
x_window_draw_string(
	x_window_t *  win ,
	x_font_t *  font ,
	x_color_t *  fg_color ,
	int  x ,
	int  y ,
	u_char *  str ,
	u_int  len
	)
{
	return  draw_string( win , font , fg_color , NULL , x , y , str , len , 1 , 0) ;
}

int
x_window_draw_string16(
	x_window_t *  win ,
	x_font_t *  font ,
	x_color_t *  fg_color ,
	int  x ,
	int  y ,
	XChar2b *  str ,
	u_int  len
	)
{
	return  draw_string( win , font , fg_color , NULL , x , y , str , len , 2 , 0) ;
}

int
x_window_draw_image_string(
	x_window_t *  win ,
	x_font_t *  font ,
	x_color_t *  fg_color ,
	x_color_t *  bg_color ,	/* If NULL is specified, use wall_picture for bg */
	int  x ,
	int  y ,
	u_char *  str ,
	u_int  len
	)
{
#ifdef  USE_GRF
	if( bg_color == NULL && x68k_tvram_is_enabled())
	{
		bg_color = &black ;
	}
#endif

	return  draw_string( win , font , fg_color , bg_color , x , y , str , len , 1 ,
			bg_color == NULL) ;
}

int
x_window_draw_image_string16(
	x_window_t *  win ,
	x_font_t *  font ,
	x_color_t *  fg_color ,
	x_color_t *  bg_color ,	/* If NULL is specified, use wall_picture for bg */
	int  x ,
	int  y ,
	XChar2b *  str ,
	u_int  len
	)
{
#ifdef  USE_GRF
	if( bg_color == NULL && x68k_tvram_is_enabled())
	{
		bg_color = &black ;
	}
#endif

	return  draw_string( win , font , fg_color , bg_color , x , y , str , len , 2 ,
			bg_color == NULL) ;
}

int
x_window_draw_rect_frame(
	x_window_t *  win ,
	int  x1 ,
	int  y1 ,
	int  x2 ,
	int  y2
	)
{
	x_window_fill_with( win , &win->fg_color , x1 , y1 , x2 - x1 + 1 , 1) ;
	x_window_fill_with( win , &win->fg_color , x1 , y1 , 1 , y2 - y1 + 1) ;
	x_window_fill_with( win , &win->fg_color , x1 , y2 , x2 - x1 + 1 , 1) ;
	x_window_fill_with( win , &win->fg_color , x2 , y1 , 1 , y2 - y1 + 1) ;

	return  1 ;
}

int
x_set_use_clipboard_selection(
	int  use_it
	)
{
	return  0 ;
}

int
x_is_using_clipboard_selection(void)
{
	return  0 ;
}

int
x_window_set_selection_owner(
	x_window_t *  win ,
	Time  time
	)
{
#ifdef  __ANDROID__
	if( win->utf_selection_requested)
	{
		(*win->utf_selection_requested)( win , NULL , 0) ;
	}
#else
	win->is_sel_owner = 1 ;
#endif

	return  1 ;
}

int
x_window_xct_selection_request(
	x_window_t *  win ,
	Time  time
	)
{
#ifdef  __ANDROID__
	x_display_request_text_selection() ;
#endif

	return  1 ;
}

int
x_window_utf_selection_request(
	x_window_t *  win ,
	Time  time
	)
{
#ifdef  __ANDROID__
	x_display_request_text_selection() ;
#endif

	return  1 ;
}

int
x_window_send_picture_selection(
	x_window_t *  win ,
	Pixmap  pixmap ,
	u_int  width ,
	u_int  height
	)
{
	return  0 ;
}

int
x_window_send_text_selection(
	x_window_t *  win ,
	XSelectionRequestEvent *  req_ev ,
	u_char *  sel_data ,
	size_t  sel_len ,
	Atom  sel_type
	)
{
#ifdef  __ANDROID__
	x_display_send_text_selection( sel_data , sel_len) ;
#endif

	return  1 ;
}

int
x_set_window_name(
	x_window_t *  win ,
	u_char *  name
	)
{
	return  1 ;
}

int
x_set_icon_name(
	x_window_t *  win ,
	u_char *  name
	)
{
	return  1 ;
}

int
x_window_set_icon(
	x_window_t *  win ,
	x_icon_picture_ptr_t  icon
	)
{
	return 1 ;
}

int
x_window_remove_icon(
	x_window_t *  win
	)
{
	return  1 ;
}

int
x_window_reset_group(
	x_window_t *  win
	)
{
	return  1 ;
}

int
x_window_get_visible_geometry(
	x_window_t *  win ,
	int *  x ,		/* x relative to root window */
	int *  y ,		/* y relative to root window */
	int *  my_x ,		/* x relative to my window */
	int *  my_y ,		/* y relative to my window */
	u_int *  width ,
	u_int *  height
	)
{
	return  1 ;
}

int
x_set_click_interval(
	int  interval
	)
{
	click_interval = interval ;

	return  1 ;
}

u_int
x_window_get_mod_ignore_mask(
	x_window_t *  win ,
	KeySym *  keysyms
	)
{
	return  ~0 ;
}

u_int
x_window_get_mod_meta_mask(
	x_window_t *  win ,
	char *  mod_key
	)
{
	return  ModMask ;
}

int
x_window_bell(
	x_window_t *  win ,
	x_bel_mode_t  mode
	)
{
	if( mode & BEL_VISUAL)
	{
		x_window_blank( win) ;
		kik_usleep( 100000) ;	/* 100 mili sec */
		(*win->window_exposed)( win , 0 , 0 , win->width , win->height) ;
	}

	return  1 ;
}

int
x_window_translate_coordinates(
	x_window_t *  win ,
	int  x ,
	int  y ,
	int *  global_x ,
	int *  global_y ,
	Window *  child
	)
{
	*global_x = x + win->x ;
	*global_y = y + win->y ;

	return  0 ;
}

void
x_window_set_input_focus(
	x_window_t *  win
	)
{
	reset_input_focus( x_get_root_window( win)) ;
	win->inputtable = win->is_focused = 1 ;
	if( win->window_focused)
	{
		(*win->window_focused)( win) ;
	}
}


/* for x_display.c */

int
x_window_clear_margin_area(
	x_window_t *  win
	)
{
	return  clear_margin_area( win) ;
}
