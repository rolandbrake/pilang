# Recursive calls and branch-heavy integer work.
def fib(n):
    if n <= 1:
        return n
    return fib(n - 1) + fib(n - 2)


result = fib(28)