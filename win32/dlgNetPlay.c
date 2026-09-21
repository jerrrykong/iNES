// =====================================================================
// iNES win32 前端 —— "联网对战"对话框(手动输入 IP)
//
// 握手与帧缓存全部交给 comm/npsession(win32 与 macOS 共用同一份会话层), 本文件
// 只负责界面与 50ms 轮询 —— 与 mac/iNESNetPlayDialog.m 一一对应, 两端行为一致。
// 原实现把状态机写在本地(dlgNetPlay_TimedCheck), 已随会话层下沉而移除。
// =====================================================================

#include "stdafx.h"
#include "i18n_ui.h"
#include "../comm/net.h"
#include "../comm/npsession.h"
#include "Resource.h"
#include "dlgNetPlay.h"


#define NETPLAY_MSG_MAX   256


// iNES.c 提供的配置读取(缓冲帧数在 config.ini 的 [netplay] cache_num)
extern ines_int_t  GetConfigInt(ines_cstr_t sec, ines_cstr_t key, ines_int_t def);

static int           s_is_server  = 0;
static int           s_connecting = 0;
static ines_dword_t  s_crc32      = 0;


static INT_PTR CALLBACK dlgNetPlay_DlgProc(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam);
static BOOL dlgNetPlay_OnInitDialog(HWND hDlg);
static VOID dlgNetPlay_OnStartConnect(HWND hDlg);
static VOID dlgNetPlay_TimedCheck(HWND hDlg);

static VOID dlgNetPlay_SetInfo(HWND hDlg, const char* utf8);
static VOID dlgNetPlay_SetInfoText(HWND hDlg, ines_cstr_t text);
static VOID dlgNetPlay_StopConnecting(HWND hDlg);


/**
 * IDD_NETPLAY 的静态文本(key 取自 lang/*.ini 的 [dialog] 段)。
 * 编辑框 / IDC_CMB_CACHE 的"值"不翻译; IDC_NETPLAY_LAB_CACHE 是"缓冲帧数"标签。
 */
static const APP_DLG_ITEM  s_netplay_items[] =
{
	{ IDC_NETPLAY_GRP_RUN_AS,   "dialog.netplay.run_as",  APP_FIT_GROW_W },
	{ IDC_RAD_SERVER,           "dialog.netplay.server",  APP_FIT_NONE },
	{ IDC_RAD_CLIENT,           "dialog.netplay.client",  APP_FIT_NONE },
	{ IDC_NETPLAY_LAB_ADDRESS,  "dialog.netplay.address", APP_FIT_NONE },
	{ IDC_NETPLAY_LAB_PORT,     "dialog.netplay.port",    APP_FIT_NONE },
	{ IDC_NETPLAY_LAB_CACHE,    "dialog.cache_frames",    APP_FIT_NONE },
	{ IDOK,                     "dialog.netplay.start",   APP_FIT_ANCHOR_RIGHT },
	{ IDCANCEL,                 "dialog.netplay.cancel",  APP_FIT_ANCHOR_RIGHT },
};


BOOL dlgNetPlay_DoModal(HINSTANCE hInstance, HWND hParentWnd, ines_dword_t  crc32)
{
	INT_PTR iRet;

	s_crc32 = crc32;

	iRet = DialogBox(hInstance, MAKEINTRESOURCE(IDD_NETPLAY), hParentWnd, dlgNetPlay_DlgProc);

	if(iRet == IDOK)
		return TRUE;

	return FALSE;
}




static INT_PTR CALLBACK dlgNetPlay_DlgProc(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam)
{

	switch (message)
	{
	case WM_INITDIALOG:
		return (INT_PTR)dlgNetPlay_OnInitDialog(hDlg);
	case WM_COMMAND:
		if (LOWORD(wParam) == IDOK )
		{
			// start connect
			dlgNetPlay_OnStartConnect(hDlg);
			return (INT_PTR)TRUE;
		}
		else if(LOWORD(wParam) == IDCANCEL)
		{
			// 握手进行中: 先停下, 不关对话框(与 mac 的"取消连接"一致)
			if(s_connecting)
			{
				dlgNetPlay_StopConnecting(hDlg);
				return (INT_PTR)TRUE;
			}

			KillTimer(hDlg, 100);
			np_end();
			EndDialog(hDlg, IDCANCEL);
			return (INT_PTR)TRUE;
		}
		else if(LOWORD(wParam) >= IDC_RAD_SERVER && LOWORD(wParam) <= IDC_RAD_CLIENT)
		{
			EnableWindow(GetDlgItem(hDlg, IDC_EDT_IP), IsDlgButtonChecked(hDlg, IDC_RAD_CLIENT));
			return (INT_PTR)TRUE;
		}
		break;
	case WM_TIMER:
		dlgNetPlay_TimedCheck(hDlg);
		return (INT_PTR)TRUE;
		break;
	case WM_APP_LANGCHANGED:      /* 语言切换: 重贴标题与静态文本 */
		SetWindowText(hDlg, L10N("dialog.netplay.title"));
		i18n_ui_apply_dialog(hDlg, s_netplay_items, count_of(s_netplay_items));
		return (INT_PTR)TRUE;
	}
	return (INT_PTR)FALSE;

}


static BOOL dlgNetPlay_OnInitDialog(HWND hDlg)
{
	ines_char_t  text[128];
	int          iCache;

	// 缓冲帧数: 只读显示。唯一来源是 config.ini 的 [netplay] cache_num
	// (未配置则用 NP_CACHE_DEFAULT), 与 mac 端一致 —— 不提供下拉框,
	// 客户端填什么都不生效(一律以服务端下发的为准)。
	iCache = (int)GetConfigInt(ISTR("netplay"), ISTR("cache_num"), NP_CACHE_DEFAULT);

	if(iCache < NP_CACHE_MIN)
		iCache = NP_CACHE_MIN;

	if(iCache > NP_CACHE_MAX)
		iCache = NP_CACHE_MAX;

	/* "%d frames" —— 与 mac 端的只读显示一致(config.ini 提示在 lang 文件里说明) */
	ines_strncpy(text, L10NF("dialog.lan.cache_frames_format", iCache), count_of(text) - 1);
	text[count_of(text) - 1] = 0;

	SetDlgItemText(hDlg, IDC_CMB_CACHE, text);

	// clear status text
	SetDlgItemText(hDlg, IDC_LAB_INFO, _T(""));

	// i18n: 标题与静态文本(含按钮), 控件尺寸按译文自适应
	SetWindowText(hDlg, L10N("dialog.netplay.title"));
	i18n_ui_apply_dialog(hDlg, s_netplay_items, count_of(s_netplay_items));
	
	
	CheckRadioButton(hDlg, IDC_RAD_SERVER, IDC_RAD_CLIENT, IDC_RAD_SERVER);
	EnableWindow(GetDlgItem(hDlg, IDC_EDT_IP), FALSE);

	// set default ip 	
	SetDlgItemText(hDlg, IDC_EDT_IP, _T("127.0.0.1"));
	SetDlgItemText(hDlg, IDC_EDT_PORT, _T("8891"));

	s_is_server  = 0;
	s_connecting = 0;

	return TRUE;
}


/**
 * 显示会话层给出的提示文本(统一 UTF-8), 转成 TCHAR 再交给控件。
 */
static VOID dlgNetPlay_SetInfo(HWND hDlg, const char* utf8)
{
	ines_char_t  text[NETPLAY_MSG_MAX];

	if(utf8 == NULL)
	{
		text[0] = 0;
	}
#ifdef UNICODE
	else if(0 == MultiByteToWideChar(CP_UTF8, 0, utf8, -1, text, (int)count_of(text)))
	{
		text[0] = 0;
	}
#else
	else
	{
		ines_strncpy(text, utf8, count_of(text) - 1);
		text[count_of(text) - 1] = 0;
	}
#endif

	SetDlgItemText(hDlg, IDC_LAB_INFO, text);
}

/** 显示语言文件里的文本(已是 TCHAR, 无需再转码) */
static VOID dlgNetPlay_SetInfoText(HWND hDlg, ines_cstr_t text)
{
	SetDlgItemText(hDlg, IDC_LAB_INFO, (text != NULL) ? text : ISTR(""));
}

/** 停止握手并恢复界面(失败或用户取消连接)。 */
static VOID dlgNetPlay_StopConnecting(HWND hDlg)
{
	KillTimer(hDlg, 100);

	if(s_connecting)
	{
		np_end();
		s_connecting = 0;
	}

	dlgNetPlay_SetInfo(hDlg, "");
	EnableWindow(GetDlgItem(hDlg, IDOK), TRUE);
}


static VOID dlgNetPlay_OnStartConnect(HWND hDlg)
{
	ines_char_t   szIP[128];
	int           iPort;
	int           iCache;
	BOOL          b;

	if(s_connecting)
		return;

	GetDlgItemText(hDlg, IDC_EDT_IP, szIP, (int)count_of(szIP));
	iPort = GetDlgItemInt(hDlg, IDC_EDT_PORT, &b, FALSE);

	if(!b)
	{
		// 与 mac 一致: 弹窗给完整原因, 状态行给短提示
		MessageBox(hDlg, L10N("dialog.netplay.err_port"), L10N("dialog.netplay.title"), MB_OK|MB_ICONSTOP);
		dlgNetPlay_SetInfoText(hDlg, L10N("dialog.netplay.err_port_short"));
		return;
	}

	// 缓冲帧数一律从 config.ini 的 [netplay] cache_num 读取(与 mac 端一致;
	// 未配置时用 NP_CACHE_DEFAULT)。客户端传多少都无效 —— 以服务端下发的为准,
	// 两端必须一致, 所以不能有"各用各的默认值"这种情况。
	iCache = (int)GetConfigInt(ISTR("netplay"), ISTR("cache_num"), NP_CACHE_DEFAULT);

	if(iCache < NP_CACHE_MIN)
		iCache = NP_CACHE_MIN;

	if(iCache > NP_CACHE_MAX)
		iCache = NP_CACHE_MAX;

	s_is_server = IsDlgButtonChecked(hDlg, IDC_RAD_SERVER) ? 1 : 0;

	// 开始握手: 服务端监听, 客户端连接(会话层内部状态机与 mac 端完全一致)
	if(0 != np_begin(s_is_server, szIP, iPort, s_crc32, iCache))
	{
		MessageBox(hDlg, net_get_last_error(), L10N("dialog.netplay.title"), MB_OK|MB_ICONSTOP);
		return;	
	}

	s_connecting = 1;

	EnableWindow(GetDlgItem(hDlg, IDOK), FALSE);

	dlgNetPlay_SetInfoText(hDlg, L10N(s_is_server ? "dialog.netplay.waiting" : "dialog.netplay.connecting"));

	// timed check(与 mac 的 0.05s 定时器一致)
	SetTimer(hDlg, 100, 50, NULL);
}


static VOID dlgNetPlay_TimedCheck(HWND hDlg)
{
	char  msg[NETPLAY_MSG_MAX];
	int   rc;

	msg[0] = 0;

	rc = np_poll(msg, (ines_size_t)sizeof(msg));

	dlgNetPlay_SetInfo(hDlg, msg);

	if(rc == NP_POLL_OK)
	{
		KillTimer(hDlg, 100);
		s_connecting = 0;

		EndDialog(hDlg, IDOK);
		return;
	}

	if(rc == NP_POLL_FAILED)
	{
		ines_char_t  text[NETPLAY_MSG_MAX];

		dlgNetPlay_StopConnecting(hDlg);

		// 会话层已关闭链路, 这里只把原因弹出来
#ifdef UNICODE
		if(0 == MultiByteToWideChar(CP_UTF8, 0, msg, -1, text, (int)count_of(text)))
			text[0] = 0;
#else
		ines_strncpy(text, msg, count_of(text) - 1);
		text[count_of(text) - 1] = 0;
#endif

		MessageBox(hDlg, (text[0] != 0) ? text : L10N("dialog.netplay.connect_failed"),
				   L10N("dialog.netplay.title"), MB_OK|MB_ICONSTOP);
		return;
	}
}
