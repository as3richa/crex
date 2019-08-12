MAYBE_UNUSED static int <%= function_name %>(native_code_t* code<%= param_list %>) {
  unsigned char* data = reserve_native_code(code, <%= max_size %>);

  if (data == NULL) {
    return 0;
  }

<% if reg_or_extension || rm_reg_param || rm_mem_param %>
  const int rex_w = <%= rex_w ? 1 : 0 %>;
  const int rex_r = <%= reg_param ? "#{reg_param} >> 3u" : 0 %>;

<% if rm_reg_param %>
  data += encode_rex_r(data, rex_w, rex_r, <%= rm_reg_param %>);
<% elsif rm_mem_param %>
  data += encode_rex_m(data, rex_w, rex_r, <%= rm_mem_param %>);
<% else %>
  (void)rex_r;
<% end %>
<% end %>

  static const unsigned char opcode[] = <%= opcode_literal %>;
  memcpy(data, opcode, sizeof(opcode));
  data += sizeof(opcode);

<% if rm_reg_param %>
  data += encode_mod_reg_rm_r(data, <%= reg_or_extension %>, <%= rm_reg_param %>);
<% elsif rm_mem_param %>
  data += encode_mod_reg_rm_m(data, <%= reg_or_extension %>, <%= rm_mem_param %>);
<% end %>

<% if imm_param %>
<% if imm_unsigned %>
  serialize_operand_le(data, <%= imm_param %>, <%= imm_size %>);
<% else %>
  copy_displacement(data, <%= imm_param %>, <%= imm_size %>);
<% end %>
  data += <%= imm_size %>;
<% end %>

  resize(code, data);

  return 1;
}
