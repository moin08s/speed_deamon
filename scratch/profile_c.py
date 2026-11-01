import time

# Let's inspect the code of bfe9c7b:track-c/solution.cpp
import subprocess
res = subprocess.run(['git', 'show', 'bfe9c7b:track-c/solution.cpp'], capture_output=True, text=True)
code = res.stdout

print(f"bfe9c7b lines: {len(code.splitlines())}")

# Let's add precise chrono timers to every section
insert_pos = code.find('int main(')
timed_code = '#include <chrono>\n' + code
timed_code = timed_code.replace('int main(int argc, char** argv) {', '''int main(int argc, char** argv) {
    auto t0 = std::chrono::high_resolution_clock::now();''')

timed_code = timed_code.replace('rf.close();', '''rf.close();
    auto t_regions = std::chrono::high_resolution_clock::now();
    std::cerr << "Regions parse time: " << std::chrono::duration<double, std::milli>(t_regions - t0).count() << " ms\\n";''')

timed_code = timed_code.replace('for (auto& th : threads) th.join();\n    mf.close();', '''for (auto& th : threads) th.join();
    auto t_phase1 = std::chrono::high_resolution_clock::now();
    std::cerr << "Phase 1 parse + thread sort time: " << std::chrono::duration<double, std::milli>(t_phase1 - t_regions).count() << " ms\\n";
    mf.close();''')

timed_code = timed_code.replace('fclose(fp);', '''fclose(fp);
    auto t_end = std::chrono::high_resolution_clock::now();
    std::cerr << "Phase 2 reduction + write time: " << std::chrono::duration<double, std::milli>(t_end - t_phase1).count() << " ms\\n";
    std::cerr << "Total Track C time: " << std::chrono::duration<double, std::milli>(t_end - t0).count() << " ms\\n";''')

with open('scratch/test_c_profile.cpp', 'w', encoding='utf-8') as f:
    f.write(timed_code)

print("Wrote scratch/test_c_profile.cpp")
