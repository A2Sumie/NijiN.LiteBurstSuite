// heraw_sr2x_gui_win.cpp -- NijiN.LiteBurstScaler (nijinLBS) Native Win32 GUI
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
#include <objidl.h>
#include <gdiplus.h>
#include <commctrl.h>
#include <commdlg.h>
#include <shellapi.h>
#include <shlobj.h>

#include <algorithm>
#include <atomic>
#include <cstdio>
#include <cstring>
#include <cwctype>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "heraw_brand_assets.hpp"

#ifndef EM_SETCUEBANNER
#define EM_SETCUEBANNER 0x1501
#endif
#ifndef IDI_APPICON
#define IDI_APPICON 101
#endif

namespace {

enum CtrlId {
    ID_LIST_INPUTS = 201,
    ID_BTN_ADD_FILES,
    ID_BTN_ADD_FOLDER,
    ID_BTN_REMOVE,
    ID_BTN_CLEAR,
    ID_BTN_INSPECT,
    ID_CHK_SORT_TIME,
    ID_CHK_RECURSIVE,

    ID_RAD_POL_INTERACTIVE,
    ID_RAD_POL_AUTOPRUNE,
    ID_RAD_POL_MULTIGROUP,
    ID_RAD_POL_FORCEALL,

    ID_RAD_PRE_PHOTO,
    ID_RAD_PRE_SCI,
    ID_RAD_PRE_ARC,
    ID_RAD_PRE_ASTRO,

    ID_RAD_MOT_ANCHOR,
    ID_RAD_MOT_DUALLAYER,
    ID_RAD_MOT_CLEANPLATE,
    ID_CHK_EMIT_FIELD,
    ID_CMB_FIELD_MODE,
    ID_CMB_BACKEND,

    ID_EDT_OUT_DIR,
    ID_BTN_OUT_DIR,
    ID_CHK_EMIT_RAW,
    ID_CMB_RAW_FMT,
    ID_CHK_EMIT_RGB,
    ID_CMB_RGB_FMT,
    ID_EDT_JXS_BPP,
    ID_RAD_CS_SRGB,
    ID_RAD_CS_ADOBERGB,

    ID_EDT_CLI_PATH,
    ID_BTN_CLI_BROWSE,
    ID_BTN_CLI_AUTO,
    ID_EDT_LIBRAW_PATH,
    ID_BTN_LIBRAW_BROWSE,
    ID_BTN_LIBRAW_RESET,

    ID_EDT_DENOISE,
    ID_EDT_CHROMA,
    ID_EDT_PSF_ITERS,
    ID_EDT_RGB_PSF_ITERS,
    ID_EDT_PSF_SIGMA,
    ID_EDT_DEGHOST,
    ID_EDT_EV_OFFSET,
    ID_EDT_GROUP_SIZE,
    ID_EDT_THREADS,
    ID_CHK_AUTO_EXP,
    ID_CHK_ROLLOFF,
    ID_CHK_EDGE_POLISH,
    ID_CHK_DITHER,

    ID_BTN_RUN,
    ID_BTN_CANCEL,
    ID_BTN_OPEN_OUT,
    ID_PROG_BAR,
    ID_EDT_LOG
};

constexpr UINT WM_APP_LOG = WM_APP + 1;
constexpr UINT WM_APP_PROGRESS = WM_APP + 2;
constexpr UINT WM_APP_DONE = WM_APP + 3;

static constexpr const wchar_t* kRegSubKey = L"Software\\NijiN\\nijinLBS";

// Muted Greyish-Aqua Palette ("略灰的水蓝")
static constexpr COLORREF kColCanvas     = RGB(231, 239, 242);
static constexpr COLORREF kColCard       = RGB(249, 252, 253);
static constexpr COLORREF kColCardBorder = RGB(195, 212, 220);
static constexpr COLORREF kColInputEdge  = RGB(182, 203, 212);
static constexpr COLORREF kColAccent     = RGB(79, 128, 148);
static constexpr COLORREF kColAccentDark = RGB(62, 106, 124);
static constexpr COLORREF kColBadgeFill  = RGB(218, 233, 238);
static constexpr COLORREF kColBadgeEdge  = RGB(172, 198, 209);
static constexpr COLORREF kColBadgeText  = RGB(64, 108, 126);
static constexpr COLORREF kColTextMain   = RGB(34, 49, 58);
static constexpr COLORREF kColTextMuted  = RGB(104, 128, 138);
static constexpr COLORREF kColLogBg      = RGB(241, 247, 249);

struct AppState {
    int dpi = 96;
    HWND hwnd = nullptr;
    HWND hTooltip = nullptr;
    HWND hHdrTitle = nullptr;
    HWND hList = nullptr, hBanner = nullptr;
    HWND hChkSortTime = nullptr, hChkRecursive = nullptr;
    HWND hRadPol[4] = {};
    HWND hRadPre[4] = {};
    HWND hRadMot[3] = {};
    HWND hCmbBackend = nullptr;

    HWND hOutDir = nullptr;
    HWND hChkRaw = nullptr, hCmbRawFmt = nullptr;
    HWND hChkRgb = nullptr, hCmbRgbFmt = nullptr, hEdtJxsBpp = nullptr;
    HWND hRadCsSrgb = nullptr, hRadCsAdobe = nullptr, hChkField = nullptr, hCmbFieldMode = nullptr;

    HWND hEdtCliPath = nullptr, hEdtLibRawPath = nullptr;

    HWND hEdtDenoise = nullptr, hEdtChroma = nullptr, hEdtPsfIters = nullptr, hEdtRgbPsf = nullptr;
    HWND hEdtPsfSigma = nullptr, hEdtDeghost = nullptr, hEdtEv = nullptr, hEdtGrpSz = nullptr, hEdtThreads = nullptr;
    HWND hChkAutoExp = nullptr, hChkRolloff = nullptr, hChkEdgePol = nullptr, hChkDither = nullptr;

    HWND hBtnRun = nullptr, hBtnInspect = nullptr, hBtnCancel = nullptr;
    HWND hPctLbl = nullptr, hLog = nullptr;
    int prog_pct = 0;
    int custom_anchor_1based = 0;
    bool is_screenshot_mode = false;

    HFONT hFont = nullptr, hFontBold = nullptr, hFontCardHdr = nullptr;
    HFONT hFontTitle = nullptr, hFontBadge = nullptr, hFontMono = nullptr;
    HBRUSH hBrCanvas = nullptr, hBrCard = nullptr, hBrLog = nullptr, hBrWhite = nullptr;
    HBITMAP hBmpGx = nullptr, hBmpNijin = nullptr;
    COLORREF bannerColor = kColTextMuted;

    std::vector<std::wstring> inputs;
    std::atomic<bool> running{false};
    std::mutex proc_mtx;
    HANDLE hChildProc = nullptr;
    std::thread worker;

    int S(int px) const { return MulDiv(px, dpi, 96); }
};

static AppState g_app;

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

static std::wstring reg_read_str(const wchar_t* name) {
    HKEY hk = nullptr;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, kRegSubKey, 0, KEY_READ, &hk) != ERROR_SUCCESS) return L"";
    wchar_t buf[2048] = {0};
    DWORD cb = sizeof(buf) - sizeof(wchar_t);
    DWORD type = 0;
    LONG rc = RegQueryValueExW(hk, name, nullptr, &type, reinterpret_cast<LPBYTE>(buf), &cb);
    RegCloseKey(hk);
    return (rc == ERROR_SUCCESS && type == REG_SZ) ? std::wstring(buf) : L"";
}

static void reg_write_str(const wchar_t* name, const std::wstring& val) {
    HKEY hk = nullptr;
    if (RegCreateKeyExW(HKEY_CURRENT_USER, kRegSubKey, 0, nullptr, 0, KEY_WRITE, nullptr, &hk, nullptr) != ERROR_SUCCESS) return;
    if (val.empty()) RegDeleteValueW(hk, name);
    else RegSetValueExW(hk, name, 0, REG_SZ, reinterpret_cast<const BYTE*>(val.c_str()),
                        (DWORD)((val.size() + 1) * sizeof(wchar_t)));
    RegCloseKey(hk);
}

static bool file_exists_regular(const std::wstring& p) {
    if (p.empty()) return false;
    DWORD a = GetFileAttributesW(p.c_str());
    return (a != INVALID_FILE_ATTRIBUTES) && !(a & FILE_ATTRIBUTE_DIRECTORY);
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

static std::wstring parent_dir_of(const std::wstring& p) {
    DWORD attr = GetFileAttributesW(p.c_str());
    if (attr != INVALID_FILE_ATTRIBUTES && (attr & FILE_ATTRIBUTE_DIRECTORY)) return p;
    size_t pos = p.find_last_of(L"/\\");
    return (pos == std::wstring::npos) ? L"" : p.substr(0, pos);
}

static std::wstring get_wnd_text(HWND h) {
    int len = GetWindowTextLengthW(h);
    if (len <= 0) return L"";
    std::wstring s(len, L'\0');
    GetWindowTextW(h, &s[0], len + 1);
    return s;
}

static std::wstring auto_detect_cli_exe(bool include_saved) {
    if (include_saved) {
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

static void append_log_ui(const std::wstring& line) {
    std::wstring msg = line + L"\r\n";
    int len = GetWindowTextLengthW(g_app.hLog);
    if (len > 28000) { SetWindowTextW(g_app.hLog, L""); len = 0; }
    SendMessageW(g_app.hLog, EM_SETSEL, (WPARAM)len, (LPARAM)len);
    SendMessageW(g_app.hLog, EM_REPLACESEL, FALSE, (LPARAM)msg.c_str());
}

static std::wstring prompt_user_for_cli_exe(HWND owner) {
    int r = MessageBoxW(
        owner,
        L"未能自动找到 nijinLBS.exe 命令行引擎，请手动选择可执行文件路径。",
        L"指定 CLI 路径",
        MB_OKCANCEL | MB_ICONINFORMATION
    );
    if (r != IDOK) return L"";
    std::wstring chosen = pick_single_file_dialog(
        owner,
        L"选择 nijinLBS.exe",
        L"CLI Engine (nijinLBS.exe;*.exe)\0nijinLBS.exe;*.exe\0All Files (*.*)\0*.*\0"
    );
    if (file_exists_regular(chosen)) {
        SetWindowTextW(g_app.hEdtCliPath, chosen.c_str());
        reg_write_str(L"CliPath", chosen);
        append_log_ui(L"[CLI] " + chosen);
        return chosen;
    }
    return L"";
}

static std::wstring resolve_active_cli_or_prompt(HWND owner) {
    std::wstring cur = trim_ws(get_wnd_text(g_app.hEdtCliPath));
    if (file_exists_regular(cur)) {
        reg_write_str(L"CliPath", cur);
        return cur;
    }
    std::wstring det = auto_detect_cli_exe(true);
    if (!det.empty()) {
        SetWindowTextW(g_app.hEdtCliPath, det.c_str());
        return det;
    }
    return prompt_user_for_cli_exe(owner);
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

static void post_log(const std::wstring& s) {
    auto* hs = new std::wstring(s);
    if (!PostMessageW(g_app.hwnd, WM_APP_LOG, 0, (LPARAM)hs)) delete hs;
}

struct DotData {
    bool hovered = false;
    std::wstring tip;
};

static inline Gdiplus::Color gdip_col(COLORREF c, BYTE alpha = 255) {
    return Gdiplus::Color(alpha, GetRValue(c), GetGValue(c), GetBValue(c));
}

static void gdip_round_rect(Gdiplus::Graphics& g, float x, float y, float w, float h, float r,
                            Gdiplus::Color fill, Gdiplus::Color stroke, float stroke_w = 1.0f) {
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
        g.DrawPath(&pen, &path);
    }
}

static void paint_hover_dot(HWND hw, HDC hdc, bool hovered) {
    RECT rc{};
    GetClientRect(hw, &rc);
    FillRect(hdc, &rc, g_app.hBrCard);

    {
        Gdiplus::Graphics g(hdc);
        g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
        Gdiplus::SolidBrush fill(gdip_col(hovered ? kColAccent : kColBadgeFill));
        Gdiplus::Pen pen(gdip_col(hovered ? kColAccent : kColBadgeEdge), 1.0f);
        g.FillEllipse(&fill, 1.0f, 1.0f, (float)(rc.right - 3), (float)(rc.bottom - 3));
        g.DrawEllipse(&pen, 1.0f, 1.0f, (float)(rc.right - 3), (float)(rc.bottom - 3));
    }

    SetBkMode(hdc, TRANSPARENT);
    SetTextColor(hdc, hovered ? RGB(255, 255, 255) : kColBadgeText);
    HGDIOBJ of = SelectObject(hdc, g_app.hFontBadge);
    DrawTextW(hdc, L"i", 1, &rc, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    SelectObject(hdc, of);
}

// Modern greyish-aqua antialiased vector Checkbox, Radio, Pill-Button hover & Dropdown subclasses
static void paint_modern_toggle(HWND hw, HDC hdc, bool hovered, bool is_radio, bool is_bold) {
    RECT rc{};
    GetClientRect(hw, &rc);
    FillRect(hdc, &rc, g_app.hBrCard);

    bool checked = (SendMessageW(hw, BM_GETCHECK, 0, 0) == BST_CHECKED);
    bool enabled = IsWindowEnabled(hw) != FALSE;

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

    wchar_t txt[160] = {0};
    GetWindowTextW(hw, txt, 159);
    SetBkMode(hdc, TRANSPARENT);
    SetTextColor(hdc, enabled ? kColTextMain : kColTextMuted);
    HGDIOBJ of = SelectObject(hdc, is_bold ? g_app.hFontBold : g_app.hFont);
    RECT tr = rc;
    tr.left += (int)box + g_app.S(7);
    DrawTextW(hdc, txt, -1, &tr, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
    SelectObject(hdc, of);
}

struct ToggleSubData {
    bool hovered = false;
    bool is_radio = false;
    bool is_bold = false;
};

static LRESULT CALLBACK ModernToggleSubclassProc(HWND hw, UINT msg, WPARAM wParam, LPARAM lParam, UINT_PTR uId, DWORD_PTR dwRef) {
    (void)uId;
    auto* td = reinterpret_cast<ToggleSubData*>(dwRef);
    switch (msg) {
        case WM_ERASEBKGND: return 1;
        case WM_MOUSEMOVE:
            if (td && !td->hovered) {
                td->hovered = true;
                TRACKMOUSEEVENT tme{sizeof(tme), TME_LEAVE, hw, 0};
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
        case WM_LBUTTONUP:
        case WM_KEYUP: {
            LRESULT r = DefSubclassProc(hw, msg, wParam, lParam);
            HWND hp = GetParent(hw);
            if (hp) {
                HWND hc = GetWindow(hp, GW_CHILD);
                while (hc) {
                    wchar_t cls[32] = {0};
                    GetClassNameW(hc, cls, 31);
                    if (_wcsicmp(cls, L"Button") == 0) InvalidateRect(hc, nullptr, FALSE);
                    hc = GetWindow(hc, GW_HWNDNEXT);
                }
            }
            return r;
        }
        case WM_PAINT: {
            PAINTSTRUCT ps{};
            HDC hdc = BeginPaint(hw, &ps);
            paint_modern_toggle(hw, hdc, td && td->hovered, td && td->is_radio, td && td->is_bold);
            EndPaint(hw, &ps);
            return 0;
        }
        case WM_PRINT:
        case WM_PRINTCLIENT: {
            paint_modern_toggle(hw, (HDC)wParam, td && td->hovered, td && td->is_radio, td && td->is_bold);
            return 0;
        }
        case WM_NCDESTROY:
            RemoveWindowSubclass(hw, ModernToggleSubclassProc, uId);
            delete td;
            break;
    }
    return DefSubclassProc(hw, msg, wParam, lParam);
}

static void attach_modern_toggle(HWND hw, bool is_radio, bool is_bold = false) {
    SetWindowSubclass(hw, ModernToggleSubclassProc, 1, reinterpret_cast<DWORD_PTR>(new ToggleSubData{false, is_radio, is_bold}));
}

static LRESULT CALLBACK ModernBtnHoverSubclassProc(HWND hw, UINT msg, WPARAM wParam, LPARAM lParam, UINT_PTR uId, DWORD_PTR dwRef) {
    (void)dwRef;
    switch (msg) {
        case WM_MOUSEMOVE:
            if (GetPropW(hw, L"NijiHover") == nullptr) {
                SetPropW(hw, L"NijiHover", (HANDLE)1);
                TRACKMOUSEEVENT tme{sizeof(tme), TME_LEAVE, hw, 0};
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
        COLORREF fillCol = !enabled ? RGB(239, 244, 246) : (hovered ? RGB(246, 250, 252) : RGB(255, 255, 255));
        COLORREF edgeCol = hovered ? kColAccent : kColInputEdge;
        gdip_round_rect(g, 0.5f, 0.5f, (float)(rc.right - 1) - 0.5f, (float)(rc.bottom - 1) - 0.5f,
                        (float)g_app.S(6), gdip_col(fillCol), gdip_col(edgeCol), 1.15f);

        // Crisp right-side vector chevron
        float cx = (float)rc.right - (float)g_app.S(13);
        float cy = 0.5f * (float)rc.bottom;
        float cw = (float)g_app.S(4);
        Gdiplus::Pen chevPen(gdip_col(hovered ? kColAccentDark : kColBadgeText), 1.6f);
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
        DrawTextW(hdc, buf, -1, &tr, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
        SelectObject(hdc, of);
    }
}

static LRESULT CALLBACK ModernComboSubclassProc(HWND hw, UINT msg, WPARAM wParam, LPARAM lParam, UINT_PTR uId, DWORD_PTR dwRef) {
    (void)dwRef;
    switch (msg) {
        case WM_ERASEBKGND: return 1;
        case WM_MOUSEMOVE:
            if (GetPropW(hw, L"NijiHover") == nullptr) {
                SetPropW(hw, L"NijiHover", (HANDLE)1);
                TRACKMOUSEEVENT tme{sizeof(tme), TME_LEAVE, hw, 0};
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
        case CB_SETCURSEL: {
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
            paint_modern_combo(hw, (HDC)wParam, GetPropW(hw, L"NijiHover") != nullptr);
            return 0;
        }
        case WM_NCDESTROY:
            RemovePropW(hw, L"NijiHover");
            RemoveWindowSubclass(hw, ModernComboSubclassProc, uId);
            break;
    }
    return DefSubclassProc(hw, msg, wParam, lParam);
}

static void draw_modern_pill_button(const DRAWITEMSTRUCT* dis, bool is_primary) {
    FillRect(dis->hDC, &dis->rcItem, g_app.hBrCard);
    bool disabled = (dis->itemState & ODS_DISABLED) != 0;
    bool pushed   = (dis->itemState & ODS_SELECTED) != 0;
    bool hovered  = GetPropW(dis->hwndItem, L"NijiHover") != nullptr;

    {
        Gdiplus::Graphics g(dis->hDC);
        g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
        float w = (float)(dis->rcItem.right - dis->rcItem.left) - 1.5f;
        float h = (float)(dis->rcItem.bottom - dis->rcItem.top) - 1.5f;
        if (is_primary) {
            COLORREF bg = disabled ? RGB(172, 190, 198)
                        : pushed   ? RGB(50, 90, 106)
                        : hovered  ? RGB(68, 115, 134) : kColAccent;
            COLORREF bd = disabled ? RGB(158, 178, 186) : kColAccentDark;
            gdip_round_rect(g, 0.75f, 0.75f, w, h, (float)g_app.S(7), gdip_col(bg), gdip_col(bd), 1.1f);
        } else {
            COLORREF bg = disabled ? RGB(238, 243, 245)
                        : pushed   ? RGB(213, 229, 236)
                        : hovered  ? RGB(228, 239, 244) : RGB(242, 247, 250);
            COLORREF bd = disabled ? RGB(208, 219, 224)
                        : (hovered || pushed) ? kColAccent : RGB(182, 203, 212);
            gdip_round_rect(g, 0.75f, 0.75f, w, h, (float)g_app.S(6), gdip_col(bg), gdip_col(bd), 1.1f);
        }
    }

    wchar_t txt[128] = {0};
    GetWindowTextW(dis->hwndItem, txt, 127);
    SetBkMode(dis->hDC, TRANSPARENT);
    SetTextColor(dis->hDC, is_primary ? RGB(255, 255, 255) : (disabled ? kColTextMuted : kColTextMain));
    HGDIOBJ of = SelectObject(dis->hDC, is_primary ? g_app.hFontBold : g_app.hFont);
    RECT tr = dis->rcItem;
    DrawTextW(dis->hDC, txt, -1, &tr, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    SelectObject(dis->hDC, of);
}

static LRESULT CALLBACK HoverDotWndProc(HWND hw, UINT msg, WPARAM wParam, LPARAM lParam) {
    auto* dd = reinterpret_cast<DotData*>(GetWindowLongPtrW(hw, GWLP_USERDATA));
    switch (msg) {
        case WM_MOUSEMOVE: {
            if (dd && !dd->hovered) {
                dd->hovered = true;
                TRACKMOUSEEVENT tme{sizeof(tme), TME_LEAVE, hw, 0};
                TrackMouseEvent(&tme);
                InvalidateRect(hw, nullptr, FALSE);
            }
            break;
        }
        case WM_MOUSELEAVE: {
            if (dd && dd->hovered) {
                dd->hovered = false;
                InvalidateRect(hw, nullptr, FALSE);
            }
            break;
        }
        case WM_LBUTTONDOWN: {
            if (dd && !dd->tip.empty()) {
                MessageBoxW(g_app.hwnd, dd->tip.c_str(), L"说明", MB_OK | MB_ICONINFORMATION);
            }
            return 0;
        }
        case WM_PAINT: {
            PAINTSTRUCT ps{};
            HDC hdc = BeginPaint(hw, &ps);
            paint_hover_dot(hw, hdc, dd && dd->hovered);
            EndPaint(hw, &ps);
            return 0;
        }
        case WM_PRINTCLIENT: {
            paint_hover_dot(hw, (HDC)wParam, dd && dd->hovered);
            return 0;
        }
        case WM_NCDESTROY: {
            delete dd;
            SetWindowLongPtrW(hw, GWLP_USERDATA, 0);
            break;
        }
    }
    return DefWindowProcW(hw, msg, wParam, lParam);
}

static HWND create_hover_dot(HWND parent, int x, int y, const std::wstring& tip_text) {
    HWND hw = CreateWindowExW(
        0, L"NijiLbsHoverDot", L"", WS_CHILD | WS_VISIBLE,
        g_app.S(x), g_app.S(y), g_app.S(18), g_app.S(18),
        parent, nullptr, GetModuleHandleW(nullptr), nullptr
    );
    auto* dd = new DotData{false, tip_text};
    SetWindowLongPtrW(hw, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(dd));

    if (g_app.hTooltip) {
        TTTOOLINFOW ti{};
        ti.cbSize = sizeof(ti);
        ti.uFlags = TTF_IDISHWND | TTF_SUBCLASS;
        ti.hwnd = parent;
        ti.uId = reinterpret_cast<UINT_PTR>(hw);
        ti.lpszText = const_cast<LPWSTR>(dd->tip.c_str());
        SendMessageW(g_app.hTooltip, TTM_ADDTOOLW, 0, reinterpret_cast<LPARAM>(&ti));
    }
    return hw;
}

static void apply_preset_to_controls(int idx) {
    for (int i = 0; i < 4; ++i) {
        if (g_app.hRadPre[i]) SendMessageW(g_app.hRadPre[i], BM_SETCHECK, (i == idx) ? BST_CHECKED : BST_UNCHECKED, 0);
    }
    if (idx == 1) { // Sci 科学
        SetWindowTextW(g_app.hEdtDenoise, L"0.40");
        SetWindowTextW(g_app.hEdtChroma, L"0.55");
        SetWindowTextW(g_app.hEdtPsfIters, L"0");
        SetWindowTextW(g_app.hEdtRgbPsf, L"0");
        SetWindowTextW(g_app.hEdtPsfSigma, L"1.05");
        SetWindowTextW(g_app.hEdtDeghost, L"1.35");
        SendMessageW(g_app.hChkAutoExp, BM_SETCHECK, BST_UNCHECKED, 0);
        SendMessageW(g_app.hChkRolloff, BM_SETCHECK, BST_UNCHECKED, 0);
        SendMessageW(g_app.hChkEdgePol, BM_SETCHECK, BST_UNCHECKED, 0);
        SendMessageW(g_app.hChkDither,  BM_SETCHECK, BST_CHECKED, 0);
    } else if (idx == 2) { // Arc 归档
        SetWindowTextW(g_app.hEdtDenoise, L"0.70");
        SetWindowTextW(g_app.hEdtChroma, L"0.85");
        SetWindowTextW(g_app.hEdtPsfIters, L"0");
        SetWindowTextW(g_app.hEdtRgbPsf, L"0");
        SetWindowTextW(g_app.hEdtPsfSigma, L"1.05");
        SetWindowTextW(g_app.hEdtDeghost, L"1.10");
        SendMessageW(g_app.hChkAutoExp, BM_SETCHECK, BST_CHECKED, 0);
        SendMessageW(g_app.hChkRolloff, BM_SETCHECK, BST_CHECKED, 0);
        SendMessageW(g_app.hChkEdgePol, BM_SETCHECK, BST_UNCHECKED, 0);
        SendMessageW(g_app.hChkDither,  BM_SETCHECK, BST_CHECKED, 0);
    } else if (idx == 3) { // Astro 星空
        SetWindowTextW(g_app.hEdtDenoise, L"0.85");
        SetWindowTextW(g_app.hEdtChroma, L"0.90");
        SetWindowTextW(g_app.hEdtPsfIters, L"3");
        SetWindowTextW(g_app.hEdtRgbPsf, L"1");
        SetWindowTextW(g_app.hEdtPsfSigma, L"0.95");
        SetWindowTextW(g_app.hEdtDeghost, L"0.85");
        SendMessageW(g_app.hChkAutoExp, BM_SETCHECK, BST_CHECKED, 0);
        SendMessageW(g_app.hChkRolloff, BM_SETCHECK, BST_CHECKED, 0);
        SendMessageW(g_app.hChkEdgePol, BM_SETCHECK, BST_UNCHECKED, 0);
        SendMessageW(g_app.hChkDither,  BM_SETCHECK, BST_CHECKED, 0);
    } else { // Photo 照片
        SetWindowTextW(g_app.hEdtDenoise, L"1.00");
        SetWindowTextW(g_app.hEdtChroma, L"1.00");
        SetWindowTextW(g_app.hEdtPsfIters, L"4");
        SetWindowTextW(g_app.hEdtRgbPsf, L"2");
        SetWindowTextW(g_app.hEdtPsfSigma, L"1.05");
        SetWindowTextW(g_app.hEdtDeghost, L"1.00");
        SendMessageW(g_app.hChkAutoExp, BM_SETCHECK, BST_CHECKED, 0);
        SendMessageW(g_app.hChkRolloff, BM_SETCHECK, BST_CHECKED, 0);
        SendMessageW(g_app.hChkEdgePol, BM_SETCHECK, BST_CHECKED, 0);
        SendMessageW(g_app.hChkDither,  BM_SETCHECK, BST_CHECKED, 0);
    }
}

static void update_banner() {
    int n = (int)g_app.inputs.size();
    bool has_dir = false;
    for (const auto& p : g_app.inputs) {
        DWORD a = GetFileAttributesW(p.c_str());
        if (a != INVALID_FILE_ATTRIBUTES && (a & FILE_ATTRIBUTE_DIRECTORY)) has_dir = true;
    }
    std::wstring txt;
    if (n == 0) {
        txt = L"未导入（支持拖放 RAW 或目录）";
        g_app.bannerColor = kColTextMuted;
    } else if (has_dir) {
        txt = std::to_wstring(n) + L" 项（含目录扫描）";
        g_app.bannerColor = kColBadgeText;
    } else if (n < 4) {
        txt = std::to_wstring(n) + L" 张（低于 4 张门槛）";
        g_app.bannerColor = RGB(188, 102, 28);
    } else {
        txt = L"已选 " + std::to_wstring(n) + L" 张";
        g_app.bannerColor = kColBadgeText;
    }
    SetWindowTextW(g_app.hBanner, txt.c_str());
    InvalidateRect(g_app.hBanner, nullptr, TRUE);
}

static void sync_list_ui() {
    SendMessageW(g_app.hList, LB_RESETCONTENT, 0, 0);
    for (size_t i = 0; i < g_app.inputs.size(); ++i) {
        DWORD a = GetFileAttributesW(g_app.inputs[i].c_str());
        bool is_d = (a != INVALID_FILE_ATTRIBUTES && (a & FILE_ATTRIBUTE_DIRECTORY));
        std::wstring item = std::wstring(is_d ? L"[DIR] " : L"[RAW] ") + g_app.inputs[i];
        SendMessageW(g_app.hList, LB_ADDSTRING, 0, (LPARAM)item.c_str());
    }
    if (!g_app.inputs.empty() && get_wnd_text(g_app.hOutDir).empty()) {
        SetWindowTextW(g_app.hOutDir, (parent_dir_of(g_app.inputs[0]) + L"\\nijinLBS_out").c_str());
    }
    update_banner();
}

static void add_unique_input(const std::wstring& p) {
    if (p.empty()) return;
    if (std::find(g_app.inputs.begin(), g_app.inputs.end(), p) == g_app.inputs.end()) {
        g_app.inputs.push_back(p);
    }
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

static void pick_raw_files_dialog(HWND owner) {
    std::vector<wchar_t> buf(65536, L'\0');
    OPENFILENAMEW ofn{};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = owner;
    ofn.lpstrFilter = L"Camera RAW Files (*.NEF;*.NRW;*.DNG;*.ARW;*.CR2;*.CR3;*.RAF;*.ORF;*.RW2)\0"
                      L"*.NEF;*.NRW;*.DNG;*.ARW;*.CR2;*.CR3;*.RAF;*.ORF;*.RW2;*.PEF;*.IIQ\0All Files (*.*)\0*.*\0";
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
    sync_list_ui();
}

static std::string extract_json_str(const std::string& s, const char* key, size_t start_pos = 0) {
    std::string pat = std::string("\"") + key + "\":\"";
    size_t p = s.find(pat, start_pos);
    if (p == std::string::npos) return "";
    p += pat.size();
    std::string out;
    while (p < s.size() && s[p] != '"') {
        if (s[p] == '\\' && p + 1 < s.size()) ++p;
        out.push_back(s[p++]);
    }
    return out;
}

static int parse_json_int(const std::string& s, const char* key, int fallback, size_t pos = 0) {
    std::string pat = std::string("\"") + key + "\":";
    size_t p = s.find(pat, pos);
    if (p == std::string::npos) return fallback;
    p += pat.size();
    while (p < s.size() && s[p] == ' ') ++p;
    return std::atoi(s.c_str() + p);
}

static double parse_json_double(const std::string& s, const char* key, double fallback, size_t pos = 0) {
    std::string pat = std::string("\"") + key + "\":";
    size_t p = s.find(pat, pos);
    if (p == std::string::npos) return fallback;
    p += pat.size();
    while (p < s.size() && s[p] == ' ') ++p;
    return std::atof(s.c_str() + p);
}

static std::wstring scene_code_to_cn(const std::string& sc) {
    if (sc == "astro_landscape")   return L"手持星野（星空 + 静态地景双域）";
    if (sc == "astro_sky")         return L"手持纯星空 / 深空星场";
    if (sc == "aeb_bracket")       return L"包围曝光混拍（AEB 跨度预警）";
    if (sc == "tripod_static")     return L"三脚架稳态（低位移高信噪比）";
    if (sc == "action_subject")    return L"静态背景 + 局部运动主体";
    if (sc == "lowlight_high_iso") return L"高感弱光手持连拍";
    return L"标准手持亚像素超分";
}

struct WizardModalState {
    int result = 0; // 0=close/cancel, 1=apply & run, 2=keep & run, 3=apply only
    int sel_idx = 0;
    HWND hCmb = nullptr;
    std::wstring line1, line2, line3;
    std::vector<std::wstring> frame_titles;
    int init_sel = 0;
};

static LRESULT CALLBACK WizardModalWndProc(HWND hw, UINT msg, WPARAM wParam, LPARAM lParam) {
    auto* st = reinterpret_cast<WizardModalState*>(GetWindowLongPtrW(hw, GWLP_USERDATA));
    switch (msg) {
        case WM_ERASEBKGND: return 1;
        case WM_PAINT: {
            PAINTSTRUCT ps{};
            HDC hdc = BeginPaint(hw, &ps);
            RECT crc{};
            GetClientRect(hw, &crc);
            FillRect(hdc, &crc, g_app.hBrCanvas);
            {
                Gdiplus::Graphics g(hdc);
                g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
                gdip_round_rect(g, (float)g_app.S(14), (float)g_app.S(14),
                                (float)(crc.right - g_app.S(28)), (float)(crc.bottom - g_app.S(28)),
                                (float)g_app.S(10), gdip_col(kColCard), gdip_col(kColCardBorder), 1.15f);
            }
            if (st) {
                SetBkMode(hdc, TRANSPARENT);
                HGDIOBJ of = SelectObject(hdc, g_app.hFontCardHdr);
                SetTextColor(hdc, kColAccentDark);
                RECT r1{g_app.S(30), g_app.S(28), crc.right - g_app.S(30), g_app.S(52)};
                DrawTextW(hdc, st->line1.c_str(), -1, &r1, DT_LEFT | DT_VCENTER | DT_SINGLELINE);

                SelectObject(hdc, g_app.hFont);
                SetTextColor(hdc, kColTextMuted);
                RECT r2{g_app.S(30), g_app.S(56), crc.right - g_app.S(30), g_app.S(78)};
                DrawTextW(hdc, st->line2.c_str(), -1, &r2, DT_LEFT | DT_VCENTER | DT_SINGLELINE);

                SetTextColor(hdc, kColTextMain);
                RECT r3{g_app.S(30), g_app.S(86), crc.right - g_app.S(30), g_app.S(154)};
                DrawTextW(hdc, st->line3.c_str(), -1, &r3, DT_LEFT | DT_WORDBREAK);

                SelectObject(hdc, g_app.hFontBold);
                RECT r4{g_app.S(30), g_app.S(168), g_app.S(152), g_app.S(194)};
                DrawTextW(hdc, L"运动/地景定格帧：", -1, &r4, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
                SelectObject(hdc, of);
            }
            EndPaint(hw, &ps);
            return 0;
        }
        case WM_DRAWITEM: {
            auto* dis = reinterpret_cast<const DRAWITEMSTRUCT*>(lParam);
            if (dis && dis->CtlType == ODT_BUTTON) {
                draw_modern_pill_button(dis, dis->CtlID == 1001);
                return TRUE;
            }
            break;
        }
        case WM_COMMAND: {
            int id = LOWORD(wParam);
            if (st && (id == 1001 || id == 1002 || id == 1003)) {
                if (st->hCmb) st->sel_idx = (int)SendMessageW(st->hCmb, CB_GETCURSEL, 0, 0);
                st->result = (id == 1001) ? 1 : (id == 1002 ? 3 : 2);
                DestroyWindow(hw);
                return 0;
            }
            if (st && id == 1010 && HIWORD(wParam) == CBN_SELCHANGE) {
                InvalidateRect(st->hCmb, nullptr, FALSE);
            }
            break;
        }
        case WM_CLOSE:
            if (st) st->result = 0;
            DestroyWindow(hw);
            return 0;
    }
    return DefWindowProcW(hw, msg, wParam, lParam);
}

// Returns: 0 = cancel/close, 1 = apply prescription & run, 2 = keep current & run, 3 = applied to UI only
static int run_smart_wizard_modal(const std::vector<std::wstring>& base_args, bool always_show) {
    std::wstring cmd;
    for (size_t i = 0; i < base_args.size(); ++i) {
        if (i > 0) cmd.push_back(L' ');
        cmd += quote_win_arg(base_args[i]);
    }
    cmd += L" --plan-json";

    SECURITY_ATTRIBUTES sa{sizeof(SECURITY_ATTRIBUTES), nullptr, TRUE};
    HANDLE hR = nullptr, hW = nullptr;
    if (!CreatePipe(&hR, &hW, &sa, 0)) return 2;
    SetHandleInformation(hR, HANDLE_FLAG_INHERIT, 0);

    STARTUPINFOW si{};
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESTDHANDLES | STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_HIDE;
    si.hStdOutput = hW;
    si.hStdError = GetStdHandle(STD_ERROR_HANDLE);
    PROCESS_INFORMATION pi{};
    std::vector<wchar_t> mcmd(cmd.begin(), cmd.end()); mcmd.push_back(L'\0');
    if (!CreateProcessW(nullptr, mcmd.data(), nullptr, nullptr, TRUE, CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi)) {
        CloseHandle(hW); CloseHandle(hR);
        return 2;
    }
    CloseHandle(hW);

    std::string out;
    char buf[1024];
    DWORD nr = 0;
    while (ReadFile(hR, buf, sizeof(buf), &nr, nullptr) && nr > 0) out.append(buf, nr);
    CloseHandle(hR);
    WaitForSingleObject(pi.hProcess, INFINITE);
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);

    if (out.find("\"clusters\":[") == std::string::npos) return 2;
    bool needs_prompt = (out.find("\"needs_prompt\":true") != std::string::npos);
    if (!always_show && !needs_prompt) return 2;

    std::string sc     = extract_json_str(out, "scene");
    std::string rPre   = extract_json_str(out, "rec_preset");
    std::string rAnc   = extract_json_str(out, "rec_anchor");
    std::string rxSum  = extract_json_str(out, "rx_summary");
    int bestAnc        = parse_json_int(out, "best_anchor", 1);
    int quadCov        = parse_json_int(out, "quad_coverage", 1);
    int srScore        = parse_json_int(out, "sr_score", 80);
    int starCnt        = parse_json_int(out, "star_count", 0);
    double movPct      = parse_json_double(out, "motion_pct", 0.0);
    double rDen        = parse_json_double(out, "rec_denoise", 1.0);
    double rDegh       = parse_json_double(out, "rec_deghost", 1.0);
    int rPsf           = parse_json_int(out, "rec_psf_iters", 4);
    double estMb       = parse_json_double(out, "est_dng_mb", 130.0);
    bool shock1        = (out.find("\"shutter_shock\":true") != std::string::npos);

    WizardModalState st;
    wchar_t wbuf[512];
    std::swprintf(wbuf, 512, L"识别场景：%ls   |   超分潜力：%d/100（%d/4 象限）",
                  scene_code_to_cn(sc).c_str(), srScore, quadCov);
    st.line1 = wbuf;
    std::swprintf(wbuf, 512, L"动体面积：%.1f%%   |   检出星点：%d   |   预计 2× 无损 DNG：~%.0f MiB%ls",
                  movPct, starCnt, estMb, shock1 ? L"   |   首帧微震预警" : L"");
    st.line2 = wbuf;
    const wchar_t* preCn = (rPre == "astro") ? L"Astro 星空"
                         : (rPre == "sci")   ? L"Sci 科学"
                         : (rPre == "arc")   ? L"Arc 归档" : L"Photo 照片";
    std::swprintf(wbuf, 512, L"推荐处方：预设【%ls】 · 亮度降噪 %.2f · 去鬼影 %.2f · RAW RL %d 次\r\n诊断依据：%ls",
                  preCn, rDen, rDegh, rPsf, utf8_to_wide(rxSum).c_str());
    st.line3 = wbuf;

    std::vector<int> frame_codes = {0, -1, -2};
    std::swprintf(wbuf, 512, L"自动最锐（皇冠帧 #%d）", bestAnc);
    st.frame_titles = {wbuf, L"首帧定格（第 #1 张）", L"中间定格（序列居中帧）"};

    size_t fPos = out.find("\"frames\":[");
    size_t fEnd = (fPos != std::string::npos) ? out.find("]", fPos) : std::string::npos;
    if (fPos != std::string::npos && fEnd != std::string::npos) {
        size_t cur = fPos;
        while ((cur = out.find("{\"idx\":", cur)) != std::string::npos && cur < fEnd) {
            int fIdx = parse_json_int(out, "idx", 1, cur);
            std::string fFile  = extract_json_str(out, "file", cur);
            std::string fFlags = extract_json_str(out, "flags", cur);
            double fAcu = parse_json_double(out, "acutance", 0.0, cur);
            double fSh  = parse_json_double(out, "shift", 0.0, cur);
            size_t nb = out.find("}", cur);
            bool isCrown = (nb != std::string::npos && out.substr(cur, nb - cur).find("\"crown\":true") != std::string::npos);
            std::swprintf(wbuf, 512, L"指定第 #%d 张：%ls（锐度 %.1f | 位移 %.2fpx%ls%ls）",
                fIdx, utf8_to_wide(fFile).c_str(), fAcu, fSh,
                isCrown ? L" ★最锐" : L"",
                (fFlags != "OK") ? (L" [" + utf8_to_wide(fFlags) + L"]").c_str() : L"");
            st.frame_titles.push_back(wbuf);
            frame_codes.push_back(fIdx);
            cur += 7;
        }
    }

    if (rAnc == "middle") st.init_sel = 2;
    else if (rAnc == "first") st.init_sel = 1;
    else if (std::atoi(rAnc.c_str()) >= 1) {
        int want = std::atoi(rAnc.c_str());
        for (size_t k = 3; k < frame_codes.size(); ++k) {
            if (frame_codes[k] == want) { st.init_sel = (int)k; break; }
        }
    }

    static bool reg_done = false;
    HINSTANCE hInst = GetModuleHandleW(nullptr);
    if (!reg_done) {
        WNDCLASSEXW wc{};
        wc.cbSize = sizeof(wc);
        wc.lpfnWndProc = WizardModalWndProc;
        wc.hInstance = hInst;
        wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        wc.lpszClassName = L"NijiLbsWizardModal";
        RegisterClassExW(&wc);
        reg_done = true;
    }

    RECT mr{0, 0, g_app.S(540), g_app.S(264)};
    DWORD mstyle = WS_POPUP | WS_CAPTION | WS_SYSMENU;
    AdjustWindowRect(&mr, mstyle, FALSE);
    RECT pr{};
    GetWindowRect(g_app.hwnd, &pr);
    int mw = mr.right - mr.left, mh = mr.bottom - mr.top;
    int mx = pr.left + ((pr.right - pr.left) - mw) / 2;
    int my = pr.top  + ((pr.bottom - pr.top) - mh) / 2;

    HWND hDlg = CreateWindowExW(WS_EX_DLGMODALFRAME, L"NijiLbsWizardModal", L"智能诊断与方案向导",
                                mstyle, mx, my, mw, mh, g_app.hwnd, nullptr, hInst, nullptr);
    SetWindowLongPtrW(hDlg, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(&st));

    st.hCmb = CreateWindowExW(0, L"COMBOBOX", L"", WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST | WS_VSCROLL,
                              g_app.S(152), g_app.S(168), g_app.S(356), g_app.S(220), hDlg, (HMENU)1010, hInst, nullptr);
    SendMessageW(st.hCmb, WM_SETFONT, (WPARAM)g_app.hFont, TRUE);
    for (const auto& ft : st.frame_titles) SendMessageW(st.hCmb, CB_ADDSTRING, 0, (LPARAM)ft.c_str());
    SendMessageW(st.hCmb, CB_SETCURSEL, (WPARAM)st.init_sel, 0);
    SetWindowSubclass(st.hCmb, ModernComboSubclassProc, 2, 0);

    auto mk_b = [&](const wchar_t* t, int x, int w, int id) {
        HWND b = CreateWindowExW(0, L"BUTTON", t, WS_CHILD | WS_VISIBLE | BS_OWNERDRAW,
                                 g_app.S(x), g_app.S(210), g_app.S(w), g_app.S(30), hDlg, (HMENU)(INT_PTR)id, hInst, nullptr);
        SendMessageW(b, WM_SETFONT, (WPARAM)g_app.hFont, TRUE);
        SetWindowSubclass(b, ModernBtnHoverSubclassProc, 3, 0);
        return b;
    };
    mk_b(L"采纳方案并开始合成", 30,  166, 1001);
    mk_b(L"仅填入推荐参数",     206, 142, 1002);
    mk_b(always_show ? L"关闭" : L"按当前参数运行", 358, 150, 1003);

    EnableWindow(g_app.hwnd, FALSE);
    ShowWindow(hDlg, SW_SHOW);
    UpdateWindow(hDlg);

    MSG m{};
    while (IsWindow(hDlg) && GetMessageW(&m, nullptr, 0, 0) > 0) {
        TranslateMessage(&m);
        DispatchMessageW(&m);
    }
    EnableWindow(g_app.hwnd, TRUE);
    SetForegroundWindow(g_app.hwnd);

    auto apply_rx_ui = [&]() {
        int pIdx = (rPre == "sci") ? 1 : (rPre == "arc") ? 2 : (rPre == "astro") ? 3 : 0;
        apply_preset_to_controls(pIdx);
        wchar_t nb[32];
        std::swprintf(nb, 32, L"%.2f", rDen);  SetWindowTextW(g_app.hEdtDenoise, nb);
        std::swprintf(nb, 32, L"%.2f", rDegh); SetWindowTextW(g_app.hEdtDeghost, nb);
        std::swprintf(nb, 32, L"%d",   rPsf);  SetWindowTextW(g_app.hEdtPsfIters, nb);

        int code = (st.sel_idx >= 0 && (size_t)st.sel_idx < frame_codes.size()) ? frame_codes[(size_t)st.sel_idx] : 0;
        if (code >= 1) {
            g_app.custom_anchor_1based = code;
            for (int i = 0; i < 3; ++i) SendMessageW(g_app.hRadMot[i], BM_SETCHECK, (i == 0) ? BST_CHECKED : BST_UNCHECKED, 0);
            SetWindowTextW(g_app.hRadMot[0], (L"指定 #" + std::to_wstring(code)).c_str());
        } else {
            g_app.custom_anchor_1based = 0;
            SetWindowTextW(g_app.hRadMot[0], L"自动最锐");
            int mIdx = (code == -1) ? 1 : (code == -2) ? 2 : 0;
            for (int i = 0; i < 3; ++i) SendMessageW(g_app.hRadMot[i], BM_SETCHECK, (i == mIdx) ? BST_CHECKED : BST_UNCHECKED, 0);
        }
    };

    if (st.result == 1) {
        apply_rx_ui();
        return 1;
    }
    if (st.result == 3) {
        apply_rx_ui();
        append_log_ui(L"[智能向导] 已将推荐处方填入控制面板。");
        return 3;
    }
    return always_show ? 0 : (st.result == 2 ? 2 : 0);
}

static void set_running_ui(bool busy) {
    g_app.running.store(busy);
    EnableWindow(g_app.hBtnRun, !busy);
    InvalidateRect(g_app.hBtnRun, nullptr, FALSE);
    EnableWindow(g_app.hBtnInspect, !busy);
    InvalidateRect(g_app.hBtnInspect, nullptr, FALSE);
    EnableWindow(g_app.hBtnCancel, busy);
    InvalidateRect(g_app.hBtnCancel, nullptr, FALSE);
}

static std::vector<std::wstring> build_base_cli_args(const std::wstring& exe, const std::wstring& lr_path) {
    std::vector<std::wstring> args;
    args.push_back(exe);
    if (!lr_path.empty()) {
        args.push_back(L"--libraw");
        args.push_back(lr_path);
        reg_write_str(L"LibRawPath", lr_path);
    }

    if (SendMessageW(g_app.hChkSortTime, BM_GETCHECK, 0, 0) == BST_CHECKED) args.push_back(L"--sort-time");
    else args.push_back(L"--no-sort-time");
    if (SendMessageW(g_app.hChkRecursive, BM_GETCHECK, 0, 0) == BST_CHECKED) args.push_back(L"--recursive");

    if (SendMessageW(g_app.hRadPol[0], BM_GETCHECK, 0, 0) == BST_CHECKED) args.push_back(L"--policy"), args.push_back(L"interactive");
    else if (SendMessageW(g_app.hRadPol[1], BM_GETCHECK, 0, 0) == BST_CHECKED) args.push_back(L"--policy"), args.push_back(L"auto-prune");
    else if (SendMessageW(g_app.hRadPol[2], BM_GETCHECK, 0, 0) == BST_CHECKED) args.push_back(L"--policy"), args.push_back(L"multi-group");
    else args.push_back(L"--policy"), args.push_back(L"force-all");

    if (SendMessageW(g_app.hRadPre[1], BM_GETCHECK, 0, 0) == BST_CHECKED) args.push_back(L"--preset"), args.push_back(L"sci");
    else if (SendMessageW(g_app.hRadPre[2], BM_GETCHECK, 0, 0) == BST_CHECKED) args.push_back(L"--preset"), args.push_back(L"arc");
    else if (SendMessageW(g_app.hRadPre[3], BM_GETCHECK, 0, 0) == BST_CHECKED) args.push_back(L"--preset"), args.push_back(L"astro");
    else args.push_back(L"--preset"), args.push_back(L"photo");

    if (g_app.custom_anchor_1based > 0) {
        args.push_back(L"--anchor-frame");
        args.push_back(std::to_wstring(g_app.custom_anchor_1based));
    } else if (SendMessageW(g_app.hRadMot[1], BM_GETCHECK, 0, 0) == BST_CHECKED) {
        args.push_back(L"--anchor-frame"); args.push_back(L"first");
    } else if (SendMessageW(g_app.hRadMot[2], BM_GETCHECK, 0, 0) == BST_CHECKED) {
        args.push_back(L"--anchor-frame"); args.push_back(L"middle");
    } else {
        args.push_back(L"--anchor-frame"); args.push_back(L"auto");
    }

    static const wchar_t* kBackends[] = {L"auto", L"gpu", L"cpu"};
    int bsel = (int)SendMessageW(g_app.hCmbBackend, CB_GETCURSEL, 0, 0);
    if (bsel >= 0 && bsel < 3) { args.push_back(L"--backend"); args.push_back(kBackends[bsel]); }

    auto add_opt = [&](const wchar_t* flag, HWND hEdt) {
        std::wstring v = trim_ws(get_wnd_text(hEdt));
        if (!v.empty()) { args.push_back(flag); args.push_back(v); }
    };
    add_opt(L"--denoise-strength", g_app.hEdtDenoise);
    add_opt(L"--chroma-denoise",   g_app.hEdtChroma);
    add_opt(L"--psf-iters",        g_app.hEdtPsfIters);
    add_opt(L"--rgb-psf-iters",    g_app.hEdtRgbPsf);
    add_opt(L"--psf-sigma",        g_app.hEdtPsfSigma);
    add_opt(L"--deghost-strength", g_app.hEdtDeghost);
    add_opt(L"--ev-offset",        g_app.hEdtEv);
    add_opt(L"--group-size",       g_app.hEdtGrpSz);

    int thr = _wtoi(get_wnd_text(g_app.hEdtThreads).c_str());
    if (thr > 0) { args.push_back(L"--threads"); args.push_back(std::to_wstring(thr)); }

    args.push_back(L"--auto-exposure");
    args.push_back(SendMessageW(g_app.hChkAutoExp, BM_GETCHECK, 0, 0) == BST_CHECKED ? L"on" : L"off");
    args.push_back(L"--highlight-rolloff");
    args.push_back(SendMessageW(g_app.hChkRolloff, BM_GETCHECK, 0, 0) == BST_CHECKED ? L"on" : L"off");
    args.push_back(L"--edge-polish");
    args.push_back(SendMessageW(g_app.hChkEdgePol, BM_GETCHECK, 0, 0) == BST_CHECKED ? L"on" : L"off");
    args.push_back(L"--dither");
    args.push_back(SendMessageW(g_app.hChkDither, BM_GETCHECK, 0, 0) == BST_CHECKED ? L"on" : L"off");

    static const wchar_t* kRawFmts[] = {L"dng", L"dng-hq", L"dng-fp16", L"dng-raw"};
    int rsel = (int)SendMessageW(g_app.hCmbRawFmt, CB_GETCURSEL, 0, 0);
    if (rsel >= 0 && rsel < 4) { args.push_back(L"--raw-format"); args.push_back(kRawFmts[rsel]); }

    static const wchar_t* kRgbFmts[] = {L"tiff-deflate", L"png", L"jxs", L"jxs-lossless", L"tiff-raw"};
    int csel = (int)SendMessageW(g_app.hCmbRgbFmt, CB_GETCURSEL, 0, 0);
    if (csel >= 0 && csel < 5) { args.push_back(L"--rgb-format"); args.push_back(kRgbFmts[csel]); }
    add_opt(L"--jxs-bpp", g_app.hEdtJxsBpp);

    static const wchar_t* kFieldModes[] = {L"split", L"rgb", L"motion", L"phase", L"focus"};
    int fmsel = (int)SendMessageW(g_app.hCmbFieldMode, CB_GETCURSEL, 0, 0);
    if (fmsel >= 0 && fmsel < 5) { args.push_back(L"--field-mode"); args.push_back(kFieldModes[fmsel]); }

    args.push_back(L"--color-space");
    args.push_back(SendMessageW(g_app.hRadCsAdobe, BM_GETCHECK, 0, 0) == BST_CHECKED ? L"adobergb" : L"srgb");
    return args;
}

static void set_prog_pct(int pct) {
    g_app.prog_pct = std::max(0, std::min(100, pct));
    RECT prc{g_app.S(392), g_app.S(596), g_app.S(940), g_app.S(620)};
    InvalidateRect(g_app.hwnd, &prc, FALSE);
    SetWindowTextW(g_app.hPctLbl, (std::to_wstring(g_app.prog_pct) + L"%").c_str());
}

static void save_ui_prefs() {
    if (g_app.is_screenshot_mode || !g_app.hCmbRawFmt) return;
    auto bstr = [](HWND h) -> std::wstring {
        return (SendMessageW(h, BM_GETCHECK, 0, 0) == BST_CHECKED) ? L"1" : L"0";
    };
    static const wchar_t* kRawCodes[] = {L"dng", L"dng-hq", L"dng-fp16", L"dng-raw"};
    static const wchar_t* kRgbCodes[] = {L"tiff-deflate", L"png", L"jxs", L"jxs-lossless", L"tiff-raw"};
    reg_write_str(L"HasSavedUiPrefs", L"1");
    reg_write_str(L"EmitRaw",       bstr(g_app.hChkRaw));
    int r_idx = (int)SendMessageW(g_app.hCmbRawFmt, CB_GETCURSEL, 0, 0);
    if (r_idx >= 0 && r_idx < 4) reg_write_str(L"RawFmtCode", kRawCodes[r_idx]);
    reg_write_str(L"EmitRgb",       bstr(g_app.hChkRgb));
    int c_idx = (int)SendMessageW(g_app.hCmbRgbFmt, CB_GETCURSEL, 0, 0);
    if (c_idx >= 0 && c_idx < 5) reg_write_str(L"RgbFmtCode", kRgbCodes[c_idx]);
    reg_write_str(L"JxsBpp",        trim_ws(get_wnd_text(g_app.hEdtJxsBpp)));
    static const wchar_t* kFieldCodes[] = {L"split", L"rgb", L"motion", L"phase", L"focus"};
    reg_write_str(L"ColorSpaceIdx", (SendMessageW(g_app.hRadCsAdobe, BM_GETCHECK, 0, 0) == BST_CHECKED) ? L"1" : L"0");
    reg_write_str(L"EmitField",     bstr(g_app.hChkField));
    int fm_idx = (int)SendMessageW(g_app.hCmbFieldMode, CB_GETCURSEL, 0, 0);
    if (fm_idx >= 0 && fm_idx < 5) reg_write_str(L"FieldModeCode", kFieldCodes[fm_idx]);

    reg_write_str(L"SortTime",      bstr(g_app.hChkSortTime));
    reg_write_str(L"Recursive",     bstr(g_app.hChkRecursive));

    for (int i = 0; i < 4; ++i)
        if (SendMessageW(g_app.hRadPol[i], BM_GETCHECK, 0, 0) == BST_CHECKED) { reg_write_str(L"PolicyIdx", std::to_wstring(i)); break; }
    for (int i = 0; i < 4; ++i)
        if (SendMessageW(g_app.hRadPre[i], BM_GETCHECK, 0, 0) == BST_CHECKED) { reg_write_str(L"PresetIdx", std::to_wstring(i)); break; }
    for (int i = 0; i < 3; ++i)
        if (SendMessageW(g_app.hRadMot[i], BM_GETCHECK, 0, 0) == BST_CHECKED) { reg_write_str(L"AnchorIdx", std::to_wstring(i)); break; }

    reg_write_str(L"BackendIdx",    std::to_wstring((int)SendMessageW(g_app.hCmbBackend, CB_GETCURSEL, 0, 0)));
    reg_write_str(L"Denoise",       trim_ws(get_wnd_text(g_app.hEdtDenoise)));
    reg_write_str(L"Chroma",        trim_ws(get_wnd_text(g_app.hEdtChroma)));
    reg_write_str(L"PsfIters",      trim_ws(get_wnd_text(g_app.hEdtPsfIters)));
    reg_write_str(L"RgbPsf",        trim_ws(get_wnd_text(g_app.hEdtRgbPsf)));
    reg_write_str(L"PsfSigma",      trim_ws(get_wnd_text(g_app.hEdtPsfSigma)));
    reg_write_str(L"Deghost",       trim_ws(get_wnd_text(g_app.hEdtDeghost)));
    reg_write_str(L"EvOffset",      trim_ws(get_wnd_text(g_app.hEdtEv)));
    reg_write_str(L"GroupSize",     trim_ws(get_wnd_text(g_app.hEdtGrpSz)));
    reg_write_str(L"Threads",       trim_ws(get_wnd_text(g_app.hEdtThreads)));

    reg_write_str(L"AutoExp",       bstr(g_app.hChkAutoExp));
    reg_write_str(L"Rolloff",       bstr(g_app.hChkRolloff));
    reg_write_str(L"EdgePol",       bstr(g_app.hChkEdgePol));
    reg_write_str(L"Dither",        bstr(g_app.hChkDither));
}

static void load_ui_prefs() {
    if (reg_read_str(L"HasSavedUiPrefs") != L"1") return;
    auto set_chk = [](HWND h, const wchar_t* k) {
        std::wstring v = reg_read_str(k);
        if (!v.empty()) SendMessageW(h, BM_SETCHECK, (v == L"1") ? BST_CHECKED : BST_UNCHECKED, 0);
    };
    auto set_cmb = [](HWND h, const wchar_t* k, int count) {
        std::wstring v = reg_read_str(k);
        if (!v.empty()) {
            int idx = _wtoi(v.c_str());
            if (idx >= 0 && idx < count) SendMessageW(h, CB_SETCURSEL, (WPARAM)idx, 0);
        }
    };
    auto set_edt = [](HWND h, const wchar_t* k) {
        std::wstring v = reg_read_str(k);
        if (!v.empty()) SetWindowTextW(h, v.c_str());
    };

    static const wchar_t* kRawCodes[] = {L"dng", L"dng-hq", L"dng-fp16", L"dng-raw"};
    static const wchar_t* kRgbCodes[] = {L"tiff-deflate", L"png", L"jxs", L"jxs-lossless", L"tiff-raw"};
    set_chk(g_app.hChkRaw, L"EmitRaw");
    std::wstring rc = reg_read_str(L"RawFmtCode");
    for (int i = 0; i < 4; ++i) if (rc == kRawCodes[i]) { SendMessageW(g_app.hCmbRawFmt, CB_SETCURSEL, (WPARAM)i, 0); break; }
    set_chk(g_app.hChkRgb, L"EmitRgb");
    std::wstring cc = reg_read_str(L"RgbFmtCode");
    for (int i = 0; i < 5; ++i) if (cc == kRgbCodes[i]) { SendMessageW(g_app.hCmbRgbFmt, CB_SETCURSEL, (WPARAM)i, 0); break; }
    set_edt(g_app.hEdtJxsBpp, L"JxsBpp");

    std::wstring cs = reg_read_str(L"ColorSpaceIdx");
    if (!cs.empty()) {
        bool adobe = (cs == L"1");
        SendMessageW(g_app.hRadCsSrgb,  BM_SETCHECK, adobe ? BST_UNCHECKED : BST_CHECKED, 0);
        SendMessageW(g_app.hRadCsAdobe, BM_SETCHECK, adobe ? BST_CHECKED : BST_UNCHECKED, 0);
    }
    static const wchar_t* kFieldCodes[] = {L"split", L"rgb", L"motion", L"phase", L"focus"};
    set_chk(g_app.hChkField, L"EmitField");
    std::wstring fmc = reg_read_str(L"FieldModeCode");
    for (int i = 0; i < 5; ++i) if (fmc == kFieldCodes[i]) { SendMessageW(g_app.hCmbFieldMode, CB_SETCURSEL, (WPARAM)i, 0); break; }
    set_chk(g_app.hChkSortTime,  L"SortTime");
    set_chk(g_app.hChkRecursive, L"Recursive");

    std::wstring pol = reg_read_str(L"PolicyIdx");
    if (!pol.empty()) {
        int pi = _wtoi(pol.c_str());
        if (pi >= 0 && pi < 4)
            for (int i = 0; i < 4; ++i) SendMessageW(g_app.hRadPol[i], BM_SETCHECK, (i == pi) ? BST_CHECKED : BST_UNCHECKED, 0);
    }
    std::wstring pre = reg_read_str(L"PresetIdx");
    if (!pre.empty()) {
        int pri = _wtoi(pre.c_str());
        if (pri >= 0 && pri < 4) apply_preset_to_controls(pri);
    }
    std::wstring anc = reg_read_str(L"AnchorIdx");
    if (!anc.empty()) {
        int ai = _wtoi(anc.c_str());
        if (ai >= 0 && ai < 3)
            for (int i = 0; i < 3; ++i) SendMessageW(g_app.hRadMot[i], BM_SETCHECK, (i == ai) ? BST_CHECKED : BST_UNCHECKED, 0);
    }

    set_cmb(g_app.hCmbBackend, L"BackendIdx", 3);
    set_edt(g_app.hEdtDenoise,  L"Denoise");
    set_edt(g_app.hEdtChroma,   L"Chroma");
    set_edt(g_app.hEdtPsfIters, L"PsfIters");
    set_edt(g_app.hEdtRgbPsf,   L"RgbPsf");
    set_edt(g_app.hEdtPsfSigma, L"PsfSigma");
    set_edt(g_app.hEdtDeghost,  L"Deghost");
    set_edt(g_app.hEdtEv,       L"EvOffset");
    set_edt(g_app.hEdtGrpSz,    L"GroupSize");
    set_edt(g_app.hEdtThreads,  L"Threads");

    set_chk(g_app.hChkAutoExp, L"AutoExp");
    set_chk(g_app.hChkRolloff, L"Rolloff");
    set_chk(g_app.hChkEdgePol, L"EdgePol");
    set_chk(g_app.hChkDither,  L"Dither");
}

static void start_job(bool wizard_inspect_only) {
    if (g_app.running.load()) return;
    save_ui_prefs();
    if (g_app.inputs.empty()) {
        MessageBoxW(g_app.hwnd, L"请先添加 RAW 文件或目录。", L"提示", MB_ICONINFORMATION);
        return;
    }

    std::wstring exe = resolve_active_cli_or_prompt(g_app.hwnd);
    if (exe.empty()) return;

    std::wstring lr_path = trim_ws(get_wnd_text(g_app.hEdtLibRawPath));
    if (!lr_path.empty() && !file_exists_regular(lr_path)) {
        MessageBoxW(g_app.hwnd, (L"LibRaw 文件不存在：\n" + lr_path).c_str(), L"提示", MB_ICONWARNING);
        return;
    }

    if (wizard_inspect_only) {
        std::vector<std::wstring> wargs = build_base_cli_args(exe, lr_path);
        for (const auto& p : g_app.inputs) wargs.push_back(p);
        int wres = run_smart_wizard_modal(wargs, true);
        if (wres != 1) return;
    }

    bool want_raw = (SendMessageW(g_app.hChkRaw, BM_GETCHECK, 0, 0) == BST_CHECKED);
    bool want_rgb = (SendMessageW(g_app.hChkRgb, BM_GETCHECK, 0, 0) == BST_CHECKED);
    bool want_fld = (SendMessageW(g_app.hChkField, BM_GETCHECK, 0, 0) == BST_CHECKED);
    if (!want_raw && !want_rgb && !want_fld) {
        MessageBoxW(g_app.hwnd, L"请至少勾选一项输出（2× RGGB / 2× RGB / 输出场图）。", L"提示", MB_ICONWARNING);
        return;
    }

    std::vector<std::wstring> args = build_base_cli_args(exe, lr_path);
    bool is_interactive = (SendMessageW(g_app.hRadPol[0], BM_GETCHECK, 0, 0) == BST_CHECKED);
    if (!wizard_inspect_only && is_interactive) {
        std::vector<std::wstring> preflight_args = args;
        for (const auto& p : g_app.inputs) preflight_args.push_back(p);
        int choice = run_smart_wizard_modal(preflight_args, false);
        if (choice == 0 || choice == 3) return;
        args = build_base_cli_args(exe, lr_path);
        args.push_back(L"--interactive-choice");
        args.push_back((choice == 1) ? L"1" : L"2");
    } else if (wizard_inspect_only && is_interactive) {
        args.push_back(L"--interactive-choice");
        args.push_back(L"1");
    }

    args.push_back(L"--progress-json");
    std::wstring out_dir = trim_ws(get_wnd_text(g_app.hOutDir));
    if (out_dir.empty()) {
        out_dir = parent_dir_of(g_app.inputs[0]) + L"\\nijinLBS_out";
        SetWindowTextW(g_app.hOutDir, out_dir.c_str());
    }
    args.push_back(L"--out-dir");
    args.push_back(out_dir);
    if (want_raw) args.push_back(L"--emit-raw");
    if (want_rgb) args.push_back(L"--emit-rgb");
    if (want_fld) args.push_back(L"--emit-field");
    for (const auto& p : g_app.inputs) args.push_back(p);

    std::wstring cmdline;
    for (size_t i = 0; i < args.size(); ++i) {
        if (i > 0) cmdline.push_back(L' ');
        cmdline += quote_win_arg(args[i]);
    }

    set_prog_pct(0);
    append_log_ui(L"> " + cmdline);
    set_running_ui(true);

    if (g_app.worker.joinable()) g_app.worker.join();
    g_app.worker = std::thread([cmdline]() {
        SECURITY_ATTRIBUTES sa{sizeof(SECURITY_ATTRIBUTES), nullptr, TRUE};
        HANDLE hR = nullptr, hW = nullptr;
        if (!CreatePipe(&hR, &hW, &sa, 0)) {
            PostMessageW(g_app.hwnd, WM_APP_DONE, 1, 0);
            return;
        }
        SetHandleInformation(hR, HANDLE_FLAG_INHERIT, 0);

        STARTUPINFOW si{};
        si.cb = sizeof(si);
        si.dwFlags = STARTF_USESTDHANDLES | STARTF_USESHOWWINDOW;
        si.wShowWindow = SW_HIDE;
        si.hStdOutput = hW;
        si.hStdError = hW;
        PROCESS_INFORMATION pi{};
        std::vector<wchar_t> mcmd(cmdline.begin(), cmdline.end()); mcmd.push_back(L'\0');

        BOOL ok = CreateProcessW(nullptr, mcmd.data(), nullptr, nullptr, TRUE, CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi);
        CloseHandle(hW);
        if (!ok) {
            CloseHandle(hR);
            post_log(L"[Error] Failed to launch CLI.");
            PostMessageW(g_app.hwnd, WM_APP_DONE, 1, 0);
            return;
        }
        { std::lock_guard<std::mutex> lk(g_app.proc_mtx); g_app.hChildProc = pi.hProcess; }

        std::string pending;
        char buf[1024];
        DWORD nr = 0;
        while (ReadFile(hR, buf, sizeof(buf), &nr, nullptr) && nr > 0) {
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
                } else {
                    post_log(utf8_to_wide(line));
                }
            }
        }
        if (!pending.empty()) post_log(utf8_to_wide(pending));
        CloseHandle(hR);

        WaitForSingleObject(pi.hProcess, INFINITE);
        DWORD ec = 1;
        GetExitCodeProcess(pi.hProcess, &ec);
        { std::lock_guard<std::mutex> lk(g_app.proc_mtx); g_app.hChildProc = nullptr; }
        CloseHandle(pi.hThread);
        CloseHandle(pi.hProcess);
        PostMessageW(g_app.hwnd, WM_APP_DONE, (WPARAM)ec, 0);
    });
}

static void paint_studio_canvas(HWND hwnd, HDC hdc) {
    RECT crc{};
    GetClientRect(hwnd, &crc);
    FillRect(hdc, &crc, g_app.hBrCanvas);

    // Header Bar (0, 0, width, 62)
    RECT hdr{0, 0, crc.right, g_app.S(62)};
    FillRect(hdc, &hdr, g_app.hBrCard);

    HBRUSH brAccent = CreateSolidBrush(RGB(84, 130, 150));
    RECT topStripe{0, 0, crc.right, g_app.S(3)};
    FillRect(hdc, &topStripe, brAccent);
    DeleteObject(brAccent);

    HBRUSH brBorder = CreateSolidBrush(kColCardBorder);
    RECT hdrLine{0, g_app.S(61), crc.right, g_app.S(62)};
    FillRect(hdc, &hdrLine, brBorder);
    DeleteObject(brBorder);

    // Right-Side Co-Brand Cluster: tight [GX Cross-Star] | [NijiN]
    int nx = crc.right - g_app.S(20 + heraw_brand::kNijin1xW);
    int divX = nx - g_app.S(6);
    int gxX = divX - g_app.S(7 + heraw_brand::kGx1xW);
    int gxY = g_app.S((62 - heraw_brand::kGx1xH) / 2);
    int njY = g_app.S((62 - heraw_brand::kNijin1xH) / 2);

    draw_alpha_bmp(hdc, g_app.hBmpGx, heraw_brand::kGx1xW, heraw_brand::kGx1xH,
                   gxX, gxY, g_app.S(heraw_brand::kGx1xW), g_app.S(heraw_brand::kGx1xH));

    HBRUSH brCoDiv = CreateSolidBrush(RGB(198, 214, 221));
    RECT rcCoDiv{divX, g_app.S(17), divX + 1, g_app.S(45)};
    FillRect(hdc, &rcCoDiv, brCoDiv);
    DeleteObject(brCoDiv);

    draw_alpha_bmp(hdc, g_app.hBmpNijin, heraw_brand::kNijin1xW, heraw_brand::kNijin1xH,
                   nx, njY, g_app.S(heraw_brand::kNijin1xW), g_app.S(heraw_brand::kNijin1xH));

    // Antialiased Rounded Cards, Pills, Input Wells & Smooth Pill Progress Bar via GDI+
    {
        Gdiplus::Graphics g(hdc);
        g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);

        auto card = [&](int x, int y, int w, int h) {
            gdip_round_rect(g, (float)g_app.S(x), (float)g_app.S(y),
                            (float)g_app.S(w), (float)g_app.S(h),
                            (float)g_app.S(11), gdip_col(kColCard), gdip_col(kColCardBorder), 1.15f);
        };
        card(18,  76,  480, 260);
        card(18,  350, 480, 216);
        card(514, 76,  488, 196);
        card(514, 286, 488, 280);
        card(18,  580, 984, 110);

        // Header "nijinLBS" pill
        gdip_round_rect(g, (float)g_app.S(228), (float)g_app.S(21),
                        (float)g_app.S(64), (float)g_app.S(20),
                        (float)g_app.S(6), gdip_col(kColBadgeFill), gdip_col(kColBadgeEdge), 1.0f);

        auto badge_bg = [&](int x, int y) {
            gdip_round_rect(g, (float)g_app.S(x), (float)g_app.S(y),
                            (float)g_app.S(26), (float)g_app.S(18),
                            (float)g_app.S(5), gdip_col(kColBadgeFill), gdip_col(kColBadgeEdge), 1.0f);
        };
        badge_bg(34,  90);
        badge_bg(34,  364);
        badge_bg(34,  462);
        badge_bg(530, 90);
        badge_bg(530, 300);

        // Subtle card dividers
        Gdiplus::Pen penDiv(gdip_col(RGB(218, 229, 234)), 1.0f);
        g.DrawLine(&penDiv, (float)g_app.S(34),  (float)g_app.S(450), (float)g_app.S(482), (float)g_app.S(450));
        g.DrawLine(&penDiv, (float)g_app.S(530), (float)g_app.S(398), (float)g_app.S(986), (float)g_app.S(398));

        // Smooth Rounded Input Wells around borderless EDIT and LISTBOX controls
        HWND hChild = GetWindow(hwnd, GW_CHILD);
        while (hChild) {
            wchar_t cls[64] = {0};
            GetClassNameW(hChild, cls, 63);
            if (_wcsicmp(cls, L"Edit") == 0 || _wcsicmp(cls, L"ListBox") == 0) {
                RECT wr{};
                GetWindowRect(hChild, &wr);
                MapWindowPoints(nullptr, hwnd, reinterpret_cast<LPPOINT>(&wr), 2);
                bool isLog = (hChild == g_app.hLog);
                float padX = (float)g_app.S(5), padY = (float)g_app.S(3);
                gdip_round_rect(g,
                    (float)wr.left - padX, (float)wr.top - padY,
                    (float)(wr.right - wr.left) + 2.0f * padX, (float)(wr.bottom - wr.top) + 2.0f * padY,
                    (float)g_app.S(6),
                    gdip_col(isLog ? kColLogBg : RGB(255, 255, 255)),
                    gdip_col(kColInputEdge), 1.15f);
            }
            hChild = GetWindow(hChild, GW_HWNDNEXT);
        }

        // Smooth Rounded Aqua Pill Progress Bar (replaces Win95 chunky trough)
        float px = (float)g_app.S(396), py = (float)g_app.S(604);
        float pw = (float)g_app.S(536), ph = (float)g_app.S(8);
        gdip_round_rect(g, px, py, pw, ph, ph * 0.5f,
                        gdip_col(RGB(218, 231, 236)), gdip_col(RGB(198, 216, 224)), 1.0f);
        if (g_app.prog_pct > 0) {
            float fw = std::max(ph, pw * ((float)g_app.prog_pct / 100.0f));
            gdip_round_rect(g, px, py, fw, ph, ph * 0.5f,
                            gdip_col(kColAccent), Gdiplus::Color(0, 0, 0, 0), 0.0f);
        } else {
            // Subtle macOS-style origin dot at 0%
            Gdiplus::SolidBrush dbr(gdip_col(kColAccent));
            g.FillEllipse(&dbr, px + 1.0f, py + 1.0f, ph - 2.0f, ph - 2.0f);
        }
    }

    // Badge & Header Text Labels
    SetBkMode(hdc, TRANSPARENT);
    SetTextColor(hdc, kColBadgeText);
    HGDIOBJ oldFont = SelectObject(hdc, g_app.hFontBadge);

    RECT rTag{g_app.S(228), g_app.S(21), g_app.S(292), g_app.S(41)};
    DrawTextW(hdc, L"nijinLBS", -1, &rTag, DT_CENTER | DT_VCENTER | DT_SINGLELINE);

    SetTextColor(hdc, kColTextMuted);
    SelectObject(hdc, g_app.hFont);
    RECT rDesc{g_app.S(304), g_app.S(21), g_app.S(540), g_app.S(41)};
    DrawTextW(hdc, L"Fast Multi-Frame Upscaler", -1, &rDesc, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
    SetTextColor(hdc, kColBadgeText);
    SelectObject(hdc, g_app.hFontBadge);

    auto badge_txt = [&](int x, int y, const wchar_t* txt) {
        RECT r{g_app.S(x), g_app.S(y), g_app.S(x + 26), g_app.S(y + 18)};
        DrawTextW(hdc, txt, -1, &r, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    };
    badge_txt(34,  90,  L"01");
    badge_txt(34,  364, L"02");
    badge_txt(34,  462, L"03");
    badge_txt(530, 90,  L"04");
    badge_txt(530, 300, L"05");

    SelectObject(hdc, oldFont);
}

static LRESULT CALLBACK MainWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
        case WM_CREATE: {
            g_app.hwnd = hwnd;
            DragAcceptFiles(hwnd, TRUE);
            HDC hdc = GetDC(hwnd);
            g_app.dpi = GetDeviceCaps(hdc, LOGPIXELSY);
            ReleaseDC(hwnd, hdc);
            auto S = [](int px) { return g_app.S(px); };

            g_app.hBrCanvas = CreateSolidBrush(kColCanvas);
            g_app.hBrCard   = CreateSolidBrush(kColCard);
            g_app.hBrLog    = CreateSolidBrush(kColLogBg);
            g_app.hBrWhite  = CreateSolidBrush(RGB(255, 255, 255));

            g_app.hBmpGx    = create_premul_dib(heraw_brand::kGx1xW, heraw_brand::kGx1xH, heraw_brand::kGxStarPremulBGRA1x);
            g_app.hBmpNijin = create_premul_dib(heraw_brand::kNijin1xW, heraw_brand::kNijin1xH, heraw_brand::kNijinPremulBGRA1x);

            g_app.hFont = CreateFontW(-S(12), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
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

            g_app.hTooltip = CreateWindowExW(
                WS_EX_TOPMOST, TOOLTIPS_CLASSW, nullptr,
                WS_POPUP | TTS_NOPREFIX | TTS_ALWAYSTIP,
                CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT,
                hwnd, nullptr, GetModuleHandleW(nullptr), nullptr
            );
            if (g_app.hTooltip) {
                SendMessageW(g_app.hTooltip, TTM_SETMAXTIPWIDTH, 0, S(420));
                SendMessageW(g_app.hTooltip, TTM_SETDELAYTIME, TTDT_INITIAL, 60);
                SendMessageW(g_app.hTooltip, TTM_SETDELAYTIME, TTDT_RESHOW, 40);
                SendMessageW(g_app.hTooltip, TTM_SETDELAYTIME, TTDT_AUTOPOP, 25000);
            }

            auto mk = [&](const wchar_t* cls, const wchar_t* txt, DWORD st, int x, int y, int w, int h, int id, DWORD ex = 0) {
                HWND hw = CreateWindowExW(ex, cls, txt, WS_CHILD | WS_VISIBLE | st,
                    S(x), S(y), S(w), S(h), hwnd, (HMENU)(INT_PTR)id, GetModuleHandleW(nullptr), nullptr);
                SendMessageW(hw, WM_SETFONT, (WPARAM)g_app.hFont, TRUE);
                return hw;
            };
            auto card_hdr = [&](const wchar_t* txt, int x, int y, int w) {
                HWND hw = mk(L"STATIC", txt, SS_LEFT, x, y, w, 20, 0);
                SendMessageW(hw, WM_SETFONT, (WPARAM)g_app.hFontCardHdr, TRUE);
                return hw;
            };

            auto mk_btn = [&](const wchar_t* txt, int x, int y, int w, int h, int id) {
                HWND hw = mk(L"BUTTON", txt, BS_OWNERDRAW, x, y, w, h, id);
                SetWindowSubclass(hw, ModernBtnHoverSubclassProc, 3, 0);
                return hw;
            };
            auto mk_chk = [&](const wchar_t* txt, int x, int y, int w, int id, bool checked = false) {
                HWND hw = mk(L"BUTTON", txt, BS_AUTOCHECKBOX, x, y, w, 22, id);
                if (checked) SendMessageW(hw, BM_SETCHECK, BST_CHECKED, 0);
                attach_modern_toggle(hw, false, false);
                return hw;
            };
            auto mk_rad = [&](const wchar_t* txt, int x, int y, int w, int id, bool group_start, bool bold = false) {
                HWND hw = mk(L"BUTTON", txt, BS_AUTORADIOBUTTON | (group_start ? WS_GROUP : 0), x, y, w, 24, id);
                if (bold) SendMessageW(hw, WM_SETFONT, (WPARAM)g_app.hFontBold, TRUE);
                attach_modern_toggle(hw, true, bold);
                return hw;
            };
            auto mk_edt = [&](const wchar_t* txt, int x, int y, int w, int h, int id, DWORD extra = 0) {
                // Inset by (5, 3) inside the antialiased rounded white input well drawn by paint_studio_canvas
                return mk(L"EDIT", txt, ES_AUTOHSCROLL | extra, x + 5, y + 3, w - 10, h - 6, id, 0);
            };
            auto mk_cmb = [&](int x, int y, int w, int drop_h, int id) {
                HWND hw = mk(L"COMBOBOX", L"", CBS_DROPDOWNLIST | WS_VSCROLL, x, y, w, drop_h, id);
                SetWindowSubclass(hw, ModernComboSubclassProc, 2, 0);
                return hw;
            };

            // Header Left Title (single line, clean)
            g_app.hHdrTitle = mk(L"STATIC", L"NijiN.LiteBurstScaler", SS_LEFT, 24, 18, 206, 26, 0);
            SendMessageW(g_app.hHdrTitle, WM_SETFONT, (WPARAM)g_app.hFontTitle, TRUE);

            // === Card 01: Input ===
            card_hdr(L"输入源", 66, 89, 58);
            g_app.hBanner = mk(L"STATIC", L"", SS_LEFT, 128, 91, 326, 18, 0);
            create_hover_dot(hwnd, 464, 90,
                L"【输入与连拍张数】\r\n"
                L"• 最低门槛：≥ 4 张有效帧\r\n"
                L"• 推荐张数：手持 6–8 张，高感 9–12 张\r\n"
                L"• 内置 G1/G2 盲暗电平自标定与热噪点修复");

            g_app.hList = mk(L"LISTBOX", L"", LBS_NOTIFY | LBS_EXTENDEDSEL | LBS_NOINTEGRALHEIGHT | WS_VSCROLL | WS_HSCROLL,
                             39, 121, 438, 130, ID_LIST_INPUTS, 0);
            SendMessageW(g_app.hList, LB_SETHORIZONTALEXTENT, S(1400), 0);

            mk_btn(L"添加文件...", 34,  262, 110, 28, ID_BTN_ADD_FILES);
            mk_btn(L"添加目录...", 150, 262, 110, 28, ID_BTN_ADD_FOLDER);
            mk_btn(L"移除",        266, 262, 104, 28, ID_BTN_REMOVE);
            mk_btn(L"清空",        376, 262, 106, 28, ID_BTN_CLEAR);

            g_app.hChkSortTime  = mk_chk(L"EXIF 时间排序", 34,  300, 136, ID_CHK_SORT_TIME, true);
            g_app.hChkRecursive = mk_chk(L"递归子目录",    178, 300, 112, ID_CHK_RECURSIVE, false);
            g_app.hBtnInspect   = mk_btn(L"智能向导",      354, 297, 128, 28, ID_BTN_INSPECT);

            // === Card 02 & 03: Policy, Presets & Pose Anchor ===
            card_hdr(L"序列策略", 66, 363, 180);
            create_hover_dot(hwnd, 464, 364,
                L"【序列策略与智能向导】\r\n"
                L"• 智能向导：五维识别星空/星野/AEB/三脚架/动体，推荐处方并支持逐帧选定格\r\n"
                L"• 自动筛除：自动剔除糊片/切镜/曝光异常，保留 ≥4 张\r\n"
                L"• 自动多组：长连拍按每组帧数自动切分多组输出\r\n"
                L"• 强制全选：忽略张数与位移门槛（可至 1 张）");

            g_app.hRadPol[0] = mk_rad(L"智能向导", 36,  391, 180, ID_RAD_POL_INTERACTIVE, true);
            g_app.hRadPol[1] = mk_rad(L"自动筛除", 252, 391, 180, ID_RAD_POL_AUTOPRUNE,   false);
            g_app.hRadPol[2] = mk_rad(L"自动多组", 36,  417, 180, ID_RAD_POL_MULTIGROUP,  false);
            g_app.hRadPol[3] = mk_rad(L"强制全选", 252, 417, 180, ID_RAD_POL_FORCEALL,    false);
            SendMessageW(g_app.hRadPol[0], BM_SETCHECK, BST_CHECKED, 0);

            card_hdr(L"预设与定格", 66, 461, 180);
            create_hover_dot(hwnd, 464, 462,
                L"【四档模式预设、定格帧与物理细节重建】\r\n"
                L"预设：\r\n"
                L"• Photo 照片：时域相干保微纹理 + 刚体微块超分 + PSF 恢复 + 自动影调\r\n"
                L"• Sci 科学：关闭 PSF / 自动曝光 / 高光滚降 / 边缘抛光，严格线性真值\r\n"
                L"• Arc 归档：关闭 PSF 与边缘抛光，温和降噪与自然影调\r\n"
                L"• Astro 星空：防吞星跨通道门控 + 保星色 + 星点/地景天地双域对齐 + 夜空黑位锚定\r\n\r\n"
                L"定格帧（决定运动物体或天际线姿态，也可点「智能向导」任选第 #1..N 张）：\r\n"
                L"• 自动最锐 / 首帧定格 / 中间定格");

            mk(L"STATIC", L"预设", SS_LEFT, 36, 495, 36, 20, 0);
            g_app.hRadPre[0] = mk_rad(L"Photo 照片", 74,  492, 100, ID_RAD_PRE_PHOTO, true,  true);
            g_app.hRadPre[1] = mk_rad(L"Sci 科学",   180, 492, 86,  ID_RAD_PRE_SCI,   false, true);
            g_app.hRadPre[2] = mk_rad(L"Arc 归档",   270, 492, 86,  ID_RAD_PRE_ARC,   false, true);
            g_app.hRadPre[3] = mk_rad(L"Astro 星空", 360, 492, 108, ID_RAD_PRE_ASTRO, false, true);
            SendMessageW(g_app.hRadPre[0], BM_SETCHECK, BST_CHECKED, 0);

            mk(L"STATIC", L"定格", SS_LEFT, 36, 527, 36, 20, 0);
            g_app.hRadMot[0] = mk_rad(L"自动最锐", 74,  524, 118, ID_RAD_MOT_ANCHOR,     true);
            g_app.hRadMot[1] = mk_rad(L"首帧定格", 206, 524, 114, ID_RAD_MOT_DUALLAYER,  false);
            g_app.hRadMot[2] = mk_rad(L"中间定格", 334, 524, 114, ID_RAD_MOT_CLEANPLATE, false);
            SendMessageW(g_app.hRadMot[0], BM_SETCHECK, BST_CHECKED, 0);

            // === Card 04: Output ===
            card_hdr(L"输出", 562, 89, 180);
            create_hover_dot(hwnd, 968, 90,
                L"【按需输出与元数据】\r\n"
                L"• 支持单选 2× RGGB、单选 2× RGB 或同时输出\r\n"
                L"• 保留参考帧 EXIF / GPS，Adobe RGB 自动内嵌 ICC v2.4");

            mk(L"STATIC", L"输出目录", SS_LEFT, 530, 123, 62, 20, 0);
            g_app.hOutDir = mk_edt(L"", 596, 120, 304, 24, ID_EDT_OUT_DIR);
            SendMessageW(g_app.hOutDir, EM_SETCUEBANNER, FALSE, (LPARAM)L"默认同目录 nijinLBS_out");
            mk_btn(L"浏览", 906, 119, 80, 26, ID_BTN_OUT_DIR);

            g_app.hChkRaw = mk_chk(L"2× RGGB", 530, 158, 94, ID_CHK_EMIT_RAW, true);
            g_app.hCmbRawFmt = mk_cmb(626, 156, 330, 160, ID_CMB_RAW_FMT);
            SendMessageW(g_app.hCmbRawFmt, CB_ADDSTRING, 0, (LPARAM)L"DNG 16-bit 无损");
            SendMessageW(g_app.hCmbRawFmt, CB_ADDSTRING, 0, (LPARAM)L"DNG 高级有损压缩");
            SendMessageW(g_app.hCmbRawFmt, CB_ADDSTRING, 0, (LPARAM)L"DNG 16-bit 半精度浮点");
            SendMessageW(g_app.hCmbRawFmt, CB_ADDSTRING, 0, (LPARAM)L"DNG 16-bit 未压缩");
            SendMessageW(g_app.hCmbRawFmt, CB_SETCURSEL, 0, 0);
            create_hover_dot(hwnd, 968, 159,
                L"【RGGB 倍率参考（未压缩 = 1）】\r\n"
                L"• DNG 16-bit 未压缩      ：1.00（~349 MiB）\r\n"
                L"• DNG 16-bit 无损        ：0.39（~135 MiB）\r\n"
                L"• DNG 16-bit 半精度浮点  ：0.29（~100 MiB）\r\n"
                L"• DNG 高级有损压缩       ：0.09（~30 MiB）");

            g_app.hChkRgb = mk_chk(L"2× RGB", 530, 194, 94, ID_CHK_EMIT_RGB, true);
            g_app.hCmbRgbFmt = mk_cmb(626, 192, 238, 170, ID_CMB_RGB_FMT);
            SendMessageW(g_app.hCmbRgbFmt, CB_ADDSTRING, 0, (LPARAM)L"16-bit 无损 TIFF");
            SendMessageW(g_app.hCmbRgbFmt, CB_ADDSTRING, 0, (LPARAM)L"16-bit 无损 PNG");
            SendMessageW(g_app.hCmbRgbFmt, CB_ADDSTRING, 0, (LPARAM)L"14-bit JPEG XS 高级有损压缩");
            SendMessageW(g_app.hCmbRgbFmt, CB_ADDSTRING, 0, (LPARAM)L"14-bit JPEG XS 无损");
            SendMessageW(g_app.hCmbRgbFmt, CB_ADDSTRING, 0, (LPARAM)L"16-bit 未压缩 TIFF");
            SendMessageW(g_app.hCmbRgbFmt, CB_SETCURSEL, 0, 0);
            mk(L"STATIC", L"BPP", SS_LEFT, 870, 196, 30, 20, 0);
            g_app.hEdtJxsBpp = mk_edt(L"10.0", 902, 192, 54, 24, ID_EDT_JXS_BPP);
            create_hover_dot(hwnd, 968, 195,
                L"【RGB 倍率参考（未压缩 = 1）】\r\n"
                L"• 16-bit 未压缩 TIFF         ：1.00（~1046 MiB）\r\n"
                L"• 14-bit JPEG XS 无损        ：0.43（~450 MiB）\r\n"
                L"• 16-bit 无损 PNG            ：0.40（~420 MiB）\r\n"
                L"• 16-bit 无损 TIFF（默认）   ：0.39（~410 MiB）\r\n"
                L"• 14-bit JPEG XS 高级有损压缩：0.21（10 bpp, ~218 MiB）");

            mk(L"STATIC", L"色彩空间", SS_LEFT, 530, 235, 56, 20, 0);
            g_app.hRadCsSrgb  = mk_rad(L"sRGB",      590, 232, 64,  ID_RAD_CS_SRGB,     true);
            g_app.hRadCsAdobe = mk_rad(L"Adobe RGB", 656, 232, 96,  ID_RAD_CS_ADOBERGB, false);
            SendMessageW(g_app.hRadCsSrgb, BM_SETCHECK, BST_CHECKED, 0);
            g_app.hChkField   = mk_chk(L"输出场图",  758, 233, 78,  ID_CHK_EMIT_FIELD,  false);
            g_app.hCmbFieldMode = mk_cmb(838, 230, 118, 170, ID_CMB_FIELD_MODE);
            SendMessageW(g_app.hCmbFieldMode, CB_ADDSTRING, 0, (LPARAM)L"三张灰度");
            SendMessageW(g_app.hCmbFieldMode, CB_ADDSTRING, 0, (LPARAM)L"合并 RGB");
            SendMessageW(g_app.hCmbFieldMode, CB_ADDSTRING, 0, (LPARAM)L"仅运动/天空");
            SendMessageW(g_app.hCmbFieldMode, CB_ADDSTRING, 0, (LPARAM)L"仅超分相位");
            SendMessageW(g_app.hCmbFieldMode, CB_ADDSTRING, 0, (LPARAM)L"仅光学对焦");
            SendMessageW(g_app.hCmbFieldMode, CB_SETCURSEL, 0, 0);
            create_hover_dot(hwnd, 968, 234,
                L"【16-bit 物理场图导出】\r\n"
                L"导出与 2× 主图逐像素对齐的 16-bit 无损 PNG 物理场图（默认分离三张单通道灰度图）：\r\n"
                L"• 运动 / 天空（_motion / _sky.png）：防镂空局部动体蒙版；星野模式自动输出天空/地景分割蒙版\r\n"
                L"• 超分相位（_phase.png）：2×2 亚像素象限覆盖与有效帧数置信度（供 Sci 训练损失加权）\r\n"
                L"• 光学对焦（_focus.png）：二阶拉普拉斯合焦锐度场（亮 = 合焦主体纹理，0 = 焦外散景）\r\n"
                L"• 可切换为「合并 RGB」输出单张 R/G/B 三通道场图，或单独导出指定场图");

            // === Card 05: Engine & Parameters ===
            card_hdr(L"引擎与参数", 562, 299, 200);
            create_hover_dot(hwnd, 968, 300,
                L"【引擎、GPU 后端与微调参数】\r\n"
                L"• CLI：自动搜索同目录 / 上级 / PATH 中的 nijinLBS.exe，未找到时提示手动指定\r\n"
                L"• LibRaw：留空使用内置 0.23-HE*，可选外部 .dll 覆盖\r\n"
                L"• 后端：自动优先加载系统 vulkan-1.dll 启用 GPU 加速，无可用 GPU 时自动退回 CPU SIMD\r\n"
                L"• 旋钮：可在任意预设基础上单独微调（0 = 关闭对应滤波/迭代）");

            mk(L"STATIC", L"CLI 路径", SS_LEFT, 530, 333, 62, 20, 0);
            g_app.hEdtCliPath = mk_edt(L"", 596, 330, 276, 24, ID_EDT_CLI_PATH);
            SendMessageW(g_app.hEdtCliPath, EM_SETCUEBANNER, FALSE, (LPARAM)L"自动搜索 nijinLBS.exe");
            mk_btn(L"选择", 878, 329, 52, 26, ID_BTN_CLI_BROWSE);
            mk_btn(L"自动", 934, 329, 52, 26, ID_BTN_CLI_AUTO);

            std::wstring saved_lr = reg_read_str(L"LibRawPath");
            mk(L"STATIC", L"LibRaw", SS_LEFT, 530, 367, 62, 20, 0);
            g_app.hEdtLibRawPath = mk_edt(saved_lr.c_str(), 596, 364, 276, 24, ID_EDT_LIBRAW_PATH);
            SendMessageW(g_app.hEdtLibRawPath, EM_SETCUEBANNER, FALSE, (LPARAM)L"内置 0.23-HE*");
            mk_btn(L"选择", 878, 363, 52, 26, ID_BTN_LIBRAW_BROWSE);
            mk_btn(L"默认", 934, 363, 52, 26, ID_BTN_LIBRAW_RESET);

            mk(L"STATIC", L"亮度降噪", 0, 530, 416, 56, 20, 0); g_app.hEdtDenoise  = mk_edt(L"1.00", 588, 412, 48, 24, ID_EDT_DENOISE);
            mk(L"STATIC", L"色度降噪", 0, 646, 416, 56, 20, 0); g_app.hEdtChroma   = mk_edt(L"1.00", 704, 412, 48, 24, ID_EDT_CHROMA);
            mk(L"STATIC", L"RAW RL",   0, 762, 416, 52, 20, 0); g_app.hEdtPsfIters = mk_edt(L"4",    816, 412, 48, 24, ID_EDT_PSF_ITERS);
            mk(L"STATIC", L"RGB RL",   0, 874, 416, 52, 20, 0); g_app.hEdtRgbPsf   = mk_edt(L"2",    928, 412, 58, 24, ID_EDT_RGB_PSF_ITERS);

            mk(L"STATIC", L"PSF 半径", 0, 530, 450, 56, 20, 0); g_app.hEdtPsfSigma = mk_edt(L"1.05", 588, 446, 48, 24, ID_EDT_PSF_SIGMA);
            mk(L"STATIC", L"去鬼影",   0, 646, 450, 56, 20, 0); g_app.hEdtDeghost  = mk_edt(L"1.00", 704, 446, 48, 24, ID_EDT_DEGHOST);
            mk(L"STATIC", L"EV 偏移",  0, 762, 450, 52, 20, 0); g_app.hEdtEv       = mk_edt(L"0.0",  816, 446, 48, 24, ID_EDT_EV_OFFSET);
            mk(L"STATIC", L"每组帧数", 0, 874, 450, 52, 20, 0); g_app.hEdtGrpSz    = mk_edt(L"8",    928, 446, 58, 24, ID_EDT_GROUP_SIZE);

            g_app.hChkAutoExp = mk_chk(L"自动曝光",  530, 486, 98, ID_CHK_AUTO_EXP,    true);
            g_app.hChkRolloff = mk_chk(L"高光滚降",  646, 486, 98, ID_CHK_ROLLOFF,     true);
            g_app.hChkEdgePol = mk_chk(L"边缘抛光",  762, 486, 98, ID_CHK_EDGE_POLISH, true);

            g_app.hChkDither  = mk_chk(L"TPDF 抖动", 530, 522, 98, ID_CHK_DITHER,      true);
            mk(L"STATIC", L"后端", 0, 636, 526, 34, 20, 0);
            g_app.hCmbBackend = mk_cmb(670, 521, 152, 120, ID_CMB_BACKEND);
            SendMessageW(g_app.hCmbBackend, CB_ADDSTRING, 0, (LPARAM)L"自动 (GPU优先)");
            SendMessageW(g_app.hCmbBackend, CB_ADDSTRING, 0, (LPARAM)L"强制 GPU");
            SendMessageW(g_app.hCmbBackend, CB_ADDSTRING, 0, (LPARAM)L"纯 CPU");
            SendMessageW(g_app.hCmbBackend, CB_SETCURSEL, 0, 0);
            mk(L"STATIC", L"线程", 0, 832, 526, 34, 20, 0);
            g_app.hEdtThreads = mk_edt(L"0", 868, 521, 118, 24, ID_EDT_THREADS, ES_NUMBER);

            apply_preset_to_controls(0);

            // === Card 06: Run & Console ===
            g_app.hBtnRun    = mk_btn(L"开始合成", 34,  592, 148, 32, ID_BTN_RUN);
            SendMessageW(g_app.hBtnRun, WM_SETFONT, (WPARAM)g_app.hFontBold, TRUE);
            g_app.hBtnCancel = mk_btn(L"停止",     192, 592, 82,  32, ID_BTN_CANCEL);
            EnableWindow(g_app.hBtnCancel, FALSE);
            mk_btn(L"输出目录",                    282, 592, 98,  32, ID_BTN_OPEN_OUT);

            g_app.hPctLbl = mk(L"STATIC", L"0%", SS_LEFT, 942, 598, 46, 20, 0);
            SendMessageW(g_app.hPctLbl, WM_SETFONT, (WPARAM)g_app.hFontBold, TRUE);

            g_app.hLog = mk(L"EDIT", L"", ES_MULTILINE | ES_AUTOVSCROLL | ES_READONLY | WS_VSCROLL | WS_HSCROLL,
                            39, 635, 942, 40, ID_EDT_LOG, 0);
            SendMessageW(g_app.hLog, WM_SETFONT, (WPARAM)g_app.hFontMono, TRUE);

            update_banner();
            return 0;
        }

        case WM_DRAWITEM: {
            auto* dis = reinterpret_cast<const DRAWITEMSTRUCT*>(lParam);
            if (!dis) break;
            if (dis->CtlType == ODT_BUTTON) {
                draw_modern_pill_button(dis, dis->CtlID == ID_BTN_RUN);
                return TRUE;
            }
            break;
        }

        case WM_ERASEBKGND:
            return 1;

        case WM_PAINT: {
            PAINTSTRUCT ps{};
            HDC hdc = BeginPaint(hwnd, &ps);
            paint_studio_canvas(hwnd, hdc);
            EndPaint(hwnd, &ps);
            return 0;
        }

        case WM_PRINTCLIENT: {
            paint_studio_canvas(hwnd, (HDC)wParam);
            return 0;
        }

        case WM_CTLCOLORSTATIC: {
            HDC hdc = (HDC)wParam;
            HWND hCtrl = (HWND)lParam;
            if (hCtrl == g_app.hLog) {
                SetBkColor(hdc, kColLogBg);
                SetTextColor(hdc, kColTextMain);
                return (LRESULT)g_app.hBrLog;
            }
            SetBkColor(hdc, kColCard);
            if (hCtrl == g_app.hHdrTitle) {
                SetTextColor(hdc, kColTextMain);
            } else if (hCtrl == g_app.hBanner) {
                SetTextColor(hdc, g_app.bannerColor);
            } else if (hCtrl == g_app.hPctLbl) {
                SetTextColor(hdc, kColBadgeText);
            } else {
                SetTextColor(hdc, kColTextMain);
            }
            return (LRESULT)g_app.hBrCard;
        }

        case WM_CTLCOLOREDIT:
        case WM_CTLCOLORLISTBOX: {
            HDC hdc = (HDC)wParam;
            HWND hCtrl = (HWND)lParam;
            if (hCtrl == g_app.hLog) {
                SetBkColor(hdc, kColLogBg);
                SetTextColor(hdc, kColTextMain);
                return (LRESULT)g_app.hBrLog;
            }
            SetBkColor(hdc, RGB(255, 255, 255));
            SetTextColor(hdc, kColTextMain);
            return (LRESULT)g_app.hBrWhite;
        }

        case WM_CTLCOLORBTN: {
            HDC hdc = (HDC)wParam;
            SetBkColor(hdc, kColCard);
            return (LRESULT)g_app.hBrCard;
        }

        case WM_DROPFILES: {
            HDROP hDrop = (HDROP)wParam;
            UINT cnt = DragQueryFileW(hDrop, 0xFFFFFFFF, nullptr, 0);
            for (UINT i = 0; i < cnt; ++i) {
                UINT len = DragQueryFileW(hDrop, i, nullptr, 0);
                std::wstring p(len, L'\0');
                DragQueryFileW(hDrop, i, &p[0], len + 1);
                add_unique_input(p);
            }
            DragFinish(hDrop);
            sync_list_ui();
            return 0;
        }

        case WM_COMMAND: {
            int id = LOWORD(wParam);
            if (HIWORD(wParam) == CBN_SELCHANGE) {
                InvalidateRect((HWND)lParam, nullptr, FALSE);
                save_ui_prefs();
            }
            switch (id) {
                case ID_RAD_PRE_PHOTO: apply_preset_to_controls(0); save_ui_prefs(); return 0;
                case ID_RAD_PRE_SCI:   apply_preset_to_controls(1); save_ui_prefs(); return 0;
                case ID_RAD_PRE_ARC:   apply_preset_to_controls(2); save_ui_prefs(); return 0;
                case ID_RAD_PRE_ASTRO: apply_preset_to_controls(3); save_ui_prefs(); return 0;
                case ID_RAD_MOT_ANCHOR:
                case ID_RAD_MOT_DUALLAYER:
                case ID_RAD_MOT_CLEANPLATE:
                    g_app.custom_anchor_1based = 0;
                    SetWindowTextW(g_app.hRadMot[0], L"自动最锐");
                    save_ui_prefs();
                    return 0;
                case ID_RAD_POL_INTERACTIVE:
                case ID_RAD_POL_AUTOPRUNE:
                case ID_RAD_POL_MULTIGROUP:
                case ID_RAD_POL_FORCEALL:
                case ID_CHK_SORT_TIME:
                case ID_CHK_RECURSIVE:
                case ID_CHK_EMIT_RAW:
                case ID_CHK_EMIT_RGB:
                case ID_CHK_EMIT_FIELD:
                case ID_RAD_CS_SRGB:
                case ID_RAD_CS_ADOBERGB:
                case ID_CHK_AUTO_EXP:
                case ID_CHK_ROLLOFF:
                case ID_CHK_EDGE_POLISH:
                case ID_CHK_DITHER:
                    save_ui_prefs();
                    return 0;

                case ID_BTN_CLI_BROWSE: {
                    std::wstring p = pick_single_file_dialog(
                        hwnd, L"选择 nijinLBS.exe",
                        L"CLI Engine (nijinLBS.exe;*.exe)\0nijinLBS.exe;*.exe\0All Files (*.*)\0*.*\0"
                    );
                    if (!p.empty()) {
                        SetWindowTextW(g_app.hEdtCliPath, p.c_str());
                        reg_write_str(L"CliPath", p);
                        append_log_ui(L"[CLI] " + p);
                    }
                    return 0;
                }
                case ID_BTN_CLI_AUTO: {
                    std::wstring found = auto_detect_cli_exe(false);
                    if (!found.empty()) {
                        SetWindowTextW(g_app.hEdtCliPath, found.c_str());
                        reg_write_str(L"CliPath", found);
                        append_log_ui(L"[CLI] " + found);
                    } else {
                        prompt_user_for_cli_exe(hwnd);
                    }
                    return 0;
                }
                case ID_BTN_LIBRAW_BROWSE: {
                    std::wstring p = pick_single_file_dialog(
                        hwnd, L"选择 LibRaw 动态库 (.dll)",
                        L"LibRaw DLL (*.dll)\0*.dll\0All Files (*.*)\0*.*\0"
                    );
                    if (!p.empty()) {
                        SetWindowTextW(g_app.hEdtLibRawPath, p.c_str());
                        reg_write_str(L"LibRawPath", p);
                        append_log_ui(L"[LibRaw] " + p);
                    }
                    return 0;
                }
                case ID_BTN_LIBRAW_RESET: {
                    SetWindowTextW(g_app.hEdtLibRawPath, L"");
                    reg_write_str(L"LibRawPath", L"");
                    append_log_ui(L"[LibRaw] Built-in 0.23-HE*");
                    return 0;
                }

                case ID_BTN_ADD_FILES:
                    pick_raw_files_dialog(hwnd);
                    return 0;
                case ID_BTN_ADD_FOLDER: {
                    std::wstring d = pick_folder_dialog(hwnd, L"选择待扫描目录");
                    if (!d.empty()) { add_unique_input(d); sync_list_ui(); }
                    return 0;
                }
                case ID_BTN_REMOVE: {
                    int cnt = (int)SendMessageW(g_app.hList, LB_GETSELCOUNT, 0, 0);
                    if (cnt > 0) {
                        std::vector<int> sel(cnt);
                        SendMessageW(g_app.hList, LB_GETSELITEMS, cnt, (LPARAM)sel.data());
                        for (int k = cnt - 1; k >= 0; --k) {
                            if (sel[k] >= 0 && sel[k] < (int)g_app.inputs.size())
                                g_app.inputs.erase(g_app.inputs.begin() + sel[k]);
                        }
                        sync_list_ui();
                    }
                    return 0;
                }
                case ID_BTN_CLEAR:
                    g_app.inputs.clear();
                    sync_list_ui();
                    return 0;
                case ID_BTN_OUT_DIR: {
                    std::wstring d = pick_folder_dialog(hwnd, L"选择输出目录");
                    if (!d.empty()) SetWindowTextW(g_app.hOutDir, d.c_str());
                    return 0;
                }
                case ID_BTN_INSPECT:
                    start_job(true);
                    return 0;
                case ID_BTN_RUN:
                    start_job(false);
                    return 0;
                case ID_BTN_CANCEL: {
                    std::lock_guard<std::mutex> lk(g_app.proc_mtx);
                    if (g_app.hChildProc) {
                        TerminateProcess(g_app.hChildProc, 130);
                        append_log_ui(L"[Stopped]");
                    }
                    return 0;
                }
                case ID_BTN_OPEN_OUT: {
                    std::wstring d = trim_ws(get_wnd_text(g_app.hOutDir));
                    if (!d.empty()) ShellExecuteW(hwnd, L"open", d.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
                    return 0;
                }
            }
            break;
        }

        case WM_APP_LOG: {
            auto* s = reinterpret_cast<std::wstring*>(lParam);
            if (s) { append_log_ui(*s); delete s; }
            return 0;
        }
        case WM_APP_PROGRESS: {
            set_prog_pct((int)wParam);
            return 0;
        }
        case WM_APP_DONE: {
            int rc = (int)wParam;
            set_running_ui(false);
            if (rc == 0) set_prog_pct(100);
            append_log_ui(L"[Exit " + std::to_wstring(rc) + L"]");
            return 0;
        }
        case WM_CLOSE: {
            if (g_app.running.load()) {
                if (MessageBoxW(hwnd, L"任务仍在运行，确认停止并退出？", L"退出确认", MB_YESNO | MB_ICONQUESTION) != IDYES) return 0;
                std::lock_guard<std::mutex> lk(g_app.proc_mtx);
                if (g_app.hChildProc) TerminateProcess(g_app.hChildProc, 130);
            }
            save_ui_prefs();
            if (g_app.worker.joinable()) g_app.worker.join();
            DestroyWindow(hwnd);
            return 0;
        }
        case WM_DESTROY:
            if (g_app.hFont) DeleteObject(g_app.hFont);
            if (g_app.hFontBold) DeleteObject(g_app.hFontBold);
            if (g_app.hFontCardHdr) DeleteObject(g_app.hFontCardHdr);
            if (g_app.hFontTitle) DeleteObject(g_app.hFontTitle);
            if (g_app.hFontBadge) DeleteObject(g_app.hFontBadge);
            if (g_app.hFontMono) DeleteObject(g_app.hFontMono);
            if (g_app.hBrCanvas) DeleteObject(g_app.hBrCanvas);
            if (g_app.hBrCard) DeleteObject(g_app.hBrCard);
            if (g_app.hBrLog) DeleteObject(g_app.hBrLog);
            if (g_app.hBrWhite) DeleteObject(g_app.hBrWhite);
            if (g_app.hBmpGx) DeleteObject(g_app.hBmpGx);
            if (g_app.hBmpNijin) DeleteObject(g_app.hBmpNijin);
            PostQuitMessage(0);
            return 0;
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

} // namespace

int WINAPI wWinMain(HINSTANCE hInst, HINSTANCE, PWSTR, int nCmdShow) {
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    Gdiplus::GdiplusStartupInput gdip_in;
    ULONG_PTR gdip_tok = 0;
    Gdiplus::GdiplusStartup(&gdip_tok, &gdip_in, nullptr);
    INITCOMMONCONTROLSEX icc{sizeof(icc), ICC_WIN95_CLASSES | ICC_STANDARD_CLASSES | ICC_PROGRESS_CLASS};
    InitCommonControlsEx(&icc);

    WNDCLASSEXW dc{};
    dc.cbSize = sizeof(dc);
    dc.lpfnWndProc = HoverDotWndProc;
    dc.hInstance = hInst;
    dc.hCursor = LoadCursorW(nullptr, IDC_HAND);
    dc.lpszClassName = L"NijiLbsHoverDot";
    RegisterClassExW(&dc);

    HICON hIconBig   = (HICON)LoadImageW(hInst, MAKEINTRESOURCEW(IDI_APPICON), IMAGE_ICON, 32, 32, LR_DEFAULTCOLOR);
    HICON hIconSmall = (HICON)LoadImageW(hInst, MAKEINTRESOURCEW(IDI_APPICON), IMAGE_ICON, 16, 16, LR_DEFAULTCOLOR);
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
    wc.lpszClassName = L"NijiNLiteBurstScalerGui";
    RegisterClassExW(&wc);

    HDC hdc = GetDC(nullptr);
    int dpi = GetDeviceCaps(hdc, LOGPIXELSY);
    ReleaseDC(nullptr, hdc);
    RECT rc{0, 0, MulDiv(1020, dpi, 96), MulDiv(706, dpi, 96)};
    DWORD style = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX;
    AdjustWindowRect(&rc, style, FALSE);

    HWND hwnd = CreateWindowExW(
        0, wc.lpszClassName, L"NijiN.LiteBurstScaler (nijinLBS)",
        style, CW_USEDEFAULT, CW_USEDEFAULT, rc.right - rc.left, rc.bottom - rc.top,
        nullptr, nullptr, hInst, nullptr
    );

    int wargc = 0;
    std::wstring shot_path;
    bool test_prefs = false;
    LPWSTR* wargv = CommandLineToArgvW(GetCommandLineW(), &wargc);
    if (wargv) {
        for (int i = 1; i < wargc; ++i) {
            std::wstring a = wargv[i];
            if (a == L"--screenshot" && i + 1 < wargc) shot_path = wargv[++i];
            else if (a == L"--test-prefs") test_prefs = true;
            else if (!a.empty() && a[0] != L'-') add_unique_input(a);
        }
        LocalFree(wargv);
        if (!g_app.inputs.empty()) sync_list_ui();
    }
    g_app.is_screenshot_mode = !shot_path.empty();
    if (!g_app.is_screenshot_mode && !test_prefs) {
        load_ui_prefs();
    }
    if (test_prefs) {
        wchar_t cmb_lbl[128] = {0}, fm_lbl[128] = {0};
        SendMessageW(g_app.hCmbRgbFmt, CB_GETLBTEXT, 0, reinterpret_cast<LPARAM>(cmb_lbl));
        SendMessageW(g_app.hCmbFieldMode, CB_GETLBTEXT, 0, reinterpret_cast<LPARAM>(fm_lbl));
        bool default_lossless_ok = (SendMessageW(g_app.hCmbRawFmt, CB_GETCURSEL, 0, 0) == 0) &&
                                   (SendMessageW(g_app.hCmbRgbFmt, CB_GETCURSEL, 0, 0) == 0) &&
                                   (std::wstring(cmb_lbl) == L"16-bit 无损 TIFF") &&
                                   (SendMessageW(g_app.hCmbFieldMode, CB_GETCURSEL, 0, 0) == 0) &&
                                   (std::wstring(fm_lbl) == L"三张灰度");
        SendMessageW(g_app.hCmbRawFmt, CB_SETCURSEL, 1, 0);
        SendMessageW(g_app.hCmbRgbFmt, CB_SETCURSEL, 2, 0);
        SendMessageW(g_app.hCmbFieldMode, CB_SETCURSEL, 1, 0);
        SetWindowTextW(g_app.hEdtJxsBpp, L"12.5");
        SendMessageW(g_app.hRadCsSrgb,  BM_SETCHECK, BST_UNCHECKED, 0);
        SendMessageW(g_app.hRadCsAdobe, BM_SETCHECK, BST_CHECKED, 0);
        SendMessageW(g_app.hChkField,   BM_SETCHECK, BST_CHECKED, 0);
        apply_preset_to_controls(3);
        save_ui_prefs();

        SendMessageW(g_app.hCmbRawFmt, CB_SETCURSEL, 0, 0);
        SendMessageW(g_app.hCmbRgbFmt, CB_SETCURSEL, 0, 0);
        SendMessageW(g_app.hCmbFieldMode, CB_SETCURSEL, 0, 0);
        SetWindowTextW(g_app.hEdtJxsBpp, L"10.0");
        SendMessageW(g_app.hRadCsSrgb,  BM_SETCHECK, BST_CHECKED, 0);
        SendMessageW(g_app.hRadCsAdobe, BM_SETCHECK, BST_UNCHECKED, 0);
        SendMessageW(g_app.hChkField,   BM_SETCHECK, BST_UNCHECKED, 0);
        apply_preset_to_controls(0);
        load_ui_prefs();

        bool ok = default_lossless_ok &&
                  (SendMessageW(g_app.hCmbRawFmt, CB_GETCURSEL, 0, 0) == 1) &&
                  (SendMessageW(g_app.hCmbRgbFmt, CB_GETCURSEL, 0, 0) == 2) &&
                  (SendMessageW(g_app.hCmbFieldMode, CB_GETCURSEL, 0, 0) == 1) &&
                  (get_wnd_text(g_app.hEdtJxsBpp) == L"12.5") &&
                  (SendMessageW(g_app.hRadCsAdobe, BM_GETCHECK, 0, 0) == BST_CHECKED) &&
                  (SendMessageW(g_app.hChkField, BM_GETCHECK, 0, 0) == BST_CHECKED) &&
                  (SendMessageW(g_app.hRadPre[3], BM_GETCHECK, 0, 0) == BST_CHECKED);
        reg_write_str(L"HasSavedUiPrefs", L"");
        DestroyWindow(hwnd);
        Gdiplus::GdiplusShutdown(gdip_tok);
        CoUninitialize();
        return ok ? 0 : 1;
    }

    std::wstring det_cli = auto_detect_cli_exe(true);
    std::wstring saved_lr = reg_read_str(L"LibRawPath");
    if (!det_cli.empty()) {
        std::wstring shown_cli = !shot_path.empty() ? L".\\nijinLBS.exe" : det_cli;
        SetWindowTextW(g_app.hEdtCliPath, shown_cli.c_str());
        append_log_ui(L"Ready | CLI: " + shown_cli + L" | LibRaw: " +
                      (saved_lr.empty() ? L"Built-in 0.23-HE*" : saved_lr));
    } else {
        append_log_ui(L"CLI auto-search missed; waiting for custom path.");
    }

    ShowWindow(hwnd, nCmdShow);
    UpdateWindow(hwnd);

    if (det_cli.empty() && shot_path.empty()) {
        prompt_user_for_cli_exe(hwnd);
    }

    if (!shot_path.empty()) {
        MSG pm{};
        while (PeekMessageW(&pm, nullptr, 0, 0, PM_REMOVE)) {
            TranslateMessage(&pm);
            DispatchMessageW(&pm);
        }
        RECT wrc{};
        GetWindowRect(hwnd, &wrc);
        int ww = wrc.right - wrc.left, wh = wrc.bottom - wrc.top;
        HDC hScreen = GetDC(nullptr);
        HDC hMem = CreateCompatibleDC(hScreen);
        BITMAPINFO bmi{};
        bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        bmi.bmiHeader.biWidth = ww;
        bmi.bmiHeader.biHeight = wh;
        bmi.bmiHeader.biPlanes = 1;
        bmi.bmiHeader.biBitCount = 24;
        bmi.bmiHeader.biCompression = BI_RGB;
        void* bits = nullptr;
        HBITMAP hBmp = CreateDIBSection(hScreen, &bmi, DIB_RGB_COLORS, &bits, nullptr, 0);
        HGDIOBJ old = SelectObject(hMem, hBmp);
        SendMessageW(hwnd, WM_PRINT, (WPARAM)hMem,
                     PRF_NONCLIENT | PRF_CLIENT | PRF_CHILDREN | PRF_ERASEBKGND);
        GdiFlush();
        DWORD row_stride = ((ww * 3 + 3) & ~3);
        DWORD img_sz = row_stride * wh;
        BITMAPFILEHEADER bfh{};
        bfh.bfType = 0x4D42;
        bfh.bfOffBits = sizeof(BITMAPFILEHEADER) + sizeof(BITMAPINFOHEADER);
        bfh.bfSize = bfh.bfOffBits + img_sz;
        FILE* fp = _wfopen(shot_path.c_str(), L"wb");
        if (fp) {
            std::fwrite(&bfh, 1, sizeof(bfh), fp);
            std::fwrite(&bmi.bmiHeader, 1, sizeof(bmi.bmiHeader), fp);
            std::fwrite(bits, 1, img_sz, fp);
            std::fclose(fp);
        }
        SelectObject(hMem, old);
        DeleteObject(hBmp);
        DeleteDC(hMem);
        ReleaseDC(nullptr, hScreen);
        DestroyWindow(hwnd);
        Gdiplus::GdiplusShutdown(gdip_tok);
        CoUninitialize();
        return 0;
    }

    MSG m{};
    while (GetMessageW(&m, nullptr, 0, 0) > 0) {
        TranslateMessage(&m);
        DispatchMessageW(&m);
    }
    Gdiplus::GdiplusShutdown(gdip_tok);
    CoUninitialize();
    return 0;
}
