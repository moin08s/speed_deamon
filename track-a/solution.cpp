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

struct GroupEntry {
    uint64_t hash = 0;
    const char* hr_ptr = nullptr;
    const char* ep_ptr = nullptr;
    uint16_t ep_len = 0;
    uint32_t count = 0;
    uint32_t errors = 0;
    uint32_t inlined[4];
    std::vector<uint32_t>* overflow = nullptr;

    inline void add(uint32_t lat, uint8_t is_err) {
        if (count < 4) {
            inlined[count] = lat;
        } else if (count == 4) {
            overflow = new std::vector<uint32_t>();
            overflow->reserve(16);
            overflow->insert(overflow->end(), inlined, inlined + 4);
            overflow->push_back(lat);
        } else {
            overflow->push_back(lat);
        }
        count++;
        if (is_err) errors++;
    }
};

struct ThreadGroupTable {
    static constexpr size_t CAP = 32768;
    static constexpr size_t MASK = CAP - 1;
    std::vector<GroupEntry> slots;
    std::vector<uint32_t> active_indices;

    ThreadGroupTable() : slots(CAP) {
        active_indices.reserve(16384);
    }

    inline void insert_record(const char* hr_ptr, const char* ep_ptr, uint16_t ep_len, uint32_t lat, uint8_t is_err) {
        uint64_t h = 0xcbf29ce484222325ULL;
        for (size_t i = 0; i < 13; ++i) h = (h ^ (unsigned char)hr_ptr[i]) * 0x100000001b3ULL;
        for (size_t i = 0; i < ep_len; ++i) h = (h ^ (unsigned char)ep_ptr[i]) * 0x100000001b3ULL;
        if (h == 0) h = 1;

        size_t idx = (size_t)h & MASK;
        while (slots[idx].hash != 0) {
            if (slots[idx].hash == h && slots[idx].ep_len == ep_len &&
                std::memcmp(slots[idx].hr_ptr, hr_ptr, 13) == 0 &&
                std::memcmp(slots[idx].ep_ptr, ep_ptr, ep_len) == 0) {
                slots[idx].add(lat, is_err);
                return;
            }
            idx = (idx + 1) & MASK;
        }

        auto& entry = slots[idx];
        entry.hash = h;
        entry.hr_ptr = hr_ptr;
        entry.ep_ptr = ep_ptr;
        entry.ep_len = ep_len;
        entry.count = 0;
        entry.errors = 0;
        entry.overflow = nullptr;
        entry.add(lat, is_err);
        active_indices.push_back((uint32_t)idx);
    }
};

struct MergedGroup {
    std::string hour;
    std::string endpoint;
    uint32_t count = 0;
    uint32_t errors = 0;
    uint32_t inlined[4];
    std::vector<uint32_t> overflow;

    inline void add_entry(const GroupEntry& ge) {
        if (ge.count == 0) return;
        if (count + ge.count <= 4) {
            for (uint32_t i = 0; i < ge.count; ++i) inlined[count + i] = ge.inlined[i];
        } else {
            if (overflow.empty()) {
                overflow.reserve(count + ge.count + 8);
                for (uint32_t i = 0; i < count; ++i) overflow.push_back(inlined[i]);
            }
            if (ge.count <= 4) {
                for (uint32_t i = 0; i < ge.count; ++i) overflow.push_back(ge.inlined[i]);
            } else if (ge.overflow) {
                overflow.insert(overflow.end(), ge.overflow->begin(), ge.overflow->end());
            }
        }
        count += ge.count;
        errors += ge.errors;
    }
};

int main(int argc, char** argv) {
    std::ios_base::sync_with_stdio(false);
    std::cin.tie(NULL);

#ifndef _WIN32
    mallopt(M_ARENA_MAX, 2);
#endif

    std::string data_dir = (argc > 1) ? argv[1] : "/data";
    std::string out_dir = (argc > 2) ? argv[2] : "/out";

    std::string log_path = data_dir + "/requests.log";
    MappedFile mf;
    if (!mf.open(log_path.c_str())) return 1;

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
        if (nl) split_points.push_back((nl - mf.data) + 1);
        else split_points.push_back(mf.size);
    }
    split_points.push_back(mf.size);

    std::vector<ThreadGroupTable> thread_tables(num_threads);
    std::vector<std::thread> threads;
    threads.reserve(num_threads);

    // Single-Pass High-Throughput Chunk Parsing Directly into Thread-Local Hash Table
    for (size_t t = 0; t < num_threads; ++t) {
        threads.emplace_back([&, t]() {
            const char* p = mf.data + split_points[t];
            const char* limit = mf.data + split_points[t + 1];
            auto& tt = thread_tables[t];

            while (p < limit) {
                if (*p == '\r' || *p == '\n') { p++; continue; }
                if (limit - p < 35) break;

                const char* hr_ptr = p;

                // O(1) HTTP method jump
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

                // Status
                const char* status_ptr = ep_end + 1;
                uint8_t is_err = (status_ptr < limit && *status_ptr == '5') ? 1 : 0;

                // Latency
                const char* lat_ptr = status_ptr + 4;
                uint32_t lat = 0;
                while (lat_ptr < limit && *lat_ptr >= '0' && *lat_ptr <= '9') {
                    lat = lat * 10 + (*lat_ptr - '0');
                    lat_ptr++;
                }

                tt.insert_record(hr_ptr, ep_start, (uint16_t)ep_len, lat, is_err);

                const char* nl = (const char*)std::memchr(lat_ptr, '\n', limit - lat_ptr);
                p = nl ? nl + 1 : limit;
            }
        });
    }

    for (auto& th : threads) th.join();

    // Global Unique Group Map
    static constexpr size_t GLOBAL_CAP = 262144;
    static constexpr size_t GLOBAL_MASK = GLOBAL_CAP - 1;
    struct GlobalSlot {
        uint64_t hash = 0;
        uint32_t merged_id = 0;
    };
    std::vector<GlobalSlot> global_hash(GLOBAL_CAP);
    std::vector<MergedGroup> merged_groups;
    merged_groups.reserve(65536);

    for (size_t t = 0; t < num_threads; ++t) {
        auto& tt = thread_tables[t];
        for (uint32_t idx : tt.active_indices) {
            const auto& ge = tt.slots[idx];
            uint64_t h = ge.hash;
            size_t gh_idx = (size_t)h & GLOBAL_MASK;
            bool found = false;
            while (global_hash[gh_idx].hash != 0) {
                if (global_hash[gh_idx].hash == h) {
                    uint32_t mid = global_hash[gh_idx].merged_id;
                    if (merged_groups[mid].endpoint.size() == ge.ep_len &&
                        std::memcmp(merged_groups[mid].hour.data(), ge.hr_ptr, 13) == 0 &&
                        std::memcmp(merged_groups[mid].endpoint.data(), ge.ep_ptr, ge.ep_len) == 0) {
                        merged_groups[mid].add_entry(ge);
                        found = true;
                        break;
                    }
                }
                gh_idx = (gh_idx + 1) & GLOBAL_MASK;
            }
            if (!found) {
                global_hash[gh_idx].hash = h;
                uint32_t mid = (uint32_t)merged_groups.size();
                global_hash[gh_idx].merged_id = mid;
                MergedGroup mg;
                mg.hour.assign(ge.hr_ptr, 13);
                mg.endpoint.assign(ge.ep_ptr, ge.ep_len);
                mg.add_entry(ge);
                merged_groups.push_back(std::move(mg));
            }
        }
        for (uint32_t idx : tt.active_indices) {
            delete tt.slots[idx].overflow;
        }
        std::vector<GroupEntry>().swap(tt.slots);
        std::vector<uint32_t>().swap(tt.active_indices);
    }

    std::vector<GlobalSlot>().swap(global_hash); // Free global hash table immediately
#ifndef _WIN32
    malloc_trim(0);
#endif
    mf.close(); // Drop requests.log page cache now that all strings are copied to merged_groups

    // Sort merged groups by hour ascending, then endpoint ascending
    std::vector<uint32_t> sorted_group_ids(merged_groups.size());
    for (size_t i = 0; i < merged_groups.size(); ++i) sorted_group_ids[i] = (uint32_t)i;
    std::sort(sorted_group_ids.begin(), sorted_group_ids.end(), [&](uint32_t a, uint32_t b) {
        const auto& ga = merged_groups[a];
        const auto& gb = merged_groups[b];
        int cmp_hr = ga.hour.compare(gb.hour);
        if (cmp_hr != 0) return cmp_hr < 0;
        return ga.endpoint < gb.endpoint;
    });

    // Parallel formatting across 12 threads
    size_t total_groups = sorted_group_ids.size();
    std::vector<std::string> thread_outputs(num_threads);
    threads.clear();
    size_t g_chunk = (total_groups + num_threads - 1) / num_threads;

    for (size_t t = 0; t < num_threads; ++t) {
        size_t start = t * g_chunk;
        size_t end = std::min(total_groups, start + g_chunk);
        threads.emplace_back([&, t, start, end]() {
            if (start >= end) return;
            std::string& out_buf = thread_outputs[t];
            out_buf.reserve((end - start) * 96);
            char line_buf[2048];

            for (size_t i = start; i < end; ++i) {
                auto& mg = merged_groups[sorted_group_ids[i]];
                size_t n = mg.count;
                if (n == 0) continue;

                size_t idx50 = (50ULL * n + 99) / 100 - 1;
                size_t idx95 = (95ULL * n + 99) / 100 - 1;
                size_t idx99 = (99ULL * n + 99) / 100 - 1;

                uint32_t p50, p95, p99;
                if (n == 1) {
                    p50 = p95 = p99 = mg.inlined[0];
                } else if (n == 2) {
                    uint32_t a = mg.inlined[0], b = mg.inlined[1];
                    if (a > b) std::swap(a, b);
                    p50 = a; p95 = b; p99 = b;
                } else if (n == 3) {
                    uint32_t a = mg.inlined[0], b = mg.inlined[1], c = mg.inlined[2];
                    if (a > b) std::swap(a, b);
                    if (b > c) std::swap(b, c);
                    if (a > b) std::swap(a, b);
                    p50 = b; p95 = c; p99 = c;
                } else if (n == 4) {
                    uint32_t a = mg.inlined[0], b = mg.inlined[1], c = mg.inlined[2], d = mg.inlined[3];
                    if (a > b) std::swap(a, b);
                    if (c > d) std::swap(c, d);
                    if (a > c) std::swap(a, c);
                    if (b > d) std::swap(b, d);
                    if (b > c) std::swap(b, c);
                    p50 = b; p95 = d; p99 = d;
                } else {
                    auto& lat = mg.overflow;
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
                }

                uint64_t ppm = (uint64_t)mg.errors * 1000000ULL / n;

                char* p = line_buf;
                std::memcpy(p, mg.hour.data(), 13);
                p += 13;
                std::memcpy(p, ":00:00Z ", 8);
                p += 8;
                std::memcpy(p, mg.endpoint.data(), mg.endpoint.size());
                p += mg.endpoint.size();
                *p++ = ' ';
                p = fast_u64(p, n);
                *p++ = ' ';
                p = fast_u32(p, p50);
                *p++ = ' ';
                p = fast_u32(p, p95);
                *p++ = ' ';
                p = fast_u32(p, p99);
                *p++ = ' ';
                p = fast_u32(p, mg.errors);
                *p++ = ' ';
                p = fast_u64(p, ppm);
                *p++ = '\n';

                out_buf.append(line_buf, p - line_buf);
            }
        });
    }

    for (auto& th : threads) th.join();

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
