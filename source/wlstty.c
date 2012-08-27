/*
 * Dumb terminal output routine.
 * Does no cursor addressing stuff.


  ****** Heavily modified. Modifications not noted consistently.  -JES ******
 */
#include "wls-config.h"
#define _CRT_SECURE_NO_WARNINGS
#include <windows.h>
#include <tchar.h>
//#include <signal.h>
#include <stdarg.h>
#include "wls.h"
#include "lifesrc.h"
#include <strsafe.h>


static	BOOL	inputready;		/* TRUE if input now ready */

static	void	gotinput PROTO((void));


/*
 * Open the terminal and enable for detecting terminal input.
 * Returns TRUE if successful.
 */
BOOL
ttyopen()
{
//	signal(SIGINT, gotinput);

	return TRUE;
}


static void
gotinput()
{
//	signal(SIGINT, gotinput);
//	inputready = TRUE;
}


/*
 * Close the terminal.
 */
void
ttyclose()
{
}


/*
 * Test to see if a keyboard character is ready.
 * Returns nonzero if so (and clears the ready flag).
 */
BOOL
ttycheck()
{
//	BOOL	result;
//
//	result = inputready;
//	inputready = FALSE;
//
//	return result;
	return FALSE;
}


/*
 * Print a formatted string to the terminal.
 * The string length is limited to 256 characters.
 */
void
ttyprintf(TCHAR * fmt, ...)
{
	va_list ap;
	TCHAR buf[256];

	va_start(ap, fmt);
	StringCbVPrintf(buf, sizeof(buf), fmt, ap);
		//vsprintf(buf, fmt, ap);
	va_end(ap);
//	ttywrite(buf, strlen(buf));
	wlsMessage(buf,0);
}


/*
 * Print a status message, like printf.
 * The string length is limited to 256 characters.
 */
void
ttystatus(TCHAR * fmt, ...)
{
	va_list ap;
	TCHAR buf[256];

	va_start(ap, fmt);
	StringCbVPrintf(buf, sizeof(buf), fmt, ap);
	va_end(ap);
//	ttywrite(buf, strlen(buf));
	wlsMessage(buf,0);
}


/*
 * Write the specified number of characters to the terminal.
 */
void
ttywrite(char *	buf, int count)
{
//	int	ch;
//
//	while (count-- > 0)
//	{
//		ch = *buf++;
//		putchar(ch);
//	}
}


void
ttyhome()
{
}


void
ttyeeop()
{
}


void
ttyflush()
{
//	fflush(stdout);
}


/*
 * Return a NULL terminated input line (without the final newline).
 * The specified string is printed as a prompt.
 * Returns TRUE on successful read, or FALSE (with an empty buffer)
 * on end of file or error.
 */
BOOL
ttyread(char *prompt, char *buf, int buflen)
{
	/*
	int	len;

	fputs(prompt, stdout);
	fflush(stdout);

	if (fgets(buf, buflen, stdin) == NULL)
	{
		buf[0] = '\0';

		return FALSE;
	}

	len = strlen(buf) - 1;

	if ((len >= 0) && (buf[len] == '\n'))
		buf[len] = '\0';
	*/
	return TRUE;
}

/* END CODE */
