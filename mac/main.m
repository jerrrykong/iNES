// =====================================================================
// iNES macOS 前端 —— 程序入口
//
// 对应 win32/iNES.c 的 _tWinMain:
//   * 解析命令行给出的 ROM 路径(也支持 Finder 的"打开方式"与拖拽);
//   * 创建 NSApplication 并指定 iNESAppController 为代理,
//     真正的启动流程在 applicationDidFinishLaunching: 里完成。
//
// 说明: 本程序不使用 nib/xib, 菜单栏完全由 iNESAppController 用代码构建,
//       因此这里直接调用 -run 而不走 NSApplicationMain。
// =====================================================================

#import <Cocoa/Cocoa.h>

#import "iNESApp.h"


int main(int argc, const char* argv[])
{
	@autoreleasepool
	{
		NSApplication*  app = [NSApplication sharedApplication];
		NSString*       rom_path = nil;
		int             i;

		// 命令行中第一个非选项参数视为 ROM 路径
		for (i = 1; i < argc; i++)
		{
			if ((argv[i] == NULL) || (argv[i][0] == 0) || (argv[i][0] == '-'))
				continue;

			rom_path = [NSString stringWithUTF8String:argv[i]];
			if (rom_path != nil)
				break;
		}

		// 有窗口的程序需要在 Dock 中显示(从终端启动时也生效)
		[app setActivationPolicy:NSApplicationActivationPolicyRegular];

		app.delegate = [iNESAppController sharedInstance];

		if (rom_path != nil)
			[[iNESAppController sharedInstance] setPendingRomPath:rom_path];

		[app run];
	}

	return 0;
}
