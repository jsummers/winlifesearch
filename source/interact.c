/*
 * Life search program - user interactions module.
 * Author: David I. Bell.

  ****** Heavily modified. Modifications not noted consistently.  -JES ******
 */

#include "wls-config.h"
#define _CRT_SECURE_NO_WARNINGS
#include <windows.h>
#include <tchar.h>
#include "lifesrc.h"
#include "wls.h"
#include <strsafe.h>


#define	VERSION	"3.5w"

extern struct globals_struct g;

/*
 * Local data.
 */
static	BOOL	nowait;		/* don't wait for commands after loading */
static	BOOL	setall;		/* set all cells from initial file */
static	BOOL	islife;		/* whether the rules are for standard Life */
static	long	foundcount;	/* number of objects found */
static	char *	initfile;	/* file containing initial cells */
static	char *	loadfile;	/* file to load state from */
/*
 * Local procedures
 */
static	long	getnum (char **, int);


/*
 * Table of addresses of parameters which are loaded and saved.
 * Changing this table may invalidate old dump files, unless new
 * parameters are added at the end and default to zero.
 * When changed incompatibly, the dump file version should be incremented.
 * The table is ended with a NULL pointer.
 */
static	int *	param_table[] =
{
	&g.curstatus,
	&g.rowmax, &g.colmax, &g.genmax, &g.rowtrans, &g.coltrans,
	&g.rowsym, &g.colsym, &g.pointsym, &g.fwdsym, &g.bwdsym,
	&g.fliprows, &g.flipcols, &g.flipquads,
	&g.parent, &g.allobjects, &g.nearcols, &g.maxcount,
	&g.userow, &g.usecol, &g.colcells, &g.colwidth, &g.follow,
	&g.orderwide, &g.ordergens, &g.ordermiddle, &g.followgens,

	&g.diagsort, &g.symmetry, &g.trans_rotate, &g.trans_flip, &g.trans_x, &g.trans_y,
	&g.knightsort,
	NULL
};


/*
 * Exclude all cells within the previous light cone centered at the
 * specified cell from searching.
 */
void
excludecone(int row, int col, int gen)
{
	int	tgen;
	int	trow;
	int	tcol;
	int	dist;

	for (tgen = g.genmax; tgen >= gen; tgen--)
	{
		dist = tgen - gen;

		for (trow = row - dist; trow <= row + dist; trow++)
		{
			for (tcol = col - dist; tcol <= col + dist; tcol++)
			{
				findcell(trow, tcol, tgen)->choose = FALSE;
			}
		}
	}
}


/*
 * Freeze all generations of the specified cell.
 * A frozen cell can be ON or OFF, but must be the same in all generations.
 * This routine marks them as frozen, and also inserts all the cells of
 * the generation into the same loop so that they will be forced
 * to have the same state.
 */
STATUS
freezecell(int row, int col)
{
	int	gen;
	CELL *	cell0;
	CELL *	cell;
	STATUS ret;

	cell0 = findcell(row, col, 0);

	for (gen = 0; gen < g.genmax; gen++)
	{
		cell = findcell(row, col, gen);

		cell->frozen = TRUE;

		ret = loopcells(cell0, cell);
		if(ret!=OK) return ret;
	}
	return OK;
}

/*
 * Backup the search to the nth latest free choice.
 * Notice: This skips examinination of some of the possibilities, thus
 * maybe missing a solution.  Therefore this should only be used when it
 * is obvious that the current search state is useless.
 */
void
getbackup(char *cp)
{
	CELL *	cell;
	STATE	state;
	int	count;
	int	blankstoo;

	blankstoo = TRUE;
#if 0
	blankstoo = FALSE;		/* this doesn't work */

	if (*cp == 'b')
	{
		blankstoo = TRUE;
		cp++;
	}
#endif
	count = getnum(&cp, 0);

	if ((count <= 0) || *cp)
	{
		ttystatus(_T("Must back up at least one cell\n"));

		return;
	}

	while (count > 0)
	{
		cell = backup();

		if (cell == NULL_CELL)
		{
			printgen(g.curgen);
			ttystatus(_T("Backed up over all possibilities\n"));

			return;
		}

		state = 1 - cell->state;

		if (blankstoo || (state == ON))
			count--;

		cell->state = UNK;

		if (go(cell, state, FALSE) != OK)
		{
			printgen(g.curgen);
			ttystatus(_T("Backed up over all possibilities\n"));

			return;
		}
	}

	printgen(g.curgen);
}

static int getfilename_l(TCHAR *fn)
{
	OPENFILENAME ofn;
	HWND hWnd;

	hWnd=NULL;

	ZeroMemory(&ofn,sizeof(OPENFILENAME));

	ofn.lStructSize=sizeof(OPENFILENAME);
	ofn.hwndOwner=hWnd;
	ofn.lpstrFilter=_T("*.txt\0*.txt\0All files\0*.*\0\0");
	ofn.nFilterIndex=1;
	ofn.lpstrTitle=_T("Load state");
	ofn.lpstrFile=fn;
	ofn.nMaxFile=MAX_PATH;
	ofn.Flags=OFN_FILEMUSTEXIST|OFN_HIDEREADONLY;

	if(GetOpenFileName(&ofn)) {
		return 1;
	}
	StringCchCopy(fn,MAX_PATH,_T(""));
	return 0;
}


static int getfilename_s(TCHAR *fn)
{
	OPENFILENAME ofn;
	HWND hWnd;

	hWnd=NULL;

	ZeroMemory(&ofn,sizeof(OPENFILENAME));

	ofn.lStructSize=sizeof(OPENFILENAME);
	ofn.hwndOwner=hWnd;
	ofn.lpstrFilter=_T("*.txt\0*.txt\0All files\0*.*\0\0");
	ofn.nFilterIndex=1;
	ofn.lpstrTitle=_T("Save state");
	ofn.lpstrDefExt=_T("txt");
	ofn.lpstrFile=fn;
	ofn.nMaxFile=MAX_PATH;
	ofn.Flags=OFN_FILEMUSTEXIST|OFN_HIDEREADONLY;

	if(GetSaveFileName(&ofn)) {
		return 1;
	}
	StringCchCopy(fn,MAX_PATH,_T(""));
	return 0;
}

/*
 * Write the current generation to the specified file.
 * Empty rows and columns are not written.
 * If no file is specified, it is asked for.
 * Filename of "." means write to stdout.
 */
void writegen(TCHAR *file1, BOOL append)
/*	char *	file;		 file name (or NULL) */
/*	BOOL	append;		 TRUE to append instead of create */
{
	FILE *	fp;
	CELL *	cell;
	int	row;
	int	col;
	int	ch;
	int	minrow, maxrow, mincol, maxcol;
	TCHAR buf[80];
	TCHAR file[MAX_PATH];
	static int writecount=0;

	if(!g.saveoutput && !g.outputcols) return;

//	file = getstr(file, "Write object to file: ");
	if(file1) {
		StringCchCopy(file,MAX_PATH,file1);
	}
	else {
		StringCchCopy(file,MAX_PATH,_T(""));
		getfilename_s(file);
	}

	if (*file == '\0')
		return;

	fp = stdout;

	if (_tcscmp(file, _T(".")))
		fp = _tfopen(file, append ? _T("a") : _T("w"));

	if (fp == NULL)
	{
		ttystatus(_T("Cannot create \"%s\"\n"), file);

		return;
	}

	/*
	 * First find the minimum bounds on the object.
	 */
	minrow = g.rowmax;
	mincol = g.colmax;
	maxrow = 1;
	maxcol = 1;

	for (row = 1; row <= g.rowmax; row++)
	{
		for (col = 1; col <= g.colmax; col++)
		{
			cell = findcell(row, col, g.curgen);

			if (cell->state == OFF)
				continue;

			if (row < minrow)
				minrow = row;

			if (row > maxrow)
				maxrow = row;

			if (col < mincol)
				mincol = col;

			if (col > maxcol)
				maxcol = col;
		}
	}

	if (minrow > maxrow)
	{
		minrow = 1;
		maxrow = 1;
		mincol = 1;
		maxcol = 1;
	}

	if (fp == stdout)
		fprintf(fp, "#\n");

	/*
	 * Now write out the bounded area.
	 */
	for (row = minrow; row <= maxrow; row++)
	{
		for (col = mincol; col <= maxcol; col++)
		{
			cell = findcell(row, col, g.curgen);

			switch (cell->state)
			{
				case OFF:	ch = '.'; break;
				case ON:	ch = '*'; break;
				case UNK:	ch =
						(cell->choose ? '?' : 'X');
						break;
				default:
					ttystatus(_T("Bad cell state"));
					fclose(fp);

					return;
			}

			fputc(ch, fp);
		}

		fputc('\n', fp);
	}

	if (append)
		fprintf(fp, "\n");

	if ((fp != stdout) && fclose(fp))
	{
		ttystatus(_T("Error writing \"%s\"\n"), file);

		return;
	}

	writecount++;
	if (fp != stdout) {
		StringCbPrintf(buf,sizeof(buf),_T("\"%s\" written (%d)"),file,writecount);
		wlsStatus(buf);
	}

	g.quitok = TRUE;
}


/*
 * Dump the current state of the search in the specified file.
 * If no file is specified, it is asked for.
 */
void dumpstate(TCHAR *file1)
{
	FILE *	fp;
	CELL **	set;
	CELL *	cell;
	int	row;
	int	col;
	int	gen;
	int **	param;
	int g1;
	int x,y,z;
	TCHAR file[MAX_PATH];
	char buf[80];

	//file = getstr(file, "Dump state to file: ");
	if(file1) {
		StringCchCopy(file,MAX_PATH,file1);
	}
	else {
		StringCchCopy(file,MAX_PATH,_T("dump.txt"));
		getfilename_s(file);
	}

	if (*file == '\0')
		return;

	fp = _tfopen(file, _T("w"));

	if (fp == NULL)
	{
		ttystatus(_T("Cannot create \"%s\"\n"), file);

		return;
	}

	/*
	 * Dump out the version so we can detect incompatible formats.
	 */
	fprintf(fp, "V %d\n", DUMPVERSION);


	/* write out the original configuration */
	fprintf(fp, "%d %d %d\n", g.colmax,g.rowmax,g.genmax);
	for(z=0;z<g.genmax;z++) {
		for(y=0;y<g.rowmax;y++) {
			for(x=0;x<g.colmax;x++) {
				fprintf(fp,"%d ",g.origfield[z][x][y]);
			}
			fprintf(fp,"\n");
		}
	}




	/*
	 * Dump out the life rule if it is not the normal one.
	 */
	if (!islife) {
#ifdef UNICODE
		StringCbPrintfA(buf,sizeof(buf),"%S",g.rulestring);
		fprintf(fp, "R %s\n", buf);
#else
		fprintf(fp, "R %s\n", g.rulestring);
#endif
	}

	/*
	 * Dump out the parameter values.
	 */
	fprintf(fp, "P");

	for (param = param_table; *param; param++)
		fprintf(fp, " %d", **param);

	fprintf(fp, "\n");

	/*
	 * Dump out those cells which have a setting.
	 */
	set = g.settable;

	while (set != g.nextset)
	{
		cell = *set++;

		fprintf(fp, "S %d %d %d %d %d\n", cell->row, cell->col,
			cell->gen, cell->state, cell->free);
	}

	/*
	 * Dump out those cells which are being excluded from the search.
	 */
	for (row = 1; row <= g.rowmax; row++)
		for (col = 1; col < g.colmax; col++)
			for (gen = 0; gen < g.genmax; gen++)
	{
		cell = findcell(row, col, gen);

		if (cell->choose)
			continue;

		fprintf(fp, "X %d %d %d\n", row, col, gen);
	}

	/*
	 * Dump out those cells in generation 0 which are frozen.
	 * It isn't necessary to remember frozen cells in other
	 * generations since they will be copied from generation 0.
	 */
	for (row = 1; row <= g.rowmax; row++)
		for (col = 1; col < g.colmax; col++)
	{
		cell = findcell(row, col, 0);

		if (cell->frozen)
			fprintf(fp, "F %d %d\n", row, col);
	}

	for(g1=0;g1<g.genmax;g1++)
		for(row=0;row<g.rowmax;row++)
			for(col=0;col<g.colmax;col++) {
				fprintf(fp, "O %d %d %d %d\n",g1,row,col,g.origfield[g1][row][col]);
			}


	/*
	 * Finish up with the setting offsets and the final line.
	 */
	fprintf(fp, "T %d %d\n", g.baseset - g.settable, g.nextset - g.settable);
	fprintf(fp, "E\n");

	if (fclose(fp))
	{
		ttystatus(_T("Error writing \"%s\"\n"), file);

		return;
	}

	ttystatus(_T("State dumped to \"%s\"\n"), file);
	g.quitok = TRUE;
}


/*
 * Load a previously dumped state from a file.
 * Warning: Almost no checks are made for validity of the state.
 * Returns OK on success, ERROR1 on failure.
 */
STATUS loadstate(TCHAR *file1)
{
	FILE *	fp;
	char *	cp;
	int	row;
	int	col;
	int	gen;
	STATUS ret;
	STATE	state;
	BOOL	free;
//	BOOL	choose;
	CELL *	cell;
	int **	param;
	char	buf[LINESIZE];
	int g1,val;
	TCHAR file[MAX_PATH];

	int x1,y1,z1,x,y,z;

	//file = getstr(file, "Load state from file: ");
	StringCchCopy(file,MAX_PATH,_T(""));
	if(file1) StringCchCopy(file,MAX_PATH,file1);
	getfilename_l(file);

	//if (*file == '\0')
	//	return OK;
	if(file[0]=='\0') return ERROR1;

	fp = _tfopen(file, _T("r"));

	if (fp == NULL)
	{
		ttystatus(_T("Cannot open state file \"%s\"\n"), file);

		return ERROR1;
	}

	buf[0] = '\0';
	fgets(buf, LINESIZE, fp);

	if (buf[0] != 'V')
	{
		ttystatus(_T("Missing version line in file \"%s\"\n"), file);
		fclose(fp);

		return ERROR1;
	}

	cp = &buf[1];
/*
	if (getnum(&cp, 0) != DUMPVERSION)
	{
		ttystatus("Unknown version in state file \"%s\"\n", file);
		fclose(fp);

		return ERROR1;
	}
*/

/**********************************************  load starting state */
	/* warning no error checking at all! */

	fgets(buf, LINESIZE, fp);  // this line has x,y,gens
	sscanf(buf,"%d %d %d",&x1,&y1,&z1);
	for(z=0;z<z1;z++) {
		for(y=0;y<y1;y++) {
			fgets(buf, LINESIZE, fp);
			cp=strtok(buf," ");
			for(x=0;x<x1;x++) {
				if(cp) {
					g.origfield[z][x][y]=atoi(cp);
					cp=strtok(NULL," ");
				}
				else g.origfield[z][x][y]=4;  // error
			}
		}
	}




/***********************************************/


	fgets(buf, LINESIZE, fp);

	/*
	 * Set the life rules if they were specified.
	 * This line is optional.
	 */
	if (buf[0] == 'R')
	{
		cp = &buf[strlen(buf) - 1];

		if (*cp == '\n')
			*cp = '\0';

		cp = &buf[1];

		while (isblank(*cp))
			cp++;

		if (!setrulesA(cp))
		{
			ttystatus(_T("Bad Life rules in state file\n"));
			fclose(fp);

			return ERROR1;
		}

		fgets(buf, LINESIZE, fp);
	}

	/*
	 * Load up all of the parameters from the parameter line.
	 * If parameters are missing at the end, they are defaulted to zero.
	 */
	if (buf[0] != 'P')
	{
		ttystatus(_T("Missing parameter line in state file\n"));
		fclose(fp);

		return ERROR1;
	}

	cp = &buf[1];

	for (param = param_table; *param; param++)
		**param = getnum(&cp, 0);

	/*
	 * Initialize the cells.
	 */
	if(OK != initcells()) {
		fclose(fp);
		return ERROR1;
	}

	/*
	 * Handle cells which have been set.
	 */
	g.newset = g.settable;

	for (;;)
	{
		buf[0] = '\0';
		fgets(buf, LINESIZE, fp);

		if (buf[0] != 'S')
			break;

		cp = &buf[1];
		row = getnum(&cp, 0);
		col = getnum(&cp, 0);
		gen = getnum(&cp, 0);
		state = (STATE)getnum(&cp, 0);
		free = getnum(&cp, 0);

		cell = findcell(row, col, gen);

		if (setcell(cell, state, free) != OK)
		{
			ttystatus(
				_T("Inconsistently setting cell at r%d c%d g%d \n"),
				row, col, gen);

			fclose(fp);

			return ERROR1;
		}
	}

	/*
	 * Handle non-choosing cells.
	 */
	while (buf[0] == 'X')
	{
		cp = &buf[1];
		row = getnum(&cp, 0);
		col = getnum(&cp, 0);
		gen = getnum(&cp, 0);

		findcell(row, col, gen)->choose = FALSE;

		buf[0] = '\0';
		fgets(buf, LINESIZE, fp);
	}

	/*
	 * Handle frozen cells.
	 */
	while (buf[0] == 'F')
	{
		cp = &buf[1];
		row = getnum(&cp, 0);
		col = getnum(&cp, 0);

		ret = freezecell(row, col);
		if(ret!=OK) {
			fclose(fp);
			return ERROR1;
		}

		buf[0] = '\0';
		fgets(buf, LINESIZE, fp);
	}

	while(buf[0]=='O') {
		cp=&buf[1];
		g1=getnum(&cp,0);
		row = getnum(&cp, 0);
		col = getnum(&cp, 0);
		val=getnum(&cp,0);
		g.origfield[g1][row][col]=val;

		buf[0] = '\0';
		fgets(buf, LINESIZE, fp);
	}



	if (buf[0] != 'T')
	{
		ttystatus(_T("Missing table line in state file\n"));
		fclose(fp);

		return ERROR1;
	}

	cp = &buf[1];
	g.baseset = &g.settable[getnum(&cp, 0)];
	g.nextset = &g.settable[getnum(&cp, 0)];

	fgets(buf, LINESIZE, fp);

	if (buf[0] != 'E')
	{
		ttystatus(_T("Missing end of file line in state file\n"));
		fclose(fp);

		return ERROR1;
	}

	if (fclose(fp))
	{
		ttystatus(_T("Error reading \"%s\"\n"), file);

		return ERROR1;
	}

	ttystatus(_T("State loaded from \"%s\"\n"), file);
	g.quitok = TRUE;

	return OK;
}


#if 0
/*
 * Read a file containing initial settings for either gen 0 or the last gen.
 * If setall is TRUE, both the ON and the OFF cells will be set.
 * Returns OK on success, ERROR1 on error.
 */
static STATUS
readfile(TCHAR *file)
{
	FILE *	fp;
	char *	cp;
	char	ch;
	int	row;
	int	col;
	int	gen;
	STATE	state;
	char	buf[LINESIZE];

	file = getstr(file, "Read initial object from file: ");

	if (*file == '\0')
		return OK;

	fp = _tfopen(file, _T("r"));

	if (fp == NULL)
	{
		ttystatus(_T("Cannot open \"%s\"\n"), file);

		return ERROR1;
	}

	gen = (parent ? (genmax - 1) : 0);
	row = 0;

	while (fgets(buf, LINESIZE, fp))
	{
		row++;
		cp = buf;
		col = 0;

		while (*cp && (*cp != '\n'))
		{
			col++;
			ch = *cp++;

			switch (ch)
			{
				case '?':
					continue;

				case 'x':
				case 'X':
					excludecone(row, col, gen);
					continue;

				case '+':
					freezecell(row, col);
					continue;

				case '.':
				case ' ':
					if (!setall)
						continue;

					state = OFF;
					break;

				case 'O':
				case 'o':
				case '*':
					state = ON;
					break;

				default:
					ttystatus(_T("Bad file format in line %d\n"),
						row);
					fclose(fp);

					return ERROR1;
			}

			if (proceed(findcell(row, col, gen), state, FALSE)
				!= OK)
			{
				ttystatus(_T("Inconsistent state for cell %d %d\n"),
					row, col);
				fclose(fp);

				return ERROR1;
			}
		}
	}

	if (fclose(fp))
	{
		ttystatus(_T("Error reading \"%s\"\n"), file);

		return ERROR1;
	}

	return OK;
}
#endif

#if 0
/*
 * Check a string for being NULL, and if so, ask the user to specify a
 * value for it.  Returned string may be static and thus is overwritten
 * for each call.  Leading spaces in the string are skipped over.
 */
static char *
getstr(char *str, char *prompt)
//	char *	str;		/* string to check for NULLness */
//	char *	prompt;		/* message to prompt with */
{
	static char	buf[LINESIZE];

	if ((str == NULL) || (*str == '\0'))
	{
		if (!ttyread(prompt, buf, LINESIZE))
		{
			buf[0] = '\0';

			return buf;
		}

		str = buf;
	}

	while (isblank(*str))
		str++;

	return str;
}
#endif


/*
 * Read a number from a string, eating any leading or trailing blanks.
 * Returns the value, and indirectly updates the string pointer.
 * Returns specified default if no number was found.
 */
static long
getnum(char **cpp, int defnum)
{
	char *	cp;
	long	num;
	BOOL	isneg;

	isneg = FALSE;
	cp = *cpp;

	while (isblank(*cp))
		cp++;

	if (*cp == '-')
	{
		cp++;
		isneg = TRUE;
	}

	if (!isdigit(*cp))
	{
		*cpp = cp;

		return defnum;
	}

	num = 0;

	while (isdigit(*cp))
		num = num * 10 + (*cp++ - '0');

	if (isneg)
		num = -num;

	while (isblank(*cp))
		cp++;

	*cpp = cp;

	return num;
}


/*
 * Parse a string and set the Life rules from it.
 * Returns TRUE on success, or FALSE on an error.
 * The rules can be "mmm,nnn",  "mmm/nnn", "Bmmm,Snnn", "Bmmm/Snnn",
 * or a hex number in the Wolfram encoding.
 */
BOOL
setrules(TCHAR *cp)
{
	int		i;
	unsigned int	bits;

	for (i = 0; i < 9; i++)
	{
		g.bornrules[i] = OFF;
		g.liverules[i] = OFF;
	}

	if (*cp == '\0')
		return FALSE;

	/*
	 * See if the string contains a comma or a slash.
	 * If not, then assume Wolfram's hex format.
	 */
	if ((_tcschr(cp, ',') == NULL) && (_tcschr(cp, '/') == NULL))
	{
		bits = 0;

		for (; *cp; cp++)
		{
			bits <<= 4;

			if ((*cp >= '0') && (*cp <= '9'))
				bits += *cp - '0';
			else if ((*cp >= 'a') && (*cp <= 'f'))
				bits += *cp - 'a' + 10;
			else if ((*cp >= 'A') && (*cp <= 'F'))
				bits += *cp - 'A' + 10;
			else
				return FALSE;
		}

		if (i & ~0x3ff)
			return FALSE;

		for (i = 0; i < 9; i++)
		{
			if (bits & 0x01)
				g.bornrules[i] = ON;

			if (bits & 0x02)
				g.liverules[i] = ON;

			bits >>= 2;
		}
	}
	else
	{
		/*
		 * It is in normal born/survive format.
		 */
		if ((*cp == 'b') || (*cp == 'B'))
			cp++;

		while ((*cp >= '0') && (*cp <= '8'))
			g.bornrules[*cp++ - '0'] = ON;

		if ((*cp != ',') && (*cp != '/'))
			return FALSE;

		cp++;

		if ((*cp == 's') || (*cp == 'S'))
			cp++;

		while ((*cp >= '0') && (*cp <= '8'))
			g.liverules[*cp++ - '0'] = ON;

		if (*cp)
			return FALSE;
	}

	/*
	 * Construct the rule string for printouts and see if this
	 * is the normal Life rule.
	 */
	cp = g.rulestring;

	*cp++ = 'B';

	for (i = 0; i < 9; i++)
	{
		if (g.bornrules[i] == ON)
			*cp++ = '0' + i;
	}

	*cp++ = '/';
	*cp++ = 'S';

	for (i = 0; i < 9; i++)
	{
		if (g.liverules[i] == ON)
			*cp++ = '0' + i;
	}

	*cp = '\0';

	islife = (_tcscmp(g.rulestring, _T("B3/S23")) == 0);

	return TRUE;
}

BOOL setrulesA(char *rulestringA)
{
#ifdef UNICODE
	WCHAR rulestringW[20];
	StringCbPrintfW(rulestringW,sizeof(rulestringW),_T("%S"),rulestringA);
	return setrules(rulestringW);
#else
	return setrules(rulestringA);
#endif
}


/*
 * Print a status message, like printf.
 * The string length is limited to 256 characters.
 */
void ttystatus(TCHAR * fmt, ...)
{
	va_list ap;
	TCHAR buf[256];

	va_start(ap, fmt);
	StringCbVPrintf(buf, sizeof(buf), fmt, ap);
	va_end(ap);
	wlsMessage(buf,0);
}


/* END CODE */
