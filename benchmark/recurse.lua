-- Recursive calls and repeated stack unwinding.
local function recurse(n)
    if n == 0 then
        return 0
    end
    return recurse(n - 1) + 1
end

local t0 = os.clock()
local i = 0
while i < 1000 do
    recurse(1000)
    i = i + 1
end

print((os.clock() - t0) * 1000)
