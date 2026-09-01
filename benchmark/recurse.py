# Recursive calls and repeated stack unwinding.
import sys
import time


sys.setrecursionlimit(3000)


def recurse(n):
    if n == 0:
        return 0
    return recurse(n - 1) + 1


t0 = time.perf_counter()
i = 0
while i < 1000:
    recurse(1000)
    i += 1

print((time.perf_counter() - t0) * 1000)
