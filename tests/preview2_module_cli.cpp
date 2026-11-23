// preview2_module_cli.cpp -- Standalone compiled C++20 production module runner
// for Preview2 numerical, scene, and Sony test cases.
#include "../heraw_preview2_modules.hpp"
#include "../heraw_burst_planner.hpp"

#include <iostream>

namespace {

std::string extract_json_str(const std::string& obj, const std::string& key) {
    const std::string pat = "\"" + key + "\"";
    size_t p = obj.find(pat);
    if (p == std::string::npos) return {};
    p = obj.find(':', p + pat.size());
    if (p == std::string::npos) return {};
    p = obj.find('"', p + 1u);
    if (p == std::string::npos) return {};
    size_t e = p + 1u;
    std::string out;
    while (e < obj.size() && obj[e] != '"') {
        if (obj[e] == '\\' && e + 1u < obj.size()) { out.push_back(obj[e + 1u]); e += 2u; }
        else { out.push_back(obj[e++]); }
    }
    return out;
}

double extract_json_num(const std::string& obj, const std::string& key) {
    const std::string pat = "\"" + key + "\"";
    size_t p = obj.find(pat);
    if (p == std::string::npos) return 0.0;
    p = obj.find(':', p + pat.size());
    if (p == std::string::npos) return 0.0;
    ++p;
    while (p < obj.size() && std::isspace(static_cast<unsigned char>(obj[p]))) ++p;
    return std::strtod(obj.c_str() + p, nullptr);
}

void solve_n1_picks_dir(const std::string& input_dir, const std::string& out_json_path) {
    std::string mtxt;
    const std::string mpath = input_dir + "/frames.json";
    if (!heraw_plan::read_small_text_file_utf8(mpath, mtxt)) {
        throw std::runtime_error("Missing frames.json in N1-picks input directory");
    }
    std::vector<heraw_plan::HeroPickFrameMeta> frames;
    size_t pos = 0;
    while (true) {
        const size_t b0 = mtxt.find('{', pos);
        if (b0 == std::string::npos) break;
        const size_t b1 = mtxt.find('}', b0);
        if (b1 == std::string::npos) break;
        const std::string item = mtxt.substr(b0, b1 - b0 + 1u);
        heraw_plan::HeroPickFrameMeta fm;
        fm.id_path = extract_json_str(item, "path");
        fm.fs_path = input_dir + "/" + fm.id_path;
        fm.camera_key = extract_json_str(item, "camera");
        fm.time_s = 0.001 * extract_json_num(item, "time_ms");
        fm.focal_mm = extract_json_num(item, "focal_mm");
        if (!fm.id_path.empty()) frames.push_back(std::move(fm));
        pos = b1 + 1u;
    }
    const auto jobs = heraw_plan::harvest_hero_pick_jobs(frames, "auto", 0.20, 0.02);
    std::ofstream ofs(heraw_fs::from_utf8(out_json_path), std::ios::binary | std::ios::trunc);
    if (!ofs) throw std::runtime_error("Failed writing N1-picks output");
    ofs << "{\n  \"jobs\": [\n";
    for (size_t i = 0; i < jobs.size(); ++i) {
        const auto& jb = jobs[i];
        ofs << "    {\n"
            << "      \"anchor\": \"" << heraw_p2mod::json_esc(jb.anchor) << "\",\n"
            << "      \"eligible\": [";
        for (size_t k = 0; k < jb.eligible.size(); ++k) {
            ofs << (k ? ", " : "") << "\"" << heraw_p2mod::json_esc(jb.eligible[k]) << "\"";
        }
        ofs << "],\n      \"members\": [";
        for (size_t k = 0; k < jb.members.size(); ++k) {
            ofs << (k ? ", " : "") << "\"" << heraw_p2mod::json_esc(jb.members[k]) << "\"";
        }
        ofs << "],\n      \"output\": \"" << heraw_p2mod::json_esc(jb.output_stem) << "\"\n"
            << "    }" << (i + 1u < jobs.size() ? "," : "") << "\n";
    }
    ofs << "  ]\n}\n";
}

} // namespace

int main(int argc, char** argv) {
    if (argc != 4) {
        std::cerr << "Usage: " << (argv[0] ? argv[0] : "preview2_module_cli")
                  << " <case_id> <in_bundle_or_dir> <out_bundle_or_json>\n";
        return 2;
    }
    try {
        const std::string case_id = argv[1];
        const std::string in_arg = argv[2];
        const std::string out_arg = argv[3];
        if (case_id == "N1-picks") {
            solve_n1_picks_dir(in_arg, out_arg);
            return 0;
        }
        if (case_id == "N6-jpeg") {
            heraw_p2mod::solve_n6_jpeg_dir(in_arg, out_arg);
            return 0;
        }
        const auto in_map = heraw_p2mod::read_tensor_bundle(in_arg);
        const auto out_map = heraw_p2mod::solve_tensor_case(case_id, in_map);
        heraw_p2mod::write_tensor_bundle(out_arg, out_map);
        return 0;
    } catch (const std::exception& ex) {
        std::cerr << "preview2_module_cli error: " << ex.what() << "\n";
        return 1;
    }
}
