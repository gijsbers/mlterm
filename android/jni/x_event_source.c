/*
 *	$Id$
 */

#include  "x_event_source.h"

#include  <kiklib/kik_debug.h>
#include  <ml_term_manager.h>

#include  "x_display.h"


/* --- global functions --- */

int
x_event_source_init(void)
{
	return  1 ;
}

int
x_event_source_final(void)
{
	return  1 ;
}

int
x_event_source_process(void)
{
	int  ident ;
	int  events ;
	struct android_poll_source *  source ;
	u_int  count ;
	ml_term_t **  terms ;
	u_int  num_of_terms ;
	static int  prev_num_of_terms ;
	static int *  fds ;

	if( ( num_of_terms = ml_get_all_terms( &terms)) == 0 &&
	    prev_num_of_terms > 0)
	{
		x_display_close_all() ;
	}

	if( prev_num_of_terms != num_of_terms)
	{
		ALooper *  looper ;
		void *  p ;

		looper = ALooper_forThread() ;

		for( count = 0 ; count < prev_num_of_terms ; count++)
		{
			ALooper_removeFd( looper , fds[count]) ;
		}

		if( num_of_terms == 0 || ! ( p = realloc( fds , sizeof(int) * num_of_terms)))
		{
			prev_num_of_terms = 0 ;
		}
		else
		{
			fds = p ;
			prev_num_of_terms = num_of_terms ;

			for( count = 0 ; count < num_of_terms ; count++)
			{
				fds[count] = ml_term_get_master_fd( terms[count]) ;
				ALooper_addFd( looper , fds[count] , 1000 + fds[count] ,
					ALOOPER_EVENT_INPUT , NULL , NULL) ;
			}
		}
	}

	/* Read all pending events. */
	if( ( ident = ALooper_pollAll(
				-1 /* block forever waiting for events */ ,
				NULL , &events, (void**)&source)) >= 0)
	{
		if( ! x_display_process_event( source , ident))
		{
			return  0 ;
		}

		for( count = 0 ; count < num_of_terms ; count ++)
		{
			if( ml_term_get_master_fd( terms[count]) + 1000 == ident)
			{
				ml_term_parse_vt100_sequence( terms[count]) ;
			}
		}

		x_display_unlock() ;
	}

	ml_close_dead_terms() ;

	return  1 ;
}

/*
 * fd >= 0  -> Normal file descriptor. handler is invoked if fd is ready.
 * fd < 0 -> Special ID. handler is invoked at interval of 0.1 sec.
 */
int
x_event_source_add_fd(
	int  fd ,
	void  (*handler)(void)
	)
{
	return  0 ;
}

int
x_event_source_remove_fd(
	int  fd
	)
{
	return  0 ;
}

