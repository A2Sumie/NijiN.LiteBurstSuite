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

    // Bottom Global Action Bar & Header Performance Pill
    ID_BTN_RUN = 450,
    ID_BTN_EXPORT_LOG,
    ID_BTN_OPEN_OUT,
    ID_BTN_PERF_HEADER = 455,
    ID_CMB_CPU_CORES,
    ID_CMB_PRIORITY,

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

    ID_FMENU_TOGGLE_INC = 540,
    ID_FMENU_KEEP_ONLY,
    ID_FMENU_RESTORE_ALL,
    ID_FMENU_LOCK_ANC,
    ID_FMENU_SPLIT,

    // Dynamic .lng Language Menu Commands (560..599)
    ID_MENU_LANG_BASE = 560
};

constexpr UINT WM_APP_STATUS       = WM_APP + 1;
constexpr UINT WM_APP_PROGRESS     = WM_APP + 2;
constexpr UINT WM_APP_DONE         = WM_APP + 3;
constexpr UINT WM_APP_PLAN_READY   = WM_APP + 4;
constexpr UINT WM_APP_OPTICS_READY = WM_APP + 5;

static const wchar_t* kRegSubKey = L"Software\\NijiN\\nijinLBS";

// Muted Greyish-Aqua Palette ("略灰的水蓝")
static constexpr COLORREF kColCanvas       = RGB(231, 239, 242);
static constexpr COLORREF kColCard         = RGB(249, 252, 253);
static constexpr COLORREF kColCardSub      = RGB(242, 247, 249);
static constexpr COLORREF kColCardBorder   = RGB(195, 212, 220);
static constexpr COLORREF kColSubBorder    = RGB(211, 225, 232);
static constexpr COLORREF kColInputEdge    = RGB(182, 203, 212);
static constexpr COLORREF kColDisabledFill = RGB(239, 244, 246);
static constexpr COLORREF kColAccent       = RGB(79, 128, 148);
static constexpr COLORREF kColAccentDark   = RGB(60, 103, 121);
static constexpr COLORREF kColAccentSoft   = RGB(226, 239, 244);
static constexpr COLORREF kColStopBtn      = RGB(178, 82, 76);
static constexpr COLORREF kColStopBtnDark  = RGB(148, 62, 57);
static constexpr COLORREF kColBadgeFill    = RGB(218, 233, 238);
static constexpr COLORREF kColBadgeEdge    = RGB(172, 198, 209);
static constexpr COLORREF kColBadgeText    = RGB(64, 108, 126);
static constexpr COLORREF kColTextMain     = RGB(34, 49, 58);
static constexpr COLORREF kColTextMuted    = RGB(104, 128, 138);
static constexpr COLORREF kColWarnAmber    = RGB(186, 108, 28);
static constexpr COLORREF kColWarnCoral    = RGB(182, 72, 66);
static constexpr COLORREF kColOkGreen      = RGB(52, 132, 98);

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

static constexpr int kNumRgbFmts = 6;
static const wchar_t* const kPresetCodes[4]    = {L"photo", L"astro", L"sci", L"arc"};
static const wchar_t* const kOlpfCodes[4]      = {L"auto", L"none", L"weak4", L"gauss16"};
static const wchar_t* const kRawFmtCodes[5]    = {L"dng", L"dng-linear", L"dng-fp16", L"dng-hq", L"dng-raw"};
static const wchar_t* const kRgbFmtCodes[kNumRgbFmts] = {L"tiff-deflate", L"tiff-hq", L"png", L"jxs", L"jxs-lossless", L"tiff-raw"};
static const wchar_t* const kRgbScaleCodes[3]  = {L"2x", L"1x", L"both"};
static const wchar_t* const kFieldModeCodes[3] = {L"tiff-alpha", L"split", L"rgb"};
static const wchar_t* const kBackendCodes[3]   = {L"auto", L"gpu", L"cpu"};
static const wchar_t* const kCpuCoresCodes[5]  = {L"auto", L"perf", L"physical", L"eff", L"all"};
static const wchar_t* const kPriorityCodes[4]  = {L"normal", L"high", L"below", L"idle"};

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
    bool has_telemetry = false;
    bool parallax_measured = false, depth_known = false, depth_observable = false;
    std::map<std::string, bool> field_observability;
    double rec_denoise = 1.0;
    double rec_deghost = 1.0;
    int rec_psf_iters = 4;
    bool shutter_shock = false;
    std::wstring shutter_summary = L"待实测防抖与快门状态";
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
    int tab = -1; // -1 = persistent, 0..3 = Tab 1..4
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
    HWND hCmbGrouping = nullptr, hBtnRegroup = nullptr, hLblGroupingHint = nullptr, hBtnIntakeAction = nullptr;
    bool grouping_dirty = false;
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

    // Right Workspace Stage
    HWND hTabBar = nullptr;
    int active_tab = 0; // 0..3

    // Tab 1 controls
    HWND hBtnPortalOptics = nullptr;
    HWND hSegPreset = nullptr;
    HWND hSegMotionTab1 = nullptr;
    HWND hBtnPortalTab2Motion = nullptr;
    HWND hCmbOlpf = nullptr;

    HWND hOutDir = nullptr, hBtnOutBrowse = nullptr, hBtnOutClear = nullptr;
    HWND hChkRaw = nullptr, hCmbRawFmt = nullptr, hChkRelayRaw = nullptr, hChkNoToolExif = nullptr;
    HWND hChkRgb = nullptr, hSegRgbScale = nullptr, hCmbRgbFmt = nullptr, hEdtJxsBpp = nullptr;
    HWND hRadCsSrgb = nullptr, hRadCsAdobe = nullptr;
    HWND hChkField = nullptr, hCmbFieldMode = nullptr, hBtnPortalTab3Fields = nullptr;
    HWND hChkTransientTab1 = nullptr, hBtnPortalTab2Trans = nullptr;

    HWND hBtnResetPreset = nullptr;
    HWND hSldDenoise = nullptr, hEdtDenoise = nullptr;
    HWND hSldPsfIters = nullptr, hEdtPsfIters = nullptr;
    HWND hSldChroma = nullptr, hEdtChroma = nullptr;
    HWND hSldPsfSigma = nullptr, hEdtPsfSigma = nullptr;
    HWND hSldDeghost = nullptr, hEdtDeghost = nullptr;
    HWND hSldEv = nullptr, hEdtEv = nullptr;

    HWND hChkSiliconClean = nullptr, hChkIbpOn = nullptr, hChkAutoLca = nullptr;
    HWND hChkAutoExp = nullptr, hChkRolloff = nullptr, hChkDither = nullptr;
    HWND hCmbBackend = nullptr, hCmbCpuCores = nullptr, hCmbPriority = nullptr;
    HWND hEdtGrpSz = nullptr, hEdtThreads = nullptr;
    HWND hBtnPerfHeader = nullptr, hBtnEngineMenu = nullptr, hLblEngineStatus = nullptr;

    // Tab 2 controls
    HWND hRadT2MotStd = nullptr, hSegT2Anchor = nullptr;
    HWND hRadT2MotClean = nullptr, hSldCleanSens = nullptr, hEdtCleanSens = nullptr;
    HWND hRadT2MotVnd = nullptr, hSldVndThresh = nullptr, hEdtVndThresh = nullptr;
    HWND hRadT2MotStrobe = nullptr, hEdtStrobeStep = nullptr, hSegStrobeFade = nullptr;
    HWND hChkTransientTab2 = nullptr, hSldTransSigma = nullptr, hEdtTransSigma = nullptr, hSegTransColor = nullptr;
    HWND hChkReflOn = nullptr, hSegReflMode = nullptr, hLblReflDrift = nullptr;
    HWND hParallaxMeter = nullptr;
    HWND hChkExportParallax = nullptr, hSldParallaxAmp = nullptr, hEdtParallaxAmp = nullptr;
    HWND hChkExportRefocus = nullptr, hSldRefocusPos = nullptr, hEdtRefocusPos = nullptr;
    HWND hSldBokehStr = nullptr, hEdtBokehStr = nullptr;

    // Tab 3 controls
    HWND hBtnFieldsBasic3 = nullptr, hBtnFieldsAll = nullptr;
    HWND hSegT3Carrier = nullptr;
    HWND hChkFields[kNumFields] = {};
    HWND hChkExportTiles = nullptr, hSegTileSize = nullptr, hSldTileConf = nullptr, hEdtTileConf = nullptr;

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
    HFONT hFontTitle = nullptr, hFontBadge = nullptr, hFontMono = nullptr;
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

static int min_valid_frames_win() {
    bool emit_raw = g_app.hChkRaw ? get_chk(g_app.hChkRaw) : true;
    bool emit_rgb = g_app.hChkRgb ? get_chk(g_app.hChkRgb) : true;
    if (!emit_raw && emit_rgb && selected_rgb_scale_code() == L"1x") return 3;
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

        COLORREF fillCol = !enabled ? RGB(230, 237, 240)
                         : checked  ? (hovered ? kColAccentDark : kColAccent)
                         : (hovered ? RGB(236, 244, 247) : RGB(255, 255, 255));
        COLORREF edgeCol = !enabled ? RGB(196, 208, 214)
                         : checked  ? (hovered ? kColAccentDark : kColAccent)
                         : (hovered ? kColAccent : RGB(168, 192, 202));

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
        COLORREF fillCol = !enabled ? kColDisabledFill : (hovered ? RGB(246, 250, 252) : RGB(255, 255, 255));
        COLORREF edgeCol = !enabled ? RGB(204, 217, 223) : (hovered ? kColAccent : kColInputEdge);
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

static void draw_modern_pill_button(const DRAWITEMSTRUCT* dis) {
    FillRect(dis->hDC, &dis->rcItem, g_app.hBrCard);
    bool disabled  = (dis->itemState & ODS_DISABLED) != 0;
    bool pushed    = (dis->itemState & ODS_SELECTED) != 0;
    bool hovered   = GetPropW(dis->hwndItem, L"NijiHover") != nullptr;
    bool is_portal = GetPropW(dis->hwndItem, L"NijiPortal") != nullptr;
    bool is_run    = (dis->CtlID == ID_BTN_RUN);
    bool is_stop   = is_run && g_app.running.load();
    bool is_perf_breath = (dis->CtlID == ID_BTN_PERF_HEADER) && g_app.perf_breath_active && !disabled;

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
        } else if (is_run) {
            COLORREF bg = disabled ? RGB(172, 190, 198)
                        : pushed   ? RGB(50, 90, 106)
                        : hovered  ? RGB(68, 115, 134) : kColAccent;
            COLORREF bd = disabled ? RGB(158, 178, 186) : kColAccentDark;
            gdip_round_rect(g, 0.75f, 0.75f, w, h, (float)g_app.S(8), gdip_col(bg), gdip_col(bd), 1.1f);
        } else if (is_perf_breath) {
            auto lerp_c = [](int a, int b, float t) -> BYTE {
                int v = a + (int)std::lround((b - a) * t);
                return (BYTE)std::max(0, std::min(255, v));
            };
            float t = pushed ? 1.0f : (hovered ? std::max(0.75f, g_app.perf_breath_phase) : g_app.perf_breath_phase);
            COLORREF bg = RGB(lerp_c(254, 255, t), lerp_c(249, 220, t), lerp_c(224, 102, t));
            COLORREF bd = RGB(lerp_c(218, 204, t), lerp_c(174, 128, t), lerp_c(64, 12, t));
            gdip_round_rect(g, 0.75f, 0.75f, w, h, (float)g_app.S(6), gdip_col(bg), gdip_col(bd), 1.5f);
        } else if (is_portal) {
            COLORREF bg = pushed ? RGB(204, 224, 232)
                        : hovered ? RGB(214, 232, 239)
                        : portal_active ? RGB(215, 234, 241) : kColBadgeFill;
            COLORREF bd = (hovered || pushed || portal_active) ? kColAccent : kColBadgeEdge;
            gdip_round_rect(g, 0.75f, 0.75f, w, h, (float)g_app.S(7), gdip_col(bg), gdip_col(bd), 1.1f);
        } else {
            COLORREF bg = disabled ? RGB(238, 243, 245)
                        : pushed   ? RGB(213, 229, 236)
                        : hovered  ? RGB(228, 239, 244) : RGB(242, 247, 250);
            COLORREF bd = disabled ? RGB(208, 219, 224)
                        : (hovered || pushed) ? kColAccent : kColInputEdge;
            gdip_round_rect(g, 0.75f, 0.75f, w, h, (float)g_app.S(6), gdip_col(bg), gdip_col(bd), 1.1f);
        }
    }

    SetBkMode(dis->hDC, TRANSPARENT);
    COLORREF tcol = is_run ? RGB(255, 255, 255)
                  : disabled ? kColTextMuted
                  : is_perf_breath ? RGB(98, 60, 8)
                  : is_portal ? (portal_active ? kColAccentDark : kColBadgeText)
                  : kColTextMain;
    SetTextColor(dis->hDC, tcol);
    HGDIOBJ of = SelectObject(dis->hDC, (is_run || portal_active || is_perf_breath) ? g_app.hFontBold : g_app.hFont);
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
        COLORREF trayFill = is_tab ? RGB(217, 230, 236) : RGB(233, 241, 244);
        COLORREF trayEdge = is_tab ? RGB(184, 204, 213) : RGB(192, 210, 219);
        gdip_round_rect(g, 0.5f, 0.5f, (float)w - 1.5f, (float)h - 1.5f, rad,
                        gdip_col(trayFill), gdip_col(trayEdge), 1.1f);

        float pad = (float)g_app.S(is_tab ? 3 : 2);
        float innerW = (float)w - 2.0f * pad;
        float segW = innerW / (float)n;

        // Subtle dividers between unselected segments
        Gdiplus::Pen divPen(gdip_col(RGB(196, 213, 221), 170), 1.0f);
        for (int i = 1; i < n; ++i) {
            if (i == sd->sel || i - 1 == sd->sel) continue;
            float dx = pad + segW * (float)i;
            g.DrawLine(&divPen, dx, pad + 4.0f, dx, (float)h - pad - 4.0f);
        }

        if (sd->hover >= 0 && sd->hover < n && sd->hover != sd->sel && enabled &&
            !(sd->disabled_mask & (1u << sd->hover))) {
            float hx = pad + segW * (float)sd->hover;
            gdip_round_rect(g, hx + 0.5f, pad + 0.5f, segW - 1.0f, (float)h - 2.0f * pad - 1.0f,
                            rad - 2.0f, gdip_col(RGB(240, 247, 250)), Gdiplus::Color(0, 0, 0, 0), 0.0f);
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
                COLORREF selBg = enabled ? kColAccent : RGB(166, 184, 192);
                COLORREF selBd = enabled ? kColAccentDark : RGB(154, 172, 180);
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

    COLORREF trkBg = enabled ? RGB(217, 230, 236) : RGB(232, 238, 241);
    COLORREF trkBd = enabled ? RGB(194, 212, 220) : RGB(212, 222, 227);
    gdip_round_rect(g, pad, cy - 0.5f * th, tw, th, 0.5f * th, gdip_col(trkBg), gdip_col(trkBd), 1.0f);

    if (kx > pad + 1.0f) {
        COLORREF fillC = enabled ? kColAccent : RGB(174, 190, 198);
        gdip_round_rect(g, pad, cy - 0.5f * th, kx - pad, th, 0.5f * th,
                        gdip_col(fillC), Gdiplus::Color(0, 0, 0, 0), 0.0f);
    }

    float kr = (float)g_app.S((ad->dragging || ad->hovered) ? 7 : 6);
    COLORREF kFill = !enabled ? RGB(236, 241, 244) : RGB(255, 255, 255);
    COLORREF kEdge = !enabled ? RGB(180, 194, 202) : (ad->dragging ? kColAccentDark : kColAccent);
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
        wchar_t mbuf[220] = {0};
        if (!grp.camera_model.empty()) {
            std::swprintf(mbuf, 220, en ? L"%ls · %ls · Quads %d/4 · Score %d" : L"%ls · %ls · 象限 %d/4 · 潜力 %d",
                          utf8_to_wide(grp.camera_model).c_str(), scene_code_to_cn(grp.scene_code).c_str(),
                          grp.quad_coverage, grp.sr_score);
        } else {
            std::swprintf(mbuf, 220, en ? L"%ls · Quads %d/4 · Score %d/100" : L"%ls · 四象限 %d/4 · 超分潜力 %d/100",
                          scene_code_to_cn(grp.scene_code).c_str(), grp.quad_coverage, grp.sr_score);
        }
        grp.meta_line = mbuf;
        std::swprintf(mbuf, 220,
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
        COLORREF bg = hov ? RGB(236, 245, 248) : RGB(244, 249, 251);
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
            COLORREF rbg = is_sel ? kColAccentSoft : RGB(243, 249, 251);
            COLORREF rbd = is_sel ? RGB(166, 200, 214) : RGB(224, 236, 241);
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
            Gdiplus::Pen sep(gdip_col(RGB(233, 241, 244)), 1.0f);
            g.DrawLine(&sep, (float)g_app.S(12), (float)y1 - 0.5f, (float)(w - g_app.S(12)), (float)y1 - 0.5f);
        }

        // Checkbox
        float cbx = (float)g_app.S(14);
        float cby = (float)y0 + (float)g_app.S(10);
        float cbs = (float)g_app.S(15);
        COLORREF cFill = grp.checked ? kColAccent : RGB(255, 255, 255);
        COLORREF cEdge = grp.checked ? kColAccentDark : RGB(168, 192, 202);
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

        int badgeW = g_app.S(58);
        int badgeX = rightEdge - badgeW;
        gdip_round_rect(g, (float)badgeX, (float)y0 + (float)g_app.S(7), (float)badgeW, (float)g_app.S(18),
                        (float)g_app.S(5), gdip_col(kColBadgeFill), gdip_col(kColBadgeEdge), 1.0f);
        HGDIOBJ of = SelectObject(hdc, g_app.hFontBadge);
        SetTextColor(hdc, kColBadgeText);
        RECT rb{badgeX, y0 + g_app.S(7), rightEdge, y0 + g_app.S(25)};
        DrawTextW(hdc, grp.sensor_badge.c_str(), -1, &rb, DT_CENTER | DT_VCENTER | DT_SINGLELINE);

        SelectObject(hdc, g_app.hFontBold);
        SetTextColor(hdc, grp.checked ? kColTextMain : kColTextMuted);
        std::wstring line1 = grp.title;
        if (!grp.span_label.empty()) line1 += L"   " + grp.span_label;
        RECT rl1{g_app.S(36), y0 + g_app.S(6), badgeX - g_app.S(6), y0 + g_app.S(26)};
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
                            gdip_col(rxHov ? kColAccent : RGB(224, 240, 246)),
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
                        gdip_col(RGB(172, 196, 206)), Gdiplus::Color(0, 0, 0, 0), 0.0f);
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
                            gdip_col(is_sel ? RGB(226, 240, 246) : RGB(244, 249, 251)),
                            is_sel ? gdip_col(RGB(174, 206, 218)) : Gdiplus::Color(0, 0, 0, 0),
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
                        gdip_col(fd.included ? kColAccentDark : RGB(174, 194, 204)), 1.15f);
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
        Gdiplus::Pen rpen(gdip_col(is_anc ? kColAccentDark : RGB(174, 194, 204)), 1.2f);
        g.FillEllipse(&rbr, rdx, rdy, rds, rds);
        g.DrawEllipse(&rpen, rdx, rdy, rds, rds);
        if (is_anc) {
            float dot = rds * 0.42f;
            Gdiplus::SolidBrush wbr(Gdiplus::Color(255, 255, 255, 255));
            g.FillEllipse(&wbr, rdx + 0.5f * (rds - dot), rdy + 0.5f * (rds - dot), dot, dot);
        }

        HGDIOBJ of = SelectObject(hdc, is_anc ? g_app.hFontBold : g_app.hFontSmall);
        SetTextColor(hdc, fd.included ? kColTextMain : kColTextMuted);
        std::wstring nm = L"#" + std::to_wstring(fd.idx_1based) + L" " + stem_of_w(fd.file);
        RECT rnm{g_app.S(50), y0, g_app.S(172), y1};
        DrawTextW(hdc, nm.c_str(), -1, &rnm, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);

        float bx = (float)g_app.S(176);
        float bw = (float)g_app.S(70);
        float bh = (float)g_app.S(6);
        float by = (float)y0 + 0.5f * ((float)rowH - bh);
        float ratio = (float)std::max(0.06, std::min(1.0, fd.acutance / max_acu));
        gdip_round_rect(g, bx, by, bw, bh, 0.5f * bh, gdip_col(RGB(224, 235, 240)), Gdiplus::Color(0, 0, 0, 0), 0.0f);
        COLORREF barCol = !fd.included ? RGB(180, 194, 202)
                        : fd.is_crown  ? kColAccent : RGB(104, 152, 170);
        gdip_round_rect(g, bx, by, bw * ratio, bh, 0.5f * bh, gdip_col(barCol), Gdiplus::Color(0, 0, 0, 0), 0.0f);

        int normScore = (int)std::llround(99.0 * (fd.acutance / max_acu));
        wchar_t sbuf[32] = {0};
        std::swprintf(sbuf, 32, L"%d%ls", normScore, fd.is_crown ? L"♛" : L"");
        SelectObject(hdc, fd.is_crown ? g_app.hFontBold : g_app.hFontSmall);
        SetTextColor(hdc, fd.is_crown ? kColAccentDark : kColTextMain);
        RECT rsc{g_app.S(250), y0, g_app.S(294), y1};
        DrawTextW(hdc, sbuf, -1, &rsc, DT_LEFT | DT_VCENTER | DT_SINGLELINE);

        std::swprintf(sbuf, 32, L"%.1fpx", fd.shift_px);
        SelectObject(hdc, g_app.hFontSmall);
        SetTextColor(hdc, kColTextMuted);
        RECT rsh{g_app.S(296), y0, g_app.S(352), y1};
        DrawTextW(hdc, sbuf, -1, &rsh, DT_RIGHT | DT_VCENTER | DT_SINGLELINE);

        std::wstring anom = flags_to_anomaly_cn(fd.flags, fd.is_shock, grp->locked_anchor_1based == fd.idx_1based);
        if (!anom.empty()) {
            int px0 = g_app.S(360);
            int pw0 = g_app.S(50);
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
                        gdip_col(RGB(172, 196, 206)), Gdiplus::Color(0, 0, 0, 0), 0.0f);
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
    float bw = (float)g_app.S(250);
    float bh = (float)g_app.S(8);
    float by = 0.5f * ((float)h - bh);
    gdip_round_rect(g, bx, by, bw, bh, 0.5f * bh, gdip_col(RGB(216, 229, 235)), Gdiplus::Color(0, 0, 0, 0), 0.0f);

    float tickX = bx + bw * 0.5f;
    Gdiplus::Pen tickPen(gdip_col(kColAccentDark), 1.4f);
    g.DrawLine(&tickPen, tickX, by - 2.5f, tickX, by + bh + 2.5f);

    if ((has_meas || has_preview) && px2 > 0.05) {
        float ratio = (float)std::min(1.0, px2 / 16.0);
        COLORREF fc = pass ? kColOkGreen : RGB(156, 176, 186);
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
    RECT rr{g_app.S(404), 0, w - g_app.S(10), h};
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

    RECT rhRc{rx0 + g_app.S(14), g_app.S(8), w - g_app.S(14), g_app.S(30)};
    DrawTextW(hdc, heraw_i18n::tr_w("t4.radar_title").c_str(), -1, &rhRc, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);

    float plotCx = 0.5f * (float)pw - (float)g_app.S(24);
    float plotCy = 0.5f * (float)h + (float)g_app.S(2);
    float plotR  = (float)std::min(pw - g_app.S(110), h - g_app.S(92)) * 0.5f;
    if (plotR < 36.0f) plotR = 36.0f;

    Gdiplus::Pen ringPen(gdip_col(RGB(205, 220, 228)), 1.0f);
    Gdiplus::REAL dashPat[2] = {3.0f, 3.0f};
    ringPen.SetDashPattern(dashPat, 2);
    g.DrawEllipse(&ringPen, plotCx - plotR, plotCy - plotR, 2.0f * plotR, 2.0f * plotR);
    g.DrawEllipse(&ringPen, plotCx - 0.5f * plotR, plotCy - 0.5f * plotR, plotR, plotR);

    Gdiplus::Pen axisPen(gdip_col(RGB(180, 200, 210)), 1.1f);
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
                            gdip_col(on ? kColAccent : RGB(230, 238, 242)),
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

    int rw = w - rx0;
    int gBadgeW = g_app.S(168);
    int gBadgeX = rx0 + rw - gBadgeW - g_app.S(12);
    COLORREF gbFill = g_app.optics.measured
                    ? (g_app.optics.is_star_gold ? RGB(248, 240, 214) : kColBadgeFill)
                    : RGB(233, 240, 243);
    COLORREF gbText = g_app.optics.measured
                    ? (g_app.optics.is_star_gold ? RGB(148, 102, 18) : kColAccentDark)
                    : kColTextMuted;
    gdip_round_rect(g, (float)gBadgeX, (float)g_app.S(34), (float)gBadgeW, (float)g_app.S(20),
                    (float)g_app.S(5), gdip_col(gbFill), gdip_col(kColBadgeEdge), 1.0f);
    SelectObject(hdc, g_app.hFontSmall);
    SetTextColor(hdc, gbText);
    RECT gbr{gBadgeX, g_app.S(34), gBadgeX + gBadgeW, g_app.S(54)};
    DrawTextW(hdc, g_app.optics.grade_badge.c_str(), -1, &gbr, DT_CENTER | DT_VCENTER | DT_SINGLELINE);

    float rCx = (float)rx0 + 0.5f * (float)rw;
    float rCy = 0.5f * (float)h + (float)g_app.S(12);
    float dxC = (float)rw * 0.28f;
    float dyC = (float)h * 0.23f;

    Gdiplus::PointF zPos[5] = {
        Gdiplus::PointF(rCx,       rCy),
        Gdiplus::PointF(rCx - dxC, rCy - dyC),
        Gdiplus::PointF(rCx + dxC, rCy - dyC),
        Gdiplus::PointF(rCx - dxC, rCy + dyC),
        Gdiplus::PointF(rCx + dxC, rCy + dyC)
    };
    static const wchar_t* kZNames[5] = {L"Center", L"NW", L"NE", L"SW", L"SE"};

    Gdiplus::Pen fldPen(gdip_col(RGB(205, 220, 228)), 1.0f);
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

        COLORREF zFill = !g_app.optics.measured ? RGB(228, 238, 242)
                       : rej ? RGB(236, 238, 240) : RGB(214, 234, 242);
        COLORREF zEdge = !g_app.optics.measured ? kColBadgeEdge
                       : rej ? RGB(190, 198, 202) : kColAccent;
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
static std::wstring build_fields_csv() {
    std::wstring csv;
    for (int i = 0; i < kNumFields; ++i) {
        if (get_chk(g_app.hChkFields[i])) {
            if (!csv.empty()) csv += L",";
            csv += kFieldSpecs[i].code;
        }
    }
    return csv;
}

static std::wstring build_effective_fields_csv(const BurstGroupItem* grp) {
    std::wstring csv;
    for (int i = 0; i < kNumFields; ++i) {
        if (!get_chk(g_app.hChkFields[i])) continue;
        if (grp) {
            auto it = grp->field_observability.find(wide_to_utf8(kFieldSpecs[i].code));
            if (it != grp->field_observability.end() && !it->second) continue;
        }
        if (!csv.empty()) csv += L",";
        csv += kFieldSpecs[i].code;
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
    return {L"--export-refocus", L"--refocus-pos", std::to_wstring(std::clamp((offset + 1.0) * 0.5, 0.0, 1.0)),
            L"--bokeh-strength", std::to_wstring(12.0 * strength)};
}

static int count_checked_fields() {
    int c = 0;
    for (int i = 0; i < kNumFields; ++i) if (get_chk(g_app.hChkFields[i])) ++c;
    return c;
}

static void apply_fields_csv(const std::wstring& csv) {
    if (csv.empty()) return;
    std::wstring padded = L"," + csv + L",";
    for (int i = 0; i < kNumFields; ++i) {
        std::wstring tok = std::wstring(L",") + kFieldSpecs[i].code + L",";
        set_chk_val(g_app.hChkFields[i], padded.find(tok) != std::wstring::npos);
    }
}

static void apply_preset_to_controls(int idx) {
    idx = std::max(0, std::min(3, idx));
    g_app.preset_idx = idx;
    if (g_app.hSegPreset) seg_pill_set_sel(g_app.hSegPreset, idx);

    if (idx == 1) { // 星空 astro
        set_tuned_pair(g_app.hSldDenoise,  g_app.hEdtDenoise,  L"0.85");
        set_tuned_pair(g_app.hSldChroma,   g_app.hEdtChroma,   L"0.90");
        set_tuned_pair(g_app.hSldPsfIters, g_app.hEdtPsfIters, L"3");
        set_tuned_pair(g_app.hSldPsfSigma, g_app.hEdtPsfSigma, L"0.95");
        set_tuned_pair(g_app.hSldDeghost,  g_app.hEdtDeghost,  L"0.85");
        set_tuned_pair(g_app.hSldEv,       g_app.hEdtEv,       L"0.0");
        set_chk_val(g_app.hChkSiliconClean, true);
        set_chk_val(g_app.hChkIbpOn,        true);
        set_chk_val(g_app.hChkAutoLca,      true);
        set_chk_val(g_app.hChkAutoExp,      true);
        set_chk_val(g_app.hChkRolloff,      true);
        set_chk_val(g_app.hChkDither,       true);
    } else if (idx == 2) { // 科学 sci
        set_tuned_pair(g_app.hSldDenoise,  g_app.hEdtDenoise,  L"0.40");
        set_tuned_pair(g_app.hSldChroma,   g_app.hEdtChroma,   L"0.55");
        set_tuned_pair(g_app.hSldPsfIters, g_app.hEdtPsfIters, L"0");
        set_tuned_pair(g_app.hSldPsfSigma, g_app.hEdtPsfSigma, L"1.05");
        set_tuned_pair(g_app.hSldDeghost,  g_app.hEdtDeghost,  L"1.35");
        set_tuned_pair(g_app.hSldEv,       g_app.hEdtEv,       L"0.0");
        set_chk_val(g_app.hChkSiliconClean, true);
        set_chk_val(g_app.hChkIbpOn,        true);
        set_chk_val(g_app.hChkAutoLca,      false);
        set_chk_val(g_app.hChkAutoExp,      false);
        set_chk_val(g_app.hChkRolloff,      false);
        set_chk_val(g_app.hChkDither,       true);
    } else if (idx == 3) { // 归档 arc
        set_tuned_pair(g_app.hSldDenoise,  g_app.hEdtDenoise,  L"0.70");
        set_tuned_pair(g_app.hSldChroma,   g_app.hEdtChroma,   L"0.85");
        set_tuned_pair(g_app.hSldPsfIters, g_app.hEdtPsfIters, L"0");
        set_tuned_pair(g_app.hSldPsfSigma, g_app.hEdtPsfSigma, L"1.05");
        set_tuned_pair(g_app.hSldDeghost,  g_app.hEdtDeghost,  L"1.10");
        set_tuned_pair(g_app.hSldEv,       g_app.hEdtEv,       L"0.0");
        set_chk_val(g_app.hChkSiliconClean, true);
        set_chk_val(g_app.hChkIbpOn,        true);
        set_chk_val(g_app.hChkAutoLca,      true);
        set_chk_val(g_app.hChkAutoExp,      true);
        set_chk_val(g_app.hChkRolloff,      true);
        set_chk_val(g_app.hChkDither,       true);
    } else { // 照片 photo
        set_tuned_pair(g_app.hSldDenoise,  g_app.hEdtDenoise,  L"1.00");
        set_tuned_pair(g_app.hSldChroma,   g_app.hEdtChroma,   L"1.00");
        set_tuned_pair(g_app.hSldPsfIters, g_app.hEdtPsfIters, L"4");
        set_tuned_pair(g_app.hSldPsfSigma, g_app.hEdtPsfSigma, L"1.05");
        set_tuned_pair(g_app.hSldDeghost,  g_app.hEdtDeghost,  L"1.00");
        set_tuned_pair(g_app.hSldEv,       g_app.hEdtEv,       L"0.0");
        set_chk_val(g_app.hChkSiliconClean, true);
        set_chk_val(g_app.hChkIbpOn,        true);
        set_chk_val(g_app.hChkAutoLca,      true);
        set_chk_val(g_app.hChkAutoExp,      true);
        set_chk_val(g_app.hChkRolloff,      true);
        set_chk_val(g_app.hChkDither,       true);
    }
}

static void switch_tab(int tab_idx) {
    tab_idx = std::max(0, std::min(3, tab_idx));
    g_app.active_tab = tab_idx;
    if (g_app.hTabBar) seg_pill_set_sel(g_app.hTabBar, tab_idx);

    for (const auto& tc : g_app.tab_ctrls) {
        if (tc.tab >= 0 && tc.hw) {
            ShowWindow(tc.hw, (tc.tab == tab_idx) ? SW_SHOW : SW_HIDE);
        }
    }
    sync_cross_tab_ui();
    if (g_app.hwnd) {
        RECT crc{};
        GetClientRect(g_app.hwnd, &crc);
        RECT stage{g_app.S(486), g_app.S(64), crc.right, crc.bottom - g_app.S(68)};
        InvalidateRect(g_app.hwnd, &stage, FALSE);
    }
}

static void update_grouping_hint() {
    if (!g_app.hLblGroupingHint) return;
    auto S = [](int px) { return g_app.S(px); };
    if (g_app.grouping_dirty) {
        if (g_app.hBtnIntakeAction) ShowWindow(g_app.hBtnIntakeAction, SW_HIDE);
        MoveWindow(g_app.hLblGroupingHint, S(34), S(142), S(428), S(36), TRUE);
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
            MoveWindow(g_app.hLblGroupingHint, S(34), S(142), S(308), S(36), TRUE);
            MoveWindow(g_app.hBtnIntakeAction, S(346), S(148), S(116), S(24), TRUE);
            ShowWindow(g_app.hBtnIntakeAction, SW_SHOW);
            InvalidateRect(g_app.hBtnIntakeAction, nullptr, FALSE);
        } else {
            ShowWindow(g_app.hBtnIntakeAction, SW_HIDE);
            MoveWindow(g_app.hLblGroupingHint, S(34), S(142), S(428), S(36), TRUE);
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
    const bool en = heraw_i18n::I18nManager::instance().is_english();

    if (g_app.motion_mode_idx >= 0 && g_app.motion_mode_idx <= 4) {
        seg_pill_set_sel(g_app.hSegMotionTab1, g_app.motion_mode_idx);
    } else {
        seg_pill_set_sel(g_app.hSegMotionTab1, -1);
    }
    seg_pill_set_item_text(g_app.hSegMotionTab1, 0,
        (locked_f > 0 && g_app.motion_mode_idx <= 2)
            ? ((en ? L"Lock #" : L"定格 #") + std::to_wstring(locked_f))
            : heraw_i18n::tr_w("t1.mot_auto"));

    set_chk_val(g_app.hRadT2MotStd,    g_app.motion_mode_idx <= 2);
    set_chk_val(g_app.hRadT2MotClean,  g_app.motion_mode_idx == 3);
    set_chk_val(g_app.hRadT2MotVnd,    g_app.motion_mode_idx == 4);
    set_chk_val(g_app.hRadT2MotStrobe, g_app.motion_mode_idx == 5);
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

    std::wstring rgb_code = selected_rgb_fmt_code();
    if (g_app.hSegRgbScale) EnableWindow(g_app.hSegRgbScale, emit_rgb);
    EnableWindow(g_app.hCmbRgbFmt,  emit_rgb);
    EnableWindow(g_app.hEdtJxsBpp,  emit_rgb && (rgb_code == L"jxs"));
    EnableWindow(g_app.hRadCsSrgb,  emit_rgb);
    EnableWindow(g_app.hRadCsAdobe, emit_rgb);

    bool emit_fld = get_chk(g_app.hChkField);
    int fm_sel = (int)SendMessageW(g_app.hCmbFieldMode, CB_GETCURSEL, 0, 0);
    if (fm_sel < 0 || fm_sel > 2) fm_sel = 0;
    EnableWindow(g_app.hCmbFieldMode, emit_fld);
    seg_pill_set_sel(g_app.hSegT3Carrier, emit_fld ? (fm_sel + 1) : 0);

    if (emit_fld && fm_sel == 0) {
        if (!emit_rgb) {
            set_chk_val(g_app.hChkRgb, true);
            emit_rgb = true;
            if (g_app.hSegRgbScale) EnableWindow(g_app.hSegRgbScale, TRUE);
            EnableWindow(g_app.hCmbRgbFmt, TRUE);
        }
        if (rgb_code != L"tiff-deflate" && rgb_code != L"tiff-hq" && rgb_code != L"tiff-raw") {
            SendMessageW(g_app.hCmbRgbFmt, CB_SETCURSEL, 0, 0);
        }
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

    bool parallax_blocked = grp && grp->depth_known && !grp->depth_observable;
    EnableWindow(g_app.hChkExportParallax, !parallax_blocked);
    EnableWindow(g_app.hChkExportRefocus,  !parallax_blocked);
    bool par_on = !parallax_blocked && get_chk(g_app.hChkExportParallax);
    bool ref_on = !parallax_blocked && get_chk(g_app.hChkExportRefocus);
    EnableWindow(g_app.hSldParallaxAmp, par_on);
    EnableWindow(g_app.hEdtParallaxAmp, par_on);
    EnableWindow(g_app.hSldRefocusPos,  ref_on);
    EnableWindow(g_app.hEdtRefocusPos,  ref_on);
    EnableWindow(g_app.hSldBokehStr,    ref_on);
    EnableWindow(g_app.hEdtBokehStr,    ref_on);

    bool trans_on = get_chk(g_app.hChkTransientTab1);
    set_chk_val(g_app.hChkTransientTab2, trans_on);
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
    }
    EnableWindow(g_app.hBtnExportLcp, !build_lcp_fields_csv().empty());
    for (int i = 0; i < kNumFields; ++i) {
        bool enabled = true;
        if (grp) {
            auto found = grp->field_observability.find(wide_to_utf8(kFieldSpecs[i].code));
            if (found != grp->field_observability.end()) enabled = found->second;
        }
        EnableWindow(g_app.hChkFields[i], enabled);
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
    if (g_app.hBtnPortalTab2Motion) {
        std::wstring mlbl = (g_app.motion_mode_idx == 5 || par_on || ref_on)
                          ? heraw_i18n::tr_w("t1.portal_strobe_on")
                          : heraw_i18n::tr_w("t1.portal_strobe_idle");
        SetWindowTextW(g_app.hBtnPortalTab2Motion, mlbl.c_str());
        InvalidateRect(g_app.hBtnPortalTab2Motion, nullptr, FALSE);
    }
    if (g_app.hBtnPortalTab2Trans) {
        SetWindowTextW(g_app.hBtnPortalTab2Trans,
                       heraw_i18n::tr_w(refl_on ? "t1.portal_refl_on" : "t1.portal_refl_idle").c_str());
        InvalidateRect(g_app.hBtnPortalTab2Trans, nullptr, FALSE);
    }
    if (g_app.hBtnPortalTab3Fields) {
        int fc = count_checked_fields();
        std::wstring fbuf = (tiles_on ? L"• " : L"") + heraw_i18n::trf_w("t1.portal_fields_fmt", fc, kNumFields);
        SetWindowTextW(g_app.hBtnPortalTab3Fields, fbuf.c_str());
        InvalidateRect(g_app.hBtnPortalTab3Fields, nullptr, FALSE);
    }

    bool tab2_extra = (g_app.motion_mode_idx >= 3) || trans_on || refl_on || par_on || ref_on;
    bool tab3_extra = emit_fld || tiles_on;
    bool tab4_extra = g_app.optics.measured;
    seg_pill_set_item_text(g_app.hTabBar, 0, heraw_i18n::tr_w("tab.1"));
    seg_pill_set_item_text(g_app.hTabBar, 1, (tab2_extra ? L"• " : L"") + heraw_i18n::tr_w("tab.2"));
    seg_pill_set_item_text(g_app.hTabBar, 2, (tab3_extra ? L"• " : L"") + heraw_i18n::tr_w("tab.3"));
    seg_pill_set_item_text(g_app.hTabBar, 3, (tab4_extra ? L"• " : L"") + heraw_i18n::tr_w("tab.4"));

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
            int active_groups = 0;
            for (const auto& gitem : g_app.groups) if (gitem.checked) ++active_groups;
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
    EnableWindow(g_app.hEdtGrpSz, grouping_editable && SendMessageW(g_app.hCmbGrouping, CB_GETCURSEL, 0, 0) != 1);
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
        RECT rCard{g_app.S(492), g_app.S(272), crc.right - g_app.S(20), g_app.S(718)};
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

    const std::wstring bat_reg = reg_read_str(L"BatteryDevice");
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
        g_app.perf_breath_active = true;
        g_app.perf_breath_start_ms = GetTickCount64();
        g_app.perf_breath_phase = 0.65f;
        if (hwnd && !g_app.is_screenshot_mode) {
            SetTimer(hwnd, TIMER_PERF_BREATH, 40, nullptr);
        }
    }
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
    if (fm_idx >= 0 && fm_idx < 3) reg_write_str(L"FieldModeCode", kFieldModeCodes[fm_idx]);
    reg_write_str(L"FieldsCsv",       build_fields_csv().empty() ? L"none" : build_fields_csv());

    reg_write_str(L"EmitTransient",   bstr(g_app.hChkTransientTab1));
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
    reg_write_str(L"IbpOn",           bstr(g_app.hChkIbpOn));
    reg_write_str(L"AutoLca",         bstr(g_app.hChkAutoLca));
    reg_write_str(L"AutoExp",         bstr(g_app.hChkAutoExp));
    reg_write_str(L"Rolloff",         bstr(g_app.hChkRolloff));
    reg_write_str(L"Dither",          bstr(g_app.hChkDither));
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
        if (mi >= 0 && mi <= 5) g_app.motion_mode_idx = mi;
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
    for (int i = 0; i < 3; ++i) if (fmc == kFieldModeCodes[i]) { SendMessageW(g_app.hCmbFieldMode, CB_SETCURSEL, (WPARAM)i, 0); break; }
    std::wstring fcsv = reg_read_str(L"FieldsCsv");
    if (!fcsv.empty()) apply_fields_csv(fcsv);

    set_chk_reg(g_app.hChkTransientTab1, L"EmitTransient");
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
    set_chk_reg(g_app.hChkAutoLca,      L"AutoLca");
    set_chk_reg(g_app.hChkAutoExp,      L"AutoExp");
    set_chk_reg(g_app.hChkRolloff,      L"Rolloff");
    set_chk_reg(g_app.hChkDither,       L"Dither");

    std::wstring tabS = reg_read_str(L"ActiveTabIdx");
    if (!tabS.empty()) {
        int ti = _wtoi(tabS.c_str());
        if (ti >= 0 && ti < 4) switch_tab(ti);
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
    ofn.lpstrFilter = L"Camera RAW Files (*.NEF;*.NRW;*.NEV;*.DNG;*.ARW;*.CR2;*.CR3;*.RAF;*.ORF;*.RW2)\0"
                      L"*.NEF;*.NRW;*.NEV;*.DNG;*.ARW;*.CR2;*.CR3;*.RAF;*.ORF;*.RW2;*.PEF;*.IIQ\0All Files (*.*)\0*.*\0";
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

    bool want_raw   = get_chk(g_app.hChkRaw);
    bool want_rgb   = get_chk(g_app.hChkRgb);
    bool want_fld   = get_chk(g_app.hChkField);
    bool want_trans = get_chk(g_app.hChkTransientTab1);
    bool want_par   = get_chk(g_app.hChkExportParallax);
    bool want_ref   = get_chk(g_app.hChkExportRefocus);
    bool want_tiles = get_chk(g_app.hChkExportTiles);
    if (want_fld && count_checked_fields() == 0) {
        set_status_ui(heraw_i18n::tr_w("st.pick_field"));
        return;
    }

    if (!want_raw && !want_rgb && !want_fld && !want_trans && !want_par && !want_ref && !want_tiles) {
        set_status_ui(heraw_i18n::tr_w("st.alert_no_output"));
        return;
    }

    std::wstring exe = resolve_active_cli_or_prompt(g_app.hwnd);
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
    args.push_back(L"force-all");
    args.push_back(L"--single-group");

    args.push_back(L"--preset");
    args.push_back(kPresetCodes[std::max(0, std::min(3, g_app.preset_idx))]);

    int olpf_sel = (int)SendMessageW(g_app.hCmbOlpf, CB_GETCURSEL, 0, 0);
    if (olpf_sel >= 0 && olpf_sel < 4) {
        args.push_back(L"--olpf-mode");
        args.push_back(kOlpfCodes[olpf_sel]);
    }

    BurstGroupItem* grp = current_group_ptr();
    if (grp && grp->locked_anchor_1based > 0 && g_app.motion_mode_idx <= 2) {
        args.push_back(L"--anchor-frame");
        args.push_back(std::to_wstring(grp->locked_anchor_1based));
    } else if (g_app.motion_mode_idx == 1) {
        args.push_back(L"--anchor-frame");
        args.push_back(L"first");
    } else if (g_app.motion_mode_idx == 2) {
        args.push_back(L"--anchor-frame");
        args.push_back(L"middle");
    } else {
        args.push_back(L"--anchor-frame");
        args.push_back(L"auto");
    }

    std::wstring skip_csv = build_skip_frames_csv(grp);
    if (!skip_csv.empty()) {
        args.push_back(L"--skip-frames");
        args.push_back(skip_csv);
    }

    auto add_edt_opt = [&](const wchar_t* flag, HWND hEdt) {
        std::wstring v = trim_ws(get_wnd_text(hEdt));
        if (!v.empty()) { args.push_back(flag); args.push_back(v); }
    };

    if (g_app.motion_mode_idx == 3) {
        args.push_back(L"--motion-mode");
        args.push_back(L"clean-plate");
        add_edt_opt(L"--clean-sens", g_app.hEdtCleanSens);
    } else if (g_app.motion_mode_idx == 4) {
        args.push_back(L"--motion-mode");
        args.push_back(L"virtual-nd");
        double vnd_v = std::clamp(_wtof(trim_ws(get_wnd_text(g_app.hEdtVndThresh)).c_str()), 0.10, 1.00);
        wchar_t vbuf[32] = {0};
        std::swprintf(vbuf, 32, L"%.2f", vnd_v);
        args.push_back(L"--vnd-thresh");
        args.push_back(vbuf);
    } else if (g_app.motion_mode_idx == 5) {
        args.push_back(L"--motion-mode");
        args.push_back(L"strobe");
        add_edt_opt(L"--strobe-step", g_app.hEdtStrobeStep);
        args.push_back(L"--strobe-fade");
        args.push_back(seg_pill_get_sel(g_app.hSegStrobeFade) == 1 ? L"1" : L"0");
    }

    append_perf_cli_args(args);

    add_edt_opt(L"--denoise-strength", g_app.hEdtDenoise);
    add_edt_opt(L"--chroma-denoise",   g_app.hEdtChroma);
    add_edt_opt(L"--psf-iters",        g_app.hEdtPsfIters);
    add_edt_opt(L"--psf-sigma",        g_app.hEdtPsfSigma);
    add_edt_opt(L"--deghost-strength", g_app.hEdtDeghost);
    add_edt_opt(L"--ev-offset",        g_app.hEdtEv);
    add_edt_opt(L"--group-size",       g_app.hEdtGrpSz);

    args.push_back(L"--silicon-clean");
    args.push_back(get_chk(g_app.hChkSiliconClean) ? L"on" : L"off");
    args.push_back(L"--ibp-iters");
    args.push_back(get_chk(g_app.hChkIbpOn) ? L"1" : L"0");
    args.push_back(L"--auto-lca");
    args.push_back(get_chk(g_app.hChkAutoLca) ? L"on" : L"off");
    args.push_back(L"--auto-exposure");
    args.push_back(get_chk(g_app.hChkAutoExp) ? L"on" : L"off");
    args.push_back(L"--highlight-rolloff");
    args.push_back(get_chk(g_app.hChkRolloff) ? L"on" : L"off");
    args.push_back(L"--dither");
    args.push_back(get_chk(g_app.hChkDither) ? L"on" : L"off");

    int rsel = (int)SendMessageW(g_app.hCmbRawFmt, CB_GETCURSEL, 0, 0);
    if (rsel >= 0 && rsel < 5) {
        args.push_back(L"--raw-format");
        args.push_back(kRawFmtCodes[rsel]);
    }
    args.push_back(L"--relay-raw");
    args.push_back((want_raw && get_chk(g_app.hChkRelayRaw)) ? L"on" : L"off");
    args.push_back(L"--no-tool-exif");
    args.push_back(get_chk(g_app.hChkNoToolExif) ? L"on" : L"off");

    std::wstring rgb_code = selected_rgb_fmt_code();
    args.push_back(L"--rgb-format");
    args.push_back(rgb_code);
    if (want_rgb) {
        args.push_back(L"--rgb-scale");
        args.push_back(selected_rgb_scale_code());
    }
    if (rgb_code == L"jxs") add_edt_opt(L"--jxs-bpp", g_app.hEdtJxsBpp);
    args.push_back(L"--color-space");
    args.push_back(get_chk(g_app.hRadCsAdobe) ? L"adobergb" : L"srgb");

    int fmsel = (int)SendMessageW(g_app.hCmbFieldMode, CB_GETCURSEL, 0, 0);
    if (fmsel >= 0 && fmsel < 3) {
        args.push_back(L"--field-mode");
        args.push_back(kFieldModeCodes[fmsel]);
    }
    if (want_fld) {
        args.push_back(L"--fields");
        args.push_back(build_fields_csv());
    }

    if (want_trans) {
        args.push_back(L"--emit-transient");
        add_edt_opt(L"--transient-sigma", g_app.hEdtTransSigma);
        args.push_back(L"--transient-color");
        args.push_back(seg_pill_get_sel(g_app.hSegTransColor) == 1 ? L"gray" : L"rgb");
    }

    if (get_chk(g_app.hChkReflOn)) {
        args.push_back(L"--reflection-mode");
        args.push_back(seg_pill_get_sel(g_app.hSegReflMode) == 1 ? L"split-layers" : L"glint-suppress");
    }

    if (want_par) {
        args.push_back(L"--export-parallax-html");
        add_edt_opt(L"--parallax-amp", g_app.hEdtParallaxAmp);
    }
    if (want_ref) {
        auto refocus = refocus_arguments();
        args.insert(args.end(), refocus.begin(), refocus.end());
    }
    if (want_tiles) {
        args.push_back(L"--export-dataset-tiles");
        args.push_back(ensure_output_dir() + L"\\dataset_tiles");
        args.push_back(L"--tile-size");
        args.push_back(seg_pill_get_sel(g_app.hSegTileSize) == 1 ? L"512" : L"256");
        add_edt_opt(L"--tile-min-conf", g_app.hEdtTileConf);
    }

    args.push_back(L"--progress-json");
    std::wstring out_dir = ensure_output_dir();
    args.push_back(L"--out-dir");
    args.push_back(out_dir);
    if (want_raw) args.push_back(L"--emit-raw");
    if (want_rgb) args.push_back(L"--emit-rgb");
    if (want_fld) args.push_back(L"--emit-field");

    auto set_option = [](std::vector<std::wstring>& job, const std::wstring& flag, const std::wstring& value) {
        auto it = std::find(job.begin(), job.end(), flag);
        if (it != job.end()) {
            size_t idx = (size_t)(it - job.begin());
            job.erase(job.begin() + idx, job.begin() + idx + 2);
        }
        if (!value.empty()) { job.push_back(flag); job.push_back(value); }
    };
    g_app.pending_jobs.clear();
    g_app.pending_group_indices.clear();
    int checked_group_total = 0;
    for (const auto& group : g_app.groups) {
        if (group.checked && !group.paths.empty()) ++checked_group_total;
    }
    for (size_t gi = 0; gi < g_app.groups.size(); ++gi) {
        const auto& group = g_app.groups[gi];
        if (!group.checked || group.paths.empty()) continue;
        std::vector<std::wstring> job = args;
        set_option(job, L"--skip-frames", build_skip_frames_csv(&group));
        std::wstring anchor = g_app.motion_mode_idx == 1 ? L"first" : g_app.motion_mode_idx == 2 ? L"middle" : L"auto";
        if (group.locked_anchor_1based > 0 && g_app.motion_mode_idx <= 2) anchor = std::to_wstring(group.locked_anchor_1based);
        set_option(job, L"--anchor-frame", anchor);
        if (want_tiles && checked_group_total > 1) {
            wchar_t gdir[64] = {0};
            std::swprintf(gdir, 64, L"\\dataset_tiles\\group_%02d", (int)gi + 1);
            set_option(job, L"--export-dataset-tiles", out_dir + gdir);
        }
        if (want_fld) {
            std::wstring eff_fields = build_effective_fields_csv(&group);
            set_option(job, L"--fields", eff_fields);
            if (eff_fields.empty()) {
                job.erase(std::remove(job.begin(), job.end(), L"--emit-field"), job.end());
            }
        }
        if (group.depth_known && !group.depth_observable) {
            for (const auto* flag : {L"--export-parallax-html", L"--export-refocus"})
                job.erase(std::remove(job.begin(), job.end(), flag), job.end());
        }
        bool has_output = false;
        for (const auto* flag : {L"--emit-raw", L"--emit-rgb", L"--emit-field", L"--emit-transient",
                                 L"--export-parallax-html", L"--export-refocus", L"--export-dataset-tiles"})
            has_output |= std::find(job.begin(), job.end(), flag) != job.end();
        if (!has_output) continue;
        for (auto& arg : job) if (arg == L"--sort-time") arg = L"--no-sort-time";
        job.insert(job.end(), group.paths.begin(), group.paths.end());
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

    // 1. Header Bar (0, 0, cw, 56)
    RECT hdr{0, 0, cw, S(56)};
    FillRect(hdc, &hdr, g_app.hBrCard);

    HBRUSH brAccent = CreateSolidBrush(RGB(84, 130, 150));
    RECT topStripe{0, 0, cw, S(3)};
    FillRect(hdc, &topStripe, brAccent);
    DeleteObject(brAccent);

    HBRUSH brBorder = CreateSolidBrush(kColCardBorder);
    RECT hdrLine{0, S(55), cw, S(56)};
    FillRect(hdc, &hdrLine, brBorder);
    DeleteObject(brBorder);

    // Measure left header title "NijiN.LiteBurstSuite" for crisp pill placement
    SetBkMode(hdc, TRANSPARENT);
    HGDIOBJ oldFont = SelectObject(hdc, g_app.hFontTitle);
    RECT titleMeasure{S(20), 0, S(500), S(56)};
    DrawTextW(hdc, L"NijiN.LiteBurstSuite", -1, &titleMeasure, DT_LEFT | DT_SINGLELINE | DT_CALCRECT);
    int titleW = titleMeasure.right - titleMeasure.left;
    RECT titleRc{S(20), 0, S(20) + titleW + S(4), S(56)};
    SetTextColor(hdc, kColTextMain);
    DrawTextW(hdc, L"NijiN.LiteBurstSuite", -1, &titleRc, DT_LEFT | DT_VCENTER | DT_SINGLELINE);

    int pillX = S(20) + titleW + S(10);
    int pillW = S(68);
    int pillY = S(18);
    int pillH = S(20);

    // Right-Side Co-Brand Cluster: tightly cropped GX Cross-Star left of NijiN
    int nijinDstW = S(124);
    int nijinDstH = S(35);
    int gxDstW    = S(41);
    int gxDstH    = S(37);
    int nx        = cw - S(20) - nijinDstW;
    int divX      = nx - S(6);
    int gxX       = divX - S(7) - gxDstW;
    int gxY       = (S(56) - gxDstH) / 2;
    int njY       = (S(56) - nijinDstH) / 2;


    int rightStageW = std::max(S(768), cw - S(512));
    auto stage_x = [&](int x) { return S(492) + MulDiv(S(x - 492), rightStageW, S(768)); };
    int bottomBarY  = ch - S(70);
    int bottomBarW  = cw - S(40);

    {
        Gdiplus::Graphics g(hdc);
        g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
        g.SetInterpolationMode(Gdiplus::InterpolationModeHighQualityBicubic);
        g.SetPixelOffsetMode(Gdiplus::PixelOffsetModeHighQuality);

        // Draw @2x PNG cropped brand marks
        if (g_app.brandGx.bmp) {
            g.DrawImage(g_app.brandGx.bmp,
                        Gdiplus::Rect(gxX, gxY, gxDstW, gxDstH),
                        g_app.brandGx.sx, g_app.brandGx.sy, g_app.brandGx.sw, g_app.brandGx.sh,
                        Gdiplus::UnitPixel);
        } else {
            draw_alpha_bmp(hdc, g_app.hBmpGxFallback, heraw_brand::kGx1xW, heraw_brand::kGx1xH,
                           gxX, gxY, gxDstW, gxDstH);
        }
        if (g_app.brandNijin.bmp) {
            g.DrawImage(g_app.brandNijin.bmp,
                        Gdiplus::Rect(nx, njY, nijinDstW, nijinDstH),
                        g_app.brandNijin.sx, g_app.brandNijin.sy, g_app.brandNijin.sw, g_app.brandNijin.sh,
                        Gdiplus::UnitPixel);
        } else {
            draw_alpha_bmp(hdc, g_app.hBmpNijinFallback, heraw_brand::kNijin1xW, heraw_brand::kNijin1xH,
                           nx, njY, nijinDstW, nijinDstH);
        }

        // Header aqua "nijinLBS" pill
        gdip_round_rect(g, (float)pillX, (float)pillY, (float)pillW, (float)pillH,
                        (float)S(6), gdip_col(kColBadgeFill), gdip_col(kColBadgeEdge), 1.0f);

        auto card = [&](int x, int y, int w, int h) {
            gdip_round_rect(g, (float)x, (float)y, (float)w, (float)h,
                            (float)S(11), gdip_col(kColCard), gdip_col(kColCardBorder), 1.15f);
        };

        // Persistent Left Master-Detail Dock (x = 20..476)
        card(S(20), S(68),  S(456), S(352)); // Upper Card: Burst Queue
        card(S(20), S(430), S(456), bottomBarY - S(440)); // Lower Card: Frame AF Inspector

        // Active Right Tab Stage Cards (x = 492..1260)
        if (g_app.active_tab == 0) {
            card(S(492), S(114), rightStageW, S(150)); // Scene & Temporal Mode
            card(S(492), S(272), rightStageW, S(208)); // Output & Quick Portals
            card(S(492), S(488), rightStageW, bottomBarY - S(498)); // Reconstruction & Compute
        } else if (g_app.active_tab == 1) {
            card(S(492), S(114), rightStageW, S(196)); // Motion Mode
            card(S(492), S(318), rightStageW, S(188)); // Extra Separated Layers
            card(S(492), S(514), rightStageW, bottomBarY - S(524)); // 2.5D & Bokeh Refocus
        } else if (g_app.active_tab == 2) {
            card(S(492), S(114), rightStageW, S(420)); // Carrier & Field Matrix
            card(S(492), S(542), rightStageW, bottomBarY - S(552)); // Sci Dataset Factory

            // 2x2 Sub-Cards inside Tab 3 Field Matrix
            auto subbox = [&](int x, int y, int w, int h) {
                gdip_round_rect(g, (float)stage_x(x), (float)S(y), (float)(stage_x(x + w) - stage_x(x)), (float)S(h),
                                (float)S(8), gdip_col(kColCardSub), gdip_col(kColSubBorder), 1.0f);
            };
            subbox(510, 200, 358, 152);
            subbox(884, 200, 358, 152);
            subbox(510, 362, 358, 156);
            subbox(884, 362, 358, 156);
        } else if (g_app.active_tab == 3) {
            card(S(492), S(114), rightStageW, S(380)); // Inline Vector Diagnostics
            card(S(492), S(502), rightStageW, S(80));  // Silicon Readout
            card(S(492), S(590), rightStageW, bottomBarY - S(600)); // LCP & Exports
        }

        // Bottom Global Action Bar
        card(S(20), bottomBarY, bottomBarW, S(56));

        // Rounded Input Wells around VISIBLE borderless EDIT controls
        HWND hChild = GetWindow(hwnd, GW_CHILD);
        while (hChild) {
            if (IsWindowVisible(hChild)) {
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
                        gdip_col(en ? kColInputEdge : RGB(204, 217, 223)), 1.15f);
                }
            }
            hChild = GetWindow(hChild, GW_HWNDNEXT);
        }

        // Smooth Rounded Aqua Progress Bar in Bottom Global Action Bar
        int statRightBar = cw - S(252);
        float px = (float)S(316);
        float py = (float)(bottomBarY + S(36));
        float pw = std::max(120.0f, (float)(statRightBar - S(316)));
        float ph = (float)S(8);
        gdip_round_rect(g, px, py, pw, ph, 0.5f * ph,
                        gdip_col(RGB(218, 231, 236)), gdip_col(RGB(198, 216, 224)), 1.0f);
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

    // Tab 3 2x2 Sub-Box Headers
    if (g_app.active_tab == 2) {
        SelectObject(hdc, g_app.hFontBold);
        SetTextColor(hdc, kColAccentDark);
        auto subhdr = [&](int x, int y, const std::wstring& t) {
            RECT sr{stage_x(x), S(y), stage_x(x + 338), S(y + 22)};
            DrawTextW(hdc, t.c_str(), -1, &sr, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);
        };
        subhdr(522, 206, heraw_i18n::tr_w("t3.box0"));
        subhdr(896, 206, heraw_i18n::tr_w("t3.box1"));
        subhdr(522, 368, heraw_i18n::tr_w("t3.box2"));
        subhdr(896, 368, heraw_i18n::tr_w("t3.box3"));

        SelectObject(hdc, g_app.hFontSmall);
        SetTextColor(hdc, kColTextMuted);
        RECT rNote{stage_x(896), S(476), stage_x(1238), S(514)};
        BurstGroupItem* group = current_group_ptr();
        std::wstring note = heraw_i18n::tr_w("t3.gate_idle");
        if (group && !group->field_observability.empty()) {
            int available = 0;
            for (const auto& item : group->field_observability) if (item.second) ++available;
            note = heraw_i18n::trf_w("t3.gate_measured_fmt", available, (int)group->field_observability.size());
        }
        DrawTextW(hdc, note.c_str(), -1, &rNote, DT_LEFT | DT_WORDBREAK | DT_NOPREFIX);
    }

    // Inline Status Text & Percentage inside Bottom Action Bar
    int statRight = cw - S(252);
    SelectObject(hdc, g_app.hFont);
    SetTextColor(hdc, kColTextMain);
    RECT rStat{S(316), bottomBarY + S(11), statRight - S(52), bottomBarY + S(31)};
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
        heraw_i18n::tr_w("t1.rgb_jxs_lossless"), heraw_i18n::tr_w("t1.rgb_tiff_raw")
    });
    reset_combo_items_keep_sel(g_app.hCmbFieldMode, {
        heraw_i18n::tr_w("t1.fld_tiff_alpha"), heraw_i18n::tr_w("t1.fld_split"),
        heraw_i18n::tr_w("t1.fld_rgb")
    });
    reset_combo_items_keep_sel(g_app.hCmbBackend, {
        heraw_i18n::tr_w("t1.bk_auto"),
        heraw_i18n::I18nManager::instance().is_english() ? L"Force Vulkan GPU" : L"强制Vulkan GPU",
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
        heraw_i18n::I18nManager::instance().is_english() ? L"Above Normal" : L"高性能优先",
        heraw_i18n::tr_w("menu.prio_below"),
        heraw_i18n::tr_w("menu.prio_idle")
    });

    if (g_app.hOutDir) {
        std::wstring ph = heraw_i18n::tr_w("t1.outdir_ph");
        SendMessageW(g_app.hOutDir, EM_SETCUEBANNER, FALSE, reinterpret_cast<LPARAM>(ph.c_str()));
    }

    seg_pill_set_items(g_app.hTabBar, {
        heraw_i18n::tr_w("tab.1"), heraw_i18n::tr_w("tab.2"),
        heraw_i18n::tr_w("tab.3"), heraw_i18n::tr_w("tab.4")
    });
    seg_pill_set_items(g_app.hSegPreset, {
        heraw_i18n::tr_w("t1.preset_photo"), heraw_i18n::tr_w("t1.preset_astro"),
        heraw_i18n::tr_w("t1.preset_sci"), heraw_i18n::tr_w("t1.preset_arc")
    });
    seg_pill_set_items(g_app.hSegMotionTab1, {
        heraw_i18n::tr_w("t1.mot_auto"), heraw_i18n::tr_w("t1.mot_first"),
        heraw_i18n::tr_w("t1.mot_middle"), heraw_i18n::tr_w("t1.mot_clean"),
        heraw_i18n::tr_w("t1.mot_vnd")
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
    seg_pill_set_items(g_app.hSegT3Carrier, {
        heraw_i18n::tr_w("t3.car_off"), heraw_i18n::tr_w("t3.car_tiff"),
        heraw_i18n::tr_w("t3.car_split"), heraw_i18n::tr_w("t3.car_rgb")
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
    AppendMenuW(hEng, MF_STRING, ID_MENU_LIBRAW_RESET,  en ? L"Restore Built-in LibRaw 0.23-HE*" : L"恢复内置 LibRaw 0.23-HE*");
    AppendMenuW(hMenu, MF_POPUP, reinterpret_cast<UINT_PTR>(hEng), en ? L"CLI Engine & External LibRaw" : L"CLI 引擎与外部 LibRaw");

    RECT br{};
    GetWindowRect(hAnchor ? hAnchor : g_app.hBtnEngineMenu, &br);
    UINT flags = align_bottom_down ? (TPM_RIGHTALIGN | TPM_TOPALIGN) : (TPM_RIGHTALIGN | TPM_BOTTOMALIGN);
    int py = align_bottom_down ? (br.bottom + 2) : br.top;
    TrackPopupMenuEx(hMenu, flags, br.right, py, hwnd, nullptr);
    DestroyMenu(hMenu);
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
            g_app.hFontBadge = CreateFontW(-S(11), 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
                DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
            g_app.hFontMono = CreateFontW(-S(11), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                FIXED_PITCH | FF_MODERN, L"Consolas");

            HINSTANCE hInst = GetModuleHandleW(nullptr);
            auto reg_ctrl = [&](HWND hw, int tab, const char* key = nullptr) {
                RECT rect{};
                GetWindowRect(hw, &rect);
                MapWindowPoints(nullptr, hwnd, reinterpret_cast<LPPOINT>(&rect), 2);
                g_app.tab_ctrls.push_back(TaggedCtrl{hw, tab, rect, key});
                if (tab > 0) ShowWindow(hw, SW_HIDE);
                return hw;
            };
            auto mk = [&](int tab, const wchar_t* cls, const wchar_t* txt, DWORD st, int x, int y, int w, int h, int id, const char* key = nullptr) {
                HWND hw = CreateWindowExW(0, cls, txt, WS_CHILD | WS_VISIBLE | st,
                    S(x), S(y), S(w), S(h), hwnd, reinterpret_cast<HMENU>((INT_PTR)id), hInst, nullptr);
                SendMessageW(hw, WM_SETFONT, reinterpret_cast<WPARAM>(g_app.hFont), TRUE);
                reg_ctrl(hw, tab, key);
                g_app.tab_ctrls.back().design = RECT{S(x), S(y), S(x + w), S(y + h)};
                return hw;
            };
            auto mk_lbl = [&](int tab, const wchar_t* txt, int x, int y, int w, int h = 20, int id = 0, const char* key = nullptr) {
                return mk(tab, L"STATIC", txt, SS_LEFT | SS_NOPREFIX, x, y, w, h, id, key);
            };
            auto mk_hdr = [&](int tab, const wchar_t* txt, int x, int y, int w, int id = 0, const char* key = nullptr) {
                HWND hw = mk(tab, L"STATIC", txt, SS_LEFT | SS_NOPREFIX, x, y, w, 22, id, key);
                SendMessageW(hw, WM_SETFONT, reinterpret_cast<WPARAM>(g_app.hFontCardHdr), TRUE);
                return hw;
            };
            auto mk_btn = [&](int tab, const wchar_t* txt, int x, int y, int w, int h, int id, bool portal = false, const char* key = nullptr) {
                HWND hw = mk(tab, L"BUTTON", txt, BS_OWNERDRAW, x, y, w, h, id, key);
                if (portal) SetPropW(hw, L"NijiPortal", reinterpret_cast<HANDLE>(1));
                SetWindowSubclass(hw, ModernBtnHoverSubclassProc, 3, 0);
                return hw;
            };
            auto mk_chk = [&](int tab, const wchar_t* txt, int x, int y, int w, int id, bool checked = false, COLORREF bg = kColCard, const char* key = nullptr) {
                HWND hw = mk(tab, L"BUTTON", txt, BS_AUTOCHECKBOX, x, y, w, 22, id, key);
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
                return reg_ctrl(hw, tab);
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
                return reg_ctrl(hw, tab);
            };

            // ================================================================
            // Persistent Top Header Performance Pill & Left Master-Detail Dock (x = 20..476)
            // ================================================================
            g_app.hBtnPerfHeader = mk_btn(-1, L"性能: GPU优先 · 智能异构 ▾", 886, 15, 182, 26, ID_BTN_PERF_HEADER, true);
            mk_hdr(-1, L"连拍队列", 34, 80, 120, 0, "dock.queue_hdr");
            g_app.hChkSortTime  = mk_chk(-1, L"EXIF时间排序", 224, 80, 118, ID_CHK_SORT_TIME, true, kColCard, "dock.sort_time");
            g_app.hChkRecursive = mk_chk(-1, L"递归子目录",   350, 80, 110, ID_CHK_RECURSIVE, true, kColCard, "dock.recursive");
            g_app.hCmbGrouping = mk_cmb(-1, 34, 110, 140, 140, ID_CMB_GROUPING);
            SendMessageW(g_app.hCmbGrouping, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"自动分组"));
            SendMessageW(g_app.hCmbGrouping, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"整组处理"));
            SendMessageW(g_app.hCmbGrouping, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"每簇精选"));
            SendMessageW(g_app.hCmbGrouping, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"全库挑1组"));
            SendMessageW(g_app.hCmbGrouping, CB_SETCURSEL, 0, 0);
            mk_lbl(-1, L"目标张数", 184, 114, 66, 20, 0, "dock.grp_sz_cap");
            g_app.hEdtGrpSz = mk_edt(-1, L"8", 254, 110, 50, 24, ID_EDT_GROUP_SIZE, ES_NUMBER);
            g_app.hBtnRegroup = mk_btn(-1, L"重新分组", 322, 108, 140, 28, ID_BTN_REGROUP, false, "dock.regroup");
            g_app.hLblGroupingHint = mk_lbl(-1, L"导入后自动剖析连拍簇（支持 Ctrl/Shift 多选、空格批勾、合并与拆分）", 34, 142, 428, 36);
            SendMessageW(g_app.hLblGroupingHint, WM_SETFONT, reinterpret_cast<WPARAM>(g_app.hFontSmall), TRUE);
            g_app.hBtnIntakeAction = mk_btn(-1, L"✦ 仅勾最优1组", 346, 148, 116, 24, ID_BTN_INTAKE_ACTION, true);
            ShowWindow(g_app.hBtnIntakeAction, SW_HIDE);

            g_app.hBurstView = CreateWindowExW(0, L"NijiBurstQueue", L"", WS_CHILD | WS_VISIBLE,
                S(34), S(182), S(428), S(190), hwnd, reinterpret_cast<HMENU>((INT_PTR)ID_VIEW_BURST_QUEUE), hInst, nullptr);

            g_app.hBtnAddFiles    = mk_btn(-1, L"+文件",      34,  380, 60, 28, ID_BTN_ADD_FILES,    false, "dock.btn_add_files");
            g_app.hBtnAddFolder   = mk_btn(-1, L"+目录",      99,  380, 60, 28, ID_BTN_ADD_FOLDER,   false, "dock.btn_add_dir");
            g_app.hBtnMergeGroups = mk_btn(-1, L"合并组",     164, 380, 68, 28, ID_BTN_MERGE_GROUPS, false, "dock.btn_merge");
            g_app.hBtnCleanStray  = mk_btn(-1, L"清散片",     237, 380, 68, 28, ID_BTN_CLEAN_STRAY,  false, "dock.btn_clean");
            g_app.hBtnExportTopK  = mk_btn(-1, L"Top-K选片",  310, 380, 94, 28, ID_BTN_EXPORT_TOPK,  false, "dock.btn_topk");
            g_app.hBtnClear       = mk_btn(-1, L"清空",       409, 380, 53, 28, ID_BTN_CLEAR,        false, "dock.btn_clear");

            g_app.hLblFrameHdr    = mk_hdr(-1, L"组内单帧检视", 34, 442, 236, 0, "dock.insp_hdr");
            g_app.hBtnSplitFrames = mk_btn(-1, L"拆为新组", 276, 440, 88, 26, ID_BTN_SPLIT_FRAMES, false, "dock.btn_split");
            g_app.hBtnLockFrame   = mk_btn(-1, L"定格此帧", 370, 440, 92, 26, ID_BTN_LOCK_FRAME,   false, "dock.btn_lock_win");

            g_app.hFrameView = CreateWindowExW(0, L"NijiFrameInsp", L"", WS_CHILD | WS_VISIBLE,
                S(34), S(472), S(428), S(234), hwnd, reinterpret_cast<HMENU>((INT_PTR)ID_VIEW_FRAME_INSP), hInst, nullptr);

            // ================================================================
            // Right Top 4-Segment Workspace Tab Bar (x = 492..1260)
            // ================================================================
            g_app.hTabBar = mk_seg(-1,
                {L"1 超分与主输出", L"2 时空与创意", L"3 物理场与数据", L"4 镜头与机身"},
                492, 68, 768, 38, ID_TAB_BAR, true);

            // ================================================================
            // Tab 1 (tab = 0): 1 超分与主输出 (Home Page + Quick Portals)
            // ================================================================
            mk_hdr(0, L"场景与时间模式", 510, 126, 220, 0, "t1.c1_title");
            g_app.hBtnPortalOptics = mk_btn(0, L"四象限 --/4 · 体检 ▸", 1074, 123, 168, 26, ID_BTN_PORTAL_TAB4_OPTICS, true);

            mk_lbl(0, L"预设模式", 510, 161, 66, 20, 0, "t1.preset_cap");
            g_app.hSegPreset = mk_seg(0, {L"照片", L"星空", L"科学", L"归档"}, 580, 156, 440, 28, ID_SEG_PRESET);

            mk_lbl(0, L"动体处理", 510, 197, 66, 20, 0, "t1.motion_cap");
            g_app.hSegMotionTab1 = mk_seg(0,
                {L"自动最锐", L"首帧", L"中间", L"空镜(去人)", L"慢门(流水)"},
                580, 192, 532, 28, ID_SEG_MOTION_TAB1);
            g_app.hBtnPortalTab2Motion = mk_btn(0, L"频闪/光场 ▸", 1120, 192, 122, 28, ID_BTN_PORTAL_TAB2_MOTION, true);

            mk_lbl(0, L"光学匹配", 510, 232, 66, 20, 0, "t1.olpf_cap");
            g_app.hCmbOlpf = mk_cmb(0, 580, 228, 440, 160, ID_CMB_OLPF_MODE);
            SendMessageW(g_app.hCmbOlpf, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"自动识别传感器低通"));
            SendMessageW(g_app.hCmbOlpf, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"无低通椭圆核"));
            SendMessageW(g_app.hCmbOlpf, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"4点双折射弱低通核"));
            SendMessageW(g_app.hCmbOlpf, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"16点高斯低通核"));
            SendMessageW(g_app.hCmbOlpf, CB_SETCURSEL, 0, 0);

            // Card 2: 输出与快捷附加层
            mk_hdr(0, L"输出与快捷附加层", 510, 282, 240, 0, "t1.c2_title");
            g_app.hChkNoToolExif = mk_chk(0, L"不在 EXIF 内写入本工具信息", 1012, 281, 230, ID_CHK_NO_TOOL_EXIF, false, kColCard, "t1.no_tool_exif");

            mk_lbl(0, L"输出目录", 510, 314, 66, 20, 0, "t1.outdir_cap");
            g_app.hOutDir = mk_edt(0, L"", 580, 310, 554, 24, ID_EDT_OUT_DIR);
            SendMessageW(g_app.hOutDir, EM_SETCUEBANNER, FALSE, reinterpret_cast<LPARAM>(L"默认同目录 nijinLBS_out"));
            g_app.hBtnOutBrowse = mk_btn(0, L"浏览", 1142, 309, 66, 26, ID_BTN_OUT_BROWSE, false, "t1.browse");
            g_app.hBtnOutClear  = mk_btn(0, L"×",    1214, 309, 28, 26, ID_BTN_OUT_CLEAR);

            g_app.hChkRaw = mk_chk(0, L"2× RGGB 底片", 510, 346, 128, ID_CHK_EMIT_RAW, true, kColCard, "t1.emit_raw");
            g_app.hCmbRawFmt = mk_cmb(0, 642, 344, 248, 180, ID_CMB_RAW_FMT);
            SendMessageW(g_app.hCmbRawFmt, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"DNG 16-bit 无损"));
            SendMessageW(g_app.hCmbRawFmt, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"DNG 三通道线性 (Capture One 兼容)"));
            SendMessageW(g_app.hCmbRawFmt, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"DNG 16-bit 半精度浮点 (0.29×)"));
            SendMessageW(g_app.hCmbRawFmt, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"DNG 高级有损压缩 (0.09×)"));
            SendMessageW(g_app.hCmbRawFmt, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"DNG 16-bit 无压缩 (1.00×)"));
            SendMessageW(g_app.hCmbRawFmt, CB_SETCURSEL, 0, 0);
            g_app.hChkRelayRaw = mk_chk(0, L"外部降噪直通", 900, 346, 150, ID_CHK_RELAY_RAW, false, kColCard, "t1.relay_raw");

            g_app.hChkRgb = mk_chk(0, L"RGB 成片", 510, 380, 96, ID_CHK_EMIT_RGB, true, kColCard, "t1.emit_rgb");
            g_app.hSegRgbScale = mk_seg(0, {L"2×超分", L"1×真彩", L"双出"}, 610, 378, 170, 25, ID_SEG_RGB_SCALE);
            g_app.hCmbRgbFmt = mk_cmb(0, 784, 378, 212, 190, ID_CMB_RGB_FMT);
            SendMessageW(g_app.hCmbRgbFmt, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"16-bit 无损 TIFF"));
            SendMessageW(g_app.hCmbRgbFmt, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"16-bit 视觉无损 TIFF (HQ, 0.48×)"));
            SendMessageW(g_app.hCmbRgbFmt, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"16-bit 无损 PNG (0.40×)"));
            SendMessageW(g_app.hCmbRgbFmt, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"14-bit JPEG XS 有损 (0.21×)"));
            SendMessageW(g_app.hCmbRgbFmt, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"14-bit JPEG XS 无损 (0.43×)"));
            SendMessageW(g_app.hCmbRgbFmt, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"16-bit 无压缩 TIFF (1.00×)"));
            SendMessageW(g_app.hCmbRgbFmt, CB_SETCURSEL, 0, 0);
            mk_lbl(0, L"BPP", 1002, 382, 28);
            g_app.hEdtJxsBpp = mk_edt(0, L"10.0", 1032, 378, 46, 24, ID_EDT_JXS_BPP);
            g_app.hRadCsSrgb  = mk_rad(0, L"sRGB",      1084, 379, 58, ID_RAD_CS_SRGB,     true);
            g_app.hRadCsAdobe = mk_rad(0, L"Adobe RGB", 1144, 379, 102, ID_RAD_CS_ADOBERGB, false);
            set_chk_val(g_app.hRadCsSrgb, true);

            g_app.hChkField = mk_chk(0, L"物理场图", 510, 414, 116, ID_CHK_EMIT_FIELD, false, kColCard, "t1.emit_field");
            g_app.hCmbFieldMode = mk_cmb(0, 630, 412, 256, 140, ID_CMB_FIELD_MODE);
            SendMessageW(g_app.hCmbFieldMode, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"内嵌至主TIFF通道"));
            SendMessageW(g_app.hCmbFieldMode, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"独立16-bit灰度PNG"));
            SendMessageW(g_app.hCmbFieldMode, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"合并三通道PNG"));
            SendMessageW(g_app.hCmbFieldMode, CB_SETCURSEL, 1, 0);
            g_app.hBtnPortalTab3Fields = mk_btn(0, L"选场(3/11)·数据集 ▸", 900, 411, 174, 26, ID_BTN_PORTAL_TAB3_FIELDS, true);

            g_app.hChkTransientTab1 = mk_chk(0, L"瞬态高光回收层 (流星 / 萤火虫 / 浪花)", 510, 448, 380, ID_CHK_TRANSIENT_TAB1, false, kColCard, "t1.emit_trans");
            g_app.hBtnPortalTab2Trans = mk_btn(0, L"消反光/阈值 ▸", 900, 445, 174, 26, ID_BTN_PORTAL_TAB2_TRANS, true);

            // Card 3: 重建与新噪声态精调
            mk_hdr(0, L"重建与新噪声态精调", 510, 498, 260, 0, "t1.c3_title");
            g_app.hBtnResetPreset = mk_btn(0, L"恢复预设", 1152, 496, 90, 25, ID_BTN_RESET_PRESET, false, "t1.reset_preset");

            mk_lbl(0, L"亮度互谱降噪", 510, 529, 88, 20, 0, "t1.denoise");
            g_app.hEdtDenoise  = mk_edt(0, L"1.00", 796, 525, 56, 24, ID_EDT_DENOISE);
            g_app.hSldDenoise  = mk_sld(0, 0.0, 2.5, 1.0, 2, g_app.hEdtDenoise, 600, 525, 188, 24, ID_SLD_DENOISE);

            mk_lbl(0, L"光学RL迭代", 884, 529, 84, 20, 0, "t1.psf_iters");
            g_app.hEdtPsfIters = mk_edt(0, L"4", 1186, 525, 56, 24, ID_EDT_PSF_ITERS);
            g_app.hSldPsfIters = mk_sld(0, 0.0, 12.0, 4.0, 0, g_app.hEdtPsfIters, 972, 525, 206, 24, ID_SLD_PSF_ITERS);

            mk_lbl(0, L"色空KLT强度", 510, 558, 88, 20, 0, "t1.chroma");
            g_app.hEdtChroma   = mk_edt(0, L"1.00", 796, 554, 56, 24, ID_EDT_CHROMA);
            g_app.hSldChroma   = mk_sld(0, 0.0, 2.5, 1.0, 2, g_app.hEdtChroma, 600, 554, 188, 24, ID_SLD_CHROMA);

            mk_lbl(0, L"PSF半径", 884, 558, 84, 20, 0, "t1.psf_sigma");
            g_app.hEdtPsfSigma = mk_edt(0, L"1.05", 1186, 554, 56, 24, ID_EDT_PSF_SIGMA);
            g_app.hSldPsfSigma = mk_sld(0, 0.35, 3.0, 1.05, 2, g_app.hEdtPsfSigma, 972, 554, 206, 24, ID_SLD_PSF_SIGMA);

            mk_lbl(0, L"去鬼影严格度", 510, 587, 88, 20, 0, "t1.deghost");
            g_app.hEdtDeghost  = mk_edt(0, L"1.00", 796, 583, 56, 24, ID_EDT_DEGHOST);
            g_app.hSldDeghost  = mk_sld(0, 0.4, 3.0, 1.0, 2, g_app.hEdtDeghost, 600, 583, 188, 24, ID_SLD_DEGHOST);

            mk_lbl(0, L"手动曝光EV", 884, 587, 84, 20, 0, "t1.ev");
            g_app.hEdtEv       = mk_edt(0, L"0.0", 1186, 583, 56, 24, ID_EDT_EV_OFFSET);
            g_app.hSldEv       = mk_sld(0, -4.0, 4.0, 0.0, 1, g_app.hEdtEv, 972, 583, 206, 24, ID_SLD_EV_OFFSET);

            g_app.hChkSiliconClean = mk_chk(0, L"硅片条带自扣除", 510, 614, 180, ID_CHK_SILICON_CLEAN, true, kColCard, "t1.silicon_clean");
            g_app.hChkIbpOn        = mk_chk(0, L"紧支集IBP回投",  750, 614, 180, ID_CHK_IBP_ON,        true, kColCard, "t1.ibp");
            g_app.hChkAutoLca      = mk_chk(0, L"盲色差归位",     990, 614, 180, ID_CHK_AUTO_LCA,      true, kColCard, "t1.auto_lca");

            g_app.hChkAutoExp      = mk_chk(0, L"自动曝光",       510, 638, 180, ID_CHK_AUTO_EXP,      true, kColCard, "t1.auto_exp");
            g_app.hChkRolloff      = mk_chk(0, L"高光滚降",       750, 638, 180, ID_CHK_ROLLOFF,       true, kColCard, "t1.rolloff");
            g_app.hChkDither       = mk_chk(0, L"TPDF抖动",       990, 638, 180, ID_CHK_DITHER,        true, kColCard, "t1.dither");

            mk_lbl(0, L"加速", 510, 668, 32, 20, 0, "t1.win_gpu_cap");
            g_app.hCmbBackend = mk_cmb(0, 544, 664, 132, 140, ID_CMB_BACKEND);
            SendMessageW(g_app.hCmbBackend, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"自动(GPU优先)"));
            SendMessageW(g_app.hCmbBackend, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"强制Vulkan GPU"));
            SendMessageW(g_app.hCmbBackend, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"纯CPU"));
            SendMessageW(g_app.hCmbBackend, CB_SETCURSEL, 0, 0);

            mk_lbl(0, L"核分配", 684, 668, 44, 20, 0, "t1.win_cores_cap");
            g_app.hCmbCpuCores = mk_cmb(0, 730, 664, 148, 160, ID_CMB_CPU_CORES);
            {
                std::wstring cmb_lbls[5], mnu_lbls[5];
                build_win_cpu_core_labels(cmb_lbls, mnu_lbls);
                for (int i = 0; i < 5; ++i) {
                    SendMessageW(g_app.hCmbCpuCores, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(cmb_lbls[i].c_str()));
                }
            }
            SendMessageW(g_app.hCmbCpuCores, CB_SETCURSEL, 0, 0);

            mk_lbl(0, L"优先级", 886, 668, 44, 20, 0, "t1.win_prio_cap");
            g_app.hCmbPriority = mk_cmb(0, 932, 664, 126, 140, ID_CMB_PRIORITY);
            SendMessageW(g_app.hCmbPriority, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"标准(防降频)"));
            SendMessageW(g_app.hCmbPriority, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"高性能优先"));
            SendMessageW(g_app.hCmbPriority, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"低于标准"));
            SendMessageW(g_app.hCmbPriority, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"后台省电(Eco)"));
            SendMessageW(g_app.hCmbPriority, CB_SETCURSEL, 0, 0);

            mk_lbl(0, L"线程", 1066, 668, 32, 20, 0, "t1.win_thr_cap");
            g_app.hEdtThreads = mk_edt(0, L"0", 1100, 664, 42, 24, ID_EDT_THREADS, ES_NUMBER);

            g_app.hBtnEngineMenu   = mk_btn(0, L"性能与引擎...", 1148, 663, 96, 26, ID_BTN_ENGINE_MENU, false, "t1.engine_btn");
            g_app.hLblEngineStatus = mk_lbl(0, L"引擎: .\\nijinLBS.exe · 内置 LibRaw 0.23-HE*", 510, 693, 732, 18, ID_LBL_ENGINE_STATUS);
            SendMessageW(g_app.hLblEngineStatus, WM_SETFONT, reinterpret_cast<WPARAM>(g_app.hFontSmall), TRUE);

            // ================================================================
            // Tab 2 (tab = 1): 2 时空与创意
            // ================================================================
            mk_hdr(1, L"时间轴与动体合成模式 (与首页「动体处理」双向同步)", 510, 126, 520, 0, "t2.c1_title");

            g_app.hRadT2MotStd = mk_rad(1, L"标准姿态定格", 510, 158, 138, ID_RAD_T2_MOT_STD, true, true, "t2.freeze");
            g_app.hSegT2Anchor = mk_seg(1,
                {L"自动最锐(AF)", L"首帧", L"中间帧", L"跟随左栏指定帧"},
                656, 156, 560, 28, ID_SEG_T2_ANCHOR);

            g_app.hRadT2MotClean = mk_rad(1, L"零路人纯净空镜 (Clean Plate)", 510, 194, 236, ID_RAD_T2_MOT_CLEAN, false, true, "t2.clean");
            mk_lbl(1, L"行人剔除灵敏度", 754, 197, 104, 20, 0, "t2.clean_sens");
            g_app.hEdtCleanSens = mk_edt(1, L"1.00", 1096, 193, 58, 24, ID_EDT_CLEAN_SENS);
            g_app.hSldCleanSens = mk_sld(1, 0.3, 2.5, 1.0, 2, g_app.hEdtCleanSens, 864, 193, 224, 24, ID_SLD_CLEAN_SENS);

            g_app.hRadT2MotVnd = mk_rad(1, L"手持虚拟慢门流水 (Virtual ND)", 510, 230, 236, ID_RAD_T2_MOT_VND, false, true, "t2.vnd");
            mk_lbl(1, L"流体识别阈值", 754, 233, 104, 20, 0, "t2.vnd_thresh");
            g_app.hEdtVndThresh = mk_edt(1, L"0.65", 1096, 229, 58, 24, ID_EDT_VND_THRESH);
            g_app.hSldVndThresh = mk_sld(1, 0.10, 1.00, 0.65, 2, g_app.hEdtVndThresh, 864, 229, 224, 24, ID_SLD_VND_THRESH);

            g_app.hRadT2MotStrobe = mk_rad(1, L"运动频闪分身轨迹 (Strobe)", 510, 266, 236, ID_RAD_T2_MOT_STROBE, false, true, "t2.strobe");
            mk_lbl(1, L"抽帧步长", 754, 269, 64, 20, 0, "t2.strobe_step");
            g_app.hEdtStrobeStep = mk_edt(1, L"2", 822, 265, 48, 24, ID_EDT_STROBE_STEP, ES_NUMBER);
            mk_lbl(1, L"轨迹衰减", 884, 269, 68, 20, 0, "t2.win_fade_cap");
            g_app.hSegStrobeFade = mk_seg(1, {L"等权实影", L"渐隐残影"}, 956, 264, 200, 27, ID_SEG_STROBE_FADE);
            set_chk_val(g_app.hRadT2MotStd, true);

            // Card T2-2: 附加分离图层提取
            mk_hdr(1, L"附加分离图层提取 (可与主图同步导出)", 510, 330, 440, 0, "t2.c2_title");

            g_app.hChkTransientTab2 = mk_chk(1, L"剥离瞬态高光回收层 (流星 / 萤火虫 / 飞溅浪花)", 510, 360, 520, ID_CHK_TRANSIENT_TAB2, false, kColCard, "t2.emit_trans");
            mk_lbl(1, L"触发门限 (σ)", 534, 391, 84, 20, 0, "t2.trans_sigma");
            g_app.hEdtTransSigma = mk_edt(1, L"3.5", 818, 387, 54, 24, ID_EDT_TRANS_SIGMA);
            g_app.hSldTransSigma = mk_sld(1, 2.0, 8.0, 3.5, 1, g_app.hEdtTransSigma, 622, 387, 188, 24, ID_SLD_TRANS_SIGMA);
            mk_lbl(1, L"色彩模式", 892, 391, 66, 20, 0, "t2.win_color_cap");
            g_app.hSegTransColor = mk_seg(1, {L"全彩层", L"灰度层"}, 962, 386, 254, 26, ID_SEG_TRANS_COLOR);

            g_app.hChkReflOn = mk_chk(1, L"镜面碎波光压制 / 展柜消反光分离", 510, 424, 440, ID_CHK_REFL_ON, false, kColCard, "t2.refl");
            g_app.hSegReflMode = mk_seg(1,
                {L"原地碎高光压制", L"宽基线双层消反光"},
                534, 450, 390, 26, ID_SEG_REFL_MODE);
            g_app.hLblReflDrift = mk_lbl(1, L"└─ 实测位移跨度: 待导入连拍组", 534, 481, 706, 20, ID_LBL_REFL_DRIFT);
            SendMessageW(g_app.hLblReflDrift, WM_SETFONT, reinterpret_cast<WPARAM>(g_app.hFontSmall), TRUE);

            // Card T2-3: 2.5D & Bokeh Refocus
            mk_hdr(1, L"三维微基线光场与线性光子域散景重对焦", 510, 526, 480, 0, "t2.c3_title");
            g_app.hParallaxMeter = reg_ctrl(CreateWindowExW(0, L"NijiParallaxBar", L"", WS_CHILD | WS_VISIBLE,
                S(510), S(554), S(732), S(28), hwnd, reinterpret_cast<HMENU>((INT_PTR)ID_VIEW_PARALLAX_METER), hInst, nullptr), 1);

            g_app.hChkExportParallax = mk_chk(1, L"导出 2.5D 交互光场包 (.html)", 510, 594, 280, ID_CHK_EXPORT_PARALLAX, false, kColCard, "t2.export_para");
            mk_lbl(1, L"视角环绕幅度", 796, 597, 94, 20, 0, "t2.para_amp");
            g_app.hEdtParallaxAmp = mk_edt(1, L"1.00", 1126, 593, 58, 24, ID_EDT_PARALLAX_AMP);
            g_app.hSldParallaxAmp = mk_sld(1, 0.2, 2.5, 1.0, 2, g_app.hEdtParallaxAmp, 894, 593, 224, 24, ID_SLD_PARALLAX_AMP);

            g_app.hChkExportRefocus = mk_chk(1, L"额外渲染线性光子域散景重对焦图", 510, 632, 420, ID_CHK_EXPORT_REFOCUS, false, kColCard, "t2.export_refocus");
            mk_lbl(1, L"合焦平面偏移", 534, 667, 88, 20, 0, "t2.refocus_pos");
            g_app.hEdtRefocusPos = mk_edt(1, L"0.00", 814, 663, 56, 24, ID_EDT_REFOCUS_POS);
            g_app.hSldRefocusPos = mk_sld(1, -1.0, 1.0, 0.0, 2, g_app.hEdtRefocusPos, 626, 663, 180, 24, ID_SLD_REFOCUS_POS);

            mk_lbl(1, L"散景光圈强度", 894, 667, 88, 20, 0, "t2.bokeh_str");
            g_app.hEdtBokehStr = mk_edt(1, L"1.00", 1174, 663, 58, 24, ID_EDT_BOKEH_STR);
            g_app.hSldBokehStr = mk_sld(1, 0.0, 3.0, 1.0, 2, g_app.hEdtBokehStr, 986, 663, 180, 24, ID_SLD_BOKEH_STR);

            // ================================================================
            // Tab 3 (tab = 2): 3 物理场与数据
            // ================================================================
            mk_hdr(2, L"物理场封装载体与按需提取矩阵", 510, 126, 460, 0, "t3.c1_title");
            g_app.hBtnFieldsBasic3 = mk_btn(2, L"常用三项", 1068, 123, 84, 26, ID_BTN_FIELDS_BASIC3, false, "t3.core3");
            g_app.hBtnFieldsAll    = mk_btn(2, L"全选",     1158, 123, 84, 26, ID_BTN_FIELDS_ALL,    false, "t3.select_all");

            mk_lbl(2, L"导出方式", 510, 165, 66, 20, 0, "t3.carrier_cap");
            g_app.hSegT3Carrier = mk_seg(2,
                {L"不导出", L"内嵌至主TIFF命名通道", L"独立16-bit灰度PNG", L"合并RGB"},
                580, 160, 662, 28, ID_SEG_T3_CARRIER);

            int boxCounts[4] = {0, 0, 0, 0};
            for (int i = 0; i < kNumFields; ++i) {
                int b = kFieldSpecs[i].box_idx;
                int row = boxCounts[b]++;
                int bx = (b % 2 == 0) ? 524 : 898;
                int by = ((b < 2) ? 236 : 398) + row * 32;
                bool def_on = (i < 3); // motion, phase, focus
                g_app.hChkFields[i] = mk_chk(2, kFieldSpecs[i].label, bx, by, 336, ID_CHK_FIELD_BASE + i, def_on, kColCardSub, kFieldSpecs[i].lang_key);
            }

            mk_hdr(2, L"Sci 机身专属自监督真值数据集工厂", 510, 554, 480, 0, "t3.c2_title");
            g_app.hChkExportTiles = mk_chk(2, L"同步切出配对训练样本块 (1× 原始带噪 RAW 块 ↔ 2× 真值 + σ_map)",
                                           510, 588, 690, ID_CHK_EXPORT_TILES, false, kColCard, "t3.export_tiles");
            mk_lbl(2, L"切片尺寸", 510, 632, 74, 20, 0, "t3.tile_size");
            g_app.hSegTileSize = mk_seg(2, {L"256×256", L"512×512"}, 588, 627, 210, 28, ID_SEG_TILE_SIZE);
            mk_lbl(2, L"静态置信门槛", 814, 632, 108, 20, 0, "t3.tile_conf");
            g_app.hEdtTileConf = mk_edt(2, L"0.75", 1168, 628, 60, 24, ID_EDT_TILE_CONF);
            g_app.hSldTileConf = mk_sld(2, 0.30, 0.99, 0.75, 2, g_app.hEdtTileConf, 926, 628, 234, 24, ID_SLD_TILE_CONF);

            // ================================================================
            // Tab 4 (tab = 3): 4 镜头与机身
            // ================================================================
            mk_hdr(3, L"原始 RAW 8× 光学体检雷达", 510, 124, 520, 0, "t4.radar_title");
            g_app.hOpticsCanvas = reg_ctrl(CreateWindowExW(0, L"NijiOpticsCanvas", L"", WS_CHILD | WS_VISIBLE,
                S(510), S(152), S(732), S(292), hwnd, reinterpret_cast<HMENU>((INT_PTR)ID_VIEW_OPTICS_CANVAS), hInst, nullptr), 3);

            g_app.hBtnRunOptics    = mk_btn(3, L"运行原始 RAW 8× 光学精测", 510, 454, 238, 28, ID_BTN_RUN_OPTICS, false, "t4.run_optics");
            g_app.hLblOpticsStatus = mk_lbl(3, g_app.optics.ois_note.c_str(), 758, 458, 484, 20, ID_LBL_OPTICS_STATUS);
            SendMessageW(g_app.hLblOpticsStatus, WM_SETFONT, reinterpret_cast<WPARAM>(g_app.hFontSmall), TRUE);

            mk_hdr(3, L"硅片电子学实测读数", 510, 512, 360, 0, "t4.silicon_title");
            g_app.hLblSiliconStats = mk_lbl(3, silicon_readout(g_app.optics).c_str(), 510, 544, 732, 22, ID_LBL_SILICON_STATS);

            mk_hdr(3, L"校准档与光学体检报告导出", 510, 600, 380, 0, "t4.export_title");
            mk_lbl(3, L".lcp 包含项:", 510, 633, 86, 20, 0, "t4.lcp_cap");
            g_app.hChkLcpLca  = mk_chk(3, L"倍率色差 (LCA)", 598, 631, 156, ID_CHK_LCP_LCA,  true,  kColCard, "t4.lcp_lca");
            g_app.hChkLcpVig  = mk_chk(3, L"真实暗角",       762, 631, 172, ID_CHK_LCP_VIG,  false, kColCard, "t4.lcp_vig");
            g_app.hChkLcpDist = mk_chk(3, L"径向畸变",       940, 631, 168, ID_CHK_LCP_DIST, false, kColCard, "t4.lcp_dist");

            g_app.hBtnExportLcp     = mk_btn(3, L"导出标准 Adobe .lcp 校正档", 510,  666, 236, 30, ID_BTN_EXPORT_LCP,      false, "t4.btn_lcp");
            g_app.hBtnExportPngCard = mk_btn(3, L"导出光学体检图卡 (.png)",    760,  666, 236, 30, ID_BTN_EXPORT_PNG_CARD, false, "t4.btn_png");
            g_app.hBtnExportJsonTel = mk_btn(3, L"导出完整遥测 (.json)",       1010, 666, 232, 30, ID_BTN_EXPORT_JSON_TEL, false, "t4.btn_json");

            // ================================================================
            // Bottom Global Action Bar (x = 20..1260, y = 730..786)
            // ================================================================
            g_app.hBtnRun       = mk_btn(-1, L"开始执行",     34,   741, 264, 34, ID_BTN_RUN,        false, "bot.run");
            g_app.hBtnExportLog = mk_btn(-1, L"导出日志",     1040, 741, 98,  34, ID_BTN_EXPORT_LOG, false, "bot.export_log");
            g_app.hBtnOpenOut   = mk_btn(-1, L"打开输出目录", 1144, 741, 102, 34, ID_BTN_OPEN_OUT,   false, "bot.open_dir");

            apply_preset_to_controls(0);
            apply_ui_language_win();
            return 0;
        }

        case WM_GETMINMAXINFO: {
            auto* mmi = reinterpret_cast<MINMAXINFO*>(lParam);
            if (mmi && g_app.dpi > 0) {
                RECT mr{0, 0, g_app.S(1280), g_app.S(800)};
                AdjustWindowRect(&mr, (DWORD)GetWindowLongPtrW(hwnd, GWL_STYLE), FALSE);
                mmi->ptMinTrackSize.x = mr.right - mr.left;
                mmi->ptMinTrackSize.y = mr.bottom - mr.top;
            }
            return 0;
        }

        case WM_SIZE: {
            int cw = LOWORD(lParam);
            int ch = HIWORD(lParam);
            if (cw > 0 && ch > 0 && g_app.hBtnRun) {
                auto S = [](int px) { return g_app.S(px); };
                int stage_width = std::max(S(768), cw - S(512));
                for (const auto& control : g_app.tab_ctrls) {
                    if (control.tab < 0) continue;
                    const RECT& r = control.design;
                    int x = S(492) + MulDiv(r.left - S(492), stage_width, S(768));
                    int width = MulDiv(r.right - r.left, stage_width, S(768));
                    MoveWindow(control.hw, x, r.top, width, r.bottom - r.top, TRUE);
                }
                int bottomBarY = ch - S(70);
                int gxX = cw - S(198);
                if (g_app.hBtnPerfHeader) {
                    MoveWindow(g_app.hBtnPerfHeader, gxX - S(196), S(15), S(182), S(26), TRUE);
                }
                MoveWindow(g_app.hFrameView, S(34), S(472), S(428), std::max(S(120), bottomBarY - S(484)), TRUE);
                MoveWindow(g_app.hTabBar,    S(492), S(68), std::max(S(768), cw - S(512)), S(38), TRUE);
                MoveWindow(g_app.hBtnRun,     S(34), bottomBarY + S(11), S(264), S(34), TRUE);
                if (g_app.hBtnExportLog) {
                    MoveWindow(g_app.hBtnExportLog, cw - S(240), bottomBarY + S(11), S(98), S(34), TRUE);
                }
                MoveWindow(g_app.hBtnOpenOut, cw - S(136), bottomBarY + S(11), S(102), S(34), TRUE);
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
                    g_app.perf_breath_phase = 0.5f - 0.5f * std::cos((float)el * (2.0f * 3.14159265f / 1500.0f));
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
            SetBkColor(hdc, kColCard);
            if (hCtrl == g_app.hLblEngineStatus || hCtrl == g_app.hLblReflDrift || hCtrl == g_app.hLblOpticsStatus) {
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
                switch (id) {
                    case ID_EDT_DENOISE:      sync_slider_from_edit(g_app.hSldDenoise,     g_app.hEdtDenoise);     break;
                    case ID_EDT_PSF_ITERS:    sync_slider_from_edit(g_app.hSldPsfIters,    g_app.hEdtPsfIters);    break;
                    case ID_EDT_CHROMA:       sync_slider_from_edit(g_app.hSldChroma,      g_app.hEdtChroma);      break;
                    case ID_EDT_PSF_SIGMA:    sync_slider_from_edit(g_app.hSldPsfSigma,    g_app.hEdtPsfSigma);    break;
                    case ID_EDT_DEGHOST:      sync_slider_from_edit(g_app.hSldDeghost,     g_app.hEdtDeghost);     break;
                    case ID_EDT_EV_OFFSET:    sync_slider_from_edit(g_app.hSldEv,          g_app.hEdtEv);          break;
                    case ID_EDT_CLEAN_SENS:   sync_slider_from_edit(g_app.hSldCleanSens,   g_app.hEdtCleanSens);   break;
                    case ID_EDT_VND_THRESH:   sync_slider_from_edit(g_app.hSldVndThresh,   g_app.hEdtVndThresh);   break;
                    case ID_EDT_TRANS_SIGMA:  sync_slider_from_edit(g_app.hSldTransSigma,  g_app.hEdtTransSigma);  break;
                    case ID_EDT_PARALLAX_AMP: sync_slider_from_edit(g_app.hSldParallaxAmp, g_app.hEdtParallaxAmp); break;
                    case ID_EDT_REFOCUS_POS:  sync_slider_from_edit(g_app.hSldRefocusPos,  g_app.hEdtRefocusPos);  break;
                    case ID_EDT_BOKEH_STR:    sync_slider_from_edit(g_app.hSldBokehStr,    g_app.hEdtBokehStr);    break;
                    case ID_EDT_TILE_CONF:    sync_slider_from_edit(g_app.hSldTileConf,    g_app.hEdtTileConf);    break;
                }
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

            if (id >= ID_CHK_FIELD_BASE && id < ID_CHK_FIELD_BASE + kNumFields) {
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
                    switch_tab(3);
                    save_ui_prefs();
                    return 0;
                case ID_BTN_PORTAL_TAB2_MOTION:
                case ID_BTN_PORTAL_TAB2_TRANS:
                    switch_tab(1);
                    save_ui_prefs();
                    return 0;
                case ID_BTN_PORTAL_TAB3_FIELDS:
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

                case ID_SEG_MOTION_TAB1: {
                    int m = seg_pill_get_sel(g_app.hSegMotionTab1);
                    g_app.motion_mode_idx = m;
                    if (m == 1 || m == 2) {
                        BurstGroupItem* grp = current_group_ptr();
                        if (grp) grp->locked_anchor_1based = 0;
                    }
                    sync_cross_tab_ui();
                    InvalidateRect(g_app.hFrameView, nullptr, FALSE);
                    save_ui_prefs();
                    return 0;
                }

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

                case ID_CHK_TRANSIENT_TAB1:
                    set_chk_val(g_app.hChkTransientTab2, get_chk(g_app.hChkTransientTab1));
                    sync_cross_tab_ui();
                    save_ui_prefs();
                    return 0;
                case ID_CHK_TRANSIENT_TAB2:
                    set_chk_val(g_app.hChkTransientTab1, get_chk(g_app.hChkTransientTab2));
                    sync_cross_tab_ui();
                    save_ui_prefs();
                    return 0;

                case ID_SEG_T3_CARRIER: {
                    int csel = seg_pill_get_sel(g_app.hSegT3Carrier);
                    if (csel == 0) {
                        set_chk_val(g_app.hChkField, false);
                    } else {
                        set_chk_val(g_app.hChkField, true);
                        SendMessageW(g_app.hCmbFieldMode, CB_SETCURSEL, (WPARAM)(csel - 1), 0);
                    }
                    sync_cross_tab_ui();
                    save_ui_prefs();
                    return 0;
                }

                case ID_BTN_FIELDS_BASIC3:
                    for (int i = 0; i < kNumFields; ++i) set_chk_val(g_app.hChkFields[i], i < 3);
                    sync_cross_tab_ui();
                    save_ui_prefs();
                    return 0;
                case ID_BTN_FIELDS_ALL:
                    for (int i = 0; i < kNumFields; ++i) set_chk_val(g_app.hChkFields[i], true);
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
                    if (code == CBN_SELCHANGE) {
                        if (id == ID_CMB_BACKEND || id == ID_CMB_CPU_CORES || id == ID_CMB_PRIORITY) {
                            stop_perf_breathing_alert(hwnd);
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
                case ID_CHK_EXPORT_TILES:
                case ID_SEG_TILE_SIZE:
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
                    show_engine_popup_menu(hwnd, g_app.hBtnEngineMenu, false);
                    return 0;
                case ID_MENU_BACKEND_AUTO:
                case ID_MENU_BACKEND_GPU:
                case ID_MENU_BACKEND_CPU:
                    SendMessageW(g_app.hCmbBackend, CB_SETCURSEL, (WPARAM)(id - ID_MENU_BACKEND_AUTO), 0);
                    InvalidateRect(g_app.hCmbBackend, nullptr, FALSE);
                    sync_cross_tab_ui();
                    save_ui_prefs();
                    return 0;
                case ID_MENU_CORES_AUTO:
                case ID_MENU_CORES_PERF:
                case ID_MENU_CORES_PHYS:
                case ID_MENU_CORES_EFF:
                case ID_MENU_CORES_ALL:
                    SendMessageW(g_app.hCmbCpuCores, CB_SETCURSEL, (WPARAM)(id - ID_MENU_CORES_AUTO), 0);
                    InvalidateRect(g_app.hCmbCpuCores, nullptr, FALSE);
                    sync_cross_tab_ui();
                    save_ui_prefs();
                    return 0;
                case ID_MENU_BATTERY_DEVICE: {
                    g_app.is_battery_device = !g_app.is_battery_device;
                    int cur_c = g_app.hCmbCpuCores ? (int)SendMessageW(g_app.hCmbCpuCores, CB_GETCURSEL, 0, 0) : 0;
                    if (g_app.is_battery_device && cur_c == 0) {
                        SendMessageW(g_app.hCmbCpuCores, CB_SETCURSEL, 2, 0);
                        InvalidateRect(g_app.hCmbCpuCores, nullptr, FALSE);
                    } else if (!g_app.is_battery_device && cur_c == 2) {
                        SendMessageW(g_app.hCmbCpuCores, CB_SETCURSEL, 0, 0);
                        InvalidateRect(g_app.hCmbCpuCores, nullptr, FALSE);
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
                    InvalidateRect(g_app.hCmbPriority, nullptr, FALSE);
                    sync_cross_tab_ui();
                    save_ui_prefs();
                    return 0;
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
    bool lcp_ok = build_lcp_fields_csv() == L"vignette,distortion" &&
        lcp_arguments(L"C:/输出 镜头.lcp") == std::vector<std::wstring>{
            L"--optics-only", L"--export-lcp", L"C:/输出 镜头.lcp", L"--lcp-fields", L"vignette,distortion"};
    bool refocus_ok = std::wcstod(get_wnd_text(g_app.hEdtRefocusPos).c_str(), nullptr) == -0.25 &&
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
    // first-launch 10s yellow breathing alert (click dismissal + 2nd-launch silence), and user choice memory.
    {
        // 1) First launch on AMD APU laptop (battery=1): GPU auto-off, no-SMT (physical), yellow breathing ON
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
            g_app.perf_breath_active &&
            (reg_read_str(L"PerfGpuHintSeen") == L"1");

        // Clicking performance pill immediately stops breathing; 2nd launch never flashes again
        stop_perf_breathing_alert(hwnd);
        perf_menu_ok = perf_menu_ok && !g_app.perf_breath_active;
        init_perf_hardware_defaults(hwnd);
        load_ui_prefs();
        perf_menu_ok = perf_menu_ok && !g_app.perf_breath_active;

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

        // 2) First launch on Intel <Xe2 desktop (battery=0): GPU auto-off + yellow breathing ON, SMT stays auto (0)
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
            g_app.perf_breath_active;
        stop_perf_breathing_alert(hwnd);

        // 3) First launch on Intel Xe2+ laptop (battery=1): GPU stays ON (0), no breathing, no-SMT (2)
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

        // 4) First launch on desktop discrete GPU (battery=0): GPU stays ON (0), SMT stays ON (0), no breathing
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
        stop_perf_breathing_alert(hwnd);
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

    std::printf("PERF_MENU_TEST:%s\n", perf_menu_ok ? "PASS" : "FAIL");
    std::printf("GROUPING_CONTROLS_TEST:%s\n", grouping_ok ? "PASS" : "FAIL");
    std::printf("MULTI_SELECT_INTAKE_TEST:%s\n", multi_intake_ok ? "PASS" : "FAIL");
    std::printf("I18N_LNG_TEST:%s\n", i18n_ok ? "PASS" : "FAIL");
    std::printf("LOG_EXPORT_TEST:%s\n", log_export_ok ? "PASS" : "FAIL");
    roundtrip_ok = roundtrip_ok && perf_menu_ok && parser_ok && telemetry_ok && kNumFields == 11 && lcp_ok && refocus_ok &&
        grouping_ok && multi_intake_ok && i18n_ok && log_export_ok &&
        version_isolation_ok;
    std::printf("UI_MEMORY_ENGINE_ISOLATION_TEST:%s\n", version_isolation_ok ? "PASS" : "FAIL");
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
    std::wstring cli_lang;
    int shot_tab = -1;
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
            } else if (a == L"--lang" && i + 1 < wargc) {
                cli_lang = wargv[++i];
            } else if (a.rfind(L"--lang=", 0) == 0) {
                cli_lang = a.substr(7);
            } else if (a == L"--tab" && i + 1 < wargc) {
                shot_tab = _wtoi(wargv[++i]);
            } else if (a.rfind(L"--tab=", 0) == 0) {
                shot_tab = _wtoi(a.c_str() + 6);
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
    g_app.is_screenshot_mode = !shot_path.empty() || test_close;
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
    RECT rc{0, 0, MulDiv(1280, dpi, 96), MulDiv(800, dpi, 96)};
    DWORD style = WS_OVERLAPPEDWINDOW;
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

    if (!g_app.is_screenshot_mode) {
        init_perf_hardware_defaults(hwnd);
        load_ui_prefs();
        if (!cli_lang.empty()) {
            heraw_i18n::I18nManager::instance().set_language(wide_to_utf8(cli_lang));
        }
        apply_ui_language_win();
    }
    if (shot_tab >= 0 && shot_tab <= 3) {
        switch_tab(shot_tab);
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
                single ? L"force-all" : L"multi-group", L"--group-size", get_wnd_text(g_app.hEdtGrpSz), L"--plan-json"};
            if (single) pargs.push_back(L"--single-group");
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

    if (!shot_path.empty()) {
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
