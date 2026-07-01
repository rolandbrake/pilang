local t0 = os.clock()
local list_ = {}

for i = 0, 1000000 do
    list_[#list_ + 1] = math.random() * 20000 - 10000
end

table.sort(list_)
print((os.clock() - t0) * 1000)
