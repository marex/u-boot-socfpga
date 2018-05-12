// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2016-2017 Intel Corporation
 */

#include <altera.h>
#include <common.h>
#include <errno.h>
#include <fdtdec.h>
#include <miiphy.h>
#include <netdev.h>
#include <ns16550.h>
#include <watchdog.h>
#include <asm/arch/misc.h>
#include <asm/arch/pinmux.h>
#include <asm/arch/reset_manager.h>
#include <asm/arch/sdram_arria10.h>
#include <asm/arch/system_manager.h>
#include <asm/arch/nic301.h>
#include <asm/io.h>
#include <asm/pl310.h>

#define PINMUX_UART0_TX_SHARED_IO_OFFSET_Q1_3	0x08
#define PINMUX_UART0_TX_SHARED_IO_OFFSET_Q2_11	0x58
#define PINMUX_UART0_TX_SHARED_IO_OFFSET_Q3_3	0x68
#define PINMUX_UART1_TX_SHARED_IO_OFFSET_Q1_7	0x18
#define PINMUX_UART1_TX_SHARED_IO_OFFSET_Q3_7	0x78
#define PINMUX_UART1_TX_SHARED_IO_OFFSET_Q4_3	0x98

#if defined(CONFIG_SPL_BUILD)
static struct pl310_regs *const pl310 =
	(struct pl310_regs *)CONFIG_SYS_PL310_BASE;
static const struct socfpga_noc_fw_ocram *noc_fw_ocram_base =
	(void *)SOCFPGA_SDR_FIREWALL_OCRAM_ADDRESS;
#endif

static struct socfpga_system_manager *sysmgr_regs =
	(struct socfpga_system_manager *)SOCFPGA_SYSMGR_ADDRESS;

/*
 * DesignWare Ethernet initialization
 */
#ifdef CONFIG_ETH_DESIGNWARE
static void arria10_dwmac_reset(const u8 of_reset_id, const u8 phymode)
{
	u32 reset;

	if (of_reset_id == EMAC0_RESET) {
		reset = SOCFPGA_RESET(EMAC0);
	} else if (of_reset_id == EMAC1_RESET) {
		reset = SOCFPGA_RESET(EMAC1);
	} else if (of_reset_id == EMAC2_RESET) {
		reset = SOCFPGA_RESET(EMAC2);
	} else {
		printf("GMAC: Invalid reset ID (%i)!\n", of_reset_id);
		return;
	}

	clrsetbits_le32(&sysmgr_regs->emac[of_reset_id - EMAC0_RESET],
			SYSMGR_EMACGRP_CTRL_PHYSEL_MASK,
			phymode);

	/* Release the EMAC controller from reset */
	socfpga_per_reset(reset, 0);
}

static int socfpga_eth_reset(void)
{
	/* Put all GMACs into RESET state. */
	socfpga_per_reset(SOCFPGA_RESET(EMAC0), 1);
	socfpga_per_reset(SOCFPGA_RESET(EMAC1), 1);
	socfpga_per_reset(SOCFPGA_RESET(EMAC2), 1);
	return socfpga_eth_reset_common(arria10_dwmac_reset);
};
#else
static int socfpga_eth_reset(void)
{
	return 0;
};
#endif

#if defined(CONFIG_SPL_BUILD)
/*
+ * This function initializes security policies to be consistent across
+ * all logic units in the Arria 10.
+ *
+ * The idea is to set all security policies to be normal, nonsecure
+ * for all units.
+ */
static void initialize_security_policies(void)
{
	/* Put OCRAM in non-secure */
	writel(0x003f0000, &noc_fw_ocram_base->region0);
	writel(0x1, &noc_fw_ocram_base->enable);
}

int arch_early_init_r(void)
{
	initialize_security_policies();

	/* Configure the L2 controller to make SDRAM start at 0 */
	writel(0x1, &pl310->pl310_addr_filter_start);

	/* assert reset to all except L4WD0 and L4TIMER0 */
	socfpga_per_reset_all();

	return 0;
}
#else
int arch_early_init_r(void)
{
	return 0;
}
#endif

/*
 * Print CPU information
 */
#if defined(CONFIG_DISPLAY_CPUINFO)
int print_cpuinfo(void)
{
	const u32 bsel =
		SYSMGR_GET_BOOTINFO_BSEL(readl(&sysmgr_regs->bootinfo));

	puts("CPU:   Altera SoCFPGA Arria 10\n");

	printf("BOOT:  %s\n", bsel_str[bsel].name);
	return 0;
}
#endif

#ifdef CONFIG_ARCH_MISC_INIT
int arch_misc_init(void)
{
	return socfpga_eth_reset();
}
#endif

#ifndef CONFIG_SPL_BUILD
static int do_bridge(cmd_tbl_t *cmdtp, int flag, int argc, char * const argv[])
{
	if (argc != 2)
		return CMD_RET_USAGE;

	argv++;

	switch (*argv[0]) {
	case 'e':	/* Enable */
		socfpga_reset_deassert_bridges_handoff();
		break;
	case 'd':	/* Disable */
		socfpga_bridges_reset();
		break;
	default:
		return CMD_RET_USAGE;
	}

	return 0;
}

U_BOOT_CMD(
	bridge, 2, 1, do_bridge,
	"SoCFPGA HPS FPGA bridge control",
	"enable  - Enable HPS-to-FPGA, FPGA-to-HPS, LWHPS-to-FPGA bridges\n"
	"bridge disable - Enable HPS-to-FPGA, FPGA-to-HPS, LWHPS-to-FPGA bridges\n"
	""
);
#endif
