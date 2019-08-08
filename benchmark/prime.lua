print(2)
print(3)
i = 3
while true do
	i = i + 2
	if i == 200001 then
		break
	end
	for j=2,(i / 2) do
		if i % j == 0 then
			goto nope
		end
	end
	print(i)
::nope::
end
