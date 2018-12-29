#include <southbridge/intel/lynxpoint/pch.h>
#include <device/pci_ops.h>
#include "mrc_sku.h"
#include "mrc_misc.h"
#include "mrc_utils.h"
#include <arch/io.h>
#include <arch/pci_ops.h>
#include <console/console.h>

int dummy_func(void)
{
	return 0;
}

void dmi_check_link(void)
{
	if (mrc_sku_type() == 1) {
		uint32_t lcap = RCBA32(0x21a4);
		uint32_t max_link_speed = lcap & 0xf;
		if (max_link_speed == 2) {
			RCBA8_AND_OR(0x21b0, 0xf0, 2);
		}
	}
}

void __attribute((regparm(2))) rtc_wait(void *ram_data, uint16_t val)
{
	uint16_t t1, t2;
	*(uint16_t*)(ram_data + 0xfee) = val;
	outw(val, 0x80);

	do {
		outb(0x48, 0x74);
		t1 = inb(0x75);
		outb(0x49, 0x74);
		t2 = inb(0x75);
		t2 = (t2 << 8) | t1;
	} while (t2 == val);
}

uint8_t __attribute((regparm(1))) pci_setup_bridge(uint8_t bus)
{
	uint8_t a = bus;
	uint8_t r = 0;
	for (int i = 0; i < 0x20; i++) {
		uint32_t dev = PCI_DEV(bus, i, 0);
		uint16_t vid = pci_read_config16(dev, 0);
		if (vid == 0xffff) continue;
		uint16_t cls = pci_read_config16(dev, 0xa);
		if (cls != 0x604) continue; // PCI_CLASS_BRIDGE_PCI
		/* primary bus number */
		pci_write_config8(dev, 0x18, bus);
		a++;
		/* secondary bus number */
		pci_write_config8(dev, 0x19, a);
		/* subordinate bus number */
		pci_write_config8(PCI_DEV(a, i, 0), 0x1a, 0xff);
		pci_write_config16(PCI_DEV(a, 0, 0), 0, 0);
		r = pci_setup_bridge(a);
		pci_write_config8(dev, 0x1a, r);
		a = r;
	}
	return (r == 0)?bus:r;
}

int fcn_fffbd29a(u32 a0, void * a1, void * a2);
int fcn_fffbd29a(u32 a0, void * a1, void * a2)
{
	u32 b1 = (*(u8*)(a1 + 5) >> 3) & 7;
	u32 *wb = (u32*)(a2 + 0xd5);

	switch (b1) {
		case 0:
			*wb = 0x1000;
			return 1;
		case 1:
			*wb = 0x2000;
			return 1;
		case 2:
			*wb = 0x4000;
			return 1;
		case 3:
			*wb = 0x8000;
			return 1;
		case 4:
			*wb = 0x10000;
			return 1;
		default:
			*wb = 0;
			return 0;
	}
}

int fcn_fffaa884(void *ram_data);
int fcn_fffaa884(void *ram_data)
{
	void *bar = *(void**)(ram_data + 0x103b);

	/* this function is not called!!! */
	printk(BIOS_DEBUG, "fcn_fffaa884: bar is %p.\n", bar);

	u32 reg_e4 = read32(bar + 0xe4);
	u32 reg_e8 = read32(bar + 0xe8);
	if (reg_e8 != *(u32*)(ram_data + 0xc))
		return 0x17;

	if (reg_e4 != *(u32*)(ram_data + 8))
		return 0x17;

	void *base = ram_data + 8;
	u16 tmp;

	for (int i = 0; i < 2; i++) {
		int i1 = i * 0x2e6;
		int i2 = i * 0x2fa;
		for (int j = 0; j < 2; j++) {
			void * local_2ch = base + j * 0xfb + i1 + 0xa80 + 0x13;
			int idx2 = j * 0x14f + i2;
			if (*(u32*)((ram_data + idx2 + 0x10c4)) == 1) {
				tmp = 0;
			} else {
				crc16(ram_data + idx2 + 0x115d, 0xb, &tmp);
			}
			if (tmp != *(u16*)(local_2ch + 0xdb))
				return 0x17;
		}
	}
	*(u8*)(ram_data + 0x1742) = 1;
	return 0;
}

int fcn_fffa782c(void *ram_data);
int fcn_fffa782c(void *ram_data)
{
	u32 reg;
	u16 v1, v2;
	u8 wb;

	*(u8*)(ram_data + 0x173f) = 0;
	*(u8*)(ram_data + 0x1740) = *(u8*)(ram_data + 0x102b);
	*(u8*)(ram_data + 0x173c) = *(u8*)(ram_data + 0x16b0);

	void *p1 = *(void**)(ram_data + 0x1047);

	/* p1 is fed84000 (GDXC_BASE_ADDRESS)
	printk(BIOS_DEBUG, "fcn_fffa782c: p1 is %p.\n", p1);
	*/

	reg = read32(p1 + 0x18);
	v1 = reg;
	v2 = reg >> 16;
	if (v1 == 0 && v2 > 1) {
		wb = v2;
	} else {
		wb = *(u8*)(ram_data + 0x16b2);
	}
	*(u8*)(ram_data + 0x173e) = wb;

	reg = read32(p1 + 0x28);
	v1 = reg;
	v2 = reg >> 16;
	if (v1 == 0 && v2 > 1) {
		wb = v2;
	} else {
		wb = *(u8*)(ram_data+ 0x16b1);
	}
	*(u8*)(ram_data + 0x173d) = wb;

	return 0;
}

void __attribute((regparm(2))) fcn_fffb2d76(void *, int);
void __attribute((regparm(3))) fcn_fffb1d24(void *, u32, u32, u32, u32);
void __attribute((regparm(2))) fcn_fffb2062(void *, u8*);

static const u32 gdata1[9] = {
	0x86186186,
	0x18618618,
	0x30c30c30,
	0xa28a28a2,
	0x8a28a28a,
	0x14514514,
	0x28a28a28,
	0x92492492,
	0x24924924
};
static const uint8_t gdata2[12] = {
	0xa1, 0x0c, 0xa1, 0x00, 0x08, 0x0d, 0xef, 0x00, 0x1e, 0x0a, 0xad, 0x00
};

int fcn_fffb8c0b(void *ram_data);
int fcn_fffb8c0b(void *ram_data)
{
	u32 data1[9]; // ebp - 0x3c
	u8 data2[12]; // ebp - 0x48

	/* fcn_fffb8c0b : bar is fed10000. (MCHBAR)
	   printk(BIOS_DEBUG, "%s : bar is %p.\n", __func__, *((void**) (ram_data + 0x103f)));
	   */

	if (*((int32_t*) (ram_data + 0x297c)) == 2 &&
			*((int8_t*) (ram_data + 0x1740)) == 1) {
		void *bar = *((void**) (ram_data + 0x103f));
		bar_update32(bar, 0x5004, 0xfcffffff, 0x1000000);
		fcn_fffb2d76 (ram_data, 0x3c);
	}
	if (*((int32_t*) (ram_data + 0x3cc3)) == 2 &&
			*((int8_t*) (ram_data + 0x1740)) == 1) {
		void *bar = *((void**) (ram_data + 0x103f));
		bar_update32(bar, 0x5008, 0xfcffffff, 0x1000000);
		fcn_fffb2d76 (ram_data, 0x3c);
	}
	memcpy(data1, gdata1, 36);
	memcpy(data2, gdata2, 12);

	fcn_fffb1d24 (ram_data, 0, 0x1010101, 8, 0);
	for (int i = 0; i < 9; i++) {
		fcn_fffb1d24 (ram_data, data1[i], 0x41041041, 6, 8 + i * 6);
	}
	fcn_fffb2062 (ram_data, data2);
	if (*((int32_t*) (ram_data + 0x297c)) == 2) {
		void *bar = *((void**) (ram_data + 0x103f));
		write32(bar + 0x4078, 0xa010102);
	}
	if (*((int32_t*) (ram_data + 0x3cc3)) == 2) {
		void *bar = *((void**) (ram_data + 0x103f));
		write32(bar + 0x4478, 0xa010102);
	}
	return 0;
}
