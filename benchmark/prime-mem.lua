
print(2)
print(3)

i = 3
n = 2

primes = {2, 3}

sqrt = math.sqrt

while true do

	i = i + 2

	if i == 1099729 then
		break
	end

	for j = 1, n do

		p = primes[j]

		if i % p == 0 then
			goto nope
		end

		if p * p >= i then
			break
		end

	end

	print(i)
	n = n + 1
	primes[n] = p

	::nope::

end
