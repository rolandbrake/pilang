numbers = []

for i in range(1_000_000):
    numbers.append(i)

total = 0
for i in numbers:
    total += i

print(total)
