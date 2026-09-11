#ifndef __INES_VIDEO_H__
#define __INES_VIDEO_H__


#import <Cocoa/Cocoa.h>

#include "../core/nes.h"


// 显示比例
typedef enum
{
	INES_ASPECT_ORIGINAL = 0,   // 256:240(像素为正方形, 与 win32 前端一致)
	INES_ASPECT_4_3      = 1,   // 4:3
	INES_ASPECT_16_9     = 2    // 16:9
} iNESAspectMode;


// 与 NES 手柄相关的逻辑按键(位掩码), 由视图维护并上报给应用层
#define IKEY_UP        (1 << 0)
#define IKEY_DOWN      (1 << 1)
#define IKEY_LEFT      (1 << 2)
#define IKEY_RIGHT     (1 << 3)
#define IKEY_A         (1 << 4)
#define IKEY_B         (1 << 5)
#define IKEY_TURBO_A   (1 << 6)
#define IKEY_TURBO_B   (1 << 7)
#define IKEY_SELECT    (1 << 8)
#define IKEY_START     (1 << 9)


@class iNESVideoView;


@protocol iNESVideoViewDelegate <NSObject>

@required
// 按键集合发生变化(按下/抬起/失焦清空)
- (void)videoViewKeyStateDidChange:(iNESVideoView*)view;

@optional
// 拖入 ROM 文件
- (void)videoView:(iNESVideoView*)view didDropFilePaths:(NSArray<NSString*>*)paths;

@end


// 画面视图: 把核心输出的 8bit 索引色画面转成 BGRA 后按最近邻缩放显示。
// presentIndexedPixels: 允许在模拟线程调用(内部加锁)。
@interface iNESVideoView : NSView

// 期望的窗口缩放百分比(100/200/300/400), 仅用于外部计算窗口大小
@property (nonatomic, assign) ines_int_t scalePercent;
// 显示比例
@property (nonatomic, assign) ines_int_t aspectMode;
// 是否显示 CPU 占用等 OSD 信息
@property (nonatomic, assign) BOOL showOsd;
// 按键状态回调
@property (nonatomic, weak) id<iNESVideoViewDelegate> delegate;
// 当前按下的逻辑按键位掩码(IKEY_*)
@property (nonatomic, readonly) ines_dword_t pressedKeys;

// 提交一帧(自底向上的 8bit 索引色画面), 线程安全
- (void)presentIndexedPixels:(const ines_byte_t*)pPixels;

// 清除画面(卸载 ROM 后调用)
- (void)clearScreen;

// 清空按键状态(窗口失焦时调用, 避免"卡键")
- (void)resetKeys;

// 按当前缩放比例计算窗口内容区大小
- (NSSize)preferredContentSize;

@end


#endif
