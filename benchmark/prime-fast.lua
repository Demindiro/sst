
print(2)
print(3)

i = 3

while true do

	i = i + 2

	if i == 20000001 then
		break
	end

	for p = 3,i,2 do

		if i % p == 0 then
			goto nope
		end

		if p * p >= i then
			break
		end

	end

	print(i)

	::nope::

end
