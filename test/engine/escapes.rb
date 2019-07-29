pattern('\a') do
  testcase("\a", "\a")
  testcase('\a', nil)
  testcase('_', nil)
end

pattern('\f') do
  testcase("\f", "\f")
  testcase('\f', nil)
  testcase('1', nil)
end

pattern('\n') do
  testcase("\n", "\n")
  testcase('\n', nil)
  testcase('x', nil)
end

pattern('\r') do
  testcase("\r", "\r")
  testcase('\r', nil)
  testcase('a', nil)
end

pattern('\t') do
  testcase("\t", "\t")
  testcase('\t', nil)
  testcase('!', nil)
end

pattern('\v') do
  testcase("\v", "\v")
  testcase('\v', nil)
  testcase('%', nil)
end
