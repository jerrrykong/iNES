// =====================================================================
// iNES macOS 前端 —— ROM 库窗口(实现)
//
// 列表的 8 个属性列与 mac/iNESOpenRomDialog.m("载入 NES 文件"对话框)完全
// 一致: 同样的列序、同样的列标题 i18n key、同样的懒加载分批解析机制, 只是
// 数据来源从"单个文件夹"变成"ROM 库路径列表"。双端规格见 docs/rom-library-plan.md。
// =====================================================================

#import "iNESRomLibrary.h"
#import "iNESRomLibraryPaths.h"

#import "iNESi18n.h"
#import "iNESUiLayout.h"
#import "iNESConfig.h"

#import "../comm/log.h"
#import "../core/rom.h"
#import "../core/ppu.h"


// ---------------------------------------------------------------------
// 布局尺寸(点)
// ---------------------------------------------------------------------

#define ROMLIB_MARGIN        8
#define ROMLIB_ROW_H         24
#define ROMLIB_BTN_W         88
#define ROMLIB_BTN_H         24
#define ROMLIB_BTN_GAP       6
#define ROMLIB_ROW_GAP       8
#define ROMLIB_SEARCH_LABEL_W 56
#define ROMLIB_SEARCH_W      180
// 初始内容区大小
#define ROMLIB_INIT_W        760
#define ROMLIB_INIT_H        520
// 最小内容区大小(拉伸下限)
#define ROMLIB_MIN_W         560
#define ROMLIB_MIN_H         340

// 单次扫描的最大 ROM 数(所有路径合计), 防止超大目录拖慢界面
#define ROMLIB_MAX_FILES     8192

// 属性解析的分片参数: 逐文件打开读文件头是扫描中唯一耗时的部分,
// 放到定时器里按时间预算分批处理, 保证界面始终可响应
#define ROMLIB_SCAN_TICK_SEC      0.010   // 定时器间隔(秒)
#define ROMLIB_SCAN_BUDGET_SEC    0.020   // 单个时间片的最大耗时(秒)
#define ROMLIB_SCAN_MAX_PER_TICK  32      // 单个时间片最多解析的文件数

// 缓存文件的版本行: 格式不兼容时直接丢弃缓存重新扫描
#define ROMLIB_CACHE_VER          "ROMLIB1"
// 缓存字段分隔符(含该字符的路径无法安全存取, 扫描时会跳过)
#define ROMLIB_CACHE_SEP          @"\t"

#define ROMLIB_CACHE_FIELD_COUNT  8

// 配置延迟保存的合并窗口(秒): 滚动列表会连续触发, 攒一小段再落盘
#define ROMLIB_SAVE_DELAY         0.5


// ---------------------------------------------------------------------
// 列表列定义(标题 / 宽度 / 是否右对齐), 顺序与 openrom 一致
// ---------------------------------------------------------------------

typedef struct _romlib_column_
{
	const char*   title;
	NSInteger     width;
	BOOL          right_align;
} romlib_column_t;

/* 标题沿用 dialog.openrom 的 i18n key, 两端两张表的列序与译文天然一致 */
static const romlib_column_t s_columns[] =
{
	{ "dialog.openrom.col_name",      220, NO  },
	{ "dialog.openrom.col_rom_size",   80, YES },
	{ "dialog.openrom.col_mapper",     60, YES },
	{ "dialog.openrom.col_prg",        70, YES },
	{ "dialog.openrom.col_chr",        70, YES },
	{ "dialog.openrom.col_mirror",     60, NO  },
	{ "dialog.openrom.col_battery",    50, NO  },
	{ "dialog.openrom.col_trainer",    60, NO  }
};


// ---------------------------------------------------------------------
// 文件头解析与显示格式化(与 iNESOpenRomDialog.m 的同类工具函数一致)
// ---------------------------------------------------------------------

typedef struct _romlib_rominfo_
{
	ines_byte_t   mapper_num;    // Mapper 编号
	ines_byte_t   mirror_type;   // 镜像方式 MIRROR_*
	ines_byte_t   has_sram;      // 是否带电池记忆
	ines_byte_t   has_trainer;   // 是否带 512 字节 trainer
	ines_word_t   prg_kb;        // PRG 大小(KB)
	ines_word_t   chr_kb;        // CHR 大小(KB)
} romlib_rominfo_t;


/**
 * 解析 iNES 文件头(只读 16 字节, 不加载 ROM 数据)。
 *
 * 判定与解析规则与 core/rom.c 的 ines_rom_load_from_file() 保持一致。
 *
 * @param path 文件完整路径
 * @param info [out] 解析结果
 * @return YES 表示是受支持的 iNES 文件
 */
static BOOL romlib_parse_header(NSString* path, romlib_rominfo_t* info)
{
	FILE*                fp;
	ines_file_header_t   header;
	ines_size_t          n_read;

	if ((path == nil) || (info == NULL))
		return NO;

	memset(info, 0, sizeof(*info));

	fp = fopen([path fileSystemRepresentation], "rb");
	if (fp == NULL)
	{
		INES_LOG(LOG_DBG, MOD_SYS, ISTR("romlib: open `%s` failed: (%d)%s\n"),
			[path fileSystemRepresentation], errno, strerror(errno));
		return NO;
	}

	n_read = fread(&header, 1, INES_FILE_HEADER_SIZE, fp);
	fclose(fp);

	if (n_read != INES_FILE_HEADER_SIZE)
		return NO;

	// 文件标识 "NES\x1a"
	if (0 != memcmp(header.tag, INES_FILE_TAG, sizeof(header.tag)))
		return NO;

	// PRG 至少 1 块, 否则视为非法文件
	if (header.PROM_block_num == 0)
		return NO;

	info->mapper_num  = (ines_byte_t)((header.flag1 >> 4) | (header.flag2 & 0xF0));
	info->mirror_type = (header.flag1 & 0x08) ? MIRROR_FOUR_SCREEN
					  : ((header.flag1 & 0x01) ? MIRROR_VERT : MIRROR_HORZ);
	info->has_sram    = (header.flag1 & 0x02) ? 1 : 0;
	info->has_trainer = (header.flag1 & 0x04) ? 1 : 0;
	info->prg_kb      = (ines_word_t)(header.PROM_block_num * (INES_PROM_BLOCK_SIZE / 1024));
	info->chr_kb      = (ines_word_t)(header.VROM_block_num * (INES_VROM_BLOCK_SIZE / 1024));

	return YES;
}


/** 格式化文件大小(自动选择单位), 与 openrom_format_size 一致。 */
static NSString* romlib_format_size(ines_int64_t size)
{
	if (size < 0)
		size = 0;

	if (size < 1024)
		return [NSString stringWithFormat:@"%lld B", (long long)size];

	if (size < 1024 * 1024)
		return [NSString stringWithFormat:@"%lld KB", (long long)(size / 1024)];

	return [NSString stringWithFormat:@"%lld.%02lld MB",
			(long long)(size / (1024 * 1024)),
			(long long)((size % (1024 * 1024)) * 100 / (1024 * 1024))];
}


/** 镜像方式文本, 与 openrom_mirror_text 一致。 */
static NSString* romlib_mirror_text(ines_byte_t mirror_type)
{
	switch (mirror_type)
	{
	case MIRROR_VERT:
		return L10N("dialog.openrom.mirror_vertical");

	case MIRROR_HORZ:
		return L10N("dialog.openrom.mirror_horizontal");

	case MIRROR_FOUR_SCREEN:
		return L10N("dialog.openrom.mirror_four");

	default:
		break;
	}

	return L10N("dialog.openrom.mirror_unknown");
}


/** 创建一个静态文本标签(等价 win32 的 LTEXT)。 */
static NSTextField* romlib_make_label(NSString* text)
{
	NSTextField*  label = [[NSTextField alloc] initWithFrame:NSZeroRect];

	label.stringValue     = text;
	label.bezeled         = NO;
	label.drawsBackground = NO;
	label.editable        = NO;
	label.selectable      = NO;
	label.font            = [NSFont systemFontOfSize:12];

	return label;
}


/** 比较两个整数(供排序使用)。 */
static NSComparisonResult romlib_compare_number(long long v1, long long v2)
{
	if (v1 == v2)
		return NSOrderedSame;

	return (v1 < v2) ? NSOrderedAscending : NSOrderedDescending;
}


// ---------------------------------------------------------------------
// 内容视图: 翻转坐标系(原点在左上角), 便于照搬 win32 的布局算式
// ---------------------------------------------------------------------

@interface iNESRomLibContentView : NSView
@end

@implementation iNESRomLibContentView

- (BOOL)isFlipped
{
	return YES;
}

@end


// ---------------------------------------------------------------------
// 列表项: ROM 完整路径 + 文件头属性
// ---------------------------------------------------------------------

@interface iNESRomLibraryEntry : NSObject

@property (nonatomic, copy)   NSString*    path;         // 完整路径
@property (nonatomic, copy)   NSString*    name;         // 文件名(不含目录)
@property (nonatomic, assign) ines_int64_t fileSize;     // 文件总字节数
@property (nonatomic, assign) BOOL         parsed;       // 文件头是否已解析(分批解析用)
@property (nonatomic, assign) ines_byte_t  mapperNum;    // Mapper 编号
@property (nonatomic, assign) ines_byte_t  mirrorType;   // 镜像方式 MIRROR_*
@property (nonatomic, assign) BOOL         hasSram;      // 是否带电池记忆
@property (nonatomic, assign) BOOL         hasTrainer;   // 是否带 512 字节 trainer
@property (nonatomic, assign) ines_word_t  prgKb;        // PRG 大小(KB)
@property (nonatomic, assign) ines_word_t  chrKb;        // CHR 大小(KB)

@end

@implementation iNESRomLibraryEntry
@end


// ---------------------------------------------------------------------
// ROM 库窗口
// ---------------------------------------------------------------------

@interface iNESRomLibrary () <NSTableViewDataSource, NSTableViewDelegate,
							  NSSearchFieldDelegate, NSWindowDelegate>
{
	NSMutableArray<NSString*>*             _paths;         // 配置的 ROM 库路径
	NSMutableArray<iNESRomLibraryEntry*>*  _entries;       // ROM 全量列表(已排序)
	NSMutableArray<iNESRomLibraryEntry*>*  _viewEntries;   // 当前显示的 ROM(搜索过滤后)
	NSString*                              _filter;        // 搜索关键字(空串表示不过滤)
	NSString*                              _selectedPath;  // 当前/上次选中的 ROM 完整路径
	NSInteger                              _savedScrollRow;// 恢复用的列表首行序号
	NSInteger                              _sortColumn;    // 当前排序列, -1 表示未排序
	BOOL                                   _sortAsc;       // 排序方向
	BOOL                                   _applyingSort;  // 正在程序内设置 sortDescriptors, 避免递归
	NSTimer*                               _scanTimer;     // 分批解析文件头的定时器
	NSInteger                              _scanNext;      // 下一个待解析的行号
	BOOL                                   _scanning;      // 是否处于解析阶段
	NSInteger                              _savedPathCount;// config.ini 里上一次写入的路径条数(清理残留槽位用)
	BOOL                                   _firstShow;     // 是否尚未首次显示
	BOOL                                   _frameRestored; // 窗口位置是否已从配置恢复
	BOOL                                   _saveScheduled; // 是否已排下一次延迟保存
}

@property (nonatomic, strong) NSButton*      settingsButton;
@property (nonatomic, strong) NSButton*      refreshButton;
@property (nonatomic, strong) NSTextField*   statusLabel;
@property (nonatomic, strong) NSTextField*   searchLabel;
@property (nonatomic, strong) NSSearchField* searchField;
@property (nonatomic, strong) NSScrollView*  scrollView;
@property (nonatomic, strong) NSTableView*   listView;
@property (nonatomic, strong) NSTextField*   pathField;
@property (nonatomic, strong) NSButton*      loadButton;

- (void)buildViews;
- (void)layoutViews;
- (void)updateButtons;
- (void)updateStatus;

// ---- 数据: 配置 / 缓存 ----
+ (NSString*)cacheFilePath;
- (void)loadPathsFromConfig;
- (void)savePathsToConfig;
- (void)loadViewState;
- (void)saveStateSoon;
- (void)saveStateNow;

// ---- 扫描 ----
- (void)refreshList;
- (void)stopScan;
- (void)onScanTimer:(NSTimer*)timer;
- (void)finishScan;
- (BOOL)parseEntryAtIndex:(NSInteger)index;
- (BOOL)loadCache;
- (void)saveCache;

// ---- 列表 ----
- (void)applySort;
- (void)updateSortIndicator;
- (NSString*)keyForColumnIndex:(NSInteger)column;
- (NSInteger)columnIndexForKey:(NSString*)key;
- (NSComparisonResult)compareEntry:(iNESRomLibraryEntry*)entry1
							  with:(iNESRomLibraryEntry*)entry2
							column:(NSInteger)column
						 ascending:(BOOL)ascending;
- (void)rebuildViewEntries;
- (BOOL)matchesFilter:(iNESRomLibraryEntry*)entry;
- (NSString*)textForEntry:(iNESRomLibraryEntry*)entry column:(NSInteger)column;
- (NSInteger)firstVisibleRow;
- (void)scrollToRow:(NSInteger)row;
- (void)restoreSelectionAndScroll;

// ---- 交互 ----
- (void)showWarning:(NSString*)message;
- (void)loadSelected;
- (void)contentViewDidScroll:(NSNotification*)notification;
- (void)centerRelativeTo:(NSWindow*)owner;

@end


@implementation iNESRomLibrary

- (instancetype)init
{
	NSWindow*  window;

	window = [[NSWindow alloc] initWithContentRect:NSMakeRect(0, 0, ROMLIB_INIT_W, ROMLIB_INIT_H)
										 styleMask:(NSWindowStyleMaskTitled
												  | NSWindowStyleMaskClosable
												  | NSWindowStyleMaskMiniaturizable
												  | NSWindowStyleMaskResizable)
										   backing:NSBackingStoreBuffered
											 defer:NO];

	self = [super initWithWindow:window];
	if (self == nil)
		return nil;

	window.title              = L10N("dialog.romlib.title");
	window.delegate           = self;
	window.contentMinSize     = NSMakeSize(ROMLIB_MIN_W, ROMLIB_MIN_H);
	window.contentView        = [[iNESRomLibContentView alloc]
									initWithFrame:NSMakeRect(0, 0, ROMLIB_INIT_W, ROMLIB_INIT_H)];
	window.releasedWhenClosed = NO;

	_paths          = [[NSMutableArray alloc] init];
	_entries        = [[NSMutableArray alloc] init];
	_viewEntries    = [[NSMutableArray alloc] init];
	_filter         = @"";
	_selectedPath   = @"";
	_sortColumn     = -1;
	_sortAsc        = YES;
	_firstShow      = YES;

	[self buildViews];
	[self layoutViews];

	[self loadPathsFromConfig];
	[self loadViewState];
	[self updateSortIndicator];

	// 列表滚动会连续触发保存请求, 这里一次性注册
	[[NSNotificationCenter defaultCenter] addObserver:self
											 selector:@selector(contentViewDidScroll:)
												 name:NSViewBoundsDidChangeNotification
											   object:self.scrollView.contentView];

	return self;
}

- (void)dealloc
{
	[[NSNotificationCenter defaultCenter] removeObserver:self];

	[NSObject cancelPreviousPerformRequestsWithTarget:self];

	if (_scanTimer != nil)
	{
		[_scanTimer invalidate];
		_scanTimer = nil;
	}
}

#pragma mark - 视图构建

- (void)buildViews
{
	NSView*    content = self.window.contentView;
	NSInteger  i;

	// ---- 顶部操作区: [设置] [刷新] [计数] ...... [搜索:] [输入框] ----
	self.settingsButton = [[NSButton alloc] initWithFrame:NSZeroRect];
	self.settingsButton.title      = L10N("dialog.romlib.settings");
	self.settingsButton.bezelStyle = NSBezelStyleRounded;
	self.settingsButton.target     = self;
	self.settingsButton.action     = @selector(onSettings:);

	self.refreshButton = [[NSButton alloc] initWithFrame:NSZeroRect];
	self.refreshButton.title      = L10N("dialog.romlib.refresh");
	self.refreshButton.bezelStyle = NSBezelStyleRounded;
	self.refreshButton.target     = self;
	self.refreshButton.action     = @selector(onRefresh:);

	self.statusLabel = romlib_make_label(@"");
	self.statusLabel.font      = [NSFont systemFontOfSize:11];
	self.statusLabel.textColor = [NSColor secondaryLabelColor];
	self.statusLabel.alignment = NSTextAlignmentRight;

	self.searchLabel = romlib_make_label(L10N("dialog.romlib.search"));

	self.searchField = [[NSSearchField alloc] initWithFrame:NSZeroRect];
	self.searchField.font    = [NSFont systemFontOfSize:12];
	self.searchField.delegate = self;

	[content addSubview:self.settingsButton];
	[content addSubview:self.refreshButton];
	[content addSubview:self.statusLabel];
	[content addSubview:self.searchLabel];
	[content addSubview:self.searchField];

	// ---- 中部: ROM 列表 ----
	self.listView = [[NSTableView alloc] initWithFrame:NSZeroRect];
	self.listView.dataSource     = self;
	self.listView.delegate       = self;
	self.listView.rowHeight      = 18;
	// 单选; 失焦后仍保持可见的高亮(与 openrom 一致)
	self.listView.allowsMultipleSelection = NO;
	self.listView.allowsEmptySelection    = YES;
	self.listView.allowsColumnReordering  = NO;
	self.listView.columnAutoresizingStyle = NSTableViewNoColumnAutoresizing;
	self.listView.gridStyleMask  = NSTableViewSolidHorizontalGridLineMask;
	self.listView.target         = self;
	self.listView.doubleAction   = @selector(onListDoubleClick:);

	for (i = 0; i < (NSInteger)count_of(s_columns); i++)
	{
		NSTableColumn*  col = [[NSTableColumn alloc] initWithIdentifier:
								[NSString stringWithFormat:@"col%ld", (long)i]];
		NSString*       title = L10N(s_columns[i].title);

		col.title  = title;
		// 列宽按译文表头测量, 但不小于设计宽度(列表可横向滚动, 故不必加宽窗口)
		col.width  = MAX((CGFloat)s_columns[i].width,
						 INESTextWidth(title, [NSFont systemFontOfSize:11.0]) + 14.0);
		col.minWidth = 40;
		// 点击表头排序: NSTableView 依据 sortDescriptorPrototype 自动在升/降序之间
		// 切换并绘制排序箭头(等价 win32 想让 HDF_SORTUP / HDF_SORTDOWN 达到的效果)
		col.sortDescriptorPrototype = [NSSortDescriptor sortDescriptorWithKey:col.identifier
																   ascending:YES];

		[self.listView addTableColumn:col];
	}

	self.scrollView = [[NSScrollView alloc] initWithFrame:NSZeroRect];
	self.scrollView.documentView          = self.listView;
	self.scrollView.hasVerticalScroller   = YES;
	self.scrollView.hasHorizontalScroller = YES;
	self.scrollView.autohidesScrollers    = YES;
	self.scrollView.borderType            = NSBezelBorder;
	self.scrollView.drawsBackground       = YES;

	[content addSubview:self.scrollView];

	// ---- 底部: 完整路径 + 载入 ----
	self.pathField = [[NSTextField alloc] initWithFrame:NSZeroRect];
	self.pathField.stringValue   = @"";
	self.pathField.font          = [NSFont systemFontOfSize:12];
	self.pathField.editable      = NO;
	self.pathField.bezeled       = NO;
	self.pathField.drawsBackground = NO;
	// 路径通常很长: 省略头部保留文件名, 并允许鼠标选中复制
	self.pathField.lineBreakMode = NSLineBreakByTruncatingHead;
	self.pathField.selectable    = YES;

	self.loadButton = [[NSButton alloc] initWithFrame:NSZeroRect];
	self.loadButton.title         = L10N("dialog.romlib.load");
	self.loadButton.bezelStyle    = NSBezelStyleRounded;
	self.loadButton.target        = self;
	self.loadButton.action        = @selector(onLoad:);
	self.loadButton.keyEquivalent = @"\r";
	self.loadButton.enabled       = NO;

	[content addSubview:self.pathField];
	[content addSubview:self.loadButton];
}

#pragma mark - 布局

- (void)layoutViews
{
	NSRect    bounds = self.window.contentView.bounds;
	CGFloat   cx = NSWidth(bounds);
	CGFloat   cy = NSHeight(bounds);
	CGFloat   bottom_y;
	CGFloat   top_y;
	CGFloat   x;

	// ---- 底部: 路径 + 载入 ----
	bottom_y = cy - ROMLIB_MARGIN - ROMLIB_BTN_H;

	self.loadButton.frame = NSMakeRect(cx - ROMLIB_MARGIN - ROMLIB_BTN_W, bottom_y,
									   ROMLIB_BTN_W, ROMLIB_BTN_H);

	INESFitButtons(self.window, @[ self.loadButton ],
				   ROMLIB_MARGIN, ROMLIB_BTN_GAP, ROMLIB_BTN_W);

	// INESFitButtons 可能加宽过窗口, 这里重新取一次内容区尺寸
	bounds = self.window.contentView.bounds;
	cx     = NSWidth(bounds);

	{
		CGFloat  left  = ROMLIB_MARGIN;
		CGFloat  width = (cx - ROMLIB_MARGIN - ROMLIB_BTN_W - ROMLIB_BTN_GAP) - left;

		if (width < 120)
			width = 120;

		self.pathField.frame = NSMakeRect(left, bottom_y + 4, width, ROMLIB_BTN_H - 8);
	}

	// ---- 顶部操作区 ----
	top_y = ROMLIB_MARGIN;

	self.settingsButton.frame = NSMakeRect(ROMLIB_MARGIN, top_y, ROMLIB_BTN_W, ROMLIB_BTN_H);
	self.refreshButton.frame  = NSMakeRect(ROMLIB_MARGIN + ROMLIB_BTN_W + ROMLIB_BTN_GAP, top_y,
										   ROMLIB_BTN_W, ROMLIB_BTN_H);

	INESFitButtons(self.window, @[ self.settingsButton, self.refreshButton ],
				   ROMLIB_MARGIN, ROMLIB_BTN_GAP, ROMLIB_BTN_W);

	{
		CGFloat  left = ROMLIB_MARGIN;

		for (NSButton* button in @[ self.settingsButton, self.refreshButton ])
		{
			button.frame = NSMakeRect(left, top_y, NSWidth(button.frame), ROMLIB_BTN_H);
			left += NSWidth(button.frame) + ROMLIB_BTN_GAP;
		}
	}

	bounds = self.window.contentView.bounds;
	cx     = NSWidth(bounds);
	cy     = NSHeight(bounds);

	// ---- 搜索区: 靠右摆放( search field 宽度随窗口富余程度自适应) ----
	{
		CGFloat  right = cx - ROMLIB_MARGIN;
		CGFloat  field_w = ROMLIB_SEARCH_W;
		CGFloat  left = NSMaxX(self.refreshButton.frame) + ROMLIB_BTN_GAP;

		if ((right - field_w) < left)
			field_w = right - left;

		if (field_w < 80)
			field_w = 80;

		self.searchField.frame = NSMakeRect(right - field_w, top_y + 2, field_w, ROMLIB_ROW_H - 4);

		x = NSMinX(self.searchField.frame) - ROMLIB_SEARCH_LABEL_W - ROMLIB_BTN_GAP;
		self.searchLabel.frame = NSMakeRect(x, top_y + 4, ROMLIB_SEARCH_LABEL_W, 16);

		// 计数文字占满操作区剩余空间, 不与左右任何控件重叠
		self.statusLabel.frame = NSMakeRect(left, top_y + 5,
											MAX(self.searchLabel.frame.origin.x - left - ROMLIB_BTN_GAP, 60),
											16);
	}

	// ---- 中部列表(拉伸) ----
	x = top_y + ROMLIB_ROW_H + ROMLIB_ROW_GAP;

	self.scrollView.frame = NSMakeRect(ROMLIB_MARGIN, x,
									   cx - ROMLIB_MARGIN * 2,
									   (bottom_y - ROMLIB_ROW_GAP) - x);
}

#pragma mark - 通用

- (void)updateButtons
{
	NSInteger  row = self.listView.selectedRow;
	BOOL       valid = ((row >= 0) && (row < (NSInteger)_viewEntries.count));

	self.loadButton.enabled = valid;

	if (valid)
		self.pathField.stringValue = _viewEntries[row].path;
	else
		self.pathField.stringValue = @"";
}

- (void)updateStatus
{
	if (_scanning)
	{
		self.statusLabel.stringValue = L10NF("dialog.romlib.scanning_format",
											 (long)_scanNext, (long)_entries.count);
		return;
	}

	if (_paths.count == 0)
	{
		self.statusLabel.stringValue = L10N("dialog.romlib.no_path");
		return;
	}

	if (_entries.count == 0)
	{
		self.statusLabel.stringValue = L10N("dialog.romlib.no_supported");
		return;
	}

	if (_viewEntries.count == 0)
	{
		self.statusLabel.stringValue = L10N("dialog.romlib.no_match");
		return;
	}

	self.statusLabel.stringValue = L10NF("dialog.romlib.total_format",
										 (unsigned long)_viewEntries.count);
}

- (void)showWarning:(NSString*)message
{
	NSAlert*  alert = [[NSAlert alloc] init];

	alert.messageText     = message;
	alert.alertStyle      = NSAlertStyleWarning;
	[alert addButtonWithTitle:L10N("msg.ok")];

	[alert runModal];
}

#pragma mark - 配置与持久化

+ (NSString*)cacheFilePath
{
	ines_char_t  data_dir[INES_MAX_PATH] = {0};
	NSString*    dir;

	ines_get_data_dir(data_dir, count_of(data_dir));

	dir = [NSString stringWithUTF8String:data_dir];
	if ((dir == nil) || (dir.length == 0))
		return nil;

	return [dir stringByAppendingPathComponent:@"romlib.dat"];
}

/**
 * 从 config.ini 的 [romlib] 段载入路径列表(count + path0..pathN-1)。
 */
- (void)loadPathsFromConfig
{
	ines_int_t  count = GetConfigInt(ISTR("romlib"), ISTR("count"), 0);
	ines_int_t  i;

	[_paths removeAllObjects];

	if (count < 0)
		count = 0;
	if (count > 256)
		count = 256;

	for (i = 0; i < count; i++)
	{
		ines_char_t   key[32];
		ines_cstr_t   value;

		ines_snprintf(key, sizeof(key), ISTR("path%d"), (int)i);

		value = GetConfigStr(ISTR("romlib"), key, ISTR(""));
		if ((value == NULL) || (value[0] == 0))
			continue;

		NSString*  path = [[iNESRomLibraryPathsDialog normalizedPath:
								[NSString stringWithUTF8String:value]] copy];

		if ((path == nil) || (path.length == 0))
			continue;

		[_paths addObject:path];
	}

	_savedPathCount = (NSInteger)count;

	INES_LOG(LOG_INF, MOD_SYS, ISTR("romlib: %lu path(s) loaded from config.\n"),
		(unsigned long)_paths.count);
}

/**
 * 把路径列表写回 config.ini 的 [romlib] 段; 条数变少时清理残留的旧槽位。
 */
- (void)savePathsToConfig
{
	NSInteger  i;

	SetConfigInt(ISTR("romlib"), ISTR("count"), (ines_int_t)_paths.count);

	for (i = 0; i < (NSInteger)_paths.count; i++)
	{
		ines_char_t  key[32];

		ines_snprintf(key, sizeof(key), ISTR("path%d"), (int)i);
		SetConfigStr(ISTR("romlib"), key, [_paths[i] fileSystemRepresentation]);
	}

	// 条数变少: 把不再使用的槽位清空(该 Key 无删除接口, 只能写空串)
	for (i = (NSInteger)_paths.count; i < _savedPathCount; i++)
	{
		ines_char_t  key[32];

		ines_snprintf(key, sizeof(key), ISTR("path%d"), (int)i);
		SetConfigStr(ISTR("romlib"), key, ISTR(""));
	}

	_savedPathCount = (NSInteger)_paths.count;
}

/**
 * 载入上次保存的视图状态: 排序、选中项、列表首行、窗口位置。
 */
- (void)loadViewState
{
	ines_int_t   value;
	ines_cstr_t  text;

	value = GetConfigInt(ISTR("romlib"), ISTR("sort_column"), -1);
	if ((value >= 0) && (value < (ines_int_t)count_of(s_columns)))
		_sortColumn = (NSInteger)value;
	else
		_sortColumn = -1;

	_sortAsc = (GetConfigInt(ISTR("romlib"), ISTR("sort_asc"), 1) != 0);

	text = GetConfigStr(ISTR("romlib"), ISTR("selected"), ISTR(""));
	if ((text != NULL) && (text[0] != 0))
		_selectedPath = [NSString stringWithUTF8String:text];
	else
		_selectedPath = @"";

	_savedScrollRow = (NSInteger)GetConfigInt(ISTR("romlib"), ISTR("scroll"), 0);
	if (_savedScrollRow < 0)
		_savedScrollRow = 0;

	// ---- 窗口位置 ----
	{
		ines_int_t  x = GetConfigInt(ISTR("romlib"), ISTR("frame_x"), 0);
		ines_int_t  y = GetConfigInt(ISTR("romlib"), ISTR("frame_y"), 0);
		ines_int_t  w = GetConfigInt(ISTR("romlib"), ISTR("frame_w"), 0);
		ines_int_t  h = GetConfigInt(ISTR("romlib"), ISTR("frame_h"), 0);
		NSRect      frame;

		if ((w >= ROMLIB_MIN_W) && (h >= ROMLIB_MIN_H))
		{
			frame = NSMakeRect((CGFloat)x, (CGFloat)y, (CGFloat)w, (CGFloat)h);

			// 只接受与某块屏幕可见区域相交的位置, 避免把窗口丢到屏幕外
			for (NSScreen* screen in [NSScreen screens])
			{
				if (NSIntersectsRect(frame, screen.visibleFrame))
				{
					[self.window setFrame:frame display:NO];
					_frameRestored = YES;
					break;
				}
			}
		}
	}

	// 窗口大小可能因恢复而改变, 重排一次
	[self layoutViews];
}

/**
 * 延迟合并保存(排序/选中/滚动位置等频繁变动的状态)。
 */
- (void)saveStateSoon
{
	if (_saveScheduled)
		return;

	_saveScheduled = YES;

	[self performSelector:@selector(saveStateNow) withObject:nil afterDelay:ROMLIB_SAVE_DELAY];
}

- (void)saveStateNow
{
	NSRect  frame = self.window.frame;

	_saveScheduled = NO;

	SetConfigInt(ISTR("romlib"), ISTR("sort_column"), (ines_int_t)_sortColumn);
	SetConfigInt(ISTR("romlib"), ISTR("sort_asc"),    _sortAsc ? 1 : 0);

	SetConfigStr(ISTR("romlib"), ISTR("selected"),
				 (_selectedPath.length > 0) ? [_selectedPath fileSystemRepresentation] : ISTR(""));

	SetConfigInt(ISTR("romlib"), ISTR("scroll"), (ines_int_t)MAX(0, [self firstVisibleRow]));

	SetConfigInt(ISTR("romlib"), ISTR("frame_x"), (ines_int_t)frame.origin.x);
	SetConfigInt(ISTR("romlib"), ISTR("frame_y"), (ines_int_t)frame.origin.y);
	SetConfigInt(ISTR("romlib"), ISTR("frame_w"), (ines_int_t)frame.size.width);
	SetConfigInt(ISTR("romlib"), ISTR("frame_h"), (ines_int_t)frame.size.height);
}

#pragma mark - ROM 缓存

/**
 * 从 <数据目录>/romlib.dat 载入上次刷新的 ROM 列表。
 *
 * 只有版本号与字段数都对得上才采用; 任一条解析失败就放弃整个缓存(重新扫描)。
 */
- (BOOL)loadCache
{
	NSString*                          file = [[self class] cacheFilePath];
	NSFileManager*                     fm   = [NSFileManager defaultManager];
	NSString*                          text;
	NSArray<NSString*>*                lines;
	NSMutableArray<iNESRomLibraryEntry*>*  loaded;
	NSError*                           error = nil;
	NSUInteger                         i;

	if ((file == nil) || ![fm fileExistsAtPath:file])
		return NO;

	text = [NSString stringWithContentsOfFile:file encoding:NSUTF8StringEncoding error:&error];
	if (text == nil)
	{
		INES_LOG(LOG_INF, MOD_SYS, ISTR("romlib: read cache failed, will rescan.\n"));
		return NO;
	}

	lines = [text componentsSeparatedByString:@"\n"];
	if (lines.count < 1)
		return NO;

	if (![lines[0] isEqualToString:@ROMLIB_CACHE_VER])
	{
		INES_LOG(LOG_INF, MOD_SYS, ISTR("romlib: cache version mismatch, will rescan.\n"));
		return NO;
	}

	loaded = [NSMutableArray array];

	for (i = 1; i < lines.count; i++)
	{
		NSString*                  line = lines[i];
		NSArray<NSString*>*        fields;
		iNESRomLibraryEntry*       entry;

		if (line.length == 0)
			continue;

		fields = [line componentsSeparatedByString:ROMLIB_CACHE_SEP];
		if (fields.count != ROMLIB_CACHE_FIELD_COUNT)
			continue;

		entry = [[iNESRomLibraryEntry alloc] init];
		entry.path       = fields[0];
		entry.name       = entry.path.lastPathComponent;
		entry.fileSize   = [fields[1] longLongValue];
		entry.mapperNum  = (ines_byte_t)[fields[2] intValue];
		entry.mirrorType = (ines_byte_t)[fields[3] intValue];
		entry.hasSram    = ([fields[4] intValue] != 0);
		entry.hasTrainer = ([fields[5] intValue] != 0);
		entry.prgKb      = (ines_word_t)[fields[6] intValue];
		entry.chrKb      = (ines_word_t)[fields[7] intValue];
		entry.parsed     = YES;

		if ((entry.path == nil) || (entry.path.length == 0))
			continue;

		[loaded addObject:entry];
	}

	[_entries setArray:loaded];

	INES_LOG(LOG_INF, MOD_SYS, ISTR("romlib: %lu ROM(s) loaded from cache.\n"),
		(unsigned long)_entries.count);

	return (_entries.count > 0);
}

/**
 * 把当前列表写入 <数据目录>/romlib.dat。
 */
- (void)saveCache
{
	NSString*       file = [[self class] cacheFilePath];
	NSMutableString* out;
	NSError*        error = nil;

	if (file == nil)
		return;

	out = [NSMutableString stringWithString:@ROMLIB_CACHE_VER];

	for (iNESRomLibraryEntry* entry in _entries)
	{
		NSString*  path = entry.path;

		// 含分隔符或换行的路径无法安全存取, 直接跳过(缓存丢了下次刷新会重来)
		if ((path == nil)
		 || [path rangeOfString:ROMLIB_CACHE_SEP].location != NSNotFound
		 || [path rangeOfString:@"\n"].location != NSNotFound)
		{
			continue;
		}

		// 8 个字段与 loadCache 的解析顺序严格对应, 分隔符统一用 ROMLIB_CACHE_SEP
		NSArray<NSString*>*  fields =
			@[ path,
			   [NSString stringWithFormat:@"%lld",   (long long)entry.fileSize],
			   [NSString stringWithFormat:@"%u",     (unsigned)entry.mapperNum],
			   [NSString stringWithFormat:@"%u",     (unsigned)entry.mirrorType],
			   [NSString stringWithFormat:@"%d",     entry.hasSram    ? 1 : 0],
			   [NSString stringWithFormat:@"%d",     entry.hasTrainer ? 1 : 0],
			   [NSString stringWithFormat:@"%u",     (unsigned)entry.prgKb],
			   [NSString stringWithFormat:@"%u",     (unsigned)entry.chrKb] ];

		[out appendString:@"\n"];
		[out appendString:[fields componentsJoinedByString:ROMLIB_CACHE_SEP]];
	}

	if (![out writeToFile:file atomically:YES encoding:NSUTF8StringEncoding error:&error])
	{
		INES_LOG(LOG_ERR, MOD_SYS, ISTR("romlib: write cache `%s` failed.\n"),
			[file fileSystemRepresentation]);
		return;
	}

	INES_LOG(LOG_INF, MOD_SYS, ISTR("romlib: %lu ROM(s) written to cache.\n"),
		(unsigned long)_entries.count);
}

#pragma mark - 扫描(懒加载解析)

/**
 * 全量刷新: 重新枚举所有配置的路径, 再分批解析文件头; 结束后回写缓存。
 *
 * 只在"路径变化"或用户点击"刷新"时调用 —— 常规打开只读缓存, 避免每次都扫盘。
 */
- (void)refreshList
{
	NSFileManager*                      fm = [NSFileManager defaultManager];
	NSMutableArray<iNESRomLibraryEntry*>* found;
	NSMutableSet<NSString*>*            seen;
	NSString*                           dir;
	BOOL                                is_dir = NO;

	[self stopScan];

	[_entries removeAllObjects];
	[_viewEntries removeAllObjects];
	[self.listView reloadData];

	found = [NSMutableArray array];
	seen  = [NSMutableSet set];

	for (dir in _paths)
	{
		NSArray<NSString*>*  names;
		NSString*            name;

		if ((dir == nil) || (dir.length == 0))
			continue;

		if (![fm fileExistsAtPath:dir isDirectory:&is_dir] || !is_dir)
		{
			INES_LOG(LOG_WAR, MOD_SYS, ISTR("romlib: path `%s` not accessible, skipped.\n"),
				[dir fileSystemRepresentation]);
			continue;
		}

		names = [fm contentsOfDirectoryAtPath:dir error:NULL];
		if (names == nil)
			continue;

		for (name in names)
		{
			NSString*              full;
			NSString*              key;
			NSDictionary*          attr;
			iNESRomLibraryEntry*   entry;

			if ((NSInteger)found.count >= ROMLIB_MAX_FILES)
				break;

			// 只收 .nes(扩展名比较不区分大小写, 与 openrom/Windows 通配一致)
			if (NSOrderedSame != [name.pathExtension caseInsensitiveCompare:@"nes"])
				continue;

			full = [dir stringByAppendingPathComponent:name];

			// 多个路径可能重叠(父目录/子目录), 同一文件只收一次
			key = [full stringByStandardizingPath];
			if ([seen containsObject:key])
				continue;

			attr = [fm attributesOfItemAtPath:full error:NULL];
			if ((attr == nil) || ![attr[NSFileType] isEqual:NSFileTypeRegular])
				continue;

			[seen addObject:key];

			entry = [[iNESRomLibraryEntry alloc] init];
			entry.path     = full;
			entry.name     = name;
			entry.fileSize = [attr[NSFileSize] longLongValue];

			[found addObject:entry];
		}

		if ((NSInteger)found.count >= ROMLIB_MAX_FILES)
			break;
	}

	// 按完整路径升序作为稳定的基础顺序(用户排序在完成解析后再叠加)
	[found sortUsingComparator:^NSComparisonResult(iNESRomLibraryEntry* e1,
												   iNESRomLibraryEntry* e2)
	{
		return [e1.path caseInsensitiveCompare:e2.path];
	}];

	[_entries addObjectsFromArray:found];

	INES_LOG(LOG_INF, MOD_SYS, ISTR("romlib: %lu nes file(s) enumerated from %lu path(s).\n"),
		(unsigned long)_entries.count, (unsigned long)_paths.count);

	if (_entries.count == 0)
	{
		[self finishScan];
		return;
	}

	// 定时器分批读文件头
	_scanNext = 0;
	_scanning = YES;

	[self.listView reloadData];
	[self updateStatus];

	_scanTimer = [NSTimer timerWithTimeInterval:ROMLIB_SCAN_TICK_SEC
										target:self
									  selector:@selector(onScanTimer:)
									  userInfo:nil
									   repeats:YES];
	[[NSRunLoop currentRunLoop] addTimer:_scanTimer forMode:NSRunLoopCommonModes];

	[self updateButtons];
}

- (void)stopScan
{
	if (_scanTimer != nil)
	{
		[_scanTimer invalidate];
		_scanTimer = nil;
	}

	_scanNext = 0;
	_scanning = NO;
}

- (void)onScanTimer:(NSTimer*)timer
{
	NSInteger       index;
	NSInteger       batch;
	NSInteger       count;
	NSTimeInterval  start;

	if (!_scanning)
	{
		[self stopScan];
		return;
	}

	start = [NSDate timeIntervalSinceReferenceDate];
	index = _scanNext;
	batch = 0;

	while ((index < (NSInteger)_entries.count)
		&& (batch < ROMLIB_SCAN_MAX_PER_TICK)
		&& (([NSDate timeIntervalSinceReferenceDate] - start) < ROMLIB_SCAN_BUDGET_SEC))
	{
		// 被移除的行不占位置: 该行号上已经是下一行, 因此游标不前进
		if ([self parseEntryAtIndex:index])
			index++;

		batch++;
	}

	_scanNext = index;
	count     = (NSInteger)_entries.count;

	// 每个时间片结束后统一刷新一次表格(比逐行刷新省得多, 视觉上同样是"逐渐填满")
	[self rebuildViewEntries];

	if (index >= count)
	{
		[self finishScan];
		return;
	}

	[self updateStatus];
}

- (void)finishScan
{
	[self stopScan];

	// 属性已齐, 此时才应用排序(解析期间属性不完整, 排了也不准)
	[self applySort];
	[self rebuildViewEntries];

	// 回写缓存 + 恢复上次的选中项与列表位置
	[self saveCache];
	[self restoreSelectionAndScroll];

	[self updateStatus];

	INES_LOG(LOG_INF, MOD_SYS, ISTR("romlib: %lu ROM(s) listed.\n"),
		(unsigned long)_entries.count);
}

/**
 * 解析一个列表行: 读文件头并补齐属性列。
 *
 * 不是有效 iNES 文件时直接从列表中移除该行(与"只列出受支持文件"的语义一致)。
 *
 * @param index 列表行号
 * @return YES 表示该行保留(游标应前进); NO 表示该行已被移除
 */
- (BOOL)parseEntryAtIndex:(NSInteger)index
{
	iNESRomLibraryEntry*  entry;
	romlib_rominfo_t      info;

	if ((index < 0) || (index >= (NSInteger)_entries.count))
		return YES;

	entry = _entries[index];

	// 同一行被重复处理时直接跳过
	if (entry.parsed)
		return YES;

	if (!romlib_parse_header(entry.path, &info))
	{
		INES_LOG(LOG_DBG, MOD_SYS, ISTR("romlib: `%s` is not a supported nes file, removed.\n"),
			[entry.path fileSystemRepresentation]);

		[_entries removeObjectAtIndex:index];

		return NO;
	}

	entry.mapperNum  = info.mapper_num;
	entry.mirrorType = info.mirror_type;
	entry.hasSram    = (info.has_sram != 0);
	entry.hasTrainer = (info.has_trainer != 0);
	entry.prgKb      = info.prg_kb;
	entry.chrKb      = info.chr_kb;
	entry.parsed     = YES;

	return YES;
}

#pragma mark - 排序与过滤

- (void)applySort
{
	NSInteger  column    = _sortColumn;
	BOOL       ascending = _sortAsc;
	NSArray*   sorted;

	// 未排序时保持基础顺序(按完整路径升序)
	if (column < 0)
	{
		[self updateSortIndicator];
		return;
	}

	if (_scanning)
	{
		// 文件头属性还没解析完, 此时排序结果不可信, 留到解析结束再统一应用
		INES_LOG(LOG_DBG, MOD_SYS, ISTR("romlib: scanning, sort by column %ld deferred.\n"),
			(long)column);
		[self updateSortIndicator];
		return;
	}

	sorted = [_entries sortedArrayUsingComparator:
				^NSComparisonResult(iNESRomLibraryEntry* e1, iNESRomLibraryEntry* e2)
	{
		return [self compareEntry:e1 with:e2 column:column ascending:ascending];
	}];

	[_entries setArray:sorted];

	[self updateSortIndicator];
}

- (void)updateSortIndicator
{
	NSSortDescriptor*  descriptor = nil;

	if (_sortColumn >= 0)
	{
		descriptor = [NSSortDescriptor sortDescriptorWithKey:[self keyForColumnIndex:_sortColumn]
												  ascending:_sortAsc];
	}

	// 这里由程序内部设置, 屏蔽 sortDescriptorsDidChange: 以免递归
	_applyingSort = YES;
	self.listView.sortDescriptors = (descriptor != nil) ? @[descriptor] : @[];
	_applyingSort = NO;
}

- (NSString*)keyForColumnIndex:(NSInteger)column
{
	return [NSString stringWithFormat:@"col%ld", (long)column];
}

- (NSInteger)columnIndexForKey:(NSString*)key
{
	NSInteger  i;

	if (key == nil)
		return -1;

	for (i = 0; i < (NSInteger)count_of(s_columns); i++)
	{
		if ([key isEqualToString:[self keyForColumnIndex:i]])
			return i;
	}

	return -1;
}

- (NSComparisonResult)compareEntry:(iNESRomLibraryEntry*)entry1
							  with:(iNESRomLibraryEntry*)entry2
							column:(NSInteger)column
						 ascending:(BOOL)ascending
{
	NSComparisonResult  result = NSOrderedSame;

	// 防御: 都不为空时才比较
	if ((entry1 == nil) || (entry2 == nil))
		return NSOrderedSame;

	switch (column)
	{
	case 0:
		// 文件名: 不区分大小写的字符串比较(等价 win32 的 _tcsicmp)
		result = [entry1.name caseInsensitiveCompare:entry2.name];
		break;

	case 1:
		result = romlib_compare_number(entry1.fileSize, entry2.fileSize);
		break;

	case 2:
		result = romlib_compare_number(entry1.mapperNum, entry2.mapperNum);
		break;

	case 3:
		result = romlib_compare_number(entry1.prgKb, entry2.prgKb);
		break;

	case 4:
		result = romlib_compare_number(entry1.chrKb, entry2.chrKb);
		break;

	case 5:
		result = romlib_compare_number(entry1.mirrorType, entry2.mirrorType);
		break;

	case 6:
		result = romlib_compare_number(entry1.hasSram ? 1 : 0, entry2.hasSram ? 1 : 0);
		break;

	case 7:
		result = romlib_compare_number(entry1.hasTrainer ? 1 : 0, entry2.hasTrainer ? 1 : 0);
		break;

	default:
		break;
	}

	// 主键相同的项再按完整路径比较, 保证排序结果稳定可预期
	if ((result == NSOrderedSame) && (column != 0))
		result = [entry1.path caseInsensitiveCompare:entry2.path];

	if (result == NSOrderedSame)
		return NSOrderedSame;

	if (ascending)
		return result;

	return (result == NSOrderedAscending) ? NSOrderedDescending : NSOrderedAscending;
}

/**
 * 搜索匹配: 只看文件名(不含目录), 不区分大小写与变音符号的包含匹配。
 */
- (BOOL)matchesFilter:(iNESRomLibraryEntry*)entry
{
	if ((_filter == nil) || (_filter.length == 0) || (entry == nil))
		return YES;

	return ([entry.name rangeOfString:_filter
							 options:(NSCaseInsensitiveSearch | NSDiacriticInsensitiveSearch)].location
			!= NSNotFound);
}

/**
 * 按当前过滤条件重建显示列表, 并重新定位到 _selectedPath 所在的行。
 */
- (void)rebuildViewEntries
{
	NSMutableArray<iNESRomLibraryEntry*>*  view = [NSMutableArray array];
	BOOL                                   selected_found = NO;
	NSIndexSet*                            selection = nil;

	for (iNESRomLibraryEntry* entry in _entries)
	{
		if (![self matchesFilter:entry])
			continue;

		if ((_selectedPath.length > 0) && [entry.path isEqualToString:_selectedPath])
		{
			selection       = [NSIndexSet indexSetWithIndex:(NSInteger)view.count];
			selected_found  = YES;
		}

		[view addObject:entry];
	}

	[_viewEntries setArray:view];

	[self.listView reloadData];

	if (selection != nil)
		[self.listView selectRowIndexes:selection byExtendingSelection:NO];

	if (!selected_found)
	{
		_selectedPath = @"";
		self.pathField.stringValue = @"";
	}

	[self updateStatus];
	[self updateButtons];
}

/**
 * 恢复上次的选中项与列表显示位置(数据就绪后调用一次)。
 */
- (void)restoreSelectionAndScroll
{
	NSInteger  i;

	if (_selectedPath.length == 0)
	{
		[self scrollToRow:_savedScrollRow];
		return;
	}

	for (i = 0; i < (NSInteger)_viewEntries.count; i++)
	{
		if ([_viewEntries[i].path isEqualToString:_selectedPath])
		{
			[self.listView selectRowIndexes:[NSIndexSet indexSetWithIndex:i]
					   byExtendingSelection:NO];
			// _savedScrollRow 记录的就是列表首行序号, 直接滚到这里
			[self scrollToRow:_savedScrollRow];
			return;
		}
	}

	// 上次选中的 ROM 已不在库中(可能被删或改了名): 清空选中, 回到记录的位置
	_selectedPath = @"";
	[self.listView deselectAll:nil];
	[self scrollToRow:_savedScrollRow];
}

#pragma mark - 列表滚动位置

- (NSInteger)firstVisibleRow
{
	NSRange  range = [self.listView rowsInRect:self.scrollView.documentVisibleRect];

	return (range.length > 0) ? (NSInteger)range.location : 0;
}

- (void)scrollToRow:(NSInteger)row
{
	NSClipView*  clip  = self.scrollView.contentView;
	NSInteger    count = (NSInteger)_viewEntries.count;
	NSRect       rect;
	NSPoint      point;

	if (clip == nil)
		return;

	if (row <= 0)
	{
		[clip scrollToPoint:NSMakePoint(0, 0)];
		[self.scrollView reflectScrolledClipView:clip];
		return;
	}

	if (row >= count)
		row = MAX(0, count - 1);

	if (count <= 0)
		return;

	rect  = [self.listView rectOfRow:row];
	point = [clip constrainScrollPoint:NSMakePoint(0, NSMinY(rect))];

	[clip scrollToPoint:point];
	[self.scrollView reflectScrolledClipView:clip];
}

- (void)contentViewDidScroll:(NSNotification*)notification
{
	[self saveStateSoon];
}

#pragma mark - 列表文本

- (NSString*)textForEntry:(iNESRomLibraryEntry*)entry column:(NSInteger)column
{
	if (entry == nil)
		return @"";

	switch (column)
	{
	case 0:
		return entry.name;

	case 1:
		// 枚举阶段就已确定, 无需打开文件
		return romlib_format_size(entry.fileSize);

	case 2:
		return entry.parsed ? [NSString stringWithFormat:@"%u", (unsigned)entry.mapperNum] : @"";

	case 3:
		return entry.parsed ? [NSString stringWithFormat:@"%u KB", (unsigned)entry.prgKb] : @"";

	case 4:
		return entry.parsed ? [NSString stringWithFormat:@"%u KB", (unsigned)entry.chrKb] : @"";

	case 5:
		return entry.parsed ? romlib_mirror_text(entry.mirrorType) : @"";

	case 6:
		return entry.parsed ? (entry.hasSram ? L10N("dialog.openrom.yes") : L10N("dialog.openrom.no")) : @"";

	case 7:
		return entry.parsed ? (entry.hasTrainer ? L10N("dialog.openrom.yes") : L10N("dialog.openrom.no")) : @"";

	default:
		break;
	}

	return @"";
}

#pragma mark - 交互动作

- (IBAction)onSettings:(id)sender
{
	NSMutableArray<NSString*>*  paths = [_paths mutableCopy];
	BOOL                        changed;

	changed = [iNESRomLibraryPathsDialog runModalWithPaths:paths owner:self.window];

	if (!changed)
		return;

	// 路径列表变了: 落盘 + 立刻全量刷新(并把刷新结果写进缓存)
	[_paths setArray:paths];

	[self savePathsToConfig];
	[self refreshList];

	INES_LOG(LOG_INF, MOD_SYS, ISTR("romlib: path list changed, %lu path(s) now.\n"),
		(unsigned long)_paths.count);
}

- (IBAction)onRefresh:(id)sender
{
	[self refreshList];
}

- (IBAction)onLoad:(id)sender
{
	[self loadSelected];
}

- (IBAction)onListDoubleClick:(id)sender
{
	if (self.listView.clickedRow >= 0)
		[self loadSelected];
}

/**
 * 载入当前选中的 ROM: 交给 loadRomHandler, 窗口保持打开。
 */
- (void)loadSelected
{
	NSInteger               row = self.listView.selectedRow;
	iNESRomLibraryEntry*    entry;

	if ((row < 0) || (row >= (NSInteger)_viewEntries.count))
	{
		[self showWarning:L10N("dialog.romlib.select_rom_first")];
		return;
	}

	entry = _viewEntries[row];

	// 加载前再确认一次文件是否仍然存在
	if (![[NSFileManager defaultManager] fileExistsAtPath:entry.path])
	{
		[self showWarning:L10N("dialog.romlib.file_missing")];
		return;
	}

	if (self.loadRomHandler == nil)
	{
		INES_LOG(LOG_ERR, MOD_SYS, ISTR("romlib: no load handler installed.\n"));
		return;
	}

	if (!self.loadRomHandler(entry.path))
		return;

	[self saveStateNow];
}

#pragma mark - NSTextFieldDelegate(搜索框)

- (void)controlTextDidChange:(NSNotification*)notification
{
	id  object = notification.object;

	if (object != self.searchField)
		return;

	_filter = [self.searchField.stringValue copy];
	if (_filter == nil)
		_filter = @"";

	[self rebuildViewEntries];
	[self saveStateSoon];
}

#pragma mark - NSTableViewDataSource / NSTableViewDelegate

- (NSInteger)numberOfRowsInTableView:(NSTableView*)tableView
{
	return (NSInteger)_viewEntries.count;
}

- (NSView*)tableView:(NSTableView*)tableView
   viewForTableColumn:(NSTableColumn*)tableColumn
				  row:(NSInteger)row
{
	NSInteger     column;
	NSTextField*  field;

	column = [self columnIndexForKey:tableColumn.identifier];

	if ((column < 0) || (row < 0) || (row >= (NSInteger)_viewEntries.count))
		return nil;

	field = [tableView makeViewWithIdentifier:tableColumn.identifier owner:self];
	if (field == nil)
	{
		field = [[NSTextField alloc] initWithFrame:NSZeroRect];
		field.identifier      = tableColumn.identifier;
		field.bordered        = NO;
		field.drawsBackground = NO;
		field.editable        = NO;
		field.selectable      = NO;
		field.lineBreakMode   = NSLineBreakByTruncatingTail;
		field.font            = [NSFont systemFontOfSize:[NSFont smallSystemFontSize]];

		// 悬停显示完整路径, 便于区分同名 ROM
		field.allowsDefaultTighteningForTruncation = NO;
	}

	field.stringValue = [self textForEntry:_viewEntries[row] column:column];
	field.alignment   = s_columns[column].right_align ? NSTextAlignmentRight : NSTextAlignmentLeft;
	field.toolTip     = (column == 0) ? _viewEntries[row].path : nil;

	return field;
}

- (void)tableViewSelectionDidChange:(NSNotification*)notification
{
	NSInteger  row = self.listView.selectedRow;

	if ((row >= 0) && (row < (NSInteger)_viewEntries.count))
		_selectedPath = [_viewEntries[row].path copy];
	else
		_selectedPath = @"";

	[self updateButtons];
	[self saveStateSoon];
}

- (void)tableView:(NSTableView*)tableView
sortDescriptorsDidChange:(NSArray<NSSortDescriptor*>*)oldDescriptors
{
	NSSortDescriptor*  descriptor;
	NSInteger          column;

	// 程序内部同步箭头时不要反过来再排一次
	if (_applyingSort)
		return;

	descriptor = tableView.sortDescriptors.firstObject;
	column     = (descriptor != nil) ? [self columnIndexForKey:descriptor.key] : -1;

	if (column < 0)
		return;

	// NSTableView 已按"同列重复点击切换升/降序、换到其它列默认升序"的规则算好了
	// 新方向, 与 win32 的表头点击处理一致, 这里直接采用
	_sortColumn = column;
	_sortAsc    = descriptor.ascending;

	INES_LOG(LOG_DBG, MOD_SYS, ISTR("romlib: sort by column %ld (%s).\n"),
		(long)column, _sortAsc ? ISTR("asc") : ISTR("desc"));

	[self applySort];
	[self rebuildViewEntries];
	[self saveStateSoon];
}

#pragma mark - NSWindowDelegate

- (void)windowDidResize:(NSNotification*)notification
{
	[self layoutViews];
	[self saveStateSoon];
}

- (void)windowDidMove:(NSNotification*)notification
{
	[self saveStateSoon];
}

- (void)windowWillClose:(NSNotification*)notification
{
	// 关闭前落盘一次(延迟保存可能还没到点)
	[NSObject cancelPreviousPerformRequestsWithTarget:self
											selector:@selector(saveStateNow)
											  object:nil];
	[self saveStateNow];
}

#pragma mark - 对外入口

- (void)showLibraryRelativeTo:(NSWindow*)owner
{
	NSWindow*  window = self.window;

	if (window == nil)
		return;

	if (_firstShow)
	{
		_firstShow = NO;

		if (!_frameRestored)
			[self centerRelativeTo:owner];

		[self layoutViews];

		// 数据: 优先读缓存; 没有缓存(首次使用)才全量扫描一次
		if (![self loadCache])
		{
			[self refreshList];
		}
		else
		{
			[self applySort];
			[self rebuildViewEntries];
			[self restoreSelectionAndScroll];
			[self updateStatus];
		}
	}

	[window makeKeyAndOrderFront:self];
}

- (void)closeLibrary
{
	[self stopScan];

	[NSObject cancelPreviousPerformRequestsWithTarget:self];
	[self saveStateNow];

	self.window.delegate = nil;
	[self.window orderOut:nil];
	[self.window close];
}

/**
 * 把窗口摆到所有者窗口正中, 并保证不超出屏幕工作区。
 */
- (void)centerRelativeTo:(NSWindow*)owner
{
	NSWindow*  window = self.window;
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
