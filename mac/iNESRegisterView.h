#ifndef __INES_REGISTER_VIEW_H__
#define __INES_REGISTER_VIEW_H__


#import <Cocoa/Cocoa.h>

#import "iNESDebug.h"


// =====================================================================
// iNES macOS 前端 —— 寄存器查看器
//
// 对应 win32 的"寄存器"菜单项(IDM_VIEW_REG): win32 端为空实现, mac 端补齐,
// 设计见 docs/register-view-plan.md。
//
// 一行一个寄存器: 名称 / 地址 / 值 / 位格 / 说明, 覆盖 CPU + PPU + APU + I/O。
// 数据只来自主线程快照(由 iNESDebugManager 定时刷新), 写入一律经 delegate
// 投递到模拟线程(ines_dbg_post_reg_write), 视图本身不触碰 host。
// =====================================================================


@class iNESRegisterView;


// ---------------------------------------------------------------------
// 视图 -> 管理器: 寄存器写入请求
// ---------------------------------------------------------------------
@protocol iNESRegisterViewDelegate <NSObject>

@required
// 视图已按"整值 / 单个位"合并好, val 为该寄存器的完整值(8/16 位一次投递)
- (void)registerView:(iNESRegisterView*)view writeReg:(ines_int_t)regId value:(ines_int_t)val;

@optional
// 危险写入前的确认($4014 OAMDMA 会立即触发 256 字节 DMA)。返回 NO 表示放弃
- (BOOL)registerView:(iNESRegisterView*)view confirmReg:(ines_int_t)regId value:(ines_int_t)val;

@end


@interface iNESRegisterView : NSView

// 主线程快照(由窗口管理器的定时器刷新, 视图只读)
@property (nonatomic, assign) const ines_dbg_snapshot_t*  snapshot;

@property (nonatomic, weak) id<iNESRegisterViewDelegate>  delegate;

// 推荐的初始客户区大小: 五列(名称/地址/值/位格/说明)完整可见 + 一点空隙
+ (NSSize)suggestedContentSize;

// 未载入 ROM 时是否有内容可显示
- (BOOL)hasContent;

@end


#endif
