import subprocess

code = open('scratch/test_track_b_fast.cpp').read()
debug_code = '''if (q_idx == 0) {
    for (int t : q.bool_terms) {
        auto [ptr, l] = get_postings(t);
        bool has3771 = false;
        for (size_t i = 0; i < l; ++i) if (ptr[i] == 3771) has3771 = true;
        std::cerr << "Term " << t << " len=" << l << " has 3771: " << has3771 << "\\n";
    }
}
char* p = line_buf;'''

code = code.replace('char* p = line_buf;', debug_code)
open('scratch/test_debug.cpp', 'w').write(code)

zig_path = r'C:\Users\Moin\AppData\Local\uv\cache\archive-v0\5vQrUNMrL19-ZSuf\Lib\site-packages\ziglang\zig.exe'
subprocess.run([zig_path, 'c++', '-O3', '-std=c++20', 'scratch/test_debug.cpp', '-o', 'test_debug.exe'], check=True)
res = subprocess.run(['./test_debug.exe', 'data/b', 'tmp_out_b_test'], capture_output=True, text=True)
print(res.stderr)
