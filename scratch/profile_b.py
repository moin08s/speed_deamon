import time, subprocess

with open('track-b/solution.cpp', 'r') as f:
    code = f.read()

# Add timing prints to track-b/solution.cpp
insert_pos = code.find('int main(')
code = '#include <chrono>\n' + code
code = code.replace('auto t0 = ', '//')
code = code.replace('int main(int argc, char** argv) {', '''int main(int argc, char** argv) {
    auto t0 = std::chrono::high_resolution_clock::now();''')

code = code.replace('size_t num_terms = term_table.count;', '''size_t num_terms = term_table.count;
    auto t_queries = std::chrono::high_resolution_clock::now();
    std::cerr << "Queries parse time: " << std::chrono::duration<double, std::milli>(t_queries - t0).count() << " ms\\n";''')

code = code.replace('for (auto& th : threads) th.join();\n\n    // Compute global term offsets', '''for (auto& th : threads) th.join();
    auto t_pass1 = std::chrono::high_resolution_clock::now();
    std::cerr << "Pass 1 time: " << std::chrono::duration<double, std::milli>(t_pass1 - t_queries).count() << " ms\\n";

    // Compute global term offsets''')

code = code.replace('for (auto& th : threads) th.join();\n\n    uint32_t max_doc_id = 0;', '''for (auto& th : threads) th.join();
    auto t_pass2 = std::chrono::high_resolution_clock::now();
    std::cerr << "Pass 2 time: " << std::chrono::duration<double, std::milli>(t_pass2 - t_pass1).count() << " ms\\n";

    uint32_t max_doc_id = 0;''')

code = code.replace('for (auto& th : threads) th.join();\n\n    std::string out_path = out_dir + "/results.txt";', '''for (auto& th : threads) th.join();
    auto t_query_eval = std::chrono::high_resolution_clock::now();
    std::cerr << "Query eval time: " << std::chrono::duration<double, std::milli>(t_query_eval - t_pass2).count() << " ms\\n";

    std::string out_path = out_dir + "/results.txt";''')

with open('scratch/test_b_profile.cpp', 'w') as f:
    f.write(code)

print("Wrote scratch/test_b_profile.cpp")
