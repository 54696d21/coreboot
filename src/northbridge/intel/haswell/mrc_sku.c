#include "mrc_sku.h"
#include <southbridge/intel/lynxpoint/pch.h>
#include <device/pci_ops.h>

uint8_t nb_usb2_ports(void)
{
	int t = mrc_sku_type();
	if (t > 2)
		return 0;
	if (t == 1) /* t = 1, not low power */
		return 14;
	else /* t = 2, low power */
		return 8;
}

uint8_t nb_usb3_ports(void)
{
	int t = mrc_sku_type();
	if (t > 2)
		return 0;
	if (t == 1)
		return 8;
	else
		return 6;
}

uint8_t mrc_pch_revision(void)
{
	uint8_t rid = pci_read_config8(PCH_LPC_DEV, 8);
	int sku = mrc_sku_type();
	if (sku == 1) {
		rid -= 2;
		if (rid > 3)
			return 7;
		else
			return rid;
	} else if (sku == 2) {
		rid -= 2;
		if (rid > 2)
			return 7;
		else
			return rid + 4;
	} else {
		return 7;
	}
}

int is_desktop_pch(uint16_t did)
{
	uint16_t t1 = did & 0xfffd;
	if (t1 == 0x8c44 /* Z87, Z85 */ || t1 == 0x8c4c /* Q87, Q85 */)
		return 1;

	if (did == 0x8c5c /* H81 */ || did == 0x8c50 /* B85 */)
		return 1;

	t1 = did & 0xfff7;
	if (t1 == 0x8c42 /* desktop ES or H87 */)
		return 1;

	return 0;
}

int is_mobile_pch(uint16_t did)
{
	return (did == 0x8c4f || did == 0x8c49 ||
			did == 0x8c4b || did == 0x8c41);
}
