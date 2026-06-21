# Bound instance-method call and return overhead.
class Worker:
    def transform(self, value):
        return value * 1.000_001 + 0.5


worker = Worker()
i = 0
value = 0.0

while i < 500_000:
    value = worker.transform(value)
    i += 1
