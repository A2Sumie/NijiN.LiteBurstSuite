// Standalone exporter tests. Build with -DSUITE_IO_LIBRAW for independent DNG
// decode and an optional real RAW crop: test_suite_io OUTPUT_DIR [SOURCE_RAW].
// TIFF/LinearRaw artifacts are also suitable for tifffile + ExifTool readback.
#include "../heraw_exif_io.hpp"
#include <cassert>
#include <cerrno>
#include <fstream>
#include <iostream>
#include <sys/stat.h>
#include <thread>
#ifdef _WIN32
#include <direct.h>
#endif
#ifdef SUITE_IO_LIBRAW
#include <libraw/libraw.h>
#endif

using namespace heraw_io;
static int opens = 0, removes = 0, replaces = 0;
static FILE* open_file(const std::string& p) { ++opens; return std::fopen(p.c_str(), "wb"); }
static FILE* open_fail(const std::string&) { ++opens; return nullptr; }
static void remove_file(const std::string& p) { ++removes; std::remove(p.c_str()); }
static bool replace_file(const std::string& a, const std::string& b) {
    ++replaces;
    return std::rename(a.c_str(), b.c_str()) == 0;
}
static bool replace_fail(const std::string&, const std::string&) { ++replaces; return false; }
static uint16_t orient(int v) { return v == 6 ? 6 : 1; }
static auto parallel = [](int n, int, auto&& f, int) {
    // Concurrent strip readers exercise the lazy-field callback contract.
    const int split = n / 2;
    std::thread first([&] { f(0, split, 0); });
    f(split, n, 1);
    first.join();
};
static const float wb[3] = {4, 2, 3};
static const double matrix[3][3] = {{0.7, -0.2, -0.1}, {-0.3, 1.2, 0.15}, {-0.02, 0.1, 0.6}};
static ExifBundle metadata() {
    ExifBundle ex;
    ex.orig_software = "Source Camera";
    ex.ifd0_keep.push_back(make_ascii_tag(315, "Suite fixture"));
    ex.exif_keep.push_back(make_ascii_tag(36867, "2026:11:14 12:34:56"));
    ex.exif_keep.push_back(make_u16_tag(40961, 1));
    ex.gps_keep.push_back(RawTagItem{0, 1, 4, {2, 3, 0, 0}});
    return ex;
}
static bool tiff(const std::string& p, const uint16_t* rgb, int h, int w, bool compressed,
                 const std::vector<NamedAlphaPlane>& extra = {},
                 FILE* (*op)(const std::string&) = open_file,
                 bool (*rp)(const std::string&, const std::string&) = replace_file,
                 const ExifBundle& ex = metadata()) {
    return write_tiff16_rgb_with_alphas(p, rgb, extra, h, w, compressed,
        ColorSpaceMode::ADOBE_RGB, ex, "Suite IO test", 2, parallel, op, remove_file, rp);
}
static bool dng(const std::string& p, const uint16_t* raw, const float* rgb,
                int h, int w, RawDngMode mode, double variance = 2e-7, double slope = 1e-4,
                FILE* (*op)(const std::string&) = open_file,
                bool (*rp)(const std::string&, const std::string&) = replace_file,
                const float* white = wb, const double (*mat)[3] = matrix) {
    return write_dng14_rggb_ex(p, raw, rgb, h, w, mode, variance, slope,
        "Suite", "Camera", white, mat, 6, -0.25, metadata(), "Suite IO test", 2,
        orient, parallel, op, remove_file, rp);
}
template<class T> static void dump(const std::string& p, const std::vector<T>& v) {
    std::ofstream f(p, std::ios::binary);
    f.write(reinterpret_cast<const char*>(v.data()), v.size() * sizeof(T));
    assert(f.good());
}

static const RawTagItem* find_tag(const std::vector<RawTagItem>& tags, uint16_t id) {
    for (const auto& tag : tags) if (tag.tag == id) return &tag;
    return nullptr;
}
static std::vector<uint8_t> tiff_ifd(const std::vector<RawTagItem>& tags) {
    std::vector<uint8_t> bytes = {'I','I',42,0,8,0,0,0};
    assert(append_serialized_ifd(bytes, tags) == 8);
    return bytes;
}
static void append_box(std::vector<uint8_t>& bytes, const char* name,
                       const std::vector<uint8_t>& data, bool extended = false) {
    uint8_t hdr[16] = {};
    const uint32_t size = uint32_t(data.size()) + (extended ? 16 : 8);
    wr_be32(hdr, extended ? 1 : size);
    std::memcpy(hdr + 4, name, 4);
    if (extended) wr_be32(hdr + 12, size);
    bytes.insert(bytes.end(), hdr, hdr + (extended ? 16 : 8));
    bytes.insert(bytes.end(), data.begin(), data.end());
}
static void metadata_container_tests(const std::string& out) {
    const std::string path = out + "/container-test.bin";
    auto ex = metadata();
    ex.exif_keep.push_back(make_u16_tag(34855, 800));
    ex.exif_keep.push_back(RawTagItem{37521, 2, 1, {'7'}}); // Legal bounded ASCII without a terminator.
    ex.exif_keep.push_back(make_ascii_tag(42036, "Fixture Lens"));
    RawTagItem exposure{33434, 5, 1, std::vector<uint8_t>(8)};
    wr_le32(exposure.data.data(), 1); wr_le32(exposure.data.data() + 4, 125);
    ex.exif_keep.push_back(exposure);
    ex.gps_keep.push_back(make_ascii_tag(1, "N"));
    ex.gps_keep.push_back(make_ascii_tag(3, "E"));
    for (uint16_t id : {uint16_t(2), uint16_t(4), uint16_t(7)}) {
        RawTagItem coordinate{id, 5, 3, std::vector<uint8_t>(24)};
        const uint32_t values[3] = {id == 2 ? 35u : id == 4 ? 139u : 12u, 30, 15};
        for (int j = 0; j < 3; ++j) {
            wr_le32(coordinate.data.data() + j * 8, values[j]);
            wr_le32(coordinate.data.data() + j * 8 + 4, 1);
        }
        ex.gps_keep.push_back(coordinate);
    }
    ex.gps_keep.push_back(make_ascii_tag(29, "2026:11:14"));
    const std::vector<RawTagItem> ifd0 = {
        make_ascii_tag(271, "Fixture Camera"),
        {305, 2, 5, {'S','W','1','2','3'}}
    };
    auto cmt1 = tiff_ifd(ifd0), cmt2 = tiff_ifd(ex.exif_keep), cmt4 = tiff_ifd(ex.gps_keep);
    std::vector<uint8_t> canon = {
        0x85,0xc0,0xb6,0x87,0x82,0x0f,0x11,0xe0,0x81,0x11,0xf4,0xce,0x46,0x2b,0x6a,0x48
    };
    append_box(canon, "CMT1", cmt1);
    append_box(canon, "CMT2", cmt2, true);
    append_box(canon, "CMT4", cmt4);
    std::vector<uint8_t> moov, cr3;
    append_box(moov, "uuid", canon);
    append_box(cr3, "ftyp", {'c','r','x',' ',0,0,0,0,'c','r','x',' '});
    const size_t moov_start = cr3.size();
    append_box(cr3, "moov", moov, true);
    dump(path, cr3);
    auto read = extract_source_exif(path);
    assert(read.orig_software == "SW123" && read.subsec_ms == 700);
    assert(find_tag(read.exif_keep, 34855) && find_tag(read.gps_keep, 1));
    for (const auto& tag : ex.gps_keep) {
        const auto* copied = find_tag(read.gps_keep, tag.tag);
        assert(copied && copied->data == tag.data && copied->type == tag.type && copied->count == tag.count);
    }
    const uint16_t pixel[3] = {0, 32768, 65535};
    assert(tiff(out + "/container-gps.tif", pixel, 1, 1, false, {}, open_file, replace_file, read));
    assert(read_raw_subsec_ms(path) == 700);
    // Every truncated prefix, including partial large-size/UUID/CMT headers.
    for (size_t length = 0; length < cr3.size(); ++length) {
        dump(path, std::vector<uint8_t>(cr3.begin(), cr3.begin() + length));
        read = extract_source_exif(path);
        assert(read.ifd0_keep.empty() && read.exif_keep.empty() && read.gps_keep.empty());
    }
    auto bad = cr3;
    std::fill(bad.begin() + moov_start + 8, bad.begin() + moov_start + 16, 255);
    dump(path, bad);
    assert(extract_source_exif(path).exif_keep.empty());
    bad = cr3;
    wr_be32(bad.data() + moov_start, 4); // Box length smaller than its header.
    dump(path, bad);
    assert(extract_source_exif(path).exif_keep.empty());
    bad = cr3;
    bad[moov_start + 16 + 8] ^= 1; // Other UUIDs do not contain Canon TIFF boxes.
    dump(path, bad);
    assert(extract_source_exif(path).exif_keep.empty());
    std::vector<uint8_t> to_end(cr3.begin(), cr3.begin() + moov_start);
    append_box(to_end, "moov", moov);
    wr_be32(to_end.data() + moov_start, 0);
    dump(path, to_end);
    assert(extract_source_exif(path).subsec_ms == 700);
    // A TIFF value pointing outside CMT2 must not borrow bytes from CMT4.
    auto invalid_ifd = tiff_ifd({{42036, 2, 10, std::vector<uint8_t>(10, 'x')}});
    wr_le32(invalid_ifd.data() + 18, uint32_t(invalid_ifd.size()) + 8);
    std::vector<uint8_t> crossing(canon.begin(), canon.begin() + 16), boxes, container;
    append_box(crossing, "CMT2", invalid_ifd);
    append_box(crossing, "CMT4", cmt4);
    append_box(boxes, "uuid", crossing);
    append_box(container, "ftyp", {'c','r','x',' ',0,0,0,0});
    append_box(container, "moov", boxes);
    dump(path, container);
    read = extract_source_exif(path);
    assert(!find_tag(read.exif_keep, 42036) && find_tag(read.gps_keep, 1));
    // RAF JPEG marker walk: APP0 and a non-EXIF APP1 precede the EXIF APP1.
    auto embedded = build_standalone_exif_blob(ex, 1, 1, 1, ColorSpaceMode::SRGB, "Fixture SW");
    std::vector<uint8_t> jpeg = {255,216,255,224,0,4,0,0,255,225,0,5,'X','M','P',255,225};
    uint8_t len[2]; wr_be16(len, uint16_t(embedded.size() + 8));
    jpeg.insert(jpeg.end(), len, len + 2);
    jpeg.insert(jpeg.end(), {'E','x','i','f',0,0});
    jpeg.insert(jpeg.end(), embedded.begin(), embedded.end());
    jpeg.insert(jpeg.end(), {255,217});
    std::vector<uint8_t> raf(160, 0);
    std::memcpy(raf.data(), "FUJIFILMCCD-RAW", 14);
    wr_be32(raf.data() + 84, uint32_t(raf.size()));
    wr_be32(raf.data() + 88, uint32_t(jpeg.size()));
    raf.insert(raf.end(), jpeg.begin(), jpeg.end());
    dump(path, raf);
    read = extract_source_exif(path);
    assert(read.subsec_ms == 700 && find_tag(read.exif_keep, 33434) && find_tag(read.gps_keep, 1));
    assert(read_raw_subsec_ms(path) == 700);
    for (size_t length = 80; length < raf.size(); ++length) {
        dump(path, std::vector<uint8_t>(raf.begin(), raf.begin() + length));
        assert(extract_source_exif(path).exif_keep.empty());
    }
    wr_be32(raf.data() + 88, UINT32_MAX);
    dump(path, raf);
    assert(extract_source_exif(path).exif_keep.empty());
    dump(path, tiff_ifd(ifd0));
    assert(extract_source_exif(path).orig_software == "SW123");
    std::remove(path.c_str());
    std::cout << "RAF/CR3 container and truncated-boundary tests passed\n";
}

#ifdef __APPLE__
static int fail_write(void*, const char*, int) { errno = ENOSPC; return -1; }
static int fail_close(void*) { errno = ENOSPC; return -1; }
static int accept_write(void*, const char*, int n) { return n; }
static FILE* open_write_error(const std::string&) {
    FILE* f = funopen(nullptr, nullptr, fail_write, nullptr, nullptr);
    assert(f); std::setvbuf(f, nullptr, _IONBF, 0); return f;
}
static FILE* open_flush_error(const std::string&) {
    FILE* f = funopen(nullptr, nullptr, fail_write, nullptr, nullptr);
    assert(f); std::setvbuf(f, nullptr, _IOFBF, 8192); return f;
}
static FILE* open_close_error(const std::string&) {
    return funopen(nullptr, nullptr, accept_write, nullptr, fail_close);
}
#endif

#ifdef SUITE_IO_LIBRAW
static void decode_dng(const std::string& p, const std::vector<uint16_t>& expected,
                       int h, int w, RawDngMode mode) {
    LibRaw reader;
    reader.imgdata.rawparams.options &= ~LIBRAW_RAWOPTIONS_CONVERTFLOAT_TO_INT;
    const int opened = reader.open_file(p.c_str());
    if (opened != LIBRAW_SUCCESS) {
        std::cerr << p << " open: " << libraw_strerror(opened) << '\n';
        std::abort();
    }
    const int unpacked = reader.unpack();
    if (unpacked != LIBRAW_SUCCESS) {
        std::cerr << p << " unpack: " << libraw_strerror(unpacked) << '\n';
        std::abort();
    }
    assert(reader.imgdata.sizes.raw_width == w && reader.imgdata.sizes.raw_height == h);
    const auto& raw = reader.imgdata.rawdata;
    double max_error = 0;
    if (mode == RawDngMode::DNG_LINEAR) {
        assert(reader.imgdata.idata.filters == 0 && reader.imgdata.idata.colors == 3);
        assert(raw.color3_image || raw.color4_image);
        int max_channel_error[3] = {};
        for (size_t i = 0; i < expected.size() / 3; ++i) {
            for (int c = 0; c < 3; ++c) {
                const uint16_t decoded = raw.color3_image ? raw.color3_image[i][c] : raw.color4_image[i][c];
                max_channel_error[c] = std::max(max_channel_error[c], std::abs(int(decoded) - expected[i * 3 + c]));
            }
        }
        std::cout << "LibRaw " << p.substr(p.find_last_of('/') + 1) << " RGB max_error="
                  << max_channel_error[0] << "," << max_channel_error[1] << "," << max_channel_error[2] << std::endl;
        for (int c = 0; c < 3; ++c) assert(max_channel_error[c] == 0);
        return;
    }
    for (size_t i = 0; i < expected.size(); ++i) {
        if (mode == RawDngMode::DNG_FP16) {
            assert(raw.float_image);
            max_error = std::max(max_error, std::fabs(double(raw.float_image[i]) - expected[i] / 65535.0));
        } else {
            assert(raw.raw_image);
            max_error = std::max(max_error, std::fabs(double(raw.raw_image[i]) - expected[i]));
        }
    }
    // HQ's fixed fixture profile has a maximum half-step below 71 ADU.
    const double bound = mode == RawDngMode::DNG_FP16 ? 0.0005 : mode == RawDngMode::DNG_HQ ? 71 : 0;
    std::cout << "LibRaw " << p.substr(p.find_last_of('/') + 1) << " max_error=" << max_error << std::endl;
    assert(max_error <= bound);
}
#endif

int main(int argc, char** argv) {
    assert(argc >= 2);
    const std::string out = argv[1];
#ifdef _WIN32
    assert(_mkdir(out.c_str()) == 0 || errno == EEXIST);
#else
    assert(mkdir(out.c_str(), 0700) == 0 || errno == EEXIST);
#endif
    const auto path = [&](const std::string& name) { return out + "/" + name; };
    metadata_container_tests(out);
    for (int i = 2; i < argc; ++i) {
        const auto source = extract_source_exif(argv[i]);
        assert(find_tag(source.exif_keep, 33434)); // ExposureTime
        assert(find_tag(source.exif_keep, 34855)); // ISO
        assert(find_tag(source.exif_keep, 37386)); // FocalLength
        assert(find_tag(source.exif_keep, 36867)); // DateTimeOriginal
        assert(find_tag(source.exif_keep, 42036)); // LensModel
        assert(read_raw_subsec_ms(argv[i]) == source.subsec_ms);
        const uint16_t pixel[3] = {0, 32768, 65535};
        assert(tiff(path("metadata-" + std::to_string(i) + ".tif"), pixel, 1, 1, false, {},
                    open_file, replace_file, source));
        std::cout << "Metadata " << i << " EXIF=" << source.exif_keep.size()
                  << " GPS=" << source.gps_keep.size() << " subsec_ms=" << source.subsec_ms
                  << " source=" << argv[i] << '\n';
    }
    for (auto dims : {std::pair<int,int>{1,1}, {17,33}, {65,31}, {259,257}}) {
        const int h = dims.first, w = dims.second;
        const size_t n = size_t(h) * w;
        std::vector<uint16_t> rgb(n * 3), raw(n);
        std::vector<float> lin(n * 3);
        std::vector<std::vector<uint16_t>> fields(11, std::vector<uint16_t>(n));
        std::vector<NamedAlphaPlane> extras;
        for (int c = 0; c < 11; ++c) {
            for (size_t i = 0; i < n; ++i) fields[c][i] = uint16_t(i * 37 + c * 4093);
            extras.push_back({"Field " + std::to_string(c + 1), fields[c].data()});
        }
        for (size_t i = 0; i < n; ++i) {
            raw[i] = uint16_t(i);
            for (int c = 0; c < 3; ++c) {
                rgb[3 * i + c] = uint16_t(i + c * 21845);
                lin[3 * i + c] = rgb[3 * i + c] / 65535.0f;
            }
        }
        const std::string stem = std::to_string(h) + "x" + std::to_string(w);
        dump(path(stem + ".rgb16"), rgb);
        dump(path(stem + ".raw16"), raw);
        for (bool compressed : {false, true}) {
            const auto suffix = compressed ? "-deflate.tif" : "-raw.tif";
            assert(tiff(path(stem + suffix), rgb.data(), h, w, compressed));
            assert(tiff(path(stem + "-extra" + suffix), rgb.data(), h, w, compressed, extras));
            auto lazy = extras;
            for (int c = 0; c < 11; ++c) {
                lazy[c].data = nullptr;
                lazy[c].sample = [w, c](int y, int x) { return uint16_t((size_t(y) * w + x) * 37 + c * 4093); };
            }
            lazy[0].data = fields[0].data();
            lazy[0].sample = [](int, int) -> uint16_t { assert(false); return 0; };
            assert(tiff(path(stem + "-lazy" + suffix), rgb.data(), h, w, compressed, lazy));
        }
        assert(dng(path(stem + "-linear.dng"), nullptr, lin.data(), h, w, RawDngMode::DNG_LINEAR));
        for (auto mode : {RawDngMode::DNG_LOSSLESS, RawDngMode::DNG_RAW, RawDngMode::DNG_HQ, RawDngMode::DNG_FP16}) {
            const std::string p = path(stem + "-mode" + std::to_string(int(mode)) + ".dng");
            assert(dng(p, raw.data(), nullptr, h, w, mode));
#ifdef SUITE_IO_LIBRAW
            // LibRaw applies its minimum camera-image dimensions to tiny fixtures.
            if (h >= 65 && w >= 31) decode_dng(p, raw, h, w, mode);
#endif
        }
#ifdef SUITE_IO_LIBRAW
        if (h >= 65 && w >= 31) decode_dng(path(stem + "-linear.dng"), rgb, h, w, RawDngMode::DNG_LINEAR);
#endif
    }
    // Full-range jumps force 16-bit category/wrap paths and independent channel prediction.
    for (int pattern = 0; pattern < 3; ++pattern) {
        const int h = 67, w = 73;
        std::vector<uint16_t> expected(size_t(h) * w * 3);
        std::vector<float> linear(expected.size());
        uint32_t random = 0x84b123;
        for (size_t i = 0; i < expected.size(); ++i) {
            random ^= random << 13; random ^= random >> 17; random ^= random << 5;
            expected[i] = pattern == 0 ? 0 : pattern == 1 ? (i & 1 ? 65535 : 0) : uint16_t(random);
            linear[i] = expected[i] / 65535.0f;
        }
        const std::string stem = "adversarial" + std::to_string(pattern);
        dump(path(stem + ".rgb16"), expected);
        assert(dng(path(stem + "-linear.dng"), nullptr, linear.data(), h, w, RawDngMode::DNG_LINEAR));
#ifdef SUITE_IO_LIBRAW
        decode_dng(path(stem + "-linear.dng"), expected, h, w, RawDngMode::DNG_LINEAR);
#endif
    }
    uint16_t rgb[3] = {0, 32768, 65535};
    float lin[3] = {-1, 0.5f, 2};
    assert(dng(path("clamped-linear.dng"), nullptr, lin, 1, 1, RawDngMode::DNG_LINEAR));
    const std::string bad = path("error.tif");
    assert(!tiff(bad, nullptr, 1, 1, false));
    assert(!tiff(bad, rgb, 0, 1, false));
    assert(!tiff(bad, rgb, -1, 1, true));
    int before = opens;
    assert(!tiff(bad, rgb, 65536, 65536, false));
    assert(!tiff(bad, rgb, 1, INT_MAX, true));
    assert(!dng(bad, rgb, nullptr, 65536, 65536, RawDngMode::DNG_RAW));
    assert(opens == before);
    std::vector<NamedAlphaPlane> excessive(UINT16_MAX - 2);
    assert(!tiff(bad, rgb, 1, 1, false, excessive));
    assert(!tiff(bad, rgb, 1, 1, false, {{"\xff", rgb}}));
    ExifBundle malformed = metadata();
    malformed.ifd0_keep.push_back({60000, 12, UINT32_MAX, {0}});
    assert(!tiff(bad, rgb, 1, 1, false, {}, open_file, replace_file, malformed));
    std::vector<uint8_t> blob(8);
    assert(append_serialized_ifd(blob, {{1, 0, 1, {0}}}) == 0);
    assert(append_serialized_ifd(blob, {{1, 1, 0, {}}}) == 0);
    assert(blob.size() == 8);
    assert(!tiff(bad, rgb, 1, 1, false, {}, open_fail));
    const int removed_before = removes;
    assert(!tiff(bad, rgb, 1, 1, false, {}, open_file, replace_fail));
    assert(removes == removed_before + 1 && !std::ifstream(bad + ".tmp").good());
    assert(!dng(bad, nullptr, nullptr, 1, 1, RawDngMode::DNG_LINEAR));
    assert(!dng(bad, rgb, nullptr, 1, 1, RawDngMode::DNG_LINEAR));
    assert(!dng(bad, rgb, lin, 0, 1, RawDngMode::DNG_RAW));
    assert(!dng(bad, rgb, lin, 1, 1, static_cast<RawDngMode>(99)));
    assert(!dng(bad, rgb, lin, 1, 1, RawDngMode::DNG_RAW, -1));
    assert(!dng(bad, rgb, lin, 1, 1, RawDngMode::DNG_RAW, 0, NAN));
    float bad_wb[3] = {1, 0, 1};
    assert(!dng(bad, rgb, lin, 1, 1, RawDngMode::DNG_RAW, 0, 0, open_file, replace_file, bad_wb));
    double bad_matrix[3][3] = {};
    assert(!dng(bad, rgb, lin, 1, 1, RawDngMode::DNG_RAW, 0, 0, open_file, replace_file, wb, bad_matrix));
    lin[0] = NAN;
    assert(!dng(bad, nullptr, lin, 1, 1, RawDngMode::DNG_LINEAR));
    lin[0] = 0;
    assert(dng(path("zero-noise.dng"), rgb, nullptr, 1, 1, RawDngMode::DNG_RAW, 0, 0));
    assert(dng(path("zero-offset.dng"), rgb, nullptr, 1, 1, RawDngMode::DNG_RAW, 0, 0.001));
    assert(!dng(bad, rgb, nullptr, 1, 1, RawDngMode::DNG_RAW, 0, 0, open_fail));
    assert(!dng(bad, rgb, nullptr, 1, 1, RawDngMode::DNG_RAW, 0, 0, open_file, replace_fail));
    assert(tiff(path("unicode.tif"), rgb, 1, 1, false, {{"\xe7\x9b\xb8\xe4\xbd\x8d \xf0\x9f\x8c\x9f", nullptr}}));
#ifdef __APPLE__
    for (auto op : {open_write_error, open_flush_error, open_close_error}) {
        const int replaced_before = replaces;
        assert(!tiff(bad, rgb, 1, 1, false, {}, op));
        assert(!tiff(bad, rgb, 1, 1, true, {}, op));
        assert(!dng(bad, rgb, nullptr, 1, 1, RawDngMode::DNG_RAW, 0, 0, op));
        assert(!dng(bad, nullptr, lin, 1, 1, RawDngMode::DNG_LINEAR, 0, 0, op));
        assert(!write_png16_rgb(bad, rgb, 1, 1, ColorSpaceMode::SRGB, metadata(), "Test", 1,
                               parallel, op, remove_file, replace_file));
        assert(!write_png16_gray(bad, rgb, 1, 1, metadata(), "Test", 1,
                                parallel, op, remove_file, replace_file));
        assert(replaces == replaced_before);
    }
#endif
#ifdef SUITE_IO_LIBRAW
    if (argc > 2) {
        LibRaw source;
        assert(source.open_file(argv[2]) == LIBRAW_SUCCESS && source.unpack() == LIBRAW_SUCCESS);
        const int h = 259, w = 257;
        assert(source.imgdata.rawdata.raw_image);
        std::vector<uint16_t> crop(size_t(h) * w), rgbcrop(size_t(h) * w * 3);
        std::vector<float> linear_crop(rgbcrop.size());
        for (int y = 0; y < h; ++y) for (int x = 0; x < w; ++x) {
            crop[size_t(y) * w + x] = source.imgdata.rawdata.raw_image[
                size_t(y + source.imgdata.sizes.top_margin) * source.imgdata.sizes.raw_width +
                x + source.imgdata.sizes.left_margin];
            // Real Bayer 2x2 area samples, black-subtracted camera channels before WB.
            unsigned sums[3] = {}, counts[3] = {};
            for (int dy = 0; dy < 2; ++dy) for (int dx = 0; dx < 2; ++dx) {
                const int sy = y * 2 + dy + source.imgdata.sizes.top_margin;
                const int sx = x * 2 + dx + source.imgdata.sizes.left_margin;
                int c = source.COLOR(sy, sx);
                if (c == 3) c = 1;
                assert(c >= 0 && c < 3);
                sums[c] += source.imgdata.rawdata.raw_image[size_t(sy) * source.imgdata.sizes.raw_width + sx];
                ++counts[c];
            }
            const auto& color = source.imgdata.color;
            for (int c = 0; c < 3; ++c) {
                assert(counts[c] > 0);
                const double black = color.black + color.cblack[c];
                const double value = std::max(0.0, std::min(1.0,
                    (double(sums[c]) / counts[c] - black) / (color.maximum - black)));
                const size_t i = (size_t(y) * w + x) * 3 + c;
                rgbcrop[i] = uint16_t(std::lround(value * 65535.0));
                linear_crop[i] = rgbcrop[i] / 65535.0f;
            }
        }
        dump(path("real-crop.raw16"), crop);
        dump(path("real-crop.rgb16"), rgbcrop);
        assert(tiff(path("real-crop.tif"), rgbcrop.data(), h, w, true));
        assert(dng(path("real-crop.dng"), crop.data(), nullptr, h, w, RawDngMode::DNG_LOSSLESS));
        decode_dng(path("real-crop.dng"), crop, h, w, RawDngMode::DNG_LOSSLESS);
        assert(dng(path("real-crop-linear.dng"), nullptr, linear_crop.data(), h, w, RawDngMode::DNG_LINEAR));
        decode_dng(path("real-crop-linear.dng"), rgbcrop, h, w, RawDngMode::DNG_LINEAR);
        std::cout << "Real RAW crop: " << argv[2] << '\n';
    }
#endif
    std::cout << "Suite IO tests passed\n";
}
