/*
 * Life search program - actual search routines.
 * Author: David I. Bell.
 * Based on the algorithms by Dean Hickerson that were
 * included with the "xlife 2.0" distribution.  Thanks!
 * Changes for arbitrary Life rules by Nathan S. Thompson.


  ****** Heavily modified. Modifications not noted consistently.  -JES ******
 */

/*
 * Define this as a null value so as to define the global variables
 * defined in lifesrc.h here.
 */
#define	EXTERN

#include <windows.h>
#include "wls.h"
#include "lifesrc.h"
#include <search.h>


#define SUMCOUNT 8

extern volatile int abortthread;
extern int symmetry;
extern int stoponstep;

/*
 * IMPLIC flag values.
 */
typedef	unsigned char	FLAGS;
#define IMPBAD	((FLAGS) 0x00)	// the cell state is inconsistent
#define IMPUN	((FLAGS) 0x01)	// change unknown neighbors (there are some)
#define IMPUN1	((FLAGS) 0x02)	// change unknown neighbors to 1 (if not set, then change to 0)
#define IMPC	((FLAGS) 0x04)	// change current cell (it is unknown)
#define IMPC1	((FLAGS) 0x08)	// change current cell to 1 (if not set, change it to 0)
#define IMPN	((FLAGS) 0x10)	// change new cell (it is unknown)
#define IMPN1	((FLAGS) 0x20)	// change new cell to 1 (if not set, change it to 0)
#define IMPVOID ((FLAGS) 0x40)  // invalid/unset implication
#define IMPOK   ((FLAGS) 0x80)  // valid state

/*
 * Table of implications.
 * Given the state of a cell and its neighbors in one generation,
 * this table determines deductions about the cell and its neighbors
 * in the previous generation.
 * The table is indexed by the descriptor value of a cell.
 */
static	FLAGS	implic[1000];

/*
 * Other local data.
 */
static	int	newcellcount;	/* number of cells ready for allocation */
static	int	auxcellcount;	/* number of cells in auxillary table */
static	CELL *	newcells;	/* cells ready for allocation */
static	CELL *	searchlist;	/* current list of cells to search */
static	CELL *	celltable[MAXCELLS];	/* table of usual cells */
static	CELL *	auxtable[AUXCELLS];	/* table of auxillary cells */
static	ROWINFO	dummyrowinfo;	/* dummy info for ignored cells */
static	COLINFO	dummycolinfo;	/* dummy info for ignored cells */


/*
 * Local procedures
 */
static	void	initimplic PROTO((void));
static	void	linkcell PROTO((CELL *));
static __inline	STATE	transition PROTO((STATE, int, int));
static	STATE	choose PROTO((CELL *));
static	FLAGS	implication PROTO((STATE, int, int));
static	CELL *	symcell PROTO((CELL *));
static	CELL *	mapcell PROTO((CELL *));
static	CELL *	allocatecell PROTO((void));
static	CELL *	getnormalunknown PROTO((void));
static	CELL *	getaverageunknown PROTO((void));
static	CELL *	getsmartunknown PROTO((void)); // KAS
static	BOOL	consistify PROTO((CELL *));
static	BOOL	consistify10 PROTO((CELL *));
static	BOOL	checkwidth PROTO((CELL *));
static	CELL *	(*getunknown) PROTO((void));

/*
 * Initialize the table of cells.
 * Each cell in the active area is set to unknown state.
 * Boundary cells are set to zero state.
 */
void
initcells()
{
	int	row, col, gen;
	int	i;
	BOOL	edge;
	CELL *	cell;
	CELL *	cell2;

	inited = FALSE;

	newcellcount=0;
	auxcellcount=0;
	newcells=NULL;
	searchlist=NULL;
	dummyrowinfo.oncount=0;
	dummycolinfo.oncount=0;


	if ((rowmax <= 0) || (rowmax > ROWMAX) ||
		(colmax <= 0) || (colmax > COLMAX) ||
		(genmax <= 0) || (genmax > GENMAX) ||
		(rowtrans < -TRANSMAX) || (rowtrans > TRANSMAX) ||
		(coltrans < -TRANSMAX) || (coltrans > TRANSMAX))
	{
		wlsError("ROW, COL, GEN, or TRANS out of range",0);
		exit(1);
	}

	for (i = 0; i < MAXCELLS; i++)
		celltable[i] = allocatecell();

	/*
	 * Link the cells together.
	 */
	for (col = 0; col <= colmax+1; col++)
	{
		for (row = 0; row <= rowmax+1; row++)
		{
			for (gen = 0; gen < genmax; gen++)
			{
				edge = ((row == 0) || (col == 0) ||
					(row > rowmax) || (col > colmax));

				cell = findcell(row, col, gen);
				cell->gen = gen;
				cell->row = row;
				cell->col = col;
				cell->rowinfo = &dummyrowinfo;
				cell->colinfo = &dummycolinfo;

				cell->active = TRUE;
				cell->unchecked = FALSE;

				/*
				 * If this is not an edge cell, then its state
				 * is unknown and it needs linking to its
				 * neighbors.
				 */
				if (!edge)
				{
					linkcell(cell);
					cell->state = UNK;
					cell->free = TRUE;
				}

				/*
				 * Map time forwards and backwards,
				 * wrapping around at the ends.
				 */
				cell->past = findcell(row, col,
					(gen+genmax-1) % genmax);

				cell->future = findcell(row, col,
					(gen+1) % genmax);

				/*
				 * If this is not an edge cell, and
				 * there is some symmetry, then put
				 * this cell in the same loop as the
				 * next symmetrical cell.
				 */
				if(symmetry && !edge)
				{
					loopcells(cell, symcell(cell));
				}
			}
		}
	}

	/*
	 * Now for the symmetry
	 * Let's look for all loops
	 * and select one cell from each loop as active
	 */

	for (col = 1; col <= colmax; col++) {
		for (row = 1; row <= rowmax; row++) {
			for (gen = 0; gen < genmax; gen++) {
				cell = findcell(row, col, gen);

				if (cell->active) {
					cell2 = cell->loop;
					while (cell2 != cell) {
						cell2->active = FALSE;
						cell2 = cell2->loop;
					}
				}
			}
		}
	}


	/*
	 * If there is a non-standard mapping between the last generation
	 * and the first generation, then change the future and past pointers
	 * to implement it.  This is for translations and flips.
	 */
	if (rowtrans || coltrans || fliprows || flipcols || flipquads)
	{
		for (row = 0; row <= rowmax+1; row++)
		{
			for (col = 0; col <= colmax+1; col++)
			{
				cell = findcell(row, col, 0);
				cell2 = mapcell(cell);
				cell->past = cell2;
				cell2->future = cell;
			}
		}
	}

	/*
	 * Initialize the row and column info addresses for generation 0.
	 */
	for (row = 1; row <= rowmax; row++)
	{
		for (col = 1; col <= colmax; col++)
		{
			cell = findcell(row, col, 0);
			cell->rowinfo = &rowinfo[row];
			cell->colinfo = &colinfo[col];
		}
	}

	if (smart) {
		getunknown = getsmartunknown; // KAS
	} else if (follow) {
		getunknown = getaverageunknown;
	} else {
		getunknown = getnormalunknown;
	}

	newset = settable;
	nextset = settable;

	searchset = searchtable;

	curstatus = OK;
	initimplic();

	inited = TRUE;
}


/*
 * The sort routine for searching.
 */
static int
ordersortfunc(const void *xxx1, const void *xxx2)
{
	CELL **	arg1;
	CELL **	arg2;
	CELL *	c1;
	CELL *	c2;
	int	midcol;
	int	midrow;
	int	dif1;
	int	dif2;

	arg1=(CELL**)xxx1;
	arg2=(CELL**)xxx2;

	c1 = *arg1;
	c2 = *arg2;

	/*
	 * If on equal position or not ordering by all generations
	 * then sort primarily by generations
	 */
	if (((c1->row == c2->row) && (c1->col == c2->col)) || !ordergens)
	{
		// Put generation 0 first
		// or if calculating parents, put generation 0 last
		if (parent)
		{
			if (c1->gen < c2->gen) return 1;
			if (c1->gen > c2->gen) return -1;
		} else {
			if (c1->gen < c2->gen) return -1;
			if (c1->gen > c2->gen) return 1;
		}
		// if we are here, it is the same cell
	}

	if(diagsort) {
		if(c1->col+c1->row > c2->col+c2->row) return 1;
		if(c1->col+c1->row < c2->col+c2->row) return -1;
		if(abs(c1->col-c1->row) > abs(c2->col-c2->row)) return (orderwide)?1:(-1);
		if(abs(c1->col-c1->row) < abs(c2->col-c2->row)) return (orderwide)?(-1):1;
	}
	if(knightsort) {
		if(c1->col*2+c1->row > c2->col*2+c2->row) return 1;
		if(c1->col*2+c1->row < c2->col*2+c2->row) return -1;
		if(abs(c1->col-c1->row) > abs(c2->col-c2->row)) return (orderwide)?1:(-1);
		if(abs(c1->col-c1->row) < abs(c2->col-c2->row)) return (orderwide)?(-1):1;
	}
	
	/*
	 * Sort on the column number.
	 * By default this is from left to right.
	 * But if middle ordering is set, the ordering is from the center
	 * column outwards.
	 */
	if (ordermiddle)
	{
		midcol = (colmax + 1) / 2;

		dif1 = abs(c1->col - midcol);

		dif2 = abs(c2->col - midcol);

		if (dif1 < dif2) return -1;

		if (dif1 > dif2) return 1;
	} else {
		if (c1->col < c2->col) return -1;

		if (c1->col > c2->col) return 1;
	}

	/*
	 * Sort on the row number.
	 * By default, this is from the middle row outwards.
	 * But if wide ordering is set, the ordering is from the edge
	 * inwards.  Note that we actually set the ordering to be the
	 * opposite of the desired order because the initial setting
	 * for new cells is OFF.
	 */
	midrow = (rowmax + 1) / 2;

	dif1 = abs(c1->row - midrow);

	dif2 = abs(c2->row - midrow);

	if (dif1 < dif2) return (orderwide ? -1 : 1);

	if (dif1 > dif2) return (orderwide ? 1 : -1);

	return 0;
}


/*
 * Order the cells to be searched by building the search table list.
 * This list is built backwards from the intended search order.
 * The default is to do searches from the middle row outwards, and
 * from the left to the right columns.  The order can be changed though.
 */
void
initsearchorder()
{
	int	row, col, gen;
	int	count;
	CELL *	cell;
	CELL *	table[MAXCELLS];
	/*
	 * Make a table of cells that will be searched.
	 * Ignore cells that are not relevant to the search due to symmetry.
	 */
	count = 0;

	for (gen = 0; gen < genmax; gen++) {
		for (col = 1; col <= colmax; col++) {
			for (row = 1; row <= rowmax; row++)	{
				cell = findcell(row, col, gen);
				// cells must be already loaded!!!
				if ((cell->active) && (cell->state == UNK) && (!cell->unchecked))
				{
					table[count++] = findcell(row, col, gen);
				}

			}
		}
	}

	/*
	 * Now sort the table based on our desired search order.
	 */
	qsort((char *) table, count, sizeof(CELL *), ordersortfunc);

	/*
	 * Finally build the search list from the table elements in the
	 * final order.
	 */
	searchlist = NULL;

	while (--count >= 0)
	{
		cell = table[count];
		cell->search = searchlist;
		searchlist = cell;
	}
}

/*
 * Set the state of a cell back to UNK/FREE
 * Proceed through the loop if present
 */

void
rescell(CELL *cell)
{
	CELL *c1;

	if (cell->state == UNK) return;

	--cellcount; // take all loops as a single cell

	c1 = cell;

	if (cell->state == ON) {
// if it was previously ON, we have some more stats to hassle
		do {
			cell->state = UNK;
			cell->free = TRUE;
			if (cell->gen == 0) { // cannot move the test outwards due to looped frozen cells
				--cell->rowinfo->oncount;
				--cell->colinfo->oncount;
				cell->colinfo->sumpos -= cell->row;
				--g0oncellcount;
				if (cell->colinfo->setcount == rowmax) --fullcolumns;
				--cell->colinfo->setcount;
			}

			cell = cell->loop;
		} while (cell != c1);

	} else {
// OFF is a little easier to do
		do {
			cell->state = UNK;
			cell->free = TRUE;
			if (cell->gen == 0) {
				if (cell->colinfo->setcount == rowmax) --fullcolumns;
				--cell->colinfo->setcount;

			}

			cell = cell->loop;
		} while (cell != c1);
	}
}

/*
 * Set the state of a cell to the specified state.
 * The state is either ON or OFF.
 * Returns ERROR if the setting is inconsistent.
 * If the cell is newly set, then it is added to the set table.
 */

BOOL
setcell(CELL *cell, STATE state, BOOL free)
{
	CELL *c1;
	if (cell->state == state)
	{
		DPRINTF4("setcell %d %d %d to state %s already set\n",
			cell->row, cell->col, cell->gen,
			(state == ON) ? "on" : "off");

		return TRUE;
	}

	if (cell->state != UNK)
	{
		DPRINTF4("setcell %d %d %d to state %s inconsistent\n",
			cell->row, cell->col, cell->gen,
			(state == ON) ? "on" : "off");

		return FALSE;
	}

	c1 = cell;

	if (state == ON) {
		// setting state ON
		// first let's examine the stats
		do {
			if (cell->gen == 0) {
				if ((usecol != 0)
					&& (colinfo[usecol].oncount == 0)
					&& (colinfo[usecol].setcount == rowmax) && inited)
				{
					return FALSE;
				}

				if ((maxcount != 0) && (g0oncellcount >= maxcount))
				{
					return FALSE;
				}

				if (nearcols && (cell->near1 <= 0) && (cell->col > 1)
					&& inited)
				{
					return FALSE;
				}

				if (colcells && (cell->colinfo->oncount >= colcells)
					&& inited)
				{
					return FALSE;
				}

				if (colwidth && inited && checkwidth(cell))
					return FALSE;

				if (nearcols) adjustnear(cell, 1);

				cell->rowinfo->oncount++;

				cell->colinfo->oncount++;

				cell->colinfo->setcount++;

				if (cell->colinfo->setcount == rowmax) fullcolumns++;

				cell->colinfo->sumpos += cell->row;

				g0oncellcount++;
			}

			cell->state = ON;
			cell->free = free;

			if (cell->active) {
				*newset++ = cell;

				*searchset++ = searchlist;

				while ((searchlist != NULL) && (searchlist->state != UNK)) {
					searchlist = searchlist->search;
				}

				free = FALSE; // all following cells in the loop are not free

			}

			cell = cell->loop;
		} while (c1 != cell);
	} else {
		// setting state OFF is somewhat easier
		do {
			if (cell->gen == 0) {
				if ((usecol != 0)
					&& (colinfo[usecol].oncount == 0)
					&& (colinfo[usecol].setcount == rowmax) && inited)
				{
					return FALSE;
				}

				cell->colinfo->setcount++;

				if (cell->colinfo->setcount == rowmax) fullcolumns++;
			}

			cell->state = OFF;
			cell->free = free;

			if (cell->active) {
				*newset++ = cell;

				*searchset++ = searchlist;

				while ((searchlist != NULL) && (searchlist->state != UNK)) {
					searchlist = searchlist->search;
				}

				free = FALSE;
			}

			cell = cell->loop;
		} while (c1 != cell);
	}

	++cellcount; // take whole loop as a single cell

	return TRUE;
}

static __inline int
sumtodesc(STATE futurestate, STATE currentstate, int neighborsum)
{
	// UNK = 0
	// ON = 1
	// OFF = 9

	// using the following expression, all different
	// combinations are mapped to different numbers
	// if you don't believe it, just try it
	
	return (neighborsum*10 + currentstate*3 + futurestate);
}

/*
 * Calculate the current descriptor for a cell.
 */
static __inline short
getdesc(CELL *cell)
{
	return sumtodesc(cell->future->state, cell->state, 
					cell->cul->state + cell->cu->state + cell->cur->state
					+ cell->cdl->state + cell->cd->state + cell->cdr->state
					+ cell->cl->state + cell->cr->state);
}

/*
 * Consistify a cell.
 * This means examine this cell in the previous generation, and
 * make sure that the previous generation can validly produce the
 * current cell.  Returns FALSE if the cell is inconsistent.
 */
static BOOL consistify(CELL *cell)
{
	CELL *prevcell;
	CELL *neighbor;
	int	desc;
	STATE state;
	FLAGS	flags;

	/*
	 * If we are searching for parents and this is generation 0, then
	 * the cell is consistent with respect to the previous generation.
	 */
	if (parent && (cell->gen == 0))
		return TRUE;

	// Now get the descriptor for the cell, its parent and its parent neighborhood

	prevcell = cell->past;
	desc = getdesc(prevcell);

	// the implic table will tell us everything we need to know

	flags = implic[desc];

	// first check if the state is consistent

	if (flags == IMPBAD) return FALSE;

	// the state is consistent
	// now for the implications

	// change the cell if needed
	if (((flags & IMPN) != 0) &&
		!setcell(cell, ((flags & IMPN1) != 0) ? ON : OFF, FALSE)) return FALSE;

	// change the parent cell if needed
	if (((flags & IMPC) != 0) &&
		!setcell(prevcell, ((flags & IMPC1) != 0) ? ON : OFF, FALSE)) return FALSE;

	if ((flags & IMPUN) != 0) {
		// let's change the parent neighborhood
		state = ((flags & IMPUN1) != 0) ? ON : OFF;

		neighbor = prevcell->cul;
		if ((neighbor->state == UNK) &&
			!setcell(neighbor, state, FALSE)) return FALSE;

		neighbor = prevcell->cu;
		if ((neighbor->state == UNK) &&
			!setcell(neighbor, state, FALSE)) return FALSE;

		neighbor = prevcell->cur;
		if ((neighbor->state == UNK) &&
			!setcell(neighbor, state, FALSE)) return FALSE;

		neighbor = prevcell->cr;
		if ((neighbor->state == UNK) &&
			!setcell(neighbor, state, FALSE)) return FALSE;

		neighbor = prevcell->cdr;
		if ((neighbor->state == UNK) &&
			!setcell(neighbor, state, FALSE)) return FALSE;

		neighbor = prevcell->cd;
		if ((neighbor->state == UNK) &&
			!setcell(neighbor, state, FALSE)) return FALSE;

		neighbor = prevcell->cdl;
		if ((neighbor->state == UNK) &&
			!setcell(neighbor, state, FALSE)) return FALSE;

		neighbor = prevcell->cl;
		if ((neighbor->state == UNK) &&
			!setcell(neighbor, state, FALSE)) return FALSE;
	}

	DPRINTF0("Implications successful\n");

	return TRUE;
}


/*
 * See if a cell and its neighbors are consistent with the cell and its
 * neighbors in the next generation.
 */
static BOOL
consistify10(CELL *cell)
{
	if (!consistify(cell))
		return FALSE;

	cell = cell->future;

	return consistify(cell)
		   && consistify(cell->cul)
		   && consistify(cell->cu)
		   && consistify(cell->cur)
		   && consistify(cell->cl)
		   && consistify(cell->cr)
		   && consistify(cell->cdl)
		   && consistify(cell->cd)
		   && consistify(cell->cdr);
}


/*
 * Examine the next choice of cell settings.
 */
STATUS
examinenext()
{
	CELL *	cell;

	/*
	 * If there are no more cells to examine, then what we have
	 * is consistent.
	 */
	if (nextset == newset)
		return CONSISTENT;

	/*
	 * Get the next cell to examine, and check it out for symmetry
	 * and for consistency with its previous and next generations.
	 */
	cell = *nextset++;

	DPRINTF4("Examining saved cell %d %d %d (%s) for consistency\n",
		cell->row, cell->col, cell->gen,
		(cell->free ? "free" : "forced"));

	return consistify10(cell) ? OK : ERROR1;
}


/*
 * Set a cell to the specified value and determine all consequences we
 * can from the choice.  Consequences are a contradiction or a consistency.
 */
BOOL
proceed(cell, state, free)
	CELL *	cell;
	STATE	state;
	BOOL	free;
{
	int	status;

	if (!setcell(cell, state, free))
		return FALSE;

	do {
		status = examinenext();
	} while (status == OK);

	return (status == CONSISTENT);
}


/*
 * Back up the list of set cells to undo choices.
 * Returns the cell which is to be tried for the other possibility.
 * Returns NULL on an "object cannot exist" error.
 */
CELL *
backup()
{
	CELL *	cell;

	// first let's find how far to backup

	nextset = newset;

	while (nextset != settable)
	{
		cell = *--nextset;
		--searchset;

		if (!cell->free) continue;

		// free cell found
		// record old status
		prevstate = cell->state;

		searchlist = *searchset;

		// reset the stack and return the cell
		while (newset != nextset) {
			rescell(*--newset);
		}

		return cell;
	}

	// free cell not found
	// let's reset the stack anyway

	while (newset != nextset) {
		rescell(*--newset);
	}

	return NULL;
}


/*
 * Do checking based on setting the specified cell.
 * Returns ERROR if an inconsistency was found.
 */
BOOL
go(CELL *cell, STATE state, BOOL free)
{
	CELL ** setpos;

	for (;;)
	{

		setpos = nextset;

		if (proceed(cell, state, free)) return TRUE;

		if (setpos == nextset) 
		{
			// no cell added to stack
			// no backup required
			// but prevstate is not defined now
			state = (ON + OFF) - state;
		} else {
			cell = backup();

			if (cell == NULL) return FALSE;

			state = (ON + OFF) - prevstate;
		}
		free = FALSE;
	}
}


/*
 * Find another unknown cell in a normal search.
 * Returns NULL if there are no more unknown cells.
 */
static CELL *
getnormalunknown()
{
	CELL *	cell;

	for (cell = searchlist; cell != NULL; cell = cell->search)
	{
		if (cell->state == UNK)
		{
			searchlist = cell;

			return cell;
		}
	}

	return NULL;
}

/*
 * Find another unknown cell when averaging is done.
 * Returns NULL if there are no more unknown cells.
 */

static CELL *
getaverageunknown()
{
	CELL *	cell;
	CELL *	bestcell;
	int	bestdist;
	int	curdist;
	int	wantrow;
	int	curcol;
	int	testcol;

	bestcell = NULL;
	bestdist = -1;

	cell = searchlist;

	while (cell)
	{
		searchlist = cell;
		curcol = cell->col;

		testcol = curcol - 1;

		while ((testcol > 0) && (colinfo[testcol].oncount <= 0))
			testcol--;

		if (testcol > 0)
		{
			wantrow = colinfo[testcol].sumpos /
				colinfo[testcol].oncount;
		}
		else
			wantrow = (rowmax + 1) / 2;

		for (; (cell != NULL) && (cell->col == curcol); cell = cell->search)
		{
			if (cell->state == UNK)
			{
				curdist = cell->row - wantrow;

				if (curdist < 0)
					curdist = -curdist;

				if (curdist > bestdist)
				{
					bestcell = cell;
					bestdist = curdist;
				}
			}
		}

		if (bestcell)
			return bestcell;
	}

	return NULL;
}

// calculate how many cells will change
// if we change the current cell to ON or OFF
// set smartlen1 and smartlen0 to appropriate numbers
// return the sum

static int getsmartnumbers(CELL *cell)
{
	int cellno;
	CELL ** setpos;

	// known and inactive cells are unimportant
	if (cell->state != UNK) return 2;

	// remember set position for proper backup
	setpos = newset;

	// remember cell count to calculate the change
	cellno = cellcount;

	// test the cell
	if (proceed(cell, ON, TRUE))
	{
		smartlen1 = cellcount - cellno;

		// back up
		backup(); 

		// and now let's try the OFF choice

		if (proceed(cell, OFF, TRUE))
		{
			smartlen0 = cellcount - cellno;

			// back up
			backup();

		} else {
			// OFF state inconsistent
			// makes a good candidate
			smartlen0 = MAXCELLS;

			// back up if something changed
			if (setpos != newset) backup();
		}

	} else {
		// ON state inconsistent
		// it's actually a good candidate
		smartlen1 = MAXCELLS;
		// no need to check the OFF choice
		smartlen0 = 1;

		// back up if something changed
		if (setpos != newset) backup();

	}

	return smartlen0 + smartlen1;
}

// Smart cell ordering

static CELL *
getsmartunknown()
{
	CELL *cell;
	CELL *best;
	int max1, max, curr, window, threshold, bestlen1, bestlen0, wnd;

	// Move the searchlist over all known cells
	while ((searchlist != NULL) && (searchlist->state != UNK)) {
		searchlist = searchlist->search;
	}

	// Return NULL if no unknown cells
	if (searchlist == NULL) return NULL;

	// Let's start here
	cell = searchlist;

	// Prepare window size
	window = smartwindow;
	if (window <= 0) window = MAXCELLS;

	// Prepare threshold
	threshold = smartthreshold;
	if (threshold <= 0) threshold = MAXCELLS;

	// Prepare the dummy maximum
	max = 3; // at least 3 cells must change
	max1 = -1;
	best = NULL;

	wnd = 0;

	while ((cell != NULL) && (window > 0) && (max < threshold)) {
		++wnd;
		--window; // count known cells too
		if (cell->state == UNK) { // process unknown cells only
			curr = getsmartnumbers(cell); // get the changes
			if (curr >= max) { // most often we don't get the maximum
///				if ((curr > max) || (smartlen1 > max1)) {
				if ((curr > max) || (smartlen0 > max1)) {
					max = curr; // store the new maximum
///					max1 = smartlen1;
					max1 = smartlen0;
					bestlen1 = smartlen1; // store the changes
					bestlen0 = smartlen0; // to propose the first state to try
					best = cell; // and of course store the best cell
				}
			}
		}
		cell = cell->search;
	}

	// Found something?
	if (best != NULL) {

		smartstatsumwnd += wnd;
		++smartstatsumwndc;

		if (MAXCELLS > max) {
			smartstatsumlen += max;
			++smartstatsumlenc;
		}

		if (smartstatsumwndc >= 100000) {
			smartstatwnd = smartstatsumwnd / smartstatsumwndc;
			smartstatsumwnd = 0;
			smartstatsumwndc = 0;
			if (smartstatsumlenc >= 10000) {
				smartstatlen = smartstatsumlen / smartstatsumlenc;
				smartstatsumlen = 0;
				smartstatsumlenc = 0;
			}
		}

		// propose the better way
		smartchoice = (bestlen1 > bestlen0) ? ON : OFF;
		//smartchoice = UNK;
		return best;
	}

	// fall back to standard
	smartchoice = UNK;

	// Just return the first UNK cell
	// This shouldn't be often anyway

	return searchlist;
}

/*
 * Choose a state for an unknown cell, either OFF or ON.
 * Normally, we try to choose OFF cells first to terminate an object.
 * But for follow generations mode, we try to choose the same setting
 * as a nearby generation.
 */

static STATE
choose(cell)
	CELL *	cell;
{
	/* 
	 * if something pre-set by the select algorithm,
	 * use the selection
	 */

	if (smartchoice != UNK) return smartchoice;

	/*
	 * If we are following cells in other generations,
	 * then try to do that.
	 */

	if (followgens)
	{
		if ((cell->past->state == ON) ||
			(cell->future->state == ON))
		{
			return ON;
		}

	}

	/* 
	 * In all other cases
	 * try the OFF state first
	 */

	return OFF;
}

/*
 * The top level search routine.
 * Returns if an object is found, or is impossible.
 */
STATUS
search()
{
	CELL *	cell;
	BOOL	free;
	BOOL	needwrite;
	STATE	state;

 	cell = (*getunknown)();

	if (cell == NULL)
	{
		// nothing to search
		// so we are at a solution
		// let's start search for another one

		cell = backup();

		if (cell == NULL)
			return ERROR1;

		free = FALSE;
		state = (ON + OFF) - prevstate;

	} else {

		state = choose(cell);
		free = TRUE;

	}

	for (;;) {
		if(abortthread) 
		{
			return OK;
		}

		// Set the state of the new cell.

		if (!go(cell, state, free)) 
		{
			showcount();
			printgen();

			return NOTEXIST;
		}


		// If it is time to dump our state, then do that.

		if (dumpfreq && (++dumpcount >= dumpfreq))
		{
			dumpcount = 0;
			dumpstate(dumpfile, FALSE);
		}


		// If we have enough columns found, then remember to
		// write it to the output file.  Also keep the last
		// columns count values up to date.

		needwrite = FALSE;

		if (outputcols &&
			(fullcolumns >= outputlastcols + outputcols))
		{
			outputlastcols = fullcolumns;
			needwrite = TRUE;
		}

		if (outputlastcols > fullcolumns)
			outputlastcols = fullcolumns;

		// If it is time to view the progress,then show it.

		if (needwrite || (viewfreq && (++viewcount >= viewfreq)))
		{
			showcount();
			printgen();
		}

		// Write the progress to the output file if needed.
		// This is done after viewing it so that the write
		// message will stay visible for a while.

		if (needwrite)
		{
			writegen(outputfile, TRUE);
		}


		// Get the next unknown cell and choose its state.

		cell = (*getunknown)();

		if (cell == NULL)
			return FOUND;

		if (stoponstep) {
			showcount();
			printgen();
			abortthread = 1;
			return OK;
		}

		state = choose(cell);
		free = TRUE;
	}
}


/*
 * Increment or decrement the near count in all the cells affected by
 * this cell.  This is done for all cells in the next columns which are
 * within the distance specified the nearcols value.  In this way, a
 * quick test can be made to see if a cell is within range of another one.
 */

void
adjustnear(CELL *cell, int inc)
{
	CELL *	curcell;
	int	count;
	int	colcount;

	for (colcount = nearcols; colcount > 0; colcount--)
	{
		cell = cell->cr;
		curcell = cell;

		for (count = nearcols; count-- >= 0; curcell = curcell->cu)
			curcell->near1 += inc;

		curcell = cell->cd;

		for (count = nearcols; count-- > 0; curcell = curcell->cd)
			curcell->near1 += inc;
	}
}


/*
 * Check to see if setting the specified cell ON would make the width of
 * the column exceed the allowed value.  For symmetric objects, the width
 * is only measured from the center to an edge.  Returns TRUE if the cell
 * would exceed the value.
 */

static BOOL
checkwidth(cell)
	CELL *	cell;
{
	int	left;
	int	width;
	int	minrow;
	int	maxrow;
	int	srcminrow;
	int	srcmaxrow;
	CELL *	ucp;
	CELL *	dcp;
	BOOL	full;

	if (!colwidth || !inited || cell->gen)
		return FALSE;

	left = cell->colinfo->oncount;

	if (left <= 0)
		return FALSE;

	ucp = cell;
	dcp = cell;
	width = colwidth;
	minrow = cell->row;
	maxrow = cell->row;
	srcminrow = 1;
	srcmaxrow = rowmax;
	full = TRUE;

	if ((rowsym && (cell->col >= rowsym)) ||
		(fliprows && (cell->col >= fliprows)))
	{
		full = FALSE;
		srcmaxrow = (rowmax + 1) / 2;

		if (cell->row > srcmaxrow)
		{
			srcminrow = (rowmax / 2) + 1;
			srcmaxrow = rowmax;
		}
	}

	while (left > 0)
	{
		if (full && (--width <= 0))
			return TRUE;

		ucp = ucp->cu;
		dcp = dcp->cd;

		if (ucp->state == ON)
		{
			if (ucp->row >= srcminrow)
				minrow = ucp->row;

			left--;
		}

		if (dcp->state == ON)
		{
			if (dcp->row <= srcmaxrow)
				maxrow = dcp->row;

			left--;
		}
	}

	if (maxrow - minrow >= colwidth)
		return TRUE;

	return FALSE;
}


/*
 * Check to see if any other generation is identical to generation 0.
 * This is used to detect and weed out all objects with subperiods.
 * (For example, stable objects or period 2 objects when using -g4.)
 * Returns TRUE if there is an identical generation.
 */
BOOL
subperiods()
{
	int	row;
	int	col;
	int	gen;
	CELL *	cellg0;
	CELL *	cellgn;

	for (gen = 1; gen < genmax; gen++)
	{
		if (genmax % gen)
			continue;

		for (row = 1; row <= rowmax; row++)
		{
			for (col = 1; col <= colmax; col++)
			{
				cellg0 = findcell(row, col, 0);
				cellgn = findcell(row, col, gen);

				if (cellg0->state != cellgn->state)
					goto nextgen;
			}
		}

		return TRUE;
nextgen:;
	}

	return FALSE;
}


/*
 * Return the mapping of a cell from the last generation back to the first
 * generation, or vice versa.  This implements all flipping and translating
 * of cells between these two generations.  This routine should only be
 * called for cells belonging to those two generations.
 */
static CELL *
mapcell(cell)
	CELL *	cell;
{
	int	row;
	int	col;
	int	tmp;
	BOOL	forward;

	row = cell->row;
	col = cell->col;
	forward = (cell->gen != 0);

	if (fliprows && (col >= fliprows))
		row = rowmax + 1 - row;

	if (flipcols && (row >= flipcols))
		col = colmax + 1 - col;

	if (flipquads)
	{				/* NEED TO GO BACKWARDS */
		tmp = col;
		col = row;
		row = colmax + 1 - tmp;
	}

	if (forward)
	{
		row += rowtrans;
		col += coltrans;
	}
	else
	{
		row -= rowtrans;
		col -= coltrans;
	}

	if (forward)
		return findcell(row, col, 0);
	else
		return findcell(row, col, genmax - 1);
}


/*
 * Make the two specified cells belong to the same loop.
 * If the two cells already belong to loops, the loops are joined.
 * This will force the state of these two cells to follow each other.
 * Symmetry uses this feature, and so does setting stable cells.
 * If any cells in the loop are frozen, then they all are.
 */
void loopcells(CELL *cell1, CELL *cell2)
{
	CELL *	cell;
	BOOL	frozen;

	if (cell2==NULL) return;

	if (cell1 == cell2) return;

	/*
	 * See if the second cell is already part of the first cell's loop.
	 * If so, they they are already joined.  We don't need to
	 * check the other direction.
	 */
	for (cell = cell1->loop; cell != cell1; cell = cell->loop)
	{
		if (cell == cell2) return;
	}

	/*
	 * The two cells belong to separate loops.
	 * Break each of those loops and make one big loop from them.
	 */
	cell = cell1->loop;
	cell1->loop = cell2->loop;
	cell2->loop = cell;

	/*
	 * See if any of the cells in the loop are frozen.
	 * If so, then mark all of the cells in the loop frozen
	 * since they effectively are anyway.  This lets the
	 * user see that fact.
	 */
	frozen = cell1->frozen;

	for (cell = cell1->loop; cell != cell1; cell = cell->loop)
	{
		if (cell->frozen)
			frozen = TRUE;
	}

	if (frozen)
	{
		cell1->frozen = TRUE;

		for (cell = cell1->loop; cell != cell1; cell = cell->loop)
			cell->frozen = TRUE;
	}
}


/*
 * Return a cell which is symmetric to the given cell.
 * It is not necessary to know all symmetric cells to a single cell,
 * as long as all symmetric cells are chained in a loop.  Thus a single
 * pointer is good enough even for the case of both row and column symmetry.
 * Returns NULL if there is no symmetry.
 */


static CELL *symcell(CELL *cell)
{
	int	row;
	int	col;
	int	nrow;
	int	ncol;

	if(!symmetry)
		return NULL;

	row = cell->row;
	col = cell->col;
	nrow = rowmax + 1 - row;
	ncol = colmax + 1 - col;

	if(symmetry==1)  // col sym
		return findcell(row,ncol,cell->gen);

	if(symmetry==2) { // row sym
		return findcell(nrow,col,cell->gen);
	}

	if(symmetry==3)       // fwd diag
		return findcell(ncol,nrow,cell->gen);

	if(symmetry==4)    {   // bwd diag
		return findcell(col,row,cell->gen);
	}

	if(symmetry==5)       // origin
		return findcell(nrow, ncol, cell->gen);

	if(symmetry==6) {
		/*
		 * Here is there is both row and column symmetry.
		 * First see if the cell is in the middle row or middle column,
		 * and if so, then this is easy.
		 */
		if ((nrow == row) || (ncol == col))
			return findcell(nrow, ncol, cell->gen);

		/*
		 * The cell is really in one of the four quadrants, and therefore
		 * has four cells making up the symmetry.  Link this cell to the
		 * symmetrical cell in the next quadrant clockwise.
		 */
		if ((row < nrow) == (col < ncol))
			return findcell(row, ncol, cell->gen);  // quadrant 2 or 4
		else
			return findcell(nrow, col, cell->gen);  // quadrant 1 or 3
	}

	if(symmetry==7) {  // diagonal 4-fold
		// if on a diagonal...
		if(row==col || row==ncol)
			return findcell(nrow,ncol,cell->gen);

		// Not on a diagonal.
		if((col<row)==(col<nrow))
			return findcell(col,row,cell->gen);
		else
			return findcell(ncol,nrow,cell->gen);
	}

	if(symmetry==8) {      // origin*4 symmetry 
		// this is surprisingly simple
			return findcell(ncol,row,cell->gen);
	}

	if(symmetry==9) {    // octagonal, this is gonna be tough
		// if on an axis
		if(nrow==row || ncol==col)
			return findcell(ncol,row,cell->gen);
		// if on a diagonal
		if(row==col || row==ncol)
			return findcell(ncol,row,cell->gen);
		if((col>nrow && row<nrow)||(col<nrow && row>nrow)) // octants 1,5
			return findcell(nrow,col,cell->gen);  // flip rows
		if((col<nrow && col>ncol)||(col>nrow && col<ncol)) // 2,6
			return findcell(ncol,nrow,cell->gen);  // fwd diag
		if((col>row && col<ncol)||(col<row && col>ncol))   // 3,7
			return findcell(row,ncol,cell->gen);  // flip cols
		if((col<row && row<nrow)||(col>row && row>nrow))   // 4,8
			return findcell(col,row,cell->gen);   // bwd diag

	}

	return NULL;   // crash if we get here :)
}

/*
 * Link a cell to its eight neighbors in the same generation, and also
 * link those neighbors back to this cell.
 */
static void
linkcell(cell)
	CELL *	cell;
{
	int	row;
	int	col;
	int	gen;
	CELL *	paircell;

	row = cell->row;
	col = cell->col;
	gen = cell->gen;

	paircell = findcell(row - 1, col - 1, gen);
	cell->cul = paircell;
	paircell->cdr = cell;

	paircell = findcell(row - 1, col, gen);
	cell->cu = paircell;
	paircell->cd = cell;

	paircell = findcell(row - 1, col + 1, gen);
	cell->cur = paircell;
	paircell->cdl = cell;

	paircell = findcell(row, col - 1, gen);
	cell->cl = paircell;
	paircell->cr = cell;

	paircell = findcell(row, col + 1, gen);
	cell->cr = paircell;
	paircell->cl = cell;

	paircell = findcell(row + 1, col - 1, gen);
	cell->cdl = paircell;
	paircell->cur = cell;

	paircell = findcell(row + 1, col, gen);
	cell->cd = paircell;
	paircell->cu = cell;

	paircell = findcell(row + 1, col + 1, gen);
	cell->cdr = paircell;
	paircell->cul = cell;
}


/*
 * Find a cell given its coordinates.
 * Most coordinates range from 0 to colmax+1, 0 to rowmax+1, and 0 to genmax-1.
 * Cells within this range are quickly found by indexing into celltable.
 * Cells outside of this range are handled by searching an auxillary table,
 * and are dynamically created as necessary.
 */
CELL *
findcell(row, col, gen)
	int	row;
	int	col;
	int	gen;
{
	CELL *	cell;
	int		i;

	/*
	 * If the cell is a normal cell, then we know where it is.
	 */
	if ((row >= 0) && (row <= rowmax + 1) &&
		(col >= 0) && (col <= colmax + 1) &&
		(gen >= 0) && (gen < genmax))
	{
		return celltable[(col * (rowmax + 2) + row) * genmax + gen];
	}

	/*
	 * See if the cell is already allocated in the auxillary table.
	 */
	for (i = 0; i < auxcellcount; i++)
	{
		cell = auxtable[i];

		if ((cell->row == row) && (cell->col == col) &&
			(cell->gen == gen))
		{
			return cell;
		}
	}

	/*
	 * Need to allocate the cell and add it to the auxillary table.
	 */
	cell = allocatecell();
	cell->row = row;
	cell->col = col;
	cell->gen = gen;
	cell->rowinfo = &dummyrowinfo;
	cell->colinfo = &dummycolinfo;

	auxtable[auxcellcount++] = cell;

	return cell;
}


/*
 * Allocate a new cell.
 * The cell is initialized as if it was a boundary cell.
 */
static CELL *
allocatecell()
{
	CELL *	cell;

	/*
	 * Allocate a new chunk of cells if there are none left.
	 */
	if (newcellcount <= 0)
	{
		newcells = (CELL *) malloc(sizeof(CELL) * ALLOCSIZE);

		if (newcells == NULL)
		{
			wlsError("Cannot allocate cell structure",0);
			exit(1);
		}

		record_malloc(1,(void*)newcells);

		newcellcount = ALLOCSIZE;
	}

	newcellcount--;
	cell = newcells++;

	/*
	 * Fill in the cell as if it was a boundary cell.
	 */
	cell->state = OFF;
	cell->free = FALSE;
	cell->frozen = FALSE;
	cell->active = TRUE;
	cell->unchecked = FALSE;
	cell->gen = -1;
	cell->row = -1;
	cell->col = -1;
	cell->past = cell;
	cell->future = cell;
	cell->cul = cell;
	cell->cu = cell;
	cell->cur = cell;
	cell->cl = cell;
	cell->cr = cell;
	cell->cdl = cell;
	cell->cd = cell;
	cell->cdr = cell;
	cell->loop = cell;

	return cell;
}

/*
 * Initialize the implication table.
 */
static void
initimplic(void)
{
	int	nunk, non, noff, cunk, con, coff, funk, fon, foff, naon, caon, faon, desc;
	BOOL valid, cison, cisoff, fison, fisoff, nison, nisoff;

	for (desc=0; desc<sizeof(implic)/sizeof(implic[0]); desc++) {
		implic[desc] = IMPVOID;
	}

	for (nunk=0; nunk<=8; nunk++) { // unknown neighbors
		for (non=0; non+nunk<=8; non++) { // on neighbors from the known ones
			noff=8-(non+nunk); // off known neighbors
			for (cunk=0; cunk<=1; cunk++) { // unknown cell
				for (con=0; con+cunk<=1; con++) { // on cell
					coff=1-(con+cunk); // off cell
					for (funk=0; funk<=1; funk++) { // unknown future cell
						for (fon=0; fon+funk<=1; fon++) { // on future cell
							foff=1-(fon+funk); // off future cell
							desc = sumtodesc((STATE)(funk*UNK+fon*ON+foff*OFF), (STATE)(cunk*UNK+con*ON+coff*OFF), nunk*UNK+non*ON+noff*OFF);
							if (implic[desc] != IMPVOID) {
								ttystatus("Duplicate descriptor!!!");
								exit(1);
							}
							// here we get all possible descriptors
							// now let's try all possible states for that descriptor
							valid = FALSE; // will change to TRUE if we get at least one valid state
							cison = TRUE; // will change to FALSE if we get a valid state with c cell OFF
							cisoff = TRUE; // will change to FALSE if we get a valid state with c cell ON
							fison = TRUE; // will change to FALSE if we get a valid state with f cell OFF
							fisoff = TRUE; // will change to FALSE if we get a valid state with f cell ON
							nison = TRUE; // will change to FALSE if we get a valid state with one unknown neighbor OFF
							nisoff = TRUE; // will change to FALSE if we get a valid state with one unknown neighbor ON
							for (naon = non; naon <= 8-noff; naon++) { // neighbors
								for (caon = con; caon <= 1-coff; caon++) { // try center
									for (faon = fon; faon <= 1-foff; faon++) { // try future
										// here we have all possible states for the descriptor
										// now for the rules
										if (((caon == 0) && (faon == 0) && !bornrules[naon]) // both dead
											|| ((caon != 0) && (faon == 0) && !liverules[naon]) // dying
											|| ((caon == 0) && (faon != 0) && bornrules[naon]) // birth
											|| ((caon != 0) && (faon != 0) && liverules[naon])) { // survival
											// woohoo! we got a valid state
											valid = TRUE;
											if (caon == 0) {
												cison = FALSE;
											} else {
												cisoff = FALSE;
											}
											if (faon == 0) {
												fison =  FALSE;
											} else {
												fisoff = FALSE;
											}
											if (naon>non) nisoff = FALSE;
											if (naon<non+nunk) nison = FALSE;
										}
									}
								}
							}
							// descriptor examination has ended
							// now for the results
							if (!valid) {
								implic[desc] = IMPBAD;
							} else {
								implic[desc] = IMPOK;
								if (funk != 0) { // future cell is unknown
									if (fison || fisoff) { // and just one state is possible
										implic[desc] |= IMPN;
										if (fison) {
											implic[desc] |= IMPN1;
										}
									}
								}
								if (cunk != 0) {
									if (cison || cisoff) {
										implic[desc] |= IMPC;
										if (cison) {
											implic[desc] |= IMPC1;
										}
									}
								}
								if (nunk != 0) {
									if (nison || nisoff) {
										implic[desc] |= IMPUN;
										if (nison) {
											implic[desc] |= IMPUN1;
										}
									}
								}
							}
						}
					}
				}
			}
		}
	}

	for (desc=0; desc<sizeof(implic)/sizeof(implic[0]); desc++) {
		if (implic[desc] == IMPVOID) {
			implic[desc] = IMPBAD;
		}
	}

}

/* END CODE */
