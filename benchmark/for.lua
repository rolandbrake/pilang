local t0 = os.clock()
local numbers = {}

for i = 0, 999999 do
    numbers[#numbers + 1] = i
end

local total = 0
for i = 1, #numbers do
    total = total + numbers[i]
end

print((os.clock() - t0) * 1000)
