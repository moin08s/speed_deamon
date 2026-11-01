quoted_name = 0
unquoted_name = 0
quoted_note = 0
unquoted_note = 0
with open('data/c/records.csv', encoding='utf-8', errors='ignore') as f:
    for line in f:
        p = line.split(',')
        if len(p) >= 5:
            if p[1].startswith('"'): quoted_name += 1
            else: unquoted_name += 1
print(f'Quoted name: {quoted_name}, Unquoted name: {unquoted_name}')
