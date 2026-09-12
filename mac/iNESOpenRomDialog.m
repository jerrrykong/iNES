// =====================================================================
// iNES macOS 前端 —— "载入 NES 文件"管理器对话框
//
// 对应 win32/dlgOpenRom.c, 布局与行为逐条对齐:
//   上 — 文件夹标签 + 路径输入框 + 选择文件夹按钮(系统文件夹图标);
//   中 — 文件列表(可随窗口拉伸), 列出文件头中的关键属性;
//   下 — 文件计数 + "加载" / "取消"。
//
// 说明:
//   1) 列表项只读取 iNES 文件头(16 字节)解析属性, 不加载 ROM 数据;
//   2) 文件头解析规则与 core/rom.c 的 ines_rom_load_from_file() 保持一致;
//   3) 加载流程仍由调用方(iNESApp.m)完成, 本模块只负责选择文件;
//   4) 列表支持点击表头排序: 排序依据是解析出的文件头属性(数值), 不是列上的
//      显示文本, 因此 ROM 大小等列不会按字符串顺序排列; 同一列重复点击切换
//      升/降序, 切换文件夹后保持当前排序方式;
//   5) 目录内文件较多时界面不会长时间卡住: 先枚举目录条目(不打开文件)把文件名
//      与文件大小填进列表, 再由定时器分批读文件头补齐其余属性列, 非 iNES 文件
//      在解析到时从列表里移除。
//
// 与 win32 的差异(仅限无法照搬之处):
//   1) 枚举顺序: win32 沿用 FindFirstFile 的文件系统顺序, 这里改用文件名升序,
//      因为 macOS 的 contentsOfDirectoryAtPath: 返回顺序不稳定;
//   2) 表头箭头: win32 的 HDF_SORTUP / HDF_SORTDOWN 需要 comctl32 v6 才会绘制,
//      而本工程没有 manifest(实际不显示); 这里使用 NSTableView 的原生排序指示器,
//      由表头自动绘制升/降序箭头;
//   3) 初始目录兜底: win32 用当前工作目录, macOS 下应用包的工作目录恒为 "/"
//      没有意义, 这里退化为用户主目录。
// =====================================================================

#import "iNESOpenRomDialog.h"

#import "../comm/log.h"
#import "../core/rom.h"
#import "../core/ppu.h"


// ---------------------------------------------------------------------
// 布局尺寸(点), 与 win32/dlgOpenRom.c 的宏一一对应
// ---------------------------------------------------------------------

#define OPENROM_MARGIN        8
#define OPENROM_ROW_H         24
#define OPENROM_LABEL_W       60
#define OPENROM_BROWSE_W      28
#define OPENROM_BTN_W         78
#define OPENROM_BTN_H         24
#define OPENROM_BTN_GAP       6
// 初始内容区大小
#define OPENROM_INIT_W        720
#define OPENROM_INIT_H        520
// 最小内容区大小(拉伸下限)
#define OPENROM_MIN_W         520
#define OPENROM_MIN_H         340
// 单次扫描的最大文件数, 防止超大目录拖慢界面
#define OPENROM_MAX_FILES     4096

// 属性解析的分片参数: 逐文件打开读文件头是扫描中唯一耗时的部分,
// 放到定时器里按时间预算分批处理, 保证界面始终可响应
#define OPENROM_SCAN_TICK_SEC      0.010   // 定时器间隔(秒)
#define OPENROM_SCAN_BUDGET_SEC    0.020   // 单个时间片的最大耗时(秒)
#define OPENROM_SCAN_MAX_PER_TICK  32      // 单个时间片最多解析的文件数


// 列表列定义(标题 / 宽度 / 是否右对齐), 顺序与 win32 的 s_columns 一致
typedef struct _openrom_column_
{
	const char*   title;
	NSInteger     width;
	BOOL          right_align;
} openrom_column_t;

static const openrom_column_t s_columns[] =
{
	{ "文件名",    220, NO  },
	{ "ROM大小",    80, YES },
	{ "Mapper",     60, YES },
	{ "PRG",        70, YES },
	{ "CHR",        70, YES },
	{ "镜像",       60, NO  },
	{ "电池",       50, NO  },
	{ "Trainer",    60, NO  }
};

// NES 文件属性(全部取自 iNES 文件头)
typedef struct _openrom_rominfo_
{
	ines_int64_t  file_size;     // 文件总字节数
	ines_byte_t   mapper_num;    // Mapper 编号
	ines_byte_t   mirror_type;   // 镜像方式 MIRROR_*
	ines_byte_t   has_sram;      // 是否带电池记忆
	ines_byte_t   has_trainer;   // 是否带 512 字节 trainer
	ines_word_t   prg_kb;        // PRG 大小(KB)
	ines_word_t   chr_kb;        // CHR 大小(KB)
} openrom_rominfo_t;


// ---------------------------------------------------------------------
// 文件头解析与显示格式化(纯 C, 与 win32 的同名工具函数逐条对应)
// ---------------------------------------------------------------------

/**
 * 解析 iNES 文件头。
 *
 * 只读取文件头(16 字节), 不加载 ROM 数据; 判定与解析规则与
 * core/rom.c 的 ines_rom_load_from_file() 保持一致。
 *
 * @param path 文件完整路径
 * @param info [out] 解析结果
 * @return YES 表示是受支持的 iNES 文件
 */
static BOOL openrom_parse_header(NSString* path, openrom_rominfo_t* info)
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
		INES_LOG(LOG_DBG, MOD_SYS, ISTR("open file `%s` failed: (%d)%s\n"),
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


/**
 * 格式化文件大小, 自动选择合适的单位(与 win32 的 dlgOpenRom_FormatSize 一致)。
 */
static NSString* openrom_format_size(ines_int64_t size)
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


/**
 * 镜像方式文本(与 win32 的 dlgOpenRom_GetMirrorText 一致)。
 */
static NSString* openrom_mirror_text(ines_byte_t mirror_type)
{
	switch (mirror_type)
	{
	case MIRROR_VERT:
		return @"垂直";

	case MIRROR_HORZ:
		return @"水平";

	case MIRROR_FOUR_SCREEN:
		return @"四屏";

	default:
		break;
	}

	return @"未知";
}


/**
 * 去掉路径首尾空白与末尾的目录分隔符(根目录 "/" 除外)。
 */
static NSString* openrom_normalize_dir(NSString* dir)
{
	NSString*  out;

	if (dir == nil)
		return @"";

	out = [dir stringByTrimmingCharactersInSet:[NSCharacterSet whitespaceAndNewlineCharacterSet]];

	while ((out.length > 1) && [out hasSuffix:@"/"])
		out = [out substringToIndex:(out.length - 1)];

	return out;
}


/**
 * 创建一个静态文本标签(等价 win32 的 LTEXT)。
 */
static NSTextField* openrom_make_label(NSString* text)
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


/**
 * 对话框内容视图: 翻转坐标系(原点在左上角), 使 win32 的布局算式可以逐条照搬。
 */
@interface iNESOpenRomContentView : NSView
@end

@implementation iNESOpenRomContentView

- (BOOL)isFlipped
{
	return YES;
}

@end


// ---------------------------------------------------------------------
// 列表项: 文件头属性 + 排序依据
// ---------------------------------------------------------------------

@interface iNESOpenRomEntry : NSObject

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

@implementation iNESOpenRomEntry
@end


// ---------------------------------------------------------------------
// 对话框
// ---------------------------------------------------------------------

@interface iNESOpenRomDialog () <NSTableViewDataSource, NSTableViewDelegate, NSWindowDelegate>
{
	NSMutableArray<iNESOpenRomEntry*>*  _entries;       // 列表数据(下标即行号)
	NSString*                           _currentDir;    // 当前浏览的文件夹
	NSTimer*                            _scanTimer;     // 分批解析文件头的定时器
	NSInteger                           _scanNext;      // 下一个待解析的行号
	BOOL                                _scanning;      // 是否处于解析阶段
	NSInteger                           _sortColumn;    // 当前排序列, -1 表示未排序
	BOOL                                _sortAsc;       // 排序方向
	BOOL                                _applyingSort;  // 正在程序内设置 sortDescriptors, 用于避免递归
	NSString*                           _selectedPath;  // 用户最终选定的 ROM 文件完整路径
}

@property (nonatomic, strong) NSTextField*   dirLabel;
@property (nonatomic, strong) NSTextField*   dirField;
@property (nonatomic, strong) NSButton*      browseButton;
@property (nonatomic, strong) NSScrollView*  scrollView;
@property (nonatomic, strong) NSTableView*   listView;
@property (nonatomic, strong) NSTextField*   countLabel;
@property (nonatomic, strong) NSButton*      loadButton;
@property (nonatomic, strong) NSButton*      cancelButton;

- (instancetype)initWithInitialDir:(NSString*)dir;

- (void)buildViews;
- (void)layoutViews;
- (void)updateButtons;
- (void)showWarning:(NSString*)message;

// ---- 扫描 / 懒加载解析 ----
+ (NSString*)resolvedInitialDir:(NSString*)dir;
- (void)scanDir:(NSString*)dir;
- (void)stopScan;
- (void)onScanTimer:(NSTimer*)timer;
- (void)finishScan;
- (BOOL)parseEntryAtIndex:(NSInteger)index;

// ---- 排序 ----
- (void)applySort;
- (void)updateSortIndicator;
- (NSString*)keyForColumnIndex:(NSInteger)column;
- (NSInteger)columnIndexForKey:(NSString*)key;
- (NSComparisonResult)compareEntry:(iNESOpenRomEntry*)entry1
							  with:(iNESOpenRomEntry*)entry2
							column:(NSInteger)column
						 ascending:(BOOL)ascending;

// ---- 列表内容 ----
- (NSString*)textForEntry:(iNESOpenRomEntry*)entry column:(NSInteger)column;

// ---- 交互 ----
- (void)placeWindowRelativeTo:(NSWindow*)owner;
- (BOOL)isEditingDirField;
- (void)applyTypedDir;
- (void)loadSelected;

@end


@implementation iNESOpenRomDialog

- (instancetype)initWithInitialDir:(NSString*)dir
{
	NSWindow*  window;

	window = [[NSWindow alloc] initWithContentRect:NSMakeRect(0, 0, OPENROM_INIT_W, OPENROM_INIT_H)
										 styleMask:(NSWindowStyleMaskTitled
												  | NSWindowStyleMaskClosable
												  | NSWindowStyleMaskMiniaturizable
												  | NSWindowStyleMaskResizable)
										   backing:NSBackingStoreBuffered
											 defer:NO];

	self = [super initWithWindow:window];
	if (self == nil)
		return nil;

	window.title              = @"载入 NES 文件";
	window.delegate           = self;
	window.contentMinSize     = NSMakeSize(OPENROM_MIN_W, OPENROM_MIN_H);
	window.contentView        = [[iNESOpenRomContentView alloc]
									initWithFrame:NSMakeRect(0, 0, OPENROM_INIT_W, OPENROM_INIT_H)];
	window.releasedWhenClosed = NO;

	_entries    = [[NSMutableArray alloc] init];
	_currentDir = @"";
	_sortColumn = -1;
	_sortAsc    = YES;

	[self buildViews];
	[self layoutViews];

	// 初始目录: 空/不存在时退化为用户主目录
	[self scanDir:[[self class] resolvedInitialDir:dir]];

	return self;
}

- (void)dealloc
{
	[self stopScan];
}

#pragma mark - 视图构建

- (void)buildViews
{
	NSView*       content = self.window.contentView;
	NSInteger     i;

	// ---- 顶部: 文件夹路径 ----
	self.dirLabel     = openrom_make_label(@"文件夹:");
	self.countLabel   = openrom_make_label(@"未选择文件夹");
	self.countLabel.font = [NSFont systemFontOfSize:11];

	self.dirField = [[NSTextField alloc] initWithFrame:NSZeroRect];
	self.dirField.stringValue     = @"";
	self.dirField.font            = [NSFont systemFontOfSize:12];
	self.dirField.editable        = YES;
	self.dirField.selectable      = YES;
	self.dirField.bezeled         = YES;
	self.dirField.bezelStyle      = NSTextFieldSquareBezel;
	self.dirField.lineBreakMode   = NSLineBreakByTruncatingHead;
	self.dirField.target          = self;
	// 路径输入框内回车 => 按输入的路径刷新列表, 而不是加载 ROM
	self.dirField.action          = @selector(onDirFieldCommit:);

	self.browseButton = [[NSButton alloc] initWithFrame:NSZeroRect];
	self.browseButton.bezelStyle = NSBezelStyleRounded;
	self.browseButton.target     = self;
	self.browseButton.action     = @selector(onBrowseDir:);

	// 等价 win32 的 SHGetFileInfo 文件夹图标; 取不到时退化为文字按钮
	if ([NSImage imageNamed:NSImageNameFolder] != nil)
	{
		self.browseButton.image         = [NSImage imageNamed:NSImageNameFolder];
		self.browseButton.imagePosition = NSImageOnly;
		self.browseButton.title         = @"";
	}
	else
	{
		self.browseButton.title = @"...";
	}

	[content addSubview:self.dirLabel];
	[content addSubview:self.dirField];
	[content addSubview:self.browseButton];

	// ---- 中部: 文件列表 ----
	self.listView = [[NSTableView alloc] initWithFrame:NSZeroRect];
	self.listView.dataSource      = self;
	self.listView.delegate        = self;
	self.listView.rowHeight       = 18;
	// 等价 LVS_SINGLESEL + LVS_SHOWSELALWAYS: 单选, 且失焦后仍保持可见的高亮
	self.listView.allowsMultipleSelection = NO;
	self.listView.allowsEmptySelection    = YES;
	self.listView.allowsColumnReordering  = NO;
	// 等价 LVS_REPORT 的固定列宽(不随窗口拉伸改变)
	self.listView.columnAutoresizingStyle = NSTableViewNoColumnAutoresizing;
	self.listView.gridStyleMask           = NSTableViewSolidHorizontalGridLineMask;
	self.listView.target                  = self;
	self.listView.doubleAction            = @selector(onListDoubleClick:);

	for (i = 0; i < (NSInteger)count_of(s_columns); i++)
	{
		NSTableColumn*  col = [[NSTableColumn alloc] initWithIdentifier:
								[NSString stringWithFormat:@"col%ld", (long)i]];

		col.title  = [NSString stringWithUTF8String:s_columns[i].title];
		col.width  = s_columns[i].width;
		col.minWidth = 40;
		// 表头可点排序: NSTableView 依据 sortDescriptorPrototype 自动在升/降序之间
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

	// ---- 底部: 按钮与统计文字 ----
	self.loadButton = [[NSButton alloc] initWithFrame:NSZeroRect];
	self.loadButton.title         = @"加载";
	self.loadButton.bezelStyle    = NSBezelStyleRounded;
	self.loadButton.target        = self;
	self.loadButton.action        = @selector(onLoad:);
	// 等价 win32 的 DEFPUSHBUTTON
	self.loadButton.keyEquivalent = @"\r";
	self.loadButton.enabled       = NO;

	self.cancelButton = [[NSButton alloc] initWithFrame:NSZeroRect];
	self.cancelButton.title         = @"取消";
	self.cancelButton.bezelStyle    = NSBezelStyleRounded;
	self.cancelButton.target        = self;
	self.cancelButton.action        = @selector(onCancel:);
	self.cancelButton.keyEquivalent = @"\e";

	[content addSubview:self.countLabel];
	[content addSubview:self.loadButton];
	[content addSubview:self.cancelButton];
}

#pragma mark - 布局

// 按内容区大小重排控件: 顶部固定, 底部固定, 中部列表拉伸。
// 算式逐条对应 win32 的 dlgOpenRom_Layout()。
- (void)layoutViews
{
	NSRect    bounds = self.window.contentView.bounds;
	CGFloat   cx = NSWidth(bounds);
	CGFloat   cy = NSHeight(bounds);
	CGFloat   x;
	CGFloat   y;
	CGFloat   w;
	CGFloat   h;

	// ---- 顶部: 文件夹路径 ----
	self.dirLabel.frame = NSMakeRect(OPENROM_MARGIN, OPENROM_MARGIN + 5,
									 OPENROM_LABEL_W, OPENROM_ROW_H - 10);

	x = OPENROM_MARGIN + OPENROM_LABEL_W;
	y = OPENROM_MARGIN;
	h = OPENROM_ROW_H;

	self.browseButton.frame = NSMakeRect(cx - OPENROM_MARGIN - OPENROM_BROWSE_W, y,
										 OPENROM_BROWSE_W, h);

	w = (cx - OPENROM_MARGIN - OPENROM_BROWSE_W - 6) - x;
	if (w < 60)
		w = 60;

	self.dirField.frame = NSMakeRect(x, y + 3, w, h - 6);

	// ---- 底部: 按钮与统计文字 ----
	y = cy - OPENROM_MARGIN - OPENROM_BTN_H;

	self.countLabel.frame = NSMakeRect(OPENROM_MARGIN, y + 6, 240, 16);

	self.loadButton.frame = NSMakeRect(cx - OPENROM_MARGIN - OPENROM_BTN_W * 2 - OPENROM_BTN_GAP,
									   y, OPENROM_BTN_W, OPENROM_BTN_H);
	self.cancelButton.frame = NSMakeRect(cx - OPENROM_MARGIN - OPENROM_BTN_W, y,
										 OPENROM_BTN_W, OPENROM_BTN_H);

	// ---- 中部: 文件列表(拉伸) ----
	y = OPENROM_MARGIN + OPENROM_ROW_H + 6;
	h = (cy - OPENROM_MARGIN - OPENROM_BTN_H - 6) - y;
	if (h < 60)
		h = 60;

	self.scrollView.frame = NSMakeRect(OPENROM_MARGIN, y, cx - OPENROM_MARGIN * 2, h);
}

#pragma mark - 通用

// 根据列表当前选中状态启用/禁用"加载"按钮。
- (void)updateButtons
{
	self.loadButton.enabled = (self.listView.selectedRow >= 0);
}

- (void)showWarning:(NSString*)message
{
	NSAlert*  alert = [[NSAlert alloc] init];

	alert.messageText     = message;
	alert.alertStyle      = NSAlertStyleWarning;
	[alert addButtonWithTitle:@"确定"];

	// 用阻塞式模态而不是 sheet: 调用点之后紧跟着"重新扫描"等动作,
	// 需要等用户确认完再继续(与 win32 的 MessageBox 语义一致)
	[alert runModal];
}

#pragma mark - 扫描(懒加载解析)

/**
 * 比较两个整数(供排序使用)。
 */
static NSComparisonResult openrom_compare_number(long long v1, long long v2)
{
	if (v1 == v2)
		return NSOrderedSame;

	return (v1 < v2) ? NSOrderedAscending : NSOrderedDescending;
}


/**
 * 推导初始浏览目录: 不可用时退化为用户主目录。
 *
 * 对应 win32 的"初始目录为空则由对话框使用当前工作目录"; macOS 下应用包的
 * 工作目录恒为 "/", 没有使用价值, 因此改为退化为用户主目录。
 */
+ (NSString*)resolvedInitialDir:(NSString*)dir
{
	BOOL  is_dir = NO;

	if ((dir != nil) && (dir.length > 0)
	 && [[NSFileManager defaultManager] fileExistsAtPath:dir isDirectory:&is_dir]
	 && is_dir)
	{
		return openrom_normalize_dir(dir);
	}

	return NSHomeDirectory();
}


/**
 * 扫描指定文件夹。
 *
 * 分两步: 先枚举目录条目(不打开文件)把文件名与文件大小填进列表, 再由定时器
 * 分批打开文件读 16 字节头补齐其余属性列。逐文件打开是扫描中唯一耗时的部分,
 * 分片后界面不会被长时间阻塞。
 */
- (void)scanDir:(NSString*)dir
{
	NSFileManager*                      fm = [NSFileManager defaultManager];
	NSArray<NSString*>*                 names;
	NSMutableArray<iNESOpenRomEntry*>*  found;
	NSString*                           normalized;
	NSString*                           name;
	NSString*                           full;
	NSDictionary*                       attr;
	iNESOpenRomEntry*                   entry;

	// 重新扫描前先停掉上一轮解析, 清空列表
	[self stopScan];
	[_entries removeAllObjects];
	[self.listView reloadData];

	normalized = openrom_normalize_dir(dir);

	if (normalized.length == 0)
	{
		_currentDir = @"";
		self.countLabel.stringValue = @"未选择文件夹";
		[self updateButtons];
		return;
	}

	_currentDir = [normalized copy];
	self.dirField.stringValue = _currentDir;

	// ---- 阶段 1: 枚举目录条目(不打开文件) ----
	names = [fm contentsOfDirectoryAtPath:_currentDir error:NULL];
	found = [NSMutableArray array];

	for (name in names)
	{
		if ((NSInteger)found.count >= OPENROM_MAX_FILES)
			break;

		// 等价 win32 的 "*.nes": 扩展名比较不区分大小写(Windows 的文件通配同样不区分)
		if (NSOrderedSame != [name.pathExtension caseInsensitiveCompare:@"nes"])
			continue;

		full = [_currentDir stringByAppendingPathComponent:name];

		// 文件名与文件大小直接来自目录条目(无需打开文件);
		// 文件头属性由随后的解析阶段分批补齐
		attr = [fm attributesOfItemAtPath:full error:NULL];
		if ((attr == nil) || ![attr[NSFileType] isEqual:NSFileTypeRegular])
			continue;

		entry = [[iNESOpenRomEntry alloc] init];
		entry.name     = name;
		entry.fileSize = [attr[NSFileSize] longLongValue];

		[found addObject:entry];
	}

	// 按文件名升序排列: macOS 的目录枚举顺序不稳定, 固定顺序以便每次打开结果一致
	[found sortUsingComparator:^NSComparisonResult(iNESOpenRomEntry* e1, iNESOpenRomEntry* e2)
	{
		return [e1.name caseInsensitiveCompare:e2.name];
	}];

	[_entries addObjectsFromArray:found];
	[self.listView reloadData];

	if (_entries.count == 0)
	{
		self.countLabel.stringValue = @"未找到 NES 文件";
		INES_LOG(LOG_INF, MOD_SYS, ISTR("scan dir `%s`: no nes file found.\n"),
			[_currentDir fileSystemRepresentation]);
		[self updateButtons];
		return;
	}

	// ---- 阶段 2: 定时器分批读文件头 ----
	_scanNext = 0;
	_scanning = YES;

	self.countLabel.stringValue = [NSString stringWithFormat:@"正在解析 0/%lu ...",
									(unsigned long)_entries.count];

	INES_LOG(LOG_INF, MOD_SYS, ISTR("scan dir `%s`: %lu nes file(s) enumerated.\n"),
		[_currentDir fileSystemRepresentation], (unsigned long)_entries.count);

	// 模态循环跑在 NSModalPanelRunLoopMode 下, 定时器必须挂到 common modes 才会触发
	_scanTimer = [NSTimer timerWithTimeInterval:OPENROM_SCAN_TICK_SEC
										 target:self
									   selector:@selector(onScanTimer:)
									   userInfo:nil
										repeats:YES];
	[[NSRunLoop currentRunLoop] addTimer:_scanTimer forMode:NSRunLoopCommonModes];

	[self updateButtons];
}


/**
 * 停止解析阶段(重新扫描或销毁对话框时调用)。
 */
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


/**
 * 解析阶段的定时器处理: 在一个时间片内尽可能多地解析文件头。
 *
 * 用时间预算而不是固定条数, 避免磁盘较慢时单次消息处理过久导致界面卡顿。
 */
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
		&& (batch < OPENROM_SCAN_MAX_PER_TICK)
		&& (([NSDate timeIntervalSinceReferenceDate] - start) < OPENROM_SCAN_BUDGET_SEC))
	{
		// 被移除的行不占位置: 该行号上已经是下一行, 因此游标不前进
		if ([self parseEntryAtIndex:index])
			index++;

		batch++;
	}

	_scanNext = index;
	count     = (NSInteger)_entries.count;

	// 每个时间片结束后统一刷新一次表格(比逐行刷新省得多, 视觉上同样是"逐渐填满")
	[self.listView reloadData];

	if (index >= count)
	{
		[self finishScan];
		return;
	}

	self.countLabel.stringValue = [NSString stringWithFormat:@"正在解析 %ld/%ld ...",
									(long)index, (long)count];
}


/**
 * 解析阶段结束: 停掉定时器, 显示最终计数, 应用排序并默认选中第一行。
 */
- (void)finishScan
{
	[self stopScan];

	if (_entries.count > 0)
	{
		self.countLabel.stringValue = [NSString stringWithFormat:@"共 %lu 个支持的 NES 文件",
										(unsigned long)_entries.count];

		// 属性已齐, 此时才应用排序(解析期间属性不完整, 排了也不准)
		[self applySort];

		// 默认选中第一行, 便于直接点击"加载"
		[self.listView selectRowIndexes:[NSIndexSet indexSetWithIndex:0]
				   byExtendingSelection:NO];
		[self.listView scrollRowToVisible:0];
	}
	else
	{
		self.countLabel.stringValue = @"未找到支持的 NES 文件";
	}

	INES_LOG(LOG_INF, MOD_SYS, ISTR("scan dir `%s`: %lu nes file(s) listed.\n"),
		[_currentDir fileSystemRepresentation], (unsigned long)_entries.count);

	[self updateButtons];
}


/**
 * 解析一个列表行: 读文件头并补齐属性列。
 *
 * 不是有效 iNES 文件时直接从列表中移除该行, 与"只列出受支持文件"的语义一致。
 *
 * @param index 列表行号
 * @return YES 表示该行保留(游标应前进); NO 表示该行已被移除
 */
- (BOOL)parseEntryAtIndex:(NSInteger)index
{
	iNESOpenRomEntry*  entry;
	openrom_rominfo_t  info;
	NSString*          path;

	if ((index < 0) || (index >= (NSInteger)_entries.count))
		return YES;

	entry = _entries[index];

	// 同一行被重复处理时直接跳过
	if (entry.parsed)
		return YES;

	path = [_currentDir stringByAppendingPathComponent:entry.name];

	if (!openrom_parse_header(path, &info))
	{
		INES_LOG(LOG_DBG, MOD_SYS, ISTR("`%s` is not a supported nes file, removed from list.\n"),
			[path fileSystemRepresentation]);

		[_entries removeObjectAtIndex:index];

		return NO;
	}

	// 文件大小由目录条目给出, 文件头里没有这一项
	entry.mapperNum  = info.mapper_num;
	entry.mirrorType = info.mirror_type;
	entry.hasSram    = (info.has_sram != 0);
	entry.hasTrainer = (info.has_trainer != 0);
	entry.prgKb      = info.prg_kb;
	entry.chrKb      = info.chr_kb;
	entry.parsed     = YES;

	return YES;
}

#pragma mark - 排序

/**
 * 按当前排序列与方向重排列表。
 *
 * 属性还没解析完时只同步表头箭头, 排序本身推迟到解析结束再应用。
 */
- (void)applySort
{
	NSInteger  column    = _sortColumn;
	BOOL       ascending = _sortAsc;
	NSArray*   sorted;

	// 与 win32 的 s_iSortColumn < 0 一致: 未排序时保持文件系统枚举顺序
	if (column < 0)
	{
		[self updateSortIndicator];
		return;
	}

	if (_scanning)
	{
		// 文件头属性还没解析完, 此时排序结果不可信, 留到解析结束再统一应用
		INES_LOG(LOG_DBG, MOD_SYS, ISTR("scanning, sort by column %ld deferred.\n"), (long)column);
		[self updateSortIndicator];
		return;
	}

	sorted = [_entries sortedArrayUsingComparator:^NSComparisonResult(iNESOpenRomEntry* e1,
																	  iNESOpenRomEntry* e2)
	{
		return [self compareEntry:e1 with:e2 column:column ascending:ascending];
	}];

	[_entries setArray:sorted];
	[self.listView reloadData];

	[self updateSortIndicator];
}


/**
 * 同步表头的排序箭头。
 *
 * NSTableView 会依据 sortDescriptors 自行绘制升/降序指示器(等价 win32 想让
 * HDF_SORTUP / HDF_SORTDOWN 达到的效果); 未排序时不设置, 表头也就没有箭头。
 */
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


/**
 * 列序号 -> 排序描述符的 key(与 NSTableColumn.identifier 一致)。
 */
- (NSString*)keyForColumnIndex:(NSInteger)column
{
	return [NSString stringWithFormat:@"col%ld", (long)column];
}


/**
 * 排序描述符的 key -> 列序号, 无法识别时返回 -1。
 */
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


/**
 * 按列比较两个列表项(升序语义)。
 *
 * @param column    列序号, 0 为文件名, 1-7 为文件头属性
 * @param ascending 是否升序
 * @return 排序结果
 */
- (NSComparisonResult)compareEntry:(iNESOpenRomEntry*)entry1
							  with:(iNESOpenRomEntry*)entry2
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
		result = openrom_compare_number(entry1.fileSize, entry2.fileSize);
		break;

	case 2:
		result = openrom_compare_number(entry1.mapperNum, entry2.mapperNum);
		break;

	case 3:
		result = openrom_compare_number(entry1.prgKb, entry2.prgKb);
		break;

	case 4:
		result = openrom_compare_number(entry1.chrKb, entry2.chrKb);
		break;

	case 5:
		result = openrom_compare_number(entry1.mirrorType, entry2.mirrorType);
		break;

	case 6:
		result = openrom_compare_number(entry1.hasSram ? 1 : 0, entry2.hasSram ? 1 : 0);
		break;

	case 7:
		result = openrom_compare_number(entry1.hasTrainer ? 1 : 0, entry2.hasTrainer ? 1 : 0);
		break;

	default:
		break;
	}

	// 主键相同的项再按文件名比较, 保证排序结果稳定可预期
	if ((result == NSOrderedSame) && (column != 0))
		result = [entry1.name caseInsensitiveCompare:entry2.name];

	if (result == NSOrderedSame)
		return NSOrderedSame;

	if (ascending)
		return result;

	return (result == NSOrderedAscending) ? NSOrderedDescending : NSOrderedAscending;
}

#pragma mark - 交互动作

/**
 * 路径输入框内回车: 按输入的路径刷新列表(不加载 ROM)。
 */
- (IBAction)onDirFieldCommit:(id)sender
{
	[self applyTypedDir];
}


/**
 * "选择文件夹"按钮: 弹出系统文件夹选择面板。
 */
- (IBAction)onBrowseDir:(id)sender
{
	NSOpenPanel*  panel = [NSOpenPanel openPanel];

	panel.title                   = @"请选择 NES 文件所在文件夹";
	panel.canChooseFiles          = NO;
	panel.canChooseDirectories    = YES;
	panel.allowsMultipleSelection = NO;
	panel.canCreateDirectories    = NO;

	// 等价 win32 的 BFFM_SETSELECTION: 让面板默认定位到当前目录
	if (_currentDir.length > 0)
		panel.directoryURL = [NSURL fileURLWithPath:_currentDir];

	if ([panel runModal] != NSModalResponseOK)
		return;

	if (panel.URL == nil)
		return;

	[self scanDir:panel.URL.path];
}


/**
 * "加载"按钮(同时是窗口的默认按钮): 等价 win32 的 DEFPUSHBUTTON。
 *
 * 焦点在路径输入框时, 回车应当刷新列表而不是加载 ROM(对应 win32 的
 * GetFocus() 判断); 那种情况由输入框自身的 action 处理, 这里直接返回。
 */
- (IBAction)onLoad:(id)sender
{
	if ([self isEditingDirField])
		return;

	[self loadSelected];
}


/**
 * "取消"按钮(同时是 Esc 快捷键)。
 */
- (IBAction)onCancel:(id)sender
{
	[NSApp stopModalWithCode:NSModalResponseCancel];
}


/**
 * 双击列表项直接加载(等价 win32 的 NM_DBLCLK)。
 */
- (IBAction)onListDoubleClick:(id)sender
{
	if (self.listView.clickedRow >= 0)
		[self loadSelected];
}


/**
 * 当前键盘焦点是否在路径输入框的字段编辑器内。
 */
- (BOOL)isEditingDirField
{
	NSResponder*  responder = self.window.firstResponder;

	// 正在编辑的文本框会把字段编辑器(NSTextView)的 delegate 指向自己;
	// 转成 id 比较, 避免 id<NSTextViewDelegate> 与 NSTextField* 的类型不匹配警告
	return ([responder isKindOfClass:[NSTextView class]]
		 && ((id)[(NSTextView*)responder delegate] == (id)self.dirField));
}


/**
 * 应用路径输入框手工输入的目录。
 */
- (void)applyTypedDir
{
	NSString*  dir;
	BOOL       is_dir = NO;

	dir = openrom_normalize_dir(self.dirField.stringValue);

	if (dir.length == 0)
		return;

	if (![[NSFileManager defaultManager] fileExistsAtPath:dir isDirectory:&is_dir] || !is_dir)
	{
		[self showWarning:@"文件夹不存在或不可访问!"];
		[self.window makeFirstResponder:self.dirField];
		return;
	}

	// 回写规范化后的路径, 然后刷新列表
	self.dirField.stringValue = dir;
	[self scanDir:dir];
}


/**
 * 取当前选中的文件, 记录完整路径并结束对话框。
 */
- (void)loadSelected
{
	NSInteger  row = self.listView.selectedRow;
	NSString*  path;

	if ((row < 0) || (row >= (NSInteger)_entries.count))
		return;

	path = [_currentDir stringByAppendingPathComponent:_entries[row].name];

	// 加载前再确认一次文件是否仍然存在
	if (![[NSFileManager defaultManager] fileExistsAtPath:path])
	{
		[self showWarning:@"文件已不存在, 请重新选择!"];
		[self scanDir:_currentDir];
		return;
	}

	_selectedPath = [path copy];

	[NSApp stopModalWithCode:NSModalResponseOK];
}

#pragma mark - NSTableViewDataSource / NSTableViewDelegate

- (NSInteger)numberOfRowsInTableView:(NSTableView*)tableView
{
	return (NSInteger)_entries.count;
}


- (NSView*)tableView:(NSTableView*)tableView
   viewForTableColumn:(NSTableColumn*)tableColumn
				  row:(NSInteger)row
{
	NSInteger     column;
	NSTextField*  field;

	column = [self columnIndexForKey:tableColumn.identifier];

	if ((column < 0) || (row < 0) || (row >= (NSInteger)_entries.count))
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
	}

	field.stringValue = [self textForEntry:_entries[row] column:column];
	field.alignment   = s_columns[column].right_align ? NSTextAlignmentRight : NSTextAlignmentLeft;

	return field;
}


- (void)tableViewSelectionDidChange:(NSNotification*)notification
{
	[self updateButtons];
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
	// 新方向, 与 win32 的 dlgOpenRom_OnColumnClick() 行为一致, 这里直接采用
	_sortColumn = column;
	_sortAsc    = descriptor.ascending;

	INES_LOG(LOG_DBG, MOD_SYS, ISTR("sort list by column %ld (%s).\n"),
		(long)column, _sortAsc ? ISTR("asc") : ISTR("desc"));

	[self applySort];
}


/**
 * 取某一列在某一行上要显示的文本。
 */
- (NSString*)textForEntry:(iNESOpenRomEntry*)entry column:(NSInteger)column
{
	if (entry == nil)
		return @"";

	switch (column)
	{
	case 0:
		return entry.name;

	case 1:
		// 枚举阶段就已确定, 无需打开文件
		return openrom_format_size(entry.fileSize);

	case 2:
		return entry.parsed ? [NSString stringWithFormat:@"%u", (unsigned)entry.mapperNum] : @"";

	case 3:
		return entry.parsed ? [NSString stringWithFormat:@"%u KB", (unsigned)entry.prgKb] : @"";

	case 4:
		return entry.parsed ? [NSString stringWithFormat:@"%u KB", (unsigned)entry.chrKb] : @"";

	case 5:
		return entry.parsed ? openrom_mirror_text(entry.mirrorType) : @"";

	case 6:
		return entry.parsed ? (entry.hasSram ? @"有" : @"无") : @"";

	case 7:
		return entry.parsed ? (entry.hasTrainer ? @"有" : @"无") : @"";

	default:
		break;
	}

	return @"";
}

#pragma mark - NSWindowDelegate

- (void)windowDidResize:(NSNotification*)notification
{
	[self layoutViews];
}


- (void)windowWillClose:(NSNotification*)notification
{
	// 解除定时器对 self 的强引用, 避免窗口关闭后仍被扫描定时器持有
	[self stopScan];

	// 用户在模态期间点了关闭按钮: 结束模态循环
	// (点"加载"成功时已由 loadSelected 结束, 这里不会重复)
	if (_selectedPath == nil)
		[NSApp stopModalWithCode:NSModalResponseCancel];
}

#pragma mark - 模态入口

+ (NSString*)runModalWithInitialDir:(NSString*)initialDir
							  owner:(NSWindow*)owner
						   lastDir:(NSString**)outLastDir
{
	iNESOpenRomDialog*  dialog;

	if (outLastDir != NULL)
		*outLastDir = nil;

	dialog = [[iNESOpenRomDialog alloc] initWithInitialDir:initialDir];
	if (dialog == nil)
		return nil;

	// 初始大小与位置: 相对所有者窗口居中, 并保证不超出屏幕工作区
	[dialog placeWindowRelativeTo:owner];

	[NSApp runModalForWindow:dialog.window];

	// 回传对话框关闭时所在的文件夹(点"加载"或"取消"都一样), 供调用方记录位置
	if (outLastDir != NULL)
		*outLastDir = dialog->_currentDir;

	// 收尾: 先摘掉 delegate, 避免释放窗口时再走一次关闭流程
	dialog.window.delegate = nil;
	[dialog.window orderOut:nil];
	[dialog.window close];

	return dialog->_selectedPath;
}


/**
 * 把窗口摆到所有者窗口正中, 并保证不超出屏幕工作区
 * (等价 win32 dlgOpenRom_OnInitDialog() 里的居中与 SPI_GETWORKAREA 修正)。
 */
- (void)placeWindowRelativeTo:(NSWindow*)owner
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
