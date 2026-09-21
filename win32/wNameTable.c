#include "stdafx.h"
#include "i18n_ui.h"
#include "../comm/log.h"
#include "../core/nes.h"
#include "wNameTable.h"
#include "Resource.h"

extern RGBQUAD rgbQuard[MAX_COLORS];
extern ines_host_t   host;

static HWND wNT_hWnd = NULL;
static BOOL wNT_bShow = FALSE;
static const TCHAR wNT_szClassName[128] = _T("iNES_NTWnd");
static TCHAR wNT_szTitle[MAX_LOADSTRING];

// draw context
static int   wNT_iUpdate = 0;

// macros
#define   wNT_WIDTH    (SCREEN_WIDTH * 2)
#define   wNT_HEIGHT   (SCREEN_HEIGHT * 2)


static LRESULT CALLBACK	wNT_WindowProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam);

static ATOM wNT_RegisterClass(HINSTANCE  hInstance)
{
	WNDCLASSEX wcex;

	wcex.cbSize = sizeof(WNDCLASSEX);

	wcex.style			= CS_HREDRAW | CS_VREDRAW | CS_OWNDC;
	wcex.lpfnWndProc	= wNT_WindowProc;
	wcex.cbClsExtra		= 0;
	wcex.cbWndExtra		= 0;
	wcex.hInstance		= hInstance;
	wcex.hIcon			= NULL; // LoadIcon(hInstance, MAKEINTRESOURCE(IDI_INES));
	wcex.hCursor		= LoadCursor(NULL, IDC_ARROW);
	wcex.hbrBackground	= /* (HBRUSH)(COLOR_WINDOW+1)*/ (HBRUSH)GetStockObject(NULL_BRUSH);
	wcex.lpszMenuName	= NULL; // MAKEINTRESOURCE(IDC_INES);
	wcex.lpszClassName	= wNT_szClassName;
	wcex.hIconSm		= NULL; // LoadIcon(wcex.hInstance, MAKEINTRESOURCE(IDI_SMALL));

	return RegisterClassEx(&wcex);
}



/** 标题取自语言文件(view.name_table); 语言切换时重取。 */
static VOID wNT_UpdateTitle(VOID)
{
	ines_strncpy(wNT_szTitle, L10N("view.name_table"), count_of(wNT_szTitle) - 1);
	wNT_szTitle[count_of(wNT_szTitle) - 1] = 0;

	if(wNT_hWnd != NULL)
		SetWindowText(wNT_hWnd, wNT_szTitle);
}

BOOL wNT_Create(HINSTANCE hInstance, HWND hParentWnd)
{
	// already created
	if(wNT_hWnd != NULL)
	{
		/// SetParent(wNT_hWnd, hParentWnd);
		return TRUE;
	}

	wNT_UpdateTitle();

	wNT_RegisterClass(hInstance);
	 
	wNT_hWnd = CreateWindowEx(0&WS_EX_TOOLWINDOW, wNT_szClassName, wNT_szTitle, 
		WS_POPUPWINDOW|WS_CAPTION|WS_OVERLAPPED|WS_SIZEBOX|WS_MINIMIZEBOX,
		CW_USEDEFAULT, CW_USEDEFAULT, 512, 512, NULL, NULL, hInstance, NULL);



	return TRUE;
}


VOID wNT_Show(BOOL bShow)
{
	if(wNT_hWnd != NULL)
	{
		ShowWindow(wNT_hWnd, bShow ? SW_SHOWNORMAL : SW_HIDE);
		wNT_bShow = bShow;
		if(bShow)
		{
			SetForegroundWindow(wNT_hWnd);
		}
	}
}

BOOL wNT_IsShow()
{
	return wNT_bShow;
}

VOID wNT_SetUpdate()
{
	wNT_iUpdate = 1;
}

VOID wNT_Destroy()
{
	if(wNT_hWnd != NULL)
	{
		DestroyWindow(wNT_hWnd);
	}	
}


static VOID wNT_OnLButtonDown(HWND hWnd, int x, int y, DWORD dwKeyState)
{
}

static VOID wNT_OnKeyDown(HWND hWnd, UINT nKeyCode)
{
}


static VOID wNT_OnChar(HWND hWnd, UINT nChar)
{
}


static BOOL wNT_OnCreate(HWND hWnd, LPCREATESTRUCT  lpCreateStruct)
{
	RECT       rc;
	//HMENU      hMenu;

	SetRect(&rc, 0, 0, wNT_WIDTH, wNT_HEIGHT);
	AdjustWindowRect(&rc, GetWindowLong(hWnd, GWL_STYLE), GetMenu(hWnd) != NULL);

	SetWindowPos(hWnd, NULL, 0, 0, rc.right - rc.left, rc.bottom - rc.top, SWP_NOMOVE|SWP_NOZORDER|SWP_NOACTIVATE);


	SetTimer(hWnd, 1, 50, NULL);
	return TRUE;
}


static VOID wNT_OnDrawEx(HWND hWnd, HDC hDC, LPRECT lpClipRect)
{
	char           bm_head_buff[sizeof(BITMAPINFOHEADER) + 256*sizeof(RGBQUAD)];
	BYTE           bm_data[wNT_WIDTH * wNT_HEIGHT];
	RECT           rcClient;
	LPBITMAPINFO   p_bm_head;
	int            n, x, y, t;
	BYTE*          p_block;
	BYTE*          p_line, *p_pat;
	int            pa, pa_base;
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
	p_bm_head->bmiHeader.biWidth = wNT_WIDTH;
	p_bm_head->bmiHeader.biHeight = wNT_HEIGHT;
	p_bm_head->bmiHeader.biPlanes = 1;
	p_bm_head->bmiHeader.biBitCount = PIXEL_BITS;
	p_bm_head->bmiHeader.biCompression = BI_RGB;
	p_bm_head->bmiHeader.biSizeImage = wNT_WIDTH * wNT_HEIGHT;
	p_bm_head->bmiHeader.biXPelsPerMeter = 0;
	p_bm_head->bmiHeader.biYPelsPerMeter = 0;
	p_bm_head->bmiHeader.biClrUsed = MAX_COLORS;
	p_bm_head->bmiHeader.biClrImportant = 0;

	for(n = 0; n < 16; n++)
		p_bm_head->bmiColors[n] = rgbQuard[host.ppu.bg_pal[n]&0x3f];
	
	pa_base = (host.ppu.reg_ctrl_1 & PPU_BG_MEM_MASK) ? 0x1000 : 0;
	
	// fill bitmap data
	for(n = 0; n < 4; n++)   // for 4 name tables
	{
		// 0: x=0, y=SCREEN_HEIGHT;
		// 1: x=SCREEN_WIDTH, y=SCREEN_HEIGHT;
		// 2: x=0, Y=0;
		// 3: x=SCREEN_WIDTH, Y=0;
		//   START=y*wNT_WIDTH+x
		p_block = &bm_data[((1-(n>>1))*SCREEN_HEIGHT)*wNT_WIDTH+(n&1)*SCREEN_WIDTH];

		for(y = 0; y < 30; y++) // each line
		{
			for(x = 0; x < 32; x++)
			{
				pa = pa_base+ host.ppu.mem_bank[n+8][y * 32 + x] * 16 ;
				attr = ((host.ppu.mem_bank[n+8][0x3c0+y/4*8+x/4]>>((x&2)+(y&2)*2))&3)<<2;
				p_line = p_block + (SCREEN_HEIGHT-(y*8)-1)*wNT_WIDTH + x*8;
				p_pat = &host.ppu.mem_bank[pa>>10][pa&0x3ff];
				for(t = 0; t < 8; t++)
				{
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
					p_line -= wNT_WIDTH;
				}
			}
		}
	}

	GetClientRect(hWnd, &rcClient);
	StretchDIBits(hDC, 0, 0, rcClient.right - rcClient.left, rcClient.bottom - rcClient.top,
		0, 0, wNT_WIDTH, wNT_HEIGHT, bm_data, p_bm_head, DIB_RGB_COLORS, SRCCOPY);
}

static VOID wNT_OnDraw(HWND hWnd, HDC hPaintDC)
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

		wNT_OnDrawEx(hWnd, hMemDC, &rcClip);

		// Copy 
		BitBlt(hPaintDC, rcClip.left, rcClip.top, width, height, hMemDC, rcClip.left, rcClip.top, SRCCOPY);

		SelectObject(hMemDC, (HGDIOBJ)hOldBitmap);

	}
	else
	{
		wNT_OnDrawEx(hWnd, hPaintDC, &rcClip);
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

static VOID wNT_OnPaint(HWND hWnd)
{
	HDC     hPaintDC;
	PAINTSTRUCT   ps;
	hPaintDC = BeginPaint(hWnd, &ps);

	wNT_OnDraw(hWnd, hPaintDC);

	EndPaint(hWnd, &ps);
}


static VOID wNT_OnDestroy(HWND hWnd)
{
	KillTimer(hWnd, 1);
	wNT_hWnd = NULL;
	wNT_bShow = FALSE;
}



static LRESULT CALLBACK	wNT_WindowProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
	switch(uMsg)
	{
	case WM_CREATE:
		if(wNT_OnCreate(hWnd, (LPCREATESTRUCT)lParam))
			return 0;
		else
			return -1;
		break;
	case WM_CLOSE:
		DestroyWindow(hWnd);
		return 0;
		break;
	case WM_APP_LANGCHANGED:      /* 语言切换: 重取标题 */
		wNT_UpdateTitle();
		return 0;
	case WM_PAINT:
		wNT_OnPaint(hWnd);
		return 0;
	case WM_TIMER:
		if(wNT_iUpdate != 0)
		{
			wNT_iUpdate = 0;
			InvalidateRect(hWnd, NULL, FALSE);
		}
		break;
	case WM_SIZE:
		wNT_iUpdate = 1;
		break;
	case WM_LBUTTONDOWN:
		wNT_OnLButtonDown(hWnd, (short)LOWORD(lParam), (short)HIWORD(lParam), wParam);
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
		wNT_OnChar(hWnd, wParam);
		break;
	case WM_KEYDOWN:
		wNT_OnKeyDown(hWnd, wParam);
		break;
	case WM_KEYUP:
		break;
	case WM_DESTROY:
		wNT_OnDestroy(hWnd);
		break;
	}
	return DefWindowProc(hWnd, uMsg, wParam, lParam);
}
