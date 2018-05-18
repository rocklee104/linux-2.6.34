/*
 * include/linux/backing-dev.h
 *
 * low-level device information and state which is propagated up through
 * to high-level code.
 */

#ifndef _LINUX_BACKING_DEV_H
#define _LINUX_BACKING_DEV_H

#include <linux/percpu_counter.h>
#include <linux/log2.h>
#include <linux/proportions.h>
#include <linux/kernel.h>
#include <linux/fs.h>
#include <linux/sched.h>
#include <linux/writeback.h>
#include <asm/atomic.h>

struct page;
struct device;
struct dentry;

/*
 * Bits in backing_dev_info.state
 */
enum bdi_state {
	BDI_pending,		/* On its way to being activated */
	BDI_wb_alloc,		/* Default embedded wb allocated */
	BDI_async_congested,	/* The async (write) queue is getting full */
	BDI_sync_congested,	/* The sync queue is getting full */
	BDI_registered,		/* bdi_register() was done */
	BDI_unused,		/* Available bits start here */
};

typedef int (congested_fn)(void *, int);

enum bdi_stat_item {
	BDI_RECLAIMABLE,
	BDI_WRITEBACK,
	NR_BDI_STAT_ITEMS
};

#define BDI_STAT_BATCH (8*(1+ilog2(nr_cpu_ids)))

/* b_dirty/b_io/b_io_more用于隐式wb */
struct bdi_writeback {
	/* 链表成员,链表头是backing_dev_info->wb_list */
	struct list_head list;			/* hangs off the bdi */

	struct backing_dev_info *bdi;		/* our parent bdi */
	/*
	 * 每个BDI可以启动的最大回写线程数,即可关联的bdi_writeback数为32.这个数表示当前writeback在bdi中的index,
	 * 由于当前每个bdi只支持1个writeback线程.所以这个成员恒为0 */
	unsigned int nr;

	/* 上次冲刷旧数据的时间,单位是jiffies */
	unsigned long last_old_flush;		/* last old data flush */

	/* 回写线程 */
	struct task_struct	*task;		/* writeback task */
	/* dirty的inode第一次变dirty被连入这个链表,最靠近链表头的inode是最近才dirty的 */
	struct list_head	b_dirty;	/* dirty inodes */
	/* inode被调度回写时,放入这个链表 */
	struct list_head	b_io;		/* parked for writeback */
	/* 防止一次性完整回写大文件inode导致其他inode饥饿 */
	struct list_head	b_more_io;	/* parked for more writeback */
};

struct backing_dev_info {
	struct list_head bdi_list;
	struct rcu_head rcu_head;
	unsigned long ra_pages;	/* max readahead in PAGE_CACHE_SIZE units */
	unsigned long state;	/* Always use atomic bitops on this */
	unsigned int capabilities; /* Device capabilities */
	/* 判断bdi是否拥塞 */
	congested_fn *congested_fn; /* Function pointer if device is md/dm */
	void *congested_data;	/* Pointer to aux data for congested func */
	/* 泄流的方法 */
	void (*unplug_io_fn)(struct backing_dev_info *, struct page *);
	void *unplug_io_data;

	char *name;

	struct percpu_counter bdi_stat[NR_BDI_STAT_ITEMS];

	struct prop_local_percpu completions;
	int dirty_exceeded;

	/* 脏的最小门限(百分比) */
	unsigned int min_ratio;
	/* 脏的最大门限(百分比) */
	unsigned int max_ratio, max_prop_frac;

	/* 默认回写线程 */
	struct bdi_writeback wb;  /* default writeback info for this bdi */
	spinlock_t wb_lock;	  /* protects update side of wb_list */
	/* 这个BDI的回写线程链表的头部 */
	struct list_head wb_list; /* the flusher threads hanging off this bdi */
	unsigned long wb_mask;	  /* bitmask of registered tasks */
	/* 已经注册的线程数目,目前只支持1 */
	unsigned int wb_cnt;	  /* number of registered tasks */

	/* 冲刷任务的链表头 */
	struct list_head work_list;

	struct device *dev;

#ifdef CONFIG_DEBUG_FS
	struct dentry *debug_dir;
	struct dentry *debug_stats;
#endif
};

int bdi_init(struct backing_dev_info *bdi);
void bdi_destroy(struct backing_dev_info *bdi);

int bdi_register(struct backing_dev_info *bdi, struct device *parent,
		const char *fmt, ...);
int bdi_register_dev(struct backing_dev_info *bdi, dev_t dev);
void bdi_unregister(struct backing_dev_info *bdi);
int bdi_setup_and_register(struct backing_dev_info *, char *, unsigned int);
void bdi_start_writeback(struct backing_dev_info *bdi, struct super_block *sb,
				long nr_pages);
int bdi_writeback_task(struct bdi_writeback *wb);
int bdi_has_dirty_io(struct backing_dev_info *bdi);

extern spinlock_t bdi_lock;
extern struct list_head bdi_list;

static inline int wb_has_dirty_io(struct bdi_writeback *wb)
{
	return !list_empty(&wb->b_dirty) ||
	       !list_empty(&wb->b_io) ||
	       !list_empty(&wb->b_more_io);
}

static inline void __add_bdi_stat(struct backing_dev_info *bdi,
		enum bdi_stat_item item, s64 amount)
{
	__percpu_counter_add(&bdi->bdi_stat[item], amount, BDI_STAT_BATCH);
}

static inline void __inc_bdi_stat(struct backing_dev_info *bdi,
		enum bdi_stat_item item)
{
	__add_bdi_stat(bdi, item, 1);
}

static inline void inc_bdi_stat(struct backing_dev_info *bdi,
		enum bdi_stat_item item)
{
	unsigned long flags;

	local_irq_save(flags);
	__inc_bdi_stat(bdi, item);
	local_irq_restore(flags);
}

static inline void __dec_bdi_stat(struct backing_dev_info *bdi,
		enum bdi_stat_item item)
{
	__add_bdi_stat(bdi, item, -1);
}

static inline void dec_bdi_stat(struct backing_dev_info *bdi,
		enum bdi_stat_item item)
{
	unsigned long flags;

	local_irq_save(flags);
	__dec_bdi_stat(bdi, item);
	local_irq_restore(flags);
}

static inline s64 bdi_stat(struct backing_dev_info *bdi,
		enum bdi_stat_item item)
{
	return percpu_counter_read_positive(&bdi->bdi_stat[item]);
}

static inline s64 __bdi_stat_sum(struct backing_dev_info *bdi,
		enum bdi_stat_item item)
{
	return percpu_counter_sum_positive(&bdi->bdi_stat[item]);
}

static inline s64 bdi_stat_sum(struct backing_dev_info *bdi,
		enum bdi_stat_item item)
{
	s64 sum;
	unsigned long flags;

	local_irq_save(flags);
	sum = __bdi_stat_sum(bdi, item);
	local_irq_restore(flags);

	return sum;
}

extern void bdi_writeout_inc(struct backing_dev_info *bdi);

/*
 * maximal error of a stat counter.
 */
static inline unsigned long bdi_stat_error(struct backing_dev_info *bdi)
{
#ifdef CONFIG_SMP
	return nr_cpu_ids * BDI_STAT_BATCH;
#else
	return 1;
#endif
}

int bdi_set_min_ratio(struct backing_dev_info *bdi, unsigned int min_ratio);
int bdi_set_max_ratio(struct backing_dev_info *bdi, unsigned int max_ratio);

/*
 * Flags in backing_dev_info::capability
 *
 * The first three flags control whether dirty pages will contribute to the
 * VM's accounting and whether writepages() should be called for dirty pages
 * (something that would not, for example, be appropriate for ramfs)
 *
 * WARNING: these flags are closely related and should not normally be
 * used separately.  The BDI_CAP_NO_ACCT_AND_WRITEBACK combines these
 * three flags into a single convenience macro.
 *
 * BDI_CAP_NO_ACCT_DIRTY:  Dirty pages shouldn't contribute to accounting
 * BDI_CAP_NO_WRITEBACK:   Don't write pages back
 * BDI_CAP_NO_ACCT_WB:     Don't automatically account writeback pages
 *
 * These flags let !MMU mmap() govern direct device mapping vs immediate
 * copying more easily for MAP_PRIVATE, especially for ROM filesystems.
 *
 * BDI_CAP_MAP_COPY:       Copy can be mapped (MAP_PRIVATE)
 * BDI_CAP_MAP_DIRECT:     Can be mapped directly (MAP_SHARED)
 * BDI_CAP_READ_MAP:       Can be mapped for reading
 * BDI_CAP_WRITE_MAP:      Can be mapped for writing
 * BDI_CAP_EXEC_MAP:       Can be mapped for execution
 *
 * BDI_CAP_SWAP_BACKED:    Count shmem/tmpfs objects as swap-backed.
 */
#define BDI_CAP_NO_ACCT_DIRTY	0x00000001
#define BDI_CAP_NO_WRITEBACK	0x00000002
#define BDI_CAP_MAP_COPY	0x00000004
#define BDI_CAP_MAP_DIRECT	0x00000008
#define BDI_CAP_READ_MAP	0x00000010
#define BDI_CAP_WRITE_MAP	0x00000020
#define BDI_CAP_EXEC_MAP	0x00000040
#define BDI_CAP_NO_ACCT_WB	0x00000080
#define BDI_CAP_SWAP_BACKED	0x00000100

#define BDI_CAP_VMFLAGS \
	(BDI_CAP_READ_MAP | BDI_CAP_WRITE_MAP | BDI_CAP_EXEC_MAP)

#define BDI_CAP_NO_ACCT_AND_WRITEBACK \
	(BDI_CAP_NO_WRITEBACK | BDI_CAP_NO_ACCT_DIRTY | BDI_CAP_NO_ACCT_WB)

#if defined(VM_MAYREAD) && \
	(BDI_CAP_READ_MAP != VM_MAYREAD || \
	 BDI_CAP_WRITE_MAP != VM_MAYWRITE || \
	 BDI_CAP_EXEC_MAP != VM_MAYEXEC)
#error please change backing_dev_info::capabilities flags
#endif

extern struct backing_dev_info default_backing_dev_info;
extern struct backing_dev_info noop_backing_dev_info;
void default_unplug_io_fn(struct backing_dev_info *bdi, struct page *page);

int writeback_in_progress(struct backing_dev_info *bdi);

static inline int bdi_congested(struct backing_dev_info *bdi, int bdi_bits)
{
	if (bdi->congested_fn)
		return bdi->congested_fn(bdi->congested_data, bdi_bits);
	return (bdi->state & bdi_bits);
}

static inline int bdi_read_congested(struct backing_dev_info *bdi)
{
	return bdi_congested(bdi, 1 << BDI_sync_congested);
}

static inline int bdi_write_congested(struct backing_dev_info *bdi)
{
	return bdi_congested(bdi, 1 << BDI_async_congested);
}

static inline int bdi_rw_congested(struct backing_dev_info *bdi)
{
	return bdi_congested(bdi, (1 << BDI_sync_congested) |
				  (1 << BDI_async_congested));
}

enum {
	BLK_RW_ASYNC	= 0,
	BLK_RW_SYNC	= 1,
};

void clear_bdi_congested(struct backing_dev_info *bdi, int sync);
void set_bdi_congested(struct backing_dev_info *bdi, int sync);
long congestion_wait(int sync, long timeout);


static inline bool bdi_cap_writeback_dirty(struct backing_dev_info *bdi)
{
	return !(bdi->capabilities & BDI_CAP_NO_WRITEBACK);
}

static inline bool bdi_cap_account_dirty(struct backing_dev_info *bdi)
{
	return !(bdi->capabilities & BDI_CAP_NO_ACCT_DIRTY);
}

static inline bool bdi_cap_account_writeback(struct backing_dev_info *bdi)
{
	/* Paranoia: BDI_CAP_NO_WRITEBACK implies BDI_CAP_NO_ACCT_WB */
	return !(bdi->capabilities & (BDI_CAP_NO_ACCT_WB |
				      BDI_CAP_NO_WRITEBACK));
}

static inline bool bdi_cap_swap_backed(struct backing_dev_info *bdi)
{
	return bdi->capabilities & BDI_CAP_SWAP_BACKED;
}

/* 只有default_backing_dev_info才有孵化其他flusher的能力 */
static inline bool bdi_cap_flush_forker(struct backing_dev_info *bdi)
{
	return bdi == &default_backing_dev_info;
}

static inline bool mapping_cap_writeback_dirty(struct address_space *mapping)
{
	return bdi_cap_writeback_dirty(mapping->backing_dev_info);
}

static inline bool mapping_cap_account_dirty(struct address_space *mapping)
{
	return bdi_cap_account_dirty(mapping->backing_dev_info);
}

static inline bool mapping_cap_swap_backed(struct address_space *mapping)
{
	return bdi_cap_swap_backed(mapping->backing_dev_info);
}

static inline int bdi_sched_wait(void *word)
{
	schedule();
	return 0;
}

static inline void blk_run_backing_dev(struct backing_dev_info *bdi,
				       struct page *page)
{
	if (bdi && bdi->unplug_io_fn)
		bdi->unplug_io_fn(bdi, page);
}

static inline void blk_run_address_space(struct address_space *mapping)
{
	if (mapping)
		blk_run_backing_dev(mapping->backing_dev_info, NULL);
}

#endif		/* _LINUX_BACKING_DEV_H */
