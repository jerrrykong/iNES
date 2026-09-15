// =====================================================================
// iNES macOS 前端 —— 应用控制器
//
// 与 win32/iNES.c 的对应关系:
//   _tWinMain + 菜单命令处理   -> applicationDidFinishLaunching + 菜单动作
//   OnIdle(帧循环 + 输入)      -> nes_proc(模拟线程) + simulationLoop
//   OnPaint(StretchDIBits)     -> iNESVideo.m
//   waveOut*                   -> iNESAudio.m
//   Get/WritePrivateProfile*   -> iNESConfig.c
//
// 线程模型:
//   * 模拟线程独占 host: 加载/卸载/复位/存档/读档都在该线程内完成, 主线程不直接触碰;
//   * 主线程通过 s_ctl(互斥量保护)下发请求, 并读取状态快照用于刷新界面;
//   * 画面 screen_front 由模拟线程写入、主线程截图时读取, 由 s_mutex_output 保护。
// =====================================================================

#import "iNESApp.h"

#import "iNESVideo.h"
#import "iNESAudio.h"
#import "iNESOsd.h"
#import "iNESPalette.h"
#import "iNESConfig.h"
#import "iNESDebug.h"
#import "iNESOpenRomDialog.h"
#import "iNESRegisterView.h"

#import "../comm/log.h"

#include <stdio.h>
#include <time.h>
#include <sys/stat.h>


#define APP_HISTORY_MAX          10         // 最近打开文件数(与 win32 一致)
#define APP_KEY_FLASH_FREQ       6          // 连发按键的半周期(帧), 与 win32 的 key_flash_freq 一致
#define APP_SRAM_SAVE_INTERVAL   5000000    // 自动保存 SRAM 的间隔(微秒), 与 win32 一致
#define APP_AUDIO_CACHE_NUM      4          // 在飞音频缓冲的目标值(等价 win32 的 audio_cache_num)
#define APP_AUDIO_LOW_WATER      2          // 水位低于该值就补静音(必须 > 0, 见帧循环里的说明)
#define APP_STATE_MAGIC          0x41545349 // 'ISTA' 即时存档文件头魔数(见 core/nes.c 的 INES_STATE_HEADER_MAGIC)
#define APP_STATE_COUNT          10         // 即时存档槽位数
#define APP_STATE_NONE           (-1)       // 无存档/读档请求

// 菜单分组标识(validateMenuItem: 里据此做组内互斥勾选)
#define APP_GROUP_SCALE          @"group.scale"
#define APP_GROUP_ASPECT         @"group.aspect"
#define APP_GROUP_VOLUME         @"group.volume"
#define APP_GROUP_LOGLEVEL       @"group.loglevel"
#define APP_GROUP_SAVESTATE      @"group.savestate"
#define APP_GROUP_LOADSTATE      @"group.loadstate"


// ---------------------------------------------------------------------
// 全局状态
// ---------------------------------------------------------------------

ines_host_t     host;
ines_byte_t     screen_front[SCREEN_IMAGE_BYTES];
ines_int_t      nes_cpu_rate    = 0;
ines_char_t     szROMTitle[INES_MAX_TITLE] = {0};
ines_int_t      screen_scale    = 200;
ines_char_t     latest_open_files[APP_HISTORY_MAX][INES_MAX_PATH];


// ---------------------------------------------------------------------
// 主线程 <-> 模拟线程 的控制块
// ---------------------------------------------------------------------

typedef struct _ines_ctl_
{
	// ---- 主线程 -> 模拟线程: 一次性请求 ----
	ines_int_t   stop;            // 请求退出模拟线程
	ines_int_t   load_request;    // 请求载入 load_path
	ines_int_t   close_request;   // 请求卸载 ROM
	ines_int_t   hard_reset;      // 请求重新上电
	ines_int_t   soft_reset;      // 请求软件复位
	ines_int_t   save_state;      // 请求存档, APP_STATE_NONE 表示无请求, 否则为槽位号
	ines_int_t   load_state;      // 请求读档, APP_STATE_NONE 表示无请求, 否则为槽位号
	ines_char_t  load_path[INES_MAX_PATH];

	// ---- 主线程 -> 模拟线程: 实时状态 ----
	ines_int_t   pause;           // 0 / NES_STATUS_PAUSE / NES_STATUS_FRAME_STEP
	ines_int_t   volume;          // 0 ~ 100
	ines_int_t   mute;            // 0 / 1
	ines_int_t   osd;             // 0 / 1, 是否绘制 CPU 占用率
	ines_dword_t keys;            // IKEY_* 位掩码

	// ---- 模拟线程 -> 主线程: 状态快照 ----
	ines_int_t   status;          // NES_STATUS_*
	ines_dword_t rom_crc32;       // 已载入 ROM 的 crc32, 0 表示未载入
	ines_char_t  rom_title[INES_MAX_TITLE];
} ines_ctl_t;


static ines_ctl_t    s_ctl;
static ines_mutex_t  s_mutex_ctl;      // 保护 s_ctl
static ines_mutex_t  s_mutex_output;   // 保护 screen_front
static ines_thread_t s_thread;
static ines_int_t    s_thread_started = 0;

static ines_char_t   s_rom_file_path[INES_MAX_PATH];     // ROM 路径, 仅模拟线程访问
static ines_char_t   s_ram_file_path[INES_MAX_PATH];     // SRAM 路径, 仅模拟线程访问
static ines_char_t   s_load_path[INES_MAX_PATH];         // 从 s_ctl 拷出的待载入路径
static ines_byte_t   s_screen_back[SCREEN_IMAGE_BYTES];  // 模拟线程的绘制缓冲

static ines_char_t   s_save_dir[INES_MAX_PATH];          // <数据目录>/save
static ines_char_t   s_state_dir[INES_MAX_PATH];         // <数据目录>/state
static ines_char_t   s_picture_dir[INES_MAX_PATH];       // ~/Pictures/iNES(截图输出目录, 用户可见)

// 截图用的 BGRA 缓冲: 240KB, 仅主线程访问, 避免每次截图在栈上开大数组
static ines_byte_t   s_snapshot_bgra[SCREEN_IMAGE_BYTES * 4];


// ---------------------------------------------------------------------
// 工具函数
// ---------------------------------------------------------------------

// 安全的字符串拷贝(始终以 0 结尾)
static void app_str_copy(ines_str_t dst, ines_size_t dstLen, ines_cstr_t src)
{
	if ((dst == NULL) || (dstLen == 0))
		return;

	if (src == NULL)
	{
		dst[0] = 0;
		return;
	}

	ines_strncpy(dst, src, dstLen - 1);
	dst[dstLen - 1] = 0;
}

// 单调时钟(微秒), 用于帧率控制, 不受系统时间调整影响
static ines_int64_t app_cur_time_us(void)
{
	struct timespec  ts;

	clock_gettime(CLOCK_MONOTONIC, &ts);
	return (ines_int64_t)ts.tv_sec * 1000000 + (ines_int64_t)(ts.tv_nsec / 1000);
}

static void app_sleep_us(ines_int64_t us)
{
	struct timespec  ts;

	if (us <= 0)
		return;

	ts.tv_sec  = (time_t)(us / 1000000);
	ts.tv_nsec = (long)((us % 1000000) * 1000);
	nanosleep(&ts, NULL);
}

static void app_sleep_ms(ines_int_t ms)
{
	app_sleep_us((ines_int64_t)ms * 1000);
}

// 逐级创建目录(等价 mkdir -p), 已存在时忽略错误
static void app_make_dir(ines_cstr_t path)
{
	ines_char_t  buf[INES_MAX_PATH];
	ines_char_t* p;

	if ((path == NULL) || (path[0] == 0))
		return;

	app_str_copy(buf, sizeof(buf), path);

	for (p = buf + 1; *p != 0; p++)
	{
		if (*p == '/')
		{
			*p = 0;
			mkdir(buf, 0755);
			*p = '/';
		}
	}
	mkdir(buf, 0755);
}

// <存档目录>/<ROM 标题>.sav
static void app_save_path(ines_cstr_t title, ines_str_t out, ines_size_t outLen)
{
	ines_snprintf(out, outLen, ISTR("%s/%s.sav"), s_save_dir, title);
}

// <即时存档目录>/<ROM 标题>.st<N>
static void app_state_path(ines_cstr_t title, ines_int_t index, ines_str_t out, ines_size_t outLen)
{
	ines_snprintf(out, outLen, ISTR("%s/%s.st%d"), s_state_dir, title, (int)index);
}

// 即时存档文件头(与 core/nes.c 的 ines_state_header_t 前 16 字节布局一致)
#pragma pack(push, 1)
typedef struct _app_state_head_
{
	ines_dword_t   magic;
	ines_word_t    size;
	ines_word_t    version;
	ines_dword_t   save_time;
	ines_dword_t   prom_crc32;
} app_state_head_t;

#pragma pack(pop)

// 读取即时存档文件头。文件不存在/魔数不符/CRC 不匹配时返回 0, 否则返回存档时间戳。
// 仅用于主线程刷新菜单显示, 不做任何写入。
static ines_dword_t app_state_file_time(ines_cstr_t path, ines_dword_t crc32)
{
	FILE*             fp;
	app_state_head_t  head;

	if ((path == NULL) || (crc32 == 0))
		return 0;

	fp = fopen(path, "rb");
	if (fp == NULL)
		return 0;

	if ((1 != fread(&head, sizeof(head), 1, fp))
	 || (head.magic != APP_STATE_MAGIC)
	 || (head.prom_crc32 != crc32))
	{
		fclose(fp);
		return 0;
	}

	fclose(fp);
	return head.save_time;
}

// 日志时间戳: 用 CPU 周期数, 与 win32 前端一致
static ines_int64_t app_log_stamp_func(void)
{
	return host.cpu.total_cycles;
}

// 模拟线程入口(定义在本文件末尾)
static int nes_proc(void* ud);


// ---------------------------------------------------------------------
// 私有接口
// ---------------------------------------------------------------------

@interface iNESAppController ()
{
	// 主线程持有的界面状态(修改后通过 pushControlState 同步到 s_ctl)
	ines_int_t   _volume;
	ines_int_t   _mute;
	ines_int_t   _osd;
	ines_int_t   _aspectMode;

	// 截图完成的轻提示(同时只显示一条, 用序号避免旧定时器误删新提示)
	NSTextField* _toastView;
	NSUInteger   _toastSeq;
}

@property (nonatomic, strong) NSWindow*       window;
@property (nonatomic, strong) iNESVideoView*  videoView;
@property (nonatomic, strong) iNESAudio*      audio;
@property (nonatomic, copy)   NSString*       pendingRomPath;

@property (nonatomic, strong) id              keyEventMonitor;   // 无修饰键快捷键监听(等价 win32 加速键表)

@property (nonatomic, strong) NSMenu*         recentFilesMenu;
@property (nonatomic, strong) NSMenu*         saveStateMenu;
@property (nonatomic, strong) NSMenu*         loadStateMenu;

- (void)initDataDirs;
- (void)loadConfig;
- (void)buildMenuBar;
- (NSMenuItem*)addItemToMenu:(NSMenu*)menu
					   title:(NSString*)title
					  action:(SEL)action
					  keyEquiv:(NSString*)key
				   modifiers:(NSUInteger)mods
						 tag:(NSInteger)tag
					   group:(NSString*)group;
- (NSMenuItem*)addSubmenuToMenu:(NSMenu*)menu title:(NSString*)title;

- (ines_int_t)currentStatus;
- (ines_int_t)currentPause;
- (ines_dword_t)currentRomCrc32;
- (void)copyCurrentRomTitle:(ines_str_t)title length:(ines_size_t)len;
- (void)setPauseState:(ines_int_t)pause;
- (void)pushControlState;
- (void)refreshTitle;
- (void)showAlert:(NSString*)title message:(NSString*)message;
- (void)showToast:(NSString*)message;

- (void)createWindow;
- (NSPoint)windowOriginForSize:(NSSize)size;
- (void)requestLoadROM:(NSString*)path;
- (NSString*)romInitialDir;
- (void)updateRecentFilesMenu;
- (void)updateStateMenu:(NSMenu*)menu label:(NSString*)label;
- (void)loadHistories;
- (void)saveHistories;
- (void)addHistoryFile:(NSString*)path;

// ---- 以下方法只能在模拟线程上调用 ----
- (void)simulationLoop;
- (void)loadROMOnThread:(ines_cstr_t)path;
- (void)closeROMOnThread;
- (void)hardResetOnThread;
- (void)saveStateOnThread:(ines_int_t)index;
- (void)loadStateOnThread:(ines_int_t)index;
- (void)publishHostState;
- (void)clearScreenOnThread;
- (void)notifyRomLoaded:(ines_cstr_t)path;
- (void)notifyRomLoadFailed:(ines_cstr_t)path;

// ---- 以下方法只能在主线程上调用 ----
- (void)romDidLoadOnMain:(NSString*)path;
- (void)romDidFailOnMain:(NSString*)path;

@end


// F1 ~ F12 的 key equivalent
static NSString* app_function_key(ines_int_t n)
{
	unichar  c;

	switch (n)
	{
	case 1:  c = NSF1FunctionKey;   break;
	case 2:  c = NSF2FunctionKey;   break;
	case 3:  c = NSF3FunctionKey;   break;
	case 4:  c = NSF4FunctionKey;   break;
	case 5:  c = NSF5FunctionKey;   break;
	case 6:  c = NSF6FunctionKey;   break;
	case 7:  c = NSF7FunctionKey;   break;
	case 8:  c = NSF8FunctionKey;   break;
	case 9:  c = NSF9FunctionKey;   break;
	case 10: c = NSF10FunctionKey;  break;
	case 11: c = NSF11FunctionKey;  break;
	case 12: c = NSF12FunctionKey;  break;
	default: return @"";
	}

	return [NSString stringWithCharacters:&c length:1];
}


@implementation iNESAppController

+ (iNESAppController*)sharedInstance
{
	static iNESAppController*  g_instance = nil;
	static dispatch_once_t     g_once;

	dispatch_once(&g_once, ^{ g_instance = [[iNESAppController alloc] init]; });

	return g_instance;
}

- (id)init
{
	self = [super init];
	if (self != nil)
	{
		_volume     = 80;
		_mute       = 0;
		_osd        = 1;
		_aspectMode = INES_ASPECT_ORIGINAL;
	}
	return self;
}

#pragma mark - 启动 / 退出

// setPendingRomPath: 由 pendingRomPath 属性自动合成, 无需手写

- (void)startup
{
	INES_LOG(LOG_NTY, MOD_SYS, ISTR("iNES(macOS) frontend starting ...\n"));

	// 1) 全局初始化
	iNES_palette_init();
	ines_set_log_stamp_func(app_log_stamp_func);
	ines_mutex_init(&s_mutex_ctl);
	ines_mutex_init(&s_mutex_output);

	memset(&s_ctl, 0, sizeof(s_ctl));
	s_ctl.save_state = APP_STATE_NONE;
	s_ctl.load_state = APP_STATE_NONE;
	s_ctl.volume     = _volume;
	s_ctl.mute       = _mute;
	s_ctl.osd        = _osd;
	s_ctl.status     = NES_STATUS_OFF;

	// 2) 数据目录与配置
	[self initDataDirs];
	[self loadConfig];

	// 3) 核心
	ines_host_init(&host, 1);
	[self publishHostState];

	// 4) 界面
	[self buildMenuBar];
	[self createWindow];
	[self updateRecentFilesMenu];

	// 调试视图窗口以主窗口为 owner(仅用于退出时统一收尾)
	[iNESDebugManager sharedManager].parentWindow = self.window;

	// 5) 音频对象(设备在模拟线程上打开)
	self.audio = [[iNESAudio alloc] init];

	// 6) 模拟线程
	s_ctl.stop = 0;
	ines_thread_init(&s_thread, nes_proc, (__bridge void*)self);
	if (0 != ines_thread_start(&s_thread))
	{
		INES_LOG(LOG_ERR, MOD_SYS, ISTR("Create simulation thread failed!\n"));
		[self showAlert:@"启动失败" message:@"无法创建模拟线程, 请查看日志。"];
		return;
	}
	s_thread_started = 1;

	// 7) 命令行/拖拽传入的 ROM
	if ((self.pendingRomPath != nil) && (self.pendingRomPath.length > 0))
	{
		[self requestLoadROM:self.pendingRomPath];
		self.pendingRomPath = nil;
	}

	[self refreshTitle];
}

- (void)shutdown
{
	INES_LOG(LOG_NTY, MOD_SYS, ISTR("iNES(macOS) frontend shutting down ...\n"));

	if (s_thread_started)
	{
		ines_mutex_lock(&s_mutex_ctl);
		s_ctl.stop = 1;
		ines_mutex_unlock(&s_mutex_ctl);

		ines_thread_wait(&s_thread);
		ines_thread_fini(&s_thread);
		s_thread_started = 0;
	}

	[[iNESDebugManager sharedManager] closeAll];

	ines_host_free(&host);

	ines_mutex_fini(&s_mutex_output);
	ines_mutex_fini(&s_mutex_ctl);
}

#pragma mark - 数据目录与配置

- (void)initDataDirs
{
	ines_char_t  data_dir[INES_MAX_PATH];
	ines_char_t  config_file[INES_MAX_PATH];
	NSArray*     pictures;
	NSString*    picture_dir;

	ines_get_data_dir(data_dir, count_of(data_dir));

	ines_snprintf(s_save_dir,  sizeof(s_save_dir),  ISTR("%s/save"),  data_dir);
	ines_snprintf(s_state_dir, sizeof(s_state_dir), ISTR("%s/state"), data_dir);

	// 截图输出到用户可见的 ~/Pictures/iNES: 数据目录在 ~/Library 下,
	// Finder 里默认看不到, 截图会"找不到"。
	pictures = NSSearchPathForDirectoriesInDomains(NSPicturesDirectory, NSUserDomainMask, YES);
	if (pictures.count > 0)
		picture_dir = [pictures[0] stringByAppendingPathComponent:@"iNES"];
	else
		picture_dir = [NSString stringWithFormat:@"%s/snapshot", data_dir];  // 兜底: 退回数据目录

	app_str_copy(s_picture_dir, sizeof(s_picture_dir), picture_dir.UTF8String);

	app_make_dir(s_save_dir);
	app_make_dir(s_state_dir);
	app_make_dir(s_picture_dir);

	ines_snprintf(config_file, sizeof(config_file), ISTR("%s/config.ini"), data_dir);
	iNES_config_set_file(config_file);

	INES_LOG(LOG_NTY, MOD_SYS, ISTR("Data directory: '%s'\n"), data_dir);
	INES_LOG(LOG_NTY, MOD_SYS, ISTR("Snapshot directory: '%s'\n"), s_picture_dir);
}

- (void)loadConfig
{
	ines_int_t  value;

	// ---- 显示 ----
	value = GetConfigInt(ISTR("display"), ISTR("scale"), 200);
	if ((value != 100) && (value != 200) && (value != 300) && (value != 400))
		value = 200;
	screen_scale = value;

	_aspectMode = GetConfigInt(ISTR("display"), ISTR("aspect"), INES_ASPECT_ORIGINAL);
	if ((_aspectMode != INES_ASPECT_ORIGINAL) && (_aspectMode != INES_ASPECT_4_3) && (_aspectMode != INES_ASPECT_16_9))
		_aspectMode = INES_ASPECT_ORIGINAL;

	_osd = GetConfigInt(ISTR("display"), ISTR("osd"), 1) ? 1 : 0;

	// ---- 音频 ----
	value = GetConfigInt(ISTR("audio"), ISTR("volume"), 80);
	if (value < 0)
		value = 0;
	if (value > 100)
		value = 100;
	_volume = value;

	_mute = GetConfigInt(ISTR("audio"), ISTR("mute"), 0) ? 1 : 0;

	[self loadHistories];
	[self pushControlState];
}

#pragma mark - 窗口

- (NSPoint)windowOriginForSize:(NSSize)size
{
	ines_int_t  x = GetConfigInt(ISTR("display"), ISTR("x"), -1);
	ines_int_t  y = GetConfigInt(ISTR("display"), ISTR("y"), -1);
	NSRect      rect;
	NSScreen*   screen;

	if ((x < 0) || (y < 0))
		return NSMakePoint(NSNotFound, NSNotFound);

	rect = NSMakeRect((CGFloat)x, (CGFloat)y, size.width, size.height);

	// 只接受与某块屏幕可见区域相交的坐标, 避免越界坐标把窗口丢到屏幕外
	for (screen in [NSScreen screens])
	{
		if (NSIntersectsRect(rect, screen.visibleFrame))
			return rect.origin;
	}

	return NSMakePoint(NSNotFound, NSNotFound);
}

- (void)createWindow
{
	NSUInteger  style;

	self.videoView = [[iNESVideoView alloc] initWithFrame:NSMakeRect(0, 0,
						SCREEN_WIDTH * screen_scale / 100.0, SCREEN_HEIGHT * screen_scale / 100.0)];
	self.videoView.delegate     = self;
	self.videoView.scalePercent = screen_scale;
	self.videoView.aspectMode   = _aspectMode;
	self.videoView.showOsd      = (_osd != 0);

	style = NSWindowStyleMaskTitled | NSWindowStyleMaskClosable
		  | NSWindowStyleMaskMiniaturizable | NSWindowStyleMaskResizable;

	self.window = [[NSWindow alloc] initWithContentRect:NSMakeRect(0, 0, SCREEN_WIDTH, SCREEN_HEIGHT)
											  styleMask:style
												backing:NSBackingStoreBuffered
												  defer:NO];
	self.window.delegate           = self;
	self.window.releasedWhenClosed = NO;
	self.window.backgroundColor    = [NSColor blackColor];

	self.window.contentView = self.videoView;
	[self.window setContentSize:[self.videoView preferredContentSize]];

	{
		NSPoint  origin = [self windowOriginForSize:self.window.frame.size];
		if (origin.x == NSNotFound)
			[self.window center];
		else
			[self.window setFrameOrigin:origin];
	}

	[self.window makeFirstResponder:self.videoView];
	[self.window makeKeyAndOrderFront:nil];
}

#pragma mark - 菜单

- (NSMenuItem*)addItemToMenu:(NSMenu*)menu
					   title:(NSString*)title
					  action:(SEL)action
					keyEquiv:(NSString*)key
				   modifiers:(NSUInteger)mods
						 tag:(NSInteger)tag
					   group:(NSString*)group
{
	NSMenuItem*  item = [[NSMenuItem alloc] initWithTitle:title
												  action:action
										   keyEquivalent:(key != nil) ? key : @""];

	item.target = self;
	item.tag    = tag;

	if (group != nil)
		item.representedObject = group;

	// 无修饰键的快捷键(方向键/空格/功能键)必须显式清零, 否则默认是 Command
	if (key != nil)
		item.keyEquivalentModifierMask = mods;

	[menu addItem:item];
	return item;
}

- (NSMenuItem*)addSubmenuToMenu:(NSMenu*)menu title:(NSString*)title
{
	NSMenuItem*  item = [[NSMenuItem alloc] initWithTitle:title action:nil keyEquivalent:@""];
	NSMenu*      sub  = [[NSMenu alloc] initWithTitle:title];

	item.submenu = sub;
	[menu addItem:item];

	return item;
}

- (void)buildMenuBar
{
	NSMenu*      main_menu = [[NSMenu alloc] initWithTitle:@""];
	NSMenuItem*  root;
	NSMenu*      menu;
	NSMenuItem*  item;
	ines_int_t   i;

	NSApp.mainMenu = main_menu;

	// ------------------ 应用菜单 ------------------
	root = [self addSubmenuToMenu:main_menu title:@"iNES"];
	menu = root.submenu;
	[self addItemToMenu:menu title:@"关于 iNES" action:@selector(showAbout:) keyEquiv:nil modifiers:0 tag:0 group:nil];
	[menu addItem:[NSMenuItem separatorItem]];
	item = [self addItemToMenu:menu title:@"隐藏 iNES" action:@selector(hide:) keyEquiv:@"h"
					 modifiers:NSEventModifierFlagCommand tag:0 group:nil];
	item.target = nil;
	[menu addItem:[NSMenuItem separatorItem]];
	item = [self addItemToMenu:menu title:@"退出 iNES" action:@selector(terminate:) keyEquiv:@"q"
					 modifiers:NSEventModifierFlagCommand tag:0 group:nil];
	item.target = nil;

	// ------------------ 文件 ------------------
	root = [self addSubmenuToMenu:main_menu title:@"文件"];
	menu = root.submenu;
	[self addItemToMenu:menu title:@"载入ROM…" action:@selector(openROM:) keyEquiv:@"o"
			  modifiers:NSEventModifierFlagCommand tag:0 group:nil];
	[self addItemToMenu:menu title:@"卸载ROM" action:@selector(closeROM:) keyEquiv:@"u"
			  modifiers:NSEventModifierFlagCommand tag:0 group:nil];
	[menu addItem:[NSMenuItem separatorItem]];

	root = [self addSubmenuToMenu:menu title:@"最近文件"];
	self.recentFilesMenu           = root.submenu;
	self.recentFilesMenu.delegate  = self;

	// ------------------ 控制 ------------------
	root = [self addSubmenuToMenu:main_menu title:@"控制"];
	menu = root.submenu;
	[self addItemToMenu:menu title:@"重新上电" action:@selector(hardReset:) keyEquiv:app_function_key(1)
			  modifiers:NSEventModifierFlagCommand tag:0 group:nil];
	[self addItemToMenu:menu title:@"软件复位" action:@selector(softReset:) keyEquiv:app_function_key(1)
			  modifiers:0 tag:0 group:nil];
	[menu addItem:[NSMenuItem separatorItem]];
	[self addItemToMenu:menu title:@"暂停" action:@selector(togglePause:) keyEquiv:@"p" modifiers:0 tag:0 group:nil];
	[self addItemToMenu:menu title:@"单帧执行" action:@selector(frameStep:) keyEquiv:@" " modifiers:0 tag:0 group:nil];
	[menu addItem:[NSMenuItem separatorItem]];
	[self addItemToMenu:menu title:@"全屏" action:@selector(toggleFullScreen:) keyEquiv:app_function_key(12)
			  modifiers:0 tag:0 group:nil];
	[menu addItem:[NSMenuItem separatorItem]];

	{
		ines_int_t  scales[] = {100, 200, 300, 400};

		root = [self addSubmenuToMenu:menu title:@"缩放"];
		for (i = 0; i < (ines_int_t)count_of(scales); i++)
		{
			[self addItemToMenu:root.submenu
						  title:[NSString stringWithFormat:@"x %d", (int)(scales[i] / 100)]
						 action:@selector(setScale:)
					   keyEquiv:app_function_key(5 + i)
					  modifiers:0
							tag:scales[i]
						  group:APP_GROUP_SCALE];
		}
	}

	root = [self addSubmenuToMenu:menu title:@"比例"];
	[self addItemToMenu:root.submenu title:@"原始比例" action:@selector(setAspect:) keyEquiv:nil
			  modifiers:0 tag:INES_ASPECT_ORIGINAL group:APP_GROUP_ASPECT];
	[self addItemToMenu:root.submenu title:@"4：3" action:@selector(setAspect:) keyEquiv:nil
			  modifiers:0 tag:INES_ASPECT_4_3 group:APP_GROUP_ASPECT];
	[self addItemToMenu:root.submenu title:@"16：9" action:@selector(setAspect:) keyEquiv:nil
			  modifiers:0 tag:INES_ASPECT_16_9 group:APP_GROUP_ASPECT];

	[menu addItem:[NSMenuItem separatorItem]];
	[self addItemToMenu:menu title:@"静音" action:@selector(toggleMute:) keyEquiv:app_function_key(9)
			  modifiers:0 tag:0 group:nil];

	{
		ines_int_t  volumes[] = {100, 80, 60, 40, 20, 0};

		root = [self addSubmenuToMenu:menu title:@"音量"];
		for (i = 0; i < (ines_int_t)count_of(volumes); i++)
		{
			[self addItemToMenu:root.submenu
						  title:[NSString stringWithFormat:@"%d%%", (int)volumes[i]]
						 action:@selector(setVolume:)
					   keyEquiv:nil
					  modifiers:0
							tag:volumes[i]
						  group:APP_GROUP_VOLUME];
		}
	}

	[menu addItem:[NSMenuItem separatorItem]];

	root = [self addSubmenuToMenu:menu title:@"即时存档"];
	self.saveStateMenu          = root.submenu;
	self.saveStateMenu.delegate = self;
	for (i = 0; i < APP_STATE_COUNT; i++)
	{
		[self addItemToMenu:self.saveStateMenu
					  title:[NSString stringWithFormat:@"存档 %d", (int)i]
					 action:@selector(saveState:)
				   keyEquiv:[NSString stringWithFormat:@"%d", (int)i]
				  modifiers:NSEventModifierFlagCommand
						tag:i
					  group:APP_GROUP_SAVESTATE];
	}

	root = [self addSubmenuToMenu:menu title:@"载入存档"];
	self.loadStateMenu          = root.submenu;
	self.loadStateMenu.delegate = self;
	for (i = 0; i < APP_STATE_COUNT; i++)
	{
		[self addItemToMenu:self.loadStateMenu
					  title:[NSString stringWithFormat:@"读档 %d", (int)i]
					 action:@selector(loadState:)
				   keyEquiv:[NSString stringWithFormat:@"%d", (int)i]
				  modifiers:(NSEventModifierFlagCommand | NSEventModifierFlagOption)
						tag:i
					  group:APP_GROUP_LOADSTATE];
	}

	[menu addItem:[NSMenuItem separatorItem]];
	// 截图用 Cmd+F10(与 win32 的 Ctrl+F10 对齐): 裸功能键会被 macOS 的系统快捷键/媒体键
	// 抢占(F11 "显示桌面" 已实测按不动), 加 Command 后系统不再拦截。
	[self addItemToMenu:menu title:@"截图" action:@selector(takeSnapshot:) keyEquiv:app_function_key(10)
			  modifiers:NSEventModifierFlagCommand tag:0 group:nil];

	// ------------------ 工具 ------------------
	root = [self addSubmenuToMenu:main_menu title:@"工具"];
	menu = root.submenu;
	[self addItemToMenu:menu title:@"选项…" action:@selector(showUnimplemented:) keyEquiv:nil
			  modifiers:0 tag:0 group:nil];
	[menu addItem:[NSMenuItem separatorItem]];
	[self addItemToMenu:menu title:@"显示 OSD" action:@selector(toggleOsd:) keyEquiv:nil
			  modifiers:0 tag:0 group:nil];
	[menu addItem:[NSMenuItem separatorItem]];

	root = [self addSubmenuToMenu:menu title:@"日志"];
	{
		struct _app_log_level_ { ines_int_t level; const char* title; };
		const struct _app_log_level_  levels[] = {
			{ LOG_TRA, "1 - TRACE"   },
			{ LOG_DBG, "2 - DEBUG"   },
			{ LOG_INF, "3 - INFO"    },
			{ LOG_WAR, "4 - WARNING" },
			{ LOG_ERR, "5 - ERROR"   },
			{ LOG_NTY, "10 - NOTIFY" },
			{ LOG_MAX, "N - NONE"    },
		};

		for (i = 0; i < (ines_int_t)count_of(levels); i++)
		{
			[self addItemToMenu:root.submenu
						  title:[NSString stringWithUTF8String:levels[i].title]
						 action:@selector(setLogLevel:)
					   keyEquiv:nil
					  modifiers:0
							tag:levels[i].level
						  group:APP_GROUP_LOGLEVEL];
		}
	}
	[self addItemToMenu:menu title:@"CPU TRACE" action:@selector(toggleCpuTrace:) keyEquiv:nil
			  modifiers:0 tag:0 group:nil];
	[menu addItem:[NSMenuItem separatorItem]];

	// 调试视图: 7 个独立工具窗口(与 win32 的 IDM_VIEW_* 一一对应, tag 即视图标识)。
	// 与 win32 一致: 菜单始终可点, 未载入 ROM 时窗口内为灰色。
	// 第 7 项"寄存器"在 win32 中是空实现, mac 端补齐(见 docs/register-view-plan.md)。
	root = [self addSubmenuToMenu:menu title:@"调试视图"];
	{
		NSArray<NSString*>*  titles = @[@"图形查看…", @"卷轴查看", @"调色板查看…", @"程序内存查看…",
										@"图案内存查看…", @"精灵内存查看…", @"寄存器查看…"];
		ines_int_t           view_id;

		for (view_id = 0; view_id < (ines_int_t)titles.count; view_id++)
		{
			[self addItemToMenu:root.submenu title:titles[view_id] action:@selector(showDebugView:)
					  keyEquiv:nil modifiers:0 tag:view_id group:nil];
		}
	}
	item = [self addItemToMenu:menu title:@"联网对战" action:nil keyEquiv:nil modifiers:0 tag:0 group:nil];
	item.enabled = NO;

	// ------------------ 帮助 ------------------
	root = [self addSubmenuToMenu:main_menu title:@"帮助"];
	[self addItemToMenu:root.submenu title:@"关于 iNES" action:@selector(showAbout:) keyEquiv:nil
			  modifiers:0 tag:0 group:nil];
}


#pragma mark - 状态访问

- (ines_int_t)currentStatus
{
	ines_int_t  status;

	ines_mutex_lock(&s_mutex_ctl);
	status = s_ctl.status;
	ines_mutex_unlock(&s_mutex_ctl);

	return status;
}

- (ines_int_t)currentPause
{
	ines_int_t  pause;

	ines_mutex_lock(&s_mutex_ctl);
	pause = s_ctl.pause;
	ines_mutex_unlock(&s_mutex_ctl);

	return pause;
}

- (ines_dword_t)currentRomCrc32
{
	ines_dword_t  crc32;

	ines_mutex_lock(&s_mutex_ctl);
	crc32 = s_ctl.rom_crc32;
	ines_mutex_unlock(&s_mutex_ctl);

	return crc32;
}

- (void)copyCurrentRomTitle:(ines_str_t)title length:(ines_size_t)len
{
	ines_mutex_lock(&s_mutex_ctl);
	app_str_copy(title, len, s_ctl.rom_title);
	ines_mutex_unlock(&s_mutex_ctl);
}

- (void)setPauseState:(ines_int_t)pause
{
	ines_mutex_lock(&s_mutex_ctl);
	s_ctl.pause = pause;
	ines_mutex_unlock(&s_mutex_ctl);

	[self refreshTitle];
}

// 主线程持有的界面状态 -> s_ctl(供模拟线程读取)
- (void)pushControlState
{
	ines_mutex_lock(&s_mutex_ctl);
	s_ctl.volume = _volume;
	s_ctl.mute   = _mute;
	s_ctl.osd    = _osd;
	ines_mutex_unlock(&s_mutex_ctl);
}

- (void)refreshTitle
{
	ines_int_t   status;
	ines_int_t   pause;
	ines_char_t  title[INES_MAX_TITLE];
	NSString*    state_text;

	if (![NSThread isMainThread])
	{
		[self performSelectorOnMainThread:@selector(refreshTitle) withObject:nil waitUntilDone:NO];
		return;
	}

	ines_mutex_lock(&s_mutex_ctl);
	status = s_ctl.status;
	pause  = s_ctl.pause;
	app_str_copy(title, sizeof(title), s_ctl.rom_title);
	ines_mutex_unlock(&s_mutex_ctl);

	if (status == NES_STATUS_OFF)
		state_text = @"未运行";
	else if (pause == NES_STATUS_PAUSE)
		state_text = @"暂停";
	else if (pause == NES_STATUS_FRAME_STEP)
		state_text = @"逐帧";
	else
		state_text = @"运行中";

	if (status == NES_STATUS_OFF)
		self.window.title = [NSString stringWithFormat:@"iNES - %@", state_text];
	else
		self.window.title = [NSString stringWithFormat:@"iNES - %s - %@", title, state_text];
}

- (void)showAlert:(NSString*)title message:(NSString*)message
{
	NSAlert*  alert = [[NSAlert alloc] init];

	alert.messageText     = (title != nil) ? title : @"";
	alert.informativeText = (message != nil) ? message : @"";
	[alert addButtonWithTitle:@"确定"];

	[alert runModal];
}

// 在画面视图上方浮现一条提示, 约 2.2 秒后淡出。用于截图等无对话框的反馈。
// 必须主线程调用; 同时只保留最新一条。
- (void)showToast:(NSString*)message
{
	NSTextField*  label;
	NSRect        bounds;
	NSUInteger    seq;

	if (![NSThread isMainThread])
	{
		[self performSelectorOnMainThread:@selector(showToast:) withObject:message waitUntilDone:NO];
		return;
	}

	if ((self.videoView == nil) || (message == nil))
		return;

	[_toastView removeFromSuperview];
	_toastView = nil;

	label = [[NSTextField alloc] initWithFrame:NSZeroRect];
	label.stringValue     = message;
	label.editable        = NO;
	label.selectable      = NO;
	label.bezeled         = NO;
	label.drawsBackground = YES;
	label.backgroundColor = [NSColor colorWithCalibratedWhite:0.0 alpha:0.72];
	label.textColor       = [NSColor whiteColor];
	label.font            = [NSFont systemFontOfSize:14.0 weight:NSFontWeightMedium];
	label.alignment       = NSTextAlignmentCenter;
	label.wantsLayer      = YES;
	label.layer.cornerRadius = 9.0;
	label.layer.masksToBounds = YES;

	[label sizeToFit];

	bounds = self.videoView.bounds;
	label.frame = NSMakeRect(0, 0,
							 label.frame.size.width + 30.0,
							 label.frame.size.height + 14.0);
	label.frame = NSMakeRect(floor(NSMidX(bounds) - label.frame.size.width / 2.0),
							 NSMaxY(bounds) - label.frame.size.height - 26.0,
							 label.frame.size.width, label.frame.size.height);

	// 窗口缩放时保持水平居中、并维持与顶部的间距
	label.autoresizingMask = NSViewMinXMargin | NSViewMaxXMargin | NSViewMinYMargin;

	[self.videoView addSubview:label];
	_toastView = label;

	_toastSeq++;
	seq = _toastSeq;

	dispatch_after(dispatch_time(DISPATCH_TIME_NOW, (int64_t)(2.2 * NSEC_PER_SEC)),
				   dispatch_get_main_queue(), ^
	{
		if (self->_toastSeq != seq)
			return;

		[NSAnimationContext runAnimationGroup:^(NSAnimationContext* context)
		{
			context.duration = 0.3;
			label.animator.alphaValue = 0.0;
		}
		completionHandler:^
		{
			if (self->_toastSeq == seq)
			{
				[label removeFromSuperview];
				self->_toastView = nil;
			}
		}];
	});
}

#pragma mark - 菜单校验与动态菜单

- (BOOL)validateMenuItem:(NSMenuItem*)item
{
	NSString*   group = item.representedObject;
	SEL         action = item.action;
	ines_int_t  status = [self currentStatus];
	BOOL        rom_loaded = (status != NES_STATUS_OFF);

	if ([group isEqualToString:APP_GROUP_SCALE])
	{
		item.state = (screen_scale == item.tag) ? NSControlStateValueOn : NSControlStateValueOff;
		return YES;
	}

	if ([group isEqualToString:APP_GROUP_ASPECT])
	{
		item.state = (_aspectMode == item.tag) ? NSControlStateValueOn : NSControlStateValueOff;
		return YES;
	}

	if ([group isEqualToString:APP_GROUP_VOLUME])
	{
		item.state = (_volume == item.tag) ? NSControlStateValueOn : NSControlStateValueOff;
		return YES;
	}

	if ([group isEqualToString:APP_GROUP_LOGLEVEL])
	{
		item.state = (ines_get_log_level() == (ines_log_level_t)item.tag) ? NSControlStateValueOn : NSControlStateValueOff;
		return YES;
	}

	if ([group isEqualToString:APP_GROUP_SAVESTATE])
		return rom_loaded;

	if ([group isEqualToString:APP_GROUP_LOADSTATE])
	{
		ines_char_t  title[INES_MAX_TITLE];
		ines_char_t  path[INES_MAX_PATH];

		if (!rom_loaded)
			return NO;

		[self copyCurrentRomTitle:title length:sizeof(title)];
		app_state_path(title, (ines_int_t)item.tag, path, sizeof(path));

		return (0 != app_state_file_time(path, [self currentRomCrc32]));
	}

	if (action == @selector(togglePause:))
	{
		item.state = ([self currentPause] == NES_STATUS_PAUSE) ? NSControlStateValueOn : NSControlStateValueOff;
		return rom_loaded;
	}

	if (action == @selector(toggleMute:))
	{
		item.state = _mute ? NSControlStateValueOn : NSControlStateValueOff;
		return YES;
	}

	if (action == @selector(toggleOsd:))
	{
		item.state = _osd ? NSControlStateValueOn : NSControlStateValueOff;
		return YES;
	}

	if (action == @selector(toggleCpuTrace:))
	{
		item.state = nes_cpu_trace_ops ? NSControlStateValueOn : NSControlStateValueOff;
		return YES;
	}

	if ((action == @selector(closeROM:))
	 || (action == @selector(hardReset:))
	 || (action == @selector(softReset:))
	 || (action == @selector(frameStep:))
	 || (action == @selector(takeSnapshot:)))
		return rom_loaded;

	return YES;
}

- (void)menuNeedsUpdate:(NSMenu*)menu
{
	if (menu == self.recentFilesMenu)
		[self updateRecentFilesMenu];
	else if ((menu == self.saveStateMenu) || (menu == self.loadStateMenu))
		[self updateStateMenu:menu label:(menu == self.saveStateMenu) ? @"存档" : @"读档"];
}

- (void)updateRecentFilesMenu
{
	ines_int_t  i;

	if (self.recentFilesMenu == nil)
		return;

	[self.recentFilesMenu removeAllItems];

	for (i = 0; i < APP_HISTORY_MAX; i++)
	{
		NSString*  title;

		if (latest_open_files[i][0] == 0)
			continue;

		title = [NSString stringWithUTF8String:latest_open_files[i]];
		if (title == nil)
			title = [NSString stringWithFormat:@"%s", latest_open_files[i]];

		[self addItemToMenu:self.recentFilesMenu
					  title:title
					 action:@selector(openRecentFile:)
				   keyEquiv:nil
				  modifiers:0
						tag:i
					  group:nil];
	}

	if (self.recentFilesMenu.numberOfItems == 0)
	{
		NSMenuItem*  item = [[NSMenuItem alloc] initWithTitle:@"（无）" action:nil keyEquivalent:@""];
		item.enabled = NO;
		[self.recentFilesMenu addItem:item];
	}
}

// 存档 / 读档菜单的标题随文件是否存在而变化; 是否可点由 validateMenuItem: 决定
- (void)updateStateMenu:(NSMenu*)menu label:(NSString*)label
{
	ines_int_t    i;
	ines_dword_t  crc32 = [self currentRomCrc32];
	ines_char_t   title[INES_MAX_TITLE];

	[self copyCurrentRomTitle:title length:sizeof(title)];

	for (i = 0; i < (ines_int_t)menu.numberOfItems; i++)
	{
		NSMenuItem*   item = [menu itemAtIndex:i];
		ines_char_t   path[INES_MAX_PATH];
		ines_dword_t  save_time;
		time_t        t;
		struct tm*    lt;

		app_state_path(title, i, path, sizeof(path));
		save_time = app_state_file_time(path, crc32);

		if (save_time == 0)
		{
			item.title = [NSString stringWithFormat:@"%@ %d (空)", label, (int)i];
			continue;
		}

		t  = (time_t)save_time;
		lt = localtime(&t);

		if (lt == NULL)
			item.title = [NSString stringWithFormat:@"%@ %d", label, (int)i];
		else
			item.title = [NSString stringWithFormat:@"%@ %d - %04d/%02d/%02d %02d:%02d:%02d", label, (int)i,
						  lt->tm_year + 1900, lt->tm_mon + 1, lt->tm_mday,
						  lt->tm_hour, lt->tm_min, lt->tm_sec];
	}
}

#pragma mark - 最近打开文件

- (void)loadHistories
{
	ines_int_t   i;
	ines_char_t  key[16];

	for (i = 0; i < APP_HISTORY_MAX; i++)
	{
		ines_snprintf(key, sizeof(key), ISTR("file%d"), (int)i);
		app_str_copy(latest_open_files[i], INES_MAX_PATH, GetConfigStr(ISTR("histories"), key, ISTR("")));
	}
}

- (void)saveHistories
{
	ines_int_t   i;
	ines_char_t  key[16];

	for (i = 0; i < APP_HISTORY_MAX; i++)
	{
		ines_snprintf(key, sizeof(key), ISTR("file%d"), (int)i);
		SetConfigStr(ISTR("histories"), key, latest_open_files[i]);
	}
}

- (void)addHistoryFile:(NSString*)path
{
	const char*  file = [path fileSystemRepresentation];
	ines_int_t   i;
	ines_int_t   found = -1;
	ines_int_t   last;

	if ((file == NULL) || (file[0] == 0))
		return;

	for (i = 0; i < APP_HISTORY_MAX; i++)
	{
		if (0 == ines_strcmp(latest_open_files[i], file))
		{
			found = i;
			break;
		}
	}

	last = (found >= 0) ? found : (APP_HISTORY_MAX - 1);

	// 把 [0, last) 整体后移一位(顺带丢弃 last 处的重复/最旧记录), 再把新文件放到最前面
	for (i = last; i > 0; i--)
		memmove(latest_open_files[i], latest_open_files[i - 1], INES_MAX_PATH);

	app_str_copy(latest_open_files[0], INES_MAX_PATH, file);

	[self saveHistories];
	[self updateRecentFilesMenu];
}

#pragma mark - 菜单动作

- (IBAction)openROM:(id)sender
{
	NSString*  last_dir = nil;
	NSString*  path;

	// 文件加载管理器: 先选文件夹, 再列出其中的 NES 文件及其属性, 最后加载选中的文件
	path = [iNESOpenRomDialog runModalWithInitialDir:[self romInitialDir]
											   owner:self.window
											 lastDir:&last_dir];

	// 记住对话框关闭时所在的文件夹(点"加载"或"取消"都算), 下次打开时默认定位到这里
	if (last_dir.length > 0)
		SetConfigStr(ISTR("rom"), ISTR("last_dir"), [last_dir fileSystemRepresentation]);

	if (path.length > 0)
		[self requestLoadROM:path];
}

// 载入对话框的初始目录: 上次使用的文件夹 -> 最近打开的 ROM 所在目录;
// 都没有时返回 nil, 由对话框自行退化为用户主目录
- (NSString*)romInitialDir
{
	NSString*    dir = nil;
	ines_cstr_t  config_dir;
	BOOL         is_dir = NO;

	// 1) 上次使用的文件夹(记录在 config.ini 里, 重启后依然有效);
	//    目录可能已被删除或改名, 不可用时忽略这条记录
	config_dir = GetConfigStr(ISTR("rom"), ISTR("last_dir"), ISTR(""));

	if ((config_dir != NULL) && (config_dir[0] != 0))
	{
		NSString*  saved = [NSString stringWithUTF8String:config_dir];

		if ((saved.length > 0)
		 && [[NSFileManager defaultManager] fileExistsAtPath:saved isDirectory:&is_dir]
		 && is_dir)
		{
			dir = saved;
		}
	}

	// 2) 退回到最近打开过的 ROM 所在目录(与 win32 的 szROMFilePath 语义一致)
	if ((dir == nil) && (latest_open_files[0][0] != 0))
	{
		NSString*  rom = [NSString stringWithUTF8String:latest_open_files[0]];

		if (rom.length > 0)
		{
			dir = [rom stringByDeletingLastPathComponent];

			if (dir.length == 0)
				dir = nil;
		}
	}

	INES_LOG(LOG_DBG, MOD_SYS, ISTR("open rom dialog initial dir: `%s`\n"),
		(dir != nil) ? [dir fileSystemRepresentation] : "");

	return dir;
}

// 只做"请求": 真正的载入由模拟线程完成
- (void)requestLoadROM:(NSString*)path
{
	if ((path == nil) || (path.length == 0))
		return;

	ines_mutex_lock(&s_mutex_ctl);
	app_str_copy(s_ctl.load_path, sizeof(s_ctl.load_path), [path fileSystemRepresentation]);
	s_ctl.load_request = 1;
	ines_mutex_unlock(&s_mutex_ctl);

	[self setPauseState:0];
}

- (IBAction)closeROM:(id)sender
{
	if ([self currentStatus] == NES_STATUS_OFF)
		return;

	ines_mutex_lock(&s_mutex_ctl);
	s_ctl.close_request = 1;
	ines_mutex_unlock(&s_mutex_ctl);
}

- (IBAction)hardReset:(id)sender
{
	if ([self currentStatus] == NES_STATUS_OFF)
		return;

	ines_mutex_lock(&s_mutex_ctl);
	s_ctl.hard_reset = 1;
	ines_mutex_unlock(&s_mutex_ctl);
}

- (IBAction)softReset:(id)sender
{
	if ([self currentStatus] == NES_STATUS_OFF)
		return;

	ines_mutex_lock(&s_mutex_ctl);
	s_ctl.soft_reset = 1;
	ines_mutex_unlock(&s_mutex_ctl);

	[self setPauseState:0];
}

- (IBAction)togglePause:(id)sender
{
	if ([self currentStatus] == NES_STATUS_OFF)
		return;

	if ([self currentPause] == NES_STATUS_PAUSE)
		[self setPauseState:0];
	else
		[self setPauseState:NES_STATUS_PAUSE];
}

- (IBAction)frameStep:(id)sender
{
	if ([self currentStatus] == NES_STATUS_OFF)
		return;

	[self setPauseState:NES_STATUS_FRAME_STEP];
}

// --------------------------------------------------------------------
// 无修饰键快捷键分发
// --------------------------------------------------------------------
// AppKit 只对带 Function 标志的按键(F1-F12/方向键等)做无修饰键的菜单
// keyEquivalent 匹配; 普通字符键("p"/空格)会直接走 keyDown 文本输入路径,
// 菜单项永远收不到(实测 F12 全屏可触发而 p/空格不行)。
// win32 侧靠 TranslateAccelerator 在消息分发前全局拦截(见 win32/iNES.rc
// 加速键表), 这里用本地事件监听器等价实现: 监听器先于窗口分发执行。
//   P    -> 暂停/继续(所有窗口)
//   空格 -> 单帧执行(寄存器视图除外: 其用空格做位切换/光标移动)
// 文本编辑中(文件面板的输入框等)不拦截, 避免吞掉正常输入。
- (void)installKeyEventMonitor
{
	__weak typeof(self)  weakSelf = self;

	self.keyEventMonitor = [NSEvent addLocalMonitorForEventsMatchingMask:NSEventMaskKeyDown
																 handler:^NSEvent*(NSEvent* event)
	{
		NSUInteger   mods;
		NSString*    chars;
		unichar      ch;
		NSWindow*    win;
		NSResponder* resp;

		mods = (event.modifierFlags & NSEventModifierFlagDeviceIndependentFlagsMask);
		if (mods != 0)                          // 仅响应无修饰键
			return event;

		chars = event.charactersIgnoringModifiers;
		if ((chars == nil) || (chars.length != 1))
			return event;
		ch = [chars characterAtIndex:0];

		win = event.window;
		if (win == nil)
			return event;

		// 文本编辑中(键盘焦点是字段编辑器)不拦截
		resp = win.firstResponder;
		if ([resp isKindOfClass:[NSTextView class]])
			return event;

		if ((ch == 'p') || (ch == 'P'))
		{
			[weakSelf togglePause:nil];
			INES_LOG(LOG_DBG, MOD_SYS, ISTR("hotkey P -> pause toggle\n"));
			return nil;
		}

		if (ch == ' ')
		{
			// 寄存器视图用空格编辑位/移动光标, 放行给视图自身处理
			if ([win.contentView isKindOfClass:[iNESRegisterView class]])
				return event;

			[weakSelf frameStep:nil];
			INES_LOG(LOG_DBG, MOD_SYS, ISTR("hotkey Space -> frame step\n"));
			return nil;
		}

		return event;
	}];
}

- (IBAction)toggleFullScreen:(id)sender
{
	[self.window toggleFullScreen:sender];
}

- (IBAction)setScale:(id)sender
{
	ines_int_t  scale = (ines_int_t)[sender tag];
	NSPoint     top_left;
	NSRect      new_frame;

	if ((scale != 100) && (scale != 200) && (scale != 300) && (scale != 400))
		return;

	if (scale == screen_scale)
		return;

	// 改变内容区大小会以左下角为锚点, 这里保持窗口左上角不动
	top_left = NSMakePoint(self.window.frame.origin.x,
						   self.window.frame.origin.y + self.window.frame.size.height);

	screen_scale = scale;
	self.videoView.scalePercent = scale;

	[self.window setContentSize:[self.videoView preferredContentSize]];

	new_frame         = self.window.frame;
	new_frame.origin  = NSMakePoint(top_left.x, top_left.y - new_frame.size.height);
	[self.window setFrame:new_frame display:YES];

	SetConfigInt(ISTR("display"), ISTR("scale"), screen_scale);
}

- (IBAction)setAspect:(id)sender
{
	_aspectMode = (ines_int_t)[sender tag];
	self.videoView.aspectMode = _aspectMode;
	[self.videoView setNeedsDisplay:YES];

	SetConfigInt(ISTR("display"), ISTR("aspect"), _aspectMode);
}

- (IBAction)toggleMute:(id)sender
{
	_mute = _mute ? 0 : 1;
	[self pushControlState];

	SetConfigInt(ISTR("audio"), ISTR("mute"), _mute);
}

- (IBAction)setVolume:(id)sender
{
	_volume = (ines_int_t)[sender tag];
	if (_volume < 0)
		_volume = 0;
	if (_volume > 100)
		_volume = 100;

	[self pushControlState];

	SetConfigInt(ISTR("audio"), ISTR("volume"), _volume);
}

- (IBAction)saveState:(id)sender
{
	ines_int_t   index = (ines_int_t)[sender tag];
	ines_char_t  title[INES_MAX_TITLE];
	ines_char_t  path[INES_MAX_PATH];

	if ([self currentStatus] == NES_STATUS_OFF)
		return;

	[self copyCurrentRomTitle:title length:sizeof(title)];
	app_state_path(title, index, path, sizeof(path));

	// 覆盖已有存档前先确认
	if (0 != app_state_file_time(path, [self currentRomCrc32]))
	{
		NSAlert*  alert = [[NSAlert alloc] init];

		alert.messageText     = @"覆盖存档";
		alert.informativeText = [NSString stringWithFormat:@"存档 %d 已存在，是否覆盖？", (int)index];
		[alert addButtonWithTitle:@"覆盖"];
		[alert addButtonWithTitle:@"取消"];

		if ([alert runModal] != NSAlertFirstButtonReturn)
			return;
	}

	ines_mutex_lock(&s_mutex_ctl);
	s_ctl.save_state = index;
	ines_mutex_unlock(&s_mutex_ctl);
}

- (IBAction)loadState:(id)sender
{
	ines_int_t  index = (ines_int_t)[sender tag];

	if ([self currentStatus] == NES_STATUS_OFF)
		return;

	ines_mutex_lock(&s_mutex_ctl);
	s_ctl.load_state = index;
	ines_mutex_unlock(&s_mutex_ctl);
}

- (IBAction)setLogLevel:(id)sender
{
	ines_set_log_level((ines_log_level_t)[sender tag]);
}

- (IBAction)toggleCpuTrace:(id)sender
{
	nes_cpu_trace_ops = nes_cpu_trace_ops ? 0 : 1;
}

- (IBAction)toggleOsd:(id)sender
{
	_osd = _osd ? 0 : 1;
	self.videoView.showOsd = (_osd != 0);

	[self pushControlState];
	SetConfigInt(ISTR("display"), ISTR("osd"), _osd);
}

- (IBAction)openRecentFile:(id)sender
{
	ines_int_t  index = (ines_int_t)[sender tag];

	if ((index < 0) || (index >= APP_HISTORY_MAX))
		return;

	if (latest_open_files[index][0] == 0)
		return;

	[self requestLoadROM:[NSString stringWithUTF8String:latest_open_files[index]]];
}

- (IBAction)takeSnapshot:(id)sender
{
	ines_byte_t   frame[SCREEN_IMAGE_BYTES];
	ines_char_t   rom_title[INES_MAX_TITLE];
	NSString*     title;
	NSString*     path;
	NSBitmapImageRep*  rep;
	NSData*       png;
	NSUInteger    i;

	if ([self currentStatus] == NES_STATUS_OFF)
		return;

	[self copyCurrentRomTitle:rom_title length:sizeof(rom_title)];

	// ROM 标题可能含路径分隔符, 直接拼进文件名会把文件写到别处
	title = [NSString stringWithUTF8String:rom_title];
	if (title == nil)
		title = @"";
	title = [title stringByReplacingOccurrencesOfString:@"/" withString:@"_"];
	title = [title stringByReplacingOccurrencesOfString:@":" withString:@"_"];

	{
		NSDateFormatter*  fmt = [[NSDateFormatter alloc] init];

		fmt.locale     = [NSLocale localeWithLocaleIdentifier:@"en_US_POSIX"];
		fmt.dateFormat = @"yyyyMMddHHmmss";

		path = [NSString stringWithFormat:@"%s/%@_snapshot_%@.png",
				s_picture_dir, title, [fmt stringFromDate:[NSDate date]]];
	}

	// 取一份画面快照, 其余工作都在主线程完成
	ines_mutex_lock(&s_mutex_output);
	memcpy(frame, screen_front, SCREEN_IMAGE_BYTES);
	ines_mutex_unlock(&s_mutex_output);

	// 索引色 -> 自顶向下 BGRA(与视频显示走同一套转换, 保证颜色一致)
	iNES_palette_to_bgra(frame, s_snapshot_bgra, SCREEN_WIDTH, SCREEN_HEIGHT);

	// NSBitmapImageRep 的像素字节序为 R,G,B,A, 把 BGRA 就地换成 RGBA
	for (i = 0; i < (NSUInteger)(SCREEN_WIDTH * SCREEN_HEIGHT); i++)
	{
		ines_byte_t  b = s_snapshot_bgra[i * 4];

		s_snapshot_bgra[i * 4]     = s_snapshot_bgra[i * 4 + 2];
		s_snapshot_bgra[i * 4 + 2] = b;
	}

	rep = [[NSBitmapImageRep alloc]
			initWithBitmapDataPlanes:NULL
						  pixelsWide:SCREEN_WIDTH
						  pixelsHigh:SCREEN_HEIGHT
					   bitsPerSample:8
					 samplesPerPixel:4
							hasAlpha:YES
							isPlanar:NO
					  colorSpaceName:NSDeviceRGBColorSpace
						 bytesPerRow:SCREEN_WIDTH * 4
						bitsPerPixel:32];
	if ((rep == nil) || (rep.bitmapData == NULL))
	{
		[self showAlert:@"截图失败" message:@"无法生成图像数据。"];
		return;
	}

	memcpy(rep.bitmapData, s_snapshot_bgra, sizeof(s_snapshot_bgra));

	png = [rep representationUsingType:NSBitmapImageFileTypePNG properties:@{}];
	if ((png == nil) || ![png writeToFile:path atomically:YES])
	{
		INES_LOG(LOG_ERR, MOD_SYS, ISTR("Save snapshot to '%s' Failed!\n"), path.UTF8String);
		[self showAlert:@"截图失败" message:[NSString stringWithFormat:@"无法写入截图文件:\n%@", path]];
		return;
	}

	INES_LOG(LOG_NTY, MOD_SYS, ISTR("Save snapshot to '%s' OK!\n"), path.UTF8String);
	[self showToast:@"截图已保存到 ~/Pictures/iNES"];
}

// 打开调试视图工具窗口(对应 win32 的 IDM_VIEW_* 菜单命令)
- (IBAction)showDebugView:(id)sender
{
	ines_int_t  view_id = (ines_int_t)[sender tag];

	if ((view_id < 0) || (view_id >= IDBG_VIEW_COUNT))
	{
		[self showUnimplemented:sender];
		return;
	}

	[[iNESDebugManager sharedManager] showView:view_id];
}

- (IBAction)showUnimplemented:(id)sender
{
	[self showAlert:@"选项" message:@"该功能尚未在当前版本中实现。"];
}

- (IBAction)showAbout:(id)sender
{
	[self showAlert:@"关于 iNES" message:@"iNES，1.0 版\nCopyright (C) 2015"];
}

// 模拟线程回调: ROM 载入结果(切回主线程处理界面)
- (void)romDidLoadOnMain:(NSString*)path
{
	if ((path != nil) && (path.length > 0))
		[self addHistoryFile:path];

	[self refreshTitle];
}

- (void)romDidFailOnMain:(NSString*)path
{
	[self refreshTitle];
	[self showAlert:@"载入 ROM 失败"
			message:[NSString stringWithFormat:@"无法载入文件：\n%@\n\n请确认它是有效的 NES ROM 且 mapper 受支持。", path]];
}


#pragma mark - 模拟线程内部实现

// 把 host 的状态发布给主线程
- (void)publishHostState
{
	ines_mutex_lock(&s_mutex_ctl);
	s_ctl.status    = host.status;
	s_ctl.rom_crc32 = (host.status != NES_STATUS_OFF) ? host.rom.crc32_p : 0;
	s_ctl.pause     = 0;
	app_str_copy(s_ctl.rom_title, sizeof(s_ctl.rom_title), szROMTitle);
	ines_mutex_unlock(&s_mutex_ctl);
}

- (void)clearScreenOnThread
{
	ines_mutex_lock(&s_mutex_output);
	memset(screen_front, 0, sizeof(screen_front));
	ines_mutex_unlock(&s_mutex_output);

	memset(s_screen_back, 0, sizeof(s_screen_back));

	[self.videoView clearScreen];
}

- (void)notifyRomLoaded:(ines_cstr_t)path
{
	[self performSelectorOnMainThread:@selector(romDidLoadOnMain:)
						   withObject:[NSString stringWithUTF8String:path]
						waitUntilDone:NO];
}

- (void)notifyRomLoadFailed:(ines_cstr_t)path
{
	[self performSelectorOnMainThread:@selector(romDidFailOnMain:)
						   withObject:[NSString stringWithUTF8String:path]
						waitUntilDone:NO];
}

- (void)loadROMOnThread:(ines_cstr_t)path
{
	// 先完整卸载上一个 ROM(会保存 SRAM)
	if (host.status != NES_STATUS_OFF)
		[self closeROMOnThread];

	app_str_copy(s_rom_file_path, sizeof(s_rom_file_path), path);

	get_file_title(szROMTitle, s_rom_file_path);
	if (szROMTitle[0] == 0)
		app_str_copy(szROMTitle, sizeof(szROMTitle), ISTR("ines"));

	app_save_path(szROMTitle, s_ram_file_path, sizeof(s_ram_file_path));

	if (ines_host_load_rom(&host, s_rom_file_path, s_ram_file_path))
	{
		ines_host_reset(&host);

		[self publishHostState];
		[self clearScreenOnThread];
		[self notifyRomLoaded:s_rom_file_path];

		INES_LOG(LOG_NTY, MOD_SYS, ISTR("Load ROM '%s' OK!\n"), s_rom_file_path);
		return;
	}

	INES_LOG(LOG_ERR, MOD_SYS, ISTR("Load ROM '%s' Failed!\n"), s_rom_file_path);

	// 清理失败现场, 回到"未运行"状态
	ines_host_free(&host);
	ines_host_init(&host, 1);

	s_rom_file_path[0] = 0;
	s_ram_file_path[0] = 0;
	szROMTitle[0]      = 0;

	[self publishHostState];
	[self clearScreenOnThread];
	[self notifyRomLoadFailed:path];
}

- (void)closeROMOnThread
{
	if (host.status == NES_STATUS_OFF)
		return;

	ines_host_save_sram(&host, s_ram_file_path);

	ines_host_free(&host);
	ines_host_init(&host, 1);

	s_rom_file_path[0] = 0;
	s_ram_file_path[0] = 0;
	szROMTitle[0]      = 0;

	[self publishHostState];
	[self clearScreenOnThread];
}

// 重新上电 = 卸载后重新加载(等价 win32 的 IDM_HARDRESET)
- (void)hardResetOnThread
{
	ines_char_t  rom_path[INES_MAX_PATH];

	if (s_rom_file_path[0] == 0)
		return;

	app_str_copy(rom_path, sizeof(rom_path), s_rom_file_path);

	[self closeROMOnThread];
	[self loadROMOnThread:rom_path];
}

- (void)saveStateOnThread:(ines_int_t)index
{
	ines_char_t  path[INES_MAX_PATH];
	FILE*        fp;

	if (host.status == NES_STATUS_OFF)
		return;

	app_state_path(szROMTitle, index, path, sizeof(path));

	fp = fopen(path, "wb");
	if (fp == NULL)
	{
		INES_LOG(LOG_ERR, MOD_SYS, ISTR("Save state to '%s' failed : can not open file!\n"), path);
		return;
	}

	if (0 != ines_host_save_state(&host, fp))
		INES_LOG(LOG_ERR, MOD_SYS, ISTR("Save state to '%s' failed!\n"), path);
	else
		INES_LOG(LOG_NTY, MOD_SYS, ISTR("Save state to '%s' OK!\n"), path);

	fclose(fp);
}

- (void)loadStateOnThread:(ines_int_t)index
{
	ines_char_t  path[INES_MAX_PATH];
	FILE*        fp;

	if (host.status == NES_STATUS_OFF)
		return;

	app_state_path(szROMTitle, index, path, sizeof(path));

	fp = fopen(path, "rb");
	if (fp == NULL)
	{
		INES_LOG(LOG_WAR, MOD_SYS, ISTR("Load state from '%s' failed : file not found!\n"), path);
		return;
	}

	if (0 != ines_host_load_state(&host, fp))
		INES_LOG(LOG_ERR, MOD_SYS, ISTR("Load state from '%s' failed!\n"), path);
	else
		INES_LOG(LOG_NTY, MOD_SYS, ISTR("Load state from '%s' OK!\n"), path);

	fclose(fp);
}

// 模拟线程主循环: 等价 win32 前端的 OnIdle(帧循环 + 输入)与 OnPaint
- (void)simulationLoop
{
	ines_int64_t  last_frame_time;
	ines_int64_t  last_fps_time;
	ines_int64_t  count_cpu_time;
	ines_int64_t  last_save_time;
	ines_int64_t  frame_us;
	ines_int64_t  cur_us;
	ines_int64_t  diff_us;
	ines_int_t    key_flash_count = 0;
	ines_byte_t   audio_buffer[INES_AUDIO_MAX_FRAME_LEN];

	// ---- 临时诊断(定位帧率下降/音频断续, 结论明确后整块移除) ----
	ines_int64_t  diag_sim_us      = 0;   // 本统计周期内 doframe 累计耗时
	ines_int64_t  diag_push_us     = 0;   // 本统计周期内音频投喂累计耗时
	ines_int64_t  diag_render_us   = 0;   // 本统计周期内画面提交累计耗时
	ines_int64_t  diag_ctrl_us     = 0;   // 本统计周期内帧率控制段(含补静音)累计耗时
	ines_int64_t  diag_refill_us   = 0;   // 本统计周期内补静音累计耗时
	ines_int_t    diag_refill_cnt  = 0;   // 本统计周期内补静音次数
	ines_int64_t  diag_playing_sum = 0;   // 本统计周期内在飞缓冲数累计
	ines_int_t    diag_frames      = 0;   // 本统计周期内帧数
	ines_int64_t  diag_fed_samples = 0;   // 本统计周期内投喂给音频的样本总数
	ines_int_t    diag_push_cnt    = 0;   // 本统计周期内实际投喂次数(out_len > 0)
	ines_int_t    diag_push_fail   = 0;   // 本统计周期内 pushFrame 返回 -1 的次数
	ines_int_t    diag_out_min     = 0;   // 本统计周期内单帧 out_len 最小值
	ines_int_t    diag_out_max     = 0;   // 本统计周期内单帧 out_len 最大值
	double        diag_played_prev = 0.0; // 上次统计时设备已渲染样本数
	// ---- 临时诊断结束 ----

	INES_LOG(LOG_NTY, MOD_SYS, ISTR("++ simulation thread running ++\n"));

	last_frame_time = app_cur_time_us();
	last_fps_time   = last_frame_time;
	last_save_time  = last_frame_time;
	count_cpu_time  = 0;

	[self.audio start];

	while (1)
	{
		ines_int_t   pause;
		ines_int_t   volume;
		ines_int_t   mute;
		ines_int_t   osd;
		ines_dword_t keys;
		ines_int_t   save_req;
		ines_int_t   load_req;
		ines_int_t   do_load;
		ines_int_t   do_close;
		ines_int_t   do_hard;
		ines_int_t   do_soft;
		ines_int_t   do_stop = 0;

		@autoreleasepool
		{
			// ---- 1) 取请求与实时状态 ----
			ines_mutex_lock(&s_mutex_ctl);

			if (s_ctl.stop)
			{
				ines_mutex_unlock(&s_mutex_ctl);
				break;
			}

			do_load  = s_ctl.load_request;   s_ctl.load_request  = 0;
			do_close = s_ctl.close_request;  s_ctl.close_request = 0;
			do_hard  = s_ctl.hard_reset;     s_ctl.hard_reset    = 0;
			do_soft  = s_ctl.soft_reset;     s_ctl.soft_reset    = 0;
			save_req = s_ctl.save_state;     s_ctl.save_state    = APP_STATE_NONE;
			load_req = s_ctl.load_state;     s_ctl.load_state    = APP_STATE_NONE;

			pause  = s_ctl.pause;
			volume = s_ctl.volume;
			mute   = s_ctl.mute;
			osd    = s_ctl.osd;
			keys   = s_ctl.keys;

			if (do_load)
				app_str_copy(s_load_path, sizeof(s_load_path), s_ctl.load_path);

			ines_mutex_unlock(&s_mutex_ctl);

			// ---- 2) 执行请求(host 只在本线程被访问) ----
			if (do_close)
				[self closeROMOnThread];

			if (do_load)
				[self loadROMOnThread:s_load_path];

			if (do_hard)
				[self hardResetOnThread];

			if (do_soft && (host.status != NES_STATUS_OFF))
			{
				ines_host_reset(&host);
				[self publishHostState];
			}

			if (save_req != APP_STATE_NONE)
				[self saveStateOnThread:save_req];

			if (load_req != APP_STATE_NONE)
				[self loadStateOnThread:load_req];

			// ---- 3) 空闲处理 ----
			if (host.status == NES_STATUS_OFF)
			{
				app_sleep_ms(10);
				continue;
			}

			if (pause == NES_STATUS_PAUSE)
			{
				app_sleep_ms(10);
				continue;
			}

			// ---- 4) 帧率控制(等价 win32 的 dblFrameTimeAdj) ----
			{
				ines_int64_t  t_ctrl = app_cur_time_us();   // ---- 临时诊断 ----
				ines_int64_t  t_refill;                     // ---- 临时诊断 ----

				frame_us = (ines_int64_t)(host.setting.frame_rate * 1000000.0);

				// 在飞缓冲少于目标值时略微加快, 反之略微放慢, 让音频始终有数据可播
				{
					ines_int_t  playing = [self.audio playingCount];

					if (playing < APP_AUDIO_LOW_WATER)
					{
						// 水位低于下限: 用静音把缓冲补回目标深度(对应 win32 在 wvPlayingNum == 0
						// 时填充静音缓冲的做法), 少了它播放会一直停滞, 声音会长时间断续甚至消失。
						// 门限必须高于 0 而不能等到"一个缓冲都不剩": 本判断在帧首执行, 而真正的
						// 断流判定在 pushFrame 里、位于一帧的模拟与等待之后(约 16.7ms = 恰好一个
						// 缓冲的播放时长)。等到帧首看到 0 时, 投喂时必然已是 0, 于是队列会长期钉在
						// 1 个缓冲上(实测 underrun ≈ 帧数) 且再也涨不回目标深度 —— 表现为持续无声。
						t_refill = app_cur_time_us();                       // ---- 临时诊断 ----
						playing  = [self.audio refillSilence:APP_AUDIO_CACHE_NUM];
						diag_refill_us += (app_cur_time_us() - t_refill);   // ---- 临时诊断 ----
						diag_refill_cnt++;                                  // ---- 临时诊断 ----
					}

					diag_playing_sum += playing;                            // ---- 临时诊断 ----

					if (playing < APP_AUDIO_CACHE_NUM)
						frame_us -= 1000;
					else if (playing > APP_AUDIO_CACHE_NUM)
						frame_us += 1000;
				}

				diag_ctrl_us += (app_cur_time_us() - t_ctrl);               // ---- 临时诊断 ----
			}

			while (!do_stop)
			{
				ines_mutex_lock(&s_mutex_ctl);
				do_stop = s_ctl.stop;
				ines_mutex_unlock(&s_mutex_ctl);

				if (do_stop)
					break;

				cur_us  = app_cur_time_us();
				diff_us = cur_us - last_frame_time;
				if (diff_us >= frame_us)
					break;

				app_sleep_us(((frame_us - diff_us) > 1000) ? 1000 : 0);
			}

			if (do_stop)
				break;

			// 本帧起点: 既用于下一帧的节拍, 也用于统计本帧的 CPU 消耗
			last_frame_time = app_cur_time_us();

			// ---- 5) 输入(连发按键按帧相位交替, 等价 win32 的 key_flash_count) ----
			if (++key_flash_count >= APP_KEY_FLASH_FREQ)
				key_flash_count = 0;

			{
				ines_int_t  main_keys = 0;

				if (keys & IKEY_A)      main_keys |= JOYPAD_KEY_A;
				if (keys & IKEY_B)      main_keys |= JOYPAD_KEY_B;
				if (keys & IKEY_SELECT) main_keys |= JOYPAD_KEY_SELECT;
				if (keys & IKEY_START)  main_keys |= JOYPAD_KEY_START;
				if (keys & IKEY_UP)     main_keys |= JOYPAD_KEY_UP;
				if (keys & IKEY_DOWN)   main_keys |= JOYPAD_KEY_DOWN;
				if (keys & IKEY_LEFT)   main_keys |= JOYPAD_KEY_LEFT;
				if (keys & IKEY_RIGHT)  main_keys |= JOYPAD_KEY_RIGHT;

				if (key_flash_count < (APP_KEY_FLASH_FREQ / 2))
				{
					if (keys & IKEY_TURBO_A) main_keys |= JOYPAD_KEY_A;
					if (keys & IKEY_TURBO_B) main_keys |= JOYPAD_KEY_B;
				}

				ines_joypad_update_bits(&host.joypad, main_keys, 0);
			}

			// ---- 6) 跑一帧前: 应用调试视图提交的内存写入 ----
			ines_dbg_apply_writes(&host);

			// ---- 6) 跑一帧(APU 输出 -> PPU 渲染 -> 提交画面) ----
			{
				ines_int64_t  t_mark = app_cur_time_us();   // ---- 临时诊断 ----

				ines_apu_setoutbuffer(&host.apu, audio_buffer, (ines_dword_t)sizeof(audio_buffer), (mute ? 0 : volume));
				ines_host_doframe(&host, s_screen_back);

				diag_sim_us += (app_cur_time_us() - t_mark);   // ---- 临时诊断 ----
				t_mark       = app_cur_time_us();              // ---- 临时诊断 ----

				{
					ines_dword_t  out_len = ines_apu_getoutlen(&host.apu);

					if (out_len > 0)
					{
						ines_int_t  rc = [self.audio pushFrame:audio_buffer length:(ines_int_t)out_len];

						// ---- 临时诊断: 记录单帧样本数分布与投喂结果 ----
						if (diag_push_cnt == 0)
							diag_out_min = diag_out_max = (ines_int_t)out_len;
						else if ((ines_int_t)out_len < diag_out_min)
							diag_out_min = (ines_int_t)out_len;
						else if ((ines_int_t)out_len > diag_out_max)
							diag_out_max = (ines_int_t)out_len;
						diag_fed_samples += (ines_int64_t)out_len;
						diag_push_cnt++;
						if (rc < 0)
							diag_push_fail++;
						// ---- 临时诊断结束 ----
					}
				}

				diag_push_us += (app_cur_time_us() - t_mark);   // ---- 临时诊断 ----
				t_mark        = app_cur_time_us();              // ---- 临时诊断 ----

				if (osd)
				{
					ines_char_t  text[32];

					ines_snprintf(text, sizeof(text), ISTR("CPU: %03d%%"), (int)nes_cpu_rate);
					DrawTextToBitmap(s_screen_back, SCREEN_WIDTH, SCREEN_HEIGHT, text, 8, 8, 32);
				}

				ines_mutex_lock(&s_mutex_output);
				memcpy(screen_front, s_screen_back, SCREEN_IMAGE_BYTES);
				ines_mutex_unlock(&s_mutex_output);

				[self.videoView presentIndexedPixels:s_screen_back];

				// 帧末: 为调试视图采集一份 host 快照(无调试窗口可见时直接返回, 无拷贝开销)
				ines_dbg_capture(&host);

				diag_render_us += (app_cur_time_us() - t_mark);   // ---- 临时诊断 ----
			}

			// ---- 7) 统计与周期性落盘 ----
			cur_us = app_cur_time_us();
			count_cpu_time += (cur_us - last_frame_time);
			diag_frames++;   // ---- 临时诊断 ----

			if ((cur_us - last_fps_time) >= 1000000)
			{
				ines_int64_t  period = cur_us - last_fps_time;

				nes_cpu_rate = (ines_int_t)(1000 * count_cpu_time / period) / 10;
				if (nes_cpu_rate > 100)
					nes_cpu_rate = 100;
				if (nes_cpu_rate < 0)
					nes_cpu_rate = 0;

				// ---- 临时诊断: 每秒输出帧率与帧内耗时分段 ----
				{
					ines_int_t  n = (diag_frames > 0) ? diag_frames : 1;

					INES_LOG(LOG_NTY, MOD_SYS,
							 ISTR("perf: fps=%.1f cpu=%d%% frame=%.2fms busy=%.2fms sim=%.2fms push=%.2fms render=%.2fms ")
							 ISTR("ctrl=%.2fms refill=%d/%.2fms playing=%.2f\n"),
							 (double)diag_frames * 1000000.0 / (double)period, (int)nes_cpu_rate,
							 (double)period / 1000.0 / (double)n,
							 (double)count_cpu_time / 1000.0 / (double)n,
							 (double)diag_sim_us / 1000.0 / (double)n,
							 (double)diag_push_us / 1000.0 / (double)n,
							 (double)diag_render_us / 1000.0 / (double)n,
							 (double)diag_ctrl_us / 1000.0 / (double)n,
							 (int)diag_refill_cnt,
							 (double)diag_refill_us / 1000.0 / (double)((diag_refill_cnt > 0) ? diag_refill_cnt : 1),
							 (double)diag_playing_sum / (double)n);
				}

				// ---- 临时诊断: 音频供需对账(APU 每帧产出 vs 设备实播) ----
				{
					double  played_now = [self.audio playedSamples];
					double  fed_s      = (double)diag_fed_samples * 1000000.0 / (double)period;
					double  play_s     = (played_now - diag_played_prev) * 1000000.0 / (double)period;

					INES_LOG(LOG_NTY, MOD_SYS,
							 ISTR("audio feed: fed=%.0f/s n=%d out=%d..%d avg=%.1f pushfail=%d play=%.0f/s need=44100/s surplus=%+.0f/s\n"),
							 fed_s, (int)diag_push_cnt, (int)diag_out_min, (int)diag_out_max,
							 (double)diag_fed_samples / (double)((diag_push_cnt > 0) ? diag_push_cnt : 1),
							 (int)diag_push_fail, play_s, fed_s - play_s);

					diag_played_prev = played_now;
					diag_fed_samples = 0;
					diag_push_cnt    = 0;
					diag_push_fail   = 0;
					diag_out_min     = 0;
					diag_out_max     = 0;
				}
				// ---- 临时诊断结束 ----

				diag_frames      = 0;
				diag_sim_us      = 0;
				diag_push_us     = 0;
				diag_render_us   = 0;
				diag_ctrl_us     = 0;
				diag_refill_us   = 0;
				diag_refill_cnt  = 0;
				diag_playing_sum = 0;
				// ---- 临时诊断结束 ----

				last_fps_time  = cur_us;
				count_cpu_time = 0;
			}

			if ((cur_us - last_save_time) > APP_SRAM_SAVE_INTERVAL)
			{
				last_save_time = cur_us;
				ines_host_save_sram(&host, s_ram_file_path);
			}

			// ---- 8) 单帧执行后自动转为暂停 ----
			if (pause == NES_STATUS_FRAME_STEP)
				[self setPauseState:NES_STATUS_PAUSE];
		}
	}

	// 退出前保存 SRAM 并关闭音频设备(音频必须在模拟线程上停止)
	if (host.status != NES_STATUS_OFF)
		ines_host_save_sram(&host, s_ram_file_path);

	[self.audio stop];

	INES_LOG(LOG_NTY, MOD_SYS, ISTR("-- simulation thread stopped --\n"));
}

#pragma mark - iNESVideoViewDelegate

- (void)videoViewKeyStateDidChange:(iNESVideoView*)view
{
	ines_mutex_lock(&s_mutex_ctl);
	s_ctl.keys = [view pressedKeys];
	ines_mutex_unlock(&s_mutex_ctl);
}

- (void)videoView:(iNESVideoView*)view didDropFilePaths:(NSArray<NSString*>*)paths
{
	NSString*  path = paths.firstObject;

	if ((path == nil) || (path.length == 0))
		return;

	if (![[path.pathExtension lowercaseString] isEqualToString:@"nes"])
		return;

	[self requestLoadROM:path];
}

#pragma mark - NSWindowDelegate

- (void)windowDidMove:(NSNotification*)notification
{
	// 窗口位置以 Cocoa 坐标(左下角为原点)持久化
	SetConfigInt(ISTR("display"), ISTR("x"), (ines_int_t)self.window.frame.origin.x);
	SetConfigInt(ISTR("display"), ISTR("y"), (ines_int_t)self.window.frame.origin.y);
}

- (void)windowDidResignKey:(NSNotification*)notification
{
	// 失焦时清空按键, 避免游戏里出现"卡键"
	[self.videoView resetKeys];
	[self videoViewKeyStateDidChange:self.videoView];
}

#pragma mark - NSApplicationDelegate

- (void)applicationDidFinishLaunching:(NSNotification*)notification
{
	[self startup];
	[self installKeyEventMonitor];
	[NSApp activateIgnoringOtherApps:YES];
}

- (BOOL)applicationShouldTerminateAfterLastWindowClosed:(NSApplication*)sender
{
	return YES;
}

- (void)applicationWillTerminate:(NSNotification*)notification
{
	[self shutdown];
}

- (BOOL)application:(NSApplication*)sender openFile:(NSString*)filename
{
	// 从 Finder 双击 ROM 时可能在 startup 之前收到该回调
	if (self.window == nil)
		[self setPendingRomPath:filename];
	else
		[self requestLoadROM:filename];

	return YES;
}

@end


// 模拟线程入口
static int nes_proc(void* ud)
{
	@autoreleasepool
	{
		iNESAppController*  controller = (__bridge iNESAppController*)ud;

		[controller simulationLoop];
	}

	return 0;
}

