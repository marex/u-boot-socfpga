// SPDX-License-Identifier: GPL-2.0+
/*
 *  Copyright (C) 2012 Altera Corporation <www.altera.com>
 */

#include <common.h>
#include <asm/io.h>
#include <asm/pl310.h>
#include <asm/u-boot.h>
#include <asm/utils.h>
#include <image.h>
#include <malloc.h>
#include <asm/arch/reset_manager.h>
#include <spl.h>
#include <asm/arch/system_manager.h>
#include <asm/arch/freeze_controller.h>
#include <asm/arch/clock_manager.h>
#include <asm/arch/scan_manager.h>
#include <asm/arch/sdram.h>
#include <asm/arch/scu.h>
#include <asm/arch/misc.h>
#include <asm/arch/nic301.h>
#include <asm/sections.h>
#include <fdtdec.h>
#include <watchdog.h>
#include <asm/arch/pinmux.h>
#include <asm/arch/fpga_manager.h>

DECLARE_GLOBAL_DATA_PTR;

static const struct socfpga_system_manager *sysmgr_regs =
	(struct socfpga_system_manager *)SOCFPGA_SYSMGR_ADDRESS;

u32 spl_boot_device(void)
{
	const u32 bsel = readl(&sysmgr_regs->bootinfo);

	switch (SYSMGR_GET_BOOTINFO_BSEL(bsel)) {
	case 0x1:	/* FPGA (HPS2FPGA Bridge) */
		return BOOT_DEVICE_RAM;
	case 0x2:	/* NAND Flash (1.8V) */
	case 0x3:	/* NAND Flash (3.0V) */
		socfpga_per_reset(SOCFPGA_RESET(NAND), 0);
		return BOOT_DEVICE_NAND;
	case 0x4:	/* SD/MMC External Transceiver (1.8V) */
	case 0x5:	/* SD/MMC Internal Transceiver (3.0V) */
		socfpga_per_reset(SOCFPGA_RESET(SDMMC), 0);
		socfpga_per_reset(SOCFPGA_RESET(DMA), 0);
		return BOOT_DEVICE_MMC1;
	case 0x6:	/* QSPI Flash (1.8V) */
	case 0x7:	/* QSPI Flash (3.0V) */
		socfpga_per_reset(SOCFPGA_RESET(QSPI), 0);
		return BOOT_DEVICE_SPI;
	default:
		printf("Invalid boot device (bsel=%08x)!\n", bsel);
		hang();
	}
}

#ifdef CONFIG_SPL_MMC_SUPPORT
u32 spl_boot_mode(const u32 boot_device)
{
#if defined(CONFIG_SPL_FAT_SUPPORT) || defined(CONFIG_SPL_EXT_SUPPORT)
	return MMCSD_MODE_FS;
#else
	return MMCSD_MODE_RAW;
#endif
}
#endif

static void spl_init_ddr_dram(void)
{
	DECLARE_GLOBAL_DATA_PTR;

	config_pins(gd->fdt_blob, "shared");
	ddr_calibration_sequence();
}

void spl_board_init(void)
{
	/* enable console uart printing */
	preloader_console_init();
	WATCHDOG_RESET();

	arch_early_init_r();

	/* If the FPGA is already loaded, ie. from EPCQ, start DDR DRAM */
	if (is_fpgamgr_early_user_mode())
		spl_init_ddr_dram();
}

void board_init_f(ulong dummy)
{
	socfpga_init_security_policies();
	socfpga_sdram_remap_zero();

	/* Assert reset to all except L4WD0 and L4TIMER0 */
	socfpga_per_reset_all();
	socfpga_watchdog_disable();

	spl_early_init();

	/* Configure the clock based on handoff */
	cm_basic_init(gd->fdt_blob);

#ifdef CONFIG_HW_WATCHDOG
	/* release osc1 watchdog timer 0 from reset */
	socfpga_reset_deassert_osc1wd0();

	/* reconfigure and enable the watchdog */
	hw_watchdog_init();
	WATCHDOG_RESET();
#endif /* CONFIG_HW_WATCHDOG */

	config_dedicated_pins(gd->fdt_blob);
	WATCHDOG_RESET();
}

#ifdef CONFIG_SPL_LOAD_FIT
int board_fit_config_name_match(const char *name)
{
	/* Just empty function now - can't decide what to choose */
	debug("%s: %s\n", __func__, name);

	return 0;
}

static char buf[32 * 1024] __aligned(4);

int spl_load_fpga_image(struct spl_load_info *info, size_t length,
			int nr_sectors, int sector_offset)
{
	u32 csize, step = sizeof(buf) / info->bl_len;
	int i, ret;

	ret = fpgamgr_program_init((u32 *)buf, length);
	if (ret) {
		printf("FPGA: Init with periph rbf failed with error.");
		printf("code %d\n", ret);
		return -EPERM;
	}

	for (i = 0; i < nr_sectors; i += step) {
		csize = min(sizeof(buf), length);

		if (info->read(info, sector_offset,
			       step, (void *)buf) != step) {
			return -EIO;
		}

		fpgamgr_program_write((void *)buf, csize);
		sector_offset += step;
		length -= csize;
	}

	if (fpgamgr_wait_early_user_mode() != -ETIMEDOUT)
		puts("FPGA: Early Release Succeeded.\n");
	else {
		puts("FPGA: Failed to see Early Release.\n");
		return -EIO;
	}

	spl_init_ddr_dram();

	return 0;
}

struct image_header *spl_get_load_buffer(int offset, size_t size)
{
	struct image_header *mem = memalign(4, size);

	if (!mem)
		hang();

	return mem;
}
#endif
