// =====================================================================
// iNES macOS 前端 —— 局域网快速配对面板(实现)
//
// 状态推进完全交给 comm/npsession(np_begin / np_poll, 与 win32 共用), 本文件只负责:
//   1) 每 500ms 刷新房间列表, 每 1s 广播一次自己的房间;
//   2) 服务端被动等待接入(nb_poll 的 server 分支会自动 accept 并握手);
//   3) 用户双击房间 -> 停广播 -> np_begin(client) 接入。
// =====================================================================

#import "iNESLanLobby.h"
#import "iNESConfig.h"
#import "iNESi18n.h"
#import "iNESUiLayout.h"

#include "../comm/i18n.h"
#include "../comm/npsession.h"

#include "../comm/net.h"
#include "../comm/log.h"

#include <time.h>
#include <string.h>


#define LOBBY_W            460.0
#define LOBBY_H            330.0

// 列表刷新节奏(与广播的 1s 解耦, 保证 3s TTL 内有 6 次机会收到新 beacon)
#define LOBBY_TIMER_SEC    0.5
#define LOBBY_ADV_SEC      1

// 默认对战端口: 被占用时自动改用系统分配端口(与"网络对战…"对话框的默认值一致)
#define LOBBY_PORT         8891

// 昵称 / 缓冲帧数在 config.ini 中的位置
#define LOBBY_CFG_SECTION  ISTR("netplay")
#define LOBBY_CFG_NICKKEY  ISTR("nickname")
#define LOBBY_CFG_CACHEKEY ISTR("cache_num")


@interface iNESLanLobby () <NSWindowDelegate, NSTableViewDataSource, NSTableViewDelegate>
{
	NSTextField*   _nickField;
	NSTextField*   _romLabel;
	NSTableView*   _table;
	NSTextField*   _infoLabel;
	NSButton*      _joinButton;
	NSTimer*       _timer;

	ines_byte_t    _peerId[16];
	ines_dword_t   _crc32;
	NSString*      _romName;
	int            _cacheNum;          // 本机作为房主时的缓冲帧数(config.ini, 发布后固定)

	lan_room_t     _rooms[LAN_ROOM_MAX];
	int            _roomCount;
	time_t         _lastAdv;
	int            _listenPort;

	BOOL           _connected;
	BOOL           _joining;
	NSString*      _failMsg;
}
@end


@implementation iNESLanLobby


#pragma mark - 工具

/** 默认昵称: Player + 4 位随机数字。 */
+ (NSString*)makeDefaultNick
{
	return [NSString stringWithFormat:@"Player %04d",
			(int)(arc4random_uniform(9000) + 1000)];
}

/**
 * 本机作为房主时的缓冲帧数: 取 config.ini 的 [netplay] cache_num,
 * 未配置(或越界)时用 NP_CACHE_DEFAULT。加入方不用这个数 —— 以房主下发的为准。
 */
+ (int)hostCacheNum
{
	int  n = (int)GetConfigInt(LOBBY_CFG_SECTION, LOBBY_CFG_CACHEKEY, NP_CACHE_DEFAULT);

	if (n < NP_CACHE_MIN)
		n = NP_CACHE_MIN;

	if (n > NP_CACHE_MAX)
		n = NP_CACHE_MAX;

	return n;
}

/**
 * 按字节数截断 UTF-8 字符串, 且不切断多字节序列(否则对端会显示乱码)。
 */
static NSString* lobby_clip_utf8(NSString* text, NSUInteger maxBytes)
{
	NSData*       data;
	NSUInteger    len;

	if (text == nil)
		return @"";

	data = [text dataUsingEncoding:NSUTF8StringEncoding];

	if (data == nil)
		return @"";

	len = data.length;

	if (len <= maxBytes)
		return text;

	len = maxBytes;

	{
		const uint8_t*  bytes = (const uint8_t*)data.bytes;

		// 末尾若是续字节(0x80~0xBF)或正好是首字节, 一律回退
		while ((len > 0) && (bytes[len - 1] >= 0x80) && (bytes[len - 1] < 0xC0))
			len--;

		if ((len > 0) && (bytes[len - 1] >= 0xC0))
			len--;   // 首字节被截断
	}

	return [[NSString alloc] initWithData:[data subdataWithRange:NSMakeRange(0, len)]
								 encoding:NSUTF8StringEncoding];
}

/** 取当前界面上的昵称(去掉首尾空白, 空则回退默认值)。 */
- (NSString*)currentNick
{
	NSString*  text = _nickField.stringValue;

	if (text == nil)
		return @"";

	text = [text stringByTrimmingCharactersInSet:[NSCharacterSet whitespaceCharacterSet]];

	return (text.length > 0) ? text : [iNESLanLobby makeDefaultNick];
}

/** 昵称写入 config.ini(持久化, 下次进入自动带上)。 */
- (void)saveNick
{
	NSString*  nick = [self currentNick];

	SetConfigStr(LOBBY_CFG_SECTION, LOBBY_CFG_NICKKEY, [nick UTF8String]);
}


#pragma mark - 控件构造

static NSTextField* lobby_make_label(NSString* title, NSRect frame)
{
	NSTextField*  field = [[NSTextField alloc] initWithFrame:frame];

	field.stringValue     = title;
	field.bezeled         = NO;
	field.editable        = NO;
	field.selectable      = NO;
	field.drawsBackground = NO;
	field.font            = [NSFont systemFontOfSize:13.0];

	((NSTextFieldCell*)field.cell).wraps      = NO;
	((NSTextFieldCell*)field.cell).scrollable = YES;

	return field;
}

static NSButton* lobby_make_button(NSString* title, NSRect frame, BOOL isDefault)
{
	NSButton*  button = [[NSButton alloc] initWithFrame:frame];

	button.title      = title;
	button.bezelStyle = NSBezelStyleRounded;
	button.font       = [NSFont systemFontOfSize:13.0];

	if (isDefault)
		button.keyEquivalent = @"\r";

	return button;
}


#pragma mark - 生命周期

- (instancetype)initWithCrc32:(ines_dword_t)crc32 romName:(NSString*)romName
{
	NSWindow*   window;
	NSUInteger  style;

	style = NSWindowStyleMaskTitled | NSWindowStyleMaskClosable;

	window = [[NSWindow alloc] initWithContentRect:NSMakeRect(0, 0, LOBBY_W, LOBBY_H)
										 styleMask:style
										   backing:NSBackingStoreBuffered
											 defer:NO];

	self = [super initWithWindow:window];
	if (self == nil)
		return nil;

	_crc32     = crc32;
	_romName   = (romName != nil) ? romName : @"";
	_cacheNum  = [iNESLanLobby hostCacheNum];
	_roomCount = 0;
	_lastAdv   = 0;
	_connected = NO;
	_joining   = NO;
	_failMsg   = nil;

	lan_gen_peer_id(_peerId);

	window.title              = L10N("dialog.lan.title");
	window.delegate           = self;
	window.releasedWhenClosed = NO;

	[self buildControls];

	return self;
}

- (void)buildControls
{
	NSView*       root = self.window.contentView;
	NSScrollView* scroll;
	NSString*     nick;

	// ---- 昵称 ----
	NSTextField*  nickLabel = lobby_make_label(L10N("dialog.lan.nickname"), NSMakeRect(18, 292, 36, 17));

	[root addSubview:nickLabel];

	nick = [NSString stringWithUTF8String:GetConfigStr(LOBBY_CFG_SECTION, LOBBY_CFG_NICKKEY, ISTR(""))];

	if (nick.length == 0)
	{
		nick = [iNESLanLobby makeDefaultNick];
		SetConfigStr(LOBBY_CFG_SECTION, LOBBY_CFG_NICKKEY, [nick UTF8String]);
	}

	_nickField                = [[NSTextField alloc] initWithFrame:NSMakeRect(58, 289, 170, 22)];
	_nickField.bezeled        = YES;
	_nickField.editable       = YES;
	_nickField.font           = [NSFont systemFontOfSize:13.0];
	_nickField.stringValue    = nick;
	[root addSubview:_nickField];

	INESFitLabel(nickLabel, @[ _nickField ], 6.0);

	// ---- 本机 ROM ----
	_romLabel           = lobby_make_label(@"", NSMakeRect(240, 292, 202, 17));
	_romLabel.stringValue = L10NF("dialog.lan.rom_prefix_format", lobby_clip_utf8(_romName, 24).UTF8String);
	_romLabel.textColor = [NSColor secondaryLabelColor];
	[root addSubview:_romLabel];

	// ---- 说明 ----
	{
		NSTextField*  hint = lobby_make_label(L10NF("dialog.lan.hint_format", (int)_cacheNum),
											  NSMakeRect(18, 264, 424, 17));

		hint.textColor = [NSColor secondaryLabelColor];
		hint.font      = [NSFont systemFontOfSize:11.0];
		[root addSubview:hint];
	}

	// ---- 房间列表 ----
	scroll = [[NSScrollView alloc] initWithFrame:NSMakeRect(18, 62, 424, 196)];
	scroll.borderType         = NSBezelBorder;
	scroll.hasVerticalScroller = YES;

	_table                = [[NSTableView alloc] initWithFrame:NSMakeRect(0, 0, 408, 196)];
	_table.rowHeight      = 18.0;
	_table.dataSource     = self;
	_table.delegate       = self;
	_table.doubleAction   = @selector(onJoin:);
	_table.target         = self;

	{
		NSTableColumn*  c1 = [[NSTableColumn alloc] initWithIdentifier:@"nick"];
		NSTableColumn*  c2 = [[NSTableColumn alloc] initWithIdentifier:@"rom"];
		NSTableColumn*  c3 = [[NSTableColumn alloc] initWithIdentifier:@"ver"];
		NSTableColumn*  c4 = [[NSTableColumn alloc] initWithIdentifier:@"cache"];

		// 表头: 术语(ROM)不翻译; 列宽按译文测量, 但不小于设计宽度
		c1.title = L10N("dialog.lan.col_nick");
		c1.width = MAX(110.0, INESTextWidth(c1.title, [NSFont systemFontOfSize:11.0]) + 14.0);
		c2.title = L10N("dialog.lan.col_rom");
		c2.width = MAX(176.0, INESTextWidth(c2.title, [NSFont systemFontOfSize:11.0]) + 14.0);
		c3.title = L10N("dialog.lan.col_ver");
		c3.width = MAX(68.0,  INESTextWidth(c3.title, [NSFont systemFontOfSize:11.0]) + 14.0);
		c4.title = L10N("dialog.lan.col_cache");
		c4.width = MAX(56.0,  INESTextWidth(c4.title, [NSFont systemFontOfSize:11.0]) + 14.0);

		[_table addTableColumn:c1];
		[_table addTableColumn:c2];
		[_table addTableColumn:c3];
		[_table addTableColumn:c4];
	}

	scroll.documentView = _table;
	[root addSubview:scroll];

	// ---- 提示 ----
	_infoLabel           = lobby_make_label(@"", NSMakeRect(18, 36, 424, 17));
	_infoLabel.textColor = [NSColor secondaryLabelColor];
	[root addSubview:_infoLabel];

	// ---- 按钮 ----
	_joinButton         = lobby_make_button(L10N("dialog.lan.join"), NSMakeRect(288, 12, 74, 28), YES);
	_joinButton.target  = self;
	_joinButton.action  = @selector(onJoin:);
	_joinButton.enabled = NO;
	[root addSubview:_joinButton];

	{
		NSButton*  cancel = lobby_make_button(L10N("dialog.lan.close"), NSMakeRect(368, 12, 74, 28), NO);

		cancel.target = self;
		cancel.action = @selector(onCancel:);
		[root addSubview:cancel];

		// 按钮按译文长度自适应: 必要时加宽窗口, 从右往左摆放
		INESFitButtons(self.window, @[ _joinButton, cancel ], 14.0, 8.0, 74.0);
	}
}

- (void)dealloc
{
	[self stopTimer];
}


#pragma mark - 发布 / 接入

/** 以服务端身份开始发布: 开发现通道 + 监听(端口被占用则交系统分配)。 */
- (BOOL)startHosting
{
	int  rc;

	if (0 != lan_open(_peerId))
	{
		_failMsg = L10N("dialog.lan.err_open_failed");
		return NO;
	}

	rc = np_begin(1, NULL, LOBBY_PORT, _crc32, _cacheNum);

	if (rc != 0)
		rc = np_begin(1, NULL, 0, _crc32, _cacheNum);   // 8891 被占用

	if (rc != 0)
	{
		lan_close();
		_failMsg = [NSString stringWithUTF8String:net_get_last_error()];
		return NO;
	}

	_listenPort = (int)net_get_local_port();
	_roomCount  = 0;
	_lastAdv    = 0;
	_joining    = NO;

	INES_LOG(LOG_NTY, MOD_NET, ISTR("lan: hosting at tcp %d\n"), _listenPort);

	return YES;
}

/** 收尾: 关发现通道; 未配对成功时同时结束会话。 */
- (void)stopAll
{
	lan_close();

	if (!_connected)
		np_end();

	_roomCount = 0;
}

/** 握手失败/取消加入后恢复发布状态(服务端仍在等待其他人)。 */
- (void)resumeHosting
{
	np_end();
	lan_close();

	if (![self startHosting])
	{
		[self stopTimer];
		[self setInfo:(_failMsg != nil) ? _failMsg : L10N("dialog.lan.err_resume_failed")];
		return;
	}

	_joinButton.enabled = YES;
	[self refreshInfo];
}

- (void)setInfo:(NSString*)text
{
	_infoLabel.stringValue = (text != nil) ? text : @"";
}

/** 空闲状态下的提示文本。 */
- (void)refreshInfo
{
	[self setInfo:(_roomCount > 0)
	 ? L10NF("dialog.lan.rooms_format", (int)_roomCount)
	 : L10N("dialog.lan.no_rooms")];
}


#pragma mark - 动作

- (IBAction)onJoin:(id)sender
{
	NSInteger    row = _table.selectedRow;
	lan_room_t   room;
	ines_char_t  ip[64];

	if ((row < 0) || (row >= _roomCount))
		return;

	if (_rooms[row].crc32 != _crc32)
	{
		[self setInfo:L10N("dialog.lan.err_rom_diff")];
		return;
	}

	// 版本不同: 列表里已灰显, 这里再拦一次(双击可能被拖选/键盘触发)
	if (_rooms[row].net_ver != (ines_dword_t)NET_VER)
	{
		[self setInfo:L10N("dialog.lan.err_ver_diff")];
		return;
	}

	// 取最新的房间信息(可能刚好超时消失)
	if (0 != lan_find(_rooms[row].peer_id, &room))
	{
		[self setInfo:L10N("dialog.lan.err_room_gone")];
		return;
	}

	if (0 != lan_addr_str(room.addr, ip, (int)sizeof(ip)))
	{
		[self setInfo:L10N("dialog.lan.err_addr_invalid")];
		return;
	}

	[self saveNick];

	// 放弃发布: 停广播(随后 np_begin(client) 会 net_close() 关掉监听, 因此
	// 本机不可能再被别人接入 —— "后加入者必为客户机")
	lan_close();
	_roomCount = 0;
	[_table reloadData];

	if (0 != np_begin(0, ip, (int)room.tcp_port, _crc32, 0))
	{
		_failMsg = [NSString stringWithUTF8String:net_get_last_error()];
		[self setInfo:_failMsg];
		[self resumeHosting];
		return;
	}

	_joining           = YES;
	_joinButton.enabled = NO;

	[self setInfo:L10NF("dialog.lan.joining_format", room.nick)];
}

- (IBAction)onCancel:(id)sender
{
	[self stopTimer];
	[self stopAll];

	[NSApp stopModalWithCode:NSModalResponseCancel];
}


#pragma mark - 定时器

- (void)startTimer
{
	[self stopTimer];

	// 与"网络对战"对话框同理: 模态循环下必须挂到 CommonModes
	_timer = [NSTimer timerWithTimeInterval:LOBBY_TIMER_SEC
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

	// 1) 推进握手: 服务端有人连入会自动 accept 并校验; 客户端连上即发校验包
	msg[0] = 0;
	rc     = np_poll(msg, (ines_size_t)sizeof(msg));

	// 2) 发现: 仅发布状态下广播与刷新列表
	if (!_joining)
	{
		time_t  now = time(NULL);

		if ((now - _lastAdv) >= LOBBY_ADV_SEC)
		{
			lan_advertise(_crc32, (ines_word_t)_listenPort, 0,
						  [[self currentNick] UTF8String],
						  [lobby_clip_utf8(_romName, LAN_ROM_MAX) UTF8String],
						  (ines_byte_t)_cacheNum);

			_lastAdv = now;
		}

		_roomCount = lan_poll(_rooms, LAN_ROOM_MAX);
		[_table reloadData];
	}

	// 3) 结果处理
	if (rc == NP_POLL_OK)
	{
		[self stopTimer];

		_connected = YES;
		_joining   = NO;

		// 开打后不再广播(房间随即从别人的列表里消失)
		//
		// 注意: 这里不能关闭监听 socket —— comm/net.c 的 net_is_server() 以"监听
		// socket 是否存在"为判据, 而 comm/npsession 的 np_frame_input() 正是
		// 用它决定主/副手柄路由; 一旦关掉, 服务端会按客户机取值(手柄反转)。
		// 监听 socket 统一由 np_end() 关闭; 且已有连接时 net_is_connected() 不会
		// 再 accept, 因此保留它对对战没有任何影响。
		lan_close();

		INES_LOG(LOG_NTY, MOD_NET, ISTR("lan: paired, run as %s\n"),
				 np_is_server() ? ISTR("server") : ISTR("client"));

		[NSApp stopModalWithCode:NSModalResponseOK];
		return;
	}

	if (rc == NP_POLL_FAILED)
	{
		NSString*  reason = [NSString stringWithUTF8String:
							 ((msg[0] != 0) ? msg : ines_i18n_text("dialog.lan.connect_failed"))];

		_failMsg = reason;

		// 服务端继续等待其他人; 客户端退回发布状态
		[self resumeHosting];
		[self setInfo:L10NF("dialog.lan.restore_format", reason.UTF8String)];
		return;
	}

	if (_joining)
		[self setInfo:[NSString stringWithUTF8String:
					   ((msg[0] != 0) ? msg : ines_i18n_text("dialog.lan.connecting"))]];
	else
		[self refreshInfo];
}


#pragma mark - NSTableView

- (NSInteger)numberOfRowsInTableView:(NSTableView*)tableView
{
	return _roomCount;
}

- (id)tableView:(NSTableView*)tableView objectValueForTableColumn:(NSTableColumn*)column row:(NSInteger)row
{
	if ((row < 0) || (row >= _roomCount))
		return nil;

	if ([column.identifier isEqualToString:@"nick"])
		return [NSString stringWithUTF8String:_rooms[row].nick];

	// 协议版本: 与本机不同则标注出来(选中被 shouldSelectRow 拦掉)
	if ([column.identifier isEqualToString:@"ver"])
	{
		if (_rooms[row].net_ver == (ines_dword_t)NET_VER)
			return [NSString stringWithFormat:@"%u", (unsigned)NET_VER];

		// 0 = 旧版 beacon 未携带该字段
		return (_rooms[row].net_ver != 0)
			 ? L10NF("dialog.lan.ver_upgrade_format", (unsigned)_rooms[row].net_ver)
			 : L10N("dialog.lan.ver_old");
	}

	// 缓冲帧数由房主发布时确定(对端未携带该字段时显示 --)
	if ([column.identifier isEqualToString:@"cache"])
	{
		return ((_rooms[row].cache_num >= NP_CACHE_MIN) && (_rooms[row].cache_num <= NP_CACHE_MAX))
			 ? L10NF("dialog.lan.cache_frames_format", (int)_rooms[row].cache_num)
			 : @"--";
	}

	{
		NSString*  name = [NSString stringWithUTF8String:_rooms[row].rom];

		// ROM 不同的房间: 可见、但标注出来(选中被 shouldSelectRow 拦掉)
		if (_rooms[row].crc32 != _crc32)
			name = [name stringByAppendingString:L10N("dialog.lan.rom_diff_suffix")];

		return name;
	}
}

/**
 * 该房间能否加入: ROM 相同 **且** 协议版本相同。
 * 版本不同的房间一律不可选 —— 连上也会被握手拒绝, 不如在列表里就拦住。
 */
- (BOOL)rowJoinable:(NSInteger)row
{
	if ((row < 0) || (row >= _roomCount))
		return NO;

	return ((_rooms[row].crc32 == _crc32)
		 && (_rooms[row].net_ver == (ines_dword_t)NET_VER)) ? YES : NO;
}

/** 不可加入的房间(ROM 不同 / 版本不同)灰字显示。 */
- (void)tableView:(NSTableView*)tableView
  willDisplayCell:(id)cell
   forTableColumn:(NSTableColumn*)column
			  row:(NSInteger)row
{
	if ((row < 0) || (row >= _roomCount))
		return;

	if (![cell respondsToSelector:@selector(setTextColor:)])
		return;

	[cell setTextColor:[self rowJoinable:row]
		 ? [NSColor labelColor] : [NSColor tertiaryLabelColor]];
}

/** 只有 ROM 相同且版本相同的房间可选(需求: 不可连的房间可见但不可选)。 */
- (BOOL)tableView:(NSTableView*)tableView shouldSelectRow:(NSInteger)row
{
	BOOL  ok = [self rowJoinable:row];

	_joinButton.enabled = ok;

	if (!ok && (row >= 0) && (row < _roomCount))
	{
		[self setInfo:(_rooms[row].net_ver != (ines_dword_t)NET_VER)
			 ? L10N("dialog.lan.err_ver_diff")
			 : L10N("dialog.lan.err_rom_diff")];
	}

	return ok;
}


#pragma mark - NSWindowDelegate

- (void)windowWillClose:(NSNotification*)notification
{
	[self stopTimer];
	[self stopAll];

	[NSApp stopModalWithCode:NSModalResponseCancel];
}


#pragma mark - 对外入口

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

+ (BOOL)runModalWithCrc32:(ines_dword_t)crc32 romName:(NSString*)romName owner:(NSWindow*)owner
{
	iNESLanLobby*  lobby = [[iNESLanLobby alloc] initWithCrc32:crc32 romName:romName];

	if (lobby == nil)
		return NO;

	if (![lobby startHosting])
	{
		NSString*  msg = (lobby->_failMsg != nil) ? lobby->_failMsg : L10N("dialog.lan.err_generic");

		[lobby stopAll];

		{
			NSAlert*  alert = [[NSAlert alloc] init];

			alert.messageText     = L10N("dialog.lan.title");
			alert.informativeText = msg;
			[alert addButtonWithTitle:L10N("msg.ok")];
			[alert runModal];
		}

		return NO;
	}

	[lobby placeWindowRelativeTo:owner];
	[lobby startTimer];
	[lobby refreshInfo];

	[NSApp runModalForWindow:lobby.window];

	// 收尾: 先摘 delegate, 避免释放窗口时再走一次关闭流程
	lobby.window.delegate = nil;
	[lobby stopTimer];
	[lobby stopAll];
	[lobby saveNick];
	[lobby.window orderOut:nil];
	[lobby.window close];

	if ((lobby->_failMsg != nil) && !lobby->_connected)
	{
		NSAlert*  alert = [[NSAlert alloc] init];

		alert.messageText     = L10N("dialog.lan.title");
		alert.informativeText = lobby->_failMsg;
		[alert addButtonWithTitle:L10N("msg.ok")];
		[alert runModal];

		lobby->_failMsg = nil;
	}

	return lobby->_connected;
}


@end
