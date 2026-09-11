#ifndef __INES_APP_H__
#define __INES_APP_H__


#import <Cocoa/Cocoa.h>

// iNESVideoViewDelegate / iNESVideoView 的定义
#import "iNESVideo.h"

#include "../core/nes.h"
#include "../comm/thread.h"


// ---------------------------------------------------------------------
// 全局状态(命名与 win32 前端保持一致, 便于对照维护)
// ---------------------------------------------------------------------

extern ines_host_t     host;
// 最近一次显示的画面(自底向上的 8bit 索引色)
extern ines_byte_t     screen_front[SCREEN_IMAGE_BYTES];
// CPU 占用率(百分比 0~100)
extern ines_int_t      nes_cpu_rate;
// 当前 ROM 的标题, 用于存档/读档/截图命名
extern ines_char_t     szROMTitle[INES_MAX_TITLE];
// 窗口缩放百分比(100/200/300/400)
extern ines_int_t      screen_scale;
// 最近打开的 10 个文件
extern ines_char_t     latest_open_files[10][INES_MAX_PATH];
// CPU 指令跟踪开关(定义在 core/cpu.c)
extern ines_int_t      nes_cpu_trace_ops;


// ---------------------------------------------------------------------
// 应用控制器
// ---------------------------------------------------------------------
@interface iNESAppController : NSObject
				<NSApplicationDelegate, NSWindowDelegate, NSMenuDelegate, iNESVideoViewDelegate>

+ (iNESAppController*)sharedInstance;

// 启动(创建窗口/菜单/模拟线程), 由 applicationDidFinishLaunching 调用
- (void)startup;
// 停止并释放资源
- (void)shutdown;

// 命令行传入的 ROM 路径(在 startup 之前设置)
- (void)setPendingRomPath:(NSString*)szPath;

// 菜单动作
- (IBAction)openROM:(id)sender;
- (IBAction)closeROM:(id)sender;
- (IBAction)hardReset:(id)sender;
- (IBAction)softReset:(id)sender;
- (IBAction)togglePause:(id)sender;
- (IBAction)frameStep:(id)sender;
- (IBAction)takeSnapshot:(id)sender;
- (IBAction)toggleFullScreen:(id)sender;
- (IBAction)setScale:(id)sender;
- (IBAction)setAspect:(id)sender;
- (IBAction)toggleMute:(id)sender;
- (IBAction)setVolume:(id)sender;
- (IBAction)saveState:(id)sender;
- (IBAction)loadState:(id)sender;
- (IBAction)setLogLevel:(id)sender;
- (IBAction)toggleCpuTrace:(id)sender;
- (IBAction)toggleOsd:(id)sender;
- (IBAction)openRecentFile:(id)sender;
- (IBAction)showUnimplemented:(id)sender;
- (IBAction)showAbout:(id)sender;

@end


#endif
