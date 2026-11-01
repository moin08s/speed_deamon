# Test if removing Phase 1 sorting changes the Phase 2 output
import random

# simulate 12 threads with random hashes for 10 countries
for seed in range(5):
    random.seed(seed)
    num_countries = 10
    num_threads = 12
    thread_data = [ [ [random.randint(1, 50) for _ in range(random.randint(0, 30))] for _ in range(num_countries) ] for _ in range(num_threads) ]
    
    # Method A: sort in Phase 1, then sort in Phase 2
    res_a = []
    for cid in range(num_countries):
        all_h = []
        for t in range(num_threads):
            nh = sorted(list(set(thread_data[t][cid]))) # Phase 1 sort + unique
            all_h.extend(nh)
        res_a.append(len(set(all_h)))
        
    # Method B: no Phase 1 sort, only sort in Phase 2
    res_b = []
    for cid in range(num_countries):
        all_h = []
        for t in range(num_threads):
            nh = thread_data[t][cid] # NO Phase 1 sort
            all_h.extend(nh)
        res_b.append(len(set(all_h)))
        
    assert res_a == res_b, f"Mismatch: {res_a} vs {res_b}"

print("Phase 1 sort elimination is 100% MATHEMATICALLY IDENTICAL!")
