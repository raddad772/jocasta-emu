#include "ccd_loader.h"

#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <string>
#include <vector>
#include <filesystem>


namespace {

struct CcdTrack {
    int track_no = -1;
    int mode = 1;
    long idx0 = -1;
    long idx1 = -1;
};

std::string trim(const std::string& s) {
    size_t a = 0, b = s.size();
    while (a < b && (unsigned char)s[a] <= ' ') a++;
    while (b > a && (unsigned char)s[b - 1] <= ' ') b--;
    return s.substr(a, b - a);
}

bool read_text_file(const char* path, std::string& out) {
    FILE* f = fopen(path, "rb");
    if (!f) return false;
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (n < 0) { fclose(f); return false; }
    out.resize(static_cast<size_t>(n));
    size_t got = fread(out.data(), 1, static_cast<size_t>(n), f);
    out.resize(got);
    fclose(f);
    return true;
}

std::string find_sibling(const std::filesystem::path& ccd, const char* ext_lower, const char* ext_upper) {
    namespace fs = std::filesystem;
    fs::path p = ccd;
    p.replace_extension(ext_lower);
    if (fs::is_regular_file(p)) return p.string();
    p.replace_extension(ext_upper);
    if (fs::is_regular_file(p)) return p.string();
    return {};
}

}

bool mfs_add_ccd_path(multi_file_set* ROMs, const char* path)
{
    namespace fs = std::filesystem;

    std::string text;
    if (!read_text_file(path, text)) {
        printf("[CCD] could not read %s\n", path);
        return false;
    }

    std::vector<CcdTrack> tracks;
    CcdTrack* cur = nullptr;

    size_t pos = 0;
    while (pos < text.size()) {
        size_t eol = text.find('\n', pos);
        if (eol == std::string::npos) eol = text.size();
        std::string line = trim(text.substr(pos, eol - pos));
        pos = eol + 1;
        if (line.empty()) continue;

        if (line[0] == '[') {
            int n = -1;
            if (sscanf(line.c_str(), "[TRACK %d]", &n) == 1) {
                tracks.emplace_back();
                cur = &tracks.back();
                cur->track_no = n;
            } else {
                cur = nullptr;
            }
            continue;
        }
        if (!cur) continue;

        int ival = 0;
        long lval = 0;
        if (sscanf(line.c_str(), "MODE=%d", &ival) == 1) {
            cur->mode = ival;
        } else if (sscanf(line.c_str(), "INDEX 1=%ld", &lval) == 1) {
            cur->idx1 = lval;
        } else if (sscanf(line.c_str(), "INDEX 0=%ld", &lval) == 1) {
            cur->idx0 = lval;
        }
    }

    if (tracks.empty()) {
        printf("[CCD] no [TRACK] sections found in %s\n", path);
        return false;
    }

    fs::path ccd_path = path;
    std::string img_path = find_sibling(ccd_path, ".img", ".IMG");
    if (img_path.empty()) {
        printf("[CCD] no matching .img next to %s\n", path);
        return false;
    }

    ROMs->clear();
    for (const auto& t : tracks) {
        if (t.idx1 < 0) continue;
        cd_track_desc d{};
        d.track_no = t.track_no;
        d.mode = (t.mode >= 0 && t.mode <= 2) ? t.mode : 1;
        d.idx1_file_lba = static_cast<u32>(t.idx1);
        d.idx0_file_lba = (t.idx0 >= 0 && t.idx0 <= t.idx1) ? static_cast<u32>(t.idx0) : static_cast<u32>(t.idx1);
        ROMs->disc_toc.push_back(d);
    }

    if (ROMs->disc_toc.empty()) {
        printf("[CCD] no usable tracks in %s\n", path);
        ROMs->clear();
        return false;
    }

    fs::path img_fs = img_path;
    std::string dir = img_fs.parent_path().string();
    if (dir.empty()) dir = ".";
    ROMs->add(img_fs.filename().string().c_str(), dir.c_str());

    if (ROMs->files.empty() || !ROMs->files[0].buf.ptr || ROMs->files[0].buf.size == 0) {
        printf("[CCD] failed to load image %s\n", img_path.c_str());
        ROMs->clear();
        return false;
    }

    return true;
}
