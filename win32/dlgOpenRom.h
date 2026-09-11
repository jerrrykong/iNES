#ifndef __DLG_OPENROM_H__
#define __DLG_OPENROM_H__

#ifdef __cplusplus
extern "C"
{
#endif

/**
 * 显示“载入 NES 文件”管理器对话框(模态)。
 *
 * 界面为上中下三段布局:
 *   上  — 文件夹路径输入框 + 选择文件夹按钮(最右侧, 系统文件夹图标);
 *   中  — 当前文件夹下受支持的 NES 文件列表(可随窗口拉伸), 列出
 *         文件名 / ROM大小 / Mapper / PRG / CHR / 镜像 / 电池 / Trainer;
 *   下  — “加载”与“取消”按钮。
 *
 * @param hInstance    当前实例句柄
 * @param hParentWnd   父窗口, 作为对话框的所有者
 * @param szInitDir    初始浏览目录, 可为 NULL(为空时使用当前工作目录)
 * @param szSelected   [out] 用户选定 ROM 文件的完整路径
 * @param nSelectedLen szSelected 缓冲区的容量(字符数)
 * @param szLastDir    [out] 对话框关闭时所在的文件夹, 供调用方记录“上次使用的目录”;
 *                     可为 NULL 表示不需要
 * @param nLastDirLen  szLastDir 缓冲区的容量(字符数)
 * @return TRUE  用户点击了“加载”, 且 szSelected 中为有效文件路径;
 *         FALSE 取消, 或参数非法
 */
BOOL dlgOpenRom_DoModal(HINSTANCE hInstance, HWND hParentWnd, ines_cstr_t szInitDir,
						ines_str_t szSelected, ines_int_t nSelectedLen,
						ines_str_t szLastDir, ines_int_t nLastDirLen);

#ifdef __cplusplus
}
#endif

#endif
