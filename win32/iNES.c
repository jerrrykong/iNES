// iNES.cpp : 定义应用程序的入口点。
//

#include "stdafx.h"
#include "iNES.h"
#include "../comm/log.h"
#include "../core/nes.h"
#include <sys/stat.h>
#include <direct.h>
#include "wMemory.h"
#include "wVMemory.h"
#include "wSPMemory.h"
#include "wNameTable.h"
#include "wPatternTable.h"
#include "wPalette.h"
#include "wRegister.h"
#include "dlgLanMatch.h"
#include "dlgNetPlay.h"
#include "dlgOpenRom.h"
#include "../comm/net.h"
#include "../comm/npsession.h"

// 模拟器全局变量
ines_host_t   host;

int channel_enabled[5] = {1,1,1,1,1};

RGBQUAD rgbQuard[MAX_COLORS] =
{	
	{0x7F, 0x7F, 0x7F, 0}, {0xB0, 0x00, 0x20, 0}, {0xB8, 0x00, 0x28, 0}, {0xA0, 0x10, 0x60, 0},
	{0x78, 0x20, 0x98, 0}, {0x30, 0x10, 0xB0, 0}, {0x00, 0x30, 0xA0, 0}, {0x00, 0x40, 0x78, 0},
	{0x00, 0x58, 0x48, 0}, {0x00, 0x68, 0x38, 0}, {0x00, 0x6C, 0x38, 0}, {0x40, 0x60, 0x30, 0},
	{0x80, 0x50, 0x30, 0}, {0x00, 0x00, 0x00, 0}, {0x00, 0x00, 0x00, 0}, {0x00, 0x00, 0x00, 0},

	{0xBC, 0xBC, 0xBC, 0}, {0xF8, 0x60, 0x40, 0}, {0xFF, 0x40, 0x40, 0}, {0xF0, 0x40, 0x90, 0},
	{0xC0, 0x40, 0xD8, 0}, {0x60, 0x40, 0xD8, 0}, {0x00, 0x50, 0xE0, 0}, {0x00, 0x70, 0xC0, 0},
	{0x00, 0x88, 0x88, 0}, {0x00, 0xA0, 0x50, 0}, {0x10, 0xA8, 0x48, 0}, {0x68, 0xA0, 0x48, 0},
	{0xC0, 0x90, 0x40, 0}, {0x00, 0x00, 0x00, 0}, {0x00, 0x00, 0x00, 0}, {0x00, 0x00, 0x00, 0},

	{0xFF, 0xFF, 0xFF, 0}, {0xFF, 0xA0, 0x60, 0}, {0xFF, 0x80, 0x50, 0}, {0xFF, 0x70, 0xA0, 0},
	{0xFF, 0x60, 0xF0, 0}, {0xB0, 0x60, 0xFF, 0}, {0x30, 0x78, 0xFF, 0}, {0x00, 0xA0, 0xFF, 0},
	{0x20, 0xD0, 0xE8, 0}, {0x00, 0xE8, 0x98, 0}, {0x40, 0xF0, 0x70, 0}, {0x90, 0xE0, 0x70, 0},
	{0xE0, 0xD0, 0x60, 0}, {0x60, 0x60, 0x60, 0}, {0x00, 0x00, 0x00, 0}, {0x00, 0x00, 0x00, 0},

	{0xFF, 0xFF, 0xFF, 0}, {0xFF, 0xD0, 0x90, 0}, {0xFF, 0xB8, 0xA0, 0}, {0xFF, 0xB0, 0xC0, 0},
	{0xFF, 0xB0, 0xE0, 0}, {0xE8, 0xB8, 0xFF, 0}, {0xB8, 0xC8, 0xFF, 0}, {0xA0, 0xD8, 0xFF, 0},
	{0x90, 0xF0, 0xFF, 0}, {0x80, 0xF0, 0xC8, 0}, {0xA0, 0xF0, 0xA0, 0}, {0xC8, 0xFF, 0xA0, 0},
	{0xF0, 0xFF, 0xA0, 0}, {0xA0, 0xA0, 0xA0, 0}, {0x00, 0x00, 0x00, 0}, {0x00, 0x00, 0x00, 0}
};


// standard ascii 5x7 font    纵向取模
// defines ascii characters 0x20-0x7f (32-127)
ines_byte_t code_ascii_5x7[] = {
	0x00, 0x00, 0x00, 0x00, 0x00,// ' '
	0x00, 0x00, 0x5F, 0x00, 0x00,// !
	0x00, 0x07, 0x00, 0x07, 0x00,// "
	0x14, 0x7F, 0x14, 0x7F, 0x14,// #
	0x24, 0x2A, 0x07, 0x2A, 0x12,// $
	0x23, 0x13, 0x08, 0x64, 0x62,// %
	0x37, 0x49, 0x55, 0x22, 0x50,// &
	0x00, 0x05, 0x03, 0x00, 0x00,// '
	0x00, 0x1C, 0x22, 0x41, 0x00,// (
	0x00, 0x41, 0x22, 0x1C, 0x00,// )
	0x08, 0x2A, 0x1C, 0x2A, 0x08,// *
	0x08, 0x08, 0x3E, 0x08, 0x08,// +
	0x00, 0x50, 0x30, 0x00, 0x00,// ,
	0x08, 0x08, 0x08, 0x08, 0x08,// -
	0x00, 0x60, 0x60, 0x00, 0x00,// .
	0x20, 0x10, 0x08, 0x04, 0x02,// /
	0x3E, 0x51, 0x49, 0x45, 0x3E,// 0
	0x00, 0x42, 0x7F, 0x40, 0x00,// 1
	0x42, 0x61, 0x51, 0x49, 0x46,// 2
	0x21, 0x41, 0x45, 0x4B, 0x31,// 3
	0x18, 0x14, 0x12, 0x7F, 0x10,// 4
	0x27, 0x45, 0x45, 0x45, 0x39,// 5
	0x3C, 0x4A, 0x49, 0x49, 0x30,// 6
	0x01, 0x71, 0x09, 0x05, 0x03,// 7
	0x36, 0x49, 0x49, 0x49, 0x36,// 8
	0x06, 0x49, 0x49, 0x29, 0x1E,// 9
	0x00, 0x36, 0x36, 0x00, 0x00,// :
	0x00, 0x56, 0x36, 0x00, 0x00,// ;
	0x00, 0x08, 0x14, 0x22, 0x41,// <
	0x14, 0x14, 0x14, 0x14, 0x14,// =
	0x41, 0x22, 0x14, 0x08, 0x00,// >
	0x02, 0x01, 0x51, 0x09, 0x06,// ?
	0x32, 0x49, 0x79, 0x41, 0x3E,// @
	0x7E, 0x11, 0x11, 0x11, 0x7E,// A
	0x7F, 0x49, 0x49, 0x49, 0x36,// B
	0x3E, 0x41, 0x41, 0x41, 0x22,// C
	0x7F, 0x41, 0x41, 0x22, 0x1C,// D
	0x7F, 0x49, 0x49, 0x49, 0x41,// E
	0x7F, 0x09, 0x09, 0x01, 0x01,// F
	0x3E, 0x41, 0x41, 0x51, 0x32,// G
	0x7F, 0x08, 0x08, 0x08, 0x7F,// H
	0x00, 0x41, 0x7F, 0x41, 0x00,// I
	0x20, 0x40, 0x41, 0x3F, 0x01,// J
	0x7F, 0x08, 0x14, 0x22, 0x41,// K
	0x7F, 0x40, 0x40, 0x40, 0x40,// L
	0x7F, 0x02, 0x04, 0x02, 0x7F,// M
	0x7F, 0x04, 0x08, 0x10, 0x7F,// N
	0x3E, 0x41, 0x41, 0x41, 0x3E,// O
	0x7F, 0x09, 0x09, 0x09, 0x06,// P
	0x3E, 0x41, 0x51, 0x21, 0x5E,// Q
	0x7F, 0x09, 0x19, 0x29, 0x46,// R
	0x46, 0x49, 0x49, 0x49, 0x31,// S
	0x01, 0x01, 0x7F, 0x01, 0x01,// T
	0x3F, 0x40, 0x40, 0x40, 0x3F,// U
	0x1F, 0x20, 0x40, 0x20, 0x1F,// V
	0x7F, 0x20, 0x18, 0x20, 0x7F,// W
	0x63, 0x14, 0x08, 0x14, 0x63,// X
	0x03, 0x04, 0x78, 0x04, 0x03,// Y
	0x61, 0x51, 0x49, 0x45, 0x43,// Z
	0x00, 0x00, 0x7F, 0x41, 0x41,// [
	0x02, 0x04, 0x08, 0x10, 0x20,// "\"
	0x41, 0x41, 0x7F, 0x00, 0x00,// ]
	0x04, 0x02, 0x01, 0x02, 0x04,// ^
	0x40, 0x40, 0x40, 0x40, 0x40,// _
	0x00, 0x01, 0x02, 0x04, 0x00,// `
	0x20, 0x54, 0x54, 0x54, 0x78,// a
	0x7F, 0x48, 0x44, 0x44, 0x38,// b
	0x38, 0x44, 0x44, 0x44, 0x20,// c
	0x38, 0x44, 0x44, 0x48, 0x7F,// d
	0x38, 0x54, 0x54, 0x54, 0x18,// e
	0x08, 0x7E, 0x09, 0x01, 0x02,// f
	0x08, 0x14, 0x54, 0x54, 0x3C,// g
	0x7F, 0x08, 0x04, 0x04, 0x78,// h
	0x00, 0x44, 0x7D, 0x40, 0x00,// i
	0x20, 0x40, 0x44, 0x3D, 0x00,// j
	0x00, 0x7F, 0x10, 0x28, 0x44,// k
	0x00, 0x41, 0x7F, 0x40, 0x00,// l
	0x7C, 0x04, 0x18, 0x04, 0x78,// m
	0x7C, 0x08, 0x04, 0x04, 0x78,// n
	0x38, 0x44, 0x44, 0x44, 0x38,// o
	0x7C, 0x14, 0x14, 0x14, 0x08,// p
	0x08, 0x14, 0x14, 0x18, 0x7C,// q
	0x7C, 0x08, 0x04, 0x04, 0x08,// r
	0x48, 0x54, 0x54, 0x54, 0x20,// s
	0x04, 0x3F, 0x44, 0x40, 0x20,// t
	0x3C, 0x40, 0x40, 0x20, 0x7C,// u
	0x1C, 0x20, 0x40, 0x20, 0x1C,// v
	0x3C, 0x40, 0x30, 0x40, 0x3C,// w
	0x44, 0x28, 0x10, 0x28, 0x44,// x
	0x0C, 0x50, 0x50, 0x50, 0x3C,// y
	0x44, 0x64, 0x54, 0x4C, 0x44,// z
	0x00, 0x08, 0x36, 0x41, 0x00,// {
	0x00, 0x00, 0x7F, 0x00, 0x00,// |
	0x00, 0x41, 0x36, 0x08, 0x00,// }
	0x02, 0x01, 0x02, 0x04, 0x02,// ~
	0xff, 0xff, 0xff, 0xff, 0xff, //black block
};

ines_byte_t     screen_buffer[2][SCREEN_IMAGE_BYTES];
ines_byte_t     bmp_info_buffer[sizeof(BITMAPINFOHEADER) + sizeof(RGBQUAD) * MAX_COLORS];
BITMAPINFO*     bmp_info = (BITMAPINFO*)bmp_info_buffer;
ines_byte_t*    screen_front = screen_buffer[0];    // 当前正在显示的BMP
ines_byte_t*    screen_back = screen_buffer[1];     // 后台正在渲染的BUFFFER
ines_int_t      nes_cpu_rate;
extern ines_int_t      nes_cpu_trace_ops;
ines_int_t      screen_scale = 200; // % percent

ines_int_t      key_flash_freq  = 6; // 
ines_int_t      key_flash_count = 0; // up to n frames
ines_int_t      main_key_state = 0;
ines_int_t      second_key_state = 0;
ines_int_t      ctrl_key_state = 0;
ines_int_t      sync_ctrl_req  = 0;   // 联机读档失败后待提交的控制码(硬复位), 见 OnIdleSyncState
ines_char_t     lastest_open_files[10][1024]; // 最近打开的10个文件

HWND   hMainWnd = NULL;

int    run_in_background = 1;

int    audio_volume = 100; // 0~100
int    audio_mute = 0;


LARGE_INTEGER  llCpuCounterFreq;
LARGE_INTEGER  llCpuStartCounter;
LARGE_INTEGER  llCpuLastCounter;


double  dblLastRateSecond = 0.0;
double  dblLastFrameSecond = 0.0;


DWORD   dwFrameLastTime = 0;

ines_int_t   pause_flag = 0;

ines_int_t   is_net_play = 0;
// 帧缓冲由 comm/npsession 持有(与 macOS 端同一份实现), 这里不再单独维护缓存数组



enum { BUFFER_FREE,  BUFFER_PLAYING, BUFFER_DONE };

typedef struct _WaveOutBuffer {
	BYTE       szData[NES_AUDIO_BYTES_PER_SECOND / 10]; // 500ms
	WAVEHDR    wvhdr;
	DWORD      dwFlag;
} WAVEOUTBUFFER;


#define MAX_BUF_NUM   10
WAVEOUTBUFFER   wvBuffer[MAX_BUF_NUM]; // 双缓冲
int             wvPlayingNum = 0;

int             audio_cache_num = 4;

HWAVEOUT    hwvOut = NULL;


WAVEOUTBUFFER* cur_wave_buffer = NULL;

// 全局变量:
HINSTANCE hInst;								// 当前实例
TCHAR szTitle[MAX_LOADSTRING];					// 标题栏文本
TCHAR szWindowClass[MAX_LOADSTRING];			// 主窗口类名

TCHAR szROMFilePath[INES_MAX_PATH]; // 装载的ROM文件路径
TCHAR szROMTitle[INES_MAX_PATH]; // ROM 的标题
TCHAR szRAMFilePath[INES_MAX_PATH]; // 关联的 RAM文件的路径

TCHAR szPrivateProfilePath[INES_MAX_PATH];

// 此代码模块中包含的函数的前向声明:
ATOM				MyRegisterClass(HINSTANCE hInstance);
BOOL				InitInstance(HINSTANCE, int);
LRESULT CALLBACK	WndProc(HWND, UINT, WPARAM, LPARAM);
INT_PTR CALLBACK	About(HWND, UINT, WPARAM, LPARAM);
VOID                OnIdle();
VOID                OnPaint(HWND hWnd, HDC  hDC);
VOID                Cleanup();
VOID                UpdateAllViews();
int                 NesGetKeyState(int vk);
LONGLONG            GetNESCPUCycles(void);


ines_cstr_t getSavePath( ines_str_t szPath, size_t szLen);
ines_cstr_t getStatePath(int index, ines_str_t szPath, size_t szLen);
ines_cstr_t getRelativeFilePath( ines_str_t szPath, size_t szLen, ines_cstr_t  fileName);

// 联机读档(状态同步): 主机发起, 两端载入同一份存档字节流
VOID            OnMenuSyncState(int index);
int             OnIdleSyncState(void);
static int      ines_state_load_mem(const void* buf, int len, void* user);
static int      ines_state_sign(ines_dword_t* out_sign, void* user);

//  profile r/w
#define  GET_CONFIG_STR(sec, key, def)  GetConfigStr((sec), (key), (def))

ines_cstr_t  GetConfigStr(ines_cstr_t sec, ines_cstr_t key, ines_cstr_t  def);
ines_int_t   GetConfigInt(ines_cstr_t sec, ines_cstr_t key, ines_int_t   def);
void         SetConfigStr(ines_cstr_t sec, ines_cstr_t key, ines_cstr_t  val);
void         SetConfigInt(ines_cstr_t sec, ines_cstr_t key, ines_int_t   val);


// history files
void LoadHistories();
void SaveHistories();
void  AddHistoryFile(ines_cstr_t  file);

BOOL NesOpenFile(LPCTSTR lpszFileName);


time_t       GetSaveStateTime(int index);
LRESULT   OnInitMenuPopup(HWND hWnd, HMENU hMenuPopup, UINT nPos, BOOL bSystem);
LRESULT   OnSizing(HWND hWnd, UINT nSide, LPRECT lpRect);
LRESULT   OnSize(HWND hWnd, UINT nSizeType, int cx, int cy);
LRESULT   OnMove(HWND hWnd, int xPos, int yPos);
LRESULT   OnDropFiles(HWND hWnd, HDROP hDrop);

void CALLBACK waveOutCallback(  HWAVEOUT hwo,    
							  UINT uMsg,          
							  DWORD_PTR dwInstance,    
							  DWORD_PTR dwParam1,      
							  DWORD_PTR dwParam2);

void DrawTextToBitmap(ines_byte_t* bits, ines_int_t iWidth, ines_int_t iHeight, 
					  ines_cstr_t szText, ines_int_t x, ines_int_t y, ines_byte_t clText);

VOID UpdateTitle();

// 联网对战: 结束对局前的二次确认 / 通知对端后收尾(实现见文件末尾, WndProc 与菜单均会调用)
BOOL ConfirmStopNetPlay(HWND hWnd);
VOID NotifyPeerQuitAndEnd(VOID);

VOID OnMenuOpen();
VOID OnMenuClose();
VOID OnMenuSoftReset();
VOID OnMenuHardReset();
VOID OnMenuLogLevel(ines_int_t  level);
VOID OnMenuCPUTrace();
VOID OnMenuPause();
VOID OnMenuFrameStep();
VOID OnMenuScale(ines_int_t  scale);
VOID OnMenuOptions();

VOID OnMenuMute();
VOID OnMenuVolumn(int volumn);

VOID OnMenuFullScreen();
VOID OnMenuSnapshot();
VOID OnMenuSaveState(int index);
VOID OnMenuLoadState(int index);



VOID UpdateMenuSaveState(HMENU hMenu, UINT nPos, int index);
VOID UpdateMenuLoadState(HMENU hMenu, UINT nPos, int index);



void send_ctrl(ines_byte_t  code, ines_word_t flag, ines_int64_t fno);
// 帧收发与帧缓存已下沉到 comm/npsession(np_frame_begin/np_input_ready/np_frame_input),
// 原 send_frame / recv_frame / cache_add_* / cache_get 不再在此声明。


int APIENTRY _tWinMain(HINSTANCE hInstance,
                     HINSTANCE hPrevInstance,
                     LPTSTR    lpCmdLine,
                     int       nCmdShow)
{
	MSG msg;
	HACCEL hAccelTable;

	UNREFERENCED_PARAMETER(hPrevInstance);
	UNREFERENCED_PARAMETER(lpCmdLine);

 	// TODO: 在此放置代码。
	_tsetlocale(LC_CTYPE, _T(""));

	ines_set_log_stamp_func(GetNESCPUCycles);

	// 初始化全局字符串
	LoadString(hInstance, IDS_APP_TITLE, szTitle, MAX_LOADSTRING);
	LoadString(hInstance, IDC_INES, szWindowClass, MAX_LOADSTRING);


	getRelativeFilePath(szPrivateProfilePath, count_of(szPrivateProfilePath), ISTR("config.ini"));

	// 加载配置
	audio_volume = GetConfigInt(ISTR("audio"), ISTR("volume"), 80);
	audio_mute   = GetConfigInt(ISTR("audio"), ISTR("mute"), 0);

	// 联机读档: 注册"从内存载入存档"与"算状态摘要"回调。
	// 主机和从机都要注册 —— 从机收到存档后同样要载入并回摘要。
	np_sync_set_handler(ines_state_load_mem, ines_state_sign, NULL);

	// 最近打开的文件
	LoadHistories();

	MyRegisterClass(hInstance);

	// 执行应用程序初始化:
	if (!InitInstance (hInstance, nCmdShow))
	{
		return FALSE;
	}

	if(!QueryPerformanceFrequency(&llCpuCounterFreq)
		|| !QueryPerformanceCounter(&llCpuStartCounter))
		return FALSE;

	llCpuLastCounter = llCpuStartCounter;



	timeBeginPeriod(1);


	ines_host_init(&host, 1);

	//ines_host_init_setting(&host, 1);


	UpdateTitle();


	hAccelTable = LoadAccelerators(hInstance, MAKEINTRESOURCE(IDC_INES));

	// 主消息循环:
	while ( TRUE )
	{
		if(PeekMessage(&msg, NULL, 0, 0, PM_REMOVE))
		{
			if(msg.message == WM_QUIT)
				break;

			if (!TranslateAccelerator(msg.hwnd, hAccelTable, &msg))
			{
				TranslateMessage(&msg);
				DispatchMessage(&msg);
			}

		}
		else
		{
			OnIdle();
		}
	}

	ines_host_free(&host);

	timeEndPeriod(1);

	Cleanup();

	INES_LOG(LOG_NTY, MOD_SYS, ISTR("iNES Shutdown OK.\n"));


	return (int) msg.wParam;
}



//
//  函数: MyRegisterClass()
//
//  目的: 注册窗口类。
//
//  注释:
//
//    仅当希望
//    此代码与添加到 Windows 95 中的“RegisterClassEx”
//    函数之前的 Win32 系统兼容时，才需要此函数及其用法。调用此函数十分重要，
//    这样应用程序就可以获得关联的
//    “格式正确的”小图标。
//
ATOM MyRegisterClass(HINSTANCE hInstance)
{
	WNDCLASSEX wcex;

	wcex.cbSize = sizeof(WNDCLASSEX);

	wcex.style			= CS_HREDRAW | CS_VREDRAW;
	wcex.lpfnWndProc	= WndProc;
	wcex.cbClsExtra		= 0;
	wcex.cbWndExtra		= 0;
	wcex.hInstance		= hInstance;
	wcex.hIcon			= LoadIcon(hInstance, MAKEINTRESOURCE(IDI_INES));
	wcex.hCursor		= LoadCursor(NULL, IDC_ARROW);
	wcex.hbrBackground	= /* (HBRUSH)(COLOR_WINDOW+1)*/ (HBRUSH)GetStockObject(NULL_BRUSH);
	wcex.lpszMenuName	= MAKEINTRESOURCE(IDC_INES);
	wcex.lpszClassName	= szWindowClass;
	wcex.hIconSm		= LoadIcon(wcex.hInstance, MAKEINTRESOURCE(IDI_SMALL));

	return RegisterClassEx(&wcex);
}

//
//   函数: InitInstance(HINSTANCE, int)
//
//   目的: 保存实例句柄并创建主窗口
//
//   注释:
//
//        在此函数中，我们在全局变量中保存实例句柄并0
//        创建和显示主程序窗口。
//
BOOL InitInstance(HINSTANCE hInstance, int nCmdShow)
{
   HWND hWnd;
   //RECT rc = { 0, 0, 512, 480};
   MMRESULT   hRet = 0;
   WAVEFORMATEX   wfmt;
   int i, x, y;

   hInst = hInstance; // 将实例句柄存储在全局变量中

   INES_LOG(LOG_NTY, MOD_SYS, ISTR("=========================================================\n"));
#ifdef _DEBUG
   INES_LOG(LOG_NTY, MOD_SYS, ISTR("iNES v1.0(Debug), Build Time ")ISTR(__TIMESTAMP__)ISTR(", Initializing...\n"));
#else
   INES_LOG(LOG_NTY, MOD_SYS, ISTR("iNES v1.0, Build Time ")ISTR(__TIMESTAMP__)ISTR(", Initializing...\n"));
#endif


   // init socket library
   np_init();


   x = GetConfigInt(ISTR("display"), ISTR("x"), CW_USEDEFAULT);
   y = GetConfigInt(ISTR("display"), ISTR("y"), CW_USEDEFAULT);

   hWnd = CreateWindow(szWindowClass, szTitle, WS_OVERLAPPEDWINDOW,
      x, y, CW_USEDEFAULT, CW_USEDEFAULT, NULL, NULL, hInstance, NULL);

   if (!hWnd)
   {
      return FALSE;
   }
	
   
/*
	wMemory_Create(hInstance, hWnd);
	wMemory_Show(TRUE);
	wVMemory_Create(hInstance, hWnd);
	wVMemory_Show(TRUE);
	wSPMemory_Create(hInstance, hWnd);
	wSPMemory_Show(TRUE);
	wNT_Create(hInstance, hWnd);
	wNT_Show(TRUE);
*/
   // init audio
   memset(&wfmt, 0, sizeof(wfmt));
   wfmt.cbSize = sizeof(wfmt);
   wfmt.wFormatTag = WAVE_FORMAT_PCM;
   wfmt.nChannels = NES_AUDIO_CHANNEL;
   wfmt.wBitsPerSample = NES_AUDIO_SAMPLE_BITS;
   wfmt.nSamplesPerSec = NES_AUDIO_SAMPLE_RATE;
   wfmt.nBlockAlign = 1;
   wfmt.nAvgBytesPerSec = NES_AUDIO_BYTES_PER_SECOND;

   hRet = waveOutOpen(&hwvOut, WAVE_MAPPER, &wfmt, (DWORD_PTR)hWnd, 0, CALLBACK_WINDOW);

   if(hRet!= 0)
   {
	   return FALSE;
   }

   for(i = 0; i < MAX_BUF_NUM; i++)
   {
	   wvBuffer[i].dwFlag = BUFFER_FREE;
   }


	// init vedio
	bmp_info = (BITMAPINFO*)bmp_info_buffer;
	memset(bmp_info_buffer, 0, sizeof(bmp_info_buffer));
	bmp_info->bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
	bmp_info->bmiHeader.biWidth = SCREEN_WIDTH;
	bmp_info->bmiHeader.biHeight = SCREEN_HEIGHT;
	bmp_info->bmiHeader.biPlanes = 1;
	bmp_info->bmiHeader.biBitCount = PIXEL_BITS;
	bmp_info->bmiHeader.biCompression = BI_RGB;
	bmp_info->bmiHeader.biSizeImage = SCREEN_PIXELS;
	bmp_info->bmiHeader.biXPelsPerMeter = 0;
	bmp_info->bmiHeader.biYPelsPerMeter = 0;
	bmp_info->bmiHeader.biClrUsed = MAX_COLORS;
	bmp_info->bmiHeader.biClrImportant = 0;

	memcpy(bmp_info->bmiColors, rgbQuard, sizeof(RGBQUAD) * MAX_COLORS);


	screen_front = screen_buffer[0];
	screen_back = screen_buffer[1];

	memset(screen_front, 0, SCREEN_IMAGE_BYTES);
	memset(screen_back, 0, SCREEN_IMAGE_BYTES);

	nes_cpu_rate = 0;

   hMainWnd = hWnd;

   screen_scale = GetConfigInt(ISTR("display"), ISTR("scale"), 200);

   //AdjustWindowRect(&rc, WS_OVERLAPPEDWINDOW, TRUE);
   //SetWindowPos(hWnd, NULL, 0, 0, rc.right - rc.left, rc.bottom - rc.top, SWP_NOMOVE|SWP_NOZORDER|SWP_NOACTIVATE);
   OnMenuScale(screen_scale);

   ShowWindow(hWnd, nCmdShow);
   UpdateWindow(hWnd);

   wMemory_Show(TRUE);

   return TRUE;
}


VOID Cleanup()
{
	int i;
	if(hwvOut != NULL)
	{
		waveOutReset(hwvOut);

		for(i = 0; i< MAX_BUF_NUM; i++)
		{
			if(wvBuffer[i].dwFlag == BUFFER_DONE)
			{
				waveOutUnprepareHeader(hwvOut, &wvBuffer[i].wvhdr, sizeof(wvBuffer[i].wvhdr));
				wvBuffer[i].dwFlag = BUFFER_FREE;
			}
		}

		waveOutClose(hwvOut);
		hwvOut = NULL;
	}

	wMemory_Destroy();

	if(hMainWnd != NULL)
	{
		DestroyWindow(hMainWnd);
	}

	net_fini();
}

//
//  函数: WndProc(HWND, UINT, WPARAM, LPARAM)
//
//  目的: 处理主窗口的消息。
//
//  WM_COMMAND	- 处理应用程序菜单
//  WM_PAINT	- 绘制主窗口
//  WM_DESTROY	- 发送退出消息并返回
//
//
LRESULT CALLBACK WndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
{
	int wmId, wmEvent;
	PAINTSTRUCT ps;
	HDC hdc;

	switch (message)
	{
	case WM_CREATE:
		// 接受拖放文件直接打开
		DragAcceptFiles(hWnd, TRUE);
		break;
	case WM_DROPFILES:
		return OnDropFiles(hWnd, (HDROP) wParam);
		break;
	case WM_INITMENUPOPUP:
		return OnInitMenuPopup(hWnd, (HMENU)wParam, (UINT)LOWORD(lParam), (BOOL)HIWORD(lParam));
		break;
	case WM_COMMAND:
		wmId    = LOWORD(wParam);
		wmEvent = HIWORD(wParam);
		// 分析菜单选择:
		switch (wmId)
		{
		case IDM_ABOUT:
			DialogBox(hInst, MAKEINTRESOURCE(IDD_ABOUTBOX), hWnd, About);
			break;
		case IDM_EXIT:
			// 联网中退出: 先确认, 再通知对端一次(否则对端会一直空转等待)
			if(!ConfirmStopNetPlay(hWnd))
				break;

			NotifyPeerQuitAndEnd();
			DestroyWindow(hWnd);
			break;
		case IDM_OPEN:   // 载入ROM文件
			OnMenuOpen();
			break;
		case IDM_CLOSE:
			OnMenuClose();
			break;
		case IDM_NET_PLAY:
			if(host.status != NES_STATUS_OFF)
			{
				if(TRUE == dlgNetPlay_DoModal(hInst, hWnd, host.rom.crc32_p))
				{
					ines_int_t cache_num = (ines_int_t)np_cache_num();

					INES_LOG(LOG_NTY, MOD_NET, ISTR("netplay: %s(manual), cache_num=%d\n"),
							 np_is_server() ? ISTR("server") : ISTR("client"), cache_num);

					is_net_play = 1;

					// 硬件复位
					OnMenuHardReset();

					UpdateTitle();   // 标题栏挂上"联网对战中"
				}
			}
			else
			{
				MessageBox(hWnd, ISTR("请先载入一个ROM。"), szTitle, MB_OK|MB_ICONWARNING);
			}
			break;
		case IDM_LAN_MATCH:
			if(host.status != NES_STATUS_OFF)
			{
				if(TRUE == dlgLanMatch_DoModal(hInst, hWnd, host.rom.crc32_p))
				{
					ines_int_t cache_num = (ines_int_t)np_cache_num();

					INES_LOG(LOG_NTY, MOD_NET, ISTR("netplay: %s(lan), cache_num=%d\n"),
							 np_is_server() ? ISTR("server") : ISTR("client"), cache_num);

					is_net_play = 1;

					// 硬件复位
					OnMenuHardReset();

					UpdateTitle();   // 标题栏挂上"联网对战中"
				}
			}
			else
			{
				MessageBox(hWnd, ISTR("请先载入一个ROM。"), szTitle, MB_OK|MB_ICONWARNING);
			}
			break;
		case IDM_RECENT_FILES+0:
		case IDM_RECENT_FILES+1:
		case IDM_RECENT_FILES+2:
		case IDM_RECENT_FILES+3:
		case IDM_RECENT_FILES+4:
		case IDM_RECENT_FILES+5:
		case IDM_RECENT_FILES+6:
		case IDM_RECENT_FILES+7:
		case IDM_RECENT_FILES+8:
		case IDM_RECENT_FILES+9:
			NesOpenFile(lastest_open_files[wmId - IDM_RECENT_FILES]);
			break;
		case IDM_SOFTRESET:
			// 非主机不能复位: 复位会重跑双方的 ROM, 从机擅自发起等于打断主机
			if(is_net_play)
			{
				if(!np_is_server())
				{
					MessageBox(hWnd, ISTR("联网对战中只有主机可以复位。"), szTitle, MB_OK|MB_ICONINFORMATION);
					break;
				}

				ctrl_key_state = NET_CTRL_CODE_SOFTRESET;
			}
			else
			{
				OnMenuSoftReset();
			}
			break;
		case IDM_HARDRESET:
			// 非主机不能复位(同上)
			if(is_net_play)
			{
				if(!np_is_server())
				{
					MessageBox(hWnd, ISTR("联网对战中只有主机可以复位。"), szTitle, MB_OK|MB_ICONINFORMATION);
					break;
				}

				ctrl_key_state = NET_CTRL_CODE_HARDRESET;
			}
			else
			{
				OnMenuHardReset();
			}
			break;
		case IDM_PAUSE:
			// 暂停会让对端收不到输入包而一直空转, 联网中禁止
			if(is_net_play)
			{
				MessageBox(hWnd, ISTR("联网对战中不能暂停。"), szTitle, MB_OK|MB_ICONINFORMATION);
				break;
			}

			OnMenuPause();
			break;
		case IDM_FRAME_STEP:
			// 与暂停同理(单帧执行同样停帧)
			if(is_net_play)
			{
				MessageBox(hWnd, ISTR("联网对战中不能单帧执行。"), szTitle, MB_OK|MB_ICONINFORMATION);
				break;
			}

			OnMenuFrameStep();
			break;
		case IDM_LOG_TRACE:
			OnMenuLogLevel(LOG_TRA);
			break;
		case IDM_LOG_DEBUG:
			OnMenuLogLevel(LOG_DBG);
			break;
		case IDM_LOG_INFO:
			OnMenuLogLevel(LOG_INF);
			break;
		case IDM_LOG_WARNING:
			OnMenuLogLevel(LOG_WAR);
			break;
		case IDM_LOG_ERROR:
			OnMenuLogLevel(LOG_ERR);
			break;
		case IDM_LOG_NOTIFY:
			OnMenuLogLevel(LOG_NTY);
			break;
		case IDM_LOG_NONE:
			OnMenuLogLevel(LOG_MAX);
			break;
		case IDM_CPU_TRACE:
			OnMenuCPUTrace();
			break;
		case IDM_SNAPSHOT:
			OnMenuSnapshot();
			break;
		case IDM_ZOOM_X1:
			OnMenuScale(100);
			break;
		case IDM_ZOOM_X2:
			OnMenuScale(200);
			break;
		case IDM_ZOOM_X3:
			OnMenuScale(300);
			break;
		case IDM_ZOOM_X4:
			OnMenuScale(400);
			break;
		case IDM_VIEW_MEMORY:
			if(1 || !wMemory_IsShow())
			{
				wMemory_Create(hInst, hMainWnd);
				wMemory_Show(TRUE);

			}
			else
			{
				wMemory_Destroy();
			}
			break;
		case IDM_VIEW_VMEMORY:
			if(1 || !wVMemory_IsShow())
			{
				wVMemory_Create(hInst, hMainWnd);
				wVMemory_Show(TRUE);

			}
			else
			{
				wVMemory_Destroy();
			}
			break;
		case IDM_VIEW_SPMEMORY:
			if(1 || !wSPMemory_IsShow())
			{
				wSPMemory_Create(hInst, hMainWnd);
				wSPMemory_Show(TRUE);

			}
			else
			{
				wSPMemory_Destroy();
			}
			break;
		case IDM_VIEW_PT:
			if(1 || !wPT_IsShow())
			{
				wPT_Create(hInst, hMainWnd);
				wPT_Show(TRUE);

			}
			else
			{
				wPT_Destroy();
			}
			break;
		case IDM_VIEW_NT:
			if(1 || !wNT_IsShow())
			{
				wNT_Create(hInst, hMainWnd);
				wNT_Show(TRUE);

			}
			else
			{
				wNT_Destroy();
			}
			break;
		case IDM_VIEW_PAL:
			if(1 || !wPal_IsShow())
			{
				wPal_Create(hInst, hMainWnd);
				wPal_Show(TRUE);

			}
			else
			{
				wPal_Destroy();
			}
			break;
		case IDM_VIEW_REG:
			if(1 || !wReg_IsShow())
			{
				wReg_Create(hInst, hMainWnd);
				wReg_Show(TRUE);

			}
			else
			{
				wReg_Destroy();
			}
			break;
		case IDM_SAVE_STATE_0:
		case IDM_SAVE_STATE_1:
		case IDM_SAVE_STATE_2:
		case IDM_SAVE_STATE_3:
		case IDM_SAVE_STATE_4:
		case IDM_SAVE_STATE_5:
		case IDM_SAVE_STATE_6:
		case IDM_SAVE_STATE_7:
		case IDM_SAVE_STATE_8:
		case IDM_SAVE_STATE_9:
			OnMenuSaveState(wmId - IDM_SAVE_STATE_0);
			break;
		case IDM_LOAD_STATE_0:
		case IDM_LOAD_STATE_1:
		case IDM_LOAD_STATE_2:
		case IDM_LOAD_STATE_3:
		case IDM_LOAD_STATE_4:
		case IDM_LOAD_STATE_5:
		case IDM_LOAD_STATE_6:
		case IDM_LOAD_STATE_7:
		case IDM_LOAD_STATE_8:
		case IDM_LOAD_STATE_9:
			OnMenuLoadState(wmId - IDM_LOAD_STATE_0);
			break;
		case IDM_MUTE:
			OnMenuMute();
			break;
		case IDM_VOLUMN_0:
			OnMenuVolumn(0);
			break;
		case IDM_VOLUMN_20:
			OnMenuVolumn(20);
			break;
		case IDM_VOLUMN_40:
			OnMenuVolumn(40);
			break;
		case IDM_VOLUMN_60:
			OnMenuVolumn(60);
			break;
		case IDM_VOLUMN_80:
			OnMenuVolumn(80);
			break;
		case IDM_VOLUMN_100:
			OnMenuVolumn(100);
			break;
		default:
			return DefWindowProc(hWnd, message, wParam, lParam);
		}
		break;
	case WM_KEYDOWN:
		if(wParam >= '1' && wParam <= '5')
		{
			channel_enabled[wParam - '1'] = channel_enabled[wParam - '1'] ? 0 : 1;
		}
		break;
	case WM_SIZING:
		return OnSizing(hWnd, (UINT)wParam, (LPRECT)lParam);
		break;
	case WM_SIZE:
		OnSize(hWnd, (UINT)wParam, (int)LOWORD(lParam), (int)HIWORD(lParam));
		break;
	case WM_MOVE:
		OnMove(hWnd, (int)(short) LOWORD(lParam), (int)(short) HIWORD(lParam));
	case WM_PAINT:
		hdc = BeginPaint(hWnd, &ps);
		// TODO: 在此添加任意绘图代码...
		OnPaint(hWnd, hdc);
		EndPaint(hWnd, &ps);
		break;
//	case WM_ERASEBKGND:
//		return 1;
//		break;
	case MM_WOM_DONE:
		{
			WAVEHDR* pwvhdr = (WAVEHDR*) lParam;
			WAVEOUTBUFFER* pBuff = (WAVEOUTBUFFER*)pwvhdr->dwUser;
			pBuff->dwFlag = BUFFER_DONE;
			INES_LOG(LOG_DBG, MOD_SYS, ISTR("[%lu] Wave Buffer [%d] Done. \n"), timeGetTime(), (int)(pBuff-wvBuffer));
		}
		break;
	case WM_CLOSE:
		// 关闭窗口(标题栏 X / 系统菜单)与"退出"走同一条路径: 联网中先确认并通知对端
		if(!ConfirmStopNetPlay(hWnd))
			return 0;

		NotifyPeerQuitAndEnd();
		DestroyWindow(hWnd);
		return 0;
	case WM_DESTROY:
		np_fini();
		PostQuitMessage(0);
		break;
	default:
		return DefWindowProc(hWnd, message, wParam, lParam);
	}
	return 0;
}

// “关于”框的消息处理程序。
INT_PTR CALLBACK About(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam)
{
	UNREFERENCED_PARAMETER(lParam);
	switch (message)
	{
	case WM_INITDIALOG:
		return (INT_PTR)TRUE;

	case WM_COMMAND:
		if (LOWORD(wParam) == IDOK || LOWORD(wParam) == IDCANCEL)
		{
			EndDialog(hDlg, LOWORD(wParam));
			return (INT_PTR)TRUE;
		}
		break;
	}
	return (INT_PTR)FALSE;
}

LONGLONG  GetNESCPUCycles(void)
{
	return host.cpu.total_cycles;
}

int NesGetKeyState(int vk)
{
	if(GetForegroundWindow() != hMainWnd)
		return 0;
	return GetKeyState(vk);
}



VOID 	SwapScreenBuffer()
{
	ines_byte_t*  p;

	p = screen_front;
	screen_front = screen_back;
	screen_back = p;
}

VOID 	PlayAudioBuffer(WAVEOUTBUFFER* lpBuff)
{
	if(NULL == lpBuff || lpBuff->wvhdr.dwBufferLength == 0)
		return;


	// fill wavehdr struct
	lpBuff->wvhdr.lpData = (LPSTR)lpBuff->szData;
	//lpBuff->wvhdr.dwBufferLength = sizeof(lpBuff->szData);
	lpBuff->wvhdr.dwFlags = 0;
	lpBuff->wvhdr.dwLoops = 0;
	lpBuff->wvhdr.dwUser = (DWORD_PTR) lpBuff;

	if (0 == waveOutPrepareHeader(hwvOut, &lpBuff->wvhdr, sizeof(lpBuff->wvhdr))
		&& 0 == waveOutWrite(hwvOut, &lpBuff->wvhdr, sizeof(lpBuff->wvhdr)) )
	{
		lpBuff->dwFlag = BUFFER_PLAYING;
		wvPlayingNum++;

		INES_LOG(LOG_DBG, MOD_SYS, ISTR("[%lu] Play Audio Buffer [%d] size [%lu], wa playing [%d]\n"), timeGetTime(), 
			(int)(lpBuff - wvBuffer), lpBuff->wvhdr.dwBufferLength, wvPlayingNum);
	}
	else
	{
		INES_LOG(LOG_DBG, MOD_SYS, ISTR("player audio buffer failed!\n"));
	}
}

WAVEOUTBUFFER*  GetFreeAudioBuffer()
{
	int i;
	for(i = 0; i< MAX_BUF_NUM; i++)
	{
		if(wvBuffer[i].dwFlag == BUFFER_DONE)
		{
			waveOutUnprepareHeader(hwvOut, &wvBuffer[i].wvhdr, sizeof(wvBuffer[i].wvhdr));
			wvBuffer[i].dwFlag = BUFFER_FREE;
			wvBuffer[i].wvhdr.dwBufferLength = 0;
			wvPlayingNum--;
		}
	}

	if(cur_wave_buffer == NULL)
	{

		for(i = 0; i< MAX_BUF_NUM; i++)
		{
			if(wvBuffer[i].dwFlag == BUFFER_FREE)
			{
				cur_wave_buffer = &wvBuffer[i];
				cur_wave_buffer->wvhdr.dwBufferLength = 0;
				break;
			}
		}
	}
	return cur_wave_buffer;
}

#if 0
void CALLBACK waveOutCallback(  HWAVEOUT hwo,    
							  UINT uMsg,          
							  DWORD_PTR dwInstance,    
							  DWORD_PTR dwParam1,      
							  DWORD_PTR dwParam2     )
{
	if(uMsg == WOM_DONE)
	{
		WAVEHDR* pwvhdr = (WAVEHDR*) dwParam1;
		WAVEOUTBUFFER* pBuff = (WAVEOUTBUFFER*)pwvhdr->dwUser;
		pBuff->dwFlag = BUFFER_DONE;
		INES_LOG(LOG_TRA, MOD_SYS, _T("[%lu] Wave Buffer [%d] Done. \n"), timeGetTime(), (int)(pBuff-wvBuffer));
	}
}
#endif 


#define UPDATE_BIT_WITH_KEY(bit, n, k)   \
do { \
	if(NesGetKeyState(k) & 0x8000) \
	{ \
		(bit) |= (n); \
	} \
} while(0)


// extern int iAPUAddCycles ;

double dblFrameTimeAdj = 0.0f;




VOID OnIdle()
{
	DWORD          dwCurTimeMS;
	LARGE_INTEGER  llCurCounter;
	double  dblSecondPassed;
	double  dblSecondDelay;
	WAVEOUTBUFFER* lpBuff;
	ines_int_t     this_ctrl;
	ines_char_t    strDisp[256];

	this_ctrl = 0;

	if(is_net_play)
	{
		np_frame_begin();

		// 对端主动退出: 退回单机模式并提示(与 mac 端的浮层提示对等)
		if(np_peer_quit())
		{
			np_end();
			is_net_play = 0;

			UpdateTitle();

			INES_LOG(LOG_NTY, MOD_NET, ISTR("netplay: peer quitted, back to offline\n"));

			MessageBox(hMainWnd, ISTR("对方已退出游戏，继续以单机模式运行。"), szTitle, MB_OK|MB_ICONINFORMATION);
		}
	}


	// 关机状态，没有加载ROM,  或者暂停中
	if(host.status == NES_STATUS_OFF || pause_flag == NES_STATUS_PAUSE)
	{
		Sleep(10);
		return;
	}

	if(run_in_background == 0 &&  GetForegroundWindow() != hMainWnd)
	{
		Sleep(10);
		return;
	}

	if(!QueryPerformanceCounter(&llCurCounter))
	{
		Sleep(1000);
		return;
	}

	dblSecondPassed = ((double)llCurCounter.QuadPart - (double)llCpuLastCounter.QuadPart) / (double)llCpuCounterFreq.QuadPart;
	dblSecondDelay = host.setting.frame_rate - (dblSecondPassed + dblFrameTimeAdj);

	if(dblSecondDelay > 0 )
	{
		if(dblSecondDelay > 0.001)
		{
			Sleep(1);
		}
		return;
	}
	llCpuLastCounter = llCurCounter;

	if(is_net_play)
	{
		// 状态同步(联机读档): 同步期间双方冻结; 结果可能结束联网(链路断开/超时)
		if(OnIdleSyncState())
			return;

		if(np_sync_state() == NP_SYNC_BUSY)
			return;

		// 如果网络缓冲空了,则等待(帧缓存由 comm/npsession 持有)
		if(!np_input_ready())
		{
			// 网络卡
			return;
		}
	}

	dwCurTimeMS = timeGetTime();

	INES_LOG(LOG_DBG, MOD_SYS, ISTR("[%lums] NES Frame Start, Step [%lums], dblSecondPassed=%0.5fms\n"), dwCurTimeMS, dwCurTimeMS - dwFrameLastTime, dblSecondPassed * 1000);

	dwFrameLastTime = dwCurTimeMS;

	// 刷新音频缓存区
	GetFreeAudioBuffer(); 
	dblFrameTimeAdj = 0.0;


	lpBuff = NULL;
	if(pause_flag == 0)
	{
		if(wvPlayingNum == 0)
		{
			INES_LOG(LOG_DBG, MOD_SYS, ISTR(" Audio PlayingNum=%d, fill to %d\n"), wvPlayingNum, audio_cache_num);

			while( wvPlayingNum < audio_cache_num && (lpBuff = GetFreeAudioBuffer()))
			{
				lpBuff->wvhdr.dwBufferLength = (WORD)((double)NES_AUDIO_SAMPLE_RATE * host.setting.frame_rate + 0.5);
				memset(lpBuff->szData, 0x80, lpBuff->wvhdr.dwBufferLength);
				PlayAudioBuffer(lpBuff);
				cur_wave_buffer = NULL;
			}

			INES_LOG(LOG_DBG, MOD_SYS, ISTR(" Audio PlayingNum=%d, FrameTimeAdj set to: %g\n"), wvPlayingNum, dblFrameTimeAdj);
		}
		else if( wvPlayingNum < audio_cache_num )
		{
			//memset(lpBuff->szData, 0x80, NES_AUDIO_SAMPLE_RATE/60);
			//lpBuff->wvhdr.dwBufferLength = NES_AUDIO_SAMPLE_RATE/60;
			//PlayAudioBuffer(lpBuff);
			//cur_wave_buffer = NULL;
			//iAPUAddCycles = 2000;

			dblFrameTimeAdj = 0.001;

			INES_LOG(LOG_DBG, MOD_SYS, ISTR(" Audio PlayingNum=%d, FrameTimeAdj set to: %g\n"), wvPlayingNum, dblFrameTimeAdj);
		}
		else if(wvPlayingNum > audio_cache_num)
		{
			dblFrameTimeAdj = -0.001;
			INES_LOG(LOG_DBG, MOD_SYS, ISTR(" Audio PlayingNum=%d, FrameTimeAdj set to: %g\n"), wvPlayingNum, dblFrameTimeAdj);
		}
		else
		{
			if(dblFrameTimeAdj < -0.001) dblFrameTimeAdj += 0.0001;
			else if(dblFrameTimeAdj > 0.001 ) dblFrameTimeAdj -= 0.0001;
			INES_LOG(LOG_DBG, MOD_SYS, ISTR(" Audio PlayingNum=%d, FrameTimeAdj set to: %g\n"), wvPlayingNum, dblFrameTimeAdj);
		}

		lpBuff = GetFreeAudioBuffer();
		if(!lpBuff)
		{
			INES_LOG(LOG_WAR, MOD_SYS, ISTR("[%lu] No Audio Buffer Free!\n"), timeGetTime());
		}
	}

	if( lpBuff != NULL)
	{
		ines_apu_setoutbuffer(&host.apu, lpBuff->szData + lpBuff->wvhdr.dwBufferLength,
			sizeof(lpBuff->szData) - lpBuff->wvhdr.dwBufferLength, audio_mute ? 0 : audio_volume);
	}
	else
	{
		ines_apu_setoutbuffer(&host.apu, NULL, 0, 0);
	}

	// set input state
	if(GetForegroundWindow() == hMainWnd)
	{
		// flash key for 
		if(++key_flash_count >= key_flash_freq)
		{
			key_flash_count = 0;
		}

		main_key_state = 0;
		

		if(key_flash_count < key_flash_freq / 2)
		{
			UPDATE_BIT_WITH_KEY(main_key_state, JOYPAD_KEY_A, 'S');  // 连发A
			UPDATE_BIT_WITH_KEY(main_key_state, JOYPAD_KEY_B, 'A');  // 连发B
		}
		UPDATE_BIT_WITH_KEY(main_key_state, JOYPAD_KEY_A, 'X');
		UPDATE_BIT_WITH_KEY(main_key_state, JOYPAD_KEY_B, 'Z');
		UPDATE_BIT_WITH_KEY(main_key_state, JOYPAD_KEY_SELECT, VK_RSHIFT);
		UPDATE_BIT_WITH_KEY(main_key_state, JOYPAD_KEY_START, VK_RETURN);
		UPDATE_BIT_WITH_KEY(main_key_state, JOYPAD_KEY_UP, VK_UP);
		UPDATE_BIT_WITH_KEY(main_key_state, JOYPAD_KEY_DOWN, VK_DOWN);
		UPDATE_BIT_WITH_KEY(main_key_state, JOYPAD_KEY_LEFT, VK_LEFT);
		UPDATE_BIT_WITH_KEY(main_key_state, JOYPAD_KEY_RIGHT, VK_RIGHT);

		{
			// joystick support
			JOYCAPS  jcaps;
			JOYINFOEX  jinfo;

			memset(&jcaps, 0, sizeof(jcaps));
			memset(&jinfo, 0, sizeof(jinfo));
			jinfo.dwSize = sizeof(jinfo);
			jinfo.dwFlags = JOY_RETURNALL;
			if( 0 == joyGetPosEx(JOYSTICKID1, &jinfo) &&
				0 == joyGetDevCaps(JOYSTICKID1, &jcaps, sizeof(jcaps)) )
			{
				UINT xm, xl, xr, ym, yt, yb;
				xm = (jcaps.wXmin + jcaps.wXmax) / 2;
				ym = (jcaps.wYmin + jcaps.wYmax) / 2;
				xl = (jcaps.wXmin + xm) / 2;
				xr = (jcaps.wXmax + xm) / 2;
				yt = (jcaps.wYmin + ym) / 2;
				yb = (jcaps.wYmax + ym) / 2;

				if(jinfo.dwButtons & JOY_BUTTON2) main_key_state |= JOYPAD_KEY_A;
				if(jinfo.dwButtons & JOY_BUTTON3) main_key_state |= JOYPAD_KEY_B;
				if(key_flash_count < key_flash_freq / 2)
				{
					if(jinfo.dwButtons & JOY_BUTTON1) main_key_state |= JOYPAD_KEY_A;
					if(jinfo.dwButtons & JOY_BUTTON4) main_key_state |= JOYPAD_KEY_B;
				}
				if(jinfo.dwButtons & JOY_BUTTON9) main_key_state |= JOYPAD_KEY_SELECT;
				if(jinfo.dwButtons & JOY_BUTTON10) main_key_state |= JOYPAD_KEY_START;

				if(jinfo.dwXpos < xl) main_key_state |= JOYPAD_KEY_LEFT;
				if(jinfo.dwXpos > xr) main_key_state |= JOYPAD_KEY_RIGHT;
				if(jinfo.dwYpos < yt) main_key_state |= JOYPAD_KEY_UP;
				if(jinfo.dwYpos > yb) main_key_state |= JOYPAD_KEY_DOWN;


				switch(jinfo.dwPOV)
				{
				case 0:
					main_key_state |= JOYPAD_KEY_UP;
					break;
				case  9000:
					main_key_state |= JOYPAD_KEY_RIGHT;
					break;
				case 18000:
					main_key_state |= JOYPAD_KEY_DOWN;
					break;
				case 27000:
					main_key_state |= JOYPAD_KEY_LEFT;
					break;
				case  4500:
					main_key_state |= JOYPAD_KEY_RIGHT;
					main_key_state |= JOYPAD_KEY_UP;
					break;
				case 13500:
					main_key_state |= JOYPAD_KEY_RIGHT;
					main_key_state |= JOYPAD_KEY_DOWN;
					break;
				case 22500:
					main_key_state |= JOYPAD_KEY_LEFT;
					main_key_state |= JOYPAD_KEY_DOWN;
					break;
				case 31500:
					main_key_state |= JOYPAD_KEY_LEFT;
					main_key_state |= JOYPAD_KEY_UP;
					break;
				}
			}
		}
	}
	else
	{
		main_key_state = 0;
	}
	second_key_state = 0;

	if(is_net_play)
	{
		// 联机读档失败 -> 提交硬复位(只发一帧, 由延迟线保证双方同帧执行)
		if(sync_ctrl_req != 0)
		{
			ctrl_key_state = sync_ctrl_req;
			sync_ctrl_req  = 0;
		}

		// 提交本方输入并取回本帧实际使用的输入(帧缓存与手柄路由由 comm/npsession 负责,
		// 与 macOS 端 np_frame_input() 完全一致)
		np_frame_input((ines_byte_t)main_key_state, (ines_byte_t)ctrl_key_state,
					   &main_key_state, &second_key_state, &this_ctrl);

		ctrl_key_state = 0;
	}

	ines_joypad_update_bits(&host.joypad, main_key_state, second_key_state);

	ines_host_doframe(&host, screen_back);




	// check save SRAM
	ines_host_save_sram(&host, szRAMFilePath);

	QueryPerformanceCounter(&llCurCounter);

	dblLastRateSecond += dblSecondPassed;


	dblSecondPassed = ((double)llCurCounter.QuadPart - (double)llCpuLastCounter.QuadPart) / (double)llCpuCounterFreq.QuadPart;

	dblLastFrameSecond += dblSecondPassed;


	if(dblLastRateSecond >= 1.0)
	{
		nes_cpu_rate = (ines_int_t)(dblLastFrameSecond / dblLastRateSecond * 100.0);
		dblLastRateSecond = 0.0;
		dblLastFrameSecond = 0.0;
	}

	dwCurTimeMS = timeGetTime();

	INES_LOG(LOG_DBG, MOD_SYS, ISTR("[%lums] No. [%I64d] NES Frame Time: %d us\n"),dwCurTimeMS, host.frame_count, (int)(dblSecondPassed*1000*1000));

	ines_sprintf(strDisp, ISTR("CPU: %03d%%"), nes_cpu_rate);
	
	DrawTextToBitmap(screen_back, SCREEN_WIDTH,SCREEN_HEIGHT, strDisp, 8,8,32);
	// 交换绘图缓存区


	if(pause_flag == NES_STATUS_FRAME_STEP)
	{
		pause_flag = NES_STATUS_PAUSE;
	}

	SwapScreenBuffer();

	if(lpBuff != NULL)
	{
		lpBuff->wvhdr.dwBufferLength += ines_apu_getoutlen(&host.apu);
		//if(lpBuff->wvhdr.dwBufferLength >= 1000)
		{
			PlayAudioBuffer(lpBuff);
			cur_wave_buffer = NULL;
		}
	}

	if(is_net_play)
	{

		if(this_ctrl == NET_CTRL_CODE_SOFTRESET)
		{
			OnMenuSoftReset();
		}
		else if(this_ctrl == NET_CTRL_CODE_HARDRESET)
		{
			OnMenuHardReset();
		}
	}


	if(hMainWnd != NULL)
	{
		InvalidateRect(hMainWnd, NULL, TRUE);
		//UpdateWindow(hMainWnd);
	}

	UpdateAllViews();
	//Sleep(10);
}



VOID OnPaint(HWND hWnd, HDC  hDC)
{
	int   ret;
	//int    n;
	//TCHAR  strDisp[1024];
	RECT  rcClient;
	//RECT  rcText;
	//HFONT   hFont;
	HBRUSH  hBS;
	//ines_cstr_t  pszLog;
	//ines_int_t  level;    

	GetClientRect(hWnd, &rcClient);

	if(host.status == NES_STATUS_OFF)
	{
		hBS = CreateSolidBrush(RGB(128,128,128));
		FillRect(hDC, &rcClient, hBS);
		DeleteObject((HGDIOBJ)hBS);
	}
	else
	{

		// SetStretchBltMode(hDC, STRETCH_HALFTONE)

		ret = StretchDIBits(hDC, rcClient.left, rcClient.top, rcClient.right - rcClient.left,rcClient.bottom - rcClient.top, 
			0, 0, SCREEN_WIDTH, SCREEN_HEIGHT, screen_front, bmp_info, DIB_RGB_COLORS, SRCCOPY);
	}

// DRAW_TEXT:

	//hFont = CreateStockObject
#if 0
	SetBkMode(hDC, TRANSPARENT);
	//ines_sprintf(strDisp, ISTR("A:%02XH  X:%02XH  Y:%02XH  P:%02XH   SP:%02XH   PC:%04XH "), 
	//	host.cpu.reg_A, host.cpu.reg_X, host.cpu.reg_Y, host.cpu.reg_P, host.cpu.reg_SP, host.cpu.reg_PC);
	if(host.status == NES_STATUS_OFF)
		ines_sprintf(strDisp, ISTR("CPU: ---"));
	else
		ines_sprintf(strDisp, ISTR("CPU: %03d%% %s"), nes_cpu_rate, pause_flag == NES_STATUS_PAUSE ? ISTR("[PAUSED]") : ISTR(""));



	rcText.left = 10;
	rcText.top = 10;
	rcText.right = rcClient.right - 10;
	rcText.bottom = rcText.top;

	DrawText(hDC, strDisp, -1, &rcText, DT_CALCRECT|DT_NOPREFIX );

	SetTextColor(hDC, RGB(128,128,128));
	DrawText(hDC, strDisp, -1, &rcText, DT_NOPREFIX|DT_LEFT|DT_TOP);
	//TextOut(hDC, 10+1, 10-1, strDisp, strlen(strDisp));
	//TextOut(hDC, 10-1, 10+1, strDisp, strlen(strDisp));
	//TextOut(hDC, 10-1, 10-1, strDisp, strlen(strDisp));

	OffsetRect(&rcText, -1,-1);
	SetTextColor(hDC, RGB(255,255,255));
	DrawText(hDC, strDisp, -1, &rcText, DT_NOPREFIX|DT_LEFT|DT_TOP);

	// TextOut(hDC, 10, 10, strDisp, strlen(strDisp));
	//INES_LOG(LOG_DBG, MOD_SYS, ISTR("{%d,%d,%d,%d}\n"), rcClient.left, rcClient.right, rcClient.top, rcClient.bottom);
#endif

#if 0
	// Draw error log
	for(n = 0; pszLog = ines_get_last_error_log(n, &level); n++)
	{
		rcText.left = 10;
		rcText.top = rcText.bottom + 2;
		rcText.right = rcClient.right - 10;
		rcText.bottom = rcText.top;

		DrawText(hDC, pszLog, -1, &rcText, DT_CALCRECT|DT_NOPREFIX|DT_WORDBREAK );

		switch(level)
		{
		case LOG_FAU: SetTextColor(hDC, RGB(255,0,0)); break;
		case LOG_ERR: SetTextColor(hDC, RGB(192,0,0)); break;
		case LOG_WAR: SetTextColor(hDC, RGB(192,128,0)); break;
		default:      SetTextColor(hDC, RGB(192,192,192)); break;
		}
		DrawText(hDC, pszLog, -1, &rcText, DT_NOPREFIX|DT_LEFT|DT_TOP|DT_WORDBREAK);
	}
#endif
}


VOID UpdateTitle()
{
	TCHAR  szBuffer[INES_MAX_PATH] = {0};
	TCHAR  szStatus[MAX_LOADSTRING] = {0};

	if(host.status == NES_STATUS_OFF)
	{
		LoadString(hInst, IDS_STATUS_OFF, szStatus, count_of(szStatus));
		ines_snprintf(szBuffer, count_of(szBuffer), ISTR("%s (%s)"), szTitle,  szStatus);
	}
	else
	{
		switch(pause_flag)
		{
		case NES_STATUS_PAUSE:      LoadString(hInst, IDS_STATUS_PAUSE, szStatus, count_of(szStatus)); break;	
		case NES_STATUS_FRAME_STEP: LoadString(hInst, IDS_STATUS_FRAME_STEP, szStatus, count_of(szStatus)); break;	
		default:                    LoadString(hInst, IDS_STATUS_RUNNING, szStatus, count_of(szStatus)); break;	
		}

		// 联网对战中在状态前加标识, 与 mac 端标题栏的"联网对战中"一致
		if(is_net_play)
			ines_snprintf(szBuffer, count_of(szBuffer), ISTR("%s - %s (联网对战中 - %s)"), szTitle, szROMTitle, szStatus);
		else
			ines_snprintf(szBuffer, count_of(szBuffer), ISTR("%s - %s (%s)"), szTitle, szROMTitle, szStatus);
	}
	
	SetWindowText(hMainWnd, szBuffer);

}


VOID  UpdateAllViews()
{
	wMemory_SetUpdate();
	wVMemory_SetUpdate();
	wSPMemory_SetUpdate();
	wNT_SetUpdate();
	wPT_SetUpdate();
	wPal_SetUpdate();
	wReg_SetUpdate();
}


static VOID UpdateMenuRecentFiles(HMENU  hMenu, UINT nPos)
{
	int i, n = 0;
	MENUITEMINFO   minfo;
	TCHAR          mtext[1024];
	UINT           mlen;

	for (i = 0; i < count_of(lastest_open_files); i++)
	{
		if(lastest_open_files[i][0] == 0)
		{
			break;
		}
		mlen = ines_snprintf(mtext, count_of(mtext), ISTR("&%d %s"), i, lastest_open_files[i] );
		ZeroMemory(&minfo, sizeof(minfo));
		minfo.cbSize = sizeof(minfo);
		minfo.fMask = MIIM_STATE|MIIM_STRING|MIIM_ID|MIIM_FTYPE;
		minfo.wID = IDM_RECENT_FILES+i;
		minfo.fState = MFS_UNCHECKED | MFS_ENABLED;
		minfo.fType = MFT_STRING;
		minfo.dwTypeData = mtext;
		minfo.cch = mlen;
		// InsertMenuItem
		InsertMenuItem(hMenu, nPos + i, TRUE, &minfo);
		n++;
	}
	
	if(n>0)
	{
		// 删除
		while(TRUE)
		{
			UINT nID = GetMenuItemID(hMenu, nPos + n);
			if (nID >= IDM_RECENT_FILES && nID < IDM_RECENT_FILES + 10)
				DeleteMenu(hMenu, nPos + n, MF_BYPOSITION);
			else
				break;
		}
	}
}

static VOID CheckMenuByPos(HMENU  hMenu, UINT nPos, BOOL bChecked)
{
	MENUITEMINFO   minfo;

	ZeroMemory(&minfo, sizeof(minfo));
	minfo.cbSize = sizeof(minfo);
	minfo.fMask = MIIM_STATE;
	if(bChecked)
		minfo.fState = MFS_CHECKED;
	else
		minfo.fState = MFS_UNCHECKED;
	SetMenuItemInfo(hMenu, nPos,  TRUE, &minfo);
}

static VOID EnableMenuByPos(HMENU hMenu, UINT nPos, BOOL bEnable)
{
	MENUITEMINFO   minfo;

	ZeroMemory(&minfo, sizeof(minfo));
	minfo.cbSize = sizeof(minfo);
	minfo.fMask = MIIM_STATE;
	if(bEnable)
		minfo.fState = MFS_ENABLED;
	else
		minfo.fState = MFS_GRAYED;
	SetMenuItemInfo(hMenu, nPos,  TRUE, &minfo);
}

LRESULT   OnInitMenuPopup(HWND hWnd, HMENU hMenuPopup, UINT nPos, BOOL bSystem)
{
	UINT nCnt;
	UINT nID;
	UINT nCurPos;

	if(!bSystem)
	{
		nCnt = GetMenuItemCount(hMenuPopup);
		for(nCurPos = 0; nCurPos < nCnt; nCurPos++)
		{
			nID = GetMenuItemID(hMenuPopup, nCurPos);
			switch(nID)
			{
			case IDM_RECENT_FILES:
				UpdateMenuRecentFiles(hMenuPopup, nCurPos);
				break;
			case IDM_PAUSE:
				CheckMenuByPos(hMenuPopup, nCurPos, pause_flag == NES_STATUS_PAUSE);
				break;
			case IDM_LOG_TRACE:
				CheckMenuByPos(hMenuPopup, nCurPos, ines_get_log_level() == LOG_TRA);
				break;
			case IDM_LOG_DEBUG:
				CheckMenuByPos(hMenuPopup, nCurPos, ines_get_log_level() == LOG_DBG);
				break;
			case IDM_LOG_INFO:
				CheckMenuByPos(hMenuPopup, nCurPos, ines_get_log_level() == LOG_INF);
				break;
			case IDM_LOG_WARNING:
				CheckMenuByPos(hMenuPopup, nCurPos, ines_get_log_level() == LOG_WAR);
				break;
			case IDM_LOG_ERROR:
				CheckMenuByPos(hMenuPopup, nCurPos, ines_get_log_level() == LOG_ERR);
				break;
			case IDM_LOG_NOTIFY:
				CheckMenuByPos(hMenuPopup, nCurPos, ines_get_log_level() == LOG_NTY);
				break;
			case IDM_LOG_NONE:
				CheckMenuByPos(hMenuPopup, nCurPos, ines_get_log_level() > LOG_NTY);
				break;
			case IDM_CPU_TRACE:
				CheckMenuByPos(hMenuPopup, nCurPos, nes_cpu_trace_ops);
				break;
			case IDM_SAVE_STATE_0:
			case IDM_SAVE_STATE_1:
			case IDM_SAVE_STATE_2:
			case IDM_SAVE_STATE_3:
			case IDM_SAVE_STATE_4:
			case IDM_SAVE_STATE_5:
			case IDM_SAVE_STATE_6:
			case IDM_SAVE_STATE_7:
			case IDM_SAVE_STATE_8:
			case IDM_SAVE_STATE_9:
				UpdateMenuSaveState(hMenuPopup, nCurPos, nID-IDM_SAVE_STATE_0);
				break;
			case IDM_LOAD_STATE_0:
			case IDM_LOAD_STATE_1:
			case IDM_LOAD_STATE_2:
			case IDM_LOAD_STATE_3:
			case IDM_LOAD_STATE_4:
			case IDM_LOAD_STATE_5:
			case IDM_LOAD_STATE_6:
			case IDM_LOAD_STATE_7:
			case IDM_LOAD_STATE_8:
			case IDM_LOAD_STATE_9:
				UpdateMenuLoadState(hMenuPopup, nCurPos, nID - IDM_LOAD_STATE_0);
				break;
			case IDM_MUTE:
				CheckMenuByPos(hMenuPopup, nCurPos, audio_mute);
				break;
			case IDM_VOLUMN_0:
				CheckMenuByPos(hMenuPopup, nCurPos, audio_volume == 0);
				break;
			case IDM_VOLUMN_20:
				CheckMenuByPos(hMenuPopup, nCurPos, audio_volume == 20);
				break;
			case IDM_VOLUMN_40:
				CheckMenuByPos(hMenuPopup, nCurPos, audio_volume == 40);
				break;
			case IDM_VOLUMN_60:
				CheckMenuByPos(hMenuPopup, nCurPos, audio_volume == 60);
				break;
			case IDM_VOLUMN_80:
				CheckMenuByPos(hMenuPopup, nCurPos, audio_volume == 80);
				break;
			case IDM_VOLUMN_100:
				CheckMenuByPos(hMenuPopup, nCurPos, audio_volume == 100);
				break;
			}
		}
	}
	return 0;
}


LRESULT   OnSize(HWND hWnd, UINT nSizeType, int cx, int cy)
{
	return 0;
}

LRESULT   OnMove(HWND hWnd, int xPos, int yPos)
{
	SetConfigInt(ISTR("display"), ISTR("x"), xPos);
	SetConfigInt(ISTR("display"), ISTR("y"), yPos);
	return 0;
}



LRESULT   OnSizing(HWND hWnd, UINT nSide, LPRECT lpRect)
{
	int client_width = 256;
	int client_height = 240;
	float   ra =  (float)client_width / client_height;

	RECT    rc;

	rc.left = 0;
	rc.top = 0;
	rc.right = rc.left + client_width;
	rc.bottom = rc.top + client_height;

	switch(nSide)
	{
	case WMSZ_LEFT:
		break;
	case WMSZ_BOTTOMLEFT:
		break;
	case WMSZ_RIGHT:
		break;
	case WMSZ_BOTTOMRIGHT:
		break;
	case WMSZ_TOP:
		break;
	case  WMSZ_TOPRIGHT:
		break;
	case WMSZ_TOPLEFT:
		break;
	case WMSZ_BOTTOM:
		break;
	}


	return 1;
}


BOOL NesOpenFile(LPCTSTR lpszFileName)
{
	BOOL bRet;

	// 联网中换 ROM 等于结束当前对局: 先确认(必须在 free host 之前, 取消才不会
	// 把当前 ROM 也一起卸掉), 确认后通知对端一次。菜单 / 最近文件 / 拖拽都走这里。
	if(is_net_play && !ConfirmStopNetPlay(hMainWnd))
		return FALSE;

	ines_host_free(&host);
	ines_host_init(&host, 1);

	if(is_net_play)
		NotifyPeerQuitAndEnd();

	pause_flag = 0;

	// Save file Path
	_tcsncpy(szROMFilePath, lpszFileName, count_of(szROMFilePath));
	get_file_title(szROMTitle, szROMFilePath);
	getSavePath(szRAMFilePath, count_of(szRAMFilePath));

	if(!ines_host_load_rom(&host, lpszFileName, szRAMFilePath))
	{
		UpdateTitle();
		InvalidateRect(hMainWnd, NULL, TRUE);
		MessageBox(hMainWnd, ISTR("Load ROM File Failed!"), szTitle, MB_OK|MB_ICONINFORMATION );
		bRet = FALSE;
	}
	else
	{
		ines_host_reset(&host);
		UpdateTitle();
		AddHistoryFile(szROMFilePath);
		bRet = TRUE;
	}

	// add to history file list...
	UpdateAllViews();
	return bRet;
}

LRESULT   OnDropFiles(HWND hWnd, HDROP hDrop)
{
	TCHAR szPathName[INES_MAX_PATH];

	szPathName[0] = 0;
	DragQueryFile(hDrop,0,szPathName, count_of(szPathName)); 

	if(NesOpenFile(szPathName))
	{

	}

	DragFinish(hDrop);

	return 0;
}



/**
 * 联网对战中做"会结束对局"的操作(换 ROM / 卸载 ROM / 退出)之前的二次确认。
 * 非联网时直接放行; 用户取消时返回 FALSE(操作应整体放弃)。
 */
BOOL ConfirmStopNetPlay(HWND hWnd)
{
	if(!is_net_play)
		return TRUE;

	return (IDOK == MessageBox(hWnd, ISTR("正在联网游戏中，是否确认结束当前游戏？"),
							   szTitle, MB_OKCANCEL|MB_ICONQUESTION|MB_DEFBUTTON2));
}

/**
 * 本方主动结束: 给对端发一次"我退出了"(NET_CMD_QUIT)再收尾。
 * win32 是单线程前端(OnIdle 与菜单同在 UI 线程), 可直接调用, 不存在并发。
 */
VOID NotifyPeerQuitAndEnd(VOID)
{
	if(!is_net_play)
		return;

	np_notify_quit();
	np_end();
	is_net_play = 0;

	UpdateTitle();

	INES_LOG(LOG_NTY, MOD_NET, ISTR("netplay: ended by local user\n"));
}


VOID OnMenuOpen()
{
	// 自定义文件加载管理器: 选择文件夹 -> 列出 NES 文件及其属性 -> 加载
	ines_char_t  szPath[INES_MAX_PATH];
	ines_char_t  szDir[INES_MAX_PATH];
	ines_char_t  szLastDir[INES_MAX_PATH];
	ines_char_t* pSplit;
	ines_cstr_t  szConfigDir;
	DWORD        dwAttr;

	szDir[0] = 0;
	szPath[0] = 0;
	szLastDir[0] = 0;

	// 初始目录优先取上次使用的文件夹(记录在 config.ini, 重启后依然有效)
	// GetConfigStr 内部按 GetLastError() 判断读取是否失败, 先清零避免上一次调用的残留错误码
	SetLastError(ERROR_SUCCESS);
	szConfigDir = GetConfigStr(ISTR("rom"), ISTR("last_dir"), ISTR(""));
	if(szConfigDir != NULL && szConfigDir[0] != 0)
	{
		_tcsncpy(szDir, szConfigDir, count_of(szDir) - 1);
		szDir[count_of(szDir) - 1] = 0;

		// 目录可能已被删除或改名, 不可用时忽略这个记录
		dwAttr = GetFileAttributes(szDir);
		if(dwAttr == INVALID_FILE_ATTRIBUTES || 0 == (dwAttr & FILE_ATTRIBUTE_DIRECTORY))
			szDir[0] = 0;
	}

	// 没有可用的历史目录时, 退回到当前 ROM 所在目录, 再没有则由对话框使用当前工作目录
	if(szDir[0] == 0 && szROMFilePath[0] != 0)
	{
		_tcsncpy(szDir, szROMFilePath, count_of(szDir) - 1);
		szDir[count_of(szDir) - 1] = 0;

		pSplit = _tcsrchr(szDir, _T('\\'));
		if(pSplit != NULL)
			*pSplit = 0;
	}

	INES_LOG(LOG_DBG, MOD_SYS, ISTR("Open ROM dialog initial dir: \'%s\'\n"), szDir);

	if(dlgOpenRom_DoModal(hInst, hMainWnd, (szDir[0] != 0) ? szDir : NULL,
			szPath, count_of(szPath), szLastDir, count_of(szLastDir)))
	{
		NesOpenFile(szPath);
	}

	// 记住对话框关闭时所在的文件夹(点“加载”或“取消”都算), 下次打开时默认定位到这里
	if(szLastDir[0] != 0)
		SetConfigStr(ISTR("rom"), ISTR("last_dir"), szLastDir);
}


VOID OnMenuClose()
{
	// 卸载 ROM 同样会结束联网: 先确认(同样在 free host 之前)
	if(is_net_play && !ConfirmStopNetPlay(hMainWnd))
		return;

	ines_host_free(&host);
	ines_host_init(&host, 1);

	if(is_net_play)
		NotifyPeerQuitAndEnd();

	nes_cpu_rate = 0;
	UpdateTitle();
	InvalidateRect(hMainWnd, NULL, TRUE);
	UpdateAllViews();
}

VOID OnMenuSoftReset()
{

	ines_host_reset(&host);

	// 取消暂停
	pause_flag = 0;

	UpdateTitle();	
	UpdateAllViews();
}

VOID OnMenuHardReset()
{

	ines_host_free(&host);
	ines_host_init(&host, 1);
	if(ines_host_load_rom(&host, szROMFilePath, szRAMFilePath))
	{
		ines_host_reset(&host);

		// 取消暂停
		pause_flag = 0;

		UpdateTitle();
	}
	else
	{
		if(is_net_play)
		{
			np_end();
			is_net_play = 0;
		}

	}
	UpdateAllViews();
}

VOID OnMenuLogLevel(ines_int_t  level)
{
	ines_set_log_level(level);
}

VOID OnMenuCPUTrace()
{
	nes_cpu_trace_ops = nes_cpu_trace_ops ? 0 : 1;
}


VOID OnMenuPause()
{
	if(pause_flag == 0)
	{
		pause_flag = NES_STATUS_PAUSE;
	}
	else
	{
		pause_flag = 0;
	}
	UpdateTitle();
	InvalidateRect(hMainWnd, NULL, TRUE);
}

VOID OnMenuFrameStep()
{
	if(host.status == NES_STATUS_RUNNING)
	{
		pause_flag = NES_STATUS_FRAME_STEP;
	}
	else
	{
		pause_flag = NES_STATUS_PAUSE;
	}
	UpdateTitle();
}

VOID OnMenuScale(ines_int_t  scale)
{
	RECT   rc;

	SetRect(&rc, 0, 0, SCREEN_WIDTH * scale / 100, SCREEN_HEIGHT * scale / 100);
	AdjustWindowRect(&rc, WS_OVERLAPPEDWINDOW, TRUE);
	SetWindowPos(hMainWnd, NULL, 0,0, rc.right - rc.left, rc.bottom - rc.top, SWP_NOMOVE|SWP_NOZORDER|SWP_NOACTIVATE);
	SetConfigInt(ISTR("display"), ISTR("scale"), scale);
}

VOID OnMenuOptions()
{

}

VOID OnMenuFullScreen()
{

}



VOID OnMenuMute()
{
	if(audio_mute ==0)
		audio_mute = 1;
	else
		audio_mute = 0;

	SetConfigInt(ISTR("audio"), ISTR("mute"), audio_mute);
}

VOID OnMenuVolumn(int volumn)
{
	audio_volume = volumn;
	SetConfigInt(ISTR("audio"), ISTR("volume"), audio_volume);
}


VOID OnMenuSnapshot()
{
	TCHAR  file_path[INES_MAX_PATH];
	TCHAR  file_name[INES_MAX_PATH];
	LPTSTR  pst;
	struct _stat   st;
	struct tm     lt;
	time_t        t;
	FILE*         fbmp;
	BITMAPFILEHEADER   bmfh;

	if(host.status == NES_STATUS_OFF )
		return;

	GetModuleFileName(NULL, file_path, INES_MAX_PATH);

	pst = file_path + _tcslen(file_path);
	while(pst > file_path && pst[-1] != '\\') pst--;

	_tcscpy(pst, ISTR("snapshot"));
	if(0 != _tstat(file_path, &st))
	{
		_tmkdir(file_path);
	}

	t = time(NULL);
	lt = *localtime(&t);

	ines_snprintf(file_name, INES_MAX_PATH, ISTR("%s\\%s_snapshot_%04d%02d%02d%02d%02d%02d.bmp"), file_path, szROMTitle, 
		lt.tm_year+1900, lt.tm_mon + 1, lt.tm_mday, lt.tm_hour, lt.tm_min, lt.tm_sec);
	

	fbmp = _tfopen(file_name, ISTR("wb"));
	if(fbmp == NULL)
	{
		MessageBox(hMainWnd, ISTR("open file for write Failed!"), szTitle, MB_ICONSTOP|MB_OK);
		return;
	}

	bmfh.bfType = 0x4d42; // "BM"
	bmfh.bfSize = sizeof(BITMAPFILEHEADER)+sizeof(BITMAPINFOHEADER)+sizeof(RGBQUAD)*256+SCREEN_IMAGE_BYTES;
	bmfh.bfReserved1 = 0;
	bmfh.bfReserved2 = 0;
	bmfh.bfOffBits = sizeof(BITMAPFILEHEADER)+sizeof(BITMAPINFOHEADER)+sizeof(RGBQUAD)*256;


	fwrite(&bmfh, sizeof(BITMAPFILEHEADER), 1, fbmp);
	fwrite(bmp_info, sizeof(BITMAPINFOHEADER)+sizeof(RGBQUAD)*256, 1, fbmp);
	fwrite(screen_front, SCREEN_IMAGE_BYTES, 1, fbmp);

	fclose(fbmp);

	// show info text
	// AddTips(_T("Snapshot has saved to ...));
	INES_LOG(LOG_NTY, MOD_SYS, ISTR("Save Snapshot to file \'%s\' OK!\n"), file_name);
}


void LoadHistories()
{
	int          i;
	ines_char_t  key[10];
	for(i = 0; i < count_of(lastest_open_files); i++)
	{
		ines_snprintf(key, count_of(key), ISTR("file%d"), i);
		ines_snprintf(lastest_open_files[i],  count_of(lastest_open_files[i]), ISTR("%s"),  GetConfigStr(ISTR("histories"), key, ISTR("") ) );
	}
}

void SaveHistories()
{
	int          i;
	ines_char_t  key[10];
	for(i = 0; i < count_of(lastest_open_files); i++)
	{
		ines_snprintf(key, count_of(key), ISTR("file%d"), i);
		SetConfigStr(ISTR("histories"), key, lastest_open_files[i] );
	}
}

void  AddHistoryFile(ines_cstr_t  file)
{
	int i;
	// 如果有相同文件名，则删除之
	for(i = 0; i < count_of(lastest_open_files); i++)
	{
		if(0 == ines_strcmp(file, lastest_open_files[i] ))
		{
			break;
		}
	}

	if(i>0)
	{
		// 如果i>=10.则没有相同的，淘汰最后一条
		while(--i>=0)
		{
			if (i+1 < count_of(lastest_open_files))
			{
				ines_strcpy(lastest_open_files[i+1], lastest_open_files[i]);
			}
		}
		ines_snprintf(lastest_open_files[0],  count_of(lastest_open_files[0]), ISTR("%s"), file);
		SaveHistories();
	}
}


ines_cstr_t  GetConfigStr(ines_cstr_t sec, ines_cstr_t key, ines_cstr_t  def)
{
	static ines_char_t buffer[4096];
	buffer[0] = 0;
	GetPrivateProfileString(sec, key, def, buffer, count_of(buffer), szPrivateProfilePath);
	if (GetLastError() != 0)
		return def;
	return buffer;
}


ines_int_t GetConfigInt(ines_cstr_t sec, ines_cstr_t key, ines_int_t   def)
{
	return GetPrivateProfileInt(sec, key, def, szPrivateProfilePath);
}


void SetConfigStr(ines_cstr_t sec, ines_cstr_t key, ines_cstr_t  val)
{
	WritePrivateProfileString(sec, key, val, szPrivateProfilePath);
}


void SetConfigInt(ines_cstr_t sec, ines_cstr_t key, ines_int_t   val)
{
	ines_char_t  buffer[128];
	_itot(val, buffer, 10);
	WritePrivateProfileString(sec, key, buffer, szPrivateProfilePath);
}


ines_cstr_t getRelativeFilePath( ines_str_t szPath, size_t szLen, ines_cstr_t  fileName)
{
	LPTSTR p;
	GetModuleFileName(NULL, szPath, szLen);
	p = szPath + _tcslen(szPath);
	while(p > szPath && p[-1] != '\\') p--;
	wsprintf(p, ISTR("%s"), fileName);
	return szPath;
}


ines_cstr_t getSavePath( ines_str_t szPath, size_t szLen)
{
	LPTSTR p;
	GetModuleFileName(NULL, szPath, szLen);
	p = szPath + _tcslen(szPath);
	while(p > szPath && p[-1] != '\\') p--;
	wsprintf(p, ISTR("save\\%s.sav"), szROMTitle);
	return szPath;
}

ines_cstr_t getStatePath(int index, ines_str_t szPath, size_t szLen)
{
	LPTSTR p;
	GetModuleFileName(NULL, szPath, szLen);
	p = szPath + _tcslen(szPath);
	while(p > szPath && p[-1] != '\\') p--;
	wsprintf(p, ISTR("state\\%s.st%d"), szROMTitle, index);
	return szPath;
}

time_t GetSaveStateTime(int index)
{
	int     t;
	FILE*   fSave;
	TCHAR   szPath[INES_MAX_PATH];
	getStatePath(index, szPath, count_of(szPath));
	fSave = _tfopen(szPath, ISTR("rb"));

	if(fSave == NULL)
		return (time_t)0;

	t = ines_check_state_time(&host, fSave);

	fclose(fSave);

	return t;
}


VOID OnMenuSaveState(int index)
{
	FILE*   fSave;
	TCHAR   szPath[INES_MAX_PATH];
	getStatePath(index, szPath, count_of(szPath));

	// 覆盖提醒 
	if(GetSaveStateTime(index) != 0 && IDOK != MessageBox(hMainWnd, ISTR("该存档已经存在，是否覆盖？"), szTitle, MB_OKCANCEL|MB_ICONWARNING))
	{
		return; // 终止
	}

	fSave = _tfopen(szPath, ISTR("wb"));
	if(fSave == NULL)
	{
		INES_LOG(LOG_WAR, MOD_SYS, ISTR("Save State failed (%d) %s\n"), errno, _tcserror(errno));
		return;
	}

	if(0 == ines_save_state(&host, fSave))
	{
		INES_LOG(LOG_INF, MOD_SYS, ISTR("Save State to \'%s\' done.\n"), szPath);
	}
	else
	{

	}

	fclose(fSave);
	
}

VOID OnMenuLoadState(int index)
{
	FILE*   fSave;
	TCHAR   szPath[INES_MAX_PATH];

	if(host.status == NES_STATUS_OFF)
		return;

	// 联网对战: 只有主机能发起读档, 且必须同步给对端 —— 本机不立即载入,
	// 由 np_sync 在收齐对端结果后统一载入, 保证两端载入的是同一份字节流
	if(is_net_play)
	{
		OnMenuSyncState(index);
		return;
	}


	getStatePath(index, szPath, count_of(szPath));

	fSave = _tfopen(szPath, ISTR("rb"));
	if(fSave == NULL)
	{
		INES_LOG(LOG_WAR, MOD_SYS, ISTR("Load State failed (%d) %s\n"), errno, _tcserror(errno));
		return;
	}

	if(0 == ines_load_state(&host, fSave))
	{
		INES_LOG(LOG_INF, MOD_SYS, ISTR("Load State to \'%s\' done.\n"), szPath);
	}
	else
	{
		OnMenuHardReset();
	}

	fclose(fSave);

}


// 联机读档(状态同步) —— 详见 docs/netplay-state-sync-plan.md

// 读档字节流的临时缓冲: 同一时刻只会有一个同步在进行
static ines_byte_t  s_sync_buf[NP_SYNC_MAX_SIZE];

/** CRC32(IEEE 802.3): 算"两端是否载入了同一份状态"的摘要。 */
static ines_dword_t ines_crc32_mem(ines_dword_t crc, const void* data, size_t len)
{
	static ines_dword_t  table[256];
	static int           inited = 0;
	const ines_byte_t*   p = (const ines_byte_t*)data;
	size_t               i;
	int                  j;

	if(!inited)
	{
		for(j = 0; j < 256; j++)
		{
			ines_dword_t  c = (ines_dword_t)j;
			int           k;

			for(k = 0; k < 8; k++)
				c = (c & 1) ? (0xedb88320u ^ (c >> 1)) : (c >> 1);

			table[j] = c;
		}

		inited = 1;
	}

	if((p == NULL) || (len == 0))
		return crc;

	for(i = 0; i < len; i++)
		crc = table[(crc ^ p[i]) & 0xff] ^ (crc >> 8);

	return crc;
}

/**
 * np_sync 的载入回调: 把对端发来的字节流写成临时文件, 再走 ines_load_state()。
 * 从机**不写自己的槽位**(需求: 不覆盖从机本地存档), 用完即删。
 */
static int ines_state_load_mem(const void* buf, int len, void* user)
{
	ines_char_t  szPath[INES_MAX_PATH];
	FILE*        fp;
	int          rc;

	(void)user;

	if((buf == NULL) || (len <= 0))
		return -1;

	getRelativeFilePath(szPath, count_of(szPath), ISTR("state\\.sync.tmp"));

	fp = _tfopen(szPath, ISTR("wb"));

	if(fp == NULL)
		return -1;

	if(fwrite(buf, 1, (size_t)len, fp) != (size_t)len)
	{
		fclose(fp);
		_tremove(szPath);
		return -1;
	}

	fclose(fp);

	fp = _tfopen(szPath, ISTR("rb"));

	if(fp == NULL)
	{
		_tremove(szPath);
		return -1;
	}

	rc = ines_load_state(&host, fp);

	fclose(fp);
	_tremove(szPath);

	if(rc != 0)
		INES_LOG(LOG_ERR, MOD_SYS, ISTR("netplay: load sync state failed!\n"));

	return (rc == 0) ? 0 : -1;
}

/**
 * np_sync 的摘要回调: RAM + 名称表 + OAM + 帧号 + CPU 寄存器。
 * 某个 mapper 的 savestate 若不完整, 两端算出来的值必然不同 —— 立刻走硬件复位,
 * 而不是跑几帧之后才发现 desync。
 */
static int ines_state_sign(ines_dword_t* out_sign, void* user)
{
	ines_dword_t  crc = 0xffffffffu;

	(void)user;

	if(out_sign == NULL)
		return -1;

	crc = ines_crc32_mem(crc, host.cpu.RAM, sizeof(host.cpu.RAM));
	crc = ines_crc32_mem(crc, host.ppu.name_table, sizeof(host.ppu.name_table));
	crc = ines_crc32_mem(crc, host.ppu.sp_RAM, sizeof(host.ppu.sp_RAM));
	crc = ines_crc32_mem(crc, &host.frame_count, sizeof(host.frame_count));
	crc = ines_crc32_mem(crc, &host.cpu.reg_A, sizeof(host.cpu.reg_A));
	crc = ines_crc32_mem(crc, &host.cpu.reg_X, sizeof(host.cpu.reg_X));
	crc = ines_crc32_mem(crc, &host.cpu.reg_Y, sizeof(host.cpu.reg_Y));
	crc = ines_crc32_mem(crc, &host.cpu.reg_P, sizeof(host.cpu.reg_P));
	crc = ines_crc32_mem(crc, &host.cpu.reg_SP, sizeof(host.cpu.reg_SP));
	crc = ines_crc32_mem(crc, &host.cpu.reg_PC, sizeof(host.cpu.reg_PC));

	*out_sign = crc ^ 0xffffffffu;

	return 0;
}

/**
 * 联网读档(仅主机可用): 把存档字节流交给会话层同步给从机。
 *
 * 本机**不立即载入** —— 由 np_sync 在收齐对端结果后统一载入, 两端才是同一份状态。
 * 本地校验失败(槽位为空 / 读不出来 / 过大)时只提示: 还没发包、状态没变,
 * 既不冻结也不复位, 对战继续。
 */
VOID OnMenuSyncState(int index)
{
	ines_char_t  szPath[INES_MAX_PATH];
	FILE*        fSave = NULL;
	long         size  = 0;

	if(host.status == NES_STATUS_OFF)
		return;

	if(!np_is_server())
	{
		MessageBox(hMainWnd, ISTR("联网对战中只有主机可以载入存档。"), szTitle, MB_OK|MB_ICONINFORMATION);
		return;
	}

	if(np_sync_state() != NP_SYNC_NONE)
	{
		MessageBox(hMainWnd, ISTR("正在同步存档，请稍候。"), szTitle, MB_OK|MB_ICONINFORMATION);
		return;
	}

	// 本地校验: 该槽位没有可用存档
	if(0 == GetSaveStateTime(index))
	{
		MessageBox(hMainWnd, ISTR("该槽位没有可用存档。"), szTitle, MB_OK|MB_ICONINFORMATION);
		return;
	}

	getStatePath(index, szPath, count_of(szPath));

	fSave = _tfopen(szPath, ISTR("rb"));

	if(fSave == NULL)
	{
		MessageBox(hMainWnd, ISTR("存档读取失败。"), szTitle, MB_OK|MB_ICONWARNING);
		return;
	}

	if((fseek(fSave, 0, SEEK_END) != 0) || ((size = ftell(fSave)) <= 0))
	{
		fclose(fSave);
		MessageBox(hMainWnd, ISTR("存档读取失败。"), szTitle, MB_OK|MB_ICONWARNING);
		return;
	}

	fseek(fSave, 0, SEEK_SET);

	if((size <= 0) || (size > NP_SYNC_MAX_SIZE))
	{
		fclose(fSave);
		MessageBox(hMainWnd, ISTR("存档过大，无法同步。"), szTitle, MB_OK|MB_ICONWARNING);
		return;
	}

	if(fread(s_sync_buf, 1, (size_t)size, fSave) != (size_t)size)
	{
		fclose(fSave);
		MessageBox(hMainWnd, ISTR("存档读取失败。"), szTitle, MB_OK|MB_ICONWARNING);
		return;
	}

	fclose(fSave);

	// np_sync_begin() 内部会拷一份, s_sync_buf 可立即复用
	if(0 != np_sync_begin(s_sync_buf, (int)size))
	{
		MessageBox(hMainWnd, ISTR("存档同步发起失败。"), szTitle, MB_OK|MB_ICONWARNING);
		return;
	}

	INES_LOG(LOG_INF, MOD_SYS, ISTR("netplay: sync state %d, %ld bytes\n"), index, size);
}

/**
 * 推进状态同步并取走结果(每帧调用一次)。
 *
 *   NP_SYNC_BUSY  : 同步中, 双方冻结(调用方跳过本帧);
 *   NP_SYNC_OK    : 成功, 继续对战;
 *   NP_SYNC_RESET : 载入失败 -> 主机提交硬复位, **连接保持**;
 *   NP_SYNC_FAILED: 链路断开 / 超时 -> 结束联网。
 *
 * @return 非 0 表示已结束联网。
 */
int OnIdleSyncState(void)
{
	ines_int_t  st = np_sync_state();

	if(st == NP_SYNC_NONE)
		return 0;

	if(st == NP_SYNC_BUSY)
	{
		np_sync_poll();   // 主机推进发送; 从机全程在 np_frame_begin() 里被动应答
		return 0;
	}

	np_sync_clear();      // 结果只消费一次

	if(st == NP_SYNC_OK)
	{
		UpdateAllViews();
		return 0;
	}

	if(st == NP_SYNC_RESET)
	{
		// 只有主机提交: 走 ctrl + 延迟线, 双方在同一逻辑帧复位
		if(np_is_server())
			sync_ctrl_req = NET_CTRL_CODE_HARDRESET;

		MessageBox(hMainWnd, ISTR("存档载入失败，已复位重开。"), szTitle, MB_OK|MB_ICONINFORMATION);
		return 0;
	}

	// NP_SYNC_FAILED: 同步中断(链路断开 / 超时) —— 状态可能已分叉, 只能结束联网
	np_end();
	is_net_play = 0;

	UpdateTitle();

	MessageBox(hMainWnd, ISTR("存档同步失败，已结束联网。"), szTitle, MB_OK|MB_ICONWARNING);

	return 1;
}




VOID UpdateMenuSaveState(HMENU hMenu, UINT nPos, int index)
{
	TCHAR  szMenuText[256];
	TCHAR  szNewMenuText[256];
	MENUITEMINFO  info;
	LPTSTR pszAccel;
	BOOL   bEnable = TRUE;
	GetMenuString(hMenu, nPos, szMenuText, count_of(szMenuText), MF_BYPOSITION);
	pszAccel = _tcsrchr(szMenuText, '\t');
	if(pszAccel == NULL)
	{
		pszAccel = szMenuText + _tcslen(szMenuText);
	}
	
	if(host.status == NES_STATUS_OFF)
	{
		bEnable = FALSE;
		ines_snprintf(szNewMenuText, count_of(szNewMenuText), ISTR("&%d 存档 (空) %s"), index, pszAccel);
	}else{
		time_t t = GetSaveStateTime(index);
		if(t == 0)
		{
			ines_snprintf(szNewMenuText, count_of(szNewMenuText), ISTR("&%d 存档 (空) %s"), index, pszAccel);
			bEnable = TRUE;
		}
		else
		{
			time_t  tt = t;
			struct  tm* lt = localtime(&tt);
			if(lt == NULL)
			{
				ines_snprintf(szNewMenuText, count_of(szNewMenuText), ISTR("&%d 存档 (\?\?) %s"), index, pszAccel);				
			}
			else
			{
				ines_snprintf(szNewMenuText, count_of(szNewMenuText), ISTR("&%d 存档 (%04d/%02d/%02d %02d:%02d:%02d) %s"),
					index, lt->tm_year+1900, lt->tm_mon+1, lt->tm_mday, lt->tm_hour, lt->tm_min, lt->tm_sec, pszAccel);				
			}
			bEnable = TRUE;
		}
	}

	info.cbSize = sizeof(info);
	info.fMask = MIIM_STATE|MIIM_TYPE;
	info.fType = MFT_STRING;
	info.dwTypeData = szNewMenuText;
	info.cch = _tcslen(szNewMenuText);
	info.fState = bEnable ? MFS_ENABLED : MFS_GRAYED;
	SetMenuItemInfo(hMenu, nPos, TRUE, &info);
}

VOID UpdateMenuLoadState(HMENU hMenu, UINT nPos, int index)
{
	TCHAR  szMenuText[256];
	TCHAR  szNewMenuText[256];
	MENUITEMINFO  info;
	LPTSTR pszAccel;
	BOOL   bEnable = TRUE;
	GetMenuString(hMenu, nPos, szMenuText, count_of(szMenuText), MF_BYPOSITION);
	pszAccel = _tcsrchr(szMenuText, '\t');
	if(pszAccel == NULL)
	{
		pszAccel = szMenuText + _tcslen(szMenuText);
	}

	if(host.status == NES_STATUS_OFF)
	{
		bEnable = FALSE;
		ines_snprintf(szNewMenuText, count_of(szNewMenuText), ISTR("&%d 读档 (空) %s"), index, pszAccel);
	}else{
		time_t t = GetSaveStateTime(index);
		if(t == 0)
		{
			ines_snprintf(szNewMenuText, count_of(szNewMenuText), ISTR("&%d 读档 (空) %s"), index, pszAccel);
			bEnable = FALSE;
		}
		else
		{
			time_t  tt = t;
			struct  tm* lt = localtime(&tt);
			if(lt == NULL)
			{
				ines_snprintf(szNewMenuText, count_of(szNewMenuText), ISTR("&%d 读档 (\?\?) %s"), index, pszAccel);				
			}
			else
			{
				ines_snprintf(szNewMenuText, count_of(szNewMenuText), ISTR("&%d 读档 (%04d/%02d/%02d %02d:%02d:%02d) %s"),
					index, lt->tm_year+1900, lt->tm_mon+1, lt->tm_mday, lt->tm_hour, lt->tm_min, lt->tm_sec, pszAccel);				
			}
			bEnable = TRUE;
		}
	}

	// 联网对战: 只有**主机**能发起读档(会把存档同步给对端); 从机灰显,
	// 同步进行中也短暂禁用, 避免重复发起(与 macOS 端的 validateMenuItem 一致)
	if(is_net_play && ((!np_is_server()) || (np_sync_state() != NP_SYNC_NONE)))
		bEnable = FALSE;

	info.cbSize = sizeof(info);
	info.fMask = MIIM_STATE|MIIM_TYPE;
	info.fType = MFT_STRING;
	info.dwTypeData = szNewMenuText;
	info.cch = _tcslen(szNewMenuText);
	info.fState = bEnable ? MFS_ENABLED : MFS_GRAYED;
	SetMenuItemInfo(hMenu, nPos, TRUE, &info);
}




void DrawTextToBitmap(ines_byte_t* bits, ines_int_t iWidth, ines_int_t iHeight, 
					  ines_cstr_t szText, ines_int_t x, ines_int_t y, ines_byte_t clText)
{
	ines_int_t   x0;
	ines_int_t   y0;

	ines_int_t   index;
	ines_int_t   offset;
	ines_int_t   n, m;
	ines_byte_t* pch;
	


	x0 = x;
	y0 = y;
	index = 0;

	while(szText &&  szText[index])
	{
		pch = (szText[index] >= 0x20 && szText[index] < 0x80) ? (code_ascii_5x7 + (szText[index] - 0x20) * 5) : code_ascii_5x7;
		offset = (iHeight - 1 - y0) * iWidth + x0;

		for(n= 0; n < 5; n++)
		{
			for(m = 0; m < 8; m++)
			{
				if(pch[n] & (1<<m)) bits[offset - m * iWidth + n] = clText;
			}
		}

		index++;
		x0 += 6;
	}
}