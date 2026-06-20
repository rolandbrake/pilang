i = 0
total = 0

while i < 1_000_000:
    total += (i * 3) % 97 - (i % 7)
    i += 1

print(total)