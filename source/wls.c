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
#include "wls.h"
#include "lifesrc.h"
#include <strsafe.h>


#define cellheight 16

#define cellwidth  16

#define TOOLBARHEIGHT 24

TCHAR rulestring[20];
int saveoutput;
int saveoutputallgen;
int stoponfound;
int stoponstep;

HINSTANCE hInst;
HWND hwndFrame,hwndMain,hwndToolbar;
HWND hwndGen,hwndGenScroll;

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

struct globals_struct g;

int selectstate=0;
POINT startcell, endcell;
RECT selectrect;
int inverted=0;

typedef struct {
	HPEN celloutline,cell_off,axes,arrow1,arrow2,unchecked;
} pens_type;

typedef struct {
	HBRUSH cell,cell_on;
} brushes_type;

volatile int abortthread;
HANDLE hthread;
int searchstate=0;
int foundcount;
int writecount;

pens_type pens;
brushes_type brushes;

int centerx,centery,centerxodd,centeryodd;
int origfield[GENMAX][COLMAX][ROWMAX];
int currfield[GENMAX][COLMAX][ROWMAX];

POINT scrollpos;

// This is self-explanatory, right?

/* \2 | 1/    
   3 \|/ 0
   ---+---
   4 /|\ 7
   /5 | 6\  */

int symmap[] = { 
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


void wlsError(TCHAR *m,int n)
{
	TCHAR s[80];
	if(n) StringCbPrintf(s,sizeof(s),_T("%s (%d)"),m,n);
	else StringCbCopy(s,sizeof(s),m);
	MessageBox(hwndFrame,s,_T("Error"),MB_OK|MB_ICONWARNING);
}

void wlsMessage(TCHAR *m,int n)
{
	TCHAR s[180];
	if(n) StringCbPrintf(s,sizeof(s),_T("%s (%d)"),m,n);
	else StringCbCopy(s,sizeof(s),m);
	MessageBox(hwndFrame,s,_T("Message"),MB_OK|MB_ICONINFORMATION);
}

HWND hwndStatus=NULL;

void wlsStatus(TCHAR *msg)
{
	if(hwndStatus)
		SetWindowText(hwndStatus,msg);
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


BOOL RegisterClasses(HANDLE hInstance)
{   WNDCLASS  wc;
	HICON iconWLS;

	wc.style = 0;
	wc.lpfnWndProc = WndProcFrame;
	wc.cbClsExtra = 0;
	wc.cbWndExtra = 0;
	wc.hInstance = hInstance;
	iconWLS = LoadIcon(hInstance,_T("ICONWLS"));
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
	wc.hInstance = hInstance;
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
	wc.hInstance = hInstance;
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
POINT *GetSymmetricCells(int x,int y,int *num)
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

// A stupid function
void SetCenter(void)
{
	centerx= g.colmax/2;
	centery= g.rowmax/2;
	centerxodd= g.colmax%2;
	centeryodd= g.rowmax%2;
}

void InitGameSettings(void)
{
	int i,j,k;

	for(k=0;k<GENMAX;k++) 
		for(i=0;i<COLMAX;i++)
			for(j=0;j<ROWMAX;j++) {
				origfield[k][i][j]=2;       // set all cells to "don't care"
				currfield[k][i][j]=2;
			}

	g.symmetry=0;

	g.trans_rotate=0;  // 1=90 degrees, 2=180, 3=270;
	g.trans_flip=0;
	g.trans_x= 0;
	g.trans_y= 0;

	g.genmax=2;

	curgen=0;
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
	viewfreq=100000;
	StringCchCopy(outputfile,80,_T("output.txt"));
	saveoutput=0;
	saveoutputallgen=0;
	stoponfound=1;
	stoponstep=0;
	StringCchCopy(dumpfile,80,_T("dump.wdf"));
	StringCchCopy(rulestring,20,_T("B3/S23"));

	SetCenter();
}

void set_main_scrollbars(int redraw)
{
	SCROLLINFO si;
	RECT r;
	HDC hDC;

	GetClientRect(hwndMain,&r);

	if(scrollpos.x<0) scrollpos.x=0;
	if(scrollpos.y<0) scrollpos.y=0;

	if(g.colmax*cellwidth<=r.right && scrollpos.x!=0) { scrollpos.x=0; redraw=1; }
	if(g.rowmax*cellheight<=r.bottom && scrollpos.y!=0) { scrollpos.y=0; redraw=1; }

	si.cbSize=sizeof(SCROLLINFO);
	si.fMask=SIF_ALL;
	si.nMin=0;
	si.nMax=g.colmax*cellwidth;
	si.nPage=r.right;
	si.nPos=scrollpos.x;
	si.nTrackPos=0;
	SetScrollInfo(hwndMain,SB_HORZ,&si,TRUE);

	si.nMax=g.rowmax*cellheight;
	si.nPage=r.bottom;
	si.nPos=scrollpos.y;
	SetScrollInfo(hwndMain,SB_VERT,&si,TRUE);

	hDC=GetDC(hwndMain);
	SetViewportOrgEx(hDC,-scrollpos.x,-scrollpos.y,NULL);
	ReleaseDC(hwndMain,hDC);
	if(redraw) InvalidateRect(hwndMain,NULL,TRUE);
}


BOOL InitApp(HANDLE hInstance, int nCmdShow)
{
	RECT r;

	scrollpos.x=0; scrollpos.y=0;

	InitGameSettings();

	/* Create a main window for this application instance.	*/
	hwndFrame = CreateWindow(
		_T("WLSCLASSFRAME"),
		_T("WinLifeSearch"),
		WS_OVERLAPPEDWINDOW,
		CW_USEDEFAULT,	/* horizontal position */
		CW_USEDEFAULT,	/* vertical position */
		CW_USEDEFAULT,	/* width */
		CW_USEDEFAULT,		/* height */
		NULL,	/* parent */
		NULL,	/* menu */
		hInstance,
		NULL	/* Pointer not needed */
	);
	if (!hwndFrame) return (FALSE);

	GetClientRect(hwndFrame,&r);
	// create the main window pane (a child window)
	hwndMain = CreateWindow(
		_T("WLSCLASSMAIN"),
		_T("WinLifeSearch - main window"),
		WS_CHILD|WS_VISIBLE|WS_HSCROLL|WS_VSCROLL,
		0,	/* horizontal position */
		0,	/* vertical position */
		r.right,	/* width */
		r.bottom-TOOLBARHEIGHT,		/* height */
		hwndFrame,	/* parent */
		NULL,	/* menu */
		hInstance,
		NULL	/* Pointer not needed */
	);
	if (!hwndMain) return (FALSE);

	hwndToolbar = CreateWindow(
		_T("WLSCLASSTOOLBAR"),
		_T("WinLifeSearch - toolbar"),
		WS_CHILD|WS_VISIBLE,
		0,	/* horizontal position */
		r.bottom-TOOLBARHEIGHT,	/* vertical position */
		r.right,	/* width */
		TOOLBARHEIGHT,		/* height */
		hwndFrame,	/* parent */
		NULL,	/* menu */
		hInstance,
		NULL	/* Pointer not needed */
	);
	if (!hwndToolbar) return (FALSE);

	set_main_scrollbars(1);
	/* Make the window visible; update its client area; and return "success" */

	ShowWindow(hwndFrame, nCmdShow);		/* Show the window */
	UpdateWindow(hwndFrame); 	/* Sends WM_PAINT message */
	return (TRUE);  /* Returns the value from PostQuitMessage */
}

/****************************/
int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine,
						  int nCmdShow)
{
	MSG msg;	/* message */
	HACCEL hAccTable;

	hInst = hInstance;

	hAccTable=LoadAccelerators(hInst,_T("WLSACCEL"));

	RegisterClasses(hInstance);

	InitApp(hInstance,nCmdShow);


	while(GetMessage(&msg,NULL,0,0)){
		if (!TranslateAccelerator(hwndFrame, hAccTable, &msg)) {
			TranslateMessage(&msg);
			DispatchMessage(&msg);
		}
	}
	return (int)msg.wParam; /* Returns the value from PostQuitMessage */
}


void DrawGuides(HDC hDC)
{
	int centerpx,centerpy;
	int px1,py1,px2,py2,px3,py3;

	SetCenter();

	// store the center pixel in some temp vars to make things readable
	centerpx=centerx*cellwidth+centerxodd*(cellwidth/2);
	centerpy=centery*cellheight+centeryodd*(cellheight/2);

	SelectObject(hDC,pens.axes);


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
		SelectObject(hDC,pens.arrow1);
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
		if(centerxodd != centeryodd)
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
		
		SelectObject(hDC,pens.arrow2);
		MoveToEx(hDC,centerpx+px1,centerpy+py1,NULL);
		LineTo(hDC,centerpx+px2,centerpy+py2);
		LineTo(hDC,centerpx+px3,centerpy+py3);

	}
}

// pen & brush must already be selected
void ClearCell(HDC hDC,int x,int y, int dblsize)
{
	Rectangle(hDC,x*cellwidth+1,y*cellheight+1,
	    (x+1)*cellwidth,(y+1)*cellheight);
	if(dblsize) {
		Rectangle(hDC,x*cellwidth+2,y*cellheight+2,
			(x+1)*cellwidth-1,(y+1)*cellheight-1);
	}
}

void DrawCell(HDC hDC,int x,int y)
{
	int allsame=1;
	int tmp;
	int i;

	SelectObject(hDC,pens.celloutline);
	SelectObject(hDC,brushes.cell);

	tmp=currfield[0][x][y];
	for(i=1;i<g.genmax;i++) {
		if(currfield[i][x][y]!=tmp) allsame=0;
	}

	ClearCell(hDC,x,y,!allsame);

	switch(currfield[curgen][x][y]) {
	case 0:        // must be off
		SelectObject(hDC,pens.cell_off);
		SelectObject(hDC,GetStockObject(NULL_BRUSH));
		Ellipse(hDC,x*cellwidth+3,y*cellheight+3,
			(x+1)*cellwidth-2,(y+1)*cellheight-2);
		break;
	case 1:	       // must be on
		SelectObject(hDC,brushes.cell_on);
		SelectObject(hDC,GetStockObject(NULL_PEN));
		Ellipse(hDC,x*cellwidth+3,y*cellheight+3,
			(x+1)*cellwidth-1,(y+1)*cellheight-1);
		break;
	case 3:       // unchecked
		SelectObject(hDC,pens.unchecked);
		MoveToEx(hDC,(x  )*cellwidth+2,(y  )*cellheight+2,NULL);
		LineTo(hDC,  (x+1)*cellwidth-2,(y+1)*cellheight-2);
		MoveToEx(hDC,(x+1)*cellwidth-2,(y  )*cellheight+2,NULL);
		LineTo(hDC,  (x  )*cellwidth+2,(y+1)*cellheight-2);

		break;
	case 4:       // frozen
		SelectObject(hDC,pens.cell_off);
		MoveToEx(hDC,x*cellwidth+2*cellwidth/3,y*cellheight+  cellheight/3,NULL);
		LineTo(hDC,  x*cellwidth+  cellwidth/3,y*cellheight+  cellheight/3);
		LineTo(hDC,  x*cellwidth+  cellwidth/3,y*cellheight+2*cellheight/3);

		MoveToEx(hDC,x*cellwidth+2*cellwidth/3,y*cellheight+  cellheight/2,NULL);
		LineTo(hDC,  x*cellwidth+  cellwidth/3,y*cellheight+  cellheight/2);

		break;

	}

}

// Set/reset unknown/unchecked
void ChangeChecking(HDC hDC, int x, int y, int allgens, int set)
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
		g1 = curgen;
		g2 = curgen;
	}

	pts=GetSymmetricCells(x,y,&numpts);

	for(j=g1;j<=g2;j++)
	{
		if (currfield[j][x][y] == s1)
		{
			currfield[j][x][y] = s2;
		}
	}
	DrawCell(hDC,x,y);

	for(i=0;i<numpts;i++) {
		for(j=g1;j<=g2;j++)
		{
			if (currfield[j][pts[i].x][pts[i].y] == s1)
			{
				currfield[j][pts[i].x][pts[i].y] = s2;
			}
		}
		DrawCell(hDC,pts[i].x,pts[i].y);
	}
}

// set and paint all cells symmetrical to the given cell
// (including the given cell)
void Symmetricalize(HDC hDC,int x,int y,int allgens)
{
	POINT *pts;
	int numpts,i,j;

	pts=GetSymmetricCells(x,y,&numpts);

	if(allgens) {
		for(j=0;j<GENMAX;j++) 
		{
			currfield[j][x][y]=currfield[curgen][x][y];
		}
	}
	DrawCell(hDC,x,y);

	for(i=0;i<numpts;i++) {
		currfield[curgen][pts[i].x][pts[i].y]=currfield[curgen][x][y];
		if(allgens) {
			for(j=0;j<GENMAX;j++)
			{
				currfield[j][pts[i].x][pts[i].y]=currfield[curgen][x][y];
			}
		}
		DrawCell(hDC,pts[i].x,pts[i].y);
	}
}


void InvertCells(HDC hDC1)
{
	RECT r;
	HDC hDC;

	if(endcell.x>=startcell.x) {
		r.left= startcell.x*cellwidth;
		r.right= (endcell.x+1)*cellwidth;
	}
	else {
		r.left= endcell.x*cellwidth;
		r.right=(startcell.x+1)*cellwidth;
	}

	if(endcell.y>=startcell.y) {
		r.top= startcell.y*cellheight;
		r.bottom= (endcell.y+1)*cellheight;
	}
	else {
		r.top=endcell.y*cellheight;
		r.bottom=(startcell.y+1)*cellheight;
	}


	if(hDC1)  hDC=hDC1;
	else      hDC=GetDC(hwndMain);

	InvertRect(hDC,&r);

	if(!hDC1) ReleaseDC(hwndMain,hDC);

}


void SelectOff(HDC hDC)
{
	if(selectstate<1) return;


	if(selectstate) {
		if(inverted) {
			InvertCells(hDC);
			inverted=0;
		}
		selectstate=0;
	}
}


void DrawWindow(HDC hDC)
{
	int i,j;
	for(i=0;i<g.colmax;i++) {
		for(j=0;j<g.rowmax;j++) {
			DrawCell(hDC,i,j);
		}
	}
	DrawGuides(hDC);

	if(selectstate>0) {
		InvertCells(hDC);
	}
}

void PaintWindow(HWND hWnd)
{
	HDC hdc;
	HPEN hOldPen;
	HBRUSH hOldBrush;
	PAINTSTRUCT ps;

	hdc= BeginPaint(hWnd,&ps);
	hOldPen= SelectObject(hdc, GetStockObject(BLACK_PEN));
	hOldBrush=SelectObject(hdc,GetStockObject(LTGRAY_BRUSH));

	DrawWindow(hdc);

	SelectObject(hdc,hOldPen);
	SelectObject(hdc,hOldBrush);
	EndPaint(hWnd,&ps);
}


void FixFrozenCells(void)
{
	int x,y,z;

	for(x=0;x<COLMAX;x++) {
		for(y=0;y<ROWMAX;y++) {
			for(z=0;z<GENMAX;z++) {
				if(z!=curgen) {
					if(currfield[curgen][x][y]==4) {
						currfield[z][x][y]=4;
					}
					else {  // current not frozen
						if(currfield[z][x][y]==4)
							currfield[z][x][y]=2;
					}
				}
			}
		}
	}
}


//returns 0 if processed
int ButtonClick(UINT msg,WORD xp,WORD yp,WPARAM wParam)
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

	xp+=(WORD)scrollpos.x;
	yp+=(WORD)scrollpos.y;

	x=xp/cellwidth;   // + scroll offset
	y=yp/cellheight;  // + scroll offset

	if(x<0 || x>=g.colmax) return 1;
	if(y<0 || y>=g.rowmax) return 1;


	lastval= currfield[curgen][x][y];
//	if(lastval==4) allgens=1;       // previously frozen


	switch(msg) {

	case WM_MOUSEMOVE:
		if(selectstate!=1) return 1;

		if(x==endcell.x && y==endcell.y) {   // cursor hasn't moved to a new cell
			return 0;
		}

		if(x==startcell.x && y==startcell.y) {  // cursor over starting cell
			hDC=GetDC(hwndMain);
			InvertCells(hDC); // turn off
			ReleaseDC(hwndMain,hDC);

			inverted=0;
			endcell.x=x;
			endcell.y=y;
			return 0;
		}


		// else we're at a different cell
			
		hDC=GetDC(hwndMain);
		if(inverted) InvertCells(hDC);    // turn off
		inverted=0;

		endcell.x=x;
		endcell.y=y;

		InvertCells(hDC);    // turn back on

		// record the select region
		if(startcell.x<=endcell.x) {
			selectrect.left= startcell.x;
			selectrect.right=endcell.x;
		}
		else {
			selectrect.left=endcell.x;
			selectrect.right=startcell.x;
		}
		if(startcell.y<=endcell.y) {
			selectrect.top= startcell.y;
			selectrect.bottom=endcell.y;
		}
		else {
			selectrect.top=endcell.y;
			selectrect.bottom=startcell.y;
		}



		inverted=1;
		ReleaseDC(hwndMain,hDC);
		
		return 0;

	case WM_LBUTTONDOWN:
		if(selectstate>0) {
			SelectOff(NULL);
		}

		selectstate=1;
		startcell.x=x;
		startcell.y=y;
		endcell.x=x;
		endcell.y=y;
		return 0;
		

	case WM_LBUTTONUP:
		if(wParam & MK_SHIFT) allgens=1;
		if(x==startcell.x && y==startcell.y) {
			selectstate=0;
			if(currfield[curgen][x][y]==1) newval=2; //currfield[curgen][x][y]=2;
			else newval=1;  //currfield[curgen][x][y]=1;
			//Symmetricalize(hDC,x,y,allgens);
		}
		else if(selectstate==1) {
			// selecting an area
			selectstate=2;
			return 0;

		}
		break;

	case WM_RBUTTONDOWN:     // toggle off/unchecked
		if(wParam & MK_SHIFT) allgens=1;
		if(currfield[curgen][x][y]==0) newval=2; //currfield[curgen][x][y]=2;
		else newval=0; //currfield[curgen][x][y]=0;
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

	hDC=GetDC(hwndMain);

	tmp=selectstate;

	SelectOff(hDC);

	if(searchstate == 0) {
		if(tmp==2) {
			if(newval>=0) {
				for(i=selectrect.left;i<=selectrect.right;i++) {
					for(j=selectrect.top;j<=selectrect.bottom;j++) {
						if (newval < 10)
						{
							currfield[curgen][i][j]=newval;
							Symmetricalize(hDC,i,j,allgens);
						} else {
							ChangeChecking(hDC,i,j,allgens,newval == 10);
						}
					}
				}
			}
		} else {
			if(newval>=0) 
			{
				if (newval < 10)
				{
					currfield[curgen][x][y]=newval;
					Symmetricalize(hDC,x,y,allgens);
				} else {
					ChangeChecking(hDC,x,y,allgens,newval == 10);
				}
			}
		}
	}

	FixFrozenCells();

	DrawGuides(hDC);

	ReleaseDC(hwndMain,hDC);
	return 0;
}

// copy my format to dbells format...
// ... and make a backup of the current state (KAS)
BOOL set_initial_cells(void)
{
	CELL *cell;
	CELL **setpos;
	BOOL change;
	TCHAR buf[80];
	int i,j,g1;

	newset = settable;
	nextset = settable;

	for(g1=0;g1<g.genmax;g1++) {
		for(i=0;i<g.colmax;i++) {
			for(j=0;j<g.rowmax;j++) {

				origfield[g1][i][j] = currfield[g1][i][j];

				switch(currfield[g1][i][j]) {
				case 0:  // forced off
					if(!proceed(findcell(j+1,i+1,g1),OFF,FALSE)) {
						StringCbPrintf(buf,sizeof(buf),_T("Inconsistent OFF state for cell (col %d,row %d,gen %d)"),i+1,j+1,g1);
						wlsMessage(buf,0);
						return FALSE;
					}
					break;
				case 1:  // forced on
					if(!proceed(findcell(j+1,i+1,g1),ON,FALSE)) {
						StringCbPrintf(buf,sizeof(buf),_T("Inconsistent ON state for cell (col %d,row %d,gen %d)"),i+1,j+1,g1);
						wlsMessage(buf,0);
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

	setpos = newset;
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
								if (setpos != newset) backup();
								if (proceed(cell, OFF, TRUE))
								{
									change = TRUE;
								} else {
									// we should never get here
									// because it's already tested that the OFF state is possible
									wlsMessage(_T("Program inconsistency found"),0);
									return FALSE;
								}
							}							
						} else {
							// can't set OFF state
							// let's try ON state
							if (setpos != newset) backup();
							if (proceed(cell, ON, TRUE))
							{
								change = TRUE;
							} else {
								// can't set neither ON nor OFF state
								StringCbPrintf(buf,sizeof(buf),_T("Inconsistent UNK state for cell (col %d,row %d,gen %d)"),i+1,j+1,g1);
								wlsMessage(buf,0);
								return FALSE;
							}
						}
					}
				}
			}
		}
	} while (change);

	newset = settable;
	nextset = settable;

	return TRUE;
}

void draw_gen_counter(void)
{
	TCHAR buf[80];
	SCROLLINFO si;

	si.cbSize=sizeof(SCROLLINFO);
	si.fMask=SIF_ALL;
	si.nMin=0;
	si.nMax=g.genmax-1;
	si.nPage=1;
	si.nPos=curgen;
	si.nTrackPos=0;

	SetScrollInfo(hwndGenScroll,SB_CTL,&si,TRUE);

	StringCbPrintf(buf,sizeof(buf),_T("%d"),curgen);
	SetWindowText(hwndGen,buf);
}

void showcount(void)
{
	TCHAR buf[80];
	static int tot=0;

	if (viewcount<0) {
		tot=0;
	} else {
		tot += viewcount;
	}
	viewcount = 0;

	StringCbPrintf(buf,sizeof(buf),_T("WinLifeSearch [%d]"),tot);
	SetWindowText(hwndFrame,buf);
}


// possible values for type:
// 0 - nothing happens
// 1 - store the generation as a solution
// 2 - copy solution

void printgen(void)
{
	int i,j,g1;
	CELL *cell;

	// copy dbell's format back into mine
	for(g1=0;g1<g.genmax;g1++) {
		for(i=0;i<g.colmax;i++){
			for(j=0;j<g.rowmax;j++) {

				cell=findcell(j+1,i+1,g1);

				switch (cell->state) {
				case ON:
					currfield[g1][i][j] = 1;
					break;
				case OFF:
					currfield[g1][i][j] = 0;
					break;
				case UNK:
					currfield[g1][i][j] = origfield[g1][i][j];
				}
			}
		}
	}

	InvalidateRect(hwndMain,NULL,FALSE);
}

void do_combine(void)
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
						--combinedcells;
						cell->combined = UNK;
					}
				}
			}
		}
	} else {
		g.combining = TRUE;
		combinedcells = 0;
		for(g1=0;g1<g.genmax;g1++) {
			for(i=0;i<g.colmax;i++){
				for(j=0;j<g.rowmax;j++) {					
					cell=findcell(j+1,i+1,g1);
					if ((origfield[g1][i][j] > 1) && ((cell->state == ON) || (cell->state == OFF)))
					{
						++combinedcells;
						cell->combined = cell->state;
					} else {
						cell->combined = UNK;
					}
				}
			}
		}
	}
	setcombinedcells = combinedcells;
	differentcombinedcells = 0;
}

void show_combine(void)
{
	int i,j,g1;
	CELL *cell;

	if (combinedcells > 0)
	{
		for(g1=0;g1<g.genmax;g1++) {
			for(i=0;i<g.colmax;i++){
				for(j=0;j<g.rowmax;j++) {					
					cell=findcell(j+1,i+1,g1);
					switch(cell->combined) {
					case ON:
						currfield[g1][i][j] = 1;
						break;
					case OFF:
						currfield[g1][i][j] = 0;
						break;
					case UNK:
						currfield[g1][i][j] = origfield[g1][i][j];
					}
				}
			}
		}
	}

	InvalidateRect(hwndMain,NULL,FALSE);
}

void pause_search(void);  // forward decl

DWORD WINAPI search_thread(LPVOID foo)
{
	BOOL reset = 0;
	TCHAR buf[180];
	int i, j, k;

	/*
	 * Initial commands are complete, now look for the object.
	 */
	while (TRUE)
	{
		if (g.curstatus == OK)
			g.curstatus = search();

		if(abortthread) goto done;

		if ((g.curstatus == FOUND) && g.userow && (rowinfo[g.userow].oncount == 0)) {
			g.curstatus = OK;
			continue;
		}

		if ((g.curstatus == FOUND) && !g.allobjects && !g.parent && subperiods()) {
			g.curstatus = OK;
			continue;
		}

		if (dumpfreq) {
			dumpcount = 0;
			dumpstate(dumpfile, FALSE);
		}

		if ((g.curstatus == FOUND) && g.combine)
		{
			g.curstatus = OK;
			++foundcount;
            do_combine();
			writegen(outputfile, TRUE);
			StringCbPrintf(buf,sizeof(buf),_T("Combine: %d cells remaining from %d solutions"), combinedcells, foundcount);
			wlsStatus(buf);
			continue;
		}

		/*
		 * Here if results are going to a file.
		 */
		if (g.curstatus == FOUND) {
			g.curstatus = OK;

			showcount();
			printgen();
			++foundcount;
			StringCbPrintf(buf,sizeof(buf),_T("Object %d found.\n"), foundcount);
			wlsStatus(buf);

			writegen(outputfile, TRUE);
			if (!stoponfound) continue;
			goto done;
		}

		if (g.combine)
		{
			show_combine();
			if (g.combining)
			{
				StringCbPrintf(buf,sizeof(buf),_T("Search completed: %d cell%s found"),
					combinedcells, (combinedcells == 1) ? _T("") : _T("s"));
				reset = (combinedcells == 0);
			}
			else
			{
				StringCbPrintf(buf,sizeof(buf),_T("Search completed: no solution"));
				reset = 1;
			}
		} else {
			showcount();
			printgen();
			StringCbPrintf(buf,sizeof(buf),_T("Search completed:  %d object%s found"),
				foundcount, (foundcount == 1) ? _T("") : _T("s"));

			reset = (foundcount == 0);
		}

		wlsMessage(buf,0);

		goto done;
	}
done:
	if (reset)
	{
		for(k=0;k<GENMAX;k++) 
			for(i=0;i<COLMAX;i++)
				for(j=0;j<ROWMAX;j++)
					currfield[k][i][j]=origfield[k][i][j];
		searchstate = 0;
		InvalidateRect(hwndMain,NULL,FALSE);
	}
	else
	{
		searchstate=1;
	}
	_endthreadex(0);
	return 0;
}


void resume_search(void)
{
	DWORD threadid;

	SetWindowText(hwndFrame,_T("WinLifeSearch"));
	if (g.combine)
	{
		if (g.combining)
		{
			TCHAR buf[80];
			StringCbPrintf(buf,sizeof(buf),_T("Combine: %d cells remaining"), combinedcells);
			wlsStatus(buf);
		}
		else
		{
			wlsStatus(_T("Combining..."));
		}
	}
	else
	{
		wlsStatus(_T("Searching..."));
	}

	if(searchstate!=1) {
		wlsError(_T("No search is paused"),0);
		return;
	}
	abortthread=0;

	searchstate=2;
	hthread=(HANDLE)_beginthreadex(NULL,0,search_thread,(void*)0,0,&threadid);


	if(hthread==NULL) {
		wlsError(_T("Unable to create search thread"),0);
		searchstate=1;
	}
	else {
		SetWindowText(hwndFrame,_T("WinLifeSearch"));
		SetThreadPriority(hthread,THREAD_PRIORITY_BELOW_NORMAL);
	}

}

BOOL prepare_search(BOOL load)
{
	int i;

	if(searchstate!=0) {
		wlsError(_T("A search is already running"),0);
		return FALSE;
	}

	if (!setrules(rulestring))
	{
		wlsError(_T("Cannot set Life rules!"),0);
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
		colinfo[i].oncount=0;
		colinfo[i].setcount=0;
		colinfo[i].sumpos=0;
	}

	for(i=0;i<ROWMAX;i++) rowinfo[i].oncount=0;

	newset=NULL;
	nextset=NULL;
	outputlastcols=0;
	fullcolumns=0;
	g.curstatus=OK;
	g0oncellcount = 0; // KAS
	cellcount = 0; // KAS
	smartchoice = UNK; // KAS

	viewcount = -1; // KAS
	showcount(); // KAS

	foundcount=0;
	writecount=0;

	g.combining = FALSE;
	combinedcells = 0;
	setcombinedcells = 0;
	differentcombinedcells = 0;

	if (load) {
		if(!loadstate())
		{
			InvalidateRect(hwndMain,NULL,TRUE);
			return FALSE;
		}

		InvalidateRect(hwndMain,NULL,TRUE);

		draw_gen_counter();
	} else {
		initcells();

		if(!set_initial_cells()) 
		{
			record_malloc(0,NULL); // release allocated memory
			return FALSE;   // there was probably an inconsistency in the initial cells
		}

		initsearchorder(); 
	}
	searchstate=1;  // pretend the search is "paused"

	printgen();

	return TRUE;
}

void start_search(void)
{
	if (prepare_search(FALSE))
	{
		resume_search();
	}
}


void pause_search(void)
{
	DWORD exitcode;

	if(searchstate!=2) {
		wlsError(_T("No search is running"),0);
		return;
	}
	abortthread=1;
	do {
		GetExitCodeThread(hthread, &exitcode);
		if(exitcode==STILL_ACTIVE) {
			Sleep(200);
		}
	} while(exitcode==STILL_ACTIVE);
	CloseHandle(hthread);
	searchstate=1;

	printgen();

	SetWindowText(hwndFrame,_T("WinLifeSearch - Paused"));

}


void reset_search(void)
{
	int i,j,k;

	if(searchstate==0) {
		wlsError(_T("No search in progress"),0);
		goto here;
	}

	// stop the search thread if it is running
	if(searchstate==2) {
		pause_search();
	}

	searchstate=0;

	record_malloc(0,NULL);    // free memory

	// restore the original cells
	for(k=0;k<GENMAX;k++) 
		for(i=0;i<COLMAX;i++)
			for(j=0;j<ROWMAX;j++)
				currfield[k][i][j]=origfield[k][i][j];

here:
	InvalidateRect(hwndMain,NULL,FALSE);

	SetWindowText(hwndFrame,_T("WinLifeSearch"));
	wlsStatus(_T(""));

}

void open_state(void)
{
	if(searchstate!=0) reset_search();

	prepare_search(TRUE);
}


void gen_changeby(int delta)
{
	if(g.genmax<2) return;
	curgen+=delta;
	if(curgen>=g.genmax) curgen=0;
	if(curgen<0) curgen=g.genmax-1;

	draw_gen_counter();
	InvalidateRect(hwndMain,NULL,FALSE);
}

void hide_selection(void)
{
	SelectOff(NULL);
}

void clear_gen(int g1)
{	int i,j;
	for(i=0;i<COLMAX;i++)
		for(j=0;j<ROWMAX;j++)
			currfield[g1][i][j]=2;
}

void clear_all(void)
{	int g1;
	for(g1=0;g1<GENMAX;g1++)
		clear_gen(g1);
	hide_selection();
}

void flip_h(int fromgen, int togen)
{
	int g1, r, c;
	int fromrow, torow, fromcol, tocol;
	int buffer;

	if (selectstate == 2)
	{
		fromcol = selectrect.left;
		tocol = selectrect.right;
		fromrow = selectrect.top;
		torow = selectrect.bottom;
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
				buffer = currfield[g1][c][r];
				currfield[g1][c][r] = currfield[g1][tocol + fromcol - c][r];
				currfield[g1][tocol + fromcol - c][r] = buffer;
			}
		}	
	}
}

void flip_v(int fromgen, int togen)
{
	int g1, r, c;
	int fromrow, torow, fromcol, tocol;
	int buffer;

	if (selectstate == 2)
	{
		fromcol = selectrect.left;
		tocol = selectrect.right;
		fromrow = selectrect.top;
		torow = selectrect.bottom;
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
				buffer = currfield[g1][c][r];
				currfield[g1][c][r] = currfield[g1][c][torow + fromrow - r];
				currfield[g1][c][torow + fromrow - r] = buffer;
			}
		}	
	}
}

void transpose(int fromgen, int togen)
{
	int g1, r, c;
	int fromrow, torow, fromcol, tocol;
	int buffer;

	if (selectstate == 2)
	{
		fromcol = selectrect.left;
		tocol = selectrect.right;
		fromrow = selectrect.top;
		torow = selectrect.bottom;
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
				buffer = currfield[g1][c][r];
				currfield[g1][c][r] = currfield[g1][fromcol - fromrow + r][fromrow - fromcol + c];
				currfield[g1][fromcol - fromrow + r][fromrow - fromcol + c] = buffer;
			}
		}	
	}
}

void shift_gen(int fromgen, int togen, int gend, int cold, int rowd)
{
	int g1,r,c;
	int fromrow, torow, fromcol, tocol;
	int gx,rx,cx;

	if (selectstate == 2)
	{
		fromcol = selectrect.left;
		tocol = selectrect.right;
		fromrow = selectrect.top;
		torow = selectrect.bottom;
	} else {
		fromcol = 0;
		tocol = g.colmax - 1;
		fromrow = 0;
		torow = g.rowmax - 1;
	}

	for(g1=fromgen; g1<=togen; g1++) {
		for(c=fromcol; c<=tocol; c++) {
			for (r=fromrow; r<=torow; r++) {
				origfield[g1][c][r] = currfield[g1][c][r];
			}
		}
	}

	for(g1=fromgen; g1<=togen; g1++) {
		for(c=fromcol; c<=tocol; c++) {
			for (r=fromrow; r<=torow; r++) {
				gx = (g1 + gend - fromgen + togen - fromgen + 1) % (togen - fromgen + 1) + fromgen;
				cx = (c + cold - fromcol + tocol - fromcol + 1) % (tocol - fromcol + 1) + fromcol;
				rx = (r + rowd - fromrow + torow - fromrow + 1) % (torow - fromrow + 1) + fromrow;
				currfield[gx][cx][rx] = origfield[g1][c][r];
			}
		}
	}
}

void copy_result(void)
{
	int g1, i, j;

	if (searchstate == 0) return;

	if (searchstate != 1) pause_search();

	for(g1=0;g1<g.genmax;g1++) {
		for(i=0;i<g.colmax;i++){
			for(j=0;j<g.rowmax;j++) {
				origfield[g1][i][j] = currfield[g1][i][j];
			}
		}
	}

	if (searchstate != 0) reset_search();
}

void copy_combination(void)
{
	int g1, i, j;

	if (searchstate == 0) return;

	if (searchstate != 1) pause_search();

	show_combine();

	for(g1=0;g1<g.genmax;g1++) {
		for(i=0;i<g.colmax;i++){
			for(j=0;j<g.rowmax;j++) {
				origfield[g1][i][j] = currfield[g1][i][j];
			}
		}
	}

	if (searchstate != 0) reset_search();
}

void clear_combination(void)
{
	if (searchstate == 0) return;

	if (searchstate != 1) pause_search();

	g.combining = 0;
}

void copytoclipboard(void)
{
	DWORD size;
	HGLOBAL hClip;
	LPVOID lpClip;
	TCHAR buf[100],buf2[10];
	TCHAR *s;
	int i,j;
	int offset;

	if(searchstate==0) {
		// bornrules/liverules may not be set up yet
		// probably we could call setrules().
		StringCbCopy(buf,sizeof(buf),_T("#P 0 0\r\n"));
	}
	else {
		// unfortunately the rulestring is in the wrong format for life32
		StringCbCopy(buf,sizeof(buf),_T("#P 0 0\r\n#R S"));
		for(i=0;i<=8;i++) {
			if(liverules[i]) {StringCbPrintf(buf2,sizeof(buf2),_T("%d"),i); StringCbCat(buf,sizeof(buf),buf2);}
		}
		StringCbCat(buf,sizeof(buf),_T("/B"));
		for(i=0;i<=8;i++) {
			if(bornrules[i]) {StringCbPrintf(buf2,sizeof(buf2),_T("%d"),i); StringCbCat(buf,sizeof(buf),buf2);}
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
			if(currfield[curgen][i][j]==1)
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

static void handle_MouseWheel(HWND hWnd, WPARAM wParam)
{
	signed short delta = (signed short)(HIWORD(wParam));
	while(delta >= 120) {
		gen_changeby(-1);
		delta -= 120;
	}
	while(delta <= -120) {
		gen_changeby(1);
		delta += 120;
	}
}

/****************************************************************************/
static LRESULT CALLBACK WndProcFrame(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
	WORD id;
	POINT pt;

	id=LOWORD(wParam);

	switch(msg) {

	case WM_MOUSEWHEEL:
		handle_MouseWheel(hWnd, wParam);
		return 0;

	case WM_CREATE:
#ifdef _DEBUG
		pens.celloutline= CreatePen(PS_SOLID,0,RGB(0xc0,0x00,0x00));
#else
		pens.celloutline= CreatePen(PS_SOLID,0,RGB(0x00,0x00,0x00));
#endif
		pens.axes=        CreatePen(PS_DOT  ,0,RGB(0x00,0x00,0xff));
		pens.cell_off=    CreatePen(PS_SOLID,0,RGB(0x00,0x00,0xff));
		pens.arrow1=      CreatePen(PS_SOLID,2,RGB(0x00,0xff,0x00));
		pens.arrow2=      CreatePen(PS_SOLID,2,RGB(0xff,0x00,0x00));
		pens.unchecked=   CreatePen(PS_SOLID,0,RGB(0x00,0x00,0x00));
		brushes.cell=     CreateSolidBrush(RGB(0xc0,0xc0,0xc0));
		brushes.cell_on=  CreateSolidBrush(RGB(0x00,0x00,0xff));
		// >>>
		return 0;
	case WM_CLOSE:
		DestroyWindow(hWnd);
		return 0;
	case WM_DESTROY:
		DeleteObject(pens.celloutline);
		DeleteObject(pens.axes);
		DeleteObject(pens.cell_off);
		DeleteObject(pens.arrow1);
		DeleteObject(pens.arrow2);
		DeleteObject(pens.unchecked);
		DeleteObject(brushes.cell);
		DeleteObject(brushes.cell_on);

		PostQuitMessage(0);
		return 0;
	case WM_SIZE:
		// set size of children
		SetWindowPos(hwndMain,NULL,0,0,LOWORD(lParam),
			HIWORD(lParam)-TOOLBARHEIGHT,SWP_NOZORDER);
		SetWindowPos(hwndToolbar,NULL,0,HIWORD(lParam)-TOOLBARHEIGHT,
			LOWORD(lParam),TOOLBARHEIGHT,SWP_NOZORDER);
		return 0;
		
	case WM_INITMENU:
		EnableMenuItem((HMENU)wParam,IDC_SEARCHSTART,searchstate==0?MF_ENABLED:MF_GRAYED);
		EnableMenuItem((HMENU)wParam,IDC_SEARCHPREPARE,searchstate==0?MF_ENABLED:MF_GRAYED);
		EnableMenuItem((HMENU)wParam,IDC_SEARCHRESET,searchstate!=0?MF_ENABLED:MF_GRAYED);
		EnableMenuItem((HMENU)wParam,IDC_SEARCHPAUSE,searchstate==2?MF_ENABLED:MF_GRAYED);
		EnableMenuItem((HMENU)wParam,IDC_SEARCHRESUME,searchstate==1?MF_ENABLED:MF_GRAYED);
		EnableMenuItem((HMENU)wParam,IDC_SEARCHBACKUP,searchstate!=0?MF_ENABLED:MF_GRAYED);
		EnableMenuItem((HMENU)wParam,IDC_SEARCHBACKUP2,searchstate!=0?MF_ENABLED:MF_GRAYED);
		EnableMenuItem((HMENU)wParam,IDC_COPYRESULT,searchstate!=0?MF_ENABLED:MF_GRAYED);
		EnableMenuItem((HMENU)wParam,IDC_COPYCOMBINATION,searchstate!=0?MF_ENABLED:MF_GRAYED);
		EnableMenuItem((HMENU)wParam,IDC_CLEARCOMBINATION,searchstate!=0?MF_ENABLED:MF_GRAYED);
		return 0;

	case WM_CHAR:
		GetCursorPos(&pt);
		ScreenToClient(hWnd,&pt);
		return ButtonClick(msg,(WORD)pt.x,(WORD)pt.y,wParam);


	case WM_COMMAND:
		switch(id) {
		case IDC_EXIT:
			DestroyWindow(hWnd);
			return 0;
		case IDC_ABOUT:
			DialogBox(hInst,_T("DLGABOUT"),hWnd,DlgProcAbout);
			return 0;
		case IDC_ROWS:
			DialogBox(hInst,_T("DLGROWS"),hWnd,DlgProcRows);
			return 0;
		case IDC_PERIOD:
			DialogBox(hInst,_T("DLGPERIOD"),hWnd,DlgProcPeriod);
			return 0;
		case IDC_SYMMETRY:
			DialogBox(hInst,_T("DLGSYMMETRY"),hWnd,DlgProcSymmetry);
			return 0;
		case IDC_TRANSLATE:
			DialogBox(hInst,_T("DLGTRANSLATE"),hWnd,DlgProcTranslate);
			return 0;
		case IDC_OUTPUTSETTINGS:
			DialogBox(hInst,_T("DLGSETTINGS"),hWnd,DlgProcOutput);
			return 0;
		case IDC_SEARCHSETTINGS:
			DialogBox(hInst,_T("DLGSEARCHSETTINGS"),hWnd,DlgProcSearch);
			return 0;
		case IDC_SEARCHSTART:
			start_search();
			return 0;
		case IDC_SEARCHPREPARE:
			prepare_search(FALSE);
			return 0;
		case IDC_SEARCHPAUSE:
			pause_search();
			return 0;
		case IDC_SEARCHRESET:
			reset_search();
			return 0;
		case IDC_SEARCHRESUME:
			resume_search();
			return 0;
		case IDC_SEARCHBACKUP:
			if (searchstate==2)
				pause_search();
			if(searchstate==1)
				getbackup("1 ");
			return 0;
		case IDC_SEARCHBACKUP2:
			if (searchstate==2)
				pause_search();
			if(searchstate==1)
				getbackup("20 ");
			return 0;
		case IDC_OPENSTATE:
			open_state();
			return 0;
		case IDC_SAVEGAME:
			if (searchstate==0) {
				if (prepare_search(FALSE)) {
					dumpstate(NULL, FALSE);
					reset_search();
				}
			} else if (searchstate==1) {
				dumpstate(NULL, FALSE);
			} else if (searchstate==2) {
				pause_search();
				dumpstate(NULL, FALSE);
				resume_search();
			}
			return 0;
		case IDC_NEXTGEN:
			gen_changeby(1);
			return 0;
		case IDC_PREVGEN:
			gen_changeby(-1);
			return 0;
		case IDC_EDITCOPY:
			copytoclipboard();
			return 0;
		case IDC_CLEARGEN:
			if(searchstate == 0) clear_gen(curgen);
			hide_selection();
			InvalidateRect(hwndMain,NULL,FALSE);
			return 0;
		case IDC_CLEAR:
			if(searchstate == 0) clear_all();
			InvalidateRect(hwndMain,NULL,FALSE);
			return 0;
		case IDC_COPYRESULT:
			if(searchstate != 0) copy_result();
			return 0;
		case IDC_COPYCOMBINATION:
			if (searchstate != 0) copy_combination();
			return 0;
		case IDC_CLEARCOMBINATION:
			if (searchstate != 0) clear_combination();
			return 0;
		case IDC_SHIFTGUP:
			if(searchstate == 0) shift_gen(curgen, curgen, 0, 0, -1);
			InvalidateRect(hwndMain,NULL,selectstate == 2);
			return 0;
		case IDC_SHIFTGDOWN:
			if(searchstate == 0) shift_gen(curgen, curgen, 0, 0, 1);
			InvalidateRect(hwndMain,NULL,selectstate == 2);
			return 0;
		case IDC_SHIFTGLEFT:
			if(searchstate == 0) shift_gen(curgen, curgen, 0, -1, 0);
			InvalidateRect(hwndMain,NULL,selectstate == 2);
			return 0;
		case IDC_SHIFTGRIGHT:
			if(searchstate == 0) shift_gen(curgen, curgen, 0, 1, 0);
			InvalidateRect(hwndMain,NULL,selectstate == 2);
			return 0;
		case IDC_SHIFTAUP:
			if(searchstate == 0) shift_gen(0, GENMAX-1, 0, 0, -1);
			InvalidateRect(hwndMain,NULL,selectstate == 2);
			return 0;
		case IDC_SHIFTADOWN:
			if(searchstate == 0) shift_gen(0, GENMAX-1, 0, 0, 1);
			InvalidateRect(hwndMain,NULL,selectstate == 2);
			return 0;
		case IDC_SHIFTALEFT:
			if(searchstate == 0) shift_gen(0, GENMAX-1, 0, -1, 0);
			InvalidateRect(hwndMain,NULL,selectstate == 2);
			return 0;
		case IDC_SHIFTARIGHT:
			if(searchstate == 0) shift_gen(0, GENMAX-1, 0, 1, 0);
			InvalidateRect(hwndMain,NULL,selectstate == 2);
			return 0;
		case IDC_SHIFTAPAST:
			if(searchstate == 0) shift_gen(0, g.genmax-1, -1, 0, 0);
			InvalidateRect(hwndMain,NULL,selectstate == 2);
			return 0;
		case IDC_SHIFTAFUTURE:
			if(searchstate == 0) shift_gen(0, g.genmax-1, 1, 0, 0);
			InvalidateRect(hwndMain,NULL,selectstate == 2);
			return 0;
		case ID_FLIP_GEN_H:
			if(searchstate == 0) flip_h(curgen, curgen);
			InvalidateRect(hwndMain,NULL,selectstate == 2);
			return 0;
		case ID_FLIP_GEN_V:
			if(searchstate == 0) flip_v(curgen, curgen);
			InvalidateRect(hwndMain,NULL,selectstate == 2);
			return 0;
		case ID_FLIP_ALL_H:
			if(searchstate == 0) flip_h(0, GENMAX - 1);
			InvalidateRect(hwndMain,NULL,selectstate == 2);
			return 0;
		case ID_FLIP_ALL_V:
			if(searchstate == 0) flip_v(0, GENMAX - 1);
			InvalidateRect(hwndMain,NULL,selectstate == 2);
			return 0;
		case ID_TRANS_GEN:
			if(searchstate == 0) transpose(curgen, curgen);
			InvalidateRect(hwndMain,NULL,selectstate == 2);
			return 0;
		case ID_TRANS_ALL:
			if(searchstate == 0) transpose(0, GENMAX - 1);
			InvalidateRect(hwndMain,NULL,selectstate == 2);
			return 0;
		case ID_HIDESEL:
			hide_selection();
			return 0;
		break;
		}
	}

	return (DefWindowProc(hWnd, msg, wParam, lParam));
}



static LRESULT CALLBACK WndProcMain(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
	WORD id;
	id = sizeof(CELL);
	id=LOWORD(wParam);

	switch(msg) {
	case WM_PAINT:
		PaintWindow(hWnd);
		return 0;
	case WM_HSCROLL:
		switch(id) {
		case SB_LINELEFT: scrollpos.x-=cellwidth; break;
		case SB_LINERIGHT: scrollpos.x+=cellwidth; break;
		case SB_PAGELEFT: scrollpos.x-=cellwidth*4; break;
		case SB_PAGERIGHT: scrollpos.x+=cellwidth*4; break;
		case SB_THUMBTRACK:
			if(HIWORD(wParam)==scrollpos.x) return 0;
			scrollpos.x=HIWORD(wParam);
			break;
		default:
			return 0;
		}
		set_main_scrollbars(1);
		return 0;
	case WM_VSCROLL:
		switch(id) {
		case SB_LINELEFT: scrollpos.y-=cellheight; break;
		case SB_LINERIGHT: scrollpos.y+=cellheight; break;
		case SB_PAGELEFT: scrollpos.y-=cellheight*4; break;
		case SB_PAGERIGHT: scrollpos.y+=cellheight*4; break;
		case SB_THUMBTRACK:
			if(HIWORD(wParam)==scrollpos.y) return 0;
			scrollpos.y=HIWORD(wParam);
			break;
		default:
			return 0;
		}
		set_main_scrollbars(1);
		return 0;
	case WM_SIZE:
		set_main_scrollbars(0);
		return 0;
	case WM_LBUTTONDOWN:
	case WM_RBUTTONDOWN:
	case WM_LBUTTONUP:
	case WM_MOUSEMOVE:
		if(searchstate==0) {
			ButtonClick(msg,LOWORD(lParam),HIWORD(lParam),wParam);
		}
		return 0;
	}

	return (DefWindowProc(hWnd, msg, wParam, lParam));
}


static LRESULT CALLBACK WndProcToolbar(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
	HWND htmp;
	WORD id;

	int ori_gen;

	id=LOWORD(wParam);

	switch(msg) {
	case WM_HSCROLL:
		htmp=(HWND)(lParam);
		if(htmp==hwndGenScroll) {
			ori_gen=curgen;
			switch(id) {
			case SB_LINELEFT: curgen--; break;
			case SB_LINERIGHT: curgen++; break;
			case SB_PAGELEFT: curgen--; break;
			case SB_PAGERIGHT: curgen++; break;
			case SB_LEFT: curgen=0; break;
			case SB_RIGHT: curgen=g.genmax-1; break;
			case SB_THUMBPOSITION: curgen=HIWORD(wParam); break;
			case SB_THUMBTRACK: curgen=HIWORD(wParam); break;
			}
			if(curgen<0) curgen=g.genmax-1;  // wrap around
			if(curgen>=g.genmax) curgen=0;
			if(curgen!=ori_gen) {
				draw_gen_counter();
				InvalidateRect(hwndMain,NULL,FALSE);
			}
			return 0;
		}
		break;


	case WM_CREATE:
		hwndGen=CreateWindow(_T("Static"),_T("0"),
			WS_CHILD|WS_VISIBLE|WS_BORDER,
			1,1,40,TOOLBARHEIGHT-2,
			hWnd,NULL,hInst,NULL);
		hwndGenScroll=CreateWindow(_T("Scrollbar"),_T("wls_gen_scrollbar"),
			WS_CHILD|WS_VISIBLE|SBS_HORZ,
			41,1,80,TOOLBARHEIGHT-2,
			hWnd,NULL,hInst,NULL);
		hwndStatus=CreateWindow(_T("Static"),_T(""),
			WS_CHILD|WS_VISIBLE|WS_BORDER,
			120,1,800,TOOLBARHEIGHT-2,
			hWnd,NULL,hInst,NULL);
#ifdef _DEBUG
			wlsStatus(_T("DEBUG BUILD"));
#endif

			draw_gen_counter();
		return 0;

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
			SetCenter();

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

			set_main_scrollbars(1);
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
			if(curgen>=g.genmax) curgen=g.genmax-1;

			draw_gen_counter();
			InvalidateRect(hwndMain,NULL,TRUE);
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
		SetDlgItemInt(hWnd,IDC_VIEWFREQ,viewfreq,FALSE);
		SetDlgItemText(hWnd,IDC_DUMPFILE,dumpfile);
		SetDlgItemInt(hWnd,IDC_DUMPFREQ,dumpfreq,FALSE);
		CheckDlgButton(hWnd,IDC_OUTPUTFILEYN,saveoutput?BST_CHECKED:BST_UNCHECKED);
		CheckDlgButton(hWnd,IDC_OUTPUTALLGEN,saveoutputallgen?BST_CHECKED:BST_UNCHECKED);
		CheckDlgButton(hWnd,IDC_STOPONFOUND,stoponfound?BST_CHECKED:BST_UNCHECKED);
		CheckDlgButton(hWnd,IDC_STOPONSTEP,stoponstep?BST_CHECKED:BST_UNCHECKED);
		SetDlgItemText(hWnd,IDC_OUTPUTFILE,outputfile);
		SetDlgItemInt(hWnd,IDC_OUTPUTCOLS,outputcols,FALSE);
		return 1;

	case WM_COMMAND:
		switch(id) {
		case IDOK:
			viewfreq=GetDlgItemInt(hWnd,IDC_VIEWFREQ,NULL,FALSE);
			dumpfreq=GetDlgItemInt(hWnd,IDC_DUMPFREQ,NULL,FALSE);
			GetDlgItemText(hWnd,IDC_DUMPFILE,dumpfile,79);
			saveoutput=(IsDlgButtonChecked(hWnd,IDC_OUTPUTFILEYN)==BST_CHECKED);
			saveoutputallgen=(IsDlgButtonChecked(hWnd,IDC_OUTPUTALLGEN)==BST_CHECKED);
			stoponfound=(IsDlgButtonChecked(hWnd,IDC_STOPONFOUND)==BST_CHECKED);
			stoponstep=(IsDlgButtonChecked(hWnd,IDC_STOPONSTEP)==BST_CHECKED);
			GetDlgItemText(hWnd,IDC_OUTPUTFILE,outputfile,79);
			outputcols=GetDlgItemInt(hWnd,IDC_OUTPUTCOLS,NULL,FALSE);
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

		SetDlgItemText(hWnd,IDC_RULESTRING,rulestring);
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

			GetDlgItemText(hWnd,IDC_RULESTRING,rulestring,50);

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

			InvalidateRect(hwndMain,NULL,FALSE);
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
			InvalidateRect(hwndMain,NULL,FALSE);
			// fall through
		case IDCANCEL:
			EndDialog(hWnd, TRUE);
			return 1;
		}
		break;
	}
	return 0;		// Didn't process a message
}


