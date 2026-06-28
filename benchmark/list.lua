-- List growth, indexed reads, and numeric accumulation.
local entries = {}
local i = 0

while i < 100000 do
    entries[#entries + 1] = i * 3
    i = i + 1
end

local total = 0
i = 1
while i <= #entries do
    total = total + entries[i]
    i = i + 1
end
