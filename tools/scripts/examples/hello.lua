local values = {1, 2, 3, 4, 5}
for _, value in ipairs(values) do
  print("square", value, value * value)
  sleep(100)
end
print(string.upper("Lua finished"))
