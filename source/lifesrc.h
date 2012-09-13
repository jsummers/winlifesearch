/*
 * Life search program include file.
 * Original author: David I. Bell.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#define WLS_VERSION_STRING _T("0.70")

#define	ROWMAX		80	/* maximum rows for search rectangle */
#define	COLMAX		132	/* maximum columns for search rectangle */
#ifdef JS
#define	GENMAX		10	/* maximum number of generations */
#else
#define	GENMAX		19	/* maximum number of generations */
#endif
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
#ifdef JS
#define	DUMPVERSION	56		/* version of dump file  (was 6) */
#else
#define	DUMPVERSION	102		/* version of dump file */
#endif

#define	ALLOCSIZE	1000		/* chunk size for cell allocation */
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

typedef	unsigned char  PACKED_BOOL;
typedef	unsigned char  STATE;
typedef	unsigned int   STATUS;

/*
 * Status returned by routines
 */
#define	OK          ((STATUS) 0)
#define	ERROR1      ((STATUS) 1)
#define	CONSISTENT  ((STATUS) 2)
#define	NOTEXIST    ((STATUS) 3)
#define	FOUND       ((STATUS) 4)


/*
 * States of a cell
 */
#ifdef JS
#define	OFF	((STATE) 0x00)		/* cell is known off */
#define	ON	((STATE) 0x01)		/* cell is known on */
#define	UNK	((STATE) 0x10)		/* cell is unknown */
#define	NSTATES	3			/* number of states */
#else
#define	UNK	((STATE) 0)		/* cell is unknown */
#define	ON	((STATE) 1)		/* cell is known on */
#define	OFF	((STATE) 9)		/* cell is known off */
#define	NSTATES	3			/* number of states */
#endif

/*
 * Information about a row.
 */
typedef	struct {
	int	oncount;	/* number of cells which are set on */
} ROWINFO;


/*
 * Information about a column.
 */
typedef struct {
	int	setcount;	/* number of cells which are set */
	int	oncount;	/* number of cells which are set on */
	int	sumpos;		/* sum of row positions for on cells */
} COLINFO;


/*
 * Information about one cell of the search.
 */
typedef	struct cell CELL;

struct cell {
	// state is the most used field so let's put it first

	STATE	state;		/* current state */

	// it makes one byte
	// let's align the address before the pointers start

	PACKED_BOOL free;	/* TRUE if this cell still has free choice */

	// aligned to two bytes - let's round it up to four

	short	gen;		/* generation number of this cell */
	short	row;		/* row of this cell */
	short	col;		/* column of this cell */

	// and now for the pointers

	CELL *	past;		/* cell in the past at this location */
	CELL *	future;		/* cell in the future at this location */
	CELL *	cul;		/* cell to up and left */
	CELL *	cu;			/* cell to up */
	CELL *	cur;		/* cell to up and right */
	CELL *	cl;			/* cell to left */
	CELL *	cr;			/* cell to right */
	CELL *	cdl;		/* cell to down and left */
	CELL *	cd;			/* cell to down */
	CELL *	cdr;		/* cell to down and right */
	CELL *	loop;		/* next cell in same loop as this one */
	CELL *	search;		/* cell next to be searched for setting */

	ROWINFO * rowinfo;	/* information about this cell's row */
	COLINFO * colinfo;	/* information about this cell's column */

	short	near1;		/* count of cells this cell is near */

	PACKED_BOOL frozen;	/* TRUE if this cell is frozen in all gens */
#ifdef JS
	PACKED_BOOL choose;	/* TRUE if can choose this cell if unknown */
#else
	PACKED_BOOL active; /* FALSE if mirror by a symmetry */
	PACKED_BOOL unchecked; /* TRUE for unchecked cells */

	STATE combined;
#endif
};


typedef	unsigned char   FLAGS;

struct globals_struct {
/*
 * Current parameter values for the program to be saved over runs.
 * These values are dumped and loaded by the dump and load commands.
 * If you add another parameter, be sure to also add it to param_table,
 * preferably at the end so as to minimize dump file incompatibilities.
 */
	STATUS curstatus; /* current status of search */
	int nrows; /* number of rows available to the current search */
	int ncols; /* number of columns available to the current search */
	int period; /* number of generations in the current search */
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

#ifndef JS
	BOOL smart;      /* use smart method (KAS) */
	BOOL smarton;
	BOOL combine;
	BOOL combining;
	int smartwindow; /* no. of cells to check */
	int smartthreshold; /* check threshold */
	int smartstatlen;
	int smartstatwnd;
	int smartstatsumlen;
	int smartstatsumwnd;
	int smartstatsumlenc;
	int smartstatsumwndc;
#endif

	int diagsort;
	int knightsort;
#ifdef JS
	int fastsym;
#endif
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
	BOOL debug;      /* enable debugging output (if compiled so) */
	BOOL inited;     /* initialization has been done */
#ifdef JS
	STATE bornrules[16]; /* rules for whether a cell is to be born */
	STATE liverules[16]; /* rules for whether a live cell stays alive */
#else
	BOOL bornrules[16];	/* rules for whether a cell is to be born */
	BOOL liverules[16];	/* rules for whether a live cell stays alive */
#endif
	int curgen;      /* current generation for display */
	int	outputcols;  /* number of columns to save for output */
	int	outputlastcols;  /* last number of columns output */
#ifndef JS
	int g0oncellcount;	/* number of live cells in generation 0 */
#endif
	int	cellcount;   /* number of live cells in generation 0 */
	long dumpfreq;   /* how often to perform dumps */
	long dumpcount;  /* counter for dumps */
	int viewfreq;   /* how often to view results */
	long viewcount;  /* counter for viewing */
	TCHAR dumpfile[80];   /* dump file name */
	TCHAR outputfile[80]; /* file to output results to */
#ifndef JS
	int smartlen0;
	int smartlen1;
	int smartcomb;
	STATE smartchoice; /* preferred state for the selected cell */
	STATE prevstate; /* the state of the last free cell before backup() */
#endif

/*
 * Data about all of the cells.
 */
	CELL * settable[MAXCELLS];	/* table of cells whose value is set */
	CELL ** newset;		/* where to add new cells into setting table */
	CELL ** nextset;	/* next cell in setting table to examine */
#ifdef JS
	CELL ** baseset;	/* base of changeable part of setting table */
	CELL * fullsearchlist;	/* complete list of cells to search */
#else
	CELL *  searchtable[MAXCELLS]; /* a stack of searchlist positions */
	CELL ** searchset;
#endif
	ROWINFO rowinfo[ROWMAX];	/* information about rows of gen 0 */
	COLINFO colinfo[COLMAX];	/* information about columns of gen 0 */
	int fullcolumns;	/* columns in gen 0 which are fully set */
#ifndef JS
	int combinedcells;
	int setcombinedcells;
	int differentcombinedcells;
#endif

#define CV_FORCEDOFF  0  // cell values - These must not be changed
#define CV_FORCEDON   1
#define CV_CLEAR      2
#define CV_UNCHECKED  3
#define CV_FROZEN     4
	int origfield[GENMAX][COLMAX][ROWMAX];
	int currfield[GENMAX][COLMAX][ROWMAX];

#define WLS_RULESTRING_LEN 50
	TCHAR rulestring[WLS_RULESTRING_LEN];
	int saveoutput;
#ifndef JS
	int saveoutputallgen;
	int stoponfound;
	int stoponstep;
	TCHAR state_filename[MAX_PATH];
#endif
	int foundcount;
	int writecount;
#ifdef JS
	BOOL islife;  /* whether the rules are for standard Life */
#endif

#ifdef JS
	/*
	 * Table of transitions.
	 * Given the state of a cell and its neighbors in one generation,
	 * this table determines the state of the cell in the next generation.
	 * The table is indexed by the descriptor value of a cell.
	 */
	STATE transit[256];
#endif

	/*
	 * Table of implications.
	 * Given the state of a cell and its neighbors in one generation,
	 * this table determines deductions about the cell and its neighbors
	 * in the previous generation.
	 * The table is indexed by the descriptor value of a cell.
	 */
#ifdef JS
#define WLS_IMPLIC_LEN 256
#else
#define WLS_IMPLIC_LEN 1000
#endif
	FLAGS implic[WLS_IMPLIC_LEN];

	int	newcellcount; /* number of cells ready for allocation */
	int	auxcellcount; /* number of cells in auxillary table */
	CELL * newcells; /* cells ready for allocation */
	CELL * searchlist; /* current list of cells to search */
	ROWINFO	dummyrowinfo; /* dummy info for ignored cells */
	COLINFO	dummycolinfo; /* dummy info for ignored cells */
#ifdef JS
	CELL * deadcell; /* boundary cell value */
#endif
	CELL * celltable[MAXCELLS]; /* table of usual cells */
	CELL * auxtable[AUXCELLS]; /* table of auxillary cells */

	void *memblks[2000];
	int memblks_used;
};


/*
 * Global procedures
 */

BOOL initcells(void);

#ifndef JS
void initsearchorder(void);
#endif

void wlsShowCurrentField(void);
void wlsShowCurrentField_Sync(void);
void wlsWriteCurrentFieldToFile(HWND hwndParent, TCHAR *file1, BOOL append);
#ifdef JS
void dumpstate(HWND hwndParent, TCHAR *file1);
#else
void dumpstate(HWND hwndParent, TCHAR *file1, BOOL echo);
#endif

void adjustnear(CELL *, int);
STATUS search(void);
BOOL proceed(CELL *, STATE, BOOL);
BOOL go(CELL *, STATE, BOOL);
BOOL setcell(CELL *, STATE, BOOL);
#ifndef JS
STATUS examinenext(void);
#endif

CELL * findcell(int, int, int);
CELL * backup(void);
BOOL subperiods(void);
BOOL loopcells(CELL *, CELL *);
#ifdef JS
void excludecone(int, int, int);
#endif
BOOL freezecell(int, int);
BOOL setrules(TCHAR *);
BOOL setrulesA(char *);
#ifdef JS
BOOL loadstate(HWND hwndParent, TCHAR *file1);
#else
BOOL loadstate(HWND hwndParent);
#endif
void getbackup(char *cp);

struct wcontext;
void wlsErrorf(struct wcontext *ctx, TCHAR *fmt, ...);
void wlsMessagef(struct wcontext *ctx, TCHAR *fmt, ...);
void wlsStatusf(struct wcontext *ctx, TCHAR *fmt, ...);
void ttystatus(TCHAR *, ...);
void record_malloc(int func,void *m);
#ifndef JS
BOOL set_initial_cells(void);
#endif
void wlsUpdateProgressCounter(void);
