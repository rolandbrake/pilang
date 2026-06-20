# User-function call and return overhead.
def transform(value):
    return value * 1.000_001 + 0.5


i = 0
value = 0.0

while i < 500_000:
    value = transform(value)
    i += 1