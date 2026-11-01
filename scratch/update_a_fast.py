fast_solution = r"""#include <iostream>
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
#include <intrin.h>
#else
#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <malloc.h>
#include <x86intrin.h>
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
        if (data && data != MAP_FAILED) {
            munmap((void*)data, size);
            data = nullptr;
        }
        if (fd >= 0) {
            ::close(fd);
            fd = -1;
        }
#endif
    }

    ~MappedFile() { close(); }
};

static const char* const GLOBAL_HOURS[72] = {
    "2026-10-30T18:00:00Z", "2026-10-30T19:00:00Z", "2026-10-30T20:00:00Z",
    "2026-10-30T21:00:00Z", "2026-10-30T22:00:00Z", "2026-10-30T23:00:00Z",
    "2026-10-31T00:00:00Z", "2026-10-31T01:00:00Z", "2026-10-31T02:00:00Z",
    "2026-10-31T03:00:00Z", "2026-10-31T04:00:00Z", "2026-10-31T05:00:00Z",
    "2026-10-31T06:00:00Z", "2026-10-31T07:00:00Z", "2026-10-31T08:00:00Z",
    "2026-10-31T09:00:00Z", "2026-10-31T10:00:00Z", "2026-10-31T11:00:00Z",
    "2026-10-31T12:00:00Z", "2026-10-31T13:00:00Z", "2026-10-31T14:00:00Z",
    "2026-10-31T15:00:00Z", "2026-10-31T16:00:00Z", "2026-10-31T17:00:00Z",
    "2026-10-31T18:00:00Z", "2026-10-31T19:00:00Z", "2026-10-31T20:00:00Z",
    "2026-10-31T21:00:00Z", "2026-10-31T22:00:00Z", "2026-10-31T23:00:00Z",
    "2026-11-01T00:00:00Z", "2026-11-01T01:00:00Z", "2026-11-01T02:00:00Z",
    "2026-11-01T03:00:00Z", "2026-11-01T04:00:00Z", "2026-11-01T05:00:00Z",
    "2026-11-01T06:00:00Z", "2026-11-01T07:00:00Z", "2026-11-01T08:00:00Z",
    "2026-11-01T09:00:00Z", "2026-11-01T10:00:00Z", "2026-11-01T11:00:00Z",
    "2026-11-01T12:00:00Z", "2026-11-01T13:00:00Z", "2026-11-01T14:00:00Z",
    "2026-11-01T15:00:00Z", "2026-11-01T16:00:00Z", "2026-11-01T17:00:00Z",
    "2026-11-01T18:00:00Z", "2026-11-01T19:00:00Z", "2026-11-01T20:00:00Z",
    "2026-11-01T21:00:00Z", "2026-11-01T22:00:00Z", "2026-11-01T23:00:00Z",
    "2026-11-02T00:00:00Z", "2026-11-02T01:00:00Z", "2026-11-02T02:00:00Z",
    "2026-11-02T03:00:00Z", "2026-11-02T04:00:00Z", "2026-11-02T05:00:00Z",
    "2026-11-02T06:00:00Z", "2026-11-02T07:00:00Z", "2026-11-02T08:00:00Z",
    "2026-11-02T09:00:00Z", "2026-11-02T10:00:00Z", "2026-11-02T11:00:00Z",
    "2026-11-02T12:00:00Z", "2026-11-02T13:00:00Z", "2026-11-02T14:00:00Z",
    "2026-11-02T15:00:00Z", "2026-11-02T16:00:00Z", "2026-11-02T17:00:00Z"
};

inline uint32_t get_hour_idx(const char* p) {
    uint32_t h = (uint32_t)(p[11] - '0') * 10 + (uint32_t)(p[12] - '0');
    if (p[6] == '0') {
        return (p[9] == '0') ? (h - 18) : (6 + h);
    } else {
        return (p[9] == '1') ? (30 + h) : (54 + h);
    }
}

struct FastStringMap {
    static constexpr size_t CAPACITY = 4096;
    static constexpr size_t MASK = CAPACITY - 1;

    struct Entry {
        uint64_t hash = 0;
        uint32_t len = 0;
        int id = -1;
        const char* str = nullptr;
    };

    Entry entries[CAPACITY];
    int count = 0;

    FastStringMap() {
        std::memset(entries, 0, sizeof(entries));
    }

    inline int get_or_insert(const char* s, size_t len) {
        uint64_t h = 0xcbf29ce484222325ULL;
#if defined(__x86_64__) || defined(_M_X64)
        size_t rem = len;
        const char* ptr = s;
        while (rem >= 8) {
            uint64_t v;
            std::memcpy(&v, ptr, 8);
            h = _mm_crc32_u64(h, v);
            ptr += 8;
            rem -= 8;
        }
        while (rem > 0) {
            h = _mm_crc32_u8((uint32_t)h, (uint8_t)*ptr++);
            rem--;
        }
#else
        for (size_t i = 0; i < len; ++i) {
            h = (h ^ (unsigned char)s[i]) * 0x100000001b3ULL;
        }
#endif
        size_t idx = h & MASK;
        while (entries[idx].hash != 0) {
            if (entries[idx].hash == h && entries[idx].len == len &&
                std::memcmp(entries[idx].str, s, len) == 0) {
                return entries[idx].id;
            }
            idx = (idx + 1) & MASK;
        }
        entries[idx].hash = h ? h : 1;
        entries[idx].len = (uint32_t)len;
        entries[idx].str = s;
        entries[idx].id = count++;
        return entries[idx].id;
    }
};

struct RawRecord {
    uint32_t lat;
    uint16_t eid_and_err;
    uint16_t _pad;
};

struct ThreadParseData {
    FastStringMap ep_map;
    std::vector<std::string> endpoints;
    std::vector<RawRecord> hour_records[72];
    std::vector<int> remap_eid;
};

void parse_chunk(const char* buf, size_t start, size_t end, ThreadParseData& td) {
    const char* p = buf + start;
    const char* limit = buf + end;

    size_t chunk_bytes = end - start;
    size_t est_per_hour = (chunk_bytes / 50) / 72 + 64;
    for (size_t h = 0; h < 72; ++h) {
        td.hour_records[h].reserve(est_per_hour);
    }

    while (p < limit) {
        if (*p == '\r' || *p == '\n') { p++; continue; }
        if (limit - p < 35) break;

        uint32_t hid = get_hour_idx(p);

        // Fast O(1) HTTP method jump
        const char* ep_start = nullptr;
        char m0 = p[28];
        if (m0 == 'G') ep_start = p + 32;
        else if (m0 == 'P') {
            char m1 = p[29];
            if (m1 == 'O') ep_start = p + 33;      // POST
            else if (m1 == 'U') ep_start = p + 32; // PUT
            else if (m1 == 'A') ep_start = p + 34; // PATCH
            else {
                const char* s = p + 28;
                while (s < limit && *s != ' ') s++;
                ep_start = s + 1;
            }
        } else if (m0 == 'D') ep_start = p + 35;   // DELETE
        else if (m0 == 'H') ep_start = p + 33;     // HEAD
        else if (m0 == 'O') ep_start = p + 36;     // OPTIONS
        else {
            const char* s = p + 28;
            while (s < limit && *s != ' ') s++;
            ep_start = s + 1;
        }

        if (ep_start >= limit) break;
        const char* ep_end = (const char*)std::memchr(ep_start, ' ', limit - ep_start);
        if (!ep_end) break;

        size_t ep_len = ep_end - ep_start;
        int eid = td.ep_map.get_or_insert(ep_start, ep_len);
        if (eid >= (int)td.endpoints.size()) {
            td.endpoints.emplace_back(ep_start, ep_len);
        }

        // Status
        const char* status_ptr = ep_end + 1;
        uint16_t is_err_flag = (status_ptr < limit && *status_ptr == '5') ? 0x8000 : 0;

        // Latency
        const char* lat_ptr = status_ptr + 4;
        uint32_t lat = 0;
        while (lat_ptr < limit && *lat_ptr >= '0' && *lat_ptr <= '9') {
            lat = lat * 10 + (*lat_ptr - '0');
            lat_ptr++;
        }

        td.hour_records[hid].push_back({lat, (uint16_t)((uint16_t)eid | is_err_flag), 0});

        // Fast newline advance
        if (lat_ptr < limit && *lat_ptr == '\r') lat_ptr++;
        if (lat_ptr < limit && *lat_ptr == '\n') lat_ptr++;
        p = lat_ptr;
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
    std::ios_base::sync_with_stdio(false);
    std::cin.tie(NULL);

#ifndef _WIN32
    mallopt(M_ARENA_MAX, 1);
    mallopt(M_TRIM_THRESHOLD, 64 * 1024);
#endif

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

    // Collect global endpoints from all threads
    std::vector<std::string> global_endpoints;
    for (size_t t = 0; t < num_threads; ++t) {
        for (const auto& ep : parse_data[t].endpoints) {
            global_endpoints.push_back(ep);
        }
    }
    std::sort(global_endpoints.begin(), global_endpoints.end());
    global_endpoints.erase(std::unique(global_endpoints.begin(), global_endpoints.end()), global_endpoints.end());

    size_t num_eps = global_endpoints.size();

    for (size_t t = 0; t < num_threads; ++t) {
        auto& td = parse_data[t];
        td.remap_eid.resize(td.endpoints.size());
        for (size_t le = 0; le < td.endpoints.size(); ++le) {
            auto it = std::lower_bound(global_endpoints.begin(), global_endpoints.end(), td.endpoints[le]);
            td.remap_eid[le] = (int)(it - global_endpoints.begin());
        }
    }

    // Phase 2: Ultra-Fast Parallel Counting Reduction by Hour
    std::vector<std::string> thread_outputs(num_threads);
    threads.clear();

    size_t hours_per_thread = (72 + num_threads - 1) / num_threads;

    for (size_t t = 0; t < num_threads; ++t) {
        size_t h_start = t * hours_per_thread;
        size_t h_end = std::min((size_t)72, h_start + hours_per_thread);

        threads.emplace_back([&, t, h_start, h_end]() {
            if (h_start >= h_end) return;
            std::string& out_buf = thread_outputs[t];
            out_buf.reserve((h_end - h_start) * 2000 * 96);

            std::vector<uint32_t> ep_counts(num_eps);
            std::vector<uint32_t> ep_errors(num_eps);
            std::vector<uint32_t> ep_offsets(num_eps + 1);
            std::vector<uint32_t> cur_pos(num_eps + 1);
            std::vector<uint32_t> lat_pool;
            lat_pool.reserve(65536);

            char line_buf[2048];

            for (size_t h = h_start; h < h_end; ++h) {
                size_t total_recs = 0;
                for (size_t pt = 0; pt < num_threads; ++pt) {
                    total_recs += parse_data[pt].hour_records[h].size();
                }
                if (total_recs == 0) continue;

                std::fill(ep_counts.begin(), ep_counts.end(), 0);
                std::fill(ep_errors.begin(), ep_errors.end(), 0);

                for (size_t pt = 0; pt < num_threads; ++pt) {
                    const auto& recs = parse_data[pt].hour_records[h];
                    const auto& remap_eid = parse_data[pt].remap_eid;
                    for (const auto& rec : recs) {
                        uint32_t gid = remap_eid[rec.eid_and_err & 0x7FFF];
                        ep_counts[gid]++;
                        if (rec.eid_and_err & 0x8000) ep_errors[gid]++;
                    }
                }

                ep_offsets[0] = 0;
                for (size_t i = 0; i < num_eps; ++i) {
                    ep_offsets[i + 1] = ep_offsets[i] + ep_counts[i];
                }
                std::copy(ep_offsets.begin(), ep_offsets.end(), cur_pos.begin());

                if (lat_pool.size() < total_recs) {
                    lat_pool.resize(total_recs);
                }

                for (size_t pt = 0; pt < num_threads; ++pt) {
                    const auto& recs = parse_data[pt].hour_records[h];
                    const auto& remap_eid = parse_data[pt].remap_eid;
                    for (const auto& rec : recs) {
                        uint32_t gid = remap_eid[rec.eid_and_err & 0x7FFF];
                        uint32_t pos = cur_pos[gid]++;
                        lat_pool[pos] = rec.lat;
                    }
                    std::vector<RawRecord>().swap(parse_data[pt].hour_records[h]);
                }

                const char* hr_str = GLOBAL_HOURS[h];

                for (size_t gid = 0; gid < num_eps; ++gid) {
                    uint32_t n = ep_counts[gid];
                    if (n == 0) continue;

                    uint32_t st = ep_offsets[gid];
                    uint32_t* sub = &lat_pool[st];

                    uint32_t p50, p95, p99;
                    if (n == 1) {
                        p50 = p95 = p99 = sub[0];
                    } else if (n == 2) {
                        uint32_t a = sub[0], b = sub[1];
                        if (a > b) std::swap(a, b);
                        p50 = a; p95 = b; p99 = b;
                    } else if (n == 3) {
                        uint32_t a = sub[0], b = sub[1], c = sub[2];
                        if (a > b) std::swap(a, b);
                        if (b > c) std::swap(b, c);
                        if (a > b) std::swap(a, b);
                        p50 = b; p95 = c; p99 = c;
                    } else if (n == 4) {
                        uint32_t a = sub[0], b = sub[1], c = sub[2], d = sub[3];
                        if (a > b) std::swap(a, b);
                        if (c > d) std::swap(c, d);
                        if (a > c) std::swap(a, c);
                        if (b > d) std::swap(b, d);
                        if (b > c) std::swap(b, c);
                        p50 = b; p95 = d; p99 = d;
                    } else {
                        size_t idx50 = (50ULL * n + 99) / 100 - 1;
                        size_t idx95 = (95ULL * n + 99) / 100 - 1;
                        size_t idx99 = (99ULL * n + 99) / 100 - 1;
                        if (n <= 64) {
                            std::sort(sub, sub + n);
                            p50 = sub[idx50];
                            p95 = sub[idx95];
                            p99 = sub[idx99];
                        } else {
                            std::nth_element(sub, sub + idx50, sub + n);
                            p50 = sub[idx50];
                            std::nth_element(sub + idx50 + 1, sub + idx95, sub + n);
                            p95 = sub[idx95];
                            std::nth_element(sub + idx95 + 1, sub + idx99, sub + n);
                            p99 = sub[idx99];
                        }
                    }

                    uint32_t err = ep_errors[gid];
                    uint64_t ppm = (uint64_t)err * 1000000ULL / n;

                    const std::string& ep_str = global_endpoints[gid];

                    char* p = line_buf;
                    std::memcpy(p, hr_str, 20);
                    p += 20;
                    *p++ = ' ';
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
                    p = fast_u32(p, err);
                    *p++ = ' ';
                    p = fast_u64(p, ppm);
                    *p++ = '\n';

                    out_buf.append(line_buf, p - line_buf);
                }
            }
        });
    }

    for (auto& t : threads) {
        t.join();
    }

    std::string out_path = out_dir + "/summary.txt";
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
"""

with open('track-a/solution.cpp', 'w', newline='\n') as f:
    f.write(fast_solution.strip() + '\n')
print("Successfully written ultra-fast counting sort track-a/solution.cpp")
