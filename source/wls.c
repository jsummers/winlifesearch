// WinLifeSearch
// a Windows port of David I. Bell's lifesrc program
// By Jason Summers
//
// !!!! PLEASE NOTE !!!!
// Unlike the original lifesrc, this port is incomplete, full
// of bugs, and has many serious design problems. If you want to
// make a good port, you might consider starting over, rather
// than try to fix up this mess.     -JES
// 


#define _CRT_SECURE_NO_WARNINGS
#include <windows.h>
#include <stdio.h>
#include <process.h>
#include <assert.h>
#include "resource.h"
#include "wls.h"

#include "lifesrc.h"


#define cellheight 15

#define cellwidth  15

// #define maxfieldsize 87
// #define maxgens    8
#define TOOLBARHEIGHT 24

char rulestring[20];
int saveoutput;



UINT msg_mswheel;

HINSTANCE hInst;
HWND hwndFrame,hwndMain,hwndToolbar;
HWND hwndGen,hwndGenScroll;

LRESULT CALLBACK WndProcFrame(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);
LRESULT CALLBACK WndProcMain(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);
LRESULT CALLBACK WndProcToolbar(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);
BOOL CALLBACK DlgProcAbout(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);
BOOL CALLBACK DlgProcSymmetry(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);
BOOL CALLBACK DlgProcTranslate(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);
BOOL CALLBACK DlgProcRows(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);
BOOL CALLBACK DlgProcPeriod(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);
BOOL CALLBACK DlgProcOutput(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);
BOOL CALLBACK DlgProcSearch(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);
BOOL CALLBACK DlgProcTest(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);


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

//typedef struct {
//	int rotate,flip,x,y;
//} translate_type;


volatile int abortthread;
HANDLE hthread;
//int threadstate=0;
int searchstate=0;
int foundcount;

pens_type pens;
brushes_type brushes;
//translate_type translate;

//int trans_rotate, trans_flip, trans_x, trans_y;
//int symmetry;

int colmax,rowmax;
int centerx,centery,centerxodd,centeryodd;
int genmax;
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


void wlsError(char *m,int n)
{
	char s[80];
	if(n) sprintf(s,"%s (%d)",m,n);
	else sprintf(s,"%s",m);
	MessageBox(hwndFrame,s,"Error",MB_OK|MB_ICONWARNING);
}

void wlsWarning(char *m,int n)
{
	char s[80];
	if(n) sprintf(s,"%s (%d)",m,n);
	else sprintf(s,"%s",m);
	MessageBox(hwndFrame,s,"Warning",MB_OK|MB_ICONWARNING);
}

void wlsMessage(char *m,int n)
{
	char s[180];
	if(n) sprintf(s,"%s (%d)",m,n);
	else sprintf(s,"%s",m);
	MessageBox(hwndFrame,s,"Message",MB_OK|MB_ICONINFORMATION);
}

int wlsQuery(char *m,int n)
{
	char s[180];
	if(n) sprintf(s,"%s (%d)",m,n);
	else sprintf(s,"%s",m);
	if(MessageBox(hwndFrame,s,"Query",MB_OKCANCEL|MB_ICONQUESTION)
		==IDCANCEL)
		return 0;
	return 1;
}


HWND hwndStatus=NULL;

void wlsStatus(char *msg)
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
	wc.lpfnWndProc = (WNDPROC)WndProcFrame;
	wc.cbClsExtra = 0;
	wc.cbWndExtra = 0;
	wc.hInstance = hInstance;
	iconWLS = LoadIcon(hInstance,"ICONWLS");
	wc.hIcon = iconWLS;
//	wc.hIcon=NULL;
	wc.hCursor = LoadCursor(NULL, IDC_ARROW);
	wc.hbrBackground = GetStockObject(WHITE_BRUSH);
	wc.lpszMenuName =  "WLSMenu";
	wc.lpszClassName = "WLSCLASSFRAME";
	RegisterClass(&wc);

	wc.style = CS_OWNDC;
	wc.lpfnWndProc = (WNDPROC)WndProcMain;
	wc.cbClsExtra = 0;
	wc.cbWndExtra = 0;
	wc.hInstance = hInstance;
	wc.hIcon=NULL;
	wc.hCursor = LoadCursor(NULL, IDC_ARROW);
	wc.hbrBackground = GetStockObject(WHITE_BRUSH);
	wc.lpszMenuName =  NULL;
	wc.lpszClassName = "WLSCLASSMAIN";
	RegisterClass(&wc);
	
	wc.style = 0;
	wc.lpfnWndProc = (WNDPROC)WndProcToolbar;
	wc.cbClsExtra = 0;
	wc.cbWndExtra = 0;
	wc.hInstance = hInstance;
	wc.hIcon=NULL;
	wc.hCursor = LoadCursor(NULL, IDC_ARROW);
	wc.hbrBackground = GetStockObject(LTGRAY_BRUSH);
	wc.lpszMenuName =  NULL;
	wc.lpszClassName = "WLSCLASSTOOLBAR";
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
//	int duplist[7];
//	int i,j,used,moveto,dup;

	s=symmap[symmetry];
	n=0;

	if(s & 0x02) {
		assert(colmax==rowmax);
		pt[n].x=rowmax-1-y; // forward diag
		pt[n].y=colmax-1-x;
		n++;
	}
	if(s & 0x04) {
		assert(colmax==rowmax);
		pt[n].x=y;               // rotate90
		pt[n].y=colmax-1-x;
		n++;
	}
	if(s & 0x08) {
		pt[n].x=colmax-1-x;  // mirrorx
		pt[n].y=y;
		n++;
	}
	if(s & 0x10) {
		pt[n].x=colmax-1-x;  // rotate180
		pt[n].y=rowmax-1-y;
		n++;
	}
	if(s & 0x20) {
		assert(colmax==rowmax);
		pt[n].x=y;                // back diag
		pt[n].y=x;
/* special symmetry
		if(x>=y) {
			pt[n].x=y-1;         
			pt[n].y=x;
		}
		else {
			pt[n].x=y;                
			pt[n].y=x+1;
		}
*/
		n++;
	}
	if(s & 0x40) {
		assert(colmax==rowmax);
		pt[n].x=rowmax-1-y; // rotate270
		pt[n].y=x;
		n++;
	}
	if(s & 0x80) {
		pt[n].x=x;               // mirrory
		pt[n].y=rowmax-1-y;
		n++;
	}

	*num=n;

	return pt;
}

// A stupid function
void SetCenter(void)
{
	centerx= colmax/2;
	centery= rowmax/2;
	centerxodd= colmax%2;
	centeryodd= rowmax%2;
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

	symmetry=0;

	trans_rotate=0;  // 1=90 degrees, 2=180, 3=270;
	trans_flip=0;
	trans_x= 0;
	trans_y= 0;

	genmax=2;

	curgen=0;
	colmax=35;
	rowmax=15;
	parent=0;
	allobjects=0;
	nearcols=0;
	maxcount=0;
	userow=0;
	usecol=0;
	colcells=0;
	colwidth=0;
	followgens=FALSE;
	follow=FALSE;
	orderwide=FALSE;
	ordergens=FALSE;
	ordermiddle=FALSE;
	diagsort=0;
	knightsort=0;
	fastsym=1;
	viewfreq=100000;
	strcpy(outputfile,"output.txt");
	saveoutput=0;
	strcpy(dumpfile,"dump.txt");
	strcpy(rulestring,"B3/S23");

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

	if(colmax*cellwidth<=r.right && scrollpos.x!=0) { scrollpos.x=0; redraw=1; }
	if(rowmax*cellheight<=r.bottom && scrollpos.y!=0) { scrollpos.y=0; redraw=1; }

	si.cbSize=sizeof(SCROLLINFO);
	si.fMask=SIF_ALL;
	si.nMin=0;
	si.nMax=colmax*cellwidth;
	si.nPage=r.right;
	si.nPos=scrollpos.x;
	si.nTrackPos=0;
	SetScrollInfo(hwndMain,SB_HORZ,&si,TRUE);

	si.nMax=rowmax*cellheight;
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
		"WLSCLASSFRAME",
		"WinLifeSearch",
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
		"WLSCLASSMAIN",
		"WinLifeSearch - main window",
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
		"WLSCLASSTOOLBAR",
		"WinLifeSearch - toolbar",
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

	hAccTable=LoadAccelerators(hInst,"WLSACCEL");

	RegisterClasses(hInstance);

	msg_mswheel=RegisterWindowMessage("MSWHEEL_ROLLMSG");

	InitApp(hInstance,nCmdShow);


	while(GetMessage(&msg,NULL,0,0)){
		if (!TranslateAccelerator(hwndFrame, hAccTable, &msg)) {
			TranslateMessage(&msg);
			DispatchMessage(&msg);
		}
	}
	return (msg.wParam);		/* Returns the value from PostQuitMessage */
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
	if(symmetry==2 || symmetry==6 || symmetry==9) {	
		MoveToEx(hDC,0,centerpy,NULL);
		LineTo(hDC,colmax*cellwidth,centerpy);
	}

	// vertical line
	if(symmetry==1 || symmetry==6 || symmetry==9) {
		MoveToEx(hDC,centerpx,0,NULL);
		LineTo(hDC,centerpx,rowmax*cellheight);
	}

	// diag - forward
	if(symmetry==3 || symmetry==5 || symmetry>=7) {
		MoveToEx(hDC,0,rowmax*cellheight,NULL);
		LineTo(hDC,colmax*cellwidth,0);
	}

	// diag - backward
	if(symmetry==4 || symmetry>=7) {
		MoveToEx(hDC,0,0,NULL);
		LineTo(hDC,colmax*cellwidth,rowmax*cellheight);
	}
	if(symmetry==5 || symmetry==8) {
		MoveToEx(hDC,0,rowmax*cellheight,NULL);
		LineTo(hDC,0,(rowmax-2)*cellheight);
		MoveToEx(hDC,colmax*cellwidth,0,NULL);
		LineTo(hDC,colmax*cellwidth,2*cellheight);
	}
	if(symmetry==8) {
		MoveToEx(hDC,0,0,NULL);
		LineTo(hDC,2*cellwidth,0);
		MoveToEx(hDC,colmax*cellwidth,rowmax*cellheight,NULL);
		LineTo(hDC,(colmax-2)*cellwidth,rowmax*cellheight);
	}
		
		
	if(trans_rotate || trans_flip || trans_x || trans_y) {
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
		if(trans_flip) {
			px3= -px3;
		}

		// rotate if necessary
		// Note: can't rotate by 90 or 270 degrees if centerxodd != centeryodd
		if(centerxodd != centeryodd)
			assert(trans_rotate==0 || trans_rotate==2);

		switch(trans_rotate) {
		case 1:
			px1=cellwidth*2;
			py1=0;
			if(trans_flip)
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
			if(trans_flip)
				py3= -py3;
			else
				px3= -px3;
			break;
		}

		// translate if necessary
		px1+=trans_x*cellwidth;
		px2+=trans_x*cellwidth;
		px3+=trans_x*cellwidth;
		py1+=trans_y*cellheight;
		py2+=trans_y*cellheight;
		py3+=trans_y*cellheight;
		
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
	for(i=1;i<genmax;i++) {
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
	case 3:       // unknown
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

//		MoveToEx(hDC,x*cellwidth+cellwidth/3,  y*cellheight+cellheight/3,NULL);
//		LineTo(hDC,  x*cellwidth+2*cellwidth/3,y*cellheight+cellheight/3);
		break;

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
			currfield[j][x][y]=currfield[curgen][x][y];
	}
	DrawCell(hDC,x,y);

	for(i=0;i<numpts;i++) {
		currfield[curgen][pts[i].x][pts[i].y]=currfield[curgen][x][y];
		if(allgens) {
			for(j=0;j<GENMAX;j++)
				currfield[j][pts[i].x][pts[i].y]=currfield[curgen][x][y];
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
		r.top=   endcell.y*cellheight;
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
	for(i=0;i<colmax;i++) {
		for(j=0;j<rowmax;j++) {
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

	if(x<0 || x>=colmax) return 1;
	if(y<0 || y>=rowmax) return 1;


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

		if(vkey=='C' || vkey=='X' || vkey=='A' || vkey=='S' || vkey=='F')
			allgens=1;

		if(vkey=='C' || vkey=='c') {
			//currfield[curgen][x][y]=2;
			newval=2;
		}
		else if(vkey=='X' || vkey=='x') {
			//currfield[curgen][x][y]=3;
			newval=3;
		}
		else if(vkey=='A' || vkey=='a') {
			//currfield[curgen][x][y]=0;
			newval=0;
		}
		else if(vkey=='S' || vkey=='s') {
			newval=1;
			//currfield[curgen][x][y]=1;
		}
		else if(vkey=='F' || vkey=='f') {
			//currfield[curgen][x][y]=4;
			newval=4;
			allgens=1;
		}
		else {
			return 1;
		}

//				if(currfield[curgen][x][y]==3) currfield[curgen][x][y]=2;
//				else currfield[curgen][x][y]=3;
		break;

	default:
		return 1;
	}

	FixFrozenCells();


	hDC=GetDC(hwndMain);

	tmp=selectstate;

	SelectOff(hDC);

	if(tmp==2) {
		if(newval>=0) {
			for(i=selectrect.left;i<=selectrect.right;i++) {
				for(j=selectrect.top;j<=selectrect.bottom;j++) {
					currfield[curgen][i][j]=newval;
					Symmetricalize(hDC,i,j,allgens);
				}
			}
		}

//		else {  // remove this??
//			for(i=selectrect.left;i<=selectrect.right;i++) {
//				for(j=selectrect.top;j<=selectrect.bottom;j++) {
//					currfield[curgen][i][j]=currfield[curgen][x][y];
//					Symmetricalize(hDC,i,j,allgens);
//				}
//			}
//		}
	}
	else {
		if(newval>=0) currfield[curgen][x][y]=newval;
		Symmetricalize(hDC,x,y,allgens);
	}

//	DrawCell(hDC,x,y);
	// If Ctrl key was held down,
	// create a "frozen" cell (same in all generations) // WRONG

	

	DrawGuides(hDC);

	ReleaseDC(hwndMain,hDC);
	return 0;
}

// copy my format to dbells format...
int set_initial_cells(void)
{
	char buf[80];
	int i,j,g;

	for(g=0;g<genmax;g++) 
		for(i=0;i<colmax;i++)
			for(j=0;j<rowmax;j++) {
				switch(origfield[g][i][j]) {
				case 0:  // forced off
					if(proceed(findcell(j+1,i+1,g),OFF,FALSE)!=OK) {
						sprintf(buf,"Inconsistent OFF state for cell (col %d,row %d,gen %d)",i+1,j+1,g);
						wlsMessage(buf,0);
						return 0;
					}
					break;
				case 1:  // forced on
					if(proceed(findcell(j+1,i+1,g),ON,FALSE)!=OK) {
						sprintf(buf,"Inconsistent ON state for cell (col %d,row %d,gen %d)",i+1,j+1,g);
						wlsMessage(buf,0);
						return 0;
					}
					break;
				case 3:  // eXcluded cells
					excludecone(j+1,i+1,g);
					break;
				case 4: // frozen cells
					freezecell(j+1, i+1);
					break;
				}

			}

	return 1;

}

void draw_gen_counter(void)
{
	char buf[80];
	SCROLLINFO si;

	si.cbSize=sizeof(SCROLLINFO);
	si.fMask=SIF_ALL;
	si.nMin=0;
	si.nMax=genmax-1;
	si.nPage=1;
	si.nPos=curgen;
	si.nTrackPos=0;

	SetScrollInfo(hwndGenScroll,SB_CTL,&si,TRUE);

	sprintf(buf,"%d",curgen);
	SetWindowText(hwndGen,buf);
}

void showcount(int c)
{
	static char buf[40];
	static int tot=0;

	if(c<0) tot=0;
	else tot+=c;

	sprintf(buf,"WinLifeSearch [%d]",tot);
	SetWindowText(hwndFrame,buf);
}



void printgen(int gen)
{
	int i,j,g;
	CELL *cell;

	curgen=gen;

	// copy dbell's format back into mine
	for(g=0;g<genmax;g++) 
		for(i=0;i<colmax;i++)
			for(j=0;j<rowmax;j++) {
				cell=findcell(j+1,i+1,g);
				switch(cell->state) {
				case OFF:
					currfield[g][i][j]=0;
					break;
				case ON:
					currfield[g][i][j]=1;
					break;
				case UNK:
					currfield[g][i][j]=2;
					break;
				}
			}

	InvalidateRect(hwndMain,NULL,TRUE);
}


void pause_search(void);  // forward decl

DWORD WINAPI search_thread(LPVOID foo)
{
//	int i,j,k;
	char buf[180];

	/*
	 * Initial commands are complete, now look for the object.
	 */
	while (TRUE)
	{
		if (curstatus == OK)
			curstatus = search();

		if(abortthread) goto done;

		if ((curstatus == FOUND) && userow && (rowinfo[userow].oncount == 0)) {
			curstatus = OK;
			continue;
		}

		if ((curstatus == FOUND) && !allobjects && subperiods()) {
			curstatus = OK;
			continue;
		}

		if (dumpfreq) {
			dumpcount = 0;
			dumpstate(dumpfile);
		}

		quitok = (curstatus == NOTEXIST);

		curgen = 0;

//		if (outputfile == NULL) {
//			getcommands();
//			continue;
//		}

		/*
		 * Here if results are going to a file.
		 */
		if (curstatus == FOUND) {
			curstatus = OK;

			if (!quiet) {
				printgen(0);
//				ttystatus("Object %ld found.\n", ++foundcount);
				sprintf(buf,"Object %ld found.\n", ++foundcount);
				wlsStatus(buf);
			}

			writegen(outputfile, TRUE);
			goto done;
//			pause_search();
//			continue;
		}

		if (foundcount == 0) {
			ttyclose();
			wlsWarning( "No objects found",0);
			goto done;
		}

		ttyclose();

		if (!quiet) {
//			sprintf(buf,"Search completed, file \"%s\" contains %ld object%s\n",
//				outputfile, foundcount, (foundcount == 1) ? "" : "s");
			sprintf(buf,"Search completed:  %ld object%s found",
				foundcount, (foundcount == 1) ? "" : "s");
			wlsMessage(buf,0);
		}

		goto done;
	}
done:
	searchstate=1;
	//ExitThread(0);
	_endthreadex(0);
	return 0;
}


void resume_search(void)
{
	DWORD threadid;

	SetWindowText(hwndFrame,"WinLifeSearch");
	wlsStatus("Searching...");

	if(searchstate!=1) {
		wlsError("No search is paused",0);
		return;
	}
	abortthread=0;

	searchstate=2;
	//hthread=CreateThread(NULL,0,search_thread,(LPVOID)0,0,&threadid);
	hthread=(HANDLE)_beginthreadex(NULL,0,search_thread,(void*)0,0,&threadid);


	if(hthread==NULL) {
		wlsError("Unable to create search thread",0);
		searchstate=1;
	}
	else {
		SetWindowText(hwndFrame,"WinLifeSearch");
		SetThreadPriority(hthread,THREAD_PRIORITY_BELOW_NORMAL);
	}

}

void start_search(char *statefile)
{
	int i,j,k;
//	CELL *cell;

	if(searchstate!=0) {
		wlsError("A search is already running",0);
		return;
	}

	showcount(-1);

	// save a copy or the starting position
	for(k=0;k<GENMAX;k++) 
		for(i=0;i<COLMAX;i++)
			for(j=0;j<ROWMAX;j++)
				origfield[k][i][j]=currfield[k][i][j];

	if (!setrules(rulestring))
	{
		wlsError("Cannot set Life rules!",0);
		return; //exit(1);
	}

	// set the variables that dbell's code uses
	coltrans= -trans_x;
	rowtrans= -trans_y;
	flipquads= trans_rotate%2;
	fliprows= (trans_rotate>=2);
	flipcols= (trans_flip==0 && trans_rotate>=2) ||
		      (trans_flip==1 && trans_rotate<2);

	// init some things that the orig code doesn't bother to init
	for(i=0;i<COLMAX;i++) {
		colinfo[i].oncount=0;
		colinfo[i].setcount=0;
		colinfo[i].sumpos=0;
	}
	for(i=0;i<ROWMAX;i++) rowinfo[i].oncount=0;
	newset=NULL;
	nextset=NULL;
	baseset=NULL;
	fullsearchlist=NULL;
	outputlastcols=0;
	fullcolumns=0;
	curstatus=OK;

	if(statefile) {
		if(loadstate(statefile) == ERROR1) return;

		printgen(curgen);
		draw_gen_counter();
		searchstate=1;
/*
		// set up the starting position
		for(k=0;k<genmax;k++) 
			for(i=0;i<colmax;i++)
				for(j=0;j<rowmax;j++) {
//					origfield[k][i][j]=currfield[k][i][j];
					cell=findcell(j+1,i+1,k);
					if(cell->free) origfield[k][i][j]=2;
					else if(cell->state==OFF) origfield[k][i][j]=0;
					else origfield[k][i][j]=1;
				}
*/


	}
	else {
		inited=FALSE;

		initcells();
		baseset = nextset;

		inited = TRUE;


		if(!set_initial_cells()) {
			return;   // there was probably an inconsistency in the initial cells
		}

		foundcount=0;
		searchstate=1;  // pretend the search is "paused"
		resume_search();
	}
}


void pause_search(void)
{
	DWORD exitcode;

	if(searchstate!=2) {
		wlsError("No search is running",0);
		return;
	}
///	threadstate=1;
//	SuspendThread(hthread);
//	wlsMessage("Paused",0);
	abortthread=1;
	do {
		GetExitCodeThread(hthread, &exitcode);
		if(exitcode==STILL_ACTIVE) {
			Sleep(200);
		}
	} while(exitcode==STILL_ACTIVE);
	CloseHandle(hthread);
	searchstate=1;

	SetWindowText(hwndFrame,"WinLifeSearch - Paused");

}


void reset_search(void)
{
//	HDC hDC;
	int i,j,k;

	if(searchstate==0) {
		wlsError("No search in progress",0);
		goto here;
	}

	// stop the search thread if it is running
//	abortthread=1;
	if(searchstate==2) {
		pause_search();
/*		do {
			GetExitCodeThread(hthread, &exitcode);
			if(exitcode==STILL_ACTIVE) {
				Sleep(200);
			}
		} while(exitcode==STILL_ACTIVE);
		*/
	}


	searchstate=0;

	record_malloc(0,NULL);    // free memory

	// restore the original cells
	for(k=0;k<GENMAX;k++) 
		for(i=0;i<COLMAX;i++)
			for(j=0;j<ROWMAX;j++)
				currfield[k][i][j]=origfield[k][i][j];

here:
	InvalidateRect(hwndMain,NULL,TRUE);

	SetWindowText(hwndFrame,"WinLifeSearch");
	wlsStatus("");

//	wlsMessage("Stopped",0);
}

void open_state(void)
{
	// get filename
	// ...

	if(searchstate!=0) reset_search();

	start_search("");

	//InvalidateRect(hwndMain,NULL,TRUE);
}


void gen_changeby(int delta)
{
	if(genmax<2) return;
	curgen+=delta;
	if(curgen>=genmax) curgen=0;
	if(curgen<0) curgen=genmax-1;

	draw_gen_counter();
	InvalidateRect(hwndMain,NULL,TRUE);
}


void clear_gen(int g)
{	int i,j;
	for(i=0;i<COLMAX;i++)
		for(j=0;j<ROWMAX;j++)
			currfield[g][i][j]=2;
}

void clear_all(void)
{	int g;
	for(g=0;g<GENMAX;g++)
		clear_gen(g);
}

void copytoclipboard(void)
{
	DWORD size;
	HGLOBAL hClip;
	LPVOID lpClip;
	char buf[100],buf2[10];
	char *s;
	int i,j;
	int offset;

	if(searchstate==0) {
		// bornrules/liverules may not be set up yet
		// probably we could call setrules().
		sprintf(buf,"#P 0 0\r\n");
	}
	else {
		// unfortunately the rulestring is in the wrong format for life32
		sprintf(buf,"#P 0 0\r\n#R S");
		for(i=0;i<=8;i++) {
			if(liverules[i]) {sprintf(buf2,"%d",i); strcat(buf,buf2);}
		}
		strcat(buf,"/B");
		for(i=0;i<=8;i++) {
			if(bornrules[i]) {sprintf(buf2,"%d",i); strcat(buf,buf2);}
		}
		strcat(buf,"\r\n");
	}

	offset=strlen(buf);

	size=offset+(colmax+2)*rowmax+1;
	hClip=GlobalAlloc(GMEM_MOVEABLE|GMEM_DDESHARE,size);

	lpClip=GlobalLock(hClip);
	s=(char*)lpClip;

	strcpy(s,buf);

	for(j=0;j<rowmax;j++) {
		for(i=0;i<colmax;i++) {
			if(currfield[curgen][i][j]==1)
				s[offset+(colmax+2)*j+i]='*';
			else
				s[offset+(colmax+2)*j+i]='.';
		}
		s[offset+(colmax+2)*j+colmax]='\r';
		s[offset+(colmax+2)*j+colmax+1]='\n';
	}
	s[offset+(colmax+2)*rowmax]='\0';

	OpenClipboard(NULL);
	EmptyClipboard();
	SetClipboardData(CF_TEXT,hClip);
	CloseClipboard();
}

/****************************************************************************/
LRESULT CALLBACK WndProcFrame(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
	WORD id;
	POINT pt;
	int rv;

	id=LOWORD(wParam);

	if(msg==msg_mswheel) {
		if(wParam & (unsigned)0x80000000)
			gen_changeby(1);
		else
			gen_changeby(-1);
		return 0;
	}

	switch(msg) {

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
		EnableMenuItem((HMENU)wParam,IDC_SEARCHRESET,searchstate!=0?MF_ENABLED:MF_GRAYED);
		EnableMenuItem((HMENU)wParam,IDC_SEARCHPAUSE,searchstate==2?MF_ENABLED:MF_GRAYED);
		EnableMenuItem((HMENU)wParam,IDC_SEARCHRESUME,searchstate==1?MF_ENABLED:MF_GRAYED);
		EnableMenuItem((HMENU)wParam,IDC_SEARCHBACKUP,searchstate==1?MF_ENABLED:MF_GRAYED);
		EnableMenuItem((HMENU)wParam,IDC_SEARCHBACKUP2,searchstate==1?MF_ENABLED:MF_GRAYED);
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
		case IDC_TEST:
			rv=DialogBox(hInst,"DLGTABS",hWnd,DlgProcTest);
			return 0;
		case IDC_ABOUT:
			DialogBox(hInst,"DLGABOUT",hWnd,DlgProcAbout);
			return 0;
		case IDC_ROWS:
			DialogBox(hInst,"DLGROWS",hWnd,DlgProcRows);
			return 0;
		case IDC_PERIOD:
			DialogBox(hInst,"DLGPERIOD",hWnd,DlgProcPeriod);
			return 0;
		case IDC_SYMMETRY:
			DialogBox(hInst,"DLGSYMMETRY",hWnd,DlgProcSymmetry);
			return 0;
		case IDC_TRANSLATE:
			DialogBox(hInst,"DLGTRANSLATE",hWnd,DlgProcTranslate);
			return 0;
		case IDC_OUTPUTSETTINGS:
			DialogBox(hInst,"DLGSETTINGS",hWnd,DlgProcOutput);
			return 0;
		case IDC_SEARCHSETTINGS:
			DialogBox(hInst,"DLGSEARCHSETTINGS",hWnd,DlgProcSearch);
			return 0;
		case IDC_SEARCHSTART:
			start_search(NULL);
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
			if(searchstate==1)
				getbackup("1 ");
			return 0;
		case IDC_SEARCHBACKUP2:
			if(searchstate==1)
				getbackup("20 ");
			return 0;
		case IDC_OPENSTATE:
			open_state();
			return 0;
		case IDC_SAVEGAME:
			if(searchstate==1)
				dumpstate(NULL);
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
			clear_gen(curgen);
			InvalidateRect(hwndMain,NULL,TRUE);
			return 0;
		case IDC_CLEAR:
			clear_all();
			InvalidateRect(hwndMain,NULL,TRUE);
			return 0;

		}
		break;
	}

	return (DefWindowProc(hWnd, msg, wParam, lParam));
}



LRESULT CALLBACK WndProcMain(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
	WORD id;
	id=LOWORD(wParam);

	switch(msg) {
//	case WM_CREATE:
//		set_main_scrollbars(1);
//		return 0;
	case WM_PAINT:
		PaintWindow(hWnd);
		return 0;
	case WM_HSCROLL:
		switch(id) {
		case SB_LINELEFT: scrollpos.x-=cellwidth; break;
		case SB_LINERIGHT: scrollpos.x+=cellwidth; break;
		case SB_PAGELEFT: scrollpos.x-=cellwidth*4; break;
		case SB_PAGERIGHT: scrollpos.x+=cellwidth*4; break;
		//case SB_THUMBPOSITION: scrollpos.x=HIWORD(wParam); break;
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
		//case SB_THUMBPOSITION: scrollpos.y=HIWORD(wParam); break;
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
//	case WM_KEYDOWN:
//		ButtonClick(msg,LOWORD(lParam),HIWORD(lParam),wParam);
//		return 0;
//
	}

	return (DefWindowProc(hWnd, msg, wParam, lParam));
}






LRESULT CALLBACK WndProcToolbar(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
	HWND htmp;
	WORD id; //,code;

	int ori_gen;
//	char buf[80];


	id=LOWORD(wParam);

	switch(msg) {
	case WM_HSCROLL:
		htmp=(HWND)(lParam);
		if(htmp==hwndGenScroll) {
//			code=LOWORD(wParam);
			ori_gen=curgen;
			switch(id) {
			case SB_LINELEFT: curgen--; break;
			case SB_LINERIGHT: curgen++; break;
			case SB_PAGELEFT: curgen--; break;
			case SB_PAGERIGHT: curgen++; break;
			case SB_LEFT: curgen=0; break;
			case SB_RIGHT: curgen=genmax-1; break;
			case SB_THUMBPOSITION: curgen=HIWORD(wParam); break;
			case SB_THUMBTRACK: curgen=HIWORD(wParam); break;
			}
			if(curgen<0) curgen=genmax-1;  // wrap around
			if(curgen>=genmax) curgen=0;
			if(curgen!=ori_gen) {
				draw_gen_counter();
				InvalidateRect(hwndMain,NULL,TRUE);
			}
			return 0;
		}
		break;


	case WM_CREATE:
		hwndGen=CreateWindow("Static","0",
			WS_CHILD|WS_VISIBLE|WS_BORDER,
			1,1,40,TOOLBARHEIGHT-2,
			hWnd,NULL,hInst,NULL);
		hwndGenScroll=CreateWindow("Scrollbar","wls_gen_scrollbar",
			WS_CHILD|WS_VISIBLE|SBS_HORZ,
			41,1,80,TOOLBARHEIGHT-2,
			hWnd,NULL,hInst,NULL);
		hwndStatus=CreateWindow("Static","",
			WS_CHILD|WS_VISIBLE|WS_BORDER,
			120,1,800,TOOLBARHEIGHT-2,
			hWnd,NULL,hInst,NULL);
#ifdef _DEBUG
			wlsStatus("DEBUG BUILD");
#endif

			draw_gen_counter();
		return 0;

	}

	return (DefWindowProc(hWnd, msg, wParam, lParam));
}

BOOL CALLBACK DlgProcAbout(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
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

BOOL CALLBACK DlgProcRows(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
	WORD id;
	id=LOWORD(wParam);

	switch (msg) {
	case WM_INITDIALOG:
		SetDlgItemInt(hWnd,IDC_COLUMNS,colmax,FALSE);
		SetDlgItemInt(hWnd,IDC_ROWS,rowmax,FALSE);
		return 1;   // didn't call SetFocus

	case WM_COMMAND:
		switch(id) {
		case IDOK:
			colmax=GetDlgItemInt(hWnd,IDC_COLUMNS,NULL,FALSE);
			rowmax=GetDlgItemInt(hWnd,IDC_ROWS,NULL,FALSE);
			if(colmax<1) colmax=1;
			if(rowmax<1) rowmax=1;
			if(colmax>COLMAX) colmax=COLMAX;
			if(rowmax>ROWMAX) rowmax=ROWMAX;
			SetCenter();

			// put these in a separate Validate function
			if(colmax!=rowmax) {
				if(symmap[symmetry] & 0x66) {
					MessageBox(hWnd,"Current symmetry requires that rows and "
						"columns be equal. Your symmetry setting has been altered.",
						"Warning",MB_OK|MB_ICONWARNING);
					switch(symmetry) {
					case 3: case 4: symmetry=0; break;
					case 7: case 8: symmetry=5; break;
					case 9: symmetry=6;
					}
				}
			}

			if(colmax != rowmax) {
				if(trans_rotate==1 || trans_rotate==3) {
					MessageBox(hWnd,"Current rotation setting requires that rows and "
						"columns be equal. Your translation setting has been altered.",
						"Warning",MB_OK|MB_ICONWARNING);
					trans_rotate--;
				}
			}

			//InvalidateRect(hwndMain,NULL,TRUE);
			set_main_scrollbars(1);
			// fall through
		case IDCANCEL:
			EndDialog(hWnd, TRUE);
			return 1;
		}
	}
	return 0;		// Didn't process a message
}


BOOL CALLBACK DlgProcPeriod(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
	WORD id;
	char buf[80];

	id=LOWORD(wParam);

	switch (msg) {
	case WM_INITDIALOG:
		sprintf(buf,"Enter a period from 1 to %d",GENMAX);
		SetDlgItemText(hWnd,IDC_PERIODTEXT,buf);
		SetDlgItemInt(hWnd,IDC_PERIOD1,genmax,FALSE);
		return 1;

	case WM_COMMAND:
		switch(id) {
		case IDOK:
			genmax=GetDlgItemInt(hWnd,IDC_PERIOD1,NULL,FALSE);
			if(genmax>GENMAX) genmax=GENMAX;
			if(genmax<1) genmax=1;
			if(curgen>=genmax) curgen=genmax-1;

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

BOOL CALLBACK DlgProcOutput(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
	WORD id;
//	char buf[80];

	id=LOWORD(wParam);

	switch (msg) {
	case WM_INITDIALOG:
		SetDlgItemInt(hWnd,IDC_VIEWFREQ,viewfreq,FALSE);
		SetDlgItemText(hWnd,IDC_DUMPFILE,dumpfile);
		SetDlgItemInt(hWnd,IDC_DUMPFREQ,dumpfreq,FALSE);
		CheckDlgButton(hWnd,IDC_OUTPUTFILEYN,saveoutput?BST_CHECKED:BST_UNCHECKED);
		SetDlgItemText(hWnd,IDC_OUTPUTFILE,outputfile);
		SetDlgItemInt(hWnd,IDC_OUTPUTCOLS,outputcols,FALSE);
//		EnableWindow(GetDlgItem(hWnd,IDC_VIEWFREQ),FALSE);

//		sprintf(buf,"Enter a period from 1 to %d",GENMAX);
//		SetDlgItemText(hWnd,IDC_PERIODTEXT,buf);
//		SetDlgItemInt(hWnd,IDC_PERIOD1,genmax,FALSE);
		return 1;

	case WM_COMMAND:
		switch(id) {
		case IDOK:
			viewfreq=GetDlgItemInt(hWnd,IDC_VIEWFREQ,NULL,FALSE);
			dumpfreq=GetDlgItemInt(hWnd,IDC_DUMPFREQ,NULL,FALSE);
			GetDlgItemText(hWnd,IDC_DUMPFILE,dumpfile,79);
			saveoutput=(IsDlgButtonChecked(hWnd,IDC_OUTPUTFILEYN)==BST_CHECKED);
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

BOOL CALLBACK DlgProcSearch(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
	WORD id;
//	char buf[80];

	id=LOWORD(wParam);

	switch (msg) {
	case WM_INITDIALOG:
		CheckDlgButton(hWnd,IDC_ORDERWIDE,orderwide);
		CheckDlgButton(hWnd,IDC_ORDERGENS,ordergens);
		CheckDlgButton(hWnd,IDC_ORDERMIDDLE,ordermiddle);
		CheckDlgButton(hWnd,IDC_DIAGSORT,diagsort);
		CheckDlgButton(hWnd,IDC_KNIGHTSORT,knightsort);
		CheckDlgButton(hWnd,IDC_FASTSYM,fastsym);
		CheckDlgButton(hWnd,IDC_ALLOBJECTS,allobjects);
		CheckDlgButton(hWnd,IDC_PARENT,parent);
		CheckDlgButton(hWnd,IDC_FOLLOW,follow);
		CheckDlgButton(hWnd,IDC_FOLLOWGENS,followgens);
		SetDlgItemInt(hWnd,IDC_NEARCOLS,nearcols,TRUE);
		SetDlgItemInt(hWnd,IDC_USECOL,usecol,TRUE);
		SetDlgItemInt(hWnd,IDC_USEROW,userow,TRUE);

		SetDlgItemInt(hWnd,IDC_MAXCOUNT,maxcount,TRUE);
		SetDlgItemInt(hWnd,IDC_COLCELLS,colcells,TRUE);
		SetDlgItemInt(hWnd,IDC_COLWIDTH,colwidth,TRUE);

		SetDlgItemText(hWnd,IDC_RULESTRING,rulestring);
		return 1;

	case WM_COMMAND:
		switch(id) {
		case IDOK:
			//GetDlgItemText(hWnd,IDC_DUMPFILE,buf,79);
			orderwide=  IsDlgButtonChecked(hWnd,IDC_ORDERWIDE  )?1:0;
			ordergens=  IsDlgButtonChecked(hWnd,IDC_ORDERGENS  )?1:0;
			ordermiddle=IsDlgButtonChecked(hWnd,IDC_ORDERMIDDLE)?1:0;
			diagsort=   IsDlgButtonChecked(hWnd,IDC_DIAGSORT   )?1:0;
			knightsort= IsDlgButtonChecked(hWnd,IDC_KNIGHTSORT )?1:0;
			fastsym=    IsDlgButtonChecked(hWnd,IDC_FASTSYM )?1:0;
			allobjects= IsDlgButtonChecked(hWnd,IDC_ALLOBJECTS )?1:0;
			parent=     IsDlgButtonChecked(hWnd,IDC_PARENT     )?1:0;
			follow=     IsDlgButtonChecked(hWnd,IDC_FOLLOW     )?1:0;
			followgens= IsDlgButtonChecked(hWnd,IDC_FOLLOWGENS )?1:0;
			nearcols=GetDlgItemInt(hWnd,IDC_NEARCOLS,NULL,TRUE);
			usecol=GetDlgItemInt(hWnd,IDC_USECOL,NULL,TRUE);
			userow=GetDlgItemInt(hWnd,IDC_USEROW,NULL,TRUE);
			maxcount=GetDlgItemInt(hWnd,IDC_MAXCOUNT,NULL,TRUE);
			colcells=GetDlgItemInt(hWnd,IDC_COLCELLS,NULL,TRUE);
			colwidth=GetDlgItemInt(hWnd,IDC_COLWIDTH,NULL,TRUE);
			GetDlgItemText(hWnd,IDC_RULESTRING,rulestring,50);

			// fall through
		case IDCANCEL:
			EndDialog(hWnd, TRUE);
			return 1;
		case IDC_CONWAY:
			SetDlgItemText(hWnd,IDC_RULESTRING,"B3/S23");
			return 1;
		}
	}
	return 0;		// Didn't process a message
}

BOOL CALLBACK DlgProcSymmetry(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
	WORD id;
	int item;

	id=LOWORD(wParam);

	switch (msg) {
	case WM_INITDIALOG:
		if(colmax!=rowmax) {
			EnableWindow(GetDlgItem(hWnd,IDC_SYM3),FALSE);
			EnableWindow(GetDlgItem(hWnd,IDC_SYM4),FALSE);
			EnableWindow(GetDlgItem(hWnd,IDC_SYM7),FALSE);
			EnableWindow(GetDlgItem(hWnd,IDC_SYM8),FALSE);
			EnableWindow(GetDlgItem(hWnd,IDC_SYM9),FALSE);
		}
		switch(symmetry) {
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
			if(IsDlgButtonChecked(hWnd,IDC_SYM0)==BST_CHECKED) symmetry=0;
			if(IsDlgButtonChecked(hWnd,IDC_SYM1)==BST_CHECKED) symmetry=1;
			if(IsDlgButtonChecked(hWnd,IDC_SYM2)==BST_CHECKED) symmetry=2;
			if(IsDlgButtonChecked(hWnd,IDC_SYM3)==BST_CHECKED) symmetry=3;
			if(IsDlgButtonChecked(hWnd,IDC_SYM4)==BST_CHECKED) symmetry=4;
			if(IsDlgButtonChecked(hWnd,IDC_SYM5)==BST_CHECKED) symmetry=5;
			if(IsDlgButtonChecked(hWnd,IDC_SYM6)==BST_CHECKED) symmetry=6;
			if(IsDlgButtonChecked(hWnd,IDC_SYM7)==BST_CHECKED) symmetry=7;
			if(IsDlgButtonChecked(hWnd,IDC_SYM8)==BST_CHECKED) symmetry=8;
			if(IsDlgButtonChecked(hWnd,IDC_SYM9)==BST_CHECKED) symmetry=9;

			InvalidateRect(hwndMain,NULL,TRUE);
			// fall through
		case IDCANCEL:
			EndDialog(hWnd, TRUE);
			return 1;
		}
		break;
	}
	return 0;		// Didn't process a message
}


BOOL CALLBACK DlgProcTranslate(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
	WORD id;
	int item;

	id=LOWORD(wParam);

	switch (msg) {
	case WM_INITDIALOG:
		SetDlgItemInt(hWnd,IDC_TRANSX,trans_x,TRUE);
		SetDlgItemInt(hWnd,IDC_TRANSY,trans_y,TRUE);

		if(colmax != rowmax) {
			EnableWindow(GetDlgItem(hWnd,IDC_TRANS1),FALSE);
			EnableWindow(GetDlgItem(hWnd,IDC_TRANS3),FALSE);
			EnableWindow(GetDlgItem(hWnd,IDC_TRANS5),FALSE);
			EnableWindow(GetDlgItem(hWnd,IDC_TRANS7),FALSE);
		}

		switch(trans_rotate + 4*trans_flip) {
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
			trans_x=GetDlgItemInt(hWnd,IDC_TRANSX,NULL,TRUE);
			trans_y=GetDlgItemInt(hWnd,IDC_TRANSY,NULL,TRUE);
			if(trans_x>TRANSMAX) trans_x=TRANSMAX;
			if(trans_y>TRANSMAX) trans_y=TRANSMAX;
			if(trans_x< -TRANSMAX) trans_x= -TRANSMAX;
			if(trans_y< -TRANSMAX) trans_y= -TRANSMAX;

			if(IsDlgButtonChecked(hWnd,IDC_TRANS0)==BST_CHECKED) { trans_flip=0; trans_rotate=0; }
			if(IsDlgButtonChecked(hWnd,IDC_TRANS1)==BST_CHECKED) { trans_flip=0; trans_rotate=1; }
			if(IsDlgButtonChecked(hWnd,IDC_TRANS2)==BST_CHECKED) { trans_flip=0; trans_rotate=2; }
			if(IsDlgButtonChecked(hWnd,IDC_TRANS3)==BST_CHECKED) { trans_flip=0; trans_rotate=3; }
			if(IsDlgButtonChecked(hWnd,IDC_TRANS4)==BST_CHECKED) { trans_flip=1; trans_rotate=0; }
			if(IsDlgButtonChecked(hWnd,IDC_TRANS5)==BST_CHECKED) { trans_flip=1; trans_rotate=1; }
			if(IsDlgButtonChecked(hWnd,IDC_TRANS6)==BST_CHECKED) { trans_flip=1; trans_rotate=2; }
			if(IsDlgButtonChecked(hWnd,IDC_TRANS7)==BST_CHECKED) { trans_flip=1; trans_rotate=3; }
			InvalidateRect(hwndMain,NULL,TRUE);
			// fall through
		case IDCANCEL:
			EndDialog(hWnd, TRUE);
			return 1;
		}
		break;
	}
	return 0;		// Didn't process a message
}




BOOL CALLBACK DlgProcTest(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
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
	return 0;
}

