/*
 * Life search program - actual search routines.
 * Author: David I. Bell.
 * Based on the algorithms by Dean Hickerson that were
 * included with the "xlife 2.0" distribution.  Thanks!
 * Changes for arbitrary Life rules by Nathan S. Thompson.
 */

/*
 * Define this as a null value so as to define the global variables
 * defined in lifesrc.h here.
 */
#define	EXTERN

#include "lifesrc.h"


/*
 * IMPLIC flag values.
 */
typedef	unsigned char	FLAGS;
#define	N0IC0	((FLAGS) 0x01)	/* new cell 0 ==> current cell 0 */
#define	N0IC1	((FLAGS) 0x02)	/* new cell 0 ==> current cell 1 */
#define	N1IC0	((FLAGS) 0x04)	/* new cell 1 ==> current cell 0 */
#define	N1IC1	((FLAGS) 0x08)	/* new cell 1 ==> current cell 1 */
#define	N0ICUN0	((FLAGS) 0x10)	/* new cell 0 ==> current unknown neighbors 0 */
#define	N0ICUN1	((FLAGS) 0x20)	/* new cell 0 ==> current unknown neighbors 1 */
#define	N1ICUN0	((FLAGS) 0x40)	/* new cell 1 ==> current unknown neighbors 0 */
#define	N1ICUN1	((FLAGS) 0x80)	/* new cell 1 ==> current unknown neighbors 1 */


/*
 * Table of transitions.
 * Given the state of a cell and its neighbors in one generation,
 * this table determines the state of the cell in the next generation.
 * The table is indexed by the descriptor value of a cell.
 */
static	STATE	transit[256];


/*
 * Table of implications.
 * Given the state of a cell and its neighbors in one generation,
 * this table determines deductions about the cell and its neighbors
 * in the previous generation.
 * The table is indexed by the descriptor value of a cell.
 */
static	FLAGS	implic[256];


/*
 * Table of state values.
 */
static	STATE	states[NSTATES] = {OFF, ON, UNK};


/*
 * Other local data.
 */
static	int	newcellcount;	/* number of cells ready for allocation */
static	int	auxcellcount;	/* number of cells in auxillary table */
static	CELL *	newcells;	/* cells ready for allocation */
static	CELL *	deadcell;	/* boundary cell value */
static	CELL *	searchlist;	/* current list of cells to search */
static	CELL *	celltable[MAXCELLS];	/* table of usual cells */
static	CELL *	auxtable[AUXCELLS];	/* table of auxillary cells */
static	ROWINFO	dummyrowinfo;	/* dummy info for ignored cells */
static	COLINFO	dummycolinfo;	/* dummy info for ignored cells */


/*
 * Local procedures
 */
static	void	inittransit PROTO((void));
static	void	initimplic PROTO((void));
static	void	initsearchorder PROTO((void));
static	void	linkcell PROTO((CELL *));
static	STATE	transition PROTO((STATE, int, int));
static	STATE	choose PROTO((CELL *));
static	FLAGS	implication PROTO((STATE, int, int));
static	CELL *	symcell PROTO((CELL *));
static	CELL *	mapcell PROTO((CELL *));
static	CELL *	allocatecell PROTO((void));
static	CELL *	getnormalunknown PROTO((void));
static	CELL *	getaverageunknown PROTO((void));
static	STATUS	consistify PROTO((CELL *));
static	STATUS	consistify10 PROTO((CELL *));
static	STATUS	examinenext PROTO((void));
static	BOOL	checkwidth PROTO((CELL *));
static	int	getdesc PROTO((CELL *));
static	int	sumtodesc PROTO((STATE, int));
static	int	ordersortfunc PROTO((CELL **, CELL **));
static	CELL *	(*getunknown) PROTO((void));
static	STATE	nextstate PROTO((STATE, int));


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

	if ((rowmax <= 0) || (rowmax > ROWMAX) ||
		(colmax <= 0) || (colmax > COLMAX) ||
		(genmax <= 0) || (genmax > GENMAX) ||
		(rowtrans < -TRANSMAX) || (rowtrans > TRANSMAX) ||
		(coltrans < -TRANSMAX) || (coltrans > TRANSMAX))
	{
		ttyclose();
		fprintf(stderr, "ROW, COL, GEN, or TRANS out of range\n");
		exit(1);
	}

	/*
	 * The first allocation of a cell MUST be deadcell.
	 * Then allocate the cells in the cell table.
	 */
	deadcell = allocatecell();

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
				cell->choose = TRUE;
				cell->rowinfo = &dummyrowinfo;
				cell->colinfo = &dummycolinfo;

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
				if ((rowsym || colsym || pointsym ||
					fwdsym || bwdsym) && !edge)
				{
					loopcells(cell, symcell(cell));
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
				cell = findcell(row, col, genmax - 1);
				cell2 = mapcell(cell);
				cell->future = cell2;
				cell2->past = cell;

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

	initsearchorder();

	if (follow)
		getunknown = getaverageunknown;
	else
		getunknown = getnormalunknown;

	newset = settable;
	nextset = settable;
	baseset = settable;

	curgen = 0;
	curstatus = OK;
	inittransit();
	initimplic();
}


/*
 * Order the cells to be searched by building the search table list.
 * This list is built backwards from the intended search order.
 * The default is to do searches from the middle row outwards, and
 * from the left to the right columns.  The order can be changed though.
 */
static void
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

	for (gen = 0; gen < genmax; gen++)
		for (col = 1; col <= colmax; col++)
			for (row = 1; row <= rowmax; row++)
	{
		if (rowsym && (col >= rowsym) && (row * 2 > rowmax + 1))
			continue;

		if (colsym && (row >= colsym) && (col * 2 > colmax + 1))
			continue;

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
	searchlist = NULL;

	while (--count >= 0)
	{
		cell = table[count];
		cell->search = searchlist;
		searchlist = cell;
	}
	
	fullsearchlist = searchlist;
}


/*
 * The sort routine for searching.
 */
static int
ordersortfunc(arg1, arg2)
	CELL **	arg1;
	CELL **	arg2;
{
	CELL *	c1;
	CELL *	c2;
	int	midcol;
	int	midrow;
	int	dif1;
	int	dif2;

	c1 = *arg1;
	c2 = *arg2;

	/*
	 * If we do not order by all generations, then put all of
	 * generation zero ahead of the other generations.
	 */
	if (!ordergens)
	{
		if (c1->gen < c2->gen)
			return -1;

		if (c1->gen > c2->gen)
			return 1;
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
	else
	{
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
	midrow = (rowmax + 1) / 2;

	dif1 = c1->row - midrow;

	if (dif1 < 0)
		dif1 = -dif1;

	dif2 = c2->row - midrow;

	if (dif2 < 0)
		dif2 = -dif2;

	if (dif1 < dif2)
		return (orderwide ? -1 : 1);

	if (dif1 > dif2)
		return (orderwide ? 1 : -1);

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
 * Set the state of a cell to the specified state.
 * The state is either ON or OFF.
 * Returns ERROR if the setting is inconsistent.
 * If the cell is newly set, then it is added to the set table.
 */
STATUS
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

		return OK;
	}

	if (cell->state != UNK)
	{
		DPRINTF4("setcell %d %d %d to state %s inconsistent\n",
			cell->row, cell->col, cell->gen,
			(state == ON) ? "on" : "off");

		return ERROR;
	}

	if (cell->gen == 0)
	{
		if (usecol && (colinfo[usecol].oncount == 0)
			&& (colinfo[usecol].setcount == rowmax) && inited)
		{
			return ERROR;
		}

		if (state == ON)
		{
			if (maxcount && (cellcount >= maxcount))
			{
				DPRINTF2("setcell %d %d 0 on exceeds maxcount\n",
					cell->row, cell->col);

				return ERROR;
			}

			if (nearcols && (cell->near <= 0) && (cell->col > 1)
				&& inited)
			{
				return ERROR;
			}

			if (colcells && (cell->colinfo->oncount >= colcells)
				&& inited)
			{
				return ERROR;
			}

			if (colwidth && inited && checkwidth(cell))
				return ERROR;

			if (nearcols)
				adjustnear(cell, 1);

			cell->rowinfo->oncount++;
			cell->colinfo->oncount++;
			cell->colinfo->sumpos += cell->row;
			cellcount++;
		}
	}

	DPRINTF5("setcell %d %d %d to %s, %s successful\n",
		cell->row, cell->col, cell->gen,
		(free ? "free" : "forced"), ((state == ON) ? "on" : "off"));

	*newset++ = cell;

	cell->state = state;
	cell->free = free;
	cell->colinfo->setcount++;

	if ((cell->gen == 0) && (cell->colinfo->setcount == rowmax))
		fullcolumns++;

	return OK;
}


/*
 * Calculate the current descriptor for a cell.
 */
static int
getdesc(cell)
	CELL *	cell;
{
	int	sum;

	sum = cell->cul->state + cell->cu->state + cell->cur->state;
	sum += cell->cdl->state + cell->cd->state + cell->cdr->state;
	sum += cell->cl->state + cell->cr->state;

	return ((sum & 0x88) ? (sum + cell->state * 2 + 0x11) :
		(sum * 2 + cell->state));
}


/*
 * Return the descriptor value for a cell and the sum of its neighbors.
 */
static int
sumtodesc(state, sum)
	STATE	state;
	int	sum;
{
	return ((sum & 0x88) ? (sum + state * 2 + 0x11) : (sum * 2 + state));
}


/*
 * Consistify a cell.
 * This means examine this cell in the previous generation, and
 * make sure that the previous generation can validly produce the
 * current cell.  Returns ERROR if the cell is inconsistent.
 */
static STATUS
consistify(cell)
	CELL *	cell;
{
	CELL *	prevcell;
	int	desc;
	STATE	state;
	FLAGS	flags;

	/*
	 * If we are searching for parents and this is generation 0, then
	 * the cell is consistent with respect to the previous generation.
	 */
	if (parent && (cell->gen == 0))
		return OK;

	/*
	 * First check the transit table entry for the previous
	 * generation.  Make sure that this cell matches the ON or
	 * OFF state demanded by the transit table.  If the current
	 * cell is unknown but the transit table knows the answer,
	 * then set the now known state of the cell.
	 */
	prevcell = cell->past;
	desc = getdesc(prevcell);
	state = transit[desc];

	if ((state != UNK) && (state != cell->state))
	{
		if (setcell(cell, state, FALSE) == ERROR)
			return ERROR;
	}

	/*
	 * Now look up the previous generation in the implic table.
	 * If this cell implies anything about the cell or its neighbors
	 * in the previous generation, then handle that.
	 */
	flags = implic[desc];

	if ((flags == 0) || (cell->state == UNK))
		return OK;

	DPRINTF1("Implication flags %x\n", flags);

	if ((flags & N0IC0) && (cell->state == OFF) &&
		(setcell(prevcell, OFF, FALSE) != OK))
	{
		return ERROR;
	}

	if ((flags & N1IC1) && (cell->state == ON) &&
		(setcell(prevcell, ON, FALSE) != OK))
	{
		return ERROR;
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

	if ((prevcell->cul->state == UNK) &&
		(setcell(prevcell->cul, state, FALSE) != OK))
	{
		return ERROR;
	}

	if ((prevcell->cu->state == UNK) &&
		(setcell(prevcell->cu, state, FALSE) != OK))
	{
		return ERROR;
	}

	if ((prevcell->cur->state == UNK) &&
		(setcell(prevcell->cur, state, FALSE) != OK))
	{
		return ERROR;
	}

	if ((prevcell->cl->state == UNK) &&
		(setcell(prevcell->cl, state, FALSE) != OK))
	{
		return ERROR;
	}

	if ((prevcell->cr->state == UNK) &&
		(setcell(prevcell->cr, state, FALSE) != OK))
	{
		return ERROR;
	}

	if ((prevcell->cdl->state == UNK) &&
		(setcell(prevcell->cdl, state, FALSE) != OK))
	{
		return ERROR;
	}

	if ((prevcell->cd->state == UNK) &&
		(setcell(prevcell->cd, state, FALSE) != OK))
	{
		return ERROR;
	}

	if ((prevcell->cdr->state == UNK) &&
		(setcell(prevcell->cdr, state, FALSE) != OK))
	{
		return ERROR;
	}

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
		return ERROR;

	cell = cell->future;

	if (consistify(cell) != OK)
		return ERROR;

	if (consistify(cell->cul) != OK)
		return ERROR;

	if (consistify(cell->cu) != OK)
		return ERROR;

	if (consistify(cell->cur) != OK)
		return ERROR;

	if (consistify(cell->cl) != OK)
		return ERROR;

	if (consistify(cell->cr) != OK)
		return ERROR;

	if (consistify(cell->cdl) != OK)
		return ERROR;

	if (consistify(cell->cd) != OK)
		return ERROR;

	if (consistify(cell->cdr) != OK)
		return ERROR;

	return OK;
}


/*
 * Examine the next choice of cell settings.
 */
static STATUS
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

	if (cell->loop && (setcell(cell->loop, cell->state, FALSE) != OK))
	{
		return ERROR;
	}

	return consistify10(cell);
}


/*
 * Set a cell to the specified value and determine all consequences we
 * can from the choice.  Consequences are a contradiction or a consistency.
 */
STATUS
proceed(cell, state, free)
	CELL *	cell;
	STATE	state;
	BOOL	free;
{
	int	status;

	if (setcell(cell, state, free) != OK)
		return ERROR;

	for (;;)
	{
		status = examinenext();

		if (status == ERROR)
			return ERROR;

		if (status == CONSISTENT)
			return OK;
	}
}


/*
 * Back up the list of set cells to undo choices.
 * Returns the cell which is to be tried for the other possibility.
 * Returns NULL_CELL on an "object cannot exist" error.
 */
CELL *
backup()
{
	CELL *	cell;

	searchlist = fullsearchlist;

	while (newset != baseset)
	{
		cell = *--newset;

		DPRINTF5("backing up cell %d %d %d, was %s, %s\n",
			cell->row, cell->col, cell->gen,
			((cell->state == ON) ? "on" : "off"),
			(cell->free ? "free": "forced"));

		if ((cell->state == ON) && (cell->gen == 0))
		{
			cell->rowinfo->oncount--;
			cell->colinfo->oncount--;
			cell->colinfo->sumpos -= cell->row;
			cellcount--;
			adjustnear(cell, -1);
		}

		if ((cell->gen == 0) && (cell->colinfo->setcount == rowmax))
			fullcolumns--;

		cell->colinfo->setcount--;

		if (!cell->free)
		{
			cell->state = UNK;
			cell->free = TRUE;

			continue;
		}

		nextset = newset;

		return cell;
	}

	nextset = baseset;
	return NULL_CELL;
}


/*
 * Do checking based on setting the specified cell.
 * Returns ERROR if an inconsistency was found.
 */
STATUS
go(cell, state, free)
	CELL *	cell;
	STATE	state;
	BOOL	free;
{
	STATUS	status;

	quitok = FALSE;

	for (;;)
	{
		status = proceed(cell, state, free);

		if (status == OK)
			return OK;

		cell = backup();

		if (cell == NULL_CELL)
			return ERROR;

		free = FALSE;
		state = 1 - cell->state;
		cell->state = UNK;
	}
}


/*
 * Find another unknown cell in a normal search.
 * Returns NULL_CELL if there are no more unknown cells.
 */
static CELL *
getnormalunknown()
{
	CELL *	cell;

	for (cell = searchlist; cell; cell = cell->search)
	{
		if (!cell->choose)
			continue;

		if (cell->state == UNK)
		{
			searchlist = cell;

			return cell;
		}
	}

	return NULL_CELL;
}


/*
 * Find another unknown cell when averaging is done.
 * Returns NULL_CELL if there are no more unknown cells.
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

	bestcell = NULL_CELL;
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

	return NULL_CELL;
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
	if (followgens)
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
search()
{
	CELL *	cell;
	BOOL	free;
	BOOL	needwrite;
	STATE	state;

	cell = (*getunknown)();

	if (cell == NULL_CELL)
	{
		cell = backup();

		if (cell == NULL_CELL)
			return ERROR;

		free = FALSE;
		state = 1 - cell->state;
		cell->state = UNK;
	}
	else
	{
		state = choose(cell);
		free = TRUE;
	}

	for (;;)
	{
		/*
		 * Set the state of the new cell.
		 */
		if (go(cell, state, free) != OK)
			return NOTEXIST;

		/*
		 * If it is time to dump our state, then do that.
		 */
		if (dumpfreq && (++dumpcount >= dumpfreq))
		{
			dumpcount = 0;
			dumpstate(dumpfile);
		}

		/*
		 * If we have enough columns found, then remember to
		 * write it to the output file.  Also keep the last
		 * columns count values up to date.
		 */
		needwrite = FALSE;

		if (outputcols &&
			(fullcolumns >= outputlastcols + outputcols))
		{
			outputlastcols = fullcolumns;
			needwrite = TRUE;
		}

		if (outputlastcols > fullcolumns)
			outputlastcols = fullcolumns;

		/*
		 * If it is time to view the progress,then show it.
		 */
		if (needwrite || (viewfreq && (++viewcount >= viewfreq)))
		{
			viewcount = 0;
			printgen(curgen);
		}

		/*
		 * Write the progress to the output file if needed.
		 * This is done after viewing it so that the write
		 * message will stay visible for a while.
		 */
		if (needwrite)
			writegen(outputfile, TRUE);

		/*
		 * Check for commands.
		 */
		if (ttycheck())
			getcommands();

		/*
		 * Get the next unknown cell and choose its state.
		 */
		cell = (*getunknown)();

		if (cell == NULL_CELL)
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

	for (colcount = nearcols; colcount > 0; colcount--)
	{
		cell = cell->cr;
		curcell = cell;

		for (count = nearcols; count-- >= 0; curcell = curcell->cu)
			curcell->near += inc;

		curcell = cell->cd;

		for (count = nearcols; count-- > 0; curcell = curcell->cd)
			curcell->near += inc;
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
void
loopcells(cell1, cell2)
	CELL *	cell1;
	CELL *	cell2;
{
	CELL *	cell;
	BOOL	frozen;

	/*
	 * Check simple cases of equality, or of either cell
	 * being the deadcell.
	 */
	if ((cell1 == deadcell) || (cell2 == deadcell))
	{
		fprintf(stderr, "Attemping to use deadcell in a loop\n");

		exit(1);
	}

	if (cell1 == cell2)
		return;

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
			return;
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
 * Returns NULL_CELL if there is no symmetry.
 */
static CELL *
symcell(cell)
	CELL *	cell;
{
	int	row;
	int	col;
	int	nrow;
	int	ncol;

	if (!rowsym && !colsym && !pointsym && !fwdsym && !bwdsym)
		return NULL_CELL;

	row = cell->row;
	col = cell->col;
	nrow = rowmax + 1 - row;
	ncol = colmax + 1 - col;

	/*
	 * If this is point symmetry, then this is easy.
	 */
	if (pointsym)
		return findcell(nrow, ncol, cell->gen);

	/*
	 * If there is symmetry on only one axis, then this is easy.
	 */
	if (!colsym)
	{
		if (col < rowsym)
			return NULL_CELL;

		return findcell(nrow, col, cell->gen);
	}

	if (!rowsym)
	{
		if (row < colsym)
			return NULL_CELL;

		return findcell(row, ncol, cell->gen);
	}

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
		return findcell(row, ncol, cell->gen);
	else
		return findcell(nrow, col, cell->gen);
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
 * Warning: The first allocation MUST be of the deadcell.
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
			ttyclose();
			fprintf(stderr, "Cannot allocate cell structure\n");
			exit(1);
		}

		newcellcount = ALLOCSIZE;
	}

	newcellcount--;
	cell = newcells++;

	/*
	 * If this is the first allocation, then make deadcell be this cell.
	 */
	if (deadcell == NULL)
		deadcell = cell;

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
	cell->past = deadcell;
	cell->future = deadcell;
	cell->cul = deadcell;
	cell->cu = deadcell;
	cell->cur = deadcell;
	cell->cl = deadcell;
	cell->cr = deadcell;
	cell->cdl = deadcell;
	cell->cd = deadcell;
	cell->cdr = deadcell;
	cell->loop = NULL;

	return cell;
}


/*
 * Initialize the implication table.
 */
static void
initimplic()
{
	STATE	state;
	int	OFFcount;
	int	ONcount;
	int	sum;
	int	desc;
	int	i;

	for (i = 0; i < NSTATES; i++)
	{
		state = states[i];

		for (OFFcount = 8; OFFcount >= 0; OFFcount--)
		{
			for (ONcount = 0; ONcount + OFFcount <= 8; ONcount++)
			{
				sum = ONcount + (8 - ONcount - OFFcount) * UNK;
				desc = sumtodesc(state, sum);

				implic[desc] =
					implication(state, OFFcount, ONcount);
			}
		}
	}
}


/*
 * Initialize the transition table.
 */
static void
inittransit()
{
	int	state;
	int	OFFcount;
	int	ONcount;
	int	sum;
	int	desc;
	int	i;

	for (i = 0; i < NSTATES; i++)
	{
		state = states[i];

		for (OFFcount = 8; OFFcount >= 0; OFFcount--)
		{
			for (ONcount = 0; ONcount + OFFcount <= 8; ONcount++)
			{
				sum = ONcount + (8 - ONcount - OFFcount) * UNK;
				desc = sumtodesc(state, sum);

				transit[desc] =
					transition(state, OFFcount, ONcount);
			}
		}
	}
}


/*
 * Return the next state if all neighbors are known.
 */
static STATE
nextstate(state, ONcount)
	STATE	state;
	int	ONcount;
{
	switch (state)
	{
		case ON:
			return liverules[ONcount];

		case OFF:
			return bornrules[ONcount];

		case UNK:
			if (bornrules[ONcount] == liverules[ONcount])
				return bornrules[ONcount];

			/* fall into default case */

		default:
			return UNK;
	}
}


/*
 * Determine the transition of a cell depending on its known neighbor counts.
 * The unknown neighbor count is implicit since there are eight neighbors.
 */
static STATE
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
	UNKcount = 8 - OFFcount - ONcount;
 
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

	UNKcount = 8 - OFFcount - ONcount;
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

/* END CODE */
