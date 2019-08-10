#!/usr/bin/env ruby

INSTRUCTIONS = [
  {
    name: 'mov64',
    rex_w: true,
    opcode: [0x8b],
    operands: [
      ['destination', :reg],
      ['source', :rm_mem],
    ],
  },
  {
    name: 'call',
    opcode: [0xff],
    extension: 0x02,
    operands: [['callee', :rm_mem]],
  },
  {
    name: 'bt64',
    rex_w: true,
    opcode: [0x0f, 0xba],
    extension: 0x04,
    operands: [
      ['bitstring', :rm_reg],
      ['index', :u8],
    ],
  },
  {
    name: 'bt32',
    opcode: [0x0f, 0xba],
    extension: 0x04,
    operands: [
      ['bitstring', :rm_mem],
      ['index', :u8],
    ]
  },
  {
    name: 'lea64',
    rex_w: true,
    opcode: [0x8d],
    operands: [
      ['destination', :reg],
      ['address', :rm_mem],
    ]
  }
]

TYPES = {
  i8: 'int',
  i32: 'long',
  reg: 'reg_t',
  rm_mem: 'indirect_operand_t',
  rm_reg: 'reg_t',
  u8: 'size_t',
  u32: 'size_t',
}

SIZES = {
  i8: 1,
  i32: 4,
  u8: 1,
  u32: 4
}

first = true

INSTRUCTIONS.each do |name:, rex_w: false, opcode:, extension: nil, operands:|
  puts("\n\n") unless first
  first = false

  function_name = ([name] + operands.map { |_, encoding| encoding.to_s.gsub('rm_', '') }).join('_')

  param_list = operands.map { |param_name, encoding| "#{TYPES.fetch(encoding)} #{param_name}"}.join(', ')

  params_by_encoding = operands.map { |param_name, encoding| [encoding, param_name] }.to_h

  reg_param = params_by_encoding[:reg]
  rm_mem_param = params_by_encoding[:rm_mem]
  rm_reg_param = params_by_encoding[:rm_reg]

  imm_param = nil
  imm_size = nil
  imm_unsigned = nil

  operands.each do |param_name, encoding|
    next unless SIZES.keys.include?(encoding)
    imm_param = param_name
    imm_size = SIZES.fetch(encoding)
    imm_unsigned = encoding.to_s.include?('u')
    break
  end

  max_size = 1 + opcode.length + (reg_param || rm_mem_param || rm_reg_param ? 1 : 0) + (rm_mem_param ? 5 : 0) + (imm_size || 0)

  reg_or_extension = reg_param || (extension && '0x' + extension.to_s(16)) || 0

  puts("static int #{function_name}(buffer_t* buffer, #{param_list}) {")

  puts("  unsigned char* data = reserve(buffer, #{max_size});")

  puts("  if (data == NULL) {")
  puts("    return 0;")
  puts("  }")

  puts("  const int rex_w = #{rex_w ? 1 : 0};")

  if rm_reg_param
    puts("  const int rex_r = #{reg_param ? "#{reg_param} >> 3u" : 0};")
    puts("  data += encode_rex_r(data, rex_w, rex_r, #{rm_reg_param});")
  elsif rm_mem_param
    puts("  const int rex_r = #{reg_param ? "#{reg_param} >> 3u" : 0};")
    puts("  data += encode_rex_m(data, rex_w, rex_r, #{rm_mem_param});")
  end

  opcode_list = opcode.map { |byte| '0x' + byte.to_s(16) }.join(', ')
  puts("  static const unsigned char opcode[] = {#{opcode_list}};")
  puts("  memcpy(data, opcode, #{opcode.size});")
  puts("  data += #{opcode.size};")

  if rm_reg_param
    puts("  data += encode_mod_reg_rm_r(data, #{reg_or_extension}, #{rm_reg_param});")
  elsif rm_mem_param
    puts("  data += encode_mod_reg_rm_m(data, #{reg_or_extension}, #{rm_mem_param});")
  end

  if imm_param
    if imm_unsigned
      puts("  serialize_operand_le(data, #{imm_param}, #{imm_size});")
    else
      puts("  copy_displacement(data, #{imm_param}, #{imm_size});")
    end
    puts("  data += #{imm_size};")
  end

  puts("  resize(buffer, data);")

  puts("  return 1;")

  puts("}")
end
