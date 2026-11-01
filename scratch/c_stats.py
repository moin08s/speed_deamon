import csv

region_to_country = {}
with open('data/c/regions.csv', 'r', encoding='utf-8-sig') as f:
    reader = csv.reader(f)
    next(reader)
    for row in reader:
        if row:
            region_to_country[int(row[0])] = row[1]

country_names = {}
with open('data/c/records.csv', 'r', encoding='utf-8-sig') as f:
    reader = csv.reader(f)
    next(reader)
    for row in reader:
        reg_id = int(row[3])
        c = region_to_country.get(reg_id, '_UNMATCHED')
        name = row[1]
        if c not in country_names:
            country_names[c] = set()
        country_names[c].add(name)

max_names = max(len(names) for names in country_names.values())
total_pairs = sum(len(names) for names in country_names.values())
print(f"Num countries: {len(country_names)}")
print(f"Max distinct names in any country: {max_names}")
print(f"Total unique (country, name) pairs: {total_pairs}")
