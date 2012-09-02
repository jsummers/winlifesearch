/*
 * Life search program include file.
 * Original author: David I. Bell.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <ctype.h>


/*
 * Maximum dimensions of the search
 */
#define	ROWMAX		80	/* maximum rows for search rectangle */
#define	COLMAX		132	/* maximum columns for search rectangle */
#define	GENMAX		10	/* maximum number of generations */
#define	TRANSMAX	8	/* largest translation value allowed */


/*
 * Build options
 */
#ifndef DEBUGFLAG
#define	DEBUGFLAG	0	/* nonzero for debugging features */
#endif


/*
 * Other definitions
 */
#define	DUMPVERSION	56		/* version of dump file  (was 6) */

#define	ALLOCSIZE	1000		/* chunk size for cell allocation */
#define	VIEWMULT	1000		/* viewing frequency multiplier */
#define	DUMPMULT	1000		/* dumping frequency multiplier */
#define	DUMPFILE	"lifesrc.dmp"	/* default dump file name */
#define	LINESIZE	1000		/* size of input lines */

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


//#define	isdigit(ch)	(((ch) >= '0') && ((ch) <= '9'))
#define	isblank(ch)	(((ch) == ' ') || ((ch) == '\t'))

typedef	unsigned int  PACKED_BOOL;
typedef	unsigned int  STATE;
typedef	unsigned int  STATUS;


/*
 * Status returned by routines
 */
#define	OK		((STATUS) 0)

#define	ERROR1		((STATUS) 1)

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
	short	near1;		/* count of cells this cell is near */

	int specsym;

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


struct globals_struct {
/*
 * Current parameter values for the program to be saved over runs.
 * These values are dumped and loaded by the dump and load commands.
 * If you add another parameter, be sure to also add it to param_table,
 * preferably at the end so as to minimize dump file incompatibilities.
 */
	STATUS curstatus; /* current status of search */
	int rowmax; /* maximum number of rows */
	int colmax; /* maximum number of columns */
	int genmax; /* maximum number of generations */
	int	rowtrans;   /* translation of rows */
	int	coltrans;   /* translation of columns */
	BOOL rowsym;    /* enable row symmetry starting at column */
	BOOL colsym;    /* enable column symmetry starting at row */
	BOOL pointsym;  /* enable symmetry with central point */
	BOOL fwdsym;    /* enable forward diagonal symmetry */
	BOOL bwdsym;    /* enable backward diagonal symmetry */
	BOOL fliprows;  /* flip rows at column number from last to first generation */
	BOOL flipcols;  /* flip columns at row number from last to first generation */
	BOOL flipquads; /* flip quadrants from last to first gen */
	BOOL parent;    /* only look for parents */
	BOOL allobjects;  /* look for all objects including subperiods */
	int nearcols;     /* maximum distance to be near columns */
	int maxcount;     /* maximum number of cells in generation 0 */
	int userow;       /* row that must have at least one ON cell */
	int usecol;       /* column that must have at least one ON cell */
	int colcells;     /* maximum cells in a column */
	int	colwidth;     /* maximum width of each column */
	BOOL follow;      /* follow average position of previous column */
	BOOL orderwide;   /* ordering tries to find wide objects */
	BOOL ordergens;   /* ordering tries all gens first */
	BOOL ordermiddle; /* ordering tries middle columns first */
	BOOL followgens;  /* try to follow setting of other gens */

	int diagsort;
	int knightsort;
	int fastsym;
	int symmetry;
	int trans_rotate;
	int trans_flip;
	int trans_x;
	int trans_y;

/*
 * These values are not affected when dumping and loading since they
 * do not affect the status of a search in progress.
 * They are either settable on the command line or are computed.
 */
	BOOL quiet;      /* don't output */
	BOOL debug;      /* enable debugging output (if compiled so) */
	BOOL quitok;     /* ok to quit without confirming */
	BOOL inited;     /* initialization has been done */
	STATE bornrules[16]; /* rules for whether a cell is to be born */
	STATE liverules[16]; /* rules for whether a live cell stays alive */
	int curgen;      /* current generation for display */
	int	outputcols;  /* number of columns to save for output */
	int	outputlastcols;  /* last number of columns output */
	int	cellcount;   /* number of live cells in generation 0 */
	long dumpfreq;   /* how often to perform dumps */
	long dumpcount;  /* counter for dumps */
	long viewfreq;   /* how often to view results */
	long viewcount;  /* counter for viewing */
	TCHAR dumpfile[80];   /* dump file name */
	TCHAR outputfile[80]; /* file to output results to */

/*
 * Data about all of the cells.
 */
	CELL * settable[MAXCELLS];	/* table of cells whose value is set */
	CELL ** newset;		/* where to add new cells into setting table */
	CELL ** nextset;	/* next cell in setting table to examine */
	CELL ** baseset;	/* base of changeable part of setting table */
	CELL * fullsearchlist;	/* complete list of cells to search */
	ROWINFO rowinfo[ROWMAX];	/* information about rows of gen 0 */
	COLINFO colinfo[COLMAX];	/* information about columns of gen 0 */
	int fullcolumns;	/* columns in gen 0 which are fully set */


	int origfield[GENMAX][COLMAX][ROWMAX];
	TCHAR rulestring[20];
	int saveoutput;
};


/*
 * Global procedures
 */

void	getcommands (void);
STATUS initcells(void);
void	printgen (int);
void	writegen (TCHAR *, BOOL);
void	dumpstate (TCHAR *);
void	adjustnear (CELL *, int);
STATUS	search (void);
STATUS	proceed (CELL *, STATE, BOOL);
STATUS	go (CELL *, STATE, BOOL);
STATUS	setcell (CELL *, STATE, BOOL);
CELL *	findcell (int, int, int);
CELL *	backup (void);
BOOL	subperiods (void);
STATUS loopcells(CELL *, CELL *);

void	ttystatus (TCHAR *, ...);

void	excludecone (int, int, int);
STATUS freezecell(int, int);

BOOL	setrules(TCHAR *);
#ifdef UNICODE
BOOL	setrulesA(char *);
#else
#define setrulesT setrules
#endif

STATUS  loadstate(TCHAR *file);
void     getbackup(char *cp);


void wlsError(TCHAR *m,int n);
void wlsWarning(TCHAR *m,int n);
void wlsMessage(TCHAR *m,int n);
int wlsQuery(TCHAR *m,int n);
void wlsStatus(TCHAR *msg);
void record_malloc(int func,void *m);
void showcount(int c);

/* END CODE */
