/*
 * Life search program include file.
 * Author: David I. Bell.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>


/*
 * Use prototypes if available.
 */
#ifdef	__STDC__
#define	PROTO(a)	a
#else
#define	PROTO(a)	()
#endif


/*
 * Maximum dimensions of the search
 */
#define	ROWMAX		49	/* maximum rows for search rectangle */
#define	COLMAX		132	/* maximum columns for search rectangle */
#define	GENMAX		8	/* maximum number of generations */
#define	TRANSMAX	4	/* largest translation value allowed */


/*
 * Build options
 */
#ifndef DEBUGFLAG
#define	DEBUGFLAG	0	/* nonzero for debugging features */
#endif


/*
 * Other definitions
 */
#define	DUMPVERSION	6		/* version of dump file */

#define	ALLOCSIZE	100		/* chunk size for cell allocation */
#define	VIEWMULT	1000		/* viewing frequency multiplier */
#define	DUMPMULT	1000		/* dumping frequency multiplier */
#define	DUMPFILE	"lifesrc.dmp"	/* default dump file name */
#define	LINESIZE	132		/* size of input lines */

#define	MAXCELLS	((COLMAX + 2) * (ROWMAX + 2) * GENMAX)
#define	AUXCELLS	(TRANSMAX * (COLMAX + ROWMAX + 4) * 2)


/*
 * Debugging macros
 */
#if DEBUGFLAG
#define	DPRINTF0(fmt)			if (debug) printf(fmt)
#define	DPRINTF1(fmt,a1)		if (debug) printf(fmt,a1)
#define	DPRINTF2(fmt,a1,a2)		if (debug) printf(fmt,a1,a2)
#define	DPRINTF3(fmt,a1,a2,a3)		if (debug) printf(fmt,a1,a2,a3)
#define	DPRINTF4(fmt,a1,a2,a3,a4)	if (debug) printf(fmt,a1,a2,a3,a4)
#define	DPRINTF5(fmt,a1,a2,a3,a4,a5)	if (debug) printf(fmt,a1,a2,a3,a4,a5)
#else
#define	DPRINTF0(fmt)
#define	DPRINTF1(fmt,a1)
#define	DPRINTF2(fmt,a1,a2)
#define	DPRINTF3(fmt,a1,a2,a3)
#define	DPRINTF4(fmt,a1,a2,a3,a4)
#define	DPRINTF5(fmt,a1,a2,a3,a4,a5)
#endif


#define	isdigit(ch)	(((ch) >= '0') && ((ch) <= '9'))
#define	isblank(ch)	(((ch) == ' ') || ((ch) == '\t'))


typedef	int		BOOL;
typedef	char		PACKED_BOOL;
typedef	unsigned char	STATE;
typedef	unsigned int	STATUS;


#define	FALSE		((BOOL) 0)
#define	TRUE		((BOOL) 1)


/*
 * Status returned by routines
 */
#define	OK		((STATUS) 0)
#define	ERROR		((STATUS) 1)
#define	CONSISTENT	((STATUS) 2)
#define	NOTEXIST	((STATUS) 3)
#define	FOUND		((STATUS) 4)


/*
 * States of a cell
 */
#define	OFF	((STATE) 0x00)		/* cell is known off */
#define	ON	((STATE) 0x01)		/* cell is known on */
#define	UNK	((STATE) 0x10)		/* cell is unknown */
#define	NSTATES	3			/* number of states */


/*
 * Information about a row.
 */
typedef	struct
{
	int	oncount;	/* number of cells which are set on */
} ROWINFO;


/*
 * Information about a column.
 */
typedef struct
{
	int	setcount;	/* number of cells which are set */
	int	oncount;	/* number of cells which are set on */
	int	sumpos;		/* sum of row positions for on cells */
} COLINFO;


/*
 * Information about one cell of the search.
 */
typedef	struct cell CELL;

struct cell
{
	STATE	state;		/* current state */
	PACKED_BOOL free;	/* TRUE if this cell still has free choice */
	PACKED_BOOL frozen;	/* TRUE if this cell is frozen in all gens */
	PACKED_BOOL choose;	/* TRUE if can choose this cell if unknown */
	short	gen;		/* generation number of this cell */
	short	row;		/* row of this cell */
	short	col;		/* column of this cell */
	short	near;		/* count of cells this cell is near */
	CELL *	search;		/* cell next to be searched for setting */
	CELL *	past;		/* cell in the past at this location */
	CELL *	future;		/* cell in the future at this location */
	CELL *	cul;		/* cell to up and left */
	CELL *	cu;		/* cell to up */
	CELL *	cur;		/* cell to up and right */
	CELL *	cl;		/* cell to left */
	CELL *	cr;		/* cell to right */
	CELL *	cdl;		/* cell to down and left */
	CELL *	cd;		/* cell to down */
	CELL *	cdr;		/* cell to down and right */
	CELL *	loop;		/* next cell in same loop as this one */
	ROWINFO * rowinfo;	/* information about this cell's row */
	COLINFO * colinfo;	/* information about this cell's column */
};

#define	NULL_CELL	((CELL *) 0)


/*
 * Declare this macro so that by default the variables are defined external.
 * In the main program, this is defined as a null value so as to actually
 * define the variables.
 */
#ifndef	EXTERN
#define	EXTERN	extern
#endif


/*
 * Current parameter values for the program to be saved over runs.
 * These values are dumped and loaded by the dump and load commands.
 * If you add another parameter, be sure to also add it to param_table,
 * preferably at the end so as to minimize dump file incompatibilities.
 */
EXTERN	STATUS	curstatus;	/* current status of search */
EXTERN	int	rowmax;		/* maximum number of rows */
EXTERN	int	colmax;		/* maximum number of columns */
EXTERN	int	genmax;		/* maximum number of generations */
EXTERN	int	rowtrans;	/* translation of rows */
EXTERN	int	coltrans;	/* translation of columns */
EXTERN	BOOL	rowsym;		/* enable row symmetry starting at column */
EXTERN	BOOL	colsym;		/* enable column symmetry starting at row */
EXTERN	BOOL	pointsym;	/* enable symmetry with central point */
EXTERN	BOOL	fwdsym;		/* enable forward diagonal symmetry */
EXTERN	BOOL	bwdsym;		/* enable backward diagonal symmetry */
EXTERN	BOOL	fliprows;	/* flip rows at column number from last to first generation */
EXTERN	BOOL	flipcols;	/* flip columns at row number from last to first generation */
EXTERN	BOOL	flipquads;	/* flip quadrants from last to first gen */
EXTERN	BOOL	parent;		/* only look for parents */
EXTERN	BOOL	allobjects;	/* look for all objects including subperiods */
EXTERN	int	nearcols;	/* maximum distance to be near columns */
EXTERN	int	maxcount;	/* maximum number of cells in generation 0 */
EXTERN	int	userow;		/* row that must have at least one ON cell */
EXTERN	int	usecol;		/* column that must have at least one ON cell */
EXTERN	int	colcells;	/* maximum cells in a column */
EXTERN	int	colwidth;	/* maximum width of each column */
EXTERN	BOOL	follow;		/* follow average position of previous column */
EXTERN	BOOL	orderwide;	/* ordering tries to find wide objects */
EXTERN	BOOL	ordergens;	/* ordering tries all gens first */
EXTERN	BOOL	ordermiddle;	/* ordering tries middle columns first */
EXTERN	BOOL	followgens;	/* try to follow setting of other gens */


/*
 * These values are not affected when dumping and loading since they
 * do not affect the status of a search in progress.
 * They are either settable on the command line or are computed.
 */
EXTERN	BOOL	quiet;		/* don't output */
EXTERN	BOOL	debug;		/* enable debugging output (if compiled so) */
EXTERN	BOOL	quitok;		/* ok to quit without confirming */
EXTERN	BOOL	inited;		/* initialization has been done */
EXTERN	STATE	bornrules[9];	/* rules for whether a cell is to be born */
EXTERN	STATE	liverules[9];	/* rules for whether a live cell stays alive */
EXTERN	int	curgen;		/* current generation for display */
EXTERN	int	outputcols;	/* number of columns to save for output */
EXTERN	int	outputlastcols;	/* last number of columns output */
EXTERN	int	cellcount;	/* number of live cells in generation 0 */
EXTERN	long	dumpfreq;	/* how often to perform dumps */
EXTERN	long	dumpcount;	/* counter for dumps */
EXTERN	long	viewfreq;	/* how often to view results */
EXTERN	long	viewcount;	/* counter for viewing */
EXTERN	char *	dumpfile;	/* dump file name */
EXTERN	char *	outputfile;	/* file to output results to */


/*
 * Data about all of the cells.
 */
EXTERN	CELL *	settable[MAXCELLS];	/* table of cells whose value is set */
EXTERN	CELL **	newset;		/* where to add new cells into setting table */
EXTERN	CELL **	nextset;	/* next cell in setting table to examine */
EXTERN	CELL **	baseset;	/* base of changeable part of setting table */
EXTERN	CELL *	fullsearchlist;	/* complete list of cells to search */
EXTERN	ROWINFO	rowinfo[ROWMAX];	/* information about rows of gen 0 */
EXTERN	COLINFO	colinfo[COLMAX];	/* information about columns of gen 0 */
EXTERN	int	fullcolumns;	/* columns in gen 0 which are fully set */


/*
 * Global procedures
 */
extern	void	getcommands PROTO((void));
extern	void	initcells PROTO((void));
extern	void	printgen PROTO((int));
extern	void	writegen PROTO((char *, BOOL));
extern	void	dumpstate PROTO((char *));
extern	void	adjustnear PROTO((CELL *, int));
extern	STATUS	search PROTO((void));
extern	STATUS	proceed PROTO((CELL *, STATE, BOOL));
extern	STATUS	go PROTO((CELL *, STATE, BOOL));
extern	STATUS	setcell PROTO((CELL *, STATE, BOOL));
extern	CELL *	findcell PROTO((int, int, int));
extern	CELL *	backup PROTO((void));
extern	BOOL	subperiods PROTO((void));
extern	void	loopcells PROTO((CELL *, CELL *));

extern	BOOL	ttyopen PROTO((void));
extern	BOOL	ttycheck PROTO((void));
extern	BOOL	ttyread PROTO((char *, char *, int));
extern	void	ttyprintf PROTO((char *, ...));
extern	void	ttystatus PROTO((char *, ...));
extern	void	ttywrite PROTO((char *, int));
extern	void	ttyhome PROTO((void));
extern	void	ttyeeop PROTO((void));
extern	void	ttyflush PROTO((void));
extern	void	ttyclose PROTO((void));

/* END CODE */
