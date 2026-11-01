with open('track-a/solution.cpp', 'rb') as f:
    content = f.read()

old_needle = b"""    const char* mru_ptr[2] = {nullptr, nullptr};
    size_t mru_len[2] = {0, 0};
    int mru_eid[2] = {-1, -1};

    uint32_t cur_hid = 0;
    char last_h0 = 0, last_h1 = 0, last_d0 = 0, last_m0 = 0;
    auto* cur_hour_vec = &td.hour_records[0];
    const char* last_madv_page = p;"""

new_needle = b"""    struct EpCacheSlot {
        uint64_t hash = 0;
        uint32_t len = 0;
        int id = -1;
        const char* str = nullptr;
    } ep_cache[4096];

    uint32_t cur_hid = 0;
    char last_h0 = 0, last_h1 = 0, last_d0 = 0, last_m0 = 0;
    auto* cur_hour_vec = &td.hour_records[0];"""

old_lookup = b"""        size_t ep_len = ep_end - ep_start;
        int eid = -1;
        if (ep_len == mru_len[0] && std::memcmp(ep_start, mru_ptr[0], ep_len) == 0) {
            eid = mru_eid[0];
        } else if (ep_len == mru_len[1] && std::memcmp(ep_start, mru_ptr[1], ep_len) == 0) {
            eid = mru_eid[1];
        } else {
            eid = td.ep_map.get_or_insert(ep_start, ep_len);
            if (eid >= (int)td.endpoints.size()) {
                td.endpoints.emplace_back(ep_start, ep_len);
            }
            mru_ptr[1] = mru_ptr[0]; mru_len[1] = mru_len[0]; mru_eid[1] = mru_eid[0];
            mru_ptr[0] = ep_start;   mru_len[0] = ep_len;     mru_eid[0] = eid;
        }"""

new_lookup = b"""        size_t ep_len = ep_end - ep_start;
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
            ep_cache[c_idx] = {h, (uint32_t)ep_len, eid, ep_start};
        }"""

for old, new, name in [(old_needle, new_needle, "header"), (old_lookup, new_lookup, "lookup")]:
    if old in content:
        content = content.replace(old, new)
        print(f"{name} replaced LF")
    elif old.replace(b'\n', b'\r\n') in content:
        content = content.replace(old.replace(b'\n', b'\r\n'), new.replace(b'\n', b'\r\n'))
        print(f"{name} replaced CRLF")
    else:
        print(f"{name} NOT found")

with open('track-a/solution.cpp', 'wb') as f:
    f.write(content)
print("Updated track-a/solution.cpp successfully!")
