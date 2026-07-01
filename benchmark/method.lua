-- Bound instance-method call and return overhead.
local Worker = {}
Worker.__index = Worker

function Worker:transform(value)
    return value * 1.000001 + 0.5
end

local worker = setmetatable({}, Worker)
local t0 = os.clock()
local i = 0
local value = 0.0

while i < 500000 do
    value = worker:transform(value)
    i = i + 1
end

print((os.clock() - t0) * 1000)
