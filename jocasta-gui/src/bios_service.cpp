#include "bios_service.h"
#include "app_settings.h"
#include "nanosha256.h"
#include "miniz.h"

#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <filesystem>
#include <string>
#include <vector>

namespace fs = std::filesystem;


static std::string join(const std::string& dir, const char* fname)
{
    if (!dir.empty() && dir.back() == '/')
        return dir + fname;
    return dir + "/" + fname;
}

static std::string sha256_file(const std::string& path)
{
    FILE* f = fopen(path.c_str(), "rb");
    if (!f) return {};

    SHA256_CTX ctx;
    SHA256_Init(&ctx);
    uint8_t buf[65536];
    size_t n;
    while ((n = fread(buf, 1, sizeof(buf), f)) > 0)
        SHA256_Update(&ctx, buf, n);
    fclose(f);

    uint8_t hash[32];
    SHA256_Final(hash, &ctx);
    char hex[65];
    for (int i = 0; i < 32; i++) snprintf(hex + i * 2, 3, "%02x", hash[i]);
    hex[64] = '\0';
    return hex;
}

static bios_status check_status(const BIOSFile& spec, const std::string& sha)
{
    if (sha.empty()) return bios_status::missing;
    if (!spec.sha256) return bios_status::unknown_hash;
    return (sha == std::string(spec.sha256)) ? bios_status::ok : bios_status::bad_hash;
}

static std::string working_name(const char* filename)
{
    std::string s(filename);
    auto dot = s.rfind('.');
    if (dot == std::string::npos)
        return s + "_working";
    return s.substr(0, dot) + "_working" + s.substr(dot);
}

static bool copy_file(const std::string& src, const std::string& dst)
{
    FILE* in = fopen(src.c_str(), "rb");
    if (!in) return false;
    FILE* out = fopen(dst.c_str(), "wb");
    if (!out) { fclose(in); return false; }

    char buf[65536];
    size_t n;
    while ((n = fread(buf, 1, sizeof(buf), in)) > 0)
        fwrite(buf, 1, n, out);

    fclose(in);
    fclose(out);
    return true;
}

static void* read_file_alloc(const std::string& path, size_t& size_out)
{
    FILE* f = fopen(path.c_str(), "rb");
    if (!f) return nullptr;
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (sz <= 0) { fclose(f); return nullptr; }
    void* buf = malloc(static_cast<size_t>(sz));
    if (!buf) { fclose(f); return nullptr; }
    size_t rd = fread(buf, 1, static_cast<size_t>(sz), f);
    fclose(f);
    if (rd != static_cast<size_t>(sz)) { free(buf); return nullptr; }
    size_out = static_cast<size_t>(sz);
    return buf;
}

static bool try_neogeo_zip(const std::string& dir, multi_file_set& mfs)
{
    std::string zip_path = join(dir, "neogeo.zip");
    if (!fs::is_regular_file(zip_path)) return false;

    mz_zip_archive zip;
    memset(&zip, 0, sizeof(zip));
    if (!mz_zip_reader_init_file(&zip, zip_path.c_str(), 0)) return false;

    mz_uint n = mz_zip_reader_get_num_files(&zip);
    mz_uint added = 0;
    for (mz_uint i = 0; i < n; i++) {
        mz_zip_archive_file_stat stat;
        if (!mz_zip_reader_file_stat(&zip, i, &stat)) continue;
        if (stat.m_is_directory || stat.m_is_encrypted || !stat.m_is_supported) continue;

        size_t sz = 0;
        void* buf = mz_zip_reader_extract_to_heap(&zip, i, &sz, 0);
        if (!buf) continue;

        const char* name = stat.m_filename;
        for (const char* p = name; *p; p++)
            if (*p == '/' || *p == '\\') name = p + 1;

        if (name[0]) {
            mfs.add_from_buf(name, zip_path.c_str(), buf, sz);
            added++;
        }
        mz_free(buf);
    }
    mz_zip_reader_end(&zip);
    return added > 0;
}


std::string bios_default_dir(const char* slug)
{
#if defined(_WIN32)
    const char* home = getenv("USERPROFILE");
#else
    const char* home = getenv("HOME");
#endif
    if (!home || !home[0]) home = ".";
    return std::string(home) + "/Documents/" + slug;
}

std::string bios_get_dir(jsm::systems sys, const AppSettings& s)
{
    const BIOSEntry* e = sysdb_find_bios(sys);
    if (!e) return {};
    std::string stored = s.get_bios_dir(e->dir_slug);
    return stored.empty() ? bios_default_dir(e->dir_slug) : stored;
}

void bios_set_dir(jsm::systems sys, const char* dir, AppSettings& s)
{
    const BIOSEntry* e = sysdb_find_bios(sys);
    if (!e) return;
    s.set_bios_dir(e->dir_slug, dir);
}


std::vector<BIOSCheckResult> bios_check(jsm::systems sys, const std::string& dir)
{
    std::vector<BIOSCheckResult> out;
    const BIOSEntry* e = sysdb_find_bios(sys);
    if (!e) return out;

    for (int i = 0; i < e->num_files; i++) {
        const BIOSFile& f = e->files[i];
        BIOSCheckResult r;
        r.file = &f;

        std::string path;
        if (f.writable) {
            std::string wp = join(dir, working_name(f.filename).c_str());
            path = fs::is_regular_file(wp) ? wp : join(dir, f.filename);
        } else {
            path = join(dir, f.filename);
        }

        r.sha256_actual = sha256_file(path);
        r.status = check_status(f, r.sha256_actual);
        out.push_back(r);
    }
    return out;
}

bool bios_ok_to_launch(jsm::systems sys, const std::string& dir)
{
    const BIOSEntry* e = sysdb_find_bios(sys);
    if (!e) return true;
    auto results = bios_check(sys, dir);
    for (auto& r : results)
        if (!r.file->optional && r.status == bios_status::missing)
            return false;
    return true;
}


bool bios_load(jsm::systems sys, const std::string& dir, multi_file_set& mfs,
               std::vector<std::string>& working_paths_out)
{
    const BIOSEntry* e = sysdb_find_bios(sys);
    if (!e) return true;

    bool used_zip = false;
    if (sys == jsm::systems::NEOGEO_AES || sys == jsm::systems::NEOGEO_MVS) {
        used_zip = try_neogeo_zip(dir, mfs);
    }

    bool all_required_ok = true;

    for (int i = 0; i < e->num_files; i++) {
        const BIOSFile& f = e->files[i];

        if (used_zip) {
            continue;
        }

        if (!f.writable) {
            mfs.add(f.filename, dir.c_str());

            if (!mfs.files.empty() && mfs.files.back().buf.size == 0) {
                printf("\nERROR GETTING FILE %s", f.filename);
                if (!f.optional) all_required_ok = false;
            }
        } else {
            std::string orig_path = join(dir, f.filename);
            std::string work_name = working_name(f.filename);
            std::string work_path = join(dir, work_name.c_str());

            if (!fs::is_regular_file(work_path)) {
                if (fs::is_regular_file(orig_path)) {
                    if (!copy_file(orig_path, work_path))
                        printf("\nWARN: could not create working copy %s", work_path.c_str());
                } else {
                    printf("\nERROR GETTING FILE %s", f.filename);
                    if (!f.optional) all_required_ok = false;
                    working_paths_out.push_back({});
                    continue;
                }
            }

            size_t sz = 0;
            void* buf = read_file_alloc(work_path, sz);
            if (buf && sz > 0) {
                mfs.add_from_buf(f.filename, dir.c_str(), buf, sz);
                free(buf);
                working_paths_out.push_back(work_path);
            } else {
                printf("\nERROR reading working copy %s", work_path.c_str());
                if (!f.optional) all_required_ok = false;
                working_paths_out.push_back({});
            }
        }
    }

    return all_required_ok;
}
