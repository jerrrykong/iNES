#ifndef __INES_DEBUG_VIEW_H__
#define __INES_DEBUG_VIEW_H__


#import <Cocoa/Cocoa.h>

#import "iNESDebug.h"


// =====================================================================
// iNES macOS 前端 —— 调试视图的绘制组件
//
// 两类视图:
//   iNESMemoryView  : 十六进制/ASCII 内存查看器(CPU / VRAM / 精灵内存 三个实例)
//   iNESGraphicView : 索引色图形查看器(卷轴 / 图形 / 色盘 三个实例)
//
// 两者都只从"主线程快照"读取数据(由 iNESDebugManager 定时刷新), 不直接触碰 host。
// =====================================================================


@class iNESMemoryView;


// ---------------------------------------------------------------------
// 内存视图: 用户输入一个十六进制半字节时回调
// ---------------------------------------------------------------------
@protocol iNESMemoryViewDelegate <NSObject>

@required
// 视图已完成"高/低半字节"合并, val 为要写入该地址的完整字节
- (void)memoryView:(iNESMemoryView*)view writeByte:(ines_byte_t)val atAddr:(ines_int_t)addr;

@optional
// 光标地址变化(用于外部状态栏显示)
- (void)memoryViewDidChangeCursor:(iNESMemoryView*)view;

@end


@interface iNESMemoryView : NSView

// IDBG_SPACE_CPU / IDBG_SPACE_VRAM / IDBG_SPACE_SPRAM
@property (nonatomic, assign) ines_int_t   space;

// 主线程快照(由窗口管理器的定时器刷新, 视图只读)
@property (nonatomic, assign) const ines_dbg_snapshot_t*  snapshot;

@property (nonatomic, weak) id<iNESMemoryViewDelegate>  delegate;

- (instancetype)initWithSpace:(ines_int_t)space;

// 未载入 ROM 时是否有内容可显示(由窗口管理器判断并置灰色)
- (BOOL)hasContent;

@end


@class iNESGraphicView;


// ---------------------------------------------------------------------
// 图形视图: 图形查看器点击切换图案索引后回调(用于同步窗口标题)
// ---------------------------------------------------------------------
@protocol iNESGraphicViewDelegate <NSObject>

@required
- (void)graphicView:(iNESGraphicView*)view didChangePatternIndex:(ines_int_t)patIdx;

@end


@interface iNESGraphicView : NSView

// IDBG_GFX_NAMETABLE / IDBG_GFX_PATTERN / IDBG_GFX_PALETTE
@property (nonatomic, assign) ines_int_t   mode;

// 仅 IDBG_GFX_PATTERN 使用: 0-7, bit2 选择 SP/BG, bit0-1 选择调色板
@property (nonatomic, assign) ines_int_t   patIdx;

@property (nonatomic, assign) const ines_dbg_snapshot_t*  snapshot;

@property (nonatomic, weak) id<iNESGraphicViewDelegate>  delegate;

- (instancetype)initWithMode:(ines_int_t)mode;

// 未载入 ROM 时是否有内容可显示
- (BOOL)hasContent;

@end


#endif
