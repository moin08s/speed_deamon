#include <iostream>
#include <vector>
#include <string>
#include <thread>
#include <barrier>
#include <algorithm>
#include <cstring>
#include <cstdint>
#include <cstdio>
#ifdef _WIN32
#include <windows.h>
#include <intrin.h>
#else
#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
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

struct CountryStats {
    int64_t count = 0, sum_rec_id = 0, sum_cents = 0, min_cents = 0, max_cents = 0;
    bool has_cents = false;
    int64_t sum_name_len = 0, sum_note_len = 0;
};

inline char* fast_i64(char* p, int64_t val) {
    if (val < 0) { *p++ = '-'; val = -val; }
    if (!val) { *p++ = '0'; return p; }
    char buf[24]; int len = 0;
    while (val) { buf[len++] = '0' + (val % 10); val /= 10; }
    for (int i = len - 1; i >= 0; --i) *p++ = buf[i];
    return p;
}

inline char* fast_cents(char* p, int64_t cents) {
    if (cents < 0) { *p++ = '-'; cents = -cents; }
    p = fast_i64(p, cents / 100);
    *p++ = '.';
    int rem = (int)(cents % 100);
    *p++ = '0' + (rem / 10); *p++ = '0' + (rem % 10);
    return p;
}

inline int64_t parse_field_int(const char*& p, const char* limit) {
    if (p >= limit) return 0;
    if (limit - p >= 3 && (unsigned char)p[0] == 0xef && (unsigned char)p[1] == 0xbb && (unsigned char)p[2] == 0xbf) p += 3;
    int64_t val = 0;
    while (p < limit && *p >= '0' && *p <= '9') val = val * 10 + (*p++ - '0');
    if (p < limit && *p == ',') p++;
    return val;
}

inline void parse_name_hash(const char*& p, const char* limit, uint64_t& h, size_t& len) {
    h = 0xcbf29ce484222325ULL; len = 0;
    if (p >= limit) return;
    if (*p == '"') {
        p++;
        while (p < limit) {
            const char* q = (const char*)std::memchr(p, '"', limit - p);
            if (!q) {
                size_t seg = limit - p; len += seg;
                while (seg >= 8) { uint64_t v; std::memcpy(&v, p, 8); h = _mm_crc32_u64(h, v); p += 8; seg -= 8; }
                while (seg--) h = _mm_crc32_u8((uint32_t)h, (uint8_t)*p++);
                break;
            }
            size_t seg = q - p; len += seg; const char* ptr = p;
            while (seg >= 8) { uint64_t v; std::memcpy(&v, ptr, 8); h = _mm_crc32_u64(h, v); ptr += 8; seg -= 8; }
            while (seg--) h = _mm_crc32_u8((uint32_t)h, (uint8_t)*ptr++);
            if (q + 1 < limit && *(q + 1) == '"') { len++; h = _mm_crc32_u8((uint32_t)h, (uint8_t)'"'); p = q + 2; }
            else { p = q + 1; break; }
        }
        if (p < limit && *p == ',') p++;
    } else {
        const char* q = (const char*)std::memchr(p, ',', limit - p);
        if (!q) q = limit;
        size_t seg = q - p; len = seg; const char* ptr = p;
        while (seg >= 8) { uint64_t v; std::memcpy(&v, ptr, 8); h = _mm_crc32_u64(h, v); ptr += 8; seg -= 8; }
        while (seg--) h = _mm_crc32_u8((uint32_t)h, (uint8_t)*ptr++);
        p = (q < limit) ? q + 1 : limit;
    }
}

inline size_t parse_note_len(const char*& p, const char* limit) {
    if (p >= limit) return 0;
    size_t len = 0;
    if (*p == '"') {
        p++;
        while (p < limit) {
            const char* q = (const char*)std::memchr(p, '"', limit - p);
            if (!q) { len += (limit - p); p = limit; break; }
            len += (q - p);
            if (q + 1 < limit && *(q + 1) == '"') { len++; p = q + 2; }
            else { p = q + 1; break; }
        }
        if (p < limit && *p == ',') p++;
    } else {
        const char* q = (const char*)std::memchr(p, ',', limit - p);
        if (q) { len = q - p; p = q + 1; }
        else { len = limit - p; p = limit; }
    }
    return len;
}

inline int32_t parse_reg_id(const char*& p, const char* limit) {
    int32_t val = 0;
    while (p < limit && *p >= '0' && *p <= '9') val = val * 10 + (*p++ - '0');
    if (p < limit && *p == ',') p++;
    return val;
}

inline int64_t parse_cents(const char*& p, const char* limit) {
    if (p >= limit) return 0;
    bool neg = (*p == '-'); if (neg) p++;
    int64_t val = 0;
    while (p < limit && *p >= '0' && *p <= '9') val = val * 10 + (*p++ - '0');
    if (p < limit && *p == '.') p++;
    if (p + 1 < limit) { val = val * 100 + (p[0] - '0') * 10 + (p[1] - '0'); p += 2; }
    while (p < limit && (*p == '\r' || *p == '\n')) p++;
    return neg ? -val : val;
}

size_t find_split(const char* d, size_t sz, size_t target) {
    size_t idx = target;
    while (idx < sz) {
        const char* nl_ptr = (const char*)std::memchr(d + idx, '\n', sz - idx);
        if (!nl_ptr) return sz;
        size_t nl = nl_ptr - d;
        if (nl >= 4 && d[nl - 3] == '.' && d[nl - 2] >= '0' && d[nl - 2] <= '9' && d[nl - 1] >= '0' && d[nl - 1] <= '9') {
            size_t back = (nl > 1000) ? nl - 1000 : 0;
            const char* cur = d + nl - 1;
            while (cur >= d + back && *cur != '\n') cur--;
            size_t ls = (cur >= d + back) ? (cur - d + 1) : 0, p = ls;
            while (p < nl && d[p] >= '0' && d[p] <= '9') p++;
            if (p > ls && p < nl && d[p] == ',') {
                size_t q = 0; for (size_t k = ls; k < nl; ++k) if (d[k] == '"') q++;
                if (q % 2 == 0) return nl + 1;
            }
        }
        idx = nl + 1;
    }
    return sz;
}

int main(int argc, char** argv) {
    std::ios_base::sync_with_stdio(false);
    std::cin.tie(NULL);

    std::string data_dir = (argc > 1) ? argv[1] : "/data";
    std::string out_dir = (argc > 2) ? argv[2] : "/out";

    MappedFile rf;
    if (!rf.open((data_dir + "/regions.csv").c_str())) return 1;

    std::vector<std::string> raw_names; raw_names.reserve(300);
    std::vector<int16_t> region_to_cid(1000000, -1);
    uint16_t code3[32768]; std::memset(code3, 0xff, sizeof(code3));

    const char* rp = rf.data, *r_lim = rf.data + rf.size;
    if (r_lim - rp >= 3 && (unsigned char)rp[0] == 0xef && (unsigned char)rp[1] == 0xbb && (unsigned char)rp[2] == 0xbf) rp += 3;
    while (rp < r_lim && *rp != '\n') rp++;
    if (rp < r_lim && *rp == '\n') rp++;

    while (rp < r_lim) {
        if (*rp == '\r' || *rp == '\n') { rp++; continue; }
        int32_t rid = 0;
        while (rp < r_lim && *rp >= '0' && *rp <= '9') rid = rid * 10 + (*rp++ - '0');
        if (rp < r_lim && *rp == ',') rp++;
        const char* c_start = rp;
        while (rp < r_lim && *rp != '\r' && *rp != '\n') rp++;
        size_t c_len = rp - c_start;
        uint32_t key = ((uint32_t)(c_start[0] & 0x1f) << 10) | ((uint32_t)(c_start[1] & 0x1f) << 5) | (uint32_t)(c_start[2] & 0x1f);
        uint16_t cid = code3[key];
        if (cid == 0xffff) {
            cid = (uint16_t)raw_names.size();
            code3[key] = cid;
            raw_names.emplace_back(c_start, c_len);
        }
        if (__builtin_expect(rid >= (int32_t)region_to_cid.size(), 0)) region_to_cid.resize(rid + 10000, -1);
        region_to_cid[rid] = (int16_t)cid;
    }
    rf.close();

    uint16_t unmatched_id = (uint16_t)raw_names.size();
    raw_names.push_back("_UNMATCHED");

    std::vector<std::string> names = raw_names;
    std::sort(names.begin(), names.end());

    std::vector<uint16_t> remap(raw_names.size());
    for (size_t i = 0; i < raw_names.size(); ++i) {
        remap[i] = (uint16_t)(std::lower_bound(names.begin(), names.end(), raw_names[i]) - names.begin());
    }
    for (size_t rid = 0; rid < region_to_cid.size(); ++rid) {
        if (region_to_cid[rid] >= 0) region_to_cid[rid] = remap[region_to_cid[rid]];
    }
    unmatched_id = remap[unmatched_id];
    size_t num_countries = names.size();

    MappedFile mf;
    if (!mf.open((data_dir + "/records.csv").c_str())) return 1;
    if (!mf.size) { fclose(fopen((out_dir + "/aggregates.txt").c_str(), "wb")); return 0; }

    const size_t num_threads = 12;
    std::vector<size_t> splits(num_threads + 1, 0);
    splits[num_threads] = mf.size;
    for (size_t i = 1; i < num_threads; ++i) splits[i] = find_split(mf.data, mf.size, i * (mf.size / num_threads));

    std::barrier sync(num_threads);

    struct TData {
        std::vector<CountryStats> stats;
        std::vector<uint64_t> items;
        uint32_t offsets[512];
        std::string out;
    };
    std::vector<TData> tdata(num_threads);
    std::vector<std::thread> ths; ths.reserve(num_threads);

    for (size_t t = 0; t < num_threads; ++t) {
        ths.emplace_back([&, t]() {
            auto& td = tdata[t];
            td.stats.resize(num_countries);
            td.items.reserve((splits[t + 1] - splits[t]) / 40 + 1024);

            const char* p = mf.data + splits[t], *lim = mf.data + splits[t + 1];
            if (t == 0) {
                if (lim - p >= 3 && (unsigned char)p[0] == 0xef && (unsigned char)p[1] == 0xbb && (unsigned char)p[2] == 0xbf) p += 3;
                while (p < lim && *p != '\n') p++;
                if (p < lim && *p == '\n') p++;
            }

            while (p < lim) {
                if (*p == '\r' || *p == '\n') { p++; continue; }
                int64_t rec_id = parse_field_int(p, lim);
                uint64_t h = 0; size_t nlen = 0;
                parse_name_hash(p, lim, h, nlen);
                size_t ntlen = parse_note_len(p, lim);
                int32_t rid = parse_reg_id(p, lim);
                int64_t cents = parse_cents(p, lim);

                uint16_t cid = (rid >= 0 && (size_t)rid < region_to_cid.size() && region_to_cid[rid] >= 0)
                               ? (uint16_t)region_to_cid[rid] : unmatched_id;
                auto& cs = td.stats[cid];
                cs.count++; cs.sum_rec_id += rec_id; cs.sum_cents += cents;
                if (!cs.has_cents) { cs.min_cents = cs.max_cents = cents; cs.has_cents = true; }
                else { if (cents < cs.min_cents) cs.min_cents = cents; if (cents > cs.max_cents) cs.max_cents = cents; }
                cs.sum_name_len += nlen; cs.sum_note_len += ntlen;
                td.items.push_back(((uint64_t)cid << 56) | (h & 0x00FFFFFFFFFFFFFFULL));
            }

            std::sort(td.items.begin(), td.items.end());
            td.items.erase(std::unique(td.items.begin(), td.items.end()), td.items.end());

            std::memset(td.offsets, 0, sizeof(td.offsets));
            uint32_t n_items = (uint32_t)td.items.size(), cur_c = 0;
            for (uint32_t i = 0; i < n_items; ++i) {
                uint16_t item_c = (uint16_t)(td.items[i] >> 56);
                while (cur_c <= item_c) td.offsets[cur_c++] = i;
            }
            while (cur_c <= num_countries) td.offsets[cur_c++] = n_items;

            sync.arrive_and_wait();
            if (t == 0) mf.close();

            size_t c_chunk = (num_countries + num_threads - 1) / num_threads;
            size_t c_start = t * c_chunk, c_end = std::min(num_countries, c_start + c_chunk);

            if (c_start < c_end) {
                td.out.reserve((c_end - c_start) * 256);
                char line[2048];
                std::vector<uint64_t> all_h; all_h.reserve(4096);

                for (size_t c = c_start; c < c_end; ++c) {
                    CountryStats comb;
                    for (size_t pt = 0; pt < num_threads; ++pt) {
                        const auto& ts = tdata[pt].stats[c];
                        if (!ts.count) continue;
                        comb.count += ts.count; comb.sum_rec_id += ts.sum_rec_id; comb.sum_cents += ts.sum_cents;
                        if (!comb.has_cents) { comb.min_cents = ts.min_cents; comb.max_cents = ts.max_cents; comb.has_cents = true; }
                        else { if (ts.min_cents < comb.min_cents) comb.min_cents = ts.min_cents; if (ts.max_cents > comb.max_cents) comb.max_cents = ts.max_cents; }
                        comb.sum_name_len += ts.sum_name_len; comb.sum_note_len += ts.sum_note_len;
                    }
                    if (!comb.count) continue;

                    size_t unames = 0;
                    if (comb.count == 1) unames = 1;
                    else {
                        all_h.clear();
                        for (size_t pt = 0; pt < num_threads; ++pt) {
                            uint32_t si = tdata[pt].offsets[c], ei = tdata[pt].offsets[c + 1];
                            all_h.insert(all_h.end(), tdata[pt].items.begin() + si, tdata[pt].items.begin() + ei);
                        }
                        std::sort(all_h.begin(), all_h.end());
                        all_h.erase(std::unique(all_h.begin(), all_h.end()), all_h.end());
                        unames = all_h.size();
                    }

                    const std::string& cn = names[c];
                    char* lp = line;
                    std::memcpy(lp, cn.data(), cn.size()); lp += cn.size();
                    *lp++ = ' '; lp = fast_i64(lp, comb.count);
                    *lp++ = ' '; lp = fast_i64(lp, comb.sum_rec_id);
                    *lp++ = ' '; lp = fast_cents(lp, comb.sum_cents);
                    *lp++ = ' '; lp = fast_cents(lp, comb.min_cents);
                    *lp++ = ' '; lp = fast_cents(lp, comb.max_cents);
                    *lp++ = ' '; lp = fast_i64(lp, unames);
                    *lp++ = ' '; lp = fast_i64(lp, comb.sum_name_len);
                    *lp++ = ' '; lp = fast_i64(lp, comb.sum_note_len);
                    *lp++ = '\n';
                    td.out.append(line, lp - line);
                }
            }
        });
    }
    for (auto& th : ths) th.join();

    FILE* fp = fopen((out_dir + "/aggregates.txt").c_str(), "wb");
    if (!fp) return 1;
    char out_buf[1048576]; setvbuf(fp, out_buf, _IOFBF, sizeof(out_buf));
    for (size_t t = 0; t < num_threads; ++t) {
        if (!tdata[t].out.empty()) fwrite(tdata[t].out.data(), 1, tdata[t].out.size(), fp);
    }
    fclose(fp);
    return 0;
}
