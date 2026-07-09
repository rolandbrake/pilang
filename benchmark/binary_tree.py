# Binary tree allocation, member access, recursion, and traversal.
import time


class Node:
    def __init__(self, item, left, right):
        self.item = item
        self.left = left
        self.right = right


def make_tree(item, depth):
    if depth <= 0:
        return Node(item, None, None)

    return Node(
        item,
        make_tree(item * 2 - 1, depth - 1),
        make_tree(item * 2, depth - 1),
    )


def check_tree(node):
    if node.left is None:
        return node.item

    return node.item + check_tree(node.left) - check_tree(node.right)


def iteration_count(max_depth, depth, min_depth):
    count = 1
    repeat = max_depth - depth + min_depth

    while repeat > 0:
        count *= 2
        repeat -= 1

    return count


min_depth = 4
max_depth = 10

t0 = time.perf_counter()
stretch = make_tree(0, max_depth + 1)
total = check_tree(stretch)
long_lived = make_tree(0, max_depth)
depth = min_depth

while depth <= max_depth:
    iterations = iteration_count(max_depth, depth, min_depth)
    i = 1

    while i <= iterations:
        total += check_tree(make_tree(i, depth))
        total += check_tree(make_tree(-i, depth))
        i += 1

    depth += 2

total += check_tree(long_lived)
print((time.perf_counter() - t0) * 1000)
