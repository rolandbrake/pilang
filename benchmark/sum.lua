local i = 0
local total = 0

while i < 1000000 do
    total = total + (i * 3) % 97 - (i % 7)
    i = i + 1
end

print(total)
