/*
 * Life search program - user interactions module.
 * Author: David I. Bell.


  ****** Heavily modified. Modifications not noted consistently.  -JES ******
 */
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
static	int	foundcount;	/* number of objects found */
static	char *	initfile;	/* file containing initial cells */
static	char *	loadfile;	/* file to load state from */
extern int saveoutput;
extern int saveoutputallgen;
extern int stoponfound;
extern int origfield[GENMAX][COLMAX][ROWMAX];
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
static	int *	param_table[] =
{
	&curstatus,
	&rowmax, &colmax, &genmax, 
	&rowtrans, &coltrans,
	&rowsym, &colsym, &pointsym, &fwdsym, &bwdsym,
	&fliprows, &flipcols, &flipquads,
	&parent, &allobjects, &nearcols, &maxcount,
	&userow, &usecol, &colcells, &colwidth, &follow,
	&orderwide, &ordergens, &ordermiddle, &followgens, 
	&diagsort, &symmetry, &trans_rotate, &trans_flip, &trans_x, &trans_y,
	&knightsort,
	&smart, &smartwindow, &smartthreshold, 
	&foundcount,
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

	count = getnum(&cp, 0);

	if ((count <= 0) || *cp)
	{
		ttystatus("Must back up at least one cell\n");

		return;
	}

	while (count > 0)
	{
		cell = backup();

		if (cell == NULL)
		{
			printgen();
			ttystatus("Backed up over all possibilities\n");

			return;
		}

		state = 1 - cell->state;

		if (state == ON)
			count--;

		cell->state = UNK;

		if (!go(cell, state, FALSE))
		{
			printgen();
			ttystatus("Backed up over all possibilities\n");

			return;
		}
	}

	printgen();
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
	CELL * cell;
	int	row;
	int	col;
	int gen;
	int	ch;
	int	minrow, maxrow, mincol, maxcol;
	char buf[80];
	char file[MAX_PATH];
	static int writecount=0;

	if(!saveoutput && !outputcols) return;

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
	maxrow = -1;
	maxcol = -1;

	for (gen=0; gen < (saveoutputallgen ? genmax : 1); gen++)
	{
		for (row = 1; row <= rowmax; row++)
		{
			for (col = 1; col <= colmax; col++)
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
		for (gen = 0; gen < (saveoutputallgen ? genmax : 1); gen++)
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
					ttystatus("Bad cell state");
					fclose(fp);

					return;
				}

				fputc(ch, fp);
			}
			if (saveoutputallgen && (gen < genmax - 1)) fputs(" ... ", fp);
		}

		fputc('\n', fp);
	}

	if (append)
	{
		fprintf(fp, ".\n.\n");
	}

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
}


/*
 * Dump the current state of the search in the specified file.
 * If no file is specified, it is asked for.
 */
void dumpstate(char *file1, BOOL echo)
{
	FILE *	fp;
	CELL **	set;
	CELL *	cell;
	int	row;
	int	col;
	int	gen;
	int **	param;
	char file[MAX_PATH];

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

	for(gen=0;gen<genmax;gen++) {
		for(row=0;row<rowmax;row++) {
			for(col=0;col<colmax;col++) {
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

		fprintf(fp, "S %d %d %d %d %d\n", cell->row, cell->col,
			cell->gen, (cell->state == ON) ? 1 : 0, cell->free);
	}

	fprintf(fp, "E\n");

	if (fclose(fp))
	{
		ttystatus("Error writing \"%s\"\n", file);

		return;
	}

	if (echo) ttystatus("State dumped to \"%s\"\n", file);
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
	char file[MAX_PATH];
	int ver;

	STATUS status;

	strcpy(file,"");
	getfilename_l(file);

	if(file[0]=='\0') return FALSE;

	fp = fopen(file, "r");

	if (fp == NULL)
	{
		ttystatus("Cannot open state file \"%s\"\n", file);

		return FALSE;
	}

//*********************************************
// Read and check the file version
//*********************************************

	buf[0] = '\0';
	fgets(buf, LINESIZE, fp);

	if (buf[0] != 'V')
	{
		ttystatus("Missing version line in file \"%s\"\n", file);
		fclose(fp);

		return FALSE;
	}

	cp = &buf[1];
	ver = getnum(&cp, 0);

	if (DUMPVERSION != ver)
	{
		ttystatus("Incorrect version of the dump file: expected %d, found %d", DUMPVERSION, ver);
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
		ttystatus("Missing parameter line in state file\n");
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
		ttystatus("Missing rule line in state file\n");
		fclose(fp);

		return FALSE;
	}
	cp = &buf[strlen(buf) - 1];

	if (*cp == '\n') *cp = '\0';

	cp = &buf[1];

	while (isblank(*cp)) cp++;

	if (!setrules(cp))
	{
		ttystatus("Bad Life rules in state file\n");
		fclose(fp);

		return FALSE;
	}

//*********************************************
// Read the initial state
//*********************************************

	for(gen=0;gen<genmax;gen++) {
		for(row=0;row<rowmax;row++) {
			fgets(buf, LINESIZE, fp);
			cp=strtok(buf," ");
			for(col=0;col<colmax;col++) {
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
		buf[0] = '\0';
		fgets(buf, LINESIZE, fp);

		if (buf[0] != 'S')
			break;

		cp = &buf[1];
		row = getnum(&cp, 0);
		col = getnum(&cp, 0);
		gen = getnum(&cp, 0);
		state = (getnum(&cp, 0) != 0) ? ON : OFF;
		free = getnum(&cp, 0);

		cell = findcell(row, col, gen);

		if (!setcell(cell, state, free))
		{
			ttystatus(
				"Inconsistently setting cell at r%d c%d g%d \n",
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
		ttystatus("Inconsistent cell status\n");
		fclose(fp);

		return FALSE;
	}

//*********************************************
// Check the presence of the 'end' line
//*********************************************

	if (buf[0] != 'E')
	{
		ttystatus("Missing end of file line in state file\n");
		fclose(fp);

		return FALSE;
	}

	if (fclose(fp))
	{
		ttystatus("Error reading \"%s\"\n", file);

		return FALSE;
	}

	ttystatus("State loaded from \"%s\"\n", file);
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
		bornrules[i] = FALSE;
		liverules[i] = FALSE;
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

	islife = (strcmp(rulestring, "B3/S23") == 0);

	return TRUE;
}



/* END CODE */
