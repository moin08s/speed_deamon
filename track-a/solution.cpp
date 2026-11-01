#include <iostream>
#include <fstream>
#include <vector>
#include <string>
#include <string_view>
#include <thread>
#include <barrier>
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

struct RawFile {
#ifdef _WIN32
    HANDLE hFile = INVALID_HANDLE_VALUE;
#else
    int fd = -1;
#endif
    size_t size = 0;

    bool open(const char* path) {
#ifdef _WIN32
        hFile = CreateFileA(path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
        if (hFile == INVALID_HANDLE_VALUE) return false;
        LARGE_INTEGER li;
        GetFileSizeEx(hFile, &li);
        size = li.QuadPart;
        return true;
#else
        fd = ::open(path, O_RDONLY);
        if (fd < 0) return false;
        struct stat st;
        if (fstat(fd, &st) < 0) { ::close(fd); fd = -1; return false; }
        size = st.st_size;
        return true;
#endif
    }

    inline ssize_t pread_bytes(void* buf, size_t count, uint64_t offset) const {
#ifdef _WIN32
        OVERLAPPED ov;
        std::memset(&ov, 0, sizeof(ov));
        ov.Offset = (DWORD)(offset & 0xFFFFFFFF);
        ov.OffsetHigh = (DWORD)(offset >> 32);
        DWORD bytesRead = 0;
        if (!ReadFile(hFile, buf, (DWORD)count, &bytesRead, &ov)) return -1;
        return (ssize_t)bytesRead;
#else
        return ::pread(fd, buf, count, (off_t)offset);
#endif
    }

    void close() {
#ifdef _WIN32
        if (hFile != INVALID_HANDLE_VALUE) { CloseHandle(hFile); hFile = INVALID_HANDLE_VALUE; }
#else
        if (fd >= 0) { ::close(fd); fd = -1; }
#endif
        size = 0;
    }

    ~RawFile() { close(); }
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
    static constexpr size_t CAP = 8192;
    static constexpr size_t MASK = CAP - 1;
    struct Entry {
        uint64_t hash = 0;
        uint32_t len = 0;
        const char* str = nullptr;
        int id = -1;
    } entries[CAP];
    int count = 0;

    FastStringMap() {
        std::memset(entries, 0, sizeof(entries));
    }

    int get_or_insert_with_hash(const char* s, size_t len, uint64_t h) {
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
    std::vector<char> output_buf;
    size_t output_len = 0;
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
    RawFile rf;
    if (!rf.open(input_path.c_str())) return 1;

    if (rf.size == 0) {
        std::string out_path = out_dir + "/summary.txt";
        std::ofstream ofs(out_path, std::ios::binary);
        return 0;
    }

    const size_t num_threads = 12;
    std::vector<size_t> split_points(num_threads + 1, 0);
    split_points[0] = 0;
    split_points[num_threads] = rf.size;

    for (size_t i = 1; i < num_threads; ++i) {
        size_t target = i * (rf.size / num_threads);
        char probe[4096];
        size_t to_probe = std::min((size_t)4096, rf.size - target);
        ssize_t n = rf.pread_bytes(probe, to_probe, target);
        if (n > 0) {
            const char* nl = (const char*)std::memchr(probe, '\n', n);
            if (nl) split_points[i] = target + (nl - probe) + 1;
            else split_points[i] = rf.size;
        } else {
            split_points[i] = rf.size;
        }
    }

    std::barrier sync_point(num_threads);
    std::vector<ThreadParseData> parse_data(num_threads);
    std::vector<std::string> global_endpoints;
    size_t num_eps = 0;

    std::vector<std::thread> threads;
    threads.reserve(num_threads);

    for (size_t t = 0; t < num_threads; ++t) {
        threads.emplace_back([&, t]() {
            auto& td = parse_data[t];
            for (size_t h = 0; h < 72; ++h) {
                td.hour_records[h].reserve(512);
            }

            struct EpCacheSlot {
                uint64_t hash = 0;
                uint32_t len = 0;
                int id = -1;
                char str[128];
            } ep_cache[4096];
            std::memset(ep_cache, 0, sizeof(ep_cache));

            size_t start = split_points[t];
            size_t end = split_points[t + 1];

            static constexpr size_t BUF_CAP = 524288; // 512 KB streaming buffer
            std::vector<char> io_buf(BUF_CAP + 65536);
            size_t cur_file_pos = start;
            size_t leftover = 0;

            uint32_t cur_hid = 0;
            char last_h0 = 0, last_h1 = 0, last_d0 = 0, last_m0 = 0;
            auto* cur_hour_vec = &td.hour_records[0];

            while (cur_file_pos < end || leftover > 0) {
                if (cur_file_pos < end) {
                    size_t read_bytes = std::min(BUF_CAP, end - cur_file_pos);
                    ssize_t n = rf.pread_bytes(io_buf.data() + leftover, read_bytes, cur_file_pos);
                    if (n <= 0 && leftover == 0) break;
                    if (n > 0) {
                        cur_file_pos += n;
                        leftover += n;
                    }
                }

                const char* buf_start = io_buf.data();
                const char* buf_limit = io_buf.data() + leftover;
                const char* p = buf_start;

                const char* parse_limit = buf_limit;
                if (cur_file_pos < end) {
                    const char* last_nl = nullptr;
                    for (const char* q = buf_limit - 1; q >= p; --q) {
                        if (*q == '\n') { last_nl = q; break; }
                    }
                    if (last_nl) parse_limit = last_nl + 1;
                    else break;
                }

                while (p < parse_limit) {
                    if (*p == '\r' || *p == '\n') { p++; continue; }
                    if (parse_limit - p < 45) break;

                    // Chronological hour cache
                    if (__builtin_expect(p[11] != last_h0 || p[12] != last_h1 || p[9] != last_d0 || p[6] != last_m0, 0)) {
                        cur_hid = get_hour_idx(p);
                        last_h0 = p[11]; last_h1 = p[12]; last_d0 = p[9]; last_m0 = p[6];
                        cur_hour_vec = &td.hour_records[cur_hid];
                    }

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
                            while (s < parse_limit && *s != ' ') s++;
                            ep_start = s + 1;
                        }
                    } else if (m0 == 'D') ep_start = p + 35;   // DELETE
                    else if (m0 == 'H') ep_start = p + 33;     // HEAD
                    else if (m0 == 'O') ep_start = p + 36;     // OPTIONS
                    else {
                        const char* s = p + 28;
                        while (s < parse_limit && *s != ' ') s++;
                        ep_start = s + 1;
                    }

                    if (ep_start >= parse_limit) break;

                    // Vectorized SIMD space search: 1-cycle AVX2 match for paths <= 32 chars
                    const char* ep_end = nullptr;
                    if (__builtin_expect(parse_limit - ep_start >= 32, 1)) {
                        __m256i chunk = _mm256_loadu_si256((const __m256i*)ep_start);
                        uint32_t mask = (uint32_t)_mm256_movemask_epi8(_mm256_cmpeq_epi8(chunk, _mm256_set1_epi8(' ')));
                        if (__builtin_expect(mask != 0, 1)) {
                            ep_end = ep_start + __builtin_ctz(mask);
                        } else {
                            ep_end = (const char*)std::memchr(ep_start + 32, ' ', parse_limit - (ep_start + 32));
                        }
                    } else {
                        ep_end = (const char*)std::memchr(ep_start, ' ', parse_limit - ep_start);
                    }
                    if (!ep_end) break;

                    size_t ep_len = ep_end - ep_start;
                    uint64_t h = 0xcbf29ce484222325ULL;
#if defined(__x86_64__) || defined(_M_X64)
                    size_t rem = ep_len;
                    const char* ptr = ep_start;
                    while (rem >= 8) {
                        uint64_t v; std::memcpy(&v, ptr, 8);
                        h = _mm_crc32_u64(h, v);
                        ptr += 8; rem -= 8;
                    }
                    while (rem > 0) {
                        h = _mm_crc32_u8((uint32_t)h, (uint8_t)*ptr++);
                        rem--;
                    }
#else
                    for (size_t i = 0; i < ep_len; ++i) {
                        h = (h ^ (unsigned char)ep_start[i]) * 0x100000001b3ULL;
                    }
#endif
                    size_t c_idx = h & 4095;
                    int eid = -1;
                    if (ep_cache[c_idx].hash == h && ep_cache[c_idx].len == ep_len && std::memcmp(ep_cache[c_idx].str, ep_start, ep_len) == 0) {
                        eid = ep_cache[c_idx].id;
                    } else {
                        eid = td.ep_map.get_or_insert_with_hash(ep_start, ep_len, h);
                        if (eid >= (int)td.endpoints.size()) {
                            td.endpoints.emplace_back(ep_start, ep_len);
                        }
                        ep_cache[c_idx].hash = h;
                        ep_cache[c_idx].len = (uint32_t)ep_len;
                        ep_cache[c_idx].id = eid;
                        if (ep_len < sizeof(ep_cache[c_idx].str)) {
                            std::memcpy(ep_cache[c_idx].str, ep_start, ep_len);
                        }
                    }

                    // Status
                    const char* status_ptr = ep_end + 1;
                    uint16_t is_err_flag = (status_ptr[0] == '5') ? 0x8000 : 0;

                    // Latency
                    const char* lat_ptr = status_ptr + 4;
                    uint32_t lat = 0;
                    while (lat_ptr < parse_limit && *lat_ptr >= '0' && *lat_ptr <= '9') {
                        lat = lat * 10 + (*lat_ptr - '0');
                        lat_ptr++;
                    }

                    cur_hour_vec->push_back({lat, (uint16_t)((uint16_t)eid | is_err_flag), 0});

                    if (lat_ptr < parse_limit && *lat_ptr == '\r') lat_ptr++;
                    if (lat_ptr < parse_limit && *lat_ptr == '\n') lat_ptr++;
                    p = lat_ptr;
                }

                size_t consumed = p - buf_start;
                leftover -= consumed;
                if (leftover > 0 && consumed > 0) {
                    std::memmove(io_buf.data(), buf_start + consumed, leftover);
                }
            }

            // Barrier 1: Wait for all threads to finish Phase 1 parse
            sync_point.arrive_and_wait();

            // Thread 0 consolidates global endpoints and precomputes remap tables
            if (t == 0) {
                rf.close(); // Close raw file handle immediately
                for (size_t i = 0; i < num_threads; ++i) {
                    for (const auto& ep : parse_data[i].endpoints) {
                        global_endpoints.push_back(ep);
                    }
                }
                std::sort(global_endpoints.begin(), global_endpoints.end());
                global_endpoints.erase(std::unique(global_endpoints.begin(), global_endpoints.end()), global_endpoints.end());

                num_eps = global_endpoints.size();

                for (size_t i = 0; i < num_threads; ++i) {
                    auto& ptd = parse_data[i];
                    ptd.remap_eid.resize(ptd.endpoints.size());
                    for (size_t le = 0; le < ptd.endpoints.size(); ++le) {
                        auto it = std::lower_bound(global_endpoints.begin(), global_endpoints.end(), ptd.endpoints[le]);
                        ptd.remap_eid[le] = (int)(it - global_endpoints.begin());
                    }
                }
            }

            // Barrier 2: Wait for Thread 0 to finish endpoint consolidation
            sync_point.arrive_and_wait();

            // Phase 2: Ultra-Fast Parallel Counting Reduction by Hour with Direct Buffer Writing
            size_t hours_per_thread = (72 + num_threads - 1) / num_threads;
            size_t h_start = t * hours_per_thread;
            size_t h_end = std::min((size_t)72, h_start + hours_per_thread);

            if (h_start < h_end) {
                // Pre-allocate direct output buffer: 800 KB per thread
                td.output_buf.resize(800000);
                char* out_ptr = td.output_buf.data();

                std::vector<uint32_t> ep_counts(num_eps);
                std::vector<uint32_t> ep_errors(num_eps);
                std::vector<uint32_t> ep_offsets(num_eps + 1);
                std::vector<uint32_t> cur_pos(num_eps + 1);
                std::vector<uint32_t> lat_pool;
                lat_pool.reserve(65536);

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

                        std::memcpy(out_ptr, hr_str, 20);
                        out_ptr += 20;
                        *out_ptr++ = ' ';
                        std::memcpy(out_ptr, ep_str.data(), ep_str.size());
                        out_ptr += ep_str.size();
                        *out_ptr++ = ' ';
                        out_ptr = fast_u64(out_ptr, n);
                        *out_ptr++ = ' ';
                        out_ptr = fast_u32(out_ptr, p50);
                        *out_ptr++ = ' ';
                        out_ptr = fast_u32(out_ptr, p95);
                        *out_ptr++ = ' ';
                        out_ptr = fast_u32(out_ptr, p99);
                        *out_ptr++ = ' ';
                        out_ptr = fast_u32(out_ptr, err);
                        *out_ptr++ = ' ';
                        out_ptr = fast_u64(out_ptr, ppm);
                        *out_ptr++ = '\n';
                    }
                }

                td.output_len = out_ptr - td.output_buf.data();
            }
        });
    }

    for (auto& t : threads) {
        t.join();
    }

    std::string out_path = out_dir + "/summary.txt";
    FILE* fp = fopen(out_path.c_str(), "wb");
    if (!fp) return 1;

    char write_buf[1048576];
    setvbuf(fp, write_buf, _IOFBF, sizeof(write_buf));

    for (size_t t = 0; t < num_threads; ++t) {
        if (parse_data[t].output_len > 0) {
            fwrite(parse_data[t].output_buf.data(), 1, parse_data[t].output_len, fp);
        }
    }
    fclose(fp);

    return 0;
}
