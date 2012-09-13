/*
 * Life search program - actual search routines.
 * Author: David I. Bell.
 * Based on the algorithms by Dean Hickerson that were
 * included with the "xlife 2.0" distribution.  Thanks!
 * Changes for arbitrary Life rules by Nathan S. Thompson.

  ****** Heavily modified. Modifications not noted consistently.  -JES ******
 */

#include "wls-config.h"
#include <windows.h>
#include <tchar.h>
#include <search.h>
#include "lifesrc.h"


#define SUMCOUNT 8

extern struct globals_struct g;

extern volatile int abortthread;


#ifdef JS

// Uncommenting this line will disable certain features in an attempt
// to make the search go a little faster. Maybe 10% faster or so...
//#define FASTER


/*
 * IMPLIC flag values.
 */
#define	N0IC0	((FLAGS) 0x01)	/* new cell 0 ==> current cell 0 */
#define	N0IC1	((FLAGS) 0x02)	/* new cell 0 ==> current cell 1 */
#define	N1IC0	((FLAGS) 0x04)	/* new cell 1 ==> current cell 0 */
#define	N1IC1	((FLAGS) 0x08)	/* new cell 1 ==> current cell 1 */
#define	N0ICUN0	((FLAGS) 0x10)	/* new cell 0 ==> current unknown neighbors 0 */
#define	N0ICUN1	((FLAGS) 0x20)	/* new cell 0 ==> current unknown neighbors 1 */
#define	N1ICUN0	((FLAGS) 0x40)	/* new cell 1 ==> current unknown neighbors 0 */
#define	N1ICUN1	((FLAGS) 0x80)	/* new cell 1 ==> current unknown neighbors 1 */

/*
 * Table of state values.
 */
static const STATE g_states[NSTATES] = {OFF, ON, UNK};


/*
 * Local procedures
 */
static	void	inittransit (void);
static	void	initimplic (void);
static	void	initsearchorder (void);
static	void	linkcell (CELL *);
static __inline	STATE	transition (STATE, int, int);
static	STATE	choose (CELL *);
static	FLAGS	implication (STATE, int, int);
static	CELL *	symcell (CELL *);
static	CELL *	mapcell (CELL *);
static	CELL *	allocatecell (void);
static	CELL *	getnormalunknown (void);
static	CELL *	getaverageunknown (void);
static	STATUS	consistify (CELL *);
static	STATUS	consistify10 (CELL *);
static	STATUS	examinenext (void);
static	BOOL	checkwidth (CELL *);
//static	int	getdesc (CELL *);
//static	int	sumtodesc (STATE, int);
//static	int	ordersortfunc (CELL **, CELL **);
static	CELL *	(*getunknown) (void);
static	__inline STATE	nextstate (STATE, int);


/*
 * Return the descriptor value for a cell and the sum of its neighbors.
 */
static __inline int
sumtodesc(STATE state, int sum)
{
	return ((sum & 0x88) ? (sum + state * 2 + 0x11) : (sum * 2 + state));
}


/*
 * Initialize the table of cells.
 * Each cell in the active area is set to unknown state.
 * Boundary cells are set to zero state.
 */
BOOL
initcells(void)
{
	int	row, col, gen;
	int	i;
	STATUS ret;
	BOOL	edge;
	CELL *	cell;
	CELL *	cell2;

	g.newcellcount=0;
	g.auxcellcount=0;
	g.newcells=NULL;
	g.deadcell=NULL;
	g.searchlist=NULL;
	g.dummyrowinfo.oncount=0;
	g.dummycolinfo.oncount=0;


	if ((g.nrows <= 0) || (g.nrows > ROWMAX) ||
		(g.ncols <= 0) || (g.ncols > COLMAX) ||
		(g.period <= 0) || (g.period > GENMAX) ||
		(g.rowtrans < -TRANSMAX) || (g.rowtrans > TRANSMAX) ||
		(g.coltrans < -TRANSMAX) || (g.coltrans > TRANSMAX))
	{
		wlsErrorf(NULL,_T("ROW, COL, GEN, or TRANS out of range"));
		return FALSE;
	}

	/*
	 * The first allocation of a cell MUST be deadcell.
	 * Then allocate the cells in the cell table.
	 */
	g.deadcell = allocatecell();

	for (i = 0; i < MAXCELLS; i++)
		g.celltable[i] = allocatecell();

	/*
	 * Link the cells together.
	 */
	for (col = 0; col <= g.ncols+1; col++)
	{
		for (row = 0; row <= g.nrows+1; row++)
		{
			for (gen = 0; gen < g.period; gen++)
			{
				edge = ((row == 0) || (col == 0) ||
					(row > g.nrows) || (col > g.ncols));

				cell = findcell(row, col, gen);
				cell->gen = gen;
				cell->row = row;
				cell->col = col;
				cell->choose = TRUE;
				cell->rowinfo = &g.dummyrowinfo;
				cell->colinfo = &g.dummycolinfo;

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
					(gen+g.period-1) % g.period);

				cell->future = findcell(row, col,
					(gen+1) % g.period);

				/*
				 * If this is not an edge cell, and
				 * there is some symmetry, then put
				 * this cell in the same loop as the
				 * next symmetrical cell.
				 */
//				if ((rowsym || colsym || pointsym ||
//					fwdsym || bwdsym) && !edge)
				if(g.symmetry)
				{
					ret = loopcells(cell, symcell(cell));
					if(!ret) return FALSE;
				}
			}
		}
	}

	/*
	 * If there is a non-standard mapping between the last generation
	 * and the first generation, then change the future and past pointers
	 * to implement it.  This is for translations and flips.
	 */
	if (g.rowtrans || g.coltrans || g.fliprows || g.flipcols || g.flipquads)
	{
		for (row = 0; row <= g.nrows+1; row++)
		{
			for (col = 0; col <= g.ncols+1; col++)
			{
/*				cell = findcell(row, col, genmax - 1);
				cell2 = mapcell(cell);
				cell->future = cell2;
				cell2->past = cell;
				*/

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
	for (row = 1; row <= g.nrows; row++)
	{
		for (col = 1; col <= g.ncols; col++)
		{
			cell = findcell(row, col, 0);
			cell->rowinfo = &g.rowinfo[row];
			cell->colinfo = &g.colinfo[col];
		}
	}

	initsearchorder();

	if (g.follow)
		getunknown = getaverageunknown;
	else
		getunknown = getnormalunknown;

	g.newset = g.settable;
	g.nextset = g.settable;
	g.baseset = g.settable;

	g.curgen = 0;
	g.curstatus = OK;
	inittransit();
	initimplic();
	return TRUE;
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
	 * If we do not order by all generations, then put all of
	 * generation zero ahead of the other generations.
	 */
	if (!g.ordergens)
	{
		if (c1->gen < c2->gen)
			return -1;

		if (c1->gen > c2->gen)
			return 1;
	}

	if(g.diagsort) {
		if(c1->col+c1->row > c2->col+c2->row) return 1;
		if(c1->col+c1->row < c2->col+c2->row) return -1;
		if(abs(c1->col-c1->row) > abs(c2->col-c2->row)) return (g.orderwide)?1:(-1);  /* ??? */
		if(abs(c1->col-c1->row) < abs(c2->col-c2->row)) return (g.orderwide)?(-1):1;
	}
	if(g.knightsort) {
		if(c1->col*2+c1->row > c2->col*2+c2->row) return 1;
		if(c1->col*2+c1->row < c2->col*2+c2->row) return -1;
		if(abs(c1->col-c1->row) > abs(c2->col-c2->row)) return (g.orderwide)?1:(-1);
		if(abs(c1->col-c1->row) < abs(c2->col-c2->row)) return (g.orderwide)?(-1):1;
	}


	/*
	 * Sort on the column number.
	 * By default this is from left to right.
	 * But if middle ordering is set, the ordering is from the center
	 * column outwards.
	 */
	if (g.ordermiddle)
	{
		midcol = (g.ncols + 1) / 2;

		dif1 = c1->col - midcol;

		if (dif1 < 0)
			dif1 = -dif1;

		dif2 = c2->col - midcol;

		if (dif2 < 0)
			dif2 = -dif2;

		if (dif1 < dif2)
			return -1;

		if (dif1 > dif2)
			return 1;
	}
	else {
		/*
		if(c1->row < c1->col) return -1;
		if(c1->row > c1->col) return 1;
		*/
		if (c1->col < c2->col)
			return -1;

		if (c1->col > c2->col)
			return 1;
	}

	/*
	 * Sort on the row number.
	 * By default, this is from the middle row outwards.
	 * But if wide ordering is set, the ordering is from the edge
	 * inwards.  Note that we actually set the ordering to be the
	 * opposite of the desired order because the initial setting
	 * for new cells is OFF.
	 */
	midrow = (g.nrows + 1) / 2;

	dif1 = c1->row - midrow;

	if (dif1 < 0)
		dif1 = -dif1;

	dif2 = c2->row - midrow;

	if (dif2 < 0)
		dif2 = -dif2;

	if (dif1 < dif2)
		return (g.orderwide ? -1 : 1);

	if (dif1 > dif2)
		return (g.orderwide ? 1 : -1);

	/*
	 * Sort by the generation again if we didn't do it yet.
	 */
	if (c1->gen < c2->gen)
		return -1;

	if (c1->gen > c2->gen)
		return 1;

	return 0;
}


/*
 * Order the cells to be searched by building the search table list.
 * This list is built backwards from the intended search order.
 * The default is to do searches from the middle row outwards, and
 * from the left to the right columns.  The order can be changed though.
 */
static void
initsearchorder(void)
{
	int	row, col, gen;
	int	count;
	CELL *	cell;
	CELL *	table[MAXCELLS];
	int nrow,ncol;
	/*
	 * Make a table of cells that will be searched.
	 * Ignore cells that are not relevant to the search due to symmetry.
	 */
	count = 0;

	for (gen = 0; gen < g.period; gen++)
		for (col = 1; col <= g.ncols; col++)
			for (row = 1; row <= g.nrows; row++)	{

				nrow=g.nrows+1-row;
				ncol=g.ncols+1-col;

				switch(g.symmetry) {
				case 1:
					if(col>ncol) continue;
					break;
				case 2:
					if(row<nrow) continue;
					break;
				case 3:
					if(col>nrow) continue;
					break;
				case 4:
					if(col>row) continue;
					break;
				case 5:
					if(col>ncol) continue;
					if(col==ncol && row>nrow) continue;
					break;
				case 6:
					if(col>ncol || row>nrow) continue;
					break;
				case 7:
					if(col>nrow || col>row) continue;
					break;
				case 8:
					if(col==ncol && row==nrow) break;  // make sure we keep the center
//					if(col<ncol || row<=nrow) continue;
					if(col>ncol || row>=nrow) continue;
					break;
				case 9:
//					if(col<row || row<nrow) continue;
					if(col>row || row>nrow) continue;
					break;
				}


/*				if (rowsym && (row * 2 > rowmax + 1))
					continue;

				if (colsym && (col * 2 > colmax + 1))
					continue;
					*/

				table[count++] = findcell(row, col, gen);

			}

	/*
	 * Now sort the table based on our desired search order.
	 */
	qsort((char *) table, count, sizeof(CELL *), ordersortfunc);

	/*
	 * Finally build the search list from the table elements in the
	 * final order.
	 */
	g.searchlist = NULL;

	while (--count >= 0)
	{
		cell = table[count];
		cell->search = g.searchlist;
		g.searchlist = cell;
	}

	g.fullsearchlist = g.searchlist;
}




/*
 * Set the state of a cell to the specified state.
 * The state is either ON or OFF.
 * Returns FALSE if the setting is inconsistent.
 * If the cell is newly set, then it is added to the set table.
 */
BOOL
setcell(cell, state, free)
	CELL *	cell;
	STATE	state;
	BOOL	free;
{
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

	if (cell->gen == 0)
	{
		if (g.usecol && (g.colinfo[g.usecol].oncount == 0)
			&& (g.colinfo[g.usecol].setcount == g.nrows) && g.inited)
		{
			return FALSE;
		}

		if (state == ON)
		{

#ifndef FASTER

			if (g.maxcount && (g.cellcount >= g.maxcount))
			{
				DPRINTF2("setcell %d %d 0 on exceeds maxcount\n",
					cell->row, cell->col);

				return FALSE;
			}

			if (g.nearcols && (cell->near1 <= 0) && (cell->col > 1)
				&& g.inited)
			{
				return FALSE;
			}

			if (g.colcells && (cell->colinfo->oncount >= g.colcells)
				&& g.inited)
			{
				return FALSE;
			}

			if (g.colwidth && g.inited && checkwidth(cell))
				return FALSE;

			if (g.nearcols)
				adjustnear(cell, 1);
#endif

			cell->rowinfo->oncount++;
			cell->colinfo->oncount++;
			cell->colinfo->sumpos += cell->row;
			g.cellcount++;
		}
	}

	DPRINTF5("setcell %d %d %d to %s, %s successful\n",
		cell->row, cell->col, cell->gen,
		(free ? "free" : "forced"), ((state == ON) ? "on" : "off"));

	*g.newset++ = cell;

	cell->state = state;
	cell->free = free;
	cell->colinfo->setcount++;

	if ((cell->gen == 0) && (cell->colinfo->setcount == g.nrows))
		g.fullcolumns++;

	return TRUE;
}


/*
 * Calculate the current descriptor for a cell.
 */
static __inline int
getdesc(cell)
	CELL *	cell;
{
	int	sum;
/*
	switch(cell->specsym) {
	case 0:	sum = cell->cul->state + cell->cu->state + cell->cur->state +
	      cell->cdl->state + cell->cd->state + cell->cdr->state +
	      cell->cl->state + cell->cr->state;
		break;

	case 1:  // orthogonal, odd symmetry

		sum = 2*(cell->cdl->state + cell->cd->state + cell->cdr->state) +
			  cell->cl->state + cell->cr->state;
		break;
	case 2:      // orthogonal, even symmetry      ????
		sum = cell->cdl->state + cell->cd->state + cell->cdr->state +
			  2*(cell->cl->state + cell->cr->state)
			  +cell->state;
		break;


	default:
		// should never get here
		exit(1);

	}
	if(cell->row==1) {
		sum = 2*(cell->cdl->state + cell->cd->state + cell->cdr->state) +
			  cell->cl->state + cell->cr->state;
	}
	else {
*/
	sum = cell->cul->state + cell->cu->state + cell->cur->state +
	      cell->cdl->state + cell->cd->state + cell->cdr->state +
	      cell->cl->state + cell->cr->state;
/*	} */


	return ((sum & 0x88) ? (sum + cell->state * 2 + 0x11) :
		(sum * 2 + cell->state));

}




/*
 * Consistify a cell.
 * This means examine this cell in the previous generation, and
 * make sure that the previous generation can validly produce the
 * current cell.  Returns ERROR if the cell is inconsistent.
 */
static STATUS consistify(CELL *cell)
{
	CELL *	prevcell;
	int	desc;
	STATE	state;
	FLAGS	flags;

	/*
	 * If we are searching for parents and this is generation 0, then
	 * the cell is consistent with respect to the previous generation.
	 */
#ifndef FASTER
	if (g.parent && (cell->gen == 0))
		return OK;
#endif

	/*
	 * First check the transit table entry for the previous
	 * generation.  Make sure that this cell matches the ON or
	 * OFF state demanded by the transit table.  If the current
	 * cell is unknown but the transit table knows the answer,
	 * then set the now known state of the cell.
	 */
	prevcell = cell->past;
	desc = getdesc(prevcell);
	state = g.transit[desc];

	if ((state != UNK) && (state != cell->state))
	{
		if (!setcell(cell, state, FALSE))
			return ERROR1;
	}

	/*
	 * Now look up the previous generation in the implic table.
	 * If this cell implies anything about the cell or its neighbors
	 * in the previous generation, then handle that.
	 */
	flags = g.implic[desc];

	if ((flags == 0) || (cell->state == UNK))
		return OK;

	DPRINTF1("Implication flags %x\n", flags);

	if ((flags & N0IC0) && (cell->state == OFF) &&
		!setcell(prevcell, OFF, FALSE))
	{
		return ERROR1;
	}

	if ((flags & N1IC1) && (cell->state == ON) &&
		!setcell(prevcell, ON, FALSE))
	{
		return ERROR1;
	}

	state = UNK;

	if (((flags & N0ICUN0) && (cell->state == OFF))
		|| ((flags & N1ICUN0) && (cell->state == ON)))
	{
		state = OFF;
	}

	if (((flags & N0ICUN1) && (cell->state == OFF))
		|| ((flags & N1ICUN1) && (cell->state == ON)))
	{
		state = ON;
	}

	if (state == UNK)
	{
		DPRINTF0("Implications successful\n");

		return OK;
	}

	/*
	 * For each unknown neighbor, set its state as indicated.
	 * Return an error if any neighbor is inconsistent.
	 */
	DPRINTF4("Forcing unknown neighbors of cell %d %d %d %s\n",
		prevcell->row, prevcell->col, prevcell->gen,
		((state == ON) ? "on" : "off"));

//	if(cell->specsym==0) {
		if ((prevcell->cul->state == UNK) &&
			!setcell(prevcell->cul, state, FALSE)) return ERROR1;

		if ((prevcell->cu->state == UNK) &&
			!setcell(prevcell->cu, state, FALSE)) return ERROR1;

		if ((prevcell->cur->state == UNK) &&
			!setcell(prevcell->cur, state, FALSE)) return ERROR1;
//	}



	if ((prevcell->cl->state == UNK) &&
		!setcell(prevcell->cl, state, FALSE)) return ERROR1;

	if ((prevcell->cr->state == UNK) &&
		!setcell(prevcell->cr, state, FALSE)) return ERROR1;

	if ((prevcell->cdl->state == UNK) &&
		!setcell(prevcell->cdl, state, FALSE)) return ERROR1;

	if ((prevcell->cd->state == UNK) &&
		!setcell(prevcell->cd, state, FALSE)) return ERROR1;

	if ((prevcell->cdr->state == UNK) &&
		!setcell(prevcell->cdr, state, FALSE)) return ERROR1;

	DPRINTF0("Implications successful\n");

	return OK;
}


/*
 * See if a cell and its neighbors are consistent with the cell and its
 * neighbors in the next generation.
 */
static STATUS
consistify10(cell)
	CELL *	cell;
{
	if (consistify(cell) != OK)
		return ERROR1;

	cell = cell->future;

	if (consistify(cell) != OK)
		return ERROR1;

//	if(cell->specsym==0) {
		if (consistify(cell->cul) != OK) return ERROR1;
		if (consistify(cell->cu) != OK) return ERROR1;
		if (consistify(cell->cur) != OK) return ERROR1;
//	}

	if (consistify(cell->cl) != OK) return ERROR1;
	if (consistify(cell->cr) != OK) return ERROR1;
	if (consistify(cell->cdl) != OK) return ERROR1;
	if (consistify(cell->cd) != OK) return ERROR1;
	if (consistify(cell->cdr) != OK) return ERROR1;

	return OK;
}


/*
 * Examine the next choice of cell settings.
 */
static STATUS
examinenext(void)
{
	CELL *	cell;

	/*
	 * If there are no more cells to examine, then what we have
	 * is consistent.
	 */
	if (g.nextset == g.newset)
		return CONSISTENT;

	/*
	 * Get the next cell to examine, and check it out for symmetry
	 * and for consistency with its previous and next generations.
	 */
	cell = *g.nextset++;

	DPRINTF4("Examining saved cell %d %d %d (%s) for consistency\n",
		cell->row, cell->col, cell->gen,
		(cell->free ? "free" : "forced"));

	if (cell->loop && !setcell(cell->loop, cell->state, FALSE))
	{
		return ERROR1;
	}

	return consistify10(cell);
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

	for (;;)
	{
		status = examinenext();

		if (status == ERROR1)
			return FALSE;

		if (status == CONSISTENT)
			return TRUE;
	}
}


/*
 * Back up the list of set cells to undo choices.
 * Returns the cell which is to be tried for the other possibility.
 * Returns NULL CELL on an "object cannot exist" error.
 */
CELL *
backup(void)
{
	CELL *	cell;

	g.searchlist = g.fullsearchlist;

	while (g.newset != g.baseset)
	{
		cell = *--g.newset;

		DPRINTF5("backing up cell %d %d %d, was %s, %s\n",
			cell->row, cell->col, cell->gen,
			((cell->state == ON) ? "on" : "off"),
			(cell->free ? "free": "forced"));

		if ((cell->state == ON) && (cell->gen == 0))
		{
			cell->rowinfo->oncount--;
			cell->colinfo->oncount--;
			cell->colinfo->sumpos -= cell->row;
			g.cellcount--;
			adjustnear(cell, -1);
		}

		if ((cell->gen == 0) && (cell->colinfo->setcount == g.nrows))
			g.fullcolumns--;

		cell->colinfo->setcount--;

		if (!cell->free)
		{
			cell->state = UNK;
			cell->free = TRUE;

			continue;
		}

		g.nextset = g.newset;

		return cell;
	}

	g.nextset = g.baseset;
	return NULL;
}


/*
 * Do checking based on setting the specified cell.
 * Returns ERROR if an inconsistency was found.
 */
BOOL
go(cell, state, free)
	CELL *	cell;
	STATE	state;
	BOOL	free;
{
	for (;;)
	{
		if (proceed(cell, state, free)) return TRUE;

		cell = backup();

		if (cell == NULL) return FALSE;

		free = FALSE;
		state = 1 - cell->state;
		cell->state = UNK;
	}
}


/*
 * Find another unknown cell in a normal search.
 * Returns NULL CELL if there are no more unknown cells.
 */
static CELL *
getnormalunknown(void)
{
	CELL *	cell;

	for (cell = g.searchlist; cell; cell = cell->search)
	{
		if (!cell->choose)
			continue;

		if (cell->state == UNK)
		{
			g.searchlist = cell;

			return cell;
		}
	}

	return NULL;
}


/*
 * Find another unknown cell when averaging is done.
 * Returns NULL CELL if there are no more unknown cells.
 */
static CELL *
getaverageunknown(void)
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

	cell = g.searchlist;

	while (cell)
	{
		g.searchlist = cell;
		curcol = cell->col;

		testcol = curcol - 1;

		while ((testcol > 0) && (g.colinfo[testcol].oncount <= 0))
			testcol--;

		if (testcol > 0)
		{
			wantrow = g.colinfo[testcol].sumpos /
				g.colinfo[testcol].oncount;
		}
		else
			wantrow = (g.nrows + 1) / 2;

		for (; cell && (cell->col == curcol); cell = cell->search)
		{
			if (!cell->choose)
				continue;

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
	 * If we are following cells in other generations,
	 * then try to do that.
	 */
	if (g.followgens)
	{
		if ((cell->past->state == ON) ||
			(cell->future->state == ON))
		{
			return ON;
		}

		if ((cell->past->state == OFF) ||
			(cell->future->state == OFF))
		{
			return OFF;
		}
	}

	return OFF;
}


/*
 * The top level search routine.
 * Returns if an object is found, or is impossible.
 */
STATUS
search(void)
{
	CELL *	cell;
	BOOL	free;
	BOOL	needwrite;
	STATE	state;

	cell = (*getunknown)();

	if (cell == NULL)
	{
		cell = backup();

		if (cell == NULL)
			return ERROR1;

		free = FALSE;
		state = 1 - cell->state;
		cell->state = UNK;
	}
	else {
#ifdef FASTER
		state=OFF;
#else
		state = choose(cell);
#endif
		free = TRUE;
	}

	for (;;) {
		if(abortthread) return OK;
		/*
		 * Set the state of the new cell.
		 */
		if (!go(cell, state, free))
			return NOTEXIST;

		/*
		 * If it is time to dump our state, then do that.
		 */
		if (g.dumpfreq && (++g.dumpcount >= g.dumpfreq))
		{
			g.dumpcount = 0;
			dumpstate(NULL, g.dumpfile);
		}

		/*
		 * If we have enough columns found, then remember to
		 * write it to the output file.  Also keep the last
		 * columns count values up to date.
		 */
		needwrite = FALSE;
//		needwrite1=FALSE;

		if (g.outputcols &&
			(g.fullcolumns >= g.outputlastcols + g.outputcols))
		{
			g.outputlastcols = g.fullcolumns;
			needwrite = TRUE;
		}

		if (g.outputlastcols > g.fullcolumns)
			g.outputlastcols = g.fullcolumns;

		/*
		 * If it is time to view the progress,then show it.
		 */
		if (needwrite || (g.viewfreq && (++g.viewcount >= g.viewfreq)))
		{
			wlsUpdateProgressCounter();
			wlsUpdateAndShowTmpField_Sync();
			g.viewcount = 0;
		}

		/*
		 * Write the progress to the output file if needed.
		 * This is done after viewing it so that the write
		 * message will stay visible for a while.
		 */
		if (needwrite)
			wlsWriteCurrentFieldToFile(NULL, g.outputfile, TRUE);

		/*
		 * Check for commands.
		 */
//		if (ttycheck())
//			getcommands();

		/*
		 * Get the next unknown cell and choose its state.
		 */
		cell = (*getunknown)();

		if (cell == NULL)
			return FOUND;

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
adjustnear(cell, inc)
	CELL *	cell;
	int	inc;
{
	CELL *	curcell;
	int	count;
	int	colcount;

	for (colcount = g.nearcols; colcount > 0; colcount--)
	{
		cell = cell->cr;
		curcell = cell;

		for (count = g.nearcols; count-- >= 0; curcell = curcell->cu)
			curcell->near1 += inc;

		curcell = cell->cd;

		for (count = g.nearcols; count-- > 0; curcell = curcell->cd)
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

	if (!g.colwidth || !g.inited || cell->gen)
		return FALSE;

	left = cell->colinfo->oncount;

	if (left <= 0)
		return FALSE;

	ucp = cell;
	dcp = cell;
	width = g.colwidth;
	minrow = cell->row;
	maxrow = cell->row;
	srcminrow = 1;
	srcmaxrow = g.nrows;
	full = TRUE;

	if ((g.rowsym && (cell->col >= g.rowsym)) ||
		(g.fliprows && (cell->col >= g.fliprows)))
	{
		full = FALSE;
		srcmaxrow = (g.nrows + 1) / 2;

		if (cell->row > srcmaxrow)
		{
			srcminrow = (g.nrows / 2) + 1;
			srcmaxrow = g.nrows;
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

	if (maxrow - minrow >= g.colwidth)
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
subperiods(void)
{
	int	row;
	int	col;
	int	gen;
	CELL *	cellg0;
	CELL *	cellgn;

	for (gen = 1; gen < g.period; gen++)
	{
		if (g.period % gen)
			continue;

		for (row = 1; row <= g.nrows; row++)
		{
			for (col = 1; col <= g.ncols; col++)
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

	if (g.fliprows && (col >= g.fliprows))
		row = g.nrows + 1 - row;

	if (g.flipcols && (row >= g.flipcols))
		col = g.ncols + 1 - col;

	if (g.flipquads)
	{				/* NEED TO GO BACKWARDS */
		tmp = col;
		col = row;
		row = g.ncols + 1 - tmp;
	}

	if (forward)
	{
		row += g.rowtrans;
		col += g.coltrans;
	}
	else
	{
		row -= g.rowtrans;
		col -= g.coltrans;
	}

	if (forward)
		return findcell(row, col, 0);
	else
		return findcell(row, col, g.period - 1);
}


/*
 * Make the two specified cells belong to the same loop.
 * If the two cells already belong to loops, the loops are joined.
 * This will force the state of these two cells to follow each other.
 * Symmetry uses this feature, and so does setting stable cells.
 * If any cells in the loop are frozen, then they all are.
 */
BOOL loopcells(CELL *cell1, CELL *cell2)
{
	CELL *	cell;
	BOOL	frozen;

	if(cell2==NULL) return TRUE;

	/*
	 * Check simple cases of equality, or of either cell
	 * being the deadcell.
	 */
	if ((cell1 == g.deadcell) || (cell2 == g.deadcell))
	{
		wlsErrorf(NULL,_T("Attemping to use deadcell in a loop"));
		return FALSE;
	}

	if (cell1 == cell2)
		return TRUE;

	/*
	 * Make the cells belong to their own loop if required.
	 * This will simplify the code.
	 */
	if (cell1->loop == NULL)
		cell1->loop = cell1;

	if (cell2->loop == NULL)
		cell2->loop = cell2;

	/*
	 * See if the second cell is already part of the first cell's loop.
	 * If so, they they are already joined.  We don't need to
	 * check the other direction.
	 */
	for (cell = cell1->loop; cell != cell1; cell = cell->loop)
	{
		if (cell == cell2)
			return TRUE;
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
	return TRUE;
}


/*
 * Return a cell which is symmetric to the given cell.
 * It is not necessary to know all symmetric cells to a single cell,
 * as long as all symmetric cells are chained in a loop.  Thus a single
 * pointer is good enough even for the case of both row and column symmetry.
 * Returns NULL CELL if there is no symmetry.
 */


static CELL *symcell(CELL *cell)
{
	int	row;
	int	col;
	int	nrow;
	int	ncol;

//	if (!rowsym && !colsym && !pointsym && !fwdsym && !bwdsym)
//		return NULL;

	if(!g.symmetry)
		return NULL;

	row = cell->row;
	col = cell->col;
	nrow = g.nrows + 1 - row;
	ncol = g.ncols + 1 - col;

	if(g.symmetry==1)  // col sym
		return findcell(row,ncol,cell->gen);

	if(g.symmetry==2) { // row sym
		if(g.fastsym) {
			if(g.nrows%2) {    // odd sym

				if(abs(nrow-row)==2) {
					return findcell(nrow,col,cell->gen);
				}
				if(row>nrow && cell->gen==0) cell->colinfo->setcount++;

				return NULL;
			}
			else {       // even sym
				if(abs(nrow-row)==1) {
					return findcell(nrow,col,cell->gen);
				}
				if(nrow>row && cell->gen==0) cell->colinfo->setcount++;
				return NULL;
			}

		}
		else {
			return findcell(nrow,col,cell->gen);
		}
	}

	if(g.symmetry==3)       // fwd diag
		return findcell(ncol,nrow,cell->gen);

	if(g.symmetry==4)    {   // bwd diag
		if(g.fastsym) {
			if(abs(col-row)==1)
				return findcell(col,row,cell->gen);
			if(abs(col-row)==2)
				return findcell(col,row,cell->gen);
			return NULL;
		}
		return findcell(col,row,cell->gen);
	}

/*    special symmetry
	if(symmetry==4) {
		if(col>=row)
			return findcell(col,row-1,cell->gen);
		else
			return findcell(col+1,row,cell->gen);

	}
*/
	if(g.symmetry==5)       // origin
		return findcell(nrow, ncol, cell->gen);

	if(g.symmetry==6) {
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

	if(g.symmetry==7) {  // diagonal 4-fold
		// if on a diagonal...
		if(row==col || row==ncol)
			return findcell(nrow,ncol,cell->gen);

		// Not on a diagonal.
		if((col<row)==(col<nrow))
			return findcell(col,row,cell->gen);
		else
			return findcell(ncol,nrow,cell->gen);
	}

	if(g.symmetry==8) {      // origin*4 symmetry
		// this is surprisingly simple
			return findcell(ncol,row,cell->gen);
	}

	if(g.symmetry==9) {    // octagonal, this is gonna be tough
		// if on an axis
		if(nrow==row || ncol==col)
			return findcell(ncol,row,cell->gen);

//			return findcell(nrow, ncol, cell->gen);
		// if on a diagonal
		if(row==col || row==ncol)
			return findcell(ncol,row,cell->gen);
//			return findcell(nrow,ncol,cell->gen);

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
	if ((row >= 0) && (row <= g.nrows + 1) &&
		(col >= 0) && (col <= g.ncols + 1) &&
		(gen >= 0) && (gen < g.period))
	{
		return g.celltable[(col * (g.nrows + 2) + row) * g.period + gen];
	}

	/*
	 * See if the cell is already allocated in the auxillary table.
	 */
	for (i = 0; i < g.auxcellcount; i++)
	{
		cell = g.auxtable[i];

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
	cell->rowinfo = &g.dummyrowinfo;
	cell->colinfo = &g.dummycolinfo;

	g.auxtable[g.auxcellcount++] = cell;

	return cell;
}


/*
 * Allocate a new cell.
 * The cell is initialized as if it was a boundary cell.
 * Warning: The first allocation MUST be of the deadcell.
 */
static CELL *
allocatecell(void)
{
	CELL *	cell;

	/*
	 * Allocate a new chunk of cells if there are none left.
	 */
	if (g.newcellcount <= 0)
	{
		g.newcells = (CELL *) malloc(sizeof(CELL) * ALLOCSIZE);

		if (g.newcells == NULL)
		{
			wlsErrorf(NULL,_T("Cannot allocate cell structure"));
			exit(1);
		}

		record_malloc(1,(void*)g.newcells);

		g.newcellcount = ALLOCSIZE;
	}

	g.newcellcount--;
	cell = g.newcells++;

	/*
	 * If this is the first allocation, then make deadcell be this cell.
	 */
	if (g.deadcell == NULL)
		g.deadcell = cell;

	/*
	 * Fill in the cell as if it was a boundary cell.
	 */
	cell->state = OFF;
	cell->free = FALSE;
	cell->frozen = FALSE;
	cell->choose = TRUE;
	cell->gen = -1;
	cell->row = -1;
	cell->col = -1;
	cell->past = g.deadcell;
	cell->future = g.deadcell;
	cell->cul = g.deadcell;
	cell->cu = g.deadcell;
	cell->cur = g.deadcell;
	cell->cl = g.deadcell;
	cell->cr = g.deadcell;
	cell->cdl = g.deadcell;
	cell->cd = g.deadcell;
	cell->cdr = g.deadcell;
	cell->loop = NULL;

	//cell->specsym=0;

	return cell;
}


/*
 * Initialize the implication table.
 */
static void
initimplic(void)
{
	STATE	state;
	int	OFFcount;
	int	ONcount;
	int	sum;
	int	desc;
	int	i;

	for (i = 0; i < NSTATES; i++)
	{
		state = g_states[i];

		for (OFFcount = SUMCOUNT; OFFcount >= 0; OFFcount--)
		{
			for (ONcount = 0; ONcount + OFFcount <= SUMCOUNT; ONcount++)
			{
				sum = ONcount + (SUMCOUNT - ONcount - OFFcount) * UNK;
				desc = sumtodesc(state, sum);

				g.implic[desc] =
					implication(state, OFFcount, ONcount);
			}
		}
	}
}


/*
 * Initialize the transition table.
 */
static void
inittransit(void)
{
	int	state;
	int	OFFcount;
	int	ONcount;
	int	sum;
	int	desc;
	int	i;

	for (i = 0; i < NSTATES; i++)
	{
		state = g_states[i];

		for (OFFcount = SUMCOUNT; OFFcount >= 0; OFFcount--)
		{
			for (ONcount = 0; ONcount + OFFcount <= SUMCOUNT; ONcount++)
			{
				sum = ONcount + (SUMCOUNT - ONcount - OFFcount) * UNK;
				desc = sumtodesc((STATE)state, sum);

				g.transit[desc] =
					transition(state, OFFcount, ONcount);
			}
		}
	}
}


/*
 * Return the next state if all neighbors are known.
 */
static __inline STATE
nextstate(state, ONcount)
	STATE	state;
	int	ONcount;
{
	switch (state)
	{
		case ON:
			return g.liverules[ONcount];

		case OFF:
			return g.bornrules[ONcount];

		case UNK:
			if (g.bornrules[ONcount] == g.liverules[ONcount])
				return g.bornrules[ONcount];

			/* fall into default case */

		default:
			return UNK;
	}
}


/*
 * Determine the transition of a cell depending on its known neighbor counts.
 * The unknown neighbor count is implicit since there are eight neighbors.
 */
static __inline STATE
transition(state, OFFcount, ONcount)
	STATE	state;
	int	OFFcount;
	int	ONcount;
{
	BOOL	on_always;
	BOOL	off_always;
	int	UNKcount;
	int	i;

	on_always = TRUE;
	off_always = TRUE;
	UNKcount = SUMCOUNT - OFFcount - ONcount;

	for (i = 0; i <= UNKcount; i++)
	{
		switch (nextstate(state, ONcount + i))
		{
			case ON:
				off_always = FALSE;
				break;

			case OFF:
				on_always = FALSE;
				break;

			default:
				return UNK;
		}
	}

	if (on_always)
		return ON;

	if (off_always)
		return OFF;

	return UNK;
}


/*
 * Determine the implications of a cell depending on its known neighbor counts.
 * The unknown neighbor count is implicit since there are eight neighbors.
 */
static FLAGS
implication(state, OFFcount, ONcount)
	STATE	state;
	int	OFFcount;
	int	ONcount;
{
	FLAGS	flags;
	STATE	next;
	int	UNKcount;
	int	i;

	UNKcount = SUMCOUNT - OFFcount - ONcount;
	flags = 0;

	if (state == UNK)
	{
		flags |= (N0IC0 | N0IC1 | N1IC0 | N1IC1);   /* set them all and */

		for (i = 0; i <= UNKcount; i++)
		{			/* look for contradictions */
			next = nextstate(OFF, ONcount + i);

			if (next == ON)
				flags &= ~N1IC1;
			else if (next == OFF)
				flags &= ~N0IC1;

			next = nextstate(ON, ONcount + i);

			if (next == ON)
				flags &= ~N1IC0;
			else if (next == OFF)
				flags &= ~N0IC0;
		}
	}

	if (UNKcount)
	{
		flags |= (N0ICUN0 | N0ICUN1 | N1ICUN0 | N1ICUN1);

		if ((state == OFF) || (state == UNK))
		{
			next = nextstate(OFF, ONcount);    /* try unknowns zero */

			if (next == ON)
				flags &= ~N1ICUN1;
			else if (next == OFF)
				flags &= ~N0ICUN1;

			next = nextstate(OFF, ONcount + UNKcount);   /* try all ones */

			if (next == ON)
				flags &= ~N1ICUN0;
			else if (next == OFF)
				flags &= ~N0ICUN0;
		}

		if ((state == ON) || (state == UNK))
		{
			next = nextstate(ON, ONcount);    /* try unknowns zero */

			if (next == ON)
				flags &= ~N1ICUN1;
			else if (next == OFF)
				flags &= ~N0ICUN1;

			next = nextstate(ON, ONcount + UNKcount);   /* try all ones */

			if (next == ON)
				flags &= ~N1ICUN0;
			else if (next == OFF)
				flags &= ~N0ICUN0;
		}

		for (i = 1; i <= UNKcount - 1; i++)
		{
			if ((state == OFF) || (state == UNK))
			{
				next = nextstate(OFF, ONcount + i);

				if (next == ON)
					flags &= ~(N1ICUN0 | N1ICUN1);
				else if (next == OFF)
					flags &= ~(N0ICUN0 | N0ICUN1);
			}

			if ((state == ON) || (state == UNK))
			{
				next = nextstate(ON, ONcount + i);

				if (next == ON)
					flags &= ~(N1ICUN0 | N1ICUN1);
				else if (next == OFF)
					flags &= ~(N0ICUN0 | N0ICUN1);
			}
		}
	}

	return flags;
}

#else // KS:


/*
 * IMPLIC flag values.
 */
//typedef	unsigned char	FLAGS;
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
 * Local procedures
 */
static	void	initimplic(void);
static	void	linkcell(CELL *);
static	STATE	choose(CELL *);
static	CELL *	symcell(CELL *);
static	CELL *	mapcell(CELL *);
static	CELL *	allocatecell(void);
static	CELL *	getnormalunknown(void);
static	CELL *	getaverageunknown(void);
static	CELL *	getsmartunknown(void); // KAS
static	BOOL	consistify(CELL *);
static	BOOL	consistify10(CELL *);
static	BOOL	checkwidth(CELL *);
static	CELL *	(*getunknown)(void);

/*
 * Initialize the table of cells.
 * Each cell in the active area is set to unknown state.
 * Boundary cells are set to zero state.
 */
BOOL
initcells(void)
{
	int	row, col, gen;
	int	i;
	BOOL	edge;
	CELL *	cell;
	CELL *	cell2;

	g.inited = FALSE;

	g.newcellcount=0;
	g.auxcellcount=0;
	g.newcells=NULL;
	g.searchlist=NULL;
	g.dummyrowinfo.oncount=0;
	g.dummycolinfo.oncount=0;


	if ((g.nrows <= 0) || (g.nrows > ROWMAX) ||
		(g.ncols <= 0) || (g.ncols > COLMAX) ||
		(g.period <= 0) || (g.period > GENMAX) ||
		(g.rowtrans < -TRANSMAX) || (g.rowtrans > TRANSMAX) ||
		(g.coltrans < -TRANSMAX) || (g.coltrans > TRANSMAX))
	{
		wlsErrorf(NULL,_T("ROW, COL, GEN, or TRANS out of range"));
		return FALSE;
	}

	for (i = 0; i < MAXCELLS; i++)
		g.celltable[i] = allocatecell();

	/*
	 * Link the cells together.
	 */
	for (col = 0; col <= g.ncols+1; col++)
	{
		for (row = 0; row <= g.nrows+1; row++)
		{
			for (gen = 0; gen < g.period; gen++)
			{
				edge = ((row == 0) || (col == 0) ||
					(row > g.nrows) || (col > g.ncols));

				cell = findcell(row, col, gen);
				cell->gen = gen;
				cell->row = row;
				cell->col = col;
				cell->rowinfo = &g.dummyrowinfo;
				cell->colinfo = &g.dummycolinfo;

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
					cell->combined = UNK;
					cell->free = TRUE;
				}

				/*
				 * Map time forwards and backwards,
				 * wrapping around at the ends.
				 */
				cell->past = findcell(row, col,
					(gen+g.period-1) % g.period);

				cell->future = findcell(row, col,
					(gen+1) % g.period);

				/*
				 * If this is not an edge cell, and
				 * there is some symmetry, then put
				 * this cell in the same loop as the
				 * next symmetrical cell.
				 */
				if(g.symmetry && !edge)
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

	for (col = 1; col <= g.ncols; col++) {
		for (row = 1; row <= g.nrows; row++) {
			for (gen = 0; gen < g.period; gen++) {
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
	if (g.rowtrans || g.coltrans || g.fliprows || g.flipcols || g.flipquads)
	{
		for (row = 0; row <= g.nrows+1; row++)
		{
			for (col = 0; col <= g.ncols+1; col++)
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
	for (row = 1; row <= g.nrows; row++)
	{
		for (col = 1; col <= g.ncols; col++)
		{
			cell = findcell(row, col, 0);
			cell->rowinfo = &g.rowinfo[row];
			cell->colinfo = &g.colinfo[col];
		}
	}

	if (g.smart) {
		getunknown = getsmartunknown; // KAS
	} else if (g.follow) {
		getunknown = getaverageunknown;
	} else {
		getunknown = getnormalunknown;
	}

	g.newset = g.settable;
	g.nextset = g.settable;

	g.searchset = g.searchtable;

	g.curstatus = OK;
	initimplic();

	g.inited = TRUE;
	return TRUE;
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
	if (((c1->row == c2->row) && (c1->col == c2->col)) || !g.ordergens)
	{
		// Put generation 0 first
		// or if calculating parents, put generation 0 last
		if (g.parent)
		{
			if (c1->gen < c2->gen) return 1;
			if (c1->gen > c2->gen) return -1;
		} else {
			if (c1->gen < c2->gen) return -1;
			if (c1->gen > c2->gen) return 1;
		}
		// if we are here, it is the same cell
	}

	if(g.diagsort) {
		if(c1->col+c1->row > c2->col+c2->row) return 1;
		if(c1->col+c1->row < c2->col+c2->row) return -1;
		if(abs(c1->col-c1->row) > abs(c2->col-c2->row)) return (g.orderwide)?1:(-1);
		if(abs(c1->col-c1->row) < abs(c2->col-c2->row)) return (g.orderwide)?(-1):1;
	}
	if(g.knightsort) {
		if(c1->col*2+c1->row > c2->col*2+c2->row) return 1;
		if(c1->col*2+c1->row < c2->col*2+c2->row) return -1;
		if(abs(c1->col-c1->row) > abs(c2->col-c2->row)) return (g.orderwide)?1:(-1);
		if(abs(c1->col-c1->row) < abs(c2->col-c2->row)) return (g.orderwide)?(-1):1;
	}

	/*
	 * Sort on the column number.
	 * By default this is from left to right.
	 * But if middle ordering is set, the ordering is from the center
	 * column outwards.
	 */
	if (g.ordermiddle)
	{
		midcol = (g.ncols + 1) / 2;

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
	midrow = (g.nrows + 1) / 2;

	dif1 = abs(c1->row - midrow);

	dif2 = abs(c2->row - midrow);

	if (dif1 < dif2) return (g.orderwide ? -1 : 1);

	if (dif1 > dif2) return (g.orderwide ? 1 : -1);

	return 0;
}


/*
 * Order the cells to be searched by building the search table list.
 * This list is built backwards from the intended search order.
 * The default is to do searches from the middle row outwards, and
 * from the left to the right columns.  The order can be changed though.
 */
void
initsearchorder(void)
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

	for (gen = 0; gen < g.period; gen++) {
		for (col = 1; col <= g.ncols; col++) {
			for (row = 1; row <= g.nrows; row++)	{
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
	g.searchlist = NULL;

	while (--count >= 0)
	{
		cell = table[count];
		cell->search = g.searchlist;
		g.searchlist = cell;
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

	--g.cellcount; // take all loops as a single cell

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
				if (g.nearcols) adjustnear(cell, -1);
				--g.g0oncellcount;
				if (cell->colinfo->setcount == g.nrows) --g.fullcolumns;
				--cell->colinfo->setcount;
			}

			if (g.combining && (cell->combined != UNK))
			{
				if (cell->combined == ON)
				{
					++g.differentcombinedcells;
				}
				--g.setcombinedcells;
			}

			cell = cell->loop;
		} while (cell != c1);

	} else {
// OFF is a little easier to do
		do {
			cell->state = UNK;
			cell->free = TRUE;
			if (cell->gen == 0) {
				if (cell->colinfo->setcount == g.nrows) --g.fullcolumns;
				--cell->colinfo->setcount;

			}

			if (g.combining && (cell->combined != UNK))
			{
				if (cell->combined == OFF)
				{
					++g.differentcombinedcells;
				}
				--g.setcombinedcells;
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

	if (g.combining && (g.differentcombinedcells == 0)) return FALSE;

	c1 = cell;

	if (state == ON) {
		// setting state ON
		// first let's examine the stats
		do {
			if (cell->gen == 0) {
				if ((g.usecol != 0)
					&& (g.colinfo[g.usecol].oncount == 0)
					&& (g.colinfo[g.usecol].setcount == g.nrows) && g.inited)
				{
					return FALSE;
				}

				if ((g.maxcount != 0) && (g.g0oncellcount >= g.maxcount))
				{
					return FALSE;
				}

				if (g.nearcols && (cell->near1 <= 0) && (cell->col > 1)
					&& g.inited)
				{
					return FALSE;
				}

				if (g.colcells && (cell->colinfo->oncount >= g.colcells)
					&& g.inited)
				{
					return FALSE;
				}

				if (g.colwidth && g.inited && checkwidth(cell))
					return FALSE;

				if (g.nearcols) adjustnear(cell, 1);

				cell->rowinfo->oncount++;

				cell->colinfo->oncount++;

				cell->colinfo->setcount++;

				if (cell->colinfo->setcount == g.nrows) g.fullcolumns++;

				cell->colinfo->sumpos += cell->row;

				g.g0oncellcount++;
			}

			cell->state = ON;
			cell->free = free;

			if (cell->active) {
				*g.newset++ = cell;

				*g.searchset++ = g.searchlist;

				while ((g.searchlist != NULL) && (g.searchlist->state != UNK)) {
					g.searchlist = g.searchlist->search;
				}

				free = FALSE; // all following cells in the loop are not free

			}

			if (g.combining &&(cell->combined != UNK))
			{
				if (cell->combined == ON)
				{
					--g.differentcombinedcells;
				}
				++g.setcombinedcells;
				if ((g.setcombinedcells == g.combinedcells) && (g.differentcombinedcells == 0))
				{
					return FALSE;
				}
			}

			cell = cell->loop;
		} while (c1 != cell);
	} else {
		// setting state OFF is somewhat easier
		do {
			if (cell->gen == 0) {
				if ((g.usecol != 0)
					&& (g.colinfo[g.usecol].oncount == 0)
					&& (g.colinfo[g.usecol].setcount == g.nrows) && g.inited)
				{
					return FALSE;
				}

				cell->colinfo->setcount++;

				if (cell->colinfo->setcount == g.nrows) g.fullcolumns++;
			}

			cell->state = OFF;
			cell->free = free;

			if (cell->active) {
				*g.newset++ = cell;

				*g.searchset++ = g.searchlist;

				while ((g.searchlist != NULL) && (g.searchlist->state != UNK)) {
					g.searchlist = g.searchlist->search;
				}

				free = FALSE;
			}

			if (g.combining && (cell->combined != UNK))
			{
				if (cell->combined == OFF)
				{
					--g.differentcombinedcells;
				}
				++g.setcombinedcells;
				if ((g.setcombinedcells == g.combinedcells) && (g.differentcombinedcells == 0))
				{
					return FALSE;
				}
			}

			cell = cell->loop;
		} while (c1 != cell);
	}

	++g.cellcount; // take whole loop as a single cell

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
	if (g.parent && (cell->gen == 0))
		return TRUE;

	// Now get the descriptor for the cell, its parent and its parent neighborhood

	prevcell = cell->past;
	desc = getdesc(prevcell);

	// the implic table will tell us everything we need to know

	flags = g.implic[desc];

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
examinenext(void)
{
	CELL *	cell;

	/*
	 * If there are no more cells to examine, then what we have
	 * is consistent.
	 */
	if (g.nextset == g.newset)
		return CONSISTENT;

	/*
	 * Get the next cell to examine, and check it out for symmetry
	 * and for consistency with its previous and next generations.
	 */
	cell = *g.nextset++;

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
backup(void)
{
	CELL *	cell;

	// first let's find how far to backup

	g.nextset = g.newset;

	while (g.nextset != g.settable)
	{
		cell = *--g.nextset;
		--g.searchset;

		if (!cell->free) continue;

		// free cell found
		// record old status
		g.prevstate = cell->state;

		g.searchlist = *g.searchset;

		// reset the stack and return the cell
		while (g.newset != g.nextset) {
			rescell(*--g.newset);
		}

		return cell;
	}

	// free cell not found
	// let's reset the stack anyway

	while (g.newset != g.nextset) {
		rescell(*--g.newset);
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

		setpos = g.nextset;

		if (proceed(cell, state, free)) return TRUE;

		if ((setpos == g.nextset) && free)
		{
			// no cell added to stack
			// no backup required
			// but prevstate is not defined now
			state = (ON + OFF) - state;
		} else {
			cell = backup();

			if (cell == NULL) return FALSE;

			state = (ON + OFF) - g.prevstate;
		}
		free = FALSE;
	}
}


/*
 * Find another unknown cell in a normal search.
 * Returns NULL if there are no more unknown cells.
 */
static CELL *
getnormalunknown(void)
{
	CELL *	cell;

	for (cell = g.searchlist; cell != NULL; cell = cell->search)
	{
		if (cell->state == UNK)
		{
			g.searchlist = cell;

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
getaverageunknown(void)
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

	cell = g.searchlist;

	while (cell)
	{
		g.searchlist = cell;
		curcol = cell->col;

		testcol = curcol - 1;

		while ((testcol > 0) && (g.colinfo[testcol].oncount <= 0))
			testcol--;

		if (testcol > 0)
		{
			wantrow = g.colinfo[testcol].sumpos /
				g.colinfo[testcol].oncount;
		}
		else
			wantrow = (g.nrows + 1) / 2;

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

static BOOL getsmartnumbers(CELL *cell)
{
	int cellno;
	int comb0, comb1;
	CELL ** setpos;

	// known and inactive cells are unimportant
	if (cell->state != UNK) return 2;

	// remember set position for proper backup
	setpos = g.newset;

	// remember cell count to calculate the change
	cellno = g.cellcount;

	comb0 = comb1 = g.differentcombinedcells + g.setcombinedcells;
	// test the cell
	if (proceed(cell, ON, TRUE))
	{
		g.smartlen1 = g.cellcount - cellno;
		comb1 = g.differentcombinedcells  + g.setcombinedcells - comb1;

		// back up
		backup();

		// and now let's try the OFF choice

		if (proceed(cell, OFF, TRUE))
		{
			g.smartlen0 = g.cellcount - cellno;
			comb0 = g.differentcombinedcells + g.setcombinedcells - comb0;

			if (g.smarton)
			{
				if (comb0 == comb1)
				{
					g.smartcomb = comb0;
					g.smartchoice = (g.smartlen1 > g.smartlen0) ? ON : OFF;
				}
				else if (comb0 > comb1)
				{
					g.smartcomb = comb0;
					g.smartchoice = OFF;
				}
				else
				{
					g.smartcomb = comb1;
					g.smartchoice = ON;
				}
			}
			else
			{
				g.smartcomb = 0;
				g.smartchoice = (g.smartlen1 > g.smartlen0) ? ON : OFF;
			}

			// back up
			backup();

			return TRUE;

		} else {
			// OFF state inconsistent
			// makes a good candidate
			g.smartchoice = OFF;

			// back up if something changed
			if (setpos != g.newset) backup();

			return FALSE;
		}

	} else {
		// ON state inconsistent
		// it's actually a good candidate
		g.smartchoice = ON;

		// back up if something changed
		if (setpos != g.newset) backup();

		return FALSE;

	}
}

// Smart cell ordering

static CELL *
getsmartunknown(void)
{
	CELL *cell;
	CELL *best;
	STATE bestchoice;
	int max, window, threshold, bestlen1, bestlen0, bestcomb, wnd, n1, n2, a, b, c, d;

	// Move the searchlist over all known cells
	while ((g.searchlist != NULL) && (g.searchlist->state != UNK)) {
		g.searchlist = g.searchlist->search;
	}

	// Return NULL if no unknown cells
	if (g.searchlist == NULL) return NULL;

	// Prepare threshold
	threshold = g.smartthreshold;
	if (threshold <= 0) threshold = MAXCELLS;

	// Prepare the dummy maximum
	max = 2; // at least 3 cells must change
	bestlen0 = 1;
	bestlen1 = 1;
	bestcomb = 0;

	best = NULL;

	wnd = 0;

	window = g.smartwindow;
	cell = g.searchlist;

	while ((cell != NULL) && (window > 0) && (max < threshold)) {
		++wnd;
		--window; // count known cells too
		if (cell->state == UNK)
		{
			if (getsmartnumbers(cell))
			{
				if (bestcomb == g.smartcomb)
				{
					// (smartlen0, smartlen1) is better than (bestlen0, bestlen1) if
					// 1/2**smartlen0 + 1/2**smartlen1 < 1/2**bestlen0 + 1/2**bestlen1
					// i.e.
					// 2**(b0+b1+s0) + 2**(b0+b1+s1) < 2**(s0+s1+b0) + 2**(s0+s1+b1)
					//
					// both sides are binary numbers with one or two 1's
					// so let's compare the exponents

					n1 = g.smartlen0 + g.smartlen1;
					n2 = n1 + bestlen1;
					n1 += bestlen0;

					if (n1 > n2)
					{
						a = n1;
						b = n2;
					} else if (n1 == n2) {
						a = n1 + 1;
						b = -1;
					} else {
						a = n2;
						b = n1;
					}

					// max = bestlen0 + bestlen1

					n1 = max + g.smartlen0;
					n2 = max + g.smartlen1;

					if (n1 > n2)
					{
						c = n1;
						d = n2;
					} else if (n1 == n2) {
						c = n1 + 1;
						d = -1;
					} else {
						c = n2;
						d = n1;
					}

					if ((a > c) || ((a = c) && (b > d)))
					{
						best = cell;
						bestchoice = g.smartchoice;
						// bestcomb = g.smartcomb; -- it's equal anyway
						bestlen1 = g.smartlen1;
						bestlen0 = g.smartlen0;
						max = bestlen0 + bestlen1;
					}
				}
				else if (bestcomb < g.smartcomb)
				{
					best = cell;
					bestchoice = g.smartchoice;
					bestcomb = g.smartcomb;
					bestlen1 = g.smartlen1;
					bestlen0 = g.smartlen0;
					max = bestlen0 + bestlen1;
				}
			} else {
				// the cell can be set only one way
				best = cell;
				bestchoice = g.smartchoice;
				max = MAXCELLS + 1;
				window = 0;
			}
		}
		cell = cell->search;
	}

	// Found something?
	if (best != NULL) {


		if (MAXCELLS >= max) {
			g.smartstatsumwnd += wnd;
			++g.smartstatsumwndc;
			g.smartstatsumlen += max;
			++g.smartstatsumlenc;

			if (g.smartstatsumwndc >= 100000) {
				g.smartstatwnd = g.smartstatsumwnd / g.smartstatsumwndc;
				g.smartstatsumwnd = 0;
				g.smartstatsumwndc = 0;
				if (g.smartstatsumlenc >= 10000) {
					g.smartstatlen = g.smartstatsumlen / g.smartstatsumlenc;
					g.smartstatsumlen = 0;
					g.smartstatsumlenc = 0;
				}
			}
		}

		if (g.smarton)
		{
			// propose the shorter tree
			g.smartchoice = bestchoice;
		} else {
			g.smartchoice = UNK;
		}
		return best;
	}

	// fall back to standard
	g.smartchoice = UNK;

	// Just return the first UNK cell
	// This shouldn't be often anyway

	return g.searchlist;
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

	if (g.smartchoice != UNK) return g.smartchoice;

	/*
	 * If we are following cells in other generations,
	 * then try to do that.
	 */

	if (g.followgens)
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

CELL *combinebackup(void);

/*
 * The top level search routine.
 * Returns if an object is found, or is impossible.
 */
STATUS
search(void)
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
		state = (ON + OFF) - g.prevstate;

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
			wlsUpdateProgressCounter();
			wlsUpdateAndShowTmpField_Sync();

			return NOTEXIST;
		}


		// If it is time to dump our state, then do that.

		if (g.dumpfreq && (++g.dumpcount >= g.dumpfreq))
		{
			g.dumpcount = 0;
			dumpstate(NULL, g.dumpfile, FALSE);
		}


		// If we have enough columns found, then remember to
		// write it to the output file.  Also keep the last
		// columns count values up to date.

		needwrite = FALSE;

		if (g.outputcols &&
			(g.fullcolumns >= g.outputlastcols + g.outputcols))
		{
			g.outputlastcols = g.fullcolumns;
			needwrite = TRUE;
		}

		if (g.outputlastcols > g.fullcolumns)
			g.outputlastcols = g.fullcolumns;

		// If it is time to view the progress,then show it.

		if (needwrite || (g.viewfreq && (++g.viewcount >= g.viewfreq)))
		{
			wlsUpdateProgressCounter();
			wlsUpdateAndShowTmpField_Sync();
		}

		// Write the progress to the output file if needed.
		// This is done after viewing it so that the write
		// message will stay visible for a while.

		if (needwrite)
		{
			wlsWriteCurrentFieldToFile(NULL, g.outputfile, TRUE);
		}


		// Get the next unknown cell and choose its state.

		cell = (*getunknown)();

		if (cell == NULL)
			return FOUND;

		if (g.stoponstep) {
			wlsUpdateProgressCounter();
			wlsUpdateAndShowTmpField_Sync();
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

	for (colcount = g.nearcols; colcount > 0; colcount--)
	{
		cell = cell->cr;
		curcell = cell;

		for (count = g.nearcols; count-- >= 0; curcell = curcell->cu)
			curcell->near1 += inc;

		curcell = cell->cd;

		for (count = g.nearcols; count-- > 0; curcell = curcell->cd)
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

	if (!g.colwidth || !g.inited || cell->gen)
		return FALSE;

	left = cell->colinfo->oncount;

	if (left <= 0)
		return FALSE;

	ucp = cell;
	dcp = cell;
	width = g.colwidth;
	minrow = cell->row;
	maxrow = cell->row;
	srcminrow = 1;
	srcmaxrow = g.nrows;
	full = TRUE;

	if ((g.rowsym && (cell->col >= g.rowsym)) ||
		(g.fliprows && (cell->col >= g.fliprows)))
	{
		full = FALSE;
		srcmaxrow = (g.nrows + 1) / 2;

		if (cell->row > srcmaxrow)
		{
			srcminrow = (g.nrows / 2) + 1;
			srcmaxrow = g.nrows;
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

	if (maxrow - minrow >= g.colwidth)
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
subperiods(void)
{
	int	row;
	int	col;
	int	gen;
	CELL *	cellg0;
	CELL *	cellgn;

	for (gen = 1; gen < g.period; gen++)
	{
		if (g.period % gen)
			continue;

		for (row = 1; row <= g.nrows; row++)
		{
			for (col = 1; col <= g.ncols; col++)
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

	if (g.fliprows && (col >= g.fliprows))
		row = g.nrows + 1 - row;

	if (g.flipcols && (row >= g.flipcols))
		col = g.ncols + 1 - col;

	if (g.flipquads)
	{				/* NEED TO GO BACKWARDS */
		tmp = col;
		col = row;
		row = g.ncols + 1 - tmp;
	}

	if (forward)
	{
		row += g.rowtrans;
		col += g.coltrans;
	}
	else
	{
		row -= g.rowtrans;
		col -= g.coltrans;
	}

	if (forward)
		return findcell(row, col, 0);
	else
		return findcell(row, col, g.period - 1);
}


/*
 * Make the two specified cells belong to the same loop.
 * If the two cells already belong to loops, the loops are joined.
 * This will force the state of these two cells to follow each other.
 * Symmetry uses this feature, and so does setting stable cells.
 * If any cells in the loop are frozen, then they all are.
 */
BOOL loopcells(CELL *cell1, CELL *cell2)
{
	CELL *	cell;
	BOOL	frozen;

	if (cell2==NULL) return TRUE;

	if (cell1 == cell2) return TRUE;

	/*
	 * See if the second cell is already part of the first cell's loop.
	 * If so, they they are already joined.  We don't need to
	 * check the other direction.
	 */
	for (cell = cell1->loop; cell != cell1; cell = cell->loop)
	{
		if (cell == cell2) return TRUE;
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

	return TRUE;
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

	if(!g.symmetry)
		return NULL;

	row = cell->row;
	col = cell->col;
	nrow = g.nrows + 1 - row;
	ncol = g.ncols + 1 - col;

	if(g.symmetry==1)  // col sym
		return findcell(row,ncol,cell->gen);

	if(g.symmetry==2) { // row sym
		return findcell(nrow,col,cell->gen);
	}

	if(g.symmetry==3)       // fwd diag
		return findcell(ncol,nrow,cell->gen);

	if(g.symmetry==4)    {   // bwd diag
		return findcell(col,row,cell->gen);
	}

	if(g.symmetry==5)       // origin
		return findcell(nrow, ncol, cell->gen);

	if(g.symmetry==6) {
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

	if(g.symmetry==7) {  // diagonal 4-fold
		// if on a diagonal...
		if(row==col || row==ncol)
			return findcell(nrow,ncol,cell->gen);

		// Not on a diagonal.
		if((col<row)==(col<nrow))
			return findcell(col,row,cell->gen);
		else
			return findcell(ncol,nrow,cell->gen);
	}

	if(g.symmetry==8) {      // origin*4 symmetry
		// this is surprisingly simple
			return findcell(ncol,row,cell->gen);
	}

	if(g.symmetry==9) {    // octagonal, this is gonna be tough
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
	if ((row >= 0) && (row <= g.nrows + 1) &&
		(col >= 0) && (col <= g.ncols + 1) &&
		(gen >= 0) && (gen < g.period))
	{
		return g.celltable[(col * (g.nrows + 2) + row) * g.period + gen];
	}

	/*
	 * See if the cell is already allocated in the auxillary table.
	 */
	for (i = 0; i < g.auxcellcount; i++)
	{
		cell = g.auxtable[i];

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
	cell->rowinfo = &g.dummyrowinfo;
	cell->colinfo = &g.dummycolinfo;

	g.auxtable[g.auxcellcount++] = cell;

	return cell;
}


/*
 * Allocate a new cell.
 * The cell is initialized as if it was a boundary cell.
 */
static CELL *
allocatecell(void)
{
	CELL *	cell;

	/*
	 * Allocate a new chunk of cells if there are none left.
	 */
	if (g.newcellcount <= 0)
	{
		g.newcells = (CELL *) malloc(sizeof(CELL) * ALLOCSIZE);

		if (g.newcells == NULL)
		{
			wlsErrorf(NULL,_T("Cannot allocate cell structure"));
			exit(1);
		}

		record_malloc(1,(void*)g.newcells);

		g.newcellcount = ALLOCSIZE;
	}

	g.newcellcount--;
	cell = g.newcells++;

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

	for (desc=0; desc<WLS_IMPLIC_LEN; desc++) {
		g.implic[desc] = IMPVOID;
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
							if (g.implic[desc] != IMPVOID) {
								ttystatus(_T("Duplicate descriptor!!!"));
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
										if (((caon == 0) && (faon == 0) && !g.bornrules[naon]) // both dead
											|| ((caon != 0) && (faon == 0) && !g.liverules[naon]) // dying
											|| ((caon == 0) && (faon != 0) && g.bornrules[naon]) // birth
											|| ((caon != 0) && (faon != 0) && g.liverules[naon])) { // survival
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
								g.implic[desc] = IMPBAD;
							} else {
								g.implic[desc] = IMPOK;
								if (funk != 0) { // future cell is unknown
									if (fison || fisoff) { // and just one state is possible
										g.implic[desc] |= IMPN;
										if (fison) {
											g.implic[desc] |= IMPN1;
										}
									}
								}
								if (cunk != 0) {
									if (cison || cisoff) {
										g.implic[desc] |= IMPC;
										if (cison) {
											g.implic[desc] |= IMPC1;
										}
									}
								}
								if (nunk != 0) {
									if (nison || nisoff) {
										g.implic[desc] |= IMPUN;
										if (nison) {
											g.implic[desc] |= IMPUN1;
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

	for (desc=0; desc<WLS_IMPLIC_LEN; desc++) {
		if (g.implic[desc] == IMPVOID) {
			g.implic[desc] = IMPBAD;
		}
	}

}

#endif // JS/KS
