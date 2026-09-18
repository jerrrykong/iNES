
#ifndef __INES_LAN_LOBBY_H__
#define __INES_LAN_LOBBY_H__

#import <Cocoa/Cocoa.h>

#include "../comm/lan.h"


// =====================================================================
// iNES macOS 前端 —— 局域网快速配对面板
//
// 与"网络对战…"(手动填 IP)并存, 两者共用同一套会话层:
//   1) 面板打开即以服务端身份监听并广播自己的房间;
//   2) 双击列表里的房间即以客户机身份接入(先放弃监听);
//   3) 握手成功(np_poll 返回 NP_POLL_OK)后关闭面板, 由 iNESApp 请求硬复位开打。
//
// 角色规则: 先发布者 = 服务端, 后加入者 = 客户机(不可协商)。
// 线程: 全部在主线程(500ms 定时器)驱动; 发现与对战在时间上互斥, 无需加锁。
// =====================================================================


@interface iNESLanLobby : NSWindowController

/**
 * 以模态方式运行配对面板。
 *
 * @param crc32   本机 ROM 校验码(只有相同值的房间可选)
 * @param romName 本机 ROM 名(广播给对端看)
 * @param owner   所有者窗口(用于居中), 可为 nil
 * @return YES 表示已配对成功(会话处于 NP_ST_PLAYING), 调用方应进入联网对战;
 *         NO 表示用户取消或无法开启
 */
+ (BOOL)runModalWithCrc32:(ines_dword_t)crc32 romName:(NSString*)romName owner:(NSWindow*)owner;

@end


#endif
