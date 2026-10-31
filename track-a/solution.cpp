#include <iostream>
#include <fstream>
#include <vector>
#include <string>
#include <string_view>
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

struct LocalGroup {
    std::string hour;
    std::string endpoint;
    std::vector<uint32_t> latencies;
    uint32_t errors = 0;
};

// Fast open-addressing hash table for string_view -> int
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

struct ThreadData {
    FastStringMap hour_map;
    FastStringMap ep_map;
    std::vector<std::string> hours;
    std::vector<std::string> endpoints;
    std::vector<LocalGroup> groups;
};

void parse_chunk_fast(const char* buf, size_t start, size_t end, ThreadData& td) {
    const char* p = buf + start;
    const char* limit = buf + end;

    constexpr int MAX_EP = 4096;
    td.groups.resize(128 * MAX_EP);

    while (p < limit) {
        const char* nl = (const char*)memchr(p, '\n', limit - p);
        const char* line_end = nl ? nl : limit;
        if (line_end > p && *(line_end - 1) == '\r') {
            line_end--;
        }

        if (line_end - p >= 35) {
            // hour is p[0..12]
            const char* hour_ptr = p;
            size_t hour_len = 13;

            // Find spaces
            const char* sp1 = (const char*)memchr(p + 20, ' ', line_end - (p + 20));
            if (sp1) {
                const char* sp2 = (const char*)memchr(sp1 + 1, ' ', line_end - (sp1 + 1));
                if (sp2) {
                    const char* sp3 = (const char*)memchr(sp2 + 1, ' ', line_end - (sp2 + 1));
                    if (sp3) {
                        const char* sp4 = (const char*)memchr(sp3 + 1, ' ', line_end - (sp3 + 1));
                        if (sp4) {
                            const char* ep_ptr = sp2 + 1;
                            size_t ep_len = sp3 - ep_ptr;

                            int hid = td.hour_map.get_or_insert(hour_ptr, hour_len);
                            if (hid >= (int)td.hours.size()) {
                                td.hours.emplace_back(hour_ptr, hour_len);
                            }

                            int eid = td.ep_map.get_or_insert(ep_ptr, ep_len);
                            if (eid >= (int)td.endpoints.size()) {
                                td.endpoints.emplace_back(ep_ptr, ep_len);
                            }

                            bool is_err = (*(sp3 + 1) == '5');

                            uint32_t lat = 0;
                            const char* lp = sp4 + 1;
                            while (lp < line_end && *lp >= '0' && *lp <= '9') {
                                lat = lat * 10 + (*lp - '0');
                                lp++;
                            }

                            size_t g_idx = (size_t)hid * MAX_EP + eid;
                            auto& grp = td.groups[g_idx];
                            if (grp.latencies.empty()) {
                                grp.latencies.reserve(256);
                            }
                            grp.latencies.push_back(lat);
                            if (is_err) grp.errors++;
                        }
                    }
                }
            }
        }
        p = nl ? nl + 1 : limit;
    }
}

int main(int argc, char** argv) {
    std::ios_base::sync_with_stdio(false);
    std::cin.tie(NULL);

    std::string data_dir = (argc > 1) ? argv[1] : "/data";
    std::string out_dir = (argc > 2) ? argv[2] : "/out";

    std::string input_path = data_dir + "/requests.log";
    MappedFile mf;
    if (!mf.open(input_path.c_str())) {
        std::cerr << "Failed to open " << input_path << "\n";
        return 1;
    }

    if (mf.size == 0) {
        std::string out_path = out_dir + "/summary.txt";
        std::ofstream ofs(out_path, std::ios::binary);
        return 0;
    }

    const size_t num_threads = 6;
    std::vector<size_t> split_points;
    split_points.push_back(0);

    for (size_t i = 1; i < num_threads; ++i) {
        size_t target = i * (mf.size / num_threads);
        const char* nl = (const char*)memchr(mf.data + target, '\n', mf.size - target);
        if (nl) {
            split_points.push_back((nl - mf.data) + 1);
        } else {
            split_points.push_back(mf.size);
        }
    }
    split_points.push_back(mf.size);

    std::vector<ThreadData> thread_data(num_threads);
    std::vector<std::thread> threads;
    threads.reserve(num_threads);

    for (size_t i = 0; i < num_threads; ++i) {
        threads.emplace_back(parse_chunk_fast, mf.data, split_points[i], split_points[i+1], std::ref(thread_data[i]));
    }

    for (auto& t : threads) {
        t.join();
    }

    struct OutGroup {
        std::string hour;
        std::string endpoint;
        std::vector<uint32_t> latencies;
        uint32_t errors = 0;
    };

    std::vector<OutGroup> out_groups;
    out_groups.reserve(70000);

    constexpr int MAX_EP = 4096;
    // Fast hash table to merge thread results without std::map
    // Key: uint64_t hash of (hour + endpoint)
    struct MergeEntry {
        uint64_t hash = 0;
        int group_idx = -1;
    };
    constexpr uint32_t MERGE_CAP = 262144;
    constexpr uint32_t MERGE_MASK = MERGE_CAP - 1;
    std::vector<MergeEntry> merge_table(MERGE_CAP);

    for (size_t t = 0; t < num_threads; ++t) {
        auto& td = thread_data[t];
        for (int h = 0; h < (int)td.hours.size(); ++h) {
            for (int e = 0; e < (int)td.endpoints.size(); ++e) {
                size_t g_idx = (size_t)h * MAX_EP + e;
                auto& grp = td.groups[g_idx];
                if (!grp.latencies.empty()) {
                    const std::string& hr = td.hours[h];
                    const std::string& ep = td.endpoints[e];
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
                        out_groups.push_back({hr, ep, std::move(grp.latencies), grp.errors});
                        merge_table[s_idx].hash = hsh;
                        merge_table[s_idx].group_idx = found_idx;
                    } else {
                        auto& target = out_groups[found_idx];
                        target.latencies.insert(target.latencies.end(), grp.latencies.begin(), grp.latencies.end());
                        target.errors += grp.errors;
                    }
                }
            }
        }
    }

    std::sort(out_groups.begin(), out_groups.end(), [](const OutGroup& a, const OutGroup& b) {
        if (a.hour != b.hour) return a.hour < b.hour;
        return a.endpoint < b.endpoint;
    });

    std::string out_path = out_dir + "/summary.txt";
    FILE* fp = fopen(out_path.c_str(), "wb");
    if (!fp) return 1;

    char out_buf[1048576];
    setvbuf(fp, out_buf, _IOFBF, sizeof(out_buf));

    char line_buf[2048];
    for (auto& grp : out_groups) {
        auto& lat = grp.latencies;
        size_t n = lat.size();
        if (n == 0) continue;

        size_t idx50 = (50ULL * n + 99) / 100 - 1;
        size_t idx95 = (95ULL * n + 99) / 100 - 1;
        size_t idx99 = (99ULL * n + 99) / 100 - 1;

        std::nth_element(lat.begin(), lat.begin() + idx50, lat.end());
        uint32_t p50 = lat[idx50];

        std::nth_element(lat.begin() + idx50 + 1, lat.begin() + idx95, lat.end());
        uint32_t p95 = lat[idx95];

        std::nth_element(lat.begin() + idx95 + 1, lat.begin() + idx99, lat.end());
        uint32_t p99 = lat[idx99];

        uint64_t ppm = (uint64_t)grp.errors * 1000000ULL / n;

        int len = snprintf(line_buf, sizeof(line_buf), "%s:00:00Z %s %zu %u %u %u %u %llu\n",
                           grp.hour.c_str(), grp.endpoint.c_str(), n,
                           p50, p95, p99, grp.errors, (unsigned long long)ppm);
        fwrite(line_buf, 1, len, fp);
    }
    fclose(fp);

    return 0;
}
