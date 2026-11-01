with open('track-c/solution.cpp', 'rb') as f:
    content = f.read()

# Normalize CRLF to LF in content temporarily or work directly
is_crlf = b'\r\n' in content

old_block = b"""inline void parse_field_name_hash(const char*& p, const char* limit, uint64_t& out_hash, size_t& utf8_len) {
    out_hash = 0xcbf29ce484222325ULL;
    utf8_len = 0;
    if (p >= limit) return;
    if (*p == '"') {
        p++;
        while (p < limit) {
            const char* q = (const char*)std::memchr(p, '"', limit - p);
            if (!q) {
                utf8_len += (limit - p);
                for (const char* k = p; k < limit; ++k) {
                    out_hash = (out_hash ^ (unsigned char)*k) * 0x100000001b3ULL;
                }
                p = limit;
                break;
            }
            utf8_len += (q - p);
            for (const char* k = p; k < q; ++k) {
                out_hash = (out_hash ^ (unsigned char)*k) * 0x100000001b3ULL;
            }
            if (q + 1 < limit && *(q + 1) == '"') {
                utf8_len++;
                out_hash = (out_hash ^ '"') * 0x100000001b3ULL;
                p = q + 2;
            } else {
                p = q + 1;
                break;
            }
        }
        if (p < limit && *p == ',') p++;
    } else {
        const char* s = p;
        while (p < limit && *p != ',' && *p != '\\n' && *p != '\\r') {
            out_hash = (out_hash ^ (unsigned char)*p++) * 0x100000001b3ULL;
        }
        utf8_len = p - s;
        if (p < limit && *p == ',') p++;
    }
}"""

new_block = b"""inline void parse_field_name_hash(const char*& p, const char* limit, uint64_t& out_hash, size_t& utf8_len) {
    utf8_len = 0;
    uint32_t crc = 0;
    if (p >= limit) { out_hash = 0; return; }
    if (*p == '"') {
        p++;
        while (p < limit) {
            const char* q = (const char*)std::memchr(p, '"', limit - p);
            if (!q) {
                size_t seg = limit - p;
                utf8_len += seg;
                const char* k = p;
#if defined(__x86_64__) || defined(_M_X64)
                while (seg >= 8) {
                    uint64_t v; std::memcpy(&v, k, 8);
                    crc = _mm_crc32_u64(crc, v);
                    k += 8; seg -= 8;
                }
#endif
                while (seg > 0) {
#if defined(__x86_64__) || defined(_M_X64)
                    crc = _mm_crc32_u8(crc, (uint8_t)*k++);
#else
                    crc = (crc ^ (uint8_t)*k++) * 0x100000001b3ULL;
#endif
                    seg--;
                }
                p = limit;
                break;
            }
            size_t seg = q - p;
            utf8_len += seg;
            const char* k = p;
#if defined(__x86_64__) || defined(_M_X64)
            while (seg >= 8) {
                uint64_t v; std::memcpy(&v, k, 8);
                crc = _mm_crc32_u64(crc, v);
                k += 8; seg -= 8;
            }
#endif
            while (seg > 0) {
#if defined(__x86_64__) || defined(_M_X64)
                crc = _mm_crc32_u8(crc, (uint8_t)*k++);
#else
                crc = (crc ^ (uint8_t)*k++) * 0x100000001b3ULL;
#endif
                seg--;
            }
            if (q + 1 < limit && *(q + 1) == '"') {
                utf8_len++;
#if defined(__x86_64__) || defined(_M_X64)
                crc = _mm_crc32_u8(crc, '"');
#else
                crc = (crc ^ '"') * 0x100000001b3ULL;
#endif
                p = q + 2;
            } else {
                p = q + 1;
                break;
            }
        }
        if (p < limit && *p == ',') p++;
    } else {
        const char* s = p;
        const char* q = (const char*)std::memchr(p, ',', limit - p);
        const char* end = q ? q : limit;
        while (s < end && *s != '\\r' && *s != '\\n') s++;
        size_t seg = s - p;
        utf8_len = seg;
        const char* k = p;
#if defined(__x86_64__) || defined(_M_X64)
        while (seg >= 8) {
            uint64_t v; std::memcpy(&v, k, 8);
            crc = _mm_crc32_u64(crc, v);
            k += 8; seg -= 8;
        }
#endif
        while (seg > 0) {
#if defined(__x86_64__) || defined(_M_X64)
            crc = _mm_crc32_u8(crc, (uint8_t)*k++);
#else
            crc = (crc ^ (uint8_t)*k++) * 0x100000001b3ULL;
#endif
            seg--;
        }
        p = q ? q + 1 : s;
    }
    out_hash = ((uint64_t)utf8_len << 32) | crc;
}"""

if old_block in content:
    content = content.replace(old_block, new_block)
    print("Replaced LF")
elif old_block.replace(b'\n', b'\r\n') in content:
    content = content.replace(old_block.replace(b'\n', b'\r\n'), new_block.replace(b'\n', b'\r\n'))
    print("Replaced CRLF")
else:
    print("NOT found")

with open('track-c/solution.cpp', 'wb') as f:
    f.write(content)
