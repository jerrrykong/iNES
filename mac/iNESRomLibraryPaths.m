// =====================================================================
// iNES macOS 前端 —— ROM 库的路径设置对话框(实现)
//
// 与 mac/iNESRomLibraryPaths.h 的约定一一对应; 布局算式与 win32 的
// docs/rom-library-plan.md 中的描述保持一致(同一套边距/行高/按钮宽)。
// =====================================================================

#import "iNESRomLibraryPaths.h"

#import "iNESi18n.h"
#import "iNESUiLayout.h"

#import "../comm/log.h"


// ---------------------------------------------------------------------
// 布局尺寸(点)
// ---------------------------------------------------------------------

#define PATHS_MARGIN       8
#define PATHS_BTN_W        88
#define PATHS_BTN_H        24
#define PATHS_BTN_GAP      6
#define PATHS_ROW_GAP      8
// 初始内容区大小
#define PATHS_INIT_W       560
#define PATHS_INIT_H       340
// 最小内容区大小(拉伸下限)
#define PATHS_MIN_W        400
#define PATHS_MIN_H        240


// ---------------------------------------------------------------------
// 内容视图: 翻转坐标系(原点在左上角), 便于照搬 win32 的布局算式
// ---------------------------------------------------------------------

@interface iNESRomLibPathsContentView : NSView
@end

@implementation iNESRomLibPathsContentView

- (BOOL)isFlipped
{
	return YES;
}

@end


// ---------------------------------------------------------------------
// 对话框
// ---------------------------------------------------------------------

@interface iNESRomLibraryPathsDialog () <NSTableViewDataSource, NSTableViewDelegate, NSWindowDelegate>
{
	NSMutableArray<NSString*>*  _paths;   // 路径列表(调用方传入的可变数组, 就地修改)
	BOOL                        _dirty;   // 是否发生过改动
}

@property (nonatomic, strong) NSScrollView*  scrollView;
@property (nonatomic, strong) NSTableView*   listView;
@property (nonatomic, strong) NSButton*      removeButton;
@property (nonatomic, strong) NSButton*      addButton;
@property (nonatomic, strong) NSButton*      closeButton;

- (instancetype)initWithPaths:(NSMutableArray<NSString*>*)paths;

- (void)buildViews;
- (void)layoutViews;
- (void)updateButtons;

- (BOOL)isPathDuplicated:(NSString*)path;

- (void)onAdd:(id)sender;
- (void)onRemove:(id)sender;
- (void)onClose:(id)sender;

/** 把窗口摆到所有者窗口正中, 并保证不超出屏幕工作区 */
+ (void)placeWindow:(NSWindow*)window relativeTo:(NSWindow*)owner;

@end


@implementation iNESRomLibraryPathsDialog

- (instancetype)initWithPaths:(NSMutableArray<NSString*>*)paths
{
	NSWindow*  window;

	window = [[NSWindow alloc] initWithContentRect:NSMakeRect(0, 0, PATHS_INIT_W, PATHS_INIT_H)
										 styleMask:(NSWindowStyleMaskTitled
												  | NSWindowStyleMaskClosable
												  | NSWindowStyleMaskResizable)
										   backing:NSBackingStoreBuffered
											 defer:NO];

	self = [super initWithWindow:window];
	if (self == nil)
		return nil;

	window.title              = L10N("dialog.romlib.paths_title");
	window.delegate           = self;
	window.contentMinSize     = NSMakeSize(PATHS_MIN_W, PATHS_MIN_H);
	window.contentView        = [[iNESRomLibPathsContentView alloc]
									initWithFrame:NSMakeRect(0, 0, PATHS_INIT_W, PATHS_INIT_H)];
	window.releasedWhenClosed = NO;

	_paths = (paths != nil) ? paths : [NSMutableArray array];
	_dirty = NO;

	[self buildViews];
	[self layoutViews];
	[self updateButtons];

	return self;
}

#pragma mark - 视图构建

- (void)buildViews
{
	NSView*      content = self.window.contentView;

	// ---- 路径列表(单列) ----
	self.listView = [[NSTableView alloc] initWithFrame:NSZeroRect];
	self.listView.dataSource     = self;
	self.listView.delegate       = self;
	self.listView.rowHeight      = 18;
	self.listView.allowsMultipleSelection = NO;
	self.listView.allowsEmptySelection    = YES;
	self.listView.allowsColumnReordering  = NO;
	self.listView.gridStyleMask  = NSTableViewSolidHorizontalGridLineMask;
	self.listView.headerView     = nil;

	// 与 win32 的 LVS_REPORT(无表头)一致: 只有一列, 单列占满可视区宽度
	NSTableColumn*  col = [[NSTableColumn alloc] initWithIdentifier:@"path"];

	col.title = L10N("dialog.romlib.col_path");
	col.width = PATHS_INIT_W - PATHS_MARGIN * 2;
	[self.listView addTableColumn:col];

	self.scrollView = [[NSScrollView alloc] initWithFrame:NSZeroRect];
	self.scrollView.documentView          = self.listView;
	self.scrollView.hasVerticalScroller   = YES;
	self.scrollView.hasHorizontalScroller = YES;
	self.scrollView.autohidesScrollers    = YES;
	self.scrollView.borderType            = NSBezelBorder;
	self.scrollView.drawsBackground       = YES;

	[content addSubview:self.scrollView];

	// ---- 底部按钮: 左侧 删除 / 添加, 右侧 关闭 ----
	self.removeButton = [[NSButton alloc] initWithFrame:NSZeroRect];
	self.removeButton.title      = L10N("dialog.romlib.remove");
	self.removeButton.bezelStyle = NSBezelStyleRounded;
	self.removeButton.target     = self;
	self.removeButton.action     = @selector(onRemove:);

	self.addButton = [[NSButton alloc] initWithFrame:NSZeroRect];
	self.addButton.title         = L10N("dialog.romlib.add");
	self.addButton.bezelStyle    = NSBezelStyleRounded;
	self.addButton.target        = self;
	self.addButton.action        = @selector(onAdd:);

	// 关闭: 与标题栏的关闭按钮等价, 同时兼作 Esc 快捷键
	self.closeButton = [[NSButton alloc] initWithFrame:NSZeroRect];
	self.closeButton.title         = L10N("dialog.romlib.close");
	self.closeButton.bezelStyle    = NSBezelStyleRounded;
	self.closeButton.target        = self;
	self.closeButton.action        = @selector(onClose:);
	self.closeButton.keyEquivalent = @"\e";

	[content addSubview:self.removeButton];
	[content addSubview:self.addButton];
	[content addSubview:self.closeButton];
}

#pragma mark - 布局

- (void)layoutViews
{
	NSRect             bounds = self.window.contentView.bounds;
	CGFloat            bottom_y;
	NSArray<NSButton*>* left_buttons;

	// ---- 底部按钮 ----
	bottom_y = NSHeight(bounds) - PATHS_MARGIN - PATHS_BTN_H;

	left_buttons = @[ self.removeButton, self.addButton ];

	self.removeButton.frame = NSMakeRect(PATHS_MARGIN, bottom_y, PATHS_BTN_W, PATHS_BTN_H);
	self.addButton.frame    = NSMakeRect(PATHS_MARGIN + PATHS_BTN_W + PATHS_BTN_GAP,
										 bottom_y, PATHS_BTN_W, PATHS_BTN_H);
	self.closeButton.frame  = NSMakeRect(NSWidth(bounds) - PATHS_MARGIN - PATHS_BTN_W,
										 bottom_y, PATHS_BTN_W, PATHS_BTN_H);

	// 按钮按译文长度自适应宽度(空间不足时向右加宽窗口);
	// INESFitButtons 是右对齐的: 左组两个一起调用后会靠右, 下面再改回左对齐
	INESFitButtons(self.window, left_buttons, PATHS_MARGIN, PATHS_BTN_GAP, PATHS_BTN_W);
	INESFitButtons(self.window, @[ self.closeButton ], PATHS_MARGIN, PATHS_BTN_GAP, PATHS_BTN_W);

	// INESFitButtons 可能加宽过窗口, 这里重新取一次内容区尺寸
	bounds = self.window.contentView.bounds;

	// 左组改回左对齐(保留自适应得到的宽度与从左往右的顺序)
	{
		CGFloat  x = PATHS_MARGIN;

		for (NSButton* button in left_buttons)
		{
			button.frame = NSMakeRect(x, bottom_y, NSWidth(button.frame), PATHS_BTN_H);
			x += NSWidth(button.frame) + PATHS_BTN_GAP;
		}
	}

	// 关闭按钮: 贴右边距(上面的自适应已给出宽度, 这里只回贴一次右边界)
	self.closeButton.frame = NSMakeRect(NSWidth(bounds) - PATHS_MARGIN - NSWidth(self.closeButton.frame),
										bottom_y, NSWidth(self.closeButton.frame), PATHS_BTN_H);

	// ---- 列表(拉伸) ----
	self.scrollView.frame = NSMakeRect(PATHS_MARGIN, PATHS_MARGIN,
									   NSWidth(bounds) - PATHS_MARGIN * 2,
									   (bottom_y - PATHS_ROW_GAP) - PATHS_MARGIN);

	// 单列铺满可视区宽度: 同时同步表格自身尺寸, 避免横向滚动条无故出现
	{
		NSSize  clip = self.scrollView.contentView.bounds.size;
		CGFloat w    = MAX(clip.width, 120.0);

		self.listView.tableColumns[0].width = w;
		self.listView.frame = NSMakeRect(0, 0, w, MAX(clip.height, 60.0));
	}
}

#pragma mark - 通用

- (void)updateButtons
{
	self.removeButton.enabled = (self.listView.selectedRow >= 0
							  && self.listView.selectedRow < (NSInteger)_paths.count);
}

+ (NSString*)normalizedPath:(NSString*)path
{
	NSString*  out;

	if (path == nil)
		return nil;

	out = [path stringByTrimmingCharactersInSet:[NSCharacterSet whitespaceAndNewlineCharacterSet]];

	if (out.length == 0)
		return nil;

	// 根目录 "/" 保留, 其余去掉末尾的 "/"
	while ((out.length > 1) && [out hasSuffix:@"/"])
		out = [out substringToIndex:(out.length - 1)];

	out = out.stringByStandardizingPath;

	if ((out != nil) && (out.length == 0))
		return nil;

	return out;
}

/**
 * 规范化后的路径是否已在列表中(重复检查)。
 */
- (BOOL)isPathDuplicated:(NSString*)path
{
	NSString*  target;

	if (path == nil)
		return NO;

	target = [[self class] normalizedPath:path];
	if (target == nil)
		return NO;

	for (NSString* item in _paths)
	{
		NSString*  existing = [[self class] normalizedPath:item];

		if ((existing != nil) && [existing isEqualToString:target])
			return YES;
	}

	return NO;
}

#pragma mark - 交互动作

/**
 * "添加": 弹出系统文件夹选择面板, 追加到列表末尾并选中。
 */
- (void)onAdd:(id)sender
{
	NSOpenPanel*  panel = [NSOpenPanel openPanel];
	NSString*     picked;

	panel.title                   = L10N("dialog.romlib.select_dir_title");
	panel.canChooseFiles          = NO;
	panel.canChooseDirectories    = YES;
	panel.allowsMultipleSelection = NO;
	panel.canCreateDirectories    = YES;

	if (panel == nil)
		return;

	if ([panel runModal] != NSModalResponseOK)
		return;

	if (panel.URL == nil)
		return;

	picked = [[self class] normalizedPath:panel.URL.path];
	if (picked == nil)
		return;

	// 重复路径直接放弃并提示, 列表里始终不出现两个相同的路径
	if ([self isPathDuplicated:picked])
	{
		NSAlert*  alert = [[NSAlert alloc] init];

		alert.messageText = L10N("dialog.romlib.dir_exists");
		alert.alertStyle  = NSAlertStyleWarning;
		[alert addButtonWithTitle:L10N("msg.ok")];
		[alert runModal];

		INES_LOG(LOG_DBG, MOD_SYS, ISTR("romlib paths: `%s` already exists, ignored.\n"),
			[picked fileSystemRepresentation]);
		return;
	}

	[_paths addObject:picked];
	_dirty = YES;

	[self.listView reloadData];
	[self.listView selectRowIndexes:[NSIndexSet indexSetWithIndex:(NSInteger)(_paths.count - 1)]
			   byExtendingSelection:NO];
	[self.listView scrollRowToVisible:(NSInteger)(_paths.count - 1)];
	[self.listView.window makeFirstResponder:self.listView];
	[self updateButtons];

	INES_LOG(LOG_INF, MOD_SYS, ISTR("romlib paths: add `%s`.\n"),
		[picked fileSystemRepresentation]);
}

/**
 * "删除": 删除当前选中的路径。
 */
- (void)onRemove:(id)sender
{
	NSInteger  row = self.listView.selectedRow;
	NSString*  removed;

	if ((row < 0) || (row >= (NSInteger)_paths.count))
		return;

	removed = _paths[row];

	[_paths removeObjectAtIndex:row];
	_dirty  = YES;

	[self.listView reloadData];

	// 选中被删除行的后继行: 便于连续删除
	if (_paths.count > 0)
	{
		NSInteger  next = (row < (NSInteger)_paths.count) ? row : (NSInteger)(_paths.count - 1);

		[self.listView selectRowIndexes:[NSIndexSet indexSetWithIndex:next] byExtendingSelection:NO];
	}

	[self updateButtons];

	INES_LOG(LOG_INF, MOD_SYS, ISTR("romlib paths: remove `%s`.\n"),
		[removed fileSystemRepresentation]);
}

/**
 * "关闭"按钮(同时是 Esc 快捷键): 关闭窗口即结束模态。
 *
 * 与标题栏的关闭按钮走同一条路径: windowWillClose 统一 stopModal, 并把
 * "本次是否有改动" 带回给调用方。
 */
- (void)onClose:(id)sender
{
	[self close];
}

#pragma mark - NSTableViewDataSource / NSTableViewDelegate

- (NSInteger)numberOfRowsInTableView:(NSTableView*)tableView
{
	return (NSInteger)_paths.count;
}

- (NSView*)tableView:(NSTableView*)tableView
   viewForTableColumn:(NSTableColumn*)tableColumn
				  row:(NSInteger)row
{
	NSTextField*  field;

	if ((row < 0) || (row >= (NSInteger)_paths.count))
		return nil;

	field = [tableView makeViewWithIdentifier:tableColumn.identifier owner:self];
	if (field == nil)
	{
		field = [[NSTextField alloc] initWithFrame:NSZeroRect];
		field.identifier      = tableColumn.identifier;
		field.bordered        = NO;
		field.drawsBackground = NO;
		field.editable        = NO;
		field.selectable      = YES;
		field.lineBreakMode   = NSLineBreakByTruncatingMiddle;
		field.font            = [NSFont systemFontOfSize:[NSFont smallSystemFontSize]];
	}

	field.stringValue = _paths[row];
	// 完整路径优先显示尾部目录名
	field.toolTip     = _paths[row];

	return field;
}

- (void)tableViewSelectionDidChange:(NSNotification*)notification
{
	[self updateButtons];
}

#pragma mark - NSWindowDelegate

- (void)windowDidResize:(NSNotification*)notification
{
	[self layoutViews];
}

- (void)windowWillClose:(NSNotification*)notification
{
	// 关闭即提交(没有"确定/取消"按钮)
	[NSApp stopModalWithCode:NSModalResponseOK];
}

#pragma mark - 模态入口

+ (BOOL)runModalWithPaths:(NSMutableArray<NSString*>*)paths
					owner:(NSWindow*)owner
{
	iNESRomLibraryPathsDialog*  dialog;

	if (paths == nil)
		return NO;

	dialog = [[iNESRomLibraryPathsDialog alloc] initWithPaths:paths];
	if (dialog == nil)
		return NO;

	[self placeWindow:dialog.window relativeTo:owner];

	[NSApp runModalForWindow:dialog.window];

	dialog.window.delegate = nil;
	[dialog.window orderOut:nil];
	[dialog.window close];

	return dialog->_dirty;
}

/**
 * 窗口居中到所有者窗口, 并保证不超出屏幕工作区。
 */
+ (void)placeWindow:(NSWindow*)window relativeTo:(NSWindow*)owner
{
	NSRect     frame;
	NSScreen*  screen;
	NSRect     visible;
	CGFloat    x = 0;
	CGFloat    y = 0;

	if (window == nil)
		return;

	frame = window.frame;

	if (owner != nil)
	{
		NSRect  owner_frame = owner.frame;

		x = owner_frame.origin.x + (NSWidth(owner_frame) - NSWidth(frame)) / 2;
		y = owner_frame.origin.y + (NSHeight(owner_frame) - NSHeight(frame)) / 2;
	}

	screen = (owner != nil) ? owner.screen : nil;
	if (screen == nil)
		screen = [NSScreen mainScreen];

	visible = screen.visibleFrame;

	if (x < NSMinX(visible))
		x = NSMinX(visible);
	if (y < NSMinY(visible))
		y = NSMinY(visible);

	if ((x + NSWidth(frame)) > NSMaxX(visible))
		x = NSMaxX(visible) - NSWidth(frame);
	if ((y + NSHeight(frame)) > NSMaxY(visible))
		y = NSMaxY(visible) - NSHeight(frame);

	[window setFrameOrigin:NSMakePoint(x, y)];
}

@end
