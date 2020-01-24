MU static int <%= function_name %>(assembler_t *assembler, <%= param_list && "#{param_list}, " %>const allocator_t *allocator) {
  (void)allocator;

  unsigned char* code = reserve_assembler_space(assembler, <%= max_size %>);

  if (code == NULL) {
    return 0;
  }

<% if special %>

<%= special.split("\n").map { |line| "  #{line}" }.join("\n") %>

<% else %>

<% if reg || extension || rm_reg || rm_mem %>
  const int rex_w = <%= rex_w ? 1 : 0 %>;
  const int rex_r = <%= reg ? "reg >> 3u" : 0 %>;

<% if rm_reg %>
  code += encode_rex_r(code, rex_w, rex_r, rm_reg);
<% elsif rm_mem %>
  code += encode_rex_m(code, rex_w, rex_r, rm_mem);
<% else %>
  code += encode_rex_r(code, rex_w, rex_r, 0);
<% end %>

<% end %>

  static const unsigned char opcode[] = <%= opcode_literal %>;
  memcpy(code, opcode, sizeof(opcode));
  code += sizeof(opcode);

<% if rm_reg %>
  code += encode_mod_reg_rm_r(code, <%= reg_or_extension %>, rm_reg);
<% elsif rm_mem %>
  code += encode_mod_reg_rm_m(code, <%= reg_or_extension %>, rm_mem);
<% end %>

<% if immediate %>
<% if immediate_unsigned %>
  serialize_operand_le(code, immediate, <%= immediate_size %>);
<% else %>
  copy_displacement(code, immediate, <%= immediate_size %>);
<% end %>

  code += <%= immediate_size %>;
<% end %>

<% end %>

  resize_assembler(assembler, code);

  return 1;
}
