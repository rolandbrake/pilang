# User-function call and return overhead.
import time

def transform(value):
    return value * 1.000_001 + 0.5


t0 = time.perf_counter()
i = 0
value = 0.0

while i < 500_000:
    value = transform(value)
    i += 1

print((time.perf_counter() - t0) * 1000)
