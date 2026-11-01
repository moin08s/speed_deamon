import os

with open('data/b/docs.txt', 'rb') as f:
    data = f.read(1000)
    print("Read first 1000 bytes successfully, len =", len(data))
