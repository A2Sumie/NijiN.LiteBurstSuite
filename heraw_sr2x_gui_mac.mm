// heraw_sr2x_gui_mac.mm -- NijiN.LiteBurstScaler (nijinLBS) Native macOS AppKit GUI
#import <Cocoa/Cocoa.h>
#include <cstdlib>
#include <string>
#include <vector>
#include <unistd.h>
#include "heraw_brand_assets.hpp"

static std::string extractJsonStr(const std::string& s, const char* key, size_t pos = 0) {
    std::string pat = std::string("\"") + key + "\":\"";
    size_t p = s.find(pat, pos);
    if (p == std::string::npos) return "";
    p += pat.size();
    std::string out;
    while (p < s.size() && s[p] != '"') {
        if (s[p] == '\\' && p + 1 < s.size()) ++p;
        out.push_back(s[p++]);
    }
    return out;
}

static int extractJsonInt(const std::string& s, const char* key, int fb, size_t pos = 0) {
    std::string pat = std::string("\"") + key + "\":";
    size_t p = s.find(pat, pos);
    if (p == std::string::npos) return fb;
    p += pat.size();
    while (p < s.size() && s[p] == ' ') ++p;
    return std::atoi(s.c_str() + p);
}

static double extractJsonDouble(const std::string& s, const char* key, double fb, size_t pos = 0) {
    std::string pat = std::string("\"") + key + "\":";
    size_t p = s.find(pat, pos);
    if (p == std::string::npos) return fb;
    p += pat.size();
    while (p < s.size() && s[p] == ' ') ++p;
    return std::atof(s.c_str() + p);
}

static NSString* sceneCodeToCn(const std::string& sc) {
    if (sc == "astro_landscape")   return @"手持星野（星空 + 静态地景双域）";
    if (sc == "astro_sky")         return @"手持纯星空 / 深空星场";
    if (sc == "aeb_bracket")       return @"包围曝光混拍（AEB 跨度预警）";
    if (sc == "tripod_static")     return @"三脚架稳态（低位移高信噪比）";
    if (sc == "action_subject")    return @"静态背景 + 局部运动主体";
    if (sc == "lowlight_high_iso") return @"高感弱光手持连拍";
    return @"标准手持亚像素超分";
}

static NSImage* loadEmbeddedPng(const unsigned char* bytes, size_t len, NSSize logicalSize) {
    NSData* data = [NSData dataWithBytesNoCopy:(void*)bytes length:len freeWhenDone:NO];
    NSImage* img = [[NSImage alloc] initWithData:data];
    if (img && logicalSize.width > 0 && logicalSize.height > 0) img.size = logicalSize;
    return img;
}

// Muted greyish-aqua primary CTA button
@interface AquaButton : NSButton
@end

@implementation AquaButton
- (BOOL)isFlipped { return YES; }
- (void)drawRect:(NSRect)dirtyRect {
    (void)dirtyRect;
    NSRect r = NSInsetRect(self.bounds, 0.5, 0.5);
    NSBezierPath* bp = [NSBezierPath bezierPathWithRoundedRect:r xRadius:7 yRadius:7];
    NSColor* bg = !self.enabled
        ? [NSColor colorWithSRGBRed:0.68 green:0.75 blue:0.78 alpha:1.0]
        : (self.isHighlighted
            ? [NSColor colorWithSRGBRed:0.25 green:0.43 blue:0.50 alpha:1.0]
            : [NSColor colorWithSRGBRed:0.31 green:0.50 blue:0.58 alpha:1.0]);
    [bg setFill];
    [bp fill];

    NSDictionary* attrs = @{
        NSFontAttributeName: [NSFont boldSystemFontOfSize:12.5],
        NSForegroundColorAttributeName: [NSColor whiteColor]
    };
    NSSize sz = [self.title sizeWithAttributes:attrs];
    [self.title drawAtPoint:NSMakePoint((self.bounds.size.width - sz.width) * 0.5,
                                        (self.bounds.size.height - sz.height) * 0.5)
             withAttributes:attrs];
}
@end

// Right-side hover dot with instant NSPopover
@interface HoverDotView : NSView
@property (nonatomic, copy) NSString* helpTitle;
@property (nonatomic, copy) NSString* helpBody;
@property (nonatomic, assign) BOOL hovered;
@property (nonatomic, strong) NSPopover* popover;
@property (nonatomic, strong) NSTrackingArea* trackArea;
- (instancetype)initWithFrame:(NSRect)frameRect title:(NSString*)title body:(NSString*)body;
- (void)showHelpPopover;
@end

@implementation HoverDotView
- (instancetype)initWithFrame:(NSRect)frameRect title:(NSString*)title body:(NSString*)body {
    self = [super initWithFrame:frameRect];
    if (self) {
        _helpTitle = [title copy];
        _helpBody = [body copy];
        self.toolTip = [NSString stringWithFormat:@"%@\n%@", title, body];
    }
    return self;
}

- (BOOL)isFlipped { return YES; }

- (void)updateTrackingAreas {
    [super updateTrackingAreas];
    if (self.trackArea) [self removeTrackingArea:self.trackArea];
    self.trackArea = [[NSTrackingArea alloc] initWithRect:self.bounds
        options:(NSTrackingMouseEnteredAndExited | NSTrackingActiveAlways | NSTrackingInVisibleRect)
        owner:self userInfo:nil];
    [self addTrackingArea:self.trackArea];
}

- (void)showHelpPopover {
    if (self.popover && self.popover.isShown) return;
    NSViewController* vc = [[NSViewController alloc] init];
    NSView* box = [[NSView alloc] initWithFrame:NSMakeRect(0, 0, 366, 10)];

    NSTextField* tLbl = [NSTextField labelWithString:self.helpTitle ?: @""];
    tLbl.font = [NSFont boldSystemFontOfSize:12.0];
    tLbl.textColor = [NSColor colorWithSRGBRed:0.16 green:0.25 blue:0.30 alpha:1.0];

    NSTextField* bLbl = [NSTextField wrappingLabelWithString:self.helpBody ?: @""];
    bLbl.font = [NSFont systemFontOfSize:11.5];
    bLbl.textColor = [NSColor colorWithSRGBRed:0.22 green:0.31 blue:0.36 alpha:1.0];
    bLbl.preferredMaxLayoutWidth = 338;
    NSSize bSz = [bLbl.cell cellSizeForBounds:NSMakeRect(0, 0, 338, 600)];
    CGFloat totalH = 10 + 18 + 5 + ceil(bSz.height) + 10;

    box.frame = NSMakeRect(0, 0, 366, totalH);
    tLbl.frame = NSMakeRect(14, totalH - 28, 338, 18);
    bLbl.frame = NSMakeRect(14, 10, 338, ceil(bSz.height));
    [box addSubview:tLbl];
    [box addSubview:bLbl];
    vc.view = box;

    NSPopover* pop = [[NSPopover alloc] init];
    pop.contentViewController = vc;
    pop.behavior = NSPopoverBehaviorTransient;
    pop.animates = NO;
    self.popover = pop;
    [pop showRelativeToRect:self.bounds ofView:self preferredEdge:NSRectEdgeMinX];
}

- (void)mouseEntered:(NSEvent*)event {
    (void)event;
    self.hovered = YES;
    [self setNeedsDisplay:YES];
    [self showHelpPopover];
}

- (void)mouseExited:(NSEvent*)event {
    (void)event;
    self.hovered = NO;
    [self setNeedsDisplay:YES];
    if (self.popover && self.popover.isShown) [self.popover close];
}

- (void)mouseDown:(NSEvent*)event {
    (void)event;
    [self showHelpPopover];
}

- (void)drawRect:(NSRect)dirtyRect {
    (void)dirtyRect;
    NSRect r = NSInsetRect(self.bounds, 1.0, 1.0);
    NSBezierPath* circle = [NSBezierPath bezierPathWithOvalInRect:r];
    if (self.hovered) {
        [[NSColor colorWithSRGBRed:0.31 green:0.50 blue:0.58 alpha:1.0] setFill];
        [circle fill];
    } else {
        [[NSColor colorWithSRGBRed:0.86 green:0.92 blue:0.94 alpha:1.0] setFill];
        [circle fill];
        [[NSColor colorWithSRGBRed:0.67 green:0.78 blue:0.82 alpha:1.0] setStroke];
        circle.lineWidth = 1.0;
        [circle stroke];
    }

    NSDictionary* attrs = @{
        NSFontAttributeName: [NSFont boldSystemFontOfSize:10.5],
        NSForegroundColorAttributeName: self.hovered
            ? [NSColor whiteColor]
            : [NSColor colorWithSRGBRed:0.25 green:0.43 blue:0.50 alpha:1.0]
    };
    NSString* sym = @"i";
    NSSize sz = [sym sizeWithAttributes:attrs];
    [sym drawAtPoint:NSMakePoint((self.bounds.size.width - sz.width) * 0.5,
                                 (self.bounds.size.height - sz.height) * 0.5)
      withAttributes:attrs];
}
@end

// Muted Greyish-Aqua Studio Canvas
@interface StudioCanvasView : NSView
@property (nonatomic, strong) NSImage* gxLogo;
@property (nonatomic, strong) NSImage* nijinLockup;
@end

@implementation StudioCanvasView
- (BOOL)isFlipped { return YES; }

static void drawCard(NSRect r) {
    NSBezierPath* sh = [NSBezierPath bezierPathWithRoundedRect:NSOffsetRect(r, 0, 1.5) xRadius:10 yRadius:10];
    [[NSColor colorWithSRGBRed:0.12 green:0.22 blue:0.26 alpha:0.04] setFill];
    [sh fill];

    NSBezierPath* bp = [NSBezierPath bezierPathWithRoundedRect:r xRadius:10 yRadius:10];
    [[NSColor colorWithSRGBRed:0.976 green:0.988 blue:0.992 alpha:1.0] setFill];
    [bp fill];
    [[NSColor colorWithSRGBRed:0.765 green:0.831 blue:0.863 alpha:1.0] setStroke];
    bp.lineWidth = 1.0;
    [bp stroke];
}

static void drawBadge(CGFloat x, CGFloat y, NSString* num) {
    NSRect r = NSMakeRect(x, y, 26, 18);
    NSBezierPath* bp = [NSBezierPath bezierPathWithRoundedRect:r xRadius:5 yRadius:5];
    [[NSColor colorWithSRGBRed:0.855 green:0.914 blue:0.933 alpha:1.0] setFill];
    [bp fill];
    NSDictionary* attrs = @{
        NSFontAttributeName: [NSFont monospacedDigitSystemFontOfSize:10.5 weight:NSFontWeightBold],
        NSForegroundColorAttributeName: [NSColor colorWithSRGBRed:0.25 green:0.43 blue:0.50 alpha:1.0]
    };
    NSSize sz = [num sizeWithAttributes:attrs];
    [num drawAtPoint:NSMakePoint(x + (26 - sz.width) * 0.5, y + (18 - sz.height) * 0.5) withAttributes:attrs];
}

static void drawSoftDivider(CGFloat x1, CGFloat x2, CGFloat y) {
    NSBezierPath* ln = [NSBezierPath bezierPath];
    [ln moveToPoint:NSMakePoint(x1, y + 0.5)];
    [ln lineToPoint:NSMakePoint(x2, y + 0.5)];
    ln.lineWidth = 1.0;
    [[NSColor colorWithSRGBRed:0.85 green:0.90 blue:0.92 alpha:1.0] setStroke];
    [ln stroke];
}

- (void)drawRect:(NSRect)dirtyRect {
    (void)dirtyRect;
    // Muted greyish-aqua canvas (#E7EFF2)
    [[NSColor colorWithSRGBRed:0.906 green:0.937 blue:0.949 alpha:1.0] setFill];
    NSRectFill(self.bounds);

    // Header Bar (0, 0, width, 62)
    NSRect hdr = NSMakeRect(0, 0, self.bounds.size.width, 62);
    [[NSColor colorWithSRGBRed:0.976 green:0.988 blue:0.992 alpha:1.0] setFill];
    NSRectFill(hdr);

    // Top 3px Muted Greyish-Aqua Accent Stripe (#548296)
    [[NSColor colorWithSRGBRed:0.329 green:0.510 blue:0.588 alpha:1.0] setFill];
    NSRectFill(NSMakeRect(0, 0, self.bounds.size.width, 3));

    // Header Bottom Border
    [[NSColor colorWithSRGBRed:0.765 green:0.831 blue:0.863 alpha:1.0] setFill];
    NSRectFill(NSMakeRect(0, 61, self.bounds.size.width, 1));

    // Subtle nijinLBS pill tag + concise English tagline beside main title
    NSRect tagRect = NSMakeRect(236, 21, 64, 20);
    NSBezierPath* tagBp = [NSBezierPath bezierPathWithRoundedRect:tagRect xRadius:5 yRadius:5];
    [[NSColor colorWithSRGBRed:0.86 green:0.915 blue:0.935 alpha:1.0] setFill];
    [tagBp fill];
    NSDictionary* tagAttrs = @{
        NSFontAttributeName: [NSFont systemFontOfSize:11.0 weight:NSFontWeightSemibold],
        NSForegroundColorAttributeName: [NSColor colorWithSRGBRed:0.25 green:0.43 blue:0.50 alpha:1.0]
    };
    NSSize tsz = [@"nijinLBS" sizeWithAttributes:tagAttrs];
    [@"nijinLBS" drawAtPoint:NSMakePoint(tagRect.origin.x + (tagRect.size.width - tsz.width) * 0.5,
                                         tagRect.origin.y + (tagRect.size.height - tsz.height) * 0.5)
              withAttributes:tagAttrs];

    NSDictionary* descAttrs = @{
        NSFontAttributeName: [NSFont systemFontOfSize:12.0 weight:NSFontWeightRegular],
        NSForegroundColorAttributeName: [NSColor colorWithSRGBRed:0.42 green:0.52 blue:0.57 alpha:1.0]
    };
    [@"Fast Multi-Frame Upscaler" drawAtPoint:NSMakePoint(312, 23) withAttributes:descAttrs];

    // Right-Side Co-Brand Cluster: tight [GX Cross-Star] | [NijiN]
    CGFloat nx = self.bounds.size.width - 20 - heraw_brand::kNijin1xW;
    CGFloat divX = nx - 6;
    CGFloat gxX = divX - 7 - heraw_brand::kGx1xW;
    CGFloat gxY = (62.0 - heraw_brand::kGx1xH) * 0.5;
    CGFloat njY = (62.0 - heraw_brand::kNijin1xH) * 0.5;

    if (self.gxLogo) {
        [self.gxLogo drawInRect:NSMakeRect(gxX, gxY, heraw_brand::kGx1xW, heraw_brand::kGx1xH)
                       fromRect:NSZeroRect operation:NSCompositingOperationSourceOver fraction:1.0
                 respectFlipped:YES hints:@{NSImageHintInterpolation: @(NSImageInterpolationHigh)}];
    }
    [[NSColor colorWithSRGBRed:0.78 green:0.84 blue:0.87 alpha:1.0] setFill];
    NSRectFill(NSMakeRect(divX, 17, 1, 28));
    if (self.nijinLockup) {
        [self.nijinLockup drawInRect:NSMakeRect(nx, njY, heraw_brand::kNijin1xW, heraw_brand::kNijin1xH)
                            fromRect:NSZeroRect operation:NSCompositingOperationSourceOver fraction:1.0
                      respectFlipped:YES hints:@{NSImageHintInterpolation: @(NSImageInterpolationHigh)}];
    }

    // 5 Rounded Cards
    drawCard(NSMakeRect(18, 76,  480, 260)); // Card 01: Input
    drawCard(NSMakeRect(18, 350, 480, 216)); // Card 02 & 03: Policy, Presets & Motion Mode
    drawCard(NSMakeRect(514, 76,  488, 196)); // Card 04: Output
    drawCard(NSMakeRect(514, 286, 488, 280)); // Card 05: Engine, Backend & Knobs
    drawCard(NSMakeRect(18, 580, 984, 110)); // Card 06: Run & Console

    // Badges
    drawBadge(34,  90,  @"01");
    drawBadge(34,  364, @"02");
    drawBadge(34,  462, @"03");
    drawBadge(530, 90,  @"04");
    drawBadge(530, 300, @"05");

    // Dividers
    drawSoftDivider(34,  482, 450);
    drawSoftDivider(530, 986, 398);
}
@end

@interface DropWindow : NSWindow
@property (nonatomic, copy) void (^onDropPaths)(NSArray<NSString*>* paths);
@end

// Custom NSApplication ensuring Cmd+Q / Cmd+W / Cmd+X/C/V/A/Z always work in unbundled Mach-O builds
@interface NijiLbsApplication : NSApplication
@end

@implementation NijiLbsApplication
- (void)sendEvent:(NSEvent*)event {
    if (event.type == NSEventTypeKeyDown) {
        NSEventModifierFlags mods = event.modifierFlags & NSEventModifierFlagDeviceIndependentFlagsMask;
        NSString* ch = [event.charactersIgnoringModifiers lowercaseString];
        if (mods == NSEventModifierFlagCommand) {
            if ([ch isEqualToString:@"q"]) { [self terminate:nil]; return; }
            if ([ch isEqualToString:@"w"]) { [self.keyWindow performClose:nil]; return; }
            if ([ch isEqualToString:@"h"]) { [self hide:nil]; return; }
            if ([ch isEqualToString:@"x"] && [self sendAction:@selector(cut:) to:nil from:self]) return;
            if ([ch isEqualToString:@"c"] && [self sendAction:@selector(copy:) to:nil from:self]) return;
            if ([ch isEqualToString:@"v"] && [self sendAction:@selector(paste:) to:nil from:self]) return;
            if ([ch isEqualToString:@"a"] && [self sendAction:@selector(selectAll:) to:nil from:self]) return;
            if ([ch isEqualToString:@"z"] && [self sendAction:NSSelectorFromString(@"undo:") to:nil from:self]) return;
        } else if (mods == (NSEventModifierFlagCommand | NSEventModifierFlagShift)) {
            if ([ch isEqualToString:@"z"] && [self sendAction:NSSelectorFromString(@"redo:") to:nil from:self]) return;
        }
    }
    [super sendEvent:event];
}
@end

@implementation DropWindow
- (NSDragOperation)draggingEntered:(id<NSDraggingInfo>)sender {
    (void)sender;
    return NSDragOperationCopy;
}
- (BOOL)performDragOperation:(id<NSDraggingInfo>)sender {
    NSPasteboard* pb = [sender draggingPasteboard];
    NSArray<NSURL*>* urls = [pb readObjectsForClasses:@[[NSURL class]] options:@{NSPasteboardURLReadingFileURLsOnlyKey: @YES}];
    if (urls.count > 0 && self.onDropPaths) {
        NSMutableArray<NSString*>* arr = [NSMutableArray arrayWithCapacity:urls.count];
        for (NSURL* u in urls) if (u.path) [arr addObject:u.path];
        self.onDropPaths(arr);
        return YES;
    }
    return NO;
}
@end

static NSString* const kPrefSuite      = @"com.nijin.liteburstscaler";
static NSString* const kPrefCliPath    = @"NijinLbsCustomCliPath";
static NSString* const kPrefLibRawPath = @"NijinLbsCustomLibRawPath";

static NSUserDefaults* nijinPrefs(void) {
    static NSUserDefaults* ud = nil;
    if (!ud) {
        NSString* bid = [[NSBundle mainBundle] bundleIdentifier];
        if ([bid isEqualToString:kPrefSuite]) {
            ud = [NSUserDefaults standardUserDefaults];
        } else {
            ud = [[NSUserDefaults alloc] initWithSuiteName:kPrefSuite] ?: [NSUserDefaults standardUserDefaults];
        }
    }
    return ud;
}

@interface AppDelegate : NSObject <NSApplicationDelegate, NSTableViewDataSource, NSTableViewDelegate>
@property (strong) DropWindow* window;
@property (strong) StudioCanvasView* canvas;
@property (strong) NSMutableArray<NSString*>* inputs;
@property (strong) NSTableView* tableView;
@property (strong) NSTextField* bannerLabel;
@property (strong) NSButton* chkSortTime;
@property (strong) NSButton* chkRecursive;

@property (strong) NSArray<NSButton*>* radPolicies;
@property (strong) NSArray<NSButton*>* radPresets;
@property (strong) NSArray<NSButton*>* radMotions;
@property (strong) NSPopUpButton* popBackend;

@property (strong) NSTextField* edtOutDir;
@property (strong) NSButton* chkEmitRaw;
@property (strong) NSPopUpButton* popRawFmt;
@property (strong) NSButton* chkEmitRgb;
@property (strong) NSPopUpButton* popRgbFmt;
@property (strong) NSTextField* edtJxsBpp;
@property (strong) NSButton* radCsSrgb;
@property (strong) NSButton* radCsAdobe;
@property (strong) NSButton* chkEmitField;
@property (strong) NSPopUpButton* popFieldMode;

@property (strong) NSTextField* edtCliPath;
@property (strong) NSTextField* edtLibRawPath;

@property (strong) NSTextField* edtDenoise;
@property (strong) NSTextField* edtChroma;
@property (strong) NSTextField* edtPsfIters;
@property (strong) NSTextField* edtRgbPsf;
@property (strong) NSTextField* edtPsfSigma;
@property (strong) NSTextField* edtDeghost;
@property (strong) NSTextField* edtEv;
@property (strong) NSTextField* edtGrpSz;
@property (strong) NSTextField* edtThreads;
@property (strong) NSButton* chkAutoExp;
@property (strong) NSButton* chkRolloff;
@property (strong) NSButton* chkEdgePol;
@property (strong) NSButton* chkDither;

@property (strong) AquaButton* btnRun;
@property (strong) NSButton* btnInspect;
@property (strong) NSButton* btnCancel;
@property (strong) NSProgressIndicator* progBar;
@property (strong) NSTextField* pctLabel;
@property (strong) NSTextView* logView;
@property (strong) NSMutableArray<HoverDotView*>* hoverDots;
@property (strong) NSTask* currentTask;
@property (assign) int customAnchor1Based;
@property (assign) BOOL isScreenshotMode;
@end

@implementation AppDelegate

- (void)setupMainMenu {
    NSMenu* mainMenu = [[NSMenu alloc] initWithTitle:@"MainMenu"];

    // Application menu (Cmd+Q, Cmd+H, Cmd+W)
    NSMenuItem* appItem = [[NSMenuItem alloc] initWithTitle:@"nijinLBS" action:nil keyEquivalent:@""];
    NSMenu* appMenu = [[NSMenu alloc] initWithTitle:@"nijinLBS"];
    [appMenu addItemWithTitle:@"隐藏 nijinLBS" action:@selector(hide:) keyEquivalent:@"h"];
    NSMenuItem* hideOthers = [appMenu addItemWithTitle:@"隐藏其他" action:@selector(hideOtherApplications:) keyEquivalent:@"h"];
    hideOthers.keyEquivalentModifierMask = NSEventModifierFlagCommand | NSEventModifierFlagOption;
    [appMenu addItem:[NSMenuItem separatorItem]];
    [appMenu addItemWithTitle:@"关闭窗口" action:@selector(performClose:) keyEquivalent:@"w"];
    [appMenu addItemWithTitle:@"退出 nijinLBS" action:@selector(terminate:) keyEquivalent:@"q"];
    appItem.submenu = appMenu;
    [mainMenu addItem:appItem];

    // Edit menu so Cmd+X/C/V/A work natively in text fields
    NSMenuItem* editItem = [[NSMenuItem alloc] initWithTitle:@"编辑" action:nil keyEquivalent:@""];
    NSMenu* editMenu = [[NSMenu alloc] initWithTitle:@"编辑"];
    [editMenu addItemWithTitle:@"撤销" action:NSSelectorFromString(@"undo:") keyEquivalent:@"z"];
    [editMenu addItemWithTitle:@"重做" action:NSSelectorFromString(@"redo:") keyEquivalent:@"Z"];
    [editMenu addItem:[NSMenuItem separatorItem]];
    [editMenu addItemWithTitle:@"剪切" action:@selector(cut:) keyEquivalent:@"x"];
    [editMenu addItemWithTitle:@"拷贝" action:@selector(copy:) keyEquivalent:@"c"];
    [editMenu addItemWithTitle:@"粘贴" action:@selector(paste:) keyEquivalent:@"v"];
    [editMenu addItemWithTitle:@"全选" action:@selector(selectAll:) keyEquivalent:@"a"];
    editItem.submenu = editMenu;
    [mainMenu addItem:editItem];

    [NSApp setMainMenu:mainMenu];
}

- (BOOL)isValidExecutable:(NSString*)path {
    if (!path || path.length == 0) return NO;
    BOOL isDir = NO;
    if (![[NSFileManager defaultManager] fileExistsAtPath:path isDirectory:&isDir] || isDir) return NO;
    return [[NSFileManager defaultManager] isExecutableFileAtPath:path];
}

- (NSString*)autoDetectCliPath:(BOOL)includeSaved {
    NSString* selfPath = [[NSBundle mainBundle] executablePath];
    if (!selfPath) selfPath = [[[NSProcessInfo processInfo] arguments] firstObject];

    // Inside a macOS .app bundle, prefer the co-packaged Contents/MacOS/nijinLBS first unless user set a custom path
    if (includeSaved) {
        NSString* saved = [nijinPrefs() stringForKey:kPrefCliPath];
        if ([self isValidExecutable:saved]) return saved;
    }
    if (selfPath) {
        NSString* bundled = [[selfPath stringByDeletingLastPathComponent] stringByAppendingPathComponent:@"nijinLBS"];
        if ([self isValidExecutable:bundled]) return [bundled stringByStandardizingPath];
    }

    NSArray<NSString*>* names = @[@"nijinLBS", @"heraw_sr2x"];
    NSMutableArray<NSString*>* dirs = [NSMutableArray array];
    if (selfPath) {
        NSString* d = [selfPath stringByDeletingLastPathComponent];
        [dirs addObject:d];
        [dirs addObject:[d stringByDeletingLastPathComponent]];
    }
    NSString* cwd = [[NSFileManager defaultManager] currentDirectoryPath];
    if (cwd) [dirs addObject:cwd];

    NSString* pathEnv = [[[NSProcessInfo processInfo] environment] objectForKey:@"PATH"];
    if (pathEnv) {
        for (NSString* p in [pathEnv componentsSeparatedByString:@":"]) {
            if (p.length > 0) [dirs addObject:p];
        }
    }
    for (NSString* nm in names) {
        for (NSString* d in dirs) {
            NSString* c = [d stringByAppendingPathComponent:nm];
            if ([self isValidExecutable:c]) return [c stringByStandardizingPath];
        }
    }
    return nil;
}

- (NSString*)promptUserForCliPath {
    NSAlert* al = [[NSAlert alloc] init];
    al.alertStyle = NSAlertStyleInformational;
    al.messageText = @"未找到 nijinLBS 命令行引擎";
    al.informativeText = @"请指定 nijinLBS 可执行文件路径。";
    [al addButtonWithTitle:@"选择路径..."];
    [al addButtonWithTitle:@"取消"];
    if ([al runModal] != NSAlertFirstButtonReturn) return nil;

    NSOpenPanel* op = [NSOpenPanel openPanel];
    op.title = @"选择 nijinLBS 可执行文件";
    op.canChooseFiles = YES;
    op.canChooseDirectories = NO;
    op.allowsMultipleSelection = NO;
    if ([op runModal] == NSModalResponseOK && op.URL.path) {
        NSString* chosen = op.URL.path;
        if ([self isValidExecutable:chosen]) {
            self.edtCliPath.stringValue = chosen;
            [nijinPrefs() setObject:chosen forKey:kPrefCliPath];
            [self appendLog:[NSString stringWithFormat:@"[CLI] %@", chosen]];
            return chosen;
        }
    }
    return nil;
}

- (NSString*)resolveActiveCliOrPrompt {
    NSString* cur = [self.edtCliPath.stringValue stringByTrimmingCharactersInSet:[NSCharacterSet whitespaceAndNewlineCharacterSet]];
    if ([self isValidExecutable:cur]) {
        [nijinPrefs() setObject:cur forKey:kPrefCliPath];
        return cur;
    }
    NSString* found = [self autoDetectCliPath:YES];
    if (found) {
        self.edtCliPath.stringValue = found;
        return found;
    }
    return [self promptUserForCliPath];
}

- (void)onBrowseCli:(id)sender {
    (void)sender;
    NSOpenPanel* op = [NSOpenPanel openPanel];
    op.title = @"选择 nijinLBS 可执行文件";
    op.canChooseFiles = YES;
    op.canChooseDirectories = NO;
    op.allowsMultipleSelection = NO;
    if ([op runModal] == NSModalResponseOK && op.URL.path) {
        self.edtCliPath.stringValue = op.URL.path;
        [nijinPrefs() setObject:op.URL.path forKey:kPrefCliPath];
        [self appendLog:[NSString stringWithFormat:@"[CLI] %@", op.URL.path]];
    }
}

- (void)onAutoDetectCli:(id)sender {
    (void)sender;
    NSString* found = [self autoDetectCliPath:NO];
    if (found) {
        self.edtCliPath.stringValue = found;
        [nijinPrefs() setObject:found forKey:kPrefCliPath];
        [self appendLog:[NSString stringWithFormat:@"[CLI] %@", found]];
    } else {
        [self promptUserForCliPath];
    }
}

- (void)onBrowseLibRaw:(id)sender {
    (void)sender;
    NSOpenPanel* op = [NSOpenPanel openPanel];
    op.title = @"选择 LibRaw 动态库 (.dylib / .so)";
    op.canChooseFiles = YES;
    op.canChooseDirectories = NO;
    op.allowsMultipleSelection = NO;
    if ([op runModal] == NSModalResponseOK && op.URL.path) {
        self.edtLibRawPath.stringValue = op.URL.path;
        [nijinPrefs() setObject:op.URL.path forKey:kPrefLibRawPath];
        [self appendLog:[NSString stringWithFormat:@"[LibRaw] %@", op.URL.path]];
    }
}

- (void)onResetLibRaw:(id)sender {
    (void)sender;
    self.edtLibRawPath.stringValue = @"";
    [nijinPrefs() removeObjectForKey:kPrefLibRawPath];
    [self appendLog:@"[LibRaw] Built-in 0.23-HE*"];
}

- (void)appendLog:(NSString*)line {
    dispatch_async(dispatch_get_main_queue(), ^{
        NSString* s = [line stringByAppendingString:@"\n"];
        NSAttributedString* attr = [[NSAttributedString alloc] initWithString:s attributes:@{
            NSFontAttributeName: [NSFont monospacedSystemFontOfSize:11 weight:NSFontWeightRegular],
            NSForegroundColorAttributeName: [NSColor colorWithSRGBRed:0.16 green:0.24 blue:0.28 alpha:1.0]
        }];
        [[self.logView textStorage] appendAttributedString:attr];
        [self.logView scrollRangeToVisible:NSMakeRange([[self.logView string] length], 0)];
    });
}

- (void)updateBanner {
    NSUInteger n = self.inputs.count;
    BOOL hasDir = NO;
    NSFileManager* fm = [NSFileManager defaultManager];
    for (NSString* p in self.inputs) {
        BOOL isD = NO;
        if ([fm fileExistsAtPath:p isDirectory:&isD] && isD) hasDir = YES;
    }
    if (n == 0) {
        self.bannerLabel.stringValue = @"未导入（支持拖放 RAW 或目录）";
        self.bannerLabel.textColor = [NSColor colorWithSRGBRed:0.42 green:0.52 blue:0.57 alpha:1.0];
    } else if (hasDir) {
        self.bannerLabel.stringValue = [NSString stringWithFormat:@"%lu 项（含目录扫描）", (unsigned long)n];
        self.bannerLabel.textColor = [NSColor colorWithSRGBRed:0.24 green:0.44 blue:0.52 alpha:1.0];
    } else if (n < 4) {
        self.bannerLabel.stringValue = [NSString stringWithFormat:@"%lu 张（低于 4 张门槛）", (unsigned long)n];
        self.bannerLabel.textColor = [NSColor colorWithSRGBRed:0.74 green:0.40 blue:0.12 alpha:1.0];
    } else {
        self.bannerLabel.stringValue = [NSString stringWithFormat:@"已选 %lu 张", (unsigned long)n];
        self.bannerLabel.textColor = [NSColor colorWithSRGBRed:0.22 green:0.45 blue:0.52 alpha:1.0];
    }
}

- (void)addInputPaths:(NSArray<NSString*>*)paths {
    for (NSString* p in paths) {
        if (![self.inputs containsObject:p]) [self.inputs addObject:p];
    }
    if (self.inputs.count > 0 && self.edtOutDir.stringValue.length == 0) {
        NSString* first = self.inputs.firstObject;
        BOOL isD = NO;
        [[NSFileManager defaultManager] fileExistsAtPath:first isDirectory:&isD];
        NSString* base = isD ? first : [first stringByDeletingLastPathComponent];
        self.edtOutDir.stringValue = [base stringByAppendingPathComponent:@"nijinLBS_out"];
    }
    [self.tableView reloadData];
    [self updateBanner];
}

- (NSInteger)numberOfRowsInTableView:(NSTableView*)tableView {
    (void)tableView;
    return (NSInteger)self.inputs.count;
}

- (NSView*)tableView:(NSTableView*)tv viewForTableColumn:(NSTableColumn*)col row:(NSInteger)row {
    (void)col;
    NSTextField* tf = [tv makeViewWithIdentifier:@"PathCell" owner:self];
    if (!tf) {
        tf = [NSTextField labelWithString:@""];
        tf.identifier = @"PathCell";
        tf.font = [NSFont systemFontOfSize:11.5];
        tf.textColor = [NSColor colorWithSRGBRed:0.15 green:0.22 blue:0.26 alpha:1.0];
        tf.lineBreakMode = NSLineBreakByTruncatingMiddle;
    }
    NSString* p = self.inputs[(NSUInteger)row];
    BOOL isD = NO;
    [[NSFileManager defaultManager] fileExistsAtPath:p isDirectory:&isD];
    tf.stringValue = [NSString stringWithFormat:@"[%@] %@", isD ? @"DIR" : @"RAW", p];
    return tf;
}

- (void)saveUiPrefs {
    if (self.isScreenshotMode) return;
    NSUserDefaults* ud = nijinPrefs();
    [ud setBool:YES forKey:@"HasSavedUiPrefs"];
    [ud setBool:(self.chkEmitRaw.state == NSControlStateValueOn) forKey:@"EmitRaw"];
    static NSArray<NSString*>* const kRawCodes = @[@"dng", @"dng-hq", @"dng-fp16", @"dng-raw"];
    static NSArray<NSString*>* const kRgbCodes = @[@"tiff-deflate", @"png", @"jxs", @"jxs-lossless", @"tiff-raw"];
    NSInteger rIdx = self.popRawFmt.indexOfSelectedItem;
    if (rIdx >= 0 && (NSUInteger)rIdx < kRawCodes.count) [ud setObject:kRawCodes[(NSUInteger)rIdx] forKey:@"RawFmtCode"];
    [ud setBool:(self.chkEmitRgb.state == NSControlStateValueOn) forKey:@"EmitRgb"];
    NSInteger cIdx = self.popRgbFmt.indexOfSelectedItem;
    if (cIdx >= 0 && (NSUInteger)cIdx < kRgbCodes.count) [ud setObject:kRgbCodes[(NSUInteger)cIdx] forKey:@"RgbFmtCode"];
    if (self.edtJxsBpp.stringValue.length > 0) [ud setObject:self.edtJxsBpp.stringValue forKey:@"JxsBpp"];
    static NSArray<NSString*>* const kFieldCodes = @[@"split", @"rgb", @"motion", @"phase", @"focus"];
    [ud setInteger:(self.radCsAdobe.state == NSControlStateValueOn ? 1 : 0) forKey:@"ColorSpaceIdx"];
    [ud setBool:(self.chkEmitField.state == NSControlStateValueOn) forKey:@"EmitField"];
    NSInteger fmIdx = self.popFieldMode.indexOfSelectedItem;
    if (fmIdx >= 0 && (NSUInteger)fmIdx < kFieldCodes.count) [ud setObject:kFieldCodes[(NSUInteger)fmIdx] forKey:@"FieldModeCode"];

    [ud setBool:(self.chkSortTime.state == NSControlStateValueOn) forKey:@"SortTime"];
    [ud setBool:(self.chkRecursive.state == NSControlStateValueOn) forKey:@"Recursive"];

    for (NSUInteger i = 0; i < self.radPolicies.count; ++i)
        if (self.radPolicies[i].state == NSControlStateValueOn) { [ud setInteger:(NSInteger)i forKey:@"PolicyIdx"]; break; }
    for (NSUInteger i = 0; i < self.radPresets.count; ++i)
        if (self.radPresets[i].state == NSControlStateValueOn) { [ud setInteger:(NSInteger)i forKey:@"PresetIdx"]; break; }
    for (NSUInteger i = 0; i < self.radMotions.count; ++i)
        if (self.radMotions[i].state == NSControlStateValueOn) { [ud setInteger:(NSInteger)i forKey:@"AnchorIdx"]; break; }

    [ud setInteger:self.popBackend.indexOfSelectedItem forKey:@"BackendIdx"];
    [ud setObject:self.edtDenoise.stringValue  forKey:@"Denoise"];
    [ud setObject:self.edtChroma.stringValue   forKey:@"Chroma"];
    [ud setObject:self.edtPsfIters.stringValue forKey:@"PsfIters"];
    [ud setObject:self.edtRgbPsf.stringValue   forKey:@"RgbPsf"];
    [ud setObject:self.edtPsfSigma.stringValue forKey:@"PsfSigma"];
    [ud setObject:self.edtDeghost.stringValue  forKey:@"Deghost"];
    [ud setObject:self.edtEv.stringValue       forKey:@"EvOffset"];
    [ud setObject:self.edtGrpSz.stringValue    forKey:@"GroupSize"];
    [ud setObject:self.edtThreads.stringValue  forKey:@"Threads"];

    [ud setBool:(self.chkAutoExp.state == NSControlStateValueOn) forKey:@"AutoExp"];
    [ud setBool:(self.chkRolloff.state == NSControlStateValueOn) forKey:@"Rolloff"];
    [ud setBool:(self.chkEdgePol.state == NSControlStateValueOn) forKey:@"EdgePol"];
    [ud setBool:(self.chkDither.state  == NSControlStateValueOn) forKey:@"Dither"];
    [ud synchronize];
}

- (void)loadUiPrefs {
    NSUserDefaults* ud = nijinPrefs();
    if (![ud boolForKey:@"HasSavedUiPrefs"]) return;

    static NSArray<NSString*>* const kRawCodes = @[@"dng", @"dng-hq", @"dng-fp16", @"dng-raw"];
    static NSArray<NSString*>* const kRgbCodes = @[@"tiff-deflate", @"png", @"jxs", @"jxs-lossless", @"tiff-raw"];
    self.chkEmitRaw.state = [ud boolForKey:@"EmitRaw"] ? NSControlStateValueOn : NSControlStateValueOff;
    NSString* rCode = [ud stringForKey:@"RawFmtCode"];
    NSUInteger rIdx = rCode ? [kRawCodes indexOfObject:rCode] : NSNotFound;
    if (rIdx != NSNotFound && (NSInteger)rIdx < self.popRawFmt.numberOfItems) [self.popRawFmt selectItemAtIndex:(NSInteger)rIdx];

    self.chkEmitRgb.state = [ud boolForKey:@"EmitRgb"] ? NSControlStateValueOn : NSControlStateValueOff;
    NSString* cCode = [ud stringForKey:@"RgbFmtCode"];
    NSUInteger cIdx = cCode ? [kRgbCodes indexOfObject:cCode] : NSNotFound;
    if (cIdx != NSNotFound && (NSInteger)cIdx < self.popRgbFmt.numberOfItems) [self.popRgbFmt selectItemAtIndex:(NSInteger)cIdx];

    NSString* bpp = [ud stringForKey:@"JxsBpp"];
    if (bpp.length > 0) self.edtJxsBpp.stringValue = bpp;

    static NSArray<NSString*>* const kFieldCodes = @[@"split", @"rgb", @"motion", @"phase", @"focus"];
    NSInteger csIdx = [ud integerForKey:@"ColorSpaceIdx"];
    self.radCsSrgb.state  = (csIdx == 0) ? NSControlStateValueOn : NSControlStateValueOff;
    self.radCsAdobe.state = (csIdx == 1) ? NSControlStateValueOn : NSControlStateValueOff;
    self.chkEmitField.state = [ud boolForKey:@"EmitField"] ? NSControlStateValueOn : NSControlStateValueOff;
    NSString* fmCode = [ud stringForKey:@"FieldModeCode"];
    NSUInteger fmIdx = fmCode ? [kFieldCodes indexOfObject:fmCode] : NSNotFound;
    if (fmIdx != NSNotFound && (NSInteger)fmIdx < self.popFieldMode.numberOfItems) [self.popFieldMode selectItemAtIndex:(NSInteger)fmIdx];

    self.chkSortTime.state  = [ud boolForKey:@"SortTime"]  ? NSControlStateValueOn : NSControlStateValueOff;
    self.chkRecursive.state = [ud boolForKey:@"Recursive"] ? NSControlStateValueOn : NSControlStateValueOff;

    NSInteger polIdx = [ud integerForKey:@"PolicyIdx"];
    if (polIdx >= 0 && (NSUInteger)polIdx < self.radPolicies.count) {
        for (NSUInteger i = 0; i < self.radPolicies.count; ++i)
            self.radPolicies[i].state = ((NSInteger)i == polIdx) ? NSControlStateValueOn : NSControlStateValueOff;
    }
    NSInteger preIdx = [ud integerForKey:@"PresetIdx"];
    if (preIdx >= 0 && (NSUInteger)preIdx < self.radPresets.count) {
        [self applyPresetIndex:(NSUInteger)preIdx];
    }
    NSInteger ancIdx = [ud integerForKey:@"AnchorIdx"];
    if (ancIdx >= 0 && (NSUInteger)ancIdx < self.radMotions.count) {
        for (NSUInteger i = 0; i < self.radMotions.count; ++i)
            self.radMotions[i].state = ((NSInteger)i == ancIdx) ? NSControlStateValueOn : NSControlStateValueOff;
    }

    NSInteger bkIdx = [ud integerForKey:@"BackendIdx"];
    if (bkIdx >= 0 && bkIdx < self.popBackend.numberOfItems) [self.popBackend selectItemAtIndex:bkIdx];

    auto loadStr = ^(NSString* key, NSTextField* tf) {
        NSString* s = [ud stringForKey:key];
        if (s.length > 0) tf.stringValue = s;
    };
    loadStr(@"Denoise",   self.edtDenoise);
    loadStr(@"Chroma",    self.edtChroma);
    loadStr(@"PsfIters",  self.edtPsfIters);
    loadStr(@"RgbPsf",    self.edtRgbPsf);
    loadStr(@"PsfSigma",  self.edtPsfSigma);
    loadStr(@"Deghost",   self.edtDeghost);
    loadStr(@"EvOffset",  self.edtEv);
    loadStr(@"GroupSize", self.edtGrpSz);
    loadStr(@"Threads",   self.edtThreads);

    self.chkAutoExp.state = [ud boolForKey:@"AutoExp"] ? NSControlStateValueOn : NSControlStateValueOff;
    self.chkRolloff.state = [ud boolForKey:@"Rolloff"] ? NSControlStateValueOn : NSControlStateValueOff;
    self.chkEdgePol.state = [ud boolForKey:@"EdgePol"] ? NSControlStateValueOn : NSControlStateValueOff;
    self.chkDither.state  = [ud boolForKey:@"Dither"]  ? NSControlStateValueOn : NSControlStateValueOff;
}

- (void)onOptionChanged:(id)sender {
    (void)sender;
    [self saveUiPrefs];
}

- (void)application:(NSApplication*)sender openFiles:(NSArray<NSString*>*)filenames {
    (void)sender;
    if (filenames.count > 0) [self addInputPaths:filenames];
    [NSApp replyToOpenOrPrint:NSApplicationDelegateReplySuccess];
}

- (void)onSelectPolicy:(NSButton*)sender {
    for (NSButton* b in self.radPolicies) b.state = (b == sender) ? NSControlStateValueOn : NSControlStateValueOff;
    [self saveUiPrefs];
}

- (void)onSelectMotion:(NSButton*)sender {
    self.customAnchor1Based = 0;
    self.radMotions[0].title = @"自动最锐";
    for (NSButton* b in self.radMotions) b.state = (b == sender) ? NSControlStateValueOn : NSControlStateValueOff;
    [self saveUiPrefs];
}

- (void)applyPresetIndex:(NSUInteger)idx {
    for (NSUInteger i = 0; i < self.radPresets.count; ++i) {
        self.radPresets[i].state = (i == idx) ? NSControlStateValueOn : NSControlStateValueOff;
    }
    if (idx == 1) { // Sci 科学
        self.edtDenoise.stringValue = @"0.40";
        self.edtChroma.stringValue = @"0.55";
        self.edtPsfIters.stringValue = @"0";
        self.edtRgbPsf.stringValue = @"0";
        self.edtPsfSigma.stringValue = @"1.05";
        self.edtDeghost.stringValue = @"1.35";
        self.chkAutoExp.state = NSControlStateValueOff;
        self.chkRolloff.state = NSControlStateValueOff;
        self.chkEdgePol.state = NSControlStateValueOff;
        self.chkDither.state  = NSControlStateValueOn;
    } else if (idx == 2) { // Arc 归档
        self.edtDenoise.stringValue = @"0.70";
        self.edtChroma.stringValue = @"0.85";
        self.edtPsfIters.stringValue = @"0";
        self.edtRgbPsf.stringValue = @"0";
        self.edtPsfSigma.stringValue = @"1.05";
        self.edtDeghost.stringValue = @"1.10";
        self.chkAutoExp.state = NSControlStateValueOn;
        self.chkRolloff.state = NSControlStateValueOn;
        self.chkEdgePol.state = NSControlStateValueOff;
        self.chkDither.state  = NSControlStateValueOn;
    } else if (idx == 3) { // Astro 星空
        self.edtDenoise.stringValue = @"0.85";
        self.edtChroma.stringValue = @"0.90";
        self.edtPsfIters.stringValue = @"3";
        self.edtRgbPsf.stringValue = @"1";
        self.edtPsfSigma.stringValue = @"0.95";
        self.edtDeghost.stringValue = @"0.85";
        self.chkAutoExp.state = NSControlStateValueOn;
        self.chkRolloff.state = NSControlStateValueOn;
        self.chkEdgePol.state = NSControlStateValueOff;
        self.chkDither.state  = NSControlStateValueOn;
    } else { // Photo 照片
        self.edtDenoise.stringValue = @"1.00";
        self.edtChroma.stringValue = @"1.00";
        self.edtPsfIters.stringValue = @"4";
        self.edtRgbPsf.stringValue = @"2";
        self.edtPsfSigma.stringValue = @"1.05";
        self.edtDeghost.stringValue = @"1.00";
        self.chkAutoExp.state = NSControlStateValueOn;
        self.chkRolloff.state = NSControlStateValueOn;
        self.chkEdgePol.state = NSControlStateValueOn;
        self.chkDither.state  = NSControlStateValueOn;
    }
}

- (void)onSelectPreset:(NSButton*)sender {
    NSUInteger idx = [self.radPresets indexOfObject:sender];
    if (idx == NSNotFound) idx = 0;
    [self applyPresetIndex:idx];
    [self saveUiPrefs];
}

- (void)onSelectCs:(NSButton*)sender {
    self.radCsSrgb.state  = (sender == self.radCsSrgb)  ? NSControlStateValueOn : NSControlStateValueOff;
    self.radCsAdobe.state = (sender == self.radCsAdobe) ? NSControlStateValueOn : NSControlStateValueOff;
    [self saveUiPrefs];
}

- (void)onAddFiles:(id)sender {
    (void)sender;
    NSOpenPanel* op = [NSOpenPanel openPanel];
    op.canChooseFiles = YES;
    op.canChooseDirectories = NO;
    op.allowsMultipleSelection = YES;
    if ([op runModal] == NSModalResponseOK) {
        NSMutableArray<NSString*>* p = [NSMutableArray array];
        for (NSURL* u in op.URLs) if (u.path) [p addObject:u.path];
        [self addInputPaths:p];
    }
}

- (void)onAddFolder:(id)sender {
    (void)sender;
    NSOpenPanel* op = [NSOpenPanel openPanel];
    op.canChooseFiles = NO;
    op.canChooseDirectories = YES;
    op.allowsMultipleSelection = YES;
    if ([op runModal] == NSModalResponseOK) {
        NSMutableArray<NSString*>* p = [NSMutableArray array];
        for (NSURL* u in op.URLs) if (u.path) [p addObject:u.path];
        [self addInputPaths:p];
    }
}

- (void)onRemoveSelected:(id)sender {
    (void)sender;
    NSIndexSet* idx = self.tableView.selectedRowIndexes;
    if (idx.count > 0) {
        [self.inputs removeObjectsAtIndexes:idx];
        [self.tableView reloadData];
        [self updateBanner];
    }
}

- (void)onClear:(id)sender {
    (void)sender;
    [self.inputs removeAllObjects];
    [self.tableView reloadData];
    [self updateBanner];
}

- (void)onBrowseOutDir:(id)sender {
    (void)sender;
    NSOpenPanel* op = [NSOpenPanel openPanel];
    op.canChooseFiles = NO;
    op.canChooseDirectories = YES;
    op.canCreateDirectories = YES;
    if ([op runModal] == NSModalResponseOK && op.URL.path) self.edtOutDir.stringValue = op.URL.path;
}

- (void)onOpenOutDir:(id)sender {
    (void)sender;
    NSString* d = self.edtOutDir.stringValue;
    if (d.length > 0) [[NSWorkspace sharedWorkspace] openURL:[NSURL fileURLWithPath:d]];
}

- (void)onCancelJob:(id)sender {
    (void)sender;
    if (self.currentTask && self.currentTask.isRunning) {
        [self.currentTask terminate];
        [self appendLog:@"[Stopped]"];
    }
}

- (NSArray<NSString*>*)buildBaseArgs {
    NSMutableArray<NSString*>* a = [NSMutableArray array];
    NSString* lrPath = [self.edtLibRawPath.stringValue stringByTrimmingCharactersInSet:[NSCharacterSet whitespaceAndNewlineCharacterSet]];
    if (lrPath.length > 0) {
        [a addObjectsFromArray:@[@"--libraw", lrPath]];
        [nijinPrefs() setObject:lrPath forKey:kPrefLibRawPath];
    }

    [a addObject:(self.chkSortTime.state == NSControlStateValueOn) ? @"--sort-time" : @"--no-sort-time"];
    if (self.chkRecursive.state == NSControlStateValueOn) [a addObject:@"--recursive"];

    NSArray<NSString*>* polNames = @[@"interactive", @"auto-prune", @"multi-group", @"force-all"];
    for (NSUInteger i = 0; i < 4; ++i) {
        if (self.radPolicies[i].state == NSControlStateValueOn) { [a addObjectsFromArray:@[@"--policy", polNames[i]]]; break; }
    }
    NSArray<NSString*>* preNames = @[@"photo", @"sci", @"arc", @"astro"];
    for (NSUInteger i = 0; i < self.radPresets.count; ++i) {
        if (self.radPresets[i].state == NSControlStateValueOn) { [a addObjectsFromArray:@[@"--preset", preNames[i]]]; break; }
    }
    if (self.customAnchor1Based > 0) {
        [a addObjectsFromArray:@[@"--anchor-frame", [NSString stringWithFormat:@"%d", self.customAnchor1Based]]];
    } else {
        NSArray<NSString*>* ancNames = @[@"auto", @"first", @"middle"];
        for (NSUInteger i = 0; i < 3; ++i) {
            if (self.radMotions[i].state == NSControlStateValueOn) { [a addObjectsFromArray:@[@"--anchor-frame", ancNames[i]]]; break; }
        }
    }
    NSArray<NSString*>* bkNames = @[@"auto", @"gpu", @"cpu"];
    NSInteger bIdx = self.popBackend.indexOfSelectedItem;
    if (bIdx >= 0 && bIdx < 3) [a addObjectsFromArray:@[@"--backend", bkNames[(NSUInteger)bIdx]]];

    auto addKv = ^(NSString* k, NSTextField* tf) {
        if (tf.stringValue.length > 0) [a addObjectsFromArray:@[k, tf.stringValue]];
    };
    addKv(@"--denoise-strength", self.edtDenoise);
    addKv(@"--chroma-denoise",   self.edtChroma);
    addKv(@"--psf-iters",        self.edtPsfIters);
    addKv(@"--rgb-psf-iters",    self.edtRgbPsf);
    addKv(@"--psf-sigma",        self.edtPsfSigma);
    addKv(@"--deghost-strength", self.edtDeghost);
    addKv(@"--ev-offset",        self.edtEv);
    addKv(@"--group-size",       self.edtGrpSz);
    if (self.edtThreads.integerValue > 0) [a addObjectsFromArray:@[@"--threads", self.edtThreads.stringValue]];

    [a addObjectsFromArray:@[@"--auto-exposure",     self.chkAutoExp.state == NSControlStateValueOn ? @"on" : @"off"]];
    [a addObjectsFromArray:@[@"--highlight-rolloff", self.chkRolloff.state == NSControlStateValueOn ? @"on" : @"off"]];
    [a addObjectsFromArray:@[@"--edge-polish",       self.chkEdgePol.state == NSControlStateValueOn ? @"on" : @"off"]];
    [a addObjectsFromArray:@[@"--dither",            self.chkDither.state  == NSControlStateValueOn ? @"on" : @"off"]];

    NSArray<NSString*>* rawFmts = @[@"dng", @"dng-hq", @"dng-fp16", @"dng-raw"];
    NSInteger rIdx = self.popRawFmt.indexOfSelectedItem;
    if (rIdx >= 0 && rIdx < 4) [a addObjectsFromArray:@[@"--raw-format", rawFmts[(NSUInteger)rIdx]]];

    NSArray<NSString*>* rgbFmts = @[@"tiff-deflate", @"png", @"jxs", @"jxs-lossless", @"tiff-raw"];
    NSInteger cIdx = self.popRgbFmt.indexOfSelectedItem;
    if (cIdx >= 0 && cIdx < 5) [a addObjectsFromArray:@[@"--rgb-format", rgbFmts[(NSUInteger)cIdx]]];
    addKv(@"--jxs-bpp", self.edtJxsBpp);

    NSArray<NSString*>* fieldModes = @[@"split", @"rgb", @"motion", @"phase", @"focus"];
    NSInteger fmIdx = self.popFieldMode.indexOfSelectedItem;
    if (fmIdx >= 0 && fmIdx < 5) [a addObjectsFromArray:@[@"--field-mode", fieldModes[(NSUInteger)fmIdx]]];

    [a addObjectsFromArray:@[@"--color-space", self.radCsAdobe.state == NSControlStateValueOn ? @"adobergb" : @"srgb"]];
    return a;
}

// Returns: 0 = cancel/close, 1 = apply prescription & run, 2 = keep current & run, 3 = applied to UI only
- (int)runSmartWizardModal:(NSString*)cliPath baseArgs:(NSArray<NSString*>*)baseArgs alwaysShow:(BOOL)alwaysShow {
    NSTask* t = [[NSTask alloc] init];
    t.executableURL = [NSURL fileURLWithPath:cliPath];
    NSMutableArray<NSString*>* args = [baseArgs mutableCopy];
    [args addObject:@"--plan-json"];
    [args addObjectsFromArray:self.inputs];
    t.arguments = args;
    NSPipe* pipe = [NSPipe pipe];
    t.standardOutput = pipe;
    if (![t launchAndReturnError:nil]) return 2;
    NSData* data = [[pipe fileHandleForReading] readDataToEndOfFile];
    [t waitUntilExit];
    std::string out((const char*)data.bytes, data.length);
    if (out.find("\"clusters\":[") == std::string::npos) return 2;

    bool needsPrompt = (out.find("\"needs_prompt\":true") != std::string::npos);
    if (!alwaysShow && !needsPrompt) return 2;

    std::string sceneCode  = extractJsonStr(out, "scene");
    std::string recPreset  = extractJsonStr(out, "rec_preset");
    std::string recAnchor  = extractJsonStr(out, "rec_anchor");
    std::string rxSummary  = extractJsonStr(out, "rx_summary");
    int bestAnchor         = extractJsonInt(out, "best_anchor", 1);
    int quadCov            = extractJsonInt(out, "quad_coverage", 1);
    int srScore            = extractJsonInt(out, "sr_score", 80);
    int starCnt            = extractJsonInt(out, "star_count", 0);
    double movPct          = extractJsonDouble(out, "motion_pct", 0.0);
    double recDen          = extractJsonDouble(out, "rec_denoise", 1.0);
    double recDegh         = extractJsonDouble(out, "rec_deghost", 1.0);
    int recPsf             = extractJsonInt(out, "rec_psf_iters", 4);
    double estMb           = extractJsonDouble(out, "est_dng_mb", 130.0);
    bool shock1            = (out.find("\"shutter_shock\":true") != std::string::npos);

    // Parse per-frame list from "frames":[ ... ]
    NSMutableArray<NSString*>* frameTitles = [NSMutableArray arrayWithObjects:
        [NSString stringWithFormat:@"自动最锐（皇冠帧 #%d）", bestAnchor],
        @"首帧定格（第 #1 张）",
        @"中间定格（序列居中帧）", nil];
    NSMutableArray<NSNumber*>* frameCodes = [NSMutableArray arrayWithObjects:@0, @(-1), @(-2), nil];

    size_t fPos = out.find("\"frames\":[");
    size_t fEnd = (fPos != std::string::npos) ? out.find("]", fPos) : std::string::npos;
    if (fPos != std::string::npos && fEnd != std::string::npos) {
        size_t cur = fPos;
        while ((cur = out.find("{\"idx\":", cur)) != std::string::npos && cur < fEnd) {
            int fIdx = extractJsonInt(out, "idx", 1, cur);
            std::string fFile = extractJsonStr(out, "file", cur);
            std::string fFlags = extractJsonStr(out, "flags", cur);
            double fAcu = extractJsonDouble(out, "acutance", 0.0, cur);
            double fSh  = extractJsonDouble(out, "shift", 0.0, cur);
            size_t nextBrace = out.find("}", cur);
            bool isCrown = (nextBrace != std::string::npos && out.substr(cur, nextBrace - cur).find("\"crown\":true") != std::string::npos);
            NSString* item = [NSString stringWithFormat:@"指定第 #%d 张：%s（锐度 %.1f | 位移 %.2fpx%@%@）",
                fIdx, fFile.c_str(), fAcu, fSh,
                isCrown ? @" ★最锐" : @"",
                (fFlags != "OK") ? [NSString stringWithFormat:@" [%s]", fFlags.c_str()] : @""];
            [frameTitles addObject:item];
            [frameCodes addObject:@(fIdx)];
            cur += 7;
        }
    }

    NSView* acc = [[NSView alloc] initWithFrame:NSMakeRect(0, 0, 480, 178)];
    NSTextField* lScene = [NSTextField labelWithString:[NSString stringWithFormat:
        @"识别场景：%@   |   超分潜力：%d/100（%d/4 象限）",
        sceneCodeToCn(sceneCode), srScore, quadCov]];
    lScene.frame = NSMakeRect(0, 150, 480, 22);
    lScene.font = [NSFont boldSystemFontOfSize:13.0];
    lScene.textColor = [NSColor colorWithSRGBRed:0.16 green:0.35 blue:0.43 alpha:1.0];
    [acc addSubview:lScene];

    NSTextField* lTele = [NSTextField labelWithString:[NSString stringWithFormat:
        @"动体面积：%.1f%%   |   检出星点：%d   |   预计 2× 无损 DNG：~%.0f MiB%@",
        movPct, starCnt, estMb, shock1 ? @"   |   ⚠️ 首帧快门微震" : @""]];
    lTele.frame = NSMakeRect(0, 126, 480, 20);
    lTele.font = [NSFont systemFontOfSize:12.0];
    [acc addSubview:lTele];

    NSString* preCn = (recPreset == "astro") ? @"Astro 星空"
                    : (recPreset == "sci")   ? @"Sci 科学"
                    : (recPreset == "arc")   ? @"Arc 归档" : @"Photo 照片";
    NSTextField* lRx = [NSTextField wrappingLabelWithString:[NSString stringWithFormat:
        @"推荐处方：预设【%@】 · 亮度降噪 %.2f · 去鬼影 %.2f · RAW RL %d 次\n诊断依据：%s",
        preCn, recDen, recDegh, recPsf, rxSummary.c_str()]];
    lRx.frame = NSMakeRect(0, 56, 480, 62);
    lRx.font = [NSFont systemFontOfSize:12.0];
    [acc addSubview:lRx];

    NSTextField* lPick = [NSTextField labelWithString:@"运动/地景定格帧："];
    lPick.frame = NSMakeRect(0, 14, 118, 22);
    lPick.font = [NSFont boldSystemFontOfSize:12.0];
    [acc addSubview:lPick];

    NSPopUpButton* popAnc = [[NSPopUpButton alloc] initWithFrame:NSMakeRect(118, 10, 362, 26) pullsDown:NO];
    [popAnc addItemsWithTitles:frameTitles];
    if (recAnchor == "middle") [popAnc selectItemAtIndex:2];
    else if (recAnchor == "first") [popAnc selectItemAtIndex:1];
    else if (std::atoi(recAnchor.c_str()) >= 1) {
        int want = std::atoi(recAnchor.c_str());
        for (NSUInteger k = 3; k < frameCodes.count; ++k) {
            if (frameCodes[k].intValue == want) { [popAnc selectItemAtIndex:(NSInteger)k]; break; }
        }
    } else {
        [popAnc selectItemAtIndex:0];
    }
    [acc addSubview:popAnc];

    NSAlert* al = [[NSAlert alloc] init];
    al.messageText = @"智能诊断与方案向导";
    al.informativeText = alwaysShow ? @"已完成毫秒级五维场景体检，可一键采纳处方或任选单帧定格：" : @"检测到当前序列可通过专用场景处方获得更佳合成画质：";
    al.accessoryView = acc;
    [al addButtonWithTitle:@"采纳方案并开始合成"];
    [al addButtonWithTitle:@"仅填入推荐参数"];
    [al addButtonWithTitle:alwaysShow ? @"关闭" : @"按当前面板参数运行"];

    NSModalResponse resp = [al runModal];
    auto applyRxToUi = ^{
        NSUInteger pIdx = (recPreset == "sci") ? 1 : (recPreset == "arc") ? 2 : (recPreset == "astro") ? 3 : 0;
        [self applyPresetIndex:pIdx];
        self.edtDenoise.stringValue  = [NSString stringWithFormat:@"%.2f", recDen];
        self.edtDeghost.stringValue  = [NSString stringWithFormat:@"%.2f", recDegh];
        self.edtPsfIters.stringValue = [NSString stringWithFormat:@"%d", recPsf];

        NSInteger sel = popAnc.indexOfSelectedItem;
        int code = (sel >= 0 && (NSUInteger)sel < frameCodes.count) ? frameCodes[(NSUInteger)sel].intValue : 0;
        if (code >= 1) {
            self.customAnchor1Based = code;
            for (NSButton* b in self.radMotions) b.state = NSControlStateValueOff;
            self.radMotions[0].state = NSControlStateValueOn;
            self.radMotions[0].title = [NSString stringWithFormat:@"指定 #%d", code];
        } else {
            self.customAnchor1Based = 0;
            self.radMotions[0].title = @"自动最锐";
            NSUInteger mIdx = (code == -1) ? 1 : (code == -2) ? 2 : 0;
            for (NSUInteger i = 0; i < 3; ++i) {
                self.radMotions[i].state = (i == mIdx) ? NSControlStateValueOn : NSControlStateValueOff;
            }
        }
    };

    if (resp == NSAlertFirstButtonReturn) {
        applyRxToUi();
        return 1;
    }
    if (resp == NSAlertSecondButtonReturn) {
        applyRxToUi();
        [self appendLog:@"[智能向导] 已将推荐处方填入控制面板。"];
        return 3;
    }
    return alwaysShow ? 0 : 2;
}

- (void)launchJob:(BOOL)wizardInspectOnly {
    if (self.currentTask && self.currentTask.isRunning) return;
    [self saveUiPrefs];
    if (self.inputs.count == 0) {
        NSAlert* a = [[NSAlert alloc] init];
        a.messageText = @"请先添加 RAW 文件或目录。";
        [a runModal];
        return;
    }

    NSString* cliPath = [self resolveActiveCliOrPrompt];
    if (!cliPath) return;

    NSString* lrPath = [self.edtLibRawPath.stringValue stringByTrimmingCharactersInSet:[NSCharacterSet whitespaceAndNewlineCharacterSet]];
    if (lrPath.length > 0 && ![[NSFileManager defaultManager] fileExistsAtPath:lrPath]) {
        NSAlert* a = [[NSAlert alloc] init];
        a.messageText = @"自定义 LibRaw 文件不存在";
        a.informativeText = lrPath;
        [a runModal];
        return;
    }

    if (wizardInspectOnly) {
        NSArray<NSString*>* baseArgs = [self buildBaseArgs];
        int wres = [self runSmartWizardModal:cliPath baseArgs:baseArgs alwaysShow:YES];
        if (wres != 1) return; // 0=closed, 3=applied to UI only; 1=proceed directly to synthesis!
    }

    BOOL wantRaw = (self.chkEmitRaw.state == NSControlStateValueOn);
    BOOL wantRgb = (self.chkEmitRgb.state == NSControlStateValueOn);
    BOOL wantFld = (self.chkEmitField.state == NSControlStateValueOn);
    if (!wantRaw && !wantRgb && !wantFld) {
        NSAlert* a = [[NSAlert alloc] init];
        a.messageText = @"请至少勾选一项输出（2× RGGB / 2× RGB / 输出场图）。";
        [a runModal];
        return;
    }

    NSMutableArray<NSString*>* args = [[self buildBaseArgs] mutableCopy];
    BOOL isInteractive = (self.radPolicies[0].state == NSControlStateValueOn);
    if (!wizardInspectOnly && isInteractive) {
        int pick = [self runSmartWizardModal:cliPath baseArgs:args alwaysShow:NO];
        if (pick == 0 || pick == 3) return;
        args = [[self buildBaseArgs] mutableCopy];
        [args addObjectsFromArray:@[@"--interactive-choice", (pick == 1) ? @"1" : @"2"]];
    } else if (wizardInspectOnly && isInteractive) {
        [args addObjectsFromArray:@[@"--interactive-choice", @"1"]];
    }

    [args addObject:@"--progress-json"];
    if (self.edtOutDir.stringValue.length == 0) {
        self.edtOutDir.stringValue = [[self.inputs.firstObject stringByDeletingLastPathComponent] stringByAppendingPathComponent:@"nijinLBS_out"];
    }
    [args addObjectsFromArray:@[@"--out-dir", self.edtOutDir.stringValue]];
    if (wantRaw) [args addObject:@"--emit-raw"];
    if (wantRgb) [args addObject:@"--emit-rgb"];
    if (wantFld) [args addObject:@"--emit-field"];
    [args addObjectsFromArray:self.inputs];

    self.progBar.doubleValue = 0;
    self.pctLabel.stringValue = @"0%";
    self.btnRun.enabled = NO;
    [self.btnRun setNeedsDisplay:YES];
    self.btnInspect.enabled = NO;
    self.btnCancel.enabled = YES;

    [self appendLog:[NSString stringWithFormat:@"> %@ %@", cliPath, [args componentsJoinedByString:@" "]]];

    NSTask* task = [[NSTask alloc] init];
    task.executableURL = [NSURL fileURLWithPath:cliPath];
    task.arguments = args;
    NSPipe* pipe = [NSPipe pipe];
    task.standardOutput = pipe;
    task.standardError = pipe;
    self.currentTask = task;

    NSFileHandle* fh = [pipe fileHandleForReading];
    __weak AppDelegate* weakSelf = self;
    fh.readabilityHandler = ^(NSFileHandle* handle) {
        NSData* d = [handle availableData];
        if (d.length == 0) return;
        NSString* chunk = [[NSString alloc] initWithData:d encoding:NSUTF8StringEncoding];
        if (!chunk) return;
        for (NSString* line in [chunk componentsSeparatedByCharactersInSet:[NSCharacterSet newlineCharacterSet]]) {
            if (line.length == 0) continue;
            std::string s = [line UTF8String];
            if (!s.empty() && s.front() == '{' && s.find("\"event\":") != std::string::npos) {
                int pct = extractJsonInt(s, "percent", -1);
                if (pct >= 0) {
                    dispatch_async(dispatch_get_main_queue(), ^{
                        weakSelf.progBar.doubleValue = pct;
                        weakSelf.pctLabel.stringValue = [NSString stringWithFormat:@"%d%%", pct];
                    });
                }
            } else {
                [weakSelf appendLog:line];
            }
        }
    };

    task.terminationHandler = ^(NSTask* finished) {
        fh.readabilityHandler = nil;
        dispatch_async(dispatch_get_main_queue(), ^{
            weakSelf.btnRun.enabled = YES;
            [weakSelf.btnRun setNeedsDisplay:YES];
            weakSelf.btnInspect.enabled = YES;
            weakSelf.btnCancel.enabled = NO;
            if (finished.terminationStatus == 0) {
                weakSelf.progBar.doubleValue = 100;
                weakSelf.pctLabel.stringValue = @"100%";
            }
            [weakSelf appendLog:[NSString stringWithFormat:@"[Exit %d]", finished.terminationStatus]];
        });
    };

    NSError* err = nil;
    if (![task launchAndReturnError:&err]) {
        self.btnRun.enabled = YES;
        [self.btnRun setNeedsDisplay:YES];
        self.btnInspect.enabled = YES;
        self.btnCancel.enabled = NO;
        [self appendLog:[NSString stringWithFormat:@"[Error] %@", err.localizedDescription]];
    }
}

- (void)onRun:(id)sender     { (void)sender; [self launchJob:NO]; }
- (void)onInspect:(id)sender { (void)sender; [self launchJob:YES]; }

- (void)applicationWillFinishLaunching:(NSNotification*)notification {
    (void)notification;
    [self setupMainMenu];
}

- (void)applicationDidFinishLaunching:(NSNotification*)notification {
    (void)notification;

    self.inputs = [NSMutableArray array];
    self.hoverDots = [NSMutableArray array];

    NSImage* appIcon = loadEmbeddedPng(heraw_brand::kAppIconPng, sizeof(heraw_brand::kAppIconPng), NSMakeSize(256, 256));
    if (appIcon) [NSApp setApplicationIconImage:appIcon];

    NSRect frame = NSMakeRect(0, 0, 1020, 706);
    self.window = [[DropWindow alloc] initWithContentRect:frame
        styleMask:(NSWindowStyleMaskTitled | NSWindowStyleMaskClosable | NSWindowStyleMaskMiniaturizable)
        backing:NSBackingStoreBuffered defer:NO];
    self.window.title = @"NijiN.LiteBurstScaler (nijinLBS)";
    self.window.appearance = [NSAppearance appearanceNamed:NSAppearanceNameAqua];
    [self.window registerForDraggedTypes:@[NSPasteboardTypeFileURL]];
    __weak AppDelegate* weakSelf = self;
    self.window.onDropPaths = ^(NSArray<NSString*>* paths) { [weakSelf addInputPaths:paths]; };

    self.canvas = [[StudioCanvasView alloc] initWithFrame:frame];
    self.canvas.gxLogo = loadEmbeddedPng(heraw_brand::kGxStarPng2x, sizeof(heraw_brand::kGxStarPng2x),
                                         NSMakeSize(heraw_brand::kGx1xW, heraw_brand::kGx1xH));
    self.canvas.nijinLockup = loadEmbeddedPng(heraw_brand::kNijinLockupPng2x, sizeof(heraw_brand::kNijinLockupPng2x),
                                              NSMakeSize(heraw_brand::kNijin1xW, heraw_brand::kNijin1xH));
    self.window.contentView = self.canvas;
    NSView* cv = self.canvas;

    NSColor* textMain = [NSColor colorWithSRGBRed:0.14 green:0.20 blue:0.24 alpha:1.0];

    auto lbl = ^(NSString* t, CGFloat x, CGFloat y, CGFloat w, CGFloat sz, BOOL bold, NSColor* col) {
        NSTextField* f = [NSTextField labelWithString:t];
        f.frame = NSMakeRect(x, y, w, sz + 7);
        f.font = bold ? [NSFont boldSystemFontOfSize:sz] : [NSFont systemFontOfSize:sz];
        f.textColor = col ?: textMain;
        [cv addSubview:f];
        return f;
    };
    auto edt = ^(NSString* val, NSString* ph, CGFloat x, CGFloat y, CGFloat w) {
        NSTextField* f = [[NSTextField alloc] initWithFrame:NSMakeRect(x, y, w, 24)];
        f.stringValue = val ?: @"";
        if (ph) f.placeholderString = ph;
        f.font = [NSFont systemFontOfSize:12];
        [cv addSubview:f];
        return f;
    };
    auto btn = ^(NSString* t, CGFloat x, CGFloat y, CGFloat w, CGFloat h, SEL act) {
        NSButton* b = [NSButton buttonWithTitle:t target:self action:act];
        b.frame = NSMakeRect(x, y, w, h);
        b.font = [NSFont systemFontOfSize:12];
        [cv addSubview:b];
        return b;
    };
    auto chk = ^(NSString* t, CGFloat x, CGFloat y, CGFloat w, BOOL on) {
        NSButton* b = [NSButton checkboxWithTitle:t target:self action:@selector(onOptionChanged:)];
        b.frame = NSMakeRect(x, y, w, 22);
        b.font = [NSFont systemFontOfSize:12];
        b.state = on ? NSControlStateValueOn : NSControlStateValueOff;
        [cv addSubview:b];
        return b;
    };
    auto rad = ^(NSString* t, CGFloat x, CGFloat y, CGFloat w, BOOL bold, SEL act) {
        NSButton* b = [NSButton radioButtonWithTitle:t target:self action:act];
        b.frame = NSMakeRect(x, y, w, 22);
        b.font = bold ? [NSFont systemFontOfSize:12.5 weight:NSFontWeightMedium] : [NSFont systemFontOfSize:12];
        [cv addSubview:b];
        return b;
    };
    auto dot = ^(CGFloat x, CGFloat y, NSString* title, NSString* body) {
        HoverDotView* d = [[HoverDotView alloc] initWithFrame:NSMakeRect(x, y, 18, 18) title:title body:body];
        [cv addSubview:d];
        [self.hoverDots addObject:d];
        return d;
    };

    // Header Left Title (clean single line, no subtitle clutter)
    lbl(@"NijiN.LiteBurstScaler", 24, 19, 218, 17.5, YES,
        [NSColor colorWithSRGBRed:0.13 green:0.20 blue:0.24 alpha:1.0]);

    // === Card 01: Input ===
    lbl(@"输入源", 66, 89, 72, 13.0, YES, nil);
    self.bannerLabel = lbl(@"", 126, 91, 326, 11.5, NO, nil);
    dot(464, 90, @"输入与连拍张数",
        @"• 最低门槛：≥ 4 张有效帧\n"
        @"• 推荐张数：手持 6–8 张，高感 9–12 张\n"
        @"• 内置 G1/G2 盲暗电平自标定与热噪点修复");

    NSScrollView* sv = [[NSScrollView alloc] initWithFrame:NSMakeRect(34, 118, 448, 136)];
    sv.hasVerticalScroller = YES;
    sv.borderType = NSBezelBorder;
    self.tableView = [[NSTableView alloc] initWithFrame:sv.bounds];
    self.tableView.rowHeight = 20;
    NSTableColumn* col = [[NSTableColumn alloc] initWithIdentifier:@"PathCol"];
    col.width = 424;
    [self.tableView addTableColumn:col];
    self.tableView.headerView = nil;
    self.tableView.allowsMultipleSelection = YES;
    self.tableView.dataSource = self;
    self.tableView.delegate = self;
    sv.documentView = self.tableView;
    [cv addSubview:sv];

    btn(@"添加文件...", 34,  262, 110, 28, @selector(onAddFiles:));
    btn(@"添加目录...", 150, 262, 110, 28, @selector(onAddFolder:));
    btn(@"移除",        266, 262, 104, 28, @selector(onRemoveSelected:));
    btn(@"清空",        376, 262, 106, 28, @selector(onClear:));

    self.chkSortTime  = chk(@"EXIF 时间排序", 34,  300, 136, YES);
    self.chkRecursive = chk(@"递归子目录",     178, 300, 112, NO);
    self.btnInspect   = btn(@"智能向导",       354, 297, 128, 28, @selector(onInspect:));

    // === Card 02 & 03: Sequence Policy, Presets & Pose Anchor ===
    lbl(@"序列策略", 66, 363, 200, 13.0, YES, nil);
    dot(464, 364, @"序列策略与智能向导",
        @"• 智能向导：五维识别星空/星野/AEB/三脚架/动体，推荐处方并支持逐帧选定格\n"
        @"• 自动筛除：自动剔除糊片/切镜/曝光异常，保留 ≥4 张\n"
        @"• 自动多组：长连拍按每组帧数自动切分多组输出\n"
        @"• 强制全选：忽略张数与位移门槛（可至 1 张）");

    NSButton* rp0 = rad(@"智能向导", 36,  392, 180, NO, @selector(onSelectPolicy:));
    NSButton* rp1 = rad(@"自动筛除", 252, 392, 180, NO, @selector(onSelectPolicy:));
    NSButton* rp2 = rad(@"自动多组", 36,  418, 180, NO, @selector(onSelectPolicy:));
    NSButton* rp3 = rad(@"强制全选", 252, 418, 180, NO, @selector(onSelectPolicy:));
    self.radPolicies = @[rp0, rp1, rp2, rp3];
    rp0.state = NSControlStateValueOn;

    lbl(@"预设与定格", 66, 461, 200, 13.0, YES, nil);
    dot(464, 462, @"四档模式预设、定格帧与物理细节重建",
        @"预设：\n"
        @"• Photo 照片：时域相干保微纹理 + 刚体微块超分 + PSF 恢复 + 自动影调\n"
        @"• Sci 科学：关闭 PSF / 自动曝光 / 高光滚降 / 边缘抛光，严格线性真值\n"
        @"• Arc 归档：关闭 PSF 与边缘抛光，温和降噪与自然影调\n"
        @"• Astro 星空：防吞星跨通道门控 + 保星色 + 星点/地景天地双域对齐 + 夜空黑位锚定\n\n"
        @"定格帧（决定运动物体或天际线姿态，也可点「智能向导」任选第 #1..N 张）：\n"
        @"• 自动最锐 / 首帧定格 / 中间定格");

    lbl(@"预设", 36, 494, 36, 12.0, NO, nil);
    NSButton* pp0 = rad(@"Photo 照片", 74,  492, 100, YES, @selector(onSelectPreset:));
    NSButton* pp1 = rad(@"Sci 科学",   180, 492, 86,  YES, @selector(onSelectPreset:));
    NSButton* pp2 = rad(@"Arc 归档",   270, 492, 86,  YES, @selector(onSelectPreset:));
    NSButton* pp3 = rad(@"Astro 星空", 360, 492, 108, YES, @selector(onSelectPreset:));
    self.radPresets = @[pp0, pp1, pp2, pp3];
    pp0.state = NSControlStateValueOn;

    lbl(@"定格", 36, 526, 40, 12.0, NO, nil);
    NSButton* mp0 = rad(@"自动最锐", 82,  524, 122, NO, @selector(onSelectMotion:));
    NSButton* mp1 = rad(@"首帧定格", 216, 524, 114, NO, @selector(onSelectMotion:));
    NSButton* mp2 = rad(@"中间定格", 344, 524, 114, NO, @selector(onSelectMotion:));
    self.radMotions = @[mp0, mp1, mp2];
    mp0.state = NSControlStateValueOn;

    // === Card 04: Output ===
    lbl(@"输出", 562, 89, 200, 13.0, YES, nil);
    dot(968, 90, @"按需输出与元数据",
        @"• 支持单选 2× RGGB、单选 2× RGB 或同时输出\n"
        @"• 保留参考帧 EXIF / GPS，Adobe RGB 自动内嵌 ICC v2.4");

    lbl(@"输出目录", 530, 123, 62, 12.0, NO, nil);
    self.edtOutDir = edt(@"", @"默认同目录 nijinLBS_out", 596, 120, 304);
    btn(@"浏览", 906, 118, 80, 28, @selector(onBrowseOutDir:));

    self.chkEmitRaw = chk(@"2× RGGB", 530, 158, 94, YES);
    self.popRawFmt = [[NSPopUpButton alloc] initWithFrame:NSMakeRect(626, 155, 330, 26) pullsDown:NO];
    [self.popRawFmt addItemsWithTitles:@[
        @"DNG 16-bit 无损",
        @"DNG 高级有损压缩",
        @"DNG 16-bit 半精度浮点",
        @"DNG 16-bit 未压缩"
    ]];
    self.popRawFmt.target = self;
    self.popRawFmt.action = @selector(onOptionChanged:);
    [cv addSubview:self.popRawFmt];
    dot(968, 159, @"RGGB 倍率参考（未压缩 = 1）",
        @"• DNG 16-bit 未压缩      ：1.00（~349 MiB）\n"
        @"• DNG 16-bit 无损        ：0.39（~135 MiB）\n"
        @"• DNG 16-bit 半精度浮点  ：0.29（~100 MiB）\n"
        @"• DNG 高级有损压缩       ：0.09（~30 MiB）");

    self.chkEmitRgb = chk(@"2× RGB", 530, 194, 94, YES);
    self.popRgbFmt = [[NSPopUpButton alloc] initWithFrame:NSMakeRect(626, 191, 238, 26) pullsDown:NO];
    [self.popRgbFmt addItemsWithTitles:@[
        @"16-bit 无损 TIFF",
        @"16-bit 无损 PNG",
        @"14-bit JPEG XS 高级有损压缩",
        @"14-bit JPEG XS 无损",
        @"16-bit 未压缩 TIFF"
    ]];
    self.popRgbFmt.target = self;
    self.popRgbFmt.action = @selector(onOptionChanged:);
    [cv addSubview:self.popRgbFmt];
    lbl(@"BPP", 870, 195, 30, 11.5, NO, nil);
    self.edtJxsBpp = edt(@"10.0", nil, 902, 192, 54);
    dot(968, 195, @"RGB 倍率参考（未压缩 = 1）",
        @"• 16-bit 未压缩 TIFF         ：1.00（~1046 MiB）\n"
        @"• 14-bit JPEG XS 无损        ：0.43（~450 MiB）\n"
        @"• 16-bit 无损 PNG            ：0.40（~420 MiB）\n"
        @"• 16-bit 无损 TIFF（默认）   ：0.39（~410 MiB）\n"
        @"• 14-bit JPEG XS 高级有损压缩：0.21（10 bpp, ~218 MiB）");

    lbl(@"色彩空间", 530, 234, 56, 12.0, NO, nil);
    self.radCsSrgb  = rad(@"sRGB",      590, 233, 64,  NO, @selector(onSelectCs:));
    self.radCsAdobe = rad(@"Adobe RGB", 656, 233, 96,  NO, @selector(onSelectCs:));
    self.radCsSrgb.state = NSControlStateValueOn;
    self.chkEmitField = chk(@"输出场图", 758, 233, 78, NO);
    self.popFieldMode = [[NSPopUpButton alloc] initWithFrame:NSMakeRect(838, 230, 118, 26) pullsDown:NO];
    self.popFieldMode.font = [NSFont systemFontOfSize:11.5];
    [self.popFieldMode addItemsWithTitles:@[
        @"三张灰度",
        @"合并 RGB",
        @"仅运动/天空",
        @"仅超分相位",
        @"仅光学对焦"
    ]];
    self.popFieldMode.target = self;
    self.popFieldMode.action = @selector(onOptionChanged:);
    [cv addSubview:self.popFieldMode];
    dot(968, 234, @"16-bit 物理场图导出",
        @"导出与 2× 主图逐像素对齐的 16-bit 无损 PNG 物理场图（默认分离三张单通道灰度图）：\n"
        @"• 运动 / 天空（_motion / _sky.png）：防镂空局部动体蒙版；星野模式自动输出天空/地景分割蒙版\n"
        @"• 超分相位（_phase.png）：2×2 亚像素象限覆盖与有效帧数置信度（供 Sci 训练损失加权）\n"
        @"• 光学对焦（_focus.png）：二阶拉普拉斯合焦锐度场（亮 = 合焦主体纹理，0 = 焦外散景）\n"
        @"• 可切换为「合并 RGB」输出单张 R/G/B 三通道场图，或单独导出指定场图");

    // === Card 05: Engine & Parameters ===
    lbl(@"引擎与参数", 562, 299, 200, 13.0, YES, nil);
    dot(968, 300, @"引擎、GPU 后端与微调参数",
        @"• CLI：自动搜索同目录 / 上级 / PATH 中的 nijinLBS，未找到时提示手动指定\n"
        @"• LibRaw：留空使用内置 0.23-HE*，可选外部动态库覆盖\n"
        @"• 后端：自动优先使用原生 Metal / Vulkan GPU 加速，无可用 GPU 时退回 CPU SIMD\n"
        @"• 旋钮：可在任意预设基础上单独微调（0 = 关闭对应滤波/迭代）");

    lbl(@"CLI 路径", 530, 333, 62, 12.0, NO, nil);
    self.edtCliPath = edt(@"", @"自动搜索 nijinLBS", 596, 330, 276);
    btn(@"选择", 878, 328, 52, 28, @selector(onBrowseCli:));
    btn(@"自动", 934, 328, 52, 28, @selector(onAutoDetectCli:));

    NSString* savedLr = [nijinPrefs() stringForKey:kPrefLibRawPath] ?: @"";
    lbl(@"LibRaw", 530, 367, 62, 12.0, NO, nil);
    self.edtLibRawPath = edt(savedLr, @"内置 0.23-HE*", 596, 364, 276);
    btn(@"选择", 878, 362, 52, 28, @selector(onBrowseLibRaw:));
    btn(@"默认", 934, 362, 52, 28, @selector(onResetLibRaw:));

    lbl(@"亮度降噪", 530, 416, 56, 11.5, NO, nil); self.edtDenoise  = edt(@"1.00", nil, 588, 412, 48);
    lbl(@"色度降噪", 646, 416, 56, 11.5, NO, nil); self.edtChroma   = edt(@"1.00", nil, 704, 412, 48);
    lbl(@"RAW RL",   762, 416, 52, 11.5, NO, nil); self.edtPsfIters = edt(@"4",    nil, 816, 412, 48);
    lbl(@"RGB RL",   874, 416, 52, 11.5, NO, nil); self.edtRgbPsf   = edt(@"2",    nil, 928, 412, 58);

    lbl(@"PSF 半径", 530, 450, 56, 11.5, NO, nil); self.edtPsfSigma = edt(@"1.05", nil, 588, 446, 48);
    lbl(@"去鬼影",   646, 450, 56, 11.5, NO, nil); self.edtDeghost  = edt(@"1.00", nil, 704, 446, 48);
    lbl(@"EV 偏移",  762, 450, 52, 11.5, NO, nil); self.edtEv       = edt(@"0.0",  nil, 816, 446, 48);
    lbl(@"每组帧数", 874, 450, 52, 11.5, NO, nil); self.edtGrpSz    = edt(@"8",    nil, 928, 446, 58);

    self.chkAutoExp = chk(@"自动曝光",   530, 486, 98,  YES);
    self.chkRolloff = chk(@"高光滚降",   646, 486, 98,  YES);
    self.chkEdgePol = chk(@"边缘抛光",   762, 486, 98,  YES);

    self.chkDither  = chk(@"TPDF 抖动",  530, 522, 98, YES);
    lbl(@"后端", 636, 525, 34, 11.5, NO, nil);
    self.popBackend = [[NSPopUpButton alloc] initWithFrame:NSMakeRect(670, 520, 152, 26) pullsDown:NO];
    [self.popBackend addItemsWithTitles:@[@"自动 (GPU优先)", @"强制 GPU", @"纯 CPU"]];
    self.popBackend.target = self;
    self.popBackend.action = @selector(onOptionChanged:);
    [cv addSubview:self.popBackend];
    lbl(@"线程", 832, 525, 34, 11.5, NO, nil);
    self.edtThreads = edt(@"0", nil, 868, 521, 118);

    [self applyPresetIndex:0];

    // === Card 06: Run & Console ===
    self.btnRun = [[AquaButton alloc] initWithFrame:NSMakeRect(34, 592, 148, 32)];
    self.btnRun.title = @"开始合成";
    self.btnRun.target = self;
    self.btnRun.action = @selector(onRun:);
    self.btnRun.bordered = NO;
    self.btnRun.keyEquivalent = @"\r";
    [cv addSubview:self.btnRun];

    self.btnCancel = btn(@"停止",     192, 592, 82,  32, @selector(onCancelJob:));
    self.btnCancel.enabled = NO;
    btn(@"输出目录",                  282, 592, 98,  32, @selector(onOpenOutDir:));

    self.progBar = [[NSProgressIndicator alloc] initWithFrame:NSMakeRect(396, 600, 538, 16)];
    self.progBar.indeterminate = NO;
    self.progBar.minValue = 0;
    self.progBar.maxValue = 100;
    [cv addSubview:self.progBar];
    self.pctLabel = lbl(@"0%", 942, 598, 46, 12.0, YES,
        [NSColor colorWithSRGBRed:0.25 green:0.43 blue:0.50 alpha:1.0]);

    NSScrollView* lsv = [[NSScrollView alloc] initWithFrame:NSMakeRect(34, 632, 952, 46)];
    lsv.hasVerticalScroller = YES;
    lsv.borderType = NSBezelBorder;
    self.logView = [[NSTextView alloc] initWithFrame:lsv.bounds];
    self.logView.editable = NO;
    self.logView.backgroundColor = [NSColor colorWithSRGBRed:0.945 green:0.969 blue:0.976 alpha:1.0];
    lsv.documentView = self.logView;
    [cv addSubview:lsv];

    NSArray<NSString*>* procArgs = [[NSProcessInfo processInfo] arguments];
    NSString* shotPath = nil;
    BOOL testCmdQ = NO;
    BOOL testPrefs = NO;
    NSInteger hoverIdx = -1;
    NSMutableArray<NSString*>* initPaths = [NSMutableArray array];
    for (NSUInteger i = 1; i < procArgs.count; ++i) {
        NSString* a = procArgs[i];
        if ([a isEqualToString:@"--screenshot"] && i + 1 < procArgs.count) {
            shotPath = procArgs[++i];
        } else if ([a isEqualToString:@"--test-cmd-q"]) {
            testCmdQ = YES;
        } else if ([a isEqualToString:@"--test-prefs"]) {
            testPrefs = YES;
        } else if ([a isEqualToString:@"--hover-dot"] && i + 1 < procArgs.count) {
            hoverIdx = [procArgs[++i] integerValue];
        } else if (![a hasPrefix:@"-"]) {
            [initPaths addObject:a];
        }
    }
    self.isScreenshotMode = (shotPath != nil);
    if (!self.isScreenshotMode && !testPrefs) {
        [self loadUiPrefs];
    }

    if (testPrefs) {
        // Isolated causal-delta self-test for compression format & option persistence
        NSUserDefaults* ud = nijinPrefs();
        NSDictionary* backup = [ud dictionaryRepresentation];
        bool defaultLosslessOk = (self.popRawFmt.indexOfSelectedItem == 0) &&
                                 (self.popRgbFmt.indexOfSelectedItem == 0) &&
                                 ([self.popRgbFmt.titleOfSelectedItem isEqualToString:@"16-bit 无损 TIFF"]) &&
                                 (self.popFieldMode.indexOfSelectedItem == 0) &&
                                 ([self.popFieldMode.titleOfSelectedItem isEqualToString:@"三张灰度"]);
        [self.popRawFmt selectItemAtIndex:1];   // DNG 高级有损压缩
        [self.popRgbFmt selectItemAtIndex:2];   // 14-bit JPEG XS 高级有损压缩
        [self.popFieldMode selectItemAtIndex:1]; // 合并 RGB
        self.edtJxsBpp.stringValue = @"12.5";
        self.radCsSrgb.state = NSControlStateValueOff;
        self.radCsAdobe.state = NSControlStateValueOn;
        self.chkEmitField.state = NSControlStateValueOn;
        [self applyPresetIndex:3];              // Astro 星空
        [self saveUiPrefs];

        // Reset controls to defaults, then reload from saved prefs
        [self.popRawFmt selectItemAtIndex:0];
        [self.popRgbFmt selectItemAtIndex:0];
        [self.popFieldMode selectItemAtIndex:0];
        self.edtJxsBpp.stringValue = @"10.0";
        self.radCsSrgb.state = NSControlStateValueOn;
        self.radCsAdobe.state = NSControlStateValueOff;
        self.chkEmitField.state = NSControlStateValueOff;
        [self applyPresetIndex:0];
        [self loadUiPrefs];

        bool ok = defaultLosslessOk &&
                  (self.popRawFmt.indexOfSelectedItem == 1) &&
                  (self.popRgbFmt.indexOfSelectedItem == 2) &&
                  (self.popFieldMode.indexOfSelectedItem == 1) &&
                  ([self.edtJxsBpp.stringValue isEqualToString:@"12.5"]) &&
                  (self.radCsAdobe.state == NSControlStateValueOn) &&
                  (self.chkEmitField.state == NSControlStateValueOn) &&
                  (self.radPresets[3].state == NSControlStateValueOn);
        [ud removePersistentDomainForName:kPrefSuite];
        for (NSString* k in backup) {
            if ([k isEqualToString:kPrefCliPath] || [k isEqualToString:kPrefLibRawPath]) {
                [ud setObject:backup[k] forKey:k];
            }
        }
        [ud synchronize];
        std::fprintf(stdout, "PREFS_TEST:%s\n", ok ? "PASS" : "FAIL");
        std::exit(ok ? 0 : 1);
    }

    [self updateBanner];
    NSString* detectedCli = [self autoDetectCliPath:YES];
    if (detectedCli) {
        NSString* shownCli = shotPath ? @"./nijinLBS" : detectedCli;
        self.edtCliPath.stringValue = shownCli;
        [self appendLog:[NSString stringWithFormat:@"Ready | CLI: %@ | LibRaw: %@",
            shownCli, savedLr.length > 0 ? savedLr : @"Built-in 0.23-HE*"]];
    } else {
        [self appendLog:@"CLI auto-search missed; waiting for custom path."];
    }

    if (initPaths.count > 0) [self addInputPaths:initPaths];

    [self.window center];
    [self.window makeKeyAndOrderFront:nil];
    [NSApp activateIgnoringOtherApps:YES];

    if (!detectedCli && !shotPath && !testCmdQ) {
        dispatch_async(dispatch_get_main_queue(), ^{
            [self promptUserForCliPath];
        });
    }

    if (testCmdQ) {
        dispatch_after(dispatch_time(DISPATCH_TIME_NOW, (int64_t)(120 * NSEC_PER_MSEC)), dispatch_get_main_queue(), ^{
            NSEvent* qEv = [NSEvent keyEventWithType:NSEventTypeKeyDown
                                            location:NSMakePoint(50, 50)
                                       modifierFlags:NSEventModifierFlagCommand
                                           timestamp:[[NSProcessInfo processInfo] systemUptime]
                                        windowNumber:self.window.windowNumber
                                             context:nil
                                          characters:@"q"
                         charactersIgnoringModifiers:@"q"
                                           isARepeat:NO
                                             keyCode:12];
            if (qEv) [NSApp postEvent:qEv atStart:NO];
        });
    }

    if (shotPath) {
        dispatch_after(dispatch_time(DISPATCH_TIME_NOW, (int64_t)(240 * NSEC_PER_MSEC)), dispatch_get_main_queue(), ^{
            if (hoverIdx >= 0 && (NSUInteger)hoverIdx < self.hoverDots.count) {
                HoverDotView* hd = self.hoverDots[(NSUInteger)hoverIdx];
                hd.hovered = YES;
                [hd setNeedsDisplay:YES];
                [hd showHelpPopover];
            }
            dispatch_after(dispatch_time(DISPATCH_TIME_NOW, (int64_t)(160 * NSEC_PER_MSEC)), dispatch_get_main_queue(), ^{
                NSView* v = self.window.contentView.superview ?: self.window.contentView;
                [v layoutSubtreeIfNeeded];
                NSBitmapImageRep* rep = [v bitmapImageRepForCachingDisplayInRect:v.bounds];
                [v cacheDisplayInRect:v.bounds toBitmapImageRep:rep];
                NSData* png = [rep representationUsingType:NSBitmapImageFileTypePNG properties:@{}];
                [png writeToFile:shotPath atomically:YES];
                [NSApp terminate:nil];
            });
        });
    }
}

- (void)applicationWillTerminate:(NSNotification*)notification {
    (void)notification;
    [self saveUiPrefs];
    if (self.currentTask && self.currentTask.isRunning) {
        [self.currentTask terminate];
    }
}

- (BOOL)applicationShouldTerminateAfterLastWindowClosed:(NSApplication*)sender {
    (void)sender;
    return YES;
}
@end

int main(int argc, const char* argv[]) {
    (void)argc; (void)argv;
    @autoreleasepool {
        NSApplication* app = [NijiLbsApplication sharedApplication];
        [app setActivationPolicy:NSApplicationActivationPolicyRegular];
        AppDelegate* del = [[AppDelegate alloc] init];
        app.delegate = del;
        [app run];
    }
    return 0;
}
