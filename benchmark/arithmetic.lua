-- Integer arithmetic and tight while-loop throughput.
local i = 0
local value = 1

while i < 1000000 do
    value = (value * 1103515245 + i) % 1000000007
    i = i + 1
end
