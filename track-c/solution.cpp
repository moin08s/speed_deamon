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
        data = (const char*)mmap(nullptr, size, PROT_READ, MAP_PRIVATE, fd, 0);
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

    ~MappedFile() {
        close();
    }
};

struct NameEntry {
    uint16_t cid;
    uint64_t hash;
};

struct CountryStats {
    int64_t count = 0;
    int64_t sum_rec_id = 0;
    int64_t sum_cents = 0;
    int64_t min_cents = 0;
    int64_t max_cents = 0;
    bool has_cents = false;
    int64_t sum_name_len = 0;
    int64_t sum_note_len = 0;
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

    std::vector<std::string> countries;
    countries.reserve(512);

    auto get_country_id = [&](const char* name, size_t len) -> uint16_t {
        for (size_t i = 0; i < countries.size(); ++i) {
            if (countries[i].size() == len && std::memcmp(countries[i].data(), name, len) == 0) {
                return (uint16_t)i;
            }
        }
        uint16_t id = (uint16_t)countries.size();
        countries.emplace_back(name, len);
        return id;
    };

    struct RegionPair { int32_t rid; uint16_t cid; };
    std::vector<RegionPair> reg_pairs;
    reg_pairs.reserve(262144);
    int32_t max_region_id = 0;

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
        reg_pairs.push_back({rid, cid});
        if (rid > max_region_id) max_region_id = rid;
    }
    rf.close(); // Unmap regions.csv immediately

    uint16_t unmatched_id = get_country_id("_UNMATCHED", 10);
    size_t reg_table_size = max_region_id + 1;
    std::vector<int16_t> region_to_country(reg_table_size, -1);
    for (const auto& rp_pair : reg_pairs) {
        if (rp_pair.rid >= 0 && (size_t)rp_pair.rid < reg_table_size) {
            region_to_country[rp_pair.rid] = (int16_t)rp_pair.cid;
        }
    }
    reg_pairs.clear();
    reg_pairs.shrink_to_fit();

    std::string records_path = data_dir + "/records.csv";
    MappedFile mf;
    if (!mf.open(records_path.c_str())) return 1;

    if (mf.size == 0) {
        std::string out_path = out_dir + "/aggregates.txt";
        std::ofstream ofs(out_path, std::ios::binary);
        return 0;
    }

    const size_t num_threads = 6;
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

    size_t num_countries = countries.size();
    std::vector<std::vector<CountryStats>> thread_stats(num_threads, std::vector<CountryStats>(num_countries));
    std::vector<std::vector<NameEntry>> thread_names(num_threads);
    std::vector<std::thread> threads;
    threads.reserve(num_threads);

    for (size_t t = 0; t < num_threads; ++t) {
        threads.emplace_back([&, t]() {
            size_t start = split_points[t];
            size_t end = split_points[t + 1];
            if (start >= end) return;

            size_t chunk_bytes = end - start;
            size_t max_records = (chunk_bytes / 20) + 1024;
            auto& names = thread_names[t];
            names.resize(max_records);
            size_t name_count = 0;

            auto& stats = thread_stats[t];
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

                if (name_count >= names.size()) {
                    names.resize(names.size() * 2);
                }
                names[name_count++] = {cid, name_hash};
            }

            names.resize(name_count);
            std::sort(names.begin(), names.end(), [](const NameEntry& a, const NameEntry& b) {
                if (a.cid != b.cid) return a.cid < b.cid;
                return a.hash < b.hash;
            });
            auto unique_end = std::unique(names.begin(), names.end(), [](const NameEntry& a, const NameEntry& b) {
                return a.cid == b.cid && a.hash == b.hash;
            });
            names.erase(unique_end, names.end());
        });
    }

    for (auto& th : threads) th.join();
    mf.close(); // Unmap records.csv immediately

    // Build per-country name hash slices for each thread
    std::vector<std::vector<std::pair<size_t, size_t>>> thread_country_ranges(num_threads,
        std::vector<std::pair<size_t, size_t>>(num_countries, {0, 0}));

    for (size_t t = 0; t < num_threads; ++t) {
        const auto& names = thread_names[t];
        size_t idx = 0;
        size_t total = names.size();
        while (idx < total) {
            uint16_t cid = names[idx].cid;
            size_t start_idx = idx;
            while (idx < total && names[idx].cid == cid) idx++;
            thread_country_ranges[t][cid] = {start_idx, idx};
        }
    }

    struct FinalResult {
        std::string country;
        CountryStats cs;
        size_t distinct_names = 0;
    };
    std::vector<FinalResult> finals;
    finals.reserve(num_countries);

    std::vector<uint64_t> merged_hashes;
    merged_hashes.reserve(16384);

    for (size_t cid = 0; cid < num_countries; ++cid) {
        CountryStats combined;
        merged_hashes.clear();

        for (size_t t = 0; t < num_threads; ++t) {
            const auto& ts = thread_stats[t][cid];
            if (ts.count == 0) continue;
            combined.count += ts.count;
            combined.sum_rec_id += ts.sum_rec_id;
            combined.sum_cents += ts.sum_cents;
            if (!combined.has_cents) {
                combined.min_cents = ts.min_cents;
                combined.max_cents = ts.max_cents;
                combined.has_cents = true;
            } else {
                if (ts.min_cents < combined.min_cents) combined.min_cents = ts.min_cents;
                if (ts.max_cents > combined.max_cents) combined.max_cents = ts.max_cents;
            }
            combined.sum_name_len += ts.sum_name_len;
            combined.sum_note_len += ts.sum_note_len;

            auto [r_start, r_end] = thread_country_ranges[t][cid];
            for (size_t k = r_start; k < r_end; ++k) {
                merged_hashes.push_back(thread_names[t][k].hash);
            }
        }

        if (combined.count > 0) {
            std::sort(merged_hashes.begin(), merged_hashes.end());
            merged_hashes.erase(std::unique(merged_hashes.begin(), merged_hashes.end()), merged_hashes.end());
            finals.push_back({countries[cid], combined, merged_hashes.size()});
        }
    }

    std::sort(finals.begin(), finals.end(), [](const FinalResult& a, const FinalResult& b) {
        return a.country < b.country;
    });

    std::string out_path = out_dir + "/aggregates.txt";
    FILE* fp = fopen(out_path.c_str(), "wb");
    if (!fp) return 1;

    char out_buf[1048576];
    setvbuf(fp, out_buf, _IOFBF, sizeof(out_buf));

    char line_buf[2048];
    for (const auto& entry : finals) {
        const auto& cs = entry.cs;
        char* p = line_buf;
        std::memcpy(p, entry.country.data(), entry.country.size());
        p += entry.country.size();
        *p++ = ' ';
        p = fast_i64(p, cs.count);
        *p++ = ' ';
        p = fast_i64(p, cs.sum_rec_id);
        *p++ = ' ';
        p = fast_format_cents(p, cs.sum_cents);
        *p++ = ' ';
        p = fast_format_cents(p, cs.min_cents);
        *p++ = ' ';
        p = fast_format_cents(p, cs.max_cents);
        *p++ = ' ';
        p = fast_i64(p, entry.distinct_names);
        *p++ = ' ';
        p = fast_i64(p, cs.sum_name_len);
        *p++ = ' ';
        p = fast_i64(p, cs.sum_note_len);
        *p++ = '\n';

        fwrite(line_buf, 1, p - line_buf, fp);
    }
    fclose(fp);

    return 0;
}
