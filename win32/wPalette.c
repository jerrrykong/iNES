#include "stdafx.h"
#include "i18n_ui.h"
#include "../comm/log.h"
#include "../core/nes.h"
#include "wPatternTable.h"
#include "Resource.h"

extern RGBQUAD rgbQuard[MAX_COLORS];
extern ines_host_t   host;

static HWND wPal_hWnd = NULL;
static BOOL wPal_bShow = FALSE;
static const TCHAR wPal_szClassName[128] = _T("iNES_PalWnd");
static TCHAR wPal_szTitle[MAX_LOADSTRING];

// draw context
static int   wPal_iUpdate = 0;

// macros
#define   wPal_WIDTH    (8*16)
#define   wPal_HEIGHT   (8*2)


static LRESULT CALLBACK	wPal_WindowProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam);

static ATOM wPal_RegisterClass(HINSTANCE  hInstance)
{
	WNDCLASSEX wcex;

	wcex.cbSize = sizeof(WNDCLASSEX);

	wcex.style			= CS_HREDRAW | CS_VREDRAW | CS_OWNDC;
	wcex.lpfnWndProc	= wPal_WindowProc;
	wcex.cbClsExtra		= 0;
	wcex.cbWndExtra		= 0;
	wcex.hInstance		= hInstance;
	wcex.hIcon			= NULL; // LoadIcon(hInstance, MAKEINTRESOURCE(IDI_INES));
	wcex.hCursor		= LoadCursor(NULL, IDC_ARROW);
	wcex.hbrBackground	= /* (HBRUSH)(COLOR_WINDOW+1)*/ (HBRUSH)GetStockObject(NULL_BRUSH);
	wcex.lpszMenuName	= NULL; // MAKEINTRESOURCE(IDC_INES);
	wcex.lpszClassName	= wPal_szClassName;
	wcex.hIconSm		= NULL; // LoadIcon(wcex.hInstance, MAKEINTRESOURCE(IDI_SMALL));

	return RegisterClassEx(&wcex);
}



/** 标题取自语言文件(view.palette); 语言切换时重取。 */
static VOID wPal_UpdateTitle(VOID)
{
	ines_strncpy(wPal_szTitle, L10N("view.palette"), count_of(wPal_szTitle) - 1);
	wPal_szTitle[count_of(wPal_szTitle) - 1] = 0;

	if(wPal_hWnd != NULL)
		SetWindowText(wPal_hWnd, wPal_szTitle);
}

BOOL wPal_Create(HINSTANCE hInstance, HWND hParentWnd)
{
	// already created
	if(wPal_hWnd != NULL)
	{
		/// SetParent(wPal_hWnd, hParentWnd);
		return TRUE;
	}

	wPal_UpdateTitle();

	wPal_RegisterClass(hInstance);
	 
	wPal_hWnd = CreateWindowEx(0&WS_EX_TOOLWINDOW, wPal_szClassName, wPal_szTitle, 
		WS_POPUPWINDOW|WS_CAPTION|WS_OVERLAPPED|WS_SIZEBOX|WS_MINIMIZEBOX,
		CW_USEDEFAULT, CW_USEDEFAULT, 512, 512, NULL, NULL, hInstance, NULL);



	return TRUE;
}


VOID wPal_Show(BOOL bShow)
{
	if(wPal_hWnd != NULL)
	{
		ShowWindow(wPal_hWnd, bShow ? SW_SHOWNORMAL : SW_HIDE);
		wPal_bShow = bShow;
		if(bShow)
		{
			SetForegroundWindow(wPal_hWnd);
		}
	}
}

BOOL wPal_IsShow()
{
	return wPal_bShow;
}

VOID wPal_SetUpdate()
{
	wPal_iUpdate = 1;
}

VOID wPal_Destroy()
{
	if(wPal_hWnd != NULL)
	{
		DestroyWindow(wPal_hWnd);
	}	
}



static VOID wPal_OnLButtonDown(HWND hWnd, int x, int y, DWORD dwKeyState)
{
}

static VOID wPal_OnKeyDown(HWND hWnd, UINT nKeyCode)
{
}


static VOID wPal_OnChar(HWND hWnd, UINT nChar)
{
}


static BOOL wPal_OnCreate(HWND hWnd, LPCREATESTRUCT  lpCreateStruct)
{
	RECT       rc;
	//HMENU      hMenu;

	SetRect(&rc, 0, 0, wPal_WIDTH, wPal_HEIGHT);
	AdjustWindowRect(&rc, GetWindowLong(hWnd, GWL_STYLE), GetMenu(hWnd) != NULL);

	SetWindowPos(hWnd, NULL, 0, 0, rc.right - rc.left, rc.bottom - rc.top, SWP_NOMOVE|SWP_NOZORDER|SWP_NOACTIVATE);
	
	SetTimer(hWnd, 1, 50, NULL);
	return TRUE;
}


static VOID wPal_OnDrawEx(HWND hWnd, HDC hDC, LPRECT lpClipRect)
{
	char           bm_head_buff[sizeof(BITMAPINFOHEADER) + 256*sizeof(RGBQUAD)];
	BYTE           bm_data[wPal_WIDTH * wPal_HEIGHT];
	RECT           rcClient;
	LPBITMAPINFO   p_bm_head;
	int            n, x, y, t;
	//BYTE*          p_block;
	BYTE*          p_line, *p_pal;
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
	p_bm_head->bmiHeader.biWidth = wPal_WIDTH;
	p_bm_head->bmiHeader.biHeight = wPal_HEIGHT;
	p_bm_head->bmiHeader.biPlanes = 1;
	p_bm_head->bmiHeader.biBitCount = PIXEL_BITS;
	p_bm_head->bmiHeader.biCompression = BI_RGB;
	p_bm_head->bmiHeader.biSizeImage = wPal_WIDTH * wPal_HEIGHT;
	p_bm_head->bmiHeader.biXPelsPerMeter = 0;
	p_bm_head->bmiHeader.biYPelsPerMeter = 0;
	p_bm_head->bmiHeader.biClrUsed = MAX_COLORS;
	p_bm_head->bmiHeader.biClrImportant = 0;

	for(n = 0; n < 64; n++)
		p_bm_head->bmiColors[n] = rgbQuard[n];
		
	// fill bitmap data

	for(y = 0; y < 2; y++) // each line
	{
		p_pal = (y==0)?host.ppu.bg_pal:host.ppu.sp_pal;
		for(x = 0; x < 16; x++)
		{
			p_line = bm_data + (wPal_HEIGHT-(y*8)-1)*wPal_WIDTH + x*8;
			for(t = 0; t < 8; t++)
			{
				p_line[0] = p_pal[x];
				p_line[1] = p_pal[x];
				p_line[2] = p_pal[x];
				p_line[3] = p_pal[x];
				p_line[4] = p_pal[x];
				p_line[5] = p_pal[x];
				p_line[6] = p_pal[x];
				p_line[7] = p_pal[x];
				p_line -= wPal_WIDTH;
			}
		}
	}

	GetClientRect(hWnd, &rcClient);
	StretchDIBits(hDC, 0, 0, rcClient.right - rcClient.left, rcClient.bottom - rcClient.top,
		0, 0, wPal_WIDTH, wPal_HEIGHT, bm_data, p_bm_head, DIB_RGB_COLORS, SRCCOPY);
}

static VOID wPal_OnDraw(HWND hWnd, HDC hPaintDC)
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

		wPal_OnDrawEx(hWnd, hMemDC, &rcClip);

		// Copy 
		BitBlt(hPaintDC, rcClip.left, rcClip.top, width, height, hMemDC, rcClip.left, rcClip.top, SRCCOPY);

		SelectObject(hMemDC, (HGDIOBJ)hOldBitmap);

	}
	else
	{
		wPal_OnDrawEx(hWnd, hPaintDC, &rcClip);
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

static VOID wPal_OnPaint(HWND hWnd)
{
	HDC     hPaintDC;
	PAINTSTRUCT   ps;
	hPaintDC = BeginPaint(hWnd, &ps);

	wPal_OnDraw(hWnd, hPaintDC);

	EndPaint(hWnd, &ps);
}


static VOID wPal_OnDestroy(HWND hWnd)
{
	KillTimer(hWnd, 1);
	wPal_hWnd = NULL;
	wPal_bShow = FALSE;
}



static LRESULT CALLBACK	wPal_WindowProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
	switch(uMsg)
	{
	case WM_CREATE:
		if(wPal_OnCreate(hWnd, (LPCREATESTRUCT)lParam))
			return 0;
		else
			return -1;
		break;
	case WM_CLOSE:
		DestroyWindow(hWnd);
		return 0;
		break;
	case WM_APP_LANGCHANGED:      /* 语言切换: 重取标题 */
		wPal_UpdateTitle();
		return 0;
	case WM_PAINT:
		wPal_OnPaint(hWnd);
		return 0;
	case WM_TIMER:
		if(wPal_iUpdate != 0)
		{
			wPal_iUpdate = 0;
			InvalidateRect(hWnd, NULL, FALSE);
		}
		break;
	case WM_SIZE:
		wPal_iUpdate = 1;
		break;
	case WM_LBUTTONDOWN:
		wPal_OnLButtonDown(hWnd, (short)LOWORD(lParam), (short)HIWORD(lParam), wParam);
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
		wPal_OnChar(hWnd, wParam);
		break;
	case WM_KEYDOWN:
		wPal_OnKeyDown(hWnd, wParam);
		break;
	case WM_KEYUP:
		break;
	case WM_DESTROY:
		wPal_OnDestroy(hWnd);
		break;
	}
	return DefWindowProc(hWnd, uMsg, wParam, lParam);
}
