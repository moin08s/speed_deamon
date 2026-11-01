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
#include <malloc.h>
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
    if (val == 0) {
        *p++ = '0';
        return p;
    }
    if (val < 0) {
        *p++ = '-';
        val = -val;
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
    int64_t whole = 0;
    while (p < limit && *p >= '0' && *p <= '9') {
        whole = whole * 10 + (*p - '0');
        p++;
    }
    int64_t frac = 0;
    if (p < limit && *p == '.') {
        p++;
        int digits = 0;
        while (p < limit && *p >= '0' && *p <= '9') {
            if (digits < 2) {
                frac = frac * 10 + (*p - '0');
                digits++;
            }
            p++;
        }
        if (digits == 1) frac *= 10;
    }
    if (quoted && p < limit && *p == '"') p++;
    while (p < limit && *p != '\n' && *p != '\r') p++;
    if (p < limit && *p == '\r') p++;
    if (p < limit && *p == '\n') p++;

    int64_t cents = whole * 100 + frac;
    return neg ? -cents : cents;
}

size_t find_fast_split(const char* data, size_t size, size_t target) {
    size_t cur_pos = target;
    while (cur_pos < size) {
        const char* nl_ptr = (const char*)std::memchr(data + cur_pos, '\n', size - cur_pos);
        if (!nl_ptr) return size;
        size_t nl = nl_ptr - data;

        // Check if preceded by .\d\d
        if (nl >= 4 && data[nl - 3] == '.' &&
            data[nl - 2] >= '0' && data[nl - 2] <= '9' &&
            data[nl - 1] >= '0' && data[nl - 1] <= '9') {
            
            // Find line start
            size_t search_back = (nl > 1000) ? nl - 1000 : 0;
            const char* cur = data + nl - 1;
            while (cur >= data + search_back && *cur != '\n') cur--;
            size_t line_start = (cur >= data + search_back) ? (cur - data + 1) : 0;

            // Check if line starts with digits followed by comma
            size_t p = line_start;
            while (p < nl && data[p] >= '0' && data[p] <= '9') p++;
            if (p > line_start && p < nl && data[p] == ',') {
                // Count quotes on this single line
                size_t line_quotes = 0;
                for (size_t q = line_start; q < nl; ++q) {
                    if (data[q] == '"') line_quotes++;
                }
                if ((line_quotes % 2) == 0) {
                    return nl + 1; // Valid record boundary!
                }
            }
        }
        cur_pos = nl + 1;
    }
    return size;
}

int main(int argc, char** argv) {
    std::ios_base::sync_with_stdio(false);
    std::cin.tie(NULL);

#ifndef _WIN32
    mallopt(M_ARENA_MAX, 2);
#endif

    std::string data_dir = (argc > 1) ? argv[1] : "/data";
    std::string out_dir = (argc > 2) ? argv[2] : "/out";

    std::string regions_path = data_dir + "/regions.csv";
    MappedFile rf;
    if (!rf.open(regions_path.c_str())) return 1;

    std::vector<std::string> raw_country_names;
    raw_country_names.reserve(300);

    static constexpr size_t CAP = 1024;
    static constexpr size_t MASK = CAP - 1;
    struct Slot {
        uint64_t hash = 0;
        uint16_t len = 0;
        uint16_t id = 0;
        char name[16];
    } table[CAP];
    std::memset(table, 0, sizeof(table));

    auto get_country_id = [&](const char* s, size_t len) -> uint16_t {
        uint64_t h = 0xcbf29ce484222325ULL;
        for (size_t i = 0; i < len; ++i) h = (h ^ (unsigned char)s[i]) * 0x100000001b3ULL;
        size_t idx = h & MASK;
        while (table[idx].hash != 0) {
            if (table[idx].hash == h && table[idx].len == len && std::memcmp(table[idx].name, s, len) == 0) {
                return table[idx].id;
            }
            idx = (idx + 1) & MASK;
        }
        table[idx].hash = h ? h : 1;
        table[idx].len = (uint16_t)len;
        uint16_t id = (uint16_t)raw_country_names.size();
        table[idx].id = id;
        std::memcpy(table[idx].name, s, len);
        table[idx].name[len] = '\0';
        raw_country_names.emplace_back(s, len);
        return id;
    };

    std::vector<int16_t> region_to_country(1000005, -1);

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

    uint16_t unmatched_raw = get_country_id("_UNMATCHED", 10);

    // Canonical alphabetical sort of countries
    size_t num_countries = raw_country_names.size();
    std::vector<uint16_t> sorted_order(num_countries);
    for (size_t i = 0; i < num_countries; ++i) sorted_order[i] = (uint16_t)i;
    std::sort(sorted_order.begin(), sorted_order.end(), [&](uint16_t a, uint16_t b) {
        return raw_country_names[a] < raw_country_names[b];
    });

    std::vector<uint16_t> raw_to_canonical(num_countries);
    std::vector<std::string> country_names(num_countries);
    for (size_t new_id = 0; new_id < num_countries; ++new_id) {
        uint16_t old_id = sorted_order[new_id];
        raw_to_canonical[old_id] = (uint16_t)new_id;
        country_names[new_id] = raw_country_names[old_id];
    }

    for (size_t i = 0; i < region_to_country.size(); ++i) {
        if (region_to_country[i] >= 0) {
            region_to_country[i] = (int16_t)raw_to_canonical[region_to_country[i]];
        }
    }
    uint16_t unmatched_id = raw_to_canonical[unmatched_raw];
    size_t reg_table_size = region_to_country.size();

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

    for (size_t i = 1; i < num_threads; ++i) {
        size_t target = i * (mf.size / num_threads);
        split_points.push_back(find_fast_split(mf.data, mf.size, target));
    }
    split_points.push_back(mf.size);

    std::vector<std::vector<CountryStats>> thread_stats(num_threads, std::vector<CountryStats>(num_countries));
    std::vector<std::vector<uint64_t>> thread_entries(num_threads);
    std::vector<std::vector<uint32_t>> thread_cid_offsets(num_threads, std::vector<uint32_t>(num_countries + 1, 0));

    std::vector<std::thread> threads;
    threads.reserve(num_threads);

    for (size_t t = 0; t < num_threads; ++t) {
        threads.emplace_back([&, t]() {
            size_t start = split_points[t];
            size_t end = split_points[t + 1];
            if (start >= end) return;

            auto& stats = thread_stats[t];
            auto& entries = thread_entries[t];
            entries.reserve(65536);

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

                entries.push_back(((uint64_t)cid << 48) | (name_hash & 0x0000FFFFFFFFFFFFULL));
            }

            // Single sort + unique on flat contiguous 64-bit entries
            if (!entries.empty()) {
                std::sort(entries.begin(), entries.end());
                entries.erase(std::unique(entries.begin(), entries.end()), entries.end());

                // Build O(1) cid offset index
                auto& offsets = thread_cid_offsets[t];
                size_t cur_e = 0;
                size_t total_e = entries.size();
                for (size_t cid = 0; cid < num_countries; ++cid) {
                    offsets[cid] = (uint32_t)cur_e;
                    while (cur_e < total_e && (entries[cur_e] >> 48) == cid) {
                        cur_e++;
                    }
                }
                offsets[num_countries] = (uint32_t)total_e;
            }
        });
    }

    for (auto& th : threads) th.join();
    mf.close(); // Immediately unmap records.csv to drop page cache

    // Phase 2: Parallel country reduction across 12 threads directly producing output strings
    std::vector<std::string> thread_outputs(num_threads);
    threads.clear();
    size_t c_chunk = (num_countries + num_threads - 1) / num_threads;

    for (size_t t = 0; t < num_threads; ++t) {
        size_t c_start = t * c_chunk;
        size_t c_end = std::min(num_countries, c_start + c_chunk);
        threads.emplace_back([&, t, c_start, c_end]() {
            if (c_start >= c_end) return;
            std::string& out = thread_outputs[t];
            out.reserve((c_end - c_start) * 256);
            char line_buf[2048];
            std::vector<uint64_t> cid_hashes;
            cid_hashes.reserve(4096);

            for (size_t cid = c_start; cid < c_end; ++cid) {
                CountryStats combined;
                for (size_t pt = 0; pt < num_threads; ++pt) {
                    const auto& ts = thread_stats[pt][cid];
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
                }

                if (combined.count == 0) continue;

                // Deduplicate unique names across threads using O(1) slice offsets
                size_t unique_names = 0;
                if (combined.count == 1) {
                    unique_names = 1;
                } else {
                    cid_hashes.clear();
                    for (size_t pt = 0; pt < num_threads; ++pt) {
                        uint32_t st = thread_cid_offsets[pt][cid];
                        uint32_t en = thread_cid_offsets[pt][cid + 1];
                        if (en > st) {
                            const auto& entries = thread_entries[pt];
                            cid_hashes.insert(cid_hashes.end(), entries.begin() + st, entries.begin() + en);
                        }
                    }
                    std::sort(cid_hashes.begin(), cid_hashes.end());
                    cid_hashes.erase(std::unique(cid_hashes.begin(), cid_hashes.end()), cid_hashes.end());
                    unique_names = cid_hashes.size();
                }

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

                out.append(line_buf, lp - line_buf);
            }
        });
    }

    for (auto& th : threads) th.join();

    std::string out_path = out_dir + "/aggregates.txt";
    FILE* fp = fopen(out_path.c_str(), "wb");
    if (!fp) return 1;

    char out_buf[1048576];
    setvbuf(fp, out_buf, _IOFBF, sizeof(out_buf));

    for (size_t t = 0; t < num_threads; ++t) {
        if (!thread_outputs[t].empty()) {
            fwrite(thread_outputs[t].data(), 1, thread_outputs[t].size(), fp);
        }
    }
    fclose(fp);

    return 0;
}
