// heraw_sr2x_gui_win.cpp -- NijiN.LiteBurstSuite (nijinLBS) Native Win32 + GDI+ 4-Tab Suite Workbench
#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif
#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0601
#endif
#define WIN32_LEAN_AND_MEAN
#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <windows.h>
#include <windowsx.h>
#include <objidl.h>
#include <gdiplus.h>
#include <commctrl.h>
#include <commdlg.h>
#include <shellapi.h>
#include <shlobj.h>

#include <algorithm>
#include <atomic>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cwctype>
#include <mutex>
#include <map>
#include <numbers>
#include <string>
#include <thread>
#include <vector>

#include "heraw_brand_assets.hpp"
#include "heraw_cpu_cache.hpp"
#include "heraw_gui_i18n.hpp"
#include "heraw_version.hpp"

#ifndef EM_SETCUEBANNER
#define EM_SETCUEBANNER 0x1501
#endif
#ifndef IDI_APPICON
#define IDI_APPICON 101
#endif

namespace {

enum CtrlId {
    // Left Dock: Burst Queue & Inline Per-Frame AF Sharpness Inspector
    ID_CHK_SORT_TIME = 201,
    ID_CHK_RECURSIVE,
    ID_VIEW_BURST_QUEUE,
    ID_BTN_ADD_FILES,
    ID_BTN_ADD_FOLDER,
    ID_BTN_MERGE_GROUPS,
    ID_BTN_CLEAN_STRAY,
    ID_BTN_EXPORT_TOPK,
    ID_BTN_CLEAR,
    ID_BTN_SPLIT_FRAMES,
    ID_BTN_LOCK_FRAME,
    ID_VIEW_FRAME_INSP,
    ID_CMB_GROUPING,
    ID_BTN_REGROUP,
    ID_BTN_INTAKE_ACTION,

    // Workspace 4-Segment Tab Bar
    ID_TAB_BAR = 220,

    // Tab 1: 1 超分与主输出
    ID_BTN_PORTAL_TAB4_OPTICS = 230,
    ID_SEG_PRESET,
    ID_SEG_MOTION_TAB1,
    ID_BTN_PORTAL_TAB2_MOTION,
    ID_CMB_OLPF_MODE,

    ID_EDT_OUT_DIR,
    ID_BTN_OUT_BROWSE,
    ID_BTN_OUT_CLEAR,

    ID_CHK_EMIT_RAW,
    ID_CMB_RAW_FMT,
    ID_CHK_RELAY_RAW,
    ID_CHK_NO_TOOL_EXIF,

    ID_CHK_EMIT_RGB,
    ID_SEG_RGB_SCALE,
    ID_CMB_RGB_FMT,
    ID_EDT_JXS_BPP,
    ID_RAD_CS_SRGB,
    ID_RAD_CS_ADOBERGB,

    ID_CHK_EMIT_FIELD,
    ID_CMB_FIELD_MODE,
    ID_BTN_PORTAL_TAB3_FIELDS,

    ID_CHK_TRANSIENT_TAB1,
    ID_BTN_PORTAL_TAB2_TRANS,

    ID_BTN_RESET_PRESET,
    ID_SLD_DENOISE,
    ID_EDT_DENOISE,
    ID_SLD_PSF_ITERS,
    ID_EDT_PSF_ITERS,
    ID_SLD_CHROMA,
    ID_EDT_CHROMA,
    ID_SLD_PSF_SIGMA,
    ID_EDT_PSF_SIGMA,
    ID_SLD_DEGHOST,
    ID_EDT_DEGHOST,
    ID_SLD_EV_OFFSET,
    ID_EDT_EV_OFFSET,

    ID_CHK_SILICON_CLEAN,
    ID_CHK_IBP_ON,
    ID_CHK_AUTO_LCA,
    ID_CHK_AUTO_EXP,
    ID_CHK_ROLLOFF,
    ID_CHK_DITHER,

    ID_CMB_BACKEND,
    ID_EDT_GROUP_SIZE,
    ID_EDT_THREADS,
    ID_BTN_ENGINE_MENU,
    ID_LBL_ENGINE_STATUS,

    // Tab 2: 2 时空与创意
    ID_RAD_T2_MOT_STD = 310,
    ID_SEG_T2_ANCHOR,
    ID_RAD_T2_MOT_CLEAN,
    ID_SLD_CLEAN_SENS,
    ID_EDT_CLEAN_SENS,
    ID_RAD_T2_MOT_VND,
    ID_SLD_VND_THRESH,
    ID_EDT_VND_THRESH,
    ID_RAD_T2_MOT_STROBE,
    ID_EDT_STROBE_STEP,
    ID_SEG_STROBE_FADE,

    ID_CHK_TRANSIENT_TAB2,
    ID_SLD_TRANS_SIGMA,
    ID_EDT_TRANS_SIGMA,
    ID_SEG_TRANS_COLOR,

    ID_CHK_REFL_ON,
    ID_SEG_REFL_MODE,
    ID_LBL_REFL_DRIFT,

    ID_VIEW_PARALLAX_METER,
    ID_CHK_EXPORT_PARALLAX,
    ID_SLD_PARALLAX_AMP,
    ID_EDT_PARALLAX_AMP,
    ID_CHK_EXPORT_REFOCUS,
    ID_SLD_REFOCUS_POS,
    ID_EDT_REFOCUS_POS,
    ID_SLD_BOKEH_STR,
    ID_EDT_BOKEH_STR,

    // Tab 3: 3 物理场与数据
    ID_BTN_FIELDS_BASIC3 = 360,
    ID_BTN_FIELDS_ALL,
    ID_SEG_T3_CARRIER,
    ID_CHK_FIELD_BASE = 370, // 370..380 (11 fields)
    ID_CHK_EXPORT_TILES = 390,
    ID_SEG_TILE_SIZE,
    ID_SLD_TILE_CONF,
    ID_EDT_TILE_CONF,

    // Tab 4: 4 镜头与机身
    ID_VIEW_OPTICS_CANVAS = 410,
    ID_BTN_RUN_OPTICS,
    ID_LBL_OPTICS_STATUS,
    ID_LBL_SILICON_STATS,
    ID_CHK_LCP_LCA,
    ID_CHK_LCP_VIG,
    ID_CHK_LCP_DIST,
    ID_BTN_EXPORT_LCP,
    ID_BTN_EXPORT_PNG_CARD,
    ID_BTN_EXPORT_JSON_TEL,

    // Bottom Global Action Bar & Header Performance Pill + Co-Brand About Button
    ID_BTN_RUN = 450,
    ID_BTN_EXPORT_LOG,
    ID_BTN_OPEN_OUT,
    ID_BTN_PERF_HEADER = 455,
    ID_BTN_BRAND_HEADER = 456,

    // V3 Controls (Tab 1 / Tab 2 / Tab 3)
    ID_CMB_MOTION_PROFILE = 470,
    ID_CMB_RGB1X_TIER,
    ID_CMB_EXR_PREC,
    ID_CMB_HIGHLIGHT_MODE,
    ID_CMB_STREAMING,
    ID_EDT_MEM_BUDGET,
    ID_CMB_SHUTTER_MODE,
    ID_EDT_FLOW_KAPPA,
    ID_CMB_INTRA_DEBLUR,
    ID_CMB_DIFFUSION_MODE,
    ID_EDT_DIFF_STRENGTH,
    ID_CMB_UC_GRADE,
    ID_CMB_UC_BAFFLE,
    ID_CMB_UC_CHROMA,
    ID_CMB_APERTURE_WEIGHT,
    ID_CMB_METRIC_POLICY,
    ID_CMB_OBSERVERS,
    ID_CHK_NEXT_SHOT_PLAN,
    ID_CMB_CPU_CORES,
    ID_CMB_PRIORITY,
    ID_CMB_EXR_PARTS,
    ID_CMB_CODEC_CONSTRAINT,
    ID_CMB_SAT_MODEL,

    // V3.1 Handheld Optics, 3-Disc STF, O1-O10 Observers & NOPB Sidecar Bus Controls (600..660)
    ID_CMB_SPECTRAL_CA = 600,
    ID_SLD_SPECTRAL_PWR,
    ID_EDT_SPECTRAL_PWR,
    ID_CMB_SPECTRAL_ANG,
    ID_SLD_DIFF_STRENGTH,
    ID_BTN_RESET_OPTICS,
    ID_SLD_BOKEH_DENISEN,
    ID_EDT_BOKEH_DENISEN,
    ID_SLD_BOKEH_SWIRL,
    ID_EDT_BOKEH_SWIRL,
    ID_SLD_BOKEH_ASPECT,
    ID_EDT_BOKEH_ASPECT,
    ID_SLD_BOKEH_ONION,
    ID_EDT_BOKEH_ONION,
    ID_SLD_BOKEH_STF,
    ID_EDT_BOKEH_STF,
    ID_EDT_BUS_IN,
    ID_BTN_BUS_BROWSE,
    ID_BTN_BUS_CLEAR,
    ID_CHK_BUS_EXPORT,
    ID_BTN_OBS_ALL = 635,
    ID_BTN_OBS_NONE,
    ID_CHK_WRITE_OBS_REP,
    ID_CHK_OBS_BASE = 640, // 640..649 (O1..O10)

    // Unified Performance & Engine Popup Menu Commands
    ID_MENU_CLI_BROWSE = 500,
    ID_MENU_CLI_AUTO,
    ID_MENU_LIBRAW_BROWSE,
    ID_MENU_LIBRAW_RESET,
    ID_MENU_BACKEND_AUTO = 505,
    ID_MENU_BACKEND_GPU,
    ID_MENU_BACKEND_CPU,
    ID_MENU_CORES_AUTO = 508,
    ID_MENU_CORES_PERF,
    ID_MENU_CORES_PHYS,
    ID_MENU_CORES_EFF,
    ID_MENU_CORES_ALL,
    ID_MENU_PRIO_NORMAL = 513,
    ID_MENU_PRIO_HIGH,
    ID_MENU_PRIO_BELOW,
    ID_MENU_PRIO_IDLE,
    ID_MENU_BATTERY_DEVICE = 517,
    ID_MENU_EXPORT_LOG = 518,
    ID_MENU_LANG_LOAD_FILE = 519,

    // Queue & Frame Context Menu Commands
    ID_QMENU_TOGGLE_CHK = 520,
    ID_QMENU_ONLY_BEST,
    ID_QMENU_CHECK_VALID,
    ID_QMENU_SELECT_ALL,
    ID_QMENU_MERGE,
    ID_QMENU_CLEAN_STRAY,
    ID_QMENU_REMOVE,

    // Streaming & Stripe Memory Budget + Welcome/About Menu Commands (528..539)
    ID_MENU_STREAM_AUTO = 528,
    ID_MENU_STREAM_ON,
    ID_MENU_STREAM_OFF,
    ID_MENU_MEM_2G = 532,
    ID_MENU_MEM_4G,
    ID_MENU_MEM_8G,
    ID_MENU_MEM_16G,
    ID_MENU_WELCOME_SETUP = 536,
    ID_MENU_ABOUT = 537,

    ID_FMENU_TOGGLE_INC = 540,
    ID_FMENU_KEEP_ONLY,
    ID_FMENU_RESTORE_ALL,
    ID_FMENU_LOCK_ANC,
    ID_FMENU_SPLIT,

    // Dynamic .lng Language Menu Commands (560..599)
    ID_MENU_LANG_BASE = 560,

    // First-Launch & About Modal Controls (701..715)
    ID_WDLG_LANG = 701,
    ID_WDLG_BACKEND,
    ID_WDLG_STREAM,
    ID_WDLG_MEM,
    ID_WDLG_CORES,
    ID_WDLG_BATTERY,
    ID_WDLG_PATHS,
    ID_WDLG_CONFIRM,
    ID_WDLG_REOPEN,
    ID_WDLG_CLOSE,
    ID_WDLG_CREDITS,
    ID_WDLG_BRAND,

    // Full Engine Parameter Coverage Controls (800..875)
    ID_BTN_INTAKE_PARAMS = 800,
    ID_RAD_T2_MOT_FSTACK,
    ID_SLD_RGB_PSF_ITERS,
    ID_EDT_RGB_PSF_ITERS,
    ID_CMB_IBP_ITERS,
    ID_CHK_EDGE_POLISH,
    ID_EDT_SPECTRAL_DEG,
    ID_CMB_W2_DEPTH_GATE,
    ID_CMB_REFOCUS_MODE,
    ID_CHK_REFOCUS_AUTO,
    ID_SLD_BOKEH_SA,
    ID_EDT_BOKEH_SA,
    ID_EDT_VIRTUAL_FSTOP,
    ID_CMB_BOKEH_BLADES,
    ID_EDT_BOKEH_ANGLE,
    ID_EDT_TILT_X,
    ID_EDT_TILT_Y,
    ID_CMB_SEE_THROUGH,
    ID_BTN_FIELDS_NONE,
    ID_CMB_DEPTH_GATE,
    ID_EDT_DEPTH_DISP,
    ID_EDT_DEPTH_COH,
    ID_EDT_DEPTH_SUP,
    ID_SEG_FIELD_BANK,
    ID_CMB_OBS_GATE,
    ID_EDT_READOUT_MS,
    ID_EDT_OBS_PITCH,
    ID_EDT_PLATE_SCALE,
    ID_CHK_EMIT_DIFFUSE,
    ID_CHK_EMIT_LSPLIT,
    ID_CHK_EVAL_SELF,
    ID_CHK_EXT_FIELD_BASE = 840, // 840..855 (16 extended fields)

    // Intake & Decoder Tuning Modal Controls (860..870)
    ID_IDLG_DECODER = 860,
    ID_IDLG_NEV,
    ID_IDLG_GAP,
    ID_IDLG_SHARP,
    ID_IDLG_SHIFT,
    ID_IDLG_LUCKY,
    ID_IDLG_CLUSTER,
    ID_IDLG_RESET,
    ID_IDLG_CANCEL,
    ID_IDLG_APPLY
};

constexpr UINT WM_APP_STATUS       = WM_APP + 1;
constexpr UINT WM_APP_PROGRESS     = WM_APP + 2;
constexpr UINT WM_APP_DONE         = WM_APP + 3;
constexpr UINT WM_APP_PLAN_READY   = WM_APP + 4;
constexpr UINT WM_APP_OPTICS_READY = WM_APP + 5;

static const wchar_t* kRegSubKey = L"Software\\NijiN\\nijinLBS";

// Muted Lake / Water / Slate Blue Accents + Pure-White Passive Studio Surfaces
static constexpr COLORREF kColCanvas       = RGB(235, 240, 247);
static constexpr COLORREF kColCard         = RGB(255, 255, 255);
static constexpr COLORREF kColCardSub      = RGB(246, 249, 253);
static constexpr COLORREF kColCardBorder   = RGB(196, 209, 226);
static constexpr COLORREF kColSubBorder    = RGB(214, 224, 237);
static constexpr COLORREF kColInputEdge    = RGB(180, 196, 218);
static constexpr COLORREF kColDisabledFill = RGB(243, 246, 251);
static constexpr COLORREF kColAccent       = RGB(78, 118, 168);
static constexpr COLORREF kColAccentDark   = RGB(60, 95, 140);
static constexpr COLORREF kColAccentSoft   = RGB(226, 236, 248);
static constexpr COLORREF kColStopBtn      = RGB(178, 82, 76);
static constexpr COLORREF kColStopBtnDark  = RGB(148, 62, 57);
static constexpr COLORREF kColBadgeFill    = RGB(222, 233, 247);
static constexpr COLORREF kColBadgeEdge    = RGB(174, 194, 220);
static constexpr COLORREF kColBadgeText    = RGB(58, 92, 136);
static constexpr COLORREF kColTextMain     = RGB(32, 44, 62);
static constexpr COLORREF kColTextMuted    = RGB(98, 116, 140);
static constexpr COLORREF kColWarnAmber    = RGB(186, 108, 28);
static constexpr COLORREF kColWarnCoral    = RGB(182, 72, 66);
static constexpr COLORREF kColOkGreen      = RGB(56, 130, 108);

struct FieldSpec {
    const wchar_t* code;
    const char* lang_key;
    const wchar_t* label;
    int box_idx; // 0=TL, 1=TR, 2=BL, 3=BR
    bool gated;
};

static constexpr int kNumFields = 11;
static const FieldSpec kFieldSpecs[kNumFields] = {
    {L"motion",     "t3.fld_motion",     L"运动 / 星野天空蒙版 (Motion)",     0, false},
    {L"phase",      "t3.fld_phase",      L"亚像素超分相位置信度 (Phase)",     0, false},
    {L"focus",      "t3.fld_focus",      L"二阶拉普拉斯对焦场 (Focus)",       0, false},
    {L"depth",      "t3.fld_depth",      L"极线视差逆深度场 (L1 Depth)*",     1, true },
    {L"defocus",    "t3.fld_defocus",    L"近摄散焦符号场 (L2 Defocus)*",     1, true },
    {L"specular",   "t3.fld_specular",   L"镜面闪烁碎高光场 (L4 Specular)",   1, false},
    {L"sigma",      "t3.fld_sigma",      L"逐像素 1σ 绝对误差棒 (L10c)",      2, false},
    {L"coherence",  "t3.fld_coherence",  L"摩尔纹与微纹理相干场 (L6)",        2, false},
    {L"velocity",   "t3.fld_velocity",   L"动体速度矢量幅值场 (L7)",          2, false},
    {L"flicker",    "t3.fld_flicker",    L"人造频闪光源分割场 (L5)*",         3, true },
    {L"turbulence", "t3.fld_turbulence", L"大气热浪纹影湍流场 (L8 BOS)*",     3, true }
};

static constexpr int kNumExtFields = 16;
static const FieldSpec kExtFieldSpecs[kNumExtFields] = {
    {L"depth_conf",        "t3.efld_dconf",  L"深度置信度 (DepthConf)*",   1, true },
    {L"dff_depth",         "t3.efld_dff",    L"散焦测距 (DfF Depth)*",     1, true },
    {L"normal_x",          "t3.efld_nx",     L"相对法线 X (Normal X)",     1, false},
    {L"normal_y",          "t3.efld_ny",     L"相对法线 Y (Normal Y)",     1, false},
    {L"occlusion",         "t3.efld_occ",    L"几何遮挡场 (Occlusion)",    1, false},
    {L"roughness",         "t3.efld_rough",  L"O1 微面粗糙度 (Rough)",     1, false},
    {L"veil_transmission", "t3.efld_veil",   L"O1 介质透射 (Veil)",        1, false},
    {L"metalness",         "t3.efld_metal",  L"O1 金属度 (Metalness)",     1, false},
    {L"ac_fraction",       "t3.efld_ac",     L"O2 交流光占比 (AC)",        2, false},
    {L"phosphor_decay",    "t3.efld_phos",   L"O2 荧光余辉 (Phosphor)",    2, false},
    {L"ttc",               "t3.efld_ttc",    L"O3 碰撞时间 (TTC)",         2, false},
    {L"vorticity",         "t3.efld_vort",   L"O3 流体涡度 (Vorticity)",   2, false},
    {L"strain",            "t3.efld_strain", L"O3 非刚性应变 (Strain)",    2, false},
    {L"schlieren_opd",     "t3.efld_opd",    L"O4 纹影光程差 (OPD)",       3, false},
    {L"vibration_amp",     "t3.efld_vib",    L"O5 微震幅值 (Vib Amp)",     3, false},
    {L"vital_perfusion",   "t3.efld_vital",  L"O5 灌注脉搏 (Perfusion)",   3, false}
};

static constexpr int kNumRgbFmts = 7;
static constexpr int kNumFieldModes = 6;
static const wchar_t* const kPresetCodes[4]    = {L"photo", L"astro", L"sci", L"arc"};
static const wchar_t* const kOlpfCodes[4]      = {L"auto", L"none", L"weak4", L"gauss16"};
static const wchar_t* const kRawFmtCodes[5]    = {L"dng", L"dng-linear", L"dng-fp16", L"dng-hq", L"dng-raw"};
static const wchar_t* const kRgbFmtCodes[kNumRgbFmts] = {L"tiff-deflate", L"tiff-hq", L"png", L"jxs", L"jxs-lossless", L"tiff-raw", L"exr"};
static const wchar_t* const kRgbScaleCodes[3]  = {L"2x", L"1x", L"both"};
static const wchar_t* const kFieldModeCodes[kNumFieldModes] = {L"tiff-alpha", L"split", L"rgb", L"motion", L"phase", L"focus"};
static const wchar_t* const kBackendCodes[3]   = {L"auto", L"gpu", L"cpu"};
static const wchar_t* const kCpuCoresCodes[5]  = {L"auto", L"perf", L"physical", L"eff", L"all"};
static const wchar_t* const kPriorityCodes[4]  = {L"normal", L"high", L"below", L"idle"};

static const wchar_t* const kMotionProfileCodes[5] = {L"auto", L"standard", L"concert", L"sports", L"dxo-superraw"};
static const wchar_t* const kRgb1xTierCodes[3]     = {L"sinc", L"fast", L"joint"};
static const wchar_t* const kExrPrecCodes[2]         = {L"half", L"float"};
static const wchar_t* const kExrPartsCodes[3]        = {L"beauty,fg,bg,scatter,depth", L"beauty,scatter", L"beauty"};
static const wchar_t* const kCodecConstraintCodes[3] = {L"off", L"box", L"probit"};
static const wchar_t* const kSatModelCodes[2]        = {L"hard", L"probit"};
static const wchar_t* const kHighlightModeCodes[3] = {L"legacy", L"oklab-stage", L"hard-clip"};
static const wchar_t* const kStreamingCodes[3]     = {L"auto", L"on", L"off"};
static const wchar_t* const kShutterModeCodes[4]   = {L"anchor", L"integrate", L"nnls-custom", L"deblur"};
static const wchar_t* const kIntraDeblurCodes[3]   = {L"auto", L"on", L"off"};
static const wchar_t* const kDiffusionCodes[8]     = {L"off", L"hdf", L"black-mist", L"ultra-contrast", L"halation", L"uc+hdf", L"uc+mist", L"uc+halation"};
static const wchar_t* const kUcGradeCodes[8]       = {L"1/8", L"1/4", L"1/2", L"1", L"2", L"3", L"4", L"5"};
static const wchar_t* const kUcBaffleCodes[3]      = {L"standard", L"hooded", L"wide-flare"};
static const wchar_t* const kUcChromaCodes[2]      = {L"neutral", L"warm-glass"};
static const wchar_t* const kApertureWeightCodes[2]= {L"voronoi", L"dwell"};
static const wchar_t* const kMetricPolicyCodes[2]  = {L"verified", L"allow-proxy"};
static const wchar_t* const kSpectralCaCodes[3]    = {L"off", L"lateral", L"axial-aura"};
static const wchar_t* const kSpectralAngleCodes[5] = {L"radial", L"0", L"45", L"90", L"custom"};
static const wchar_t* const kDecoderCodes[3]       = {L"auto", L"wvdc", L"libraw"};
static const wchar_t* const kLuckyCodes[3]         = {L"auto", L"on", L"off"};
static const wchar_t* const kIbpIterCodes[3]       = {L"0", L"1", L"2"};
static const wchar_t* const kRefocusModeCodes[4]   = {L"auto", L"photon-bokeh", L"synthetic-aperture", L"shift-deconv"};
static const wchar_t* const kBokehBladeCodes[7]    = {L"0", L"5", L"6", L"7", L"8", L"9", L"11"};
static const wchar_t* const kSeeThroughCodes[3]    = {L"auto", L"on", L"off"};
static const wchar_t* const kDepthGateCodes[3]     = {L"auto", L"relaxed", L"force"};
static const wchar_t* const kObsGateCodes[3]       = {L"auto", L"relaxed", L"force"};

struct ObsSpecWin {
    const wchar_t* code;
    const char* lang_key;
    const wchar_t* label;
};
static constexpr int kNumObs = 10;
static const ObsSpecWin kObsSpecs[kNumObs] = {
    {L"o1",  "obs.o1",  L"O1 漫反射/镜面辐射"},
    {L"o2",  "obs.o2",  L"O2 频闪锁相与余辉"},
    {L"o3",  "obs.o3",  L"O3 刚体几何与涡度"},
    {L"o4",  "obs.o4",  L"O4 热浪纹影 (BOS)"},
    {L"o5",  "obs.o5",  L"O5 滚快微震测振"},
    {L"o6",  "obs.o6",  L"O6 轴向色差定号"},
    {L"o7",  "obs.o7",  L"O7 图像波速代理"},
    {L"o8",  "obs.o8",  L"O8 差分倾斜视宁度"},
    {L"o9",  "obs.o9",  L"O9 相对流位与底色"},
    {L"o10", "obs.o10", L"O10 暗空星迹测速"}
};

struct FrameDiagItem {
    int idx_1based = 1;
    int assigned_group = 0;
    std::wstring file;
    std::wstring full_path;
    std::wstring flags = L"OK";
    double acutance = 0.0;
    double shift_px = 0.0;
    double dx = 0.0;
    double dy = 0.0;
    double time_seconds = 0.0;
    bool has_time = false;
    double zncc = 0.99;
    bool is_crown = false;
    bool is_shock = false;
    bool included = true;
};

struct BurstGroupItem {
    int cluster_idx = 0;
    bool checked = true;
    bool is_stray_archive = false;
    int valid_frame_count = 0;
    std::wstring title;
    std::wstring span_label;
    std::wstring sensor_badge = L"Bayer";
    std::wstring meta_line;
    std::string camera_model;
    std::string scene_code;
    std::string rec_preset = "photo";
    std::string rec_anchor = "auto";
    int best_anchor_1based = 1;
    int locked_anchor_1based = 0; // 0 = follow global motion/anchor mode; >=1 = locked frame
    int quad_coverage = 0;
    int sr_score = 0;
    int star_count = 0;
    double motion_pct = 0.0;
    double ev_spread = 0.0;
    double max_drift_px = 0.0;
    double max_parallax_2x_px = 0.0;
    double focus_distance_m = 0.0;
    bool focus_is_infinity = false;
    bool has_telemetry = false;
    bool parallax_measured = false, depth_known = false, depth_observable = false;
    std::map<std::string, bool> field_observability;
    double rec_denoise = 1.0;
    double rec_deghost = 1.0;
    int rec_psf_iters = 4;
    bool shutter_shock = false;
    std::wstring shutter_summary = L"待实测防抖与快门状态";
    int raw_width = 0;
    int raw_height = 0;
    std::wstring bus_in_path;
    std::vector<std::wstring> paths;
    std::vector<FrameDiagItem> frames;
    std::vector<int> phase_quadrants;
};

struct OpticsTelemetry {
    bool measured = false;
    bool parallax_measured = false, depth_known = false, depth_observable = false;
    double max_parallax_2x_px = 0;
    std::map<std::string, bool> field_observability;
    std::wstring grade_badge;
    bool is_star_gold = false;
    double zone_mtf50[5] = {0.0, 0.0, 0.0, 0.0, 0.0}; // C, NW, NE, SW, SE
    double zone_astig[5] = {0.0, 0.0, 0.0, 0.0, 0.0};
    bool zone_rejected[5] = {false, false, false, false, false};
    double lca_rad_px = 0.0;
    double max_drift_px = 0.0;
    double sigma0 = -1.0;
    double gain_g = -1.0;
    double temp_c = NAN;
    int warm_pixels = -1;
    bool has_silicon = false;
    std::wstring ois_note;
    std::string raw_json;
    std::wstring json_file_path;
    std::wstring passport_path;
    std::wstring zone_status[5];
    bool lca_valid[2] = {false, false};
    double lca_edge_px[2] = {0, 0};
    std::vector<FrameDiagItem> drift;
    std::vector<int> phase_quadrants;

    OpticsTelemetry() {
        grade_badge = heraw_i18n::tr_w("t4.badge_pending");
        ois_note = heraw_i18n::tr_w("t4.ois_idle");
    }
};

struct TaggedCtrl {
    HWND hw = nullptr;
    int tab = -1;     // -1 = persistent, 0..2 = Workspace 1..3, -99 = hidden backing
    int col_mode = 0; // 0 = left fixed, 1 = center elastic, 2 = right dock anchored
    RECT design{};
    const char* lang_key = nullptr;
};

static std::wstring silicon_readout(const OpticsTelemetry& optics) {
    char sigma[32] = "--", gain[32] = "--", temperature[32] = "--", warm[32] = "--";
    if (optics.sigma0 >= 0) std::snprintf(sigma, sizeof(sigma), "%.5f", optics.sigma0);
    if (optics.gain_g >= 0) std::snprintf(gain, sizeof(gain), "%.5f", optics.gain_g);
    if (std::isfinite(optics.temp_c)) std::snprintf(temperature, sizeof(temperature), "%.1f °C", optics.temp_c);
    if (optics.warm_pixels >= 0) std::snprintf(warm, sizeof(warm), "%d", optics.warm_pixels);
    return heraw_i18n::trf_w("t4.silicon_fmt", sigma, gain, temperature, warm);
}

struct BrandCroppedBmp {
    Gdiplus::Bitmap* bmp = nullptr;
    int sx = 0, sy = 0, sw = 0, sh = 0;
};

struct AppState {
    int dpi = 96;
    HWND hwnd = nullptr;

    // Persistent Left Dock
    HWND hChkSortTime = nullptr, hChkRecursive = nullptr;
    HWND hCmbGrouping = nullptr, hBtnRegroup = nullptr, hBtnIntakeParams = nullptr, hLblGroupingHint = nullptr, hBtnIntakeAction = nullptr;
    bool grouping_dirty = false;
    std::wstring decoder_mode       = L"auto";
    std::wstring nev_frames_spec    = L"all";
    std::wstring burst_gap_s        = L"3.5";
    std::wstring min_rel_sharpness  = L"0.72";
    std::wstring max_shift_px       = L"120";
    std::wstring lucky_mode         = L"auto";
    std::wstring max_cluster_frames = L"128";
    HWND hBurstView = nullptr, hFrameView = nullptr;
    HWND hBtnAddFiles = nullptr, hBtnAddFolder = nullptr, hBtnMergeGroups = nullptr, hBtnCleanStray = nullptr;
    HWND hBtnExportTopK = nullptr, hBtnClear = nullptr;
    HWND hLblFrameHdr = nullptr, hBtnSplitFrames = nullptr, hBtnLockFrame = nullptr;
    int burst_scroll_y = 0, burst_hover_idx = -1, burst_hover_part = 0; // part: 1=chk, 2=rx_pill, 0=row
    int selected_group = 0, group_anchor = 0;
    std::vector<uint8_t> group_sel;
    int frame_scroll_y = 0, frame_hover_idx = -1, frame_sel_idx = 0, frame_anchor = 0;
    std::vector<uint8_t> frame_sel;
    int context_frame_idx = -1;

    // Center 3-Workspace Stage
    HWND hTabBar = nullptr;
    int active_tab = 0; // 0..2

    // Persistent Right Dock Controls (R1 / R2 / R3)
    HWND hLblR1GroupSummary = nullptr, hLblR1GateSr = nullptr, hLblR1GatePar = nullptr;
    int r1_sr_status = 0, r1_par_status = 0;
    HWND hLblBusPreflight = nullptr;
    int bus_preflight_status = 0;
    std::wstring default_bus_in_path;
    HWND hLblBppCap = nullptr;
    HWND hLblLiveReceipt = nullptr;

    // Workspace 1 controls
    HWND hBtnPortalOptics = nullptr;
    HWND hSegPreset = nullptr;
    HWND hCmbOlpf = nullptr;

    HWND hOutDir = nullptr, hBtnOutBrowse = nullptr, hBtnOutClear = nullptr;
    HWND hChkRaw = nullptr, hCmbRawFmt = nullptr, hChkRelayRaw = nullptr, hChkNoToolExif = nullptr;
    HWND hChkRgb = nullptr, hSegRgbScale = nullptr, hCmbRgbFmt = nullptr, hEdtJxsBpp = nullptr;
    HWND hRadCsSrgb = nullptr, hRadCsAdobe = nullptr;
    HWND hChkField = nullptr, hCmbFieldMode = nullptr;

    HWND hBtnResetPreset = nullptr;
    HWND hSldDenoise = nullptr, hEdtDenoise = nullptr;
    HWND hSldPsfIters = nullptr, hEdtPsfIters = nullptr;
    HWND hSldChroma = nullptr, hEdtChroma = nullptr;
    HWND hSldPsfSigma = nullptr, hEdtPsfSigma = nullptr;
    HWND hSldDeghost = nullptr, hEdtDeghost = nullptr;
    HWND hSldEv = nullptr, hEdtEv = nullptr;
    HWND hSldRgbPsfIters = nullptr, hEdtRgbPsfIters = nullptr;
    HWND hCmbIbpIters = nullptr, hChkEdgePolish = nullptr;

    HWND hChkSiliconClean = nullptr, hChkIbpOn = nullptr, hChkAutoLca = nullptr;
    HWND hChkAutoExp = nullptr, hChkRolloff = nullptr, hChkDither = nullptr;
    HWND hCmbBackend = nullptr, hCmbCpuCores = nullptr, hCmbPriority = nullptr;
    HWND hEdtGrpSz = nullptr, hEdtThreads = nullptr;
    HWND hBtnPerfHeader = nullptr, hBtnBrandHeader = nullptr, hBtnEngineMenu = nullptr, hLblEngineStatus = nullptr;

    // Temporal & Split Layer controls (Workspace 1 Card 2)
    HWND hRadT2MotStd = nullptr, hSegT2Anchor = nullptr;
    HWND hRadT2MotClean = nullptr, hSldCleanSens = nullptr, hEdtCleanSens = nullptr;
    HWND hRadT2MotVnd = nullptr, hSldVndThresh = nullptr, hEdtVndThresh = nullptr;
    HWND hRadT2MotStrobe = nullptr, hEdtStrobeStep = nullptr, hSegStrobeFade = nullptr;
    HWND hRadT2MotFstack = nullptr;
    HWND hChkTransientTab2 = nullptr, hSldTransSigma = nullptr, hEdtTransSigma = nullptr, hSegTransColor = nullptr;
    HWND hChkReflOn = nullptr, hSegReflMode = nullptr, hLblReflDrift = nullptr;

    // Workspace 2 controls (Optics, Bokeh Shaping & Light-Field Refocus)
    HWND hBtnResetOptics = nullptr, hLblBokehHint = nullptr;
    HWND hSldDiffStrength = nullptr;
    HWND hSldSpectralPwr = nullptr, hEdtSpectralDeg = nullptr;
    HWND hSldBokehDeNisen = nullptr, hSldBokehSwirl = nullptr, hSldBokehAspect = nullptr, hEdtBokehAspect = nullptr, hSldBokehOnion = nullptr;
    HWND hCmbW2DepthGate = nullptr;
    HWND hParallaxMeter = nullptr;
    HWND hChkExportParallax = nullptr, hSldParallaxAmp = nullptr, hEdtParallaxAmp = nullptr;
    HWND hChkExportRefocus = nullptr, hCmbRefocusMode = nullptr, hChkRefocusAuto = nullptr;
    HWND hSldRefocusPos = nullptr, hEdtRefocusPos = nullptr;
    HWND hSldBokehStr = nullptr, hEdtBokehStr = nullptr;
    HWND hSldBokehStf = nullptr, hEdtBokehStf = nullptr;
    HWND hSldBokehSa = nullptr, hEdtBokehSa = nullptr;
    HWND hEdtVirtualFstop = nullptr, hCmbBokehBlades = nullptr, hEdtBokehAngle = nullptr;
    HWND hEdtTiltX = nullptr, hEdtTiltY = nullptr, hCmbSeeThrough = nullptr;

    // Workspace 3 controls (Twin Radars, 27 Fields, O1-O10 Observers & Dataset Tiles)
    HWND hBtnFieldsBasic3 = nullptr, hBtnFieldsAll = nullptr, hBtnFieldsNone = nullptr;
    HWND hCmbDepthGate = nullptr, hEdtDepthDisp = nullptr, hEdtDepthCoh = nullptr, hEdtDepthSup = nullptr;
    HWND hSegFieldBank = nullptr;
    HWND hChkFields[kNumFields] = {};
    HWND hChkExtFields[kNumExtFields] = {};
    HWND hLblFieldsGateNote = nullptr;
    HWND hBtnObsAll = nullptr, hBtnObsNone = nullptr;
    HWND hChkObs[kNumObs] = {};
    HWND hCmbObsGate = nullptr, hEdtReadoutMs = nullptr, hEdtObsPitch = nullptr, hEdtPlateScale = nullptr;
    HWND hChkWriteObsReport = nullptr, hChkNextShotPlan = nullptr;
    HWND hChkEmitDiffuse = nullptr, hChkEmitLightSplit = nullptr, hChkEvalSelf = nullptr;
    HWND hChkExportTiles = nullptr, hSegTileSize = nullptr, hSldTileConf = nullptr, hEdtTileConf = nullptr;

    // V3 extra controls
    HWND hCmbMotionProfile = nullptr, hCmbRgb1xTier = nullptr, hCmbExrPrec = nullptr;
    HWND hCmbExrParts = nullptr, hCmbCodecConstraint = nullptr, hCmbSatModel = nullptr;
    HWND hCmbHighlightMode = nullptr, hCmbStreaming = nullptr, hEdtMemBudget = nullptr;
    HWND hCmbShutterMode = nullptr, hEdtFlowKappa = nullptr, hCmbIntraDeblur = nullptr;
    HWND hCmbDiffusionMode = nullptr, hEdtDiffStrength = nullptr;
    HWND hCmbUcGrade = nullptr, hCmbUcBaffle = nullptr, hCmbUcChroma = nullptr;
    HWND hCmbApertureWeight = nullptr, hCmbMetricPolicy = nullptr;
    HWND hCmbSpectralCa = nullptr, hEdtSpectralPwr = nullptr, hCmbSpectralAng = nullptr;
    HWND hEdtBokehDeNisen = nullptr, hEdtBokehSwirl = nullptr, hEdtBokehOnion = nullptr;
    HWND hEdtBusIn = nullptr, hBtnBusBrowse = nullptr, hBtnBusClear = nullptr, hChkBusExport = nullptr;

    // Tab 4 controls
    HWND hOpticsCanvas = nullptr;
    HWND hBtnRunOptics = nullptr, hLblOpticsStatus = nullptr;
    HWND hLblSiliconStats = nullptr;
    HWND hChkLcpLca = nullptr, hChkLcpVig = nullptr, hChkLcpDist = nullptr;
    HWND hBtnExportLcp = nullptr, hBtnExportPngCard = nullptr, hBtnExportJsonTel = nullptr;

    // Bottom Global Action Bar
    HWND hBtnRun = nullptr, hBtnExportLog = nullptr, hBtnOpenOut = nullptr;
    int prog_pct = 0;
    std::wstring status_line = L"就绪 · 拖入 RAW 连拍或目录即可静默分簇与审片";

    // Unified logical state (single source of truth across tabs)
    int preset_idx = 0;      // 0=photo, 1=astro, 2=sci, 3=arc
    int motion_mode_idx = 0; // 0=auto, 1=first, 2=middle, 3=clean-plate, 4=virtual-nd, 5=strobe
    int last_min_req = 5;
    std::wstring cli_path;
    std::wstring libraw_path;
    bool is_battery_device = false;
    heraw_cache::HostGpuPolicy gpu_policy{};
    bool perf_breath_active = false;
    ULONGLONG perf_breath_start_ms = 0;
    float perf_breath_phase = 0.0f;
    bool is_screenshot_mode = false;
    bool allow_unconstrained_size = false;
    bool suppress_sync = false;
    bool loading_prefs = true;

    std::vector<TaggedCtrl> tab_ctrls;
    std::vector<std::wstring> inputs;
    std::vector<BurstGroupItem> groups;
    OpticsTelemetry optics;
    std::wstring optics_export_path;
    std::wstring optics_png_export_path;
    std::wstring lcp_export_dst;

    // Async state
    std::atomic<bool> running{false};
    std::atomic<bool> stopping{false};
    std::atomic<bool> cancel_job{false};
    std::atomic<bool> plan_scanning{false};
    std::atomic<uint64_t> plan_gen{0};
    std::mutex proc_mtx;
    HANDLE hChildProc = nullptr;
    HANDLE hPlanProc = nullptr;
    HANDLE hOpticsProc = nullptr;
    std::string last_cli_output;
    std::thread worker;
    std::thread plan_worker;
    std::thread optics_worker;
    std::vector<std::vector<std::wstring>> pending_jobs;
    std::vector<int> pending_group_indices;
    int active_job_group_idx = -1;
    int total_synthesis_jobs = 0;
    int completed_synthesis_jobs = 0;
    int skipped_synthesis_jobs = 0;
    FILETIME job_start_ft{};
    std::wstring active_out_dir;
    std::wstring auto_out_dir;
    std::wstring lcp_tmp_prefix;

    // GDI / GDI+ resources
    HFONT hFont = nullptr, hFontSmall = nullptr, hFontBold = nullptr, hFontCardHdr = nullptr;
    HFONT hFontTitle = nullptr, hFontMajorVer = nullptr, hFontVerTag = nullptr;
    HFONT hFontLink = nullptr, hFontLinkHdr = nullptr;
    HFONT hFontBadge = nullptr, hFontMono = nullptr;
    HBRUSH hBrCanvas = nullptr, hBrCard = nullptr, hBrCardSub = nullptr;
    HBRUSH hBrWhite = nullptr, hBrDisabledEdit = nullptr;
    HBITMAP hBmpGxFallback = nullptr, hBmpNijinFallback = nullptr;
    BrandCroppedBmp brandGx{}, brandNijin{};

    int S(int px) const { return MulDiv(px, dpi, 96); }
};

static AppState g_app;

// Forward declarations
static void sync_cross_tab_ui();
static void save_ui_prefs();
static void trigger_async_plan_scan();
static void apply_preset_to_controls(int idx);
static std::wstring selected_rgb_fmt_code();
static std::wstring selected_rgb_scale_code();
static int min_valid_frames_win();
static void apply_ui_language_win();
static void export_session_log_dialog(HWND owner);
static void show_welcome_or_about_win(HWND owner, bool is_first_launch);

static inline Gdiplus::Color gdip_col(COLORREF c, BYTE alpha = 255) {
    return Gdiplus::Color(alpha, GetRValue(c), GetGValue(c), GetBValue(c));
}

static void gdip_round_rect(Gdiplus::Graphics& g, float x, float y, float w, float h, float r,
                            Gdiplus::Color fill, Gdiplus::Color stroke, float stroke_w = 1.0f,
                            bool dashed = false) {
    if (w <= 0.0f || h <= 0.0f) return;
    r = std::min(r, 0.5f * std::min(w, h));
    float d = 2.0f * r;
    Gdiplus::GraphicsPath path;
    if (r <= 0.5f) {
        path.AddRectangle(Gdiplus::RectF(x, y, w, h));
    } else {
        path.AddArc(x, y, d, d, 180.0f, 90.0f);
        path.AddArc(x + w - d, y, d, d, 270.0f, 90.0f);
        path.AddArc(x + w - d, y + h - d, d, d, 0.0f, 90.0f);
        path.AddArc(x, y + h - d, d, d, 90.0f, 90.0f);
        path.CloseFigure();
    }
    if (fill.GetA() > 0) {
        Gdiplus::SolidBrush br(fill);
        g.FillPath(&br, &path);
    }
    if (stroke.GetA() > 0 && stroke_w > 0.0f) {
        Gdiplus::Pen pen(stroke, stroke_w);
        if (dashed) {
            Gdiplus::REAL pat[2] = {5.0f, 4.0f};
            pen.SetDashPattern(pat, 2);
        }
        g.DrawPath(&pen, &path);
    }
}

static HBITMAP create_premul_dib(int w, int h, const unsigned char* premul_bgra) {
    BITMAPINFO bmi{};
    bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth = w;
    bmi.bmiHeader.biHeight = -h;
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = 32;
    bmi.bmiHeader.biCompression = BI_RGB;
    void* bits = nullptr;
    HDC hdc = GetDC(nullptr);
    HBITMAP hbmp = CreateDIBSection(hdc, &bmi, DIB_RGB_COLORS, &bits, nullptr, 0);
    ReleaseDC(nullptr, hdc);
    if (hbmp && bits) std::memcpy(bits, premul_bgra, (size_t)w * (size_t)h * 4);
    return hbmp;
}

static BrandCroppedBmp load_cropped_png_from_mem(const unsigned char* data, size_t len) {
    BrandCroppedBmp res{};
    if (!data || len == 0) return res;
    HGLOBAL hMem = GlobalAlloc(GMEM_MOVEABLE, len);
    if (!hMem) return res;
    void* ptr = GlobalLock(hMem);
    if (!ptr) {
        GlobalFree(hMem);
        return res;
    }
    std::memcpy(ptr, data, len);
    GlobalUnlock(hMem);
    IStream* stm = nullptr;
    if (FAILED(CreateStreamOnHGlobal(hMem, TRUE, &stm)) || !stm) {
        GlobalFree(hMem);
        return res;
    }
    Gdiplus::Bitmap* bmp = Gdiplus::Bitmap::FromStream(stm, FALSE);
    if (!bmp || bmp->GetLastStatus() != Gdiplus::Ok) {
        delete bmp;
        stm->Release();
        return res;
    }
    Gdiplus::Bitmap* owned = bmp->Clone(0, 0, (int)bmp->GetWidth(), (int)bmp->GetHeight(), PixelFormat32bppARGB);
    delete bmp;
    stm->Release();
    bmp = owned;
    if (!bmp || bmp->GetLastStatus() != Gdiplus::Ok) { delete bmp; return res; }
    int w = (int)bmp->GetWidth();
    int h = (int)bmp->GetHeight();
    int minX = w, minY = h, maxX = -1, maxY = -1;
    Gdiplus::BitmapData bd{};
    Gdiplus::Rect fullRc(0, 0, w, h);
    if (bmp->LockBits(&fullRc, Gdiplus::ImageLockModeRead, PixelFormat32bppARGB, &bd) == Gdiplus::Ok) {
        for (int y = 0; y < h; ++y) {
            const auto* row = reinterpret_cast<const BYTE*>(bd.Scan0) + y * bd.Stride;
            for (int x = 0; x < w; ++x) {
                if (row[x * 4 + 3] > 6) {
                    if (x < minX) minX = x;
                    if (x > maxX) maxX = x;
                    if (y < minY) minY = y;
                    if (y > maxY) maxY = y;
                }
            }
        }
        bmp->UnlockBits(&bd);
    }
    res.bmp = bmp;
    if (maxX >= minX && maxY >= minY) {
        res.sx = minX;
        res.sy = minY;
        res.sw = maxX - minX + 1;
        res.sh = maxY - minY + 1;
    } else {
        res.sx = 0;
        res.sy = 0;
        res.sw = w;
        res.sh = h;
    }
    return res;
}

static void draw_alpha_bmp(HDC hdc, HBITMAP hbmp, int srcW, int srcH, int dstX, int dstY, int dstW, int dstH) {
    if (!hbmp) return;
    HDC mem = CreateCompatibleDC(hdc);
    HGDIOBJ old = SelectObject(mem, hbmp);
    BLENDFUNCTION bf{};
    bf.BlendOp = AC_SRC_OVER;
    bf.SourceConstantAlpha = 255;
    bf.AlphaFormat = AC_SRC_ALPHA;
    AlphaBlend(hdc, dstX, dstY, dstW, dstH, mem, 0, 0, srcW, srcH, bf);
    SelectObject(mem, old);
    DeleteDC(mem);
}

static bool get_png_encoder_clsid(CLSID* out_clsid) {
    UINT num = 0, bytes = 0;
    Gdiplus::GetImageEncodersSize(&num, &bytes);
    if (num == 0 || bytes == 0) return false;
    std::vector<BYTE> buf(bytes);
    auto* info = reinterpret_cast<Gdiplus::ImageCodecInfo*>(buf.data());
    if (Gdiplus::GetImageEncoders(num, bytes, info) != Gdiplus::Ok) return false;
    for (UINT i = 0; i < num; ++i) {
        if (info[i].MimeType && std::wcscmp(info[i].MimeType, L"image/png") == 0) {
            *out_clsid = info[i].Clsid;
            return true;
        }
    }
    return false;
}

static bool save_window_screenshot_png(HWND hwnd, const std::wstring& out_path) {
    RECT wrc{};
    GetWindowRect(hwnd, &wrc);
    int ww = wrc.right - wrc.left;
    int wh = wrc.bottom - wrc.top;
    if (ww <= 0 || wh <= 0) return false;

    HDC hScreen = GetDC(nullptr);
    HDC hMem = CreateCompatibleDC(hScreen);
    BITMAPINFO bmi{};
    bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth = ww;
    bmi.bmiHeader.biHeight = -wh; // top-down DIB
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = 32;
    bmi.bmiHeader.biCompression = BI_RGB;
    void* bits = nullptr;
    HBITMAP hBmp = CreateDIBSection(hScreen, &bmi, DIB_RGB_COLORS, &bits, nullptr, 0);
    if (!hBmp || !bits) {
        DeleteDC(hMem);
        ReleaseDC(nullptr, hScreen);
        return false;
    }
    HGDIOBJ old = SelectObject(hMem, hBmp);
    RECT fillRc{0, 0, ww, wh};
    FillRect(hMem, &fillRc, g_app.hBrCanvas);
    SendMessageW(hwnd, WM_PRINT, reinterpret_cast<WPARAM>(hMem),
                 PRF_NONCLIENT | PRF_CLIENT | PRF_CHILDREN | PRF_ERASEBKGND);
    GdiFlush();

    // Ensure 100% opaque alpha across ClearType & GDI child surfaces before PNG encode
    auto* px = static_cast<BYTE*>(bits);
    size_t total_px = (size_t)ww * (size_t)wh;
    for (size_t i = 0; i < total_px; ++i) {
        px[i * 4 + 3] = 255;
    }

    bool saved = false;
    CLSID pngClsid{};
    if (get_png_encoder_clsid(&pngClsid)) {
        Gdiplus::Bitmap gdipBmp(ww, wh, ww * 4, PixelFormat32bppARGB, px);
        saved = (gdipBmp.Save(out_path.c_str(), &pngClsid, nullptr) == Gdiplus::Ok);
    }

    SelectObject(hMem, old);
    DeleteObject(hBmp);
    DeleteDC(hMem);
    ReleaseDC(nullptr, hScreen);
    return saved;
}

static std::wstring reg_value_name(const wchar_t* name) {
    if (std::wcscmp(name, L"CliPath") == 0 || std::wcscmp(name, L"LibRawPath") == 0)
        return std::wstring(name) + L"." NIJIN_VERSION;
    return name;
}

static std::wstring reg_read_str(const wchar_t* name) {
    HKEY hk = nullptr;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, kRegSubKey, 0, KEY_READ, &hk) != ERROR_SUCCESS) return L"";
    wchar_t buf[2048] = {0};
    DWORD cb = sizeof(buf) - sizeof(wchar_t);
    DWORD type = 0;
    LONG rc = RegQueryValueExW(hk, reg_value_name(name).c_str(), nullptr, &type, reinterpret_cast<LPBYTE>(buf), &cb);
    RegCloseKey(hk);
    return (rc == ERROR_SUCCESS && type == REG_SZ) ? std::wstring(buf) : L"";
}

static void reg_write_str(const wchar_t* name, const std::wstring& val) {
    HKEY hk = nullptr;
    if (RegCreateKeyExW(HKEY_CURRENT_USER, kRegSubKey, 0, nullptr, 0, KEY_WRITE, nullptr, &hk, nullptr) != ERROR_SUCCESS) return;
    if (val.empty()) RegDeleteValueW(hk, reg_value_name(name).c_str());
    else RegSetValueExW(hk, reg_value_name(name).c_str(), 0, REG_SZ, reinterpret_cast<const BYTE*>(val.c_str()),
                        (DWORD)((val.size() + 1) * sizeof(wchar_t)));
    RegCloseKey(hk);
}

static bool file_exists_regular(const std::wstring& p) {
    if (p.empty()) return false;
    DWORD a = GetFileAttributesW(p.c_str());
    return (a != INVALID_FILE_ATTRIBUTES) && !(a & FILE_ATTRIBUTE_DIRECTORY);
}

static bool path_is_dir(const std::wstring& p) {
    if (p.empty()) return false;
    DWORD a = GetFileAttributesW(p.c_str());
    return (a != INVALID_FILE_ATTRIBUTES) && ((a & FILE_ATTRIBUTE_DIRECTORY) != 0);
}

static std::wstring trim_ws(const std::wstring& s) {
    size_t b = 0, e = s.size();
    while (b < e && std::iswspace(s[b])) ++b;
    while (e > b && std::iswspace(s[e - 1])) --e;
    return s.substr(b, e - b);
}

static std::wstring utf8_to_wide(const std::string& s) {
    if (s.empty()) return L"";
    int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), nullptr, 0);
    if (n <= 0) return std::wstring(s.begin(), s.end());
    std::wstring w(n, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), &w[0], n);
    return w;
}

static std::string wide_to_utf8(const std::wstring& w) {
    if (w.empty()) return "";
    int n = WideCharToMultiByte(CP_UTF8, 0, w.data(), (int)w.size(), nullptr, 0, nullptr, nullptr);
    if (n <= 0) return "";
    std::string s(n, '\0');
    WideCharToMultiByte(CP_UTF8, 0, w.data(), (int)w.size(), &s[0], n, nullptr, nullptr);
    return s;
}

static std::wstring basename_of_w(const std::wstring& p) {
    size_t pos = p.find_last_of(L"/\\");
    return (pos == std::wstring::npos) ? p : p.substr(pos + 1);
}

static std::wstring stem_of_w(const std::wstring& p) {
    std::wstring b = basename_of_w(p);
    size_t fpos = b.rfind(L"#frame=");
    if (fpos != std::wstring::npos) {
        std::wstring base = b.substr(0, fpos);
        std::wstring kstr = b.substr(fpos + 7);
        size_t dot = base.find_last_of(L'.');
        std::wstring stem = (dot == std::wstring::npos) ? base : base.substr(0, dot);
        return stem + L"#f" + kstr;
    }
    size_t dot = b.find_last_of(L'.');
    return (dot == std::wstring::npos) ? b : b.substr(0, dot);
}

static std::wstring parent_dir_of(const std::wstring& p) {
    if (path_is_dir(p)) return p;
    size_t pos = p.find_last_of(L"/\\");
    return (pos == std::wstring::npos) ? L"" : p.substr(0, pos);
}

static std::wstring get_wnd_text(HWND h) {
    if (!h) return L"";
    int len = GetWindowTextLengthW(h);
    if (len <= 0) return L"";
    std::wstring s(len + 1, L'\0');
    GetWindowTextW(h, &s[0], len + 1);
    s.resize(len);
    return s;
}

static bool get_chk(HWND h) {
    return h && (SendMessageW(h, BM_GETCHECK, 0, 0) == BST_CHECKED);
}

static void set_chk_val(HWND h, bool on) {
    if (h) SendMessageW(h, BM_SETCHECK, on ? BST_CHECKED : BST_UNCHECKED, 0);
}

static std::wstring auto_detect_cli_exe(bool include_saved) {
    if (include_saved && !g_app.is_screenshot_mode) {
        std::wstring saved = reg_read_str(L"CliPath");
        if (file_exists_regular(saved)) return saved;
    }
    static const wchar_t* kNames[] = {L"nijinLBS.exe", L"heraw_sr2x.exe"};
    wchar_t mod[MAX_PATH * 4] = {0};
    GetModuleFileNameW(nullptr, mod, MAX_PATH * 4 - 1);
    std::wstring self_dir = parent_dir_of(mod);
    wchar_t cwd[MAX_PATH * 4] = {0};
    GetCurrentDirectoryW(MAX_PATH * 4 - 1, cwd);

    for (const wchar_t* nm : kNames) {
        if (!self_dir.empty()) {
            std::wstring c1 = self_dir + L"\\" + nm;
            if (file_exists_regular(c1)) return c1;
            std::wstring up = parent_dir_of(self_dir);
            if (!up.empty()) {
                std::wstring c2 = up + L"\\" + nm;
                if (file_exists_regular(c2)) return c2;
            }
        }
        if (cwd[0]) {
            std::wstring c3 = std::wstring(cwd) + L"\\" + nm;
            if (file_exists_regular(c3)) return c3;
        }
        wchar_t sp[MAX_PATH * 4] = {0};
        if (SearchPathW(nullptr, nm, nullptr, MAX_PATH * 4 - 1, sp, nullptr) > 0 && file_exists_regular(sp)) {
            return sp;
        }
    }
    return L"";
}

static std::wstring pick_single_file_dialog(HWND owner, const wchar_t* title, const wchar_t* filter) {
    std::vector<wchar_t> buf(4096, L'\0');
    OPENFILENAMEW ofn{};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = owner;
    ofn.lpstrTitle = title;
    ofn.lpstrFilter = filter;
    ofn.lpstrFile = buf.data();
    ofn.nMaxFile = (DWORD)buf.size();
    ofn.Flags = OFN_EXPLORER | OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST;
    if (!GetOpenFileNameW(&ofn)) return L"";
    return std::wstring(buf.data());
}

static std::wstring save_single_file_dialog(HWND owner, const wchar_t* title, const wchar_t* filter,
                                            const wchar_t* def_ext, const std::wstring& def_name) {
    std::vector<wchar_t> buf(4096, L'\0');
    if (!def_name.empty() && def_name.size() + 1 < buf.size()) {
        std::memcpy(buf.data(), def_name.c_str(), def_name.size() * sizeof(wchar_t));
    }
    OPENFILENAMEW ofn{};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = owner;
    ofn.lpstrTitle = title;
    ofn.lpstrFilter = filter;
    ofn.lpstrDefExt = def_ext;
    ofn.lpstrFile = buf.data();
    ofn.nMaxFile = (DWORD)buf.size();
    ofn.Flags = OFN_EXPLORER | OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST;
    if (!GetSaveFileNameW(&ofn)) return L"";
    return std::wstring(buf.data());
}

static std::wstring pick_folder_dialog(HWND owner, const wchar_t* title) {
    BROWSEINFOW bi{};
    bi.hwndOwner = owner;
    bi.lpszTitle = title;
    bi.ulFlags = BIF_RETURNONLYFSDIRS | BIF_NEWDIALOGSTYLE | BIF_USENEWUI;
    PIDLIST_ABSOLUTE pidl = SHBrowseForFolderW(&bi);
    if (!pidl) return L"";
    wchar_t path[MAX_PATH * 2] = {0};
    bool ok = SHGetPathFromIDListW(pidl, path) != FALSE;
    CoTaskMemFree(pidl);
    return ok ? std::wstring(path) : L"";
}

static std::wstring format_win_cpu_topology_badge() {
    const auto& p = heraw_cache::host_cache_profile();
    const bool en = heraw_i18n::I18nManager::instance().is_english();
    wchar_t buf[160] = {};
    if (p.is_hybrid) {
        if (p.low_power_cores > 0) {
            std::swprintf(buf, 160,
                          en ? L"%dP+%dE+%dLP (%dC/%dT · L3 %zuMB)" : L"%d大核+%d能效核+%d超低功耗核 (%d核%dT · L3 %zuMB)",
                          p.perf_cores, p.eff_cores, p.low_power_cores,
                          p.physical_cores, p.logical_threads, p.l3_total_bytes >> 20);
        } else if (p.is_asymmetric_llc) {
            std::swprintf(buf, 160,
                          en ? L"%dP+%dE (%dC/%dT · L3 %zu+%zuMB)" : L"%d大核+%d小核 (%d核%dT · L3 %zu+%zuMB)",
                          p.perf_cores, p.eff_cores, p.physical_cores, p.logical_threads,
                          p.l3_max_slice_bytes >> 20, p.l3_llc_bytes >> 20);
        } else {
            std::swprintf(buf, 160,
                          en ? L"%dP+%dE (%dC/%dT · L3 %zuMB)" : L"%d大核+%d小核 (%d核%dT · L3 %zuMB)",
                          p.perf_cores, p.eff_cores, p.physical_cores, p.logical_threads,
                          p.l3_total_bytes >> 20);
        }
    } else {
        std::swprintf(buf, 160,
                      en ? L"Homogeneous %dC/%dT (L3 %zuMB%ls)" : L"同构 %d核%dT (L3 %zuMB%ls)",
                      p.physical_cores, p.logical_threads, p.l3_total_bytes >> 20,
                      p.is_3d_vcache ? L" X3D" : L"");
    }
    return buf;
}

static void build_win_cpu_core_labels(std::wstring combo_out[5], std::wstring menu_out[5]) {
    const auto& p = heraw_cache::host_cache_profile();
    const bool en = heraw_i18n::I18nManager::instance().is_english();
    const int n_auto = heraw_cache::recommended_thread_count(p, heraw_cache::CpuCorePolicy::AUTO, 0);
    const int n_perf = heraw_cache::recommended_thread_count(p, heraw_cache::CpuCorePolicy::PERF_ONLY, 0);
    const int n_phys = heraw_cache::recommended_thread_count(p, heraw_cache::CpuCorePolicy::PHYSICAL_ONLY, 0);
    const int n_eff  = heraw_cache::recommended_thread_count(p, heraw_cache::CpuCorePolicy::EFF_ONLY, 0);
    const int n_all  = heraw_cache::recommended_thread_count(p, heraw_cache::CpuCorePolicy::ALL_LOGICAL, 0);
    wchar_t b[160] = {};
    if (p.is_hybrid) {
        std::swprintf(b, 160, en ? L"Auto Hybrid (%dT)" : L"智能异构 (%dT)", n_auto); combo_out[0] = b;
        if (p.low_power_cores > 0) {
            std::swprintf(b, 160, en ? L"CPU Cores: Auto Hybrid (%dT · %dP+%dE, skip %d LP-E)"
                                     : L"CPU 核分配: 智能异构推荐 (%dT · %d大+%d小, 排除%d个LP-E)",
                          n_auto, p.perf_cores, p.eff_cores, p.low_power_cores);
        } else if (p.is_asymmetric_llc) {
            std::swprintf(b, 160, en ? L"CPU Cores: Auto Hybrid (%dT · %dP 2T + %dE 1T L3-aware)"
                                     : L"CPU 核分配: 智能异构推荐 (%dT · %d大核2T + %d小核1T护L3)",
                          n_auto, p.perf_cores, p.eff_cores);
        } else {
            std::swprintf(b, 160, en ? L"CPU Cores: Auto Hybrid (%dT · %dP + %dE)"
                                     : L"CPU 核分配: 智能异构推荐 (%dT · %d大核 + %d小核)",
                          n_auto, p.perf_cores, p.eff_cores);
        }
        menu_out[0] = b;

        std::swprintf(b, 160, en ? L"P-Cores (%dC/%dT)" : L"仅大核 (%d核%dT)", p.perf_cores, n_perf); combo_out[1] = b;
        std::swprintf(b, 160, en ? L"CPU Cores: P-Cores Only (%dP · %dT)" : L"CPU 核分配: 仅性能大核 (%d大核 · %d线程)", p.perf_cores, n_perf); menu_out[1] = b;

        std::swprintf(b, 160, en ? L"Physical (%dT)" : L"仅物理核 (%dT)", n_phys); combo_out[2] = b;
        std::swprintf(b, 160, en ? L"CPU Cores: Physical Only (%dC · No SMT)" : L"CPU 核分配: 仅物理核心 (%d核 · 关超线程)", n_phys); menu_out[2] = b;

        std::swprintf(b, 160, en ? L"E-Cores (%dC/%dT)" : L"仅小核 (%d核%dT)", p.eff_cores, n_eff); combo_out[3] = b;
        std::swprintf(b, 160, en ? L"CPU Cores: E-Cores Only (%dE · %dT)" : L"CPU 核分配: 仅能效小核 (%d小核 · %d线程)", p.eff_cores, n_eff); menu_out[3] = b;

        std::swprintf(b, 160, en ? L"All Logical (%dT)" : L"全核满载 (%dT)", n_all); combo_out[4] = b;
        std::swprintf(b, 160, en ? L"CPU Cores: All Logical (%dC · %dT)" : L"CPU 核分配: 全逻辑核满载 (%d核 · %d线程)", p.physical_cores, n_all); menu_out[4] = b;
    } else {
        std::swprintf(b, 160, en ? L"Auto All (%dT)" : L"自动全核 (%dT)", n_auto); combo_out[0] = b;
        std::swprintf(b, 160, en ? L"CPU Cores: Auto Recommended (%dC · %dT)" : L"CPU 核分配: 自动推荐 (%d核 · %d线程)", p.physical_cores, n_auto); menu_out[0] = b;

        std::swprintf(b, 160, en ? L"Max Perf (%dT)" : L"性能全开 (%dT)", n_perf); combo_out[1] = b;
        std::swprintf(b, 160, en ? L"CPU Cores: Max Performance (%dC · %dT)" : L"CPU 核分配: 性能核心全开 (%d核 · %d线程)", p.physical_cores, n_perf); menu_out[1] = b;

        std::swprintf(b, 160, en ? L"Physical (%dT)" : L"仅物理核 (%dT)", n_phys); combo_out[2] = b;
        std::swprintf(b, 160, en ? L"CPU Cores: Physical Only (%dC · No SMT)" : L"CPU 核分配: 仅物理核心 (%d核 · 关超线程)", n_phys); menu_out[2] = b;

        std::swprintf(b, 160, en ? L"1T/Core (%dT)" : L"单线程/核 (%dT)", n_eff); combo_out[3] = b;
        std::swprintf(b, 160, en ? L"CPU Cores: Power-Saver Budget (%dT)" : L"CPU 核分配: 省电物理核预算 (%d线程)", n_eff); menu_out[3] = b;

        std::swprintf(b, 160, en ? L"All Logical (%dT)" : L"全逻辑核 (%dT)", n_all); combo_out[4] = b;
        std::swprintf(b, 160, en ? L"CPU Cores: All Logical (%dC · %dT)" : L"CPU 核分配: 全逻辑核满载 (%d核 · %d线程)", p.physical_cores, n_all); menu_out[4] = b;
    }
}

static void refresh_perf_header_label() {
    if (!g_app.hBtnPerfHeader) return;
    const auto& p = heraw_cache::host_cache_profile();
    const bool en = heraw_i18n::I18nManager::instance().is_english();
    int bsel = g_app.hCmbBackend ? (int)SendMessageW(g_app.hCmbBackend, CB_GETCURSEL, 0, 0) : 0;
    int csel = g_app.hCmbCpuCores ? (int)SendMessageW(g_app.hCmbCpuCores, CB_GETCURSEL, 0, 0) : 0;
    int psel = g_app.hCmbPriority ? (int)SendMessageW(g_app.hCmbPriority, CB_GETCURSEL, 0, 0) : 0;
    int thr = g_app.hEdtThreads ? _wtoi(get_wnd_text(g_app.hEdtThreads).c_str()) : 0;
    std::wstring btxt = (bsel == 1) ? heraw_i18n::tr_w("hdr.gpu_force")
                      : ((bsel == 2 || (bsel == 0 && g_app.gpu_policy.default_disable_gpu))
                          ? heraw_i18n::tr_w("hdr.gpu_cpu") : heraw_i18n::tr_w("hdr.gpu_auto"));
    std::wstring ctxt;
    if (thr > 0) {
        ctxt = std::to_wstring(thr) + L"T";
    } else if (csel == 1) {
        ctxt = p.is_hybrid ? (std::to_wstring(p.perf_cores) + (en ? L"P" : L"大核")) : (en ? L"Perf" : L"性能核");
    } else if (csel == 2) {
        ctxt = std::to_wstring(p.physical_cores - p.low_power_cores) + (en ? L"Phys" : L"物理核");
    } else if (csel == 3) {
        ctxt = p.is_hybrid ? (std::to_wstring(p.eff_cores) + (en ? L"E" : L"小核")) : (en ? L"Eco" : L"省电核");
    } else if (csel == 4) {
        ctxt = (en ? L"All " : L"全") + std::to_wstring(p.logical_threads) + L"T";
    } else {
        ctxt = p.is_hybrid
            ? (std::to_wstring(p.perf_cores) + (en ? L"P+" : L"大+") + std::to_wstring(p.eff_cores) + (en ? L"E" : L"小"))
            : (std::to_wstring(p.physical_cores) + L"C" + std::to_wstring(p.logical_threads) + L"T");
    }
    if (psel == 3) ctxt += en ? L"·Eco" : L"·省电";
    else if (psel == 1) ctxt += en ? L"·High" : L"·高优";
    std::wstring label = heraw_i18n::trf_w("hdr.perf_fmt", wide_to_utf8(btxt).c_str(), wide_to_utf8(ctxt).c_str());
    SetWindowTextW(g_app.hBtnPerfHeader, label.c_str());
    InvalidateRect(g_app.hBtnPerfHeader, nullptr, FALSE);
}

static void refresh_engine_status_label() {
    if (!g_app.hLblEngineStatus) return;
    std::string cpu_s = wide_to_utf8(format_win_cpu_topology_badge());
    std::string cli_s = g_app.cli_path.empty() ? heraw_i18n::tr_s("t1.engine_select_cli") : wide_to_utf8(basename_of_w(g_app.cli_path));
    std::string lr_s  = g_app.libraw_path.empty()
                      ? heraw_i18n::tr_s("t1.engine_builtin_lr")
                      : heraw_i18n::trf("t1.engine_ext_lr_fmt", wide_to_utf8(basename_of_w(g_app.libraw_path)).c_str());
    SetWindowTextW(g_app.hLblEngineStatus, heraw_i18n::trf_w("t1.engine_strip_fmt", cpu_s.c_str(), cli_s.c_str(), lr_s.c_str()).c_str());
    refresh_perf_header_label();
}

static std::wstring resolve_active_cli_or_prompt(HWND owner) {
    if (file_exists_regular(g_app.cli_path)) {
        reg_write_str(L"CliPath", g_app.cli_path);
        return g_app.cli_path;
    }
    std::wstring det = auto_detect_cli_exe(true);
    if (!det.empty()) {
        g_app.cli_path = det;
        refresh_engine_status_label();
        return det;
    }
    std::wstring title = heraw_i18n::tr_w("dlg.pick_cli");
    std::wstring chosen = pick_single_file_dialog(
        owner, title.c_str(),
        L"CLI Engine (nijinLBS.exe;*.exe)\0nijinLBS.exe;*.exe\0All Files (*.*)\0*.*\0"
    );
    if (file_exists_regular(chosen)) {
        g_app.cli_path = chosen;
        reg_write_str(L"CliPath", chosen);
        refresh_engine_status_label();
        return chosen;
    }
    return L"";
}

static void set_status_ui(const std::wstring& text);
static int seg_pill_get_sel(HWND hw);

static void export_session_log_dialog(HWND owner) {
    std::wstring title = heraw_i18n::tr_w("dlg.save_log");
    std::wstring def_name = utf8_to_wide(heraw_i18n::SessionDiagLog::default_filename());
    std::wstring dst = save_single_file_dialog(
        owner, title.c_str(),
        L"Diagnostic Log (*.log;*.txt)\0*.log;*.txt\0All Files (*.*)\0*.*\0",
        L"log", def_name
    );
    if (dst.empty()) return;
    std::string utf8_dst = wide_to_utf8(dst);
    heraw_i18n::diag_log().log_event("UI", "Exporting diagnostic log to " + utf8_dst);
    if (heraw_i18n::diag_log().export_to_file(utf8_dst)) {
        set_status_ui(heraw_i18n::trf_w("st.log_saved_fmt", utf8_dst.c_str()));
    } else {
        set_status_ui(heraw_i18n::tr_w("st.log_err"));
    }
}

static std::wstring selected_rgb_fmt_code() {
    int sel = g_app.hCmbRgbFmt ? (int)SendMessageW(g_app.hCmbRgbFmt, CB_GETCURSEL, 0, 0) : 0;
    return (sel >= 0 && sel < kNumRgbFmts) ? kRgbFmtCodes[sel] : L"tiff-deflate";
}

static std::wstring selected_rgb_scale_code() {
    int sel = g_app.hSegRgbScale ? seg_pill_get_sel(g_app.hSegRgbScale) : 0;
    return (sel >= 0 && sel < 3) ? kRgbScaleCodes[sel] : L"2x";
}

template <size_t N>
static std::wstring selected_cmb_code(HWND hCmb, const wchar_t* const (&codes)[N], const wchar_t* def_val) {
    if (!hCmb) return def_val;
    int sel = (int)SendMessageW(hCmb, CB_GETCURSEL, 0, 0);
    return (sel >= 0 && sel < (int)N) ? codes[sel] : def_val;
}

template <size_t N>
static void select_cmb_code(HWND hCmb, const wchar_t* const (&codes)[N], const std::wstring& val) {
    if (!hCmb || val.empty()) return;
    for (int i = 0; i < (int)N; ++i) {
        if (val == codes[i]) { SendMessageW(hCmb, CB_SETCURSEL, (WPARAM)i, 0); return; }
    }
}

static bool has_dxo_input_win() {
    for (const auto& rawp : g_app.inputs) {
        std::wstring p = rawp;
        size_t pos = p.rfind(L"#frame=");
        if (pos != std::wstring::npos) p = p.substr(0, pos);
        size_t dot = p.find_last_of(L'.');
        if (dot == std::wstring::npos) continue;
        std::wstring ext = p.substr(dot);
        for (wchar_t& ch : ext) ch = (wchar_t)std::towlower(ch);
        if (ext == L".dxo" || ext == L".4rw") return true;
    }
    return false;
}

static int min_valid_frames_win() {
    bool emit_raw = g_app.hChkRaw ? get_chk(g_app.hChkRaw) : true;
    bool emit_rgb = g_app.hChkRgb ? get_chk(g_app.hChkRgb) : true;
    if (!emit_raw && emit_rgb && selected_rgb_scale_code() == L"1x") return 3;
    if (selected_cmb_code(g_app.hCmbMotionProfile, kMotionProfileCodes, L"auto") == L"dxo-superraw" || has_dxo_input_win()) return 4;
    return 5;
}

static std::wstring quote_win_arg(const std::wstring& arg) {
    if (!arg.empty() && arg.find_first_of(L" \t\n\v\"") == std::wstring::npos) return arg;
    std::wstring q = L"\"";
    for (size_t i = 0; i < arg.size(); ++i) {
        unsigned bs = 0;
        while (i < arg.size() && arg[i] == L'\\') { ++i; ++bs; }
        if (i == arg.size()) { q.append(bs * 2, L'\\'); break; }
        else if (arg[i] == L'"') { q.append(bs * 2 + 1, L'\\'); q.push_back(L'"'); }
        else { q.append(bs, L'\\'); q.push_back(arg[i]); }
    }
    q.push_back(L'"');
    return q;
}

static std::wstring join_cmd_args(const std::vector<std::wstring>& args) {
    std::wstring cmd;
    for (size_t i = 0; i < args.size(); ++i) {
        if (i > 0) cmd.push_back(L' ');
        cmd += quote_win_arg(args[i]);
    }
    return cmd;
}

static void set_status_ui(const std::wstring& text) {
    g_app.status_line = text;
    if (g_app.hwnd) {
        RECT crc{};
        GetClientRect(g_app.hwnd, &crc);
        RECT bar{g_app.S(20), crc.bottom - g_app.S(72), crc.right - g_app.S(20), crc.bottom - g_app.S(10)};
        InvalidateRect(g_app.hwnd, &bar, FALSE);
    }
}

static void post_status(const std::wstring& s) {
    auto* hs = new std::wstring(s);
    if (!PostMessageW(g_app.hwnd, WM_APP_STATUS, 0, reinterpret_cast<LPARAM>(hs))) delete hs;
}

// ============================================================================
// Lightweight JSON Helpers for --plan-json and _telemetry.json
// ============================================================================
static std::string extract_json_str(const std::string& s, const char* key, size_t start_pos = 0, size_t end_pos = std::string::npos) {
    std::string pat = std::string("\"") + key + "\"";
    size_t p = s.find(pat, start_pos);
    if (p == std::string::npos || p >= end_pos) return "";
    p = s.find(':', p + pat.size());
    if (p == std::string::npos || p >= end_pos) return "";
    ++p;
    while (p < s.size() && p < end_pos && (s[p] == ' ' || s[p] == '\t' || s[p] == '\r' || s[p] == '\n')) ++p;
    if (p >= s.size() || p >= end_pos || s[p] != '"') return "";
    ++p;
    std::string out;
    while (p < s.size() && p < end_pos && s[p] != '"') {
        char c = s[p++];
        if (c != '\\') { out.push_back(c); continue; }
        if (p >= s.size()) return "";
        c = s[p++];
        if (c == 'n') out.push_back('\n');
        else if (c == 'r') out.push_back('\r');
        else if (c == 't') out.push_back('\t');
        else if (c == 'b') out.push_back('\b');
        else if (c == 'f') out.push_back('\f');
        else if (c == 'u') {
            std::wstring units;
            do {
                if (p + 4 > s.size()) return "";
                unsigned v = 0;
                for (int i = 0; i < 4; ++i) {
                    char h = s[p++];
                    unsigned d = h >= '0' && h <= '9' ? h - '0' :
                                 h >= 'a' && h <= 'f' ? h - 'a' + 10 :
                                 h >= 'A' && h <= 'F' ? h - 'A' + 10 : 16;
                    if (d == 16) return "";
                    v = v * 16 + d;
                }
                units.push_back((wchar_t)v);
                if (v < 0xD800 || v > 0xDBFF || s.compare(p, 2, "\\u") != 0) break;
                p += 2;
            } while (units.size() < 2);
            out += wide_to_utf8(units);
        } else if (c == '"' || c == '\\' || c == '/') out.push_back(c);
        else return "";
    }
    return out;
}

static bool has_json_key(const std::string& s, const char* key, size_t start_pos = 0, size_t end_pos = std::string::npos) {
    std::string pat = std::string("\"") + key + "\"";
    size_t p = s.find(pat, start_pos);
    return p != std::string::npos && p < end_pos;
}

static double parse_json_double(const std::string& s, const char* key, double fallback, size_t start_pos = 0, size_t end_pos = std::string::npos) {
    std::string pat = std::string("\"") + key + "\"";
    size_t p = s.find(pat, start_pos);
    if (p == std::string::npos || p >= end_pos) return fallback;
    p = s.find(':', p + pat.size());
    if (p == std::string::npos || p >= end_pos) return fallback;
    ++p;
    while (p < s.size() && p < end_pos && (s[p] == ' ' || s[p] == '\t')) ++p;
    if (p >= s.size() || p >= end_pos) return fallback;
    char* end = nullptr;
    double value = std::strtod(s.c_str() + p, &end);
    return end != s.c_str() + p && std::isfinite(value) ? value : fallback;
}

static int parse_json_int(const std::string& s, const char* key, int fallback, size_t start_pos = 0, size_t end_pos = std::string::npos) {
    return (int)std::llround(parse_json_double(s, key, (double)fallback, start_pos, end_pos));
}

static bool parse_json_bool(const std::string& s, const char* key, bool fallback, size_t start_pos = 0, size_t end_pos = std::string::npos) {
    std::string pat = std::string("\"") + key + "\"";
    size_t p = s.find(pat, start_pos);
    if (p == std::string::npos || p >= end_pos) return fallback;
    p = s.find(':', p + pat.size());
    if (p == std::string::npos || p >= end_pos) return fallback;
    ++p;
    while (p < s.size() && p < end_pos && (s[p] == ' ' || s[p] == '\t')) ++p;
    if (p + 4 <= s.size() && s.compare(p, 4, "true") == 0) return true;
    if (p + 5 <= s.size() && s.compare(p, 5, "false") == 0) return false;
    return fallback;
}

static size_t find_matching_brace(const std::string& s, size_t open_pos, char open_c, char close_c) {
    if (open_pos >= s.size() || s[open_pos] != open_c) return std::string::npos;
    int depth = 0;
    bool in_str = false;
    for (size_t i = open_pos; i < s.size(); ++i) {
        char c = s[i];
        if (in_str) {
            if (c == '\\' && i + 1 < s.size()) { ++i; continue; }
            if (c == '"') in_str = false;
            continue;
        }
        if (c == '"') in_str = true;
        else if (c == open_c) ++depth;
        else if (c == close_c) {
            --depth;
            if (depth == 0) return i;
        }
    }
    return std::string::npos;
}

static std::string json_container(const std::string& text, const char* key, char opening = '{') {
    size_t pos = text.find(std::string("\"") + key + "\"");
    if (pos == std::string::npos) return "";
    pos = text.find(':', pos);
    if (pos == std::string::npos) return "";
    ++pos;
    while (pos < text.size() && std::isspace((unsigned char)text[pos])) ++pos;
    if (pos == text.size() || text[pos] != opening) return "";
    size_t end = find_matching_brace(text, pos, opening, opening == '{' ? '}' : ']');
    return end == std::string::npos ? "" : text.substr(pos, end - pos + 1);
}

static std::vector<std::string> json_objects(const std::string& text, const char* key) {
    std::string array = json_container(text, key, '[');
    std::vector<std::string> objects;
    size_t pos = 1;
    while ((pos = array.find('{', pos)) != std::string::npos) {
        size_t end = find_matching_brace(array, pos, '{', '}');
        if (end == std::string::npos) break;
        objects.push_back(array.substr(pos, end - pos + 1));
        pos = end + 1;
    }
    return objects;
}

static std::vector<double> json_numbers(const std::string& text, const char* key) {
    std::string array = json_container(text, key, '[');
    std::vector<double> values;
    size_t pos = 1;
    while (pos < array.size()) {
        while (pos < array.size() && (std::isspace((unsigned char)array[pos]) || array[pos] == ',')) ++pos;
        if (pos == array.size() || array[pos] == ']') break;
        char* end = nullptr;
        double value = std::strtod(array.c_str() + pos, &end);
        if (end == array.c_str() + pos || !std::isfinite(value)) return {};
        values.push_back(value);
        pos = (size_t)(end - array.c_str());
    }
    return values;
}

static double json_metric(const std::string& object, const char* key, double fallback = NAN) {
    return parse_json_double(json_container(object, key), "value", fallback);
}

static std::wstring scene_code_to_cn(const std::string& sc) {
    const bool en = heraw_i18n::I18nManager::instance().is_english();
    if (sc == "astro_landscape")   return en ? L"Astro Landscape" : L"手持星野";
    if (sc == "astro_sky")         return en ? L"Deep-Sky"        : L"深空星场";
    if (sc == "aeb_bracket")       return en ? L"AEB Bracket"     : L"AEB 包围曝光";
    if (sc == "tripod_static")     return en ? L"Tripod Static"   : L"脚架稳态";
    if (sc == "action_subject")    return en ? L"Fast Action"     : L"高速动体";
    if (sc == "lowlight_high_iso") return en ? L"High-ISO"        : L"高感弱光";
    return en ? L"Handheld Burst" : L"手持超分";
}

static std::wstring preset_code_to_cn(const std::string& pre) {
    if (pre == "astro") return heraw_i18n::tr_w("t1.preset_astro");
    if (pre == "sci")   return heraw_i18n::tr_w("t1.preset_sci");
    if (pre == "arc")   return heraw_i18n::tr_w("t1.preset_arc");
    return heraw_i18n::tr_w("t1.preset_photo");
}

static int preset_code_to_idx(const std::string& pre) {
    if (pre == "astro") return 1;
    if (pre == "sci")   return 2;
    if (pre == "arc")   return 3;
    return 0;
}

static std::wstring flags_to_anomaly_cn(const std::wstring& fl, bool is_shock, bool is_locked) {
    if (is_locked) return heraw_i18n::tr_w("ftag.anchor");
    if (fl.find(L"DARK") != std::wstring::npos || fl.find(L"BLACK") != std::wstring::npos) return heraw_i18n::tr_w("ftag.dark");
    if (fl.find(L"EXP") != std::wstring::npos) return heraw_i18n::tr_w("ftag.exp");
    if (fl.find(L"BLUR") != std::wstring::npos || fl.find(L"SHARP") != std::wstring::npos ||
        fl.find(L"OUTLIER") != std::wstring::npos) return heraw_i18n::tr_w("ftag.blur");
    if (is_shock || fl.find(L"SHOCK") != std::wstring::npos) return heraw_i18n::tr_w("ftag.shock");
    return L"";
}

// ============================================================================
// Modern Antialiased Subclasses & Custom Controls (Zero Win95 Sunken Bevels)
// ============================================================================
struct ToggleSubData {
    bool hovered = false;
    bool is_radio = false;
    bool is_bold = false;
    COLORREF bg_col = kColCard;
};

static void paint_modern_toggle(HWND hw, HDC hdc, const ToggleSubData* td) {
    RECT rc{};
    GetClientRect(hw, &rc);
    HBRUSH bgBr = (td && td->bg_col == kColCardSub) ? g_app.hBrCardSub : g_app.hBrCard;
    FillRect(hdc, &rc, bgBr);

    bool hovered  = td && td->hovered;
    bool is_radio = td && td->is_radio;
    bool is_bold  = td && td->is_bold;
    bool checked  = (SendMessageW(hw, BM_GETCHECK, 0, 0) == BST_CHECKED);
    bool enabled  = IsWindowEnabled(hw) != FALSE;

    float box = (float)g_app.S(15);
    float bx = 1.0f;
    float by = 0.5f * ((float)(rc.bottom - rc.top) - box);

    {
        Gdiplus::Graphics g(hdc);
        g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);

        COLORREF fillCol = !enabled ? RGB(232, 237, 245)
                         : checked  ? (hovered ? kColAccentDark : kColAccent)
                         : (hovered ? RGB(236, 242, 250) : RGB(255, 255, 255));
        COLORREF edgeCol = !enabled ? RGB(196, 206, 220)
                         : checked  ? (hovered ? kColAccentDark : kColAccent)
                         : (hovered ? kColAccent : RGB(166, 184, 210));

        if (is_radio) {
            Gdiplus::SolidBrush br(gdip_col(fillCol));
            Gdiplus::Pen pen(gdip_col(edgeCol), 1.25f);
            g.FillEllipse(&br, bx, by, box, box);
            g.DrawEllipse(&pen, bx, by, box, box);
            if (checked) {
                float dot = box * 0.40f;
                Gdiplus::SolidBrush wbr(Gdiplus::Color(255, 255, 255, 255));
                g.FillEllipse(&wbr, bx + 0.5f * (box - dot), by + 0.5f * (box - dot), dot, dot);
            }
        } else {
            gdip_round_rect(g, bx, by, box, box, (float)g_app.S(4), gdip_col(fillCol), gdip_col(edgeCol), 1.2f);
            if (checked) {
                Gdiplus::Pen chkPen(Gdiplus::Color(255, 255, 255, 255), (float)std::max(1, g_app.S(1)) * 1.75f);
                chkPen.SetStartCap(Gdiplus::LineCapRound);
                chkPen.SetEndCap(Gdiplus::LineCapRound);
                chkPen.SetLineJoin(Gdiplus::LineJoinRound);
                Gdiplus::PointF pts[3] = {
                    Gdiplus::PointF(bx + box * 0.23f, by + box * 0.52f),
                    Gdiplus::PointF(bx + box * 0.43f, by + box * 0.72f),
                    Gdiplus::PointF(bx + box * 0.78f, by + box * 0.28f)
                };
                g.DrawLines(&chkPen, pts, 3);
            }
        }
    }

    wchar_t txt[200] = {0};
    GetWindowTextW(hw, txt, 199);
    SetBkMode(hdc, TRANSPARENT);
    SetTextColor(hdc, enabled ? kColTextMain : kColTextMuted);
    HGDIOBJ of = SelectObject(hdc, is_bold ? g_app.hFontBold : g_app.hFont);
    RECT tr = rc;
    tr.left += (int)box + g_app.S(7);
    DrawTextW(hdc, txt, -1, &tr, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);
    SelectObject(hdc, of);
}

static LRESULT CALLBACK ModernToggleSubclassProc(HWND hw, UINT msg, WPARAM wParam, LPARAM lParam, UINT_PTR uId, DWORD_PTR dwRef) {
    (void)uId;
    auto* td = reinterpret_cast<ToggleSubData*>(dwRef);
    switch (msg) {
        case WM_ERASEBKGND: return 1;
        case WM_MOUSEMOVE:
            if (td && !td->hovered) {
                td->hovered = true;
                TRACKMOUSEEVENT tme{};
                tme.cbSize = sizeof(tme);
                tme.dwFlags = TME_LEAVE;
                tme.hwndTrack = hw;
                TrackMouseEvent(&tme);
                InvalidateRect(hw, nullptr, FALSE);
            }
            break;
        case WM_MOUSELEAVE:
            if (td && td->hovered) {
                td->hovered = false;
                InvalidateRect(hw, nullptr, FALSE);
            }
            break;
        case BM_SETCHECK: {
            LRESULT r = DefSubclassProc(hw, msg, wParam, lParam);
            InvalidateRect(hw, nullptr, FALSE);
            return r;
        }
        case WM_ENABLE: {
            LRESULT r = DefSubclassProc(hw, msg, wParam, lParam);
            InvalidateRect(hw, nullptr, FALSE);
            return r;
        }
        case WM_LBUTTONUP:
        case WM_KEYUP: {
            LRESULT r = DefSubclassProc(hw, msg, wParam, lParam);
            if (td && td->is_radio) {
                HWND hp = GetParent(hw);
                if (hp) {
                    HWND hc = GetWindow(hp, GW_CHILD);
                    while (hc) {
                        if (IsWindowVisible(hc)) {
                            wchar_t cls[32] = {0};
                            GetClassNameW(hc, cls, 31);
                            if (_wcsicmp(cls, L"Button") == 0) InvalidateRect(hc, nullptr, FALSE);
                        }
                        hc = GetWindow(hc, GW_HWNDNEXT);
                    }
                }
            }
            return r;
        }
        case WM_PAINT: {
            PAINTSTRUCT ps{};
            HDC hdc = BeginPaint(hw, &ps);
            paint_modern_toggle(hw, hdc, td);
            EndPaint(hw, &ps);
            return 0;
        }
        case WM_PRINT:
        case WM_PRINTCLIENT: {
            paint_modern_toggle(hw, reinterpret_cast<HDC>(wParam), td);
            return 0;
        }
        case WM_NCDESTROY:
            RemoveWindowSubclass(hw, ModernToggleSubclassProc, uId);
            delete td;
            break;
    }
    return DefSubclassProc(hw, msg, wParam, lParam);
}

static void attach_modern_toggle(HWND hw, bool is_radio, bool is_bold = false, COLORREF bg = kColCard) {
    auto* td = new ToggleSubData{};
    td->is_radio = is_radio;
    td->is_bold = is_bold;
    td->bg_col = bg;
    SetWindowSubclass(hw, ModernToggleSubclassProc, 1, reinterpret_cast<DWORD_PTR>(td));
}

static LRESULT CALLBACK ModernBtnHoverSubclassProc(HWND hw, UINT msg, WPARAM wParam, LPARAM lParam, UINT_PTR uId, DWORD_PTR dwRef) {
    (void)dwRef;
    switch (msg) {
        case WM_SETCURSOR: {
            int cid = GetDlgCtrlID(hw);
            if (cid == ID_BTN_BRAND_HEADER || cid == ID_WDLG_BRAND || cid == ID_BTN_PERF_HEADER) {
                SetCursor(LoadCursorW(nullptr, IDC_HAND));
                return TRUE;
            }
            break;
        }
        case WM_MOUSEMOVE:
            if (GetPropW(hw, L"NijiHover") == nullptr) {
                SetPropW(hw, L"NijiHover", reinterpret_cast<HANDLE>(1));
                TRACKMOUSEEVENT tme{};
                tme.cbSize = sizeof(tme);
                tme.dwFlags = TME_LEAVE;
                tme.hwndTrack = hw;
                TrackMouseEvent(&tme);
                InvalidateRect(hw, nullptr, FALSE);
            }
            break;
        case WM_MOUSELEAVE:
            if (GetPropW(hw, L"NijiHover") != nullptr) {
                RemovePropW(hw, L"NijiHover");
                InvalidateRect(hw, nullptr, FALSE);
            }
            break;
        case WM_NCDESTROY:
            RemovePropW(hw, L"NijiHover");
            RemovePropW(hw, L"NijiPortal");
            RemoveWindowSubclass(hw, ModernBtnHoverSubclassProc, uId);
            break;
    }
    return DefSubclassProc(hw, msg, wParam, lParam);
}

static void paint_modern_combo(HWND hw, HDC hdc, bool hovered) {
    RECT rc{};
    GetClientRect(hw, &rc);
    FillRect(hdc, &rc, g_app.hBrCard);

    bool enabled = IsWindowEnabled(hw) != FALSE;
    {
        Gdiplus::Graphics g(hdc);
        g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
        COLORREF fillCol = !enabled ? kColDisabledFill : (hovered ? RGB(246, 249, 253) : RGB(255, 255, 255));
        COLORREF edgeCol = !enabled ? RGB(204, 214, 228) : (hovered ? kColAccent : kColInputEdge);
        gdip_round_rect(g, 0.5f, 0.5f, (float)(rc.right - 1) - 0.5f, (float)(rc.bottom - 1) - 0.5f,
                        (float)g_app.S(6), gdip_col(fillCol), gdip_col(edgeCol), 1.15f);

        float cx = (float)rc.right - (float)g_app.S(13);
        float cy = 0.5f * (float)rc.bottom;
        float cw = (float)g_app.S(4);
        Gdiplus::Pen chevPen(gdip_col(!enabled ? kColTextMuted : (hovered ? kColAccentDark : kColBadgeText)), 1.6f);
        chevPen.SetStartCap(Gdiplus::LineCapRound);
        chevPen.SetEndCap(Gdiplus::LineCapRound);
        chevPen.SetLineJoin(Gdiplus::LineJoinRound);
        Gdiplus::PointF pts[3] = {
            Gdiplus::PointF(cx - cw, cy - 0.55f * cw),
            Gdiplus::PointF(cx,      cy + 0.55f * cw),
            Gdiplus::PointF(cx + cw, cy - 0.55f * cw)
        };
        g.DrawLines(&chevPen, pts, 3);
    }

    int sel = (int)SendMessageW(hw, CB_GETCURSEL, 0, 0);
    if (sel >= 0) {
        wchar_t buf[256] = {0};
        SendMessageW(hw, CB_GETLBTEXT, (WPARAM)sel, reinterpret_cast<LPARAM>(buf));
        SetBkMode(hdc, TRANSPARENT);
        SetTextColor(hdc, enabled ? kColTextMain : kColTextMuted);
        HGDIOBJ of = SelectObject(hdc, g_app.hFont);
        RECT tr{g_app.S(9), 0, rc.right - g_app.S(24), rc.bottom};
        DrawTextW(hdc, buf, -1, &tr, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);
        SelectObject(hdc, of);
    }
}

static LRESULT CALLBACK ModernComboSubclassProc(HWND hw, UINT msg, WPARAM wParam, LPARAM lParam, UINT_PTR uId, DWORD_PTR dwRef) {
    (void)dwRef;
    switch (msg) {
        case WM_ERASEBKGND: return 1;
        case WM_MOUSEMOVE:
            if (GetPropW(hw, L"NijiHover") == nullptr) {
                SetPropW(hw, L"NijiHover", reinterpret_cast<HANDLE>(1));
                TRACKMOUSEEVENT tme{};
                tme.cbSize = sizeof(tme);
                tme.dwFlags = TME_LEAVE;
                tme.hwndTrack = hw;
                TrackMouseEvent(&tme);
                InvalidateRect(hw, nullptr, FALSE);
            }
            break;
        case WM_MOUSELEAVE:
            if (GetPropW(hw, L"NijiHover") != nullptr) {
                RemovePropW(hw, L"NijiHover");
                InvalidateRect(hw, nullptr, FALSE);
            }
            break;
        case CB_SETCURSEL:
        case WM_ENABLE: {
            LRESULT r = DefSubclassProc(hw, msg, wParam, lParam);
            InvalidateRect(hw, nullptr, FALSE);
            return r;
        }
        case WM_PAINT: {
            PAINTSTRUCT ps{};
            HDC hdc = BeginPaint(hw, &ps);
            paint_modern_combo(hw, hdc, GetPropW(hw, L"NijiHover") != nullptr);
            EndPaint(hw, &ps);
            return 0;
        }
        case WM_PRINT:
        case WM_PRINTCLIENT: {
            paint_modern_combo(hw, reinterpret_cast<HDC>(wParam), GetPropW(hw, L"NijiHover") != nullptr);
            return 0;
        }
        case WM_NCDESTROY:
            RemovePropW(hw, L"NijiHover");
            RemoveWindowSubclass(hw, ModernComboSubclassProc, uId);
            break;
    }
    return DefSubclassProc(hw, msg, wParam, lParam);
}

static void draw_brand_lockup_button(const DRAWITEMSTRUCT* dis) {
    FillRect(dis->hDC, &dis->rcItem, g_app.hBrCard);
    bool pushed  = (dis->itemState & ODS_SELECTED) != 0;
    bool hovered = GetPropW(dis->hwndItem, L"NijiHover") != nullptr;
    int bw = dis->rcItem.right - dis->rcItem.left;
    int bh = dis->rcItem.bottom - dis->rcItem.top;
    if (bw <= 0 || bh <= 0) return;

    Gdiplus::Graphics g(dis->hDC);
    g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
    g.SetInterpolationMode(Gdiplus::InterpolationModeHighQualityBicubic);
    g.SetPixelOffsetMode(Gdiplus::PixelOffsetModeHighQuality);

    if (hovered || pushed) {
        COLORREF bg = pushed ? RGB(220, 232, 248) : RGB(242, 246, 253);
        COLORREF bd = RGB(188, 206, 230);
        gdip_round_rect(g, 1.0f, 1.0f, (float)bw - 2.0f, (float)bh - 2.0f, (float)g_app.S(8),
                        gdip_col(bg), gdip_col(bd), 1.0f);
    }

    int maxH = std::max(g_app.S(22), bh - g_app.S(6));
    double sc = std::min(1.0, (double)maxH / (double)g_app.S(heraw_brand::kGx1xH));
    int gxDstW    = std::max(1, (int)std::lround(g_app.S(heraw_brand::kGx1xW) * sc));
    int gxDstH    = std::max(1, (int)std::lround(g_app.S(heraw_brand::kGx1xH) * sc));
    int nijinDstW = std::max(1, (int)std::lround(g_app.S(heraw_brand::kNijin1xW) * sc));
    int nijinDstH = std::max(1, (int)std::lround(g_app.S(heraw_brand::kNijin1xH) * sc));
    int gap       = g_app.S(9);
    int nx        = dis->rcItem.left + std::max(g_app.S(4), bw - g_app.S(4) - nijinDstW);
    int startX    = std::max<int>((int)dis->rcItem.left + g_app.S(2), nx - gap - gxDstW);
    int gxY       = dis->rcItem.top + (bh - gxDstH) / 2;
    int njY       = dis->rcItem.top + (bh - nijinDstH) / 2;

    if (g_app.brandGx.bmp) {
        g.DrawImage(g_app.brandGx.bmp,
                    Gdiplus::Rect(startX, gxY, gxDstW, gxDstH),
                    g_app.brandGx.sx, g_app.brandGx.sy, g_app.brandGx.sw, g_app.brandGx.sh,
                    Gdiplus::UnitPixel);
    } else {
        draw_alpha_bmp(dis->hDC, g_app.hBmpGxFallback, heraw_brand::kGx1xW, heraw_brand::kGx1xH,
                       startX, gxY, gxDstW, gxDstH);
    }
    if (g_app.brandNijin.bmp) {
        g.DrawImage(g_app.brandNijin.bmp,
                    Gdiplus::Rect(nx, njY, nijinDstW, nijinDstH),
                    g_app.brandNijin.sx, g_app.brandNijin.sy, g_app.brandNijin.sw, g_app.brandNijin.sh,
                    Gdiplus::UnitPixel);
    } else {
        draw_alpha_bmp(dis->hDC, g_app.hBmpNijinFallback, heraw_brand::kNijin1xW, heraw_brand::kNijin1xH,
                       nx, njY, nijinDstW, nijinDstH);
    }
}

static void draw_modern_pill_button(const DRAWITEMSTRUCT* dis) {
    if (dis->CtlID == ID_BTN_BRAND_HEADER || dis->CtlID == ID_WDLG_BRAND) {
        draw_brand_lockup_button(dis);
        return;
    }
    bool on_canvas = (dis->CtlID == ID_WDLG_CONFIRM || dis->CtlID == ID_WDLG_CLOSE || dis->CtlID == ID_WDLG_REOPEN);
    FillRect(dis->hDC, &dis->rcItem, on_canvas ? g_app.hBrCanvas : g_app.hBrCard);
    bool disabled   = (dis->itemState & ODS_DISABLED) != 0;
    bool pushed     = (dis->itemState & ODS_SELECTED) != 0;
    bool hovered    = GetPropW(dis->hwndItem, L"NijiHover") != nullptr;
    bool is_portal  = GetPropW(dis->hwndItem, L"NijiPortal") != nullptr;
    bool is_primary = (dis->CtlID == ID_BTN_RUN || dis->CtlID == ID_WDLG_CONFIRM || dis->CtlID == ID_WDLG_CLOSE || dis->CtlID == ID_IDLG_APPLY);
    bool is_stop    = (dis->CtlID == ID_BTN_RUN) && g_app.running.load();

    wchar_t txt[160] = {0};
    GetWindowTextW(dis->hwndItem, txt, 159);
    bool portal_active = is_portal && (std::wcschr(txt, L'•') != nullptr);

    {
        Gdiplus::Graphics g(dis->hDC);
        g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
        float w = (float)(dis->rcItem.right - dis->rcItem.left) - 1.5f;
        float h = (float)(dis->rcItem.bottom - dis->rcItem.top) - 1.5f;
        if (is_stop) {
            COLORREF bg = pushed ? kColStopBtnDark : (hovered ? RGB(164, 72, 66) : kColStopBtn);
            gdip_round_rect(g, 0.75f, 0.75f, w, h, (float)g_app.S(8), gdip_col(bg), gdip_col(kColStopBtnDark), 1.1f);
        } else if (is_primary) {
            COLORREF bg = disabled ? RGB(172, 186, 206)
                        : pushed   ? RGB(52, 84, 126)
                        : hovered  ? RGB(68, 106, 154) : kColAccent;
            COLORREF bd = disabled ? RGB(156, 172, 194) : kColAccentDark;
            gdip_round_rect(g, 0.75f, 0.75f, w, h, (float)g_app.S(8), gdip_col(bg), gdip_col(bd), 1.1f);
        } else if (is_portal) {
            COLORREF bg = pushed ? RGB(204, 219, 240)
                        : hovered ? RGB(214, 227, 245)
                        : portal_active ? RGB(216, 229, 246) : kColBadgeFill;
            COLORREF bd = (hovered || pushed || portal_active) ? kColAccent : kColBadgeEdge;
            gdip_round_rect(g, 0.75f, 0.75f, w, h, (float)g_app.S(7), gdip_col(bg), gdip_col(bd), 1.1f);
        } else {
            COLORREF bg = disabled ? RGB(238, 242, 248)
                        : pushed   ? RGB(212, 224, 240)
                        : hovered  ? RGB(226, 235, 247) : RGB(242, 246, 252);
            COLORREF bd = disabled ? RGB(206, 216, 228)
                        : (hovered || pushed) ? kColAccent : kColInputEdge;
            gdip_round_rect(g, 0.75f, 0.75f, w, h, (float)g_app.S(6), gdip_col(bg), gdip_col(bd), 1.1f);
        }
    }

    SetBkMode(dis->hDC, TRANSPARENT);
    COLORREF tcol = is_primary ? RGB(255, 255, 255)
                  : disabled ? kColTextMuted
                  : is_portal ? (portal_active ? kColAccentDark : kColBadgeText)
                  : kColTextMain;
    SetTextColor(dis->hDC, tcol);
    HGDIOBJ of = SelectObject(dis->hDC, (is_primary || portal_active) ? g_app.hFontBold : g_app.hFont);
    RECT tr = dis->rcItem;
    DrawTextW(dis->hDC, txt, -1, &tr, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
    SelectObject(dis->hDC, of);
}

// ============================================================================
// Custom GDI+ Segmented Pill Control (Workspace Tab Bar & Mode Pills)
// ============================================================================
struct SegPillData {
    std::vector<std::wstring> items;
    int sel = 0;
    int hover = -1;
    bool is_tab_bar = false;
    uint32_t disabled_mask = 0;
};

static void paint_seg_pill(HWND hw, HDC hdc, const SegPillData* sd) {
    RECT rc{};
    GetClientRect(hw, &rc);
    int w = rc.right - rc.left;
    int h = rc.bottom - rc.top;
    if (w <= 0 || h <= 0 || !sd || sd->items.empty()) return;

    bool is_tab = sd->is_tab_bar;
    FillRect(hdc, &rc, is_tab ? g_app.hBrCanvas : g_app.hBrCard);

    int n = (int)sd->items.size();
    bool enabled = IsWindowEnabled(hw) != FALSE;

    {
        Gdiplus::Graphics g(hdc);
        g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);

        float rad = (float)g_app.S(is_tab ? 10 : 7);
        COLORREF trayFill = is_tab ? RGB(216, 226, 240) : RGB(232, 238, 248);
        COLORREF trayEdge = is_tab ? RGB(184, 198, 218) : RGB(192, 205, 224);
        gdip_round_rect(g, 0.5f, 0.5f, (float)w - 1.5f, (float)h - 1.5f, rad,
                        gdip_col(trayFill), gdip_col(trayEdge), 1.1f);

        float pad = (float)g_app.S(is_tab ? 3 : 2);
        float innerW = (float)w - 2.0f * pad;
        float segW = innerW / (float)n;

        // Subtle dividers between unselected segments
        Gdiplus::Pen divPen(gdip_col(RGB(196, 209, 226), 170), 1.0f);
        for (int i = 1; i < n; ++i) {
            if (i == sd->sel || i - 1 == sd->sel) continue;
            float dx = pad + segW * (float)i;
            g.DrawLine(&divPen, dx, pad + 4.0f, dx, (float)h - pad - 4.0f);
        }

        if (sd->hover >= 0 && sd->hover < n && sd->hover != sd->sel && enabled &&
            !(sd->disabled_mask & (1u << sd->hover))) {
            float hx = pad + segW * (float)sd->hover;
            gdip_round_rect(g, hx + 0.5f, pad + 0.5f, segW - 1.0f, (float)h - 2.0f * pad - 1.0f,
                            rad - 2.0f, gdip_col(RGB(240, 245, 252)), Gdiplus::Color(0, 0, 0, 0), 0.0f);
        }

        if (sd->sel >= 0 && sd->sel < n) {
            float sx = pad + segW * (float)sd->sel;
            if (is_tab) {
                gdip_round_rect(g, sx, pad, segW, (float)h - 2.0f * pad, rad - 2.0f,
                                gdip_col(RGB(255, 255, 255)), gdip_col(kColAccent), 1.25f);
                float indW = std::min(segW * 0.42f, (float)g_app.S(56));
                gdip_round_rect(g, sx + 0.5f * (segW - indW), (float)h - pad - (float)g_app.S(3),
                                indW, (float)g_app.S(2), 1.0f,
                                gdip_col(kColAccent), Gdiplus::Color(0, 0, 0, 0), 0.0f);
            } else {
                COLORREF selBg = enabled ? kColAccent : RGB(166, 180, 198);
                COLORREF selBd = enabled ? kColAccentDark : RGB(154, 168, 186);
                gdip_round_rect(g, sx, pad, segW, (float)h - 2.0f * pad, rad - 1.5f,
                                gdip_col(selBg), gdip_col(selBd), 1.0f);
            }
        }
    }

    SetBkMode(hdc, TRANSPARENT);
    int padI = g_app.S(is_tab ? 3 : 2);
    int innerW = w - 2 * padI;
    for (int i = 0; i < n; ++i) {
        int x0 = padI + (innerW * i) / n;
        int x1 = padI + (innerW * (i + 1)) / n;
        RECT sr{x0, 0, x1, h};
        bool active = (i == sd->sel);
        bool seg_dis = !enabled || ((sd->disabled_mask & (1u << i)) != 0);
        bool has_dot = (sd->items[(size_t)i].find(L"• ") == 0);
        HGDIOBJ of = SelectObject(hdc, (active || is_tab) ? (active ? g_app.hFontCardHdr : g_app.hFontBold)
                                                          : (active ? g_app.hFontBold : g_app.hFont));
        if (!is_tab) SelectObject(hdc, active ? g_app.hFontBold : g_app.hFont);

        if (seg_dis) {
            SetTextColor(hdc, active ? RGB(255, 255, 255) : kColTextMuted);
        } else if (is_tab) {
            SetTextColor(hdc, active ? kColAccentDark : (has_dot ? kColBadgeText : kColTextMain));
        } else {
            SetTextColor(hdc, active ? RGB(255, 255, 255) : kColTextMain);
        }
        DrawTextW(hdc, sd->items[(size_t)i].c_str(), -1, &sr, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
        SelectObject(hdc, of);
    }
}

static LRESULT CALLBACK NijiSegPillWndProc(HWND hw, UINT msg, WPARAM wParam, LPARAM lParam) {
    auto* sd = reinterpret_cast<SegPillData*>(GetWindowLongPtrW(hw, GWLP_USERDATA));
    switch (msg) {
        case WM_ERASEBKGND: return 1;
        case WM_MOUSEMOVE: {
            if (!sd || sd->items.empty()) break;
            RECT rc{};
            GetClientRect(hw, &rc);
            int x = GET_X_LPARAM(lParam);
            int n = (int)sd->items.size();
            int idx = std::max(0, std::min(n - 1, (x * n) / std::max(1, (int)rc.right)));
            if (sd->hover != idx) {
                sd->hover = idx;
                TRACKMOUSEEVENT tme{};
                tme.cbSize = sizeof(tme);
                tme.dwFlags = TME_LEAVE;
                tme.hwndTrack = hw;
                TrackMouseEvent(&tme);
                InvalidateRect(hw, nullptr, FALSE);
            }
            return 0;
        }
        case WM_MOUSELEAVE:
            if (sd && sd->hover != -1) {
                sd->hover = -1;
                InvalidateRect(hw, nullptr, FALSE);
            }
            return 0;
        case WM_LBUTTONDOWN: {
            if (!sd || sd->items.empty() || !IsWindowEnabled(hw)) return 0;
            RECT rc{};
            GetClientRect(hw, &rc);
            int x = GET_X_LPARAM(lParam);
            int n = (int)sd->items.size();
            int idx = std::max(0, std::min(n - 1, (x * n) / std::max(1, (int)rc.right)));
            if (sd->disabled_mask & (1u << idx)) return 0;
            if (sd->sel != idx) {
                sd->sel = idx;
                InvalidateRect(hw, nullptr, FALSE);
                SendMessageW(GetParent(hw), WM_COMMAND,
                             MAKEWPARAM(GetDlgCtrlID(hw), CBN_SELCHANGE),
                             reinterpret_cast<LPARAM>(hw));
            }
            return 0;
        }
        case WM_ENABLE:
            InvalidateRect(hw, nullptr, FALSE);
            return 0;
        case WM_PAINT: {
            PAINTSTRUCT ps{};
            HDC hdc = BeginPaint(hw, &ps);
            RECT rc{};
            GetClientRect(hw, &rc);
            HDC mem = CreateCompatibleDC(hdc);
            HBITMAP bmp = CreateCompatibleBitmap(hdc, std::max(1L, rc.right), std::max(1L, rc.bottom));
            HGDIOBJ old = SelectObject(mem, bmp);
            paint_seg_pill(hw, mem, sd);
            BitBlt(hdc, 0, 0, rc.right, rc.bottom, mem, 0, 0, SRCCOPY);
            SelectObject(mem, old);
            DeleteObject(bmp);
            DeleteDC(mem);
            EndPaint(hw, &ps);
            return 0;
        }
        case WM_PRINT:
        case WM_PRINTCLIENT:
            paint_seg_pill(hw, reinterpret_cast<HDC>(wParam), sd);
            return 0;
        case WM_NCDESTROY:
            delete sd;
            SetWindowLongPtrW(hw, GWLP_USERDATA, 0);
            break;
    }
    return DefWindowProcW(hw, msg, wParam, lParam);
}

static void seg_pill_set_sel(HWND hw, int sel) {
    auto* sd = reinterpret_cast<SegPillData*>(GetWindowLongPtrW(hw, GWLP_USERDATA));
    if (!sd) return;
    sd->sel = sel;
    InvalidateRect(hw, nullptr, FALSE);
}

static int seg_pill_get_sel(HWND hw) {
    auto* sd = reinterpret_cast<SegPillData*>(GetWindowLongPtrW(hw, GWLP_USERDATA));
    return sd ? sd->sel : 0;
}

static void seg_pill_set_item_text(HWND hw, int idx, const std::wstring& txt) {
    auto* sd = reinterpret_cast<SegPillData*>(GetWindowLongPtrW(hw, GWLP_USERDATA));
    if (!sd || idx < 0 || (size_t)idx >= sd->items.size()) return;
    if (sd->items[(size_t)idx] != txt) {
        sd->items[(size_t)idx] = txt;
        InvalidateRect(hw, nullptr, FALSE);
    }
}

static void seg_pill_set_items(HWND hw, const std::vector<std::wstring>& items) {
    auto* sd = reinterpret_cast<SegPillData*>(GetWindowLongPtrW(hw, GWLP_USERDATA));
    if (!sd || items.size() != sd->items.size()) return;
    sd->items = items;
    InvalidateRect(hw, nullptr, FALSE);
}

static void seg_pill_set_item_enabled(HWND hw, int idx, bool en) {
    auto* sd = reinterpret_cast<SegPillData*>(GetWindowLongPtrW(hw, GWLP_USERDATA));
    if (!sd || idx < 0 || idx >= 31) return;
    uint32_t bit = (1u << idx);
    uint32_t next = en ? (sd->disabled_mask & ~bit) : (sd->disabled_mask | bit);
    if (next != sd->disabled_mask) {
        sd->disabled_mask = next;
        InvalidateRect(hw, nullptr, FALSE);
    }
}

// ============================================================================
// Custom GDI+ Antialiased Aqua Slider (Two-Way Linked to Numeric Edit)
// ============================================================================
struct AquaSliderData {
    double min_v = 0.0;
    double max_v = 1.0;
    double val = 1.0;
    int decimals = 2; // 0 = int, 1 = %.1f, 2 = %.2f
    HWND linked_edt = nullptr;
    bool dragging = false;
    bool hovered = false;
};

static void format_slider_val(const AquaSliderData* ad, wchar_t* buf, size_t cap) {
    if (ad->decimals <= 0) std::swprintf(buf, cap, L"%d", (int)std::llround(ad->val));
    else if (ad->decimals == 1) std::swprintf(buf, cap, L"%.1f", ad->val);
    else std::swprintf(buf, cap, L"%.2f", ad->val);
}

static void paint_aqua_slider(HWND hw, HDC hdc, const AquaSliderData* ad) {
    RECT rc{};
    GetClientRect(hw, &rc);
    FillRect(hdc, &rc, g_app.hBrCard);
    if (!ad || rc.right <= 0) return;

    bool enabled = IsWindowEnabled(hw) != FALSE;
    float pad = (float)g_app.S(8);
    float tw = std::max(4.0f, (float)rc.right - 2.0f * pad);
    float cy = 0.5f * (float)rc.bottom;
    float th = (float)g_app.S(5);
    double span = std::max(1e-6, ad->max_v - ad->min_v);
    float t = (float)std::max(0.0, std::min(1.0, (ad->val - ad->min_v) / span));
    float kx = pad + t * tw;

    Gdiplus::Graphics g(hdc);
    g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);

    COLORREF trkBg = enabled ? RGB(218, 228, 242) : RGB(232, 237, 244);
    COLORREF trkBd = enabled ? RGB(192, 206, 224) : RGB(210, 218, 230);
    gdip_round_rect(g, pad, cy - 0.5f * th, tw, th, 0.5f * th, gdip_col(trkBg), gdip_col(trkBd), 1.0f);

    if (ad->min_v < 0.0 && ad->max_v > 0.0) {
        float zeroT = (float)((0.0 - ad->min_v) / span);
        float zx = pad + zeroT * tw;
        float x0 = std::min(zx, kx);
        float fw = std::fabs(kx - zx);
        if (fw > 1.0f) {
            COLORREF fillC = enabled ? kColAccent : RGB(174, 186, 204);
            gdip_round_rect(g, x0, cy - 0.5f * th, fw, th, 0.5f * th,
                            gdip_col(fillC), Gdiplus::Color(0, 0, 0, 0), 0.0f);
        }
        Gdiplus::Pen zPen(gdip_col(enabled ? kColAccentDark : RGB(174, 186, 204)), 1.2f);
        g.DrawLine(&zPen, zx, cy - 0.5f * th - 2.0f, zx, cy + 0.5f * th + 2.0f);
    } else if (kx > pad + 1.0f) {
        COLORREF fillC = enabled ? kColAccent : RGB(174, 186, 204);
        gdip_round_rect(g, pad, cy - 0.5f * th, kx - pad, th, 0.5f * th,
                        gdip_col(fillC), Gdiplus::Color(0, 0, 0, 0), 0.0f);
    }

    float kr = (float)g_app.S((ad->dragging || ad->hovered) ? 7 : 6);
    COLORREF kFill = !enabled ? RGB(236, 240, 246) : RGB(255, 255, 255);
    COLORREF kEdge = !enabled ? RGB(180, 192, 208) : (ad->dragging ? kColAccentDark : kColAccent);
    Gdiplus::SolidBrush kBr(gdip_col(kFill));
    Gdiplus::Pen kPen(gdip_col(kEdge), enabled ? 2.0f : 1.3f);
    g.FillEllipse(&kBr, kx - kr, cy - kr, 2.0f * kr, 2.0f * kr);
    g.DrawEllipse(&kPen, kx - kr, cy - kr, 2.0f * kr, 2.0f * kr);
}

static void slider_update_from_mouse(HWND hw, AquaSliderData* ad, int mouse_x) {
    RECT rc{};
    GetClientRect(hw, &rc);
    float pad = (float)g_app.S(8);
    float tw = std::max(4.0f, (float)rc.right - 2.0f * pad);
    double t = std::max(0.0, std::min(1.0, ((double)mouse_x - (double)pad) / (double)tw));
    double raw = ad->min_v + t * (ad->max_v - ad->min_v);
    if (ad->decimals <= 0) raw = std::round(raw);
    else if (ad->decimals == 1) raw = std::round(raw * 10.0) / 10.0;
    else raw = std::round(raw * 100.0) / 100.0;
    ad->val = std::max(ad->min_v, std::min(ad->max_v, raw));
    InvalidateRect(hw, nullptr, FALSE);
    if (ad->linked_edt) {
        g_app.suppress_sync = true;
        wchar_t buf[32] = {0};
        format_slider_val(ad, buf, 32);
        SetWindowTextW(ad->linked_edt, buf);
        g_app.suppress_sync = false;
    }
}

static LRESULT CALLBACK NijiSliderWndProc(HWND hw, UINT msg, WPARAM wParam, LPARAM lParam) {
    auto* ad = reinterpret_cast<AquaSliderData*>(GetWindowLongPtrW(hw, GWLP_USERDATA));
    switch (msg) {
        case WM_ERASEBKGND: return 1;
        case WM_MOUSEMOVE:
            if (!ad) break;
            if (!ad->hovered) {
                ad->hovered = true;
                TRACKMOUSEEVENT tme{};
                tme.cbSize = sizeof(tme);
                tme.dwFlags = TME_LEAVE;
                tme.hwndTrack = hw;
                TrackMouseEvent(&tme);
                InvalidateRect(hw, nullptr, FALSE);
            }
            if (ad->dragging && IsWindowEnabled(hw)) {
                slider_update_from_mouse(hw, ad, GET_X_LPARAM(lParam));
            }
            return 0;
        case WM_MOUSELEAVE:
            if (ad && ad->hovered) {
                ad->hovered = false;
                InvalidateRect(hw, nullptr, FALSE);
            }
            return 0;
        case WM_LBUTTONDOWN:
            if (ad && IsWindowEnabled(hw)) {
                ad->dragging = true;
                SetCapture(hw);
                slider_update_from_mouse(hw, ad, GET_X_LPARAM(lParam));
            }
            return 0;
        case WM_LBUTTONUP:
            if (ad && ad->dragging) {
                ad->dragging = false;
                ReleaseCapture();
                InvalidateRect(hw, nullptr, FALSE);
                save_ui_prefs();
            }
            return 0;
        case WM_ENABLE:
            InvalidateRect(hw, nullptr, FALSE);
            return 0;
        case WM_PAINT: {
            PAINTSTRUCT ps{};
            HDC hdc = BeginPaint(hw, &ps);
            RECT rc{};
            GetClientRect(hw, &rc);
            HDC mem = CreateCompatibleDC(hdc);
            HBITMAP bmp = CreateCompatibleBitmap(hdc, std::max(1L, rc.right), std::max(1L, rc.bottom));
            HGDIOBJ old = SelectObject(mem, bmp);
            paint_aqua_slider(hw, mem, ad);
            BitBlt(hdc, 0, 0, rc.right, rc.bottom, mem, 0, 0, SRCCOPY);
            SelectObject(mem, old);
            DeleteObject(bmp);
            DeleteDC(mem);
            EndPaint(hw, &ps);
            return 0;
        }
        case WM_PRINT:
        case WM_PRINTCLIENT:
            paint_aqua_slider(hw, reinterpret_cast<HDC>(wParam), ad);
            return 0;
        case WM_NCDESTROY:
            delete ad;
            SetWindowLongPtrW(hw, GWLP_USERDATA, 0);
            break;
    }
    return DefWindowProcW(hw, msg, wParam, lParam);
}

static void sync_slider_from_edit(HWND hSld, HWND hEdt) {
    if (!hSld || !hEdt) return;
    auto* ad = reinterpret_cast<AquaSliderData*>(GetWindowLongPtrW(hSld, GWLP_USERDATA));
    if (!ad) return;
    std::wstring txt = trim_ws(get_wnd_text(hEdt));
    if (txt.empty()) return;
    double v = _wtof(txt.c_str());
    ad->val = std::max(ad->min_v, std::min(ad->max_v, v));
    InvalidateRect(hSld, nullptr, FALSE);
}

static void set_tuned_pair(HWND hSld, HWND hEdt, const wchar_t* txt) {
    if (hEdt) {
        g_app.suppress_sync = true;
        SetWindowTextW(hEdt, txt);
        g_app.suppress_sync = false;
    }
    sync_slider_from_edit(hSld, hEdt);
}

// ============================================================================
// Left Dock Custom Control 1: Burst Queue Master View (Empty Drop-Well + Cards)
// ============================================================================
static BurstGroupItem* current_group_ptr() {
    if (g_app.selected_group >= 0 && (size_t)g_app.selected_group < g_app.groups.size()) {
        return &g_app.groups[(size_t)g_app.selected_group];
    }
    return nullptr;
}

static void normalize_selection_masks() {
    size_t ng = g_app.groups.size();
    if (ng == 0) {
        g_app.group_sel.clear();
        g_app.selected_group = 0;
        g_app.group_anchor = 0;
        g_app.frame_sel.clear();
        g_app.frame_sel_idx = 0;
        g_app.frame_anchor = 0;
        return;
    }
    if (g_app.selected_group < 0 || (size_t)g_app.selected_group >= ng) {
        g_app.selected_group = (int)ng - 1;
    }
    if (g_app.group_anchor < 0 || (size_t)g_app.group_anchor >= ng) {
        g_app.group_anchor = g_app.selected_group;
    }
    if (g_app.group_sel.size() != ng) {
        g_app.group_sel.assign(ng, 0);
        g_app.group_sel[(size_t)g_app.selected_group] = 1;
    } else {
        bool any = false;
        for (uint8_t v : g_app.group_sel) if (v) { any = true; break; }
        if (!any) g_app.group_sel[(size_t)g_app.selected_group] = 1;
    }

    BurstGroupItem* grp = current_group_ptr();
    size_t nf = grp ? grp->frames.size() : 0;
    if (nf == 0) {
        g_app.frame_sel.clear();
        g_app.frame_sel_idx = 0;
        g_app.frame_anchor = 0;
    } else {
        if (g_app.frame_sel_idx < 0 || (size_t)g_app.frame_sel_idx >= nf) {
            g_app.frame_sel_idx = 0;
        }
        if (g_app.frame_anchor < 0 || (size_t)g_app.frame_anchor >= nf) {
            g_app.frame_anchor = g_app.frame_sel_idx;
        }
        if (g_app.frame_sel.size() != nf) {
            g_app.frame_sel.assign(nf, 0);
            g_app.frame_sel[(size_t)g_app.frame_sel_idx] = 1;
        } else {
            bool any_f = false;
            for (uint8_t v : g_app.frame_sel) if (v) { any_f = true; break; }
            if (!any_f) g_app.frame_sel[(size_t)g_app.frame_sel_idx] = 1;
        }
    }
}

static bool is_group_selected(int idx) {
    if (idx < 0 || (size_t)idx >= g_app.groups.size()) return false;
    if (g_app.group_sel.size() == g_app.groups.size()) return g_app.group_sel[(size_t)idx] != 0;
    return idx == g_app.selected_group;
}

static std::vector<int> selected_group_indices() {
    normalize_selection_masks();
    std::vector<int> res;
    for (size_t i = 0; i < g_app.group_sel.size(); ++i) {
        if (g_app.group_sel[i]) res.push_back((int)i);
    }
    return res;
}

static void select_single_group(int idx, bool reset_frames = true) {
    if (g_app.groups.empty()) return;
    idx = std::clamp(idx, 0, (int)g_app.groups.size() - 1);
    if (g_app.selected_group != idx) g_app.optics = OpticsTelemetry{};
    g_app.selected_group = idx;
    g_app.group_anchor = idx;
    g_app.group_sel.assign(g_app.groups.size(), 0);
    g_app.group_sel[(size_t)idx] = 1;
    if (reset_frames) {
        g_app.frame_sel_idx = 0;
        g_app.frame_anchor = 0;
        g_app.frame_scroll_y = 0;
        g_app.frame_sel.clear();
        normalize_selection_masks();
    }
}

static bool is_frame_selected(int idx) {
    BurstGroupItem* grp = current_group_ptr();
    if (!grp || idx < 0 || (size_t)idx >= grp->frames.size()) return false;
    if (g_app.frame_sel.size() == grp->frames.size()) return g_app.frame_sel[(size_t)idx] != 0;
    return idx == g_app.frame_sel_idx;
}

static std::vector<int> selected_frame_indices() {
    normalize_selection_masks();
    std::vector<int> res;
    for (size_t i = 0; i < g_app.frame_sel.size(); ++i) {
        if (g_app.frame_sel[i]) res.push_back((int)i);
    }
    return res;
}

static void select_single_frame(int idx) {
    BurstGroupItem* grp = current_group_ptr();
    if (!grp || grp->frames.empty()) return;
    idx = std::clamp(idx, 0, (int)grp->frames.size() - 1);
    g_app.frame_sel_idx = idx;
    g_app.frame_anchor = idx;
    g_app.frame_sel.assign(grp->frames.size(), 0);
    g_app.frame_sel[(size_t)idx] = 1;
}

static void recalculate_group_item(BurstGroupItem& grp, int ordinal_1based) {
    grp.paths.clear();
    int valid_cnt = 0;
    double max_acu = -1.0;
    int best_idx = 1;
    double max_shift = 0.0;
    for (size_t i = 0; i < grp.frames.size(); ++i) {
        auto& f = grp.frames[i];
        f.idx_1based = (int)i + 1;
        f.is_crown = false;
        if (!f.full_path.empty()) grp.paths.push_back(f.full_path);
        bool is_dark = (f.flags.find(L"DARK") != std::wstring::npos);
        if (f.included && !is_dark) ++valid_cnt;
        if (f.acutance > max_acu) { max_acu = f.acutance; best_idx = f.idx_1based; }
        max_shift = std::max(max_shift, f.shift_px);
    }
    grp.valid_frame_count = valid_cnt;
    grp.best_anchor_1based = best_idx;
    if (!grp.is_stray_archive && best_idx >= 1 && (size_t)best_idx <= grp.frames.size()) {
        grp.frames[(size_t)best_idx - 1].is_crown = true;
    }
    if (grp.locked_anchor_1based > (int)grp.frames.size()) grp.locked_anchor_1based = 0;
    if (grp.max_drift_px <= 0.0) grp.max_drift_px = max_shift;

    if (!grp.frames.empty()) {
        std::wstring s0 = stem_of_w(grp.frames.front().file);
        std::wstring s1 = stem_of_w(grp.frames.back().file);
        grp.span_label = (grp.frames.size() > 1 && s0 != s1) ? (s0 + L"–" + s1) : s0;
    } else {
        grp.span_label.clear();
    }

    const bool en = heraw_i18n::I18nManager::instance().is_english();
    int min_req = min_valid_frames_win();
    wchar_t tbuf[128] = {0};
    if (grp.is_stray_archive) {
        std::swprintf(tbuf, 128, en ? L"Single Shots · %d (Skipped)" : L"未成组单拍 · %d 张 (已跳过)", (int)grp.frames.size());
        grp.sensor_badge = en ? L"Stray" : L"散片";
        grp.meta_line = heraw_i18n::tr_w("qcell.stray_model");
    } else if (valid_cnt < min_req) {
        std::swprintf(tbuf, 128, en ? L"Grp %02d · %df (Short)" : L"组 %02d · %d 张 (少帧)", ordinal_1based, (int)grp.frames.size());
    } else {
        std::swprintf(tbuf, 128, en ? L"Grp %02d · %df" : L"组 %02d · %d 张", ordinal_1based, (int)grp.frames.size());
    }
    grp.title = tbuf;

    if (!grp.is_stray_archive && grp.has_telemetry) {
        wchar_t mbuf[320] = {0};
        wchar_t fbuf[32] = {0};
        if (grp.focus_is_infinity || grp.focus_distance_m >= 999999.0) {
            std::swprintf(fbuf, 32, L" \x00B7 \x221E");
        } else if (grp.focus_distance_m > 0.0) {
            const wchar_t* ffmt = (grp.focus_distance_m < 10.0) ? L" \x00B7 %.2fm"
                                : ((grp.focus_distance_m < 100.0) ? L" \x00B7 %.1fm" : L" \x00B7 %.0fm");
            std::swprintf(fbuf, 32, ffmt, grp.focus_distance_m);
        }
        if (!grp.camera_model.empty()) {
            std::swprintf(mbuf, 320, en ? L"%ls · %ls · Quads %d/4 · Score %d%ls" : L"%ls · %ls · 象限 %d/4 · 潜力 %d%ls",
                          utf8_to_wide(grp.camera_model).c_str(), scene_code_to_cn(grp.scene_code).c_str(),
                          grp.quad_coverage, grp.sr_score, fbuf);
        } else {
            std::swprintf(mbuf, 320, en ? L"%ls · Quads %d/4 · Score %d/100%ls" : L"%ls · 四象限 %d/4 · 超分潜力 %d/100%ls",
                          scene_code_to_cn(grp.scene_code).c_str(), grp.quad_coverage, grp.sr_score, fbuf);
        }
        grp.meta_line = mbuf;
        std::swprintf(mbuf, 320,
                      en ? L"Max drift: %.1f px · Quads: %d/4 · %ls" : L"最大漂移: %.1f px · 四象限: %d/4 · %ls",
                      grp.max_drift_px, grp.quad_coverage,
                      grp.shutter_shock
                          ? (en ? L"Frame #1 shutter shock flagged" : L"首帧检出快门微震 (建议跳过或避开#1锚定)")
                          : (en ? L"Shutter & OIS trajectory steady" : L"快门/防抖轨迹平稳"));
        grp.shutter_summary = mbuf;
    }
}

static void sync_inputs_and_ordinals_from_groups() {
    g_app.inputs.clear();
    for (size_t i = 0; i < g_app.groups.size(); ++i) {
        recalculate_group_item(g_app.groups[i], (int)i + 1);
        for (const auto& p : g_app.groups[i].paths) {
            if (!p.empty() && std::find(g_app.inputs.begin(), g_app.inputs.end(), p) == g_app.inputs.end()) {
                g_app.inputs.push_back(p);
            }
        }
    }
}

static bool has_stray_groups_win() {
    int min_req = min_valid_frames_win();
    for (const auto& g : g_app.groups) {
        if (g.is_stray_archive || g.valid_frame_count < min_req) return true;
    }
    return false;
}

static double quality_rank_for_group_win(const BurstGroupItem& g) {
    if (g.is_stray_archive) return -1e9;
    double max_acu = 0.0;
    for (const auto& f : g.frames) max_acu = std::max(max_acu, f.acutance);
    int min_req = min_valid_frames_win();
    return (g.valid_frame_count >= min_req ? 100000.0 : 0.0) + (double)g.sr_score * 100.0 + (double)g.valid_frame_count * 5.0 + max_acu;
}

static void select_only_sharpest_group_win() {
    if (g_app.groups.empty()) return;
    size_t best_i = 0;
    double best_r = -1e12;
    for (size_t i = 0; i < g_app.groups.size(); ++i) {
        double r = quality_rank_for_group_win(g_app.groups[i]);
        if (r > best_r) { best_r = r; best_i = i; }
    }
    for (size_t i = 0; i < g_app.groups.size(); ++i) {
        g_app.groups[i].checked = (i == best_i && !g_app.groups[i].is_stray_archive && g_app.groups[i].valid_frame_count >= 1);
    }
}

static void repaint_left_dock_views() {
    if (g_app.hBurstView) InvalidateRect(g_app.hBurstView, nullptr, FALSE);
    if (g_app.hFrameView) InvalidateRect(g_app.hFrameView, nullptr, FALSE);
    if (g_app.hOpticsCanvas) InvalidateRect(g_app.hOpticsCanvas, nullptr, FALSE);
    if (g_app.hParallaxMeter) InvalidateRect(g_app.hParallaxMeter, nullptr, FALSE);
}

static void batch_toggle_selected_groups_checked() {
    auto sel = selected_group_indices();
    if (sel.empty()) return;
    bool all_on = true;
    for (int idx : sel) if (!g_app.groups[(size_t)idx].checked) { all_on = false; break; }
    bool target = !all_on;
    for (int idx : sel) g_app.groups[(size_t)idx].checked = target;
    sync_cross_tab_ui();
    repaint_left_dock_views();
}

static void remove_selected_groups_win() {
    if (g_app.running.load() || g_app.plan_scanning.load() || g_app.groups.empty()) return;
    auto sel = selected_group_indices();
    if (sel.empty()) return;
    int first_rem = sel.front();
    for (int i = (int)sel.size() - 1; i >= 0; --i) {
        int idx = sel[(size_t)i];
        if (idx >= 0 && (size_t)idx < g_app.groups.size()) {
            g_app.groups.erase(g_app.groups.begin() + idx);
        }
    }
    sync_inputs_and_ordinals_from_groups();
    if (!g_app.groups.empty()) {
        select_single_group(std::min(first_rem, (int)g_app.groups.size() - 1));
    } else {
        normalize_selection_masks();
    }
    sync_cross_tab_ui();
    repaint_left_dock_views();
}

static void merge_selected_groups_win() {
    if (g_app.running.load() || g_app.plan_scanning.load()) return;
    auto sel = selected_group_indices();
    if (sel.size() < 2) return;
    int anchor_idx = sel.front();
    auto& merged = g_app.groups[(size_t)anchor_idx];
    merged.is_stray_archive = false;
    merged.parallax_measured = false;
    merged.depth_known = false;
    merged.depth_observable = false;
    merged.field_observability.clear();
    if (merged.scene_code == "single_shot") merged.scene_code = "handheld_standard";
    if (merged.sensor_badge == L"散片" || merged.sensor_badge == L"Stray") merged.sensor_badge = L"Bayer";
    merged.meta_line = heraw_i18n::tr_w("qcell.raw_seq_model");

    for (size_t k = 1; k < sel.size(); ++k) {
        const auto& other = g_app.groups[(size_t)sel[k]];
        for (const auto& f : other.frames) {
            bool dup = !f.full_path.empty() &&
                       std::any_of(merged.frames.begin(), merged.frames.end(),
                                   [&](const FrameDiagItem& ex) { return ex.full_path == f.full_path; });
            if (!dup) merged.frames.push_back(f);
        }
        merged.max_drift_px = std::max(merged.max_drift_px, other.max_drift_px);
        merged.max_parallax_2x_px = std::max(merged.max_parallax_2x_px, other.max_parallax_2x_px);
        merged.quad_coverage = std::max(merged.quad_coverage, other.quad_coverage);
        merged.sr_score = std::max(merged.sr_score, other.sr_score);
    }
    for (int k = (int)sel.size() - 1; k >= 1; --k) {
        g_app.groups.erase(g_app.groups.begin() + sel[(size_t)k]);
    }
    sync_inputs_and_ordinals_from_groups();
    auto& target_grp = g_app.groups[(size_t)anchor_idx];
    int min_req = min_valid_frames_win();
    target_grp.checked = (target_grp.valid_frame_count >= min_req && target_grp.paths.size() == target_grp.frames.size());
    select_single_group(anchor_idx);
    set_status_ui(heraw_i18n::trf_w("st.merged_fmt", (int)sel.size(), anchor_idx + 1, (int)target_grp.frames.size()));
    sync_cross_tab_ui();
    repaint_left_dock_views();
}

static void split_selected_frames_win() {
    if (g_app.running.load() || g_app.plan_scanning.load()) return;
    BurstGroupItem* src = current_group_ptr();
    auto fsel = selected_frame_indices();
    if (!src || fsel.empty() || fsel.size() >= src->frames.size()) return;
    int src_idx = g_app.selected_group;

    BurstGroupItem dst = *src;
    dst.is_stray_archive = false;
    dst.parallax_measured = false;
    dst.depth_known = false;
    dst.depth_observable = false;
    dst.field_observability.clear();
    if (dst.scene_code == "single_shot") dst.scene_code = "handheld_standard";
    if (dst.sensor_badge == L"散片" || dst.sensor_badge == L"Stray") dst.sensor_badge = L"Bayer";
    dst.meta_line = heraw_i18n::tr_w("qcell.raw_seq_model");
    dst.frames.clear();
    dst.paths.clear();
    dst.locked_anchor_1based = 0;

    for (int fi : fsel) dst.frames.push_back(src->frames[(size_t)fi]);
    for (int k = (int)fsel.size() - 1; k >= 0; --k) {
        src->frames.erase(src->frames.begin() + fsel[(size_t)k]);
    }
    src->locked_anchor_1based = 0;
    src->parallax_measured = false;
    src->depth_known = false;
    src->depth_observable = false;
    src->field_observability.clear();

    int ins_pos = src_idx + 1;
    g_app.groups.insert(g_app.groups.begin() + ins_pos, std::move(dst));
    sync_inputs_and_ordinals_from_groups();
    int min_req = min_valid_frames_win();
    if (!g_app.groups[(size_t)src_idx].is_stray_archive) {
        g_app.groups[(size_t)src_idx].checked =
            (g_app.groups[(size_t)src_idx].valid_frame_count >= min_req &&
             g_app.groups[(size_t)src_idx].paths.size() == g_app.groups[(size_t)src_idx].frames.size());
    }
    g_app.groups[(size_t)ins_pos].checked =
        (g_app.groups[(size_t)ins_pos].valid_frame_count >= min_req &&
         g_app.groups[(size_t)ins_pos].paths.size() == g_app.groups[(size_t)ins_pos].frames.size());
    select_single_group(ins_pos);
    set_status_ui(heraw_i18n::trf_w("st.split_fmt", (int)fsel.size(), ins_pos + 1));
    sync_cross_tab_ui();
    repaint_left_dock_views();
}

static void clean_stray_groups_win() {
    if (g_app.running.load() || g_app.plan_scanning.load() || g_app.groups.empty()) return;
    int min_req = min_valid_frames_win();
    size_t rem_groups = 0, rem_frames = 0;
    for (int i = (int)g_app.groups.size() - 1; i >= 0; --i) {
        const auto& g = g_app.groups[(size_t)i];
        if (g.is_stray_archive || g.valid_frame_count < min_req) {
            ++rem_groups;
            rem_frames += g.frames.size();
            g_app.groups.erase(g_app.groups.begin() + i);
        }
    }
    if (rem_groups == 0) return;
    sync_inputs_and_ordinals_from_groups();
    if (!g_app.groups.empty()) select_single_group(0);
    else normalize_selection_masks();
    set_status_ui(heraw_i18n::trf_w("st.cleaned_fmt", (int)rem_groups, (int)rem_frames));
    sync_cross_tab_ui();
    repaint_left_dock_views();
}

static void batch_toggle_selected_frames_included() {
    BurstGroupItem* grp = current_group_ptr();
    auto fsel = selected_frame_indices();
    if (!grp || fsel.empty()) return;
    bool all_inc = true;
    for (int fi : fsel) if (!grp->frames[(size_t)fi].included) { all_inc = false; break; }
    bool target = !all_inc;
    for (int fi : fsel) {
        auto& fd = grp->frames[(size_t)fi];
        fd.included = target;
        if (!target && grp->locked_anchor_1based == fd.idx_1based) grp->locked_anchor_1based = 0;
    }
    grp->parallax_measured = false;
    grp->depth_known = false;
    grp->depth_observable = false;
    grp->field_observability.clear();
    recalculate_group_item(*grp, g_app.selected_group + 1);
    sync_cross_tab_ui();
    repaint_left_dock_views();
}

static void apply_group_recommendation(int gidx) {
    if (gidx < 0 || (size_t)gidx >= g_app.groups.size()) return;
    const auto& grp = g_app.groups[(size_t)gidx];
    int pIdx = preset_code_to_idx(grp.rec_preset);
    apply_preset_to_controls(pIdx);

    wchar_t nb[32] = {0};
    std::swprintf(nb, 32, L"%.2f", grp.rec_denoise);
    set_tuned_pair(g_app.hSldDenoise, g_app.hEdtDenoise, nb);
    std::swprintf(nb, 32, L"%.2f", grp.rec_deghost);
    set_tuned_pair(g_app.hSldDeghost, g_app.hEdtDeghost, nb);
    std::swprintf(nb, 32, L"%d", grp.rec_psf_iters);
    set_tuned_pair(g_app.hSldPsfIters, g_app.hEdtPsfIters, nb);
    if (grp.rec_anchor == "middle") g_app.motion_mode_idx = 2;

    sync_cross_tab_ui();
    save_ui_prefs();
    InvalidateRect(g_app.hBurstView, nullptr, FALSE);
    const bool en = heraw_i18n::I18nManager::instance().is_english();
    set_status_ui((en ? L"Applied recommended preset: " : L"已套用推荐配置：预设【") +
                  preset_code_to_cn(grp.rec_preset) + (en ? L"" : L"】"));
}

static void trigger_intake_action_win() {
    if (g_app.groups.empty()) return;
    int min_req = min_valid_frames_win();
    const wchar_t* curPresetCode = kPresetCodes[std::max(0, std::min(3, g_app.preset_idx))];
    size_t valid_groups = 0, checked_cnt = 0;
    int rx_idx = -1;
    for (size_t i = 0; i < g_app.groups.size(); ++i) {
        const auto& g = g_app.groups[i];
        if (!g.is_stray_archive && g.valid_frame_count >= min_req) ++valid_groups;
        if (g.checked) {
            ++checked_cnt;
            if (rx_idx < 0 && !g.is_stray_archive && g.has_telemetry && !g.rec_preset.empty() &&
                g.rec_preset != "photo" && utf8_to_wide(g.rec_preset) != curPresetCode) {
                rx_idx = (int)i;
            }
        }
    }
    if (checked_cnt == 0) {
        if (valid_groups > 0) {
            for (auto& g : g_app.groups) g.checked = (!g.is_stray_archive && g.valid_frame_count >= min_req);
        } else {
            select_only_sharpest_group_win();
        }
    } else if (rx_idx >= 0) {
        apply_group_recommendation(rx_idx);
        return;
    } else if (valid_groups >= 2 && checked_cnt >= 2) {
        select_only_sharpest_group_win();
    } else if (valid_groups >= 2) {
        for (auto& g : g_app.groups) g.checked = (!g.is_stray_archive && g.valid_frame_count >= min_req);
    }
    sync_cross_tab_ui();
    repaint_left_dock_views();
}

static void paint_burst_queue(HWND hw, HDC hdc) {
    normalize_selection_masks();
    RECT rc{};
    GetClientRect(hw, &rc);
    int w = rc.right - rc.left;
    int h = rc.bottom - rc.top;
    FillRect(hdc, &rc, g_app.hBrCard);

    Gdiplus::Graphics g(hdc);
    g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);

    if (g_app.groups.empty() && g_app.inputs.empty()) {
        bool hov = (g_app.burst_hover_idx == -2);
        COLORREF bg = hov ? RGB(236, 243, 252) : RGB(255, 255, 255);
        COLORREF bd = hov ? kColAccent : kColBadgeEdge;
        gdip_round_rect(g, 1.0f, 1.0f, (float)w - 2.5f, (float)h - 2.5f, (float)g_app.S(9),
                        gdip_col(bg), gdip_col(bd), 1.35f, true);

        float cr = (float)g_app.S(18);
        float cx = 0.5f * (float)w;
        float cy = 0.5f * (float)h - (float)g_app.S(22);
        Gdiplus::SolidBrush cbr(gdip_col(hov ? kColAccent : kColBadgeFill));
        Gdiplus::Pen cpen(gdip_col(hov ? kColAccentDark : kColBadgeEdge), 1.2f);
        g.FillEllipse(&cbr, cx - cr, cy - cr, 2.0f * cr, 2.0f * cr);
        g.DrawEllipse(&cpen, cx - cr, cy - cr, 2.0f * cr, 2.0f * cr);

        Gdiplus::Pen ppen(gdip_col(hov ? RGB(255, 255, 255) : kColAccentDark), 2.0f);
        ppen.SetStartCap(Gdiplus::LineCapRound);
        ppen.SetEndCap(Gdiplus::LineCapRound);
        float arm = (float)g_app.S(7);
        g.DrawLine(&ppen, cx - arm, cy, cx + arm, cy);
        g.DrawLine(&ppen, cx, cy - arm, cx, cy + arm);

        SetBkMode(hdc, TRANSPARENT);
        HGDIOBJ of = SelectObject(hdc, g_app.hFontBold);
        SetTextColor(hdc, hov ? kColAccentDark : kColTextMain);
        RECT r1{g_app.S(16), (int)(cy + cr + g_app.S(8)), w - g_app.S(16), (int)(cy + cr + g_app.S(30))};
        DrawTextW(hdc, heraw_i18n::tr_w("dock.well_idle").c_str(), -1, &r1, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);

        SelectObject(hdc, g_app.hFont);
        SetTextColor(hdc, kColTextMuted);
        RECT r2{g_app.S(16), r1.bottom + g_app.S(2), w - g_app.S(16), std::min<LONG>(h - g_app.S(6), r1.bottom + g_app.S(42))};
        const bool en = heraw_i18n::I18nManager::instance().is_english();
        DrawTextW(hdc,
                  en ? L"Auto-clusters by shutter cadence · Extracts AF score & subpixel drift"
                     : L"静默按快门时序自动分簇 · 提取 AF 对焦点锐度与亚像素轨迹",
                  -1, &r2, DT_CENTER | DT_WORDBREAK | DT_NOPREFIX);
        SelectObject(hdc, of);
        return;
    }

    gdip_round_rect(g, 0.5f, 0.5f, (float)w - 1.5f, (float)h - 1.5f, (float)g_app.S(8),
                    gdip_col(RGB(255, 255, 255)), gdip_col(kColInputEdge), 1.15f);

    int rowH = g_app.S(54);
    int pad  = g_app.S(4);
    int totalH = (int)g_app.groups.size() * rowH;
    int maxScroll = std::max(0, totalH - (h - 2 * pad));
    g_app.burst_scroll_y = std::max(0, std::min(maxScroll, g_app.burst_scroll_y));

    int savedDC = SaveDC(hdc);
    IntersectClipRect(hdc, pad, pad, w - pad, h - pad);

    SetBkMode(hdc, TRANSPARENT);
    const wchar_t* curPresetCode = kPresetCodes[std::max(0, std::min(3, g_app.preset_idx))];

    for (size_t i = 0; i < g_app.groups.size(); ++i) {
        const auto& grp = g_app.groups[i];
        int y0 = pad + (int)i * rowH - g_app.burst_scroll_y;
        int y1 = y0 + rowH;
        if (y1 < pad || y0 > h - pad) continue;

        bool is_sel = is_group_selected((int)i);
        bool is_hov = ((int)i == g_app.burst_hover_idx);
        if (is_sel || is_hov) {
            COLORREF rbg = is_sel ? kColAccentSoft : RGB(242, 247, 253);
            COLORREF rbd = is_sel ? RGB(166, 190, 222) : RGB(222, 232, 246);
            gdip_round_rect(g, (float)pad + 2.0f, (float)y0 + 1.5f,
                            (float)(w - 2 * pad - (maxScroll > 0 ? g_app.S(8) : 0)) - 4.0f,
                            (float)rowH - 3.0f, (float)g_app.S(6),
                            gdip_col(rbg), gdip_col(rbd), 1.0f);
            if (is_sel) {
                gdip_round_rect(g, (float)pad + 3.0f, (float)y0 + (float)g_app.S(8),
                                (float)g_app.S(3), (float)rowH - (float)g_app.S(16), 1.5f,
                                gdip_col(kColAccent), Gdiplus::Color(0, 0, 0, 0), 0.0f);
            }
        } else if (i + 1 < g_app.groups.size()) {
            Gdiplus::Pen sep(gdip_col(RGB(232, 238, 247)), 1.0f);
            g.DrawLine(&sep, (float)g_app.S(12), (float)y1 - 0.5f, (float)(w - g_app.S(12)), (float)y1 - 0.5f);
        }

        // Checkbox
        float cbx = (float)g_app.S(14);
        float cby = (float)y0 + (float)g_app.S(10);
        float cbs = (float)g_app.S(15);
        COLORREF cFill = grp.checked ? kColAccent : RGB(255, 255, 255);
        COLORREF cEdge = grp.checked ? kColAccentDark : RGB(166, 184, 210);
        gdip_round_rect(g, cbx, cby, cbs, cbs, (float)g_app.S(4), gdip_col(cFill), gdip_col(cEdge), 1.2f);
        if (grp.checked) {
            Gdiplus::Pen cp(Gdiplus::Color(255, 255, 255, 255), 1.75f);
            cp.SetStartCap(Gdiplus::LineCapRound);
            cp.SetEndCap(Gdiplus::LineCapRound);
            cp.SetLineJoin(Gdiplus::LineJoinRound);
            Gdiplus::PointF pts[3] = {
                Gdiplus::PointF(cbx + cbs * 0.22f, cby + cbs * 0.52f),
                Gdiplus::PointF(cbx + cbs * 0.43f, cby + cbs * 0.72f),
                Gdiplus::PointF(cbx + cbs * 0.78f, cby + cbs * 0.28f)
            };
            g.DrawLines(&cp, pts, 3);
        }

        int rightEdge = w - g_app.S(maxScroll > 0 ? 18 : 12);

        int badgeW = g_app.S(54);
        int badgeX = rightEdge - badgeW;
        gdip_round_rect(g, (float)badgeX, (float)y0 + (float)g_app.S(7), (float)badgeW, (float)g_app.S(18),
                        (float)g_app.S(5), gdip_col(kColBadgeFill), gdip_col(kColBadgeEdge), 1.0f);
        HGDIOBJ of = SelectObject(hdc, g_app.hFontBadge);
        SetTextColor(hdc, kColBadgeText);
        RECT rb{badgeX, y0 + g_app.S(7), rightEdge, y0 + g_app.S(25)};
        DrawTextW(hdc, grp.sensor_badge.c_str(), -1, &rb, DT_CENTER | DT_VCENTER | DT_SINGLELINE);

        int line1Right = badgeX - g_app.S(6);
        if (!grp.bus_in_path.empty()) {
            int nibW = g_app.S(36);
            int nibX = badgeX - g_app.S(4) - nibW;
            gdip_round_rect(g, (float)nibX, (float)y0 + (float)g_app.S(7), (float)nibW, (float)g_app.S(18),
                            (float)g_app.S(5), gdip_col(kColBadgeFill), gdip_col(kColBadgeEdge), 1.0f);
            SetTextColor(hdc, kColBadgeText);
            RECT rnib{nibX, y0 + g_app.S(7), nibX + nibW, y0 + g_app.S(25)};
            DrawTextW(hdc, L"NIB", -1, &rnib, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
            line1Right = nibX - g_app.S(6);
        }

        SelectObject(hdc, g_app.hFontBold);
        SetTextColor(hdc, grp.checked ? kColTextMain : kColTextMuted);
        std::wstring line1 = grp.title;
        if (!grp.span_label.empty()) line1 += L"   " + grp.span_label;
        RECT rl1{g_app.S(36), y0 + g_app.S(6), line1Right, y0 + g_app.S(26)};
        DrawTextW(hdc, line1.c_str(), -1, &rl1, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);

        std::wstring recW = utf8_to_wide(grp.rec_preset);
        bool show_rx_pill = !grp.is_stray_archive && grp.has_telemetry && !recW.empty() &&
                            grp.rec_preset != "photo" && (recW != curPresetCode);
        int metaRight = rightEdge;
        if (show_rx_pill) {
            int rxW = g_app.S(112);
            int rxX = rightEdge - rxW;
            metaRight = rxX - g_app.S(6);
            bool rxHov = is_hov && (g_app.burst_hover_part == 2);
            gdip_round_rect(g, (float)rxX, (float)y0 + (float)g_app.S(29), (float)rxW, (float)g_app.S(19),
                            (float)g_app.S(5),
                            gdip_col(rxHov ? kColAccent : RGB(224, 235, 248)),
                            gdip_col(kColAccent), 1.0f);
            SelectObject(hdc, g_app.hFontSmall);
            SetTextColor(hdc, rxHov ? RGB(255, 255, 255) : kColAccentDark);
            RECT rrx{rxX, y0 + g_app.S(29), rightEdge, y0 + g_app.S(48)};
            std::wstring rxLbl = grp.rec_preset == "astro" ? heraw_i18n::tr_w("qcell.pill_astro")
                               : grp.rec_preset == "sci"   ? heraw_i18n::tr_w("qcell.pill_sci")
                               : heraw_i18n::tr_w("qcell.pill_arc");
            DrawTextW(hdc, rxLbl.c_str(), -1, &rrx, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        }

        SelectObject(hdc, g_app.hFontSmall);
        SetTextColor(hdc, kColTextMuted);
        RECT rl2{g_app.S(36), y0 + g_app.S(29), metaRight, y0 + g_app.S(48)};
        std::wstring meta = g_app.plan_scanning.load() && !grp.has_telemetry
                          ? heraw_i18n::tr_w("dock.well_scanning")
                          : grp.meta_line;
        DrawTextW(hdc, meta.c_str(), -1, &rl2, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);

        SelectObject(hdc, of);
    }

    RestoreDC(hdc, savedDC);

    if (maxScroll > 0) {
        float sbX = (float)(w - g_app.S(8));
        float sbY = (float)pad + 2.0f;
        float sbH = (float)(h - 2 * pad) - 4.0f;
        float thumbH = std::max((float)g_app.S(24), sbH * ((float)(h - 2 * pad) / (float)totalH));
        float thumbY = sbY + (sbH - thumbH) * ((float)g_app.burst_scroll_y / (float)maxScroll);
        gdip_round_rect(g, sbX, thumbY, (float)g_app.S(4), thumbH, 2.0f,
                        gdip_col(RGB(172, 188, 212)), Gdiplus::Color(0, 0, 0, 0), 0.0f);
    }
}

static void pick_raw_files_dialog(HWND owner);

static LRESULT CALLBACK NijiBurstQueueWndProc(HWND hw, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
        case WM_ERASEBKGND: return 1;
        case WM_MOUSEMOVE: {
            int x = GET_X_LPARAM(lParam);
            int y = GET_Y_LPARAM(lParam);
            RECT rc{};
            GetClientRect(hw, &rc);
            int newIdx = -1, newPart = 0;
            if (g_app.groups.empty() && g_app.inputs.empty()) {
                newIdx = -2;
            } else {
                int rowH = g_app.S(54);
                int pad  = g_app.S(4);
                int idx = (y - pad + g_app.burst_scroll_y) / std::max(1, rowH);
                if (idx >= 0 && (size_t)idx < g_app.groups.size()) {
                    newIdx = idx;
                    int yRel = (y - pad + g_app.burst_scroll_y) - idx * rowH;
                    int rightEdge = rc.right - g_app.S(12);
                    if (x >= g_app.S(10) && x <= g_app.S(33) && yRel >= g_app.S(6) && yRel <= g_app.S(30)) newPart = 1;
                    else if (x >= rightEdge - g_app.S(114) && x <= rightEdge && yRel >= g_app.S(27)) newPart = 2;
                }
            }
            if (newIdx != g_app.burst_hover_idx || newPart != g_app.burst_hover_part) {
                g_app.burst_hover_idx = newIdx;
                g_app.burst_hover_part = newPart;
                TRACKMOUSEEVENT tme{};
                tme.cbSize = sizeof(tme);
                tme.dwFlags = TME_LEAVE;
                tme.hwndTrack = hw;
                TrackMouseEvent(&tme);
                InvalidateRect(hw, nullptr, FALSE);
            }
            return 0;
        }
        case WM_MOUSELEAVE:
            if (g_app.burst_hover_idx != -1 || g_app.burst_hover_part != 0) {
                g_app.burst_hover_idx = -1;
                g_app.burst_hover_part = 0;
                InvalidateRect(hw, nullptr, FALSE);
            }
            return 0;
        case WM_MOUSEWHEEL: {
            int delta = GET_WHEEL_DELTA_WPARAM(wParam);
            g_app.burst_scroll_y -= (delta / WHEEL_DELTA) * g_app.S(36);
            if (g_app.burst_scroll_y < 0) g_app.burst_scroll_y = 0;
            InvalidateRect(hw, nullptr, FALSE);
            return 0;
        }
        case WM_GETDLGCODE:
            return DLGC_WANTARROWS | DLGC_WANTCHARS;
        case WM_KEYDOWN: {
            bool ctrl = (GetKeyState(VK_CONTROL) < 0);
            bool shift = (GetKeyState(VK_SHIFT) < 0);
            if (ctrl && (wParam == 'A' || wParam == 'a')) {
                if (!g_app.groups.empty()) {
                    g_app.group_sel.assign(g_app.groups.size(), 1);
                    sync_cross_tab_ui();
                    repaint_left_dock_views();
                }
                return 0;
            }
            if (ctrl && (wParam == 'M' || wParam == 'm')) {
                merge_selected_groups_win();
                return 0;
            }
            if (wParam == VK_SPACE) {
                batch_toggle_selected_groups_checked();
                return 0;
            }
            if (wParam == VK_DELETE || wParam == VK_BACK) {
                remove_selected_groups_win();
                return 0;
            }
            if ((wParam == VK_UP || wParam == VK_DOWN) && !g_app.groups.empty()) {
                normalize_selection_masks();
                int next = std::clamp(g_app.selected_group + (wParam == VK_DOWN ? 1 : -1), 0, (int)g_app.groups.size() - 1);
                if (shift) {
                    g_app.selected_group = next;
                    g_app.group_sel.assign(g_app.groups.size(), 0);
                    int lo = std::min(g_app.group_anchor, next), hi = std::max(g_app.group_anchor, next);
                    for (int k = lo; k <= hi; ++k) g_app.group_sel[(size_t)k] = 1;
                    g_app.frame_sel.clear();
                    normalize_selection_masks();
                } else {
                    select_single_group(next);
                }
                sync_cross_tab_ui();
                repaint_left_dock_views();
                return 0;
            }
            break;
        }
        case WM_LBUTTONDOWN: {
            if (g_app.groups.empty() && g_app.inputs.empty()) {
                pick_raw_files_dialog(g_app.hwnd);
                return 0;
            }
            SetFocus(hw);
            normalize_selection_masks();
            RECT rc{};
            GetClientRect(hw, &rc);
            int x = GET_X_LPARAM(lParam);
            int y = GET_Y_LPARAM(lParam);
            int rowH = g_app.S(54);
            int pad  = g_app.S(4);
            int idx = (y - pad + g_app.burst_scroll_y) / std::max(1, rowH);
            if (idx >= 0 && (size_t)idx < g_app.groups.size()) {
                int yRel = (y - pad + g_app.burst_scroll_y) - idx * rowH;
                int totalH = (int)g_app.groups.size() * rowH;
                int maxScroll = std::max(0, totalH - ((int)rc.bottom - 2 * pad));
                int rightEdge = rc.right - g_app.S(maxScroll > 0 ? 18 : 12);

                auto& grp = g_app.groups[(size_t)idx];
                const wchar_t* curPresetCode = kPresetCodes[std::max(0, std::min(3, g_app.preset_idx))];
                bool show_rx = !grp.is_stray_archive && grp.has_telemetry && !grp.rec_preset.empty() &&
                               grp.rec_preset != "photo" && (utf8_to_wide(grp.rec_preset) != curPresetCode);

                bool ctrl = (wParam & MK_CONTROL) != 0 || (GetKeyState(VK_CONTROL) < 0);
                bool shift = (wParam & MK_SHIFT) != 0 || (GetKeyState(VK_SHIFT) < 0);

                if (x >= g_app.S(10) && x <= g_app.S(33) && yRel >= g_app.S(6) && yRel <= g_app.S(30)) {
                    auto sel = selected_group_indices();
                    bool target = !grp.checked;
                    if (sel.size() >= 2 && is_group_selected(idx)) {
                        for (int si : sel) g_app.groups[(size_t)si].checked = target;
                    } else {
                        grp.checked = target;
                        select_single_group(idx);
                    }
                    sync_cross_tab_ui();
                } else if (show_rx && x >= rightEdge - g_app.S(114) && x <= rightEdge && yRel >= g_app.S(27)) {
                    select_single_group(idx);
                    apply_group_recommendation(idx);
                } else if (shift) {
                    if (g_app.selected_group != idx) g_app.optics = OpticsTelemetry{};
                    if (!ctrl) g_app.group_sel.assign(g_app.groups.size(), 0);
                    int lo = std::min(g_app.group_anchor, idx);
                    int hi = std::max(g_app.group_anchor, idx);
                    for (int k = lo; k <= hi; ++k) g_app.group_sel[(size_t)k] = 1;
                    g_app.selected_group = idx;
                    g_app.frame_sel_idx = 0;
                    g_app.frame_anchor = 0;
                    g_app.frame_scroll_y = 0;
                    g_app.frame_sel.clear();
                    normalize_selection_masks();
                    sync_cross_tab_ui();
                } else if (ctrl) {
                    if (g_app.selected_group != idx) g_app.optics = OpticsTelemetry{};
                    if (g_app.group_sel[(size_t)idx] && selected_group_indices().size() > 1) {
                        g_app.group_sel[(size_t)idx] = 0;
                        auto rem = selected_group_indices();
                        if (!rem.empty()) g_app.selected_group = rem.front();
                    } else {
                        g_app.group_sel[(size_t)idx] = 1;
                        g_app.selected_group = idx;
                    }
                    g_app.group_anchor = idx;
                    g_app.frame_sel_idx = 0;
                    g_app.frame_anchor = 0;
                    g_app.frame_scroll_y = 0;
                    g_app.frame_sel.clear();
                    normalize_selection_masks();
                    sync_cross_tab_ui();
                } else {
                    select_single_group(idx);
                    sync_cross_tab_ui();
                }
                repaint_left_dock_views();
            }
            return 0;
        }
        case WM_RBUTTONDOWN: {
            if (g_app.groups.empty()) return 0;
            SetFocus(hw);
            normalize_selection_masks();
            int y = GET_Y_LPARAM(lParam);
            int rowH = g_app.S(54);
            int pad  = g_app.S(4);
            int idx = (y - pad + g_app.burst_scroll_y) / std::max(1, rowH);
            if (idx >= 0 && (size_t)idx < g_app.groups.size() && !is_group_selected(idx)) {
                select_single_group(idx);
                sync_cross_tab_ui();
                repaint_left_dock_views();
            }
            auto sel = selected_group_indices();
            int min_req = min_valid_frames_win();
            HMENU hMenu = CreatePopupMenu();
            if (hMenu) {
                AppendMenuW(hMenu, MF_STRING, ID_QMENU_TOGGLE_CHK,
                            heraw_i18n::tr_w(sel.size() >= 2 ? "qmenu.toggle_multi" : "qmenu.toggle_one").c_str());
                AppendMenuW(hMenu, MF_STRING, ID_QMENU_ONLY_BEST,   heraw_i18n::tr_w("qmenu.only_best").c_str());
                AppendMenuW(hMenu, MF_STRING, ID_QMENU_CHECK_VALID, heraw_i18n::trf_w("qmenu.check_valid_fmt", min_req).c_str());
                AppendMenuW(hMenu, MF_STRING, ID_QMENU_SELECT_ALL,  (heraw_i18n::tr_w("qmenu.select_all") + L" (Ctrl+A)").c_str());
                AppendMenuW(hMenu, MF_SEPARATOR, 0, nullptr);
                AppendMenuW(hMenu, MF_STRING | ((sel.size() >= 2 && !g_app.running.load()) ? 0 : MF_GRAYED),
                            ID_QMENU_MERGE, (heraw_i18n::tr_w("qmenu.merge") + L" (Ctrl+M)").c_str());
                AppendMenuW(hMenu, MF_STRING | ((has_stray_groups_win() && !g_app.running.load()) ? 0 : MF_GRAYED),
                            ID_QMENU_CLEAN_STRAY, heraw_i18n::trf_w("qmenu.clean_fmt", min_req).c_str());
                AppendMenuW(hMenu, MF_STRING | ((!sel.empty() && !g_app.running.load()) ? 0 : MF_GRAYED),
                            ID_QMENU_REMOVE, (heraw_i18n::tr_w(sel.size() >= 2 ? "qmenu.remove_multi" : "qmenu.remove_one") + L" (Delete)").c_str());
                POINT pt{GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
                ClientToScreen(hw, &pt);
                TrackPopupMenuEx(hMenu, TPM_LEFTALIGN | TPM_TOPALIGN, pt.x, pt.y, g_app.hwnd, nullptr);
                DestroyMenu(hMenu);
            }
            return 0;
        }
        case WM_PAINT: {
            PAINTSTRUCT ps{};
            HDC hdc = BeginPaint(hw, &ps);
            RECT rc{};
            GetClientRect(hw, &rc);
            HDC mem = CreateCompatibleDC(hdc);
            HBITMAP bmp = CreateCompatibleBitmap(hdc, std::max(1L, rc.right), std::max(1L, rc.bottom));
            HGDIOBJ old = SelectObject(mem, bmp);
            paint_burst_queue(hw, mem);
            BitBlt(hdc, 0, 0, rc.right, rc.bottom, mem, 0, 0, SRCCOPY);
            SelectObject(mem, old);
            DeleteObject(bmp);
            DeleteDC(mem);
            EndPaint(hw, &ps);
            return 0;
        }
        case WM_PRINT:
        case WM_PRINTCLIENT:
            paint_burst_queue(hw, reinterpret_cast<HDC>(wParam));
            return 0;
    }
    return DefWindowProcW(hw, msg, wParam, lParam);
}

// ============================================================================
// Left Dock Custom Control 2: Inline Per-Frame AF Sharpness Inspector
// ============================================================================
static void lock_selected_frame_anchor(int frame_list_idx) {
    BurstGroupItem* grp = current_group_ptr();
    if (!grp || grp->is_stray_archive || frame_list_idx < 0 || (size_t)frame_list_idx >= grp->frames.size()) return;
    int f1 = grp->frames[(size_t)frame_list_idx].idx_1based;
    const bool en = heraw_i18n::I18nManager::instance().is_english();
    if (grp->locked_anchor_1based == f1) {
        grp->locked_anchor_1based = 0;
        set_status_ui(en ? L"Unlocked pose anchor; restored automatic sharpest selection"
                         : L"已解除定格帧锁定，恢复自动最锐选帧");
    } else {
        grp->locked_anchor_1based = f1;
        grp->frames[(size_t)frame_list_idx].included = true;
        recalculate_group_item(*grp, g_app.selected_group + 1);
        g_app.motion_mode_idx = 0;
        set_status_ui((en ? L"Locked frame #" : L"已锁定第 #") + std::to_wstring(f1) +
                      (en ? L" as pose anchor (--anchor-frame " : L" 帧为定格锚点 (--anchor-frame ") +
                      std::to_wstring(f1) + L")");
    }
    sync_cross_tab_ui();
    InvalidateRect(g_app.hFrameView, nullptr, FALSE);
}

static void paint_frame_inspector(HWND hw, HDC hdc) {
    normalize_selection_masks();
    RECT rc{};
    GetClientRect(hw, &rc);
    int w = rc.right - rc.left;
    int h = rc.bottom - rc.top;
    FillRect(hdc, &rc, g_app.hBrCard);

    Gdiplus::Graphics g(hdc);
    g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
    gdip_round_rect(g, 0.5f, 0.5f, (float)w - 1.5f, (float)h - 1.5f, (float)g_app.S(8),
                    gdip_col(RGB(255, 255, 255)), gdip_col(kColInputEdge), 1.15f);

    BurstGroupItem* grp = current_group_ptr();
    if (!grp || grp->frames.empty()) {
        SetBkMode(hdc, TRANSPARENT);
        HGDIOBJ of = SelectObject(hdc, g_app.hFont);
        SetTextColor(hdc, kColTextMuted);
        const std::wstring inspTxt = heraw_i18n::tr_w("dock.insp_empty");
        RECT calc{g_app.S(20), 0, w - g_app.S(20), 0};
        DrawTextW(hdc, inspTxt.c_str(), -1, &calc, DT_CENTER | DT_WORDBREAK | DT_NOPREFIX | DT_CALCRECT);
        int th = calc.bottom - calc.top;
        int topY = std::max(g_app.S(8), (h - th) / 2);
        RECT er{g_app.S(20), topY, w - g_app.S(20), topY + th + g_app.S(4)};
        DrawTextW(hdc, inspTxt.c_str(), -1, &er, DT_CENTER | DT_WORDBREAK | DT_NOPREFIX);
        SelectObject(hdc, of);
        return;
    }

    double max_acu = 1.0;
    for (const auto& f : grp->frames) if (f.acutance > max_acu) max_acu = f.acutance;

    int rowH = g_app.S(30);
    int pad  = g_app.S(4);
    int totalH = (int)grp->frames.size() * rowH;
    int maxScroll = std::max(0, totalH - (h - 2 * pad));
    g_app.frame_scroll_y = std::max(0, std::min(maxScroll, g_app.frame_scroll_y));

    int savedDC = SaveDC(hdc);
    IntersectClipRect(hdc, pad, pad, w - pad, h - pad);
    SetBkMode(hdc, TRANSPARENT);

    int eff_anchor = grp->locked_anchor_1based > 0 ? grp->locked_anchor_1based : grp->best_anchor_1based;
    std::wstring tok_anc = heraw_i18n::tr_w("ftag.anchor");
    std::wstring tok_shk = heraw_i18n::tr_w("ftag.shock");
    std::wstring tok_blr = heraw_i18n::tr_w("ftag.blur");

    for (size_t i = 0; i < grp->frames.size(); ++i) {
        const auto& fd = grp->frames[i];
        int y0 = pad + (int)i * rowH - g_app.frame_scroll_y;
        int y1 = y0 + rowH;
        if (y1 < pad || y0 > h - pad) continue;

        bool is_sel = is_frame_selected((int)i);
        bool is_hov = ((int)i == g_app.frame_hover_idx);
        bool is_anc = !grp->is_stray_archive &&
                      ((grp->locked_anchor_1based == fd.idx_1based) ||
                       (grp->locked_anchor_1based == 0 && g_app.motion_mode_idx == 0 && fd.idx_1based == eff_anchor));

        if (is_sel || is_hov) {
            gdip_round_rect(g, (float)pad + 2.0f, (float)y0 + 1.0f,
                            (float)(w - 2 * pad - (maxScroll > 0 ? g_app.S(8) : 0)) - 4.0f,
                            (float)rowH - 2.0f, (float)g_app.S(5),
                            gdip_col(is_sel ? RGB(226, 236, 248) : RGB(243, 247, 253)),
                            is_sel ? gdip_col(RGB(172, 196, 224)) : Gdiplus::Color(0, 0, 0, 0),
                            is_sel ? 1.0f : 0.0f);
            if (is_sel) {
                gdip_round_rect(g, (float)pad + 3.0f, (float)y0 + (float)g_app.S(5),
                                (float)g_app.S(2.5), (float)rowH - (float)g_app.S(10), 1.2f,
                                gdip_col(kColAccent), Gdiplus::Color(0, 0, 0, 0), 0.0f);
            }
        }

        float cbx = (float)g_app.S(10);
        float cbs = (float)g_app.S(14);
        float cby = (float)y0 + 0.5f * ((float)rowH - cbs);
        gdip_round_rect(g, cbx, cby, cbs, cbs, (float)g_app.S(3),
                        gdip_col(fd.included ? kColAccent : RGB(255, 255, 255)),
                        gdip_col(fd.included ? kColAccentDark : RGB(172, 188, 212)), 1.15f);
        if (fd.included) {
            Gdiplus::Pen cp(Gdiplus::Color(255, 255, 255, 255), 1.6f);
            cp.SetStartCap(Gdiplus::LineCapRound);
            cp.SetEndCap(Gdiplus::LineCapRound);
            cp.SetLineJoin(Gdiplus::LineJoinRound);
            Gdiplus::PointF pts[3] = {
                Gdiplus::PointF(cbx + cbs * 0.22f, cby + cbs * 0.52f),
                Gdiplus::PointF(cbx + cbs * 0.44f, cby + cbs * 0.72f),
                Gdiplus::PointF(cbx + cbs * 0.78f, cby + cbs * 0.28f)
            };
            g.DrawLines(&cp, pts, 3);
        }

        float rdx = (float)g_app.S(31);
        float rds = (float)g_app.S(13);
        float rdy = (float)y0 + 0.5f * ((float)rowH - rds);
        Gdiplus::SolidBrush rbr(gdip_col(is_anc ? kColAccent : RGB(255, 255, 255)));
        Gdiplus::Pen rpen(gdip_col(is_anc ? kColAccentDark : RGB(172, 188, 212)), 1.2f);
        g.FillEllipse(&rbr, rdx, rdy, rds, rds);
        g.DrawEllipse(&rpen, rdx, rdy, rds, rds);
        if (is_anc) {
            float dot = rds * 0.42f;
            Gdiplus::SolidBrush wbr(Gdiplus::Color(255, 255, 255, 255));
            g.FillEllipse(&wbr, rdx + 0.5f * (rds - dot), rdy + 0.5f * (rds - dot), dot, dot);
        }

        int rightEdge = w - g_app.S(maxScroll > 0 ? 14 : 8);
        int pw0 = g_app.S(44);
        int px0 = rightEdge - pw0;
        int shRight = px0 - g_app.S(4);
        int shLeft  = shRight - g_app.S(48);
        int scRight = shLeft - g_app.S(2);
        int scLeft  = scRight - g_app.S(40);
        int barW    = std::clamp((w * 18) / 100, g_app.S(52), g_app.S(72));
        int barLeft = scLeft - g_app.S(4) - barW;

        HGDIOBJ of = SelectObject(hdc, is_anc ? g_app.hFontBold : g_app.hFontSmall);
        SetTextColor(hdc, fd.included ? kColTextMain : kColTextMuted);
        std::wstring nm = L"#" + std::to_wstring(fd.idx_1based) + L" " + stem_of_w(fd.file);
        RECT rnm{g_app.S(48), y0, std::max(g_app.S(110), barLeft - g_app.S(4)), y1};
        DrawTextW(hdc, nm.c_str(), -1, &rnm, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);

        float bx = (float)barLeft;
        float bw = (float)barW;
        float bh = (float)g_app.S(6);
        float by = (float)y0 + 0.5f * ((float)rowH - bh);
        float ratio = (float)std::max(0.06, std::min(1.0, fd.acutance / max_acu));
        gdip_round_rect(g, bx, by, bw, bh, 0.5f * bh, gdip_col(RGB(224, 232, 245)), Gdiplus::Color(0, 0, 0, 0), 0.0f);
        COLORREF barCol = !fd.included ? RGB(180, 190, 206)
                        : fd.is_crown  ? kColAccent : RGB(102, 138, 182);
        gdip_round_rect(g, bx, by, bw * ratio, bh, 0.5f * bh, gdip_col(barCol), Gdiplus::Color(0, 0, 0, 0), 0.0f);

        int normScore = (int)std::llround(99.0 * (fd.acutance / max_acu));
        wchar_t sbuf[32] = {0};
        std::swprintf(sbuf, 32, L"%d%ls", normScore, fd.is_crown ? L"♛" : L"");
        SelectObject(hdc, fd.is_crown ? g_app.hFontBold : g_app.hFontSmall);
        SetTextColor(hdc, fd.is_crown ? kColAccentDark : kColTextMain);
        RECT rsc{scLeft, y0, scRight, y1};
        DrawTextW(hdc, sbuf, -1, &rsc, DT_LEFT | DT_VCENTER | DT_SINGLELINE);

        std::swprintf(sbuf, 32, L"%.1fpx", fd.shift_px);
        SelectObject(hdc, g_app.hFontSmall);
        SetTextColor(hdc, kColTextMuted);
        RECT rsh{shLeft, y0, shRight, y1};
        DrawTextW(hdc, sbuf, -1, &rsh, DT_RIGHT | DT_VCENTER | DT_SINGLELINE);

        std::wstring anom = flags_to_anomaly_cn(fd.flags, fd.is_shock, grp->locked_anchor_1based == fd.idx_1based);
        if (!anom.empty()) {
            COLORREF pBg = (anom == tok_anc) ? kColBadgeFill
                         : (anom == tok_shk) ? RGB(250, 238, 220)
                         : (anom == tok_blr) ? RGB(250, 229, 227) : RGB(232, 238, 241);
            COLORREF pFg = (anom == tok_anc) ? kColAccentDark
                         : (anom == tok_shk) ? kColWarnAmber
                         : (anom == tok_blr) ? kColWarnCoral : kColTextMuted;
            gdip_round_rect(g, (float)px0, (float)y0 + (float)g_app.S(6), (float)pw0, (float)g_app.S(18),
                            (float)g_app.S(4), gdip_col(pBg), Gdiplus::Color(0, 0, 0, 0), 0.0f);
            SetTextColor(hdc, pFg);
            RECT ran{px0, y0 + g_app.S(6), px0 + pw0, y0 + g_app.S(24)};
            DrawTextW(hdc, anom.c_str(), -1, &ran, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        }

        SelectObject(hdc, of);
    }

    RestoreDC(hdc, savedDC);

    if (maxScroll > 0) {
        float sbX = (float)(w - g_app.S(8));
        float sbY = (float)pad + 2.0f;
        float sbH = (float)(h - 2 * pad) - 4.0f;
        float thumbH = std::max((float)g_app.S(22), sbH * ((float)(h - 2 * pad) / (float)totalH));
        float thumbY = sbY + (sbH - thumbH) * ((float)g_app.frame_scroll_y / (float)maxScroll);
        gdip_round_rect(g, sbX, thumbY, (float)g_app.S(4), thumbH, 2.0f,
                        gdip_col(RGB(172, 188, 212)), Gdiplus::Color(0, 0, 0, 0), 0.0f);
    }
}

static LRESULT CALLBACK NijiFrameInspWndProc(HWND hw, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
        case WM_ERASEBKGND: return 1;
        case WM_GETDLGCODE:
            return DLGC_WANTARROWS | DLGC_WANTCHARS;
        case WM_KEYDOWN: {
            BurstGroupItem* grp = current_group_ptr();
            if (!grp || grp->frames.empty()) return 0;
            bool ctrl = (GetKeyState(VK_CONTROL) < 0);
            bool shift = (GetKeyState(VK_SHIFT) < 0);
            if (ctrl && (wParam == 'A' || wParam == 'a')) {
                g_app.frame_sel.assign(grp->frames.size(), 1);
                sync_cross_tab_ui();
                InvalidateRect(hw, nullptr, FALSE);
                return 0;
            }
            if (ctrl && shift && (wParam == 'N' || wParam == 'n')) {
                split_selected_frames_win();
                return 0;
            }
            if (wParam == VK_SPACE || wParam == VK_DELETE || wParam == VK_BACK) {
                batch_toggle_selected_frames_included();
                return 0;
            }
            if (wParam == VK_UP || wParam == VK_DOWN) {
                normalize_selection_masks();
                int next = std::clamp(g_app.frame_sel_idx + (wParam == VK_DOWN ? 1 : -1), 0, (int)grp->frames.size() - 1);
                if (shift) {
                    g_app.frame_sel_idx = next;
                    g_app.frame_sel.assign(grp->frames.size(), 0);
                    int lo = std::min(g_app.frame_anchor, next), hi = std::max(g_app.frame_anchor, next);
                    for (int k = lo; k <= hi; ++k) g_app.frame_sel[(size_t)k] = 1;
                } else {
                    select_single_frame(next);
                }
                sync_cross_tab_ui();
                InvalidateRect(hw, nullptr, FALSE);
                return 0;
            }
            break;
        }
        case WM_MOUSEMOVE: {
            BurstGroupItem* grp = current_group_ptr();
            int newIdx = -1;
            if (grp && !grp->frames.empty()) {
                int y = GET_Y_LPARAM(lParam);
                int rowH = g_app.S(30);
                int pad  = g_app.S(4);
                int idx = (y - pad + g_app.frame_scroll_y) / std::max(1, rowH);
                if (idx >= 0 && (size_t)idx < grp->frames.size()) newIdx = idx;
            }
            if (newIdx != g_app.frame_hover_idx) {
                g_app.frame_hover_idx = newIdx;
                TRACKMOUSEEVENT tme{};
                tme.cbSize = sizeof(tme);
                tme.dwFlags = TME_LEAVE;
                tme.hwndTrack = hw;
                TrackMouseEvent(&tme);
                InvalidateRect(hw, nullptr, FALSE);
            }
            return 0;
        }
        case WM_MOUSELEAVE:
            if (g_app.frame_hover_idx != -1) {
                g_app.frame_hover_idx = -1;
                InvalidateRect(hw, nullptr, FALSE);
            }
            return 0;
        case WM_MOUSEWHEEL: {
            int delta = GET_WHEEL_DELTA_WPARAM(wParam);
            g_app.frame_scroll_y -= (delta / WHEEL_DELTA) * g_app.S(30);
            if (g_app.frame_scroll_y < 0) g_app.frame_scroll_y = 0;
            InvalidateRect(hw, nullptr, FALSE);
            return 0;
        }
        case WM_LBUTTONDOWN: {
            BurstGroupItem* grp = current_group_ptr();
            if (!grp || grp->frames.empty()) return 0;
            SetFocus(hw);
            normalize_selection_masks();
            int x = GET_X_LPARAM(lParam);
            int y = GET_Y_LPARAM(lParam);
            int rowH = g_app.S(30);
            int pad  = g_app.S(4);
            int idx = (y - pad + g_app.frame_scroll_y) / std::max(1, rowH);
            if (idx >= 0 && (size_t)idx < grp->frames.size()) {
                bool ctrl = (wParam & MK_CONTROL) != 0 || (GetKeyState(VK_CONTROL) < 0);
                bool shift = (wParam & MK_SHIFT) != 0 || (GetKeyState(VK_SHIFT) < 0);
                auto& fd = grp->frames[(size_t)idx];
                if (x >= g_app.S(6) && x <= g_app.S(27)) {
                    auto fsel = selected_frame_indices();
                    bool target = !fd.included;
                    if (fsel.size() >= 2 && is_frame_selected(idx)) {
                        for (int fi : fsel) {
                            auto& item = grp->frames[(size_t)fi];
                            item.included = target;
                            if (!target && grp->locked_anchor_1based == item.idx_1based) grp->locked_anchor_1based = 0;
                        }
                    } else {
                        fd.included = target;
                        if (!fd.included && grp->locked_anchor_1based == fd.idx_1based) grp->locked_anchor_1based = 0;
                        select_single_frame(idx);
                    }
                    grp->parallax_measured = false;
                    grp->depth_known = false;
                    grp->depth_observable = false;
                    grp->field_observability.clear();
                    recalculate_group_item(*grp, g_app.selected_group + 1);
                    sync_cross_tab_ui();
                    InvalidateRect(g_app.hBurstView, nullptr, FALSE);
                } else if (x >= g_app.S(28) && x <= g_app.S(47)) {
                    select_single_frame(idx);
                    lock_selected_frame_anchor(idx);
                } else if (shift) {
                    if (!ctrl) g_app.frame_sel.assign(grp->frames.size(), 0);
                    int lo = std::min(g_app.frame_anchor, idx), hi = std::max(g_app.frame_anchor, idx);
                    for (int k = lo; k <= hi; ++k) g_app.frame_sel[(size_t)k] = 1;
                    g_app.frame_sel_idx = idx;
                    sync_cross_tab_ui();
                } else if (ctrl) {
                    if (g_app.frame_sel[(size_t)idx] && selected_frame_indices().size() > 1) {
                        g_app.frame_sel[(size_t)idx] = 0;
                        auto rem = selected_frame_indices();
                        if (!rem.empty()) g_app.frame_sel_idx = rem.front();
                    } else {
                        g_app.frame_sel[(size_t)idx] = 1;
                        g_app.frame_sel_idx = idx;
                    }
                    g_app.frame_anchor = idx;
                    sync_cross_tab_ui();
                } else {
                    select_single_frame(idx);
                    sync_cross_tab_ui();
                }
                InvalidateRect(hw, nullptr, FALSE);
            }
            return 0;
        }
        case WM_RBUTTONDOWN: {
            BurstGroupItem* grp = current_group_ptr();
            if (!grp || grp->frames.empty()) return 0;
            SetFocus(hw);
            normalize_selection_masks();
            int y = GET_Y_LPARAM(lParam);
            int rowH = g_app.S(30);
            int pad  = g_app.S(4);
            int idx = (y - pad + g_app.frame_scroll_y) / std::max(1, rowH);
            if (idx >= 0 && (size_t)idx < grp->frames.size() && !is_frame_selected(idx)) {
                select_single_frame(idx);
                sync_cross_tab_ui();
                InvalidateRect(hw, nullptr, FALSE);
            }
            g_app.context_frame_idx = (idx >= 0 && (size_t)idx < grp->frames.size()) ? idx : g_app.frame_sel_idx;
            auto fsel = selected_frame_indices();
            int f1 = grp->frames[(size_t)std::clamp(g_app.context_frame_idx, 0, (int)grp->frames.size() - 1)].idx_1based;
            HMENU hMenu = CreatePopupMenu();
            if (hMenu) {
                AppendMenuW(hMenu, MF_STRING, ID_FMENU_TOGGLE_INC,
                            heraw_i18n::tr_w(fsel.size() >= 2 ? "fmenu.toggle_multi" : "fmenu.toggle_one").c_str());
                AppendMenuW(hMenu, MF_STRING | ((fsel.size() >= 1 && fsel.size() < grp->frames.size()) ? 0 : MF_GRAYED),
                            ID_FMENU_KEEP_ONLY, heraw_i18n::tr_w("fmenu.keep_only").c_str());
                AppendMenuW(hMenu, MF_STRING, ID_FMENU_RESTORE_ALL, heraw_i18n::tr_w("fmenu.restore_all").c_str());
                AppendMenuW(hMenu, MF_STRING | (!grp->is_stray_archive ? 0 : MF_GRAYED),
                            ID_FMENU_LOCK_ANC, heraw_i18n::trf_w("fmenu.lock_anchor_fmt", f1).c_str());
                AppendMenuW(hMenu, MF_SEPARATOR, 0, nullptr);
                AppendMenuW(hMenu, MF_STRING | ((fsel.size() >= 1 && fsel.size() < grp->frames.size() && !g_app.running.load()) ? 0 : MF_GRAYED),
                            ID_FMENU_SPLIT, (heraw_i18n::tr_w("fmenu.split") + L" (Ctrl+Shift+N)").c_str());
                POINT pt{GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
                ClientToScreen(hw, &pt);
                TrackPopupMenuEx(hMenu, TPM_LEFTALIGN | TPM_TOPALIGN, pt.x, pt.y, g_app.hwnd, nullptr);
                DestroyMenu(hMenu);
            }
            return 0;
        }
        case WM_LBUTTONDBLCLK: {
            BurstGroupItem* grp = current_group_ptr();
            if (!grp || grp->frames.empty()) return 0;
            int y = GET_Y_LPARAM(lParam);
            int rowH = g_app.S(30);
            int pad  = g_app.S(4);
            int idx = (y - pad + g_app.frame_scroll_y) / std::max(1, rowH);
            if (idx >= 0 && (size_t)idx < grp->frames.size()) {
                select_single_frame(idx);
                lock_selected_frame_anchor(idx);
            }
            return 0;
        }
        case WM_PAINT: {
            PAINTSTRUCT ps{};
            HDC hdc = BeginPaint(hw, &ps);
            RECT rc{};
            GetClientRect(hw, &rc);
            HDC mem = CreateCompatibleDC(hdc);
            HBITMAP bmp = CreateCompatibleBitmap(hdc, std::max(1L, rc.right), std::max(1L, rc.bottom));
            HGDIOBJ old = SelectObject(mem, bmp);
            paint_frame_inspector(hw, mem);
            BitBlt(hdc, 0, 0, rc.right, rc.bottom, mem, 0, 0, SRCCOPY);
            SelectObject(mem, old);
            DeleteObject(bmp);
            DeleteDC(mem);
            EndPaint(hw, &ps);
            return 0;
        }
        case WM_PRINT:
        case WM_PRINTCLIENT:
            paint_frame_inspector(hw, reinterpret_cast<HDC>(wParam));
            return 0;
    }
    return DefWindowProcW(hw, msg, wParam, lParam);
}

// ============================================================================
// Tab 2 Custom Control: Live Epipolar Parallax Meter (max_parallax_2x_px vs 8px)
// ============================================================================
static void paint_parallax_meter(HWND hw, HDC hdc) {
    RECT rc{};
    GetClientRect(hw, &rc);
    int w = rc.right - rc.left;
    int h = rc.bottom - rc.top;
    FillRect(hdc, &rc, g_app.hBrCard);

    Gdiplus::Graphics g(hdc);
    g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
    gdip_round_rect(g, 0.5f, 0.5f, (float)w - 1.5f, (float)h - 1.5f, (float)g_app.S(7),
                    gdip_col(kColCardSub), gdip_col(kColSubBorder), 1.0f);

    BurstGroupItem* grp = current_group_ptr();
    bool has_meas = grp && grp->parallax_measured;
    bool has_preview = grp && grp->has_telemetry;
    double px2 = grp ? grp->max_parallax_2x_px : 0.0;
    bool pass = has_meas && grp->depth_known && grp->depth_observable;
    const bool en = heraw_i18n::I18nManager::instance().is_english();

    SetBkMode(hdc, TRANSPARENT);
    HGDIOBJ of = SelectObject(hdc, g_app.hFontBold);
    SetTextColor(hdc, kColTextMain);
    RECT rl{g_app.S(12), 0, g_app.S(136), h};
    DrawTextW(hdc, has_meas ? (en ? L"Measured Parallax" : L"实测极线视差") : (en ? L"Est. Parallax" : L"预估视差"),
              -1, &rl, DT_LEFT | DT_VCENTER | DT_SINGLELINE);

    float bx = (float)g_app.S(142);
    float bw = std::clamp((float)w * 0.34f, (float)g_app.S(136), (float)g_app.S(250));
    float bh = (float)g_app.S(8);
    float by = 0.5f * ((float)h - bh);
    gdip_round_rect(g, bx, by, bw, bh, 0.5f * bh, gdip_col(RGB(216, 226, 240)), Gdiplus::Color(0, 0, 0, 0), 0.0f);

    float tickX = bx + bw * 0.5f;
    Gdiplus::Pen tickPen(gdip_col(kColAccentDark), 1.4f);
    g.DrawLine(&tickPen, tickX, by - 2.5f, tickX, by + bh + 2.5f);

    if ((has_meas || has_preview) && px2 > 0.05) {
        float ratio = (float)std::min(1.0, px2 / 16.0);
        COLORREF fc = pass ? kColOkGreen : RGB(154, 170, 194);
        gdip_round_rect(g, bx, by, std::max(bh, bw * ratio), bh, 0.5f * bh,
                        gdip_col(fc), Gdiplus::Color(0, 0, 0, 0), 0.0f);
    }

    std::wstring msg;
    COLORREF tcol = kColTextMuted;
    if (!has_meas && !has_preview) {
        msg = heraw_i18n::tr_w("t2.para_idle");
    } else if (!has_meas) {
        msg = heraw_i18n::trf_w("t2.para_est_fmt", px2);
    } else if (pass) {
        msg = heraw_i18n::trf_w("t2.para_ok_fmt", px2);
        tcol = kColOkGreen;
    } else {
        msg = heraw_i18n::trf_w(grp->depth_known ? "t2.para_low_fmt" : "t2.para_pend_fmt", px2);
        tcol = kColWarnAmber;
    }
    SelectObject(hdc, pass ? g_app.hFontBold : g_app.hFont);
    SetTextColor(hdc, tcol);
    RECT rr{(int)(bx + bw + g_app.S(12)), 0, w - g_app.S(10), h};
    DrawTextW(hdc, msg.c_str(), -1, &rr, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
    SelectObject(hdc, of);
}

static LRESULT CALLBACK NijiParallaxBarWndProc(HWND hw, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
        case WM_ERASEBKGND: return 1;
        case WM_PAINT: {
            PAINTSTRUCT ps{};
            HDC hdc = BeginPaint(hw, &ps);
            paint_parallax_meter(hw, hdc);
            EndPaint(hw, &ps);
            return 0;
        }
        case WM_PRINT:
        case WM_PRINTCLIENT:
            paint_parallax_meter(hw, reinterpret_cast<HDC>(wParam));
            return 0;
    }
    return DefWindowProcW(hw, msg, wParam, lParam);
}

// ============================================================================
// Tab 4 Custom Control: GDI+ Vector Trajectory + 2x2 Quad + 5-Zone MTF50 Radar
// ============================================================================
static void render_optics_diagnostics_dc(HDC hdc, int w, int h) {
    RECT full{0, 0, w, h};
    FillRect(hdc, &full, g_app.hBrCard);

    Gdiplus::Graphics g(hdc);
    g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
    g.SetTextRenderingHint(Gdiplus::TextRenderingHintClearTypeGridFit);

    int gap = g_app.S(14);
    int pw  = (w - gap) / 2;
    int rx0 = pw + gap;

    gdip_round_rect(g, 0.5f, 0.5f, (float)pw - 1.0f, (float)h - 1.0f, (float)g_app.S(9),
                    gdip_col(kColCardSub), gdip_col(kColSubBorder), 1.1f);
    gdip_round_rect(g, (float)rx0 + 0.5f, 0.5f, (float)(w - rx0) - 1.0f, (float)h - 1.0f, (float)g_app.S(9),
                    gdip_col(kColCardSub), gdip_col(kColSubBorder), 1.1f);

    SetBkMode(hdc, TRANSPARENT);
    HGDIOBJ of = SelectObject(hdc, g_app.hFontBold);
    SetTextColor(hdc, kColTextMain);
    RECT lhRc{g_app.S(14), g_app.S(8), pw - g_app.S(14), g_app.S(30)};
    DrawTextW(hdc, heraw_i18n::tr_w("t4.traj_title").c_str(), -1, &lhRc, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);

    int rw = w - rx0;
    int gBadgeW = g_app.S(142);
    int gBadgeX = rx0 + rw - gBadgeW - g_app.S(12);
    RECT rhRc{rx0 + g_app.S(14), g_app.S(8), gBadgeX - g_app.S(6), g_app.S(30)};
    DrawTextW(hdc, heraw_i18n::tr_w("t4.radar_title").c_str(), -1, &rhRc, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);

    float plotCx = 0.5f * (float)pw - (float)g_app.S(24);
    float plotCy = 0.5f * (float)h + (float)g_app.S(2);
    float plotR  = (float)std::min(pw - g_app.S(110), h - g_app.S(92)) * 0.5f;
    if (plotR < 36.0f) plotR = 36.0f;

    float wellL = (float)g_app.S(14);
    float wellT = (float)g_app.S(34);
    float wellW = std::max(40.0f, (float)(pw - g_app.S(96)));
    float wellH = std::max(40.0f, (float)h - (float)g_app.S(64));
    gdip_round_rect(g, wellL, wellT, wellW, wellH, (float)g_app.S(6),
                    gdip_col(RGB(255, 255, 255)), gdip_col(kColSubBorder), 1.0f);

    Gdiplus::Pen ringPen(gdip_col(RGB(204, 216, 234)), 1.0f);
    Gdiplus::REAL dashPat[2] = {3.0f, 3.0f};
    ringPen.SetDashPattern(dashPat, 2);
    g.DrawEllipse(&ringPen, plotCx - plotR, plotCy - plotR, 2.0f * plotR, 2.0f * plotR);
    g.DrawEllipse(&ringPen, plotCx - 0.5f * plotR, plotCy - 0.5f * plotR, plotR, plotR);

    Gdiplus::Pen axisPen(gdip_col(RGB(178, 194, 218)), 1.1f);
    g.DrawLine(&axisPen, plotCx - plotR - 8.0f, plotCy, plotCx + plotR + 8.0f, plotCy);
    g.DrawLine(&axisPen, plotCx, plotCy - plotR - 8.0f, plotCx, plotCy + plotR + 8.0f);

    BurstGroupItem* grp = current_group_ptr();
    bool quad_hit[2][2] = {{false, false}, {false, false}};
    int quad_cnt = 0;

    if (grp && grp->has_telemetry && !grp->frames.empty()) {
        double max_abs = 2.0;
        for (const auto& f : grp->frames) {
            max_abs = std::max(max_abs, std::max(std::fabs(f.dx), std::fabs(f.dy)) * 1.15);
            int qx = (f.dx - std::floor(f.dx)) >= 0.5 ? 1 : 0;
            int qy = (f.dy - std::floor(f.dy)) >= 0.5 ? 1 : 0;
            quad_hit[qy][qx] = true;
        }
        quad_cnt = (quad_hit[0][0] ? 1 : 0) + (quad_hit[0][1] ? 1 : 0) +
                   (quad_hit[1][0] ? 1 : 0) + (quad_hit[1][1] ? 1 : 0);

        std::vector<Gdiplus::PointF> pts;
        pts.reserve(grp->frames.size());
        for (const auto& f : grp->frames) {
            float px = plotCx + (float)(f.dx / max_abs) * plotR;
            float py = plotCy - (float)(f.dy / max_abs) * plotR;
            pts.emplace_back(px, py);
        }
        if (pts.size() >= 2) {
            Gdiplus::Pen trajPen(gdip_col(kColAccent, 215), 2.0f);
            trajPen.SetLineJoin(Gdiplus::LineJoinRound);
            g.DrawLines(&trajPen, pts.data(), (INT)pts.size());
        }
        SelectObject(hdc, g_app.hFontSmall);
        for (size_t i = 0; i < grp->frames.size(); ++i) {
            const auto& f = grp->frames[i];
            float nr = (float)g_app.S(f.is_crown ? 5 : 4);
            COLORREF nc = f.is_shock ? kColWarnAmber : (f.is_crown ? kColOkGreen : kColAccentDark);
            Gdiplus::SolidBrush nbr(gdip_col(nc));
            Gdiplus::Pen npen(Gdiplus::Color(255, 255, 255, 255), 1.3f);
            g.FillEllipse(&nbr, pts[i].X - nr, pts[i].Y - nr, 2.0f * nr, 2.0f * nr);
            g.DrawEllipse(&npen, pts[i].X - nr, pts[i].Y - nr, 2.0f * nr, 2.0f * nr);
            if (i == 0 || f.is_crown || i + 1 == grp->frames.size()) {
                wchar_t lb[24] = {0};
                std::swprintf(lb, 24, L"#%d", f.idx_1based);
                SetTextColor(hdc, nc);
                RECT lr{(int)pts[i].X + g_app.S(5), (int)pts[i].Y - g_app.S(9),
                        (int)pts[i].X + g_app.S(42), (int)pts[i].Y + g_app.S(9)};
                DrawTextW(hdc, lb, -1, &lr, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
            }
        }
    }

    if (grp && grp->phase_quadrants.size() == 4) {
        quad_cnt = 0;
        for (int q = 0; q < 4; ++q) {
            quad_hit[q/2][q%2] = grp->phase_quadrants[q] > 0;
            if (quad_hit[q/2][q%2]) ++quad_cnt;
        }
    }
    const bool en = heraw_i18n::I18nManager::instance().is_english();
    float qx0 = (float)(pw - g_app.S(78));
    float qy0 = plotCy - (float)g_app.S(34);
    float qcs = (float)g_app.S(24);
    float qgp = (float)g_app.S(4);
    SelectObject(hdc, g_app.hFontSmall);
    SetTextColor(hdc, kColTextMuted);
    RECT qhdr{(int)qx0 - g_app.S(8), (int)(qy0 - g_app.S(20)), pw - g_app.S(8), (int)qy0};
    DrawTextW(hdc, en ? L"2×2 Phase" : L"2×2 相位", -1, &qhdr, DT_CENTER | DT_VCENTER | DT_SINGLELINE);

    static const wchar_t* kQNames[2][2] = {{L"0,0", L"0,1"}, {L"1,0", L"1,1"}};
    for (int r = 0; r < 2; ++r) {
        for (int c = 0; c < 2; ++c) {
            float cx = qx0 + (float)c * (qcs + qgp);
            float cy = qy0 + (float)r * (qcs + qgp);
            bool on = quad_hit[r][c];
            gdip_round_rect(g, cx, cy, qcs, qcs, (float)g_app.S(4),
                            gdip_col(on ? kColAccent : RGB(230, 236, 246)),
                            gdip_col(on ? kColAccentDark : kColInputEdge), 1.0f);
            SetTextColor(hdc, on ? RGB(255, 255, 255) : kColTextMuted);
            RECT qr{(int)cx, (int)cy, (int)(cx + qcs), (int)(cy + qcs)};
            DrawTextW(hdc, kQNames[r][c], -1, &qr, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        }
    }
    wchar_t qsum[64] = {0};
    std::swprintf(qsum, 64, en ? L"%d / 4 Quads" : L"%d / 4 象限", quad_cnt);
    SetTextColor(hdc, quad_cnt >= 3 ? kColAccentDark : kColTextMuted);
    RECT qftr{(int)qx0 - g_app.S(10), (int)(qy0 + 2.0f * (qcs + qgp) + g_app.S(2)),
              pw - g_app.S(6), (int)(qy0 + 2.0f * (qcs + qgp) + g_app.S(22))};
    DrawTextW(hdc, qsum, -1, &qftr, DT_CENTER | DT_VCENTER | DT_SINGLELINE);

    SetTextColor(hdc, (grp && grp->shutter_shock) ? kColWarnAmber : kColTextMuted);
    RECT shRc{g_app.S(14), h - g_app.S(26), pw - g_app.S(12), h - g_app.S(6)};
    std::wstring shTxt = grp ? grp->shutter_summary : heraw_i18n::tr_w("t4.traj_empty");
    DrawTextW(hdc, shTxt.c_str(), -1, &shRc, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);

    COLORREF gbFill = g_app.optics.measured
                    ? (g_app.optics.is_star_gold ? RGB(248, 240, 214) : kColBadgeFill)
                    : RGB(232, 238, 248);
    COLORREF gbText = g_app.optics.measured
                    ? (g_app.optics.is_star_gold ? RGB(148, 102, 18) : kColAccentDark)
                    : kColTextMuted;
    gdip_round_rect(g, (float)gBadgeX, (float)g_app.S(9), (float)gBadgeW, (float)g_app.S(20),
                    (float)g_app.S(5), gdip_col(gbFill), gdip_col(kColBadgeEdge), 1.0f);
    SelectObject(hdc, g_app.hFontSmall);
    SetTextColor(hdc, gbText);
    RECT gbr{gBadgeX, g_app.S(9), gBadgeX + gBadgeW, g_app.S(29)};
    DrawTextW(hdc, g_app.optics.grade_badge.c_str(), -1, &gbr, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);

    float rWellL = (float)rx0 + (float)g_app.S(14);
    float rWellT = (float)g_app.S(34);
    float rWellW = std::max(40.0f, (float)rw - (float)g_app.S(28));
    float rWellH = std::max(40.0f, (float)h - (float)g_app.S(64));
    gdip_round_rect(g, rWellL, rWellT, rWellW, rWellH, (float)g_app.S(6),
                    gdip_col(RGB(255, 255, 255)), gdip_col(kColSubBorder), 1.0f);

    float rCx = (float)rx0 + 0.5f * (float)rw;
    float rCy = rWellT + 0.5f * rWellH;
    float dxC = (float)rw * 0.27f;
    float dyC = std::max(16.0f, rWellH * 0.23f);

    Gdiplus::PointF zPos[5] = {
        Gdiplus::PointF(rCx,       rCy),
        Gdiplus::PointF(rCx - dxC, rCy - dyC),
        Gdiplus::PointF(rCx + dxC, rCy - dyC),
        Gdiplus::PointF(rCx - dxC, rCy + dyC),
        Gdiplus::PointF(rCx + dxC, rCy + dyC)
    };
    static const wchar_t* kZNames[5] = {L"Center", L"NW", L"NE", L"SW", L"SE"};

    Gdiplus::Pen fldPen(gdip_col(RGB(204, 216, 234)), 1.0f);
    fldPen.SetDashPattern(dashPat, 2);
    g.DrawRectangle(&fldPen, rCx - dxC - 16.0f, rCy - dyC - 14.0f, 2.0f * (dxC + 16.0f), 2.0f * (dyC + 14.0f));
    g.DrawLine(&fldPen, zPos[1], zPos[4]);
    g.DrawLine(&fldPen, zPos[2], zPos[3]);

    if (g_app.optics.measured) {
        int order[4] = {1, 2, 4, 3};
        Gdiplus::PointF poly[4];
        for (int k = 0; k < 4; ++k) {
            int zi = order[k];
            float sc = g_app.optics.zone_rejected[zi] ? 0.35f
                     : (float)std::max(0.25, std::min(1.0, g_app.optics.zone_mtf50[zi] / 0.45));
            poly[k] = Gdiplus::PointF(rCx + (zPos[zi].X - rCx) * sc, rCy + (zPos[zi].Y - rCy) * sc);
        }
        Gdiplus::SolidBrush pFill(gdip_col(kColAccent, 38));
        Gdiplus::Pen pEdge(gdip_col(kColAccent, 185), 1.6f);
        g.FillPolygon(&pFill, poly, 4);
        g.DrawPolygon(&pEdge, poly, 4);
    }

    std::wstring rej_lbl = heraw_i18n::tr_w("t4.zone_rej");
    for (int z = 0; z < 5; ++z) {
        bool rej = g_app.optics.measured && g_app.optics.zone_rejected[z];
        double mtf = g_app.optics.zone_mtf50[z];
        double ast = g_app.optics.zone_astig[z];
        float baseR = (float)g_app.S(z == 0 ? 18 : 14);
        float rxE = baseR * (float)(1.0 + std::min(0.45, ast * 2.0));
        float ryE = baseR * (float)std::max(0.55, 1.0 - std::min(0.40, ast * 1.5));

        COLORREF zFill = !g_app.optics.measured ? RGB(228, 236, 248)
                       : rej ? RGB(236, 238, 244) : RGB(214, 228, 246);
        COLORREF zEdge = !g_app.optics.measured ? kColBadgeEdge
                       : rej ? RGB(190, 196, 208) : kColAccent;
        Gdiplus::SolidBrush eBr(gdip_col(zFill));
        Gdiplus::Pen ePen(gdip_col(zEdge), z == 0 ? 1.8f : 1.35f);
        g.FillEllipse(&eBr, zPos[z].X - rxE, zPos[z].Y - ryE, 2.0f * rxE, 2.0f * ryE);
        g.DrawEllipse(&ePen, zPos[z].X - rxE, zPos[z].Y - ryE, 2.0f * rxE, 2.0f * ryE);

        wchar_t ztxt[64] = {0};
        if (!g_app.optics.measured) {
            std::swprintf(ztxt, 64, L"%ls --", kZNames[z]);
        } else if (rej) {
            std::swprintf(ztxt, 64, L"%ls %ls", kZNames[z], rej_lbl.c_str());
        } else {
            std::swprintf(ztxt, 64, L"%ls %.2f", kZNames[z], mtf);
        }
        SelectObject(hdc, z == 0 ? g_app.hFontBold : g_app.hFontSmall);
        SetTextColor(hdc, rej ? kColTextMuted : kColTextMain);
        int ly = (z == 1 || z == 2) ? (int)(zPos[z].Y - ryE - g_app.S(18))
                                    : (int)(zPos[z].Y + ryE + g_app.S(2));
        RECT zr{(int)zPos[z].X - g_app.S(48), ly, (int)zPos[z].X + g_app.S(48), ly + g_app.S(18)};
        DrawTextW(hdc, ztxt, -1, &zr, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    }

    SelectObject(hdc, g_app.hFontSmall);
    SetTextColor(hdc, kColTextMuted);
    std::wstring lcaStr;
    if (g_app.optics.measured) {
        char red[24] = "--", blue[24] = "--";
        if (g_app.optics.lca_valid[0]) std::snprintf(red, sizeof(red), "%.2fpx", g_app.optics.lca_edge_px[0]);
        if (g_app.optics.lca_valid[1]) std::snprintf(blue, sizeof(blue), "%.2fpx", g_app.optics.lca_edge_px[1]);
        lcaStr = L"cycles/raw-pixel · " + heraw_i18n::trf_w("t4.radar_l2_fmt", red, blue);
    } else {
        lcaStr = heraw_i18n::tr_w("t4.radar_l1_idle");
    }
    RECT rftr{rx0 + g_app.S(14), h - g_app.S(26), w - g_app.S(12), h - g_app.S(6)};
    DrawTextW(hdc, lcaStr.c_str(), -1, &rftr, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);

    SelectObject(hdc, of);
}

static bool export_optics_card_png(const std::wstring& out_png_path) {
    if (file_exists_regular(g_app.optics.passport_path)) {
        if (_wcsicmp(out_png_path.c_str(), g_app.optics.passport_path.c_str()) == 0) return true;
        return CopyFileW(g_app.optics.passport_path.c_str(), out_png_path.c_str(), FALSE) != FALSE;
    }
    int cw = g_app.S(920);
    int ch = g_app.S(420);
    HDC hScr = GetDC(nullptr);
    HDC hMem = CreateCompatibleDC(hScr);
    BITMAPINFO bmi{};
    bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth = cw;
    bmi.bmiHeader.biHeight = -ch;
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = 32;
    bmi.bmiHeader.biCompression = BI_RGB;
    void* bits = nullptr;
    HBITMAP hBmp = CreateDIBSection(hScr, &bmi, DIB_RGB_COLORS, &bits, nullptr, 0);
    if (!hBmp || !bits) {
        DeleteDC(hMem);
        ReleaseDC(nullptr, hScr);
        return false;
    }
    HGDIOBJ old = SelectObject(hMem, hBmp);
    render_optics_diagnostics_dc(hMem, cw, ch);
    GdiFlush();
    auto* px = static_cast<BYTE*>(bits);
    for (size_t i = 0; i < (size_t)cw * (size_t)ch; ++i) px[i * 4 + 3] = 255;

    bool ok = false;
    CLSID pngClsid{};
    if (get_png_encoder_clsid(&pngClsid)) {
        Gdiplus::Bitmap b(cw, ch, cw * 4, PixelFormat32bppARGB, px);
        ok = (b.Save(out_png_path.c_str(), &pngClsid, nullptr) == Gdiplus::Ok);
    }
    SelectObject(hMem, old);
    DeleteObject(hBmp);
    DeleteDC(hMem);
    ReleaseDC(nullptr, hScr);
    return ok;
}

static LRESULT CALLBACK NijiOpticsCanvasWndProc(HWND hw, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
        case WM_ERASEBKGND: return 1;
        case WM_PAINT: {
            PAINTSTRUCT ps{};
            HDC hdc = BeginPaint(hw, &ps);
            RECT rc{};
            GetClientRect(hw, &rc);
            HDC mem = CreateCompatibleDC(hdc);
            HBITMAP bmp = CreateCompatibleBitmap(hdc, std::max(1L, rc.right), std::max(1L, rc.bottom));
            HGDIOBJ old = SelectObject(mem, bmp);
            render_optics_diagnostics_dc(mem, rc.right, rc.bottom);
            BitBlt(hdc, 0, 0, rc.right, rc.bottom, mem, 0, 0, SRCCOPY);
            SelectObject(mem, old);
            DeleteObject(bmp);
            DeleteDC(mem);
            EndPaint(hw, &ps);
            return 0;
        }
        case WM_PRINT:
        case WM_PRINTCLIENT: {
            RECT rc{};
            GetClientRect(hw, &rc);
            render_optics_diagnostics_dc(reinterpret_cast<HDC>(wParam), rc.right, rc.bottom);
            return 0;
        }
    }
    return DefWindowProcW(hw, msg, wParam, lParam);
}

// ============================================================================
// Presets, Cross-Tab Synchronization, Affordance Dimming & Tab Switching
// ============================================================================
static bool is_depth_gate_overridden_win() {
    if (!g_app.hCmbDepthGate) return false;
    int sel = (int)SendMessageW(g_app.hCmbDepthGate, CB_GETCURSEL, 0, 0);
    if (sel > 0) return true;
    return !trim_ws(get_wnd_text(g_app.hEdtDepthDisp)).empty() ||
           !trim_ws(get_wnd_text(g_app.hEdtDepthCoh)).empty() ||
           !trim_ws(get_wnd_text(g_app.hEdtDepthSup)).empty();
}

static bool is_intake_nondefault_win() {
    return (g_app.decoder_mode != L"auto") ||
           (!g_app.nev_frames_spec.empty() && g_app.nev_frames_spec != L"all") ||
           (std::fabs(_wtof(g_app.burst_gap_s.c_str()) - 3.5) > 1e-3) ||
           (std::fabs(_wtof(g_app.min_rel_sharpness.c_str()) - 0.72) > 1e-3) ||
           (std::fabs(_wtof(g_app.max_shift_px.c_str()) - 120.0) > 1e-2) ||
           (g_app.lucky_mode != L"auto") ||
           (_wtoi(g_app.max_cluster_frames.c_str()) != 128);
}

static void refresh_intake_btn_badge_win() {
    if (!g_app.hBtnIntakeParams) return;
    std::wstring lbl = heraw_i18n::tr_w("dock.intake_btn");
    if (is_intake_nondefault_win()) lbl += L" •";
    if (get_wnd_text(g_app.hBtnIntakeParams) != lbl) {
        SetWindowTextW(g_app.hBtnIntakeParams, lbl.c_str());
        InvalidateRect(g_app.hBtnIntakeParams, nullptr, FALSE);
    }
}

static void update_field_bank_visibility_win() {
    int bank = g_app.hSegFieldBank ? seg_pill_get_sel(g_app.hSegFieldBank) : 0;
    bool on_tab2 = (g_app.active_tab == 2);
    for (int i = 0; i < kNumFields; ++i) {
        if (g_app.hChkFields[i]) {
            ShowWindow(g_app.hChkFields[i], (on_tab2 && bank == 0) ? SW_SHOW : SW_HIDE);
        }
    }
    for (int i = 0; i < kNumExtFields; ++i) {
        if (g_app.hChkExtFields[i]) {
            ShowWindow(g_app.hChkExtFields[i], (on_tab2 && bank == 1) ? SW_SHOW : SW_HIDE);
        }
    }
}

static std::wstring build_fields_csv() {
    std::wstring csv;
    for (int i = 0; i < kNumFields; ++i) {
        if (get_chk(g_app.hChkFields[i])) {
            if (!csv.empty()) csv += L",";
            csv += kFieldSpecs[i].code;
        }
    }
    for (int i = 0; i < kNumExtFields; ++i) {
        if (get_chk(g_app.hChkExtFields[i])) {
            if (!csv.empty()) csv += L",";
            csv += kExtFieldSpecs[i].code;
        }
    }
    return csv;
}

static std::wstring build_effective_fields_csv(const BurstGroupItem* grp) {
    bool force_gate = is_depth_gate_overridden_win();
    std::wstring csv;
    for (int i = 0; i < kNumFields; ++i) {
        if (!get_chk(g_app.hChkFields[i])) continue;
        if (grp && !force_gate) {
            auto it = grp->field_observability.find(wide_to_utf8(kFieldSpecs[i].code));
            if (it != grp->field_observability.end() && !it->second) continue;
        }
        if (!csv.empty()) csv += L",";
        csv += kFieldSpecs[i].code;
    }
    for (int i = 0; i < kNumExtFields; ++i) {
        if (!get_chk(g_app.hChkExtFields[i])) continue;
        if (grp && !force_gate) {
            auto it = grp->field_observability.find(wide_to_utf8(kExtFieldSpecs[i].code));
            if (it != grp->field_observability.end() && !it->second) continue;
        }
        if (!csv.empty()) csv += L",";
        csv += kExtFieldSpecs[i].code;
    }
    return csv;
}

static std::wstring build_lcp_fields_csv() {
    std::wstring csv;
    if (get_chk(g_app.hChkLcpLca)) csv = L"lca";
    if (get_chk(g_app.hChkLcpVig)) csv += csv.empty() ? L"vignette" : L",vignette";
    if (get_chk(g_app.hChkLcpDist)) csv += csv.empty() ? L"distortion" : L",distortion";
    return csv;
}

static std::vector<std::wstring> lcp_arguments(const std::wstring& path) {
    return {L"--optics-only", L"--export-lcp", path, L"--lcp-fields", build_lcp_fields_csv()};
}

static std::vector<std::wstring> refocus_arguments() {
    double offset = std::wcstod(get_wnd_text(g_app.hEdtRefocusPos).c_str(), nullptr);
    double strength = std::wcstod(get_wnd_text(g_app.hEdtBokehStr).c_str(), nullptr);
    bool auto_pos = g_app.hChkRefocusAuto && get_chk(g_app.hChkRefocusAuto);
    std::wstring pos_arg = auto_pos ? L"auto" : std::to_wstring(std::clamp((offset + 1.0) * 0.5, 0.0, 1.0));
    std::vector<std::wstring> out = {
        L"--export-refocus",
        L"--refocus-pos", pos_arg,
        L"--bokeh-strength", std::to_wstring(12.0 * strength)
    };
    if (g_app.hCmbRefocusMode) {
        std::wstring rmode = selected_cmb_code(g_app.hCmbRefocusMode, kRefocusModeCodes, L"auto");
        if (rmode != L"auto") {
            out.push_back(L"--refocus-mode");
            out.push_back(rmode);
        }
    }
    if (g_app.hEdtBokehStf) {
        std::wstring stf_s = trim_ws(get_wnd_text(g_app.hEdtBokehStf));
        if (!stf_s.empty() && _wtof(stf_s.c_str()) > 1e-4) {
            out.push_back(L"--bokeh-stf");
            out.push_back(stf_s);
        }
    }
    if (g_app.hEdtBokehSa) {
        std::wstring sa_s = trim_ws(get_wnd_text(g_app.hEdtBokehSa));
        double sa_v = _wtof(sa_s.c_str());
        if (!sa_s.empty() && std::fabs(sa_v) > 1e-4) {
            wchar_t b[32] = {0};
            std::swprintf(b, 32, L"%.2f", std::clamp(sa_v, -0.5, 0.5));
            out.push_back(L"--bokeh-sa");
            out.push_back(b);
        }
    }
    if (g_app.hEdtVirtualFstop) {
        double fv = _wtof(trim_ws(get_wnd_text(g_app.hEdtVirtualFstop)).c_str());
        if (fv > 0.05) {
            wchar_t b[32] = {0};
            std::swprintf(b, 32, L"%.2f", fv);
            out.push_back(L"--virtual-fstop");
            out.push_back(b);
        }
    }
    if (g_app.hCmbBokehBlades) {
        std::wstring bl = selected_cmb_code(g_app.hCmbBokehBlades, kBokehBladeCodes, L"0");
        if (bl != L"0") {
            out.push_back(L"--bokeh-blades");
            out.push_back(bl);
            double ang = _wtof(trim_ws(get_wnd_text(g_app.hEdtBokehAngle)).c_str());
            if (std::fabs(ang) > 1e-3) {
                wchar_t b[32] = {0};
                std::swprintf(b, 32, L"%.1f", ang);
                out.push_back(L"--bokeh-angle");
                out.push_back(b);
            }
        }
    }
    if (g_app.hEdtTiltX && g_app.hEdtTiltY) {
        double tx = _wtof(trim_ws(get_wnd_text(g_app.hEdtTiltX)).c_str());
        double ty = _wtof(trim_ws(get_wnd_text(g_app.hEdtTiltY)).c_str());
        if (std::fabs(tx) > 1e-4 || std::fabs(ty) > 1e-4) {
            wchar_t b[64] = {0};
            std::swprintf(b, 64, L"%.2f,%.2f", tx, ty);
            out.push_back(L"--tilt-plane");
            out.push_back(b);
        }
    }
    if (g_app.hCmbSeeThrough) {
        std::wstring st = selected_cmb_code(g_app.hCmbSeeThrough, kSeeThroughCodes, L"auto");
        if (st != L"auto") {
            out.push_back(L"--see-through");
            out.push_back(st);
        }
    }
    return out;
}

static int count_checked_observers_win() {
    int c = 0;
    for (int i = 0; i < kNumObs; ++i) if (get_chk(g_app.hChkObs[i])) ++c;
    return c;
}

static std::wstring build_observers_csv_win() {
    std::wstring csv;
    for (int i = 0; i < kNumObs; ++i) {
        if (get_chk(g_app.hChkObs[i])) {
            if (!csv.empty()) csv += L",";
            csv += kObsSpecs[i].code;
        }
    }
    return csv;
}

static void apply_observers_csv_win(const std::wstring& csv) {
    for (int i = 0; i < kNumObs; ++i) set_chk_val(g_app.hChkObs[i], false);
    std::wstring trimmed = trim_ws(csv);
    if (trimmed.empty()) return;
    if (trimmed == L"all") {
        for (int i = 0; i < kNumObs; ++i) set_chk_val(g_app.hChkObs[i], true);
        return;
    }
    std::wstring padded = L"," + trimmed + L",";
    auto map_legacy = [](const std::wstring& tok) -> int {
        if (tok == L"loca_sign") return 5;
        if (tok == L"modal_wave") return 6;
        if (tok == L"seeing") return 7;
        if (tok == L"refraction") return 8;
        if (tok == L"streak_astrometry") return 9;
        return -1;
    };
    for (int i = 0; i < kNumObs; ++i) {
        std::wstring tok = std::wstring(L",") + kObsSpecs[i].code + L",";
        if (padded.find(tok) != std::wstring::npos) set_chk_val(g_app.hChkObs[i], true);
    }
    for (const wchar_t* leg : {L"loca_sign", L"modal_wave", L"seeing", L"refraction", L"streak_astrometry"}) {
        if (padded.find(std::wstring(L",") + leg + L",") != std::wstring::npos) {
            int idx = map_legacy(leg);
            if (idx >= 0) set_chk_val(g_app.hChkObs[idx], true);
        }
    }
}

static std::wstring active_bus_in_for_group_win(const BurstGroupItem* grp) {
    if (!grp) return trim_ws(g_app.default_bus_in_path);
    for (const auto& qg : g_app.groups) {
        if (&qg == grp) return trim_ws(grp->bus_in_path);
    }
    std::wstring own = trim_ws(grp->bus_in_path);
    return !own.empty() ? own : trim_ws(g_app.default_bus_in_path);
}

static void commit_bus_in_edit_win(const std::wstring& raw_val) {
    std::wstring trimmed = trim_ws(raw_val);
    BurstGroupItem* grp = current_group_ptr();
    if (grp) {
        grp->bus_in_path = trimmed;
        if (g_app.hBurstView) InvalidateRect(g_app.hBurstView, nullptr, FALSE);
    } else {
        g_app.default_bus_in_path = trimmed;
    }
    if (g_app.hEdtBusIn && get_wnd_text(g_app.hEdtBusIn) != trimmed) {
        g_app.suppress_sync = true;
        SetWindowTextW(g_app.hEdtBusIn, trimmed.c_str());
        g_app.suppress_sync = false;
    }
}

static void reset_optics_controls_win() {
    if (g_app.hCmbDiffusionMode) SendMessageW(g_app.hCmbDiffusionMode, CB_SETCURSEL, 0, 0);
    set_tuned_pair(g_app.hSldDiffStrength, g_app.hEdtDiffStrength, L"1.00");
    if (g_app.hCmbSpectralCa) SendMessageW(g_app.hCmbSpectralCa, CB_SETCURSEL, 0, 0);
    set_tuned_pair(g_app.hSldSpectralPwr, g_app.hEdtSpectralPwr, L"0.00");
    if (g_app.hCmbSpectralAng) SendMessageW(g_app.hCmbSpectralAng, CB_SETCURSEL, 0, 0);
    if (g_app.hEdtSpectralDeg) SetWindowTextW(g_app.hEdtSpectralDeg, L"0.0");
    set_tuned_pair(g_app.hSldBokehDeNisen, g_app.hEdtBokehDeNisen, L"0.00");
    set_tuned_pair(g_app.hSldBokehSwirl,   g_app.hEdtBokehSwirl,   L"0.00");
    set_tuned_pair(g_app.hSldBokehAspect,  g_app.hEdtBokehAspect,  L"1.00");
    set_tuned_pair(g_app.hSldBokehOnion,   g_app.hEdtBokehOnion,   L"0.00");
}

static int count_checked_fields() {
    int c = 0;
    for (int i = 0; i < kNumFields; ++i) if (get_chk(g_app.hChkFields[i])) ++c;
    for (int i = 0; i < kNumExtFields; ++i) if (get_chk(g_app.hChkExtFields[i])) ++c;
    return c;
}

static void apply_fields_csv(const std::wstring& csv) {
    if (csv == L"none") {
        for (int i = 0; i < kNumFields; ++i) set_chk_val(g_app.hChkFields[i], false);
        for (int i = 0; i < kNumExtFields; ++i) set_chk_val(g_app.hChkExtFields[i], false);
        return;
    }
    if (csv.empty()) return;
    if (csv == L"all") {
        for (int i = 0; i < kNumFields; ++i) set_chk_val(g_app.hChkFields[i], true);
        for (int i = 0; i < kNumExtFields; ++i) set_chk_val(g_app.hChkExtFields[i], true);
        return;
    }
    std::wstring padded = L"," + csv + L",";
    for (int i = 0; i < kNumFields; ++i) {
        std::wstring tok = std::wstring(L",") + kFieldSpecs[i].code + L",";
        set_chk_val(g_app.hChkFields[i], padded.find(tok) != std::wstring::npos);
    }
    for (int i = 0; i < kNumExtFields; ++i) {
        std::wstring tok = std::wstring(L",") + kExtFieldSpecs[i].code + L",";
        set_chk_val(g_app.hChkExtFields[i], padded.find(tok) != std::wstring::npos);
    }
}

static void set_ibp_iters_win(int iters) {
    int clamped = std::clamp(iters, 0, 2);
    if (g_app.hCmbIbpIters) SendMessageW(g_app.hCmbIbpIters, CB_SETCURSEL, (WPARAM)clamped, 0);
    set_chk_val(g_app.hChkIbpOn, clamped > 0);
}

static int get_ibp_iters_win() {
    if (g_app.hCmbIbpIters) {
        int sel = (int)SendMessageW(g_app.hCmbIbpIters, CB_GETCURSEL, 0, 0);
        if (sel >= 0 && sel <= 2) return sel;
    }
    return get_chk(g_app.hChkIbpOn) ? 1 : 0;
}

static void apply_preset_to_controls(int idx) {
    idx = std::max(0, std::min(3, idx));
    g_app.preset_idx = idx;
    if (g_app.hSegPreset) seg_pill_set_sel(g_app.hSegPreset, idx);

    if (idx == 1) { // 星空 astro
        set_tuned_pair(g_app.hSldDenoise,     g_app.hEdtDenoise,     L"0.85");
        set_tuned_pair(g_app.hSldChroma,      g_app.hEdtChroma,      L"0.90");
        set_tuned_pair(g_app.hSldPsfIters,    g_app.hEdtPsfIters,    L"3");
        set_tuned_pair(g_app.hSldRgbPsfIters, g_app.hEdtRgbPsfIters, L"1");
        set_tuned_pair(g_app.hSldPsfSigma,    g_app.hEdtPsfSigma,    L"0.95");
        set_tuned_pair(g_app.hSldDeghost,     g_app.hEdtDeghost,     L"0.85");
        set_tuned_pair(g_app.hSldEv,          g_app.hEdtEv,          L"0.0");
        set_chk_val(g_app.hChkSiliconClean, true);
        set_ibp_iters_win(1);
        set_chk_val(g_app.hChkEdgePolish,   false);
        set_chk_val(g_app.hChkAutoLca,      true);
        set_chk_val(g_app.hChkAutoExp,      true);
        set_chk_val(g_app.hChkRolloff,      true);
        set_chk_val(g_app.hChkDither,       true);
    } else if (idx == 2) { // 科学 sci
        set_tuned_pair(g_app.hSldDenoise,     g_app.hEdtDenoise,     L"0.40");
        set_tuned_pair(g_app.hSldChroma,      g_app.hEdtChroma,      L"0.55");
        set_tuned_pair(g_app.hSldPsfIters,    g_app.hEdtPsfIters,    L"0");
        set_tuned_pair(g_app.hSldRgbPsfIters, g_app.hEdtRgbPsfIters, L"0");
        set_tuned_pair(g_app.hSldPsfSigma,    g_app.hEdtPsfSigma,    L"1.05");
        set_tuned_pair(g_app.hSldDeghost,     g_app.hEdtDeghost,     L"1.35");
        set_tuned_pair(g_app.hSldEv,          g_app.hEdtEv,          L"0.0");
        set_chk_val(g_app.hChkSiliconClean, true);
        set_ibp_iters_win(1);
        set_chk_val(g_app.hChkEdgePolish,   false);
        set_chk_val(g_app.hChkAutoLca,      false);
        set_chk_val(g_app.hChkAutoExp,      false);
        set_chk_val(g_app.hChkRolloff,      false);
        set_chk_val(g_app.hChkDither,       true);
    } else if (idx == 3) { // 归档 arc
        set_tuned_pair(g_app.hSldDenoise,     g_app.hEdtDenoise,     L"0.70");
        set_tuned_pair(g_app.hSldChroma,      g_app.hEdtChroma,      L"0.85");
        set_tuned_pair(g_app.hSldPsfIters,    g_app.hEdtPsfIters,    L"0");
        set_tuned_pair(g_app.hSldRgbPsfIters, g_app.hEdtRgbPsfIters, L"0");
        set_tuned_pair(g_app.hSldPsfSigma,    g_app.hEdtPsfSigma,    L"1.05");
        set_tuned_pair(g_app.hSldDeghost,     g_app.hEdtDeghost,     L"1.10");
        set_tuned_pair(g_app.hSldEv,          g_app.hEdtEv,          L"0.0");
        set_chk_val(g_app.hChkSiliconClean, true);
        set_ibp_iters_win(1);
        set_chk_val(g_app.hChkEdgePolish,   false);
        set_chk_val(g_app.hChkAutoLca,      true);
        set_chk_val(g_app.hChkAutoExp,      true);
        set_chk_val(g_app.hChkRolloff,      true);
        set_chk_val(g_app.hChkDither,       true);
    } else { // 照片 photo
        set_tuned_pair(g_app.hSldDenoise,     g_app.hEdtDenoise,     L"1.00");
        set_tuned_pair(g_app.hSldChroma,      g_app.hEdtChroma,      L"1.00");
        set_tuned_pair(g_app.hSldPsfIters,    g_app.hEdtPsfIters,    L"4");
        set_tuned_pair(g_app.hSldRgbPsfIters, g_app.hEdtRgbPsfIters, L"2");
        set_tuned_pair(g_app.hSldPsfSigma,    g_app.hEdtPsfSigma,    L"1.05");
        set_tuned_pair(g_app.hSldDeghost,     g_app.hEdtDeghost,     L"1.00");
        set_tuned_pair(g_app.hSldEv,          g_app.hEdtEv,          L"0.0");
        set_chk_val(g_app.hChkSiliconClean, true);
        set_ibp_iters_win(1);
        set_chk_val(g_app.hChkEdgePolish,   true);
        set_chk_val(g_app.hChkAutoLca,      true);
        set_chk_val(g_app.hChkAutoExp,      true);
        set_chk_val(g_app.hChkRolloff,      true);
        set_chk_val(g_app.hChkDither,       true);
    }
}

static void switch_tab(int tab_idx) {
    tab_idx = std::max(0, std::min(2, tab_idx));
    g_app.active_tab = tab_idx;
    if (g_app.hTabBar) seg_pill_set_sel(g_app.hTabBar, tab_idx);

    for (const auto& tc : g_app.tab_ctrls) {
        if (tc.tab >= 0 && tc.hw) {
            ShowWindow(tc.hw, (tc.tab == tab_idx) ? SW_SHOW : SW_HIDE);
        }
    }
    update_field_bank_visibility_win();
    sync_cross_tab_ui();
    if (g_app.hwnd) {
        RECT crc{};
        GetClientRect(g_app.hwnd, &crc);
        RECT stage{g_app.S(422), g_app.S(62), crc.right, crc.bottom - g_app.S(68)};
        InvalidateRect(g_app.hwnd, &stage, FALSE);
    }
}

static void update_grouping_hint() {
    if (!g_app.hLblGroupingHint) return;
    auto S = [](int px) { return g_app.S(px); };
    if (g_app.grouping_dirty) {
        if (g_app.hBtnIntakeAction) ShowWindow(g_app.hBtnIntakeAction, SW_HIDE);
        MoveWindow(g_app.hLblGroupingHint, S(30), S(140), S(372), S(34), TRUE);
        SetWindowTextW(g_app.hLblGroupingHint, heraw_i18n::tr_w("hint.dirty").c_str());
        return;
    }

    int min_req = min_valid_frames_win();
    size_t total = 0, valid_groups = 0, checked_cnt = 0, stray_frames = 0, size_items = 0;
    std::wstring sizes;
    const wchar_t* curPresetCode = kPresetCodes[std::max(0, std::min(3, g_app.preset_idx))];
    int rx_idx = -1;

    for (size_t gi = 0; gi < g_app.groups.size(); ++gi) {
        const auto& group = g_app.groups[gi];
        total += group.frames.size();
        if (group.is_stray_archive) {
            stray_frames += group.frames.size();
        } else {
            if (group.valid_frame_count >= min_req) ++valid_groups;
            else stray_frames += group.frames.size();
            if (size_items < 6) {
                if (!sizes.empty()) sizes += L"+";
                sizes += std::to_wstring(group.frames.size());
                ++size_items;
            }
        }
        if (group.checked) {
            ++checked_cnt;
            if (rx_idx < 0 && !group.is_stray_archive && group.has_telemetry && !group.rec_preset.empty() &&
                group.rec_preset != "photo" && utf8_to_wide(group.rec_preset) != curPresetCode) {
                rx_idx = (int)gi;
            }
        }
    }
    if (g_app.groups.size() > size_items && (g_app.groups.empty() || !g_app.groups.back().is_stray_archive)) {
        sizes += L"+…";
    }

    bool show_pill = false;
    std::wstring pill_txt;
    if (total > 0) {
        if (checked_cnt == 0) {
            pill_txt = (valid_groups > 0) ? heraw_i18n::trf_w("pill.check_n_fmt", (int)valid_groups)
                                          : heraw_i18n::tr_w("pill.fallback_fuse");
            show_pill = true;
        } else if (rx_idx >= 0) {
            const auto& rxg = g_app.groups[(size_t)rx_idx];
            pill_txt = (rxg.rec_preset == "astro") ? heraw_i18n::tr_w("pill.rx_astro")
                     : (rxg.rec_preset == "sci")   ? heraw_i18n::tr_w("pill.rx_sci") : heraw_i18n::tr_w("pill.rx_arc");
            show_pill = true;
        } else if (valid_groups >= 2 && checked_cnt >= 2) {
            pill_txt = heraw_i18n::tr_w("pill.only_best");
            show_pill = true;
        } else if (valid_groups >= 2 && checked_cnt == 1) {
            pill_txt = heraw_i18n::trf_w("pill.check_all_fmt", (int)valid_groups);
            show_pill = true;
        }
    }
    if (g_app.hBtnIntakeAction) {
        if (show_pill) {
            SetWindowTextW(g_app.hBtnIntakeAction, pill_txt.c_str());
            MoveWindow(g_app.hLblGroupingHint, S(30), S(140), S(252), S(34), TRUE);
            MoveWindow(g_app.hBtnIntakeAction, S(286), S(145), S(116), S(24), TRUE);
            ShowWindow(g_app.hBtnIntakeAction, SW_SHOW);
            InvalidateRect(g_app.hBtnIntakeAction, nullptr, FALSE);
        } else {
            ShowWindow(g_app.hBtnIntakeAction, SW_HIDE);
            MoveWindow(g_app.hLblGroupingHint, S(30), S(140), S(372), S(34), TRUE);
        }
    }

    std::string sizes_u8 = wide_to_utf8(sizes);
    std::wstring result = heraw_i18n::tr_w("hint.idle_win");
    if (total > 0) {
        if (stray_frames > 0 && valid_groups > 0) {
            result = heraw_i18n::trf_w("hint.summary_stray_fmt", (int)total, (int)valid_groups, sizes_u8.c_str(), (int)stray_frames);
        } else {
            result = heraw_i18n::trf_w("hint.summary_all_fmt", (int)total, (int)g_app.groups.size(), sizes_u8.c_str());
        }
    }

    int gm = (int)SendMessageW(g_app.hCmbGrouping, CB_GETCURSEL, 0, 0);
    std::string target_s = wide_to_utf8(get_wnd_text(g_app.hEdtGrpSz));
    if (total > 0) {
        result += L"\n";
        if (gm == 1) {
            result += heraw_i18n::tr_w("hint.mode_single_win");
        } else if (gm == 2) {
            result += heraw_i18n::trf_w("hint.mode_best_fmt", target_s.c_str());
        } else if (gm == 3) {
            result += heraw_i18n::tr_w("hint.mode_top1");
        } else if (checked_cnt == 0 && valid_groups == 0) {
            result += heraw_i18n::trf_w("hint.mode_auto_short_fmt", min_req);
        } else {
            result += heraw_i18n::trf_w("hint.mode_auto_ok_fmt", target_s.c_str(), min_req);
        }
    }
    SetWindowTextW(g_app.hLblGroupingHint, result.c_str());
}

static void relayout_right_dock_r3_context() {
    if (!g_app.hwnd || !g_app.hLblLiveReceipt) return;
    RECT crc{};
    GetClientRect(g_app.hwnd, &crc);
    if (crc.right <= 0 || crc.bottom <= 0) return;
    auto S = [](int px) { return g_app.S(px); };
    const int curRxDock = crc.right - S(18) - S(336);
    const int bottomBarY = crc.bottom - S(68);
    const int workBot = bottomBarY - S(12);
    const std::wstring rgb_code = selected_rgb_fmt_code();
    const bool is_jxs_fmt = (rgb_code == L"jxs");
    const bool is_exr_fmt = (rgb_code == L"exr");

    if (g_app.hLblBppCap)  ShowWindow(g_app.hLblBppCap,  is_jxs_fmt ? SW_SHOW : SW_HIDE);
    if (g_app.hEdtJxsBpp)  ShowWindow(g_app.hEdtJxsBpp,  is_jxs_fmt ? SW_SHOW : SW_HIDE);
    if (g_app.hCmbExrPrec) ShowWindow(g_app.hCmbExrPrec, is_exr_fmt ? SW_SHOW : SW_HIDE);
    if (g_app.hCmbExrParts)ShowWindow(g_app.hCmbExrParts,is_exr_fmt ? SW_SHOW : SW_HIDE);

    const int recTop = is_exr_fmt ? S(612) : S(584);
    const int lblTop = recTop + S(24);
    const int lblH   = std::max(S(30), workBot - S(12) - lblTop);
    MoveWindow(g_app.hLblLiveReceipt, curRxDock + S(22), lblTop, S(292), lblH, TRUE);
}

static void sync_cross_tab_ui() {
    if (g_app.suppress_sync || !g_app.hSegPreset) return;
    g_app.suppress_sync = true;

    int cur_min_req = min_valid_frames_win();
    if (cur_min_req != g_app.last_min_req) {
        int prev_min = g_app.last_min_req;
        g_app.last_min_req = cur_min_req;
        for (size_t gi = 0; gi < g_app.groups.size(); ++gi) {
            auto& gitem = g_app.groups[gi];
            recalculate_group_item(gitem, (int)gi + 1);
            if (!gitem.is_stray_archive && gitem.paths.size() == gitem.frames.size()) {
                if (cur_min_req < prev_min && gitem.valid_frame_count >= cur_min_req && gitem.valid_frame_count < prev_min) {
                    gitem.checked = true;
                } else if (cur_min_req > prev_min && gitem.valid_frame_count < cur_min_req) {
                    gitem.checked = false;
                }
            }
        }
        if (g_app.hBurstView) InvalidateRect(g_app.hBurstView, nullptr, FALSE);
    }

    BurstGroupItem* grp = current_group_ptr();
    int locked_f = grp ? grp->locked_anchor_1based : 0;

    set_chk_val(g_app.hRadT2MotStd,    g_app.motion_mode_idx <= 2);
    set_chk_val(g_app.hRadT2MotClean,  g_app.motion_mode_idx == 3);
    set_chk_val(g_app.hRadT2MotVnd,    g_app.motion_mode_idx == 4);
    set_chk_val(g_app.hRadT2MotStrobe, g_app.motion_mode_idx == 5);
    if (g_app.hRadT2MotFstack) set_chk_val(g_app.hRadT2MotFstack, g_app.motion_mode_idx == 6);
    if (g_app.motion_mode_idx <= 2) {
        seg_pill_set_sel(g_app.hSegT2Anchor, (locked_f > 0) ? 3 : g_app.motion_mode_idx);
    }
    seg_pill_set_item_text(g_app.hSegT2Anchor, 3,
        (locked_f > 0) ? heraw_i18n::trf_w("t2.pose_locked_fmt", locked_f) : heraw_i18n::tr_w("t2.pose_follow"));
    EnableWindow(g_app.hSegT2Anchor, g_app.motion_mode_idx <= 2);
    EnableWindow(g_app.hSldCleanSens, g_app.motion_mode_idx == 3);
    EnableWindow(g_app.hEdtCleanSens, g_app.motion_mode_idx == 3);
    EnableWindow(g_app.hSldVndThresh, g_app.motion_mode_idx == 4);
    EnableWindow(g_app.hEdtVndThresh, g_app.motion_mode_idx == 4);
    EnableWindow(g_app.hEdtStrobeStep, g_app.motion_mode_idx == 5);
    EnableWindow(g_app.hSegStrobeFade, g_app.motion_mode_idx == 5);

    bool emit_raw = get_chk(g_app.hChkRaw);
    bool emit_rgb = get_chk(g_app.hChkRgb);
    bool relay_on = get_chk(g_app.hChkRelayRaw);
    EnableWindow(g_app.hCmbRawFmt,   emit_raw);
    EnableWindow(g_app.hChkRelayRaw, emit_raw);

    bool bypass_stage5 = emit_raw && relay_on && !emit_rgb;
    EnableWindow(g_app.hSldDenoise,  !bypass_stage5);
    EnableWindow(g_app.hEdtDenoise,  !bypass_stage5);
    EnableWindow(g_app.hSldChroma,   !bypass_stage5);
    EnableWindow(g_app.hEdtChroma,   !bypass_stage5);
    EnableWindow(g_app.hSldPsfIters, !bypass_stage5);
    EnableWindow(g_app.hEdtPsfIters, !bypass_stage5);
    if (g_app.hSldRgbPsfIters) EnableWindow(g_app.hSldRgbPsfIters, !bypass_stage5);
    if (g_app.hEdtRgbPsfIters) EnableWindow(g_app.hEdtRgbPsfIters, !bypass_stage5);
    if (g_app.hChkEdgePolish)  EnableWindow(g_app.hChkEdgePolish,  !bypass_stage5);

    std::wstring rgb_code = selected_rgb_fmt_code();
    if (g_app.hSegRgbScale) EnableWindow(g_app.hSegRgbScale, emit_rgb);
    EnableWindow(g_app.hCmbRgbFmt,  emit_rgb);
    EnableWindow(g_app.hEdtJxsBpp,  emit_rgb && (rgb_code == L"jxs"));
    EnableWindow(g_app.hRadCsSrgb,  emit_rgb);
    EnableWindow(g_app.hRadCsAdobe, emit_rgb);

    bool emit_fld = get_chk(g_app.hChkField);
    int fm_sel = (int)SendMessageW(g_app.hCmbFieldMode, CB_GETCURSEL, 0, 0);
    if (fm_sel < 0 || fm_sel >= kNumFieldModes) fm_sel = 0;
    EnableWindow(g_app.hCmbFieldMode, emit_fld);

    if (emit_fld && fm_sel == 0) {
        bool is_tiff = (rgb_code == L"tiff-deflate" || rgb_code == L"tiff-hq" || rgb_code == L"tiff-raw");
        if (!emit_rgb) {
            set_chk_val(g_app.hChkRgb, true);
            emit_rgb = true;
            if (g_app.hSegRgbScale) EnableWindow(g_app.hSegRgbScale, TRUE);
            EnableWindow(g_app.hCmbRgbFmt, TRUE);
            SendMessageW(g_app.hCmbRgbFmt, CB_SETCURSEL, 0, 0);
            rgb_code = L"tiff-deflate";
            is_tiff = true;
        } else if (!is_tiff) {
            SendMessageW(g_app.hCmbFieldMode, CB_SETCURSEL, 1, 0);
            fm_sel = 1;
        }
        if (is_tiff) {
            EnableWindow(g_app.hRadCsSrgb, TRUE);
            EnableWindow(g_app.hRadCsAdobe, TRUE);
            EnableWindow(g_app.hEdtJxsBpp, FALSE);
            EnableWindow(g_app.hSldDenoise, TRUE);
            EnableWindow(g_app.hEdtDenoise, TRUE);
            EnableWindow(g_app.hSldChroma, TRUE);
            EnableWindow(g_app.hEdtChroma, TRUE);
            EnableWindow(g_app.hSldPsfIters, TRUE);
            EnableWindow(g_app.hEdtPsfIters, TRUE);
        }
    }
    relayout_right_dock_r3_context();

    bool depth_ovr = is_depth_gate_overridden_win();
    bool parallax_blocked = grp && grp->depth_known && !grp->depth_observable && !depth_ovr;
    EnableWindow(g_app.hChkExportParallax, !parallax_blocked);
    EnableWindow(g_app.hChkExportRefocus,  !parallax_blocked);
    bool par_on = !parallax_blocked && get_chk(g_app.hChkExportParallax);
    bool ref_on = !parallax_blocked && get_chk(g_app.hChkExportRefocus);
    bool ref_auto = g_app.hChkRefocusAuto && get_chk(g_app.hChkRefocusAuto);
    EnableWindow(g_app.hSldParallaxAmp, par_on);
    EnableWindow(g_app.hEdtParallaxAmp, par_on);
    if (g_app.hCmbRefocusMode) EnableWindow(g_app.hCmbRefocusMode, ref_on);
    if (g_app.hChkRefocusAuto) EnableWindow(g_app.hChkRefocusAuto, ref_on);
    EnableWindow(g_app.hSldRefocusPos,  ref_on && !ref_auto);
    EnableWindow(g_app.hEdtRefocusPos,  ref_on && !ref_auto);
    EnableWindow(g_app.hSldBokehStr,    ref_on);
    EnableWindow(g_app.hEdtBokehStr,    ref_on);
    if (g_app.hSldBokehStf) EnableWindow(g_app.hSldBokehStf, ref_on);
    if (g_app.hEdtBokehStf) EnableWindow(g_app.hEdtBokehStf, ref_on);
    if (g_app.hSldBokehSa)  EnableWindow(g_app.hSldBokehSa,  ref_on);
    if (g_app.hEdtBokehSa)  EnableWindow(g_app.hEdtBokehSa,  ref_on);
    if (g_app.hEdtVirtualFstop) EnableWindow(g_app.hEdtVirtualFstop, ref_on);
    if (g_app.hCmbBokehBlades) {
        EnableWindow(g_app.hCmbBokehBlades, ref_on);
        bool has_bl = ref_on && (selected_cmb_code(g_app.hCmbBokehBlades, kBokehBladeCodes, L"0") != L"0");
        if (g_app.hEdtBokehAngle) EnableWindow(g_app.hEdtBokehAngle, has_bl);
    }
    if (g_app.hEdtTiltX) EnableWindow(g_app.hEdtTiltX, ref_on);
    if (g_app.hEdtTiltY) EnableWindow(g_app.hEdtTiltY, ref_on);
    if (g_app.hCmbSeeThrough) EnableWindow(g_app.hCmbSeeThrough, ref_on);

    bool trans_on = get_chk(g_app.hChkTransientTab2);
    bool obs_write_on = g_app.hChkWriteObsReport && get_chk(g_app.hChkWriteObsReport);
    bool next_plan_on = g_app.hChkNextShotPlan && get_chk(g_app.hChkNextShotPlan);
    bool bus_exp_on   = g_app.hChkBusExport && get_chk(g_app.hChkBusExport);
    bool emit_diff_on = g_app.hChkEmitDiffuse && get_chk(g_app.hChkEmitDiffuse);
    bool emit_ls_on   = g_app.hChkEmitLightSplit && get_chk(g_app.hChkEmitLightSplit);
    bool eval_self_on = g_app.hChkEvalSelf && get_chk(g_app.hChkEvalSelf);
    bool is_exr_w = emit_rgb && (rgb_code == L"exr");
    bool any_out_w = emit_raw || emit_rgb || emit_fld || trans_on || bus_exp_on || obs_write_on || next_plan_on || emit_diff_on || emit_ls_on || eval_self_on;
    if (g_app.hCmbExrPrec)         EnableWindow(g_app.hCmbExrPrec,         is_exr_w);
    if (g_app.hCmbExrParts)        EnableWindow(g_app.hCmbExrParts,        is_exr_w);
    if (g_app.hCmbCodecConstraint) EnableWindow(g_app.hCmbCodecConstraint, any_out_w);
    if (g_app.hCmbSatModel)        EnableWindow(g_app.hCmbSatModel,        any_out_w);
    if (g_app.hCmbRgb1xTier)       EnableWindow(g_app.hCmbRgb1xTier, emit_rgb && (selected_rgb_scale_code() != L"2x"));
    if (g_app.hCmbHighlightMode) EnableWindow(g_app.hCmbHighlightMode, emit_rgb && get_chk(g_app.hChkRolloff));
    if (g_app.hEdtMemBudget) EnableWindow(g_app.hEdtMemBudget, selected_cmb_code(g_app.hCmbStreaming, kStreamingCodes, L"auto") != L"off");
    if (g_app.hCmbApertureWeight) EnableWindow(g_app.hCmbApertureWeight, ref_on);
    if (g_app.hCmbMetricPolicy)   EnableWindow(g_app.hCmbMetricPolicy,   ref_on);
    if (g_app.hCmbShutterMode) {
        bool act_shut = (selected_cmb_code(g_app.hCmbShutterMode, kShutterModeCodes, L"anchor") != L"anchor");
        if (g_app.hEdtFlowKappa) EnableWindow(g_app.hEdtFlowKappa, act_shut);
        if (g_app.hCmbIntraDeblur) EnableWindow(g_app.hCmbIntraDeblur, act_shut);
    }

    std::wstring dm = selected_cmb_code(g_app.hCmbDiffusionMode, kDiffusionCodes, L"off");
    bool d_on = (dm != L"off");
    if (g_app.hCmbDiffusionMode) {
        bool has_uc = (dm == L"ultra-contrast" || dm == L"black-mist" || dm.rfind(L"uc+", 0) == 0);
        if (g_app.hSldDiffStrength) EnableWindow(g_app.hSldDiffStrength, d_on);
        if (g_app.hEdtDiffStrength) EnableWindow(g_app.hEdtDiffStrength, d_on);
        if (g_app.hCmbUcGrade)      EnableWindow(g_app.hCmbUcGrade, has_uc);
        if (g_app.hCmbUcBaffle)     EnableWindow(g_app.hCmbUcBaffle, (dm == L"ultra-contrast" || dm.rfind(L"uc+", 0) == 0));
        if (g_app.hCmbUcChroma)     EnableWindow(g_app.hCmbUcChroma, has_uc);
    }
    std::wstring sca_m = selected_cmb_code(g_app.hCmbSpectralCa, kSpectralCaCodes, L"off");
    bool sca_active = (sca_m != L"off");
    if (g_app.hCmbSpectralCa) {
        if (g_app.hSldSpectralPwr) EnableWindow(g_app.hSldSpectralPwr, sca_active);
        if (g_app.hEdtSpectralPwr) EnableWindow(g_app.hEdtSpectralPwr, sca_active);
        bool is_lat = (sca_m == L"lateral");
        if (g_app.hCmbSpectralAng) EnableWindow(g_app.hCmbSpectralAng, is_lat);
        bool is_cust_ang = is_lat && (selected_cmb_code(g_app.hCmbSpectralAng, kSpectralAngleCodes, L"radial") == L"custom");
        if (g_app.hEdtSpectralDeg) EnableWindow(g_app.hEdtSpectralDeg, is_cust_ang);
    }

    bool bsh_active = (g_app.hEdtBokehDeNisen && _wtof(trim_ws(get_wnd_text(g_app.hEdtBokehDeNisen)).c_str()) > 1e-3) ||
                      (g_app.hEdtBokehSwirl && std::fabs(_wtof(trim_ws(get_wnd_text(g_app.hEdtBokehSwirl)).c_str())) > 1e-3) ||
                      (g_app.hEdtBokehAspect && std::fabs(_wtof(trim_ws(get_wnd_text(g_app.hEdtBokehAspect)).c_str()) - 1.0) > 5e-3) ||
                      (g_app.hEdtBokehOnion && _wtof(trim_ws(get_wnd_text(g_app.hEdtBokehOnion)).c_str()) > 1e-3);
    if (g_app.hBtnResetOptics) {
        EnableWindow(g_app.hBtnResetOptics, d_on || sca_active || bsh_active);
    }

    EnableWindow(g_app.hSldTransSigma, trans_on);
    EnableWindow(g_app.hEdtTransSigma, trans_on);
    EnableWindow(g_app.hSegTransColor, trans_on);

    bool refl_on = get_chk(g_app.hChkReflOn);
    EnableWindow(g_app.hSegReflMode, refl_on);

    bool optics_drift = g_app.optics.measured && g_app.optics.max_drift_px >= 0;
    double drift_px = 2.0 * (optics_drift ? g_app.optics.max_drift_px : (grp ? grp->max_drift_px : 0.0));
    bool has_drift = optics_drift || (grp && grp->has_telemetry);
    if (has_drift) {
        bool allow_split = (drift_px >= 40.0);
        seg_pill_set_item_enabled(g_app.hSegReflMode, 1, allow_split);
        if (!allow_split && seg_pill_get_sel(g_app.hSegReflMode) == 1) {
            seg_pill_set_sel(g_app.hSegReflMode, 0);
        }
        if (g_app.hLblReflDrift) {
            SetWindowTextW(g_app.hLblReflDrift,
                           heraw_i18n::trf_w(allow_split ? "t2.drift_ok_fmt" : "t2.drift_low_fmt", drift_px).c_str());
        }
    } else {
        seg_pill_set_item_enabled(g_app.hSegReflMode, 1, true);
        if (g_app.hLblReflDrift) {
            SetWindowTextW(g_app.hLblReflDrift, heraw_i18n::tr_w("t2.drift_idle").c_str());
        }
    }

    if (g_app.hChkLcpVig && g_app.hChkLcpDist) {
        EnableWindow(g_app.hChkLcpVig, TRUE);
        EnableWindow(g_app.hChkLcpDist, TRUE);
        SetWindowTextW(g_app.hChkLcpVig, heraw_i18n::tr_w("t4.lcp_vig").c_str());
        SetWindowTextW(g_app.hChkLcpDist, heraw_i18n::tr_w("t4.lcp_dist").c_str());
    }
    EnableWindow(g_app.hBtnExportLcp, !build_lcp_fields_csv().empty());
    int known_flds = 0, avail_flds = 0;
    for (int i = 0; i < kNumFields; ++i) {
        bool enabled = true;
        if (grp && !depth_ovr) {
            auto found = grp->field_observability.find(wide_to_utf8(kFieldSpecs[i].code));
            if (found != grp->field_observability.end()) {
                enabled = found->second;
                ++known_flds;
                if (found->second) ++avail_flds;
            }
        }
        EnableWindow(g_app.hChkFields[i], enabled);
    }
    for (int i = 0; i < kNumExtFields; ++i) {
        bool enabled = true;
        if (grp && !depth_ovr) {
            auto found = grp->field_observability.find(wide_to_utf8(kExtFieldSpecs[i].code));
            if (found != grp->field_observability.end()) {
                enabled = found->second;
                ++known_flds;
                if (found->second) ++avail_flds;
            }
        }
        if (g_app.hChkExtFields[i]) EnableWindow(g_app.hChkExtFields[i], enabled);
    }
    update_field_bank_visibility_win();
    if (g_app.hLblFieldsGateNote) {
        std::wstring gnote = (known_flds > 0)
            ? heraw_i18n::trf_w("t3.gate_measured_fmt", avail_flds, known_flds)
            : heraw_i18n::tr_w("t3.gate_idle");
        SetWindowTextW(g_app.hLblFieldsGateNote, gnote.c_str());
    }

    bool tiles_on = get_chk(g_app.hChkExportTiles);
    EnableWindow(g_app.hSegTileSize, tiles_on);
    EnableWindow(g_app.hSldTileConf, tiles_on);
    EnableWindow(g_app.hEdtTileConf, tiles_on);

    if (g_app.hBtnPortalOptics) {
        std::wstring obuf = (grp && grp->has_telemetry)
                          ? heraw_i18n::trf_w("t1.portal_quad_fmt", grp->quad_coverage)
                          : heraw_i18n::tr_w("t1.portal_quad_idle");
        SetWindowTextW(g_app.hBtnPortalOptics, obuf.c_str());
        InvalidateRect(g_app.hBtnPortalOptics, nullptr, FALSE);
    }

    int active_groups = 0;
    for (const auto& gitem : g_app.groups) if (gitem.checked) ++active_groups;
    if (g_app.hLblR1GroupSummary && g_app.hLblR1GateSr && g_app.hLblR1GatePar) {
        if (!grp) {
            g_app.r1_sr_status  = 0;
            g_app.r1_par_status = 0;
            SetWindowTextW(g_app.hLblR1GroupSummary, heraw_i18n::tr_w("r1.idle").c_str());
            SetWindowTextW(g_app.hLblR1GateSr,       heraw_i18n::tr_w("t4.quad_idle").c_str());
            SetWindowTextW(g_app.hLblR1GatePar,      heraw_i18n::tr_w("r1.gate_par_wait").c_str());
        } else {
            if (grp->is_stray_archive) {
                SetWindowTextW(g_app.hLblR1GroupSummary,
                    heraw_i18n::trf_w("r1.stray_fmt", (int)grp->frames.size(), active_groups).c_str());
            } else {
                SetWindowTextW(g_app.hLblR1GroupSummary,
                    heraw_i18n::trf_w("r1.grp_fmt", g_app.selected_group + 1, (int)grp->frames.size(), active_groups).c_str());
            }
            if (grp->valid_frame_count >= cur_min_req && grp->quad_coverage >= 2) {
                g_app.r1_sr_status = 1;
                SetWindowTextW(g_app.hLblR1GateSr, heraw_i18n::trf_w("r1.gate_sr_ok", grp->quad_coverage).c_str());
            } else {
                g_app.r1_sr_status = 2;
                SetWindowTextW(g_app.hLblR1GateSr, heraw_i18n::trf_w("r1.gate_sr_1x", grp->valid_frame_count).c_str());
            }
            if (grp->depth_known && grp->depth_observable) {
                g_app.r1_par_status = 1;
                SetWindowTextW(g_app.hLblR1GatePar, heraw_i18n::trf_w("r1.gate_par_ok", grp->max_parallax_2x_px).c_str());
            } else if (grp->depth_known && !grp->depth_observable) {
                g_app.r1_par_status = 0;
                SetWindowTextW(g_app.hLblR1GatePar, heraw_i18n::trf_w("r1.gate_par_low", grp->max_parallax_2x_px).c_str());
            } else {
                g_app.r1_par_status = 0;
                SetWindowTextW(g_app.hLblR1GatePar, heraw_i18n::tr_w("r1.gate_par_wait").c_str());
            }
        }
        InvalidateRect(g_app.hLblR1GateSr, nullptr, TRUE);
        InvalidateRect(g_app.hLblR1GatePar, nullptr, TRUE);
    }

    std::wstring cur_bus_path = active_bus_in_for_group_win(grp);
    if (g_app.hEdtBusIn && GetFocus() != g_app.hEdtBusIn) {
        if (get_wnd_text(g_app.hEdtBusIn) != cur_bus_path) {
            SetWindowTextW(g_app.hEdtBusIn, cur_bus_path.c_str());
        }
    }
    if (g_app.hLblBusPreflight) {
        auto pf = heraw_i18n::inspect_nib_for_gui(
            wide_to_utf8(cur_bus_path),
            grp ? (int)grp->frames.size() : 0,
            grp ? grp->raw_width : 0,
            grp ? grp->raw_height : 0
        );
        if (pf.empty) {
            g_app.bus_preflight_status = 0;
            SetWindowTextW(g_app.hLblBusPreflight, heraw_i18n::tr_w("r2.idle").c_str());
        } else if (pf.valid) {
            g_app.bus_preflight_status = 1;
            SetWindowTextW(g_app.hLblBusPreflight, utf8_to_wide(pf.summary).c_str());
        } else {
            g_app.bus_preflight_status = 2;
            SetWindowTextW(g_app.hLblBusPreflight, utf8_to_wide(pf.summary).c_str());
        }
        InvalidateRect(g_app.hLblBusPreflight, nullptr, TRUE);
    }

    if (g_app.hLblLiveReceipt) {
        std::vector<std::wstring> lines;
        if (emit_raw) {
            int rsel = (int)SendMessageW(g_app.hCmbRawFmt, CB_GETCURSEL, 0, 0);
            wchar_t rlbl[96] = L"DNG";
            if (rsel >= 0) SendMessageW(g_app.hCmbRawFmt, CB_GETLBTEXT, (WPARAM)rsel, reinterpret_cast<LPARAM>(rlbl));
            lines.push_back(std::wstring(L"• *_nijinLBS.dng (") + rlbl + (relay_on ? L" · Relay)" : L")"));
        }
        if (emit_rgb) {
            std::wstring sc = selected_rgb_scale_code();
            std::wstring ext = (rgb_code == L"png") ? L"png"
                             : (rgb_code == L"exr") ? L"exr"
                             : (rgb_code.rfind(L"jxs", 0) == 0 ? L"jxs" : L"tif");
            std::wstring cs = get_chk(g_app.hRadCsAdobe) ? L"AdobeRGB" : L"sRGB";
            if (sc == L"both") lines.push_back(L"• *_nijinLBS." + ext + L" + *_1x." + ext + L" (" + cs + L")");
            else if (sc == L"1x") lines.push_back(L"• *_nijinLBS_1x." + ext + L" (1× · " + cs + L")");
            else lines.push_back(L"• *_nijinLBS." + ext + L" (2× · " + cs + L")");
        }
        if (emit_fld) {
            wchar_t flbl[96] = L"";
            if (fm_sel >= 0) SendMessageW(g_app.hCmbFieldMode, CB_GETLBTEXT, (WPARAM)fm_sel, reinterpret_cast<LPARAM>(flbl));
            lines.push_back(L"• fields (" + std::to_wstring(count_checked_fields()) + L"/27 · " + flbl + L")");
        }
        if (trans_on)     lines.push_back(L"• *_transient.png");
        if (emit_diff_on) lines.push_back(L"• *_diffuse.png (O1 Intrinsic)");
        if (emit_ls_on)   lines.push_back(L"• *_dc.png + *_ac.png (O2 Split)");
        if (par_on)       lines.push_back(L"• *_parallax.html (2.5D WebGL)");
        if (ref_on)       lines.push_back(L"• *_refocus.png (Photon Bokeh)");
        if (tiles_on)     lines.push_back(L"• dataset_tiles/ (NPZ/PNG)");
        if (bus_exp_on)   lines.push_back(L"• session_bus.nib (NOPB/1 S4)");
        if (eval_self_on) lines.push_back(L"• eval_self.json (LOO χ² / FRC)");
        int obs_cnt = count_checked_observers_win();
        if (obs_write_on || obs_cnt > 0) {
            std::wstring ocsv = build_observers_csv_win();
            if (obs_cnt == kNumObs) ocsv = L"all";
            if (obs_write_on) lines.push_back(L"• observer_report.json (" + (ocsv.empty() ? std::wstring(L"all") : ocsv) + L")");
            else lines.push_back(L"✦ Observers: " + ocsv);
        }
        if (next_plan_on) lines.push_back(L"• next_shot_plan.json");

        std::vector<std::wstring> fx;
        if (d_on) fx.push_back(dm + L"(" + trim_ws(get_wnd_text(g_app.hEdtDiffStrength)) + L")");
        if (sca_active) fx.push_back(L"CA:" + sca_m + L"(" + trim_ws(get_wnd_text(g_app.hEdtSpectralPwr)) + L")");
        if (g_app.hEdtBokehDeNisen && _wtof(trim_ws(get_wnd_text(g_app.hEdtBokehDeNisen)).c_str()) > 1e-3)
            fx.push_back(L"DN:" + trim_ws(get_wnd_text(g_app.hEdtBokehDeNisen)));
        if (g_app.hEdtBokehSwirl && std::fabs(_wtof(trim_ws(get_wnd_text(g_app.hEdtBokehSwirl)).c_str())) > 1e-3)
            fx.push_back(L"Swirl:" + trim_ws(get_wnd_text(g_app.hEdtBokehSwirl)));
        if (g_app.hEdtBokehAspect && std::fabs(_wtof(trim_ws(get_wnd_text(g_app.hEdtBokehAspect)).c_str()) - 1.0) > 5e-3)
            fx.push_back(L"Asp:" + trim_ws(get_wnd_text(g_app.hEdtBokehAspect)) + L"×");
        if (g_app.hEdtBokehOnion && _wtof(trim_ws(get_wnd_text(g_app.hEdtBokehOnion)).c_str()) > 1e-3)
            fx.push_back(L"Onion:" + trim_ws(get_wnd_text(g_app.hEdtBokehOnion)));
        if (ref_on && g_app.hEdtBokehStf && _wtof(trim_ws(get_wnd_text(g_app.hEdtBokehStf)).c_str()) > 5e-3)
            fx.push_back(L"STF:" + trim_ws(get_wnd_text(g_app.hEdtBokehStf)));
        if (!cur_bus_path.empty()) fx.push_back(L"NIB←" + basename_of_w(cur_bus_path));
        if (!fx.empty()) {
            std::wstring fx_join;
            for (size_t k = 0; k < fx.size(); ++k) {
                if (k > 0) fx_join += L" · ";
                fx_join += fx[k];
            }
            lines.push_back(L"✦ " + fx_join);
        }
        if (lines.empty()) lines.push_back(heraw_i18n::tr_w("st.alert_no_output"));
        std::wstring joined;
        for (size_t k = 0; k < lines.size(); ++k) {
            if (k > 0) joined += L"\n";
            joined += lines[k];
        }
        SetWindowTextW(g_app.hLblLiveReceipt, joined.c_str());
    }

    int c_tab2 = (d_on ? 1 : 0) + (sca_active ? 1 : 0) + (bsh_active ? 1 : 0) + (par_on ? 1 : 0) + (ref_on ? 1 : 0);
    int c_tab3 = (emit_fld ? 1 : 0) + ((obs_write_on || count_checked_observers_win() > 0) ? 1 : 0) +
                 (next_plan_on ? 1 : 0) + (tiles_on ? 1 : 0);
    seg_pill_set_item_text(g_app.hTabBar, 0, heraw_i18n::tr_w("tab.1"));
    seg_pill_set_item_text(g_app.hTabBar, 1,
        heraw_i18n::tr_w("tab.2") + (c_tab2 > 0 ? L" · " + std::to_wstring(c_tab2) : L""));
    seg_pill_set_item_text(g_app.hTabBar, 2,
        heraw_i18n::tr_w("tab.3") + (c_tab3 > 0 ? L" · " + std::to_wstring(c_tab3) : L""));

    if (g_app.hLblFrameHdr) {
        auto gsel = selected_group_indices();
        std::wstring fh;
        if (gsel.size() >= 2 && grp) {
            fh = heraw_i18n::trf_w("dock.insp_hdr_multi_fmt", g_app.selected_group + 1, (int)gsel.size());
        } else if (grp) {
            fh = grp->is_stray_archive
               ? heraw_i18n::tr_w("dock.insp_hdr_stray")
               : heraw_i18n::trf_w("dock.insp_hdr_group_fmt", g_app.selected_group + 1);
        } else {
            fh = heraw_i18n::tr_w("dock.insp_hdr");
        }
        SetWindowTextW(g_app.hLblFrameHdr, fh.c_str());
    }

    if (g_app.hBtnRun) {
        if (g_app.running.load()) {
            SetWindowTextW(g_app.hBtnRun, heraw_i18n::tr_w("bot.stop").c_str());
        } else {
            std::vector<std::wstring> tags;
            std::wstring scale_code = selected_rgb_scale_code();
            if (emit_rgb && scale_code == L"1x") tags.push_back(heraw_i18n::tr_w("bot.tag_1x"));
            else if (emit_rgb && scale_code == L"both") tags.push_back(heraw_i18n::tr_w("bot.tag_both"));
            if (g_app.motion_mode_idx == 3) tags.push_back(heraw_i18n::tr_w("bot.tag_clean"));
            else if (g_app.motion_mode_idx == 4) tags.push_back(heraw_i18n::tr_w("bot.tag_vnd"));
            else if (g_app.motion_mode_idx == 5) tags.push_back(heraw_i18n::tr_w("bot.tag_strobe"));
            if (trans_on) tags.push_back(heraw_i18n::tr_w("bot.tag_trans"));
            if (refl_on)  tags.push_back(heraw_i18n::tr_w("bot.tag_refl"));
            if (emit_fld) tags.push_back(heraw_i18n::trf_w("bot.tag_fields_fmt", count_checked_fields()));
            if (par_on)   tags.push_back(heraw_i18n::tr_w("bot.tag_25d"));
            if (ref_on)   tags.push_back(heraw_i18n::tr_w("bot.tag_refocus"));
            if (tiles_on) tags.push_back(heraw_i18n::tr_w("bot.tag_tiles"));
            if (bus_exp_on) tags.push_back(L"NIB");

            std::string tag_join;
            for (size_t k = 0; k < tags.size() && k < 2; ++k) {
                if (k > 0) tag_join += "+";
                tag_join += wide_to_utf8(tags[k]);
            }
            std::wstring runLbl;
            if (active_groups > 0 && !tag_join.empty()) {
                runLbl = heraw_i18n::trf_w("bot.run_grp_tag_fmt", active_groups, tag_join.c_str());
            } else if (active_groups > 0) {
                runLbl = heraw_i18n::trf_w("bot.run_grp_fmt", active_groups);
            } else if (!tag_join.empty()) {
                runLbl = heraw_i18n::trf_w("bot.run_tag_fmt", tag_join.c_str());
            } else {
                runLbl = heraw_i18n::tr_w("bot.run");
            }
            SetWindowTextW(g_app.hBtnRun, runLbl.c_str());
        }
        InvalidateRect(g_app.hBtnRun, nullptr, FALSE);
    }

    if (g_app.hParallaxMeter) InvalidateRect(g_app.hParallaxMeter, nullptr, FALSE);
    bool grouping_editable = !g_app.running.load() && !g_app.plan_scanning.load();
    EnableWindow(g_app.hCmbGrouping, grouping_editable);
    EnableWindow(g_app.hBtnRegroup, grouping_editable && !g_app.inputs.empty());
    if (g_app.hBtnIntakeParams) EnableWindow(g_app.hBtnIntakeParams, grouping_editable);
    EnableWindow(g_app.hEdtGrpSz, grouping_editable && SendMessageW(g_app.hCmbGrouping, CB_GETCURSEL, 0, 0) != 1);
    refresh_intake_btn_badge_win();
    if (g_app.hBtnMergeGroups) {
        EnableWindow(g_app.hBtnMergeGroups, grouping_editable && selected_group_indices().size() >= 2);
    }
    if (g_app.hBtnCleanStray) {
        EnableWindow(g_app.hBtnCleanStray, grouping_editable && has_stray_groups_win());
    }
    if (g_app.hBtnSplitFrames) {
        auto fsel = selected_frame_indices();
        EnableWindow(g_app.hBtnSplitFrames, grouping_editable && grp && fsel.size() >= 1 && fsel.size() < grp->frames.size());
    }
    if (g_app.hBtnLockFrame) {
        EnableWindow(g_app.hBtnLockFrame, grouping_editable && grp && !grp->is_stray_archive && !grp->frames.empty());
    }
    EnableWindow(g_app.hBtnRun, g_app.running.load() || (!g_app.grouping_dirty && !g_app.plan_scanning.load()));
    update_grouping_hint();
    refresh_perf_header_label();
    if (g_app.hLblSiliconStats) SetWindowTextW(g_app.hLblSiliconStats, silicon_readout(g_app.optics).c_str());
    if (g_app.hwnd) {
        RECT crc{};
        GetClientRect(g_app.hwnd, &crc);
        RECT rCard{g_app.S(422), g_app.S(110), crc.right - g_app.S(14), crc.bottom - g_app.S(70)};
        InvalidateRect(g_app.hwnd, &rCard, FALSE);
    }

    g_app.suppress_sync = false;
}

// ============================================================================
// Registry Persistence (Software\NijiN\nijinLBS) & Hardware Performance Policy
// ============================================================================
static constexpr UINT_PTR TIMER_PERF_BREATH = 901;

static void stop_perf_breathing_alert(HWND hwnd) {
    if (hwnd) KillTimer(hwnd, TIMER_PERF_BREATH);
    if (g_app.perf_breath_active) {
        g_app.perf_breath_active = false;
        g_app.perf_breath_phase = 0.0f;
        if (!g_app.is_screenshot_mode) {
            reg_write_str(L"PerfGpuHintSeen", L"1");
        }
        if (g_app.hBtnPerfHeader) {
            InvalidateRect(g_app.hBtnPerfHeader, nullptr, FALSE);
        }
    }
}

static void init_perf_hardware_defaults(HWND hwnd) {
    stop_perf_breathing_alert(hwnd);
    g_app.gpu_policy = heraw_cache::probe_win_gpu_policy();
    const auto pwr = heraw_cache::query_power_status();

    const std::wstring bat_reg = g_app.is_screenshot_mode ? L"" : reg_read_str(L"BatteryDevice");
    if (bat_reg.empty()) {
        g_app.is_battery_device = pwr.has_battery;
        if (!g_app.is_screenshot_mode) {
            reg_write_str(L"BatteryDevice", g_app.is_battery_device ? L"1" : L"0");
        }
    } else {
        g_app.is_battery_device = (bat_reg == L"1");
    }

    const bool has_saved_ui = (reg_read_str(L"HasSavedUiPrefs") == L"1");
    const bool has_saved_backend = !reg_read_str(L"BackendIdx").empty();
    const bool hint_seen = (reg_read_str(L"PerfGpuHintSeen") == L"1") || has_saved_ui;

    g_app.loading_prefs = true;
    if (g_app.hCmbBackend) {
        SendMessageW(g_app.hCmbBackend, CB_SETCURSEL, (WPARAM)(g_app.gpu_policy.default_disable_gpu ? 2 : 0), 0);
    }
    if (g_app.hCmbCpuCores) {
        SendMessageW(g_app.hCmbCpuCores, CB_SETCURSEL, (WPARAM)(g_app.is_battery_device ? 2 : 0), 0);
    }
    g_app.loading_prefs = false;

    if (g_app.gpu_policy.default_disable_gpu && !has_saved_backend && !hint_seen) {
        if (!g_app.is_screenshot_mode) {
            reg_write_str(L"PerfGpuHintSeen", L"1");
        }
    }
    g_app.perf_breath_active = false;
    g_app.perf_breath_phase = 0.0f;
    refresh_engine_status_label();
}

static void save_ui_prefs() {
    if (g_app.is_screenshot_mode || !g_app.hCmbRawFmt || g_app.suppress_sync || g_app.loading_prefs) return;

    auto bstr = [](HWND h) -> std::wstring { return get_chk(h) ? L"1" : L"0"; };
    reg_write_str(L"HasSavedUiPrefs", L"1");
    reg_write_str(L"LcpLca", bstr(g_app.hChkLcpLca));
    reg_write_str(L"LcpVignette", bstr(g_app.hChkLcpVig));
    reg_write_str(L"LcpDistortion", bstr(g_app.hChkLcpDist));
    reg_write_str(L"ActiveTabIdx",    std::to_wstring(g_app.active_tab));

    reg_write_str(L"SortTime",        bstr(g_app.hChkSortTime));
    reg_write_str(L"Recursive",       bstr(g_app.hChkRecursive));

    reg_write_str(L"PresetIdx",       std::to_wstring(g_app.preset_idx));
    reg_write_str(L"PresetCode",      kPresetCodes[std::max(0, std::min(3, g_app.preset_idx))]);
    reg_write_str(L"MotionModeIdx",   std::to_wstring(g_app.motion_mode_idx));

    int olpf_idx = (int)SendMessageW(g_app.hCmbOlpf, CB_GETCURSEL, 0, 0);
    if (olpf_idx >= 0 && olpf_idx < 4) {
        reg_write_str(L"OlpfModeIdx",  std::to_wstring(olpf_idx));
        reg_write_str(L"OlpfModeCode", kOlpfCodes[olpf_idx]);
    }

    reg_write_str(L"EmitRaw",         bstr(g_app.hChkRaw));
    int r_idx = (int)SendMessageW(g_app.hCmbRawFmt, CB_GETCURSEL, 0, 0);
    if (r_idx >= 0 && r_idx < 5) reg_write_str(L"RawFmtCode", kRawFmtCodes[r_idx]);
    reg_write_str(L"RelayRaw",        bstr(g_app.hChkRelayRaw));
    reg_write_str(L"NoToolExif",      bstr(g_app.hChkNoToolExif));

    reg_write_str(L"LangCode", utf8_to_wide(heraw_i18n::I18nManager::instance().active_code()));
    reg_write_str(L"EmitRgb",         bstr(g_app.hChkRgb));
    reg_write_str(L"RgbScaleCode",    selected_rgb_scale_code());
    int c_idx = (int)SendMessageW(g_app.hCmbRgbFmt, CB_GETCURSEL, 0, 0);
    if (c_idx >= 0 && c_idx < kNumRgbFmts) reg_write_str(L"RgbFmtCode", kRgbFmtCodes[c_idx]);
    reg_write_str(L"JxsBpp",          trim_ws(get_wnd_text(g_app.hEdtJxsBpp)));
    reg_write_str(L"ColorSpaceIdx",   get_chk(g_app.hRadCsAdobe) ? L"1" : L"0");

    reg_write_str(L"EmitField",       bstr(g_app.hChkField));
    int fm_idx = (int)SendMessageW(g_app.hCmbFieldMode, CB_GETCURSEL, 0, 0);
    if (fm_idx >= 0 && fm_idx < kNumFieldModes) reg_write_str(L"FieldModeCode", kFieldModeCodes[fm_idx]);
    reg_write_str(L"FieldsCsv",       build_fields_csv().empty() ? L"none" : build_fields_csv());

    reg_write_str(L"EmitTransient",   bstr(g_app.hChkTransientTab2));
    reg_write_str(L"TransientSigma",  trim_ws(get_wnd_text(g_app.hEdtTransSigma)));
    reg_write_str(L"TransientColor",  (seg_pill_get_sel(g_app.hSegTransColor) == 1) ? L"gray" : L"rgb");

    reg_write_str(L"CleanSens",       trim_ws(get_wnd_text(g_app.hEdtCleanSens)));
    reg_write_str(L"VndThresh",       trim_ws(get_wnd_text(g_app.hEdtVndThresh)));
    reg_write_str(L"StrobeStep",      trim_ws(get_wnd_text(g_app.hEdtStrobeStep)));
    reg_write_str(L"StrobeFade",      std::to_wstring(seg_pill_get_sel(g_app.hSegStrobeFade)));

    int refl_sel = seg_pill_get_sel(g_app.hSegReflMode);
    reg_write_str(L"ReflectionMode",  !get_chk(g_app.hChkReflOn) ? L"off"
                                      : (refl_sel == 1 ? L"split-layers" : L"glint-suppress"));

    reg_write_str(L"ExportParallax",  bstr(g_app.hChkExportParallax));
    reg_write_str(L"ParallaxAmp",     trim_ws(get_wnd_text(g_app.hEdtParallaxAmp)));
    reg_write_str(L"ExportRefocus",   bstr(g_app.hChkExportRefocus));
    reg_write_str(L"RefocusPos",      trim_ws(get_wnd_text(g_app.hEdtRefocusPos)));
    reg_write_str(L"BokehStrength",   trim_ws(get_wnd_text(g_app.hEdtBokehStr)));
    if (g_app.hEdtBokehStf) reg_write_str(L"BokehStf", trim_ws(get_wnd_text(g_app.hEdtBokehStf)));

    reg_write_str(L"ExportTiles",     bstr(g_app.hChkExportTiles));
    reg_write_str(L"TileSize",        (seg_pill_get_sel(g_app.hSegTileSize) == 1) ? L"512" : L"256");
    reg_write_str(L"TileMinConf",     trim_ws(get_wnd_text(g_app.hEdtTileConf)));

    reg_write_str(L"Denoise",         trim_ws(get_wnd_text(g_app.hEdtDenoise)));
    reg_write_str(L"Chroma",          trim_ws(get_wnd_text(g_app.hEdtChroma)));
    reg_write_str(L"PsfIters",        trim_ws(get_wnd_text(g_app.hEdtPsfIters)));
    reg_write_str(L"PsfSigma",        trim_ws(get_wnd_text(g_app.hEdtPsfSigma)));
    reg_write_str(L"Deghost",         trim_ws(get_wnd_text(g_app.hEdtDeghost)));
    reg_write_str(L"EvOffset",        trim_ws(get_wnd_text(g_app.hEdtEv)));
    reg_write_str(L"GroupSize",       trim_ws(get_wnd_text(g_app.hEdtGrpSz)));
    {
        int gsel = (int)SendMessageW(g_app.hCmbGrouping, CB_GETCURSEL, 0, 0);
        const wchar_t* gcode = (gsel == 1) ? L"single" : (gsel == 2 ? L"best" : (gsel == 3 ? L"top1" : L"auto"));
        reg_write_str(L"GroupingMode", gcode);
    }
    reg_write_str(L"Threads",         trim_ws(get_wnd_text(g_app.hEdtThreads)));
    reg_write_str(L"BackendIdx",      std::to_wstring((int)SendMessageW(g_app.hCmbBackend, CB_GETCURSEL, 0, 0)));
    reg_write_str(L"BatteryDevice",   g_app.is_battery_device ? L"1" : L"0");
    {
        int csel = g_app.hCmbCpuCores ? (int)SendMessageW(g_app.hCmbCpuCores, CB_GETCURSEL, 0, 0) : 0;
        if (csel >= 0 && csel < 5) reg_write_str(L"CpuCoresCode", kCpuCoresCodes[csel]);
        int psel = g_app.hCmbPriority ? (int)SendMessageW(g_app.hCmbPriority, CB_GETCURSEL, 0, 0) : 0;
        if (psel >= 0 && psel < 4) reg_write_str(L"PriorityCode", kPriorityCodes[psel]);
    }

    reg_write_str(L"SiliconClean",    bstr(g_app.hChkSiliconClean));
    reg_write_str(L"IbpOn",           get_ibp_iters_win() > 0 ? L"1" : L"0");
    reg_write_str(L"IbpIters",        std::to_wstring(get_ibp_iters_win()));
    if (g_app.hEdtRgbPsfIters) reg_write_str(L"RgbPsfIters", trim_ws(get_wnd_text(g_app.hEdtRgbPsfIters)));
    if (g_app.hChkEdgePolish)  reg_write_str(L"EdgePolish",  bstr(g_app.hChkEdgePolish));
    reg_write_str(L"AutoLca",         bstr(g_app.hChkAutoLca));
    reg_write_str(L"AutoExp",         bstr(g_app.hChkAutoExp));
    reg_write_str(L"Rolloff",         bstr(g_app.hChkRolloff));
    reg_write_str(L"Dither",          bstr(g_app.hChkDither));

    reg_write_str(L"DecoderModeCode",  g_app.decoder_mode);
    reg_write_str(L"NevFramesSpec",    g_app.nev_frames_spec);
    reg_write_str(L"BurstGapSec",      g_app.burst_gap_s);
    reg_write_str(L"MinRelSharpness",  g_app.min_rel_sharpness);
    reg_write_str(L"MaxShiftPx",       g_app.max_shift_px);
    reg_write_str(L"LuckyModeCode",    g_app.lucky_mode);
    reg_write_str(L"MaxClusterFrames", g_app.max_cluster_frames);

    reg_write_str(L"MotionProfileCode", selected_cmb_code(g_app.hCmbMotionProfile, kMotionProfileCodes, L"auto"));
    reg_write_str(L"Rgb1xTierCode",     selected_cmb_code(g_app.hCmbRgb1xTier,     kRgb1xTierCodes,     L"sinc"));
    reg_write_str(L"ExrPrecCode",         selected_cmb_code(g_app.hCmbExrPrec,         kExrPrecCodes,         L"half"));
    reg_write_str(L"ExrPartsCode",        selected_cmb_code(g_app.hCmbExrParts,        kExrPartsCodes,        L"beauty,fg,bg,scatter,depth"));
    reg_write_str(L"CodecConstraintCode", selected_cmb_code(g_app.hCmbCodecConstraint, kCodecConstraintCodes, L"off"));
    reg_write_str(L"SatModelCode",        selected_cmb_code(g_app.hCmbSatModel,        kSatModelCodes,        L"hard"));
    reg_write_str(L"HighlightModeCode", selected_cmb_code(g_app.hCmbHighlightMode, kHighlightModeCodes, L"legacy"));
    reg_write_str(L"StreamingModeCode", selected_cmb_code(g_app.hCmbStreaming,     kStreamingCodes,     L"auto"));
    reg_write_str(L"MemBudgetGb",       trim_ws(get_wnd_text(g_app.hEdtMemBudget)));
    reg_write_str(L"ShutterModeCode",   selected_cmb_code(g_app.hCmbShutterMode,   kShutterModeCodes,   L"anchor"));
    reg_write_str(L"FlowKappa",         trim_ws(get_wnd_text(g_app.hEdtFlowKappa)));
    reg_write_str(L"IntraDeblurCode",   selected_cmb_code(g_app.hCmbIntraDeblur,   kIntraDeblurCodes,   L"auto"));
    reg_write_str(L"DiffusionModeCode", selected_cmb_code(g_app.hCmbDiffusionMode, kDiffusionCodes,     L"off"));
    reg_write_str(L"DiffusionStrength", trim_ws(get_wnd_text(g_app.hEdtDiffStrength)));
    reg_write_str(L"UcGradeCode",       selected_cmb_code(g_app.hCmbUcGrade,       kUcGradeCodes,       L"1"));
    reg_write_str(L"UcBaffleCode",      selected_cmb_code(g_app.hCmbUcBaffle,      kUcBaffleCodes,      L"standard"));
    reg_write_str(L"UcChromaCode",      selected_cmb_code(g_app.hCmbUcChroma,      kUcChromaCodes,      L"neutral"));
    reg_write_str(L"ApertureWeightCode",selected_cmb_code(g_app.hCmbApertureWeight,kApertureWeightCodes,L"voronoi"));
    reg_write_str(L"MetricPolicyCode",  selected_cmb_code(g_app.hCmbMetricPolicy,  kMetricPolicyCodes,  L"verified"));
    if (g_app.hCmbRefocusMode)  reg_write_str(L"RefocusModeCode", selected_cmb_code(g_app.hCmbRefocusMode, kRefocusModeCodes, L"auto"));
    if (g_app.hChkRefocusAuto)  reg_write_str(L"RefocusAutoPos",  bstr(g_app.hChkRefocusAuto));
    if (g_app.hEdtBokehSa)      reg_write_str(L"BokehSa",         trim_ws(get_wnd_text(g_app.hEdtBokehSa)));
    if (g_app.hEdtVirtualFstop) reg_write_str(L"VirtualFstop",    trim_ws(get_wnd_text(g_app.hEdtVirtualFstop)));
    if (g_app.hCmbBokehBlades)  reg_write_str(L"BokehBladesCode", selected_cmb_code(g_app.hCmbBokehBlades, kBokehBladeCodes, L"0"));
    if (g_app.hEdtBokehAngle)   reg_write_str(L"BokehAngle",      trim_ws(get_wnd_text(g_app.hEdtBokehAngle)));
    if (g_app.hEdtTiltX)        reg_write_str(L"TiltPlaneX",      trim_ws(get_wnd_text(g_app.hEdtTiltX)));
    if (g_app.hEdtTiltY)        reg_write_str(L"TiltPlaneY",      trim_ws(get_wnd_text(g_app.hEdtTiltY)));
    if (g_app.hCmbSeeThrough)   reg_write_str(L"SeeThroughCode",  selected_cmb_code(g_app.hCmbSeeThrough, kSeeThroughCodes, L"auto"));
    if (g_app.hCmbDepthGate)    reg_write_str(L"DepthGateCode",   selected_cmb_code(g_app.hCmbDepthGate,  kDepthGateCodes,  L"auto"));
    if (g_app.hEdtDepthDisp)    reg_write_str(L"DepthMinDisp",    trim_ws(get_wnd_text(g_app.hEdtDepthDisp)).empty() ? L"__AUTO__" : trim_ws(get_wnd_text(g_app.hEdtDepthDisp)));
    if (g_app.hEdtDepthCoh)     reg_write_str(L"DepthMinCoh",     trim_ws(get_wnd_text(g_app.hEdtDepthCoh)).empty()  ? L"__AUTO__" : trim_ws(get_wnd_text(g_app.hEdtDepthCoh)));
    if (g_app.hEdtDepthSup)     reg_write_str(L"DepthMinSupport", trim_ws(get_wnd_text(g_app.hEdtDepthSup)).empty()  ? L"__AUTO__" : trim_ws(get_wnd_text(g_app.hEdtDepthSup)));
    if (g_app.hCmbObsGate)      reg_write_str(L"ObserverGateCode",selected_cmb_code(g_app.hCmbObsGate,    kObsGateCodes,    L"auto"));
    if (g_app.hEdtReadoutMs)    reg_write_str(L"ReadoutMs",       trim_ws(get_wnd_text(g_app.hEdtReadoutMs)).empty() ? L"__AUTO__" : trim_ws(get_wnd_text(g_app.hEdtReadoutMs)));
    if (g_app.hEdtObsPitch)     reg_write_str(L"ObsPitchDeg",     trim_ws(get_wnd_text(g_app.hEdtObsPitch)));
    if (g_app.hEdtPlateScale)   reg_write_str(L"PlateScale",      trim_ws(get_wnd_text(g_app.hEdtPlateScale)));
    if (g_app.hChkEmitDiffuse)    reg_write_str(L"EmitDiffuse",    bstr(g_app.hChkEmitDiffuse));
    if (g_app.hChkEmitLightSplit) reg_write_str(L"EmitLightSplit", bstr(g_app.hChkEmitLightSplit));
    if (g_app.hChkEvalSelf)       reg_write_str(L"EvalSelf",       bstr(g_app.hChkEvalSelf));
    {
        std::wstring ocsv = build_observers_csv_win();
        if (count_checked_observers_win() == kNumObs) ocsv = L"all";
        reg_write_str(L"ObserversCsv",  ocsv.empty() ? L"none" : ocsv);
    }
    if (g_app.hChkWriteObsReport) reg_write_str(L"WriteObserverReport", bstr(g_app.hChkWriteObsReport));
    reg_write_str(L"NextShotPlan",      bstr(g_app.hChkNextShotPlan));
    reg_write_str(L"SpectralCaModeCode",selected_cmb_code(g_app.hCmbSpectralCa,    kSpectralCaCodes,    L"off"));
    reg_write_str(L"SpectralCaPower",   trim_ws(get_wnd_text(g_app.hEdtSpectralPwr)));
    reg_write_str(L"SpectralCaAngleCode",selected_cmb_code(g_app.hCmbSpectralAng,  kSpectralAngleCodes, L"radial"));
    if (g_app.hEdtSpectralDeg)  reg_write_str(L"SpectralCaCustomDeg", trim_ws(get_wnd_text(g_app.hEdtSpectralDeg)));
    reg_write_str(L"BokehDeNisen",      trim_ws(get_wnd_text(g_app.hEdtBokehDeNisen)));
    reg_write_str(L"BokehSwirl",        trim_ws(get_wnd_text(g_app.hEdtBokehSwirl)));
    if (g_app.hEdtBokehAspect) reg_write_str(L"BokehAspectCode", trim_ws(get_wnd_text(g_app.hEdtBokehAspect)));
    reg_write_str(L"BokehOnionClean",   trim_ws(get_wnd_text(g_app.hEdtBokehOnion)));
    reg_write_str(L"BusInPath",         active_bus_in_for_group_win(current_group_ptr()));
    reg_write_str(L"BusExport",         bstr(g_app.hChkBusExport));
}

static void load_ui_prefs() {
    if (reg_read_str(L"HasSavedUiPrefs") != L"1") return;
    g_app.loading_prefs = true;

    auto set_chk_reg = [](HWND h, const wchar_t* k) {
        std::wstring v = reg_read_str(k);
        if (!v.empty()) set_chk_val(h, v == L"1");
    };
    auto set_pair_reg = [](HWND hSld, HWND hEdt, const wchar_t* k) {
        std::wstring v = reg_read_str(k);
        if (!v.empty()) set_tuned_pair(hSld, hEdt, v.c_str());
    };
    auto set_edt_reg = [](HWND hEdt, const wchar_t* k) {
        std::wstring v = reg_read_str(k);
        if (!v.empty()) {
            g_app.suppress_sync = true;
            SetWindowTextW(hEdt, v.c_str());
            g_app.suppress_sync = false;
        }
    };

    set_chk_reg(g_app.hChkSortTime,  L"SortTime");
    set_chk_reg(g_app.hChkLcpLca, L"LcpLca");
    set_chk_reg(g_app.hChkLcpVig, L"LcpVignette");
    set_chk_reg(g_app.hChkLcpDist, L"LcpDistortion");
    set_chk_reg(g_app.hChkRecursive, L"Recursive");

    std::wstring preCode = reg_read_str(L"PresetCode");
    std::wstring pre = reg_read_str(L"PresetIdx");
    if (!preCode.empty()) {
        for (int i = 0; i < 4; ++i) if (preCode == kPresetCodes[i]) apply_preset_to_controls(i);
    } else if (!pre.empty()) {
        int pi = _wtoi(pre.c_str());
        if (pi >= 0 && pi < 4) apply_preset_to_controls(pi == 3 ? 1 : pi == 1 ? 2 : pi == 2 ? 3 : 0);
    }

    std::wstring mm = reg_read_str(L"MotionModeIdx");
    if (!mm.empty()) {
        int mi = _wtoi(mm.c_str());
        if (mi >= 0 && mi <= 6) g_app.motion_mode_idx = mi;
    }

    std::wstring oc = reg_read_str(L"OlpfModeCode");
    bool olpf_matched = false;
    for (int i = 0; i < 4; ++i) {
        if (oc == kOlpfCodes[i]) { SendMessageW(g_app.hCmbOlpf, CB_SETCURSEL, (WPARAM)i, 0); olpf_matched = true; break; }
    }
    if (!olpf_matched) {
        std::wstring oi = reg_read_str(L"OlpfModeIdx");
        if (!oi.empty()) {
            int idx = _wtoi(oi.c_str());
            if (idx >= 0 && idx < 4) SendMessageW(g_app.hCmbOlpf, CB_SETCURSEL, (WPARAM)idx, 0);
        }
    }

    set_chk_reg(g_app.hChkRaw,      L"EmitRaw");
    std::wstring rfc = reg_read_str(L"RawFmtCode");
    for (int i = 0; i < 5; ++i) if (rfc == kRawFmtCodes[i]) { SendMessageW(g_app.hCmbRawFmt, CB_SETCURSEL, (WPARAM)i, 0); break; }
    set_chk_reg(g_app.hChkRelayRaw, L"RelayRaw");
    set_chk_reg(g_app.hChkNoToolExif, L"NoToolExif");

    set_chk_reg(g_app.hChkRgb,      L"EmitRgb");
    {
        std::wstring rsc = reg_read_str(L"RgbScaleCode");
        int sidx = (rsc == L"1x") ? 1 : (rsc == L"both" ? 2 : 0);
        if (g_app.hSegRgbScale) seg_pill_set_sel(g_app.hSegRgbScale, sidx);
    }
    std::wstring cfc = reg_read_str(L"RgbFmtCode");
    for (int i = 0; i < kNumRgbFmts; ++i) if (cfc == kRgbFmtCodes[i]) { SendMessageW(g_app.hCmbRgbFmt, CB_SETCURSEL, (WPARAM)i, 0); break; }
    set_edt_reg(g_app.hEdtJxsBpp,   L"JxsBpp");

    std::wstring cs = reg_read_str(L"ColorSpaceIdx");
    if (!cs.empty()) {
        bool adobe = (cs == L"1");
        set_chk_val(g_app.hRadCsSrgb,  !adobe);
        set_chk_val(g_app.hRadCsAdobe, adobe);
    }

    set_chk_reg(g_app.hChkField,    L"EmitField");
    std::wstring fmc = reg_read_str(L"FieldModeCode");
    for (int i = 0; i < kNumFieldModes; ++i) if (fmc == kFieldModeCodes[i]) { SendMessageW(g_app.hCmbFieldMode, CB_SETCURSEL, (WPARAM)i, 0); break; }
    std::wstring fcsv = reg_read_str(L"FieldsCsv");
    if (!fcsv.empty()) apply_fields_csv(fcsv);

    set_chk_reg(g_app.hChkTransientTab2, L"EmitTransient");
    set_pair_reg(g_app.hSldTransSigma, g_app.hEdtTransSigma, L"TransientSigma");
    std::wstring tc = reg_read_str(L"TransientColor");
    if (!tc.empty()) seg_pill_set_sel(g_app.hSegTransColor, (tc == L"gray") ? 1 : 0);

    set_pair_reg(g_app.hSldCleanSens, g_app.hEdtCleanSens, L"CleanSens");
    {
        std::wstring vnd_v = reg_read_str(L"VndThresh");
        if (!vnd_v.empty()) {
            double clamped = std::clamp(_wtof(vnd_v.c_str()), 0.10, 1.00);
            wchar_t vbuf[32] = {0};
            std::swprintf(vbuf, 32, L"%.2f", clamped);
            set_tuned_pair(g_app.hSldVndThresh, g_app.hEdtVndThresh, vbuf);
        }
    }
    set_edt_reg(g_app.hEdtStrobeStep, L"StrobeStep");
    std::wstring sf = reg_read_str(L"StrobeFade");
    if (!sf.empty()) seg_pill_set_sel(g_app.hSegStrobeFade, (_wtoi(sf.c_str()) == 1) ? 1 : 0);

    std::wstring rm = reg_read_str(L"ReflectionMode");
    if (!rm.empty()) {
        if (rm == L"off") set_chk_val(g_app.hChkReflOn, false);
        else {
            set_chk_val(g_app.hChkReflOn, true);
            seg_pill_set_sel(g_app.hSegReflMode, (rm == L"split-layers") ? 1 : 0);
        }
    }

    set_chk_reg(g_app.hChkExportParallax, L"ExportParallax");
    set_pair_reg(g_app.hSldParallaxAmp, g_app.hEdtParallaxAmp, L"ParallaxAmp");
    set_chk_reg(g_app.hChkExportRefocus, L"ExportRefocus");
    set_pair_reg(g_app.hSldRefocusPos, g_app.hEdtRefocusPos, L"RefocusPos");
    set_pair_reg(g_app.hSldBokehStr, g_app.hEdtBokehStr, L"BokehStrength");
    if (g_app.hEdtBokehStf) set_pair_reg(g_app.hSldBokehStf, g_app.hEdtBokehStf, L"BokehStf");

    set_chk_reg(g_app.hChkExportTiles, L"ExportTiles");
    std::wstring ts = reg_read_str(L"TileSize");
    if (!ts.empty()) seg_pill_set_sel(g_app.hSegTileSize, (ts == L"512") ? 1 : 0);
    set_pair_reg(g_app.hSldTileConf, g_app.hEdtTileConf, L"TileMinConf");

    set_pair_reg(g_app.hSldDenoise,  g_app.hEdtDenoise,  L"Denoise");
    set_pair_reg(g_app.hSldChroma,   g_app.hEdtChroma,   L"Chroma");
    set_pair_reg(g_app.hSldPsfIters, g_app.hEdtPsfIters, L"PsfIters");
    set_pair_reg(g_app.hSldPsfSigma, g_app.hEdtPsfSigma, L"PsfSigma");
    set_pair_reg(g_app.hSldDeghost,  g_app.hEdtDeghost,  L"Deghost");
    set_pair_reg(g_app.hSldEv,       g_app.hEdtEv,       L"EvOffset");
    set_edt_reg(g_app.hEdtGrpSz,     L"GroupSize");
    {
        std::wstring gm = reg_read_str(L"GroupingMode");
        int gidx = (gm == L"single") ? 1 : (gm == L"best" ? 2 : (gm == L"top1" ? 3 : 0));
        SendMessageW(g_app.hCmbGrouping, CB_SETCURSEL, (WPARAM)gidx, 0);
    }
    set_edt_reg(g_app.hEdtThreads,   L"Threads");
    {
        std::wstring bd = reg_read_str(L"BatteryDevice");
        if (!bd.empty()) {
            g_app.is_battery_device = (bd == L"1");
        } else {
            g_app.is_battery_device = heraw_cache::query_power_status().has_battery;
            if (!g_app.is_screenshot_mode) {
                reg_write_str(L"BatteryDevice", g_app.is_battery_device ? L"1" : L"0");
            }
        }
    }

    std::wstring bi = reg_read_str(L"BackendIdx");
    if (!bi.empty()) {
        int bidx = _wtoi(bi.c_str());
        if (bidx >= 0 && bidx < 3) SendMessageW(g_app.hCmbBackend, CB_SETCURSEL, (WPARAM)bidx, 0);
    }
    if (g_app.hCmbCpuCores) {
        std::wstring cc = reg_read_str(L"CpuCoresCode");
        for (int i = 0; i < 5; ++i) {
            if (cc == kCpuCoresCodes[i]) { SendMessageW(g_app.hCmbCpuCores, CB_SETCURSEL, (WPARAM)i, 0); break; }
        }
    }
    if (g_app.hCmbPriority) {
        std::wstring pc = reg_read_str(L"PriorityCode");
        for (int i = 0; i < 4; ++i) {
            if (pc == kPriorityCodes[i]) { SendMessageW(g_app.hCmbPriority, CB_SETCURSEL, (WPARAM)i, 0); break; }
        }
    }
    refresh_engine_status_label();

    set_chk_reg(g_app.hChkSiliconClean, L"SiliconClean");
    set_chk_reg(g_app.hChkIbpOn,        L"IbpOn");
    {
        std::wstring ibpS = reg_read_str(L"IbpIters");
        if (!ibpS.empty()) set_ibp_iters_win(_wtoi(ibpS.c_str()));
        else set_ibp_iters_win(get_chk(g_app.hChkIbpOn) ? 1 : 0);
    }
    if (g_app.hEdtRgbPsfIters) set_pair_reg(g_app.hSldRgbPsfIters, g_app.hEdtRgbPsfIters, L"RgbPsfIters");
    if (g_app.hChkEdgePolish)  set_chk_reg(g_app.hChkEdgePolish, L"EdgePolish");
    set_chk_reg(g_app.hChkAutoLca,      L"AutoLca");
    set_chk_reg(g_app.hChkAutoExp,      L"AutoExp");
    set_chk_reg(g_app.hChkRolloff,      L"Rolloff");
    set_chk_reg(g_app.hChkDither,       L"Dither");

    {
        auto rd_def = [](const wchar_t* k, std::wstring& dst) {
            std::wstring v = reg_read_str(k);
            if (!v.empty()) dst = v;
        };
        rd_def(L"DecoderModeCode",  g_app.decoder_mode);
        rd_def(L"NevFramesSpec",    g_app.nev_frames_spec);
        rd_def(L"BurstGapSec",      g_app.burst_gap_s);
        rd_def(L"MinRelSharpness",  g_app.min_rel_sharpness);
        rd_def(L"MaxShiftPx",       g_app.max_shift_px);
        rd_def(L"LuckyModeCode",    g_app.lucky_mode);
        rd_def(L"MaxClusterFrames", g_app.max_cluster_frames);
    }

    select_cmb_code(g_app.hCmbMotionProfile, kMotionProfileCodes, reg_read_str(L"MotionProfileCode"));
    select_cmb_code(g_app.hCmbRgb1xTier,     kRgb1xTierCodes,     reg_read_str(L"Rgb1xTierCode"));
    select_cmb_code(g_app.hCmbExrPrec,         kExrPrecCodes,         reg_read_str(L"ExrPrecCode"));
    select_cmb_code(g_app.hCmbExrParts,        kExrPartsCodes,        reg_read_str(L"ExrPartsCode"));
    select_cmb_code(g_app.hCmbCodecConstraint, kCodecConstraintCodes, reg_read_str(L"CodecConstraintCode"));
    select_cmb_code(g_app.hCmbSatModel,        kSatModelCodes,        reg_read_str(L"SatModelCode"));
    select_cmb_code(g_app.hCmbHighlightMode, kHighlightModeCodes, reg_read_str(L"HighlightModeCode"));
    select_cmb_code(g_app.hCmbStreaming,     kStreamingCodes,     reg_read_str(L"StreamingModeCode"));
    set_edt_reg(g_app.hEdtMemBudget,     L"MemBudgetGb");
    select_cmb_code(g_app.hCmbShutterMode,   kShutterModeCodes,   reg_read_str(L"ShutterModeCode"));
    set_edt_reg(g_app.hEdtFlowKappa,     L"FlowKappa");
    select_cmb_code(g_app.hCmbIntraDeblur,   kIntraDeblurCodes,   reg_read_str(L"IntraDeblurCode"));
    select_cmb_code(g_app.hCmbDiffusionMode, kDiffusionCodes,     reg_read_str(L"DiffusionModeCode"));
    set_pair_reg(g_app.hSldDiffStrength, g_app.hEdtDiffStrength,  L"DiffusionStrength");
    select_cmb_code(g_app.hCmbUcGrade,       kUcGradeCodes,       reg_read_str(L"UcGradeCode"));
    select_cmb_code(g_app.hCmbUcBaffle,      kUcBaffleCodes,      reg_read_str(L"UcBaffleCode"));
    select_cmb_code(g_app.hCmbUcChroma,      kUcChromaCodes,      reg_read_str(L"UcChromaCode"));
    select_cmb_code(g_app.hCmbApertureWeight,kApertureWeightCodes,reg_read_str(L"ApertureWeightCode"));
    select_cmb_code(g_app.hCmbMetricPolicy,  kMetricPolicyCodes,  reg_read_str(L"MetricPolicyCode"));
    if (g_app.hCmbRefocusMode)  select_cmb_code(g_app.hCmbRefocusMode,  kRefocusModeCodes, reg_read_str(L"RefocusModeCode"));
    if (g_app.hChkRefocusAuto)  set_chk_reg(g_app.hChkRefocusAuto, L"RefocusAutoPos");
    if (g_app.hEdtBokehSa)      set_pair_reg(g_app.hSldBokehSa, g_app.hEdtBokehSa, L"BokehSa");
    if (g_app.hEdtVirtualFstop) set_edt_reg(g_app.hEdtVirtualFstop, L"VirtualFstop");
    if (g_app.hCmbBokehBlades)  select_cmb_code(g_app.hCmbBokehBlades, kBokehBladeCodes, reg_read_str(L"BokehBladesCode"));
    if (g_app.hEdtBokehAngle)   set_edt_reg(g_app.hEdtBokehAngle, L"BokehAngle");
    if (g_app.hEdtTiltX)        set_edt_reg(g_app.hEdtTiltX, L"TiltPlaneX");
    if (g_app.hEdtTiltY)        set_edt_reg(g_app.hEdtTiltY, L"TiltPlaneY");
    if (g_app.hCmbSeeThrough)   select_cmb_code(g_app.hCmbSeeThrough, kSeeThroughCodes, reg_read_str(L"SeeThroughCode"));
    if (g_app.hCmbDepthGate) {
        std::wstring dgc = reg_read_str(L"DepthGateCode");
        select_cmb_code(g_app.hCmbDepthGate, kDepthGateCodes, dgc);
        if (g_app.hCmbW2DepthGate) select_cmb_code(g_app.hCmbW2DepthGate, kDepthGateCodes, dgc);
    }
    auto set_auto_edt_reg = [](HWND hEdt, const wchar_t* k) {
        if (!hEdt) return;
        std::wstring v = reg_read_str(k);
        if (!v.empty()) {
            g_app.suppress_sync = true;
            SetWindowTextW(hEdt, (v == L"__AUTO__") ? L"" : v.c_str());
            g_app.suppress_sync = false;
        }
    };
    set_auto_edt_reg(g_app.hEdtDepthDisp, L"DepthMinDisp");
    set_auto_edt_reg(g_app.hEdtDepthCoh,  L"DepthMinCoh");
    set_auto_edt_reg(g_app.hEdtDepthSup,  L"DepthMinSupport");
    if (g_app.hCmbObsGate) select_cmb_code(g_app.hCmbObsGate, kObsGateCodes, reg_read_str(L"ObserverGateCode"));
    set_auto_edt_reg(g_app.hEdtReadoutMs, L"ReadoutMs");
    if (g_app.hEdtObsPitch)   set_edt_reg(g_app.hEdtObsPitch,   L"ObsPitchDeg");
    if (g_app.hEdtPlateScale) set_edt_reg(g_app.hEdtPlateScale, L"PlateScale");
    if (g_app.hChkEmitDiffuse)    set_chk_reg(g_app.hChkEmitDiffuse,    L"EmitDiffuse");
    if (g_app.hChkEmitLightSplit) set_chk_reg(g_app.hChkEmitLightSplit, L"EmitLightSplit");
    if (g_app.hChkEvalSelf)       set_chk_reg(g_app.hChkEvalSelf,       L"EvalSelf");
    {
        std::wstring ocsv = reg_read_str(L"ObserversCsv");
        if (!ocsv.empty()) {
            apply_observers_csv_win(ocsv == L"none" ? L"" : ocsv);
        } else {
            std::wstring leg_obs = reg_read_str(L"ObserversCode");
            if (!leg_obs.empty()) {
                apply_observers_csv_win(leg_obs);
                if (g_app.hChkWriteObsReport) set_chk_val(g_app.hChkWriteObsReport, true);
            }
        }
    }
    if (g_app.hChkWriteObsReport) set_chk_reg(g_app.hChkWriteObsReport, L"WriteObserverReport");
    set_chk_reg(g_app.hChkNextShotPlan,  L"NextShotPlan");
    select_cmb_code(g_app.hCmbSpectralCa,    kSpectralCaCodes,    reg_read_str(L"SpectralCaModeCode"));
    set_pair_reg(g_app.hSldSpectralPwr,  g_app.hEdtSpectralPwr,   L"SpectralCaPower");
    select_cmb_code(g_app.hCmbSpectralAng,   kSpectralAngleCodes, reg_read_str(L"SpectralCaAngleCode"));
    if (g_app.hEdtSpectralDeg) set_edt_reg(g_app.hEdtSpectralDeg, L"SpectralCaCustomDeg");
    set_pair_reg(g_app.hSldBokehDeNisen, g_app.hEdtBokehDeNisen,  L"BokehDeNisen");
    set_pair_reg(g_app.hSldBokehSwirl,   g_app.hEdtBokehSwirl,    L"BokehSwirl");
    if (g_app.hEdtBokehAspect) set_pair_reg(g_app.hSldBokehAspect, g_app.hEdtBokehAspect, L"BokehAspectCode");
    set_pair_reg(g_app.hSldBokehOnion,   g_app.hEdtBokehOnion,    L"BokehOnionClean");
    {
        std::wstring bip = reg_read_str(L"BusInPath");
        commit_bus_in_edit_win(bip);
    }
    set_chk_reg(g_app.hChkBusExport,     L"BusExport");

    std::wstring tabS = reg_read_str(L"ActiveTabIdx");
    if (!tabS.empty()) {
        int ti = _wtoi(tabS.c_str());
        switch_tab(std::clamp(ti, 0, 2));
    } else {
        sync_cross_tab_ui();
    }
    g_app.loading_prefs = false;
    sync_cross_tab_ui();
}

// ============================================================================
// Subprocess Pipe Helper & Async --plan-json / --optics-only / Synthesis Jobs
// ============================================================================
static std::string run_cli_capture_stdout(const std::vector<std::wstring>& args, DWORD* out_exit = nullptr,
                                         HANDLE* slot = nullptr, uint64_t generation = 0,
                                         const char* diag_label = "CLI Probe") {
    if (out_exit) *out_exit = 1;
    std::string cli_u8 = args.empty() ? "" : wide_to_utf8(args[0]);
    std::vector<std::string> rest_u8;
    for (size_t i = 1; i < args.size(); ++i) rest_u8.push_back(wide_to_utf8(args[i]));
    int task_id = heraw_i18n::diag_log().begin_task(diag_label ? diag_label : "CLI", cli_u8, rest_u8);

    std::unique_lock<std::mutex> spawn_lock(g_app.proc_mtx);
    std::wstring cmd = join_cmd_args(args);
    SECURITY_ATTRIBUTES sa{};
    sa.nLength = sizeof(SECURITY_ATTRIBUTES);
    sa.bInheritHandle = TRUE;
    HANDLE hR = nullptr, hW = nullptr;
    if (!CreatePipe(&hR, &hW, &sa, 0)) {
        heraw_i18n::diag_log().end_task(task_id, -1, false);
        return "";
    }
    SetHandleInformation(hR, HANDLE_FLAG_INHERIT, 0);

    STARTUPINFOW si{};
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESTDHANDLES | STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_HIDE;
    si.hStdOutput = hW;
    si.hStdError  = hW;
    PROCESS_INFORMATION pi{};
    std::vector<wchar_t> mcmd(cmd.begin(), cmd.end());
    mcmd.push_back(L'\0');

    if (!CreateProcessW(nullptr, mcmd.data(), nullptr, nullptr, TRUE, CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi)) {
        CloseHandle(hW);
        CloseHandle(hR);
        heraw_i18n::diag_log().end_task(task_id, -1, false);
        return "";
    }
    CloseHandle(hW);
    if (slot) *slot = pi.hProcess;
    if (g_app.stopping || (generation && generation != g_app.plan_gen) ||
        (slot == &g_app.hOpticsProc && g_app.cancel_job)) TerminateProcess(pi.hProcess, 130);
    spawn_lock.unlock();

    std::string out;
    char buf[2048];
    DWORD nr = 0;
    while (ReadFile(hR, buf, sizeof(buf), &nr, nullptr) && nr > 0) {
        out.append(buf, nr);
        heraw_i18n::diag_log().append_task_output(task_id, buf, nr);
    }
    CloseHandle(hR);
    WaitForSingleObject(pi.hProcess, INFINITE);
    DWORD ec = 1;
    GetExitCodeProcess(pi.hProcess, &ec);
    if (out_exit) *out_exit = ec;
    heraw_i18n::diag_log().end_task(task_id, (int)ec, ec == 130);
    {
        std::lock_guard<std::mutex> lk(g_app.proc_mtx);
        if (slot) *slot = nullptr;
    }
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    return out;
}

static std::string read_whole_file_utf8(const std::wstring& path) {
    FILE* fp = _wfopen(path.c_str(), L"rb");
    if (!fp) return "";
    std::fseek(fp, 0, SEEK_END);
    long sz = std::ftell(fp);
    std::fseek(fp, 0, SEEK_SET);
    if (sz <= 0 || sz > 16 * 1024 * 1024) {
        std::fclose(fp);
        return "";
    }
    std::string data((size_t)sz, '\0');
    size_t rd = std::fread(&data[0], 1, (size_t)sz, fp);
    data.resize(rd);
    std::fclose(fp);
    return data;
}

static bool write_utf8_file(const std::wstring& path, const std::string& data) {
    FILE* file = _wfopen(path.c_str(), L"wb");
    if (!file) return false;
    bool written = std::fwrite(data.data(), 1, data.size(), file) == data.size();
    int closed = std::fclose(file);
    return written && closed == 0;
}

static std::wstring infer_sensor_badge(const std::string& cjson, const std::vector<FrameDiagItem>& frames, double ev_spread) {
    std::string cfa = extract_json_str(cjson, "cfa");
    if (cfa.empty()) cfa = extract_json_str(cjson, "cfa_type");
    if (cfa.empty()) cfa = extract_json_str(cjson, "sensor_type");
    std::string model = extract_json_str(cjson, "model");
    std::string sht   = extract_json_str(cjson, "shutter_type");
    if (cfa.find("global") != std::string::npos || sht.find("global") != std::string::npos ||
        model.find("9M3") != std::string::npos || model.find("A9 III") != std::string::npos) {
        return heraw_i18n::I18nManager::instance().is_english() ? L"Global" : L"全域";
    }
    if (cfa.find("xtrans") != std::string::npos || cfa.find("X-Trans") != std::string::npos ||
        cfa.find("6x6") != std::string::npos) {
        return L"X-Trans";
    }
    if (cfa == "bayer") return L"Bayer";
    for (const auto& f : frames) {
        std::wstring u = f.file;
        std::transform(u.begin(), u.end(), u.begin(), ::towupper);
        if (u.size() >= 4 && u.substr(u.size() - 4) == L".RAF") return L"X-Trans";
    }
    if (ev_spread >= 0.85) return L"AEB";
    return L"Bayer";
}

static std::vector<BurstGroupItem> parse_plan_json_groups(const std::string& out, const std::vector<std::wstring>& fallback_inputs) {
    std::vector<BurstGroupItem> parsed;
    size_t cArr = out.find("\"clusters\"");
    if (cArr == std::string::npos) return parsed;
    size_t cOpen = out.find('[', cArr);
    size_t cEnd  = find_matching_brace(out, cOpen, '[', ']');
    if (cEnd == std::string::npos) cEnd = out.size();

    size_t pos = cOpen + 1;
    int ordinal = 1;
    while (pos < cEnd) {
        size_t objStart = out.find('{', pos);
        if (objStart == std::string::npos || objStart >= cEnd) break;
        size_t objEnd = find_matching_brace(out, objStart, '{', '}');
        if (objEnd == std::string::npos || objEnd > cEnd) break;

        std::string cj = out.substr(objStart, objEnd - objStart + 1);
        BurstGroupItem grp;
        grp.cluster_idx = parse_json_int(cj, "cluster_index", ordinal - 1);
        grp.scene_code  = extract_json_str(cj, "scene");
        grp.rec_preset  = extract_json_str(cj, "rec_preset");
        if (grp.rec_preset.empty()) grp.rec_preset = "photo";
        grp.rec_anchor  = extract_json_str(cj, "rec_anchor");
        grp.best_anchor_1based = parse_json_int(cj, "best_anchor", 1);
        grp.quad_coverage = parse_json_int(cj, "quad_coverage", 0);
        grp.sr_score      = parse_json_int(cj, "sr_score", 0);
        grp.star_count    = parse_json_int(cj, "star_count", 0);
        grp.motion_pct    = parse_json_double(cj, "motion_pct", 0.0);
        grp.ev_spread     = parse_json_double(cj, "ev_spread", 0.0);
        grp.rec_denoise   = parse_json_double(cj, "rec_denoise", 1.0);
        grp.rec_deghost   = parse_json_double(cj, "rec_deghost", 1.0);
        grp.rec_psf_iters = parse_json_int(cj, "rec_psf_iters", 4);
        grp.shutter_shock = parse_json_bool(cj, "shutter_shock", false);
        grp.focus_distance_m = parse_json_double(cj, "focus_distance_m", 0.0);
        grp.focus_is_infinity = parse_json_bool(cj, "focus_is_infinity", false) || (grp.focus_distance_m >= 999999.0);
        grp.raw_width  = parse_json_int(cj, "width", 0);
        grp.raw_height = parse_json_int(cj, "height", 0);
        grp.has_telemetry = true;

        // Parse frames array inside cluster
        size_t fKey = cj.find("\"frames\"");
        if (fKey != std::string::npos) {
            size_t fOpen = cj.find('[', fKey);
            size_t fClose = find_matching_brace(cj, fOpen, '[', ']');
            if (fClose != std::string::npos) {
                size_t fp = fOpen + 1;
                while (fp < fClose) {
                    size_t fo = cj.find('{', fp);
                    if (fo == std::string::npos || fo >= fClose) break;
                    size_t fc = find_matching_brace(cj, fo, '{', '}');
                    if (fc == std::string::npos || fc > fClose) break;

                    FrameDiagItem fd;
                    fd.idx_1based = parse_json_int(cj, "idx", (int)grp.frames.size() + 1, fo, fc);
                    fd.assigned_group = parse_json_int(cj, "group", 0, fo, fc);
                    std::string ffile = extract_json_str(cj, "file", fo, fc);
                    std::string fpath = extract_json_str(cj, "path", fo, fc);
                    fd.file  = utf8_to_wide(ffile);
                    fd.flags = utf8_to_wide(extract_json_str(cj, "flags", fo, fc));
                    if (fd.flags.empty()) fd.flags = L"OK";
                    fd.acutance = parse_json_double(cj, "af_score",
                        parse_json_double(cj, "af_sharpness",
                            parse_json_double(cj, "acutance", 0.0, fo, fc), fo, fc), fo, fc);
                    fd.shift_px = parse_json_double(cj, "shift", 0.0, fo, fc);
                    fd.zncc     = parse_json_double(cj, "zncc", 0.99, fo, fc);
                    fd.is_crown = parse_json_bool(cj, "crown", false, fo, fc);
                    fd.is_shock = parse_json_bool(cj, "shock", false, fo, fc);
                    if ((has_json_key(cj, "dx_1x", fo, fc) && has_json_key(cj, "dy_1x", fo, fc)) ||
                        (has_json_key(cj, "dx", fo, fc) && has_json_key(cj, "dy", fo, fc))) {
                        fd.dx = parse_json_double(cj, "dx_1x", parse_json_double(cj, "dx", 0.0, fo, fc), fo, fc);
                        fd.dy = parse_json_double(cj, "dy_1x", parse_json_double(cj, "dy", 0.0, fo, fc), fo, fc);
                    } else grp.has_telemetry = false;
                    fd.included = (fd.flags.find(L"DARK") == std::wstring::npos &&
                                   fd.flags.find(L"BLUR") == std::wstring::npos &&
                                   fd.flags.find(L"EXP_OUTLIER") == std::wstring::npos &&
                                   fd.flags.find(L"EXPOSURE") == std::wstring::npos &&
                                   fd.flags.find(L"DIM_MISMATCH") == std::wstring::npos);

                    if (!fpath.empty()) {
                        fd.full_path = utf8_to_wide(fpath);
                    } else {
                        for (const auto& inp : fallback_inputs) {
                            if (_wcsicmp(basename_of_w(inp).c_str(), fd.file.c_str()) == 0) {
                                fd.full_path = inp;
                                break;
                            }
                        }
                    }
                    if (!fd.full_path.empty()) grp.paths.push_back(fd.full_path);
                    if (fd.shift_px > grp.max_drift_px) grp.max_drift_px = fd.shift_px;
                    grp.frames.push_back(fd);
                    fp = fc + 1;
                }
            }
        }

        if (has_json_key(cj, "max_drift_px")) grp.max_drift_px = parse_json_double(cj, "max_drift_px", grp.max_drift_px);
        if (has_json_key(cj, "max_drift_2x_px")) grp.max_drift_px = parse_json_double(cj, "max_drift_2x_px", 0.0) * 0.5;
        if (has_json_key(cj, "max_parallax_2x_px")) {
            grp.max_parallax_2x_px = parse_json_double(cj, "max_parallax_2x_px", 0.0);
        } else if (has_json_key(cj, "parallax_2x_px")) {
            grp.max_parallax_2x_px = parse_json_double(cj, "parallax_2x_px", 0.0);
        } else {
            grp.max_parallax_2x_px = 0.0;
        }
        // --plan-json runs on 1/8 thumbnails; leave real depth & 11-field physical gates open until synthesis or --optics-report
        grp.parallax_measured = false;
        grp.depth_known = false;
        grp.depth_observable = false;
        grp.field_observability.clear();

        int min_req = min_valid_frames_win();
        grp.camera_model = extract_json_str(cj, "model");
        grp.sensor_badge = infer_sensor_badge(cj, grp.frames, grp.ev_spread);

        std::vector<int> group_ids;
        int nonneg_groups = 0;
        for (const auto& frame : grp.frames) {
            if (std::find(group_ids.begin(), group_ids.end(), frame.assigned_group) == group_ids.end()) {
                group_ids.push_back(frame.assigned_group);
                if (frame.assigned_group >= 0) ++nonneg_groups;
            }
        }
        std::sort(group_ids.begin(), group_ids.end());
        if (nonneg_groups <= 1 && !grp.frames.empty()) {
            BurstGroupItem sub = grp;
            sub.frames.clear();
            sub.paths.clear();
            int valid_cnt = 0;
            for (const auto& frame : grp.frames) {
                auto copy = frame;
                copy.idx_1based = (int)sub.frames.size() + 1;
                sub.frames.push_back(copy);
                if (!copy.full_path.empty()) sub.paths.push_back(copy.full_path);
                if (copy.included) ++valid_cnt;
            }
            recalculate_group_item(sub, ordinal++);
            sub.checked = nonneg_groups >= 1 && valid_cnt >= min_req && sub.paths.size() == sub.frames.size();
            parsed.push_back(std::move(sub));
        } else {
            for (int id : group_ids) {
                if (id < 0 && nonneg_groups > 0) continue;
                BurstGroupItem sub = grp;
                sub.frames.clear();
                sub.paths.clear();
                int valid_cnt = 0;
                for (const auto& frame : grp.frames) {
                    if (frame.assigned_group != id) continue;
                    auto copy = frame;
                    copy.idx_1based = (int)sub.frames.size() + 1;
                    sub.frames.push_back(copy);
                    if (!copy.full_path.empty()) sub.paths.push_back(copy.full_path);
                    if (copy.included) ++valid_cnt;
                }
                if (sub.frames.empty()) continue;
                recalculate_group_item(sub, ordinal++);
                sub.checked = id >= 0 && valid_cnt >= min_req && sub.paths.size() == sub.frames.size();
                parsed.push_back(std::move(sub));
            }
        }
        pos = objEnd + 1;
    }

    int min_req = min_valid_frames_win();
    size_t valid_burst_cnt = 0;
    std::vector<size_t> single_stray_indices;
    for (size_t i = 0; i < parsed.size(); ++i) {
        const auto& gm = parsed[i];
        if ((int)gm.frames.size() >= min_req || gm.checked) {
            ++valid_burst_cnt;
        } else if (gm.frames.size() == 1 && !gm.checked && gm.frames.front().assigned_group < 0) {
            single_stray_indices.push_back(i);
        }
    }
    if ((valid_burst_cnt >= 1 && single_stray_indices.size() >= 2) || single_stray_indices.size() >= 4) {
        BurstGroupItem archive;
        archive.is_stray_archive = true;
        archive.checked = false;
        archive.scene_code = "single_shot";
        archive.rec_preset = "photo";
        for (size_t si : single_stray_indices) {
            for (auto f : parsed[si].frames) {
                f.idx_1based = (int)archive.frames.size() + 1;
                f.is_crown = false;
                if (!f.full_path.empty()) archive.paths.push_back(f.full_path);
                if (f.included) ++archive.valid_frame_count;
                archive.frames.push_back(std::move(f));
            }
        }
        for (int k = (int)single_stray_indices.size() - 1; k >= 0; --k) {
            parsed.erase(parsed.begin() + (ptrdiff_t)single_stray_indices[(size_t)k]);
        }
        recalculate_group_item(archive, (int)parsed.size() + 1);
        parsed.push_back(std::move(archive));
        for (size_t i = 0; i + 1 < parsed.size(); ++i) {
            recalculate_group_item(parsed[i], (int)i + 1);
        }
    }
    return parsed;
}

static BurstGroupItem make_provisional_group(const std::vector<std::wstring>& inputs) {
    BurstGroupItem grp;
    grp.checked = false;
    grp.paths = inputs;
    int n = (int)inputs.size();
    bool any_dir = false;
    for (size_t i = 0; i < inputs.size(); ++i) {
        if (path_is_dir(inputs[i])) {
            any_dir = true;
        } else {
            FrameDiagItem fd;
            fd.idx_1based = (int)grp.frames.size() + 1;
            fd.file = basename_of_w(inputs[i]);
            fd.full_path = inputs[i];
            fd.is_crown = (fd.idx_1based == 1);
            grp.frames.push_back(fd);
        }
    }
    const bool en = heraw_i18n::I18nManager::instance().is_english();
    wchar_t tb[96] = {0};
    if (any_dir) std::swprintf(tb, 96, en ? L"Grp 01 · Folder Pending (%d)" : L"组 01 · 目录待扫描 (%d 项)", n);
    else std::swprintf(tb, 96, en ? L"Grp 01 · %df" : L"组 01 · %d 张", (int)grp.frames.size());
    grp.title = tb;
    if (!grp.frames.empty()) {
        grp.span_label = (grp.frames.size() > 1)
            ? (stem_of_w(grp.frames.front().file) + L"–" + stem_of_w(grp.frames.back().file))
            : stem_of_w(grp.frames.front().file);
    }
    grp.sensor_badge = infer_sensor_badge("", grp.frames, 0.0);
    grp.meta_line = heraw_i18n::tr_w("dock.well_scanning");
    return grp;
}

static void append_intake_cli_args_win(std::vector<std::wstring>& dst, bool for_plan_scan) {
    if (!g_app.decoder_mode.empty() && g_app.decoder_mode != L"auto") {
        dst.push_back(L"--decoder");
        dst.push_back(g_app.decoder_mode);
    }
    std::wstring nev = trim_ws(g_app.nev_frames_spec);
    if (for_plan_scan) {
        dst.push_back(L"--nev-frames");
        dst.push_back(nev.empty() ? L"all" : nev);
    } else if (!nev.empty() && nev != L"all") {
        dst.push_back(L"--nev-frames");
        dst.push_back(nev);
    }
    std::wstring gap = trim_ws(g_app.burst_gap_s);
    if (!gap.empty() && std::fabs(_wtof(gap.c_str()) - 3.5) > 1e-3) {
        dst.push_back(L"--burst-gap-s");
        dst.push_back(gap);
    }
    std::wstring shp = trim_ws(g_app.min_rel_sharpness);
    if (!shp.empty() && std::fabs(_wtof(shp.c_str()) - 0.72) > 1e-3) {
        dst.push_back(L"--min-rel-sharpness");
        dst.push_back(shp);
    }
    std::wstring shf = trim_ws(g_app.max_shift_px);
    if (!shf.empty() && std::fabs(_wtof(shf.c_str()) - 120.0) > 1e-2) {
        dst.push_back(L"--max-shift-px");
        dst.push_back(shf);
    }
    if (!g_app.lucky_mode.empty() && g_app.lucky_mode != L"auto") {
        dst.push_back(L"--lucky-imaging");
        dst.push_back(g_app.lucky_mode);
    }
    std::wstring mcf = trim_ws(g_app.max_cluster_frames);
    if (!mcf.empty() && _wtoi(mcf.c_str()) > 0 && _wtoi(mcf.c_str()) != 128) {
        dst.push_back(L"--max-cluster-frames");
        dst.push_back(mcf);
    }
}

static void trigger_async_plan_scan() {
    uint64_t my_gen = ++g_app.plan_gen;
    {
        std::lock_guard<std::mutex> lk(g_app.proc_mtx);
        if (g_app.hPlanProc) TerminateProcess(g_app.hPlanProc, 130);
    }
    if (g_app.plan_worker.joinable()) g_app.plan_worker.join();
    g_app.optics = OpticsTelemetry{};
    if (g_app.inputs.empty()) {
        g_app.grouping_dirty = false;
        g_app.groups.clear();
        g_app.selected_group = 0;
        g_app.plan_scanning.store(false);
        sync_cross_tab_ui();
        if (g_app.hBurstView) InvalidateRect(g_app.hBurstView, nullptr, FALSE);
        if (g_app.hFrameView) InvalidateRect(g_app.hFrameView, nullptr, FALSE);
        if (g_app.hOpticsCanvas) InvalidateRect(g_app.hOpticsCanvas, nullptr, FALSE);
        if (g_app.hParallaxMeter) InvalidateRect(g_app.hParallaxMeter, nullptr, FALSE);
        set_status_ui(heraw_i18n::tr_w("bot.ready_idle"));
        return;
    }

    int grp_mode = (int)SendMessageW(g_app.hCmbGrouping, CB_GETCURSEL, 0, 0);
    int min_req = min_valid_frames_win();
    if (grp_mode != 1 && _wtoi(get_wnd_text(g_app.hEdtGrpSz).c_str()) < min_req) {
        g_app.grouping_dirty = true;
        g_app.plan_scanning.store(false);
        sync_cross_tab_ui();
        SetWindowTextW(g_app.hLblGroupingHint, heraw_i18n::trf_w("hint.min_grp_sz_fmt", min_req).c_str());
        return;
    }
    if (get_wnd_text(g_app.hOutDir).empty()) {
        std::wstring base = parent_dir_of(g_app.inputs[0]);
        if (!base.empty()) {
            std::wstring auto_dir = base + L"\\nijinLBS_out";
            g_app.auto_out_dir = auto_dir;
            g_app.suppress_sync = true;
            SetWindowTextW(g_app.hOutDir, auto_dir.c_str());
            g_app.suppress_sync = false;
        }
    }

    g_app.groups = {make_provisional_group(g_app.inputs)};
    g_app.selected_group = 0;
    sync_cross_tab_ui();
    if (g_app.hBurstView) InvalidateRect(g_app.hBurstView, nullptr, FALSE);
    if (g_app.hFrameView) InvalidateRect(g_app.hFrameView, nullptr, FALSE);

    std::wstring exe = file_exists_regular(g_app.cli_path) ? g_app.cli_path : auto_detect_cli_exe(true);
    if (exe.empty()) return;

    std::vector<std::wstring> args;
    args.push_back(exe);
    if (!g_app.libraw_path.empty() && file_exists_regular(g_app.libraw_path)) {
        args.push_back(L"--libraw");
        args.push_back(g_app.libraw_path);
    }
    args.push_back(get_chk(g_app.hChkSortTime) ? L"--sort-time" : L"--no-sort-time");
    if (get_chk(g_app.hChkRecursive)) args.push_back(L"--recursive");
    args.push_back(L"--policy");
    bool single_group = (grp_mode == 1);
    bool best_per_burst = (grp_mode == 2 || grp_mode == 3);
    args.push_back(single_group ? L"force-all" : (best_per_burst ? L"auto-prune" : L"multi-group"));
    if (single_group) args.push_back(L"--single-group");
    std::wstring gsz_str = get_wnd_text(g_app.hEdtGrpSz);
    args.push_back(L"--group-size");
    args.push_back(gsz_str);
    if (best_per_burst) {
        args.push_back(L"--max-frames");
        args.push_back(gsz_str);
    }
    std::wstring mp = selected_cmb_code(g_app.hCmbMotionProfile, kMotionProfileCodes, L"auto");
    if (mp != L"auto") {
        args.push_back(L"--motion-profile");
        args.push_back(mp);
    }
    append_intake_cli_args_win(args, true);
    args.push_back(L"--plan-json");
    std::vector<std::wstring> snap_inputs = g_app.inputs;
    for (const auto& p : snap_inputs) args.push_back(p);

    g_app.plan_scanning.store(true);
    g_app.grouping_dirty = false;
    sync_cross_tab_ui();
    set_status_ui(heraw_i18n::tr_w("st.scanning"));

    g_app.plan_worker = std::thread([args, snap_inputs, my_gen]() {
        DWORD ec = 1;
        std::string json_out = run_cli_capture_stdout(args, &ec, &g_app.hPlanProc, my_gen, "--plan-json");
        if (my_gen != g_app.plan_gen.load()) return;
        auto* payload = new std::vector<BurstGroupItem>(ec == 0 ? parse_plan_json_groups(json_out, snap_inputs) : std::vector<BurstGroupItem>{});
        if (!PostMessageW(g_app.hwnd, WM_APP_PLAN_READY, (WPARAM)my_gen, reinterpret_cast<LPARAM>(payload))) {
            delete payload;
        }
    });
}

static void add_unique_input(const std::wstring& p) {
    if (p.empty()) return;
    if (std::find(g_app.inputs.begin(), g_app.inputs.end(), p) == g_app.inputs.end()) {
        g_app.inputs.push_back(p);
    }
}

static void pick_raw_files_dialog(HWND owner) {
    std::vector<wchar_t> buf(65536, L'\0');
    OPENFILENAMEW ofn{};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = owner;
    ofn.lpstrFilter = L"Camera RAW Files (*.NEF;*.NRW;*.NEV;*.JXS;*.DXO;*.4RW;*.DNG;*.ARW;*.CR2;*.CR3;*.RAF;*.ORF;*.RW2;*.SRW)\0"
                      L"*.NEF;*.NRW;*.NEV;*.JXS;*.DXO;*.4RW;*.DNG;*.ARW;*.CR2;*.CR3;*.RAF;*.ORF;*.RW2;*.PEF;*.SRW;*.IIQ\0All Files (*.*)\0*.*\0";
    ofn.lpstrFile = buf.data();
    ofn.nMaxFile = (DWORD)buf.size();
    ofn.Flags = OFN_EXPLORER | OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_ALLOWMULTISELECT;
    if (!GetOpenFileNameW(&ofn)) return;

    const wchar_t* p = buf.data();
    std::wstring first = p;
    p += first.size() + 1;
    if (*p == L'\0') add_unique_input(first);
    else {
        std::vector<std::wstring> sel;
        while (*p != L'\0') {
            std::wstring nm = p;
            sel.push_back(first + L"\\" + nm);
            p += nm.size() + 1;
        }
        std::sort(sel.begin(), sel.end());
        for (const auto& f : sel) add_unique_input(f);
    }
    trigger_async_plan_scan();
}

static std::vector<std::wstring> collect_active_input_paths() {
    std::vector<std::wstring> out;
    bool any_group_paths = false;
    for (const auto& grp : g_app.groups) {
        if (!grp.paths.empty()) any_group_paths = true;
        if (grp.checked) {
            for (const auto& p : grp.paths) out.push_back(p);
        }
    }
    if (!any_group_paths && out.empty()) return g_app.inputs;
    return out;
}

static std::wstring build_skip_frames_csv(const BurstGroupItem* grp) {
    if (!grp) return L"";
    std::wstring csv;
    for (const auto& f : grp->frames) {
        if (!f.included) {
            if (!csv.empty()) csv += L",";
            csv += std::to_wstring(f.idx_1based);
        }
    }
    return csv;
}

static void set_prog_pct(int pct) {
    g_app.prog_pct = std::max(0, std::min(100, pct));
    if (g_app.hwnd) {
        RECT crc{};
        GetClientRect(g_app.hwnd, &crc);
        RECT bar{g_app.S(20), crc.bottom - g_app.S(72), crc.right - g_app.S(20), crc.bottom - g_app.S(10)};
        InvalidateRect(g_app.hwnd, &bar, FALSE);
    }
}

static void set_running_ui(bool busy) {
    g_app.running.store(busy);
    sync_cross_tab_ui();
}

static void launch_async_cli_job(const std::vector<std::wstring>& args, const std::wstring& start_msg,
                                 bool reset_progress = true, const char* diag_label = "CLI Task") {
    if (g_app.running) return;
    g_app.cancel_job = false;
    std::wstring cmdline = join_cmd_args(args);
    std::string cli_u8 = args.empty() ? "" : wide_to_utf8(args[0]);
    std::vector<std::string> rest_u8;
    for (size_t i = 1; i < args.size(); ++i) rest_u8.push_back(wide_to_utf8(args[i]));
    int task_id = heraw_i18n::diag_log().begin_task(diag_label ? diag_label : "CLI Task", cli_u8, rest_u8);
    {
        std::lock_guard<std::mutex> lk(g_app.proc_mtx);
        g_app.last_cli_output.clear();
    }
    if (reset_progress) set_prog_pct(0);
    set_status_ui(start_msg);
    set_running_ui(true);

    if (g_app.worker.joinable()) g_app.worker.join();
    g_app.worker = std::thread([cmdline, task_id]() {
        std::unique_lock<std::mutex> spawn_lock(g_app.proc_mtx);
        SECURITY_ATTRIBUTES sa{};
        sa.nLength = sizeof(SECURITY_ATTRIBUTES);
        sa.bInheritHandle = TRUE;
        HANDLE hR = nullptr, hW = nullptr;
        if (!CreatePipe(&hR, &hW, &sa, 0)) {
            heraw_i18n::diag_log().end_task(task_id, 1, false);
            PostMessageW(g_app.hwnd, WM_APP_DONE, 1, 0);
            return;
        }
        SetHandleInformation(hR, HANDLE_FLAG_INHERIT, 0);

        STARTUPINFOW si{};
        si.cb = sizeof(si);
        si.dwFlags = STARTF_USESTDHANDLES | STARTF_USESHOWWINDOW;
        si.wShowWindow = SW_HIDE;
        si.hStdOutput = hW;
        si.hStdError  = hW;
        PROCESS_INFORMATION pi{};
        std::vector<wchar_t> mcmd(cmdline.begin(), cmdline.end());
        mcmd.push_back(L'\0');

        BOOL ok = CreateProcessW(nullptr, mcmd.data(), nullptr, nullptr, TRUE, CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi);
        CloseHandle(hW);
        if (!ok) {
            CloseHandle(hR);
            heraw_i18n::diag_log().end_task(task_id, 1, false);
            post_status(heraw_i18n::trf_w("st.launch_err_fmt", "CreateProcessW failed"));
            PostMessageW(g_app.hwnd, WM_APP_DONE, 1, 0);
            return;
        }
        g_app.hChildProc = pi.hProcess;
        if (g_app.stopping || g_app.cancel_job) TerminateProcess(pi.hProcess, 130);
        spawn_lock.unlock();

        std::string full_out;
        std::string pending;
        char buf[1024];
        DWORD nr = 0;
        while (ReadFile(hR, buf, sizeof(buf), &nr, nullptr) && nr > 0) {
            heraw_i18n::diag_log().append_task_output(task_id, buf, nr);
            if (full_out.size() < 65536) full_out.append(buf, std::min<size_t>(nr, 65536 - full_out.size()));
            pending.append(buf, nr);
            size_t nl = 0;
            while ((nl = pending.find('\n')) != std::string::npos) {
                std::string line = pending.substr(0, nl);
                pending.erase(0, nl + 1);
                if (!line.empty() && line.back() == '\r') line.pop_back();
                if (line.empty()) continue;
                if (line.front() == '{' && line.find("\"event\":") != std::string::npos) {
                    int pct = parse_json_int(line, "percent", -1);
                    if (pct >= 0) PostMessageW(g_app.hwnd, WM_APP_PROGRESS, (WPARAM)pct, 0);
                    std::string stage = extract_json_str(line, "stage");
                    if (stage.empty()) stage = extract_json_str(line, "message");
                    if (!stage.empty()) post_status(utf8_to_wide(stage));
                } else if (line.rfind("[Cost]", 0) != 0 &&
                           line.rfind("[BackendCost]", 0) != 0 &&
                           line.rfind("[C1Cost]", 0) != 0 &&
                           line.rfind("[C6Cost]", 0) != 0 &&
                           line.rfind("[Tiles]", 0) != 0) {
                    post_status(utf8_to_wide(line));
                }
            }
        }
        CloseHandle(hR);

        WaitForSingleObject(pi.hProcess, INFINITE);
        DWORD ec = 1;
        GetExitCodeProcess(pi.hProcess, &ec);
        heraw_i18n::diag_log().end_task(task_id, (int)ec, g_app.cancel_job.load() || ec == 130);
        {
            std::lock_guard<std::mutex> lk(g_app.proc_mtx);
            g_app.hChildProc = nullptr;
            g_app.last_cli_output = std::move(full_out);
        }
        CloseHandle(pi.hThread);
        CloseHandle(pi.hProcess);
        PostMessageW(g_app.hwnd, WM_APP_DONE, (WPARAM)ec, 0);
    });
}

static std::wstring ensure_output_dir() {
    std::wstring out_dir = trim_ws(get_wnd_text(g_app.hOutDir));
    if (out_dir.empty() && !g_app.inputs.empty()) {
        out_dir = parent_dir_of(g_app.inputs[0]) + L"\\nijinLBS_out";
        g_app.auto_out_dir = out_dir;
        g_app.suppress_sync = true;
        SetWindowTextW(g_app.hOutDir, out_dir.c_str());
        g_app.suppress_sync = false;
    }
    return out_dir;
}

static void refresh_group_from_fields_json_win(const std::wstring& out_dir, int grp_idx, const FILETIME& since_ft) {
    if (out_dir.empty() || grp_idx < 0 || (size_t)grp_idx >= g_app.groups.size()) return;
    WIN32_FIND_DATAW ffd{};
    HANDLE hFind = FindFirstFileW((out_dir + L"\\*_fields.json").c_str(), &ffd);
    if (hFind == INVALID_HANDLE_VALUE) return;
    std::wstring best_path;
    FILETIME best_ft{};
    bool has_best = false;
    ULARGE_INTEGER since_u{};
    since_u.LowPart = since_ft.dwLowDateTime;
    since_u.HighPart = since_ft.dwHighDateTime;
    if (since_u.QuadPart > 20000000ULL) since_u.QuadPart -= 20000000ULL;
    do {
        if (ffd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
        ULARGE_INTEGER mod_u{};
        mod_u.LowPart = ffd.ftLastWriteTime.dwLowDateTime;
        mod_u.HighPart = ffd.ftLastWriteTime.dwHighDateTime;
        if (mod_u.QuadPart < since_u.QuadPart) continue;
        if (!has_best || CompareFileTime(&ffd.ftLastWriteTime, &best_ft) > 0) {
            best_ft = ffd.ftLastWriteTime;
            best_path = out_dir + L"\\" + ffd.cFileName;
            has_best = true;
        }
    } while (FindNextFileW(hFind, &ffd));
    FindClose(hFind);
    if (!has_best) return;
    std::string js = read_whole_file_utf8(best_path);
    if (js.empty()) return;
    auto& grp = g_app.groups[(size_t)grp_idx];
    if (has_json_key(js, "parallax_2x_px")) {
        double p2x = parse_json_double(js, "parallax_2x_px", NAN);
        if (std::isfinite(p2x)) {
            grp.max_parallax_2x_px = p2x;
            grp.parallax_measured = true;
        }
    }
    for (const auto& fobj : json_objects(js, "fields")) {
        std::string nm = extract_json_str(fobj, "name");
        if (!nm.empty() && has_json_key(fobj, "observable")) {
            bool obs = parse_json_bool(fobj, "observable", false);
            grp.field_observability[nm] = obs;
            if (nm == "depth") {
                grp.depth_known = true;
                grp.depth_observable = obs;
            }
        }
    }
    sync_cross_tab_ui();
    if (g_app.hParallaxMeter) InvalidateRect(g_app.hParallaxMeter, nullptr, FALSE);
}

static void append_perf_cli_args(std::vector<std::wstring>& args) {
    int bsel = g_app.hCmbBackend ? (int)SendMessageW(g_app.hCmbBackend, CB_GETCURSEL, 0, 0) : 0;
    if (bsel >= 0 && bsel < 3) {
        args.push_back(L"--backend");
        args.push_back(kBackendCodes[bsel]);
    }
    int core_sel = g_app.hCmbCpuCores ? (int)SendMessageW(g_app.hCmbCpuCores, CB_GETCURSEL, 0, 0) : 0;
    if (core_sel >= 0 && core_sel < 5) {
        args.push_back(L"--cpu-cores");
        args.push_back(kCpuCoresCodes[core_sel]);
    }
    args.push_back(L"--battery-device");
    args.push_back(g_app.is_battery_device ? L"on" : L"off");
    int prio_sel = g_app.hCmbPriority ? (int)SendMessageW(g_app.hCmbPriority, CB_GETCURSEL, 0, 0) : 0;
    if (prio_sel >= 0 && prio_sel < 4) {
        args.push_back(L"--priority");
        args.push_back(kPriorityCodes[prio_sel]);
    }
    if (g_app.hEdtThreads) {
        int thr = _wtoi(get_wnd_text(g_app.hEdtThreads).c_str());
        if (thr > 0) {
            args.push_back(L"--threads");
            args.push_back(std::to_wstring(thr));
        }
    }
}

static void append_v3_win_args(
    std::vector<std::wstring>& dst_args,
    const std::wstring& odir,
    int grp_display_1based,
    bool is_multi,
    const BurstGroupItem* grp = nullptr
) {
    std::wstring mp = selected_cmb_code(g_app.hCmbMotionProfile, kMotionProfileCodes, L"auto");
    if (mp != L"auto") { dst_args.push_back(L"--motion-profile"); dst_args.push_back(mp); }
    if (selected_rgb_fmt_code() == L"exr") {
        dst_args.push_back(L"--exr-precision");
        dst_args.push_back(selected_cmb_code(g_app.hCmbExrPrec, kExrPrecCodes, L"half"));
        std::wstring ep = selected_cmb_code(g_app.hCmbExrParts, kExrPartsCodes, L"beauty,fg,bg,scatter,depth");
        if (ep != L"beauty,fg,bg,scatter,depth") {
            dst_args.push_back(L"--exr-parts");
            dst_args.push_back(ep);
        }
    }
    std::wstring r1t = selected_cmb_code(g_app.hCmbRgb1xTier, kRgb1xTierCodes, L"sinc");
    if (r1t != L"sinc") { dst_args.push_back(L"--rgb-1x-tier"); dst_args.push_back(r1t); }
    std::wstring hlm = selected_cmb_code(g_app.hCmbHighlightMode, kHighlightModeCodes, L"legacy");
    if (hlm != L"legacy") { dst_args.push_back(L"--highlight-mode"); dst_args.push_back(hlm); }
    std::wstring ccm = selected_cmb_code(g_app.hCmbCodecConstraint, kCodecConstraintCodes, L"off");
    if (ccm != L"off") { dst_args.push_back(L"--codec-constraint"); dst_args.push_back(ccm); }
    std::wstring smm = selected_cmb_code(g_app.hCmbSatModel, kSatModelCodes, L"hard");
    if (smm != L"hard") { dst_args.push_back(L"--sat-model"); dst_args.push_back(smm); }
    std::wstring stm = selected_cmb_code(g_app.hCmbStreaming, kStreamingCodes, L"auto");
    if (stm != L"auto") { dst_args.push_back(L"--streaming"); dst_args.push_back(stm); }
    std::wstring mem_g = trim_ws(get_wnd_text(g_app.hEdtMemBudget));
    if (stm != L"off" && !mem_g.empty() && _wtoi(mem_g.c_str()) > 0 && _wtoi(mem_g.c_str()) != 4) {
        dst_args.push_back(L"--mem-budget-gb"); dst_args.push_back(mem_g);
    }
    std::wstring shm = selected_cmb_code(g_app.hCmbShutterMode, kShutterModeCodes, L"anchor");
    if (shm != L"anchor") {
        dst_args.push_back(L"--shutter-mode"); dst_args.push_back(shm);
        std::wstring fk = trim_ws(get_wnd_text(g_app.hEdtFlowKappa));
        if (!fk.empty() && std::fabs(_wtof(fk.c_str())) > 1e-4) {
            dst_args.push_back(L"--flow-kappa"); dst_args.push_back(fk);
        }
        std::wstring idb = selected_cmb_code(g_app.hCmbIntraDeblur, kIntraDeblurCodes, L"auto");
        if (idb != L"auto") { dst_args.push_back(L"--intra-deblur"); dst_args.push_back(idb); }
    }
    std::wstring dm = selected_cmb_code(g_app.hCmbDiffusionMode, kDiffusionCodes, L"off");
    if (dm != L"off") {
        dst_args.push_back(L"--diffusion-mode"); dst_args.push_back(dm);
        std::wstring dstr = trim_ws(get_wnd_text(g_app.hEdtDiffStrength));
        if (!dstr.empty() && std::fabs(_wtof(dstr.c_str()) - 1.0) > 1e-3) {
            dst_args.push_back(L"--diffusion-strength"); dst_args.push_back(dstr);
        }
        if (dm == L"ultra-contrast" || dm == L"black-mist" || dm.rfind(L"uc+", 0) == 0) {
            dst_args.push_back(L"--uc-grade");  dst_args.push_back(selected_cmb_code(g_app.hCmbUcGrade, kUcGradeCodes, L"1"));
            dst_args.push_back(L"--uc-baffle"); dst_args.push_back(selected_cmb_code(g_app.hCmbUcBaffle, kUcBaffleCodes, L"standard"));
            dst_args.push_back(L"--uc-chroma"); dst_args.push_back(selected_cmb_code(g_app.hCmbUcChroma, kUcChromaCodes, L"neutral"));
        }
    }
    std::wstring aw = selected_cmb_code(g_app.hCmbApertureWeight, kApertureWeightCodes, L"voronoi");
    if (aw != L"voronoi") { dst_args.push_back(L"--aperture-weight"); dst_args.push_back(aw); }
    std::wstring mpol = selected_cmb_code(g_app.hCmbMetricPolicy, kMetricPolicyCodes, L"verified");
    if (mpol != L"verified") { dst_args.push_back(L"--metric-policy"); dst_args.push_back(mpol); }
    if (g_app.hCmbDepthGate) {
        std::wstring dg = selected_cmb_code(g_app.hCmbDepthGate, kDepthGateCodes, L"auto");
        if (dg != L"auto") { dst_args.push_back(L"--depth-gate"); dst_args.push_back(dg); }
    }
    if (g_app.hEdtDepthDisp) {
        std::wstring s = trim_ws(get_wnd_text(g_app.hEdtDepthDisp));
        if (!s.empty()) { dst_args.push_back(L"--depth-min-disp"); dst_args.push_back(s); }
    }
    if (g_app.hEdtDepthCoh) {
        std::wstring s = trim_ws(get_wnd_text(g_app.hEdtDepthCoh));
        if (!s.empty()) { dst_args.push_back(L"--depth-min-coh"); dst_args.push_back(s); }
    }
    if (g_app.hEdtDepthSup) {
        std::wstring s = trim_ws(get_wnd_text(g_app.hEdtDepthSup));
        if (!s.empty()) { dst_args.push_back(L"--depth-min-support"); dst_args.push_back(s); }
    }
    std::wstring obsp = build_observers_csv_win();
    if (count_checked_observers_win() == kNumObs) obsp = L"all";
    if (!obsp.empty()) {
        dst_args.push_back(L"--observers");
        dst_args.push_back(obsp);
    }
    if (g_app.hCmbObsGate) {
        std::wstring og = selected_cmb_code(g_app.hCmbObsGate, kObsGateCodes, L"auto");
        if (og != L"auto") { dst_args.push_back(L"--observer-gate"); dst_args.push_back(og); }
    }
    if (g_app.hEdtReadoutMs) {
        std::wstring rms = trim_ws(get_wnd_text(g_app.hEdtReadoutMs));
        if (!rms.empty() && _wtof(rms.c_str()) > 0.0) {
            dst_args.push_back(L"--readout-ms"); dst_args.push_back(rms);
        }
    }
    if (g_app.hEdtObsPitch) {
        std::wstring ps = trim_ws(get_wnd_text(g_app.hEdtObsPitch));
        if (!ps.empty() && std::fabs(_wtof(ps.c_str())) > 1e-4) {
            dst_args.push_back(L"--obs-pitch-deg"); dst_args.push_back(ps);
        }
    }
    if (g_app.hEdtPlateScale) {
        std::wstring psc = trim_ws(get_wnd_text(g_app.hEdtPlateScale));
        if (!psc.empty() && _wtof(psc.c_str()) > 1e-4) {
            dst_args.push_back(L"--plate-scale"); dst_args.push_back(psc);
        }
    }
    if (g_app.hChkEmitDiffuse && get_chk(g_app.hChkEmitDiffuse)) {
        dst_args.push_back(L"--emit-diffuse");
    }
    if (g_app.hChkEmitLightSplit && get_chk(g_app.hChkEmitLightSplit)) {
        dst_args.push_back(L"--emit-light-split");
    }
    if (g_app.hChkEvalSelf && get_chk(g_app.hChkEvalSelf)) {
        wchar_t ebuf[64] = {0};
        if (is_multi) std::swprintf(ebuf, 64, L"\\eval_self_g%02d.json", grp_display_1based);
        else std::swprintf(ebuf, 64, L"\\eval_self.json");
        dst_args.push_back(L"--eval-self");
        dst_args.push_back(odir + ebuf);
    }
    if (g_app.hChkWriteObsReport && get_chk(g_app.hChkWriteObsReport)) {
        wchar_t obuf[64] = {0};
        if (is_multi) std::swprintf(obuf, 64, L"\\observer_report_g%02d.json", grp_display_1based);
        else std::swprintf(obuf, 64, L"\\observer_report.json");
        dst_args.push_back(L"--observer-report");
        dst_args.push_back(odir + obuf);
    }
    if (g_app.hChkNextShotPlan && get_chk(g_app.hChkNextShotPlan)) {
        wchar_t pbuf[64] = {0};
        if (is_multi) std::swprintf(pbuf, 64, L"\\next_shot_plan_g%02d.json", grp_display_1based);
        else std::swprintf(pbuf, 64, L"\\next_shot_plan.json");
        dst_args.push_back(L"--next-shot-plan");
        dst_args.push_back(odir + pbuf);
    }
    std::wstring sca_m = selected_cmb_code(g_app.hCmbSpectralCa, kSpectralCaCodes, L"off");
    if (sca_m != L"off") {
        dst_args.push_back(L"--spectral-ca"); dst_args.push_back(sca_m);
        std::wstring spwr = trim_ws(get_wnd_text(g_app.hEdtSpectralPwr));
        if (!spwr.empty() && std::fabs(_wtof(spwr.c_str())) > 1e-4) {
            dst_args.push_back(L"--spectral-ca-power"); dst_args.push_back(spwr);
        }
        if (sca_m == L"lateral") {
            std::wstring sang = selected_cmb_code(g_app.hCmbSpectralAng, kSpectralAngleCodes, L"radial");
            if (sang == L"custom") {
                std::wstring cdeg = g_app.hEdtSpectralDeg ? trim_ws(get_wnd_text(g_app.hEdtSpectralDeg)) : L"0.0";
                dst_args.push_back(L"--spectral-ca-angle");
                dst_args.push_back(cdeg.empty() ? L"0.0" : cdeg);
            } else if (sang != L"radial") {
                dst_args.push_back(L"--spectral-ca-angle"); dst_args.push_back(sang);
            }
        }
    }
    std::wstring b_dn = trim_ws(get_wnd_text(g_app.hEdtBokehDeNisen));
    if (!b_dn.empty() && _wtof(b_dn.c_str()) > 1e-4) {
        dst_args.push_back(L"--bokeh-de-nisen"); dst_args.push_back(b_dn);
    }
    std::wstring b_sw = trim_ws(get_wnd_text(g_app.hEdtBokehSwirl));
    if (!b_sw.empty() && std::fabs(_wtof(b_sw.c_str())) > 1e-4) {
        dst_args.push_back(L"--bokeh-swirl"); dst_args.push_back(b_sw);
    }
    if (g_app.hEdtBokehAspect) {
        std::wstring b_asp = trim_ws(get_wnd_text(g_app.hEdtBokehAspect));
        if (!b_asp.empty() && std::fabs(_wtof(b_asp.c_str()) - 1.0) > 5e-3) {
            dst_args.push_back(L"--bokeh-aspect"); dst_args.push_back(b_asp);
        }
    }
    std::wstring b_on = trim_ws(get_wnd_text(g_app.hEdtBokehOnion));
    if (!b_on.empty() && _wtof(b_on.c_str()) > 1e-4) {
        dst_args.push_back(L"--bokeh-onion-clean"); dst_args.push_back(b_on);
    }
    std::wstring bus_in = active_bus_in_for_group_win(grp ? grp : current_group_ptr());
    if (!bus_in.empty()) {
        dst_args.push_back(L"--bus-in"); dst_args.push_back(bus_in);
    }
    if (g_app.hChkBusExport && get_chk(g_app.hChkBusExport)) {
        wchar_t bbuf[64] = {0};
        if (is_multi) std::swprintf(bbuf, 64, L"\\session_bus_g%02d.nib", grp_display_1based);
        else std::swprintf(bbuf, 64, L"\\session_bus.nib");
        dst_args.push_back(L"--bus-export");
        dst_args.push_back(odir + bbuf);
    }
}

static std::vector<std::wstring> build_group_job_args_win(
    const std::wstring& exe,
    const BurstGroupItem& group,
    const std::wstring& out_dir,
    int grp_1based,
    bool is_multi
) {
    bool want_raw   = get_chk(g_app.hChkRaw);
    bool want_rgb   = get_chk(g_app.hChkRgb);
    bool want_fld   = get_chk(g_app.hChkField);
    bool want_trans = get_chk(g_app.hChkTransientTab2);
    bool want_par   = get_chk(g_app.hChkExportParallax);
    bool want_ref   = get_chk(g_app.hChkExportRefocus);
    bool want_tiles = get_chk(g_app.hChkExportTiles);

    std::vector<std::wstring> job;
    job.push_back(exe);
    if (!g_app.libraw_path.empty() && file_exists_regular(g_app.libraw_path)) {
        job.push_back(L"--libraw");
        job.push_back(g_app.libraw_path);
    }
    append_intake_cli_args_win(job, false);
    job.push_back(L"--no-sort-time");
    if (get_chk(g_app.hChkRecursive)) job.push_back(L"--recursive");
    job.push_back(L"--policy");
    job.push_back(L"force-all");
    job.push_back(L"--single-group");

    job.push_back(L"--preset");
    job.push_back(kPresetCodes[std::max(0, std::min(3, g_app.preset_idx))]);

    int olpf_sel = (int)SendMessageW(g_app.hCmbOlpf, CB_GETCURSEL, 0, 0);
    if (olpf_sel >= 0 && olpf_sel < 4) {
        job.push_back(L"--olpf-mode");
        job.push_back(kOlpfCodes[olpf_sel]);
    }

    std::wstring anchor = g_app.motion_mode_idx == 1 ? L"first" : (g_app.motion_mode_idx == 2 ? L"middle" : L"auto");
    if (group.locked_anchor_1based > 0 && g_app.motion_mode_idx <= 2) {
        anchor = std::to_wstring(group.locked_anchor_1based);
    }
    job.push_back(L"--anchor-frame");
    job.push_back(anchor);

    std::wstring skip_csv = build_skip_frames_csv(&group);
    if (!skip_csv.empty()) {
        job.push_back(L"--skip-frames");
        job.push_back(skip_csv);
    }

    auto add_edt_opt = [&](const wchar_t* flag, HWND hEdt) {
        if (!hEdt) return;
        std::wstring v = trim_ws(get_wnd_text(hEdt));
        if (!v.empty()) { job.push_back(flag); job.push_back(v); }
    };

    if (g_app.motion_mode_idx == 3) {
        job.push_back(L"--motion-mode");
        job.push_back(L"clean-plate");
        add_edt_opt(L"--clean-sens", g_app.hEdtCleanSens);
    } else if (g_app.motion_mode_idx == 4) {
        job.push_back(L"--motion-mode");
        job.push_back(L"virtual-nd");
        double vnd_v = std::clamp(_wtof(trim_ws(get_wnd_text(g_app.hEdtVndThresh)).c_str()), 0.10, 1.00);
        wchar_t vbuf[32] = {0};
        std::swprintf(vbuf, 32, L"%.2f", vnd_v);
        job.push_back(L"--vnd-thresh");
        job.push_back(vbuf);
    } else if (g_app.motion_mode_idx == 5) {
        job.push_back(L"--motion-mode");
        job.push_back(L"strobe");
        add_edt_opt(L"--strobe-step", g_app.hEdtStrobeStep);
        job.push_back(L"--strobe-fade");
        job.push_back(seg_pill_get_sel(g_app.hSegStrobeFade) == 1 ? L"1" : L"0");
    } else if (g_app.motion_mode_idx == 6) {
        job.push_back(L"--motion-mode");
        job.push_back(L"focus-stack");
    }

    append_perf_cli_args(job);

    add_edt_opt(L"--denoise-strength", g_app.hEdtDenoise);
    add_edt_opt(L"--chroma-denoise",   g_app.hEdtChroma);
    add_edt_opt(L"--psf-iters",        g_app.hEdtPsfIters);
    add_edt_opt(L"--rgb-psf-iters",    g_app.hEdtRgbPsfIters);
    add_edt_opt(L"--psf-sigma",        g_app.hEdtPsfSigma);
    add_edt_opt(L"--deghost-strength", g_app.hEdtDeghost);
    add_edt_opt(L"--ev-offset",        g_app.hEdtEv);
    add_edt_opt(L"--group-size",       g_app.hEdtGrpSz);

    job.push_back(L"--silicon-clean");
    job.push_back(get_chk(g_app.hChkSiliconClean) ? L"on" : L"off");
    job.push_back(L"--ibp-iters");
    job.push_back(std::to_wstring(get_ibp_iters_win()));
    if (g_app.hChkEdgePolish) {
        job.push_back(L"--edge-polish");
        job.push_back(get_chk(g_app.hChkEdgePolish) ? L"on" : L"off");
    }
    job.push_back(L"--auto-lca");
    job.push_back(get_chk(g_app.hChkAutoLca) ? L"on" : L"off");
    job.push_back(L"--auto-exposure");
    job.push_back(get_chk(g_app.hChkAutoExp) ? L"on" : L"off");
    job.push_back(L"--highlight-rolloff");
    job.push_back(get_chk(g_app.hChkRolloff) ? L"on" : L"off");
    job.push_back(L"--dither");
    job.push_back(get_chk(g_app.hChkDither) ? L"on" : L"off");

    int rsel = (int)SendMessageW(g_app.hCmbRawFmt, CB_GETCURSEL, 0, 0);
    if (rsel >= 0 && rsel < 5) {
        job.push_back(L"--raw-format");
        job.push_back(kRawFmtCodes[rsel]);
    }
    job.push_back(L"--relay-raw");
    job.push_back((want_raw && get_chk(g_app.hChkRelayRaw)) ? L"on" : L"off");
    job.push_back(L"--no-tool-exif");
    job.push_back(get_chk(g_app.hChkNoToolExif) ? L"on" : L"off");

    std::wstring rgb_code = selected_rgb_fmt_code();
    job.push_back(L"--rgb-format");
    job.push_back(rgb_code);
    if (want_rgb) {
        job.push_back(L"--rgb-scale");
        job.push_back(selected_rgb_scale_code());
    }
    if (rgb_code == L"jxs") add_edt_opt(L"--jxs-bpp", g_app.hEdtJxsBpp);
    job.push_back(L"--color-space");
    job.push_back(get_chk(g_app.hRadCsAdobe) ? L"adobergb" : L"srgb");

    if (want_fld) {
        std::wstring eff_fields = build_effective_fields_csv(&group);
        if (!eff_fields.empty()) {
            int fmsel = (int)SendMessageW(g_app.hCmbFieldMode, CB_GETCURSEL, 0, 0);
            if (fmsel >= 0 && fmsel < kNumFieldModes) {
                job.push_back(L"--field-mode");
                job.push_back(kFieldModeCodes[fmsel]);
            }
            job.push_back(L"--fields");
            job.push_back(eff_fields);
            job.push_back(L"--emit-field");
        }
    }

    if (want_trans) {
        job.push_back(L"--emit-transient");
        add_edt_opt(L"--transient-sigma", g_app.hEdtTransSigma);
        job.push_back(L"--transient-color");
        job.push_back(seg_pill_get_sel(g_app.hSegTransColor) == 1 ? L"gray" : L"rgb");
    }

    if (get_chk(g_app.hChkReflOn)) {
        job.push_back(L"--reflection-mode");
        job.push_back(seg_pill_get_sel(g_app.hSegReflMode) == 1 ? L"split-layers" : L"glint-suppress");
    }

    bool depth_blocked = group.depth_known && !group.depth_observable && !is_depth_gate_overridden_win();
    if (want_par && !depth_blocked) {
        job.push_back(L"--export-parallax-html");
        add_edt_opt(L"--parallax-amp", g_app.hEdtParallaxAmp);
    }
    if (want_ref && !depth_blocked) {
        auto refocus = refocus_arguments();
        job.insert(job.end(), refocus.begin(), refocus.end());
    }
    if (want_tiles) {
        wchar_t gdir[64] = {0};
        if (is_multi) std::swprintf(gdir, 64, L"\\dataset_tiles\\group_%02d", grp_1based);
        else std::swprintf(gdir, 64, L"\\dataset_tiles");
        job.push_back(L"--export-dataset-tiles");
        job.push_back(out_dir + gdir);
        job.push_back(L"--tile-size");
        job.push_back(seg_pill_get_sel(g_app.hSegTileSize) == 1 ? L"512" : L"256");
        add_edt_opt(L"--tile-min-conf", g_app.hEdtTileConf);
    }

    job.push_back(L"--progress-json");
    job.push_back(L"--out-dir");
    job.push_back(out_dir);

    if (want_raw) job.push_back(L"--emit-raw");
    if (want_rgb) job.push_back(L"--emit-rgb");

    append_v3_win_args(job, out_dir, grp_1based, is_multi, &group);
    job.insert(job.end(), group.paths.begin(), group.paths.end());
    return job;
}

static void start_synthesis_job() {
    if (g_app.running.load()) return;
    if (g_app.grouping_dirty || g_app.plan_scanning.load()) {
        set_status_ui(heraw_i18n::tr_w("st.regroup_first"));
        return;
    }
    save_ui_prefs();

    std::vector<std::wstring> target_inputs = collect_active_input_paths();
    if (target_inputs.empty()) {
        set_status_ui(heraw_i18n::tr_w(g_app.inputs.empty() ? "st.alert_no_input" : "st.alert_no_checked"));
        return;
    }

    bool want_raw      = get_chk(g_app.hChkRaw);
    bool want_rgb      = get_chk(g_app.hChkRgb);
    bool want_fld      = get_chk(g_app.hChkField);
    bool want_trans    = get_chk(g_app.hChkTransientTab2);
    bool want_par      = get_chk(g_app.hChkExportParallax);
    bool want_ref      = get_chk(g_app.hChkExportRefocus);
    bool want_tiles    = get_chk(g_app.hChkExportTiles);
    bool want_bus      = g_app.hChkBusExport && get_chk(g_app.hChkBusExport);
    bool want_obs_rep  = g_app.hChkWriteObsReport && get_chk(g_app.hChkWriteObsReport);
    bool want_next_pln = g_app.hChkNextShotPlan && get_chk(g_app.hChkNextShotPlan);
    bool want_diff     = g_app.hChkEmitDiffuse && get_chk(g_app.hChkEmitDiffuse);
    bool want_lsplit   = g_app.hChkEmitLightSplit && get_chk(g_app.hChkEmitLightSplit);
    bool want_eval     = g_app.hChkEvalSelf && get_chk(g_app.hChkEvalSelf);
    bool want_obs      = count_checked_observers_win() > 0;
    if (want_fld && count_checked_fields() == 0) {
        set_status_ui(heraw_i18n::tr_w("st.pick_field"));
        return;
    }

    if (!want_raw && !want_rgb && !want_fld && !want_trans && !want_par &&
        !want_ref && !want_tiles && !want_bus && !want_obs_rep && !want_next_pln &&
        !want_diff && !want_lsplit && !want_eval && !want_obs) {
        set_status_ui(heraw_i18n::tr_w("st.alert_no_output"));
        return;
    }

    std::wstring exe = resolve_active_cli_or_prompt(g_app.hwnd);
    if (exe.empty()) return;

    std::wstring out_dir = ensure_output_dir();
    g_app.pending_jobs.clear();
    g_app.pending_group_indices.clear();
    int checked_group_total = 0;
    for (const auto& group : g_app.groups) {
        if (group.checked && !group.paths.empty()) ++checked_group_total;
    }
    for (size_t gi = 0; gi < g_app.groups.size(); ++gi) {
        const auto& group = g_app.groups[gi];
        if (!group.checked || group.paths.empty()) continue;
        std::vector<std::wstring> job = build_group_job_args_win(exe, group, out_dir, (int)gi + 1, checked_group_total > 1);
        bool has_output = false;
        for (const auto* flag : {L"--emit-raw", L"--emit-rgb", L"--emit-field", L"--emit-transient",
                                 L"--export-parallax-html", L"--export-refocus", L"--export-dataset-tiles",
                                 L"--bus-export", L"--observer-report", L"--next-shot-plan",
                                 L"--emit-diffuse", L"--emit-light-split", L"--eval-self", L"--observers"}) {
            has_output |= std::find(job.begin(), job.end(), flag) != job.end();
        }
        if (!has_output) continue;
        g_app.pending_jobs.push_back(std::move(job));
        g_app.pending_group_indices.push_back((int)gi);
    }
    if (g_app.pending_jobs.empty()) {
        set_status_ui(heraw_i18n::tr_w("st.gate_none_runnable"));
        return;
    }
    g_app.total_synthesis_jobs = (int)g_app.pending_jobs.size();
    g_app.completed_synthesis_jobs = 0;
    g_app.skipped_synthesis_jobs = 0;
    g_app.active_out_dir = out_dir;
    g_app.lcp_tmp_prefix.clear();
    g_app.lcp_export_dst.clear();
    GetSystemTimeAsFileTime(&g_app.job_start_ft);
    auto first_job = std::move(g_app.pending_jobs.front());
    g_app.pending_jobs.erase(g_app.pending_jobs.begin());
    g_app.active_job_group_idx = g_app.pending_group_indices.front();
    g_app.pending_group_indices.erase(g_app.pending_group_indices.begin());
    launch_async_cli_job(first_job, heraw_i18n::trf_w("st.group_starting_fmt", 1, g_app.total_synthesis_jobs), true, "Synthesis Group 1");
}

static void start_topk_export_job() {
    if (g_app.running.load()) return;
    std::vector<std::wstring> target_inputs = collect_active_input_paths();
    if (target_inputs.empty()) {
        set_status_ui(heraw_i18n::tr_w("st.alert_no_input"));
        return;
    }
    std::wstring exe = resolve_active_cli_or_prompt(g_app.hwnd);
    if (exe.empty()) return;

    std::wstring out_dir = ensure_output_dir();
    std::vector<std::wstring> args;
    args.push_back(exe);
    if (!g_app.libraw_path.empty() && file_exists_regular(g_app.libraw_path)) {
        args.push_back(L"--libraw");
        args.push_back(g_app.libraw_path);
    }
    append_perf_cli_args(args);
    args.push_back(get_chk(g_app.hChkSortTime) ? L"--sort-time" : L"--no-sort-time");
    if (get_chk(g_app.hChkRecursive)) args.push_back(L"--recursive");
    args.push_back(L"--export-topk");
    args.push_back(L"3");
    args.push_back(L"--out-dir");
    args.push_back(out_dir);
    for (const auto& p : target_inputs) args.push_back(p);

    g_app.total_synthesis_jobs = 0;
    g_app.active_job_group_idx = -1;
    g_app.active_out_dir = out_dir;
    launch_async_cli_job(args, heraw_i18n::tr_w("st.topk_running"), true, "Top-K Export");
}

static OpticsTelemetry parse_optics_telemetry_json(const std::string& js, const std::wstring& json_path) {
    OpticsTelemetry tel{};
    tel.raw_json = js;
    tel.json_file_path = json_path;
    tel.sigma0 = parse_json_double(js, "sigma0", parse_json_double(js, "read_noise_sigma0", -1));
    tel.gain_g = parse_json_double(js, "gain_g", parse_json_double(js, "photon_gain_g", -1));
    tel.temp_c = parse_json_double(js, "sensor_temp_c", parse_json_double(js, "temp_c", NAN));
    tel.warm_pixels = parse_json_int(js, "warm_pixels", parse_json_int(js, "hot_pixels", -1));
    tel.has_silicon = tel.sigma0 >= 0 && tel.gain_g >= 0;
    const std::wstring suffix = L"_telemetry.json";
    if (json_path.size() >= suffix.size() && json_path.compare(json_path.size() - suffix.size(), suffix.size(), suffix) == 0)
        tel.passport_path = json_path.substr(0, json_path.size() - suffix.size()) + L"_passport.png";
    if (extract_json_str(js, "schema") == "org.nijin.optics.v1") {
        tel.measured = true;
        tel.max_parallax_2x_px = parse_json_double(js, "max_parallax_2x_px", NAN);
        tel.parallax_measured = std::isfinite(tel.max_parallax_2x_px);
        tel.depth_known = has_json_key(js, "depth_observable");
        tel.depth_observable = parse_json_bool(js, "depth_observable", false);
        std::string observability = json_container(js, "field_observability");
        for (const auto& spec : kFieldSpecs) {
            std::string code = wide_to_utf8(spec.code);
            if (has_json_key(observability, code.c_str()))
                tel.field_observability[code] = parse_json_bool(observability, code.c_str(), false);
        }
        if (tel.field_observability.count("depth")) {
            tel.depth_known = true; tel.depth_observable = tel.field_observability["depth"];
        }
        for (int i = 0; i < 5; ++i) { tel.zone_rejected[i] = true; tel.zone_mtf50[i] = -1; tel.zone_status[i] = L"待测"; }
        const char* names[] = {"center", "top_left", "top_right", "bottom_left", "bottom_right"};
        int count = 0;
        bool stellar = false;
        for (const auto& zone : json_objects(js, "zones")) {
            std::string name = extract_json_str(zone, "name");
            int index = -1;
            for (int i = 0; i < 5; ++i) if (name == names[i]) index = i;
            if (index < 0) continue;
            tel.zone_mtf50[index] = json_metric(zone, "mtf50", -1);
            tel.zone_rejected[index] = tel.zone_mtf50[index] < 0;
            tel.zone_status[index] = utf8_to_wide(extract_json_str(zone, "status"));
            double major = json_metric(zone, "fwhm_major_px"), minor = json_metric(zone, "fwhm_minor_px");
            if (std::isfinite(major) && major > 0 && std::isfinite(minor) && minor >= 0)
                tel.zone_astig[index] = std::clamp(1.0 - minor / major, 0.0, 1.0);
            stellar |= extract_json_str(zone, "method").find("stellar") != std::string::npos;
        }
        for (bool rejected : tel.zone_rejected) if (!rejected) ++count;
        tel.grade_badge = heraw_i18n::trf_w(stellar ? "t4.badge_stellar_fmt" : "t4.badge_mtf_fmt", count);
        tel.max_drift_px = json_metric(js, "drift_span_px", -1);
        for (double n : json_numbers(js, "phase_quadrants")) tel.phase_quadrants.push_back((int)std::max(0.0, n));
        for (const auto& frame : json_objects(js, "drift")) {
            FrameDiagItem item;
            item.idx_1based = parse_json_int(frame, "frame", -1) + 1;
            item.dx = json_metric(frame, "dx_px");
            item.dy = json_metric(frame, "dy_px");
            item.included = item.idx_1based > 0 && std::isfinite(item.dx) && std::isfinite(item.dy);
            item.time_seconds = json_metric(frame, "time_seconds");
            item.has_time = std::isfinite(item.time_seconds);
            tel.drift.push_back(item);
        }
        double width = parse_json_double(js, "width", 0), height = parse_json_double(js, "height", 0);
        double extent = std::max(width, height);
        for (const auto& channel : json_objects(json_container(js, "lca"), "channels")) {
            auto c = json_numbers(channel, "coefficients");
            std::string name = extract_json_str(channel, "channel");
            int index = name == "red" ? 0 : name == "blue" ? 1 : -1;
            if (index < 0 || c.size() != 4 || extent <= 0) continue;
            tel.lca_valid[index] = true;
            for (int sx : {-1, 1}) for (int sy : {-1, 1}) {
                double x = sx * width * 0.5 / extent, y = sy * height * 0.5 / extent;
                double scale = c[2] + c[3] * (x*x + y*y);
                tel.lca_edge_px[index] = std::max(tel.lca_edge_px[index], std::hypot(c[0] + x*scale, c[1] + y*scale));
            }
        }
        tel.lca_rad_px = std::max(tel.lca_edge_px[0], tel.lca_edge_px[1]);
        std::string ois_str = heraw_i18n::tr_s(parse_json_bool(js, "ois", false) ? "t4.ois_on" : "t4.ois_off");
        tel.ois_note = heraw_i18n::trf_w("t4.ois_measured_fmt", ois_str.c_str(), count);
        return tel;
    }

    std::string grade = extract_json_str(js, "grade");
    if (grade.empty()) grade = extract_json_str(js, "metrology_grade");
    tel.is_star_gold = grade == "absolute_star_epsf";
    tel.grade_badge = grade.empty() ? heraw_i18n::tr_w("t4.badge_optics") : utf8_to_wide(grade);

    static const char* kFlatMtf[5] = {"center_mtf50", "nw_mtf50", "ne_mtf50", "sw_mtf50", "se_mtf50"};
    static const char* kFlatAst[5] = {"center_astig", "nw_astig", "ne_astig", "sw_astig", "se_astig"};
    static const char* kRejected[5] = {"center_rejected", "nw_rejected", "ne_rejected", "sw_rejected", "se_rejected"};
    for (int z = 0; z < 5; ++z) {
        tel.zone_mtf50[z] = parse_json_double(js, kFlatMtf[z], -1.0);
        tel.zone_astig[z] = parse_json_double(js, kFlatAst[z], 0.0);
        tel.zone_rejected[z] = parse_json_bool(js, kRejected[z], tel.zone_mtf50[z] < 0.0);
    }

    size_t zKey = js.find("\"zones\"");
    if (zKey != std::string::npos) {
        size_t zOpen = js.find('[', zKey);
        size_t zClose = find_matching_brace(js, zOpen, '[', ']');
        if (zClose != std::string::npos) {
            size_t zp = zOpen + 1;
            int zi = 0;
            while (zp < zClose && zi < 5) {
                size_t zo = js.find('{', zp);
                if (zo == std::string::npos || zo >= zClose) break;
                size_t zc = find_matching_brace(js, zo, '{', '}');
                if (zc == std::string::npos || zc > zClose) break;
                tel.zone_mtf50[zi]    = parse_json_double(js, "mtf50", tel.zone_mtf50[zi], zo, zc);
                tel.zone_astig[zi]    = parse_json_double(js, "astigmatism",
                                        parse_json_double(js, "astig", tel.zone_astig[zi], zo, zc), zo, zc);
                tel.zone_rejected[zi] = parse_json_bool(js, "rejected", false, zo, zc);
                ++zi;
                zp = zc + 1;
            }
        }
    }

    tel.measured = tel.zone_mtf50[0] >= 0.0;
    if (!tel.measured) tel.grade_badge = heraw_i18n::tr_w("t4.badge_pending");
    tel.lca_rad_px   = parse_json_double(js, "lca_px", parse_json_double(js, "lca_max_px", 0.0));
    tel.max_drift_px = parse_json_double(js, "max_drift_2x_px",
        2.0 * parse_json_double(js, "max_drift_px", 0.0)) * 0.5;

    std::string ois = extract_json_str(js, "ois_status");
    tel.ois_note = ois.empty() ? heraw_i18n::trf_w("st.optics_done_fmt", wide_to_utf8(tel.grade_badge).c_str())
                               : utf8_to_wide(ois);
    return tel;
}

static void start_optics_probe_job() {
    if (g_app.running.load()) return;
    BurstGroupItem* grp = current_group_ptr();
    std::vector<std::wstring> files = (grp && !grp->paths.empty()) ? grp->paths : g_app.inputs;
    if (files.empty()) {
        set_status_ui(heraw_i18n::tr_w("st.alert_select_group"));
        return;
    }
    std::wstring exe = resolve_active_cli_or_prompt(g_app.hwnd);
    if (exe.empty()) return;

    wchar_t tmpDir[MAX_PATH] = {0};
    GetTempPathW(MAX_PATH - 1, tmpDir);
    std::wstring rep_prefix = std::wstring(tmpDir) + L"nijinLBS_optics_" +
        std::to_wstring(GetCurrentProcessId()) + L"_" + std::to_wstring(GetTickCount64());
    std::wstring rep_json   = rep_prefix + L"_telemetry.json";

    std::vector<std::wstring> args;
    args.push_back(exe);
    if (!g_app.libraw_path.empty() && file_exists_regular(g_app.libraw_path)) {
        args.push_back(L"--libraw");
        args.push_back(g_app.libraw_path);
    }
    append_perf_cli_args(args);
    args.push_back(L"--no-sort-time");
    args.push_back(L"--optics-only");
    args.push_back(L"--optics-report");
    args.push_back(rep_prefix);
    std::wstring skip_csv = build_skip_frames_csv(grp);
    if (!skip_csv.empty()) {
        args.push_back(L"--skip-frames");
        args.push_back(skip_csv);
    }
    for (const auto& f : files) args.push_back(f);

    EnableWindow(g_app.hBtnRunOptics, FALSE);
    SetWindowTextW(g_app.hLblOpticsStatus, heraw_i18n::tr_w("st.optics_running").c_str());
    set_status_ui(heraw_i18n::tr_w("st.optics_running"));

    g_app.cancel_job = false;
    set_running_ui(true);
    if (g_app.optics_worker.joinable()) g_app.optics_worker.join();
    int group_index = g_app.selected_group;
    uint64_t generation = g_app.plan_gen;
    g_app.optics_worker = std::thread([args, rep_json, rep_prefix, group_index, generation]() {
        DWORD ec = 1;
        std::string stdout_s = run_cli_capture_stdout(args, &ec, &g_app.hOpticsProc, 0, "8x Optics Probe");
        {
            std::lock_guard<std::mutex> lk(g_app.proc_mtx);
            g_app.last_cli_output = stdout_s;
        }
        std::wstring chosen_path = rep_json;
        std::string js = read_whole_file_utf8(rep_json);
        if (js.empty()) {
            std::wstring alt = rep_prefix + L".json";
            js = read_whole_file_utf8(alt);
            if (!js.empty()) chosen_path = alt;
        }
        if (js.empty() && stdout_s.find('{') != std::string::npos) {
            js = stdout_s;
        }
        auto* tel = new OpticsTelemetry(ec == 0 ? parse_optics_telemetry_json(js, chosen_path) : OpticsTelemetry{});
        DeleteFileW(rep_json.c_str());
        DeleteFileW((rep_prefix + L".json").c_str());
        if (g_app.stopping) {
            DeleteFileW((rep_prefix + L"_passport.png").c_str());
            delete tel;
            return;
        }
        if (generation != g_app.plan_gen || g_app.cancel_job) *tel = OpticsTelemetry{};
        if (!PostMessageW(g_app.hwnd, WM_APP_OPTICS_READY, (WPARAM)group_index, reinterpret_cast<LPARAM>(tel))) {
            DeleteFileW((rep_prefix + L"_passport.png").c_str());
            delete tel;
        }
    });
}

// ============================================================================
// Studio Canvas Background Painter (Header, Cards, Dividers, Input Wells, Action Bar)
// ============================================================================
static void paint_studio_canvas(HWND hwnd, HDC hdc) {
    RECT crc{};
    GetClientRect(hwnd, &crc);
    FillRect(hdc, &crc, g_app.hBrCanvas);

    auto S = [](int px) { return g_app.S(px); };
    int cw = crc.right;
    int ch = crc.bottom;

    int bottomBarY = ch - S(68);
    int workBot    = bottomBarY - S(12);
    int rwDock     = S(336);
    int rxDock     = cw - S(18) - rwDock;
    int hdrRightX  = std::max(S(640), rxDock - S(14));
    int cxStage    = S(428);
    int cwStage    = std::max(S(556), rxDock - S(14) - cxStage);
    int stageTop   = S(112);
    int stageH     = std::max(S(580), workBot - stageTop);
    auto stage_x   = [&](int x) { return cxStage + MulDiv(S(x - 428), cwStage, S(744)); };
    auto stage_y   = [&](int y) { return stageTop + MulDiv(S(y - 112), stageH, S(728)); };
    bool is_exr_fmt = (selected_rgb_fmt_code() == L"exr");
    int recTop     = is_exr_fmt ? S(612) : S(584);

    // 1. Header Bar spans Columns 1 + 2 ONLY (0 .. hdrRightX), so Column 3 owns the full right strip
    RECT hdr{0, 0, hdrRightX, S(56)};
    FillRect(hdc, &hdr, g_app.hBrCard);

    HBRUSH brAccent = CreateSolidBrush(kColAccent);
    RECT topStripe{0, 0, hdrRightX, S(3)};
    FillRect(hdc, &topStripe, brAccent);
    DeleteObject(brAccent);

    HBRUSH brBorder = CreateSolidBrush(kColCardBorder);
    RECT hdrLine{0, S(55), hdrRightX, S(56)};
    FillRect(hdc, &hdrLine, brBorder);
    RECT hdrRightEdge{hdrRightX - 1, 0, hdrRightX, S(56)};
    FillRect(hdc, &hdrRightEdge, brBorder);
    DeleteObject(brBorder);

    // Baseline-aligned header typography: "NijiN.LiteBurstSuite" + prominent "3" + [nijinLBS] + "v3.0.0"
    SetBkMode(hdc, TRANSPARENT);
    HGDIOBJ oldFont = SelectObject(hdc, g_app.hFontTitle);
    TEXTMETRICW tmTitle{};
    GetTextMetricsW(hdc, &tmTitle);
    int capH = std::max(S(10), (int)(tmTitle.tmAscent - tmTitle.tmInternalLeading));
    int baselineY = S(3) + (S(53) + capH) / 2;
    int capCenterY = baselineY - capH / 2;

    SIZE szTitle{}, szMaj{};
    GetTextExtentPoint32W(hdc, L"NijiN.LiteBurstSuite", 20, &szTitle);
    SelectObject(hdc, g_app.hFontMajorVer ? g_app.hFontMajorVer : g_app.hFontTitle);
    GetTextExtentPoint32W(hdc, L"3", 1, &szMaj);

    int titleX = S(20);
    int majX   = titleX + szTitle.cx + S(6);
    int pillX  = majX + szMaj.cx + S(10);
    int pillW  = S(68);
    int pillH  = S(20);
    int pillY  = capCenterY - pillH / 2;
    int verX   = pillX + pillW + S(8);

    UINT prevAlign = SetTextAlign(hdc, TA_LEFT | TA_BASELINE);
    SelectObject(hdc, g_app.hFontTitle);
    SetTextColor(hdc, kColTextMain);
    TextOutW(hdc, titleX, baselineY, L"NijiN.LiteBurstSuite", 20);

    SelectObject(hdc, g_app.hFontMajorVer ? g_app.hFontMajorVer : g_app.hFontTitle);
    SetTextColor(hdc, kColAccent);
    TextOutW(hdc, majX, baselineY, L"3", 1);

    const std::wstring verTag = L"v" NIJIN_VERSION;
    SelectObject(hdc, g_app.hFontVerTag ? g_app.hFontVerTag : g_app.hFont);
    SetTextColor(hdc, kColTextMuted);
    TextOutW(hdc, verX, baselineY, verTag.c_str(), (int)verTag.size());
    SetTextAlign(hdc, prevAlign);

    {
        Gdiplus::Graphics g(hdc);
        g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
        g.SetInterpolationMode(Gdiplus::InterpolationModeHighQualityBicubic);
        g.SetPixelOffsetMode(Gdiplus::PixelOffsetModeHighQuality);

        // Header lake-blue "nijinLBS" pill centered on cap-height center
        gdip_round_rect(g, (float)pillX, (float)pillY, (float)pillW, (float)pillH,
                        (float)S(6), gdip_col(kColBadgeFill), gdip_col(kColBadgeEdge), 1.0f);

        auto card = [&](int x, int y, int w, int h) {
            gdip_round_rect(g, (float)x, (float)y, (float)w, (float)h,
                            (float)S(11), gdip_col(kColCard), gdip_col(kColCardBorder), 1.15f);
        };
        Gdiplus::Pen divPen(gdip_col(kColSubBorder), 1.0f);
        auto hdiv = [&](int x1, int x2, int y) {
            g.DrawLine(&divPen, (float)x1, (float)y, (float)x2, (float)y);
        };

        // Column 1: Persistent Left Master-Detail Dock (x = 18..414, w = 396)
        card(S(18), S(66),  S(396), S(390));
        card(S(18), S(466), S(396), std::max(S(180), workBot - S(466)));

        // Column 3: Full-Height Persistent Right Dock (y = 12..workBot, R1 / R2 / R3, w = 336)
        card(rxDock, S(12),  rwDock, S(138));
        hdiv(rxDock + S(14), rxDock + rwDock - S(14), S(112));
        card(rxDock, S(160), rwDock, S(150));
        card(rxDock, S(320), rwDock, std::max(S(260), workBot - S(320)));
        hdiv(rxDock + S(14), rxDock + rwDock - S(14), S(416));
        hdiv(rxDock + S(14), rxDock + rwDock - S(14), S(482));
        gdip_round_rect(g, (float)(rxDock + S(14)), (float)recTop, (float)S(308),
                        (float)std::max(S(56), workBot - S(8) - recTop),
                        (float)S(8), gdip_col(kColCardSub), gdip_col(kColSubBorder), 1.0f);

        // Column 2: Elastic Center 3-Workspace Stage (x = 428 .. rxDock - 14)
        if (g_app.active_tab == 0) {
            card(cxStage, stage_y(112), cwStage, stage_y(218) - stage_y(112));
            card(cxStage, stage_y(226), cwStage, stage_y(548) - stage_y(226));
            card(cxStage, stage_y(556), cwStage, workBot - stage_y(556));
            hdiv(stage_x(444), stage_x(1156), stage_y(442));
            hdiv(stage_x(444), stage_x(1156), stage_y(774));
        } else if (g_app.active_tab == 1) {
            card(cxStage, stage_y(112), cwStage, stage_y(224) - stage_y(112));
            card(cxStage, stage_y(232), cwStage, stage_y(442) - stage_y(232));
            card(cxStage, stage_y(450), cwStage, workBot - stage_y(450));
            hdiv(stage_x(444), stage_x(1156), stage_y(312));
            hdiv(stage_x(444), stage_x(1156), stage_y(560));
        } else if (g_app.active_tab == 2) {
            int leftW = stage_x(794) - cxStage;
            int rx2   = stage_x(806);
            int rightW = (cxStage + cwStage) - rx2;
            card(cxStage, stage_y(112), cwStage, stage_y(434) - stage_y(112));
            card(cxStage, stage_y(442), leftW,  workBot - stage_y(442));
            card(rx2,     stage_y(442), rightW, workBot - stage_y(442));
            hdiv(stage_x(818), stage_x(1160), stage_y(716));
        }

        // Bottom Global Action Bar
        card(S(18), bottomBarY, cw - S(36), S(56));

        // Rounded Input Wells around VISIBLE borderless EDIT controls
        HWND hChild = GetWindow(hwnd, GW_CHILD);
        while (hChild) {
            if ((GetWindowLongPtrW(hChild, GWL_STYLE) & WS_VISIBLE) != 0) {
                wchar_t cls[32] = {0};
                GetClassNameW(hChild, cls, 31);
                if (_wcsicmp(cls, L"Edit") == 0) {
                    RECT wr{};
                    GetWindowRect(hChild, &wr);
                    MapWindowPoints(nullptr, hwnd, reinterpret_cast<LPPOINT>(&wr), 2);
                    bool en = IsWindowEnabled(hChild) != FALSE;
                    float padX = (float)S(5), padY = (float)S(3);
                    gdip_round_rect(g,
                        (float)wr.left - padX, (float)wr.top - padY,
                        (float)(wr.right - wr.left) + 2.0f * padX,
                        (float)(wr.bottom - wr.top) + 2.0f * padY,
                        (float)S(6),
                        gdip_col(en ? RGB(255, 255, 255) : kColDisabledFill),
                        gdip_col(en ? kColInputEdge : RGB(204, 214, 228)), 1.15f);
                }
            }
            hChild = GetWindow(hChild, GW_HWNDNEXT);
        }

        // Smooth Rounded Lake-Blue Progress Bar in Bottom Global Action Bar
        int statRightBar = cw - S(256);
        float px = (float)S(320);
        float py = (float)(bottomBarY + S(36));
        float pw = std::max(120.0f, (float)(statRightBar - S(320)));
        float ph = (float)S(8);
        gdip_round_rect(g, px, py, pw, ph, 0.5f * ph,
                        gdip_col(RGB(220, 229, 242)), gdip_col(RGB(192, 206, 226)), 1.0f);
        if (g_app.prog_pct > 0) {
            float fw = std::max(ph, pw * ((float)g_app.prog_pct / 100.0f));
            gdip_round_rect(g, px, py, fw, ph, 0.5f * ph,
                            gdip_col(kColAccent), Gdiplus::Color(0, 0, 0, 0), 0.0f);
        } else {
            Gdiplus::SolidBrush dbr(gdip_col(kColAccent));
            g.FillEllipse(&dbr, px + 1.0f, py + 1.0f, ph - 2.0f, ph - 2.0f);
        }
    }

    // Header "nijinLBS" pill label
    SelectObject(hdc, g_app.hFontBadge);
    SetTextColor(hdc, kColBadgeText);
    RECT rTag{pillX, pillY, pillX + pillW, pillY + pillH};
    DrawTextW(hdc, L"nijinLBS", -1, &rTag, DT_CENTER | DT_VCENTER | DT_SINGLELINE);

    // Card R3 Live Receipt Sub-Box Header
    SelectObject(hdc, g_app.hFontBold);
    SetTextColor(hdc, kColAccentDark);
    RECT rRecHdr{rxDock + S(22), recTop + S(4), rxDock + S(314), recTop + S(22)};
    DrawTextW(hdc, heraw_i18n::tr_w("r3.receipt_hdr").c_str(), -1, &rRecHdr,
              DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);

    // Inline Status Text & Percentage inside Bottom Action Bar
    int statRight = cw - S(256);
    SelectObject(hdc, g_app.hFont);
    SetTextColor(hdc, kColTextMain);
    RECT rStat{S(320), bottomBarY + S(11), statRight - S(52), bottomBarY + S(31)};
    DrawTextW(hdc, g_app.status_line.c_str(), -1, &rStat, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);

    SelectObject(hdc, g_app.hFontBold);
    SetTextColor(hdc, kColBadgeText);
    wchar_t pbuf[16] = {0};
    std::swprintf(pbuf, 16, L"%d%%", g_app.prog_pct);
    RECT rPct{statRight - S(50), bottomBarY + S(11), statRight, bottomBarY + S(31)};
    DrawTextW(hdc, pbuf, -1, &rPct, DT_RIGHT | DT_VCENTER | DT_SINGLELINE);

    SelectObject(hdc, oldFont);
}

static void reset_combo_items_keep_sel(HWND hCmb, const std::vector<std::wstring>& items) {
    if (!hCmb) return;
    int sel = (int)SendMessageW(hCmb, CB_GETCURSEL, 0, 0);
    if (sel < 0) sel = 0;
    SendMessageW(hCmb, CB_RESETCONTENT, 0, 0);
    for (const auto& it : items) {
        SendMessageW(hCmb, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(it.c_str()));
    }
    if (!items.empty()) {
        SendMessageW(hCmb, CB_SETCURSEL, (WPARAM)std::min(sel, (int)items.size() - 1), 0);
    }
    InvalidateRect(hCmb, nullptr, FALSE);
}

static void apply_ui_language_win() {
    if (!g_app.hwnd) return;
    SetWindowTextW(g_app.hwnd, heraw_i18n::tr_w("app.window_title").c_str());

    for (const auto& tc : g_app.tab_ctrls) {
        if (tc.hw && tc.lang_key) {
            SetWindowTextW(tc.hw, heraw_i18n::tr_w(tc.lang_key).c_str());
            InvalidateRect(tc.hw, nullptr, FALSE);
        }
    }
    if (g_app.hLblBokehHint) {
        SetWindowTextW(g_app.hLblBokehHint,
            heraw_i18n::I18nManager::instance().is_english()
                ? L"↺ Negative = CCW / ↻ Positive = CW · In-focus subject auto-protected"
                : L"↺ 负值逆时针 / ↻ 正值顺时针对数螺旋 · 单次对焦自动保护合焦主体");
    }

    reset_combo_items_keep_sel(g_app.hCmbGrouping, {
        heraw_i18n::tr_w("dock.grp_auto"), heraw_i18n::tr_w("dock.grp_single"),
        heraw_i18n::tr_w("dock.grp_best"), heraw_i18n::tr_w("dock.grp_top1")
    });
    reset_combo_items_keep_sel(g_app.hCmbOlpf, {
        heraw_i18n::tr_w("t1.olpf_auto"), heraw_i18n::tr_w("t1.olpf_none"),
        heraw_i18n::tr_w("t1.olpf_weak4"), heraw_i18n::tr_w("t1.olpf_gauss16")
    });
    reset_combo_items_keep_sel(g_app.hCmbRawFmt, {
        heraw_i18n::tr_w("t1.raw_dng"), heraw_i18n::tr_w("t1.raw_linear"),
        heraw_i18n::tr_w("t1.raw_fp16"), heraw_i18n::tr_w("t1.raw_hq"),
        heraw_i18n::tr_w("t1.raw_uncomp")
    });
    reset_combo_items_keep_sel(g_app.hCmbRgbFmt, {
        heraw_i18n::tr_w("t1.rgb_tiff"), heraw_i18n::tr_w("t1.rgb_tiff_hq"),
        heraw_i18n::tr_w("t1.rgb_png"), heraw_i18n::tr_w("t1.rgb_jxs"),
        heraw_i18n::tr_w("t1.rgb_jxs_lossless"), heraw_i18n::tr_w("t1.rgb_tiff_raw"),
        heraw_i18n::tr_w("t1.rgb_exr")
    });
    if (g_app.hCmbMotionProfile) reset_combo_items_keep_sel(g_app.hCmbMotionProfile, {
        heraw_i18n::tr_w("t1.mprof_auto"), heraw_i18n::tr_w("t1.mprof_std"),
        heraw_i18n::tr_w("t1.mprof_conc"), heraw_i18n::tr_w("t1.mprof_sport"),
        heraw_i18n::tr_w("t1.mprof_dxo")
    });
    if (g_app.hCmbRgb1xTier) reset_combo_items_keep_sel(g_app.hCmbRgb1xTier, {
        heraw_i18n::tr_w("t1.tier_sinc"), heraw_i18n::tr_w("t1.tier_fast"),
        heraw_i18n::tr_w("t1.tier_joint")
    });
    if (g_app.hCmbExrPrec) reset_combo_items_keep_sel(g_app.hCmbExrPrec, {
        heraw_i18n::tr_w("t1.exr_half"), heraw_i18n::tr_w("t1.exr_float")
    });
    if (g_app.hCmbExrParts) reset_combo_items_keep_sel(g_app.hCmbExrParts, {
        heraw_i18n::tr_w("t1.exr_parts_all"), heraw_i18n::tr_w("t1.exr_parts_bs"),
        heraw_i18n::tr_w("t1.exr_parts_b")
    });
    if (g_app.hCmbCodecConstraint) reset_combo_items_keep_sel(g_app.hCmbCodecConstraint, {
        heraw_i18n::tr_w("t1.codec_off"), heraw_i18n::tr_w("t1.codec_box"),
        heraw_i18n::tr_w("t1.codec_probit")
    });
    if (g_app.hCmbSatModel) reset_combo_items_keep_sel(g_app.hCmbSatModel, {
        heraw_i18n::tr_w("t1.sat_hard"), heraw_i18n::tr_w("t1.sat_probit")
    });
    if (g_app.hCmbHighlightMode) reset_combo_items_keep_sel(g_app.hCmbHighlightMode, {
        heraw_i18n::tr_w("t1.hl_legacy"), heraw_i18n::tr_w("t1.hl_oklab"),
        heraw_i18n::tr_w("t1.hl_clip")
    });
    if (g_app.hCmbStreaming) reset_combo_items_keep_sel(g_app.hCmbStreaming, {
        heraw_i18n::tr_w("t1.stream_auto"), heraw_i18n::tr_w("t1.stream_on"),
        heraw_i18n::tr_w("t1.stream_off")
    });
    if (g_app.hCmbShutterMode) reset_combo_items_keep_sel(g_app.hCmbShutterMode, {
        heraw_i18n::tr_w("t2.shut_anchor"), heraw_i18n::tr_w("t2.shut_integ"),
        heraw_i18n::tr_w("t2.shut_nnls"), heraw_i18n::tr_w("t2.shut_deblur")
    });
    if (g_app.hCmbIntraDeblur) reset_combo_items_keep_sel(g_app.hCmbIntraDeblur, {
        heraw_i18n::tr_w("t2.intra_auto"), heraw_i18n::tr_w("t2.intra_on"),
        heraw_i18n::tr_w("t2.intra_off")
    });
    if (g_app.hCmbDiffusionMode) reset_combo_items_keep_sel(g_app.hCmbDiffusionMode, {
        heraw_i18n::tr_w("t2.diff_off"), heraw_i18n::tr_w("t2.diff_hdf"),
        heraw_i18n::tr_w("t2.diff_mist"), heraw_i18n::tr_w("t2.diff_uc"),
        heraw_i18n::tr_w("t2.diff_hal"), heraw_i18n::tr_w("t2.diff_uc_hdf"),
        heraw_i18n::tr_w("t2.diff_uc_mist"), heraw_i18n::tr_w("t2.diff_uc_hal")
    });
    if (g_app.hCmbUcBaffle) reset_combo_items_keep_sel(g_app.hCmbUcBaffle, {
        heraw_i18n::tr_w("t2.baffle_std"), heraw_i18n::tr_w("t2.baffle_hood"),
        heraw_i18n::tr_w("t2.baffle_wide")
    });
    if (g_app.hCmbUcChroma) reset_combo_items_keep_sel(g_app.hCmbUcChroma, {
        heraw_i18n::tr_w("t2.chroma_neu"), heraw_i18n::tr_w("t2.chroma_warm")
    });
    if (g_app.hCmbApertureWeight) reset_combo_items_keep_sel(g_app.hCmbApertureWeight, {
        heraw_i18n::tr_w("t2.ap_voronoi"), heraw_i18n::tr_w("t2.ap_dwell")
    });
    if (g_app.hCmbMetricPolicy) reset_combo_items_keep_sel(g_app.hCmbMetricPolicy, {
        heraw_i18n::tr_w("t2.met_ver"), heraw_i18n::tr_w("t2.met_proxy")
    });
    if (g_app.hCmbSpectralCa) reset_combo_items_keep_sel(g_app.hCmbSpectralCa, {
        heraw_i18n::tr_w("t2.sca_off"), heraw_i18n::tr_w("t2.sca_lat"),
        heraw_i18n::tr_w("t2.sca_aura")
    });
    if (g_app.hCmbSpectralAng) reset_combo_items_keep_sel(g_app.hCmbSpectralAng, {
        heraw_i18n::tr_w("t2.sca_ang_rad"), heraw_i18n::tr_w("t2.sca_ang_0"),
        heraw_i18n::tr_w("t2.sca_ang_45"), heraw_i18n::tr_w("t2.sca_ang_90"),
        heraw_i18n::tr_w("t2.sca_ang_custom")
    });
    if (g_app.hCmbIbpIters) reset_combo_items_keep_sel(g_app.hCmbIbpIters, {
        heraw_i18n::tr_w("t1.ibp_0"), heraw_i18n::tr_w("t1.ibp_1"), heraw_i18n::tr_w("t1.ibp_2")
    });
    if (g_app.hCmbW2DepthGate) reset_combo_items_keep_sel(g_app.hCmbW2DepthGate, {
        heraw_i18n::tr_w("t3.gate_auto"), heraw_i18n::tr_w("t3.gate_relaxed"), heraw_i18n::tr_w("t3.gate_force")
    });
    if (g_app.hCmbDepthGate) reset_combo_items_keep_sel(g_app.hCmbDepthGate, {
        heraw_i18n::tr_w("t3.gate_auto"), heraw_i18n::tr_w("t3.gate_relaxed"), heraw_i18n::tr_w("t3.gate_force")
    });
    if (g_app.hCmbObsGate) reset_combo_items_keep_sel(g_app.hCmbObsGate, {
        heraw_i18n::tr_w("t3.gate_auto"), heraw_i18n::tr_w("t3.gate_relaxed"), heraw_i18n::tr_w("t3.gate_force")
    });
    if (g_app.hCmbRefocusMode) reset_combo_items_keep_sel(g_app.hCmbRefocusMode, {
        heraw_i18n::tr_w("w2.ref_mode_auto"), heraw_i18n::tr_w("w2.ref_mode_bokeh"),
        heraw_i18n::tr_w("w2.ref_mode_synth"), heraw_i18n::tr_w("w2.ref_mode_shift")
    });
    if (g_app.hCmbBokehBlades) reset_combo_items_keep_sel(g_app.hCmbBokehBlades, {
        heraw_i18n::tr_w("w2.blades_0"), heraw_i18n::tr_w("w2.blades_5"), heraw_i18n::tr_w("w2.blades_6"),
        heraw_i18n::tr_w("w2.blades_7"), heraw_i18n::tr_w("w2.blades_8"), heraw_i18n::tr_w("w2.blades_9"),
        heraw_i18n::tr_w("w2.blades_11")
    });
    if (g_app.hCmbSeeThrough) reset_combo_items_keep_sel(g_app.hCmbSeeThrough, {
        heraw_i18n::tr_w("w2.see_auto"), heraw_i18n::tr_w("w2.see_on"), heraw_i18n::tr_w("w2.see_off")
    });
    reset_combo_items_keep_sel(g_app.hCmbFieldMode, {
        heraw_i18n::tr_w("t1.fld_tiff_alpha"), heraw_i18n::tr_w("t1.fld_split"),
        heraw_i18n::tr_w("t1.fld_rgb"), heraw_i18n::tr_w("t3.car_motion"),
        heraw_i18n::tr_w("t3.car_phase"), heraw_i18n::tr_w("t3.car_focus")
    });
    reset_combo_items_keep_sel(g_app.hCmbBackend, {
        heraw_i18n::tr_w("t1.bk_auto"),
        heraw_i18n::tr_w("t1.bk_gpu_win"),
        heraw_i18n::tr_w("t1.bk_cpu")
    });
    {
        std::wstring cmb_lbls[5], mnu_lbls[5];
        build_win_cpu_core_labels(cmb_lbls, mnu_lbls);
        reset_combo_items_keep_sel(g_app.hCmbCpuCores, {
            cmb_lbls[0], cmb_lbls[1], cmb_lbls[2], cmb_lbls[3], cmb_lbls[4]
        });
    }
    reset_combo_items_keep_sel(g_app.hCmbPriority, {
        heraw_i18n::tr_w("menu.prio_normal"),
        heraw_i18n::tr_w("menu.prio_above"),
        heraw_i18n::tr_w("menu.prio_below"),
        heraw_i18n::tr_w("menu.prio_idle")
    });

    if (g_app.hOutDir) {
        std::wstring ph = heraw_i18n::tr_w("t1.outdir_ph");
        SendMessageW(g_app.hOutDir, EM_SETCUEBANNER, FALSE, reinterpret_cast<LPARAM>(ph.c_str()));
    }
    if (g_app.hEdtBusIn) {
        std::wstring bph = heraw_i18n::tr_w("t3.bus_in_ph");
        SendMessageW(g_app.hEdtBusIn, EM_SETCUEBANNER, FALSE, reinterpret_cast<LPARAM>(bph.c_str()));
    }
    for (HWND he : {g_app.hEdtDepthDisp, g_app.hEdtDepthCoh, g_app.hEdtDepthSup, g_app.hEdtReadoutMs}) {
        if (he) SendMessageW(he, EM_SETCUEBANNER, FALSE, reinterpret_cast<LPARAM>(L"auto"));
    }
    if (g_app.hSegFieldBank) {
        seg_pill_set_items(g_app.hSegFieldBank, {
            heraw_i18n::tr_w("t3.tab_core11"), heraw_i18n::tr_w("t3.tab_ext16")
        });
    }

    seg_pill_set_items(g_app.hTabBar, {
        heraw_i18n::tr_w("tab.1"), heraw_i18n::tr_w("tab.2"), heraw_i18n::tr_w("tab.3")
    });
    seg_pill_set_items(g_app.hSegPreset, {
        heraw_i18n::tr_w("t1.preset_photo"), heraw_i18n::tr_w("t1.preset_astro"),
        heraw_i18n::tr_w("t1.preset_sci"), heraw_i18n::tr_w("t1.preset_arc")
    });
    seg_pill_set_items(g_app.hSegRgbScale, {
        heraw_i18n::tr_w("t1.scale_2x"), heraw_i18n::tr_w("t1.scale_1x"),
        heraw_i18n::tr_w("t1.scale_both")
    });
    seg_pill_set_items(g_app.hSegT2Anchor, {
        heraw_i18n::tr_w("t2.pose_auto"), heraw_i18n::tr_w("t2.pose_first"),
        heraw_i18n::tr_w("t2.pose_middle"), heraw_i18n::tr_w("t2.pose_follow")
    });
    seg_pill_set_items(g_app.hSegStrobeFade, {
        heraw_i18n::tr_w("t2.fade_solid"), heraw_i18n::tr_w("t2.fade_trail")
    });
    seg_pill_set_items(g_app.hSegTransColor, {
        heraw_i18n::tr_w("t2.trans_rgb"), heraw_i18n::tr_w("t2.trans_gray")
    });
    seg_pill_set_items(g_app.hSegReflMode, {
        heraw_i18n::tr_w("t2.refl_glint"), heraw_i18n::tr_w("t2.refl_split")
    });

    for (size_t gi = 0; gi < g_app.groups.size(); ++gi) {
        recalculate_group_item(g_app.groups[gi], (int)gi + 1);
    }
    if (!g_app.optics.measured) {
        g_app.optics.grade_badge = heraw_i18n::tr_w("t4.badge_pending");
        g_app.optics.ois_note = heraw_i18n::tr_w("t4.ois_idle");
        if (g_app.hLblOpticsStatus) SetWindowTextW(g_app.hLblOpticsStatus, g_app.optics.ois_note.c_str());
    }
    if (g_app.hLblSiliconStats) {
        SetWindowTextW(g_app.hLblSiliconStats, silicon_readout(g_app.optics).c_str());
    }
    if (g_app.inputs.empty() && !g_app.running.load()) {
        g_app.status_line = heraw_i18n::tr_w("bot.ready_idle");
    }
    refresh_engine_status_label();
    sync_cross_tab_ui();
    repaint_left_dock_views();
    InvalidateRect(g_app.hwnd, nullptr, TRUE);
}

static void show_engine_popup_menu(HWND hwnd, HWND hAnchor, bool align_bottom_down = false) {
    stop_perf_breathing_alert(hwnd);
    HMENU hMenu = CreatePopupMenu();
    if (!hMenu) return;

    const bool en = heraw_i18n::I18nManager::instance().is_english();
    int bsel = g_app.hCmbBackend ? (int)SendMessageW(g_app.hCmbBackend, CB_GETCURSEL, 0, 0) : 0;
    int csel = g_app.hCmbCpuCores ? (int)SendMessageW(g_app.hCmbCpuCores, CB_GETCURSEL, 0, 0) : 0;
    int psel = g_app.hCmbPriority ? (int)SendMessageW(g_app.hCmbPriority, CB_GETCURSEL, 0, 0) : 0;
    int ssel = g_app.hCmbStreaming ? (int)SendMessageW(g_app.hCmbStreaming, CB_GETCURSEL, 0, 0) : 0;
    int mem_gb = g_app.hEdtMemBudget ? _wtoi(trim_ws(get_wnd_text(g_app.hEdtMemBudget)).c_str()) : 4;

    AppendMenuW(hMenu, MF_STRING | MF_GRAYED, 0,
        heraw_i18n::trf_w("menu.hw_detected_fmt", wide_to_utf8(format_win_cpu_topology_badge()).c_str()).c_str());
    {
        std::wstring gpu_badge;
        if (!g_app.gpu_policy.has_gpu || g_app.gpu_policy.device_name[0] == '\0') {
            gpu_badge = en ? L"Detected GPU: No Vulkan GPU (CPU default)" : L"已识别显卡: 无可用 Vulkan 显卡 (默认纯 CPU)";
        } else {
            std::wstring gname = utf8_to_wide(g_app.gpu_policy.device_name);
            if (g_app.gpu_policy.is_discrete) gpu_badge = (en ? L"Discrete GPU: " : L"已识别独显: ") + gname;
            else if (g_app.gpu_policy.default_disable_gpu) gpu_badge = (en ? L"Integrated GPU: " : L"已识别核显: ") + gname + (en ? L" (Default CPU)" : L" (默认关闭加速)");
            else gpu_badge = (en ? L"Integrated GPU: " : L"已识别核显: ") + gname + (en ? L" (Xe2+ Enabled)" : L" (Xe2+ 默认启用)");
        }
        AppendMenuW(hMenu, MF_STRING | MF_GRAYED, 0, gpu_badge.c_str());
    }
    AppendMenuW(hMenu, MF_SEPARATOR, 0, nullptr);

    if (g_app.gpu_policy.default_disable_gpu) {
        AppendMenuW(hMenu, MF_STRING, ID_MENU_BACKEND_AUTO, en ? L"GPU Accel: Auto (dGPU/Xe2+ on, current iGPU uses CPU)" : L"显卡加速: 自动 (独显/Xe2+启用，当前核显走CPU)");
        AppendMenuW(hMenu, MF_STRING, ID_MENU_BACKEND_GPU,  en ? L"GPU Accel: Force Vulkan GPU" : L"显卡加速: 强制开启 Vulkan GPU");
        AppendMenuW(hMenu, MF_STRING, ID_MENU_BACKEND_CPU,  en ? L"GPU Accel: Pure CPU (Cache-Adaptive SIMD)" : L"显卡加速: 纯 CPU (自适应缓存加速)");
    } else {
        AppendMenuW(hMenu, MF_STRING, ID_MENU_BACKEND_AUTO, en ? L"GPU Accel: Auto (Vulkan GPU Preferred)" : L"显卡加速: 自动 (Vulkan GPU 优先)");
        AppendMenuW(hMenu, MF_STRING, ID_MENU_BACKEND_GPU,  en ? L"GPU Accel: Force Vulkan GPU" : L"显卡加速: 强制 Vulkan GPU");
        AppendMenuW(hMenu, MF_STRING, ID_MENU_BACKEND_CPU,  en ? L"GPU Accel: Pure CPU (Cache-Adaptive SIMD)" : L"显卡加速: 纯 CPU (自适应缓存加速)");
    }
    CheckMenuRadioItem(hMenu, ID_MENU_BACKEND_AUTO, ID_MENU_BACKEND_CPU,
                       ID_MENU_BACKEND_AUTO + std::max(0, std::min(2, bsel)), MF_BYCOMMAND);

    AppendMenuW(hMenu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(hMenu, MF_STRING | (g_app.is_battery_device ? MF_CHECKED : MF_UNCHECKED),
                ID_MENU_BATTERY_DEVICE, en ? L"Battery / Portable Device (Default No-SMT)" : L"是电池设备 (默认关闭超线程 SMT)");
    std::wstring cmb_lbls[5], mnu_lbls[5];
    build_win_cpu_core_labels(cmb_lbls, mnu_lbls);
    for (int i = 0; i < 5; ++i) {
        AppendMenuW(hMenu, MF_STRING, ID_MENU_CORES_AUTO + i, mnu_lbls[i].c_str());
    }
    CheckMenuRadioItem(hMenu, ID_MENU_CORES_AUTO, ID_MENU_CORES_ALL,
                       ID_MENU_CORES_AUTO + std::max(0, std::min(4, csel)), MF_BYCOMMAND);

    AppendMenuW(hMenu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(hMenu, MF_STRING, ID_MENU_PRIO_NORMAL, en ? L"Priority: Normal (Prevent Win11 Background Throttling)" : L"调度优先级: 标准 (防Win11后台降频)");
    AppendMenuW(hMenu, MF_STRING, ID_MENU_PRIO_HIGH,   en ? L"Priority: Above Normal (High Performance)" : L"调度优先级: 高性能优先 (Above Normal)");
    AppendMenuW(hMenu, MF_STRING, ID_MENU_PRIO_BELOW,  en ? L"Priority: Below Normal (Yield to Foreground)" : L"调度优先级: 低于标准 (让路前台应用)");
    AppendMenuW(hMenu, MF_STRING, ID_MENU_PRIO_IDLE,   en ? L"Priority: EcoQoS Idle (Power Saver)" : L"调度优先级: 后台静默省电 (Win11 EcoQoS)");
    CheckMenuRadioItem(hMenu, ID_MENU_PRIO_NORMAL, ID_MENU_PRIO_IDLE,
                       ID_MENU_PRIO_NORMAL + std::max(0, std::min(3, psel)), MF_BYCOMMAND);

    AppendMenuW(hMenu, MF_SEPARATOR, 0, nullptr);
    HMENU hStream = CreatePopupMenu();
    AppendMenuW(hStream, MF_STRING, ID_MENU_STREAM_AUTO, heraw_i18n::tr_w("t1.stream_auto").c_str());
    AppendMenuW(hStream, MF_STRING, ID_MENU_STREAM_ON,   heraw_i18n::tr_w("t1.stream_on").c_str());
    AppendMenuW(hStream, MF_STRING, ID_MENU_STREAM_OFF,  heraw_i18n::tr_w("t1.stream_off").c_str());
    CheckMenuRadioItem(hStream, ID_MENU_STREAM_AUTO, ID_MENU_STREAM_OFF,
                       ID_MENU_STREAM_AUTO + std::clamp(ssel, 0, 2), MF_BYCOMMAND);
    AppendMenuW(hStream, MF_SEPARATOR, 0, nullptr);
    static const int kMemBudgets[4] = {2, 4, 8, 16};
    for (int i = 0; i < 4; ++i) {
        std::wstring ml = (en ? L"Stripe Target: " : L"分带目标预算: ") + std::to_wstring(kMemBudgets[i]) + L" GB";
        AppendMenuW(hStream, MF_STRING | ((mem_gb == kMemBudgets[i]) ? MF_CHECKED : MF_UNCHECKED),
                    ID_MENU_MEM_2G + i, ml.c_str());
    }
    AppendMenuW(hMenu, MF_POPUP, reinterpret_cast<UINT_PTR>(hStream),
                en ? L"Stripe Streaming & Memory Target" : L"分带流式与内存调度目标");

    HMENU hLang = CreatePopupMenu();
    auto langs = heraw_i18n::I18nManager::instance().available_languages();
    std::string active_lang = heraw_i18n::I18nManager::instance().active_code();
    for (size_t li = 0; li < langs.size() && li < 40; ++li) {
        UINT lflags = MF_STRING | ((langs[li].first == active_lang) ? MF_CHECKED : MF_UNCHECKED);
        AppendMenuW(hLang, lflags, ID_MENU_LANG_BASE + (UINT)li, utf8_to_wide(langs[li].second).c_str());
    }
    AppendMenuW(hLang, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(hLang, MF_STRING, ID_MENU_LANG_LOAD_FILE,
                en ? L"Load External Language Pack (.lng)..." : L"加载外部语言文件 (.lng)...");
    AppendMenuW(hMenu, MF_POPUP, reinterpret_cast<UINT_PTR>(hLang), heraw_i18n::tr_w("menu.language").c_str());

    AppendMenuW(hMenu, MF_STRING, ID_MENU_EXPORT_LOG, (heraw_i18n::tr_w("menu.export_log") + L"\tCtrl+L").c_str());

    AppendMenuW(hMenu, MF_SEPARATOR, 0, nullptr);
    HMENU hEng = CreatePopupMenu();
    AppendMenuW(hEng, MF_STRING, ID_MENU_CLI_BROWSE,    en ? L"Select CLI Engine (nijinLBS.exe)..." : L"指定 CLI 引擎路径 (nijinLBS.exe)...");
    AppendMenuW(hEng, MF_STRING, ID_MENU_CLI_AUTO,      en ? L"Auto-Detect Bundled / PATH CLI" : L"自动探测同目录 / PATH 引擎");
    AppendMenuW(hEng, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(hEng, MF_STRING, ID_MENU_LIBRAW_BROWSE, en ? L"Load External LibRaw (.dll)..." : L"加载外部 LibRaw 动态库 (.dll)...");
    AppendMenuW(hEng, MF_STRING, ID_MENU_LIBRAW_RESET,  en ? L"Restore Built-in nijinWVDC + LibRaw 0.23-HE*" : L"恢复内置 nijinWVDC + LibRaw 0.23-HE*");
    AppendMenuW(hMenu, MF_POPUP, reinterpret_cast<UINT_PTR>(hEng), en ? L"CLI Engine & External LibRaw" : L"CLI 引擎与外部 LibRaw");

    AppendMenuW(hMenu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(hMenu, MF_STRING, ID_MENU_WELCOME_SETUP, heraw_i18n::tr_w("menu.welcome_setup").c_str());
    AppendMenuW(hMenu, MF_STRING, ID_MENU_ABOUT,         heraw_i18n::tr_w("menu.about").c_str());

    RECT br{};
    GetWindowRect(hAnchor ? hAnchor : g_app.hBtnEngineMenu, &br);
    UINT flags = align_bottom_down ? (TPM_RIGHTALIGN | TPM_TOPALIGN) : (TPM_RIGHTALIGN | TPM_BOTTOMALIGN);
    int py = align_bottom_down ? (br.bottom + 2) : br.top;
    TrackPopupMenuEx(hMenu, flags, br.right, py, hwnd, nullptr);
    DestroyMenu(hMenu);
}

// ============================================================================
// First-Launch Language / Performance / Backend Setup & Clickable About Modal
// ============================================================================
struct CreditHitLink {
    RECT rc{};
    std::wstring text;
    std::wstring url;
};

struct CreditsViewData {
    bool is_english = false;
    int hover_link = -1;
    std::vector<CreditHitLink> hit_links;
    std::wstring plain_dump;
};

struct RichCreditSpan {
    const wchar_t* text;
    const wchar_t* url;
};

static void layout_and_paint_credits(HDC hdc, int w, CreditsViewData* cd, bool do_draw) {
    if (!cd || w <= 0) return;
    auto S = [](int px) { return g_app.S(px); };
    cd->hit_links.clear();
    cd->plain_dump.clear();

    SetBkMode(hdc, TRANSPARENT);
    HGDIOBJ oldFont = SelectObject(hdc, g_app.hFontLinkHdr ? g_app.hFontLinkHdr : g_app.hFontCardHdr);

    // 1. Project Version Hyperlink -> GitHub (with ★ Star on GitHub beside it in English)
    const std::wstring projTitle = L"NijiN.LiteBurstSuite " NIJIN_VERSION;
    const std::wstring ghUrl     = L"https://github.com/A2Sumie/NijiN.LiteBurstSuite";
    SIZE szProj{};
    GetTextExtentPoint32W(hdc, projTitle.c_str(), (int)projTitle.size(), &szProj);
    int topLineH = szProj.cy + S(2);
    RECT rProj{0, 0, szProj.cx, topLineH};
    int projIdx = (int)cd->hit_links.size();
    cd->hit_links.push_back(CreditHitLink{rProj, projTitle, ghUrl});
    cd->plain_dump += projTitle;
    if (do_draw) {
        SetTextColor(hdc, (cd->hover_link == projIdx) ? kColAccent : kColBadgeText);
        DrawTextW(hdc, projTitle.c_str(), -1, &rProj, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
    }
    if (cd->is_english) {
        const wchar_t* sepTxt = L"   ·   ";
        const wchar_t* starTxt = L"★ Star on GitHub";
        SelectObject(hdc, g_app.hFont);
        SIZE szSep{};
        GetTextExtentPoint32W(hdc, sepTxt, (int)std::wcslen(sepTxt), &szSep);
        RECT rSep{rProj.right, 0, rProj.right + szSep.cx, topLineH};
        cd->plain_dump += sepTxt;
        if (do_draw) {
            SetTextColor(hdc, kColTextMuted);
            DrawTextW(hdc, sepTxt, -1, &rSep, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
        }
        SelectObject(hdc, g_app.hFontLink ? g_app.hFontLink : g_app.hFont);
        SIZE szStar{};
        GetTextExtentPoint32W(hdc, starTxt, (int)std::wcslen(starTxt), &szStar);
        RECT rStar{rSep.right, 0, rSep.right + szStar.cx, topLineH};
        int starIdx = (int)cd->hit_links.size();
        cd->hit_links.push_back(CreditHitLink{rStar, starTxt, ghUrl});
        cd->plain_dump += starTxt;
        if (do_draw) {
            SetTextColor(hdc, (cd->hover_link == starIdx) ? kColAccent : kColBadgeText);
            DrawTextW(hdc, starTxt, -1, &rStar, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
        }
    }
    cd->plain_dump += L"\n";

    int y = topLineH + S(5);

    // 2. Apache-2.0 + LibRaw CDDL-1.0 + nijinWVDC Notice
    SelectObject(hdc, g_app.hFontSmall);
    const std::wstring licText = heraw_i18n::tr_w("about.lic_text");
    RECT rLic{0, y, w, y + S(96)};
    DrawTextW(hdc, licText.c_str(), -1, &rLic, DT_LEFT | DT_WORDBREAK | DT_NOPREFIX | DT_CALCRECT);
    cd->plain_dump += licText + L"\n";
    if (do_draw) {
        SetTextColor(hdc, kColTextMuted);
        RECT rDraw{0, y, w, rLic.bottom};
        DrawTextW(hdc, licText.c_str(), -1, &rDraw, DT_LEFT | DT_WORDBREAK | DT_NOPREFIX);
    }
    y = rLic.bottom + S(9);

    auto emit_span_row = [&](const RichCreditSpan* spans, size_t nspans, int rowY) -> int {
        int curX = 0;
        int lineH = S(20);
        for (size_t i = 0; i < nspans; ++i) {
            const wchar_t* t = spans[i].text;
            const wchar_t* u = spans[i].url;
            bool is_lnk = (u != nullptr && u[0] != L'\0');
            SelectObject(hdc, is_lnk ? (g_app.hFontLink ? g_app.hFontLink : g_app.hFont) : g_app.hFont);
            SIZE sz{};
            int tlen = (int)std::wcslen(t);
            GetTextExtentPoint32W(hdc, t, tlen, &sz);
            lineH = std::max(lineH, (int)sz.cy + S(2));
            RECT rs{curX, rowY, curX + sz.cx, rowY + lineH};
            cd->plain_dump += t;
            if (is_lnk) {
                int lidx = (int)cd->hit_links.size();
                cd->hit_links.push_back(CreditHitLink{rs, t, u});
                if (do_draw) {
                    SetTextColor(hdc, (cd->hover_link == lidx) ? kColAccent : kColBadgeText);
                    DrawTextW(hdc, t, -1, &rs, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
                }
            } else if (do_draw) {
                SetTextColor(hdc, kColTextMain);
                DrawTextW(hdc, t, -1, &rs, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
            }
            curX += sz.cx;
        }
        cd->plain_dump += L"\n";
        return rowY + lineH;
    };

    // 3. Author & Support Links (zh-CN vs en-US exact contract)
    if (!cd->is_english) {
        static const RichCreditSpan kZhAuthor[] = {
            {L"作者 A2Sumie  ·  B站 ", L""},
            {L"@共星", L"https://space.bilibili.com/3546931909364051"},
            {L"  ", L""},
            {L"@格子A2Si_苏和郡", L"https://space.bilibili.com/2127237409"},
            {L"  ·  推特 ", L""},
            {L"@kA2Sumie", L"https://x.com/kA2Sumie"},
            {L"  ·  小红书 kA2Sumie", L""}
        };
        y = emit_span_row(kZhAuthor, sizeof(kZhAuthor) / sizeof(kZhAuthor[0]), y);
    } else {
        static const RichCreditSpan kEnAuthor[] = {
            {L"Author: A2Sumie  ·  X ", L""},
            {L"@kA2Sumie", L"https://x.com/kA2Sumie"}
        };
        y = emit_span_row(kEnAuthor, sizeof(kEnAuthor) / sizeof(kEnAuthor[0]), y);
    }

    y += S(6);
    if (do_draw) {
        HBRUSH hDiv = CreateSolidBrush(kColCardBorder);
        RECT rDiv{0, y, std::min(w, S(360)), y + 1};
        FillRect(hdc, &rDiv, hDiv);
        DeleteObject(hDiv);
    }
    y += S(7);

    if (!cd->is_english) {
        static const RichCreditSpan kZhSupport[] = {
            {L"支持本项目可以关注 ", L""},
            {L"@227_Official", L"https://space.bilibili.com/3546378326248411"},
            {L" 并 ", L""},
            {L"Star 本项目", L"https://github.com/A2Sumie/NijiN.LiteBurstSuite"}
        };
        y = emit_span_row(kZhSupport, sizeof(kZhSupport) / sizeof(kZhSupport[0]), y);
    } else {
        static const RichCreditSpan kEnSupport[] = {
            {L"Support this project: Please follow ", L""},
            {L"@sally_amaki", L"https://x.com/sally_amaki"},
            {L"  ·  ", L""},
            {L"@sallyamakiofficial", L"https://www.youtube.com/@sallyamakiofficial"},
            {L"  ·  ", L""},
            {L"@227SMEJ", L"https://x.com/227SMEJ"}
        };
        y = emit_span_row(kEnSupport, sizeof(kEnSupport) / sizeof(kEnSupport[0]), y);
    }

    y += S(3);
    const wchar_t* closeMsg = !cd->is_english ? L"您的一点心意就是最好的支持" : L"Your goodwill is the greatest support.";
    cd->plain_dump += closeMsg;
    if (do_draw) {
        SelectObject(hdc, g_app.hFont);
        SetTextColor(hdc, kColTextMain);
        RECT rClose{0, y, w, y + S(22)};
        DrawTextW(hdc, closeMsg, -1, &rClose, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
    }

    SelectObject(hdc, oldFont);
}

static LRESULT CALLBACK NijiCreditsViewWndProc(HWND hw, UINT msg, WPARAM wParam, LPARAM lParam) {
    auto* cd = reinterpret_cast<CreditsViewData*>(GetWindowLongPtrW(hw, GWLP_USERDATA));
    switch (msg) {
        case WM_ERASEBKGND:
            return 1;
        case WM_SETCURSOR:
            if (cd && cd->hover_link >= 0) {
                SetCursor(LoadCursorW(nullptr, IDC_HAND));
                return TRUE;
            }
            break;
        case WM_MOUSEMOVE: {
            if (!cd) break;
            POINT pt{GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
            int hit = -1;
            for (size_t i = 0; i < cd->hit_links.size(); ++i) {
                if (PtInRect(&cd->hit_links[i].rc, pt)) {
                    hit = (int)i;
                    break;
                }
            }
            if (hit != cd->hover_link) {
                cd->hover_link = hit;
                TRACKMOUSEEVENT tme{};
                tme.cbSize = sizeof(tme);
                tme.dwFlags = TME_LEAVE;
                tme.hwndTrack = hw;
                TrackMouseEvent(&tme);
                InvalidateRect(hw, nullptr, FALSE);
            }
            return 0;
        }
        case WM_MOUSELEAVE:
            if (cd && cd->hover_link != -1) {
                cd->hover_link = -1;
                InvalidateRect(hw, nullptr, FALSE);
            }
            return 0;
        case WM_LBUTTONDOWN: {
            if (!cd) break;
            POINT pt{GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
            for (const auto& lnk : cd->hit_links) {
                if (PtInRect(&lnk.rc, pt) && !lnk.url.empty()) {
                    ShellExecuteW(GetParent(hw), L"open", lnk.url.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
                    return 0;
                }
            }
            return 0;
        }
        case WM_PAINT: {
            PAINTSTRUCT ps{};
            HDC hdc = BeginPaint(hw, &ps);
            RECT rc{};
            GetClientRect(hw, &rc);
            HDC mem = CreateCompatibleDC(hdc);
            HBITMAP bmp = CreateCompatibleBitmap(hdc, std::max(1L, rc.right), std::max(1L, rc.bottom));
            HGDIOBJ old = SelectObject(mem, bmp);
            FillRect(mem, &rc, g_app.hBrCard);
            layout_and_paint_credits(mem, rc.right, cd, true);
            BitBlt(hdc, 0, 0, rc.right, rc.bottom, mem, 0, 0, SRCCOPY);
            SelectObject(mem, old);
            DeleteObject(bmp);
            DeleteDC(mem);
            EndPaint(hw, &ps);
            return 0;
        }
        case WM_PRINT:
        case WM_PRINTCLIENT: {
            RECT rc{};
            GetClientRect(hw, &rc);
            HDC hdc = reinterpret_cast<HDC>(wParam);
            FillRect(hdc, &rc, g_app.hBrCard);
            layout_and_paint_credits(hdc, rc.right, cd, true);
            return 0;
        }
        case WM_NCDESTROY:
            delete cd;
            SetWindowLongPtrW(hw, GWLP_USERDATA, 0);
            break;
    }
    return DefWindowProcW(hw, msg, wParam, lParam);
}

struct ModalDlgData {
    bool is_first_launch = false;
    bool cli_ok = true;
    int result_code = 0; // 0 = closed, 1 = confirmed, 2 = open engine menu, 3 = switch to welcome
    HWND hCmbLang = nullptr;
    HWND hCmbBackend = nullptr;
    HWND hCmbStream = nullptr;
    HWND hEdtMem = nullptr;
    HWND hCmbCores = nullptr;
    HWND hChkBattery = nullptr;
    HWND hLblHint = nullptr;
    HWND hLblReady = nullptr;
    HWND hCredits = nullptr;
    std::vector<std::string> lang_codes;
};

static void populate_welcome_or_about_win(HWND hDlg, bool is_first_launch);

static void sync_welcome_dlg_to_main_win(ModalDlgData* md) {
    if (!md || !md->is_first_launch) return;
    if (md->hCmbBackend && g_app.hCmbBackend) {
        int bsel = (int)SendMessageW(md->hCmbBackend, CB_GETCURSEL, 0, 0);
        if (bsel >= 0) SendMessageW(g_app.hCmbBackend, CB_SETCURSEL, (WPARAM)bsel, 0);
    }
    if (md->hCmbStream && g_app.hCmbStreaming) {
        int ssel = (int)SendMessageW(md->hCmbStream, CB_GETCURSEL, 0, 0);
        if (ssel >= 0) SendMessageW(g_app.hCmbStreaming, CB_SETCURSEL, (WPARAM)ssel, 0);
    }
    if (md->hEdtMem && g_app.hEdtMemBudget) {
        std::wstring mtxt = trim_ws(get_wnd_text(md->hEdtMem));
        if (_wtoi(mtxt.c_str()) > 0) SetWindowTextW(g_app.hEdtMemBudget, mtxt.c_str());
    }
    if (md->hCmbCores && g_app.hCmbCpuCores) {
        int csel = (int)SendMessageW(md->hCmbCores, CB_GETCURSEL, 0, 0);
        if (csel >= 0) SendMessageW(g_app.hCmbCpuCores, CB_SETCURSEL, (WPARAM)csel, 0);
    }
    if (md->hChkBattery) {
        g_app.is_battery_device = get_chk(md->hChkBattery);
    }
}

static void paint_modal_dlg_bg(HWND hDlg, HDC hdc, const ModalDlgData* md) {
    RECT crc{};
    GetClientRect(hDlg, &crc);
    FillRect(hdc, &crc, g_app.hBrCanvas);
    auto S = [](int px) { return g_app.S(px); };
    int cw = crc.right;

    // Top White Banner (0 .. 52)
    RECT hdr{0, 0, cw, S(52)};
    FillRect(hdc, &hdr, g_app.hBrCard);

    HBRUSH brAcc = CreateSolidBrush(kColAccent);
    RECT topStripe{0, 0, cw, S(3)};
    FillRect(hdc, &topStripe, brAcc);
    DeleteObject(brAcc);

    HBRUSH brBd = CreateSolidBrush(kColCardBorder);
    RECT hdrLine{0, S(51), cw, S(52)};
    FillRect(hdc, &hdrLine, brBd);
    DeleteObject(brBd);

    Gdiplus::Graphics g(hdc);
    g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);

    if (md && md->is_first_launch) {
        gdip_round_rect(g, (float)S(16), (float)S(64), (float)(cw - S(32)), (float)S(268),
                        (float)S(10), gdip_col(kColCard), gdip_col(kColCardBorder), 1.15f);
        Gdiplus::Pen divPen(gdip_col(kColSubBorder), 1.0f);
        g.DrawLine(&divPen, (float)S(30), (float)S(127), (float)(cw - S(30)), (float)S(127));
        gdip_round_rect(g, (float)S(30), (float)S(134), (float)(cw - S(60)), (float)S(44),
                        (float)S(8), gdip_col(kColCardSub), gdip_col(kColSubBorder), 1.0f);

        gdip_round_rect(g, (float)S(16), (float)S(342), (float)(cw - S(32)), (float)S(206),
                        (float)S(10), gdip_col(kColCard), gdip_col(kColCardBorder), 1.15f);

        if (md->hEdtMem && (GetWindowLongPtrW(md->hEdtMem, GWL_STYLE) & WS_VISIBLE) != 0) {
            RECT wr{};
            GetWindowRect(md->hEdtMem, &wr);
            MapWindowPoints(nullptr, hDlg, reinterpret_cast<LPPOINT>(&wr), 2);
            float padX = (float)S(5), padY = (float)S(3);
            gdip_round_rect(g,
                (float)wr.left - padX, (float)wr.top - padY,
                (float)(wr.right - wr.left) + 2.0f * padX,
                (float)(wr.bottom - wr.top) + 2.0f * padY,
                (float)S(6), gdip_col(RGB(255, 255, 255)), gdip_col(kColInputEdge), 1.15f);
        }
    } else {
        gdip_round_rect(g, (float)S(16), (float)S(64), (float)(cw - S(32)), (float)S(206),
                        (float)S(10), gdip_col(kColCard), gdip_col(kColCardBorder), 1.15f);
        gdip_round_rect(g, (float)S(16), (float)S(280), (float)(cw - S(32)), (float)S(48),
                        (float)S(10), gdip_col(kColCard), gdip_col(kColCardBorder), 1.15f);
    }
}

static LRESULT CALLBACK NijiModalDlgWndProc(HWND hDlg, UINT msg, WPARAM wParam, LPARAM lParam) {
    auto* md = reinterpret_cast<ModalDlgData*>(GetWindowLongPtrW(hDlg, GWLP_USERDATA));
    switch (msg) {
        case WM_ERASEBKGND:
            return 1;
        case WM_DRAWITEM: {
            const auto* dis = reinterpret_cast<const DRAWITEMSTRUCT*>(lParam);
            if (dis && dis->CtlType == ODT_BUTTON) {
                draw_modern_pill_button(dis);
                return TRUE;
            }
            break;
        }
        case WM_CTLCOLORSTATIC: {
            HDC hdc = reinterpret_cast<HDC>(wParam);
            HWND hCtrl = reinterpret_cast<HWND>(lParam);
            if (md && hCtrl == md->hLblHint) {
                SetBkColor(hdc, kColCardSub);
                SetTextColor(hdc, md->cli_ok ? kColBadgeText : kColWarnAmber);
                return reinterpret_cast<LRESULT>(g_app.hBrCardSub);
            }
            SetBkColor(hdc, kColCard);
            if (md && hCtrl == md->hLblReady) {
                SetTextColor(hdc, md->cli_ok ? kColOkGreen : kColWarnAmber);
            } else if (GetPropW(hCtrl, L"NijiMuted") != nullptr) {
                SetTextColor(hdc, kColTextMuted);
            } else {
                SetTextColor(hdc, kColTextMain);
            }
            return reinterpret_cast<LRESULT>(g_app.hBrCard);
        }
        case WM_CTLCOLOREDIT: {
            HDC hdc = reinterpret_cast<HDC>(wParam);
            SetBkColor(hdc, RGB(255, 255, 255));
            SetTextColor(hdc, kColTextMain);
            return reinterpret_cast<LRESULT>(g_app.hBrWhite);
        }
        case WM_CTLCOLORBTN: {
            HDC hdc = reinterpret_cast<HDC>(wParam);
            SetBkColor(hdc, kColCard);
            return reinterpret_cast<LRESULT>(g_app.hBrCard);
        }
        case WM_PAINT: {
            PAINTSTRUCT ps{};
            HDC hdc = BeginPaint(hDlg, &ps);
            RECT crc{};
            GetClientRect(hDlg, &crc);
            HDC mem = CreateCompatibleDC(hdc);
            HBITMAP bmp = CreateCompatibleBitmap(hdc, std::max(1L, crc.right), std::max(1L, crc.bottom));
            HGDIOBJ old = SelectObject(mem, bmp);
            paint_modal_dlg_bg(hDlg, mem, md);
            BitBlt(hdc, 0, 0, crc.right, crc.bottom, mem, 0, 0, SRCCOPY);
            SelectObject(mem, old);
            DeleteObject(bmp);
            DeleteDC(mem);
            EndPaint(hDlg, &ps);
            return 0;
        }
        case WM_PRINTCLIENT: {
            paint_modal_dlg_bg(hDlg, reinterpret_cast<HDC>(wParam), md);
            return 0;
        }
        case WM_COMMAND: {
            int id = LOWORD(wParam);
            int code = HIWORD(wParam);
            if (!md) break;
            if (id == ID_WDLG_LANG && code == CBN_SELCHANGE) {
                int sel = (int)SendMessageW(md->hCmbLang, CB_GETCURSEL, 0, 0);
                if (sel >= 0 && (size_t)sel < md->lang_codes.size()) {
                    if (md->is_first_launch) sync_welcome_dlg_to_main_win(md);
                    heraw_i18n::I18nManager::instance().set_language(md->lang_codes[(size_t)sel]);
                    apply_ui_language_win();
                    save_ui_prefs();
                    populate_welcome_or_about_win(hDlg, md->is_first_launch);
                }
                return 0;
            }
            if (id == ID_WDLG_BATTERY && code == BN_CLICKED && md->hChkBattery && md->hCmbCores) {
                bool bat = get_chk(md->hChkBattery);
                int cur_c = (int)SendMessageW(md->hCmbCores, CB_GETCURSEL, 0, 0);
                if (bat && cur_c == 0) SendMessageW(md->hCmbCores, CB_SETCURSEL, 2, 0);
                else if (!bat && cur_c == 2) SendMessageW(md->hCmbCores, CB_SETCURSEL, 0, 0);
                InvalidateRect(md->hCmbCores, nullptr, FALSE);
                return 0;
            }
            if (id == ID_WDLG_BRAND) {
                ShellExecuteW(hDlg, L"open", L"https://github.com/A2Sumie/NijiN.LiteBurstSuite", nullptr, nullptr, SW_SHOWNORMAL);
                return 0;
            }
            if (id == ID_WDLG_CONFIRM || id == IDOK) {
                if (md->is_first_launch) {
                    sync_welcome_dlg_to_main_win(md);
                    if (!g_app.is_screenshot_mode) {
                        reg_write_str(L"FirstLaunchSetupDone", L"1");
                    }
                    sync_cross_tab_ui();
                    refresh_engine_status_label();
                    save_ui_prefs();
                }
                md->result_code = 1;
                DestroyWindow(hDlg);
                return 0;
            }
            if (id == ID_WDLG_CLOSE || id == IDCANCEL) {
                md->result_code = 0;
                DestroyWindow(hDlg);
                return 0;
            }
            if (id == ID_WDLG_PATHS) {
                sync_welcome_dlg_to_main_win(md);
                if (!g_app.is_screenshot_mode) {
                    reg_write_str(L"FirstLaunchSetupDone", L"1");
                }
                sync_cross_tab_ui();
                refresh_engine_status_label();
                save_ui_prefs();
                md->result_code = 2;
                DestroyWindow(hDlg);
                return 0;
            }
            if (id == ID_WDLG_REOPEN) {
                md->result_code = 3;
                DestroyWindow(hDlg);
                return 0;
            }
            break;
        }
        case WM_CLOSE:
            if (md) md->result_code = 0;
            DestroyWindow(hDlg);
            return 0;
    }
    return DefWindowProcW(hDlg, msg, wParam, lParam);
}

static void populate_welcome_or_about_win(HWND hDlg, bool is_first_launch) {
    auto* md = reinterpret_cast<ModalDlgData*>(GetWindowLongPtrW(hDlg, GWLP_USERDATA));
    if (!md) return;
    md->is_first_launch = is_first_launch;

    // Destroy existing child controls cleanly before rebuilding
    HWND hc = GetWindow(hDlg, GW_CHILD);
    while (hc) {
        HWND next = GetWindow(hc, GW_HWNDNEXT);
        RemovePropW(hc, L"NijiMuted");
        DestroyWindow(hc);
        hc = next;
    }
    md->hCmbLang = md->hCmbBackend = md->hCmbStream = md->hEdtMem = md->hCmbCores = nullptr;
    md->hChkBattery = md->hLblHint = md->hLblReady = md->hCredits = nullptr;
    md->lang_codes.clear();

    auto S = [](int px) { return g_app.S(px); };
    const int dW = 756;
    const int dH = is_first_launch ? 616 : 394;
    DWORD style = (DWORD)GetWindowLongPtrW(hDlg, GWL_STYLE);
    RECT adj{0, 0, S(dW), S(dH)};
    AdjustWindowRect(&adj, style, FALSE);
    SetWindowPos(hDlg, nullptr, 0, 0, adj.right - adj.left, adj.bottom - adj.top,
                 SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
    SetWindowTextW(hDlg, heraw_i18n::tr_w(is_first_launch ? "welcome.title" : "about.title").c_str());

    HINSTANCE hInst = GetModuleHandleW(nullptr);
    auto mk_static = [&](const std::wstring& txt, int x, int y, int w, int h, HFONT fn, bool muted = false, DWORD extra = 0) {
        HWND hw = CreateWindowExW(0, L"STATIC", txt.c_str(), WS_CHILD | WS_VISIBLE | SS_LEFT | SS_NOPREFIX | extra,
                                  S(x), S(y), S(w), S(h), hDlg, nullptr, hInst, nullptr);
        SendMessageW(hw, WM_SETFONT, reinterpret_cast<WPARAM>(fn), TRUE);
        if (muted) SetPropW(hw, L"NijiMuted", reinterpret_cast<HANDLE>(1));
        return hw;
    };
    auto mk_btn = [&](const std::wstring& txt, int x, int y, int w, int h, int id) {
        HWND hw = CreateWindowExW(0, L"BUTTON", txt.c_str(), WS_CHILD | WS_VISIBLE | BS_OWNERDRAW,
                                  S(x), S(y), S(w), S(h), hDlg, reinterpret_cast<HMENU>((INT_PTR)id), hInst, nullptr);
        SendMessageW(hw, WM_SETFONT, reinterpret_cast<WPARAM>(g_app.hFont), TRUE);
        SetWindowSubclass(hw, ModernBtnHoverSubclassProc, 3, 0);
        return hw;
    };
    auto mk_cmb = [&](int x, int y, int w, int drop_h, int id) {
        HWND hw = CreateWindowExW(0, L"COMBOBOX", L"", WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST | WS_VSCROLL,
                                  S(x), S(y), S(w), S(drop_h), hDlg, reinterpret_cast<HMENU>((INT_PTR)id), hInst, nullptr);
        SendMessageW(hw, WM_SETFONT, reinterpret_cast<WPARAM>(g_app.hFont), TRUE);
        SetWindowSubclass(hw, ModernComboSubclassProc, 2, 0);
        return hw;
    };

    // Banner Title (x = 16) + Clickable Co-Brand Mark right-aligned to card border (dW - 16)
    const int brandW = 176;
    mk_static(heraw_i18n::tr_w(is_first_launch ? "welcome.title" : "about.title"),
              16, 15, dW - 36 - brandW, 22, g_app.hFontCardHdr);
    mk_btn(L"", dW - 12 - brandW, 5, brandW, 44, ID_WDLG_BRAND);

    int credY = 64;
    if (is_first_launch) {
        // Row 0: Language Auto-Detected Selection
        mk_static(heraw_i18n::tr_w("welcome.lang_lbl"), 32, 80, 136, 20, g_app.hFontBold);
        md->hCmbLang = mk_cmb(172, 76, 214, 160, ID_WDLG_LANG);
        auto langs = heraw_i18n::I18nManager::instance().available_languages();
        std::string cur_code = heraw_i18n::I18nManager::instance().active_code();
        int sel_lang = 0;
        for (size_t li = 0; li < langs.size(); ++li) {
            md->lang_codes.push_back(langs[li].first);
            SendMessageW(md->hCmbLang, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(utf8_to_wide(langs[li].second).c_str()));
            if (_stricmp(langs[li].first.c_str(), cur_code.c_str()) == 0) sel_lang = (int)li;
        }
        SendMessageW(md->hCmbLang, CB_SETCURSEL, (WPARAM)sel_lang, 0);
        mk_static(heraw_i18n::tr_w("welcome.lang_note"), 32, 105, dW - 64, 18, g_app.hFontSmall, true);

        // Reassurance Sub-Box + Hardware/Decoder Readiness
        std::wstring cliDet = file_exists_regular(g_app.cli_path) ? g_app.cli_path : auto_detect_cli_exe(true);
        md->cli_ok = !cliDet.empty();
        md->hLblHint = mk_static(heraw_i18n::tr_w(md->cli_ok ? "welcome.hint_ok" : "welcome.hint_missing"),
                                 42, 139, dW - 84, 34, g_app.hFont);

        std::string gpuShort = (g_app.gpu_policy.has_gpu && g_app.gpu_policy.device_name[0])
                             ? std::string(g_app.gpu_policy.device_name) : "CPU AVX2";
        if (gpuShort.size() > 30) gpuShort = gpuShort.substr(0, 28) + "..";
        std::string hwBadge = wide_to_utf8(format_win_cpu_topology_badge()) + " · " + gpuShort;
        std::string cliShort = md->cli_ok ? wide_to_utf8(basename_of_w(cliDet)) : "missing";
        md->hLblReady = mk_static(
            md->cli_ok ? heraw_i18n::trf_w("welcome.hw_ready_fmt", hwBadge.c_str(), cliShort.c_str())
                       : heraw_i18n::trf_w("welcome.hw_miss_fmt", hwBadge.c_str()),
            32, 184, dW - 64, 34, g_app.hFontBold, false, 0);

        // Row 1: Compute Backend + Stripe Target
        mk_static(heraw_i18n::tr_w("welcome.backend_lbl"), 32, 229, 108, 20, g_app.hFont);
        md->hCmbBackend = mk_cmb(144, 225, 212, 140, ID_WDLG_BACKEND);
        for (const char* k : {"t1.bk_auto", "t1.bk_gpu_win", "t1.bk_cpu"}) {
            SendMessageW(md->hCmbBackend, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(heraw_i18n::tr_w(k).c_str()));
        }
        int cur_bk = g_app.hCmbBackend ? (int)SendMessageW(g_app.hCmbBackend, CB_GETCURSEL, 0, 0) : 0;
        SendMessageW(md->hCmbBackend, CB_SETCURSEL, (WPARAM)std::clamp(cur_bk, 0, 2), 0);

        mk_static(heraw_i18n::tr_w("welcome.stream_lbl"), 370, 229, 96, 20, g_app.hFont);
        md->hCmbStream = mk_cmb(468, 225, 124, 140, ID_WDLG_STREAM);
        for (const char* k : {"t1.stream_auto", "t1.stream_on", "t1.stream_off"}) {
            SendMessageW(md->hCmbStream, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(heraw_i18n::tr_w(k).c_str()));
        }
        int cur_st = g_app.hCmbStreaming ? (int)SendMessageW(g_app.hCmbStreaming, CB_GETCURSEL, 0, 0) : 0;
        SendMessageW(md->hCmbStream, CB_SETCURSEL, (WPARAM)std::clamp(cur_st, 0, 2), 0);

        std::wstring cur_mem = g_app.hEdtMemBudget ? get_wnd_text(g_app.hEdtMemBudget) : L"4";
        md->hEdtMem = CreateWindowExW(0, L"EDIT", cur_mem.c_str(), WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL | ES_NUMBER | ES_CENTER,
                                      S(600 + 5), S(226 + 3), S(48 - 10), S(24 - 6), hDlg,
                                      reinterpret_cast<HMENU>((INT_PTR)ID_WDLG_MEM), hInst, nullptr);
        SendMessageW(md->hEdtMem, WM_SETFONT, reinterpret_cast<WPARAM>(g_app.hFontMono), TRUE);

        // Row 2: CPU Core Policy + Paths Button
        mk_static(heraw_i18n::tr_w("welcome.cores_lbl"), 32, 263, 108, 20, g_app.hFont);
        md->hCmbCores = mk_cmb(144, 259, 236, 160, ID_WDLG_CORES);
        std::wstring cmb_lbls[5], mnu_lbls[5];
        build_win_cpu_core_labels(cmb_lbls, mnu_lbls);
        for (int i = 0; i < 5; ++i) {
            SendMessageW(md->hCmbCores, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(cmb_lbls[i].c_str()));
        }
        int cur_co = g_app.hCmbCpuCores ? (int)SendMessageW(g_app.hCmbCpuCores, CB_GETCURSEL, 0, 0) : 0;
        SendMessageW(md->hCmbCores, CB_SETCURSEL, (WPARAM)std::clamp(cur_co, 0, 4), 0);

        mk_btn(heraw_i18n::tr_w("welcome.btn_locate_cli"), dW - 32 - 174, 259, 174, 26, ID_WDLG_PATHS);

        // Row 3: Laptop / Battery Efficiency Mode
        md->hChkBattery = CreateWindowExW(0, L"BUTTON", heraw_i18n::tr_w("welcome.battery_chk").c_str(),
                                          WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX,
                                          S(32), S(296), S(dW - 64), S(22), hDlg,
                                          reinterpret_cast<HMENU>((INT_PTR)ID_WDLG_BATTERY), hInst, nullptr);
        SendMessageW(md->hChkBattery, WM_SETFONT, reinterpret_cast<WPARAM>(g_app.hFont), TRUE);
        set_chk_val(md->hChkBattery, g_app.is_battery_device);
        attach_modern_toggle(md->hChkBattery, false, false, kColCard);

        credY = 342;
        mk_btn(heraw_i18n::tr_w("welcome.btn_confirm"), dW - 16 - 196, dH - 48, 196, 34, ID_WDLG_CONFIRM);
    } else {
        credY = 64;
        mk_static(heraw_i18n::tr_w("welcome.lang_lbl"), 32, 294, 136, 20, g_app.hFontBold);
        md->hCmbLang = mk_cmb(172, 290, 214, 160, ID_WDLG_LANG);
        auto langs = heraw_i18n::I18nManager::instance().available_languages();
        std::string cur_code = heraw_i18n::I18nManager::instance().active_code();
        int sel_lang = 0;
        for (size_t li = 0; li < langs.size(); ++li) {
            md->lang_codes.push_back(langs[li].first);
            SendMessageW(md->hCmbLang, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(utf8_to_wide(langs[li].second).c_str()));
            if (_stricmp(langs[li].first.c_str(), cur_code.c_str()) == 0) sel_lang = (int)li;
        }
        SendMessageW(md->hCmbLang, CB_SETCURSEL, (WPARAM)sel_lang, 0);
        mk_static(heraw_i18n::tr_w("welcome.lang_note"), 398, 295, dW - 428, 18, g_app.hFontSmall, true);

        mk_btn(heraw_i18n::tr_w("menu.welcome_setup"), 16, dH - 48 + 2, 296, 30, ID_WDLG_REOPEN);
        mk_btn(heraw_i18n::tr_w("about.btn_close"), dW - 16 - 112, dH - 48, 112, 34, ID_WDLG_CLOSE);
    }

    // Version + Licenses + Attribution Hyperlink View
    auto* cd = new CreditsViewData{};
    cd->is_english = heraw_i18n::I18nManager::instance().is_english();
    md->hCredits = CreateWindowExW(0, L"NijiCreditsView", L"", WS_CHILD | WS_VISIBLE,
                                   S(32), S(credY + 14), S(dW - 64), S(180), hDlg,
                                   reinterpret_cast<HMENU>((INT_PTR)ID_WDLG_CREDITS), hInst, nullptr);
    SetWindowLongPtrW(md->hCredits, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(cd));
    {
        HDC hdcTmp = GetDC(md->hCredits);
        layout_and_paint_credits(hdcTmp, S(dW - 64), cd, false);
        ReleaseDC(md->hCredits, hdcTmp);
    }

    InvalidateRect(hDlg, nullptr, TRUE);
}

static HWND build_welcome_or_about_win(HWND owner, bool is_first_launch, ModalDlgData* md_storage) {
    auto S = [](int px) { return g_app.S(px); };
    const int dW = 756;
    const int dH = is_first_launch ? 616 : 394;
    DWORD style = WS_POPUP | WS_CAPTION | WS_SYSMENU;
    RECT rc{0, 0, S(dW), S(dH)};
    AdjustWindowRect(&rc, style, FALSE);
    int ww = rc.right - rc.left;
    int wh = rc.bottom - rc.top;

    int x = CW_USEDEFAULT, y = CW_USEDEFAULT;
    if (owner && IsWindow(owner)) {
        RECT orc{};
        GetWindowRect(owner, &orc);
        x = orc.left + ((orc.right - orc.left) - ww) / 2;
        y = orc.top + ((orc.bottom - orc.top) - wh) / 2;
    } else {
        RECT wa{};
        if (SystemParametersInfoW(SPI_GETWORKAREA, 0, &wa, 0)) {
            x = wa.left + ((wa.right - wa.left) - ww) / 2;
            y = wa.top + ((wa.bottom - wa.top) - wh) / 2;
        }
    }

    HINSTANCE hInst = GetModuleHandleW(nullptr);
    HWND hDlg = CreateWindowExW(
        WS_EX_DLGMODALFRAME, L"NijiModalDlg",
        heraw_i18n::tr_w(is_first_launch ? "welcome.title" : "about.title").c_str(),
        style, x, y, ww, wh, owner, nullptr, hInst, nullptr
    );
    if (!hDlg) return nullptr;
    SetWindowLongPtrW(hDlg, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(md_storage));
    populate_welcome_or_about_win(hDlg, is_first_launch);
    return hDlg;
}

static void show_welcome_or_about_win(HWND owner, bool is_first_launch) {
    bool next_first = is_first_launch;
    for (;;) {
        ModalDlgData md{};
        HWND hDlg = build_welcome_or_about_win(owner, next_first, &md);
        if (!hDlg) return;
        if (owner) EnableWindow(owner, FALSE);
        ShowWindow(hDlg, SW_SHOW);
        UpdateWindow(hDlg);
        SetFocus(hDlg);

        MSG m{};
        while (IsWindow(hDlg) && GetMessageW(&m, nullptr, 0, 0) > 0) {
            if (m.message == WM_KEYDOWN && m.wParam == VK_ESCAPE) {
                PostMessageW(hDlg, WM_CLOSE, 0, 0);
                continue;
            }
            if (m.message == WM_KEYDOWN && m.wParam == VK_RETURN) {
                PostMessageW(hDlg, WM_COMMAND, MAKEWPARAM(next_first ? ID_WDLG_CONFIRM : ID_WDLG_CLOSE, BN_CLICKED), 0);
                continue;
            }
            if (m.message == WM_QUIT) {
                if (owner) EnableWindow(owner, TRUE);
                DestroyWindow(hDlg);
                PostQuitMessage((int)m.wParam);
                return;
            }
            TranslateMessage(&m);
            DispatchMessageW(&m);
        }

        if (owner) {
            EnableWindow(owner, TRUE);
            SetForegroundWindow(owner);
        }
        if (md.result_code == 2 && owner) {
            show_engine_popup_menu(owner, g_app.hBtnPerfHeader, true);
            return;
        }
        if (md.result_code == 3) {
            next_first = true;
            continue;
        }
        return;
    }
}

struct IntakeDlgData {
    bool applied = false;
    HWND hCmbDec = nullptr;
    HWND hEdtNev = nullptr;
    HWND hEdtGap = nullptr;
    HWND hEdtSharp = nullptr;
    HWND hEdtShift = nullptr;
    HWND hCmbLucky = nullptr;
    HWND hEdtCluster = nullptr;
};

static void paint_intake_dlg_bg(HWND hDlg, HDC hdc) {
    RECT crc{};
    GetClientRect(hDlg, &crc);
    FillRect(hdc, &crc, g_app.hBrCanvas);
    auto S = [](int px) { return g_app.S(px); };
    int cw = crc.right;

    RECT hdr{0, 0, cw, S(48)};
    FillRect(hdc, &hdr, g_app.hBrCard);
    HBRUSH brAcc = CreateSolidBrush(kColAccent);
    RECT topStripe{0, 0, cw, S(3)};
    FillRect(hdc, &topStripe, brAcc);
    DeleteObject(brAcc);
    HBRUSH brBd = CreateSolidBrush(kColCardBorder);
    RECT hdrLine{0, S(47), cw, S(48)};
    FillRect(hdc, &hdrLine, brBd);
    DeleteObject(brBd);

    Gdiplus::Graphics g(hdc);
    g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
    gdip_round_rect(g, (float)S(16), (float)S(60), (float)(cw - S(32)), (float)S(246),
                    (float)S(10), gdip_col(kColCard), gdip_col(kColCardBorder), 1.15f);

    HWND hChild = GetWindow(hDlg, GW_CHILD);
    while (hChild) {
        if ((GetWindowLongPtrW(hChild, GWL_STYLE) & WS_VISIBLE) != 0) {
            wchar_t cls[32] = {0};
            GetClassNameW(hChild, cls, 31);
            if (_wcsicmp(cls, L"Edit") == 0) {
                RECT wr{};
                GetWindowRect(hChild, &wr);
                MapWindowPoints(nullptr, hDlg, reinterpret_cast<LPPOINT>(&wr), 2);
                float padX = (float)S(5), padY = (float)S(3);
                gdip_round_rect(g,
                    (float)wr.left - padX, (float)wr.top - padY,
                    (float)(wr.right - wr.left) + 2.0f * padX,
                    (float)(wr.bottom - wr.top) + 2.0f * padY,
                    (float)S(6), gdip_col(RGB(255, 255, 255)), gdip_col(kColInputEdge), 1.15f);
            }
        }
        hChild = GetWindow(hChild, GW_HWNDNEXT);
    }
}

static LRESULT CALLBACK NijiIntakeDlgWndProc(HWND hDlg, UINT msg, WPARAM wParam, LPARAM lParam) {
    auto* idd = reinterpret_cast<IntakeDlgData*>(GetWindowLongPtrW(hDlg, GWLP_USERDATA));
    switch (msg) {
        case WM_ERASEBKGND:
            return 1;
        case WM_DRAWITEM: {
            const auto* dis = reinterpret_cast<const DRAWITEMSTRUCT*>(lParam);
            if (dis && dis->CtlType == ODT_BUTTON) {
                draw_modern_pill_button(dis);
                return TRUE;
            }
            break;
        }
        case WM_CTLCOLORSTATIC: {
            HDC hdc = reinterpret_cast<HDC>(wParam);
            SetBkColor(hdc, kColCard);
            SetTextColor(hdc, kColTextMain);
            return reinterpret_cast<LRESULT>(g_app.hBrCard);
        }
        case WM_CTLCOLOREDIT: {
            HDC hdc = reinterpret_cast<HDC>(wParam);
            SetBkColor(hdc, RGB(255, 255, 255));
            SetTextColor(hdc, kColTextMain);
            return reinterpret_cast<LRESULT>(g_app.hBrWhite);
        }
        case WM_PAINT: {
            PAINTSTRUCT ps{};
            HDC hdc = BeginPaint(hDlg, &ps);
            RECT crc{};
            GetClientRect(hDlg, &crc);
            HDC mem = CreateCompatibleDC(hdc);
            HBITMAP bmp = CreateCompatibleBitmap(hdc, std::max(1L, crc.right), std::max(1L, crc.bottom));
            HGDIOBJ old = SelectObject(mem, bmp);
            paint_intake_dlg_bg(hDlg, mem);
            BitBlt(hdc, 0, 0, crc.right, crc.bottom, mem, 0, 0, SRCCOPY);
            SelectObject(mem, old);
            DeleteObject(bmp);
            DeleteDC(mem);
            EndPaint(hDlg, &ps);
            return 0;
        }
        case WM_PRINTCLIENT:
            paint_intake_dlg_bg(hDlg, reinterpret_cast<HDC>(wParam));
            return 0;
        case WM_COMMAND: {
            int id = LOWORD(wParam);
            if (!idd) break;
            if (id == ID_IDLG_RESET) {
                SendMessageW(idd->hCmbDec,   CB_SETCURSEL, 0, 0);
                SetWindowTextW(idd->hEdtNev,     L"all");
                SetWindowTextW(idd->hEdtGap,     L"3.5");
                SetWindowTextW(idd->hEdtSharp,   L"0.72");
                SetWindowTextW(idd->hEdtShift,   L"120");
                SendMessageW(idd->hCmbLucky, CB_SETCURSEL, 0, 0);
                SetWindowTextW(idd->hEdtCluster, L"128");
                InvalidateRect(hDlg, nullptr, FALSE);
                return 0;
            }
            if (id == ID_IDLG_APPLY || id == IDOK) {
                g_app.decoder_mode = selected_cmb_code(idd->hCmbDec, kDecoderCodes, L"auto");
                std::wstring nv = trim_ws(get_wnd_text(idd->hEdtNev));
                g_app.nev_frames_spec = nv.empty() ? L"all" : nv;
                std::wstring gp = trim_ws(get_wnd_text(idd->hEdtGap));
                g_app.burst_gap_s = gp.empty() ? L"3.5" : gp;
                std::wstring sh = trim_ws(get_wnd_text(idd->hEdtSharp));
                g_app.min_rel_sharpness = sh.empty() ? L"0.72" : sh;
                std::wstring sf = trim_ws(get_wnd_text(idd->hEdtShift));
                g_app.max_shift_px = sf.empty() ? L"120" : sf;
                g_app.lucky_mode = selected_cmb_code(idd->hCmbLucky, kLuckyCodes, L"auto");
                std::wstring mc = trim_ws(get_wnd_text(idd->hEdtCluster));
                g_app.max_cluster_frames = mc.empty() ? L"128" : mc;
                idd->applied = true;
                DestroyWindow(hDlg);
                return 0;
            }
            if (id == ID_IDLG_CANCEL || id == IDCANCEL) {
                idd->applied = false;
                DestroyWindow(hDlg);
                return 0;
            }
            break;
        }
        case WM_CLOSE:
            if (idd) idd->applied = false;
            DestroyWindow(hDlg);
            return 0;
    }
    return DefWindowProcW(hDlg, msg, wParam, lParam);
}

static void show_intake_params_win(HWND owner) {
    auto S = [](int px) { return g_app.S(px); };
    const int dW = 540, dH = 360;
    DWORD style = WS_POPUP | WS_CAPTION | WS_SYSMENU;
    RECT rc{0, 0, S(dW), S(dH)};
    AdjustWindowRect(&rc, style, FALSE);
    int ww = rc.right - rc.left, wh = rc.bottom - rc.top;
    int x = CW_USEDEFAULT, y = CW_USEDEFAULT;
    if (owner && IsWindow(owner)) {
        RECT orc{};
        GetWindowRect(owner, &orc);
        x = orc.left + ((orc.right - orc.left) - ww) / 2;
        y = orc.top + ((orc.bottom - orc.top) - wh) / 2;
    }
    HINSTANCE hInst = GetModuleHandleW(nullptr);
    IntakeDlgData idd{};
    HWND hDlg = CreateWindowExW(
        WS_EX_DLGMODALFRAME, L"NijiIntakeDlg",
        heraw_i18n::tr_w("intake.title").c_str(),
        style, x, y, ww, wh, owner, nullptr, hInst, nullptr
    );
    if (!hDlg) return;
    SetWindowLongPtrW(hDlg, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(&idd));

    auto mk_lbl = [&](const char* k, int lx, int ly, int lw) {
        HWND hw = CreateWindowExW(0, L"STATIC", heraw_i18n::tr_w(k).c_str(),
            WS_CHILD | WS_VISIBLE | SS_LEFT | SS_NOPREFIX, S(lx), S(ly), S(lw), S(20), hDlg, nullptr, hInst, nullptr);
        SendMessageW(hw, WM_SETFONT, reinterpret_cast<WPARAM>(g_app.hFont), TRUE);
        return hw;
    };
    auto mk_edt = [&](const std::wstring& v, int ex, int ey, int ew, int id) {
        HWND hw = CreateWindowExW(0, L"EDIT", v.c_str(), WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL,
            S(ex + 5), S(ey + 3), S(ew - 10), S(24 - 6), hDlg, reinterpret_cast<HMENU>((INT_PTR)id), hInst, nullptr);
        SendMessageW(hw, WM_SETFONT, reinterpret_cast<WPARAM>(g_app.hFontMono), TRUE);
        return hw;
    };
    auto mk_cmb = [&](int cx, int cy, int cw, int id) {
        HWND hw = CreateWindowExW(0, L"COMBOBOX", L"", WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST | WS_VSCROLL,
            S(cx), S(cy), S(cw), S(140), hDlg, reinterpret_cast<HMENU>((INT_PTR)id), hInst, nullptr);
        SendMessageW(hw, WM_SETFONT, reinterpret_cast<WPARAM>(g_app.hFont), TRUE);
        SetWindowSubclass(hw, ModernComboSubclassProc, 2, 0);
        return hw;
    };
    auto mk_btn = [&](const char* k, int bx, int by, int bw, int bh, int id) {
        HWND hw = CreateWindowExW(0, L"BUTTON", heraw_i18n::tr_w(k).c_str(), WS_CHILD | WS_VISIBLE | BS_OWNERDRAW,
            S(bx), S(by), S(bw), S(bh), hDlg, reinterpret_cast<HMENU>((INT_PTR)id), hInst, nullptr);
        SendMessageW(hw, WM_SETFONT, reinterpret_cast<WPARAM>(g_app.hFont), TRUE);
        SetWindowSubclass(hw, ModernBtnHoverSubclassProc, 3, 0);
        return hw;
    };

    HWND hHdr = CreateWindowExW(0, L"STATIC", heraw_i18n::tr_w("intake.title").c_str(),
        WS_CHILD | WS_VISIBLE | SS_LEFT | SS_NOPREFIX, S(16), S(13), S(dW - 32), S(22), hDlg, nullptr, hInst, nullptr);
    SendMessageW(hHdr, WM_SETFONT, reinterpret_cast<WPARAM>(g_app.hFontCardHdr), TRUE);

    mk_lbl("intake.decoder_lbl", 30, 76, 268);
    idd.hCmbDec = mk_cmb(304, 72, 206, ID_IDLG_DECODER);
    for (const char* k : {"intake.dec_auto", "intake.dec_wvdc", "intake.dec_libraw"}) {
        SendMessageW(idd.hCmbDec, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(heraw_i18n::tr_w(k).c_str()));
    }
    select_cmb_code(idd.hCmbDec, kDecoderCodes, g_app.decoder_mode);

    mk_lbl("intake.nev_lbl", 30, 109, 268);
    idd.hEdtNev = mk_edt(g_app.nev_frames_spec, 304, 106, 206, ID_IDLG_NEV);

    mk_lbl("intake.gap_lbl", 30, 142, 268);
    idd.hEdtGap = mk_edt(g_app.burst_gap_s, 304, 139, 206, ID_IDLG_GAP);

    mk_lbl("intake.sharp_lbl", 30, 175, 268);
    idd.hEdtSharp = mk_edt(g_app.min_rel_sharpness, 304, 172, 206, ID_IDLG_SHARP);

    mk_lbl("intake.shift_lbl", 30, 208, 268);
    idd.hEdtShift = mk_edt(g_app.max_shift_px, 304, 205, 206, ID_IDLG_SHIFT);

    mk_lbl("intake.lucky_lbl", 30, 241, 268);
    idd.hCmbLucky = mk_cmb(304, 237, 206, ID_IDLG_LUCKY);
    for (const char* k : {"intake.lucky_auto", "intake.lucky_on", "intake.lucky_off"}) {
        SendMessageW(idd.hCmbLucky, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(heraw_i18n::tr_w(k).c_str()));
    }
    select_cmb_code(idd.hCmbLucky, kLuckyCodes, g_app.lucky_mode);

    mk_lbl("intake.cluster_lbl", 30, 274, 268);
    idd.hEdtCluster = mk_edt(g_app.max_cluster_frames, 304, 271, 206, ID_IDLG_CLUSTER);

    mk_btn("intake.reset",  16,  dH - 42, 108, 30, ID_IDLG_RESET);
    mk_btn("dlg.cancel_btn", dW - 16 - 128 - 8 - 88, dH - 42, 88, 30, ID_IDLG_CANCEL);
    mk_btn("intake.apply",  dW - 16 - 128, dH - 42, 128, 30, ID_IDLG_APPLY);

    if (owner) EnableWindow(owner, FALSE);
    ShowWindow(hDlg, SW_SHOW);
    UpdateWindow(hDlg);
    SetFocus(hDlg);

    MSG m{};
    while (IsWindow(hDlg) && GetMessageW(&m, nullptr, 0, 0) > 0) {
        if (m.message == WM_KEYDOWN && m.wParam == VK_ESCAPE) {
            PostMessageW(hDlg, WM_CLOSE, 0, 0);
            continue;
        }
        if (m.message == WM_KEYDOWN && m.wParam == VK_RETURN) {
            PostMessageW(hDlg, WM_COMMAND, MAKEWPARAM(ID_IDLG_APPLY, BN_CLICKED), 0);
            continue;
        }
        if (m.message == WM_QUIT) {
            if (owner) EnableWindow(owner, TRUE);
            DestroyWindow(hDlg);
            PostQuitMessage((int)m.wParam);
            return;
        }
        TranslateMessage(&m);
        DispatchMessageW(&m);
    }

    if (owner) {
        EnableWindow(owner, TRUE);
        SetForegroundWindow(owner);
    }
    if (idd.applied) {
        save_ui_prefs();
        sync_cross_tab_ui();
        if (!g_app.inputs.empty()) trigger_async_plan_scan();
    }
}

// ============================================================================
// Main Window Procedure
// ============================================================================
static LRESULT CALLBACK MainWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
        case WM_CREATE: {
            g_app.hwnd = hwnd;
            DragAcceptFiles(hwnd, TRUE);
            HDC hdc = GetDC(hwnd);
            g_app.dpi = GetDeviceCaps(hdc, LOGPIXELSY);
            ReleaseDC(hwnd, hdc);
            auto S = [](int px) { return g_app.S(px); };

            g_app.hBrCanvas       = CreateSolidBrush(kColCanvas);
            g_app.hBrCard         = CreateSolidBrush(kColCard);
            g_app.hBrCardSub      = CreateSolidBrush(kColCardSub);
            g_app.hBrWhite        = CreateSolidBrush(RGB(255, 255, 255));
            g_app.hBrDisabledEdit = CreateSolidBrush(kColDisabledFill);

            g_app.brandGx    = load_cropped_png_from_mem(heraw_brand::kGxStarPng2x, sizeof(heraw_brand::kGxStarPng2x));
            g_app.brandNijin = load_cropped_png_from_mem(heraw_brand::kNijinLockupPng2x, sizeof(heraw_brand::kNijinLockupPng2x));
            g_app.hBmpGxFallback    = create_premul_dib(heraw_brand::kGx1xW, heraw_brand::kGx1xH, heraw_brand::kGxStarPremulBGRA1x);
            g_app.hBmpNijinFallback = create_premul_dib(heraw_brand::kNijin1xW, heraw_brand::kNijin1xH, heraw_brand::kNijinPremulBGRA1x);
            (void)heraw_brand::kAppIconPng;

            g_app.hFont = CreateFontW(-S(12), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                DEFAULT_PITCH | FF_DONTCARE, L"Microsoft YaHei UI");
            g_app.hFontSmall = CreateFontW(-S(11), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                DEFAULT_PITCH | FF_DONTCARE, L"Microsoft YaHei UI");
            g_app.hFontBold = CreateFontW(-S(12), 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE,
                DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                DEFAULT_PITCH | FF_DONTCARE, L"Microsoft YaHei UI");
            g_app.hFontCardHdr = CreateFontW(-S(13), 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
                DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                DEFAULT_PITCH | FF_DONTCARE, L"Microsoft YaHei UI");
            g_app.hFontTitle = CreateFontW(-S(17), 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
                DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
            g_app.hFontMajorVer = CreateFontW(-S(20), 0, 0, 0, FW_HEAVY, FALSE, FALSE, FALSE,
                DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
            g_app.hFontVerTag = CreateFontW(-S(12), 0, 0, 0, FW_MEDIUM, FALSE, FALSE, FALSE,
                DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
            g_app.hFontLink = CreateFontW(-S(12), 0, 0, 0, FW_NORMAL, FALSE, TRUE, FALSE,
                DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                DEFAULT_PITCH | FF_DONTCARE, L"Microsoft YaHei UI");
            g_app.hFontLinkHdr = CreateFontW(-S(13), 0, 0, 0, FW_BOLD, FALSE, TRUE, FALSE,
                DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
            g_app.hFontBadge = CreateFontW(-S(11), 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
                DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
            g_app.hFontMono = CreateFontW(-S(11), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                FIXED_PITCH | FF_MODERN, L"Consolas");

            HINSTANCE hInst = GetModuleHandleW(nullptr);
            auto reg_ctrl = [&](HWND hw, int tab, int col_mode, RECT design_rc, const char* key = nullptr) {
                g_app.tab_ctrls.push_back(TaggedCtrl{hw, tab, col_mode, design_rc, key});
                if (tab > 0 || tab == -99) ShowWindow(hw, SW_HIDE);
                return hw;
            };
            auto infer_col = [](int tab, int x) -> int {
                if (tab == -99) return -1;
                if (tab >= 0) return 1;
                if (x >= 1180) return 2;
                if (x >= 420) return 1;
                return 0;
            };
            auto mk = [&](int tab, const wchar_t* cls, const wchar_t* txt, DWORD st, int x, int y, int w, int h, int id, const char* key = nullptr, int col_ovr = -2) {
                DWORD vis = (tab == -99) ? 0 : WS_VISIBLE;
                HWND hw = CreateWindowExW(0, cls, txt, WS_CHILD | vis | st,
                    S(x), S(y), S(w), S(h), hwnd, reinterpret_cast<HMENU>((INT_PTR)id), hInst, nullptr);
                SendMessageW(hw, WM_SETFONT, reinterpret_cast<WPARAM>(g_app.hFont), TRUE);
                int cm = (col_ovr != -2) ? col_ovr : infer_col(tab, x);
                return reg_ctrl(hw, tab, cm, RECT{S(x), S(y), S(x + w), S(y + h)}, key);
            };
            auto mk_lbl = [&](int tab, const wchar_t* txt, int x, int y, int w, int h = 20, int id = 0, const char* key = nullptr) {
                return mk(tab, L"STATIC", txt, SS_LEFT | SS_NOPREFIX, x, y, w, h, id, key);
            };
            auto mk_hdr = [&](int tab, const wchar_t* txt, int x, int y, int w, int id = 0, const char* key = nullptr) {
                HWND hw = mk(tab, L"STATIC", txt, SS_LEFT | SS_NOPREFIX, x, y, w, 22, id, key);
                SendMessageW(hw, WM_SETFONT, reinterpret_cast<WPARAM>(g_app.hFontCardHdr), TRUE);
                return hw;
            };
            auto mk_btn = [&](int tab, const wchar_t* txt, int x, int y, int w, int h, int id, bool portal = false, const char* key = nullptr, int col_ovr = -2) {
                HWND hw = mk(tab, L"BUTTON", txt, BS_OWNERDRAW, x, y, w, h, id, key, col_ovr);
                if (portal) SetPropW(hw, L"NijiPortal", reinterpret_cast<HANDLE>(1));
                SetWindowSubclass(hw, ModernBtnHoverSubclassProc, 3, 0);
                return hw;
            };
            auto mk_chk = [&](int tab, const wchar_t* txt, int x, int y, int w, int id, bool checked = false, COLORREF bg = kColCard, const char* key = nullptr, int h = 22) {
                HWND hw = mk(tab, L"BUTTON", txt, BS_AUTOCHECKBOX, x, y, w, h, id, key);
                if (checked) SendMessageW(hw, BM_SETCHECK, BST_CHECKED, 0);
                attach_modern_toggle(hw, false, false, bg);
                return hw;
            };
            auto mk_rad = [&](int tab, const wchar_t* txt, int x, int y, int w, int id, bool group_start, bool bold = false, const char* key = nullptr) {
                HWND hw = mk(tab, L"BUTTON", txt, BS_AUTORADIOBUTTON | (group_start ? WS_GROUP : 0), x, y, w, 24, id, key);
                attach_modern_toggle(hw, true, bold, kColCard);
                return hw;
            };
            auto mk_edt = [&](int tab, const wchar_t* txt, int x, int y, int w, int h, int id, DWORD extra = 0) {
                return mk(tab, L"EDIT", txt, ES_AUTOHSCROLL | extra, x + 5, y + 3, w - 10, h - 6, id);
            };
            auto mk_cmb = [&](int tab, int x, int y, int w, int drop_h, int id) {
                HWND hw = mk(tab, L"COMBOBOX", L"", CBS_DROPDOWNLIST | WS_VSCROLL, x, y, w, drop_h, id);
                SetWindowSubclass(hw, ModernComboSubclassProc, 2, 0);
                return hw;
            };
            auto mk_seg = [&](int tab, const std::vector<std::wstring>& items, int x, int y, int w, int h, int id, bool is_tab = false) {
                HWND hw = CreateWindowExW(0, L"NijiSegPill", L"", WS_CHILD | WS_VISIBLE,
                    S(x), S(y), S(w), S(h), hwnd, reinterpret_cast<HMENU>((INT_PTR)id), hInst, nullptr);
                auto* sd = new SegPillData{};
                sd->items = items;
                sd->sel = 0;
                sd->is_tab_bar = is_tab;
                SetWindowLongPtrW(hw, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(sd));
                return reg_ctrl(hw, tab, infer_col(tab, x), RECT{S(x), S(y), S(x + w), S(y + h)});
            };
            auto mk_sld = [&](int tab, double min_v, double max_v, double init_v, int dec, HWND linked_edt, int x, int y, int w, int h, int id) {
                HWND hw = CreateWindowExW(0, L"NijiAquaSlider", L"", WS_CHILD | WS_VISIBLE,
                    S(x), S(y), S(w), S(h), hwnd, reinterpret_cast<HMENU>((INT_PTR)id), hInst, nullptr);
                auto* ad = new AquaSliderData{};
                ad->min_v = min_v;
                ad->max_v = max_v;
                ad->val = init_v;
                ad->decimals = dec;
                ad->linked_edt = linked_edt;
                SetWindowLongPtrW(hw, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(ad));
                return reg_ctrl(hw, tab, infer_col(tab, x), RECT{S(x), S(y), S(x + w), S(y + h)});
            };

            // ================================================================
            // Column 1: Persistent Left Master-Detail Dock (x = 18..414, inner = 30..402)
            // ================================================================
            g_app.hBtnPerfHeader  = mk_btn(-1, L"性能: GPU优先 · 智能异构 ▾", 782, 15, 194, 26, ID_BTN_PERF_HEADER, true, nullptr, 0);
            g_app.hBtnBrandHeader = mk_btn(-1, L"", 986, 7, 176, 44, ID_BTN_BRAND_HEADER, false, nullptr, 0);
            mk_hdr(-1, L"连拍队列", 30, 78, 126, 0, "dock.queue_hdr");
            g_app.hChkSortTime  = mk_chk(-1, L"EXIF时间排序", 162, 78, 124, ID_CHK_SORT_TIME, true, kColCard, "dock.sort_time");
            g_app.hChkRecursive = mk_chk(-1, L"递归子目录",   290, 78, 112, ID_CHK_RECURSIVE, true, kColCard, "dock.recursive");
            g_app.hCmbGrouping = mk_cmb(-1, 30, 108, 106, 140, ID_CMB_GROUPING);
            SendMessageW(g_app.hCmbGrouping, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"自动分组"));
            SendMessageW(g_app.hCmbGrouping, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"整组处理"));
            SendMessageW(g_app.hCmbGrouping, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"每簇精选"));
            SendMessageW(g_app.hCmbGrouping, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"全库挑1组"));
            SendMessageW(g_app.hCmbGrouping, CB_SETCURSEL, 0, 0);
            mk_lbl(-1, L"目标张数", 140, 112, 52, 20, 0, "dock.grp_sz_cap");
            g_app.hEdtGrpSz        = mk_edt(-1, L"8", 194, 108, 40, 24, ID_EDT_GROUP_SIZE, ES_NUMBER);
            g_app.hBtnRegroup      = mk_btn(-1, L"重新分组",   240, 106, 76, 28, ID_BTN_REGROUP,       false, "dock.regroup",    0);
            g_app.hBtnIntakeParams = mk_btn(-1, L"分簇调参...", 320, 106, 82, 28, ID_BTN_INTAKE_PARAMS, false, nullptr,           0);
            g_app.hLblGroupingHint = mk_lbl(-1, L"导入后自动剖析连拍簇（支持 Ctrl/Shift 多选、空格批勾、合并与拆分）", 30, 140, 372, 34);
            SendMessageW(g_app.hLblGroupingHint, WM_SETFONT, reinterpret_cast<WPARAM>(g_app.hFontSmall), TRUE);
            g_app.hBtnIntakeAction = mk_btn(-1, L"✦ 仅勾最优1组", 286, 145, 116, 24, ID_BTN_INTAKE_ACTION, true, nullptr, 0);
            ShowWindow(g_app.hBtnIntakeAction, SW_HIDE);

            g_app.hBurstView = CreateWindowExW(0, L"NijiBurstQueue", L"", WS_CHILD | WS_VISIBLE,
                S(30), S(178), S(372), S(232), hwnd, reinterpret_cast<HMENU>((INT_PTR)ID_VIEW_BURST_QUEUE), hInst, nullptr);

            g_app.hBtnAddFiles    = mk_btn(-1, L"+文件",      30,  418, 56, 28, ID_BTN_ADD_FILES,    false, "dock.btn_add_files", 0);
            g_app.hBtnAddFolder   = mk_btn(-1, L"+目录",      90,  418, 56, 28, ID_BTN_ADD_FOLDER,   false, "dock.btn_add_dir",   0);
            g_app.hBtnMergeGroups = mk_btn(-1, L"合并组",     150, 418, 58, 28, ID_BTN_MERGE_GROUPS, false, "dock.btn_merge",     0);
            g_app.hBtnCleanStray  = mk_btn(-1, L"清散片",     212, 418, 58, 28, ID_BTN_CLEAN_STRAY,  false, "dock.btn_clean",     0);
            g_app.hBtnExportTopK  = mk_btn(-1, L"Top-K选片",  274, 418, 74, 28, ID_BTN_EXPORT_TOPK,  false, "dock.btn_topk",      0);
            g_app.hBtnClear       = mk_btn(-1, L"清空",       352, 418, 50, 28, ID_BTN_CLEAR,        false, "dock.btn_clear",     0);

            g_app.hLblFrameHdr    = mk_hdr(-1, L"组内单帧检视", 30, 478, 194, 0, "dock.insp_hdr");
            g_app.hBtnSplitFrames = mk_btn(-1, L"拆为新组", 228, 476, 82, 26, ID_BTN_SPLIT_FRAMES, false, "dock.btn_split",    0);
            g_app.hBtnLockFrame   = mk_btn(-1, L"定格此帧", 314, 476, 88, 26, ID_BTN_LOCK_FRAME,   false, "dock.btn_lock_win", 0);

            g_app.hFrameView = CreateWindowExW(0, L"NijiFrameInsp", L"", WS_CHILD | WS_VISIBLE,
                S(30), S(508), S(372), S(320), hwnd, reinterpret_cast<HMENU>((INT_PTR)ID_VIEW_FRAME_INSP), hInst, nullptr);

            // ================================================================
            // Column 3: Full-Height Persistent Right Dock (R1 / R2 / R3, design x = 1186..1522, y = 12..840)
            // ================================================================
            mk_hdr(-1, L"当前组与物理门限护照", 1200, 22, 308, 0, "r1.title");
            g_app.hLblR1GroupSummary = mk_lbl(-1, L"", 1200, 46, 308, 18);
            SendMessageW(g_app.hLblR1GroupSummary, WM_SETFONT, reinterpret_cast<WPARAM>(g_app.hFontBold), TRUE);
            g_app.hLblR1GateSr  = mk_lbl(-1, L"", 1200, 67, 308, 18);
            g_app.hLblR1GatePar = mk_lbl(-1, L"", 1200, 87, 308, 18);
            SendMessageW(g_app.hLblR1GateSr,  WM_SETFONT, reinterpret_cast<WPARAM>(g_app.hFontSmall), TRUE);
            SendMessageW(g_app.hLblR1GatePar, WM_SETFONT, reinterpret_cast<WPARAM>(g_app.hFontSmall), TRUE);
            g_app.hLblEngineStatus = mk(-1, L"STATIC", L"引擎: .\\nijinLBS.exe",
                SS_LEFT | SS_NOPREFIX | SS_ENDELLIPSIS, 1200, 120, 198, 18, ID_LBL_ENGINE_STATUS, nullptr, 2);
            SendMessageW(g_app.hLblEngineStatus, WM_SETFONT, reinterpret_cast<WPARAM>(g_app.hFontSmall), TRUE);
            g_app.hBtnEngineMenu = mk_btn(-1, L"性能与引擎...", 1402, 116, 106, 25, ID_BTN_ENGINE_MENU, false, "t1.engine_btn", 2);

            mk_hdr(-1, L"组专属 NOPB/1 切面总线", 1200, 170, 308, 0, "r2.title");
            g_app.hEdtBusIn     = mk_edt(-1, L"", 1200, 198, 214, 24, ID_EDT_BUS_IN);
            g_app.hBtnBusBrowse = mk_btn(-1, L"浏览", 1420, 197, 56, 26, ID_BTN_BUS_BROWSE, false, "t1.browse", 2);
            g_app.hBtnBusClear  = mk_btn(-1, L"×",    1480, 197, 28, 26, ID_BTN_BUS_CLEAR,  false, nullptr,     2);
            g_app.hLblBusPreflight = mk_lbl(-1, L"", 1200, 228, 308, 36);
            SendMessageW(g_app.hLblBusPreflight, WM_SETFONT, reinterpret_cast<WPARAM>(g_app.hFontSmall), TRUE);
            g_app.hChkBusExport = mk_chk(-1, L"导出 S4 线性与溯源总线 (.nib)", 1200, 272, 308, ID_CHK_BUS_EXPORT, false, kColCard, "t3.bus_exp");

            mk_hdr(-1, L"交付产物与实时清单", 1200, 330, 308, 0, "r3.title");
            mk_lbl(-1, L"输出目录", 1200, 360, 62, 20, 0, "t1.outdir_cap");
            g_app.hOutDir       = mk_edt(-1, L"", 1264, 357, 152, 24, ID_EDT_OUT_DIR);
            SendMessageW(g_app.hOutDir, EM_SETCUEBANNER, FALSE, reinterpret_cast<LPARAM>(L"默认同目录 nijinLBS_out"));
            g_app.hBtnOutBrowse = mk_btn(-1, L"浏览", 1422, 356, 54, 26, ID_BTN_OUT_BROWSE, false, "t1.browse", 2);
            g_app.hBtnOutClear  = mk_btn(-1, L"×",    1480, 356, 28, 26, ID_BTN_OUT_CLEAR,  false, nullptr,     2);
            g_app.hChkNoToolExif = mk_chk(-1, L"不在 EXIF 内写入本工具信息", 1200, 388, 308, ID_CHK_NO_TOOL_EXIF, false, kColCard, "t1.no_tool_exif");

            g_app.hChkRaw      = mk_chk(-1, L"2× RGGB 底片", 1200, 424, 148, ID_CHK_EMIT_RAW, true, kColCard, "t1.emit_raw");
            g_app.hChkRelayRaw = mk_chk(-1, L"外部降噪直通", 1352, 424, 156, ID_CHK_RELAY_RAW, false, kColCard, "t1.relay_raw");
            g_app.hCmbRawFmt   = mk_cmb(-1, 1200, 450, 308, 180, ID_CMB_RAW_FMT);
            for (const wchar_t* s : {L"DNG 16-bit 无损", L"DNG 三通道线性 (Capture One 兼容)", L"DNG 16-bit 半精度浮点 (0.29×)",
                                     L"DNG 高级有损压缩 (0.09×)", L"DNG 16-bit 无压缩 (1.00×)"}) {
                SendMessageW(g_app.hCmbRawFmt, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(s));
            }
            SendMessageW(g_app.hCmbRawFmt, CB_SETCURSEL, 0, 0);

            g_app.hChkRgb      = mk_chk(-1, L"RGB 成片", 1200, 490, 118, ID_CHK_EMIT_RGB, true, kColCard, "t1.emit_rgb");
            g_app.hSegRgbScale = mk_seg(-1, {L"2×超分", L"1×真彩", L"双出"}, 1322, 488, 186, 25, ID_SEG_RGB_SCALE);
            g_app.hCmbRgbFmt   = mk_cmb(-1, 1200, 520, 308, 210, ID_CMB_RGB_FMT);
            for (const wchar_t* s : {L"16-bit 无损 TIFF", L"16-bit 视觉无损 TIFF (HQ, 0.48×)", L"16-bit 无损 PNG (0.40×)",
                                     L"14-bit JPEG XS 有损 (0.21×)", L"14-bit JPEG XS 无损 (0.43×)", L"16-bit 无压缩 TIFF (1.00×)"}) {
                SendMessageW(g_app.hCmbRgbFmt, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(s));
            }
            SendMessageW(g_app.hCmbRgbFmt, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(heraw_i18n::tr_w("t1.rgb_exr").c_str()));
            SendMessageW(g_app.hCmbRgbFmt, CB_SETCURSEL, 0, 0);

            g_app.hRadCsSrgb  = mk_rad(-1, L"sRGB",      1200, 552, 68,  ID_RAD_CS_SRGB,     true);
            g_app.hRadCsAdobe = mk_rad(-1, L"Adobe RGB", 1272, 552, 108, ID_RAD_CS_ADOBERGB, false);
            set_chk_val(g_app.hRadCsSrgb, true);
            g_app.hLblBppCap  = mk_lbl(-1, L"码率(bpp)", 1382, 555, 62, 18, 0, "r3.jxs_bpp_cap");
            g_app.hEdtJxsBpp  = mk_edt(-1, L"10.0", 1446, 552, 62, 22, ID_EDT_JXS_BPP);
            g_app.hCmbExrPrec = mk_cmb(-1, 1200, 580, 118, 120, ID_CMB_EXR_PREC);
            for (const char* k : {"t1.exr_half", "t1.exr_float"}) {
                SendMessageW(g_app.hCmbExrPrec, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(heraw_i18n::tr_w(k).c_str()));
            }
            SendMessageW(g_app.hCmbExrPrec, CB_SETCURSEL, 0, 0);
            g_app.hCmbExrParts = mk_cmb(-1, 1324, 580, 184, 140, ID_CMB_EXR_PARTS);
            for (const char* k : {"t1.exr_parts_all", "t1.exr_parts_bs", "t1.exr_parts_b"}) {
                SendMessageW(g_app.hCmbExrParts, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(heraw_i18n::tr_w(k).c_str()));
            }
            SendMessageW(g_app.hCmbExrParts, CB_SETCURSEL, 0, 0);

            g_app.hLblLiveReceipt = mk_lbl(-1, L"", 1208, 608, 292, 212);
            SendMessageW(g_app.hLblLiveReceipt, WM_SETFONT, reinterpret_cast<WPARAM>(g_app.hFontSmall), TRUE);

            // ================================================================
            // Column 2: Center 3-Workspace Bar & Pages (design x = 428..1172, w = 744)
            // ================================================================
            g_app.hTabBar = mk_seg(-1,
                {L"1 显影与时空", L"2 光学与散景", L"3 计量与科学场"},
                428, 66, 744, 38, ID_TAB_BAR, true);

            // ================== Workspace 1 (tab = 0): 1 显影与时空 ==================
            mk_hdr(0, L"拍摄场景预设与传感器模型", 444, 120, 520, 0, "w1.c1_title");
            g_app.hBtnPortalOptics = mk_btn(0, L"四象限 --/4 · 体检 ▸", 984, 118, 172, 26, ID_BTN_PORTAL_TAB4_OPTICS, true);

            mk_lbl(0, L"预设模式", 444, 155, 74, 20, 0, "t1.preset_cap");
            g_app.hSegPreset = mk_seg(0, {L"照片", L"星空", L"科学", L"归档"}, 522, 150, 440, 27, ID_SEG_PRESET);

            mk_lbl(0, L"光学匹配", 444, 188, 74, 20, 0, "t1.olpf_cap");
            g_app.hCmbOlpf = mk_cmb(0, 522, 184, 252, 160, ID_CMB_OLPF_MODE);
            for (const wchar_t* s : {L"自动识别传感器低通", L"无低通椭圆核", L"4点双折射弱低通核", L"16点高斯低通核"}) {
                SendMessageW(g_app.hCmbOlpf, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(s));
            }
            SendMessageW(g_app.hCmbOlpf, CB_SETCURSEL, 0, 0);
            mk_lbl(0, L"运动档位", 790, 188, 88, 20, 0, "t1.mot_prof_cap");
            g_app.hCmbMotionProfile = mk_cmb(0, 882, 184, 274, 160, ID_CMB_MOTION_PROFILE);
            for (const char* k : {"t1.mprof_auto", "t1.mprof_std", "t1.mprof_conc", "t1.mprof_sport", "t1.mprof_dxo"}) {
                SendMessageW(g_app.hCmbMotionProfile, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(heraw_i18n::tr_w(k).c_str()));
            }
            SendMessageW(g_app.hCmbMotionProfile, CB_SETCURSEL, 0, 0);

            // Card 2: 时间轴合成、计算快门与分离图层
            mk_hdr(0, L"时间轴合成、计算快门与分离图层", 444, 234, 712, 0, "w1.c2_title");

            g_app.hRadT2MotStd = mk_rad(0, L"标准姿态定格", 444, 262, 158, ID_RAD_T2_MOT_STD, true, true, "t2.freeze");
            g_app.hSegT2Anchor = mk_seg(0,
                {L"自动最锐(AF)", L"首帧", L"中间帧", L"跟随左栏指定帧"},
                608, 261, 548, 25, ID_SEG_T2_ANCHOR);

            g_app.hRadT2MotClean = mk_rad(0, L"零路人纯净空镜 (Clean Plate)", 444, 290, 250, ID_RAD_T2_MOT_CLEAN, false, true, "t2.clean");
            mk_lbl(0, L"行人剔除灵敏度", 702, 293, 116, 20, 0, "t2.clean_sens");
            g_app.hEdtCleanSens = mk_edt(0, L"1.00", 1096, 290, 60, 22, ID_EDT_CLEAN_SENS);
            g_app.hSldCleanSens = mk_sld(0, 0.3, 2.5, 1.0, 2, g_app.hEdtCleanSens, 822, 290, 266, 22, ID_SLD_CLEAN_SENS);

            g_app.hRadT2MotVnd = mk_rad(0, L"手持虚拟慢门流水 (Virtual ND)", 444, 318, 250, ID_RAD_T2_MOT_VND, false, true, "t2.vnd");
            mk_lbl(0, L"流体识别阈值", 702, 321, 116, 20, 0, "t2.vnd_thresh");
            g_app.hEdtVndThresh = mk_edt(0, L"0.65", 1096, 318, 60, 22, ID_EDT_VND_THRESH);
            g_app.hSldVndThresh = mk_sld(0, 0.10, 1.00, 0.65, 2, g_app.hEdtVndThresh, 822, 318, 266, 22, ID_SLD_VND_THRESH);

            g_app.hRadT2MotStrobe = mk_rad(0, L"运动频闪分身轨迹 (Strobe)", 444, 346, 250, ID_RAD_T2_MOT_STROBE, false, true, "t2.strobe");
            mk_lbl(0, L"抽帧步长", 702, 349, 76, 20, 0, "t2.strobe_step");
            g_app.hEdtStrobeStep = mk_edt(0, L"2", 782, 346, 52, 22, ID_EDT_STROBE_STEP, ES_NUMBER);
            mk_lbl(0, L"轨迹衰减", 848, 349, 76, 20, 0, "t2.win_fade_cap");
            g_app.hSegStrobeFade = mk_seg(0, {L"等权实影", L"渐隐残影"}, 928, 345, 228, 24, ID_SEG_STROBE_FADE);

            g_app.hRadT2MotFstack = mk_rad(0, L"景深焦点堆叠 (Focus Stack)", 444, 374, 360, ID_RAD_T2_MOT_FSTACK, false, true, "t2.fstack");
            set_chk_val(g_app.hRadT2MotStd, true);

            mk_lbl(0, L"计算快门", 444, 411, 74, 20, 0, "t2.shut_cap");
            g_app.hCmbShutterMode = mk_cmb(0, 522, 408, 176, 140, ID_CMB_SHUTTER_MODE);
            for (const char* k : {"t2.shut_anchor", "t2.shut_integ", "t2.shut_nnls", "t2.shut_deblur"}) {
                SendMessageW(g_app.hCmbShutterMode, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(heraw_i18n::tr_w(k).c_str()));
            }
            SendMessageW(g_app.hCmbShutterMode, CB_SETCURSEL, 0, 0);
            mk_lbl(0, L"κ", 712, 411, 20, 20, 0, "t2.kappa_cap");
            g_app.hEdtFlowKappa = mk_edt(0, L"0.00", 734, 408, 58, 22, ID_EDT_FLOW_KAPPA);
            g_app.hCmbIntraDeblur = mk_cmb(0, 806, 408, 172, 120, ID_CMB_INTRA_DEBLUR);
            for (const char* k : {"t2.intra_auto", "t2.intra_on", "t2.intra_off"}) {
                SendMessageW(g_app.hCmbIntraDeblur, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(heraw_i18n::tr_w(k).c_str()));
            }
            SendMessageW(g_app.hCmbIntraDeblur, CB_SETCURSEL, 0, 0);

            g_app.hChkTransientTab2 = mk_chk(0, L"剥离瞬态高光回收层 (流星 / 萤火虫 / 飞溅浪花)", 444, 448, 712, ID_CHK_TRANSIENT_TAB2, false, kColCard, "t2.emit_trans", 21);
            mk_lbl(0, L"触发门限 (σ)", 468, 476, 96, 20, 0, "t2.trans_sigma");
            g_app.hEdtTransSigma = mk_edt(0, L"3.5", 800, 473, 56, 22, ID_EDT_TRANS_SIGMA);
            g_app.hSldTransSigma = mk_sld(0, 2.0, 8.0, 3.5, 1, g_app.hEdtTransSigma, 568, 473, 224, 22, ID_SLD_TRANS_SIGMA);
            mk_lbl(0, L"色彩模式", 874, 476, 74, 20, 0, "t2.win_color_cap");
            g_app.hSegTransColor = mk_seg(0, {L"全彩层", L"灰度层"}, 952, 472, 204, 24, ID_SEG_TRANS_COLOR);

            g_app.hChkReflOn   = mk_chk(0, L"镜面碎波光 / 展柜消反光", 444, 500, 288, ID_CHK_REFL_ON, false, kColCard, "t2.refl", 21);
            g_app.hSegReflMode = mk_seg(0, {L"原地碎高光压制", L"宽基线双层消反光"}, 740, 499, 260, 24, ID_SEG_REFL_MODE);
            g_app.hLblReflDrift = mk_lbl(0, L"└─ 位移: 待导入", 468, 525, 688, 17, ID_LBL_REFL_DRIFT);
            SendMessageW(g_app.hLblReflDrift, WM_SETFONT, reinterpret_cast<WPARAM>(g_app.hFontSmall), TRUE);

            // Card 3: 超分反演、空频降噪与高光/码字似然
            mk_hdr(0, L"超分反演、空频降噪与高光/码字似然", 444, 564, 580, 0, "w1.c3_title");
            g_app.hBtnResetPreset = mk_btn(0, L"恢复预设", 1052, 562, 104, 25, ID_BTN_RESET_PRESET, false, "t1.reset_preset");

            mk_lbl(0, L"亮度互谱降噪", 444, 597, 96, 20, 0, "t1.denoise");
            g_app.hEdtDenoise  = mk_edt(0, L"1.00", 732, 594, 56, 22, ID_EDT_DENOISE);
            g_app.hSldDenoise  = mk_sld(0, 0.0, 2.5, 1.0, 2, g_app.hEdtDenoise, 544, 594, 180, 22, ID_SLD_DENOISE);

            mk_lbl(0, L"光学RL迭代", 812, 597, 92, 20, 0, "t1.psf_iters");
            g_app.hEdtPsfIters = mk_edt(0, L"4", 1100, 594, 56, 22, ID_EDT_PSF_ITERS);
            g_app.hSldPsfIters = mk_sld(0, 0.0, 12.0, 4.0, 0, g_app.hEdtPsfIters, 908, 594, 184, 22, ID_SLD_PSF_ITERS);

            mk_lbl(0, L"色空KLT强度", 444, 626, 96, 20, 0, "t1.chroma");
            g_app.hEdtChroma   = mk_edt(0, L"1.00", 732, 623, 56, 22, ID_EDT_CHROMA);
            g_app.hSldChroma   = mk_sld(0, 0.0, 2.5, 1.0, 2, g_app.hEdtChroma, 544, 623, 180, 22, ID_SLD_CHROMA);

            mk_lbl(0, L"PSF半径", 812, 626, 92, 20, 0, "t1.psf_sigma");
            g_app.hEdtPsfSigma = mk_edt(0, L"1.05", 1100, 623, 56, 22, ID_EDT_PSF_SIGMA);
            g_app.hSldPsfSigma = mk_sld(0, 0.35, 3.0, 1.05, 2, g_app.hEdtPsfSigma, 908, 623, 184, 22, ID_SLD_PSF_SIGMA);

            mk_lbl(0, L"去鬼影严格度", 444, 655, 96, 20, 0, "t1.deghost");
            g_app.hEdtDeghost  = mk_edt(0, L"1.00", 732, 652, 56, 22, ID_EDT_DEGHOST);
            g_app.hSldDeghost  = mk_sld(0, 0.4, 3.0, 1.0, 2, g_app.hEdtDeghost, 544, 652, 180, 22, ID_SLD_DEGHOST);

            mk_lbl(0, L"手动曝光EV", 812, 655, 92, 20, 0, "t1.ev");
            g_app.hEdtEv       = mk_edt(0, L"0.0", 1100, 652, 56, 22, ID_EDT_EV_OFFSET);
            g_app.hSldEv       = mk_sld(0, -4.0, 4.0, 0.0, 1, g_app.hEdtEv, 908, 652, 184, 22, ID_SLD_EV_OFFSET);

            mk_lbl(0, L"RGB微反差RL", 444, 684, 96, 20, 0, "t1.rgb_psf_cap");
            g_app.hEdtRgbPsfIters = mk_edt(0, L"2", 732, 681, 56, 22, ID_EDT_RGB_PSF_ITERS);
            g_app.hSldRgbPsfIters = mk_sld(0, 0.0, 12.0, 2.0, 0, g_app.hEdtRgbPsfIters, 544, 681, 180, 22, ID_SLD_RGB_PSF_ITERS);

            g_app.hCmbIbpIters = mk_cmb(0, 812, 680, 148, 120, ID_CMB_IBP_ITERS);
            for (const char* k : {"t1.ibp_0", "t1.ibp_1", "t1.ibp_2"}) {
                SendMessageW(g_app.hCmbIbpIters, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(heraw_i18n::tr_w(k).c_str()));
            }
            SendMessageW(g_app.hCmbIbpIters, CB_SETCURSEL, 1, 0);
            g_app.hChkEdgePolish = mk_chk(0, L"平坦区双边抛光", 970, 681, 186, ID_CHK_EDGE_POLISH, true, kColCard, "t1.polish", 21);

            g_app.hChkSiliconClean = mk_chk(0, L"硅片条带自扣除", 444, 714, 228, ID_CHK_SILICON_CLEAN, true, kColCard, "t1.silicon_clean", 21);
            g_app.hChkIbpOn        = mk_chk(-99, L"紧支集IBP回投", 0, 0, 120,   ID_CHK_IBP_ON,        true, kColCard, "t1.ibp", 21);
            g_app.hChkAutoLca      = mk_chk(0, L"盲色差归位",     684, 714, 228, ID_CHK_AUTO_LCA,      true, kColCard, "t1.auto_lca", 21);
            g_app.hChkAutoExp      = mk_chk(0, L"自动曝光",       924, 714, 232, ID_CHK_AUTO_EXP,      true, kColCard, "t1.auto_exp", 21);

            g_app.hChkRolloff      = mk_chk(0, L"高光滚降",       444, 742, 228, ID_CHK_ROLLOFF,       true, kColCard, "t1.rolloff", 21);
            g_app.hChkDither       = mk_chk(0, L"TPDF抖动",       684, 742, 228, ID_CHK_DITHER,        true, kColCard, "t1.dither", 21);

            g_app.hCmbRgb1xTier = mk_cmb(0, 444, 786, 170, 120, ID_CMB_RGB1X_TIER);
            for (const char* k : {"t1.tier_sinc", "t1.tier_fast", "t1.tier_joint"}) {
                SendMessageW(g_app.hCmbRgb1xTier, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(heraw_i18n::tr_w(k).c_str()));
            }
            SendMessageW(g_app.hCmbRgb1xTier, CB_SETCURSEL, 0, 0);

            g_app.hCmbHighlightMode = mk_cmb(0, 622, 786, 172, 120, ID_CMB_HIGHLIGHT_MODE);
            for (const char* k : {"t1.hl_legacy", "t1.hl_oklab", "t1.hl_clip"}) {
                SendMessageW(g_app.hCmbHighlightMode, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(heraw_i18n::tr_w(k).c_str()));
            }
            SendMessageW(g_app.hCmbHighlightMode, CB_SETCURSEL, 0, 0);

            g_app.hCmbCodecConstraint = mk_cmb(0, 802, 786, 172, 140, ID_CMB_CODEC_CONSTRAINT);
            for (const char* k : {"t1.codec_off", "t1.codec_box", "t1.codec_probit"}) {
                SendMessageW(g_app.hCmbCodecConstraint, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(heraw_i18n::tr_w(k).c_str()));
            }
            SendMessageW(g_app.hCmbCodecConstraint, CB_SETCURSEL, 0, 0);

            g_app.hCmbSatModel = mk_cmb(0, 982, 786, 174, 120, ID_CMB_SAT_MODEL);
            for (const char* k : {"t1.sat_hard", "t1.sat_probit"}) {
                SendMessageW(g_app.hCmbSatModel, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(heraw_i18n::tr_w(k).c_str()));
            }
            SendMessageW(g_app.hCmbSatModel, CB_SETCURSEL, 0, 0);

            // Hidden backing controls synced with Top Header / Card R1 Performance Menu
            g_app.hCmbBackend = mk_cmb(-99, 0, 0, 120, 120, ID_CMB_BACKEND);
            for (const wchar_t* s : {L"自动(GPU优先)", L"强制Vulkan GPU", L"纯CPU"}) {
                SendMessageW(g_app.hCmbBackend, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(s));
            }
            SendMessageW(g_app.hCmbBackend, CB_SETCURSEL, 0, 0);
            g_app.hCmbCpuCores = mk_cmb(-99, 0, 0, 120, 140, ID_CMB_CPU_CORES);
            {
                std::wstring cmb_lbls[5], mnu_lbls[5];
                build_win_cpu_core_labels(cmb_lbls, mnu_lbls);
                for (int i = 0; i < 5; ++i) {
                    SendMessageW(g_app.hCmbCpuCores, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(cmb_lbls[i].c_str()));
                }
            }
            SendMessageW(g_app.hCmbCpuCores, CB_SETCURSEL, 0, 0);
            g_app.hCmbPriority = mk_cmb(-99, 0, 0, 120, 120, ID_CMB_PRIORITY);
            for (const wchar_t* s : {L"标准(防降频)", L"高性能优先", L"低于标准", L"后台省电(Eco)"}) {
                SendMessageW(g_app.hCmbPriority, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(s));
            }
            SendMessageW(g_app.hCmbPriority, CB_SETCURSEL, 0, 0);
            g_app.hEdtThreads   = mk_edt(-99, L"0", 0, 0, 42, 22, ID_EDT_THREADS, ES_NUMBER);
            g_app.hCmbStreaming = mk_cmb(-99, 0, 0, 120, 120, ID_CMB_STREAMING);
            for (const char* k : {"t1.stream_auto", "t1.stream_on", "t1.stream_off"}) {
                SendMessageW(g_app.hCmbStreaming, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(heraw_i18n::tr_w(k).c_str()));
            }
            SendMessageW(g_app.hCmbStreaming, CB_SETCURSEL, 0, 0);
            g_app.hEdtMemBudget = mk_edt(-99, L"4", 0, 0, 42, 22, ID_EDT_MEM_BUDGET, ES_NUMBER);

            // ================== Workspace 2 (tab = 1): 2 光学与散景 ==================
            mk_hdr(1, L"线性域物理玻璃柔焦滤镜 (DNG 底片保持纯净)", 444, 120, 712, 0, "w2.c1_title");
            mk_lbl(1, L"光学柔焦", 444, 154, 74, 20, 0, "t2.diff_cap");
            g_app.hCmbDiffusionMode = mk_cmb(1, 522, 151, 196, 220, ID_CMB_DIFFUSION_MODE);
            for (const char* k : {"t2.diff_off", "t2.diff_hdf", "t2.diff_mist", "t2.diff_uc", "t2.diff_hal", "t2.diff_uc_hdf", "t2.diff_uc_mist", "t2.diff_uc_hal"}) {
                SendMessageW(g_app.hCmbDiffusionMode, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(heraw_i18n::tr_w(k).c_str()));
            }
            SendMessageW(g_app.hCmbDiffusionMode, CB_SETCURSEL, 0, 0);
            mk_lbl(1, L"强度", 732, 154, 44, 20, 0, "t2.diff_str_cap");
            g_app.hEdtDiffStrength = mk_edt(1, L"1.00", 1096, 151, 60, 22, ID_EDT_DIFF_STRENGTH);
            g_app.hSldDiffStrength = mk_sld(1, 0.0, 2.0, 1.0, 2, g_app.hEdtDiffStrength, 780, 151, 308, 22, ID_SLD_DIFF_STRENGTH);

            mk_lbl(1, L"UC档", 522, 190, 52, 20, 0, "t2.uc_grade_cap");
            g_app.hCmbUcGrade = mk_cmb(1, 578, 187, 78, 180, ID_CMB_UC_GRADE);
            for (const wchar_t* gs : kUcGradeCodes) {
                SendMessageW(g_app.hCmbUcGrade, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(gs));
            }
            SendMessageW(g_app.hCmbUcGrade, CB_SETCURSEL, 3, 0);
            g_app.hCmbUcBaffle = mk_cmb(1, 666, 187, 156, 120, ID_CMB_UC_BAFFLE);
            for (const char* k : {"t2.baffle_std", "t2.baffle_hood", "t2.baffle_wide"}) {
                SendMessageW(g_app.hCmbUcBaffle, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(heraw_i18n::tr_w(k).c_str()));
            }
            SendMessageW(g_app.hCmbUcBaffle, CB_SETCURSEL, 0, 0);
            g_app.hCmbUcChroma = mk_cmb(1, 832, 187, 146, 100, ID_CMB_UC_CHROMA);
            for (const char* k : {"t2.chroma_neu", "t2.chroma_warm"}) {
                SendMessageW(g_app.hCmbUcChroma, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(heraw_i18n::tr_w(k).c_str()));
            }
            SendMessageW(g_app.hCmbUcChroma, CB_SETCURSEL, 0, 0);

            // Card 2: 手持单次对焦光学 (OP-O1 光谱色差 + OP-O3 实拍散景重塑)
            mk_hdr(1, L"手持单次对焦光学：光谱色差与实拍散景重塑", 444, 240, 580, 0, "w2.c2_title");
            g_app.hBtnResetOptics = mk_btn(1, L"恢复预设", 1052, 238, 104, 25, ID_BTN_RESET_OPTICS, false, "t1.reset_preset");

            mk_lbl(1, L"光谱色差", 444, 277, 74, 20, 0, "t2.sca_cap");
            g_app.hCmbSpectralCa = mk_cmb(1, 520, 274, 152, 120, ID_CMB_SPECTRAL_CA);
            for (const char* k : {"t2.sca_off", "t2.sca_lat", "t2.sca_aura"}) {
                SendMessageW(g_app.hCmbSpectralCa, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(heraw_i18n::tr_w(k).c_str()));
            }
            SendMessageW(g_app.hCmbSpectralCa, CB_SETCURSEL, 0, 0);
            mk_lbl(1, L"幅度", 680, 277, 36, 20, 0, "t2.sca_pwr");
            g_app.hEdtSpectralPwr = mk_edt(1, L"0.00", 870, 274, 52, 22, ID_EDT_SPECTRAL_PWR);
            g_app.hSldSpectralPwr = mk_sld(1, 0.0, 12.0, 0.0, 2, g_app.hEdtSpectralPwr, 718, 274, 146, 22, ID_SLD_SPECTRAL_PWR);
            g_app.hCmbSpectralAng = mk_cmb(1, 930, 274, 154, 160, ID_CMB_SPECTRAL_ANG);
            for (const char* k : {"t2.sca_ang_rad", "t2.sca_ang_0", "t2.sca_ang_45", "t2.sca_ang_90", "t2.sca_ang_custom"}) {
                SendMessageW(g_app.hCmbSpectralAng, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(heraw_i18n::tr_w(k).c_str()));
            }
            SendMessageW(g_app.hCmbSpectralAng, CB_SETCURSEL, 0, 0);
            g_app.hEdtSpectralDeg = mk_edt(1, L"0.0", 1092, 274, 64, 22, ID_EDT_SPECTRAL_DEG);

            HWND hLblBshCap = mk_lbl(1, L"实拍散景", 444, 322, 110, 20, 0, "t2.bsh_cap");
            SendMessageW(hLblBshCap, WM_SETFONT, reinterpret_cast<WPARAM>(g_app.hFontBold), TRUE);
            g_app.hLblBokehHint = mk_lbl(1, L"↺ 负值逆时针 / ↻ 正值顺时针对数螺旋 · 单次对焦自动保护合焦主体", 560, 323, 596, 18);
            SendMessageW(g_app.hLblBokehHint, WM_SETFONT, reinterpret_cast<WPARAM>(g_app.hFontSmall), TRUE);

            mk_lbl(1, L"去二线", 444, 357, 84, 20, 0, "t2.bsh_dn");
            g_app.hEdtBokehDeNisen = mk_edt(1, L"0.00", 732, 354, 56, 22, ID_EDT_BOKEH_DENISEN);
            g_app.hSldBokehDeNisen = mk_sld(1, 0.0, 1.0, 0.0, 2, g_app.hEdtBokehDeNisen, 532, 354, 192, 22, ID_SLD_BOKEH_DENISEN);

            mk_lbl(1, L"旋焦 ↺/↻", 812, 357, 84, 20, 0, "t2.bsh_sw");
            g_app.hEdtBokehSwirl = mk_edt(1, L"0.00", 1100, 354, 56, 22, ID_EDT_BOKEH_SWIRL);
            g_app.hSldBokehSwirl = mk_sld(1, -1.0, 1.0, 0.0, 2, g_app.hEdtBokehSwirl, 900, 354, 192, 22, ID_SLD_BOKEH_SWIRL);

            mk_lbl(1, L"变形宽银幕", 444, 401, 84, 20, 0, "t2.bsh_asp");
            g_app.hEdtBokehAspect = mk_edt(1, L"1.00", 732, 398, 56, 22, ID_EDT_BOKEH_ASPECT);
            g_app.hSldBokehAspect = mk_sld(1, 1.0, 2.0, 1.0, 2, g_app.hEdtBokehAspect, 532, 398, 192, 22, ID_SLD_BOKEH_ASPECT);

            mk_lbl(1, L"净洋葱圈", 812, 401, 84, 20, 0, "t2.bsh_on");
            g_app.hEdtBokehOnion = mk_edt(1, L"0.00", 1100, 398, 56, 22, ID_EDT_BOKEH_ONION);
            g_app.hSldBokehOnion = mk_sld(1, 0.0, 1.0, 0.0, 2, g_app.hEdtBokehOnion, 900, 398, 192, 22, ID_SLD_BOKEH_ONION);

            // Card 3: 2.5D 微基线光场与 OP-O2 光子域重对焦 + 完整光圈/球差/移轴/透视调参
            mk_hdr(1, L"三维微基线光场与线性光子域散景重对焦", 444, 458, 496, 0, "t2.c3_title");
            HWND hW2DgLbl = mk_lbl(1, L"条件门禁", 948, 460, 72, 18, 0, "t3.dgate_cap");
            SendMessageW(hW2DgLbl, WM_SETFONT, reinterpret_cast<WPARAM>(g_app.hFontSmall), TRUE);
            SetPropW(hW2DgLbl, L"NijiMuted", reinterpret_cast<HANDLE>(1));
            g_app.hCmbW2DepthGate = mk_cmb(1, 1024, 456, 132, 120, ID_CMB_W2_DEPTH_GATE);
            for (const char* k : {"t3.gate_auto", "t3.gate_relaxed", "t3.gate_force"}) {
                SendMessageW(g_app.hCmbW2DepthGate, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(heraw_i18n::tr_w(k).c_str()));
            }
            SendMessageW(g_app.hCmbW2DepthGate, CB_SETCURSEL, 0, 0);

            g_app.hParallaxMeter = reg_ctrl(CreateWindowExW(0, L"NijiParallaxBar", L"", WS_CHILD | WS_VISIBLE,
                S(444), S(488), S(712), S(24), hwnd, reinterpret_cast<HMENU>((INT_PTR)ID_VIEW_PARALLAX_METER), hInst, nullptr),
                1, 1, RECT{S(444), S(488), S(444 + 712), S(488 + 24)});

            g_app.hChkExportParallax = mk_chk(1, L"导出 2.5D 交互光场包 (.html)", 444, 524, 290, ID_CHK_EXPORT_PARALLAX, false, kColCard, "t2.export_para");
            mk_lbl(1, L"视角环绕幅度", 744, 527, 96, 20, 0, "t2.para_amp");
            g_app.hEdtParallaxAmp = mk_edt(1, L"1.00", 1096, 524, 60, 22, ID_EDT_PARALLAX_AMP);
            g_app.hSldParallaxAmp = mk_sld(1, 0.2, 2.5, 1.0, 2, g_app.hEdtParallaxAmp, 844, 524, 244, 22, ID_SLD_PARALLAX_AMP);

            g_app.hChkExportRefocus = mk_chk(1, L"额外渲染线性光子域散景重对焦图", 444, 573, 256, ID_CHK_EXPORT_REFOCUS, false, kColCard, "t2.export_refocus");
            g_app.hCmbRefocusMode = mk_cmb(1, 706, 570, 146, 140, ID_CMB_REFOCUS_MODE);
            for (const char* k : {"w2.ref_mode_auto", "w2.ref_mode_bokeh", "w2.ref_mode_synth", "w2.ref_mode_shift"}) {
                SendMessageW(g_app.hCmbRefocusMode, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(heraw_i18n::tr_w(k).c_str()));
            }
            SendMessageW(g_app.hCmbRefocusMode, CB_SETCURSEL, 0, 0);
            g_app.hCmbApertureWeight = mk_cmb(1, 860, 570, 144, 100, ID_CMB_APERTURE_WEIGHT);
            for (const char* k : {"t2.ap_voronoi", "t2.ap_dwell"}) {
                SendMessageW(g_app.hCmbApertureWeight, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(heraw_i18n::tr_w(k).c_str()));
            }
            SendMessageW(g_app.hCmbApertureWeight, CB_SETCURSEL, 0, 0);
            g_app.hCmbMetricPolicy = mk_cmb(1, 1012, 570, 144, 100, ID_CMB_METRIC_POLICY);
            for (const char* k : {"t2.met_ver", "t2.met_proxy"}) {
                SendMessageW(g_app.hCmbMetricPolicy, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(heraw_i18n::tr_w(k).c_str()));
            }
            SendMessageW(g_app.hCmbMetricPolicy, CB_SETCURSEL, 0, 0);

            mk_lbl(1, L"合焦平面偏移", 444, 617, 92, 20, 0, "t2.refocus_pos");
            g_app.hEdtRefocusPos = mk_edt(1, L"0.00", 662, 614, 52, 22, ID_EDT_REFOCUS_POS);
            g_app.hSldRefocusPos = mk_sld(1, -1.0, 1.0, 0.0, 2, g_app.hEdtRefocusPos, 538, 614, 118, 22, ID_SLD_REFOCUS_POS);
            g_app.hChkRefocusAuto = mk_chk(1, L"自动主体", 720, 614, 86, ID_CHK_REFOCUS_AUTO, false, kColCard, "w2.ref_auto_pos", 21);

            mk_lbl(1, L"散景光圈强度", 812, 617, 96, 20, 0, "t2.bokeh_str");
            g_app.hEdtBokehStr = mk_edt(1, L"1.00", 1100, 614, 56, 22, ID_EDT_BOKEH_STR);
            g_app.hSldBokehStr = mk_sld(1, 0.0, 3.0, 1.0, 2, g_app.hEdtBokehStr, 912, 614, 180, 22, ID_SLD_BOKEH_STR);

            mk_lbl(1, L"三圆盘正值STF", 444, 659, 96, 20, 0, "w2.stf_cap");
            g_app.hEdtBokehStf = mk_edt(1, L"0.00", 732, 656, 56, 22, ID_EDT_BOKEH_STF);
            g_app.hSldBokehStf = mk_sld(1, 0.0, 1.0, 0.0, 2, g_app.hEdtBokehStf, 544, 656, 180, 22, ID_SLD_BOKEH_STF);

            mk_lbl(1, L"球差SA", 812, 659, 96, 20, 0, "w2.sa_cap");
            g_app.hEdtBokehSa = mk_edt(1, L"0.00", 1100, 656, 56, 22, ID_EDT_BOKEH_SA);
            g_app.hSldBokehSa = mk_sld(1, -0.50, 0.50, 0.0, 2, g_app.hEdtBokehSa, 912, 656, 180, 22, ID_SLD_BOKEH_SA);

            mk_lbl(1, L"F/", 444, 703, 34, 20, 0, "w2.fstop_cap");
            g_app.hEdtVirtualFstop = mk_edt(1, L"0.0", 480, 700, 64, 22, ID_EDT_VIRTUAL_FSTOP);
            g_app.hCmbBokehBlades  = mk_cmb(1, 560, 698, 190, 180, ID_CMB_BOKEH_BLADES);
            for (const char* k : {"w2.blades_0", "w2.blades_5", "w2.blades_6", "w2.blades_7", "w2.blades_8", "w2.blades_9", "w2.blades_11"}) {
                SendMessageW(g_app.hCmbBokehBlades, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(heraw_i18n::tr_w(k).c_str()));
            }
            SendMessageW(g_app.hCmbBokehBlades, CB_SETCURSEL, 0, 0);
            mk_lbl(1, L"转角°", 768, 703, 64, 20, 0, "w2.angle_cap");
            g_app.hEdtBokehAngle = mk_edt(1, L"0", 836, 700, 64, 22, ID_EDT_BOKEH_ANGLE);

            mk_lbl(1, L"沙姆X/Y", 444, 747, 84, 20, 0, "w2.tilt_cap");
            g_app.hEdtTiltX = mk_edt(1, L"0.00", 532, 744, 64, 22, ID_EDT_TILT_X);
            mk_lbl(1, L"×", 602, 747, 14, 20);
            g_app.hEdtTiltY = mk_edt(1, L"0.00", 620, 744, 64, 22, ID_EDT_TILT_Y);
            g_app.hCmbSeeThrough = mk_cmb(1, 704, 742, 232, 120, ID_CMB_SEE_THROUGH);
            for (const char* k : {"w2.see_auto", "w2.see_on", "w2.see_off"}) {
                SendMessageW(g_app.hCmbSeeThrough, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(heraw_i18n::tr_w(k).c_str()));
            }
            SendMessageW(g_app.hCmbSeeThrough, CB_SETCURSEL, 0, 0);

            // ================== Workspace 3 (tab = 2): 3 计量与科学场 ==================
            // Top Card: Twin Metrology Radars + Silicon + .lcp / PNG / JSON Export
            g_app.hOpticsCanvas = reg_ctrl(CreateWindowExW(0, L"NijiOpticsCanvas", L"", WS_CHILD | WS_VISIBLE,
                S(444), S(118), S(712), S(192), hwnd, reinterpret_cast<HMENU>((INT_PTR)ID_VIEW_OPTICS_CANVAS), hInst, nullptr),
                2, 1, RECT{S(444), S(118), S(444 + 712), S(118 + 192)});

            g_app.hBtnRunOptics    = mk_btn(2, L"运行原始 RAW 8× 光学精测", 444, 315, 224, 26, ID_BTN_RUN_OPTICS, false, "t4.run_optics");
            g_app.hLblOpticsStatus = mk_lbl(2, g_app.optics.ois_note.c_str(), 676, 319, 480, 18, ID_LBL_OPTICS_STATUS);
            SendMessageW(g_app.hLblOpticsStatus, WM_SETFONT, reinterpret_cast<WPARAM>(g_app.hFontSmall), TRUE);
            g_app.hLblSiliconStats = mk_lbl(2, silicon_readout(g_app.optics).c_str(), 444, 345, 712, 18, ID_LBL_SILICON_STATS);
            SendMessageW(g_app.hLblSiliconStats, WM_SETFONT, reinterpret_cast<WPARAM>(g_app.hFontMono), TRUE);

            mk_lbl(2, L".lcp 包含项:", 444, 370, 88, 20, 0, "t4.lcp_cap");
            g_app.hChkLcpLca  = mk_chk(2, L"倍率色差 (LCA)", 536, 369, 168, ID_CHK_LCP_LCA,  true,  kColCard, "t4.lcp_lca");
            g_app.hChkLcpVig  = mk_chk(2, L"真实暗角",       710, 369, 164, ID_CHK_LCP_VIG,  false, kColCard, "t4.lcp_vig");
            g_app.hChkLcpDist = mk_chk(2, L"径向畸变",       880, 369, 180, ID_CHK_LCP_DIST, false, kColCard, "t4.lcp_dist");

            g_app.hBtnExportLcp     = mk_btn(2, L"导出标准 Adobe .lcp 校正档", 444, 397, 230, 27, ID_BTN_EXPORT_LCP,      false, "t4.btn_lcp");
            g_app.hBtnExportPngCard = mk_btn(2, L"导出光学体检图卡 (.png)",    685, 397, 230, 27, ID_BTN_EXPORT_PNG_CARD, false, "t4.btn_png");
            g_app.hBtnExportJsonTel = mk_btn(2, L"导出完整遥测 (.json)",       926, 397, 230, 27, ID_BTN_EXPORT_JSON_TEL, false, "t4.btn_json");

            // Bottom-Left Card: 27 Per-Pixel Physical Fields + 6 Carrier Modes + Depth Gate Thresholds
            HWND hHdrFlds = mk_hdr(2, L"物理场提取与视差门禁调参", 440, 450, 158, 0, "t3.c1_title");
            SendMessageW(hHdrFlds, WM_SETFONT, reinterpret_cast<WPARAM>(g_app.hFontBold), TRUE);
            g_app.hBtnFieldsBasic3 = mk_btn(2, L"常用三项", 600, 449, 62, 23, ID_BTN_FIELDS_BASIC3, false, "t3.core3");
            g_app.hBtnFieldsAll    = mk_btn(2, L"全选",     665, 449, 62, 23, ID_BTN_FIELDS_ALL,    false, "t3.select_all");
            g_app.hBtnFieldsNone   = mk_btn(2, L"清空",     730, 449, 52, 23, ID_BTN_FIELDS_NONE,   false, "dock.btn_clear");

            g_app.hChkField     = mk_chk(2, L"物理场图", 440, 479, 98, ID_CHK_EMIT_FIELD, false, kColCard, "t1.emit_field", 21);
            g_app.hCmbFieldMode = mk_cmb(2, 542, 477, 240, 180, ID_CMB_FIELD_MODE);
            for (const char* k : {"t1.fld_tiff_alpha", "t1.fld_split", "t1.fld_rgb", "t3.car_motion", "t3.car_phase", "t3.car_focus"}) {
                SendMessageW(g_app.hCmbFieldMode, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(heraw_i18n::tr_w(k).c_str()));
            }
            SendMessageW(g_app.hCmbFieldMode, CB_SETCURSEL, 1, 0);

            auto mk_muted_lbl2 = [&](const char* k, int lx, int ly, int lw) {
                HWND hw = mk_lbl(2, heraw_i18n::tr_w(k).c_str(), lx, ly, lw, 18, 0, k);
                SendMessageW(hw, WM_SETFONT, reinterpret_cast<WPARAM>(g_app.hFontSmall), TRUE);
                SetPropW(hw, L"NijiMuted", reinterpret_cast<HANDLE>(1));
                return hw;
            };
            mk_muted_lbl2("t3.dgate_cap", 440, 509, 76);
            g_app.hCmbDepthGate = mk_cmb(2, 520, 505, 262, 120, ID_CMB_DEPTH_GATE);
            for (const char* k : {"t3.gate_auto", "t3.gate_relaxed", "t3.gate_force"}) {
                SendMessageW(g_app.hCmbDepthGate, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(heraw_i18n::tr_w(k).c_str()));
            }
            SendMessageW(g_app.hCmbDepthGate, CB_SETCURSEL, 0, 0);

            mk_muted_lbl2("t3.min_disp_cap", 440, 537, 46);
            g_app.hEdtDepthDisp = mk_edt(2, L"", 488, 534, 58, 21, ID_EDT_DEPTH_DISP);
            mk_muted_lbl2("t3.min_coh_cap", 554, 537, 46);
            g_app.hEdtDepthCoh  = mk_edt(2, L"", 602, 534, 58, 21, ID_EDT_DEPTH_COH);
            mk_muted_lbl2("t3.min_sup_cap", 668, 537, 50);
            g_app.hEdtDepthSup  = mk_edt(2, L"", 720, 534, 62, 21, ID_EDT_DEPTH_SUP);

            g_app.hSegFieldBank = mk_seg(2, {L"核心 11 场", L"扩展几何与观测 16 场"}, 440, 561, 342, 24, ID_SEG_FIELD_BANK);

            for (int i = 0; i < kNumFields; ++i) {
                bool def_on = (i < 3); // motion, phase, focus
                g_app.hChkFields[i] = mk_chk(2, kFieldSpecs[i].label, 440, 590 + i * 20, 342,
                                             ID_CHK_FIELD_BASE + i, def_on, kColCard, kFieldSpecs[i].lang_key, 19);
                SendMessageW(g_app.hChkFields[i], WM_SETFONT, reinterpret_cast<WPARAM>(g_app.hFontSmall), TRUE);
            }
            for (int i = 0; i < kNumExtFields; ++i) {
                int r = i / 2, c = i % 2;
                int ex = (c == 0) ? 440 : 614;
                g_app.hChkExtFields[i] = mk_chk(2, kExtFieldSpecs[i].label, ex, 592 + r * 26, 168,
                                                ID_CHK_EXT_FIELD_BASE + i, false, kColCard, kExtFieldSpecs[i].lang_key, 20);
                SendMessageW(g_app.hChkExtFields[i], WM_SETFONT, reinterpret_cast<WPARAM>(g_app.hFontSmall), TRUE);
            }
            g_app.hLblFieldsGateNote = mk_lbl(2, heraw_i18n::tr_w("t3.gate_idle").c_str(), 440, 814, 342, 18);
            SendMessageW(g_app.hLblFieldsGateNote, WM_SETFONT, reinterpret_cast<WPARAM>(g_app.hFontSmall), TRUE);

            // Bottom-Right Card: O1-O10 Multi-Select Observers + Calibration + Self-Eval + Dataset Tile Factory
            HWND hHdrObs = mk_hdr(2, L"科学观测器 (O1–O10) 与标定", 818, 450, 214, 0, "obs.matrix_title");
            SendMessageW(hHdrObs, WM_SETFONT, reinterpret_cast<WPARAM>(g_app.hFontBold), TRUE);
            g_app.hBtnObsAll  = mk_btn(2, L"全选", 1036, 449, 60, 23, ID_BTN_OBS_ALL,  false, "t3.select_all");
            g_app.hBtnObsNone = mk_btn(2, L"清空", 1100, 449, 60, 23, ID_BTN_OBS_NONE, false, "dock.btn_clear");
            for (int i = 0; i < kNumObs; ++i) {
                int r = i / 2, c = i % 2;
                int ox = (c == 0) ? 818 : 990;
                int ow = (c == 0) ? 168 : 170;
                g_app.hChkObs[i] = mk_chk(2, kObsSpecs[i].label, ox, 478 + r * 21, ow,
                                          ID_CHK_OBS_BASE + i, false, kColCard, kObsSpecs[i].lang_key, 19);
                SendMessageW(g_app.hChkObs[i], WM_SETFONT, reinterpret_cast<WPARAM>(g_app.hFontSmall), TRUE);
            }
            mk_muted_lbl2("obs.gate_cap", 818, 590, 62);
            g_app.hCmbObsGate = mk_cmb(2, 882, 586, 126, 120, ID_CMB_OBS_GATE);
            for (const char* k : {"t3.gate_auto", "t3.gate_relaxed", "t3.gate_force"}) {
                SendMessageW(g_app.hCmbObsGate, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(heraw_i18n::tr_w(k).c_str()));
            }
            SendMessageW(g_app.hCmbObsGate, CB_SETCURSEL, 0, 0);
            mk_muted_lbl2("obs.readout_cap", 1016, 590, 68);
            g_app.hEdtReadoutMs = mk_edt(2, L"", 1086, 587, 74, 21, ID_EDT_READOUT_MS);

            mk_muted_lbl2("obs.pitch_cap", 818, 618, 52);
            g_app.hEdtObsPitch = mk_edt(2, L"0.0", 872, 615, 74, 21, ID_EDT_OBS_PITCH);
            mk_muted_lbl2("obs.pscale_cap", 958, 618, 84);
            g_app.hEdtPlateScale = mk_edt(2, L"0.00", 1046, 615, 114, 21, ID_EDT_PLATE_SCALE);

            g_app.hChkWriteObsReport = mk_chk(2, L"写出观测报告 (.json)",   818, 642, 168, ID_CHK_WRITE_OBS_REP,  false, kColCard, "obs.write_rep",  19);
            g_app.hChkNextShotPlan   = mk_chk(2, L"下一张拍摄规划 (.json)", 990, 642, 170, ID_CHK_NEXT_SHOT_PLAN, false, kColCard, "obs.next_short", 19);
            g_app.hChkEmitDiffuse    = mk_chk(2, L"O1 漫反射本征图",        818, 665, 168, ID_CHK_EMIT_DIFFUSE,   false, kColCard, "obs.emit_diff",  19);
            g_app.hChkEmitLightSplit = mk_chk(2, L"O2 交直流分离层",        990, 665, 170, ID_CHK_EMIT_LSPLIT,    false, kColCard, "obs.emit_lsplit",19);
            g_app.hChkEvalSelf       = mk_chk(2, L"自校验 LOO χ² / 半组 FRC (.json)", 818, 688, 342, ID_CHK_EVAL_SELF, false, kColCard, "obs.eval_self", 19);
            for (HWND ho : {g_app.hChkWriteObsReport, g_app.hChkNextShotPlan, g_app.hChkEmitDiffuse, g_app.hChkEmitLightSplit, g_app.hChkEvalSelf}) {
                SendMessageW(ho, WM_SETFONT, reinterpret_cast<WPARAM>(g_app.hFontSmall), TRUE);
            }

            HWND hHdrTiles = mk_hdr(2, L"Sci 机身专属自监督真值数据集工厂", 818, 722, 342, 0, "t3.c2_title");
            SendMessageW(hHdrTiles, WM_SETFONT, reinterpret_cast<WPARAM>(g_app.hFontBold), TRUE);
            g_app.hChkExportTiles = mk_chk(2, L"同步切出配对训练块 (1× 带噪 RAW ↔ 2× 真值 + σ)", 818, 746, 342, ID_CHK_EXPORT_TILES, false, kColCard, "t3.export_tiles", 20);
            SendMessageW(g_app.hChkExportTiles, WM_SETFONT, reinterpret_cast<WPARAM>(g_app.hFontSmall), TRUE);
            mk_lbl(2, L"切片尺寸", 818, 774, 80, 20, 0, "t3.tile_size");
            g_app.hSegTileSize = mk_seg(2, {L"256×256", L"512×512"}, 902, 770, 176, 24, ID_SEG_TILE_SIZE);
            mk_lbl(2, L"静态相位置信度门槛", 818, 804, 132, 20, 0, "t3.tile_conf");
            g_app.hEdtTileConf = mk_edt(2, L"0.75", 1104, 802, 56, 22, ID_EDT_TILE_CONF);
            g_app.hSldTileConf = mk_sld(2, 0.30, 0.99, 0.75, 2, g_app.hEdtTileConf, 954, 802, 144, 22, ID_SLD_TILE_CONF);

            // ================================================================
            // Bottom Global Action Bar (x = 18..1522, y = 852..908)
            // ================================================================
            g_app.hBtnRun       = mk_btn(-1, L"开始执行",     30,   863, 276, 34, ID_BTN_RUN,        false, "bot.run",        0);
            g_app.hBtnExportLog = mk_btn(-1, L"导出日志",     1298, 863, 102, 34, ID_BTN_EXPORT_LOG, false, "bot.export_log", 0);
            g_app.hBtnOpenOut   = mk_btn(-1, L"打开输出目录", 1406, 863, 104, 34, ID_BTN_OPEN_OUT,   false, "bot.open_dir",   0);

            apply_preset_to_controls(0);
            apply_ui_language_win();
            return 0;
        }

        case WM_GETMINMAXINFO: {
            auto* mmi = reinterpret_cast<MINMAXINFO*>(lParam);
            if (mmi) {
                int dpi = g_app.dpi > 0 ? g_app.dpi : 96;
                RECT mr{0, 0, MulDiv(1360, dpi, 96), MulDiv(800, dpi, 96)};
                AdjustWindowRect(&mr, (DWORD)GetWindowLongPtrW(hwnd, GWL_STYLE), FALSE);
                mmi->ptMinTrackSize.x = mr.right - mr.left;
                mmi->ptMinTrackSize.y = mr.bottom - mr.top;
                if (g_app.is_screenshot_mode || g_app.allow_unconstrained_size) {
                    mmi->ptMaxTrackSize.x = std::max<LONG>(mmi->ptMaxTrackSize.x, MulDiv(4096, dpi, 96));
                    mmi->ptMaxTrackSize.y = std::max<LONG>(mmi->ptMaxTrackSize.y, MulDiv(4096, dpi, 96));
                }
            }
            return 0;
        }

        case WM_SIZE: {
            int cw = LOWORD(lParam);
            int ch = HIWORD(lParam);
            if (cw > 0 && ch > 0 && g_app.hBtnRun) {
                auto S = [](int px) { return g_app.S(px); };
                int bottomBarY = ch - S(68);
                int workBot    = bottomBarY - S(12);
                int rwDock     = S(336);
                int rxDock     = cw - S(18) - rwDock;
                int dockDx     = rxDock - S(1186);
                int cxStage    = S(428);
                int cwStage    = std::max(S(556), rxDock - S(14) - cxStage);
                int stageTop   = S(112);
                int stageH     = std::max(S(580), workBot - stageTop);

                for (const auto& control : g_app.tab_ctrls) {
                    if (control.tab == -99 || !control.hw) continue;
                    const RECT& r = control.design;
                    if (control.col_mode == 1) {
                        int x = cxStage + MulDiv(r.left - cxStage, cwStage, S(744));
                        int w = MulDiv(r.right - r.left, cwStage, S(744));
                        int y = r.top;
                        int h = r.bottom - r.top;
                        if (control.tab >= 0) {
                            y = stageTop + MulDiv(r.top - stageTop, stageH, S(728));
                            if (control.hw == g_app.hOpticsCanvas) {
                                h = std::max(S(140), MulDiv(r.bottom - r.top, stageH, S(728)));
                            }
                        }
                        MoveWindow(control.hw, x, y, w, h, TRUE);
                    } else if (control.col_mode == 2) {
                        if (control.hw == g_app.hLblLiveReceipt) continue;
                        MoveWindow(control.hw, r.left + dockDx, r.top, r.right - r.left, r.bottom - r.top, TRUE);
                    }
                }
                relayout_right_dock_r3_context();

                int hdrRightX = std::max(S(640), rxDock - S(14));
                int brandW    = S(176);
                int brandX    = hdrRightX - S(10) - brandW;
                if (g_app.hBtnBrandHeader) {
                    MoveWindow(g_app.hBtnBrandHeader, brandX, S(7), brandW, S(44), TRUE);
                }
                if (g_app.hBtnPerfHeader) {
                    MoveWindow(g_app.hBtnPerfHeader, brandX - S(10) - S(194), S(15), S(194), S(26), TRUE);
                }
                MoveWindow(g_app.hFrameView, S(30), S(508), S(372), std::max(S(120), workBot - S(520)), TRUE);
                MoveWindow(g_app.hBtnRun,     S(30), bottomBarY + S(11), S(276), S(34), TRUE);
                if (g_app.hBtnExportLog) {
                    MoveWindow(g_app.hBtnExportLog, cw - S(244), bottomBarY + S(11), S(102), S(34), TRUE);
                }
                MoveWindow(g_app.hBtnOpenOut, cw - S(136), bottomBarY + S(11), S(104), S(34), TRUE);
                InvalidateRect(hwnd, nullptr, FALSE);
            }
            return 0;
        }

        case WM_TIMER: {
            if (wParam == TIMER_PERF_BREATH) {
                if (!g_app.perf_breath_active) {
                    KillTimer(hwnd, TIMER_PERF_BREATH);
                    return 0;
                }
                ULONGLONG el = GetTickCount64() - g_app.perf_breath_start_ms;
                if (el >= 10000ULL) {
                    stop_perf_breathing_alert(hwnd);
                } else {
                    g_app.perf_breath_phase = 0.5f - 0.5f * std::cos((float)el * (2.0f * std::numbers::pi_v<float> / 1500.0f));
                    if (g_app.hBtnPerfHeader) InvalidateRect(g_app.hBtnPerfHeader, nullptr, FALSE);
                }
                return 0;
            }
            break;
        }

        case WM_DRAWITEM: {
            const auto* dis = reinterpret_cast<const DRAWITEMSTRUCT*>(lParam);
            if (dis && dis->CtlType == ODT_BUTTON) {
                draw_modern_pill_button(dis);
                return TRUE;
            }
            break;
        }

        case WM_ERASEBKGND:
            return 1;

        case WM_PAINT: {
            PAINTSTRUCT ps{};
            HDC hdc = BeginPaint(hwnd, &ps);
            RECT crc{};
            GetClientRect(hwnd, &crc);
            HDC mem = CreateCompatibleDC(hdc);
            HBITMAP bmp = CreateCompatibleBitmap(hdc, std::max(1L, crc.right), std::max(1L, crc.bottom));
            HGDIOBJ old = SelectObject(mem, bmp);
            paint_studio_canvas(hwnd, mem);
            BitBlt(hdc, 0, 0, crc.right, crc.bottom, mem, 0, 0, SRCCOPY);
            SelectObject(mem, old);
            DeleteObject(bmp);
            DeleteDC(mem);
            EndPaint(hwnd, &ps);
            return 0;
        }

        case WM_PRINTCLIENT: {
            paint_studio_canvas(hwnd, reinterpret_cast<HDC>(wParam));
            return 0;
        }

        case WM_CTLCOLORSTATIC: {
            HDC hdc = reinterpret_cast<HDC>(wParam);
            HWND hCtrl = reinterpret_cast<HWND>(lParam);
            wchar_t cls[32] = {0};
            GetClassNameW(hCtrl, cls, 31);
            if (_wcsicmp(cls, L"Edit") == 0) {
                SetBkColor(hdc, kColDisabledFill);
                SetTextColor(hdc, kColTextMuted);
                return reinterpret_cast<LRESULT>(g_app.hBrDisabledEdit);
            }
            if (hCtrl == g_app.hLblLiveReceipt) {
                SetBkColor(hdc, kColCardSub);
                SetTextColor(hdc, kColTextMain);
                return reinterpret_cast<LRESULT>(g_app.hBrCardSub);
            }
            SetBkColor(hdc, kColCard);
            if (hCtrl == g_app.hLblBusPreflight) {
                SetTextColor(hdc, g_app.bus_preflight_status == 1 ? kColOkGreen
                                : (g_app.bus_preflight_status == 2 ? kColWarnCoral : kColTextMuted));
            } else if (hCtrl == g_app.hLblR1GateSr) {
                SetTextColor(hdc, g_app.r1_sr_status == 1 ? kColOkGreen
                                : (g_app.r1_sr_status == 2 ? kColWarnAmber : kColTextMuted));
            } else if (hCtrl == g_app.hLblR1GatePar) {
                SetTextColor(hdc, g_app.r1_par_status == 1 ? kColOkGreen : kColTextMuted);
            } else if (hCtrl == g_app.hLblR1GroupSummary) {
                SetTextColor(hdc, kColAccentDark);
            } else if (hCtrl == g_app.hLblEngineStatus || hCtrl == g_app.hLblReflDrift ||
                       hCtrl == g_app.hLblOpticsStatus || hCtrl == g_app.hLblBokehHint ||
                       hCtrl == g_app.hLblFieldsGateNote || GetPropW(hCtrl, L"NijiMuted") != nullptr) {
                SetTextColor(hdc, kColTextMuted);
            } else {
                SetTextColor(hdc, kColTextMain);
            }
            return reinterpret_cast<LRESULT>(g_app.hBrCard);
        }

        case WM_CTLCOLOREDIT: {
            HDC hdc = reinterpret_cast<HDC>(wParam);
            SetBkColor(hdc, RGB(255, 255, 255));
            SetTextColor(hdc, kColTextMain);
            return reinterpret_cast<LRESULT>(g_app.hBrWhite);
        }

        case WM_CTLCOLORBTN: {
            HDC hdc = reinterpret_cast<HDC>(wParam);
            SetBkColor(hdc, kColCard);
            return reinterpret_cast<LRESULT>(g_app.hBrCard);
        }

        case WM_DROPFILES: {
            HDROP hDrop = reinterpret_cast<HDROP>(wParam);
            UINT cnt = DragQueryFileW(hDrop, 0xFFFFFFFF, nullptr, 0);
            for (UINT i = 0; i < cnt; ++i) {
                UINT len = DragQueryFileW(hDrop, i, nullptr, 0);
                std::wstring p(len, L'\0');
                DragQueryFileW(hDrop, i, &p[0], len + 1);
                add_unique_input(p);
            }
            DragFinish(hDrop);
            trigger_async_plan_scan();
            return 0;
        }

        case WM_COMMAND: {
            int id = LOWORD(wParam);
            int code = HIWORD(wParam);

            if (code == EN_CHANGE && !g_app.suppress_sync) {
                if (id == ID_EDT_GROUP_SIZE && !g_app.loading_prefs && !g_app.inputs.empty()) {
                    g_app.grouping_dirty = true;
                    sync_cross_tab_ui();
                }
                if (id == ID_EDT_BUS_IN) {
                    commit_bus_in_edit_win(get_wnd_text(g_app.hEdtBusIn));
                    sync_cross_tab_ui();
                    return 0;
                }
                switch (id) {
                    case ID_EDT_DENOISE:       sync_slider_from_edit(g_app.hSldDenoise,      g_app.hEdtDenoise);      break;
                    case ID_EDT_PSF_ITERS:     sync_slider_from_edit(g_app.hSldPsfIters,     g_app.hEdtPsfIters);     break;
                    case ID_EDT_RGB_PSF_ITERS: sync_slider_from_edit(g_app.hSldRgbPsfIters,  g_app.hEdtRgbPsfIters);  break;
                    case ID_EDT_CHROMA:        sync_slider_from_edit(g_app.hSldChroma,       g_app.hEdtChroma);       break;
                    case ID_EDT_PSF_SIGMA:     sync_slider_from_edit(g_app.hSldPsfSigma,     g_app.hEdtPsfSigma);     break;
                    case ID_EDT_DEGHOST:       sync_slider_from_edit(g_app.hSldDeghost,      g_app.hEdtDeghost);      break;
                    case ID_EDT_EV_OFFSET:     sync_slider_from_edit(g_app.hSldEv,           g_app.hEdtEv);           break;
                    case ID_EDT_CLEAN_SENS:    sync_slider_from_edit(g_app.hSldCleanSens,    g_app.hEdtCleanSens);    break;
                    case ID_EDT_VND_THRESH:    sync_slider_from_edit(g_app.hSldVndThresh,    g_app.hEdtVndThresh);    break;
                    case ID_EDT_TRANS_SIGMA:   sync_slider_from_edit(g_app.hSldTransSigma,   g_app.hEdtTransSigma);   break;
                    case ID_EDT_DIFF_STRENGTH: sync_slider_from_edit(g_app.hSldDiffStrength, g_app.hEdtDiffStrength); break;
                    case ID_EDT_SPECTRAL_PWR:  sync_slider_from_edit(g_app.hSldSpectralPwr,  g_app.hEdtSpectralPwr);  break;
                    case ID_EDT_BOKEH_DENISEN: sync_slider_from_edit(g_app.hSldBokehDeNisen, g_app.hEdtBokehDeNisen); break;
                    case ID_EDT_BOKEH_SWIRL:   sync_slider_from_edit(g_app.hSldBokehSwirl,   g_app.hEdtBokehSwirl);   break;
                    case ID_EDT_BOKEH_ASPECT:  sync_slider_from_edit(g_app.hSldBokehAspect,  g_app.hEdtBokehAspect);  break;
                    case ID_EDT_BOKEH_ONION:   sync_slider_from_edit(g_app.hSldBokehOnion,   g_app.hEdtBokehOnion);   break;
                    case ID_EDT_PARALLAX_AMP:  sync_slider_from_edit(g_app.hSldParallaxAmp,  g_app.hEdtParallaxAmp);  break;
                    case ID_EDT_REFOCUS_POS:   sync_slider_from_edit(g_app.hSldRefocusPos,   g_app.hEdtRefocusPos);   break;
                    case ID_EDT_BOKEH_STR:     sync_slider_from_edit(g_app.hSldBokehStr,     g_app.hEdtBokehStr);     break;
                    case ID_EDT_BOKEH_STF:     sync_slider_from_edit(g_app.hSldBokehStf,     g_app.hEdtBokehStf);     break;
                    case ID_EDT_BOKEH_SA:      sync_slider_from_edit(g_app.hSldBokehSa,      g_app.hEdtBokehSa);      break;
                    case ID_EDT_TILE_CONF:     sync_slider_from_edit(g_app.hSldTileConf,     g_app.hEdtTileConf);     break;
                }
                sync_cross_tab_ui();
                return 0;
            }
            if (code == EN_KILLFOCUS) {
                save_ui_prefs();
                if (id == ID_EDT_GROUP_SIZE && g_app.grouping_dirty && !g_app.inputs.empty()) {
                    int gm = (int)SendMessageW(g_app.hCmbGrouping, CB_GETCURSEL, 0, 0);
                    if (gm == 1 || _wtoi(get_wnd_text(g_app.hEdtGrpSz).c_str()) >= min_valid_frames_win()) {
                        trigger_async_plan_scan();
                    }
                }
                return 0;
            }

            if ((id >= ID_CHK_FIELD_BASE && id < ID_CHK_FIELD_BASE + kNumFields) ||
                (id >= ID_CHK_EXT_FIELD_BASE && id < ID_CHK_EXT_FIELD_BASE + kNumExtFields)) {
                sync_cross_tab_ui();
                save_ui_prefs();
                return 0;
            }
            if (id >= ID_CHK_OBS_BASE && id < ID_CHK_OBS_BASE + kNumObs) {
                if (count_checked_observers_win() > 0 && g_app.hChkWriteObsReport && !get_chk(g_app.hChkWriteObsReport)) {
                    set_chk_val(g_app.hChkWriteObsReport, true);
                }
                sync_cross_tab_ui();
                save_ui_prefs();
                return 0;
            }

            if (id >= ID_MENU_LANG_BASE && id < ID_MENU_LANG_BASE + 40) {
                auto langs = heraw_i18n::I18nManager::instance().available_languages();
                size_t li = (size_t)(id - ID_MENU_LANG_BASE);
                if (li < langs.size()) {
                    heraw_i18n::I18nManager::instance().set_language(langs[li].first);
                    heraw_i18n::diag_log().log_event("UI", "Switched UI language to " + langs[li].first);
                    apply_ui_language_win();
                    save_ui_prefs();
                }
                return 0;
            }

            switch (id) {
                case ID_CMB_GROUPING:
                    if (code == CBN_SELCHANGE) {
                        save_ui_prefs();
                        if (!g_app.inputs.empty()) {
                            int gm = (int)SendMessageW(g_app.hCmbGrouping, CB_GETCURSEL, 0, 0);
                            int min_req = min_valid_frames_win();
                            if (gm != 1 && _wtoi(get_wnd_text(g_app.hEdtGrpSz).c_str()) < min_req) {
                                g_app.grouping_dirty = true;
                                sync_cross_tab_ui();
                                SetWindowTextW(g_app.hLblGroupingHint, heraw_i18n::trf_w("hint.min_grp_sz_fmt", min_req).c_str());
                            } else {
                                trigger_async_plan_scan();
                            }
                        } else {
                            g_app.grouping_dirty = false;
                            sync_cross_tab_ui();
                        }
                    }
                    return 0;
                case ID_BTN_REGROUP: {
                    int min_req = min_valid_frames_win();
                    if (SendMessageW(g_app.hCmbGrouping, CB_GETCURSEL, 0, 0) != 1 &&
                        _wtoi(get_wnd_text(g_app.hEdtGrpSz).c_str()) < min_req) {
                        g_app.grouping_dirty = true;
                        sync_cross_tab_ui();
                        SetWindowTextW(g_app.hLblGroupingHint, heraw_i18n::trf_w("hint.min_grp_sz_fmt", min_req).c_str());
                        return 0;
                    }
                    save_ui_prefs();
                    trigger_async_plan_scan();
                    return 0;
                }
                case ID_BTN_INTAKE_PARAMS:
                    show_intake_params_win(hwnd);
                    return 0;
                case ID_BTN_INTAKE_ACTION:
                    trigger_intake_action_win();
                    return 0;
                case ID_BTN_MERGE_GROUPS:
                case ID_QMENU_MERGE:
                    merge_selected_groups_win();
                    return 0;
                case ID_BTN_CLEAN_STRAY:
                case ID_QMENU_CLEAN_STRAY:
                    clean_stray_groups_win();
                    return 0;
                case ID_BTN_SPLIT_FRAMES:
                case ID_FMENU_SPLIT:
                    split_selected_frames_win();
                    return 0;
                case ID_QMENU_TOGGLE_CHK:
                    batch_toggle_selected_groups_checked();
                    return 0;
                case ID_QMENU_ONLY_BEST:
                    select_only_sharpest_group_win();
                    sync_cross_tab_ui();
                    repaint_left_dock_views();
                    return 0;
                case ID_QMENU_CHECK_VALID: {
                    int min_req = min_valid_frames_win();
                    for (auto& g : g_app.groups) g.checked = (!g.is_stray_archive && g.valid_frame_count >= min_req);
                    sync_cross_tab_ui();
                    repaint_left_dock_views();
                    return 0;
                }
                case ID_QMENU_SELECT_ALL:
                    if (!g_app.groups.empty()) {
                        g_app.group_sel.assign(g_app.groups.size(), 1);
                        sync_cross_tab_ui();
                        repaint_left_dock_views();
                    }
                    return 0;
                case ID_QMENU_REMOVE:
                    remove_selected_groups_win();
                    return 0;
                case ID_FMENU_TOGGLE_INC:
                    batch_toggle_selected_frames_included();
                    return 0;
                case ID_FMENU_KEEP_ONLY: {
                    BurstGroupItem* grp = current_group_ptr();
                    if (grp) {
                        normalize_selection_masks();
                        for (size_t i = 0; i < grp->frames.size(); ++i) {
                            grp->frames[i].included = (i < g_app.frame_sel.size() && g_app.frame_sel[i] != 0);
                        }
                        grp->locked_anchor_1based = 0;
                        grp->parallax_measured = false;
                        grp->depth_known = false;
                        grp->depth_observable = false;
                        grp->field_observability.clear();
                        recalculate_group_item(*grp, g_app.selected_group + 1);
                        sync_cross_tab_ui();
                        repaint_left_dock_views();
                    }
                    return 0;
                }
                case ID_FMENU_RESTORE_ALL: {
                    BurstGroupItem* grp = current_group_ptr();
                    if (grp) {
                        for (auto& f : grp->frames) {
                            f.included = (f.flags.find(L"DARK") == std::wstring::npos);
                        }
                        grp->parallax_measured = false;
                        grp->depth_known = false;
                        grp->depth_observable = false;
                        grp->field_observability.clear();
                        recalculate_group_item(*grp, g_app.selected_group + 1);
                        sync_cross_tab_ui();
                        repaint_left_dock_views();
                    }
                    return 0;
                }
                case ID_FMENU_LOCK_ANC:
                    lock_selected_frame_anchor(g_app.context_frame_idx >= 0 ? g_app.context_frame_idx : g_app.frame_sel_idx);
                    return 0;
                case ID_CHK_LCP_LCA:
                case ID_CHK_LCP_VIG:
                case ID_CHK_LCP_DIST:
                    sync_cross_tab_ui();
                    save_ui_prefs();
                    return 0;
                case ID_TAB_BAR:
                    switch_tab(seg_pill_get_sel(g_app.hTabBar));
                    save_ui_prefs();
                    return 0;

                case ID_BTN_PORTAL_TAB4_OPTICS:
                    switch_tab(2);
                    save_ui_prefs();
                    return 0;

                case ID_SEG_PRESET:
                    apply_preset_to_controls(seg_pill_get_sel(g_app.hSegPreset));
                    sync_cross_tab_ui();
                    InvalidateRect(g_app.hBurstView, nullptr, FALSE);
                    save_ui_prefs();
                    return 0;

                case ID_BTN_RESET_PRESET:
                    apply_preset_to_controls(g_app.preset_idx);
                    sync_cross_tab_ui();
                    save_ui_prefs();
                    set_status_ui(heraw_i18n::I18nManager::instance().is_english()
                        ? L"Preset default tuning restored" : L"已恢复当前预设默认精调参数");
                    return 0;

                case ID_BTN_RESET_OPTICS:
                    reset_optics_controls_win();
                    sync_cross_tab_ui();
                    save_ui_prefs();
                    return 0;

                case ID_RAD_T2_MOT_STD:
                    if (g_app.motion_mode_idx > 2) g_app.motion_mode_idx = 0;
                    sync_cross_tab_ui();
                    save_ui_prefs();
                    return 0;
                case ID_SEG_T2_ANCHOR: {
                    int sub = seg_pill_get_sel(g_app.hSegT2Anchor);
                    BurstGroupItem* grp = current_group_ptr();
                    if (sub >= 0 && sub <= 2) {
                        g_app.motion_mode_idx = sub;
                        if (grp) grp->locked_anchor_1based = 0;
                    } else {
                        g_app.motion_mode_idx = 0;
                        if (grp && grp->locked_anchor_1based == 0 && !grp->frames.empty()) {
                            grp->locked_anchor_1based = grp->best_anchor_1based;
                        }
                    }
                    sync_cross_tab_ui();
                    InvalidateRect(g_app.hFrameView, nullptr, FALSE);
                    save_ui_prefs();
                    return 0;
                }
                case ID_RAD_T2_MOT_CLEAN:
                    g_app.motion_mode_idx = 3;
                    sync_cross_tab_ui();
                    save_ui_prefs();
                    return 0;
                case ID_RAD_T2_MOT_VND:
                    g_app.motion_mode_idx = 4;
                    sync_cross_tab_ui();
                    save_ui_prefs();
                    return 0;
                case ID_RAD_T2_MOT_STROBE:
                    g_app.motion_mode_idx = 5;
                    sync_cross_tab_ui();
                    save_ui_prefs();
                    return 0;
                case ID_RAD_T2_MOT_FSTACK:
                    g_app.motion_mode_idx = 6;
                    sync_cross_tab_ui();
                    save_ui_prefs();
                    return 0;

                case ID_CHK_TRANSIENT_TAB2:
                    sync_cross_tab_ui();
                    save_ui_prefs();
                    return 0;

                case ID_BTN_FIELDS_BASIC3:
                    for (int i = 0; i < kNumFields; ++i) set_chk_val(g_app.hChkFields[i], i < 3);
                    for (int i = 0; i < kNumExtFields; ++i) set_chk_val(g_app.hChkExtFields[i], false);
                    sync_cross_tab_ui();
                    save_ui_prefs();
                    return 0;
                case ID_BTN_FIELDS_ALL:
                    for (int i = 0; i < kNumFields; ++i) set_chk_val(g_app.hChkFields[i], true);
                    for (int i = 0; i < kNumExtFields; ++i) set_chk_val(g_app.hChkExtFields[i], true);
                    sync_cross_tab_ui();
                    save_ui_prefs();
                    return 0;
                case ID_BTN_FIELDS_NONE:
                    for (int i = 0; i < kNumFields; ++i) set_chk_val(g_app.hChkFields[i], false);
                    for (int i = 0; i < kNumExtFields; ++i) set_chk_val(g_app.hChkExtFields[i], false);
                    sync_cross_tab_ui();
                    save_ui_prefs();
                    return 0;
                case ID_SEG_FIELD_BANK:
                    update_field_bank_visibility_win();
                    if (g_app.hwnd) {
                        RECT crc{};
                        GetClientRect(g_app.hwnd, &crc);
                        InvalidateRect(g_app.hwnd, &crc, FALSE);
                    }
                    return 0;
                case ID_BTN_OBS_ALL:
                    for (int i = 0; i < kNumObs; ++i) set_chk_val(g_app.hChkObs[i], true);
                    if (g_app.hChkWriteObsReport) set_chk_val(g_app.hChkWriteObsReport, true);
                    sync_cross_tab_ui();
                    save_ui_prefs();
                    return 0;
                case ID_BTN_OBS_NONE:
                    for (int i = 0; i < kNumObs; ++i) set_chk_val(g_app.hChkObs[i], false);
                    if (g_app.hChkWriteObsReport) set_chk_val(g_app.hChkWriteObsReport, false);
                    sync_cross_tab_ui();
                    save_ui_prefs();
                    return 0;

                case ID_CHK_SORT_TIME:
                case ID_CHK_RECURSIVE:
                    save_ui_prefs();
                    if (!g_app.inputs.empty()) trigger_async_plan_scan();
                    return 0;

                case ID_CMB_OLPF_MODE:
                case ID_CMB_RAW_FMT:
                case ID_CMB_RGB_FMT:
                case ID_CMB_FIELD_MODE:
                case ID_CMB_BACKEND:
                case ID_CMB_CPU_CORES:
                case ID_CMB_PRIORITY:
                case ID_CMB_MOTION_PROFILE:
                case ID_CMB_RGB1X_TIER:
                case ID_CMB_EXR_PREC:
                case ID_CMB_EXR_PARTS:
                case ID_CMB_CODEC_CONSTRAINT:
                case ID_CMB_SAT_MODEL:
                case ID_CMB_HIGHLIGHT_MODE:
                case ID_CMB_STREAMING:
                case ID_CMB_SHUTTER_MODE:
                case ID_CMB_INTRA_DEBLUR:
                case ID_CMB_DIFFUSION_MODE:
                case ID_CMB_UC_GRADE:
                case ID_CMB_UC_BAFFLE:
                case ID_CMB_UC_CHROMA:
                case ID_CMB_APERTURE_WEIGHT:
                case ID_CMB_METRIC_POLICY:
                case ID_CMB_SPECTRAL_CA:
                case ID_CMB_SPECTRAL_ANG:
                case ID_CMB_IBP_ITERS:
                case ID_CMB_W2_DEPTH_GATE:
                case ID_CMB_DEPTH_GATE:
                case ID_CMB_REFOCUS_MODE:
                case ID_CMB_BOKEH_BLADES:
                case ID_CMB_SEE_THROUGH:
                case ID_CMB_OBS_GATE:
                    if (code == CBN_SELCHANGE) {
                        if (id == ID_CMB_BACKEND || id == ID_CMB_CPU_CORES || id == ID_CMB_PRIORITY) {
                            stop_perf_breathing_alert(hwnd);
                        }
                        if (id == ID_CMB_IBP_ITERS) {
                            set_chk_val(g_app.hChkIbpOn, get_ibp_iters_win() > 0);
                        }
                        if (id == ID_CMB_W2_DEPTH_GATE && g_app.hCmbDepthGate) {
                            SendMessageW(g_app.hCmbDepthGate, CB_SETCURSEL, SendMessageW(g_app.hCmbW2DepthGate, CB_GETCURSEL, 0, 0), 0);
                        } else if (id == ID_CMB_DEPTH_GATE && g_app.hCmbW2DepthGate) {
                            SendMessageW(g_app.hCmbW2DepthGate, CB_SETCURSEL, SendMessageW(g_app.hCmbDepthGate, CB_GETCURSEL, 0, 0), 0);
                        }
                        if (id == ID_CMB_RGB_FMT) {
                            std::wstring rc = selected_rgb_fmt_code();
                            if (rc != L"tiff-deflate" && rc != L"tiff-hq" && rc != L"tiff-raw" &&
                                SendMessageW(g_app.hCmbFieldMode, CB_GETCURSEL, 0, 0) == 0) {
                                SendMessageW(g_app.hCmbFieldMode, CB_SETCURSEL, 1, 0);
                            }
                        }
                        InvalidateRect(reinterpret_cast<HWND>(lParam), nullptr, FALSE);
                        sync_cross_tab_ui();
                        save_ui_prefs();
                    }
                    return 0;

                case ID_CHK_EMIT_RAW:
                case ID_CHK_RELAY_RAW:
                case ID_CHK_NO_TOOL_EXIF:
                case ID_CHK_EMIT_RGB:
                case ID_SEG_RGB_SCALE:
                case ID_RAD_CS_SRGB:
                case ID_RAD_CS_ADOBERGB:
                case ID_CHK_EMIT_FIELD:
                case ID_CHK_SILICON_CLEAN:
                case ID_CHK_IBP_ON:
                case ID_CHK_EDGE_POLISH:
                case ID_CHK_AUTO_LCA:
                case ID_CHK_AUTO_EXP:
                case ID_CHK_ROLLOFF:
                case ID_CHK_DITHER:
                case ID_SEG_STROBE_FADE:
                case ID_SEG_TRANS_COLOR:
                case ID_CHK_REFL_ON:
                case ID_SEG_REFL_MODE:
                case ID_CHK_EXPORT_PARALLAX:
                case ID_CHK_EXPORT_REFOCUS:
                case ID_CHK_REFOCUS_AUTO:
                case ID_CHK_EXPORT_TILES:
                case ID_SEG_TILE_SIZE:
                case ID_CHK_WRITE_OBS_REP:
                case ID_CHK_NEXT_SHOT_PLAN:
                case ID_CHK_EMIT_DIFFUSE:
                case ID_CHK_EMIT_LSPLIT:
                case ID_CHK_EVAL_SELF:
                case ID_CHK_BUS_EXPORT:
                    sync_cross_tab_ui();
                    save_ui_prefs();
                    return 0;

                case ID_BTN_BUS_BROWSE: {
                    std::wstring chosen = pick_single_file_dialog(
                        hwnd, heraw_i18n::tr_w("t3.bus_cap").c_str(),
                        L"NOPB Bus Bundle (*.nib)\0*.nib\0All Files (*.*)\0*.*\0"
                    );
                    if (!chosen.empty()) {
                        commit_bus_in_edit_win(chosen);
                        sync_cross_tab_ui();
                        save_ui_prefs();
                    }
                    return 0;
                }
                case ID_BTN_BUS_CLEAR:
                    commit_bus_in_edit_win(L"");
                    sync_cross_tab_ui();
                    save_ui_prefs();
                    return 0;

                case ID_BTN_ADD_FILES:
                    pick_raw_files_dialog(hwnd);
                    return 0;
                case ID_BTN_ADD_FOLDER: {
                    std::wstring d = pick_folder_dialog(hwnd, heraw_i18n::tr_w("dlg.pick_raw_dir").c_str());
                    if (!d.empty()) {
                        add_unique_input(d);
                        trigger_async_plan_scan();
                    }
                    return 0;
                }
                case ID_BTN_EXPORT_TOPK:
                    start_topk_export_job();
                    return 0;
                case ID_BTN_CLEAR:
                    g_app.inputs.clear();
                    g_app.default_bus_in_path.clear();
                    if (!g_app.auto_out_dir.empty() && get_wnd_text(g_app.hOutDir) == g_app.auto_out_dir) {
                        g_app.suppress_sync = true;
                        SetWindowTextW(g_app.hOutDir, L"");
                        g_app.suppress_sync = false;
                    }
                    g_app.auto_out_dir.clear();
                    trigger_async_plan_scan();
                    set_status_ui(heraw_i18n::tr_w("st.cleared"));
                    return 0;
                case ID_BTN_LOCK_FRAME:
                    lock_selected_frame_anchor(g_app.frame_sel_idx);
                    return 0;

                case ID_BTN_OUT_BROWSE: {
                    std::wstring d = pick_folder_dialog(hwnd, heraw_i18n::tr_w("t1.outdir_cap").c_str());
                    if (!d.empty()) {
                        g_app.auto_out_dir.clear();
                        SetWindowTextW(g_app.hOutDir, d.c_str());
                    }
                    return 0;
                }
                case ID_BTN_OUT_CLEAR:
                    g_app.auto_out_dir.clear();
                    SetWindowTextW(g_app.hOutDir, L"");
                    return 0;

                case ID_BTN_PERF_HEADER:
                    show_engine_popup_menu(hwnd, g_app.hBtnPerfHeader, true);
                    return 0;
                case ID_BTN_ENGINE_MENU:
                    show_engine_popup_menu(hwnd, g_app.hBtnEngineMenu, true);
                    return 0;
                case ID_BTN_BRAND_HEADER:
                case ID_MENU_ABOUT:
                    show_welcome_or_about_win(hwnd, false);
                    return 0;
                case ID_MENU_WELCOME_SETUP:
                    show_welcome_or_about_win(hwnd, true);
                    return 0;
                case ID_MENU_BACKEND_AUTO:
                case ID_MENU_BACKEND_GPU:
                case ID_MENU_BACKEND_CPU:
                    SendMessageW(g_app.hCmbBackend, CB_SETCURSEL, (WPARAM)(id - ID_MENU_BACKEND_AUTO), 0);
                    sync_cross_tab_ui();
                    save_ui_prefs();
                    return 0;
                case ID_MENU_CORES_AUTO:
                case ID_MENU_CORES_PERF:
                case ID_MENU_CORES_PHYS:
                case ID_MENU_CORES_EFF:
                case ID_MENU_CORES_ALL:
                    SendMessageW(g_app.hCmbCpuCores, CB_SETCURSEL, (WPARAM)(id - ID_MENU_CORES_AUTO), 0);
                    sync_cross_tab_ui();
                    save_ui_prefs();
                    return 0;
                case ID_MENU_BATTERY_DEVICE: {
                    g_app.is_battery_device = !g_app.is_battery_device;
                    int cur_c = g_app.hCmbCpuCores ? (int)SendMessageW(g_app.hCmbCpuCores, CB_GETCURSEL, 0, 0) : 0;
                    if (g_app.is_battery_device && cur_c == 0) {
                        SendMessageW(g_app.hCmbCpuCores, CB_SETCURSEL, 2, 0);
                    } else if (!g_app.is_battery_device && cur_c == 2) {
                        SendMessageW(g_app.hCmbCpuCores, CB_SETCURSEL, 0, 0);
                    }
                    sync_cross_tab_ui();
                    save_ui_prefs();
                    return 0;
                }
                case ID_MENU_PRIO_NORMAL:
                case ID_MENU_PRIO_HIGH:
                case ID_MENU_PRIO_BELOW:
                case ID_MENU_PRIO_IDLE:
                    SendMessageW(g_app.hCmbPriority, CB_SETCURSEL, (WPARAM)(id - ID_MENU_PRIO_NORMAL), 0);
                    sync_cross_tab_ui();
                    save_ui_prefs();
                    return 0;
                case ID_MENU_STREAM_AUTO:
                case ID_MENU_STREAM_ON:
                case ID_MENU_STREAM_OFF:
                    SendMessageW(g_app.hCmbStreaming, CB_SETCURSEL, (WPARAM)(id - ID_MENU_STREAM_AUTO), 0);
                    sync_cross_tab_ui();
                    save_ui_prefs();
                    return 0;
                case ID_MENU_MEM_2G:
                case ID_MENU_MEM_4G:
                case ID_MENU_MEM_8G:
                case ID_MENU_MEM_16G: {
                    static const wchar_t* kMemStrs[4] = {L"2", L"4", L"8", L"16"};
                    SetWindowTextW(g_app.hEdtMemBudget, kMemStrs[id - ID_MENU_MEM_2G]);
                    sync_cross_tab_ui();
                    save_ui_prefs();
                    return 0;
                }
                case ID_MENU_LANG_LOAD_FILE: {
                    std::wstring p = pick_single_file_dialog(
                        hwnd, heraw_i18n::tr_w("menu.language").c_str(),
                        L"Language Pack (*.lng)\0*.lng\0All Files (*.*)\0*.*\0"
                    );
                    if (file_exists_regular(p)) {
                        std::string u8p = wide_to_utf8(p);
                        if (heraw_i18n::I18nManager::instance().load_lng_file(u8p)) {
                            std::string stem = wide_to_utf8(stem_of_w(basename_of_w(p)));
                            auto langs = heraw_i18n::I18nManager::instance().available_languages();
                            if (!langs.empty()) {
                                bool matched = heraw_i18n::I18nManager::instance().set_language(stem);
                                if (!matched) heraw_i18n::I18nManager::instance().set_language(langs.back().first);
                            }
                            heraw_i18n::diag_log().log_event("UI", "Loaded external language pack: " + u8p);
                            apply_ui_language_win();
                            save_ui_prefs();
                        }
                    }
                    return 0;
                }
                case ID_BTN_EXPORT_LOG:
                case ID_MENU_EXPORT_LOG:
                    export_session_log_dialog(hwnd);
                    return 0;
                case ID_MENU_CLI_BROWSE: {
                    std::wstring p = pick_single_file_dialog(
                        hwnd, heraw_i18n::tr_w("dlg.pick_cli").c_str(),
                        L"CLI Engine (nijinLBS.exe;*.exe)\0nijinLBS.exe;*.exe\0All Files (*.*)\0*.*\0"
                    );
                    if (file_exists_regular(p)) {
                        g_app.cli_path = p;
                        reg_write_str(L"CliPath", p);
                        refresh_engine_status_label();
                        if (!g_app.inputs.empty()) trigger_async_plan_scan();
                    }
                    return 0;
                }
                case ID_MENU_CLI_AUTO: {
                    std::wstring det = auto_detect_cli_exe(false);
                    if (!det.empty()) {
                        g_app.cli_path = det;
                        reg_write_str(L"CliPath", det);
                        refresh_engine_status_label();
                        set_status_ui((heraw_i18n::I18nManager::instance().is_english() ? L"CLI detected: " : L"已自动定位引擎: ") + det);
                    } else {
                        resolve_active_cli_or_prompt(hwnd);
                    }
                    return 0;
                }
                case ID_MENU_LIBRAW_BROWSE: {
                    std::wstring p = pick_single_file_dialog(
                        hwnd, heraw_i18n::tr_w("dlg.pick_lr").c_str(),
                        L"LibRaw DLL (*.dll)\0*.dll\0All Files (*.*)\0*.*\0"
                    );
                    if (file_exists_regular(p)) {
                        g_app.libraw_path = p;
                        reg_write_str(L"LibRawPath", p);
                        refresh_engine_status_label();
                    }
                    return 0;
                }
                case ID_MENU_LIBRAW_RESET:
                    g_app.libraw_path.clear();
                    reg_write_str(L"LibRawPath", L"");
                    refresh_engine_status_label();
                    return 0;

                case ID_BTN_RUN_OPTICS:
                    g_app.optics_export_path.clear();
                    g_app.optics_png_export_path.clear();
                    start_optics_probe_job();
                    return 0;

                case ID_BTN_EXPORT_LCP: {
                    if (g_app.running) return 0;
                    if (build_lcp_fields_csv().empty()) return 0;
                    BurstGroupItem* grp = current_group_ptr();
                    std::vector<std::wstring> files = (grp && !grp->paths.empty()) ? grp->paths : g_app.inputs;
                    if (files.empty()) {
                        set_status_ui(heraw_i18n::tr_w("st.alert_select_group"));
                        return 0;
                    }
                    std::wstring exe = resolve_active_cli_or_prompt(hwnd);
                    if (exe.empty()) return 0;
                    std::wstring dst = save_single_file_dialog(
                        hwnd, heraw_i18n::tr_w("dlg.save_lcp").c_str(),
                        L"Adobe Lens Profile (*.lcp)\0*.lcp\0All Files (*.*)\0*.*\0",
                        L"lcp", L"nijinLBS_lens_profile.lcp"
                    );
                    if (dst.empty()) return 0;
                    wchar_t tmpDir[MAX_PATH] = {0};
                    GetTempPathW(MAX_PATH - 1, tmpDir);
                    g_app.lcp_tmp_prefix = std::wstring(tmpDir) + L"nijinLBS_lcp_" +
                        std::to_wstring(GetCurrentProcessId()) + L"_" + std::to_wstring(GetTickCount64());
                    g_app.lcp_export_dst = dst;
                    std::vector<std::wstring> args = {exe};
                    if (!g_app.libraw_path.empty() && file_exists_regular(g_app.libraw_path)) {
                        args.push_back(L"--libraw");
                        args.push_back(g_app.libraw_path);
                    }
                    append_perf_cli_args(args);
                    args.push_back(L"--no-sort-time");
                    auto options = lcp_arguments(dst);
                    args.insert(args.end(), options.begin(), options.end());
                    args.push_back(L"--optics-report");
                    args.push_back(g_app.lcp_tmp_prefix);
                    std::wstring skip_csv = build_skip_frames_csv(grp);
                    if (!skip_csv.empty()) { args.push_back(L"--skip-frames"); args.push_back(skip_csv); }
                    for (const auto& f : files) args.push_back(f);
                    g_app.total_synthesis_jobs = 0;
                    g_app.active_job_group_idx = -1;
                    launch_async_cli_job(args, heraw_i18n::tr_w("st.lcp_running"));
                    return 0;
                }

                case ID_BTN_EXPORT_PNG_CARD: {
                    if (g_app.running) return 0;
                    std::wstring dst = save_single_file_dialog(
                        hwnd, heraw_i18n::tr_w("dlg.save_png").c_str(),
                        L"PNG Image (*.png)\0*.png\0All Files (*.*)\0*.*\0",
                        L"png", L"nijinLBS_optics_passport.png"
                    );
                    if (dst.empty()) return 0;
                    if (!g_app.optics.measured) {
                        BurstGroupItem* grp = current_group_ptr();
                        std::vector<std::wstring> files = (grp && !grp->paths.empty()) ? grp->paths : g_app.inputs;
                        if (!files.empty()) {
                            g_app.optics_png_export_path = dst;
                            start_optics_probe_job();
                            return 0;
                        }
                    }
                    if (export_optics_card_png(dst)) {
                        set_status_ui(heraw_i18n::trf_w("st.png_saved_fmt", wide_to_utf8(dst).c_str()));
                    } else {
                        set_status_ui(heraw_i18n::tr_w("st.png_err"));
                    }
                    return 0;
                }

                case ID_BTN_EXPORT_JSON_TEL: {
                    if (g_app.running) return 0;
                    std::wstring dst = save_single_file_dialog(
                        hwnd, heraw_i18n::tr_w("dlg.save_json").c_str(),
                        L"JSON Telemetry (*.json)\0*.json\0All Files (*.*)\0*.*\0",
                        L"json", L"nijinLBS_telemetry.json"
                    );
                    if (dst.empty()) return 0;
                    if (!g_app.optics.raw_json.empty()) {
                        set_status_ui(write_utf8_file(dst, g_app.optics.raw_json) ?
                            heraw_i18n::trf_w("st.json_saved_fmt", wide_to_utf8(dst).c_str()) : heraw_i18n::tr_w("st.json_err"));
                    } else {
                        BurstGroupItem* grp = current_group_ptr();
                        std::vector<std::wstring> files = (grp && !grp->paths.empty()) ? grp->paths : g_app.inputs;
                        if (files.empty()) {
                            set_status_ui(heraw_i18n::tr_w("st.alert_select_group"));
                            return 0;
                        }
                        g_app.optics_export_path = dst;
                        start_optics_probe_job();
                    }
                    return 0;
                }

                case ID_BTN_RUN: {
                    if (g_app.running.load()) {
                        g_app.cancel_job = true;
                        std::lock_guard<std::mutex> lk(g_app.proc_mtx);
                        if (g_app.hOpticsProc) TerminateProcess(g_app.hOpticsProc, 130);
                        if (g_app.hChildProc) {
                            TerminateProcess(g_app.hChildProc, 130);
                            set_status_ui(heraw_i18n::tr_w("st.task_stopped"));
                        }
                    } else {
                        start_synthesis_job();
                    }
                    return 0;
                }

                case ID_BTN_OPEN_OUT: {
                    std::wstring d = ensure_output_dir();
                    if (!d.empty()) {
                        CreateDirectoryW(d.c_str(), nullptr);
                        ShellExecuteW(hwnd, L"open", d.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
                    }
                    return 0;
                }
            }
            break;
        }

        case WM_APP_STATUS: {
            auto* s = reinterpret_cast<std::wstring*>(lParam);
            if (s) {
                set_status_ui(*s);
                delete s;
            }
            return 0;
        }

        case WM_APP_PROGRESS: {
            int p = std::clamp((int)wParam, 0, 100);
            if (g_app.total_synthesis_jobs > 1) {
                p = ((g_app.completed_synthesis_jobs + g_app.skipped_synthesis_jobs) * 100 + p) / g_app.total_synthesis_jobs;
            }
            set_prog_pct(p);
            return 0;
        }

        case WM_APP_DONE: {
            int rc = (int)wParam;
            if (g_app.cancel_job) rc = 130;
            bool was_lcp = !g_app.lcp_tmp_prefix.empty();
            std::wstring lcp_saved_dst = g_app.lcp_export_dst;
            if (!g_app.lcp_tmp_prefix.empty()) {
                std::wstring tel_json = g_app.lcp_tmp_prefix + L"_telemetry.json";
                if (rc == 0 && file_exists_regular(tel_json)) {
                    OpticsTelemetry parsed_tel = parse_optics_telemetry_json(read_whole_file_utf8(tel_json), tel_json);
                    if (parsed_tel.measured) {
                        g_app.optics = std::move(parsed_tel);
                        SetWindowTextW(g_app.hLblOpticsStatus, g_app.optics.ois_note.c_str());
                        SetWindowTextW(g_app.hLblSiliconStats, silicon_readout(g_app.optics).c_str());
                        InvalidateRect(g_app.hOpticsCanvas, nullptr, FALSE);
                    }
                }
                DeleteFileW(tel_json.c_str());
                DeleteFileW((g_app.lcp_tmp_prefix + L"_passport.png").c_str());
                g_app.lcp_tmp_prefix.clear();
                g_app.lcp_export_dst.clear();
            }
            std::string raw_cli_out;
            {
                std::lock_guard<std::mutex> lk(g_app.proc_mtx);
                raw_cli_out = g_app.last_cli_output;
            }
            std::string reason_u8 = heraw_i18n::SessionDiagLog::extract_failure_reason(raw_cli_out);

            if (rc == 0 && g_app.active_job_group_idx >= 0) {
                refresh_group_from_fields_json_win(g_app.active_out_dir, g_app.active_job_group_idx, g_app.job_start_ft);
                ++g_app.completed_synthesis_jobs;
            } else if (rc == 3 && g_app.total_synthesis_jobs > 1 && !g_app.pending_jobs.empty()) {
                int skipped_ord = g_app.active_job_group_idx >= 0 ? (g_app.active_job_group_idx + 1) : 1;
                ++g_app.skipped_synthesis_jobs;
                auto next = std::move(g_app.pending_jobs.front());
                g_app.pending_jobs.erase(g_app.pending_jobs.begin());
                if (!g_app.pending_group_indices.empty()) {
                    g_app.active_job_group_idx = g_app.pending_group_indices.front();
                    g_app.pending_group_indices.erase(g_app.pending_group_indices.begin());
                } else {
                    g_app.active_job_group_idx = -1;
                }
                GetSystemTimeAsFileTime(&g_app.job_start_ft);
                set_running_ui(false);
                launch_async_cli_job(next, heraw_i18n::trf_w("st.group_skipped_fmt", skipped_ord, reason_u8.c_str()), false);
                return 0;
            }
            if (rc == 0 && !g_app.pending_jobs.empty()) {
                auto next = std::move(g_app.pending_jobs.front());
                g_app.pending_jobs.erase(g_app.pending_jobs.begin());
                if (!g_app.pending_group_indices.empty()) {
                    g_app.active_job_group_idx = g_app.pending_group_indices.front();
                    g_app.pending_group_indices.erase(g_app.pending_group_indices.begin());
                } else {
                    g_app.active_job_group_idx = -1;
                }
                GetSystemTimeAsFileTime(&g_app.job_start_ft);
                set_running_ui(false);
                launch_async_cli_job(next, heraw_i18n::trf_w("st.group_starting_fmt",
                    g_app.completed_synthesis_jobs + g_app.skipped_synthesis_jobs + 1, g_app.total_synthesis_jobs), false);
                return 0;
            }
            int done_cnt = g_app.completed_synthesis_jobs;
            int skip_cnt = g_app.skipped_synthesis_jobs;
            int tot_cnt  = g_app.total_synthesis_jobs;
            int failed_grp = g_app.active_job_group_idx;
            g_app.pending_jobs.clear();
            g_app.pending_group_indices.clear();
            g_app.active_job_group_idx = -1;
            g_app.total_synthesis_jobs = 0;
            g_app.completed_synthesis_jobs = 0;
            g_app.skipped_synthesis_jobs = 0;
            set_running_ui(false);
            if (rc == 0) {
                set_prog_pct(100);
                if (was_lcp && !lcp_saved_dst.empty()) {
                    set_status_ui(heraw_i18n::trf_w("st.lcp_done_fmt", wide_to_utf8(lcp_saved_dst).c_str()));
                } else if (tot_cnt > 0 && skip_cnt > 0) {
                    set_status_ui(heraw_i18n::trf_w("st.all_done_partial_fmt", done_cnt, tot_cnt, skip_cnt));
                } else if (tot_cnt > 0) {
                    set_status_ui(heraw_i18n::trf_w("st.all_done_fmt", done_cnt, wide_to_utf8(g_app.active_out_dir).c_str()));
                } else {
                    set_status_ui(heraw_i18n::tr_w("st.done"));
                }
            } else if (rc == 130) {
                set_status_ui(heraw_i18n::tr_w("st.stopped_exec"));
            } else if (rc == 3 && tot_cnt > 1 && done_cnt > 0) {
                set_prog_pct(100);
                set_status_ui(heraw_i18n::trf_w("st.all_done_partial_fmt", done_cnt, tot_cnt, skip_cnt + 1));
            } else if (failed_grp >= 0) {
                set_status_ui(heraw_i18n::trf_w("st.group_aborted_fmt", failed_grp + 1, rc, reason_u8.c_str()));
            } else {
                set_status_ui(heraw_i18n::trf_w("st.subtask_err_fmt", rc, reason_u8.c_str()));
            }
            return 0;
        }

        case WM_APP_PLAN_READY: {
            auto* parsed = reinterpret_cast<std::vector<BurstGroupItem>*>(lParam);
            uint64_t gen = (uint64_t)wParam;
            if (parsed) {
                if (gen == g_app.plan_gen.load()) {
                    g_app.plan_scanning.store(false);
                    if (!parsed->empty()) {
                        g_app.groups = std::move(*parsed);
                        if (SendMessageW(g_app.hCmbGrouping, CB_GETCURSEL, 0, 0) == 3) {
                            select_only_sharpest_group_win();
                        }
                        int first_active = 0;
                        for (size_t gi = 0; gi < g_app.groups.size(); ++gi) {
                            if (g_app.groups[gi].checked) { first_active = (int)gi; break; }
                        }
                        select_single_group(first_active);
                        set_status_ui(heraw_i18n::trf_w("st.ready_groups_fmt", (int)g_app.groups.size()));
                    } else {
                        g_app.grouping_dirty = true;
                        set_status_ui(heraw_i18n::trf_w("st.plan_failed_fmt", 1));
                    }
                    sync_cross_tab_ui();
                    InvalidateRect(g_app.hBurstView, nullptr, FALSE);
                    InvalidateRect(g_app.hFrameView, nullptr, FALSE);
                    InvalidateRect(g_app.hOpticsCanvas, nullptr, FALSE);
                    InvalidateRect(g_app.hParallaxMeter, nullptr, FALSE);
                }
                delete parsed;
            }
            return 0;
        }

        case WM_APP_OPTICS_READY: {
            set_running_ui(false);
            auto* tel = reinterpret_cast<OpticsTelemetry*>(lParam);
            EnableWindow(g_app.hBtnRunOptics, TRUE);
            bool exported = !g_app.optics_export_path.empty() || !g_app.optics_png_export_path.empty();
            if (!g_app.optics_export_path.empty()) {
                bool saved = tel && !tel->raw_json.empty() && write_utf8_file(g_app.optics_export_path, tel->raw_json);
                set_status_ui(saved
                    ? heraw_i18n::trf_w("st.json_saved_fmt", wide_to_utf8(g_app.optics_export_path).c_str())
                    : heraw_i18n::tr_w("st.json_err"));
                g_app.optics_export_path.clear();
            }
            if ((int)wParam != g_app.selected_group) {
                g_app.optics_png_export_path.clear();
                if (tel && tel->passport_path.find(L"nijinLBS_optics_") != std::wstring::npos) {
                    DeleteFileW(tel->passport_path.c_str());
                }
                delete tel;
                return 0;
            }
            if (tel) {
                std::wstring tmp_passport = tel->passport_path;
                g_app.optics = std::move(*tel);
                delete tel;
                BurstGroupItem* group = current_group_ptr();
                if (group) {
                    group->parallax_measured = g_app.optics.parallax_measured;
                    if (group->parallax_measured) group->max_parallax_2x_px = g_app.optics.max_parallax_2x_px;
                    group->depth_known = g_app.optics.depth_known;
                    group->depth_observable = g_app.optics.depth_observable;
                    group->field_observability = g_app.optics.field_observability;
                    size_t measured = 0;
                    for (const auto& frame : g_app.optics.drift) {
                        if (frame.idx_1based < 1 || (size_t)frame.idx_1based > group->frames.size()) continue;
                        auto& target = group->frames[(size_t)frame.idx_1based - 1];
                        if (frame.included) {
                            target.dx = frame.dx; target.dy = frame.dy;
                            target.shift_px = std::hypot(frame.dx, frame.dy); ++measured;
                        }
                        target.has_time = frame.has_time;
                        target.time_seconds = frame.has_time ? frame.time_seconds : 0;
                    }
                    if (!group->frames.empty() && measured == group->frames.size()) group->has_telemetry = true;
                    if (g_app.optics.phase_quadrants.size() == 4) {
                        group->phase_quadrants = g_app.optics.phase_quadrants;
                        group->quad_coverage = (int)std::count_if(group->phase_quadrants.begin(), group->phase_quadrants.end(), [](int n) { return n > 0; });
                    }
                    if (g_app.optics.max_drift_px >= 0) group->max_drift_px = g_app.optics.max_drift_px;
                }
                SetWindowTextW(g_app.hLblOpticsStatus, g_app.optics.ois_note.c_str());
                SetWindowTextW(g_app.hLblSiliconStats, silicon_readout(g_app.optics).c_str());
                sync_cross_tab_ui();
                InvalidateRect(g_app.hOpticsCanvas, nullptr, FALSE);
                if (!g_app.optics_png_export_path.empty()) {
                    std::wstring png_dst = g_app.optics_png_export_path;
                    g_app.optics_png_export_path.clear();
                    bool ok_png = export_optics_card_png(png_dst);
                    set_status_ui(ok_png
                        ? heraw_i18n::trf_w("st.png_saved_fmt", wide_to_utf8(png_dst).c_str())
                        : heraw_i18n::tr_w("st.png_err"));
                } else if (!exported) {
                    if (g_app.cancel_job) {
                        set_status_ui(heraw_i18n::tr_w("st.stopped_exec"));
                    } else if (g_app.optics.measured) {
                        set_status_ui(heraw_i18n::trf_w("st.optics_done_fmt", wide_to_utf8(g_app.optics.grade_badge).c_str()));
                    } else {
                        std::string r_u8;
                        {
                            std::lock_guard<std::mutex> lk(g_app.proc_mtx);
                            r_u8 = heraw_i18n::SessionDiagLog::extract_failure_reason(g_app.last_cli_output);
                        }
                        set_status_ui(heraw_i18n::trf_w("st.subtask_err_fmt", 1, r_u8.c_str()));
                    }
                }
                if (tmp_passport.find(L"nijinLBS_optics_") != std::wstring::npos) {
                    DeleteFileW(tmp_passport.c_str());
                    g_app.optics.passport_path.clear();
                }
            }
            return 0;
        }

        case WM_CLOSE: {
            g_app.stopping = true;
            g_app.cancel_job = true;
            ++g_app.plan_gen;
            {
                std::lock_guard<std::mutex> lk(g_app.proc_mtx);
                if (g_app.hChildProc) TerminateProcess(g_app.hChildProc, 130);
                if (g_app.hPlanProc) TerminateProcess(g_app.hPlanProc, 130);
                if (g_app.hOpticsProc) TerminateProcess(g_app.hOpticsProc, 130);
            }
            save_ui_prefs();
            if (g_app.worker.joinable()) g_app.worker.join();
            if (g_app.plan_worker.joinable()) g_app.plan_worker.join();
            if (g_app.optics_worker.joinable()) g_app.optics_worker.join();
            MSG pending{};
            while (PeekMessageW(&pending, hwnd, WM_APP_STATUS, WM_APP_OPTICS_READY, PM_REMOVE)) {
                if (pending.message == WM_APP_STATUS) delete reinterpret_cast<std::wstring*>(pending.lParam);
                if (pending.message == WM_APP_PLAN_READY) delete reinterpret_cast<std::vector<BurstGroupItem>*>(pending.lParam);
                if (pending.message == WM_APP_OPTICS_READY) delete reinterpret_cast<OpticsTelemetry*>(pending.lParam);
            }
            DestroyWindow(hwnd);
            return 0;
        }

        case WM_DESTROY:
            KillTimer(hwnd, TIMER_PERF_BREATH);
            if (g_app.hFont) DeleteObject(g_app.hFont);
            if (g_app.hFontSmall) DeleteObject(g_app.hFontSmall);
            if (g_app.hFontBold) DeleteObject(g_app.hFontBold);
            if (g_app.hFontCardHdr) DeleteObject(g_app.hFontCardHdr);
            if (g_app.hFontTitle) DeleteObject(g_app.hFontTitle);
            if (g_app.hFontMajorVer) DeleteObject(g_app.hFontMajorVer);
            if (g_app.hFontVerTag) DeleteObject(g_app.hFontVerTag);
            if (g_app.hFontLink) DeleteObject(g_app.hFontLink);
            if (g_app.hFontLinkHdr) DeleteObject(g_app.hFontLinkHdr);
            if (g_app.hFontBadge) DeleteObject(g_app.hFontBadge);
            if (g_app.hFontMono) DeleteObject(g_app.hFontMono);
            if (g_app.hBrCanvas) DeleteObject(g_app.hBrCanvas);
            if (g_app.hBrCard) DeleteObject(g_app.hBrCard);
            if (g_app.hBrCardSub) DeleteObject(g_app.hBrCardSub);
            if (g_app.hBrWhite) DeleteObject(g_app.hBrWhite);
            if (g_app.hBrDisabledEdit) DeleteObject(g_app.hBrDisabledEdit);
            if (g_app.hBmpGxFallback) DeleteObject(g_app.hBmpGxFallback);
            if (g_app.hBmpNijinFallback) DeleteObject(g_app.hBmpNijinFallback);
            delete g_app.brandGx.bmp;
            g_app.brandGx.bmp = nullptr;
            delete g_app.brandNijin.bmp;
            g_app.brandNijin.bmp = nullptr;
            PostQuitMessage(0);
            return 0;
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

static void register_custom_classes(HINSTANCE hInst) {
    auto reg = [&](const wchar_t* name, WNDPROC proc, UINT extra_style = 0, LPCWSTR cur = IDC_ARROW) {
        WNDCLASSEXW wc{};
        wc.cbSize = sizeof(wc);
        wc.style = extra_style;
        wc.lpfnWndProc = proc;
        wc.hInstance = hInst;
        wc.hCursor = LoadCursorW(nullptr, cur);
        wc.lpszClassName = name;
        RegisterClassExW(&wc);
    };
    reg(L"NijiSegPill",      NijiSegPillWndProc,      0,          IDC_HAND);
    reg(L"NijiAquaSlider",   NijiSliderWndProc,       0,          IDC_HAND);
    reg(L"NijiBurstQueue",   NijiBurstQueueWndProc,   CS_DBLCLKS, IDC_HAND);
    reg(L"NijiFrameInsp",    NijiFrameInspWndProc,    CS_DBLCLKS, IDC_ARROW);
    reg(L"NijiParallaxBar",  NijiParallaxBarWndProc,  0,          IDC_ARROW);
    reg(L"NijiOpticsCanvas", NijiOpticsCanvasWndProc, 0,          IDC_ARROW);
    reg(L"NijiCreditsView",  NijiCreditsViewWndProc,  0,          IDC_ARROW);
    reg(L"NijiModalDlg",     NijiModalDlgWndProc,     0,          IDC_ARROW);
    reg(L"NijiIntakeDlg",    NijiIntakeDlgWndProc,    0,          IDC_ARROW);
}

static int run_prefs_selftest(HWND hwnd, ULONG_PTR gdip_tok) {
    heraw_i18n::I18nManager::instance().set_language("zh-CN");
    apply_ui_language_win();

    wchar_t raw_lbl[128] = {0}, rgb_lbl[128] = {0}, rgb_hq_lbl[128] = {0}, fm_lbl[128] = {0};
    SendMessageW(g_app.hCmbRawFmt,    CB_GETLBTEXT, 0, reinterpret_cast<LPARAM>(raw_lbl));
    SendMessageW(g_app.hCmbRgbFmt,    CB_GETLBTEXT, 0, reinterpret_cast<LPARAM>(rgb_lbl));
    SendMessageW(g_app.hCmbRgbFmt,    CB_GETLBTEXT, 1, reinterpret_cast<LPARAM>(rgb_hq_lbl));
    SendMessageW(g_app.hCmbFieldMode, CB_GETLBTEXT, 0, reinterpret_cast<LPARAM>(fm_lbl));

    bool defaults_ok =
        get_chk(g_app.hChkRaw) &&
        get_chk(g_app.hChkRgb) &&
        (seg_pill_get_sel(g_app.hSegRgbScale) == 0) &&
        (min_valid_frames_win() == 5) &&
        (SendMessageW(g_app.hCmbRawFmt, CB_GETCURSEL, 0, 0) == 0) &&
        (std::wstring(raw_lbl) == L"DNG 16-bit 无损") &&
        (SendMessageW(g_app.hCmbRgbFmt, CB_GETCURSEL, 0, 0) == 0) &&
        (std::wstring(rgb_lbl) == L"16-bit 无损 TIFF") &&
        (std::wstring(rgb_hq_lbl).find(L"HQ") != std::wstring::npos) &&
        (SendMessageW(g_app.hCmbFieldMode, CB_GETCURSEL, 0, 0) == 1) &&
        (build_fields_csv() == L"motion,phase,focus") &&
        get_chk(g_app.hChkLcpLca) && !get_chk(g_app.hChkLcpVig) && !get_chk(g_app.hChkLcpDist) &&
        !get_chk(g_app.hChkNoToolExif) &&
        (g_app.active_tab == 0);

    HKEY old_key = nullptr;
    if (RegCreateKeyExW(HKEY_CURRENT_USER, kRegSubKey, 0, nullptr, 0, KEY_WRITE, nullptr, &old_key, nullptr) != ERROR_SUCCESS)
        return 1;
    const wchar_t old_path[] = L"C:\\old\\engine";
    for (const wchar_t* name : {L"CliPath", L"LibRawPath"})
        RegSetValueExW(old_key, name, 0, REG_SZ, reinterpret_cast<const BYTE*>(old_path), sizeof(old_path));
    RegCloseKey(old_key);
    reg_write_str(L"HasSavedUiPrefs", L"1");
    reg_write_str(L"RawFmtCode", L"dng-fp16");
    load_ui_prefs();
    bool version_isolation_ok = SendMessageW(g_app.hCmbRawFmt, CB_GETCURSEL, 0, 0) == 2 &&
        reg_read_str(L"CliPath").empty() && reg_read_str(L"LibRawPath").empty();
    reg_write_str(L"CliPath", L"C:\\current\\nijinLBS.exe");
    version_isolation_ok &= reg_read_str(L"CliPath") == L"C:\\current\\nijinLBS.exe";

    // Mutate state across all 4 tabs and save
    SendMessageW(g_app.hCmbRawFmt,    CB_SETCURSEL, 1, 0); // dng-linear
    SendMessageW(g_app.hCmbRgbFmt,    CB_SETCURSEL, 3, 0); // jxs (index 3, since tiff-hq is index 1)
    seg_pill_set_sel(g_app.hSegRgbScale, 1);               // 1x
    SendMessageW(g_app.hCmbFieldMode, CB_SETCURSEL, 1, 0); // split
    SetWindowTextW(g_app.hEdtJxsBpp, L"12.5");
    set_chk_val(g_app.hRadCsSrgb,  false);
    set_chk_val(g_app.hRadCsAdobe, true);
    set_chk_val(g_app.hChkField,   true);
    set_chk_val(g_app.hChkRelayRaw, true);
    set_chk_val(g_app.hChkNoToolExif, true);
    apply_fields_csv(L"motion,phase,focus,depth,sigma");
    apply_preset_to_controls(1); // astro
    set_chk_val(g_app.hChkSiliconClean, false);
    set_chk_val(g_app.hChkLcpLca, false);
    set_chk_val(g_app.hChkLcpVig, true);
    set_chk_val(g_app.hChkLcpDist, true);
    g_app.motion_mode_idx = 3;   // clean-plate
    SendMessageW(g_app.hCmbGrouping, CB_SETCURSEL, 1, 0);
    SetWindowTextW(g_app.hEdtGrpSz, L"9");
    SendMessageW(g_app.hCmbBackend,  CB_SETCURSEL, 2, 0); // cpu
    SendMessageW(g_app.hCmbCpuCores, CB_SETCURSEL, 1, 0); // perf
    SendMessageW(g_app.hCmbPriority, CB_SETCURSEL, 3, 0); // idle
    SetWindowTextW(g_app.hEdtThreads, L"8");
    set_tuned_pair(g_app.hSldRefocusPos, g_app.hEdtRefocusPos, L"-0.25");
    set_tuned_pair(g_app.hSldBokehStr, g_app.hEdtBokehStr, L"3.00");
    SendMessageW(g_app.hCmbOlpf, CB_SETCURSEL, 2, 0); // weak4
    switch_tab(2);               // Tab 3
    save_ui_prefs();

    // Reset in-memory controls back to clean baseline
    g_app.loading_prefs = true;
    SendMessageW(g_app.hCmbGrouping, CB_SETCURSEL, 0, 0);
    SetWindowTextW(g_app.hEdtGrpSz, L"8");
    SendMessageW(g_app.hCmbBackend,  CB_SETCURSEL, 0, 0);
    SendMessageW(g_app.hCmbCpuCores, CB_SETCURSEL, 0, 0);
    SendMessageW(g_app.hCmbPriority, CB_SETCURSEL, 0, 0);
    SetWindowTextW(g_app.hEdtThreads, L"0");
    SendMessageW(g_app.hCmbRawFmt,    CB_SETCURSEL, 0, 0);
    SendMessageW(g_app.hCmbRgbFmt,    CB_SETCURSEL, 0, 0);
    seg_pill_set_sel(g_app.hSegRgbScale, 0);
    SendMessageW(g_app.hCmbFieldMode, CB_SETCURSEL, 0, 0);
    SetWindowTextW(g_app.hEdtJxsBpp, L"10.0");
    set_chk_val(g_app.hRadCsSrgb,  true);
    set_chk_val(g_app.hRadCsAdobe, false);
    set_chk_val(g_app.hChkField,   false);
    set_chk_val(g_app.hChkRelayRaw, false);
    set_chk_val(g_app.hChkNoToolExif, false);
    set_chk_val(g_app.hChkSiliconClean, true);
    apply_fields_csv(L"motion,phase,focus");
    apply_preset_to_controls(0);
    g_app.motion_mode_idx = 0;
    set_chk_val(g_app.hChkLcpLca, true);
    set_tuned_pair(g_app.hSldRefocusPos, g_app.hEdtRefocusPos, L"0.00");
    set_tuned_pair(g_app.hSldBokehStr, g_app.hEdtBokehStr, L"1.00");
    set_chk_val(g_app.hChkLcpVig, false);
    set_chk_val(g_app.hChkLcpDist, false);
    SendMessageW(g_app.hCmbOlpf, CB_SETCURSEL, 0, 0);
    switch_tab(0);
    g_app.loading_prefs = false;

    // Reload from registry and verify semantic round-trip
    load_ui_prefs();

    bool roundtrip_ok = SendMessageW(g_app.hCmbGrouping, CB_GETCURSEL, 0, 0) == 1 &&
        get_wnd_text(g_app.hEdtGrpSz) == L"9" &&
        defaults_ok &&
        (SendMessageW(g_app.hCmbRawFmt, CB_GETCURSEL, 0, 0) == 1) &&
        (SendMessageW(g_app.hCmbRgbFmt, CB_GETCURSEL, 0, 0) == 3) &&
        (seg_pill_get_sel(g_app.hSegRgbScale) == 1) &&
        (SendMessageW(g_app.hCmbFieldMode, CB_GETCURSEL, 0, 0) == 1) &&
        (get_wnd_text(g_app.hEdtJxsBpp) == L"12.5") &&
        get_chk(g_app.hRadCsAdobe) &&
        get_chk(g_app.hChkField) &&
        get_chk(g_app.hChkRelayRaw) &&
        get_chk(g_app.hChkNoToolExif) &&
        !get_chk(g_app.hChkSiliconClean) &&
        (build_fields_csv() == L"motion,phase,focus,depth,sigma") &&
        (g_app.preset_idx == 1) &&
        (g_app.motion_mode_idx == 3) &&
        (SendMessageW(g_app.hCmbOlpf, CB_GETCURSEL, 0, 0) == 2) &&
        (g_app.active_tab == 2);

    // Verify 1x-only RGB lowers minimum burst gate to 3, and 2x restores gate to 5
    set_chk_val(g_app.hChkRaw, false);
    set_chk_val(g_app.hChkRgb, true);
    seg_pill_set_sel(g_app.hSegRgbScale, 1);
    bool gate_1x_ok = (min_valid_frames_win() == 3);
    set_chk_val(g_app.hChkRaw, true);
    seg_pill_set_sel(g_app.hSegRgbScale, 0);
    gate_1x_ok = gate_1x_ok && (min_valid_frames_win() == 5);
    roundtrip_ok = roundtrip_ok && gate_1x_ok;

    // Verify v3 controls Registry round-trip & .dxo / dxo-superraw 4-frame gate
    g_app.loading_prefs = true;
    SendMessageW(g_app.hCmbFieldMode, CB_SETCURSEL, 1, 0);
    select_cmb_code(g_app.hCmbMotionProfile,  kMotionProfileCodes,  L"dxo-superraw");
    select_cmb_code(g_app.hCmbRgbFmt,         kRgbFmtCodes,         L"exr");
    select_cmb_code(g_app.hCmbExrPrec,        kExrPrecCodes,        L"float");
    select_cmb_code(g_app.hCmbExrParts,       kExrPartsCodes,       L"beauty");
    select_cmb_code(g_app.hCmbRgb1xTier,      kRgb1xTierCodes,      L"joint");
    select_cmb_code(g_app.hCmbHighlightMode,  kHighlightModeCodes,  L"oklab-stage");
    select_cmb_code(g_app.hCmbCodecConstraint,kCodecConstraintCodes,L"probit");
    select_cmb_code(g_app.hCmbSatModel,       kSatModelCodes,       L"probit");
    select_cmb_code(g_app.hCmbStreaming,      kStreamingCodes,      L"on");
    SetWindowTextW(g_app.hEdtMemBudget,       L"2");
    select_cmb_code(g_app.hCmbShutterMode,    kShutterModeCodes,    L"nnls-custom");
    SetWindowTextW(g_app.hEdtFlowKappa,       L"0.65");
    select_cmb_code(g_app.hCmbIntraDeblur,    kIntraDeblurCodes,    L"on");
    select_cmb_code(g_app.hCmbDiffusionMode,  kDiffusionCodes,      L"ultra-contrast");
    set_tuned_pair(g_app.hSldDiffStrength,    g_app.hEdtDiffStrength, L"1.40");
    select_cmb_code(g_app.hCmbUcGrade,        kUcGradeCodes,        L"2");
    select_cmb_code(g_app.hCmbUcBaffle,       kUcBaffleCodes,       L"hooded");
    select_cmb_code(g_app.hCmbUcChroma,       kUcChromaCodes,       L"warm-glass");
    select_cmb_code(g_app.hCmbApertureWeight, kApertureWeightCodes, L"dwell");
    select_cmb_code(g_app.hCmbMetricPolicy,   kMetricPolicyCodes,   L"allow-proxy");
    apply_observers_csv_win(L"o7,o8");
    set_chk_val(g_app.hChkWriteObsReport,     true);
    set_chk_val(g_app.hChkNextShotPlan,       true);
    select_cmb_code(g_app.hCmbSpectralCa,     kSpectralCaCodes,     L"lateral");
    set_tuned_pair(g_app.hSldSpectralPwr,     g_app.hEdtSpectralPwr, L"2.50");
    select_cmb_code(g_app.hCmbSpectralAng,    kSpectralAngleCodes,  L"45");
    set_tuned_pair(g_app.hSldBokehDeNisen,    g_app.hEdtBokehDeNisen, L"0.65");
    set_tuned_pair(g_app.hSldBokehSwirl,      g_app.hEdtBokehSwirl,   L"-0.40");
    set_tuned_pair(g_app.hSldBokehAspect,     g_app.hEdtBokehAspect,  L"1.50");
    set_tuned_pair(g_app.hSldBokehOnion,      g_app.hEdtBokehOnion,   L"0.80");
    set_tuned_pair(g_app.hSldBokehStf,        g_app.hEdtBokehStf,     L"0.55");
    commit_bus_in_edit_win(L"C:\\v3_in\\ext_policy.nib");
    set_chk_val(g_app.hChkBusExport,          true);
    g_app.loading_prefs = false;
    save_ui_prefs();
    g_app.loading_prefs = true;
    SendMessageW(g_app.hCmbMotionProfile, CB_SETCURSEL, 0, 0);
    SendMessageW(g_app.hCmbRgbFmt,        CB_SETCURSEL, 0, 0);
    SendMessageW(g_app.hCmbExrParts,      CB_SETCURSEL, 0, 0);
    SendMessageW(g_app.hCmbCodecConstraint, CB_SETCURSEL, 0, 0);
    SendMessageW(g_app.hCmbSatModel,      CB_SETCURSEL, 0, 0);
    SendMessageW(g_app.hCmbDiffusionMode, CB_SETCURSEL, 0, 0);
    apply_observers_csv_win(L"");
    set_chk_val(g_app.hChkWriteObsReport, false);
    set_chk_val(g_app.hChkNextShotPlan,   false);
    SendMessageW(g_app.hCmbSpectralCa,    CB_SETCURSEL, 0, 0);
    set_tuned_pair(g_app.hSldSpectralPwr, g_app.hEdtSpectralPwr, L"0.00");
    SendMessageW(g_app.hCmbSpectralAng,   CB_SETCURSEL, 0, 0);
    set_tuned_pair(g_app.hSldBokehDeNisen,g_app.hEdtBokehDeNisen, L"0.00");
    set_tuned_pair(g_app.hSldBokehSwirl,  g_app.hEdtBokehSwirl,   L"0.00");
    set_tuned_pair(g_app.hSldBokehAspect, g_app.hEdtBokehAspect,  L"1.00");
    set_tuned_pair(g_app.hSldBokehOnion,  g_app.hEdtBokehOnion,   L"0.00");
    set_tuned_pair(g_app.hSldBokehStf,    g_app.hEdtBokehStf,     L"0.00");
    commit_bus_in_edit_win(L"");
    set_chk_val(g_app.hChkBusExport,      false);
    g_app.loading_prefs = false;
    load_ui_prefs();
    bool v3_win_ok = (min_valid_frames_win() == 4) &&
        (selected_cmb_code(g_app.hCmbMotionProfile,  kMotionProfileCodes,  L"") == L"dxo-superraw") &&
        (selected_rgb_fmt_code() == L"exr") &&
        (selected_cmb_code(g_app.hCmbExrPrec,        kExrPrecCodes,        L"") == L"float") &&
        (selected_cmb_code(g_app.hCmbExrParts,       kExrPartsCodes,       L"") == L"beauty") &&
        (selected_cmb_code(g_app.hCmbRgb1xTier,      kRgb1xTierCodes,      L"") == L"joint") &&
        (selected_cmb_code(g_app.hCmbHighlightMode,  kHighlightModeCodes,  L"") == L"oklab-stage") &&
        (selected_cmb_code(g_app.hCmbCodecConstraint,kCodecConstraintCodes,L"") == L"probit") &&
        (selected_cmb_code(g_app.hCmbSatModel,       kSatModelCodes,       L"") == L"probit") &&
        (selected_cmb_code(g_app.hCmbStreaming,      kStreamingCodes,      L"") == L"on") &&
        (trim_ws(get_wnd_text(g_app.hEdtMemBudget)) == L"2") &&
        (selected_cmb_code(g_app.hCmbShutterMode,    kShutterModeCodes,    L"") == L"nnls-custom") &&
        (trim_ws(get_wnd_text(g_app.hEdtFlowKappa)) == L"0.65") &&
        (selected_cmb_code(g_app.hCmbIntraDeblur,    kIntraDeblurCodes,    L"") == L"on") &&
        (selected_cmb_code(g_app.hCmbDiffusionMode,  kDiffusionCodes,      L"") == L"ultra-contrast") &&
        (trim_ws(get_wnd_text(g_app.hEdtDiffStrength)) == L"1.40") &&
        (selected_cmb_code(g_app.hCmbUcGrade,        kUcGradeCodes,        L"") == L"2") &&
        (selected_cmb_code(g_app.hCmbUcBaffle,       kUcBaffleCodes,       L"") == L"hooded") &&
        (selected_cmb_code(g_app.hCmbUcChroma,       kUcChromaCodes,       L"") == L"warm-glass") &&
        (selected_cmb_code(g_app.hCmbApertureWeight, kApertureWeightCodes, L"") == L"dwell") &&
        (selected_cmb_code(g_app.hCmbMetricPolicy,   kMetricPolicyCodes,   L"") == L"allow-proxy") &&
        (build_observers_csv_win() == L"o7,o8") &&
        get_chk(g_app.hChkWriteObsReport) &&
        get_chk(g_app.hChkNextShotPlan) &&
        (selected_cmb_code(g_app.hCmbSpectralCa,     kSpectralCaCodes,     L"") == L"lateral") &&
        (trim_ws(get_wnd_text(g_app.hEdtSpectralPwr)) == L"2.50") &&
        (selected_cmb_code(g_app.hCmbSpectralAng,    kSpectralAngleCodes,  L"") == L"45") &&
        (trim_ws(get_wnd_text(g_app.hEdtBokehDeNisen)) == L"0.65") &&
        (trim_ws(get_wnd_text(g_app.hEdtBokehSwirl)) == L"-0.40") &&
        (trim_ws(get_wnd_text(g_app.hEdtBokehAspect)) == L"1.50") &&
        (trim_ws(get_wnd_text(g_app.hEdtBokehOnion)) == L"0.80") &&
        (trim_ws(get_wnd_text(g_app.hEdtBokehStf)) == L"0.55") &&
        (trim_ws(get_wnd_text(g_app.hEdtBusIn)) == L"C:\\v3_in\\ext_policy.nib") &&
        get_chk(g_app.hChkBusExport);
    // Verify tiff-alpha switches hCmbFieldMode to split instead of overwriting EXR
    g_app.loading_prefs = true;
    SendMessageW(g_app.hCmbFieldMode, CB_SETCURSEL, 0, 0);
    set_chk_val(g_app.hChkField, true);
    select_cmb_code(g_app.hCmbRgbFmt, kRgbFmtCodes, L"exr");
    set_chk_val(g_app.hChkRgb, true);
    sync_cross_tab_ui();
    v3_win_ok = v3_win_ok && (selected_rgb_fmt_code() == L"exr") &&
        ((int)SendMessageW(g_app.hCmbFieldMode, CB_GETCURSEL, 0, 0) == 1);
    set_chk_val(g_app.hChkField, false);
    g_app.loading_prefs = false;
    std::printf("V3_GUI_CONTROLS_TEST:%s\n", v3_win_ok ? "PASS" : "FAIL");
    std::vector<std::wstring> v3_cli;
    append_v3_win_args(v3_cli, L"C:\\v3_out", 1, false);
    auto v3_opt = [&](const std::wstring& k) -> std::wstring {
        for (size_t i = 0; i + 1 < v3_cli.size(); ++i) {
            if (v3_cli[i] == k) return v3_cli[i + 1];
        }
        return L"";
    };
    const bool v3_cli_ok = (v3_opt(L"--motion-profile") == L"dxo-superraw") &&
        (v3_opt(L"--exr-precision") == L"float") &&
        (v3_opt(L"--exr-parts") == L"beauty") &&
        (v3_opt(L"--rgb-1x-tier") == L"joint") &&
        (v3_opt(L"--highlight-mode") == L"oklab-stage") &&
        (v3_opt(L"--codec-constraint") == L"probit") &&
        (v3_opt(L"--sat-model") == L"probit") &&
        (v3_opt(L"--streaming") == L"on") &&
        (v3_opt(L"--mem-budget-gb") == L"2") &&
        (v3_opt(L"--shutter-mode") == L"nnls-custom") &&
        (v3_opt(L"--flow-kappa") == L"0.65") &&
        (v3_opt(L"--intra-deblur") == L"on") &&
        (v3_opt(L"--diffusion-mode") == L"ultra-contrast") &&
        (v3_opt(L"--diffusion-strength") == L"1.40") &&
        (v3_opt(L"--uc-grade") == L"2") &&
        (v3_opt(L"--uc-baffle") == L"hooded") &&
        (v3_opt(L"--uc-chroma") == L"warm-glass") &&
        (v3_opt(L"--aperture-weight") == L"dwell") &&
        (v3_opt(L"--metric-policy") == L"allow-proxy") &&
        (v3_opt(L"--observers") == L"o7,o8") &&
        (v3_opt(L"--observer-report") == L"C:\\v3_out\\observer_report.json") &&
        (v3_opt(L"--next-shot-plan") == L"C:\\v3_out\\next_shot_plan.json") &&
        (v3_opt(L"--spectral-ca") == L"lateral") &&
        (v3_opt(L"--spectral-ca-power") == L"2.50") &&
        (v3_opt(L"--spectral-ca-angle") == L"45") &&
        (v3_opt(L"--bokeh-de-nisen") == L"0.65") &&
        (v3_opt(L"--bokeh-swirl") == L"-0.40") &&
        (v3_opt(L"--bokeh-aspect") == L"1.50") &&
        (v3_opt(L"--bokeh-onion-clean") == L"0.80") &&
        (v3_opt(L"--bus-in") == L"C:\\v3_in\\ext_policy.nib") &&
        (v3_opt(L"--bus-export") == L"C:\\v3_out\\session_bus.nib");
    bool nib_iso_ok = false;
    {
        BurstGroupItem g1{}, g2{};
        g1.bus_in_path = L"C:\\v3_in\\grp1_only.nib";
        g2.bus_in_path = L"";
        g_app.groups = {g1, g2};
        std::vector<std::wstring> cli_g1, cli_g2;
        append_v3_win_args(cli_g1, L"C:\\v3_out", 1, true, &g_app.groups[0]);
        append_v3_win_args(cli_g2, L"C:\\v3_out", 2, true, &g_app.groups[1]);
        bool g1_has = false, g2_has = false;
        for (size_t i = 0; i + 1 < cli_g1.size(); ++i) {
            if (cli_g1[i] == L"--bus-in" && cli_g1[i + 1] == L"C:\\v3_in\\grp1_only.nib") g1_has = true;
        }
        for (const auto& tok : cli_g2) {
            if (tok == L"--bus-in") g2_has = true;
        }
        g_app.groups.clear();
        nib_iso_ok = g1_has && !g2_has;
    }
    SendMessageW(g_app.hCmbExrParts, CB_SETCURSEL, 0, 0);
    std::vector<std::wstring> v3_cli_def;
    append_v3_win_args(v3_cli_def, L"C:\\v3_out", 1, false);
    bool def_omits_parts = true;
    for (const auto& tok : v3_cli_def) {
        if (tok == L"--exr-parts") def_omits_parts = false;
    }
    SendMessageW(g_app.hCmbExrParts, CB_SETCURSEL, 1, 0);
    std::vector<std::wstring> v3_cli_bs;
    append_v3_win_args(v3_cli_bs, L"C:\\v3_out", 1, false);
    bool bs_emits_parts = false;
    for (size_t i = 0; i + 1 < v3_cli_bs.size(); ++i) {
        if (v3_cli_bs[i] == L"--exr-parts" && v3_cli_bs[i + 1] == L"beauty,scatter") bs_emits_parts = true;
    }
    const bool v3_cli_all_ok = v3_cli_ok && nib_iso_ok && def_omits_parts && bs_emits_parts;
    std::printf("V3_GUI_CLI_ARGS_TEST:%s\n", v3_cli_all_ok ? "PASS" : "FAIL");
    roundtrip_ok = roundtrip_ok && v3_win_ok && v3_cli_all_ok;
    bool stf_argv_ok = false;
    {
        auto stf_args = refocus_arguments();
        for (size_t i = 0; i + 1 < stf_args.size(); ++i) {
            if (stf_args[i] == L"--bokeh-stf" && std::fabs(std::wcstod(stf_args[i + 1].c_str(), nullptr) - 0.55) < 1e-4) {
                stf_argv_ok = true;
            }
        }
    }
    g_app.loading_prefs = true;
    SendMessageW(g_app.hCmbMotionProfile, CB_SETCURSEL, 0, 0);
    SendMessageW(g_app.hCmbRgbFmt,        CB_SETCURSEL, 0, 0);
    SendMessageW(g_app.hCmbExrParts,      CB_SETCURSEL, 0, 0);
    SendMessageW(g_app.hCmbCodecConstraint, CB_SETCURSEL, 0, 0);
    SendMessageW(g_app.hCmbSatModel,      CB_SETCURSEL, 0, 0);
    SendMessageW(g_app.hCmbDiffusionMode, CB_SETCURSEL, 0, 0);
    apply_observers_csv_win(L"");
    set_chk_val(g_app.hChkWriteObsReport, false);
    set_chk_val(g_app.hChkNextShotPlan,   false);
    SendMessageW(g_app.hCmbSpectralCa,    CB_SETCURSEL, 0, 0);
    set_tuned_pair(g_app.hSldSpectralPwr, g_app.hEdtSpectralPwr, L"0.00");
    SendMessageW(g_app.hCmbSpectralAng,   CB_SETCURSEL, 0, 0);
    set_tuned_pair(g_app.hSldBokehDeNisen,g_app.hEdtBokehDeNisen, L"0.00");
    set_tuned_pair(g_app.hSldBokehSwirl,  g_app.hEdtBokehSwirl,   L"0.00");
    set_tuned_pair(g_app.hSldBokehAspect, g_app.hEdtBokehAspect,  L"1.00");
    set_tuned_pair(g_app.hSldBokehOnion,  g_app.hEdtBokehOnion,   L"0.00");
    set_tuned_pair(g_app.hSldBokehStf,    g_app.hEdtBokehStf,     L"0.00");
    commit_bus_in_edit_win(L"");
    set_chk_val(g_app.hChkBusExport,      false);
    g_app.loading_prefs = false;
    bool lcp_ok = build_lcp_fields_csv() == L"vignette,distortion" &&
        lcp_arguments(L"C:/输出 镜头.lcp") == std::vector<std::wstring>{
            L"--optics-only", L"--export-lcp", L"C:/输出 镜头.lcp", L"--lcp-fields", L"vignette,distortion"};
    bool refocus_ok = stf_argv_ok &&
        std::wcstod(get_wnd_text(g_app.hEdtRefocusPos).c_str(), nullptr) == -0.25 &&
        std::wcstod(get_wnd_text(g_app.hEdtBokehStr).c_str(), nullptr) == 3.0;
    const wchar_t* offsets[] = {L"-1.00", L"0.00", L"1.00"};
    const wchar_t* strengths[] = {L"0.00", L"1.00", L"3.00"};
    const double positions[] = {0, 0.5, 1}, radii[] = {0, 12, 36};
    g_app.loading_prefs = true;
    for (int i = 0; i < 3; ++i) for (int j = 0; j < 3; ++j) {
        set_tuned_pair(g_app.hSldRefocusPos, g_app.hEdtRefocusPos, offsets[i]);
        set_tuned_pair(g_app.hSldBokehStr, g_app.hEdtBokehStr, strengths[j]);
        auto args = refocus_arguments();
        refocus_ok = refocus_ok && args[1] == L"--refocus-pos" && args[3] == L"--bokeh-strength" &&
            std::wcstod(args[2].c_str(), nullptr) == positions[i] && std::wcstod(args[4].c_str(), nullptr) == radii[j];
    }
    g_app.loading_prefs = false;
    std::printf("REFOCUS_ARGV_TEST:%s offsets=-1,0,1 positions=0,0.5,1 strengths=0,1,3 radii=0,12,36 prefs=UI\n",
        refocus_ok ? "PASS" : "FAIL");
    set_chk_val(g_app.hChkLcpLca, false);
    set_chk_val(g_app.hChkLcpVig, false);
    set_chk_val(g_app.hChkLcpDist, false);
    sync_cross_tab_ui();
    lcp_ok = lcp_ok && !IsWindowEnabled(g_app.hBtnExportLcp) && IsWindowEnabled(g_app.hChkExportParallax);
    set_chk_val(g_app.hChkLcpLca, true);
    sync_cross_tab_ui();
    lcp_ok = lcp_ok && IsWindowEnabled(g_app.hBtnExportLcp);
    std::printf("LCP_TEST:%s\n", lcp_ok ? "PASS" : "FAIL");
    std::fflush(stdout);

    auto parsed = parse_plan_json_groups(R"({"clusters" : [{"frames" : [
        {"idx":1,"path":"C:\\raw\\\u76f8\u673a\ud83d\ude00.NEF","file":"a.NEF","group":0,"dx_1x":1.5,"dy_1x":-2.0},
        {"idx":2,"path":"C:\\raw\\b.NEF","file":"b.NEF","group":1,"dx":0,"dy":0}
    ]}]})", {});
    bool parser_ok = parsed.size() == 2 && parsed[0].paths.size() == 1 &&
        parsed[0].paths[0] == L"C:\\raw\\相机😀.NEF" && parsed[0].max_parallax_2x_px == 0.0 &&
        parsed[0].frames[0].dx == 1.5 && parsed[1].frames[0].idx_1based == 1;
    bool telemetry_ok = !parse_optics_telemetry_json("{}", L"").measured &&
        !parse_optics_telemetry_json("{}", L"").has_silicon;
    g_app.grouping_dirty = true;
    sync_cross_tab_ui();
    bool grouping_ok = !IsWindowEnabled(g_app.hBtnRun) && !IsWindowEnabled(g_app.hEdtGrpSz);
    g_app.grouping_dirty = false;
    sync_cross_tab_ui();
    grouping_ok = grouping_ok && IsWindowEnabled(g_app.hBtnRun);
    auto lib_parsed = parse_plan_json_groups(R"({"clusters":[
        {"sr_score":92,"frames":[
            {"idx":1,"path":"C:\\lib\\b1.NEF","file":"b1.NEF","group":0,"af_score":90},
            {"idx":2,"path":"C:\\lib\\b2.NEF","file":"b2.NEF","group":0,"af_score":95},
            {"idx":3,"path":"C:\\lib\\b3.NEF","file":"b3.NEF","group":0,"af_score":88},
            {"idx":4,"path":"C:\\lib\\b4.NEF","file":"b4.NEF","group":0,"af_score":91},
            {"idx":5,"path":"C:\\lib\\b5.NEF","file":"b5.NEF","group":0,"af_score":93}
        ]},
        {"sr_score":86,"frames":[
            {"idx":1,"path":"C:\\lib\\c1.NEF","file":"c1.NEF","group":0,"af_score":80},
            {"idx":2,"path":"C:\\lib\\c2.NEF","file":"c2.NEF","group":0,"af_score":84}
        ]},
        {"frames":[{"idx":1,"path":"C:\\lib\\s1.NEF","file":"s1.NEF","group":-1,"af_score":50}]},
        {"frames":[{"idx":1,"path":"C:\\lib\\s2.NEF","file":"s2.NEF","group":-1,"af_score":50}]},
        {"frames":[{"idx":1,"path":"C:\\lib\\s3.NEF","file":"s3.NEF","group":-1,"af_score":50}]}
    ]})", {});
    bool multi_intake_ok = (lib_parsed.size() == 3) &&
        lib_parsed[0].checked && (lib_parsed[0].frames.size() == 5) &&
        !lib_parsed[1].checked && (lib_parsed[1].frames.size() == 2) &&
        lib_parsed[2].is_stray_archive && !lib_parsed[2].checked && (lib_parsed[2].frames.size() == 3);
    g_app.groups = lib_parsed;
    g_app.group_sel = {1, 1, 0};
    g_app.selected_group = 0;
    merge_selected_groups_win();
    multi_intake_ok = multi_intake_ok && (g_app.groups.size() == 2) &&
        (g_app.groups[0].frames.size() == 7) && g_app.groups[0].checked;
    select_single_group(0);
    g_app.frame_sel = {0, 0, 0, 0, 0, 1, 1};
    split_selected_frames_win();
    multi_intake_ok = multi_intake_ok && (g_app.groups.size() == 3) &&
        (g_app.groups[0].frames.size() == 5) && (g_app.groups[1].frames.size() == 2);
    g_app.group_sel = {1, 1, 0};
    batch_toggle_selected_groups_checked();
    multi_intake_ok = multi_intake_ok && g_app.groups[0].checked && g_app.groups[1].checked;
    clean_stray_groups_win();
    multi_intake_ok = multi_intake_ok && (g_app.groups.size() == 1) && (g_app.groups[0].frames.size() == 5);

    bool perf_menu_ok = (SendMessageW(g_app.hCmbBackend, CB_GETCURSEL, 0, 0) == 2) &&
        (SendMessageW(g_app.hCmbCpuCores, CB_GETCURSEL, 0, 0) == 1) &&
        (SendMessageW(g_app.hCmbPriority, CB_GETCURSEL, 0, 0) == 3) &&
        (get_wnd_text(g_app.hEdtThreads) == L"8") &&
        (get_wnd_text(g_app.hBtnPerfHeader).find(L"纯CPU") != std::wstring::npos) &&
        !format_win_cpu_topology_badge().empty();
    {
        std::vector<std::wstring> pargs;
        append_perf_cli_args(pargs);
        auto find_opt = [&](const std::wstring& k) -> std::wstring {
            for (size_t i = 0; i + 1 < pargs.size(); ++i) if (pargs[i] == k) return pargs[i + 1];
            return L"";
        };
        perf_menu_ok = perf_menu_ok &&
            find_opt(L"--backend") == L"cpu" &&
            find_opt(L"--cpu-cores") == L"perf" &&
            find_opt(L"--priority") == L"idle" &&
            find_opt(L"--threads") == L"8";
    }
    // Verify battery-device default no-SMT, APU / Intel <Xe2 default-off, Xe2+ / dGPU stay-on,
    // zero yellow pill breathing alert, and user choice memory.
    {
        // 1) First launch on AMD APU laptop (battery=1): GPU auto-off, no-SMT (physical), yellow lamp permanently OFF
        RegDeleteTreeW(HKEY_CURRENT_USER, kRegSubKey);
        _putenv_s("NIJIN_GPU_POLICY_TEST", "apu");
        _putenv_s("NIJIN_BATTERY_TEST", "1");
        init_perf_hardware_defaults(hwnd);
        load_ui_prefs();
        perf_menu_ok = perf_menu_ok &&
            g_app.is_battery_device &&
            (reg_read_str(L"BatteryDevice") == L"1") &&
            (SendMessageW(g_app.hCmbBackend, CB_GETCURSEL, 0, 0) == 2) &&
            (SendMessageW(g_app.hCmbCpuCores, CB_GETCURSEL, 0, 0) == 2) &&
            !g_app.perf_breath_active &&
            (reg_read_str(L"PerfGpuHintSeen") == L"1");

        // User explicitly selects auto cores (0) + forced GPU (1) on battery device -> remembered across restart
        SendMessageW(g_app.hCmbBackend,  CB_SETCURSEL, 1, 0);
        SendMessageW(g_app.hCmbCpuCores, CB_SETCURSEL, 0, 0);
        save_ui_prefs();
        init_perf_hardware_defaults(hwnd);
        load_ui_prefs();
        perf_menu_ok = perf_menu_ok &&
            g_app.is_battery_device &&
            (SendMessageW(g_app.hCmbBackend, CB_GETCURSEL, 0, 0) == 1) &&
            (SendMessageW(g_app.hCmbCpuCores, CB_GETCURSEL, 0, 0) == 0) &&
            !g_app.perf_breath_active;

        // 2) First launch on Intel <Xe2 desktop (battery=0): GPU auto-off, SMT stays auto (0), yellow lamp OFF
        RegDeleteTreeW(HKEY_CURRENT_USER, kRegSubKey);
        _putenv_s("NIJIN_GPU_POLICY_TEST", "intel_old");
        _putenv_s("NIJIN_BATTERY_TEST", "0");
        init_perf_hardware_defaults(hwnd);
        load_ui_prefs();
        perf_menu_ok = perf_menu_ok &&
            !g_app.is_battery_device &&
            (reg_read_str(L"BatteryDevice") == L"0") &&
            (SendMessageW(g_app.hCmbBackend, CB_GETCURSEL, 0, 0) == 2) &&
            (SendMessageW(g_app.hCmbCpuCores, CB_GETCURSEL, 0, 0) == 0) &&
            !g_app.perf_breath_active;

        // 3) First launch on Intel Xe2+ laptop (battery=1): GPU stays ON (0), no-SMT (2)
        RegDeleteTreeW(HKEY_CURRENT_USER, kRegSubKey);
        _putenv_s("NIJIN_GPU_POLICY_TEST", "intel_xe2");
        _putenv_s("NIJIN_BATTERY_TEST", "1");
        init_perf_hardware_defaults(hwnd);
        load_ui_prefs();
        perf_menu_ok = perf_menu_ok &&
            g_app.is_battery_device &&
            (SendMessageW(g_app.hCmbBackend, CB_GETCURSEL, 0, 0) == 0) &&
            (SendMessageW(g_app.hCmbCpuCores, CB_GETCURSEL, 0, 0) == 2) &&
            !g_app.perf_breath_active;

        // 4) First launch on desktop discrete GPU (battery=0): GPU stays ON (0), SMT stays ON (0)
        RegDeleteTreeW(HKEY_CURRENT_USER, kRegSubKey);
        _putenv_s("NIJIN_GPU_POLICY_TEST", "dgpu");
        _putenv_s("NIJIN_BATTERY_TEST", "0");
        init_perf_hardware_defaults(hwnd);
        load_ui_prefs();
        perf_menu_ok = perf_menu_ok &&
            !g_app.is_battery_device &&
            (SendMessageW(g_app.hCmbBackend, CB_GETCURSEL, 0, 0) == 0) &&
            (SendMessageW(g_app.hCmbCpuCores, CB_GETCURSEL, 0, 0) == 0) &&
            !g_app.perf_breath_active;

        _putenv_s("NIJIN_GPU_POLICY_TEST", "");
        _putenv_s("NIJIN_BATTERY_TEST", "");
        // 5) Real host probe sanity report
        RegDeleteTreeW(HKEY_CURRENT_USER, kRegSubKey);
        init_perf_hardware_defaults(hwnd);
        std::printf("HOST_POLICY_PROBE:gpu=\"%s\" discrete=%d default_off=%d battery=%d\n",
            g_app.gpu_policy.device_name,
            g_app.gpu_policy.is_discrete ? 1 : 0,
            g_app.gpu_policy.default_disable_gpu ? 1 : 0,
            g_app.is_battery_device ? 1 : 0);
    }
    // 6) Verify .lng multi-language live switching & custom external .lng loading
    bool i18n_ok = false;
    {
        heraw_i18n::I18nManager::instance().set_language("en-US");
        apply_ui_language_win();
        bool en_ok = heraw_i18n::I18nManager::instance().is_english() &&
            (get_wnd_text(g_app.hBtnExportLog) == L"Export Log") &&
            (get_wnd_text(g_app.hBtnOpenOut) == L"Open Folder") &&
            (get_wnd_text(g_app.hBtnRun).rfind(L"Start", 0) == 0);

        wchar_t tmpDir[MAX_PATH] = {0};
        GetTempPathW(MAX_PATH - 1, tmpDir);
        std::wstring custom_lng = std::wstring(tmpDir) + L"nijinLBS_custom_test.lng";
        write_utf8_file(custom_lng,
            "meta.code = en-TEST\nmeta.name = Custom Test (en-TEST)\nbot.export_log = CustomDiagLog\n");
        bool ext_ok = heraw_i18n::I18nManager::instance().load_lng_file(wide_to_utf8(custom_lng)) &&
            heraw_i18n::I18nManager::instance().set_language("en-TEST");
        apply_ui_language_win();
        ext_ok = ext_ok && (get_wnd_text(g_app.hBtnExportLog) == L"CustomDiagLog");
        DeleteFileW(custom_lng.c_str());

        heraw_i18n::I18nManager::instance().set_language("zh-CN");
        apply_ui_language_win();
        bool zh_ok = !heraw_i18n::I18nManager::instance().is_english() &&
            (get_wnd_text(g_app.hBtnExportLog) == L"导出日志");
        i18n_ok = en_ok && ext_ok && zh_ok;
    }

    // 7) Verify session diagnostic log capture, failure reason extraction, and file export
    bool log_export_ok = false;
    {
        int tid = heraw_i18n::diag_log().begin_task("SelfTestProbe", "C:\\nijinLBS.exe", {"--rgb-scale", "1x"});
        const char* mock_out = "[W8] Vignette skipped: burst translation insufficient (<50px)\n";
        heraw_i18n::diag_log().append_task_output(tid, mock_out, std::strlen(mock_out));
        heraw_i18n::diag_log().end_task(tid, 3, false);
        std::string extracted = heraw_i18n::SessionDiagLog::extract_failure_reason(mock_out);
        wchar_t tmpDir[MAX_PATH] = {0};
        GetTempPathW(MAX_PATH - 1, tmpDir);
        std::wstring log_dst = std::wstring(tmpDir) + L"nijinLBS_selftest_diag.log";
        bool saved = heraw_i18n::diag_log().export_to_file(wide_to_utf8(log_dst));
        std::string log_body = read_whole_file_utf8(log_dst);
        DeleteFileW(log_dst.c_str());
        log_export_ok = saved &&
            extracted.find("[W8]") != std::string::npos &&
            log_body.find("SelfTestProbe") != std::string::npos &&
            log_body.find("--rgb-scale 1x") != std::string::npos &&
            log_body.find("exit_code=3") != std::string::npos;
    }

    // 8) Verify locale auto-detection contract + Welcome/About modal & Credits link contract
    bool welcome_about_ok =
        (heraw_i18n::I18nManager::classify_locale_tag_to_lang_code("zh-CN") == "zh-CN") &&
        (heraw_i18n::I18nManager::classify_locale_tag_to_lang_code("zh-Hans-CN") == "zh-CN") &&
        (heraw_i18n::I18nManager::classify_locale_tag_to_lang_code("zh-Hans-US") == "zh-CN") &&
        (heraw_i18n::I18nManager::classify_locale_tag_to_lang_code("zh-SG") == "en-US") &&
        (heraw_i18n::I18nManager::classify_locale_tag_to_lang_code("zh-Hans-SG") == "en-US") &&
        (heraw_i18n::I18nManager::classify_locale_tag_to_lang_code("Chinese_Singapore") == "en-US") &&
        (heraw_i18n::I18nManager::classify_locale_tag_to_lang_code("zh-TW") == "en-US") &&
        (heraw_i18n::I18nManager::classify_locale_tag_to_lang_code("zh-HK") == "en-US") &&
        (heraw_i18n::I18nManager::classify_locale_tag_to_lang_code("ja-JP") == "en-US") &&
        (heraw_i18n::I18nManager::classify_locale_tag_to_lang_code("en-US") == "en-US");
    {
        heraw_i18n::I18nManager::instance().set_language("zh-CN");
        ModalDlgData mdZh{};
        HWND hWelcome = build_welcome_or_about_win(hwnd, true, &mdZh);
        auto* cdZh = mdZh.hCredits ? reinterpret_cast<CreditsViewData*>(GetWindowLongPtrW(mdZh.hCredits, GWLP_USERDATA)) : nullptr;
        bool has_xhs_link = false;
        if (cdZh) {
            for (const auto& l : cdZh->hit_links) {
                if (l.text.find(L"小红书") != std::wstring::npos) has_xhs_link = true;
            }
        }
        welcome_about_ok = welcome_about_ok && (hWelcome != nullptr) && (cdZh != nullptr) &&
            !has_xhs_link &&
            (cdZh->plain_dump.find(L"Apache License 2.0") != std::wstring::npos) &&
            (cdZh->plain_dump.find(L"NOTICE") == std::wstring::npos) &&
            (cdZh->plain_dump.find(L"@共星") != std::wstring::npos) &&
            (cdZh->plain_dump.find(L"@格子A2Si_苏和郡") != std::wstring::npos) &&
            (cdZh->plain_dump.find(L"小红书 kA2Sumie") != std::wstring::npos) &&
            (cdZh->plain_dump.find(L"@227_Official") != std::wstring::npos) &&
            (cdZh->plain_dump.find(L"您的一点心意就是最好的支持") != std::wstring::npos);
        if (hWelcome) DestroyWindow(hWelcome);

        heraw_i18n::I18nManager::instance().set_language("en-US");
        ModalDlgData mdEn{};
        HWND hAbout = build_welcome_or_about_win(hwnd, false, &mdEn);
        auto* cdEn = mdEn.hCredits ? reinterpret_cast<CreditsViewData*>(GetWindowLongPtrW(mdEn.hCredits, GWLP_USERDATA)) : nullptr;
        welcome_about_ok = welcome_about_ok && (hAbout != nullptr) && (cdEn != nullptr) && (mdEn.hCmbLang != nullptr) &&
            (cdEn->plain_dump.find(L"NOTICE") == std::wstring::npos) &&
            (cdEn->plain_dump.find(L"@kA2Sumie") != std::wstring::npos) &&
            (cdEn->plain_dump.find(L"@sally_amaki") != std::wstring::npos) &&
            (cdEn->plain_dump.find(L"@sallyamakiofficial") != std::wstring::npos) &&
            (cdEn->plain_dump.find(L"@227SMEJ") != std::wstring::npos) &&
            (cdEn->plain_dump.find(L"小红书") == std::wstring::npos) &&
            (cdEn->plain_dump.find(L"@格子A2Si") == std::wstring::npos);
        if (hAbout) DestroyWindow(hAbout);
        heraw_i18n::I18nManager::instance().set_language("zh-CN");
    }

    // 9) Verify 100% full engine parameter coverage: Intake/Decoder 7 knobs, W1 (FocusStack, RGB RL, IBP 0/1/2, EdgePolish),
    // W2 (Custom° SCA, synced DepthGate, RefocusMode, AutoPos, F/, Blades, Angle, SA, TiltX/Y, SeeThrough),
    // W3 (27 Fields Core11+Ext16, DepthGate+Disp/Coh/Sup, ObsGate+ReadoutMs+Pitch+PlateScale+Diffuse+LightSplit+EvalSelf)
    bool full_param_ok = false;
    {
        g_app.loading_prefs = true;
        g_app.decoder_mode       = L"wvdc";
        g_app.nev_frames_spec    = L"2-9";
        g_app.burst_gap_s        = L"1.75";
        g_app.min_rel_sharpness  = L"0.64";
        g_app.max_shift_px       = L"240";
        g_app.lucky_mode         = L"on";
        g_app.max_cluster_frames = L"48";
        g_app.motion_mode_idx    = 6; // focus-stack
        set_tuned_pair(g_app.hSldRgbPsfIters, g_app.hEdtRgbPsfIters, L"7");
        set_ibp_iters_win(2);
        set_chk_val(g_app.hChkEdgePolish, false);
        select_cmb_code(g_app.hCmbSpectralCa,  kSpectralCaCodes,    L"lateral");
        set_tuned_pair(g_app.hSldSpectralPwr,  g_app.hEdtSpectralPwr, L"1.80");
        select_cmb_code(g_app.hCmbSpectralAng, kSpectralAngleCodes, L"custom");
        SetWindowTextW(g_app.hEdtSpectralDeg, L"33.5");
        select_cmb_code(g_app.hCmbDepthGate,   kDepthGateCodes,     L"force");
        select_cmb_code(g_app.hCmbW2DepthGate, kDepthGateCodes,     L"force");
        SetWindowTextW(g_app.hEdtDepthDisp, L"0.35");
        SetWindowTextW(g_app.hEdtDepthCoh,  L"0.42");
        SetWindowTextW(g_app.hEdtDepthSup,  L"0.02");
        set_chk_val(g_app.hChkField, true);
        SendMessageW(g_app.hCmbFieldMode, CB_SETCURSEL, 1, 0);
        apply_fields_csv(L"all");
        bool all27_checked = (count_checked_fields() == 27);
        set_chk_val(g_app.hChkExportRefocus, true);
        select_cmb_code(g_app.hCmbRefocusMode, kRefocusModeCodes, L"synthetic-aperture");
        set_chk_val(g_app.hChkRefocusAuto, true);
        set_tuned_pair(g_app.hSldBokehSa, g_app.hEdtBokehSa, L"-0.45");
        SetWindowTextW(g_app.hEdtVirtualFstop, L"2.0");
        select_cmb_code(g_app.hCmbBokehBlades, kBokehBladeCodes, L"9");
        SetWindowTextW(g_app.hEdtBokehAngle, L"18");
        SetWindowTextW(g_app.hEdtTiltX, L"0.25");
        SetWindowTextW(g_app.hEdtTiltY, L"-0.15");
        select_cmb_code(g_app.hCmbSeeThrough, kSeeThroughCodes, L"on");
        select_cmb_code(g_app.hCmbObsGate, kObsGateCodes, L"relaxed");
        SetWindowTextW(g_app.hEdtReadoutMs,  L"14.2");
        SetWindowTextW(g_app.hEdtObsPitch,   L"-11.5");
        SetWindowTextW(g_app.hEdtPlateScale, L"1.85");
        set_chk_val(g_app.hChkEmitDiffuse,    true);
        set_chk_val(g_app.hChkEmitLightSplit, true);
        set_chk_val(g_app.hChkEvalSelf,       true);
        g_app.loading_prefs = false;
        save_ui_prefs();

        // Reset in-memory controls to baseline and reload from Registry
        g_app.loading_prefs = true;
        g_app.decoder_mode       = L"auto";
        g_app.nev_frames_spec    = L"all";
        g_app.burst_gap_s        = L"3.5";
        g_app.min_rel_sharpness  = L"0.72";
        g_app.max_shift_px       = L"120";
        g_app.lucky_mode         = L"auto";
        g_app.max_cluster_frames = L"128";
        g_app.motion_mode_idx    = 0;
        apply_preset_to_controls(0);
        reset_optics_controls_win();
        SendMessageW(g_app.hCmbDepthGate,   CB_SETCURSEL, 0, 0);
        SendMessageW(g_app.hCmbW2DepthGate, CB_SETCURSEL, 0, 0);
        SetWindowTextW(g_app.hEdtDepthDisp, L"");
        SetWindowTextW(g_app.hEdtDepthCoh,  L"");
        SetWindowTextW(g_app.hEdtDepthSup,  L"");
        apply_fields_csv(L"motion,phase,focus");
        set_chk_val(g_app.hChkExportRefocus, false);
        SendMessageW(g_app.hCmbRefocusMode, CB_SETCURSEL, 0, 0);
        set_chk_val(g_app.hChkRefocusAuto, false);
        set_tuned_pair(g_app.hSldBokehSa, g_app.hEdtBokehSa, L"0.00");
        SetWindowTextW(g_app.hEdtVirtualFstop, L"0.0");
        SendMessageW(g_app.hCmbBokehBlades, CB_SETCURSEL, 0, 0);
        SetWindowTextW(g_app.hEdtBokehAngle, L"0");
        SetWindowTextW(g_app.hEdtTiltX, L"0.00");
        SetWindowTextW(g_app.hEdtTiltY, L"0.00");
        SendMessageW(g_app.hCmbSeeThrough, CB_SETCURSEL, 0, 0);
        SendMessageW(g_app.hCmbObsGate, CB_SETCURSEL, 0, 0);
        SetWindowTextW(g_app.hEdtReadoutMs,  L"");
        SetWindowTextW(g_app.hEdtObsPitch,   L"0.0");
        SetWindowTextW(g_app.hEdtPlateScale, L"0.00");
        set_chk_val(g_app.hChkEmitDiffuse,    false);
        set_chk_val(g_app.hChkEmitLightSplit, false);
        set_chk_val(g_app.hChkEvalSelf,       false);
        g_app.loading_prefs = false;
        load_ui_prefs();

        BurstGroupItem sample_grp{};
        sample_grp.checked = true;
        sample_grp.paths = {L"C:\\raw\\f1.NEF", L"C:\\raw\\f2.NEF", L"C:\\raw\\f3.NEF", L"C:\\raw\\f4.NEF", L"C:\\raw\\f5.NEF"};
        sample_grp.depth_known = true;
        sample_grp.depth_observable = false;
        g_app.groups = {sample_grp};
        g_app.selected_group = 0;
        sync_cross_tab_ui();

        std::vector<std::wstring> fp_argv = build_group_job_args_win(L"C:\\nijinLBS.exe", g_app.groups[0], L"C:\\fp_out", 1, false);
        auto fp_opt = [&](const std::wstring& k) -> std::wstring {
            for (size_t i = 0; i + 1 < fp_argv.size(); ++i) {
                if (fp_argv[i] == k) return fp_argv[i + 1];
            }
            return L"";
        };
        auto fp_has = [&](const std::wstring& k) -> bool {
            return std::find(fp_argv.begin(), fp_argv.end(), k) != fp_argv.end();
        };
        int comma_cnt = 0;
        std::wstring flds_arg = fp_opt(L"--fields");
        for (wchar_t ch : flds_arg) if (ch == L',') ++comma_cnt;
        int fld_tok_cnt = flds_arg.empty() ? 0 : (comma_cnt + 1);

        full_param_ok = all27_checked && (count_checked_fields() == 27) &&
            (SendMessageW(g_app.hCmbW2DepthGate, CB_GETCURSEL, 0, 0) == 2) &&
            (IsWindowEnabled(g_app.hChkExportRefocus) != FALSE) &&
            (fp_opt(L"--decoder") == L"wvdc") &&
            (fp_opt(L"--nev-frames") == L"2-9") &&
            (fp_opt(L"--burst-gap-s") == L"1.75") &&
            (fp_opt(L"--min-rel-sharpness") == L"0.64") &&
            (fp_opt(L"--max-shift-px") == L"240") &&
            (fp_opt(L"--lucky-imaging") == L"on") &&
            (fp_opt(L"--max-cluster-frames") == L"48") &&
            (fp_opt(L"--motion-mode") == L"focus-stack") &&
            (fp_opt(L"--rgb-psf-iters") == L"7") &&
            (fp_opt(L"--ibp-iters") == L"2") &&
            (fp_opt(L"--edge-polish") == L"off") &&
            (fp_opt(L"--spectral-ca-angle") == L"33.5") &&
            (fp_opt(L"--depth-gate") == L"force") &&
            (fp_opt(L"--depth-min-disp") == L"0.35") &&
            (fp_opt(L"--depth-min-coh") == L"0.42") &&
            (fp_opt(L"--depth-min-support") == L"0.02") &&
            (fld_tok_cnt == 27) &&
            fp_has(L"--export-refocus") &&
            (fp_opt(L"--refocus-mode") == L"synthetic-aperture") &&
            (fp_opt(L"--refocus-pos") == L"auto") &&
            (fp_opt(L"--bokeh-sa") == L"-0.45") &&
            (fp_opt(L"--virtual-fstop") == L"2.00") &&
            (fp_opt(L"--bokeh-blades") == L"9") &&
            (fp_opt(L"--bokeh-angle") == L"18.0") &&
            (fp_opt(L"--tilt-plane") == L"0.25,-0.15") &&
            (fp_opt(L"--see-through") == L"on") &&
            (fp_opt(L"--observer-gate") == L"relaxed") &&
            (fp_opt(L"--readout-ms") == L"14.2") &&
            (fp_opt(L"--obs-pitch-deg") == L"-11.5") &&
            (fp_opt(L"--plate-scale") == L"1.85") &&
            fp_has(L"--emit-diffuse") &&
            fp_has(L"--emit-light-split") &&
            (fp_opt(L"--eval-self") == L"C:\\fp_out\\eval_self.json");
        g_app.groups.clear();
    }

    std::printf("FULL_PARAM_COVERAGE_TEST:%s\n", full_param_ok ? "PASS" : "FAIL");
    std::printf("WELCOME_ABOUT_TEST:%s\n", welcome_about_ok ? "PASS" : "FAIL");
    std::printf("PERF_MENU_TEST:%s\n", perf_menu_ok ? "PASS" : "FAIL");
    std::printf("GROUPING_CONTROLS_TEST:%s\n", grouping_ok ? "PASS" : "FAIL");
    std::printf("MULTI_SELECT_INTAKE_TEST:%s\n", multi_intake_ok ? "PASS" : "FAIL");
    std::printf("I18N_LNG_TEST:%s\n", i18n_ok ? "PASS" : "FAIL");
    std::printf("LOG_EXPORT_TEST:%s\n", log_export_ok ? "PASS" : "FAIL");
    roundtrip_ok = roundtrip_ok && full_param_ok && welcome_about_ok && perf_menu_ok && parser_ok && telemetry_ok && kNumFields == 11 && lcp_ok && refocus_ok &&
        grouping_ok && multi_intake_ok && i18n_ok && log_export_ok &&
        version_isolation_ok;
    std::printf("UI_MEMORY_ENGINE_ISOLATION_TEST:%s\n", version_isolation_ok ? "PASS" : "FAIL");
    std::printf("GUI_CONTRACT:defaults=%d ui_memory_engine_isolation=%d roundtrip=%d fields=%d\n",
                defaults_ok ? 1 : 0, version_isolation_ok ? 1 : 0, roundtrip_ok ? 1 : 0, kNumFields);
    std::printf("PREFS_TEST:%s\n", roundtrip_ok ? "PASS" : "FAIL");
    std::fflush(stdout);

    RegDeleteTreeW(HKEY_CURRENT_USER, kRegSubKey);
    DestroyWindow(hwnd);
    Gdiplus::GdiplusShutdown(gdip_tok);
    CoUninitialize();
    return roundtrip_ok ? 0 : 1;
}

static int run_optics_selftest(const std::wstring& path) {
    auto report = parse_optics_telemetry_json(read_whole_file_utf8(path), path);
    BurstGroupItem group;
    group.frames.resize(report.drift.size());
    g_app.groups = {group};
    SendMessageW(g_app.hwnd, WM_APP_OPTICS_READY, 0, reinterpret_cast<LPARAM>(new OpticsTelemetry(report)));
    std::printf("OPTICS_JSON:{\"loaded\":%s,\"mtf50\":[", report.measured ? "true" : "false");
    for (int i = 0; i < 5; ++i) {
        if (i) std::printf(",");
        if (report.zone_rejected[i] || !report.measured) std::printf("null");
        else std::printf("%.10g", report.zone_mtf50[i]);
    }
    std::printf("],\"phase_quadrants\":[");
    for (size_t i = 0; i < report.phase_quadrants.size(); ++i)
        std::printf("%s%d", i ? "," : "", report.phase_quadrants[i]);
    std::printf("],\"drift\":[");
    for (size_t i = 0; i < report.drift.size(); ++i) {
        const auto& frame = report.drift[i];
        if (i) std::printf(",");
        std::printf("{\"dx\":");
        if (frame.included) std::printf("%.10g", frame.dx); else std::printf("null");
        std::printf(",\"dy\":");
        if (frame.included) std::printf("%.10g", frame.dy); else std::printf("null");
        std::printf(",\"time\":");
        if (frame.has_time) std::printf("%.10g", frame.time_seconds); else std::printf("null");
        std::printf("}");
    }
    std::printf("],\"lca_valid\":[%s,%s],\"passport\":%s,",
        report.lca_valid[0] ? "true" : "false", report.lca_valid[1] ? "true" : "false",
        file_exists_regular(report.passport_path) ? "true" : "false");
    std::printf("\"parallax_measured\":%s,\"max_parallax_2x_px\":", report.parallax_measured ? "true" : "false");
    if (report.parallax_measured) std::printf("%.10g", report.max_parallax_2x_px); else std::printf("null");
    std::printf(",\"depth_observable\":%s,\"depth_enabled\":%s,\"field_observability\":{",
        report.depth_known ? (report.depth_observable ? "true" : "false") : "null",
        IsWindowEnabled(g_app.hChkExportParallax) ? "true" : "false");
    bool first = true;
    for (const auto& item : report.field_observability) {
        std::printf("%s\"%s\":%s", first ? "" : ",", item.first.c_str(), item.second ? "true" : "false");
        first = false;
    }
    std::printf("},\"field_enabled\":{");
    for (int i = 0; i < kNumFields; ++i)
        std::printf("%s\"%s\":%s", i ? "," : "", wide_to_utf8(kFieldSpecs[i].code).c_str(),
            IsWindowEnabled(g_app.hChkFields[i]) ? "true" : "false");
    std::printf("},\"silicon\":{\"sigma0\":");
    if (report.sigma0 >= 0) std::printf("%.10g", report.sigma0); else std::printf("null");
    std::printf(",\"gain_g\":");
    if (report.gain_g >= 0) std::printf("%.10g", report.gain_g); else std::printf("null");
    std::printf(",\"sensor_temp_c\":");
    if (std::isfinite(report.temp_c)) std::printf("%.10g", report.temp_c); else std::printf("null");
    std::printf(",\"warm_pixels\":");
    if (report.warm_pixels >= 0) std::printf("%d", report.warm_pixels); else std::printf("null");
    std::printf("},\"silicon_text\":\"%s\"}\n", wide_to_utf8(silicon_readout(report)).c_str());
    std::fflush(stdout);
    return report.measured ? 0 : 1;
}

} // namespace

int WINAPI wWinMain(HINSTANCE hInst, HINSTANCE, PWSTR, int nCmdShow) {
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    Gdiplus::GdiplusStartupInput gdip_in;
    ULONG_PTR gdip_tok = 0;
    Gdiplus::GdiplusStartup(&gdip_tok, &gdip_in, nullptr);

    INITCOMMONCONTROLSEX icc{};
    icc.dwSize = sizeof(icc);
    icc.dwICC = ICC_WIN95_CLASSES | ICC_STANDARD_CLASSES | ICC_PROGRESS_CLASS;
    InitCommonControlsEx(&icc);

    register_custom_classes(hInst);

    HICON hIconBig   = reinterpret_cast<HICON>(LoadImageW(hInst, MAKEINTRESOURCEW(IDI_APPICON), IMAGE_ICON, 32, 32, LR_DEFAULTCOLOR));
    HICON hIconSmall = reinterpret_cast<HICON>(LoadImageW(hInst, MAKEINTRESOURCEW(IDI_APPICON), IMAGE_ICON, 16, 16, LR_DEFAULTCOLOR));
    if (!hIconBig)   hIconBig   = LoadIconW(nullptr, IDI_APPLICATION);
    if (!hIconSmall) hIconSmall = hIconBig;

    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = MainWndProc;
    wc.hInstance = hInst;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hIcon = hIconBig;
    wc.hIconSm = hIconSmall;
    wc.hbrBackground = nullptr;
    wc.lpszClassName = L"NijiNLiteBurstSuiteGui";
    RegisterClassExW(&wc);

    int wargc = 0;
    std::wstring shot_path;
    std::wstring shot_welcome_path;
    std::wstring shot_about_path;
    std::wstring cli_lang;
    int shot_tab = -1;
    bool ext_fields = false;
    bool test_prefs = false;
    bool test_close = false;
    std::wstring test_optics, optics_json;
    std::vector<std::wstring> init_inputs;
    LPWSTR* wargv = CommandLineToArgvW(GetCommandLineW(), &wargc);
    if (wargv) {
        for (int i = 1; i < wargc; ++i) {
            std::wstring a = wargv[i];
            if (a == L"--screenshot" && i + 1 < wargc) {
                shot_path = wargv[++i];
            } else if (a == L"--screenshot-welcome" && i + 1 < wargc) {
                shot_welcome_path = wargv[++i];
            } else if (a == L"--screenshot-about" && i + 1 < wargc) {
                shot_about_path = wargv[++i];
            } else if (a == L"--lang" && i + 1 < wargc) {
                cli_lang = wargv[++i];
            } else if (a.rfind(L"--lang=", 0) == 0) {
                cli_lang = a.substr(7);
            } else if (a == L"--tab" && i + 1 < wargc) {
                shot_tab = _wtoi(wargv[++i]);
            } else if (a.rfind(L"--tab=", 0) == 0) {
                shot_tab = _wtoi(a.c_str() + 6);
            } else if (a == L"--ext-fields") {
                ext_fields = true;
            } else if (a == L"--test-prefs") {
                test_prefs = true;
            } else if (a == L"--test-cmd-q") {
                test_close = true;
            } else if (a == L"--test-optics" && i + 1 < wargc) {
                test_optics = wargv[++i];
            } else if (a == L"--optics-json" && i + 1 < wargc) {
                optics_json = wargv[++i];
            } else if (a.size() == 6 && a.rfind(L"--tab", 0) == 0) {
                shot_tab = _wtoi(a.c_str() + 5);
            } else if (!a.empty() && a[0] != L'-') {
                init_inputs.push_back(a);
            }
        }
        LocalFree(wargv);
    }
    g_app.is_screenshot_mode = !shot_path.empty() || !shot_welcome_path.empty() || !shot_about_path.empty() || test_close;
    g_app.allow_unconstrained_size = g_app.is_screenshot_mode || test_prefs || !test_optics.empty();
    if (test_prefs) kRegSubKey = L"Software\\NijiN\\nijinLBS-GuiTest";

    {
        wchar_t selfMod[MAX_PATH * 4] = {0};
        GetModuleFileNameW(nullptr, selfMod, MAX_PATH * 4 - 1);
        std::string pref_lang = wide_to_utf8(cli_lang);
        if (pref_lang.empty()) {
            if (g_app.is_screenshot_mode || test_prefs) {
                pref_lang = "zh-CN";
            } else {
                pref_lang = wide_to_utf8(reg_read_str(L"LangCode"));
            }
        }
        heraw_i18n::I18nManager::instance().init(wide_to_utf8(selfMod), pref_lang);
        heraw_i18n::diag_log().init_session("Windows Win32 GUI (" + wide_to_utf8(format_win_cpu_topology_badge()) + ")");
    }

    HDC hdc = GetDC(nullptr);
    int dpi = GetDeviceCaps(hdc, LOGPIXELSY);
    ReleaseDC(nullptr, hdc);
    DWORD style = WS_OVERLAPPEDWINDOW;
    int client_w_px = MulDiv(1540, dpi, 96);
    int client_h_px = MulDiv(920, dpi, 96);
    if (!g_app.allow_unconstrained_size) {
        RECT wa{};
        if (SystemParametersInfoW(SPI_GETWORKAREA, 0, &wa, 0)) {
            RECT frame_pad{0, 0, 0, 0};
            AdjustWindowRect(&frame_pad, style, FALSE);
            int pad_w = frame_pad.right - frame_pad.left;
            int pad_h = frame_pad.bottom - frame_pad.top;
            int max_cw = std::max<int>(MulDiv(1360, dpi, 96), (wa.right - wa.left) - pad_w - MulDiv(16, dpi, 96));
            int max_ch = std::max<int>(MulDiv(800, dpi, 96), (wa.bottom - wa.top) - pad_h - MulDiv(16, dpi, 96));
            client_w_px = std::min(client_w_px, max_cw);
            client_h_px = std::min(client_h_px, max_ch);
        }
    }
    RECT rc{0, 0, client_w_px, client_h_px};
    AdjustWindowRect(&rc, style, FALSE);

    HWND hwnd = CreateWindowExW(
        0, wc.lpszClassName, heraw_i18n::tr_w("app.window_title").c_str(),
        style, CW_USEDEFAULT, CW_USEDEFAULT, rc.right - rc.left, rc.bottom - rc.top,
        nullptr, nullptr, hInst, nullptr
    );
    g_app.loading_prefs = false;

    if (test_prefs) {
        return run_prefs_selftest(hwnd, gdip_tok);
    }
    if (!test_optics.empty()) {
        int result = run_optics_selftest(test_optics);
        DestroyWindow(hwnd);
        Gdiplus::GdiplusShutdown(gdip_tok);
        CoUninitialize();
        return result;
    }

    init_perf_hardware_defaults(hwnd);
    if (!g_app.is_screenshot_mode) {
        load_ui_prefs();
        if (!cli_lang.empty()) {
            heraw_i18n::I18nManager::instance().set_language(wide_to_utf8(cli_lang));
        }
        apply_ui_language_win();
    }
    if (ext_fields && g_app.hSegFieldBank) {
        seg_pill_set_sel(g_app.hSegFieldBank, 1);
    }
    if (shot_tab >= 0) {
        switch_tab(std::min(shot_tab, 2));
    } else if (ext_fields) {
        update_field_bank_visibility_win();
    }

    g_app.cli_path    = auto_detect_cli_exe(true);
    g_app.libraw_path = g_app.is_screenshot_mode ? L"" : reg_read_str(L"LibRawPath");
    refresh_engine_status_label();

    for (const auto& inp : init_inputs) add_unique_input(inp);
    if (!g_app.inputs.empty()) {
        if (g_app.is_screenshot_mode && !g_app.cli_path.empty()) {
            // Synchronous plan-json populate in screenshot mode when inputs are provided
            bool single = SendMessageW(g_app.hCmbGrouping, CB_GETCURSEL, 0, 0) == 1;
            std::vector<std::wstring> pargs = {g_app.cli_path, L"--sort-time", L"--policy",
                single ? L"force-all" : L"multi-group", L"--group-size", get_wnd_text(g_app.hEdtGrpSz)};
            if (single) pargs.push_back(L"--single-group");
            append_intake_cli_args_win(pargs, true);
            pargs.push_back(L"--plan-json");
            for (const auto& p : g_app.inputs) pargs.push_back(p);
            std::string pout = run_cli_capture_stdout(pargs, nullptr, nullptr, 0, "Screenshot Plan Scan");
            auto pgrps = parse_plan_json_groups(pout, g_app.inputs);
            if (!pgrps.empty()) g_app.groups = std::move(pgrps);
            else g_app.groups = {make_provisional_group(g_app.inputs)};
            sync_cross_tab_ui();
        } else {
            trigger_async_plan_scan();
        }
    }

    ShowWindow(hwnd, nCmdShow);
    UpdateWindow(hwnd);
    if (!optics_json.empty()) {
        auto* report = new OpticsTelemetry(parse_optics_telemetry_json(read_whole_file_utf8(optics_json), optics_json));
        SendMessageW(hwnd, WM_APP_OPTICS_READY, (WPARAM)g_app.selected_group, reinterpret_cast<LPARAM>(report));
    }
    if (test_close) PostMessageW(hwnd, WM_CLOSE, 0, 0);

    if (!shot_welcome_path.empty() || !shot_about_path.empty()) {
        init_perf_hardware_defaults(hwnd);
        ModalDlgData mdShot{};
        bool is_wel = !shot_welcome_path.empty();
        HWND hModal = build_welcome_or_about_win(hwnd, is_wel, &mdShot);
        bool ok = false;
        if (hModal) {
            ShowWindow(hModal, SW_SHOW);
            UpdateWindow(hModal);
            SetFocus(nullptr);
            MSG pm{};
            while (PeekMessageW(&pm, nullptr, 0, 0, PM_REMOVE)) {
                TranslateMessage(&pm);
                DispatchMessageW(&pm);
            }
            ok = save_window_screenshot_png(hModal, is_wel ? shot_welcome_path : shot_about_path);
            DestroyWindow(hModal);
        }
        DestroyWindow(hwnd);
        Gdiplus::GdiplusShutdown(gdip_tok);
        CoUninitialize();
        return ok ? 0 : 1;
    }

    if (!shot_path.empty()) {
        SetFocus(nullptr);
        MSG pm{};
        while (PeekMessageW(&pm, nullptr, 0, 0, PM_REMOVE)) {
            TranslateMessage(&pm);
            DispatchMessageW(&pm);
        }
        bool ok = save_window_screenshot_png(hwnd, shot_path);
        DestroyWindow(hwnd);
        Gdiplus::GdiplusShutdown(gdip_tok);
        CoUninitialize();
        return ok ? 0 : 1;
    }

    if (!g_app.is_screenshot_mode && !test_close && reg_read_str(L"FirstLaunchSetupDone") != L"1") {
        show_welcome_or_about_win(hwnd, true);
    }

    MSG m{};
    while (GetMessageW(&m, nullptr, 0, 0) > 0) {
        if (m.message == WM_KEYDOWN) {
            bool ctrl = (GetKeyState(VK_CONTROL) < 0);
            bool shift = (GetKeyState(VK_SHIFT) < 0);
            HWND hf = GetFocus();
            wchar_t cls[32] = {0};
            if (hf) GetClassNameW(hf, cls, 31);
            bool in_edit = (_wcsicmp(cls, L"Edit") == 0);
            if (hf == g_app.hEdtGrpSz && m.wParam == VK_RETURN) {
                SendMessageW(hwnd, WM_COMMAND, MAKEWPARAM(ID_BTN_REGROUP, BN_CLICKED), 0);
                continue;
            }
            if (ctrl && !shift && (m.wParam == 'O' || m.wParam == 'o')) {
                pick_raw_files_dialog(hwnd);
                continue;
            }
            if (ctrl && !shift && (m.wParam == 'L' || m.wParam == 'l')) {
                export_session_log_dialog(hwnd);
                continue;
            }
            if (ctrl && !shift && m.wParam == VK_OEM_COMMA) {
                show_engine_popup_menu(hwnd, g_app.hBtnPerfHeader, true);
                continue;
            }
            if (ctrl && !shift && (m.wParam == 'M' || m.wParam == 'm')) {
                merge_selected_groups_win();
                continue;
            }
            if (ctrl && shift && (m.wParam == 'N' || m.wParam == 'n')) {
                split_selected_frames_win();
                continue;
            }
            if (!in_edit && hf != g_app.hBurstView && hf != g_app.hFrameView) {
                if (ctrl && !shift && (m.wParam == 'A' || m.wParam == 'a')) {
                    if (!g_app.groups.empty()) {
                        g_app.group_sel.assign(g_app.groups.size(), 1);
                        sync_cross_tab_ui();
                        repaint_left_dock_views();
                    }
                    continue;
                }
                if (m.wParam == VK_DELETE || m.wParam == VK_BACK) {
                    remove_selected_groups_win();
                    continue;
                }
            }
        }
        TranslateMessage(&m);
        DispatchMessageW(&m);
    }
    Gdiplus::GdiplusShutdown(gdip_tok);
    CoUninitialize();
    return 0;
}
