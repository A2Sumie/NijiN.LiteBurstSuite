// heraw_gui_i18n.hpp -- External .lng Localization & Session Diagnostic Log for NijiN.LiteBurstSuite
#pragma once

#include <algorithm>
#include <cctype>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

#ifdef __OBJC__
#import <Foundation/Foundation.h>
#endif

#include "heraw_fs_utf8.hpp"
#include "heraw_version.hpp"

namespace heraw_i18n {

struct LangPack {
    std::string code;
    std::string display_name;
    std::string source_path;
    std::unordered_map<std::string, std::string> strings;
};

inline std::string trim_ascii(const std::string& s) {
    size_t a = 0, b = s.size();
    while (a < b && (unsigned char)s[a] <= 0x20) ++a;
    while (b > a && (unsigned char)s[b - 1] <= 0x20) --b;
    return s.substr(a, b - a);
}

inline std::string unescape_lng_val(const std::string& raw) {
    std::string s = raw;
    if (s.size() >= 2 && s.front() == '"' && s.back() == '"') {
        s = s.substr(1, s.size() - 2);
    }
    std::string out;
    out.reserve(s.size());
    for (size_t i = 0; i < s.size(); ++i) {
        if (s[i] == '\\' && i + 1 < s.size()) {
            char nx = s[++i];
            if (nx == 'n') out.push_back('\n');
            else if (nx == 'r') out.push_back('\r');
            else if (nx == 't') out.push_back('\t');
            else out.push_back(nx);
        } else {
            out.push_back(s[i]);
        }
    }
    return out;
}

inline void parse_lng_content(const std::string& content, LangPack& pack) {
    size_t pos = 0;
    if (content.size() >= 3 &&
        (unsigned char)content[0] == 0xEF &&
        (unsigned char)content[1] == 0xBB &&
        (unsigned char)content[2] == 0xBF) {
        pos = 3;
    }
    while (pos < content.size()) {
        size_t eol = content.find('\n', pos);
        std::string line = (eol == std::string::npos) ? content.substr(pos) : content.substr(pos, eol - pos);
        pos = (eol == std::string::npos) ? content.size() : (eol + 1);
        if (!line.empty() && line.back() == '\r') line.pop_back();

        std::string trimmed = trim_ascii(line);
        if (trimmed.empty() || trimmed[0] == '#' || trimmed[0] == ';') continue;
        size_t eq = trimmed.find('=');
        if (eq == std::string::npos) continue;
        std::string key = trim_ascii(trimmed.substr(0, eq));
        std::string val = unescape_lng_val(trim_ascii(trimmed.substr(eq + 1)));
        if (key.empty()) continue;
        if (key == "meta.code" && !val.empty()) {
            pack.code = val;
        } else if (key == "meta.name" && !val.empty()) {
            pack.display_name = val;
        } else {
            pack.strings[key] = val;
        }
    }
}

inline void populate_builtin_zh_cn(LangPack& p) {
    p.code = "zh-CN";
    p.display_name = "简体中文 (zh-CN)";
    p.strings = {
        {"app.window_title", "NijiN.LiteBurstSuite " NIJIN_VERSION " (nijinLBS)"},
        {"hdr.perf_fmt", "性能: %s · %s ▾"},
        {"hdr.perf_tip_mac", "性能、语言与硬件加速菜单 (显卡加速 / CPU核数 / 导出日志 ⌘L / 引擎设置 ⌘,)"},
        {"hdr.perf_tip_win", "性能、语言与硬件加速菜单 (显卡加速 / CPU核数 / 导出日志 Ctrl+L / 引擎设置)"},
        {"hdr.gpu_auto", "GPU优先"},
        {"hdr.gpu_force", "强制GPU"},
        {"hdr.gpu_cpu", "纯CPU"},
        {"hdr.gpu_off", "默认CPU"},
        {"hdr.cores_all_fmt", "全%d核"},
        {"hdr.cores_n_fmt", "%d核"},
        {"hdr.cores_perf", "大核"},
        {"hdr.cores_phys", "物理核"},

        {"menu.perf_settings", "性能与引擎设置..."},
        {"menu.export_log", "导出诊断日志..."},
        {"menu.language", "界面语言 (Language)"},
        {"menu.hide_app", "隐藏 nijinLBS"},
        {"menu.hide_others", "隐藏其他"},
        {"menu.close_win", "关闭窗口"},
        {"menu.quit_app", "退出 nijinLBS"},
        {"menu.file", "文件"},
        {"menu.add_raw_or_dir", "添加 RAW 文件或目录..."},
        {"menu.edit", "编辑"},
        {"menu.undo", "撤销"},
        {"menu.redo", "重做"},
        {"menu.cut", "剪切"},
        {"menu.copy", "拷贝"},
        {"menu.paste", "粘贴"},
        {"menu.select_all", "全选"},
        {"menu.merge_groups", "合并所选连拍组"},
        {"menu.split_frames", "将所选帧拆分为新组"},
        {"menu.clean_stray_fmt", "清理少帧散片 (<%d张)"},
        {"menu.perf", "性能"},
        {"menu.hw_detected_fmt", "已识别硬件: %s"},
        {"menu.hw_hybrid_mac_fmt", "%d大核+%d小核 (共%d核)"},
        {"menu.hw_cores_fmt", "%d核"},
        {"menu.metal_hdr", "Metal 显卡加速"},
        {"menu.metal_auto", "自动 (Metal GPU优先)"},
        {"menu.metal_force", "强制 Metal GPU"},
        {"menu.metal_cpu", "纯 CPU (NEON缓存加速)"},
        {"menu.d3d_hdr", "Vulkan 显卡加速"},
        {"menu.d3d_auto", "自动选择 (独显优先)"},
        {"menu.d3d_force", "强制 Vulkan GPU"},
        {"menu.d3d_cpu", "纯 CPU (AVX2缓存加速)"},
        {"menu.d3d_adapter_hdr", "GPU 适配器选择"},
        {"menu.d3d_adapter_auto", "自动优选独立显卡"},
        {"menu.cpu_budget_hdr", "CPU 核心数量预算"},
        {"menu.core_all_fmt", "自动全核 (%d核)"},
        {"menu.core_perf_fmt", "性能核数 (%d核)"},
        {"menu.core_main_fmt", "主力核数 (%d核)"},
        {"menu.core_phys_fmt", "物理核数 (%d核, 关SMT)"},
        {"menu.core_half_fmt", "半数核心 (%d核)"},
        {"menu.core_custom", "自定义核数"},
        {"menu.core_custom_n_fmt", "自定义核数 (%d核)..."},
        {"menu.core_custom_engine", "自定义核数 / 引擎配置..."},
        {"menu.power_hdr", "电源与移动设备策略"},
        {"menu.battery_auto_on", "自动检测 (当前识别为电池设备)"},
        {"menu.battery_auto_off", "自动检测 (当前识别为台式供电)"},
        {"menu.battery_force_on", "按电池移动设备调度 (默认物理核)"},
        {"menu.battery_force_off", "按桌面持续供电调度 (默认全核)"},
        {"menu.prio_hdr", "后台进程优先级"},
        {"menu.prio_normal", "标准优先级 (Normal)"},
        {"menu.prio_above", "高性能优先"},
        {"menu.prio_below", "低于标准 (Below Normal)"},
        {"menu.prio_idle", "后台空闲 (Idle)"},

        {"qmenu.toggle_multi", "切换所选组勾选状态 (Space)"},
        {"qmenu.toggle_one", "切换当前组勾选状态 (Space)"},
        {"qmenu.only_best", "仅勾选全库最优 1 组"},
        {"qmenu.check_valid_fmt", "勾选全部可合成组 (≥%d张)"},
        {"qmenu.select_all", "全选所有队列项"},
        {"qmenu.merge", "合并所选组为同一组"},
        {"qmenu.clean_fmt", "清理少帧与单拍散片 (<%d张)"},
        {"qmenu.remove_multi", "移除所选连拍组"},
        {"qmenu.remove_one", "移除当前连拍组"},

        {"fmenu.toggle_multi", "启用 / 跳过所选帧 (Space)"},
        {"fmenu.toggle_one", "启用 / 跳过此帧 (Space)"},
        {"fmenu.keep_only", "仅保留所选帧 (跳过其余帧)"},
        {"fmenu.restore_all", "恢复启用本组全部有效帧"},
        {"fmenu.lock_anchor_fmt", "锁定 #%d 为姿态定格帧"},
        {"fmenu.split", "将所选帧拆出为新连拍组"},

        {"dock.queue_hdr", "连拍队列"},
        {"dock.sort_time", "EXIF时间排序"},
        {"dock.recursive", "递归子目录"},
        {"dock.grp_auto", "自动分组"},
        {"dock.grp_single", "整组处理"},
        {"dock.grp_best", "每簇精选"},
        {"dock.grp_top1", "全库挑1组"},
        {"dock.grp_tip", "自动分组：自适应快门/节奏并按谷点均衡拆组\n整组处理：所有导入帧合为 1 组\n每簇精选：每段连拍挑最优相位帧合为 1 组\n全库挑1组：大资料库自动勾选画质最优的 1 组"},
        {"dock.grp_sz_cap", "目标张数"},
        {"dock.grp_sz_tip_fmt", "自动分组或每簇精选的目标张数（当前模式至少 %d 张）。"},
        {"dock.regroup", "重新分组"},
        {"dock.well_scanning", "正在静默剖析连拍簇与 AF 对焦点..."},
        {"dock.well_idle", "拖入 RAW 连拍或文件夹，或点击此处选择"},
        {"dock.btn_add_files", "+文件"},
        {"dock.btn_add_dir", "+目录"},
        {"dock.btn_merge", "合并组"},
        {"dock.btn_clean", "清散片"},
        {"dock.btn_topk", "Top-K选片"},
        {"dock.btn_clear", "清空"},
        {"dock.tip_merge", "将多选的多个连拍组合并为同一组"},
        {"dock.tip_clean_fmt", "一键移除队列中不足 %d 张的单拍与散片"},
        {"dock.tip_topk", "按 AF 对焦点锐度直接筛出 Top-3 原片拷贝"},
        {"dock.insp_hdr", "组内单帧检视"},
        {"dock.insp_hdr_multi_fmt", "组 %02d 检视 (已选%d组)"},
        {"dock.insp_hdr_stray", "单拍散片检视"},
        {"dock.insp_hdr_group_fmt", "组 %02d 单帧检视"},
        {"dock.insp_hint_mac", "⌘/⇧多选 · 空格启停 · 点●定格"},
        {"dock.insp_hint_win", "Ctrl/Shift多选 · 空格启停"},
        {"dock.btn_split", "拆为新组"},
        {"dock.btn_lock_win", "定格此帧"},
        {"dock.tip_split", "将下方选中的帧拆分为独立的新连拍组"},
        {"dock.tip_anchor_dot", "点击锁定此帧为姿态定格锚点"},
        {"dock.insp_empty", "选择连拍组后在此内联检视逐帧 AF 锐度、位移与微震标记"},

        {"qcell.stray_title_fmt", "单拍散片 · %d张 (已折叠)  %s"},
        {"qcell.stray_badge", "[散片]"},
        {"qcell.stray_model", "独立单拍已折叠 · 可拆出或一键清理"},
        {"qcell.raw_seq_model", "RAW 连拍序列"},
        {"qcell.short_cnt_fmt", "%d张 (少帧)"},
        {"qcell.norm_cnt_fmt", "%d张"},
        {"qcell.group_title_fmt", "组 %02d · %s  %s"},
        {"qcell.badge_global", "[全域]"},
        {"qcell.pill_astro", "✦ 套用星空配置"},
        {"qcell.pill_sci", "✦ 套用科学配置"},
        {"qcell.pill_arc", "✦ 套用归档配置"},

        {"ftag.dark", "黑场"},
        {"ftag.blur", "动糊"},
        {"ftag.exp", "曝光"},
        {"ftag.shock", "微震"},
        {"ftag.anchor", "定格"},

        {"hint.dirty", "设置已修改，按回车或点击“重新分组”更新队列预览。"},
        {"hint.min_grp_sz_fmt", "当前模式目标张数至少为 %d，请修改后重新分组。"},
        {"hint.idle_mac", "导入后自动剖析连拍簇（支持 ⌘/⇧ 多选、空格批勾、合并与拆分）"},
        {"hint.idle_win", "导入后自动剖析连拍簇（支持 Ctrl/Shift 多选、空格批勾、合并与拆分）"},
        {"hint.summary_stray_fmt", "%d 张 → %d 组连拍（%s），另 %d 张单拍/少帧已折叠"},
        {"hint.summary_all_fmt", "%d 张 → %d 组（%s）"},
        {"hint.mode_single_mac", "整组处理：全部合为 1 组；可在下方 ⌘/⇧ 多选帧或拆出新组。"},
        {"hint.mode_single_win", "整组处理：全部合为 1 组；可在下方 Ctrl/Shift 多选帧或拆出新组。"},
        {"hint.mode_best_fmt", "每簇精选：每段连拍自动挑最多 %s 张相位互补最锐帧合为 1 组。"},
        {"hint.mode_top1", "全库挑1组：自动勾选全库综合画质最优的 1 组连拍。"},
        {"hint.mode_auto_short_fmt", "当前组不足 %d 张门槛；可切换 1× 真采样 (≥3张) 或点右侧启用融合。"},
        {"hint.mode_auto_ok_fmt", "自适应快门/节奏分簇（目标 %s 张，门槛 ≥%d 张）；支持多选合并或清散片。"},

        {"pill.check_n_fmt", "✦ 勾选 %d 组"},
        {"pill.fallback_fuse", "✦ 启用强制融合"},
        {"pill.rx_astro", "✦ 套用星空处方"},
        {"pill.rx_sci", "✦ 套用科学处方"},
        {"pill.rx_arc", "✦ 套用归档处方"},
        {"pill.only_best", "✦ 仅勾最优1组"},
        {"pill.check_all_fmt", "✦ 全勾 %d 组"},

        {"tab.1", "1 显影与时空"},
        {"tab.2", "2 光学与散景"},
        {"tab.3", "3 计量与科学场"},
        {"tab.4", "4 镜头与机身"},

        {"t1.c1_title", "场景与时间模式"},
        {"t1.portal_quad_fmt", "四象限 %d/4 · 体检 ▸"},
        {"t1.portal_quad_idle", "四象限 --/4 · 体检 ▸"},
        {"t1.preset_cap", "预设模式"},
        {"t1.preset_photo", "照片"},
        {"t1.preset_astro", "星空"},
        {"t1.preset_sci", "科学"},
        {"t1.preset_arc", "归档"},
        {"t1.motion_cap", "动体处理"},
        {"t1.mot_auto", "自动最锐"},
        {"t1.mot_first", "首帧"},
        {"t1.mot_middle", "中间"},
        {"t1.mot_clean", "空镜(去人)"},
        {"t1.mot_vnd", "慢门(流水)"},
        {"t1.portal_strobe_on", "• 频闪已启 ▸"},
        {"t1.portal_strobe_idle", "频闪/光场 ▸"},
        {"t1.olpf_cap", "光学匹配"},
        {"t1.olpf_auto", "自动识别传感器低通"},
        {"t1.olpf_none", "无低通椭圆核"},
        {"t1.olpf_weak4", "4点双折射弱低通核"},
        {"t1.olpf_gauss16", "16点高斯低通核"},

        {"t1.c2_title", "输出与快捷附加层"},
        {"t1.c2_sub", "与专页标签双向实时同步"},
        {"t1.outdir_cap", "输出目录"},
        {"t1.outdir_ph", "默认同目录 nijinLBS_out"},
        {"t1.browse", "浏览"},
        {"t1.emit_raw", "2× RGGB 底片"},
        {"t1.raw_dng", "DNG 16-bit 无损"},
        {"t1.raw_linear", "DNG 三通道线性 (Capture One 兼容)"},
        {"t1.raw_fp16", "DNG 16-bit 半精度浮点 (0.29×)"},
        {"t1.raw_hq", "DNG 高级有损压缩 (0.09×)"},
        {"t1.raw_uncomp", "DNG 16-bit 无压缩 (1.00×)"},
        {"t1.relay_raw", "外部降噪直通"},
        {"t1.no_tool_exif", "不在 EXIF 内写入本工具信息"},
        {"t1.no_tool_exif_tip", "开启后，导出的 DNG / TIFF / PNG / JPEG XS 仅保留相机原始 EXIF，不写入 ProcessingSoftware (Tag 11) 与本工具名称及参数。"},
        {"t1.emit_rgb", "RGB 成片"},
        {"t1.scale_2x", "2×超分"},
        {"t1.scale_1x", "1×真彩"},
        {"t1.scale_both", "双出"},
        {"t1.scale_tip", "2×超分：标准 2× 亚像素超分 (门槛 ≥5 张)\n1×真彩：原生 1× 16-bit 真采样色彩与噪声模型强化 (门槛 ≥3 张)\n双出：单次重建同步导出 2× 与 1× 16-bit RGB"},
        {"t1.rgb_tiff", "16-bit 无损 TIFF"},
        {"t1.rgb_tiff_hq", "16-bit 视觉无损 TIFF (HQ, 0.48×)"},
        {"t1.rgb_png", "16-bit 无损 PNG (0.40×)"},
        {"t1.rgb_jxs", "14-bit JPEG XS 有损 (0.21×)"},
        {"t1.rgb_jxs_lossless", "14-bit JPEG XS 无损 (0.43×)"},
        {"t1.rgb_tiff_raw", "16-bit 无压缩 TIFF (1.00×)"},
        {"t1.emit_field", "物理场图"},
        {"t1.fld_tiff_alpha", "内嵌至主TIFF通道"},
        {"t1.fld_split", "独立16-bit灰度PNG"},
        {"t1.fld_rgb", "合并三通道PNG"},
        {"t1.portal_fields_fmt", "选场(%d/%d)·数据集 ▸"},
        {"t1.emit_trans", "瞬态高光回收层 (流星 / 萤火虫 / 浪花)"},
        {"t1.portal_refl_on", "• 消反光/阈值 ▸"},
        {"t1.portal_refl_idle", "消反光/阈值 ▸"},

        {"t1.c3_title", "重建与新噪声态精调"},
        {"t1.reset_preset", "恢复预设"},
        {"t1.denoise", "亮度互谱降噪"},
        {"t1.chroma", "色空KLT强度"},
        {"t1.deghost", "去鬼影严格度"},
        {"t1.psf_iters", "光学RL迭代"},
        {"t1.psf_sigma", "PSF半径"},
        {"t1.ev", "手动曝光EV"},
        {"t1.silicon_clean", "硅片条带自扣除"},
        {"t1.ibp", "紧支集IBP回投"},
        {"t1.auto_lca", "盲色差归位"},
        {"t1.auto_exp", "自动曝光"},
        {"t1.rolloff", "高光滚降"},
        {"t1.dither", "TPDF抖动"},
        {"t1.backend_cap", "显卡加速"},
        {"t1.bk_auto", "自动(GPU优先)"},
        {"t1.bk_gpu_mac", "强制Metal GPU"},
        {"t1.bk_gpu_win", "强制Vulkan GPU"},
        {"t1.bk_cpu", "纯CPU"},
        {"t1.threads_cap", "CPU核数"},
        {"t1.threads_tip", "CPU 工作核数 (0 = 自动分配)"},
        {"t1.win_gpu_cap", "加速"},
        {"t1.win_cores_cap", "核分配"},
        {"t1.win_prio_cap", "优先级"},
        {"t1.win_thr_cap", "线程"},
        {"t1.engine_btn", "性能与引擎..."},
        {"t1.engine_strip_fmt", "CPU: %s  ·  引擎: %s (%s)"},
        {"t1.engine_select_cli", "请选择 nijinLBS 引擎"},
        {"t1.engine_ext_lr_fmt", "外部 %s"},
        {"t1.engine_builtin_lr", "内置 nijinWVDC + LibRaw 0.23-HE*"},

        {"t2.c1_title", "时间轴与动体合成模式 (与首页「动体处理」双向同步)"},
        {"t2.freeze", "标准姿态定格"},
        {"t2.pose_auto", "自动最锐(AF)"},
        {"t2.pose_first", "首帧"},
        {"t2.pose_middle", "中间帧"},
        {"t2.pose_follow", "跟随左栏指定帧"},
        {"t2.pose_locked_fmt", "指定帧 #%d"},
        {"t2.clean", "零路人纯净空镜 (Clean Plate)"},
        {"t2.clean_sens", "行人剔除灵敏度"},
        {"t2.vnd", "手持虚拟慢门流水 (Virtual ND)"},
        {"t2.vnd_thresh", "流体识别阈值"},
        {"t2.strobe", "运动频闪分身轨迹 (Strobe)"},
        {"t2.strobe_step", "抽帧步长"},
        {"t2.step_1", "每1帧"},
        {"t2.step_2", "每2帧"},
        {"t2.step_3", "每3帧"},
        {"t2.step_4", "每4帧"},
        {"t2.step_5", "每5帧"},
        {"t2.step_6", "每6帧"},
        {"t2.fade_solid", "等权实影"},
        {"t2.fade_trail", "渐隐残影"},
        {"t2.win_fade_cap", "轨迹衰减"},
        {"t2.win_color_cap", "色彩模式"},
        {"t2.c2_title", "附加分离图层提取 (可与主图同步导出)"},
        {"t2.emit_trans", "剥离瞬态高光回收层 (流星 / 萤火虫 / 飞溅浪花)"},
        {"t2.trans_sigma", "触发门限 (σ)"},
        {"t2.trans_rgb", "全彩层"},
        {"t2.trans_gray", "灰度层"},
        {"t2.refl", "镜面碎波光压制 / 展柜消反光分离"},
        {"t2.refl_glint", "原地碎高光压制"},
        {"t2.refl_split", "宽基线双层消反光"},
        {"t2.drift_ok_fmt", "└─ 实测 2× 位移跨度: %.1f px (已达 >= 40 px 宽基线双层分离条件)"},
        {"t2.drift_low_fmt", "└─ 实测 2× 位移跨度: %.1f px (碎高光压制可用；整窗双层消反光需 >= 40 px)"},
        {"t2.drift_idle", "└─ 实测位移跨度: 待导入连拍组 (整窗双层消反光需横向平移 >= 40 px)"},
        {"t2.c3_title", "三维微基线光场与线性光子域散景重对焦"},
        {"t2.para_idle", "视差待测 · 可先选择输出，执行时回显条件"},
        {"t2.para_est_fmt", "预估视差 %.1f px · 执行时确认深度条件"},
        {"t2.para_ok_fmt", "实测视差 %.1f px · 深度条件满足"},
        {"t2.para_low_fmt", "实测视差 %.1f px · 当前深度条件不足"},
        {"t2.para_pend_fmt", "实测视差 %.1f px · 深度条件待确认"},
        {"t2.export_para", "导出 2.5D 交互光场包 (.html)"},
        {"t2.para_amp", "视角环绕幅度"},
        {"t2.export_refocus", "额外渲染线性光子域散景重对焦图"},
        {"t2.refocus_pos", "合焦平面偏移"},
        {"t2.bokeh_str", "散景光圈强度"},

        {"t3.c1_title", "物理场提取与视差门禁调参"},
        {"t3.carrier_cap", "导出方式"},
        {"t3.car_off", "不导出"},
        {"t3.car_tiff", "内嵌至主TIFF命名通道"},
        {"t3.car_split", "独立16-bit灰度PNG"},
        {"t3.car_rgb", "合并RGB"},
        {"t3.car_motion", "仅 Motion 单场 PNG"},
        {"t3.car_phase", "仅 Phase 单场 PNG"},
        {"t3.car_focus", "仅 Focus 单场 PNG"},
        {"t3.matrix_hdr", "可提取物理场矩阵 (按需惰性求值：仅计算所选物理场)"},
        {"t3.core3", "常用三项"},
        {"t3.select_all", "全选"},
        {"t3.box0", "核心合成场"},
        {"t3.box1", "空间与本征材质场"},
        {"t3.box2", "频域与辐射度误差场"},
        {"t3.box3", "照明与隐形介质场"},
        {"t3.fld_motion", "运动/星野蒙版 (Motion)"},
        {"t3.fld_phase", "超分相位置信 (Phase)"},
        {"t3.fld_focus", "拉普拉斯对焦 (Focus)"},
        {"t3.fld_depth", "极线逆深度 (L1 Depth)*"},
        {"t3.fld_defocus", "散焦符号场 (L2 Defocus)*"},
        {"t3.fld_specular", "镜面碎高光 (L4 Specular)"},
        {"t3.fld_sigma", "逐像素1σ误差 (L10c)"},
        {"t3.fld_coherence", "摩尔相干场 (L6)"},
        {"t3.fld_velocity", "动体速度幅值 (L7)"},
        {"t3.fld_flicker", "频闪光源分割 (L5)*"},
        {"t3.fld_turbulence", "热浪纹影湍流 (L8 BOS)*"},
        {"t3.gate_measured_fmt", "实测条件场: %d/%d 可用 · 灰项保留输出选择"},
        {"t3.gate_idle", "条件场可先选择 · 执行时回显实测结果"},
        {"t3.c2_title", "Sci 机身专属自监督真值数据集工厂"},
        {"t3.export_tiles", "同步切出配对训练块 (1× 带噪 RAW ↔ 2× 真值 + σ)"},
        {"t3.tile_size", "切片尺寸"},
        {"t3.tile_conf", "静态相位置信度门槛"},

        {"t4.traj_title", "防抖亚像素轨迹与四象限覆盖"},
        {"t4.traj_fullscale_fmt", "满量程 ±%.1f px"},
        {"t4.traj_pending", "轨迹坐标待引擎测量"},
        {"t4.traj_empty", "导入连拍组后绘制 #1..N 漂移轨迹"},
        {"t4.quad_fmt", "四象限相位覆盖: %d / 4"},
        {"t4.quad_idle", "四象限相位覆盖: -- / 4"},
        {"t4.shut_pending", "待检视"},
        {"t4.shut_global", "全域快门"},
        {"t4.shut_elec", "电子快门"},
        {"t4.shut_mech", "机械快门"},
        {"t4.shock_marked", "首帧微震已标记"},
        {"t4.shut_line_fmt", "%s · %s · 最大漂移 %.1fpx"},
        {"t4.shut_line_noshock_fmt", "%s · 最大漂移 %.1fpx"},
        {"t4.radar_title", "原始 RAW 8× 光学体检雷达"},
        {"t4.badge_pending", "待实测"},
        {"t4.badge_stellar_fmt", "星点 ePSF · %d/5"},
        {"t4.badge_mtf_fmt", "MTF 测量 · %d/5"},
        {"t4.badge_optics", "光学测量"},
        {"t4.zone_center", "中心"},
        {"t4.zone_rej", "拒检"},
        {"t4.radar_l1_fmt", "MTF50 · %d/5 区有效 · cycles/raw-pixel"},
        {"t4.radar_l1_idle", "五区 MTF50 (lp/px) · 像散椭圆 · 色差矢量"},
        {"t4.radar_l2_fmt", "边角 LCA 位移: R %s / B %s"},
        {"t4.radar_l2_idle", "点击下方「运行原始 RAW 8× 光学精测」生成实测雷达"},
        {"t4.run_optics", "运行原始 RAW 8× 光学精测"},
        {"t4.ois_idle", "OIS 元数据: 待导入"},
        {"t4.ois_measured_fmt", "OIS 元数据: %s · 有效 MTF %d/5"},
        {"t4.ois_on", "开启"},
        {"t4.ois_off", "关闭"},
        {"t4.ois_status_fmt", "OIS状态: %s"},
        {"t4.silicon_title", "硅片电子学实测读数"},
        {"t4.silicon_fmt", "读出噪声 σ0: %s   │   光子增益 g: %s   │   传感器温度: %s   │   检出温点: %s"},
        {"t4.export_title", "校准档与光学体检报告导出"},
        {"t4.lcp_cap", ".lcp 包含项:"},
        {"t4.lcp_lca", "倍率色差 (LCA)"},
        {"t4.lcp_vig", "真实暗角"},
        {"t4.lcp_dist", "径向畸变"},
        {"t4.btn_lcp", "导出标准 Adobe .lcp 校正档"},
        {"t4.btn_png", "导出光学体检图卡 (.png)"},
        {"t4.btn_json", "导出完整遥测 (.json)"},

        {"dlg.engine_title", "性能、显卡加速与 CLI 引擎配置"},
        {"dlg.gpu_lbl", "显卡加速:"},
        {"dlg.cores_lbl", "CPU 核数:"},
        {"dlg.thr_num_lbl", "指定核数 (0=自动):"},
        {"dlg.cli_lbl", "CLI 引擎:"},
        {"dlg.cli_ph", "自动搜索 nijinLBS"},
        {"dlg.auto_btn", "自动"},
        {"dlg.lr_lbl", "LibRaw:"},
        {"dlg.lr_ph", "留空使用内置 nijinWVDC + LibRaw 0.23-HE*"},
        {"dlg.builtin_btn", "内置"},
        {"dlg.done_btn", "完成"},
        {"dlg.cancel_btn", "取消"},
        {"dlg.prio_lbl", "优先级:"},
        {"dlg.power_lbl", "电源模式:"},
        {"dlg.adapter_lbl", "GPU 设备:"},
        {"dlg.pick_cli", "选择 nijinLBS 可执行文件"},
        {"dlg.pick_lr", "选择外部 LibRaw 动态库"},
        {"dlg.pick_raw_mix", "选择 RAW 连拍文件或目录"},
        {"dlg.pick_raw_files", "添加 RAW 连拍文件"},
        {"dlg.pick_raw_dir", "添加包含 RAW 连拍的目录"},
        {"dlg.pick_topk_dir", "选择最锐 Top-K 原片导出目录"},
        {"dlg.save_lcp", "导出标准 Adobe .lcp 镜头校正档"},
        {"dlg.save_png", "导出光学体检图卡 (.png)"},
        {"dlg.save_json", "导出完整光学与机身遥测 (.json)"},
        {"dlg.save_log", "导出会话诊断日志 (.log)"},

        {"bot.run", "开始执行"},
        {"bot.stop", "停止"},
        {"bot.run_grp_tag_fmt", "开始执行 (%d 组 · %s)"},
        {"bot.run_grp_fmt", "开始执行 (%d 组)"},
        {"bot.run_tag_fmt", "开始执行 (%s)"},
        {"bot.tag_1x", "1×真彩"},
        {"bot.tag_both", "2×+1×"},
        {"bot.tag_clean", "空镜"},
        {"bot.tag_vnd", "慢门"},
        {"bot.tag_strobe", "频闪"},
        {"bot.tag_trans", "瞬态层"},
        {"bot.tag_refl", "消反光"},
        {"bot.tag_fields_fmt", "%d场"},
        {"bot.tag_25d", "2.5D"},
        {"bot.tag_refocus", "重对焦"},
        {"bot.tag_tiles", "切片"},
        {"bot.export_log", "导出日志"},
        {"bot.export_log_tip", "导出本次会话与 CLI 子任务的完整诊断日志"},
        {"bot.open_dir", "打开输出目录"},
        {"bot.ready_idle", "就绪 · 拖入 RAW 连拍或文件夹即可静默分拣"},

        {"st.scanning", "正在后台静默剖析 RAW 连拍簇与对焦点锐度..."},
        {"st.ready_groups_fmt", "就绪 · 已剖析 %d 个连拍组"},
        {"st.plan_failed_fmt", "连拍剖析失败 (退出码 %d)"},
        {"st.merged_fmt", "已合并 %d 个组为组 %02d (%d 张)"},
        {"st.split_fmt", "已拆出 %d 张帧为新组 %02d"},
        {"st.cleaned_fmt", "已清理 %d 个少帧/散片项 (共 %d 张)"},
        {"st.cleared", "队列已清空"},
        {"st.topk_running", "正在按 AF 对焦点锐度筛出 Top-3 原片..."},
        {"st.optics_running", "正在运行原始线性 RAW 8× 光学精测..."},
        {"st.optics_done_fmt", "光学报告 · %s"},
        {"st.lcp_running", "正在生成 Adobe .lcp 镜头校正档..."},
        {"st.lcp_done_fmt", "已导出 .lcp 校正档: %s"},
        {"st.png_saved_fmt", "已导出体检图卡: %s"},
        {"st.png_err", "图卡写入失败，请选择可写目录"},
        {"st.json_running", "正在采集完整光学与硅片遥测 JSON..."},
        {"st.json_saved_fmt", "已导出遥测 JSON: %s"},
        {"st.json_err", "JSON 写入失败，请选择可写目录"},
        {"st.log_saved_fmt", "已导出诊断日志: %s"},
        {"st.log_err", "日志写入失败，请选择可写目录"},
        {"st.subtask_err_fmt", "子任务结束 (退出码 %d)%s"},
        {"st.task_stopped", "已停止当前任务"},
        {"st.done", "已完成"},
        {"st.launch_err_fmt", "启动失败: %s"},
        {"st.regroup_first", "请先完成重新分组，再按队列预览执行。"},
        {"st.pick_field", "请在「物理场与数据」中选择至少一项物理场"},
        {"st.alert_no_input", "请先拖入或选择 RAW 连拍文件 / 文件夹。"},
        {"st.alert_no_checked", "请至少在左侧队列中勾选一个待执行的连拍组。"},
        {"st.alert_no_output", "请至少勾选一项输出（2× RGGB / RGB 成片 / 物理场图 / 附加分离层）。"},
        {"st.alert_select_group", "请先在左栏导入并选择一个连拍组"},
        {"st.gate_none_runnable", "已选输出的实测条件不满足，请调整输出或连拍组"},
        {"st.group_starting_fmt", "[组 %d/%d] 正在启动合成..."},
        {"st.group_msg_fmt", "[组 %d/%d] %s"},
        {"st.group_aborted_fmt", "组 %02d 执行中止 (退出码 %d)%s"},
        {"st.group_skipped_fmt", "组 %02d 条件不满足已跳过%s，继续后续组..."},
        {"st.all_done_fmt", "全部完成 · 已输出 %d 组至 %s"},
        {"st.all_done_partial_fmt", "队列结束 · 成功 %d/%d 组 (%d 组条件不足跳过，详见日志)"},
        {"st.stopped_exec", "已停止执行"},

        {"t1.mot_prof_cap", "运动档位"},
        {"t1.mprof_auto", "档位:自动"},
        {"t1.mprof_std", "档位:标准"},
        {"t1.mprof_conc", "档位:演唱会"},
        {"t1.mprof_sport", "档位:体育"},
        {"t1.mprof_dxo", "档位:DxO-4帧"},
        {"t1.rgb_exr", "OpenEXR 2.0 多层线性 (.exr)"},
        {"t1.exr_half", "EXR:16f"},
        {"t1.exr_float", "EXR:32f"},
        {"t1.exr_parts_all", "EXR:全图层"},
        {"t1.exr_parts_bs", "EXR:主+散射"},
        {"t1.exr_parts_b", "EXR:仅主图"},
        {"t1.codec_off", "量化:关"},
        {"t1.codec_box", "量化:箱型"},
        {"t1.codec_probit", "量化:Probit"},
        {"t1.sat_hard", "饱和:硬顶"},
        {"t1.sat_probit", "饱和:Probit"},
        {"t1.tier_sinc", "1×:Sinc"},
        {"t1.tier_fast", "1×:快速"},
        {"t1.tier_joint", "1×:联合解"},
        {"t1.hl_legacy", "高光:Tanh"},
        {"t1.hl_oklab", "高光:OkLab"},
        {"t1.hl_clip", "高光:硬裁"},
        {"t1.stream_auto", "流式:自动"},
        {"t1.stream_on", "流式:开"},
        {"t1.stream_off", "流式:关"},
        {"t1.mem_gb_cap", "条带目标(G)"},

        {"t2.shut_cap", "计算快门"},
        {"t2.shut_anchor", "锚点定格"},
        {"t2.shut_integ", "积分慢门"},
        {"t2.shut_nnls", "NNLS包络"},
        {"t2.shut_deblur", "运动去糊"},
        {"t2.kappa_cap", "κ"},
        {"t2.intra_auto", "帧内:自动"},
        {"t2.intra_on", "帧内:开"},
        {"t2.intra_off", "帧内:关"},
        {"t2.diff_cap", "光学柔焦"},
        {"t2.diff_off", "关闭滤镜"},
        {"t2.diff_hdf", "HDF高光扩散"},
        {"t2.diff_mist", "黑柔 BlackMist"},
        {"t2.diff_uc", "Tiffen超低反差 UC"},
        {"t2.diff_hal", "胶片光晕 Halation"},
        {"t2.diff_uc_hdf", "UC + HDF串联"},
        {"t2.diff_uc_mist", "UC + 黑柔串联"},
        {"t2.diff_uc_hal", "UC + 光晕串联"},
        {"t2.diff_str_cap", "强度"},
        {"t2.uc_grade_cap", "UC档"},
        {"t2.baffle_std", "遮光:标准"},
        {"t2.baffle_hood", "遮光:罩"},
        {"t2.baffle_wide", "遮光:无(炫光)"},
        {"t2.chroma_neu", "中性玻璃"},
        {"t2.chroma_warm", "暖调玻璃"},
        {"t2.ap_voronoi", "孔径:Voronoi"},
        {"t2.ap_dwell", "孔径:驻留"},
        {"t2.met_ver", "尺度:校验"},
        {"t2.met_proxy", "尺度:代理"},

        {"t3.obs_cap", "扩展观测"},
        {"t3.obs_off", "按需关闭"},
        {"t3.obs_all", "全开 (O6-O10)"},
        {"t3.obs_o6", "O6 轴向色差定号"},
        {"t3.obs_o7", "O7 图像波速代理"},
        {"t3.obs_o8", "O8 差分倾斜视宁度"},
        {"t3.obs_o9", "O9 相对流位移与底色"},
        {"t3.obs_o10", "O10 暗空星迹测速"},
        {"t3.next_shot", "同步导出下一张最优拍摄规划 (.json)"},
        {"t2.sca_cap", "光谱色差"},
        {"t2.sca_off", "关闭"},
        {"t2.sca_lat", "连续横向色散"},
        {"t2.sca_aura", "合焦光谱辉光"},
        {"t2.sca_pwr", "幅度"},
        {"t2.sca_ang_rad", "径向离轴"},
        {"t2.sca_ang_0", "棱镜 0°"},
        {"t2.sca_ang_45", "棱镜 45°"},
        {"t2.sca_ang_90", "棱镜 90°"},
        {"t2.bsh_cap", "实拍散景"},
        {"t2.bsh_dn", "去二线"},
        {"t2.bsh_sw", "旋焦 ↺/↻"},
        {"t2.bsh_asp", "拉伸"},
        {"t2.bsh_on", "净洋葱"},
        {"t3.bus_cap", "NOPB 总线"},
        {"t3.bus_in_ph", "可选本组外部输入包 (.nib：保护选区 / 光流 / 修补先验)"},
        {"t3.bus_exp", "导出 S4 线性切面与溯源包 (.nib)"},
        {"w1.c1_title", "拍摄场景预设与传感器模型"},
        {"w1.c2_title", "时间轴合成、计算快门与分离图层"},
        {"w1.c3_title", "超分反演、空频降噪与高光/码字似然"},
        {"w2.c1_title", "线性域物理玻璃柔焦滤镜（DNG 底片保持纯净）"},
        {"w2.c2_title", "手持单次对焦光学：连续光谱色差与实拍散景重塑"},
        {"w2.stf_cap", "三圆盘正性 STF"},
        {"obs.matrix_title", "科学观测器 (O1–O10) 与标定"},
        {"obs.o1", "O1 漫反射/镜面辐射"},
        {"obs.o2", "O2 频闪锁相与余辉"},
        {"obs.o3", "O3 刚体几何与涡度"},
        {"obs.o4", "O4 热浪纹影 (BOS)"},
        {"obs.o5", "O5 滚快微震测振"},
        {"obs.o6", "O6 轴向色差定号"},
        {"obs.o7", "O7 图像波速代理"},
        {"obs.o8", "O8 差分倾斜视宁度"},
        {"obs.o9", "O9 相对流位移与底色"},
        {"obs.o10", "O10 暗空星迹测速"},
        {"obs.write_rep", "写出观测报告 (.json)"},
        {"r1.title", "当前组门禁与状态速览"},
        {"r1.idle", "待导入 RAW 连拍（左栏勾选批处理 · 点击行切换检视组）"},
        {"r1.grp_fmt", "检视: 组 #%02d (%d帧) ｜ 批勾: %d 组"},
        {"r1.stray_fmt", "检视: 单拍散片 (%d张) ｜ 批勾: %d 组"},
        {"r1.gate_sr_ok", "超分相位: %d/4 象限 · 2× 就绪"},
        {"r1.gate_sr_1x", "超分相位: %d 帧 · 建议 1× 真彩"},
        {"r1.gate_par_ok", "视差门禁: %.1f px · 重对焦就绪"},
        {"r1.gate_par_low", "视差门禁: %.1f px · 仅实拍散景"},
        {"r1.gate_par_wait", "视差门禁: 待检视（实拍散景随时可用）"},
        {"r2.title", "本组 NOPB/1 外部侧挂槽"},
        {"r2.idle", "未挂载外部 .nib（跟当前组独立绑定，切组不串包）"},
        {"r2.ok_fmt", "✓ 会话匹配 (%d帧) · %s"},
        {"r2.valid_fmt", "✓ 格式有效 (期望 %d 帧 · %s)"},
        {"r2.err_fmt", "✗ 预检拦截: %s"},
        {"r3.title", "交付总控与实时文件回执"},
        {"r3.receipt_hdr", "预计产出文件与激活效果:"},
        {"hdr.brand_tip", "关于 NijiN.LiteBurstSuite " NIJIN_VERSION " · 版本、开源许可与署名"},
        {"menu.about", "关于 NijiN.LiteBurstSuite " NIJIN_VERSION "..."},
        {"menu.welcome_setup", "重新打开首次启动性能向导..."},
        {"welcome.title", "欢迎使用 NijiN.LiteBurstSuite"},
        {"welcome.lang_lbl", "界面语言 / Language"},
        {"welcome.lang_note", "已根据当前系统语言自动选定，可随时切换"},
        {"welcome.hint_ok", "已按本机硬件自动完成配置；若下方引擎与解码组件均显示就绪（无缺失项），无需修改，直接点击「确认并进入工作台」即可。"},
        {"welcome.hint_missing", "提示：未自动定位到同目录 nijinLBS 命令行引擎，请点击「引擎与解码路径...」指定可执行文件。"},
        {"welcome.hw_ready_fmt", "✓ 硬件与组件就绪：%s ｜ 引擎：%s ｜ 内置 nijinWVDC + LibRaw 0.23-HE*"},
        {"welcome.hw_miss_fmt", "⚠ 引擎待定位：%s ｜ 请指定 nijinLBS 可执行文件"},
        {"welcome.backend_lbl", "计算后端"},
        {"welcome.cores_lbl", "CPU 核心调度"},
        {"welcome.stream_lbl", "流式条带目标"},
        {"welcome.battery_chk", "便携/电池设备能效模式（默认避开超线程以降低发热）"},
        {"welcome.btn_locate_cli", "引擎与解码路径..."},
        {"welcome.btn_confirm", "确认并进入工作台"},
        {"about.title", "关于 NijiN.LiteBurstSuite " NIJIN_VERSION},
        {"about.lic_text", "First-party source & bundled nijinWVDC native decoder are licensed under Apache License 2.0; prebuilt binaries link LibRaw 0.23-HE* as a Larger Work under CDDL-1.0 Sec. 3.6 (runtime replaceable via --libraw), with SVT-JPEG-XS (BSD-2-Clause-Patent), Little CMS (MIT), and zlib."},
        {"about.btn_close", "关闭"},
        {"dock.intake_btn", "分簇调参..."},
        {"intake.title", "连拍分簇、选帧与 RAW 解码调参"},
        {"intake.decoder_lbl", "RAW 解码路由 (--decoder)"},
        {"intake.dec_auto", "自动 (nijinWVDC 优先)"},
        {"intake.dec_wvdc", "强制 nijinWVDC 原生解码"},
        {"intake.dec_libraw", "强制 LibRaw 解码"},
        {"intake.nev_lbl", ".NEV 帧段范围 (--nev-frames)"},
        {"intake.gap_lbl", "连拍分簇最大间隔秒 (--burst-gap-s)"},
        {"intake.sharp_lbl", "相对中位锐度门限 (--min-rel-sharpness)"},
        {"intake.shift_lbl", "最大 1× 全局位移 px (--max-shift-px)"},
        {"intake.lucky_lbl", "大气幸运选帧 (--lucky-imaging)"},
        {"intake.lucky_auto", "自动 (Auto)"},
        {"intake.lucky_on", "强制开启 (On)"},
        {"intake.lucky_off", "关闭 (Off)"},
        {"intake.cluster_lbl", "单簇最大帧上限 (--max-cluster-frames)"},
        {"intake.reset", "恢复默认"},
        {"intake.apply", "应用并保存"},
        {"r3.jxs_bpp_cap", "码率(bpp)"},
        {"t2.fstack", "景深焦点堆叠 (Focus Stack)"},
        {"t1.rgb_psf_cap", "RGB微反差RL"},
        {"t1.ibp_0", "IBP:关(0)"},
        {"t1.ibp_1", "IBP:1次"},
        {"t1.ibp_2", "IBP:2次"},
        {"t1.polish", "平坦区双边抛光"},
        {"t2.sca_ang_custom", "自定义角°"},
        {"w2.ref_mode_auto", "重对焦:自动"},
        {"w2.ref_mode_bokeh", "光子散景"},
        {"w2.ref_mode_synth", "合成孔径"},
        {"w2.ref_mode_shift", "移轴反卷积"},
        {"w2.ref_auto_pos", "自动主体"},
        {"w2.fstop_cap", "F/"},
        {"w2.blades_0", "叶片:圆形"},
        {"w2.blades_5", "叶片:5片"},
        {"w2.blades_6", "叶片:6片"},
        {"w2.blades_7", "叶片:7片"},
        {"w2.blades_8", "叶片:8片"},
        {"w2.blades_9", "叶片:9片"},
        {"w2.blades_11", "叶片:11片"},
        {"w2.angle_cap", "转角°"},
        {"w2.sa_cap", "球差SA"},
        {"w2.tilt_cap", "沙姆X/Y"},
        {"w2.see_auto", "透视:自动"},
        {"w2.see_on", "透视:开启"},
        {"w2.see_off", "透视:关闭"},
        {"t3.dgate_cap", "条件门禁"},
        {"t3.gate_auto", "自动实测"},
        {"t3.gate_relaxed", "宽容探测"},
        {"t3.gate_force", "强制求值"},
        {"t3.min_disp_cap", "视差≥"},
        {"t3.min_coh_cap", "相干≥"},
        {"t3.min_sup_cap", "支撑≥"},
        {"t3.tab_core11", "核心 11 场"},
        {"t3.tab_ext16", "扩展几何与观测 16 场"},
        {"t3.efld_dconf", "深度置信度 (DepthConf)*"},
        {"t3.efld_dff", "散焦测距 (DfF Depth)*"},
        {"t3.efld_nx", "相对法线 X (Normal X)"},
        {"t3.efld_ny", "相对法线 Y (Normal Y)"},
        {"t3.efld_occ", "几何遮挡场 (Occlusion)"},
        {"t3.efld_rough", "O1 微面粗糙度 (Rough)"},
        {"t3.efld_veil", "O1 介质透射 (Veil)"},
        {"t3.efld_metal", "O1 金属度 (Metalness)"},
        {"t3.efld_ac", "O2 交流光占比 (AC)"},
        {"t3.efld_phos", "O2 荧光余辉 (Phosphor)"},
        {"t3.efld_ttc", "O3 碰撞时间 (TTC)"},
        {"t3.efld_vort", "O3 流体涡度 (Vorticity)"},
        {"t3.efld_strain", "O3 非刚性应变 (Strain)"},
        {"t3.efld_opd", "O4 纹影光程差 (OPD)"},
        {"t3.efld_vib", "O5 微震幅值 (Vib Amp)"},
        {"t3.efld_vital", "O5 灌注脉搏 (Perfusion)"},
        {"obs.gate_cap", "观测门禁"},
        {"obs.readout_cap", "读出ms"},
        {"obs.pitch_cap", "俯仰°"},
        {"obs.pscale_cap", "角秒/px"},
        {"obs.emit_diff", "O1 漫反射本征图"},
        {"obs.emit_lsplit", "O2 交直流分离层"},
        {"obs.eval_self", "自校验 LOO χ² / 半组 FRC (.json)"},
        {"obs.next_short", "下一张拍摄规划 (.json)"}
    };
}

inline void populate_builtin_en_us(LangPack& p) {
    p.code = "en-US";
    p.display_name = "English (en-US)";
    p.strings = {
        {"app.window_title", "NijiN.LiteBurstSuite " NIJIN_VERSION " (nijinLBS)"},
        {"hdr.perf_fmt", "Perf: %s · %s ▾"},
        {"hdr.perf_tip_mac", "Performance, Language & Engine Menu (GPU / CPU Cores / Export Log ⌘L / Settings ⌘,)"},
        {"hdr.perf_tip_win", "Performance, Language & Engine Menu (GPU / CPU Cores / Export Log Ctrl+L / Settings)"},
        {"hdr.gpu_auto", "GPU Auto"},
        {"hdr.gpu_force", "Force GPU"},
        {"hdr.gpu_cpu", "CPU Only"},
        {"hdr.gpu_off", "CPU Def"},
        {"hdr.cores_all_fmt", "All %dC"},
        {"hdr.cores_n_fmt", "%dC"},
        {"hdr.cores_perf", "P-Cores"},
        {"hdr.cores_phys", "Phys"},

        {"menu.perf_settings", "Performance & Engine Settings..."},
        {"menu.export_log", "Export Diagnostic Log..."},
        {"menu.language", "Language / 界面语言"},
        {"menu.hide_app", "Hide nijinLBS"},
        {"menu.hide_others", "Hide Others"},
        {"menu.close_win", "Close Window"},
        {"menu.quit_app", "Quit nijinLBS"},
        {"menu.file", "File"},
        {"menu.add_raw_or_dir", "Add RAW Files or Folder..."},
        {"menu.edit", "Edit"},
        {"menu.undo", "Undo"},
        {"menu.redo", "Redo"},
        {"menu.cut", "Cut"},
        {"menu.copy", "Copy"},
        {"menu.paste", "Paste"},
        {"menu.select_all", "Select All"},
        {"menu.merge_groups", "Merge Selected Groups"},
        {"menu.split_frames", "Split Selected Frames to New Group"},
        {"menu.clean_stray_fmt", "Clean Short/Stray Groups (<%d)"},
        {"menu.perf", "Performance"},
        {"menu.hw_detected_fmt", "Hardware: %s"},
        {"menu.hw_hybrid_mac_fmt", "%dP+%dE (%d Cores)"},
        {"menu.hw_cores_fmt", "%d Cores"},
        {"menu.metal_hdr", "Metal GPU Acceleration"},
        {"menu.metal_auto", "Auto (Prefer Metal GPU)"},
        {"menu.metal_force", "Force Metal GPU"},
        {"menu.metal_cpu", "CPU Only (NEON Cache)"},
        {"menu.d3d_hdr", "Vulkan GPU Acceleration"},
        {"menu.d3d_auto", "Auto (Prefer Discrete GPU)"},
        {"menu.d3d_force", "Force Vulkan GPU"},
        {"menu.d3d_cpu", "CPU Only (AVX2 Cache)"},
        {"menu.d3d_adapter_hdr", "GPU Adapter Selection"},
        {"menu.d3d_adapter_auto", "Auto (Prefer Discrete GPU)"},
        {"menu.cpu_budget_hdr", "CPU Core Budget"},
        {"menu.core_all_fmt", "Auto All (%d Threads)"},
        {"menu.core_perf_fmt", "P-Cores Only (%d)"},
        {"menu.core_main_fmt", "Primary Cores (%d)"},
        {"menu.core_phys_fmt", "Physical Cores (%d, No SMT)"},
        {"menu.core_half_fmt", "Half Cores (%d)"},
        {"menu.core_custom", "Custom Cores"},
        {"menu.core_custom_n_fmt", "Custom Cores (%d)..."},
        {"menu.core_custom_engine", "Custom Cores / Engine..."},
        {"menu.power_hdr", "Power & Battery Policy"},
        {"menu.battery_auto_on", "Auto Detect (Battery Device)"},
        {"menu.battery_auto_off", "Auto Detect (AC Desktop)"},
        {"menu.battery_force_on", "Battery Device Mode (Physical Cores)"},
        {"menu.battery_force_off", "AC Desktop Mode (All Threads)"},
        {"menu.prio_hdr", "Process Priority"},
        {"menu.prio_normal", "Normal"},
        {"menu.prio_above", "Above Normal"},
        {"menu.prio_below", "Below Normal"},
        {"menu.prio_idle", "Background Idle"},

        {"qmenu.toggle_multi", "Toggle Selected Groups (Space)"},
        {"qmenu.toggle_one", "Toggle Group Check (Space)"},
        {"qmenu.only_best", "Check Best Single Group Only"},
        {"qmenu.check_valid_fmt", "Check All Valid Groups (≥%d)"},
        {"qmenu.select_all", "Select All Queue Items"},
        {"qmenu.merge", "Merge Selected Groups"},
        {"qmenu.clean_fmt", "Clean Short & Stray Items (<%d)"},
        {"qmenu.remove_multi", "Remove Selected Groups"},
        {"qmenu.remove_one", "Remove Group"},

        {"fmenu.toggle_multi", "Enable / Skip Selected Frames (Space)"},
        {"fmenu.toggle_one", "Enable / Skip Frame (Space)"},
        {"fmenu.keep_only", "Keep Selected Frames Only"},
        {"fmenu.restore_all", "Re-enable All Valid Frames"},
        {"fmenu.lock_anchor_fmt", "Lock #%d as Pose Anchor"},
        {"fmenu.split", "Split Selected Frames to New Group"},

        {"dock.queue_hdr", "Burst Queue"},
        {"dock.sort_time", "EXIF Time Sort"},
        {"dock.recursive", "Subfolders"},
        {"dock.grp_auto", "Auto Group"},
        {"dock.grp_single", "Single Group"},
        {"dock.grp_best", "Best Subset"},
        {"dock.grp_top1", "Library Top-1"},
        {"dock.grp_tip", "Auto Group: cadence-aware burst partitioning\nSingle Group: fuse all imported frames into 1 group\nBest Subset: pick phase-diverse sharpest subset per burst\nLibrary Top-1: auto-check the single highest-scoring burst"},
        {"dock.grp_sz_cap", "Target"},
        {"dock.grp_sz_tip_fmt", "Target frames per group (minimum %d for current output mode)."},
        {"dock.regroup", "Regroup"},
        {"dock.well_scanning", "Scanning RAW bursts & AF sharpness..."},
        {"dock.well_idle", "Drop RAW files/folders here, or click to browse"},
        {"dock.btn_add_files", "+Files"},
        {"dock.btn_add_dir", "+Folder"},
        {"dock.btn_merge", "Merge"},
        {"dock.btn_clean", "Clean"},
        {"dock.btn_topk", "Top-K"},
        {"dock.btn_clear", "Clear"},
        {"dock.tip_merge", "Merge selected burst groups into one group"},
        {"dock.tip_clean_fmt", "Remove single shots and groups below %d frames"},
        {"dock.tip_topk", "Copy Top-3 sharpest original RAWs by AF score"},
        {"dock.insp_hdr", "Frame Inspector"},
        {"dock.insp_hdr_multi_fmt", "Grp %02d (%d Selected)"},
        {"dock.insp_hdr_stray", "Stray Singles"},
        {"dock.insp_hdr_group_fmt", "Group %02d Frames"},
        {"dock.insp_hint_mac", "⌘/⇧ Multi · Space · ● Anchor"},
        {"dock.insp_hint_win", "Ctrl/Shift · Space Toggle"},
        {"dock.btn_split", "Split New"},
        {"dock.btn_lock_win", "Lock Pose"},
        {"dock.tip_split", "Split selected frames into a new burst group"},
        {"dock.tip_anchor_dot", "Click to lock this frame as the pose anchor"},
        {"dock.insp_empty", "Select a burst group to inspect per-frame AF score, shift & shutter shock"},

        {"qcell.stray_title_fmt", "Single Shots · %d (Folded)  %s"},
        {"qcell.stray_badge", "[Stray]"},
        {"qcell.stray_model", "Folded isolated single shots · Split or Clean"},
        {"qcell.raw_seq_model", "RAW Burst Sequence"},
        {"qcell.short_cnt_fmt", "%df (Short)"},
        {"qcell.norm_cnt_fmt", "%df"},
        {"qcell.group_title_fmt", "Grp %02d · %s  %s"},
        {"qcell.badge_global", "[Global]"},
        {"qcell.pill_astro", "✦ Apply Astro"},
        {"qcell.pill_sci", "✦ Apply Sci"},
        {"qcell.pill_arc", "✦ Apply Archive"},

        {"ftag.dark", "DARK"},
        {"ftag.blur", "BLUR"},
        {"ftag.exp", "EXP"},
        {"ftag.shock", "SHOCK"},
        {"ftag.anchor", "LOCK"},

        {"hint.dirty", "Grouping changed; press Enter or click Regroup to refresh."},
        {"hint.min_grp_sz_fmt", "Target size requires at least %d frames; adjust and Regroup."},
        {"hint.idle_mac", "Auto-clusters bursts on import (⌘/⇧ multi-select, Space toggle, merge & split)"},
        {"hint.idle_win", "Auto-clusters bursts on import (Ctrl/Shift multi-select, Space toggle, merge & split)"},
        {"hint.summary_stray_fmt", "%d frames → %d burst(s) (%s), %d stray/short folded"},
        {"hint.summary_all_fmt", "%d frames → %d group(s) (%s)"},
        {"hint.mode_single_mac", "Single Group: all frames in 1 group; ⌘/⇧ select frames below to split."},
        {"hint.mode_single_win", "Single Group: all frames in 1 group; Ctrl/Shift select frames below to split."},
        {"hint.mode_best_fmt", "Best Subset: selects up to %s sharpest phase-complementary frames per burst."},
        {"hint.mode_top1", "Library Top-1: auto-checks the single highest-scoring burst in the library."},
        {"hint.mode_auto_short_fmt", "Below %d-frame threshold; switch to 1× True RGB (≥3) or click Force Fuse."},
        {"hint.mode_auto_ok_fmt", "Cadence clustering (target %s, min ≥%d); multi-select merge or clean."},

        {"pill.check_n_fmt", "✦ Check %d"},
        {"pill.fallback_fuse", "✦ Force Fuse"},
        {"pill.rx_astro", "✦ Astro Rx"},
        {"pill.rx_sci", "✦ Sci Rx"},
        {"pill.rx_arc", "✦ Archive Rx"},
        {"pill.only_best", "✦ Best 1 Only"},
        {"pill.check_all_fmt", "✦ Check All %d"},

        {"tab.1", "1 Develop & Time"},
        {"tab.2", "2 Optics & Bokeh"},
        {"tab.3", "3 Metrology & Lab"},
        {"tab.4", "4 Optics & Sensor"},

        {"t1.c1_title", "Scene & Temporal Mode"},
        {"t1.portal_quad_fmt", "Quads %d/4 · Optics ▸"},
        {"t1.portal_quad_idle", "Quads --/4 · Optics ▸"},
        {"t1.preset_cap", "Preset"},
        {"t1.preset_photo", "Photo"},
        {"t1.preset_astro", "Astro"},
        {"t1.preset_sci", "Sci"},
        {"t1.preset_arc", "Archive"},
        {"t1.motion_cap", "Motion"},
        {"t1.mot_auto", "Sharpest"},
        {"t1.mot_first", "First"},
        {"t1.mot_middle", "Middle"},
        {"t1.mot_clean", "CleanPlate"},
        {"t1.mot_vnd", "VirtualND"},
        {"t1.portal_strobe_on", "• Strobe On ▸"},
        {"t1.portal_strobe_idle", "Strobe/LF ▸"},
        {"t1.olpf_cap", "OLPF"},
        {"t1.olpf_auto", "Auto Sensor OLPF Profile"},
        {"t1.olpf_none", "OLPF-Free Elliptical PSF"},
        {"t1.olpf_weak4", "Weak 4-Spot Birefringent OLPF"},
        {"t1.olpf_gauss16", "16-Point Gaussian OLPF"},

        {"t1.c2_title", "Outputs & Layer Portals"},
        {"t1.c2_sub", "Synced live with dedicated tabs"},
        {"t1.outdir_cap", "Out Dir"},
        {"t1.outdir_ph", "Default: ./nijinLBS_out"},
        {"t1.browse", "Browse"},
        {"t1.emit_raw", "2× RGGB Master"},
        {"t1.raw_dng", "DNG 16-bit Lossless"},
        {"t1.raw_linear", "DNG 3-Ch Linear (Capture One)"},
        {"t1.raw_fp16", "DNG 16-bit Float FP16 (0.29×)"},
        {"t1.raw_hq", "DNG Visually-Lossless HQ (0.09×)"},
        {"t1.raw_uncomp", "DNG 16-bit Uncompressed (1.00×)"},
        {"t1.relay_raw", "Relay Denoiser"},
        {"t1.no_tool_exif", "Omit Tool Info in EXIF"},
        {"t1.no_tool_exif_tip", "When enabled, exported DNG / TIFF / PNG / JPEG XS files retain source camera EXIF only and omit ProcessingSoftware (Tag 11) and tool branding."},
        {"t1.emit_rgb", "RGB Image"},
        {"t1.scale_2x", "2×SR"},
        {"t1.scale_1x", "1×True"},
        {"t1.scale_both", "Both"},
        {"t1.scale_tip", "2×SR: 2× subpixel super-resolution (min ≥5 frames)\n1×True: native 1× 16-bit true-sampled RGB + Poisson-Gaussian KLT denoise (min ≥3 frames)\nBoth: emit both 2× and 1× 16-bit RGB in one pass"},
        {"t1.rgb_tiff", "16-bit Lossless TIFF"},
        {"t1.rgb_tiff_hq", "16-bit Visually-Lossless TIFF (HQ, 0.48×)"},
        {"t1.rgb_png", "16-bit Lossless PNG (0.40×)"},
        {"t1.rgb_jxs", "14-bit JPEG XS Lossy (0.21×)"},
        {"t1.rgb_jxs_lossless", "14-bit JPEG XS Lossless (0.43×)"},
        {"t1.rgb_tiff_raw", "16-bit Uncompressed TIFF (1.00×)"},
        {"t1.emit_field", "Phys Fields"},
        {"t1.fld_tiff_alpha", "Embed in Main TIFF Channels"},
        {"t1.fld_split", "Separate 16-bit Gray PNGs"},
        {"t1.fld_rgb", "Packed 3-Ch RGB PNG"},
        {"t1.portal_fields_fmt", "Fields(%d/%d)·Tiles ▸"},
        {"t1.emit_trans", "Transient Highlight Layer (Meteors / Fireflies / Spray)"},
        {"t1.portal_refl_on", "• De-Glint On ▸"},
        {"t1.portal_refl_idle", "De-Glint/Thr ▸"},

        {"t1.c3_title", "Reconstruction & Noise Model"},
        {"t1.reset_preset", "Reset"},
        {"t1.denoise", "Luma Denoise"},
        {"t1.chroma", "Chroma KLT"},
        {"t1.deghost", "Deghosting"},
        {"t1.psf_iters", "RL Iters"},
        {"t1.psf_sigma", "PSF Sigma"},
        {"t1.ev", "EV Offset"},
        {"t1.silicon_clean", "Silicon Banding Clean"},
        {"t1.ibp", "Compact IBP"},
        {"t1.auto_lca", "Blind Auto-LCA"},
        {"t1.auto_exp", "Auto Exposure"},
        {"t1.rolloff", "Highlight Rolloff"},
        {"t1.dither", "TPDF Dither"},
        {"t1.backend_cap", "Compute"},
        {"t1.bk_auto", "Auto (GPU Pref)"},
        {"t1.bk_gpu_mac", "Force Metal GPU"},
        {"t1.bk_gpu_win", "Force Vulkan GPU"},
        {"t1.bk_cpu", "CPU Only"},
        {"t1.threads_cap", "Cores"},
        {"t1.threads_tip", "Worker thread count (0 = Auto)"},
        {"t1.win_gpu_cap", "GPU"},
        {"t1.win_cores_cap", "Cores"},
        {"t1.win_prio_cap", "Prio"},
        {"t1.win_thr_cap", "Thr"},
        {"t1.engine_btn", "Perf & Engine..."},
        {"t1.engine_strip_fmt", "CPU: %s  ·  Engine: %s (%s)"},
        {"t1.engine_select_cli", "Select nijinLBS CLI"},
        {"t1.engine_ext_lr_fmt", "Ext %s"},
        {"t1.engine_builtin_lr", "Built-in nijinWVDC + LibRaw 0.23-HE*"},

        {"t2.c1_title", "Temporal & Motion Synthesis (Synced with Tab 1 Motion)"},
        {"t2.freeze", "Pose Freeze"},
        {"t2.pose_auto", "Sharpest (AF)"},
        {"t2.pose_first", "First"},
        {"t2.pose_middle", "Middle"},
        {"t2.pose_follow", "Follow Locked Frame"},
        {"t2.pose_locked_fmt", "Frame #%d"},
        {"t2.clean", "Pedestrian-Free Clean Plate"},
        {"t2.clean_sens", "Rejection Sens"},
        {"t2.vnd", "Handheld Virtual ND Flow"},
        {"t2.vnd_thresh", "Fluid Threshold"},
        {"t2.strobe", "Strobe Motion Trail"},
        {"t2.strobe_step", "Stride"},
        {"t2.step_1", "Every 1"},
        {"t2.step_2", "Every 2"},
        {"t2.step_3", "Every 3"},
        {"t2.step_4", "Every 4"},
        {"t2.step_5", "Every 5"},
        {"t2.step_6", "Every 6"},
        {"t2.fade_solid", "Equal Solid"},
        {"t2.fade_trail", "Fading Trail"},
        {"t2.win_fade_cap", "Trail"},
        {"t2.win_color_cap", "Channel"},
        {"t2.c2_title", "Separated Layer Extraction (Co-exported with Master)"},
        {"t2.emit_trans", "Extract Transient Highlights (Meteors / Spray)"},
        {"t2.trans_sigma", "Threshold (σ)"},
        {"t2.trans_rgb", "Color PNG"},
        {"t2.trans_gray", "Gray PNG"},
        {"t2.refl", "Glint & Showcase Reflection Split"},
        {"t2.refl_glint", "In-Place Glint"},
        {"t2.refl_split", "Wide-Baseline Split"},
        {"t2.drift_ok_fmt", "└─ Measured 2× drift span: %.1f px (>= 40 px two-layer split ready)"},
        {"t2.drift_low_fmt", "└─ Measured 2× drift span: %.1f px (glint suppress ready; split requires >= 40 px)"},
        {"t2.drift_idle", "└─ Measured drift span: import burst (two-layer split requires >= 40 px translation)"},
        {"t2.c3_title", "3D Micro-Baseline Light Field & Photon-Linear Refocus"},
        {"t2.para_idle", "Parallax pending · Selectable now, verified during run"},
        {"t2.para_est_fmt", "Est. parallax %.1f px · Depth gate checked during run"},
        {"t2.para_ok_fmt", "Measured parallax %.1f px · Depth gate passed"},
        {"t2.para_low_fmt", "Parallax %.1f px · Below depth gate"},
        {"t2.para_pend_fmt", "Measured parallax %.1f px · Depth gate pending"},
        {"t2.export_para", "Export 2.5D Interactive Viewer (.html)"},
        {"t2.para_amp", "Orbit Amp"},
        {"t2.export_refocus", "Render Photon-Linear Bokeh Refocus"},
        {"t2.refocus_pos", "Focus Plane"},
        {"t2.bokeh_str", "Bokeh Radius"},

        {"t3.c1_title", "Physical Fields & Gate"},
        {"t3.carrier_cap", "Carrier"},
        {"t3.car_off", "Off"},
        {"t3.car_tiff", "Main TIFF Extra Channels"},
        {"t3.car_split", "16-bit Gray PNGs"},
        {"t3.car_rgb", "Packed RGB"},
        {"t3.car_motion", "Motion-Only PNG"},
        {"t3.car_phase", "Phase-Only PNG"},
        {"t3.car_focus", "Focus-Only PNG"},
        {"t3.matrix_hdr", "Physical Field Matrix (Lazy evaluation: computes selected fields only)"},
        {"t3.core3", "Core 3"},
        {"t3.select_all", "All"},
        {"t3.box0", "Core Synthesis Fields"},
        {"t3.box1", "Geometry & Intrinsic Material"},
        {"t3.box2", "Spectral & Radiometric Uncertainty"},
        {"t3.box3", "Illumination & Refractive Media"},
        {"t3.fld_motion", "Motion / Sky Matte"},
        {"t3.fld_phase", "SR Phase Confidence"},
        {"t3.fld_focus", "Laplacian Focus"},
        {"t3.fld_depth", "Inverse Depth (L1)*"},
        {"t3.fld_defocus", "Signed Defocus (L2)*"},
        {"t3.fld_specular", "Specular Glint (L4)"},
        {"t3.fld_sigma", "1σ Uncertainty (L10c)"},
        {"t3.fld_coherence", "Moiré Coherence (L6)"},
        {"t3.fld_velocity", "Subject Velocity (L7)"},
        {"t3.fld_flicker", "PWM Flicker Map (L5)*"},
        {"t3.fld_turbulence", "Schlieren BOS (L8)*"},
        {"t3.gate_measured_fmt", "Observable gated fields: %d/%d · Disabled keep check"},
        {"t3.gate_idle", "Conditional fields selectable · Physical gates evaluated on run"},
        {"t3.c2_title", "Self-Supervised Camera Dataset Tile Factory"},
        {"t3.export_tiles", "Export Paired Tiles (1× Noisy RAW ↔ 2× Clean + σ)"},
        {"t3.tile_size", "Tile Size"},
        {"t3.tile_conf", "Min Confidence"},

        {"t4.traj_title", "Subpixel Drift Trajectory & 2×2 Quadrants"},
        {"t4.traj_fullscale_fmt", "Full scale ±%.1f px"},
        {"t4.traj_pending", "Trajectory awaiting engine measurement"},
        {"t4.traj_empty", "Import a burst group to plot #1..N subpixel trajectory"},
        {"t4.quad_fmt", "2×2 Phase Quadrants: %d / 4"},
        {"t4.quad_idle", "2×2 Phase Quadrants: -- / 4"},
        {"t4.shut_pending", "Pending"},
        {"t4.shut_global", "Global Shutter"},
        {"t4.shut_elec", "Electronic Shutter"},
        {"t4.shut_mech", "Mechanical Shutter"},
        {"t4.shock_marked", "Frame #1 Shock Flagged"},
        {"t4.shut_line_fmt", "%s · %s · Max drift %.1fpx"},
        {"t4.shut_line_noshock_fmt", "%s · Max drift %.1fpx"},
        {"t4.radar_title", "RAW 8× MTF50 Radar"},
        {"t4.badge_pending", "Unmeasured"},
        {"t4.badge_stellar_fmt", "Stellar ePSF · %d/5"},
        {"t4.badge_mtf_fmt", "Slanted-Edge · %d/5"},
        {"t4.badge_optics", "Measured"},
        {"t4.zone_center", "Center"},
        {"t4.zone_rej", "Low"},
        {"t4.radar_l1_fmt", "MTF50 · %d/5 Valid Zones · cycles/raw-pixel"},
        {"t4.radar_l1_idle", "5-Zone MTF50 (lp/px) · Astigmatism · Lateral CA"},
        {"t4.radar_l2_fmt", "Corner LCA Shift: R %s / B %s"},
        {"t4.radar_l2_idle", "Click 'Run Linear RAW 8× Optics Probe' below to measure"},
        {"t4.run_optics", "Run Linear RAW 8× Optics Probe"},
        {"t4.ois_idle", "OIS Metadata: Pending"},
        {"t4.ois_measured_fmt", "OIS Metadata: %s · Valid MTF %d/5"},
        {"t4.ois_on", "ON"},
        {"t4.ois_off", "OFF"},
        {"t4.ois_status_fmt", "OIS Status: %s"},
        {"t4.silicon_title", "Silicon Sensor Electronics Readout"},
        {"t4.silicon_fmt", "Read Noise σ0: %s   │   Photon Gain g: %s   │   Sensor Temp: %s   │   Warm Pixels: %s"},
        {"t4.export_title", "Lens Profile & Optical Passport Export"},
        {"t4.lcp_cap", ".lcp Terms:"},
        {"t4.lcp_lca", "Lateral CA (LCA)"},
        {"t4.lcp_vig", "Vignetting"},
        {"t4.lcp_dist", "Radial Distortion"},
        {"t4.btn_lcp", "Export Adobe .lcp Profile"},
        {"t4.btn_png", "Export Passport Card (.png)"},
        {"t4.btn_json", "Export Full Telemetry (.json)"},

        {"dlg.engine_title", "Performance, GPU & CLI Engine Configuration"},
        {"dlg.gpu_lbl", "Compute:"},
        {"dlg.cores_lbl", "CPU Cores:"},
        {"dlg.thr_num_lbl", "Threads (0=Auto):"},
        {"dlg.cli_lbl", "CLI Path:"},
        {"dlg.cli_ph", "Auto-detect nijinLBS"},
        {"dlg.auto_btn", "Auto"},
        {"dlg.lr_lbl", "LibRaw:"},
        {"dlg.lr_ph", "Blank = Built-in nijinWVDC + LibRaw 0.23-HE*"},
        {"dlg.builtin_btn", "Built-in"},
        {"dlg.done_btn", "Done"},
        {"dlg.cancel_btn", "Cancel"},
        {"dlg.prio_lbl", "Priority:"},
        {"dlg.power_lbl", "Power:"},
        {"dlg.adapter_lbl", "GPU Dev:"},
        {"dlg.pick_cli", "Select nijinLBS Executable"},
        {"dlg.pick_lr", "Select External LibRaw Shared Library"},
        {"dlg.pick_raw_mix", "Select RAW Burst Files or Folders"},
        {"dlg.pick_raw_files", "Add RAW Burst Files"},
        {"dlg.pick_raw_dir", "Add Folder Containing RAW Bursts"},
        {"dlg.pick_topk_dir", "Select Output Folder for Top-K Sharpest RAWs"},
        {"dlg.save_lcp", "Export Adobe .lcp Lens Profile"},
        {"dlg.save_png", "Export Optical Passport Card (.png)"},
        {"dlg.save_json", "Export Optics & Sensor Telemetry (.json)"},
        {"dlg.save_log", "Export Diagnostic Session Log (.log)"},

        {"bot.run", "Start"},
        {"bot.stop", "Stop"},
        {"bot.run_grp_tag_fmt", "Start (%d Grp · %s)"},
        {"bot.run_grp_fmt", "Start (%d Grp)"},
        {"bot.run_tag_fmt", "Start (%s)"},
        {"bot.tag_1x", "1×True"},
        {"bot.tag_both", "2×+1×"},
        {"bot.tag_clean", "Clean"},
        {"bot.tag_vnd", "VirtND"},
        {"bot.tag_strobe", "Strobe"},
        {"bot.tag_trans", "Transient"},
        {"bot.tag_refl", "DeGlint"},
        {"bot.tag_fields_fmt", "%dFld"},
        {"bot.tag_25d", "2.5D"},
        {"bot.tag_refocus", "Refocus"},
        {"bot.tag_tiles", "Tiles"},
        {"bot.export_log", "Export Log"},
        {"bot.export_log_tip", "Save full session and CLI subprocess diagnostic log"},
        {"bot.open_dir", "Open Folder"},
        {"bot.ready_idle", "Ready · Drop RAW bursts or folders to auto-cluster"},

        {"st.scanning", "Analyzing RAW burst clusters & AF sharpness in background..."},
        {"st.ready_groups_fmt", "Ready · %d burst group(s) analyzed"},
        {"st.plan_failed_fmt", "Burst analysis failed (exit code %d)"},
        {"st.merged_fmt", "Merged %d groups into Group %02d (%d frames)"},
        {"st.split_fmt", "Split %d frame(s) into new Group %02d"},
        {"st.cleaned_fmt", "Cleaned %d short/stray item(s) (%d frames)"},
        {"st.cleared", "Queue cleared"},
        {"st.topk_running", "Selecting Top-3 sharpest original RAWs by AF score..."},
        {"st.optics_running", "Running linear RAW 8× slanted-edge optics probe..."},
        {"st.optics_done_fmt", "Optics Report · %s"},
        {"st.lcp_running", "Generating Adobe .lcp lens correction profile..."},
        {"st.lcp_done_fmt", "Exported .lcp profile: %s"},
        {"st.png_saved_fmt", "Exported optical passport card: %s"},
        {"st.png_err", "Failed to write PNG; choose a writable folder"},
        {"st.json_running", "Collecting full optics & silicon telemetry JSON..."},
        {"st.json_saved_fmt", "Exported telemetry JSON: %s"},
        {"st.json_err", "Failed to write JSON; choose a writable folder"},
        {"st.log_saved_fmt", "Exported diagnostic log: %s"},
        {"st.log_err", "Failed to write log; choose a writable folder"},
        {"st.subtask_err_fmt", "Task exited (code %d)%s"},
        {"st.task_stopped", "Stopped current task"},
        {"st.done", "Completed"},
        {"st.launch_err_fmt", "Launch failed: %s"},
        {"st.regroup_first", "Click Regroup to refresh the queue preview before starting."},
        {"st.pick_field", "Select at least one physical field in Tab 3"},
        {"st.alert_no_input", "Drop or add RAW burst files / folders first."},
        {"st.alert_no_checked", "Check at least one burst group in the left queue."},
        {"st.alert_no_output", "Enable at least one output (2× RGGB / RGB Image / Fields / Extra Layer)."},
        {"st.alert_select_group", "Import and select a burst group in the left queue first"},
        {"st.gate_none_runnable", "Selected outputs did not pass physical gates; adjust outputs or group"},
        {"st.group_starting_fmt", "[Group %d/%d] Starting synthesis..."},
        {"st.group_msg_fmt", "[Group %d/%d] %s"},
        {"st.group_aborted_fmt", "Group %02d aborted (exit code %d)%s"},
        {"st.group_skipped_fmt", "Group %02d skipped%s; continuing remaining queue..."},
        {"st.all_done_fmt", "Done · Exported %d group(s) to %s"},
        {"st.all_done_partial_fmt", "Finished · %d/%d group(s) exported (%d skipped; see Export Log)"},
        {"st.stopped_exec", "Execution stopped"},

        {"t1.mot_prof_cap", "Profile"},
        {"t1.mprof_auto", "Profile:Auto"},
        {"t1.mprof_std", "Profile:Std"},
        {"t1.mprof_conc", "Profile:Concert"},
        {"t1.mprof_sport", "Profile:Sports"},
        {"t1.mprof_dxo", "Profile:DxO-4f"},
        {"t1.rgb_exr", "OpenEXR 2.0 Multi-Part (.exr)"},
        {"t1.exr_half", "EXR:16f"},
        {"t1.exr_float", "EXR:32f"},
        {"t1.exr_parts_all", "EXR:All Parts"},
        {"t1.exr_parts_bs", "EXR:Beauty+Scatter"},
        {"t1.exr_parts_b", "EXR:Beauty"},
        {"t1.codec_off", "Codec:Off"},
        {"t1.codec_box", "Codec:Box"},
        {"t1.codec_probit", "Codec:Probit"},
        {"t1.sat_hard", "Sat:Hard"},
        {"t1.sat_probit", "Sat:Probit"},
        {"t1.tier_sinc", "1×:Sinc"},
        {"t1.tier_fast", "1×:Fast"},
        {"t1.tier_joint", "1×:Joint"},
        {"t1.hl_legacy", "HL:Legacy"},
        {"t1.hl_oklab", "HL:OkLab"},
        {"t1.hl_clip", "HL:Clip"},
        {"t1.stream_auto", "Stream:Auto"},
        {"t1.stream_on", "Stream:On"},
        {"t1.stream_off", "Stream:Off"},
        {"t1.mem_gb_cap", "Stripe(G)"},

        {"t2.shut_cap", "Shutter"},
        {"t2.shut_anchor", "Anchor"},
        {"t2.shut_integ", "Integrate"},
        {"t2.shut_nnls", "NNLS"},
        {"t2.shut_deblur", "Deblur"},
        {"t2.kappa_cap", "κ"},
        {"t2.intra_auto", "Intra:Auto"},
        {"t2.intra_on", "Intra:On"},
        {"t2.intra_off", "Intra:Off"},
        {"t2.diff_cap", "Diffusion"},
        {"t2.diff_off", "Off"},
        {"t2.diff_hdf", "HDF"},
        {"t2.diff_mist", "Black Mist"},
        {"t2.diff_uc", "Ultra Contrast"},
        {"t2.diff_hal", "Halation"},
        {"t2.diff_uc_hdf", "UC + HDF"},
        {"t2.diff_uc_mist", "UC + Mist"},
        {"t2.diff_uc_hal", "UC + Halation"},
        {"t2.diff_str_cap", "Str"},
        {"t2.uc_grade_cap", "Grade"},
        {"t2.baffle_std", "Baffle:Std"},
        {"t2.baffle_hood", "Baffle:Hood"},
        {"t2.baffle_wide", "Baffle:Wide"},
        {"t2.chroma_neu", "Neutral"},
        {"t2.chroma_warm", "Warm-Glass"},
        {"t2.ap_voronoi", "Ap:Voronoi"},
        {"t2.ap_dwell", "Ap:Dwell"},
        {"t2.met_ver", "Metric:Ver"},
        {"t2.met_proxy", "Metric:Proxy"},

        {"t3.obs_cap", "Observers"},
        {"t3.obs_off", "Off"},
        {"t3.obs_all", "All (O6-O10)"},
        {"t3.obs_o6", "O6 Axial CA Sign"},
        {"t3.obs_o7", "O7 Wave Speed Proxy"},
        {"t3.obs_o8", "O8 Diff-Tilt Seeing"},
        {"t3.obs_o9", "O9 Rel-Flow & Albedo"},
        {"t3.obs_o10", "O10 Astro Streaks"},
        {"t3.next_shot", "Export Next-Shot Optimal Plan (.json)"},
        {"t2.sca_cap", "Spectral CA"},
        {"t2.sca_off", "Off"},
        {"t2.sca_lat", "Lateral Fan"},
        {"t2.sca_aura", "Axial Aura"},
        {"t2.sca_pwr", "Px"},
        {"t2.sca_ang_rad", "Radial"},
        {"t2.sca_ang_0", "Prism 0°"},
        {"t2.sca_ang_45", "Prism 45°"},
        {"t2.sca_ang_90", "Prism 90°"},
        {"t2.bsh_cap", "Bokeh Shape"},
        {"t2.bsh_dn", "De-Nisen"},
        {"t2.bsh_sw", "Swirl ↺/↻"},
        {"t2.bsh_asp", "Aspect"},
        {"t2.bsh_on", "Onion"},
        {"t3.bus_cap", "NOPB Bus"},
        {"t3.bus_in_ph", "Optional group sidecar (.nib: protect mask / flow / prior)"},
        {"t3.bus_exp", "Export S4 Linear & Provenance (.nib)"},
        {"w1.c1_title", "Scene Preset & Sensor Profile"},
        {"w1.c2_title", "Temporal Synthesis, Shutter & Layer Split"},
        {"w1.c3_title", "SR Inversion, Denoise & Likelihood Models"},
        {"w2.c1_title", "Linear Optical Glass Diffusion (DNG Stays Pure)"},
        {"w2.c2_title", "Handheld Single-Focus Optics: Spectral CA & Bokeh Shaping"},
        {"w2.stf_cap", "3-Disc Pos STF"},
        {"obs.matrix_title", "Scientific Observers (O1–O10)"},
        {"obs.o1", "O1 Diffuse/Spec"},
        {"obs.o2", "O2 Flicker Lock-In"},
        {"obs.o3", "O3 Kinematics/Vort"},
        {"obs.o4", "O4 Schlieren BOS"},
        {"obs.o5", "O5 RS Vibrometry"},
        {"obs.o6", "O6 Axial CA Sign"},
        {"obs.o7", "O7 Wave Speed Proxy"},
        {"obs.o8", "O8 Diff-Tilt Seeing"},
        {"obs.o9", "O9 Rel-Flow & Albedo"},
        {"obs.o10", "O10 Astro Streaks"},
        {"obs.write_rep", "Write Observer JSON"},
        {"r1.title", "Active Group & Gate Passport"},
        {"r1.idle", "Drop RAW bursts on left (check to run · click row to inspect)"},
        {"r1.grp_fmt", "Inspect: Grp #%02d (%df) | Batch: %d grp(s)"},
        {"r1.stray_fmt", "Inspect: Stray Singles (%d) | Batch: %d grp(s)"},
        {"r1.gate_sr_ok", "SR Phase: %d/4 Quads · 2× Ready"},
        {"r1.gate_sr_1x", "SR Phase: %d frames · 1× Recommended"},
        {"r1.gate_par_ok", "Parallax: %.1f px · Refocus Ready"},
        {"r1.gate_par_low", "Parallax: %.1f px · Real Bokeh Only"},
        {"r1.gate_par_wait", "Parallax: Pending (Real Bokeh Always Ready)"},
        {"r2.title", "Group NOPB/1 Sidecar Dock"},
        {"r2.idle", "No .nib attached (bound per group · no cross-group leak)"},
        {"r2.ok_fmt", "✓ Matched (%df) · %s"},
        {"r2.valid_fmt", "✓ Valid header (expects %df · %s)"},
        {"r2.err_fmt", "✗ Preflight error: %s"},
        {"r3.title", "Deliverables & Live File Plan"},
        {"r3.receipt_hdr", "Planned Deliverables & Active Effects:"},
        {"hdr.brand_tip", "About NijiN.LiteBurstSuite " NIJIN_VERSION " · Version, Licenses & Credits"},
        {"menu.about", "About NijiN.LiteBurstSuite " NIJIN_VERSION "..."},
        {"menu.welcome_setup", "Reopen First-Launch Performance Setup..."},
        {"welcome.title", "Welcome to NijiN.LiteBurstSuite"},
        {"welcome.lang_lbl", "Language / 界面语言"},
        {"welcome.lang_note", "Auto-selected from system language; you can switch anytime."},
        {"welcome.hint_ok", "Hardware defaults are automatically configured. If CLI engine and decoders below show Ready (nothing missing), no changes are needed — click Confirm & Enter Studio."},
        {"welcome.hint_missing", "Note: nijinLBS CLI binary was not auto-detected nearby; click Engine & LibRaw Paths... to select it."},
        {"welcome.hw_ready_fmt", "✓ Hardware & Decoders Ready: %s | Engine: %s | Built-in nijinWVDC + LibRaw 0.23-HE*"},
        {"welcome.hw_miss_fmt", "⚠ Engine Missing: %s | Please select nijinLBS executable"},
        {"welcome.backend_lbl", "Compute Backend"},
        {"welcome.cores_lbl", "CPU Core Policy"},
        {"welcome.stream_lbl", "Stripe Target"},
        {"welcome.battery_chk", "Laptop / Battery Efficiency Mode (disable SMT by default)"},
        {"welcome.btn_locate_cli", "Engine & LibRaw Paths..."},
        {"welcome.btn_confirm", "Confirm & Enter Studio"},
        {"about.title", "About NijiN.LiteBurstSuite " NIJIN_VERSION},
        {"about.lic_text", "First-party source & bundled nijinWVDC native decoder are licensed under Apache License 2.0; prebuilt binaries link LibRaw 0.23-HE* as a Larger Work under CDDL-1.0 Sec. 3.6 (runtime replaceable via --libraw), with SVT-JPEG-XS (BSD-2-Clause-Patent), Little CMS (MIT), and zlib."},
        {"about.btn_close", "Close"},
        {"dock.intake_btn", "Intake..."},
        {"intake.title", "Burst Intake, Filtering & Decoder Parameters"},
        {"intake.decoder_lbl", "RAW Decoder Route (--decoder)"},
        {"intake.dec_auto", "Auto (Prefer nijinWVDC)"},
        {"intake.dec_wvdc", "Force nijinWVDC Native"},
        {"intake.dec_libraw", "Force LibRaw"},
        {"intake.nev_lbl", ".NEV Frame Range (--nev-frames)"},
        {"intake.gap_lbl", "Max Burst Gap Seconds (--burst-gap-s)"},
        {"intake.sharp_lbl", "Min Relative Sharpness (--min-rel-sharpness)"},
        {"intake.shift_lbl", "Max 1× Global Shift px (--max-shift-px)"},
        {"intake.lucky_lbl", "Lucky Imaging (--lucky-imaging)"},
        {"intake.lucky_auto", "Auto"},
        {"intake.lucky_on", "Force On"},
        {"intake.lucky_off", "Off"},
        {"intake.cluster_lbl", "Max Cluster Frames (--max-cluster-frames)"},
        {"intake.reset", "Reset Defaults"},
        {"intake.apply", "Apply & Save"},
        {"r3.jxs_bpp_cap", "BPP"},
        {"t2.fstack", "Focus Stack (Macro DoF)"},
        {"t1.rgb_psf_cap", "RGB RL Iters"},
        {"t1.ibp_0", "IBP:Off(0)"},
        {"t1.ibp_1", "IBP:1 Iter"},
        {"t1.ibp_2", "IBP:2 Iters"},
        {"t1.polish", "Flat Edge Polish"},
        {"t2.sca_ang_custom", "Custom°"},
        {"w2.ref_mode_auto", "Mode:Auto"},
        {"w2.ref_mode_bokeh", "Mode:Bokeh"},
        {"w2.ref_mode_synth", "Mode:Synth-Ap"},
        {"w2.ref_mode_shift", "Mode:Shift-Deconv"},
        {"w2.ref_auto_pos", "Auto Focus"},
        {"w2.fstop_cap", "F/"},
        {"w2.blades_0", "Blades:Round"},
        {"w2.blades_5", "Blades:5"},
        {"w2.blades_6", "Blades:6"},
        {"w2.blades_7", "Blades:7"},
        {"w2.blades_8", "Blades:8"},
        {"w2.blades_9", "Blades:9"},
        {"w2.blades_11", "Blades:11"},
        {"w2.angle_cap", "Rot°"},
        {"w2.sa_cap", "SA"},
        {"w2.tilt_cap", "Tilt X/Y"},
        {"w2.see_auto", "Occ:Auto"},
        {"w2.see_on", "Occ:On"},
        {"w2.see_off", "Occ:Off"},
        {"t3.dgate_cap", "Field Gate"},
        {"t3.gate_auto", "Auto"},
        {"t3.gate_relaxed", "Relaxed"},
        {"t3.gate_force", "Force"},
        {"t3.min_disp_cap", "Disp≥"},
        {"t3.min_coh_cap", "Coh≥"},
        {"t3.min_sup_cap", "Sup≥"},
        {"t3.tab_core11", "Core 11 Fields"},
        {"t3.tab_ext16", "Extended & Observer 16"},
        {"t3.efld_dconf", "Depth Confidence*"},
        {"t3.efld_dff", "DfF Depth*"},
        {"t3.efld_nx", "Surface Normal X"},
        {"t3.efld_ny", "Surface Normal Y"},
        {"t3.efld_occ", "Ambient Occlusion"},
        {"t3.efld_rough", "O1 Roughness"},
        {"t3.efld_veil", "O1 Veil Trans."},
        {"t3.efld_metal", "O1 Metalness"},
        {"t3.efld_ac", "O2 AC Light Frac"},
        {"t3.efld_phos", "O2 Phosphor Decay"},
        {"t3.efld_ttc", "O3 Time-to-Contact"},
        {"t3.efld_vort", "O3 Vorticity"},
        {"t3.efld_strain", "O3 Nonrigid Strain"},
        {"t3.efld_opd", "O4 Schlieren OPD"},
        {"t3.efld_vib", "O5 Vibration Amp"},
        {"t3.efld_vital", "O5 Vital Perfusion"},
        {"obs.gate_cap", "Obs Gate"},
        {"obs.readout_cap", "RS(ms)"},
        {"obs.pitch_cap", "Pitch°"},
        {"obs.pscale_cap", "″/px"},
        {"obs.emit_diff", "O1 Diffuse Albedo"},
        {"obs.emit_lsplit", "O2 DC/AC Split"},
        {"obs.eval_self", "Self-Eval LOO χ² & Half-FRC (.json)"},
        {"obs.next_short", "Next-Shot Plan (.json)"}
    };
}

class I18nManager {
public:
    static I18nManager& instance() {
        static I18nManager inst;
        return inst;
    }

    void init(const std::string& exe_path, const std::string& preferred_code = "") {
        std::lock_guard<std::mutex> lk(mu_);
        packs_.clear();
        LangPack zh, en;
        populate_builtin_zh_cn(zh);
        populate_builtin_en_us(en);
        packs_.push_back(std::move(zh));
        packs_.push_back(std::move(en));

        std::vector<std::filesystem::path> dirs;
        auto add_dir = [&](const std::filesystem::path& p) {
            std::error_code ec;
            if (p.empty() || !std::filesystem::is_directory(p, ec)) return;
            auto can = std::filesystem::weakly_canonical(p, ec);
            const auto& key = ec ? p : can;
            for (const auto& d : dirs) {
                if (d == key) return;
            }
            dirs.push_back(key);
        };

        if (!exe_path.empty()) {
            std::error_code ec;
            auto ep = heraw_fs::from_utf8(exe_path);
            auto ed = ep.parent_path();
            add_dir(ed / "lang");
            add_dir(ed.parent_path() / "Resources" / "lang");
            add_dir(ed.parent_path() / "lang");
        }
        {
            std::error_code ec;
            add_dir(std::filesystem::current_path(ec) / "lang");
        }

        for (const auto& dir : dirs) {
            std::error_code ec;
            std::vector<std::filesystem::path> files;
            for (const auto& ent : std::filesystem::directory_iterator(dir, ec)) {
                if (ec) break;
                if (!ent.is_regular_file(ec)) continue;
                std::string ext = heraw_fs::to_utf8(ent.path().extension());
                for (char& ch : ext) ch = (char)std::tolower((unsigned char)ch);
                if (ext == ".lng") files.push_back(ent.path());
            }
            std::sort(files.begin(), files.end());
            for (const auto& fp : files) {
                load_lng_file_unlocked(fp);
            }
        }

        std::string target = preferred_code.empty() ? detect_system_lang_code() : preferred_code;
        set_language_unlocked(target);
    }

    bool load_lng_file(const std::string& utf8_path) {
        std::lock_guard<std::mutex> lk(mu_);
        return load_lng_file_unlocked(heraw_fs::from_utf8(utf8_path), true);
    }

    bool set_language(const std::string& code) {
        std::lock_guard<std::mutex> lk(mu_);
        return set_language_unlocked(code);
    }

    std::string active_code() const {
        std::lock_guard<std::mutex> lk(mu_);
        return packs_[active_idx_].code;
    }

    bool is_english() const {
        std::string c = active_code();
        return c.size() >= 2 && (c[0] == 'e' || c[0] == 'E') && (c[1] == 'n' || c[1] == 'N');
    }

    std::vector<std::pair<std::string, std::string>> available_languages() const {
        std::lock_guard<std::mutex> lk(mu_);
        std::vector<std::pair<std::string, std::string>> out;
        out.reserve(packs_.size());
        for (const auto& p : packs_) {
            out.emplace_back(p.code, p.display_name.empty() ? p.code : p.display_name);
        }
        return out;
    }

    std::string active_source_path() const {
        std::lock_guard<std::mutex> lk(mu_);
        return packs_[active_idx_].source_path;
    }

    const std::string& tr(const char* key) const {
        std::lock_guard<std::mutex> lk(mu_);
        if (!key) {
            static const std::string kEmpty;
            return kEmpty;
        }
        std::string k(key);
        const auto& cur = packs_[active_idx_].strings;
        auto it = cur.find(k);
        if (it != cur.end()) return it->second;
        for (const auto& fb : packs_) {
            auto fit = fb.strings.find(k);
            if (fit != fb.strings.end()) return fit->second;
        }
        auto bit = missing_cache_.emplace(k, k).first;
        return bit->second;
    }

    static std::string classify_locale_tag_to_lang_code(const std::string& raw_tag) {
        std::string s;
        s.reserve(raw_tag.size());
        for (char ch : raw_tag) {
            if (ch == '.') break;
            if (ch == '@') break;
            s.push_back((ch == '_') ? '-' : (char)std::tolower((unsigned char)ch));
        }
        while (!s.empty() && std::isspace((unsigned char)s.front())) s.erase(s.begin());
        while (!s.empty() && std::isspace((unsigned char)s.back())) s.pop_back();
        const bool is_zh_bcp = (s.size() >= 2 && s.compare(0, 2, "zh") == 0 && (s.size() == 2 || s[2] == '-'));
        const bool is_zh_desc = (s.find("chinese") != std::string::npos);
        if (!is_zh_bcp && !is_zh_desc) {
            return "en-US";
        }
        auto has_tok = [&](const char* tok) -> bool {
            std::string needle = std::string("-") + tok;
            size_t pos = s.find(needle);
            while (pos != std::string::npos) {
                size_t end = pos + needle.size();
                if (end == s.size() || s[end] == '-') return true;
                pos = s.find(needle, pos + 1);
            }
            return false;
        };
        if (has_tok("sg") || s.find("singapore") != std::string::npos ||
            has_tok("hant") || has_tok("tw") || has_tok("hk") || has_tok("mo") || has_tok("cht") ||
            s.find("traditional") != std::string::npos || s.find("taiwan") != std::string::npos ||
            s.find("hong") != std::string::npos || s.find("macau") != std::string::npos || s.find("macao") != std::string::npos) {
            return "en-US";
        }
        if (has_tok("hans") || has_tok("cn") || has_tok("chs") ||
            s.find("simplified") != std::string::npos || s.find("china") != std::string::npos) {
            return "zh-CN";
        }
        return "en-US";
    }

    static std::string detect_system_lang_code() {
        const char* test_loc = std::getenv("NIJIN_SYS_LOCALE_TEST");
        if (test_loc && *test_loc) {
            return classify_locale_tag_to_lang_code(test_loc);
        }
#ifdef _WIN32
        wchar_t loc_name[LOCALE_NAME_MAX_LENGTH] = {0};
        if (GetUserDefaultLocaleName(loc_name, LOCALE_NAME_MAX_LENGTH) > 0 && loc_name[0] != L'\0') {
            std::string tag;
            for (size_t i = 0; loc_name[i] != L'\0'; ++i) {
                wchar_t wc = loc_name[i];
                tag.push_back((wc < 128) ? (char)wc : '?');
            }
            return classify_locale_tag_to_lang_code(tag);
        }
        LANGID lid = GetUserDefaultUILanguage();
        if (PRIMARYLANGID(lid) == LANG_CHINESE && SUBLANGID(lid) == SUBLANG_CHINESE_SIMPLIFIED) {
            return "zh-CN";
        }
        return "en-US";
#elif defined(__OBJC__)
        NSArray<NSString*>* langs = [NSLocale preferredLanguages];
        if (langs.count > 0 && langs.firstObject.length > 0) {
            return classify_locale_tag_to_lang_code([langs.firstObject UTF8String] ?: "");
        }
        NSString* cur = [[NSLocale currentLocale] localeIdentifier];
        if (cur.length > 0) {
            return classify_locale_tag_to_lang_code([cur UTF8String] ?: "");
        }
        return "en-US";
#else
        const char* lc = std::getenv("LC_ALL");
        if (!lc || !*lc) lc = std::getenv("LC_MESSAGES");
        if (!lc || !*lc) lc = std::getenv("LANG");
        if (lc && *lc) return classify_locale_tag_to_lang_code(lc);
        return "en-US";
#endif
    }

private:
    I18nManager() {
        LangPack zh, en;
        populate_builtin_zh_cn(zh);
        populate_builtin_en_us(en);
        packs_.push_back(std::move(zh));
        packs_.push_back(std::move(en));
    }

    bool load_lng_file_unlocked(const std::filesystem::path& fp, bool force_override = false) {
#ifdef _WIN32
        FILE* f = _wfopen(fp.wstring().c_str(), L"rb");
#else
        FILE* f = std::fopen(fp.c_str(), "rb");
#endif
        if (!f) return false;
        std::string buf;
        char chunk[4096];
        size_t n = 0;
        while ((n = std::fread(chunk, 1, sizeof(chunk), f)) > 0) {
            buf.append(chunk, n);
        }
        std::fclose(f);
        if (buf.empty()) return false;

        LangPack parsed;
        parsed.code = heraw_fs::to_utf8(fp.stem());
        parsed.display_name = parsed.code;
        parsed.source_path = heraw_fs::to_utf8(fp);
        parse_lng_content(buf, parsed);
        if (parsed.strings.empty()) return false;

        for (auto& existing : packs_) {
            if (iequals(existing.code, parsed.code)) {
                if (force_override || existing.source_path.empty()) {
                    existing.source_path = parsed.source_path;
                    if (!parsed.display_name.empty()) existing.display_name = parsed.display_name;
                    for (const auto& kv : parsed.strings) {
                        existing.strings[kv.first] = kv.second;
                    }
                }
                return true;
            }
        }
        packs_.push_back(std::move(parsed));
        return true;
    }

    bool set_language_unlocked(const std::string& code) {
        if (code.empty()) return false;
        for (size_t i = 0; i < packs_.size(); ++i) {
            if (iequals(packs_[i].code, code)) {
                active_idx_ = i;
                return true;
            }
        }
        if (code.size() >= 2) {
            std::string pfx = code.substr(0, 2);
            for (size_t i = 0; i < packs_.size(); ++i) {
                if (packs_[i].code.size() >= 2 && iequals(packs_[i].code.substr(0, 2), pfx)) {
                    active_idx_ = i;
                    return true;
                }
            }
        }
        return false;
    }

    static bool iequals(const std::string& a, const std::string& b) {
        if (a.size() != b.size()) return false;
        for (size_t i = 0; i < a.size(); ++i) {
            if (std::tolower((unsigned char)a[i]) != std::tolower((unsigned char)b[i])) return false;
        }
        return true;
    }

    mutable std::mutex mu_;
    std::vector<LangPack> packs_;
    size_t active_idx_ = 0;
    mutable std::unordered_map<std::string, std::string> missing_cache_;
};

inline const std::string& tr_s(const char* key) {
    return I18nManager::instance().tr(key);
}

inline const char* tr_c(const char* key) {
    return I18nManager::instance().tr(key).c_str();
}

inline std::string vformat_str(const char* fmt, va_list ap) {
    if (!fmt) return "";
    va_list ap2;
    va_copy(ap2, ap);
    char stack_buf[512];
    int n = std::vsnprintf(stack_buf, sizeof(stack_buf), fmt, ap);
    if (n >= 0 && (size_t)n < sizeof(stack_buf)) {
        va_end(ap2);
        return std::string(stack_buf, (size_t)n);
    }
    if (n < 0) {
        va_end(ap2);
        return std::string(fmt);
    }
    std::string dyn((size_t)n + 1, '\0');
    std::vsnprintf(&dyn[0], dyn.size(), fmt, ap2);
    va_end(ap2);
    dyn.resize((size_t)n);
    return dyn;
}

inline std::string trf(const char* key, ...) {
    const std::string& fmt = tr_s(key);
    va_list ap;
    va_start(ap, key);
    std::string out = vformat_str(fmt.c_str(), ap);
    va_end(ap);
    return out;
}

#ifdef _WIN32
inline std::wstring utf8_to_wide(const std::string& s) {
    if (s.empty()) return std::wstring();
    int wn = MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), nullptr, 0);
    if (wn <= 0) return std::wstring();
    std::wstring w((size_t)wn, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), &w[0], wn);
    return w;
}

inline std::string wide_to_utf8(const std::wstring& w) {
    if (w.empty()) return std::string();
    int un = WideCharToMultiByte(CP_UTF8, 0, w.data(), (int)w.size(), nullptr, 0, nullptr, nullptr);
    if (un <= 0) return std::string();
    std::string s((size_t)un, '\0');
    WideCharToMultiByte(CP_UTF8, 0, w.data(), (int)w.size(), &s[0], un, nullptr, nullptr);
    return s;
}

inline std::wstring tr_w(const char* key) {
    return utf8_to_wide(tr_s(key));
}

inline std::wstring trf_w(const char* key, ...) {
    const std::string& fmt = tr_s(key);
    va_list ap;
    va_start(ap, key);
    std::string out = vformat_str(fmt.c_str(), ap);
    va_end(ap);
    return utf8_to_wide(out);
}
#endif

#ifdef __OBJC__
inline NSString* tr_ns(const char* key) {
    const std::string& s = tr_s(key);
    return [NSString stringWithUTF8String:s.c_str()] ?: @"";
}

inline NSString* trf_ns(const char* key, ...) {
    const std::string& fmt = tr_s(key);
    va_list ap;
    va_start(ap, key);
    std::string out = vformat_str(fmt.c_str(), ap);
    va_end(ap);
    return [NSString stringWithUTF8String:out.c_str()] ?: @"";
}
#endif

// ============================================================================
// Thread-Safe Session Diagnostic Log
// ============================================================================

class SessionDiagLog {
public:
    static constexpr size_t kMaxSessionBytes = 8 * 1024 * 1024;
    static constexpr size_t kMaxPerTaskBytes = 2 * 1024 * 1024;

    static SessionDiagLog& instance() {
        static SessionDiagLog inst;
        return inst;
    }

    void init_session(const std::string& platform_info) {
        std::lock_guard<std::mutex> lk(mu_);
        if (initialized_) return;
        initialized_ = true;
        buf_.reserve(65536);
        append_unlocked("========================================================================\n");
        append_unlocked("NijiN.LiteBurstSuite (nijinLBS) v" NIJIN_VERSION " GUI Diagnostic Log\n");
        append_unlocked("Session Start : " + now_timestamp() + "\n");
        append_unlocked("Platform      : " + platform_info + "\n");
        append_unlocked("Language      : " + I18nManager::instance().active_code() + "\n");
        append_unlocked("========================================================================\n");
    }

    void log_event(const std::string& category, const std::string& msg) {
        std::lock_guard<std::mutex> lk(mu_);
        append_unlocked("[" + now_timestamp() + "] [" + category + "] " + msg + "\n");
    }

    int begin_task(const std::string& label, const std::string& cli_path, const std::vector<std::string>& args) {
        std::lock_guard<std::mutex> lk(mu_);
        int id = ++next_task_id_;
        task_bytes_[id] = 0;
        std::string cmd = quote_arg(cli_path);
        for (const auto& a : args) {
            cmd += " ";
            cmd += quote_arg(a);
        }
        append_unlocked("\n--- [TASK #" + std::to_string(id) + " START " + now_timestamp() + "] " + label + " ---\n");
        append_unlocked("CMD: " + cmd + "\n");
        return id;
    }

    void append_task_output(int task_id, const char* data, size_t len) {
        if (!data || len == 0) return;
        std::lock_guard<std::mutex> lk(mu_);
        size_t& used = task_bytes_[task_id];
        if (used >= kMaxPerTaskBytes) return;
        size_t take = std::min(len, kMaxPerTaskBytes - used);
        append_unlocked(std::string(data, take));
        used += take;
        if (used >= kMaxPerTaskBytes && take < len) {
            append_unlocked("\n[... task output capped at 2 MiB ...]\n");
        }
    }

    void end_task(int task_id, int exit_code, bool user_stopped = false) {
        std::lock_guard<std::mutex> lk(mu_);
        if (!buf_.empty() && buf_.back() != '\n') buf_.push_back('\n');
        append_unlocked("--- [TASK #" + std::to_string(task_id) + " END " + now_timestamp() +
                        "] exit_code=" + std::to_string(exit_code) +
                        (user_stopped ? " (stopped_by_user)" : "") + " ---\n");
        task_bytes_.erase(task_id);
    }

    std::string snapshot() const {
        std::lock_guard<std::mutex> lk(mu_);
        return buf_;
    }

    bool export_to_file(const std::string& utf8_path, std::string* err_out = nullptr) const {
        if (utf8_path.empty()) return false;
        std::string snap = snapshot();
#ifdef _WIN32
        std::wstring wp = utf8_to_wide(utf8_path);
        FILE* f = wp.empty() ? nullptr : _wfopen(wp.c_str(), L"wb");
#else
        FILE* f = std::fopen(utf8_path.c_str(), "wb");
#endif
        if (!f) {
            if (err_out) *err_out = "open_failed";
            return false;
        }
        size_t wr = snap.empty() ? 0 : std::fwrite(snap.data(), 1, snap.size(), f);
        bool ok = (wr == snap.size()) && (std::ferror(f) == 0);
        if (std::fclose(f) != 0) ok = false;
        if (!ok && err_out) *err_out = "write_failed";
        return ok;
    }

    static std::string default_filename() {
        std::time_t t = std::time(nullptr);
        std::tm tm_v{};
#ifdef _WIN32
        localtime_s(&tm_v, &t);
#else
        localtime_r(&t, &tm_v);
#endif
        char buf[64];
        std::snprintf(buf, sizeof(buf), "nijinLBS_diag_%04d%02d%02d_%02d%02d%02d.log",
                      tm_v.tm_year + 1900, tm_v.tm_mon + 1, tm_v.tm_mday,
                      tm_v.tm_hour, tm_v.tm_min, tm_v.tm_sec);
        return std::string(buf);
    }

    static std::string extract_failure_reason(const std::string& raw_output) {
        if (raw_output.empty()) return "";
        std::vector<std::string> lines;
        size_t pos = 0;
        while (pos < raw_output.size()) {
            size_t nl = raw_output.find('\n', pos);
            std::string ln = (nl == std::string::npos) ? raw_output.substr(pos) : raw_output.substr(pos, nl - pos);
            pos = (nl == std::string::npos) ? raw_output.size() : (nl + 1);
            ln = trim_ascii(ln);
            if (!ln.empty() && ln.front() != '{') lines.push_back(std::move(ln));
        }
        auto clean_suffix = [](std::string s) -> std::string {
            if (s.size() > 120) s = s.substr(0, 117) + "...";
            return " · " + s;
        };
        for (auto it = lines.rbegin(); it != lines.rend(); ++it) {
            const std::string& s = *it;
            if (s.find("[W") != std::string::npos ||
                s.find("Error:") != std::string::npos ||
                s.find("Error ") != std::string::npos ||
                s.find("Unpack error") != std::string::npos ||
                s.find("No exportable") != std::string::npos ||
                s.find("Fewer than") != std::string::npos ||
                s.find("Suitability") != std::string::npos ||
                s.find("additional reliably camera-aligned frames") != std::string::npos ||
                s.find("insufficient") != std::string::npos) {
                return clean_suffix(s);
            }
        }
        if (!lines.empty()) {
            const std::string& last = lines.back();
            if (last.find("[1/6]") == std::string::npos && last.find("[Cache]") == std::string::npos) {
                return clean_suffix(last);
            }
        }
        return "";
    }

private:
    SessionDiagLog() = default;

    static std::string now_timestamp() {
        std::time_t t = std::time(nullptr);
        std::tm tm_v{};
#ifdef _WIN32
        localtime_s(&tm_v, &t);
#else
        localtime_r(&t, &tm_v);
#endif
        char buf[64];
        std::snprintf(buf, sizeof(buf), "%04d-%02d-%02d %02d:%02d:%02d",
                      tm_v.tm_year + 1900, tm_v.tm_mon + 1, tm_v.tm_mday,
                      tm_v.tm_hour, tm_v.tm_min, tm_v.tm_sec);
        return std::string(buf);
    }

    static std::string quote_arg(const std::string& s) {
        bool need = s.empty();
        for (char c : s) {
            if (std::isspace((unsigned char)c) || c == '"') { need = true; break; }
        }
        if (!need) return s;
        std::string out = "\"";
        for (char c : s) {
            if (c == '"') out += "\\\"";
            else out.push_back(c);
        }
        out.push_back('"');
        return out;
    }

    void append_unlocked(const std::string& s) {
        if (s.empty()) return;
        if (buf_.size() + s.size() > kMaxSessionBytes && buf_.size() > 131072) {
            size_t head_keep = 32768;
            size_t tail_keep = kMaxSessionBytes / 2;
            if (buf_.size() > head_keep + tail_keep) {
                std::string next = buf_.substr(0, head_keep);
                next += "\n[... earlier session log lines trimmed ...]\n";
                next += buf_.substr(buf_.size() - tail_keep);
                buf_.swap(next);
            }
        }
        buf_ += s;
    }

    mutable std::mutex mu_;
    bool initialized_ = false;
    int next_task_id_ = 0;
    std::string buf_;
    std::unordered_map<int, size_t> task_bytes_;
};

inline SessionDiagLog& diag_log() {
    return SessionDiagLog::instance();
}

struct NibPreflightInfo {
    bool empty = true;
    bool valid = false;
    bool matched = false;
    uint32_t stage = 0;
    uint32_t frames = 0;
    uint32_t width = 0;
    uint32_t height = 0;
    uint32_t field_count = 0;
    std::string summary;
};

inline NibPreflightInfo inspect_nib_for_gui(
    const std::string& utf8_path, int expected_frames = 0, int expected_w = 0, int expected_h = 0
) {
    NibPreflightInfo info;
    if (utf8_path.empty()) return info;
    info.empty = false;
    std::ifstream fp(heraw_fs::from_utf8(utf8_path), std::ios::binary);
    if (!fp) {
        info.summary = trf("r2.err_fmt", "cannot open file");
        return info;
    }
    uint8_t hdr[64] = {0};
    fp.read(reinterpret_cast<char*>(hdr), 64);
    if (fp.gcount() != 64 || std::memcmp(hdr, "NIJBUS01", 8) != 0) {
        info.summary = trf("r2.err_fmt", "invalid NIJBUS01 magic");
        return info;
    }
    auto rd32 = [](const uint8_t* p) -> uint32_t {
        return static_cast<uint32_t>(p[0]) | (static_cast<uint32_t>(p[1]) << 8) |
               (static_cast<uint32_t>(p[2]) << 16) | (static_cast<uint32_t>(p[3]) << 24);
    };
    auto rd64 = [&](const uint8_t* p) -> uint64_t {
        return static_cast<uint64_t>(rd32(p)) | (static_cast<uint64_t>(rd32(p + 4)) << 32);
    };
    const uint32_t ver = rd32(hdr + 8);
    info.stage = rd32(hdr + 12);
    info.frames = rd32(hdr + 16);
    info.width = rd32(hdr + 20);
    info.height = rd32(hdr + 24);
    info.field_count = rd32(hdr + 28);
    const uint64_t meta_off = rd64(hdr + 40);
    const uint64_t meta_len = rd64(hdr + 48);
    if (ver != 1 || info.frames == 0 || info.field_count > 4096 || meta_len > (2u << 20)) {
        info.summary = trf("r2.err_fmt", "unsupported version/header bounds");
        return info;
    }
    for (uint32_t i = 0; i < info.field_count; ++i) {
        uint8_t desc[128] = {0};
        fp.read(reinterpret_cast<char*>(desc), 128);
        if (fp.gcount() != 128) {
            info.summary = trf("r2.err_fmt", "truncated descriptor table");
            return info;
        }
        const uint16_t orig = static_cast<uint16_t>(desc[44] | (desc[45] << 8));
        if (orig == 0) {
            info.summary = trf("r2.err_fmt", "external origin=0 rejected");
            return info;
        }
    }
    std::string manifest;
    if (meta_len > 0) {
        manifest.resize(static_cast<size_t>(meta_len));
        fp.seekg(static_cast<std::streamoff>(meta_off), std::ios::beg);
        fp.read(manifest.data(), static_cast<std::streamsize>(meta_len));
        if (fp.gcount() != static_cast<std::streamsize>(meta_len)) {
            info.summary = trf("r2.err_fmt", "truncated manifest");
            return info;
        }
    }
    info.valid = true;
    if (expected_frames > 0 && static_cast<int>(info.frames) != expected_frames) {
        char buf[96];
        std::snprintf(buf, sizeof(buf), "frames %u != %d", info.frames, expected_frames);
        info.summary = trf("r2.err_fmt", buf);
        return info;
    }
    if (expected_w > 0 && expected_h > 0 && info.width > 0 && info.height > 0 &&
        (static_cast<int>(info.width) != expected_w || static_cast<int>(info.height) != expected_h)) {
        char buf[96];
        std::snprintf(buf, sizeof(buf), "%ux%u != %dx%d", info.width, info.height, expected_w, expected_h);
        info.summary = trf("r2.err_fmt", buf);
        return info;
    }
    std::string roles;
    auto add_role = [&](const char* r) {
        if (!roles.empty()) roles += " / ";
        roles += r;
    };
    if (manifest.find("\"protect_mask\"") != std::string::npos) add_role("protect");
    if (manifest.find("\"emitter_mask\"") != std::string::npos) add_role("emitter");
    if (manifest.find("\"composites\"") != std::string::npos) add_role("composite");
    if (manifest.find("\"exclusions\"") != std::string::npos) add_role("exclude");
    if (manifest.find("\"align\"") != std::string::npos || manifest.find("\"layers\"") != std::string::npos) {
        add_role("passthrough");
    }
    if (roles.empty()) {
        char fbuf[48];
        std::snprintf(fbuf, sizeof(fbuf), "%u field(s)", info.field_count);
        roles = fbuf;
    }
    info.matched = (expected_frames > 0);
    info.summary = info.matched
        ? trf("r2.ok_fmt", static_cast<int>(info.frames), roles.c_str())
        : trf("r2.valid_fmt", static_cast<int>(info.frames), roles.c_str());
    return info;
}

} // namespace heraw_i18n
