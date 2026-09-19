// =====================================================================
// iNES macOS 前端 —— "网络对战"对话框(实现)
//
// 状态机完全由 comm/npsession 提供(win32 与 macOS 共用), 本文件只负责界面与
// 50ms 轮询, 与 win32/dlgNetPlay.c 的 WM_TIMER 驱动方式一致(模拟线程不受影响)。
// =====================================================================

#import "iNESNetPlayDialog.h"

#include "../comm/npsession.h"


#define NPDLG_CONTENT_W   340.0
#define NPDLG_CONTENT_H   176.0

// 与 win32 的 SetTimer(hDlg, 100, 50, NULL) 一致
#define NPDLG_TIMER_SEC   0.05

#define NPDLG_IP_DEFAULT   @"127.0.0.1"
#define NPDLG_PORT_DEFAULT @"8891"

// 缓冲帧数下拉项(与 win32 的 IDC_CMB_CACHE 一致)
#define NPDLG_CACHE_MIN    1
#define NPDLG_CACHE_MAX    5


@interface iNESNetPlayDialog () <NSWindowDelegate>
{
	NSButton*       _radioServer;
	NSButton*       _radioClient;
	NSTextField*    _ipField;
	NSTextField*    _portField;
	NSPopUpButton*  _cachePopup;
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

	window.title              = @"网络对战";
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
	box = [[NSBox alloc] initWithFrame:NSMakeRect(18, 118, 304, 46)];
	box.title         = @"运行为";
	box.titlePosition = NSAboveTop;
	box.titleFont     = [NSFont systemFontOfSize:13.0];
	box.contentViewMargins = NSMakeSize(0, 0);
	[root addSubview:box];

	_radioServer       = npdlg_make_radio(@"服务器", NSMakeRect(24, 124, 80, 18));
	_radioClient       = npdlg_make_radio(@"客户机", NSMakeRect(140, 124, 80, 18));
	_radioServer.state = NSControlStateValueOn;
	_radioClient.state = NSControlStateValueOff;

	_radioServer.target = self;
	_radioServer.action = @selector(onRoleChanged:);
	_radioClient.target = self;
	_radioClient.action = @selector(onRoleChanged:);

	[root addSubview:_radioServer];
	[root addSubview:_radioClient];

	// ---- 地址 / 端口 ----
	[root addSubview:npdlg_make_label(@"地址", NSMakeRect(20, 92, 36, 17))];
	_ipField = npdlg_make_edit(NSMakeRect(58, 89, 118, 22));
	_ipField.stringValue = NPDLG_IP_DEFAULT;
	[root addSubview:_ipField];

	[root addSubview:npdlg_make_label(@"端口", NSMakeRect(192, 92, 30, 17))];
	_portField = npdlg_make_edit(NSMakeRect(224, 89, 62, 22));
	_portField.stringValue = NPDLG_PORT_DEFAULT;
	[root addSubview:_portField];

	// ---- 缓冲帧数 ----
	[root addSubview:npdlg_make_label(@"缓冲帧数", NSMakeRect(20, 62, 62, 17))];
	_cachePopup = [[NSPopUpButton alloc] initWithFrame:NSMakeRect(84, 59, 70, 22) pullsDown:NO];
	{
		int  n;

		for (n = NPDLG_CACHE_MIN; n <= NPDLG_CACHE_MAX; n++)
			[_cachePopup addItemWithTitle:[NSString stringWithFormat:@"%d", n]];

		// 默认 4(与 win32 的 CB_SETCURSEL 3 一致)
		[_cachePopup selectItemAtIndex:(NP_CACHE_DEFAULT - NPDLG_CACHE_MIN)];
	}
	[root addSubview:_cachePopup];

	// ---- 提示信息 ----
	_infoLabel           = npdlg_make_label(@"", NSMakeRect(18, 32, 304, 17));
	_infoLabel.textColor = [NSColor secondaryLabelColor];
	[root addSubview:_infoLabel];

	// ---- 开始 / 取消 ----
	_startButton         = npdlg_make_button(@"开始", NSMakeRect(172, 12, 74, 28), YES);
	_startButton.target  = self;
	_startButton.action  = @selector(onStart:);
	[root addSubview:_startButton];

	{
		NSButton*  cancel = npdlg_make_button(@"取消", NSMakeRect(252, 12, 74, 28), NO);

		cancel.target = self;
		cancel.action = @selector(onCancel:);
		[root addSubview:cancel];
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

/** 与 win32 一致: 地址仅客户机可用, 缓冲帧数仅服务器可设。 */
- (IBAction)onRoleChanged:(id)sender
{
	BOOL  isServer = (_radioServer.state == NSControlStateValueOn);

	_ipField.enabled    = isServer ? NO : YES;
	_cachePopup.enabled = isServer ? YES : NO;
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
		_failMsg = @"端口无效，请输入 1 ~ 65535 之间的端口号。";
		[self setInfo:@"端口无效"];
		[NSApp stopModalWithCode:NSModalResponseCancel];
		return;
	}

	isServer = (_radioServer.state == NSControlStateValueOn);
	cacheNum = (int)(_cachePopup.indexOfSelectedItem + NPDLG_CACHE_MIN);

	if (0 != np_begin(isServer ? 1 : 0, [_ipField.stringValue UTF8String], port, _crc32, cacheNum))
	{
		_failMsg = [NSString stringWithUTF8String:net_get_last_error()];
		[self setInfo:_failMsg];
		[NSApp stopModalWithCode:NSModalResponseCancel];
		return;
	}

	_connecting         = YES;
	_startButton.enabled = NO;

	[self setInfo:isServer ? @"等待客户端的连接..." : @"正在连接到服务器..."];
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
		_failMsg    = [NSString stringWithUTF8String:((msg[0] != 0) ? msg : "连接失败")];

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

		alert.messageText     = @"网络对战";
		alert.informativeText = dialog->_failMsg;
		[alert addButtonWithTitle:@"确定"];
		[alert runModal];

		dialog->_failMsg = nil;
	}

	return dialog->_connected;
}


@end
