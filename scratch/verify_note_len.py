with open('data/c/records.csv', 'rb') as f:
    data = f.read()

def old_note_len(raw):
    l = 0
    if not raw: return 0
    if raw[0] == 34 and raw[-1] == 34:
        inner = raw[1:-1]
        i = 0
        while i < len(inner):
            if inner[i:i+2] == b'""':
                l += 1
                i += 2
            else:
                l += 1
                i += 1
    else:
        l = len(raw)
    return l

def new_note_len(raw):
    if not raw: return 0
    if raw[0] == 34 and raw[-1] == 34:
        inner = raw[1:-1]
        return len(inner) - inner.count(b'""')
    return len(raw)

assert new_note_len(b'"said ""no"" twice"') == old_note_len(b'"said ""no"" twice"')
assert new_note_len(b'"Betts"') == 5
assert new_note_len(b'""') == 0
assert new_note_len(b'"a note that\nspans two lines"') == 27
print('new_note_len verified!')
