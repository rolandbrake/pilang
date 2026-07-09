-- Binary tree allocation, member access, recursion, and traversal.
local Node = {}
Node.__index = Node

function Node.new(item, left, right)
    return setmetatable({
        item = item,
        left = left,
        right = right,
    }, Node)
end

local function make_tree(item, depth)
    if depth <= 0 then
        return Node.new(item, nil, nil)
    end

    return Node.new(
        item,
        make_tree(item * 2 - 1, depth - 1),
        make_tree(item * 2, depth - 1)
    )
end

local function check_tree(node)
    if node.left == nil then
        return node.item
    end

    return node.item + check_tree(node.left) - check_tree(node.right)
end

local function iteration_count(max_depth, depth, min_depth)
    local count = 1
    local repeat_count = max_depth - depth + min_depth

    while repeat_count > 0 do
        count = count * 2
        repeat_count = repeat_count - 1
    end

    return count
end

local min_depth = 4
local max_depth = 10

local t0 = os.clock()
local stretch = make_tree(0, max_depth + 1)
local total = check_tree(stretch)
local long_lived = make_tree(0, max_depth)
local depth = min_depth

while depth <= max_depth do
    local iterations = iteration_count(max_depth, depth, min_depth)
    local i = 1

    while i <= iterations do
        total = total + check_tree(make_tree(i, depth))
        total = total + check_tree(make_tree(-i, depth))
        i = i + 1
    end

    depth = depth + 2
end

total = total + check_tree(long_lived)
print((os.clock() - t0) * 1000)
