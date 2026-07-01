-- Recursive calls and branch-heavy integer work.
local function fib(n)
    if n <= 1 then
        return n
    end
    return fib(n - 1) + fib(n - 2)
end

local t0 = os.clock()
local result = fib(28)
print((os.clock() - t0) * 1000)
