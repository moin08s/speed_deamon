import re

with open('scratch/test_track_c_ultra2.cpp', 'r', encoding='utf-8') as f:
    code = f.read()

# Remove chrono and cerr
code = code.replace('#include <chrono>\n', '')
code = code.replace('    auto t0 = std::chrono::high_resolution_clock::now();\n\n', '')
tail_re = re.compile(r'    auto t_end = std::chrono::high_resolution_clock::now\(\);\n    double total_ms = [^\n]+;\n    std::cerr << [^\n]+;\n', re.MULTILINE)
code = tail_re.sub('', code)

with open('track-c/solution.cpp', 'w', encoding='utf-8', newline='\n') as f:
    f.write(code)
print('Successfully written to track-c/solution.cpp')
