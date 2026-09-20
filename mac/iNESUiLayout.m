/*
 * i18n 自适应布局辅助(实现)
 *
 * 只依赖控件自身的测量结果(intrinsicContentSize / 文本度量), 不引入 Auto Layout。
 */

#import "iNESUiLayout.h"


#define INES_UI_MARGIN   18.0      /* 对话框内容区左右边距(与现有布局一致) */


CGFloat INESFitLabel(NSTextField* label, NSArray<NSView*>* trailing, CGFloat gap)
{
	NSWindow*  window;
	NSView*    root;
	NSSize     fit;
	CGFloat    want;
	CGFloat    delta;
	CGFloat    right = 0;
	NSRect     frame;

	if (label == nil)
		return 0;

	fit = (label.font != nil)
		? [label.stringValue sizeWithAttributes:@{ NSFontAttributeName : label.font }]
		: NSMakeSize(0.0, 0.0);

	want = ceil(fit.width) + 2.0;      /* +2: 度量取整误差, 避免末尾字符被切 */
	if (want <= label.frame.size.width)
		return NSMaxX(label.frame);

	delta = want - label.frame.size.width;

	frame = label.frame;
	frame.size.width = want;
	label.frame = frame;

	for (NSView* view in trailing)
	{
		if (view == nil)
			continue;

		frame = view.frame;
		frame.origin.x += delta;
		view.frame = frame;

		right = MAX(right, NSMaxX(frame));
	}

	/* 右移后超出内容区则加宽窗口(左边控件不动, 不会造成重叠) */
	window = label.window;
	root   = (window != nil) ? window.contentView : nil;
	if (root != nil)
		INESEnsureContentWidth(window, right + INES_UI_MARGIN);

	return NSMaxX(label.frame);
}

void INESFitButtons(NSWindow* window, NSArray<NSButton*>* buttons, CGFloat margin, CGFloat gap, CGFloat minWidth)
{
	NSView*    root;
	CGFloat    total = 0.0;
	CGFloat    content_w;
	CGFloat*   widths;
	NSUInteger count;
	NSUInteger i;
	CGFloat    x;

	if (window == nil || buttons.count == 0)
		return;

	root = window.contentView;
	if (root == nil)
		return;

	count  = buttons.count;
	widths = (CGFloat*)calloc(count, sizeof(CGFloat));
	if (widths == NULL)
		return;

	for (i = 0; i < count; i++)
	{
		NSButton*  button = buttons[i];
		NSSize     fit    = button.intrinsicContentSize;
		CGFloat    w      = MAX(minWidth, ceil(fit.width) + 20.0);

		widths[i] = w;
		total    += w;
	}
	total += gap * (CGFloat)(count - 1);

	content_w = root.frame.size.width;
	if (total > (content_w - margin * 2.0))
		content_w = INESEnsureContentWidth(window, total + margin * 2.0);

	/* 从右往左摆放: buttons 数组的顺序即视觉上从左到右 */
	x = content_w - margin;
	for (i = count; i > 0; i--)
	{
		NSButton*  button = buttons[i - 1];
		NSRect     frame  = button.frame;

		x -= widths[i - 1];

		frame.origin.x   = x;
		frame.size.width = widths[i - 1];
		button.frame     = frame;

		x -= gap;
	}

	free(widths);
}

CGFloat INESTextWidth(NSString* text, NSFont* font)
{
	if (text == nil || text.length == 0)
		return 0;

	if (font == nil)
		font = [NSFont systemFontOfSize:[NSFont systemFontSize]];

	return ceil([text sizeWithAttributes:@{ NSFontAttributeName : font }].width);
}

CGFloat INESEnsureContentWidth(NSWindow* window, CGFloat needed)
{
	NSView*  root;
	NSSize   size;

	if (window == nil)
		return 0;

	root = window.contentView;
	if (root == nil)
		return 0;

	if (needed <= root.frame.size.width)
		return root.frame.size.width;

	size = root.frame.size;
	size.width = needed;

	[window setContentSize:size];

	return needed;
}
