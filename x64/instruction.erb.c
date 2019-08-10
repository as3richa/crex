static int <%= function_name %>(buffer_t* buffer, <%= param_list %>) {
  unsigned char* data = reserve(buffer, <%= max_size %>);

  if (data == NULL) {
    return 0;
  }

  const int rex_w = <%= rex_w ? 1 : 0 %>;
  const int rex_r = <%= reg_param ? "#{reg_param} >> 3u" : 0 %>

<% if rm_reg_param %>
  data += encode_rex_r(data, rex_w, rex_r, <%= rm_reg_param %>);
<% elsif rm_mem_param %>
  data += encode_rex_m(data, rex_w, rex_r, M%= rm_mem_param %>);
<% else %>
  (void)rex_r;
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
<% end %>

  data += <%= imm_size %>;
<% end %>

  resize(buffer, data);

  return 1;
}
