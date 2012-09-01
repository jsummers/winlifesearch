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

extern struct globals_struct g;

extern int origfield[GENMAX][COLMAX][ROWMAX];

/*
 * Local data.
 */
extern TCHAR rulestring[20];	/* rule string for printouts */
static	int	foundcount;	/* number of objects found */
static int writecount; /* number of objects written to a file */
extern int saveoutput;
extern int saveoutputallgen;
extern int stoponfound;
extern int origfield[GENMAX][COLMAX][ROWMAX];
static TCHAR filename[MAX_PATH] = {'\0'};

/*
 * Local procedures
 */
static	long	getnum PROTO((char **, int));

/*
 * Table of addresses of parameters which are loaded and saved.
 * Changing this table may invalidate old dump files, unless new
 * parameters are added at the end and default to zero.
 * When changed incompatibly, the dump file version should be incremented.
 * The table is ended with a NULL pointer.
 */
static	int *param_table[] =
{
	&g.curstatus,
	&g.rowmax, &g.colmax, &g.genmax, 
	&g.rowtrans, &g.coltrans,
	&g.rowsym, &g.colsym, &g.pointsym, &g.fwdsym, &g.bwdsym,
	&g.fliprows, &g.flipcols, &g.flipquads,
	&g.parent, &g.allobjects, &g.nearcols, &g.maxcount,
	&g.userow, &g.usecol, &g.colcells, &g.colwidth, &g.follow,
	&g.orderwide, &g.ordergens, &g.ordermiddle, &g.followgens, 
	&g.diagsort, &g.symmetry, &g.trans_rotate, &g.trans_flip, &g.trans_x, &g.trans_y,
	&g.knightsort,
	&g.smart, &g.smartwindow, &g.smartthreshold,
	&foundcount,
	&g.combine, &g.combining, &combinedcells, &setcombinedcells, &differentcombinedcells, 

	NULL
};

/*
 * Freeze all generations of the specified cell.
 * A frozen cell can be ON or OFF, but must be the same in all generations.
 * This routine marks them as frozen, and also inserts all the cells of
 * the generation into the same loop so that they will be forced
 * to have the same state.
 */
void
freezecell(int row, int col)
{
	int	gen;
	CELL *	cell0;
	CELL *	cell;

	cell0 = findcell(row, col, 0);

	for (gen = 0; gen < g.genmax; gen++)
	{
		cell = findcell(row, col, gen);

		cell->frozen = TRUE;

		loopcells(cell0, cell);
	}
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

	count = getnum(&cp, 0);

	if ((count <= 0) || *cp)
	{
		ttystatus(_T("Must back up at least one cell\n"));

		return;
	}

	while (count > 0)
	{
		cell = backup();

		if (cell == NULL)
		{
			printgen();
			ttystatus(_T("Backed up over all possibilities\n"));

			return;
		}

		state = 1 - cell->state;

		if (state == ON)
			count--;

		cell->state = UNK;

		if (!go(cell, state, FALSE))
		{
			printgen();
			ttystatus(_T("Backed up over all possibilities\n"));

			return;
		}
	}

	printgen();
}

static BOOL getfilename_l()
{
	OPENFILENAME ofn;
	HWND hWnd;

	hWnd=NULL;

	ZeroMemory(&ofn,sizeof(OPENFILENAME));

	ofn.lStructSize=sizeof(OPENFILENAME);
	ofn.hwndOwner=hWnd;
	ofn.lpstrFilter=_T("WLS Dump Files (*.wdf)\0*.wdf\0Text Files (*.txt)\0*.txt\0All files\0*.*\0\0");
	ofn.nFilterIndex=1;
	ofn.lpstrTitle=_T("Load state");
	ofn.lpstrFile=filename;
	ofn.nMaxFile=MAX_PATH;
	ofn.Flags=OFN_FILEMUSTEXIST|OFN_HIDEREADONLY;

	if(GetOpenFileName(&ofn)) 
	{
		return TRUE;
	}
	return FALSE;
}


BOOL getfilename_s()
{
	OPENFILENAME ofn;
	HWND hWnd;

	hWnd=NULL;

	ZeroMemory(&ofn,sizeof(OPENFILENAME));

	ofn.lStructSize=sizeof(OPENFILENAME);
	ofn.hwndOwner=hWnd;
	ofn.lpstrFilter=_T("WLS Dump Files (*.wdf)\0*.wdf\0Text Files (*.txt)\0*.txt\0All files\0*.*\0\0");
	ofn.nFilterIndex=1;
	ofn.lpstrTitle=_T("Save state");
	ofn.lpstrDefExt=_T("txt");
	ofn.lpstrFile=filename;
	ofn.nMaxFile=MAX_PATH;
	ofn.Flags=OFN_HIDEREADONLY|OFN_OVERWRITEPROMPT;

	if(GetSaveFileName(&ofn)) 
	{
		return TRUE;
	}
	return FALSE;
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
	CELL * cell;
	int	row;
	int	col;
	int gen;
	int	ch;
	int	minrow, maxrow, mincol, maxcol;
	TCHAR buf[80];
	TCHAR file[MAX_PATH];

	if(!saveoutput && !outputcols) return;

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
	maxrow = -1;
	maxcol = -1;

	for (gen=0; gen < g.genmax; gen++)
	{
		for (row = 1; row <= g.rowmax; row++)
		{
			for (col = 1; col <= g.colmax; col++)
			{
				cell = findcell(row, col, gen);

				if (cell->state != OFF)
				{

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
		}
	}

	if (minrow > maxrow)
	{
		minrow = 0;
		maxrow = 0;
		mincol = 0;
		maxcol = 0;
	}

	if (fp == stdout)
		fprintf(fp, "#\n");

	/*
	 * Now write out the bounded area.
	 */
	for (row = minrow; row <= maxrow; row++)
	{
		for (gen = 0; gen < (saveoutputallgen ? g.genmax : 1); gen++)
		{
			for (col = mincol; col <= maxcol; col++)
			{
				cell = findcell(row, col, gen);
				switch (cell->state)
				{
				case OFF:	
					ch = '.'; 
					break;

				case ON:	
					ch = '*'; 
					break;

				case UNK:	
					ch = cell->unchecked ? 'X' : '?'; 
					break;

				default:
					ttystatus(_T("Bad cell state"));
					fclose(fp);

					return;
				}

				fputc(ch, fp);
			}
			if (saveoutputallgen && (gen < g.genmax - 1)) fputs(" ... ", fp);
		}

		fputc('\n', fp);
	}

	if (append)
	{
		fprintf(fp, ".\n.\n");
	}

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
}


/*
 * Dump the current state of the search in the specified file.
 * If no file is specified, it is asked for.
 */
void dumpstate(TCHAR *file1, BOOL echo)
{
	FILE *	fp;
	CELL **	set;
	CELL *	cell;
	int	row;
	int	col;
	int	gen;
	int **	param;
	char ind;
	TCHAR *file = filename;

	if(file1) {
		file = file1;
	}
	else 
	{
		if (!getfilename_s()) 
		{
			return;
		}
	}

	fp = _tfopen(file, _T("wt"));

	if (fp == NULL)
	{
		ttystatus(_T("Cannot create \"%s\"\n"), file);

		return;
	}

	/*
	 * Dump out the version so we can detect incompatible formats.
	 */
	fprintf(fp, "V %d\n", DUMPVERSION);

	/*
	 * Dump out the parameter values.
	 */
	fprintf(fp, "P");

	for (param = param_table; *param; param++)
		fprintf(fp, " %d", **param);

	fprintf(fp, "\n");

	/*
	 * Dump out the life rule
	 */

	fprintf(fp, "R %s\n", rulestring);

	/* write out the original configuration */

	for(gen=0;gen<g.genmax;gen++) {
		for(row=0;row<g.rowmax;row++) {
			for(col=0;col<g.colmax;col++) {
				fprintf(fp,"%d ",origfield[gen][col][row]);
			}
			fprintf(fp,"\n");
		}
	}

	/*
	 * Dump out those cells which have a setting.
	 */
	set = settable;

	while (set != nextset)
	{
		cell = *set++;

		if (g.combining && (cell->combined != UNK))
		{
			ind = (cell->combined == cell->state) ? '=' : '!';
		}
		else
		{
			ind = '.';
		}

		fprintf(fp, "S %3d %3d %2d %d %d %c\n", cell->row, cell->col,
			cell->gen, (cell->state == ON) ? 1 : 0, cell->free, ind);
	}

	/*
	 * save combination
	 */

	if (g.combining) 
	{
		for(col=1;col<=g.colmax;col++) {
			for(row=1;row<=g.rowmax;row++) {
				for(gen=0;gen<g.genmax;gen++) {
					cell = findcell(row, col, gen);
					if (cell->combined != UNK)
					{
						fprintf(fp, "F %3d %3d %2d %d \n", row, col, gen, (cell->combined == ON) ? 1 : 0);
					}
				}
			}
		}
	}

	/*
	 * end of file marker
	 */

	fprintf(fp, "E\n");

	if (fclose(fp))
	{
		ttystatus(_T("Error writing \"%s\"\n"), file);

		return;
	}

	if (echo) 
	{
		TCHAR buf[1000];
		StringCbPrintf(buf, sizeof(buf), _T("State dumped to \"%s\"\n"), file);
		wlsStatus(buf);
	}
}


/*
 * Load a previously dumped state from a file.
 * Warning: Almost no checks are made for validity of the state.
 * Returns OK on success, ERROR1 on failure.
 */
BOOL loadstate(void)
{
	FILE *	fp;
	char *	cp;
	int	row;
	int	col;
	int	gen;
	STATE	state;
	BOOL	free;
	CELL *	cell;
	int **	param;
	char	buf[LINESIZE];
	int ver;

	STATUS status;

	if (!getfilename_l()) return FALSE;

	fp = _tfopen(filename, _T("r"));

	if (fp == NULL)
	{
		ttystatus(_T("Cannot open state file \"%s\"\n"), filename);

		return FALSE;
	}

//*********************************************
// Read and check the file version
//*********************************************

	buf[0] = '\0';
	fgets(buf, LINESIZE, fp);

	if (buf[0] != 'V')
	{
		ttystatus(_T("Missing version line in file \"%s\"\n"), filename);
		fclose(fp);

		return FALSE;
	}

	cp = &buf[1];
	ver = getnum(&cp, 0);

	if (DUMPVERSION != ver)
	{
		ttystatus(_T("Incorrect version of the dump file: expected %d, found %d"), DUMPVERSION, ver);
		fclose(fp);
		return FALSE;
	}

//*********************************************
// Read parameters
//*********************************************

	fgets(buf, LINESIZE, fp);

	/*
	 * Load up all of the parameters from the parameter line.
	 * If parameters are missing at the end, they are defaulted to zero.
	 */
	if (buf[0] != 'P')
	{
		ttystatus(_T("Missing parameter line in state file\n"));
		fclose(fp);

		return FALSE;
	}

	cp = &buf[1];

	for (param = param_table; *param; param++)
		**param = getnum(&cp, 0);

//*********************************************
// Initialise
//*********************************************

	initcells();

//*********************************************
// Read life rule
//*********************************************

	fgets(buf, LINESIZE, fp);

	/*
	 * Set the life rules if they were specified.
	 * This line is optional.
	 */
	if (buf[0] != 'R')
	{
		ttystatus(_T("Missing rule line in state file\n"));
		fclose(fp);

		return FALSE;
	}
	cp = &buf[strlen(buf) - 1];

	if (*cp == '\n') *cp = '\0';

	cp = &buf[1];

	while (isblank(*cp)) cp++;

	if (!setrulesA(cp))
	{
		ttystatus(_T("Bad Life rules in state file\n"));
		fclose(fp);

		return FALSE;
	}

//*********************************************
// Read the initial state
//*********************************************

	for(gen=0;gen<g.genmax;gen++) {
		for(row=0;row<g.rowmax;row++) {
			fgets(buf, LINESIZE, fp);
			cp=strtok(buf," ");
			for(col=0;col<g.colmax;col++) {
				if(cp) {
					currfield[gen][col][row]=atoi(cp);
					cp=strtok(NULL," ");
				}
				else currfield[gen][col][row]=4;  // error
			}
		}
	}

//*********************************************
// Set the initial state
//*********************************************

	if (!set_initial_cells())
	{
		fclose(fp);
		record_malloc(0,NULL); // free memory
		return FALSE;
	}

//*********************************************
// Set the search order
//*********************************************

	initsearchorder();

//*********************************************
// Process cells in the stack
//*********************************************

	for (;;)
	{
		// read the next line
		buf[0] = '\0';
		fgets(buf, LINESIZE, fp);

		if (buf[0] != 'S')
			break;

		cp = &buf[1];
		row = getnum(&cp, 0);
		col = getnum(&cp, 0);
		gen = getnum(&cp, 0);
		state = (getnum(&cp, 0) == 1) ? ON : OFF;
		free = getnum(&cp, 0);

		cell = findcell(row, col, gen);

		if (!setcell(cell, state, free))
		{
			ttystatus(
				_T("Inconsistently setting cell at r%d c%d g%d \n"),
				row, col, gen);

			fclose(fp);

			return FALSE;
		}

	}

//*********************************************
// Check the consistency
//*********************************************

	do {
		status = examinenext();
	} while (status == OK);

	if (status != CONSISTENT) {
		ttystatus(_T("Inconsistent cell status\n"));
		fclose(fp);

		return FALSE;
	}

//*********************************************
// Read combination
//*********************************************

	for (;;)
	{
		if (buf[0] != 'F')
		{
			break;
		}
		cp = &buf[1];
		row = getnum(&cp, 0);
		col = getnum(&cp, 0);
		gen = getnum(&cp, 0);
		state = (getnum(&cp, 0) == 1) ? ON : OFF;

		cell = findcell(row, col, gen);
		cell->combined = state;

		buf[0] = '\0';
		fgets(buf, LINESIZE, fp);
	
	}

//*********************************************
// Check the presence of the 'end' line
//*********************************************

	if (buf[0] != 'E')
	{
		ttystatus(_T("Missing end of file line in state file\n"));
		fclose(fp);

		return FALSE;
	}

	if (fclose(fp))
	{
		ttystatus(_T("Error reading \"%s\"\n"), filename);

		return FALSE;
	}

	{
		TCHAR buf[1000];
		StringCbPrintf(buf, sizeof(buf), _T("State loaded from \"%s\"\n"), filename);

		wlsStatus(buf);
	}
	return TRUE;
}

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
		bornrules[i] = FALSE;
		liverules[i] = FALSE;
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
				bornrules[i] = TRUE;

			if (bits & 0x02)
				liverules[i] = TRUE;

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
			bornrules[*cp++ - '0'] = TRUE;

		if ((*cp != ',') && (*cp != '/'))
			return FALSE;

		cp++;

		if ((*cp == 's') || (*cp == 'S'))
			cp++;

		while ((*cp >= '0') && (*cp <= '8'))
			liverules[*cp++ - '0'] = TRUE;

		if (*cp)
			return FALSE;
	}

	/*
	 * Construct the rule string for printouts and see if this
	 * is the normal Life rule.
	 */
	cp = rulestring;

	*cp++ = 'B';

	for (i = 0; i < 9; i++)
	{
		if (bornrules[i])
			*cp++ = '0' + i;
	}

	*cp++ = '/';
	*cp++ = 'S';

	for (i = 0; i < 9; i++)
	{
		if (liverules[i])
			*cp++ = '0' + i;
	}

	*cp = '\0';

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
