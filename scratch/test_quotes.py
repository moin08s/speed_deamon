with open('data/c/records.csv', 'rb') as f:
    b = f.read()
    print('Total bytes:', len(b))
    print('Total double quotes:', b.count(b'"'))
    print('Total newlines:', b.count(b'\n'))
    print('Total commas:', b.count(b','))
