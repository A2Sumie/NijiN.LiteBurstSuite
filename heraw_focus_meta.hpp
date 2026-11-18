// heraw_focus_meta.hpp -- Multi-vendor MakerNote focus distance, diopter uncertainty & optical DoF.
// Pure mathematical, deterministic, C++17 header-only, 7-bit ASCII, zero external dependencies.
#ifndef HERAW_FOCUS_META_HPP
#define HERAW_FOCUS_META_HPP

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

namespace heraw_focus {

enum class FocusSource : uint8_t {
    None = 0,
    SonyMakerNote9402 = 1,
    SonyMakerNote9404 = 2,
    SonyMakerNote9403 = 3,
    SonyMoreInfo0020 = 4,
    SonyMinolta0020 = 5,
    NikonLensData0800 = 6,
    NikonLensDataLegacy = 7,
    NikonManualFocus0085 = 8,
    CanonShotInfo = 9,
    CanonFileInfo = 10,
    ExifSubjectDistance = 11,
    OlympusMakerNote = 12,
    WvdcContainer = 13
};

struct FocusTelemetry {
    FocusSource source = FocusSource::None;
    bool valid = false;
    bool is_infinity = false;
    float distance_m = 0.0f;      // 1000000.0f when is_infinity
    float distance_low_m = 0.0f;  // lower confidence bound (closer)
    float distance_high_m = 0.0f; // upper confidence bound (farther, 1000000.0f if unbounded)
    float diopter_m1 = 0.0f;      // 1 / distance_m (0.0f at infinity)
    float sigma_diopter_m1 = 0.0f;
    float focal_nom_mm = 0.0f;
    float focal_opt_mm = 0.0f;
    float focus_step_closer = -1.0f; // strictly monotonic closer-is-larger ordinal
    uint16_t raw_position = 0;
    int32_t lens_position_abs = 0;
    bool has_lens_position_abs = false;

    // Optical DoF / blur quantities populated by finalize_optical_dof()
    float pixel_pitch_um = 0.0f;
    float coc_mm = 0.0f;
    float hyperfocal_m = 0.0f;
    float dof_near_m = 0.0f;
    float dof_far_m = 0.0f;
    float infinity_coc_1x_px = 0.0f;
    bool infinity_in_dof = false;
    bool clearly_near_macro = false;
};

namespace detail {

inline uint16_t rd_be16(const uint8_t* p) {
    return static_cast<uint16_t>((static_cast<uint16_t>(p[0]) << 8) | p[1]);
}

inline uint32_t rd_be32(const uint8_t* p) {
    return (static_cast<uint32_t>(p[0]) << 24) |
           (static_cast<uint32_t>(p[1]) << 16) |
           (static_cast<uint32_t>(p[2]) << 8) |
           static_cast<uint32_t>(p[3]);
}

inline uint16_t rd_le16(const uint8_t* p) {
    return static_cast<uint16_t>(p[0] | (static_cast<uint16_t>(p[1]) << 8));
}

inline uint32_t rd_le32(const uint8_t* p) {
    return static_cast<uint32_t>(p[0]) |
           (static_cast<uint32_t>(p[1]) << 8) |
           (static_cast<uint32_t>(p[2]) << 16) |
           (static_cast<uint32_t>(p[3]) << 24);
}

inline uint16_t rd_u16_e(const uint8_t* p, bool be) {
    return be ? rd_be16(p) : rd_le16(p);
}

inline uint32_t rd_u32_e(const uint8_t* p, bool be) {
    return be ? rd_be32(p) : rd_le32(p);
}

inline uint32_t tiff_unit_bytes(uint16_t t) {
    switch (t) {
        case 1: case 2: case 6: case 7: return 1u;
        case 3: case 8: return 2u;
        case 4: case 9: case 11: case 13: return 4u;
        case 5: case 10: case 12: return 8u;
        default: return 0u;
    }
}

inline std::string to_upper_ascii(const std::string& s) {
    std::string out = s;
    for (char& c : out) {
        c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    }
    return out;
}

inline bool starts_with_ci(const std::string& s, const char* prefix) {
    size_t n = std::strlen(prefix);
    if (s.size() < n) return false;
    for (size_t i = 0; i < n; ++i) {
        if (std::tolower(static_cast<unsigned char>(s[i])) !=
            std::tolower(static_cast<unsigned char>(prefix[i]))) {
            return false;
        }
    }
    return true;
}

inline bool ends_with_ci(const std::string& s, const char* suffix) {
    size_t n = std::strlen(suffix);
    if (s.size() < n) return false;
    size_t off = s.size() - n;
    for (size_t i = 0; i < n; ++i) {
        if (std::tolower(static_cast<unsigned char>(s[off + i])) !=
            std::tolower(static_cast<unsigned char>(suffix[i]))) {
            return false;
        }
    }
    return true;
}

inline std::string clean_ascii(const uint8_t* p, size_t n) {
    size_t len = 0;
    while (len < n && p[len] != '\0') ++len;
    while (len > 0 && p[len - 1] == ' ') --len;
    return std::string(reinterpret_cast<const char*>(p), len);
}

struct IfdEntry {
    uint16_t tag = 0;
    uint16_t type = 0;
    uint32_t count = 0;
    std::vector<uint8_t> data; // raw on-disk byte order preserved for binary MakerNotes
};

template <typename ReadAtFn>
inline bool read_ifd_entries(
    ReadAtFn&& read_at,
    uint64_t base_off,
    bool be,
    uint32_t ifd_rel_off,
    std::vector<IfdEntry>& out) {
    out.clear();
    uint8_t cnt_buf[2] = {};
    if (!read_at(base_off + ifd_rel_off, cnt_buf, 2u)) return false;
    uint16_t n_ent = rd_u16_e(cnt_buf, be);
    if (n_ent == 0u || n_ent > 512u) return false;
    std::vector<uint8_t> dir(static_cast<size_t>(n_ent) * 12u);
    if (!read_at(base_off + ifd_rel_off + 2u, dir.data(), dir.size())) return false;

    out.reserve(n_ent);
    for (uint16_t i = 0; i < n_ent; ++i) {
        const uint8_t* e = dir.data() + static_cast<size_t>(i) * 12u;
        uint16_t tag = rd_u16_e(e + 0, be);
        uint16_t typ = rd_u16_e(e + 2, be);
        uint32_t cnt = rd_u32_e(e + 4, be);
        uint32_t unit = tiff_unit_bytes(typ);
        if (unit == 0u || cnt == 0u || cnt > 131072u) continue;
        uint64_t blen = static_cast<uint64_t>(unit) * static_cast<uint64_t>(cnt);
        if (blen > 262144ULL) continue;

        IfdEntry ent;
        ent.tag = tag;
        ent.type = typ;
        ent.count = cnt;
        ent.data.resize(static_cast<size_t>(blen));
        if (blen <= 4u) {
            std::memcpy(ent.data.data(), e + 8, static_cast<size_t>(blen));
        } else {
            uint32_t val_rel = rd_u32_e(e + 8, be);
            if (!read_at(base_off + val_rel, ent.data.data(), static_cast<size_t>(blen))) continue;
        }
        out.push_back(std::move(ent));
    }
    return true;
}

constexpr uint8_t sony_pow_mod_249(uint8_t base, uint8_t exp) {
    uint32_t res = 1u;
    uint32_t b = base % 249u;
    uint8_t e = exp;
    while (e > 0u) {
        if (e & 1u) res = (res * b) % 249u;
        b = (b * b) % 249u;
        e >>= 1u;
    }
    return static_cast<uint8_t>(res);
}

constexpr std::array<uint8_t, 256> make_sony_94xx_inv_lut() {
    std::array<uint8_t, 256> lut{};
    for (uint32_t c = 0; c < 249u; ++c) {
        lut[c] = sony_pow_mod_249(static_cast<uint8_t>(c), 55u);
    }
    for (uint32_t c = 249u; c < 256u; ++c) {
        lut[c] = static_cast<uint8_t>(c);
    }
    return lut;
}

inline bool parse_nikon_serial_key(const std::string& serial_str, const std::string& model, uint8_t& out_key) {
    if (!serial_str.empty()) {
        bool all_digits = true;
        uint32_t acc = 0u;
        for (char ch : serial_str) {
            if (ch >= '0' && ch <= '9') {
                acc = (acc * 10u + static_cast<uint32_t>(ch - '0')) & 0xFFu;
            } else {
                all_digits = false;
                break;
            }
        }
        if (all_digits) {
            out_key = static_cast<uint8_t>(acc);
            return true;
        }
    }
    if (ends_with_ci(model, "D50")) {
        out_key = 0x22u;
        return true;
    }
    if (!model.empty()) {
        out_key = 0x60u;
        return true;
    }
    return false;
}

} // namespace detail

inline void set_focus_infinity(FocusTelemetry& ft, FocusSource src) {
    ft.valid = true;
    ft.is_infinity = true;
    ft.source = src;
    ft.distance_m = 1000000.0f;
    ft.distance_low_m = 0.0f;
    ft.distance_high_m = 1000000.0f;
    ft.diopter_m1 = 0.0f;
    ft.sigma_diopter_m1 = 0.02f;
}

inline void set_focus_distance_rel(
    FocusTelemetry& ft,
    FocusSource src,
    double dist_m,
    double rel_sig) {
    if (!(dist_m > 0.0) || !std::isfinite(dist_m)) return;
    if (dist_m >= 999999.0) {
        set_focus_infinity(ft, src);
        return;
    }
    const double rs = std::max(0.02, std::min(0.85, rel_sig));
    ft.valid = true;
    ft.is_infinity = false;
    ft.source = src;
    ft.distance_m = static_cast<float>(dist_m);
    ft.distance_low_m = static_cast<float>(dist_m / (1.0 + rs));
    ft.distance_high_m = static_cast<float>(dist_m / (1.0 - rs));
    ft.diopter_m1 = static_cast<float>(1.0 / dist_m);
    ft.sigma_diopter_m1 = static_cast<float>(rs / dist_m);
}

inline void set_focus_bounds(
    FocusTelemetry& ft,
    FocusSource src,
    double d_lo_m,
    double d_hi_m,
    bool hi_inf) {
    if (hi_inf) {
        if (!(d_lo_m > 0.0) || d_lo_m >= 50.0) {
            set_focus_infinity(ft, src);
            return;
        }
        ft.valid = true;
        ft.is_infinity = false;
        ft.source = src;
        ft.distance_low_m = static_cast<float>(d_lo_m);
        ft.distance_high_m = 1000000.0f;
        ft.distance_m = static_cast<float>(2.0 * d_lo_m);
        ft.diopter_m1 = static_cast<float>(0.5 / d_lo_m);
        ft.sigma_diopter_m1 = static_cast<float>(0.5 / d_lo_m);
        return;
    }
    if (!(d_lo_m > 0.0) || !(d_hi_m > 0.0) || !std::isfinite(d_lo_m) || !std::isfinite(d_hi_m)) return;
    double lo = std::min(d_lo_m, d_hi_m);
    double hi = std::max(d_lo_m, d_hi_m);
    if (hi - lo < 0.04 * hi) {
        lo = std::max(0.01, lo * 0.95);
        hi = hi * 1.05;
    }
    const double d_mid = 2.0 / (1.0 / lo + 1.0 / hi);
    ft.valid = true;
    ft.is_infinity = false;
    ft.source = src;
    ft.distance_low_m = static_cast<float>(lo);
    ft.distance_high_m = static_cast<float>(hi);
    ft.distance_m = static_cast<float>(d_mid);
    ft.diopter_m1 = static_cast<float>(0.5 * (1.0 / lo + 1.0 / hi));
    ft.sigma_diopter_m1 = static_cast<float>(0.5 * (1.0 / lo - 1.0 / hi));
}

inline void apply_exif_subject_distance(FocusTelemetry& ft, uint32_t num, uint32_t den) {
    if (ft.valid) return;
    if (num == 0xFFFFFFFFu && den > 0u) {
        set_focus_infinity(ft, FocusSource::ExifSubjectDistance);
        return;
    }
    if (num > 0u && den > 0u) {
        const double d = static_cast<double>(num) / static_cast<double>(den);
        if (d >= 999999.0) {
            set_focus_infinity(ft, FocusSource::ExifSubjectDistance);
        } else {
            set_focus_distance_rel(ft, FocusSource::ExifSubjectDistance, d, 0.15);
        }
    }
}

template <typename ReadAtFn>
inline bool parse_sony_makernote(
    ReadAtFn&& read_at,
    uint64_t tiff_base,
    bool be,
    uint64_t mn_file_off,
    const std::string& make,
    const std::string& model,
    float exif_focal_mm,
    FocusTelemetry& ft) {
    const std::string mu = detail::to_upper_ascii(make);
    const std::string ml = detail::to_upper_ascii(model);
    const bool is_sony = (mu.find("SONY") != std::string::npos ||
                          mu.find("MINOLTA") != std::string::npos ||
                          detail::starts_with_ci(ml, "ILCE-") ||
                          detail::starts_with_ci(ml, "ILCA-") ||
                          detail::starts_with_ci(ml, "ILME-") ||
                          detail::starts_with_ci(ml, "NEX-") ||
                          detail::starts_with_ci(ml, "SLT-") ||
                          detail::starts_with_ci(ml, "DSLR-A") ||
                          detail::starts_with_ci(ml, "ZV-"));
    if (!is_sony || mn_file_off <= tiff_base) return false;

    uint8_t mn_hdr[12] = {};
    if (!read_at(mn_file_off, mn_hdr, sizeof(mn_hdr))) return false;
    const uint64_t mn_ifd_abs = mn_file_off + (std::memcmp(mn_hdr, "SONY", 4) == 0 ? 12u : 0u);
    if (mn_ifd_abs < tiff_base || mn_ifd_abs - tiff_base > 0xFFFFFFFFULL) return false;

    std::vector<detail::IfdEntry> tags;
    if (!detail::read_ifd_entries(read_at, tiff_base, be, static_cast<uint32_t>(mn_ifd_abs - tiff_base), tags)) {
        return false;
    }

    static constexpr auto kInvLut = detail::make_sony_94xx_inv_lut();
    const detail::IfdEntry* tag_9402 = nullptr;
    const detail::IfdEntry* tag_9403 = nullptr;
    const detail::IfdEntry* tag_9404 = nullptr;
    const detail::IfdEntry* tag_0020 = nullptr;
    for (const auto& tv : tags) {
        if (tv.tag == 0x9402) tag_9402 = &tv;
        else if (tv.tag == 0x9403) tag_9403 = &tv;
        else if (tv.tag == 0x9404) tag_9404 = &tv;
        else if (tv.tag == 0x0020) tag_0020 = &tv;
    }

    float f_nom = 0.0f;
    float f_opt = 0.0f;
    uint16_t focus_pos = 0;
    bool is_log_pos2 = false;
    FocusSource cand_src = FocusSource::None;

    const bool is_slt_ilca = detail::starts_with_ci(ml, "SLT-") ||
                             detail::starts_with_ci(ml, "HV") ||
                             detail::starts_with_ci(ml, "ILCA-");
    const bool is_dsc = detail::starts_with_ci(ml, "DSC-") || detail::starts_with_ci(ml, "STELLAR");

    // Branch A: modern E-mount Tag 0x9402
    if (!is_slt_ilca && !is_dsc && tag_9402 && tag_9402->data.size() >= 0x2Eu) {
        const auto& raw = tag_9402->data;
        if (raw[0] != 0x05u && raw[0] != 0xFFu) {
            uint8_t dec[0x30] = {};
            for (size_t i = 0; i < 0x30u; ++i) dec[i] = kInvLut[raw[i]];
            if (dec[0] != 0x00u) {
                const uint16_t u_nom = detail::rd_le16(dec + 0x24);
                const uint16_t u_opt = detail::rd_le16(dec + 0x28);
                if (u_nom > 0u && u_nom < 60000u) f_nom = static_cast<float>(u_nom) * 0.1f;
                if (u_opt > 0u && u_opt < 60000u) f_opt = static_cast<float>(u_opt) * 0.1f;
                focus_pos = dec[0x2D];
                is_log_pos2 = (focus_pos > 0u);
                if (focus_pos > 0u) cand_src = FocusSource::SonyMakerNote9402;
            }
        }
    }

    // Branch B: SLT / ILCA Tag 0x9404 / 0x9403
    if (focus_pos == 0u && is_slt_ilca && tag_9404 && tag_9404->data.size() > 0x20u) {
        const auto& raw = tag_9404->data;
        const uint8_t d0 = kInvLut[raw[0]];
        const uint8_t d3 = kInvLut[raw[3]];
        if ((d0 == 9u || d0 == 12u || d0 == 13u || d0 == 15u || d0 == 16u) && d3 == 2u) {
            focus_pos = kInvLut[raw[0x20]];
            is_log_pos2 = (focus_pos > 0u);
            if (focus_pos > 0u) cand_src = FocusSource::SonyMakerNote9404;
        }
    }
    if (focus_pos == 0u && is_slt_ilca && tag_9403 && tag_9403->data.size() > 0x20u) {
        focus_pos = kInvLut[tag_9403->data[0x20]];
        is_log_pos2 = (focus_pos > 0u);
        if (focus_pos > 0u) cand_src = FocusSource::SonyMakerNote9403;
    }

    // Branch C / D: legacy MoreInfo / FocusInfo Tag 0x0020
    if (focus_pos == 0u && tag_0020) {
        const auto& b20 = tag_0020->data;
        const size_t cnt = b20.size();
        if ((cnt == 19154u || cnt == 19148u) && detail::starts_with_ci(ml, "DSLR-A")) {
            if (0x09BBu < cnt) {
                focus_pos = b20[0x09BB];
                is_log_pos2 = false;
                if (focus_pos > 0u) cand_src = FocusSource::SonyMinolta0020;
            }
        } else if (cnt >= 64u && cnt != 19154u && cnt != 19148u) {
            const uint16_t num = detail::rd_le16(b20.data());
            if (num >= 1u && num <= 50u && 4u + static_cast<size_t>(num) * 4u <= cnt) {
                for (uint16_t i = 0; i < num; ++i) {
                    const uint16_t stag = detail::rd_le16(b20.data() + 4u + i * 4u);
                    const uint16_t soff = detail::rd_le16(b20.data() + 6u + i * 4u);
                    if (stag == 0x0001u && static_cast<size_t>(soff) + 0x30u <= cnt) {
                        size_t poff = 0x2Fu;
                        if (detail::starts_with_ci(ml, "DSLR-A450") ||
                            detail::starts_with_ci(ml, "DSLR-A500") ||
                            detail::starts_with_ci(ml, "DSLR-A550")) {
                            poff = 0x29u;
                        } else if (ml == "NEX-3" || ml == "NEX-5" || ml == "NEX-5C") {
                            poff = 0x2Bu;
                        }
                        focus_pos = b20[static_cast<size_t>(soff) + poff];
                        is_log_pos2 = (focus_pos > 0u);
                        if (focus_pos > 0u) cand_src = FocusSource::SonyMoreInfo0020;
                        break;
                    }
                }
            }
        }
    }

    if (f_nom > 0.5f && ft.focal_nom_mm <= 0.0f) ft.focal_nom_mm = f_nom;
    const float f_ref = (f_nom > 0.5f) ? f_nom : exif_focal_mm;
    float f_eff = (exif_focal_mm > 0.5f) ? exif_focal_mm : f_nom;
    if (f_opt > 0.5f && (f_ref <= 0.5f || (f_opt >= 0.5f * f_ref && f_opt <= 1.5f * f_ref))) {
        ft.focal_opt_mm = f_opt;
        f_eff = f_opt;
    } else if (f_nom > 0.5f && (exif_focal_mm <= 0.5f ||
                                (f_nom >= 0.5f * exif_focal_mm && f_nom <= 1.5f * exif_focal_mm))) {
        f_eff = f_nom;
    }

    if (focus_pos == 0u || cand_src == FocusSource::None) return false;
    ft.raw_position = focus_pos;
    // Closer focus has strictly larger step ordinal across Pos2 / linear encoders.
    ft.focus_step_closer = static_cast<float>(256u - std::min<uint16_t>(255u, focus_pos));

    if (is_log_pos2) {
        if (focus_pos >= 255u) {
            set_focus_infinity(ft, cand_src);
            return true;
        }
        if (f_eff > 0.5f) {
            const double d = (std::pow(2.0, static_cast<double>(focus_pos) / 16.0 - 5.0) + 1.0) *
                             (static_cast<double>(f_eff) / 1000.0);
            set_focus_distance_rel(ft, cand_src, d, 0.12);
            return ft.valid;
        }
    } else {
        if (focus_pos >= 128u) {
            set_focus_infinity(ft, cand_src);
            return true;
        }
        if (f_eff > 0.5f) {
            const double d = static_cast<double>(focus_pos) * static_cast<double>(f_eff) / 1000.0;
            const double rel_sig = std::max(0.18, 1.5 / static_cast<double>(focus_pos));
            set_focus_distance_rel(ft, cand_src, d, rel_sig);
            return ft.valid;
        }
    }
    return false;
}

inline bool decrypt_nikon_lens_data(
    const uint8_t* src,
    size_t len,
    const std::string& serial_str,
    uint32_t shutter_count,
    bool has_shutter_count,
    const std::string& model,
    std::vector<uint8_t>& dec_out) {
    if (!src || len < 8u) return false;
    static constexpr uint8_t kXlat0[256] = {
        0xc1,0xbf,0x6d,0x0d,0x59,0xc5,0x13,0x9d,0x83,0x61,0x6b,0x4f,0xc7,0x7f,0x3d,0x3d,
        0x53,0x59,0xe3,0xc7,0xe9,0x2f,0x95,0xa7,0x95,0x1f,0xdf,0x7f,0x2b,0x29,0xc7,0x0d,
        0xdf,0x07,0xef,0x71,0x89,0x3d,0x13,0x3d,0x3b,0x13,0xfb,0x0d,0x89,0xc1,0x65,0x1f,
        0xb3,0x0d,0x6b,0x29,0xe3,0xfb,0xef,0xa3,0x6b,0x47,0x7f,0x95,0x35,0xa7,0x47,0x4f,
        0xc7,0xf1,0x59,0x95,0x35,0x11,0x29,0x61,0xf1,0x3d,0xb3,0x2b,0x0d,0x43,0x89,0xc1,
        0x9d,0x9d,0x89,0x65,0xf1,0xe9,0xdf,0xbf,0x3d,0x7f,0x53,0x97,0xe5,0xe9,0x95,0x17,
        0x1d,0x3d,0x8b,0xfb,0xc7,0xe3,0x67,0xa7,0x07,0xf1,0x71,0xa7,0x53,0xb5,0x29,0x89,
        0xe5,0x2b,0xa7,0x17,0x29,0xe9,0x4f,0xc5,0x65,0x6d,0x6b,0xef,0x0d,0x89,0x49,0x2f,
        0xb3,0x43,0x53,0x65,0x1d,0x49,0xa3,0x13,0x89,0x59,0xef,0x6b,0xef,0x65,0x1d,0x0b,
        0x59,0x13,0xe3,0x4f,0x9d,0xb3,0x29,0x43,0x2b,0x07,0x1d,0x95,0x59,0x59,0x47,0xfb,
        0xe5,0xe9,0x61,0x47,0x2f,0x35,0x7f,0x17,0x7f,0xef,0x7f,0x95,0x95,0x71,0xd3,0xa3,
        0x0b,0x71,0xa3,0xad,0x0b,0x3b,0xb5,0xfb,0xa3,0xbf,0x4f,0x83,0x1d,0xad,0xe9,0x2f,
        0x71,0x65,0xa3,0xe5,0x07,0x35,0x3d,0x0d,0xb5,0xe9,0xe5,0x47,0x3b,0x9d,0xef,0x35,
        0xa3,0xbf,0xb3,0xdf,0x53,0xd3,0x97,0x53,0x49,0x71,0x07,0x35,0x61,0x71,0x2f,0x43,
        0x2f,0x11,0xdf,0x17,0x97,0xfb,0x95,0x3b,0x7f,0x6b,0xd3,0x25,0xbf,0xad,0xc7,0xc5,
        0xc5,0xb5,0x8b,0xef,0x2f,0xd3,0x07,0x6b,0x25,0x49,0x95,0x25,0x49,0x6d,0x71,0xc7
    };
    static constexpr uint8_t kXlat1[256] = {
        0xa7,0xbc,0xc9,0xad,0x91,0xdf,0x85,0xe5,0xd4,0x78,0xd5,0x17,0x46,0x7c,0x29,0x4c,
        0x4d,0x03,0xe9,0x25,0x68,0x11,0x86,0xb3,0xbd,0xf7,0x6f,0x61,0x22,0xa2,0x26,0x34,
        0x2a,0xbe,0x1e,0x46,0x14,0x68,0x9d,0x44,0x18,0xc2,0x40,0xf4,0x7e,0x5f,0x1b,0xad,
        0x0b,0x94,0xb6,0x67,0xb4,0x0b,0xe1,0xea,0x95,0x9c,0x66,0xdc,0xe7,0x5d,0x6c,0x05,
        0xda,0xd5,0xdf,0x7a,0xef,0xf6,0xdb,0x1f,0x82,0x4c,0xc0,0x68,0x47,0xa1,0xbd,0xee,
        0x39,0x50,0x56,0x4a,0xdd,0xdf,0xa5,0xf8,0xc6,0xda,0xca,0x90,0xca,0x01,0x42,0x9d,
        0x8b,0x0c,0x73,0x43,0x75,0x05,0x94,0xde,0x24,0xb3,0x80,0x34,0xe5,0x2c,0xdc,0x9b,
        0x3f,0xca,0x33,0x45,0xd0,0xdb,0x5f,0xf5,0x52,0xc3,0x21,0xda,0xe2,0x22,0x72,0x6b,
        0x3e,0xd0,0x5b,0xa8,0x87,0x8c,0x06,0x5d,0x0f,0xdd,0x09,0x19,0x93,0xd0,0xb9,0xfc,
        0x8b,0x0f,0x84,0x60,0x33,0x1c,0x9b,0x45,0xf1,0xf0,0xa3,0x94,0x3a,0x12,0x77,0x33,
        0x4d,0x44,0x78,0x28,0x3c,0x9e,0xfd,0x65,0x57,0x16,0x94,0x6b,0xfb,0x59,0xd0,0xc8,
        0x22,0x36,0xdb,0xd2,0x63,0x98,0x43,0xa1,0x04,0x87,0x86,0xf7,0xa6,0x26,0xbb,0xd6,
        0x59,0x4d,0xbf,0x6a,0x2e,0xaa,0x2b,0xef,0xe6,0x78,0xb6,0x4e,0xe0,0x2f,0xdc,0x7c,
        0xbe,0x57,0x19,0x32,0x7e,0x2a,0xd0,0xb8,0xba,0x29,0x00,0x3c,0x52,0x7d,0xa8,0x49,
        0x3b,0x2d,0xeb,0x25,0x49,0xfa,0xa3,0xaa,0x39,0xa7,0xc5,0xa7,0x50,0x11,0x36,0xfb,
        0xc6,0x67,0x4a,0xf5,0xa5,0x12,0x65,0x7e,0xb0,0xdf,0xaf,0x4e,0xb3,0x61,0x7f,0x2f
    };

    const bool need_decrypt = (src[0] == '0' && (src[1] == '2' || src[1] == '4' || src[1] == '8'));
    dec_out.assign(src, src + len);
    if (!need_decrypt) {
        return (src[0] == '0' && src[1] == '1');
    }
    uint8_t s_key = 0u;
    if (!has_shutter_count || !detail::parse_nikon_serial_key(serial_str, model, s_key)) {
        return false;
    }
    const uint8_t c_key = static_cast<uint8_t>(
        ((shutter_count >> 24) ^ (shutter_count >> 16) ^ (shutter_count >> 8) ^ shutter_count) & 0xFFu);
    const uint8_t ci = kXlat0[s_key];
    uint8_t cj = kXlat1[c_key];
    uint8_t ck = 0x60u;
    for (size_t i = 4u; i < dec_out.size(); ++i) {
        cj = static_cast<uint8_t>((static_cast<uint32_t>(cj) + static_cast<uint32_t>(ci) * static_cast<uint32_t>(ck)) & 0xFFu);
        ck = static_cast<uint8_t>((static_cast<uint32_t>(ck) + 1u) & 0xFFu);
        dec_out[i] ^= cj;
    }
    return true;
}

inline bool decode_nikon_lens_data(
    const uint8_t* raw_ld,
    size_t len,
    const std::string& serial_str,
    uint32_t shutter_count,
    bool has_shutter_count,
    const std::string& model,
    FocusTelemetry& ft) {
    std::vector<uint8_t> dec;
    if (!decrypt_nikon_lens_data(raw_ld, len, serial_str, shutter_count, has_shutter_count, model, dec)) {
        return false;
    }
    if (dec.size() < 12u) return false;

    const char v0 = static_cast<char>(dec[0]);
    const char v1 = static_cast<char>(dec[1]);
    const char v2 = static_cast<char>(dec[2]);
    const char v3 = static_cast<char>(dec[3]);

    if (v0 == '0' && v1 == '8' && v2 == '0' && (v3 == '0' || v3 == '1' || v3 == '2') && dec.size() >= 0x60u) {
        bool has_new = false;
        for (size_t i = 0x30u; i <= 0x3Eu; ++i) {
            if (dec[i] != 0u) { has_new = true; break; }
        }
        if (has_new && dec[0x35] == 1u) {
            const uint16_t u16_fl = detail::rd_le16(dec.data() + 0x3C);
            if (ft.focal_nom_mm <= 0.0f && u16_fl > 0u && u16_fl < 3000u) {
                ft.focal_nom_mm = static_cast<float>(u16_fl);
            }
            const uint8_t range_w = dec[0x4C];
            const uint16_t u16_fd = detail::rd_le16(dec.data() + 0x4E);
            const uint8_t drive_end = dec[0x56];
            const uint8_t steps_from_inf = dec[0x58];
            const int32_t lpa = static_cast<int32_t>(detail::rd_le32(dec.data() + 0x5A));
            ft.lens_position_abs = lpa;
            ft.has_lens_position_abs = true;
            ft.raw_position = u16_fd;
            ft.focus_step_closer = std::max(1.0f, 100000.0f + static_cast<float>(lpa));
            if (steps_from_inf == 0u && (lpa <= 0 || drive_end == 2u)) {
                set_focus_infinity(ft, FocusSource::NikonLensData0800);
                return true;
            }
            if (u16_fd > 0u) {
                const double d = std::pow(2.0, (static_cast<double>(u16_fd) / 256.0 - 80.0) / 12.0);
                const double rel_sig = std::max(0.05, std::min(0.35, 0.04 + 0.008 * static_cast<double>(range_w)));
                set_focus_distance_rel(ft, FocusSource::NikonLensData0800, d, rel_sig);
                return ft.valid;
            }
            return false;
        }
        bool has_old = false;
        for (size_t i = 0x04u; i <= 0x12u; ++i) {
            if (dec[i] != 0u) { has_old = true; break; }
        }
        if (has_old) {
            const uint8_t v = dec[0x0B];
            ft.raw_position = v;
            if (v > 0u) ft.focus_step_closer = static_cast<float>(256u - std::min<uint8_t>(255u, v));
            if (v == 255u) {
                set_focus_infinity(ft, FocusSource::NikonLensDataLegacy);
                return true;
            }
            if (v > 0u) {
                const double d = 0.01 * std::pow(10.0, static_cast<double>(v) / 40.0);
                set_focus_distance_rel(ft, FocusSource::NikonLensDataLegacy, d, 0.30);
                return ft.valid;
            }
        }
        return false;
    }

    size_t legacy_off = 0u;
    if (v0 == '0' && v1 == '2' && v2 == '0' && v3 == '4' && dec.size() > 0x0Au) {
        legacy_off = 0x0Au;
    } else if (((v0 == '0' && v1 == '2' && v2 == '0' && (v3 >= '1' && v3 <= '3')) ||
                (v0 == '0' && v1 == '1' && v2 == '0' && v3 == '1')) &&
               dec.size() > 0x09u) {
        legacy_off = 0x09u;
    } else if (v0 == '0' && v1 == '1' && v2 == '0' && v3 == '0' && dec.size() > 0x08u) {
        legacy_off = 0x08u;
    }
    if (legacy_off > 0u) {
        const uint8_t v = dec[legacy_off];
        ft.raw_position = v;
        if (v > 0u) ft.focus_step_closer = static_cast<float>(256u - std::min<uint8_t>(255u, v));
        if (v == 255u) {
            set_focus_infinity(ft, FocusSource::NikonLensDataLegacy);
            return true;
        }
        if (v > 0u) {
            const double d = 0.01 * std::pow(10.0, static_cast<double>(v) / 40.0);
            set_focus_distance_rel(ft, FocusSource::NikonLensDataLegacy, d, 0.30);
            return ft.valid;
        }
    }
    return false;
}

template <typename ReadAtFn>
inline bool parse_nikon_makernote(
    ReadAtFn&& read_at,
    uint64_t tiff_base,
    bool parent_be,
    uint64_t mn_file_off,
    const std::string& make,
    const std::string& model,
    const std::string& body_serial,
    FocusTelemetry& ft) {
    const std::string mu = detail::to_upper_ascii(make);
    if (mu.find("NIKON") == std::string::npos && !detail::starts_with_ci(model, "NIKON")) {
        return false;
    }
    uint8_t hdr[18] = {};
    if (!read_at(mn_file_off, hdr, sizeof(hdr))) return false;

    uint64_t mn_base = tiff_base;
    bool mn_be = parent_be;
    uint32_t ifd_rel = 0u;
    if (std::memcmp(hdr, "Nikon\0", 6) == 0) {
        const bool is_be = (hdr[10] == 'M' && hdr[11] == 'M');
        const bool is_le = (hdr[10] == 'I' && hdr[11] == 'I');
        if ((!is_be && !is_le) || detail::rd_u16_e(hdr + 12, is_be) != 42u) return false;
        mn_base = mn_file_off + 10u;
        mn_be = is_be;
        ifd_rel = detail::rd_u32_e(hdr + 14, mn_be);
    } else {
        if (mn_file_off < tiff_base || mn_file_off - tiff_base > 0xFFFFFFFFULL) return false;
        ifd_rel = static_cast<uint32_t>(mn_file_off - tiff_base);
    }

    std::vector<detail::IfdEntry> tags;
    if (!detail::read_ifd_entries(read_at, mn_base, mn_be, ifd_rel, tags)) return false;

    std::string serial = body_serial;
    uint32_t shutter_count = 0u;
    bool has_shutter = false;
    double manual_dist = 0.0;
    const detail::IfdEntry* lens_data = nullptr;

    for (const auto& tv : tags) {
        if (tv.tag == 0x001D && tv.type == 2u && !tv.data.empty()) {
            std::string s = detail::clean_ascii(tv.data.data(), tv.data.size());
            if (!s.empty()) serial = s;
        } else if (tv.tag == 0x00A7) {
            if (tv.type == 4u && tv.data.size() >= 4u) {
                shutter_count = detail::rd_u32_e(tv.data.data(), mn_be);
                has_shutter = true;
            } else if (tv.type == 3u && tv.data.size() >= 2u) {
                shutter_count = detail::rd_u16_e(tv.data.data(), mn_be);
                has_shutter = true;
            }
        } else if (tv.tag == 0x0085 && tv.type == 5u && tv.data.size() >= 8u) {
            const uint32_t num = detail::rd_u32_e(tv.data.data(), mn_be);
            const uint32_t den = detail::rd_u32_e(tv.data.data() + 4, mn_be);
            if (num > 0u && den > 0u) manual_dist = static_cast<double>(num) / static_cast<double>(den);
        } else if (tv.tag == 0x0098) {
            lens_data = &tv;
        }
    }

    if (lens_data && !lens_data->data.empty()) {
        if (decode_nikon_lens_data(lens_data->data.data(), lens_data->data.size(),
                                   serial, shutter_count, has_shutter, model, ft)) {
            return true;
        }
    }
    if (manual_dist > 0.0 && std::isfinite(manual_dist)) {
        set_focus_distance_rel(ft, FocusSource::NikonManualFocus0085, manual_dist, 0.25);
        return ft.valid;
    }
    return false;
}

inline bool parse_canon_ifd_tags(
    const std::vector<detail::IfdEntry>& tags,
    bool be,
    FocusTelemetry& ft) {
    auto apply_canon_pair = [&](uint16_t upper, uint16_t lower, FocusSource src) -> bool {
        if (upper == 0u) return false;
        ft.raw_position = upper;
        ft.focus_step_closer = (upper >= 65535u) ? 1.0f : (100000.0f / static_cast<float>(upper));
        if (upper >= 65535u) {
            const bool lo_inf = (lower == 0u || lower >= 65535u || (static_cast<double>(lower) * 0.01) >= 50.0);
            set_focus_bounds(ft, src, static_cast<double>(lower) * 0.01, 1000000.0, true);
            if (lo_inf) ft.is_infinity = true;
            return ft.valid;
        }
        const double d_hi = static_cast<double>(upper) * 0.01;
        const double d_lo = (lower > 0u && lower <= upper) ? (static_cast<double>(lower) * 0.01) : (0.85 * d_hi);
        set_focus_bounds(ft, src, d_lo, d_hi, false);
        return ft.valid;
    };

    const detail::IfdEntry* shot_info = nullptr;
    const detail::IfdEntry* file_info = nullptr;
    for (const auto& tv : tags) {
        if (tv.tag == 0x0004) shot_info = &tv;
        else if (tv.tag == 0x0093) file_info = &tv;
    }
    if (shot_info && shot_info->data.size() >= 20u * 2u) {
        const uint16_t upper = detail::rd_u16_e(shot_info->data.data() + 19u * 2u, be);
        const uint16_t lower = (shot_info->data.size() >= 21u * 2u)
                                   ? detail::rd_u16_e(shot_info->data.data() + 20u * 2u, be)
                                   : 0u;
        if (apply_canon_pair(upper, lower, FocusSource::CanonShotInfo)) return true;
    }
    if (file_info && file_info->data.size() >= 21u * 2u) {
        const uint16_t upper = detail::rd_u16_e(file_info->data.data() + 20u * 2u, be);
        const uint16_t lower = (file_info->data.size() >= 22u * 2u)
                                   ? detail::rd_u16_e(file_info->data.data() + 21u * 2u, be)
                                   : 0u;
        if (apply_canon_pair(upper, lower, FocusSource::CanonFileInfo)) return true;
    }
    return false;
}

template <typename ReadAtFn>
inline bool parse_canon_makernote(
    ReadAtFn&& read_at,
    uint64_t tiff_base,
    bool be,
    uint64_t mn_file_off,
    bool is_cr3_cmt3,
    const std::string& make,
    FocusTelemetry& ft) {
    if (!is_cr3_cmt3) {
        const std::string mu = detail::to_upper_ascii(make);
        if (mu.find("CANON") == std::string::npos) return false;
    }

    uint64_t base = tiff_base;
    bool mn_be = be;
    uint32_t ifd_rel = 0u;
    if (is_cr3_cmt3) {
        uint8_t thdr[8] = {};
        if (!read_at(mn_file_off, thdr, sizeof(thdr))) return false;
        const bool is_be = (thdr[0] == 'M' && thdr[1] == 'M');
        const bool is_le = (thdr[0] == 'I' && thdr[1] == 'I');
        if ((!is_be && !is_le) || detail::rd_u16_e(thdr + 2, is_be) != 42u) return false;
        base = mn_file_off;
        mn_be = is_be;
        ifd_rel = detail::rd_u32_e(thdr + 4, mn_be);
    } else {
        if (mn_file_off <= tiff_base || mn_file_off - tiff_base > 0xFFFFFFFFULL) return false;
        ifd_rel = static_cast<uint32_t>(mn_file_off - tiff_base);
    }

    std::vector<detail::IfdEntry> tags;
    if (!detail::read_ifd_entries(read_at, base, mn_be, ifd_rel, tags)) return false;
    return parse_canon_ifd_tags(tags, mn_be, ft);
}

inline void finalize_optical_dof(
    FocusTelemetry& ft,
    float focal_mm,
    float aperture,
    int raw_w_1x,
    int raw_h_1x,
    double fp_x_res = 0.0,
    int fp_res_unit = 0,
    float focal_35mm = 0.0f,
    const std::string& model = std::string()) {
    double pitch_mm = 0.0;
    if (fp_x_res > 0.0 && std::isfinite(fp_x_res)) {
        double unit_mm = 0.0;
        switch (fp_res_unit) {
            case 2: unit_mm = 25.4; break;   // inch
            case 3: unit_mm = 10.0; break;   // cm
            case 4: unit_mm = 1.0; break;    // mm
            case 5: unit_mm = 0.001; break;  // um
            default: break;
        }
        if (unit_mm > 0.0) {
            const double p = unit_mm / fp_x_res;
            if (p >= 0.0008 && p <= 0.025) pitch_mm = p;
        }
    }
    if (pitch_mm <= 0.0 && raw_w_1x >= 64 && raw_h_1x >= 64) {
        const double diag_px = std::hypot(static_cast<double>(raw_w_1x), static_cast<double>(raw_h_1x));
        double diag_mm = 43.266615; // default full-frame 36x24mm diagonal
        if (focal_35mm > 0.5f && focal_mm > 0.5f) {
            const double crop = std::max(0.5, std::min(6.0, static_cast<double>(focal_35mm) / static_cast<double>(focal_mm)));
            diag_mm = 43.266615 / crop;
        } else {
            const std::string ml = detail::to_upper_ascii(model);
            if (ml.find("APS") != std::string::npos || ml.find("A6") != std::string::npos ||
                ml.find("Z 50") != std::string::npos || ml.find("Z50") != std::string::npos ||
                ml.find("Z 30") != std::string::npos || ml.find("Z30") != std::string::npos ||
                ml.find("Z FC") != std::string::npos || ml.find("ZFC") != std::string::npos ||
                ml.find("ZV-E10") != std::string::npos || detail::starts_with_ci(ml, "X-")) {
                diag_mm = 28.2;
            }
        }
        pitch_mm = diag_mm / diag_px;
    }
    if (pitch_mm <= 0.0 || !std::isfinite(pitch_mm)) pitch_mm = 0.0045;

    const double coc_mm = 2.0 * pitch_mm; // 1x Bayer 2x2 Nyquist unit cell
    ft.pixel_pitch_um = static_cast<float>(pitch_mm * 1000.0);
    ft.coc_mm = static_cast<float>(coc_mm);

    const float f_use = (ft.focal_opt_mm > 0.5f) ? ft.focal_opt_mm
                      : ((focal_mm > 0.5f) ? focal_mm : ft.focal_nom_mm);
    if (!(f_use > 0.5f) || !(aperture > 0.1f)) {
        ft.infinity_in_dof = ft.is_infinity;
        return;
    }

    const double f_d = static_cast<double>(f_use);
    const double N_d = static_cast<double>(aperture);
    const double H_m = (f_d * f_d) / (N_d * coc_mm * 1000.0) + f_d * 0.001;
    ft.hyperfocal_m = static_cast<float>(H_m);

    if (!ft.valid) return;
    if (ft.is_infinity) {
        ft.dof_near_m = static_cast<float>(0.5 * H_m);
        ft.dof_far_m = 1000000.0f;
        ft.infinity_coc_1x_px = 0.0f;
        ft.infinity_in_dof = true;
        ft.clearly_near_macro = false;
        return;
    }

    const double d_nom = static_cast<double>(ft.distance_m);
    const double d_mm = d_nom * 1000.0;
    const double near_den = H_m + std::max(0.0, d_nom - f_d * 0.001);
    const double d_near = (near_den > 1e-6) ? (H_m * d_nom / near_den) : 0.01;
    ft.dof_near_m = static_cast<float>(std::max(0.01, d_near));
    ft.dof_far_m = (d_nom >= H_m) ? 1000000.0f
                                  : static_cast<float>((H_m * d_nom) / std::max(1e-6, H_m - d_nom));
    (void)d_mm;

    const double d_lo = (ft.distance_low_m > 0.0f) ? static_cast<double>(ft.distance_low_m) : d_nom;
    const double d_hi = (ft.distance_high_m > 0.0f) ? static_cast<double>(ft.distance_high_m) : d_nom;

    auto inf_blur_px = [&](double d_m) -> double {
        if (d_m >= 999999.0) return 0.0;
        const double denom_mm = std::max(f_d, d_m * 1000.0 - f_d);
        return ((f_d / N_d) * (f_d / denom_mm)) / pitch_mm;
    };
    const double b_inf_max = inf_blur_px(d_lo);
    const double b_inf_min = inf_blur_px(d_hi);
    ft.infinity_coc_1x_px = static_cast<float>(b_inf_max);
    ft.infinity_in_dof = (b_inf_max <= 2.5);
    ft.clearly_near_macro = (d_hi < 10.0) && (b_inf_min > 8.0);
}

} // namespace heraw_focus

#endif // HERAW_FOCUS_META_HPP
