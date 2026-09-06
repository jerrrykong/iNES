#include "stdafx.h"
#include "../comm/log.h"
#include "../core/nes.h"
#include "wMemory.h"
#include "Resource.h"


extern ines_host_t   host;

static HWND wMemory_hWnd = NULL;
static BOOL wMemory_bShow = FALSE;
static const TCHAR wMemory_szClassName[128] = _T("iNES_MemoryWnd");
static TCHAR wMemory_szTitle[MAX_LOADSTRING];

// draw context
static HFONT    wMemory_hFont = NULL;
static int wMemory_iCharWidth = 0;
static int wMemory_iCharHeight = 0;
//static HBITMAP wMemory_hBmpChar = NULL;
static int wMemory_iStartLine = 0;
static int wMemory_iStartCol = 0;
static int wMemory_iUpdate = 0;
static int wMemory_iCurAddr = 0;
static int wMemory_iHalfChar = 0;

#define MEMORY_BYTES           0x10000
#define LINE_SPLIT_HIGH        3
#define BYTES_PER_LINE         16
#define HEAD_LINES             1
#define ADDR_COLS              6

static LRESULT CALLBACK	wMemory_WindowProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam);

static ATOM wMemory_RegisterClass(HINSTANCE  hInstance)
{
	WNDCLASSEX wcex;

	wcex.cbSize = sizeof(WNDCLASSEX);

	wcex.style			= CS_HREDRAW | CS_VREDRAW | CS_OWNDC;
	wcex.lpfnWndProc	= wMemory_WindowProc;
	wcex.cbClsExtra		= 0;
	wcex.cbWndExtra		= 0;
	wcex.hInstance		= hInstance;
	wcex.hIcon			= NULL; // LoadIcon(hInstance, MAKEINTRESOURCE(IDI_INES));
	wcex.hCursor		= LoadCursor(NULL, IDC_ARROW);
	wcex.hbrBackground	= /* (HBRUSH)(COLOR_WINDOW+1)*/ (HBRUSH)GetStockObject(NULL_BRUSH);
	wcex.lpszMenuName	= NULL; // MAKEINTRESOURCE(IDC_INES);
	wcex.lpszClassName	= wMemory_szClassName;
	wcex.hIconSm		= NULL; // LoadIcon(wcex.hInstance, MAKEINTRESOURCE(IDI_SMALL));

	return RegisterClassEx(&wcex);
}



BOOL wMemory_Create(HINSTANCE hInstance, HWND hParentWnd)
{
	// already created
	if(wMemory_hWnd != NULL)
	{
		/// SetParent(wMemory_hWnd, hParentWnd);
		return TRUE;
	}

	LoadString(hInstance, IDS_WND_MEMORY_TITLE, wMemory_szTitle, count_of(wMemory_szTitle));

	wMemory_RegisterClass(hInstance);
	 
	wMemory_hWnd = CreateWindowEx(0&WS_EX_TOOLWINDOW, wMemory_szClassName, wMemory_szTitle, 
		WS_POPUPWINDOW|WS_CAPTION|WS_OVERLAPPED|WS_SIZEBOX|WS_MINIMIZEBOX|WS_HSCROLL|WS_VSCROLL,
		CW_USEDEFAULT, CW_USEDEFAULT, 512, 512, NULL, NULL, hInstance, NULL);



	return TRUE;
}


VOID wMemory_Show(BOOL bShow)
{
	if(wMemory_hWnd != NULL)
	{
		ShowWindow(wMemory_hWnd, bShow ? SW_SHOWNORMAL : SW_HIDE);
		wMemory_bShow = bShow;
		if(bShow)
		{
			SetForegroundWindow(wMemory_hWnd);
		}
	}
}

BOOL wMemory_IsShow()
{
	return wMemory_bShow;
}

VOID wMemory_SetUpdate()
{
	wMemory_iUpdate = 1;
}

VOID wMemory_Destroy()
{
	if(wMemory_hWnd != NULL)
	{
		DestroyWindow(wMemory_hWnd);
	}
	
}


static VOID wMemory_UpdateScrollBar(HWND hWnd)
{
	RECT  rcClient;
	int   width, height;
	SCROLLINFO    sinfo;
	int   col, row;
	int   totalcol, totalrow;

	// calc client area size
	GetClientRect(hWnd, &rcClient);

	width = (rcClient.right - rcClient.left) - wMemory_iCharWidth * ADDR_COLS; // skip first 6 cols;
	height = (rcClient.bottom - rcClient.top) - wMemory_iCharHeight - LINE_SPLIT_HIGH;  // skip head height and line height

	//  avoid with and height too small
	if(width < 10) width = 10;
	if(height < 10) height = 10;


	col = width / wMemory_iCharWidth;
	row = height / wMemory_iCharHeight;

	totalcol = BYTES_PER_LINE * 3 + 1 + BYTES_PER_LINE;
	totalrow = MEMORY_BYTES / BYTES_PER_LINE;



	if(totalcol <= col)
	{
		// disable horz scroll bar
		EnableScrollBar(hWnd, SB_HORZ, ESB_DISABLE_BOTH);
		wMemory_iStartCol = 0;
	}
	else
	{
		if(wMemory_iStartCol > totalcol - col)
			wMemory_iStartCol = totalcol - col;
		memset(&sinfo, 0, sizeof(sinfo));
		sinfo.cbSize = sizeof(sinfo);
		sinfo.fMask = SIF_RANGE | SIF_POS | SIF_PAGE | SIF_DISABLENOSCROLL;
		sinfo.nMin = 0;
		sinfo.nMax = totalcol;
		sinfo.nPos = wMemory_iStartCol;
		sinfo.nPage = col;
		SetScrollInfo(hWnd, SB_HORZ, &sinfo, TRUE);
	}


	if(totalrow <= row)
	{
		// disable horz scroll bar
		EnableScrollBar(hWnd, SB_VERT, ESB_DISABLE_BOTH);
		wMemory_iStartCol = 0;
	}
	else
	{
		if(wMemory_iStartLine > totalrow - row)
			wMemory_iStartLine = totalrow - row;
		memset(&sinfo, 0, sizeof(sinfo));
		sinfo.cbSize = sizeof(sinfo);
		sinfo.fMask = SIF_RANGE | SIF_POS | SIF_PAGE | SIF_DISABLENOSCROLL;
		sinfo.nMin = 0;
		sinfo.nMax = totalrow;
		sinfo.nPos = wMemory_iStartLine;
		sinfo.nPage = row;
		SetScrollInfo(hWnd, SB_VERT, &sinfo, TRUE);
	}
}

static VOID wMemory_OnScroll(HWND hWnd, int nBar, int nCode)
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
		pos = wMemory_iStartCol;
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
		wMemory_iStartCol = pos;
		break;
	case SB_VERT:
		pos = wMemory_iStartLine;
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
		wMemory_iStartLine = pos;
		break;
	}

	memset(&sinfo, 0, sizeof(sinfo));
	sinfo.cbSize = sizeof(sinfo);
	sinfo.fMask = SIF_POS;
	sinfo.nPos = pos;
	SetScrollInfo(hWnd, nBar, &sinfo, TRUE);

	InvalidateRect(hWnd, NULL, FALSE);

}

static VOID wMemory_SetCursorVisible(HWND hWnd)
{
	int  width, height;
	int  col, row;
	int  curcol, currow;
	RECT rcClient;
	int  update = 0;

	// calc client area size
	GetClientRect(hWnd, &rcClient);

	width = (rcClient.right - rcClient.left) - wMemory_iCharWidth * ADDR_COLS; // skip first 6 cols;
	height = (rcClient.bottom - rcClient.top) - wMemory_iCharHeight - LINE_SPLIT_HIGH;  // skip head height and line height

	//  avoid with and height too small
	if(width < 10) width = 10;
	if(height < 10) height = 10;


	col = width / wMemory_iCharWidth;
	row = height / wMemory_iCharHeight;

	currow = wMemory_iCurAddr / BYTES_PER_LINE;
	curcol = (wMemory_iCurAddr % BYTES_PER_LINE) * 3 + wMemory_iHalfChar;



	if(currow > wMemory_iStartLine + row - 1)
	{
		wMemory_iStartLine = currow - row + 1;
		update = 1;
	}
	else if(currow < wMemory_iStartLine)
	{
		wMemory_iStartLine = currow;
		update = 1;
	}

	if(curcol > wMemory_iStartCol + col - 1)
	{
		wMemory_iStartCol = curcol - col + 1;
		update = 1;
	}
	else if(curcol < wMemory_iStartCol)
	{
		wMemory_iStartCol = curcol;
		update = 1;
	}

	if(update)
	{
		wMemory_UpdateScrollBar(hWnd);
	}
}

static VOID wMemory_SetCurAddr(HWND hWnd, int iAddr)
{
	if(iAddr >= 0 && iAddr < MEMORY_BYTES)
	{
		wMemory_iCurAddr = iAddr;
		wMemory_iHalfChar = 0;
		wMemory_SetCursorVisible(hWnd);
		InvalidateRect(hWnd, NULL, FALSE);
	}
}


static VOID wMemory_OnLButtonDown(HWND hWnd, int x, int y, DWORD dwKeyState)
{
	int row, col;
	//int addr;
	RECT rcClient;
	GetClientRect(hWnd, &rcClient);
	x -= ADDR_COLS * wMemory_iCharWidth;
	y -= HEAD_LINES * wMemory_iCharHeight + LINE_SPLIT_HIGH;
	if(x > 0 && y > 0)
	{
		row = y / wMemory_iCharHeight + wMemory_iStartLine;
		col = x / wMemory_iCharWidth + wMemory_iStartCol;

		if(col / 3 < BYTES_PER_LINE /* && col % 3 < 2 */)
		{
			wMemory_SetCurAddr(hWnd, row * BYTES_PER_LINE + col / 3);
		}
		else
		{
			col -= BYTES_PER_LINE * 3 + 1;
			if(col >= 0 && col < BYTES_PER_LINE)
			{
				wMemory_SetCurAddr(hWnd, wMemory_iCurAddr = row * BYTES_PER_LINE + col);
			}
		}
	}
}

static VOID wMemory_OnKeyDown(HWND hWnd, UINT nKeyCode)
{
	switch(nKeyCode)
	{
	case VK_LEFT:
		wMemory_SetCurAddr(hWnd, wMemory_iCurAddr - 1);
		break;
	case VK_RIGHT:
		wMemory_SetCurAddr(hWnd, wMemory_iCurAddr + 1);
		break;
	case VK_UP:
		wMemory_SetCurAddr(hWnd, wMemory_iCurAddr - BYTES_PER_LINE);
		break;
	case VK_DOWN:
		wMemory_SetCurAddr(hWnd, wMemory_iCurAddr + BYTES_PER_LINE);
		break;
	case VK_PRIOR:
		wMemory_OnScroll(hWnd, SB_VERT, SB_PAGEUP);
		break;
	case VK_NEXT:
		wMemory_OnScroll(hWnd, SB_VERT, SB_PAGEDOWN);
		break;
	case VK_HOME:
		wMemory_OnScroll(hWnd, SB_VERT, SB_TOP);
		break;
	case VK_END:
		wMemory_OnScroll(hWnd, SB_VERT, SB_BOTTOM);
		break;
	}
}


static VOID wMemory_OnChar(HWND hWnd, UINT nChar)
{
	int addr;
	ines_byte_t* p_mem;
	ines_byte_t  val;

	addr = wMemory_iCurAddr;
	p_mem = NULL;
	if(host.status != NES_STATUS_OFF)
	{
		switch(addr>>13)
		{
		case 0:  // CPU RAM  SPACE
			if(addr < 0x800)
			{
				p_mem = &host.cpu.RAM[addr];
			}
			break;
		case 1: case 2:  // REGISTER SPACE
			break;
		case 3:  // SAVE RAM  SPACE
		case 4:case 5:case 6:case 7:  // PROGRAM ROM SPACE
			if(host.cpu.mem_bank[addr>>13])
				p_mem = &host.cpu.mem_bank[addr>>13][addr&0x1fff]; 
			break;
		}
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

	if(wMemory_iHalfChar == 0)
	{
		*p_mem = (*p_mem & 0x0f) | (val << 4);
		wMemory_iHalfChar = 1;
	}
	else
	{
		*p_mem = (*p_mem & 0xf0) | (val);
		wMemory_iHalfChar = 0;
		wMemory_iCurAddr++;
	}
	wMemory_SetCursorVisible(hWnd);
	wMemory_iUpdate = 1;
}


static BOOL wMemory_OnCreate(HWND hWnd, LPCREATESTRUCT  lpCreateStruct)
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

	wMemory_hFont = hFont;

	hWndDC = GetDC(hWnd);
	hOldFont = (HFONT)SelectObject(hWndDC, (HGDIOBJ)hFont);

	SetRect(&rc, 0,0,0,0);
	DrawText(hWndDC, _T("X"), 1, &rc, DT_CALCRECT|DT_NOPREFIX);
	wMemory_iCharWidth = rc.right - rc.left;
	wMemory_iCharHeight = rc.bottom - rc.top;
	SelectObject(hWndDC, (HGDIOBJ)hOldFont);

	
	ReleaseDC(hWnd, hWndDC);


	//hMenu = LoadMenu(GetModuleHandle(NULL), MAKEINTRESOURCE(IDC_MEMORY_VIEW));
	// SetMenu(hWnd, hMenu);
	//AppendMenu(hMenu, MF_STRING, 0x1001, _T("³ÌÐòÄÚ´æ"))


	SetTimer(hWnd, 1, 50, NULL);
	return TRUE;
}


static VOID wMemory_OnDrawEx(HWND hWnd, HDC hDC, LPRECT lpClipRect)
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
	hOldFont = (HFONT)SelectObject(hDC, (HGDIOBJ)wMemory_hFont);

	x = 0;
	y = 0;
	wsprintf(szText, _T("%s  %s"), _T("ADDR"), _T("+0 +1 +2 +3 +4 +5 +6 +7 +8 +9 +A +B +C +D +E +F  0123456789ABCDEF")+wMemory_iStartCol);

	// Draw head line
	TextOut(hDC, x, y, szText, _tcslen(szText));
	y += wMemory_iCharHeight;

	// draw split line
	MoveToEx(hDC, 0, y + LINE_SPLIT_HIGH/2, NULL);
	LineTo(hDC, rcClient.right, y + LINE_SPLIT_HIGH/2);

	y += LINE_SPLIT_HIGH;

	iTotalLines = (rcClient.bottom - rcClient.top - wMemory_iCharHeight * HEAD_LINES - LINE_SPLIT_HIGH) / wMemory_iCharHeight + 1; 

	if(iTotalLines + wMemory_iStartLine > MEMORY_BYTES / BYTES_PER_LINE )
		iTotalLines = MEMORY_BYTES / BYTES_PER_LINE - wMemory_iStartLine;

	for(iLine = 0; iLine < iTotalLines; iLine++)
	{
		addr = (wMemory_iStartLine + iLine) * BYTES_PER_LINE;
		n = wsprintf(szText, _T("%04X  "), addr);

		p_mem = NULL;
		if(host.status != NES_STATUS_OFF)
		{
			switch(addr>>13)
			{
			case 0:  // CPU RAM  SPACE
				if(addr < 0x800)
				{
					p_mem = &host.cpu.RAM[addr];
				}
				break;
			case 1: case 2:  // REGISTER SPACE
				break;
			case 3:  // SAVE RAM  SPACE
			case 4:case 5:case 6:case 7:  // PROGRAM ROM SPACE
				if(host.cpu.mem_bank[addr>>13])
					p_mem = &host.cpu.mem_bank[addr>>13][addr&0x1fff]; 
				break;
			}
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


		if(wMemory_iStartCol > 0)
		{
			memmove(&szText[6], &szText[ADDR_COLS + wMemory_iStartCol], sizeof(szText[0]) * (n - ADDR_COLS - wMemory_iStartCol + 1));
		}

		TextOut(hDC, x, y, szText, _tcslen(szText));
		y += wMemory_iCharHeight;
	}

	// draw cursor
	iLine = wMemory_iCurAddr / BYTES_PER_LINE - wMemory_iStartLine;
	iCol = wMemory_iCurAddr % BYTES_PER_LINE;
	if(iLine >= 0 && iLine < iTotalLines)
	{
		if(iCol * 3 + wMemory_iHalfChar >= wMemory_iStartCol)
		{
			// draw cursor
			rc.left = (iCol * 3 + wMemory_iHalfChar - wMemory_iStartCol + ADDR_COLS)  * wMemory_iCharWidth;
			rc.top = (iLine + HEAD_LINES) * wMemory_iCharHeight + LINE_SPLIT_HIGH;
			rc.right = rc.left + wMemory_iCharWidth;
			rc.bottom = rc.top + wMemory_iCharHeight;
			InvertRect(hDC, &rc);
		} 

		if(iCol + BYTES_PER_LINE * 3 + 1 >= wMemory_iStartCol  )
		{
			rc.left = (iCol + BYTES_PER_LINE * 3 + 1 - wMemory_iStartCol + ADDR_COLS)  * wMemory_iCharWidth;
			rc.top = (iLine + HEAD_LINES) * wMemory_iCharHeight + LINE_SPLIT_HIGH;
			rc.right = rc.left + wMemory_iCharWidth;
			rc.bottom = rc.top + wMemory_iCharHeight;
			InvertRect(hDC, &rc);
		}
	}




	SelectObject(hDC, (HGDIOBJ)hOldPen);
	SelectObject(hDC, (HGDIOBJ)hOldFont);	

	DeleteObject((HGDIOBJ)hBrushBG);
	DeleteObject((HGDIOBJ)hPenLine);


}

static VOID wMemory_OnDraw(HWND hWnd, HDC hPaintDC)
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

		wMemory_OnDrawEx(hWnd, hMemDC, &rcClip);

		// Copy 
		BitBlt(hPaintDC, rcClip.left, rcClip.top, width, height, hMemDC, rcClip.left, rcClip.top, SRCCOPY);

		SelectObject(hMemDC, (HGDIOBJ)hOldBitmap);

	}
	else
	{
		wMemory_OnDrawEx(hWnd, hPaintDC, &rcClip);
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

static VOID wMemory_OnPaint(HWND hWnd)
{
	HDC     hPaintDC;
	PAINTSTRUCT   ps;
	hPaintDC = BeginPaint(hWnd, &ps);

	wMemory_OnDraw(hWnd, hPaintDC);

	EndPaint(hWnd, &ps);
}


static VOID wMemory_OnDestroy(HWND hWnd)
{
	KillTimer(hWnd, 1);

	if(wMemory_hFont != NULL)
	{
		DeleteObject((HGDIOBJ)wMemory_hFont);
		wMemory_hFont = NULL;
	}
	wMemory_iCharHeight = 0;
	wMemory_iCharWidth = 0;

	wMemory_hWnd = NULL;
	wMemory_bShow = FALSE;

}



LRESULT wMemory_OnNcCalcSize(HWND hWnd, WPARAM wParam, LPARAM lParam)
{
	NCCALCSIZE_PARAMS* lpncsp = (NCCALCSIZE_PARAMS*)lParam;
	LRESULT hr = DefWindowProc(hWnd, WM_NCCALCSIZE, wParam, lParam);
	
	//int cy = GetSystemMetrics(SM_CYMENU);
	//lpncsp->rgrc[0].top += cy;

	return hr;
}

LRESULT wMemory_OnNcPaint(HWND hWnd, WPARAM wParam, LPARAM lParam)
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

static LRESULT CALLBACK	wMemory_WindowProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
	switch(uMsg)
	{
	case WM_CREATE:
		if(wMemory_OnCreate(hWnd, (LPCREATESTRUCT)lParam))
			return 0;
		else
			return -1;
		break;
	case WM_CLOSE:
		DestroyWindow(hWnd);
		return 0;
		break;
	case WM_PAINT:
		wMemory_OnPaint(hWnd);
		return 0;
	case WM_TIMER:
		if(wMemory_iUpdate != 0)
		{
			wMemory_iUpdate = 0;
			InvalidateRect(hWnd, NULL, FALSE);
		}
		break;
	case WM_SIZE:
		wMemory_UpdateScrollBar(hWnd);
		break;
	case WM_VSCROLL:
		wMemory_OnScroll(hWnd, SB_VERT, LOWORD(wParam));
		break;
	case WM_HSCROLL:
		wMemory_OnScroll(hWnd, SB_HORZ, LOWORD(wParam));
		break;
	case WM_LBUTTONDOWN:
		wMemory_OnLButtonDown(hWnd, (short)LOWORD(lParam), (short)HIWORD(lParam), wParam);
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
		return wMemory_OnNcCalcSize(hWnd, wParam, lParam);
	case WM_NCPAINT:
		return wMemory_OnNcPaint(hWnd, wParam, lParam);
		break;
	case WM_CHAR:
		wMemory_OnChar(hWnd, wParam);
		break;
	case WM_KEYDOWN:
		wMemory_OnKeyDown(hWnd, wParam);
		break;
	case WM_KEYUP:
		break;
	case WM_DESTROY:
		wMemory_OnDestroy(hWnd);
		break;
	}
	return DefWindowProc(hWnd, uMsg, wParam, lParam);
}
