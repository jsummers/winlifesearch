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
#include <stdarg.h>
#include <process.h>
#include <malloc.h>
#include <assert.h>
#include "resource.h"
#include "wls.h"
#include "lifesrc.h"
#include <strsafe.h>


#define cellheight 16

#define cellwidth  16

#define TOOLBARHEIGHT 24


static LRESULT CALLBACK WndProcFrame(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);
static LRESULT CALLBACK WndProcMain(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);
static LRESULT CALLBACK WndProcToolbar(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);
static INT_PTR CALLBACK DlgProcAbout(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);
static INT_PTR CALLBACK DlgProcSymmetry(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);
static INT_PTR CALLBACK DlgProcTranslate(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);
static INT_PTR CALLBACK DlgProcRows(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);
static INT_PTR CALLBACK DlgProcPeriod(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);
static INT_PTR CALLBACK DlgProcOutput(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);
static INT_PTR CALLBACK DlgProcSearch(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);


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
	int selectstate;
	POINT startcell, endcell;
	RECT selectrect;
	int inverted;
	HANDLE hthread;
	int searchstate;
	pens_type pens;
	brushes_type brushes;
	int centerx,centery,centerxodd,centeryodd;
	POINT scrollpos;
	HFONT statusfont;
};

struct globals_struct g;

static struct wcontext *gctx;

volatile int abortthread;



/* \2 | 1/    
   3 \|/ 0
   ---+---
   4 /|\ 7
   /5 | 6\  */

static const int symmap[] = { 
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

void record_malloc(int func,void *m)
{
	static void *memblks[2000];
	static int used=0;
	int i;

	switch(func) {
	case 0:
		for(i=0;i<used;i++)
			free(memblks[i]);
		used=0;
		break;
	case 1:      // record this pointer
		memblks[used++]=m;
		break;
	}
}


static BOOL RegisterClasses(struct wcontext *ctx)
{   WNDCLASS  wc;
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


/* Returns all the cells symmetric to the given cell.
 * Returns an array irs of (x,y) coords.
 * (it will be 0, 1, 3, or 7.)
 *
 */
static POINT *GetSymmetricCells(int x,int y,int *num)
{
	static POINT pt[7];
	int s,n;

	s=symmap[g.symmetry];
	n=0;

	if(s & 0x02) {
		assert(g.colmax==g.rowmax);
		pt[n].x=g.rowmax-1-y; // forward diag
		pt[n].y=g.colmax-1-x;
		n++;
	}
	if(s & 0x04) {
		assert(g.colmax==g.rowmax);
		pt[n].x=y;               // rotate90
		pt[n].y=g.colmax-1-x;
		n++;
	}
	if(s & 0x08) {
		pt[n].x=g.colmax-1-x;  // mirrorx
		pt[n].y=y;
		n++;
	}
	if(s & 0x10) {
		pt[n].x=g.colmax-1-x;  // rotate180
		pt[n].y=g.rowmax-1-y;
		n++;
	}
	if(s & 0x20) {
		assert(g.colmax==g.rowmax);
		pt[n].x=y;                // back diag
		pt[n].y=x;
		n++;
	}
	if(s & 0x40) {
		assert(g.colmax==g.rowmax);
		pt[n].x=g.rowmax-1-y; // rotate270
		pt[n].y=x;
		n++;
	}
	if(s & 0x80) {
		pt[n].x=x;               // mirrory
		pt[n].y=g.rowmax-1-y;
		n++;
	}

	*num=n;

	return pt;
}

static void RecalcCenter(struct wcontext *ctx)
{
	ctx->centerx= g.colmax/2;
	ctx->centery= g.rowmax/2;
	ctx->centerxodd= g.colmax%2;
	ctx->centeryodd= g.rowmax%2;
}

static void InitGameSettings(struct wcontext *ctx)
{
	int i,j,k;

	for(k=0;k<GENMAX;k++) 
		for(i=0;i<COLMAX;i++)
			for(j=0;j<ROWMAX;j++) {
				g.origfield[k][i][j]=2;       // set all cells to "don't care"
				g.currfield[k][i][j]=2;
			}

	g.symmetry=0;

	g.trans_rotate=0;  // 1=90 degrees, 2=180, 3=270;
	g.trans_flip=0;
	g.trans_x= 0;
	g.trans_y= 0;

	g.genmax=2;

	g.curgen=0;
	g.colmax=35;
	g.rowmax=15;
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
	g.orderwide=FALSE;
	g.ordergens=TRUE;
	g.ordermiddle=FALSE;
	g.diagsort=0;
	g.knightsort=0;
	g.viewfreq=100000;
	StringCchCopy(g.outputfile,80,_T("output.txt"));
	g.saveoutput=0;
	g.saveoutputallgen=0;
	g.stoponfound=1;
	g.stoponstep=0;
	StringCchCopy(g.dumpfile,80,_T("dump.wdf"));
	StringCchCopy(g.rulestring,WLS_RULESTRING_LEN,_T("B3/S23"));

	RecalcCenter(ctx);
}

static int fix_scrollpos(struct wcontext *ctx)
{
	RECT r;
	int changed=0;
	int n;

	GetClientRect(ctx->hwndMain,&r);

	// If the entire field doesn't fit in the window, there should be no
	// unused space at the right/bottom of the field.
	n = g.colmax*cellwidth; // width of field
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

	n = g.rowmax*cellheight;
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
	si.nMax=g.colmax*cellwidth;
	si.nPage=r.right;
	si.nPos=ctx->scrollpos.x;
	si.nTrackPos=0;
	SetScrollInfo(ctx->hwndMain,SB_HORZ,&si,TRUE);

	si.nMax=g.rowmax*cellheight;
	si.nPage=r.bottom;
	si.nPos=ctx->scrollpos.y;
	SetScrollInfo(ctx->hwndMain,SB_VERT,&si,TRUE);

	hDC=GetDC(ctx->hwndMain);
	SetViewportOrgEx(hDC,-ctx->scrollpos.x,-ctx->scrollpos.y,NULL);
	ReleaseDC(ctx->hwndMain,hDC);
	if(redraw) InvalidateRect(ctx->hwndMain,NULL,TRUE);
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

	ctx->scrollpos.x=0; ctx->scrollpos.y=0;

	InitGameSettings(ctx);

	wlsCreateFonts(ctx);

	/* Create a main window for this application instance.	*/
	ctx->hwndFrame = CreateWindow(
		_T("WLSCLASSFRAME"),
		_T("WinLifeSearch"),
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
}

/****************************/
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


	while(GetMessage(&msg,NULL,0,0)){
		if (!TranslateAccelerator(ctx->hwndFrame, hAccTable, &msg)) {
			TranslateMessage(&msg);
			DispatchMessage(&msg);
		}
	}

	UninitApp(ctx);

	free(ctx);
	return (int)msg.wParam; /* Returns the value from PostQuitMessage */
}


static void DrawGuides(struct wcontext *ctx, HDC hDC)
{
	int centerpx,centerpy;
	int px1,py1,px2,py2,px3,py3;

	RecalcCenter(ctx);

	// store the center pixel in some temp vars to make things readable
	centerpx=ctx->centerx*cellwidth+ctx->centerxodd*(cellwidth/2);
	centerpy=ctx->centery*cellheight+ctx->centeryodd*(cellheight/2);

	SelectObject(hDC,ctx->pens.axes);


	// horizontal line
	if(g.symmetry==2 || g.symmetry==6 || g.symmetry==9) {	
		MoveToEx(hDC,0,centerpy,NULL);
		LineTo(hDC,g.colmax*cellwidth,centerpy);
	}

	// vertical line
	if(g.symmetry==1 || g.symmetry==6 || g.symmetry==9) {
		MoveToEx(hDC,centerpx,0,NULL);
		LineTo(hDC,centerpx,g.rowmax*cellheight);
	}

	// diag - forward
	if(g.symmetry==3 || g.symmetry==5 || g.symmetry>=7) {
		MoveToEx(hDC,0,g.rowmax*cellheight,NULL);
		LineTo(hDC,g.colmax*cellwidth,0);
	}

	// diag - backward
	if(g.symmetry==4 || g.symmetry>=7) {
		MoveToEx(hDC,0,0,NULL);
		LineTo(hDC,g.colmax*cellwidth,g.rowmax*cellheight);
	}
	if(g.symmetry==5 || g.symmetry==8) {
		MoveToEx(hDC,0,g.rowmax*cellheight,NULL);
		LineTo(hDC,0,(g.rowmax-2)*cellheight);
		MoveToEx(hDC,g.colmax*cellwidth,0,NULL);
		LineTo(hDC,g.colmax*cellwidth,2*cellheight);
	}
	if(g.symmetry==8) {
		MoveToEx(hDC,0,0,NULL);
		LineTo(hDC,2*cellwidth,0);
		MoveToEx(hDC,g.colmax*cellwidth,g.rowmax*cellheight,NULL);
		LineTo(hDC,(g.colmax-2)*cellwidth,g.rowmax*cellheight);
	}
		
		
	if(g.trans_rotate || g.trans_flip || g.trans_x || g.trans_y) {
		// the px & py values are pixels offsets from the center
		px1=0;           py1=2*cellheight;
		px2=0;           py2=0;
		px3=cellwidth/2; py3=cellheight/2;

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
			px1=cellwidth*2;
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
			px1= -cellwidth*2;
			py1=0;
			if(g.trans_flip)
				py3= -py3;
			else
				px3= -px3;
			break;
		}

		// translate if necessary
		px1+=g.trans_x*cellwidth;
		px2+=g.trans_x*cellwidth;
		px3+=g.trans_x*cellwidth;
		py1+=g.trans_y*cellheight;
		py2+=g.trans_y*cellheight;
		py3+=g.trans_y*cellheight;
		
		SelectObject(hDC,ctx->pens.arrow2);
		MoveToEx(hDC,centerpx+px1,centerpy+py1,NULL);
		LineTo(hDC,centerpx+px2,centerpy+py2);
		LineTo(hDC,centerpx+px3,centerpy+py3);

	}
}

// pen & brush must already be selected
static void ClearCell(HDC hDC,int x,int y, int dblsize)
{
	Rectangle(hDC,x*cellwidth+1,y*cellheight+1,
	    (x+1)*cellwidth,(y+1)*cellheight);
	if(dblsize) {
		Rectangle(hDC,x*cellwidth+2,y*cellheight+2,
			(x+1)*cellwidth-1,(y+1)*cellheight-1);
	}
}

static void DrawCell(struct wcontext *ctx, HDC hDC,int x,int y)
{
	int allsame=1;
	int tmp;
	int i;

	SelectObject(hDC,ctx->pens.celloutline);
	SelectObject(hDC,ctx->brushes.cell);

	tmp=g.currfield[0][x][y];
	for(i=1;i<g.genmax;i++) {
		if(g.currfield[i][x][y]!=tmp) allsame=0;
	}

	ClearCell(hDC,x,y,!allsame);

	switch(g.currfield[g.curgen][x][y]) {
	case 0:        // must be off
		SelectObject(hDC,ctx->pens.cell_off);
		SelectObject(hDC,GetStockObject(NULL_BRUSH));
		Ellipse(hDC,x*cellwidth+3,y*cellheight+3,
			(x+1)*cellwidth-2,(y+1)*cellheight-2);
		break;
	case 1:	       // must be on
		SelectObject(hDC,ctx->brushes.cell_on);
		SelectObject(hDC,GetStockObject(NULL_PEN));
		Ellipse(hDC,x*cellwidth+3,y*cellheight+3,
			(x+1)*cellwidth-1,(y+1)*cellheight-1);
		break;
	case 3:       // unchecked
		SelectObject(hDC,ctx->pens.unchecked);
		MoveToEx(hDC,(x  )*cellwidth+2,(y  )*cellheight+2,NULL);
		LineTo(hDC,  (x+1)*cellwidth-2,(y+1)*cellheight-2);
		MoveToEx(hDC,(x+1)*cellwidth-2,(y  )*cellheight+2,NULL);
		LineTo(hDC,  (x  )*cellwidth+2,(y+1)*cellheight-2);

		break;
	case 4:       // frozen
		SelectObject(hDC,ctx->pens.cell_off);
		MoveToEx(hDC,x*cellwidth+2*cellwidth/3,y*cellheight+  cellheight/3,NULL);
		LineTo(hDC,  x*cellwidth+  cellwidth/3,y*cellheight+  cellheight/3);
		LineTo(hDC,  x*cellwidth+  cellwidth/3,y*cellheight+2*cellheight/3);

		MoveToEx(hDC,x*cellwidth+2*cellwidth/3,y*cellheight+  cellheight/2,NULL);
		LineTo(hDC,  x*cellwidth+  cellwidth/3,y*cellheight+  cellheight/2);

		break;

	}

}

// Set/reset unknown/unchecked
static void ChangeChecking(struct wcontext *ctx, HDC hDC, int x, int y, int allgens, int set)
{
	POINT *pts;
	int numpts,i,j;
	int s1, s2;
	int g1, g2;

	if (set)
	{
		s1 = 2;
		s2 = 3;
	} else {
		s1 = 3;
		s2 = 2;
	}

	if (allgens)
	{
		g1 = 0;
		g2 = GENMAX - 1;
	}
	else
	{
		g1 = g.curgen;
		g2 = g.curgen;
	}

	pts=GetSymmetricCells(x,y,&numpts);

	for(j=g1;j<=g2;j++)
	{
		if (g.currfield[j][x][y] == s1)
		{
			g.currfield[j][x][y] = s2;
		}
	}
	DrawCell(ctx,hDC,x,y);

	for(i=0;i<numpts;i++) {
		for(j=g1;j<=g2;j++)
		{
			if (g.currfield[j][pts[i].x][pts[i].y] == s1)
			{
				g.currfield[j][pts[i].x][pts[i].y] = s2;
			}
		}
		DrawCell(ctx,hDC,pts[i].x,pts[i].y);
	}
}

// set and paint all cells symmetrical to the given cell
// (including the given cell)
static void Symmetricalize(struct wcontext *ctx, HDC hDC,int x,int y,int allgens)
{
	POINT *pts;
	int numpts,i,j;

	pts=GetSymmetricCells(x,y,&numpts);

	if(allgens) {
		for(j=0;j<GENMAX;j++) 
		{
			g.currfield[j][x][y]=g.currfield[g.curgen][x][y];
		}
	}
	DrawCell(ctx,hDC,x,y);

	for(i=0;i<numpts;i++) {
		g.currfield[g.curgen][pts[i].x][pts[i].y]=g.currfield[g.curgen][x][y];
		if(allgens) {
			for(j=0;j<GENMAX;j++)
			{
				g.currfield[j][pts[i].x][pts[i].y]=g.currfield[g.curgen][x][y];
			}
		}
		DrawCell(ctx,hDC,pts[i].x,pts[i].y);
	}
}


static void InvertCells(struct wcontext *ctx, HDC hDC1)
{
	RECT r;
	HDC hDC;

	if(ctx->endcell.x>=ctx->startcell.x) {
		r.left= ctx->startcell.x*cellwidth;
		r.right= (ctx->endcell.x+1)*cellwidth;
	}
	else {
		r.left= ctx->endcell.x*cellwidth;
		r.right=(ctx->startcell.x+1)*cellwidth;
	}

	if(ctx->endcell.y>=ctx->startcell.y) {
		r.top= ctx->startcell.y*cellheight;
		r.bottom= (ctx->endcell.y+1)*cellheight;
	}
	else {
		r.top=ctx->endcell.y*cellheight;
		r.bottom=(ctx->startcell.y+1)*cellheight;
	}


	if(hDC1)  hDC=hDC1;
	else      hDC=GetDC(ctx->hwndMain);

	InvertRect(hDC,&r);

	if(!hDC1) ReleaseDC(ctx->hwndMain,hDC);

}


static void SelectOff(struct wcontext *ctx, HDC hDC)
{
	if(ctx->selectstate<1) return;

	if(ctx->selectstate) {
		if(ctx->inverted) {
			InvertCells(ctx,hDC);
			ctx->inverted=0;
		}
		ctx->selectstate=0;
	}
}


static void DrawWindow(struct wcontext *ctx, HDC hDC)
{
	int i,j;

	for(i=0;i<g.colmax;i++) {
		for(j=0;j<g.rowmax;j++) {
			DrawCell(ctx,hDC,i,j);
		}
	}
	DrawGuides(ctx,hDC);

	if(ctx->selectstate>0) {
		InvertCells(ctx,hDC);
	}
}

static void PaintWindow(struct wcontext *ctx, HWND hWnd)
{
	HDC hdc;
	HPEN hOldPen;
	HBRUSH hOldBrush;
	PAINTSTRUCT ps;

	hdc= BeginPaint(hWnd,&ps);
	hOldPen= SelectObject(hdc, GetStockObject(BLACK_PEN));
	hOldBrush=SelectObject(hdc,GetStockObject(LTGRAY_BRUSH));

	DrawWindow(ctx,hdc);

	SelectObject(hdc,hOldPen);
	SelectObject(hdc,hOldBrush);
	EndPaint(hWnd,&ps);
}


static void FixFrozenCells(void)
{
	int x,y,z;

	for(x=0;x<COLMAX;x++) {
		for(y=0;y<ROWMAX;y++) {
			for(z=0;z<GENMAX;z++) {
				if(z!=g.curgen) {
					if(g.currfield[g.curgen][x][y]==4) {
						g.currfield[z][x][y]=4;
					}
					else {  // current not frozen
						if(g.currfield[z][x][y]==4)
							g.currfield[z][x][y]=2;
					}
				}
			}
		}
	}
}


//returns 0 if processed
static int ButtonClick(struct wcontext *ctx, UINT msg,WORD xp,WORD yp,WPARAM wParam)
{
	int x,y;
	int i,j;
	HDC hDC;
	int vkey;
	int tmp;
	int lastval;
	int allgens=0;
	int newval;

	newval = -1;

	xp+=(WORD)ctx->scrollpos.x;
	yp+=(WORD)ctx->scrollpos.y;

	x=xp/cellwidth;   // + scroll offset
	y=yp/cellheight;  // + scroll offset

	if(x<0 || x>=g.colmax) return 1;
	if(y<0 || y>=g.rowmax) return 1;


	lastval= g.currfield[g.curgen][x][y];
//	if(lastval==4) allgens=1;       // previously frozen


	switch(msg) {

	case WM_MOUSEMOVE:
		if(ctx->selectstate!=1) return 1;

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
		if(ctx->selectstate>0) {
			SelectOff(ctx,NULL);
		}

		ctx->selectstate=1;
		ctx->startcell.x=x;
		ctx->startcell.y=y;
		ctx->endcell.x=x;
		ctx->endcell.y=y;
		return 0;
		

	case WM_LBUTTONUP:
		if(wParam & MK_SHIFT) allgens=1;
		if(x==ctx->startcell.x && y==ctx->startcell.y) {
			ctx->selectstate=0;
			if(g.currfield[g.curgen][x][y]==1) newval=2; //g.currfield[g.curgen][x][y]=2;
			else newval=1;  //g.currfield[g.curgen][x][y]=1;
			//Symmetricalize(hDC,x,y,allgens);
		}
		else if(ctx->selectstate==1) {
			// selecting an area
			ctx->selectstate=2;
			return 0;

		}
		break;

	case WM_RBUTTONDOWN:     // toggle off/unchecked
		if(wParam & MK_SHIFT) allgens=1;
		if(g.currfield[g.curgen][x][y]==0) newval=2; //currfield[g.curgen][x][y]=2;
		else newval=0; //currfield[g.curgen][x][y]=0;
		break;


	case WM_CHAR:
		vkey=(int)wParam;

		if(vkey=='C' || vkey=='X' || vkey=='A' || vkey=='S' || vkey=='F' || vkey=='I' || vkey=='O')
			allgens=1;

		if(vkey=='C' || vkey=='c') {
			newval=2;
		}
		else if(vkey=='X' || vkey=='x') {
			newval=3;
		}
		else if(vkey=='A' || vkey=='a') {
			newval=0;
		}
		else if(vkey=='S' || vkey=='s') {
			newval=1;
		}
		else if(vkey=='F' || vkey=='f') {
			newval=4;
			allgens=1;
		} else if(vkey=='I' || vkey=='i') {
			newval=11;
		} else if(vkey=='O' || vkey=='o') {
			newval=10;
		} else {
			return 1;
		}

		break;

	default:
		return 1;
	}

	hDC=GetDC(ctx->hwndMain);

	tmp=ctx->selectstate;

	SelectOff(ctx,hDC);

	if(ctx->searchstate == 0) {
		if(tmp==2) {
			if(newval>=0) {
				for(i=ctx->selectrect.left;i<=ctx->selectrect.right;i++) {
					for(j=ctx->selectrect.top;j<=ctx->selectrect.bottom;j++) {
						if (newval < 10)
						{
							g.currfield[g.curgen][i][j]=newval;
							Symmetricalize(ctx,hDC,i,j,allgens);
						} else {
							ChangeChecking(ctx,hDC,i,j,allgens,newval == 10);
						}
					}
				}
			}
		} else {
			if(newval>=0) 
			{
				if (newval < 10)
				{
					g.currfield[g.curgen][x][y]=newval;
					Symmetricalize(ctx,hDC,x,y,allgens);
				} else {
					ChangeChecking(ctx,hDC,x,y,allgens,newval == 10);
				}
			}
		}
	}

	FixFrozenCells();

	DrawGuides(ctx,hDC);

	ReleaseDC(ctx->hwndMain,hDC);
	return 0;
}

// copy my format to dbells format...
// ... and make a backup of the current state (KAS)
BOOL set_initial_cells(void)
{
	CELL *cell;
	CELL **setpos;
	BOOL change;
	int i,j,g1;
	struct wcontext *ctx = gctx;

	g.newset = g.settable;
	g.nextset = g.settable;

	for(g1=0;g1<g.genmax;g1++) {
		for(i=0;i<g.colmax;i++) {
			for(j=0;j<g.rowmax;j++) {

				g.origfield[g1][i][j] = g.currfield[g1][i][j];

				switch(g.currfield[g1][i][j]) {
				case 0:  // forced off
					if(!proceed(findcell(j+1,i+1,g1),OFF,FALSE)) {
						wlsMessagef(ctx,_T("Inconsistent OFF state for cell (col %d,row %d,gen %d)"),i+1,j+1,g1);
						return FALSE;
					}
					break;
				case 1:  // forced on
					if(!proceed(findcell(j+1,i+1,g1),ON,FALSE)) {
						wlsMessagef(ctx,_T("Inconsistent ON state for cell (col %d,row %d,gen %d)"),i+1,j+1,g1);
						return FALSE;
					}
					break;
				case 3: // unchecked
					cell = findcell(j+1,i+1,g1);
					cell->unchecked = TRUE;
					break;
				case 4: // frozen cells
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
		for(g1=0;g1<g.genmax;g1++) {
			for(i=0;i<g.colmax;i++) {
				for(j=0;j<g.rowmax;j++) {
					cell = findcell(j+1,i+1,g1);
					if (cell->active && (cell->state == UNK)) {
						if (proceed(cell, OFF, TRUE))
						{
							backup();
							if (proceed(cell, ON, TRUE))
							{
								backup();
							} else {
								// OFF possible, ON impossible
								if (setpos != g.newset) backup();
								if (proceed(cell, OFF, TRUE))
								{
									change = TRUE;
								} else {
									// we should never get here
									// because it's already tested that the OFF state is possible
									wlsMessagef(ctx,_T("Program inconsistency found"));
									return FALSE;
								}
							}							
						} else {
							// can't set OFF state
							// let's try ON state
							if (setpos != g.newset) backup();
							if (proceed(cell, ON, TRUE))
							{
								change = TRUE;
							} else {
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

static void draw_gen_counter(struct wcontext *ctx)
{
	TCHAR buf[80];
	SCROLLINFO si;

	si.cbSize=sizeof(SCROLLINFO);
	si.fMask=SIF_ALL;
	si.nMin=0;
	si.nMax=g.genmax-1;
	si.nPage=1;
	si.nPos=g.curgen;
	si.nTrackPos=0;

	SetScrollInfo(ctx->hwndGenScroll,SB_CTL,&si,TRUE);

	StringCbPrintf(buf,sizeof(buf),_T("%d"),g.curgen);
	SetWindowText(ctx->hwndGen,buf);
}

void showcount(void)
{
	TCHAR buf[80];
	struct wcontext *ctx = gctx;
	static int tot=0;

	if (g.viewcount<0) {
		tot=0;
	} else {
		tot += g.viewcount;
	}
	g.viewcount = 0;

	StringCbPrintf(buf,sizeof(buf),_T("WinLifeSearch [%d]"),tot);
	SetWindowText(ctx->hwndFrame,buf);
}


// possible values for type:
// 0 - nothing happens
// 1 - store the generation as a solution
// 2 - copy solution

void printgen(void)
{
	int i,j,g1;
	CELL *cell;
	struct wcontext *ctx = gctx;

	// copy dbell's format back into mine
	for(g1=0;g1<g.genmax;g1++) {
		for(i=0;i<g.colmax;i++){
			for(j=0;j<g.rowmax;j++) {

				cell=findcell(j+1,i+1,g1);

				switch (cell->state) {
				case ON:
					g.currfield[g1][i][j] = 1;
					break;
				case OFF:
					g.currfield[g1][i][j] = 0;
					break;
				case UNK:
					g.currfield[g1][i][j] = g.origfield[g1][i][j];
				}
			}
		}
	}

	InvalidateRect(ctx->hwndMain,NULL,FALSE);
}

static void do_combine(void)
{
	int i,j,g1;
	CELL *cell;

	if (g.combining)
	{
		for(g1=0;g1<g.genmax;g1++) {
			for(i=0;i<g.colmax;i++){
				for(j=0;j<g.rowmax;j++) {
					cell=findcell(j+1,i+1,g1);
					if ((cell->combined != UNK) && (cell->combined != cell->state))
					{
						--g.combinedcells;
						cell->combined = UNK;
					}
				}
			}
		}
	} else {
		g.combining = TRUE;
		g.combinedcells = 0;
		for(g1=0;g1<g.genmax;g1++) {
			for(i=0;i<g.colmax;i++){
				for(j=0;j<g.rowmax;j++) {					
					cell=findcell(j+1,i+1,g1);
					if ((g.origfield[g1][i][j] > 1) && ((cell->state == ON) || (cell->state == OFF)))
					{
						++g.combinedcells;
						cell->combined = cell->state;
					} else {
						cell->combined = UNK;
					}
				}
			}
		}
	}
	g.setcombinedcells = g.combinedcells;
	g.differentcombinedcells = 0;
}

static void show_combine(struct wcontext *ctx)
{
	int i,j,g1;
	CELL *cell;

	if (g.combinedcells > 0)
	{
		for(g1=0;g1<g.genmax;g1++) {
			for(i=0;i<g.colmax;i++){
				for(j=0;j<g.rowmax;j++) {					
					cell=findcell(j+1,i+1,g1);
					switch(cell->combined) {
					case ON:
						g.currfield[g1][i][j] = 1;
						break;
					case OFF:
						g.currfield[g1][i][j] = 0;
						break;
					case UNK:
						g.currfield[g1][i][j] = g.origfield[g1][i][j];
					}
				}
			}
		}
	}

	InvalidateRect(ctx->hwndMain,NULL,FALSE);
}

static void pause_search(struct wcontext *ctx);  // forward decl

static DWORD WINAPI search_thread(LPVOID foo)
{
	BOOL reset = 0;
	int i, j, k;
	struct wcontext *ctx = gctx;

	/*
	 * Initial commands are complete, now look for the object.
	 */
	while (TRUE)
	{
		if (g.curstatus == OK)
			g.curstatus = search();

		if(abortthread) goto done;

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
			dumpstate(g.dumpfile, FALSE);
		}

		if ((g.curstatus == FOUND) && g.combine)
		{
			g.curstatus = OK;
			++g.foundcount;
            do_combine();
			writegen(g.outputfile, TRUE);
			wlsStatusf(ctx,_T("Combine: %d cells remaining from %d solutions"), g.combinedcells, g.foundcount);
			continue;
		}

		/*
		 * Here if results are going to a file.
		 */
		if (g.curstatus == FOUND) {
			g.curstatus = OK;

			showcount();
			printgen();
			++g.foundcount;
			wlsStatusf(ctx,_T("Object %d found.\n"), g.foundcount);

			writegen(g.outputfile, TRUE);
			if (!g.stoponfound) continue;
			goto done;
		}

		if (g.combine)
		{
			show_combine(ctx);
			if (g.combining)
			{
				reset = (g.combinedcells == 0);
				wlsMessagef(ctx,_T("Search completed: %d cell%s found"),
					g.combinedcells, (g.combinedcells == 1) ? _T("") : _T("s"));
			}
			else
			{
				reset = 1;
				wlsMessagef(ctx,_T("Search completed: no solution"));
			}
		} else {
			showcount();
			printgen();
			reset = (g.foundcount == 0);
			wlsMessagef(ctx,_T("Search completed:  %d object%s found"),
				g.foundcount, (g.foundcount == 1) ? _T("") : _T("s"));
		}

		goto done;
	}
done:
	if (reset)
	{
		for(k=0;k<GENMAX;k++) 
			for(i=0;i<COLMAX;i++)
				for(j=0;j<ROWMAX;j++)
					g.currfield[k][i][j]=g.origfield[k][i][j];
		ctx->searchstate = 0;
		InvalidateRect(ctx->hwndMain,NULL,FALSE);
	}
	else
	{
		ctx->searchstate=1;
	}
	_endthreadex(0);
	return 0;
}


static void resume_search(struct wcontext *ctx)
{
	DWORD threadid;

	SetWindowText(ctx->hwndFrame,_T("WinLifeSearch"));
	if (g.combine)
	{
		if (g.combining)
		{
			wlsStatusf(ctx,_T("Combine: %d cells remaining"), g.combinedcells);
		}
		else
		{
			wlsStatusf(ctx,_T("Combining..."));
		}
	}
	else
	{
		wlsStatusf(ctx,_T("Searching..."));
	}

	if(ctx->searchstate!=1) {
		wlsErrorf(ctx,_T("No search is paused"));
		return;
	}
	abortthread=0;

	ctx->searchstate=2;
	ctx->hthread=(HANDLE)_beginthreadex(NULL,0,search_thread,(void*)0,0,&threadid);


	if(ctx->hthread==NULL) {
		wlsErrorf(ctx,_T("Unable to create search thread"));
		ctx->searchstate=1;
	}
	else {
		SetWindowText(ctx->hwndFrame,_T("WinLifeSearch"));

		// Request that the computer not go to sleep while we're searching.
		SetThreadExecutionState(ES_CONTINUOUS | ES_SYSTEM_REQUIRED);

		SetThreadPriority(ctx->hthread,THREAD_PRIORITY_BELOW_NORMAL);
	}

}

static BOOL prepare_search(struct wcontext *ctx, BOOL load)
{
	int i;

	if(ctx->searchstate!=0) {
		wlsErrorf(ctx,_T("A search is already running"));
		return FALSE;
	}

	if (!setrules(g.rulestring))
	{
		wlsErrorf(ctx,_T("Cannot set Life rules!"));
		return FALSE;
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

	for(i=0;i<ROWMAX;i++) g.rowinfo[i].oncount=0;

	g.newset=NULL;
	g.nextset=NULL;
	g.outputlastcols=0;
	g.fullcolumns=0;
	g.curstatus=OK;
	g.g0oncellcount = 0; // KAS
	g.cellcount = 0; // KAS
	g.smartchoice = UNK; // KAS

	g.viewcount = -1; // KAS
	showcount(); // KAS

	g.foundcount=0;
	g.writecount=0;

	g.combining = FALSE;
	g.combinedcells = 0;
	g.setcombinedcells = 0;
	g.differentcombinedcells = 0;

	if (load) {
		if(!loadstate())
		{
			InvalidateRect(ctx->hwndMain,NULL,TRUE);
			return FALSE;
		}

		InvalidateRect(ctx->hwndMain,NULL,TRUE);

		draw_gen_counter(ctx);
	} else {
		initcells();

		if(!set_initial_cells()) 
		{
			record_malloc(0,NULL); // release allocated memory
			return FALSE;   // there was probably an inconsistency in the initial cells
		}

		initsearchorder(); 
	}
	ctx->searchstate=1;  // pretend the search is "paused"

	printgen();

	return TRUE;
}

static void start_search(struct wcontext *ctx)
{
	if (prepare_search(ctx,FALSE))
	{
		resume_search(ctx);
	}
}


static void pause_search(struct wcontext *ctx)
{
	DWORD exitcode;

	if(ctx->searchstate!=2) {
		wlsErrorf(ctx,_T("No search is running"));
		return;
	}
	abortthread=1;
	do {
		GetExitCodeThread(ctx->hthread, &exitcode);
		if(exitcode==STILL_ACTIVE) {
			Sleep(200);
		}
	} while(exitcode==STILL_ACTIVE);
	CloseHandle(ctx->hthread);
	ctx->searchstate=1;

	// Tell Windows we're okay with the computer going to sleep.
	SetThreadExecutionState(ES_CONTINUOUS);

	printgen();

	SetWindowText(ctx->hwndFrame,_T("WinLifeSearch - Paused"));

}


static void reset_search(struct wcontext *ctx)
{
	int i,j,k;

	if(ctx->searchstate==0) {
		wlsErrorf(ctx,_T("No search in progress"));
		goto here;
	}

	// stop the search thread if it is running
	if(ctx->searchstate==2) {
		pause_search(ctx);
	}

	ctx->searchstate=0;

	record_malloc(0,NULL);    // free memory

	// restore the original cells
	for(k=0;k<GENMAX;k++) 
		for(i=0;i<COLMAX;i++)
			for(j=0;j<ROWMAX;j++)
				g.currfield[k][i][j]=g.origfield[k][i][j];

here:
	InvalidateRect(ctx->hwndMain,NULL,FALSE);

	SetWindowText(ctx->hwndFrame,_T("WinLifeSearch"));
	wlsStatusf(ctx,_T(""));

}

static void open_state(struct wcontext *ctx)
{
	if(ctx->searchstate!=0) reset_search(ctx);

	prepare_search(ctx,TRUE);
}


static void gen_changeby(struct wcontext *ctx, int delta)
{
	if(g.genmax<2) return;
	g.curgen+=delta;
	if(g.curgen>=g.genmax) g.curgen=0;
	if(g.curgen<0) g.curgen=g.genmax-1;

	draw_gen_counter(ctx);
	InvalidateRect(ctx->hwndMain,NULL,FALSE);
}

static void hide_selection(struct wcontext *ctx)
{
	SelectOff(ctx,NULL);
}

static void clear_gen(int g1)
{	int i,j;
	for(i=0;i<COLMAX;i++)
		for(j=0;j<ROWMAX;j++)
			g.currfield[g1][i][j]=2;
}

static void clear_all(struct wcontext *ctx)
{	int g1;
	for(g1=0;g1<GENMAX;g1++)
		clear_gen(g1);
	hide_selection(ctx);
}

static void flip_h(struct wcontext *ctx, int fromgen, int togen)
{
	int g1, r, c;
	int fromrow, torow, fromcol, tocol;
	int buffer;

	if (ctx->selectstate == 2)
	{
		fromcol = ctx->selectrect.left;
		tocol = ctx->selectrect.right;
		fromrow = ctx->selectrect.top;
		torow = ctx->selectrect.bottom;
	} else {
		fromcol = 0;
		tocol = g.colmax - 1;
		fromrow = 0;
		torow = g.rowmax - 1;
	}

	for (g1 = fromgen; g1 <= togen; ++g1)
	{
		for (r = fromrow; r <= torow; ++r)
		{
			for (c = (fromcol + tocol) / 2; c >= fromcol; --c)
			{
				buffer = g.currfield[g1][c][r];
				g.currfield[g1][c][r] = g.currfield[g1][tocol + fromcol - c][r];
				g.currfield[g1][tocol + fromcol - c][r] = buffer;
			}
		}	
	}
}

static void flip_v(struct wcontext *ctx, int fromgen, int togen)
{
	int g1, r, c;
	int fromrow, torow, fromcol, tocol;
	int buffer;

	if (ctx->selectstate == 2)
	{
		fromcol = ctx->selectrect.left;
		tocol = ctx->selectrect.right;
		fromrow = ctx->selectrect.top;
		torow = ctx->selectrect.bottom;
	} else {
		fromcol = 0;
		tocol = g.colmax - 1;
		fromrow = 0;
		torow = g.rowmax - 1;
	}

	for (g1 = fromgen; g1 <= togen; ++g1)
	{
		for (r = (fromrow + torow) / 2; r >= fromrow; --r)
		{
			for (c = fromcol; c <= tocol; ++c)
			{
				buffer = g.currfield[g1][c][r];
				g.currfield[g1][c][r] = g.currfield[g1][c][torow + fromrow - r];
				g.currfield[g1][c][torow + fromrow - r] = buffer;
			}
		}	
	}
}

static void transpose(struct wcontext *ctx, int fromgen, int togen)
{
	int g1, r, c;
	int fromrow, torow, fromcol, tocol;
	int buffer;

	if (ctx->selectstate == 2)
	{
		fromcol = ctx->selectrect.left;
		tocol = ctx->selectrect.right;
		fromrow = ctx->selectrect.top;
		torow = ctx->selectrect.bottom;
	} else {
		fromcol = 0;
		tocol = g.colmax - 1;
		fromrow = 0;
		torow = g.rowmax - 1;
	}

	if ((fromcol - tocol) != (fromrow - torow))
	{
		// only transpose squares
		return;
	}

	for (g1 = fromgen; g1 <= togen; ++g1)
	{
		for (r = fromrow + 1; r <= torow; ++r)
		{
			for (c = fromcol; c < fromcol + (r - fromrow); ++c)
			{
				buffer = g.currfield[g1][c][r];
				g.currfield[g1][c][r] = g.currfield[g1][fromcol - fromrow + r][fromrow - fromcol + c];
				g.currfield[g1][fromcol - fromrow + r][fromrow - fromcol + c] = buffer;
			}
		}	
	}
}

static void shift_gen(struct wcontext *ctx, int fromgen, int togen, int gend, int cold, int rowd)
{
	int g1,r,c;
	int fromrow, torow, fromcol, tocol;
	int gx,rx,cx;

	if (ctx->selectstate == 2)
	{
		fromcol = ctx->selectrect.left;
		tocol = ctx->selectrect.right;
		fromrow = ctx->selectrect.top;
		torow = ctx->selectrect.bottom;
	} else {
		fromcol = 0;
		tocol = g.colmax - 1;
		fromrow = 0;
		torow = g.rowmax - 1;
	}

	for(g1=fromgen; g1<=togen; g1++) {
		for(c=fromcol; c<=tocol; c++) {
			for (r=fromrow; r<=torow; r++) {
				g.origfield[g1][c][r] = g.currfield[g1][c][r];
			}
		}
	}

	for(g1=fromgen; g1<=togen; g1++) {
		for(c=fromcol; c<=tocol; c++) {
			for (r=fromrow; r<=torow; r++) {
				gx = (g1 + gend - fromgen + togen - fromgen + 1) % (togen - fromgen + 1) + fromgen;
				cx = (c + cold - fromcol + tocol - fromcol + 1) % (tocol - fromcol + 1) + fromcol;
				rx = (r + rowd - fromrow + torow - fromrow + 1) % (torow - fromrow + 1) + fromrow;
				g.currfield[gx][cx][rx] = g.origfield[g1][c][r];
			}
		}
	}
}

static void copy_result(struct wcontext *ctx)
{
	int g1, i, j;

	if (ctx->searchstate == 0) return;

	if (ctx->searchstate != 1) pause_search(ctx);

	for(g1=0;g1<g.genmax;g1++) {
		for(i=0;i<g.colmax;i++){
			for(j=0;j<g.rowmax;j++) {
				g.origfield[g1][i][j] = g.currfield[g1][i][j];
			}
		}
	}

	if (ctx->searchstate != 0) reset_search(ctx);
}

static void copy_combination(struct wcontext *ctx)
{
	int g1, i, j;

	if (ctx->searchstate == 0) return;

	if (ctx->searchstate != 1) pause_search(ctx);

	show_combine(ctx);

	for(g1=0;g1<g.genmax;g1++) {
		for(i=0;i<g.colmax;i++){
			for(j=0;j<g.rowmax;j++) {
				g.origfield[g1][i][j] = g.currfield[g1][i][j];
			}
		}
	}

	if (ctx->searchstate != 0) reset_search(ctx);
}

static void clear_combination(struct wcontext *ctx)
{
	if (ctx->searchstate == 0) return;

	if (ctx->searchstate != 1) pause_search(ctx);

	g.combining = 0;
}

static void copytoclipboard(struct wcontext *ctx)
{
	DWORD size;
	HGLOBAL hClip;
	LPVOID lpClip;
	TCHAR buf[100],buf2[10];
	TCHAR *s;
	int i,j;
	int offset;

	if(ctx->searchstate==0) {
		// bornrules/liverules may not be set up yet
		// probably we could call setrules().
		StringCbCopy(buf,sizeof(buf),_T("#P 0 0\r\n"));
	}
	else {
		// unfortunately the rulestring is in the wrong format for life32
		StringCbCopy(buf,sizeof(buf),_T("#P 0 0\r\n#R S"));
		for(i=0;i<=8;i++) {
			if(g.liverules[i]) {StringCbPrintf(buf2,sizeof(buf2),_T("%d"),i); StringCbCat(buf,sizeof(buf),buf2);}
		}
		StringCbCat(buf,sizeof(buf),_T("/B"));
		for(i=0;i<=8;i++) {
			if(g.bornrules[i]) {StringCbPrintf(buf2,sizeof(buf2),_T("%d"),i); StringCbCat(buf,sizeof(buf),buf2);}
		}
		StringCbCat(buf,sizeof(buf),_T("\r\n"));
	}

	offset=lstrlen(buf);

	size=(offset+(g.colmax+2)*g.rowmax+1)*sizeof(TCHAR);
	hClip=GlobalAlloc(GMEM_MOVEABLE|GMEM_DDESHARE,size);

	lpClip=GlobalLock(hClip);
	s=(TCHAR*)lpClip;

	StringCbCopy(s,size,buf);

	for(j=0;j<g.rowmax;j++) {
		for(i=0;i<g.colmax;i++) {
			if(g.currfield[g.curgen][i][j]==1)
				s[offset+(g.colmax+2)*j+i]='*';
			else
				s[offset+(g.colmax+2)*j+i]='.';
		}
		s[offset+(g.colmax+2)*j+g.colmax]='\r';
		s[offset+(g.colmax+2)*j+g.colmax+1]='\n';
	}
	s[offset+(g.colmax+2)*g.rowmax]='\0';

	OpenClipboard(NULL);
	EmptyClipboard();
#ifdef UNICODE
	SetClipboardData(CF_UNICODETEXT,hClip);
#else
	SetClipboardData(CF_TEXT,hClip);
#endif
	CloseClipboard();
}

static void handle_MouseWheel(struct wcontext *ctx, HWND hWnd, WPARAM wParam)
{
	signed short delta = (signed short)(HIWORD(wParam));
	while(delta >= 120) {
		gen_changeby(ctx,-1);
		delta -= 120;
	}
	while(delta <= -120) {
		gen_changeby(ctx,1);
		delta += 120;
	}
}

/****************************************************************************/
static LRESULT CALLBACK WndProcFrame(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
	WORD id;
	POINT pt;
	struct wcontext *ctx = gctx;

	id=LOWORD(wParam);

	switch(msg) {

	case WM_MOUSEWHEEL:
		handle_MouseWheel(ctx, hWnd, wParam);
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
		// >>>
		return 0;
	case WM_CLOSE:
		DestroyWindow(hWnd);
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
		EnableMenuItem((HMENU)wParam,IDC_SEARCHSTART,ctx->searchstate==0?MF_ENABLED:MF_GRAYED);
		EnableMenuItem((HMENU)wParam,IDC_SEARCHPREPARE,ctx->searchstate==0?MF_ENABLED:MF_GRAYED);
		EnableMenuItem((HMENU)wParam,IDC_SEARCHRESET,ctx->searchstate!=0?MF_ENABLED:MF_GRAYED);
		EnableMenuItem((HMENU)wParam,IDC_SEARCHPAUSE,ctx->searchstate==2?MF_ENABLED:MF_GRAYED);
		EnableMenuItem((HMENU)wParam,IDC_SEARCHRESUME,ctx->searchstate==1?MF_ENABLED:MF_GRAYED);
		EnableMenuItem((HMENU)wParam,IDC_SEARCHBACKUP,ctx->searchstate!=0?MF_ENABLED:MF_GRAYED);
		EnableMenuItem((HMENU)wParam,IDC_SEARCHBACKUP2,ctx->searchstate!=0?MF_ENABLED:MF_GRAYED);
		EnableMenuItem((HMENU)wParam,IDC_COPYRESULT,ctx->searchstate!=0?MF_ENABLED:MF_GRAYED);
		EnableMenuItem((HMENU)wParam,IDC_COPYCOMBINATION,ctx->searchstate!=0?MF_ENABLED:MF_GRAYED);
		EnableMenuItem((HMENU)wParam,IDC_CLEARCOMBINATION,ctx->searchstate!=0?MF_ENABLED:MF_GRAYED);
		return 0;

	case WM_CHAR:
		GetCursorPos(&pt);
		ScreenToClient(hWnd,&pt);
		return ButtonClick(ctx,msg,(WORD)pt.x,(WORD)pt.y,wParam);


	case WM_COMMAND:
		switch(id) {
		case IDC_EXIT:
			DestroyWindow(hWnd);
			return 0;
		case IDC_ABOUT:
			DialogBox(ctx->hInst,_T("DLGABOUT"),hWnd,DlgProcAbout);
			return 0;
		case IDC_ROWS:
			DialogBox(ctx->hInst,_T("DLGROWS"),hWnd,DlgProcRows);
			return 0;
		case IDC_PERIOD:
			DialogBox(ctx->hInst,_T("DLGPERIOD"),hWnd,DlgProcPeriod);
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
		case IDC_SEARCHSTART:
			start_search(ctx);
			return 0;
		case IDC_SEARCHPREPARE:
			prepare_search(ctx,FALSE);
			return 0;
		case IDC_SEARCHPAUSE:
			pause_search(ctx);
			return 0;
		case IDC_SEARCHRESET:
			reset_search(ctx);
			return 0;
		case IDC_SEARCHRESUME:
			resume_search(ctx);
			return 0;
		case IDC_SEARCHBACKUP:
			if (ctx->searchstate==2)
				pause_search(ctx);
			if(ctx->searchstate==1)
				getbackup("1 ");
			return 0;
		case IDC_SEARCHBACKUP2:
			if (ctx->searchstate==2)
				pause_search(ctx);
			if(ctx->searchstate==1)
				getbackup("20 ");
			return 0;
		case IDC_OPENSTATE:
			open_state(ctx);
			return 0;
		case IDC_SAVEGAME:
			if (ctx->searchstate==0) {
				if (prepare_search(ctx,FALSE)) {
					dumpstate(NULL, FALSE);
					reset_search(ctx);
				}
			} else if (ctx->searchstate==1) {
				dumpstate(NULL, FALSE);
			} else if (ctx->searchstate==2) {
				pause_search(ctx);
				dumpstate(NULL, FALSE);
				resume_search(ctx);
			}
			return 0;
		case IDC_NEXTGEN:
			gen_changeby(ctx,1);
			return 0;
		case IDC_PREVGEN:
			gen_changeby(ctx,-1);
			return 0;
		case IDC_EDITCOPY:
			copytoclipboard(ctx);
			return 0;
		case IDC_CLEARGEN:
			if(ctx->searchstate == 0) clear_gen(g.curgen);
			hide_selection(ctx);
			InvalidateRect(ctx->hwndMain,NULL,FALSE);
			return 0;
		case IDC_CLEAR:
			if(ctx->searchstate == 0) clear_all(ctx);
			InvalidateRect(ctx->hwndMain,NULL,FALSE);
			return 0;
		case IDC_COPYRESULT:
			if(ctx->searchstate != 0) copy_result(ctx);
			return 0;
		case IDC_COPYCOMBINATION:
			if (ctx->searchstate != 0) copy_combination(ctx);
			return 0;
		case IDC_CLEARCOMBINATION:
			if (ctx->searchstate != 0) clear_combination(ctx);
			return 0;
		case IDC_SHIFTGUP:
			if(ctx->searchstate == 0) shift_gen(ctx, g.curgen, g.curgen, 0, 0, -1);
			InvalidateRect(ctx->hwndMain,NULL,ctx->selectstate == 2);
			return 0;
		case IDC_SHIFTGDOWN:
			if(ctx->searchstate == 0) shift_gen(ctx, g.curgen, g.curgen, 0, 0, 1);
			InvalidateRect(ctx->hwndMain,NULL,ctx->selectstate == 2);
			return 0;
		case IDC_SHIFTGLEFT:
			if(ctx->searchstate == 0) shift_gen(ctx, g.curgen, g.curgen, 0, -1, 0);
			InvalidateRect(ctx->hwndMain,NULL,ctx->selectstate == 2);
			return 0;
		case IDC_SHIFTGRIGHT:
			if(ctx->searchstate == 0) shift_gen(ctx, g.curgen, g.curgen, 0, 1, 0);
			InvalidateRect(ctx->hwndMain,NULL,ctx->selectstate == 2);
			return 0;
		case IDC_SHIFTAUP:
			if(ctx->searchstate == 0) shift_gen(ctx, 0, GENMAX-1, 0, 0, -1);
			InvalidateRect(ctx->hwndMain,NULL,ctx->selectstate == 2);
			return 0;
		case IDC_SHIFTADOWN:
			if(ctx->searchstate == 0) shift_gen(ctx, 0, GENMAX-1, 0, 0, 1);
			InvalidateRect(ctx->hwndMain,NULL,ctx->selectstate == 2);
			return 0;
		case IDC_SHIFTALEFT:
			if(ctx->searchstate == 0) shift_gen(ctx, 0, GENMAX-1, 0, -1, 0);
			InvalidateRect(ctx->hwndMain,NULL,ctx->selectstate == 2);
			return 0;
		case IDC_SHIFTARIGHT:
			if(ctx->searchstate == 0) shift_gen(ctx, 0, GENMAX-1, 0, 1, 0);
			InvalidateRect(ctx->hwndMain,NULL,ctx->selectstate == 2);
			return 0;
		case IDC_SHIFTAPAST:
			if(ctx->searchstate == 0) shift_gen(ctx, 0, g.genmax-1, -1, 0, 0);
			InvalidateRect(ctx->hwndMain,NULL,ctx->selectstate == 2);
			return 0;
		case IDC_SHIFTAFUTURE:
			if(ctx->searchstate == 0) shift_gen(ctx, 0, g.genmax-1, 1, 0, 0);
			InvalidateRect(ctx->hwndMain,NULL,ctx->selectstate == 2);
			return 0;
		case ID_FLIP_GEN_H:
			if(ctx->searchstate == 0) flip_h(ctx, g.curgen, g.curgen);
			InvalidateRect(ctx->hwndMain,NULL,ctx->selectstate == 2);
			return 0;
		case ID_FLIP_GEN_V:
			if(ctx->searchstate == 0) flip_v(ctx, g.curgen, g.curgen);
			InvalidateRect(ctx->hwndMain,NULL,ctx->selectstate == 2);
			return 0;
		case ID_FLIP_ALL_H:
			if(ctx->searchstate == 0) flip_h(ctx, 0, GENMAX - 1);
			InvalidateRect(ctx->hwndMain,NULL,ctx->selectstate == 2);
			return 0;
		case ID_FLIP_ALL_V:
			if(ctx->searchstate == 0) flip_v(ctx, 0, GENMAX - 1);
			InvalidateRect(ctx->hwndMain,NULL,ctx->selectstate == 2);
			return 0;
		case ID_TRANS_GEN:
			if(ctx->searchstate == 0) transpose(ctx, g.curgen, g.curgen);
			InvalidateRect(ctx->hwndMain,NULL,ctx->selectstate == 2);
			return 0;
		case ID_TRANS_ALL:
			if(ctx->searchstate == 0) transpose(ctx, 0, GENMAX - 1);
			InvalidateRect(ctx->hwndMain,NULL,ctx->selectstate == 2);
			return 0;
		case ID_HIDESEL:
			hide_selection(ctx);
			return 0;
		break;
		}
	}

	return (DefWindowProc(hWnd, msg, wParam, lParam));
}

static void Handle_Scroll(struct wcontext *ctx, UINT msg, WORD scrollboxpos, WORD id)
{
	POINT oldscrollpos;

	oldscrollpos = ctx->scrollpos;

	if(msg==WM_HSCROLL) {
		switch(id) {
		case SB_LINELEFT: ctx->scrollpos.x-=cellwidth; break;
		case SB_LINERIGHT: ctx->scrollpos.x+=cellwidth; break;
		case SB_PAGELEFT: ctx->scrollpos.x-=cellwidth*4; break;
		case SB_PAGERIGHT: ctx->scrollpos.x+=cellwidth*4; break;
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
		case SB_LINELEFT: ctx->scrollpos.y-=cellheight; break;
		case SB_LINERIGHT: ctx->scrollpos.y+=cellheight; break;
		case SB_PAGELEFT: ctx->scrollpos.y-=cellheight*4; break;
		case SB_PAGERIGHT: ctx->scrollpos.y+=cellheight*4; break;
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

	id = sizeof(CELL);
	id=LOWORD(wParam);

	switch(msg) {
	case WM_PAINT:
		PaintWindow(ctx,hWnd);
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
		if(ctx->searchstate==0) {
			ButtonClick(ctx,msg,LOWORD(lParam),HIWORD(lParam),wParam);
		}
		return 0;
	}

	return (DefWindowProc(hWnd, msg, wParam, lParam));
}

static void Handle_ToolbarCreate(struct wcontext *ctx, HWND hWnd, LPARAM lParam)
{
	CREATESTRUCT *cs;

	cs = (CREATESTRUCT*)lParam;

	ctx->hwndGen=CreateWindow(_T("Static"),_T("0"),
		WS_CHILD|WS_BORDER|SS_CENTER|SS_NOPREFIX,
		1,1,40,TOOLBARHEIGHT-2,
		hWnd,NULL,ctx->hInst,NULL);
	SendMessage(ctx->hwndGen,WM_SETFONT,(WPARAM)ctx->statusfont,(LPARAM)FALSE);
	ShowWindow(ctx->hwndGen,SW_SHOW);

	ctx->hwndGenScroll=CreateWindow(_T("Scrollbar"),_T("wls_gen_scrollbar"),
		WS_CHILD|WS_VISIBLE|SBS_HORZ,
		41,1,80,TOOLBARHEIGHT-2,
		hWnd,NULL,ctx->hInst,NULL);

	ctx->hwndStatus=CreateWindow(_T("Static"),_T(""),
		WS_CHILD|WS_BORDER|SS_LEFTNOWORDWRAP|SS_NOPREFIX,
		121,1,cs->cx-121,TOOLBARHEIGHT-2,
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
	SetWindowPos(ctx->hwndStatus,NULL,0,0,newwidth-121,TOOLBARHEIGHT-2,
		/*SWP_NOACTIVATE|*/SWP_NOMOVE/*|SWP_NOOWNERZORDER*/|SWP_NOZORDER);
}

static LRESULT CALLBACK WndProcToolbar(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
	HWND htmp;
	WORD id;
	struct wcontext *ctx = gctx;

	int ori_gen;

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
			case SB_RIGHT: g.curgen=g.genmax-1; break;
			case SB_THUMBPOSITION: g.curgen=HIWORD(wParam); break;
			case SB_THUMBTRACK: g.curgen=HIWORD(wParam); break;
			}
			if(g.curgen<0) g.curgen=g.genmax-1;  // wrap around
			if(g.curgen>=g.genmax) g.curgen=0;
			if(g.curgen!=ori_gen) {
				draw_gen_counter(ctx);
				InvalidateRect(ctx->hwndMain,NULL,FALSE);
			}
			return 0;
		}
		break;

#if 0
	case WM_CREATE:
		ctx->hwndGen=CreateWindow(_T("Static"),_T("0"),
			WS_CHILD|WS_VISIBLE|WS_BORDER,
			1,1,40,TOOLBARHEIGHT-2,
			hWnd,NULL,ctx->hInst,NULL);
		ctx->hwndGenScroll=CreateWindow(_T("Scrollbar"),_T("wls_gen_scrollbar"),
			WS_CHILD|WS_VISIBLE|SBS_HORZ,
			41,1,80,TOOLBARHEIGHT-2,
			hWnd,NULL,ctx->hInst,NULL);
		ctx->hwndStatus=CreateWindow(_T("Static"),_T(""),
			WS_CHILD|WS_VISIBLE|WS_BORDER,
			120,1,800,TOOLBARHEIGHT-2,
			hWnd,NULL,ctx->hInst,NULL);
#ifdef _DEBUG
			wlsStatus(_T("DEBUG BUILD"));
#endif

			draw_gen_counter();
		return 0;
#endif
	}

	return (DefWindowProc(hWnd, msg, wParam, lParam));
}

static INT_PTR CALLBACK DlgProcAbout(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
	WORD id;
	id=LOWORD(wParam);

	switch (msg) {
	case WM_INITDIALOG:
		return 1;   // didn't call SetFocus

	case WM_COMMAND:
		if (id == IDOK || id == IDCANCEL) {
			EndDialog(hWnd, TRUE);
			return 1;
		}
		break;
	}
	return 0;		// Didn't process a message
}

static INT_PTR CALLBACK DlgProcRows(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
	WORD id;
	struct wcontext *ctx = gctx;

	id=LOWORD(wParam);

	switch (msg) {
	case WM_INITDIALOG:
		SetDlgItemInt(hWnd,IDC_COLUMNS,g.colmax,FALSE);
		SetDlgItemInt(hWnd,IDC_ROWS,g.rowmax,FALSE);
		return 1;   // didn't call SetFocus

	case WM_COMMAND:
		switch(id) {
		case IDOK:
			g.colmax=GetDlgItemInt(hWnd,IDC_COLUMNS,NULL,FALSE);
			g.rowmax=GetDlgItemInt(hWnd,IDC_ROWS,NULL,FALSE);
			if(g.colmax<1) g.colmax=1;
			if(g.rowmax<1) g.rowmax=1;
			if(g.colmax>COLMAX) g.colmax=COLMAX;
			if(g.rowmax>ROWMAX) g.rowmax=ROWMAX;
			RecalcCenter(ctx);

			// put these in a separate Validate function
			if(g.colmax!=g.rowmax) {
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

			if(g.colmax != g.rowmax) {
				if(g.trans_rotate==1 || g.trans_rotate==3) {
					MessageBox(hWnd,_T("Current rotation setting requires that rows and ")
						_T("columns be equal. Your translation setting has been altered."),
						_T("Warning"),MB_OK|MB_ICONWARNING);
					g.trans_rotate--;
				}
			}

			set_main_scrollbars(ctx, 1, 1);
			// fall through
		case IDCANCEL:
			EndDialog(hWnd, TRUE);
			return 1;
		}
	}
	return 0;		// Didn't process a message
}


static INT_PTR CALLBACK DlgProcPeriod(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
	WORD id;
	TCHAR buf[80];
	struct wcontext *ctx = gctx;

	id=LOWORD(wParam);

	switch (msg) {
	case WM_INITDIALOG:
		StringCbPrintf(buf,sizeof(buf),_T("Enter a period from 1 to %d"),GENMAX);
		SetDlgItemText(hWnd,IDC_PERIODTEXT,buf);
		SetDlgItemInt(hWnd,IDC_PERIOD1,g.genmax,FALSE);
		return 1;

	case WM_COMMAND:
		switch(id) {
		case IDOK:
			g.genmax=GetDlgItemInt(hWnd,IDC_PERIOD1,NULL,FALSE);
			if(g.genmax>GENMAX) g.genmax=GENMAX;
			if(g.genmax<1) g.genmax=1;
			if(g.curgen>=g.genmax) g.curgen=g.genmax-1;

			draw_gen_counter(ctx);
			InvalidateRect(ctx->hwndMain,NULL,TRUE);
			// fall through
		case IDCANCEL:
			EndDialog(hWnd, TRUE);
			return 1;
		}
	}
	return 0;		// Didn't process a message
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
		CheckDlgButton(hWnd,IDC_OUTPUTALLGEN,g.saveoutputallgen?BST_CHECKED:BST_UNCHECKED);
		CheckDlgButton(hWnd,IDC_STOPONFOUND,g.stoponfound?BST_CHECKED:BST_UNCHECKED);
		CheckDlgButton(hWnd,IDC_STOPONSTEP,g.stoponstep?BST_CHECKED:BST_UNCHECKED);
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
			g.saveoutputallgen=(IsDlgButtonChecked(hWnd,IDC_OUTPUTALLGEN)==BST_CHECKED);
			g.stoponfound=(IsDlgButtonChecked(hWnd,IDC_STOPONFOUND)==BST_CHECKED);
			g.stoponstep=(IsDlgButtonChecked(hWnd,IDC_STOPONSTEP)==BST_CHECKED);
			GetDlgItemText(hWnd,IDC_OUTPUTFILE,g.outputfile,79);
			g.outputcols=GetDlgItemInt(hWnd,IDC_OUTPUTCOLS,NULL,FALSE);
			// fall through
		case IDCANCEL:
			EndDialog(hWnd, TRUE);
			return 1;
		}
	}
	return 0;		// Didn't process a message
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
		CheckDlgButton(hWnd,IDC_ALLOBJECTS,g.allobjects);
		CheckDlgButton(hWnd,IDC_PARENT,g.parent);
		CheckDlgButton(hWnd,IDC_FOLLOW,g.follow);
		CheckDlgButton(hWnd,IDC_FOLLOWGENS,g.followgens);
		CheckDlgButton(hWnd,IDC_SMART,g.smart);
		CheckDlgButton(hWnd,IDC_SMARTON,g.smarton);
		CheckDlgButton(hWnd,IDC_COMBINE,g.combine);
		SetDlgItemInt(hWnd,IDC_NEARCOLS,g.nearcols,TRUE);
		SetDlgItemInt(hWnd,IDC_USECOL,g.usecol,TRUE);
		SetDlgItemInt(hWnd,IDC_USEROW,g.userow,TRUE);
		SetDlgItemInt(hWnd,IDC_SMARTWINDOW,g.smartwindow,TRUE);
		SetDlgItemInt(hWnd,IDC_SMARTTHRESHOLD,g.smartthreshold,TRUE);
		SetDlgItemInt(hWnd,IDC_SMARTWINDOWSTAT,g.smartstatwnd,TRUE);
		SetDlgItemInt(hWnd,IDC_SMARTTHRESHOLDSTAT,g.smartstatlen,TRUE);

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
			g.allobjects= IsDlgButtonChecked(hWnd,IDC_ALLOBJECTS )?1:0;
			g.parent=     IsDlgButtonChecked(hWnd,IDC_PARENT     )?1:0;
			g.follow=     IsDlgButtonChecked(hWnd,IDC_FOLLOW     )?1:0;
			g.followgens= IsDlgButtonChecked(hWnd,IDC_FOLLOWGENS )?1:0;
			g.smart=      IsDlgButtonChecked(hWnd,IDC_SMART      )?1:0;
			g.smarton=    IsDlgButtonChecked(hWnd,IDC_SMARTON    )?1:0; 
			g.combine=    IsDlgButtonChecked(hWnd,IDC_COMBINE    )?1:0; 
			
			g.nearcols=GetDlgItemInt(hWnd,IDC_NEARCOLS,NULL,TRUE);
			g.usecol=GetDlgItemInt(hWnd,IDC_USECOL,NULL,TRUE);
			g.userow=GetDlgItemInt(hWnd,IDC_USEROW,NULL,TRUE);
			g.maxcount=GetDlgItemInt(hWnd,IDC_MAXCOUNT,NULL,TRUE);
			g.colcells=GetDlgItemInt(hWnd,IDC_COLCELLS,NULL,TRUE);
			g.colwidth=GetDlgItemInt(hWnd,IDC_COLWIDTH,NULL,TRUE);
			g.smartwindow=GetDlgItemInt(hWnd,IDC_SMARTWINDOW,NULL,TRUE);
			g.smartthreshold=GetDlgItemInt(hWnd,IDC_SMARTTHRESHOLD,NULL,TRUE);

			GetDlgItemText(hWnd,IDC_RULESTRING,g.rulestring,WLS_RULESTRING_LEN);

			// fall through
		case IDCANCEL:
			EndDialog(hWnd, TRUE);
			return 1;
		case IDC_CONWAY:
			SetDlgItemText(hWnd,IDC_RULESTRING,_T("B3/S23"));
			return 1;
		}
	}
	return 0;		// Didn't process a message
}

static INT_PTR CALLBACK DlgProcSymmetry(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
	WORD id;
	int item;
	struct wcontext *ctx = gctx;

	id=LOWORD(wParam);

	switch (msg) {
	case WM_INITDIALOG:
		if(g.colmax!=g.rowmax) {
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
		return 1;   // didn't call SetFocus

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

			InvalidateRect(ctx->hwndMain,NULL,FALSE);
			// fall through
		case IDCANCEL:
			EndDialog(hWnd, TRUE);
			return 1;
		}
		break;
	}
	return 0;		// Didn't process a message
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

		if(g.colmax != g.rowmax) {
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
		return 1;   // didn't call SetFocus

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
			InvalidateRect(ctx->hwndMain,NULL,FALSE);
			// fall through
		case IDCANCEL:
			EndDialog(hWnd, TRUE);
			return 1;
		}
		break;
	}
	return 0;		// Didn't process a message
}


