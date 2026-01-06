#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include <sys/select.h>
#include <slirp/libvdeslirp.h>

#include "emu.h"

/**
 * This is a vdeslirp backend.
 */
static int pkt_init(struct vdeslirp **slirp)
{
	SlirpConfig cfg;

	vdeslirp_init(&cfg, VDE_INIT_DEFAULT);

	*slirp = vdeslirp_open(&cfg);
	if (!*slirp)
		fail("pkt: cannot open slirp");

	return 0;
}

static void pkt_send(struct vdeslirp *slirp, uint8_t *buf, unsigned len)
{
	ssize_t r = vdeslirp_send(slirp, buf, len);
	if (r != len)
		warn("pkt: sent %zd instead of %u", r, len);
}

static unsigned pkt_recv(struct vdeslirp *slirp, uint8_t *buf, unsigned len)
{
	ssize_t r;
	int fd;
	fd_set fds;
	struct timeval tv;

	fd = vdeslirp_fd(slirp);
	FD_ZERO(&fds);
	FD_SET(fd, &fds);

	tv.tv_sec = 0;
	tv.tv_usec = 0;

	if (select(fd+1, &fds, NULL, NULL, &tv) == 0)
		return 0;

	r = vdeslirp_recv(slirp, buf, len);
	if (r >= 0)
		return r;

	warn("pkg: recv returned %zd", r);
	return 0;
}

/**
 * This is a partial implementation of the
 * DP83848 10/100 Mb/s Ethernet Physical Layer.
 */
enum phy_regs {
	PHY_BMCR    = 0x00, /* Basic Mode Control Register */
	PHY_BMSR    = 0x01, /* Basic Mode Status Register */
	PHY_PHYIDR1 = 0x02, /* PHY Identifier Register #1 */
	PHY_PHYIDR2 = 0x03, /* PHY Identifier Register #2 */
	PHY_ANAR    = 0x04, /* Auto-Negotiation Advertisement Register */
	PHY_ANLPAR  = 0x05, /* Auto-Negotiation Link Partner Ability Reg */
	PHY_ANER    = 0x06, /* Auto-Negotiation Expansion Register */
	PHY_ANNPTR  = 0x07, /* Auto-Negotiation Next Page TX */
	PHY_PHYSTS  = 0x10, /* PHY Status Register */
	PHY_MICR    = 0x11, /* MII Interrupt Control Register */
	PHY_MISR    = 0x12, /* MII Interrupt Status and Misc Control Reg */
	PHY_FCSCR   = 0x14, /* False Carrier Sense Counter Register */
	PHY_RECR    = 0x15, /* Receive Error Counter Register */
	PHY_PCR     = 0x16, /* PCS Sub-Layer Configuration and Status Reg */
	PHY_RBR     = 0x17, /* RMII and Bypass Register */
	PHY_LEDCR   = 0x18, /* LED Direct Control Register */
	PHY_PHYCR   = 0x19, /* PHY Control Register */
	PHY_10BTSCR = 0x1A, /* 10Base-T Status/Control Register */
	PHY_CDCTRL1 = 0x1B, /* CD Test Control Reg and BIST Extensions Reg */
	PHY_EDCR    = 0x1D, /* Energy Detect Control Register */
	PHY_MAX     = 0x20,
};

struct phy {
	uint16_t regs[PHY_MAX]; /* registers */
};

static void phy_reset(struct phy *phy)
{
	memset(phy->regs, 0, sizeof(phy));
	phy->regs[PHY_BMCR]    = 0x3100;
	phy->regs[PHY_BMSR]    = 0x7869;
	phy->regs[PHY_PHYIDR1] = 0x2000;
	phy->regs[PHY_PHYIDR2] = 0x5CA2;
	phy->regs[PHY_ANAR]    = 0x01E1;
}

static int phy_get_link(struct phy *phy)
{
	return (phy->regs[PHY_BMSR] & 0x0004);
}

static void phy_set_link(struct phy *phy, int link)
{
	if (link) {
		/* detect link */
		phy->regs[PHY_BMSR]   |=  0x0004;
		phy->regs[PHY_ANLPAR] |=  0x0061; /* 10 Mbps Full */
	} else {
		/* detect no link */
		phy->regs[PHY_BMSR]   &= ~0x0004;
		phy->regs[PHY_ANLPAR] &= ~0x03FF;
	}
	info("phy: link %sdetected", link ? "" : "not ");
}

static uint16_t phy_read(struct phy *phy, unsigned reg)
{
	if (reg < PHY_MAX)
		return phy->regs[reg];

	warn("phy: read invalid reg %d", reg);
	return 0x00;
}

static void phy_write(struct phy *phy, unsigned reg, uint16_t data)
{
	/* read-only registers */
	if (reg == PHY_BMSR || reg == PHY_PHYIDR1 || PHY_PHYIDR2)
		return;

	/* control register reset bit */
	if (reg == PHY_BMCR && (data & 0x8000)) {
		int link = phy_get_link(phy);
		phy_reset(phy);
		phy_set_link(phy, link);
		return;
	}

	/* read-write registers */
	if (reg < PHY_MAX) {
		phy->regs[reg] = data;
		return;
	}

	warn("phy: write invalid reg %d data %04x", reg, data);
	return;
}

/**
 * This is a partial implementation of the
 * OpenCores Ethernet MAC 10/100 Mbps [1] to
 * match the Linux "opencores,ethoc" driver.
 * [1] https://opencores.org/projects/ethmac
 */
enum eth_regs {
	ETH_MODER         =  0, /* Mode Register */
	ETH_INT_SOURCE    =  1, /* Interrupt Source Register */
	ETH_INT_MASK      =  2, /* Interrupt Mask Register */
	ETH_IPGT          =  3, /* Back to Back Inter Packet Gap Register */
	ETH_IPGR1         =  4, /* Non Back to Back Inter Packet Gap Reg 1 */
	ETH_IPGR2         =  5, /* Non Back to Back Inter Packet Gap Reg 2 */
	ETH_PACKETLEN     =  6, /* Packet Length (minimum and maximum) Reg */
	ETH_COLLCONF      =  7, /* Collision and Retry Configuration */
	ETH_TX_BD_NUM     =  8, /* Transmit Buffer Descriptor Number */
	ETH_CTRLMODER     =  9, /* Control Module Mode Register */
	ETH_MIIMODER      = 10, /* MII Mode Register */
	ETH_MIICOMMAND    = 11, /* MII Command Register */
	ETH_MIIADDRESS    = 12, /* MII Address Register */
	ETH_MIITX_DATA    = 13, /* MII Transmit Data */
	ETH_MIIRX_DATA    = 14, /* MII Receive Data */
	ETH_MIISTATUS     = 15, /* MII Status Register */
	ETH_MAC_ADDR0     = 16, /* MAC Individual Address 0 */
	ETH_MAC_ADDR1     = 17, /* MAC Individual Address 1 */
	ETH_ETH_HASH0_ADR = 18, /* HASH0 Register */
	ETH_ETH_HASH1_ADR = 19, /* HASH1 Register */
	ETH_ETH_TXCTRL    = 20, /* Transmit Control Register */
	ETH_MAX           = 21,
};

struct eth {
	struct   plic *plic; /* interrupt controller */
	unsigned irqnum;     /* interrupt number */

	struct   vdeslirp *slirp; /* backend */

	uint32_t regs[ETH_MAX]; /* registers */
	uint32_t desc[256];     /* descriptors */
	struct   phy phy;       /* phy */

	uint8_t  *rxbuf;  /* rx packet buffer */
	unsigned rxlen;   /* rx buffer size */

	unsigned tx_desc; /* current tx descriptor */
	unsigned rx_desc; /* current rx descriptor */
};

static void eth_realloc(struct eth *eth)
{
	/* reallocate receive buffer for largest possible packet */
	eth->rxlen = (eth->regs[ETH_MODER] & 0x4000) ? 65536 :
		eth->regs[ETH_PACKETLEN] & 0xFFFF;
	eth->rxbuf = realloc(eth->rxbuf, eth->rxlen);
}

static void eth_reset(struct eth *eth)
{
	memset(eth->regs, 0, sizeof(eth->regs));
	eth->regs[ETH_MODER]     = 0x0000A000;
	eth->regs[ETH_IPGT]      = 0x00000012;
	eth->regs[ETH_IPGR1]     = 0x0000000C;
	eth->regs[ETH_IPGR2]     = 0x00000012;
	eth->regs[ETH_PACKETLEN] = 0x00400600;
	eth->regs[ETH_COLLCONF]  = 0x000F003F;
	eth->regs[ETH_TX_BD_NUM] = 0x00000040;
	eth->regs[ETH_MIIMODER]  = 0x00000064;
	eth->regs[ETH_MAC_ADDR0] = 0x00123456;
	eth->regs[ETH_MAC_ADDR1] = 0x00005254;
	eth_realloc(eth);

	eth->tx_desc = 0;
	eth->rx_desc = 2 * eth->regs[ETH_TX_BD_NUM];

	phy_reset(&eth->phy);
	phy_set_link(&eth->phy, 1);
}

static void eth_irq(struct eth *eth)
{
	uint32_t source = eth->regs[ETH_INT_SOURCE];
	uint32_t mask   = eth->regs[ETH_INT_MASK];

	if (eth->plic && (source & mask))
		plic_irq(eth->plic, eth->irqnum);
}

static void eth_try_tx(struct eth *eth)
{
	while((eth->regs[ETH_MODER] & 0x0002)    &&    /* Tx enabled  */
	      (eth->regs[ETH_TX_BD_NUM] > 0)     &&    /* desc exists */
	      (eth->desc[eth->tx_desc] & 0x8000) &&    /* desc valid  */
	      ((eth->desc[eth->tx_desc] >> 16) > 4)) { /* min length  */
		unsigned minfl    = eth->regs[ETH_PACKETLEN] >> 16;
		unsigned maxfl    = eth->regs[ETH_PACKETLEN] & 0xFFFF;
		unsigned desc_len = eth->desc[eth->tx_desc] >> 16;
		unsigned desc_flg = eth->desc[eth->tx_desc] & 0xFFFF;
		unsigned desc_ptr = eth->desc[eth->tx_desc + 1];
		unsigned tx_len   = desc_len;
		uint8_t  *buf;

		/* set packet length */
		if ((eth->regs[ETH_MODER] & 0x8000) && (tx_len < minfl))
			tx_len = minfl;
		if (!(eth->regs[ETH_MODER] & 0x4000) && (tx_len > maxfl))
			tx_len = maxfl;
		if (desc_len > tx_len)
			desc_len = tx_len;

		/* allocate, fill, zero-pad, send and free transmit buffer */
		buf = malloc(tx_len);
		for (unsigned i = 0; i < desc_len; ++i) {
			if (bus_read8(desc_ptr + i, &buf[i]))
				fail("eth: DMA fault during transmit");
		}
		for (unsigned i = desc_len; i < tx_len; ++i)
			buf[i] = 0;
		pkt_send(eth->slirp, buf, tx_len);
		free(buf);

		/* update descriptor */
		eth->desc[eth->tx_desc] &= 0xFFFF7800;

		/* next descriptor */
		if (desc_flg & 0x2000) {
			eth->tx_desc = 0;
		} else {
			eth->tx_desc += 2;
			if (eth->tx_desc >= 2 * eth->regs[ETH_TX_BD_NUM])
				eth->tx_desc = 0;
		}

		/* trigger interrupt */
		if (desc_flg & 0x4000) {
			eth->regs[ETH_INT_SOURCE] |= 0x0001;
			eth_irq(eth);
		}
	}
}

static void eth_try_rx(struct eth *eth)
{
	while ((eth->regs[ETH_MODER] & 0x0001) &&    /* Rx enabled  */
	       (eth->regs[ETH_TX_BD_NUM] < 0x80)) {  /* desc exists */
		unsigned minfl    = eth->regs[ETH_PACKETLEN] >> 16;
		unsigned maxfl    = eth->regs[ETH_PACKETLEN] & 0xFFFF;
		unsigned desc_flg = eth->desc[eth->rx_desc] & 0xFFFF;
		unsigned desc_ptr = eth->desc[eth->rx_desc + 1];
		unsigned len, rx_len;
		int miss = 1;

		/* receive packet */
		if (!(len = pkt_recv(eth->slirp, eth->rxbuf, eth->rxlen)))
			break;

		/* address matching */
		if (len >= 6) {
			if (!(eth->regs[ETH_MODER] & 0x0008)     &&
			    (eth->rxbuf[0] == 0xFF) && (eth->rxbuf[1] == 0xFF) &&
			    (eth->rxbuf[2] == 0xFF) && (eth->rxbuf[3] == 0xFF) &&
			    (eth->rxbuf[4] == 0xFF) && (eth->rxbuf[5] == 0xFF)) {
				/* broadcast packet */
				miss = 0;
			} else if ((eth->regs[ETH_MODER] & 0x0010) ||
			           (eth->rxbuf[0] & 0x01)) {
				/* hash or multicast */
				warn("eth: no multicast or IAM support");
			} else if ((eth->rxbuf[0] == ((eth->regs[ETH_MAC_ADDR1] >>  8) & 0xFF)) &&
			           (eth->rxbuf[1] == ((eth->regs[ETH_MAC_ADDR1]      ) & 0xFF)) &&
			           (eth->rxbuf[2] == ((eth->regs[ETH_MAC_ADDR0] >> 24) & 0xFF)) &&
			           (eth->rxbuf[3] == ((eth->regs[ETH_MAC_ADDR0] >> 16) & 0xFF)) &&
			           (eth->rxbuf[4] == ((eth->regs[ETH_MAC_ADDR0] >>  8) & 0xFF)) &&
			           (eth->rxbuf[5] == ((eth->regs[ETH_MAC_ADDR0]      ) & 0xFF))) {
				/* targeted packet */
				miss = 0;
			}
		}

		/* drop non-matching unless promiscous mode */
		if (miss && !(eth->regs[ETH_MODER] & 0x0020))
			return;

		/* drop and interrupt if descriptor busy */
		if (!(desc_flg & 0x8000)) {
			eth->regs[ETH_INT_SOURCE] |= 0x0010;
			eth_irq(eth);
			return;
		}

		/* set packet length */
		len += 4;
		rx_len = (eth->regs[ETH_MODER] & 0x4000) ? 65536 : maxfl;
		if (rx_len > len)
			rx_len = len;
		if ((eth->regs[ETH_MODER] & 0x8000) && rx_len < minfl)
			rx_len = minfl;
		len -= 4;

		/* write len bytes and zero-pad to rx_len */
		for (unsigned i = 0; i < len; ++i) {
			if (bus_write8(desc_ptr + i, eth->rxbuf[i]))
				fail("eth: DMA fault during receive (data)");
		}
		for (unsigned i = len; i < rx_len; ++i) {
			if (bus_write8(desc_ptr + i, 0))
				fail("eth: DMA fault during receive (padding)");
		}

		/* update descriptor */
		eth->desc[eth->rx_desc] &= 0x00007E00;
		eth->desc[eth->rx_desc] |= rx_len << 16;
		if (miss)
			eth->desc[eth->rx_desc] |= 0x0080;
//		if (len < min_len)
//			eth->desc[eth->rx_desc] |= 0x0004;
		if (len > maxfl)
			eth->desc[eth->rx_desc] |= 0x0040;

		/* next descriptor */
		if (desc_flg & 0x2000) {
			eth->rx_desc = 2 * eth->regs[ETH_TX_BD_NUM];
		} else {
			eth->rx_desc += 2;
			if (eth->rx_desc >= 256)
				eth->rx_desc = 2 * eth->regs[ETH_TX_BD_NUM];
		}

		/* trigger interrupt */
		if (desc_flg & 0x4000) {
			eth->regs[ETH_INT_SOURCE] |= 0x0002;
			eth_irq(eth);
		}
	}
}

static int eth_reg_read(struct eth *eth, unsigned reg, uint32_t *data)
{
	if (reg < ETH_MAX) {
		*data = eth->regs[reg];
		return 0;
	}

	warn("eth: read invalid reg %d", reg);
	return 1;
}

static int eth_reg_write(struct eth *eth, unsigned reg, uint32_t data)
{
	/* special registers */
	switch (reg) {
	case ETH_MODER:
	{
		uint32_t rising = data & ~eth->regs[reg];

		/* reset bit is not documented */
		if (rising & 0x0800) {
			eth_reset(eth);
		}

		/* update register */
		eth->regs[reg] = data;

		/* transmit descriptors start at zero */
		if (rising & 0x0002) {
			eth->tx_desc = 0;
			eth_try_tx(eth);
		}

		/* receive descriptors follow */
		if (rising & 0x0001) {
			eth->rx_desc = 2 * eth->regs[ETH_TX_BD_NUM];
			eth_try_rx(eth);
		}

		return 0;
	}
	case ETH_INT_SOURCE:
		eth->regs[reg] &= ~data;
		eth_irq(eth);
		return 0;
	case ETH_INT_MASK:
		eth->regs[reg] = data;
		eth_irq(eth);
		return 0;
	case ETH_PACKETLEN:
		eth->regs[reg] = data;
		eth_realloc(eth);
		return 0;
	case ETH_TX_BD_NUM:
		if (data <= 0x80)
			eth->regs[ETH_TX_BD_NUM] = data;
		eth_try_rx(eth);
		return 0;
	case ETH_MIICOMMAND:
	{
		unsigned phynum = (eth->regs[ETH_MIIADDRESS] & 0x001F);
		unsigned phyreg = (eth->regs[ETH_MIIADDRESS] & 0x1F00) >> 8;

		/* PHY write */
		if ((data & 0x04) && (phynum == 1)) {
			phy_write(&eth->phy, phyreg,
				eth->regs[ETH_MIITX_DATA]);
		}

		/* PHY read */
		if (data & 0x02) {
			if (phynum == 1) {
				eth->regs[ETH_MIIRX_DATA] =
					phy_read(&eth->phy, phyreg);
			} else {
				eth->regs[ETH_MIIRX_DATA] = 0xFFFF;
			}
			eth->regs[ETH_MIISTATUS] =
				phy_get_link(&eth->phy) ? 0 : 1;
		}

		return 0;
	}
	case ETH_MIITX_DATA:
	{
		unsigned phynum = (eth->regs[ETH_MIIADDRESS] & 0x001F);
		unsigned phyreg = (eth->regs[ETH_MIIADDRESS] & 0x1F00) >> 8;

		eth->regs[reg] = data & 0xFFFF;
		if (phynum == 1)
			phy_write(&eth->phy, phyreg, eth->regs[reg]);

		return 0;
	}
	case ETH_MIISTATUS:
		/* read-only */
		return 0;
	}

	/* read-write registers */
	if (reg < ETH_MAX) {
		eth->regs[reg] = data;
		return 0;
	}

	warn("eth: write invalid reg %d data %08x", reg, data);
	return 1;
}

static int eth_read32(void *ctx, uint32_t addr, uint32_t *data)
{
	struct eth *eth = (struct eth *)ctx;

	if (addr < 0x0400)
		return eth_reg_read(eth, addr / 4, data);

	if (addr < 0x0800) {
		unsigned idx = (addr & 0x03FF) / 4;
		*data = eth->desc[idx];
		return 0;
	}

	return 1;
}

static int eth_write32(void *ctx, uint32_t addr, uint32_t data)
{
	struct eth *eth = (struct eth *)ctx;

	if (addr < 0x0400)
		return eth_reg_write(eth, addr / 4, data);

	if (addr < 0x0800) {
		unsigned idx = (addr & 0x03FF) / 4;
		eth->desc[idx] = data;
		eth_try_tx(eth);
		return 0;
	}

	return 1;
}

struct bus_ops eth_ops = {
	.read32  = eth_read32,
	.write32 = eth_write32,
};

void eth_tick(struct eth *eth)
{
	eth_try_rx(eth);
}

struct eth *eth_init(struct plic *plic, unsigned irq)
{
	struct eth *eth = calloc(1, sizeof(struct eth));
	if (eth) {
		eth->plic   = plic;
		eth->irqnum = irq;
		pkt_init(&eth->slirp);
		eth_reset(eth);
	}
	return eth;
}
