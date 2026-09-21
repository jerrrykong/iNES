// =====================================================================
// iNES win32 前端 —— "局域网快速对战"对话框(快速配对)
//
// 与 mac/iNESLanLobby.m 一一对应:
//   * 面板一打开即以服务端身份发布房间(UDP 8892 广播 + TCP 监听);
//   * 500ms 定时器推进握手并刷新房间列表, 每 1s 广播一次自己的房间;
//   * 双击房间或点"加入" -> 停广播 -> np_begin(client) 接入对端。
//
// 握手与帧缓存全部由 comm/npsession 提供(与 macOS 端共用同一份实现), 本文件只
// 负责界面与定时器, 不含任何协议逻辑。
// =====================================================================

#include "stdafx.h"
#include "i18n_ui.h"
#include "../comm/lan.h"
#include "../comm/net.h"
#include "../comm/npsession.h"
#include "../comm/log.h"
#include "Resource.h"
#include "dlgLanMatch.h"

#include <time.h>


#define LANMATCH_TIMER_ID     100
#define LANMATCH_TIMER_MS     500
#define LANMATCH_ADV_SEC      1

// 默认对战端口: 被占用时自动改用系统分配端口(与"联网对战..."的默认值一致)
#define LANMATCH_PORT         8891

#define LANMATCH_MSG_MAX      256

// 昵称在 config.ini 中的位置(与 mac 端一致, 两端各自保存)
#define LANMATCH_CFG_SECTION  ISTR("netplay")
#define LANMATCH_CFG_NICKKEY  ISTR("nickname")


// iNES.c 提供的配置读写与当前 ROM 标题
extern ines_cstr_t  GetConfigStr(ines_cstr_t sec, ines_cstr_t key, ines_cstr_t def);
extern void         SetConfigStr(ines_cstr_t sec, ines_cstr_t key, ines_cstr_t val);
extern ines_int_t   GetConfigInt(ines_cstr_t sec, ines_cstr_t key, ines_int_t def);
extern void         SetConfigInt(ines_cstr_t sec, ines_cstr_t key, ines_int_t val);
extern ines_char_t  szROMTitle[INES_MAX_PATH];


static ines_byte_t  s_peer_id[16];
static ines_dword_t s_crc32;
static lan_room_t   s_rooms[LAN_ROOM_MAX];
static int          s_room_count;
static time_t       s_last_adv;
static int          s_listen_port;
static int          s_connected;
static int          s_joining;
static int          s_cache_num;
static ines_char_t  s_fail_msg[LANMATCH_MSG_MAX];

// 当前选中房间(按 peer_id 记录, 列表刷新后据此恢复选中)
static ines_byte_t  s_sel_id[16];
static int          s_has_sel;


static INT_PTR CALLBACK dlgLanMatch_DlgProc(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam);
static BOOL  dlgLanMatch_OnInitDialog(HWND hDlg);
static void  dlgLanMatch_OnTimer(HWND hDlg);
static void  dlgLanMatch_OnJoin(HWND hDlg);
static BOOL  dlgLanMatch_OnItemChanged(HWND hDlg, NMLISTVIEW* pnm);
static BOOL  dlgLanMatch_OnCustomDraw(HWND hDlg, NMLVCUSTOMDRAW* pcd);

static void  dlgLanMatch_UpdateLabels(HWND hDlg);
static void  dlgLanMatch_UpdateColumns(HWND hList);
static void  dlgLanMatch_SetInfo(HWND hDlg, ines_cstr_t text);
static void  dlgLanMatch_SetInfoUtf8(HWND hDlg, const char* utf8);
static void  dlgLanMatch_RefreshInfo(HWND hDlg);
static void  dlgLanMatch_RefreshList(HWND hDlg);
static void  dlgLanMatch_CurrentNick(HWND hDlg, ines_str_t buf, int len);
static void  dlgLanMatch_SaveNick(HWND hDlg);
static int   dlgLanMatch_StartHosting(void);
static void  dlgLanMatch_ResumeHosting(HWND hDlg);
static void  dlgLanMatch_StopAll(void);

static int   lanmatch_default_nick_num(void);
static void  lanmatch_utf8_to_tchar(ines_str_t dst, int dst_chars, const char* src);


/**
 * 打开对话框: 先起服务端发布房间, 再进入模态循环; 结束后统一收尾。
 */
BOOL dlgLanMatch_DoModal(HINSTANCE hInstance, HWND hParentWnd, ines_dword_t crc32)
{
	INT_PTR  iRet;

	s_crc32       = crc32;
	s_room_count  = 0;
	s_last_adv    = 0;
	s_listen_port = 0;
	s_connected   = 0;
	s_joining     = 0;

	// 本机作为房主时的缓冲帧数: config.ini 的 [netplay] cache_num, 未配置则用默认。
	// 加入方不用这个数 —— 以房主下发的为准(两端必须一致)。
	s_cache_num   = (int)GetConfigInt(LANMATCH_CFG_SECTION, ISTR("cache_num"), NP_CACHE_DEFAULT);

	if(s_cache_num < NP_CACHE_MIN)
		s_cache_num = NP_CACHE_MIN;

	if(s_cache_num > NP_CACHE_MAX)
		s_cache_num = NP_CACHE_MAX;
	s_has_sel     = 0;
	s_fail_msg[0] = 0;

	lan_gen_peer_id(s_peer_id);

	if(!dlgLanMatch_StartHosting())
	{
		if(s_fail_msg[0] == 0)
			ines_strncpy(s_fail_msg, L10N("dialog.lan.err_generic"), count_of(s_fail_msg) - 1);

		s_fail_msg[count_of(s_fail_msg) - 1] = 0;

		MessageBox(hParentWnd, s_fail_msg, L10N("dialog.lan.title"), MB_OK|MB_ICONSTOP);

		dlgLanMatch_StopAll();

		return FALSE;
	}

	iRet = DialogBox(hInstance, MAKEINTRESOURCE(IDD_LANMATCH), hParentWnd, dlgLanMatch_DlgProc);

	dlgLanMatch_StopAll();

	INES_LOG(LOG_NTY, MOD_NET, ISTR("lan: dialog done, ret=%d, connected=%d\n"),
			 (int)iRet, s_connected);

	return (s_connected != 0) ? TRUE : FALSE;
}


// ---------------------------------------------------------------------
// 工具
// ---------------------------------------------------------------------

/** 默认昵称的随机尾号(Player + 4 位数字)。 */
static int lanmatch_default_nick_num(void)
{
	return 1000 + (int)(GetTickCount() % 9000);
}

/** 把会话层给出的 UTF-8 提示文本转成 TCHAR 再显示。 */
static void lanmatch_utf8_to_tchar(ines_str_t dst, int dst_chars, const char* src)
{
	if((dst == NULL) || (dst_chars <= 0))
		return;

	if(src == NULL)
	{
		dst[0] = 0;
		return;
	}

#ifdef UNICODE
	if(0 == MultiByteToWideChar(CP_UTF8, 0, src, -1, dst, dst_chars))
		dst[0] = 0;
#else
	ines_strncpy(dst, src, dst_chars - 1);
	dst[dst_chars - 1] = 0;
#endif
}


// ---------------------------------------------------------------------
// 发布 / 接入
// ---------------------------------------------------------------------

/** 以服务端身份开始发布: 开发现通道 + 监听(端口被占用则交系统分配)。 */
static int dlgLanMatch_StartHosting(void)
{
	if(0 != lan_open(s_peer_id))
	{
		ines_strncpy(s_fail_msg, L10N("dialog.lan.err_open_failed"), count_of(s_fail_msg) - 1);
		s_fail_msg[count_of(s_fail_msg) - 1] = 0;
		return 0;
	}

	if(0 != np_begin(1, NULL, LANMATCH_PORT, s_crc32, s_cache_num))
	{
		if(0 != np_begin(1, NULL, 0, s_crc32, s_cache_num))   // 8891 被占用
		{
			ines_strncpy(s_fail_msg, net_get_last_error(), count_of(s_fail_msg) - 1);
			s_fail_msg[count_of(s_fail_msg) - 1] = 0;

			lan_close();
			return 0;
		}
	}

	s_listen_port = (int)net_get_local_port();
	s_room_count  = 0;
	s_last_adv    = 0;
	s_joining     = 0;
	s_has_sel     = 0;

	INES_LOG(LOG_NTY, MOD_NET, ISTR("lan: hosting at tcp %d\n"), s_listen_port);

	return 1;
}

/** 收尾: 关发现通道; 未配对成功时同时结束会话。 */
static void dlgLanMatch_StopAll(void)
{
	lan_close();

	if(!s_connected)
		np_end();

	s_room_count = 0;
}

/** 恢复发布状态(握手失败 / 取消加入后重来一次; 服务端仍在等待其他人)。 */
static void dlgLanMatch_ResumeHosting(HWND hDlg)
{
	np_end();
	lan_close();

	if(!dlgLanMatch_StartHosting())
	{
		KillTimer(hDlg, LANMATCH_TIMER_ID);
		dlgLanMatch_SetInfo(hDlg, (s_fail_msg[0] != 0) ? s_fail_msg : L10N("dialog.lan.err_resume_failed"));
		return;
	}

	dlgLanMatch_RefreshInfo(hDlg);
}


// ---------------------------------------------------------------------
// 界面
// ---------------------------------------------------------------------

/**
 * IDD_LANMATCH 的静态文本(key 全部在 lang/*.ini 的 [dialog] 段)。
 * IDC_LANMATCH_EDT_NICK 是"值"、IDC_LANMATCH_CMB_CACHE 是只读显示, 都不翻译。
 */
static const APP_DLG_ITEM  s_lanmatch_items[] =
{
	{ IDC_LANMATCH_LAB_NICK,   "dialog.lan.nickname",     APP_FIT_NONE },
	{ IDC_LANMATCH_LAB_CACHE,  "dialog.cache_frames",     APP_FIT_NONE },
	{ IDC_LANMATCH_LAB_HINT,   "dialog.lan.hint_format",  APP_FIT_WRAP },
	{ IDOK,                    "dialog.lan.join",         APP_FIT_ANCHOR_RIGHT },
	{ IDCANCEL,                "dialog.lan.close",        APP_FIT_ANCHOR_RIGHT },
};

/** 房间列表的列: 文本走译文, 列宽与 mac 端一致(110/176/68/56)。 */
static const struct
{
	const char*  key;
	int          width;
} s_lanmatch_cols[] =
{
	{ "dialog.lan.col_nick",  110 },
	{ "dialog.lan.col_rom",   176 },
	{ "dialog.lan.col_ver",    68 },
	{ "dialog.lan.col_cache",  56 },
};

/**
 * 贴标题与所有 dialog.lan.* 静态文本。
 * IDC_LANMATCH_LAB_HINT 是格式串(带缓冲帧数), 这里先给足%d 的译文, 
 * 帧数本身不入语言文件(与 mac 一致: 数字是数据, 不是文本)。
 */
static void dlgLanMatch_ApplyLanguage(HWND hDlg)
{
	SetWindowText(hDlg, L10N("dialog.lan.title"));
	i18n_ui_apply_dialog(hDlg, s_lanmatch_items, count_of(s_lanmatch_items));
}

/** 写列表表头: 列已存在则只改文本(语言切换时用), 否则插入新列。 */
static void dlgLanMatch_UpdateColumns(HWND hList)
{
	HWND      hHeader;
	LVCOLUMN  col;
	int       nExist;
	int       i;

	if(hList == NULL)
		return;

	hHeader = ListView_GetHeader(hList);
	nExist  = (hHeader != NULL) ? Header_GetItemCount(hHeader) : 0;

	memset(&col, 0, sizeof(col));
	col.mask = LVCF_TEXT|LVCF_WIDTH|LVCF_SUBITEM;

	for(i = 0; i < (int)count_of(s_lanmatch_cols); i++)
	{
		col.pszText  = (ines_str_t)L10N(s_lanmatch_cols[i].key);
		col.cx       = s_lanmatch_cols[i].width;
		col.iSubItem = i;

		if(i < nExist)
			ListView_SetColumn(hList, i, &col);
		else
			ListView_InsertColumn(hList, i, &col);
	}

	i18n_ui_fit_columns(hList);
}

/** 随当次会话变化的三个文本: 本机 ROM / 说明(含缓冲帧数) / 缓冲帧数值。 */
static void dlgLanMatch_UpdateLabels(HWND hDlg)
{
	ines_char_t  text[512];

	ines_snprintf(text, count_of(text), L10N("dialog.lan.rom_prefix_format"), szROMTitle);
	text[count_of(text) - 1] = 0;
	SetDlgItemText(hDlg, IDC_LANMATCH_LAB_ROM, text);

	ines_snprintf(text, count_of(text), L10N("dialog.lan.hint_format"), s_cache_num);
	text[count_of(text) - 1] = 0;
	SetDlgItemText(hDlg, IDC_LANMATCH_LAB_HINT, text);

	/* "%d frames" —— 配置来源写在 hint 里, 这里只显示数值 */
	ines_strncpy(text, L10NF("dialog.lan.cache_frames_format", s_cache_num), count_of(text) - 1);
	text[count_of(text) - 1] = 0;
	SetDlgItemText(hDlg, IDC_LANMATCH_CMB_CACHE, text);
}

static void dlgLanMatch_SetInfo(HWND hDlg, ines_cstr_t text)
{
	SetDlgItemText(hDlg, IDC_LANMATCH_LAB_INFO, (text != NULL) ? text : ISTR(""));
}

static void dlgLanMatch_SetInfoUtf8(HWND hDlg, const char* utf8)
{
	ines_char_t  text[LANMATCH_MSG_MAX];

	lanmatch_utf8_to_tchar(text, (int)count_of(text), utf8);

	dlgLanMatch_SetInfo(hDlg, text);
}

/**
 * 空闲状态下的提示文本(房间数不入语言文件, 由 dialog.lan.*_format 组合)。
 * 0 个房间时用 dialog.lan.no_rooms(把"需同一局域网 / 防火墙"的原因一起说了)。
 */
static void dlgLanMatch_RefreshInfo(HWND hDlg)
{
	ines_char_t  text[256];

	if(s_room_count <= 0)
	{
		ines_strncpy(text, L10N("dialog.lan.no_rooms"), count_of(text) - 1);
		text[count_of(text) - 1] = 0;
	}
	else
		ines_snprintf(text, count_of(text), L10N("dialog.lan.rooms_format"), s_room_count);

	text[count_of(text) - 1] = 0;

	dlgLanMatch_SetInfo(hDlg, text);
}

/** 取当前界面上的昵称(去掉首尾空白, 空则回退默认值)。 */
static void dlgLanMatch_CurrentNick(HWND hDlg, ines_str_t buf, int len)
{
	ines_char_t  raw[128];
	int          i;
	int          j;

	if((buf == NULL) || (len <= 0))
		return;

	buf[0] = 0;

	GetDlgItemText(hDlg, IDC_LANMATCH_EDT_NICK, raw, (int)count_of(raw));
	raw[count_of(raw) - 1] = 0;

	for(i = 0; (raw[i] != 0) && ((raw[i] == ' ') || (raw[i] == '\t')); i++)
		;

	j = (int)_tcslen(raw + i);

	while((j > 0) && ((raw[i + j - 1] == ' ') || (raw[i + j - 1] == '\t')))
		j--;

	if(j <= 0)
	{
		ines_snprintf(buf, len, ISTR("Player %04d"), lanmatch_default_nick_num());
		return;
	}

	if(j > (len - 1))
		j = len - 1;

	for(i = 0; i < j; i++)
		buf[i] = raw[i];

	buf[j] = 0;
}

/** 昵称写入 config.ini(持久化, 下次进入自动带上)。 */
static void dlgLanMatch_SaveNick(HWND hDlg)
{
	ines_char_t  nick[128];

	dlgLanMatch_CurrentNick(hDlg, nick, (int)count_of(nick));

	SetConfigStr(LANMATCH_CFG_SECTION, LANMATCH_CFG_NICKKEY, nick);
}

/** 按当前房间表重画列表(ROM 不同的房间灰显并加后缀)。 */
static void dlgLanMatch_RefreshList(HWND hDlg)
{
	HWND         hwndList;
	LVITEM       item;
	ines_char_t  text[256];
	int          i;
	int          sel_row;

	hwndList = GetDlgItem(hDlg, IDC_LANMATCH_LIST);

	ListView_DeleteAllItems(hwndList);

	for(i = 0; i < s_room_count; i++)
	{
		memset(&item, 0, sizeof(item));
		item.mask     = LVIF_TEXT|LVIF_PARAM;
		item.iItem    = i;
		item.iSubItem = 0;
		item.pszText  = s_rooms[i].nick;
		item.lParam   = (LPARAM)i;

		ListView_InsertItem(hwndList, &item);

		if(s_rooms[i].crc32 != s_crc32)
			ines_snprintf(text, count_of(text), ISTR("%s%s"), s_rooms[i].rom, L10N("dialog.lan.rom_diff_suffix"));
		else
		{
			ines_strncpy(text, s_rooms[i].rom, count_of(text) - 1);
			text[count_of(text) - 1] = 0;
		}

		ListView_SetItemText(hwndList, i, 1, text);

		// 协议版本: 与本机不同 -> 标注出来(选中被 OnItemChanged 拦掉, 不可加入)
		if(s_rooms[i].net_ver == (ines_dword_t)NET_VER)
			ines_snprintf(text, count_of(text), ISTR("%u"), (unsigned)NET_VER);
		else
			// beacon 未携带该字段(旧版)时版本号为 0, 同样按"需升级"显示
			ines_snprintf(text, count_of(text), L10N("dialog.lan.ver_upgrade_format"),
						  (unsigned)s_rooms[i].net_ver);

		text[count_of(text) - 1] = 0;

		ListView_SetItemText(hwndList, i, 2, text);

		// 缓冲帧数: 对端未携带该字段(旧版本)时显示 --
		if((s_rooms[i].cache_num >= NP_CACHE_MIN) && (s_rooms[i].cache_num <= NP_CACHE_MAX))
			ines_strncpy(text, L10NF("dialog.lan.cache_frames_format", (int)s_rooms[i].cache_num),
						 count_of(text) - 1);
		else
			ines_strncpy(text, ISTR("--"), count_of(text) - 1);   // 未知: 就是个符号, 不用翻译

		text[count_of(text) - 1] = 0;

		ListView_SetItemText(hwndList, i, 3, text);
	}

	// 按 peer_id 恢复选中(列表每次刷新都重建, 行号会变)
	sel_row = -1;

	if(s_has_sel)
	{
		for(i = 0; i < s_room_count; i++)
		{
			if(0 == memcmp(s_rooms[i].peer_id, s_sel_id, 16))
			{
				sel_row = i;
				break;
			}
		}
	}

	if(sel_row >= 0)
	{
		ListView_SetItemState(hwndList, sel_row, LVIS_SELECTED|LVIS_FOCUSED, LVIS_SELECTED|LVIS_FOCUSED);
	}
	else
	{
		s_has_sel = 0;
		EnableWindow(GetDlgItem(hDlg, IDOK), FALSE);
	}
}


// ---------------------------------------------------------------------
// 动作
// ---------------------------------------------------------------------

/** 加入选中的房间: 停广播 -> 以客户机身份连过去。 */
static void dlgLanMatch_OnJoin(HWND hDlg)
{
	HWND         hwndList;
	int          row;
	lan_room_t   room;
	ines_char_t  ip[64];
	ines_char_t  text[256];

	if(s_joining || s_connected)
		return;

	hwndList = GetDlgItem(hDlg, IDC_LANMATCH_LIST);
	row      = ListView_GetNextItem(hwndList, -1, LVNI_SELECTED);

	if((row < 0) || (row >= s_room_count))
	{
		dlgLanMatch_SetInfo(hDlg, L10N("dialog.lan.select_room_first"));
		return;
	}

	if(s_rooms[row].crc32 != s_crc32)
	{
		dlgLanMatch_SetInfo(hDlg, L10N("dialog.lan.err_rom_diff"));
		return;
	}

	// 协议版本必须相同(不同版本连上也会被握手拒绝, 这里直接拦住并提示升级)
	if(s_rooms[row].net_ver != (ines_dword_t)NET_VER)
	{
		dlgLanMatch_SetInfo(hDlg, L10N("dialog.lan.err_ver_diff"));
		return;
	}

	// 取最新的房间信息(可能刚好超时消失)
	if(0 != lan_find(s_rooms[row].peer_id, &room))
	{
		dlgLanMatch_SetInfo(hDlg, L10N("dialog.lan.err_room_gone"));
		return;
	}

	if(0 != lan_addr_str(room.addr, ip, (int)count_of(ip)))
	{
		dlgLanMatch_SetInfo(hDlg, L10N("dialog.lan.err_addr_invalid"));
		return;
	}

	dlgLanMatch_SaveNick(hDlg);

	// 放弃发布: 停广播(随后 np_begin(client) 会 net_close() 关掉监听, 因此
	// 本机不可能再被别人接入 —— "后加入者必为客户机")
	lan_close();
	s_room_count = 0;

	ListView_DeleteAllItems(hwndList);

	if(0 != np_begin(0, ip, (int)room.tcp_port, s_crc32, 0))
	{
		ines_strncpy(s_fail_msg, net_get_last_error(), count_of(s_fail_msg) - 1);
		s_fail_msg[count_of(s_fail_msg) - 1] = 0;

		dlgLanMatch_SetInfo(hDlg, s_fail_msg);
		dlgLanMatch_ResumeHosting(hDlg);
		return;
	}

	s_joining = 1;

	EnableWindow(GetDlgItem(hDlg, IDOK), FALSE);

	ines_snprintf(text, count_of(text), L10N("dialog.lan.joining_format"), room.nick);

	dlgLanMatch_SetInfo(hDlg, text);
}

// ---------------------------------------------------------------------
// 定时器
// ---------------------------------------------------------------------

static void dlgLanMatch_OnTimer(HWND hDlg)
{
	char         msg[LANMATCH_MSG_MAX];
	ines_char_t  nick[128];
	int          rc;

	// 1) 推进握手: 服务端有人连入会自动 accept 并校验; 客户端连上即发校验包
	msg[0] = 0;
	rc     = np_poll(msg, (ines_size_t)sizeof(msg));

	// 2) 发现: 仅发布状态下广播与刷新列表
	if(!s_joining)
	{
		time_t  now = time(NULL);

		if((now - s_last_adv) >= LANMATCH_ADV_SEC)
		{
			dlgLanMatch_CurrentNick(hDlg, nick, (int)count_of(nick));

			// 缓冲帧数随 beacon 一起广播: 加入方以房主的值为准
			lan_advertise(s_crc32, (ines_word_t)s_listen_port, 0, nick, szROMTitle,
						  (ines_byte_t)s_cache_num);

			s_last_adv = now;
		}

		s_room_count = lan_poll(s_rooms, LAN_ROOM_MAX);

		if(s_room_count < 0)
			s_room_count = 0;

		dlgLanMatch_RefreshList(hDlg);
	}

	// 3) 结果处理
	if(rc == NP_POLL_OK)
	{
		KillTimer(hDlg, LANMATCH_TIMER_ID);

		s_connected = 1;
		s_joining   = 0;

		// 开打后不再广播(房间随即从别人的列表里消失)
		//
		// 注意: 这里不能关闭监听 socket —— comm/net.c 的 net_is_server() 以"监听
		// socket 是否存在"为判据, 而 comm/npsession 的 np_frame_input() 正是
		// 用它决定主/副手柄路由; 一旦关掉, 服务端会按客户机取值(手柄反转)。
		// 监听 socket 统一由 np_end() 关闭; 且已有连接时 net_is_connected() 不会
		// 再 accept, 因此保留它对对战没有任何影响。
		lan_close();

		INES_LOG(LOG_NTY, MOD_NET, ISTR("lan: paired, run as %s\n"),
				 np_is_server() ? ISTR("server") : ISTR("client"));

		EndDialog(hDlg, IDOK);
		return;
	}

	if(rc == NP_POLL_FAILED)
	{
		ines_char_t  text[LANMATCH_MSG_MAX];

		// 失败原因来自会话层(UTF-8); 没有则用 dialog.lan.connect_failed
		if(msg[0] != 0)
			lanmatch_utf8_to_tchar(text, (int)count_of(text), msg);
		else
		{
			ines_strncpy(text, L10N("dialog.lan.connect_failed"), count_of(text) - 1);
			text[count_of(text) - 1] = 0;
		}

		ines_strncpy(s_fail_msg, text, count_of(s_fail_msg) - 1);
		s_fail_msg[count_of(s_fail_msg) - 1] = 0;

		// 服务端继续等待其他人; 客户端退回发布状态
		dlgLanMatch_ResumeHosting(hDlg);

		ines_snprintf(text, count_of(text), L10N("dialog.lan.restore_format"), s_fail_msg);

		dlgLanMatch_SetInfo(hDlg, text);
		return;
	}

	if(s_joining)
	{
		ines_char_t  text[LANMATCH_MSG_MAX];

		// 握手进度来自会话层(UTF-8); 没有则用 dialog.lan.connecting
		if(msg[0] != 0)
		{
			lanmatch_utf8_to_tchar(text, (int)count_of(text), msg);
			dlgLanMatch_SetInfo(hDlg, (text[0] != 0) ? text : L10N("dialog.lan.connecting"));
		}
		else
			dlgLanMatch_SetInfo(hDlg, L10N("dialog.lan.connecting"));
		return;
	}

	dlgLanMatch_RefreshInfo(hDlg);
}


// ---------------------------------------------------------------------
// 消息处理
// ---------------------------------------------------------------------

static BOOL dlgLanMatch_OnInitDialog(HWND hDlg)
{
	INITCOMMONCONTROLSEX  icex;
	HWND         hwndList;
	ines_char_t  text[256];

	icex.dwSize = sizeof(icex);
	icex.dwICC  = ICC_LISTVIEW_CLASSES;
	InitCommonControlsEx(&icex);

	// ---- 昵称 ----
	ines_strncpy(text, GetConfigStr(LANMATCH_CFG_SECTION, LANMATCH_CFG_NICKKEY, ISTR("")), count_of(text) - 1);
	text[count_of(text) - 1] = 0;

	if(text[0] == 0)
	{
		ines_snprintf(text, count_of(text), ISTR("Player %04d"), lanmatch_default_nick_num());
		SetConfigStr(LANMATCH_CFG_SECTION, LANMATCH_CFG_NICKKEY, text);
	}

	SetDlgItemText(hDlg, IDC_LANMATCH_EDT_NICK, text);

	// ---- 昵称 / 加入 / 关闭 / 标题 ----
	dlgLanMatch_ApplyLanguage(hDlg);

	// ---- 房间列表 ----
	hwndList = GetDlgItem(hDlg, IDC_LANMATCH_LIST);

	ListView_SetExtendedListViewStyle(hwndList, LVS_EX_FULLROWSELECT|LVS_EX_DOUBLEBUFFER);

	dlgLanMatch_UpdateColumns(hwndList);

	dlgLanMatch_UpdateLabels(hDlg);

	EnableWindow(GetDlgItem(hDlg, IDOK), FALSE);

	dlgLanMatch_RefreshInfo(hDlg);

	// 与"联网对战"对话框同理: 模态循环下靠 WM_TIMER 推进
	SetTimer(hDlg, LANMATCH_TIMER_ID, LANMATCH_TIMER_MS, NULL);

	return TRUE;
}

/** 选中变化: 只有 ROM 相同的房间可加入(不同 ROM 可见但不可加入)。 */
static BOOL dlgLanMatch_OnItemChanged(HWND hDlg, NMLISTVIEW* pnm)
{
	HWND  hwndList;
	int   row;

	if(pnm == NULL)
		return FALSE;

	if((pnm->uChanged & LVIF_STATE) == 0)
		return FALSE;

	if((pnm->uNewState & LVIS_SELECTED) == 0)
		return FALSE;

	hwndList = GetDlgItem(hDlg, IDC_LANMATCH_LIST);
	row      = ListView_GetNextItem(hwndList, -1, LVNI_SELECTED);

	if((row < 0) || (row >= s_room_count))
	{
		s_has_sel = 0;
		EnableWindow(GetDlgItem(hDlg, IDOK), FALSE);
		return FALSE;
	}

	memcpy(s_sel_id, s_rooms[row].peer_id, 16);
	s_has_sel = 1;

	if(s_rooms[row].crc32 != s_crc32)
	{
		EnableWindow(GetDlgItem(hDlg, IDOK), FALSE);
		dlgLanMatch_SetInfo(hDlg, L10N("dialog.lan.err_rom_diff"));
		return FALSE;
	}

	if(s_rooms[row].net_ver != (ines_dword_t)NET_VER)
	{
		EnableWindow(GetDlgItem(hDlg, IDOK), FALSE);
		dlgLanMatch_SetInfo(hDlg, L10N("dialog.lan.err_ver_diff"));
		return FALSE;
	}

	EnableWindow(GetDlgItem(hDlg, IDOK), TRUE);
	dlgLanMatch_RefreshInfo(hDlg);

	return FALSE;
}

/** 不可加入的房间(ROM 不同 / 版本不同)灰字显示。 */
static BOOL dlgLanMatch_OnCustomDraw(HWND hDlg, NMLVCUSTOMDRAW* pcd)
{
	DWORD  row;

	if(pcd == NULL)
		return FALSE;

	if(pcd->nmcd.dwDrawStage == CDDS_PREPAINT)
	{
		SetWindowLongPtr(hDlg, DWLP_MSGRESULT, (LONG_PTR)CDRF_NOTIFYITEMDRAW);
		return TRUE;
	}

	if(pcd->nmcd.dwDrawStage != CDDS_ITEMPREPAINT)
		return FALSE;

	row = (DWORD)pcd->nmcd.dwItemSpec;

	if((row < (DWORD)s_room_count)
	 && ((s_rooms[row].crc32 != s_crc32) || (s_rooms[row].net_ver != (ines_dword_t)NET_VER)))
		pcd->clrText = GetSysColor(COLOR_GRAYTEXT);

	SetWindowLongPtr(hDlg, DWLP_MSGRESULT, (LONG_PTR)CDRF_NEWFONT);

	return TRUE;
}

static INT_PTR CALLBACK dlgLanMatch_DlgProc(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam)
{
	switch(message)
	{
	case WM_INITDIALOG:
		return (INT_PTR)dlgLanMatch_OnInitDialog(hDlg);

	case WM_TIMER:
		dlgLanMatch_OnTimer(hDlg);
		return (INT_PTR)TRUE;

	case WM_APP_LANGCHANGED:      /* 语言切换: 标题 / 静态文本 / 表头 / ROM 说明都重来 */
		dlgLanMatch_ApplyLanguage(hDlg);
		dlgLanMatch_UpdateColumns(GetDlgItem(hDlg, IDC_LANMATCH_LIST));
		dlgLanMatch_UpdateLabels(hDlg);
		dlgLanMatch_RefreshList(hDlg);      /* 行内的"ROM 不同"/版本后缀 */
		dlgLanMatch_RefreshInfo(hDlg);
		return (INT_PTR)TRUE;

	case WM_COMMAND:
		if(LOWORD(wParam) == IDOK)
		{
			dlgLanMatch_OnJoin(hDlg);
			return (INT_PTR)TRUE;
		}

		if(LOWORD(wParam) == IDCANCEL)
		{
			KillTimer(hDlg, LANMATCH_TIMER_ID);
			dlgLanMatch_SaveNick(hDlg);
			dlgLanMatch_StopAll();
			EndDialog(hDlg, IDCANCEL);
			return (INT_PTR)TRUE;
		}

		break;

	case WM_NOTIFY:
		{
			NMHDR*  pnm = (NMHDR*)lParam;

			if((pnm != NULL) && (pnm->idFrom == IDC_LANMATCH_LIST))
			{
				if(pnm->code == NM_DBLCLK)
				{
					dlgLanMatch_OnJoin(hDlg);
					return (INT_PTR)TRUE;
				}

				if(pnm->code == LVN_ITEMCHANGED)
					return (INT_PTR)dlgLanMatch_OnItemChanged(hDlg, (NMLISTVIEW*)lParam);

				if(pnm->code == NM_CUSTOMDRAW)
					return (INT_PTR)dlgLanMatch_OnCustomDraw(hDlg, (NMLVCUSTOMDRAW*)lParam);
			}
		}
		break;

	case WM_CLOSE:
		KillTimer(hDlg, LANMATCH_TIMER_ID);
		dlgLanMatch_SaveNick(hDlg);
		dlgLanMatch_StopAll();
		EndDialog(hDlg, IDCANCEL);
		return (INT_PTR)TRUE;
	}

	return (INT_PTR)FALSE;
}
