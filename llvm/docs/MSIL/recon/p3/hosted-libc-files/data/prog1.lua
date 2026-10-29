-- hosted-libc recon: a deterministic Lua program without errors or yields.
local function fib(n) if n < 2 then return n end return fib(n-1) + fib(n-2) end
print("fib(27)", fib(27))
local nums = {0.1, 1/3, 2^53, -0.0, 1e300*10, 123456789.125, math.pi, 1e-310, 255}
for _, x in ipairs(nums) do
  io.write(string.format("%g|%.14g|%a|%e|%5.2f|", x, x, x, x, x), tostring(x), "\n")
end
print(string.format("%d %5d %-5d| %x %X %o %c %q", 42, -7, 3, 255, 3054, 8, 65, "a\nb\0c"))
print(tonumber("0x1p4"), tonumber("  12.5e3 "), tonumber("0x10"), tonumber("1e400"), math.tointeger(3.0))
local t = {}
for i = 1, 2000 do t[i] = (i * 7919) % 2003 end
table.sort(t, function(a, b) return a > b end)
print("sorted", t[1], t[1000], t[2000])
local s = string.rep("abc", 1000) .. "XYZ"
print(#s, s:find("XYZ"), (s:gsub("b", "B")):sub(1, 9), select(2, s:gsub("c", "")))
print(("hello world from lua"):gsub("(%w+)", "<%1>"))
print(string.pack("<i4d", 7, 2.5):byte(1, -1))
print(string.unpack("<i4d", string.pack("<i4d", 7, 2.5)))
print(utf8.char(72, 228, 8364, 128512), utf8.len("h\u{e4}\u{20ac}"))
print(os.time({year=2024, month=2, day=29, hour=12, min=0, sec=0, isdst=false}) ~= nil)
print(os.date("!%Y-%m-%d %H:%M:%S %j %a %b", 86400 * 19000 + 3661))
print(os.setlocale(), os.setlocale(nil, "numeric"))
print(type(os.getenv("PATH")), os.getenv("MSIL_NO_SUCH_VAR"))
local name = "msil_prog1_tmp.txt"
local f = assert(io.open(name, "w")); for i = 1, 5 do f:write(i, " line ", i * i, "\n") end; f:close()
local sum = 0; for l in io.lines(name) do sum = sum + tonumber(l:match("(%d+)$")) end
print("lines sum", sum, os.remove(name))
math.randomseed(42); local r = {}; for i = 1, 5 do r[i] = math.random(1, 1000) end; print(table.concat(r, " "))
print(math.floor(-3.5), math.ceil(-3.5), math.fmod(7, -3), math.sqrt(2), math.log(8, 2), math.exp(1), math.ult(1, -1))
print(string.format("%.3f %.3f %.3f %.3f", math.sin(1), math.cos(1), math.tan(1), math.atan(1, 2)))
print(collectgarbage("step"), #tostring(collectgarbage("count")) > 0)
print(os.clock() >= 0, os.difftime(10, 4))
