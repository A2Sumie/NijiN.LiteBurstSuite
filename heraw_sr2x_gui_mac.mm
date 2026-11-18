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
#include <sys/sysctl.h>
#include <unistd.h>
#include "heraw_brand_assets.hpp"
#include "heraw_gui_i18n.hpp"
#include "heraw_version.hpp"

namespace {

static void detectMacCoreCounts(int& outTotal, int& outPerf) {
    auto rdSys = [](const char* name) -> int {
        uint64_t val = 0;
        size_t sz = sizeof(val);
        if (sysctlbyname(name, &val, &sz, nullptr, 0) == 0 && sz > 0 && val > 0) {
            if (sz == 4) {
                uint32_t v32 = 0;
                std::memcpy(&v32, &val, 4);
                return (int)v32;
            }
            return (int)val;
        }
        return 0;
    };
    int logC = rdSys("hw.logicalcpu");
    int physC = rdSys("hw.physicalcpu");
    int pC = rdSys("hw.perflevel0.physicalcpu");
    outTotal = std::max(1, logC > 0 ? logC : (physC > 0 ? physC : 8));
    if (pC > 0 && pC < outTotal) {
        outPerf = pC;
    } else if (physC > 0 && physC < outTotal) {
        outPerf = physC;
    } else {
        outPerf = std::max(1, (outTotal * 3) / 4);
    }
}

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
    const char* titleKey;
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
@property (assign) int srScore;
@property (assign) BOOL isStrayArchive;
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
        _srScore = 80;
        _isStrayArchive = NO;
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
        _tierBadge = heraw_i18n::tr_ns("t4.badge_pending");
        _oisSummary = heraw_i18n::tr_ns("t4.ois_idle");
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

    NSString* t2 = heraw_i18n::tr_ns("tab.2");
    NSString* t3 = heraw_i18n::tr_ns("tab.3");
    NSArray<NSString*>* titles = @[
        heraw_i18n::tr_ns("tab.1"),
        self.dotTab2 ? [@"• " stringByAppendingString:t2] : t2,
        self.dotTab3 ? [@"• " stringByAppendingString:t3] : t3,
        heraw_i18n::tr_ns("tab.4")
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

    NSString* mainText = self.scanning ? heraw_i18n::tr_ns("dock.well_scanning") : heraw_i18n::tr_ns("dock.well_idle");
    NSDictionary* mainAttrs = @{
        NSFontAttributeName: [NSFont systemFontOfSize:13.0 weight:NSFontWeightSemibold],
        NSForegroundColorAttributeName: [NSColor colorWithSRGBRed:0.20 green:0.36 blue:0.43 alpha:1.0]
    };
    NSSize msz = [mainText sizeWithAttributes:mainAttrs];
    [mainText drawAtPoint:NSMakePoint(cx - msz.width * 0.5, cy + 2) withAttributes:mainAttrs];

}
@end

@interface AquaSoftTableRowView : NSTableRowView
@end

@implementation AquaSoftTableRowView
- (NSBackgroundStyle)interiorBackgroundStyle {
    return NSBackgroundStyleNormal;
}
- (void)drawSelectionInRect:(NSRect)dirtyRect {
    (void)dirtyRect;
    if (self.selectionHighlightStyle == NSTableViewSelectionHighlightStyleNone) return;
    NSRect r = NSInsetRect(self.bounds, 3.0, 1.5);
    NSBezierPath* bp = [NSBezierPath bezierPathWithRoundedRect:r xRadius:6.0 yRadius:6.0];
    [[NSColor colorWithSRGBRed:0.875 green:0.938 blue:0.962 alpha:1.0] setFill];
    [bp fill];
    [[NSColor colorWithSRGBRed:0.62 green:0.78 blue:0.85 alpha:1.0] setStroke];
    bp.lineWidth = 1.0;
    [bp stroke];

    CGFloat barH = std::max(10.0, (double)r.size.height - 12.0);
    NSRect bar = NSMakeRect(r.origin.x + 1.5, r.origin.y + (r.size.height - barH) * 0.5, 3.0, barH);
    NSBezierPath* ab = [NSBezierPath bezierPathWithRoundedRect:bar xRadius:1.5 yRadius:1.5];
    [[NSColor colorWithSRGBRed:0.24 green:0.51 blue:0.60 alpha:1.0] setFill];
    [ab fill];
}
@end

@interface BurstQueueTableView : NSTableView
@property (nonatomic, copy) void (^onDeletePressed)(void);
@property (nonatomic, copy) void (^onSpacePressed)(void);
@property (nonatomic, copy) NSMenu* (^menuProvider)(NSInteger clickedRow);
@end

@implementation BurstQueueTableView
- (void)keyDown:(NSEvent*)event {
    NSString* ch = event.charactersIgnoringModifiers;
    if (ch.length > 0) {
        unichar c = [ch characterAtIndex:0];
        if (c == NSDeleteCharacter || c == NSBackspaceCharacter || c == NSDeleteFunctionKey) {
            if (self.onDeletePressed) { self.onDeletePressed(); return; }
        }
        if (c == ' ') {
            if (self.onSpacePressed) { self.onSpacePressed(); return; }
        }
    }
    [super keyDown:event];
}
- (NSMenu*)menuForEvent:(NSEvent*)event {
    NSPoint pt = [self convertPoint:event.locationInWindow fromView:nil];
    NSInteger row = [self rowAtPoint:pt];
    if (row >= 0 && ![self.selectedRowIndexes containsIndex:(NSUInteger)row]) {
        [self selectRowIndexes:[NSIndexSet indexSetWithIndex:(NSUInteger)row] byExtendingSelection:NO];
    }
    return self.menuProvider ? self.menuProvider(row) : nil;
}
@end

@interface FrameInspectorTableView : NSTableView
@property (nonatomic, copy) void (^onSpacePressed)(void);
@property (nonatomic, copy) NSMenu* (^menuProvider)(NSInteger clickedRow);
@end

@implementation FrameInspectorTableView
- (void)keyDown:(NSEvent*)event {
    NSString* ch = event.charactersIgnoringModifiers;
    if (ch.length > 0) {
        unichar c = [ch characterAtIndex:0];
        if (c == ' ' || c == NSDeleteCharacter || c == NSBackspaceCharacter || c == NSDeleteFunctionKey) {
            if (self.onSpacePressed) { self.onSpacePressed(); return; }
        }
    }
    [super keyDown:event];
}
- (NSMenu*)menuForEvent:(NSEvent*)event {
    NSPoint pt = [self convertPoint:event.locationInWindow fromView:nil];
    NSInteger row = [self rowAtPoint:pt];
    if (row >= 0 && ![self.selectedRowIndexes containsIndex:(NSUInteger)row]) {
        [self selectRowIndexes:[NSIndexSet indexSetWithIndex:(NSUInteger)row] byExtendingSelection:NO];
    }
    return self.menuProvider ? self.menuProvider(row) : nil;
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
        _btnAnchorDot.toolTip = heraw_i18n::tr_ns("dock.tip_anchor_dot");
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
        txt = heraw_i18n::tr_ns("t2.para_idle");
        tc = [NSColor colorWithSRGBRed:0.35 green:0.46 blue:0.52 alpha:1.0];
    } else if (!self.measured) {
        txt = heraw_i18n::trf_ns("t2.para_est_fmt", self.parallax2xPx);
        tc = [NSColor colorWithSRGBRed:0.35 green:0.46 blue:0.52 alpha:1.0];
    } else if (self.depthKnown && self.depthObservable) {
        txt = heraw_i18n::trf_ns("t2.para_ok_fmt", self.parallax2xPx);
        tc = [NSColor colorWithSRGBRed:0.16 green:0.45 blue:0.52 alpha:1.0];
    } else {
        txt = self.depthKnown
            ? heraw_i18n::trf_ns("t2.para_low_fmt", self.parallax2xPx)
            : heraw_i18n::trf_ns("t2.para_pend_fmt", self.parallax2xPx);
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
    drawSubBox(NSInsetRect(self.bounds, 0.5, 0.5), heraw_i18n::tr_ns("t4.traj_title"));

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

        NSString* scaleTxt = heraw_i18n::trf_ns("t4.traj_fullscale_fmt", maxAbs);
        [scaleTxt drawAtPoint:NSMakePoint(plotRect.origin.x + 8, NSMaxY(plotRect) - 16) withAttributes:axAttrs];
    } else {
        NSDictionary* emptyAttrs = @{
            NSFontAttributeName: [NSFont systemFontOfSize:11.5 weight:NSFontWeightMedium],
            NSForegroundColorAttributeName: [NSColor colorWithSRGBRed:0.42 green:0.53 blue:0.58 alpha:1.0]
        };
        NSString* msg = self.group ? heraw_i18n::tr_ns("t4.traj_pending") : heraw_i18n::tr_ns("t4.traj_empty");
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
        ? heraw_i18n::trf_ns("t4.quad_fmt", quadCount)
        : heraw_i18n::tr_ns("t4.quad_idle");
    [qText drawAtPoint:NSMakePoint(qx0 + 32, qy0 - 2) withAttributes:sumBold];

    NSString* shutCn = heraw_i18n::tr_ns("t4.shut_pending");
    if (self.group) {
        const char* stKey = [self.group.shutterType isEqualToString:@"global"] ? "t4.shut_global"
                          : ([self.group.shutterType isEqualToString:@"electronic"] ? "t4.shut_elec" : "t4.shut_mech");
        std::string st = heraw_i18n::tr_s(stKey);
        if (self.group.shutterShock) {
            std::string sk = heraw_i18n::tr_s("t4.shock_marked");
            shutCn = heraw_i18n::trf_ns("t4.shut_line_fmt", st.c_str(), sk.c_str(), self.group.maxDrift2xPx * 0.5);
        } else {
            shutCn = heraw_i18n::trf_ns("t4.shut_line_noshock_fmt", st.c_str(), self.group.maxDrift2xPx * 0.5);
        }
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
    drawSubBox(NSInsetRect(self.bounds, 0.5, 0.5), heraw_i18n::tr_ns("t4.radar_title"));

    CGFloat w = self.bounds.size.width;
    CGFloat h = self.bounds.size.height;

    // Tier badge on top-right
    NSString* badge = (self.telemetry && self.telemetry.tierBadge.length > 0)
        ? self.telemetry.tierBadge : heraw_i18n::tr_ns("t4.badge_pending");
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
    NSString* zoneNames[5] = {heraw_i18n::tr_ns("t4.zone_center"), @"NW", @"NE", @"SW", @"SE"};

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
                         : (!zValid[i] ? heraw_i18n::tr_ns("t4.zone_rej") : [NSString stringWithFormat:@"%.2f", mtf[i]]);
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
        ? heraw_i18n::trf_ns("t4.radar_l1_fmt", validZones)
        : heraw_i18n::tr_ns("t4.radar_l1_idle");
    std::string rLca = (pop && self.telemetry.redLcaValid) ? heraw_i18n::trf("%.2fpx", self.telemetry.lcaRedPx) : "--";
    std::string bLca = (pop && self.telemetry.blueLcaValid) ? heraw_i18n::trf("%.2fpx", self.telemetry.lcaBluePx) : "--";
    NSString* l2 = pop
        ? heraw_i18n::trf_ns("t4.radar_l2_fmt", rLca.c_str(), bLca.c_str())
        : heraw_i18n::tr_ns("t4.radar_l2_idle");
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
@property (nonatomic, copy) void (^onCmdL)(void);
@property (nonatomic, copy) void (^onCmdComma)(void);
@property (nonatomic, copy) void (^onDeleteGroup)(void);
@property (nonatomic, copy) void (^onMergeGroups)(void);
@property (nonatomic, copy) void (^onSplitFrames)(void);
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
            if ([ch isEqualToString:@"l"]) { if (self.onCmdL) { self.onCmdL(); return; } }
            if ([ch isEqualToString:@","]) { if (self.onCmdComma) { self.onCmdComma(); return; } }
            if ([ch isEqualToString:@"m"]) { if (self.onMergeGroups) { self.onMergeGroups(); return; } }
            if ([ch isEqualToString:@"x"] && [self sendAction:@selector(cut:) to:nil from:self]) return;
            if ([ch isEqualToString:@"c"] && [self sendAction:@selector(copy:) to:nil from:self]) return;
            if ([ch isEqualToString:@"v"] && [self sendAction:@selector(paste:) to:nil from:self]) return;
            if ([ch isEqualToString:@"a"] && [self sendAction:@selector(selectAll:) to:nil from:self]) return;
            if ([ch isEqualToString:@"z"] && [self sendAction:NSSelectorFromString(@"undo:") to:nil from:self]) return;
        } else if (mods == (NSEventModifierFlagCommand | NSEventModifierFlagShift)) {
            if ([ch isEqualToString:@"n"]) { if (self.onSplitFrames) { self.onSplitFrames(); return; } }
            if ([ch isEqualToString:@"z"] && [self sendAction:NSSelectorFromString(@"redo:") to:nil from:self]) return;
        } else if (mods == 0 && ch.length > 0) {
            unichar uc = [ch characterAtIndex:0];
            if (uc == NSDeleteCharacter || uc == NSBackspaceCharacter || uc == NSDeleteFunctionKey) {
                NSResponder* fr = self.keyWindow.firstResponder;
                if (![fr isKindOfClass:[NSTextView class]] && ![fr isKindOfClass:[FrameInspectorTableView class]] && self.onDeleteGroup) {
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
static NSString* const kPrefCliPath     = @"NijinLbsCustomCliPath." NIJIN_VERSION;
static NSString* const kPrefLibRawPath  = @"NijinLbsCustomLibRawPath." NIJIN_VERSION;

static NSUserDefaults* nijinPrefs(void) {
    static NSUserDefaults* ud = nil;
    if (!ud) {
        ud = [[[NSBundle mainBundle] bundleIdentifier] isEqualToString:kPrefSuite]
            ? [NSUserDefaults standardUserDefaults] : [[NSUserDefaults alloc] initWithSuiteName:kPrefSuite];
    }
    return ud;
}

// ============================================================================
// Main Controller (AppDelegate)
// ============================================================================

@interface AppDelegate : NSObject <NSApplicationDelegate, NSWindowDelegate, NSTableViewDataSource, NSTableViewDelegate, NSTextFieldDelegate, NSMenuDelegate>
@property (strong) DropWindow* window;
@property (strong) StudioCanvasView* canvas;
@property (strong) PortalPillButton* btnPerfHeader;
@property (assign) int hostTotalCores;
@property (assign) int hostPerfCores;
@property (assign) int hostHalfCores;

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
@property (strong) PortalPillButton* btnIntakeAction;
@property (strong) NSButton* btnAddFiles;
@property (strong) NSButton* btnAddFolder;
@property (strong) NSButton* btnMergeGroups;
@property (strong) NSButton* btnCleanStray;
@property (strong) NSButton* btnExportTopK;
@property (strong) NSButton* btnClearQueue;

@property (strong) NSTextField* lblInspectorHeader;
@property (strong) NSTextField* lblInspectorHint;
@property (strong) NSButton* btnSplitFrames;
@property (strong) NSTextField* lblInspectorEmpty;
@property (strong) NSScrollView* svFrames;
@property (strong) FrameInspectorTableView* tblFrames;

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
@property (copy) NSString* autoOutDir;
@property (strong) NSButton* btnBrowseOut;
@property (strong) NSButton* btnClearOut;

@property (strong) NSButton* chkEmitRaw;
@property (strong) NSPopUpButton* popRawFmt;
@property (strong) NSButton* chkRelayRaw;
@property (strong) NSButton* chkNoToolExif;

@property (strong) NSButton* chkEmitRgb;
@property (strong) NSSegmentedControl* segRgbScale;
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
@property (strong) NSPopUpButton* popGrouping;
@property (strong) NSButton* btnRegroup;
@property (strong) NSTextField* lblGroupingHint;
@property (assign) BOOL groupingDirty;
@property (strong) NSTextField* edtGrpSz;
@property (strong) NSTextField* lblThreadsCap;
@property (strong) NSPopUpButton* popCoreCount;
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
@property (strong) NSButton* btnExportLog;
@property (strong) NSButton* btnOpenOutDir;
@property (copy) NSString* langCode;

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
    {"motion",     "t3.fld_motion",     0, false},
    {"phase",      "t3.fld_phase",      0, false},
    {"focus",      "t3.fld_focus",      0, false},
    {"depth",      "t3.fld_depth",      1, true},
    {"defocus",    "t3.fld_defocus",    1, true},
    {"specular",   "t3.fld_specular",   1, false},
    {"sigma",      "t3.fld_sigma",      2, false},
    {"coherence",  "t3.fld_coherence",  2, false},
    {"velocity",   "t3.fld_velocity",   2, false},
    {"flicker",    "t3.fld_flicker",    3, true},
    {"turbulence", "t3.fld_turbulence", 3, true}
};

static NSArray<NSString*>* rawFormatCodes(void) {
    return @[@"dng", @"dng-linear", @"dng-fp16", @"dng-hq", @"dng-raw"];
}

static NSArray<NSString*>* rgbFormatCodes(void) {
    return @[@"tiff-deflate", @"tiff-hq", @"png", @"jxs", @"jxs-lossless", @"tiff-raw"];
}

static NSArray<NSString*>* rgbScaleCodes(void) {
    return @[@"2x", @"1x", @"both"];
}

static NSArray<NSString*>* fieldModeCodes(void) {
    return @[@"tiff-alpha", @"split", @"rgb"];
}

static NSArray<NSString*>* olpfModeCodes(void) {
    return @[@"auto", @"none", @"weak4", @"gauss16"];
}

- (NSString*)selectedRgbFormatCode {
    NSArray<NSString*>* codes = rgbFormatCodes();
    NSInteger idx = self.popRgbFmt ? self.popRgbFmt.indexOfSelectedItem : 0;
    return (idx >= 0 && (NSUInteger)idx < codes.count) ? codes[(NSUInteger)idx] : @"tiff-deflate";
}

- (NSString*)selectedRgbScaleCode {
    NSArray<NSString*>* codes = rgbScaleCodes();
    NSInteger idx = self.segRgbScale ? self.segRgbScale.selectedSegment : 0;
    return (idx >= 0 && (NSUInteger)idx < codes.count) ? codes[(NSUInteger)idx] : @"2x";
}

- (int)minValidFramesRequired {
    BOOL emitRaw = (self.chkEmitRaw && self.chkEmitRaw.state == NSControlStateValueOn);
    BOOL emitRgb = (!self.chkEmitRgb || self.chkEmitRgb.state == NSControlStateValueOn);
    NSString* sc = [self selectedRgbScaleCode];
    return (!emitRaw && emitRgb && [sc isEqualToString:@"1x"]) ? 3 : 5;
}

// ============================================================================
// Menu & CLI Discovery
// ============================================================================

- (NSMenu*)buildLanguageSubmenu {
    NSMenu* langMenu = [[NSMenu alloc] initWithTitle:heraw_i18n::tr_ns("menu.language")];
    std::string active = heraw_i18n::I18nManager::instance().active_code();
    auto langs = heraw_i18n::I18nManager::instance().available_languages();
    for (const auto& kv : langs) {
        NSString* code = [NSString stringWithUTF8String:kv.first.c_str()] ?: @"";
        NSString* name = [NSString stringWithUTF8String:kv.second.c_str()] ?: code;
        NSMenuItem* it = [[NSMenuItem alloc] initWithTitle:name action:@selector(onSelectLanguageItem:) keyEquivalent:@""];
        it.target = self;
        it.representedObject = code;
        it.state = ([code caseInsensitiveCompare:[NSString stringWithUTF8String:active.c_str()]] == NSOrderedSame)
            ? NSControlStateValueOn : NSControlStateValueOff;
        [langMenu addItem:it];
    }
    return langMenu;
}

- (void)setupMainMenu {
    NSMenu* mainMenu = [[NSMenu alloc] initWithTitle:@"MainMenu"];
    int minReq = [self minValidFramesRequired];

    NSMenuItem* appItem = [[NSMenuItem alloc] initWithTitle:@"nijinLBS" action:nil keyEquivalent:@""];
    NSMenu* appMenu = [[NSMenu alloc] initWithTitle:@"nijinLBS"];
    NSMenuItem* prefsAppItem = [[NSMenuItem alloc] initWithTitle:heraw_i18n::tr_ns("menu.perf_settings")
                                                          action:@selector(onOpenEngineSheet:) keyEquivalent:@","];
    prefsAppItem.target = self;
    [appMenu addItem:prefsAppItem];
    NSMenuItem* langAppItem = [[NSMenuItem alloc] initWithTitle:heraw_i18n::tr_ns("menu.language") action:nil keyEquivalent:@""];
    langAppItem.submenu = [self buildLanguageSubmenu];
    [appMenu addItem:langAppItem];
    [appMenu addItem:[NSMenuItem separatorItem]];
    [appMenu addItemWithTitle:heraw_i18n::tr_ns("menu.hide_app") action:@selector(hide:) keyEquivalent:@"h"];
    NSMenuItem* hideOthers = [appMenu addItemWithTitle:heraw_i18n::tr_ns("menu.hide_others")
                                                action:@selector(hideOtherApplications:) keyEquivalent:@"h"];
    hideOthers.keyEquivalentModifierMask = NSEventModifierFlagCommand | NSEventModifierFlagOption;
    [appMenu addItem:[NSMenuItem separatorItem]];
    [appMenu addItemWithTitle:heraw_i18n::tr_ns("menu.close_win") action:@selector(performClose:) keyEquivalent:@"w"];
    [appMenu addItemWithTitle:heraw_i18n::tr_ns("menu.quit_app") action:@selector(terminate:) keyEquivalent:@"q"];
    appItem.submenu = appMenu;
    [mainMenu addItem:appItem];

    NSMenuItem* fileItem = [[NSMenuItem alloc] initWithTitle:heraw_i18n::tr_ns("menu.file") action:nil keyEquivalent:@""];
    NSMenu* fileMenu = [[NSMenu alloc] initWithTitle:heraw_i18n::tr_ns("menu.file")];
    NSMenuItem* openFilesItem = [[NSMenuItem alloc] initWithTitle:heraw_i18n::tr_ns("menu.add_raw_or_dir")
                                                           action:@selector(onOpenMixedPanel:) keyEquivalent:@"o"];
    openFilesItem.target = self;
    [fileMenu addItem:openFilesItem];
    NSMenuItem* expLogItem = [[NSMenuItem alloc] initWithTitle:heraw_i18n::tr_ns("menu.export_log")
                                                        action:@selector(onExportDiagLog:) keyEquivalent:@"l"];
    expLogItem.target = self;
    [fileMenu addItem:expLogItem];
    fileItem.submenu = fileMenu;
    [mainMenu addItem:fileItem];

    NSMenuItem* editItem = [[NSMenuItem alloc] initWithTitle:heraw_i18n::tr_ns("menu.edit") action:nil keyEquivalent:@""];
    NSMenu* editMenu = [[NSMenu alloc] initWithTitle:heraw_i18n::tr_ns("menu.edit")];
    [editMenu addItemWithTitle:heraw_i18n::tr_ns("menu.undo") action:NSSelectorFromString(@"undo:") keyEquivalent:@"z"];
    [editMenu addItemWithTitle:heraw_i18n::tr_ns("menu.redo") action:NSSelectorFromString(@"redo:") keyEquivalent:@"Z"];
    [editMenu addItem:[NSMenuItem separatorItem]];
    [editMenu addItemWithTitle:heraw_i18n::tr_ns("menu.cut") action:@selector(cut:) keyEquivalent:@"x"];
    [editMenu addItemWithTitle:heraw_i18n::tr_ns("menu.copy") action:@selector(copy:) keyEquivalent:@"c"];
    [editMenu addItemWithTitle:heraw_i18n::tr_ns("menu.paste") action:@selector(paste:) keyEquivalent:@"v"];
    [editMenu addItemWithTitle:heraw_i18n::tr_ns("menu.select_all") action:@selector(selectAll:) keyEquivalent:@"a"];
    [editMenu addItem:[NSMenuItem separatorItem]];
    NSMenuItem* mergeItem = [[NSMenuItem alloc] initWithTitle:heraw_i18n::tr_ns("menu.merge_groups")
                                                       action:@selector(onMergeSelectedGroups:) keyEquivalent:@"m"];
    mergeItem.target = self;
    [editMenu addItem:mergeItem];
    NSMenuItem* splitItem = [[NSMenuItem alloc] initWithTitle:heraw_i18n::tr_ns("menu.split_frames")
                                                       action:@selector(onSplitSelectedFrames:) keyEquivalent:@"N"];
    splitItem.keyEquivalentModifierMask = NSEventModifierFlagCommand | NSEventModifierFlagShift;
    splitItem.target = self;
    [editMenu addItem:splitItem];
    NSMenuItem* cleanItem = [[NSMenuItem alloc] initWithTitle:heraw_i18n::trf_ns("menu.clean_stray_fmt", minReq)
                                                       action:@selector(onCleanStrayGroups:) keyEquivalent:@""];
    cleanItem.target = self;
    [editMenu addItem:cleanItem];
    editItem.submenu = editMenu;
    [mainMenu addItem:editItem];

    NSMenuItem* perfItem = [[NSMenuItem alloc] initWithTitle:heraw_i18n::tr_ns("menu.perf") action:nil keyEquivalent:@""];
    NSMenu* perfMenu = [[NSMenu alloc] initWithTitle:heraw_i18n::tr_ns("menu.perf")];
    perfMenu.delegate = self;
    [self populatePerformanceMenu:perfMenu];
    perfItem.submenu = perfMenu;
    [mainMenu addItem:perfItem];

    [NSApp setMainMenu:mainMenu];
}

- (NSArray<NSString*>*)macCorePresetTitles {
    NSString* pLabel = (self.hostPerfCores < self.hostTotalCores)
        ? heraw_i18n::trf_ns("menu.core_perf_fmt", self.hostPerfCores)
        : heraw_i18n::trf_ns("menu.core_main_fmt", self.hostPerfCores);
    return @[
        heraw_i18n::trf_ns("menu.core_all_fmt", self.hostTotalCores),
        pLabel,
        heraw_i18n::trf_ns("menu.core_half_fmt", self.hostHalfCores),
        heraw_i18n::tr_ns("menu.core_custom")
    ];
}

- (void)syncCoreCountPopupFromThreads {
    if (!self.popCoreCount || !self.edtThreads) return;
    NSInteger thr = self.edtThreads.integerValue;
    NSInteger idx = 3;
    if (thr <= 0) idx = 0;
    else if (thr == self.hostPerfCores) idx = 1;
    else if (thr == self.hostHalfCores) idx = 2;
    if (idx >= 0 && idx < self.popCoreCount.numberOfItems) {
        [self.popCoreCount selectItemAtIndex:idx];
    }
    [self updatePerfHeaderTitle];
}

- (void)updatePerfHeaderTitle {
    if (!self.btnPerfHeader) return;
    NSInteger bIdx = self.popBackend ? self.popBackend.indexOfSelectedItem : 0;
    const char* bKey = (bIdx == 1) ? "hdr.gpu_force" : ((bIdx == 2) ? "hdr.gpu_cpu" : "hdr.gpu_auto");
    std::string bShort = heraw_i18n::tr_s(bKey);
    NSInteger thr = self.edtThreads ? self.edtThreads.integerValue : 0;
    std::string cShort = (thr <= 0)
        ? heraw_i18n::trf("hdr.cores_all_fmt", self.hostTotalCores)
        : heraw_i18n::trf("hdr.cores_n_fmt", (int)thr);
    self.btnPerfHeader.title = heraw_i18n::trf_ns("hdr.perf_fmt", bShort.c_str(), cShort.c_str());
    self.btnPerfHeader.activeHighlight = (bIdx != 0 || thr > 0);
    [self.btnPerfHeader setNeedsDisplay:YES];
}

- (void)onCoreCountPopupChanged:(NSPopUpButton*)sender {
    NSInteger idx = sender.indexOfSelectedItem;
    if (idx == 0) {
        self.edtThreads.stringValue = @"0";
    } else if (idx == 1) {
        self.edtThreads.stringValue = [NSString stringWithFormat:@"%d", self.hostPerfCores];
    } else if (idx == 2) {
        self.edtThreads.stringValue = [NSString stringWithFormat:@"%d", self.hostHalfCores];
    } else {
        if (self.edtThreads.integerValue <= 0) {
            self.edtThreads.stringValue = [NSString stringWithFormat:@"%d", self.hostTotalCores];
        }
        [self.window makeFirstResponder:self.edtThreads];
    }
    [self updatePerfHeaderTitle];
    [self saveUiPrefs];
}

- (NSString*)macCpuTopologyBadge {
    if (self.hostPerfCores > 0 && self.hostPerfCores < self.hostTotalCores) {
        int effC = std::max(0, self.hostTotalCores - self.hostPerfCores);
        return heraw_i18n::trf_ns("menu.hw_hybrid_mac_fmt", self.hostPerfCores, effC, self.hostTotalCores);
    }
    return heraw_i18n::trf_ns("menu.hw_cores_fmt", self.hostTotalCores);
}

- (void)populatePerformanceMenu:(NSMenu*)menu {
    [menu removeAllItems];
    NSInteger bIdx = self.popBackend ? self.popBackend.indexOfSelectedItem : 0;
    NSInteger cIdx = self.popCoreCount ? self.popCoreCount.indexOfSelectedItem : 0;
    NSInteger thr  = self.edtThreads ? self.edtThreads.integerValue : 0;

    std::string badgeUtf8 = [[self macCpuTopologyBadge] UTF8String] ?: "";
    NSMenuItem* hdrHw = [[NSMenuItem alloc] initWithTitle:heraw_i18n::trf_ns("menu.hw_detected_fmt", badgeUtf8.c_str())
                                                   action:nil keyEquivalent:@""];
    hdrHw.enabled = NO;
    [menu addItem:hdrHw];
    [menu addItem:[NSMenuItem separatorItem]];

    NSMenuItem* hdrGpu = [[NSMenuItem alloc] initWithTitle:heraw_i18n::tr_ns("menu.metal_hdr") action:nil keyEquivalent:@""];
    hdrGpu.enabled = NO;
    [menu addItem:hdrGpu];

    NSArray<NSString*>* gpuTitles = @[
        heraw_i18n::tr_ns("menu.metal_auto"),
        heraw_i18n::tr_ns("menu.metal_force"),
        heraw_i18n::tr_ns("menu.metal_cpu")
    ];
    for (NSUInteger i = 0; i < gpuTitles.count; ++i) {
        NSMenuItem* it = [[NSMenuItem alloc] initWithTitle:gpuTitles[i] action:@selector(onPerfMenuBackend:) keyEquivalent:@""];
        it.target = self;
        it.tag = (NSInteger)i;
        it.state = ((NSInteger)i == bIdx) ? NSControlStateValueOn : NSControlStateValueOff;
        [menu addItem:it];
    }

    [menu addItem:[NSMenuItem separatorItem]];
    NSMenuItem* hdrCpu = [[NSMenuItem alloc] initWithTitle:heraw_i18n::tr_ns("menu.cpu_budget_hdr") action:nil keyEquivalent:@""];
    hdrCpu.enabled = NO;
    [menu addItem:hdrCpu];

    NSArray<NSString*>* coreTitles = [self macCorePresetTitles];
    for (NSUInteger i = 0; i < 3 && i < coreTitles.count; ++i) {
        NSMenuItem* it = [[NSMenuItem alloc] initWithTitle:coreTitles[i] action:@selector(onPerfMenuCorePreset:) keyEquivalent:@""];
        it.target = self;
        it.tag = (NSInteger)i;
        it.state = ((NSInteger)i == cIdx) ? NSControlStateValueOn : NSControlStateValueOff;
        [menu addItem:it];
    }
    NSString* customTitle = (cIdx == 3 && thr > 0)
        ? heraw_i18n::trf_ns("menu.core_custom_n_fmt", (int)thr)
        : heraw_i18n::tr_ns("menu.core_custom_engine");
    NSMenuItem* customItem = [[NSMenuItem alloc] initWithTitle:customTitle action:@selector(onOpenEngineSheet:) keyEquivalent:@""];
    customItem.target = self;
    customItem.state = (cIdx == 3) ? NSControlStateValueOn : NSControlStateValueOff;
    [menu addItem:customItem];

    [menu addItem:[NSMenuItem separatorItem]];
    NSMenuItem* langSub = [[NSMenuItem alloc] initWithTitle:heraw_i18n::tr_ns("menu.language") action:nil keyEquivalent:@""];
    langSub.submenu = [self buildLanguageSubmenu];
    [menu addItem:langSub];

    NSMenuItem* logItem = [[NSMenuItem alloc] initWithTitle:heraw_i18n::tr_ns("menu.export_log")
                                                     action:@selector(onExportDiagLog:) keyEquivalent:@"l"];
    logItem.target = self;
    [menu addItem:logItem];
}

- (void)onSelectLanguageItem:(NSMenuItem*)sender {
    NSString* code = [sender.representedObject isKindOfClass:[NSString class]] ? sender.representedObject : nil;
    if (code.length == 0) return;
    [self setLanguageCodeAndRefresh:code persist:YES];
}

- (void)setLanguageCodeAndRefresh:(NSString*)code persist:(BOOL)persist {
    if (!code || code.length == 0) return;
    heraw_i18n::I18nManager::instance().set_language([code UTF8String]);
    self.langCode = [NSString stringWithUTF8String:heraw_i18n::I18nManager::instance().active_code().c_str()];
    heraw_i18n::diag_log().log_event("UI", std::string("Language switched to ") + [self.langCode UTF8String]);
    [self applyUiLanguage];
    if (persist) [self saveUiPrefs];
}

- (void)menuNeedsUpdate:(NSMenu*)menu {
    [self populatePerformanceMenu:menu];
}

- (void)onPerfHeaderClicked:(NSButton*)sender {
    NSMenu* m = [[NSMenu alloc] initWithTitle:@"PerfQuickMenu"];
    [self populatePerformanceMenu:m];
    [m popUpMenuPositioningItem:nil atLocation:NSMakePoint(0, sender.bounds.size.height + 4) inView:sender];
}

- (void)onPerfMenuBackend:(NSMenuItem*)sender {
    NSInteger idx = sender.tag;
    if (idx >= 0 && idx < self.popBackend.numberOfItems) {
        [self.popBackend selectItemAtIndex:idx];
        [self updatePerfHeaderTitle];
        [self saveUiPrefs];
    }
}

- (void)onPerfMenuCorePreset:(NSMenuItem*)sender {
    NSInteger idx = sender.tag;
    if (idx >= 0 && idx < self.popCoreCount.numberOfItems) {
        [self.popCoreCount selectItemAtIndex:idx];
        [self onCoreCountPopupChanged:self.popCoreCount];
    }
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
    if (!self.lblEngineStrip) return;
    NSString* cli = [self autoDetectCliPath:YES];
    std::string cliShown = cli ? [[cli lastPathComponent] UTF8String] : heraw_i18n::tr_s("t1.engine_select_cli");
    std::string lrShown = (self.customLibRawPath && self.customLibRawPath.length > 0)
        ? heraw_i18n::trf("t1.engine_ext_lr_fmt", [[self.customLibRawPath lastPathComponent] UTF8String])
        : heraw_i18n::tr_s("t1.engine_builtin_lr");
    std::string cpuBadge = [[self macCpuTopologyBadge] UTF8String] ?: "";
    self.lblEngineStrip.stringValue = heraw_i18n::trf_ns("t1.engine_strip_fmt", cpuBadge.c_str(), cliShown.c_str(), lrShown.c_str());
}

- (NSString*)resolveActiveCliOrPrompt {
    NSString* found = [self autoDetectCliPath:YES];
    if (found) return found;
    [self onOpenEngineSheet:nil];
    return [self autoDetectCliPath:YES];
}

- (void)onOpenEngineSheet:(id)sender {
    (void)sender;
    NSWindow* sheet = [[NSWindow alloc] initWithContentRect:NSMakeRect(0, 0, 540, 268)
        styleMask:NSWindowStyleMaskTitled backing:NSBackingStoreBuffered defer:NO];
    sheet.appearance = [NSAppearance appearanceNamed:NSAppearanceNameAqua];
    NSView* box = [[NSView alloc] initWithFrame:NSMakeRect(0, 0, 540, 268)];
    sheet.contentView = box;

    NSTextField* title = [NSTextField labelWithString:heraw_i18n::tr_ns("dlg.engine_title")];
    title.font = [NSFont boldSystemFontOfSize:13.5];
    title.frame = NSMakeRect(20, 230, 500, 22);
    [box addSubview:title];

    NSTextField* lGpu = [NSTextField labelWithString:heraw_i18n::tr_ns("dlg.gpu_lbl")];
    lGpu.frame = NSMakeRect(20, 192, 72, 20);
    [box addSubview:lGpu];

    NSPopUpButton* pGpu = [[NSPopUpButton alloc] initWithFrame:NSMakeRect(96, 189, 210, 26) pullsDown:NO];
    [pGpu addItemsWithTitles:@[
        heraw_i18n::tr_ns("menu.metal_auto"),
        heraw_i18n::tr_ns("menu.metal_force"),
        heraw_i18n::tr_ns("menu.metal_cpu")
    ]];
    if (self.popBackend.indexOfSelectedItem >= 0) [pGpu selectItemAtIndex:self.popBackend.indexOfSelectedItem];
    [box addSubview:pGpu];

    NSTextField* lCores = [NSTextField labelWithString:heraw_i18n::tr_ns("dlg.cores_lbl")];
    lCores.frame = NSMakeRect(20, 154, 72, 20);
    [box addSubview:lCores];

    NSPopUpButton* pCores = [[NSPopUpButton alloc] initWithFrame:NSMakeRect(96, 151, 210, 26) pullsDown:NO];
    [pCores addItemsWithTitles:[self macCorePresetTitles]];
    if (self.popCoreCount.indexOfSelectedItem >= 0) [pCores selectItemAtIndex:self.popCoreCount.indexOfSelectedItem];
    [box addSubview:pCores];

    NSTextField* lThrNum = [NSTextField labelWithString:heraw_i18n::tr_ns("dlg.thr_num_lbl")];
    lThrNum.frame = NSMakeRect(318, 154, 118, 20);
    [box addSubview:lThrNum];

    NSTextField* eThr = [[NSTextField alloc] initWithFrame:NSMakeRect(438, 152, 66, 24)];
    eThr.font = [NSFont monospacedDigitSystemFontOfSize:12.0 weight:NSFontWeightRegular];
    eThr.stringValue = self.edtThreads.stringValue ?: @"0";
    eThr.identifier = @"SheetThrField";
    [box addSubview:eThr];

    pCores.target = self;
    pCores.action = @selector(sheetCorePresetChanged:);
    pCores.identifier = @"SheetCorePopup";

    NSBox* sep = [[NSBox alloc] initWithFrame:NSMakeRect(20, 138, 500, 1)];
    sep.boxType = NSBoxSeparator;
    [box addSubview:sep];

    NSTextField* lCli = [NSTextField labelWithString:heraw_i18n::tr_ns("dlg.cli_lbl")];
    lCli.frame = NSMakeRect(20, 104, 72, 20);
    [box addSubview:lCli];

    NSTextField* eCli = [[NSTextField alloc] initWithFrame:NSMakeRect(96, 102, 306, 24)];
    eCli.placeholderString = heraw_i18n::tr_ns("dlg.cli_ph");
    eCli.stringValue = self.customCliPath ?: ([self autoDetectCliPath:YES] ?: @"");
    [box addSubview:eCli];

    NSButton* bCliBrowse = [NSButton buttonWithTitle:heraw_i18n::tr_ns("t1.browse") target:nil action:nil];
    bCliBrowse.frame = NSMakeRect(408, 100, 62, 28);
    [box addSubview:bCliBrowse];

    NSButton* bCliAuto = [NSButton buttonWithTitle:heraw_i18n::tr_ns("dlg.auto_btn") target:nil action:nil];
    bCliAuto.frame = NSMakeRect(470, 100, 52, 28);
    [box addSubview:bCliAuto];

    NSTextField* lLr = [NSTextField labelWithString:heraw_i18n::tr_ns("dlg.lr_lbl")];
    lLr.frame = NSMakeRect(20, 64, 72, 20);
    [box addSubview:lLr];

    NSTextField* eLr = [[NSTextField alloc] initWithFrame:NSMakeRect(96, 62, 306, 24)];
    eLr.placeholderString = heraw_i18n::tr_ns("dlg.lr_ph");
    eLr.stringValue = self.customLibRawPath ?: @"";
    [box addSubview:eLr];

    NSButton* bLrBrowse = [NSButton buttonWithTitle:heraw_i18n::tr_ns("t1.browse") target:nil action:nil];
    bLrBrowse.frame = NSMakeRect(408, 60, 62, 28);
    [box addSubview:bLrBrowse];

    NSButton* bLrReset = [NSButton buttonWithTitle:heraw_i18n::tr_ns("dlg.builtin_btn") target:nil action:nil];
    bLrReset.frame = NSMakeRect(470, 60, 52, 28);
    [box addSubview:bLrReset];

    NSButton* bDone = [NSButton buttonWithTitle:heraw_i18n::tr_ns("dlg.done_btn") target:nil action:nil];
    bDone.keyEquivalent = @"\r";
    bDone.frame = NSMakeRect(436, 16, 86, 30);
    [box addSubview:bDone];

    NSButton* bCancel = [NSButton buttonWithTitle:heraw_i18n::tr_ns("dlg.cancel_btn") target:nil action:nil];
    bCancel.frame = NSMakeRect(344, 16, 84, 30);
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
            if (pGpu.indexOfSelectedItem >= 0 && pGpu.indexOfSelectedItem < s.popBackend.numberOfItems) {
                [s.popBackend selectItemAtIndex:pGpu.indexOfSelectedItem];
            }
            NSInteger nVal = eThr.integerValue;
            s.edtThreads.stringValue = (nVal > 0) ? [NSString stringWithFormat:@"%ld", (long)nVal] : @"0";
            [s syncCoreCountPopupFromThreads];

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

- (void)sheetCorePresetChanged:(NSPopUpButton*)sender {
    NSTextField* eThr = [self findSheetField:sender.superview ident:@"SheetThrField"];
    if (!eThr) return;
    NSInteger idx = sender.indexOfSelectedItem;
    if (idx == 0) eThr.stringValue = @"0";
    else if (idx == 1) eThr.stringValue = [NSString stringWithFormat:@"%d", self.hostPerfCores];
    else if (idx == 2) eThr.stringValue = [NSString stringWithFormat:@"%d", self.hostHalfCores];
    else if (eThr.integerValue <= 0) eThr.stringValue = [NSString stringWithFormat:@"%d", self.hostTotalCores];
}

- (NSTextField*)findSheetField:(NSView*)root ident:(NSString*)ident {
    for (NSView* v in root.subviews) {
        if ([v isKindOfClass:[NSTextField class]] && [v.identifier isEqualToString:ident]) return (NSTextField*)v;
    }
    return nil;
}

- (void)sheetBrowseCli:(NSButton*)sender {
    NSOpenPanel* op = [NSOpenPanel openPanel];
    op.title = heraw_i18n::tr_ns("dlg.pick_cli");
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
    op.title = heraw_i18n::tr_ns("dlg.pick_lr");
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

- (NSString*)effectiveFieldsCsvForGroup:(BurstGroupModel*)grp {
    NSMutableArray<NSString*>* arr = [NSMutableArray array];
    for (NSUInteger i = 0; i < self.chkFields.count && i < 11; ++i) {
        if (self.chkFields[i].state != NSControlStateValueOn) continue;
        NSString* code = [NSString stringWithUTF8String:kFieldSpecs[i].code];
        NSNumber* obs = grp ? grp.fieldObservability[code] : nil;
        if (obs != nil && !obs.boolValue) continue;
        [arr addObject:code];
    }
    return [arr componentsJoinedByString:@","];
}

- (NSString*)skipFramesCsvForGroup:(BurstGroupModel*)grp {
    if (!grp) return @"";
    NSMutableArray<NSString*>* skips = [NSMutableArray array];
    for (FrameDiagItem* f in grp.frames) {
        if (!f.enabled) [skips addObject:[NSString stringWithFormat:@"%d", f.idx1Based]];
    }
    return [skips componentsJoinedByString:@","];
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
            [self.segT2FreezePose setLabel:heraw_i18n::trf_ns("t2.pose_locked_fmt", grp.customAnchor1Based) forSegment:3];
        } else {
            [self.segT2FreezePose setLabel:heraw_i18n::tr_ns("t2.pose_follow") forSegment:3];
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
    NSString* curRgbCode = [self selectedRgbFormatCode];
    if (emitFld && fmIdx == 0) {
        BOOL isTiff = ([curRgbCode isEqualToString:@"tiff-deflate"] ||
                       [curRgbCode isEqualToString:@"tiff-hq"] ||
                       [curRgbCode isEqualToString:@"tiff-raw"]);
        if (self.chkEmitRgb.state != NSControlStateValueOn || !isTiff) {
            self.chkEmitRgb.state = NSControlStateValueOn;
            [self.popRgbFmt selectItemAtIndex:0];
            curRgbCode = @"tiff-deflate";
        }
    }
    self.popFieldMode.enabled = emitFld;
    self.segCarrierTab3.selectedSegment = emitFld ? (fmIdx + 1) : 0;

    // 4. Causal Affordances on Tab 1 Output & Sliders
    BOOL emitRaw = (self.chkEmitRaw.state == NSControlStateValueOn);
    self.popRawFmt.enabled = emitRaw;
    self.chkRelayRaw.enabled = emitRaw;
    BOOL relayOn = emitRaw && (self.chkRelayRaw.state == NSControlStateValueOn);

    BOOL emitRgb = (self.chkEmitRgb.state == NSControlStateValueOn);
    if (self.segRgbScale) self.segRgbScale.enabled = emitRgb;
    self.popRgbFmt.enabled = emitRgb;
    self.segColorSpace.enabled = emitRgb;

    BOOL isJxsLossy = (emitRgb && [curRgbCode isEqualToString:@"jxs"]);
    self.edtJxsBpp.enabled = isJxsLossy;
    self.lblBppCap.alphaValue = isJxsLossy ? 1.0 : 0.45;
    self.edtJxsBpp.alphaValue = isJxsLossy ? 1.0 : 0.45;

    // Relay-RAW causal dimming when RGB is unchecked
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
            ? heraw_i18n::trf_ns("t2.drift_ok_fmt", drift2x)
            : heraw_i18n::trf_ns("t2.drift_low_fmt", drift2x);
    } else {
        [self.segReflectionMode setEnabled:YES forSegment:1];
        self.lblDriftReadout.stringValue = heraw_i18n::tr_ns("t2.drift_idle");
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
        ? heraw_i18n::trf_ns("t3.gate_measured_fmt", (int)available, (int)known)
        : heraw_i18n::tr_ns("t3.gate_idle");

    NSUInteger selFields = [self selectedFieldCount];
    self.btnPortalFields.title = heraw_i18n::trf_ns("t1.portal_fields_fmt", (int)selFields, (int)self.chkFields.count);
    self.btnPortalFields.activeHighlight = (emitFld || tilesOn || selFields != 3);
    [self.btnPortalFields setNeedsDisplay:YES];

    // 7. Tab 1 Portals & Tab 4 Telemetry Sync
    if (grp && grp.hasTelemetry) {
        self.btnPortalQuad.title = heraw_i18n::trf_ns("t1.portal_quad_fmt", grp.quadCoverage);
    } else {
        self.btnPortalQuad.title = heraw_i18n::tr_ns("t1.portal_quad_idle");
    }
    [self.btnPortalQuad setNeedsDisplay:YES];

    BOOL strobeOrLf = ([mm isEqualToString:@"strobe"] || pxOn || rfOn);
    self.btnPortalStrobe.title = [mm isEqualToString:@"strobe"]
        ? heraw_i18n::tr_ns("t1.portal_strobe_on")
        : heraw_i18n::tr_ns("t1.portal_strobe_idle");
    self.btnPortalStrobe.activeHighlight = strobeOrLf;
    [self.btnPortalStrobe setNeedsDisplay:YES];

    BOOL reflOrCustomTrans = (reflOn || (emitTrans && std::fabs(self.edtTransSigma.doubleValue - 3.5) > 0.05));
    self.btnPortalRefl.title = reflOrCustomTrans
        ? heraw_i18n::tr_ns("t1.portal_refl_on")
        : heraw_i18n::tr_ns("t1.portal_refl_idle");
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
    self.chkLcpVignette.title = heraw_i18n::tr_ns("t4.lcp_vig");
    self.chkLcpDistortion.title = heraw_i18n::tr_ns("t4.lcp_dist");
    self.btnExportLcp.enabled = [self selectedLcpFieldsCsv].length > 0;

    std::string tStr = std::isfinite(self.opticsModel.sensorTempC)
        ? heraw_i18n::trf("%.1f °C", self.opticsModel.sensorTempC) : "--";
    std::string wStr = self.opticsModel.warmPixels >= 0
        ? std::to_string(self.opticsModel.warmPixels) : "--";
    std::string sigma = self.opticsModel.sigma0 >= 0
        ? heraw_i18n::trf("%.5f", self.opticsModel.sigma0) : "--";
    std::string gain = self.opticsModel.gainG >= 0
        ? heraw_i18n::trf("%.5f", self.opticsModel.gainG) : "--";
    self.lblSiliconReadout.stringValue = heraw_i18n::trf_ns("t4.silicon_fmt",
        sigma.c_str(), gain.c_str(), tStr.c_str(), wStr.c_str());
    self.lblOisStatus.stringValue = self.opticsModel.oisSummary ?: heraw_i18n::tr_ns("t4.ois_idle");

    // 10. Bottom Morphing Run Button Summary
    [self syncCoreCountPopupFromThreads];
    [self updateRunButtonTitle];
    [self updateGroupingHint];
}

- (BOOL)hasStrayGroups {
    int minReq = [self minValidFramesRequired];
    for (BurstGroupModel* g in self.burstGroups) {
        if (g.isStrayArchive || g.validFrameCount < minReq) return YES;
    }
    return NO;
}

- (void)updateRunButtonTitle {
    BOOL groupingEditable = self.currentTask == nil && self.planTask == nil;
    self.popGrouping.enabled = groupingEditable;
    self.btnRegroup.enabled = groupingEditable && self.rawInputPaths.count > 0;
    self.edtGrpSz.enabled = groupingEditable && self.popGrouping.indexOfSelectedItem != 1;
    self.btnMergeGroups.enabled = groupingEditable && self.tblQueue.selectedRowIndexes.count >= 2;
    self.btnCleanStray.enabled = groupingEditable && [self hasStrayGroups];
    BurstGroupModel* curG = [self selectedBurstGroup];
    NSUInteger selFrmCnt = self.tblFrames.selectedRowIndexes.count;
    self.btnSplitFrames.enabled = groupingEditable && curG != nil && selFrmCnt >= 1 && selFrmCnt < curG.frames.count;
    self.btnRun.enabled = self.currentTask != nil || (!self.groupingDirty && self.planTask == nil);
    if (self.currentTask) {
        self.btnRun.stopMode = YES;
        self.btnRun.title = heraw_i18n::tr_ns("bot.stop");
        [self.btnRun setNeedsDisplay:YES];
        return;
    }
    self.btnRun.stopMode = NO;

    NSUInteger checkedCnt = 0;
    for (BurstGroupModel* g in self.burstGroups) {
        if (g.checked) ++checkedCnt;
    }

    NSMutableArray<NSString*>* tags = [NSMutableArray array];
    if (self.chkEmitRgb.state == NSControlStateValueOn) {
        NSString* sc = [self selectedRgbScaleCode];
        if ([sc isEqualToString:@"1x"]) [tags addObject:heraw_i18n::tr_ns("bot.tag_1x")];
        else if ([sc isEqualToString:@"both"]) [tags addObject:heraw_i18n::tr_ns("bot.tag_both")];
    }
    if ([self.motionMode isEqualToString:@"clean-plate"]) [tags addObject:heraw_i18n::tr_ns("bot.tag_clean")];
    else if ([self.motionMode isEqualToString:@"virtual-nd"]) [tags addObject:heraw_i18n::tr_ns("bot.tag_vnd")];
    else if ([self.motionMode isEqualToString:@"strobe"]) [tags addObject:heraw_i18n::tr_ns("bot.tag_strobe")];

    BurstGroupModel* selGrp = [self selectedBurstGroup];
    BOOL depthAllowed = !selGrp || !selGrp.depthKnown || selGrp.depthObservable;
    if (self.chkEmitTransientTab1.state == NSControlStateValueOn) [tags addObject:heraw_i18n::tr_ns("bot.tag_trans")];
    if (self.chkReflection.state == NSControlStateValueOn) [tags addObject:heraw_i18n::tr_ns("bot.tag_refl")];
    if (self.chkEmitField.state == NSControlStateValueOn) {
        [tags addObject:heraw_i18n::trf_ns("bot.tag_fields_fmt", (int)[self selectedFieldCount])];
    }
    if (depthAllowed && self.chkExportParallax.state == NSControlStateValueOn) [tags addObject:heraw_i18n::tr_ns("bot.tag_25d")];
    if (depthAllowed && self.chkExportRefocus.state == NSControlStateValueOn) [tags addObject:heraw_i18n::tr_ns("bot.tag_refocus")];
    if (self.chkExportTiles.state == NSControlStateValueOn) [tags addObject:heraw_i18n::tr_ns("bot.tag_tiles")];

    NSString* tagSummary = (tags.count > 0) ? [tags componentsJoinedByString:@"+"] : nil;
    if (checkedCnt > 0 && tagSummary) {
        self.btnRun.title = heraw_i18n::trf_ns("bot.run_grp_tag_fmt", (int)checkedCnt, [tagSummary UTF8String]);
    } else if (checkedCnt > 0) {
        self.btnRun.title = heraw_i18n::trf_ns("bot.run_grp_fmt", (int)checkedCnt);
    } else if (tagSummary) {
        self.btnRun.title = heraw_i18n::trf_ns("bot.run_tag_fmt", [tagSummary UTF8String]);
    } else {
        self.btnRun.title = heraw_i18n::tr_ns("bot.run");
    }
    [self.btnRun setNeedsDisplay:YES];
}

// ============================================================================
// Persistence (shared UI settings, version-scoped engine paths)
// ============================================================================

- (void)saveUiPrefs {
    [self saveUiPrefsFlush:YES];
}

- (void)saveUiPrefsFlush:(BOOL)flush {
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
    [ud setBool:(self.chkNoToolExif.state == NSControlStateValueOn) forKey:@"NoToolExif"];

    [ud setBool:(self.chkEmitRgb.state == NSControlStateValueOn) forKey:@"EmitRgb"];
    [ud setObject:[self selectedRgbScaleCode] forKey:@"RgbScaleCode"];
    [ud setObject:[self selectedRgbFormatCode] forKey:@"RgbFmtCode"];
    if (self.langCode.length > 0) [ud setObject:self.langCode forKey:@"LangCode"];
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
    NSInteger gIdx = self.popGrouping.indexOfSelectedItem;
    NSString* gCode = (gIdx == 1) ? @"single" : (gIdx == 2 ? @"best" : (gIdx == 3 ? @"top1" : @"auto"));
    [ud setObject:gCode forKey:@"GroupingMode"];
    [ud setObject:self.edtThreads.stringValue forKey:@"Threads"];

    if (self.window) {
        [ud setObject:[self.window stringWithSavedFrame] forKey:@"WindowFrame"];
    }
    if (flush) [ud synchronize];
}

- (void)loadUiPrefs {
    NSUserDefaults* ud = nijinPrefs();
    if (![ud boolForKey:@"HasSavedUiPrefs"]) return;

    self.suppressPrefSave = YES;
    if ([ud objectForKey:@"LcpLca"]) self.chkLcpLca.state = [ud boolForKey:@"LcpLca"] ? NSControlStateValueOn : NSControlStateValueOff;
    if ([ud objectForKey:@"LcpVignette"]) self.chkLcpVignette.state = [ud boolForKey:@"LcpVignette"] ? NSControlStateValueOn : NSControlStateValueOff;
    if ([ud objectForKey:@"LcpDistortion"]) self.chkLcpDistortion.state = [ud boolForKey:@"LcpDistortion"] ? NSControlStateValueOn : NSControlStateValueOff;

    if ([ud objectForKey:@"SortTime"])  self.chkSortTime.state  = [ud boolForKey:@"SortTime"]  ? NSControlStateValueOn : NSControlStateValueOff;
    if ([ud objectForKey:@"Recursive"]) self.chkRecursive.state = [ud boolForKey:@"Recursive"] ? NSControlStateValueOn : NSControlStateValueOff;

    // Preset
    NSString* pCode = [ud stringForKey:@"PresetCode"];
    if (pCode) {
        for (int i = 0; i < 4; ++i) {
            if ([pCode isEqualToString:[NSString stringWithUTF8String:kPresets[i].code]]) {
                [self applyPresetIndex:i];
                break;
            }
        }
    }

    NSString* mMode = [ud stringForKey:@"MotionMode"];
    if (mMode.length > 0) {
        self.motionMode = mMode;
    }

    NSString* olpf = [ud stringForKey:@"OlpfMode"];
    NSUInteger olpfIdx = olpf ? [olpfModeCodes() indexOfObject:olpf] : NSNotFound;
    if (olpfIdx != NSNotFound) [self.popOlpf selectItemAtIndex:(NSInteger)olpfIdx];

    if ([ud objectForKey:@"EmitRaw"]) self.chkEmitRaw.state = [ud boolForKey:@"EmitRaw"] ? NSControlStateValueOn : NSControlStateValueOff;
    NSString* rCode = [ud stringForKey:@"RawFmtCode"];
    NSUInteger rIdx = rCode ? [rawFormatCodes() indexOfObject:rCode] : NSNotFound;
    if (rIdx != NSNotFound) [self.popRawFmt selectItemAtIndex:(NSInteger)rIdx];
    if ([ud objectForKey:@"RelayRaw"]) self.chkRelayRaw.state = [ud boolForKey:@"RelayRaw"] ? NSControlStateValueOn : NSControlStateValueOff;
    if ([ud objectForKey:@"NoToolExif"]) self.chkNoToolExif.state = [ud boolForKey:@"NoToolExif"] ? NSControlStateValueOn : NSControlStateValueOff;

    if ([ud objectForKey:@"EmitRgb"]) self.chkEmitRgb.state = [ud boolForKey:@"EmitRgb"] ? NSControlStateValueOn : NSControlStateValueOff;
    NSString* scCode = [ud stringForKey:@"RgbScaleCode"];
    NSUInteger scIdx = scCode ? [rgbScaleCodes() indexOfObject:scCode] : NSNotFound;
    if (scIdx != NSNotFound && self.segRgbScale) self.segRgbScale.selectedSegment = (NSInteger)scIdx;
    NSString* cCode = [ud stringForKey:@"RgbFmtCode"];
    NSUInteger cIdx = cCode ? [rgbFormatCodes() indexOfObject:cCode] : NSNotFound;
    if (cIdx != NSNotFound) [self.popRgbFmt selectItemAtIndex:(NSInteger)cIdx];

    NSString* savedLang = [ud stringForKey:@"LangCode"];
    if (savedLang.length > 0) {
        self.langCode = savedLang;
        heraw_i18n::I18nManager::instance().set_language([savedLang UTF8String]);
        [self applyUiLanguage];
    }

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
    NSString* gmSaved = [ud stringForKey:@"GroupingMode"];
    NSInteger gmIdx = [gmSaved isEqualToString:@"single"] ? 1
                    : ([gmSaved isEqualToString:@"best"]  ? 2
                    : ([gmSaved isEqualToString:@"top1"]  ? 3 : 0));
    [self.popGrouping selectItemAtIndex:gmIdx];
    loadKnob(@"Threads",   self.edtThreads, nil);

    NSInteger savedTab = [ud integerForKey:@"ActiveTab"];
    [self selectTab:savedTab];

    NSString* wf = [ud stringForKey:@"WindowFrame"];
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
            @"nef", @"nrw", @"nev", @"arw", @"srf", @"sr2", @"cr2", @"cr3", @"crw",
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
        NSString* autoDir = [base stringByAppendingPathComponent:@"nijinLBS_out"];
        self.edtOutDir.stringValue = autoDir;
        self.autoOutDir = autoDir;
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
        self.groupingDirty = NO;
        [self.burstGroups removeAllObjects];
        [self.tblQueue reloadData];
        [self.tblFrames reloadData];
        [self updateQueueEmptyVisibility];
        [self syncAllUiState];
        return;
    }

    NSInteger grpMode = self.popGrouping.indexOfSelectedItem;
    int minReq = [self minValidFramesRequired];
    if (grpMode != 1 && self.edtGrpSz.integerValue < minReq) {
        self.groupingDirty = YES;
        [self updateRunButtonTitle];
        self.lblGroupingHint.stringValue = heraw_i18n::trf_ns("hint.min_grp_sz_fmt", minReq);
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
            g.checked = ((int)allFiles.count >= minReq);
            [g.filePaths addObjectsFromArray:allFiles];
            RawQuickMeta qm = peekRawMeta(allFiles.firstObject);
            g.cameraModel = qm.model.empty() ? heraw_i18n::tr_ns("qcell.raw_seq_model") : [NSString stringWithUTF8String:qm.model.c_str()];
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
    self.lblStageStatus.stringValue = heraw_i18n::tr_ns("st.scanning");

    NSMutableArray<NSString*>* args = [NSMutableArray arrayWithObject:@"--plan-json"];
    BOOL singleGroup = (grpMode == 1);
    BOOL bestPerBurst = (grpMode == 2 || grpMode == 3);
    NSString* policyStr = singleGroup ? @"force-all" : (bestPerBurst ? @"auto-prune" : @"multi-group");
    [args addObjectsFromArray:@[@"--policy", policyStr,
                               @"--group-size", self.edtGrpSz.stringValue]];
    if (bestPerBurst) {
        [args addObjectsFromArray:@[@"--max-frames", self.edtGrpSz.stringValue]];
    }
    if (singleGroup) [args addObject:@"--single-group"];
    if (minReq == 3) {
        [args addObjectsFromArray:@[@"--rgb-scale", @"1x"]];
    }
    if (self.edtThreads.integerValue > 0) {
        [args addObjectsFromArray:@[@"--threads", self.edtThreads.stringValue]];
    }
    self.groupingDirty = NO;
    if (self.customLibRawPath.length > 0) {
        [args addObjectsFromArray:@[@"--libraw", self.customLibRawPath]];
    }
    [args addObject:(self.chkSortTime.state == NSControlStateValueOn) ? @"--sort-time" : @"--no-sort-time"];
    if (self.chkRecursive.state == NSControlStateValueOn) [args addObject:@"--recursive"];
    [args addObjectsFromArray:self.rawInputPaths];

    std::vector<std::string> argVec;
    argVec.reserve(args.count);
    for (NSString* a in args) argVec.emplace_back([a UTF8String] ?: "");
    int logTaskId = heraw_i18n::diag_log().begin_task("plan-json", [cli UTF8String] ?: "", argVec);

    NSTask* task = [[NSTask alloc] init];
    task.executableURL = [NSURL fileURLWithPath:cli];
    task.arguments = args;
    NSPipe* outPipe = [NSPipe pipe];
    task.standardOutput = outPipe;
    task.standardError = outPipe;
    self.planTask = task;
    [self updateRunButtonTitle];

    NSDictionary<NSString*, NSString*>* baseSnapshot = [self.fileByBasename copy];
    __weak AppDelegate* weakSelf = self;

    NSError* launchErr = nil;
    if (![task launchAndReturnError:&launchErr]) {
        heraw_i18n::diag_log().end_task(logTaskId, -1);
        self.planTask = nil;
        self.emptyWell.scanning = NO;
        self.lblStageStatus.stringValue = launchErr.localizedDescription;
        self.groupingDirty = YES;
        [self updateRunButtonTitle];
        return;
    }
    dispatch_async(dispatch_get_global_queue(QOS_CLASS_USER_INITIATED, 0), ^{
        NSData* data = [[outPipe fileHandleForReading] readDataToEndOfFile];
        [task waitUntilExit];
        std::string jsonStr((const char*)data.bytes, data.length);
        heraw_i18n::diag_log().append_task_output(logTaskId, jsonStr.data(), jsonStr.size());
        heraw_i18n::diag_log().end_task(logTaskId, task.terminationStatus);
        NSArray<BurstGroupModel*>* parsed = [weakSelf parsePlanJsonOutput:jsonStr basenameMap:baseSnapshot];

        dispatch_async(dispatch_get_main_queue(), ^{
            AppDelegate* s = weakSelf;
            if (!s || s.planSerial != mySerial) return;
            s.planTask = nil;
            s.groupingDirty = task.terminationStatus != 0;
            s.emptyWell.scanning = NO;
            [s.emptyWell setNeedsDisplay:YES];
            [s.burstGroups setArray:parsed];
            if (grpMode == 3 && s.burstGroups.count > 0) {
                [s selectOnlySharpestGroup];
            }
            [s.tblQueue reloadData];
            if (s.burstGroups.count > 0) {
                NSUInteger firstActive = 0;
                for (NSUInteger gi = 0; gi < s.burstGroups.count; ++gi) {
                    if (s.burstGroups[gi].checked) { firstActive = gi; break; }
                }
                [s.tblQueue selectRowIndexes:[NSIndexSet indexSetWithIndex:firstActive] byExtendingSelection:NO];
            }
            [s.tblFrames reloadData];
            [s updateQueueEmptyVisibility];
            s.lblStageStatus.stringValue = task.terminationStatus == 0
                ? heraw_i18n::trf_ns("st.ready_groups_fmt", (int)s.burstGroups.count)
                : heraw_i18n::trf_ns("st.plan_failed_fmt", task.terminationStatus);
            [s syncAllUiState];
            [s updateGroupingHint];
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
        double fnumJson       = extractJsonDouble(cl, "aperture", extractJsonDouble(cl, "f_number", 0.0));
        int isoJson           = extractJsonInt(cl, "iso", 0);
        int bestAnc           = extractJsonInt(cl, "best_anchor", 1);
        int quadCov           = extractJsonInt(cl, "quad_coverage", 1);
        int srScoreJson       = extractJsonInt(cl, "sr_score", 80);
        double recDen         = extractJsonDouble(cl, "rec_denoise", 1.0);
        double recDegh        = extractJsonDouble(cl, "rec_deghost", 1.0);
        int recPsf            = extractJsonInt(cl, "rec_psf_iters", 4);
        bool shock1           = extractJsonBool(cl, "shutter_shock", false);
        double drift2xJson    = extractJsonDouble(cl, "max_drift_2x_px", -1.0);
        double para2xJson     = extractJsonDouble(cl, "max_parallax_2x_px", -1.0);
        bool hasDepthKey      = cl.find("\"depth_observable\"") != std::string::npos;
        bool depthObsJson     = hasDepthKey ? extractJsonBool(cl, "depth_observable", false) : false;
        NSMutableDictionary<NSString*, NSNumber*>* fieldObsDict = nil;
        size_t foKeyPos = cl.find("\"field_observability\"");
        if (foKeyPos != std::string::npos) {
            size_t ob = cl.find('{', foKeyPos);
            size_t cb = (ob != std::string::npos) ? cl.find('}', ob) : std::string::npos;
            if (ob != std::string::npos && cb != std::string::npos && cb > ob) {
                std::string foSub = cl.substr(ob, cb - ob + 1);
                fieldObsDict = [NSMutableDictionary dictionary];
                for (int k = 0; k < 11; ++k) {
                    std::string pat = std::string("\"") + kFieldSpecs[k].code + "\"";
                    if (foSub.find(pat) != std::string::npos) {
                        bool v = extractJsonBool(foSub, kFieldSpecs[k].code, false);
                        fieldObsDict[[NSString stringWithUTF8String:kFieldSpecs[k].code]] = @(v);
                    }
                }
            }
        }

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
                            fFlags.find("BLUR") != std::string::npos ||
                            fFlags.find("EXP_OUTLIER") != std::string::npos ||
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
            gm.srScore      = srScoreJson;
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

            int minReq = [self minValidFramesRequired];
            gm.validFrameCount = validCnt;
            gm.checked = (validCnt >= minReq && gm.filePaths.count == gm.frames.count);
            gm.maxDrift2xPx = (drift2xJson >= 0.0) ? drift2xJson : (subMaxShift1x * 2.0);
            gm.maxParallax2xPx = (para2xJson >= 0.0) ? para2xJson : 0.0;
            gm.depthKnown = NO;
            gm.depthObservable = depthObsJson;
            gm.hasMeasuredParallax = NO;
            (void)fieldObsDict;

            RawQuickMeta qm = peekRawMeta(gm.filePaths.firstObject);
            gm.cameraModel = !modelJson.empty() ? [NSString stringWithUTF8String:modelJson.c_str()]
                           : (!qm.model.empty() ? [NSString stringWithUTF8String:qm.model.c_str()] : heraw_i18n::tr_ns("qcell.raw_seq_model"));
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

    // Fold isolated unassigned 1-frame single shots into a single neat archive row during mixed-library imports
    int minReqFold = [self minValidFramesRequired];
    NSUInteger validBurstCnt = 0;
    NSMutableArray<BurstGroupModel*>* singleStrays = [NSMutableArray array];
    for (BurstGroupModel* gm in outGroups) {
        if ((int)gm.frames.count >= minReqFold || gm.checked) {
            ++validBurstCnt;
        } else if (gm.frames.count == 1 && !gm.checked && gm.frames.firstObject.assignedGroup < 0) {
            [singleStrays addObject:gm];
        }
    }
    if ((validBurstCnt >= 1 && singleStrays.count >= 2) || singleStrays.count >= 4) {
        BurstGroupModel* archive = [[BurstGroupModel alloc] init];
        archive.isStrayArchive = YES;
        archive.checked = NO;
        archive.sceneCode = @"single_shot";
        archive.recPreset = @"photo";
        archive.cameraModel = heraw_i18n::tr_ns("qcell.stray_model");
        archive.cfaType = singleStrays.firstObject.cfaType ?: @"bayer";
        archive.shutterType = singleStrays.firstObject.shutterType ?: @"mechanical";
        int fIdx = 1;
        int vCnt = 0;
        for (BurstGroupModel* sg in singleStrays) {
            for (FrameDiagItem* it in sg.frames) {
                it.idx1Based = fIdx++;
                it.isCrown = NO;
                [archive.frames addObject:it];
                if (it.fullPath.length > 0) [archive.filePaths addObject:it.fullPath];
                if (it.enabled) ++vCnt;
            }
            [outGroups removeObjectIdenticalTo:sg];
        }
        archive.validFrameCount = vCnt;
        [outGroups addObject:archive];
        for (NSUInteger i = 0; i < outGroups.count; ++i) {
            outGroups[i].displayIndex = (int)(i + 1);
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

- (NSTableRowView*)tableView:(NSTableView*)tableView rowViewForRow:(NSInteger)row {
    (void)row;
    AquaSoftTableRowView* rv = [tableView makeViewWithIdentifier:@"AquaSoftRow" owner:self];
    if (!rv) {
        rv = [[AquaSoftTableRowView alloc] initWithFrame:NSZeroRect];
        rv.identifier = @"AquaSoftRow";
    }
    return rv;
}

- (void)recalculateGroupMetrics:(BurstGroupModel*)gm {
    if (!gm) return;
    [gm.filePaths removeAllObjects];
    int vCnt = 0;
    double maxAf = -1.0;
    int bestIdx = 1;
    double maxShift1x = 0.0;
    for (NSUInteger i = 0; i < gm.frames.count; ++i) {
        FrameDiagItem* it = gm.frames[i];
        it.idx1Based = (int)(i + 1);
        it.isCrown = NO;
        if (it.fullPath.length > 0) [gm.filePaths addObject:it.fullPath];
        bool isDark = ([it.flags rangeOfString:@"DARK"].location != NSNotFound);
        if (it.enabled && !isDark) ++vCnt;
        if (it.afScore > maxAf) { maxAf = it.afScore; bestIdx = it.idx1Based; }
        maxShift1x = std::max(maxShift1x, it.shiftPx);
    }
    gm.validFrameCount = vCnt;
    gm.bestAnchor = bestIdx;
    if (!gm.isStrayArchive && bestIdx >= 1 && (NSUInteger)bestIdx <= gm.frames.count) {
        gm.frames[(NSUInteger)(bestIdx - 1)].isCrown = YES;
    }
    if (gm.customAnchor1Based > (int)gm.frames.count) gm.customAnchor1Based = 0;
    if (gm.maxDrift2xPx <= 0.0) gm.maxDrift2xPx = maxShift1x * 2.0;
}

- (void)syncRawInputsFromGroups {
    [self.rawInputPaths removeAllObjects];
    NSMutableSet<NSString*>* seen = [NSMutableSet set];
    for (NSUInteger i = 0; i < self.burstGroups.count; ++i) {
        BurstGroupModel* g = self.burstGroups[i];
        g.displayIndex = (int)(i + 1);
        for (NSString* p in g.filePaths) {
            if (p.length > 0 && ![seen containsObject:p]) {
                [seen addObject:p];
                [self.rawInputPaths addObject:p];
            }
        }
    }
}

- (double)qualityRankForGroup:(BurstGroupModel*)g {
    if (!g || g.isStrayArchive) return -1e9;
    int minReq = [self minValidFramesRequired];
    double maxAf = 0.0;
    for (FrameDiagItem* f in g.frames) maxAf = std::max(maxAf, f.afScore);
    return (g.validFrameCount >= minReq ? 100000.0 : 0.0) + (double)g.srScore * 100.0 + (double)g.validFrameCount * 5.0 + maxAf;
}

- (void)selectOnlySharpestGroup {
    if (self.burstGroups.count == 0) return;
    BurstGroupModel* best = nil;
    double bestRank = -1e12;
    for (BurstGroupModel* g in self.burstGroups) {
        double r = [self qualityRankForGroup:g];
        if (r > bestRank) { bestRank = r; best = g; }
    }
    for (BurstGroupModel* g in self.burstGroups) {
        g.checked = (g == best && !g.isStrayArchive && g.validFrameCount >= 1);
    }
}

- (void)applyGroupScenePrescription:(BurstGroupModel*)g {
    if (!g) return;
    for (int i = 0; i < 4; ++i) {
        if ([g.recPreset isEqualToString:[NSString stringWithUTF8String:kPresets[i].code]]) {
            [self applyPresetIndex:i];
            self.sldDenoise.doubleValue = g.recDenoise;
            self.edtDenoise.stringValue = [NSString stringWithFormat:@"%.2f", g.recDenoise];
            self.sldDeghost.doubleValue = g.recDeghost;
            self.edtDeghost.stringValue = [NSString stringWithFormat:@"%.2f", g.recDeghost];
            self.sldPsfIters.integerValue = g.recPsfIters;
            self.edtPsfIters.stringValue = [NSString stringWithFormat:@"%d", g.recPsfIters];
            if ([g.recAnchor isEqualToString:@"middle"]) self.motionMode = @"middle";
            [self syncAllUiState];
            [self saveUiPrefs];
            break;
        }
    }
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
        if (g.isStrayArchive) {
            cell.lblTitle.stringValue = heraw_i18n::trf_ns("qcell.stray_title_fmt",
                (int)g.frames.count, [rangeStr UTF8String] ?: "");
            cell.lblBadge.stringValue = heraw_i18n::tr_ns("qcell.stray_badge");
        } else {
            int minReq = [self minValidFramesRequired];
            std::string countSuffix = (g.validFrameCount < minReq)
                ? heraw_i18n::trf("qcell.short_cnt_fmt", (int)g.frames.count)
                : heraw_i18n::trf("qcell.norm_cnt_fmt", (int)g.frames.count);
            cell.lblTitle.stringValue = heraw_i18n::trf_ns("qcell.group_title_fmt",
                g.displayIndex, countSuffix.c_str(), [rangeStr UTF8String] ?: "");

            if ([g.cfaType isEqualToString:@"xtrans"]) {
                cell.lblBadge.stringValue = @"[X-Trans]";
            } else if ([g.shutterType isEqualToString:@"global"]) {
                cell.lblBadge.stringValue = heraw_i18n::tr_ns("qcell.badge_global");
            } else {
                cell.lblBadge.stringValue = @"[Bayer]";
            }
        }

        NSMutableArray<NSString*>* metaParts = [NSMutableArray array];
        if (g.isStrayArchive) {
            [metaParts addObject:heraw_i18n::tr_ns("qcell.stray_model")];
        } else if (g.cameraModel.length > 0) {
            [metaParts addObject:g.cameraModel];
        }
        if (!g.isStrayArchive) {
            if (g.focalMm > 0.1 && g.fNumber > 0.1) {
                [metaParts addObject:[NSString stringWithFormat:@"%.0fmm f/%.1f", g.focalMm, g.fNumber]];
            } else if (g.focalMm > 0.1) {
                [metaParts addObject:[NSString stringWithFormat:@"%.0fmm", g.focalMm]];
            }
            if (g.iso > 0) [metaParts addObject:[NSString stringWithFormat:@"ISO %d", g.iso]];
        }
        cell.lblMeta.stringValue = [metaParts componentsJoinedByString:@" · "];

        // Contextual Scene Affordance Pill
        NSInteger curPresetIdx = self.segPreset.selectedSegment;
        NSString* curPresetCode = (curPresetIdx >= 0 && curPresetIdx < 4)
            ? [NSString stringWithUTF8String:kPresets[curPresetIdx].code] : @"photo";
        BOOL showScenePill = (!g.isStrayArchive && g.recPreset.length > 0 &&
                              ![g.recPreset isEqualToString:@"photo"] &&
                              ![g.recPreset isEqualToString:curPresetCode]);
        if (showScenePill) {
            const char* pillKey = [g.recPreset isEqualToString:@"astro"] ? "qcell.pill_astro"
                                : ([g.recPreset isEqualToString:@"sci"]  ? "qcell.pill_sci" : "qcell.pill_arc");
            cell.btnScenePill.title = heraw_i18n::tr_ns(pillKey);
            cell.btnScenePill.hidden = NO;
        } else {
            cell.btnScenePill.hidden = YES;
        }
        [cell resizeSubviewsWithOldSize:cell.bounds.size];

        cell.onToggleCheck = ^(BOOL on) {
            AppDelegate* s = weakSelf;
            if (!s) return;
            NSIndexSet* selSet = s.tblQueue.selectedRowIndexes;
            if (selSet.count >= 2 && [selSet containsIndex:(NSUInteger)row]) {
                [selSet enumerateIndexesUsingBlock:^(NSUInteger idx, BOOL* stop) {
                    (void)stop;
                    if (idx < s.burstGroups.count) s.burstGroups[idx].checked = on;
                }];
                [s.tblQueue reloadDataForRowIndexes:selSet columnIndexes:[NSIndexSet indexSetWithIndex:0]];
            } else {
                g.checked = on;
            }
            [s updateRunButtonTitle];
            [s updateGroupingHint];
        };
        cell.onApplyScene = ^{
            AppDelegate* s = weakSelf;
            if (!s) return;
            [s applyGroupScenePrescription:g];
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

    fc.btnAnchorDot.toolTip = heraw_i18n::tr_ns("dock.tip_anchor_dot");
    if ([f.flags rangeOfString:@"DARK"].location != NSNotFound) {
        fc.lblTag.stringValue = heraw_i18n::tr_ns("ftag.dark");
        fc.lblTag.textColor = [NSColor colorWithSRGBRed:0.52 green:0.30 blue:0.28 alpha:1.0];
    } else if ([f.flags rangeOfString:@"BLUR"].location != NSNotFound) {
        fc.lblTag.stringValue = heraw_i18n::tr_ns("ftag.blur");
        fc.lblTag.textColor = [NSColor colorWithSRGBRed:0.70 green:0.38 blue:0.16 alpha:1.0];
    } else if ([f.flags rangeOfString:@"EXP"].location != NSNotFound) {
        fc.lblTag.stringValue = heraw_i18n::tr_ns("ftag.exp");
        fc.lblTag.textColor = [NSColor colorWithSRGBRed:0.70 green:0.38 blue:0.16 alpha:1.0];
    } else if (f.isShock) {
        fc.lblTag.stringValue = heraw_i18n::tr_ns("ftag.shock");
        fc.lblTag.textColor = [NSColor colorWithSRGBRed:0.68 green:0.42 blue:0.14 alpha:1.0];
    } else if (isLockedAnchor) {
        fc.lblTag.stringValue = heraw_i18n::tr_ns("ftag.anchor");
        fc.lblTag.textColor = [NSColor colorWithSRGBRed:0.18 green:0.44 blue:0.53 alpha:1.0];
    } else {
        fc.lblTag.stringValue = @"";
    }

    fc.onToggleFrame = ^(BOOL on) {
        AppDelegate* s = weakSelf;
        if (!s) return;
        NSIndexSet* fSel = s.tblFrames.selectedRowIndexes;
        if (fSel.count >= 2 && [fSel containsIndex:(NSUInteger)row]) {
            [fSel enumerateIndexesUsingBlock:^(NSUInteger idx, BOOL* stop) {
                (void)stop;
                if (idx < g.frames.count) {
                    FrameDiagItem* item = g.frames[idx];
                    item.enabled = on;
                    if (!on && g.customAnchor1Based == item.idx1Based) g.customAnchor1Based = 0;
                }
            }];
        } else {
            f.enabled = on;
            if (!on && g.customAnchor1Based == f.idx1Based) g.customAnchor1Based = 0;
        }
        g.hasMeasuredParallax = NO;
        g.depthKnown = NO;
        g.fieldObservability = nil;
        [s recalculateGroupMetrics:g];
        [s.tblFrames reloadData];
        if (fSel.count > 0) [s.tblFrames selectRowIndexes:fSel byExtendingSelection:NO];
        NSUInteger gRow = [s.burstGroups indexOfObjectIdenticalTo:g];
        if (gRow != NSNotFound) {
            [s.tblQueue reloadDataForRowIndexes:[NSIndexSet indexSetWithIndex:gRow]
                                  columnIndexes:[NSIndexSet indexSetWithIndex:0]];
        }
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
            [s recalculateGroupMetrics:g];
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

- (void)refreshInspectorHeaderTitle {
    BurstGroupModel* g = [self selectedBurstGroup];
    NSUInteger selCnt = self.tblQueue.selectedRowIndexes.count;
    if (selCnt >= 2 && g) {
        self.lblInspectorHeader.stringValue = heraw_i18n::trf_ns("dock.insp_hdr_multi_fmt", g.displayIndex, (int)selCnt);
    } else if (g) {
        self.lblInspectorHeader.stringValue = g.isStrayArchive
            ? heraw_i18n::tr_ns("dock.insp_hdr_stray")
            : heraw_i18n::trf_ns("dock.insp_hdr_group_fmt", g.displayIndex);
    } else {
        self.lblInspectorHeader.stringValue = heraw_i18n::tr_ns("dock.insp_hdr");
    }
}

- (void)tableViewSelectionDidChange:(NSNotification*)notification {
    if (notification.object == self.tblQueue) {
        self.opticsModel = [[OpticsTelemetryModel alloc] init];
        [self refreshInspectorHeaderTitle];
        [self.tblFrames reloadData];
        [self updateQueueEmptyVisibility];
        [self syncAllUiState];
    } else if (notification.object == self.tblFrames) {
        [self updateRunButtonTitle];
    }
}

// ============================================================================
// UI Control Actions
// ============================================================================

- (void)onOpenMixedPanel:(id)sender {
    (void)sender;
    NSOpenPanel* op = [NSOpenPanel openPanel];
    op.title = heraw_i18n::tr_ns("dlg.pick_raw_mix");
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
    op.title = heraw_i18n::tr_ns("dlg.pick_raw_files");
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
    op.title = heraw_i18n::tr_ns("dlg.pick_raw_dir");
    op.canChooseFiles = NO;
    op.canChooseDirectories = YES;
    op.allowsMultipleSelection = YES;
    if ([op runModal] == NSModalResponseOK) {
        NSMutableArray<NSString*>* arr = [NSMutableArray array];
        for (NSURL* u in op.URLs) if (u.path) [arr addObject:u.path];
        [self addInputPaths:arr];
    }
}

- (void)onToggleSelectedGroupsChecked:(id)sender {
    (void)sender;
    NSIndexSet* sel = self.tblQueue.selectedRowIndexes;
    if (sel.count == 0) return;
    __block BOOL allOn = YES;
    [sel enumerateIndexesUsingBlock:^(NSUInteger idx, BOOL* stop) {
        if (idx < self.burstGroups.count && !self.burstGroups[idx].checked) { allOn = NO; *stop = YES; }
    }];
    BOOL target = !allOn;
    [sel enumerateIndexesUsingBlock:^(NSUInteger idx, BOOL* stop) {
        (void)stop;
        if (idx < self.burstGroups.count) self.burstGroups[idx].checked = target;
    }];
    [self.tblQueue reloadDataForRowIndexes:sel columnIndexes:[NSIndexSet indexSetWithIndex:0]];
    [self syncAllUiState];
}

- (void)onToggleSelectedFramesEnabled:(id)sender {
    (void)sender;
    BurstGroupModel* g = [self selectedBurstGroup];
    NSIndexSet* sel = self.tblFrames.selectedRowIndexes;
    if (!g || sel.count == 0) return;
    __block BOOL allOn = YES;
    [sel enumerateIndexesUsingBlock:^(NSUInteger idx, BOOL* stop) {
        if (idx < g.frames.count && !g.frames[idx].enabled) { allOn = NO; *stop = YES; }
    }];
    BOOL target = !allOn;
    [sel enumerateIndexesUsingBlock:^(NSUInteger idx, BOOL* stop) {
        (void)stop;
        if (idx < g.frames.count) {
            g.frames[idx].enabled = target;
            if (!target && g.customAnchor1Based == g.frames[idx].idx1Based) g.customAnchor1Based = 0;
        }
    }];
    [self recalculateGroupMetrics:g];
    [self.tblFrames reloadData];
    [self.tblFrames selectRowIndexes:sel byExtendingSelection:NO];
    NSUInteger gRow = [self.burstGroups indexOfObjectIdenticalTo:g];
    if (gRow != NSNotFound) {
        [self.tblQueue reloadDataForRowIndexes:[NSIndexSet indexSetWithIndex:gRow]
                              columnIndexes:[NSIndexSet indexSetWithIndex:0]];
    }
    [self syncAllUiState];
}

- (void)onRemoveSelectedGroup:(id)sender {
    (void)sender;
    if (self.currentTask) return;
    NSMutableIndexSet* toRemove = [self.tblQueue.selectedRowIndexes mutableCopy];
    if (toRemove.count == 0 && self.tblQueue.selectedRow >= 0) {
        [toRemove addIndex:(NSUInteger)self.tblQueue.selectedRow];
    }
    [toRemove removeIndexesInRange:NSMakeRange(self.burstGroups.count, NSNotFound - self.burstGroups.count)];
    if (toRemove.count == 0) return;

    NSUInteger firstRemoved = toRemove.firstIndex;
    [self.burstGroups removeObjectsAtIndexes:toRemove];
    [self syncRawInputsFromGroups];
    [self.tblQueue reloadData];
    if (self.burstGroups.count > 0) {
        NSUInteger nextSel = std::min(firstRemoved, self.burstGroups.count - 1);
        [self.tblQueue selectRowIndexes:[NSIndexSet indexSetWithIndex:nextSel] byExtendingSelection:NO];
    }
    [self.tblFrames reloadData];
    [self updateQueueEmptyVisibility];
    [self syncAllUiState];
}

- (void)onMergeSelectedGroups:(id)sender {
    (void)sender;
    if (self.currentTask || self.planTask) return;
    NSIndexSet* sel = self.tblQueue.selectedRowIndexes;
    if (sel.count < 2) return;

    NSUInteger anchorPos = sel.firstIndex;
    if (anchorPos >= self.burstGroups.count) return;
    BurstGroupModel* merged = self.burstGroups[anchorPos];
    BOOL wasStray = merged.isStrayArchive;
    merged.isStrayArchive = NO;
    if ([merged.sceneCode isEqualToString:@"single_shot"]) merged.sceneCode = @"handheld_standard";
    if (wasStray) {
        RawQuickMeta qm = peekRawMeta(merged.filePaths.firstObject);
        merged.cameraModel = !qm.model.empty() ? [NSString stringWithUTF8String:qm.model.c_str()] : heraw_i18n::tr_ns("qcell.raw_seq_model");
    }

    NSMutableSet<NSString*>* seenPaths = [NSMutableSet setWithArray:merged.filePaths];
    NSMutableIndexSet* absorb = [[NSMutableIndexSet alloc] init];
    [sel enumerateIndexesUsingBlock:^(NSUInteger idx, BOOL* stop) {
        (void)stop;
        if (idx == anchorPos || idx >= self.burstGroups.count) return;
        BurstGroupModel* other = self.burstGroups[idx];
        for (FrameDiagItem* f in other.frames) {
            if (f.fullPath.length > 0 && [seenPaths containsObject:f.fullPath]) continue;
            if (f.fullPath.length > 0) [seenPaths addObject:f.fullPath];
            [merged.frames addObject:f];
        }
        merged.maxDrift2xPx = std::max(merged.maxDrift2xPx, other.maxDrift2xPx);
        merged.maxParallax2xPx = std::max(merged.maxParallax2xPx, other.maxParallax2xPx);
        merged.quadCoverage = std::max(merged.quadCoverage, other.quadCoverage);
        [absorb addIndex:idx];
    }];
    merged.hasMeasuredParallax = NO;
    merged.depthKnown = NO;
    merged.fieldObservability = nil;

    [self.burstGroups removeObjectsAtIndexes:absorb];
    [self recalculateGroupMetrics:merged];
    int minReq = [self minValidFramesRequired];
    merged.checked = (merged.validFrameCount >= minReq && merged.filePaths.count == merged.frames.count);
    [self syncRawInputsFromGroups];
    [self.tblQueue reloadData];
    NSUInteger mergedIdx = [self.burstGroups indexOfObjectIdenticalTo:merged];
    if (mergedIdx != NSNotFound) {
        [self.tblQueue selectRowIndexes:[NSIndexSet indexSetWithIndex:mergedIdx] byExtendingSelection:NO];
    }
    [self.tblFrames reloadData];
    self.lblStageStatus.stringValue = heraw_i18n::trf_ns("st.merged_fmt",
        (int)sel.count, merged.displayIndex, (int)merged.frames.count);
    [self syncAllUiState];
}

- (void)onSplitSelectedFrames:(id)sender {
    (void)sender;
    if (self.currentTask || self.planTask) return;
    BurstGroupModel* src = [self selectedBurstGroup];
    NSIndexSet* fSel = self.tblFrames.selectedRowIndexes;
    if (!src || fSel.count == 0 || fSel.count >= src.frames.count) return;

    BurstGroupModel* dst = [[BurstGroupModel alloc] init];
    dst.isStrayArchive = NO;
    dst.cameraModel = src.isStrayArchive
        ? (^{ RawQuickMeta qm = peekRawMeta(src.filePaths.firstObject);
              return !qm.model.empty() ? [NSString stringWithUTF8String:qm.model.c_str()] : heraw_i18n::tr_ns("qcell.raw_seq_model"); }())
        : src.cameraModel;
    dst.cfaType = src.cfaType;
    dst.shutterType = src.shutterType;
    dst.oisEnabled = src.oisEnabled;
    dst.focalMm = src.focalMm;
    dst.fNumber = src.fNumber;
    dst.iso = src.iso;
    dst.sceneCode = [src.sceneCode isEqualToString:@"single_shot"] ? @"handheld_standard" : src.sceneCode;
    dst.recPreset = src.recPreset;
    dst.recAnchor = src.recAnchor;
    dst.recDenoise = src.recDenoise;
    dst.recDeghost = src.recDeghost;
    dst.recPsfIters = src.recPsfIters;
    dst.quadCoverage = src.quadCoverage;
    dst.srScore = src.srScore;
    dst.hasTelemetry = src.hasTelemetry;
    dst.depthKnown = NO;
    dst.depthObservable = src.depthObservable;
    dst.hasMeasuredParallax = NO;
    dst.maxParallax2xPx = src.maxParallax2xPx;
    dst.fieldObservability = nil;

    NSArray<FrameDiagItem*>* extracted = [src.frames objectsAtIndexes:fSel];
    [dst.frames addObjectsFromArray:extracted];
    [src.frames removeObjectsAtIndexes:fSel];
    src.customAnchor1Based = 0;
    src.depthKnown = NO;
    src.hasMeasuredParallax = NO;
    src.fieldObservability = nil;

    [self recalculateGroupMetrics:src];
    [self recalculateGroupMetrics:dst];
    int minReq = [self minValidFramesRequired];
    if (!src.isStrayArchive) src.checked = (src.validFrameCount >= minReq && src.filePaths.count == src.frames.count);
    dst.checked = (dst.validFrameCount >= minReq && dst.filePaths.count == dst.frames.count);

    NSUInteger srcPos = [self.burstGroups indexOfObjectIdenticalTo:src];
    NSUInteger insPos = (srcPos != NSNotFound) ? (srcPos + 1) : self.burstGroups.count;
    [self.burstGroups insertObject:dst atIndex:insPos];
    [self syncRawInputsFromGroups];
    [self.tblQueue reloadData];
    [self.tblQueue selectRowIndexes:[NSIndexSet indexSetWithIndex:insPos] byExtendingSelection:NO];
    [self.tblFrames reloadData];
    self.lblStageStatus.stringValue = heraw_i18n::trf_ns("st.split_fmt",
        (int)dst.frames.count, dst.displayIndex);
    [self syncAllUiState];
}

- (void)onCleanStrayGroups:(id)sender {
    (void)sender;
    if (self.currentTask || self.planTask || self.burstGroups.count == 0) return;
    int minReq = [self minValidFramesRequired];
    NSMutableIndexSet* strays = [NSMutableIndexSet indexSet];
    for (NSUInteger i = 0; i < self.burstGroups.count; ++i) {
        BurstGroupModel* g = self.burstGroups[i];
        if (g.isStrayArchive || g.validFrameCount < minReq) [strays addIndex:i];
    }
    if (strays.count == 0) return;
    NSUInteger removedFrames = 0;
    for (BurstGroupModel* g in [self.burstGroups objectsAtIndexes:strays]) removedFrames += g.frames.count;
    [self.burstGroups removeObjectsAtIndexes:strays];
    [self syncRawInputsFromGroups];
    [self.tblQueue reloadData];
    if (self.burstGroups.count > 0) {
        [self.tblQueue selectRowIndexes:[NSIndexSet indexSetWithIndex:0] byExtendingSelection:NO];
    }
    [self.tblFrames reloadData];
    [self updateQueueEmptyVisibility];
    self.lblStageStatus.stringValue = heraw_i18n::trf_ns("st.cleaned_fmt",
        (int)strays.count, (int)removedFrames);
    [self syncAllUiState];
}

- (void)onKeepOnlySelectedFrames:(id)sender {
    (void)sender;
    BurstGroupModel* g = [self selectedBurstGroup];
    NSIndexSet* sel = self.tblFrames.selectedRowIndexes;
    if (!g || sel.count == 0) return;
    for (NSUInteger i = 0; i < g.frames.count; ++i) {
        g.frames[i].enabled = [sel containsIndex:i];
    }
    g.customAnchor1Based = 0;
    [self recalculateGroupMetrics:g];
    [self.tblFrames reloadData];
    [self.tblFrames selectRowIndexes:sel byExtendingSelection:NO];
    [self.tblQueue reloadData];
    [self syncAllUiState];
}

- (void)onRestoreGroupFrames:(id)sender {
    (void)sender;
    BurstGroupModel* g = [self selectedBurstGroup];
    if (!g) return;
    for (FrameDiagItem* f in g.frames) {
        BOOL isDark = ([f.flags rangeOfString:@"DARK"].location != NSNotFound);
        f.enabled = !isDark && f.fullPath.length > 0;
    }
    [self recalculateGroupMetrics:g];
    [self.tblFrames reloadData];
    [self.tblQueue reloadData];
    [self syncAllUiState];
}

- (void)onIntakeActionClicked:(id)sender {
    (void)sender;
    if (self.burstGroups.count == 0) return;
    int minReq = [self minValidFramesRequired];
    NSInteger curPresetIdx = self.segPreset.selectedSegment;
    NSString* curPresetCode = (curPresetIdx >= 0 && curPresetIdx < 4)
        ? [NSString stringWithUTF8String:kPresets[curPresetIdx].code] : @"photo";

    NSUInteger validGroups = 0, checkedCnt = 0;
    BurstGroupModel* rxGroup = nil;
    for (BurstGroupModel* g in self.burstGroups) {
        if (!g.isStrayArchive && g.validFrameCount >= minReq) ++validGroups;
        if (g.checked) {
            ++checkedCnt;
            if (!rxGroup && g.recPreset.length > 0 && ![g.recPreset isEqualToString:@"photo"] &&
                ![g.recPreset isEqualToString:curPresetCode]) {
                rxGroup = g;
            }
        }
    }

    if (checkedCnt == 0) {
        if (validGroups > 0) {
            for (BurstGroupModel* g in self.burstGroups) {
                g.checked = (!g.isStrayArchive && g.validFrameCount >= minReq);
            }
        } else {
            [self selectOnlySharpestGroup];
        }
    } else if (rxGroup) {
        [self applyGroupScenePrescription:rxGroup];
        return;
    } else if (validGroups >= 2 && checkedCnt >= 2) {
        [self selectOnlySharpestGroup];
    } else if (validGroups >= 2) {
        for (BurstGroupModel* g in self.burstGroups) {
            g.checked = (!g.isStrayArchive && g.validFrameCount >= minReq);
        }
    }
    [self.tblQueue reloadData];
    [self syncAllUiState];
}

- (NSMenu*)buildQueueContextMenuForRow:(NSInteger)clickedRow {
    (void)clickedRow;
    if (self.burstGroups.count == 0) return nil;
    int minReq = [self minValidFramesRequired];
    NSMenu* m = [[NSMenu alloc] initWithTitle:@"QueueMenu"];
    NSIndexSet* sel = self.tblQueue.selectedRowIndexes;

    NSMenuItem* toggleChk = [[NSMenuItem alloc] initWithTitle:(sel.count >= 2
            ? heraw_i18n::tr_ns("qmenu.toggle_multi") : heraw_i18n::tr_ns("qmenu.toggle_one"))
        action:@selector(onToggleSelectedGroupsChecked:) keyEquivalent:@""];
    toggleChk.target = self;
    [m addItem:toggleChk];

    NSMenuItem* onlyBest = [[NSMenuItem alloc] initWithTitle:heraw_i18n::tr_ns("qmenu.only_best")
                                                      action:@selector(onMenuOnlyBestGroup:) keyEquivalent:@""];
    onlyBest.target = self;
    [m addItem:onlyBest];

    NSMenuItem* checkValid = [[NSMenuItem alloc] initWithTitle:heraw_i18n::trf_ns("qmenu.check_valid_fmt", minReq)
                                                        action:@selector(onMenuCheckAllValid:) keyEquivalent:@""];
    checkValid.target = self;
    [m addItem:checkValid];

    NSMenuItem* selAll = [[NSMenuItem alloc] initWithTitle:heraw_i18n::tr_ns("qmenu.select_all")
                                                    action:@selector(selectAll:) keyEquivalent:@"a"];
    selAll.target = self.tblQueue;
    [m addItem:selAll];

    [m addItem:[NSMenuItem separatorItem]];
    NSMenuItem* mergeIt = [[NSMenuItem alloc] initWithTitle:heraw_i18n::tr_ns("qmenu.merge")
                                                     action:@selector(onMergeSelectedGroups:) keyEquivalent:@"m"];
    mergeIt.target = self;
    mergeIt.enabled = (sel.count >= 2 && !self.currentTask);
    [m addItem:mergeIt];

    NSMenuItem* cleanIt = [[NSMenuItem alloc] initWithTitle:heraw_i18n::trf_ns("qmenu.clean_fmt", minReq)
                                                     action:@selector(onCleanStrayGroups:) keyEquivalent:@""];
    cleanIt.target = self;
    cleanIt.enabled = ([self hasStrayGroups] && !self.currentTask);
    [m addItem:cleanIt];

    NSMenuItem* remIt = [[NSMenuItem alloc] initWithTitle:(sel.count >= 2
            ? heraw_i18n::tr_ns("qmenu.remove_multi") : heraw_i18n::tr_ns("qmenu.remove_one"))
        action:@selector(onRemoveSelectedGroup:) keyEquivalent:@""];
    remIt.target = self;
    remIt.enabled = (sel.count >= 1 && !self.currentTask);
    [m addItem:remIt];
    return m;
}

- (void)onMenuOnlyBestGroup:(id)s {
    (void)s;
    [self selectOnlySharpestGroup];
    [self.tblQueue reloadData];
    [self syncAllUiState];
}

- (void)onMenuCheckAllValid:(id)s {
    (void)s;
    int minReq = [self minValidFramesRequired];
    for (BurstGroupModel* g in self.burstGroups) {
        g.checked = (!g.isStrayArchive && g.validFrameCount >= minReq);
    }
    [self.tblQueue reloadData];
    [self syncAllUiState];
}

- (NSMenu*)buildFrameContextMenuForRow:(NSInteger)clickedRow {
    BurstGroupModel* g = [self selectedBurstGroup];
    if (!g || g.frames.count == 0) return nil;
    NSMenu* m = [[NSMenu alloc] initWithTitle:@"FrameMenu"];
    NSIndexSet* sel = self.tblFrames.selectedRowIndexes;

    NSMenuItem* tog = [[NSMenuItem alloc] initWithTitle:(sel.count >= 2
            ? heraw_i18n::tr_ns("fmenu.toggle_multi") : heraw_i18n::tr_ns("fmenu.toggle_one"))
        action:@selector(onToggleSelectedFramesEnabled:) keyEquivalent:@""];
    tog.target = self;
    [m addItem:tog];

    NSMenuItem* keepOnly = [[NSMenuItem alloc] initWithTitle:heraw_i18n::tr_ns("fmenu.keep_only")
                                                      action:@selector(onKeepOnlySelectedFrames:) keyEquivalent:@""];
    keepOnly.target = self;
    keepOnly.enabled = (sel.count >= 1 && sel.count < g.frames.count);
    [m addItem:keepOnly];

    NSMenuItem* restoreAll = [[NSMenuItem alloc] initWithTitle:heraw_i18n::tr_ns("fmenu.restore_all")
                                                        action:@selector(onRestoreGroupFrames:) keyEquivalent:@""];
    restoreAll.target = self;
    [m addItem:restoreAll];

    if (clickedRow >= 0 && (NSUInteger)clickedRow < g.frames.count) {
        NSMenuItem* lockAnc = [[NSMenuItem alloc] initWithTitle:heraw_i18n::trf_ns("fmenu.lock_anchor_fmt", g.frames[(NSUInteger)clickedRow].idx1Based)
                                                         action:@selector(onMenuLockClickedAnchor:) keyEquivalent:@""];
        lockAnc.tag = clickedRow;
        lockAnc.target = self;
        [m addItem:lockAnc];
    }

    [m addItem:[NSMenuItem separatorItem]];
    NSMenuItem* splitIt = [[NSMenuItem alloc] initWithTitle:heraw_i18n::tr_ns("fmenu.split")
                                                     action:@selector(onSplitSelectedFrames:) keyEquivalent:@"N"];
    splitIt.keyEquivalentModifierMask = NSEventModifierFlagCommand | NSEventModifierFlagShift;
    splitIt.target = self;
    splitIt.enabled = (sel.count >= 1 && sel.count < g.frames.count && !self.currentTask);
    [m addItem:splitIt];
    return m;
}

- (void)onMenuLockClickedAnchor:(NSMenuItem*)sender {
    BurstGroupModel* g = [self selectedBurstGroup];
    NSInteger r = sender.tag;
    if (!g || r < 0 || (NSUInteger)r >= g.frames.count) return;
    FrameDiagItem* f = g.frames[(NSUInteger)r];
    g.customAnchor1Based = (g.customAnchor1Based == f.idx1Based) ? 0 : f.idx1Based;
    if (g.customAnchor1Based > 0) {
        f.enabled = YES;
        if ([self.motionMode isEqualToString:@"clean-plate"] ||
            [self.motionMode isEqualToString:@"virtual-nd"] ||
            [self.motionMode isEqualToString:@"strobe"]) {
            self.motionMode = @"auto";
        }
    }
    [self recalculateGroupMetrics:g];
    [self.tblFrames reloadData];
    [self syncAllUiState];
}

- (void)onClearQueue:(id)sender {
    (void)sender;
    if (self.planTask && self.planTask.isRunning) [self.planTask terminate];
    self.planTask = nil;
    self.emptyWell.scanning = NO;
    ++self.planSerial;
    [self.rawInputPaths removeAllObjects];
    self.groupingDirty = NO;
    [self.fileByBasename removeAllObjects];
    [self.burstGroups removeAllObjects];
    if (self.autoOutDir.length > 0 && [self.edtOutDir.stringValue isEqualToString:self.autoOutDir]) {
        self.edtOutDir.stringValue = @"";
    }
    self.autoOutDir = nil;
    self.opticsModel = [[OpticsTelemetryModel alloc] init];
    [self.tblQueue reloadData];
    [self.tblFrames reloadData];
    [self updateQueueEmptyVisibility];
    self.lblStageStatus.stringValue = heraw_i18n::tr_ns("st.cleared");
    [self syncAllUiState];
}

- (void)onScanOptChanged:(id)sender {
    (void)sender;
    [self saveUiPrefs];
    if (self.rawInputPaths.count > 0) [self triggerBackgroundPlanJson];
}

- (void)layoutUpperHintBar {
    if (!self.canvas || !self.lblGroupingHint) return;
    NSRect uc = self.canvas.leftUpperCardRect;
    if (uc.size.width <= 0) return;
    if (self.btnIntakeAction && !self.btnIntakeAction.hidden) {
        self.lblGroupingHint.frame = NSMakeRect(uc.origin.x + 12, uc.origin.y + 40, uc.size.width - 140, 36);
        self.btnIntakeAction.frame = NSMakeRect(uc.origin.x + uc.size.width - 124, uc.origin.y + 46, 112, 22);
    } else {
        self.lblGroupingHint.frame = NSMakeRect(uc.origin.x + 12, uc.origin.y + 40, uc.size.width - 24, 36);
    }
}

- (void)updateGroupingHint {
    if (self.groupingDirty) {
        self.btnIntakeAction.hidden = YES;
        [self layoutUpperHintBar];
        self.lblGroupingHint.stringValue = heraw_i18n::tr_ns("hint.dirty");
        return;
    }
    int minReq = [self minValidFramesRequired];
    NSUInteger total = 0, validGroups = 0, checkedCnt = 0, strayFrames = 0;
    NSMutableArray<NSString*>* sizes = [NSMutableArray array];
    NSInteger curPresetIdx = self.segPreset.selectedSegment;
    NSString* curPresetCode = (curPresetIdx >= 0 && curPresetIdx < 4)
        ? [NSString stringWithUTF8String:kPresets[curPresetIdx].code] : @"photo";
    BurstGroupModel* rxGroup = nil;

    for (BurstGroupModel* group in self.burstGroups) {
        total += group.frames.count;
        if (group.isStrayArchive) {
            strayFrames += group.frames.count;
        } else {
            if (group.validFrameCount >= minReq) ++validGroups;
            else strayFrames += group.frames.count;
            if (sizes.count < 6) [sizes addObject:[NSString stringWithFormat:@"%lu", (unsigned long)group.frames.count]];
        }
        if (group.checked) {
            ++checkedCnt;
            if (!rxGroup && !group.isStrayArchive && group.recPreset.length > 0 &&
                ![group.recPreset isEqualToString:@"photo"] && ![group.recPreset isEqualToString:curPresetCode]) {
                rxGroup = group;
            }
        }
    }
    if (self.burstGroups.count > sizes.count && !self.burstGroups.lastObject.isStrayArchive) {
        [sizes addObject:@"…"];
    }

    // Inline Non-Popup Intake Action Pill
    if (total == 0) {
        self.btnIntakeAction.hidden = YES;
    } else if (checkedCnt == 0) {
        self.btnIntakeAction.title = (validGroups > 0)
            ? heraw_i18n::trf_ns("pill.check_n_fmt", (int)validGroups)
            : heraw_i18n::tr_ns("pill.fallback_fuse");
        self.btnIntakeAction.hidden = NO;
    } else if (rxGroup) {
        const char* rxKey = [rxGroup.recPreset isEqualToString:@"astro"] ? "pill.rx_astro"
                          : ([rxGroup.recPreset isEqualToString:@"sci"]  ? "pill.rx_sci" : "pill.rx_arc");
        self.btnIntakeAction.title = heraw_i18n::tr_ns(rxKey);
        self.btnIntakeAction.hidden = NO;
    } else if (validGroups >= 2 && checkedCnt >= 2) {
        self.btnIntakeAction.title = heraw_i18n::tr_ns("pill.only_best");
        self.btnIntakeAction.hidden = NO;
    } else if (validGroups >= 2 && checkedCnt == 1) {
        self.btnIntakeAction.title = heraw_i18n::trf_ns("pill.check_all_fmt", (int)validGroups);
        self.btnIntakeAction.hidden = NO;
    } else {
        self.btnIntakeAction.hidden = YES;
    }
    [self layoutUpperHintBar];

    NSString* joinedSz = [sizes componentsJoinedByString:@"+"];
    NSString* result = heraw_i18n::tr_ns("hint.idle_mac");
    if (total > 0) {
        if (strayFrames > 0 && validGroups > 0) {
            result = heraw_i18n::trf_ns("hint.summary_stray_fmt",
                (int)total, (int)validGroups, [joinedSz UTF8String] ?: "", (int)strayFrames);
        } else {
            result = heraw_i18n::trf_ns("hint.summary_all_fmt",
                (int)total, (int)self.burstGroups.count, [joinedSz UTF8String] ?: "");
        }
    }

    NSInteger gm = self.popGrouping.indexOfSelectedItem;
    if (total == 0) {
        self.lblGroupingHint.stringValue = result;
    } else if (gm == 1) {
        self.lblGroupingHint.stringValue = [result stringByAppendingFormat:@"\n%@", heraw_i18n::tr_ns("hint.mode_single_mac")];
    } else if (gm == 2) {
        self.lblGroupingHint.stringValue = [result stringByAppendingFormat:@"\n%@",
            heraw_i18n::trf_ns("hint.mode_best_fmt", [self.edtGrpSz.stringValue UTF8String] ?: "8")];
    } else if (gm == 3) {
        self.lblGroupingHint.stringValue = [result stringByAppendingFormat:@"\n%@", heraw_i18n::tr_ns("hint.mode_top1")];
    } else {
        NSString* reason = (checkedCnt == 0 && validGroups == 0)
            ? heraw_i18n::trf_ns("hint.mode_auto_short_fmt", minReq)
            : heraw_i18n::trf_ns("hint.mode_auto_ok_fmt", [self.edtGrpSz.stringValue UTF8String] ?: "8", minReq);
        self.lblGroupingHint.stringValue = [result stringByAppendingFormat:@"\n%@", reason];
    }
    self.lblGroupingHint.toolTip = self.lblGroupingHint.stringValue;
}

- (void)onGroupingChanged:(id)sender {
    if (self.currentTask) return;
    int minReq = [self minValidFramesRequired];
    [self saveUiPrefs];
    if (sender == self.popGrouping) {
        if (self.rawInputPaths.count == 0) {
            self.groupingDirty = NO;
            [self updateGroupingHint];
            [self updateRunButtonTitle];
            return;
        }
        if (self.popGrouping.indexOfSelectedItem != 1 && self.edtGrpSz.integerValue < minReq) {
            self.groupingDirty = YES;
            [self updateGroupingHint];
            [self updateRunButtonTitle];
            self.lblGroupingHint.stringValue = heraw_i18n::trf_ns("hint.min_grp_sz_fmt", minReq);
            return;
        }
        [self triggerBackgroundPlanJson];
        return;
    }
    ++self.planSerial;
    if (self.planTask.isRunning) [self.planTask terminate];
    self.planTask = nil;
    self.emptyWell.scanning = NO;
    self.groupingDirty = self.rawInputPaths.count > 0;
    [self updateGroupingHint];
    [self updateRunButtonTitle];
}

- (void)onRegroup:(id)sender {
    (void)sender;
    if (self.currentTask) return;
    int minReq = [self minValidFramesRequired];
    [self.window makeFirstResponder:nil];
    if (self.popGrouping.indexOfSelectedItem != 1 && self.edtGrpSz.integerValue < minReq) {
        self.groupingDirty = YES;
        [self updateRunButtonTitle];
        self.lblGroupingHint.stringValue = heraw_i18n::trf_ns("hint.min_grp_sz_fmt", minReq);
        return;
    }
    [self saveUiPrefs];
    [self triggerBackgroundPlanJson];
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

- (void)reapplySuitabilityForCurrentThreshold {
    int minReq = [self minValidFramesRequired];
    for (BurstGroupModel* g in self.burstGroups) {
        if (!g.isStrayArchive && g.filePaths.count == g.frames.count) {
            g.checked = (g.validFrameCount >= minReq);
        }
    }
    if (self.popGrouping.indexOfSelectedItem == 3 && self.burstGroups.count > 0) {
        [self selectOnlySharpestGroup];
    }
    [self.tblQueue reloadData];
    if (self.btnCleanStray) self.btnCleanStray.toolTip = heraw_i18n::trf_ns("dock.tip_clean_fmt", minReq);
    if (self.edtGrpSz) self.edtGrpSz.toolTip = heraw_i18n::trf_ns("dock.grp_sz_tip_fmt", minReq);
    [self setupMainMenu];
}

- (void)onGenericOptionChanged:(id)sender {
    BOOL scaleImpact = (sender == self.chkEmitRaw || sender == self.chkEmitRgb || sender == self.segRgbScale);
    [self syncAllUiState];
    if (scaleImpact) {
        [self reapplySuitabilityForCurrentThreshold];
        [self syncAllUiState];
        if (self.rawInputPaths.count > 0 && !self.currentTask) {
            [self triggerBackgroundPlanJson];
        }
    }
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
    [self saveUiPrefsFlush:NO];
}

- (void)controlTextDidChange:(NSNotification*)obj {
    if (obj.object == self.edtGrpSz) [self onGroupingChanged:obj.object];
}

- (void)controlTextDidEndEditing:(NSNotification*)obj {
    NSTextField* tf = obj.object;
    if (tf == self.edtGrpSz) {
        int minReq = [self minValidFramesRequired];
        [self saveUiPrefs];
        if (self.rawInputPaths.count > 0 && self.groupingDirty &&
            (self.popGrouping.indexOfSelectedItem == 1 || self.edtGrpSz.integerValue >= minReq)) {
            [self triggerBackgroundPlanJson];
        }
        return;
    }
    if (tf == self.edtOutDir) {
        if (![tf.stringValue isEqualToString:(self.autoOutDir ?: @"")]) self.autoOutDir = nil;
    }
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
    else if (tf == self.edtThreads)    [self syncCoreCountPopupFromThreads];
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
        self.autoOutDir = nil;
        self.edtOutDir.stringValue = op.URL.path;
    }
}

- (void)onClearOutDir:(id)sender {
    (void)sender;
    self.autoOutDir = nil;
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
        a.messageText = heraw_i18n::tr_ns("st.alert_select_group");
        [a runModal];
        return;
    }
    NSString* cli = [self resolveActiveCliOrPrompt];
    if (!cli) return;

    NSOpenPanel* op = [NSOpenPanel openPanel];
    op.title = heraw_i18n::tr_ns("dlg.pick_topk_dir");
    op.canChooseFiles = NO;
    op.canChooseDirectories = YES;
    op.canCreateDirectories = YES;
    if ([op runModal] != NSModalResponseOK || !op.URL.path) return;

    NSMutableArray<NSString*>* args = [NSMutableArray array];
    if (self.customLibRawPath.length > 0) [args addObjectsFromArray:@[@"--libraw", self.customLibRawPath]];
    if (self.edtThreads.integerValue > 0) [args addObjectsFromArray:@[@"--threads", self.edtThreads.stringValue]];
    [args addObjectsFromArray:@[@"--no-sort-time", @"--export-topk", @"3", @"--out-dir", op.URL.path]];
    NSString* skipCsv = [self skipFramesCsvForGroup:g];
    if (skipCsv.length > 0) [args addObjectsFromArray:@[@"--skip-frames", skipCsv]];
    [args addObjectsFromArray:g.filePaths];

    [self runUtilityCliTask:cli args:args label:heraw_i18n::tr_ns("st.topk_running") completion:^(int rc) {
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
        model.tierBadge = stellar
            ? heraw_i18n::trf_ns("t4.badge_stellar_fmt", valid)
            : heraw_i18n::trf_ns("t4.badge_mtf_fmt", valid);
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
        std::string oisStr = [root[@"ois"] boolValue] ? heraw_i18n::tr_s("t4.ois_on") : heraw_i18n::tr_s("t4.ois_off");
        model.oisSummary = heraw_i18n::trf_ns("t4.ois_measured_fmt", oisStr.c_str(), valid);
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
    m.tierBadge = grade.empty() ? heraw_i18n::tr_ns("t4.badge_optics") : [NSString stringWithUTF8String:grade.c_str()];

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
    if (!m.populated) m.tierBadge = heraw_i18n::tr_ns("t4.badge_pending");

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
    if (!ois.empty()) m.oisSummary = heraw_i18n::trf_ns("t4.ois_status_fmt", ois.c_str());

    self.opticsModel = m;
    [self syncAllUiState];
}

- (void)onRunOpticsReport:(id)sender {
    (void)sender;
    if (self.currentTask) return;
    BurstGroupModel* g = [self selectedBurstGroup] ?: self.burstGroups.firstObject;
    if (!g || g.filePaths.count == 0) {
        NSAlert* a = [[NSAlert alloc] init];
        a.messageText = heraw_i18n::tr_ns("st.alert_select_group");
        [a runModal];
        return;
    }
    NSString* cli = [self resolveActiveCliOrPrompt];
    if (!cli) return;

    NSString* prefix = [NSTemporaryDirectory() stringByAppendingPathComponent:[@"nijin_optics_" stringByAppendingString:NSUUID.UUID.UUIDString]];

    NSMutableArray<NSString*>* args = [NSMutableArray array];
    if (self.customLibRawPath.length > 0) [args addObjectsFromArray:@[@"--libraw", self.customLibRawPath]];
    if (self.edtThreads.integerValue > 0) [args addObjectsFromArray:@[@"--threads", self.edtThreads.stringValue]];
    [args addObjectsFromArray:@[@"--no-sort-time", @"--optics-only", @"--optics-report", prefix]];
    NSString* skipCsv = [self skipFramesCsvForGroup:g];
    if (skipCsv.length > 0) [args addObjectsFromArray:@[@"--skip-frames", skipCsv]];
    [args addObjectsFromArray:g.filePaths];

    __weak AppDelegate* weakSelf = self;
    [self runUtilityCliTask:cli args:args label:heraw_i18n::tr_ns("st.optics_running") completion:^(int rc) {
        AppDelegate* s = weakSelf;
        if (!s || [s selectedBurstGroup] != g) return;
        NSString* jsonPath = [prefix stringByAppendingString:@"_telemetry.json"];
        NSString* pngPath  = [prefix stringByAppendingString:@"_passport.png"];
        if (![[NSFileManager defaultManager] fileExistsAtPath:pngPath]) {
            pngPath = [prefix stringByAppendingString:@".png"];
        }
        if (rc == 0 && [[NSFileManager defaultManager] fileExistsAtPath:jsonPath]) {
            [s parseOpticsTelemetryFile:jsonPath cardPng:pngPath];
            s.lblStageStatus.stringValue = heraw_i18n::trf_ns("st.optics_done_fmt",
                [s.opticsModel.tierBadge UTF8String] ?: "");
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
        a.messageText = heraw_i18n::tr_ns("st.alert_select_group");
        [a runModal];
        return;
    }
    NSString* cli = [self resolveActiveCliOrPrompt];
    if (!cli) return;

    NSSavePanel* sp = [NSSavePanel savePanel];
    sp.title = heraw_i18n::tr_ns("dlg.save_lcp");
    sp.nameFieldStringValue = @"nijin_lens_profile.lcp";
    if ([sp runModal] != NSModalResponseOK || !sp.URL.path) return;

    NSString* tmpPrefix = [NSTemporaryDirectory() stringByAppendingPathComponent:[@"nijin_lcp_" stringByAppendingString:NSUUID.UUID.UUIDString]];
    NSMutableArray<NSString*>* args = [NSMutableArray array];
    if (self.customLibRawPath.length > 0) [args addObjectsFromArray:@[@"--libraw", self.customLibRawPath]];
    if (self.edtThreads.integerValue > 0) [args addObjectsFromArray:@[@"--threads", self.edtThreads.stringValue]];
    [args addObject:@"--no-sort-time"];
    [args addObjectsFromArray:[self lcpArguments:sp.URL.path]];
    [args addObjectsFromArray:@[@"--optics-report", tmpPrefix]];
    NSString* skipCsv = [self skipFramesCsvForGroup:g];
    if (skipCsv.length > 0) [args addObjectsFromArray:@[@"--skip-frames", skipCsv]];
    [args addObjectsFromArray:g.filePaths];

    __weak AppDelegate* weakSelf = self;
    [self runUtilityCliTask:cli args:args label:heraw_i18n::tr_ns("st.lcp_running") completion:^(int rc) {
        AppDelegate* s = weakSelf;
        NSString* tj = [tmpPrefix stringByAppendingString:@"_telemetry.json"];
        NSString* tp = [tmpPrefix stringByAppendingString:@"_passport.png"];
        if (rc == 0 && s && [[NSFileManager defaultManager] fileExistsAtPath:tj]) {
            [s parseOpticsTelemetryFile:tj cardPng:tp];
            s.lblStageStatus.stringValue = heraw_i18n::trf_ns("st.lcp_done_fmt",
                [[sp.URL.path lastPathComponent] UTF8String] ?: "");
        }
    }];
}

- (void)onExportOpticsPng:(id)sender {
    (void)sender;
    if (self.currentTask) return;
    NSSavePanel* sp = [NSSavePanel savePanel];
    sp.title = heraw_i18n::tr_ns("dlg.save_png");
    sp.nameFieldStringValue = @"nijin_optics_passport.png";
    if ([sp runModal] != NSModalResponseOK || !sp.URL.path) return;

    if (self.opticsModel.lastCardPngPath && [[NSFileManager defaultManager] fileExistsAtPath:self.opticsModel.lastCardPngPath]) {
        NSData* data = [NSData dataWithContentsOfFile:self.opticsModel.lastCardPngPath];
        BOOL saved = [data writeToURL:sp.URL atomically:YES];
        self.lblStageStatus.stringValue = saved
            ? heraw_i18n::trf_ns("st.png_saved_fmt", [[sp.URL.path lastPathComponent] UTF8String] ?: "")
            : heraw_i18n::tr_ns("st.png_err");
        return;
    }

    BurstGroupModel* g = [self selectedBurstGroup] ?: self.burstGroups.firstObject;
    NSString* cli = (g && g.filePaths.count > 0) ? [self resolveActiveCliOrPrompt] : nil;
    if (cli && g && g.filePaths.count > 0) {
        NSString* tmpPrefix = [NSTemporaryDirectory() stringByAppendingPathComponent:[@"nijin_optics_" stringByAppendingString:NSUUID.UUID.UUIDString]];
        NSMutableArray<NSString*>* args = [NSMutableArray array];
        if (self.customLibRawPath.length > 0) [args addObjectsFromArray:@[@"--libraw", self.customLibRawPath]];
        if (self.edtThreads.integerValue > 0) [args addObjectsFromArray:@[@"--threads", self.edtThreads.stringValue]];
        [args addObjectsFromArray:@[@"--no-sort-time", @"--optics-only", @"--optics-report", tmpPrefix]];
        NSString* skipCsv = [self skipFramesCsvForGroup:g];
        if (skipCsv.length > 0) [args addObjectsFromArray:@[@"--skip-frames", skipCsv]];
        [args addObjectsFromArray:g.filePaths];

        __weak AppDelegate* weakSelf = self;
        [self runUtilityCliTask:cli args:args label:heraw_i18n::tr_ns("st.optics_running") completion:^(int rc) {
            AppDelegate* s = weakSelf;
            if (!s) return;
            NSString* srcJson = [tmpPrefix stringByAppendingString:@"_telemetry.json"];
            NSString* srcPng  = [tmpPrefix stringByAppendingString:@"_passport.png"];
            if (rc == 0 && [[NSFileManager defaultManager] fileExistsAtPath:srcPng]) {
                BOOL saved = [[NSData dataWithContentsOfFile:srcPng] writeToURL:sp.URL atomically:YES];
                if ([[NSFileManager defaultManager] fileExistsAtPath:srcJson]) {
                    [s parseOpticsTelemetryFile:srcJson cardPng:srcPng];
                }
                s.lblStageStatus.stringValue = saved
                    ? heraw_i18n::trf_ns("st.png_saved_fmt", [[sp.URL.path lastPathComponent] UTF8String] ?: "")
                    : heraw_i18n::tr_ns("st.png_err");
            }
        }];
        return;
    }

    [self.page3 layoutSubtreeIfNeeded];
    NSBitmapImageRep* rep = [self.page3 bitmapImageRepForCachingDisplayInRect:self.page3.bounds];
    if (rep) {
        [self.page3 cacheDisplayInRect:self.page3.bounds toBitmapImageRep:rep];
        NSData* png = [rep representationUsingType:NSBitmapImageFileTypePNG properties:@{}];
        BOOL saved = [png writeToFile:sp.URL.path atomically:YES];
        self.lblStageStatus.stringValue = saved
            ? heraw_i18n::trf_ns("st.png_saved_fmt", [[sp.URL.path lastPathComponent] UTF8String] ?: "")
            : heraw_i18n::tr_ns("st.png_err");
    }
}

- (void)onExportOpticsJson:(id)sender {
    (void)sender;
    if (self.currentTask) return;
    NSSavePanel* sp = [NSSavePanel savePanel];
    sp.title = heraw_i18n::tr_ns("dlg.save_json");
    sp.nameFieldStringValue = @"nijin_telemetry.json";
    if ([sp runModal] != NSModalResponseOK || !sp.URL.path) return;

    if (self.opticsModel.lastJsonPath && [[NSFileManager defaultManager] fileExistsAtPath:self.opticsModel.lastJsonPath]) {
        NSData* data = [NSData dataWithContentsOfFile:self.opticsModel.lastJsonPath];
        BOOL saved = [data writeToURL:sp.URL atomically:YES];
        self.lblStageStatus.stringValue = saved
            ? heraw_i18n::trf_ns("st.json_saved_fmt", [[sp.URL.path lastPathComponent] UTF8String] ?: "")
            : heraw_i18n::tr_ns("st.json_err");
        return;
    }

    BurstGroupModel* g = [self selectedBurstGroup] ?: self.burstGroups.firstObject;
    if (!g || g.filePaths.count == 0) return;
    NSString* cli = [self resolveActiveCliOrPrompt];
    if (!cli) return;

    NSString* tmpPrefix = [NSTemporaryDirectory() stringByAppendingPathComponent:[@"nijin_optics_" stringByAppendingString:NSUUID.UUID.UUIDString]];
    NSMutableArray<NSString*>* args = [NSMutableArray array];
    if (self.customLibRawPath.length > 0) [args addObjectsFromArray:@[@"--libraw", self.customLibRawPath]];
    if (self.edtThreads.integerValue > 0) [args addObjectsFromArray:@[@"--threads", self.edtThreads.stringValue]];
    [args addObjectsFromArray:@[@"--no-sort-time", @"--optics-only", @"--optics-report", tmpPrefix]];
    NSString* skipCsv = [self skipFramesCsvForGroup:g];
    if (skipCsv.length > 0) [args addObjectsFromArray:@[@"--skip-frames", skipCsv]];
    [args addObjectsFromArray:g.filePaths];

    __weak AppDelegate* weakSelf = self;
    [self runUtilityCliTask:cli args:args label:heraw_i18n::tr_ns("st.json_running") completion:^(int rc) {
        AppDelegate* s = weakSelf;
        if (!s) return;
        NSString* srcJson = [tmpPrefix stringByAppendingString:@"_telemetry.json"];
        NSString* srcPng  = [tmpPrefix stringByAppendingString:@"_passport.png"];
        if (rc == 0 && [[NSFileManager defaultManager] fileExistsAtPath:srcJson]) {
            BOOL saved = [[NSData dataWithContentsOfFile:srcJson] writeToURL:sp.URL atomically:YES];
            [s parseOpticsTelemetryFile:srcJson cardPng:srcPng];
            s.lblStageStatus.stringValue = saved
                ? heraw_i18n::trf_ns("st.json_saved_fmt", [[sp.URL.path lastPathComponent] UTF8String] ?: "")
                : heraw_i18n::tr_ns("st.json_err");
        }
    }];
}

- (BOOL)exportDiagLogToPath:(NSString*)path {
    if (!path || path.length == 0) return NO;
    heraw_i18n::diag_log().log_event("EXPORT", std::string("Saving session diagnostic log to ") + ([path UTF8String] ?: ""));
    BOOL ok = heraw_i18n::diag_log().export_to_file([path UTF8String] ?: "") ? YES : NO;
    self.lblStageStatus.stringValue = ok
        ? heraw_i18n::trf_ns("st.log_saved_fmt", [[path lastPathComponent] UTF8String] ?: "")
        : heraw_i18n::tr_ns("st.log_err");
    return ok;
}

- (void)onExportDiagLog:(id)sender {
    (void)sender;
    NSSavePanel* sp = [NSSavePanel savePanel];
    sp.title = heraw_i18n::tr_ns("dlg.save_log");
    sp.nameFieldStringValue = [NSString stringWithUTF8String:heraw_i18n::SessionDiagLog::default_filename().c_str()];
    NSString* dir = [self effectiveOutputDirectory];
    if (dir.length > 0 && [[NSFileManager defaultManager] fileExistsAtPath:dir]) {
        sp.directoryURL = [NSURL fileURLWithPath:dir];
    }
    if ([sp runModal] != NSModalResponseOK || !sp.URL.path) return;
    [self exportDiagLogToPath:sp.URL.path];
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

    std::vector<std::string> argVec;
    argVec.reserve(args.count);
    for (NSString* a in args) argVec.emplace_back([a UTF8String] ?: "");
    int logTaskId = heraw_i18n::diag_log().begin_task(
        [statusText UTF8String] ?: "utility", [cliPath UTF8String] ?: "", argVec);

    NSTask* t = [[NSTask alloc] init];
    t.executableURL = [NSURL fileURLWithPath:cliPath];
    t.arguments = args;
    NSPipe* output = [NSPipe pipe];
    t.standardOutput = output;
    t.standardError = output;
    NSFileHandle* reader = output.fileHandleForReading;
    auto capMu = std::make_shared<std::mutex>();
    auto capBuf = std::make_shared<std::string>();

    reader.readabilityHandler = ^(NSFileHandle* h) {
        NSData* d = [h availableData];
        if (d.length == 0) { h.readabilityHandler = nil; return; }
        heraw_i18n::diag_log().append_task_output(logTaskId, (const char*)d.bytes, d.length);
        std::lock_guard<std::mutex> lk(*capMu);
        if (capBuf->size() < 65536) {
            capBuf->append((const char*)d.bytes, std::min<size_t>(d.length, 65536 - capBuf->size()));
        }
    };
    self.currentTask = t;
    [self updateRunButtonTitle];

    __weak AppDelegate* weakSelf = self;
    t.terminationHandler = ^(NSTask* finished) {
        reader.readabilityHandler = nil;
        NSData* rem = [reader readDataToEndOfFile];
        if (rem.length > 0) {
            heraw_i18n::diag_log().append_task_output(logTaskId, (const char*)rem.bytes, rem.length);
            std::lock_guard<std::mutex> lk(*capMu);
            if (capBuf->size() < 65536) {
                capBuf->append((const char*)rem.bytes, std::min<size_t>(rem.length, 65536 - capBuf->size()));
            }
        }
        int rc = finished.terminationStatus;
        std::string reason;
        {
            std::lock_guard<std::mutex> lk(*capMu);
            reason = heraw_i18n::SessionDiagLog::extract_failure_reason(*capBuf);
        }
        dispatch_async(dispatch_get_main_queue(), ^{
            AppDelegate* s = weakSelf;
            heraw_i18n::diag_log().end_task(logTaskId, rc, s ? (bool)s.userStoppedJob : false);
            if (!s || s.currentTask != finished) return;
            s.currentTask = nil;
            s.progView.progress = (rc == 0) ? 100.0 : 0.0;
            [s.progView setNeedsDisplay:YES];
            s.lblPct.stringValue = (rc == 0) ? @"100%" : @"--";
            if (rc != 0 && !s.userStoppedJob) {
                s.lblStageStatus.stringValue = heraw_i18n::trf_ns("st.subtask_err_fmt", rc, reason.c_str());
            } else if (s.userStoppedJob) {
                s.lblStageStatus.stringValue = heraw_i18n::tr_ns("st.task_stopped");
            } else {
                s.lblStageStatus.stringValue = heraw_i18n::tr_ns("st.done");
            }
            [s updateRunButtonTitle];
            if (completion && !s.userStoppedJob) completion(rc);
        });
    };

    NSError* err = nil;
    if (![t launchAndReturnError:&err]) {
        reader.readabilityHandler = nil;
        heraw_i18n::diag_log().end_task(logTaskId, -1);
        self.currentTask = nil;
        self.progView.progress = 0.0;
        [self.progView setNeedsDisplay:YES];
        self.lblStageStatus.stringValue = heraw_i18n::trf_ns("st.launch_err_fmt",
            [err.localizedDescription UTF8String] ?: "");
        [self updateRunButtonTitle];
    }
}

- (NSArray<NSString*>*)buildCliArgsForGroup:(BurstGroupModel*)grp outDir:(NSString*)outDir {
    return [self buildCliArgsForGroup:grp outDir:outDir multiGroup:NO];
}

- (NSArray<NSString*>*)buildCliArgsForGroup:(BurstGroupModel*)grp outDir:(NSString*)outDir multiGroup:(BOOL)multiGroup {
    NSMutableArray<NSString*>* a = [NSMutableArray array];
    if (self.customLibRawPath.length > 0) {
        [a addObjectsFromArray:@[@"--libraw", self.customLibRawPath]];
    }
    [a addObject:@"--no-sort-time"];
    if (self.chkRecursive.state == NSControlStateValueOn) [a addObject:@"--recursive"];

    [a addObjectsFromArray:@[@"--policy", @"force-all", @"--single-group"]];

    NSInteger pIdx = self.segPreset.selectedSegment;
    if (pIdx < 0 || pIdx > 3) pIdx = 0;
    [a addObjectsFromArray:@[@"--preset", [NSString stringWithUTF8String:kPresets[pIdx].code]]];

    // Motion & Pose Anchor
    NSString* mm = self.motionMode ?: @"auto";
    if ([mm isEqualToString:@"clean-plate"]) {
        [a addObjectsFromArray:@[@"--motion-mode", @"clean-plate", @"--clean-sens", self.edtCleanSens.stringValue]];
    } else if ([mm isEqualToString:@"virtual-nd"]) {
        double vndVal = std::clamp(self.edtVndThresh.doubleValue, 0.10, 1.00);
        [a addObjectsFromArray:@[@"--motion-mode", @"virtual-nd", @"--vnd-thresh",
                                 [NSString stringWithFormat:@"%.2f", vndVal]]];
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
    BOOL wantRaw = (self.chkEmitRaw.state == NSControlStateValueOn);
    [a addObjectsFromArray:@[@"--relay-raw", (wantRaw && self.chkRelayRaw.state == NSControlStateValueOn) ? @"on" : @"off"]];
    [a addObjectsFromArray:@[@"--no-tool-exif", (self.chkNoToolExif.state == NSControlStateValueOn) ? @"on" : @"off"]];
    if (wantRaw) [a addObject:@"--emit-raw"];

    [a addObjectsFromArray:@[@"--rgb-format", [self selectedRgbFormatCode]]];
    NSString* scCode = [self selectedRgbScaleCode];
    if (![scCode isEqualToString:@"2x"]) {
        [a addObjectsFromArray:@[@"--rgb-scale", scCode]];
    }
    if (self.edtJxsBpp.stringValue.length > 0) {
        [a addObjectsFromArray:@[@"--jxs-bpp", self.edtJxsBpp.stringValue]];
    }
    [a addObjectsFromArray:@[@"--color-space", (self.segColorSpace.selectedSegment == 1) ? @"adobergb" : @"srgb"]];
    if (self.chkEmitRgb.state == NSControlStateValueOn) {
        if ([scCode isEqualToString:@"1x"]) {
            [a addObject:@"--emit-rgb-1x"];
        } else {
            [a addObject:@"--emit-rgb"];
        }
    }

    if (self.chkEmitField.state == NSControlStateValueOn) {
        NSString* effCsv = [self effectiveFieldsCsvForGroup:grp];
        if (effCsv.length > 0) {
            [a addObject:@"--emit-field"];
            NSArray<NSString*>* fmCodes = fieldModeCodes();
            NSInteger fmIdx = self.popFieldMode.indexOfSelectedItem;
            if (fmIdx >= 0 && (NSUInteger)fmIdx < fmCodes.count) {
                [a addObjectsFromArray:@[@"--field-mode", fmCodes[(NSUInteger)fmIdx]]];
            }
            [a addObjectsFromArray:@[@"--fields", effCsv]];
        }
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
        if (multiGroup) {
            tilesDir = [tilesDir stringByAppendingPathComponent:[NSString stringWithFormat:@"group_%02d", grp.displayIndex]];
        }
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
    if (self.groupingDirty || self.planTask) {
        self.lblStageStatus.stringValue = heraw_i18n::tr_ns("st.regroup_first");
        return;
    }
    if (self.chkEmitField.state == NSControlStateValueOn && [self selectedFieldCount] == 0) {
        self.lblStageStatus.stringValue = heraw_i18n::tr_ns("st.pick_field");
        return;
    }
    NSMutableArray<BurstGroupModel*>* checkedGroups = [NSMutableArray array];
    for (BurstGroupModel* g in self.burstGroups) {
        if (g.checked && g.filePaths.count > 0) [checkedGroups addObject:g];
    }
    if (checkedGroups.count == 0) {
        NSAlert* a = [[NSAlert alloc] init];
        a.messageText = (self.burstGroups.count == 0)
            ? heraw_i18n::tr_ns("st.alert_no_input")
            : heraw_i18n::tr_ns("st.alert_no_checked");
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
        a.messageText = heraw_i18n::tr_ns("st.alert_no_output");
        [a runModal];
        return;
    }

    NSString* cliPath = [self resolveActiveCliOrPrompt];
    if (!cliPath) return;

    NSString* outDir = [self effectiveOutputDirectory];
    if (self.edtOutDir.stringValue.length == 0) self.edtOutDir.stringValue = outDir;

    BOOL multiGroup = (checkedGroups.count > 1);
    NSMutableArray<NSArray<NSString*>*>* jobs = [NSMutableArray array];
    NSMutableArray<BurstGroupModel*>* runnable = [NSMutableArray array];
    for (BurstGroupModel* g in checkedGroups) {
        NSArray<NSString*>* job = [self buildCliArgsForGroup:g outDir:outDir multiGroup:multiGroup];
        BOOL hasOutput = NO;
        for (NSString* flag in @[@"--emit-raw", @"--emit-rgb", @"--emit-rgb-1x", @"--emit-field", @"--emit-transient",
                                @"--export-parallax-html", @"--export-refocus", @"--export-dataset-tiles"])
            hasOutput |= [job containsObject:flag];
        if (hasOutput) { [jobs addObject:job]; [runnable addObject:g]; }
    }
    if (runnable.count == 0) {
        self.lblStageStatus.stringValue = heraw_i18n::tr_ns("st.gate_none_runnable");
        return;
    }
    checkedGroups = runnable;
    self.queuedArguments = jobs;
    self.userStoppedJob = NO;
    self.progView.progress = 0.0;
    [self.progView setNeedsDisplay:YES];
    self.lblPct.stringValue = @"0%";
    [self runQueuedGroups:checkedGroups atIndex:0 succeeded:0 skipped:0 cliPath:cliPath outDir:outDir];
}

- (void)runQueuedGroups:(NSArray<BurstGroupModel*>*)groups
                atIndex:(NSUInteger)idx
              succeeded:(NSUInteger)succeeded
                skipped:(NSUInteger)skipped
                cliPath:(NSString*)cliPath
                 outDir:(NSString*)outDir {
    if (self.userStoppedJob || idx >= groups.count) {
        self.currentTask = nil;
        if (!self.userStoppedJob) {
            self.progView.progress = 100.0;
            [self.progView setNeedsDisplay:YES];
            self.lblPct.stringValue = @"100%";
            if (skipped > 0) {
                self.lblStageStatus.stringValue = heraw_i18n::trf_ns("st.all_done_partial_fmt",
                    (int)succeeded, (int)groups.count, (int)skipped);
            } else {
                self.lblStageStatus.stringValue = heraw_i18n::trf_ns("st.all_done_fmt",
                    (int)succeeded, [[outDir lastPathComponent] UTF8String] ?: "");
            }
        } else {
            self.lblStageStatus.stringValue = heraw_i18n::tr_ns("st.stopped_exec");
        }
        [self updateRunButtonTitle];
        return;
    }

    BurstGroupModel* grp = groups[idx];
    NSArray<NSString*>* args = self.queuedArguments[idx];
    NSDate* jobStart = [NSDate dateWithTimeIntervalSinceNow:-2.0];

    std::vector<std::string> argVec;
    argVec.reserve(args.count);
    for (NSString* a in args) argVec.emplace_back([a UTF8String] ?: "");
    int logTaskId = heraw_i18n::diag_log().begin_task(
        heraw_i18n::trf("group_%02d", grp.displayIndex), [cliPath UTF8String] ?: "", argVec);

    NSTask* task = [[NSTask alloc] init];
    task.executableURL = [NSURL fileURLWithPath:cliPath];
    task.arguments = args;
    NSPipe* pipe = [NSPipe pipe];
    task.standardOutput = pipe;
    task.standardError = pipe;
    self.currentTask = task;
    [self updateRunButtonTitle];

    self.lblStageStatus.stringValue = heraw_i18n::trf_ns("st.group_starting_fmt",
        (int)(idx + 1), (int)groups.count);

    NSFileHandle* fh = [pipe fileHandleForReading];
    __weak AppDelegate* weakSelf = self;
    NSUInteger totalGroups = groups.count;
    auto capMu = std::make_shared<std::mutex>();
    auto pending = std::make_shared<std::string>();
    auto fullLog = std::make_shared<std::string>();

    fh.readabilityHandler = ^(NSFileHandle* handle) {
        NSData* d = [handle availableData];
        if (d.length == 0) { handle.readabilityHandler = nil; return; }
        heraw_i18n::diag_log().append_task_output(logTaskId, (const char*)d.bytes, d.length);
        std::vector<std::string> jsonLines;
        {
            std::lock_guard<std::mutex> lk(*capMu);
            if (fullLog->size() < 65536) {
                fullLog->append((const char*)d.bytes, std::min<size_t>(d.length, 65536 - fullLog->size()));
            }
            pending->append((const char*)d.bytes, d.length);
            size_t nl;
            while ((nl = pending->find('\n')) != std::string::npos) {
                std::string s = pending->substr(0, nl);
                pending->erase(0, nl + 1);
                if (!s.empty() && s.front() == '{' && s.find("\"event\"") != std::string::npos) {
                    jsonLines.push_back(std::move(s));
                }
            }
        }
        for (const std::string& s : jsonLines) {
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
                    st.lblStageStatus.stringValue = heraw_i18n::trf_ns("st.group_msg_fmt",
                        (int)(idx + 1), (int)totalGroups, msg.c_str());
                }
            });
        }
    };

    task.terminationHandler = ^(NSTask* finished) {
        fh.readabilityHandler = nil;
        NSData* rem = [fh readDataToEndOfFile];
        if (rem.length > 0) {
            heraw_i18n::diag_log().append_task_output(logTaskId, (const char*)rem.bytes, rem.length);
            std::lock_guard<std::mutex> lk(*capMu);
            if (fullLog->size() < 65536) {
                fullLog->append((const char*)rem.bytes, std::min<size_t>(rem.length, 65536 - fullLog->size()));
            }
        }
        int rc = finished.terminationStatus;
        std::string reason;
        {
            std::lock_guard<std::mutex> lk(*capMu);
            reason = heraw_i18n::SessionDiagLog::extract_failure_reason(*fullLog);
        }
        dispatch_async(dispatch_get_main_queue(), ^{
            AppDelegate* st = weakSelf;
            heraw_i18n::diag_log().end_task(logTaskId, rc, st ? (bool)st.userStoppedJob : false);
            if (!st || st.currentTask != finished) return;
            if (st.userStoppedJob) {
                st.currentTask = nil;
                st.lblStageStatus.stringValue = heraw_i18n::tr_ns("st.stopped_exec");
                [st updateRunButtonTitle];
                return;
            }
            if (rc == 3 && totalGroups > 1 && (idx + 1 < totalGroups || succeeded > 0)) {
                st.lblStageStatus.stringValue = heraw_i18n::trf_ns("st.group_skipped_fmt",
                    grp.displayIndex, reason.c_str());
                [st refreshGroupFromFieldsJsonInDir:outDir group:grp since:jobStart];
                [st runQueuedGroups:groups atIndex:(idx + 1) succeeded:succeeded skipped:(skipped + 1)
                            cliPath:cliPath outDir:outDir];
                return;
            }
            if (rc != 0) {
                st.currentTask = nil;
                st.lblStageStatus.stringValue = heraw_i18n::trf_ns("st.group_aborted_fmt",
                    grp.displayIndex, rc, reason.c_str());
                [st updateRunButtonTitle];
                return;
            }
            [st refreshGroupFromFieldsJsonInDir:outDir group:grp since:jobStart];
            [st runQueuedGroups:groups atIndex:(idx + 1) succeeded:(succeeded + 1) skipped:skipped
                        cliPath:cliPath outDir:outDir];
        });
    };

    NSError* err = nil;
    if (![task launchAndReturnError:&err]) {
        fh.readabilityHandler = nil;
        heraw_i18n::diag_log().end_task(logTaskId, -1);
        self.currentTask = nil;
        self.lblStageStatus.stringValue = heraw_i18n::trf_ns("st.launch_err_fmt",
            [err.localizedDescription UTF8String] ?: "");
        [self updateRunButtonTitle];
    }
}

- (void)refreshGroupFromFieldsJsonInDir:(NSString*)outDir
                                  group:(BurstGroupModel*)grp
                                  since:(NSDate*)sinceDate {
    if (!outDir || !grp) return;
    NSArray<NSString*>* entries = [[NSFileManager defaultManager] contentsOfDirectoryAtPath:outDir error:nil];
    if (!entries) return;
    NSString* bestPath = nil;
    NSDate* bestDate = nil;
    for (NSString* name in entries) {
        if (![name hasSuffix:@"_fields.json"]) continue;
        NSString* full = [outDir stringByAppendingPathComponent:name];
        NSDictionary* attrs = [[NSFileManager defaultManager] attributesOfItemAtPath:full error:nil];
        NSDate* mod = attrs[NSFileModificationDate];
        if (!mod || (sinceDate && [mod compare:sinceDate] == NSOrderedAscending)) continue;
        if (!bestDate || [mod compare:bestDate] == NSOrderedDescending) {
            bestDate = mod;
            bestPath = full;
        }
    }
    if (!bestPath) return;
    NSData* d = [NSData dataWithContentsOfFile:bestPath];
    if (!d || d.length == 0) return;
    id parsed = [NSJSONSerialization JSONObjectWithData:d options:0 error:nil];
    if (![parsed isKindOfClass:[NSDictionary class]]) return;
    NSDictionary* root = parsed;
    if ([root[@"parallax_2x_px"] isKindOfClass:[NSNumber class]]) {
        double p2x = [root[@"parallax_2x_px"] doubleValue];
        if (std::isfinite(p2x)) {
            grp.maxParallax2xPx = p2x;
            grp.hasMeasuredParallax = YES;
        }
    }
    if ([root[@"fields"] isKindOfClass:[NSArray class]]) {
        NSMutableDictionary<NSString*, NSNumber*>* obs = [NSMutableDictionary dictionaryWithDictionary:(grp.fieldObservability ?: @{})];
        for (id item in (NSArray*)root[@"fields"]) {
            if (![item isKindOfClass:[NSDictionary class]]) continue;
            NSDictionary* fd = item;
            NSString* name = [fd[@"name"] isKindOfClass:[NSString class]] ? fd[@"name"] : nil;
            id oval = fd[@"observable"];
            if (name.length > 0 && [oval isKindOfClass:[NSNumber class]]) {
                obs[name] = @([oval boolValue]);
            }
        }
        grp.fieldObservability = obs;
        if (obs[@"depth"]) {
            grp.depthKnown = YES;
            grp.depthObservable = [obs[@"depth"] boolValue];
        }
    }
    [self syncAllUiState];
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

    CGFloat gxHeaderX = W - 20.0 - heraw_brand::kNijin1xW - 7.0 - 8.0 - heraw_brand::kGx1xW;
    self.btnPerfHeader.frame = NSMakeRect(gxHeaderX - 210.0, 15.0, 194.0, 26.0);

    // --- Left Persistent Master-Detail Dock (x = 20..476, width = 456 pt) ---
    CGFloat lx = 20.0;
    CGFloat lw = 456.0;

    self.lblQueueHeader.frame = NSMakeRect(lx + 2, topY + 3, 140, 22);
    self.chkSortTime.frame    = NSMakeRect(lx + lw - 244, topY + 3, 130, 22);
    self.chkRecursive.frame   = NSMakeRect(lx + lw - 108, topY + 3, 108, 22);

    CGFloat upperCardY = topY + 30.0;
    CGFloat availCardsH = workH - 30.0 - 38.0 - 28.0;
    CGFloat upperH = std::floor(availCardsH * 0.53);
    CGFloat lowerH = availCardsH - upperH;

    NSRect upperCard = NSMakeRect(lx, upperCardY, lw, upperH);
    self.canvas.leftUpperCardRect = upperCard;
    self.popGrouping.frame = NSMakeRect(lx + 12, upperCardY + 8, 146, 26);
    self.lblGrpSzCap.frame = NSMakeRect(lx + 166, upperCardY + 12, 68, 20);
    self.edtGrpSz.frame = NSMakeRect(lx + 236, upperCardY + 9, 48, 24);
    self.btnRegroup.frame = NSMakeRect(lx + 296, upperCardY + 6, 148, 30);
    [self layoutUpperHintBar];
    self.emptyWell.frame = NSMakeRect(lx + 12, upperCardY + 80, lw - 24, upperH - 92);
    self.svQueue.frame = NSMakeRect(lx + 8, upperCardY + 80, lw - 16, upperH - 88);
    if (self.tblQueue.tableColumns.count > 0) {
        self.tblQueue.tableColumns[0].width = self.svQueue.bounds.size.width - 18;
    }

    CGFloat actY = NSMaxY(upperCard) + 6.0;
    self.btnAddFiles.frame    = NSMakeRect(lx,       actY, 64, 28);
    self.btnAddFolder.frame   = NSMakeRect(lx + 69,  actY, 64, 28);
    self.btnMergeGroups.frame = NSMakeRect(lx + 138, actY, 74, 28);
    self.btnCleanStray.frame  = NSMakeRect(lx + 217, actY, 74, 28);
    self.btnExportTopK.frame  = NSMakeRect(lx + 296, actY, 96, 28);
    self.btnClearQueue.frame  = NSMakeRect(lx + 397, actY, 59, 28);

    CGFloat inspHdrY = actY + 36.0;
    self.lblInspectorHeader.frame = NSMakeRect(lx + 2,   inspHdrY, 168, 20);
    self.lblInspectorHint.frame   = NSMakeRect(lx + 172, inspHdrY + 2, 198, 17);
    self.btnSplitFrames.frame     = NSMakeRect(lx + 374, inspHdrY - 2, 82, 24);

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
    CGFloat noExifW = 200.0;
    self.lblT1Card2Title.frame = NSMakeRect(16, c2y + 10, 176, 20);
    self.lblT1Card2Sub.frame   = NSMakeRect(196, c2y + 12, std::max<CGFloat>(0.0, pw - 16 - noExifW - 8 - 196), 18);
    self.chkNoToolExif.frame   = NSMakeRect(pw - 16 - noExifW, c2y + 9, noExifW, 22);

    self.lblOutDirCap.frame    = NSMakeRect(16, c2y + 39, 62, 20);
    self.edtOutDir.frame       = NSMakeRect(82, c2y + 37, pw - 82 - 16 - 96, 24);
    self.btnBrowseOut.frame    = NSMakeRect(pw - 108, c2y + 35, 60, 28);
    self.btnClearOut.frame     = NSMakeRect(pw - 44,  c2y + 35, 28, 28);

    self.chkEmitRaw.frame      = NSMakeRect(16, c2y + 70, 120, 22);
    CGFloat rawPopW = std::min(272.0, (double)pw - 284.0);
    self.popRawFmt.frame       = NSMakeRect(138, c2y + 68, rawPopW, 26);
    self.chkRelayRaw.frame     = NSMakeRect(138 + rawPopW + 10, c2y + 70, 134, 22);

    self.chkEmitRgb.frame      = NSMakeRect(16, c2y + 101, 88, 22);
    self.segRgbScale.frame     = NSMakeRect(106, c2y + 99, 152, 25);
    CGFloat rgbPopW = std::max(146.0, std::min(212.0, (double)pw - 494.0));
    self.popRgbFmt.frame       = NSMakeRect(264, c2y + 99, rgbPopW, 26);
    CGFloat bppX = 264 + rgbPopW + 8;
    self.lblBppCap.frame       = NSMakeRect(bppX, c2y + 103, 28, 18);
    self.edtJxsBpp.frame       = NSMakeRect(bppX + 30, c2y + 100, 44, 23);
    self.segColorSpace.frame   = NSMakeRect(bppX + 80, c2y + 99, std::min(140.0, (double)pw - (bppX + 80) - 16.0), 25);

    self.chkEmitField.frame    = NSMakeRect(16, c2y + 131, 110, 22);
    self.popFieldMode.frame    = NSMakeRect(128, c2y + 129, 228, 26);
    self.btnPortalFields.frame = NSMakeRect(pw - 170, c2y + 131, 154, 22);

    self.chkEmitTransientTab1.frame = NSMakeRect(16, c2y + 157, 370, 22);
    self.btnPortalRefl.frame        = NSMakeRect(pw - 138, c2y + 157, 122, 22);

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
    self.lblBackendCap.frame   = NSMakeRect(16,  compY + 3, 56, 18);
    self.popBackend.frame      = NSMakeRect(74,  compY, 152, 25);
    self.lblThreadsCap.frame   = NSMakeRect(244, compY + 3, 56, 18);
    self.popCoreCount.frame    = NSMakeRect(302, compY, 168, 25);
    self.edtThreads.frame      = NSMakeRect(474, compY + 1, 48, 22);

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
        heraw_i18n::tr_ns("t3.box0"),
        heraw_i18n::tr_ns("t3.box1"),
        heraw_i18n::tr_ns("t3.box2"),
        heraw_i18n::tr_ns("t3.box3")
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

    self.btnRunOptics.frame = NSMakeRect(16, p3c1H - 36, 236, 28);
    self.lblOisStatus.frame = NSMakeRect(264, p3c1H - 31, pw - 280, 20);

    CGFloat p3c2y = p3Card2.origin.y;
    self.lblT4SiliconTitle.frame  = NSMakeRect(16, p3c2y + 8, 320, 19);
    self.lblSiliconReadout.frame  = NSMakeRect(16, p3c2y + 32, pw - 32, 20);

    CGFloat p3c3y = p3Card3.origin.y;
    self.lblT4ExportTitle.frame   = NSMakeRect(16, p3c3y + 8, 320, 19);
    self.lblLcpItemsCap.frame     = NSMakeRect(16, p3c3y + 35, 84, 20);
    self.chkLcpLca.frame          = NSMakeRect(104, p3c3y + 34, 148, 22);
    self.chkLcpVignette.frame     = NSMakeRect(260, p3c3y + 34, 156, 22);
    self.chkLcpDistortion.frame   = NSMakeRect(424, p3c3y + 34, 178, 22);

    CGFloat expBtnW = std::min(216.0, (pw - 32.0 - 20.0) / 3.0);
    self.btnExportLcp.frame        = NSMakeRect(16,                        p3c3y + 64, expBtnW, 28);
    self.btnExportOpticsPng.frame  = NSMakeRect(16 + expBtnW + 10,         p3c3y + 64, expBtnW, 28);
    self.btnExportOpticsJson.frame = NSMakeRect(16 + (expBtnW + 10) * 2.0, p3c3y + 64, expBtnW, 28);
    [self.page3 setNeedsDisplay:YES];

    // --- Bottom Global Action Bar ---
    self.btnRun.frame         = NSMakeRect(20, botBarY + 10, 274, 36);
    self.btnExportLog.frame   = NSMakeRect(W - 242, botBarY + 11, 100, 34);
    self.btnOpenOutDir.frame  = NSMakeRect(W - 134, botBarY + 11, 114, 34);

    CGFloat progX = 310.0;
    CGFloat progW = W - 256.0 - progX;
    self.lblStageStatus.frame = NSMakeRect(progX, botBarY + 10, progW - 56, 18);
    self.lblPct.frame         = NSMakeRect(progX + progW - 52, botBarY + 10, 52, 18);
    self.progView.frame       = NSMakeRect(progX, botBarY + 32, progW, 11);
}

- (void)applyUiLanguage {
    if (!self.window) return;
    int minReq = [self minValidFramesRequired];
    [self setupMainMenu];
    self.window.title = heraw_i18n::tr_ns("app.window_title");
    self.btnPerfHeader.toolTip = heraw_i18n::tr_ns("hdr.perf_tip_mac");

    auto setPopupTitles = [](NSPopUpButton* pop, NSArray<NSString*>* titles) {
        if (!pop) return;
        NSInteger sel = pop.indexOfSelectedItem;
        [pop removeAllItems];
        [pop addItemsWithTitles:titles];
        if (sel >= 0 && sel < pop.numberOfItems) [pop selectItemAtIndex:sel];
    };
    auto setSegLabels = [](NSSegmentedControl* seg, NSArray<NSString*>* labels) {
        if (!seg) return;
        for (NSUInteger i = 0; i < labels.count && (NSInteger)i < seg.segmentCount; ++i) {
            [seg setLabel:labels[i] forSegment:(NSInteger)i];
        }
    };

    // Left Dock
    self.lblQueueHeader.stringValue = heraw_i18n::tr_ns("dock.queue_hdr");
    self.chkSortTime.title  = heraw_i18n::tr_ns("dock.sort_time");
    self.chkRecursive.title = heraw_i18n::tr_ns("dock.recursive");
    setPopupTitles(self.popGrouping, @[
        heraw_i18n::tr_ns("dock.grp_auto"),
        heraw_i18n::tr_ns("dock.grp_single"),
        heraw_i18n::tr_ns("dock.grp_best"),
        heraw_i18n::tr_ns("dock.grp_top1")
    ]);
    self.popGrouping.toolTip = heraw_i18n::tr_ns("dock.grp_tip");
    self.lblGrpSzCap.stringValue = heraw_i18n::tr_ns("dock.grp_sz_cap");
    self.edtGrpSz.toolTip = heraw_i18n::trf_ns("dock.grp_sz_tip_fmt", minReq);
    self.btnRegroup.title = heraw_i18n::tr_ns("dock.regroup");
    [self.emptyWell setNeedsDisplay:YES];

    self.btnAddFiles.title    = heraw_i18n::tr_ns("dock.btn_add_files");
    self.btnAddFolder.title   = heraw_i18n::tr_ns("dock.btn_add_dir");
    self.btnMergeGroups.title = heraw_i18n::tr_ns("dock.btn_merge");
    self.btnCleanStray.title  = heraw_i18n::tr_ns("dock.btn_clean");
    self.btnExportTopK.title  = heraw_i18n::tr_ns("dock.btn_topk");
    self.btnClearQueue.title  = heraw_i18n::tr_ns("dock.btn_clear");
    self.btnMergeGroups.toolTip = heraw_i18n::tr_ns("dock.tip_merge");
    self.btnCleanStray.toolTip  = heraw_i18n::trf_ns("dock.tip_clean_fmt", minReq);
    self.btnExportTopK.toolTip  = heraw_i18n::tr_ns("dock.tip_topk");

    [self refreshInspectorHeaderTitle];
    self.lblInspectorHint.stringValue  = heraw_i18n::tr_ns("dock.insp_hint_mac");
    self.btnSplitFrames.title          = heraw_i18n::tr_ns("dock.btn_split");
    self.btnSplitFrames.toolTip        = heraw_i18n::tr_ns("dock.tip_split");
    self.lblInspectorEmpty.stringValue = heraw_i18n::tr_ns("dock.insp_empty");

    // Tab 1
    self.lblT1Card1Title.stringValue = heraw_i18n::tr_ns("t1.c1_title");
    self.lblPresetCap.stringValue    = heraw_i18n::tr_ns("t1.preset_cap");
    setSegLabels(self.segPreset, @[
        heraw_i18n::tr_ns("t1.preset_photo"),
        heraw_i18n::tr_ns("t1.preset_astro"),
        heraw_i18n::tr_ns("t1.preset_sci"),
        heraw_i18n::tr_ns("t1.preset_arc")
    ]);
    self.lblMotionCap.stringValue = heraw_i18n::tr_ns("t1.motion_cap");
    setSegLabels(self.segMotionTab1, @[
        heraw_i18n::tr_ns("t1.mot_auto"),
        heraw_i18n::tr_ns("t1.mot_first"),
        heraw_i18n::tr_ns("t1.mot_middle"),
        heraw_i18n::tr_ns("t1.mot_clean"),
        heraw_i18n::tr_ns("t1.mot_vnd")
    ]);
    self.lblOlpfCap.stringValue = heraw_i18n::tr_ns("t1.olpf_cap");
    setPopupTitles(self.popOlpf, @[
        heraw_i18n::tr_ns("t1.olpf_auto"),
        heraw_i18n::tr_ns("t1.olpf_none"),
        heraw_i18n::tr_ns("t1.olpf_weak4"),
        heraw_i18n::tr_ns("t1.olpf_gauss16")
    ]);

    self.lblT1Card2Title.stringValue = heraw_i18n::tr_ns("t1.c2_title");
    self.lblT1Card2Sub.stringValue   = heraw_i18n::tr_ns("t1.c2_sub");
    self.chkNoToolExif.title         = heraw_i18n::tr_ns("t1.no_tool_exif");
    self.chkNoToolExif.toolTip       = heraw_i18n::tr_ns("t1.no_tool_exif_tip");
    self.lblOutDirCap.stringValue    = heraw_i18n::tr_ns("t1.outdir_cap");
    self.edtOutDir.placeholderString = heraw_i18n::tr_ns("t1.outdir_ph");
    self.btnBrowseOut.title          = heraw_i18n::tr_ns("t1.browse");
    self.chkEmitRaw.title            = heraw_i18n::tr_ns("t1.emit_raw");
    setPopupTitles(self.popRawFmt, @[
        heraw_i18n::tr_ns("t1.raw_dng"),
        heraw_i18n::tr_ns("t1.raw_linear"),
        heraw_i18n::tr_ns("t1.raw_fp16"),
        heraw_i18n::tr_ns("t1.raw_hq"),
        heraw_i18n::tr_ns("t1.raw_uncomp")
    ]);
    self.chkRelayRaw.title = heraw_i18n::tr_ns("t1.relay_raw");
    self.chkEmitRgb.title  = heraw_i18n::tr_ns("t1.emit_rgb");
    setSegLabels(self.segRgbScale, @[
        heraw_i18n::tr_ns("t1.scale_2x"),
        heraw_i18n::tr_ns("t1.scale_1x"),
        heraw_i18n::tr_ns("t1.scale_both")
    ]);
    self.segRgbScale.toolTip = heraw_i18n::tr_ns("t1.scale_tip");
    setPopupTitles(self.popRgbFmt, @[
        heraw_i18n::tr_ns("t1.rgb_tiff"),
        heraw_i18n::tr_ns("t1.rgb_tiff_hq"),
        heraw_i18n::tr_ns("t1.rgb_png"),
        heraw_i18n::tr_ns("t1.rgb_jxs"),
        heraw_i18n::tr_ns("t1.rgb_jxs_lossless"),
        heraw_i18n::tr_ns("t1.rgb_tiff_raw")
    ]);
    self.chkEmitField.title = heraw_i18n::tr_ns("t1.emit_field");
    setPopupTitles(self.popFieldMode, @[
        heraw_i18n::tr_ns("t1.fld_tiff_alpha"),
        heraw_i18n::tr_ns("t1.fld_split"),
        heraw_i18n::tr_ns("t1.fld_rgb")
    ]);
    self.chkEmitTransientTab1.title = heraw_i18n::tr_ns("t1.emit_trans");

    self.lblT1Card3Title.stringValue = heraw_i18n::tr_ns("t1.c3_title");
    self.btnResetPreset.title        = heraw_i18n::tr_ns("t1.reset_preset");
    self.lblDenoise.stringValue      = heraw_i18n::tr_ns("t1.denoise");
    self.lblChroma.stringValue       = heraw_i18n::tr_ns("t1.chroma");
    self.lblDeghost.stringValue      = heraw_i18n::tr_ns("t1.deghost");
    self.lblPsfIters.stringValue     = heraw_i18n::tr_ns("t1.psf_iters");
    self.lblPsfSigma.stringValue     = heraw_i18n::tr_ns("t1.psf_sigma");
    self.lblEv.stringValue           = heraw_i18n::tr_ns("t1.ev");

    self.chkSiliconClean.title = heraw_i18n::tr_ns("t1.silicon_clean");
    self.chkIbp.title          = heraw_i18n::tr_ns("t1.ibp");
    self.chkAutoLca.title      = heraw_i18n::tr_ns("t1.auto_lca");
    self.chkAutoExp.title      = heraw_i18n::tr_ns("t1.auto_exp");
    self.chkRolloff.title      = heraw_i18n::tr_ns("t1.rolloff");
    self.chkDither.title       = heraw_i18n::tr_ns("t1.dither");

    self.lblBackendCap.stringValue = heraw_i18n::tr_ns("t1.backend_cap");
    setPopupTitles(self.popBackend, @[
        heraw_i18n::tr_ns("t1.bk_auto"),
        heraw_i18n::tr_ns("t1.bk_gpu_mac"),
        heraw_i18n::tr_ns("t1.bk_cpu")
    ]);
    self.lblThreadsCap.stringValue = heraw_i18n::tr_ns("t1.threads_cap");
    setPopupTitles(self.popCoreCount, [self macCorePresetTitles]);
    self.edtThreads.toolTip   = heraw_i18n::tr_ns("t1.threads_tip");
    self.btnEngineSheet.title = heraw_i18n::tr_ns("t1.engine_btn");

    // Tab 2
    self.lblT2Card1Title.stringValue = heraw_i18n::tr_ns("t2.c1_title");
    self.radT2Freeze.title           = heraw_i18n::tr_ns("t2.freeze");
    setSegLabels(self.segT2FreezePose, @[
        heraw_i18n::tr_ns("t2.pose_auto"),
        heraw_i18n::tr_ns("t2.pose_first"),
        heraw_i18n::tr_ns("t2.pose_middle"),
        heraw_i18n::tr_ns("t2.pose_follow")
    ]);
    self.radT2Clean.title            = heraw_i18n::tr_ns("t2.clean");
    self.lblCleanSens.stringValue    = heraw_i18n::tr_ns("t2.clean_sens");
    self.radT2Vnd.title              = heraw_i18n::tr_ns("t2.vnd");
    self.lblVndThresh.stringValue    = heraw_i18n::tr_ns("t2.vnd_thresh");
    self.radT2Strobe.title           = heraw_i18n::tr_ns("t2.strobe");
    self.lblStrobeStep.stringValue   = heraw_i18n::tr_ns("t2.strobe_step");
    setPopupTitles(self.popStrobeStep, @[
        heraw_i18n::tr_ns("t2.step_1"),
        heraw_i18n::tr_ns("t2.step_2"),
        heraw_i18n::tr_ns("t2.step_3"),
        heraw_i18n::tr_ns("t2.step_4"),
        heraw_i18n::tr_ns("t2.step_5"),
        heraw_i18n::tr_ns("t2.step_6")
    ]);
    setSegLabels(self.segStrobeFade, @[
        heraw_i18n::tr_ns("t2.fade_solid"),
        heraw_i18n::tr_ns("t2.fade_trail")
    ]);

    self.lblT2Card2Title.stringValue  = heraw_i18n::tr_ns("t2.c2_title");
    self.chkEmitTransientTab2.title   = heraw_i18n::tr_ns("t2.emit_trans");
    self.lblTransSigma.stringValue    = heraw_i18n::tr_ns("t2.trans_sigma");
    setSegLabels(self.segTransColor, @[
        heraw_i18n::tr_ns("t2.trans_rgb"),
        heraw_i18n::tr_ns("t2.trans_gray")
    ]);
    self.chkReflection.title = heraw_i18n::tr_ns("t2.refl");
    setSegLabels(self.segReflectionMode, @[
        heraw_i18n::tr_ns("t2.refl_glint"),
        heraw_i18n::tr_ns("t2.refl_split")
    ]);

    self.lblT2Card3Title.stringValue = heraw_i18n::tr_ns("t2.c3_title");
    self.chkExportParallax.title     = heraw_i18n::tr_ns("t2.export_para");
    self.lblParallaxAmp.stringValue  = heraw_i18n::tr_ns("t2.para_amp");
    self.chkExportRefocus.title      = heraw_i18n::tr_ns("t2.export_refocus");
    self.lblRefocusPos.stringValue   = heraw_i18n::tr_ns("t2.refocus_pos");
    self.lblBokehStr.stringValue     = heraw_i18n::tr_ns("t2.bokeh_str");

    // Tab 3
    self.lblT3Card1Title.stringValue = heraw_i18n::tr_ns("t3.c1_title");
    self.lblCarrierCap.stringValue   = heraw_i18n::tr_ns("t3.carrier_cap");
    setSegLabels(self.segCarrierTab3, @[
        heraw_i18n::tr_ns("t3.car_off"),
        heraw_i18n::tr_ns("t3.car_tiff"),
        heraw_i18n::tr_ns("t3.car_split"),
        heraw_i18n::tr_ns("t3.car_rgb")
    ]);
    self.lblMatrixHeader.stringValue = heraw_i18n::tr_ns("t3.matrix_hdr");
    self.btnFieldsCore3.title        = heraw_i18n::tr_ns("t3.core3");
    self.btnFieldsAll.title          = heraw_i18n::tr_ns("t3.select_all");
    for (NSUInteger i = 0; i < self.chkFields.count && i < 11; ++i) {
        self.chkFields[i].title = heraw_i18n::tr_ns(kFieldSpecs[i].titleKey);
    }
    self.lblT3Card2Title.stringValue = heraw_i18n::tr_ns("t3.c2_title");
    self.chkExportTiles.title        = heraw_i18n::tr_ns("t3.export_tiles");
    self.lblTileSizeCap.stringValue  = heraw_i18n::tr_ns("t3.tile_size");
    self.lblTileConfCap.stringValue  = heraw_i18n::tr_ns("t3.tile_conf");

    // Tab 4
    self.btnRunOptics.title = heraw_i18n::tr_ns("t4.run_optics");
    [self.btnRunOptics setNeedsDisplay:YES];
    if (!self.opticsModel.populated) {
        self.opticsModel.tierBadge  = heraw_i18n::tr_ns("t4.badge_pending");
        self.opticsModel.oisSummary = heraw_i18n::tr_ns("t4.ois_idle");
    }
    self.lblT4SiliconTitle.stringValue = heraw_i18n::tr_ns("t4.silicon_title");
    self.lblT4ExportTitle.stringValue  = heraw_i18n::tr_ns("t4.export_title");
    self.lblLcpItemsCap.stringValue    = heraw_i18n::tr_ns("t4.lcp_cap");
    self.chkLcpLca.title               = heraw_i18n::tr_ns("t4.lcp_lca");
    self.chkLcpVignette.title          = heraw_i18n::tr_ns("t4.lcp_vig");
    self.chkLcpDistortion.title        = heraw_i18n::tr_ns("t4.lcp_dist");
    self.btnExportLcp.title            = heraw_i18n::tr_ns("t4.btn_lcp");
    self.btnExportOpticsPng.title      = heraw_i18n::tr_ns("t4.btn_png");
    self.btnExportOpticsJson.title     = heraw_i18n::tr_ns("t4.btn_json");

    // Bottom Bar
    self.btnExportLog.title   = heraw_i18n::tr_ns("bot.export_log");
    self.btnExportLog.toolTip = heraw_i18n::tr_ns("bot.export_log_tip");
    self.btnOpenOutDir.title  = heraw_i18n::tr_ns("bot.open_dir");
    if (!self.currentTask && !self.planTask && self.burstGroups.count == 0) {
        self.lblStageStatus.stringValue = heraw_i18n::tr_ns("bot.ready_idle");
    }

    [self updateEngineStatusStrip];
    [self relayoutForBounds:self.canvas.bounds];
    [self.tblQueue reloadData];
    [self.tblFrames reloadData];
    [self syncAllUiState];
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
    int totC = 8, perfC = 6;
    detectMacCoreCounts(totC, perfC);
    self.hostTotalCores = totC;
    self.hostPerfCores  = perfC;
    self.hostHalfCores  = std::max(1, totC / 2);

    NSString* selfExe = [[NSBundle mainBundle] executablePath];
    if (!selfExe) selfExe = [[[NSProcessInfo processInfo] arguments] firstObject];
    NSString* prefLang = [nijinPrefs() stringForKey:@"LangCode"];
    heraw_i18n::I18nManager::instance().init(selfExe ? ([selfExe UTF8String] ?: "") : "",
                                             prefLang ? ([prefLang UTF8String] ?: "") : "");
    self.langCode = [NSString stringWithUTF8String:heraw_i18n::I18nManager::instance().active_code().c_str()];

    NSString* osVer = [[NSProcessInfo processInfo] operatingSystemVersionString] ?: @"macOS";
    std::string platInfo = std::string("macOS ") + ([osVer UTF8String] ?: "") +
        " | CPU=" + std::to_string(totC) + " (" + std::to_string(perfC) + "P)";
    heraw_i18n::diag_log().init_session(platInfo);
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
    self.customCliPath = [nijinPrefs() stringForKey:kPrefCliPath];
    self.customLibRawPath = [nijinPrefs() stringForKey:kPrefLibRawPath];

    NSImage* appIcon = loadEmbeddedPng(heraw_brand::kAppIconPng, sizeof(heraw_brand::kAppIconPng), NSMakeSize(256, 256));
    if (appIcon) [NSApp setApplicationIconImage:appIcon];

    __weak AppDelegate* weakSelf = self;
    NijiLbsApplication* nijiApp = (NijiLbsApplication*)NSApp;
    nijiApp.onCmdO = ^{ [weakSelf onOpenMixedPanel:nil]; };
    nijiApp.onCmdL = ^{ [weakSelf onExportDiagLog:nil]; };
    nijiApp.onCmdComma = ^{ [weakSelf onOpenEngineSheet:nil]; };
    nijiApp.onDeleteGroup = ^{ [weakSelf onRemoveSelectedGroup:nil]; };
    nijiApp.onMergeGroups = ^{ [weakSelf onMergeSelectedGroups:nil]; };
    nijiApp.onSplitFrames = ^{ [weakSelf onSplitSelectedFrames:nil]; };

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

    self.btnPerfHeader = [[PortalPillButton alloc] initWithFrame:NSZeroRect];
    self.btnPerfHeader.target = self;
    self.btnPerfHeader.action = @selector(onPerfHeaderClicked:);
    self.btnPerfHeader.toolTip = @"性能与硬件加速快捷菜单 (显卡加速 / CPU核数预算 / 引擎设置 ⌘,)";
    [self.canvas addSubview:self.btnPerfHeader];

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
    self.popGrouping = [[NSPopUpButton alloc] initWithFrame:NSZeroRect pullsDown:NO];
    [self.popGrouping addItemsWithTitles:@[@"自动分组", @"整组处理", @"每簇精选", @"全库挑1组"]];
    self.popGrouping.toolTip = @"自动分组：自适应快门/节奏并按谷点均衡拆组\n整组处理：所有导入帧合为 1 组\n每簇精选：每段连拍挑最优相位帧合为 1 组\n全库挑1组：大资料库仅自动勾选画质最优的 1 组";
    self.popGrouping.target = self;
    self.popGrouping.action = @selector(onGroupingChanged:);
    [self.canvas addSubview:self.popGrouping];
    self.lblGrpSzCap = makeLabel(self.canvas, @"目标张数", 11.5, NO, nil);
    self.edtGrpSz = makeEdit(self.canvas, @"8", nil);
    self.edtGrpSz.toolTip = @"自动分组或每簇精选的目标张数（至少 4 张）。";
    self.btnRegroup = makeButton(self.canvas, @"重新分组", @selector(onRegroup:));
    self.lblGroupingHint = makeLabel(self.canvas, @"导入后自动剖析连拍簇（支持 ⌘/⇧ 多选、空格批勾、合并与拆分）", 10.5, NO, textMuted);
    self.lblGroupingHint.maximumNumberOfLines = 2;
    self.lblGroupingHint.lineBreakMode = NSLineBreakByWordWrapping;

    self.btnIntakeAction = [[PortalPillButton alloc] initWithFrame:NSZeroRect];
    self.btnIntakeAction.activeHighlight = YES;
    self.btnIntakeAction.target = self;
    self.btnIntakeAction.action = @selector(onIntakeActionClicked:);
    self.btnIntakeAction.hidden = YES;
    [self.canvas addSubview:self.btnIntakeAction];

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
    self.tblQueue.allowsMultipleSelection = YES;
    self.tblQueue.onDeletePressed = ^{ [weakSelf onRemoveSelectedGroup:nil]; };
    self.tblQueue.onSpacePressed  = ^{ [weakSelf onToggleSelectedGroupsChecked:nil]; };
    self.tblQueue.menuProvider    = ^NSMenu*(NSInteger r) { return [weakSelf buildQueueContextMenuForRow:r]; };
    NSTableColumn* qCol = [[NSTableColumn alloc] initWithIdentifier:@"BurstCol"];
    qCol.width = 410;
    [self.tblQueue addTableColumn:qCol];
    self.tblQueue.dataSource = self;
    self.tblQueue.delegate = self;
    self.svQueue.documentView = self.tblQueue;
    self.svQueue.hidden = YES;
    [self.canvas addSubview:self.svQueue];

    self.btnAddFiles    = makeButton(self.canvas, @"+文件",     @selector(onAddFiles:));
    self.btnAddFolder   = makeButton(self.canvas, @"+目录",     @selector(onAddFolder:));
    self.btnMergeGroups = makeButton(self.canvas, @"合并组",    @selector(onMergeSelectedGroups:));
    self.btnCleanStray  = makeButton(self.canvas, @"清散片",    @selector(onCleanStrayGroups:));
    self.btnExportTopK  = makeButton(self.canvas, @"Top-K选片", @selector(onExportTopK:));
    self.btnClearQueue  = makeButton(self.canvas, @"清空",      @selector(onClearQueue:));
    for (NSButton* b in @[self.btnAddFiles, self.btnAddFolder, self.btnMergeGroups, self.btnCleanStray, self.btnExportTopK, self.btnClearQueue]) {
        b.font = [NSFont systemFontOfSize:11.5];
    }
    self.btnMergeGroups.toolTip = @"将 ⌘/⇧ 多选的多个连拍组合并为同一组 (⌘M)";
    self.btnCleanStray.toolTip  = @"一键移除队列中不足 4 张的单拍与散片";
    self.btnExportTopK.toolTip  = @"按 AF 对焦点锐度直接筛出 Top-3 原片拷贝";

    self.lblInspectorHeader = makeLabel(self.canvas, @"组内单帧检视", 13.0, YES, nil);
    self.lblInspectorHint   = makeLabel(self.canvas, @"⌘/⇧多选 · 空格启停 · 点●定格", 10.5, NO, textMuted);
    self.lblInspectorHint.alignment = NSTextAlignmentRight;
    self.btnSplitFrames = makeButton(self.canvas, @"拆为新组", @selector(onSplitSelectedFrames:));
    self.btnSplitFrames.controlSize = NSControlSizeSmall;
    self.btnSplitFrames.font = [NSFont systemFontOfSize:11.0];
    self.btnSplitFrames.toolTip = @"将下方选中的帧拆分为独立的新连拍组 (⇧⌘N)";

    self.lblInspectorEmpty = makeLabel(self.canvas, @"选择连拍组后在此内联检视逐帧 AF 锐度、位移与微震标记", 11.5, NO, textMuted);
    self.lblInspectorEmpty.alignment = NSTextAlignmentCenter;

    self.svFrames = [[NSScrollView alloc] initWithFrame:NSZeroRect];
    self.svFrames.hasVerticalScroller = YES;
    self.svFrames.drawsBackground = NO;
    self.svFrames.borderType = NSNoBorder;
    self.tblFrames = [[FrameInspectorTableView alloc] initWithFrame:NSMakeRect(0, 0, 420, 180)];
    self.tblFrames.rowHeight = 28.0;
    self.tblFrames.backgroundColor = [NSColor clearColor];
    self.tblFrames.headerView = nil;
    self.tblFrames.allowsMultipleSelection = YES;
    self.tblFrames.onSpacePressed = ^{ [weakSelf onToggleSelectedFramesEnabled:nil]; };
    self.tblFrames.menuProvider   = ^NSMenu*(NSInteger r) { return [weakSelf buildFrameContextMenuForRow:r]; };
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
    self.chkNoToolExif   = makeCheck(self.page0, heraw_i18n::tr_ns("t1.no_tool_exif"), NO, @selector(onGenericOptionChanged:));
    self.chkNoToolExif.font    = [NSFont systemFontOfSize:11.5];
    self.chkNoToolExif.toolTip = heraw_i18n::tr_ns("t1.no_tool_exif_tip");

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

    self.chkEmitRgb = makeCheck(self.page0, heraw_i18n::tr_ns("t1.emit_rgb"), YES, @selector(onGenericOptionChanged:));
    self.segRgbScale = [NSSegmentedControl segmentedControlWithLabels:@[
        heraw_i18n::tr_ns("t1.scale_2x"),
        heraw_i18n::tr_ns("t1.scale_1x"),
        heraw_i18n::tr_ns("t1.scale_both")
    ] trackingMode:NSSegmentSwitchTrackingSelectOne target:self action:@selector(onGenericOptionChanged:)];
    self.segRgbScale.selectedSegment = 0;
    self.segRgbScale.toolTip = heraw_i18n::tr_ns("t1.scale_tip");
    [self.page0 addSubview:self.segRgbScale];

    self.popRgbFmt  = [[NSPopUpButton alloc] initWithFrame:NSZeroRect pullsDown:NO];
    [self.popRgbFmt addItemsWithTitles:@[
        heraw_i18n::tr_ns("t1.rgb_tiff"),
        heraw_i18n::tr_ns("t1.rgb_tiff_hq"),
        heraw_i18n::tr_ns("t1.rgb_png"),
        heraw_i18n::tr_ns("t1.rgb_jxs"),
        heraw_i18n::tr_ns("t1.rgb_jxs_lossless"),
        heraw_i18n::tr_ns("t1.rgb_tiff_raw")
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

    self.lblBackendCap = makeLabel(self.page0, @"显卡加速", 11.5, NO, nil);
    self.popBackend = [[NSPopUpButton alloc] initWithFrame:NSZeroRect pullsDown:NO];
    [self.popBackend addItemsWithTitles:@[@"自动(GPU优先)", @"强制Metal GPU", @"纯CPU"]];
    self.popBackend.target = self;
    self.popBackend.action = @selector(onGenericOptionChanged:);
    [self.page0 addSubview:self.popBackend];

    self.lblThreadsCap = makeLabel(self.page0, @"CPU核数", 11.5, NO, nil);
    self.popCoreCount = [[NSPopUpButton alloc] initWithFrame:NSZeroRect pullsDown:NO];
    [self.popCoreCount addItemsWithTitles:[self macCorePresetTitles]];
    self.popCoreCount.target = self;
    self.popCoreCount.action = @selector(onCoreCountPopupChanged:);
    [self.page0 addSubview:self.popCoreCount];
    self.edtThreads    = makeEdit(self.page0, @"0", nil);
    self.edtThreads.toolTip = @"CPU 工作核数 (0 = 自动全核)";

    self.lblEngineStrip = makeLabel(self.page0, @"引擎: 内置 LibRaw 0.23-HE*", 11.0, NO, textMuted);
    self.btnEngineSheet = makeButton(self.page0, @"性能与引擎...", @selector(onOpenEngineSheet:));
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
    self.sldVndThresh = makeSlider(self.page1, 0.10, 1.00, 0.65);
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
        NSButton* cb = makeCheck(self.page2, heraw_i18n::tr_ns(kFieldSpecs[i].titleKey), defOn, @selector(onGenericOptionChanged:));
        cb.font = [NSFont systemFontOfSize:11.5];
        [self.chkFields addObject:cb];
    }
    self.lblFieldsGateNote = makeLabel(self.page2, heraw_i18n::tr_ns("t3.gate_idle"), 10.5, NO, textMuted);

    self.lblT3Card2Title = makeLabel(self.page2, heraw_i18n::tr_ns("t3.c2_title"), 13.0, YES, nil);
    self.chkExportTiles  = makeCheck(self.page2, heraw_i18n::tr_ns("t3.export_tiles"), NO, @selector(onGenericOptionChanged:));
    self.lblTileSizeCap  = makeLabel(self.page2, heraw_i18n::tr_ns("t3.tile_size"), 12.0, NO, nil);
    self.segTileSize = [NSSegmentedControl segmentedControlWithLabels:@[@"256×256", @"512×512"]
                                                         trackingMode:NSSegmentSwitchTrackingSelectOne
                                                               target:self action:@selector(onGenericOptionChanged:)];
    self.segTileSize.selectedSegment = 0;
    [self.page2 addSubview:self.segTileSize];
    self.lblTileConfCap = makeLabel(self.page2, heraw_i18n::tr_ns("t3.tile_conf"), 12.0, NO, nil);
    self.sldTileConf    = makeSlider(self.page2, 0.30, 0.99, 0.75);
    self.edtTileConf    = makeEdit(self.page2, @"0.75", nil);

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

    self.btnExportLog  = makeButton(self.canvas, heraw_i18n::tr_ns("bot.export_log"), @selector(onExportDiagLog:));
    self.btnOpenOutDir = makeButton(self.canvas, heraw_i18n::tr_ns("bot.open_dir"), @selector(onOpenOutDir:));

    // Apply initial defaults, localization & layout
    [self applyPresetIndex:0];
    [self applyUiLanguage];
    [self relayoutForBounds:self.canvas.bounds];
    [self updateQueueEmptyVisibility];

    // Parse Process Arguments
    NSArray<NSString*>* procArgs = [[NSProcessInfo processInfo] arguments];
    NSString* shotPath = nil;
    NSString* cliLang = nil;
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
        } else if ([a isEqualToString:@"--lang"] && i + 1 < procArgs.count) {
            cliLang = procArgs[++i];
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
    if (cliLang.length > 0) {
        [self setLanguageCodeAndRefresh:cliLang persist:NO];
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
    NSDictionary* backupNew = [[ud persistentDomainForName:kPrefSuite] copy];

    [ud removePersistentDomainForName:kPrefSuite];
    [ud synchronize];

    self.suppressPrefSave = YES;
    [self setLanguageCodeAndRefresh:@"zh-CN" persist:NO];
    self.suppressPrefSave = NO;

    NSString* savedCustomCli = self.customCliPath;
    self.customCliPath = nil;
    [ud setObject:@"/usr/bin/true" forKey:@"NijinLbsCustomCliPath"];
    NSString* bundledCli = [self autoDetectCliPath:NO];
    bool engineMigrationOk = bundledCli && [[self autoDetectCliPath:YES] isEqualToString:bundledCli];
    [ud setObject:@"/usr/bin/true" forKey:kPrefCliPath];
    engineMigrationOk &= [[self autoDetectCliPath:YES] isEqualToString:@"/usr/bin/true"];
    [ud removeObjectForKey:kPrefCliPath];

    // 1. Verify fresh lossless defaults + 2x (>=5) default scale
    bool defaultLosslessOk =
        (self.chkEmitRaw.state == NSControlStateValueOn) &&
        (self.popRawFmt.indexOfSelectedItem == 0) &&
        ([self.popRawFmt.titleOfSelectedItem hasPrefix:@"DNG 16-bit 无损"]) &&
        (self.chkEmitRgb.state == NSControlStateValueOn) &&
        ([[self selectedRgbScaleCode] isEqualToString:@"2x"]) &&
        ([[self selectedRgbFormatCode] isEqualToString:@"tiff-deflate"]) &&
        ([self.popRgbFmt.titleOfSelectedItem hasPrefix:@"16-bit 无损 TIFF"]) &&
        ([self minValidFramesRequired] == 5) &&
        ([self selectedFieldCount] == 3) &&
        (self.popFieldMode.indexOfSelectedItem == 1) &&
        (self.chkNoToolExif.state == NSControlStateValueOff) &&
        (self.tabBar.selectedTab == 0);

    // 2. UI settings persist across versions; engine paths do not.
    [ud setBool:YES forKey:@"HasSavedUiPrefs"];
    [ud setObject:@"dng-fp16" forKey:@"RawFmtCode"];
    [ud setObject:@"png" forKey:@"RgbFmtCode"];
    [ud setObject:@"both" forKey:@"RgbScaleCode"];
    [ud setObject:@"/old/libraw.dylib" forKey:@"NijinLbsCustomLibRawPath"];
    [ud setObject:@"astro" forKey:@"PresetCode"];
    [ud synchronize];

    [self loadUiPrefs];
    bool versionIsolationOk =
        (self.popRawFmt.indexOfSelectedItem == 2) &&
        ([[self selectedRgbFormatCode] isEqualToString:@"png"]) &&
        ([[self selectedRgbScaleCode] isEqualToString:@"both"]) &&
        (self.segPreset.selectedSegment == 1) &&
        ![ud objectForKey:kPrefLibRawPath] &&
        [[self autoDetectCliPath:YES] isEqualToString:bundledCli];
    self.customCliPath = savedCustomCli;

    // 3. Mutate v2.0.2 suite options (formats including tiff-hq/jxs, tab, field matrix, creative motion, sliders) & save
    [self selectTab:2];
    [self.popRawFmt selectItemAtIndex:1]; // dng-linear
    [self.popRgbFmt selectItemAtIndex:[rgbFormatCodes() indexOfObject:@"jxs"]];
    self.segRgbScale.selectedSegment = 2; // both
    self.edtJxsBpp.stringValue = @"12.5";
    self.segColorSpace.selectedSegment = 1; // Adobe RGB
    self.chkEmitField.state = NSControlStateValueOn;
    self.chkNoToolExif.state = NSControlStateValueOn;
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
    [self.popGrouping selectItemAtIndex:1];
    self.edtGrpSz.stringValue = @"9";
    [self.popBackend selectItemAtIndex:2]; // 纯CPU
    [self.popCoreCount selectItemAtIndex:1]; // 性能核数
    [self onCoreCountPopupChanged:self.popCoreCount];
    [self syncAllUiState];
    [self saveUiPrefs];

    // Reset controls to initial defaults, then reload from com.nijin.liteburstsuite
    [self.popGrouping selectItemAtIndex:0];
    self.edtGrpSz.stringValue = @"8";
    self.suppressPrefSave = YES;
    [self.popBackend selectItemAtIndex:0];
    self.edtThreads.stringValue = @"0";
    [self syncCoreCountPopupFromThreads];
    [self selectTab:0];
    [self.popRawFmt selectItemAtIndex:0];
    [self.popRgbFmt selectItemAtIndex:0];
    self.segRgbScale.selectedSegment = 0;
    self.edtJxsBpp.stringValue = @"10.0";
    self.segColorSpace.selectedSegment = 0;
    self.chkEmitField.state = NSControlStateValueOff;
    self.chkNoToolExif.state = NSControlStateValueOff;
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

    bool roundtripOk = self.popGrouping.indexOfSelectedItem == 1 && self.edtGrpSz.integerValue == 9 &&
        (self.tabBar.selectedTab == 2) &&
        (self.popRawFmt.indexOfSelectedItem == 1) &&
        ([[self selectedRgbFormatCode] isEqualToString:@"jxs"]) &&
        ([[self selectedRgbScaleCode] isEqualToString:@"both"]) &&
        ([self.edtJxsBpp.stringValue isEqualToString:@"12.5"]) &&
        (self.segColorSpace.selectedSegment == 1) &&
        (self.chkEmitField.state == NSControlStateValueOn) &&
        (self.chkNoToolExif.state == NSControlStateValueOn) &&
        (self.popFieldMode.indexOfSelectedItem == 1) &&
        ([self selectedFieldCount] == 5) &&
        ([self.motionMode isEqualToString:@"clean-plate"]) &&
        ([self.edtDenoise.stringValue isEqualToString:@"0.62"]);
    bool perfMenuOk = (self.popBackend.indexOfSelectedItem == 2) &&
        (self.popCoreCount.indexOfSelectedItem == 1) &&
        (self.edtThreads.integerValue == self.hostPerfCores) &&
        ([self.btnPerfHeader.title containsString:@"纯CPU"]);
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
    [self.popRgbFmt selectItemAtIndex:[rgbFormatCodes() indexOfObject:@"tiff-hq"]];
    self.segCarrierTab3.selectedSegment = 1; // tiff-alpha should accept tiff-hq!
    [self onCarrierTab3Changed:self.segCarrierTab3];
    syncOk = syncOk && self.chkEmitRgb.state == NSControlStateValueOn &&
        [[self selectedRgbFormatCode] isEqualToString:@"tiff-hq"] && self.chkFields.count == 11;
    std::string fixture = R"({"clusters" : [{"focal_len":400,"depth_observable":false,"frames" : [
        {"idx":1,"path":"/raw/\u76f8\u673a\ud83d\ude00.NEF","file":"a.NEF","group":0,"dx_1x":1.5,"dy_1x":-2.0},
        {"idx":2,"path":"/raw/b.NEF","file":"b.NEF","group":0,"dx":0,"dy":0}
    ]}]})";
    NSArray<BurstGroupModel*>* parsed = [self parsePlanJsonOutput:fixture basenameMap:@{}];
    bool parserOk = parsed.count == 1 && parsed[0].frames.count == 2 &&
        [parsed[0].filePaths[0] isEqualToString:@"/raw/相机😀.NEF"] &&
        !parsed[0].depthKnown && !parsed[0].hasMeasuredParallax &&
        parsed[0].maxParallax2xPx == 0.0 && parsed[0].frames[0].dxPx == 1.5 && parsed[0].focalMm == 400;
    BurstGroupModel* sample = parsed.firstObject;
    self.groupingDirty = YES;
    [self updateRunButtonTitle];
    bool groupingOk = !self.btnRun.enabled && !self.edtGrpSz.enabled;
    self.groupingDirty = NO;
    [self updateRunButtonTitle];
    groupingOk &= self.btnRun.enabled;
    sample.frames[1].enabled = NO;
    self.motionMode = @"strobe";
    self.segStrobeFade.selectedSegment = 1;
    self.chkExportTiles.state = NSControlStateValueOn;
    NSArray<NSString*>* argv = [self buildCliArgsForGroup:sample outDir:@"/输出 空格" multiGroup:YES];
    auto option = [&](NSString* key) -> NSString* {
        NSUInteger idx = [argv indexOfObject:key];
        return idx != NSNotFound && idx + 1 < argv.count ? argv[idx + 1] : @"";
    };
    bool argsOk = [option(@"--policy") isEqualToString:@"force-all"] &&
        [option(@"--backend") isEqualToString:@"cpu"] &&
        [option(@"--threads") integerValue] == self.hostPerfCores &&
        [option(@"--rgb-format") isEqualToString:@"tiff-hq"] &&
        [option(@"--rgb-scale") isEqualToString:@"both"] &&
        [option(@"--no-tool-exif") isEqualToString:@"on"] &&
        [argv containsObject:@"--single-group"] && [option(@"--strobe-fade") isEqualToString:@"1"] &&
        [option(@"--skip-frames") isEqualToString:@"2"] &&
        [option(@"--export-dataset-tiles") isEqualToString:@"/输出 空格/dataset_tiles/group_01"] &&
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

    // Verify 2x >= 5 vs 1x >= 3 suitability + mixed-library stray folding + merge / split / clean strays
    self.chkEmitRaw.state = NSControlStateValueOn;
    self.segRgbScale.selectedSegment = 0; // 2x -> minReq = 5
    std::string libFixture = R"({"clusters":[
        {"sr_score":92,"frames":[
            {"idx":1,"path":"/lib/b1.NEF","file":"b1.NEF","group":0,"af_score":90},
            {"idx":2,"path":"/lib/b2.NEF","file":"b2.NEF","group":0,"af_score":95},
            {"idx":3,"path":"/lib/b3.NEF","file":"b3.NEF","group":0,"af_score":88},
            {"idx":4,"path":"/lib/b4.NEF","file":"b4.NEF","group":0,"af_score":91},
            {"idx":5,"path":"/lib/b5.NEF","file":"b5.NEF","group":0,"af_score":93}
        ]},
        {"sr_score":86,"frames":[
            {"idx":1,"path":"/lib/c1.NEF","file":"c1.NEF","group":0,"af_score":80},
            {"idx":2,"path":"/lib/c2.NEF","file":"c2.NEF","group":0,"af_score":84},
            {"idx":3,"path":"/lib/c3.NEF","file":"c3.NEF","group":0,"af_score":82}
        ]},
        {"frames":[{"idx":1,"path":"/lib/s1.NEF","file":"s1.NEF","group":-1,"af_score":50}]},
        {"frames":[{"idx":1,"path":"/lib/s2.NEF","file":"s2.NEF","group":-1,"af_score":50}]},
        {"frames":[{"idx":1,"path":"/lib/s3.NEF","file":"s3.NEF","group":-1,"af_score":50}]}
    ]})";
    NSArray<BurstGroupModel*>* libParsed = [self parsePlanJsonOutput:libFixture basenameMap:@{}];
    bool multiIntakeOk = (libParsed.count == 3) &&
        libParsed[0].checked && (libParsed[0].frames.count == 5) &&
        !libParsed[1].checked && (libParsed[1].frames.count == 3) &&
        libParsed[2].isStrayArchive && !libParsed[2].checked && (libParsed[2].frames.count == 3) &&
        self.tblQueue.allowsMultipleSelection && self.tblFrames.allowsMultipleSelection;
    [self.burstGroups setArray:libParsed];
    [self.tblQueue reloadData];
    // Switch to pure 1x RGB (!emitRaw && emitRgb && scale=1x) -> minReq=3 -> 3-frame group c1..c3 auto-checks!
    self.chkEmitRaw.state = NSControlStateValueOff;
    self.segRgbScale.selectedSegment = 1; // 1x
    [self reapplySuitabilityForCurrentThreshold];
    NSArray<NSString*>* argv1x = [self buildCliArgsForGroup:self.burstGroups[1] outDir:@"/out1x"];
    multiIntakeOk = multiIntakeOk && ([self minValidFramesRequired] == 3) &&
        self.burstGroups[0].checked && self.burstGroups[1].checked && !self.burstGroups[2].checked &&
        [argv1x containsObject:@"--emit-rgb-1x"] && ![argv1x containsObject:@"--emit-raw"];
    // Switch back to 2x -> minReq=5 -> 3-frame group unchecks
    self.chkEmitRaw.state = NSControlStateValueOn;
    self.segRgbScale.selectedSegment = 0; // 2x
    [self reapplySuitabilityForCurrentThreshold];
    multiIntakeOk = multiIntakeOk && ([self minValidFramesRequired] == 5) &&
        self.burstGroups[0].checked && !self.burstGroups[1].checked;

    // Multi-select groups 0 and 1, merge -> 1 group of 8 frames + 1 stray archive of 3 frames
    NSIndexSet* pairSel = [NSIndexSet indexSetWithIndexesInRange:NSMakeRange(0, 2)];
    [self.tblQueue selectRowIndexes:pairSel byExtendingSelection:NO];
    [self onMergeSelectedGroups:nil];
    multiIntakeOk = multiIntakeOk && (self.burstGroups.count == 2) &&
        (self.burstGroups[0].frames.count == 8) && self.burstGroups[0].checked;
    // Multi-select last 3 frames in Frame Inspector, split -> 3 groups (5 frames + 3 frames + 3 stray)
    [self.tblQueue selectRowIndexes:[NSIndexSet indexSetWithIndex:0] byExtendingSelection:NO];
    [self.tblFrames reloadData];
    [self.tblFrames selectRowIndexes:[NSIndexSet indexSetWithIndexesInRange:NSMakeRange(5, 3)] byExtendingSelection:NO];
    [self onSplitSelectedFrames:nil];
    multiIntakeOk = multiIntakeOk && (self.burstGroups.count == 3) &&
        (self.burstGroups[0].frames.count == 5) && (self.burstGroups[1].frames.count == 3);
    // Batch toggle checked via Space handler on groups 0 & 1
    [self.tblQueue selectRowIndexes:pairSel byExtendingSelection:NO];
    [self onToggleSelectedGroupsChecked:nil];
    multiIntakeOk = multiIntakeOk && self.burstGroups[0].checked && self.burstGroups[1].checked;
    // Clean strays (<5 frames + folded single-shot archive) -> leaves 1 valid 5-frame group
    [self onCleanStrayGroups:nil];
    multiIntakeOk = multiIntakeOk && (self.burstGroups.count == 1) && (self.burstGroups[0].frames.count == 5);

    // Verify .lng live language switching (zh-CN <-> en-US) and diagnostic log export
    [self setLanguageCodeAndRefresh:@"en-US" persist:NO];
    bool i18nOk = ([self.btnExportLog.title isEqualToString:@"Export Log"]) &&
        ([self.btnOpenOutDir.title isEqualToString:@"Open Folder"]) &&
        ([self.popRawFmt.itemTitles.firstObject isEqualToString:@"DNG 16-bit Lossless"]) &&
        ([self.segRgbScale labelForSegment:1] && [[self.segRgbScale labelForSegment:1] isEqualToString:@"1×True"]) &&
        (!heraw_i18n::I18nManager::instance().active_source_path().empty());
    [self setLanguageCodeAndRefresh:@"zh-CN" persist:NO];
    i18nOk = i18nOk && ([self.btnExportLog.title isEqualToString:@"导出日志"]) &&
        ([self.popRawFmt.itemTitles.firstObject isEqualToString:@"DNG 16-bit 无损"]);

    NSString* tmpLog = [NSTemporaryDirectory() stringByAppendingPathComponent:
        [@"nijin_selftest_" stringByAppendingString:NSUUID.UUID.UUIDString]];
    BOOL logExportSaved = [self exportDiagLogToPath:tmpLog];
    NSString* logContent = [NSString stringWithContentsOfFile:tmpLog encoding:NSUTF8StringEncoding error:nil];
    [[NSFileManager defaultManager] removeItemAtPath:tmpLog error:nil];
    bool logExportOk = logExportSaved && logContent.length > 80 &&
        [logContent containsString:@"NijiN.LiteBurstSuite"] &&
        [logContent containsString:@"Language switched to en-US"];

    // Restore original user domains cleanly
    [ud removePersistentDomainForName:kPrefSuite];
    if (backupNew) [ud setPersistentDomain:backupNew forName:kPrefSuite];
    [ud synchronize];

    bool ok = defaultLosslessOk && versionIsolationOk && engineMigrationOk && roundtripOk && perfMenuOk &&
              syncOk && parserOk && argsOk && lcpOk && refocusOk && groupingOk && multiIntakeOk && i18nOk && logExportOk;
    std::printf("PERF_MENU_TEST:%s\n", perfMenuOk ? "PASS" : "FAIL");
    std::printf("GROUPING_CONTROLS_TEST:%s\n", groupingOk ? "PASS" : "FAIL");
    std::printf("MULTI_SELECT_INTAKE_TEST:%s\n", multiIntakeOk ? "PASS" : "FAIL");
    std::printf("ENGINE_MIGRATION_TEST:%s\n", engineMigrationOk ? "PASS" : "FAIL");
    std::printf("I18N_LNG_TEST:%s\n", i18nOk ? "PASS" : "FAIL");
    std::printf("LOG_EXPORT_TEST:%s\n", logExportOk ? "PASS" : "FAIL");
    std::printf("REFOCUS_ARGV_TEST:%s offsets=-1,0,1 positions=0,0.5,1 strengths=0,1,3 radii=0,12,36 prefs=UI\n",
        refocusOk ? "PASS" : "FAIL");
    std::printf("LCP_TEST:%s\n", lcpOk ? "PASS" : "FAIL");
    std::fprintf(stdout, "GUI_CONTRACT:defaults=%d ui_memory_engine_isolation=%d roundtrip=%d sync=%d unicode=%d argv=%d\n",
        defaultLosslessOk, versionIsolationOk, roundtripOk, syncOk, parserOk, argsOk);
    std::fprintf(stdout, "PREFS_TEST:%s\n", ok ? "PASS" : "FAIL");
    std::fflush(stdout);
    std::exit(ok ? 0 : 1);
}

- (void)runProcessSelfTestAndExit {
    self.suppressPrefSave = YES;
    [self setLanguageCodeAndRefresh:@"zh-CN" persist:NO];
    NSString* flood = @"i=0; while [ \"$i\" -lt 12000 ]; do printf 'diagnostic-output-abcdefghijklmnopqrstuvwxyz0123456789\\n' >&2; i=$((i+1)); done; exit 7";
    [self runUtilityCliTask:@"/bin/sh" args:@[@"-c", flood] label:@"测试管道排空" completion:^(int rc) {
        if (rc != 7 || self.currentTask) std::exit(1);
        std::string snap = heraw_i18n::diag_log().snapshot();
        if (snap.find("diagnostic-output-abcdefghijklmnopqrstuvwxyz0123456789") == std::string::npos) std::exit(4);
        [self runUtilityCliTask:@"/bin/sleep" args:@[@"30"] label:@"测试取消" completion:^(int status) {
            (void)status;
            std::exit(2);
        }];
        dispatch_after(dispatch_time(DISPATCH_TIME_NOW, 100 * NSEC_PER_MSEC), dispatch_get_main_queue(), ^{
            [self onRunOrStop:nil];
        });
        dispatch_after(dispatch_time(DISPATCH_TIME_NOW, NSEC_PER_SEC), dispatch_get_main_queue(), ^{
            bool ok = self.userStoppedJob && self.currentTask == nil &&
                [self.lblStageStatus.stringValue isEqualToString:heraw_i18n::tr_ns("st.task_stopped")];
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
