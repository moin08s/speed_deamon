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

    ~MappedFile() {
#ifdef _WIN32
        if (data) UnmapViewOfFile(data);
        if (hMap) CloseHandle(hMap);
        if (hFile != INVALID_HANDLE_VALUE) CloseHandle(hFile);
#else
        if (data && data != MAP_FAILED) munmap((void*)data, size);
        if (fd >= 0) ::close(fd);
#endif
    }
};

struct FastStringMap {
    static constexpr uint32_t CAPACITY = 8192;
    static constexpr uint32_t MASK = CAPACITY - 1;

    struct Entry {
        uint64_t hash = 0;
        uint32_t len = 0;
        const char* ptr = nullptr;
        int id = -1;
    };

    Entry table[CAPACITY];
    int count = 0;

    FastStringMap() {
        std::memset(table, 0, sizeof(table));
    }

    static inline uint64_t hash_bytes(const char* p, size_t len) {
        uint64_t h = 0xcbf29ce484222325ULL;
        for (size_t i = 0; i < len; ++i) {
            h = (h ^ (unsigned char)p[i]) * 0x100000001b3ULL;
        }
        return h ? h : 1;
    }

    inline int get_or_insert(const char* p, size_t len) {
        uint64_t h = hash_bytes(p, len);
        uint32_t idx = (uint32_t)h & MASK;
        while (table[idx].hash != 0) {
            if (table[idx].hash == h && table[idx].len == len && std::memcmp(table[idx].ptr, p, len) == 0) {
                return table[idx].id;
            }
            idx = (idx + 1) & MASK;
        }
        table[idx].hash = h;
        table[idx].len = (uint32_t)len;
        table[idx].ptr = p;
        table[idx].id = count++;
        return table[idx].id;
    }
};

struct LogEntry {
    uint32_t key; // (hid << 13) | (eid << 1) | is_err
    uint32_t lat;

    inline bool operator<(const LogEntry& o) const {
        return key < o.key;
    }
};

struct ThreadData {
    FastStringMap hour_map;
    FastStringMap ep_map;
    std::vector<std::string> hours;
    std::vector<std::string> endpoints;
    std::vector<LogEntry> entries;
};

void parse_chunk(const char* buf, size_t start, size_t end, ThreadData& td) {
    const char* p = buf + start;
    const char* limit = buf + end;

    td.entries.reserve((end - start) / 40);

    int last_hid = -1;
    const char* last_hour_ptr = nullptr;

    while (p < limit) {
        if (*p == '\r' || *p == '\n') {
            p++;
            continue;
        }

        if (limit - p < 35) break;

        // Hour: p[0..12]
        int hid = last_hid;
        if (last_hour_ptr == nullptr || std::memcmp(last_hour_ptr, p, 13) != 0) {
            hid = td.hour_map.get_or_insert(p, 13);
            if (hid >= (int)td.hours.size()) {
                td.hours.emplace_back(p, 13);
            }
            last_hid = hid;
            last_hour_ptr = p;
        }

        // Method starts at p + 28
        const char* m_scan = p + 28;
        while (m_scan < limit && *m_scan != ' ') m_scan++;
        if (m_scan >= limit) break;

        // Endpoint starts at m_scan + 1
        const char* ep_start = m_scan + 1;
        const char* ep_end = ep_start;
        while (ep_end < limit && *ep_end != ' ') ep_end++;
        if (ep_end >= limit) break;

        size_t ep_len = ep_end - ep_start;
        int eid = td.ep_map.get_or_insert(ep_start, ep_len);
        if (eid >= (int)td.endpoints.size()) {
            td.endpoints.emplace_back(ep_start, ep_len);
        }

        // Status: 3 digits after space
        const char* status_ptr = ep_end + 1;
        bool is_err = (status_ptr < limit && *status_ptr == '5');

        // Latency
        const char* lat_ptr = status_ptr + 4;
        uint32_t lat = 0;
        while (lat_ptr < limit && *lat_ptr >= '0' && *lat_ptr <= '9') {
            lat = lat * 10 + (*lat_ptr - '0');
            lat_ptr++;
        }

        uint32_t key = (uint32_t)((hid << 13) | (eid << 1) | (is_err ? 1 : 0));
        td.entries.push_back({key, lat});

        while (lat_ptr < limit && *lat_ptr != '\n') lat_ptr++;
        p = (lat_ptr < limit) ? lat_ptr + 1 : limit;
    }

    std::sort(td.entries.begin(), td.entries.end());
}

inline char* fast_u32(char* p, uint32_t val) {
    if (val == 0) {
        *p++ = '0';
        return p;
    }
    char buf[10];
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

inline char* fast_u64(char* p, uint64_t val) {
    if (val == 0) {
        *p++ = '0';
        return p;
    }
    char buf[20];
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

struct OutGroup {
    std::string hour;
    std::string endpoint;
    std::vector<uint32_t> latencies;
    uint32_t errors = 0;
};

void format_worker(OutGroup* groups, size_t start, size_t end, std::string& out_buf) {
    out_buf.reserve((end - start) * 96);
    char line_buf[2048];

    for (size_t i = start; i < end; ++i) {
        auto& grp = groups[i];
        auto& lat = grp.latencies;
        size_t n = lat.size();
        if (n == 0) continue;

        size_t idx50 = (50ULL * n + 99) / 100 - 1;
        size_t idx95 = (95ULL * n + 99) / 100 - 1;
        size_t idx99 = (99ULL * n + 99) / 100 - 1;

        uint32_t p50, p95, p99;
        if (n <= 16) {
            std::sort(lat.begin(), lat.end());
            p50 = lat[idx50];
            p95 = lat[idx95];
            p99 = lat[idx99];
        } else {
            std::nth_element(lat.begin(), lat.begin() + idx50, lat.end());
            p50 = lat[idx50];
            std::nth_element(lat.begin() + idx50 + 1, lat.begin() + idx95, lat.end());
            p95 = lat[idx95];
            std::nth_element(lat.begin() + idx95 + 1, lat.begin() + idx99, lat.end());
            p99 = lat[idx99];
        }

        uint64_t ppm = (uint64_t)grp.errors * 1000000ULL / n;

        char* p = line_buf;
        std::memcpy(p, grp.hour.data(), 13);
        p += 13;
        std::memcpy(p, ":00:00Z ", 8);
        p += 8;
        std::memcpy(p, grp.endpoint.data(), grp.endpoint.size());
        p += grp.endpoint.size();
        *p++ = ' ';
        p = fast_u64(p, n);
        *p++ = ' ';
        p = fast_u32(p, p50);
        *p++ = ' ';
        p = fast_u32(p, p95);
        *p++ = ' ';
        p = fast_u32(p, p99);
        *p++ = ' ';
        p = fast_u32(p, grp.errors);
        *p++ = ' ';
        p = fast_u64(p, ppm);
        *p++ = '\n';

        out_buf.append(line_buf, p - line_buf);
    }
}

int main(int argc, char** argv) {
    std::ios_base::sync_with_stdio(false);
    std::cin.tie(NULL);

    std::string data_dir = (argc > 1) ? argv[1] : "/data";
    std::string out_dir = (argc > 2) ? argv[2] : "/out";

    std::string input_path = data_dir + "/requests.log";
    MappedFile mf;
    if (!mf.open(input_path.c_str())) return 1;

    if (mf.size == 0) {
        std::string out_path = out_dir + "/summary.txt";
        std::ofstream ofs(out_path, std::ios::binary);
        return 0;
    }

    const size_t num_threads = 12;
    std::vector<size_t> split_points;
    split_points.push_back(0);

    for (size_t i = 1; i < num_threads; ++i) {
        size_t target = i * (mf.size / num_threads);
        const char* nl = (const char*)memchr(mf.data + target, '\n', mf.size - target);
        if (nl) split_points.push_back((nl - mf.data) + 1);
        else split_points.push_back(mf.size);
    }
    split_points.push_back(mf.size);

    std::vector<ThreadData> thread_data(num_threads);
    std::vector<std::thread> threads;
    threads.reserve(num_threads);

    for (size_t i = 0; i < num_threads; ++i) {
        threads.emplace_back(parse_chunk, mf.data, split_points[i], split_points[i+1], std::ref(thread_data[i]));
    }

    for (auto& t : threads) {
        t.join();
    }

    // Reconstruct global hour and endpoint maps
    std::vector<OutGroup> out_groups;
    out_groups.reserve(70000);

    struct MergeEntry {
        uint64_t hash = 0;
        int group_idx = -1;
    };
    constexpr uint32_t MERGE_CAP = 262144;
    constexpr uint32_t MERGE_MASK = MERGE_CAP - 1;
    std::vector<MergeEntry> merge_table(MERGE_CAP);

    for (size_t t = 0; t < num_threads; ++t) {
        auto& td = thread_data[t];
        const auto& entries = td.entries;
        size_t n_entries = entries.size();
        size_t idx = 0;
        while (idx < n_entries) {
            uint32_t raw_key = entries[idx].key;
            uint32_t group_key = raw_key >> 1; // without is_err
            int hid = group_key >> 12;
            int eid = group_key & 0xFFF;

            const std::string& hr = td.hours[hid];
            const std::string& ep = td.endpoints[eid];

            uint64_t hsh = FastStringMap::hash_bytes(hr.data(), hr.size());
            hsh ^= FastStringMap::hash_bytes(ep.data(), ep.size()) + 0x9e3779b97f4a7c15ULL + (hsh << 6) + (hsh >> 2);
            if (hsh == 0) hsh = 1;

            uint32_t s_idx = (uint32_t)hsh & MERGE_MASK;
            int found_idx = -1;
            while (merge_table[s_idx].hash != 0) {
                if (merge_table[s_idx].hash == hsh) {
                    int g = merge_table[s_idx].group_idx;
                    if (out_groups[g].hour == hr && out_groups[g].endpoint == ep) {
                        found_idx = g;
                        break;
                    }
                }
                s_idx = (s_idx + 1) & MERGE_MASK;
            }

            if (found_idx == -1) {
                found_idx = (int)out_groups.size();
                out_groups.push_back({hr, ep, {}, 0});
                merge_table[s_idx].hash = hsh;
                merge_table[s_idx].group_idx = found_idx;
            }

            auto& target = out_groups[found_idx];
            while (idx < n_entries && (entries[idx].key >> 1) == group_key) {
                target.latencies.push_back(entries[idx].lat);
                if (entries[idx].key & 1) target.errors++;
                idx++;
            }
        }
    }

    std::sort(out_groups.begin(), out_groups.end(), [](const OutGroup& a, const OutGroup& b) {
        if (a.hour != b.hour) return a.hour < b.hour;
        return a.endpoint < b.endpoint;
    });

    // Parallel formatting across 12 threads
    std::vector<std::string> thread_outputs(num_threads);
    std::vector<std::thread> format_threads;
    format_threads.reserve(num_threads);

    size_t total_out = out_groups.size();
    size_t chunk_sz = (total_out + num_threads - 1) / num_threads;

    for (size_t i = 0; i < num_threads; ++i) {
        size_t s = i * chunk_sz;
        size_t e = std::min(s + chunk_sz, total_out);
        format_threads.emplace_back(format_worker, out_groups.data(), s, e, std::ref(thread_outputs[i]));
    }

    for (auto& t : format_threads) {
        t.join();
    }

    std::string out_path = out_dir + "/summary.txt";
    FILE* fp = fopen(out_path.c_str(), "wb");
    if (!fp) return 1;
    char file_buf[2097152];
    setvbuf(fp, file_buf, _IOFBF, sizeof(file_buf));

    for (size_t i = 0; i < num_threads; ++i) {
        if (!thread_outputs[i].empty()) {
            fwrite(thread_outputs[i].data(), 1, thread_outputs[i].size(), fp);
        }
    }
    fclose(fp);

    return 0;
}
