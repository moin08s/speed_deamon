with open('data/c/records.csv', 'r', encoding='utf-8') as f:
    import csv
    r = csv.reader(f)
    next(r)
    total = 0
    with_quotes = 0
    with_newlines = 0
    for row in r:
        total += 1
        note = row[2]
        if '"' in note: with_quotes += 1
        if '\n' in note or '\r' in note: with_newlines += 1
print(f'Total: {total}, Notes with quotes: {with_quotes}, Notes with newlines: {with_newlines}')
