
#include "stdafx.h"
#include "../comm/log.h"
#include "../comm/net.h"
#include "Resource.h"
#include "dlgNetPlay.h"
#include <time.h>


// 联网对战的缓冲帧数(定义在 iNES.c)。原实现只把 1~5 填进下拉框却从未写回,
// 选择项实际不生效, 这里在点"开始"时写回(只有"服务器"侧可设, 与下拉框的可用
// 状态一致; cache_add_mine() 按 net_cache_num 定位写入槽位, 故必须夹到合法范围)。
extern  ines_int_t  net_cache_num;

#define NETPLAY_CACHE_MIN   1
#define NETPLAY_CACHE_MAX   5


static int s_is_server = 0;
static int s_status = NET_ST_NONE;
static time_t  s_status_time = 0;
static ines_dword_t s_crc32 = 0;

static INT_PTR CALLBACK dlgNetPlay_DlgProc(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam);
static BOOL dlgNetPlay_OnInitDialog(HWND hDlg);
static VOID dlgNetPlay_OnNotify(HWND hDlg, UINT nID, LPNMHDR lpNMHDR);

static VOID dlgNetPlay_OnStartConnect(HWND hDlg);
static VOID dlgNetPlay_TimedCheck(HWND hDlg);


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
	case WM_NOTIFY:
		dlgNetPlay_OnNotify(hDlg, (UINT)wParam, (LPNMHDR)lParam);
		break;
	case WM_COMMAND:
		if (LOWORD(wParam) == IDOK )
		{
			// start connect
			dlgNetPlay_OnStartConnect(hDlg);
			return (INT_PTR)TRUE;
		}
		else if(LOWORD(wParam) == IDCANCEL)
		{
			// cancel net mode dialog
			KillTimer(hDlg, 100);
			net_close();
			if(s_status == NET_ST_NONE)
			{
				EndDialog(hDlg, LOWORD(wParam));
			}
			else
			{
				s_status = NET_ST_NONE;
				SetDlgItemText(hDlg, IDC_LAB_INFO, ISTR(""));
				EnableWindow(GetDlgItem(hDlg, IDOK), TRUE);
			}
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
	SendDlgItemMessage(hDlg, IDC_CMB_CACHE, CB_SETCURSEL, 3, 0);



	// clear status text
	SetDlgItemText(hDlg, IDC_LAB_INFO, _T(""));
	
	
	CheckRadioButton(hDlg, IDC_RAD_SERVER, IDC_RAD_CLIENT, IDC_RAD_SERVER);
	EnableWindow(GetDlgItem(hDlg, IDC_EDT_IP), FALSE);
	EnableWindow(GetDlgItem(hDlg, IDC_CMB_CACHE), TRUE);

	// set default ip 	
	SetDlgItemText(hDlg, IDC_EDT_IP, _T("127.0.0.1"));
	SetDlgItemText(hDlg, IDC_EDT_PORT, _T("8891"));

	s_status = NET_ST_NONE;

	return TRUE;
}


static VOID dlgNetPlay_OnNotify(HWND hDlg, UINT nID, LPNMHDR lpNMHDR)
{
}



static VOID dlgNetPlay_OnStartConnect(HWND hDlg)
{
	ines_char_t   szIP[128];
	int           iPort;
	BOOL          b;

	GetDlgItemText(hDlg, IDC_EDT_IP, szIP, 128);
	iPort = GetDlgItemInt(hDlg, IDC_EDT_PORT, &b, FALSE);

	if(!b)
	{
		MessageBox(hDlg, ISTR("error input port"), ISTR("iNes"), MB_OK|MB_ICONSTOP);
		return;
	}

	// 写回"缓冲帧数"(仅服务器侧可设)
	if(IsDlgButtonChecked(hDlg, IDC_RAD_SERVER))
	{
		int  iSel = (int)SendDlgItemMessage(hDlg, IDC_CMB_CACHE, CB_GETCURSEL, 0, 0);

		if(iSel >= 0)
			net_cache_num = iSel + 1;

		if(net_cache_num < NETPLAY_CACHE_MIN)
			net_cache_num = NETPLAY_CACHE_MIN;

		if(net_cache_num > NETPLAY_CACHE_MAX)
			net_cache_num = NETPLAY_CACHE_MAX;
	}

	net_close();

	s_is_server = IsDlgButtonChecked(hDlg, IDC_RAD_SERVER);

	// start connect
	if(s_is_server)
	{
		// run as a server
		if( 0 != net_listen(ISTR("0.0.0.0"), iPort))
		{
			MessageBox(hDlg, net_get_last_error(), ISTR("iNes"), MB_OK|MB_ICONSTOP);
			return;	
		}
		
		SetDlgItemText(hDlg, IDC_LAB_INFO, ISTR("等待客户端的连接..."));
		
	}
	else
	{
		// run as a client

		if( 0 != net_connect(szIP, iPort))
		{
			MessageBox(hDlg, net_get_last_error(), ISTR("iNes"), MB_OK|MB_ICONSTOP);
			return;	
		}

		SetDlgItemText(hDlg, IDC_LAB_INFO, ISTR("正在连接到服务器..."));
	}

	// timed check 

	EnableWindow(GetDlgItem(hDlg, IDOK), FALSE);
	// EnableWindow(GetDlgItem(hDlg, IDCANCEL), FALSE);

	s_status = NET_ST_WAIT_CONN;
	s_status_time = time(NULL);

	SetTimer(hDlg, 100, 50, NULL);
}


static VOID dlgNetPlay_ConnectError(HWND hDlg, ines_cstr_t msg, void* rsp_data, int len)
{
	s_status = NET_ST_NONE;
	s_status_time = 0;
	KillTimer(hDlg, 100);
	if(rsp_data != NULL)
	{
		net_send(rsp_data, len);
	}
	MessageBox(hDlg, msg, ISTR("iNes"), MB_OK|MB_ICONSTOP);
	net_close();
	SetDlgItemText(hDlg, IDC_LAB_INFO, ISTR(""));
	EnableWindow(GetDlgItem(hDlg, IDOK), TRUE);
}

static VOID dlgNetPlay_TimedCheck(HWND hDlg)
{
	if(s_is_server)
	{
		switch(s_status)
		{
		case NET_ST_WAIT_CONN:
			if(!net_is_connected())
				return;
			SetDlgItemText(hDlg, IDC_LAB_INFO, ISTR("连接成功，等待验证..."));
			s_status = NET_ST_WAIT_START;
			s_status_time = time(NULL);
			break;
		case NET_ST_WAIT_START:
			//if(net_check_recv() >= sizeof(nst))
			{
				struct _net_start  nst;
				struct _net_start_rsp  rsp;
				rsp.cmd = NET_CMD_START_RSP;

				if(0 == net_pick_recv_data(&nst, sizeof(nst)) )
				{ 
					net_del_recv_data(sizeof(nst));
					if( nst.cmd != NET_CMD_START )
					{
						dlgNetPlay_ConnectError(hDlg, ISTR("连接错误!"), NULL, 0);
					} 
					else if( nst.ver != NET_VER )
					{
						rsp.code = 1;
						dlgNetPlay_ConnectError(hDlg, ISTR("版本不匹配!"), &rsp, sizeof(rsp));
					}
					else if( nst.crc32 != s_crc32 )
					{
						rsp.code = 2;
						dlgNetPlay_ConnectError(hDlg, ISTR("ROM不匹配!"),  &rsp, sizeof(rsp));
					}
					else
					{
						rsp.code = 0;
						rsp.is_ntsc = 1;
						net_send(&rsp, sizeof(rsp));
						EndDialog(hDlg, IDOK);
					}
					break;
				}
			}
			if(s_status_time + 5 < time(NULL))
			{
				// check verify timeout
				dlgNetPlay_ConnectError(hDlg, ISTR("客户端验证超时!"), NULL, 0);
				break;
			}
			break;
		}
	}
	else
	{
		switch(s_status)
		{
		case NET_ST_WAIT_CONN:
			if(net_is_connect_failed())
			{
				dlgNetPlay_ConnectError(hDlg, net_get_last_error(), NULL, 0);
				break;
			}
			if(!net_is_connected())
			{
				return;
			}

			{
				struct _net_start   nst;
				nst.cmd = NET_CMD_START;
				nst.ver = NET_VER;
				nst.crc32 = s_crc32;

				net_send(&nst, sizeof(nst));

			}
			SetDlgItemText(hDlg, IDC_LAB_INFO, ISTR("连接成功，等待验证..."));
			s_status = NET_ST_WAIT_START;
			s_status_time = time(NULL);
			break;
		case NET_ST_WAIT_START:
			//if(net_check_recv() >= sizeof(nst))
			{
				struct _net_start_rsp  rsp;

				if(0 == net_pick_recv_data(&rsp, sizeof(rsp)) )
				{
					net_del_recv_data(sizeof(rsp));
					if( rsp.cmd != NET_CMD_START_RSP )
					{
						dlgNetPlay_ConnectError(hDlg, ISTR("连接错误!"), NULL, 0);
					} 
					else if(rsp.code == 0)
					{
						// set ntsc...
						// todo:
						EndDialog(hDlg, IDOK);
					}
					else if( rsp.code  == 1 )
					{
						dlgNetPlay_ConnectError(hDlg, ISTR("版本不匹配!"), NULL, 0);
					}
					else if( rsp.code == 2 )
					{
						dlgNetPlay_ConnectError(hDlg, ISTR("ROM不匹配!"), NULL, 0);
					}
					else
					{
						dlgNetPlay_ConnectError(hDlg, ISTR("连接错误!"), NULL, 0);
					}
					break;
				}
			}
			if(s_status_time + 5 < time(NULL))
			{
				// check verify timeout
				dlgNetPlay_ConnectError(hDlg, ISTR("客户端验证超时!"), NULL, 0);
				break;
			}
			break;
		}
	}
}
