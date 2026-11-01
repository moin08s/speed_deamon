#include <iostream>
#include <vector>
#include <string>
#include <string_view>
#include <thread>
#include <algorithm>
#include <cstring>
#include <cstdint>
#include <cstdio>
#include <atomic>
#include <mutex>

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
    HANDLE hFile = INVALID_HANDLE_VALUE, hMap = NULL;
    bool open(const char* p) {
        hFile = CreateFileA(p, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
        if (hFile == INVALID_HANDLE_VALUE) return false;
        LARGE_INTEGER li; GetFileSizeEx(hFile, &li); size = li.QuadPart;
        if (!size) return true;
        hMap = CreateFileMappingA(hFile, NULL, PAGE_READONLY, 0, 0, NULL);
        if (!hMap) { CloseHandle(hFile); return false; }
        data = (const char*)MapViewOfFile(hMap, FILE_MAP_READ, 0, 0, size);
        return data != nullptr;
    }
    void close() {
        if (data) { UnmapViewOfFile(data); data = nullptr; }
        if (hMap) { CloseHandle(hMap); hMap = NULL; }
        if (hFile != INVALID_HANDLE_VALUE) { CloseHandle(hFile); hFile = INVALID_HANDLE_VALUE; }
    }
#else
    int fd = -1;
    bool open(const char* p) {
        fd = ::open(p, O_RDONLY); if (fd < 0) return false;
        struct stat st; if (fstat(fd, &st) < 0) { ::close(fd); return false; }
        size = st.st_size; if (!size) return true;
        data = (const char*)mmap(nullptr, size, PROT_READ, MAP_SHARED, fd, 0);
        return data != MAP_FAILED;
    }
    void close() {
        if (data && data != MAP_FAILED) { munmap((void*)data, size); data = nullptr; }
        if (fd >= 0) { ::close(fd); fd = -1; }
    }
#endif
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
    if (p[6] == '0') return (p[9] == '0') ? (h - 18) : (6 + h);
    return (p[9] == '1') ? (30 + h) : (54 + h);
}

struct AtomicEndpointMap {
    static constexpr size_t CAPACITY = 4096;
    static constexpr size_t MASK = CAPACITY - 1;
    struct Entry {
        std::atomic<uint64_t> hash{0};
        uint32_t len{0};
        const char* str{nullptr};
        int id{-1};
    };
    Entry entries[CAPACITY];
    std::atomic<int> count{0};
    std::string_view id_to_ep[2500];
    std::mutex insert_mutex;

    int get_or_insert(const char* s, size_t len, uint64_t h) {
        if (!h) h = 1;
        size_t idx = h & MASK;
        while (true) {
            uint64_t eh = entries[idx].hash.load(std::memory_order_relaxed);
            if (eh == 0) {
                std::lock_guard<std::mutex> lock(insert_mutex);
                eh = entries[idx].hash.load(std::memory_order_relaxed);
                if (eh == 0) {
                    int new_id = count.fetch_add(1, std::memory_order_relaxed);
                    entries[idx].len = (uint32_t)len;
                    entries[idx].str = s;
                    entries[idx].id = new_id;
                    id_to_ep[new_id] = std::string_view(s, len);
                    entries[idx].hash.store(h, std::memory_order_release);
                    return new_id;
                }
            }
            if (eh == h && entries[idx].len == len) {
                return entries[idx].id;
            }
            idx = (idx + 1) & MASK;
        }
    }
};

static AtomicEndpointMap GLOBAL_EP_MAP;

struct RawRecord { uint32_t lat; uint16_t eid_and_err; uint16_t _pad; };

struct ThreadParseData {
    std::vector<RawRecord> hour_records[72];
};

void parse_chunk(const char* buf, size_t start, size_t end, ThreadParseData& td) {
    const char* p = buf + start, *limit = buf + end;
    size_t chunk_bytes = end - start;
    size_t est_per_hour = (chunk_bytes / 50) / 72 + 64;
    for (size_t h = 0; h < 72; ++h) td.hour_records[h].reserve(est_per_hour);

    uint32_t last_tag = 0, cur_hid = 0;

    while (p < limit) {
        if (*p == '\r' || *p == '\n') { p++; continue; }
        if (limit - p < 35) break;

        uint32_t tag = *(const uint32_t*)(p + 9);
        if (__builtin_expect(tag != last_tag, 0)) {
            cur_hid = get_hour_idx(p);
            last_tag = tag;
        }

        const char* ep_start = nullptr;
        char m0 = p[28];
        if (m0 == 'G') ep_start = p + 32;
        else if (m0 == 'P') {
            char m1 = p[29];
            if (m1 == 'O') ep_start = p + 33;
            else if (m1 == 'U') ep_start = p + 32;
            else if (m1 == 'A') ep_start = p + 34;
            else { const char* s = p + 28; while (s < limit && *s != ' ') s++; ep_start = s + 1; }
        } else if (m0 == 'D') ep_start = p + 35;
        else if (m0 == 'H') ep_start = p + 33;
        else if (m0 == 'O') ep_start = p + 36;
        else { const char* s = p + 28; while (s < limit && *s != ' ') s++; ep_start = s + 1; }

        if (ep_start >= limit) break;

        const char* ep_end = nullptr;
        if (__builtin_expect(limit - ep_start >= 32, 1)) {
            __m256i chunk = _mm256_loadu_si256((const __m256i*)ep_start);
            uint32_t mask = (uint32_t)_mm256_movemask_epi8(_mm256_cmpeq_epi8(chunk, _mm256_set1_epi8(' ')));
            if (__builtin_expect(mask != 0, 1)) ep_end = ep_start + __builtin_ctz(mask);
            else ep_end = (const char*)std::memchr(ep_start + 32, ' ', limit - (ep_start + 32));
        } else {
            ep_end = (const char*)std::memchr(ep_start, ' ', limit - ep_start);
        }
        if (!ep_end) break;

        size_t ep_len = ep_end - ep_start;
        uint64_t h = 0xcbf29ce484222325ULL;
        size_t rem = ep_len; const char* ptr = ep_start;
        while (rem >= 8) { uint64_t v; std::memcpy(&v, ptr, 8); h = _mm_crc32_u64(h, v); ptr += 8; rem -= 8; }
        while (rem--) h = _mm_crc32_u8((uint32_t)h, (uint8_t)*ptr++);

        int eid = GLOBAL_EP_MAP.get_or_insert(ep_start, ep_len, h);

        const char* status_ptr = ep_end + 1;
        uint16_t is_err = (status_ptr < limit && *status_ptr == '5') ? 0x8000 : 0;
        const char* lat_ptr = status_ptr + 4;
        uint32_t lat = 0;
        while (lat_ptr < limit && *lat_ptr >= '0' && *lat_ptr <= '9') lat = lat * 10 + (*lat_ptr++ - '0');

        td.hour_records[cur_hid].push_back({lat, (uint16_t)((uint16_t)eid | is_err), 0});
        if (lat_ptr < limit && *lat_ptr == '\r') lat_ptr++;
        if (lat_ptr < limit && *lat_ptr == '\n') lat_ptr++;
        p = lat_ptr;
    }
}

static const char DIGITS_100[200] = {
    '0','0','0','1','0','2','0','3','0','4','0','5','0','6','0','7','0','8','0','9',
    '1','0','1','1','1','2','1','3','1','4','1','5','1','6','1','7','1','8','1','9',
    '2','0','2','1','2','2','2','3','2','4','2','5','2','6','2','7','2','8','2','9',
    '3','0','3','1','3','2','3','3','3','4','3','5','3','6','3','7','3','8','3','9',
    '4','0','4','1','4','2','4','3','4','4','4','5','4','6','4','7','4','8','4','9',
    '5','0','5','1','5','2','5','3','5','4','5','5','5','6','5','7','5','8','5','9',
    '6','0','6','1','6','2','6','3','6','4','6','5','6','6','6','7','6','8','6','9',
    '7','0','7','1','7','2','7','3','7','4','7','5','7','6','7','7','7','8','7','9',
    '8','0','8','1','8','2','8','3','8','4','8','5','8','6','8','7','8','8','8','9',
    '9','0','9','1','9','2','9','3','9','4','9','5','9','6','9','7','9','8','9','9'
};

inline char* fast_u32(char* p, uint32_t val) {
    if (val < 10) { *p++ = (char)('0' + val); return p; }
    char buf[12]; int len = 0;
    while (val >= 100) {
        uint32_t rem = val % 100;
        val /= 100;
        buf[len++] = DIGITS_100[rem * 2 + 1];
        buf[len++] = DIGITS_100[rem * 2];
    }
    if (val < 10) {
        buf[len++] = (char)('0' + val);
    } else {
        buf[len++] = DIGITS_100[val * 2 + 1];
        buf[len++] = DIGITS_100[val * 2];
    }
    for (int i = len - 1; i >= 0; --i) *p++ = buf[i];
    return p;
}

int main(int argc, char** argv) {
    std::ios_base::sync_with_stdio(false);
    std::cin.tie(NULL);
#ifndef _WIN32
    mallopt(M_ARENA_MAX, 2);
#endif

    std::string data_dir = (argc > 1) ? argv[1] : "/data";
    std::string out_dir = (argc > 2) ? argv[2] : "/out";

    MappedFile mf;
    if (!mf.open((data_dir + "/requests.log").c_str())) return 1;
    if (!mf.size) { fclose(fopen((out_dir + "/summary.txt").c_str(), "wb")); return 0; }

    const size_t num_threads = 12;
    std::vector<size_t> splits(num_threads + 1, 0);
    splits[num_threads] = mf.size;
    for (size_t i = 1; i < num_threads; ++i) {
        size_t target = i * (mf.size / num_threads);
        const char* nl = (const char*)std::memchr(mf.data + target, '\n', mf.size - target);
        splits[i] = nl ? (nl - mf.data + 1) : mf.size;
    }

    std::vector<ThreadParseData> parse_data(num_threads);
    std::vector<std::thread> threads; threads.reserve(num_threads);
    for (size_t t = 0; t < num_threads; ++t) {
        threads.emplace_back([&, t]() { parse_chunk(mf.data, splits[t], splits[t + 1], parse_data[t]); });
    }
    for (auto& th : threads) th.join();

    int num_eps = GLOBAL_EP_MAP.count.load();
    std::vector<int> sorted_order(num_eps);
    for (int i = 0; i < num_eps; ++i) sorted_order[i] = i;

    std::sort(sorted_order.begin(), sorted_order.end(), [&](int a, int b) {
        return GLOBAL_EP_MAP.id_to_ep[a] < GLOBAL_EP_MAP.id_to_ep[b];
    });

    std::vector<uint16_t> old_to_new(num_eps);
    for (int new_id = 0; new_id < num_eps; ++new_id) {
        old_to_new[sorted_order[new_id]] = (uint16_t)new_id;
    }

    size_t pool_bytes = 0;
    for (int i = 0; i < num_eps; ++i) pool_bytes += GLOBAL_EP_MAP.id_to_ep[i].size();

    std::string ep_pool;
    ep_pool.reserve(pool_bytes + 16);
    std::vector<std::string_view> global_endpoints(num_eps);
    for (int new_id = 0; new_id < num_eps; ++new_id) {
        int old_id = sorted_order[new_id];
        std::string_view sv = GLOBAL_EP_MAP.id_to_ep[old_id];
        size_t off = ep_pool.size();
        ep_pool.append(sv.data(), sv.size());
        global_endpoints[new_id] = std::string_view(ep_pool.data() + off, sv.size());
    }

    mf.close();
#ifndef _WIN32
    malloc_trim(0);
#endif

    std::vector<std::string> hour_outputs(72);
    threads.clear();
    std::atomic<size_t> next_hour(0);

    for (size_t t = 0; t < num_threads; ++t) {
        threads.emplace_back([&, t]() {
            std::vector<uint32_t> ep_counts(num_eps), ep_errors(num_eps), ep_offsets(num_eps + 1), cur_pos(num_eps + 1), lat_pool;
            lat_pool.reserve(16384);
            char line_buf[2048];

            size_t h;
            while ((h = next_hour.fetch_add(1)) < 72) {
                size_t total_recs = 0;
                for (size_t pt = 0; pt < num_threads; ++pt) total_recs += parse_data[pt].hour_records[h].size();
                if (!total_recs) continue;

                std::fill(ep_counts.begin(), ep_counts.end(), 0);
                std::fill(ep_errors.begin(), ep_errors.end(), 0);

                for (size_t pt = 0; pt < num_threads; ++pt) {
                    const auto& recs = parse_data[pt].hour_records[h];
                    for (const auto& r : recs) {
                        uint32_t gid = old_to_new[r.eid_and_err & 0x7FFF];
                        ep_counts[gid]++;
                        if (r.eid_and_err & 0x8000) ep_errors[gid]++;
                    }
                }

                ep_offsets[0] = 0;
                for (int i = 0; i < num_eps; ++i) ep_offsets[i + 1] = ep_offsets[i] + ep_counts[i];
                std::copy(ep_offsets.begin(), ep_offsets.end(), cur_pos.begin());

                if (lat_pool.size() < total_recs) lat_pool.resize(total_recs);

                for (size_t pt = 0; pt < num_threads; ++pt) {
                    const auto& recs = parse_data[pt].hour_records[h];
                    for (const auto& r : recs) {
                        uint32_t gid = old_to_new[r.eid_and_err & 0x7FFF];
                        lat_pool[cur_pos[gid]++] = r.lat;
                    }
                    std::vector<RawRecord>().swap(parse_data[pt].hour_records[h]);
                }

                std::string& out_buf = hour_outputs[h];
                out_buf.reserve(num_eps * 48);

                const char* hr_str = GLOBAL_HOURS[h];
                for (int gid = 0; gid < num_eps; ++gid) {
                    uint32_t n = ep_counts[gid];
                    if (!n) continue;
                    uint32_t* sub = &lat_pool[ep_offsets[gid]];
                    uint32_t p50, p95, p99;
                    if (n == 1) p50 = p95 = p99 = sub[0];
                    else if (n == 2) { uint32_t a = sub[0], b = sub[1]; if (a > b) std::swap(a, b); p50 = a; p95 = p99 = b; }
                    else if (n == 3) {
                        uint32_t a = sub[0], b = sub[1], c = sub[2];
                        if (a > b) std::swap(a, b); if (b > c) std::swap(b, c); if (a > b) std::swap(a, b);
                        p50 = b; p95 = p99 = c;
                    } else if (n == 4) {
                        uint32_t a = sub[0], b = sub[1], c = sub[2], d = sub[3];
                        if (a > b) std::swap(a, b); if (c > d) std::swap(c, d); if (a > c) std::swap(a, c);
                        if (b > d) std::swap(b, d); if (b > c) std::swap(b, c);
                        p50 = b; p95 = p99 = d;
                    } else {
                        size_t idx50 = (50ULL * n + 99) / 100 - 1, idx95 = (95ULL * n + 99) / 100 - 1, idx99 = (99ULL * n + 99) / 100 - 1;
                        if (n <= 64) { std::sort(sub, sub + n); p50 = sub[idx50]; p95 = sub[idx95]; p99 = sub[idx99]; }
                        else {
                            std::nth_element(sub, sub + idx50, sub + n); p50 = sub[idx50];
                            std::nth_element(sub + idx50 + 1, sub + idx95, sub + n); p95 = sub[idx95];
                            std::nth_element(sub + idx95 + 1, sub + idx99, sub + n); p99 = sub[idx99];
                        }
                    }

                    uint32_t err = ep_errors[gid];
                    uint32_t ppm = (uint32_t)((uint64_t)err * 1000000ULL / n);
                    std::string_view ep_str = global_endpoints[gid];

                    char* p = line_buf;
                    std::memcpy(p, hr_str, 20); p += 20; *p++ = ' ';
                    std::memcpy(p, ep_str.data(), ep_str.size()); p += ep_str.size(); *p++ = ' ';
                    p = fast_u32(p, n); *p++ = ' ';
                    p = fast_u32(p, p50); *p++ = ' ';
                    p = fast_u32(p, p95); *p++ = ' ';
                    p = fast_u32(p, p99); *p++ = ' ';
                    p = fast_u32(p, err); *p++ = ' ';
                    p = fast_u32(p, ppm); *p++ = '\n';
                    out_buf.append(line_buf, p - line_buf);
                }
            }
        });
    }
    for (auto& th : threads) th.join();

    FILE* fp = fopen((out_dir + "/summary.txt").c_str(), "wb");
    if (!fp) return 1;
    char out_buf[1048576]; setvbuf(fp, out_buf, _IOFBF, sizeof(out_buf));
    for (size_t h = 0; h < 72; ++h) {
        if (!hour_outputs[h].empty()) fwrite(hour_outputs[h].data(), 1, hour_outputs[h].size(), fp);
    }
    fclose(fp);
    return 0;
}
