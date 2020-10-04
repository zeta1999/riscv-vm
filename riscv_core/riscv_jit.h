#pragma once
#include <stdint.h>
#include <stdio.h>

#define DEBUG_JIT 0

#if DEBUG_JIT
#define JITPRINTF(...) printf(__VA_ARGS__)
#else
#define JITPRINTF(...)
#endif


static void gen_xor_edx_edx(struct block_t *block, struct riscv_t *rv);

static void gen_emit_data(struct block_t *block,
                          struct riscv_t *rv,
                          uint8_t *ptr,
                          uint32_t size) {
  // copy into the code buffer
  memcpy(block->code + block->head, ptr, size);
  block->head += size;
}

static void gen_mov_rax_imm32(struct block_t *block,
                              struct riscv_t *rv,
                              uint32_t imm) {
  JITPRINTF("mov rax, %u\n", imm);
  gen_emit_data(block, rv, "\x48\xc7\xc0", 3);
  gen_emit_data(block, rv, (uint8_t*)&imm, 4);
}

static void gen_mov_rcx_imm32(struct block_t *block,
                              struct riscv_t *rv,
                              uint32_t imm) {
  JITPRINTF("mov rcx, %u\n", imm);
  gen_emit_data(block, rv, "\x48\xc7\xc1", 3);
  gen_emit_data(block, rv, (uint8_t*)&imm, 4);
}

static void gen_mov_rcx_imm64(struct block_t *block,
                              struct riscv_t *rv,
                              uint64_t imm) {
  JITPRINTF("mov r8, %llx\n", imm);
  gen_emit_data(block, rv, "\x48\xb9", 2);
  gen_emit_data(block, rv, (uint8_t*)&imm, 8);
}

static void gen_cmp_rax_rcx(struct block_t *block, struct riscv_t *rv) {
  JITPRINTF("cmp rax, rcx\n");
  gen_emit_data(block, rv, "\x48\x39\xc8", 3);
}

static void gen_xor_rax_rax(struct block_t *block, struct riscv_t *rv) {
  JITPRINTF("xor rax, rax\n");
  gen_emit_data(block, rv, "\x48\x31\xc0", 3);
}

static void gen_cmp_rax_imm32(struct block_t *block,
                              struct riscv_t *rv,
                              uint32_t imm) {
  JITPRINTF("cmp rax, %d\n", (int32_t)imm);
  gen_emit_data(block, rv, "\x48\x3d", 2);
  gen_emit_data(block, rv, (uint8_t*)&imm, 2);
}

static void gen_mov_r8_imm64(struct block_t *block,
                             struct riscv_t *rv,
                             uint64_t imm) {
  JITPRINTF("mov r8, %llx\n", imm);
  gen_emit_data(block, rv, "\x49\xb8", 2);
  gen_emit_data(block, rv, (uint8_t*)&imm, 8);
}

static void gen_mov_r8_imm32(struct block_t *block,
                             struct riscv_t *rv,
                             uint32_t imm) {
  JITPRINTF("mov r8, %02x\n", imm);
  gen_emit_data(block, rv, "\x49\xc7\xc0", 3);
  gen_emit_data(block, rv, (uint8_t*)&imm, 4);
}

static void gen_mov_r9_imm64(struct block_t *block,
                             struct riscv_t *rv,
                             uint64_t imm) {
  JITPRINTF("mov r9, %llx\n", imm);
  gen_emit_data(block, rv, "\x49\xb9", 2);
  gen_emit_data(block, rv, (uint8_t*)&imm, 8);
}

static void gen_call_r9(struct block_t *block, struct riscv_t *rv) {

  // note: this is an often generated code fragment and we should look for
  //       ways not to generate this or optimize it.

  // preserve the original stack frame which seems to be needed by some parts
  // of the MSCV stdlib.  without this i'd get a segfault.  perhaps it has
  // something to do with exception handlers and/or unwinding.
  JITPRINTF("push rbp\n");
  JITPRINTF("mov rbp, rsp\n");
  gen_emit_data(block, rv, "\x55\x48\x89\xE5", 4);
  // the caller must allocate space for 4 arguments on the stack prior to
  // calling.
  JITPRINTF("sub rsp, 32\n");
  gen_emit_data(block, rv, "\x48\x83\xec\x20", 4);
  // execute the call
  JITPRINTF("call r9\n");
  gen_emit_data(block, rv, "\x41\xff\xd1", 3);
  // release the stack space we allocated for temp saves
  JITPRINTF("add rsp, 32\n");
  gen_emit_data(block, rv, "\x48\x83\xc4\x20", 4);
  // pop the stack frame
  JITPRINTF("pop rbp\n");
  gen_emit_data(block, rv, "\x5D", 1);
}

static void gen_add_rdx_imm32(struct block_t *block,
                              struct riscv_t *rv,
                              uint32_t imm) {
  if (imm != 0) {
    JITPRINTF("add rdx, %d\n", (int32_t)imm);
    gen_emit_data(block, rv, "\x48\x81\xc2", 3);
    gen_emit_data(block, rv, (uint8_t*)&imm, 4);
  }
}

static void gen_xor_ecx_ecx(struct block_t *block, struct riscv_t *rv) {
  JITPRINTF("xor ecx, ecx\n");
  gen_emit_data(block, rv, "\x31\xc9", 2);
}

static void gen_mov_ecx_imm32(struct block_t *block,
                              struct riscv_t *rv,
                              uint32_t imm) {
  if (imm == 0) {
    gen_xor_ecx_ecx(block, rv);
  }
  else {
    JITPRINTF("mov ecx, %u\n", imm);
    gen_emit_data(block, rv, "\xb9", 1);
    gen_emit_data(block, rv, (uint8_t*)&imm, 4);
  }
}

static void gen_mov_eax_rv32pc(struct block_t *block, struct riscv_t *rv) {
  JITPRINTF("mov r11, &rv->PC\n");
  gen_emit_data(block, rv, "\x49\xbb", 2);
  uintptr_t ptr = (uintptr_t)&rv->PC;
  gen_emit_data(block, rv, (uint8_t*)&ptr, 8);
  JITPRINTF("mov eax, [r11]\n");
  gen_emit_data(block, rv, "\x41\x8b\x03", 3);
}

static void gen_mov_rv32pc_eax(struct block_t *block, struct riscv_t *rv) {
  JITPRINTF("mov r11, &rv->PC\n");
  gen_emit_data(block, rv, "\x49\xbb", 2);
  uintptr_t ptr = (uintptr_t)&rv->PC;
  gen_emit_data(block, rv, (uint8_t*)&ptr, 8);
  JITPRINTF("mov [r11], eax\n");
  gen_emit_data(block, rv, "\x41\x89\x03", 3);
}

static void gen_mov_eax_rv32reg(struct block_t *block,
                                struct riscv_t *rv,
                                uint32_t reg) {

  // note: this is an often generated code fragment and we should look for
  //       ways not to generate this or optimize it.

  if (reg == rv_reg_zero) {
    JITPRINTF("xor eax, eax\n");
    gen_emit_data(block, rv, "\x31\xc0", 2);
  }
  else {
    JITPRINTF("mov r11, &rv->X[%u]\n", reg);
    gen_emit_data(block, rv, "\x49\xbb", 2);
    uintptr_t ptr = (uintptr_t)&rv->X[reg];
    gen_emit_data(block, rv, (uint8_t*)&ptr, 8);
    JITPRINTF("mov eax, [r11]\n");
    gen_emit_data(block, rv, "\x41\x8b\x03", 3);
  }
}

static void gen_mov_ecx_rv32reg(struct block_t *block,
                                struct riscv_t *rv,
                                uint32_t reg) {
  if (reg == rv_reg_zero) {
    JITPRINTF("xor ecx, ecx\n");
    gen_emit_data(block, rv, "\x31\xc9", 2);
  }
  else {
    JITPRINTF("mov r11, &rv->PC\n");
    gen_emit_data(block, rv, "\x49\xbb", 2);
    uintptr_t ptr = (uintptr_t)&rv->X[reg];
    gen_emit_data(block, rv, (uint8_t*)&ptr, 8);
    JITPRINTF("mov ecx, [r11]\n");
    gen_emit_data(block, rv, "\x41\x8b\x0b", 3);
  }
}

static void gen_mov_rv32reg_eax(struct block_t *block,
                                struct riscv_t *rv,
                                uint32_t reg) {

  // note: this is currently the most frequently generated instruction fragment
  //       by a hefty margin. should look at ways to not emit it such as
  //       tracking register overwrites somehow.  we could eliminate the mov
  //       into r11 if we keep track of the pointer its loaded with.

  if (reg != rv_reg_zero) {
    JITPRINTF("mov r11, &rv->X[%u]\n", reg);
    gen_emit_data(block, rv, "\x49\xbb", 2);
    uintptr_t ptr = (uintptr_t)&rv->X[reg];
    gen_emit_data(block, rv, (uint8_t*)&ptr, 8);
    JITPRINTF("mov [r11], eax\n");
    gen_emit_data(block, rv, "\x41\x89\x03", 3);
  }
}

static void gen_add_eax_ecx(struct block_t *block, struct riscv_t *rv) {
  JITPRINTF("add eax, ecx\n");
  gen_emit_data(block, rv, "\x01\xc8", 2);
}

static void gen_add_eax_imm32(struct block_t *block,
                              struct riscv_t *rv,
                              uint32_t imm) {
  if (imm != 0) {
    JITPRINTF("add eax, %02x\n", imm);
    gen_emit_data(block, rv, "\x05", 1);
    gen_emit_data(block, rv, (uint8_t*)&imm, 4);
  }
}

static void gen_xor_eax_eax(struct block_t *block, struct riscv_t *rv) {
  JITPRINTF("xor eax, eax\n");
  gen_emit_data(block, rv, "\x31\xc0", 2);
}

static void gen_xor_eax_ecx(struct block_t *block, struct riscv_t *rv) {
  JITPRINTF("xor eax, ecx\n");
  gen_emit_data(block, rv, "\x31\xc8", 2);
}

static void gen_and_eax_ecx(struct block_t *block, struct riscv_t *rv) {
  JITPRINTF("and eax, ecx\n");
  gen_emit_data(block, rv, "\x21\xc8", 2);
}

static void gen_or_eax_ecx(struct block_t *block, struct riscv_t *rv) {
  JITPRINTF("or eax, ecx\n");
  gen_emit_data(block, rv, "\x09\xc8", 2);
}

static void gen_sub_eax_ecx(struct block_t *block, struct riscv_t *rv) {
  JITPRINTF("sub eax, ecx\n");
  gen_emit_data(block, rv, "\x29\xc8", 2);
}

static void gen_xor_eax_imm32(struct block_t *block,
                              struct riscv_t *rv,
                              uint32_t imm) {
  if (imm != 0) {
    JITPRINTF("xor eax, %02x\n", imm);
    gen_emit_data(block, rv, "\x35", 1);
    gen_emit_data(block, rv, (uint8_t*)&imm, 4);
  }
}

static void gen_or_eax_imm32(struct block_t *block,
                             struct riscv_t *rv,
                             uint32_t imm) {
  JITPRINTF("or eax, %02x\n", imm);
  gen_emit_data(block, rv, "\x0d", 1);
  gen_emit_data(block, rv, (uint8_t*)&imm, 4);
}

static void gen_and_eax_imm32(struct block_t *block,
                              struct riscv_t *rv,
                              uint32_t imm) {
  if (imm == 0) {
    gen_xor_eax_eax(block, rv);
  }
  else {
    JITPRINTF("and eax, %02x\n", imm);
    gen_emit_data(block, rv, "\x25", 1);
    gen_emit_data(block, rv, (uint8_t*)&imm, 4);
  }
}

static void gen_cmp_eax_imm32(struct block_t *block,
                              struct riscv_t *rv,
                              uint32_t imm) {
  JITPRINTF("cmp eax, %02x\n", imm);
  gen_emit_data(block, rv, "\x3d", 1);
  gen_emit_data(block, rv, (uint8_t*)&imm, 4);
}

static void gen_mov_eax_imm32(struct block_t *block,
                              struct riscv_t *rv,
                              uint32_t imm) {
  if (imm == 0) {
    gen_xor_eax_eax(block, rv);
  }
  else {
    JITPRINTF("mov eax, %02u\n", imm);
    gen_emit_data(block, rv, "\xb8", 1);
    gen_emit_data(block, rv, (uint8_t*)&imm, 4);
  }
}

static void gen_cmp_eax_ecx(struct block_t *block, struct riscv_t *rv) {
  JITPRINTF("cmp eax, ecx\n");
  gen_emit_data(block, rv, "\x39\xc8", 2);
}

static void gen_mov_rv32pc_r8(struct block_t *block, struct riscv_t *rv) {
  JITPRINTF("mov r11, &rv->PC\n");
  gen_emit_data(block, rv, "\x49\xbb", 2);
  uintptr_t ptr = (uintptr_t)&rv->PC;
  gen_emit_data(block, rv, (uint8_t*)&ptr, 8);
  JITPRINTF("mov [r11], r8\n");
  gen_emit_data(block, rv, "\x4d\x89\x03", 3);
}

static void gen_mov_r8_rv32reg(struct block_t *block,
                               struct riscv_t *rv,
                               uint32_t reg) {
  if (reg == rv_reg_zero) {
    JITPRINTF("xor r8, r8\n");
    gen_emit_data(block, rv, "\x4d\x31\xc0", 3);
  }
  else {
    JITPRINTF("mov r11, &rv->X[%u]\n", reg);
    gen_emit_data(block, rv, "\x49\xbb", 2);
    uintptr_t ptr = (uintptr_t)&rv->X[reg];
    gen_emit_data(block, rv, (uint8_t*)&ptr, 8);
    JITPRINTF("mov r8, [r11]\n");
    gen_emit_data(block, rv, "\x4d\x8b\x03", 3);
  }
}

static void gen_mov_edx_rv32reg(struct block_t *block,
                                struct riscv_t *rv,
                                uint32_t reg) {
  if (reg == rv_reg_zero) {
    JITPRINTF("xor edx, edx\n");
    gen_emit_data(block, rv, "\x31\xd2", 2);
  }
  else {
    JITPRINTF("mov r11, &rv->X[%u]\n", reg);
    gen_emit_data(block, rv, "\x49\xbb", 2);
    uintptr_t ptr = (uintptr_t)&rv->X[reg];
    gen_emit_data(block, rv, (uint8_t*)&ptr, 8);
    JITPRINTF("mov edx, [r11]\n");
    gen_emit_data(block, rv, "\x41\x8b\x13", 3);
  }
}

static void gen_xor_rdx_rdx(struct block_t *block, struct riscv_t *rv) {
  JITPRINTF("xor rdx, rdx\n");
  gen_emit_data(block, rv, "\x48\x31\xd2", 3);
}

static void gen_add_edx_imm32(struct block_t *block,
                              struct riscv_t *rv,
                              uint32_t imm) {
  if (imm != 0) {
    JITPRINTF("add edx, %02x\n", imm);
    gen_emit_data(block, rv, "\x81\xc2", 2);
    gen_emit_data(block, rv, (uint8_t*)&imm, 4);
  }
}

static void gen_and_cl_imm8(struct block_t *block,
                            struct riscv_t *rv,
                            uint8_t imm) {
  JITPRINTF("and cl, %02x\n", (int32_t)imm);
  gen_emit_data(block, rv, "\x80\xe1", 2);
  gen_emit_data(block, rv, &imm, 1);
}

static void gen_shl_eax_cl(struct block_t *block, struct riscv_t *rv) {
  JITPRINTF("shl eax, cl\n");
  gen_emit_data(block, rv, "\xd3\xe0", 2);
}

static void gen_sar_eax_cl(struct block_t *block, struct riscv_t *rv) {
  JITPRINTF("sar eax, cl\n");
  gen_emit_data(block, rv, "\xd3\xf8", 2);
}

static void gen_setb_dl(struct block_t *block, struct riscv_t *rv) {
  JITPRINTF("setb dl\n");
  gen_emit_data(block, rv, "\x0f\x92\xc2", 3);
}

static void gen_setl_dl(struct block_t *block, struct riscv_t *rv) {
  JITPRINTF("setl dl\n");
  gen_emit_data(block, rv, "\x0f\x9c\xc2", 3);
}

static void gen_shr_eax_imm8(struct block_t *block,
                             struct riscv_t *rv,
                             uint8_t imm) {
  if (imm != 0) {
    JITPRINTF("shr eax, %d\n", (int32_t)imm);
    gen_emit_data(block, rv, "\xc1\xe8", 2);
    gen_emit_data(block, rv, &imm, 1);
  }
}

static void gen_sar_eax_imm8(struct block_t *block,
                             struct riscv_t *rv,
                             uint8_t imm) {
  if (imm != 0) {
    JITPRINTF("shr eax, %d\n", (int32_t)imm);
    gen_emit_data(block, rv, "\xc1\xf8", 2);
    gen_emit_data(block, rv, &imm, 1);
  }
}

static void gen_shl_eax_imm8(struct block_t *block,
                             struct riscv_t *rv,
                             uint8_t imm) {
  if (imm != 0) {
    JITPRINTF("shl eax, %d\n", (int32_t)imm);
    gen_emit_data(block, rv, "\xc1\xe0", 2);
    gen_emit_data(block, rv, &imm, 1);
  }
}

static void gen_movsx_eax_al(struct block_t *block, struct riscv_t *rv) {
  JITPRINTF("movsx eax, al\n");
  gen_emit_data(block, rv, "\x0f\xbe\xc0", 3);
}

static void gen_movsx_eax_ax(struct block_t *block, struct riscv_t *rv) {
  JITPRINTF("movsx eax, ax\n");
  gen_emit_data(block, rv, "\x0f\xbf\xc0", 3);
}

static void gen_mov_edx_imm32(struct block_t *block,
                              struct riscv_t *rv,
                              uint32_t imm) {
  if (imm == 0) {
    gen_xor_edx_edx(block, rv);
  }
  else {
    JITPRINTF("mov edx, %02x\n", imm);
    gen_emit_data(block, rv, "\xba", 1);
    gen_emit_data(block, rv, (uint8_t*)&imm, 4);
  }
}

static void gen_cmove_eax_edx(struct block_t *block, struct riscv_t *rv) {
  JITPRINTF("cmove eax, edx\n");
  gen_emit_data(block, rv, "\x0f\x44\xc2", 3);
}

static void gen_cmovne_eax_edx(struct block_t *block, struct riscv_t *rv) {
  JITPRINTF("cmovne eax, edx\n");
  gen_emit_data(block, rv, "\x0f\x45\xc2", 3);
}

static void gen_cmovl_eax_edx(struct block_t *block, struct riscv_t *rv) {
  JITPRINTF("cmovl eax, edx\n");
  gen_emit_data(block, rv, "\x0f\x4c\xc2", 3);
}

static void gen_cmovge_eax_edx(struct block_t *block, struct riscv_t *rv) {
  JITPRINTF("cmovge eax, edx\n");
  gen_emit_data(block, rv, "\x0f\x4d\xc2", 3);
}

static void gen_cmovb_eax_edx(struct block_t *block, struct riscv_t *rv) {
  JITPRINTF("cmovb eax, edx\n");
  gen_emit_data(block, rv, "\x0f\x42\xc2", 3);
}

static void gen_cmovnb_eax_edx(struct block_t *block, struct riscv_t *rv) {
  JITPRINTF("cmovnb eax, edx\n");
  gen_emit_data(block, rv, "\x0f\x43\xc2", 3);
}

static void gen_ret(struct block_t *block, struct riscv_t *rv) {
  JITPRINTF("ret\n");
  gen_emit_data(block, rv, "\xc3", 1);
}

static void gen_xor_edx_edx(struct block_t *block, struct riscv_t *rv) {
  JITPRINTF("xor edx, edx\n");
  gen_emit_data(block, rv, "\x31\xd2", 2);
}

static void gen_mov_eax_edx(struct block_t *block, struct riscv_t *rv) {
  JITPRINTF("mov eax, edx\n");
  gen_emit_data(block, rv, "\x89\xd0", 2);
}

static void gen_shr_eax_cl(struct block_t *block, struct riscv_t *rv) {
  JITPRINTF("shr eax, cl\n");
  gen_emit_data(block, rv, "\xd3\xe8", 2);
}

static void gen_movzx_eax_dl(struct block_t *block, struct riscv_t *rv) {
  JITPRINTF("movzx eax, dl\n");
  gen_emit_data(block, rv, "\x0f\xb6\xc2", 3);
}

static void gen_imul_ecx(struct block_t *block, struct riscv_t *rv) {
  JITPRINTF("imul ecx\n");
  gen_emit_data(block, rv, "\xf7\xe9", 2);
}

static void gen_mul_ecx(struct block_t *block, struct riscv_t *rv) {
  JITPRINTF("mul ecx\n");
  gen_emit_data(block, rv, "\xf7\xe1", 2);
}
