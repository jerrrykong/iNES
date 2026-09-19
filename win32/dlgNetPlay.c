// =====================================================================
// iNES win32 前端 —— "联网对战"对话框(手动输入 IP)
//
// 握手与帧缓存全部交给 comm/npsession(win32 与 macOS 共用同一份会话层), 本文件
// 只负责界面与 50ms 轮询 —— 与 mac/iNESNetPlayDialog.m 一一对应, 两端行为一致。
// 原实现把状态机写在本地(dlgNetPlay_TimedCheck), 已随会话层下沉而移除。
// =====================================================================

#include "stdafx.h"
#include "../comm/net.h"
#include "../comm/npsession.h"
#include "Resource.h"
#include "dlgNetPlay.h"


#define NETPLAY_MSG_MAX   256


static int           s_is_server  = 0;
static int           s_connecting = 0;
static ines_dword_t  s_crc32      = 0;


static INT_PTR CALLBACK dlgNetPlay_DlgProc(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam);
static BOOL dlgNetPlay_OnInitDialog(HWND hDlg);
static VOID dlgNetPlay_OnStartConnect(HWND hDlg);
static VOID dlgNetPlay_TimedCheck(HWND hDlg);

static VOID dlgNetPlay_SetInfo(HWND hDlg, const char* utf8);
static VOID dlgNetPlay_StopConnecting(HWND hDlg);


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
			EnableWindow(GetDlgItem(hDlg, IDC_CMB_CACHE), IsDlgButtonChecked(hDlg, IDC_RAD_SERVER));
			return (INT_PTR)TRUE;
		}
		break;
	case WM_TIMER:
		dlgNetPlay_TimedCheck(hDlg);
		return (INT_PTR)TRUE;
		break;
	}
	return (INT_PTR)FALSE;

}


static BOOL dlgNetPlay_OnInitDialog(HWND hDlg)
{
	// init listbox
	SendDlgItemMessage(hDlg, IDC_CMB_CACHE, CB_ADDSTRING, 0, (LPARAM)_T("1"));
	SendDlgItemMessage(hDlg, IDC_CMB_CACHE, CB_ADDSTRING, 0, (LPARAM)_T("2"));
	SendDlgItemMessage(hDlg, IDC_CMB_CACHE, CB_ADDSTRING, 0, (LPARAM)_T("3"));
	SendDlgItemMessage(hDlg, IDC_CMB_CACHE, CB_ADDSTRING, 0, (LPARAM)_T("4"));
	SendDlgItemMessage(hDlg, IDC_CMB_CACHE, CB_ADDSTRING, 0, (LPARAM)_T("5"));
	// select 4 
	SendDlgItemMessage(hDlg, IDC_CMB_CACHE, CB_SETCURSEL, NP_CACHE_DEFAULT - NP_CACHE_MIN, 0);



	// clear status text
	SetDlgItemText(hDlg, IDC_LAB_INFO, _T(""));
	
	
	CheckRadioButton(hDlg, IDC_RAD_SERVER, IDC_RAD_CLIENT, IDC_RAD_SERVER);
	EnableWindow(GetDlgItem(hDlg, IDC_EDT_IP), FALSE);
	EnableWindow(GetDlgItem(hDlg, IDC_CMB_CACHE), TRUE);

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
		MessageBox(hDlg, ISTR("error input port"), ISTR("iNes"), MB_OK|MB_ICONSTOP);
		return;
	}

	// 缓冲帧数只有服务端侧可设(与下拉框的可用状态一致); 客户端以服务端下发的为准
	iCache = NP_CACHE_DEFAULT;

	if(IsDlgButtonChecked(hDlg, IDC_RAD_SERVER))
	{
		int  iSel = (int)SendDlgItemMessage(hDlg, IDC_CMB_CACHE, CB_GETCURSEL, 0, 0);

		if(iSel >= 0)
			iCache = iSel + NP_CACHE_MIN;

		if(iCache < NP_CACHE_MIN)
			iCache = NP_CACHE_MIN;

		if(iCache > NP_CACHE_MAX)
			iCache = NP_CACHE_MAX;
	}

	s_is_server = IsDlgButtonChecked(hDlg, IDC_RAD_SERVER) ? 1 : 0;

	// 开始握手: 服务端监听, 客户端连接(会话层内部状态机与 mac 端完全一致)
	if(0 != np_begin(s_is_server, szIP, iPort, s_crc32, iCache))
	{
		MessageBox(hDlg, net_get_last_error(), ISTR("iNes"), MB_OK|MB_ICONSTOP);
		return;	
	}

	s_connecting = 1;

	EnableWindow(GetDlgItem(hDlg, IDOK), FALSE);

	dlgNetPlay_SetInfo(hDlg, s_is_server ? "等待客户端的连接..." : "正在连接到服务器...");

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

		MessageBox(hDlg, (text[0] != 0) ? text : ISTR("连接失败"), ISTR("iNes"), MB_OK|MB_ICONSTOP);
		return;
	}
}
