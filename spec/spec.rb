module Spec
  def pattern(pat)
    raise "nested call to pattern" unless @active_pattern_id.nil?

    @active_pattern_id = patterns.size
    patterns << pat

    yield

    @active_pattern_id = nil
  end

  def testcase(str, groups)
    raise "call to testcase outside of pattern" if @active_pattern_id.nil?
    groups = { 0 => groups } if groups.is_a?(String)
    testcases << [@active_pattern_id, str, groups]
  end

  def emit
    puts("#include \"../spec/types.h\"")

    puts("const size_t n_patterns = #{patterns.size};")

    puts("const pattern_defn_t pattern_defns[] = {")
    puts(patterns.map { |pattern| "  {#{pattern.inspect}, #{pattern.size}}" }.join(",\n"))
    puts("};")

    puts("const size_t n_testcases = #{testcases.size};")

    serialized_cases = testcases.map do |pattern_id, str, groups|
      if groups.nil?
        groups_ary = [[0xffffffff, 0xffffffff]]
      else
        groups_ary = [[0xffffffff, 0xffffffff]] * (groups.keys.max + 1)

        groups.each do |index, substr|
          if substr.is_a?(Array)
            position = substr[1]
            substr = substr[0]
          else
            position = str.index(substr)
          end

          raise "bad test data" unless str[position, substr.length] == substr

          groups_ary[index] = [position, position + substr.length]
        end
      end

      serialized_groups = groups_ary.map { |position, finish| "{#{position}, #{finish}}"}.join(', ')

      "  {#{pattern_id}, #{str.inspect}, #{str.size}, #{groups.nil? ? 0 : 1}, {#{serialized_groups}}}"
    end

    puts("const testcase_t testcases[] = {")
    puts(serialized_cases.join(",\n"))
    puts("};")
  end

  private

  def patterns
    @patterns ||= []
  end

  def testcases
    @testcases ||= []
  end
end

include Spec

Dir.glob('**/*.rb', base: __dir__) do |path|
  next if path == File.basename(__FILE__)
  require_relative(path)
end

emit
