-- User-function call and return overhead.
local function transform(value)
    return value * 1.000001 + 0.5
end

local i = 0
local value = 0.0

while i < 500000 do
    value = transform(value)
    i = i + 1
end
