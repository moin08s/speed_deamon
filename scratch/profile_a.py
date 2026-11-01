import time
import subprocess

res = subprocess.run(['git', 'show', 'aee192f:track-a/solution.cpp'], capture_output=True, text=True)
code = res.stdout

print(f"aee192f lines: {len(code.splitlines())}")

# Add precise chrono timers
code = '#include <chrono>\n' + code
code = code.replace('int main(int argc, char** argv) {', '''int main(int argc, char** argv) {
    auto t0 = std::chrono::high_resolution_clock::now();''')

code = code.replace('for (auto& t : threads) {\n        t.join();\n    }\n    mf.close();', '''for (auto& t : threads) {
        t.join();
    }
    auto t_phase1 = std::chrono::high_resolution_clock::now();
    std::cerr << "Phase 1 parse time: " << std::chrono::duration<double, std::milli>(t_phase1 - t0).count() << " ms\\n";
    mf.close();''')

code = code.replace('fclose(fp);', '''fclose(fp);
    auto t_end = std::chrono::high_resolution_clock::now();
    std::cerr << "Phase 2 reduction + write time: " << std::chrono::duration<double, std::milli>(t_end - t_phase1).count() << " ms\\n";
    std::cerr << "Total Track A time: " << std::chrono::duration<double, std::milli>(t_end - t0).count() << " ms\\n";''')

with open('scratch/test_a_profile.cpp', 'w', encoding='utf-8') as f:
    f.write(code)

print("Wrote scratch/test_a_profile.cpp")
