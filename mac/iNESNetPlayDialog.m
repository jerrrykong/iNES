// =====================================================================
// iNES macOS 前端 —— "网络对战"对话框(实现)
//
// 状态机完全由 comm/npsession 提供(win32 与 macOS 共用), 本文件只负责界面与
// 50ms 轮询, 与 win32/dlgNetPlay.c 的 WM_TIMER 驱动方式一致(模拟线程不受影响)。
// =====================================================================

#import "iNESNetPlayDialog.h"
#import "iNESConfig.h"
#import "iNESi18n.h"
#import "iNESUiLayout.h"

#include "../comm/i18n.h"
#include "../comm/npsession.h"


#define NPDLG_CONTENT_W   340.0
#define NPDLG_CONTENT_H   146.0

// 与 win32 的 SetTimer(hDlg, 100, 50, NULL) 一致
#define NPDLG_TIMER_SEC   0.05

#define NPDLG_IP_DEFAULT   @"127.0.0.1"
#define NPDLG_PORT_DEFAULT @"8891"

// 缓冲帧数不在这里选择: 取 config.ini 的 [netplay] cache_num(未配置则用默认),
// 后续在"设置"里提供修改入口 —— 放在对战对话框底部容易被误读成"加入时才选的参数"。
// 客户端该参数无效: 一律以服务端下发的值为准(两端必须一致)。
#define NPDLG_CFG_SECTION   ISTR("netplay")
#define NPDLG_CFG_CACHEKEY  ISTR("cache_num")


@interface iNESNetPlayDialog () <NSWindowDelegate>
{
	NSButton*       _radioServer;
	NSButton*       _radioClient;
	NSTextField*    _ipField;
	NSTextField*    _portField;
	NSTextField*    _infoLabel;
	NSButton*       _startButton;
	NSTimer*        _timer;

	ines_dword_t    _crc32;
	BOOL            _connected;
	BOOL            _connecting;
	NSString*       _failMsg;
}
@end


@implementation iNESNetPlayDialog


#pragma mark - 控件构造

/** 构造不可编辑的标签(等价 win32 的 LTEXT)。 */
static NSTextField* npdlg_make_label(NSString* title, NSRect frame)
{
	NSTextField*  field = [[NSTextField alloc] initWithFrame:frame];

	field.stringValue     = title;
	field.bezeled         = NO;
	field.editable        = NO;
	field.selectable      = NO;
	field.drawsBackground = NO;
	field.font            = [NSFont systemFontOfSize:13.0];

	// 竖直方向靠上对齐
	((NSTextFieldCell*)field.cell).wraps     = NO;
	((NSTextFieldCell*)field.cell).scrollable = YES;

	return field;
}

/** 构造单行输入框(等价 win32 的 EDITTEXT)。 */
static NSTextField* npdlg_make_edit(NSRect frame)
{
	NSTextField*  field = [[NSTextField alloc] initWithFrame:frame];

	field.bezeled  = YES;
	field.editable = YES;
	field.font     = [NSFont systemFontOfSize:13.0];

	return field;
}

/** 构造单选按钮(等价 win32 的 BS_AUTORADIOBUTTON)。 */
static NSButton* npdlg_make_radio(NSString* title, NSRect frame)
{
	NSButton*  button = [[NSButton alloc] initWithFrame:frame];

	button.title       = title;
	button.buttonType  = NSButtonTypeRadio;
	button.bezelStyle  = NSBezelStyleRounded;
	button.font        = [NSFont systemFontOfSize:13.0];
	[button setButtonType:NSButtonTypeRadio];

	return button;
}

/** 构造普通按钮(等价 win32 的 PUSHBUTTON)。 */
static NSButton* npdlg_make_button(NSString* title, NSRect frame, BOOL isDefault)
{
	NSButton*  button = [[NSButton alloc] initWithFrame:frame];

	button.title      = title;
	button.bezelStyle = NSBezelStyleRounded;
	button.font       = [NSFont systemFontOfSize:13.0];

	if (isDefault)
	{
		button.keyEquivalent = @"\r";
		button.bezelStyle    = NSBezelStyleRounded;
	}

	return button;
}


#pragma mark - 生命周期

- (instancetype)initWithCrc32:(ines_dword_t)crc32
{
	NSWindow*  window;
	NSUInteger style;

	style = NSWindowStyleMaskTitled | NSWindowStyleMaskClosable;

	window = [[NSWindow alloc] initWithContentRect:NSMakeRect(0, 0, NPDLG_CONTENT_W, NPDLG_CONTENT_H)
										 styleMask:style
										   backing:NSBackingStoreBuffered
											 defer:NO];

	self = [super initWithWindow:window];
	if (self == nil)
		return nil;

	_crc32      = crc32;
	_connected  = NO;
	_connecting = NO;
	_failMsg    = nil;

	window.title              = L10N("dialog.netplay.title");
	window.delegate           = self;
	window.releasedWhenClosed = NO;

	[self buildControls];

	return self;
}

- (void)buildControls
{
	NSView*  root = self.window.contentView;
	NSBox*   box;

	// ---- 运行为 ----
	box = [[NSBox alloc] initWithFrame:NSMakeRect(18, 88, 304, 46)];
	box.title         = L10N("dialog.netplay.run_as");
	box.titlePosition = NSAboveTop;
	box.titleFont     = [NSFont systemFontOfSize:13.0];
	box.contentViewMargins = NSMakeSize(0, 0);
	[root addSubview:box];

	_radioServer       = npdlg_make_radio(L10N("dialog.netplay.server"), NSMakeRect(24, 94, 80, 18));
	_radioClient       = npdlg_make_radio(L10N("dialog.netplay.client"), NSMakeRect(140, 94, 80, 18));
	_radioServer.state = NSControlStateValueOn;
	_radioClient.state = NSControlStateValueOff;

	_radioServer.target = self;
	_radioServer.action = @selector(onRoleChanged:);
	_radioClient.target = self;
	_radioClient.action = @selector(onRoleChanged:);

	[root addSubview:_radioServer];
	[root addSubview:_radioClient];

	// ---- 地址 / 端口 ----
	{
		NSTextField*  addr_label = npdlg_make_label(L10N("dialog.netplay.address"), NSMakeRect(20, 62, 36, 17));
		NSTextField*  port_label = npdlg_make_label(L10N("dialog.netplay.port"),    NSMakeRect(192, 62, 30, 17));

		[root addSubview:addr_label];
		[root addSubview:port_label];

		_ipField = npdlg_make_edit(NSMakeRect(58, 59, 118, 22));
		_ipField.stringValue = NPDLG_IP_DEFAULT;
		[root addSubview:_ipField];

		_portField = npdlg_make_edit(NSMakeRect(224, 59, 62, 22));
		_portField.stringValue = NPDLG_PORT_DEFAULT;
		[root addSubview:_portField];

		// 标签按译文长度自适应, 同一行后续控件整体右移(避免重叠)
		INESFitLabel(addr_label, @[ _ipField ], 6.0);
		INESFitLabel(port_label, @[ _portField ], 6.0);
	}

	// ---- 提示信息 ----
	_infoLabel           = npdlg_make_label(@"", NSMakeRect(18, 32, 304, 17));
	_infoLabel.textColor = [NSColor secondaryLabelColor];
	[root addSubview:_infoLabel];

	// ---- 开始 / 取消 ----
	_startButton         = npdlg_make_button(L10N("dialog.netplay.start"), NSMakeRect(172, 12, 74, 28), YES);
	_startButton.target  = self;
	_startButton.action  = @selector(onStart:);
	[root addSubview:_startButton];

	{
		NSButton*  cancel = npdlg_make_button(L10N("dialog.netplay.cancel"), NSMakeRect(252, 12, 74, 28), NO);

		cancel.target = self;
		cancel.action = @selector(onCancel:);
		[root addSubview:cancel];

		// 按钮按译文长度自适应: 必要时加宽窗口, 从右往左摆放
		INESFitButtons(self.window, @[ cancel, _startButton ], 14.0, 8.0, 74.0);
	}

	[self onRoleChanged:nil];
}

- (void)dealloc
{
	[self stopTimer];
}


#pragma mark - 界面同步

- (void)setInfo:(NSString*)text
{
	_infoLabel.stringValue = (text != nil) ? text : @"";
}

/** 与 win32 一致: 地址仅客户机可用(缓冲帧数已改为固定默认值, 不再有控件)。 */
- (IBAction)onRoleChanged:(id)sender
{
	BOOL  isServer = (_radioServer.state == NSControlStateValueOn);

	_ipField.enabled = isServer ? NO : YES;
}


#pragma mark - 动作

- (IBAction)onStart:(id)sender
{
	BOOL   isServer;
	int    port;
	int    cacheNum;

	port = [_portField.stringValue intValue];
	if ((port <= 0) || (port > 65535))
	{
		// win32: MessageBox("error input port")
		_failMsg = L10N("dialog.netplay.err_port");
		[self setInfo:L10N("dialog.netplay.err_port_short")];
		[NSApp stopModalWithCode:NSModalResponseCancel];
		return;
	}

	isServer = (_radioServer.state == NSControlStateValueOn);

	// 缓冲帧数取本机配置; 客户端传多少都无效(以服务端下发的为准), 服务端用它发布房间
	cacheNum = (int)GetConfigInt(NPDLG_CFG_SECTION, NPDLG_CFG_CACHEKEY, NP_CACHE_DEFAULT);

	if (cacheNum < NP_CACHE_MIN)
		cacheNum = NP_CACHE_MIN;

	if (cacheNum > NP_CACHE_MAX)
		cacheNum = NP_CACHE_MAX;

	if (0 != np_begin(isServer ? 1 : 0, [_ipField.stringValue UTF8String], port, _crc32, cacheNum))
	{
		_failMsg = [NSString stringWithUTF8String:net_get_last_error()];
		[self setInfo:_failMsg];
		[NSApp stopModalWithCode:NSModalResponseCancel];
		return;
	}

	_connecting         = YES;
	_startButton.enabled = NO;

	[self setInfo:isServer ? L10N("dialog.netplay.waiting") : L10N("dialog.netplay.connecting")];
	[self startTimer];
}

/**
 * 取消: 连接中时只中止本次尝试(留在对话框内, 可改参数重试),
 * 否则直接关闭 —— 与 win32 的 IDCANCEL 分支一致。
 */
- (IBAction)onCancel:(id)sender
{
	[self stopTimer];

	if (_connecting)
	{
		np_end();

		_connecting          = NO;
		_startButton.enabled = YES;

		[self setInfo:@""];
		return;
	}

	[NSApp stopModalWithCode:NSModalResponseCancel];
}


#pragma mark - 握手轮询

- (void)startTimer
{
	[self stopTimer];

	// 模态循环运行在 NSModalPanelRunLoopMode 下, 定时器必须挂到 CommonModes 才会触发
	_timer = [NSTimer timerWithTimeInterval:NPDLG_TIMER_SEC
									 target:self
								   selector:@selector(onTimer:)
								   userInfo:nil
									repeats:YES];

	[[NSRunLoop mainRunLoop] addTimer:_timer forMode:NSRunLoopCommonModes];
}

- (void)stopTimer
{
	if (_timer != nil)
	{
		[_timer invalidate];
		_timer = nil;
	}
}

- (void)onTimer:(NSTimer*)timer
{
	ines_char_t  msg[256];
	int          rc;

	msg[0] = 0;
	rc     = np_poll(msg, (ines_size_t)sizeof(msg));

	[self setInfo:[NSString stringWithUTF8String:((msg[0] != 0) ? msg : "")]];

	if (rc == NP_POLL_OK)
	{
		[self stopTimer];

		_connected  = YES;
		_connecting = NO;

		[NSApp stopModalWithCode:NSModalResponseOK];
	}
	else if (rc == NP_POLL_FAILED)
	{
		[self stopTimer];

		_connected  = NO;
		_connecting = NO;
		_failMsg    = [NSString stringWithUTF8String:((msg[0] != 0) ? msg : ines_i18n_text("dialog.netplay.connect_failed"))];

		[NSApp stopModalWithCode:NSModalResponseCancel];
	}
}


#pragma mark - NSWindowDelegate

- (void)windowWillClose:(NSNotification*)notification
{
	[self stopTimer];

	if (_connecting)
	{
		np_end();
		_connecting = NO;
	}

	[NSApp stopModalWithCode:NSModalResponseCancel];
}


#pragma mark - 对外入口

/** 把窗口摆到所有者窗口正中(无所有者时居中于屏幕可视区)。 */
- (void)placeWindowRelativeTo:(NSWindow*)owner
{
	NSWindow*  window = self.window;
	NSRect     frame  = window.frame;
	NSRect     base   = frame;

	if (owner != nil)
		base = owner.frame;
	else if (NSScreen.mainScreen != nil)
		base = NSScreen.mainScreen.visibleFrame;

	frame.origin.x = base.origin.x + (base.size.width  - frame.size.width)  / 2.0;
	frame.origin.y = base.origin.y + (base.size.height - frame.size.height) / 2.0;

	[window setFrameOrigin:frame.origin];
}

+ (BOOL)runModalWithCrc32:(ines_dword_t)crc32 owner:(NSWindow*)owner
{
	iNESNetPlayDialog*  dialog = [[iNESNetPlayDialog alloc] initWithCrc32:crc32];

	if (dialog == nil)
		return NO;

	[dialog placeWindowRelativeTo:owner];

	[NSApp runModalForWindow:dialog.window];

	// 收尾: 先摘掉 delegate, 避免释放窗口时再走一次关闭流程
	dialog.window.delegate = nil;
	[dialog stopTimer];
	[dialog.window orderOut:nil];
	[dialog.window close];

	// 失败原因在模态结束后再弹(避免嵌套模态)
	if (dialog->_failMsg != nil)
	{
		NSAlert*  alert = [[NSAlert alloc] init];

		alert.messageText     = L10N("dialog.netplay.title");
		alert.informativeText = dialog->_failMsg;
		[alert addButtonWithTitle:L10N("msg.ok")];
		[alert runModal];

		dialog->_failMsg = nil;
	}

	return dialog->_connected;
}


@end
