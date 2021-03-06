/*
 * This file is part of the coreboot project.
 *
 * Copyright 2016 Google Inc.
 * Copyright (C) 2017-2018 Siemens AG
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; version 2 of the License.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include <console/console.h>
#include <device/mmio.h>
#include <device/device.h>
#include <device/pci_ops.h>
#include <device/pci_ids.h>
#include <hwilib.h>
#include <i210.h>
#include <intelblocks/cpulib.h>
#include <intelblocks/systemagent.h>
#include <soc/pci_devs.h>
#include <string.h>
#include <timer.h>
#include <baseboard/variants.h>
#include <types.h>

#define MAX_PATH_DEPTH		12
#define MAX_NUM_MAPPINGS	10

#define BIOS_MAILBOX_WAIT_MAX_MS	1000
#define BIOS_MAILBOX_DATA		0x7080
#define BIOS_MAILBOX_INTERFACE		0x7084
#define  RUN_BUSY_STS			(1 << 31)

/*
 * SPI Opcode Menu setup for SPIBAR lock down
 * should support most common flash chips.
 */
#define SPI_OPMENU_0 0x01 /* WRSR: Write Status Register */
#define SPI_OPTYPE_0 0x01 /* Write, no address */

#define SPI_OPMENU_1 0x02 /* PP: Page Program */
#define SPI_OPTYPE_1 0x03 /* Write, address required */

#define SPI_OPMENU_2 0x03 /* READ: Read Data */
#define SPI_OPTYPE_2 0x02 /* Read, address required */

#define SPI_OPMENU_3 0x05 /* RDSR: Read Status Register */
#define SPI_OPTYPE_3 0x00 /* Read, no address */

#define SPI_OPMENU_4 0x20 /* SE20: Sector Erase 0x20 */
#define SPI_OPTYPE_4 0x03 /* Write, address required */

#define SPI_OPMENU_5 0x9f /* RDID: Read ID */
#define SPI_OPTYPE_5 0x00 /* Read, no address */

#define SPI_OPMENU_6 0xd8 /* BED8: Block Erase 0xd8 */
#define SPI_OPTYPE_6 0x03 /* Write, address required */

#define SPI_OPMENU_7 0x0b /* FAST: Fast Read */
#define SPI_OPTYPE_7 0x02 /* Read, address required */

#define SPI_OPMENU_UPPER ((SPI_OPMENU_7 << 24) | (SPI_OPMENU_6 << 16) | \
			  (SPI_OPMENU_5 << 8) | SPI_OPMENU_4)
#define SPI_OPMENU_LOWER ((SPI_OPMENU_3 << 24) | (SPI_OPMENU_2 << 16) | \
			  (SPI_OPMENU_1 << 8) | SPI_OPMENU_0)

#define SPI_OPTYPE	((SPI_OPTYPE_7 << 14) | (SPI_OPTYPE_6 << 12) | \
			 (SPI_OPTYPE_5 << 10) | (SPI_OPTYPE_4 << 8) | \
			 (SPI_OPTYPE_3 << 6) | (SPI_OPTYPE_2 << 4) | \
			 (SPI_OPTYPE_1 << 2) | (SPI_OPTYPE_0))

#define SPI_OPPREFIX	((0x50 << 8) | 0x06) /* EWSR and WREN */

#define SPIBAR_OFFSET		0x3800
#define SPI_REG_PREOP_OPTYPE	0xa4
#define SPI_REG_OPMENU_L	0xa8
#define SPI_REG_OPMENU_H	0xac

#define SD_CAP_BYP		0x810
#define  SD_CAP_BYP_EN		0x5A
#define SD_CAP_BYP_REG1		0x814

/** \brief This function can decide if a given MAC address is valid or not.
 *         Currently, addresses filled with 0xff or 0x00 are not valid.
 * @param  mac  Buffer to the MAC address to check
 * @return 0    if address is not valid, otherwise 1
 */
static uint8_t is_mac_adr_valid(uint8_t mac[6])
{
	uint8_t buf[6];

	memset(buf, 0, sizeof(buf));
	if (!memcmp(buf, mac, sizeof(buf)))
		return 0;
	memset(buf, 0xff, sizeof(buf));
	if (!memcmp(buf, mac, sizeof(buf)))
		return 0;
	return 1;
}

/** \brief This function will search for a MAC address which can be assigned
 *         to a MACPHY.
 * @param  dev     pointer to PCI device
 * @param  mac     buffer where to store the MAC address
 * @return cb_err  CB_ERR or CB_SUCCESS
 */
enum cb_err mainboard_get_mac_address(struct device *dev, uint8_t mac[6])
{
	struct bus *parent = dev->bus;
	uint8_t buf[16], mapping[16], i = 0, chain_len = 0;

	memset(buf, 0, sizeof(buf));
	memset(mapping, 0, sizeof(mapping));

	/* The first entry in the tree is the device itself. */
	buf[0] = dev->path.pci.devfn;
	chain_len = 1;
	for (i = 1; i < MAX_PATH_DEPTH && parent->dev->bus->subordinate; i++) {
		buf[i] = parent->dev->path.pci.devfn;
		chain_len++;
		parent = parent->dev->bus;
	}
	if (i == MAX_PATH_DEPTH) {
		/* The path is deeper than MAX_PATH_DEPTH devices, error. */
		printk(BIOS_ERR, "Too many bridges for %s\n", dev_path(dev));
		return CB_ERR;
	}
	/*
	 * Now construct the mapping based on the device chain starting from
	 * root bridge device to the device itself.
	 */
	mapping[0] = 1;
	mapping[1] = chain_len;
	for (i = 0; i < chain_len; i++)
		mapping[i + 4] = buf[chain_len - i - 1];

	/* Open main hwinfo block */
	if (hwilib_find_blocks("hwinfo.hex") != CB_SUCCESS)
		return CB_ERR;
	/* Now try to find a valid MAC address in hwinfo for this mapping.*/
	for (i = 0; i < MAX_NUM_MAPPINGS; i++) {
		if ((hwilib_get_field(XMac1Mapping + i, buf, 16) == 16) &&
			!(memcmp(buf, mapping, chain_len + 4))) {
		/* There is a matching mapping available, get MAC address. */
			if ((hwilib_get_field(XMac1 + i, mac, 6) == 6) &&
			    (is_mac_adr_valid(mac))) {
				return CB_SUCCESS;
			} else {
				return CB_ERR;
			}
		} else
			continue;
	}
	/* No MAC address found for */
	return CB_ERR;
}

/** \brief This function fixes an accuracy issue with IDT PMIC.
 *         The current reported system power consumption is higher than the
 *         actual consumption. With a correction of slope and offset for Vcc
 *         and Vnn, the issue is solved.
 */
static void config_pmic_imon(void)
{
	struct stopwatch sw;
	uint32_t power_max;

	printk(BIOS_DEBUG, "PMIC: Configure PMIC IMON - Start\n");

	/* Calculate CPU TDP in mW */
	power_max = cpu_get_power_max();
	printk(BIOS_INFO, "PMIC: CPU TDP %d mW.\n", power_max);

	/*
	 * Fix Vnn slope and offset value.
	 * slope  = 0x4a4  # 2.32
	 * offset = 0xfa0d # -2.975
	 */
	stopwatch_init_msecs_expire(&sw, BIOS_MAILBOX_WAIT_MAX_MS);
	/* Read P_CR_BIOS_MAILBOX_INTERFACE_0_0_0_MCHBAR and check RUN_BUSY. */
	while ((MCHBAR32(BIOS_MAILBOX_INTERFACE) & RUN_BUSY_STS)) {
		if (stopwatch_expired(&sw)) {
			printk(BIOS_ERR, "PMIC: Power consumption measurement "
					"setup fails for Vnn.\n");
			return;
		}
	}
	/* Set Vnn values into P_CR_BIOS_MAILBOX_DATA_0_0_0_MCHBAR. */
	MCHBAR32(BIOS_MAILBOX_DATA) = 0xfa0d04a4;
	/* Set command, address and busy bit. */
	MCHBAR32(BIOS_MAILBOX_INTERFACE) = 0x8000011d;
	printk(BIOS_DEBUG, "PMIC: Fix Vnn slope and offset value.\n");

	/*
	 * Fix Vcc slope and offset value.
	 * Premium and High SKU:
	 * slope  = 0x466  # 2.2
	 * offset = 0xe833 # -11.9
	 * Low and Intermediate SKU:
	 * slope  = 0x3b3  # 1.85
	 * offset = 0xed33 # -9.4
	 */
	stopwatch_init_msecs_expire(&sw, BIOS_MAILBOX_WAIT_MAX_MS);
	while ((MCHBAR32(BIOS_MAILBOX_INTERFACE) & RUN_BUSY_STS)) {
		if (stopwatch_expired(&sw)) {
			printk(BIOS_ERR, "PMIC: Power consumption measurement "
					"setup fails for Vcc.\n");
			return;
		}
	}

	/*
	 * CPU TDP limit between Premium/High and Low/Intermediate SKU
	 * is 9010 mW.
	 */
	if (power_max > 9010) {
		MCHBAR32(BIOS_MAILBOX_DATA) = 0xe8330466;
		MCHBAR32(BIOS_MAILBOX_INTERFACE) = 0x8000001d;
		printk(BIOS_INFO, "PMIC: Fix Vcc for Premium SKU.\n");
	} else {
		MCHBAR32(BIOS_MAILBOX_DATA) = 0xed3303b3;
		MCHBAR32(BIOS_MAILBOX_INTERFACE) = 0x8000001d;
		printk(BIOS_INFO, "PMIC: Fix Vcc for Low SKU.\n");
	}

	printk(BIOS_DEBUG, "PMIC: Configure PMIC IMON - End\n");
}

static void mainboard_init(void *chip_info)
{
	const struct pad_config *pads;
	size_t num;

	pads = variant_gpio_table(&num);
	gpio_configure_pads(pads, num);

	config_pmic_imon();
}

static void mainboard_final(void *chip_info)
{
	uint16_t cmd = 0;
	struct device *dev = NULL;
	void *spi_base = NULL;

	/* Do board specific things */
	variant_mainboard_final();

	/* Set Master Enable for on-board PCI device. */
	dev = dev_find_device(PCI_VENDOR_ID_SIEMENS, 0x403f, 0);
	if (dev) {
		cmd = pci_read_config16(dev, PCI_COMMAND);
		cmd |= PCI_COMMAND_MASTER;
		pci_write_config16(dev, PCI_COMMAND, cmd);
	}
	/* Set up SPI OPCODE menu before the controller is locked. */
	dev = PCH_DEV_SPI;
	spi_base = (void *)pci_read_config32(dev, PCI_BASE_ADDRESS_0);
	if (!spi_base)
		return;
	write32((spi_base + SPI_REG_PREOP_OPTYPE),
			((SPI_OPTYPE << 16) | SPI_OPPREFIX));
	write32((spi_base + SPI_REG_OPMENU_L), SPI_OPMENU_LOWER);
	write32((spi_base + SPI_REG_OPMENU_H), SPI_OPMENU_UPPER);

	/* Set SD-Card speed to HS mode only. */
	dev = pcidev_path_on_root(PCH_DEVFN_SDCARD);
	if (dev) {
		uint32_t reg;
		struct resource *res = find_resource(dev, PCI_BASE_ADDRESS_0);
		if (!res)
			return;

		write32(res2mmio(res, SD_CAP_BYP, 0), SD_CAP_BYP_EN);
		reg = read32(res2mmio(res, SD_CAP_BYP_REG1, 0));
		/* Disable all UHS-I SD-Card speed modes, keep only HS mode. */
		reg &= ~0x2000f800;
		write32(res2mmio(res, SD_CAP_BYP_REG1, 0), reg);
	}
}

/* The following function performs board specific things. */
void __weak variant_mainboard_final(void)
{
}

struct chip_operations mainboard_ops = {
	.init = mainboard_init,
	.final = mainboard_final,
};
