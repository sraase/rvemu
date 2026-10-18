#ifndef EMU_H
#define EMU_H

/* risc-v registers */
enum rv_reg {
	REG_X0  = 0,  REG_ZERO =  0, /* hardwired zero       */
	REG_X1  = 1,  REG_RA   =  1, /* return address       */
	REG_X2  = 2,  REG_SP   =  2, /* stack pointer        */
	REG_X3  = 3,  REG_GP   =  3, /* global pointer       */
	REG_X4  = 4,  REG_TP   =  4, /* thread pointer       */
	REG_X5  = 5,  REG_T0   =  5, /* temporary register 0 */
	REG_X6  = 6,  REG_T1   =  6, /* temporary register 1 */
	REG_X7  = 7,  REG_T2   =  7, /* temporary register 2 */
	REG_X8  = 8,  REG_S0   =  8, /* saved register 0     */
	              REG_FP   =  8, /* frame pointer        */
	REG_X9  = 9,  REG_S1   =  9, /* saved register 1     */
	REG_X10 = 10, REG_A0   = 10, /* function argument 0  */
	              REG_R0   = 10, /* return value 0       */
	REG_X11 = 11, REG_A1   = 11, /* function argument 1  */
	              REG_R1   = 11, /* return value 1       */
	REG_X12 = 12, REG_A2   = 12, /* function argument 2  */
	REG_X13 = 13, REG_A3   = 13, /* function argument 3  */
	REG_X14 = 14, REG_A4   = 14, /* function argument 4  */
	REG_X15 = 15, REG_A5   = 15, /* function argument 5  */
	REG_X16 = 16, REG_A6   = 16, /* function argument 6  */
	REG_X17 = 17, REG_A7   = 17, /* function argument 7  */
	REG_X18 = 18, REG_S2   = 18, /* saved register 2     */
	REG_X19 = 19, REG_S3   = 19, /* saved register 3     */
	REG_X20 = 20, REG_S4   = 20, /* saved register 4     */
	REG_X21 = 21, REG_S5   = 21, /* saved register 5     */
	REG_X22 = 22, REG_S6   = 22, /* saved register 6     */
	REG_X23 = 23, REG_S7   = 23, /* saved register 7     */
	REG_X24 = 24, REG_S8   = 24, /* saved register 8     */
	REG_X25 = 25, REG_S9   = 25, /* saved register 9     */
	REG_X26 = 26, REG_S10  = 26, /* saved register 10    */
	REG_X27 = 27, REG_S11  = 27, /* saved register 11    */
	REG_X28 = 28, REG_T3   = 28, /* temporary register 3 */
	REG_X29 = 29, REG_T4   = 29, /* temporary register 4 */
	REG_X30 = 30, REG_T5   = 30, /* temporary register 5 */
	REG_X31 = 31, REG_T6   = 31, /* temporary register 6 */
};

/* risc-v control and status registers */
enum rv_csr {
	/* Unprivileged Floating-Point CSRs */
	CSR_FFLAGS     = 0x001, /* Floating-Point Accrued Exceptions.        */
	CSR_FRM        = 0x002, /* Floating-Point Dynamic Rounding Mode.     */
	CSR_FCSR       = 0x003, /* Floating-Point Control and Status Register. */

	/* Unprivileged Counter/Timers */
	CSR_CYCLE      = 0xC00, /* Cycle counter for RDCYCLE instrucion.     */
	CSR_TIME       = 0xC01, /* Timer for RDTIME instruction.             */
	CSR_INSTRET    = 0xC02, /* Instructions-retired counter for RDINSTRET. */
	/* ... */
	CSR_CYCLEH     = 0xC80, /* Upper 32 bits of cycle, RV32 only.        */
	CSR_TIMEH      = 0xC81, /* Upper 32 bits of time, RV32 only.         */
	CSR_INSTRETH   = 0xC82, /* Upper 32 bits of instret, RV32 only.      */
	/* ... */

	/* Machine Information Registers */
	CSR_MVENDORID  = 0xF11, /* Vendor ID.                                */
	CSR_MARCHID    = 0xF12, /* Architecture ID.                          */
	CSR_MIMPID     = 0xF13, /* Implementation ID.                        */
	CSR_MHARTID    = 0xF14, /* Hardware thread ID.                       */
	CSR_MCONFIGPTR = 0xF15, /* Pointer to configuration data structure.  */

	/* Machine Trap Setup */
	CSR_MSTATUS    = 0x300, /* Machine status register.                  */
	CSR_MISA       = 0x301, /* ISA and extensions.                       */
	CSR_MEDELEG    = 0x302, /* Machine exception delegation register.    */
	CSR_MIDELEG    = 0x303, /* Machine interrupt delegation register.    */
	CSR_MIE        = 0x304, /* Machine interrupt-enable register.        */
	CSR_MTVEC      = 0x305, /* Machine trap-handler base address.        */
	CSR_MCOUNTEREN = 0x306, /* Machine counter enable.                   */
	CSR_MSTATUSH   = 0x310, /* Additional machine status register, RV32 only. */
	CSR_MEDELEGH   = 0x312, /* Upper 32 bits of medeleg, RV32 only.      */

	/* Machine Trap Handling */
	CSR_MSCRATCH   = 0x340, /* Scratch register for machine trap handlers. */
	CSR_MEPC       = 0x341, /* Machine exception program counter.        */
	CSR_MCAUSE     = 0x342, /* Machine trap cause.                       */
	CSR_MTVAL      = 0x343, /* Machine bad address or instruction.       */
	CSR_MIP        = 0x344, /* Machine interrupt pending.                */
	CSR_MTINST     = 0x34A, /* Machine trap instruction (transformed).   */
	CSR_MTVAL2     = 0x34B, /* Machine bad guest physical address.       */

	/* Machine Configuration */
	CSR_MENVCFG    = 0x30A, /* Machine environment configuration register. */
	CSR_MENVCFGH   = 0x31A, /* Upper 32 bits of menvcfg, RV32 only.      */
	CSR_MSECCFG    = 0x747, /* Machine security configuration register.  */
	CSR_MSECCFGH   = 0x757, /* Upper 32 bits of mseccfg, RV32 only.      */

	/* Machine Memory Protection */
	CSR_PMPCFG0    = 0x3A0, /* Physical memory protection configuration. */
	CSR_PMPCFG1    = 0x3A1, /* Physical memory protection configuration, RV32 only. */
	/* ... */
	CSR_PMPADDR0   = 0x3B0, /* Physical memory protection address register. */
	CSR_PMPADDR1   = 0x3B1, /* Physical memory protection address register. */
	/* ... */

	/* Machine Non-Maskable Interrupt Handling */
	CSR_MNSCRATCH  = 0x740, /* Resumable NMI scratch register.           */
	CSR_MNEPC      = 0x741, /* Resumable NMI program counter.            */
	CSR_MNCAUSE    = 0x742, /* Resumable NMI cause.                      */
	CSR_MNSTATUS   = 0x744, /* Resumable NMI status.                     */

	/* Machine Counter/Timers */
	CSR_MCYCLE     = 0xB00, /* Machine cycle counter.                    */
	CSR_MINSTRET   = 0xB02, /* Machine instructions-retired counter.     */
	/* ... */
	CSR_MCYCLEH    = 0xB80, /* Upper 32 bits of mcycle, RV32 only.       */
	CSR_MINSTRETH  = 0xB82, /* Upper 32 bits of minstret, RV32 only.     */
	/* ... */
};

/* bus operations */
struct bus_ops {
	int (*read32) (void *ctx, uint32_t addr, uint32_t *data);
	int (*write32)(void *ctx, uint32_t addr, uint32_t  data);
	int (*read16) (void *ctx, uint32_t addr, uint16_t *data);
	int (*write16)(void *ctx, uint32_t addr, uint16_t  data);
	int (*read8)  (void *ctx, uint32_t addr, uint8_t  *data);
	int (*write8) (void *ctx, uint32_t addr, uint8_t   data);
	int (*read_reserved32)(void *ctx,
		uint32_t addr, uint32_t *data, uint32_t id);
	int (*write_conditional32)(void *ctx,
		uint32_t addr, uint32_t data, uint32_t id, uint32_t *success);
};

/* user interface */
struct   ui *ui_init   (void);
void     ui_free       (struct ui *ui);
void     ui_tick       (struct ui *ui);
void     ui_fb_getsize (struct ui *ui, uint32_t *width, uint32_t *height);
void     ui_fb_setbuf  (struct ui *ui, uint32_t *buf);
void     ui_fb_update  (struct ui *ui);
void     ui_kb_setcb   (struct ui *ui, void *ctx,
                        void (*callback)(void *ctx, uint32_t key));

/* risc-v processor */
struct   cpu *cpu_init(uint32_t id);
void     cpu_free     (struct cpu *cpu);
void     cpu_reset    (struct cpu *cpu, uint32_t entry);
int      cpu_run      (struct cpu *cpu, unsigned cycles);
uint32_t cpu_reg_get  (struct cpu *cpu, enum rv_reg reg);
void     cpu_reg_set  (struct cpu *cpu, enum rv_reg reg, uint32_t val);
uint32_t cpu_csr_get  (struct cpu *cpu, enum rv_csr csr);
void     cpu_csr_set  (struct cpu *cpu, enum rv_csr csr, uint32_t val);
int      cpu_irq_get  (struct cpu *cpu, unsigned irq);
void     cpu_irq_set  (struct cpu *cpu, unsigned irq);
void     cpu_irq_clear(struct cpu *cpu, unsigned irq);

/* system bus */
void     bus_attach (uint32_t start, uint32_t size,
                     struct bus_ops *ops, void *ctx);
int      bus_read32 (uint32_t addr, uint32_t *data);
int      bus_write32(uint32_t addr, uint32_t  data);
int      bus_read16 (uint32_t addr, uint16_t *data);
int      bus_write16(uint32_t addr, uint16_t  data);
int      bus_read8  (uint32_t addr, uint8_t  *data);
int      bus_write8 (uint32_t addr, uint8_t   data);
int      bus_read_reserved32(uint32_t addr, uint32_t *data, uint32_t id);
int      bus_write_conditional32(uint32_t addr, uint32_t data,
                                 uint32_t id, uint32_t *success);

/* clint */
extern   struct bus_ops clint_ops;
struct   clint *clint_init(struct cpu **cpu, size_t num_cpus);
void     clint_tick       (struct clint *clint);
void     clint_free       (struct clint *clint);

/* plic */
extern   struct bus_ops plic_ops;
struct   plic *plic_init(struct cpu **cpu, size_t num_cpus);
void     plic_free      (struct plic *plic);
void     plic_irq_set   (struct plic *plic, unsigned irq);
void     plic_irq_clear (struct plic *plic, unsigned irq);

/* uart */
extern   struct bus_ops uart_ops;
struct   uart *uart_init(struct plic *plic, unsigned irq);
void     uart_tick      (struct uart *uart);
void     uart_free      (struct uart *uart);

/* display */
extern   struct bus_ops display_ops;
struct   display *display_init(struct ui *ui);
void     display_free         (struct display *display);

/* utility */
uint64_t gettime_us(void);

void logmsg(const char *prefix, const char *postfix, int fatal, const char *fmt, ...);
#define debug(...)  logmsg("[[36mdbg [0m]", "\r", 0, __VA_ARGS__)
#define info(...)   logmsg("[[32minfo[0m]", "\r", 0, __VA_ARGS__)
#define warn(...)   logmsg("[[33mwarn[0m]", "\r", 0, __VA_ARGS__)
#define fail(...)   logmsg("[[31mfail[0m]", "\r", 1, __VA_ARGS__)

#endif
