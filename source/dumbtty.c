/*
 * Dumb terminal output routine.
 * Does no cursor addressing stuff.
 */

#include <signal.h>
#include <stdarg.h>
#include "lifesrc.h"


static	BOOL	inputready;		/* TRUE if input now ready */

static	void	gotinput PROTO((void));


/*
 * Open the terminal and enable for detecting terminal input.
 * Returns TRUE if successful.
 */
BOOL
ttyopen()
{
	signal(SIGINT, gotinput);

	return TRUE;
}


static void
gotinput()
{
	signal(SIGINT, gotinput);
	inputready = TRUE;
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
	BOOL	result;

	result = inputready;
	inputready = FALSE;

	return result;
}


/*
 * Print a formatted string to the terminal.
 * The string length is limited to 256 characters.
 */
void
ttyprintf(fmt)
	char *	fmt;
{
	va_list ap;
	static char	buf[256];

	va_start(ap, fmt);
	vsprintf(buf, fmt, ap);
	va_end(ap);
	ttywrite(buf, strlen(buf));
}


/*
 * Print a status message, like printf.
 * The string length is limited to 256 characters.
 */
void
ttystatus(fmt)
	char *	fmt;
{
	va_list ap;
	static char	buf[256];

	va_start(ap, fmt);
	vsprintf(buf, fmt, ap);
	va_end(ap);
	ttywrite(buf, strlen(buf));
}


/*
 * Write the specified number of characters to the terminal.
 */
void
ttywrite(buf, count)
	char *	buf;			/* buffer to write */
	int	count;			/* number of characters */
{
	int	ch;

	while (count-- > 0)
	{
		ch = *buf++;
		putchar(ch);
	}
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
	fflush(stdout);
}


/*
 * Return a NULL terminated input line (without the final newline).
 * The specified string is printed as a prompt.
 * Returns TRUE on successful read, or FALSE (with an empty buffer)
 * on end of file or error.
 */
BOOL
ttyread(prompt, buf, buflen)
	char *	prompt;
	char *	buf;
	int	buflen;
{
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

	return TRUE;
}

/* END CODE */
