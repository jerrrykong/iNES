#ifndef __INES_UI_LAYOUT_H__
#define __INES_UI_LAYOUT_H__


#import <Cocoa/Cocoa.h>


/*
 * i18n 自适应布局辅助(macOS)
 *
 * mac 前端沿用 win32 的手工 frame 布局, 语言切换后译文长度会变, 因此控件尺寸
 * 不能写死。这里的三个函数只做"测量后定位", 不引入 Auto Layout, 也不改变视觉
 * 顺序, 便于将来整体迁移时替换。
 */

/** 标签按译文实际宽度自适应; 同一行后续控件整体右移, 避免重叠。返回标签新的右边界。 */
CGFloat INESFitLabel(NSTextField* label, NSArray<NSView*>* trailing, CGFloat gap);

/** 按钮行自适应: 按标题测宽(不窄于 minWidth), 从右往左依次摆放; 空间不足时加宽窗口。 */
void INESFitButtons(NSWindow* window, NSArray<NSButton*>* buttons, CGFloat margin, CGFloat gap, CGFloat minWidth);

/** 确保窗口内容区宽度不小于 needed(不足则向右加宽, 左侧控件位置不变)。返回最终宽度。 */
CGFloat INESEnsureContentWidth(NSWindow* window, CGFloat needed);

/** 文本在给定字体下的显示宽度(用于表头/列宽等按译文测量的场合)。 */
CGFloat INESTextWidth(NSString* text, NSFont* font);


#endif
