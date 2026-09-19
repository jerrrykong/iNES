#ifndef __DLG_LAN_MATCH_H__
#define __DLG_LAN_MATCH_H__

#include "../comm/idef.h"


#ifdef __cplusplus
extern "C"
{
#endif


/**
 * 打开"局域网快速对战"对话框(快速配对)。
 *
 * 面板一打开即以服务端身份在局域网内发布房间, 用户选择别人的房间后本机转为客户机接入。
 * 与 mac/iNESLanLobby 使用同一套发现层(comm/lan)与会话层(comm/npsession), 因此
 * win32 与 macOS 可以互相发现、互相对战。
 *
 * @param hInstance   模块句柄
 * @param hParentWnd  父窗口
 * @param crc32       本机 ROM 的 crc32(用于比对对端的 ROM 是否一致)
 * @return TRUE 配对成功(可开始对战); FALSE 用户取消或无法开启发现
 */
BOOL dlgLanMatch_DoModal(HINSTANCE hInstance, HWND hParentWnd, ines_dword_t crc32);


#ifdef __cplusplus
};
#endif


#endif
