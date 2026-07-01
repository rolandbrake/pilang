import time

i = 0
total = 0

t0 = time.perf_counter()
while i < 1_000_000:
    total += (i * 3) % 97 - (i % 7)
    i += 1

print((time.perf_counter() - t0) * 1000)
