#ifndef __INES_OPENROM_DIALOG_H__
#define __INES_OPENROM_DIALOG_H__

#import <Cocoa/Cocoa.h>


// =====================================================================
// iNES macOS 前端 —— "载入 NES 文件"管理器对话框
//
// 与 win32/dlgOpenRom.c 的 dlgOpenRom_DoModal() 一一对应, 界面为上中下三段:
//   上 — 文件夹路径输入框 + 选择文件夹按钮(最右侧, 系统文件夹图标);
//   中 — 当前文件夹下受支持的 NES 文件列表(可随窗口拉伸), 列出
//        文件名 / ROM大小 / Mapper / PRG / CHR / 镜像 / 电池 / Trainer;
//   下 — 文件计数提示 + "加载" / "取消" 按钮。
//
// 行为要点(与 win32 保持一致):
//   1) 列表项只读取 iNES 文件头(16 字节)解析属性, 不加载 ROM 数据;
//   2) 文件头解析与判定规则与 core/rom.c 的 ines_rom_load_from_file() 一致;
//   3) 懒加载: 先枚举目录条目(不打开文件)取文件名与大小, 再由定时器按时间预算
//      分批读文件头补齐其余属性列, 非 iNES 文件在解析到时从列表里移除;
//   4) 点击表头排序: 排序依据是解析出的属性数值而不是列上的显示文本,
//      同一列重复点击切换升/降序, 切换文件夹后保持当前排序方式;
//   5) 路径输入框内回车 => 按输入的路径刷新列表, 而不是加载 ROM;
//   6) 双击列表项、或选中后点"加载"/回车 => 返回该文件的完整路径。
// =====================================================================

@interface iNESOpenRomDialog : NSWindowController

/**
 * 以模态方式显示"载入 NES 文件"对话框。
 *
 * 本方法内部会创建窗口控制器实例并驱动 NSApp 的模态循环, 返回后实例即被释放。
 * 模态期间主窗口的模拟线程不受影响。
 *
 * @param initialDir 初始浏览目录, 可为 nil/空串(此时退化为用户主目录)。
 *                   目录不存在时同样退化为用户主目录。
 * @param owner      作为对话框所有者的窗口(用于居中), 可为 nil。
 * @param outLastDir [out] 对话框关闭时所在的文件夹(点"加载"或"取消"都一样),
 *                   供调用方记录"上次使用的目录"; 可为 NULL。
 * @return 用户确认加载时返回所选 ROM 文件的完整路径; 取消时返回 nil。
 */
+ (NSString*)runModalWithInitialDir:(NSString*)initialDir
                              owner:(NSWindow*)owner
                           lastDir:(NSString**)outLastDir;

@end


#endif
