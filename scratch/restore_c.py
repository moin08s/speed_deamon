import subprocess

res = subprocess.run(['git', 'show', 'bfe9c7b:track-c/solution.cpp'], capture_output=True, text=True)
code = res.stdout

# Add mallopt(M_ARENA_MAX, 2)
assert '#include <unistd.h>' in code
code = code.replace('#include <unistd.h>', '#include <unistd.h>\n#include <malloc.h>')

main_init = """int main(int argc, char** argv) {
    std::ios_base::sync_with_stdio(false);
    std::cin.tie(NULL);"""

main_rep = """int main(int argc, char** argv) {
    std::ios_base::sync_with_stdio(false);
    std::cin.tie(NULL);

#ifndef _WIN32
    mallopt(M_ARENA_MAX, 2);
#endif"""

assert main_init in code
code = code.replace(main_init, main_rep)

with open('track-c/solution.cpp', 'w', encoding='utf-8', newline='\n') as f:
    f.write(code)

print("Restored track-c/solution.cpp with mallopt M_ARENA_MAX=2")
