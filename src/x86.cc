/*
 Copyright (C) 2019 Andrew Sveikauskas

 Permission to use, copy, modify, and distribute this software for any
 purpose with or without fee is hereby granted, provided that the above
 copyright notice and this permission notice appear in all copies.
*/

#include <dbg/dbg.h>
#include <dbg/arch.h>

#include <udis86.h>

#include <string.h>

static void
set_mode(ud_t *ud)
{
   switch (sizeof(void*))
   {
   case 4:
      ud_set_mode(ud, 32);
      break;
   case 8:
      ud_set_mode(ud, 64);
      break;
   }
}

int
dbg::Cpu::GetInstructionLength(const void *text, int len)
{
   ud_t ud;

   ud_init(&ud);
   set_mode(&ud);
   ud_set_input_buffer(&ud, (const uint8_t*)text, len);

   return ud_disassemble(&ud);
}

struct DisasmState
{
   dbg::Debugger *dbg;
   bool eof;
   dbg::addr_t addr;
   size_t offset;
   size_t bufsz;
   unsigned char *buf;
};

static int
input_hook(
   ud_t *ud
)
{
   auto state = (DisasmState*)ud_get_user_opaque_data(ud);
   error err;

   if (state->eof)
      return UD_EOI;

   if (state->offset < state->bufsz)
      goto out;

   state->dbg->ReadMemory(state->addr, state->bufsz, state->buf, &err);
   if (ERROR_FAILED(&err))
      return UD_EOI;

   state->addr += state->bufsz;
   state->offset = 0;
out:
   return state->buf[state->offset++];
}

void
dbg::Cpu::Disassemble(
   Debugger *dbg,
   addr_t text,
   int instrs,
   std::function<void(addr_t addr, const void *instr, int instrlen, const char*, error *err)> callback,
   error *err
)
{
   ud_t ud;
   DisasmState state = {0};
   state.dbg = dbg;
   state.addr = text;
   state.bufsz = dbg->proc->GetBlockSize();
   unsigned char buf[state.bufsz];
   state.buf = buf;
   state.offset = state.bufsz;

   ud_init(&ud);
   set_mode(&ud);

   ud_set_input_hook(&ud, input_hook);
   ud_set_user_opaque_data(&ud, &state);

   ud_set_pc(&ud, text);

   ud_set_syntax(&ud, UD_SYN_INTEL);

   while (instrs < 0 || instrs)
   {
      int instr_len = ud_disassemble(&ud);

      if (instr_len <= 0)
         break;

      callback(
         ud.pc - instr_len,
         ud_insn_ptr(&ud),
         instr_len,
         ud_insn_asm(&ud),
         err
      );
      ERROR_CHECK(err);
      if (instrs)
         --instrs;
   }

exit:;
}

dbg::addr_t
dbg::Cpu::GetPc(
   Process *proc,
   error *err
)
{
   addr_t addr = 0;
   proc->GetRegister(DBG_IP, &addr, err);
   return addr;
}

void
dbg::Cpu::GenerateBreakpoint(
   addr_t pc,
   void *buffer,
   int desiredLen,
   error *err
)
{
   // Fill the buffer with the int3 instruction.
   //
   memset(buffer, 0xcc, desiredLen);
}

int
dbg::Cpu::GetFixedSizedBreakpoint()
{
   return 1;
}

void
dbg::Cpu::OnBreakpointBreak(Process *proc, error *err)
{
   uintptr_t ip = 0;

   proc->GetRegister(DBG_IP, &ip, err);
   ERROR_CHECK(err);

   ip--;

   proc->SetRegister(DBG_IP, &ip, err);
   ERROR_CHECK(err);
exit:;
}

void
dbg::Cpu::StackTrace(
   Debugger *dbg,
   std::function<void(addr_t pc, addr_t frame, bool& cancel, error *err)> callback,
   error *err
)
{
   bool cancel = false;
   addr_t ip = 0, frame = 0, stack = 0;
   ud_t ud;
   unsigned char buf[16];

   // Grab some interesting registers...
   //
   dbg->proc->GetRegister(DBG_IP, &ip, err);
   ERROR_CHECK(err);
   dbg->proc->GetRegister(DBG_BP, &frame, err);
   ERROR_CHECK(err);
   callback(ip, frame, cancel, err);
   ERROR_CHECK(err);
   if (cancel)
      goto exit;

   // Handle some corner cases where the frame pointer is in flux.
   //
   dbg->ReadMemory(ip, sizeof(buf), buf, err);
   ERROR_CHECK(err);

   ud_init(&ud);
   set_mode(&ud);

   ud_set_input_buffer(&ud, buf, sizeof(buf));

   if (!ud_disassemble(&ud))
      ERROR_SET(err, unknown, "Disassemble failed");

   switch (ud.mnemonic)
   {
      // 
      // If the current instruction is "mov ebp, esp", grab the frame
      // pointer from esp.
      // 

   case UD_Imov:

      if (ud.operand[0].type != UD_OP_REG ||
          ud.operand[1].type != UD_OP_REG)
         break;

      if (ud.operand[0].base != UD_R_EBP && ud.operand[0].base != UD_R_RBP)
         break;

      if (ud.operand[1].base != UD_R_ESP && ud.operand[1].base != UD_R_RSP)
         break;

      frame = 0;
      dbg->proc->GetRegister(DBG_SP, &frame, err);
      ERROR_CHECK(err);

      break;

   //
   // If the current instruction is "push ebp" or "ret", grab the
   // previous IP from [esp].
   //

   case UD_Ipush:

      if (ud.operand[0].type != UD_OP_REG)
         break;
      if (ud.operand[0].base != UD_R_EBP && ud.operand[0].base != UD_R_RBP)
         break;

   case UD_Iret:

      dbg->proc->GetRegister(DBG_SP, &stack, err);
      ERROR_CHECK(err);

      ip = 0;

      dbg->ReadMemory(stack, sizeof(void*), &ip, err);
      ERROR_CHECK(err);

      if (ip)
      {
         callback(ip, frame, cancel, err);
         ERROR_CHECK(err);
         if (cancel)
            goto exit;
      }
      break;

   default:
      break;
   }

   // Read the prevous frames...
   //
   while (!cancel && ip && frame)
   {
      void *ptrs[2];

      dbg->ReadMemory(frame, sizeof(ptrs), ptrs, err);
      ERROR_CHECK(err);

      frame = (addr_t)ptrs[0];
      ip = (addr_t)ptrs[1];

      if (ip)
      {
         callback(ip, frame, cancel, err);
         ERROR_CHECK(err);
      }
   }

exit:;
}

