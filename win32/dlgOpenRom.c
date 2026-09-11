// dlgOpenRom.c : “载入 NES 文件”管理器对话框
//
// 布局(上中下三段):
//   上 — 文件夹路径输入框 + 选择文件夹按钮(系统文件夹图标)
//   中 — NES 文件列表(随窗口拉伸), 列出文件头中的关键属性
//   下 — “加载”/“取消”按钮 + 文件计数提示
//
// 说明:
//   1) 列表项只读取 iNES 文件头(16 字节)解析属性, 不加载 ROM 数据;
//   2) 文件头解析规则与 core/rom.c 的 ines_rom_load_from_file() 保持一致,
//      列表显示的 Mapper/镜像等信息与实际模拟运行结果一致;
//   3) 加载流程仍然由调用方(iNES.c)完成, 本模块只负责选择文件;
//   4) 列表支持点击表头排序: 排序依据是解析出的文件头属性(数值),
//      不是列上的显示文本, 因此 ROM 大小等列不会按字符串顺序排列;
//      同一列重复点击切换升/降序, 切换文件夹后保持当前排序方式;
//   5) 目录内文件较多时界面不会长时间卡住: 先枚举目录条目(不打开文件)把
//      文件名与文件大小一次性填进列表, 再由定时器分批读文件头补齐其余属性列,
//      非 iNES 文件在解析到时从列表里移除。

#include "stdafx.h"
#include <shlobj.h>
#include "../comm/log.h"
#include "../core/rom.h"
#include "../core/ppu.h"
#include "Resource.h"
#include "dlgOpenRom.h"


// 布局尺寸(像素)
#define DLGOPENROM_MARGIN        8
#define DLGOPENROM_ROW_H         24
#define DLGOPENROM_LABEL_W       60
#define DLGOPENROM_BROWSE_W      28
#define DLGOPENROM_BTN_W         78
#define DLGOPENROM_BTN_H         24
#define DLGOPENROM_BTN_GAP       6
// 初始客户区大小
#define DLGOPENROM_INIT_W        720
#define DLGOPENROM_INIT_H        520
// 最小客户区大小(拉伸下限)
#define DLGOPENROM_MIN_W         520
#define DLGOPENROM_MIN_H         340
// 单次扫描的最大文件数, 防止超大目录拖慢界面
#define DLGOPENROM_MAX_FILES     4096

// 属性解析的分片参数: 逐文件打开读文件头是扫描中唯一耗时的部分,
// 放到定时器里按时间预算分批处理, 保证界面始终可响应
#define DLGOPENROM_TIMER_SCAN        1   // 解析定时器 ID
#define DLGOPENROM_SCAN_TICK_MS      10  // 定时器间隔(毫秒)
#define DLGOPENROM_SCAN_BUDGET_MS    20  // 单个时间片的最大耗时(毫秒)
#define DLGOPENROM_SCAN_MAX_PER_TICK 32  // 单个时间片最多解析的文件数

// NES 文件属性(全部取自 iNES 文件头)
typedef struct _dlgOpenRom_rominfo_
{
	ines_int64_t  file_size;     // 文件总字节数
	ines_byte_t   mapper_num;    // Mapper 编号
	ines_byte_t   mirror_type;   // 镜像方式 MIRROR_*
	ines_byte_t   has_sram;      // 是否带电池记忆
	ines_byte_t   has_trainer;   // 是否带 512 字节 trainer
	ines_word_t   prm_kb;        // PRG 大小(KB)
	ines_word_t   chr_kb;        // CHR 大小(KB)
} dlgOpenRom_rominfo_t;

// 列表列定义
typedef struct _dlgOpenRom_column_
{
	ines_cstr_t  name;
	ines_int_t   width;
	ines_int_t   fmt;
} dlgOpenRom_column_t;

static const dlgOpenRom_column_t s_columns[] =
{
	{ ISTR("文件名"),    220, LVCFMT_LEFT  },
	{ ISTR("ROM大小"),    80, LVCFMT_RIGHT },
	{ ISTR("Mapper"),     60, LVCFMT_RIGHT },
	{ ISTR("PRG"),        70, LVCFMT_RIGHT },
	{ ISTR("CHR"),        70, LVCFMT_RIGHT },
	{ ISTR("镜像"),        60, LVCFMT_LEFT  },
	{ ISTR("电池"),        50, LVCFMT_LEFT  },
	{ ISTR("Trainer"),    60, LVCFMT_LEFT  }
};

// 列表项的排序数据: 与列表项通过 LVIF_PARAM 一一关联
// (尾部 szName 为变长空间, 按文件名实际长度分配, 不占固定路径缓冲)
typedef struct _dlgOpenRom_item_
{
	dlgOpenRom_rominfo_t  info;      // 文件头解析出的属性(数值列排序依据)
	ines_bool_t           bParsed;   // 文件头是否已解析(分批解析用)
	ines_char_t           szName[1]; // 文件名(其后的空间由分配时补足)
} dlgOpenRom_item_t;

// 当前浏览的文件夹
static ines_char_t  s_szCurDir[INES_MAX_PATH] = {0};
// 用户最终选定的 ROM 文件完整路径
static ines_char_t  s_szSelected[INES_MAX_PATH] = {0};
// 选择文件夹按钮使用的系统文件夹图标
static HICON        s_hFolderIcon = NULL;
// 列表项排序数据(下标 0 .. s_nItemCount-1 有效)
static dlgOpenRom_item_t*  s_pItems[DLGOPENROM_MAX_FILES] = {0};
static ines_int_t          s_nItemCount = 0;
// 当前排序状态: -1 表示未排序(保持文件系统枚举顺序)
static ines_int_t          s_iSortColumn = -1;
static ines_bool_t         s_bSortAsc    = ines_true;
// 分批解析状态: s_iScanNext 为下一个待解析的列表行号
static ines_int_t          s_iScanNext = 0;
static ines_bool_t         s_bScanning = ines_false;

static INT_PTR CALLBACK dlgOpenRom_DlgProc(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam);
static BOOL     dlgOpenRom_OnInitDialog(HWND hDlg);
static INT_PTR  dlgOpenRom_OnCommand(HWND hDlg, UINT nID, UINT nCode);
static INT_PTR  dlgOpenRom_OnNotify(HWND hDlg, LPNMHDR pNMHDR);
static VOID     dlgOpenRom_OnDestroy(HWND hDlg);

static VOID     dlgOpenRom_InitCommonControls(VOID);
static VOID     dlgOpenRom_Layout(HWND hDlg);
static VOID     dlgOpenRom_InitList(HWND hDlg);
static VOID     dlgOpenRom_SetBrowseIcon(HWND hDlg);
static VOID     dlgOpenRom_UpdateButtons(HWND hDlg);
static VOID     dlgOpenRom_ScanDir(HWND hDlg);
static VOID     dlgOpenRom_OnBrowseDir(HWND hDlg);
static VOID     dlgOpenRom_ApplyTypedDir(HWND hDlg);
static BOOL     dlgOpenRom_OnLoad(HWND hDlg);
static VOID     dlgOpenRom_OnColumnClick(HWND hDlg, ines_int_t iColumn);
static VOID     dlgOpenRom_SortList(HWND hList);
static VOID     dlgOpenRom_UpdateHeaderArrow(HWND hList);

static VOID     dlgOpenRom_OnScanTimer(HWND hDlg);
static VOID     dlgOpenRom_StopScan(HWND hDlg);
static VOID     dlgOpenRom_FinishScan(HWND hDlg);
static ines_bool_t dlgOpenRom_ParseRow(HWND hList, ines_int_t iItem);

static dlgOpenRom_item_t* dlgOpenRom_AllocItem(ines_cstr_t szName, const dlgOpenRom_rominfo_t* pInfo);
static VOID     dlgOpenRom_FreeItems(VOID);
static VOID     dlgOpenRom_RemoveItem(dlgOpenRom_item_t* pItem);
static int CALLBACK dlgOpenRom_CompareItems(LPARAM lParam1, LPARAM lParam2, LPARAM lParamSort);

static VOID     dlgOpenRom_BuildPath(ines_str_t szOut, ines_int_t nLen, ines_cstr_t szDir, ines_cstr_t szName);
static VOID     dlgOpenRom_TrimPath(ines_str_t szPath);
static VOID     dlgOpenRom_FormatSize(ines_int64_t size, ines_str_t szBuf, ines_int_t nLen);
static VOID     dlgOpenRom_SetItemText(HWND hList, ines_int_t iItem, ines_int_t iSubItem, ines_cstr_t szText);
static ines_cstr_t dlgOpenRom_GetMirrorText(ines_byte_t mirror_type);
static ines_bool_t dlgOpenRom_ParseNesHeader(ines_cstr_t szFile, dlgOpenRom_rominfo_t* pInfo);
static int CALLBACK dlgOpenRom_BrowseCallback(HWND hWnd, UINT uMsg, LPARAM lParam, LPARAM lpData);


/**
 * 显示“载入 NES 文件”对话框。
 */
BOOL dlgOpenRom_DoModal(HINSTANCE hInstance, HWND hParentWnd, ines_cstr_t szInitDir,
						ines_str_t szSelected, ines_int_t nSelectedLen,
						ines_str_t szLastDir, ines_int_t nLastDirLen)
{
	INT_PTR  iRet;

	if(szSelected == NULL || nSelectedLen <= 0)
		return FALSE;

	if(szLastDir != NULL && nLastDirLen > 0)
		szLastDir[0] = 0;

	// 对话框内使用了列表视图控件, 必须先加载并注册公共控件类
	dlgOpenRom_InitCommonControls();

	szSelected[0] = 0;
	s_szSelected[0] = 0;

	// 排序与扫描状态不跨次保留: 每次打开对话框都从文件系统枚举顺序开始
	s_iSortColumn = -1;
	s_bSortAsc    = ines_true;
	dlgOpenRom_StopScan(NULL);

	// 初始目录: 优先使用调用方给出的目录, 否则取当前工作目录
	if(szInitDir != NULL && szInitDir[0] != 0)
	{
		_tcsncpy(s_szCurDir, szInitDir, count_of(s_szCurDir) - 1);
		s_szCurDir[count_of(s_szCurDir) - 1] = 0;
	}
	else if(0 == GetCurrentDirectory((DWORD)count_of(s_szCurDir), s_szCurDir))
	{
		s_szCurDir[0] = 0;
	}

	iRet = DialogBox(hInstance, MAKEINTRESOURCE(IDD_OPENROM), hParentWnd, dlgOpenRom_DlgProc);

	// 回传对话框关闭时所在的文件夹(点“加载”或“取消”都一样), 供调用方记录位置
	if(iRet != -1 && szLastDir != NULL && nLastDirLen > 0)
	{
		_tcsncpy(szLastDir, s_szCurDir, (ines_size_t)(nLastDirLen - 1));
		szLastDir[nLastDirLen - 1] = 0;
	}

	if(iRet != IDOK)
		return FALSE;

	_tcsncpy(szSelected, s_szSelected, (ines_size_t)(nSelectedLen - 1));
	szSelected[nSelectedLen - 1] = 0;

	return TRUE;
}


/**
 * 初始化列表视图控件所属的公共控件。
 *
 * 宿主程序没有导入 comctl32.dll, 若不显式初始化, SysListView32 窗口类
 * 不会被注册, 对话框将创建失败。
 */
static VOID dlgOpenRom_InitCommonControls(VOID)
{
	static BOOL           s_bInited = FALSE;
	INITCOMMONCONTROLSEX  icc;

	if(s_bInited)
		return;

	icc.dwSize = sizeof(icc);
	icc.dwICC  = ICC_LISTVIEW_CLASSES;

	if(!InitCommonControlsEx(&icc))
	{
		INES_LOG(LOG_WAR, MOD_SYS, ISTR("InitCommonControlsEx failed: %d\n"), (ines_int_t)GetLastError());
	}

	s_bInited = TRUE;
}


/**
 * 对话框消息处理。
 */
static INT_PTR CALLBACK dlgOpenRom_DlgProc(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam)
{
	switch(message)
	{
	case WM_INITDIALOG:
		return (INT_PTR)dlgOpenRom_OnInitDialog(hDlg);

	case WM_SIZE:
		// 最小化时不重排布局
		if(wParam != SIZE_MINIMIZED)
			dlgOpenRom_Layout(hDlg);
		return (INT_PTR)TRUE;

	case WM_GETMINMAXINFO:
		{
			// 限制拉伸下限, 避免控件被挤压到不可用
			LPMINMAXINFO  lpmmi;
			RECT          rc;
			DWORD_PTR     dwStyle;
			DWORD_PTR     dwExStyle;

			lpmmi     = (LPMINMAXINFO)lParam;
			dwStyle   = (DWORD_PTR)GetWindowLongPtr(hDlg, GWL_STYLE);
			dwExStyle = (DWORD_PTR)GetWindowLongPtr(hDlg, GWL_EXSTYLE);

			SetRect(&rc, 0, 0, DLGOPENROM_MIN_W, DLGOPENROM_MIN_H);
			AdjustWindowRectEx(&rc, (DWORD)dwStyle, FALSE, (DWORD)dwExStyle);

			lpmmi->ptMinTrackSize.x = rc.right - rc.left;
			lpmmi->ptMinTrackSize.y = rc.bottom - rc.top;
		}
		return (INT_PTR)TRUE;

	case WM_NOTIFY:
		return dlgOpenRom_OnNotify(hDlg, (LPNMHDR)lParam);

	case WM_COMMAND:
		return dlgOpenRom_OnCommand(hDlg, (UINT)LOWORD(wParam), (UINT)HIWORD(wParam));

	case WM_TIMER:
		// 分批解析文件头(仅解析阶段有该定时器)
		if(wParam == (WPARAM)DLGOPENROM_TIMER_SCAN)
			dlgOpenRom_OnScanTimer(hDlg);
		return (INT_PTR)TRUE;

	case WM_DESTROY:
		dlgOpenRom_OnDestroy(hDlg);
		return (INT_PTR)TRUE;

	default:
		break;
	}

	return (INT_PTR)FALSE;
}


/**
 * 初始化对话框: 设置初始大小/位置, 初始化列表并扫描当前目录。
 * 返回 FALSE 表示已自行设置输入焦点。
 */
static BOOL dlgOpenRom_OnInitDialog(HWND hDlg)
{
	RECT       rc;
	RECT       rcParent;
	RECT       rcWork;
	HWND       hList;
	ines_int_t cx;
	ines_int_t cy;
	ines_int_t x;
	ines_int_t y;
	DWORD_PTR  dwStyle;
	DWORD_PTR  dwExStyle;

	dlgOpenRom_InitList(hDlg);
	dlgOpenRom_SetBrowseIcon(hDlg);

	// 初始大小(客户区)与位置: 相对父窗口居中, 并保证不超出工作区
	dwStyle   = (DWORD_PTR)GetWindowLongPtr(hDlg, GWL_STYLE);
	dwExStyle = (DWORD_PTR)GetWindowLongPtr(hDlg, GWL_EXSTYLE);

	SetRect(&rc, 0, 0, DLGOPENROM_INIT_W, DLGOPENROM_INIT_H);
	AdjustWindowRectEx(&rc, (DWORD)dwStyle, FALSE, (DWORD)dwExStyle);

	cx = rc.right - rc.left;
	cy = rc.bottom - rc.top;
	x  = 0;
	y  = 0;

	if(GetWindowRect(GetParent(hDlg), &rcParent))
	{
		x = rcParent.left + ((rcParent.right - rcParent.left) - cx) / 2;
		y = rcParent.top + ((rcParent.bottom - rcParent.top) - cy) / 2;
	}

	if(SystemParametersInfo(SPI_GETWORKAREA, 0, &rcWork, 0))
	{
		if(x < rcWork.left)	x = rcWork.left;
		if(y < rcWork.top)	y = rcWork.top;
		if(x + cx > rcWork.right)	x = rcWork.right - cx;
		if(y + cy > rcWork.bottom)	y = rcWork.bottom - cy;
	}

	SetWindowPos(hDlg, NULL, x, y, cx, cy, SWP_NOZORDER | SWP_NOACTIVATE);

	SetDlgItemText(hDlg, IDC_OPENROM_EDT_DIR, s_szCurDir);

	dlgOpenRom_ScanDir(hDlg);

	// 保证布局正确(WM_SIZE 可能尚未到达)
	dlgOpenRom_Layout(hDlg);

	hList = GetDlgItem(hDlg, IDC_OPENROM_LIST);
	if(hList != NULL)
		SetFocus(hList);

	return FALSE;
}


/**
 * 按客户区大小重排控件: 顶部固定, 底部固定, 中部列表拉伸。
 */
static VOID dlgOpenRom_Layout(HWND hDlg)
{
	RECT       rc;
	HWND       hItem;
	ines_int_t cx;
	ines_int_t cy;
	ines_int_t x;
	ines_int_t y;
	ines_int_t w;
	ines_int_t h;

	GetClientRect(hDlg, &rc);
	cx = rc.right - rc.left;
	cy = rc.bottom - rc.top;

	// ---- 顶部: 文件夹路径 ----
	hItem = GetDlgItem(hDlg, IDC_OPENROM_LAB_DIR);
	if(hItem != NULL)
		SetWindowPos(hItem, NULL, DLGOPENROM_MARGIN, DLGOPENROM_MARGIN + 5,
			DLGOPENROM_LABEL_W, DLGOPENROM_ROW_H - 10, SWP_NOZORDER);

	x = DLGOPENROM_MARGIN + DLGOPENROM_LABEL_W;
	y = DLGOPENROM_MARGIN;
	h = DLGOPENROM_ROW_H;

	hItem = GetDlgItem(hDlg, IDC_OPENROM_BTN_DIR);
	if(hItem != NULL)
		SetWindowPos(hItem, NULL, cx - DLGOPENROM_MARGIN - DLGOPENROM_BROWSE_W, y,
			DLGOPENROM_BROWSE_W, h, SWP_NOZORDER);

	w = (cx - DLGOPENROM_MARGIN - DLGOPENROM_BROWSE_W - 6) - x;
	if(w < 60)
		w = 60;

	hItem = GetDlgItem(hDlg, IDC_OPENROM_EDT_DIR);
	if(hItem != NULL)
		SetWindowPos(hItem, NULL, x, y + 3, w, h - 6, SWP_NOZORDER);

	// ---- 底部: 按钮与统计文字 ----
	y = cy - DLGOPENROM_MARGIN - DLGOPENROM_BTN_H;

	hItem = GetDlgItem(hDlg, IDC_OPENROM_LAB_COUNT);
	if(hItem != NULL)
		SetWindowPos(hItem, NULL, DLGOPENROM_MARGIN, y + 6, 240, 16, SWP_NOZORDER);

	hItem = GetDlgItem(hDlg, IDOK);
	if(hItem != NULL)
		SetWindowPos(hItem, NULL,
			cx - DLGOPENROM_MARGIN - DLGOPENROM_BTN_W * 2 - DLGOPENROM_BTN_GAP, y,
			DLGOPENROM_BTN_W, DLGOPENROM_BTN_H, SWP_NOZORDER);

	hItem = GetDlgItem(hDlg, IDCANCEL);
	if(hItem != NULL)
		SetWindowPos(hItem, NULL, cx - DLGOPENROM_MARGIN - DLGOPENROM_BTN_W, y,
			DLGOPENROM_BTN_W, DLGOPENROM_BTN_H, SWP_NOZORDER);

	// ---- 中部: 文件列表(拉伸) ----
	y = DLGOPENROM_MARGIN + DLGOPENROM_ROW_H + 6;
	h = (cy - DLGOPENROM_MARGIN - DLGOPENROM_BTN_H - 6) - y;
	if(h < 60)
		h = 60;

	hItem = GetDlgItem(hDlg, IDC_OPENROM_LIST);
	if(hItem != NULL)
		SetWindowPos(hItem, NULL, DLGOPENROM_MARGIN, y, cx - DLGOPENROM_MARGIN * 2, h, SWP_NOZORDER);
}


/**
 * 初始化文件列表的列。
 */
static VOID dlgOpenRom_InitList(HWND hDlg)
{
	HWND        hList;
	LVCOLUMN    col;
	ines_int_t  i;

	hList = GetDlgItem(hDlg, IDC_OPENROM_LIST);
	if(hList == NULL)
		return;

	// 整行选中 + 双缓冲(减少刷新闪烁)
	ListView_SetExtendedListViewStyle(hList, LVS_EX_FULLROWSELECT | LVS_EX_DOUBLEBUFFER);

	memset(&col, 0, sizeof(col));
	col.mask = LVCF_TEXT | LVCF_WIDTH | LVCF_FMT | LVCF_SUBITEM;

	for(i = 0; i < (ines_int_t)count_of(s_columns); i++)
	{
		col.iSubItem = i;
		col.pszText  = (ines_str_t)s_columns[i].name;
		col.cx       = s_columns[i].width;
		col.fmt      = s_columns[i].fmt;
		ListView_InsertColumn(hList, i, &col);
	}
}


/**
 * 给“选择文件夹”按钮设置系统文件夹图标; 取不到图标时退化为文字按钮。
 */
static VOID dlgOpenRom_SetBrowseIcon(HWND hDlg)
{
	SHFILEINFO  sfi;
	HWND        hBtn;
	LONG_PTR    lStyle;

	hBtn = GetDlgItem(hDlg, IDC_OPENROM_BTN_DIR);
	if(hBtn == NULL)
		return;

	memset(&sfi, 0, sizeof(sfi));

	// 这里只需要一个目录类型的图标, 用 SHGFI_USEFILEATTRIBUTES 避免真实访问磁盘
	if(0 == SHGetFileInfo(ISTR("C:\\"), FILE_ATTRIBUTE_DIRECTORY, &sfi, sizeof(sfi),
			SHGFI_ICON | SHGFI_SMALLICON | SHGFI_USEFILEATTRIBUTES) || sfi.hIcon == NULL)
	{
		lStyle = GetWindowLongPtr(hBtn, GWL_STYLE);
		SetWindowLongPtr(hBtn, GWL_STYLE, lStyle & ~((LONG_PTR)BS_ICON));
		SetWindowText(hBtn, ISTR("..."));
		InvalidateRect(hBtn, NULL, TRUE);
		return;
	}

	s_hFolderIcon = sfi.hIcon;
	SendMessage(hBtn, BM_SETIMAGE, (WPARAM)IMAGE_ICON, (LPARAM)s_hFolderIcon);
}


/**
 * 根据列表当前选中状态启用/禁用“加载”按钮。
 */
static VOID dlgOpenRom_UpdateButtons(HWND hDlg)
{
	HWND  hList;
	HWND  hLoad;
	int   iSel;

	hList = GetDlgItem(hDlg, IDC_OPENROM_LIST);
	hLoad = GetDlgItem(hDlg, IDOK);
	if(hLoad == NULL)
		return;

	iSel = (hList != NULL) ? ListView_GetNextItem(hList, -1, LVNI_SELECTED) : -1;

	EnableWindow(hLoad, iSel >= 0);
}


/**
 * 扫描当前文件夹。
 *
 * 分两步: 先枚举目录条目(不打开文件)把文件名与文件大小填进列表, 再由定时器
 * 分批打开文件读 16 字节头补齐其余属性列。逐文件打开是扫描中唯一耗时的部分,
 * 分片后界面不会被长时间阻塞。
 */
static VOID dlgOpenRom_ScanDir(HWND hDlg)
{
	WIN32_FIND_DATA       fd;
	HANDLE                hFind;
	ines_char_t           szPattern[INES_MAX_PATH];
	ines_char_t           szBuf[128];
	dlgOpenRom_rominfo_t  info;
	dlgOpenRom_item_t*    pItem;
	HWND                  hList;
	LVITEM                item;
	ines_int_t            iCount;

	hList = GetDlgItem(hDlg, IDC_OPENROM_LIST);
	if(hList == NULL)
		return;

	// 重新扫描前先停掉上一轮解析, 清空列表并释放上一批列表项的排序数据
	dlgOpenRom_StopScan(hDlg);
	dlgOpenRom_FreeItems();
	ListView_DeleteAllItems(hList);
	iCount = 0;

	if(s_szCurDir[0] == 0)
	{
		SetDlgItemText(hDlg, IDC_OPENROM_LAB_COUNT, ISTR("未选择文件夹"));
		dlgOpenRom_UpdateButtons(hDlg);
		return;
	}

	dlgOpenRom_BuildPath(szPattern, count_of(szPattern), s_szCurDir, ISTR("*.nes"));

	// 枚举阶段只读目录条目、不打开文件, 因此可以一次做完;
	// 期间关闭重绘, 避免逐行插入时反复刷新界面
	SendMessage(hList, WM_SETREDRAW, FALSE, 0);

	memset(&fd, 0, sizeof(fd));
	hFind = FindFirstFile(szPattern, &fd);

	if(hFind != INVALID_HANDLE_VALUE)
	{
		do
		{
			if(0 != (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY))
				continue;

			if(s_nItemCount >= (ines_int_t)count_of(s_pItems))
				break;

			// 文件名与文件大小直接来自目录条目(无需打开文件);
			// 文件头属性由随后的解析阶段分批补齐
			memset(&info, 0, sizeof(info));
			info.file_size = ((ines_int64_t)fd.nFileSizeHigh << 32) | (ines_int64_t)fd.nFileSizeLow;

			// 排序数据: 点击表头排序时按这里保存的文件头属性比较
			pItem = dlgOpenRom_AllocItem(fd.cFileName, &info);
			if(pItem == NULL)
			{
				INES_LOG(LOG_ERR, MOD_SYS, ISTR("alloc sort data for `%s` failed.\n"), fd.cFileName);
				break;
			}

			memset(&item, 0, sizeof(item));
			item.mask     = LVIF_TEXT | LVIF_PARAM;
			item.iItem    = iCount;
			item.iSubItem = 0;
			item.pszText  = pItem->szName;
			item.lParam   = (LPARAM)pItem;

			if(ListView_InsertItem(hList, &item) < 0)
			{
				ines_free(pItem);
				pItem = NULL;
				break;
			}

			s_pItems[s_nItemCount] = pItem;
			s_nItemCount++;

			dlgOpenRom_FormatSize(info.file_size, szBuf, count_of(szBuf));
			dlgOpenRom_SetItemText(hList, iCount, 1, szBuf);

			iCount++;
		}
		while(iCount < DLGOPENROM_MAX_FILES && FindNextFile(hFind, &fd));

		FindClose(hFind);
	}

	SendMessage(hList, WM_SETREDRAW, TRUE, 0);
	InvalidateRect(hList, NULL, TRUE);

	if(iCount <= 0)
	{
		SetDlgItemText(hDlg, IDC_OPENROM_LAB_COUNT, ISTR("未找到 NES 文件"));
		INES_LOG(LOG_INF, MOD_SYS, ISTR("scan dir `%s`: no nes file found.\n"), s_szCurDir);
		dlgOpenRom_UpdateButtons(hDlg);
		return;
	}

	// 进入解析阶段: 定时器分批读文件头, 解析完再显示计数、应用排序并默认选中第一行
	s_iScanNext = 0;
	s_bScanning = ines_true;

	ines_snprintf(szBuf, count_of(szBuf), ISTR("正在解析 0/%d ..."), iCount);
	SetDlgItemText(hDlg, IDC_OPENROM_LAB_COUNT, szBuf);

	INES_LOG(LOG_INF, MOD_SYS, ISTR("scan dir `%s`: %d nes file(s) enumerated.\n"), s_szCurDir, iCount);

	SetTimer(hDlg, DLGOPENROM_TIMER_SCAN, DLGOPENROM_SCAN_TICK_MS, NULL);

	dlgOpenRom_UpdateButtons(hDlg);
}


/**
 * 解析阶段的定时器处理: 在一个时间片内尽可能多地解析文件头。
 *
 * 用时间预算而不是固定条数, 避免磁盘较慢时单次消息处理过久导致界面卡顿。
 */
static VOID dlgOpenRom_OnScanTimer(HWND hDlg)
{
	DWORD       dwStart;
	HWND        hList;
	ines_int_t  iItem;
	ines_int_t  iBatch;
	ines_int_t  iCount;
	ines_char_t szBuf[128];

	if(!s_bScanning)
	{
		dlgOpenRom_StopScan(hDlg);
		return;
	}

	hList = GetDlgItem(hDlg, IDC_OPENROM_LIST);
	if(hList == NULL)
	{
		dlgOpenRom_StopScan(hDlg);
		return;
	}

	dwStart = GetTickCount();
	iItem   = s_iScanNext;
	iBatch  = 0;
	iCount  = ListView_GetItemCount(hList);

	while(iItem < iCount && iBatch < DLGOPENROM_SCAN_MAX_PER_TICK &&
		(GetTickCount() - dwStart) < DLGOPENROM_SCAN_BUDGET_MS)
	{
		// 被移除的行不占位置: 该行号上已经是下一行, 因此游标不前进
		if(dlgOpenRom_ParseRow(hList, iItem))
			iItem++;

		iBatch++;

		iCount = ListView_GetItemCount(hList);
	}

	s_iScanNext = iItem;

	if(iItem >= iCount)
	{
		dlgOpenRom_FinishScan(hDlg);
		return;
	}

	ines_snprintf(szBuf, count_of(szBuf), ISTR("正在解析 %d/%d ..."), iItem, iCount);
	SetDlgItemText(hDlg, IDC_OPENROM_LAB_COUNT, szBuf);
}


/**
 * 解析阶段结束: 停掉定时器, 显示最终计数, 应用排序并默认选中第一行。
 */
static VOID dlgOpenRom_FinishScan(HWND hDlg)
{
	HWND        hList;
	ines_int_t  iCount;
	ines_char_t szBuf[128];

	dlgOpenRom_StopScan(hDlg);

	hList = GetDlgItem(hDlg, IDC_OPENROM_LIST);
	if(hList == NULL)
		return;

	iCount = ListView_GetItemCount(hList);

	if(iCount > 0)
	{
		ines_snprintf(szBuf, count_of(szBuf), ISTR("共 %d 个支持的 NES 文件"), iCount);
		SetDlgItemText(hDlg, IDC_OPENROM_LAB_COUNT, szBuf);

		// 属性已齐, 此时才应用排序(解析期间属性不完整, 排了也不准)
		dlgOpenRom_SortList(hList);

		// 默认选中第一行, 便于直接点击“加载”
		ListView_SetItemState(hList, 0, LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
	}
	else
	{
		SetDlgItemText(hDlg, IDC_OPENROM_LAB_COUNT, ISTR("未找到支持的 NES 文件"));
	}

	INES_LOG(LOG_INF, MOD_SYS, ISTR("scan dir `%s`: %d nes file(s) listed.\n"), s_szCurDir, iCount);

	dlgOpenRom_UpdateButtons(hDlg);
}


/**
 * 停止解析阶段(重新扫描或销毁对话框时调用)。
 */
static VOID dlgOpenRom_StopScan(HWND hDlg)
{
	if(hDlg != NULL)
		KillTimer(hDlg, DLGOPENROM_TIMER_SCAN);

	s_iScanNext = 0;
	s_bScanning = ines_false;
}


/**
 * 解析一个列表行: 读文件头并补齐属性列。
 *
 * 不是有效 iNES 文件时直接从列表中移除该行, 与“只列出受支持文件”的语义一致。
 *
 * @param hList 列表控件
 * @param iItem 列表行号
 * @return ines_true 表示该行保留(游标应前进); ines_false 表示该行已被移除
 */
static ines_bool_t dlgOpenRom_ParseRow(HWND hList, ines_int_t iItem)
{
	LVITEM                lvItem;
	dlgOpenRom_item_t*    pItem;
	dlgOpenRom_rominfo_t  info;
	ines_char_t           szPath[INES_MAX_PATH];
	ines_char_t           szBuf[128];

	memset(&lvItem, 0, sizeof(lvItem));
	lvItem.mask     = LVIF_PARAM;
	lvItem.iItem    = iItem;
	lvItem.iSubItem = 0;

	// 防御: 取不到行数据时按已处理跳过, 避免卡在同一行上
	if(!ListView_GetItem(hList, &lvItem))
		return ines_true;

	pItem = (dlgOpenRom_item_t*)lvItem.lParam;
	if(pItem == NULL)
		return ines_true;

	// 同一行被重复处理时直接跳过
	if(pItem->bParsed)
		return ines_true;

	dlgOpenRom_BuildPath(szPath, count_of(szPath), s_szCurDir, pItem->szName);

	if(ines_false == dlgOpenRom_ParseNesHeader(szPath, &info))
	{
		INES_LOG(LOG_DBG, MOD_SYS, ISTR("`%s` is not a supported nes file, removed from list.\n"), szPath);

		ListView_DeleteItem(hList, iItem);
		dlgOpenRom_RemoveItem(pItem);

		return ines_false;
	}

	// 文件大小由目录条目给出, 文件头里没有这一项
	info.file_size = pItem->info.file_size;

	pItem->info    = info;
	pItem->bParsed = ines_true;

	// 第 1 列(ROM 大小)在枚举阶段已经填好
	ines_snprintf(szBuf, count_of(szBuf), ISTR("%d"), info.mapper_num);
	dlgOpenRom_SetItemText(hList, iItem, 2, szBuf);

	ines_snprintf(szBuf, count_of(szBuf), ISTR("%d KB"), info.prm_kb);
	dlgOpenRom_SetItemText(hList, iItem, 3, szBuf);

	ines_snprintf(szBuf, count_of(szBuf), ISTR("%d KB"), info.chr_kb);
	dlgOpenRom_SetItemText(hList, iItem, 4, szBuf);

	dlgOpenRom_SetItemText(hList, iItem, 5, dlgOpenRom_GetMirrorText(info.mirror_type));
	dlgOpenRom_SetItemText(hList, iItem, 6, (info.has_sram != 0) ? ISTR("有") : ISTR("无"));
	dlgOpenRom_SetItemText(hList, iItem, 7, (info.has_trainer != 0) ? ISTR("有") : ISTR("无"));

	return ines_true;
}


/**
 * 点击表头: 按该列排序。
 *
 * 同一列重复点击切换升/降序, 换到其它列时默认升序。
 *
 * @param hDlg    对话框窗口
 * @param iColumn 列序号(0 为文件名)
 */
static VOID dlgOpenRom_OnColumnClick(HWND hDlg, ines_int_t iColumn)
{
	HWND  hList;

	if(iColumn < 0 || iColumn >= (ines_int_t)count_of(s_columns))
		return;

	hList = GetDlgItem(hDlg, IDC_OPENROM_LIST);
	if(hList == NULL)
		return;

	if(iColumn == s_iSortColumn)
	{
		s_bSortAsc = (s_bSortAsc) ? ines_false : ines_true;
	}
	else
	{
		s_iSortColumn = iColumn;
		s_bSortAsc    = ines_true;
	}

	INES_LOG(LOG_DBG, MOD_SYS, ISTR("sort list by column %d (%s).\n"),
		iColumn, (s_bSortAsc) ? ISTR("asc") : ISTR("desc"));

	dlgOpenRom_SortList(hList);
}


/**
 * 按当前排序列与方向重排列表, 并同步表头上的排序箭头。
 */
static VOID dlgOpenRom_SortList(HWND hList)
{
	if(hList == NULL || s_iSortColumn < 0)
		return;

	if(s_bScanning)
	{
		// 文件头属性还没解析完, 此时排序结果不可信, 留到解析结束再统一应用
		INES_LOG(LOG_DBG, MOD_SYS, ISTR("scanning, sort by column %d deferred.\n"), s_iSortColumn);
		return;
	}

	if(!ListView_SortItems(hList, dlgOpenRom_CompareItems, (LPARAM)s_iSortColumn))
	{
		INES_LOG(LOG_WAR, MOD_SYS, ISTR("sort list by column %d failed: %d\n"),
			s_iSortColumn, (ines_int_t)GetLastError());
	}

	dlgOpenRom_UpdateHeaderArrow(hList);
}


/**
 * 在表头对应列上显示排序方向箭头(HDF_SORTUP / HDF_SORTDOWN)。
 *
 * 该箭头需要 comctl32 v6(启用视觉样式)才会被绘制, 老版本下不会显示,
 * 但不影响排序本身。
 */
static VOID dlgOpenRom_UpdateHeaderArrow(HWND hList)
{
	HWND        hHeader;
	HDITEM      hd;
	ines_int_t  i;

	hHeader = ListView_GetHeader(hList);
	if(hHeader == NULL)
		return;

	for(i = 0; i < (ines_int_t)count_of(s_columns); i++)
	{
		memset(&hd, 0, sizeof(hd));
		hd.mask = HDI_FORMAT;

		if(!Header_GetItem(hHeader, i, &hd))
			continue;

		// 先清掉可能残留的旧箭头
		hd.fmt &= ~(HDF_SORTUP | HDF_SORTDOWN);

		if(i == s_iSortColumn)
		{
			// 箭头与文本对齐方式共用同一个字段, 需要一并写回
			hd.fmt &= ~HDF_JUSTIFYMASK;
			hd.fmt |= (s_columns[i].fmt == LVCFMT_RIGHT) ? HDF_RIGHT : HDF_LEFT;
			hd.fmt |= (s_bSortAsc) ? HDF_SORTUP : HDF_SORTDOWN;
		}

		Header_SetItem(hHeader, i, &hd);
	}
}


/**
 * 为列表项分配排序数据: 结构体 + 文件名副本。
 *
 * 结构体尾部是 szName[1] 的变长空间, 这里按文件名实际长度一次性分配,
 * 避免为每一项占用完整的路径缓冲。
 *
 * @param szName 文件名(不含目录)
 * @param pInfo  文件头解析结果
 * @return 分配好的排序数据; 失败返回 NULL
 */
static dlgOpenRom_item_t* dlgOpenRom_AllocItem(ines_cstr_t szName, const dlgOpenRom_rominfo_t* pInfo)
{
	dlgOpenRom_item_t*  pItem;
	ines_size_t         nLen;
	ines_size_t         nSize;

	if(szName == NULL || pInfo == NULL)
		return NULL;

	nLen  = _tcslen(szName);
	nSize = sizeof(dlgOpenRom_item_t) + (nLen + 1) * sizeof(ines_char_t);

	pItem = (dlgOpenRom_item_t*)ines_alloc(nSize);
	if(pItem == NULL)
		return NULL;

	memset(pItem, 0, nSize);

	pItem->info    = *pInfo;
	pItem->bParsed = ines_false;
	memcpy(pItem->szName, szName, (nLen + 1) * sizeof(ines_char_t));

	return pItem;
}


/**
 * 释放全部列表项的排序数据(重新扫描或销毁对话框时调用)。
 */
static VOID dlgOpenRom_FreeItems(VOID)
{
	ines_int_t  i;

	for(i = 0; i < s_nItemCount; i++)
	{
		if(s_pItems[i] != NULL)
		{
			ines_free(s_pItems[i]);
			s_pItems[i] = NULL;
		}
	}

	s_nItemCount = 0;
}


/**
 * 从记录中摘除并释放一个列表项的排序数据(解析阶段剔除非法文件时使用)。
 *
 * @param pItem 要移除的项, 应来自 s_pItems
 */
static VOID dlgOpenRom_RemoveItem(dlgOpenRom_item_t* pItem)
{
	ines_int_t  i;

	if(pItem == NULL)
		return;

	for(i = 0; i < s_nItemCount; i++)
	{
		if(s_pItems[i] != pItem)
			continue;

		// 用尾部元素填补空洞, 保持数组紧凑
		s_nItemCount--;
		s_pItems[i] = s_pItems[s_nItemCount];
		s_pItems[s_nItemCount] = NULL;

		ines_free(pItem);
		return;
	}

	// 防御: 理论上不会走到这里, 仍然释放以免泄漏
	INES_LOG(LOG_WAR, MOD_SYS, ISTR("list item data not tracked, free it anyway.\n"));
	ines_free(pItem);
}


/**
 * 按列比较两个列表项(升序语义)。
 *
 * @param iColumn 列序号, 0 为文件名, 1-7 为文件头属性
 * @param p1/p2   待比较的列表项排序数据
 * @return 负数/0/正数, 语义同 strcmp
 */
static ines_int_t dlgOpenRom_CompareColumn(ines_int_t iColumn,
	const dlgOpenRom_item_t* p1, const dlgOpenRom_item_t* p2)
{
	ines_int64_t  v1;
	ines_int64_t  v2;
	ines_int_t    iCmp;

	switch(iColumn)
	{
	case 1:
		v1 = p1->info.file_size;
		v2 = p2->info.file_size;
		break;

	case 2:
		v1 = p1->info.mapper_num;
		v2 = p2->info.mapper_num;
		break;

	case 3:
		v1 = p1->info.prm_kb;
		v2 = p2->info.prm_kb;
		break;

	case 4:
		v1 = p1->info.chr_kb;
		v2 = p2->info.chr_kb;
		break;

	case 5:
		v1 = p1->info.mirror_type;
		v2 = p2->info.mirror_type;
		break;

	case 6:
		v1 = p1->info.has_sram;
		v2 = p2->info.has_sram;
		break;

	case 7:
		v1 = p1->info.has_trainer;
		v2 = p2->info.has_trainer;
		break;

	default:
		// 第 0 列(文件名): 不区分大小写的字符串比较
		iCmp = _tcsicmp(p1->szName, p2->szName);
		return (iCmp < 0) ? -1 : ((iCmp > 0) ? 1 : 0);
	}

	if(v1 == v2)
		return 0;

	return (v1 < v2) ? -1 : 1;
}


/**
 * 列表项比较回调(供 ListView_SortItems 调用)。
 *
 * @param lParam1/lParam2 列表项上挂接的 dlgOpenRom_item_t*
 * @param lParamSort      列序号
 * @return 排序结果, 方向由当前的 s_bSortAsc 决定
 */
static int CALLBACK dlgOpenRom_CompareItems(LPARAM lParam1, LPARAM lParam2, LPARAM lParamSort)
{
	const dlgOpenRom_item_t*  p1;
	const dlgOpenRom_item_t*  p2;
	ines_int_t                iCmp;

	p1 = (const dlgOpenRom_item_t*)lParam1;
	p2 = (const dlgOpenRom_item_t*)lParam2;

	// 防御: 未挂接排序数据的项视为相等
	if(p1 == NULL || p2 == NULL)
		return 0;

	iCmp = dlgOpenRom_CompareColumn((ines_int_t)lParamSort, p1, p2);

	// 主键相同的项再按文件名比较, 保证排序结果稳定可预期
	if(iCmp == 0)
		iCmp = _tcsicmp(p1->szName, p2->szName);

	if(iCmp == 0)
		return 0;

	if(s_bSortAsc)
		return (iCmp < 0) ? -1 : 1;

	return (iCmp < 0) ? 1 : -1;
}


/**
 * 弹出系统“选择文件夹”对话框。
 */
static VOID dlgOpenRom_OnBrowseDir(HWND hDlg)
{
	BROWSEINFO   bi;
	LPITEMIDLIST pidl;
	ines_char_t  szDir[INES_MAX_PATH];

	memset(&bi, 0, sizeof(bi));
	memset(szDir, 0, sizeof(szDir));

	bi.hwndOwner      = hDlg;
	bi.pszDisplayName = szDir;
	bi.lpszTitle      = ISTR("请选择 NES 文件所在文件夹");
	bi.ulFlags        = BIF_RETURNONLYFSDIRS;
	bi.lpfn           = dlgOpenRom_BrowseCallback;
	bi.lParam         = (s_szCurDir[0] != 0) ? (LPARAM)s_szCurDir : 0;

	pidl = SHBrowseForFolder(&bi);
	if(pidl == NULL)
		return;

	if(SHGetPathFromIDList(pidl, szDir))
	{
		_tcsncpy(s_szCurDir, szDir, count_of(s_szCurDir) - 1);
		s_szCurDir[count_of(s_szCurDir) - 1] = 0;

		SetDlgItemText(hDlg, IDC_OPENROM_EDT_DIR, s_szCurDir);
		dlgOpenRom_ScanDir(hDlg);
	}

	// SHBrowseForFolder 返回的 PIDL 由调用方释放
	CoTaskMemFree(pidl);
}


/**
 * SHBrowseForFolder 回调: 让对话框默认定位到当前目录。
 */
static int CALLBACK dlgOpenRom_BrowseCallback(HWND hWnd, UINT uMsg, LPARAM lParam, LPARAM lpData)
{
	UNREFERENCED_PARAMETER(lParam);

	if(uMsg == BFFM_INITIALIZED && lpData != 0)
	{
		SendMessage(hWnd, BFFM_SETSELECTION, TRUE, lpData);
	}

	return 0;
}


/**
 * 应用路径输入框手工输入的目录。
 */
static VOID dlgOpenRom_ApplyTypedDir(HWND hDlg)
{
	ines_char_t  szDir[INES_MAX_PATH];
	DWORD        dwAttr;

	szDir[0] = 0;
	GetDlgItemText(hDlg, IDC_OPENROM_EDT_DIR, szDir, (ines_int_t)count_of(szDir));
	dlgOpenRom_TrimPath(szDir);

	if(szDir[0] == 0)
		return;

	dwAttr = GetFileAttributes(szDir);
	if(dwAttr == INVALID_FILE_ATTRIBUTES || 0 == (dwAttr & FILE_ATTRIBUTE_DIRECTORY))
	{
		MessageBox(hDlg, ISTR("文件夹不存在或不可访问!"), ISTR("iNes"), MB_OK | MB_ICONWARNING);
		SetFocus(GetDlgItem(hDlg, IDC_OPENROM_EDT_DIR));
		return;
	}

	_tcsncpy(s_szCurDir, szDir, count_of(s_szCurDir) - 1);
	s_szCurDir[count_of(s_szCurDir) - 1] = 0;

	// 回写规范化后的路径
	SetDlgItemText(hDlg, IDC_OPENROM_EDT_DIR, s_szCurDir);

	dlgOpenRom_ScanDir(hDlg);
}


/**
 * 取当前选中的文件, 记录完整路径并结束对话框。
 */
static BOOL dlgOpenRom_OnLoad(HWND hDlg)
{
	HWND         hList;
	LVITEM       item;
	ines_char_t  szName[INES_MAX_PATH];
	ines_char_t  szPath[INES_MAX_PATH];
	ines_int_t   iSel;

	hList = GetDlgItem(hDlg, IDC_OPENROM_LIST);
	if(hList == NULL)
		return FALSE;

	iSel = ListView_GetNextItem(hList, -1, LVNI_SELECTED);
	if(iSel < 0)
		return FALSE;

	memset(szName, 0, sizeof(szName));
	memset(&item, 0, sizeof(item));
	item.mask       = LVIF_TEXT;
	item.iItem      = iSel;
	item.iSubItem   = 0;
	item.pszText    = szName;
	item.cchTextMax = (ines_int_t)count_of(szName);

	if(!ListView_GetItem(hList, &item) || szName[0] == 0)
		return FALSE;

	dlgOpenRom_BuildPath(szPath, count_of(szPath), s_szCurDir, szName);

	// 加载前再确认一次文件是否仍然存在
	if(GetFileAttributes(szPath) == INVALID_FILE_ATTRIBUTES)
	{
		MessageBox(hDlg, ISTR("文件已不存在, 请重新选择!"), ISTR("iNes"), MB_OK | MB_ICONWARNING);
		dlgOpenRom_ScanDir(hDlg);
		return FALSE;
	}

	_tcsncpy(s_szSelected, szPath, count_of(s_szSelected) - 1);
	s_szSelected[count_of(s_szSelected) - 1] = 0;

	EndDialog(hDlg, IDOK);

	return TRUE;
}


/**
 * 命令消息处理。
 */
static INT_PTR dlgOpenRom_OnCommand(HWND hDlg, UINT nID, UINT nCode)
{
	UNREFERENCED_PARAMETER(nCode);

	switch(nID)
	{
	case IDOK:
		// 路径输入框内回车 => 按输入的路径刷新列表, 而不是加载 ROM
		if(GetFocus() == GetDlgItem(hDlg, IDC_OPENROM_EDT_DIR))
		{
			dlgOpenRom_ApplyTypedDir(hDlg);
			return (INT_PTR)TRUE;
		}

		dlgOpenRom_OnLoad(hDlg);
		return (INT_PTR)TRUE;

	case IDCANCEL:
		EndDialog(hDlg, IDCANCEL);
		return (INT_PTR)TRUE;

	case IDC_OPENROM_BTN_DIR:
		dlgOpenRom_OnBrowseDir(hDlg);
		return (INT_PTR)TRUE;

	default:
		break;
	}

	return (INT_PTR)FALSE;
}


/**
 * 通知消息处理(列表选中变化、双击加载)。
 */
static INT_PTR dlgOpenRom_OnNotify(HWND hDlg, LPNMHDR pNMHDR)
{
	if(pNMHDR == NULL)
		return (INT_PTR)FALSE;

	if((UINT)pNMHDR->idFrom == (UINT)IDC_OPENROM_LIST)
	{
		switch(pNMHDR->code)
		{
		case LVN_ITEMCHANGED:
			dlgOpenRom_UpdateButtons(hDlg);
			return (INT_PTR)TRUE;

		case LVN_COLUMNCLICK:
			// 点击表头: 按该列排序, 同列重复点击切换升降序
			dlgOpenRom_OnColumnClick(hDlg, ((LPNMLISTVIEW)pNMHDR)->iSubItem);
			return (INT_PTR)TRUE;

		case NM_DBLCLK:
			// 双击列表项直接加载
			dlgOpenRom_OnLoad(hDlg);
			return (INT_PTR)TRUE;

		default:
			break;
		}
	}

	return (INT_PTR)FALSE;
}


/**
 * 销毁对话框: 释放列表项排序数据与系统文件夹图标。
 */
static VOID dlgOpenRom_OnDestroy(HWND hDlg)
{
	dlgOpenRom_StopScan(hDlg);
	dlgOpenRom_FreeItems();

	if(s_hFolderIcon != NULL)
	{
		DestroyIcon(s_hFolderIcon);
		s_hFolderIcon = NULL;
	}
}


/**
 * 拼接目录与文件(子)名, 自动处理目录末尾的分隔符。
 */
static VOID dlgOpenRom_BuildPath(ines_str_t szOut, ines_int_t nLen, ines_cstr_t szDir, ines_cstr_t szName)
{
	ines_size_t  nDirLen;

	if(szOut == NULL || nLen <= 0)
		return;

	szOut[0] = 0;

	if(szDir == NULL || szName == NULL)
		return;

	nDirLen = _tcslen(szDir);
	if(nDirLen > 0 && (szDir[nDirLen - 1] == '\\' || szDir[nDirLen - 1] == '/'))
		ines_snprintf(szOut, (ines_size_t)nLen, ISTR("%s%s"), szDir, szName);
	else
		ines_snprintf(szOut, (ines_size_t)nLen, ISTR("%s\\%s"), szDir, szName);
}


/**
 * 去掉路径首尾空白与末尾的目录分隔符(保留 "C:\" 形式)。
 */
static VOID dlgOpenRom_TrimPath(ines_str_t szPath)
{
	ines_size_t  nLen;
	ines_str_t   p;

	if(szPath == NULL)
		return;

	p = szPath;
	while(*p != 0 && _istspace(*p))
		p++;

	if(p != szPath)
		memmove(szPath, p, (_tcslen(p) + 1) * sizeof(ines_char_t));

	nLen = _tcslen(szPath);
	while(nLen > 0 && _istspace(szPath[nLen - 1]))
		szPath[--nLen] = 0;

	while(nLen > 1 && (szPath[nLen - 1] == '\\' || szPath[nLen - 1] == '/'))
	{
		if(nLen == 3 && szPath[1] == ':')
			break;
		szPath[--nLen] = 0;
	}
}


/**
 * 格式化文件大小, 自动选择合适的单位。
 */
static VOID dlgOpenRom_FormatSize(ines_int64_t size, ines_str_t szBuf, ines_int_t nLen)
{
	if(size < 0)
		size = 0;

	if(size < 1024)
		ines_snprintf(szBuf, (ines_size_t)nLen, ISTR("%d B"), (ines_int_t)size);
	else if(size < 1024 * 1024)
		ines_snprintf(szBuf, (ines_size_t)nLen, ISTR("%d KB"), (ines_int_t)(size / 1024));
	else
		ines_snprintf(szBuf, (ines_size_t)nLen, ISTR("%d.%02d MB"),
			(ines_int_t)(size / (1024 * 1024)),
			(ines_int_t)((size % (1024 * 1024)) * 100 / (1024 * 1024)));
}


/**
 * 设置列表项文本(内部转存到可写缓冲, 避免直接改动常量字符串)。
 */
static VOID dlgOpenRom_SetItemText(HWND hList, ines_int_t iItem, ines_int_t iSubItem, ines_cstr_t szText)
{
	ines_char_t  szBuf[128];

	if(szText == NULL)
		szText = ISTR("");

	ines_snprintf(szBuf, count_of(szBuf), ISTR("%s"), szText);
	ListView_SetItemText(hList, iItem, iSubItem, szBuf);
}


/**
 * 镜像方式文本。
 */
static ines_cstr_t dlgOpenRom_GetMirrorText(ines_byte_t mirror_type)
{
	switch(mirror_type)
	{
	case MIRROR_VERT:
		return ISTR("垂直");

	case MIRROR_HORZ:
		return ISTR("水平");

	case MIRROR_FOUR_SCREEN:
		return ISTR("四屏");

	default:
		break;
	}

	return ISTR("未知");
}


/**
 * 解析 iNES 文件头。
 *
 * 只读取文件头(16 字节), 不加载 ROM 数据; 判定与解析规则与
 * core/rom.c 的 ines_rom_load_from_file() 保持一致。
 *
 * @param szFile 文件完整路径
 * @param pInfo  [out] 解析结果
 * @return ines_true 表示是受支持的 iNES 文件
 */
static ines_bool_t dlgOpenRom_ParseNesHeader(ines_cstr_t szFile, dlgOpenRom_rominfo_t* pInfo)
{
	FILE*                pf;
	ines_file_header_t   header;
	ines_size_t          nRead;

	if(szFile == NULL || pInfo == NULL)
		return ines_false;

	memset(pInfo, 0, sizeof(*pInfo));

	pf = _tfopen(szFile, ISTR("rb"));
	if(pf == NULL)
	{
		INES_LOG(LOG_DBG, MOD_SYS, ISTR("open file `%s` failed: (%d)%s\n"), szFile, errno, _tcserror(errno));
		return ines_false;
	}

	nRead = fread(&header, 1, INES_FILE_HEADER_SIZE, pf);
	fclose(pf);

	if(nRead != INES_FILE_HEADER_SIZE)
		return ines_false;

	// 文件标识 "NES\x1a"
	if(0 != memcmp(header.tag, INES_FILE_TAG, sizeof(header.tag)))
		return ines_false;

	// PRG 至少 1 块, 否则视为非法文件
	if(header.PROM_block_num == 0)
		return ines_false;

	pInfo->mapper_num  = (ines_byte_t)((header.flag1 >> 4) | (header.flag2 & 0xF0));
	pInfo->mirror_type = (header.flag1 & 0x08) ? MIRROR_FOUR_SCREEN
						 : ((header.flag1 & 0x01) ? MIRROR_VERT : MIRROR_HORZ);
	pInfo->has_sram    = (header.flag1 & 0x02) ? 1 : 0;
	pInfo->has_trainer = (header.flag1 & 0x04) ? 1 : 0;
	pInfo->prm_kb      = (ines_word_t)(header.PROM_block_num * (INES_PROM_BLOCK_SIZE / 1024));
	pInfo->chr_kb      = (ines_word_t)(header.VROM_block_num * (INES_VROM_BLOCK_SIZE / 1024));

	return ines_true;
}
