/*
 * Dumb terminal output routine.
 * Does no cursor addressing stuff.


  ****** Heavily modified. Modifications not noted consistently.  -JES ******
 */
#include <windows.h>
#include <stdarg.h>
#include "wls.h"
#include "lifesrc.h"

/*
 * Print a status message, like printf.
 * The string length is limited to 256 characters.
 */
void
ttystatus(char * fmt)
{
	va_list ap;
	static char	buf[256];

	va_start(ap, fmt);
	vsprintf(buf, fmt, ap);
	va_end(ap);
	wlsMessage(buf,0);
}


/* END CODE */
