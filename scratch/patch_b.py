with open('track-b/solution.cpp', 'r', encoding='utf-8') as f:
    code = f.read()

code = code.replace('madvise((void*)mf.data, mf.size, MADV_DONTNEED);', 'madvise((void*)mf.data, mf.size, MADV_WILLNEED);')

target = 'for (auto& th : threads) th.join();\n\n    std::string out_path = out_dir + "/results.txt";'
repl = 'for (auto& th : threads) th.join();\n    mf.close();\n\n    std::string out_path = out_dir + "/results.txt";'
if target in code:
    code = code.replace(target, repl)
    print("Replaced close successfully")
else:
    print("Target not found for close")

with open('track-b/solution.cpp', 'w', encoding='utf-8', newline='\n') as f:
    f.write(code)
print("Updated track-b/solution.cpp")
