#ifndef __INES_NETPLAY_DIALOG_H__
#define __INES_NETPLAY_DIALOG_H__


#import <Cocoa/Cocoa.h>

#include "../comm/idef.h"


// =====================================================================
// iNES macOS 前端 —— "网络对战"对话框
//
// 与 win32/dlgNetPlay.c + IDD_NETPLAY(win32/iNES.rc:234-251) 一一对应:
//   运行为(服务器 / 客户机) / 地址 / 端口 / 缓冲帧数 / 提示信息 / 开始 / 取消。
//
// 行为要点(与 win32 保持一致):
//   1) 默认服务器 + 地址 127.0.0.1 + 端口 8891 + 缓冲帧数 4;
//   2) "地址"仅客户机可用, "缓冲帧数"仅服务器可设(由服务器下发, 两端严格一致);
//   3) 点"开始"后以 50ms 周期推进握手(等价 win32 的 SetTimer(50ms)),
//      连接中"开始"置灰; 校验通过后对话框以 OK 结束;
//   4) 连接中点"取消"只中止本次尝试并留在对话框内(可改参数重试),
//      未连接时点"取消"直接关闭;
//   5) 失败/5 秒超时: 关闭链路并提示原因(win32 为 MessageBox)。
// =====================================================================

@interface iNESNetPlayDialog : NSWindowController

/**
 * 以模态方式显示"网络对战"对话框。
 *
 * @param crc32 当前 ROM 的 crc32, 用于与对端校验(不一致时拒绝连接)
 * @param owner 作为对话框所有者的窗口(用于居中), 可为 nil
 * @return 校验通过、可以开始对战时返回 YES; 取消或失败返回 NO
 */
+ (BOOL)runModalWithCrc32:(ines_dword_t)crc32 owner:(NSWindow*)owner;

@end


#endif
