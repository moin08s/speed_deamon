#include <iostream>
#include <fstream>
#include <vector>
#include <string>
#include <string_view>
#include <unordered_map>
#include <map>
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

struct CountryStats {
    int64_t count = 0;
    int64_t sum_rec_id = 0;
    int64_t sum_cents = 0;
    int64_t min_cents = 0;
    int64_t max_cents = 0;
    bool has_cents = false;
    int64_t sum_name_len = 0;
    int64_t sum_note_len = 0;
    std::vector<uint64_t> name_hashes;
};

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
                unsigned char c = (unsigned char)*p;
                if ((c & 0xC0) != 0x80) utf8_len++;
                out_hash = (out_hash ^ c) * 0x100000001b3ULL;
                p++;
            }
        }
        if (p < limit && *p == ',') p++;
    } else {
        while (p < limit && *p != ',' && *p != '\n' && *p != '\r') {
            unsigned char c = (unsigned char)*p;
            if ((c & 0xC0) != 0x80) utf8_len++;
            out_hash = (out_hash ^ c) * 0x100000001b3ULL;
            p++;
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
                unsigned char c = (unsigned char)*p;
                if ((c & 0xC0) != 0x80) utf8_len++;
                p++;
            }
        }
        if (p < limit && *p == ',') p++;
    } else {
        while (p < limit && *p != ',' && *p != '\n' && *p != '\r') {
            unsigned char c = (unsigned char)*p;
            if ((c & 0xC0) != 0x80) utf8_len++;
            p++;
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

void parse_chunk(const char* buf, size_t start, size_t end, bool is_first,
                 const int16_t* region_to_country, size_t max_reg, int16_t unmatched_id,
                 std::vector<CountryStats>& stats) {
    const char* p = buf + start;
    const char* limit = buf + end;

    if (is_first) {
        if (limit - p >= 3 && (unsigned char)p[0] == 0xef && (unsigned char)p[1] == 0xbb && (unsigned char)p[2] == 0xbf) {
            p += 3;
        }
        while (p < limit && *p != '\n') p++;
        if (p < limit && *p == '\n') p++;
    }

    while (p < limit) {
        if (*p == '\r' || *p == '\n') {
            p++;
            continue;
        }

        int64_t rec_id = parse_field_int(p, limit);
        uint64_t name_hash = 0;
        size_t name_len = 0;
        parse_field_name_hash(p, limit, name_hash, name_len);
        size_t note_len = parse_field_len_only(p, limit);
        int32_t reg_id = parse_field_int32(p, limit);
        int64_t cents = parse_field_cents(p, limit);

        int16_t cid = (reg_id >= 0 && (size_t)reg_id < max_reg) ? region_to_country[reg_id] : unmatched_id;
        if (cid < 0) cid = unmatched_id;

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
        cs.name_hashes.push_back(name_hash);
    }

    for (auto& cs : stats) {
        if (!cs.name_hashes.empty()) {
            std::sort(cs.name_hashes.begin(), cs.name_hashes.end());
            cs.name_hashes.erase(std::unique(cs.name_hashes.begin(), cs.name_hashes.end()), cs.name_hashes.end());
        }
    }
}

std::string format_cents(int64_t cents) {
    std::string s;
    if (cents < 0) {
        s += "-";
        cents = -cents;
    }
    s += std::to_string(cents / 100);
    s += ".";
    int rem = cents % 100;
    if (rem < 10) s += "0";
    s += std::to_string(rem);
    return s;
}

int main(int argc, char** argv) {
    std::ios_base::sync_with_stdio(false);
    std::cin.tie(NULL);

    std::string data_dir = (argc > 1) ? argv[1] : "/data";
    std::string out_dir = (argc > 2) ? argv[2] : "/out";

    std::string regions_path = data_dir + "/regions.csv";
    std::ifstream rf(regions_path);
    if (!rf) return 1;

    std::string line;
    std::getline(rf, line);

    std::vector<std::string> countries;
    std::unordered_map<std::string, int16_t> country_to_id;
    auto get_country_id = [&](const std::string& c) -> int16_t {
        auto it = country_to_id.find(c);
        if (it != country_to_id.end()) return it->second;
        int16_t id = (int16_t)countries.size();
        countries.push_back(c);
        country_to_id[c] = id;
        return id;
    };

    std::vector<std::pair<int32_t, int16_t>> reg_pairs;
    int32_t max_region_id = 0;

    while (std::getline(rf, line)) {
        while (!line.empty() && (line.back() == '\r' || line.back() == '\n')) line.pop_back();
        if (line.empty()) continue;
        size_t comma = line.find(',');
        if (comma != std::string::npos) {
            int32_t r_id = std::stoi(line.substr(0, comma));
            std::string c = line.substr(comma + 1);
            int16_t cid = get_country_id(c);
            reg_pairs.emplace_back(r_id, cid);
            if (r_id > max_region_id) max_region_id = r_id;
        }
    }
    rf.close();

    int16_t unmatched_id = get_country_id("_UNMATCHED");
    size_t reg_table_size = max_region_id + 1;
    std::vector<int16_t> region_to_country(reg_table_size, -1);
    for (auto& [rid, cid] : reg_pairs) {
        if (rid >= 0 && (size_t)rid < reg_table_size) {
            region_to_country[rid] = cid;
        }
    }

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

    std::vector<std::vector<CountryStats>> thread_stats(num_threads, std::vector<CountryStats>(countries.size()));
    std::vector<std::thread> threads;
    threads.reserve(num_threads);

    for (size_t i = 0; i < num_threads; ++i) {
        threads.emplace_back(parse_chunk, mf.data, split_points[i], split_points[i+1], i == 0,
                             region_to_country.data(), reg_table_size, unmatched_id,
                             std::ref(thread_stats[i]));
    }

    for (auto& t : threads) {
        t.join();
    }
    mf.close(); // Immediately unmap records.csv to drop page cache

    struct FinalCountryEntry {
        std::string country;
        CountryStats cs;
    };
    std::vector<std::vector<FinalCountryEntry>> thread_finals(num_threads);
    threads.clear();
    size_t c_chunk = (countries.size() + num_threads - 1) / num_threads;

    for (size_t t = 0; t < num_threads; ++t) {
        size_t c_start = t * c_chunk;
        size_t c_end = std::min(countries.size(), c_start + c_chunk);
        threads.emplace_back([&, t, c_start, c_end]() {
            for (size_t i = c_start; i < c_end; ++i) {
                CountryStats combined;
                for (size_t pt = 0; pt < num_threads; ++pt) {
                    const auto& ts = thread_stats[pt][i];
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
                    combined.name_hashes.insert(combined.name_hashes.end(), ts.name_hashes.begin(), ts.name_hashes.end());
                }
                if (combined.count > 0) {
                    std::sort(combined.name_hashes.begin(), combined.name_hashes.end());
                    combined.name_hashes.erase(std::unique(combined.name_hashes.begin(), combined.name_hashes.end()), combined.name_hashes.end());
                    thread_finals[t].push_back({countries[i], std::move(combined)});
                }
            }
        });
    }

    for (auto& t : threads) t.join();

    std::vector<FinalCountryEntry> final_entries;
    final_entries.reserve(countries.size());
    for (size_t t = 0; t < num_threads; ++t) {
        for (auto& entry : thread_finals[t]) {
            final_entries.push_back(std::move(entry));
        }
    }

    std::sort(final_entries.begin(), final_entries.end(), [](const FinalCountryEntry& a, const FinalCountryEntry& b) {
        return a.country < b.country;
    });

    std::string out_path = out_dir + "/aggregates.txt";
    FILE* fp = fopen(out_path.c_str(), "wb");
    if (!fp) return 1;

    char out_buf[1048576];
    setvbuf(fp, out_buf, _IOFBF, sizeof(out_buf));

    char line_buf[2048];
    for (auto& entry : final_entries) {
        const auto& cs = entry.cs;
        std::string s_cents = format_cents(cs.sum_cents);
        std::string s_min = format_cents(cs.min_cents);
        std::string s_max = format_cents(cs.max_cents);

        int len = snprintf(line_buf, sizeof(line_buf), "%s %lld %lld %s %s %s %zu %lld %lld\n",
                           entry.country.c_str(), (long long)cs.count, (long long)cs.sum_rec_id,
                           s_cents.c_str(), s_min.c_str(), s_max.c_str(),
                           cs.name_hashes.size(), (long long)cs.sum_name_len, (long long)cs.sum_note_len);
        fwrite(line_buf, 1, len, fp);
    }
    fclose(fp);

    return 0;
}
