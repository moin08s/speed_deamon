#include <iostream>
#include <fstream>
#include <vector>
#include <string>
#include <string_view>
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

struct FastStringMap {
    static constexpr uint32_t CAPACITY = 4096;
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

struct RawRecord {
    uint16_t eid;
    uint32_t lat;
    uint8_t is_err;
};

struct ThreadParseData {
    FastStringMap hour_map;
    FastStringMap ep_map;
    std::vector<std::string> hours;
    std::vector<std::string> endpoints;
    std::vector<std::vector<RawRecord>> hour_records;
    std::vector<int> remap_eid;
};

void parse_chunk(const char* buf, size_t start, size_t end, ThreadParseData& td) {
    const char* p = buf + start;
    const char* limit = buf + end;

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
            if (hid >= (int)td.hour_records.size()) {
                td.hour_records.resize(hid + 1);
                td.hour_records[hid].reserve(4096);
            }
            last_hid = hid;
            last_hour_ptr = p;
        }

        // Fast method jump
        const char* ep_start = nullptr;
        if (p + 31 < limit && p[31] == ' ') ep_start = p + 32;
        else if (p + 32 < limit && p[32] == ' ') ep_start = p + 33;
        else if (p + 34 < limit && p[34] == ' ') ep_start = p + 35;
        else {
            const char* m_scan = p + 28;
            while (m_scan < limit && *m_scan != ' ') m_scan++;
            if (m_scan >= limit) break;
            ep_start = m_scan + 1;
        }
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
        uint8_t is_err = (status_ptr < limit && *status_ptr == '5') ? 1 : 0;

        // Latency
        const char* lat_ptr = status_ptr + 4;
        uint32_t lat = 0;
        while (lat_ptr < limit && *lat_ptr >= '0' && *lat_ptr <= '9') {
            lat = lat * 10 + (*lat_ptr - '0');
            lat_ptr++;
        }

        td.hour_records[hid].push_back({(uint16_t)eid, lat, is_err});

        while (lat_ptr < limit && *lat_ptr != '\n') lat_ptr++;
        p = (lat_ptr < limit) ? lat_ptr + 1 : limit;
    }

#ifndef _WIN32
    if (end > start) {
        uintptr_t page_start = (uintptr_t)(buf + start) & ~4095ULL;
        uintptr_t page_end = ((uintptr_t)(buf + end) + 4095ULL) & ~4095ULL;
        madvise((void*)page_start, page_end - page_start, MADV_DONTNEED);
    }
#endif
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

int main(int argc, char** argv) {
#ifndef _WIN32
    mallopt(M_ARENA_MAX, 2);
    mallopt(M_TRIM_THRESHOLD, 64 * 1024);
#endif
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

    std::vector<ThreadParseData> parse_data(num_threads);
    std::vector<std::thread> threads;
    threads.reserve(num_threads);

    for (size_t i = 0; i < num_threads; ++i) {
        threads.emplace_back(parse_chunk, mf.data, split_points[i], split_points[i+1], std::ref(parse_data[i]));
    }

    for (auto& t : threads) {
        t.join();
    }
    mf.close(); // Immediately unmap requests.log to drop page cache

    // Collect and sort global hours
    std::vector<std::string> global_hours;
    for (size_t t = 0; t < num_threads; ++t) {
        for (const auto& h : parse_data[t].hours) {
            global_hours.push_back(h);
        }
    }
    std::sort(global_hours.begin(), global_hours.end());
    global_hours.erase(std::unique(global_hours.begin(), global_hours.end()), global_hours.end());

    size_t num_hours = global_hours.size();

    // Collect global endpoints
    std::vector<std::string> global_endpoints;
    for (size_t t = 0; t < num_threads; ++t) {
        for (const auto& ep : parse_data[t].endpoints) {
            global_endpoints.push_back(ep);
        }
    }
    std::sort(global_endpoints.begin(), global_endpoints.end());
    global_endpoints.erase(std::unique(global_endpoints.begin(), global_endpoints.end()), global_endpoints.end());

    size_t num_eps = global_endpoints.size();

    // Map global hour to local hour per thread
    std::vector<std::vector<int>> global_to_local_hour(num_threads, std::vector<int>(num_hours, -1));

    for (size_t t = 0; t < num_threads; ++t) {
        auto& td = parse_data[t];
        for (size_t lh = 0; lh < td.hours.size(); ++lh) {
            auto it = std::lower_bound(global_hours.begin(), global_hours.end(), td.hours[lh]);
            if (it != global_hours.end() && *it == td.hours[lh]) {
                int gh = (int)(it - global_hours.begin());
                global_to_local_hour[t][gh] = (int)lh;
            }
        }

        td.remap_eid.resize(td.endpoints.size());
        for (size_t le = 0; le < td.endpoints.size(); ++le) {
            auto it = std::lower_bound(global_endpoints.begin(), global_endpoints.end(), td.endpoints[le]);
            td.remap_eid[le] = (int)(it - global_endpoints.begin());
        }
    }

    // Phase 2: Parallel Reduction by Hour
    std::vector<std::string> thread_outputs(num_threads);
    threads.clear();

    size_t hours_per_thread = (num_hours + num_threads - 1) / num_threads;

    for (size_t t = 0; t < num_threads; ++t) {
        size_t h_start = t * hours_per_thread;
        size_t h_end = std::min(num_hours, h_start + hours_per_thread);

        threads.emplace_back([&, t, h_start, h_end]() {
            if (h_start >= h_end) return;
            std::string& out_buf = thread_outputs[t];
            out_buf.reserve((h_end - h_start) * 2000 * 96);

            struct EpData {
                std::vector<uint32_t> latencies;
                uint32_t errors = 0;
            };

            std::vector<EpData> ep_data(num_eps);
            std::vector<uint32_t> active_eps;
            active_eps.reserve(2048);

            char line_buf[2048];

            for (size_t h = h_start; h < h_end; ++h) {
                active_eps.clear();

                for (size_t pt = 0; pt < num_threads; ++pt) {
                    int local_hid = global_to_local_hour[pt][h];
                    if (local_hid < 0) continue;

                    const auto& recs = parse_data[pt].hour_records[local_hid];
                    const auto& remap_eid = parse_data[pt].remap_eid;

                    for (const auto& rec : recs) {
                        uint32_t gid = remap_eid[rec.eid];
                        auto& ed = ep_data[gid];
                        if (ed.latencies.empty()) {
                            active_eps.push_back(gid);
                            if (ed.latencies.capacity() == 0) ed.latencies.reserve(64);
                        }
                        ed.latencies.push_back(rec.lat);
                        if (rec.is_err) ed.errors++;
                    }
                    parse_data[pt].hour_records[local_hid].clear();
                    // shrink_to_fit removed
                }

                if (active_eps.empty()) continue;

                std::sort(active_eps.begin(), active_eps.end());

                const std::string& hr_str = global_hours[h];

                for (uint32_t gid : active_eps) {
                    auto& ed = ep_data[gid];
                    auto& lat = ed.latencies;
                    size_t n = lat.size();
                    if (n == 0) continue;

                    size_t idx50 = (50ULL * n + 99) / 100 - 1;
                    size_t idx95 = (95ULL * n + 99) / 100 - 1;
                    size_t idx99 = (99ULL * n + 99) / 100 - 1;

                    uint32_t p50, p95, p99;
                    if (n <= 64) {
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

                    uint64_t ppm = (uint64_t)ed.errors * 1000000ULL / n;
                    const std::string& ep_str = global_endpoints[gid];

                    char* p = line_buf;
                    std::memcpy(p, hr_str.data(), 13);
                    p += 13;
                    std::memcpy(p, ":00:00Z ", 8);
                    p += 8;
                    std::memcpy(p, ep_str.data(), ep_str.size());
                    p += ep_str.size();
                    *p++ = ' ';
                    p = fast_u64(p, n);
                    *p++ = ' ';
                    p = fast_u32(p, p50);
                    *p++ = ' ';
                    p = fast_u32(p, p95);
                    *p++ = ' ';
                    p = fast_u32(p, p99);
                    *p++ = ' ';
                    p = fast_u32(p, ed.errors);
                    *p++ = ' ';
                    p = fast_u64(p, ppm);
                    *p++ = '\n';

                    out_buf.append(line_buf, p - line_buf);

                    // Clear for next hour
                    ed.latencies.clear();
                    ed.errors = 0;
                }
            }
        });
    }

    for (auto& th : threads) {
        th.join();
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
