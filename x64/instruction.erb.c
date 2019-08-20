MAYBE_UNUSED static int <%= function_name %>(assembler_t* assembler<%= param_list %>) {
  unsigned char* data = reserve_native_code(assembler, <%= max_size %>);

  if (data == NULL) {
    return 0;
  }

<% if special %>

<%= special.split("\n").map { |line| "  #{line}" }.join("\n") %>

<% else %>

<% if reg || extension || rm_reg || rm_mem %>
  const int rex_w = <%= rex_w ? 1 : 0 %>;
  const int rex_r = <%= reg ? "reg >> 3u" : 0 %>;

<% if rm_reg %>
  data += encode_rex_r(data, rex_w, rex_r, rm_reg);
<% elsif rm_mem %>
  data += encode_rex_m(data, rex_w, rex_r, rm_mem);
<% else %>
  data += encode_rex_r(data, rex_w, rex_r, 0);
<% end %>

<% end %>

  static const unsigned char opcode[] = <%= opcode_literal %>;
  memcpy(data, opcode, sizeof(opcode));
  data += sizeof(opcode);

<% if rm_reg %>
  data += encode_mod_reg_rm_r(data, <%= reg_or_extension %>, rm_reg);
<% elsif rm_mem %>
  data += encode_mod_reg_rm_m(data, <%= reg_or_extension %>, rm_mem);
<% end %>

<% if immediate %>
<% if immediate_unsigned %>
  serialize_operand_le(data, immediate, <%= immediate_size %>);
<% else %>
  copy_displacement(data, immediate, <%= immediate_size %>);
<% end %>

  data += <%= immediate_size %>;
<% end %>

<% end %>

  resize(assembler, data);

  return 1;
}
