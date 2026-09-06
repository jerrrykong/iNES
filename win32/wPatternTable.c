#include "stdafx.h"
#include "../comm/log.h"
#include "../core/nes.h"
#include "wPatternTable.h"
#include "Resource.h"

extern RGBQUAD rgbQuard[MAX_COLORS];
extern ines_host_t   host;

static HWND wPT_hWnd = NULL;
static BOOL wPT_bShow = FALSE;
static const TCHAR wPT_szClassName[128] = _T("iNES_PTWnd");
static TCHAR wPT_szTitle[MAX_LOADSTRING];

// draw context
static int   wPT_iUpdate = 0;
static int   wPT_PatIdx = 0;  // 0~7: BG0,BG1,BG2,BG3,SP0,SP1,SP2,SP3

// macros
#define   wPT_WIDTH    (8*16)
#define   wPT_HEIGHT   (8*32)


static LRESULT CALLBACK	wPT_WindowProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam);

static ATOM wPT_RegisterClass(HINSTANCE  hInstance)
{
	WNDCLASSEX wcex;

	wcex.cbSize = sizeof(WNDCLASSEX);

	wcex.style			= CS_HREDRAW | CS_VREDRAW | CS_OWNDC;
	wcex.lpfnWndProc	= wPT_WindowProc;
	wcex.cbClsExtra		= 0;
	wcex.cbWndExtra		= 0;
	wcex.hInstance		= hInstance;
	wcex.hIcon			= NULL; // LoadIcon(hInstance, MAKEINTRESOURCE(IDI_INES));
	wcex.hCursor		= LoadCursor(NULL, IDC_ARROW);
	wcex.hbrBackground	= /* (HBRUSH)(COLOR_WINDOW+1)*/ (HBRUSH)GetStockObject(NULL_BRUSH);
	wcex.lpszMenuName	= NULL; // MAKEINTRESOURCE(IDC_INES);
	wcex.lpszClassName	= wPT_szClassName;
	wcex.hIconSm		= NULL; // LoadIcon(wcex.hInstance, MAKEINTRESOURCE(IDI_SMALL));

	return RegisterClassEx(&wcex);
}



BOOL wPT_Create(HINSTANCE hInstance, HWND hParentWnd)
{
	// already created
	if(wPT_hWnd != NULL)
	{
		/// SetParent(wPT_hWnd, hParentWnd);
		return TRUE;
	}

	LoadString(hInstance, IDS_WND_PT_TITLE, wPT_szTitle, count_of(wPT_szTitle));

	wPT_RegisterClass(hInstance);
	 
	wPT_hWnd = CreateWindowEx(0&WS_EX_TOOLWINDOW, wPT_szClassName, wPT_szTitle, 
		WS_POPUPWINDOW|WS_CAPTION|WS_OVERLAPPED|WS_SIZEBOX|WS_MINIMIZEBOX,
		CW_USEDEFAULT, CW_USEDEFAULT, 512, 512, NULL, NULL, hInstance, NULL);



	return TRUE;
}


VOID wPT_Show(BOOL bShow)
{
	if(wPT_hWnd != NULL)
	{
		ShowWindow(wPT_hWnd, bShow ? SW_SHOWNORMAL : SW_HIDE);
		wPT_bShow = bShow;
		if(bShow)
		{
			SetForegroundWindow(wPT_hWnd);
		}
	}
}

BOOL wPT_IsShow()
{
	return wPT_bShow;
}

VOID wPT_SetUpdate()
{
	wPT_iUpdate = 1;
}

VOID wPT_Destroy()
{
	if(wPT_hWnd != NULL)
	{
		DestroyWindow(wPT_hWnd);
	}	
}

static VOID  wPT_UpdateTitle(HWND hWnd)
{
	TCHAR  szTitle[128];

	wsprintf(szTitle,  _T("%s(%s%d)"), wPT_szTitle, wPT_PatIdx&0x4?_T("SP"):_T("BG"), wPT_PatIdx&0x03);

	SetWindowText(hWnd, szTitle);
}


static VOID wPT_OnLButtonDown(HWND hWnd, int x, int y, DWORD dwKeyState)
{
	wPT_PatIdx = ((wPT_PatIdx + 1) & 0x07);
	wPT_UpdateTitle(hWnd);
	InvalidateRect(hWnd, NULL, TRUE);
}

static VOID wPT_OnKeyDown(HWND hWnd, UINT nKeyCode)
{
}


static VOID wPT_OnChar(HWND hWnd, UINT nChar)
{
}


static BOOL wPT_OnCreate(HWND hWnd, LPCREATESTRUCT  lpCreateStruct)
{
	RECT       rc;
	//HMENU      hMenu;

	SetRect(&rc, 0, 0, wPT_WIDTH, wPT_HEIGHT);
	AdjustWindowRect(&rc, GetWindowLong(hWnd, GWL_STYLE), GetMenu(hWnd) != NULL);

	SetWindowPos(hWnd, NULL, 0, 0, rc.right - rc.left, rc.bottom - rc.top, SWP_NOMOVE|SWP_NOZORDER|SWP_NOACTIVATE);
	
	wPT_UpdateTitle(hWnd);

	SetTimer(hWnd, 1, 50, NULL);
	return TRUE;
}


static VOID wPT_OnDrawEx(HWND hWnd, HDC hDC, LPRECT lpClipRect)
{
	char           bm_head_buff[sizeof(BITMAPINFOHEADER) + 256*sizeof(RGBQUAD)];
	BYTE           bm_data[wPT_WIDTH * wPT_HEIGHT];
	RECT           rcClient;
	LPBITMAPINFO   p_bm_head;
	int            n, x, y, t;
	//BYTE*          p_block;
	BYTE*          p_line, *p_pat;
	int            pa;
	int            attr, pat;
	HBRUSH         hBS;



	if(host.status == NES_STATUS_OFF)
	{
		GetClientRect(hWnd, &rcClient);
		hBS = CreateSolidBrush(RGB(128,128,128));
		FillRect(hDC, &rcClient, hBS);
		return;
	}


	p_bm_head = (LPBITMAPINFO)bm_head_buff;

	// fill bmp header
	memset(bm_head_buff, 0, sizeof(bm_head_buff));
	p_bm_head->bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
	p_bm_head->bmiHeader.biWidth = wPT_WIDTH;
	p_bm_head->bmiHeader.biHeight = wPT_HEIGHT;
	p_bm_head->bmiHeader.biPlanes = 1;
	p_bm_head->bmiHeader.biBitCount = PIXEL_BITS;
	p_bm_head->bmiHeader.biCompression = BI_RGB;
	p_bm_head->bmiHeader.biSizeImage = wPT_WIDTH * wPT_HEIGHT;
	p_bm_head->bmiHeader.biXPelsPerMeter = 0;
	p_bm_head->bmiHeader.biYPelsPerMeter = 0;
	p_bm_head->bmiHeader.biClrUsed = MAX_COLORS;
	p_bm_head->bmiHeader.biClrImportant = 0;

	for(n = 0; n < 4; n++)
		p_bm_head->bmiColors[n] = rgbQuard[(wPT_PatIdx&0x04?host.ppu.sp_pal[(wPT_PatIdx&0x03)*4+n]:host.ppu.bg_pal[(wPT_PatIdx&0x03)*4+n])&0x3f];
	
	// pa_base = (host.ppu.reg_ctrl_1 & PPU_BG_MEM_MASK) ? 0x1000 : 0;
	
	// fill bitmap data

	for(y = 0; y < 32; y++) // each line
	{
		for(x = 0; x < 16; x++)
		{
			pa = (y * 16 + x) * 16;
			p_line = bm_data + (wPT_HEIGHT-(y*8)-1)*wPT_WIDTH + x*8;
			p_pat = &host.ppu.mem_bank[pa>>10][pa&0x3ff];
			for(t = 0; t < 8; t++)
			{
				attr = 0;
				pat = p_pat[t] | (int)p_pat[t+8]<<8;
				// h7 h6 h5 h4 h3 h2 h1 h0 l7 l6 l5 l4 l3 l2 l1 l0
				// h7    h5    h3    h1       l6    l4    l2    l0
				// l7    l5    l3    l1
				// h6    h4    h2    h0 
				// h7 l7 h5 l5 h3 l3 h1 l1 h6 l6 h4 l4 h2 l2 h0 l0	
				pat = (pat & 0xaa55) | ((pat & 0x00aa)<<7) | ((pat & 0x5500) >> 7);
				p_line[0] = ((pat>>14)&0x3)|attr;
				p_line[1] = ((pat>>6)&0x3)|attr;
				p_line[2] = ((pat>>12)&0x3)|attr;
				p_line[3] = ((pat>>4)&0x3)|attr;
				p_line[4] = ((pat>>10)&0x3)|attr;
				p_line[5] = ((pat>>2)&0x3)|attr;
				p_line[6] = ((pat>>8)&0x3)|attr;
				p_line[7] = (pat&0x3)|attr;
				p_line -= wPT_WIDTH;
			}
		}
	}

	GetClientRect(hWnd, &rcClient);
	StretchDIBits(hDC, 0, 0, rcClient.right - rcClient.left, rcClient.bottom - rcClient.top,
		0, 0, wPT_WIDTH, wPT_HEIGHT, bm_data, p_bm_head, DIB_RGB_COLORS, SRCCOPY);
}

static VOID wPT_OnDraw(HWND hWnd, HDC hPaintDC)
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

	//hMemDC = CreateCompatibleDC(hPaintDC);
	//hMemBitmap = CreateCompatibleBitmap(hPaintDC, width, height);
	hOldBitmap = NULL;

	GetClipBox(hPaintDC, &rcClip);


	if(hMemDC && hMemBitmap)
	{
		hOldBitmap = (HBITMAP)SelectObject(hMemDC, (HGDIOBJ)hMemBitmap);
		hClipRgn = CreateRectRgnIndirect(&rcClip);
		SelectClipRgn(hMemDC, hClipRgn);

		wPT_OnDrawEx(hWnd, hMemDC, &rcClip);

		// Copy 
		BitBlt(hPaintDC, rcClip.left, rcClip.top, width, height, hMemDC, rcClip.left, rcClip.top, SRCCOPY);

		SelectObject(hMemDC, (HGDIOBJ)hOldBitmap);

	}
	else
	{
		wPT_OnDrawEx(hWnd, hPaintDC, &rcClip);
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

static VOID wPT_OnPaint(HWND hWnd)
{
	HDC     hPaintDC;
	PAINTSTRUCT   ps;
	hPaintDC = BeginPaint(hWnd, &ps);

	wPT_OnDraw(hWnd, hPaintDC);

	EndPaint(hWnd, &ps);
}


static VOID wPT_OnDestroy(HWND hWnd)
{
	KillTimer(hWnd, 1);
	wPT_hWnd = NULL;
	wPT_bShow = FALSE;
}



static LRESULT CALLBACK	wPT_WindowProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
	switch(uMsg)
	{
	case WM_CREATE:
		if(wPT_OnCreate(hWnd, (LPCREATESTRUCT)lParam))
			return 0;
		else
			return -1;
		break;
	case WM_CLOSE:
		DestroyWindow(hWnd);
		return 0;
		break;
	case WM_PAINT:
		wPT_OnPaint(hWnd);
		return 0;
	case WM_TIMER:
		if(wPT_iUpdate != 0)
		{
			wPT_iUpdate = 0;
			InvalidateRect(hWnd, NULL, FALSE);
		}
		break;
	case WM_SIZE:
		wPT_iUpdate = 1;
		break;
	case WM_LBUTTONDOWN:
		wPT_OnLButtonDown(hWnd, (short)LOWORD(lParam), (short)HIWORD(lParam), wParam);
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
	case WM_CHAR:
		wPT_OnChar(hWnd, wParam);
		break;
	case WM_KEYDOWN:
		wPT_OnKeyDown(hWnd, wParam);
		break;
	case WM_KEYUP:
		break;
	case WM_DESTROY:
		wPT_OnDestroy(hWnd);
		break;
	}
	return DefWindowProc(hWnd, uMsg, wParam, lParam);
}
