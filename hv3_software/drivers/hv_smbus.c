/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * *
 *                                                                       *
 *    Copyright (c) 2016 Netlist Inc.                                    *
 *    All rights reserved.                                               *
 *                                                                       *
 *                                                                       *
 * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */

#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/kernel.h>
#include <linux/fs.h>
#include <linux/blkdev.h>
#include <linux/hdreg.h>
#include <linux/debugfs.h>
#include <linux/module.h>
#include <linux/io.h>
#include <linux/pci.h>
#include "hv_params.h"

#include <stdio.h>
#include <string.h>

#include "hv_mmio.h"
#include "hv_cmd.h"

/* Addr from 0x8086/0x6F28[90h] converted to virtual addr */
//unsigned int smb_vbase;

unsigned int get_smb_vbase(void);
int smb_read_dword(int bus, int dev, int func, int off);
int smb_write_dword(int bus, int dev, int func, int off, int data);


void show_pci_config_regs(int bus, int dev, int func)
{
	int i, j;
	struct pci_dev *pdev = NULL;
	unsigned char regb[16];

	/* Finding the device by Vendor/Device ID Pair */
	pdev = pci_get_device(0x8086, 0x6F28, pdev);
	if (pdev == NULL) {
		pr_mmio("[%s]: 0x8086-0x6F28 not found\n", __func__);
	} else {

		pr_mmio("[%s]: Intel PCI Device (0, 5, 0) Config Space\n", __func__);
		for (i=0; i < 16; i++)
		{
			for (j=0; j < 16; j++)
			{
				pci_read_config_byte(pdev, i*16+j, &regb[j]);
			}
			pr_mmio("%.2x: %.2x %.2x %.2x %.2x %.2x %.2x %.2x %.2x %.2x %.2x %.2x %.2x %.2x %.2x %.2x %.2x\n",
					i*16, regb[0], regb[1], regb[2], regb[3], regb[4], regb[5], regb[6], regb[7],
					regb[8], regb[9], regb[10], regb[11], regb[12], regb[13], regb[14], regb[15]);
		}

	}

}

unsigned int get_smb_vbase(void)
{
	struct pci_dev *pdev = NULL;
	unsigned int reg_dword;

	pdev = pci_get_device(0x8086, 0x6F28, pdev);
	if (pdev == NULL) {
		pr_debug("[%s]: 0x8086-0x6F28 not found\n", __func__);
	} else {
		pci_read_config_dword(pdev, 0x90, &reg_dword);
	}

	return(phys_to_virt(reg_dword & 0xFFFFFFFF));
}

int smb_read_dword(int bus, int dev, int func, int off)
{
	unsigned int smb_vbase, offset, value;

	smb_vbase = get_smb_vbase();
	offset = (unsigned int) ((bus<<20) + (dev<<15) + (func<<12) + off);
	value = *(unsigned int *) (smb_vbase + offset);

	pr_debug("[%s]: bus=%d dev=%d func=%d off=%d value=0x%.8x\n",
			__func__, bus, dev, func, off, value);
//	return(*(unsigned int *) (smb_vbase + offset));
	return(value);
}

int smb_write_dword(int bus, int dev, int func, int off, int data)
{
	unsigned int value, smb_vbase, offset;

	smb_vbase = get_smb_vbase();
	offset = (unsigned int) ((bus<<20) + (dev<<15) + (func<<12) + off);
	*(unsigned int *) (smb_vbase + offset) = data;
	value = *(unsigned int *) (smb_vbase + offset);
	return(value);
}
