/*
 *
 *  HVDIMM header file for BSM/MMLS.
 *
 *  (C) 2015 Netlist, Inc.
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
 */

#include <linux/init.h>
#include <linux/module.h>
#include <linux/debugfs.h>
#include <linux/fs.h>
#include <linux/random.h>
#include <linux/spinlock.h>
#include <linux/delay.h>
#include <linux/random.h>
#include <linux/blkdev.h>		// for clflush_cache_range()
#include <linux/kernel.h>		//MK1130
#include "hv_mmio.h"
#include "hv_cmd.h"
#include "hv_params.h"

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("hvdimm command and data interface");

#define len 10000
//MK1216-begn
#define len2		20000
//MK1216-end

struct dentry *dirret;
struct dentry *fileret;
struct dentry *u32int, *u64int;
static char ker_buf_bw[len], ker_buf_br[len];
//MK0421static char ker_buf_mw[len], ker_buf_mr[len];
//MK0421-begin
static char ker_buf_mw[len2], ker_buf_mr[len2];
//MK0421-end
//MK1216static char ker_buf_4k_in[len], ker_buf_4k_out[len], ker_buf_wr[len];
//MK1216-begin
static char ker_buf_4k_in[len2], ker_buf_4k_out[len2], ker_buf_wr[len2];
//MK1216-end
static char ker_buf_reset[len], ker_buf_inq[len], ker_buf_cfg[len];
static int file_bsm_w, file_bsm_r;
static int file_mmls_w, file_mmls_r;
static int file_4k_in, file_4k_out, file_wr_cmp;
static int file_ecc_train;
static int file_reset, file_inq, file_confg;
//MK1111-begin
static int file_memcpy_test;
//MK1111-end
//MK1006-begin
static u32 extdat_value=0;
//MK1006-end
//MK1215-begin
static int file_mem_dump;
//MK1215-end

//MK0123-begin
static int file_terminate_cmd;
//MK0123-end

//MK0208-begin
static u32 intdat_idx_value=1;
//MK0208-end

//MK0421-begin
static int file_load_frb;
//MK0421-end

//MK1118-begin
/* default = fake-read buffer addr */
static u64 fake_read_buff_pa=MMLS_WRITE_BUFF_SYS_PADDR;
static u64 fake_write_buff_pa=MMLS_READ_BUFF_SYS_PADDR;
//MK1118-end
static u32 tag_value;
static u32 async_value;
static u64 sector_value;
static u64 LBA_value;
static u64 sz_emmc_value;
static u64 sz_rdimm_value;
static u64 sz_mmls_value;
static u64 sz_bsm_value;
static u64 sz_nvdimm_value;
static u64 to_emmc_value;
static u64 to_rdimm_value;
static u64 to_mmls_value;
static u64 to_bsm_value;
static u64 to_nvdimm_value;
static u32 latency_mode = 0;
static u32 comp_result;
static char src_buf[16*1024*1024];  /* data for bsm_write test */
static char dst_buf[16*1024*1024];  /* data for bsm_read test */
static unsigned long in_num[516];
static char a_in[10000];
static char unit_ary[516][20];
static unsigned int bytes_per_line;
static int file_in_size;
//MK1130-begin
static long int wr_cmp_mask=DEFAULT_PATTERN_MASK;
static unsigned long cmp_result[512];
//MK1130-end
//MK1215-begin
static unsigned long target_pa=MMLS_WRITE_BUFF_SYS_PADDR;
//MK1215-end
//MK0117-begin
static u32 max_retry_count_value=5;
static u32 which_retry_value=1;
//MK20170710static char fr_stat_str_len=0;
//MK0117-end

//MK0120-begin
static u32 expected_checksum_value=0xFFFFFFFF;
//MK0120-end

//MK0223-begin
//MK0321static u32 debug_feat_value=0x0000000F;
static u32 debug_feat_value=0x0000000F;		//MK0606
//MK0518static u32 debug_feat_bsm_wrt_value=0x0002F000;
//MK0518static u32 debug_feat_bsm_rd_value=0x0026F0F0;
//MK0518-begin
static u32 debug_feat_bsm_wrt_value=0x002600F0;
//MK0605static u32 debug_feat_bsm_rd_value=0x002200F0;
//MK0605-begin
static u32 debug_feat_bsm_rd_value=0x0022F0F0;
//MK0605-end
//MK0518-end
static int file_debug_feat_enable;
//MK0223-end
//MK0221-begin
static u32 user_defined_delay_value=0;
//MK0221-end

//MK0224-begin
//MK0605static u64 debug_feat_bsm_wrt_qc_status_delay_value=400000;	// 400 us
//MK0605static u64 debug_feat_bsm_rd_qc_status_delay_value=400000;	// 400 us
//MK0605-begin
static u64 debug_feat_bsm_wrt_qc_status_delay_value=1;		// 1 us
static u64 debug_feat_bsm_rd_qc_status_delay_value=1000;	// 1000 us = 1 ms
//MK0605-end
//MK0224-end
//MK0301-begin
static u32 debug_feat_bsm_wrt_dummy_command_lba_value=0x200;
static u32 debug_feat_bsm_rd_dummy_command_lba_value=0x200;
static u64 debug_feat_bsm_wrt_dummy_read_addr_value=MMLS_WRITE_BUFF_SYS_PADDR;
static u64 debug_feat_bsm_rd_dummy_read_addr_value=MMLS_WRITE_BUFF_SYS_PADDR;
//MK0301-end

//MK0126-begin
/* Fake-write buffer checksum from the most recent fake-write operation */
//MK0126static unsigned char mr_fwb_checksum=0;
/* Checksum for the most recent fake-write operation from FPGA */
//MK0201static unsigned char mr_fpga_checksum=0;
//MK0126-end

//MK0405-begin
//MK0605static u32 bsm_wrt_delay_before_qc_value=1;	// in ms
//MK0605static u32 bsm_rd_delay_before_qc_value=1;	// in ms
//MK0605-begin
static u32 bsm_wrt_delay_before_qc_value=700;	// in us
static u32 bsm_rd_delay_before_qc_value=700;	// in us
//MK0605-end
//MK0405-end

/* static struct HV_BSM_IO_t BSM_io_data; */
static int cb_function(unsigned short);
static int set_test_data(void);

static unsigned long b, a;
static unsigned long latency;
static int use_4k_file = 0;

//MK0728static unsigned long rand_long(void);
//MKstatic unsigned int calc_long_size(int);
//MKstatic int rand_in_size;
//MK-begin
static unsigned long calc_long_size(unsigned long sectors);
static unsigned long rand_in_size;
//MK-end

//MK20170710-begin
static int bsm_wr_result=0, bsm_rd_result=0;
//MK20170710-end

//MK0728-begin
void generate_fake_read_data(void);		// 4KB data in one 4KB buffer
void generate_fake_read_data_2(void);	// 4KB data in 16KB buffer, interleaving way
void generate_fake_read_data_3(void);	// 4KB data in 8KB buffer, write every other 64-byte space
//MK0728-end
//MK1115-begin
void generate_fake_read_data_4(void);	// 4KB data in one 4KB buffer, inc pattern on every 8 bytes
//MK1115-end
//MK1118-begin
void generate_fake_read_data_5(void);
//MK1118-end

//MK0208-begin
void pattern_index_0(unsigned long *pbuff, unsigned long size);
void pattern_index_1(unsigned long *pbuff, unsigned long size);
void pattern_index_2(unsigned long *pbuff, unsigned long size);
//MK0208-end

//MK0126//MK0120-begin
//MK0126unsigned char calculate_checksum(void *ba, unsigned long size);
//MK0126//MK0120-end

//MK0725-begin
/* Buffer to store test patterns for fake_read/write */
static unsigned long hvbuffer_pa = FAKE_BUFF_SYS_PADDR;
static unsigned long *p_hvbuffer_va;
//MK0725-end

//MK1130-begin
/* Default address will be set in single_cmd_init() */
static unsigned long *p_fake_write_buff_va;
static unsigned long *p_fake_read_buff_va;
//MK1130-end

//MK1216-begin
unsigned char ascii_to_binary(unsigned char c)
{
	unsigned char ctemp=0xFF;

	if (c >= '0' && c <= '9') {
		ctemp = c - 0x30;
	} else if (c >= 'a' && c <= 'f') {
		ctemp = c - 0x57;
	} else if (c >= 'A' && c <= 'F') {
		ctemp = c - 0x37;
	}

	return(ctemp);
}
//MK1216-end
//MK1130-begin
void binary_to_ascii(unsigned char c, unsigned char *hi, unsigned char *lo)
{
	unsigned char ctemp=c & 0x0F;

	if (ctemp >= 0 && ctemp <= 9) {
		*lo = ctemp + 0x30;
	} else {
		*lo = ctemp + 0x57;		// 0x37; to a,b,c,d,e,f
	}

	ctemp = c >> 4;
	if (ctemp >= 0 && ctemp <= 9) {
		*hi = ctemp + 0x30;
	} else {
		*hi = ctemp + 0x57;		// 0x37; to a,b,c,d,e,f
	}
}
//MK1130-end

//MK1130-begin
#define ISSPACE(c)		((c) == ' ' || ((c) >= '\t' && (c) <= '\r'))
#define ISASCII(c)		(((c) & ~0x7f) == 0)
#define ISUPPER(c)		((c) >= 'A' && (c) <= 'Z')
#define ISLOWER(c)		((c) >= 'a' && (c) <= 'z')
#define ISALPHA(c)		(ISUPPER(c) || ISLOWER(c))
#define ISDIGIT(c)		((c) >= '0' && (c) <= '9')

long int mystrtoul(char *nstr, char** endptr, int base)
{
	char *s=nstr;
	unsigned long acc;
	unsigned char c;
	unsigned long cutoff;
	int neg=0, any, cutlim;


	/* Skip white space */
	do
	{
		c = *s++;
	} while (ISSPACE(c));

	/* Pick up leading +/- sign if any */
	if (c == '-') {
		neg = 1;
		c = *s++;
	} else if (c == '+') {
		c = *s++;
	}

	/*
	 * If base is 0, allow 0x for hex and 0 for octal, else assume decimal;
	 * if base is already 16, allow 0x.
	 */
	if ( (base == 0 || base == 16) && c == '0' && (*s == 'x' || *s == 'X') ) {
		c = s[1];
		s += 2;
		base = 16;
	}

	if (base == 0)
		base = c == '0' ? 8 : 10;

	/*
	 * Compute the cutoff value between legal numbers and illegal numbers. That
	 * is the largest legal value, divided by the base. An input number that
	 * is greater than this value, if followed by a legal input character, is
	 * too big. One that is equal to this value may be valid or not; the limit
	 * between valid and invalid numbers is then based on the last digit. For
	 * instance, if the range for longs is [-2147483648..2147483647] and
	 * the input base is 10, cutoff will be set to 214748364 and cutlim to
	 * either 7 (neg==0) or 8 (neg==1), meaning that if we have accumulated
	 * a value > 214748364, or equal but the next digit is > 7 (or 8),
	 * the number is too big, and we will return a range error.
	 *
	 * Set any if any `digits' consumed; make it negative to indicate overflow.
	 */
	cutoff = (unsigned long)ULONG_MAX / (unsigned long)base;
	cutlim = (unsigned long)ULONG_MAX % (unsigned long)base;
	for (acc=0, any=0; ; c=*s++)
	{
		if (!ISASCII(c))
			break;

		if (ISDIGIT(c))
			c -= '0';
		else if (ISALPHA(c))
			c -= ISUPPER(c) ? 'A' - 10 : 'a' - 10;
		else
			break;

		if (c >= base)
			break;

		if (any < 0 || acc > cutoff || (acc == cutoff && c > cutlim)) {
			any = -1;
		} else {
			any = 1;
			acc *= base;
			acc += c;
		}
	}

	if (any < 0)
	{
		acc = INT_MAX;
	} else if (neg) {
		acc = -acc;
	}

	if (endptr != 0) {
		*((const char **)endptr) = any ? s - 1 : nstr;
	}

	return (acc);
}

unsigned int hd_memcmp_64(const void* s1, const void* s2, unsigned long qwcount, unsigned long qwmask)
{
    const unsigned long *p1 = (unsigned long *)s1, *p2 = (unsigned long *)s2;
    unsigned int error_count=0, i;

    for (i=0; i < qwcount; i++)
    {
    	cmp_result[i] = (*p1 & qwmask) ^ (*p2 & qwmask);
    	if( cmp_result[i] != 0 ) {
        	error_count++;
        }

    	p1++;
    	p2++;
    }

    return error_count;
}
//MK1130-end

//MK0126-begin
int check_fake_read_buff_pa(void)
{
	int ret_code=0;

	if (fake_read_buff_pa < hvbuffer_pa) {
		pr_debug("[%s]: fake_read_buff_pa (0x%.16lX) should be GE to 0x%.16lX\n", __func__, (unsigned long)fake_read_buff_pa, hvbuffer_pa);
		ret_code = 999;
	} else if (fake_read_buff_pa > (hvbuffer_pa+HV_BUFFER_SIZE-4096)) {
		pr_debug("[%s]: fake_read_buff_pa (0x%.16lX) should be LE to 0x%.16lX\n", __func__, (unsigned long)fake_read_buff_pa, (hvbuffer_pa+HV_BUFFER_SIZE-4096));
		ret_code = 999;
	}

	return ret_code;
}

int check_fake_write_buff_pa(void)
{
	int ret_code=0;

	if (fake_write_buff_pa < hvbuffer_pa) {
		pr_debug("[%s]: fake_write_buff_pa (0x%.16lx) should be GE to 0x%.16lx\n", __func__, (unsigned long)fake_write_buff_pa, hvbuffer_pa);
		ret_code = 999;
	} else if (fake_write_buff_pa > (hvbuffer_pa+HV_BUFFER_SIZE-4096)) {
		pr_debug("[%s]: fake_write_buff_pa (0x%.16lx) should be LE to 0x%.16lx\n", __func__, (unsigned long)fake_write_buff_pa, (hvbuffer_pa+HV_BUFFER_SIZE-4096));
		ret_code = 999;
	}

	return ret_code;
}

unsigned char* get_fake_read_buff_va(void)
{
	return ( (unsigned char *)((unsigned long)p_hvbuffer_va + ((unsigned long)fake_read_buff_pa - hvbuffer_pa)) );
}

unsigned char* get_fake_write_buff_va(void)
{
	return ( (unsigned char *)((unsigned long)p_hvbuffer_va + ((unsigned long)fake_write_buff_pa - hvbuffer_pa)) );
}
//MK0126-end

//MK1215-begin
/* Memory dump operation */
static ssize_t view_mem_dump(struct file *fp, char __user *user_buffer,
				size_t count, loff_t *position)
{
	unsigned long *p_target_va=(unsigned long *)((unsigned long)p_hvbuffer_va + (target_pa - hvbuffer_pa));

	pr_debug("[%s]: target_pa=0x%.16lX - p_target_va=0x%.16lX\n",
			__func__, target_pa, (unsigned long)p_target_va);

	/* Display 4KB data from p_target_va */
	display_buffer(p_target_va, 4096/8, wr_cmp_mask);

	return simple_read_from_buffer(user_buffer, count, position, ker_buf_wr, len2);
}

/* Memory dump operation */
static ssize_t mem_dump(struct file *fp, const char __user *user_buffer,
				size_t count, loff_t *position)
{
	unsigned long copylen, temp_pa;
	unsigned long *p_target_va=NULL;
	char pa_str[20], *pEnd;

	/* Extract and set a 64-bit mask value from the user buffer */
	memset(pa_str, 0, 20);
	if (count == 12) {
		copylen = copy_from_user((void *)pa_str, user_buffer, count-1);
		temp_pa = mystrtoul(pa_str, &pEnd, 16);

		if (temp_pa < hvbuffer_pa) {
			pr_debug("[%s]: error: input pa (0x%.16lx) should be >= 0x%.16lx\n",
					__func__, temp_pa, hvbuffer_pa);
			goto errorExit;
		} else if (temp_pa > (hvbuffer_pa+HV_BUFFER_SIZE-4096)) {
			pr_debug("[%s]: error:  input pa (0x%.16lx) should be <= 0x%.16lx\n",
					__func__, temp_pa, (hvbuffer_pa+HV_BUFFER_SIZE-4096));
			goto errorExit;
		}
	} else {
		pr_debug("[%s]: error(count=%ld): input pa must be a 64-bit hex value (current pa=0x%.16lx)\n", __func__, count, target_pa);
		goto errorExit;
	}

	target_pa = temp_pa;
	pr_debug("[%s]: str=%s, copylen=%ld, target_pa=0x%.16lx\n", __func__, pa_str, copylen, temp_pa);

	p_target_va=(unsigned long *)((unsigned long)p_hvbuffer_va + (target_pa - hvbuffer_pa));
	pr_debug("[%s]: target_pa=0x%.16lX - p_target_va=0x%.16lX\n",
			__func__, target_pa, (unsigned long)p_target_va);

	/* Display 4KB data from p_target_va */
	display_buffer(p_target_va, 4096/8, wr_cmp_mask);

errorExit:
	return simple_write_to_buffer(ker_buf_wr, len2, position, user_buffer, count);
}
//MK1215-end

/* view hv loopback file operation */
static ssize_t view_wr_cmp(struct file *fp, char __user *user_buffer,
				size_t count, loff_t *position)
{
#if 0	//MK1130
	pr_debug("%s\n", __func__);

	return simple_read_from_buffer(user_buffer, count, position, ker_buf_wr, len2);
#endif	//MK1130
//MK1130-begin
	unsigned int error_count=0, i, j, k, krnlbuff_size;
	char *pkrnlbuff;
	unsigned char c, hi, lo, qwstr[16];
	unsigned long qw;


//MK0126	if (fake_read_buff_pa < hvbuffer_pa) {
//MK0126		pr_debug("[%s]: fake_read_buff_pa (0x%.16lX) should be GE to 0x%.16lX\n", __func__, (unsigned long)fake_read_buff_pa, hvbuffer_pa);
//MK0126		error_count = 999;
//MK0126	} else if (fake_read_buff_pa > (hvbuffer_pa+HV_BUFFER_SIZE-4096)) {
//MK0126		pr_debug("[%s]: fake_read_buff_pa (0x%.16lx) should be LE to 0x%.16lx\n", __func__, (unsigned long)fake_read_buff_pa, (hvbuffer_pa+HV_BUFFER_SIZE-4096));
//MK0126		error_count = 999;
//MK0126	}
//MK0126
//MK0126	if (fake_write_buff_pa < hvbuffer_pa) {
//MK0126		pr_debug("[%s]: fake_write_buff_pa (0x%.16lx) should be GE to 0x%.16lx\n", __func__, (unsigned long)fake_write_buff_pa, hvbuffer_pa);
//MK0126		error_count = 999;
//MK0126	} else if (fake_write_buff_pa > (hvbuffer_pa+HV_BUFFER_SIZE-4096)) {
//MK0126		pr_debug("[%s]: fake_write_buff_pa (0x%.16lx) should be LE to 0x%.16lx\n", __func__, (unsigned long)fake_write_buff_pa, (hvbuffer_pa+HV_BUFFER_SIZE-4096));
//MK0126		error_count = 999;
//MK0126	}
//MK0126-begin
	if ( (error_count = check_fake_read_buff_pa()) == 0 ) {
		error_count = check_fake_write_buff_pa();
	}
//MK0126-end

	/* These addresses will be used for the comparison */
//MK0126	p_fake_read_buff_va = (unsigned long *)((unsigned long)p_hvbuffer_va + ((unsigned long)fake_read_buff_pa - hvbuffer_pa));
//MK0126	p_fake_write_buff_va = (unsigned long *)((unsigned long)p_hvbuffer_va + ((unsigned long)fake_write_buff_pa - hvbuffer_pa));
//MK0126-begin
	p_fake_read_buff_va = (unsigned long *)get_fake_read_buff_va();
	p_fake_write_buff_va = (unsigned long *)get_fake_write_buff_va();
//MK0126-end
	pr_debug("[%s]: fake_read_buff_pa=0x%.16lX, p_fake_read_buff_va=0x%.16lX\n", __func__, (unsigned long)fake_read_buff_pa, (unsigned long)p_fake_read_buff_va);
	pr_debug("[%s]: fake_write_buff_pa=0x%.16lX, p_fake_write_buff_va=0x%.16lX\n", __func__, (unsigned long)fake_write_buff_pa, (unsigned long)p_fake_write_buff_va);

	/* Compare two 4KB buffers with pattern mask. The result will be stored in cmp_result[] */
	memset((void*)cmp_result, 0, 4096);
	if (error_count == 999) {
		/* Actual string count is 19 including \n but need to give an extra byte for null char */
		snprintf(ker_buf_wr, 10, "%s %.3d\n", "ERRR", error_count);
	}
	else if ( (error_count=hd_memcmp_64((void *)p_fake_write_buff_va, (void *)p_fake_read_buff_va, 512, wr_cmp_mask)) == 0 ) {
		snprintf(ker_buf_wr, 10, "%s %.3d\n", "PASS", error_count);
	} else {
		snprintf(ker_buf_wr, 10, "%s %.3d\n", "FAIL", error_count);
	}

	/* Dump data to the file. We're removing the null char added above. */
	pkrnlbuff = ker_buf_wr + 9;
	krnlbuff_size = sizeof(ker_buf_wr) - 9;
	for (i=0; i < 64; i++)
	{
		/* There will be 8 qword in each row */
		for (j=0; j < 8; j++)
		{
			/*
			 * Get a 8-byte binary data from the result buffer and convert it to
			 * a 16-byte ASCII string
			 */
			qw = cmp_result[i*8+j];
			memset((void*)qwstr, 0, sizeof(qwstr));
			for (k=0; k < 8; k++, qw=qw>>8)
			{
				/* A hex byte will be converted to two ASCII byte */
				c = (unsigned char)(qw & 0x00000000000000FF);
				binary_to_ascii(c, &hi, &lo);
				qwstr[sizeof(qwstr)-2*k-2] = hi;
				qwstr[sizeof(qwstr)-2*k-1] = lo;
			}

			/* Store the converted string in the kernel buffer. Give an extra
			 * byte for a null char */
			snprintf(pkrnlbuff, sizeof(qwstr)+1, "%s", qwstr);
			if (j == 7) {
				/* CR will be added instead of space. The start offset from
				 * where the null char is. Don't forget an extra space for
				 * a null char. */
				snprintf(pkrnlbuff+sizeof(qwstr), 2, "\n");
			} else {
				/* Each qword is separated by a space */
				snprintf(pkrnlbuff+sizeof(qwstr), 2, " ");
			}

			/* Advance to the next location by # of chars added above */
			pkrnlbuff += (sizeof(qwstr)+1);
			krnlbuff_size -= (sizeof(qwstr)+1);
		}
	}

	return simple_read_from_buffer(user_buffer, 9+8192+512, position, ker_buf_wr, 9+8192+512);
//MK1130-end
}

/* bsm loopback file operation */
static ssize_t wr_cmp(struct file *fp, const char __user *user_buffer,
				size_t count, loff_t *position)
{
#if 0 	//MK1130
	int i;
	unsigned long *p_src, *p_dst;
	unsigned long dst_data, src_data;

	//pr_debug("wr_cmp, count=%d, len=%d\n", *(int *)&count, len);

	/* data comparison between bsm-write and bsm-read */
	comp_result = 1;
	p_src = (unsigned long *)src_buf;
	p_dst = (unsigned long *)dst_buf;

	//for (i = 0; i < FS_BLOCK_SIZE/sizeof(unsigned long); i++) {
	for (i = 0; i < rand_in_size; i++) {
		/* if (i<5) pr_debug("first 5 data: dst=0x%lx, src=0x%lx\n",
			*p_dst, *p_src); */
		dst_data = (*p_dst++);
		src_data = (*p_src++);
		/* dst_data = (*p_dst++) & 0xFFFFFFFF00000000; */
		/* src_data = (*p_src++) & 0xFFFFFFFF00000000; */
		if (i < 5)
			pr_debug("diff: dst_data=0x%lx, src_data=0x%lx\n",
				dst_data, src_data);
		if (dst_data != src_data) {
			comp_result = 2;
			break;
		}
	}

	if (comp_result == 1) {
		pr_warn("PASS!!!\n\t write data same as read data!!!\n");
	} else {
		pr_warn("FAIL!!!\n\tRead data different from Write data!!!\n");
		pr_warn("mis-matched line no: %d\n", i);
	};
#endif	//MK1130
//MK1130-begin
	unsigned long copylen;
	char mask_str[20], *pEnd;
//	int temp;

	/* Extract and set a 64-bit mask value from the user buffer */
	memset(mask_str, 0, 20);
	if (count == 17 || count == 19) {
		copylen = copy_from_user((void *)mask_str, user_buffer, count-1);
		wr_cmp_mask = mystrtoul(mask_str, &pEnd, 16);
		pr_debug("[%s]: str=%s, copylen=%ld, wr_cmp_mask=0x%.16lx\n", __func__, mask_str, copylen, wr_cmp_mask);
	} else {
		pr_debug("[%s]: error(count=%ld): wr_cmp_mask must be a 64-bit hex value (current mask=0x%.16lx)\n", __func__, count, wr_cmp_mask);
	}
//MK1130-end

	return simple_write_to_buffer(ker_buf_wr, len2, position, user_buffer,
	count);
}

/* view bsm 4k output file operation */
static ssize_t view_file4k_out(struct file *fp, char __user *user_buffer,
				size_t count, loff_t *position)
{
	int i;
	unsigned long *p_ker_buf, *p_data_4k;

	/* copy dest_buf to kernel buffer for view */
	p_data_4k = (unsigned long *)dst_buf;
	p_ker_buf = (unsigned long *)ker_buf_4k_out;
	for (i = 0; i < 512; i++)
		sprintf(ker_buf_4k_out+19*i, "0x%016lX\n", *p_data_4k++);
	/* for (i=0; i<24;i++) pr_info("**ker_buf_4k_out=%c\n",
		ker_buf_4k_out[i]); */

	return simple_read_from_buffer(user_buffer, count, position,
		ker_buf_4k_out, file_in_size*64);
}

/* Get test data from the external file, convert them and save it in ker_buf_4k_out */
static ssize_t file4k_out(struct file *fp, const char __user *user_buffer,
				size_t count, loff_t *position)
{
//MK0927-begin
	ssize_t byte_count;
//MK1216	char *pbuff=ker_buf_4k_out;
//MK1216-begin
	char *pbuff=ker_buf_4k_out, c1, c2;
	unsigned long *pqword;
//MK1216-end
	unsigned int i, j;
//MK0927-end
//MK1216	pr_debug("file4k_out, count=%d, len=%d\n", *(int *)&count, len);

//MK0927	pr_debug("%s ***WARNNING: Not allowed to modify 4k output file from user space!!!", __func__);

//MK0927	return simple_write_to_buffer(ker_buf_4k_out, len, position,
//MK0927		user_buffer, count);
//MK0927-begin
	/* Copy data from the user buffer to a kernel buffer */
	byte_count = simple_write_to_buffer(ker_buf_4k_out, len2, position, user_buffer, count);

//MK1216-begin
	pr_debug("[%s]: count=%d, len2=%d. byte_count=%ld\n", __func__, *(int *)&count, len2, (unsigned long)byte_count);

	/* Convert the first two bytes into a new byte: New byte =  c2 << 4 | c1 */
	for (i=0, j=0; i < 16384; i+=4, j++)
	{
		c1 = ascii_to_binary(*(pbuff+i));
		c2 = ascii_to_binary(*(pbuff+i+2));
		if (c1 == 0xFF) {
			pr_debug("[%s]: invalid data (0x%.2x) at byte offset 0x%.8x\n",
					__func__, c1, i);
			goto errorExit;
		} else if (c2 == 0xFF) {
			pr_debug("[%s]: invalid data (0x%.2x) at byte offset 0x%.8x\n",
					__func__, c2, i+2);
			goto errorExit;
		}
		*(pbuff+j) = (c2 << 4) | c1;
	}

	pr_debug("[%s]: i=%d, j=%d\n", __func__, i, j);
//MK1216-end

#if 0	//MK1216
	for (i=0; i < 256; i++)
	{
		pr_debug("%x %x %x %x %x %x %x %x %x %x %x %x %x %x %x %x %x %x %x %x %x %x %x %x %x %x %x %x %x %x %x %x\n",
				*pbuff, *(pbuff+1), *(pbuff+2), *(pbuff+3), *(pbuff+4), *(pbuff+5), *(pbuff+6), *(pbuff+7),
				*(pbuff+8), *(pbuff+9), *(pbuff+10), *(pbuff+11), *(pbuff+12), *(pbuff+13), *(pbuff+14), *(pbuff+15),
				*(pbuff+16), *(pbuff+17), *(pbuff+18), *(pbuff+19), *(pbuff+20), *(pbuff+21), *(pbuff+22), *(pbuff+23),
				*(pbuff+24), *(pbuff+25), *(pbuff+26), *(pbuff+27), *(pbuff+28), *(pbuff+29), *(pbuff+30), *(pbuff+31));
		pbuff += 32;
	}
#endif	//MK1216
//MK1216-begin
	pqword = (unsigned long *)ker_buf_4k_out;
	for (i=0; i < 64; i++)
	{
		pr_debug("%.16lx %.16lx %.16lx %.16lx %.16lx %.16lx %.16lx %.16lx\n",
				*pqword, *(pqword+1), *(pqword+2), *(pqword+3), *(pqword+4), *(pqword+5), *(pqword+6), *(pqword+7));
		pqword += 8;
	}

errorExit:
//MK1216-end

	return byte_count;
//MK0927-end
}


/* view 4k-file file operation */
static ssize_t view_file4k_in(struct file *fp, char __user *user_buffer,
				size_t count, loff_t *position)
{

	int i;
	unsigned long *p_ker_buf, *p_data_in;
	/* pr_debug("%s, file_in_size=%d\n", __func__, file_in_size); */

	/* copy source buf to kernel buffer for view */
	p_data_in = (unsigned long *)src_buf;
	p_ker_buf = (unsigned long *)ker_buf_4k_in;
	for (i = 0; i < 512; i++)
		sprintf(ker_buf_4k_in+19*i, "0x%016lX\n", *p_data_in++);

	return simple_read_from_buffer(user_buffer, count, position,
		ker_buf_4k_in, file_in_size*64);
}

/* fetch data from the file in debugfs and stored in src_buf[] */
static ssize_t file4k_in(struct file *fp, const char __user *user_buffer,
				size_t count, loff_t *position)
{
	unsigned long *p_temp;
	unsigned int i, j;
//MK-begin
	int backup_use_4k_file=use_4k_file;
//MK-end

	use_4k_file = 1;
	/* pr_debug("bsm_wr, count=%d, len=%d\n", *(int *)&count, len); */
	file_in_size = count;

	for (i = 0; i < count; i++)
		a_in[i] = user_buffer[i];
	a_in[count] = 0;

	set_test_data();	/* produce in_num for data sourcing */

	/* get source buffer data */
	p_temp = (unsigned long *)src_buf;
	for (i = 0; i < (4096/sizeof(long))/8; i++) {
		for (j = 0; j < 8; j++)
			*p_temp++ = in_num[i*8+j];
	}

//MK-begin
	/* Restore */
	use_4k_file = backup_use_4k_file;
//MK-end

//MK1130	return simple_write_to_buffer(ker_buf_4k_in, len, position,
//MK1130		user_buffer, count);
//MK1130-begin
	return simple_write_to_buffer(ker_buf_4k_in, len2, position,
		user_buffer, count);
//MK1130-end
}

//MK0421-begin
/* Show the content of fake-read buffer to the user */
static ssize_t view_load_frb(struct file *fp, char __user *user_buffer,
							size_t count, loff_t *position)
{
	unsigned int i, j;
	unsigned char *psrc=NULL, *pdest=NULL, hinibble, lonibble;

	/*
	 * Data in fake-read buffer will be converted to ASCII format and
	 * stored in a kernel buffer
	 */
	psrc = (unsigned char *)get_fake_read_buff_va();
	pdest = (unsigned char *)ker_buf_mw;
	pr_info("[%s]: PA from User = 0x%.16lx - VA for the user PA = 0x%.16lx\n",
			__func__, (unsigned long)fake_read_buff_pa, (unsigned long)psrc);

	/* Convert the first two bytes into a new byte: New byte =  c2 << 4 | c1 */
	for (i=0, j=0; i < 4096; i++, j+=4)
	{
		binary_to_ascii(*(psrc+i), &hinibble, &lonibble);
		*(pdest+j) = lonibble;
		*(pdest+j+1) = 0x20;
		*(pdest+j+2) = hinibble;
		if ( ((i+1) % 64) == 0 ) {
			*(pdest+j+3) = 0x0A;	// LF
		} else {
			*(pdest+j+3) = 0x20;	// SPACE
		}
	}

	pr_debug("[%s]: i=%d, j=%d\n", __func__, i, j);

	/*
	 * Length of each qword in ASCII including SPACE = 16*2 = 32
	 * # of ASCII qwords per line = 8
	 * # of lines = 64
	 */
	return simple_read_from_buffer(user_buffer, 16*2*8*64, position, ker_buf_mw, 16*2*8*64);
}

/* Load data in fake-read buffer */
static ssize_t load_frb(struct file *fp, const char __user *user_buffer,
				size_t count, loff_t *position)
{
	/*
	 * This function reauires the following information in order to fill up
	 * fake-read buffer correctly. The five items should be provided to the
	 * debug FS code in this order.
	 *   1. LBA_value if extdata = 0 & intdata_idx = 1 or 3
	 *   2. fake_read_buff_pa
	 *   3. extdat
	 *   4. intdat_idx if extdat = 0
	 *   5. ker_buf_4k_out if extdata = 1
	 */
	if ( check_fake_read_buff_pa() != 0 ) {
		goto errorExit;
	}
	generate_fake_read_data_5();

errorExit:
	return simple_write_to_buffer(ker_buf_mw, len, position, user_buffer, count);
}
//MK0421-end

/* view bsm write file operation */
static ssize_t view_bsm_wr(struct file *fp, char __user *user_buffer,
				size_t count, loff_t *position)
{
#if 0	//MK0117
	pr_debug("%s\n", __func__);

	return simple_read_from_buffer(user_buffer, count, position,
		ker_buf_bw, len);
#endif	//MK0117
//MK0117-begin
	pr_debug("[%s]: entered\n", __func__);
	/* Clear destination buffer */
	memset(ker_buf_bw, 0, sizeof(ker_buf_bw));
//MK20170710	memcpy(ker_buf_bw, ker_buf_mw, fr_stat_str_len+1);

//MK20170710	return simple_read_from_buffer(user_buffer, fr_stat_str_len+1, position, ker_buf_bw, fr_stat_str_len+1);
//MK0117-end

//MK20170710-begin
	if (bsm_wr_result == 0) {
		/* Actual string count is 19 including \n but need to give an extra byte for null char */
		snprintf(ker_buf_bw, 15, "%s %.8X\n", "PASS", bsm_wr_result);
	} else {
		snprintf(ker_buf_bw, 15, "%s %.8X\n", "FAIL", bsm_wr_result);
	}

	return simple_read_from_buffer(user_buffer, 14, position, ker_buf_bw, 14);
//MK20170710-end
}

/* bsm write file operation */
static ssize_t bsm_wr(struct file *fp, const char __user *user_buffer,
				size_t count, loff_t *position)
{
#if 0	//MK0728
	unsigned long l_data;
	unsigned long *tmp_ptr;
	int i;

	/* pr_debug("bsm_wr, count=%d, len=%d\n", *(int *)&count, len); */
	rand_in_size = calc_long_size(sector_value);
	tmp_ptr = (unsigned long *)src_buf;

	pr_debug("use_4k_file=%d\n", use_4k_file);
	if (!use_4k_file){
		for (i =0; i<rand_in_size; i++){
			l_data = rand_long();
			if (i<10) pr_debug("l_data=0x%lx, rand_in_size=%d\n", l_data, rand_in_size);
			*tmp_ptr++ = l_data;
		}
	}
#endif	//MK0728

//MK0117-begin
	unsigned char *vbuf=NULL;
	int ret_code=0;

//MK20170710-begin
	/* Assume no error */
	bsm_wr_result = 0;
//MK20170710-end

//MK0130	unsigned int retry_count=max_retry_count_value;

//MK0130	memset(ker_buf_mw, 0, sizeof(ker_buf_mw));
//MK0130	fr_stat_str_len = 0;
//MK0117-end

//MK0728-begin
//MK1118	unsigned char *pAlignedBuff;

//MK1118	pAlignedBuff = (unsigned char *) p_hvbuffer_va;
//MK1118	pAlignedBuff = (unsigned char *) ((unsigned long)(pAlignedBuff + 0x0FFF) & 0xFFFFFFFFFFFFF000);

	/* Fill up the buffer with data for fake read operation */
//MK1118	generate_fake_read_data();
//MK0126//MK1118-begin
//MK0126	if (fake_read_buff_pa < hvbuffer_pa) {
//MK0126		pr_debug("[%s]: fake_read_buff_pa (0x%.16lX) should be GE to 0x%.16lX\n", __func__, (unsigned long)fake_read_buff_pa, hvbuffer_pa);
//MK0126		goto errorExit;
//MK0126	} else if (fake_read_buff_pa > (hvbuffer_pa+HV_BUFFER_SIZE-4096)) {
//MK0126		pr_debug("[%s]: fake_read_buff_pa (0x%.16lX) should be LE to 0x%.16lX\n", __func__, (unsigned long)fake_read_buff_pa, (hvbuffer_pa+HV_BUFFER_SIZE-4096));
//MK0126		goto errorExit;
//MK0126	}
//MK0126	generate_fake_read_data_5();
//MK0126//MK1118-end
//MK0126-begin
	if ( check_fake_read_buff_pa() != 0 ) {
		goto errorExit;
	}
	generate_fake_read_data_5();
//MK0126-end

//MK1115-begin
//	generate_fake_read_data_4();
//MK1115-end
//	generate_fake_read_data_2();
//	generate_fake_read_data_3();
//MK0728-end

//MK0126//MK0117-begin
//MK0126	/* This pointer will be used for fake-read operation */
//MK0126//MK0126	vbuf = (unsigned char *)((unsigned long)p_hvbuffer_va + ((unsigned long)fake_read_buff_pa - hvbuffer_pa));
//MK0126//MK0117-end
//MK0126
//MK0126//MK1130-begin
//MK0126	/* This address will be used to compare fake-rd data with fake-wrt data */
//MK0126	p_fake_read_buff_va = (unsigned long *)((unsigned long)p_hvbuffer_va + ((unsigned long)fake_read_buff_pa - hvbuffer_pa));
//MK0126	pr_debug("[%s]: fake_read_buff_pa=0x%.16lX, p_fake_read_buff_va=0x%.16lX\n", __func__, (unsigned long)fake_read_buff_pa, (unsigned long)p_fake_read_buff_va);
//MK0126//MK1130-end
//MK0126-begin
	vbuf = get_fake_read_buff_va();
	p_fake_read_buff_va = (unsigned long *)vbuf;
	pr_debug("[%s]: fake_read_buff_pa=0x%.16lX, p_fake_read_buff_va=0x%.16lX\n", __func__, (unsigned long)fake_read_buff_pa, (unsigned long)p_fake_read_buff_va);
//MK0126-end

	async_value = get_async_mode();

	if ((latency_mode==BSM_W_LTNCY)||(latency_mode==BSM_RW_LTNCY)||(latency_mode==ALL_RW_LTNCY)){
		b = hv_nstimeofday();
//MK0724		bsm_write_command(tag_value, sector_value, LBA_value, src_buf,
//MK0724			async_value, cb_function);
//MK0724-begin
//MK1118		bsm_write_command(tag_value, sector_value, LBA_value, pAlignedBuff, async_value, cb_function);
//MK1118-begin
//MK0201		bsm_write_command(tag_value, sector_value, LBA_value, (unsigned char *)fake_read_buff_pa, async_value, cb_function);
//MK1118-end
//MK0724-end

//MK0201//MK0117-begin
//MK0201		ret_code = bsm_write_command_2(tag_value, sector_value, LBA_value, (unsigned char *)fake_read_buff_pa, vbuf, async_value, cb_function);
//MK0201//MK0117-end
//MK0201-begin
//MK0201		ret_code = bsm_write_command_2(tag_value, sector_value, LBA_value, (unsigned char *)fake_read_buff_pa, vbuf, async_value, cb_function, max_retry_count_value);
//MK0324		ret_code = bsm_write_command_3(tag_value, sector_value, LBA_value, (unsigned char *)fake_read_buff_pa, vbuf, async_value, cb_function, max_retry_count_value);
//MK0201-end
//MK0324-begin
		ret_code = bsm_write_command_emmc(tag_value, sector_value, LBA_value, (unsigned char *)fake_read_buff_pa, vbuf, async_value, cb_function);
//MK0324-end
		a = hv_nstimeofday();
		latency = a - b;
//MK		pr_info("latency from bsm_w is %ldns\n", latency);
//MK-begin
		pr_info("[%s]: latency of bsm_write_commmand = %ldns\n", __func__, latency);
//MK-end
	} else {

//MK0130//MK0117-begin
//MK0130send_command:
//MK0130//MK0117-end

//MK0724		bsm_write_command(tag_value, sector_value, LBA_value, src_buf,
//MK0724			async_value, cb_function);
//MK0724-begin
//MK1118		bsm_write_command(tag_value, sector_value, LBA_value, pAlignedBuff, async_value, cb_function);
//MK1118-begin
//MK0201		bsm_write_command(tag_value, sector_value, LBA_value, (unsigned char *)fake_read_buff_pa, async_value, cb_function);
//MK1118-end
//MK0724-end

//MK0201//MK0117-begin
//MK0201//MK0130do_fake_read_again:
//MK0201		ret_code = bsm_write_command_2(tag_value, sector_value, LBA_value, (unsigned char *)fake_read_buff_pa, vbuf, async_value, cb_function);
//MK0201//MK0117-end
//MK0201-begin
//MK0201		ret_code = bsm_write_command_2(tag_value, sector_value, LBA_value, (unsigned char *)fake_read_buff_pa, vbuf, async_value, cb_function, max_retry_count_value);
//MK0324		ret_code = bsm_write_command_3(tag_value, sector_value, LBA_value, (unsigned char *)fake_read_buff_pa, vbuf, async_value, cb_function, max_retry_count_value);
//MK0201-end
//MK0324-begin
		ret_code = bsm_write_command_emmc(tag_value, sector_value, LBA_value, (unsigned char *)fake_read_buff_pa, vbuf, async_value, cb_function);
//MK0324-end

	}


//MK0130//MK0117-begin
//MK0130	if (ret_code == 0) {
//MK0130		pr_info("[%s]: ret_code = %.2d, which_retry_value = %.2d, # of bsm_write operations = %.2d\n",
//MK0130				__func__, ret_code, which_retry_value, max_retry_count_value-retry_count+1);
//MK0130		sprintf(ker_buf_mw, "[%s]: ret_code = %.2d, which_retry_value = %.2d, # of bsm_write operations = %.2d\n",
//MK0130				__func__, ret_code, which_retry_value, max_retry_count_value-retry_count+1);
//MK0130		fr_stat_str_len += 80;
//MK0130	}
//MK0130	else if (retry_count != 0) {
//MK0130//SJ0118-begin
//MK0130		pr_info("[%s]: ret_code = %.2d, # of retry_count = %.2d, which_retry_value = %.2d, LBA = 0x%.8x\n",
//MK0130				__func__, ret_code, retry_count, which_retry_value, (unsigned int)LBA_value);
//MK0130		sprintf(ker_buf_mw, "[%s]: ret_code = %.2d, # of retry_count = %.2d, which_retry_value = %.2d, LBA = 0x%.8x\n",
//MK0130				__func__, ret_code, retry_count, which_retry_value, (unsigned int)LBA_value);
//MK0130		fr_stat_str_len += 89;
//MK0130//SJ0118-end
//MK0130		retry_count--;
//MK0130
//MK0130		if (which_retry_value == 0) {
//MK0130			goto send_command;
//MK0130		} else {
//MK0130			goto do_fake_read_again;
//MK0130		}
//MK0130	} else {
//MK0130		pr_info("[%s]: bsm_write failed. retry_count reached zero (max retry count was %.2d)\n", __func__, max_retry_count_value);
//MK0130		sprintf(ker_buf_mw, "[%s]: bsm_write failed. retry_count reached zero (max retry count was %.2d)\n",
//MK0130				__func__, max_retry_count_value);
//MK0130		fr_stat_str_len += 78;
//MK0130	}
//MK0130//MK0117-end

//MK1118-begin
errorExit:
//MK1118-end

//MK20170710-begin
	/* Save the error code from BSM_WRITE API */
	bsm_wr_result = ret_code;
//MK20170710-end

	return simple_write_to_buffer(ker_buf_bw, len, position, user_buffer,
	count);
}

/* view bsm read file operation */
static ssize_t view_bsm_rd(struct file *fp, char __user *user_buffer,
				size_t count, loff_t *position)
{
	pr_debug("%s\n", __func__);

//MK20170710	return simple_read_from_buffer(user_buffer, count, position, ker_buf_br, len);
//MK20170710-begin
	memset(ker_buf_br, 0, sizeof(ker_buf_br));

	if (bsm_rd_result == 0) {
		/* Actual string count is 19 including \n but need to give an extra byte for null char */
		snprintf(ker_buf_br, 15, "%s %.8X\n", "PASS", bsm_rd_result);
	} else {
		snprintf(ker_buf_br, 15, "%s %.8X\n", "FAIL", bsm_rd_result);
	}

	return simple_read_from_buffer(user_buffer, 14, position, ker_buf_br, 14);
//MK20170710-end
}

/* bsm read file operation */
static ssize_t bsm_rd(struct file *fp, const char __user *user_buffer,
				size_t count, loff_t *position)
{
//MK1109	int i;
	//unsigned long *src_pt, *dst_pt;
//MK0817-begin
//MK1118	unsigned char *pAlignedBuff;
//MK1118	unsigned long *pTemp;

//MK1024	pAlignedBuff = (unsigned char *) p_hvbuffer_va;
//MK1024-begin
//MK1118	pAlignedBuff = (unsigned char *) (p_hvbuffer_va + MMLS_WRITE_BUFF_SIZE);
//MK1024-end
//MK1118	pAlignedBuff = (unsigned char *) ((unsigned long)(pAlignedBuff + 0x0FFF) & 0xFFFFFFFFFFFFF000);
//MK1118	pTemp = (unsigned long *) pAlignedBuff;
//MK0817-end

//MK0120-begin
	unsigned char *vbuf=NULL;
//MK0126	unsigned char checksum=0;
//MK0126-begin
	int ret_code=0;
//MK0221	unsigned int error_count=0;
//MK0126-end
//MK0126	unsigned int retry_count=max_retry_count_value;
//MK0120-end

//MK20170710-begin
	/* Assume no error */
	bsm_rd_result = 0;
//MK20170710-end


//MK0126//MK1118-begin
//MK0126	if (fake_write_buff_pa < hvbuffer_pa) {
//MK0126		pr_debug("[%s]: fake_write_buff_pa (0x%.16lX) should be GE to 0x%.16lX\n", __func__, (unsigned long)fake_write_buff_pa, hvbuffer_pa);
//MK0126		goto errorExit;
//MK0126	} else if (fake_write_buff_pa > (hvbuffer_pa+HV_BUFFER_SIZE-4096)) {
//MK0126		pr_debug("[%s]: fake_write_buff_pa (0x%.16lX) should be LE to 0x%.16lX\n", __func__, (unsigned long)fake_write_buff_pa, (hvbuffer_pa+HV_BUFFER_SIZE-4096));
//MK0126		goto errorExit;
//MK0126	}

//MK0126//MK1118-end
//MK0126-begin
	if ( check_fake_write_buff_pa() != 0 ) {
		goto errorExit;
	}
//MK0126-end

//MK0126//MK0120-begin
//MK0126	vbuf = (unsigned char *)((unsigned long)p_hvbuffer_va + ((unsigned long)fake_write_buff_pa - hvbuffer_pa));
//MK0126//MK0120-end

	/* clear test buffer */
//MK	for (i = 0; i < 20; i++) dst_buf[i] = 0;
//MK0817-begin
	/* Clear 4KB space in the buffer */
//MK1109	for (i = 0; i < 512; i++)
//MK1109		*(pTemp+i) = 0;
//MK0817-end

//MK0126//MK1130-begin
//MK0126	/* This address will be used to compare fake-rd data with fake-wrt data */
//MK0126	p_fake_write_buff_va = (unsigned long *)((unsigned long)p_hvbuffer_va + ((unsigned long)fake_write_buff_pa - hvbuffer_pa));
//MK0126	pr_debug("[%s]: fake_write_buff_pa=0x%.16lX, p_fake_write_buff_va=0x%.16lX\n", __func__, (unsigned long)fake_write_buff_pa, (unsigned long)p_fake_write_buff_va);
//MK0126//MK1130-end
//MK0126-begin
	vbuf = get_fake_write_buff_va();
	p_fake_write_buff_va = (unsigned long *)vbuf;
	pr_debug("[%s]: fake_write_buff_pa=0x%.16lX, p_fake_write_buff_va=0x%.16lX\n", __func__, (unsigned long)fake_write_buff_pa, (unsigned long)p_fake_write_buff_va);
//MK0126-end

	async_value = get_async_mode();

//MK	pr_debug("latency_mode=%d\n", latency_mode);
//MK-begin
	pr_debug("[%s]: latency_mode = %d\n", __func__, latency_mode);
//MK-end
	if ((latency_mode==BSM_R_LTNCY)||(latency_mode==BSM_RW_LTNCY)||(latency_mode==ALL_RW_LTNCY)){
		b = hv_nstimeofday();
//MK0817		bsm_read_command(tag_value, sector_value, LBA_value, dst_buf,
//MK0817			async_value, cb_function);
//MK0817-begin
//MK1118		bsm_read_command(tag_value, sector_value, LBA_value, pAlignedBuff,
//MK1118				async_value, cb_function);
//MK1118-begin
//MK0207		bsm_read_command(tag_value, sector_value, LBA_value, (unsigned char *)fake_write_buff_pa,
//MK0207				async_value, cb_function);
//MK1118-end
//MK0817-end

//MK0201//MK0120-begin
//MK0201		ret_code = bsm_read_command_2(tag_value, sector_value, LBA_value, (unsigned char *)fake_write_buff_pa,
//MK0201				vbuf, async_value, cb_function, &mr_fpga_checksum);
//MK0201//MK0120-end
//MK0201-begin
//MK0207		ret_code = bsm_read_command_2(tag_value, sector_value, LBA_value, (unsigned char *)fake_write_buff_pa,
//MK0207				vbuf, async_value, cb_function, max_retry_count_value);
//MK0201-end

//MK0207-begin
//MK0213		if (bsm_read_command(tag_value, sector_value, LBA_value, (unsigned char *)fake_write_buff_pa, async_value, cb_function, max_retry_count_value) == 0) {
//MK0213			ret_code = bsm_read_command_2(tag_value, sector_value, LBA_value, (unsigned char *)fake_write_buff_pa,
//MK0213					vbuf, async_value, cb_function, max_retry_count_value);
//MK0213		}
//MK0207-end
//MK0213-begin
//MK0324			ret_code = bsm_read_command_3(tag_value, sector_value, LBA_value, (unsigned char *)fake_write_buff_pa,
//MK0324					vbuf, async_value, cb_function, max_retry_count_value);
//MK0213-end
//MK0324-begin
			ret_code = bsm_read_command_emmc(tag_value, sector_value, LBA_value, (unsigned char *)fake_write_buff_pa, vbuf, async_value, cb_function);
//MK0324-end

		a = hv_nstimeofday();
		latency = a - b;
//MK		pr_info("latency from bsm_r is %ldns\n", latency);
//MK-begin
		pr_info("[%s]: latency of bsm_read_commmand = %ldns\n", __func__, latency);
//MK-end
	} else {
//MK0817		bsm_read_command(tag_value, sector_value, LBA_value, dst_buf,
//MK0817			async_value, cb_function);
//MK0817-begin
//MK1118		bsm_read_command(tag_value, sector_value, LBA_value, pAlignedBuff,
//MK1118				async_value, cb_function);
//MK1118-begin
//MK0120-begin
//MK0126send_command:
//MK0120-end
//MK0207		bsm_read_command(tag_value, sector_value, LBA_value, (unsigned char *)fake_write_buff_pa,
//MK0207				async_value, cb_function);
//MK1118-end
//MK0817-end

//MK0201//MK0120-begin
//MK0201//MK0126do_fake_write_again:
//MK0201		ret_code = bsm_read_command_2(tag_value, sector_value, LBA_value, (unsigned char *)fake_write_buff_pa,
//MK0201				vbuf, async_value, cb_function, &mr_fpga_checksum);
//MK0201//MK0120-end
//MK0201-begin
//MK0207		ret_code = bsm_read_command_2(tag_value, sector_value, LBA_value, (unsigned char *)fake_write_buff_pa,
//MK0207				vbuf, async_value, cb_function, max_retry_count_value);
//MK0201-end

//MK0207-begin
//MK0213		if (bsm_read_command(tag_value, sector_value, LBA_value, (unsigned char *)fake_write_buff_pa, async_value, cb_function, max_retry_count_value) == 0) {
//MK0213			ret_code = bsm_read_command_2(tag_value, sector_value, LBA_value, (unsigned char *)fake_write_buff_pa,
//MK0213					vbuf, async_value, cb_function, max_retry_count_value);
//MK0213	}
//MK0213-begin
//MK0324			ret_code = bsm_read_command_3(tag_value, sector_value, LBA_value, (unsigned char *)fake_write_buff_pa,
//MK0324					vbuf, async_value, cb_function, max_retry_count_value);
//MK0213-end
//MK0207-end
//MK0324-begin
			ret_code = bsm_read_command_emmc(tag_value, sector_value, LBA_value, (unsigned char *)fake_write_buff_pa, vbuf, async_value, cb_function);
//MK0324-end

#if 0		
		// for test only; loopback here
		src_pt = (unsigned long *)src_buf;
		dst_pt = (unsigned long *)dst_buf;
		for (i=0; i<rand_in_size; i++)  *dst_pt++ = *src_pt++;
#endif
	}

//MK0221//MK0126-begin
//MK0221	if ( (error_count = hd_memcmp_64((void *)p_fake_write_buff_va, (void *)p_fake_read_buff_va, 512, wr_cmp_mask)) != 0 ) {
//MK0221		pr_info("[%s]: ERROR: fake-write buffer != fake-read buffer (error_count=%d)\n", __func__, error_count);
//MK0221	}
//MK0221//MK0126-end

#if 0	//MK0126
//MK0120-begin
	/* Give higher priority to checksum from the user */
	if (expected_checksum_value != 0xFFFFFFFF) {
		if (fpga_checksum == (unsigned char)expected_checksum_value) {
			pr_info("[%s]: checksum matched: expected checksum = %.2x, fpga_checksum = %.2x, fwb_checksum = %.2x, retry_count = %.2d\n",
					__func__, (unsigned char)expected_checksum_value, fpga_checksum, fwb_checksum, retry_count);
		} else if (retry_count != 0) {
			pr_info("[%s]: checksum NOT matched: expected checksum = %.2x, fgpa_checksum = %.2x, fwb_checksum = %.2x, retry_count = %.2d\n",
					__func__, (unsigned char)expected_checksum_value, fpga_checksum, fwb_checksum, retry_count);
			retry_count--;

			if (which_retry_value == 0) {
				goto send_command;
			} else {
				goto do_fake_write_again;
			}
		} else {
			pr_info("[%s]: bsm_read failed: expected checksum value = %.2x, fpga_checksum = %.2x, fwb_checksum = %.2x, retry_count = %.2d\n",
					__func__, (unsigned char)expected_checksum_value, fpga_checksum, fwb_checksum, retry_count);
		}
	} else {
		if (fpga_checksum == frb_checksum) {
			pr_info("[%s]: checksum matched: frb_checksum = %.2x, fpga_checksum = %.2x, fwb_checksum = %.2x, retry_count = %.2d\n",
					__func__, frb_checksum, fpga_checksum, fwb_checksum, retry_count);
		} else if (retry_count != 0) {
			pr_info("[%s]: checksum NOT matched: frb_checksum = %.2x, fgpa_checksum = %.2x, fwb_checksum = %.2x, retry_count = %.2d\n",
					__func__, frb_checksum, fpga_checksum, fwb_checksum, retry_count);
			retry_count--;

			if (which_retry_value == 0) {
				goto send_command;
			} else {
				goto do_fake_write_again;
			}
		} else {
			pr_info("[%s]: bsm_read failed: frb_checksum = %.2x, fpga_checksum = %.2x, fwb_checksum = %.2x, retry_count = %.2d\n",
					__func__, frb_checksum, fpga_checksum, fwb_checksum, retry_count);
		}
	}
//MK0120-end
#endif	//MK0126

//MK0817-begin
	/* Display total 512 bytes, total 16 lines, 32 bytes per each line */
//MK1109	for (i=0; i < 16; i++)
//MK1109	{
//MK1109		pr_debug("[%s]: 0x%.16lx - 0x%.16lx - 0x%.16lx - 0x%.16lx\n",
//MK1109				__func__, *pTemp, *(pTemp+1), *(pTemp+2), *(pTemp+3));
//MK1109		pTemp += 4;
//MK1109	}
//MK0817-end

//MK1118-begin
errorExit:
//MK1118-end

//MK20170710-begin
	/* Save the error code from BSM_READ API */
	bsm_wr_result = ret_code;
//MK20170710-end

	return simple_write_to_buffer(ker_buf_br, len, position, user_buffer,
	count);
}

/* view mmls write file operation */
static ssize_t view_mmls_wr(struct file *fp, char __user *user_buffer,
				size_t count, loff_t *position)
{
	pr_debug("%s\n", __func__);

	return simple_read_from_buffer(user_buffer, count, position, ker_buf_mw,
	len);
}

/* mmls write file operation */
static ssize_t mmls_wr(struct file *fp, const char __user *user_buffer,
				size_t count, loff_t *position)
{
#if 0	//MK0724
	unsigned long l_data;
	unsigned long *tmp_ptr;
	int i;
	/* pr_info("%s, count=%d\n", __func__, count); */

	/* pre-arrange some data */
	rand_in_size = calc_long_size(sector_value);
	tmp_ptr = (unsigned long *)src_buf;
	pr_debug("use_4k_file=%d\n", use_4k_file);

	if (!use_4k_file){
		for (i =0; i<rand_in_size; i++){
			l_data = rand_long();
//MK			if (i<10) pr_debug("l_data=0x%lx, rand_in_size=%d\n", l_data, rand_in_size);
//MK-begin
			if (i<10) pr_debug("l_data=0x%lx, rand_in_size=%lu\n", l_data, rand_in_size);
//MK-end
			*tmp_ptr++ = l_data;
		}
	}

	async_value = get_async_mode();

	if ((latency_mode==MMLS_W_LTNCY)||(latency_mode==MMLS_RW_LTNCY)||(latency_mode==ALL_RW_LTNCY)){
		b = hv_nstimeofday();
		mmls_write_command(tag_value, sector_value, LBA_value,
			(unsigned long)src_buf, async_value, cb_function);
		a = hv_nstimeofday();
		latency = a - b;
		pr_info("latency from mmls_w is %ldns\n", latency);
	} else
		mmls_write_command(tag_value, sector_value, LBA_value,
			(unsigned long)src_buf, async_value, cb_function);


	return simple_write_to_buffer(ker_buf_mw, len, position, user_buffer,
	count);

#else	//MK0724

//MK0924	unsigned long l_data;
//MK0924	unsigned long *tmp_ptr;
//MK	int i;
//MK-begin
//	unsigned long i;
//MK1118	unsigned char *pAlignedBuff;
//MK1118-begin
//MK0126	unsigned char *vbuf = (unsigned char *)((unsigned long)p_hvbuffer_va + ((unsigned long)fake_read_buff_pa - hvbuffer_pa));
//MK0126-begin
	unsigned char *vbuf = get_fake_read_buff_va();
//MK0126-end
//MK1118-end
//MK-end

//MK0201-begin
	int ret_code=0;
//MK0201-end

	/* pr_debug("bsm_wr, count=%d, len=%d\n", *(int *)&count, len); */
	rand_in_size = calc_long_size(sector_value);
//MK0724	tmp_ptr = (unsigned long *)src_buf;
//MK0724-begin
	/*
	 * Calculate the first 4K-aligned address from the base address of
	 * the source buffer
	 */
//MK1118	pAlignedBuff = (unsigned char *) p_hvbuffer_va;
//MK1118	pAlignedBuff = (unsigned char *) ((unsigned long)(pAlignedBuff + 0x0FFF) & 0xFFFFFFFFFFFFF000);
//MK0914	tmp_ptr = (unsigned long *) pAlignedBuff;
//MK0914	l_data = 0x55555555AAAAAAAA;
//MK0914	pr_debug("[%s]: unaligned addr=0x%.16lx 4K-aligned addr=0x%.16lx data pattern=0x%.16lx\n", __func__, (unsigned long)pAlignedBuff, (unsigned long) tmp_ptr, l_data);
//MK0914-begin
//MK1109	generate_fake_read_data();
//MK0914-end
//MK0724-end

//MK	pr_debug("use_4k_file=%d\n", use_4k_file);
//MK-begin
//	pr_debug("[%s]: use_4k_file=%d, rand_in_size=%ul\n", __func__, use_4k_file, rand_in_size);
//MK-end

#if 0	//MK0726 let's not perform any write to HVDIMM memory while doing the fake read. The pattern was already prepared in the command session.
	if (!use_4k_file){
		for (i =0; i<rand_in_size; i++){
//MK			l_data = rand_long();
//MK			if (i<10) pr_debug("l_data=0x%lx, rand_in_size=%d\n", l_data, rand_in_size);
//MK			*tmp_ptr++ = l_data;
//MK-begin
			/* random_in_size = 64 qwords when sector_value = 1 */
			/* Duplicate the least significant 16-bit in upper 16-bit fields */
//			l_data = i & 0x000000000000FFFF;
//			l_data = l_data | (l_data << 16) | (l_data << 32) | (l_data << 48);
			*tmp_ptr++ = l_data;
//MK-end
		}
	}
#endif	//MK0726

	async_value = get_async_mode();

	if ((latency_mode==BSM_W_LTNCY)||(latency_mode==BSM_RW_LTNCY)||(latency_mode==ALL_RW_LTNCY)){
		b = hv_nstimeofday();
//MK0201//MK0724		bsm_write_command(tag_value, sector_value, LBA_value, src_buf,
//MK0201//MK0724			async_value, cb_function);
//MK0201//MK0724-begin
//MK0201//MK1118		bsm_write_command_2(tag_value, sector_value, LBA_value, pAlignedBuff, async_value, cb_function);
//MK0201//MK1118-begin
//MK0201		bsm_write_command_2(tag_value, sector_value, LBA_value, (unsigned char *)fake_read_buff_pa, vbuf, async_value, cb_function);
//MK0201//MK1118-end
//MK0201//MK0724-end
//MK0201-begin
		ret_code = bsm_write_command_2(tag_value, sector_value, LBA_value, (unsigned char *)fake_read_buff_pa, vbuf, async_value, cb_function, max_retry_count_value);
//MK0201-end

		a = hv_nstimeofday();
		latency = a - b;
//MK		pr_info("latency from bsm_w is %ldns\n", latency);
//MK-begin
		pr_info("[%s]: latency of bsm_write_commmand = %ldns\n", __func__, latency);
//MK-end
	} else
//MK0201//MK0724		bsm_write_command(tag_value, sector_value, LBA_value, src_buf,
//MK0201//MK0724			async_value, cb_function);
//MK0201//MK0724-begin
//MK0201//MK1118		bsm_write_command_2(tag_value, sector_value, LBA_value, pAlignedBuff, async_value, cb_function);
//MK0201//MK1118-begin
//MK0201		bsm_write_command_2(tag_value, sector_value, LBA_value, (unsigned char *)fake_read_buff_pa, vbuf, async_value, cb_function);
//MK0201//MK1118-end
//MK0201//MK0724-end
//MK0201-begin
		ret_code = bsm_write_command_2(tag_value, sector_value, LBA_value, (unsigned char *)fake_read_buff_pa, vbuf, async_value, cb_function, max_retry_count_value);
//MK0201-end

	return simple_write_to_buffer(ker_buf_bw, len, position, user_buffer,
	count);

#endif	//MK0724
}

/* view mmls read file operation */
static ssize_t view_mmls_rd(struct file *fp, char __user *user_buffer,
				size_t count, loff_t *position)
{
	pr_debug("%s ...\n", __func__);

	return simple_read_from_buffer(user_buffer, count, position, ker_buf_mr,
	len);
}

/* mmls read file operation */
static ssize_t mmls_rd(struct file *fp, const char __user *user_buffer,
				size_t count, loff_t *position)
{
//	int i;
//	unsigned long *src_pt, *dst_pt;
//MK0817-begin
//MK1109	unsigned int i;
//MK1118	unsigned char *pAlignedBuff;
//MK1118	unsigned long *pTemp;
//MK1118-begin
//MK0126	unsigned char *vbuf = (unsigned char *)((unsigned long)p_hvbuffer_va + ((unsigned long)fake_write_buff_pa - hvbuffer_pa));
//MK0126-begin
	unsigned char *vbuf = get_fake_write_buff_va();
//MK0126-end
//MK1118-end

//MK0201//MK0120-begin
//MK0201	unsigned char fpga_checksum=0;
//MK0201//MK0120-end
//MK0201-begin
	int ret_code=0;
//MK0201-end

//MK1024	pAlignedBuff = (unsigned char *) p_hvbuffer_va;
//MK1024-begin
//MK1118	pAlignedBuff = (unsigned char *) (p_hvbuffer_va + MMLS_WRITE_BUFF_SIZE);
//MK1024-end
//MK1118	pAlignedBuff = (unsigned char *) ((unsigned long)(pAlignedBuff + 0x0FFF) & 0xFFFFFFFFFFFFF000);
//MK1118	pTemp = (unsigned long *) pAlignedBuff;

	/* Clear test buffer */
//MK1109	for (i = 0; i < 64; i++)
//MK1109		*(pTemp+i) = 0;
//MK0817-end

	/* pr_debug("%s, count=%d\n", __func__, count); */

	/* pre-arrange some data in kernel buffer, done in driver */

	async_value = get_async_mode();

	if ((latency_mode==MMLS_R_LTNCY)||(latency_mode==MMLS_RW_LTNCY)||(latency_mode==ALL_RW_LTNCY)){
		b = hv_nstimeofday();
//MK0817		mmls_read_command(tag_value, sector_value, LBA_value,
//MK0817		(unsigned long)dst_buf, async_value, cb_function);
//MK0817-begin
//MK1118		bsm_read_command_2(tag_value, sector_value, LBA_value, pAlignedBuff,
//MK1118				async_value, cb_function);
//MK0120//MK1118-begin
//MK0120		bsm_read_command_2(tag_value, sector_value, LBA_value, (unsigned char *)fake_write_buff_pa,
//MK0120				vbuf, async_value, cb_function);
//MK0120//MK1118-end
//MK0817-end

//MK0201//MK0120-begin
//MK0201		bsm_read_command_2(tag_value, sector_value, LBA_value, (unsigned char *)fake_write_buff_pa,
//MK0201				vbuf, async_value, cb_function, &fpga_checksum);
//MK0201//MK0120-end
//MK0201-begin
//MK0410		ret_code = bsm_read_command_2(tag_value, sector_value, LBA_value, (unsigned char *)fake_write_buff_pa,
//MK0410				vbuf, async_value, cb_function, max_retry_count_value);
//MK0201-end

//MK0410-begin
			ret_code = bsm_read_command_3_cmd_only(tag_value, sector_value, LBA_value, (unsigned char *)fake_write_buff_pa, vbuf, async_value, cb_function, 0);
//MK0410-end

		a = hv_nstimeofday();
		latency = a - b;
		pr_info("latency from mmls_rd is %ldns\n", latency);
	} else
//MK0201//MK0817		mmls_read_command(tag_value, sector_value, LBA_value,
//MK0201//MK0817			(unsigned long)dst_buf, async_value, cb_function);
//MK0201//MK0817-begin
//MK0201//MK1118		bsm_read_command_2(tag_value, sector_value, LBA_value, pAlignedBuff,
//MK0201//MK1118				async_value, cb_function);
//MK0201//MK0120//MK1118-begin
//MK0201//MK0120		bsm_read_command_2(tag_value, sector_value, LBA_value, (unsigned char *)fake_write_buff_pa,
//MK0201//MK0120				vbuf, async_value, cb_function);
//MK0201//MK0120//MK1118-end
//MK0201//MK0817-end

//MK0201//MK0120-begin
//MK0201		bsm_read_command_2(tag_value, sector_value, LBA_value, (unsigned char *)fake_write_buff_pa,
//MK0201				vbuf, async_value, cb_function, &fpga_checksum);
//MK0201//MK0120-end
//MK0201-begin
//MK0410		ret_code = bsm_read_command_2(tag_value, sector_value, LBA_value, (unsigned char *)fake_write_buff_pa,
//MK0410				vbuf, async_value, cb_function, max_retry_count_value);
//MK0201-end

//MK0410-begin
			ret_code = bsm_read_command_3_cmd_only(tag_value, sector_value, LBA_value, (unsigned char *)fake_write_buff_pa, vbuf, async_value, cb_function, 0);
//MK0410-end

#if 0		
	// for test only; loopback here
	src_pt = (unsigned long *)src_buf;
	dst_pt = (unsigned long *)dst_buf;
	for (i=0; i<rand_in_size; i++)  *dst_pt++ = *src_pt++;
#endif

//MK0817-begin
	/* Display total 512 bytes, total 16 lines, 32 bytes per each line */
//MK1109	for (i=0; i < 16; i++)
//MK1109	{
//MK1109		pr_debug("[%s]: 0x%.16lx - 0x%.16lx - 0x%.16lx - 0x%.16lx\n",
//MK1109				__func__, *pTemp, *(pTemp+1), *(pTemp+2), *(pTemp+3));
//MK1109		pTemp += 4;
//MK1109	}
//MK0817-end

	return simple_write_to_buffer(ker_buf_mr, len, position, user_buffer,
		count);
}

/* view ecc_train file operation */
static ssize_t view_ecc_train(struct file *fp, char __user *user_buffer,
				size_t count, loff_t *position)
{
	pr_debug("%s ...\n", __func__);

	return simple_read_from_buffer(user_buffer, count, position,
		ker_buf_reset, len);
}

/* ecc_train command file operation */
static ssize_t ecc_train(struct file *fp, const char __user *user_buffer,
				size_t count, loff_t *position)
{
	pr_debug("%s\n", __func__);

	ecc_train_command();

	/* pr_info("ecc_train command is finished!!!"); */

	return simple_write_to_buffer(ker_buf_reset, len, position, user_buffer,
	count);
}


/* view hv_reset file operation */
static ssize_t view_hv_reset(struct file *fp, char __user *user_buffer,
				size_t count, loff_t *position)
{
	pr_debug("%s ...\n", __func__);

	return simple_read_from_buffer(user_buffer, count, position,
		ker_buf_reset, len);
}

/* reset command file operation */
static ssize_t hv_reset(struct file *fp, const char __user *user_buffer,
				size_t count, loff_t *position)
{
	pr_debug("%s\n", __func__);

	reset_command(tag_value);

	pr_info("reset command is finished!!!");

	return simple_write_to_buffer(ker_buf_reset, len, position, user_buffer,
	count);
}

/* view hv_inquiry file operation */
static ssize_t view_hv_inquiry(struct file *fp, char __user *user_buffer,
				size_t count, loff_t *position)
{
	pr_debug("%s ...\n", __func__);

	return simple_read_from_buffer(user_buffer, count, position,
		ker_buf_inq, len);
}

/* inquiry command file operation */
static ssize_t hv_inquiry(struct file *fp, const char __user *user_buffer,
				size_t count, loff_t *position)
{
	pr_debug("%s\n", __func__);

	inquiry_command(tag_value);

	pr_info("inquiry command is finished!!!");

	return simple_write_to_buffer(ker_buf_inq, len, position, user_buffer,
	count);
}

/* view hv_config file operation */
static ssize_t view_hv_config(struct file *fp, char __user *user_buffer,
				size_t count, loff_t *position)
{
	pr_debug("%s ...\n", __func__);

	return simple_read_from_buffer(user_buffer, count, position,
		ker_buf_cfg, len);
}

/* inquiry command file operation */
static ssize_t hv_config(struct file *fp, const char __user *user_buffer,
				size_t count, loff_t *position)
{
	pr_debug("%s\n", __func__);

	config_command(tag_value,
			sz_emmc_value,
			sz_rdimm_value,
			sz_mmls_value,
			sz_bsm_value,
			sz_nvdimm_value,
			to_emmc_value,
			to_rdimm_value,
			to_mmls_value,
			to_bsm_value,
			to_nvdimm_value);

	pr_info("CONFIG command is finished!!!");

	return simple_write_to_buffer(ker_buf_cfg, len, position, user_buffer,
	count);
}

//MK0123-begin
/* View terminate_cmd file operation */
static ssize_t view_terminate_cmd(struct file *fp, char __user *user_buffer,
				size_t count, loff_t *position)
{
	pr_debug("[%s]: entered\n", __func__);
	return simple_read_from_buffer(user_buffer, count, position, ker_buf_mw, len);
}

/* Terminate_cmd file operation */
static ssize_t terminate_cmd(struct file *fp, const char __user *user_buffer,
				size_t count, loff_t *position)
{
	pr_debug("[%s]: entered\n", __func__);

	(void) bsm_terminate_command();
	return simple_write_to_buffer(ker_buf_mw, len, position, user_buffer, count);
}
//MK0123-end

//MK0215-begin
/* View terminate_cmd file operation */
static ssize_t view_debug_feat_enable(struct file *fp, char __user *user_buffer,
				size_t count, loff_t *position)
{
	pr_debug("[%s]: entered\n", __func__);
	return simple_read_from_buffer(user_buffer, count, position, ker_buf_mw, len);
}

/* Terminate_cmd file operation */
static ssize_t debug_feat_enable(struct file *fp, const char __user *user_buffer,
				size_t count, loff_t *position)
{
//MK0221	pr_debug("[%s]: debug_feat = 0x%.8X, debug_feat_bsm_wrt = 0x%.8X, debug_feat_bsm_rd = 0x%.8X\n",
//MK0221			__func__, debug_feat_value, debug_feat_bsm_wrt_value, debug_feat_bsm_rd_value);
//MK0221-begin
	pr_debug("[%s]: debug_feat = 0x%.8X, debug_feat_bsm_wrt = 0x%.8X, debug_feat_bsm_rd = 0x%.8X, user_defined_dealy = 0x%.8X\n",
			__func__, debug_feat_value, debug_feat_bsm_wrt_value, debug_feat_bsm_rd_value, user_defined_delay_value);
//MK0221-end
//MK0224-begin
	pr_debug("[%s]: debug_feat_bsm_wrt_qc_status_delay = %ld ns, debug_feat_bsm_rd_qc_status_delay = %ld ns\n",
			__func__, (unsigned long)debug_feat_bsm_wrt_qc_status_delay_value, (unsigned long)debug_feat_bsm_rd_qc_status_delay_value);
//MK0224-end

//MK0405-begin
	pr_debug("[%s]: bsm_wrt_delay_before_qc = 0x%.8X, bsm_rd_delay_before_qc = 0x%.8X\n",
			__func__, bsm_wrt_delay_before_qc_value, bsm_rd_delay_before_qc_value);
//MK0405-end

//MK0223-begin
	set_debug_feat_flags(debug_feat_value);
	set_debug_feat_bsm_wrt_flags(debug_feat_bsm_wrt_value);
	set_debug_feat_bsm_rd_flags(debug_feat_bsm_rd_value);
//MK0223-end

//MK0221-begin
//MK0605	set_user_defined_delay(user_defined_delay_value);
//MK0605-begin
	set_user_defined_delay_us(0, user_defined_delay_value);
//MK0605-end
//MK0221-end
//MK0224-begin
	set_bsm_wrt_qc_status_delay((unsigned long)debug_feat_bsm_wrt_qc_status_delay_value);
	set_bsm_rd_qc_status_delay((unsigned long)debug_feat_bsm_rd_qc_status_delay_value);
//MK0224-end
//MK0301-begin
	if (debug_feat_bsm_wrt_value & 0x00080000) {
		set_bsm_wrt_dummy_command_lba(debug_feat_bsm_wrt_dummy_command_lba_value);
	}

	if (debug_feat_bsm_wrt_value & 0x00100000) {
		if (debug_feat_bsm_wrt_dummy_read_addr_value >= hvbuffer_pa &&
				debug_feat_bsm_wrt_dummy_read_addr_value <= (TOP_OF_SYS_MEM-4096) ) {
			pr_debug("[%s]: debug_feat_bsm_wrt_dummy_read_addr (PA) = 0x%.16lX  (VA) = 0x%.16lX\n",
					__func__, (unsigned long)debug_feat_bsm_wrt_dummy_read_addr_value,
					((unsigned long)debug_feat_bsm_wrt_dummy_read_addr_value - hvbuffer_pa) + (unsigned long)p_hvbuffer_va);
		} else {
			pr_debug("[%s]: debug_feat_bsm_wrt_dummy_read_addr_value (0x%.16lX) out of range. Set to default value (0x%.16lX)\n",
					__func__, (unsigned long)debug_feat_bsm_wrt_dummy_read_addr_value, (unsigned long)MMLS_WRITE_BUFF_SYS_PADDR);
			debug_feat_bsm_wrt_dummy_read_addr_value = MMLS_WRITE_BUFF_SYS_PADDR;
		}
		set_bsm_wrt_dummy_read_addr( ((unsigned long)debug_feat_bsm_wrt_dummy_read_addr_value - hvbuffer_pa) + (unsigned long)p_hvbuffer_va );
	}

	if (debug_feat_bsm_rd_value & 0x00080000) {
		set_bsm_rd_dummy_command_lba(debug_feat_bsm_rd_dummy_command_lba_value);
	}

	if (debug_feat_bsm_rd_value & 0x00100000) {
		if (debug_feat_bsm_rd_dummy_read_addr_value >= hvbuffer_pa &&
				debug_feat_bsm_rd_dummy_read_addr_value <= (TOP_OF_SYS_MEM-4096) ) {
			pr_debug("[%s]: debug_feat_bsm_rd_dummy_read_addr (PA) = 0x%.16lX  (VA) = 0x%.16lX\n",
					__func__, (unsigned long)debug_feat_bsm_rd_dummy_read_addr_value,
					((unsigned long)debug_feat_bsm_rd_dummy_read_addr_value - hvbuffer_pa) + (unsigned long)p_hvbuffer_va);
		} else {
			pr_debug("[%s]: debug_feat_bsm_rd_dummy_read_addr_value (0x%.16lX) out of range. Set to default value (0x%.16lX)\n",
					__func__, (unsigned long)debug_feat_bsm_rd_dummy_read_addr_value, (unsigned long)MMLS_WRITE_BUFF_SYS_PADDR);
			debug_feat_bsm_rd_dummy_read_addr_value = MMLS_WRITE_BUFF_SYS_PADDR;
		}
		set_bsm_rd_dummy_read_addr( ((unsigned long)debug_feat_bsm_rd_dummy_read_addr_value - hvbuffer_pa) + (unsigned long)p_hvbuffer_va );
	}

	set_pattern_mask(wr_cmp_mask);
//MK0301-end

//MK0405-begin
//MK0605	set_user_defined_delay_ms(0, bsm_wrt_delay_before_qc_value);
//MK0605	set_user_defined_delay_ms(1, bsm_rd_delay_before_qc_value);
//MK0605-begin
	set_user_defined_delay_us(1, bsm_wrt_delay_before_qc_value);
	set_user_defined_delay_us(2, bsm_rd_delay_before_qc_value);
//MK0605-end
//MK0405-end

	return simple_write_to_buffer(ker_buf_mw, len, position, user_buffer, count);
}
//MK0215-end

//MK1111-begin
#if 0	//MK1111 - having a build problem so commented out until resolved.
#include <asm/i387.h>
#pragma GCC push_options
#pragma GCC target ("mmx", "avx")
#define  _MM_MALLOC_H_INCLUDED
#include <x86intrin.h>
#undef _MM_MALLOC_H_INCLUDED
#pragma GCC pop_options
//#include <avxintrin.h>
//#include <xmmintrin.h>
//#include <emmintrin.h>
#endif	//MK1111

void memset_mm_stream_si128(void *pdest, unsigned char c, unsigned long size)
{
#if 0	//MK1111 - having a build problem so commented out until resolved.
	unsigned long i;
	volatile char *p = pdest;

	kernel_fpu_begin();

	__m128i v = _mm_set_epi8(c, c, c, c, c, c, c, c,
							c, c, c, c, c, c, c, c);

	/* Each loop stores 64 bytes of data */
	for (i = 0; i < size; i+=64)
	{
		_mm_stream_si128((__m128i *)&p[i+ 0], v);
		_mm_stream_si128((__m128i *)&p[i+16], v);
		_mm_stream_si128((__m128i *)&p[i+32], v);
		_mm_stream_si128((__m128i *)&p[i+48], v);
	}

	kernel_fpu_end();
#endif	//MK1111
}
//#pragma GCC pop_options


/* view memcpy_test file operation */
static ssize_t view_memcpy_test(struct file *fp, char __user *user_buffer,
				size_t count, loff_t *position)
{
	pr_debug("%s\n", __func__);

	return simple_read_from_buffer(user_buffer, count, position, ker_buf_br,
	len);
}

/* memcpy_test file operation */
static ssize_t memcpy_test(struct file *fp, const char __user *user_buffer,
				size_t count, loff_t *position)
{
	unsigned long t1, t2, t3, t4, t5;
	void *kbuff1=NULL, *kbuff2=NULL, *kbuff3=NULL, *kbuff4=NULL, *kbuff5=NULL;
	unsigned long *pbuff1, *pbuff2, *pbuff3, *pbuff4, *pbuff5;
	unsigned long *pIomBuff1, *pIomBuff2, *pIomBuff3, *pIomBuff4, *pIomBuff5;
	unsigned char *pAlignedBuff;

//------------------------------------------------------------------------------
	/* Test using normal memory allocated by kmalloc() */
	kbuff1 = kmalloc(4096, GFP_KERNEL);
	if (kbuff1 == NULL) goto errorExit;
	kbuff2 = kmalloc(4096, GFP_KERNEL);
	if (kbuff2 == NULL) goto errorExit;
	kbuff3 = kmalloc(4096, GFP_KERNEL);
	if (kbuff3 == NULL) goto errorExit;
	kbuff4 = kmalloc(4096, GFP_KERNEL);
	if (kbuff4 == NULL) goto errorExit;
	kbuff5 = kmalloc(4096, GFP_KERNEL);
	if (kbuff5 == NULL) goto errorExit;

	pr_debug("[%s]: kbuff1: va = 0x%.16lx pa = 0x%.16lx\n", __func__,
			(unsigned long)kbuff1, (unsigned long)virt_to_phys(kbuff1));
	pr_debug("[%s]: kbuff2: va = 0x%.16lx pa = 0x%.16lx\n", __func__,
			(unsigned long)kbuff2, (unsigned long)virt_to_phys(kbuff2));
	pr_debug("[%s]: kbuff3: va = 0x%.16lx pa = 0x%.16lx\n", __func__,
			(unsigned long)kbuff3, (unsigned long)virt_to_phys(kbuff3));
	pr_debug("[%s]: kbuff4: va = 0x%.16lx pa = 0x%.16lx\n", __func__,
			(unsigned long)kbuff4, (unsigned long)virt_to_phys(kbuff4));
	pr_debug("[%s]: kbuff5: va = 0x%.16lx pa = 0x%.16lx\n", __func__,
			(unsigned long)kbuff5, (unsigned long)virt_to_phys(kbuff5));

	memset(kbuff1, 0x11, 4096);
	memset(kbuff2, 0x22, 4096);
	memset(kbuff3, 0x33, 4096);
	memset(kbuff4, 0x44, 4096);
	memset(kbuff5, 0x55, 4096);

	t1 = hv_nstimeofday();
	memcpy(kbuff1, kbuff2, 4096);
//	clflush_cache_range((void *)(kbuff1), 4096);
	t2 = hv_nstimeofday();
	memcpy(kbuff2, kbuff3, 4096);
//	clflush_cache_range((void *)(kbuff2), 4096);
	t3 = hv_nstimeofday();
	memcpy_64B_movnti_2(kbuff3, kbuff4, 4096);
	t4 = hv_nstimeofday();
	memcpy_64B_movnti_2(kbuff4, kbuff5, 4096);
	t5 = hv_nstimeofday();

	pr_debug("[%s]: memcpy              - kbuff1 <- kbuff2 - %ld ns\n", __func__, t2-t1);
	pr_debug("[%s]: memcpy              - kbuff2 <- kbuff3 - %ld ns\n", __func__, t3-t2);
	pr_debug("[%s]: memcpy_64B_movnti_2 - kbuff3 <- kbuff4 - %ld ns\n", __func__, t4-t3);
	pr_debug("[%s]: memcpy_64B_movnti_2 - kbuff4 <- kbuff5 - %ld ns\n", __func__, t5-t4);

//------------------------------------------------------------------------------
	/* Test using reserved memory reserved by memblock_reserve() */
	pbuff1 = (unsigned long *) phys_to_virt(0x480000000);
	pbuff2 = pbuff1 + 4096;
	pbuff3 = pbuff2 + 4096;
	pbuff4 = pbuff3 + 4096;
	pbuff5 = pbuff4 + 4096;

	memset((void *)pbuff1, 0x11, 4096);
	memset((void *)pbuff2, 0x22, 4096);
	memset((void *)pbuff3, 0x33, 4096);
	memset((void *)pbuff4, 0x44, 4096);
	memset((void *)pbuff5, 0x55, 4096);

	pr_debug("[%s]: pbuff1 = 0x%.16lx, *pbuff1 = 0x%.16lx\n", __func__, (unsigned long)pbuff1, *pbuff1);
	pr_debug("[%s]: pbuff2 = 0x%.16lx, *pbuff2 = 0x%.16lx\n", __func__, (unsigned long)pbuff2, *pbuff2);
	pr_debug("[%s]: pbuff3 = 0x%.16lx, *pbuff3 = 0x%.16lx\n", __func__, (unsigned long)pbuff3, *pbuff3);
	pr_debug("[%s]: pbuff4 = 0x%.16lx, *pbuff4 = 0x%.16lx\n", __func__, (unsigned long)pbuff4, *pbuff4);
	pr_debug("[%s]: pbuff5 = 0x%.16lx, *pbuff5 = 0x%.16lx\n", __func__, (unsigned long)pbuff5, *pbuff5);

	t1 = hv_nstimeofday();
	memcpy((void *)pbuff1, (void *)pbuff2, 4096);
//	clflush_cache_range((void *)(pbuff1), 4096);
	t2 = hv_nstimeofday();
	memcpy((void *)pbuff2, (void *)pbuff3, 4096);
//	clflush_cache_range((void *)(pbuff2), 4096);
	t3 = hv_nstimeofday();
	memcpy_64B_movnti_2((void *)pbuff3, (void *)pbuff4, 4096);
	t4 = hv_nstimeofday();
	memcpy_64B_movnti_2((void *)pbuff4, (void *)pbuff5, 4096);
	t5 = hv_nstimeofday();

	pr_debug("[%s]: memcpy              - pbuff1 <- pbuff2 - %ld ns\n", __func__, t2-t1);
	pr_debug("[%s]: memcpy              - pbuff2 <- pbuff3 - %ld ns\n", __func__, t3-t2);
	pr_debug("[%s]: memcpy_64B_movnti_2 - pbuff3 <- pbuff4 - %ld ns\n", __func__, t4-t3);
	pr_debug("[%s]: memcpy_64B_movnti_2 - pbuff4 <- pbuff5 - %ld ns\n", __func__, t5-t4);

//------------------------------------------------------------------------------
	/* test using io memory allocated by ioremamp_wc() */
	pAlignedBuff = (unsigned char *) (p_hvbuffer_va);
	pAlignedBuff = (unsigned char *) ((unsigned long)(pAlignedBuff + 0x0FFF) & 0xFFFFFFFFFFFFF000);
	pIomBuff1 = (unsigned long *) pAlignedBuff;
	pIomBuff2 = pIomBuff1 + 4096;
	pIomBuff3 = pIomBuff2 + 4096;
	pIomBuff4 = pIomBuff3 + 4096;
	pIomBuff5 = pIomBuff4 + 4096;

	memset((void *)pIomBuff1, 0x11, 4096);
	memset((void *)pIomBuff2, 0x22, 4096);
	memset((void *)pIomBuff3, 0x33, 4096);
	memset((void *)pIomBuff4, 0x44, 4096);
	memset((void *)pIomBuff5, 0x55, 4096);

	t1 = hv_nstimeofday();
	memcpy((void *)pIomBuff1, (void *)pIomBuff2, 4096);
//	clflush_cache_range((void *)(pIomBuff1), 4096);
	t2 = hv_nstimeofday();
	memcpy((void *)pIomBuff2, (void *)pIomBuff3, 4096);
//	clflush_cache_range((void *)(pIomBuff2), 4096);
	t3 = hv_nstimeofday();
	memcpy_64B_movnti_2((void *)pIomBuff3, (void *)pIomBuff4, 4096);
	t4 = hv_nstimeofday();
	memcpy_64B_movnti_2((void *)pIomBuff4, (void *)pIomBuff5, 4096);
	t5 = hv_nstimeofday();

	pr_debug("[%s]: memcpy              - pIomBuff1 <- pIomBuff2 - %ld ns\n", __func__, t2-t1);
	pr_debug("[%s]: memcpy              - pIomBuff2 <- pIomBuff3 - %ld ns\n", __func__, t3-t2);
	pr_debug("[%s]: memcpy_64B_movnti_2 - pIomBuff3 <- pIomBuff4 - %ld ns\n", __func__, t4-t3);
	pr_debug("[%s]: memcpy_64B_movnti_2 - pIomBuff4 <- pIomBuff5 - %ld ns\n", __func__, t5-t4);

//------------------------------------------------------------------------------

errorExit:
	if (kbuff1 != NULL) kfree(kbuff1);
	if (kbuff2 != NULL) kfree(kbuff2);
	if (kbuff3 != NULL) kfree(kbuff3);
	if (kbuff4 != NULL) kfree(kbuff4);
	if (kbuff5 != NULL) kfree(kbuff5);

	return simple_write_to_buffer(ker_buf_br, len, position, user_buffer, count);
}
//MK1111-end

//MK1215-begin
static const struct file_operations fops_mem_dump = {
	.read = view_mem_dump,
	.write = mem_dump,
};
//MK1215-end

static const struct file_operations fops_wr_cmp = {
	.read = view_wr_cmp,
	.write = wr_cmp,
};

static const struct file_operations fops_file_4k_in = {
	.read = view_file4k_in,
	.write = file4k_in,
};

static const struct file_operations fops_file_4k_out = {
	.read = view_file4k_out,
	.write = file4k_out,
};

static const struct file_operations fops_bsm_wr = {
	.read = view_bsm_wr,
	.write = bsm_wr,
};

static const struct file_operations fops_bsm_rd = {
	.read = view_bsm_rd,
	.write = bsm_rd,
};

static const struct file_operations fops_mmls_wr = {
	.read = view_mmls_wr,
	.write = mmls_wr,
};

static const struct file_operations fops_mmls_rd = {
	.read = view_mmls_rd,
	.write = mmls_rd,
};

static const struct file_operations fops_ecc_train = {
	.read = view_ecc_train,
	.write = ecc_train,
};

static const struct file_operations fops_hv_reset = {
	.read = view_hv_reset,
	.write = hv_reset,
};

static const struct file_operations fops_hv_inquiry = {
	.read = view_hv_inquiry,
	.write = hv_inquiry,
};

static const struct file_operations fops_hv_config = {
	.read = view_hv_config,
	.write = hv_config,
};

//MK1111-begin
static const struct file_operations fops_memcpy_test = {
	.read = view_memcpy_test,
	.write = memcpy_test,
};
//MK1111-end

//MK0123-begin
static const struct file_operations fops_terminate_cmd = {
	.read = view_terminate_cmd,
	.write = terminate_cmd,
};
//MK0123-end

//MK0215-begin
static const struct file_operations fops_debug_feat_enable = {
	.read = view_debug_feat_enable,
	.write = debug_feat_enable,
};
//MK0215-end

//MK0421-begin
static const struct file_operations fops_load_frb = {
	.read = view_load_frb,
	.write = load_frb,
};
//MK0421-end

int single_cmd_init(void)
{
	pr_info("%s: entered single command test ...\n", __func__);

//MK the following init was already done from hv_init() before calling this
//MK this routine.
//MK	hv_io_init();

	/* create a directory by the name dell in /sys/kernel/debugfs */
	dirret = debugfs_create_dir("hvcmd_test", NULL);

//MK1215-begin
	/* Memory dump operations */
	fileret = debugfs_create_file("mem_dump", 0644, dirret, &file_mem_dump,
	&fops_mem_dump);
	if (!fileret) {
		/* Abort module loading */
		pr_err("init_debug: failed to create file for mem_dump");
	}
//MK1215-end

	/* This requires BSM write file operations */
	fileret = debugfs_create_file("wr_cmp", 0644, dirret, &file_wr_cmp,
	&fops_wr_cmp);
	if (!fileret) {
		/* Abort module loading */
		pr_err("init_debug: failed to create file for	wr_cmp");
	}

	/* This creates 4k file input */
	fileret = debugfs_create_file("4k_file_in", 0644, dirret, &file_4k_in,
	&fops_file_4k_in);
	if (!fileret) {
		/* Abort module loading */
		pr_err("init_debug: failed to create file for input 4k data ");
	}

	/* This creates 4k file output */
	fileret = debugfs_create_file("4k_file_out", 0644, dirret, &file_4k_out,
	&fops_file_4k_out);
	if (!fileret) {
		/* Abort module loading */
		pr_err("init_debug: failed to create file for output 4k data ");
	}

	/* create a file in the above directory
	This requires BSM write file operations */
	fileret = debugfs_create_file("bsm_write", 0644, dirret, &file_bsm_w,
	&fops_bsm_wr);
	if (!fileret) {
		/* Abort module loading */
		pr_err("init_debug: failed to create file for	bsm_write");
	}

	/* This requires BSM read file operations */
	fileret = debugfs_create_file("bsm_read", 0644, dirret, &file_bsm_r,
	&fops_bsm_rd);
	if (!fileret) {
		/* Abort module loading */
		pr_err("init_debug: failed to create file for	bsm_read");
	}

	/* create a file in the above directory
	This requires MMLS write file operations */
	fileret = debugfs_create_file("mmls_write", 0644, dirret, &file_mmls_w,
	&fops_mmls_wr);
	if (!fileret) {
		/* Abort module loading */
		pr_err("init_debug: failed to create file for	mmls_write");
	}

	/* This requires MMLS read file operations */
	fileret = debugfs_create_file("mmls_read", 0644, dirret, &file_mmls_r,
	&fops_mmls_rd);
	if (!fileret) {
		/* Abort module loading */
		pr_err("init_debug: failed to create file for mmls_read");
	}

	/* This requires "ecc train" file operations */
	fileret = debugfs_create_file("ecc_train", 0644, dirret,
		&file_ecc_train, &fops_ecc_train);
	if (!fileret) {
		/* Abort module loading */
		pr_err("init_debug: failed to create file for ecc_train");
	}

	/* This requires hv_reset file operations */
	fileret = debugfs_create_file("hv_reset", 0644, dirret, &file_reset,
	&fops_hv_reset);
	if (!fileret) {
		/* Abort module loading */
		pr_err("init_debug: failed to create file for hv_reset");
	}

	/* This requires hv_inquiry file operations */
	fileret = debugfs_create_file("hv_inquiry", 0644, dirret, &file_inq,
	&fops_hv_inquiry);
	if (!fileret) {
		/* Abort module loading */
		pr_err("init_debug: failed to create file for hv_inquiry");
	}

	/* This requires hv_config file operations */
	fileret = debugfs_create_file("hv_config", 0644, dirret, &file_confg,
	&fops_hv_config);
	if (!fileret) {
		/* Abort module loading */
		pr_err("init_debug: failed to create file for hv_config");
	}

//MK0123-begin
	/* This requires TERMINATE command operations */
	fileret = debugfs_create_file("terminate_cmd", 0644, dirret, &file_terminate_cmd,
	&fops_terminate_cmd);
	if (!fileret) {
		/* Abort module loading */
		pr_err("init_debug: failed to create file for terminate_cmd");
	}
//MK0123-end

//MK0215-begin
	/* create a file in the above directory
	This requires BSM write file operations */
	fileret = debugfs_create_file("debug_feat_enable", 0644, dirret, &file_debug_feat_enable,
	&fops_debug_feat_enable);
	if (!fileret) {
		/* Abort module loading */
		pr_err("init_debug: failed to create file for debug_feat_enable");
	}
//MK0215-end

//MK0421-begin
	/* Create load_frb file */
	fileret = debugfs_create_file("load_frb", 0644, dirret, &file_load_frb,
	&fops_load_frb);
	if (!fileret) {
		/* Abort module loading */
		pr_err("init_debug: failed to create load_frb file");
	}
//MK0421-end

//MK1006-begin
	/* Create input variable, extdat, for bsm/mmls command */
	/* Generate test data internally when extdat = 0 */
	/* Use data stored in ker_buf_4k_out as test pattern when extdat = 61 */
	/* Test data in ker_buf_4k_out come thru file4k_out() */
	u32int = debugfs_create_u32("extdat", 0644, dirret, &extdat_value);
	if (!u32int) {
		pr_err("error in creating extdat file");
		return (-ENODEV);
	}
//MK1006-end

//MK0208-begin
	/* Create input variable, intdat_idx, for bsm/mmls command */
	/* This value specifies the type of test pattern internally */
	/* generated. */
	u32int = debugfs_create_u32("intdat_idx", 0644, dirret, &intdat_idx_value);
	if (!u32int) {
		pr_err("error in creating intdat_idx file");
		return (-ENODEV);
	}
//MK0208-end

//MK1118-begin
	/*
	 * Create input variable, fake_read_buff_pa for bsm/mmls_wr command
	 * A value written to /sys/kernel/debug/hvcmd_test/fake_read_buff_pa
	 * will be stored in fake_read_buff_pa and will be used for
	 * fake-read operation.
	 */
	u64int = debugfs_create_u64("fake_read_buff_pa", 0644, dirret, &fake_read_buff_pa);
	if (!u64int) {
		pr_err("error in creating fake_read_buff_pa file");
		return (-ENODEV);
	}

	/*
	 * Create input variable, fake_write_buff_pa for bsm/mmls_rd command
	 * A value written to /sys/kernel/debug/hvcmd_test/fake_write_buff_pa
	 * will be stored in fake_write_buff_pa and will be used for
	 * fake-write operation.
	 */
	u64int = debugfs_create_u64("fake_write_buff_pa", 0644, dirret, &fake_write_buff_pa);
	if (!u64int) {
		pr_err("error in creating fake_write_buff_pa file");
		return (-ENODEV);
	}
//MK1118-end

	/* create input variable, tag, for bsm/mmls command */
	u32int = debugfs_create_u32("tag", 0644, dirret, &tag_value);
	if (!u32int) {
		pr_err("error in creating tag file");
		return (-ENODEV);
	}
#if 0
	/* create input variable, async, for bsm/mmls command */
	u32int = debugfs_create_u32("async", 0644, dirret, &async_value);
	if (!u32int) {
		pr_err("error in creating async file");
		return (-ENODEV);
	}
#endif
	/* create input variable, sector, for bsm/mmls command */
	u64int = debugfs_create_u64("sector", 0644, dirret, &sector_value);
	if (!u64int) {
		pr_err("error in creating sector file");
		return (-ENODEV);
	}

	/* create input variable, LBA, for bsm/mmls command */
	u64int = debugfs_create_u64("LBA", 0644, dirret, &LBA_value);
	if (!u64int) {
		pr_err("error in creating LBA file");
		return (-ENODEV);
	}

	/* create output variable, comp_result, for wr_cmp command */
	u32int = debugfs_create_u32("cmp_result", 0644, dirret, &comp_result);
	if (!u32int) {
		pr_err("error in creating cmp_result file");
		return (-ENODEV);
	}

	/* create latency variable, hv_time, for latency test purpose */
	u32int = debugfs_create_u32("latency_mode", 0644, dirret, &latency_mode);
	if (!u32int) {
		pr_err("error in creating latency_mode file");
		return (-ENODEV);
	}

	/* create input variable, size_eMMC, for CONFIG command */
	u64int = debugfs_create_u64("size_eMMC", 0644, dirret, &sz_emmc_value);
	if (!u64int) {
		pr_err("error in creating size_eMMC file");
		return (-ENODEV);
	}

	/* create input variable, size_RDIMM, for CONFIG command */
	u64int = debugfs_create_u64("size_RDIMM", 0644, dirret,
		&sz_rdimm_value);
	if (!u64int) {
		pr_err("error in creating size_RDIMM file");
		return (-ENODEV);
	}

	/* create input variable, size_MMLS, for CONFIG command */
	u64int = debugfs_create_u64("size_MMLS", 0644, dirret, &sz_mmls_value);
	if (!u64int) {
		pr_err("error in creating size_MMLS file");
		return (-ENODEV);
	}

	/* create input variable, size_BSM, for CONFIG command */
	u64int = debugfs_create_u64("size_BSM", 0644, dirret, &sz_bsm_value);
	if (!u64int) {
		pr_err("error in creating size_BSM file");
		return (-ENODEV);
	}

	/* create input variable, size_NVDIMM, for CONFIG command */
	u64int = debugfs_create_u64("size_NVDIMM", 0644, dirret,
			&sz_nvdimm_value);
	if (!u64int) {
		pr_err("error in creating size_NVDIMM file");
		return (-ENODEV);
	}

	/* create input variable, timeout_eMMC, for CONFIG command */
	u64int = debugfs_create_u64("timeout_eMMC", 0644, dirret,
			&to_emmc_value);
	if (!u64int) {
		pr_err("error in creating timeout_eMMC file");
		return (-ENODEV);
	}

	/* create input time out variable, timeout_RDIMM, for CONFIG command */
	u64int = debugfs_create_u64("timeout_RDIMM", 0644, dirret,
			&to_rdimm_value);
	if (!u64int) {
		pr_err("error in creating timeout_RDIMM file");
		return (-ENODEV);
	}

	/* create input time out variable, timeout_MMLS, for CONFIG command */
	u64int = debugfs_create_u64("timeout_MMLS", 0644, dirret,
			&to_mmls_value);
	if (!u64int) {
		pr_err("error in creating timeout_MMLS file");
		return (-ENODEV);
	}

	/* create input time out variable, timeout_BSM, for CONFIG command */
	u64int = debugfs_create_u64("timeout_BSM", 0644, dirret,
			&to_bsm_value);
	if (!u64int) {
		pr_err("error in creating timeout_BSM file");
		return (-ENODEV);
	}

	/* create input time out variable, timeout_NVDIMM, for CONFIG command */
	u64int = debugfs_create_u64("timeout_NVDIMM", 0644, dirret,
			&to_nvdimm_value);
	if (!u64int) {
		pr_err("error in creating timeout_NVDIMM file");
		return (-ENODEV);
	}

//MK1111-begin
	/* This requires memcpy_test file operations */
	fileret = debugfs_create_file("memcpy_test", 0644, dirret, &file_memcpy_test,
	&fops_memcpy_test);
	if (!fileret) {
		/* Abort module loading */
		pr_err("init_debug: failed to create file for memcpy_test");
	}
//MK1111-end

//MK0117-begin
	/* Create input variable, max_retry_count, for bsm command */
	u32int = debugfs_create_u32("max_retry_count", 0644, dirret, &max_retry_count_value);
	if (!u32int) {
		pr_err("error in creating max_retry_count file");
		return (-ENODEV);
	}

	/* Create input variable, which_retry, for bsm command */
	u32int = debugfs_create_u32("which_retry", 0644, dirret, &which_retry_value);
	if (!u32int) {
		pr_err("error in creating which_retry file");
		return (-ENODEV);
	}
//MK0117-end

//MK0120-begin
	/* Create input variable, expected_checksum, for bsm read command */
	u32int = debugfs_create_u32("expected_checksum", 0644, dirret, &expected_checksum_value);
	if (!u32int) {
		pr_err("error in creating expected_checksum file");
		return (-ENODEV);
	}
//MK0120-end

//MK0223-begin
	/* Create input variable, debug_feat, for bsm command */
	u32int = debugfs_create_u32("debug_feat", 0644, dirret, &debug_feat_value);
	if (!u32int) {
		pr_err("error in creating debug_feat file");
		return (-ENODEV);
	}

	/* Create input variable, debug_feat_bsm_wrt, for bsm command */
	u32int = debugfs_create_u32("debug_feat_bsm_wrt", 0644, dirret, &debug_feat_bsm_wrt_value);
	if (!u32int) {
		pr_err("error in creating debug_feat_bsm_wrt file");
		return (-ENODEV);
	}

	/* Create input variable, debug_feat_bsm_rd, for bsm command */
	u32int = debugfs_create_u32("debug_feat_bsm_rd", 0644, dirret, &debug_feat_bsm_rd_value);
	if (!u32int) {
		pr_err("error in creating debug_feat_bsm_rd file");
		return (-ENODEV);
	}
//MK0223-end

//MK0221-begin
	/* Create input variable, user_defined_delay, for bsm command */
	u32int = debugfs_create_u32("user_defined_delay", 0644, dirret, &user_defined_delay_value);
	if (!u32int) {
		pr_err("error in creating user_defined_delay file");
		return (-ENODEV);
	}
//MK0221-end

//MK0224-begin
	/* Create input variable, debug_feat_bsm_wrt_qc_status_delay_value */
	u64int = debugfs_create_u64("debug_feat_bsm_wrt_qc_status_delay", 0644, dirret, &debug_feat_bsm_wrt_qc_status_delay_value);
	if (!u64int) {
		pr_err("error in creating debug_feat_bsm_wrt_qc_status_delay file");
		return (-ENODEV);
	}

	/* Create input variable, debug_feat_bsm_rd_qc_status_delay_value */
	u64int = debugfs_create_u64("debug_feat_bsm_rd_qc_status_delay", 0644, dirret, &debug_feat_bsm_rd_qc_status_delay_value);
	if (!u64int) {
		pr_err("error in creating debug_feat_bsm_rd_qc_status_delay file");
		return (-ENODEV);
	}
//MK0224-end

//MK0301-begin
	/* Create input variable, debug_feat_bsm_wrt_dummy_command_lba_value */
	u32int = debugfs_create_u32("debug_feat_bsm_wrt_dummy_command_lba", 0644, dirret, &debug_feat_bsm_wrt_dummy_command_lba_value);
	if (!u32int) {
		pr_err("error in creating debug_feat_bsm_wrt_dummy_command_lba file");
		return (-ENODEV);
	}

	/* Create input variable, debug_feat_bsm_rd_dummy_command_lba_value */
	u32int = debugfs_create_u32("debug_feat_bsm_rd_dummy_command_lba", 0644, dirret, &debug_feat_bsm_rd_dummy_command_lba_value);
	if (!u32int) {
		pr_err("error in creating debug_feat_bsm_rd_dummy_command_lba file");
		return (-ENODEV);
	}

	/* Create input variable, debug_feat_bsm_wrt_dummy_read_addr_value */
	u64int = debugfs_create_u64("debug_feat_bsm_wrt_dummy_read_addr", 0644, dirret, &debug_feat_bsm_wrt_dummy_read_addr_value);
	if (!u64int) {
		pr_err("error in creating debug_feat_bsm_wrt_dummy_read_addr file");
		return (-ENODEV);
	}

	/* Create input variable, debug_feat_bsm_rd_dummy_read_addr_value */
	u64int = debugfs_create_u64("debug_feat_bsm_rd_dummy_read_addr", 0644, dirret, &debug_feat_bsm_rd_dummy_read_addr_value);
	if (!u64int) {
		pr_err("error in creating debug_feat_bsm_rd_dummy_read_addr file");
		return (-ENODEV);
	}
//MK0301-end

//MK0405-begin
	/* Create input variable, bsm_wrt_delay_before_qc, for bsm command */
	u32int = debugfs_create_u32("bsm_wrt_delay_before_qc", 0644, dirret, &bsm_wrt_delay_before_qc_value);
	if (!u32int) {
		pr_err("error in creating bsm_wrt_delay_before_qc file");
		return (-ENODEV);
	}

	/* Create input variable, bsm_rd_delay_before_qc, for bsm command */
	u32int = debugfs_create_u32("bsm_rd_delay_before_qc", 0644, dirret, &bsm_rd_delay_before_qc_value);
	if (!u32int) {
		pr_err("error in creating bsm_rd_delay_before_qc file");
		return (-ENODEV);
	}
//MK0405-end

	hv_cmd_stress_init(dirret);

	spin_for_cmd_init();

//MK0725-begin
	/**
	 * NOTE: request_mem_region() tells the kernel that the command driver wants
	 * to use this range of I/O addresses so the region is reserved and other
	 * drivers can't ask for any part of the region using the same API.
	 * This mechanism doesn't do any kind of mapping, it's a pure reservation
	 * mechanism, which relies on the fact that all kernel device drivers must
	 * be nice, and they must call request_mem_region, check the return value,
	 * and behave properly in case of error. So it is completely logical that
	 * the driver code works without request_mem_region, it's just that it
	 * doesn't comply with the kernel coding rules.
	 */
//MK	hv_request_mem(hvbuffer_pa,	(unsigned long *) p_hvbuffer_va, HV_BUFFER_SIZE, 0, "hvbuffer");
	if (request_mem_region(hvbuffer_pa, HV_BUFFER_SIZE, "hvbuffer") == NULL) {
		pr_warn("[%s]: unable to request hvbuffer IO space starting 0x%.16lx size(0x%.8x)\n", __func__, hvbuffer_pa, HV_BUFFER_SIZE);

		/**
		 * The following return stmt is temporarily removed for the following
		 * reason.
		 * The test server had one LRDIMM (16GB) and one HybriDIMM (8GB). Since
		 * we do not support "interleaving" as of today, in order to have the
		 * system BIOS disable "memory interleaving" in this memory
		 * configuration, SPD in HybriDIMM had to be programmed as "NV" and this
		 * makes request_mem_region() fail. Since request_mem_region() is
		 * a courtesy call, we can ignore the error and call ioremap_xyz().
		 */
//MK0817		return -1;
	}

//MK1103	p_hvbuffer_va = (unsigned long *) ioremap_cache(hvbuffer_pa, HV_BUFFER_SIZE);
//MK1103-begin
	p_hvbuffer_va = (unsigned long *) ioremap_wc(hvbuffer_pa, HV_BUFFER_SIZE);
//MK1103-end
	pr_debug("[%s]: p_hvbuffer_va = 0x%.16lx hvbuffer_pa = 0x%.16lx size(0x%.8x)\n",
			__func__, (unsigned long) p_hvbuffer_va, hvbuffer_pa, HV_BUFFER_SIZE);
//MK0725-end
//MK1130-begin
	/* Set default address for BSM_WRITE & BSM_READ respectively */
	p_fake_read_buff_va = p_hvbuffer_va;							// BSM_WRITE
	p_fake_write_buff_va = p_hvbuffer_va + MMLS_WRITE_BUFF_SIZE;	// BSM_READ
	pr_debug("[%s]: p_fake_read_buff_va (default) = 0x%.16lx p_fake_write_buff_va (default) = 0x%.16lx\n",
			__func__, (unsigned long)p_fake_read_buff_va, (unsigned long)p_fake_write_buff_va);
//MK1130-end
	return 0;
}

int cb_function(unsigned short tag_value)
{

	pr_debug("%s, in driver call back func: tag =%d !\n", __func__, tag_value);

	return 1;
}

int single_cmd_exit(void)
{
//MK0725-begin
	iounmap((void *) p_hvbuffer_va);
	release_mem_region(hvbuffer_pa, HV_BUFFER_SIZE);
//MK0725-end

	/* removing the directory recursively which in turn cleans all files */
	debugfs_remove_recursive(dirret);
	return 0;
}

int set_test_data(void)
{
	char *p_a;
	unsigned int i, j, k;

	/* find out bytes per input line */
	p_a = &a_in[0];
	j = 0;
	k = 0;
	for (i = 0; i < 20; i++) {
		if (*p_a != '\n')
			unit_ary[k][j++] = *p_a++;
		else
			break;
	}
	p_a++;
	bytes_per_line = i;
	unit_ary[0][i] = 0;

	/* extract the rest of input data pattern */
	for (k = 1; k < 8; k++) {
		for (j = 0; j < bytes_per_line; j++)
			unit_ary[k][j] = *p_a++;
		unit_ary[k][j] = 0;
		if ((*p_a++) != '\n') {
			pr_info("wrong data!!!\n");
			break;
		}
	}

	/* convert string to long integers */
	for (k = 0; k < 8; k++) {
		/* pr_info("k=%d, unit_ary=%s\n", k, unit_ary[k][0]); */
		/* if (strict_strtoul(unit_ary[k], 0, &in_num[k]) < 0) { */
		if (kstrtoul(unit_ary[k], 0, &in_num[k]) < 0) {
			pr_warn("Unable to parse input string!\n");
			return -EINVAL;
		}
		/* pr_info("input number is: 0x%lx\n", in_num[k]); */
	}

	/* duplicate up to 64 burst */
	for (i = 1; i < 64; i++) {
		for (j = 0; j < 8; j++)
			in_num[i*8+j] = in_num[j];
	}

	return 0;
}

#if 0	//MK0728
static unsigned long rand_long(void)
{
	unsigned long i;

	get_random_bytes(&i, sizeof(i));
	return i;
}
#endif	//MK0728

//MKstatic unsigned int calc_long_size(sectors)
//MK-begin
static unsigned long calc_long_size(unsigned long sectors)
//MK-end
{
//MK	int size;
//MK	size = sectors*512/sizeof(long);
//MKreturn size;
//MK-begin
	return(sectors*512/sizeof(unsigned long));
//MK-end
}

//MK0728-begin
void generate_fake_read_data(void)
{
	unsigned long i, qwdata, max_qword_cnt, *tmp_ptr;
	unsigned char *pAlignedBuff;
//MK1006-begin
	unsigned long *p_ker_buf_4k_out;
//MK1006-end
//MK1110-begin
	unsigned int j;
//MK1110-end

	/*
	 * The physical address for p_hvbuffer_va is supposed to be 4KB aligned
	 * so, this virtual address is also 4KB aligned. But, just to make sure
	 * calculate the first 4K-aligned address from the base address of
	 * the source buffer in HVDIMM.
	 */
	pAlignedBuff = (unsigned char *) p_hvbuffer_va;
	pAlignedBuff = (unsigned char *) ((unsigned long)(pAlignedBuff + 0x0FFF) & 0xFFFFFFFFFFFFF000);
	tmp_ptr = (unsigned long *) pAlignedBuff;
	pr_debug("[%s]: unaligned addr=0x%.16lx 4K-aligned addr=0x%.16lx\n", __func__, (unsigned long)p_hvbuffer_va, (unsigned long) tmp_ptr);

	/* Convert byte count to qword count */
	max_qword_cnt = 4096 >> 3;
//MK1006-begin
	/* Check if the user wants to use test data provided from an external source (extdat_value) */
	/* If the external data is present, it should be in ker_buf_4k_out */
	if (extdat_value == 1) {
		p_ker_buf_4k_out = (unsigned long *) ker_buf_4k_out;
		for (i=0; i < max_qword_cnt; i++)
		{
			/* Copy 64-bit data at a time */
			*(tmp_ptr+i) = *(p_ker_buf_4k_out+i);

//MK1109			pr_debug("[%s]: vaddr=0x%.16lx data=0x%.16lx\n", __func__, (unsigned long)(tmp_ptr+i), *(p_ker_buf_4k_out+i));

			/* Flush cache every 64 bytes */
			if (((i+1) % 8) == 0) {
				clflush_cache_range((void *)(tmp_ptr+i-7), 64);
//MK1109				pr_debug("[%s]: flushed 64 bytes from 0x%.16lx\n", __func__, (unsigned long)(tmp_ptr+i-7));
			}
		}
	} else {
//MK1006-end
//MK1110-begin
		/* Each 64-byte block will have the identical data */
		max_qword_cnt >>= 3;
//MK1110-end
		/* Otherwise, the test data will be generated internally */
		for (i=1; i <= max_qword_cnt; i++)
		{
			/* random_in_size = 64 qwords when sector_value = 1 */
			/* Duplicate the least significant 16-bit in upper 16-bit fields */
//MK1110			qwdata = i & 0x000000000000FFFF;
//MK1110			qwdata = qwdata | (qwdata << 16) | (qwdata << 32) | (qwdata << 48);
//MK1110-begin
			qwdata = i & 0x000000000000FF;
			qwdata = qwdata | (qwdata << 8) | (qwdata << 16) | (qwdata << 24) |
					(qwdata << 32) | (qwdata << 40) | (qwdata << 48) | (qwdata << 56);
//MK1110-end
//			pr_debug("[%s]: vaddr=0x%.16lx data=0x%.16lx\n", __func__, (unsigned long)tmp_ptr, qwdata);

			/* Write 64-bit data and increment 64-bit pointer by one */
//MK1110			*tmp_ptr++ = qwdata;
//MK1110			/* Flush cache every 64 bytes */
//MK1110			if ((i % 8) == 0) {
//MK1110				clflush_cache_range((void *)(tmp_ptr-8), 64);
//MK1110//MK1109				pr_debug("[%s]: flushed 64 bytes from 0x%.16lx\n", __func__, (unsigned long)(tmp_ptr-8));
//MK1110			}
//MK1110-begin
			for (j=0; j < 8; j++)
			{
				*(tmp_ptr+j) = qwdata;
			}

			/* Flush cache every 64 bytes */
			clflush_cache_range((void *)tmp_ptr, 64);

			/* Advance to the next 64-byte block */
			tmp_ptr += j;
//MK1110-end

		} // end of for (i=1;...
//MK1006-begin

	} // end of if (extdat_value == 1)
//MK1006-end
}

void generate_fake_read_data_2(void)
{
	unsigned long qwdata, i, pattern_data, total_burst_count, burst_index;
	unsigned long *pbuff, *tmp_ptr;
	unsigned char *pAlignedBuff;


	/*
	 * The physical address for p_hvbuffer_va is supposed to be 4KB aligned
	 * so, this virtual address is also 4KB aligned. But, just to make sure
	 * calculate the first 4K-aligned address from the base address of
	 * the source buffer in HVDIMM.
	 */
	pAlignedBuff = (unsigned char *) p_hvbuffer_va;
	pAlignedBuff = (unsigned char *) ((unsigned long)(pAlignedBuff + 0x0FFF) & 0xFFFFFFFFFFFFF000);
	tmp_ptr = (unsigned long *) pAlignedBuff;
	pr_debug("[%s]: unaligned addr=0x%.16lx 4K-aligned addr=0x%.16lx\n", __func__, (unsigned long)p_hvbuffer_va, (unsigned long) tmp_ptr);

	total_burst_count = 4096 >> 6;	// 4KB / 64 bytes
	pattern_data = 0;
	pbuff = tmp_ptr;
	for (burst_index=0; burst_index < total_burst_count; burst_index++) {

		/*
		 * The following for loop is supposed to generate 64 bytes of data,
		 * which are 8 qwords.
		 */
		for (i=0; i < 8; i++)
		{
			/* Duplicate the least significant 16-bit in upper 16-bit fields */
			qwdata = pattern_data & 0x000000000000FFFF;
			qwdata = qwdata | (qwdata << 16) | (qwdata << 32) | (qwdata << 48);

			pr_debug("[%s]: vaddr=0x%.16lx data=0x%.16lx\n", __func__, (unsigned long)pbuff, qwdata);

			/* Write 64-bit data and increment 64-bit pointer by one */
			*pbuff++ = qwdata;
			pattern_data++;
		}

		/* Since we just wrote 64 bytes, flush as much as we wrote. */
		clflush_cache_range((void *)(pbuff-8), 64);

		/*
		 * We just completed writing 64 bytes of data in one 128KB block in
		 * HVDIMM DRAM. Before advancing to the next 128KB block in the other
		 * bank, which is located about 8KB away, move the pointer to the
		 * beginning of the adjacent 128KB block first and then advance the
		 * pointer to the next target 128KB block.
		 * Since the pointer has been advance by 8 qwords from above loop,
		 * we just need to increment the pointer by 8 more to reach the adjacent
		 * 128KB block.
		 */
		pbuff += 8;

		/* Advance ptr to a block with the next burst index */
		if (burst_index % 2 == 0)
			/*
			 * The next 128KB block is (8KB-128bytes) away. "1024" means 1024
			 * qwords (8bytes * 1024 = 8KB) and "16" means 16 qwords,
			 * which is 128 bytes (8bytes * 16 = 128bytes).
			 */
			pbuff = pbuff + 1024 - 16;
		else
			/*
			 * The next 128KB block is (8KB) away. "-1024" means moving 1024
			 * qwords downward.
			 */
			pbuff = pbuff - 1024;

		pr_debug("[%s]:\n", __func__);
	}

}

void generate_fake_read_data_3(void)
{
	unsigned long qwdata, i, pattern_data, total_burst_count, burst_index;
	unsigned long *pbuff, *tmp_ptr;
	unsigned char *pAlignedBuff;


	/*
	 * The physical address for p_hvbuffer_va is supposed to be 4KB aligned
	 * so, this virtual address is also 4KB aligned. But, just to make sure
	 * calculate the first 4K-aligned address from the base address of
	 * the source buffer in HVDIMM.
	 */
	pAlignedBuff = (unsigned char *) p_hvbuffer_va;
	pAlignedBuff = (unsigned char *) ((unsigned long)(pAlignedBuff + 0x0FFF) & 0xFFFFFFFFFFFFF000);
	tmp_ptr = (unsigned long *) pAlignedBuff;
	pr_debug("[%s]: unaligned addr=0x%.16lx 4K-aligned addr=0x%.16lx\n", __func__, (unsigned long)p_hvbuffer_va, (unsigned long) tmp_ptr);

	total_burst_count = 4096 >> 6;	// 4KB / 64 bytes
	pattern_data = 0;
	pbuff = tmp_ptr;
	for (burst_index=0; burst_index < total_burst_count; burst_index++)
	{
		/*
		 * The following loop is supposed to generate 64 bytes of data,
		 * which are 8 qwords.
		 */
		for (i=0; i < 8; i++)
		{
			/* Duplicate the least significant 16-bit in upper 16-bit fields */
			qwdata = pattern_data & 0x000000000000FFFF;
			qwdata = qwdata | (qwdata << 16) | (qwdata << 32) | (qwdata << 48);

			pr_debug("[%s]: vaddr=0x%.16lx data=0x%.16lx\n", __func__, (unsigned long)pbuff, qwdata);

			/* Write 64-bit data and increment 64-bit pointer by one */
			*pbuff++ = qwdata;
			pattern_data++;
		}

		/* Since we just wrote 64 bytes, flush as much as we wrote. */
		clflush_cache_range((void *)(pbuff-8), 64);

		/*
		 * We just completed writing 64 bytes of data in HVDIMM DRAM. Skip
		 * the next 64 bytes of space, which belongs to another bank because
		 * our FPGA doesn't handle this case yet.
		 */
		pbuff += 8;

		pr_debug("[%s]:\n", __func__);
	}

}
//MK0728-end

//MK1115-begin
/* Hao wants the test data to increment every 8 bytes */
void generate_fake_read_data_4(void)
{
	unsigned long i, qwdata, max_qword_cnt, *tmp_ptr;
	unsigned char *pAlignedBuff;
//MK1006-begin
	unsigned long *p_ker_buf_4k_out;
//MK1006-end

	/*
	 * The physical address for p_hvbuffer_va is supposed to be 4KB aligned
	 * so, this virtual address is also 4KB aligned. But, just to make sure
	 * calculate the first 4K-aligned address from the base address of
	 * the source buffer in HVDIMM.
	 */
	pAlignedBuff = (unsigned char *) p_hvbuffer_va;
	pAlignedBuff = (unsigned char *) ((unsigned long)(pAlignedBuff + 0x0FFF) & 0xFFFFFFFFFFFFF000);
	tmp_ptr = (unsigned long *) pAlignedBuff;
	pr_debug("[%s]: unaligned addr=0x%.16lx 4K-aligned addr=0x%.16lx\n", __func__, (unsigned long)p_hvbuffer_va, (unsigned long) tmp_ptr);

	/* Convert byte count to qword count */
	max_qword_cnt = 4096 >> 3;
//MK1006-begin
	/* Check if the user wants to use test data provided from an external source (extdat_value) */
	/* If the external data is present, it should be in ker_buf_4k_out */
	if (extdat_value == 1) {
		p_ker_buf_4k_out = (unsigned long *) ker_buf_4k_out;
		for (i=0; i < max_qword_cnt; i++)
		{
			/* Copy 64-bit data at a time */
			*(tmp_ptr+i) = *(p_ker_buf_4k_out+i);

//MK1109			pr_debug("[%s]: vaddr=0x%.16lx data=0x%.16lx\n", __func__, (unsigned long)(tmp_ptr+i), *(p_ker_buf_4k_out+i));

			/* Flush cache every 64 bytes */
			if (((i+1) % 8) == 0) {
				clflush_cache_range((void *)(tmp_ptr+i-7), 64);
//MK1109				pr_debug("[%s]: flushed 64 bytes from 0x%.16lx\n", __func__, (unsigned long)(tmp_ptr+i-7));
			}
		}
	} else {
//MK1006-end
		/* Otherwise, the test data will be generated internally */
		for (i=1; i <= max_qword_cnt; i++)
		{
			/* random_in_size = 64 qwords when sector_value = 1 */
			/* Duplicate the least significant 16-bit in upper 16-bit fields */
			qwdata = i & 0x00000000000000FF;
			qwdata = qwdata | (qwdata << 8) | (qwdata << 16) | (qwdata << 24) |
					(qwdata << 32) | (qwdata << 40) | (qwdata << 48) | (qwdata << 56);
//			pr_debug("[%s]: vaddr=0x%.16lx data=0x%.16lx\n", __func__, (unsigned long)tmp_ptr, qwdata);

			/* Write 64-bit data and increment 64-bit pointer by one */
			*tmp_ptr++ = qwdata;

			/* Flush cache every 64 bytes */
			if ((i % 8) == 0) {
				clflush_cache_range((void *)(tmp_ptr-8), 64);
//				pr_debug("[%s]: flushed 64 bytes from 0x%.16lx\n", __func__, (unsigned long)(tmp_ptr-8));
			}

		} // end of for (i=1;...
//MK1006-begin

	} // end of if (extdat_value == 1)
//MK1006-end
}
//MK1115-end

//MK1118-begin
/*
 * This function is based on generate_fake_read_data_4(). Only difference is
 * that this routine uses the buffer address from the user,
 * fake_read_buff_pa.
 */
void generate_fake_read_data_5(void)
{
//MK0208	unsigned long i, qwdata, max_qword_cnt, *tmp_ptr;
//MK0208-begin
	unsigned long i, max_qword_cnt, *tmp_ptr=NULL;
//MK0208-end
///	unsigned char *pAlignedBuff;
//MK1006-begin
	unsigned long *p_ker_buf_4k_out;
//MK1006-end

	/*
	 * The physical address for p_hvbuffer_va is supposed to be 4KB aligned
	 * so, this virtual address is also 4KB aligned. But, just to make sure
	 * calculate the first 4K-aligned address from the base address of
	 * the source buffer in HVDIMM.
	 */
//MK0126	tmp_ptr = (unsigned long *)((unsigned long)p_hvbuffer_va + ((unsigned long)fake_read_buff_pa - hvbuffer_pa));
//MK0126-begin
	tmp_ptr = (unsigned long *)get_fake_read_buff_va();
//MK0126-end
///	pAlignedBuff = (unsigned char *) ((unsigned long)p_hvbuffer_va + ((unsigned long)fake_read_buff_pa - hvbuffer_pa));
//	pAlignedBuff = (unsigned char *) ((unsigned long)(pAlignedBuff + 0x0FFF) & 0xFFFFFFFFFFFFF000);
///	tmp_ptr = (unsigned long *) pAlignedBuff;
///	pr_debug("[%s]: fake buff paddr=0x%.16lx - fake buff vaddr=0x%.16lx\n",
///			__func__, (unsigned long)fake_read_buff_pa, (unsigned long)tmp_ptr);
	pr_info("[%s]: PA from User = 0x%.16lx - VA for the user PA = 0x%.16lx\n", __func__,
			(unsigned long)fake_read_buff_pa, (unsigned long)tmp_ptr);

	/* Convert byte count to qword count */
	max_qword_cnt = 4096 >> 3;
//MK1006-begin
	/* Check if the user wants to use test data provided from an external source (extdat_value) */
	/* If the external data is present, it should be in ker_buf_4k_out */
	if (extdat_value == 1) {
		p_ker_buf_4k_out = (unsigned long *) ker_buf_4k_out;
		for (i=0; i < max_qword_cnt; i++)
		{
			/* Copy 64-bit data at a time */
			*(tmp_ptr+i) = *(p_ker_buf_4k_out+i);

//MK1109			pr_debug("[%s]: vaddr=0x%.16lx data=0x%.16lx\n", __func__, (unsigned long)(tmp_ptr+i), *(p_ker_buf_4k_out+i));

			/* Flush cache every 64 bytes */
			if (((i+1) % 8) == 0) {
				clflush_cache_range((void *)(tmp_ptr+i-7), 64);
//MK1109				pr_debug("[%s]: flushed 64 bytes from 0x%.16lx\n", __func__, (unsigned long)(tmp_ptr+i-7));
			}
		}
	} else {
//MK1006-end
//MK0208		/* Otherwise, the test data will be generated internally */
//MK0208		for (i=1; i <= max_qword_cnt; i++)
//MK0208		{
//MK0208			/* random_in_size = 64 qwords when sector_value = 1 */
//MK0208			/* Duplicate the least significant 16-bit in upper 16-bit fields */
//MK0208			qwdata = i & 0x00000000000000FF;
//MK0208			qwdata = qwdata | (qwdata << 8) | (qwdata << 16) | (qwdata << 24) |
//MK0208					(qwdata << 32) | (qwdata << 40) | (qwdata << 48) | (qwdata << 56);
//MK0208//			pr_debug("[%s]: vaddr=0x%.16lx data=0x%.16lx\n", __func__, (unsigned long)tmp_ptr, qwdata);
//MK0208
//MK0208			/* Write 64-bit data and increment 64-bit pointer by one */
//MK0208			*tmp_ptr++ = qwdata;
//MK0208
//MK0208			/* Flush cache every 64 bytes */
//MK0208			if ((i % 8) == 0) {
//MK0208				clflush_cache_range((void *)(tmp_ptr-8), 64);
//MK0208//				pr_debug("[%s]: flushed 64 bytes from 0x%.16lx\n", __func__, (unsigned long)(tmp_ptr-8));
//MK0208			}
//MK0208
//MK0208		} // end of for (i=1;...
//MK0208-begin
		if (intdat_idx_value == 0) {
			pattern_index_0(tmp_ptr, 4096);
		} else if (intdat_idx_value == 2) {
			pattern_index_2(tmp_ptr, 4096);
		} else {
			pattern_index_1(tmp_ptr, 4096);
		}
//MK0208-end

//MK1006-begin

	} // end of if (extdat_value == 1)
//MK1006-end
}
//MK1118-end

//MK0208-begin
/* Each byte in every qword has the same incremental value */
/* 01010101_01010101, 02020202_02020202, ... FFFFFFFF_FFFFFFFF */
void pattern_index_0(unsigned long *pbuff, unsigned long size)
{
	unsigned long i, max_qword_cnt, qwdata;
	unsigned long *tmp_ptr=pbuff;

	/* Convert byte count to qword count */
	max_qword_cnt = size >> 3;

	/* Otherwise, the test data will be generated internally */
	for (i=1; i <= max_qword_cnt; i++)
	{
		/* random_in_size = 64 qwords when sector_value = 1 */
		/* Duplicate the least significant 16-bit in upper 16-bit fields */
		qwdata = i & 0x00000000000000FF;
		qwdata = qwdata | (qwdata << 8) | (qwdata << 16) | (qwdata << 24) |
				(qwdata << 32) | (qwdata << 40) | (qwdata << 48) | (qwdata << 56);
//			pr_debug("[%s]: vaddr=0x%.16lx data=0x%.16lx\n", __func__, (unsigned long)tmp_ptr, qwdata);

		/* Write 64-bit data and increment 64-bit pointer by one */
		*tmp_ptr++ = qwdata;

		/* Flush cache every 64 bytes */
		if ((i % 8) == 0) {
			clflush_cache_range((void *)(tmp_ptr-8), 64);
//				pr_debug("[%s]: flushed 64 bytes from 0x%.16lx\n", __func__, (unsigned long)(tmp_ptr-8));
		}

	} // end of for (i=1;...
}

/* Each dword in every qword has TBM address */
/* 8000_0000, 8000_0004, 8000_0008, ... etc. */
void pattern_index_1(unsigned long *pbuff, unsigned long size)
{
	unsigned long i, max_qword_cnt, qwdata, base;
	unsigned long *tmp_ptr=pbuff;

	/* Convert byte count to qword count */
	max_qword_cnt = size >> 3;

	/* Calculate the base address of TBM, which will be the 1st data */
	base = (LBA_value << 3) + 0x80000000;

	/* Otherwise, the test data will be generated internally */
	for (i=0; i < max_qword_cnt; i++)
	{
		qwdata = base + (i*8);
		qwdata |= ((qwdata+4) << 32);
//			pr_debug("[%s]: vaddr=0x%.16lx data=0x%.16lx\n", __func__, (unsigned long)tmp_ptr, qwdata);

		/* Write 64-bit data and increment 64-bit pointer by one */
		*tmp_ptr++ = qwdata;

		/* Flush cache every 64 bytes */
		if (((i+1) % 8) == 0) {
			clflush_cache_range((void *)(tmp_ptr-8), 64);
//				pr_debug("[%s]: flushed 64 bytes from 0x%.16lx\n", __func__, (unsigned long)(tmp_ptr-8));
		}

	} // end of for (i=1;...
}

/* Patterns will be random numbers */
void pattern_index_2(unsigned long *pbuff, unsigned long size)
{
	get_random_bytes((void *)pbuff, (int)size);
}
//MK0208-end
