/*
 *	$Id$
 */

#ifndef  __ML_LOGICAL_VISUAL_H__
#define  __ML_LOGICAL_VISUAL_H__


#include  "ml_image.h"
#include  "ml_iscii.h"


typedef struct  ml_logical_visual
{
	ml_image_t *  image ;

	int  (*delete)( struct ml_logical_visual *) ;
	int  (*change_image)( struct ml_logical_visual * , ml_image_t *) ;

	/*
	 * !! Notice !!
	 * ml_image_t should not be modified between render/viaul <=> logical.
	 */
	int  (*render)( struct ml_logical_visual *) ;
	int  (*visual)( struct ml_logical_visual *) ;
	int  (*logical)( struct ml_logical_visual *) ;

	int  (*visual_line)( struct ml_logical_visual * , ml_image_line_t *  line) ;
	
} ml_logical_visual_t ;


ml_logical_visual_t *  ml_logvis_bidi_new( ml_image_t *  image) ;

ml_logical_visual_t *  ml_logvis_iscii_new( ml_image_t *  image , ml_iscii_state_t  iscii_state) ;


#endif
