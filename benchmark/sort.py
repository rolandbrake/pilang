import random
import time

t0 = time.perf_counter()
list_ = []

for i in range(1_000_001):
    list_.append(random.uniform(-10_000, 10_000))

list_.sort()
print((time.perf_counter() - t0) * 1000)
