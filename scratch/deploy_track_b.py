import re

with open('scratch/test_track_b_fastquery.cpp', 'r', encoding='utf-8') as f:
    code = f.read()

# 1. Remove chrono and cerr
code = code.replace('#include <chrono>\n', '')
code = code.replace('    auto t0 = std::chrono::high_resolution_clock::now();\n\n', '')
code = code.replace('    auto t_queries = std::chrono::high_resolution_clock::now();\n    std::cerr << "Fast queries parse time: " << std::chrono::duration<double, std::milli>(t_queries - t0).count() << " ms\\n";\n', '')
tail_re = re.compile(r'    auto t_end = std::chrono::high_resolution_clock::now\(\);\n    double total_ms = [^\n]+;\n    std::cerr << [^\n]+;\n', re.MULTILINE)
code = tail_re.sub('', code)

# 2. Add malloc.h
assert '#include <unistd.h>' in code
code = code.replace('#include <unistd.h>', '#include <unistd.h>\n#include <malloc.h>')

# 3. Add mallopt(M_ARENA_MAX, 2);
main_init = """int main(int argc, char** argv) {
    std::ios_base::sync_with_stdio(false);
    std::cin.tie(NULL);
    init_tables();"""

main_replacement = """int main(int argc, char** argv) {
    std::ios_base::sync_with_stdio(false);
    std::cin.tie(NULL);
    init_tables();

#ifndef _WIN32
    mallopt(M_ARENA_MAX, 2);
#endif"""

assert main_init in code
code = code.replace(main_init, main_replacement)

with open('track-b/solution.cpp', 'w', encoding='utf-8', newline='\n') as f:
    f.write(code)

print("Successfully written to track-b/solution.cpp")
