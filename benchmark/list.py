# List growth, indexed reads, and numeric accumulation.
entries = []
i = 0

while i < 100_000:
    entries.append(i * 3)
    i += 1

total = 0
i = 0
while i < len(entries):
    total += entries[i]
    i += 1