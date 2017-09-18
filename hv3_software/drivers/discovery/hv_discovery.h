
#define DRIVER_NAME "hv_discovery"
#define PDEBUG(fmt,args...) printk(KERN_DEBUG"%s:"fmt,DRIVER_NAME, ##args)
#define PERR(fmt,args...) printk(KERN_ERR"%s:"fmt,DRIVER_NAME,##args)
#define PINFO(fmt,args...) printk(KERN_INFO"%s:"fmt,DRIVER_NAME, ##args)
#include<linux/init.h>
#include<linux/module.h>
#include<linux/moduleparam.h>
#include<linux/pci.h>
#include<linux/spinlock.h>
#include <linux/pci_ids.h>
#include <linux/slab.h>
#include <linux/delay.h>
#include <linux/edac.h>
#include <linux/mmzone.h>
#include <linux/smp.h>
#include <linux/bitmap.h>
#include <linux/math64.h>
#include <asm/processor.h>
#include <asm/mce.h>
#include <linux/kallsyms.h>
#include <linux/string.h>
#include <asm/e820.h>

/*
 * Maximum number of layers used by the memory controller to uniquely
 * identify a single memory stick.
 * NOTE: Changing this constant requires not only to change the constant
 * below, but also to change the existing code at the core, as there are
 * some code there that are optimized for 3 layers.
*/
#define EDAC_MAX_LAYERS         3

/* EDAC internal operation states */
#define OP_ALLOC                0x100
#define OP_RUNNING_POLL         0x201
#define OP_RUNNING_INTERRUPT    0x202
#define OP_RUNNING_POLL_INTR    0x203
#define OP_OFFLINE              0x300

#define EDAC_DIMM_OFF(layers, nlayers, layer0, layer1, layer2) ({               \
        int __i;                                                        \
        if ((nlayers) == 1)                                             \
                __i = layer0;                                           \
        else if ((nlayers) == 2)                                        \
                __i = (layer1) + ((layers[1]).size * (layer0));         \
        else if ((nlayers) == 3)                                        \
                __i = (layer2) + ((layers[2]).size * ((layer1) +        \
                            ((layers[1]).size * (layer0))));            \
        else                                                            \
                __i = -EINVAL;                                          \
        __i;                                                            \
})

/* Some defs that are not available in 3.10 kernel.
 * sb_edac code is ported from 4.4 (to support up to KNL arch)
 */
#define  MEM_DDR4 	18		/* should be in edac.h enum at kernel 4.4 */
#define  MEM_RDDR4 	19

#define MEM_FLAG_DDR4           BIT(MEM_DDR4)
#define MEM_FLAG_RDDR4          BIT(MEM_RDDR4)

#define PCI_DEVICE_ID_INTEL_SBRIDGE_IMC_RAS     0x3c71  /* 15.1 */
#define PCI_DEVICE_ID_INTEL_SBRIDGE_IMC_ERR0    0x3c72  /* 16.2 */
#define PCI_DEVICE_ID_INTEL_SBRIDGE_IMC_ERR1    0x3c73  /* 16.3 */
#define PCI_DEVICE_ID_INTEL_SBRIDGE_IMC_ERR2    0x3c76  /* 16.6 */
#define PCI_DEVICE_ID_INTEL_SBRIDGE_IMC_ERR3    0x3c77  /* 16.7 */
#define PCI_DEVICE_ID_INTEL_SBRIDGE_IMC_HA0     0x3ca0  /* 14.0 */
#define PCI_DEVICE_ID_INTEL_SBRIDGE_IMC_TA      0x3ca8  /* 15.0 */
#define PCI_DEVICE_ID_INTEL_SBRIDGE_IMC_TAD0    0x3caa  /* 15.2 */
#define PCI_DEVICE_ID_INTEL_SBRIDGE_IMC_TAD1    0x3cab  /* 15.3 */
#define PCI_DEVICE_ID_INTEL_SBRIDGE_IMC_TAD2    0x3cac  /* 15.4 */
#define PCI_DEVICE_ID_INTEL_SBRIDGE_IMC_TAD3    0x3cad  /* 15.5 */
#define PCI_DEVICE_ID_INTEL_SBRIDGE_IMC_DDRIO   0x3cb8
#define PCI_DEVICE_ID_INTEL_SBRIDGE_SAD0        0x3cf4  /* 12.6 */
#define PCI_DEVICE_ID_INTEL_SBRIDGE_BR          0x3cf5  /* 13.6 */
#define PCI_DEVICE_ID_INTEL_SBRIDGE_SAD1        0x3cf6  /* 12.7 */

#define GENMASK_ULL(h, l) \
         (((~0ULL) << (l)) & (~0ULL >> (BITS_PER_LONG_LONG - 1 - (h))))

 #if PAGE_SHIFT < 20
 #define PAGES_TO_MiB(pages)     ((pages) >> (20 - PAGE_SHIFT))
 #define MiB_TO_PAGES(mb)        ((mb) << (20 - PAGE_SHIFT))
 #else                           /* PAGE_SHIFT > 20 */
 #define PAGES_TO_MiB(pages)     ((pages) << (PAGE_SHIFT - 20))
 #define MiB_TO_PAGES(mb)        ((mb) >> (PAGE_SHIFT - 20))
 #endif
