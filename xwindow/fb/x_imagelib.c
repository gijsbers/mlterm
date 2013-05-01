/*
 *	$Id$
 */

#ifndef  NO_IMAGE

#include  "../x_imagelib.h"

#include  <stdio.h>	/* sprintf */
#include  <unistd.h>	/* write , STDIN_FILENO */
#ifdef  DLOPEN_LIBM
#include <kiklib/kik_dlfcn.h>   /* dynamically loading pow */
#else
#include <math.h>               /* pow */
#endif
#include  <kiklib/kik_debug.h>

#include  "x_display.h"		/* x_get_closest_color */


/* Trailing "/" is appended in value_table_refresh(). */
#ifndef  LIBMDIR
#define  LIBMDIR  "/lib"
#endif


/* --- static functions --- */

static void
value_table_refresh(
	u_char *  value_table ,		/* 256 bytes */
	x_picture_modifier_t *  mod
	)
{
	int i , tmp ;
	double real_gamma , real_brightness , real_contrast ;
	static double (*pow_func)( double , double) ;

	real_gamma = (double)(mod->gamma) / 100 ;
	real_contrast = (double)(mod->contrast) / 100 ;
	real_brightness = (double)(mod->brightness) / 100 ;

	if( ! pow_func)
	{
	#ifdef  DLOPEN_LIBM
		kik_dl_handle_t  handle ;

		if( ( ! ( handle = kik_dl_open( LIBMDIR "/" , "m")) &&
		      ! ( handle = kik_dl_open( "" , "m"))) ||
		    ! ( pow_func = kik_dl_func_symbol( handle , "pow")))
		{
		#ifdef  DEBUG
			kik_debug_printf( KIK_DEBUG_TAG " Failed to load pow in libm.so\n") ;
		#endif

			if( handle)
			{
				kik_dl_close( handle) ;
			}

			/*
			 * gamma, contrast and brightness options are ignored.
			 * (alpha option still survives.)
			 */
			for( i = 0 ; i < 256 ; i++)
			{
				value_table[i] = i ;
			}

			return ;
		}
	#else  /* DLOPEN_LIBM */
		pow_func = pow ;
	#endif /* DLOPEN_LIBM */
	}

	for( i = 0 ; i < 256 ; i++)
	{
		tmp = real_contrast * (255 * (*pow_func)(((double)i + 0.5)/ 255, real_gamma) -128)
			+ 128 * real_brightness ;
		if( tmp >= 255)
		{
			break;
		}
		else if( tmp < 0)
		{
			value_table[i] = 0 ;
		}
		else
		{
			value_table[i] = tmp ;
		}
	}

	for( ; i < 256 ; i++)
	{
		value_table[i] = 255 ;
	}
}

static void
modify_pixmap(
	Display *  display ,
	Pixmap  pixmap ,
	x_picture_modifier_t *  pic_mod ,
	u_int  depth
	)
{
	u_char *  value_table ;
	u_int  y ;
	u_int  x ;
	u_char  r , g , b , a ;
	u_int32_t  pixel ;

	if( ! x_picture_modifier_is_normal( pic_mod) &&
	    ( value_table = alloca( 256)))
	{
		value_table_refresh( value_table , pic_mod) ;
	}
	else if( display->bytes_per_pixel == 4)
	{
		return ;
	}
	else
	{
		value_table = NULL ;
	}

	for( y = 0 ; y < pixmap->height ; y++)
	{
		for( x = 0 ; x < pixmap->width ; x++)
		{
			pixel = *(((u_int32_t*)pixmap->image) + (y * pixmap->width + x)) ;

			a = (pixel >> 24) & 0xff ;
			r = (pixel >> 16) & 0xff ;
			g = (pixel >> 8) & 0xff ;
			b = pixel & 0xff ;

			if( value_table)
			{
				r = (value_table[r] * (255 - pic_mod->alpha) +
					pic_mod->blend_red * pic_mod->alpha) / 255 ;
				g = (value_table[g] * (255 - pic_mod->alpha) +
					pic_mod->blend_green * pic_mod->alpha) / 255 ;
				b = (value_table[b] * (255 - pic_mod->alpha) +
					pic_mod->blend_blue * pic_mod->alpha) / 255 ;
			}

			if( display->bytes_per_pixel == 1)
			{
				*(pixmap->image + y * pixmap->width + x) =
					x_get_closest_color( display->cmap , r , g , b) ;
			}
			else
			{
				pixel = RGB_TO_PIXEL(r,g,b,display->rgbinfo) |
					(depth == 32 ? (a << 24) : 0) ;

				if( display->bytes_per_pixel == 2)
				{
					*(((u_int16_t*)pixmap->image) + (y * pixmap->width + x)) =
						pixel ;
				}
				else /* if( display->bytes_per_pixel == 4) */
				{
					*(((u_int32_t*)pixmap->image) + (y * pixmap->width + x)) =
						pixel ;
				}
			}
		}
	}

	if( display->bytes_per_pixel < 4)
	{
		void *  p ;

		if( ( p = realloc( pixmap->image ,
				pixmap->width * pixmap->height * display->bytes_per_pixel)))
		{
			pixmap->image = p ;
		}
	}
}

#define  BUILTIN_IMAGELIB
#define  SIXEL_1BPP
#include  <string.h>	/* memset/memmove */
#include  "../../common/c_imagelib.c"

static int
load_sixel_1bpp_from_file(
	Display *  display ,
	char *  path ,
	u_int  width ,
	u_int  height ,
	x_picture_modifier_t *  pic_mod ,
	u_int  depth ,
	Pixmap *  pixmap ,
	PixmapMask *  mask
	)
{
	int  x ;
	int  y ;
	u_char *  src ;
	u_char *  dst ;

	if( display->pixels_per_byte != 8 || ! strstr( path , ".six") || pic_mod ||
	    ! ( *pixmap = calloc( 1 , sizeof(**pixmap))))
	{
		return  0 ;
	}

	if( ! ( src = (*pixmap)->image = load_sixel_from_file( path ,
						&(*pixmap)->width , &(*pixmap)->height)))
	{
		free( *pixmap) ;

		return  0 ;
	}

	if( ( width != 0 && width != (*pixmap)->width) ||
	    /* (*pixmap)->height is always multiple of 6. */
	    ( height != 0 && (height + 5) / 6 != (*pixmap)->height / 6))
	{
	#ifdef  DEBUG
		kik_debug_printf( KIK_DEBUG_TAG
			" Picture size (%dx%d) doesn't match specified size (%dx%d)\n" ,
			(*pixmap)->width , (*pixmap)->height , width , height) ;
	#endif

		free( (*pixmap)->image) ;
		free( *pixmap) ;

		return  0 ;
	}

	if( mask && ( dst = *mask = calloc( 1 , (*pixmap)->width * (*pixmap)->height)))
	{
		int  has_tp ;

		has_tp = 0 ;

		for( y = 0 ; y < (*pixmap)->height ; y++)
		{
			for( x = 0 ; x < (*pixmap)->width ; x++)
			{
				if( *src >= 0x80)
				{
					*dst = 1 ;
					/* clear opaque mark */
					*src &= 0x7f ;
				}
				else
				{
					has_tp = 1 ;
				}

				src ++ ;
				dst ++ ;
			}
		}

		if( ! has_tp)
		{
			free( *mask) ;
			*mask = None ;
		}
	}
	else
	{
		for( y = 0 ; y < (*pixmap)->height ; y++)
		{
			for( x = 0 ; x < (*pixmap)->width ; x++)
			{
				/* clear opaque mark */
				*(src ++) &= 0x7f ;
			}
		}
	}

	return  1 ;
}

static int
load_file(
	Display *  display ,
	char *  path ,
	u_int  width ,
	u_int  height ,
	x_picture_modifier_t *  pic_mod ,
	u_int  depth ,
	Pixmap *  pixmap ,
	PixmapMask *  mask
	)
{
	pid_t  pid ;
	int  fds1[2] ;
	int  fds2[2] ;
	ssize_t  size ;
	u_int32_t  tmp ;

	if( ! path || ! *path)
	{
		return  0 ;
	}

	if( load_sixel_1bpp_from_file( display , path , width , height ,
		pic_mod , depth , pixmap , mask))
	{
		return  1 ;
	}

	if( pipe( fds1) == -1)
	{
		return  0 ;
	}
	if( pipe( fds2) == -1)
	{
		close( fds1[0]) ;
		close( fds1[1]) ;

		return  0 ;
	}

	pid = fork() ;
	if( pid == -1)
	{
		close( fds1[0]) ;
		close( fds1[1]) ;
		close( fds2[0]) ;
		close( fds2[0]) ;

		return  0 ;
	}

	if( pid == 0)
	{
		/* child process */

		char *  args[7] ;
		char  width_str[DIGIT_STR_LEN(u_int) + 1] ;
		char  height_str[DIGIT_STR_LEN(u_int) + 1] ;

		args[0] = LIBEXECDIR "/mlterm/mlimgloader" ;
		args[1] = "0" ;
		sprintf( width_str , "%u" , width) ;
		args[2] = width_str ;
		sprintf( height_str , "%u" , height) ;
		args[3] = height_str ;
		args[4] = path ;
		args[5] = "-c" ;
		args[6] = NULL ;

		close( fds1[1]) ;
		close( fds2[0]) ;
		if( dup2( fds1[0] , STDIN_FILENO) != -1 && dup2( fds2[1] , STDOUT_FILENO) != -1)
		{
			execv( args[0] , args) ;
		}

		kik_msg_printf( "Failed to exec %s.\n" , args[0]) ;

		exit(1) ;
	}

	close( fds1[0]) ;
	close( fds2[1]) ;

	if( ! ( *pixmap = calloc( 1 , sizeof(**pixmap))))
	{
		goto  error ;
	}

	if( read( fds2[0] , &tmp , sizeof(u_int32_t)) != sizeof(u_int32_t))
	{
		goto  error ;
	}

	size = ((*pixmap)->width = tmp) * sizeof(u_int32_t) ;

	if( read( fds2[0] , &tmp , sizeof(u_int32_t)) != sizeof(u_int32_t))
	{
		goto  error ;
	}

	size *= ((*pixmap)->height = tmp) ;

	if( ! ( (*pixmap)->image = malloc( size)))
	{
		goto  error ;
	}
	else
	{
		u_char *  p ;
		ssize_t  n_rd ;

		p = (*pixmap)->image ;
		while( ( n_rd = read( fds2[0] , p , size)) > 0)
		{
			p += n_rd ;
			size -= n_rd ;
		}

		if( size > 0)
		{
			goto  error ;
		}
	}

	close( fds2[0]) ;
	close( fds1[1]) ;

	if( mask)
	{
		u_char *  dst ;

		if( ( dst = *mask = calloc( 1 , (*pixmap)->width * (*pixmap)->height)))
		{
			int  x ;
			int  y ;
			int  has_tp ;
			u_int32_t *  src ;

			has_tp = 0 ;
			src = (u_int32_t*)(*pixmap)->image ;

			for( y = 0 ; y < (*pixmap)->height ; y++)
			{
				for( x = 0 ; x < (*pixmap)->width ; x++)
				{
					if( *(src ++) >= 0x80000000)
					{
						*dst = 1 ;
					}
					else
					{
						has_tp = 1 ;
					}

					dst ++ ;
				}
			}

			if( ! has_tp)
			{
				free( *mask) ;
				*mask = None ;
			}
		}
	}

	modify_pixmap( display , *pixmap , pic_mod , depth) ;

#ifdef  DEBUG
	kik_debug_printf( KIK_DEBUG_TAG " %s(w %d h %d) is loaded%s.\n" ,
			path , (*pixmap)->width , (*pixmap)->height ,
			(mask && *mask) ? " (has mask)" : "") ;
#endif

	return  1 ;

error:
	if( *pixmap)
	{
		free( (*pixmap)->image) ;
		free( *pixmap) ;
	}

	close( fds2[0]) ;
	close( fds1[1]) ;

	return  0 ;
}


/* --- global functions --- */

int
x_imagelib_display_opened(
	Display *  display
	)
{
	return  1 ;
}

int
x_imagelib_display_closed(
	Display *  display
	)
{
	return  1 ;
}

Pixmap
x_imagelib_load_file_for_background(
	x_window_t *  win ,
	char *  path ,
	x_picture_modifier_t *  pic_mod
	)
{
	Pixmap  pixmap ;

	if( load_file( win->disp->display , path ,
			ACTUAL_WIDTH(win) , ACTUAL_HEIGHT(win) , pic_mod , win->disp->depth ,
			&pixmap , NULL))
	{
		return  pixmap ;
	}
	else
	{
		return  None ;
	}
}

int
x_imagelib_root_pixmap_available(
	Display *   display
	)
{
	return  0 ;
}

Pixmap
x_imagelib_get_transparent_background(
	x_window_t *  win ,
	x_picture_modifier_t *  pic_mod
	)
{
	return  None ;
}

int
x_imagelib_load_file(
	x_display_t *  disp ,
	char *  path ,
	u_int32_t **  cardinal,
	Pixmap *  pixmap,
	PixmapMask *  mask,
	u_int *  width,
	u_int *  height
	)
{
	if( cardinal)
	{
		return  0 ;
	}

	if( ! load_file( disp->display , path , *width , *height ,
				NULL , disp->depth , pixmap , mask))
	{
		return  0 ;
	}

	if( *width == 0 || *height == 0)
	{
		*width = (*pixmap)->width ;
		*height = (*pixmap)->height ;
	}

	return  1 ;
}

int
x_delete_image(
	Display *  display ,
	Pixmap  pixmap
	)
{
	free( pixmap->image) ;
	free( pixmap) ;

	return  1 ;
}

int
x_delete_mask(
	Display *  display ,
	PixmapMask  mask	/* can be NULL */
	)
{
	free( mask) ;

	return  1 ;
}

#endif	/* NO_IMAGE */
