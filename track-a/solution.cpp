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

    ~MappedFile() {
        close();
    }
};

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

struct ThreadGroup {
    char hour[13];
    std::string endpoint;
    uint32_t errors = 0;
    std::vector<uint32_t> latencies;
};

struct FastGroupMap {
    static constexpr uint32_t CAPACITY = 16384;
    static constexpr uint32_t MASK = CAPACITY - 1;

    struct Entry {
        uint64_t hash = 0;
        uint32_t ep_len = 0;
        const char* ep_ptr = nullptr;
        char hour[13];
        int id = -1;
    };

    std::vector<Entry> table;
    int count = 0;

    FastGroupMap() : table(CAPACITY) {
        std::memset(table.data(), 0, CAPACITY * sizeof(Entry));
    }

    static inline uint64_t hash_bytes(const char* p, size_t len) {
        uint64_t h = 0xcbf29ce484222325ULL;
        for (size_t i = 0; i < len; ++i) {
            h = (h ^ (unsigned char)p[i]) * 0x100000001b3ULL;
        }
        return h ? h : 1;
    }

    inline int get_or_insert(const char* hr, const char* ep, size_t ep_len, uint64_t ep_hash, std::vector<ThreadGroup>& groups) {
        uint64_t hr_hash = *(const uint64_t*)hr ^ (*(const uint64_t*)(hr + 5));
        uint64_t combined_hash = hr_hash ^ ep_hash;
        uint32_t idx = (uint32_t)combined_hash & MASK;

        while (table[idx].hash != 0) {
            if (table[idx].hash == combined_hash && table[idx].ep_len == ep_len &&
                std::memcmp(table[idx].hour, hr, 13) == 0 &&
                std::memcmp(table[idx].ep_ptr, ep, ep_len) == 0) {
                return table[idx].id;
            }
            idx = (idx + 1) & MASK;
        }

        table[idx].hash = combined_hash;
        table[idx].ep_len = (uint32_t)ep_len;
        table[idx].ep_ptr = ep;
        std::memcpy(table[idx].hour, hr, 13);
        int new_id = count++;
        table[idx].id = new_id;

        groups.emplace_back();
        auto& g = groups.back();
        std::memcpy(g.hour, hr, 13);
        g.endpoint.assign(ep, ep_len);
        g.latencies.reserve(16);

        return new_id;
    }
};

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

    std::vector<std::vector<ThreadGroup>> thread_groups(num_threads);
    std::vector<std::thread> threads;
    threads.reserve(num_threads);

    for (size_t t = 0; t < num_threads; ++t) {
        threads.emplace_back([&, t]() {
            size_t start = split_points[t];
            size_t end = split_points[t + 1];
            if (start >= end) return;

            auto& groups = thread_groups[t];
            groups.reserve(8192);
            FastGroupMap group_map;

            const char* p = mf.data + start;
            const char* limit = mf.data + end;

            while (p < limit) {
                if (*p == '\r' || *p == '\n') { p++; continue; }
                if (limit - p < 35) break;

                const char* hour_ptr = p;

                const char* m_scan = p + 28;
                while (m_scan < limit && *m_scan != ' ') m_scan++;
                if (m_scan >= limit) break;

                const char* ep_start = m_scan + 1;
                const char* ep_end = ep_start;
                uint64_t ep_hash = 0xcbf29ce484222325ULL;
                while (ep_end < limit && *ep_end != ' ') {
                    ep_hash = (ep_hash ^ (unsigned char)*ep_end) * 0x100000001b3ULL;
                    ep_end++;
                }
                if (ep_end >= limit) break;
                size_t ep_len = ep_end - ep_start;

                const char* status_ptr = ep_end + 1;
                uint8_t is_err = (status_ptr < limit && *status_ptr == '5') ? 1 : 0;

                const char* lat_ptr = status_ptr + 4;
                uint32_t lat = 0;
                while (lat_ptr < limit && *lat_ptr >= '0' && *lat_ptr <= '9') {
                    lat = lat * 10 + (*lat_ptr++ - '0');
                }

                int gid = group_map.get_or_insert(hour_ptr, ep_start, ep_len, ep_hash, groups);
                auto& grp = groups[gid];
                grp.latencies.push_back(lat);
                if (is_err) grp.errors++;

                while (lat_ptr < limit && *lat_ptr != '\n') lat_ptr++;
                p = (lat_ptr < limit) ? lat_ptr + 1 : limit;
            }
        });
    }

    for (auto& th : threads) th.join();
    mf.close(); // Immediately unmap requests.log to drop page cache

    // Collect all groups across threads
    size_t total_groups_count = 0;
    for (size_t t = 0; t < num_threads; ++t) {
        total_groups_count += thread_groups[t].size();
    }

    std::vector<ThreadGroup> all_groups;
    all_groups.reserve(total_groups_count);
    for (size_t t = 0; t < num_threads; ++t) {
        for (auto& g : thread_groups[t]) {
            all_groups.push_back(std::move(g));
        }
        thread_groups[t].clear();
        thread_groups[t].shrink_to_fit();
    }

    std::sort(all_groups.begin(), all_groups.end(), [](const ThreadGroup& a, const ThreadGroup& b) {
        int c = std::memcmp(a.hour, b.hour, 13);
        if (c != 0) return c < 0;
        return a.endpoint < b.endpoint;
    });

    // Merge duplicate (hour, endpoint) groups across thread boundaries
    std::vector<ThreadGroup> merged_groups;
    merged_groups.reserve(all_groups.size());

    for (size_t i = 0; i < all_groups.size(); ++i) {
        if (!merged_groups.empty() &&
            std::memcmp(merged_groups.back().hour, all_groups[i].hour, 13) == 0 &&
            merged_groups.back().endpoint == all_groups[i].endpoint) {
            auto& prev = merged_groups.back();
            prev.errors += all_groups[i].errors;
            prev.latencies.insert(prev.latencies.end(), all_groups[i].latencies.begin(), all_groups[i].latencies.end());
        } else {
            merged_groups.push_back(std::move(all_groups[i]));
        }
    }
    all_groups.clear();
    all_groups.shrink_to_fit();

    // Parallel formatting of results
    size_t num_final = merged_groups.size();
    std::vector<std::string> thread_outputs(num_threads);
    threads.clear();
    size_t chunk_groups = (num_final + num_threads - 1) / num_threads;

    for (size_t t = 0; t < num_threads; ++t) {
        size_t start = t * chunk_groups;
        size_t end = std::min(num_final, start + chunk_groups);
        threads.emplace_back([&, t, start, end]() {
            if (start >= end) return;
            std::string& out = thread_outputs[t];
            out.reserve((end - start) * 96);
            char line_buf[2048];

            for (size_t i = start; i < end; ++i) {
                auto& grp = merged_groups[i];
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
                std::memcpy(p, grp.hour, 13);
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

                out.append(line_buf, p - line_buf);
            }
        });
    }

    for (auto& th : threads) th.join();

    std::string out_path = out_dir + "/summary.txt";
    FILE* fp = fopen(out_path.c_str(), "wb");
    if (!fp) return 1;

    char file_buf[2097152];
    setvbuf(fp, file_buf, _IOFBF, sizeof(file_buf));

    for (size_t t = 0; t < num_threads; ++t) {
        if (!thread_outputs[t].empty()) {
            fwrite(thread_outputs[t].data(), 1, thread_outputs[t].size(), fp);
        }
    }
    fclose(fp);

    return 0;
}
