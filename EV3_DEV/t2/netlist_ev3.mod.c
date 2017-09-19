#include <linux/module.h>
#include <linux/vermagic.h>
#include <linux/compiler.h>

MODULE_INFO(vermagic, VERMAGIC_STRING);

__visible struct module __this_module
__attribute__((section(".gnu.linkonce.this_module"))) = {
	.name = KBUILD_MODNAME,
	.init = init_module,
#ifdef CONFIG_MODULE_UNLOAD
	.exit = cleanup_module,
#endif
	.arch = MODULE_ARCH_INIT,
};

static const struct modversion_info ____versions[]
__used
__attribute__((section("__versions"))) = {
	{ 0xb7893099, __VMLINUX_SYMBOL_STR(module_layout) },
	{ 0x5e25804, __VMLINUX_SYMBOL_STR(__request_region) },
	{ 0xbce028d5, __VMLINUX_SYMBOL_STR(part_round_stats) },
	{ 0x460bcde2, __VMLINUX_SYMBOL_STR(kmalloc_caches) },
	{ 0xef2a7999, __VMLINUX_SYMBOL_STR(pci_bus_read_config_byte) },
	{ 0xd2b09ce5, __VMLINUX_SYMBOL_STR(__kmalloc) },
	{ 0xc7af21c8, __VMLINUX_SYMBOL_STR(alloc_disk) },
	{ 0x36f2ec8b, __VMLINUX_SYMBOL_STR(blk_cleanup_queue) },
	{ 0x6bf1c17f, __VMLINUX_SYMBOL_STR(pv_lock_ops) },
	{ 0x4afc9c0f, __VMLINUX_SYMBOL_STR(param_ops_int) },
	{ 0x6c09c2a4, __VMLINUX_SYMBOL_STR(del_timer) },
	{ 0xc364ae22, __VMLINUX_SYMBOL_STR(iomem_resource) },
	{ 0x17011151, __VMLINUX_SYMBOL_STR(blk_queue_max_hw_sectors) },
	{ 0x43a53735, __VMLINUX_SYMBOL_STR(__alloc_workqueue_key) },
	{ 0xd2d1927b, __VMLINUX_SYMBOL_STR(hrtimer_forward) },
	{ 0xbf9cb3e2, __VMLINUX_SYMBOL_STR(pci_disable_device) },
	{ 0xe418fde4, __VMLINUX_SYMBOL_STR(hrtimer_cancel) },
	{ 0xd9d3bcd3, __VMLINUX_SYMBOL_STR(_raw_spin_lock_bh) },
	{ 0xc87c1f84, __VMLINUX_SYMBOL_STR(ktime_get) },
	{ 0xeae3dfd6, __VMLINUX_SYMBOL_STR(__const_udelay) },
	{ 0x9580deb, __VMLINUX_SYMBOL_STR(init_timer_key) },
	{ 0x7a2af7b4, __VMLINUX_SYMBOL_STR(cpu_number) },
	{ 0xad698c9b, __VMLINUX_SYMBOL_STR(kthread_create_on_node) },
	{ 0x7d11c268, __VMLINUX_SYMBOL_STR(jiffies) },
	{ 0x9e88526, __VMLINUX_SYMBOL_STR(__init_waitqueue_head) },
	{ 0xc671e369, __VMLINUX_SYMBOL_STR(_copy_to_user) },
	{ 0x9b048d63, __VMLINUX_SYMBOL_STR(blk_queue_max_segments) },
	{ 0x706d051c, __VMLINUX_SYMBOL_STR(del_timer_sync) },
	{ 0xa7b1507, __VMLINUX_SYMBOL_STR(hrtimer_start_range_ns) },
	{ 0xc69f89ec, __VMLINUX_SYMBOL_STR(pci_enable_pcie_error_reporting) },
	{ 0xfbd306db, __VMLINUX_SYMBOL_STR(blk_alloc_queue) },
	{ 0xdd7bafd, __VMLINUX_SYMBOL_STR(current_task) },
	{ 0x27e1a049, __VMLINUX_SYMBOL_STR(printk) },
	{ 0x6e5989, __VMLINUX_SYMBOL_STR(kthread_stop) },
	{ 0xdf5b3f10, __VMLINUX_SYMBOL_STR(del_gendisk) },
	{ 0xa1c76e0a, __VMLINUX_SYMBOL_STR(_cond_resched) },
	{ 0x16305289, __VMLINUX_SYMBOL_STR(warn_slowpath_null) },
	{ 0x6ad9be4e, __VMLINUX_SYMBOL_STR(pci_bus_write_config_dword) },
	{ 0x8c03d20c, __VMLINUX_SYMBOL_STR(destroy_workqueue) },
	{ 0x71a50dbc, __VMLINUX_SYMBOL_STR(register_blkdev) },
	{ 0x1bb31047, __VMLINUX_SYMBOL_STR(add_timer) },
	{ 0xd6b8e852, __VMLINUX_SYMBOL_STR(request_threaded_irq) },
	{ 0x952664c5, __VMLINUX_SYMBOL_STR(do_exit) },
	{ 0x2f38953c, __VMLINUX_SYMBOL_STR(bio_endio) },
	{ 0xd1fbb15f, __VMLINUX_SYMBOL_STR(pci_find_capability) },
	{ 0xb5a459dc, __VMLINUX_SYMBOL_STR(unregister_blkdev) },
	{ 0xd9141906, __VMLINUX_SYMBOL_STR(arch_dma_alloc_attrs) },
	{ 0x36d5d98, __VMLINUX_SYMBOL_STR(blk_queue_bounce_limit) },
	{ 0x78764f4e, __VMLINUX_SYMBOL_STR(pv_irq_ops) },
	{ 0x42c8de35, __VMLINUX_SYMBOL_STR(ioremap_nocache) },
	{ 0xa5d9895f, __VMLINUX_SYMBOL_STR(pci_bus_read_config_word) },
	{ 0xe3918d97, __VMLINUX_SYMBOL_STR(blk_queue_make_request) },
	{ 0x1cc8e7eb, __VMLINUX_SYMBOL_STR(pci_bus_read_config_dword) },
	{ 0xbba70a2d, __VMLINUX_SYMBOL_STR(_raw_spin_unlock_bh) },
	{ 0xdb7305a1, __VMLINUX_SYMBOL_STR(__stack_chk_fail) },
	{ 0x1000e51, __VMLINUX_SYMBOL_STR(schedule) },
	{ 0x47fe5d89, __VMLINUX_SYMBOL_STR(put_disk) },
	{ 0xf1e4eb3a, __VMLINUX_SYMBOL_STR(wake_up_process) },
	{ 0xbdfb6dbb, __VMLINUX_SYMBOL_STR(__fentry__) },
	{ 0x8d15114a, __VMLINUX_SYMBOL_STR(__release_region) },
	{ 0x3018dfb9, __VMLINUX_SYMBOL_STR(pci_enable_msi_range) },
	{ 0x792d8100, __VMLINUX_SYMBOL_STR(pci_unregister_driver) },
	{ 0x52cb3db6, __VMLINUX_SYMBOL_STR(kmem_cache_alloc_trace) },
	{ 0xe259ae9e, __VMLINUX_SYMBOL_STR(_raw_spin_lock) },
	{ 0xb19a5453, __VMLINUX_SYMBOL_STR(__per_cpu_offset) },
	{ 0xa6bbd805, __VMLINUX_SYMBOL_STR(__wake_up) },
	{ 0xb3f7646e, __VMLINUX_SYMBOL_STR(kthread_should_stop) },
	{ 0x2207a57f, __VMLINUX_SYMBOL_STR(prepare_to_wait_event) },
	{ 0x7eeb718c, __VMLINUX_SYMBOL_STR(pci_bus_write_config_byte) },
	{ 0x1e047854, __VMLINUX_SYMBOL_STR(warn_slowpath_fmt) },
	{ 0x37a0cba, __VMLINUX_SYMBOL_STR(kfree) },
	{ 0x6df1aaf1, __VMLINUX_SYMBOL_STR(kernel_sigaction) },
	{ 0x38d68fc3, __VMLINUX_SYMBOL_STR(pci_disable_msi) },
	{ 0x1ec0070a, __VMLINUX_SYMBOL_STR(dma_supported) },
	{ 0xcc22fcf4, __VMLINUX_SYMBOL_STR(blk_queue_dma_alignment) },
	{ 0x3484211f, __VMLINUX_SYMBOL_STR(add_disk) },
	{ 0x83ba5fbb, __VMLINUX_SYMBOL_STR(hrtimer_init) },
	{ 0xedc03953, __VMLINUX_SYMBOL_STR(iounmap) },
	{ 0x548f1925, __VMLINUX_SYMBOL_STR(__pci_register_driver) },
	{ 0xf08242c2, __VMLINUX_SYMBOL_STR(finish_wait) },
	{ 0x28318305, __VMLINUX_SYMBOL_STR(snprintf) },
	{ 0x848b0480, __VMLINUX_SYMBOL_STR(blk_queue_max_segment_size) },
	{ 0x4d5d97c6, __VMLINUX_SYMBOL_STR(pci_enable_device) },
	{ 0xb5419b40, __VMLINUX_SYMBOL_STR(_copy_from_user) },
	{ 0xc6361f41, __VMLINUX_SYMBOL_STR(pci_find_ext_capability) },
	{ 0x57550c39, __VMLINUX_SYMBOL_STR(dma_ops) },
	{ 0xf20dabd8, __VMLINUX_SYMBOL_STR(free_irq) },
};

static const char __module_depends[]
__used
__attribute__((section(".modinfo"))) =
"depends=";

MODULE_ALIAS("pci:v00001C1Bd00000004sv*sd*bc*sc*i*");
MODULE_ALIAS("pci:v00001C1Bd00000005sv*sd*bc*sc*i*");
MODULE_ALIAS("pci:v00001C1Bd00000006sv*sd*bc*sc*i*");

MODULE_INFO(srcversion, "253DDDCEEA1DA4DCE7A1028");
