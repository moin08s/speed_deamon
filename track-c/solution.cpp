#include <iostream>
#include <fstream>
#include <vector>
#include <string>
#include <thread>
#include <algorithm>
#include <cstring>
#include <cstdint>
#include <cstdio>
#include <utility>

#ifdef _WIN32
#include <windows.h>
#else
#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#endif

struct MappedFile {
    const char* data = nullptr;
    size_t size = 0;
#ifdef _WIN32
    HANDLE hFile = INVALID_HANDLE_VALUE;
    HANDLE hMap = NULL;
#else
    int fd = -1;
#endif

    bool open(const char* path) {
#ifdef _WIN32
        hFile = CreateFileA(path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
        if (hFile == INVALID_HANDLE_VALUE) return false;
        LARGE_INTEGER li;
        GetFileSizeEx(hFile, &li);
        size = li.QuadPart;
        if (size == 0) return true;
        hMap = CreateFileMappingA(hFile, NULL, PAGE_READONLY, 0, 0, NULL);
        if (!hMap) { CloseHandle(hFile); return false; }
        data = (const char*)MapViewOfFile(hMap, FILE_MAP_READ, 0, 0, size);
        return data != nullptr;
#else
        fd = ::open(path, O_RDONLY);
        if (fd < 0) return false;
        struct stat st;
        if (fstat(fd, &st) < 0) { ::close(fd); return false; }
        size = st.st_size;
        if (size == 0) return true;
        data = (const char*)mmap(nullptr, size, PROT_READ, MAP_SHARED, fd, 0);
        return data != MAP_FAILED;
#endif
    }

    void close() {
#ifdef _WIN32
        if (data) { UnmapViewOfFile(data); data = nullptr; }
        if (hMap) { CloseHandle(hMap); hMap = NULL; }
        if (hFile != INVALID_HANDLE_VALUE) { CloseHandle(hFile); hFile = INVALID_HANDLE_VALUE; }
#else
        if (data && data != MAP_FAILED) { munmap((void*)data, size); data = nullptr; }
        if (fd >= 0) { ::close(fd); fd = -1; }
#endif
        size = 0;
    }

    ~MappedFile() { close(); }
};

struct CountryScalars {
    int64_t count = 0;
    int64_t sum_rec_id = 0;
    int64_t sum_cents = 0;
    int64_t min_cents = 0;
    int64_t max_cents = 0;
    int64_t sum_name_len = 0;
    int64_t sum_note_len = 0;
    bool has_cents = false;
};

struct NameEntry {
    uint16_t cid;
    uint64_t hash;

    bool operator<(const NameEntry& o) const {
        if (cid != o.cid) return cid < o.cid;
        return hash < o.hash;
    }
    bool operator==(const NameEntry& o) const {
        return cid == o.cid && hash == o.hash;
    }
};

struct ThreadLocalData {
    std::vector<CountryScalars> stats;
    std::vector<NameEntry> name_entries;
};

inline char* fast_i64(char* p, int64_t val) {
    if (val < 0) {
        *p++ = '-';
        val = -val;
    }
    if (val == 0) {
        *p++ = '0';
        return p;
    }
    char buf[24];
    int len = 0;
    while (val > 0) {
        buf[len++] = '0' + (val % 10);
        val /= 10;
    }
    for (int i = len - 1; i >= 0; --i) {
        *p++ = buf[i];
    }
    return p;
}

inline char* fast_format_cents(char* p, int64_t cents) {
    if (cents < 0) {
        *p++ = '-';
        cents = -cents;
    }
    int64_t whole = cents / 100;
    int rem = (int)(cents % 100);
    p = fast_i64(p, whole);
    *p++ = '.';
    *p++ = '0' + (rem / 10);
    *p++ = '0' + (rem % 10);
    return p;
}

inline int64_t parse_field_int(const char*& p, const char* limit) {
    if (p >= limit) return 0;
    if (limit - p >= 3 && (unsigned char)p[0] == 0xef && (unsigned char)p[1] == 0xbb && (unsigned char)p[2] == 0xbf) {
        p += 3;
    }
    bool quoted = (*p == '"');
    if (quoted) p++;
    int64_t val = 0;
    while (p < limit && *p >= '0' && *p <= '9') {
        val = val * 10 + (*p - '0');
        p++;
    }
    if (quoted && p < limit && *p == '"') p++;
    if (p < limit && *p == ',') p++;
    return val;
}

inline void parse_field_name_hash(const char*& p, const char* limit, uint64_t& out_hash, size_t& utf8_len) {
    out_hash = 0xcbf29ce484222325ULL;
    utf8_len = 0;
    if (p >= limit) return;
    if (*p == '"') {
        p++;
        while (p < limit) {
            if (*p == '"') {
                if (p + 1 < limit && *(p + 1) == '"') {
                    out_hash = (out_hash ^ '"') * 0x100000001b3ULL;
                    utf8_len++;
                    p += 2;
                } else {
                    p++;
                    break;
                }
            } else {
                unsigned char c = (unsigned char)*p++;
                if ((c & 0xC0) != 0x80) utf8_len++;
                out_hash = (out_hash ^ c) * 0x100000001b3ULL;
            }
        }
        if (p < limit && *p == ',') p++;
    } else {
        while (p < limit && *p != ',' && *p != '\n' && *p != '\r') {
            unsigned char c = (unsigned char)*p++;
            if ((c & 0xC0) != 0x80) utf8_len++;
            out_hash = (out_hash ^ c) * 0x100000001b3ULL;
        }
        if (p < limit && *p == ',') p++;
    }
}

inline size_t parse_field_len_only(const char*& p, const char* limit) {
    size_t utf8_len = 0;
    if (p >= limit) return 0;
    if (*p == '"') {
        p++;
        while (p < limit) {
            if (*p == '"') {
                if (p + 1 < limit && *(p + 1) == '"') {
                    utf8_len++;
                    p += 2;
                } else {
                    p++;
                    break;
                }
            } else {
                unsigned char c = (unsigned char)*p++;
                if ((c & 0xC0) != 0x80) utf8_len++;
            }
        }
        if (p < limit && *p == ',') p++;
    } else {
        while (p < limit && *p != ',' && *p != '\n' && *p != '\r') {
            unsigned char c = (unsigned char)*p++;
            if ((c & 0xC0) != 0x80) utf8_len++;
        }
        if (p < limit && *p == ',') p++;
    }
    return utf8_len;
}

inline int32_t parse_field_int32(const char*& p, const char* limit) {
    if (p >= limit) return 0;
    bool quoted = (*p == '"');
    if (quoted) p++;
    int32_t val = 0;
    while (p < limit && *p >= '0' && *p <= '9') {
        val = val * 10 + (*p - '0');
        p++;
    }
    if (quoted && p < limit && *p == '"') p++;
    if (p < limit && *p == ',') p++;
    return val;
}

inline int64_t parse_field_cents(const char*& p, const char* limit) {
    if (p >= limit) return 0;
    bool quoted = (*p == '"');
    if (quoted) p++;
    bool neg = false;
    if (p < limit && *p == '-') {
        neg = true;
        p++;
    }
    int64_t val = 0;
    while (p < limit && *p != '\n' && *p != '\r' && *p != '"') {
        if (*p >= '0' && *p <= '9') {
            val = val * 10 + (*p - '0');
        }
        p++;
    }
    if (quoted && p < limit && *p == '"') p++;
    while (p < limit && (*p == '\r' || *p == '\n')) {
        p++;
    }
    return neg ? -val : val;
}

int main(int argc, char** argv) {
    std::ios_base::sync_with_stdio(false);
    std::cin.tie(NULL);

    std::string data_dir = (argc > 1) ? argv[1] : "/data";
    std::string out_dir = (argc > 2) ? argv[2] : "/out";

    std::string regions_path = data_dir + "/regions.csv";
    MappedFile rf;
    if (!rf.open(regions_path.c_str())) return 1;

    std::vector<std::string> country_names;
    country_names.reserve(300);

    static constexpr size_t TCAP = 1024;
    static constexpr size_t TMASK = TCAP - 1;
    struct TSlot {
        uint64_t hash = 0;
        uint16_t len = 0;
        uint16_t id = 0;
        char name[16];
    } ctable[TCAP];
    std::memset(ctable, 0, sizeof(ctable));

    auto get_country_id = [&](const char* s, size_t len) -> uint16_t {
        uint64_t h = 0xcbf29ce484222325ULL;
        for (size_t i = 0; i < len; ++i) h = (h ^ (unsigned char)s[i]) * 0x100000001b3ULL;
        size_t idx = h & TMASK;
        while (ctable[idx].hash != 0) {
            if (ctable[idx].hash == h && ctable[idx].len == len && std::memcmp(ctable[idx].name, s, len) == 0) {
                return ctable[idx].id;
            }
            idx = (idx + 1) & TMASK;
        }
        ctable[idx].hash = h ? h : 1;
        ctable[idx].len = (uint16_t)len;
        uint16_t new_id = (uint16_t)country_names.size();
        ctable[idx].id = new_id;
        std::memcpy(ctable[idx].name, s, len);
        ctable[idx].name[len] = '\0';
        country_names.emplace_back(s, len);
        return new_id;
    };

    std::vector<int16_t> region_to_country(1000000, -1);

    const char* rp = rf.data;
    const char* r_limit = rf.data + rf.size;

    if (r_limit - rp >= 3 && (unsigned char)rp[0] == 0xef && (unsigned char)rp[1] == 0xbb && (unsigned char)rp[2] == 0xbf) {
        rp += 3;
    }
    while (rp < r_limit && *rp != '\n') rp++;
    if (rp < r_limit && *rp == '\n') rp++;

    while (rp < r_limit) {
        if (*rp == '\r' || *rp == '\n') { rp++; continue; }
        int32_t rid = 0;
        while (rp < r_limit && *rp >= '0' && *rp <= '9') {
            rid = rid * 10 + (*rp - '0');
            rp++;
        }
        if (rp < r_limit && *rp == ',') rp++;
        const char* c_start = rp;
        while (rp < r_limit && *rp != '\r' && *rp != '\n') rp++;
        size_t c_len = rp - c_start;
        uint16_t cid = get_country_id(c_start, c_len);
        if (rid >= (int32_t)region_to_country.size()) {
            region_to_country.resize(rid + 10000, -1);
        }
        region_to_country[rid] = (int16_t)cid;
    }
    rf.close();

    uint16_t unmatched_id = get_country_id("_UNMATCHED", 10);

    // Sort country names alphabetically and assign canonical IDs
    std::vector<uint16_t> old_to_new_cid(country_names.size());
    std::vector<uint16_t> sorted_order(country_names.size());
    for (size_t i = 0; i < country_names.size(); ++i) sorted_order[i] = (uint16_t)i;
    std::sort(sorted_order.begin(), sorted_order.end(), [&](uint16_t a, uint16_t b) {
        return country_names[a] < country_names[b];
    });

    std::vector<std::string> sorted_country_names(country_names.size());
    for (size_t new_id = 0; new_id < sorted_order.size(); ++new_id) {
        uint16_t old_id = sorted_order[new_id];
        old_to_new_cid[old_id] = (uint16_t)new_id;
        sorted_country_names[new_id] = country_names[old_id];
    }
    country_names = std::move(sorted_country_names);

    // Remap region_to_country to canonical alphabetical IDs
    for (size_t i = 0; i < region_to_country.size(); ++i) {
        if (region_to_country[i] >= 0) {
            region_to_country[i] = old_to_new_cid[region_to_country[i]];
        }
    }
    unmatched_id = old_to_new_cid[unmatched_id];

    size_t reg_table_size = region_to_country.size();
    size_t num_countries = country_names.size();

    std::string records_path = data_dir + "/records.csv";
    MappedFile mf;
    if (!mf.open(records_path.c_str())) return 1;

    if (mf.size == 0) {
        std::string out_path = out_dir + "/aggregates.txt";
        std::ofstream ofs(out_path, std::ios::binary);
        return 0;
    }

    const size_t num_threads = 12;
    std::vector<size_t> split_points;
    split_points.push_back(0);

    size_t running_quotes = 0;
    size_t scan_idx = 0;
    for (size_t i = 1; i < num_threads; ++i) {
        size_t target = i * (mf.size / num_threads);
        while (scan_idx < target && scan_idx < mf.size) {
            if (mf.data[scan_idx] == '"') running_quotes++;
            scan_idx++;
        }
        while (scan_idx < mf.size) {
            if (mf.data[scan_idx] == '"') running_quotes++;
            if (mf.data[scan_idx] == '\n' && (running_quotes % 2 == 0)) {
                split_points.push_back(scan_idx + 1);
                scan_idx++;
                break;
            }
            scan_idx++;
        }
    }
    split_points.push_back(mf.size);

    std::vector<ThreadLocalData> thread_data(num_threads);
    for (size_t t = 0; t < num_threads; ++t) {
        thread_data[t].stats.assign(num_countries, CountryScalars{});
        size_t est_records = (split_points[t + 1] - split_points[t]) / 50;
        thread_data[t].name_entries.reserve(est_records + 1024);
    }

    std::vector<std::thread> threads;
    threads.reserve(num_threads);

    for (size_t t = 0; t < num_threads; ++t) {
        threads.emplace_back([&, t]() {
            size_t start = split_points[t];
            size_t end = split_points[t + 1];
            if (start >= end) return;

            auto& stats = thread_data[t].stats;
            auto& entries = thread_data[t].name_entries;

            const char* p = mf.data + start;
            const char* limit = mf.data + end;

            if (t == 0) {
                if (limit - p >= 3 && (unsigned char)p[0] == 0xef && (unsigned char)p[1] == 0xbb && (unsigned char)p[2] == 0xbf) {
                    p += 3;
                }
                while (p < limit && *p != '\n') p++;
                if (p < limit && *p == '\n') p++;
            }

            while (p < limit) {
                if (*p == '\r' || *p == '\n') { p++; continue; }
                int64_t rec_id = parse_field_int(p, limit);
                uint64_t name_hash = 0;
                size_t name_len = 0;
                parse_field_name_hash(p, limit, name_hash, name_len);
                size_t note_len = parse_field_len_only(p, limit);
                int32_t reg_id = parse_field_int32(p, limit);
                int64_t cents = parse_field_cents(p, limit);

                uint16_t cid = (reg_id >= 0 && (size_t)reg_id < reg_table_size && region_to_country[reg_id] >= 0)
                               ? (uint16_t)region_to_country[reg_id] : unmatched_id;

                auto& cs = stats[cid];
                cs.count++;
                cs.sum_rec_id += rec_id;
                cs.sum_cents += cents;
                if (!cs.has_cents) {
                    cs.min_cents = cents;
                    cs.max_cents = cents;
                    cs.has_cents = true;
                } else {
                    if (cents < cs.min_cents) cs.min_cents = cents;
                    if (cents > cs.max_cents) cs.max_cents = cents;
                }
                cs.sum_name_len += name_len;
                cs.sum_note_len += note_len;
                entries.push_back({cid, name_hash});
            }

#ifndef _WIN32
            if (end > start) {
                uintptr_t page_start = (uintptr_t)(mf.data + start) & ~4095ULL;
                uintptr_t page_end = ((uintptr_t)(mf.data + end) + 4095ULL) & ~4095ULL;
                madvise((void*)page_start, page_end - page_start, MADV_DONTNEED);
            }
#endif

            // Sort entries in place
            std::sort(entries.begin(), entries.end());
            entries.erase(std::unique(entries.begin(), entries.end()), entries.end());
        });
    }

    for (auto& th : threads) th.join();
    mf.close();

    // Output is already sorted by alphabetical cid (0 .. num_countries - 1)
    std::string out_path = out_dir + "/aggregates.txt";
    FILE* fp = fopen(out_path.c_str(), "wb");
    if (!fp) return 1;

    char out_buf[1048576];
    setvbuf(fp, out_buf, _IOFBF, sizeof(out_buf));

    // Pointer index per thread for fast multiway merge
    std::vector<size_t> cur_ptrs(num_threads, 0);
    std::vector<uint64_t> merged_hashes;
    merged_hashes.reserve(8192);

    char line_buf[2048];
    for (uint16_t cid = 0; cid < num_countries; ++cid) {
        CountryScalars combined;
        for (size_t t = 0; t < num_threads; ++t) {
            const auto& cs = thread_data[t].stats[cid];
            if (cs.count == 0) continue;
            combined.count += cs.count;
            combined.sum_rec_id += cs.sum_rec_id;
            combined.sum_cents += cs.sum_cents;
            if (!combined.has_cents) {
                combined.min_cents = cs.min_cents;
                combined.max_cents = cs.max_cents;
                combined.has_cents = true;
            } else {
                if (cs.min_cents < combined.min_cents) combined.min_cents = cs.min_cents;
                if (cs.max_cents > combined.max_cents) combined.max_cents = cs.max_cents;
            }
            combined.sum_name_len += cs.sum_name_len;
            combined.sum_note_len += cs.sum_note_len;
        }

        if (combined.count == 0) continue;

        merged_hashes.clear();
        for (size_t t = 0; t < num_threads; ++t) {
            const auto& entries = thread_data[t].name_entries;
            size_t& p = cur_ptrs[t];
            while (p < entries.size() && entries[p].cid == cid) {
                merged_hashes.push_back(entries[p].hash);
                p++;
            }
        }

        std::sort(merged_hashes.begin(), merged_hashes.end());
        merged_hashes.erase(std::unique(merged_hashes.begin(), merged_hashes.end()), merged_hashes.end());
        size_t unique_names = merged_hashes.size();

        const std::string& cname = country_names[cid];
        char* lp = line_buf;
        std::memcpy(lp, cname.data(), cname.size());
        lp += cname.size();
        *lp++ = ' ';
        lp = fast_i64(lp, combined.count);
        *lp++ = ' ';
        lp = fast_i64(lp, combined.sum_rec_id);
        *lp++ = ' ';
        lp = fast_format_cents(lp, combined.sum_cents);
        *lp++ = ' ';
        lp = fast_format_cents(lp, combined.min_cents);
        *lp++ = ' ';
        lp = fast_format_cents(lp, combined.max_cents);
        *lp++ = ' ';
        lp = fast_i64(lp, unique_names);
        *lp++ = ' ';
        lp = fast_i64(lp, combined.sum_name_len);
        *lp++ = ' ';
        lp = fast_i64(lp, combined.sum_note_len);
        *lp++ = '\n';

        fwrite(line_buf, 1, lp - line_buf, fp);
    }

    fclose(fp);
    return 0;
}
