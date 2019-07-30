pattern('a?') do
  testcase('', '')

  (1..10).each do |size|
    testcase('a' * size, 'a')
  end
end

pattern('a??') do
  (0..10).each do |size|
    testcase('a' * size, '')
  end
end

pattern('a*') do
  (0..10).each do |size|
    str = 'a' * size
    testcase(str, str)
  end
end

pattern('a*?') do
  (0..10).each do |size|
    testcase('a' * size, '')
  end
end

pattern('a{50}') do
  [0, 10, 30].each do |size|
    str = 'a' * size
    testcase(str, nil)
  end

  [50, 100, 200].each do |size|
    str = 'a' * size
    testcase(str, 'a' * 50)
  end
end

pattern('a{13,37}') do
  [0, 5, 10, 12].each do |size|
    str = 'a' * size
    testcase(str, nil)
  end

  [13, 15, 20, 25, 30, 37].each do |size|
    str = 'a' * size
    testcase(str, str)
  end

  [38, 50, 100, 1000].each do |size|
    str = 'a'  *size
    testcase(str, 'a' * 37)
  end
end
