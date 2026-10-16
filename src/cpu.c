#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "emu.h"

/* instruction set extensions */
#define ISA_ZAAMO    1 /* enable AMOxxxx.W */
#define ISA_ZALRSC   1 /* enable LR.W and SC.W */
#define ISA_ZICOND   0 /* enable CZERO.EQZ and CZERO.NEZ */
#define ISA_ZICSR    1 /* enable CSRnnnn */
#define ISA_ZIFENCEI 1 /* enable FENCE.I */
#define ISA_M        1 /* enable MUL and DIV */
#define ISA_ZMMUL    1 /* enable MULxxx (implicit in M) */

/* state flags */
#define FLAG_PRIV 0x03 /* current privilege level */
#define FLAG_WFI  0x04 /* waiting for interrupt */

/* RISC-V processor */
struct cpu {
	uint32_t regs[32];
	uint32_t cycles[2];
	uint32_t pc;
	uint32_t flags;
	uint32_t trap;
	uint32_t hartid;

	uint32_t mstatus;
	uint32_t mie;
	uint32_t mtvec;
	uint32_t mscratch;
	uint32_t mepc;
	uint32_t mcause;
	uint32_t mtval;
	uint32_t mip;
};

/* traps and interrupts */
enum trap {
	TRAP_NONE = 0,
	/* traps are offset by 0x00000001 */
	TRAP_FETCH_MISALIGNED =  1, TRAP_FETCH_FAULT  =  2,
	TRAP_ILLEGAL          =  3, TRAP_BREAKPOINT   =  4,
	TRAP_LOAD_MISALIGNED  =  5, TRAP_LOAD_FAULT   =  6,
	TRAP_STORE_MISALIGNED =  7, TRAP_STORE_FAULT  =  8,
	TRAP_ECALL_FROM_U     =  9, TRAP_ECALL_FROM_S = 10,
	/* reserved */              TRAP_ECALL_FROM_M = 12,
	/* interrupts are offset by 0x80000001 */
	TRAP_MSI = 0x80000004, /* machine software interrupt */
	TRAP_MTI = 0x80000008, /* machine timer    interrupt */
	TRAP_MEI = 0x8000000C, /* machine external interrupt */
};


/* instruction fields, sign-extended */
#define RD    ((int32_t)(instr << 20) >> 27)
#define RS1   ((int32_t)(instr << 12) >> 27)
#define RS2   ((int32_t)(instr <<  7) >> 27)
#define IMM12 ((int32_t)(instr      ) >> 20)

#define STORE_OFF  ((int32_t)((instr & 0xFE000000) | \
                              (instr & 0x00000F80) << 13) >> 20)
#define BRANCH_OFF ((int32_t)((instr & 0x80000000)       | \
                              (instr & 0x7E000000) >>  1 | \
                              (instr & 0x00000F00) << 12 | \
                              (instr & 0x00000080) << 23) >> 19)
#define JAL_OFF    ((int32_t)((instr & 0x000FF000) << 11 | \
			      (instr & 0x00100000) <<  2 | \
			      (instr & 0x7FE00000) >>  9 | \
			      (instr & 0x80000000)) >> 11)

/* cpu registers */
#define REG_RD  (cpu->regs[(instr >>  7) & 0x1F])
#define REG_RS1 (cpu->regs[(instr >> 15) & 0x1F])
#define REG_RS2 (cpu->regs[(instr >> 20) & 0x1F])

/* execute instructions */
int cpu_run(struct cpu *cpu, unsigned cycles)
{
	uint32_t instr;

	/* do not execute if halted */
	if (cpu->flags & FLAG_WFI)
		return 1;

	/* check pending interrupts */
	if ((cpu->mie & cpu->mip) &&
	    (cpu->mstatus & (1 << 3)) && !cpu->trap) {
		uint32_t m = (cpu->mie & cpu->mip);
		cpu->mtval = 0;
		if (m & (1 <<  3)) cpu->trap = TRAP_MSI; /* software */
		if (m & (1 <<  7)) cpu->trap = TRAP_MTI; /* timer    */
		if (m & (1 << 11)) cpu->trap = TRAP_MEI; /* external */
	}

	/* execute instructions */
	while (cycles--) {
		/* check pending traps */
		if (cpu->trap)
			goto trap;

		/* include instruction counter */
		cpu->cycles[0]++;
		if (cpu->cycles[0] == 0)
			cpu->cycles[1]++;

		/* read instruction */
		if (bus_read32(cpu->pc, &instr)) {
			warn("cpu: fetch fault at %08x", cpu->pc);
			cpu->trap  = TRAP_FETCH_FAULT;
			cpu->mtval = cpu->pc;
			goto trap;
		}
//fprintf(stderr, "trace %02u pc %08x instr %08x cycle %08x%08x regs x0 %08x ra %08x sp %08x gp %08x tp %08x t0 %08x t1 %08x t2 %08x s0 %08x s1 %08x a0 %08x a1 %08x a2 %08x a3 %08x a4 %08x a5 %08x a6 %08x a7 %08x s2 %08x s3 %08x s4 %08x s5 %08x s6 %08x s7 %08x s8 %08x s9 %08x s10 %08x s11 %08x t3 %08x t4 %08x t5 %08x t6 %08x\r\n", cpu->hartid, cpu->pc, instr, cpu->cycles[1], cpu->cycles[0], cpu->regs[0], cpu->regs[1], cpu->regs[2], cpu->regs[3], cpu->regs[4], cpu->regs[5], cpu->regs[6], cpu->regs[7], cpu->regs[8], cpu->regs[9], cpu->regs[10], cpu->regs[11], cpu->regs[12], cpu->regs[13], cpu->regs[14], cpu->regs[15], cpu->regs[16], cpu->regs[17], cpu->regs[18], cpu->regs[19], cpu->regs[20], cpu->regs[21], cpu->regs[22], cpu->regs[23], cpu->regs[24], cpu->regs[25], cpu->regs[26], cpu->regs[27], cpu->regs[28], cpu->regs[29], cpu->regs[30], cpu->regs[31]);
		if ((instr & 0x03) != 0x03) {
			warn("cpu: compressed instruction %08x",
				instr, cpu->pc);
			goto illegal;
		}

		/* execute one instruction */
		switch ((instr & 0x007C) << 1 | (instr & 0x7000) >> 12) {
		case 0x00: /* LOAD/LB */
			if (RD) {
				uint8_t data;
				if (bus_read8(REG_RS1 + IMM12, &data)) {
					cpu->trap  = TRAP_LOAD_FAULT;
					cpu->mtval = REG_RS1 + IMM12;
					goto trap;
				}
				REG_RD = (int32_t)(int8_t)data;
			}
			break;

		case 0x01: /* LOAD/LH */
			if (RD) {
				uint16_t data;
				if (bus_read16(REG_RS1 + IMM12, &data)) {
					cpu->trap  = TRAP_LOAD_FAULT;
					cpu->mtval = REG_RS1 + IMM12;
					goto trap;
				}
				REG_RD = (int32_t)(int16_t)data;
			}
			break;

		case 0x02: /* LOAD/LW */
			if (RD) {
				uint32_t data;
				if (bus_read32(REG_RS1 + IMM12, &data)) {
					cpu->trap  = TRAP_LOAD_FAULT;
					cpu->mtval = REG_RS1 + IMM12;
					goto trap;
				}
				REG_RD = (int32_t)data;
			}
			break;

		case 0x04: /* LOAD/LBU */
			if (RD) {
				uint8_t data;
				if (bus_read8(REG_RS1 + IMM12, &data)) {
					cpu->trap  = TRAP_LOAD_FAULT;
					cpu->mtval = REG_RS1 + IMM12;
					goto trap;
				}
				REG_RD = data;
			}
			break;

		case 0x05: /* LOAD/LHU */
			if (RD) {
				uint16_t data;
				if (bus_read16(REG_RS1 + IMM12, &data)) {
					cpu->trap  = TRAP_LOAD_FAULT;
					cpu->mtval = REG_RS1 + IMM12;
					goto trap;
				}
				REG_RD = data;
			}
			break;

		case 0x18: /* MISC-MEM/FENCE */
			/* nothing */
			break;

#if ISA_ZIFENCEI
		case 0x19: /* MISC-MEM/FENCE.I */
			/* nothing */
			break;
#endif /* ISA_ZIFENCEI */

		case 0x20: /* OP-IMM/ADDI */
			if (RD)
				REG_RD = REG_RS1 + IMM12;
			break;

		case 0x21: /* OP-IMM/SLxI */
			switch (instr >> 25) {
			case 0x00: /* SLLI */
				if (RD)
					REG_RD = REG_RS1 << (IMM12 & 0x1F);
				break;

			default:
				goto illegal;
			}
			break;

		case 0x22: /* OP-IMM/SLTI */
			if (RD)
				REG_RD = (int32_t)REG_RS1 < (int32_t)IMM12;
			break;

		case 0x23: /* OP-IMM/SLTIU */
			if (RD)
				REG_RD = (uint32_t)REG_RS1 < (uint32_t)IMM12;
			break;

		case 0x24: /* OP-IMM/XORI */
			if (RD)
				REG_RD = REG_RS1 ^ IMM12;
			break;

		case 0x25: /* OP-IMM/SRxI */
			switch (instr >> 25) {
			case 0x00: /* SRLI */
				if (RD)
					REG_RD = REG_RS1 >> (IMM12 & 0x1F);
				break;

			case 0x20: /* SRAI */
				if (RD)
					REG_RD = (int32_t)REG_RS1 >> (IMM12 & 0x1F);
				break;

			default:
				goto illegal;
			}
			break;

		case 0x26: /* OP-IMM/ORI */
			if (RD)
				REG_RD = REG_RS1 | IMM12;
			break;

		case 0x27: /* OP-IMM/ANDI */
			if (RD)
				REG_RD = REG_RS1 & IMM12;
			break;

		case 0x28: /* AUIPC */
		case 0x29: /* AUIPC */
		case 0x2A: /* AUIPC */
		case 0x2B: /* AUIPC */
		case 0x2C: /* AUIPC */
		case 0x2D: /* AUIPC */
		case 0x2E: /* AUIPC */
		case 0x2F: /* AUIPC */
			if (RD)
				REG_RD = cpu->pc + (instr & 0xFFFFF000);
			break;

		case 0x40: /* STORE/SB */
			if (bus_write8(REG_RS1 + STORE_OFF, REG_RS2)) {
				cpu->trap  = TRAP_STORE_FAULT;
				cpu->mtval = REG_RS1 + STORE_OFF;
				goto trap;
			}
			break;

		case 0x41: /* STORE/SH */
			if (bus_write16(REG_RS1 + STORE_OFF, REG_RS2)) {
				cpu->trap  = TRAP_STORE_FAULT;
				cpu->mtval = REG_RS1 + STORE_OFF;
				goto trap;
			}
			break;

		case 0x42: /* STORE/SW */
			if (bus_write32(REG_RS1 + STORE_OFF, REG_RS2)) {
				cpu->trap  = TRAP_STORE_FAULT;
				cpu->mtval = REG_RS1 + STORE_OFF;
				goto trap;
			}
			break;

		case 0x5A: /* AMO */
			switch (instr >> 27) {
#if ISA_ZALRSC
			case 2: /* LR.W */
				if (1) {
					uint32_t data;
					if (bus_read_reserved32(REG_RS1, &data, cpu->hartid)) {
						cpu->trap  = TRAP_LOAD_FAULT;
						cpu->mtval = REG_RS1;
						goto trap;
					}
					if (RD) {
						REG_RD = (int32_t)data;
					}
				}
				break;

			case 3: /* SC.W */
				if (1) {
					uint32_t success;
					if (bus_write_conditional32(REG_RS1, REG_RS2,
					                            cpu->hartid, &success)) {
						cpu->trap  = TRAP_STORE_FAULT;
						cpu->mtval = REG_RS1;
						goto trap;
					}
					if (RD) {
						REG_RD = (int32_t)success;
					}
				}
				break;
#endif /* ISA_ZALRSC */
#if ISA_ZAAMO
			case  0: /* AMOADD.W  */
			case  1: /* AMOSWAP.W */
			case  4: /* AMOXOR.W  */
			case  8: /* AMOOR.W   */
			case 12: /* AMOAND.W  */
			case 16: /* AMOMIN.W  */
			case 20: /* AMOMAX.W  */
			case 24: /* AMOMINU.W */
			case 28: /* AMOMAXU.W */
			{
				uint32_t ov, nv = 0;

				/* read operand */
				if (bus_read32(REG_RS1, &ov)) {
					cpu->trap  = TRAP_LOAD_FAULT;
					cpu->mtval = REG_RS1;
					goto trap;
				}

				/* modify operand */
				switch (instr >> 27) {
				case  0: nv = ov + REG_RS2; break;
				case  1: nv =      REG_RS2; break;
				case  4: nv = ov ^ REG_RS2; break;
				case  8: nv = ov | REG_RS2; break;
				case 12: nv = ov & REG_RS2; break;
				case 16: nv = (int32_t)ov <
				              (int32_t)REG_RS2 ?
				              ov : REG_RS2; break;
				case 20: nv = (int32_t)ov >
				              (int32_t)REG_RS2 ?
				              ov : REG_RS2; break;
				case 24: nv = ov < REG_RS2 ?
				              ov : REG_RS2; break;
				case 28: nv = ov > REG_RS2 ?
				              ov : REG_RS2; break;
				}

				/* write operand */
				if (bus_write32(REG_RS1, nv)) {
					cpu->trap  = TRAP_STORE_FAULT;
					cpu->mtval = REG_RS1;
					goto trap;
				}

				/* return unmodified operand */
				if (RD)
					REG_RD = ov;
			}	break;
#endif /* ISA_ZAAMO */
			default:
				goto illegal;
			}
			break;

		case 0x60: /* OP */
			switch (instr >> 25) {
			case 0x00: /* ADD */
				if (RD)
					REG_RD = REG_RS1 + REG_RS2;
				break;

#if ISA_M | ISA_ZMMUL
			case 0x01: /* MUL */
				if (RD)
					REG_RD = (int32_t)REG_RS1 * (int32_t)REG_RS2;
				break;
#endif /* ISA_M | ISA_ZMMUL */

			case 0x20: /* SUB */
				if (RD)
					REG_RD = REG_RS1 - REG_RS2;
				break;

			default:
				goto illegal;
			}
			break;

		case 0x61: /* OP */
			switch (instr >> 25) {
			case 0x00: /* SLL */
				if (RD)
					REG_RD = REG_RS1 << (REG_RS2 & 0x1F);
				break;

#if ISA_M | ISA_ZMMUL
			case 0x01: /* MULH */
				if (RD)
					REG_RD = ((int64_t)((int32_t)(REG_RS1)) * (int64_t)((int32_t)(REG_RS2))) >> 32;
				break;
#endif /* ISA_M | ISA_ZMMUL */

			default:
				goto illegal;
			}
			break;

		case 0x62: /* OP */
			switch (instr >> 25) {
			case 0x00: /* SLT */
				if (RD)
					REG_RD = (int32_t)REG_RS1 < (int32_t)REG_RS2;
				break;

#if ISA_M | ISA_ZMMUL
			case 0x01: /* MULHSU */
				if (RD)
					REG_RD = ((int64_t)((int32_t)(REG_RS1)) * (uint64_t)(REG_RS2)) >> 32;
				break;
#endif /* ISA_M | ISA_ZMMUL */

			default:
				goto illegal;
			}
			break;

		case 0x63: /* OP */
			switch (instr >> 25) {
			case 0x00: /* SLTU */
				if (RD)
					REG_RD = REG_RS1 < REG_RS2;
				break;

#if ISA_M | ISA_ZMMUL
			case 0x01: /* MULHU */
				if (RD)
					REG_RD = ((uint64_t)(REG_RS1) * (uint64_t)(REG_RS2)) >> 32;
				break;
#endif /* ISA_M | ISA_ZMMUL */

			default:
				goto illegal;
			}
			break;

		case 0x64: /* OP */
			switch (instr >> 25) {
			case 0x00: /* XOR */
				if (RD)
					REG_RD = REG_RS1 ^ REG_RS2;
				break;

#if ISA_M
			case 0x01: /* DIV */
				if (RD)
					REG_RD = REG_RS2 ? (uint32_t)((int32_t)REG_RS1 / (int32_t)REG_RS2) : (uint32_t)-1;
				break;
#endif /* ISA_M */

			default:
				goto illegal;
			}
			break;

		case 0x65: /* OP */
			switch (instr >> 25) {
			case 0x00: /* SRL */
				if (RD)
					REG_RD = REG_RS1 >> (REG_RS2 & 0x1F);
				break;

#if ISA_M
			case 0x01: /* DIVU */
				if (RD)
					REG_RD = REG_RS2 ? (REG_RS1 / REG_RS2) : (uint32_t)-1;
				break;
#endif /* ISA_M */

			case 0x20: /* SRA */
				if (RD)
					REG_RD = (int32_t)REG_RS1 >> (REG_RS2 & 0x1F);
				break;

			default:
				goto illegal;
			}
			break;

		case 0x66: /* OP */
			switch (instr >> 25) {
			case 0x00: /* OR */
				if (RD)
					REG_RD = REG_RS1 | REG_RS2;
				break;

#if ISA_M
			case 0x01: /* REM */
				if (RD)
					REG_RD = REG_RS2 ? (uint32_t)((int32_t)REG_RS1 % (int32_t)REG_RS2) : REG_RS1;
				break;
#endif /* ISA_M */

			default:
				goto illegal;
			}
			break;

		case 0x67: /* OP */
			switch (instr >> 25) {
			case 0x00: /* AND */
				if (RD)
					REG_RD = REG_RS1 & REG_RS2;
				break;

#if ISA_M
			case 0x01: /* REMU */
				if (RD)
					REG_RD = REG_RS2 ?  (REG_RS1 % REG_RS2) : REG_RS1;
				break;
#endif /* ISA_M */

			default:
				goto illegal;
			}
			break;


		case 0x68: /* LUI */
		case 0x69: /* LUI */
		case 0x6A: /* LUI */
		case 0x6B: /* LUI */
		case 0x6C: /* LUI */
		case 0x6D: /* LUI */
		case 0x6E: /* LUI */
		case 0x6F: /* LUI */
			if (RD)
				REG_RD = instr & 0xFFFFF000;
			break;

		case 0xC0: /* BEQ */
			if (REG_RS1 == REG_RS2)
				cpu->pc += BRANCH_OFF - 4;
			break;

		case 0xC1: /* BNE */
			if (REG_RS1 != REG_RS2)
				cpu->pc += BRANCH_OFF - 4;
			break;

		case 0xC4: /* BLT */
			if ((int32_t)REG_RS1 < (int32_t)REG_RS2)
				cpu->pc += BRANCH_OFF - 4;
			break;

		case 0xC5: /* BGE */
			if ((int32_t)REG_RS1 >= (int32_t)REG_RS2)
				cpu->pc += BRANCH_OFF - 4;
			break;

		case 0xC6: /* BLTU */
			if (REG_RS1 < REG_RS2)
				cpu->pc += BRANCH_OFF - 4;
			break;

		case 0xC7: /* BGEU */
			if (REG_RS1 >= REG_RS2)
				cpu->pc += BRANCH_OFF - 4;
			break;

		case 0xC8: { /* JALR */
			uint32_t addr = REG_RS1 + IMM12;
			if (RD)
				REG_RD = cpu->pc + 4;
			cpu->pc = addr - 4;
			} break;

		case 0xD8: /* JAL */
		case 0xD9: /* JAL */
		case 0xDA: /* JAL */
		case 0xDB: /* JAL */
		case 0xDC: /* JAL */
		case 0xDD: /* JAL */
		case 0xDE: /* JAL */
		case 0xDF: /* JAL */
			if (RD)
				REG_RD = cpu->pc + 4;
			cpu->pc += JAL_OFF - 4;
			break;

		case 0xE0: /* SYSTEM */
			switch (instr) {
			case 0x00000073: /* ECALL */
				cpu->trap = cpu->flags & FLAG_PRIV ?
					TRAP_ECALL_FROM_M :
					TRAP_ECALL_FROM_U;
				cpu->mtval = cpu->pc;
				goto trap;

			case 0x00100073: /* EBREAK */
				cpu->trap = TRAP_BREAKPOINT;
				cpu->mtval = cpu->pc;
				warn("ebreak");
				goto trap;

			case 0x10500073: /* WFI */
				/* only sleep if no interrupts pending */
				if (!(cpu->mie & cpu->mip))
					cpu->flags |= FLAG_WFI;
				break;

			case 0x30200073: /* MRET */
			{
				/* PRIV <= MPP, MPP <= 0,
				   MIE <= MPIE, MPIE <= 1 */
				uint32_t mstatus = cpu->mstatus;
				cpu->mstatus = (cpu->mstatus & (1 << 7)) >> 4 |
				               (cpu->flags & FLAG_PRIV) << 11 |
				               (1 << 7);
				cpu->flags   = (cpu->flags & ~FLAG_PRIV) |
				               (mstatus & (3 << 11)) >> 11;
				cpu->pc      = cpu->mepc - 4;
			}	break;

			default:
				goto illegal;
			}
			break;

#if ISA_ZICSR
		case 0xE1: /* SYSTEM/CSRRW */
			if (RD) {
				uint32_t tmp = cpu_csr_get(cpu, instr >> 20);
				cpu_csr_set(cpu, instr >> 20, REG_RS1);
				REG_RD = tmp;
			} else {
				cpu_csr_set(cpu, instr >> 20, REG_RS1);
			}
			break;

		case 0xE2: /* SYSTEM/CSRRS */
		{
			uint32_t tmp = cpu_csr_get(cpu, instr >> 20);
			if (RD)
				REG_RD = tmp;
			if (RS1)
				cpu_csr_set(cpu, instr >> 20, tmp | REG_RS1);
		}	break;

		case 0xE3: /* SYSTEM/CSRRC */
		{
			uint32_t tmp = cpu_csr_get(cpu, instr >> 20);
			if (RD)
				REG_RD = tmp;
			if (RS1)
				cpu_csr_set(cpu, instr >> 20, tmp & ~REG_RS1);
		}	break;

		case 0xE5: /* SYSTEM/CSRRWI */
			if (RD) {
				uint32_t tmp = cpu_csr_get(cpu, instr >> 20);
				cpu_csr_set(cpu, instr >> 20, RS1);
				REG_RD = tmp;
			} else {
				cpu_csr_set(cpu, instr >> 20, RS1);
			}
			break;

		case 0xE6: /* SYSTEM/CSRRSI */
		{
			uint32_t tmp = cpu_csr_get(cpu, instr >> 20);
			if (RD)
				REG_RD = tmp;
			if (RS1)
				cpu_csr_set(cpu, instr >> 20, tmp | RS1);
		}	break;

		case 0xE7: /* SYSTEM/CSRRCI */
		{
			uint32_t tmp = cpu_csr_get(cpu, instr >> 20);
			if (RD)
				REG_RD = tmp;
			if (RS1)
				cpu_csr_set(cpu, instr >> 20, tmp & ~RS1);
		}	break;
#endif /* ISA_ZICSR */

		default:
			goto illegal;
		}

		/* handle traps */
trap:		if (cpu->trap) {
			/* MPP <= PRIV, MPIE <= MIE, MIE <= 0 */
			cpu->mcause  = cpu->trap - 1;
			cpu->mstatus = (cpu->mstatus & (1 << 3)) << 4 |
			               (cpu->flags & FLAG_PRIV) << 11;
			cpu->flags  |= FLAG_PRIV; /* switch to M */

			/* continue from vector */
			cpu->mepc = cpu->pc;
			cpu->pc   = cpu->mtvec - 4;
			cpu->trap = 0;
		}

		/* continue with next instruction */
		cpu->pc += 4;
		continue;

		/* illegal instruction */
illegal:	fail("cpu: illegal instruction %08x at %08x",
			instr, cpu->pc);
		cpu->trap  = TRAP_ILLEGAL;
		cpu->mtval = instr;
		goto trap; /* continue */
	}

	/* finished execution */
	return 0;
}

/* irq clear pending */
void cpu_irq_clear(struct cpu *cpu, unsigned irq)
{
	cpu->mip &= ~(1 << irq);
}

/* irq set pending */
void cpu_irq_set(struct cpu *cpu, unsigned irq)
{
	cpu->mip |= (1 << irq);
	cpu->flags &= ~FLAG_WFI;
}

/* irq get pending */
int cpu_irq_get(struct cpu *cpu, unsigned irq)
{
	return !!(cpu->mip & (1 << irq));
}

/* csr write */
void cpu_csr_set(struct cpu *cpu, enum rv_csr csr, uint32_t val)
{
	switch (csr) {
	case CSR_MSTATUS:  cpu->mstatus  = val; break;
	case CSR_MIE:      cpu->mie      = val; break;
	case CSR_MTVEC:    cpu->mtvec    = val; break;
	case CSR_MSCRATCH: cpu->mscratch = val; break;
	case CSR_MEPC:     cpu->mepc     = val; break;
	case CSR_MCAUSE:   cpu->mcause   = val; break;
	case CSR_PMPCFG0:  break; /* not implemented */
	case CSR_PMPADDR0: break; /* not implemented */
	case CSR_MIP:      cpu->mip      = val; break;
	default:
		fail("cpu: invalid csr %08x val %08x (%c)",
			csr, val, (val >= 0x20 && val < 0x7F) ? val : '.');
	}

	return;
}

/* read csr */
uint32_t cpu_csr_get(struct cpu *cpu, enum rv_csr csr)
{
	switch (csr) {
	case CSR_CYCLE:      return cpu->cycles[0];
	case CSR_CYCLEH:     return cpu->cycles[1];
	case CSR_MVENDORID:  return 0x00000000; /* not implemented */
	case CSR_MARCHID:    return 0x00000000; /* not implemented */
	case CSR_MIMPID:     return 0x00000000; /* not implemented */
	case CSR_MHARTID:    return cpu->hartid;
	case CSR_MCONFIGPTR: return 0x00000000; /* not implemented */
	case CSR_PMPCFG0:    return 0x00000000; /* not implemented */
	case CSR_PMPADDR0:   return 0x00000000; /* not implemented */
	case CSR_MSTATUS:    return cpu->mstatus;
	case CSR_MISA:       return 0x00000000; /* not implemented */
	case CSR_MIE:        return cpu->mie;
	case CSR_MTVEC:      return cpu->mtvec;
	case CSR_MSCRATCH:   return cpu->mscratch;
	case CSR_MEPC:       return cpu->mepc;
	case CSR_MCAUSE:     return cpu->mcause;
	case CSR_MTVAL:      return cpu->mtval;
	case CSR_MIP:        return cpu->mip;
	default:             fail("cpu: invalid csr %08x", csr);
	}

	return (uint32_t)-1;
}

/* write register */
void cpu_reg_set(struct cpu *cpu, enum rv_reg reg, uint32_t val)
{
	if (reg >= 32)
		fail("cpu: invalid reg %u", reg);

	cpu->regs[reg] = val;
	return;
}

/* read register */
uint32_t cpu_reg_get(struct cpu *cpu, enum rv_reg reg)
{
	if (reg >= 32)
		fail("cpu: invalid reg %u", reg);

	return cpu->regs[reg];
}

/* reset cpu */
void cpu_reset(struct cpu *cpu, uint32_t entry)
{
	uint32_t hartid = cpu->hartid;
	memset(cpu, 0, sizeof(struct cpu));
	cpu->flags |= FLAG_PRIV; /* M mode */
	cpu->hartid = hartid;
	cpu->pc = entry;
}

/* free a cpu instance */
void cpu_free(struct cpu *cpu)
{
	free(cpu);
	return;
}

/* initialize a cpu instance */
struct cpu *cpu_init(uint32_t id)
{
	struct cpu *cpu = malloc(sizeof(struct cpu));
	if (cpu) {
		cpu->hartid = id;
		cpu_reset(cpu, 0);
	}
	return cpu;
}
