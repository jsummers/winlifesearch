/*
 * Life search program - user interactions module.
 * Author: David I. Bell.
 */

#include "lifesrc.h"

#define	VERSION	"3.5"


/*
 * Local data.
 */
static	BOOL	nowait;		/* don't wait for commands after loading */
static	BOOL	setall;		/* set all cells from initial file */
static	BOOL	islife;		/* whether the rules are for standard Life */
static	char	rulestring[20];	/* rule string for printouts */
static	long	foundcount;	/* number of objects found */
static	char *	initfile;	/* file containing initial cells */
static	char *	loadfile;	/* file to load state from */


/*
 * Local procedures
 */
static	void	usage PROTO((void));
static	void	getsetting PROTO((char *));
static	void	getbackup PROTO((char *));
static	void	getclear PROTO((char *));
static	void	getexclude PROTO((char *));
static	void	getfreeze PROTO((char *));
static	void	excludecone PROTO((int, int, int));
static	void	freezecell PROTO((int, int));
static	STATUS	loadstate PROTO((char *));
static	STATUS	readfile PROTO((char *));
static	BOOL	confirm PROTO((char *));
static	BOOL	setrules PROTO((char *));
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
	NULL
};


int
main(argc, argv)
	int	argc;
	char **	argv;
{
	char *	str;

	if (--argc <= 0)
	{
		usage();
		exit(1);
	}

	argv++;

	if (!setrules("3/23"))
	{
		fprintf(stderr, "Cannot set Life rules!\n");
		exit(1);
	}

	while (argc-- > 0)
	{
		str = *argv++;

		if (*str++ != '-')
		{
			usage();
			exit(1);
		}

		switch (*str++)
		{
			case 'q':
				quiet = TRUE;		/* don't output */
				break;

			case 'r':			/* rows */
				rowmax = atoi(str);
				break;

			case 'c':			/* columns */
				colmax = atoi(str);
				break;

			case 'g':			/* generations */
				genmax = atoi(str);
				break;

			case 't':			/* translation */
				switch (*str++)
				{
					case 'r':
						rowtrans = atoi(str);
						break;

					case 'c':
						coltrans = atoi(str);
						break;

					default:
						fprintf(stderr, "Bad translate\n");
						exit(1);
				}

				break;

			case 'f':			/* flip cells */
				switch (*str++)
				{
					case 'r':
						fliprows = 1;

						if (*str)
							fliprows = atoi(str);

						break;

					case 'c':
						flipcols = 1;

						if (*str)
							flipcols = atoi(str);

						break;

					case 'q':
						flipquads = TRUE;
						break;

					case 'g':
						followgens = TRUE;
						break;

					case '\0':
						follow = TRUE;
						break;

					default:
						fprintf(stderr, "Bad flip\n");
						exit(1);
				}

				break;

			case 's':			/* symmetry */
				switch (*str++)
				{
					case 'r':
						rowsym = 1;

						if (*str)
							rowsym = atoi(str);

						break;

					case 'c':
						colsym = 1;

						if (*str)
							colsym = atoi(str);

						break;

					case 'p':
						pointsym = TRUE;
						break;

					case 'f':
						fwdsym = TRUE;
						break;

					case 'b':
						bwdsym = TRUE;
						break;

					default:
						fprintf(stderr, "Bad symmetry\n");
						exit(1);
				}

				break;

			case 'n':			/* near cells */
				switch (*str++)
				{
					case 'c':
						nearcols = atoi(str);
						break;

					default:
						fprintf(stderr, "Bad near\n");
						exit(1);
				}

				break;

			case 'w':			/* max width */
				switch (*str++)
				{
					case 'c':
						colwidth = atoi(str);
						break;

					default:
						fprintf(stderr, "Bad width\n");
						exit(1);
				}

				break;

			case 'u':			/* use row or column */
				switch (*str++)
				{
					case 'r':
						userow = atoi(str);
						break;

					case 'c':
						usecol = atoi(str);
						break;

					default:
						fprintf(stderr, "Bad use\n");
						exit(1);
				}

				break;

			case 'd':			/* dump frequency */
				dumpfreq = atol(str) * DUMPMULT;
				dumpfile = DUMPFILE;

				if ((argc > 0) && (**argv != '-'))
				{
					argc--;
					dumpfile = *argv++;
				}

				break;

			case 'v':			/* view frequency */
				viewfreq = atol(str) * VIEWMULT;
				break;

			case 'l':			/* load file */
				if (*str == 'n')
					nowait = TRUE;

				if ((argc <= 0) || (**argv == '-'))
				{
					fprintf(stderr, "Missing load file name\n");
					exit(1);
				}

				loadfile = *argv++;
				argc--;
				break;

			case 'i':			/* initial file */
				if (*str != 'n')
					setall = TRUE;

				if ((argc <= 0) || (**argv == '-'))
				{
					fprintf(stderr, "Missing initial file name\n");
					exit(1);
				}

				initfile = *argv++;
				argc--;
				break;

			case 'o':
				if ((*str == '\0') || isdigit(*str))
				{
					/*
					 * Output file name
					 */
					outputcols = atol(str);

					if ((argc <= 0) || (**argv == '-'))
					{
						fprintf(stderr,
						"Missing output file name\n");
						exit(1);
					}

					outputfile = *argv++;
					argc--;
					break;
				}

				/*
				 * An ordering option.
				 */
				while (*str)
				{
					switch (*str++)
					{
						case 'w':
							orderwide = TRUE;
							break;

						case 'g':
							ordergens = TRUE;
							break;

						case 'm':
							ordermiddle = TRUE;
							break;

						default:
							fprintf(stderr,
							"Bad ordering option\n");
							exit(1);
					}
				}

				break;

			case 'm':			/* max cell count */
				switch (*str++)
				{
					case 'c':
						colcells = atoi(str);
						break;

					case 't':
						maxcount = atoi(str);
						break;

					default:
						fprintf(stderr, "Bad maximum\n");
						exit(1);
				}

				break;

			case 'p':			/* find parents only */
				parent = TRUE;
				break;

			case 'a':
				allobjects = TRUE;	/* find all objects */
				break;

			case 'D':			/* debugging output */
				debug = TRUE;
				break;

			case 'R':			/* set rules */
				if (!setrules(str))
				{
					fprintf(stderr, "Bad rule string\n");
					exit(1);
				}

				break;

			default:
				fprintf(stderr, "Unknown option -%c\n",
					str[-1]);

				exit(1);
		}
	}

	if (parent && (rowtrans || coltrans || flipquads ||
		fliprows || flipcols))
	{
		fprintf(stderr, "Cannot specify translations or flips with -p\n");
		exit(1);
	}

	if ((pointsym != 0) + (rowsym || colsym) + (fwdsym || bwdsym) > 1)
	{
		fprintf(stderr, "Conflicting symmetries specified\n");
		exit(1);
	}

	if ((fwdsym || bwdsym || flipquads) && (rowmax != colmax))
	{
		fprintf(stderr, "Rows must equal cols with -sf, -sb, or -fq\n");
		exit(1);
	}

	if ((rowtrans || coltrans) + (flipquads != 0) > 1)
	{
		fprintf(stderr, "Conflicting translation or flipping specified\n");
		exit(1);
	}

	if ((rowtrans && fliprows) || (coltrans && flipcols))
	{
		fprintf(stderr, "Conflicting translation or flipping specified\n");
		exit(1);
	}

	if ((userow < 0) || (userow > rowmax))
	{
		fprintf(stderr, "Bad row for -ur\n");
		exit(1);
	}

	if ((usecol < 0) || (usecol > colmax))
	{
		fprintf(stderr, "Bad column for -uc\n");
		exit(1);
	}

	if (!ttyopen())
	{
		fprintf(stderr, "Cannot initialize terminal\n");
		exit(1);
	}

	/*
	 * Check for loading state from file or reading initial
	 * object from file.
	 */
	if (loadfile)
	{
		if (loadstate(loadfile) != OK)
		{
			ttyclose();
			exit(1);
		}
	}
	else
	{
		initcells();

		if (initfile)
		{
			if (readfile(initfile) != OK)
			{
				ttyclose();
				exit(1);
			}

			baseset = nextset;
		}
	}

	/*
	 * If we are looking for parents, then set the current generation
	 * to the last one so that it can be input easily.  Then get the
	 * commands to initialize the cells, unless we were told to not wait.
	 */
	if (parent)
		curgen = genmax - 1;

	if (nowait && !quiet)
		printgen(0);
	else
		getcommands();

	inited = TRUE;

	/*
	 * Initial commands are complete, now look for the object.
	 */
	while (TRUE)
	{
		if (curstatus == OK)
			curstatus = search();

		if ((curstatus == FOUND) && userow &&
			(rowinfo[userow].oncount == 0))
		{
			curstatus = OK;
			continue;
		}

		if ((curstatus == FOUND) && !allobjects && subperiods())
		{
			curstatus = OK;
			continue;
		}

		if (dumpfreq)
		{
			dumpcount = 0;
			dumpstate(dumpfile);
		}

		quitok = (curstatus == NOTEXIST);

		curgen = 0;

		if (outputfile == NULL)
		{
			getcommands();
			continue;
		}

		/*
		 * Here if results are going to a file.
		 */
		if (curstatus == FOUND)
		{
			curstatus = OK;

			if (!quiet)
			{
				printgen(0);
				ttystatus("Object %ld found.\n", ++foundcount);
			}

			writegen(outputfile, TRUE);
			continue;
		}

		if (foundcount == 0)
		{
			ttyclose();
			fprintf(stderr, "No objects found\n");
			exit(1);
		}

		ttyclose();

		if (!quiet)
			printf("Search completed, file \"%s\" contains %ld object%s\n",
				outputfile, foundcount, (foundcount == 1) ? "" : "s");

		exit(0);
	}
}


/*
 * Get one or more user commands.
 * Commands are ended by a blank line.
 */
void
getcommands()
{
	char *	cp;
	char *	cmd;
	char	buf[LINESIZE];

	dumpcount = 0;
	viewcount = 0;
	printgen(curgen);

	while (TRUE)
	{
		if (!ttyread("> ", buf, LINESIZE))
		{
			ttyclose();
			exit(0);
		}

		cp = buf;

		while (isblank(*cp))
			cp++;

		cmd = cp;

		if (*cp)
			cp++;

		while (isblank(*cp))
			cp++;

		switch (*cmd)
		{
			case 'p':		/* print previous generation */
				printgen((curgen + genmax - 1) % genmax);
				break;

			case 'n':		/* print next generation */
				printgen((curgen + 1) % genmax);
				break;

			case 's':		/* add a cell setting */
				getsetting(cp);
				break;

			case 'b':		/* backup the search */
				getbackup(cp);
				break;

			case 'c':		/* clear area */
				getclear(cp);
				break;

			case 'v':		/* set view frequency */
				viewfreq = atol(cp) * VIEWMULT;
				printgen(curgen);
				break;

			case 'w':		/* write generation to file */
				writegen(cp, FALSE);
				break;

			case 'd':		/* dump state to file */
				dumpstate(cp);
				break;

			case 'N':		/* find next object */
				if (curstatus == FOUND)
					curstatus = OK;

				return;

			case 'q':		/* quit program */
			case 'Q':
				if (quitok || confirm("Really quit? "))
				{
					ttyclose();
					exit(0);
				}

				break;

			case 'x':		/* exclude cells from search */
				getexclude(cp);
				break;

			case 'f':		/* freeze state of cells */
				getfreeze(cp);
				break;
	
			case '\n':		/* return from commands */
			case '\0':
				return;

			default:
				if (isdigit(*cmd))
				{
					getsetting(cmd);
					break;
				}

				ttystatus("Unknown command\n");
				break;
		}
	}
}


/*
 * Get a cell to be set in the current generation.
 * The state of the cell is defaulted to ON.
 * Warning: Use of this routine invalidates backing up over
 * the setting, so that the setting is permanent.
 */
static void
getsetting(cp)
	char *	cp;
{
	int	row;
	int	col;
	STATE	state;

	cp = getstr(cp, "Cell to set (row col [state]): ");

	if (*cp == '\0')
		return;

	row = getnum(&cp, -1);

	if (*cp == ',')
		cp++;

	col = getnum(&cp, -1);

	if (*cp == ',')
		cp++;

	state = getnum(&cp, 1);

	while (isblank(*cp))
		cp++;

	if (*cp != '\0')
	{
		ttystatus("Bad input line format\n");

		return;
	}

	if ((row <= 0) || (row > rowmax) || (col <= 0) || (col > colmax) ||
		((state != 0) && (state != 1)))
	{
		ttystatus("Illegal cell value\n");

		return;
	}

	if (proceed(findcell(row, col, curgen), state, FALSE) != OK)
	{
		ttystatus("Inconsistent state for cell\n");

		return;
	}

	baseset = nextset;
	printgen(curgen);
}


/*
 * Backup the search to the nth latest free choice.
 * Notice: This skips examinination of some of the possibilities, thus
 * maybe missing a solution.  Therefore this should only be used when it
 * is obvious that the current search state is useless.
 */
static void
getbackup(cp)
	char *	cp;
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


/*
 * Clear all remaining unknown cells in the current generation or all
 * generations, or else just the specified rectangular area.  If
 * clearing the whole area, then confirmation is required.
 */
static void
getclear(cp)
	char *	cp;
{
	int	beggen;
	int	begrow;
	int	begcol;
	int	endgen;
	int	endrow;
	int	endcol;
	int	gen;
	int	row;
	int	col;
	CELL *	cell;

	/*
	 * Assume we are doing just this generation, but if the 'cg'
	 * command was given, then clear in all generations.
	 */
	beggen = curgen;
	endgen = curgen;

	if (*cp == 'g')
	{
		cp++;
		beggen = 0;
		endgen = genmax - 1;
	}

	while (isblank(*cp))
		cp++;

	/*
	 * Get the coordinates.
	 */
	if (*cp)
	{
		begrow = getnum(&cp, -1);
		begcol = getnum(&cp, -1);
		endrow = getnum(&cp, -1);
		endcol = getnum(&cp, -1);
	}
	else
	{
		if (!confirm("Clear all unknown cells ?"))
			return;

		begrow = 1;
		begcol = 1;
		endrow = rowmax;
		endcol = colmax;
	}

	if ((begrow < 1) || (begrow > endrow) || (endrow > rowmax) ||
		(begcol < 1) || (begcol > endcol) || (endcol > colmax))
	{
		ttystatus("Illegal clear coordinates");

		return;
	}

	for (row = begrow; row <= endrow; row++)
	{
		for (col = begcol; col <= endcol; col++)
		{
			for (gen = beggen; gen <= endgen; gen++)
			{
				cell = findcell(row, col, gen);

				if (cell->state != UNK)
					continue;

				if (proceed(cell, OFF, FALSE) != OK)
				{
					ttystatus("Inconsistent state for cell\n");

					return;
				}
			}
		}
	}

	baseset = nextset;
	printgen(curgen);
}


/*
 * Exclude cells in a rectangular area from searching.
 * This simply means that such cells will not be selected for setting.
 */
static void
getexclude(cp)
	char *	cp;
{
	int	begrow;
	int	begcol;
	int	endrow;
	int	endcol;
	int	row;
	int	col;

	while (isblank(*cp))
		cp++;

	if (*cp == '\0')
	{
		ttystatus("Coordinates needed for exclusion");

		return;
	}

	begrow = getnum(&cp, -1);
	begcol = getnum(&cp, -1);
	endrow = begrow;
	endcol = begcol;

	while (isblank(*cp))
		cp++;

	if (*cp)
	{
		endrow = getnum(&cp, -1);
		endcol = getnum(&cp, -1);
	}

	if ((begrow < 1) || (begrow > endrow) || (endrow > rowmax) ||
		(begcol < 1) || (begcol > endcol) || (endcol > colmax))
	{
		ttystatus("Illegal exclusion coordinates");

		return;
	}

	for (row = begrow; row <= endrow; row++)
	{
		for (col = begcol; col <= endcol; col++)
			excludecone(row, col, curgen);
	}

	printgen(curgen);
}


/*
 * Exclude all cells within the previous light cone centered at the
 * specified cell from searching.
 */
static void
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
 * Freeze cells in a rectangular area so that their states in all
 * generations are the same.
 */
static void
getfreeze(cp)
	char *	cp;
{
	int	begrow;
	int	begcol;
	int	endrow;
	int	endcol;
	int	row;
	int	col;

	while (isblank(*cp))
		cp++;

	if (*cp == '\0')
	{
		ttystatus("Coordinates needed for freezing");

		return;
	}

	begrow = getnum(&cp, -1);
	begcol = getnum(&cp, -1);
	endrow = begrow;
	endcol = begcol;

	while (isblank(*cp))
		cp++;

	if (*cp)
	{
		endrow = getnum(&cp, -1);
		endcol = getnum(&cp, -1);
	}

	if ((begrow < 1) || (begrow > endrow) || (endrow > rowmax) ||
		(begcol < 1) || (begcol > endcol) || (endcol > colmax))
	{
		ttystatus("Illegal freeze coordinates");

		return;
	}

	for (row = begrow; row <= endrow; row++)
	{
		for (col = begcol; col <= endcol; col++)
			freezecell(row, col);
	}

	printgen(curgen);
}


/*
 * Freeze all generations of the specified cell.
 * A frozen cell can be ON or OFF, but must be the same in all generations.
 * This routine marks them as frozen, and also inserts all the cells of
 * the generation into the same loop so that they will be forced
 * to have the same state.
 */
void
freezecell(row, col)
	int	row;
	int	col;
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
 * Print out the current status of the specified generation.
 * This also sets the current generation.
 */
void
printgen(gen)
	int	gen;
{
	int	row;
	int	col;
	int	count;
	CELL *	cell;
	char *	msg;

	curgen = gen;

	switch (curstatus)
	{
		case NOTEXIST:	msg = "No such object"; break;
		case FOUND:	msg = "Found object"; break;
		default:	msg = ""; break;
	}

	count = 0;

	for (row = 1; row <= rowmax; row++)
	{
		for (col = 1; col <= colmax; col++)
		{
			count += (findcell(row, col, gen)->state == ON);
		}
	}

	ttyhome();
	ttyeeop();

	if (islife)
	{
		ttyprintf("%s (gen %d, cells %d)", msg, gen, count);
	}
	else
	{
		ttyprintf("%s (rule %s, gen %d, cells %d)",
			msg, rulestring, gen, count);
	}

	ttyprintf(" -r%d -c%d -g%d", rowmax, colmax, genmax);

	if (rowtrans)
		ttyprintf(" -tr%d", rowtrans);

	if (coltrans)
		ttyprintf(" -tc%d", coltrans);

	if (fliprows == 1)
		ttyprintf(" -fr");

	if (fliprows > 1)
		ttyprintf(" -fr%d", fliprows);

	if (flipcols == 1)
		ttyprintf(" -fc");

	if (flipcols > 1)
		ttyprintf(" -fc%d", flipcols);

	if (flipquads)
		ttyprintf(" -fq");

	if (rowsym == 1)
		ttyprintf(" -sr");

	if (rowsym > 1)
		ttyprintf(" -sr%d", rowsym);

	if (colsym == 1)
		ttyprintf(" -sc");

	if (colsym > 1)
		ttyprintf(" -sc%d", colsym);

	if (pointsym)
		ttyprintf(" -sp");

	if (fwdsym)
		ttyprintf(" -sf");

	if (bwdsym)
		ttyprintf(" -sb");

	if (ordergens || orderwide || ordermiddle)
	{
		ttyprintf(" -o");

		if (ordergens)
			ttyprintf("g");

		if (orderwide)
			ttyprintf("w");

		if (ordermiddle)
			ttyprintf("m");
	}

	if (follow)
		ttyprintf(" -f");

	if (followgens)
		ttyprintf(" -fg");

	if (parent)
		ttyprintf(" -p");

	if (allobjects)
		ttyprintf(" -a");

	if (userow)
		ttyprintf(" -ur%d", userow);

	if (usecol)
		ttyprintf(" -uc%d", usecol);

	if (nearcols)
		ttyprintf(" -nc%d", nearcols);

	if (maxcount)
		ttyprintf(" -mt%d", maxcount);

	if (colcells)
		ttyprintf(" -mc%d", colcells);

	if (colwidth)
		ttyprintf(" -wc%d", colwidth);

	if (viewfreq)
		ttyprintf(" -v%ld", viewfreq / VIEWMULT);

	if (dumpfreq)
		ttyprintf(" -d%ld %s", dumpfreq / DUMPMULT, dumpfile);

	if (outputfile)
	{
		if (outputcols)
			ttyprintf(" -o%d %s", outputcols, outputfile);
		else
			ttyprintf(" -o %s", outputfile);

		if (foundcount)
			ttyprintf(" [%d]", foundcount);
	}

	ttyprintf("\n");

	for (row = 1; row <= rowmax; row++)
	{
		for (col = 1; col <= colmax; col++)
		{
			cell = findcell(row, col, gen);

			switch (cell->state)
			{
				case OFF:
					msg = ". ";
					break;

				case ON:
					msg = "O ";
					break;

				case UNK:
					msg = "? ";

					if (cell->frozen)
						msg = "+ ";

					if (!cell->choose)
						msg = "X ";

					break;
			}

			/*
			 * If wide output, print only one character,
			 * else print both characters.
			 */
			ttywrite(msg, (colmax < 40) + 1);
		}

		ttywrite("\n", 1);
	}

	ttyhome();
	ttyflush();
}


/*
 * Write the current generation to the specified file.
 * Empty rows and columns are not written.
 * If no file is specified, it is asked for.
 * Filename of "." means write to stdout.
 */
void
writegen(file, append)
	char *	file;		/* file name (or NULL) */
	BOOL	append;		/* TRUE to append instead of create */
{
	FILE *	fp;
	CELL *	cell;
	int	row;
	int	col;
	int	ch;
	int	minrow, maxrow, mincol, maxcol;

	file = getstr(file, "Write object to file: ");

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

	if (fp != stdout)
		ttystatus("\"%s\" written\n", file);

	quitok = TRUE;
}


/*
 * Dump the current state of the search in the specified file.
 * If no file is specified, it is asked for.
 */
void
dumpstate(file)
	char *	file;
{
	FILE *	fp;
	CELL **	set;
	CELL *	cell;
	int	row;
	int	col;
	int	gen;
	int **	param;

	file = getstr(file, "Dump state to file: ");

	if (*file == '\0')
		return;

	fp = fopen(file, "w");

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
 * Returns OK on success, ERROR on failure.
 */
static STATUS
loadstate(file)
	char *	file;
{
	FILE *	fp;
	char *	cp;
	int	row;
	int	col;
	int	gen;
	STATE	state;
	BOOL	free;
	BOOL	choose;
	CELL *	cell;
	int **	param;
	char	buf[LINESIZE];

	file = getstr(file, "Load state from file: ");

	if (*file == '\0')
		return OK;

	fp = fopen(file, "r");

	if (fp == NULL)
	{
		ttystatus("Cannot open state file \"%s\"\n", file);

		return ERROR;
	}

	buf[0] = '\0';
	fgets(buf, LINESIZE, fp);

	if (buf[0] != 'V')
	{
		ttystatus("Missing version line in file \"%s\"\n", file);
		fclose(fp);

		return ERROR;
	}

	cp = &buf[1];

	if (getnum(&cp, 0) != DUMPVERSION)
	{
		ttystatus("Unknown version in state file \"%s\"\n", file);
		fclose(fp);

		return ERROR;
	}

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

		if (setrules(cp))
		{
			ttystatus("Bad Life rules in state file\n");
			fclose(fp);

			return ERROR;
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

		return ERROR;
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
		state = getnum(&cp, 0);
		free = getnum(&cp, 0);

		cell = findcell(row, col, gen);

		if (setcell(cell, state, free) != OK)
		{
			ttystatus(
				"Inconsistently setting cell at r%d c%d g%d \n",
				row, col, gen);

			fclose(fp);

			return ERROR;
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

	if (buf[0] != 'T')
	{
		ttystatus("Missing table line in state file\n");
		fclose(fp);

		return ERROR;
	}

	cp = &buf[1];
	baseset = &settable[getnum(&cp, 0)];
	nextset = &settable[getnum(&cp, 0)];

	fgets(buf, LINESIZE, fp);

	if (buf[0] != 'E')
	{
		ttystatus("Missing end of file line in state file\n");
		fclose(fp);

		return ERROR;
	}

	if (fclose(fp))
	{
		ttystatus("Error reading \"%s\"\n", file);

		return ERROR;
	}

	ttystatus("State loaded from \"%s\"\n", file);
	quitok = TRUE;

	return OK;
}


/*
 * Read a file containing initial settings for either gen 0 or the last gen.
 * If setall is TRUE, both the ON and the OFF cells will be set.
 * Returns OK on success, ERROR on error.
 */
static STATUS
readfile(file)
	char *	file;
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

		return ERROR;
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

					return ERROR;
			}

			if (proceed(findcell(row, col, gen), state, FALSE)
				!= OK)
			{
				ttystatus("Inconsistent state for cell %d %d\n",
					row, col);
				fclose(fp);

				return ERROR;
			}
		}
	}

	if (fclose(fp))
	{
		ttystatus("Error reading \"%s\"\n", file);

		return ERROR;
	}

	return OK;
}


/*
 * Check a string for being NULL, and if so, ask the user to specify a
 * value for it.  Returned string may be static and thus is overwritten
 * for each call.  Leading spaces in the string are skipped over.
 */
static char *
getstr(str, prompt)
	char *	str;		/* string to check for NULLness */
	char *	prompt;		/* message to prompt with */
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
confirm(prompt)
	char *	prompt;
{
	int	ch;

	ch = *getstr(NULL, prompt);

	if ((ch == 'y') || (ch == 'Y'))
		return TRUE;

	return FALSE;
}


/*
 * Read a number from a string, eating any leading or trailing blanks.
 * Returns the value, and indirectly updates the string pointer.
 * Returns specified default if no number was found.
 */
static long
getnum(cpp, defnum)
	char **	cpp;
	int	defnum;
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
static BOOL
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


/*
 * Print usage text.
 */
static void
usage()
{
	char **	cpp;
	static char *text[] =
	{
	"",
	"lifesrc -r# -c# -g# [other options]",
	"lifesrc -l[n] file -v# -o# file -d# file",
	"",
	"   -r   Number of rows",
	"   -c   Number of columns",
	"   -g   Number of generations",
	"   -tr  Translate rows between last and first generation",
	"   -tc  Translate columns between last and first generation",
	"   -fr  Flip rows between last and first generation",
	"   -fc  Flip columns between last and first generation",
	"   -fq  Flip quadrants between last and first generation",
	"   -sr  Enforce symmetry on rows",
	"   -sc  Enforce symmetry on columns",
	"   -sp  Enforce symmetry around central point",
	"   -sf  Enforce symmetry on forward diagonal",
	"   -sb  Enforce symmetry on backward diagonal",
	"   -nc  Near N cells of live cells in previous columns for generation 0",
	"   -wc  Maximum width of live cells in each column for generation 0",
	"   -mt  Maximum total live cells for generation 0",
	"   -mc  Maximum live cells in any column for generation 0",
	"   -ur  Force using at least one ON cell in the given row for generation 0",
	"   -uc  Force using at least one ON cell in the given column for generation 0",
	"   -f   First follow the average location of the previous column's cells",
	"   -fg  First follow settings of previous or next generation",
	"   -ow  Set search order to find wide objects first",
	"   -og  Set search order to examine all gens in a column before next column",
	"   -om  Set search order to examine from middle column outwards",
	"   -p   Only look for parents of last generation",
	"   -a   Find all objects (even those with subperiods)",
	"   -v   View object every N thousand searches",
	"   -d   Dump status to file every N thousand searches",
	"   -l   Load status from file",
	"   -ln  Load status without entering command mode",
	"   -i   Read initial object setting both ON and OFF cells",
	"   -in  Read initial object from file setting only ON cells",
	"   -o   Output objects to file (appending) every N columns",
	"   -R   Use Life rules specified by born,live values",
	NULL
	};

	printf("Program to search for Life oscillators or spaceships (version %s)\n", VERSION);

	for (cpp = text; *cpp; cpp++)
		fprintf(stderr, "%s\n", *cpp);
}

/* END CODE */
