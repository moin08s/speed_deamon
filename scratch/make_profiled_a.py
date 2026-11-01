with open('track-a/solution.cpp', 'r') as f:
    code = f.read()

# Add chrono prints
code = '#include <chrono>\n' + code
code = code.replace('int main(int argc, char** argv) {', '''int main(int argc, char** argv) {
    auto t0 = std::chrono::high_resolution_clock::now();''')

code = code.replace('for (auto& t : threads) {\n        t.join();\n    }', '''for (auto& t : threads) {
        t.join();
    }
    auto t_pass1 = std::chrono::high_resolution_clock::now();
    std::cerr << "Phase 1 Parse time: " << std::chrono::duration<double, std::milli>(t_pass1 - t0).count() << " ms\\n";''')

code = code.replace('for (auto& t : threads) {\n        t.join();\n    }\n\n    std::string out_path = out_dir + "/summary.txt";', '''for (auto& t : threads) {
        t.join();
    }
    auto t_pass2 = std::chrono::high_resolution_clock::now();
    std::cerr << "Phase 2 Reduction time: " << std::chrono::duration<double, std::milli>(t_pass2 - t_pass1).count() << " ms\\n";

    std::string out_path = out_dir + "/summary.txt";''')

code = code.replace('fclose(fp);\n\n    return 0;', '''fclose(fp);
    auto t_end = std::chrono::high_resolution_clock::now();
    std::cerr << "Output write time: " << std::chrono::duration<double, std::milli>(t_end - t_pass2).count() << " ms\\n";
    std::cerr << "Total time: " << std::chrono::duration<double, std::milli>(t_end - t0).count() << " ms\\n";

    return 0;''')

with open('scratch/profiled_a.cpp', 'w') as f:
    f.write(code)

print("Wrote scratch/profiled_a.cpp")
