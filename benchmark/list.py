# List growth, indexed reads, and numeric accumulation.
import time

t0 = time.perf_counter()
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

print((time.perf_counter() - t0) * 1000)
