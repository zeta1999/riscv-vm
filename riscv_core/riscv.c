#include <assert.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

#if RISCV_VM_SUPPORT_RV32F
#include <math.h>
#endif

#include "riscv.h"
#include "riscv_private.h"


// get a pointer to a CSR
static uint32_t *csr_get_ptr(struct riscv_t *rv, uint32_t csr) {
  switch (csr) {
  case CSR_CYCLE:
    return (uint32_t*)(&rv->csr_cycle) + 0;
  case CSR_CYCLEH:
    return (uint32_t*)(&rv->csr_cycle) + 1;
  case CSR_MSTATUS:
    return (uint32_t*)(&rv->csr_mstatus);
#if RISCV_VM_SUPPORT_RV32F
  case CSR_FCSR:
    return (uint32_t*)(&rv->csr_fcsr);
#endif
  default:
    return NULL;
  }
}

static bool csr_is_writable(uint32_t csr) {
  switch (csr) {
  case CSR_MSTATUS:
    return true;
  case CSR_CYCLE:
  case CSR_CYCLEH:
  case CSR_FCSR:
  default:
    return false;
  }
}

// perform csrrw
static uint32_t csr_csrrw(struct riscv_t *rv, uint32_t csr, uint32_t val) {
  uint32_t *c = csr_get_ptr(rv, csr);
  if (!c) {
    return 0;
  }
  const uint32_t out = *c;
  if (csr_is_writable(csr)) {
    *c = val;
  }
  return out;
}

// perform csrrs (atomic read and set)
static uint32_t csr_csrrs(struct riscv_t *rv, uint32_t csr, uint32_t val) {
  uint32_t *c = csr_get_ptr(rv, csr);
  if (!c) {
    return 0;
  }
  const uint32_t out = *c;
  if (csr_is_writable(csr)) {
    *c |= val;
  }
  return out;
}

// perform csrrc (atomic read and clear)
static uint32_t csr_csrrc(struct riscv_t *rv, uint32_t csr, uint32_t val) {
  uint32_t *c = csr_get_ptr(rv, csr);
  if (!c) {
    return 0;
  }
  const uint32_t out = *c;
  if (csr_is_writable(csr)) {
    *c &= ~val;
  }
  return out;
}

// raise an exception in the processor
static inline void raise_exception(struct riscv_t *rv, uint32_t type) {
  rv->exception = type;
}

static bool op_load(struct riscv_t *rv, uint32_t inst) {
  // itype format
  const int32_t  imm    = dec_itype_imm(inst);
  const uint32_t rs1    = dec_rs1(inst);
  const uint32_t funct3 = dec_funct3(inst);
  const uint32_t rd     = dec_rd(inst);
  // load address
  const uint32_t addr = rv->X[rs1] + imm;
  // dispatch by read size
  switch (funct3) {
  case 0: // LB
    rv->X[rd] = sign_extend_b(rv->io.mem_read_b(rv, addr));
    break;
  case 1: // LH
    rv->X[rd] = sign_extend_h(rv->io.mem_read_s(rv, addr));
    break;
  case 2: // LW
    rv->X[rd] = rv->io.mem_read_w(rv, addr);
    break;
  case 4: // LBU
    rv->X[rd] = rv->io.mem_read_b(rv, addr);
    break;
  case 5: // LHU
    rv->X[rd] = rv->io.mem_read_s(rv, addr);
    break;
  default:
    assert(!"unreachable");
    break;
  }
  // step over instruction
  rv->PC += 4;
  // enforce zero register
  if (rd == rv_reg_zero) {
    rv->X[rv_reg_zero] = 0;
  }
  return true;
}

#if RISCV_VM_SUPPORT_Zifencei
static bool op_misc_mem(struct riscv_t *rv, uint32_t inst) {
  // TODO
  rv->PC += 4;
  return true;
}
#else
#define op_misc_mem NULL
#endif  // RISCV_VM_SUPPORT_Zifencei

static bool op_op_imm(struct riscv_t *rv, uint32_t inst) {
  // i-type decode
  const int32_t  imm    = dec_itype_imm(inst);
  const uint32_t rd     = dec_rd(inst);
  const uint32_t rs1    = dec_rs1(inst);
  const uint32_t funct3 = dec_funct3(inst);
  // dispatch operation type
  switch (funct3) {
  case 0: // ADDI
    rv->X[rd] = (int32_t)(rv->X[rs1]) + imm;
    break;
  case 1: // SLLI
    rv->X[rd] = rv->X[rs1] << (imm & 0x1f);
    break;
  case 2: // SLTI
    rv->X[rd] = ((int32_t)(rv->X[rs1]) < imm) ? 1 : 0;
    break;
  case 3: // SLTIU
    rv->X[rd] = (rv->X[rs1] < (uint32_t)imm) ? 1 : 0;
    break;
  case 4: // XORI
    rv->X[rd] = rv->X[rs1] ^ imm;
    break;
  case 5:
    if (imm & ~0x1f) {
      // SRAI
      rv->X[rd] = ((int32_t)rv->X[rs1]) >> (imm & 0x1f);
    }
    else {
      // SRLI
      rv->X[rd] = rv->X[rs1] >> (imm & 0x1f);
    }
    break;
  case 6: // ORI
    rv->X[rd] = rv->X[rs1] | imm;
    break;
  case 7: // ANDI
    rv->X[rd] = rv->X[rs1] & imm;
    break;
  default:
    assert(!"unreachable");
    break;
  }
  // step over instruction
  rv->PC += 4;
  // enforce zero register
  if (rd == rv_reg_zero) {
    rv->X[rv_reg_zero] = 0;
  }
  return true;
}

// add upper immediate to pc
static bool op_auipc(struct riscv_t *rv, uint32_t inst) {
  // u-type decode
  const uint32_t rd  = dec_rd(inst);
  const uint32_t val = dec_utype_imm(inst) + rv->PC;
  rv->X[rd] = val;
  // step over instruction
  rv->PC += 4;
  // enforce zero register
  if (rd == rv_reg_zero) {
    rv->X[rv_reg_zero] = 0;
  }
  return true;
}

static bool op_store(struct riscv_t *rv, uint32_t inst) {
  // s-type format
  const int32_t  imm    = dec_stype_imm(inst);
  const uint32_t rs1    = dec_rs1(inst);
  const uint32_t rs2    = dec_rs2(inst);
  const uint32_t funct3 = dec_funct3(inst);
  // store address
  const uint32_t addr = rv->X[rs1] + imm;
  const uint32_t data = rv->X[rs2];
  // dispatch by write size
  switch (funct3) {
  case 0: // SB
    rv->io.mem_write_b(rv, addr, data);
    break;
  case 1: // SH
    rv->io.mem_write_s(rv, addr, data);
    break;
  case 2: // SW
    rv->io.mem_write_w(rv, addr, data);
    break;
  default:
    assert(!"unreachable");
    break;
  }
  // step over instruction
  rv->PC += 4;
  return true;
}

static bool op_op(struct riscv_t *rv, uint32_t inst) {
  // r-type decode
  const uint32_t rd     = dec_rd(inst);
  const uint32_t funct3 = dec_funct3(inst);
  const uint32_t rs1    = dec_rs1(inst);
  const uint32_t rs2    = dec_rs2(inst);
  const uint32_t funct7 = dec_funct7(inst);

  switch (funct7) {
  case 0b0000000:
    switch (funct3) {
    case 0b000: // ADD
      rv->X[rd] = (int32_t)(rv->X[rs1]) + (int32_t)(rv->X[rs2]);
      break;
    case 0b001: // SLL
      rv->X[rd] = rv->X[rs1] << (rv->X[rs2] & 0x1f);
      break;
    case 0b010: // SLT
      rv->X[rd] = ((int32_t)(rv->X[rs1]) < (int32_t)(rv->X[rs2])) ? 1 : 0;
      break;
    case 0b011: // SLTU
      rv->X[rd] = (rv->X[rs1] < rv->X[rs2]) ? 1 : 0;
      break;
    case 0b100: // XOR
      rv->X[rd] = rv->X[rs1] ^ rv->X[rs2];
      break;
    case 0b101: // SRL
      rv->X[rd] = rv->X[rs1] >> (rv->X[rs2] & 0x1f);
      break;
    case 0b110: // OR
      rv->X[rd] = rv->X[rs1] | rv->X[rs2];
      break;
    case 0b111: // AND
      rv->X[rd] = rv->X[rs1] & rv->X[rs2];
      break;
    default:
      assert(!"unreachable");
      break;
    }
    break;
#if RISCV_VM_SUPPORT_RV32M
  case 0b0000001:
    // RV32M instructions
    switch (funct3) {
    case 0b000: // MUL
      rv->X[rd] = (int32_t)rv->X[rs1] * (int32_t)rv->X[rs2];
      break;
    case 0b001: // MULH
      {
        const int64_t a = (int32_t)rv->X[rs1];
        const int64_t b = (int32_t)rv->X[rs2];
        rv->X[rd] = ((uint64_t)(a * b)) >> 32;
      }
      break;
    case 0b010: // MULHSU
      {
        const int64_t a = (int32_t)rv->X[rs1];
        const uint64_t b = rv->X[rs2];
        rv->X[rd] = ((uint64_t)(a * b)) >> 32;
      }
      break;
    case 0b011: // MULHU
      rv->X[rd] = ((uint64_t)rv->X[rs1] * (uint64_t)rv->X[rs2]) >> 32;
      break;
    case 0b100: // DIV
      {
        const int32_t dividend = (int32_t)rv->X[rs1];
        const int32_t divisor = (int32_t)rv->X[rs2];
        if (divisor == 0) {
          rv->X[rd] = ~0u;
        }
        else if (divisor == -1 && rv->X[rs1] == 0x80000000u) {
          rv->X[rd] = rv->X[rs1];
        }
        else {
          rv->X[rd] = dividend / divisor;
        }
      }
      break;
    case 0b101: // DIVU
      {
        const uint32_t dividend = rv->X[rs1];
        const uint32_t divisor  = rv->X[rs2];
        if (divisor == 0) {
          rv->X[rd] = ~0u;
        }
        else {
          rv->X[rd] = dividend / divisor;
        }
      }
      break;
    case 0b110: // REM
      {
        const int32_t dividend = rv->X[rs1];
        const int32_t divisor = rv->X[rs2];
        if (divisor == 0) {
          rv->X[rd] = dividend;
        }
        else if (divisor == -1 && rv->X[rs1] == 0x80000000u) {
          rv->X[rd] = 0;
        }
        else {
          rv->X[rd] = dividend % divisor;
        }
      }
      break;
    case 0b111: // REMU
      {
        const uint32_t dividend = rv->X[rs1];
        const uint32_t divisor = rv->X[rs2];
        if (divisor == 0) {
          rv->X[rd] = dividend;
        }
        else {
          rv->X[rd] = dividend % divisor;
        }
      }
      break;
    default:
      assert(!"unreachable");
      break;
    }
    break;
#endif  // RISCV_VM_SUPPORT_RV32M
  case 0b0100000:
    switch (funct3) {
    case 0b000:  // SUB
      rv->X[rd] = (int32_t)(rv->X[rs1]) - (int32_t)(rv->X[rs2]);
      break;
    case 0b101:  // SRA
      rv->X[rd] = ((int32_t)rv->X[rs1]) >> (rv->X[rs2] & 0x1f);
      break;
    default:
      assert(!"unreachable");
      break;
    }
    break;
  default:
    assert(!"unreachable");
    break;
  }
  // step over instruction
  rv->PC += 4;
  // enforce zero register
  if (rd == rv_reg_zero) {
    rv->X[rv_reg_zero] = 0;
  }
  return true;
}

static bool op_lui(struct riscv_t *rv, uint32_t inst) {
  // u-type decode
  const uint32_t rd  = dec_rd(inst);
  const uint32_t val = dec_utype_imm(inst);
  rv->X[rd] = val;
  // step over instruction
  rv->PC += 4;
  // enforce zero register
  if (rd == rv_reg_zero) {
    rv->X[rv_reg_zero] = 0;
  }
  return true;
}

static bool op_branch(struct riscv_t *rv, uint32_t inst) {
  // b-type decode
  const uint32_t func3 = dec_funct3(inst);
  const int32_t  imm   = dec_btype_imm(inst);
  const uint32_t rs1   = dec_rs1(inst);
  const uint32_t rs2   = dec_rs2(inst);
  // track if branch is taken or not
  bool taken = false;
  // dispatch by branch type
  switch (func3) {
  case 0: // BEQ
    taken = (rv->X[rs1] == rv->X[rs2]);
    break;
  case 1: // BNE
    taken = (rv->X[rs1] != rv->X[rs2]);
    break;
  case 4: // BLT
    taken = ((int32_t)rv->X[rs1] < (int32_t)rv->X[rs2]);
    break;
  case 5: // BGE
    taken = ((int32_t)rv->X[rs1] >= (int32_t)rv->X[rs2]);
    break;
  case 6: // BLTU
    taken = (rv->X[rs1] < rv->X[rs2]);
    break;
  case 7: // BGEU
    taken = (rv->X[rs1] >= rv->X[rs2]);
    break;
  default:
    assert(!"unreachable");
  }
  // perform branch action
  if (taken) {
    rv->PC += imm;
    if (rv->PC & 0x3) {
      raise_exception(rv, rv_except_inst_misaligned);
    }
  }
  else {
    // step over instruction
    rv->PC += 4;
  }
  // can branch
  return false;
}

static bool op_jalr(struct riscv_t *rv, uint32_t inst) {
  // i-type decode
  const uint32_t rd  = dec_rd(inst);
  const uint32_t rs1 = dec_rs1(inst);
  const int32_t  imm = dec_itype_imm(inst);
  // compute return address
  const uint32_t ra = rv->PC + 4;
  // jump
  rv->PC = (rv->X[rs1] + imm) & ~1u;
  // link
  if (rd != rv_reg_zero) {
    rv->X[rd] = ra;
  }
  // check for exception
  if (rv->PC & 0x3) {
    raise_exception(rv, rv_except_inst_misaligned);
  }
  // can branch
  return false;
}

static bool op_jal(struct riscv_t *rv, uint32_t inst) {
  // j-type decode
  const uint32_t rd  = dec_rd(inst);
  const int32_t rel = dec_jtype_imm(inst);
  // compute return address
  const uint32_t ra = rv->PC + 4;
  rv->PC += rel;
  // link
  if (rd != rv_reg_zero) {
    rv->X[rd] = ra;
  }
  // check alignment of PC
  if (rv->PC & 0x3) {
    raise_exception(rv, rv_except_inst_misaligned);
  }
  // can branch
  return false;
}

static bool op_system(struct riscv_t *rv, uint32_t inst) {
  // i-type decode
  const int32_t  imm    = dec_itype_imm(inst);
  const int32_t  csr    = dec_csr(inst);
  const uint32_t funct3 = dec_funct3(inst);
  const uint32_t rs1    = dec_rs1(inst);
  const uint32_t rd     = dec_rd(inst);
  // dispatch by func3 field
  switch (funct3) {
  case 0:
    // dispatch from imm field
    switch (imm) {
    case 0: // ECALL
      rv->io.on_ecall(rv, rv->PC, inst);
      break;
    case 1: // EBREAK
      rv->io.on_ebreak(rv, rv->PC, inst);
      break;
    default:
      assert(!"unreachable");
    }
    break;
#if RISCV_VM_SUPPORT_Zicsr
  case 1: // CSRRW    (Atomic Read/Write CSR)
    rv->X[rd] = csr_csrrw(rv, csr, rs1);
    break;
  case 2: // CSRRS    (Atomic Read and Set Bits in CSR)
    rv->X[rd] = csr_csrrs(rv, csr, rs1);
    break;
  case 3: // CSRRC    (Atomic Read and Clear Bits in CSR)
    rv->X[rd] = csr_csrrc(rv, csr, rs1);
    break;
  case 5: // CSRRWI
  case 6: // CSRRSI
  case 7: // CSRRCI
    // TODO
    break;
#endif  // RISCV_VM_SUPPORT_Zicsr
  default:
    assert(!"unreachable");
  }
  // step over instruction
  rv->PC += 4;
  // enforce zero register
  if (rd == rv_reg_zero) {
    rv->X[rv_reg_zero] = 0;
  }
  return true;
}

#if RISCV_VM_SUPPORT_RV32A
static bool op_amo(struct riscv_t *rv, uint32_t inst) {
  const uint32_t rd     = dec_rd(inst);
  const uint32_t rs1    = dec_rs1(inst);
  const uint32_t rs2    = dec_rs2(inst);
  const uint32_t f7     = dec_funct7(inst);
  const uint32_t rl     = (f7 >> 0) & 1;
  const uint32_t aq     = (f7 >> 1) & 1;
  const uint32_t funct5 = (f7 >> 2) & 0x1f;

  switch (funct5) {
  case 0b00010:  // LR.W
    rv->X[rd] = rv->io.mem_read_w(rv, rv->X[rs1]);
    // skip registration of the 'reservation set'
    // TODO: implement me
    break;
  case 0b00011:  // SC.W
    // we assume the 'reservation set' is valid
    // TODO: implement me
    rv->io.mem_write_w(rv, rv->X[rs1], rv->X[rs2]);
    rv->X[rd] = 0;
    break;
  case 0b00001:  // AMOSWAP.W
    {
      rv->X[rd] = rv->io.mem_read_w(rv, rs1);
      rv->io.mem_write_s(rv, rs1, rv->X[rs2]);
      break;
    }
  case 0b00000:  // AMOADD.W
    {
      rv->X[rd] = rv->io.mem_read_w(rv, rs1);
      const int32_t res = (int32_t)rv->X[rd] + (int32_t)rv->X[rs2];
      rv->io.mem_write_s(rv, rs1, res);
      break;
    }
  case 0b00100:  // AMOXOR.W
    {
      rv->X[rd] = rv->io.mem_read_w(rv, rs1);
      const int32_t res = rv->X[rd] ^ rv->X[rs2];
      rv->io.mem_write_s(rv, rs1, res);
      break;
    }
  case 0b01100:  // AMOAND.W
    {
      rv->X[rd] = rv->io.mem_read_w(rv, rs1);
      const int32_t res = rv->X[rd] & rv->X[rs2];
      rv->io.mem_write_s(rv, rs1, res);
      break;
    }
  case 0b01000:  // AMOOR.W
    {
      rv->X[rd] = rv->io.mem_read_w(rv, rs1);
      const int32_t res = rv->X[rd] | rv->X[rs2];
      rv->io.mem_write_s(rv, rs1, res);
      break;
    }
  case 0b10000:  // AMOMIN.W
    {
      rv->X[rd] = rv->io.mem_read_w(rv, rs1);
      const int32_t a = rv->X[rd];
      const int32_t b = rv->X[rs2];
      const int32_t res = a < b ? a : b;
      rv->io.mem_write_s(rv, rs1, res);
      break;
    }
  case 0b10100:  // AMOMAX.W
    {
      rv->X[rd] = rv->io.mem_read_w(rv, rs1);
      const int32_t a = rv->X[rd];
      const int32_t b = rv->X[rs2];
      const int32_t res = a > b ? a : b;
      rv->io.mem_write_s(rv, rs1, res);
      break;
    }
  case 0b11000:  // AMOMINU.W
    {
      rv->X[rd] = rv->io.mem_read_w(rv, rs1);
      const uint32_t a = rv->X[rd];
      const uint32_t b = rv->X[rs2];
      const uint32_t res = a < b ? a : b;
      rv->io.mem_write_s(rv, rs1, res);
      break;
    }
  case 0b11100:  // AMOMAXU.W
    {
      rv->X[rd] = rv->io.mem_read_w(rv, rs1);
      const uint32_t a = rv->X[rd];
      const uint32_t b = rv->X[rs2];
      const uint32_t res = a > b ? a : b;
      rv->io.mem_write_s(rv, rs1, res);
      break;
    }
  default:
    assert(!"unreachable");
  }
  // step over instruction
  rv->PC += 4;
  // enforce zero register
  if (rd == rv_reg_zero) {
    rv->X[rv_reg_zero] = 0;
  }
  return true;
}
#else
#define op_amo NULL
#endif  // RISCV_VM_SUPPORT_RV32A

#if RISCV_VM_SUPPORT_RV32F
enum {
  //             ....xxxx....xxxx....xxxx....xxxx
  FMASK_SIGN = 0b10000000000000000000000000000000,
  FMASK_EXPN = 0b01111111100000000000000000000000,
  FMASK_FRAC = 0b00000000011111111111111111111111,
  //             ........xxxxxxxx........xxxxxxxx
};

static uint32_t calc_fclass(uint32_t f) {
  const uint32_t sign = f & FMASK_SIGN;
  const uint32_t expn = f & FMASK_EXPN;
  const uint32_t frac = f & FMASK_FRAC;

  // note: this could be turned into a binary decision tree for speed

  uint32_t out = 0;
  // 0x001    rs1 is -INF
  out |= (f == 0xff800000)                               ? 0x001 : 0;
  // 0x002    rs1 is negative normal
  out |= (expn && expn < 0x78000000 && sign)             ? 0x002 : 0;
  // 0x004    rs1 is negative subnormal
  out |= (!expn && frac && sign)                         ? 0x004 : 0;
  // 0x008    rs1 is -0
  out |= (f == 0x80000000)                               ? 0x008 : 0;
  // 0x010    rs1 is +0
  out |= (f == 0x00000000)                               ? 0x010 : 0;
  // 0x020    rs1 is positive subnormal
  out |= (!expn && frac && !sign)                        ? 0x020 : 0;
  // 0x040    rs1 is positive normal
  out |= (expn && expn < 0x78000000 && !sign)            ? 0x040 : 0;
  // 0x080    rs1 is +INF
  out |= (f == 0x7f800000)                               ? 0x080 : 0;
  // 0x100    rs1 is a signaling NaN
  out |= (expn == FMASK_EXPN && (frac <= 0x7ff) && frac) ? 0x100 : 0;
  // 0x200    rs1 is a quiet NaN
  out |= (expn == FMASK_EXPN && (frac >= 0x800))         ? 0x200 : 0;

  return out;
}

bool op_load_fp(struct riscv_t *rv, uint32_t inst) {
  const uint32_t rd  = dec_rd(inst);
  const uint32_t rs1 = dec_rs1(inst);
  const int32_t imm = dec_itype_imm(inst);
  // calculate load address
  const uint32_t addr = rv->X[rs1] + imm;
  // copy into the float register
  const uint32_t data = rv->io.mem_read_w(rv, addr);
  memcpy(rv->F + rd, &data, 4);
  // step over instruction
  rv->PC += 4;
  return true;
}

bool op_store_fp(struct riscv_t *rv, uint32_t inst) {
  const uint32_t rs1 = dec_rs1(inst);
  const uint32_t rs2 = dec_rs2(inst);
  const int32_t imm = dec_stype_imm(inst);
  // calculate store address
  const uint32_t addr = rv->X[rs1] + imm;
  // copy from float registers
  uint32_t data;
  memcpy(&data, (const void*)(rv->F + rs2), 4);
  rv->io.mem_write_w(rv, addr, data);
  // step over instruction
  rv->PC += 4;
  return true;
}

bool op_fp(struct riscv_t *rv, uint32_t inst) {
  const uint32_t rd     = dec_rd(inst);
  const uint32_t rs1    = dec_rs1(inst);
  const uint32_t rs2    = dec_rs2(inst);
  const uint32_t rm     = dec_funct3(inst); // TODO: rounding!
  const uint32_t funct7 = dec_funct7(inst);
  // dispatch based on func7 (low 2 bits are width)
  switch (funct7) {
  case 0b0000000:  // FADD
    rv->F[rd] = rv->F[rs1] + rv->F[rs2];
    break;
  case 0b0000100:  // FSUB
    rv->F[rd] = rv->F[rs1] - rv->F[rs2];
    break;
  case 0b0001000:  // FMUL
    rv->F[rd] = rv->F[rs1] * rv->F[rs2];
    break;
  case 0b0001100:  // FDIV
    rv->F[rd] = rv->F[rs1] / rv->F[rs2];
    break;
  case 0b0101100:  // FSQRT
    rv->F[rd] = sqrtf(rv->F[rs1]);
    break;
  case 0b0010000:
  {
    uint32_t f1, f2, res;
    memcpy(&f1, rv->F + rs1, 4);
    memcpy(&f2, rv->F + rs2, 4);
    switch (rm) {
    case 0b000:  // FSGNJ.S
      res = (f1 & ~FMASK_SIGN) | (f2 & FMASK_SIGN);
      break;
    case 0b001:  // FSGNJN.S
      res = (f1 & ~FMASK_SIGN) | (~f2 & FMASK_SIGN);
      break;
    case 0b010:  // FSGNJX.S
      res = f1 ^ (f2 & FMASK_SIGN);
      break;
    default:
      assert(!"unreachable");
    }
    memcpy(rv->F + rd, &res, 4);
    break;
  }
  case 0b0010100:
    switch (rm) {
    case 0b000:  // FMIN
      rv->F[rd] = fminf(rv->F[rs1], rv->F[rs2]);
      break;
    case 0b001:  // FMAX
      rv->F[rd] = fmaxf(rv->F[rs1], rv->F[rs2]);
      break;
    default:
      assert(!"unreachable");
    }
    break;
  case 0b1100000:
    switch (rs2) {
    case 0b00000:  // FCVT.W.S
      rv->X[rd] = (int32_t)rv->F[rs1];
      break;
    case 0b00001:  // FCVT.WU.S
      rv->X[rd] = (uint32_t)rv->F[rs1];
      break;
    default:
      assert(!"unreachable");
    }
    break;
  case 0b1110000:
    switch (rm) {
    case 0b000:  // FMV.X.W
      // bit exact copy between register files
      memcpy(rv->X + rd, rv->F + rs1, 4);
      break;
    case 0b001:  // FCLASS.S
      {
        uint32_t bits;
        memcpy(&bits, rv->F + rs1, 4);
        rv->X[rd] = calc_fclass(bits);
        break;
      }
    default:
      assert(!"unreachable");
    }
    break;
  case 0b1010000:
    switch (rm) {
    case 0b010:  // FEQ.S
      rv->X[rd] = (rv->F[rs1] == rv->F[rs2]) ? 1 : 0;
      break;
    case 0b001:  // FLT.S
      rv->X[rd] = (rv->F[rs1] < rv->F[rs2]) ? 1 : 0;
      break;
    case 0b000:  // FLE.S
      rv->X[rd] = (rv->F[rs1] <= rv->F[rs2]) ? 1 : 0;
      break;
    default:
      assert(!"unreachable");
    }
    break;
  case 0b1101000:
    switch (rs2) {
    case 0b00000:  // FCVT.S.W
      rv->F[rd] = (float)(int32_t)rv->X[rs1];
      break;
    case 0b00001:  // FCVT.S.WU
      rv->F[rd] = (float)(uint32_t)rv->X[rs1];
      break;
    default:
      assert(!"unreachable");
    }
    break;
  case 0b1111000:  // FMV.W.X
    // bit exact copy between register files
    memcpy(rv->F + rd, rv->X + rs1, 4);
    break;
  default:
    assert(!"unreachable");
  }
  // step over instruction
  rv->PC += 4;
  return true;
}

bool op_madd(struct riscv_t *rv, uint32_t inst) {
  const uint32_t rd  = dec_rd(inst);
  const uint32_t rm  = dec_funct3(inst);      // todo
  const uint32_t rs1 = dec_rs1(inst);
  const uint32_t rs2 = dec_rs2(inst);
  const uint32_t fmt = dec_r4type_fmt(inst);  // unused
  const uint32_t rs3 = dec_r4type_rs3(inst);
  // compute
  rv->F[rd] = rv->F[rs1] * rv->F[rs2] + rv->F[rs3];
  // step over instruction
  rv->PC += 4;
  return true;
}

bool op_msub(struct riscv_t *rv, uint32_t inst) {
  const uint32_t rd  = dec_rd(inst);
  const uint32_t rm  = dec_funct3(inst);      // todo
  const uint32_t rs1 = dec_rs1(inst);
  const uint32_t rs2 = dec_rs2(inst);
  const uint32_t fmt = dec_r4type_fmt(inst);  // unused
  const uint32_t rs3 = dec_r4type_rs3(inst);
  // compute
  rv->F[rd] = rv->F[rs1] * rv->F[rs2] - rv->F[rs3];
  // step over instruction
  rv->PC += 4;
  return true;
}

bool op_nmsub(struct riscv_t *rv, uint32_t inst) {
  const uint32_t rd  = dec_rd(inst);
  const uint32_t rm  = dec_funct3(inst);      // todo
  const uint32_t rs1 = dec_rs1(inst);
  const uint32_t rs2 = dec_rs2(inst);
  const uint32_t fmt = dec_r4type_fmt(inst);  // unused
  const uint32_t rs3 = dec_r4type_rs3(inst);
  // compute
  rv->F[rd] = -(rv->F[rs1] * rv->F[rs2]) + rv->F[rs3];
  // step over instruction
  rv->PC += 4;
  return true;
}

bool op_nmadd(struct riscv_t *rv, uint32_t inst) {
  const uint32_t rd  = dec_rd(inst);
  const uint32_t rm  = dec_funct3(inst);      // todo
  const uint32_t rs1 = dec_rs1(inst);
  const uint32_t rs2 = dec_rs2(inst);
  const uint32_t fmt = dec_r4type_fmt(inst);  // unused
  const uint32_t rs3 = dec_r4type_rs3(inst);
  // compute
  rv->F[rd] = -(rv->F[rs1] * rv->F[rs2]) - rv->F[rs3];
  // step over instruction
  rv->PC += 4;
  return true;
}
#else
#define op_load_fp  NULL
#define op_store_fp NULL
#define op_fp       NULL
#define op_madd     NULL
#define op_msub     NULL
#define op_nmsub    NULL
#define op_nmadd    NULL
#endif  // RISCV_VM_SUPPORT_RV32F

// opcode handler type
typedef bool(*opcode_t)(struct riscv_t *rv, uint32_t inst);

// opcode dispatch table
static const opcode_t opcodes[] = {
//  000        001          010       011          100        101       110   111
    op_load,   op_load_fp,  NULL,     op_misc_mem, op_op_imm, op_auipc, NULL, NULL, // 00
    op_store,  op_store_fp, NULL,     op_amo,      op_op,     op_lui,   NULL, NULL, // 01
    op_madd,   op_msub,     op_nmsub, op_nmadd,    op_fp,     NULL,     NULL, NULL, // 10
    op_branch, op_jalr,     NULL,     op_jal,      op_system, NULL,     NULL, NULL, // 11
};

struct riscv_t *rv_create(const struct riscv_io_t *io, riscv_user_t userdata) {
  assert(io);
  struct riscv_t *rv = (struct riscv_t *)malloc(sizeof(struct riscv_t));
  memset(rv, 0, sizeof(struct riscv_t));
  // copy over the IO interface
  memcpy(&rv->io, io, sizeof(struct riscv_io_t));
  // copy over the userdata
  rv->userdata = userdata;
  // reset
  rv_reset(rv, 0u);

#if RISCV_VM_X64_JIT
  // initalize jit engine
  rv_init_jit(rv);
#endif

  return rv;
}

#if RISCV_VM_X64_JIT
void rv_step(struct riscv_t *rv, int32_t cycles) {
  assert(rv);

  const uint64_t cycles_start = rv->csr_cycle;
  const uint64_t cycles_target = rv->csr_cycle + cycles;

  while (rv->csr_cycle < cycles_target && !rv->exception) {

    // ask the jit engine to execute
    if (rv_step_jit(rv, cycles_target)) {
      continue;
    }

    // emulate until we hit a branch
    while (rv->csr_cycle < cycles_target && !rv->exception) {
      // fetch the next instruction
      const uint32_t inst = rv->io.mem_ifetch(rv, rv->PC);
      const uint32_t index = (inst & INST_6_2) >> 2;
      // dispatch this opcode
      const opcode_t op = opcodes[index];
      assert(op);
      if (!op(rv, inst)) {
        break;
      }
      // increment the cycles csr
      rv->csr_cycle++;
    }
  }
}
#else
void rv_step(struct riscv_t *rv, int32_t cycles) {
  assert(rv);

  const uint64_t cycles_start = rv->csr_cycle;
  const uint64_t cycles_target = rv->csr_cycle + cycles;

  while (rv->csr_cycle < cycles_target && !rv->exception) {

    // fetch the next instruction
    const uint32_t inst = rv->io.mem_ifetch(rv, rv->PC);
    const uint32_t index = (inst & INST_6_2) >> 2;
    // dispatch this opcode
    const opcode_t op = opcodes[index];
    assert(op);
    if (!op(rv, inst)) {
      break;
    }
    // increment the cycles csr
    rv->csr_cycle++;
  }
}
#endif

void rv_delete(struct riscv_t *rv) {
  assert(rv);
  free(rv);
  return;
}

void rv_reset(struct riscv_t *rv, riscv_word_t pc) {
  assert(rv);
  memset(rv->X, 0, sizeof(uint32_t) * RV_NUM_REGS);
  // set the reset address
  rv->PC = pc;
  // set the default stack pointer
  rv->X[rv_reg_sp] = DEFAULT_STACK_ADDR;
  // reset exception state
  rv->exception = rv_except_none;
  // reset the csrs
  rv->csr_cycle = 0;
  rv->csr_mstatus = 0;
  // reset float registers
#if RISCV_VM_SUPPORT_RV32F
  memset(rv->F, 0, sizeof(float) * RV_NUM_REGS);
  rv->csr_fcsr = 0;
#endif
}
