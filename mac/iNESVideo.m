// 画面视图: 8bit 索引色 -> BGRA -> CGImage, 最近邻整数缩放绘制。
//
// 线程模型: 模拟线程调用 presentIndexedPixels:, 主线程调用 drawRect:,
//           两者通过 _lock 互斥访问位图数据。

#import "iNESVideo.h"

#import <time.h>

#import "../comm/log.h"

#import "iNESPalette.h"


// macOS 上 CGBitmapContext 的经典 BGRA 布局:
// kCGBitmapByteOrder32Little + kCGImageAlphaNoneSkipFirst => 内存字节序为 B,G,R,X
#define INES_BITMAP_FLAGS   (kCGBitmapByteOrder32Little | kCGImageAlphaNoneSkipFirst)


@implementation iNESVideoView
{
	NSLock*        _lock;
	ines_byte_t*   _bitmapData;      // SCREEN_WIDTH * SCREEN_HEIGHT * 4 (BGRA)
	CGContextRef   _bitmapContext;
	BOOL           _hasImage;        // 是否已经提交过至少一帧
	ines_dword_t   _pressedKeys;     // 按下的逻辑按键
	BOOL           _shiftDown;

	// ---- 临时诊断: 画面提交 -> 主线程绘制完成 ----
	ines_int64_t   _presentStamp;
	ines_int64_t   _drawSum;
	ines_int64_t   _drawMax;
	ines_int_t     _drawCnt;
}

@synthesize scalePercent = _scalePercent;
@synthesize aspectMode   = _aspectMode;
@synthesize showOsd      = _showOsd;
@synthesize delegate     = _delegate;

- (instancetype)initWithFrame:(NSRect)frameRect
{
	self = [super initWithFrame:frameRect];
	if(self == nil)
		return nil;

	_lock         = [[NSLock alloc] init];
	_hasImage     = NO;
	_pressedKeys  = 0;
	_shiftDown    = NO;
	_scalePercent = 200;
	_aspectMode   = INES_ASPECT_ORIGINAL;
	_showOsd      = NO;

	_bitmapData = (ines_byte_t*)calloc((ines_size_t)SCREEN_WIDTH * SCREEN_HEIGHT * 4, 1);
	if(_bitmapData == NULL)
	{
		INES_LOG(LOG_ERR, MOD_SYS, ISTR("video: allocate bitmap buffer Failed!\n"));
		return self;
	}

	CGColorSpaceRef colorSpace = CGColorSpaceCreateDeviceRGB();
	_bitmapContext = CGBitmapContextCreate(_bitmapData,
										   SCREEN_WIDTH, SCREEN_HEIGHT,
										   8, SCREEN_WIDTH * 4, colorSpace,
										   INES_BITMAP_FLAGS);
	CGColorSpaceRelease(colorSpace);

	if(_bitmapContext == NULL)
		INES_LOG(LOG_ERR, MOD_SYS, ISTR("video: create bitmap context Failed!\n"));

	// 支持把 ROM 拖进窗口
	[self registerForDraggedTypes:@[NSFilenamesPboardType]];

	return self;
}

- (void)dealloc
{
	if(_bitmapContext != NULL)
		CGContextRelease(_bitmapContext);
	free(_bitmapData);
}

- (BOOL)isOpaque
{
	return YES;
}

- (BOOL)acceptsFirstResponder
{
	return YES;
}

- (BOOL)becomeFirstResponder
{
	return YES;
}

- (NSSize)preferredContentSize
{
	ines_int_t  scale = _scalePercent;

	if(scale < 100)
		scale = 100;

	return NSMakeSize(SCREEN_WIDTH  * scale / 100.0,
					  SCREEN_HEIGHT * scale / 100.0);
}


#pragma mark - 画面提交

- (void)presentIndexedPixels:(const ines_byte_t*)pPixels
{
	if(pPixels == NULL || _bitmapContext == NULL)
		return;

	[_lock lock];
	iNES_palette_to_bgra(pPixels, _bitmapData, SCREEN_WIDTH, SCREEN_HEIGHT);
	_hasImage = YES;
	[_lock unlock];

	// ---- 临时诊断: 记录提交时刻 ----
	{
		struct timespec  ts;

		clock_gettime(CLOCK_MONOTONIC, &ts);
		_presentStamp = (ines_int64_t)ts.tv_sec * 1000000 + (ines_int64_t)(ts.tv_nsec / 1000);
	}

	// 由主线程负责重绘
	dispatch_async(dispatch_get_main_queue(), ^{
		[self setNeedsDisplay:YES];
	});
}

- (void)clearScreen
{
	[_lock lock];
	_hasImage = NO;
	[_lock unlock];

	dispatch_async(dispatch_get_main_queue(), ^{
		[self setNeedsDisplay:YES];
	});
}


#pragma mark - 绘制

// 计算画面在视图内的目标矩形(等比缩放并居中, 坐标为整数以避免糊边)
- (CGRect)imageRectForBounds:(NSRect)bounds
{
	double  aspect = (double)SCREEN_WIDTH / (double)SCREEN_HEIGHT;
	double  width, height, x, y;

	if(_aspectMode == INES_ASPECT_4_3)
		aspect = 4.0 / 3.0;
	else if(_aspectMode == INES_ASPECT_16_9)
		aspect = 16.0 / 9.0;

	width  = bounds.size.width;
	height = bounds.size.height;

	if(width / height > aspect)
		width = height * aspect;
	else
		height = width / aspect;

	width  = floor(width);
	height = floor(height);
	x = floor(bounds.origin.x + (bounds.size.width  - width)  / 2.0);
	y = floor(bounds.origin.y + (bounds.size.height - height) / 2.0);

	return CGRectMake(x, y, width, height);
}

- (void)drawRect:(NSRect)dirtyRect
{
	CGContextRef  context = [[NSGraphicsContext currentContext] CGContext];
	NSRect        bounds  = [self bounds];
	CGImageRef    image   = NULL;

	// 背景(无画面时可见)
	CGContextSetRGBFillColor(context, 0.09, 0.09, 0.11, 1.0);
	CGContextFillRect(context, NSRectToCGRect(bounds));

	[_lock lock];
	if(_hasImage && _bitmapContext != NULL)
		image = CGBitmapContextCreateImage(_bitmapContext);
	[_lock unlock];

	if(image != NULL)
	{
		CGContextSaveGState(context);
		// 像素风: 禁止插值
		CGContextSetInterpolationQuality(context, kCGInterpolationNone);
		CGContextSetShouldAntialias(context, false);
		CGContextDrawImage(context, [self imageRectForBounds:bounds], image);
		CGContextRestoreGState(context);
		CGImageRelease(image);

		// ---- 临时诊断: 提交 -> 绘制完成 ----
		if(_presentStamp != 0)
		{
			struct timespec  ts;
			ines_int64_t     now;
			ines_int64_t     d;

			clock_gettime(CLOCK_MONOTONIC, &ts);
			now = (ines_int64_t)ts.tv_sec * 1000000 + (ines_int64_t)(ts.tv_nsec / 1000);
			d   = now - _presentStamp;
			_presentStamp = 0;

			if(d >= 0 && d < 1000000)
			{
				_drawSum += d;
				if(d > _drawMax)
					_drawMax = d;
				_drawCnt++;
			}

			if(_drawCnt >= 120)
			{
				INES_LOG(LOG_NTY, MOD_SYS, ISTR("draw lag: present->draw avg=%.2fms max=%.2fms n=%d\n"),
						 (double)_drawSum / 1000.0 / (double)_drawCnt,
						 (double)_drawMax / 1000.0, (int)_drawCnt);
				_drawSum = 0;
				_drawMax = 0;
				_drawCnt = 0;
			}
		}
		return;
	}

	// 没有 ROM 时给个提示
	NSDictionary*  attributes = @{
		NSFontAttributeName:            [NSFont systemFontOfSize:14],
		NSForegroundColorAttributeName: [NSColor colorWithCalibratedWhite:0.55 alpha:1.0]
	};
	NSString*      tip = @"将 .nes 文件拖到此处, 或使用 文件 > 载入 ROM… (⌘O)";
	NSSize         size = [tip sizeWithAttributes:attributes];
	NSRect         rect = NSMakeRect((bounds.size.width - size.width) / 2.0,
									 (bounds.size.height - size.height) / 2.0,
									 size.width, size.height);

	[tip drawInRect:rect withAttributes:attributes];
}


#pragma mark - 窗口尺寸变化

- (void)viewDidEndLiveResize
{
	[super viewDidEndLiveResize];
	[self setNeedsDisplay:YES];
}


#pragma mark - 键盘输入

// 把 NSEvent 转成逻辑按键位(返回 0 表示与手柄无关)
- (ines_dword_t)logicalKeyForEvent:(NSEvent*)event
{
	unsigned short  keyCode = [event keyCode];
	NSString*       chars;

	switch(keyCode)
	{
	case 123: return IKEY_LEFT;     // ←
	case 124: return IKEY_RIGHT;    // →
	case 125: return IKEY_DOWN;     // ↓
	case 126: return IKEY_UP;       // ↑
	case 36:                        // Return
	case 76: return IKEY_START;     // 小键盘 Enter
	default: break;
	}

	chars = [[event charactersIgnoringModifiers] lowercaseString];
	if([chars length] == 0)
		return 0;

	switch([chars characterAtIndex:0])
	{
	case 'a': return IKEY_TURBO_B;
	case 'z': return IKEY_B;
	case 's': return IKEY_TURBO_A;
	case 'x': return IKEY_A;
	default:  break;
	}

	return 0;
}

- (void)setKey:(ines_dword_t)key down:(BOOL)down
{
	ines_dword_t  before = _pressedKeys;

	if(key == 0)
		return;

	if(down)
		_pressedKeys |= key;
	else
		_pressedKeys &= ~key;

	if(before != _pressedKeys)
		[_delegate videoViewKeyStateDidChange:self];
}

- (void)keyDown:(NSEvent*)event
{
	[self setKey:[self logicalKeyForEvent:event] down:YES];
	// 不调用 super, 避免系统提示音
}

- (void)keyUp:(NSEvent*)event
{
	[self setKey:[self logicalKeyForEvent:event] down:NO];
}

- (void)flagsChanged:(NSEvent*)event
{
	// SELECT 用 Shift 模拟(左右均可)
	BOOL  down = (([event modifierFlags] & NSEventModifierFlagShift) != 0);

	if(down == _shiftDown)
		return;
	_shiftDown = down;
	[self setKey:IKEY_SELECT down:down];
}

// 窗口失去焦点时清空按键, 避免"卡键"(对应 win32 的 GetForegroundWindow 判断)
- (void)resetKeys
{
	if(_pressedKeys == 0 && !_shiftDown)
		return;

	_pressedKeys = 0;
	_shiftDown   = NO;
	[_delegate videoViewKeyStateDidChange:self];
}


#pragma mark - 拖放

- (NSDragOperation)draggingEntered:(id<NSDraggingInfo>)sender
{
	NSPasteboard*  pasteboard = [sender draggingPasteboard];

	if([pasteboard availableTypeFromArray:@[NSFilenamesPboardType]] == nil)
		return NSDragOperationNone;

	return NSDragOperationCopy;
}

- (BOOL)performDragOperation:(id<NSDraggingInfo>)sender
{
	NSPasteboard*  pasteboard = [sender draggingPasteboard];
	NSArray*       files;

	if(_delegate == nil
	|| ![pasteboard availableTypeFromArray:@[NSFilenamesPboardType]])
		return NO;

	files = [pasteboard propertyListForType:NSFilenamesPboardType];
	if(files == nil || [files count] == 0)
		return NO;

	if([_delegate respondsToSelector:@selector(videoView:didDropFilePaths:)])
		[_delegate videoView:self didDropFilePaths:files];

	return YES;
}

@end
