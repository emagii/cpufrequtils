/*
 * test module to dump contents of the ACPI tables relevant for
 * CPU frequency scaling support. Kernel 2.6.3 or later required.
 *
 * Partly based on a patch for ACPI-PentiumM-cpufreq interaction by David
 * Moore, sent to the cpufreq list in June 2003, and on various stuff to
 * be found in drivers/acpi/processor.c and include/acpi/processor.h,
 * (C) 2001, 2002 Andy Grover <andrew.grover@intel.com> and 
 * (C) 2001, 2002 Paul Diefenbaugh <paul.s.diefenbaugh@intel.com>
 *
 * (C) 2003 - 2004	Dominik Brodowski
 *
 * To use:
 * 1.) ACPI_PROCESSOR must (at least) be built as a module, and
 * CONFIG_CPU_FREQ must be defined in the kernel sources. Do not load
 * any cpufreq driver, though.
 *
 * 2.) load this module
 *
 * 3.) if applicable: load this module with the "pdc=" parameters
 *     to tell the BIOS of the cpufreq capabilities of the Operating
 *     System.
 *
 * 4.) run dmesg to find the output
 * 
 */

#include <linux/config.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/init.h>
#include <linux/cpufreq.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>
#include <asm/io.h>
#include <asm/delay.h>
#include <asm/uaccess.h>

#include <linux/acpi.h>
#include <acpi/processor.h>

struct acpi_processor_performance p;

#define ACPI_PDC_REVISION_ID                   0x1

static int pdc = 0;

static int __init
acpi_pdump_init (void)
{
	unsigned int i;
	struct acpi_pct_register *r;
	union acpi_object               arg0 = {ACPI_TYPE_BUFFER};
	u32                             arg0_buf[3];
	struct acpi_object_list         arg_list = {1, &arg0};

	if (pdc) {
		arg0.buffer.length = 12;
		arg0.buffer.pointer = (u8 *) arg0_buf;
		arg0_buf[0] = ACPI_PDC_REVISION_ID;
		arg0_buf[1] = 1;	/* One capability bit */
		arg0_buf[2] = pdc;
	}

	if (pdc)
		p.pdc = &arg_list;

	if (acpi_processor_register_performance(&p, 0))
		return -EIO;

	printk("number of states: %d\n", p.state_count);
	for (i=0; i< p.state_count; i++)
		printk(KERN_INFO "acpi_pdump: P%d: %d MHz, %d mW, %d uS s:0x%x c:0x%x\n",
		       i,
		       (u32) p.states[i].core_frequency,
		       (u32) p.states[i].power,
		       (u32) p.states[i].transition_latency,
		       (u32) p.states[i].status,
		       (u32) p.states[i].control);

	printk("control_register:\n");
	r = &p.control_register;
	printk("%d %d %d %d %d %d %lld\n", r->descriptor, r->length, r->space_id,
	       r->bit_width, r->bit_offset, r->reserved, r->address);

	printk("status_register:\n");
	r = &p.status_register;
	printk("%d %d %d %d %d %d %lld\n", r->descriptor, r->length, r->space_id,
	       r->bit_width, r->bit_offset, r->reserved, r->address);

	acpi_processor_unregister_performance(&p, 0);

	return -ENODEV;
}


static void __exit
acpi_pdump_exit (void)
{
	return;
}


module_init(acpi_pdump_init);
module_exit(acpi_pdump_exit);

module_param(pdc, uint, 0444);
MODULE_PARM_DESC(pdc, "_PDC capability bits for _PDC revision 1 to be passed to BIOS.");

MODULE_AUTHOR("Dominik Brodowski");
MODULE_DESCRIPTION("Dump content of cpufreq-relevant parts of ACPI tables");
MODULE_LICENSE ("GPL");

