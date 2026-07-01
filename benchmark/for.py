import time

t0 = time.perf_counter()
numbers = []

for i in range(1_000_000):
    numbers.append(i)

total = 0
for i in numbers:
    total += i

print((time.perf_counter() - t0) * 1000)
