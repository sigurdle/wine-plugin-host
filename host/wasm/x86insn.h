/*
 * THE DECODED INSTRUCTION: the record host/wasm/x86decode.c fills, the constants its fields hold and
 * the list of operations. One description for the interpreter, the recompiler and the disassembler
 * (briefs/review_np_2026-11/x64_pass.md R12); tools/gen-structlayout.mjs gives JS the same offsets.
 *
 * A memory operand is SYMBOLIC: base, index, scale, displacement, segment override and ipRelative.
 * Nothing here is an effective address. A RIP-relative one is `next + disp`, and `next` is known only
 * after the immediate is fetched, so the address is computed by whoever executes, from the record.
 */

#ifndef HOSTFN_X86INSN_H
#define HOSTFN_X86INSN_H

#include "structlayout.h"

typedef unsigned char u8;
typedef unsigned short u16;
typedef long long i64;

/*
 * Register numbers are 0..15 (the encoding, with the REX bit already in). The AH, CH, DH, BH of a
 * byte operand are 16..19, so no reader needs the REX byte to tell SPL from AH. Absent is X86_NONE.
 */
#define GL_X86_INSN(FIELD, FIELDS) \
	FIELD(address,      U64) \
	FIELD(next,         U64) \
	FIELD(imm,          U64) \
	FIELD(imm2,         U64) \
	FIELD(disp,         U64) \
	FIELD(op,           U16) \
	FIELD(length,       U8) \
	FIELD(status,       U8) \
	FIELD(mode,         U8) \
	FIELD(map,          U8) \
	FIELD(opcode,       U8) \
	FIELD(modrm,        U8) \
	FIELD(rex,          U8) \
	FIELD(prefixes,     U8) \
	FIELD(mandatory,    U8) \
	FIELD(segment,      U8) \
	FIELD(form,         U8) \
	FIELD(classes,      U8) \
	FIELD(attrs,        U8) \
	FIELD(opSize,       U8) \
	FIELD(addrSize,     U8) \
	FIELD(memSize,      U8) \
	FIELD(immSize,      U8) \
	FIELD(imm2Size,     U8) \
	FIELD(hasModrm,     U8) \
	FIELD(mod,          U8) \
	FIELD(reg,          U8) \
	FIELD(rm,           U8) \
	FIELD(cc,           U8) \
	FIELD(isMem,        U8) \
	FIELD(memBase,      U8) \
	FIELD(memIndex,     U8) \
	FIELD(memScale,     U8) \
	FIELD(ipRelative,   U8) \
	FIELD(dispSize,     U8) \
	FIELD(flagsRead,    U8) \
	FIELD(flagsWritten, U8)
GL_STRUCT(X86_INSN, host)
typedef struct GL_X86_INSN_host X86Insn;

/*
 * The fields:
 *   address, next   where the instruction is and where the one after it starts
 *   imm, imm2       the immediate, sign-extended to 64 bits (a branch displacement, an ENTER size, the
 *                   offset of a far pointer); imm2 is the ENTER level or the selector of a far pointer
 *   disp            the displacement, sign-extended; the whole address of a moffs operand
 *   op              X86_OP_*
 *   status          X86_DECODE_*; length counts the bytes consumed even when it is not OK
 *   mode            X86_MODE_*
 *   map, opcode     0 one-byte, 1 0F, 2 0F 38, 3 0F 3A; and the last opcode byte
 *   prefixes        X86_PFX_* as written; mandatory is the column the row came from
 *                   (0 none, 1 66, 2 F3, 3 F2)
 *   segment         the override (X86_SEG_*) or X86_NONE; ES, CS, SS, DS are dropped in 64-bit mode
 *   form, classes, attrs   from the table row: X86_FORM_*, reg class | rm class << 4, X86_A_*
 *   opSize          the operand width in bytes (1, 2, 4, 8), 0 where the operation has none
 *   addrSize        2, 4 or 8
 *   memSize         the width of the r/m operand in bytes (0: none or implied by the operation)
 *   mod, reg, rm    the ModRM fields, REX applied; for a register-in-opcode form rm is that register
 *   cc              the condition of JCC, SETCC, CMOVCC
 *   isMem           the r/m operand is memory: memBase, memIndex (X86_NONE when absent), memScale,
 *                   disp, dispSize, ipRelative
 *   flagsRead, flagsWritten   X86_FL_* the operation may read and may write
 */

#define X86_NONE 0xffu
#define X86_REG_AH 16u

#define X86_MODE_32 0u
#define X86_MODE_64 1u

#define X86_DECODE_OK          0u
#define X86_DECODE_INVALID     1u	/* #UD: no such instruction in this mode, or a LOCK it does not take */
#define X86_DECODE_TOO_LONG    2u	/* more than 15 bytes */
#define X86_DECODE_UNSUPPORTED 3u	/* VEX / EVEX: AVX is not decoded yet; the length is not known */
#define X86_DECODE_TRUNCATED   4u	/* the bytes given ended inside the instruction */

#define X86_PFX_OPSIZE   0x01u
#define X86_PFX_ADDRSIZE 0x02u
#define X86_PFX_LOCK     0x04u
#define X86_PFX_REP      0x08u	/* F3 */
#define X86_PFX_REPNE    0x10u	/* F2 */

#define X86_SEG_ES 0u
#define X86_SEG_CS 1u
#define X86_SEG_SS 2u
#define X86_SEG_DS 3u
#define X86_SEG_FS 4u
#define X86_SEG_GS 5u

/* Where the operands are encoded. MR: r/m is the destination, reg the source; RM the reverse. */
#define X86_FORM_INVALID     0u
#define X86_FORM_NONE        1u	/* no ModRM: implied operands, an immediate or a branch target */
#define X86_FORM_MR          2u
#define X86_FORM_RM          3u
#define X86_FORM_M           4u	/* r/m alone; reg extends the opcode */
#define X86_FORM_OPREG       5u	/* the register is the low three bits of the opcode (and REX.B) */
#define X86_FORM_ACC         6u	/* the accumulator and an immediate */
#define X86_FORM_MOFFS_LOAD  7u
#define X86_FORM_MOFFS_STORE 8u
#define X86_FORM_GROUP       9u	/* table only: reg selects the row of a group */
#define X86_FORM_X87         10u	/* table only: D8..DF */
#define X86_FORM_VEX         11u

/* Register classes of the reg and r/m fields. GPRD is a general register that is never a byte one. */
#define X86_RC_NONE 0u
#define X86_RC_GPR  1u
#define X86_RC_GPRD 2u
#define X86_RC_XMM  3u
#define X86_RC_MM   4u
#define X86_RC_SEG  5u
#define X86_RC_CR   6u
#define X86_RC_DR   7u
#define X86_RC_ST   8u

/* Operand-size behavior of a row. */
#define X86_SZ_N   0u	/* none */
#define X86_SZ_B   1u	/* a byte */
#define X86_SZ_V   2u	/* 32; 16 under 66; 64 under REX.W */
#define X86_SZ_Z   3u	/* 32; 16 under 66 */
#define X86_SZ_D64 4u	/* as V, and 64 by default in 64-bit mode (push, pop) */
#define X86_SZ_F64 5u	/* as V, and always 64 in 64-bit mode (near branches) */
#define X86_SZ_Y   6u	/* 32; 64 under REX.W */
#define X86_SZ_W   7u	/* 16 */

/* The immediate of a row. */
#define X86_IMM_NONE  0u
#define X86_IMM_IB    1u
#define X86_IMM_IW    2u
#define X86_IMM_IZ    3u	/* 16 when the operand is 16 bits, else 32 */
#define X86_IMM_IV    4u	/* the operand width, 64 bits included */
#define X86_IMM_IW_IB 5u	/* ENTER */
#define X86_IMM_FAR   6u	/* offset (IZ), then a 16-bit selector */
#define X86_IMM_REL8  7u
#define X86_IMM_RELZ  8u

#define X86_A_LOCK    0x01u	/* takes LOCK with a memory destination */
#define X86_A_REP     0x02u	/* a string operation: F2 / F3 repeat it */
#define X86_A_END     0x04u	/* ends a block: control leaves the straight line */
#define X86_A_I64     0x08u	/* invalid in 64-bit mode */
#define X86_A_O64     0x10u	/* only in 64-bit mode */
#define X86_A_MEMONLY 0x20u
#define X86_A_REGONLY 0x40u
#define X86_A_SPLIT   0x80u	/* a group whose register forms are rows 8..15 */

#define X86_FL_C 0x01u
#define X86_FL_P 0x02u
#define X86_FL_A 0x04u
#define X86_FL_Z 0x08u
#define X86_FL_S 0x10u
#define X86_FL_O 0x20u
#define X86_FL_D 0x40u

/** One table row: what an opcode is and what follows it. */
typedef struct X86Row
{
	u16 op;      /* X86_OP_*; for a GROUP row the index of the group */
	u8 form;     /* X86_FORM_* */
	u8 classes;  /* reg class | rm class << 4 */
	u8 size;     /* X86_SZ_* */
	u8 imm;      /* X86_IMM_* */
	u8 mem;      /* the width of the r/m operand in bytes; 0 is the operand size */
	u8 attrs;    /* X86_A_* */
} X86Row;

/**
 * Decode the instruction at `code`, of which `avail` bytes may be read, as the one at `address`.
 * @returns X86_DECODE_*, also in out->status
 */
u32 x86Decode(const u8 *code, u32 avail, u64 address, u32 mode, X86Insn *out);

/** The name of an operation, upper case, NUL-terminated ("INVALID" for one out of range). */
const char *x86OpName(u32 op);

/*
 * EVERY OPERATION: OP(name, flags read, flags written). The order is the numbering; ADD..CMP,
 * ROL..SAR and each xxPS, xxPD, xxSS, xxSD run are consecutive because the table rows count on it.
 */
#define X86_F4(OP, n) OP(n##PS, 0, 0) OP(n##PD, 0, 0) OP(n##SS, 0, 0) OP(n##SD, 0, 0)
#define X86_OPS(OP) \
	OP(INVALID, 0, 0) \
	OP(ADD, 0, 0x3f) OP(OR, 0, 0x3f) OP(ADC, 1, 0x3f) OP(SBB, 1, 0x3f) OP(AND, 0, 0x3f) OP(SUB, 0, 0x3f) OP(XOR, 0, 0x3f) OP(CMP, 0, 0x3f) \
	OP(ROL, 0, 0x21) OP(ROR, 0, 0x21) OP(RCL, 1, 0x21) OP(RCR, 1, 0x21) OP(SHL, 0, 0x3f) OP(SHR, 0, 0x3f) OP(SAL, 0, 0x3f) OP(SAR, 0, 0x3f) \
	OP(TEST, 0, 0x3f) OP(NOT, 0, 0) OP(NEG, 0, 0x3f) OP(MUL, 0, 0x3f) OP(IMUL, 0, 0x3f) OP(DIV, 0, 0x3f) OP(IDIV, 0, 0x3f) \
	OP(INC, 0, 0x3e) OP(DEC, 0, 0x3e) OP(PUSH, 0, 0) OP(POP, 0, 0) OP(PUSHA, 0, 0) OP(POPA, 0, 0) OP(BOUND, 0, 0) OP(ARPL, 0, 0x08) \
	OP(MOVSXD, 0, 0) OP(JCC, 0x3f, 0) OP(XCHG, 0, 0) OP(MOV, 0, 0) OP(LEA, 0, 0) OP(NOP, 0, 0) OP(PAUSE, 0, 0) OP(CBW, 0, 0) OP(CWD, 0, 0) \
	OP(CALL, 0, 0) OP(CALLI, 0, 0) OP(CALLF, 0, 0) OP(CALLFI, 0, 0) OP(JMP, 0, 0) OP(JMPI, 0, 0) OP(JMPF, 0, 0) OP(JMPFI, 0, 0) \
	OP(RET, 0, 0) OP(RETF, 0, 0) OP(WAIT, 0, 0) OP(PUSHF, 0x7f, 0) OP(POPF, 0, 0x7f) OP(SAHF, 0, 0x1f) OP(LAHF, 0x1f, 0) \
	OP(MOVS, 0x40, 0) OP(CMPS, 0x40, 0x3f) OP(STOS, 0x40, 0) OP(LODS, 0x40, 0) OP(SCAS, 0x40, 0x3f) OP(INS, 0x40, 0) OP(OUTS, 0x40, 0) \
	OP(LES, 0, 0) OP(LDS, 0, 0) OP(LSS, 0, 0) OP(LFS, 0, 0) OP(LGS, 0, 0) OP(ENTER, 0, 0) OP(LEAVE, 0, 0) \
	OP(INT3, 0, 0) OP(INT, 0, 0) OP(INTO, 0x20, 0) OP(INT1, 0, 0) OP(IRET, 0, 0x7f) OP(AAM, 0, 0x3f) OP(AAD, 0, 0x3f) OP(SALC, 1, 0) OP(XLAT, 0, 0) \
	OP(AAA, 0x04, 0x3f) OP(AAS, 0x04, 0x3f) OP(DAA, 0x05, 0x3f) OP(DAS, 0x05, 0x3f) \
	OP(LOOPNE, 0x08, 0) OP(LOOPE, 0x08, 0) OP(LOOP, 0, 0) OP(JCXZ, 0, 0) OP(IN, 0, 0) OP(OUT, 0, 0) OP(HLT, 0, 0) \
	OP(CMC, 1, 1) OP(CLC, 0, 1) OP(STC, 0, 1) OP(CLI, 0, 0) OP(STI, 0, 0) OP(CLD, 0, 0x40) OP(STD, 0, 0x40) \
	OP(PUSHSEG, 0, 0) OP(POPSEG, 0, 0) OP(MOVSEG, 0, 0) OP(SYS0F00, 0, 0x08) OP(SYS0F01, 0, 0) OP(XGETBV, 0, 0) OP(LAR, 0, 0x08) OP(LSL, 0, 0x08) \
	OP(SYSCALL, 0, 0x7f) OP(CLTS, 0, 0) OP(SYSRET, 0, 0x7f) OP(INVD, 0, 0) OP(WBINVD, 0, 0) OP(UD2, 0, 0) OP(UD1, 0, 0) \
	OP(PREFETCHW, 0, 0) OP(PREFETCH, 0, 0) OP(FEMMS, 0, 0) OP(NOW3D, 0, 0) OP(MOVCR, 0, 0x3f) OP(MOVDR, 0, 0x3f) \
	OP(WRMSR, 0, 0) OP(RDTSC, 0, 0) OP(RDMSR, 0, 0) OP(RDPMC, 0, 0) OP(SYSENTER, 0, 0x7f) OP(SYSEXIT, 0, 0) \
	OP(CMOVCC, 0x3f, 0) OP(SETCC, 0x3f, 0) OP(CPUID, 0, 0) OP(BT, 0, 0x3f) OP(BTS, 0, 0x3f) OP(BTR, 0, 0x3f) OP(BTC, 0, 0x3f) \
	OP(SHLD, 0, 0x3f) OP(SHRD, 0, 0x3f) OP(CMPXCHG, 0, 0x3f) OP(CMPXCHG8B, 0, 0x08) OP(MOVZX, 0, 0) OP(MOVSX, 0, 0) \
	OP(POPCNT, 0, 0x3f) OP(BSF, 0, 0x3f) OP(BSR, 0, 0x3f) OP(TZCNT, 0, 0x3f) OP(LZCNT, 0, 0x3f) OP(XADD, 0, 0x3f) OP(MOVNTI, 0, 0) \
	OP(BSWAP, 0, 0) OP(MOVBE, 0, 0) OP(CRC32, 0, 0) OP(RDRAND, 0, 0x3f) OP(RDSEED, 0, 0x3f) \
	OP(FXSAVE, 0, 0) OP(FXRSTOR, 0, 0) OP(LDMXCSR, 0, 0) OP(STMXCSR, 0, 0) OP(XSAVE, 0, 0) OP(XRSTOR, 0, 0) OP(XSAVEOPT, 0, 0) \
	OP(CLFLUSH, 0, 0) OP(LFENCE, 0, 0) OP(MFENCE, 0, 0) OP(SFENCE, 0, 0) OP(VEX, 0, 0) OP(EVEX, 0, 0) \
	/* x87 */ \
	OP(FADD, 0, 0) OP(FMUL, 0, 0) OP(FCOM, 0, 0) OP(FCOMP, 0, 0) OP(FSUB, 0, 0) OP(FSUBR, 0, 0) OP(FDIV, 0, 0) OP(FDIVR, 0, 0) \
	OP(FIADD, 0, 0) OP(FIMUL, 0, 0) OP(FICOM, 0, 0) OP(FICOMP, 0, 0) OP(FISUB, 0, 0) OP(FISUBR, 0, 0) OP(FIDIV, 0, 0) OP(FIDIVR, 0, 0) \
	OP(FADDP, 0, 0) OP(FMULP, 0, 0) OP(FCOMPP, 0, 0) OP(FSUBRP, 0, 0) OP(FSUBP, 0, 0) OP(FDIVRP, 0, 0) OP(FDIVP, 0, 0) \
	OP(FLD, 0, 0) OP(FST, 0, 0) OP(FSTP, 0, 0) OP(FLDENV, 0, 0) OP(FLDCW, 0, 0) OP(FNSTENV, 0, 0) OP(FNSTCW, 0, 0) \
	OP(FILD, 0, 0) OP(FISTTP, 0, 0) OP(FIST, 0, 0) OP(FISTP, 0, 0) OP(FBLD, 0, 0) OP(FBSTP, 0, 0) OP(FRSTOR, 0, 0) OP(FNSAVE, 0, 0) OP(FNSTSW, 0, 0) \
	OP(FXCH, 0, 0) OP(FNOP, 0, 0) OP(FFREE, 0, 0) OP(FFREEP, 0, 0) OP(FUCOM, 0, 0) OP(FUCOMP, 0, 0) OP(FUCOMPP, 0, 0) \
	OP(FCMOVB, 0x0b, 0) OP(FCMOVE, 0x0b, 0) OP(FCMOVBE, 0x0b, 0) OP(FCMOVU, 0x0b, 0) OP(FCMOVNB, 0x0b, 0) OP(FCMOVNE, 0x0b, 0) OP(FCMOVNBE, 0x0b, 0) OP(FCMOVNU, 0x0b, 0) \
	OP(FUCOMI, 0, 0x3f) OP(FCOMI, 0, 0x3f) OP(FUCOMIP, 0, 0x3f) OP(FCOMIP, 0, 0x3f) \
	OP(FNENI, 0, 0) OP(FNDISI, 0, 0) OP(FNCLEX, 0, 0) OP(FNINIT, 0, 0) OP(FNSETPM, 0, 0) \
	OP(FCHS, 0, 0) OP(FABS, 0, 0) OP(FTST, 0, 0) OP(FXAM, 0, 0) OP(FLD1, 0, 0) OP(FLDL2T, 0, 0) OP(FLDL2E, 0, 0) OP(FLDPI, 0, 0) OP(FLDLG2, 0, 0) OP(FLDLN2, 0, 0) OP(FLDZ, 0, 0) \
	OP(F2XM1, 0, 0) OP(FYL2X, 0, 0) OP(FPTAN, 0, 0) OP(FPATAN, 0, 0) OP(FXTRACT, 0, 0) OP(FPREM1, 0, 0) OP(FDECSTP, 0, 0) OP(FINCSTP, 0, 0) \
	OP(FPREM, 0, 0) OP(FYL2XP1, 0, 0) OP(FSQRT, 0, 0) OP(FSINCOS, 0, 0) OP(FRNDINT, 0, 0) OP(FSCALE, 0, 0) OP(FSIN, 0, 0) OP(FCOS, 0, 0) \
	/* MMX, SSE .. SSE4.2 */ \
	OP(MOVUPS, 0, 0) OP(MOVUPD, 0, 0) OP(MOVSS, 0, 0) OP(MOVSD, 0, 0) X86_F4(OP, SQRT) X86_F4(OP, ADD) X86_F4(OP, MUL) X86_F4(OP, SUB) X86_F4(OP, MIN) X86_F4(OP, DIV) X86_F4(OP, MAX) X86_F4(OP, CMP) \
	OP(MOVLPS, 0, 0) OP(MOVLPD, 0, 0) OP(MOVSLDUP, 0, 0) OP(MOVDDUP, 0, 0) OP(MOVHPS, 0, 0) OP(MOVHPD, 0, 0) OP(MOVSHDUP, 0, 0) \
	OP(UNPCKLPS, 0, 0) OP(UNPCKLPD, 0, 0) OP(UNPCKHPS, 0, 0) OP(UNPCKHPD, 0, 0) OP(MOVAPS, 0, 0) OP(MOVAPD, 0, 0) \
	OP(CVTPI2PS, 0, 0) OP(CVTPI2PD, 0, 0) OP(CVTSI2SS, 0, 0) OP(CVTSI2SD, 0, 0) OP(MOVNTPS, 0, 0) OP(MOVNTPD, 0, 0) \
	OP(CVTTPS2PI, 0, 0) OP(CVTTPD2PI, 0, 0) OP(CVTTSS2SI, 0, 0) OP(CVTTSD2SI, 0, 0) OP(CVTPS2PI, 0, 0) OP(CVTPD2PI, 0, 0) OP(CVTSS2SI, 0, 0) OP(CVTSD2SI, 0, 0) \
	OP(UCOMISS, 0, 0x3f) OP(UCOMISD, 0, 0x3f) OP(COMISS, 0, 0x3f) OP(COMISD, 0, 0x3f) OP(MOVMSKPS, 0, 0) OP(MOVMSKPD, 0, 0) \
	OP(RSQRTPS, 0, 0) OP(RSQRTSS, 0, 0) OP(RCPPS, 0, 0) OP(RCPSS, 0, 0) \
	OP(ANDPS, 0, 0) OP(ANDPD, 0, 0) OP(ANDNPS, 0, 0) OP(ANDNPD, 0, 0) OP(ORPS, 0, 0) OP(ORPD, 0, 0) OP(XORPS, 0, 0) OP(XORPD, 0, 0) \
	OP(CVTPS2PD, 0, 0) OP(CVTPD2PS, 0, 0) OP(CVTSS2SD, 0, 0) OP(CVTSD2SS, 0, 0) OP(CVTDQ2PS, 0, 0) OP(CVTPS2DQ, 0, 0) OP(CVTTPS2DQ, 0, 0) \
	OP(PUNPCKLBW, 0, 0) OP(PUNPCKLWD, 0, 0) OP(PUNPCKLDQ, 0, 0) OP(PACKSSWB, 0, 0) OP(PCMPGTB, 0, 0) OP(PCMPGTW, 0, 0) OP(PCMPGTD, 0, 0) OP(PACKUSWB, 0, 0) \
	OP(PUNPCKHBW, 0, 0) OP(PUNPCKHWD, 0, 0) OP(PUNPCKHDQ, 0, 0) OP(PACKSSDW, 0, 0) OP(PUNPCKLQDQ, 0, 0) OP(PUNPCKHQDQ, 0, 0) \
	OP(MOVD, 0, 0) OP(MOVQ, 0, 0) OP(MOVDQA, 0, 0) OP(MOVDQU, 0, 0) OP(PSHUFW, 0, 0) OP(PSHUFD, 0, 0) OP(PSHUFHW, 0, 0) OP(PSHUFLW, 0, 0) \
	OP(PSRLW, 0, 0) OP(PSRAW, 0, 0) OP(PSLLW, 0, 0) OP(PSRLD, 0, 0) OP(PSRAD, 0, 0) OP(PSLLD, 0, 0) OP(PSRLQ, 0, 0) OP(PSRLDQ, 0, 0) OP(PSLLQ, 0, 0) OP(PSLLDQ, 0, 0) \
	OP(PCMPEQB, 0, 0) OP(PCMPEQW, 0, 0) OP(PCMPEQD, 0, 0) OP(EMMS, 0, 0) OP(HADDPD, 0, 0) OP(HADDPS, 0, 0) OP(HSUBPD, 0, 0) OP(HSUBPS, 0, 0) \
	OP(PINSRW, 0, 0) OP(PEXTRW, 0, 0) OP(SHUFPS, 0, 0) OP(SHUFPD, 0, 0) OP(ADDSUBPD, 0, 0) OP(ADDSUBPS, 0, 0) \
	OP(PADDQ, 0, 0) OP(PMULLW, 0, 0) OP(MOVQ2DQ, 0, 0) OP(MOVDQ2Q, 0, 0) OP(PMOVMSKB, 0, 0) OP(PSUBUSB, 0, 0) OP(PSUBUSW, 0, 0) OP(PMINUB, 0, 0) OP(PAND, 0, 0) \
	OP(PADDUSB, 0, 0) OP(PADDUSW, 0, 0) OP(PMAXUB, 0, 0) OP(PANDN, 0, 0) OP(PAVGB, 0, 0) OP(PAVGW, 0, 0) OP(PMULHUW, 0, 0) OP(PMULHW, 0, 0) \
	OP(CVTTPD2DQ, 0, 0) OP(CVTDQ2PD, 0, 0) OP(CVTPD2DQ, 0, 0) OP(MOVNTQ, 0, 0) OP(MOVNTDQ, 0, 0) OP(PSUBSB, 0, 0) OP(PSUBSW, 0, 0) OP(PMINSW, 0, 0) OP(POR, 0, 0) \
	OP(PADDSB, 0, 0) OP(PADDSW, 0, 0) OP(PMAXSW, 0, 0) OP(PXOR, 0, 0) OP(LDDQU, 0, 0) OP(PMULUDQ, 0, 0) OP(PMADDWD, 0, 0) OP(PSADBW, 0, 0) \
	OP(MASKMOVQ, 0, 0) OP(MASKMOVDQU, 0, 0) OP(PSUBB, 0, 0) OP(PSUBW, 0, 0) OP(PSUBD, 0, 0) OP(PSUBQ, 0, 0) OP(PADDB, 0, 0) OP(PADDW, 0, 0) OP(PADDD, 0, 0) \
	OP(PSHUFB, 0, 0) OP(PHADDW, 0, 0) OP(PHADDD, 0, 0) OP(PHADDSW, 0, 0) OP(PMADDUBSW, 0, 0) OP(PHSUBW, 0, 0) OP(PHSUBD, 0, 0) OP(PHSUBSW, 0, 0) \
	OP(PSIGNB, 0, 0) OP(PSIGNW, 0, 0) OP(PSIGND, 0, 0) OP(PMULHRSW, 0, 0) OP(PABSB, 0, 0) OP(PABSW, 0, 0) OP(PABSD, 0, 0) \
	OP(PBLENDVB, 0, 0) OP(BLENDVPS, 0, 0) OP(BLENDVPD, 0, 0) OP(PTEST, 0, 0x3f) \
	OP(PMOVSXBW, 0, 0) OP(PMOVSXBD, 0, 0) OP(PMOVSXBQ, 0, 0) OP(PMOVSXWD, 0, 0) OP(PMOVSXWQ, 0, 0) OP(PMOVSXDQ, 0, 0) \
	OP(PMULDQ, 0, 0) OP(PCMPEQQ, 0, 0) OP(MOVNTDQA, 0, 0) OP(PACKUSDW, 0, 0) \
	OP(PMOVZXBW, 0, 0) OP(PMOVZXBD, 0, 0) OP(PMOVZXBQ, 0, 0) OP(PMOVZXWD, 0, 0) OP(PMOVZXWQ, 0, 0) OP(PMOVZXDQ, 0, 0) \
	OP(PCMPGTQ, 0, 0) OP(PMINSB, 0, 0) OP(PMINSD, 0, 0) OP(PMINUW, 0, 0) OP(PMINUD, 0, 0) OP(PMAXSB, 0, 0) OP(PMAXSD, 0, 0) OP(PMAXUW, 0, 0) OP(PMAXUD, 0, 0) \
	OP(PMULLD, 0, 0) OP(PHMINPOSUW, 0, 0) OP(AESIMC, 0, 0) OP(AESENC, 0, 0) OP(AESENCLAST, 0, 0) OP(AESDEC, 0, 0) OP(AESDECLAST, 0, 0) \
	OP(ROUNDPS, 0, 0) OP(ROUNDPD, 0, 0) OP(ROUNDSS, 0, 0) OP(ROUNDSD, 0, 0) OP(BLENDPS, 0, 0) OP(BLENDPD, 0, 0) OP(PBLENDW, 0, 0) OP(PALIGNR, 0, 0) \
	OP(PEXTRB, 0, 0) OP(PEXTRD, 0, 0) OP(EXTRACTPS, 0, 0) OP(PINSRB, 0, 0) OP(INSERTPS, 0, 0) OP(PINSRD, 0, 0) \
	OP(DPPS, 0, 0) OP(DPPD, 0, 0) OP(MPSADBW, 0, 0) OP(PCLMULQDQ, 0, 0) \
	OP(PCMPESTRM, 0, 0x3f) OP(PCMPESTRI, 0, 0x3f) OP(PCMPISTRM, 0, 0x3f) OP(PCMPISTRI, 0, 0x3f) OP(AESKEYGENASSIST, 0, 0)

#define X86_OP_ENUM(name, r, w) X86_OP_##name,
enum { X86_OPS(X86_OP_ENUM) X86_OP_COUNT };

#endif
