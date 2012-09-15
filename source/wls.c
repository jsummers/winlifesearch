// WinLifeSearch
// a Windows port of David I. Bell's lifesrc program
// By Jason Summers
//

// Request a newer version of comctl32.dll.
#ifdef _WIN64
#pragma comment(linker, \
	"\"/manifestdependency:type='Win32' "\
	"name='Microsoft.Windows.Common-Controls' "\
	"version='6.0.0.0' "\
	"processorArchitecture='amd64' "\
	"publicKeyToken='6595b64144ccf1df' "\
	"language='*'\"")
#else
#pragma comment(linker, \
	"\"/manifestdependency:type='Win32' "\
	"name='Microsoft.Windows.Common-Controls' "\
	"version='6.0.0.0' "\
	"processorArchitecture='X86' "\
	"publicKeyToken='6595b64144ccf1df' "\
	"language='*'\"")
#endif

#include "wls-config.h"
#include <windows.h>
#include <tchar.h>
#include <stdio.h>
#include <process.h>
#include <malloc.h>
#include <assert.h>
#include "resource.h"
#include "lifesrc.h"
#include <strsafe.h>

#ifdef JS
#define WLS_APPNAME _T("WinLifeSearchJ")
#else
#define WLS_APPNAME _T("WinLifeSearchK")
#endif

#define WLS_WM_THREADDONE (WM_APP+1)

#define TOOLBARHEIGHT 24

static LRESULT CALLBACK WndProcFrame(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);
static LRESULT CALLBACK WndProcMain(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);
static LRESULT CALLBACK WndProcToolbar(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);
static INT_PTR CALLBACK DlgProcAbout(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);
static INT_PTR CALLBACK DlgProcSymmetry(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);
static INT_PTR CALLBACK DlgProcTranslate(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);
static INT_PTR CALLBACK DlgProcPeriodRowsCols(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);
static INT_PTR CALLBACK DlgProcOutput(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);
static INT_PTR CALLBACK DlgProcSearch(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);
static INT_PTR CALLBACK DlgProcPreferences(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

typedef struct {
	HPEN celloutline,cell_off,axes,arrow1,arrow2,unchecked;
} pens_type;

typedef struct {
	HBRUSH cell,cell_on;
} brushes_type;

struct wcontext {
	HINSTANCE hInst;
	HWND hwndFrame,hwndMain,hwndToolbar;
	HWND hwndGen,hwndGenScroll;
	HWND hwndStatus;
#define WLS_SEL_OFF        0
#define WLS_SEL_SELECTING  1
#define WLS_SEL_SELECTED   2
	int selectstate;
	POINT startcell, endcell;
	RECT selectrect;
	int inverted;
	HANDLE hthread;
#define WLS_SRCH_OFF      0
#define WLS_SRCH_PAUSED   1
#define WLS_SRCH_RUNNING  2
	int searchstate;
	pens_type pens;
	brushes_type brushes;
	int cellwidth, cellheight;
	int centerx,centery,centerxodd,centeryodd;
	POINT scrollpos;
	HFONT statusfont;
	__int64 progress_counter_tot;
	int ignore_lbuttonup;
	int user_default_period;
	int user_default_columns;
	int user_default_rows;
#define WLS_PRIORITY_IDLE    10
#define WLS_PRIORITY_LOW     20
#define WLS_PRIORITY_NORMAL  30
	int search_priority;
	int wheel_gen_dir; // 1:up decreases gen  2:up increases gen

#define WLS_ACTION_APPEXIT           0x00000001
#define WLS_ACTION_RESETSEARCH       0x00000004
#define WLS_ACTION_UPDATEDISPLAY     0x00000010
#define WLS_ACTION_OPENSTATE         0x00000020
#define WLS_ACTION_SAVEANDRESUME     0x00000040
	unsigned int deferred_action;
#define WLS_THREADFLAG_ABORTED       0x00000001
#define WLS_THREADFLAG_RESETREQ      0x00000002
	unsigned int thread_stop_flags;

	CRITICAL_SECTION critsec_tmpfield;
};

struct globals_struct g;

struct wcontext *gctx;

volatile int abortthread;


/* \2 | 1/
   3 \|/ 0
   ---+---
   4 /|\ 7
   /5 | 6\  */

static const int symmap[10] = {
			// 76543210
	0x01,	// 00000001  no symmetry
	0x09,	// 00001001  mirror-x
	0x81,	// 10000001  mirror-y
	0x03,	// 00000011  diag-forward
	0x21,	// 00100001  diag-backward
	0x11,	// 00010001  origin
	0x99,	// 10011001  mirror-both
	0x33,	// 00110011  diag-both
	0x55,	// 01010101  origin-2
	0xff	// 11111111  octagonal
};

static void wlsErrorvf(struct wcontext *ctx, TCHAR *fmt, va_list ap)
{
	TCHAR buf[500];
	StringCbVPrintf(buf,sizeof(buf),fmt,ap);
	MessageBox(ctx->hwndFrame,buf,_T("Error"),MB_OK|MB_ICONWARNING);
}

// Show an error message box.
void wlsErrorf(struct wcontext *ctx, TCHAR *fmt, ...)
{
	va_list ap;
	if (!ctx) ctx = gctx;
	va_start(ap,fmt);
	wlsErrorvf(ctx,fmt,ap);
	va_end(ap);
}

static void wlsMessagevf(struct wcontext *ctx, TCHAR *fmt, va_list ap)
{
	TCHAR buf[500];
	StringCbVPrintf(buf,sizeof(buf),fmt,ap);
	MessageBox(ctx->hwndFrame,buf,_T("Message"),MB_OK|MB_ICONINFORMATION);
}

void wlsMessagef(struct wcontext *ctx, TCHAR *fmt, ...)
{
	va_list ap;
	if(!ctx) ctx = gctx;
	va_start(ap,fmt);
	wlsMessagevf(ctx,fmt,ap);
	va_end(ap);
}

// Essentially an an alias for wlsMessagef.
void ttystatus(TCHAR * fmt, ...)
{
	va_list ap;
	va_start(ap, fmt);
	wlsMessagevf(gctx,fmt,ap);
	va_end(ap);
}

static void wlsStatusvf(struct wcontext *ctx, TCHAR *fmt, va_list ap)
{
	TCHAR buf[500];
	StringCbVPrintf(buf,sizeof(buf),fmt,ap);
	SetWindowText(ctx->hwndStatus,buf);
}

// Show something on the status bar.
void wlsStatusf(struct wcontext *ctx, TCHAR *fmt, ...)
{
	va_list ap;
	if(!ctx) ctx = gctx;
	va_start(ap,fmt);
	wlsStatusvf(ctx,fmt,ap);
	va_end(ap);
}

// TODO: Find a better way to keep track of allocated memory.
// At least use a linked list or something.
// (The original lifesrc did not bother to free memory or to make it easy to
// free, because only one search would ever be done per process. Since we do
// multiple searches, we have to be able to free it.)
// func == 0: Free all recorded pointers.
// func == 1: Record pointer m.
void record_malloc(int func, void *m)
{
	int i;

	switch(func) {
	case 0:
		for(i=0;i<g.memblks_used;i++) {
			free(g.memblks[i]);
		}
		g.memblks_used=0;
		break;
	case 1:      // record this pointer
		if(g.memblks_used<2000)
			g.memblks[g.memblks_used++]=m;
		break;
	}
}

void wlsSetCellVal_Safe(struct field_struct *field, int k, int i, int j, WLS_CELLVAL v)
{
	if(k>=g.period || i>=g.ncols || j>=g.nrows || k<0 || i<0 || j<0) {
		return;
	}
	wlsSetCellVal(field,k,i,j,v);
}

// Assumes the fields are the same size.
static void wlsCopyField(struct field_struct *src, struct field_struct *dst)
{
	memcpy(dst->c,src->c,dst->ngens*dst->gen_stride);
}

static void wlsFillField_Gen(struct field_struct *field, int gen, WLS_CELLVAL cellval)
{
	memset(&field->c[gen*field->gen_stride],cellval,field->gen_stride);
}

// Set every cell to the same value
static void wlsFillField_All(struct field_struct *field, WLS_CELLVAL cellval)
{
	memset(field->c,cellval,field->ngens*field->gen_stride);
}

static void wlsFreeField(struct field_struct *field)
{
	if(!field) return;
	free(field->c);
	free(field);
}

// Allocate a new cell field.
// The new field will have dimensions at least g.period x g.nrows x g.ncols.
// If prev_field is not NULL, the field data will be copied from prev_field,
// and then prev_field will be freed.
// Any other cells will be initialized to CV_CLEAR.
static struct field_struct *wlsAllocField(struct field_struct *prev_field)
{
	struct field_struct *new_field;

	new_field = (struct field_struct*)calloc(1,sizeof(struct field_struct));
	if(!new_field) return NULL;

	new_field->ngens = g.period;
	new_field->ncols = g.ncols;
	new_field->nrows = g.nrows;
	new_field->gen_stride = new_field->ncols * new_field->nrows;
	new_field->row_stride = new_field->ncols;

	new_field->c = (WLS_CELLVAL*)malloc(new_field->ngens * new_field->gen_stride);
	if(!new_field) return NULL;

	if(prev_field) {
		int i,j,k; // position in the new field
		int i2,j2,k2; // position in the old field
		WLS_CELLVAL val;

		// Copy cells from the old field.
		for(k=0;k<new_field->ngens;k++) {
			for(j=0;j<new_field->nrows;j++) {
				for(i=0;i<new_field->ncols;i++) {
					// Find the nearest cell that exists in the old field.
					i2=i; j2=j; k2=k;
					if(k2>prev_field->ngens-1) k2=prev_field->ngens-1;
					if(j2>prev_field->nrows-1) j2=prev_field->nrows-1;
					if(i2>prev_field->ncols-1) i2=prev_field->ncols-1;

					// Read its value.
					val = wlsCellVal(prev_field,k2,i2,j2);

					if(k2!=k || j2!=j || i2!=i) {
						// Cell does not exist in old field. Use the value of the
						// nearest cell that does exist, unless it is forced ON.
						if(val==CV_FORCEDON) val=CV_CLEAR;
					}
					wlsSetCellVal(new_field,k,i,j,val);
				}
			}
		}

		wlsFreeField(prev_field);
	}
	else {
		// No previous field to copy from
		wlsFillField_All(new_field,CV_CLEAR);
	}


	return new_field;
}


// Set full_repaint to 1 if something other than cell states has changed
// (e.g. the number of cells).
static void wlsRepaintCells(struct wcontext *ctx, int full_repaint)
{
	InvalidateRect(ctx->hwndMain,NULL,full_repaint?TRUE:FALSE);
}

static void wlsRepaintCells_Sync(struct wcontext *ctx, int full_repaint)
{
	wlsRepaintCells(ctx,full_repaint);
	UpdateWindow(ctx->hwndMain);
}

/* Find all the cells symmetric to the given cell.
 * Populates the pt array with (x,y) coords.
 * pt[0] will be the given (x,y) point.
 * sets *num to the number of cells (it will be 1, 2, 4, or 8).
 * Caller must supply pt[8]
 */
static POINT *GetSymmetricCells(int x, int y, POINT *pt, int *num)
{
	int s,n;

	s=symmap[g.symmetry];
	pt[0].x = x;
	pt[0].y = y;
	n=1;

	if(s & 0x02) {
		assert(g.ncols==g.nrows);
		pt[n].x=g.nrows-1-y; // forward diag
		pt[n].y=g.ncols-1-x;
		n++;
	}
	if(s & 0x04) {
		assert(g.ncols==g.nrows);
		pt[n].x=y;               // rotate90
		pt[n].y=g.ncols-1-x;
		n++;
	}
	if(s & 0x08) {
		pt[n].x=g.ncols-1-x;  // mirrorx
		pt[n].y=y;
		n++;
	}
	if(s & 0x10) {
		pt[n].x=g.ncols-1-x;  // rotate180
		pt[n].y=g.nrows-1-y;
		n++;
	}
	if(s & 0x20) {
		assert(g.ncols==g.nrows);
		pt[n].x=y;                // back diag
		pt[n].y=x;
		n++;
	}
	if(s & 0x40) {
		assert(g.ncols==g.nrows);
		pt[n].x=g.nrows-1-y; // rotate270
		pt[n].y=x;
		n++;
	}
	if(s & 0x80) {
		pt[n].x=x;               // mirrory
		pt[n].y=g.nrows-1-y;
		n++;
	}

	*num=n;

	return pt;
}

static void RecalcCenter(struct wcontext *ctx)
{
	ctx->centerx= g.ncols/2;
	ctx->centery= g.nrows/2;
	ctx->centerxodd= g.ncols%2;
	ctx->centeryodd= g.nrows%2;
}

static int fix_scrollpos(struct wcontext *ctx)
{
	RECT r;
	int changed=0;
	int n;

	GetClientRect(ctx->hwndMain,&r);

	// If the entire field doesn't fit in the window, there should be no
	// unused space at the right/bottom of the field.
	n = g.ncols*ctx->cellwidth; // width of field
	// max scrollpos should be n - r.right
	if(n>r.right) {
		if(ctx->scrollpos.x>n-r.right) {
			ctx->scrollpos.x = n-r.right;
			changed=1;
		}
	}
	else if(ctx->scrollpos.x!=0) {
		// Entire field fits in the window, so it should never be scrolled.
		ctx->scrollpos.x=0;
		changed=1;
	}

	n = g.nrows*ctx->cellheight;
	if(n>r.bottom) {
		if(ctx->scrollpos.y>n-r.bottom) {
			ctx->scrollpos.y = n-r.bottom;
			changed = 1;
		}
	}
	else if(ctx->scrollpos.y!=0) {
		ctx->scrollpos.y=0;
		changed=1;
	}

	// scrollpos is never < 0
	if(ctx->scrollpos.x<0) ctx->scrollpos.x=0;
	if(ctx->scrollpos.y<0) ctx->scrollpos.y=0;

	return changed;
}

// Adjust the visible position of the scrollbars to ctx->scrollpos.
// redraw: Always repaint the window.
// checkscrollpos: Check if the requested scroll position is valid, and if not,
//   make it valid and repaint the window.
static void set_main_scrollbars(struct wcontext *ctx, int redraw, int checkscrollpos)
{
	HDC hDC;
	SCROLLINFO si;
	RECT r;

	if(checkscrollpos) {
		if(fix_scrollpos(ctx)) redraw = 1;
	}

	// TODO: We call GetClientRect too many times.
	GetClientRect(ctx->hwndMain,&r);

	si.cbSize=sizeof(SCROLLINFO);
	si.fMask=SIF_ALL;
	si.nMin=0;
	si.nMax=g.ncols*ctx->cellwidth;
	si.nPage=r.right;
	si.nPos=ctx->scrollpos.x;
	si.nTrackPos=0;
	SetScrollInfo(ctx->hwndMain,SB_HORZ,&si,TRUE);

	si.nMax=g.nrows*ctx->cellheight;
	si.nPage=r.bottom;
	si.nPos=ctx->scrollpos.y;
	SetScrollInfo(ctx->hwndMain,SB_VERT,&si,TRUE);

	hDC=GetDC(ctx->hwndMain);
	SetViewportOrgEx(hDC,-ctx->scrollpos.x,-ctx->scrollpos.y,NULL);
	ReleaseDC(ctx->hwndMain,hDC);
	if(redraw) wlsRepaintCells(ctx,TRUE);
}

static void DrawGuides(struct wcontext *ctx, HDC hDC)
{
	int centerpx,centerpy;
	int px1,py1,px2,py2,px3,py3;

	RecalcCenter(ctx);

	// store the center pixel in some temp vars to make things readable
	centerpx=ctx->centerx*ctx->cellwidth+ctx->centerxodd*(ctx->cellwidth/2);
	centerpy=ctx->centery*ctx->cellheight+ctx->centeryodd*(ctx->cellheight/2);

	SelectObject(hDC,ctx->pens.axes);


	// horizontal line
	if(g.symmetry==2 || g.symmetry==6 || g.symmetry==9) {
		MoveToEx(hDC,0,centerpy,NULL);
		LineTo(hDC,g.ncols*ctx->cellwidth,centerpy);
	}

	// vertical line
	if(g.symmetry==1 || g.symmetry==6 || g.symmetry==9) {
		MoveToEx(hDC,centerpx,0,NULL);
		LineTo(hDC,centerpx,g.nrows*ctx->cellheight);
	}

	// diag - forward
	if(g.symmetry==3 || g.symmetry==5 || g.symmetry>=7) {
		MoveToEx(hDC,0,g.nrows*ctx->cellheight,NULL);
		LineTo(hDC,g.ncols*ctx->cellwidth,0);
	}

	// diag - backward
	if(g.symmetry==4 || g.symmetry>=7) {
		MoveToEx(hDC,0,0,NULL);
		LineTo(hDC,g.ncols*ctx->cellwidth,g.nrows*ctx->cellheight);
	}
	if(g.symmetry==5 || g.symmetry==8) {
		MoveToEx(hDC,0,g.nrows*ctx->cellheight,NULL);
		LineTo(hDC,0,(g.nrows-2)*ctx->cellheight);
		MoveToEx(hDC,g.ncols*ctx->cellwidth,0,NULL);
		LineTo(hDC,g.ncols*ctx->cellwidth,2*ctx->cellheight);
	}
	if(g.symmetry==8) {
		MoveToEx(hDC,0,0,NULL);
		LineTo(hDC,2*ctx->cellwidth,0);
		MoveToEx(hDC,g.ncols*ctx->cellwidth,g.nrows*ctx->cellheight,NULL);
		LineTo(hDC,(g.ncols-2)*ctx->cellwidth,g.nrows*ctx->cellheight);
	}

	if(g.trans_rotate || g.trans_flip || g.trans_x || g.trans_y) {
		// the px & py values are pixels offsets from the center
		px1=0;           py1=2*ctx->cellheight;
		px2=0;           py2=0;
		px3=ctx->cellwidth/2; py3=ctx->cellheight/2;

		// an arrow indicating the starting position
		SelectObject(hDC,ctx->pens.arrow1);
		MoveToEx(hDC,centerpx+px1,centerpy+py1,NULL);
		LineTo(hDC,centerpx+px2,centerpy+py2);
		LineTo(hDC,centerpx+px3,centerpy+py3);

		// an arrow indicating the ending position
		// flip (horizontally) if necessary
		if(g.trans_flip) {
			px3= -px3;
		}

		// rotate if necessary
		// Note: can't rotate by 90 or 270 degrees if centerxodd != centeryodd
		if(ctx->centerxodd != ctx->centeryodd)
			assert(g.trans_rotate==0 || g.trans_rotate==2);

		switch(g.trans_rotate) {
		case 1:
			px1=ctx->cellwidth*2;
			py1=0;
			if(g.trans_flip)
				px3= -px3;
			else
				py3= -py3;
			break;
		case 2:
			py1= -py1;
			px3= -px3;
			py3= -py3;
			break;
		case 3:
			px1= -ctx->cellwidth*2;
			py1=0;
			if(g.trans_flip)
				py3= -py3;
			else
				px3= -px3;
			break;
		}

		// translate if necessary
		px1+=g.trans_x*ctx->cellwidth;
		px2+=g.trans_x*ctx->cellwidth;
		px3+=g.trans_x*ctx->cellwidth;
		py1+=g.trans_y*ctx->cellheight;
		py2+=g.trans_y*ctx->cellheight;
		py3+=g.trans_y*ctx->cellheight;

		SelectObject(hDC,ctx->pens.arrow2);
		MoveToEx(hDC,centerpx+px1,centerpy+py1,NULL);
		LineTo(hDC,centerpx+px2,centerpy+py2);
		LineTo(hDC,centerpx+px3,centerpy+py3);

	}
}

// pen & brush must already be selected
static void ClearCell(struct wcontext *ctx, HDC hDC,int x,int y, int thick_outline)
{
	Rectangle(hDC,x*ctx->cellwidth+1,y*ctx->cellheight+1,
	    (x+1)*ctx->cellwidth,(y+1)*ctx->cellheight);
	if(thick_outline) {
		Rectangle(hDC,x*ctx->cellwidth+2,y*ctx->cellheight+2,
			(x+1)*ctx->cellwidth-1,(y+1)*ctx->cellheight-1);
	}
}

static void DrawCell(struct wcontext *ctx, HDC hDC,int x,int y, struct field_struct *field)
{
	int allsame=1;
	WLS_CELLVAL tmp;
	int i;

	SelectObject(hDC,ctx->pens.celloutline);
	SelectObject(hDC,ctx->brushes.cell);

	// If all generations are the same, draw the cell in a different style.
	tmp = wlsCellVal(field,0,x,y);
	for(i=1;i<g.period;i++) {
		if(wlsCellVal(field,i,x,y)!=tmp) {
			allsame=0;
			break;
		}
	}

	ClearCell(ctx,hDC,x,y,!allsame);

	switch(wlsCellVal(field,g.curgen,x,y)) {
	case CV_FORCEDOFF:
		SelectObject(hDC,ctx->pens.cell_off);
		SelectObject(hDC,GetStockObject(NULL_BRUSH));
		Ellipse(hDC,x*ctx->cellwidth+3,y*ctx->cellheight+3,
			(x+1)*ctx->cellwidth-2,(y+1)*ctx->cellheight-2);
		break;
	case CV_FORCEDON:
		SelectObject(hDC,ctx->brushes.cell_on);
		SelectObject(hDC,GetStockObject(NULL_PEN));
		Ellipse(hDC,x*ctx->cellwidth+3,y*ctx->cellheight+3,
			(x+1)*ctx->cellwidth-1,(y+1)*ctx->cellheight-1);
		break;
	case CV_UNCHECKED:
		SelectObject(hDC,ctx->pens.unchecked);
		MoveToEx(hDC,(x  )*ctx->cellwidth+2,(y  )*ctx->cellheight+2,NULL);
		LineTo(hDC,  (x+1)*ctx->cellwidth-2,(y+1)*ctx->cellheight-2);
		MoveToEx(hDC,(x+1)*ctx->cellwidth-2,(y  )*ctx->cellheight+2,NULL);
		LineTo(hDC,  (x  )*ctx->cellwidth+2,(y+1)*ctx->cellheight-2);

		break;
	case CV_FROZEN:
		SelectObject(hDC,ctx->pens.cell_off);
		MoveToEx(hDC,x*ctx->cellwidth+2*ctx->cellwidth/3,y*ctx->cellheight+  ctx->cellheight/3,NULL);
		LineTo(hDC,  x*ctx->cellwidth+  ctx->cellwidth/3,y*ctx->cellheight+  ctx->cellheight/3);
		LineTo(hDC,  x*ctx->cellwidth+  ctx->cellwidth/3,y*ctx->cellheight+2*ctx->cellheight/3);

		MoveToEx(hDC,x*ctx->cellwidth+2*ctx->cellwidth/3,y*ctx->cellheight+  ctx->cellheight/2,NULL);
		LineTo(hDC,  x*ctx->cellwidth+  ctx->cellwidth/3,y*ctx->cellheight+  ctx->cellheight/2);

		break;

	}

}

#ifndef JS
// Set/reset unknown/unchecked
static void ChangeChecking(struct wcontext *ctx, HDC hDC, int x, int y, int allgens, int set)
{
	POINT pts[8];
	int numpts,i,j;
	WLS_CELLVAL s1, s2;
	int g1, g2;

	if (set) {
		s1 = CV_CLEAR;
		s2 = CV_UNCHECKED;
	}
	else {
		s1 = CV_UNCHECKED;
		s2 = CV_CLEAR;
	}

	if (allgens) {
		g1 = 0;
		g2 = g.period - 1;
	}
	else {
		g1 = g.curgen;
		g2 = g.curgen;
	}

	GetSymmetricCells(x,y,pts,&numpts);

	for(i=0;i<numpts;i++) {
		for(j=g1;j<=g2;j++) {
			if (wlsCellVal(g.field,j,pts[i].x,pts[i].y) == s1) {
				wlsSetCellVal(g.field,j,pts[i].x,pts[i].y,s2);
			}
		}
		DrawCell(ctx,hDC,pts[i].x,pts[i].y,g.field);
	}
}
#endif

// set and paint all cells symmetrical to the given cell
// (including the given cell)
static void Symmetricalize(struct wcontext *ctx, HDC hDC,int x,int y,int allgens)
{
	POINT pts[8];
	int numpts,i,j;
	WLS_CELLVAL cellval;

	GetSymmetricCells(x,y,pts,&numpts);

	cellval = wlsCellVal(g.field,g.curgen,x,y);

	for(i=0;i<numpts;i++) {
		if(i>0)
			wlsSetCellVal(g.field,g.curgen,pts[i].x,pts[i].y,cellval);
		if(allgens) {
			for(j=0;j<g.period;j++) {
				wlsSetCellVal(g.field,j,pts[i].x,pts[i].y,cellval);
			}
		}
		DrawCell(ctx,hDC,pts[i].x,pts[i].y, g.field);
	}
}

// HDC can be NULL
static void InvertCells(struct wcontext *ctx, HDC hDC1)
{
	RECT r;
	HDC hDC;

	if(ctx->endcell.x>=ctx->startcell.x) {
		r.left= ctx->startcell.x*ctx->cellwidth;
		r.right= (ctx->endcell.x+1)*ctx->cellwidth;
	}
	else {
		r.left= ctx->endcell.x*ctx->cellwidth;
		r.right=(ctx->startcell.x+1)*ctx->cellwidth;
	}

	if(ctx->endcell.y>=ctx->startcell.y) {
		r.top= ctx->startcell.y*ctx->cellheight;
		r.bottom= (ctx->endcell.y+1)*ctx->cellheight;
	}
	else {
		r.top=ctx->endcell.y*ctx->cellheight;
		r.bottom=(ctx->startcell.y+1)*ctx->cellheight;
	}


	if(hDC1)  hDC=hDC1;
	else      hDC=GetDC(ctx->hwndMain);

	InvertRect(hDC,&r);

	if(!hDC1) ReleaseDC(ctx->hwndMain,hDC);

}

// HDC can be NULL
static void SelectOff(struct wcontext *ctx, HDC hDC)
{
	if(ctx->selectstate==WLS_SEL_OFF) return;

	if(ctx->inverted) {
		InvertCells(ctx,hDC);
		ctx->inverted=0;
	}
	ctx->selectstate=WLS_SEL_OFF;
}

static void DrawWindow(struct wcontext *ctx, HDC hDC, struct field_struct *field)
{
	int i,j;

	for(j=0;j<g.nrows;j++) {
		for(i=0;i<g.ncols;i++) {
			DrawCell(ctx,hDC,i,j,field);
		}
	}
	DrawGuides(ctx,hDC);

	if(ctx->selectstate!=WLS_SEL_OFF) {
		InvertCells(ctx, hDC);
	}
}

static void PaintWindow(struct wcontext *ctx, HWND hWnd, struct field_struct *field)
{
	HDC hdc;
	HPEN hOldPen;
	HBRUSH hOldBrush;
	PAINTSTRUCT ps;

	hdc= BeginPaint(hWnd,&ps);
	hOldPen= SelectObject(hdc, GetStockObject(BLACK_PEN));
	hOldBrush=SelectObject(hdc,GetStockObject(LTGRAY_BRUSH));

	DrawWindow(ctx,hdc,field);

	SelectObject(hdc,hOldPen);
	SelectObject(hdc,hOldBrush);
	EndPaint(hWnd,&ps);
}

static void FixFrozenCells(void)
{
	int x,y,z;
	WLS_CELLVAL cellval;

	for(y=0;y<g.nrows;y++) {
		for(x=0;x<g.ncols;x++) {
			cellval = wlsCellVal(g.field,g.curgen,x,y);
			for(z=0;z<g.period;z++) {
				if(z!=g.curgen) {
					if(cellval==CV_FROZEN) {
						wlsSetCellVal(g.field,z,x,y,CV_FROZEN);
					}
					else {  // current not frozen
						if(wlsCellVal(g.field,z,x,y)==CV_FROZEN)
							wlsSetCellVal(g.field,z,x,y,CV_CLEAR);
					}
				}
			}
		}
	}
}


// returns 0 if processed
static int Handle_UIEvent(struct wcontext *ctx, UINT msg,WORD xp,WORD yp,WPARAM wParam)
{
	int x,y;
	int i,j;
	HDC hDC;
	int vkey;
	int tmp;
	int allgens=0;
	WLS_CELLVAL prevval;
	WLS_CELLVAL newval;

	newval = CV_INVALID;

	xp+=(WORD)ctx->scrollpos.x;
	yp+=(WORD)ctx->scrollpos.y;

	x=xp/ctx->cellwidth;   // + scroll offset
	y=yp/ctx->cellheight;  // + scroll offset

	if(x<0 || x>=g.ncols) return 1;
	if(y<0 || y>=g.nrows) return 1;

	prevval = wlsCellVal(g.field,g.curgen,x,y);

	switch(msg) {

	case WM_MOUSEMOVE:
		if(ctx->selectstate!=WLS_SEL_SELECTING) return 1;

		if(x==ctx->endcell.x && y==ctx->endcell.y) {   // cursor hasn't moved to a new cell
			return 0;
		}

		if(x==ctx->startcell.x && y==ctx->startcell.y) {  // cursor over starting cell
			hDC=GetDC(ctx->hwndMain);
			InvertCells(ctx,hDC); // turn off
			ReleaseDC(ctx->hwndMain,hDC);

			ctx->inverted=0;
			ctx->endcell.x=x;
			ctx->endcell.y=y;
			return 0;
		}


		// else we're at a different cell

		hDC=GetDC(ctx->hwndMain);
		if(ctx->inverted) InvertCells(ctx,hDC);    // turn off
		ctx->inverted=0;

		ctx->endcell.x=x;
		ctx->endcell.y=y;

		InvertCells(ctx,hDC);    // turn back on

		// record the select region
		if(ctx->startcell.x<=ctx->endcell.x) {
			ctx->selectrect.left= ctx->startcell.x;
			ctx->selectrect.right=ctx->endcell.x;
		}
		else {
			ctx->selectrect.left=ctx->endcell.x;
			ctx->selectrect.right=ctx->startcell.x;
		}
		if(ctx->startcell.y<=ctx->endcell.y) {
			ctx->selectrect.top= ctx->startcell.y;
			ctx->selectrect.bottom=ctx->endcell.y;
		}
		else {
			ctx->selectrect.top=ctx->endcell.y;
			ctx->selectrect.bottom=ctx->startcell.y;
		}

		ctx->inverted=1;
		ReleaseDC(ctx->hwndMain,hDC);

		return 0;

	case WM_LBUTTONDOWN:
		if(ctx->selectstate!=WLS_SEL_OFF) {
			SelectOff(ctx,NULL);
			// The left button was used to cancel the selection (and then to
			// potentially start a new selection.)
			// When it is released, make sure that isn't interpreted as a
			// command to change the contents of a cell.
			ctx->ignore_lbuttonup = 1;
		}
		else {
			ctx->ignore_lbuttonup = 0;
		}

		ctx->selectstate=WLS_SEL_SELECTING;
		ctx->startcell.x=x;
		ctx->startcell.y=y;
		ctx->endcell.x=x;
		ctx->endcell.y=y;
		return 0;

	case WM_LBUTTONUP:
		if(wParam & MK_SHIFT) allgens=1;
		if(x==ctx->startcell.x && y==ctx->startcell.y) {
			if(ctx->ignore_lbuttonup) {
				ctx->ignore_lbuttonup=0;
				SelectOff(ctx,NULL);
				return 0;
			}

			ctx->selectstate=WLS_SEL_OFF;
			if(prevval==CV_FORCEDON) newval=CV_CLEAR;
			else newval=CV_FORCEDON;
			//Symmetricalize(hDC,x,y,allgens);
		}
		else if(ctx->selectstate==WLS_SEL_SELECTING) {
			// selecting an area
			ctx->selectstate=WLS_SEL_SELECTED;
			return 0;

		}
		break;

	case WM_RBUTTONDOWN:     // toggle off/unchecked
		if(ctx->selectstate!=WLS_SEL_OFF) {
			SelectOff(ctx,NULL);
			return 0;
		}
		if(wParam & MK_SHIFT) allgens=1;
		if(prevval==CV_FORCEDOFF) newval=CV_CLEAR;
		else newval=CV_FORCEDOFF;
		break;

	case WM_CHAR:
		vkey=(int)wParam;

		if(vkey=='C' || vkey=='X' || vkey=='A' || vkey=='S' || vkey=='F')
			allgens=1;
#ifndef JS
		if(vkey=='I' || vkey=='O')
			allgens=1;
#endif
		if(vkey=='C' || vkey=='c') {
			newval=CV_CLEAR;
		}
		else if(vkey=='X' || vkey=='x') {
			newval=CV_UNCHECKED;
		}
		else if(vkey=='A' || vkey=='a') {
			newval=CV_FORCEDOFF;
		}
		else if(vkey=='S' || vkey=='s') {
			newval=CV_FORCEDON;
		}
		else if(vkey=='F' || vkey=='f') {
			newval=CV_FROZEN;
			allgens=1;
		}
#ifndef JS
		else if(vkey=='I' || vkey=='i') {
			newval=11;
		}
		else if(vkey=='O' || vkey=='o') {
			newval=10;
		}
#endif
		else {
			return 1;
		}
		break;

	default:
		return 1;
	}

#ifdef JS
	FixFrozenCells();

	hDC=GetDC(ctx->hwndMain);

	tmp=ctx->selectstate;

	SelectOff(ctx, hDC);

	if(tmp==WLS_SEL_SELECTED) {
		if(newval!=CV_INVALID) {
			for(i=ctx->selectrect.left;i<=ctx->selectrect.right;i++) {
				for(j=ctx->selectrect.top;j<=ctx->selectrect.bottom;j++) {
					wlsSetCellVal(g.field,g.curgen,i,j,newval);
					Symmetricalize(ctx, hDC,i,j,allgens);
				}
			}
		}
	}
	else {
		if(newval!=CV_INVALID) wlsSetCellVal(g.field,g.curgen,x,y,newval);
		Symmetricalize(ctx, hDC,x,y,allgens);
	}

	DrawGuides(ctx, hDC);

	ReleaseDC(ctx->hwndMain,hDC);
#else
	hDC=GetDC(ctx->hwndMain);

	tmp=ctx->selectstate;

	SelectOff(ctx,hDC);

	if(ctx->searchstate == WLS_SRCH_OFF) {
		if(tmp==WLS_SEL_SELECTED) {
			if(newval!=CV_INVALID) {
				for(j=ctx->selectrect.top;j<=ctx->selectrect.bottom;j++) {
					for(i=ctx->selectrect.left;i<=ctx->selectrect.right;i++) {
						if (newval < 10) {
							wlsSetCellVal(g.field,g.curgen,i,j,newval);
							Symmetricalize(ctx,hDC,i,j,allgens);
						}
						else {
							ChangeChecking(ctx,hDC,i,j,allgens,newval == 10);
						}
					}
				}
			}
		}
		else {
			if(newval!=CV_INVALID) {
				if (newval < 10) {
					wlsSetCellVal(g.field,g.curgen,x,y,newval);
					Symmetricalize(ctx,hDC,x,y,allgens);
				}
				else {
					ChangeChecking(ctx,hDC,x,y,allgens,newval == 10);
				}
			}
		}
	}

	FixFrozenCells();

	DrawGuides(ctx,hDC);

	ReleaseDC(ctx->hwndMain,hDC);
#endif

	return 0;
}

#ifdef JS

// copy my format to dbells format...
// ... and copy the initial state to tmpfield
static int set_initial_cells(void)
{
	int i,j,g1;
	struct wcontext *ctx = gctx;

	wlsCopyField(g.field,g.tmpfield);

	for(g1=0;g1<g.period;g1++) {
		for(j=0;j<g.nrows;j++) {
			for(i=0;i<g.ncols;i++) {
				switch(wlsCellVal(g.field,g1,i,j)) {
				case CV_FORCEDOFF:
					if(!proceed(findcell(j+1,i+1,g1),OFF,FALSE)) {
						wlsMessagef(ctx,_T("Inconsistent OFF state for cell (col %d,row %d,gen %d)"),i+1,j+1,g1);
						return 0;
					}
					break;
				case CV_FORCEDON:
					if(!proceed(findcell(j+1,i+1,g1),ON,FALSE)) {
						wlsMessagef(ctx,_T("Inconsistent ON state for cell (col %d,row %d,gen %d)"),i+1,j+1,g1);
						return 0;
					}
					break;
				case CV_UNCHECKED:  // eXcluded cells
					excludecone(j+1,i+1,g1);
					break;
				case CV_FROZEN:
					freezecell(j+1, i+1);
					break;
				}
			}
		}
	}
	return 1;
}

#else

// copy my format to dbells format...
// ... and copy the initial state to tmpfield
BOOL set_initial_cells(void)
{
	CELL *cell;
	CELL **setpos;
	BOOL change;
	int i,j,g1;
	struct wcontext *ctx = gctx;

	wlsCopyField(g.field,g.tmpfield);

	g.newset = g.settable;
	g.nextset = g.settable;

	for(g1=0;g1<g.period;g1++) {
		for(j=0;j<g.nrows;j++) {
			for(i=0;i<g.ncols;i++) {
				switch(wlsCellVal(g.field,g1,i,j)) {
				case CV_FORCEDOFF:
					if(!proceed(findcell(j+1,i+1,g1),OFF,FALSE)) {
						wlsMessagef(ctx,_T("Inconsistent OFF state for cell (col %d,row %d,gen %d)"),i+1,j+1,g1);
						return FALSE;
					}
					break;
				case CV_FORCEDON:
					if(!proceed(findcell(j+1,i+1,g1),ON,FALSE)) {
						wlsMessagef(ctx,_T("Inconsistent ON state for cell (col %d,row %d,gen %d)"),i+1,j+1,g1);
						return FALSE;
					}
					break;
				case CV_UNCHECKED:
					cell = findcell(j+1,i+1,g1);
					cell->unchecked = TRUE;
					break;
				case CV_FROZEN:
					freezecell(j+1, i+1);
					break;
				}
			}
		}
	}

	// now let's try all UNK cells for ON and OFF state
	// set those which allow only one

	setpos = g.newset;
	do {
		change = FALSE;
		for(g1=0;g1<g.period;g1++) {
			for(j=0;j<g.nrows;j++) {
				for(i=0;i<g.ncols;i++) {
					cell = findcell(j+1,i+1,g1);
					if (cell->active && (cell->state == UNK)) {
						if (proceed(cell, OFF, TRUE)) {
							backup();
							if (proceed(cell, ON, TRUE)) {
								backup();
							}
							else {
								// OFF possible, ON impossible
								if (setpos != g.newset) backup();
								if (proceed(cell, OFF, TRUE)) {
									change = TRUE;
								}
								else {
									// we should never get here
									// because it's already tested that the OFF state is possible
									wlsMessagef(ctx,_T("Program inconsistency found"));
									return FALSE;
								}
							}
						}
						else {
							// can't set OFF state
							// let's try ON state
							if (setpos != g.newset) backup();
							if (proceed(cell, ON, TRUE)) {
								change = TRUE;
							}
							else {
								// can't set neither ON nor OFF state
								wlsMessagef(ctx,_T("Inconsistent UNK state for cell (col %d,row %d,gen %d)"),i+1,j+1,g1);
								return FALSE;
							}
						}
					}
				}
			}
		}
	} while (change);

	g.newset = g.settable;
	g.nextset = g.settable;

	return TRUE;
}

#endif

static void draw_gen_counter(struct wcontext *ctx)
{
	TCHAR buf[80];
	SCROLLINFO si;

	si.cbSize=sizeof(SCROLLINFO);
	si.fMask=SIF_ALL;
	si.nMin=0;
	si.nMax=g.period-1;
	si.nPage=1;
	si.nPos=g.curgen;
	si.nTrackPos=0;

	SetScrollInfo(ctx->hwndGenScroll,SB_CTL,&si,TRUE);

	StringCbPrintf(buf,sizeof(buf),_T("p%d: %d"),g.period,g.curgen);
	SetWindowText(ctx->hwndGen,buf);
}

// g.viewcount is the number of calculations since wlsUpdateProgressCounter() was last
// called.
// To reset the counter, set g.viewcount to -1 before calling wlsUpdateProgressCounter().
void wlsUpdateProgressCounter(void)
{
	TCHAR buf[80];
	struct wcontext *ctx = gctx;

	if (g.viewcount<0) {
		ctx->progress_counter_tot=0;
	}
	else {
		ctx->progress_counter_tot += g.viewcount;
	}
	g.viewcount = 0;

	StringCbPrintf(buf,sizeof(buf),WLS_APPNAME _T(" [%I64d]"),ctx->progress_counter_tot);
	SetWindowText(ctx->hwndFrame,buf);
}

static void wlsCopyLifesrcToTmpfield(struct wcontext *ctx)
{
	int i,j,g1;
	CELL *cell;

	if(ctx->searchstate==WLS_SRCH_OFF) {
		return;
	}

	// copy dbell's format back into mine
	for(g1=0;g1<g.period;g1++) {
		for(j=0;j<g.nrows;j++) {
			for(i=0;i<g.ncols;i++) {
				cell=findcell(j+1,i+1,g1);
				switch(cell->state) {
				case OFF:
					wlsSetCellVal(g.tmpfield,g1,i,j,CV_FORCEDOFF);
					break;
				case ON:
					wlsSetCellVal(g.tmpfield,g1,i,j,CV_FORCEDON);
					break;
				case UNK:
#ifdef JS
					wlsSetCellVal(g.tmpfield,g1,i,j,CV_CLEAR);
#else
					wlsSetCellVal(g.tmpfield,g1,i,j,wlsCellVal(g.field,g1,i,j));
#endif
				}
			}
		}
	}
}

void wlsUpdateAndShowTmpField(void)
{
	struct wcontext *ctx = gctx;
	if(ctx->searchstate==WLS_SRCH_OFF) return;
	wlsCopyLifesrcToTmpfield(ctx);
	wlsRepaintCells(ctx,FALSE);
}

void wlsUpdateAndShowTmpField_Sync(void)
{
	struct wcontext *ctx = gctx;

	EnterCriticalSection(&ctx->critsec_tmpfield);
	wlsCopyLifesrcToTmpfield(ctx);
	LeaveCriticalSection(&ctx->critsec_tmpfield);
	wlsRepaintCells_Sync(ctx,FALSE);
}

#ifndef JS

static void do_combine(void)
{
	int i,j,g1;
	CELL *cell;

	if (g.combining) {
		for(g1=0;g1<g.period;g1++) {
			for(i=0;i<g.ncols;i++) {
				for(j=0;j<g.nrows;j++) {
					cell=findcell(j+1,i+1,g1);
					if ((cell->combined != UNK) && (cell->combined != cell->state)) {
						--g.combinedcells;
						cell->combined = UNK;
					}
				}
			}
		}
	}
	else {
		g.combining = TRUE;
		g.combinedcells = 0;
		for(g1=0;g1<g.period;g1++) {
			for(i=0;i<g.ncols;i++) {
				for(j=0;j<g.nrows;j++) {
					cell=findcell(j+1,i+1,g1);
					if ((wlsCellVal(g.field,g1,i,j) > 1) && ((cell->state == ON) || (cell->state == OFF))) {
						++g.combinedcells;
						cell->combined = cell->state;
					}
					else {
						cell->combined = UNK;
					}
				}
			}
		}
	}
	g.setcombinedcells = g.combinedcells;
	g.differentcombinedcells = 0;
}

static void wlsCopyCombineToTmpfield(struct wcontext *ctx)
{
	int i,j,g1;
	CELL *cell;

	if (g.combinedcells > 0) {
		for(g1=0;g1<g.period;g1++) {
			for(j=0;j<g.nrows;j++) {
				for(i=0;i<g.ncols;i++) {
					cell=findcell(j+1,i+1,g1);
					switch(cell->combined) {
					case OFF:
						wlsSetCellVal(g.tmpfield,g1,i,j,CV_FORCEDOFF);
						break;
					case ON:
						wlsSetCellVal(g.tmpfield,g1,i,j,CV_FORCEDON);
						break;
					case UNK:
						wlsSetCellVal(g.tmpfield,g1,i,j,wlsCellVal(g.field,g1,i,j));
					}
				}
			}
		}
	}
}

#endif

#ifdef JS

static DWORD WINAPI search_thread(LPVOID foo)
{
	struct wcontext *ctx = gctx;

	ctx->thread_stop_flags = 0;

	while (TRUE) {
		if (g.curstatus == OK)
			g.curstatus = search();

		if(abortthread) {
			ctx->thread_stop_flags |= WLS_THREADFLAG_ABORTED;
			goto done;
		}

		if ((g.curstatus == FOUND) && g.userow && (g.rowinfo[g.userow].oncount == 0)) {
			g.curstatus = OK;
			continue;
		}

		if ((g.curstatus == FOUND) && !g.allobjects && subperiods()) {
			g.curstatus = OK;
			continue;
		}

		if (g.dumpfreq) {
			g.dumpcount = 0;
			dumpstate(ctx->hwndFrame, g.dumpfile);
		}

		g.curgen = 0;

		if (g.curstatus == FOUND) {
			g.curstatus = OK;

			wlsUpdateProgressCounter();
			wlsUpdateAndShowTmpField_Sync();
			wlsStatusf(ctx,_T("Object %d found."), ++g.foundcount);

			wlsWriteCurrentFieldToFile(ctx->hwndFrame, g.outputfile, TRUE);
			goto done;
		}

		if (g.foundcount == 0) {
			wlsStatusf(ctx,_T(""));
			wlsMessagef(ctx,_T("No objects found"));
		}
		else {
			wlsMessagef(ctx,_T("Search completed: %d object%s found"),
				g.foundcount, (g.foundcount == 1) ? _T("") : _T("s"));
		}

		wlsStatusf(ctx,_T("Search complete."));

		goto done;
	}
done:
	ctx->searchstate=WLS_SRCH_PAUSED;
	PostMessage(ctx->hwndFrame,WLS_WM_THREADDONE,0,0);
	_endthreadex(0);
	return 0;
}

#else

static DWORD WINAPI search_thread(LPVOID foo)
{
	BOOL reset = 0;
	struct wcontext *ctx = gctx;

	ctx->thread_stop_flags = 0;

	/*
	 * Initial commands are complete, now look for the object.
	 */
	while (TRUE) {
		if (g.curstatus == OK)
			g.curstatus = search();

		if(abortthread) {
			ctx->thread_stop_flags |= WLS_THREADFLAG_ABORTED;
			goto done;
		}

		if ((g.curstatus == FOUND) && g.userow && (g.rowinfo[g.userow].oncount == 0)) {
			g.curstatus = OK;
			continue;
		}

		if ((g.curstatus == FOUND) && !g.allobjects && !g.parent && subperiods()) {
			g.curstatus = OK;
			continue;
		}

		if (g.dumpfreq) {
			g.dumpcount = 0;
			dumpstate(NULL, g.dumpfile, FALSE);
		}

		if ((g.curstatus == FOUND) && g.combine) {
			g.curstatus = OK;
			++g.foundcount;
			do_combine();
			wlsWriteCurrentFieldToFile(NULL, g.outputfile, TRUE);
			wlsStatusf(ctx,_T("Combine: %d cells remaining from %d solutions"), g.combinedcells, g.foundcount);
			continue;
		}

		/*
		 * Here if results are going to a file.
		 */
		if (g.curstatus == FOUND) {
			g.curstatus = OK;

			wlsUpdateProgressCounter();
			wlsUpdateAndShowTmpField_Sync();
			++g.foundcount;
			wlsStatusf(ctx,_T("Object %d found.\n"), g.foundcount);

			wlsWriteCurrentFieldToFile(NULL, g.outputfile, TRUE);
			if (!g.stoponfound) continue;
			goto done;
		}

		if (g.combine) {
			wlsCopyCombineToTmpfield(ctx);
			wlsRepaintCells_Sync(ctx,FALSE);
			if (g.combining) {
				reset = (g.combinedcells == 0);
				wlsMessagef(ctx,_T("Search completed: %d cell%s found"),
					g.combinedcells, (g.combinedcells == 1) ? _T("") : _T("s"));
			}
			else {
				reset = 1;
				wlsMessagef(ctx,_T("Search completed: no solution"));
			}
		}
		else {
			wlsUpdateProgressCounter();
			wlsUpdateAndShowTmpField_Sync();
			reset = (g.foundcount == 0);
			wlsMessagef(ctx,_T("Search completed:  %d object%s found"),
				g.foundcount, (g.foundcount == 1) ? _T("") : _T("s"));
		}
		if(reset) {
			wlsStatusf(ctx,_T(""));
		}
		else {
			wlsStatusf(ctx,_T("Search complete."));
		}

		goto done;
	}
done:
	if (reset) {
		// Request that the main thread perform a search-reset.
		ctx->thread_stop_flags |= WLS_THREADFLAG_RESETREQ;
	}
	ctx->searchstate = WLS_SRCH_PAUSED;
	PostMessage(ctx->hwndFrame,WLS_WM_THREADDONE,0,0);
	_endthreadex(0);
	return 0;
}

#endif

static int wlsWLSPriorityToWindowsPriority(int wlsprio)
{
	switch(wlsprio) {
	case WLS_PRIORITY_IDLE: return THREAD_PRIORITY_IDLE;
	case WLS_PRIORITY_NORMAL: return THREAD_PRIORITY_NORMAL;
	}
	return THREAD_PRIORITY_BELOW_NORMAL;
}

#ifdef JS

static void resume_search(struct wcontext *ctx)
{
	DWORD threadid;

	wlsStatusf(ctx,_T("Searching\x2026"));

	if(ctx->searchstate!=WLS_SRCH_PAUSED) {
		wlsErrorf(ctx,_T("No search is paused"));
		return;
	}
	ctx->deferred_action = 0;
	abortthread=0;

	ctx->searchstate=WLS_SRCH_RUNNING;
	ctx->hthread=(HANDLE)_beginthreadex(NULL,0,search_thread,(void*)0,0,&threadid);


	if(ctx->hthread==NULL) {
		wlsErrorf(ctx,_T("Unable to create search thread"));
		ctx->searchstate=WLS_SRCH_PAUSED;
	}
	else {
		wlsUpdateProgressCounter();

		// Request that the computer not go to sleep while we're searching.
		SetThreadExecutionState(ES_CONTINUOUS | ES_SYSTEM_REQUIRED);

		SetThreadPriority(ctx->hthread,wlsWLSPriorityToWindowsPriority(ctx->search_priority));
	}

}

#else

static void resume_search(struct wcontext *ctx)
{
	DWORD threadid;

	if (g.combine) {
		if (g.combining) {
			wlsStatusf(ctx,_T("Combine: %d cells remaining"), g.combinedcells);
		}
		else {
			wlsStatusf(ctx,_T("Combining\x2026"));
		}
	}
	else {
		wlsStatusf(ctx,_T("Searching\x2026"));
	}

	if(ctx->searchstate!=WLS_SRCH_PAUSED) {
		wlsErrorf(ctx,_T("No search is paused"));
		return;
	}
	ctx->deferred_action = 0;
	abortthread=0;

	ctx->searchstate=WLS_SRCH_RUNNING;
	ctx->hthread=(HANDLE)_beginthreadex(NULL,0,search_thread,(void*)0,0,&threadid);


	if(ctx->hthread==NULL) {
		wlsErrorf(ctx,_T("Unable to create search thread"));
		ctx->searchstate=WLS_SRCH_PAUSED;
	}
	else {
		wlsUpdateProgressCounter();

		// Request that the computer not go to sleep while we're searching.
		SetThreadExecutionState(ES_CONTINUOUS | ES_SYSTEM_REQUIRED);

		SetThreadPriority(ctx->hthread,wlsWLSPriorityToWindowsPriority(ctx->search_priority));
	}

}

#endif

static BOOL prepare_search(struct wcontext *ctx, BOOL load)
{
	int i;
	HCURSOR prevcur = NULL;
	BOOL retval = FALSE;

	if(ctx->searchstate!=WLS_SRCH_OFF) {
		wlsErrorf(ctx,_T("A search is already running"));
		goto done;
	}

	if (!setrules(g.rulestring)) {
		wlsErrorf(ctx,_T("Cannot set Life rules!"));
		goto done;
	}

	// set the variables that dbell's code uses
	g.coltrans= -g.trans_x;
	g.rowtrans= -g.trans_y;
	g.flipquads= g.trans_rotate%2;
	g.fliprows= (g.trans_rotate>=2);
	g.flipcols= (g.trans_flip==0 && g.trans_rotate>=2) ||
		      (g.trans_flip==1 && g.trans_rotate<2);

	// init some things that the orig code doesn't bother to init
	for(i=0;i<COLMAX;i++) {
		g.colinfo[i].oncount=0;
		g.colinfo[i].setcount=0;
		g.colinfo[i].sumpos=0;
	}

	for(i=0;i<ROWMAX;i++) { g.rowinfo[i].oncount=0; }

	g.newset=NULL;
	g.nextset=NULL;
	g.outputlastcols=0;
	g.fullcolumns=0;
	g.curstatus=OK;

#ifdef JS
	g.baseset=NULL;
	g.fullsearchlist=NULL;
#else
	g.g0oncellcount = 0; // KAS
	g.cellcount = 0; // KAS
	g.smartchoice = UNK; // KAS

	g.foundcount=0;
	g.writecount=0;

	g.combining = FALSE;
	g.combinedcells = 0;
	g.setcombinedcells = 0;
	g.differentcombinedcells = 0;
#endif

	g.viewcount = -1;
	wlsUpdateProgressCounter();

	if (load) {
		if(!loadstate(ctx->hwndFrame)) {
			wlsRepaintCells(ctx,TRUE);
			goto done;
		}

		ctx->searchstate=WLS_SRCH_PAUSED;
		g.curgen = 0;
		wlsUpdateAndShowTmpField();
		draw_gen_counter(ctx);
	}
	else {
		wlsStatusf(ctx,_T("Preparing search\x2026"));
		prevcur=SetCursor(LoadCursor(NULL,IDC_WAIT));

#ifdef JS
		g.inited=FALSE;
#endif
		if(!initcells()) {
			goto done;
		}

#ifdef JS
		g.baseset = g.nextset;
		g.inited = TRUE;
#endif

		if(!set_initial_cells()) {
			record_malloc(0,NULL); // release allocated memory
			goto done;   // there was probably an inconsistency in the initial cells
		}
#ifdef JS
		g.foundcount=0;
#else
		initsearchorder();
#endif
	}

	ctx->searchstate=WLS_SRCH_PAUSED;  // pretend the search is "paused"

#ifndef JS
	wlsUpdateAndShowTmpField();
#endif
	retval = TRUE;

done:
	wlsStatusf(ctx,_T(""));
	if(prevcur) SetCursor(prevcur);
	return retval;
}

static void start_search(struct wcontext *ctx)
{
	if (prepare_search(ctx,FALSE)) {
		resume_search(ctx);
	}
}

// Low-level function that ends the search thread(s), and does no UI.
static void wlsRequestPauseSearch(struct wcontext *ctx)
{
	if(ctx->searchstate!=WLS_SRCH_RUNNING || !ctx->hthread) {
		return;
	}
	abortthread=1;
}

static void wlsResetSearch(struct wcontext *ctx);
static void open_state(struct wcontext *ctx);

static void wlsOnThreadDone(struct wcontext *ctx)
{
	if(ctx->hthread) {
		CloseHandle(ctx->hthread);
		ctx->hthread = NULL;
	}
	ctx->searchstate=WLS_SRCH_PAUSED;

	// Tell Windows we're okay with the computer going to sleep.
	SetThreadExecutionState(ES_CONTINUOUS);

	if(ctx->deferred_action & WLS_ACTION_APPEXIT) {
		// Thread was stopped because the application wants to exit.
		// It's our job to actually make that happen.
		// Don't do any unnecessary UI; just destroy the main window and
		// get out of here.
		DestroyWindow(ctx->hwndFrame);
		return;
	}
	else if(ctx->deferred_action & WLS_ACTION_SAVEANDRESUME) {
#ifndef JS
		dumpstate(ctx->hwndFrame, NULL, FALSE);
		resume_search(ctx);
#endif
	}
	else if(ctx->deferred_action & WLS_ACTION_OPENSTATE) {
		wlsResetSearch(ctx);
		open_state(ctx);
	}
	else if((ctx->deferred_action & WLS_ACTION_RESETSEARCH) ||
		(ctx->thread_stop_flags & WLS_THREADFLAG_RESETREQ))
	{
		wlsResetSearch(ctx);
	}
	else {
		if(ctx->thread_stop_flags & WLS_THREADFLAG_ABORTED) {
			wlsStatusf(ctx,_T("Search paused."));
		}
	}

	if(ctx->deferred_action & WLS_ACTION_UPDATEDISPLAY) {
		wlsUpdateProgressCounter();
		wlsUpdateAndShowTmpField();
	}
}

static void wlsRequestEndApp(struct wcontext *ctx)
{
	if(ctx->searchstate!=WLS_SRCH_RUNNING || !ctx->hthread) {
		// If no other threads exist, exit immediately.
		DestroyWindow(ctx->hwndFrame);
		return;
	}
	// Another thread is still running.
	// We can't destroy the main window first, because the other thread may need
	// to send a message to it before it reaches a point where it can stop.
	// We can't wait for it using WaitForSingleObject() in this thread, because
	// that will deadlock if the other thread tries to send a message to us.
	// So we will set a flag indicating that an application exit is desired, ask
	// the thread to cancel, and that's all. When the thread ends, it will post
	// a message to us. At that time we'll check for the global flag, and if it's
	// set, exit for real.
	wlsStatusf(ctx,_T("Stopping search thread\x2026"));
	ctx->deferred_action = WLS_ACTION_APPEXIT;
	wlsRequestPauseSearch(ctx);
}

static void wlsResetSearch(struct wcontext *ctx)
{
	if(ctx->searchstate==WLS_SRCH_OFF) {
		wlsErrorf(ctx,_T("No search in progress"));
		goto here;
	}

	ctx->searchstate=WLS_SRCH_OFF;

	free(g.settable); g.settable=NULL;
	free(g.celltable); g.celltable=NULL;
	free(g.auxtable); g.auxtable=NULL;
#ifndef JS
	free(g.searchtable); g.searchtable=NULL;
#endif

	record_malloc(0,NULL);    // free memory

here:
	wlsRepaintCells(ctx,FALSE);
	SetWindowText(ctx->hwndFrame,WLS_APPNAME);
	wlsStatusf(ctx,_T(""));
}

static void wlsRequestResetSearch(struct wcontext *ctx)
{
	// stop the search thread if it is running
	if(ctx->searchstate==WLS_SRCH_RUNNING) {
		ctx->deferred_action |= WLS_ACTION_RESETSEARCH;
		wlsRequestPauseSearch(ctx);
	}
	else if(ctx->searchstate==WLS_SRCH_PAUSED) {
		wlsResetSearch(ctx);
	}
	else {
		wlsErrorf(ctx,_T("No search in progress"));
	}
}


static void open_state(struct wcontext *ctx)
{
	if(ctx->searchstate==WLS_SRCH_RUNNING) {
		ctx->deferred_action |= WLS_ACTION_OPENSTATE;
		wlsRequestPauseSearch(ctx);
		return;
	}
	else if(ctx->searchstate==WLS_SRCH_PAUSED) {
		wlsResetSearch(ctx);
	}

	prepare_search(ctx,TRUE);
}

static void gen_changeby(struct wcontext *ctx, int delta)
{
	if(g.period<2) return;
	g.curgen+=delta;
	if(g.curgen>=g.period) g.curgen=0;
	if(g.curgen<0) g.curgen=g.period-1;

	draw_gen_counter(ctx);
	wlsRepaintCells(ctx,FALSE);
}

static void hide_selection(struct wcontext *ctx)
{
	SelectOff(ctx,NULL);
}

static void clear_gen(int g1)
{
	wlsFillField_Gen(g.field,g1,CV_CLEAR);
}

static void clear_all(struct wcontext *ctx)
{
	wlsFillField_All(g.field,CV_CLEAR);
	hide_selection(ctx);
}

static void flip_h(struct wcontext *ctx, int fromgen, int togen)
{
	int g1, r, c;
	int fromrow, torow, fromcol, tocol;
	WLS_CELLVAL buffer;

	if (ctx->selectstate == WLS_SEL_SELECTED) {
		fromcol = ctx->selectrect.left;
		tocol = ctx->selectrect.right;
		fromrow = ctx->selectrect.top;
		torow = ctx->selectrect.bottom;
	}
	else {
		fromcol = 0;
		tocol = g.ncols - 1;
		fromrow = 0;
		torow = g.nrows - 1;
	}

	for (g1 = fromgen; g1 <= togen; ++g1) {
		for (r = fromrow; r <= torow; ++r) {
			for (c = (fromcol + tocol) / 2; c >= fromcol; --c) {
				buffer = wlsCellVal(g.field,g1,c,r);
				wlsSetCellVal(g.field,g1,c,r,wlsCellVal(g.field,g1,tocol + fromcol - c,r));
				wlsSetCellVal(g.field,g1,tocol + fromcol - c,r,buffer);
			}
		}
	}
}

static void flip_v(struct wcontext *ctx, int fromgen, int togen)
{
	int g1, r, c;
	int fromrow, torow, fromcol, tocol;
	WLS_CELLVAL buffer;

	if (ctx->selectstate == WLS_SEL_SELECTED) {
		fromcol = ctx->selectrect.left;
		tocol = ctx->selectrect.right;
		fromrow = ctx->selectrect.top;
		torow = ctx->selectrect.bottom;
	}
	else {
		fromcol = 0;
		tocol = g.ncols - 1;
		fromrow = 0;
		torow = g.nrows - 1;
	}

	for (g1 = fromgen; g1 <= togen; ++g1) {
		for (r = (fromrow + torow) / 2; r >= fromrow; --r) {
			for (c = fromcol; c <= tocol; ++c) {
				buffer = wlsCellVal(g.field,g1,c,r);
				wlsSetCellVal(g.field,g1,c,r,wlsCellVal(g.field,g1,c,torow + fromrow - r));
				wlsSetCellVal(g.field,g1,c,torow + fromrow - r,buffer);
			}
		}
	}
}

static void transpose(struct wcontext *ctx, int fromgen, int togen)
{
	int g1, r, c;
	int fromrow, torow, fromcol, tocol;
	WLS_CELLVAL buffer;

	if (ctx->selectstate == WLS_SEL_SELECTED) {
		fromcol = ctx->selectrect.left;
		tocol = ctx->selectrect.right;
		fromrow = ctx->selectrect.top;
		torow = ctx->selectrect.bottom;
	}
	else {
		fromcol = 0;
		tocol = g.ncols - 1;
		fromrow = 0;
		torow = g.nrows - 1;
	}

	if ((fromcol - tocol) != (fromrow - torow)) {
		wlsErrorf(ctx,_T("Can only transpose square regions"));
		return;
	}

	for (g1 = fromgen; g1 <= togen; ++g1) {
		for (r = fromrow + 1; r <= torow; ++r) {
			for (c = fromcol; c < fromcol + (r - fromrow); ++c) {
				buffer = wlsCellVal(g.field,g1,c,r);
				wlsSetCellVal(g.field,g1,c,r,wlsCellVal(g.field,g1,fromcol - fromrow + r,fromrow - fromcol + c));
				wlsSetCellVal(g.field,g1,fromcol - fromrow + r,fromrow - fromcol + c,buffer);
			}
		}
	}
}

static void shift_gen(struct wcontext *ctx, int fromgen, int togen, int gend, int cold, int rowd)
{
	int g1,r,c;
	int fromrow, torow, fromcol, tocol;
	int gx,rx,cx;

	if (ctx->selectstate == WLS_SEL_SELECTED) {
		fromcol = ctx->selectrect.left;
		tocol = ctx->selectrect.right;
		fromrow = ctx->selectrect.top;
		torow = ctx->selectrect.bottom;
	}
	else {
		fromcol = 0;
		tocol = g.ncols - 1;
		fromrow = 0;
		torow = g.nrows - 1;
	}

	// Make a temporary copy of the relevant cells.
	for(g1=fromgen; g1<=togen; g1++) {
		for (r=fromrow; r<=torow; r++) {
			for(c=fromcol; c<=tocol; c++) {
				wlsSetCellVal(g.tmpfield,g1,c,r,wlsCellVal(g.field,g1,c,r));
			}
		}
	}

	for(g1=fromgen; g1<=togen; g1++) {
		for (r=fromrow; r<=torow; r++) {
			for(c=fromcol; c<=tocol; c++) {
				gx = (g1 + gend - fromgen + togen - fromgen + 1) % (togen - fromgen + 1) + fromgen;
				cx = (c + cold - fromcol + tocol - fromcol + 1) % (tocol - fromcol + 1) + fromcol;
				rx = (r + rowd - fromrow + torow - fromrow + 1) % (torow - fromrow + 1) + fromrow;
				wlsSetCellVal(g.field,gx,cx,rx,wlsCellVal(g.tmpfield,g1,c,r));
			}
		}
	}
}

#ifndef JS

static void copy_result(struct wcontext *ctx)
{
	if (ctx->searchstate == WLS_SRCH_OFF) return;

	EnterCriticalSection(&ctx->critsec_tmpfield);
	wlsCopyField(g.tmpfield,g.field);
	LeaveCriticalSection(&ctx->critsec_tmpfield);

	wlsRequestResetSearch(ctx);
}

static void copy_combination(struct wcontext *ctx)
{
	if (ctx->searchstate == WLS_SRCH_OFF) return;

	EnterCriticalSection(&ctx->critsec_tmpfield);
	wlsCopyCombineToTmpfield(ctx);
	wlsCopyField(g.tmpfield,g.field);
	LeaveCriticalSection(&ctx->critsec_tmpfield);

	wlsRequestResetSearch(ctx);
}

static void clear_combination(struct wcontext *ctx)
{
	if (ctx->searchstate == WLS_SRCH_OFF) return;

	g.combining = 0;

	wlsRequestPauseSearch(ctx);
}

#endif

static void copytoclipboard(struct wcontext *ctx, struct field_struct *field)
{
	DWORD size;
	HGLOBAL hClip;
	LPVOID lpClip;
	TCHAR buf[100],buf2[10];
	TCHAR *s;
	int i,j;
	int adj_i, adj_j;
	int offset;

	if(ctx->searchstate==WLS_SRCH_OFF) {
		// bornrules/liverules may not be set up yet
		// probably we could call setrules().
		StringCbCopy(buf,sizeof(buf),_T("#P 0 0\r\n"));
	}
	else {
		// unfortunately the rulestring is in the wrong format for life32
		StringCbCopy(buf,sizeof(buf),_T("#P 0 0\r\n#R S"));
		for(i=0;i<=8;i++) {
			if(g.liverules[i]) { StringCbPrintf(buf2,sizeof(buf2),_T("%d"),i); StringCbCat(buf,sizeof(buf),buf2); }
		}
		StringCbCat(buf,sizeof(buf),_T("/B"));
		for(i=0;i<=8;i++) {
			if(g.bornrules[i]) { StringCbPrintf(buf2,sizeof(buf2),_T("%d"),i); StringCbCat(buf,sizeof(buf),buf2); }
		}
		StringCbCat(buf,sizeof(buf),_T("\r\n"));
	}

	offset=lstrlen(buf);

	size=(offset+(g.ncols+2)*g.nrows+1)*sizeof(TCHAR);
	hClip=GlobalAlloc(GMEM_MOVEABLE|GMEM_DDESHARE,size);

	lpClip=GlobalLock(hClip);
	s=(TCHAR*)lpClip;

	StringCbCopy(s,size,buf);

	for(j=0;j<g.nrows;j++) {
		for(i=0;i<g.ncols;i++) {
			adj_i=i; adj_j=j;

#ifdef JS
			// A hack to properly copy cells when fast-symmetry is in use.
			if(g.fastsym && ctx->searchstate!=WLS_SRCH_OFF) {
				if(g.symmetry==2) {
					if(j < (g.nrows - g.nrows%2)/2) {
						adj_j = g.nrows - 1 - j;
					}
				}
				else if(g.symmetry==4) {
					if(j<i) {
						adj_i = j;
						adj_j = i;
					}
				}
			}
#endif

			if(wlsCellVal(field,g.curgen,adj_i,adj_j)==CV_FORCEDON)
				s[offset+(g.ncols+2)*j+i]='*';
			else
				s[offset+(g.ncols+2)*j+i]='.';
		}
		s[offset+(g.ncols+2)*j+g.ncols]='\r';
		s[offset+(g.ncols+2)*j+g.ncols+1]='\n';
	}
	s[offset+(g.ncols+2)*g.nrows]='\0';

	OpenClipboard(NULL);
	EmptyClipboard();
#ifdef UNICODE
	SetClipboardData(CF_UNICODETEXT,hClip);
#else
	SetClipboardData(CF_TEXT,hClip);
#endif
	CloseClipboard();
}

static void Handle_MouseWheel(struct wcontext *ctx, HWND hWnd, WPARAM wParam)
{
	signed short delta = (signed short)(HIWORD(wParam));
	while(delta >= 120) {
		gen_changeby(ctx, (ctx->wheel_gen_dir==2) ? 1 : -1);
		delta -= 120;
	}
	while(delta <= -120) {
		gen_changeby(ctx, (ctx->wheel_gen_dir==2) ? -1 : 1);
		delta += 120;
	}
}

static void Handle_Copy(struct wcontext *ctx)
{
	if(ctx->searchstate == WLS_SRCH_PAUSED) {
		copytoclipboard(ctx,g.tmpfield);
	}
	else if(ctx->searchstate == WLS_SRCH_RUNNING) {
		EnterCriticalSection(&ctx->critsec_tmpfield);
		copytoclipboard(ctx,g.tmpfield);
		LeaveCriticalSection(&ctx->critsec_tmpfield);
	}
	else {
		copytoclipboard(ctx,g.field);
	}
}

static void Handle_Save(struct wcontext *ctx)
{
#ifdef JS
	if(ctx->searchstate==WLS_SRCH_PAUSED)
		dumpstate(ctx->hwndFrame, NULL);
#else
	if (ctx->searchstate==WLS_SRCH_OFF) {
		if (prepare_search(ctx,FALSE)) {
			dumpstate(ctx->hwndFrame, NULL, FALSE);
			wlsResetSearch(ctx);
		}
	}
	else if (ctx->searchstate==WLS_SRCH_PAUSED) {
		dumpstate(ctx->hwndFrame, NULL, FALSE);
	}
	else if (ctx->searchstate==WLS_SRCH_RUNNING) {
		ctx->deferred_action |= WLS_ACTION_SAVEANDRESUME;
		wlsRequestPauseSearch(ctx);
	}
#endif
}

// If a search is running or paused, UpdateDisplay forces a display of the
// current state of the search (as opposed to the most recent progress
// report).
// If no search is active, it just repaints the display.
static void Handle_UpdateDisplay(struct wcontext *ctx)
{
	if(ctx->searchstate==WLS_SRCH_OFF) {
		wlsRepaintCells(ctx,TRUE);
		return;
	}
	wlsUpdateProgressCounter();
	wlsUpdateAndShowTmpField_Sync();
}

static LRESULT CALLBACK WndProcFrame(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
	WORD id;
	DWORD dwtmp;
	POINT pt;
	struct wcontext *ctx = gctx;

	id=LOWORD(wParam);

	switch(msg) {

	case WM_MOUSEWHEEL:
		Handle_MouseWheel(ctx, hWnd, wParam);
		return 0;

	case WM_CREATE:
#ifdef _DEBUG
		ctx->pens.celloutline= CreatePen(PS_SOLID,0,RGB(0xc0,0x00,0x00));
#else
		ctx->pens.celloutline= CreatePen(PS_SOLID,0,RGB(0x00,0x00,0x00));
#endif
		ctx->pens.axes=        CreatePen(PS_DOT  ,0,RGB(0x00,0x00,0xff));
		ctx->pens.cell_off=    CreatePen(PS_SOLID,0,RGB(0x00,0x00,0xff));
		ctx->pens.arrow1=      CreatePen(PS_SOLID,2,RGB(0x00,0xff,0x00));
		ctx->pens.arrow2=      CreatePen(PS_SOLID,2,RGB(0xff,0x00,0x00));
		ctx->pens.unchecked=   CreatePen(PS_SOLID,0,RGB(0x00,0x00,0x00));
		ctx->brushes.cell=     CreateSolidBrush(RGB(0xc0,0xc0,0xc0));
		ctx->brushes.cell_on=  CreateSolidBrush(RGB(0x00,0x00,0xff));
		return 0;

	case WM_CLOSE:
		wlsRequestEndApp(ctx);
		return 0;

	case WM_DESTROY:
		DeleteObject(ctx->pens.celloutline);
		DeleteObject(ctx->pens.axes);
		DeleteObject(ctx->pens.cell_off);
		DeleteObject(ctx->pens.arrow1);
		DeleteObject(ctx->pens.arrow2);
		DeleteObject(ctx->pens.unchecked);
		DeleteObject(ctx->brushes.cell);
		DeleteObject(ctx->brushes.cell_on);
		PostQuitMessage(0);
		return 0;

	case WM_SIZE:
		// set size of children
		SetWindowPos(ctx->hwndMain,NULL,0,0,LOWORD(lParam),
			HIWORD(lParam)-TOOLBARHEIGHT,SWP_NOZORDER);
		SetWindowPos(ctx->hwndToolbar,NULL,0,HIWORD(lParam)-TOOLBARHEIGHT,
			LOWORD(lParam),TOOLBARHEIGHT,SWP_NOZORDER);
		return 0;

	case WM_INITMENU:
		EnableMenuItem((HMENU)wParam,IDC_SEARCHSTART,ctx->searchstate==WLS_SRCH_OFF?MF_ENABLED:MF_GRAYED);
		EnableMenuItem((HMENU)wParam,IDC_SEARCHRESET,ctx->searchstate!=WLS_SRCH_OFF?MF_ENABLED:MF_GRAYED);
		EnableMenuItem((HMENU)wParam,IDC_SEARCHPAUSE,ctx->searchstate==WLS_SRCH_RUNNING?MF_ENABLED:MF_GRAYED);
		EnableMenuItem((HMENU)wParam,IDC_SEARCHRESUME,ctx->searchstate==WLS_SRCH_PAUSED?MF_ENABLED:MF_GRAYED);
		EnableMenuItem((HMENU)wParam,IDC_SEARCHBACKUP,ctx->searchstate!=WLS_SRCH_OFF?MF_ENABLED:MF_GRAYED);
		EnableMenuItem((HMENU)wParam,IDC_SEARCHBACKUP2,ctx->searchstate!=WLS_SRCH_OFF?MF_ENABLED:MF_GRAYED);
		EnableMenuItem((HMENU)wParam,ID_HIDESEL,ctx->selectstate==WLS_SEL_SELECTED?MF_ENABLED:MF_GRAYED);
#ifdef JS
		EnableMenuItem((HMENU)wParam,IDC_SEARCHPREPARE,MF_GRAYED);
		EnableMenuItem((HMENU)wParam,IDC_COPYRESULT,MF_GRAYED);
		EnableMenuItem((HMENU)wParam,IDC_COPYCOMBINATION,MF_GRAYED);
		EnableMenuItem((HMENU)wParam,IDC_CLEARCOMBINATION,MF_GRAYED);
#else
		EnableMenuItem((HMENU)wParam,IDC_SEARCHPREPARE,ctx->searchstate==WLS_SRCH_OFF?MF_ENABLED:MF_GRAYED);
		EnableMenuItem((HMENU)wParam,IDC_COPYRESULT,ctx->searchstate!=WLS_SRCH_OFF?MF_ENABLED:MF_GRAYED);
		EnableMenuItem((HMENU)wParam,IDC_COPYCOMBINATION,ctx->searchstate!=WLS_SRCH_OFF?MF_ENABLED:MF_GRAYED);
		EnableMenuItem((HMENU)wParam,IDC_CLEARCOMBINATION,ctx->searchstate!=WLS_SRCH_OFF?MF_ENABLED:MF_GRAYED);
#endif
		return 0;

	case WM_CHAR:
		dwtmp = GetMessagePos();
		pt.x = (LONG)(short)LOWORD(dwtmp);
		pt.y = (LONG)(short)HIWORD(dwtmp);
		ScreenToClient(hWnd,&pt);
		return Handle_UIEvent(ctx,msg,(WORD)pt.x,(WORD)pt.y,wParam);

	case WLS_WM_THREADDONE:
		wlsOnThreadDone(ctx);
		return 0;

	case WM_COMMAND:
		switch(id) {
		case IDC_EXIT:
			wlsRequestEndApp(ctx);
			return 0;
		case IDC_ABOUT:
			DialogBox(ctx->hInst,_T("DLGABOUT"),hWnd,DlgProcAbout);
			return 0;
		case IDC_PERIODROWSCOLS:
			DialogBox(ctx->hInst,_T("DLGPERIODROWSCOLS"),hWnd,DlgProcPeriodRowsCols);
			return 0;
		case IDC_SYMMETRY:
			DialogBox(ctx->hInst,_T("DLGSYMMETRY"),hWnd,DlgProcSymmetry);
			return 0;
		case IDC_TRANSLATE:
			DialogBox(ctx->hInst,_T("DLGTRANSLATE"),hWnd,DlgProcTranslate);
			return 0;
		case IDC_OUTPUTSETTINGS:
			DialogBox(ctx->hInst,_T("DLGSETTINGS"),hWnd,DlgProcOutput);
			return 0;
		case IDC_SEARCHSETTINGS:
			DialogBox(ctx->hInst,_T("DLGSEARCHSETTINGS"),hWnd,DlgProcSearch);
			return 0;
		case IDC_PREFERENCES:
			DialogBox(ctx->hInst,_T("DLGPREFERENCES"),hWnd,DlgProcPreferences);
			return 0;

		case IDC_SEARCHSTART:
			start_search(ctx);
			return 0;

#ifndef JS
		case IDC_SEARCHPREPARE:
			prepare_search(ctx,FALSE);
			return 0;
#endif

		case IDC_SEARCHPAUSE:
			wlsRequestPauseSearch(ctx);
			return 0;
		case IDC_SEARCHRESET:
			wlsRequestResetSearch(ctx);
			return 0;
		case IDC_SEARCHRESUME:
			resume_search(ctx);
			return 0;

		case IDC_SEARCHBACKUP:
			if (ctx->searchstate==WLS_SRCH_RUNNING) {
				// If a search is running, just pause it.
				// The user can just press 'b' again if he really wants
				// to back up from wherever the search happened to get
				// interrupted.
				ctx->deferred_action |= WLS_ACTION_UPDATEDISPLAY;
				wlsRequestPauseSearch(ctx);
			}
			else if(ctx->searchstate==WLS_SRCH_PAUSED) {
				getbackup("1 ");
			}
			return 0;

		case IDC_SEARCHBACKUP2:
			if (ctx->searchstate==WLS_SRCH_RUNNING) {
				ctx->deferred_action |= WLS_ACTION_UPDATEDISPLAY;
				wlsRequestPauseSearch(ctx);
			}
			else if(ctx->searchstate==WLS_SRCH_PAUSED) {
				getbackup("20 ");
			}
			return 0;

		case IDC_OPENSTATE:
			open_state(ctx);
			return 0;
		case IDC_SAVEGAME:
			Handle_Save(ctx);
			return 0;
		case IDC_NEXTGEN:
			gen_changeby(ctx,1);
			return 0;
		case IDC_PREVGEN:
			gen_changeby(ctx,-1);
			return 0;
		case IDC_UPDATEDISPLAY:
			Handle_UpdateDisplay(ctx);
			return 0;
		case IDC_EDITCOPY:
			Handle_Copy(ctx);
			return 0;
		case IDC_CLEARGEN:
			if(ctx->searchstate == WLS_SRCH_OFF) clear_gen(g.curgen);
			hide_selection(ctx);
			wlsRepaintCells(ctx,FALSE);
			return 0;
		case IDC_CLEAR:
			if(ctx->searchstate == WLS_SRCH_OFF) clear_all(ctx);
			wlsRepaintCells(ctx,FALSE);
			return 0;

#ifndef JS
		case IDC_COPYRESULT:
			if(ctx->searchstate != WLS_SRCH_OFF) copy_result(ctx);
			return 0;
		case IDC_COPYCOMBINATION:
			if (ctx->searchstate != WLS_SRCH_OFF) copy_combination(ctx);
			return 0;
		case IDC_CLEARCOMBINATION:
			if (ctx->searchstate != WLS_SRCH_OFF) clear_combination(ctx);
			return 0;
#endif
		case IDC_SHIFTGUP:
			if(ctx->searchstate == WLS_SRCH_OFF) shift_gen(ctx, g.curgen, g.curgen, 0, 0, -1);
			wlsRepaintCells(ctx,ctx->selectstate == WLS_SEL_SELECTED);
			return 0;
		case IDC_SHIFTGDOWN:
			if(ctx->searchstate == WLS_SRCH_OFF) shift_gen(ctx, g.curgen, g.curgen, 0, 0, 1);
			wlsRepaintCells(ctx,ctx->selectstate == WLS_SEL_SELECTED);
			return 0;
		case IDC_SHIFTGLEFT:
			if(ctx->searchstate == WLS_SRCH_OFF) shift_gen(ctx, g.curgen, g.curgen, 0, -1, 0);
			wlsRepaintCells(ctx,ctx->selectstate == WLS_SEL_SELECTED);
			return 0;
		case IDC_SHIFTGRIGHT:
			if(ctx->searchstate == WLS_SRCH_OFF) shift_gen(ctx, g.curgen, g.curgen, 0, 1, 0);
			wlsRepaintCells(ctx,ctx->selectstate == WLS_SEL_SELECTED);
			return 0;
		case IDC_SHIFTAUP:
			if(ctx->searchstate == WLS_SRCH_OFF) shift_gen(ctx, 0, g.period-1, 0, 0, -1);
			wlsRepaintCells(ctx,ctx->selectstate == WLS_SEL_SELECTED);
			return 0;
		case IDC_SHIFTADOWN:
			if(ctx->searchstate == WLS_SRCH_OFF) shift_gen(ctx, 0, g.period-1, 0, 0, 1);
			wlsRepaintCells(ctx,ctx->selectstate == WLS_SEL_SELECTED);
			return 0;
		case IDC_SHIFTALEFT:
			if(ctx->searchstate == WLS_SRCH_OFF) shift_gen(ctx, 0, g.period-1, 0, -1, 0);
			wlsRepaintCells(ctx,ctx->selectstate == WLS_SEL_SELECTED);
			return 0;
		case IDC_SHIFTARIGHT:
			if(ctx->searchstate == WLS_SRCH_OFF) shift_gen(ctx, 0, g.period-1, 0, 1, 0);
			wlsRepaintCells(ctx,ctx->selectstate == WLS_SEL_SELECTED);
			return 0;
		case IDC_SHIFTAPAST:
			if(ctx->searchstate == WLS_SRCH_OFF) shift_gen(ctx, 0, g.period-1, -1, 0, 0);
			wlsRepaintCells(ctx,ctx->selectstate == WLS_SEL_SELECTED);
			return 0;
		case IDC_SHIFTAFUTURE:
			if(ctx->searchstate == WLS_SRCH_OFF) shift_gen(ctx, 0, g.period-1, 1, 0, 0);
			wlsRepaintCells(ctx,ctx->selectstate == WLS_SEL_SELECTED);
			return 0;
		case ID_FLIP_GEN_H:
			if(ctx->searchstate == WLS_SRCH_OFF) flip_h(ctx, g.curgen, g.curgen);
			wlsRepaintCells(ctx,ctx->selectstate == WLS_SEL_SELECTED);
			return 0;
		case ID_FLIP_GEN_V:
			if(ctx->searchstate == WLS_SRCH_OFF) flip_v(ctx, g.curgen, g.curgen);
			wlsRepaintCells(ctx,ctx->selectstate == WLS_SEL_SELECTED);
			return 0;
		case ID_FLIP_ALL_H:
			if(ctx->searchstate == WLS_SRCH_OFF) flip_h(ctx, 0, g.period - 1);
			wlsRepaintCells(ctx,ctx->selectstate == WLS_SEL_SELECTED);
			return 0;
		case ID_FLIP_ALL_V:
			if(ctx->searchstate == WLS_SRCH_OFF) flip_v(ctx, 0, g.period - 1);
			wlsRepaintCells(ctx,ctx->selectstate == WLS_SEL_SELECTED);
			return 0;
		case ID_TRANS_GEN:
			if(ctx->searchstate == WLS_SRCH_OFF) transpose(ctx, g.curgen, g.curgen);
			wlsRepaintCells(ctx,ctx->selectstate == WLS_SEL_SELECTED);
			return 0;
		case ID_TRANS_ALL:
			if(ctx->searchstate == WLS_SRCH_OFF) transpose(ctx, 0, g.period - 1);
			wlsRepaintCells(ctx,ctx->selectstate == WLS_SEL_SELECTED);
			return 0;
		case ID_HIDESEL:
			hide_selection(ctx);
			return 0;
		}
		break;
	}

	return DefWindowProc(hWnd, msg, wParam, lParam);
}

static void Handle_Scroll(struct wcontext *ctx, UINT msg, WORD scrollboxpos, WORD id)
{
	POINT oldscrollpos;

	oldscrollpos = ctx->scrollpos;

	if(msg==WM_HSCROLL) {
		switch(id) {
		case SB_LINELEFT: ctx->scrollpos.x-=ctx->cellwidth; break;
		case SB_LINERIGHT: ctx->scrollpos.x+=ctx->cellwidth; break;
		case SB_PAGELEFT: ctx->scrollpos.x-=ctx->cellwidth*4; break;
		case SB_PAGERIGHT: ctx->scrollpos.x+=ctx->cellwidth*4; break;
		case SB_THUMBTRACK:
			if(scrollboxpos==ctx->scrollpos.x) return;
			ctx->scrollpos.x=scrollboxpos;
			break;
		default:
			return;
		}
	}
	else { // WM_VSCROLL
		switch(id) {
		case SB_LINELEFT: ctx->scrollpos.y-=ctx->cellheight; break;
		case SB_LINERIGHT: ctx->scrollpos.y+=ctx->cellheight; break;
		case SB_PAGELEFT: ctx->scrollpos.y-=ctx->cellheight*4; break;
		case SB_PAGERIGHT: ctx->scrollpos.y+=ctx->cellheight*4; break;
		case SB_THUMBTRACK:
			if(scrollboxpos==ctx->scrollpos.y) return;
			ctx->scrollpos.y=scrollboxpos;
			break;
		default:
			return;
		}
	}

	fix_scrollpos(ctx);

	if(ctx->scrollpos.x==oldscrollpos.x && ctx->scrollpos.y==oldscrollpos.y) return;

	ScrollWindowEx(ctx->hwndMain,
		oldscrollpos.x-ctx->scrollpos.x,
		oldscrollpos.y-ctx->scrollpos.y,
		NULL,NULL,NULL,NULL,SW_INVALIDATE|SW_ERASE);

	set_main_scrollbars(ctx, 0, 0);
}

static LRESULT CALLBACK WndProcMain(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
	WORD id;
	struct wcontext *ctx = gctx;

	id=LOWORD(wParam);

	switch(msg) {
	case WM_PAINT:
		if(ctx->searchstate==WLS_SRCH_OFF) {
			PaintWindow(ctx,hWnd,g.field);
		}
		else {
			// There is a race condition here if a search is running,
			// because it may be in the middle of updating tmpfield.
			// It should only cause cosmetic problems, though.
			PaintWindow(ctx,hWnd,g.tmpfield);
		}
		return 0;

	case WM_HSCROLL:
	case WM_VSCROLL:
		Handle_Scroll(ctx,msg,HIWORD(wParam),id);
		return 0;

	case WM_SIZE:
		set_main_scrollbars(ctx, 0, 1);
		return 0;
	case WM_LBUTTONDOWN:
	case WM_RBUTTONDOWN:
	case WM_LBUTTONUP:
	case WM_MOUSEMOVE:
		if(ctx->searchstate==WLS_SRCH_OFF) {
			Handle_UIEvent(ctx,msg,LOWORD(lParam),HIWORD(lParam),wParam);
		}
		return 0;
	}

	return DefWindowProc(hWnd, msg, wParam, lParam);
}

static void Handle_ToolbarCreate(struct wcontext *ctx, HWND hWnd, LPARAM lParam)
{
	CREATESTRUCT *cs;

	cs = (CREATESTRUCT*)lParam;
#define WLS_GENWINDOWWIDTH 54
	ctx->hwndGen=CreateWindow(_T("Static"),_T(""),
		WS_CHILD|WS_BORDER|SS_LEFTNOWORDWRAP|SS_NOPREFIX,
		1,1,WLS_GENWINDOWWIDTH,TOOLBARHEIGHT-2,
		hWnd,NULL,ctx->hInst,NULL);
	SendMessage(ctx->hwndGen,WM_SETFONT,(WPARAM)ctx->statusfont,(LPARAM)FALSE);
	ShowWindow(ctx->hwndGen,SW_SHOW);

	ctx->hwndGenScroll=CreateWindow(_T("Scrollbar"),_T("wls_gen_scrollbar"),
		WS_CHILD|WS_VISIBLE|SBS_HORZ,
		WLS_GENWINDOWWIDTH+1,1,80,TOOLBARHEIGHT-2,
		hWnd,NULL,ctx->hInst,NULL);

	ctx->hwndStatus=CreateWindow(_T("Static"),_T(""),
		WS_CHILD|WS_BORDER|SS_LEFTNOWORDWRAP|SS_NOPREFIX,
		WLS_GENWINDOWWIDTH+81,1,cs->cx-(WLS_GENWINDOWWIDTH+81),TOOLBARHEIGHT-2,
		hWnd,NULL,ctx->hInst,NULL);
	SendMessage(ctx->hwndStatus,WM_SETFONT,(WPARAM)ctx->statusfont,(LPARAM)FALSE);
	ShowWindow(ctx->hwndStatus,SW_SHOW);

#ifdef _DEBUG
		wlsStatusf(ctx,_T("DEBUG BUILD"));
#endif
		draw_gen_counter(ctx);
}

static void Handle_ToolbarSize(struct wcontext *ctx, HWND hWnd, LPARAM lParam)
{
	int newwidth = LOWORD(lParam);

	if(!ctx->hwndStatus) return;

	// Resize the status window accordingly
	SetWindowPos(ctx->hwndStatus,NULL,0,0,newwidth-(WLS_GENWINDOWWIDTH+81),TOOLBARHEIGHT-2,
		SWP_NOACTIVATE|SWP_NOMOVE|SWP_NOOWNERZORDER|SWP_NOZORDER);
}

static LRESULT CALLBACK WndProcToolbar(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
	HWND htmp;
	WORD id;
	int ori_gen;
	struct wcontext *ctx = gctx;

	id=LOWORD(wParam);

	switch(msg) {

	case WM_CREATE:
		Handle_ToolbarCreate(ctx,hWnd,lParam);
		return 0;

	case WM_SIZE:
		Handle_ToolbarSize(ctx,hWnd,lParam);
		return 0;

	case WM_HSCROLL:
		htmp=(HWND)(lParam);
		if(htmp==ctx->hwndGenScroll) {
			ori_gen=g.curgen;
			switch(id) {
			case SB_LINELEFT: g.curgen--; break;
			case SB_LINERIGHT: g.curgen++; break;
			case SB_PAGELEFT: g.curgen--; break;
			case SB_PAGERIGHT: g.curgen++; break;
			case SB_LEFT: g.curgen=0; break;
			case SB_RIGHT: g.curgen=g.period-1; break;
			case SB_THUMBPOSITION: g.curgen=HIWORD(wParam); break;
			case SB_THUMBTRACK: g.curgen=HIWORD(wParam); break;
			}
			if(g.curgen<0) g.curgen=g.period-1;  // wrap around
			if(g.curgen>=g.period) g.curgen=0;
			if(g.curgen!=ori_gen) {
				draw_gen_counter(ctx);
				wlsRepaintCells(ctx,FALSE);
			}
			return 0;
		}
		break;
	}

	return DefWindowProc(hWnd, msg, wParam, lParam);
}

static INT_PTR CALLBACK DlgProcAbout(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
	WORD id;
	id=LOWORD(wParam);

	switch (msg) {
	case WM_INITDIALOG:
#ifdef JS
		SetDlgItemText(hWnd,IDC_ABOUTTEXT,WLS_APPNAME _T(" v") WLS_VERSION_STRING _T("\r\n\r\n")
			_T("A Windows port of David Bell\x2019s LIFESRC v3.5\r\n\r\n")
			_T("By Jason Summers"));
#else
		SetDlgItemText(hWnd,IDC_ABOUTTEXT,WLS_APPNAME _T(" v") WLS_VERSION_STRING _T("\r\n\r\n")
			_T("A Windows port of David Bell\x2019s LIFESRC v3.5\r\n\r\n")
			_T("By Jason Summers and Karel Suhajda"));
#endif
		return 1;  // Didn't call SetFocus

	case WM_COMMAND:
		if (id == IDOK || id == IDCANCEL) {
			EndDialog(hWnd, TRUE);
			return 1;
		}
		break;
	}
	return 0;  // Didn't process a message
}

static void wlsResizeField(void)
{
	g.field = wlsAllocField(g.field);
	wlsFreeField(g.tmpfield);
	g.tmpfield = wlsAllocField(NULL);
	RecalcCenter(gctx);
}

void wlsNewField(void)
{
	wlsFreeField(g.field);
	wlsFreeField(g.tmpfield);
	g.field = wlsAllocField(NULL);
	g.tmpfield = wlsAllocField(NULL);
	RecalcCenter(gctx);
}

static void Handle_PeriodRowsCols_OK(struct wcontext *ctx, HWND hWnd)
{
	int newperiod, newrows, newcols;

	newperiod = GetDlgItemInt(hWnd,IDC_PERIOD,NULL,FALSE);
	if(newperiod>GENMAX) newperiod=GENMAX;
	if(newperiod<1) newperiod=1;

	newcols = GetDlgItemInt(hWnd,IDC_COLUMNS,NULL,FALSE);
	newrows = GetDlgItemInt(hWnd,IDC_ROWS,NULL,FALSE);
	if(newcols<1) newcols=1;
	if(newrows<1) newrows=1;
	if(newcols>COLMAX) newcols=COLMAX;
	if(newrows>ROWMAX) newrows=ROWMAX;

	if(newperiod==g.period && newcols==g.ncols && newrows==g.nrows) {
		return;
	}

	g.period = newperiod;
	g.ncols = newcols;
	g.nrows = newrows;
	if(g.curgen>=g.period) g.curgen=g.period-1;

	// put these in a separate Validate function
	if(g.ncols!=g.nrows) {
		if(symmap[g.symmetry] & 0x66) {
			MessageBox(hWnd,_T("Current symmetry requires that rows and ")
				_T("columns be equal. Your symmetry setting has been altered."),
				_T("Warning"),MB_OK|MB_ICONWARNING);
			switch(g.symmetry) {
			case 3: case 4: g.symmetry=0; break;
			case 7: case 8: g.symmetry=5; break;
			case 9: g.symmetry=6;
			}
		}
	}

	if(g.ncols != g.nrows) {
		if(g.trans_rotate==1 || g.trans_rotate==3) {
			MessageBox(hWnd,_T("Current rotation setting requires that rows and ")
				_T("columns be equal. Your translation setting has been altered."),
				_T("Warning"),MB_OK|MB_ICONWARNING);
			g.trans_rotate--;
		}
	}

	wlsResizeField();

	draw_gen_counter(ctx);
	set_main_scrollbars(ctx, 1, 1);
}

static INT_PTR CALLBACK DlgProcPeriodRowsCols(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
	WORD id;
	TCHAR buf[80];
	struct wcontext *ctx = gctx;

	id=LOWORD(wParam);

	switch (msg) {
	case WM_INITDIALOG:
		StringCbPrintf(buf,sizeof(buf),_T("Period (1\x2013%d)"),GENMAX);
		SetDlgItemText(hWnd,IDC_PERIODTEXT,buf);
		SetDlgItemInt(hWnd,IDC_PERIOD,g.period,FALSE);
		SetDlgItemInt(hWnd,IDC_COLUMNS,g.ncols,FALSE);
		SetDlgItemInt(hWnd,IDC_ROWS,g.nrows,FALSE);
		return 1;

	case WM_COMMAND:
		switch(id) {
		case IDOK:
			Handle_PeriodRowsCols_OK(ctx,hWnd);
			EndDialog(hWnd, TRUE);
			return 1;
			
		case IDCANCEL:
			EndDialog(hWnd, TRUE);
			return 1;
		}
	}
	return 0;
}

static INT_PTR CALLBACK DlgProcOutput(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
	WORD id;

	id=LOWORD(wParam);

	switch (msg) {
	case WM_INITDIALOG:
		SetDlgItemInt(hWnd,IDC_VIEWFREQ,g.viewfreq,FALSE);
		SetDlgItemText(hWnd,IDC_DUMPFILE,g.dumpfile);
		SetDlgItemInt(hWnd,IDC_DUMPFREQ,g.dumpfreq,FALSE);
		CheckDlgButton(hWnd,IDC_OUTPUTFILEYN,g.saveoutput?BST_CHECKED:BST_UNCHECKED);
#ifndef JS
		CheckDlgButton(hWnd,IDC_OUTPUTALLGEN,g.saveoutputallgen?BST_CHECKED:BST_UNCHECKED);
		CheckDlgButton(hWnd,IDC_STOPONFOUND,g.stoponfound?BST_CHECKED:BST_UNCHECKED);
		CheckDlgButton(hWnd,IDC_STOPONSTEP,g.stoponstep?BST_CHECKED:BST_UNCHECKED);
#endif
		SetDlgItemText(hWnd,IDC_OUTPUTFILE,g.outputfile);
		SetDlgItemInt(hWnd,IDC_OUTPUTCOLS,g.outputcols,FALSE);
		return 1;

	case WM_COMMAND:
		switch(id) {
		case IDOK:
			g.viewfreq=GetDlgItemInt(hWnd,IDC_VIEWFREQ,NULL,FALSE);
			g.dumpfreq=GetDlgItemInt(hWnd,IDC_DUMPFREQ,NULL,FALSE);
			GetDlgItemText(hWnd,IDC_DUMPFILE,g.dumpfile,79);
			g.saveoutput=(IsDlgButtonChecked(hWnd,IDC_OUTPUTFILEYN)==BST_CHECKED);
#ifndef JS
			g.saveoutputallgen=(IsDlgButtonChecked(hWnd,IDC_OUTPUTALLGEN)==BST_CHECKED);
			g.stoponfound=(IsDlgButtonChecked(hWnd,IDC_STOPONFOUND)==BST_CHECKED);
			g.stoponstep=(IsDlgButtonChecked(hWnd,IDC_STOPONSTEP)==BST_CHECKED);
#endif
			GetDlgItemText(hWnd,IDC_OUTPUTFILE,g.outputfile,79);
			g.outputcols=GetDlgItemInt(hWnd,IDC_OUTPUTCOLS,NULL,FALSE);
			EndDialog(hWnd, TRUE);
			return 1;

		case IDCANCEL:
			EndDialog(hWnd, TRUE);
			return 1;
		}
	}
	return 0;
}

static INT_PTR CALLBACK DlgProcSearch(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
	WORD id;

	id=LOWORD(wParam);

	switch (msg) {
	case WM_INITDIALOG:
		CheckDlgButton(hWnd,IDC_ORDERWIDE,g.orderwide);
		CheckDlgButton(hWnd,IDC_ORDERGENS,g.ordergens);
		CheckDlgButton(hWnd,IDC_ORDERMIDDLE,g.ordermiddle);
		CheckDlgButton(hWnd,IDC_DIAGSORT,g.diagsort);
		CheckDlgButton(hWnd,IDC_KNIGHTSORT,g.knightsort);
#ifdef JS
		CheckDlgButton(hWnd,IDC_FASTSYM,g.fastsym);
#endif
		CheckDlgButton(hWnd,IDC_ALLOBJECTS,g.allobjects);
		CheckDlgButton(hWnd,IDC_PARENT,g.parent);
		CheckDlgButton(hWnd,IDC_FOLLOW,g.follow);
		CheckDlgButton(hWnd,IDC_FOLLOWGENS,g.followgens);
#ifndef JS
		CheckDlgButton(hWnd,IDC_SMART,g.smart);
		CheckDlgButton(hWnd,IDC_SMARTON,g.smarton);
		CheckDlgButton(hWnd,IDC_COMBINE,g.combine);
#endif
		SetDlgItemInt(hWnd,IDC_NEARCOLS,g.nearcols,TRUE);
		SetDlgItemInt(hWnd,IDC_USECOL,g.usecol,TRUE);
		SetDlgItemInt(hWnd,IDC_USEROW,g.userow,TRUE);
#ifndef JS
		SetDlgItemInt(hWnd,IDC_SMARTWINDOW,g.smartwindow,TRUE);
		SetDlgItemInt(hWnd,IDC_SMARTTHRESHOLD,g.smartthreshold,TRUE);
		SetDlgItemInt(hWnd,IDC_SMARTWINDOWSTAT,g.smartstatwnd,TRUE);
		SetDlgItemInt(hWnd,IDC_SMARTTHRESHOLDSTAT,g.smartstatlen,TRUE);
#endif

		SetDlgItemInt(hWnd,IDC_MAXCOUNT,g.maxcount,TRUE);
		SetDlgItemInt(hWnd,IDC_COLCELLS,g.colcells,TRUE);
		SetDlgItemInt(hWnd,IDC_COLWIDTH,g.colwidth,TRUE);

		SetDlgItemText(hWnd,IDC_RULESTRING,g.rulestring);
		return 1;

	case WM_COMMAND:
		switch(id) {
		case IDOK:
			g.orderwide=  IsDlgButtonChecked(hWnd,IDC_ORDERWIDE  )?1:0;
			g.ordergens=  IsDlgButtonChecked(hWnd,IDC_ORDERGENS  )?1:0;
			g.ordermiddle=IsDlgButtonChecked(hWnd,IDC_ORDERMIDDLE)?1:0;
			g.diagsort=   IsDlgButtonChecked(hWnd,IDC_DIAGSORT   )?1:0;
			g.knightsort= IsDlgButtonChecked(hWnd,IDC_KNIGHTSORT )?1:0;
#ifdef JS
			g.fastsym=    IsDlgButtonChecked(hWnd,IDC_FASTSYM )?1:0;
#endif
			g.allobjects= IsDlgButtonChecked(hWnd,IDC_ALLOBJECTS )?1:0;
			g.parent=     IsDlgButtonChecked(hWnd,IDC_PARENT     )?1:0;
			g.follow=     IsDlgButtonChecked(hWnd,IDC_FOLLOW     )?1:0;
			g.followgens= IsDlgButtonChecked(hWnd,IDC_FOLLOWGENS )?1:0;
#ifndef JS
			g.smart=      IsDlgButtonChecked(hWnd,IDC_SMART      )?1:0;
			g.smarton=    IsDlgButtonChecked(hWnd,IDC_SMARTON    )?1:0;
			g.combine=    IsDlgButtonChecked(hWnd,IDC_COMBINE    )?1:0;
#endif

			g.nearcols=GetDlgItemInt(hWnd,IDC_NEARCOLS,NULL,TRUE);
			g.usecol=GetDlgItemInt(hWnd,IDC_USECOL,NULL,TRUE);
			g.userow=GetDlgItemInt(hWnd,IDC_USEROW,NULL,TRUE);
			g.maxcount=GetDlgItemInt(hWnd,IDC_MAXCOUNT,NULL,TRUE);
			g.colcells=GetDlgItemInt(hWnd,IDC_COLCELLS,NULL,TRUE);
			g.colwidth=GetDlgItemInt(hWnd,IDC_COLWIDTH,NULL,TRUE);
#ifndef JS
			g.smartwindow=GetDlgItemInt(hWnd,IDC_SMARTWINDOW,NULL,TRUE);
			g.smartthreshold=GetDlgItemInt(hWnd,IDC_SMARTTHRESHOLD,NULL,TRUE);
#endif

			GetDlgItemText(hWnd,IDC_RULESTRING,g.rulestring,WLS_RULESTRING_LEN);
			EndDialog(hWnd, TRUE);
			return 1;

		case IDCANCEL:
			EndDialog(hWnd, TRUE);
			return 1;
		case IDC_CONWAY:
			SetDlgItemText(hWnd,IDC_RULESTRING,_T("B3/S23"));
			return 1;
		}
	}
	return 0;
}

static INT_PTR CALLBACK DlgProcSymmetry(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
	WORD id;
	int item;
	struct wcontext *ctx = gctx;

	id=LOWORD(wParam);

	switch (msg) {
	case WM_INITDIALOG:
		if(g.ncols!=g.nrows) {
			EnableWindow(GetDlgItem(hWnd,IDC_SYM3),FALSE);
			EnableWindow(GetDlgItem(hWnd,IDC_SYM4),FALSE);
			EnableWindow(GetDlgItem(hWnd,IDC_SYM7),FALSE);
			EnableWindow(GetDlgItem(hWnd,IDC_SYM8),FALSE);
			EnableWindow(GetDlgItem(hWnd,IDC_SYM9),FALSE);
		}
		switch(g.symmetry) {
		case 1: item=IDC_SYM1; break;
		case 2: item=IDC_SYM2; break;
		case 3: item=IDC_SYM3; break;
		case 4: item=IDC_SYM4; break;
		case 5: item=IDC_SYM5; break;
		case 6: item=IDC_SYM6; break;
		case 7: item=IDC_SYM7; break;
		case 8: item=IDC_SYM8; break;
		case 9: item=IDC_SYM9; break;
		default: item=IDC_SYM0;
		}
		CheckDlgButton(hWnd,item,BST_CHECKED);
		return 1;

	case WM_COMMAND:
		switch(id) {
		case IDOK:
			if(IsDlgButtonChecked(hWnd,IDC_SYM0)==BST_CHECKED) g.symmetry=0;
			if(IsDlgButtonChecked(hWnd,IDC_SYM1)==BST_CHECKED) g.symmetry=1;
			if(IsDlgButtonChecked(hWnd,IDC_SYM2)==BST_CHECKED) g.symmetry=2;
			if(IsDlgButtonChecked(hWnd,IDC_SYM3)==BST_CHECKED) g.symmetry=3;
			if(IsDlgButtonChecked(hWnd,IDC_SYM4)==BST_CHECKED) g.symmetry=4;
			if(IsDlgButtonChecked(hWnd,IDC_SYM5)==BST_CHECKED) g.symmetry=5;
			if(IsDlgButtonChecked(hWnd,IDC_SYM6)==BST_CHECKED) g.symmetry=6;
			if(IsDlgButtonChecked(hWnd,IDC_SYM7)==BST_CHECKED) g.symmetry=7;
			if(IsDlgButtonChecked(hWnd,IDC_SYM8)==BST_CHECKED) g.symmetry=8;
			if(IsDlgButtonChecked(hWnd,IDC_SYM9)==BST_CHECKED) g.symmetry=9;

			wlsRepaintCells(ctx,TRUE);
			EndDialog(hWnd, TRUE);
			return 1;

		case IDCANCEL:
			EndDialog(hWnd, TRUE);
			return 1;
		}
		break;
	}
	return 0;
}

static INT_PTR CALLBACK DlgProcTranslate(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
	WORD id;
	int item;
	struct wcontext *ctx = gctx;

	id=LOWORD(wParam);

	switch (msg) {
	case WM_INITDIALOG:
		SetDlgItemInt(hWnd,IDC_TRANSX,g.trans_x,TRUE);
		SetDlgItemInt(hWnd,IDC_TRANSY,g.trans_y,TRUE);

		if(g.ncols != g.nrows) {
			EnableWindow(GetDlgItem(hWnd,IDC_TRANS1),FALSE);
			EnableWindow(GetDlgItem(hWnd,IDC_TRANS3),FALSE);
			EnableWindow(GetDlgItem(hWnd,IDC_TRANS5),FALSE);
			EnableWindow(GetDlgItem(hWnd,IDC_TRANS7),FALSE);
		}

		switch(g.trans_rotate + 4*g.trans_flip) {
		case 1: item=IDC_TRANS1; break;
		case 2: item=IDC_TRANS2; break;
		case 3: item=IDC_TRANS3; break;
		case 4: item=IDC_TRANS4; break;
		case 5: item=IDC_TRANS5; break;
		case 6: item=IDC_TRANS6; break;
		case 7: item=IDC_TRANS7; break;
		default: item=IDC_TRANS0;
		}
		CheckDlgButton(hWnd,item,BST_CHECKED);
		return 1;

	case WM_COMMAND:
		switch(id) {
		case IDOK:
			g.trans_x=GetDlgItemInt(hWnd,IDC_TRANSX,NULL,TRUE);
			g.trans_y=GetDlgItemInt(hWnd,IDC_TRANSY,NULL,TRUE);
			if(g.trans_x>TRANSMAX) g.trans_x=TRANSMAX;
			if(g.trans_y>TRANSMAX) g.trans_y=TRANSMAX;
			if(g.trans_x< -TRANSMAX) g.trans_x= -TRANSMAX;
			if(g.trans_y< -TRANSMAX) g.trans_y= -TRANSMAX;

			if(IsDlgButtonChecked(hWnd,IDC_TRANS0)==BST_CHECKED) { g.trans_flip=0; g.trans_rotate=0; }
			if(IsDlgButtonChecked(hWnd,IDC_TRANS1)==BST_CHECKED) { g.trans_flip=0; g.trans_rotate=1; }
			if(IsDlgButtonChecked(hWnd,IDC_TRANS2)==BST_CHECKED) { g.trans_flip=0; g.trans_rotate=2; }
			if(IsDlgButtonChecked(hWnd,IDC_TRANS3)==BST_CHECKED) { g.trans_flip=0; g.trans_rotate=3; }
			if(IsDlgButtonChecked(hWnd,IDC_TRANS4)==BST_CHECKED) { g.trans_flip=1; g.trans_rotate=0; }
			if(IsDlgButtonChecked(hWnd,IDC_TRANS5)==BST_CHECKED) { g.trans_flip=1; g.trans_rotate=1; }
			if(IsDlgButtonChecked(hWnd,IDC_TRANS6)==BST_CHECKED) { g.trans_flip=1; g.trans_rotate=2; }
			if(IsDlgButtonChecked(hWnd,IDC_TRANS7)==BST_CHECKED) { g.trans_flip=1; g.trans_rotate=3; }

			wlsRepaintCells(ctx,TRUE);
			EndDialog(hWnd, TRUE);
			return 1;

		case IDCANCEL:
			EndDialog(hWnd, TRUE);
			return 1;
		}
		break;
	}
	return 0;
}

#define WLS_REGISTRY_KEY _T("SOFTWARE\\WinLifeSearch")

static void wlsWriteIntPref(struct wcontext *ctx, HKEY hkey, const TCHAR *valname, int val)
{
	DWORD tmp;

	tmp = (DWORD)val;
	RegSetValueEx(hkey,valname,0,REG_DWORD,(const BYTE *)&tmp,sizeof(DWORD));
}

static void wlsSavePreferences(struct wcontext *ctx)
{
	LONG ret;
	HKEY hkey=NULL;

	ret=RegCreateKeyEx(HKEY_CURRENT_USER,WLS_REGISTRY_KEY,0,NULL,
		REG_OPTION_NON_VOLATILE,KEY_WRITE,NULL,&hkey,NULL);

	if(ret!=ERROR_SUCCESS) {
		wlsErrorf(ctx,_T("Failed to save user preferences"));
		hkey=NULL;
		goto done;
	}

	wlsWriteIntPref(ctx,hkey,_T("ViewFreq"),g.viewfreq);
	wlsWriteIntPref(ctx,hkey,_T("CellSize"),ctx->cellwidth);
	wlsWriteIntPref(ctx,hkey,_T("DefaultPeriod"),ctx->user_default_period);
	wlsWriteIntPref(ctx,hkey,_T("DefaultColumns"),ctx->user_default_columns);
	wlsWriteIntPref(ctx,hkey,_T("DefaultRows"),ctx->user_default_rows);
	wlsWriteIntPref(ctx,hkey,_T("SearchPriority"),ctx->search_priority);
	wlsWriteIntPref(ctx,hkey,_T("MouseWheelAction"),ctx->wheel_gen_dir);

done:
	if(hkey) RegCloseKey(hkey);
}

// If the registry value could not be read, returns FALSE and does not
// modify *pval.
static BOOL wlsReadIntPref(struct wcontext *ctx, HKEY hkey, const TCHAR *valname, int *pval)
{
	DWORD tmp;
	LONG ret;
	DWORD datatype;
	DWORD datasize;

	datasize = sizeof(DWORD);
	ret = RegQueryValueEx(hkey,valname,NULL,&datatype,(BYTE*)&tmp,&datasize);
	if(ret!=ERROR_SUCCESS || datatype!=REG_DWORD || datasize!=sizeof(DWORD)) {
		return FALSE;
	}
	*pval = (int)tmp;
	return TRUE;
}

static void wlsLoadPreferences(struct wcontext *ctx)
{
	LONG ret;
	HKEY hkey=NULL;
	BOOL b;

	ret=RegOpenKeyEx(HKEY_CURRENT_USER,WLS_REGISTRY_KEY,0,KEY_QUERY_VALUE,&hkey);
	if(ret!=ERROR_SUCCESS) goto done;

	wlsReadIntPref(ctx,hkey,_T("ViewFreq"),&g.viewfreq);
	b=wlsReadIntPref(ctx,hkey,_T("CellSize"),&ctx->cellwidth);
	if(b) ctx->cellheight = ctx->cellwidth;
	wlsReadIntPref(ctx,hkey,_T("DefaultPeriod"),&ctx->user_default_period);
	wlsReadIntPref(ctx,hkey,_T("DefaultColumns"),&ctx->user_default_columns);
	wlsReadIntPref(ctx,hkey,_T("DefaultRows"),&ctx->user_default_rows);
	wlsReadIntPref(ctx,hkey,_T("SearchPriority"),&ctx->search_priority);
	wlsReadIntPref(ctx,hkey,_T("MouseWheelAction"),&ctx->wheel_gen_dir);

done:
	if(hkey) RegCloseKey(hkey);
}

static void Handle_Prefs_Init(struct wcontext *ctx, HWND hWnd)
{
	int sel;

	SetDlgItemInt(hWnd,IDC_VIEWFREQ,g.viewfreq,FALSE);
	SetDlgItemInt(hWnd,IDC_CELLSIZE,ctx->cellwidth,FALSE);
	SetDlgItemInt(hWnd,IDC_DEFAULTPERIOD,ctx->user_default_period,FALSE);
	SetDlgItemInt(hWnd,IDC_DEFAULTCOLUMNS,ctx->user_default_columns,FALSE);
	SetDlgItemInt(hWnd,IDC_DEFAULTROWS,ctx->user_default_rows,FALSE);

	// "Priority" drop-down list
	SendDlgItemMessage(hWnd,IDC_SEARCHPRIORITY,CB_ADDSTRING,0,(LPARAM)_T("Idle"));
	SendDlgItemMessage(hWnd,IDC_SEARCHPRIORITY,CB_ADDSTRING,0,(LPARAM)_T("Low"));
	SendDlgItemMessage(hWnd,IDC_SEARCHPRIORITY,CB_ADDSTRING,0,(LPARAM)_T("Normal"));
	sel=1; // Default to Low
	if(ctx->search_priority==WLS_PRIORITY_IDLE) sel=0;
	else if(ctx->search_priority==WLS_PRIORITY_NORMAL) sel=2;
	SendDlgItemMessage(hWnd,IDC_SEARCHPRIORITY,CB_SETCURSEL,(WPARAM)sel,0);

	// Mouse Wheel Action drop-down list
	SendDlgItemMessage(hWnd,IDC_WHEELGENDIR,CB_ADDSTRING,0,(LPARAM)_T("Decrease gen"));
	SendDlgItemMessage(hWnd,IDC_WHEELGENDIR,CB_ADDSTRING,0,(LPARAM)_T("Increase gen"));
	sel=0;
	if(ctx->wheel_gen_dir==2) sel=1;
	SendDlgItemMessage(hWnd,IDC_WHEELGENDIR,CB_SETCURSEL,(WPARAM)sel,0);
}

static void Handle_Prefs_OK(struct wcontext *ctx, HWND hWnd)
{
	int sel;

	g.viewfreq=GetDlgItemInt(hWnd,IDC_VIEWFREQ,NULL,FALSE);
	if(g.viewfreq<20000) g.viewfreq=20000;
	ctx->cellwidth=GetDlgItemInt(hWnd,IDC_CELLSIZE,NULL,FALSE);
	if(ctx->cellwidth<8) ctx->cellwidth=8;
	if(ctx->cellwidth>200) ctx->cellwidth=200;
	ctx->cellheight=ctx->cellwidth;
	ctx->user_default_period=GetDlgItemInt(hWnd,IDC_DEFAULTPERIOD,NULL,FALSE);
	ctx->user_default_columns=GetDlgItemInt(hWnd,IDC_DEFAULTCOLUMNS,NULL,FALSE);
	ctx->user_default_rows=GetDlgItemInt(hWnd,IDC_DEFAULTROWS,NULL,FALSE);

	sel = (int)SendDlgItemMessage(hWnd,IDC_SEARCHPRIORITY,CB_GETCURSEL,0,0);
	if(sel==0) ctx->search_priority = WLS_PRIORITY_IDLE;
	else if(sel==2) ctx->search_priority = WLS_PRIORITY_NORMAL;
	else ctx->search_priority = WLS_PRIORITY_LOW;

	sel = (int)SendDlgItemMessage(hWnd,IDC_WHEELGENDIR,CB_GETCURSEL,0,0);
	if(sel==1) ctx->wheel_gen_dir = 2;
	else ctx->wheel_gen_dir = 1;

	wlsSavePreferences(ctx);
	wlsRepaintCells(ctx,1);
}

static INT_PTR CALLBACK DlgProcPreferences(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
	WORD id;
	struct wcontext *ctx = gctx;

	switch(msg) {

	case WM_INITDIALOG:
		Handle_Prefs_Init(ctx,hWnd);
		return 1;

	case WM_COMMAND:
		id=LOWORD(wParam);

		switch(id) {

		case IDOK:
			Handle_Prefs_OK(ctx,hWnd);
			EndDialog(hWnd, TRUE);
			return 1;

		case IDCANCEL:
			EndDialog(hWnd, TRUE);
			return 1;

		}
	}
	return 0;
}

//////////////////////////////////////////////////////////////////

static void InitGameSettings(struct wcontext *ctx)
{
	// The cellwidth and height are required to be the same, because only a
	// single value is stored in the registry.
	ctx->cellwidth = 16;
	ctx->cellheight = ctx->cellwidth;

	ctx->search_priority = WLS_PRIORITY_LOW;
	ctx->wheel_gen_dir = 1;

	g.symmetry=0;

	g.trans_rotate=0;  // 1=90 degrees, 2=180, 3=270;
	g.trans_flip=0;
	g.trans_x= 0;
	g.trans_y= 0;

	g.curgen=0;
	g.parent=0;
	g.allobjects=0;
	g.nearcols=0;
	g.maxcount=0;
	g.userow=0;
	g.usecol=0;
	g.colcells=0;
	g.colwidth=0;
	g.followgens=FALSE;
	g.follow=FALSE;
#ifndef JS
	g.smart=TRUE;
	g.smartwindow = 50;
	g.smartthreshold = 4;
	g.smartstatlen = 0;
	g.smartstatwnd = 0;
	g.smartstatsumlen = 0;
	g.smartstatsumwnd = 0;
	g.smartstatsumlenc = 0;
	g.smartstatsumwndc = 0;
	g.smarton=TRUE;
	g.combine=FALSE;
	g.combining=FALSE;
#endif
	g.orderwide=FALSE;
#ifdef JS
	g.ordergens=FALSE;
#else
	g.ordergens=TRUE;
#endif
	g.ordermiddle=FALSE;
	g.diagsort=0;
	g.knightsort=0;
#ifdef JS
	g.fastsym=1;
#endif
	g.viewfreq=400000;
	StringCchCopy(g.outputfile,80,_T("output.txt"));
	g.saveoutput=0;
#ifndef JS
	g.saveoutputallgen=0;
	g.stoponfound=1;
	g.stoponstep=0;
#endif
#ifdef JS
	StringCchCopy(g.dumpfile,80,_T("dump.txt"));
#else
	StringCchCopy(g.dumpfile,80,_T("dump.wdf"));
#endif
	StringCchCopy(g.rulestring,WLS_RULESTRING_LEN,_T("B3/S23"));

	wlsLoadPreferences(ctx);

#define WLS_SYSTEM_DEFAULT_PERIOD   2
#define WLS_SYSTEM_DEFAULT_COLUMNS  35
#define WLS_SYSTEM_DEFAULT_ROWS     15

	if(ctx->user_default_period==0)
		ctx->user_default_period = WLS_SYSTEM_DEFAULT_PERIOD;
	g.period = ctx->user_default_period;
	if(g.period<1) g.period = 1;
	if(g.period>GENMAX) g.period = GENMAX;

	if(ctx->user_default_columns==0)
		ctx->user_default_columns = WLS_SYSTEM_DEFAULT_COLUMNS;
	g.ncols = ctx->user_default_columns;
	if(g.ncols<1) g.ncols = 1;
	if(g.ncols>COLMAX) g.ncols = COLMAX;

	if(ctx->user_default_rows==0)
		ctx->user_default_rows = WLS_SYSTEM_DEFAULT_ROWS;
	g.nrows = ctx->user_default_rows;
	if(g.nrows<1) g.nrows = 1;
	if(g.nrows>ROWMAX) g.nrows = ROWMAX;

	RecalcCenter(ctx);

	g.field = wlsAllocField(NULL);
	g.tmpfield = wlsAllocField(NULL);
}

static BOOL RegisterClasses(struct wcontext *ctx)
{
	WNDCLASS  wc;
	HICON iconWLS;

	wc.style = 0;
	wc.lpfnWndProc = WndProcFrame;
	wc.cbClsExtra = 0;
	wc.cbWndExtra = 0;
	wc.hInstance = ctx->hInst;
	iconWLS = LoadIcon(ctx->hInst,_T("ICONWLS"));
	wc.hIcon = iconWLS;
	wc.hCursor = LoadCursor(NULL, IDC_ARROW);
	wc.hbrBackground = GetStockObject(WHITE_BRUSH);
	wc.lpszMenuName =  _T("WLSMenu");
	wc.lpszClassName = _T("WLSCLASSFRAME");
	RegisterClass(&wc);

	wc.style = CS_OWNDC;
	wc.lpfnWndProc = WndProcMain;
	wc.cbClsExtra = 0;
	wc.cbWndExtra = 0;
	wc.hInstance = ctx->hInst;
	wc.hIcon=NULL;
	wc.hCursor = LoadCursor(NULL, IDC_ARROW);
	wc.hbrBackground = GetStockObject(WHITE_BRUSH);
	wc.lpszMenuName =  NULL;
	wc.lpszClassName = _T("WLSCLASSMAIN");
	RegisterClass(&wc);

	wc.style = 0;
	wc.lpfnWndProc = WndProcToolbar;
	wc.cbClsExtra = 0;
	wc.cbWndExtra = 0;
	wc.hInstance = ctx->hInst;
	wc.hIcon=NULL;
	wc.hCursor = LoadCursor(NULL, IDC_ARROW);
	wc.hbrBackground = GetStockObject(LTGRAY_BRUSH);
	wc.lpszMenuName =  NULL;
	wc.lpszClassName = _T("WLSCLASSTOOLBAR");
	RegisterClass(&wc);

	return 1;
}

static void wlsCreateFonts(struct wcontext *ctx)
{
	NONCLIENTMETRICS ncm;
	BOOL b;
	TCHAR fontname[100];

	memset(&ncm,0,sizeof(NONCLIENTMETRICS));
	ncm.cbSize = sizeof(NONCLIENTMETRICS);
	b=SystemParametersInfo(SPI_GETNONCLIENTMETRICS,sizeof(NONCLIENTMETRICS),&ncm,0);
	StringCbCopy(fontname,sizeof(fontname),
		b ? ncm.lfStatusFont.lfFaceName : _T("Arial"));

	ctx->statusfont = CreateFont(TOOLBARHEIGHT-4,0,
		0,0,FW_DONTCARE,0,0,0,
		ANSI_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,
		DEFAULT_QUALITY,VARIABLE_PITCH|FF_SWISS,fontname);
}

static BOOL InitApp(struct wcontext *ctx, int nCmdShow)
{
	RECT r;

	InitGameSettings(ctx);

	InitializeCriticalSection(&ctx->critsec_tmpfield);

	wlsCreateFonts(ctx);

	/* Create a main window for this application instance.	*/
	ctx->hwndFrame = CreateWindow(
		_T("WLSCLASSFRAME"),
		WLS_APPNAME,
		WS_OVERLAPPEDWINDOW,
		CW_USEDEFAULT,	/* horizontal position */
		CW_USEDEFAULT,	/* vertical position */
		CW_USEDEFAULT,	/* width */
		CW_USEDEFAULT,		/* height */
		NULL,	/* parent */
		NULL,	/* menu */
		ctx->hInst,
		NULL	/* Pointer not needed */
	);
	if (!ctx->hwndFrame) return (FALSE);

	GetClientRect(ctx->hwndFrame,&r);
	// create the main window pane (a child window)
	ctx->hwndMain = CreateWindow(
		_T("WLSCLASSMAIN"),
		_T("WinLifeSearch - main window"),
		WS_CHILD|WS_VISIBLE|WS_HSCROLL|WS_VSCROLL,
		0,	/* horizontal position */
		0,	/* vertical position */
		r.right,	/* width */
		r.bottom-TOOLBARHEIGHT,		/* height */
		ctx->hwndFrame,	/* parent */
		NULL,	/* menu */
		ctx->hInst,
		NULL	/* Pointer not needed */
	);
	if (!ctx->hwndMain) return (FALSE);

	ctx->hwndToolbar = CreateWindow(
		_T("WLSCLASSTOOLBAR"),
		_T("WinLifeSearch - toolbar"),
		WS_CHILD|WS_VISIBLE|WS_CLIPCHILDREN,
		0,	/* horizontal position */
		r.bottom-TOOLBARHEIGHT,	/* vertical position */
		r.right,	/* width */
		TOOLBARHEIGHT,		/* height */
		ctx->hwndFrame,	/* parent */
		NULL,	/* menu */
		ctx->hInst,
		NULL	/* Pointer not needed */
	);
	if (!ctx->hwndToolbar) return (FALSE);

	set_main_scrollbars(ctx, 1, 1);
	/* Make the window visible; update its client area; and return "success" */

	ShowWindow(ctx->hwndFrame, nCmdShow);		/* Show the window */
	UpdateWindow(ctx->hwndFrame); 	/* Sends WM_PAINT message */
	return (TRUE);  /* Returns the value from PostQuitMessage */
}

static void UninitApp(struct wcontext *ctx)
{
	if(ctx->statusfont) DeleteObject((HGDIOBJ)ctx->statusfont);
	wlsFreeField(g.field);
	wlsFreeField(g.tmpfield);
	DeleteCriticalSection(&ctx->critsec_tmpfield);
}

int CALLBACK WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine,
						  int nCmdShow)
{
	MSG msg;	/* message */
	HACCEL hAccTable;
	struct wcontext *ctx = NULL;

	memset(&g,0,sizeof(struct globals_struct));
	ctx = calloc(1,sizeof(struct wcontext));
	gctx = ctx;

	ctx->hInst = hInstance;

	hAccTable=LoadAccelerators(ctx->hInst,_T("WLSACCEL"));

	RegisterClasses(ctx);

	InitApp(ctx,nCmdShow);


	while(GetMessage(&msg,NULL,0,0)) {
		if (!TranslateAccelerator(ctx->hwndFrame, hAccTable, &msg)) {
			TranslateMessage(&msg);
			DispatchMessage(&msg);
		}
	}

	UninitApp(ctx);

	free(ctx);
	return (int)msg.wParam; /* Returns the value from PostQuitMessage */
}
