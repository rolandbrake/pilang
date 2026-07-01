# Integer arithmetic and tight while-loop throughput.
import time

t0 = time.perf_counter()
i = 0
value = 1

while i < 1_000_000:
    value = (value * 1_103_515_245 + i) % 1_000_000_007
    i += 1

print((time.perf_counter() - t0) * 1000)
