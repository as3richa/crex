pattern('\a') do
  testcase("\a", "\a")
  testcase('\a', nil)
  testcase('_', nil)
  testcase('', nil)
end

pattern('\f') do
  testcase("\f", "\f")
  testcase('\f', nil)
  testcase('1', nil)
  testcase('', nil)
end

pattern('\n') do
  testcase("\n", "\n")
  testcase('\n', nil)
  testcase('x', nil)
  testcase('', nil)
end

pattern('\r') do
  testcase("\r", "\r")
  testcase('\r', nil)
  testcase('a', nil)
  testcase('', nil)
end

pattern('\t') do
  testcase("\t", "\t")
  testcase('\t', nil)
  testcase('!', nil)
  testcase('', nil)
end

pattern('\v') do
  testcase("\v", "\v")
  testcase('\v', nil)
  testcase('%', nil)
  testcase('', nil)
end

[0, 5, 10, 17, 30, 80, 101, 127, 150, 210, 255].each do |byte|
  pat = '\x'
  pat += '0' if byte < 16
  pat += byte.to_s(16)

  pattern(pat) do
    testcase(byte.chr, byte.chr)
    testcase((255 - byte).chr, nil)
  end
end
