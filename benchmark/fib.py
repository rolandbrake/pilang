# Recursive calls and branch-heavy integer work.
import time

def fib(n):
    if n <= 1:
        return n
    return fib(n - 1) + fib(n - 2)


t0 = time.perf_counter()
result = fib(28)
print((time.perf_counter() - t0) * 1000)
