// heraw_sr2x_gui_mac.mm -- NijiN.LiteBurstSuite (nijinLBS) v2.0.0 Native macOS Cocoa Workbench
#import <Cocoa/Cocoa.h>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>
#include <unistd.h>
#include "heraw_brand_assets.hpp"

namespace {

static std::string extractJsonStr(const std::string& s, const char* key, size_t pos = 0) {
    std::string pat = std::string("\"") + key + "\"";
    size_t p = s.find(pat, pos);
    if (p == std::string::npos) return "";
    p = s.find(':', p + pat.size());
    if (p == std::string::npos) return "";
    ++p;
    while (p < s.size() && std::isspace((unsigned char)s[p])) ++p;
    if (p >= s.size() || s[p] != '"') return "";
    size_t start = p;
    ++p;
    while (p < s.size() && s[p] != '"') {
        if (s[p] == '\\' && p + 1 < s.size()) ++p;
        ++p;
    }
    if (p == s.size()) return "";
    NSData* data = [NSData dataWithBytes:s.data() + start length:p - start + 1];
    id value = [NSJSONSerialization JSONObjectWithData:data options:NSJSONReadingFragmentsAllowed error:nil];
    return [value isKindOfClass:[NSString class]] ? std::string([value UTF8String]) : "";
}

static int extractJsonInt(const std::string& s, const char* key, int fb, size_t pos = 0) {
    std::string pat = std::string("\"") + key + "\"";
    size_t p = s.find(pat, pos);
    if (p == std::string::npos) return fb;
    p = s.find(':', p + pat.size());
    if (p == std::string::npos) return fb;
    ++p;
    while (p < s.size() && std::isspace((unsigned char)s[p])) ++p;
    if (p >= s.size() || (!std::isdigit((unsigned char)s[p]) && s[p] != '-')) return fb;
    return std::atoi(s.c_str() + p);
}

static double extractJsonDouble(const std::string& s, const char* key, double fb, size_t pos = 0) {
    std::string pat = std::string("\"") + key + "\"";
    size_t p = s.find(pat, pos);
    if (p == std::string::npos) return fb;
    p = s.find(':', p + pat.size());
    if (p == std::string::npos) return fb;
    ++p;
    while (p < s.size() && std::isspace((unsigned char)s[p])) ++p;
    if (p >= s.size() || (!std::isdigit((unsigned char)s[p]) && s[p] != '-' && s[p] != '.')) return fb;
    return std::atof(s.c_str() + p);
}

static bool extractJsonBool(const std::string& s, const char* key, bool fb, size_t pos = 0) {
    std::string pat = std::string("\"") + key + "\"";
    size_t p = s.find(pat, pos);
    if (p == std::string::npos) return fb;
    p = s.find(':', p + pat.size());
    if (p == std::string::npos) return fb;
    ++p;
    while (p < s.size() && std::isspace((unsigned char)s[p])) ++p;
    if (s.compare(p, 4, "true") == 0) return true;
    if (s.compare(p, 5, "false") == 0) return false;
    return fb;
}

static std::vector<std::string> extractJsonArrayObjects(const std::string& s, const char* arrayKey, size_t pos = 0) {
    std::vector<std::string> items;
    std::string pat = std::string("\"") + arrayKey + "\"";
    size_t p = s.find(pat, pos);
    if (p == std::string::npos) return items;
    p += pat.size();
    while (p < s.size() && s[p] != '[') ++p;
    if (p >= s.size()) return items;
    ++p; // skip '['

    bool inStr = false;
    int braceDepth = 0;
    int bracketDepth = 1;
    size_t objStart = std::string::npos;

    while (p < s.size() && bracketDepth > 0) {
        char c = s[p];
        if (inStr) {
            if (c == '\\' && p + 1 < s.size()) { p += 2; continue; }
            if (c == '"') inStr = false;
            ++p;
            continue;
        }
        if (c == '"') {
            inStr = true;
        } else if (c == '[') {
            ++bracketDepth;
        } else if (c == ']') {
            --bracketDepth;
            if (bracketDepth == 0) break;
        } else if (c == '{') {
            if (braceDepth == 0) objStart = p;
            ++braceDepth;
        } else if (c == '}') {
            --braceDepth;
            if (braceDepth == 0 && objStart != std::string::npos) {
                items.push_back(s.substr(objStart, p - objStart + 1));
                objStart = std::string::npos;
            }
        }
        ++p;
    }
    return items;
}

struct RawQuickMeta {
    std::string model;
    std::string cfaType = "bayer";
    std::string shutterType = "mechanical";
    double focalMm = 0.0;
    double fNumber = 0.0;
    int iso = 0;
};

static uint16_t rdU16(const uint8_t* b, size_t n, size_t off, bool le) {
    if (off + 2 > n) return 0;
    return le ? (uint16_t)(b[off] | (b[off + 1] << 8))
              : (uint16_t)((b[off] << 8) | b[off + 1]);
}

static uint32_t rdU32(const uint8_t* b, size_t n, size_t off, bool le) {
    if (off + 4 > n) return 0;
    return le ? ((uint32_t)b[off] | ((uint32_t)b[off + 1] << 8) | ((uint32_t)b[off + 2] << 16) | ((uint32_t)b[off + 3] << 24))
              : (((uint32_t)b[off] << 24) | ((uint32_t)b[off + 1] << 16) | ((uint32_t)b[off + 2] << 8) | (uint32_t)b[off + 3]);
}

static void parseTiffIfd(const uint8_t* tiff, size_t tiffLen, size_t ifdOff, bool le, RawQuickMeta& out, int depth) {
    if (depth > 3 || ifdOff + 2 > tiffLen) return;
    uint16_t count = rdU16(tiff, tiffLen, ifdOff, le);
    if (count > 256) return;
    for (uint16_t i = 0; i < count; ++i) {
        size_t e = ifdOff + 2 + (size_t)i * 12;
        if (e + 12 > tiffLen) break;
        uint16_t tag  = rdU16(tiff, tiffLen, e, le);
        uint16_t type = rdU16(tiff, tiffLen, e + 2, le);
        uint32_t cnt  = rdU32(tiff, tiffLen, e + 4, le);
        uint32_t valOff = rdU32(tiff, tiffLen, e + 8, le);

        if (tag == 0x0110 && type == 2 && cnt > 0 && cnt < 128) { // Model
            size_t sOff = (cnt <= 4) ? (e + 8) : valOff;
            if (sOff + cnt <= tiffLen) {
                std::string m((const char*)(tiff + sOff), cnt);
                while (!m.empty() && (m.back() == '\0' || m.back() == ' ')) m.pop_back();
                if (!m.empty()) out.model = m;
            }
        } else if (tag == 0x8769 && (type == 4 || type == 13)) { // ExifIFD
            parseTiffIfd(tiff, tiffLen, valOff, le, out, depth + 1);
        } else if ((tag == 0x8827 || tag == 0x8833) && out.iso == 0) { // ISO
            if (type == 3) out.iso = (int)rdU16(tiff, tiffLen, e + 8, le);
            else if (type == 4) out.iso = (int)valOff;
        } else if (tag == 0x829D && type == 5 && valOff + 8 <= tiffLen) { // FNumber
            uint32_t num = rdU32(tiff, tiffLen, valOff, le);
            uint32_t den = rdU32(tiff, tiffLen, valOff + 4, le);
            if (den > 0) out.fNumber = (double)num / (double)den;
        } else if (tag == 0x920A && type == 5 && valOff + 8 <= tiffLen) { // FocalLength
            uint32_t num = rdU32(tiff, tiffLen, valOff, le);
            uint32_t den = rdU32(tiff, tiffLen, valOff + 4, le);
            if (den > 0) out.focalMm = (double)num / (double)den;
        }
    }
}

static RawQuickMeta peekRawMeta(NSString* path) {
    RawQuickMeta m;
    if (!path || path.length == 0) return m;
    NSString* ext = [[path pathExtension] lowercaseString];
    if ([ext isEqualToString:@"raf"]) m.cfaType = "xtrans";

    FILE* fp = std::fopen([path fileSystemRepresentation], "rb");
    if (!fp) return m;
    std::vector<uint8_t> buf(131072, 0);
    size_t n = std::fread(buf.data(), 1, buf.size(), fp);
    std::fclose(fp);
    if (n < 16) return m;

    if (n >= 16 && std::memcmp(buf.data(), "FUJIFILMCCD-RAW", 15) == 0) {
        m.cfaType = "xtrans";
        uint32_t jpgOff = rdU32(buf.data(), n, 0x54, false);
        if (jpgOff + 16 < n) {
            for (size_t k = jpgOff; k + 12 < n; ++k) {
                if (buf[k] == 0xFF && buf[k + 1] == 0xE1 && std::memcmp(buf.data() + k + 4, "Exif\0\0", 6) == 0) {
                    const uint8_t* tiff = buf.data() + k + 10;
                    size_t tiffLen = n - (k + 10);
                    bool le = (tiff[0] == 'I' && tiff[1] == 'I');
                    uint32_t ifd0 = rdU32(tiff, tiffLen, 4, le);
                    parseTiffIfd(tiff, tiffLen, ifd0, le, m, 0);
                    break;
                }
            }
        }
    } else if ((buf[0] == 'I' && buf[1] == 'I') || (buf[0] == 'M' && buf[1] == 'M')) {
        bool le = (buf[0] == 'I');
        uint32_t ifd0 = rdU32(buf.data(), n, 4, le);
        parseTiffIfd(buf.data(), n, ifd0, le, m, 0);
    } else {
        // Scan for embedded Exif TIFF header (e.g. ISO BMFF CR3)
        for (size_t k = 0; k + 24 < n; ++k) {
            if ((buf[k] == 'I' && buf[k + 1] == 'I' && buf[k + 2] == 42 && buf[k + 3] == 0) ||
                (buf[k] == 'M' && buf[k + 1] == 'M' && buf[k + 2] == 0 && buf[k + 3] == 42)) {
                bool le = (buf[k] == 'I');
                uint32_t ifd0 = rdU32(buf.data() + k, n - k, 4, le);
                if (ifd0 >= 8 && ifd0 < n - k) {
                    parseTiffIfd(buf.data() + k, n - k, ifd0, le, m, 0);
                    if (!m.model.empty()) break;
                }
            }
        }
    }

    if (m.model.find("ILCE-9M3") != std::string::npos || m.model.find("A9 III") != std::string::npos) {
        m.shutterType = "global";
    }
    if (m.model.find("GFX") != std::string::npos) {
        m.cfaType = "bayer";
    } else if (m.model.find("X-T") != std::string::npos || m.model.find("X-H2") != std::string::npos ||
               m.model.find("X100") != std::string::npos || m.model.find("X-Pro") != std::string::npos ||
               m.model.find("X-S") != std::string::npos || m.model.find("X-E") != std::string::npos) {
        m.cfaType = "xtrans";
    }
    return m;
}

static NSImage* loadEmbeddedPng(const unsigned char* bytes, size_t len, NSSize logicalSize) {
    NSData* data = [NSData dataWithBytesNoCopy:(void*)bytes length:len freeWhenDone:NO];
    NSImage* img = [[NSImage alloc] initWithData:data];
    if (img && logicalSize.width > 0 && logicalSize.height > 0) img.size = logicalSize;
    return img;
}

static void drawRoundedCard(NSRect r, CGFloat radius) {
    NSBezierPath* sh = [NSBezierPath bezierPathWithRoundedRect:NSOffsetRect(r, 0, 1.5) xRadius:radius yRadius:radius];
    [[NSColor colorWithSRGBRed:0.12 green:0.22 blue:0.26 alpha:0.045] setFill];
    [sh fill];

    NSBezierPath* bp = [NSBezierPath bezierPathWithRoundedRect:r xRadius:radius yRadius:radius];
    [[NSColor colorWithSRGBRed:0.978 green:0.989 blue:0.993 alpha:1.0] setFill];
    [bp fill];
    [[NSColor colorWithSRGBRed:0.765 green:0.831 blue:0.863 alpha:1.0] setStroke];
    bp.lineWidth = 1.0;
    [bp stroke];
}

static void drawSubBox(NSRect r, NSString* groupTitle) {
    NSBezierPath* bp = [NSBezierPath bezierPathWithRoundedRect:r xRadius:8 yRadius:8];
    [[NSColor colorWithSRGBRed:0.945 green:0.969 blue:0.978 alpha:1.0] setFill];
    [bp fill];
    [[NSColor colorWithSRGBRed:0.812 green:0.871 blue:0.898 alpha:1.0] setStroke];
    bp.lineWidth = 1.0;
    [bp stroke];

    if (groupTitle && groupTitle.length > 0) {
        NSDictionary* attrs = @{
            NSFontAttributeName: [NSFont systemFontOfSize:11.0 weight:NSFontWeightSemibold],
            NSForegroundColorAttributeName: [NSColor colorWithSRGBRed:0.26 green:0.43 blue:0.50 alpha:1.0]
        };
        [groupTitle drawAtPoint:NSMakePoint(r.origin.x + 12, r.origin.y + 8) withAttributes:attrs];
    }
}

struct PresetDef {
    const char* code;
    double denoise;
    double chroma;
    int psfIters;
    double psfSigma;
    double deghost;
    double evOffset;
    bool siliconClean;
    bool ibpEnable;
    bool autoLca;
    bool autoExp;
    bool rolloff;
    bool dither;
};

static const PresetDef kPresets[4] = {
    // 0: Photo 照片
    {"photo", 1.00, 1.00, 4, 1.05, 1.00, 0.0, true, true, true,  true,  true,  true},
    // 1: Astro 星空
    {"astro", 0.85, 0.90, 3, 0.95, 0.85, 0.0, true, true, true,  true,  true,  true},
    // 2: Sci 科学
    {"sci",   0.40, 0.55, 0, 1.05, 1.35, 0.0, true, true, false, false, false, true},
    // 3: Arc 归档
    {"arc",   0.70, 0.85, 0, 1.05, 1.10, 0.0, true, true, true,  true,  true,  true}
};

struct FieldSpec {
    const char* code;
    NSString* title;
    int boxIdx; // 0..3
    bool strictGate;
};

} // namespace

// ============================================================================
// Data Models
// ============================================================================

@interface FrameDiagItem : NSObject
@property (assign) int idx1Based;
@property (assign) int assignedGroup;
@property (copy) NSString* fileName;
@property (copy) NSString* fullPath;
@property (copy) NSString* flags;
@property (assign) double afScore;
@property (assign) double acutance;
@property (assign) double shiftPx;
@property (assign) double dxPx;
@property (assign) double dyPx;
@property (assign) BOOL hasPosition;
@property (assign) BOOL hasTime;
@property (assign) double timeSeconds;
@property (assign) BOOL isCrown;
@property (assign) BOOL isShock;
@property (assign) BOOL enabled;
@end

@implementation FrameDiagItem
@end

@interface BurstGroupModel : NSObject
@property (assign) int displayIndex;
@property (assign) BOOL checked;
@property (assign) int validFrameCount;
@property (copy) NSString* cameraModel;
@property (copy) NSString* cfaType;      // "bayer" | "xtrans"
@property (copy) NSString* shutterType;  // "mechanical" | "electronic" | "global"
@property (assign) BOOL oisEnabled;
@property (assign) double focalMm;
@property (assign) double fNumber;
@property (assign) int iso;
@property (copy) NSString* sceneCode;
@property (copy) NSString* recPreset;    // "photo" | "astro" | "sci" | "arc"
@property (copy) NSString* recAnchor;
@property (assign) double recDenoise;
@property (assign) double recDeghost;
@property (assign) int recPsfIters;
@property (assign) int bestAnchor;
@property (assign) int quadCoverage;
@property (assign) BOOL shutterShock;
@property (assign) double maxDrift2xPx;
@property (assign) double maxParallax2xPx;
@property (assign) BOOL hasTelemetry;
@property (assign) BOOL hasMeasuredParallax;
@property (assign) BOOL depthKnown;
@property (assign) BOOL depthObservable;
@property (copy) NSDictionary<NSString*, NSNumber*>* fieldObservability;
@property (copy) NSArray<NSNumber*>* phaseQuadrants;
@property (assign) int customAnchor1Based; // 0 = follow global motion mode
@property (strong) NSMutableArray<FrameDiagItem*>* frames;
@property (strong) NSMutableArray<NSString*>* filePaths;
@end

@implementation BurstGroupModel
- (instancetype)init {
    self = [super init];
    if (self) {
        _cfaType = @"bayer";
        _shutterType = @"mechanical";
        _sceneCode = @"handheld_standard";
        _recPreset = @"photo";
        _recAnchor = @"auto";
        _recDenoise = 1.0;
        _recDeghost = 1.0;
        _recPsfIters = 4;
        _bestAnchor = 1;
        _quadCoverage = 1;
        _frames = [NSMutableArray array];
        _filePaths = [NSMutableArray array];
    }
    return self;
}
@end

@interface OpticsTelemetryModel : NSObject
@property (assign) BOOL populated;
@property (assign) BOOL hasSilicon;
@property (copy) NSString* tierBadge;
@property (copy) NSString* oisSummary;
@property (assign) double centerMtf;
@property (assign) BOOL centerValid;
@property (assign) double nwMtf;
@property (assign) double neMtf;
@property (assign) double swMtf;
@property (assign) double seMtf;
@property (assign) double nwAstig;
@property (assign) double neAstig;
@property (assign) double swAstig;
@property (assign) double seAstig;
@property (assign) BOOL nwValid;
@property (assign) BOOL neValid;
@property (assign) BOOL swValid;
@property (assign) BOOL seValid;
@property (assign) double lcaRedPx;
@property (assign) double lcaBluePx;
@property (assign) BOOL redLcaValid;
@property (assign) BOOL blueLcaValid;
@property (copy) NSArray<NSString*>* zoneStatuses;
@property (assign) double sigma0;
@property (assign) double gainG;
@property (assign) double sensorTempC;
@property (assign) int warmPixels;
@property (copy) NSString* lastJsonPath;
@property (copy) NSString* lastCardPngPath;
@end

@implementation OpticsTelemetryModel
- (instancetype)init {
    self = [super init];
    if (self) {
        _tierBadge = @"待实测";
        _oisSummary = @"OIS 元数据: 待导入";
        _nwValid = YES; _neValid = YES; _swValid = YES; _seValid = YES;
        _sigma0 = -1.0; _gainG = -1.0;
        _sensorTempC = NAN;
        _warmPixels = -1;
    }
    return self;
}
@end

// ============================================================================
// Custom Controls & CoreGraphics Views
// ============================================================================

@interface AquaButton : NSButton
@property (nonatomic, assign) BOOL stopMode;
@property (nonatomic, assign) BOOL compactFont;
@end

@implementation AquaButton
- (BOOL)isFlipped { return YES; }
- (void)drawRect:(NSRect)dirtyRect {
    (void)dirtyRect;
    NSRect r = NSInsetRect(self.bounds, 0.5, 0.5);
    NSBezierPath* bp = [NSBezierPath bezierPathWithRoundedRect:r xRadius:8 yRadius:8];
    NSColor* bg = nil;
    if (!self.enabled) {
        bg = [NSColor colorWithSRGBRed:0.69 green:0.76 blue:0.79 alpha:1.0];
    } else if (self.stopMode) {
        bg = self.isHighlighted
            ? [NSColor colorWithSRGBRed:0.52 green:0.25 blue:0.24 alpha:1.0]
            : [NSColor colorWithSRGBRed:0.62 green:0.31 blue:0.30 alpha:1.0];
    } else {
        bg = self.isHighlighted
            ? [NSColor colorWithSRGBRed:0.24 green:0.42 blue:0.49 alpha:1.0]
            : [NSColor colorWithSRGBRed:0.31 green:0.50 blue:0.58 alpha:1.0];
    }
    [bg setFill];
    [bp fill];

    NSDictionary* attrs = @{
        NSFontAttributeName: [NSFont boldSystemFontOfSize:(self.compactFont ? 12.0 : 12.5)],
        NSForegroundColorAttributeName: [NSColor whiteColor]
    };
    NSSize sz = [self.title sizeWithAttributes:attrs];
    [self.title drawAtPoint:NSMakePoint((self.bounds.size.width - sz.width) * 0.5,
                                        (self.bounds.size.height - sz.height) * 0.5)
             withAttributes:attrs];
}
@end

@interface PortalPillButton : NSButton
@property (nonatomic, assign) BOOL activeHighlight;
@end

@implementation PortalPillButton
- (BOOL)isFlipped { return YES; }
- (void)drawRect:(NSRect)dirtyRect {
    (void)dirtyRect;
    NSRect r = NSInsetRect(self.bounds, 0.5, 0.5);
    CGFloat rad = r.size.height * 0.5;
    NSBezierPath* bp = [NSBezierPath bezierPathWithRoundedRect:r xRadius:rad yRadius:rad];
    NSColor* fill = self.isHighlighted
        ? [NSColor colorWithSRGBRed:0.76 green:0.86 blue:0.90 alpha:1.0]
        : (self.activeHighlight
            ? [NSColor colorWithSRGBRed:0.81 green:0.90 blue:0.94 alpha:1.0]
            : [NSColor colorWithSRGBRed:0.89 green:0.94 blue:0.96 alpha:1.0]);
    NSColor* stroke = self.activeHighlight
        ? [NSColor colorWithSRGBRed:0.36 green:0.58 blue:0.67 alpha:1.0]
        : [NSColor colorWithSRGBRed:0.67 green:0.79 blue:0.84 alpha:1.0];
    [fill setFill];
    [bp fill];
    [stroke setStroke];
    bp.lineWidth = 1.0;
    [bp stroke];

    NSDictionary* attrs = @{
        NSFontAttributeName: [NSFont systemFontOfSize:11.0 weight:NSFontWeightSemibold],
        NSForegroundColorAttributeName: [NSColor colorWithSRGBRed:0.18 green:0.35 blue:0.42 alpha:1.0]
    };
    NSSize sz = [self.title sizeWithAttributes:attrs];
    [self.title drawAtPoint:NSMakePoint((self.bounds.size.width - sz.width) * 0.5,
                                        (self.bounds.size.height - sz.height) * 0.5)
             withAttributes:attrs];
}
@end

@interface AquaProgressView : NSView
@property (nonatomic, assign) double progress; // 0..100
@end

@implementation AquaProgressView
- (BOOL)isFlipped { return YES; }
- (void)drawRect:(NSRect)dirtyRect {
    (void)dirtyRect;
    NSRect r = NSInsetRect(self.bounds, 0.5, 0.5);
    CGFloat rad = r.size.height * 0.5;
    NSBezierPath* track = [NSBezierPath bezierPathWithRoundedRect:r xRadius:rad yRadius:rad];
    [[NSColor colorWithSRGBRed:0.855 green:0.906 blue:0.925 alpha:1.0] setFill];
    [track fill];
    [[NSColor colorWithSRGBRed:0.74 green:0.82 blue:0.85 alpha:1.0] setStroke];
    track.lineWidth = 1.0;
    [track stroke];

    double p = std::max(0.0, std::min(100.0, self.progress));
    if (p > 0.3) {
        CGFloat fw = std::max(rad * 2.0, (CGFloat)(r.size.width * (p / 100.0)));
        fw = std::min(fw, r.size.width);
        NSRect fr = NSMakeRect(r.origin.x, r.origin.y, fw, r.size.height);
        NSBezierPath* fillBp = [NSBezierPath bezierPathWithRoundedRect:fr xRadius:rad yRadius:rad];
        [[NSColor colorWithSRGBRed:0.31 green:0.51 blue:0.59 alpha:1.0] setFill];
        [fillBp fill];
    }
}
@end

@interface SuiteTabBarView : NSView
@property (nonatomic, assign) NSInteger selectedTab;
@property (nonatomic, assign) BOOL dotTab2;
@property (nonatomic, assign) BOOL dotTab3;
@property (nonatomic, copy) void (^onSelectTab)(NSInteger idx);
@end

@implementation SuiteTabBarView
- (BOOL)isFlipped { return YES; }
- (void)drawRect:(NSRect)dirtyRect {
    (void)dirtyRect;
    NSRect outer = NSInsetRect(self.bounds, 0.5, 0.5);
    NSBezierPath* track = [NSBezierPath bezierPathWithRoundedRect:outer xRadius:9 yRadius:9];
    [[NSColor colorWithSRGBRed:0.843 green:0.898 blue:0.918 alpha:1.0] setFill];
    [track fill];
    [[NSColor colorWithSRGBRed:0.737 green:0.812 blue:0.843 alpha:1.0] setStroke];
    track.lineWidth = 1.0;
    [track stroke];

    NSArray<NSString*>* titles = @[
        @"1 超分与主输出",
        self.dotTab2 ? @"• 2 时空与创意" : @"2 时空与创意",
        self.dotTab3 ? @"• 3 物理场与数据" : @"3 物理场与数据",
        @"4 镜头与机身"
    ];

    CGFloat pad = 3.0;
    CGFloat segW = (self.bounds.size.width - pad * 2.0) / 4.0;
    CGFloat segH = self.bounds.size.height - pad * 2.0;

    for (NSInteger i = 0; i < 4; ++i) {
        NSRect sr = NSMakeRect(pad + segW * i, pad, segW, segH);
        BOOL sel = (i == self.selectedTab);
        if (sel) {
            NSBezierPath* sbp = [NSBezierPath bezierPathWithRoundedRect:NSInsetRect(sr, 1.0, 0.5) xRadius:7 yRadius:7];
            [[NSColor colorWithSRGBRed:0.988 green:0.996 blue:1.0 alpha:1.0] setFill];
            [sbp fill];
            [[NSColor colorWithSRGBRed:0.49 green:0.66 blue:0.73 alpha:1.0] setStroke];
            sbp.lineWidth = 1.0;
            [sbp stroke];
        }
        BOOL hasDot = ((i == 1 && self.dotTab2) || (i == 2 && self.dotTab3));
        NSDictionary* attrs = @{
            NSFontAttributeName: sel
                ? [NSFont systemFontOfSize:12.5 weight:NSFontWeightBold]
                : [NSFont systemFontOfSize:12.0 weight:NSFontWeightMedium],
            NSForegroundColorAttributeName: sel
                ? [NSColor colorWithSRGBRed:0.12 green:0.24 blue:0.29 alpha:1.0]
                : (hasDot
                    ? [NSColor colorWithSRGBRed:0.19 green:0.42 blue:0.51 alpha:1.0]
                    : [NSColor colorWithSRGBRed:0.28 green:0.39 blue:0.44 alpha:1.0])
        };
        NSSize tsz = [titles[(NSUInteger)i] sizeWithAttributes:attrs];
        [titles[(NSUInteger)i] drawAtPoint:NSMakePoint(sr.origin.x + (sr.size.width - tsz.width) * 0.5,
                                                       sr.origin.y + (sr.size.height - tsz.height) * 0.5)
                            withAttributes:attrs];
    }
}

- (void)mouseDown:(NSEvent*)event {
    NSPoint pt = [self convertPoint:event.locationInWindow fromView:nil];
    CGFloat segW = self.bounds.size.width / 4.0;
    NSInteger idx = (NSInteger)(pt.x / std::max(1.0, (double)segW));
    if (idx < 0) idx = 0;
    if (idx > 3) idx = 3;
    if (idx != self.selectedTab) {
        self.selectedTab = idx;
        [self setNeedsDisplay:YES];
        if (self.onSelectTab) self.onSelectTab(idx);
    }
}
@end

@interface EmptyDropWellView : NSView
@property (nonatomic, assign) BOOL scanning;
@property (nonatomic, assign) BOOL hovered;
@property (nonatomic, strong) NSTrackingArea* trackArea;
@property (nonatomic, copy) void (^onClickWell)(void);
@property (nonatomic, copy) void (^onDropPaths)(NSArray<NSString*>* paths);
@end

@implementation EmptyDropWellView
- (instancetype)initWithFrame:(NSRect)frameRect {
    self = [super initWithFrame:frameRect];
    if (self) {
        [self registerForDraggedTypes:@[NSPasteboardTypeFileURL]];
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
- (void)mouseEntered:(NSEvent*)e { (void)e; self.hovered = YES; [self setNeedsDisplay:YES]; }
- (void)mouseExited:(NSEvent*)e  { (void)e; self.hovered = NO;  [self setNeedsDisplay:YES]; }
- (void)mouseDown:(NSEvent*)e {
    (void)e;
    if (!self.scanning && self.onClickWell) self.onClickWell();
}
- (NSDragOperation)draggingEntered:(id<NSDraggingInfo>)sender {
    (void)sender;
    self.hovered = YES;
    [self setNeedsDisplay:YES];
    return NSDragOperationCopy;
}
- (void)draggingExited:(id<NSDraggingInfo>)sender {
    (void)sender;
    self.hovered = NO;
    [self setNeedsDisplay:YES];
}
- (BOOL)performDragOperation:(id<NSDraggingInfo>)sender {
    self.hovered = NO;
    [self setNeedsDisplay:YES];
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
- (void)drawRect:(NSRect)dirtyRect {
    (void)dirtyRect;
    NSRect r = NSInsetRect(self.bounds, 2.0, 2.0);
    NSBezierPath* bp = [NSBezierPath bezierPathWithRoundedRect:r xRadius:9 yRadius:9];
    NSColor* fill = self.hovered
        ? [NSColor colorWithSRGBRed:0.91 green:0.955 blue:0.972 alpha:1.0]
        : [NSColor colorWithSRGBRed:0.945 green:0.972 blue:0.982 alpha:1.0];
    [fill setFill];
    [bp fill];

    CGFloat dash[2] = {6.0, 4.0};
    [bp setLineDash:dash count:2 phase:0];
    bp.lineWidth = 1.5;
    [[NSColor colorWithSRGBRed:0.55 green:0.70 blue:0.76 alpha:1.0] setStroke];
    [bp stroke];

    CGFloat cx = self.bounds.size.width * 0.5;
    CGFloat cy = self.bounds.size.height * 0.5;

    // Minimalist stacked-frames icon
    NSRect iconBack = NSMakeRect(cx - 19, cy - 38, 30, 22);
    NSRect iconFront = NSMakeRect(cx - 11, cy - 32, 30, 22);
    NSBezierPath* ib1 = [NSBezierPath bezierPathWithRoundedRect:iconBack xRadius:4 yRadius:4];
    [[NSColor colorWithSRGBRed:0.62 green:0.76 blue:0.82 alpha:1.0] setStroke];
    ib1.lineWidth = 1.4;
    [ib1 stroke];

    NSBezierPath* ib2 = [NSBezierPath bezierPathWithRoundedRect:iconFront xRadius:4 yRadius:4];
    [[NSColor colorWithSRGBRed:0.95 green:0.98 blue:0.99 alpha:1.0] setFill];
    [ib2 fill];
    [[NSColor colorWithSRGBRed:0.35 green:0.55 blue:0.63 alpha:1.0] setStroke];
    ib2.lineWidth = 1.5;
    [ib2 stroke];

    NSString* mainText = self.scanning ? @"正在静默剖析连拍簇与 AF 对焦点..." : @"拖入 RAW 连拍或文件夹，或点击此处选择";
    NSDictionary* mainAttrs = @{
        NSFontAttributeName: [NSFont systemFontOfSize:13.0 weight:NSFontWeightSemibold],
        NSForegroundColorAttributeName: [NSColor colorWithSRGBRed:0.20 green:0.36 blue:0.43 alpha:1.0]
    };
    NSSize msz = [mainText sizeWithAttributes:mainAttrs];
    [mainText drawAtPoint:NSMakePoint(cx - msz.width * 0.5, cy + 2) withAttributes:mainAttrs];

    NSString* subText = self.scanning
        ? @"自动识别传感器 CFA / 快门微震 / 四象限相位覆盖"
        : @"自动按快门簇分拣 · 原生支持 Bayer / 6×6 X-Trans / Nikon HE*";
    NSDictionary* subAttrs = @{
        NSFontAttributeName: [NSFont systemFontOfSize:11.0 weight:NSFontWeightRegular],
        NSForegroundColorAttributeName: [NSColor colorWithSRGBRed:0.42 green:0.54 blue:0.59 alpha:1.0]
    };
    NSSize ssz = [subText sizeWithAttributes:subAttrs];
    [subText drawAtPoint:NSMakePoint(cx - ssz.width * 0.5, cy + 24) withAttributes:subAttrs];
}
@end

@interface BurstQueueTableView : NSTableView
@property (nonatomic, copy) void (^onDeletePressed)(void);
@end

@implementation BurstQueueTableView
- (void)keyDown:(NSEvent*)event {
    NSString* ch = event.charactersIgnoringModifiers;
    if (ch.length > 0) {
        unichar c = [ch characterAtIndex:0];
        if (c == NSDeleteCharacter || c == NSBackspaceCharacter || c == NSDeleteFunctionKey) {
            if (self.onDeletePressed) { self.onDeletePressed(); return; }
        }
    }
    [super keyDown:event];
}
@end

@interface BurstGroupCellView : NSTableCellView
@property (nonatomic, strong) NSButton* chkEnable;
@property (nonatomic, strong) NSTextField* lblTitle;
@property (nonatomic, strong) NSTextField* lblBadge;
@property (nonatomic, strong) NSTextField* lblMeta;
@property (nonatomic, strong) PortalPillButton* btnScenePill;
@property (nonatomic, copy) void (^onToggleCheck)(BOOL on);
@property (nonatomic, copy) void (^onApplyScene)(void);
@end

@implementation BurstGroupCellView
- (BOOL)isFlipped { return YES; }
- (instancetype)initWithFrame:(NSRect)frameRect {
    self = [super initWithFrame:frameRect];
    if (self) {
        _chkEnable = [NSButton checkboxWithTitle:@"" target:self action:@selector(handleCheck:)];
        _chkEnable.frame = NSMakeRect(8, 9, 20, 20);
        [self addSubview:_chkEnable];

        _lblTitle = [NSTextField labelWithString:@""];
        _lblTitle.font = [NSFont systemFontOfSize:12.0 weight:NSFontWeightSemibold];
        _lblTitle.textColor = [NSColor colorWithSRGBRed:0.13 green:0.22 blue:0.27 alpha:1.0];
        _lblTitle.lineBreakMode = NSLineBreakByTruncatingMiddle;
        _lblTitle.frame = NSMakeRect(30, 8, 290, 18);
        [self addSubview:_lblTitle];

        _lblBadge = [NSTextField labelWithString:@""];
        _lblBadge.font = [NSFont systemFontOfSize:10.5 weight:NSFontWeightSemibold];
        _lblBadge.alignment = NSTextAlignmentCenter;
        _lblBadge.wantsLayer = YES;
        _lblBadge.layer.cornerRadius = 4.0;
        _lblBadge.layer.backgroundColor = [NSColor colorWithSRGBRed:0.86 green:0.92 blue:0.94 alpha:1.0].CGColor;
        _lblBadge.textColor = [NSColor colorWithSRGBRed:0.22 green:0.41 blue:0.49 alpha:1.0];
        _lblBadge.frame = NSMakeRect(360, 8, 56, 17);
        [self addSubview:_lblBadge];

        _lblMeta = [NSTextField labelWithString:@""];
        _lblMeta.font = [NSFont systemFontOfSize:11.0 weight:NSFontWeightRegular];
        _lblMeta.textColor = [NSColor colorWithSRGBRed:0.36 green:0.47 blue:0.52 alpha:1.0];
        _lblMeta.lineBreakMode = NSLineBreakByTruncatingTail;
        _lblMeta.frame = NSMakeRect(30, 30, 274, 17);
        [self addSubview:_lblMeta];

        _btnScenePill = [[PortalPillButton alloc] initWithFrame:NSMakeRect(306, 28, 110, 20)];
        _btnScenePill.activeHighlight = YES;
        _btnScenePill.target = self;
        _btnScenePill.action = @selector(handlePill:);
        _btnScenePill.hidden = YES;
        [self addSubview:_btnScenePill];
    }
    return self;
}
- (void)handleCheck:(NSButton*)sender {
    if (self.onToggleCheck) self.onToggleCheck(sender.state == NSControlStateValueOn);
}
- (void)handlePill:(id)sender {
    (void)sender;
    if (self.onApplyScene) self.onApplyScene();
}
- (void)resizeSubviewsWithOldSize:(NSSize)oldSize {
    (void)oldSize;
    CGFloat w = self.bounds.size.width;
    self.lblBadge.frame = NSMakeRect(w - 66, 8, 58, 17);
    self.lblTitle.frame = NSMakeRect(30, 7, std::max(120.0, (double)w - 102.0), 18);
    if (self.btnScenePill.hidden) {
        self.lblMeta.frame = NSMakeRect(30, 29, std::max(120.0, (double)w - 40.0), 17);
    } else {
        self.btnScenePill.frame = NSMakeRect(w - 122, 27, 114, 20);
        self.lblMeta.frame = NSMakeRect(30, 29, std::max(100.0, (double)w - 158.0), 17);
    }
}
@end

@interface FrameSharpnessBarView : NSView
@property (nonatomic, assign) double normRatio; // 0..1
@property (nonatomic, assign) BOOL isCrown;
@property (nonatomic, assign) BOOL dimmed;
@end

@implementation FrameSharpnessBarView
- (BOOL)isFlipped { return YES; }
- (void)drawRect:(NSRect)dirtyRect {
    (void)dirtyRect;
    NSRect r = NSInsetRect(self.bounds, 0.0, 2.0);
    CGFloat rad = r.size.height * 0.5;
    NSBezierPath* tr = [NSBezierPath bezierPathWithRoundedRect:r xRadius:rad yRadius:rad];
    [[NSColor colorWithSRGBRed:0.86 green:0.91 blue:0.93 alpha:1.0] setFill];
    [tr fill];

    double v = std::max(0.06, std::min(1.0, self.normRatio));
    NSRect fr = NSMakeRect(r.origin.x, r.origin.y, r.size.width * v, r.size.height);
    NSBezierPath* fb = [NSBezierPath bezierPathWithRoundedRect:fr xRadius:rad yRadius:rad];
    NSColor* col = self.dimmed
        ? [NSColor colorWithSRGBRed:0.68 green:0.75 blue:0.78 alpha:1.0]
        : (self.isCrown
            ? [NSColor colorWithSRGBRed:0.22 green:0.52 blue:0.62 alpha:1.0]
            : [NSColor colorWithSRGBRed:0.36 green:0.58 blue:0.66 alpha:1.0]);
    [col setFill];
    [fb fill];
}
@end

@interface AnchorRadioDotButton : NSButton
@property (nonatomic, assign) BOOL lockedAnchor;
@end

@implementation AnchorRadioDotButton
- (BOOL)isFlipped { return YES; }
- (void)drawRect:(NSRect)dirtyRect {
    (void)dirtyRect;
    CGFloat sz = 13.0;
    NSRect r = NSMakeRect((self.bounds.size.width - sz) * 0.5, (self.bounds.size.height - sz) * 0.5, sz, sz);
    NSBezierPath* outer = [NSBezierPath bezierPathWithOvalInRect:r];
    if (self.lockedAnchor) {
        [[NSColor colorWithSRGBRed:0.25 green:0.49 blue:0.58 alpha:1.0] setStroke];
        outer.lineWidth = 1.6;
        [outer stroke];
        NSBezierPath* inner = [NSBezierPath bezierPathWithOvalInRect:NSInsetRect(r, 3.2, 3.2)];
        [[NSColor colorWithSRGBRed:0.25 green:0.49 blue:0.58 alpha:1.0] setFill];
        [inner fill];
    } else {
        [[NSColor colorWithSRGBRed:0.58 green:0.69 blue:0.74 alpha:1.0] setStroke];
        outer.lineWidth = 1.2;
        [outer stroke];
    }
}
@end

@interface FrameInspectorCellView : NSTableCellView
@property (nonatomic, strong) NSButton* chkFrame;
@property (nonatomic, strong) AnchorRadioDotButton* btnAnchorDot;
@property (nonatomic, strong) NSTextField* lblName;
@property (nonatomic, strong) FrameSharpnessBarView* barView;
@property (nonatomic, strong) NSTextField* lblScore;
@property (nonatomic, strong) NSTextField* lblShift;
@property (nonatomic, strong) NSTextField* lblTag;
@property (nonatomic, copy) void (^onToggleFrame)(BOOL on);
@property (nonatomic, copy) void (^onClickAnchor)(void);
@end

@implementation FrameInspectorCellView
- (BOOL)isFlipped { return YES; }
- (instancetype)initWithFrame:(NSRect)frameRect {
    self = [super initWithFrame:frameRect];
    if (self) {
        _chkFrame = [NSButton checkboxWithTitle:@"" target:self action:@selector(handleChk:)];
        _chkFrame.frame = NSMakeRect(6, 4, 20, 20);
        [self addSubview:_chkFrame];

        _btnAnchorDot = [[AnchorRadioDotButton alloc] initWithFrame:NSMakeRect(27, 4, 20, 20)];
        _btnAnchorDot.bordered = NO;
        _btnAnchorDot.title = @"";
        _btnAnchorDot.toolTip = @"点击锁定此帧为姿态定格锚点";
        _btnAnchorDot.target = self;
        _btnAnchorDot.action = @selector(handleDot:);
        [self addSubview:_btnAnchorDot];

        _lblName = [NSTextField labelWithString:@""];
        _lblName.font = [NSFont monospacedDigitSystemFontOfSize:11.5 weight:NSFontWeightRegular];
        _lblName.textColor = [NSColor colorWithSRGBRed:0.15 green:0.23 blue:0.27 alpha:1.0];
        _lblName.lineBreakMode = NSLineBreakByTruncatingMiddle;
        _lblName.frame = NSMakeRect(50, 5, 134, 18);
        [self addSubview:_lblName];

        _barView = [[FrameSharpnessBarView alloc] initWithFrame:NSMakeRect(188, 8, 84, 12)];
        [self addSubview:_barView];

        _lblScore = [NSTextField labelWithString:@""];
        _lblScore.font = [NSFont monospacedDigitSystemFontOfSize:11.0 weight:NSFontWeightSemibold];
        _lblScore.textColor = [NSColor colorWithSRGBRed:0.20 green:0.38 blue:0.45 alpha:1.0];
        _lblScore.frame = NSMakeRect(276, 5, 44, 18);
        [self addSubview:_lblScore];

        _lblShift = [NSTextField labelWithString:@""];
        _lblShift.font = [NSFont monospacedDigitSystemFontOfSize:11.0 weight:NSFontWeightRegular];
        _lblShift.alignment = NSTextAlignmentRight;
        _lblShift.textColor = [NSColor colorWithSRGBRed:0.36 green:0.47 blue:0.52 alpha:1.0];
        _lblShift.frame = NSMakeRect(322, 5, 52, 18);
        [self addSubview:_lblShift];

        _lblTag = [NSTextField labelWithString:@""];
        _lblTag.font = [NSFont systemFontOfSize:10.5 weight:NSFontWeightSemibold];
        _lblTag.alignment = NSTextAlignmentCenter;
        _lblTag.frame = NSMakeRect(380, 5, 44, 17);
        [self addSubview:_lblTag];
    }
    return self;
}
- (void)handleChk:(NSButton*)s { if (self.onToggleFrame) self.onToggleFrame(s.state == NSControlStateValueOn); }
- (void)handleDot:(id)s { (void)s; if (self.onClickAnchor) self.onClickAnchor(); }
@end

@interface ParallaxGateView : NSView
@property (nonatomic, assign) BOOL hasGroup;
@property (nonatomic, assign) BOOL measured;
@property (nonatomic, assign) BOOL depthKnown;
@property (nonatomic, assign) BOOL depthObservable;
@property (nonatomic, assign) double parallax2xPx;
@end

@implementation ParallaxGateView
- (BOOL)isFlipped { return YES; }
- (void)drawRect:(NSRect)dirtyRect {
    (void)dirtyRect;
    NSRect barRect = NSMakeRect(0, 6, 210, 12);
    CGFloat rad = 6.0;
    NSBezierPath* tr = [NSBezierPath bezierPathWithRoundedRect:barRect xRadius:rad yRadius:rad];
    [[NSColor colorWithSRGBRed:0.86 green:0.91 blue:0.93 alpha:1.0] setFill];
    [tr fill];
    [[NSColor colorWithSRGBRed:0.74 green:0.82 blue:0.85 alpha:1.0] setStroke];
    tr.lineWidth = 1.0;
    [tr stroke];

    // 8.0 px gate tick at 50% of 16.0 px full scale
    CGFloat gateX = barRect.origin.x + barRect.size.width * 0.5;
    double ratio = self.hasGroup ? std::min(1.0, std::max(0.03, self.parallax2xPx / 16.0)) : 0.0;
    BOOL passGate = self.measured && self.depthKnown && self.depthObservable;

    if (ratio > 0.0) {
        NSRect fr = NSMakeRect(barRect.origin.x, barRect.origin.y, barRect.size.width * ratio, barRect.size.height);
        NSBezierPath* fb = [NSBezierPath bezierPathWithRoundedRect:fr xRadius:rad yRadius:rad];
        NSColor* fc = passGate
            ? [NSColor colorWithSRGBRed:0.25 green:0.53 blue:0.62 alpha:1.0]
            : [NSColor colorWithSRGBRed:0.62 green:0.70 blue:0.74 alpha:1.0];
        [fc setFill];
        [fb fill];
    }

    NSBezierPath* tick = [NSBezierPath bezierPath];
    [tick moveToPoint:NSMakePoint(gateX, barRect.origin.y - 2)];
    [tick lineToPoint:NSMakePoint(gateX, barRect.origin.y + barRect.size.height + 2)];
    tick.lineWidth = 1.5;
    [[NSColor colorWithSRGBRed:0.24 green:0.42 blue:0.49 alpha:1.0] setStroke];
    [tick stroke];

    NSString* txt = nil;
    NSColor* tc = nil;
    if (!self.hasGroup) {
        txt = @"视差待测 · 可先选择输出，执行时回显条件";
        tc = [NSColor colorWithSRGBRed:0.35 green:0.46 blue:0.52 alpha:1.0];
    } else if (!self.measured) {
        txt = [NSString stringWithFormat:@"预估视差 %.1f px · 执行时确认深度条件", self.parallax2xPx];
        tc = [NSColor colorWithSRGBRed:0.35 green:0.46 blue:0.52 alpha:1.0];
    } else if (self.depthKnown && self.depthObservable) {
        txt = [NSString stringWithFormat:@"实测视差 %.1f px · 深度条件满足", self.parallax2xPx];
        tc = [NSColor colorWithSRGBRed:0.16 green:0.45 blue:0.52 alpha:1.0];
    } else {
        txt = [NSString stringWithFormat:@"实测视差 %.1f px · %@", self.parallax2xPx,
            self.depthKnown ? @"当前深度条件不足" : @"深度条件待确认"];
        tc = [NSColor colorWithSRGBRed:0.48 green:0.56 blue:0.60 alpha:1.0];
    }
    NSDictionary* attrs = @{
        NSFontAttributeName: [NSFont systemFontOfSize:11.5 weight:NSFontWeightMedium],
        NSForegroundColorAttributeName: tc
    };
    [txt drawAtPoint:NSMakePoint(222, 3) withAttributes:attrs];
}
@end

// Tab 4 Left Plot: Subpixel (dx, dy) Trajectory + 2x2 Quadrant Occupancy
@interface TrajectoryCanvasView : NSView
@property (nonatomic, strong) BurstGroupModel* group;
@end

@implementation TrajectoryCanvasView
- (BOOL)isFlipped { return YES; }
- (void)drawRect:(NSRect)dirtyRect {
    (void)dirtyRect;
    drawSubBox(NSInsetRect(self.bounds, 0.5, 0.5), @"防抖亚像素轨迹与四象限覆盖");

    CGFloat w = self.bounds.size.width;
    CGFloat h = self.bounds.size.height;
    NSRect plotRect = NSMakeRect(16, 34, w - 32, h - 98);
    CGFloat cx = NSMidX(plotRect);
    CGFloat cy = NSMidY(plotRect);
    CGFloat radius = std::min(plotRect.size.width, plotRect.size.height) * 0.44;

    // Plot background
    NSBezierPath* pbg = [NSBezierPath bezierPathWithRoundedRect:plotRect xRadius:6 yRadius:6];
    [[NSColor colorWithSRGBRed:0.976 green:0.988 blue:0.992 alpha:1.0] setFill];
    [pbg fill];
    [[NSColor colorWithSRGBRed:0.83 green:0.89 blue:0.91 alpha:1.0] setStroke];
    pbg.lineWidth = 1.0;
    [pbg stroke];

    // Concentric guide circles & crosshair axes
    for (int ring = 1; ring <= 2; ++ring) {
        CGFloat rr = radius * (ring * 0.5);
        NSBezierPath* c = [NSBezierPath bezierPathWithOvalInRect:NSMakeRect(cx - rr, cy - rr, rr * 2, rr * 2)];
        CGFloat dsh[2] = {3.0, 3.0};
        [c setLineDash:dsh count:2 phase:0];
        [[NSColor colorWithSRGBRed:0.82 green:0.88 blue:0.91 alpha:1.0] setStroke];
        c.lineWidth = 0.8;
        [c stroke];
    }

    NSBezierPath* axes = [NSBezierPath bezierPath];
    [axes moveToPoint:NSMakePoint(plotRect.origin.x + 10, cy)];
    [axes lineToPoint:NSMakePoint(NSMaxX(plotRect) - 10, cy)];
    [axes moveToPoint:NSMakePoint(cx, plotRect.origin.y + 10)];
    [axes lineToPoint:NSMakePoint(cx, NSMaxY(plotRect) - 10)];
    [[NSColor colorWithSRGBRed:0.75 green:0.83 blue:0.86 alpha:1.0] setStroke];
    axes.lineWidth = 1.0;
    [axes stroke];

    NSDictionary* axAttrs = @{
        NSFontAttributeName: [NSFont monospacedSystemFontOfSize:9.5 weight:NSFontWeightMedium],
        NSForegroundColorAttributeName: [NSColor colorWithSRGBRed:0.46 green:0.57 blue:0.62 alpha:1.0]
    };
    [@"+x" drawAtPoint:NSMakePoint(NSMaxX(plotRect) - 22, cy + 3) withAttributes:axAttrs];
    [@"+y" drawAtPoint:NSMakePoint(cx + 4, plotRect.origin.y + 6) withAttributes:axAttrs];

    BOOL quadSeen[4] = {NO, NO, NO, NO};
    int quadCount = 0;

    if (self.group && self.group.hasTelemetry && self.group.frames.count > 0) {
        double maxAbs = 1.0;
        for (FrameDiagItem* f in self.group.frames) {
            maxAbs = std::max(maxAbs, std::max(std::fabs(f.dxPx), std::fabs(f.dyPx)));
            int qx = (f.dxPx - std::floor(f.dxPx)) >= 0.5 ? 1 : 0;
            int qy = (f.dyPx - std::floor(f.dyPx)) >= 0.5 ? 1 : 0;
            quadSeen[qy * 2 + qx] = YES;
        }
        if (self.group.phaseQuadrants.count == 4)
            for (int q = 0; q < 4; ++q) quadSeen[q] = self.group.phaseQuadrants[q].integerValue > 0;
        int seenSum = (int)quadSeen[0] + (int)quadSeen[1] + (int)quadSeen[2] + (int)quadSeen[3];
        quadCount = seenSum;

        NSBezierPath* poly = [NSBezierPath bezierPath];
        NSMutableArray<NSValue*>* pts = [NSMutableArray arrayWithCapacity:self.group.frames.count];
        for (NSUInteger i = 0; i < self.group.frames.count; ++i) {
            FrameDiagItem* f = self.group.frames[i];
            CGFloat px = cx + (CGFloat)(f.dxPx / maxAbs) * radius;
            CGFloat py = cy - (CGFloat)(f.dyPx / maxAbs) * radius;
            [pts addObject:[NSValue valueWithPoint:NSMakePoint(px, py)]];
            if (i == 0) [poly moveToPoint:NSMakePoint(px, py)];
            else [poly lineToPoint:NSMakePoint(px, py)];
        }
        poly.lineWidth = 1.8;
        [[NSColor colorWithSRGBRed:0.31 green:0.53 blue:0.62 alpha:0.85] setStroke];
        [poly stroke];

        NSDictionary* lblAttrs = @{
            NSFontAttributeName: [NSFont monospacedDigitSystemFontOfSize:9.5 weight:NSFontWeightSemibold],
            NSForegroundColorAttributeName: [NSColor colorWithSRGBRed:0.16 green:0.28 blue:0.34 alpha:1.0]
        };
        for (NSUInteger i = 0; i < self.group.frames.count; ++i) {
            FrameDiagItem* f = self.group.frames[i];
            NSPoint p = [pts[i] pointValue];
            CGFloat rDot = f.isCrown ? 4.6 : 3.4;
            NSBezierPath* dot = [NSBezierPath bezierPathWithOvalInRect:NSMakeRect(p.x - rDot, p.y - rDot, rDot * 2, rDot * 2)];
            NSColor* dc = f.isShock
                ? [NSColor colorWithSRGBRed:0.78 green:0.46 blue:0.18 alpha:1.0]
                : (f.isCrown
                    ? [NSColor colorWithSRGBRed:0.16 green:0.46 blue:0.56 alpha:1.0]
                    : [NSColor colorWithSRGBRed:0.35 green:0.58 blue:0.66 alpha:1.0]);
            [dc setFill];
            [dot fill];
            [[NSColor whiteColor] setStroke];
            dot.lineWidth = 1.0;
            [dot stroke];

            if (self.group.frames.count <= 12 || i == 0 || f.isCrown || i + 1 == self.group.frames.count) {
                NSString* tag = [NSString stringWithFormat:@"#%d%@", f.idx1Based, f.isCrown ? @"♛" : (f.isShock ? @"!" : @"")];
                [tag drawAtPoint:NSMakePoint(p.x + 5, p.y - 7) withAttributes:lblAttrs];
            }
        }

        NSString* scaleTxt = [NSString stringWithFormat:@"满量程 ±%.1f px", maxAbs];
        [scaleTxt drawAtPoint:NSMakePoint(plotRect.origin.x + 8, NSMaxY(plotRect) - 16) withAttributes:axAttrs];
    } else {
        NSDictionary* emptyAttrs = @{
            NSFontAttributeName: [NSFont systemFontOfSize:11.5 weight:NSFontWeightMedium],
            NSForegroundColorAttributeName: [NSColor colorWithSRGBRed:0.42 green:0.53 blue:0.58 alpha:1.0]
        };
        NSString* msg = self.group ? @"轨迹坐标待引擎测量" : @"导入连拍组后绘制 #1..N 漂移轨迹";
        NSSize sz = [msg sizeWithAttributes:emptyAttrs];
        [msg drawAtPoint:NSMakePoint(cx - sz.width * 0.5, cy - sz.height * 0.5) withAttributes:emptyAttrs];
    }

    if (self.group.phaseQuadrants.count == 4) {
        quadCount = 0;
        for (int q = 0; q < 4; ++q) {
            quadSeen[q] = self.group.phaseQuadrants[q].integerValue > 0;
            if (quadSeen[q]) ++quadCount;
        }
    }
    // Bottom 2x2 Subpixel Quadrant Mini-Grid + Telemetry Summary
    CGFloat qx0 = 16;
    CGFloat qy0 = NSMaxY(plotRect) + 10;
    CGFloat cell = 11.0;
    for (int r = 0; r < 2; ++r) {
        for (int c = 0; c < 2; ++c) {
            NSRect qr = NSMakeRect(qx0 + c * (cell + 2), qy0 + r * (cell + 2), cell, cell);
            NSBezierPath* qbp = [NSBezierPath bezierPathWithRoundedRect:qr xRadius:2.5 yRadius:2.5];
            if (quadSeen[r * 2 + c]) {
                [[NSColor colorWithSRGBRed:0.27 green:0.51 blue:0.60 alpha:1.0] setFill];
            } else {
                [[NSColor colorWithSRGBRed:0.85 green:0.90 blue:0.92 alpha:1.0] setFill];
            }
            [qbp fill];
            [[NSColor colorWithSRGBRed:0.66 green:0.77 blue:0.81 alpha:1.0] setStroke];
            qbp.lineWidth = 0.8;
            [qbp stroke];
        }
    }

    NSDictionary* sumBold = @{
        NSFontAttributeName: [NSFont systemFontOfSize:11.5 weight:NSFontWeightSemibold],
        NSForegroundColorAttributeName: [NSColor colorWithSRGBRed:0.17 green:0.31 blue:0.37 alpha:1.0]
    };
    NSDictionary* sumSub = @{
        NSFontAttributeName: [NSFont systemFontOfSize:11.0 weight:NSFontWeightRegular],
        NSForegroundColorAttributeName: [NSColor colorWithSRGBRed:0.35 green:0.46 blue:0.52 alpha:1.0]
    };
    NSString* qText = self.group && (self.group.hasTelemetry || self.group.phaseQuadrants.count == 4)
        ? [NSString stringWithFormat:@"四象限相位覆盖: %d / 4", quadCount]
        : @"四象限相位覆盖: -- / 4";
    [qText drawAtPoint:NSMakePoint(qx0 + 32, qy0 - 2) withAttributes:sumBold];

    NSString* shutCn = @"待检视";
    if (self.group) {
        NSString* st = [self.group.shutterType isEqualToString:@"global"] ? @"全域快门"
                     : ([self.group.shutterType isEqualToString:@"electronic"] ? @"电子快门" : @"机械快门");
        NSString* shockStr = self.group.shutterShock ? @"首帧微震已标记" : @"";
        shutCn = [NSString stringWithFormat:@"%@ · %@ · 最大漂移 %.1fpx", st, shockStr, self.group.maxDrift2xPx * 0.5];
    }
    [shutCn drawAtPoint:NSMakePoint(qx0 + 32, qy0 + 14) withAttributes:sumSub];
}
@end

// Tab 4 Right Plot: 5-Zone MTF50 / Astigmatism / LCA Radar Diagram
@interface OpticsRadarView : NSView
@property (nonatomic, strong) OpticsTelemetryModel* telemetry;
@end

@implementation OpticsRadarView
- (BOOL)isFlipped { return YES; }
- (void)drawRect:(NSRect)dirtyRect {
    (void)dirtyRect;
    drawSubBox(NSInsetRect(self.bounds, 0.5, 0.5), @"原始 RAW 8× 光学体检雷达");

    CGFloat w = self.bounds.size.width;
    CGFloat h = self.bounds.size.height;

    // Tier badge on top-right
    NSString* badge = (self.telemetry && self.telemetry.tierBadge.length > 0) ? self.telemetry.tierBadge : @"待实测";
    NSDictionary* bAttrs = @{
        NSFontAttributeName: [NSFont systemFontOfSize:10.5 weight:NSFontWeightSemibold],
        NSForegroundColorAttributeName: (self.telemetry && self.telemetry.populated)
            ? [NSColor colorWithSRGBRed:0.16 green:0.42 blue:0.50 alpha:1.0]
            : [NSColor colorWithSRGBRed:0.40 green:0.51 blue:0.56 alpha:1.0]
    };
    NSSize bSz = [badge sizeWithAttributes:bAttrs];
    NSRect bRect = NSMakeRect(w - bSz.width - 26, 7, bSz.width + 14, 18);
    NSBezierPath* bBp = [NSBezierPath bezierPathWithRoundedRect:bRect xRadius:5 yRadius:5];
    [[NSColor colorWithSRGBRed:0.86 green:0.92 blue:0.95 alpha:1.0] setFill];
    [bBp fill];
    [badge drawAtPoint:NSMakePoint(bRect.origin.x + 7, bRect.origin.y + 2) withAttributes:bAttrs];

    NSRect plotRect = NSMakeRect(16, 34, w - 32, h - 98);
    NSBezierPath* pbg = [NSBezierPath bezierPathWithRoundedRect:plotRect xRadius:6 yRadius:6];
    [[NSColor colorWithSRGBRed:0.976 green:0.988 blue:0.992 alpha:1.0] setFill];
    [pbg fill];
    [[NSColor colorWithSRGBRed:0.83 green:0.89 blue:0.91 alpha:1.0] setStroke];
    pbg.lineWidth = 1.0;
    [pbg stroke];

    CGFloat cx = NSMidX(plotRect);
    CGFloat cy = NSMidY(plotRect);
    CGFloat rx = plotRect.size.width * 0.30;
    CGFloat ry = plotRect.size.height * 0.31;

    // 5-Zone X-frame + concentric optical field rings
    for (int k = 1; k <= 3; ++k) {
        CGFloat f = k / 3.0;
        NSBezierPath* el = [NSBezierPath bezierPathWithOvalInRect:NSMakeRect(cx - rx * f * 1.15, cy - ry * f * 1.15,
                                                                             rx * f * 2.3, ry * f * 2.3)];
        CGFloat dsh[2] = {3.0, 3.0};
        [el setLineDash:dsh count:2 phase:0];
        [[NSColor colorWithSRGBRed:0.83 green:0.89 blue:0.91 alpha:1.0] setStroke];
        el.lineWidth = 0.8;
        [el stroke];
    }

    NSPoint zonePts[5] = {
        NSMakePoint(cx, cy),             // Center
        NSMakePoint(cx - rx, cy - ry),   // NW
        NSMakePoint(cx + rx, cy - ry),   // NE
        NSMakePoint(cx - rx, cy + ry),   // SW
        NSMakePoint(cx + rx, cy + ry)    // SE
    };
    NSString* zoneNames[5] = {@"中心", @"NW", @"NE", @"SW", @"SE"};

    // Diagonals from Center to 4 corners
    NSBezierPath* diag = [NSBezierPath bezierPath];
    for (int i = 1; i < 5; ++i) {
        [diag moveToPoint:zonePts[0]];
        [diag lineToPoint:zonePts[i]];
    }
    [[NSColor colorWithSRGBRed:0.78 green:0.85 blue:0.88 alpha:1.0] setStroke];
    diag.lineWidth = 1.0;
    [diag stroke];

    BOOL pop = (self.telemetry && self.telemetry.populated);
    double mtf[5] = {
        pop ? self.telemetry.centerMtf : 0.0,
        pop ? self.telemetry.nwMtf : 0.0,
        pop ? self.telemetry.neMtf : 0.0,
        pop ? self.telemetry.swMtf : 0.0,
        pop ? self.telemetry.seMtf : 0.0
    };
    double astig[5] = {
        0.0,
        pop ? self.telemetry.nwAstig : 0.0,
        pop ? self.telemetry.neAstig : 0.0,
        pop ? self.telemetry.swAstig : 0.0,
        pop ? self.telemetry.seAstig : 0.0
    };
    BOOL zValid[5] = {
        pop ? self.telemetry.centerValid : YES,
        pop ? self.telemetry.nwValid : YES,
        pop ? self.telemetry.neValid : YES,
        pop ? self.telemetry.swValid : YES,
        pop ? self.telemetry.seValid : YES
    };

    if (pop) {
        // 4-corner MTF envelope polygon scaled relative to 0.50 lp/px
        int order[4] = {1, 2, 4, 3}; // NW -> NE -> SE -> SW
        NSBezierPath* env = [NSBezierPath bezierPath];
        for (int k = 0; k < 4; ++k) {
            int zi = order[k];
            double s = zValid[zi] ? std::min(1.15, std::max(0.25, mtf[zi] / 0.45)) : 0.30;
            NSPoint p = NSMakePoint(cx + (zonePts[zi].x - cx) * s, cy + (zonePts[zi].y - cy) * s);
            if (k == 0) [env moveToPoint:p];
            else [env lineToPoint:p];
        }
        [env closePath];
        [[NSColor colorWithSRGBRed:0.31 green:0.53 blue:0.62 alpha:0.16] setFill];
        [env fill];
        [[NSColor colorWithSRGBRed:0.28 green:0.50 blue:0.59 alpha:0.85] setStroke];
        env.lineWidth = 1.6;
        [env stroke];
    }

    NSDictionary* zTitleAttrs = @{
        NSFontAttributeName: [NSFont systemFontOfSize:10.5 weight:NSFontWeightSemibold],
        NSForegroundColorAttributeName: [NSColor colorWithSRGBRed:0.20 green:0.34 blue:0.40 alpha:1.0]
    };
    NSDictionary* zValAttrs = @{
        NSFontAttributeName: [NSFont monospacedDigitSystemFontOfSize:11.0 weight:NSFontWeightBold],
        NSForegroundColorAttributeName: [NSColor colorWithSRGBRed:0.14 green:0.38 blue:0.47 alpha:1.0]
    };
    NSDictionary* zMuteAttrs = @{
        NSFontAttributeName: [NSFont monospacedDigitSystemFontOfSize:10.0 weight:NSFontWeightMedium],
        NSForegroundColorAttributeName: [NSColor colorWithSRGBRed:0.48 green:0.57 blue:0.61 alpha:1.0]
    };

    for (int i = 0; i < 5; ++i) {
        NSPoint p = zonePts[i];
        CGFloat ew = 16.0;
        CGFloat eh = (i == 0) ? 16.0 : std::max(9.0, 16.0 * (1.0 - std::min(0.45, astig[i])));
        NSRect nodeR = NSMakeRect(p.x - ew * 0.5, p.y - eh * 0.5, ew, eh);
        NSBezierPath* el = [NSBezierPath bezierPathWithOvalInRect:nodeR];
        if (pop && zValid[i]) {
            [[NSColor colorWithSRGBRed:0.84 green:0.92 blue:0.95 alpha:1.0] setFill];
            [el fill];
            [[NSColor colorWithSRGBRed:0.26 green:0.49 blue:0.58 alpha:1.0] setStroke];
        } else {
            [[NSColor colorWithSRGBRed:0.92 green:0.95 blue:0.96 alpha:1.0] setFill];
            [el fill];
            [[NSColor colorWithSRGBRed:0.68 green:0.77 blue:0.81 alpha:1.0] setStroke];
        }
        el.lineWidth = 1.4;
        [el stroke];

        NSString* valStr = !pop ? @"--"
                         : (!zValid[i] ? @"拒检" : [NSString stringWithFormat:@"%.2f", mtf[i]]);
        NSString* fullLbl = [NSString stringWithFormat:@"%@ %@", zoneNames[i], valStr];
        NSSize lsz = [fullLbl sizeWithAttributes:(pop && zValid[i]) ? zValAttrs : zMuteAttrs];
        CGFloat ly = (i == 1 || i == 2) ? (p.y - 24) : (p.y + 11);
        if (i == 0) ly = p.y - 25;
        [fullLbl drawAtPoint:NSMakePoint(p.x - lsz.width * 0.5, ly)
              withAttributes:(pop && zValid[i]) ? zValAttrs : zMuteAttrs];
        (void)zTitleAttrs;
    }

    // Bottom LCA & Astigmatism readout
    CGFloat by = NSMaxY(plotRect) + 8;
    NSDictionary* sumBold = @{
        NSFontAttributeName: [NSFont systemFontOfSize:11.5 weight:NSFontWeightSemibold],
        NSForegroundColorAttributeName: [NSColor colorWithSRGBRed:0.17 green:0.31 blue:0.37 alpha:1.0]
    };
    NSDictionary* sumSub = @{
        NSFontAttributeName: [NSFont systemFontOfSize:11.0 weight:NSFontWeightRegular],
        NSForegroundColorAttributeName: [NSColor colorWithSRGBRed:0.35 green:0.46 blue:0.52 alpha:1.0]
    };
    int validZones = 0;
    for (int i = 0; i < 5; ++i) if (zValid[i]) ++validZones;
    NSString* l1 = pop
        ? [NSString stringWithFormat:@"MTF50 · %d/5 区有效 · cycles/raw-pixel", validZones]
        : @"五区 MTF50 (lp/px) · 像散椭圆 · 色差矢量";
    NSString* l2 = pop
        ? [NSString stringWithFormat:@"边角 LCA 位移: R %@ / B %@",
            self.telemetry.redLcaValid ? [NSString stringWithFormat:@"%.2fpx", self.telemetry.lcaRedPx] : @"--",
            self.telemetry.blueLcaValid ? [NSString stringWithFormat:@"%.2fpx", self.telemetry.lcaBluePx] : @"--"]
        : @"点击下方「运行原始 RAW 8× 光学精测」生成实测雷达";
    [l1 drawAtPoint:NSMakePoint(16, by) withAttributes:sumBold];
    [l2 drawAtPoint:NSMakePoint(16, by + 16) withAttributes:sumSub];
}
@end

@interface TabPageCanvasView : NSView
@property (nonatomic, strong) NSArray<NSValue*>* cardRects;
@property (nonatomic, strong) NSArray<NSValue*>* subBoxRects;
@property (nonatomic, strong) NSArray<NSString*>* subBoxTitles;
@property (nonatomic, strong) NSArray<NSValue*>* dividerLines; // NSRect(x1, y, x2-x1, 1)
@end

@implementation TabPageCanvasView
- (BOOL)isFlipped { return YES; }
- (void)drawRect:(NSRect)dirtyRect {
    (void)dirtyRect;
    for (NSValue* v in self.cardRects) {
        drawRoundedCard([v rectValue], 10.0);
    }
    for (NSUInteger i = 0; i < self.subBoxRects.count; ++i) {
        NSString* t = (i < self.subBoxTitles.count) ? self.subBoxTitles[i] : nil;
        drawSubBox([self.subBoxRects[i] rectValue], t);
    }
    for (NSValue* v in self.dividerLines) {
        NSRect r = [v rectValue];
        NSBezierPath* ln = [NSBezierPath bezierPath];
        [ln moveToPoint:NSMakePoint(r.origin.x, r.origin.y + 0.5)];
        [ln lineToPoint:NSMakePoint(r.origin.x + r.size.width, r.origin.y + 0.5)];
        ln.lineWidth = 1.0;
        [[NSColor colorWithSRGBRed:0.85 green:0.90 blue:0.92 alpha:1.0] setStroke];
        [ln stroke];
    }
}
@end

@interface StudioCanvasView : NSView
@property (nonatomic, strong) NSImage* gxLogo;
@property (nonatomic, strong) NSImage* nijinLockup;
@property (nonatomic, assign) NSRect leftUpperCardRect;
@property (nonatomic, assign) NSRect leftLowerCardRect;
@property (nonatomic, copy) void (^onResizeBounds)(NSRect newBounds);
@end

@implementation StudioCanvasView
- (BOOL)isFlipped { return YES; }
- (void)setFrameSize:(NSSize)newSize {
    [super setFrameSize:newSize];
    if (self.onResizeBounds) self.onResizeBounds(self.bounds);
    [self setNeedsDisplay:YES];
}
- (void)drawRect:(NSRect)dirtyRect {
    (void)dirtyRect;
    CGFloat W = self.bounds.size.width;
    CGFloat H = self.bounds.size.height;

    // Muted greyish-aqua studio canvas (#E7EFF2)
    [[NSColor colorWithSRGBRed:0.906 green:0.937 blue:0.949 alpha:1.0] setFill];
    NSRectFill(self.bounds);

    // Top Header Bar (0, 0, W, 56)
    NSRect hdr = NSMakeRect(0, 0, W, 56);
    [[NSColor colorWithSRGBRed:0.978 green:0.989 blue:0.993 alpha:1.0] setFill];
    NSRectFill(hdr);

    // 3px Muted Greyish-Aqua Top Accent Stripe (#548296)
    [[NSColor colorWithSRGBRed:0.329 green:0.510 blue:0.588 alpha:1.0] setFill];
    NSRectFill(NSMakeRect(0, 0, W, 3));

    // Header bottom crisp border
    [[NSColor colorWithSRGBRed:0.765 green:0.831 blue:0.863 alpha:1.0] setFill];
    NSRectFill(NSMakeRect(0, 55, W, 1));

    // Header Left: NijiN.LiteBurstSuite + pill nijinLBS
    NSDictionary* titleAttrs = @{
        NSFontAttributeName: [NSFont boldSystemFontOfSize:17.5],
        NSForegroundColorAttributeName: [NSColor colorWithSRGBRed:0.13 green:0.20 blue:0.24 alpha:1.0]
    };
    NSString* brandTitle = @"NijiN.LiteBurstSuite";
    NSSize btsz = [brandTitle sizeWithAttributes:titleAttrs];
    [brandTitle drawAtPoint:NSMakePoint(20, (56.0 - btsz.height) * 0.5 + 1.0) withAttributes:titleAttrs];

    NSRect pillRect = NSMakeRect(20 + btsz.width + 10, 19, 64, 20);
    NSBezierPath* pillBp = [NSBezierPath bezierPathWithRoundedRect:pillRect xRadius:5 yRadius:5];
    [[NSColor colorWithSRGBRed:0.855 green:0.914 blue:0.935 alpha:1.0] setFill];
    [pillBp fill];
    NSDictionary* pillAttrs = @{
        NSFontAttributeName: [NSFont systemFontOfSize:11.0 weight:NSFontWeightSemibold],
        NSForegroundColorAttributeName: [NSColor colorWithSRGBRed:0.24 green:0.43 blue:0.50 alpha:1.0]
    };
    NSSize psz = [@"nijinLBS" sizeWithAttributes:pillAttrs];
    [@"nijinLBS" drawAtPoint:NSMakePoint(pillRect.origin.x + (pillRect.size.width - psz.width) * 0.5,
                                         pillRect.origin.y + (pillRect.size.height - psz.height) * 0.5)
              withAttributes:pillAttrs];

    // Header Right: tightly cropped GX Cross-Star left of NijiN
    CGFloat nx = W - 20 - heraw_brand::kNijin1xW;
    CGFloat divX = nx - 7;
    CGFloat gxX = divX - 8 - heraw_brand::kGx1xW;
    CGFloat gxY = (56.0 - heraw_brand::kGx1xH) * 0.5 + 1.0;
    CGFloat njY = (56.0 - heraw_brand::kNijin1xH) * 0.5 + 1.0;

    if (self.gxLogo) {
        [self.gxLogo drawInRect:NSMakeRect(gxX, gxY, heraw_brand::kGx1xW, heraw_brand::kGx1xH)
                       fromRect:NSZeroRect operation:NSCompositingOperationSourceOver fraction:1.0
                 respectFlipped:YES hints:@{NSImageHintInterpolation: @(NSImageInterpolationHigh)}];
    }
    [[NSColor colorWithSRGBRed:0.78 green:0.84 blue:0.87 alpha:1.0] setFill];
    NSRectFill(NSMakeRect(divX, 15, 1, 27));
    if (self.nijinLockup) {
        [self.nijinLockup drawInRect:NSMakeRect(nx, njY, heraw_brand::kNijin1xW, heraw_brand::kNijin1xH)
                            fromRect:NSZeroRect operation:NSCompositingOperationSourceOver fraction:1.0
                      respectFlipped:YES hints:@{NSImageHintInterpolation: @(NSImageInterpolationHigh)}];
    }

    // Left Persistent Dock Cards
    if (self.leftUpperCardRect.size.width > 0) drawRoundedCard(self.leftUpperCardRect, 10.0);
    if (self.leftLowerCardRect.size.width > 0) drawRoundedCard(self.leftLowerCardRect, 10.0);

    // Bottom Global Action Bar (0, H - 56, W, 56)
    NSRect bot = NSMakeRect(0, H - 56, W, 56);
    [[NSColor colorWithSRGBRed:0.978 green:0.989 blue:0.993 alpha:1.0] setFill];
    NSRectFill(bot);
    [[NSColor colorWithSRGBRed:0.765 green:0.831 blue:0.863 alpha:1.0] setFill];
    NSRectFill(NSMakeRect(0, H - 56, W, 1));
}
@end

@interface DropWindow : NSWindow
@property (nonatomic, copy) void (^onDropPaths)(NSArray<NSString*>* paths);
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

@interface NijiLbsApplication : NSApplication
@property (nonatomic, copy) void (^onCmdO)(void);
@property (nonatomic, copy) void (^onDeleteGroup)(void);
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
            if ([ch isEqualToString:@"o"]) { if (self.onCmdO) { self.onCmdO(); return; } }
            if ([ch isEqualToString:@"x"] && [self sendAction:@selector(cut:) to:nil from:self]) return;
            if ([ch isEqualToString:@"c"] && [self sendAction:@selector(copy:) to:nil from:self]) return;
            if ([ch isEqualToString:@"v"] && [self sendAction:@selector(paste:) to:nil from:self]) return;
            if ([ch isEqualToString:@"a"] && [self sendAction:@selector(selectAll:) to:nil from:self]) return;
            if ([ch isEqualToString:@"z"] && [self sendAction:NSSelectorFromString(@"undo:") to:nil from:self]) return;
        } else if (mods == (NSEventModifierFlagCommand | NSEventModifierFlagShift)) {
            if ([ch isEqualToString:@"z"] && [self sendAction:NSSelectorFromString(@"redo:") to:nil from:self]) return;
        } else if (mods == 0 && ch.length > 0) {
            unichar uc = [ch characterAtIndex:0];
            if (uc == NSDeleteCharacter || uc == NSBackspaceCharacter || uc == NSDeleteFunctionKey) {
                NSResponder* fr = self.keyWindow.firstResponder;
                if (![fr isKindOfClass:[NSTextView class]] && self.onDeleteGroup) {
                    self.onDeleteGroup();
                    return;
                }
            }
        }
    }
    [super sendEvent:event];
}
@end

// ============================================================================
// Preferences Suite & Fallback
// ============================================================================

static NSString* const kPrefSuite       = @"com.nijin.liteburstsuite";
static NSString* const kPrefSuiteLegacy = @"com.nijin.liteburstscaler";
static NSString* const kPrefCliPath     = @"NijinLbsCustomCliPath";
static NSString* const kPrefLibRawPath  = @"NijinLbsCustomLibRawPath";

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

static NSUserDefaults* nijinLegacyPrefs(void) {
    return [[NSUserDefaults alloc] initWithSuiteName:kPrefSuiteLegacy];
}

// ============================================================================
// Main Controller (AppDelegate)
// ============================================================================

@interface AppDelegate : NSObject <NSApplicationDelegate, NSWindowDelegate, NSTableViewDataSource, NSTableViewDelegate, NSTextFieldDelegate>
@property (strong) DropWindow* window;
@property (strong) StudioCanvasView* canvas;

// Input & Planner State
@property (strong) NSMutableArray<NSString*>* rawInputPaths;
@property (strong) NSMutableDictionary<NSString*, NSString*>* fileByBasename;
@property (strong) NSMutableArray<BurstGroupModel*>* burstGroups;
@property (assign) NSInteger planSerial;
@property (strong) NSTask* planTask;
@property (strong) OpticsTelemetryModel* opticsModel;

// Left Persistent Dock
@property (strong) NSTextField* lblQueueHeader;
@property (strong) NSButton* chkSortTime;
@property (strong) NSButton* chkRecursive;
@property (strong) EmptyDropWellView* emptyWell;
@property (strong) NSScrollView* svQueue;
@property (strong) BurstQueueTableView* tblQueue;
@property (strong) NSButton* btnAddFiles;
@property (strong) NSButton* btnAddFolder;
@property (strong) NSButton* btnExportTopK;
@property (strong) NSButton* btnClearQueue;

@property (strong) NSTextField* lblInspectorHeader;
@property (strong) NSTextField* lblInspectorHint;
@property (strong) NSTextField* lblInspectorEmpty;
@property (strong) NSScrollView* svFrames;
@property (strong) NSTableView* tblFrames;

// Right 4-Tab Bar & Pages
@property (strong) SuiteTabBarView* tabBar;
@property (strong) TabPageCanvasView* page0;
@property (strong) TabPageCanvasView* page1;
@property (strong) TabPageCanvasView* page2;
@property (strong) TabPageCanvasView* page3;

// Shared Canonical Motion State: "auto" | "first" | "middle" | "clean-plate" | "virtual-nd" | "strobe"
@property (copy) NSString* motionMode;

// Tab 1 Controls
@property (strong) NSTextField* lblT1Card1Title;
@property (strong) PortalPillButton* btnPortalQuad;
@property (strong) NSTextField* lblPresetCap;
@property (strong) NSSegmentedControl* segPreset;
@property (strong) NSTextField* lblMotionCap;
@property (strong) NSSegmentedControl* segMotionTab1;
@property (strong) PortalPillButton* btnPortalStrobe;
@property (strong) NSTextField* lblOlpfCap;
@property (strong) NSPopUpButton* popOlpf;

@property (strong) NSTextField* lblT1Card2Title;
@property (strong) NSTextField* lblT1Card2Sub;
@property (strong) NSTextField* lblOutDirCap;
@property (strong) NSTextField* edtOutDir;
@property (strong) NSButton* btnBrowseOut;
@property (strong) NSButton* btnClearOut;

@property (strong) NSButton* chkEmitRaw;
@property (strong) NSPopUpButton* popRawFmt;
@property (strong) NSButton* chkRelayRaw;

@property (strong) NSButton* chkEmitRgb;
@property (strong) NSPopUpButton* popRgbFmt;
@property (strong) NSTextField* lblBppCap;
@property (strong) NSTextField* edtJxsBpp;
@property (strong) NSSegmentedControl* segColorSpace;

@property (strong) NSButton* chkEmitField;
@property (strong) NSPopUpButton* popFieldMode;
@property (strong) PortalPillButton* btnPortalFields;

@property (strong) NSButton* chkEmitTransientTab1;
@property (strong) PortalPillButton* btnPortalRefl;

@property (strong) NSTextField* lblT1Card3Title;
@property (strong) NSButton* btnResetPreset;

@property (strong) NSTextField* lblDenoise;
@property (strong) NSSlider* sldDenoise;
@property (strong) NSTextField* edtDenoise;

@property (strong) NSTextField* lblChroma;
@property (strong) NSSlider* sldChroma;
@property (strong) NSTextField* edtChroma;

@property (strong) NSTextField* lblDeghost;
@property (strong) NSSlider* sldDeghost;
@property (strong) NSTextField* edtDeghost;

@property (strong) NSTextField* lblPsfIters;
@property (strong) NSSlider* sldPsfIters;
@property (strong) NSTextField* edtPsfIters;

@property (strong) NSTextField* lblPsfSigma;
@property (strong) NSSlider* sldPsfSigma;
@property (strong) NSTextField* edtPsfSigma;

@property (strong) NSTextField* lblEv;
@property (strong) NSSlider* sldEv;
@property (strong) NSTextField* edtEv;

@property (strong) NSButton* chkSiliconClean;
@property (strong) NSButton* chkIbp;
@property (strong) NSButton* chkAutoLca;
@property (strong) NSButton* chkAutoExp;
@property (strong) NSButton* chkRolloff;
@property (strong) NSButton* chkDither;

@property (strong) NSTextField* lblBackendCap;
@property (strong) NSPopUpButton* popBackend;
@property (strong) NSTextField* lblGrpSzCap;
@property (strong) NSTextField* edtGrpSz;
@property (strong) NSTextField* lblThreadsCap;
@property (strong) NSTextField* edtThreads;

@property (strong) NSTextField* lblEngineStrip;
@property (strong) NSButton* btnEngineSheet;
@property (copy) NSString* customCliPath;
@property (copy) NSString* customLibRawPath;

// Tab 2 Controls
@property (strong) NSTextField* lblT2Card1Title;
@property (strong) NSButton* radT2Freeze;
@property (strong) NSSegmentedControl* segT2FreezePose;
@property (strong) NSButton* radT2Clean;
@property (strong) NSTextField* lblCleanSens;
@property (strong) NSSlider* sldCleanSens;
@property (strong) NSTextField* edtCleanSens;
@property (strong) NSButton* radT2Vnd;
@property (strong) NSTextField* lblVndThresh;
@property (strong) NSSlider* sldVndThresh;
@property (strong) NSTextField* edtVndThresh;
@property (strong) NSButton* radT2Strobe;
@property (strong) NSTextField* lblStrobeStep;
@property (strong) NSPopUpButton* popStrobeStep;
@property (strong) NSSegmentedControl* segStrobeFade;

@property (strong) NSTextField* lblT2Card2Title;
@property (strong) NSButton* chkEmitTransientTab2;
@property (strong) NSTextField* lblTransSigma;
@property (strong) NSSlider* sldTransSigma;
@property (strong) NSTextField* edtTransSigma;
@property (strong) NSSegmentedControl* segTransColor;

@property (strong) NSButton* chkReflection;
@property (strong) NSSegmentedControl* segReflectionMode;
@property (strong) NSTextField* lblDriftReadout;

@property (strong) NSTextField* lblT2Card3Title;
@property (strong) ParallaxGateView* parallaxGateView;
@property (strong) NSButton* chkExportParallax;
@property (strong) NSTextField* lblParallaxAmp;
@property (strong) NSSlider* sldParallaxAmp;
@property (strong) NSTextField* edtParallaxAmp;

@property (strong) NSButton* chkExportRefocus;
@property (strong) NSTextField* lblRefocusPos;
@property (strong) NSSlider* sldRefocusPos;
@property (strong) NSTextField* edtRefocusPos;
@property (strong) NSTextField* lblBokehStr;
@property (strong) NSSlider* sldBokehStr;
@property (strong) NSTextField* edtBokehStr;

// Tab 3 Controls
@property (strong) NSTextField* lblT3Card1Title;
@property (strong) NSTextField* lblCarrierCap;
@property (strong) NSSegmentedControl* segCarrierTab3;
@property (strong) NSTextField* lblMatrixHeader;
@property (strong) NSButton* btnFieldsCore3;
@property (strong) NSButton* btnFieldsAll;
@property (strong) NSMutableArray<NSButton*>* chkFields;
@property (strong) NSTextField* lblFieldsGateNote;

@property (strong) NSTextField* lblT3Card2Title;
@property (strong) NSButton* chkExportTiles;
@property (strong) NSTextField* lblTileSizeCap;
@property (strong) NSSegmentedControl* segTileSize;
@property (strong) NSTextField* lblTileConfCap;
@property (strong) NSSlider* sldTileConf;
@property (strong) NSTextField* edtTileConf;

// Tab 4 Controls
@property (strong) TrajectoryCanvasView* trajCanvas;
@property (strong) OpticsRadarView* radarCanvas;
@property (strong) AquaButton* btnRunOptics;
@property (strong) NSTextField* lblOisStatus;
@property (strong) NSTextField* lblT4SiliconTitle;
@property (strong) NSTextField* lblSiliconReadout;
@property (strong) NSTextField* lblT4ExportTitle;
@property (strong) NSTextField* lblLcpItemsCap;
@property (strong) NSButton* chkLcpLca;
@property (strong) NSButton* chkLcpVignette;
@property (strong) NSButton* chkLcpDistortion;
@property (strong) NSButton* btnExportLcp;
@property (strong) NSButton* btnExportOpticsPng;
@property (strong) NSButton* btnExportOpticsJson;

// Bottom Global Action Bar
@property (strong) AquaButton* btnRun;
@property (strong) NSTextField* lblStageStatus;
@property (strong) NSTextField* lblPct;
@property (strong) AquaProgressView* progView;
@property (strong) NSButton* btnOpenOutDir;

// Execution State
@property (strong) NSTask* currentTask;
@property (copy) NSArray<NSArray<NSString*>*>* queuedArguments;
@property (assign) BOOL userStoppedJob;
@property (assign) BOOL isScreenshotMode;
@property (copy) NSString* screenshotOpticsPath;
@property (assign) BOOL suppressPrefSave;
@end

@implementation AppDelegate

static const FieldSpec kFieldSpecs[11] = {
    {"motion",     @"运动 / 星野天空蒙版 (Motion)",     0, false},
    {"phase",      @"亚像素超分相位置信度 (Phase)",      0, false},
    {"focus",      @"二阶拉普拉斯对焦场 (Focus)",       0, false},
    {"depth",      @"极线视差逆深度场 (L1 Depth)*",     1, true},
    {"defocus",    @"近摄散焦符号场 (L2 Defocus)*",     1, true},
    {"specular",   @"镜面闪烁碎高光场 (L4 Specular)",   1, false},
    {"sigma",      @"逐像素 1σ 绝对误差棒 (L10c)",      2, false},
    {"coherence",  @"摩尔纹与微纹理相干场 (L6)",        2, false},
    {"velocity",   @"动体速度矢量幅值场 (L7)",          2, false},
    {"flicker",    @"人造频闪光源分割场 (L5)*",         3, true},
    {"turbulence", @"大气热浪纹影湍流场 (L8 BOS)*",     3, true}
};

static NSArray<NSString*>* rawFormatCodes(void) {
    return @[@"dng", @"dng-linear", @"dng-fp16", @"dng-hq", @"dng-raw"];
}

static NSArray<NSString*>* rgbFormatCodes(void) {
    return @[@"tiff-deflate", @"png", @"jxs", @"jxs-lossless", @"tiff-raw"];
}

static NSArray<NSString*>* fieldModeCodes(void) {
    return @[@"tiff-alpha", @"split", @"rgb"];
}

static NSArray<NSString*>* olpfModeCodes(void) {
    return @[@"auto", @"none", @"weak4", @"gauss16"];
}

// ============================================================================
// Menu & CLI Discovery
// ============================================================================

- (void)setupMainMenu {
    NSMenu* mainMenu = [[NSMenu alloc] initWithTitle:@"MainMenu"];

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

    NSMenuItem* fileItem = [[NSMenuItem alloc] initWithTitle:@"文件" action:nil keyEquivalent:@""];
    NSMenu* fileMenu = [[NSMenu alloc] initWithTitle:@"文件"];
    NSMenuItem* openFilesItem = [[NSMenuItem alloc] initWithTitle:@"添加 RAW 文件或目录..." action:@selector(onOpenMixedPanel:) keyEquivalent:@"o"];
    openFilesItem.target = self;
    [fileMenu addItem:openFilesItem];
    fileItem.submenu = fileMenu;
    [mainMenu addItem:fileItem];

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
    if (includeSaved && !self.isScreenshotMode) {
        if ([self isValidExecutable:self.customCliPath]) return self.customCliPath;
        NSString* saved = [nijinPrefs() stringForKey:kPrefCliPath];
        if (!saved) saved = [nijinLegacyPrefs() stringForKey:kPrefCliPath];
        if ([self isValidExecutable:saved]) return saved;
    }

    NSString* selfPath = [[NSBundle mainBundle] executablePath];
    if (!selfPath) selfPath = [[[NSProcessInfo processInfo] arguments] firstObject];

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

- (void)updateEngineStatusStrip {
    NSString* cli = [self autoDetectCliPath:YES];
    NSString* cliShown = cli ? [cli lastPathComponent] : @"请选择 nijinLBS 引擎";
    NSString* lrShown = (self.customLibRawPath && self.customLibRawPath.length > 0)
        ? [NSString stringWithFormat:@"外部 %@", [self.customLibRawPath lastPathComponent]]
        : @"内置 LibRaw 0.23-HE*";
    self.lblEngineStrip.stringValue = [NSString stringWithFormat:@"引擎: %@  ·  解码器: %@", cliShown, lrShown];
}

- (NSString*)resolveActiveCliOrPrompt {
    NSString* found = [self autoDetectCliPath:YES];
    if (found) return found;
    [self onOpenEngineSheet:nil];
    return [self autoDetectCliPath:YES];
}

- (void)onOpenEngineSheet:(id)sender {
    (void)sender;
    NSWindow* sheet = [[NSWindow alloc] initWithContentRect:NSMakeRect(0, 0, 520, 186)
        styleMask:NSWindowStyleMaskTitled backing:NSBackingStoreBuffered defer:NO];
    sheet.appearance = [NSAppearance appearanceNamed:NSAppearanceNameAqua];
    NSView* box = [[NSView alloc] initWithFrame:NSMakeRect(0, 0, 520, 186)];
    sheet.contentView = box;

    NSTextField* title = [NSTextField labelWithString:@"命令行引擎与外部 LibRaw 动态库配置"];
    title.font = [NSFont boldSystemFontOfSize:13.5];
    title.frame = NSMakeRect(20, 148, 480, 22);
    [box addSubview:title];

    NSTextField* lCli = [NSTextField labelWithString:@"CLI 引擎:"];
    lCli.frame = NSMakeRect(20, 108, 72, 20);
    [box addSubview:lCli];

    NSTextField* eCli = [[NSTextField alloc] initWithFrame:NSMakeRect(94, 106, 292, 24)];
    eCli.placeholderString = @"自动搜索 nijinLBS";
    eCli.stringValue = self.customCliPath ?: ([self autoDetectCliPath:YES] ?: @"");
    [box addSubview:eCli];

    NSButton* bCliBrowse = [NSButton buttonWithTitle:@"浏览..." target:nil action:nil];
    bCliBrowse.frame = NSMakeRect(392, 104, 62, 28);
    [box addSubview:bCliBrowse];

    NSButton* bCliAuto = [NSButton buttonWithTitle:@"自动" target:nil action:nil];
    bCliAuto.frame = NSMakeRect(454, 104, 50, 28);
    [box addSubview:bCliAuto];

    NSTextField* lLr = [NSTextField labelWithString:@"LibRaw:"];
    lLr.frame = NSMakeRect(20, 68, 72, 20);
    [box addSubview:lLr];

    NSTextField* eLr = [[NSTextField alloc] initWithFrame:NSMakeRect(94, 66, 292, 24)];
    eLr.placeholderString = @"留空使用内置 LibRaw 0.23-HE*";
    eLr.stringValue = self.customLibRawPath ?: @"";
    [box addSubview:eLr];

    NSButton* bLrBrowse = [NSButton buttonWithTitle:@"浏览..." target:nil action:nil];
    bLrBrowse.frame = NSMakeRect(392, 64, 62, 28);
    [box addSubview:bLrBrowse];

    NSButton* bLrReset = [NSButton buttonWithTitle:@"内置" target:nil action:nil];
    bLrReset.frame = NSMakeRect(454, 64, 50, 28);
    [box addSubview:bLrReset];

    NSButton* bDone = [NSButton buttonWithTitle:@"完成" target:nil action:nil];
    bDone.keyEquivalent = @"\r";
    bDone.frame = NSMakeRect(418, 18, 86, 30);
    [box addSubview:bDone];

    NSButton* bCancel = [NSButton buttonWithTitle:@"取消" target:nil action:nil];
    bCancel.frame = NSMakeRect(326, 18, 84, 30);
    [box addSubview:bCancel];

    __weak AppDelegate* weakSelf = self;
    __weak NSWindow* weakSheet = sheet;

    eCli.identifier = @"SheetCliField";
    eLr.identifier = @"SheetLrField";
    bCliBrowse.target = self; bCliBrowse.action = @selector(sheetBrowseCli:);
    bCliAuto.target   = self; bCliAuto.action   = @selector(sheetAutoCli:);
    bLrBrowse.target  = self; bLrBrowse.action  = @selector(sheetBrowseLr:);
    bLrReset.target   = self; bLrReset.action   = @selector(sheetResetLr:);
    bDone.target      = self; bDone.action      = @selector(sheetConfirm:);
    bCancel.target    = self; bCancel.action    = @selector(sheetDismiss:);

    [self.window beginSheet:sheet completionHandler:^(NSModalResponse returnCode) {
        AppDelegate* s = weakSelf;
        NSWindow* sh = weakSheet;
        if (!s || !sh) return;
        if (returnCode == NSModalResponseOK) {
            NSString* cVal = [eCli.stringValue stringByTrimmingCharactersInSet:[NSCharacterSet whitespaceAndNewlineCharacterSet]];
            NSString* lVal = [eLr.stringValue stringByTrimmingCharactersInSet:[NSCharacterSet whitespaceAndNewlineCharacterSet]];
            s.customCliPath = cVal;
            s.customLibRawPath = lVal;
            if (cVal.length > 0) [nijinPrefs() setObject:cVal forKey:kPrefCliPath];
            else [nijinPrefs() removeObjectForKey:kPrefCliPath];
            if (lVal.length > 0) [nijinPrefs() setObject:lVal forKey:kPrefLibRawPath];
            else [nijinPrefs() removeObjectForKey:kPrefLibRawPath];
            [s updateEngineStatusStrip];
            [s saveUiPrefs];
            if (s.rawInputPaths.count > 0) [s triggerBackgroundPlanJson];
        }
    }];
}

- (NSTextField*)findSheetField:(NSView*)root ident:(NSString*)ident {
    for (NSView* v in root.subviews) {
        if ([v isKindOfClass:[NSTextField class]] && [v.identifier isEqualToString:ident]) return (NSTextField*)v;
    }
    return nil;
}

- (void)sheetBrowseCli:(NSButton*)sender {
    NSOpenPanel* op = [NSOpenPanel openPanel];
    op.title = @"选择 nijinLBS 可执行文件";
    op.canChooseFiles = YES;
    op.canChooseDirectories = NO;
    if ([op runModal] == NSModalResponseOK && op.URL.path) {
        NSTextField* tf = [self findSheetField:sender.window.contentView ident:@"SheetCliField"];
        if (tf) tf.stringValue = op.URL.path;
    }
}

- (void)sheetAutoCli:(NSButton*)sender {
    NSString* found = [self autoDetectCliPath:NO];
    if (found) {
        NSTextField* tf = [self findSheetField:sender.window.contentView ident:@"SheetCliField"];
        if (tf) tf.stringValue = found;
    }
}

- (void)sheetBrowseLr:(NSButton*)sender {
    NSOpenPanel* op = [NSOpenPanel openPanel];
    op.title = @"选择外部 LibRaw 动态库 (.dylib / .so)";
    op.canChooseFiles = YES;
    op.canChooseDirectories = NO;
    if ([op runModal] == NSModalResponseOK && op.URL.path) {
        NSTextField* tf = [self findSheetField:sender.window.contentView ident:@"SheetLrField"];
        if (tf) tf.stringValue = op.URL.path;
    }
}

- (void)sheetResetLr:(NSButton*)sender {
    NSTextField* tf = [self findSheetField:sender.window.contentView ident:@"SheetLrField"];
    if (tf) tf.stringValue = @"";
}

- (void)sheetConfirm:(NSButton*)sender {
    [self.window endSheet:sender.window returnCode:NSModalResponseOK];
}

- (void)sheetDismiss:(NSButton*)sender {
    [self.window endSheet:sender.window returnCode:NSModalResponseCancel];
}

// ============================================================================
// State Sync, Causal Affordances & Presets
// ============================================================================

- (BurstGroupModel*)selectedBurstGroup {
    NSInteger r = self.tblQueue.selectedRow;
    if (r >= 0 && (NSUInteger)r < self.burstGroups.count) {
        return self.burstGroups[(NSUInteger)r];
    }
    return nil;
}

- (NSUInteger)selectedFieldCount {
    NSUInteger n = 0;
    for (NSButton* b in self.chkFields) {
        if (b.state == NSControlStateValueOn) ++n;
    }
    return n;
}

- (NSString*)selectedFieldsCsv {
    NSMutableArray<NSString*>* arr = [NSMutableArray array];
    for (NSUInteger i = 0; i < self.chkFields.count && i < 11; ++i) {
        if (self.chkFields[i].state == NSControlStateValueOn) {
            [arr addObject:[NSString stringWithUTF8String:kFieldSpecs[i].code]];
        }
    }
    return [arr componentsJoinedByString:@","];
}

- (NSString*)selectedLcpFieldsCsv {
    NSMutableArray<NSString*>* fields = [NSMutableArray array];
    if (self.chkLcpLca.state == NSControlStateValueOn) [fields addObject:@"lca"];
    if (self.chkLcpVignette.state == NSControlStateValueOn) [fields addObject:@"vignette"];
    if (self.chkLcpDistortion.state == NSControlStateValueOn) [fields addObject:@"distortion"];
    return [fields componentsJoinedByString:@","];
}

- (NSArray<NSString*>*)lcpArguments:(NSString*)path {
    return @[@"--optics-only", @"--export-lcp", path, @"--lcp-fields", [self selectedLcpFieldsCsv]];
}

- (NSArray<NSString*>*)refocusArguments {
    double position = std::clamp((self.edtRefocusPos.doubleValue + 1.0) * 0.5, 0.0, 1.0);
    double radius = 12.0 * self.edtBokehStr.doubleValue;
    return @[@"--export-refocus", @"--refocus-pos", [NSString stringWithFormat:@"%.6f", position],
             @"--bokeh-strength", [NSString stringWithFormat:@"%.6f", radius]];
}

- (void)applySelectedFieldsCsv:(NSString*)csv {
    NSSet<NSString*>* set = [NSSet setWithArray:[csv componentsSeparatedByString:@","]];
    for (NSUInteger i = 0; i < self.chkFields.count && i < 11; ++i) {
        NSString* code = [NSString stringWithUTF8String:kFieldSpecs[i].code];
        self.chkFields[i].state = [set containsObject:code] ? NSControlStateValueOn : NSControlStateValueOff;
    }
}

- (BOOL)isPresetModifiedFromDefaults {
    NSInteger pIdx = self.segPreset.selectedSegment;
    if (pIdx < 0 || pIdx > 3) pIdx = 0;
    const PresetDef& d = kPresets[pIdx];
    auto diff = [](double a, double b) { return std::fabs(a - b) > 0.006; };
    if (diff(self.edtDenoise.doubleValue, d.denoise)) return YES;
    if (diff(self.edtChroma.doubleValue, d.chroma)) return YES;
    if (self.edtPsfIters.integerValue != d.psfIters) return YES;
    if (diff(self.edtPsfSigma.doubleValue, d.psfSigma)) return YES;
    if (diff(self.edtDeghost.doubleValue, d.deghost)) return YES;
    if (diff(self.edtEv.doubleValue, d.evOffset)) return YES;
    if ((self.chkSiliconClean.state == NSControlStateValueOn) != d.siliconClean) return YES;
    if ((self.chkIbp.state == NSControlStateValueOn) != d.ibpEnable) return YES;
    if ((self.chkAutoLca.state == NSControlStateValueOn) != d.autoLca) return YES;
    if ((self.chkAutoExp.state == NSControlStateValueOn) != d.autoExp) return YES;
    if ((self.chkRolloff.state == NSControlStateValueOn) != d.rolloff) return YES;
    if ((self.chkDither.state == NSControlStateValueOn) != d.dither) return YES;
    return NO;
}

- (void)applyPresetIndex:(NSInteger)idx {
    if (idx < 0 || idx > 3) idx = 0;
    self.segPreset.selectedSegment = idx;
    const PresetDef& d = kPresets[idx];

    self.sldDenoise.doubleValue  = d.denoise;  self.edtDenoise.stringValue  = [NSString stringWithFormat:@"%.2f", d.denoise];
    self.sldChroma.doubleValue   = d.chroma;   self.edtChroma.stringValue   = [NSString stringWithFormat:@"%.2f", d.chroma];
    self.sldPsfIters.integerValue = d.psfIters; self.edtPsfIters.stringValue = [NSString stringWithFormat:@"%d", d.psfIters];
    self.sldPsfSigma.doubleValue = d.psfSigma; self.edtPsfSigma.stringValue = [NSString stringWithFormat:@"%.2f", d.psfSigma];
    self.sldDeghost.doubleValue  = d.deghost;  self.edtDeghost.stringValue  = [NSString stringWithFormat:@"%.2f", d.deghost];
    self.sldEv.doubleValue       = d.evOffset; self.edtEv.stringValue       = [NSString stringWithFormat:@"%.1f", d.evOffset];

    self.chkSiliconClean.state = d.siliconClean ? NSControlStateValueOn : NSControlStateValueOff;
    self.chkIbp.state          = d.ibpEnable    ? NSControlStateValueOn : NSControlStateValueOff;
    self.chkAutoLca.state      = d.autoLca      ? NSControlStateValueOn : NSControlStateValueOff;
    self.chkAutoExp.state      = d.autoExp      ? NSControlStateValueOn : NSControlStateValueOff;
    self.chkRolloff.state      = d.rolloff      ? NSControlStateValueOn : NSControlStateValueOff;
    self.chkDither.state       = d.dither       ? NSControlStateValueOn : NSControlStateValueOff;

    [self syncAllUiState];
    [self.tblQueue reloadData];
}

- (void)selectTab:(NSInteger)tabIdx {
    if (tabIdx < 0) tabIdx = 0;
    if (tabIdx > 3) tabIdx = 3;
    self.tabBar.selectedTab = tabIdx;
    [self.tabBar setNeedsDisplay:YES];
    self.page0.hidden = (tabIdx != 0);
    self.page1.hidden = (tabIdx != 1);
    self.page2.hidden = (tabIdx != 2);
    self.page3.hidden = (tabIdx != 3);
    [self saveUiPrefs];
}

- (void)syncAllUiState {
    BurstGroupModel* grp = [self selectedBurstGroup];

    // 1. Sync Motion Mode between Tab 1, Tab 2 & Left Frame Anchor
    NSString* mm = self.motionMode ?: @"auto";
    BOOL hasCustomFrameAnchor = (grp && grp.customAnchor1Based > 0);

    if ([mm isEqualToString:@"clean-plate"]) {
        self.segMotionTab1.selectedSegment = 3;
        self.radT2Freeze.state = NSControlStateValueOff;
        self.radT2Clean.state  = NSControlStateValueOn;
        self.radT2Vnd.state    = NSControlStateValueOff;
        self.radT2Strobe.state = NSControlStateValueOff;
    } else if ([mm isEqualToString:@"virtual-nd"]) {
        self.segMotionTab1.selectedSegment = 4;
        self.radT2Freeze.state = NSControlStateValueOff;
        self.radT2Clean.state  = NSControlStateValueOff;
        self.radT2Vnd.state    = NSControlStateValueOn;
        self.radT2Strobe.state = NSControlStateValueOff;
    } else if ([mm isEqualToString:@"strobe"]) {
        self.segMotionTab1.selectedSegment = -1;
        self.radT2Freeze.state = NSControlStateValueOff;
        self.radT2Clean.state  = NSControlStateValueOff;
        self.radT2Vnd.state    = NSControlStateValueOff;
        self.radT2Strobe.state = NSControlStateValueOn;
    } else {
        self.radT2Freeze.state = NSControlStateValueOn;
        self.radT2Clean.state  = NSControlStateValueOff;
        self.radT2Vnd.state    = NSControlStateValueOff;
        self.radT2Strobe.state = NSControlStateValueOff;
        if (hasCustomFrameAnchor) {
            self.segMotionTab1.selectedSegment = 0;
            self.segT2FreezePose.selectedSegment = 3;
            [self.segT2FreezePose setLabel:[NSString stringWithFormat:@"指定帧 #%d", grp.customAnchor1Based] forSegment:3];
        } else {
            [self.segT2FreezePose setLabel:@"跟随左栏指定帧" forSegment:3];
            if ([mm isEqualToString:@"first"]) {
                self.segMotionTab1.selectedSegment = 1;
                self.segT2FreezePose.selectedSegment = 1;
            } else if ([mm isEqualToString:@"middle"]) {
                self.segMotionTab1.selectedSegment = 2;
                self.segT2FreezePose.selectedSegment = 2;
            } else {
                self.segMotionTab1.selectedSegment = 0;
                self.segT2FreezePose.selectedSegment = 0;
            }
        }
    }

    self.segT2FreezePose.enabled = (self.radT2Freeze.state == NSControlStateValueOn);
    self.sldCleanSens.enabled    = (self.radT2Clean.state == NSControlStateValueOn);
    self.edtCleanSens.enabled    = (self.radT2Clean.state == NSControlStateValueOn);
    self.sldVndThresh.enabled    = (self.radT2Vnd.state == NSControlStateValueOn);
    self.edtVndThresh.enabled    = (self.radT2Vnd.state == NSControlStateValueOn);
    self.popStrobeStep.enabled   = (self.radT2Strobe.state == NSControlStateValueOn);
    self.segStrobeFade.enabled   = (self.radT2Strobe.state == NSControlStateValueOn);

    // 2. Sync Transient Layer & Portals
    BOOL emitTrans = (self.chkEmitTransientTab1.state == NSControlStateValueOn);
    self.chkEmitTransientTab2.state = emitTrans ? NSControlStateValueOn : NSControlStateValueOff;
    self.sldTransSigma.enabled = emitTrans;
    self.edtTransSigma.enabled = emitTrans;
    self.segTransColor.enabled = emitTrans;

    // 3. Sync Field Carrier between Tab 1 & Tab 3 + Auto-TIFF Affordance
    BOOL emitFld = (self.chkEmitField.state == NSControlStateValueOn);
    NSInteger fmIdx = self.popFieldMode.indexOfSelectedItem;
    if (fmIdx < 0) fmIdx = 0;
    if (emitFld && fmIdx == 0) {
        // tiff-alpha requires 2x RGB TIFF active
        BOOL isTiff = (self.popRgbFmt.indexOfSelectedItem == 0 || self.popRgbFmt.indexOfSelectedItem == 4);
        if (self.chkEmitRgb.state != NSControlStateValueOn || !isTiff) {
            self.chkEmitRgb.state = NSControlStateValueOn;
            [self.popRgbFmt selectItemAtIndex:0];
        }
    }
    self.popFieldMode.enabled = emitFld;
    self.segCarrierTab3.selectedSegment = emitFld ? (fmIdx + 1) : 0;

    // 4. Causal Affordances on Tab 1 Output & Sliders
    BOOL emitRaw = (self.chkEmitRaw.state == NSControlStateValueOn);
    BOOL relayOn = (self.chkRelayRaw.state == NSControlStateValueOn);
    if (relayOn && !emitRaw) {
        emitRaw = YES;
        self.chkEmitRaw.state = NSControlStateValueOn;
    }
    self.popRawFmt.enabled = emitRaw;

    BOOL emitRgb = (self.chkEmitRgb.state == NSControlStateValueOn);
    self.popRgbFmt.enabled = emitRgb;
    self.segColorSpace.enabled = emitRgb;

    BOOL isJxsLossy = (emitRgb && self.popRgbFmt.indexOfSelectedItem == 2);
    self.edtJxsBpp.enabled = isJxsLossy;
    self.lblBppCap.alphaValue = isJxsLossy ? 1.0 : 0.45;
    self.edtJxsBpp.alphaValue = isJxsLossy ? 1.0 : 0.45;

    // Relay-RAW causal dimming when 2x RGB is unchecked
    BOOL bypassStage5 = (relayOn && !emitRgb);
    CGFloat s5Alpha = bypassStage5 ? 0.42 : 1.0;
    self.sldDenoise.enabled  = !bypassStage5; self.edtDenoise.enabled  = !bypassStage5; self.lblDenoise.alphaValue  = s5Alpha;
    self.sldChroma.enabled   = !bypassStage5; self.edtChroma.enabled   = !bypassStage5; self.lblChroma.alphaValue   = s5Alpha;
    self.sldPsfIters.enabled = !bypassStage5; self.edtPsfIters.enabled = !bypassStage5; self.lblPsfIters.alphaValue = s5Alpha;

    self.btnResetPreset.enabled = [self isPresetModifiedFromDefaults];

    // 5. Tab 2 Drift & Parallax Physical Gates
    BOOL reflOn = (self.chkReflection.state == NSControlStateValueOn);
    self.segReflectionMode.enabled = reflOn;
    if (grp && grp.hasTelemetry) {
        double drift2x = grp.maxDrift2xPx;
        BOOL allowSplit = (drift2x >= 40.0);
        [self.segReflectionMode setEnabled:allowSplit forSegment:1];
        if (!allowSplit && self.segReflectionMode.selectedSegment == 1) {
            self.segReflectionMode.selectedSegment = 0;
        }
        self.lblDriftReadout.stringValue = allowSplit
            ? [NSString stringWithFormat:@"└─ 实测 2× 位移跨度: %.1f px (已达 >= 40 px 宽基线双层分离条件)", drift2x]
            : [NSString stringWithFormat:@"└─ 实测 2× 位移跨度: %.1f px (碎高光压制可用；整窗双层消反光需 >= 40 px)", drift2x];
    } else {
        [self.segReflectionMode setEnabled:YES forSegment:1];
        self.lblDriftReadout.stringValue = @"└─ 实测位移跨度: 待导入连拍组 (整窗双层消反光需横向平移 >= 40 px)";
    }

    self.parallaxGateView.hasGroup = (grp && (grp.hasTelemetry || grp.hasMeasuredParallax));
    self.parallaxGateView.measured = grp.hasMeasuredParallax;
    self.parallaxGateView.depthKnown = grp.depthKnown;
    self.parallaxGateView.depthObservable = grp.depthObservable;
    self.parallaxGateView.parallax2xPx = grp ? grp.maxParallax2xPx : 0.0;
    [self.parallaxGateView setNeedsDisplay:YES];

    BOOL parallaxAllowed = !grp || !grp.depthKnown || grp.depthObservable;
    self.chkExportParallax.enabled = parallaxAllowed;
    self.chkExportRefocus.enabled  = parallaxAllowed;
    BOOL pxOn = parallaxAllowed && (self.chkExportParallax.state == NSControlStateValueOn);
    BOOL rfOn = parallaxAllowed && (self.chkExportRefocus.state == NSControlStateValueOn);
    self.sldParallaxAmp.enabled = pxOn; self.edtParallaxAmp.enabled = pxOn;
    self.sldRefocusPos.enabled  = rfOn; self.edtRefocusPos.enabled  = rfOn;
    self.sldBokehStr.enabled    = rfOn; self.edtBokehStr.enabled    = rfOn;

    // 6. Tab 3 Dataset Tiles & Portal Count
    BOOL tilesOn = (self.chkExportTiles.state == NSControlStateValueOn);
    self.segTileSize.enabled = tilesOn;
    self.sldTileConf.enabled = tilesOn;
    self.edtTileConf.enabled = tilesOn;
    NSUInteger known = 0, available = 0;
    for (NSUInteger i = 0; i < self.chkFields.count; ++i) {
        NSString* code = [NSString stringWithUTF8String:kFieldSpecs[i].code];
        NSNumber* observable = grp.fieldObservability[code];
        self.chkFields[i].enabled = observable == nil || observable.boolValue;
        if (observable) { ++known; if (observable.boolValue) ++available; }
    }
    self.lblFieldsGateNote.stringValue = known
        ? [NSString stringWithFormat:@"实测条件场: %lu/%lu 可用 · 灰项保留输出选择",
            (unsigned long)available, (unsigned long)known]
        : @"条件场可先选择 · 执行时回显实测结果";

    NSUInteger selFields = [self selectedFieldCount];
    self.btnPortalFields.title = [NSString stringWithFormat:@"选场(%lu/%lu)·数据集 ▸",
                                  (unsigned long)selFields, (unsigned long)self.chkFields.count];
    self.btnPortalFields.activeHighlight = (emitFld || tilesOn || selFields != 3);
    [self.btnPortalFields setNeedsDisplay:YES];

    // 7. Tab 1 Portals & Tab 4 Telemetry Sync
    if (grp && grp.hasTelemetry) {
        self.btnPortalQuad.title = [NSString stringWithFormat:@"四象限 %d/4 · 体检 ▸", grp.quadCoverage];
    } else {
        self.btnPortalQuad.title = @"四象限 --/4 · 体检 ▸";
    }
    [self.btnPortalQuad setNeedsDisplay:YES];

    BOOL strobeOrLf = ([mm isEqualToString:@"strobe"] || pxOn || rfOn);
    self.btnPortalStrobe.title = [mm isEqualToString:@"strobe"] ? @"• 频闪已启 ▸" : @"频闪/光场 ▸";
    self.btnPortalStrobe.activeHighlight = strobeOrLf;
    [self.btnPortalStrobe setNeedsDisplay:YES];

    BOOL reflOrCustomTrans = (reflOn || (emitTrans && std::fabs(self.edtTransSigma.doubleValue - 3.5) > 0.05));
    self.btnPortalRefl.title = reflOrCustomTrans ? @"• 消反光/阈值 ▸" : @"消反光/阈值 ▸";
    self.btnPortalRefl.activeHighlight = reflOrCustomTrans;
    [self.btnPortalRefl setNeedsDisplay:YES];

    // 8. Tab Bar Cross-Tab Awareness Dots (`• `)
    BOOL tab2Active = ([mm isEqualToString:@"clean-plate"] ||
                       [mm isEqualToString:@"virtual-nd"] ||
                       [mm isEqualToString:@"strobe"] ||
                       emitTrans || reflOn || pxOn || rfOn);
    NSString* defCsv = @"motion,phase,focus";
    BOOL tab3Active = (emitFld || tilesOn || ![[self selectedFieldsCsv] isEqualToString:defCsv]);
    self.tabBar.dotTab2 = tab2Active;
    self.tabBar.dotTab3 = tab3Active;
    [self.tabBar setNeedsDisplay:YES];

    // 9. Tab 4 Views & LCP Checkboxes
    self.trajCanvas.group = grp;
    [self.trajCanvas setNeedsDisplay:YES];
    self.radarCanvas.telemetry = self.opticsModel;
    [self.radarCanvas setNeedsDisplay:YES];

    self.chkLcpVignette.enabled = YES;
    self.chkLcpDistortion.enabled = YES;
    self.chkLcpVignette.title = @"真实暗角";
    self.chkLcpDistortion.title = @"径向畸变";
    self.btnExportLcp.enabled = [self selectedLcpFieldsCsv].length > 0;

    NSString* tStr = std::isfinite(self.opticsModel.sensorTempC)
        ? [NSString stringWithFormat:@"%.1f °C", self.opticsModel.sensorTempC] : @"--";
    NSString* wStr = self.opticsModel.warmPixels >= 0
        ? [NSString stringWithFormat:@"%d", self.opticsModel.warmPixels] : @"--";
    NSString* sigma = self.opticsModel.sigma0 >= 0
        ? [NSString stringWithFormat:@"%.5f", self.opticsModel.sigma0] : @"--";
    NSString* gain = self.opticsModel.gainG >= 0
        ? [NSString stringWithFormat:@"%.5f", self.opticsModel.gainG] : @"--";
    self.lblSiliconReadout.stringValue = [NSString stringWithFormat:
        @"读出噪声 σ0: %@   │   光子增益 g: %@   │   传感器温度: %@   │   检出温点: %@",
        sigma, gain, tStr, wStr];
    self.lblOisStatus.stringValue = self.opticsModel.oisSummary ?: @"OIS 元数据: 待导入";

    // 10. Bottom Morphing Run Button Summary
    [self updateRunButtonTitle];
}

- (void)updateRunButtonTitle {
    if (self.currentTask) {
        self.btnRun.stopMode = YES;
        self.btnRun.title = @"停止";
        [self.btnRun setNeedsDisplay:YES];
        return;
    }
    self.btnRun.stopMode = NO;

    NSUInteger checkedCnt = 0;
    for (BurstGroupModel* g in self.burstGroups) {
        if (g.checked) ++checkedCnt;
    }

    NSMutableArray<NSString*>* tags = [NSMutableArray array];
    if ([self.motionMode isEqualToString:@"clean-plate"]) [tags addObject:@"空镜"];
    else if ([self.motionMode isEqualToString:@"virtual-nd"]) [tags addObject:@"慢门"];
    else if ([self.motionMode isEqualToString:@"strobe"]) [tags addObject:@"频闪"];

    if (self.chkEmitTransientTab1.state == NSControlStateValueOn) [tags addObject:@"瞬态层"];
    if (self.chkReflection.state == NSControlStateValueOn) [tags addObject:@"消反光"];
    if (self.chkEmitField.state == NSControlStateValueOn) {
        [tags addObject:[NSString stringWithFormat:@"%lu场", (unsigned long)[self selectedFieldCount]]];
    }
    if (self.chkExportTiles.state == NSControlStateValueOn) [tags addObject:@"切片"];

    NSString* tagSummary = (tags.count > 0) ? [tags componentsJoinedByString:@"+"] : nil;
    if (checkedCnt > 0 && tagSummary) {
        self.btnRun.title = [NSString stringWithFormat:@"开始执行 (%lu 组 · %@)", (unsigned long)checkedCnt, tagSummary];
    } else if (checkedCnt > 0) {
        self.btnRun.title = [NSString stringWithFormat:@"开始执行 (%lu 组)", (unsigned long)checkedCnt];
    } else if (tagSummary) {
        self.btnRun.title = [NSString stringWithFormat:@"开始执行 (%@)", tagSummary];
    } else {
        self.btnRun.title = @"开始执行";
    }
    [self.btnRun setNeedsDisplay:YES];
}

// ============================================================================
// Persistence (NSUserDefaults: com.nijin.liteburstsuite + legacy fallback)
// ============================================================================

- (void)saveUiPrefs {
    if (self.isScreenshotMode || self.suppressPrefSave) return;
    NSUserDefaults* ud = nijinPrefs();
    [ud setBool:YES forKey:@"HasSavedUiPrefs"];
    [ud setBool:(self.chkLcpLca.state == NSControlStateValueOn) forKey:@"LcpLca"];
    [ud setBool:(self.chkLcpVignette.state == NSControlStateValueOn) forKey:@"LcpVignette"];
    [ud setBool:(self.chkLcpDistortion.state == NSControlStateValueOn) forKey:@"LcpDistortion"];
    [ud setInteger:self.tabBar.selectedTab forKey:@"ActiveTab"];

    [ud setBool:(self.chkSortTime.state == NSControlStateValueOn) forKey:@"SortTime"];
    [ud setBool:(self.chkRecursive.state == NSControlStateValueOn) forKey:@"Recursive"];

    NSInteger pIdx = self.segPreset.selectedSegment;
    if (pIdx >= 0 && pIdx < 4) [ud setObject:[NSString stringWithUTF8String:kPresets[pIdx].code] forKey:@"PresetCode"];
    [ud setObject:(self.motionMode ?: @"auto") forKey:@"MotionMode"];

    NSInteger olpfIdx = self.popOlpf.indexOfSelectedItem;
    NSArray<NSString*>* olpfs = olpfModeCodes();
    if (olpfIdx >= 0 && (NSUInteger)olpfIdx < olpfs.count) [ud setObject:olpfs[(NSUInteger)olpfIdx] forKey:@"OlpfMode"];

    [ud setBool:(self.chkEmitRaw.state == NSControlStateValueOn) forKey:@"EmitRaw"];
    NSInteger rIdx = self.popRawFmt.indexOfSelectedItem;
    NSArray<NSString*>* rCodes = rawFormatCodes();
    if (rIdx >= 0 && (NSUInteger)rIdx < rCodes.count) [ud setObject:rCodes[(NSUInteger)rIdx] forKey:@"RawFmtCode"];
    [ud setBool:(self.chkRelayRaw.state == NSControlStateValueOn) forKey:@"RelayRaw"];

    [ud setBool:(self.chkEmitRgb.state == NSControlStateValueOn) forKey:@"EmitRgb"];
    NSInteger cIdx = self.popRgbFmt.indexOfSelectedItem;
    NSArray<NSString*>* cCodes = rgbFormatCodes();
    if (cIdx >= 0 && (NSUInteger)cIdx < cCodes.count) [ud setObject:cCodes[(NSUInteger)cIdx] forKey:@"RgbFmtCode"];
    if (self.edtJxsBpp.stringValue.length > 0) [ud setObject:self.edtJxsBpp.stringValue forKey:@"JxsBpp"];
    [ud setInteger:self.segColorSpace.selectedSegment forKey:@"ColorSpaceIdx"];

    [ud setBool:(self.chkEmitField.state == NSControlStateValueOn) forKey:@"EmitField"];
    NSInteger fmIdx = self.popFieldMode.indexOfSelectedItem;
    NSArray<NSString*>* fmCodes = fieldModeCodes();
    if (fmIdx >= 0 && (NSUInteger)fmIdx < fmCodes.count) [ud setObject:fmCodes[(NSUInteger)fmIdx] forKey:@"FieldModeCode"];
    [ud setObject:[self selectedFieldsCsv] forKey:@"SelectedFieldsCsv"];

    [ud setBool:(self.chkEmitTransientTab1.state == NSControlStateValueOn) forKey:@"EmitTransient"];
    [ud setObject:self.edtTransSigma.stringValue forKey:@"TransientSigma"];
    [ud setInteger:self.segTransColor.selectedSegment forKey:@"TransientColorIdx"];

    [ud setObject:self.edtCleanSens.stringValue forKey:@"CleanSens"];
    [ud setObject:self.edtVndThresh.stringValue forKey:@"VndThresh"];
    [ud setInteger:self.popStrobeStep.indexOfSelectedItem forKey:@"StrobeStepIdx"];
    [ud setInteger:self.segStrobeFade.selectedSegment forKey:@"StrobeFadeIdx"];

    [ud setBool:(self.chkReflection.state == NSControlStateValueOn) forKey:@"ReflectionOn"];
    [ud setInteger:self.segReflectionMode.selectedSegment forKey:@"ReflectionModeIdx"];

    [ud setBool:(self.chkExportParallax.state == NSControlStateValueOn) forKey:@"ExportParallax"];
    [ud setObject:self.edtParallaxAmp.stringValue forKey:@"ParallaxAmp"];
    [ud setBool:(self.chkExportRefocus.state == NSControlStateValueOn) forKey:@"ExportRefocus"];
    [ud setObject:self.edtRefocusPos.stringValue forKey:@"RefocusPos"];
    [ud setObject:self.edtBokehStr.stringValue forKey:@"BokehStrength"];

    [ud setBool:(self.chkExportTiles.state == NSControlStateValueOn) forKey:@"ExportTiles"];
    [ud setInteger:self.segTileSize.selectedSegment forKey:@"TileSizeIdx"];
    [ud setObject:self.edtTileConf.stringValue forKey:@"TileMinConf"];

    [ud setObject:self.edtDenoise.stringValue  forKey:@"Denoise"];
    [ud setObject:self.edtChroma.stringValue   forKey:@"Chroma"];
    [ud setObject:self.edtPsfIters.stringValue forKey:@"PsfIters"];
    [ud setObject:self.edtPsfSigma.stringValue forKey:@"PsfSigma"];
    [ud setObject:self.edtDeghost.stringValue  forKey:@"Deghost"];
    [ud setObject:self.edtEv.stringValue       forKey:@"EvOffset"];

    [ud setBool:(self.chkSiliconClean.state == NSControlStateValueOn) forKey:@"SiliconClean"];
    [ud setBool:(self.chkIbp.state == NSControlStateValueOn)          forKey:@"IbpEnable"];
    [ud setBool:(self.chkAutoLca.state == NSControlStateValueOn)      forKey:@"AutoLca"];
    [ud setBool:(self.chkAutoExp.state == NSControlStateValueOn)      forKey:@"AutoExp"];
    [ud setBool:(self.chkRolloff.state == NSControlStateValueOn)      forKey:@"Rolloff"];
    [ud setBool:(self.chkDither.state == NSControlStateValueOn)       forKey:@"Dither"];

    [ud setInteger:self.popBackend.indexOfSelectedItem forKey:@"BackendIdx"];
    [ud setObject:self.edtGrpSz.stringValue   forKey:@"GroupSize"];
    [ud setObject:self.edtThreads.stringValue forKey:@"Threads"];

    if (self.window) {
        [ud setObject:[self.window stringWithSavedFrame] forKey:@"WindowFrame"];
    }
    [ud synchronize];
}

- (void)loadUiPrefs {
    NSUserDefaults* ud = nijinPrefs();
    BOOL isLegacy = NO;
    if (![ud boolForKey:@"HasSavedUiPrefs"]) {
        NSUserDefaults* leg = nijinLegacyPrefs();
        if (leg && [leg boolForKey:@"HasSavedUiPrefs"]) {
            ud = leg;
            isLegacy = YES;
        } else {
            return;
        }
    }

    self.suppressPrefSave = YES;
    if ([ud objectForKey:@"LcpLca"]) self.chkLcpLca.state = [ud boolForKey:@"LcpLca"] ? NSControlStateValueOn : NSControlStateValueOff;
    if ([ud objectForKey:@"LcpVignette"]) self.chkLcpVignette.state = [ud boolForKey:@"LcpVignette"] ? NSControlStateValueOn : NSControlStateValueOff;
    if ([ud objectForKey:@"LcpDistortion"]) self.chkLcpDistortion.state = [ud boolForKey:@"LcpDistortion"] ? NSControlStateValueOn : NSControlStateValueOff;

    if ([ud objectForKey:@"SortTime"])  self.chkSortTime.state  = [ud boolForKey:@"SortTime"]  ? NSControlStateValueOn : NSControlStateValueOff;
    if ([ud objectForKey:@"Recursive"]) self.chkRecursive.state = [ud boolForKey:@"Recursive"] ? NSControlStateValueOn : NSControlStateValueOff;

    // Preset (supports new PresetCode and legacy PresetIdx: 0=photo, 1=sci, 2=arc, 3=astro)
    NSString* pCode = [ud stringForKey:@"PresetCode"];
    if (pCode) {
        for (int i = 0; i < 4; ++i) {
            if ([pCode isEqualToString:[NSString stringWithUTF8String:kPresets[i].code]]) {
                [self applyPresetIndex:i];
                break;
            }
        }
    } else if (isLegacy && [ud objectForKey:@"PresetIdx"]) {
        NSInteger legIdx = [ud integerForKey:@"PresetIdx"];
        NSInteger mapped = (legIdx == 3) ? 1 : (legIdx == 1) ? 2 : (legIdx == 2) ? 3 : 0;
        [self applyPresetIndex:mapped];
    }

    NSString* mMode = [ud stringForKey:@"MotionMode"];
    if (mMode.length > 0) {
        self.motionMode = mMode;
    } else if (isLegacy && [ud objectForKey:@"AnchorIdx"]) {
        NSInteger anc = [ud integerForKey:@"AnchorIdx"];
        self.motionMode = (anc == 1) ? @"first" : (anc == 2) ? @"middle" : @"auto";
    }

    NSString* olpf = [ud stringForKey:@"OlpfMode"];
    NSUInteger olpfIdx = olpf ? [olpfModeCodes() indexOfObject:olpf] : NSNotFound;
    if (olpfIdx != NSNotFound) [self.popOlpf selectItemAtIndex:(NSInteger)olpfIdx];

    if ([ud objectForKey:@"EmitRaw"]) self.chkEmitRaw.state = [ud boolForKey:@"EmitRaw"] ? NSControlStateValueOn : NSControlStateValueOff;
    NSString* rCode = [ud stringForKey:@"RawFmtCode"];
    NSUInteger rIdx = rCode ? [rawFormatCodes() indexOfObject:rCode] : NSNotFound;
    if (rIdx != NSNotFound) [self.popRawFmt selectItemAtIndex:(NSInteger)rIdx];
    if ([ud objectForKey:@"RelayRaw"]) self.chkRelayRaw.state = [ud boolForKey:@"RelayRaw"] ? NSControlStateValueOn : NSControlStateValueOff;

    if ([ud objectForKey:@"EmitRgb"]) self.chkEmitRgb.state = [ud boolForKey:@"EmitRgb"] ? NSControlStateValueOn : NSControlStateValueOff;
    NSString* cCode = [ud stringForKey:@"RgbFmtCode"];
    NSUInteger cIdx = cCode ? [rgbFormatCodes() indexOfObject:cCode] : NSNotFound;
    if (cIdx != NSNotFound) [self.popRgbFmt selectItemAtIndex:(NSInteger)cIdx];

    NSString* bpp = [ud stringForKey:@"JxsBpp"];
    if (bpp.length > 0) self.edtJxsBpp.stringValue = bpp;

    if ([ud objectForKey:@"ColorSpaceIdx"]) {
        NSInteger cs = [ud integerForKey:@"ColorSpaceIdx"];
        self.segColorSpace.selectedSegment = (cs == 1) ? 1 : 0;
    }

    if ([ud objectForKey:@"EmitField"]) self.chkEmitField.state = [ud boolForKey:@"EmitField"] ? NSControlStateValueOn : NSControlStateValueOff;
    NSString* fmCode = [ud stringForKey:@"FieldModeCode"];
    NSUInteger fmIdx = fmCode ? [fieldModeCodes() indexOfObject:fmCode] : NSNotFound;
    if (fmIdx != NSNotFound) [self.popFieldMode selectItemAtIndex:(NSInteger)fmIdx];

    NSString* fCsv = [ud stringForKey:@"SelectedFieldsCsv"];
    if (fCsv) [self applySelectedFieldsCsv:fCsv];

    if ([ud objectForKey:@"EmitTransient"]) self.chkEmitTransientTab1.state = [ud boolForKey:@"EmitTransient"] ? NSControlStateValueOn : NSControlStateValueOff;
    NSString* tSig = [ud stringForKey:@"TransientSigma"];
    if (tSig.length > 0) { self.edtTransSigma.stringValue = tSig; self.sldTransSigma.doubleValue = tSig.doubleValue; }
    if ([ud objectForKey:@"TransientColorIdx"]) self.segTransColor.selectedSegment = ([ud integerForKey:@"TransientColorIdx"] == 1) ? 1 : 0;

    NSString* cSens = [ud stringForKey:@"CleanSens"];
    if (cSens.length > 0) { self.edtCleanSens.stringValue = cSens; self.sldCleanSens.doubleValue = cSens.doubleValue; }
    NSString* vndT = [ud stringForKey:@"VndThresh"];
    if (vndT.length > 0) { self.edtVndThresh.stringValue = vndT; self.sldVndThresh.doubleValue = vndT.doubleValue; }
    if ([ud objectForKey:@"StrobeStepIdx"]) {
        NSInteger st = [ud integerForKey:@"StrobeStepIdx"];
        if (st >= 0 && st < self.popStrobeStep.numberOfItems) [self.popStrobeStep selectItemAtIndex:st];
    }
    if ([ud objectForKey:@"StrobeFadeIdx"]) self.segStrobeFade.selectedSegment = ([ud integerForKey:@"StrobeFadeIdx"] == 1) ? 1 : 0;

    if ([ud objectForKey:@"ReflectionOn"]) self.chkReflection.state = [ud boolForKey:@"ReflectionOn"] ? NSControlStateValueOn : NSControlStateValueOff;
    if ([ud objectForKey:@"ReflectionModeIdx"]) self.segReflectionMode.selectedSegment = ([ud integerForKey:@"ReflectionModeIdx"] == 1) ? 1 : 0;

    if ([ud objectForKey:@"ExportParallax"]) self.chkExportParallax.state = [ud boolForKey:@"ExportParallax"] ? NSControlStateValueOn : NSControlStateValueOff;
    NSString* pAmp = [ud stringForKey:@"ParallaxAmp"];
    if (pAmp.length > 0) { self.edtParallaxAmp.stringValue = pAmp; self.sldParallaxAmp.doubleValue = pAmp.doubleValue; }

    if ([ud objectForKey:@"ExportRefocus"]) self.chkExportRefocus.state = [ud boolForKey:@"ExportRefocus"] ? NSControlStateValueOn : NSControlStateValueOff;
    NSString* rPos = [ud stringForKey:@"RefocusPos"];
    if (rPos.length > 0) { self.edtRefocusPos.stringValue = rPos; self.sldRefocusPos.doubleValue = rPos.doubleValue; }
    NSString* bStr = [ud stringForKey:@"BokehStrength"];
    if (bStr.length > 0) { self.edtBokehStr.stringValue = bStr; self.sldBokehStr.doubleValue = bStr.doubleValue; }

    if ([ud objectForKey:@"ExportTiles"]) self.chkExportTiles.state = [ud boolForKey:@"ExportTiles"] ? NSControlStateValueOn : NSControlStateValueOff;
    if ([ud objectForKey:@"TileSizeIdx"]) self.segTileSize.selectedSegment = ([ud integerForKey:@"TileSizeIdx"] == 1) ? 1 : 0;
    NSString* tConf = [ud stringForKey:@"TileMinConf"];
    if (tConf.length > 0) { self.edtTileConf.stringValue = tConf; self.sldTileConf.doubleValue = tConf.doubleValue; }

    auto loadKnob = ^(NSString* key, NSTextField* tf, NSSlider* sld) {
        NSString* s = [ud stringForKey:key];
        if (s.length > 0) {
            tf.stringValue = s;
            if (sld) sld.doubleValue = s.doubleValue;
        }
    };
    loadKnob(@"Denoise",  self.edtDenoise,  self.sldDenoise);
    loadKnob(@"Chroma",   self.edtChroma,   self.sldChroma);
    loadKnob(@"PsfIters", self.edtPsfIters, self.sldPsfIters);
    loadKnob(@"PsfSigma", self.edtPsfSigma, self.sldPsfSigma);
    loadKnob(@"Deghost",  self.edtDeghost,  self.sldDeghost);
    loadKnob(@"EvOffset", self.edtEv,       self.sldEv);

    if ([ud objectForKey:@"SiliconClean"]) self.chkSiliconClean.state = [ud boolForKey:@"SiliconClean"] ? NSControlStateValueOn : NSControlStateValueOff;
    if ([ud objectForKey:@"IbpEnable"])    self.chkIbp.state          = [ud boolForKey:@"IbpEnable"]    ? NSControlStateValueOn : NSControlStateValueOff;
    if ([ud objectForKey:@"AutoLca"])      self.chkAutoLca.state      = [ud boolForKey:@"AutoLca"]      ? NSControlStateValueOn : NSControlStateValueOff;
    if ([ud objectForKey:@"AutoExp"])      self.chkAutoExp.state      = [ud boolForKey:@"AutoExp"]      ? NSControlStateValueOn : NSControlStateValueOff;
    if ([ud objectForKey:@"Rolloff"])      self.chkRolloff.state      = [ud boolForKey:@"Rolloff"]      ? NSControlStateValueOn : NSControlStateValueOff;
    if ([ud objectForKey:@"Dither"])       self.chkDither.state       = [ud boolForKey:@"Dither"]       ? NSControlStateValueOn : NSControlStateValueOff;

    if ([ud objectForKey:@"BackendIdx"]) {
        NSInteger bk = [ud integerForKey:@"BackendIdx"];
        if (bk >= 0 && bk < self.popBackend.numberOfItems) [self.popBackend selectItemAtIndex:bk];
    }
    loadKnob(@"GroupSize", self.edtGrpSz, nil);
    loadKnob(@"Threads",   self.edtThreads, nil);

    NSInteger savedTab = (!isLegacy && [ud objectForKey:@"ActiveTab"]) ? [ud integerForKey:@"ActiveTab"] : 0;
    [self selectTab:savedTab];

    NSString* wf = (!isLegacy) ? [ud stringForKey:@"WindowFrame"] : nil;
    if (wf.length > 0 && self.window && !self.isScreenshotMode) {
        [self.window setFrameFromString:wf];
        NSSize cs = [self.window contentRectForFrameRect:self.window.frame].size;
        if (cs.width < 1140 || cs.height < 740) {
            [self.window setContentSize:NSMakeSize(std::max(1140.0, (double)cs.width), std::max(740.0, (double)cs.height))];
        }
    }

    self.suppressPrefSave = NO;
    [self syncAllUiState];
}

// ============================================================================
// Input Scanning & Async Background `--plan-json` Queue Population
// ============================================================================

- (BOOL)isRawExtension:(NSString*)path {
    static NSSet<NSString*>* kExts = nil;
    if (!kExts) {
        kExts = [NSSet setWithArray:@[
            @"nef", @"nrw", @"arw", @"srf", @"sr2", @"cr2", @"cr3", @"crw",
            @"raf", @"dng", @"orf", @"ori", @"rw2", @"raw", @"pef", @"srw",
            @"iiq", @"3fr", @"fff", @"erf", @"kdc", @"dcr", @"mos", @"x3f"
        ]];
    }
    NSString* ext = [[path pathExtension] lowercaseString];
    return [kExts containsObject:ext];
}

- (void)rebuildLocalBasenameIndex {
    [self.fileByBasename removeAllObjects];
    NSFileManager* fm = [NSFileManager defaultManager];
    BOOL rec = (self.chkRecursive.state == NSControlStateValueOn);
    for (NSString* p in self.rawInputPaths) {
        BOOL isDir = NO;
        if (![fm fileExistsAtPath:p isDirectory:&isDir]) continue;
        if (!isDir) {
            if ([self isRawExtension:p]) self.fileByBasename[p] = p;
        } else {
            NSDirectoryEnumerationOptions opts = NSDirectoryEnumerationSkipsHiddenFiles;
            if (!rec) opts |= NSDirectoryEnumerationSkipsSubdirectoryDescendants;
            NSDirectoryEnumerator* en = [fm enumeratorAtURL:[NSURL fileURLWithPath:p]
                                 includingPropertiesForKeys:@[NSURLIsRegularFileKey]
                                                    options:opts
                                               errorHandler:nil];
            for (NSURL* u in en) {
                NSString* fp = u.path;
                if (fp && [self isRawExtension:fp]) {
                    self.fileByBasename[fp] = fp;
                }
            }
        }
    }
}

- (void)addInputPaths:(NSArray<NSString*>*)paths {
    if (!paths || paths.count == 0) return;
    for (NSString* p in paths) {
        if (p.length > 0 && ![self.rawInputPaths containsObject:p]) {
            [self.rawInputPaths addObject:p];
        }
    }
    if (self.rawInputPaths.count > 0 && self.edtOutDir.stringValue.length == 0) {
        NSString* first = self.rawInputPaths.firstObject;
        BOOL isD = NO;
        [[NSFileManager defaultManager] fileExistsAtPath:first isDirectory:&isD];
        NSString* base = isD ? first : [first stringByDeletingLastPathComponent];
        self.edtOutDir.stringValue = [base stringByAppendingPathComponent:@"nijinLBS_out"];
    }
    [self triggerBackgroundPlanJson];
}

- (void)updateQueueEmptyVisibility {
    BOOL empty = (self.burstGroups.count == 0);
    self.emptyWell.hidden = !empty;
    self.svQueue.hidden = empty;
    self.lblInspectorEmpty.hidden = ([self selectedBurstGroup] != nil);
    self.svFrames.hidden = ([self selectedBurstGroup] == nil);
}

- (void)triggerBackgroundPlanJson {
    NSInteger mySerial = ++self.planSerial;
    self.opticsModel = [[OpticsTelemetryModel alloc] init];
    if (self.planTask && self.planTask.isRunning) {
        [self.planTask terminate];
    }
    self.planTask = nil;
    self.emptyWell.scanning = NO;
    if (self.rawInputPaths.count == 0) {
        [self.burstGroups removeAllObjects];
        [self.tblQueue reloadData];
        [self.tblFrames reloadData];
        [self updateQueueEmptyVisibility];
        [self syncAllUiState];
        return;
    }

    [self rebuildLocalBasenameIndex];
    NSString* cli = [self autoDetectCliPath:YES];
    if (!cli) {
        // Fallback group from discovered RAW files even before CLI is configured
        [self.burstGroups removeAllObjects];
        NSArray<NSString*>* allFiles = [[self.fileByBasename allValues] sortedArrayUsingSelector:@selector(localizedStandardCompare:)];
        if (allFiles.count > 0) {
            BurstGroupModel* g = [[BurstGroupModel alloc] init];
            g.displayIndex = 1;
            g.validFrameCount = (int)allFiles.count;
            g.checked = (allFiles.count >= 4);
            [g.filePaths addObjectsFromArray:allFiles];
            RawQuickMeta qm = peekRawMeta(allFiles.firstObject);
            g.cameraModel = qm.model.empty() ? @"RAW 连拍序列" : [NSString stringWithUTF8String:qm.model.c_str()];
            g.cfaType     = [NSString stringWithUTF8String:qm.cfaType.c_str()];
            g.shutterType = [NSString stringWithUTF8String:qm.shutterType.c_str()];
            g.focalMm     = qm.focalMm;
            g.fNumber     = qm.fNumber;
            g.iso         = qm.iso;
            for (NSUInteger i = 0; i < allFiles.count; ++i) {
                FrameDiagItem* fi = [[FrameDiagItem alloc] init];
                fi.idx1Based = (int)(i + 1);
                fi.assignedGroup = 0;
                fi.fullPath = allFiles[i];
                fi.fileName = [allFiles[i] lastPathComponent];
                fi.flags = @"OK";
                fi.afScore = 0.0;
                fi.acutance = 0.0;
                fi.enabled = YES;
                fi.isCrown = NO;
                [g.frames addObject:fi];
            }
            [self.burstGroups addObject:g];
            [self.tblQueue reloadData];
            [self.tblQueue selectRowIndexes:[NSIndexSet indexSetWithIndex:0] byExtendingSelection:NO];
        }
        [self updateQueueEmptyVisibility];
        [self syncAllUiState];
        return;
    }

    self.emptyWell.scanning = YES;
    [self.emptyWell setNeedsDisplay:YES];
    self.lblStageStatus.stringValue = @"正在后台静默剖析 RAW 连拍簇与对焦点锐度...";

    NSMutableArray<NSString*>* args = [NSMutableArray arrayWithObject:@"--plan-json"];
    [args addObjectsFromArray:@[@"--policy", @"multi-group", @"--group-size", self.edtGrpSz.stringValue]];
    if (self.customLibRawPath.length > 0) {
        [args addObjectsFromArray:@[@"--libraw", self.customLibRawPath]];
    }
    [args addObject:(self.chkSortTime.state == NSControlStateValueOn) ? @"--sort-time" : @"--no-sort-time"];
    if (self.chkRecursive.state == NSControlStateValueOn) [args addObject:@"--recursive"];
    [args addObjectsFromArray:self.rawInputPaths];

    NSTask* task = [[NSTask alloc] init];
    task.executableURL = [NSURL fileURLWithPath:cli];
    task.arguments = args;
    NSPipe* outPipe = [NSPipe pipe];
    task.standardOutput = outPipe;
    task.standardError = outPipe;
    self.planTask = task;

    NSDictionary<NSString*, NSString*>* baseSnapshot = [self.fileByBasename copy];
    __weak AppDelegate* weakSelf = self;

    NSError* launchErr = nil;
    if (![task launchAndReturnError:&launchErr]) {
        self.planTask = nil;
        self.emptyWell.scanning = NO;
        self.lblStageStatus.stringValue = launchErr.localizedDescription;
        return;
    }
    dispatch_async(dispatch_get_global_queue(QOS_CLASS_USER_INITIATED, 0), ^{
        NSData* data = [[outPipe fileHandleForReading] readDataToEndOfFile];
        [task waitUntilExit];
        std::string jsonStr((const char*)data.bytes, data.length);
        NSArray<BurstGroupModel*>* parsed = [weakSelf parsePlanJsonOutput:jsonStr basenameMap:baseSnapshot];

        dispatch_async(dispatch_get_main_queue(), ^{
            AppDelegate* s = weakSelf;
            if (!s || s.planSerial != mySerial) return;
            s.planTask = nil;
            s.emptyWell.scanning = NO;
            [s.emptyWell setNeedsDisplay:YES];
            [s.burstGroups setArray:parsed];
            [s.tblQueue reloadData];
            if (s.burstGroups.count > 0) {
                [s.tblQueue selectRowIndexes:[NSIndexSet indexSetWithIndex:0] byExtendingSelection:NO];
            }
            [s.tblFrames reloadData];
            [s updateQueueEmptyVisibility];
            s.lblStageStatus.stringValue = task.terminationStatus == 0
                ? [NSString stringWithFormat:@"就绪 · 已剖析 %lu 个连拍组", (unsigned long)s.burstGroups.count]
                : [NSString stringWithFormat:@"连拍剖析失败 (退出码 %d)", task.terminationStatus];
            [s syncAllUiState];
        });
    });
}

- (NSArray<BurstGroupModel*>*)parsePlanJsonOutput:(const std::string&)jsonStr
                                      basenameMap:(NSDictionary<NSString*, NSString*>*)baseMap {
    NSMutableArray<BurstGroupModel*>* outGroups = [NSMutableArray array];
    std::vector<std::string> clusterObjs = extractJsonArrayObjects(jsonStr, "clusters");
    int dispIdx = 1;

    for (const std::string& cl : clusterObjs) {
        std::string scene     = extractJsonStr(cl, "scene");
        std::string recPre    = extractJsonStr(cl, "rec_preset");
        std::string recAnc    = extractJsonStr(cl, "rec_anchor");
        std::string modelJson = extractJsonStr(cl, "model");
        std::string cfaJson   = extractJsonStr(cl, "cfa_type");
        std::string shutJson  = extractJsonStr(cl, "shutter_type");
        double focalJson      = extractJsonDouble(cl, "focal_len", extractJsonDouble(cl, "focal_mm", 0.0));
        double fnumJson       = extractJsonDouble(cl, "f_number", 0.0);
        int isoJson           = extractJsonInt(cl, "iso", 0);
        int bestAnc           = extractJsonInt(cl, "best_anchor", 1);
        int quadCov           = extractJsonInt(cl, "quad_coverage", 1);
        double recDen         = extractJsonDouble(cl, "rec_denoise", 1.0);
        double recDegh        = extractJsonDouble(cl, "rec_deghost", 1.0);
        int recPsf            = extractJsonInt(cl, "rec_psf_iters", 4);
        bool shock1           = extractJsonBool(cl, "shutter_shock", false);
        double drift2xJson    = extractJsonDouble(cl, "max_drift_2x_px", -1.0);
        double para2xJson     = extractJsonDouble(cl, "max_parallax_2x_px", -1.0);

        std::vector<std::string> frameObjs = extractJsonArrayObjects(cl, "frames");
        if (frameObjs.empty()) continue;

        NSMutableArray<FrameDiagItem*>* allFrames = [NSMutableArray arrayWithCapacity:frameObjs.size()];
        int maxGrpId = -1;
        double maxAcu = 1e-6;
        double maxShift1x = 0.0;
        bool hasCoordinates = true;

        for (size_t fi = 0; fi < frameObjs.size(); ++fi) {
            const std::string& fo = frameObjs[fi];
            FrameDiagItem* item = [[FrameDiagItem alloc] init];
            item.idx1Based     = extractJsonInt(fo, "idx", (int)(fi + 1));
            item.assignedGroup = extractJsonInt(fo, "group", 0);
            std::string fFile  = extractJsonStr(fo, "file");
            std::string fPath  = extractJsonStr(fo, "path");
            std::string fFlags = extractJsonStr(fo, "flags");
            item.fileName = [NSString stringWithUTF8String:fFile.c_str()];
            if (!fPath.empty()) {
                item.fullPath = [NSString stringWithUTF8String:fPath.c_str()];
            } else {
                NSArray<NSString*>* matches = [[baseMap allValues] filteredArrayUsingPredicate:
                    [NSPredicate predicateWithBlock:^BOOL(NSString* path, NSDictionary* bindings) {
                        (void)bindings;
                        return [[path lastPathComponent] isEqualToString:item.fileName];
                    }]];
                item.fullPath = matches.count == 1 ? matches.firstObject : nil;
            }
            item.flags    = fFlags.empty() ? @"OK" : [NSString stringWithUTF8String:fFlags.c_str()];
            item.acutance = extractJsonDouble(fo, "acutance", 0.0);
            item.afScore  = extractJsonDouble(fo, "af_score", -1.0);
            item.shiftPx  = extractJsonDouble(fo, "shift", 0.0);
            double hasDx  = extractJsonDouble(fo, "dx_1x", extractJsonDouble(fo, "dx", -99999.0));
            double hasDy  = extractJsonDouble(fo, "dy_1x", extractJsonDouble(fo, "dy", -99999.0));
            if (hasDx > -90000.0 && hasDy > -90000.0) {
                item.dxPx = hasDx;
                item.dyPx = hasDy;
                item.hasPosition = YES;
            } else hasCoordinates = false;
            item.isCrown = extractJsonBool(fo, "crown", false);
            item.isShock = extractJsonBool(fo, "shock", false);

            bool badFlag = (fFlags.find("DARK") != std::string::npos ||
                            fFlags.find("MOTION_BLUR") != std::string::npos ||
                            fFlags.find("EXPOSURE") != std::string::npos ||
                            fFlags.find("DIM_MISMATCH") != std::string::npos);
            item.enabled = !badFlag && item.fullPath.length > 0;

            maxGrpId = std::max(maxGrpId, item.assignedGroup);
            maxAcu = std::max(maxAcu, item.acutance);
            maxShift1x = std::max(maxShift1x, item.shiftPx);
            [allFrames addObject:item];
        }

        // Normalize af_score to 0..99 when CLI only emits raw acutance
        for (FrameDiagItem* item in allFrames) {
            if (item.afScore < 0.0) {
                item.afScore = (maxAcu > 1e-6) ? std::min(99.0, std::round((item.acutance / maxAcu) * 99.0)) : 0.0;
            }
        }

        int numSubGroups = (maxGrpId >= 1) ? (maxGrpId + 1) : 1;
        for (int sg = 0; sg < numSubGroups; ++sg) {
            BurstGroupModel* gm = [[BurstGroupModel alloc] init];
            gm.displayIndex = dispIdx++;
            gm.sceneCode    = scene.empty() ? @"handheld_standard" : [NSString stringWithUTF8String:scene.c_str()];
            gm.recPreset    = recPre.empty() ? @"photo" : [NSString stringWithUTF8String:recPre.c_str()];
            gm.recAnchor    = recAnc.empty() ? @"auto"  : [NSString stringWithUTF8String:recAnc.c_str()];
            gm.recDenoise   = recDen;
            gm.recDeghost   = recDegh;
            gm.recPsfIters  = recPsf;
            gm.bestAnchor   = bestAnc;
            gm.quadCoverage = std::max(1, std::min(4, quadCov));
            gm.shutterShock = shock1;
            gm.hasTelemetry = hasCoordinates;
            gm.oisEnabled = extractJsonBool(cl, "has_ois", false);

            int subIdx = 1;
            int validCnt = 0;
            double subMaxShift1x = 0.0;
            for (FrameDiagItem* item in allFrames) {
                if (numSubGroups > 1 && item.assignedGroup != sg) continue;
                item.idx1Based = subIdx++;
                [gm.frames addObject:item];
                if (item.fullPath.length > 0) [gm.filePaths addObject:item.fullPath];
                bool isDark = ([item.flags rangeOfString:@"DARK"].location != NSNotFound);
                if (item.enabled && !isDark) ++validCnt;
                subMaxShift1x = std::max(subMaxShift1x, item.shiftPx);
            }
            if (gm.frames.count == 0) continue;
            FrameDiagItem* sharpest = gm.frames.firstObject;
            for (FrameDiagItem* frame in gm.frames) if (frame.afScore > sharpest.afScore) sharpest = frame;
            gm.bestAnchor = sharpest.idx1Based;

            gm.validFrameCount = validCnt;
            gm.checked = (validCnt >= 4 && gm.filePaths.count == gm.frames.count);
            gm.maxDrift2xPx = (drift2xJson >= 0.0) ? drift2xJson : (subMaxShift1x * 2.0);
            gm.maxParallax2xPx = (para2xJson >= 0.0) ? para2xJson : 0.0;

            RawQuickMeta qm = peekRawMeta(gm.filePaths.firstObject);
            gm.cameraModel = !modelJson.empty() ? [NSString stringWithUTF8String:modelJson.c_str()]
                           : (!qm.model.empty() ? [NSString stringWithUTF8String:qm.model.c_str()] : @"RAW 连拍序列");
            gm.cfaType     = !cfaJson.empty()   ? [NSString stringWithUTF8String:cfaJson.c_str()]
                                                : [NSString stringWithUTF8String:qm.cfaType.c_str()];
            gm.shutterType = !shutJson.empty()  ? [NSString stringWithUTF8String:shutJson.c_str()]
                                                : [NSString stringWithUTF8String:qm.shutterType.c_str()];
            gm.focalMm     = (focalJson > 0.0)  ? focalJson : qm.focalMm;
            gm.fNumber     = (fnumJson > 0.0)   ? fnumJson  : qm.fNumber;
            gm.iso         = (isoJson > 0)      ? isoJson   : qm.iso;

            [outGroups addObject:gm];
        }
    }
    return outGroups;
}

// ============================================================================
// NSTableView DataSource & Delegate (Upper Queue + Lower Frame Inspector)
// ============================================================================

- (NSInteger)numberOfRowsInTableView:(NSTableView*)tableView {
    if (tableView == self.tblQueue) return (NSInteger)self.burstGroups.count;
    BurstGroupModel* g = [self selectedBurstGroup];
    return g ? (NSInteger)g.frames.count : 0;
}

- (NSView*)tableView:(NSTableView*)tableView viewForTableColumn:(NSTableColumn*)tableColumn row:(NSInteger)row {
    (void)tableColumn;
    __weak AppDelegate* weakSelf = self;

    if (tableView == self.tblQueue) {
        if (row < 0 || (NSUInteger)row >= self.burstGroups.count) return nil;
        BurstGroupModel* g = self.burstGroups[(NSUInteger)row];
        BurstGroupCellView* cell = [tableView makeViewWithIdentifier:@"BurstGroupCell" owner:self];
        if (!cell) {
            cell = [[BurstGroupCellView alloc] initWithFrame:NSMakeRect(0, 0, tableView.bounds.size.width, 54)];
            cell.identifier = @"BurstGroupCell";
        }
        cell.chkEnable.state = g.checked ? NSControlStateValueOn : NSControlStateValueOff;

        NSString* firstStem = [[g.frames.firstObject.fileName stringByDeletingPathExtension] ?: @"" copy];
        NSString* lastStem  = [[g.frames.lastObject.fileName stringByDeletingPathExtension] ?: @"" copy];
        NSString* rangeStr = (g.frames.count <= 1 || [firstStem isEqualToString:lastStem])
            ? firstStem
            : [NSString stringWithFormat:@"%@–%@", firstStem, lastStem];
        NSString* countSuffix = (g.validFrameCount < 4)
            ? [NSString stringWithFormat:@"%lu张 (少帧)", (unsigned long)g.frames.count]
            : [NSString stringWithFormat:@"%lu张", (unsigned long)g.frames.count];
        cell.lblTitle.stringValue = [NSString stringWithFormat:@"组 %02d · %@  %@", g.displayIndex, countSuffix, rangeStr];

        if ([g.cfaType isEqualToString:@"xtrans"]) {
            cell.lblBadge.stringValue = @"[X-Trans]";
        } else if ([g.shutterType isEqualToString:@"global"]) {
            cell.lblBadge.stringValue = @"[全域]";
        } else {
            cell.lblBadge.stringValue = @"[Bayer]";
        }

        NSMutableArray<NSString*>* metaParts = [NSMutableArray array];
        if (g.cameraModel.length > 0) [metaParts addObject:g.cameraModel];
        if (g.focalMm > 0.1 && g.fNumber > 0.1) {
            [metaParts addObject:[NSString stringWithFormat:@"%.0fmm f/%.1f", g.focalMm, g.fNumber]];
        } else if (g.focalMm > 0.1) {
            [metaParts addObject:[NSString stringWithFormat:@"%.0fmm", g.focalMm]];
        }
        if (g.iso > 0) [metaParts addObject:[NSString stringWithFormat:@"ISO %d", g.iso]];
        cell.lblMeta.stringValue = [metaParts componentsJoinedByString:@" · "];

        // Contextual Scene Affordance Pill
        NSInteger curPresetIdx = self.segPreset.selectedSegment;
        NSString* curPresetCode = (curPresetIdx >= 0 && curPresetIdx < 4)
            ? [NSString stringWithUTF8String:kPresets[curPresetIdx].code] : @"photo";
        BOOL showScenePill = (g.recPreset.length > 0 &&
                              ![g.recPreset isEqualToString:@"photo"] &&
                              ![g.recPreset isEqualToString:curPresetCode]);
        if (showScenePill) {
            NSString* pillTitle = [g.recPreset isEqualToString:@"astro"] ? @"✦ 套用星空配置"
                                : ([g.recPreset isEqualToString:@"sci"]  ? @"✦ 套用科学配置" : @"✦ 套用归档配置");
            cell.btnScenePill.title = pillTitle;
            cell.btnScenePill.hidden = NO;
        } else {
            cell.btnScenePill.hidden = YES;
        }
        [cell resizeSubviewsWithOldSize:cell.bounds.size];

        cell.onToggleCheck = ^(BOOL on) {
            AppDelegate* s = weakSelf;
            if (!s) return;
            g.checked = on;
            [s updateRunButtonTitle];
        };
        cell.onApplyScene = ^{
            AppDelegate* s = weakSelf;
            if (!s) return;
            for (int i = 0; i < 4; ++i) {
                if ([g.recPreset isEqualToString:[NSString stringWithUTF8String:kPresets[i].code]]) {
                    [s applyPresetIndex:i];
                    s.sldDenoise.doubleValue = g.recDenoise;
                    s.edtDenoise.stringValue = [NSString stringWithFormat:@"%.2f", g.recDenoise];
                    s.sldDeghost.doubleValue = g.recDeghost;
                    s.edtDeghost.stringValue = [NSString stringWithFormat:@"%.2f", g.recDeghost];
                    s.sldPsfIters.integerValue = g.recPsfIters;
                    s.edtPsfIters.stringValue = [NSString stringWithFormat:@"%d", g.recPsfIters];
                    if ([g.recAnchor isEqualToString:@"middle"]) s.motionMode = @"middle";
                    [s syncAllUiState];
                    [s saveUiPrefs];
                    break;
                }
            }
        };
        return cell;
    }

    // Lower Card: Frame Inspector
    BurstGroupModel* g = [self selectedBurstGroup];
    if (!g || row < 0 || (NSUInteger)row >= g.frames.count) return nil;
    FrameDiagItem* f = g.frames[(NSUInteger)row];

    FrameInspectorCellView* fc = [tableView makeViewWithIdentifier:@"FrameCell" owner:self];
    if (!fc) {
        fc = [[FrameInspectorCellView alloc] initWithFrame:NSMakeRect(0, 0, tableView.bounds.size.width, 28)];
        fc.identifier = @"FrameCell";
    }

    double maxScore = 1.0;
    for (FrameDiagItem* it in g.frames) maxScore = std::max(maxScore, it.afScore);

    fc.chkFrame.state = f.enabled ? NSControlStateValueOn : NSControlStateValueOff;
    BOOL isLockedAnchor = (g.customAnchor1Based == f.idx1Based);
    fc.btnAnchorDot.lockedAnchor = isLockedAnchor;
    [fc.btnAnchorDot setNeedsDisplay:YES];

    NSString* shortName = [f.fileName stringByDeletingPathExtension] ?: f.fileName;
    fc.lblName.stringValue = [NSString stringWithFormat:@"#%d %@", f.idx1Based, shortName];
    fc.lblName.alphaValue = f.enabled ? 1.0 : 0.5;

    fc.barView.normRatio = f.afScore / maxScore;
    fc.barView.isCrown = f.isCrown;
    fc.barView.dimmed = !f.enabled;
    [fc.barView setNeedsDisplay:YES];

    fc.lblScore.stringValue = [NSString stringWithFormat:@"%.0f%@", f.afScore, f.isCrown ? @"♛" : @""];
    fc.lblShift.stringValue = [NSString stringWithFormat:@"%.1fpx", f.shiftPx];
    fc.toolTip = f.hasTime ? [NSString stringWithFormat:@"%@ · t=%.6fs", f.fullPath, f.timeSeconds] : f.fullPath;

    if ([f.flags rangeOfString:@"DARK"].location != NSNotFound) {
        fc.lblTag.stringValue = @"黑场";
        fc.lblTag.textColor = [NSColor colorWithSRGBRed:0.52 green:0.30 blue:0.28 alpha:1.0];
    } else if ([f.flags rangeOfString:@"MOTION_BLUR"].location != NSNotFound) {
        fc.lblTag.stringValue = @"动糊";
        fc.lblTag.textColor = [NSColor colorWithSRGBRed:0.70 green:0.38 blue:0.16 alpha:1.0];
    } else if (f.isShock) {
        fc.lblTag.stringValue = @"微震";
        fc.lblTag.textColor = [NSColor colorWithSRGBRed:0.68 green:0.42 blue:0.14 alpha:1.0];
    } else if (isLockedAnchor) {
        fc.lblTag.stringValue = @"定格";
        fc.lblTag.textColor = [NSColor colorWithSRGBRed:0.18 green:0.44 blue:0.53 alpha:1.0];
    } else {
        fc.lblTag.stringValue = @"";
    }

    fc.onToggleFrame = ^(BOOL on) {
        AppDelegate* s = weakSelf;
        if (!s) return;
        f.enabled = on;
        if (!on && g.customAnchor1Based == f.idx1Based) g.customAnchor1Based = 0;
        int vCnt = 0;
        for (FrameDiagItem* it in g.frames) {
            if (it.enabled && [it.flags rangeOfString:@"DARK"].location == NSNotFound) ++vCnt;
        }
        g.validFrameCount = vCnt;
        [s.tblFrames reloadData];
        [s.tblQueue reloadDataForRowIndexes:[NSIndexSet indexSetWithIndex:(NSUInteger)[s.burstGroups indexOfObject:g]]
                              columnIndexes:[NSIndexSet indexSetWithIndex:0]];
        [s syncAllUiState];
    };
    fc.onClickAnchor = ^{
        AppDelegate* s = weakSelf;
        if (!s) return;
        if (g.customAnchor1Based == f.idx1Based) {
            g.customAnchor1Based = 0;
        } else {
            g.customAnchor1Based = f.idx1Based;
            f.enabled = YES;
            g.validFrameCount = 0;
            for (FrameDiagItem* frame in g.frames) if (frame.enabled) ++g.validFrameCount;
            if ([s.motionMode isEqualToString:@"clean-plate"] ||
                [s.motionMode isEqualToString:@"virtual-nd"] ||
                [s.motionMode isEqualToString:@"strobe"]) {
                s.motionMode = @"auto";
            }
        }
        [s.tblFrames reloadData];
        [s syncAllUiState];
    };
    return fc;
}

- (void)tableViewSelectionDidChange:(NSNotification*)notification {
    if (notification.object == self.tblQueue) {
        self.opticsModel = [[OpticsTelemetryModel alloc] init];
        BurstGroupModel* g = [self selectedBurstGroup];
        if (g) {
            self.lblInspectorHeader.stringValue = [NSString stringWithFormat:@"组 %02d 单帧检视", g.displayIndex];
        } else {
            self.lblInspectorHeader.stringValue = @"组内单帧检视";
        }
        [self.tblFrames reloadData];
        [self updateQueueEmptyVisibility];
        [self syncAllUiState];
    }
}

// ============================================================================
// UI Control Actions
// ============================================================================

- (void)onOpenMixedPanel:(id)sender {
    (void)sender;
    NSOpenPanel* op = [NSOpenPanel openPanel];
    op.title = @"选择 RAW 连拍文件或目录";
    op.canChooseFiles = YES;
    op.canChooseDirectories = YES;
    op.allowsMultipleSelection = YES;
    if ([op runModal] == NSModalResponseOK) {
        NSMutableArray<NSString*>* arr = [NSMutableArray array];
        for (NSURL* u in op.URLs) if (u.path) [arr addObject:u.path];
        [self addInputPaths:arr];
    }
}

- (void)onAddFiles:(id)sender {
    (void)sender;
    NSOpenPanel* op = [NSOpenPanel openPanel];
    op.title = @"添加 RAW 连拍文件";
    op.canChooseFiles = YES;
    op.canChooseDirectories = NO;
    op.allowsMultipleSelection = YES;
    if ([op runModal] == NSModalResponseOK) {
        NSMutableArray<NSString*>* arr = [NSMutableArray array];
        for (NSURL* u in op.URLs) if (u.path) [arr addObject:u.path];
        [self addInputPaths:arr];
    }
}

- (void)onAddFolder:(id)sender {
    (void)sender;
    NSOpenPanel* op = [NSOpenPanel openPanel];
    op.title = @"添加包含 RAW 连拍的目录";
    op.canChooseFiles = NO;
    op.canChooseDirectories = YES;
    op.allowsMultipleSelection = YES;
    if ([op runModal] == NSModalResponseOK) {
        NSMutableArray<NSString*>* arr = [NSMutableArray array];
        for (NSURL* u in op.URLs) if (u.path) [arr addObject:u.path];
        [self addInputPaths:arr];
    }
}

- (void)onRemoveSelectedGroup:(id)sender {
    (void)sender;
    NSInteger r = self.tblQueue.selectedRow;
    if (r >= 0 && (NSUInteger)r < self.burstGroups.count) {
        [self.burstGroups removeObjectAtIndex:(NSUInteger)r];
        [self.rawInputPaths removeAllObjects];
        for (BurstGroupModel* group in self.burstGroups) [self.rawInputPaths addObjectsFromArray:group.filePaths];
        for (NSUInteger i = 0; i < self.burstGroups.count; ++i) {
            self.burstGroups[i].displayIndex = (int)(i + 1);
        }
        if (self.burstGroups.count == 0) [self.rawInputPaths removeAllObjects];
        [self.tblQueue reloadData];
        if (self.burstGroups.count > 0) {
            NSUInteger nextSel = std::min((NSUInteger)r, self.burstGroups.count - 1);
            [self.tblQueue selectRowIndexes:[NSIndexSet indexSetWithIndex:nextSel] byExtendingSelection:NO];
        }
        [self.tblFrames reloadData];
        [self updateQueueEmptyVisibility];
        [self syncAllUiState];
    }
}

- (void)onClearQueue:(id)sender {
    (void)sender;
    if (self.planTask && self.planTask.isRunning) [self.planTask terminate];
    self.planTask = nil;
    self.emptyWell.scanning = NO;
    ++self.planSerial;
    [self.rawInputPaths removeAllObjects];
    [self.fileByBasename removeAllObjects];
    [self.burstGroups removeAllObjects];
    self.opticsModel = [[OpticsTelemetryModel alloc] init];
    [self.tblQueue reloadData];
    [self.tblFrames reloadData];
    [self updateQueueEmptyVisibility];
    self.lblStageStatus.stringValue = @"队列已清空";
    [self syncAllUiState];
}

- (void)onScanOptChanged:(id)sender {
    (void)sender;
    [self saveUiPrefs];
    if (self.rawInputPaths.count > 0) [self triggerBackgroundPlanJson];
}

- (void)onPresetSegChanged:(NSSegmentedControl*)sender {
    [self applyPresetIndex:sender.selectedSegment];
    [self saveUiPrefs];
}

- (void)onResetPreset:(id)sender {
    (void)sender;
    [self applyPresetIndex:self.segPreset.selectedSegment];
    [self saveUiPrefs];
}

- (void)onMotionTab1Changed:(NSSegmentedControl*)sender {
    NSInteger s = sender.selectedSegment;
    BurstGroupModel* g = [self selectedBurstGroup];
    if (g) g.customAnchor1Based = 0;
    if (s == 1) self.motionMode = @"first";
    else if (s == 2) self.motionMode = @"middle";
    else if (s == 3) self.motionMode = @"clean-plate";
    else if (s == 4) self.motionMode = @"virtual-nd";
    else self.motionMode = @"auto";
    [self.tblFrames reloadData];
    [self syncAllUiState];
    [self saveUiPrefs];
}

- (void)onTab2MotionRadio:(NSButton*)sender {
    BurstGroupModel* g = [self selectedBurstGroup];
    if (sender == self.radT2Clean) {
        self.motionMode = @"clean-plate";
    } else if (sender == self.radT2Vnd) {
        self.motionMode = @"virtual-nd";
    } else if (sender == self.radT2Strobe) {
        self.motionMode = @"strobe";
    } else {
        NSInteger p = self.segT2FreezePose.selectedSegment;
        if (p == 1) { if (g) g.customAnchor1Based = 0; self.motionMode = @"first"; }
        else if (p == 2) { if (g) g.customAnchor1Based = 0; self.motionMode = @"middle"; }
        else if (p == 3 && g && g.bestAnchor > 0) {
            if (g.customAnchor1Based <= 0) g.customAnchor1Based = g.bestAnchor;
            self.motionMode = @"auto";
        } else {
            if (g) g.customAnchor1Based = 0;
            self.motionMode = @"auto";
        }
    }
    [self.tblFrames reloadData];
    [self syncAllUiState];
    [self saveUiPrefs];
}

- (void)onTab2FreezePoseSeg:(NSSegmentedControl*)sender {
    BurstGroupModel* g = [self selectedBurstGroup];
    self.motionMode = @"auto";
    NSInteger p = sender.selectedSegment;
    if (p == 1) { if (g) g.customAnchor1Based = 0; self.motionMode = @"first"; }
    else if (p == 2) { if (g) g.customAnchor1Based = 0; self.motionMode = @"middle"; }
    else if (p == 3) {
        if (g && g.customAnchor1Based <= 0) g.customAnchor1Based = std::max(1, g.bestAnchor);
    } else {
        if (g) g.customAnchor1Based = 0;
    }
    [self.tblFrames reloadData];
    [self syncAllUiState];
    [self saveUiPrefs];
}

- (void)onTransientTab1Changed:(NSButton*)sender {
    self.chkEmitTransientTab2.state = sender.state;
    [self syncAllUiState];
    [self saveUiPrefs];
}

- (void)onTransientTab2Changed:(NSButton*)sender {
    self.chkEmitTransientTab1.state = sender.state;
    [self syncAllUiState];
    [self saveUiPrefs];
}

- (void)onCarrierTab3Changed:(NSSegmentedControl*)sender {
    NSInteger s = sender.selectedSegment;
    if (s <= 0) {
        self.chkEmitField.state = NSControlStateValueOff;
    } else {
        self.chkEmitField.state = NSControlStateValueOn;
        [self.popFieldMode selectItemAtIndex:(s - 1)];
    }
    [self syncAllUiState];
    [self saveUiPrefs];
}

- (void)onFieldsCore3:(id)sender {
    (void)sender;
    [self applySelectedFieldsCsv:@"motion,phase,focus"];
    [self syncAllUiState];
    [self saveUiPrefs];
}

- (void)onFieldsSelectAll:(id)sender {
    (void)sender;
    for (NSButton* b in self.chkFields) b.state = NSControlStateValueOn;
    [self syncAllUiState];
    [self saveUiPrefs];
}

- (void)onGenericOptionChanged:(id)sender {
    (void)sender;
    [self syncAllUiState];
    [self saveUiPrefs];
}

- (void)onSliderMoved:(NSSlider*)sender {
    if (sender == self.sldDenoise)       self.edtDenoise.stringValue     = [NSString stringWithFormat:@"%.2f", sender.doubleValue];
    else if (sender == self.sldChroma)   self.edtChroma.stringValue      = [NSString stringWithFormat:@"%.2f", sender.doubleValue];
    else if (sender == self.sldPsfIters) self.edtPsfIters.stringValue    = [NSString stringWithFormat:@"%ld", (long)std::llround(sender.doubleValue)];
    else if (sender == self.sldPsfSigma) self.edtPsfSigma.stringValue    = [NSString stringWithFormat:@"%.2f", sender.doubleValue];
    else if (sender == self.sldDeghost)  self.edtDeghost.stringValue     = [NSString stringWithFormat:@"%.2f", sender.doubleValue];
    else if (sender == self.sldEv)       self.edtEv.stringValue          = [NSString stringWithFormat:@"%.1f", sender.doubleValue];
    else if (sender == self.sldCleanSens) self.edtCleanSens.stringValue  = [NSString stringWithFormat:@"%.2f", sender.doubleValue];
    else if (sender == self.sldVndThresh) self.edtVndThresh.stringValue  = [NSString stringWithFormat:@"%.2f", sender.doubleValue];
    else if (sender == self.sldTransSigma) self.edtTransSigma.stringValue = [NSString stringWithFormat:@"%.1f", sender.doubleValue];
    else if (sender == self.sldParallaxAmp) self.edtParallaxAmp.stringValue = [NSString stringWithFormat:@"%.2f", sender.doubleValue];
    else if (sender == self.sldRefocusPos) self.edtRefocusPos.stringValue = [NSString stringWithFormat:@"%.2f", sender.doubleValue];
    else if (sender == self.sldBokehStr) self.edtBokehStr.stringValue    = [NSString stringWithFormat:@"%.2f", sender.doubleValue];
    else if (sender == self.sldTileConf) self.edtTileConf.stringValue    = [NSString stringWithFormat:@"%.2f", sender.doubleValue];
    [self syncAllUiState];
    [self saveUiPrefs];
}

- (void)controlTextDidEndEditing:(NSNotification*)obj {
    NSTextField* tf = obj.object;
    if (tf == self.edtDenoise)         self.sldDenoise.doubleValue     = tf.doubleValue;
    else if (tf == self.edtChroma)     self.sldChroma.doubleValue      = tf.doubleValue;
    else if (tf == self.edtPsfIters)   self.sldPsfIters.integerValue   = tf.integerValue;
    else if (tf == self.edtPsfSigma)   self.sldPsfSigma.doubleValue    = tf.doubleValue;
    else if (tf == self.edtDeghost)    self.sldDeghost.doubleValue     = tf.doubleValue;
    else if (tf == self.edtEv)         self.sldEv.doubleValue          = tf.doubleValue;
    else if (tf == self.edtCleanSens)  self.sldCleanSens.doubleValue   = tf.doubleValue;
    else if (tf == self.edtVndThresh)  self.sldVndThresh.doubleValue   = tf.doubleValue;
    else if (tf == self.edtTransSigma) self.sldTransSigma.doubleValue  = tf.doubleValue;
    else if (tf == self.edtParallaxAmp) self.sldParallaxAmp.doubleValue = tf.doubleValue;
    else if (tf == self.edtRefocusPos) self.sldRefocusPos.doubleValue  = tf.doubleValue;
    else if (tf == self.edtBokehStr)   self.sldBokehStr.doubleValue    = tf.doubleValue;
    else if (tf == self.edtTileConf)   self.sldTileConf.doubleValue    = tf.doubleValue;
    [self syncAllUiState];
    [self saveUiPrefs];
}

- (void)onBrowseOutDir:(id)sender {
    (void)sender;
    NSOpenPanel* op = [NSOpenPanel openPanel];
    op.canChooseFiles = NO;
    op.canChooseDirectories = YES;
    op.canCreateDirectories = YES;
    if ([op runModal] == NSModalResponseOK && op.URL.path) {
        self.edtOutDir.stringValue = op.URL.path;
    }
}

- (void)onClearOutDir:(id)sender {
    (void)sender;
    self.edtOutDir.stringValue = @"";
}

- (void)onOpenOutDir:(id)sender {
    (void)sender;
    NSString* d = [self effectiveOutputDirectory];
    if (d.length > 0) {
        [[NSFileManager defaultManager] createDirectoryAtPath:d withIntermediateDirectories:YES attributes:nil error:nil];
        [[NSWorkspace sharedWorkspace] openURL:[NSURL fileURLWithPath:d]];
    }
}

- (NSString*)effectiveOutputDirectory {
    NSString* d = [self.edtOutDir.stringValue stringByTrimmingCharactersInSet:[NSCharacterSet whitespaceAndNewlineCharacterSet]];
    if (d.length > 0) return d;
    BurstGroupModel* g = self.burstGroups.firstObject;
    NSString* first = g.filePaths.firstObject ?: self.rawInputPaths.firstObject;
    if (!first) return [NSTemporaryDirectory() stringByAppendingPathComponent:@"nijinLBS_out"];
    BOOL isD = NO;
    [[NSFileManager defaultManager] fileExistsAtPath:first isDirectory:&isD];
    NSString* base = isD ? first : [first stringByDeletingLastPathComponent];
    return [base stringByAppendingPathComponent:@"nijinLBS_out"];
}

// Portal Switches
- (void)onPortalToTab2:(id)s { (void)s; [self selectTab:1]; }
- (void)onPortalToTab3:(id)s { (void)s; [self selectTab:2]; }
- (void)onPortalToTab4:(id)s { (void)s; [self selectTab:3]; }

// ============================================================================
// Top-K Culling, Tab 4 Optics Telemetry & Main Batch Execution
// ============================================================================

- (void)onExportTopK:(id)sender {
    (void)sender;
    if (self.currentTask) return;
    BurstGroupModel* g = [self selectedBurstGroup] ?: self.burstGroups.firstObject;
    if (!g || g.filePaths.count == 0) {
        NSAlert* a = [[NSAlert alloc] init];
        a.messageText = @"请先导入并选择一个连拍组";
        [a runModal];
        return;
    }
    NSString* cli = [self resolveActiveCliOrPrompt];
    if (!cli) return;

    NSOpenPanel* op = [NSOpenPanel openPanel];
    op.title = @"选择最锐 Top-K 原片导出目录";
    op.canChooseFiles = NO;
    op.canChooseDirectories = YES;
    op.canCreateDirectories = YES;
    if ([op runModal] != NSModalResponseOK || !op.URL.path) return;

    NSMutableArray<NSString*>* args = [NSMutableArray array];
    if (self.customLibRawPath.length > 0) [args addObjectsFromArray:@[@"--libraw", self.customLibRawPath]];
    [args addObjectsFromArray:@[@"--export-topk", @"3", @"--out-dir", op.URL.path]];
    [args addObjectsFromArray:g.filePaths];

    [self runUtilityCliTask:cli args:args label:@"正在按 AF 对焦点锐度筛出 Top-3 原片..." completion:^(int rc) {
        if (rc == 0) {
            [[NSWorkspace sharedWorkspace] openURL:[NSURL fileURLWithPath:op.URL.path]];
        }
    }];
}

- (void)parseOpticsTelemetryFile:(NSString*)jsonPath cardPng:(NSString*)pngPath {
    NSData* d = [NSData dataWithContentsOfFile:jsonPath];
    if (!d || d.length == 0) return;
    id parsed = [NSJSONSerialization JSONObjectWithData:d options:0 error:nil];
    if (![parsed isKindOfClass:[NSDictionary class]]) return;
    NSDictionary* root = parsed;
    if ([root[@"schema"] isEqual:@"org.nijin.optics.v1"]) {
        auto number = [](id value, double fallback) {
            return [value isKindOfClass:[NSNumber class]] && std::isfinite([value doubleValue])
                ? [value doubleValue] : fallback;
        };
        auto metric = [&](id value, double fallback) {
            return [value isKindOfClass:[NSDictionary class]] ? number(value[@"value"], fallback) : fallback;
        };
        OpticsTelemetryModel* model = [[OpticsTelemetryModel alloc] init];
        model.sigma0 = number(root[@"sigma0"], -1);
        model.gainG = number(root[@"gain_g"], -1);
        model.sensorTempC = number(root[@"sensor_temp_c"], NAN);
        model.warmPixels = (int)number(root[@"warm_pixels"], -1);
        model.hasSilicon = model.sigma0 >= 0 && model.gainG >= 0;
        model.populated = YES;
        model.lastJsonPath = jsonPath;
        if (pngPath && [[NSFileManager defaultManager] fileExistsAtPath:pngPath]) model.lastCardPngPath = pngPath;
        NSArray* names = @[@"center", @"top_left", @"top_right", @"bottom_left", @"bottom_right"];
        double mtf[5] = {-1,-1,-1,-1,-1}, anisotropy[5] = {};
        NSMutableArray<NSString*>* statuses = [NSMutableArray arrayWithArray:@[@"待测",@"待测",@"待测",@"待测",@"待测"]];
        NSMutableArray<NSString*>* details = [NSMutableArray array];
        NSArray* zones = [root[@"zones"] isKindOfClass:[NSArray class]] ? root[@"zones"] : @[];
        BOOL stellar = NO;
        for (id item in zones) {
            if (![item isKindOfClass:[NSDictionary class]]) continue;
            NSDictionary* zone = item;
            NSUInteger index = [names indexOfObject:zone[@"name"] ?: @""];
            if (index == NSNotFound) continue;
            mtf[index] = metric(zone[@"mtf50"], -1.0);
            double major = metric(zone[@"fwhm_major_px"], -1.0);
            double minor = metric(zone[@"fwhm_minor_px"], -1.0);
            if (major > 0 && minor >= 0) anisotropy[index] = std::clamp(1.0 - minor / major, 0.0, 1.0);
            NSString* status = [zone[@"status"] isKindOfClass:[NSString class]] ? zone[@"status"] : @"待测";
            statuses[index] = status;
            NSString* method = [zone[@"method"] isKindOfClass:[NSString class]] ? zone[@"method"] : @"";
            stellar |= [method containsString:@"stellar"];
            NSDictionary* measurement = [zone[@"mtf50"] isKindOfClass:[NSDictionary class]] ? zone[@"mtf50"] : @{};
            [details addObject:[NSString stringWithFormat:@"%@: %@ · %@ · samples %@ · FWHM %@/%@ px",
                names[index], status, measurement[@"status"] ?: @"待测", measurement[@"samples"] ?: @0,
                major >= 0 ? [NSString stringWithFormat:@"%.3f", major] : @"--",
                minor >= 0 ? [NSString stringWithFormat:@"%.3f", minor] : @"--"]];
        }
        model.centerMtf = mtf[0]; model.nwMtf = mtf[1]; model.neMtf = mtf[2]; model.swMtf = mtf[3]; model.seMtf = mtf[4];
        model.centerValid = mtf[0] >= 0; model.nwValid = mtf[1] >= 0; model.neValid = mtf[2] >= 0;
        model.swValid = mtf[3] >= 0; model.seValid = mtf[4] >= 0;
        model.nwAstig = anisotropy[1]; model.neAstig = anisotropy[2]; model.swAstig = anisotropy[3]; model.seAstig = anisotropy[4];
        model.zoneStatuses = statuses;
        int valid = 0;
        for (double value : mtf) if (value >= 0) ++valid;
        model.tierBadge = [NSString stringWithFormat:@"%@ · %d/5", stellar ? @"星点 ePSF" : @"MTF 测量", valid];
        NSDictionary* lca = [root[@"lca"] isKindOfClass:[NSDictionary class]] ? root[@"lca"] : @{};
        NSArray* channels = [lca[@"channels"] isKindOfClass:[NSArray class]] ? lca[@"channels"] : @[];
        double width = number(root[@"width"], 0), height = number(root[@"height"], 0);
        double extent = std::max(width, height);
        for (id item in channels) {
            if (![item isKindOfClass:[NSDictionary class]]) continue;
            NSDictionary* channel = item;
            NSArray* coefficients = [channel[@"coefficients"] isKindOfClass:[NSArray class]] ? channel[@"coefficients"] : @[];
            if (coefficients.count != 4 || extent <= 0) continue;
            double c[4];
            bool usable = true;
            for (int i = 0; i < 4; ++i) { c[i] = number(coefficients[i], NAN); usable &= std::isfinite(c[i]); }
            if (!usable) continue;
            double maxShift = 0;
            for (int sx : {-1, 1}) for (int sy : {-1, 1}) {
                double x = sx * width * 0.5 / extent, y = sy * height * 0.5 / extent;
                double scale = c[2] + c[3] * (x*x + y*y);
                maxShift = std::max(maxShift, std::hypot(c[0] + x*scale, c[1] + y*scale));
            }
            if ([channel[@"channel"] isEqual:@"red"]) { model.lcaRedPx = maxShift; model.redLcaValid = YES; }
            if ([channel[@"channel"] isEqual:@"blue"]) { model.lcaBluePx = maxShift; model.blueLcaValid = YES; }
        }
        BurstGroupModel* group = [self selectedBurstGroup];
        double parallax = number(root[@"max_parallax_2x_px"], NAN);
        if (group && std::isfinite(parallax)) { group.maxParallax2xPx = parallax; group.hasMeasuredParallax = YES; }
        if (group && [root[@"depth_observable"] isKindOfClass:[NSNumber class]]) {
            group.depthKnown = YES; group.depthObservable = [root[@"depth_observable"] boolValue];
        }
        if (group && [root[@"field_observability"] isKindOfClass:[NSDictionary class]]) {
            NSMutableDictionary* observability = [NSMutableDictionary dictionary];
            for (NSString* key in root[@"field_observability"]) {
                id value = root[@"field_observability"][key];
                if ([value isKindOfClass:[NSNumber class]]) observability[key] = @([value boolValue]);
            }
            group.fieldObservability = observability;
            if (observability[@"depth"]) { group.depthKnown = YES; group.depthObservable = [observability[@"depth"] boolValue]; }
        }
        NSArray* drift = [root[@"drift"] isKindOfClass:[NSArray class]] ? root[@"drift"] : @[];
        NSUInteger validDrift = 0;
        for (id item in drift) {
            if (![item isKindOfClass:[NSDictionary class]]) continue;
            NSDictionary* frame = item;
            NSInteger index = (NSInteger)number(frame[@"frame"], -1);
            if (!group || index < 0 || (NSUInteger)index >= group.frames.count) continue;
            FrameDiagItem* target = group.frames[(NSUInteger)index];
            double dx = metric(frame[@"dx_px"], NAN), dy = metric(frame[@"dy_px"], NAN);
            if (std::isfinite(dx) && std::isfinite(dy)) {
                target.dxPx = dx; target.dyPx = dy; target.hasPosition = YES;
                target.shiftPx = std::hypot(dx, dy); ++validDrift;
            }
            double time = metric(frame[@"time_seconds"], NAN);
            target.hasTime = std::isfinite(time);
            target.timeSeconds = target.hasTime ? time : 0;
        }
        if (group && group.frames.count > 0 && validDrift == group.frames.count) group.hasTelemetry = YES;
        double span = metric(root[@"drift_span_px"], -1);
        if (group && span >= 0) group.maxDrift2xPx = 2 * span;
        NSArray* quadrants = [root[@"phase_quadrants"] isKindOfClass:[NSArray class]] ? root[@"phase_quadrants"] : @[];
        if (group && quadrants.count == 4) {
            NSMutableArray<NSNumber*>* counts = [NSMutableArray array];
            group.quadCoverage = 0;
            for (id count in quadrants) {
                double value = std::max(0.0, number(count, 0));
                [counts addObject:@(value)];
                if (value > 0) ++group.quadCoverage;
            }
            group.phaseQuadrants = counts;
        }
        model.oisSummary = [NSString stringWithFormat:@"OIS 元数据: %@ · 有效 MTF %d/5",
            [root[@"ois"] boolValue] ? @"开启" : @"关闭", valid];
        self.opticsModel = model;
        self.radarCanvas.toolTip = [details componentsJoinedByString:@"\n"];
        [self.tblFrames reloadData];
        [self syncAllUiState];
        return;
    }
    std::string js((const char*)d.bytes, d.length);

    OpticsTelemetryModel* m = [[OpticsTelemetryModel alloc] init];
    m.lastJsonPath = jsonPath;
    if (pngPath && [[NSFileManager defaultManager] fileExistsAtPath:pngPath]) {
        m.lastCardPngPath = pngPath;
    }

    std::string grade = extractJsonStr(js, "grade");
    if (grade.empty()) grade = extractJsonStr(js, "tier");
    m.tierBadge = grade.empty() ? @"光学测量" : [NSString stringWithUTF8String:grade.c_str()];

    auto zoneVal = [&](const char* flatKey, const char* objKey, double fb) -> double {
        double v = extractJsonDouble(js, flatKey, -1.0);
        if (v >= 0.0) return v;
        size_t p = js.find(std::string("\"") + objKey + "\":");
        if (p != std::string::npos) {
            double nv = extractJsonDouble(js, "mtf50", -1.0, p);
            if (nv >= 0.0) return nv;
        }
        return fb;
    };

    m.centerMtf = zoneVal("center_mtf50", "center", extractJsonDouble(js, "mtf50_center", -1.0));
    m.nwMtf     = zoneVal("nw_mtf50",     "nw",     extractJsonDouble(js, "mtf50_nw",     -1.0));
    m.neMtf     = zoneVal("ne_mtf50",     "ne",     extractJsonDouble(js, "mtf50_ne",     -1.0));
    m.swMtf     = zoneVal("sw_mtf50",     "sw",     extractJsonDouble(js, "mtf50_sw",     -1.0));
    m.seMtf     = zoneVal("se_mtf50",     "se",     extractJsonDouble(js, "mtf50_se",     -1.0));
    m.populated = m.centerMtf >= 0.0;
    m.centerValid = m.populated;
    if (!m.populated) m.tierBadge = @"待实测";

    m.nwAstig   = extractJsonDouble(js, "nw_astig", 0.0);
    m.neAstig   = extractJsonDouble(js, "ne_astig", 0.0);
    m.swAstig   = extractJsonDouble(js, "sw_astig", 0.0);
    m.seAstig   = extractJsonDouble(js, "se_astig", 0.0);

    m.nwValid   = m.nwMtf >= 0.0 && !extractJsonBool(js, "nw_rejected", false);
    m.neValid   = m.neMtf >= 0.0 && !extractJsonBool(js, "ne_rejected", false);
    m.swValid   = m.swMtf >= 0.0 && !extractJsonBool(js, "sw_rejected", false);
    m.seValid   = m.seMtf >= 0.0 && !extractJsonBool(js, "se_rejected", false);

    m.lcaRedPx  = extractJsonDouble(js, "lca_r_px", extractJsonDouble(js, "lca_red", 0.0));
    m.lcaBluePx = extractJsonDouble(js, "lca_b_px", extractJsonDouble(js, "lca_blue", 0.0));
    m.redLcaValid = js.find("\"lca_r_px\"") != std::string::npos || js.find("\"lca_red\"") != std::string::npos;
    m.blueLcaValid = js.find("\"lca_b_px\"") != std::string::npos || js.find("\"lca_blue\"") != std::string::npos;
    m.sigma0    = extractJsonDouble(js, "sigma0", extractJsonDouble(js, "read_noise", -1.0));
    m.gainG     = extractJsonDouble(js, "gain_g", extractJsonDouble(js, "photon_gain", -1.0));
    m.hasSilicon = m.sigma0 >= 0.0 && m.gainG >= 0.0;
    m.sensorTempC = extractJsonDouble(js, "sensor_temp_c", extractJsonDouble(js, "temperature_c", NAN));
    m.warmPixels  = extractJsonInt(js, "warm_pixels", extractJsonInt(js, "hot_pixels", -1));

    std::string ois = extractJsonStr(js, "ois_status");
    if (!ois.empty()) m.oisSummary = [NSString stringWithFormat:@"OIS状态: %s", ois.c_str()];

    self.opticsModel = m;
    [self syncAllUiState];
}

- (void)onRunOpticsReport:(id)sender {
    (void)sender;
    if (self.currentTask) return;
    BurstGroupModel* g = [self selectedBurstGroup] ?: self.burstGroups.firstObject;
    if (!g || g.filePaths.count == 0) {
        NSAlert* a = [[NSAlert alloc] init];
        a.messageText = @"请先在左栏导入并选择一个连拍组";
        [a runModal];
        return;
    }
    NSString* cli = [self resolveActiveCliOrPrompt];
    if (!cli) return;

    NSString* outDir = [self effectiveOutputDirectory];
    [[NSFileManager defaultManager] createDirectoryAtPath:outDir withIntermediateDirectories:YES attributes:nil error:nil];
    NSString* prefix = [outDir stringByAppendingPathComponent:[@"nijin_optics_" stringByAppendingString:NSUUID.UUID.UUIDString]];

    NSMutableArray<NSString*>* args = [NSMutableArray array];
    if (self.customLibRawPath.length > 0) [args addObjectsFromArray:@[@"--libraw", self.customLibRawPath]];
    [args addObjectsFromArray:@[@"--optics-only", @"--optics-report", prefix]];
    [args addObjectsFromArray:g.filePaths];

    __weak AppDelegate* weakSelf = self;
    [self runUtilityCliTask:cli args:args label:@"正在运行原始线性 RAW 8× 光学精测..." completion:^(int rc) {
        AppDelegate* s = weakSelf;
        if (!s || [s selectedBurstGroup] != g) return;
        NSString* jsonPath = [prefix stringByAppendingString:@"_telemetry.json"];
        NSString* pngPath  = [prefix stringByAppendingString:@"_passport.png"];
        if (![[NSFileManager defaultManager] fileExistsAtPath:pngPath]) {
            pngPath = [prefix stringByAppendingString:@".png"];
        }
        if (rc == 0 && [[NSFileManager defaultManager] fileExistsAtPath:jsonPath]) {
            [s parseOpticsTelemetryFile:jsonPath cardPng:pngPath];
            s.lblStageStatus.stringValue = [@"光学报告 · " stringByAppendingString:s.opticsModel.tierBadge];
        }
    }];
}

- (void)onExportLcp:(id)sender {
    (void)sender;
    if (self.currentTask) return;
    if ([self selectedLcpFieldsCsv].length == 0) return;
    BurstGroupModel* g = [self selectedBurstGroup] ?: self.burstGroups.firstObject;
    if (!g || g.filePaths.count == 0) {
        NSAlert* a = [[NSAlert alloc] init];
        a.messageText = @"请先在左栏导入并选择一个连拍组";
        [a runModal];
        return;
    }
    NSString* cli = [self resolveActiveCliOrPrompt];
    if (!cli) return;

    NSSavePanel* sp = [NSSavePanel savePanel];
    sp.title = @"导出标准 Adobe .lcp 镜头校正档";
    sp.nameFieldStringValue = @"nijin_lens_profile.lcp";
    if ([sp runModal] != NSModalResponseOK || !sp.URL.path) return;

    NSMutableArray<NSString*>* args = [NSMutableArray array];
    if (self.customLibRawPath.length > 0) [args addObjectsFromArray:@[@"--libraw", self.customLibRawPath]];
    [args addObjectsFromArray:[self lcpArguments:sp.URL.path]];
    [args addObjectsFromArray:g.filePaths];

    [self runUtilityCliTask:cli args:args label:@"正在生成 Adobe .lcp 镜头校正档..." completion:^(int rc) {
        (void)rc;
    }];
}

- (void)onExportOpticsPng:(id)sender {
    (void)sender;
    NSSavePanel* sp = [NSSavePanel savePanel];
    sp.title = @"导出光学体检图卡 (.png)";
    sp.nameFieldStringValue = @"nijin_optics_passport.png";
    if ([sp runModal] != NSModalResponseOK || !sp.URL.path) return;

    if (self.opticsModel.lastCardPngPath && [[NSFileManager defaultManager] fileExistsAtPath:self.opticsModel.lastCardPngPath]) {
        NSData* data = [NSData dataWithContentsOfFile:self.opticsModel.lastCardPngPath];
        BOOL saved = [data writeToURL:sp.URL atomically:YES];
        self.lblStageStatus.stringValue = saved ? [NSString stringWithFormat:@"已导出体检图卡: %@", [sp.URL.path lastPathComponent]] : @"图卡写入失败，请选择可写目录";
        return;
    }

    // Snapshot Tab 4 diagnostic canvases directly at native Retina resolution
    [self.page3 layoutSubtreeIfNeeded];
    NSBitmapImageRep* rep = [self.page3 bitmapImageRepForCachingDisplayInRect:self.page3.bounds];
    if (rep) {
        [self.page3 cacheDisplayInRect:self.page3.bounds toBitmapImageRep:rep];
        NSData* png = [rep representationUsingType:NSBitmapImageFileTypePNG properties:@{}];
        BOOL saved = [png writeToFile:sp.URL.path atomically:YES];
        self.lblStageStatus.stringValue = saved ? [NSString stringWithFormat:@"已导出体检图卡: %@", [sp.URL.path lastPathComponent]] : @"图卡写入失败，请选择可写目录";
    }
}

- (void)onExportOpticsJson:(id)sender {
    (void)sender;
    if (self.currentTask) return;
    NSSavePanel* sp = [NSSavePanel savePanel];
    sp.title = @"导出完整光学与机身遥测 (.json)";
    sp.nameFieldStringValue = @"nijin_telemetry.json";
    if ([sp runModal] != NSModalResponseOK || !sp.URL.path) return;

    if (self.opticsModel.lastJsonPath && [[NSFileManager defaultManager] fileExistsAtPath:self.opticsModel.lastJsonPath]) {
        NSData* data = [NSData dataWithContentsOfFile:self.opticsModel.lastJsonPath];
        BOOL saved = [data writeToURL:sp.URL atomically:YES];
        self.lblStageStatus.stringValue = saved ? [NSString stringWithFormat:@"已导出遥测 JSON: %@", [sp.URL.path lastPathComponent]] : @"JSON 写入失败，请选择可写目录";
        return;
    }

    BurstGroupModel* g = [self selectedBurstGroup] ?: self.burstGroups.firstObject;
    if (!g || g.filePaths.count == 0) return;
    NSString* cli = [self resolveActiveCliOrPrompt];
    if (!cli) return;

    NSString* tmpPrefix = [NSTemporaryDirectory() stringByAppendingPathComponent:[@"nijin_optics_" stringByAppendingString:NSUUID.UUID.UUIDString]];
    NSMutableArray<NSString*>* args = [NSMutableArray array];
    if (self.customLibRawPath.length > 0) [args addObjectsFromArray:@[@"--libraw", self.customLibRawPath]];
    [args addObjectsFromArray:@[@"--optics-only", @"--optics-report", tmpPrefix]];
    [args addObjectsFromArray:g.filePaths];

    __weak AppDelegate* weakSelf = self;
    [self runUtilityCliTask:cli args:args label:@"正在采集完整光学与硅片遥测 JSON..." completion:^(int rc) {
        AppDelegate* s = weakSelf;
        if (!s) return;
        NSString* srcJson = [tmpPrefix stringByAppendingString:@"_telemetry.json"];
        if (rc == 0 && [[NSFileManager defaultManager] fileExistsAtPath:srcJson]) {
            BOOL saved = [[NSData dataWithContentsOfFile:srcJson] writeToURL:sp.URL atomically:YES];
            [s parseOpticsTelemetryFile:srcJson cardPng:nil];
            s.lblStageStatus.stringValue = saved ? [NSString stringWithFormat:@"已导出遥测 JSON: %@", [sp.URL.path lastPathComponent]] : @"JSON 写入失败，请选择可写目录";
        }
    }];
}

- (void)runUtilityCliTask:(NSString*)cliPath
                     args:(NSArray<NSString*>*)args
                    label:(NSString*)statusText
               completion:(void (^)(int exitCode))completion {
    self.userStoppedJob = NO;
    self.lblStageStatus.stringValue = statusText;
    self.progView.progress = 15.0;
    [self.progView setNeedsDisplay:YES];
    self.lblPct.stringValue = @"...";

    NSTask* t = [[NSTask alloc] init];
    t.executableURL = [NSURL fileURLWithPath:cliPath];
    t.arguments = args;
    NSPipe* output = [NSPipe pipe];
    t.standardOutput = output;
    t.standardError = output;
    NSFileHandle* reader = output.fileHandleForReading;
    reader.readabilityHandler = ^(NSFileHandle* h) {
        if ([h availableData].length == 0) h.readabilityHandler = nil;
    };
    self.currentTask = t;
    [self updateRunButtonTitle];

    __weak AppDelegate* weakSelf = self;
    t.terminationHandler = ^(NSTask* finished) {
        reader.readabilityHandler = nil;
        int rc = finished.terminationStatus;
        dispatch_async(dispatch_get_main_queue(), ^{
            AppDelegate* s = weakSelf;
            if (!s || s.currentTask != finished) return;
            s.currentTask = nil;
            s.progView.progress = (rc == 0) ? 100.0 : 0.0;
            [s.progView setNeedsDisplay:YES];
            s.lblPct.stringValue = (rc == 0) ? @"100%" : @"--";
            if (rc != 0 && !s.userStoppedJob) {
                s.lblStageStatus.stringValue = [NSString stringWithFormat:@"子任务结束 (退出码 %d)", rc];
            } else if (s.userStoppedJob) {
                s.lblStageStatus.stringValue = @"已停止当前任务";
            } else {
                s.lblStageStatus.stringValue = @"已完成";
            }
            [s updateRunButtonTitle];
            if (completion && !s.userStoppedJob) completion(rc);
        });
    };

    NSError* err = nil;
    if (![t launchAndReturnError:&err]) {
        reader.readabilityHandler = nil;
        self.currentTask = nil;
        self.progView.progress = 0.0;
        [self.progView setNeedsDisplay:YES];
        self.lblStageStatus.stringValue = [NSString stringWithFormat:@"启动失败: %@", err.localizedDescription];
        [self updateRunButtonTitle];
    }
}

- (NSArray<NSString*>*)buildCliArgsForGroup:(BurstGroupModel*)grp outDir:(NSString*)outDir {
    NSMutableArray<NSString*>* a = [NSMutableArray array];
    if (self.customLibRawPath.length > 0) {
        [a addObjectsFromArray:@[@"--libraw", self.customLibRawPath]];
    }
    [a addObject:@"--no-sort-time"];
    if (self.chkRecursive.state == NSControlStateValueOn) [a addObject:@"--recursive"];

    // <4 checked group runs with --policy force-all automatically
    [a addObjectsFromArray:@[@"--policy", (grp.validFrameCount < 4) ? @"force-all" : @"auto-prune"]];

    NSInteger pIdx = self.segPreset.selectedSegment;
    if (pIdx < 0 || pIdx > 3) pIdx = 0;
    [a addObjectsFromArray:@[@"--preset", [NSString stringWithUTF8String:kPresets[pIdx].code]]];

    // Motion & Pose Anchor
    NSString* mm = self.motionMode ?: @"auto";
    if ([mm isEqualToString:@"clean-plate"]) {
        [a addObjectsFromArray:@[@"--motion-mode", @"clean-plate", @"--clean-sens", self.edtCleanSens.stringValue]];
    } else if ([mm isEqualToString:@"virtual-nd"]) {
        [a addObjectsFromArray:@[@"--motion-mode", @"virtual-nd", @"--vnd-thresh", self.edtVndThresh.stringValue]];
    } else if ([mm isEqualToString:@"strobe"]) {
        NSString* stepStr = [NSString stringWithFormat:@"%ld", (long)(self.popStrobeStep.indexOfSelectedItem + 1)];
        NSString* fadeStr = (self.segStrobeFade.selectedSegment == 1) ? @"1" : @"0";
        [a addObjectsFromArray:@[@"--motion-mode", @"strobe", @"--strobe-step", stepStr, @"--strobe-fade", fadeStr]];
    } else {
        if (grp.customAnchor1Based > 0) {
            [a addObjectsFromArray:@[@"--anchor-frame", [NSString stringWithFormat:@"%d", grp.customAnchor1Based]]];
        } else {
            [a addObjectsFromArray:@[@"--anchor-frame", mm]];
        }
    }

    // Per-frame skip CSV (1-based)
    NSMutableArray<NSString*>* skips = [NSMutableArray array];
    for (FrameDiagItem* f in grp.frames) {
        if (!f.enabled) [skips addObject:[NSString stringWithFormat:@"%d", f.idx1Based]];
    }
    if (skips.count > 0) {
        [a addObjectsFromArray:@[@"--skip-frames", [skips componentsJoinedByString:@","]]];
    }

    NSInteger olpfIdx = self.popOlpf.indexOfSelectedItem;
    NSArray<NSString*>* olpfs = olpfModeCodes();
    if (olpfIdx >= 0 && (NSUInteger)olpfIdx < olpfs.count) {
        [a addObjectsFromArray:@[@"--olpf-mode", olpfs[(NSUInteger)olpfIdx]]];
    }

    NSArray<NSString*>* bkCodes = @[@"auto", @"gpu", @"cpu"];
    NSInteger bIdx = self.popBackend.indexOfSelectedItem;
    if (bIdx >= 0 && bIdx < 3) [a addObjectsFromArray:@[@"--backend", bkCodes[(NSUInteger)bIdx]]];

    // Reconstruction & Noise Sliders
    [a addObjectsFromArray:@[@"--denoise-strength", self.edtDenoise.stringValue]];
    [a addObjectsFromArray:@[@"--chroma-denoise",   self.edtChroma.stringValue]];
    [a addObjectsFromArray:@[@"--psf-iters",        self.edtPsfIters.stringValue]];
    [a addObjectsFromArray:@[@"--psf-sigma",        self.edtPsfSigma.stringValue]];
    [a addObjectsFromArray:@[@"--deghost-strength", self.edtDeghost.stringValue]];
    [a addObjectsFromArray:@[@"--ev-offset",        self.edtEv.stringValue]];
    [a addObjectsFromArray:@[@"--group-size",       self.edtGrpSz.stringValue]];
    if (self.edtThreads.integerValue > 0) {
        [a addObjectsFromArray:@[@"--threads", self.edtThreads.stringValue]];
    }

    // Toggles
    [a addObjectsFromArray:@[@"--silicon-clean",     self.chkSiliconClean.state == NSControlStateValueOn ? @"on" : @"off"]];
    [a addObjectsFromArray:@[@"--ibp-iters",         self.chkIbp.state == NSControlStateValueOn ? @"1" : @"0"]];
    [a addObjectsFromArray:@[@"--auto-lca",          self.chkAutoLca.state == NSControlStateValueOn ? @"on" : @"off"]];
    [a addObjectsFromArray:@[@"--auto-exposure",     self.chkAutoExp.state == NSControlStateValueOn ? @"on" : @"off"]];
    [a addObjectsFromArray:@[@"--highlight-rolloff", self.chkRolloff.state == NSControlStateValueOn ? @"on" : @"off"]];
    [a addObjectsFromArray:@[@"--dither",            self.chkDither.state == NSControlStateValueOn ? @"on" : @"off"]];

    // Outputs & Formats
    [a addObjectsFromArray:@[@"--out-dir", outDir, @"--progress-json"]];

    NSArray<NSString*>* rCodes = rawFormatCodes();
    NSInteger rIdx = self.popRawFmt.indexOfSelectedItem;
    if (rIdx >= 0 && (NSUInteger)rIdx < rCodes.count) {
        [a addObjectsFromArray:@[@"--raw-format", rCodes[(NSUInteger)rIdx]]];
    }
    [a addObjectsFromArray:@[@"--relay-raw", self.chkRelayRaw.state == NSControlStateValueOn ? @"on" : @"off"]];
    if (self.chkEmitRaw.state == NSControlStateValueOn) [a addObject:@"--emit-raw"];

    NSArray<NSString*>* cCodes = rgbFormatCodes();
    NSInteger cIdx = self.popRgbFmt.indexOfSelectedItem;
    if (cIdx >= 0 && (NSUInteger)cIdx < cCodes.count) {
        [a addObjectsFromArray:@[@"--rgb-format", cCodes[(NSUInteger)cIdx]]];
    }
    if (self.edtJxsBpp.stringValue.length > 0) {
        [a addObjectsFromArray:@[@"--jxs-bpp", self.edtJxsBpp.stringValue]];
    }
    [a addObjectsFromArray:@[@"--color-space", (self.segColorSpace.selectedSegment == 1) ? @"adobergb" : @"srgb"]];
    if (self.chkEmitRgb.state == NSControlStateValueOn) [a addObject:@"--emit-rgb"];

    if (self.chkEmitField.state == NSControlStateValueOn) {
        [a addObject:@"--emit-field"];
        NSArray<NSString*>* fmCodes = fieldModeCodes();
        NSInteger fmIdx = self.popFieldMode.indexOfSelectedItem;
        if (fmIdx >= 0 && (NSUInteger)fmIdx < fmCodes.count) {
            [a addObjectsFromArray:@[@"--field-mode", fmCodes[(NSUInteger)fmIdx]]];
        }
        [a addObjectsFromArray:@[@"--fields", [self selectedFieldsCsv]]];
    }

    // Tab 2 Creative / Layer Extractions
    if (self.chkEmitTransientTab1.state == NSControlStateValueOn) {
        [a addObjectsFromArray:@[
            @"--emit-transient",
            @"--transient-sigma", self.edtTransSigma.stringValue,
            @"--transient-color", (self.segTransColor.selectedSegment == 1) ? @"gray" : @"rgb"
        ]];
    }

    if (self.chkReflection.state == NSControlStateValueOn) {
        NSString* rMode = (self.segReflectionMode.selectedSegment == 1) ? @"split-layers" : @"glint-suppress";
        [a addObjectsFromArray:@[@"--reflection-mode", rMode]];
    } else {
        [a addObjectsFromArray:@[@"--reflection-mode", @"off"]];
    }

    BOOL depthAllowed = !grp.depthKnown || grp.depthObservable;
    if (depthAllowed && self.chkExportParallax.state == NSControlStateValueOn) {
        [a addObjectsFromArray:@[@"--export-parallax-html", @"--parallax-amp", self.edtParallaxAmp.stringValue]];
    }
    if (depthAllowed && self.chkExportRefocus.state == NSControlStateValueOn) {
        [a addObjectsFromArray:[self refocusArguments]];
    }

    // Tab 3 Dataset Factory
    if (self.chkExportTiles.state == NSControlStateValueOn) {
        NSString* tilesDir = [outDir stringByAppendingPathComponent:@"dataset_tiles"];
        NSString* tSz = (self.segTileSize.selectedSegment == 1) ? @"512" : @"256";
        [a addObjectsFromArray:@[
            @"--export-dataset-tiles", tilesDir,
            @"--tile-size", tSz,
            @"--tile-min-conf", self.edtTileConf.stringValue
        ]];
    }

    [a addObjectsFromArray:grp.filePaths];
    return a;
}

- (void)onRunOrStop:(id)sender {
    (void)sender;
    if (self.currentTask) {
        self.userStoppedJob = YES;
        if (self.currentTask.isRunning) [self.currentTask terminate];
        return;
    }

    [self saveUiPrefs];
    if (self.chkEmitField.state == NSControlStateValueOn && [self selectedFieldCount] == 0) {
        self.lblStageStatus.stringValue = @"请在「物理场与数据」中选择至少一项物理场";
        return;
    }
    NSMutableArray<BurstGroupModel*>* checkedGroups = [NSMutableArray array];
    for (BurstGroupModel* g in self.burstGroups) {
        if (g.checked && g.filePaths.count > 0) [checkedGroups addObject:g];
    }
    if (checkedGroups.count == 0) {
        NSAlert* a = [[NSAlert alloc] init];
        a.messageText = (self.burstGroups.count == 0)
            ? @"请先拖入或选择 RAW 连拍文件 / 文件夹。"
            : @"请至少在左侧队列中勾选一个待执行的连拍组。";
        [a runModal];
        return;
    }

    BOOL anyOut = (self.chkEmitRaw.state == NSControlStateValueOn) ||
                  (self.chkEmitRgb.state == NSControlStateValueOn) ||
                  (self.chkEmitField.state == NSControlStateValueOn) ||
                  (self.chkEmitTransientTab1.state == NSControlStateValueOn) ||
                  (self.chkExportParallax.state == NSControlStateValueOn) ||
                  (self.chkExportRefocus.state == NSControlStateValueOn) ||
                  (self.chkExportTiles.state == NSControlStateValueOn);
    if (!anyOut) {
        NSAlert* a = [[NSAlert alloc] init];
        a.messageText = @"请至少勾选一项输出（2× RGGB / 2× RGB / 物理场图 / 附加分离层）。";
        [a runModal];
        return;
    }

    NSString* cliPath = [self resolveActiveCliOrPrompt];
    if (!cliPath) return;

    NSString* outDir = [self effectiveOutputDirectory];
    if (self.edtOutDir.stringValue.length == 0) self.edtOutDir.stringValue = outDir;

    NSMutableArray<NSArray<NSString*>*>* jobs = [NSMutableArray array];
    NSMutableArray<BurstGroupModel*>* runnable = [NSMutableArray array];
    for (BurstGroupModel* g in checkedGroups) {
        NSArray<NSString*>* job = [self buildCliArgsForGroup:g outDir:outDir];
        BOOL hasOutput = NO;
        for (NSString* flag in @[@"--emit-raw", @"--emit-rgb", @"--emit-field", @"--emit-transient",
                                @"--export-parallax-html", @"--export-refocus", @"--export-dataset-tiles"])
            hasOutput |= [job containsObject:flag];
        if (hasOutput) { [jobs addObject:job]; [runnable addObject:g]; }
    }
    if (runnable.count == 0) {
        self.lblStageStatus.stringValue = @"已选输出的实测条件不满足，请调整输出或连拍组";
        return;
    }
    checkedGroups = runnable;
    self.queuedArguments = jobs;
    self.userStoppedJob = NO;
    self.progView.progress = 0.0;
    [self.progView setNeedsDisplay:YES];
    self.lblPct.stringValue = @"0%";
    [self runQueuedGroups:checkedGroups atIndex:0 cliPath:cliPath outDir:outDir];
}

- (void)runQueuedGroups:(NSArray<BurstGroupModel*>*)groups
                atIndex:(NSUInteger)idx
                cliPath:(NSString*)cliPath
                 outDir:(NSString*)outDir {
    if (self.userStoppedJob || idx >= groups.count) {
        self.currentTask = nil;
        if (!self.userStoppedJob) {
            self.progView.progress = 100.0;
            [self.progView setNeedsDisplay:YES];
            self.lblPct.stringValue = @"100%";
            self.lblStageStatus.stringValue = [NSString stringWithFormat:@"全部完成 · 已输出 %lu 组至 %@",
                                               (unsigned long)groups.count, [outDir lastPathComponent]];
        } else {
            self.lblStageStatus.stringValue = @"已停止执行";
        }
        [self updateRunButtonTitle];
        return;
    }

    BurstGroupModel* grp = groups[idx];
    NSArray<NSString*>* args = self.queuedArguments[idx];

    NSTask* task = [[NSTask alloc] init];
    task.executableURL = [NSURL fileURLWithPath:cliPath];
    task.arguments = args;
    NSPipe* pipe = [NSPipe pipe];
    task.standardOutput = pipe;
    task.standardError = pipe;
    self.currentTask = task;
    [self updateRunButtonTitle];

    self.lblStageStatus.stringValue = [NSString stringWithFormat:@"[组 %lu/%lu] 正在启动合成...",
                                       (unsigned long)(idx + 1), (unsigned long)groups.count];

    NSFileHandle* fh = [pipe fileHandleForReading];
    __weak AppDelegate* weakSelf = self;
    NSUInteger totalGroups = groups.count;
    __block std::string pending;

    fh.readabilityHandler = ^(NSFileHandle* handle) {
        NSData* d = [handle availableData];
        if (d.length == 0) { handle.readabilityHandler = nil; return; }
        pending.append((const char*)d.bytes, d.length);
        size_t nl;
        while ((nl = pending.find('\n')) != std::string::npos) {
            std::string s = pending.substr(0, nl);
            pending.erase(0, nl + 1);
            if (!s.empty() && s.front() == '{' && s.find("\"event\"") != std::string::npos) {
                int pct = extractJsonInt(s, "percent", -1);
                std::string msg = extractJsonStr(s, "message");
                if (msg.empty()) msg = extractJsonStr(s, "stage");
                dispatch_async(dispatch_get_main_queue(), ^{
                    AppDelegate* st = weakSelf;
                    if (!st || st.currentTask != task) return;
                    if (pct >= 0) {
                        double overall = (((double)idx + (double)pct / 100.0) / (double)totalGroups) * 100.0;
                        st.progView.progress = overall;
                        [st.progView setNeedsDisplay:YES];
                        st.lblPct.stringValue = [NSString stringWithFormat:@"%.0f%%", overall];
                    }
                    if (!msg.empty()) {
                        st.lblStageStatus.stringValue = [NSString stringWithFormat:@"[组 %lu/%lu] %s",
                            (unsigned long)(idx + 1), (unsigned long)totalGroups, msg.c_str()];
                    }
                });
            }
        }
    };

    task.terminationHandler = ^(NSTask* finished) {
        fh.readabilityHandler = nil;
        int rc = finished.terminationStatus;
        dispatch_async(dispatch_get_main_queue(), ^{
            AppDelegate* st = weakSelf;
            if (!st || st.currentTask != finished) return;
            if (st.userStoppedJob || rc != 0) {
                st.currentTask = nil;
                if (!st.userStoppedJob) {
                    st.lblStageStatus.stringValue = [NSString stringWithFormat:@"组 %02d 执行中止 (退出码 %d)", grp.displayIndex, rc];
                } else {
                    st.lblStageStatus.stringValue = @"已停止执行";
                }
                [st updateRunButtonTitle];
                return;
            }
            [st runQueuedGroups:groups atIndex:(idx + 1) cliPath:cliPath outDir:outDir];
        });
    };

    NSError* err = nil;
    if (![task launchAndReturnError:&err]) {
        fh.readabilityHandler = nil;
        self.currentTask = nil;
        self.lblStageStatus.stringValue = [NSString stringWithFormat:@"启动失败: %@", err.localizedDescription];
        [self updateRunButtonTitle];
    }
}

// ============================================================================
// Responsive Programmatic Layout (`1280 x 800`, min `1140 x 740`)
// ============================================================================

- (void)relayoutForBounds:(NSRect)bounds {
    CGFloat W = std::max(1140.0, (double)bounds.size.width);
    CGFloat H = std::max(740.0,  (double)bounds.size.height);

    CGFloat topY = 66.0;
    CGFloat botBarY = H - 56.0;
    CGFloat workBottom = botBarY - 12.0;
    CGFloat workH = workBottom - topY; // 666 at H=800, 606 at H=740

    // --- Left Persistent Master-Detail Dock (x = 20..476, width = 456 pt) ---
    CGFloat lx = 20.0;
    CGFloat lw = 456.0;

    self.lblQueueHeader.frame = NSMakeRect(lx + 2, topY + 3, 120, 22);
    self.chkSortTime.frame    = NSMakeRect(lx + lw - 236, topY + 3, 122, 22);
    self.chkRecursive.frame   = NSMakeRect(lx + lw - 108, topY + 3, 108, 22);

    CGFloat upperCardY = topY + 30.0;
    CGFloat availCardsH = workH - 30.0 - 38.0 - 28.0;
    CGFloat upperH = std::floor(availCardsH * 0.53);
    CGFloat lowerH = availCardsH - upperH;

    NSRect upperCard = NSMakeRect(lx, upperCardY, lw, upperH);
    self.canvas.leftUpperCardRect = upperCard;
    self.emptyWell.frame = NSInsetRect(upperCard, 12, 12);
    self.svQueue.frame   = NSInsetRect(upperCard, 8, 8);
    if (self.tblQueue.tableColumns.count > 0) {
        self.tblQueue.tableColumns[0].width = self.svQueue.bounds.size.width - 18;
    }

    CGFloat actY = NSMaxY(upperCard) + 6.0;
    self.btnAddFiles.frame   = NSMakeRect(lx,       actY, 102, 28);
    self.btnAddFolder.frame  = NSMakeRect(lx + 108, actY, 102, 28);
    self.btnExportTopK.frame = NSMakeRect(lx + 216, actY, 162, 28);
    self.btnClearQueue.frame = NSMakeRect(lx + 384, actY, 72,  28);

    CGFloat inspHdrY = actY + 36.0;
    self.lblInspectorHeader.frame = NSMakeRect(lx + 2,   inspHdrY, 180, 20);
    self.lblInspectorHint.frame   = NSMakeRect(lx + 184, inspHdrY + 1, lw - 186, 18);

    NSRect lowerCard = NSMakeRect(lx, inspHdrY + 24.0, lw, lowerH);
    self.canvas.leftLowerCardRect = lowerCard;
    self.svFrames.frame = NSInsetRect(lowerCard, 8, 8);
    if (self.tblFrames.tableColumns.count > 0) {
        self.tblFrames.tableColumns[0].width = self.svFrames.bounds.size.width - 18;
    }
    self.lblInspectorEmpty.frame = NSMakeRect(lx + 16, NSMidY(lowerCard) - 10, lw - 32, 22);

    // --- Right 4-Tab Stage (x = 492 .. W - 20, width = 768 pt at W=1280) ---
    CGFloat rx = 492.0;
    CGFloat rw = W - 20.0 - rx;
    self.tabBar.frame = NSMakeRect(rx, topY - 2.0, rw, 34.0);

    NSRect pageFrame = NSMakeRect(rx, topY + 40.0, rw, workBottom - (topY + 40.0));
    CGFloat pw = pageFrame.size.width;
    CGFloat ph = pageFrame.size.height; // 626 at H=800, 566 at H=740
    self.page0.frame = pageFrame;
    self.page1.frame = pageFrame;
    self.page2.frame = pageFrame;
    self.page3.frame = pageFrame;

    // ================== Page 0 (Tab 1: 超分与主输出) ==================
    CGFloat p0c1H = 132.0;
    CGFloat p0c2H = 184.0;
    CGFloat p0Gap = std::max(8.0, std::min(12.0, (ph - p0c1H - p0c2H - 234.0) * 0.5));
    CGFloat p0c3Y = p0c1H + p0Gap + p0c2H + p0Gap;
    CGFloat p0c3H = ph - p0c3Y;

    NSRect p0Card1 = NSMakeRect(0, 0, pw, p0c1H);
    NSRect p0Card2 = NSMakeRect(0, p0c1H + p0Gap, pw, p0c2H);
    NSRect p0Card3 = NSMakeRect(0, p0c3Y, pw, p0c3H);
    self.page0.cardRects = @[
        [NSValue valueWithRect:p0Card1],
        [NSValue valueWithRect:p0Card2],
        [NSValue valueWithRect:p0Card3]
    ];

    // Card 1: 场景与时间模式
    self.lblT1Card1Title.frame = NSMakeRect(16, 10, 220, 20);
    self.btnPortalQuad.frame   = NSMakeRect(pw - 166, 9, 150, 22);

    self.lblPresetCap.frame    = NSMakeRect(16, 40, 62, 20);
    self.segPreset.frame       = NSMakeRect(82, 37, std::min(360.0, (double)pw - 104.0), 25);

    self.lblMotionCap.frame    = NSMakeRect(16, 71, 62, 20);
    CGFloat portalStrobeW = 104.0;
    CGFloat segMotW = std::min(430.0, (double)pw - 82.0 - portalStrobeW - 24.0);
    self.segMotionTab1.frame   = NSMakeRect(82, 68, segMotW, 25);
    self.btnPortalStrobe.frame = NSMakeRect(pw - portalStrobeW - 16, 69, portalStrobeW, 22);

    self.lblOlpfCap.frame      = NSMakeRect(16, 102, 62, 20);
    self.popOlpf.frame         = NSMakeRect(82, 99, std::min(320.0, (double)pw - 104.0), 26);

    // Card 2: 输出与快捷附加层
    CGFloat c2y = p0Card2.origin.y;
    self.lblT1Card2Title.frame = NSMakeRect(16, c2y + 10, 150, 20);
    self.lblT1Card2Sub.frame   = NSMakeRect(166, c2y + 12, pw - 182, 18);

    self.lblOutDirCap.frame    = NSMakeRect(16, c2y + 39, 62, 20);
    self.edtOutDir.frame       = NSMakeRect(82, c2y + 37, pw - 82 - 16 - 92, 24);
    self.btnBrowseOut.frame    = NSMakeRect(pw - 104, c2y + 35, 56, 28);
    self.btnClearOut.frame     = NSMakeRect(pw - 44,  c2y + 35, 28, 28);

    self.chkEmitRaw.frame      = NSMakeRect(16, c2y + 70, 110, 22);
    CGFloat rawPopW = std::min(282.0, (double)pw - 266.0);
    self.popRawFmt.frame       = NSMakeRect(128, c2y + 68, rawPopW, 26);
    self.chkRelayRaw.frame     = NSMakeRect(128 + rawPopW + 12, c2y + 70, 122, 22);

    self.chkEmitRgb.frame      = NSMakeRect(16, c2y + 101, 110, 22);
    CGFloat rgbPopW = std::min(218.0, (double)pw - 376.0);
    self.popRgbFmt.frame       = NSMakeRect(128, c2y + 99, rgbPopW, 26);
    CGFloat bppX = 128 + rgbPopW + 10;
    self.lblBppCap.frame       = NSMakeRect(bppX, c2y + 103, 28, 18);
    self.edtJxsBpp.frame       = NSMakeRect(bppX + 30, c2y + 100, 48, 23);
    self.segColorSpace.frame   = NSMakeRect(bppX + 86, c2y + 99, std::min(148.0, (double)pw - (bppX + 86) - 16.0), 25);

    self.chkEmitField.frame    = NSMakeRect(16, c2y + 131, 110, 22);
    self.popFieldMode.frame    = NSMakeRect(128, c2y + 129, 218, 26);
    self.btnPortalFields.frame = NSMakeRect(pw - 166, c2y + 131, 150, 22);

    self.chkEmitTransientTab1.frame = NSMakeRect(16, c2y + 157, 330, 22);
    self.btnPortalRefl.frame        = NSMakeRect(pw - 134, c2y + 157, 118, 22);

    // Card 3: 重建与新噪声态精调
    CGFloat c3y = p0Card3.origin.y;
    self.lblT1Card3Title.frame = NSMakeRect(16, c3y + 9, 240, 20);
    self.btnResetPreset.frame  = NSMakeRect(pw - 96, c3y + 7, 80, 24);

    CGFloat extraV = std::max(0.0, (double)(p0c3H - 230.0));
    CGFloat rowStep = 25.0 + std::min(4.0, extraV / 8.0);
    CGFloat sRow0 = c3y + 36.0;
    CGFloat colW = (pw - 44.0) * 0.5;
    CGFloat cLeft = 16.0;
    CGFloat cRight = 16.0 + colW + 12.0;
    CGFloat lblW = 88.0;
    CGFloat edtW = 48.0;
    CGFloat sldW = std::max(84.0, (double)colW - lblW - edtW - 14.0);

    auto placeKnob = [&](NSTextField* l, NSSlider* s, NSTextField* e, CGFloat x, CGFloat y) {
        l.frame = NSMakeRect(x, y + 2, lblW, 19);
        s.frame = NSMakeRect(x + lblW + 4, y + 2, sldW, 19);
        e.frame = NSMakeRect(x + lblW + 8 + sldW, y, edtW, 22);
    };
    placeKnob(self.lblDenoise,  self.sldDenoise,  self.edtDenoise,  cLeft,  sRow0);
    placeKnob(self.lblPsfIters, self.sldPsfIters, self.edtPsfIters, cRight, sRow0);
    placeKnob(self.lblChroma,   self.sldChroma,   self.edtChroma,   cLeft,  sRow0 + rowStep);
    placeKnob(self.lblPsfSigma, self.sldPsfSigma, self.edtPsfSigma, cRight, sRow0 + rowStep);
    placeKnob(self.lblDeghost,  self.sldDeghost,  self.edtDeghost,  cLeft,  sRow0 + rowStep * 2.0);
    placeKnob(self.lblEv,       self.sldEv,       self.edtEv,       cRight, sRow0 + rowStep * 2.0);

    CGFloat togY1 = sRow0 + rowStep * 3.0 + 4.0 + extraV * 0.15;
    CGFloat togColW = (pw - 32.0) / 3.0;
    self.chkSiliconClean.frame = NSMakeRect(16,                 togY1, togColW, 21);
    self.chkIbp.frame          = NSMakeRect(16 + togColW,       togY1, togColW, 21);
    self.chkAutoLca.frame      = NSMakeRect(16 + togColW * 2.0, togY1, togColW, 21);

    CGFloat togY2 = togY1 + 23.0 + extraV * 0.08;
    self.chkAutoExp.frame      = NSMakeRect(16,                 togY2, togColW, 21);
    self.chkRolloff.frame      = NSMakeRect(16 + togColW,       togY2, togColW, 21);
    self.chkDither.frame       = NSMakeRect(16 + togColW * 2.0, togY2, togColW, 21);

    CGFloat compY = togY2 + 26.0 + extraV * 0.14;
    self.lblBackendCap.frame   = NSMakeRect(16,  compY + 3, 36, 18);
    self.popBackend.frame      = NSMakeRect(54,  compY, 152, 25);
    self.lblGrpSzCap.frame     = NSMakeRect(220, compY + 3, 58, 18);
    self.edtGrpSz.frame        = NSMakeRect(280, compY + 1, 46, 22);
    self.lblThreadsCap.frame   = NSMakeRect(340, compY + 3, 36, 18);
    self.edtThreads.frame      = NSMakeRect(378, compY + 1, 46, 22);

    CGFloat stripY = NSMaxY(p0Card3) - 29.0;
    self.page0.dividerLines = @[[NSValue valueWithRect:NSMakeRect(16, stripY - 5, pw - 32, 1)]];
    self.lblEngineStrip.frame = NSMakeRect(16, stripY, pw - 166, 19);
    self.btnEngineSheet.frame = NSMakeRect(pw - 144, stripY - 2, 128, 23);
    [self.page0 setNeedsDisplay:YES];

    // ================== Page 1 (Tab 2: 时空与创意) ==================
    CGFloat p1Gap = std::max(10.0, (ph - 172.0 - 180.0 - 186.0) * 0.5);
    NSRect p1Card1 = NSMakeRect(0, 0, pw, 174);
    NSRect p1Card2 = NSMakeRect(0, 174 + p1Gap, pw, 184);
    NSRect p1Card3 = NSMakeRect(0, 174 + p1Gap + 184 + p1Gap, pw, ph - (174 + p1Gap + 184 + p1Gap));
    self.page1.cardRects = @[
        [NSValue valueWithRect:p1Card1],
        [NSValue valueWithRect:p1Card2],
        [NSValue valueWithRect:p1Card3]
    ];

    self.lblT2Card1Title.frame = NSMakeRect(16, 10, pw - 32, 20);
    self.radT2Freeze.frame     = NSMakeRect(16, 38, 126, 22);
    self.segT2FreezePose.frame = NSMakeRect(148, 36, std::min(430.0, (double)pw - 168.0), 25);

    self.radT2Clean.frame      = NSMakeRect(16, 71, 210, 22);
    self.lblCleanSens.frame    = NSMakeRect(236, 73, 98, 18);
    self.sldCleanSens.frame    = NSMakeRect(338, 73, std::max(120.0, (double)pw - 416.0), 19);
    self.edtCleanSens.frame    = NSMakeRect(pw - 68, 70, 52, 22);

    self.radT2Vnd.frame        = NSMakeRect(16, 104, 210, 22);
    self.lblVndThresh.frame    = NSMakeRect(236, 106, 98, 18);
    self.sldVndThresh.frame    = NSMakeRect(338, 106, std::max(120.0, (double)pw - 416.0), 19);
    self.edtVndThresh.frame    = NSMakeRect(pw - 68, 103, 52, 22);

    self.radT2Strobe.frame     = NSMakeRect(16, 137, 210, 22);
    self.lblStrobeStep.frame   = NSMakeRect(236, 139, 64, 18);
    self.popStrobeStep.frame   = NSMakeRect(302, 135, 96, 26);
    self.segStrobeFade.frame   = NSMakeRect(408, 135, 168, 25);

    CGFloat p1c2y = p1Card2.origin.y;
    self.lblT2Card2Title.frame      = NSMakeRect(16, p1c2y + 10, pw - 32, 20);
    self.chkEmitTransientTab2.frame = NSMakeRect(16, p1c2y + 38, 360, 22);
    self.lblTransSigma.frame        = NSMakeRect(38, p1c2y + 66, 86, 18);
    self.sldTransSigma.frame        = NSMakeRect(128, p1c2y + 66, 160, 19);
    self.edtTransSigma.frame        = NSMakeRect(294, p1c2y + 64, 48, 22);
    self.segTransColor.frame        = NSMakeRect(362, p1c2y + 63, 168, 25);

    self.page1.dividerLines = @[[NSValue valueWithRect:NSMakeRect(16, p1c2y + 96, pw - 32, 1)]];
    self.chkReflection.frame        = NSMakeRect(16, p1c2y + 106, 280, 22);
    self.segReflectionMode.frame    = NSMakeRect(302, p1c2y + 104, std::min(308.0, (double)pw - 318.0), 25);
    self.lblDriftReadout.frame      = NSMakeRect(38, p1c2y + 136, pw - 54, 20);

    CGFloat p1c3y = p1Card3.origin.y;
    self.lblT2Card3Title.frame   = NSMakeRect(16, p1c3y + 10, pw - 32, 20);
    self.parallaxGateView.frame  = NSMakeRect(16, p1c3y + 38, pw - 32, 24);

    self.chkExportParallax.frame = NSMakeRect(16, p1c3y + 70, 290, 22);
    self.lblParallaxAmp.frame    = NSMakeRect(318, p1c3y + 72, 86, 18);
    self.sldParallaxAmp.frame    = NSMakeRect(408, p1c3y + 72, std::max(96.0, (double)pw - 486.0), 19);
    self.edtParallaxAmp.frame    = NSMakeRect(pw - 68, p1c3y + 70, 52, 22);

    self.chkExportRefocus.frame  = NSMakeRect(16, p1c3y + 106, 360, 22);
    self.lblRefocusPos.frame     = NSMakeRect(38, p1c3y + 136, 78, 18);
    self.sldRefocusPos.frame     = NSMakeRect(118, p1c3y + 136, 130, 19);
    self.edtRefocusPos.frame     = NSMakeRect(254, p1c3y + 134, 48, 22);

    self.lblBokehStr.frame       = NSMakeRect(320, p1c3y + 136, 78, 18);
    self.sldBokehStr.frame       = NSMakeRect(400, p1c3y + 136, std::max(96.0, (double)pw - 478.0), 19);
    self.edtBokehStr.frame       = NSMakeRect(pw - 68, p1c3y + 134, 52, 22);
    [self.page1 setNeedsDisplay:YES];

    // ================== Page 2 (Tab 3: 物理场与数据) ==================
    CGFloat p2c2H = 132.0;
    CGFloat p2Gap = 12.0;
    CGFloat p2c1H = ph - p2c2H - p2Gap;
    NSRect p2Card1 = NSMakeRect(0, 0, pw, p2c1H);
    NSRect p2Card2 = NSMakeRect(0, p2c1H + p2Gap, pw, p2c2H);
    self.page2.cardRects = @[
        [NSValue valueWithRect:p2Card1],
        [NSValue valueWithRect:p2Card2]
    ];

    self.lblT3Card1Title.frame = NSMakeRect(16, 10, pw - 32, 20);
    self.lblCarrierCap.frame   = NSMakeRect(16, 40, 62, 20);
    self.segCarrierTab3.frame  = NSMakeRect(82, 37, std::min(560.0, (double)pw - 98.0), 25);

    self.lblMatrixHeader.frame = NSMakeRect(16, 76, pw - 210, 20);
    self.btnFieldsCore3.frame  = NSMakeRect(pw - 178, 73, 84, 25);
    self.btnFieldsAll.frame    = NSMakeRect(pw - 88,  73, 72, 25);

    CGFloat boxTop = 106.0;
    CGFloat boxGap = 12.0;
    CGFloat boxW = (pw - 32.0 - boxGap) * 0.5;
    CGFloat boxH = std::max(114.0, (p2c1H - boxTop - boxGap - 16.0) * 0.5);
    NSRect sub0 = NSMakeRect(16,              boxTop,              boxW, boxH);
    NSRect sub1 = NSMakeRect(16 + boxW + boxGap, boxTop,           boxW, boxH);
    NSRect sub2 = NSMakeRect(16,              boxTop + boxH + boxGap, boxW, boxH);
    NSRect sub3 = NSMakeRect(16 + boxW + boxGap, boxTop + boxH + boxGap, boxW, boxH);
    self.page2.subBoxRects = @[
        [NSValue valueWithRect:sub0],
        [NSValue valueWithRect:sub1],
        [NSValue valueWithRect:sub2],
        [NSValue valueWithRect:sub3]
    ];
    self.page2.subBoxTitles = @[
        @"核心合成场",
        @"空间与本征材质场",
        @"频域与辐射度误差场",
        @"照明与隐形介质场"
    ];

    NSRect subs[4] = {sub0, sub1, sub2, sub3};
    int rowInBox[4] = {0, 0, 0, 0};
    CGFloat chkStep = std::min(27.0, (boxH - 34.0) / 3.0);
    for (NSUInteger i = 0; i < self.chkFields.count && i < 11; ++i) {
        int bi = kFieldSpecs[i].boxIdx;
        int ri = rowInBox[bi]++;
        NSRect br = subs[bi];
        self.chkFields[i].frame = NSMakeRect(br.origin.x + 12, br.origin.y + 30 + ri * chkStep, br.size.width - 20, 21);
    }
    self.lblFieldsGateNote.frame = NSMakeRect(sub3.origin.x + 14, sub3.origin.y + 30 + 2 * chkStep + 2, sub3.size.width - 24, 18);

    CGFloat p2c2y = p2Card2.origin.y;
    self.lblT3Card2Title.frame = NSMakeRect(16, p2c2y + 10, pw - 32, 20);
    self.chkExportTiles.frame  = NSMakeRect(16, p2c2y + 40, pw - 32, 22);
    self.lblTileSizeCap.frame  = NSMakeRect(38, p2c2y + 76, 66, 20);
    self.segTileSize.frame     = NSMakeRect(106, p2c2y + 73, 168, 25);
    self.lblTileConfCap.frame  = NSMakeRect(298, p2c2y + 76, 128, 20);
    self.sldTileConf.frame     = NSMakeRect(430, p2c2y + 76, std::max(84.0, (double)pw - 508.0), 19);
    self.edtTileConf.frame     = NSMakeRect(pw - 68, p2c2y + 74, 52, 22);
    [self.page2 setNeedsDisplay:YES];

    // ================== Page 3 (Tab 4: 镜头与机身) ==================
    CGFloat p3c2H = 62.0;
    CGFloat p3c3H = 104.0;
    CGFloat p3Gap = 10.0;
    CGFloat p3c1H = ph - p3c2H - p3c3H - p3Gap * 2.0;

    NSRect p3Card1 = NSMakeRect(0, 0, pw, p3c1H);
    NSRect p3Card2 = NSMakeRect(0, p3c1H + p3Gap, pw, p3c2H);
    NSRect p3Card3 = NSMakeRect(0, p3c1H + p3Gap + p3c2H + p3Gap, pw, p3c3H);
    self.page3.cardRects = @[
        [NSValue valueWithRect:p3Card1],
        [NSValue valueWithRect:p3Card2],
        [NSValue valueWithRect:p3Card3]
    ];

    CGFloat plotGap = 12.0;
    CGFloat plotW = (pw - 32.0 - plotGap) * 0.5;
    CGFloat plotH = p3c1H - 58.0;
    self.trajCanvas.frame  = NSMakeRect(16, 14, plotW, plotH);
    self.radarCanvas.frame = NSMakeRect(16 + plotW + plotGap, 14, plotW, plotH);

    self.btnRunOptics.frame = NSMakeRect(16, p3c1H - 36, 220, 28);
    self.lblOisStatus.frame = NSMakeRect(248, p3c1H - 31, pw - 264, 20);

    CGFloat p3c2y = p3Card2.origin.y;
    self.lblT4SiliconTitle.frame  = NSMakeRect(16, p3c2y + 8, 220, 19);
    self.lblSiliconReadout.frame  = NSMakeRect(16, p3c2y + 32, pw - 32, 20);

    CGFloat p3c3y = p3Card3.origin.y;
    self.lblT4ExportTitle.frame   = NSMakeRect(16, p3c3y + 8, 240, 19);
    self.lblLcpItemsCap.frame     = NSMakeRect(16, p3c3y + 35, 80, 20);
    self.chkLcpLca.frame          = NSMakeRect(100, p3c3y + 34, 130, 22);
    self.chkLcpVignette.frame     = NSMakeRect(238, p3c3y + 34, 178, 22);
    self.chkLcpDistortion.frame   = NSMakeRect(422, p3c3y + 34, 168, 22);

    CGFloat expBtnW = std::min(216.0, (pw - 32.0 - 20.0) / 3.0);
    self.btnExportLcp.frame        = NSMakeRect(16,                        p3c3y + 64, expBtnW, 28);
    self.btnExportOpticsPng.frame  = NSMakeRect(16 + expBtnW + 10,         p3c3y + 64, expBtnW, 28);
    self.btnExportOpticsJson.frame = NSMakeRect(16 + (expBtnW + 10) * 2.0, p3c3y + 64, expBtnW, 28);
    [self.page3 setNeedsDisplay:YES];

    // --- Bottom Global Action Bar ---
    self.btnRun.frame         = NSMakeRect(20, botBarY + 10, 274, 36);
    self.btnOpenOutDir.frame  = NSMakeRect(W - 134, botBarY + 11, 114, 34);

    CGFloat progX = 310.0;
    CGFloat progW = W - 148.0 - progX;
    self.lblStageStatus.frame = NSMakeRect(progX, botBarY + 10, progW - 56, 18);
    self.lblPct.frame         = NSMakeRect(progX + progW - 52, botBarY + 10, 52, 18);
    self.progView.frame       = NSMakeRect(progX, botBarY + 32, progW, 11);
}

// ============================================================================
// App Lifecycle, UI Construction & Automated Self-Tests
// ============================================================================

- (void)application:(NSApplication*)sender openFiles:(NSArray<NSString*>*)filenames {
    (void)sender;
    if (filenames.count > 0) [self addInputPaths:filenames];
    [NSApp replyToOpenOrPrint:NSApplicationDelegateReplySuccess];
}

- (void)applicationWillFinishLaunching:(NSNotification*)notification {
    (void)notification;
    [self setupMainMenu];
}

- (void)windowDidResize:(NSNotification*)notification {
    (void)notification;
    if (self.canvas) [self relayoutForBounds:self.canvas.bounds];
}

- (void)applicationDidFinishLaunching:(NSNotification*)notification {
    (void)notification;

    self.rawInputPaths = [NSMutableArray array];
    self.fileByBasename = [NSMutableDictionary dictionary];
    self.burstGroups = [NSMutableArray array];
    self.chkFields = [NSMutableArray array];
    self.opticsModel = [[OpticsTelemetryModel alloc] init];
    self.motionMode = @"auto";
    self.customCliPath = [nijinPrefs() stringForKey:kPrefCliPath] ?: [nijinLegacyPrefs() stringForKey:kPrefCliPath];
    self.customLibRawPath = [nijinPrefs() stringForKey:kPrefLibRawPath] ?: [nijinLegacyPrefs() stringForKey:kPrefLibRawPath];

    NSImage* appIcon = loadEmbeddedPng(heraw_brand::kAppIconPng, sizeof(heraw_brand::kAppIconPng), NSMakeSize(256, 256));
    if (appIcon) [NSApp setApplicationIconImage:appIcon];

    __weak AppDelegate* weakSelf = self;
    NijiLbsApplication* nijiApp = (NijiLbsApplication*)NSApp;
    nijiApp.onCmdO = ^{ [weakSelf onOpenMixedPanel:nil]; };
    nijiApp.onDeleteGroup = ^{ [weakSelf onRemoveSelectedGroup:nil]; };

    NSRect initRect = NSMakeRect(0, 0, 1280, 800);
    self.window = [[DropWindow alloc] initWithContentRect:initRect
        styleMask:(NSWindowStyleMaskTitled | NSWindowStyleMaskClosable | NSWindowStyleMaskMiniaturizable | NSWindowStyleMaskResizable)
        backing:NSBackingStoreBuffered defer:NO];
    self.window.title = @"NijiN.LiteBurstSuite (nijinLBS)";
    self.window.contentMinSize = NSMakeSize(1140, 740);
    self.window.appearance = [NSAppearance appearanceNamed:NSAppearanceNameAqua];
    self.window.delegate = self;
    [self.window registerForDraggedTypes:@[NSPasteboardTypeFileURL]];
    self.window.onDropPaths = ^(NSArray<NSString*>* paths) { [weakSelf addInputPaths:paths]; };

    self.canvas = [[StudioCanvasView alloc] initWithFrame:initRect];
    self.canvas.autoresizingMask = NSViewWidthSizable | NSViewHeightSizable;
    self.canvas.gxLogo = loadEmbeddedPng(heraw_brand::kGxStarPng2x, sizeof(heraw_brand::kGxStarPng2x),
                                         NSMakeSize(heraw_brand::kGx1xW, heraw_brand::kGx1xH));
    self.canvas.nijinLockup = loadEmbeddedPng(heraw_brand::kNijinLockupPng2x, sizeof(heraw_brand::kNijinLockupPng2x),
                                              NSMakeSize(heraw_brand::kNijin1xW, heraw_brand::kNijin1xH));
    self.canvas.onResizeBounds = ^(NSRect b) { [weakSelf relayoutForBounds:b]; };
    self.window.contentView = self.canvas;

    NSColor* textPrimary = [NSColor colorWithSRGBRed:0.13 green:0.21 blue:0.25 alpha:1.0];
    NSColor* textMuted   = [NSColor colorWithSRGBRed:0.38 green:0.49 blue:0.54 alpha:1.0];

    auto makeLabel = ^(NSView* parent, NSString* t, CGFloat sz, BOOL bold, NSColor* col) {
        NSTextField* f = [NSTextField labelWithString:(t ?: @"")];
        f.font = bold ? [NSFont boldSystemFontOfSize:sz] : [NSFont systemFontOfSize:sz];
        f.textColor = col ?: textPrimary;
        f.lineBreakMode = NSLineBreakByTruncatingTail;
        [parent addSubview:f];
        return f;
    };
    auto makeEdit = ^(NSView* parent, NSString* val, NSString* ph) {
        NSTextField* f = [[NSTextField alloc] initWithFrame:NSMakeRect(0, 0, 60, 22)];
        f.stringValue = val ?: @"";
        if (ph) f.placeholderString = ph;
        f.font = [NSFont monospacedDigitSystemFontOfSize:11.5 weight:NSFontWeightRegular];
        f.delegate = self;
        [parent addSubview:f];
        return f;
    };
    auto makeButton = ^(NSView* parent, NSString* t, SEL act) {
        NSButton* b = [NSButton buttonWithTitle:t target:self action:act];
        b.font = [NSFont systemFontOfSize:12.0];
        [parent addSubview:b];
        return b;
    };
    auto makeCheck = ^(NSView* parent, NSString* t, BOOL on, SEL act) {
        NSButton* b = [NSButton checkboxWithTitle:t target:self action:act];
        b.font = [NSFont systemFontOfSize:12.0];
        b.state = on ? NSControlStateValueOn : NSControlStateValueOff;
        [parent addSubview:b];
        return b;
    };
    auto makeSlider = ^(NSView* parent, double mn, double mx, double val) {
        NSSlider* s = [NSSlider sliderWithValue:val minValue:mn maxValue:mx target:self action:@selector(onSliderMoved:)];
        s.continuous = YES;
        [parent addSubview:s];
        return s;
    };

    // --- Left Dock Construction ---
    self.lblQueueHeader = makeLabel(self.canvas, @"连拍队列", 13.5, YES, nil);
    self.chkSortTime    = makeCheck(self.canvas, @"EXIF时间排序", YES, @selector(onScanOptChanged:));
    self.chkRecursive   = makeCheck(self.canvas, @"递归子目录",   YES, @selector(onScanOptChanged:));

    self.emptyWell = [[EmptyDropWellView alloc] initWithFrame:NSZeroRect];
    self.emptyWell.onClickWell = ^{ [weakSelf onOpenMixedPanel:nil]; };
    self.emptyWell.onDropPaths = ^(NSArray<NSString*>* p) { [weakSelf addInputPaths:p]; };
    [self.canvas addSubview:self.emptyWell];

    self.svQueue = [[NSScrollView alloc] initWithFrame:NSZeroRect];
    self.svQueue.hasVerticalScroller = YES;
    self.svQueue.drawsBackground = NO;
    self.svQueue.borderType = NSNoBorder;
    self.tblQueue = [[BurstQueueTableView alloc] initWithFrame:NSMakeRect(0, 0, 420, 200)];
    self.tblQueue.rowHeight = 54.0;
    self.tblQueue.backgroundColor = [NSColor clearColor];
    self.tblQueue.headerView = nil;
    self.tblQueue.onDeletePressed = ^{ [weakSelf onRemoveSelectedGroup:nil]; };
    NSTableColumn* qCol = [[NSTableColumn alloc] initWithIdentifier:@"BurstCol"];
    qCol.width = 410;
    [self.tblQueue addTableColumn:qCol];
    self.tblQueue.dataSource = self;
    self.tblQueue.delegate = self;
    self.svQueue.documentView = self.tblQueue;
    self.svQueue.hidden = YES;
    [self.canvas addSubview:self.svQueue];

    self.btnAddFiles   = makeButton(self.canvas, @"+ 添加文件",     @selector(onAddFiles:));
    self.btnAddFolder  = makeButton(self.canvas, @"+ 添加目录",     @selector(onAddFolder:));
    self.btnExportTopK = makeButton(self.canvas, @"导出最锐Top-K",  @selector(onExportTopK:));
    self.btnClearQueue = makeButton(self.canvas, @"清空",           @selector(onClearQueue:));

    self.lblInspectorHeader = makeLabel(self.canvas, @"组内单帧检视", 13.0, YES, nil);
    self.lblInspectorHint   = makeLabel(self.canvas, @"AF对焦点锐度 · 点 ● 锁定定格锚点", 11.0, NO, textMuted);
    self.lblInspectorHint.alignment = NSTextAlignmentRight;

    self.lblInspectorEmpty = makeLabel(self.canvas, @"选择连拍组后在此内联检视逐帧 AF 锐度、位移与微震标记", 11.5, NO, textMuted);
    self.lblInspectorEmpty.alignment = NSTextAlignmentCenter;

    self.svFrames = [[NSScrollView alloc] initWithFrame:NSZeroRect];
    self.svFrames.hasVerticalScroller = YES;
    self.svFrames.drawsBackground = NO;
    self.svFrames.borderType = NSNoBorder;
    self.tblFrames = [[NSTableView alloc] initWithFrame:NSMakeRect(0, 0, 420, 180)];
    self.tblFrames.rowHeight = 28.0;
    self.tblFrames.backgroundColor = [NSColor clearColor];
    self.tblFrames.headerView = nil;
    self.tblFrames.selectionHighlightStyle = NSTableViewSelectionHighlightStyleNone;
    NSTableColumn* fCol = [[NSTableColumn alloc] initWithIdentifier:@"FrameCol"];
    fCol.width = 410;
    [self.tblFrames addTableColumn:fCol];
    self.tblFrames.dataSource = self;
    self.tblFrames.delegate = self;
    self.svFrames.documentView = self.tblFrames;
    self.svFrames.hidden = YES;
    [self.canvas addSubview:self.svFrames];

    // --- Right 4-Tab Bar & Pages ---
    self.tabBar = [[SuiteTabBarView alloc] initWithFrame:NSZeroRect];
    self.tabBar.onSelectTab = ^(NSInteger idx) { [weakSelf selectTab:idx]; };
    [self.canvas addSubview:self.tabBar];

    self.page0 = [[TabPageCanvasView alloc] initWithFrame:NSZeroRect];
    self.page1 = [[TabPageCanvasView alloc] initWithFrame:NSZeroRect];
    self.page2 = [[TabPageCanvasView alloc] initWithFrame:NSZeroRect];
    self.page3 = [[TabPageCanvasView alloc] initWithFrame:NSZeroRect];
    self.page1.hidden = YES;
    self.page2.hidden = YES;
    self.page3.hidden = YES;
    [self.canvas addSubview:self.page0];
    [self.canvas addSubview:self.page1];
    [self.canvas addSubview:self.page2];
    [self.canvas addSubview:self.page3];

    // ================== Build Tab 1 (`page0`) ==================
    self.lblT1Card1Title = makeLabel(self.page0, @"场景与时间模式", 13.0, YES, nil);
    self.btnPortalQuad = [[PortalPillButton alloc] initWithFrame:NSZeroRect];
    self.btnPortalQuad.title = @"四象限 --/4 · 体检 ▸";
    self.btnPortalQuad.target = self;
    self.btnPortalQuad.action = @selector(onPortalToTab4:);
    [self.page0 addSubview:self.btnPortalQuad];

    self.lblPresetCap = makeLabel(self.page0, @"预设模式", 12.0, NO, nil);
    self.segPreset = [NSSegmentedControl segmentedControlWithLabels:@[@"照片", @"星空", @"科学", @"归档"]
                                                       trackingMode:NSSegmentSwitchTrackingSelectOne
                                                             target:self action:@selector(onPresetSegChanged:)];
    self.segPreset.selectedSegment = 0;
    [self.page0 addSubview:self.segPreset];

    self.lblMotionCap = makeLabel(self.page0, @"动体处理", 12.0, NO, nil);
    self.segMotionTab1 = [NSSegmentedControl segmentedControlWithLabels:@[@"自动最锐", @"首帧", @"中间", @"空镜(去人)", @"慢门(流水)"]
                                                           trackingMode:NSSegmentSwitchTrackingSelectOne
                                                                 target:self action:@selector(onMotionTab1Changed:)];
    self.segMotionTab1.selectedSegment = 0;
    [self.page0 addSubview:self.segMotionTab1];

    self.btnPortalStrobe = [[PortalPillButton alloc] initWithFrame:NSZeroRect];
    self.btnPortalStrobe.title = @"频闪/光场 ▸";
    self.btnPortalStrobe.target = self;
    self.btnPortalStrobe.action = @selector(onPortalToTab2:);
    [self.page0 addSubview:self.btnPortalStrobe];

    self.lblOlpfCap = makeLabel(self.page0, @"光学匹配", 12.0, NO, nil);
    self.popOlpf = [[NSPopUpButton alloc] initWithFrame:NSZeroRect pullsDown:NO];
    [self.popOlpf addItemsWithTitles:@[
        @"自动识别传感器低通",
        @"无低通椭圆核",
        @"4点双折射弱低通核",
        @"16点高斯低通核"
    ]];
    self.popOlpf.target = self;
    self.popOlpf.action = @selector(onGenericOptionChanged:);
    [self.page0 addSubview:self.popOlpf];

    // Card 2
    self.lblT1Card2Title = makeLabel(self.page0, @"输出与快捷附加层", 13.0, YES, nil);
    self.lblT1Card2Sub   = makeLabel(self.page0, @"与专页标签双向实时同步", 11.0, NO, textMuted);

    self.lblOutDirCap = makeLabel(self.page0, @"输出目录", 12.0, NO, nil);
    self.edtOutDir    = makeEdit(self.page0, @"", @"默认同目录 nijinLBS_out");
    self.edtOutDir.font = [NSFont systemFontOfSize:12.0];
    self.btnBrowseOut = makeButton(self.page0, @"浏览", @selector(onBrowseOutDir:));
    self.btnClearOut  = makeButton(self.page0, @"×",    @selector(onClearOutDir:));

    self.chkEmitRaw = makeCheck(self.page0, @"2× RGGB 底片", YES, @selector(onGenericOptionChanged:));
    self.popRawFmt  = [[NSPopUpButton alloc] initWithFrame:NSZeroRect pullsDown:NO];
    [self.popRawFmt addItemsWithTitles:@[
        @"DNG 16-bit 无损",
        @"DNG 三通道线性 (Capture One 兼容)",
        @"DNG 16-bit 半精度浮点 (0.29×)",
        @"DNG 高级有损压缩 (0.09×)",
        @"DNG 16-bit 未压缩 (1.00×)"
    ]];
    self.popRawFmt.target = self;
    self.popRawFmt.action = @selector(onGenericOptionChanged:);
    [self.page0 addSubview:self.popRawFmt];
    self.chkRelayRaw = makeCheck(self.page0, @"外部降噪直通", NO, @selector(onGenericOptionChanged:));

    self.chkEmitRgb = makeCheck(self.page0, @"2× RGB 成片", YES, @selector(onGenericOptionChanged:));
    self.popRgbFmt  = [[NSPopUpButton alloc] initWithFrame:NSZeroRect pullsDown:NO];
    [self.popRgbFmt addItemsWithTitles:@[
        @"16-bit 无损 TIFF",
        @"16-bit 无损 PNG (0.40×)",
        @"14-bit JPEG XS 有损 (0.21×)",
        @"14-bit JPEG XS 无损 (0.43×)",
        @"16-bit 未压缩 TIFF (1.00×)"
    ]];
    self.popRgbFmt.target = self;
    self.popRgbFmt.action = @selector(onGenericOptionChanged:);
    [self.page0 addSubview:self.popRgbFmt];

    self.lblBppCap = makeLabel(self.page0, @"BPP", 11.5, NO, nil);
    self.edtJxsBpp = makeEdit(self.page0, @"10.0", nil);
    self.segColorSpace = [NSSegmentedControl segmentedControlWithLabels:@[@"sRGB", @"Adobe RGB"]
                                                           trackingMode:NSSegmentSwitchTrackingSelectOne
                                                                 target:self action:@selector(onGenericOptionChanged:)];
    self.segColorSpace.selectedSegment = 0;
    [self.page0 addSubview:self.segColorSpace];

    self.chkEmitField = makeCheck(self.page0, @"物理场图", NO, @selector(onGenericOptionChanged:));
    self.popFieldMode = [[NSPopUpButton alloc] initWithFrame:NSZeroRect pullsDown:NO];
    [self.popFieldMode addItemsWithTitles:@[
        @"内嵌至主TIFF通道",
        @"独立16-bit灰度PNG",
        @"合并三通道PNG"
    ]];
    self.popFieldMode.target = self;
    self.popFieldMode.action = @selector(onGenericOptionChanged:);
    [self.popFieldMode selectItemAtIndex:1];
    [self.page0 addSubview:self.popFieldMode];

    self.btnPortalFields = [[PortalPillButton alloc] initWithFrame:NSZeroRect];
    self.btnPortalFields.title = @"选场(3/11)·数据集 ▸";
    self.btnPortalFields.target = self;
    self.btnPortalFields.action = @selector(onPortalToTab3:);
    [self.page0 addSubview:self.btnPortalFields];

    self.chkEmitTransientTab1 = makeCheck(self.page0, @"瞬态高光回收层 (流星 / 萤火虫 / 浪花)", NO, @selector(onTransientTab1Changed:));
    self.btnPortalRefl = [[PortalPillButton alloc] initWithFrame:NSZeroRect];
    self.btnPortalRefl.title = @"消反光/阈值 ▸";
    self.btnPortalRefl.target = self;
    self.btnPortalRefl.action = @selector(onPortalToTab2:);
    [self.page0 addSubview:self.btnPortalRefl];

    // Card 3
    self.lblT1Card3Title = makeLabel(self.page0, @"重建与新噪声态精调", 13.0, YES, nil);
    self.btnResetPreset  = makeButton(self.page0, @"恢复预设", @selector(onResetPreset:));
    self.btnResetPreset.controlSize = NSControlSizeSmall;
    self.btnResetPreset.font = [NSFont systemFontOfSize:11.5];

    self.lblDenoise  = makeLabel(self.page0, @"亮度互谱降噪", 11.5, NO, nil);
    self.sldDenoise  = makeSlider(self.page0, 0.0, 2.5, 1.0);
    self.edtDenoise  = makeEdit(self.page0, @"1.00", nil);

    self.lblChroma   = makeLabel(self.page0, @"色空KLT强度", 11.5, NO, nil);
    self.sldChroma   = makeSlider(self.page0, 0.0, 2.5, 1.0);
    self.edtChroma   = makeEdit(self.page0, @"1.00", nil);

    self.lblDeghost  = makeLabel(self.page0, @"去鬼影严格度", 11.5, NO, nil);
    self.sldDeghost  = makeSlider(self.page0, 0.4, 3.0, 1.0);
    self.edtDeghost  = makeEdit(self.page0, @"1.00", nil);

    self.lblPsfIters = makeLabel(self.page0, @"光学RL迭代", 11.5, NO, nil);
    self.sldPsfIters = makeSlider(self.page0, 0.0, 12.0, 4.0);
    self.edtPsfIters = makeEdit(self.page0, @"4", nil);

    self.lblPsfSigma = makeLabel(self.page0, @"PSF半径", 11.5, NO, nil);
    self.sldPsfSigma = makeSlider(self.page0, 0.35, 3.0, 1.05);
    self.edtPsfSigma = makeEdit(self.page0, @"1.05", nil);

    self.lblEv       = makeLabel(self.page0, @"手动曝光EV", 11.5, NO, nil);
    self.sldEv       = makeSlider(self.page0, -4.0, 4.0, 0.0);
    self.edtEv       = makeEdit(self.page0, @"0.0", nil);

    self.chkSiliconClean = makeCheck(self.page0, @"硅片条带自扣除", YES, @selector(onGenericOptionChanged:));
    self.chkIbp          = makeCheck(self.page0, @"紧支集IBP回投", YES, @selector(onGenericOptionChanged:));
    self.chkAutoLca      = makeCheck(self.page0, @"盲色差归位",     YES, @selector(onGenericOptionChanged:));
    self.chkAutoExp      = makeCheck(self.page0, @"自动曝光",       YES, @selector(onGenericOptionChanged:));
    self.chkRolloff      = makeCheck(self.page0, @"高光滚降",       YES, @selector(onGenericOptionChanged:));
    self.chkDither       = makeCheck(self.page0, @"TPDF抖动",       YES, @selector(onGenericOptionChanged:));

    self.lblBackendCap = makeLabel(self.page0, @"后端", 11.5, NO, nil);
    self.popBackend = [[NSPopUpButton alloc] initWithFrame:NSZeroRect pullsDown:NO];
    [self.popBackend addItemsWithTitles:@[@"自动(GPU优先)", @"强制GPU", @"纯CPU"]];
    self.popBackend.target = self;
    self.popBackend.action = @selector(onGenericOptionChanged:);
    [self.page0 addSubview:self.popBackend];

    self.lblGrpSzCap   = makeLabel(self.page0, @"每组上限", 11.5, NO, nil);
    self.edtGrpSz      = makeEdit(self.page0, @"8", nil);
    self.lblThreadsCap = makeLabel(self.page0, @"线程", 11.5, NO, nil);
    self.edtThreads    = makeEdit(self.page0, @"0", nil);

    self.lblEngineStrip = makeLabel(self.page0, @"引擎: 内置 LibRaw 0.23-HE*", 11.0, NO, textMuted);
    self.btnEngineSheet = makeButton(self.page0, @"更换引擎/LibRaw...", @selector(onOpenEngineSheet:));
    self.btnEngineSheet.controlSize = NSControlSizeSmall;
    self.btnEngineSheet.font = [NSFont systemFontOfSize:11.0];

    // ================== Build Tab 2 (`page1`) ==================
    self.lblT2Card1Title = makeLabel(self.page1, @"时间轴与动体合成模式 (与首页「动体处理」双向同步)", 13.0, YES, nil);

    self.radT2Freeze = [NSButton radioButtonWithTitle:@"标准姿态定格" target:self action:@selector(onTab2MotionRadio:)];
    self.radT2Freeze.font = [NSFont systemFontOfSize:12.0 weight:NSFontWeightMedium];
    [self.page1 addSubview:self.radT2Freeze];

    self.segT2FreezePose = [NSSegmentedControl segmentedControlWithLabels:@[@"自动最锐(AF)", @"首帧", @"中间帧", @"跟随左栏指定帧"]
                                                             trackingMode:NSSegmentSwitchTrackingSelectOne
                                                                   target:self action:@selector(onTab2FreezePoseSeg:)];
    self.segT2FreezePose.selectedSegment = 0;
    [self.page1 addSubview:self.segT2FreezePose];

    self.radT2Clean = [NSButton radioButtonWithTitle:@"零路人纯净空镜 (Clean Plate)" target:self action:@selector(onTab2MotionRadio:)];
    self.radT2Clean.font = [NSFont systemFontOfSize:12.0 weight:NSFontWeightMedium];
    [self.page1 addSubview:self.radT2Clean];
    self.lblCleanSens = makeLabel(self.page1, @"行人剔除灵敏度", 11.5, NO, textMuted);
    self.sldCleanSens = makeSlider(self.page1, 0.30, 2.50, 1.00);
    self.edtCleanSens = makeEdit(self.page1, @"1.00", nil);

    self.radT2Vnd = [NSButton radioButtonWithTitle:@"手持虚拟慢门流水 (Virtual ND)" target:self action:@selector(onTab2MotionRadio:)];
    self.radT2Vnd.font = [NSFont systemFontOfSize:12.0 weight:NSFontWeightMedium];
    [self.page1 addSubview:self.radT2Vnd];
    self.lblVndThresh = makeLabel(self.page1, @"流体识别阈值", 11.5, NO, textMuted);
    self.sldVndThresh = makeSlider(self.page1, 0.15, 1.50, 0.65);
    self.edtVndThresh = makeEdit(self.page1, @"0.65", nil);

    self.radT2Strobe = [NSButton radioButtonWithTitle:@"运动频闪分身轨迹 (Strobe)" target:self action:@selector(onTab2MotionRadio:)];
    self.radT2Strobe.font = [NSFont systemFontOfSize:12.0 weight:NSFontWeightMedium];
    [self.page1 addSubview:self.radT2Strobe];
    self.lblStrobeStep = makeLabel(self.page1, @"抽帧步长", 11.5, NO, textMuted);
    self.popStrobeStep = [[NSPopUpButton alloc] initWithFrame:NSZeroRect pullsDown:NO];
    [self.popStrobeStep addItemsWithTitles:@[@"每1帧", @"每2帧", @"每3帧", @"每4帧", @"每5帧", @"每6帧"]];
    [self.popStrobeStep selectItemAtIndex:1];
    self.popStrobeStep.target = self;
    self.popStrobeStep.action = @selector(onGenericOptionChanged:);
    [self.page1 addSubview:self.popStrobeStep];

    self.segStrobeFade = [NSSegmentedControl segmentedControlWithLabels:@[@"等权实影", @"渐隐残影"]
                                                           trackingMode:NSSegmentSwitchTrackingSelectOne
                                                                 target:self action:@selector(onGenericOptionChanged:)];
    self.segStrobeFade.selectedSegment = 0;
    [self.page1 addSubview:self.segStrobeFade];

    // Tab 2 Card 2
    self.lblT2Card2Title      = makeLabel(self.page1, @"附加分离图层提取 (可与主图同步导出)", 13.0, YES, nil);
    self.chkEmitTransientTab2 = makeCheck(self.page1, @"剥离瞬态高光回收层 (流星 / 萤火虫 / 飞溅浪花)", NO, @selector(onTransientTab2Changed:));
    self.lblTransSigma        = makeLabel(self.page1, @"触发门限 (σ)", 11.5, NO, textMuted);
    self.sldTransSigma        = makeSlider(self.page1, 2.0, 8.0, 3.5);
    self.edtTransSigma        = makeEdit(self.page1, @"3.5", nil);
    self.segTransColor = [NSSegmentedControl segmentedControlWithLabels:@[@"全彩层", @"灰度层"]
                                                           trackingMode:NSSegmentSwitchTrackingSelectOne
                                                                 target:self action:@selector(onGenericOptionChanged:)];
    self.segTransColor.selectedSegment = 0;
    [self.page1 addSubview:self.segTransColor];

    self.chkReflection = makeCheck(self.page1, @"镜面碎波光压制 / 展柜消反光分离", NO, @selector(onGenericOptionChanged:));
    self.segReflectionMode = [NSSegmentedControl segmentedControlWithLabels:@[@"原地碎高光压制", @"宽基线双层消反光"]
                                                               trackingMode:NSSegmentSwitchTrackingSelectOne
                                                                     target:self action:@selector(onGenericOptionChanged:)];
    self.segReflectionMode.selectedSegment = 0;
    [self.page1 addSubview:self.segReflectionMode];
    self.lblDriftReadout = makeLabel(self.page1, @"", 11.0, NO, textMuted);

    // Tab 2 Card 3
    self.lblT2Card3Title = makeLabel(self.page1, @"三维微基线光场与线性光子域散景重对焦", 13.0, YES, nil);
    self.parallaxGateView = [[ParallaxGateView alloc] initWithFrame:NSZeroRect];
    [self.page1 addSubview:self.parallaxGateView];

    self.chkExportParallax = makeCheck(self.page1, @"导出 2.5D 交互光场包 (.html)", NO, @selector(onGenericOptionChanged:));
    self.lblParallaxAmp    = makeLabel(self.page1, @"视角环绕幅度", 11.5, NO, textMuted);
    self.sldParallaxAmp    = makeSlider(self.page1, 0.20, 2.50, 1.00);
    self.edtParallaxAmp    = makeEdit(self.page1, @"1.00", nil);

    self.chkExportRefocus  = makeCheck(self.page1, @"额外渲染线性光子域散景重对焦图", NO, @selector(onGenericOptionChanged:));
    self.lblRefocusPos     = makeLabel(self.page1, @"合焦平面偏移", 11.5, NO, textMuted);
    self.sldRefocusPos     = makeSlider(self.page1, -1.00, 1.00, 0.00);
    self.edtRefocusPos     = makeEdit(self.page1, @"0.00", nil);
    self.lblBokehStr       = makeLabel(self.page1, @"散景光圈强度", 11.5, NO, textMuted);
    self.sldBokehStr       = makeSlider(self.page1, 0.00, 3.00, 1.00);
    self.edtBokehStr       = makeEdit(self.page1, @"1.00", nil);

    // ================== Build Tab 3 (`page2`) ==================
    self.lblT3Card1Title = makeLabel(self.page2, @"物理场封装载体与按需提取矩阵", 13.0, YES, nil);
    self.lblCarrierCap   = makeLabel(self.page2, @"导出方式", 12.0, NO, nil);
    self.segCarrierTab3  = [NSSegmentedControl segmentedControlWithLabels:@[
        @"不导出", @"内嵌至主TIFF命名通道", @"独立16-bit灰度PNG", @"合并RGB"
    ] trackingMode:NSSegmentSwitchTrackingSelectOne target:self action:@selector(onCarrierTab3Changed:)];
    self.segCarrierTab3.selectedSegment = 0;
    [self.page2 addSubview:self.segCarrierTab3];

    self.lblMatrixHeader = makeLabel(self.page2, @"可提取物理场矩阵 (惰性求值：未勾选场零计算开销)", 12.0, YES, nil);
    self.btnFieldsCore3  = makeButton(self.page2, @"常用三项", @selector(onFieldsCore3:));
    self.btnFieldsAll    = makeButton(self.page2, @"全选",     @selector(onFieldsSelectAll:));

    for (int i = 0; i < 11; ++i) {
        BOOL defOn = (i < 3);
        NSButton* cb = makeCheck(self.page2, kFieldSpecs[i].title, defOn, @selector(onGenericOptionChanged:));
        cb.font = [NSFont systemFontOfSize:11.5];
        [self.chkFields addObject:cb];
    }
    self.lblFieldsGateNote = makeLabel(self.page2, @"(* 号项依视差/焦段/快门实测物理门禁自动判定)", 10.5, NO, textMuted);

    self.lblT3Card2Title = makeLabel(self.page2, @"Sci 机身专属自监督真值数据集工厂", 13.0, YES, nil);
    self.chkExportTiles  = makeCheck(self.page2, @"同步切出配对训练样本块 (1× 原始带噪 RAW 块 ↔ 2× 无偏真值 + σ_map)", NO, @selector(onGenericOptionChanged:));
    self.lblTileSizeCap  = makeLabel(self.page2, @"切片尺寸", 12.0, NO, nil);
    self.segTileSize = [NSSegmentedControl segmentedControlWithLabels:@[@"256×256", @"512×512"]
                                                         trackingMode:NSSegmentSwitchTrackingSelectOne
                                                               target:self action:@selector(onGenericOptionChanged:)];
    self.segTileSize.selectedSegment = 0;
    [self.page2 addSubview:self.segTileSize];
    self.lblTileConfCap = makeLabel(self.page2, @"静态相位置信度门槛", 12.0, NO, nil);
    self.sldTileConf    = makeSlider(self.page2, 0.50, 0.99, 0.92);
    self.edtTileConf    = makeEdit(self.page2, @"0.92", nil);

    // ================== Build Tab 4 (`page3`) ==================
    self.trajCanvas  = [[TrajectoryCanvasView alloc] initWithFrame:NSZeroRect];
    self.radarCanvas = [[OpticsRadarView alloc] initWithFrame:NSZeroRect];
    self.radarCanvas.telemetry = self.opticsModel;
    [self.page3 addSubview:self.trajCanvas];
    [self.page3 addSubview:self.radarCanvas];

    self.btnRunOptics = [[AquaButton alloc] initWithFrame:NSZeroRect];
    self.btnRunOptics.compactFont = YES;
    self.btnRunOptics.title = @"运行原始 RAW 8× 光学精测";
    self.btnRunOptics.bordered = NO;
    self.btnRunOptics.target = self;
    self.btnRunOptics.action = @selector(onRunOpticsReport:);
    [self.page3 addSubview:self.btnRunOptics];

    self.lblOisStatus = makeLabel(self.page3, @"OIS状态: 自动取跨帧 MTF 上包络剥离动态偏心", 11.5, NO, textMuted);

    self.lblT4SiliconTitle = makeLabel(self.page3, @"硅片电子学实测读数", 12.5, YES, nil);
    self.lblSiliconReadout = makeLabel(self.page3, @"", 11.5, NO, textPrimary);
    self.lblSiliconReadout.font = [NSFont monospacedDigitSystemFontOfSize:11.5 weight:NSFontWeightRegular];

    self.lblT4ExportTitle  = makeLabel(self.page3, @"校准档与光学体检报告导出", 12.5, YES, nil);
    self.lblLcpItemsCap    = makeLabel(self.page3, @".lcp 包含项:", 11.5, NO, textMuted);
    self.chkLcpLca         = makeCheck(self.page3, @"倍率色差 (LCA)", YES, @selector(onGenericOptionChanged:));
    self.chkLcpVignette    = makeCheck(self.page3, @"真实暗角 (需漂移>=50px)", NO, @selector(onGenericOptionChanged:));
    self.chkLcpDistortion  = makeCheck(self.page3, @"径向畸变 (需漂移>=50px)", NO, @selector(onGenericOptionChanged:));

    self.btnExportLcp        = makeButton(self.page3, @"导出标准 Adobe .lcp 校正档", @selector(onExportLcp:));
    self.btnExportOpticsPng  = makeButton(self.page3, @"导出光学体检图卡 (.png)",    @selector(onExportOpticsPng:));
    self.btnExportOpticsJson = makeButton(self.page3, @"导出完整遥测 (.json)",       @selector(onExportOpticsJson:));

    // --- Bottom Global Action Bar ---
    self.btnRun = [[AquaButton alloc] initWithFrame:NSZeroRect];
    self.btnRun.title = @"开始执行";
    self.btnRun.bordered = NO;
    self.btnRun.keyEquivalent = @"\r";
    self.btnRun.target = self;
    self.btnRun.action = @selector(onRunOrStop:);
    [self.canvas addSubview:self.btnRun];

    self.lblStageStatus = makeLabel(self.canvas, @"就绪 · 拖入 RAW 连拍或文件夹即可静默分拣", 11.5, NO, textPrimary);
    self.lblPct = makeLabel(self.canvas, @"0%", 11.5, YES, [NSColor colorWithSRGBRed:0.22 green:0.44 blue:0.52 alpha:1.0]);
    self.lblPct.alignment = NSTextAlignmentRight;

    self.progView = [[AquaProgressView alloc] initWithFrame:NSZeroRect];
    [self.canvas addSubview:self.progView];

    self.btnOpenOutDir = makeButton(self.canvas, @"打开输出目录", @selector(onOpenOutDir:));

    // Apply initial defaults & layout
    [self applyPresetIndex:0];
    [self relayoutForBounds:self.canvas.bounds];
    [self updateQueueEmptyVisibility];

    // Parse Process Arguments
    NSArray<NSString*>* procArgs = [[NSProcessInfo processInfo] arguments];
    NSString* shotPath = nil;
    BOOL testCmdQ = NO;
    BOOL testPrefs = NO;
    BOOL testProcesses = NO;
    NSString* testOptics = nil;
    NSInteger cliTab = -1;
    NSMutableArray<NSString*>* initPaths = [NSMutableArray array];

    for (NSUInteger i = 1; i < procArgs.count; ++i) {
        NSString* a = procArgs[i];
        if ([a isEqualToString:@"--screenshot"] && i + 1 < procArgs.count) {
            shotPath = procArgs[++i];
        } else if ([a isEqualToString:@"--tab"] && i + 1 < procArgs.count) {
            cliTab = [procArgs[++i] integerValue];
        } else if ([a isEqualToString:@"--test-cmd-q"]) {
            testCmdQ = YES;
        } else if ([a isEqualToString:@"--test-prefs"]) {
            testPrefs = YES;
        } else if ([a isEqualToString:@"--test-processes"]) {
            testProcesses = YES;
        } else if ([a isEqualToString:@"--test-optics"] && i + 1 < procArgs.count) {
            testOptics = procArgs[++i];
        } else if ([a isEqualToString:@"--optics-json"] && i + 1 < procArgs.count) {
            self.screenshotOpticsPath = procArgs[++i];
        } else if ([a hasPrefix:@"--tab"] && a.length == 6) {
            cliTab = [[a substringFromIndex:5] integerValue];
        } else if (![a hasPrefix:@"-"]) {
            [initPaths addObject:a];
        }
    }

    self.isScreenshotMode = (shotPath != nil || testCmdQ || testProcesses || testOptics != nil);
    if (self.isScreenshotMode) {
        self.customCliPath = nil;
        self.customLibRawPath = nil;
    }
    if (!self.isScreenshotMode && !testPrefs && !testProcesses && !testOptics) {
        [self loadUiPrefs];
    }

    if (testPrefs) {
        [self runPrefsSelfTestAndExit];
        return;
    }
    if (testProcesses) {
        [self runProcessSelfTestAndExit];
        return;
    }
    if (testOptics) {
        [self runOpticsSelfTestAndExit:testOptics];
        return;
    }

    if (cliTab >= 0 && cliTab <= 3) {
        [self selectTab:cliTab];
    }

    [self updateEngineStatusStrip];
    [self syncAllUiState];

    if (initPaths.count > 0) {
        [self addInputPaths:initPaths];
    }

    if (self.isScreenshotMode || ![nijinPrefs() objectForKey:@"WindowFrame"]) {
        [self.window center];
    }
    [self.window makeKeyAndOrderFront:nil];
    [NSApp activateIgnoringOtherApps:YES];

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
        NSInteger waitMs = (initPaths.count > 0) ? 700 : 240;
        dispatch_after(dispatch_time(DISPATCH_TIME_NOW, (int64_t)(waitMs * NSEC_PER_MSEC)), dispatch_get_main_queue(), ^{
            [self captureScreenshot:shotPath attempts:300];
        });
    }
}

- (void)captureScreenshot:(NSString*)path attempts:(NSInteger)attempts {
    if (self.planTask) {
        if (attempts == 0) std::exit(2);
        dispatch_after(dispatch_time(DISPATCH_TIME_NOW, 100 * NSEC_PER_MSEC), dispatch_get_main_queue(), ^{
            [self captureScreenshot:path attempts:attempts - 1];
        });
        return;
    }
    if (self.screenshotOpticsPath) {
        NSString* prefix = [self.screenshotOpticsPath stringByReplacingOccurrencesOfString:@"_telemetry.json" withString:@""];
        [self parseOpticsTelemetryFile:self.screenshotOpticsPath cardPng:[prefix stringByAppendingString:@"_passport.png"]];
        self.screenshotOpticsPath = nil;
    }
    NSView* view = self.window.contentView.superview ?: self.window.contentView;
    [view layoutSubtreeIfNeeded];
    NSBitmapImageRep* rep = [view bitmapImageRepForCachingDisplayInRect:view.bounds];
    [view cacheDisplayInRect:view.bounds toBitmapImageRep:rep];
    NSData* png = [rep representationUsingType:NSBitmapImageFileTypePNG properties:@{}];
    if (![png writeToFile:path atomically:YES]) std::exit(1);
    [NSApp terminate:nil];
}

- (void)runOpticsSelfTestAndExit:(NSString*)path {
    self.suppressPrefSave = YES;
    NSDictionary* root = [NSJSONSerialization JSONObjectWithData:[NSData dataWithContentsOfFile:path] options:0 error:nil];
    if (![root isKindOfClass:[NSDictionary class]]) std::exit(1);
    BurstGroupModel* group = [[BurstGroupModel alloc] init];
    for (NSInteger i = 0; i < [root[@"frames"] integerValue]; ++i) {
        FrameDiagItem* frame = [[FrameDiagItem alloc] init];
        frame.idx1Based = (int)i + 1; frame.fileName = @"test-frame"; frame.flags = @"";
        [group.frames addObject:frame];
    }
    [self.burstGroups addObject:group];
    [self.tblQueue reloadData];
    [self.tblQueue selectRowIndexes:[NSIndexSet indexSetWithIndex:0] byExtendingSelection:NO];
    NSString* prefix = [path stringByReplacingOccurrencesOfString:@"_telemetry.json" withString:@""];
    [self parseOpticsTelemetryFile:path cardPng:[prefix stringByAppendingString:@"_passport.png"]];
    auto m = self.opticsModel;
    NSArray* mtf = @[m.centerValid ? @(m.centerMtf) : NSNull.null, m.nwValid ? @(m.nwMtf) : NSNull.null,
        m.neValid ? @(m.neMtf) : NSNull.null, m.swValid ? @(m.swMtf) : NSNull.null, m.seValid ? @(m.seMtf) : NSNull.null];
    NSMutableArray* drift = [NSMutableArray array];
    for (FrameDiagItem* frame in group.frames) [drift addObject:@{
        @"dx": frame.hasPosition ? @(frame.dxPx) : NSNull.null,
        @"dy": frame.hasPosition ? @(frame.dyPx) : NSNull.null,
        @"time": frame.hasTime ? @(frame.timeSeconds) : NSNull.null
    }];
    NSMutableDictionary* enabled = [NSMutableDictionary dictionary];
    for (NSUInteger i = 0; i < self.chkFields.count; ++i)
        enabled[[NSString stringWithUTF8String:kFieldSpecs[i].code]] = @(self.chkFields[i].enabled);
    NSDictionary* summary = @{@"mtf50":mtf, @"phase_quadrants":group.phaseQuadrants ?: @[],
        @"drift":drift, @"lca_valid":@[@(m.redLcaValid), @(m.blueLcaValid)],
        @"passport":@(m.lastCardPngPath.length > 0), @"loaded":@(m.populated),
        @"parallax_measured":@(group.hasMeasuredParallax),
        @"max_parallax_2x_px":group.hasMeasuredParallax ? @(group.maxParallax2xPx) : NSNull.null,
        @"depth_observable":group.depthKnown ? @(group.depthObservable) : NSNull.null,
        @"field_observability":group.fieldObservability ?: @{},
        @"field_enabled":enabled,
        @"silicon":@{@"sigma0":m.sigma0 >= 0 ? @(m.sigma0) : NSNull.null,
                     @"gain_g":m.gainG >= 0 ? @(m.gainG) : NSNull.null,
                     @"sensor_temp_c":std::isfinite(m.sensorTempC) ? @(m.sensorTempC) : NSNull.null,
                     @"warm_pixels":m.warmPixels >= 0 ? @(m.warmPixels) : NSNull.null},
        @"silicon_text":self.lblSiliconReadout.stringValue,
        @"depth_enabled":@(self.chkExportParallax.enabled)};
    NSData* data = [NSJSONSerialization dataWithJSONObject:summary options:0 error:nil];
    std::printf("OPTICS_JSON:%s\n", [[NSString alloc] initWithData:data encoding:NSUTF8StringEncoding].UTF8String);
    std::fflush(stdout);
    std::exit(m.populated ? 0 : 1);
}

- (void)runPrefsSelfTestAndExit {
    NSUserDefaults* ud = nijinPrefs();
    NSUserDefaults* leg = nijinLegacyPrefs();
    NSDictionary* backupNew = [[ud persistentDomainForName:kPrefSuite] copy];
    NSDictionary* backupLeg = [[leg persistentDomainForName:kPrefSuiteLegacy] copy];

    [ud removePersistentDomainForName:kPrefSuite];
    [ud synchronize];

    // 1. Verify fresh lossless defaults
    bool defaultLosslessOk =
        (self.chkEmitRaw.state == NSControlStateValueOn) &&
        (self.popRawFmt.indexOfSelectedItem == 0) &&
        ([self.popRawFmt.titleOfSelectedItem hasPrefix:@"DNG 16-bit 无损"]) &&
        (self.chkEmitRgb.state == NSControlStateValueOn) &&
        (self.popRgbFmt.indexOfSelectedItem == 0) &&
        ([self.popRgbFmt.titleOfSelectedItem hasPrefix:@"16-bit 无损 TIFF"]) &&
        ([self selectedFieldCount] == 3) &&
        (self.popFieldMode.indexOfSelectedItem == 1) &&
        (self.tabBar.selectedTab == 0);

    // 2. Verify legacy fallback from com.nijin.liteburstscaler when new suite is empty
    [leg setBool:YES forKey:@"HasSavedUiPrefs"];
    [leg setObject:@"dng-fp16" forKey:@"RawFmtCode"];
    [leg setObject:@"png" forKey:@"RgbFmtCode"];
    [leg setInteger:3 forKey:@"PresetIdx"]; // legacy 3 = astro -> new segment 1
    [leg synchronize];

    [self loadUiPrefs];
    bool legacyFallbackOk =
        (self.popRawFmt.indexOfSelectedItem == 2) &&
        (self.popRgbFmt.indexOfSelectedItem == 1) &&
        (self.segPreset.selectedSegment == 1);

    // 3. Mutate v2.0.0 suite options (formats, tab, field matrix, creative motion, sliders) & save
    [self selectTab:2];
    [self.popRawFmt selectItemAtIndex:1]; // dng-linear
    [self.popRgbFmt selectItemAtIndex:2]; // jxs
    self.edtJxsBpp.stringValue = @"12.5";
    self.segColorSpace.selectedSegment = 1; // Adobe RGB
    self.chkEmitField.state = NSControlStateValueOn;
    [self.popFieldMode selectItemAtIndex:1]; // split
    [self applySelectedFieldsCsv:@"motion,phase,focus,depth,sigma"];
    self.motionMode = @"clean-plate";
    self.edtDenoise.stringValue = @"0.62";
    self.sldDenoise.doubleValue = 0.62;
    self.edtRefocusPos.stringValue = @"-0.25";
    self.edtBokehStr.stringValue = @"3.00";
    self.chkLcpLca.state = NSControlStateValueOff;
    self.chkLcpVignette.state = NSControlStateValueOn;
    self.chkLcpDistortion.state = NSControlStateValueOn;
    [self syncAllUiState];
    [self saveUiPrefs];

    // Reset controls to initial defaults, then reload from com.nijin.liteburstsuite
    self.suppressPrefSave = YES;
    [self selectTab:0];
    [self.popRawFmt selectItemAtIndex:0];
    [self.popRgbFmt selectItemAtIndex:0];
    self.edtJxsBpp.stringValue = @"10.0";
    self.segColorSpace.selectedSegment = 0;
    self.chkEmitField.state = NSControlStateValueOff;
    [self.popFieldMode selectItemAtIndex:0];
    [self applySelectedFieldsCsv:@"motion,phase,focus"];
    self.motionMode = @"auto";
    [self applyPresetIndex:0];
    self.edtRefocusPos.stringValue = @"0.00";
    self.edtBokehStr.stringValue = @"1.00";
    self.chkLcpLca.state = NSControlStateValueOn;
    self.chkLcpVignette.state = NSControlStateValueOff;
    self.chkLcpDistortion.state = NSControlStateValueOff;
    self.suppressPrefSave = NO;

    [self loadUiPrefs];

    bool roundtripOk =
        (self.tabBar.selectedTab == 2) &&
        (self.popRawFmt.indexOfSelectedItem == 1) &&
        (self.popRgbFmt.indexOfSelectedItem == 2) &&
        ([self.edtJxsBpp.stringValue isEqualToString:@"12.5"]) &&
        (self.segColorSpace.selectedSegment == 1) &&
        (self.chkEmitField.state == NSControlStateValueOn) &&
        (self.popFieldMode.indexOfSelectedItem == 1) &&
        ([self selectedFieldCount] == 5) &&
        ([self.motionMode isEqualToString:@"clean-plate"]) &&
        ([self.edtDenoise.stringValue isEqualToString:@"0.62"]);
    bool lcpOk = [[self selectedLcpFieldsCsv] isEqualToString:@"vignette,distortion"] &&
        [[self lcpArguments:@"/输出 镜头.lcp"] isEqualToArray:
            @[@"--optics-only", @"--export-lcp", @"/输出 镜头.lcp", @"--lcp-fields", @"vignette,distortion"]];
    bool refocusOk = self.edtRefocusPos.doubleValue == -0.25 && self.edtBokehStr.doubleValue == 3.0;

    self.suppressPrefSave = YES;
    self.chkLcpLca.state = self.chkLcpVignette.state = self.chkLcpDistortion.state = NSControlStateValueOff;
    [self syncAllUiState];
    lcpOk = lcpOk && !self.btnExportLcp.enabled && self.chkExportParallax.enabled;
    self.chkLcpLca.state = NSControlStateValueOn;
    [self syncAllUiState];
    lcpOk = lcpOk && self.btnExportLcp.enabled;
    self.chkEmitTransientTab2.state = NSControlStateValueOn;
    [self onTransientTab2Changed:self.chkEmitTransientTab2];
    bool syncOk = self.chkEmitTransientTab1.state == NSControlStateValueOn;
    self.segCarrierTab3.selectedSegment = 1;
    [self onCarrierTab3Changed:self.segCarrierTab3];
    syncOk = syncOk && self.chkEmitRgb.state == NSControlStateValueOn &&
        self.popRgbFmt.indexOfSelectedItem == 0 && self.chkFields.count == 11;
    std::string fixture = R"({"clusters" : [{"focal_len":400,"frames" : [
        {"idx":1,"path":"/raw/\u76f8\u673a\ud83d\ude00.NEF","file":"a.NEF","group":0,"dx_1x":1.5,"dy_1x":-2.0},
        {"idx":2,"path":"/raw/b.NEF","file":"b.NEF","group":0,"dx":0,"dy":0}
    ]}]})";
    NSArray<BurstGroupModel*>* parsed = [self parsePlanJsonOutput:fixture basenameMap:@{}];
    bool parserOk = parsed.count == 1 && parsed[0].frames.count == 2 &&
        [parsed[0].filePaths[0] isEqualToString:@"/raw/相机😀.NEF"] &&
        parsed[0].maxParallax2xPx == 0.0 && parsed[0].frames[0].dxPx == 1.5 && parsed[0].focalMm == 400;
    BurstGroupModel* sample = parsed.firstObject;
    sample.frames[1].enabled = NO;
    self.motionMode = @"strobe";
    self.segStrobeFade.selectedSegment = 1;
    self.chkExportTiles.state = NSControlStateValueOn;
    NSArray<NSString*>* argv = [self buildCliArgsForGroup:sample outDir:@"/输出 空格"];
    auto option = [&](NSString* key) -> NSString* {
        NSUInteger idx = [argv indexOfObject:key];
        return idx != NSNotFound && idx + 1 < argv.count ? argv[idx + 1] : @"";
    };
    bool argsOk = [option(@"--strobe-fade") isEqualToString:@"1"] &&
        [option(@"--skip-frames") isEqualToString:@"2"] &&
        [option(@"--export-dataset-tiles") isEqualToString:@"/输出 空格/dataset_tiles"] &&
        [argv containsObject:@"--no-sort-time"] && [argv containsObject:@"/raw/相机😀.NEF"];
    self.chkExportParallax.state = NSControlStateValueOn;
    self.chkExportRefocus.state = NSControlStateValueOn;
    NSArray* unknown = [self buildCliArgsForGroup:sample outDir:@"/output"];
    sample.depthKnown = YES; sample.depthObservable = NO;
    NSArray* blocked = [self buildCliArgsForGroup:sample outDir:@"/output"];
    sample.depthObservable = YES; sample.maxParallax2xPx = 0.5;
    NSArray* allowed = [self buildCliArgsForGroup:sample outDir:@"/output"];
    argsOk = argsOk && [unknown containsObject:@"--export-parallax-html"] &&
        ![blocked containsObject:@"--export-parallax-html"] && ![blocked containsObject:@"--export-refocus"] &&
        [allowed containsObject:@"--export-parallax-html"];
    const double offsets[] = {-1, 0, 1}, positions[] = {0, 0.5, 1};
    const double strengths[] = {0, 1, 3}, radii[] = {0, 12, 36};
    for (int i = 0; i < 3; ++i) for (int j = 0; j < 3; ++j) {
        self.edtRefocusPos.stringValue = [NSString stringWithFormat:@"%.2f", offsets[i]];
        self.edtBokehStr.stringValue = [NSString stringWithFormat:@"%.2f", strengths[j]];
        NSArray<NSString*>* job = [self buildCliArgsForGroup:sample outDir:@"/output"];
        NSUInteger pos = [job indexOfObject:@"--refocus-pos"], radius = [job indexOfObject:@"--bokeh-strength"];
        refocusOk = refocusOk && pos != NSNotFound && radius != NSNotFound &&
            [job[pos + 1] doubleValue] == positions[i] && [job[radius + 1] doubleValue] == radii[j];
    }

    // Restore original user domains cleanly
    [ud removePersistentDomainForName:kPrefSuite];
    if (backupNew) [ud setPersistentDomain:backupNew forName:kPrefSuite];
    [ud synchronize];

    [leg removePersistentDomainForName:kPrefSuiteLegacy];
    if (backupLeg) [leg setPersistentDomain:backupLeg forName:kPrefSuiteLegacy];
    [leg synchronize];

    bool ok = defaultLosslessOk && legacyFallbackOk && roundtripOk && syncOk && parserOk && argsOk && lcpOk && refocusOk;
    std::printf("REFOCUS_ARGV_TEST:%s offsets=-1,0,1 positions=0,0.5,1 strengths=0,1,3 radii=0,12,36 prefs=UI\n",
        refocusOk ? "PASS" : "FAIL");
    std::printf("LCP_TEST:%s\n", lcpOk ? "PASS" : "FAIL");
    std::fprintf(stdout, "GUI_CONTRACT:defaults=%d migration=%d roundtrip=%d sync=%d unicode=%d argv=%d\n",
        defaultLosslessOk, legacyFallbackOk, roundtripOk, syncOk, parserOk, argsOk);
    std::fprintf(stdout, "PREFS_TEST:%s\n", ok ? "PASS" : "FAIL");
    std::fflush(stdout);
    std::exit(ok ? 0 : 1);
}

- (void)runProcessSelfTestAndExit {
    self.suppressPrefSave = YES;
    NSString* flood = @"i=0; while [ \"$i\" -lt 12000 ]; do printf 'diagnostic-output-abcdefghijklmnopqrstuvwxyz0123456789\\n' >&2; i=$((i+1)); done; exit 7";
    [self runUtilityCliTask:@"/bin/sh" args:@[@"-c", flood] label:@"测试管道排空" completion:^(int rc) {
        if (rc != 7 || self.currentTask) std::exit(1);
        [self runUtilityCliTask:@"/bin/sleep" args:@[@"30"] label:@"测试取消" completion:^(int status) {
            (void)status;
            std::exit(2);
        }];
        dispatch_after(dispatch_time(DISPATCH_TIME_NOW, 100 * NSEC_PER_MSEC), dispatch_get_main_queue(), ^{
            [self onRunOrStop:nil];
        });
        dispatch_after(dispatch_time(DISPATCH_TIME_NOW, NSEC_PER_SEC), dispatch_get_main_queue(), ^{
            bool ok = self.userStoppedJob && self.currentTask == nil &&
                [self.lblStageStatus.stringValue isEqualToString:@"已停止当前任务"];
            std::printf("PROCESS_TEST:%s\n", ok ? "PASS" : "FAIL");
            std::fflush(stdout);
            std::exit(ok ? 0 : 3);
        });
    }];
}

- (void)applicationWillTerminate:(NSNotification*)notification {
    (void)notification;
    [self saveUiPrefs];
    if (self.planTask && self.planTask.isRunning) [self.planTask terminate];
    if (self.currentTask && self.currentTask.isRunning) [self.currentTask terminate];
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
