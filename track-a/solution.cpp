#include <iostream>
#include <fstream>
#include <vector>
#include <string>
#include <string_view>
#include <map>
#include <unordered_map>
#include <thread>
#include <algorithm>
#include <cstring>
#include <cstdint>

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

struct ThreadGroup {
    std::vector<uint32_t> latencies;
    uint32_t errors = 0;
};

struct KeyHash {
    size_t operator()(const std::pair<std::string, std::string>& k) const {
        size_t h1 = std::hash<std::string>{}(k.first);
        size_t h2 = std::hash<std::string>{}(k.second);
        return h1 ^ (h2 + 0x9e3779b97f4a7c15ULL + (h1 << 6) + (h1 >> 2));
    }
};

using LocalMap = std::unordered_map<std::pair<std::string, std::string>, ThreadGroup, KeyHash>;

void parse_chunk(const char* buf, size_t start, size_t end, LocalMap& local_groups) {
    const char* p = buf + start;
    const char* limit = buf + end;

    while (p < limit) {
        const char* nl = (const char*)memchr(p, '\n', limit - p);
        const char* line_end = nl ? nl : limit;
        if (line_end > p && *(line_end - 1) == '\r') {
            line_end--;
        }

        if (line_end - p >= 35) { // minimum length of a valid log line
            // hour is p[0..12]
            std::string hour(p, 13);

            // Find spaces
            // Timestamp is around 27 chars, method follows, then space before endpoint
            const char* sp1 = (const char*)memchr(p + 20, ' ', line_end - (p + 20));
            if (sp1 && sp1 < line_end) {
                const char* sp2 = (const char*)memchr(sp1 + 1, ' ', line_end - (sp1 + 1));
                if (sp2 && sp2 < line_end) {
                    const char* sp3 = (const char*)memchr(sp2 + 1, ' ', line_end - (sp2 + 1));
                    if (sp3 && sp3 < line_end) {
                        const char* sp4 = (const char*)memchr(sp3 + 1, ' ', line_end - (sp3 + 1));
                        if (sp4 && sp4 < line_end) {
                            std::string endpoint(sp2 + 1, sp3 - (sp2 + 1));
                            bool is_err = (*(sp3 + 1) == '5');

                            uint32_t lat = 0;
                            const char* lp = sp4 + 1;
                            while (lp < line_end && *lp >= '0' && *lp <= '9') {
                                lat = lat * 10 + (*lp - '0');
                                lp++;
                            }

                            auto& grp = local_groups[{hour, std::move(endpoint)}];
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
        std::ofstream ofs(out_path);
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

    std::vector<LocalMap> thread_maps(num_threads);
    std::vector<std::thread> threads;
    threads.reserve(num_threads);

    for (size_t i = 0; i < num_threads; ++i) {
        threads.emplace_back(parse_chunk, mf.data, split_points[i], split_points[i+1], std::ref(thread_maps[i]));
    }

    for (auto& t : threads) {
        t.join();
    }

    // Merge into std::map for automatic lexicographical sort by (hour, endpoint)
    struct MergedGroup {
        std::vector<uint32_t> latencies;
        uint32_t errors = 0;
    };
    std::map<std::pair<std::string, std::string>, MergedGroup> merged;

    for (auto& lmap : thread_maps) {
        for (auto& [k, grp] : lmap) {
            auto& m = merged[k];
            m.latencies.insert(m.latencies.end(), grp.latencies.begin(), grp.latencies.end());
            m.errors += grp.errors;
        }
    }

    std::string out_path = out_dir + "/summary.txt";
    std::ofstream ofs(out_path, std::ios::binary);

    for (auto& [key, grp] : merged) {
        const std::string& hour = key.first;
        const std::string& endpoint = key.second;
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

        ofs << hour << ":00:00Z " << endpoint << " " << n << " "
            << p50 << " " << p95 << " " << p99 << " "
            << grp.errors << " " << ppm << "\n";
    }

    return 0;
}
