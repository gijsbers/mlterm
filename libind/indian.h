/* indian.h - This is the header file used for incorporating Indian Script 
 * support in X Windows Applications.
 * Copyright (C) 1999, Naoshad A. Mehta, Rudrava Roy
 */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <sys/types.h>

/* Structure to hold key maps */
struct a2i_tabl {
	char *ascii;
	char *iscii;
};

/* Structure to hold font map */
struct tabl
{
	char *iscii;
	char *font;
};

/* Function prototypes */

char *binsearch(struct tabl *, int, char *);
char *illdefault(struct tabl *, char *, int );
int iscii2font(struct tabl *, char *, char *, int);
char *ins2iscii(struct a2i_tabl *, char *, int);
char *iitk2iscii(struct a2i_tabl *, char *, char *, int);