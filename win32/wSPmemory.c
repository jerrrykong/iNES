#include "stdafx.h"
#include "../comm/log.h"
#include "../core/nes.h"
#include "wSPMemory.h"
#include "Resource.h"


extern ines_host_t   host;

static HWND wSPMemory_hWnd = NULL;
static BOOL wSPMemory_bShow = FALSE;
static const TCHAR wSPMemory_szClassName[128] = _T("iNES_SPMemoryWnd");
static TCHAR wSPMemory_szTitle[MAX_LOADSTRING];

// draw context
static HFONT    wSPMemory_hFont = NULL;
static int wSPMemory_iCharWidth = 0;
static int wSPMemory_iCharHeight = 0;
//static HBITMAP wSPMemory_hBmpChar = NULL;
static int wSPMemory_iStartLine = 0;
static int wSPMemory_iStartCol = 0;
static int wSPMemory_iUpdate = 0;
static int wSPMemory_iCurAddr = 0;
static int wSPMemory_iHalfChar = 0;

#define SPMEMORY_BYTES         0x100
#define LINE_SPLIT_HIGH        3
#define BYTES_PER_LINE         16
#define HEAD_LINES             1
#define ADDR_COLS              6

static LRESULT CALLBACK	wSPMemory_WindowProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam);

static ATOM wSPMemory_RegisterClass(HINSTANCE  hInstance)
{
	WNDCLASSEX wcex;

	wcex.cbSize = sizeof(WNDCLASSEX);

	wcex.style			= CS_HREDRAW | CS_VREDRAW | CS_OWNDC;
	wcex.lpfnWndProc	= wSPMemory_WindowProc;
	wcex.cbClsExtra		= 0;
	wcex.cbWndExtra		= 0;
	wcex.hInstance		= hInstance;
	wcex.hIcon			= NULL; // LoadIcon(hInstance, MAKEINTRESOURCE(IDI_INES));
	wcex.hCursor		= LoadCursor(NULL, IDC_ARROW);
	wcex.hbrBackground	= /* (HBRUSH)(COLOR_WINDOW+1)*/ (HBRUSH)GetStockObject(NULL_BRUSH);
	wcex.lpszMenuName	= NULL; // MAKEINTRESOURCE(IDC_INES);
	wcex.lpszClassName	= wSPMemory_szClassName;
	wcex.hIconSm		= NULL; // LoadIcon(wcex.hInstance, MAKEINTRESOURCE(IDI_SMALL));

	return RegisterClassEx(&wcex);
}



BOOL wSPMemory_Create(HINSTANCE hInstance, HWND hParentWnd)
{
	// already created
	if(wSPMemory_hWnd != NULL)
	{
		/// SetParent(wSPMemory_hWnd, hParentWnd);
		return TRUE;
	}

	LoadString(hInstance, IDS_WND_SPMEMORY_TITLE, wSPMemory_szTitle, count_of(wSPMemory_szTitle));

	wSPMemory_RegisterClass(hInstance);
	 
	wSPMemory_hWnd = CreateWindowEx(0&WS_EX_TOOLWINDOW, wSPMemory_szClassName, wSPMemory_szTitle, 
		WS_POPUPWINDOW|WS_CAPTION|WS_OVERLAPPED|WS_SIZEBOX|WS_MINIMIZEBOX|WS_HSCROLL|WS_VSCROLL,
		CW_USEDEFAULT, CW_USEDEFAULT, 512, 512, NULL, NULL, hInstance, NULL);



	return TRUE;
}


VOID wSPMemory_Show(BOOL bShow)
{
	if(wSPMemory_hWnd != NULL)
	{
		ShowWindow(wSPMemory_hWnd, bShow ? SW_SHOWNORMAL : SW_HIDE);
		wSPMemory_bShow = bShow;
		if(bShow)
		{
			SetForegroundWindow(wSPMemory_hWnd);
		}
	}
}

BOOL wSPMemory_IsShow()
{
	return wSPMemory_bShow;
}

VOID wSPMemory_SetUpdate()
{
	wSPMemory_iUpdate = 1;
}

VOID wSPMemory_Destroy()
{
	if(wSPMemory_hWnd != NULL)
	{
		DestroyWindow(wSPMemory_hWnd);
	}
	
}


static VOID wSPMemory_UpdateScrollBar(HWND hWnd)
{
	RECT  rcClient;
	int   width, height;
	SCROLLINFO    sinfo;
	int   col, row;
	int   totalcol, totalrow;

	// calc client area size
	GetClientRect(hWnd, &rcClient);

	width = (rcClient.right - rcClient.left) - wSPMemory_iCharWidth * ADDR_COLS; // skip first 6 cols;
	height = (rcClient.bottom - rcClient.top) - wSPMemory_iCharHeight - LINE_SPLIT_HIGH;  // skip head height and line height

	//  avoid with and height too small
	if(width < 10) width = 10;
	if(height < 10) height = 10;


	col = width / wSPMemory_iCharWidth;
	row = height / wSPMemory_iCharHeight;

	totalcol = BYTES_PER_LINE * 3 + 1 + BYTES_PER_LINE;
	totalrow = SPMEMORY_BYTES / BYTES_PER_LINE;



	if(totalcol <= col)
	{
		// disable horz scroll bar
		EnableScrollBar(hWnd, SB_HORZ, ESB_DISABLE_BOTH);
		wSPMemory_iStartCol = 0;
	}
	else
	{
		if(wSPMemory_iStartCol > totalcol - col)
			wSPMemory_iStartCol = totalcol - col;
		memset(&sinfo, 0, sizeof(sinfo));
		sinfo.cbSize = sizeof(sinfo);
		sinfo.fMask = SIF_RANGE | SIF_POS | SIF_PAGE | SIF_DISABLENOSCROLL;
		sinfo.nMin = 0;
		sinfo.nMax = totalcol;
		sinfo.nPos = wSPMemory_iStartCol;
		sinfo.nPage = col;
		SetScrollInfo(hWnd, SB_HORZ, &sinfo, TRUE);
	}


	if(totalrow <= row)
	{
		// disable horz scroll bar
		EnableScrollBar(hWnd, SB_VERT, ESB_DISABLE_BOTH);
		wSPMemory_iStartCol = 0;
	}
	else
	{
		if(wSPMemory_iStartLine > totalrow - row)
			wSPMemory_iStartLine = totalrow - row;
		memset(&sinfo, 0, sizeof(sinfo));
		sinfo.cbSize = sizeof(sinfo);
		sinfo.fMask = SIF_RANGE | SIF_POS | SIF_PAGE | SIF_DISABLENOSCROLL;
		sinfo.nMin = 0;
		sinfo.nMax = totalrow;
		sinfo.nPos = wSPMemory_iStartLine;
		sinfo.nPage = row;
		SetScrollInfo(hWnd, SB_VERT, &sinfo, TRUE);
	}
}

static VOID wSPMemory_OnScroll(HWND hWnd, int nBar, int nCode)
{
	int           pos;
	SCROLLINFO    sinfo;
	memset(&sinfo, 0, sizeof(sinfo));
	sinfo.cbSize = sizeof(sinfo);
	sinfo.fMask = SIF_RANGE | SIF_PAGE | SIF_TRACKPOS;

	GetScrollInfo(hWnd, nBar, &sinfo);

	switch(nBar)
	{
	case SB_HORZ:
		pos = wSPMemory_iStartCol;
		switch(nCode)
		{
		case SB_LEFT:
			pos = 0;
			break;
		case SB_RIGHT:
			pos = sinfo.nMax - (int)sinfo.nPage;
			break;
		case SB_LINELEFT:
			pos--;
			break;
		case SB_LINERIGHT:
			pos++;
			break;
		case SB_PAGELEFT:
			pos -= sinfo.nPage;
			break;
		case SB_PAGERIGHT:
			pos += sinfo.nPage;
			break;
		case SB_THUMBPOSITION:
		case SB_THUMBTRACK:
			pos = sinfo.nTrackPos;
			break;
		}
		if(pos < 0)
			pos = 0;
		else if(pos > sinfo.nMax - (int)sinfo.nPage)
			pos = sinfo.nMax - (int)sinfo.nPage;
		wSPMemory_iStartCol = pos;
		break;
	case SB_VERT:
		pos = wSPMemory_iStartLine;
		switch(nCode)
		{
		case SB_TOP:
			pos = 0;
			break;
		case SB_BOTTOM:
			pos = sinfo.nMax - (int)sinfo.nPage;
			break;
		case SB_LINEUP:
			pos--;
			break;
		case SB_LINEDOWN:
			pos++;
			break;
		case SB_PAGEUP:
			pos -= sinfo.nPage;
			break;
		case SB_PAGEDOWN:
			pos += sinfo.nPage;
			break;
		case SB_THUMBPOSITION:
		case SB_THUMBTRACK:
			pos = sinfo.nTrackPos;
			break;
		}
		if(pos < 0)
			pos = 0;
		else if(pos > sinfo.nMax - (int)sinfo.nPage)
			pos = sinfo.nMax - (int)sinfo.nPage;
		wSPMemory_iStartLine = pos;
		break;
	}

	memset(&sinfo, 0, sizeof(sinfo));
	sinfo.cbSize = sizeof(sinfo);
	sinfo.fMask = SIF_POS;
	sinfo.nPos = pos;
	SetScrollInfo(hWnd, nBar, &sinfo, TRUE);

	InvalidateRect(hWnd, NULL, FALSE);

}

static VOID wSPMemory_SetCursorVisible(HWND hWnd)
{
	int  width, height;
	int  col, row;
	int  curcol, currow;
	RECT rcClient;
	int  update = 0;

	// calc client area size
	GetClientRect(hWnd, &rcClient);

	width = (rcClient.right - rcClient.left) - wSPMemory_iCharWidth * ADDR_COLS; // skip first 6 cols;
	height = (rcClient.bottom - rcClient.top) - wSPMemory_iCharHeight - LINE_SPLIT_HIGH;  // skip head height and line height

	//  avoid with and height too small
	if(width < 10) width = 10;
	if(height < 10) height = 10;


	col = width / wSPMemory_iCharWidth;
	row = height / wSPMemory_iCharHeight;

	currow = wSPMemory_iCurAddr / BYTES_PER_LINE;
	curcol = (wSPMemory_iCurAddr % BYTES_PER_LINE) * 3 + wSPMemory_iHalfChar;



	if(currow > wSPMemory_iStartLine + row - 1)
	{
		wSPMemory_iStartLine = currow - row + 1;
		update = 1;
	}
	else if(currow < wSPMemory_iStartLine)
	{
		wSPMemory_iStartLine = currow;
		update = 1;
	}

	if(curcol > wSPMemory_iStartCol + col - 1)
	{
		wSPMemory_iStartCol = curcol - col + 1;
		update = 1;
	}
	else if(curcol < wSPMemory_iStartCol)
	{
		wSPMemory_iStartCol = curcol;
		update = 1;
	}

	if(update)
	{
		wSPMemory_UpdateScrollBar(hWnd);
	}
}

static VOID wSPMemory_SetCurAddr(HWND hWnd, int iAddr)
{
	if(iAddr >= 0 && iAddr < SPMEMORY_BYTES)
	{
		wSPMemory_iCurAddr = iAddr;
		wSPMemory_iHalfChar = 0;
		wSPMemory_SetCursorVisible(hWnd);
		InvalidateRect(hWnd, NULL, FALSE);
	}
}


static VOID wSPMemory_OnLButtonDown(HWND hWnd, int x, int y, DWORD dwKeyState)
{
	int row, col;
	//int addr;
	RECT rcClient;
	GetClientRect(hWnd, &rcClient);
	x -= ADDR_COLS * wSPMemory_iCharWidth;
	y -= HEAD_LINES * wSPMemory_iCharHeight + LINE_SPLIT_HIGH;
	if(x > 0 && y > 0)
	{
		row = y / wSPMemory_iCharHeight + wSPMemory_iStartLine;
		col = x / wSPMemory_iCharWidth + wSPMemory_iStartCol;

		if(col / 3 < BYTES_PER_LINE /* && col % 3 < 2 */)
		{
			wSPMemory_SetCurAddr(hWnd, row * BYTES_PER_LINE + col / 3);
		}
		else
		{
			col -= BYTES_PER_LINE * 3 + 1;
			if(col >= 0 && col < BYTES_PER_LINE)
			{
				wSPMemory_SetCurAddr(hWnd, wSPMemory_iCurAddr = row * BYTES_PER_LINE + col);
			}
		}
	}
}

static VOID wSPMemory_OnKeyDown(HWND hWnd, UINT nKeyCode)
{
	switch(nKeyCode)
	{
	case VK_LEFT:
		wSPMemory_SetCurAddr(hWnd, wSPMemory_iCurAddr - 1);
		break;
	case VK_RIGHT:
		wSPMemory_SetCurAddr(hWnd, wSPMemory_iCurAddr + 1);
		break;
	case VK_UP:
		wSPMemory_SetCurAddr(hWnd, wSPMemory_iCurAddr - BYTES_PER_LINE);
		break;
	case VK_DOWN:
		wSPMemory_SetCurAddr(hWnd, wSPMemory_iCurAddr + BYTES_PER_LINE);
		break;
	case VK_PRIOR:
		wSPMemory_OnScroll(hWnd, SB_VERT, SB_PAGEUP);
		break;
	case VK_NEXT:
		wSPMemory_OnScroll(hWnd, SB_VERT, SB_PAGEDOWN);
		break;
	case VK_HOME:
		wSPMemory_OnScroll(hWnd, SB_VERT, SB_TOP);
		break;
	case VK_END:
		wSPMemory_OnScroll(hWnd, SB_VERT, SB_BOTTOM);
		break;
	}
}


static VOID wSPMemory_OnChar(HWND hWnd, UINT nChar)
{
	int addr;
	ines_byte_t* p_mem;
	ines_byte_t  val;

	addr = wSPMemory_iCurAddr;
	p_mem = NULL;
	if(host.status != NES_STATUS_OFF)
	{
		p_mem = host.ppu.sp_RAM + addr;
	}

	if(p_mem == NULL)
		return;

	if(nChar >= '0' && nChar <='9')
	{
		val = nChar - '0';
	}
	else if(nChar >= 'a' && nChar <= 'f')
	{
		val = nChar - 'a' + 10;
	}
	else if(nChar >= 'A' && nChar <= 'F')
	{
		val = nChar - 'A' + 10;
	}
	else
	{
		// invalid hex number 
		return;
	}

	if(wSPMemory_iHalfChar == 0)
	{
		*p_mem = (*p_mem & 0x0f) | (val << 4);
		wSPMemory_iHalfChar = 1;
	}
	else
	{
		*p_mem = (*p_mem & 0xf0) | (val);
		wSPMemory_iHalfChar = 0;
		wSPMemory_iCurAddr++;
	}
	wSPMemory_SetCursorVisible(hWnd);
	wSPMemory_iUpdate = 1;
}


static BOOL wSPMemory_OnCreate(HWND hWnd, LPCREATESTRUCT  lpCreateStruct)
{
	LOGFONT    lfo;
	HFONT      hFont;
	RECT       rc;
	HDC        hWndDC;
	HFONT      hOldFont;
	//HMENU      hMenu;

	// create font 
	memset(&lfo, 0, sizeof(lfo));
	GetObject(GetStockObject(SYSTEM_FONT),sizeof(LOGFONT),&lfo);
	lfo.lfHeight = 12;
	lfo.lfWidth = 0;
	lfo.lfWeight = 300;
	_tcsncpy(lfo.lfFaceName, _T("Courier New"), count_of(lfo.lfFaceName));
	hFont = CreateFontIndirect(&lfo);

	if(hFont == NULL)
	{
		return FALSE;
	}

	wSPMemory_hFont = hFont;

	hWndDC = GetDC(hWnd);
	hOldFont = (HFONT)SelectObject(hWndDC, (HGDIOBJ)hFont);

	SetRect(&rc, 0,0,0,0);
	DrawText(hWndDC, _T("X"), 1, &rc, DT_CALCRECT|DT_NOPREFIX);
	wSPMemory_iCharWidth = rc.right - rc.left;
	wSPMemory_iCharHeight = rc.bottom - rc.top;
	SelectObject(hWndDC, (HGDIOBJ)hOldFont);

	
	ReleaseDC(hWnd, hWndDC);


	//hMenu = LoadMenu(GetModuleHandle(NULL), MAKEINTRESOURCE(IDC_MEMORY_VIEW));
	// SetMenu(hWnd, hMenu);
	//AppendMenu(hMenu, MF_STRING, 0x1001, _T("程序内存"))


	SetTimer(hWnd, 1, 50, NULL);
	return TRUE;
}


static VOID wSPMemory_OnDrawEx(HWND hWnd, HDC hDC, LPRECT lpClipRect)
{
	RECT     rc;
	RECT     rcClient;
	LOGBRUSH lbs;
	HBRUSH   hBrushBG;
	LOGPEN   lpen;
	HPEN     hPenLine;
	HPEN     hOldPen;
	HFONT    hOldFont;
	int      iTotalLines;
	int      iLine;
	int      iCol;
	int      addr;
	//int      val;
	ines_byte_t   *p_mem;
	int      x,y;
	TCHAR    szText[1024];
	int      n;

	lbs.lbColor = GetSysColor(COLOR_WINDOW);
	lbs.lbStyle = BS_SOLID;
	lbs.lbHatch = 0;
	hBrushBG = CreateBrushIndirect(&lbs);

	lpen.lopnColor = GetSysColor(COLOR_WINDOWTEXT);
	lpen.lopnStyle = PS_SOLID;
	lpen.lopnWidth.x = 1;
	lpen.lopnWidth.y = 1;
	hPenLine = CreatePenIndirect(&lpen);

	// client rect 
	GetClientRect(hWnd, &rcClient);

	// fill background
	FillRect(hDC, lpClipRect, hBrushBG);

	hOldPen = (HPEN)SelectObject(hDC, (HGDIOBJ)hPenLine);
	hOldFont = (HFONT)SelectObject(hDC, (HGDIOBJ)wSPMemory_hFont);

	x = 0;
	y = 0;
	wsprintf(szText, _T("%s  %s"), _T("ADDR"), _T("+0 +1 +2 +3 +4 +5 +6 +7 +8 +9 +A +B +C +D +E +F  0123456789ABCDEF")+wSPMemory_iStartCol);

	// Draw head line
	TextOut(hDC, x, y, szText, _tcslen(szText));
	y += wSPMemory_iCharHeight;

	// draw split line
	MoveToEx(hDC, 0, y + LINE_SPLIT_HIGH/2, NULL);
	LineTo(hDC, rcClient.right, y + LINE_SPLIT_HIGH/2);

	y += LINE_SPLIT_HIGH;

	iTotalLines = (rcClient.bottom - rcClient.top - wSPMemory_iCharHeight * HEAD_LINES - LINE_SPLIT_HIGH) / wSPMemory_iCharHeight + 1; 

	if(iTotalLines + wSPMemory_iStartLine > SPMEMORY_BYTES / BYTES_PER_LINE )
		iTotalLines = SPMEMORY_BYTES / BYTES_PER_LINE - wSPMemory_iStartLine;

	for(iLine = 0; iLine < iTotalLines; iLine++)
	{
		addr = (wSPMemory_iStartLine + iLine) * BYTES_PER_LINE;
		n = wsprintf(szText, _T("%04X  "), addr);

		p_mem = NULL;
		if(host.status != NES_STATUS_OFF)
		{
			p_mem = host.ppu.sp_RAM + addr;
		}

		for(iCol = 0; iCol < BYTES_PER_LINE; iCol++)
		{
			if(p_mem)
				n += wsprintf(szText+n, _T("%02X "), p_mem[iCol]);
			else
				n += wsprintf(szText+n, _T("\?\? "));
		}
		szText[n++] = ' ';
		for(iCol = 0; iCol < 16; iCol++)
		{
			if(p_mem && isprint(p_mem[iCol]))
			{
				szText[n++] = (TCHAR)p_mem[iCol];
			}
			else
			{
				szText[n++] = '.';
			}
		}

		szText[n++] = 0;


		if(wSPMemory_iStartCol > 0)
		{
			memmove(&szText[6], &szText[ADDR_COLS + wSPMemory_iStartCol], sizeof(szText[0]) * (n - ADDR_COLS - wSPMemory_iStartCol + 1));
		}

		TextOut(hDC, x, y, szText, _tcslen(szText));
		y += wSPMemory_iCharHeight;
	}

	// draw cursor
	iLine = wSPMemory_iCurAddr / BYTES_PER_LINE - wSPMemory_iStartLine;
	iCol = wSPMemory_iCurAddr % BYTES_PER_LINE;
	if(iLine >= 0 && iLine < iTotalLines)
	{
		if(iCol * 3 + wSPMemory_iHalfChar >= wSPMemory_iStartCol)
		{
			// draw cursor
			rc.left = (iCol * 3 + wSPMemory_iHalfChar - wSPMemory_iStartCol + ADDR_COLS)  * wSPMemory_iCharWidth;
			rc.top = (iLine + HEAD_LINES) * wSPMemory_iCharHeight + LINE_SPLIT_HIGH;
			rc.right = rc.left + wSPMemory_iCharWidth;
			rc.bottom = rc.top + wSPMemory_iCharHeight;
			InvertRect(hDC, &rc);
		} 

		if(iCol + BYTES_PER_LINE * 3 + 1 >= wSPMemory_iStartCol  )
		{
			rc.left = (iCol + BYTES_PER_LINE * 3 + 1 - wSPMemory_iStartCol + ADDR_COLS)  * wSPMemory_iCharWidth;
			rc.top = (iLine + HEAD_LINES) * wSPMemory_iCharHeight + LINE_SPLIT_HIGH;
			rc.right = rc.left + wSPMemory_iCharWidth;
			rc.bottom = rc.top + wSPMemory_iCharHeight;
			InvertRect(hDC, &rc);
		}
	}




	SelectObject(hDC, (HGDIOBJ)hOldPen);
	SelectObject(hDC, (HGDIOBJ)hOldFont);	

	DeleteObject((HGDIOBJ)hBrushBG);
	DeleteObject((HGDIOBJ)hPenLine);


}

static VOID wSPMemory_OnDraw(HWND hWnd, HDC hPaintDC)
{
	HDC       hMemDC;
	HBITMAP   hMemBitmap;
	RECT      rcClient;
	RECT      rcClip;
	int       width;
	int       height;
	HRGN      hClipRgn;
	HBITMAP   hOldBitmap;
	LARGE_INTEGER   llf, lls, lle;

	QueryPerformanceFrequency(&llf);
	QueryPerformanceCounter(&lls);

	GetClientRect(hWnd, &rcClient);

	width = rcClient.right - rcClient.left;
	height = rcClient.bottom - rcClient.top;
	hMemDC = NULL;
	hMemBitmap = NULL;
	hClipRgn = NULL;

	hMemDC = CreateCompatibleDC(hPaintDC);
	hMemBitmap = CreateCompatibleBitmap(hPaintDC, width, height);
	hOldBitmap = NULL;

	GetClipBox(hPaintDC, &rcClip);


	if(hMemDC && hMemBitmap)
	{
		hOldBitmap = (HBITMAP)SelectObject(hMemDC, (HGDIOBJ)hMemBitmap);
		hClipRgn = CreateRectRgnIndirect(&rcClip);
		SelectClipRgn(hMemDC, hClipRgn);

		wSPMemory_OnDrawEx(hWnd, hMemDC, &rcClip);

		// Copy 
		BitBlt(hPaintDC, rcClip.left, rcClip.top, width, height, hMemDC, rcClip.left, rcClip.top, SRCCOPY);

		SelectObject(hMemDC, (HGDIOBJ)hOldBitmap);

	}
	else
	{
		wSPMemory_OnDrawEx(hWnd, hPaintDC, &rcClip);
	}

	if(hMemBitmap != NULL)
	{
		DeleteObject((HGDIOBJ)hMemBitmap);
	}
	if(hMemDC != NULL)
	{
		DeleteDC(hMemDC);
	}
	if(hClipRgn != NULL)
	{
		DeleteObject((HGDIOBJ)hClipRgn);
	}
	QueryPerformanceCounter(&lle);

	INES_LOG(LOG_DBG, MOD_SYS, ISTR("%s: time=%d us\n"), __TFUNCTION__, (int)(1000000.0 * (lle.QuadPart - lls.QuadPart) / llf.QuadPart));

}

static VOID wSPMemory_OnPaint(HWND hWnd)
{
	HDC     hPaintDC;
	PAINTSTRUCT   ps;
	hPaintDC = BeginPaint(hWnd, &ps);

	wSPMemory_OnDraw(hWnd, hPaintDC);

	EndPaint(hWnd, &ps);
}


static VOID wSPMemory_OnDestroy(HWND hWnd)
{
	KillTimer(hWnd, 1);
	if(wSPMemory_hFont != NULL)
	{
		DeleteObject((HGDIOBJ)wSPMemory_hFont);
		wSPMemory_hFont = NULL;
	}
	wSPMemory_iCharHeight = 0;
	wSPMemory_iCharWidth = 0;

	wSPMemory_hWnd = NULL;
	wSPMemory_bShow = FALSE;

}



LRESULT wSPMemory_OnNcCalcSize(HWND hWnd, WPARAM wParam, LPARAM lParam)
{
	NCCALCSIZE_PARAMS* lpncsp = (NCCALCSIZE_PARAMS*)lParam;
	LRESULT hr = DefWindowProc(hWnd, WM_NCCALCSIZE, wParam, lParam);
	
	//int cy = GetSystemMetrics(SM_CYMENU);
	//lpncsp->rgrc[0].top += cy;

	return hr;
}

LRESULT wSPMemory_OnNcPaint(HWND hWnd, WPARAM wParam, LPARAM lParam)
{
	LRESULT hr = DefWindowProc(hWnd, WM_NCPAINT, wParam, lParam);
	/*
	HDC   hWndDC = GetWindowDC(hWnd);
	RECT rcWindow;
	RECT rcClient;

	GetClientRect(hWnd, &rcClient);
	GetWindowRect(hWnd, &rcWindow);

	ClientToScreen(hWnd, (LPPOINT)&rcClient.left);
	ClientToScreen(hWnd, (LPPOINT)&rcClient.right);

	rcWindow.left += GetSystemMetrics(SM_CXBORDER);
	rcWindow.right -= GetSystemMetrics(SM_CXBORDER);
	rcWindow.top += GetSystemMetrics(SM_CYCAPTION);
	rcWindow.bottom = rcClient.top;

	// DrawFrameControl(hWndDC, &rcWindow, DFC_MENU,  );
	DrawFocusRect(hWndDC, &rcWindow);


	ReleaseDC(hWnd, hWndDC);
	*/

	return hr;
}

static LRESULT CALLBACK	wSPMemory_WindowProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
	switch(uMsg)
	{
	case WM_CREATE:
		if(wSPMemory_OnCreate(hWnd, (LPCREATESTRUCT)lParam))
			return 0;
		else
			return -1;
		break;
	case WM_CLOSE:
		DestroyWindow(hWnd);
		return 0;
		break;
	case WM_PAINT:
		wSPMemory_OnPaint(hWnd);
		return 0;
	case WM_TIMER:
		if(wSPMemory_iUpdate != 0)
		{
			wSPMemory_iUpdate = 0;
			InvalidateRect(hWnd, NULL, FALSE);
		}
		break;
	case WM_SIZE:
		wSPMemory_UpdateScrollBar(hWnd);
		break;
	case WM_VSCROLL:
		wSPMemory_OnScroll(hWnd, SB_VERT, LOWORD(wParam));
		break;
	case WM_HSCROLL:
		wSPMemory_OnScroll(hWnd, SB_HORZ, LOWORD(wParam));
		break;
	case WM_LBUTTONDOWN:
		wSPMemory_OnLButtonDown(hWnd, (short)LOWORD(lParam), (short)HIWORD(lParam), wParam);
		break;
	case WM_LBUTTONUP:
		break;
	case WM_MOUSEMOVE:
		break;
	case WM_MOUSEWHEEL:
		break;
	case WM_NCHITTEST:
		break;
	case WM_SETCURSOR:
		break;
	case WM_NCCALCSIZE:
		return wSPMemory_OnNcCalcSize(hWnd, wParam, lParam);
	case WM_NCPAINT:
		return wSPMemory_OnNcPaint(hWnd, wParam, lParam);
		break;
	case WM_CHAR:
		wSPMemory_OnChar(hWnd, wParam);
		break;
	case WM_KEYDOWN:
		wSPMemory_OnKeyDown(hWnd, wParam);
		break;
	case WM_KEYUP:
		break;
	case WM_DESTROY:
		wSPMemory_OnDestroy(hWnd);
		break;
	}
	return DefWindowProc(hWnd, uMsg, wParam, lParam);
}
