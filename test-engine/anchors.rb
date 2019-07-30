pattern('\A') do
  testcase('', '')
  testcase('asdfghjkl', '')
end

pattern('\Ax') do
  testcase('x', 'x')
  testcase('xasdfx', 'x')

  testcase("\nx", nil)
  testcase('ax', nil)
  testcase("\nax", nil)
  testcase('', nil)
end

pattern('^') do
  testcase('', '')
  testcase('asdfghjkl', '')
end  

pattern('^x') do
  testcase('x', 'x')
  testcase('xasdf', 'x')
  testcase("\nx", 'x')
  testcase("a\nx", 'x')

  testcase('ax', nil)
  testcase("\nax", nil)
  testcase('', nil)
end

pattern('\z') do
  testcase('', '')
  testcase('0123456789', ['', 10])
  testcase("01234\n01234", ['', 11])
end

pattern('x\z') do
  testcase('x', 'x')
  testcase('adsfx', 'x')
  testcase('xadsfx', ['x', 5])
  testcase("bbb\nx", ['x', 4])

  testcase('xa', nil)
  testcase("x\n", nil)
  testcase("x\nxa", nil)
  testcase("xa\nxa", nil)
end

pattern('$') do
  testcase('', '')
  testcase('0123456789', ['', 10])
  testcase("01234\n01234", ['', 5])
end

pattern('x$') do
  testcase('x', 'x')
  testcase('adsfx', 'x')
  testcase('xadsfx', ['x', 5])
  testcase("xb\nx", ['x', 3])
  testcase("xb\nbx", ['x', 4])

  testcase('xa', nil)
  testcase("xa\nxa", nil)
end

pattern('\b') do
  testcase('adam', '')
  testcase('    alpha beta gamma', ['', 4])
  testcase(' 1', ['', 1])
  testcase('  _   ', ['', 2])

  testcase('', nil)
end

pattern('\b\.') do
  testcase('hello world.', '.')
  testcase('hi. bonjour.', ['.', 2])

  testcase('', nil)
  testcase('hi', nil)
end

pattern('\B') do
  testcase('iggy', ['', 1])
  testcase(' delta', '')
  testcase('1 ', ['', 2])
  testcase('11', ['', 1])
  testcase('', '')
end

pattern('\Bi') do
  testcase('emits', 'i')
  testcase('it might', ['i', 4])
  testcase('sdfadsfasdfi', 'i')

  testcase('it', nil)
  testcase('i', nil)
end

pattern('e\B') do
  testcase('emits', 'e')
  testcase('seems', 'e')
  testcase('qqqqqqqqqqqqqqqqecscasdfsaf', 'e')

  testcase('case', nil)
  testcase('e', nil)
end
