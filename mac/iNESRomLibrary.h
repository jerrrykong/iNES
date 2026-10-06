#ifndef __INES_ROM_LIBRARY_H__
#define __INES_ROM_LIBRARY_H__


#import <Cocoa/Cocoa.h>


// =====================================================================
// iNES macOS 前端 —— ROM 库窗口(非模态)
//
// 对应 win32 的 dlgRomLib.c(双端一致性规格见 docs/rom-library-plan.md)。
// 布局(上中下三段):
//   上 — 一行操作区: [设置] [刷新] ...... [搜索:] [输入框];
//   中 — ROM 列表(列定义与"载入 NES 文件"对话框一致, 支持点击表头排序);
//   下 — ROM 完整路径显示框 + [载入] 按钮(无关闭按钮)。
//
// 数据规则:
//   1) ROM 列表只在"路径变化"或"点击刷新"时全量扫描一次, 扫描结果写入
//      <数据目录>/romlib.dat 缓存文件; 之后打开窗口只读缓存, 不再扫描;
//   2) 配置的路径列表、排序方式、选中项、列表显示位置、窗口位置都写入
//      config.ini 的 [romlib] 段, 下次打开时按上次的样式恢复;
//   3) 扫描是懒加载的: 先枚举目录条目(不打开文件)填路径/文件名/大小,
//      再由定时器按时间预算分批读 16 字节文件头补齐属性列, 解析失败的
//      (非 iNES 文件)在解析到时从列表移除;
//   4) 载入 ROM 由 loadRomHandler 回调交给调用方(iNESAppController),
//      本窗口不自行加载, 也不在载入后关闭;
//   5) 搜索只按 ROM 文件名(不含目录)做不区分大小写的包含匹配。
// =====================================================================

@interface iNESRomLibrary : NSWindowController

/** ROM 载入请求回调。返回 YES 表示调用方已接受并开始加载 */
@property (nonatomic, copy) BOOL (^loadRomHandler)(NSString* path);

/**
 * 显示 ROM 库窗口(非模态)。
 *
 * 首次调用会按 config.ini 恢复上次的位置、排序与选中项; 窗口不存在本地缓存时
 * 自动做一次全量扫描并写入缓存。重复调用只是把已有窗口前置。
 *
 * @param owner 所有者窗口(用于居中), 可为 nil
 */
- (void)showLibraryRelativeTo:(NSWindow*)owner;

/** 关闭窗口并释放扫描定时器(应用退出前调用) */
- (void)closeLibrary;

@end


#endif
