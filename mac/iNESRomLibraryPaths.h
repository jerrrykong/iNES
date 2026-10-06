#ifndef __INES_ROM_LIBRARY_PATHS_H__
#define __INES_ROM_LIBRARY_PATHS_H__


#import <Cocoa/Cocoa.h>


// =====================================================================
// iNES macOS 前端 —— ROM 库的路径设置对话框
//
// 对应 win32 的 dlgRomLibPaths.c(见 docs/rom-library-plan.md), 行为约定:
//   1) 单列列表, 每行为一个 ROM 库路径;
//   2) 下方只有"删除"与"添加"两个按钮, 没有"确定/取消" —— 改动即时生效到
//      列表数据, 对话框关闭时统一告知调用方是否需要重新扫描;
//   3) "删除"删除当前选中的行; 未选中行时该按钮不可用;
//   4) "添加"弹出系统文件夹选择面板, 选中后追加到列表末尾并选中新行;
//   5) 添加前做重复检查: 规范化后的路径已存在则提示并放弃;
//   6) 只做路径本身的有效性检查(存在且为目录)。
// =====================================================================

@interface iNESRomLibraryPathsDialog : NSWindowController

/**
 * 以模态方式编辑 ROM 库的路径列表。
 *
 * @param paths 待编辑的路径列表(NSMutableArray<NSString*>), 会在对话框内就地修改
 * @param owner 所有者窗口(用于居中), 可为 nil
 * @return YES 表示列表发生了变化, 调用方应立刻重新扫描并回写缓存;
 *         NO 表示用户没有改动任何内容
 */
+ (BOOL)runModalWithPaths:(NSMutableArray<NSString*>*)paths
                    owner:(NSWindow*)owner;

/**
 * 路径规范化(供本模块与 ROM 库窗口共用两端一致的路径比较规则):
 * 去掉首尾空白与末尾的目录分隔符(根目录 "/" 除外), 再交给
 * NSString.stringByStandardizingPath 展开 "~" 与符号链接。
 *
 * @param path 原始路径, 可为 nil
 * @return 规范化后的路径; 输入为空时返回 nil
 */
+ (NSString*)normalizedPath:(NSString*)path;

@end


#endif
