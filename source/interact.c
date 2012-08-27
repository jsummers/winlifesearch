/*
 * Life search program - user interactions module.
 * Author: David I. Bell.


  ****** Heavily modified. Modifications not noted consistently.  -JES ******
 */
#define _CRT_SECURE_NO_WARNINGS
#include <windows.h>
#include "lifesrc.h"
#include "wls.h"


extern int origfield[GENMAX][COLMAX][ROWMAX];


#define	VERSION	"3.5w"


/*
 * Local data.
 */
static	BOOL	nowait;		/* don't wait for commands after loading */
static	BOOL	setall;		/* set all cells from initial file */
static	BOOL	islife;		/* whether the rules are for standard Life */
extern char	rulestring[20];	/* rule string for printouts */
static	long	foundcount;	/* number of objects found */
static	char *	initfile;	/* file containing initial cells */
static	char *	loadfile;	/* file to load state from */
extern int saveoutput;
extern int origfield[GENMAX][COLMAX][ROWMAX];
/*
 * Local procedures
 */
//static	void	usage PROTO((void));
//static	void	getsetting PROTO((char *));
//static	void	getbackup PROTO((char *));
//static	void	getclear PROTO((char *));
//static	void	getexclude PROTO((char *));
//static	void	getfreeze PROTO((char *));

//static	STATUS	loadstate PROTO((char *));
static	STATUS	readfile PROTO((char *));
static	BOOL	confirm PROTO((char *));
static	long	getnum PROTO((char **, int));
static	char *	getstr PROTO((char *, char *));


/*
 * Table of addresses of parameters which are loaded and saved.
 * Changing this table may invalidate old dump files, unless new
 * parameters are added at the end and default to zero.
 * When changed incompatibly, the dump file version should be incremented.
 * The table is ended with a NULL pointer.
 */
static	int *	param_table[] =
{
	&curstatus,
	&rowmax, &colmax, &genmax, &rowtrans, &coltrans,
	&rowsym, &colsym, &pointsym, &fwdsym, &bwdsym,
	&fliprows, &flipcols, &flipquads,
	&parent, &allobjects, &nearcols, &maxcount,
	&userow, &usecol, &colcells, &colwidth, &follow,
	&orderwide, &ordergens, &ordermiddle, &followgens,

	&diagsort, &symmetry, &trans_rotate, &trans_flip, &trans_x, &trans_y,
	&knightsort,
	NULL
};




/*
 * Exclude all cells within the previous light cone centered at the
 * specified cell from searching.
 */
void
excludecone(row, col, gen)
{
	int	tgen;
	int	trow;
	int	tcol;
	int	dist;

	for (tgen = genmax; tgen >= gen; tgen--)
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
void
freezecell(int row, int col)
{
	int	gen;
	CELL *	cell0;
	CELL *	cell;

	cell0 = findcell(row, col, 0);

	for (gen = 0; gen < genmax; gen++)
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
		ttystatus("Must back up at least one cell\n");

		return;
	}

	while (count > 0)
	{
		cell = backup();

		if (cell == NULL_CELL)
		{
			printgen(curgen);
			ttystatus("Backed up over all possibilities\n");

			return;
		}

		state = 1 - cell->state;

		if (blankstoo || (state == ON))
			count--;

		cell->state = UNK;

		if (go(cell, state, FALSE) != OK)
		{
			printgen(curgen);
			ttystatus("Backed up over all possibilities\n");

			return;
		}
	}

	printgen(curgen);
}

int getfilename_l(char *fn)
{
	OPENFILENAME ofn;
	HWND hWnd;

	hWnd=NULL;

	ZeroMemory(&ofn,sizeof(OPENFILENAME));

	ofn.lStructSize=sizeof(OPENFILENAME);
	ofn.hwndOwner=hWnd;
	ofn.lpstrFilter="*.txt\0*.txt\0All files\0*.*\0\0";
	ofn.nFilterIndex=1;
	ofn.lpstrTitle="Load state";
	ofn.lpstrFile=fn;
	ofn.nMaxFile=MAX_PATH;
	ofn.Flags=OFN_FILEMUSTEXIST|OFN_HIDEREADONLY;

	if(GetOpenFileName(&ofn)) {
		return 1;
	}
	strcpy(fn,"");
	return 0;
}


int getfilename_s(char *fn)
{
	OPENFILENAME ofn;
	HWND hWnd;

	hWnd=NULL;

	ZeroMemory(&ofn,sizeof(OPENFILENAME));

	ofn.lStructSize=sizeof(OPENFILENAME);
	ofn.hwndOwner=hWnd;
	ofn.lpstrFilter="*.txt\0*.txt\0All files\0*.*\0\0";
	ofn.nFilterIndex=1;
	ofn.lpstrTitle="Save state";
	ofn.lpstrDefExt="txt";
	ofn.lpstrFile=fn;
	ofn.nMaxFile=MAX_PATH;
	ofn.Flags=OFN_FILEMUSTEXIST|OFN_HIDEREADONLY;

	if(GetSaveFileName(&ofn)) {
		return 1;
	}
	strcpy(fn,"");
	return 0;
}

/*
 * Write the current generation to the specified file.
 * Empty rows and columns are not written.
 * If no file is specified, it is asked for.
 * Filename of "." means write to stdout.
 */
void writegen(char *file1, BOOL append)
/*	char *	file;		 file name (or NULL) */
/*	BOOL	append;		 TRUE to append instead of create */
{
	FILE *	fp;
	CELL *	cell;
	int	row;
	int	col;
	int	ch;
	int	minrow, maxrow, mincol, maxcol;
	char buf[80];
	char file[MAX_PATH];
	static int writecount=0;

	if(!saveoutput && !outputcols) return;

//	file = getstr(file, "Write object to file: ");
	if(file1) {
		strcpy(file,file1);
	}
	else {
		strcpy(file,"");
		getfilename_s(file);
	}

	if (*file == '\0')
		return;

	fp = stdout;

	if (strcmp(file, "."))
		fp = fopen(file, append ? "a" : "w");

	if (fp == NULL)
	{
		ttystatus("Cannot create \"%s\"\n", file);

		return;
	}

	/*
	 * First find the minimum bounds on the object.
	 */
	minrow = rowmax;
	mincol = colmax;
	maxrow = 1;
	maxcol = 1;

	for (row = 1; row <= rowmax; row++)
	{
		for (col = 1; col <= colmax; col++)
		{
			cell = findcell(row, col, curgen);

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
			cell = findcell(row, col, curgen);

			switch (cell->state)
			{
				case OFF:	ch = '.'; break;
				case ON:	ch = '*'; break;
				case UNK:	ch =
						(cell->choose ? '?' : 'X');
						break;
				default:
					ttystatus("Bad cell state");
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
		ttystatus("Error writing \"%s\"\n", file);

		return;
	}

	writecount++;
	if (fp != stdout) {
		sprintf(buf,"\"%s\" written (%d)",file,writecount);
		wlsStatus(buf);
	}

	quitok = TRUE;
}


/*
 * Dump the current state of the search in the specified file.
 * If no file is specified, it is asked for.
 */
void dumpstate(char *file1)
{
	FILE *	fp;
	CELL **	set;
	CELL *	cell;
	int	row;
	int	col;
	int	gen;
	int **	param;
	int g;
	int x,y,z;
	char file[MAX_PATH];

	//file = getstr(file, "Dump state to file: ");
	if(file1) {
		strcpy(file,file1);
	}
	else {
		strcpy(file,"dump.txt");
		getfilename_s(file);
	}

	if (*file == '\0')
		return;

	fp = fopen(file, "wt");

	if (fp == NULL)
	{
		ttystatus("Cannot create \"%s\"\n", file);

		return;
	}

	/*
	 * Dump out the version so we can detect incompatible formats.
	 */
	fprintf(fp, "V %d\n", DUMPVERSION);


	/* write out the original configuration */
	fprintf(fp, "%d %d %d\n", colmax,rowmax,genmax);
	for(z=0;z<genmax;z++) {
		for(y=0;y<rowmax;y++) {
			for(x=0;x<colmax;x++) {
				fprintf(fp,"%d ",origfield[z][x][y]);
			}
			fprintf(fp,"\n");
		}
	}




	/*
	 * Dump out the life rule if it is not the normal one.
	 */
	if (!islife)
		fprintf(fp, "R %s\n", rulestring);

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
	set = settable;

	while (set != nextset)
	{
		cell = *set++;

		fprintf(fp, "S %d %d %d %d %d\n", cell->row, cell->col,
			cell->gen, cell->state, cell->free);
	}

	/*
	 * Dump out those cells which are being excluded from the search.
	 */
	for (row = 1; row <= rowmax; row++)
		for (col = 1; col < colmax; col++)
			for (gen = 0; gen < genmax; gen++)
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
	for (row = 1; row <= rowmax; row++)
		for (col = 1; col < colmax; col++)
	{
		cell = findcell(row, col, 0);

		if (cell->frozen)
			fprintf(fp, "F %d %d\n", row, col);
	}

	for(g=0;g<genmax;g++)
		for(row=0;row<rowmax;row++)
			for(col=0;col<colmax;col++) {
				fprintf(fp, "O %d %d %d %d\n",g,row,col,origfield[g][row][col]);
			}


	/*
	 * Finish up with the setting offsets and the final line.
	 */
	fprintf(fp, "T %d %d\n", baseset - settable, nextset - settable);
	fprintf(fp, "E\n");

	if (fclose(fp))
	{
		ttystatus("Error writing \"%s\"\n", file);

		return;
	}

	ttystatus("State dumped to \"%s\"\n", file);
	quitok = TRUE;
}


/*
 * Load a previously dumped state from a file.
 * Warning: Almost no checks are made for validity of the state.
 * Returns OK on success, ERROR1 on failure.
 */
STATUS loadstate(char *file1)
{
	FILE *	fp;
	char *	cp;
	int	row;
	int	col;
	int	gen;
	STATE	state;
	BOOL	free;
//	BOOL	choose;
	CELL *	cell;
	int **	param;
	char	buf[LINESIZE];
	int g,val;
	char file[MAX_PATH];

	int x1,y1,z1,x,y,z;

	//file = getstr(file, "Load state from file: ");
	strcpy(file,"");
	if(file1) strcpy(file,file1);
	getfilename_l(file);

	//if (*file == '\0')
	//	return OK;
	if(file[0]=='\0') return ERROR1;

	fp = fopen(file, "r");

	if (fp == NULL)
	{
		ttystatus("Cannot open state file \"%s\"\n", file);

		return ERROR1;
	}

	buf[0] = '\0';
	fgets(buf, LINESIZE, fp);

	if (buf[0] != 'V')
	{
		ttystatus("Missing version line in file \"%s\"\n", file);
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
					origfield[z][x][y]=atoi(cp);
					cp=strtok(NULL," ");
				}
				else origfield[z][x][y]=4;  // error
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

		if (!setrules(cp))
		{
			ttystatus("Bad Life rules in state file\n");
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
		ttystatus("Missing parameter line in state file\n");
		fclose(fp);

		return ERROR1;
	}

	cp = &buf[1];

	for (param = param_table; *param; param++)
		**param = getnum(&cp, 0);

	/*
	 * Initialize the cells.
	 */
	initcells();

	/*
	 * Handle cells which have been set.
	 */
	newset = settable;

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
				"Inconsistently setting cell at r%d c%d g%d \n",
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

		freezecell(row, col);

		buf[0] = '\0';
		fgets(buf, LINESIZE, fp);
	}

	while(buf[0]=='O') {
		cp=&buf[1];
		g=getnum(&cp,0);
		row = getnum(&cp, 0);
		col = getnum(&cp, 0);
		val=getnum(&cp,0);
		origfield[g][row][col]=val;

		buf[0] = '\0';
		fgets(buf, LINESIZE, fp);
	}



	if (buf[0] != 'T')
	{
		ttystatus("Missing table line in state file\n");
		fclose(fp);

		return ERROR1;
	}

	cp = &buf[1];
	baseset = &settable[getnum(&cp, 0)];
	nextset = &settable[getnum(&cp, 0)];

	fgets(buf, LINESIZE, fp);

	if (buf[0] != 'E')
	{
		ttystatus("Missing end of file line in state file\n");
		fclose(fp);

		return ERROR1;
	}

	if (fclose(fp))
	{
		ttystatus("Error reading \"%s\"\n", file);

		return ERROR1;
	}

	ttystatus("State loaded from \"%s\"\n", file);
	quitok = TRUE;

	return OK;
}


/*
 * Read a file containing initial settings for either gen 0 or the last gen.
 * If setall is TRUE, both the ON and the OFF cells will be set.
 * Returns OK on success, ERROR1 on error.
 */
static STATUS
readfile(char *file)
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

	fp = fopen(file, "r");

	if (fp == NULL)
	{
		ttystatus("Cannot open \"%s\"\n", file);

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
					ttystatus("Bad file format in line %d\n",
						row);
					fclose(fp);

					return ERROR1;
			}

			if (proceed(findcell(row, col, gen), state, FALSE)
				!= OK)
			{
				ttystatus("Inconsistent state for cell %d %d\n",
					row, col);
				fclose(fp);

				return ERROR1;
			}
		}
	}

	if (fclose(fp))
	{
		ttystatus("Error reading \"%s\"\n", file);

		return ERROR1;
	}

	return OK;
}


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


/*
 * Confirm an action by prompting with the specified string and reading
 * an answer.  Entering 'y' or 'Y' indicates TRUE, everything else FALSE.
 */
static BOOL
confirm(char *prompt)
{
/*	int	ch;

	ch = *getstr(NULL, prompt);

	if ((ch == 'y') || (ch == 'Y'))
		return TRUE;

	return FALSE;
	*/
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
setrules(cp)
	char *	cp;
{
	int		i;
	unsigned int	bits;

	for (i = 0; i < 9; i++)
	{
		bornrules[i] = OFF;
		liverules[i] = OFF;
	}

	if (*cp == '\0')
		return FALSE;

	/*
	 * See if the string contains a comma or a slash.
	 * If not, then assume Wolfram's hex format.
	 */
	if ((strchr(cp, ',') == NULL) && (strchr(cp, '/') == NULL))
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
				bornrules[i] = ON;

			if (bits & 0x02)
				liverules[i] = ON;

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
			bornrules[*cp++ - '0'] = ON;

		if ((*cp != ',') && (*cp != '/'))
			return FALSE;

		cp++;

		if ((*cp == 's') || (*cp == 'S'))
			cp++;

		while ((*cp >= '0') && (*cp <= '8'))
			liverules[*cp++ - '0'] = ON;

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
		if (bornrules[i] == ON)
			*cp++ = '0' + i;
	}

	*cp++ = '/';
	*cp++ = 'S';

	for (i = 0; i < 9; i++)
	{
		if (liverules[i] == ON)
			*cp++ = '0' + i;
	}

	*cp = '\0';

	islife = (strcmp(rulestring, "B3/S23") == 0);

	return TRUE;
}



/* END CODE */
