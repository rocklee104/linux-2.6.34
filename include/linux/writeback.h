/*
 * include/linux/writeback.h
 */
#ifndef WRITEBACK_H
#define WRITEBACK_H

#include <linux/sched.h>
#include <linux/fs.h>

struct backing_dev_info;

extern spinlock_t inode_lock;
/* 干净的inode要么放在inode_in_use,要么inode_unused */
extern struct list_head inode_in_use;
extern struct list_head inode_unused;

/*
 * fs/fs-writeback.c
 */
enum writeback_sync_modes {
	/* 用于非数据完整性操作 */
	WB_SYNC_NONE,	/* Don't wait on anything */
	/* 用于数据完整性操作 */
	WB_SYNC_ALL,	/* Wait on every mapping */
};

/*
 * A control structure which tells the writeback code what to do.  These are
 * always on the stack, and hence need no locking.  They are always initialised
 * in a manner such that unspecified fields are set to zero.
 */
struct writeback_control {
	/* 如果为非NULL,只回写这个队列 */
	struct backing_dev_info *bdi;	/* If !NULL, only write back this
					   queue */
	/* 如果为非空,只回写属于这个sb的inode */
	struct super_block *sb;		/* if !NULL, only write inodes from
					   this super_block */
	enum writeback_sync_modes sync_mode;
	/* 如果为非空,则只将比这个时间更早的inode回写到磁盘,这个域的优先级要高于nr_to_write */
	unsigned long *older_than_this;	/* If !NULL, only write back inodes
					   older than this */
	/* writeback_inodes_wb被调用的时间 */
	unsigned long wb_start;         /* Time writeback_inodes_wb was
					   called. This is needed to avoid
					   extra jobs and livelock */
	long nr_to_write;		/* Write this many pages, and decrement
					   this for each page written */
	/* 跳过的页面个数 */
	long pages_skipped;		/* Pages which were not written */

	/*
	 * For a_ops->writepages(): is start or end are non-zero then this is
	 * a hint that the filesystem need only write out the pages inside that
	 * byterange.  The byte at `end' is included in the writeout request.
	 */
	/* 回写范围的起始字节偏移(含) */
	loff_t range_start;
	/* 回写范围的结束字节偏移(含) */
	loff_t range_end;

	/* 如果为1,表示不要阻塞 */
	unsigned nonblocking:1;		/* Don't get stuck on request queues */
	/* 如果为1,表示遭遇拥塞 */
	unsigned encountered_congestion:1; /* An output: a queue is full */
	/*
	 * 如果为1,表示kupdate回写.即将超过特定驻留时间的脏页面回写磁盘.
	 * 当脏页在内存中驻留时间超过一个特定的阈值时,内核必须将超时的
	 * 脏页回写磁盘.确保脏页不会无限期驻留内存.
	 */
	unsigned for_kupdate:1;		/* A kupdate writeback */
	/* 如果为1,表示后台回写.后台回写用于释放内存目的。如果系统脏页总数降到脏门槛以下,结束回写 */
	unsigned for_background:1;	/* A background writeback */
	/* 如果为1,表示这次回写因为内存回收而调用 */
	unsigned for_reclaim:1;		/* Invoked from the page allocator */
	/* 如果为1,表示回写不受range_start和range_end范围的限制,即在地址空间内循环查找可以回写的页面 */
	unsigned range_cyclic:1;	/* range_start is cyclic */
	/* 如果为1,表示有更多的io等待派发 */
	unsigned more_io:1;		/* more io to be dispatched */
	/*
	 * write_cache_pages() won't update wbc->nr_to_write and
	 * mapping->writeback_index if no_nrwrite_index_update
	 * is set.  write_cache_pages() may write more than we
	 * requested and we want to make sure nr_to_write and
	 * writeback_index are updated in a consistent manner
	 * so we use a single control to update them
	 */
	/*
	 * 如果该域设置,write_cache_pages不会更新wbc->nr_to_write和mapping->writeback_index.
	 * 某些文件系统,如ext4,希望调用write_cache_pages但希望自行修改这些计数,所以需要设置此域
	 */
	unsigned no_nrwrite_index_update:1;
};

/*
 * fs/fs-writeback.c
 */	
struct bdi_writeback;
int inode_wait(void *);
void writeback_inodes_sb(struct super_block *);
int writeback_inodes_sb_if_idle(struct super_block *);
void sync_inodes_sb(struct super_block *);
void writeback_inodes_wbc(struct writeback_control *wbc);
long wb_do_writeback(struct bdi_writeback *wb, int force_wait);
void wakeup_flusher_threads(long nr_pages);

/* writeback.h requires fs.h; it, too, is not included from here. */
static inline void wait_on_inode(struct inode *inode)
{
	might_sleep();
	wait_on_bit(&inode->i_state, __I_NEW, inode_wait, TASK_UNINTERRUPTIBLE);
}
static inline void inode_sync_wait(struct inode *inode)
{
	might_sleep();
	wait_on_bit(&inode->i_state, __I_SYNC, inode_wait,
							TASK_UNINTERRUPTIBLE);
}


/*
 * mm/page-writeback.c
 */
void laptop_io_completion(void);
void laptop_sync_completion(void);
void throttle_vm_writeout(gfp_t gfp_mask);

/* These are exported to sysctl. */
extern int dirty_background_ratio;
extern unsigned long dirty_background_bytes;
extern int vm_dirty_ratio;
extern unsigned long vm_dirty_bytes;
extern unsigned int dirty_writeback_interval;
extern unsigned int dirty_expire_interval;
extern int vm_highmem_is_dirtyable;
extern int block_dump;
extern int laptop_mode;

extern unsigned long determine_dirtyable_memory(void);

extern int dirty_background_ratio_handler(struct ctl_table *table, int write,
		void __user *buffer, size_t *lenp,
		loff_t *ppos);
extern int dirty_background_bytes_handler(struct ctl_table *table, int write,
		void __user *buffer, size_t *lenp,
		loff_t *ppos);
extern int dirty_ratio_handler(struct ctl_table *table, int write,
		void __user *buffer, size_t *lenp,
		loff_t *ppos);
extern int dirty_bytes_handler(struct ctl_table *table, int write,
		void __user *buffer, size_t *lenp,
		loff_t *ppos);

struct ctl_table;
int dirty_writeback_centisecs_handler(struct ctl_table *, int,
				      void __user *, size_t *, loff_t *);

void get_dirty_limits(unsigned long *pbackground, unsigned long *pdirty,
		      unsigned long *pbdi_dirty, struct backing_dev_info *bdi);

void page_writeback_init(void);
void balance_dirty_pages_ratelimited_nr(struct address_space *mapping,
					unsigned long nr_pages_dirtied);

static inline void
balance_dirty_pages_ratelimited(struct address_space *mapping)
{
	balance_dirty_pages_ratelimited_nr(mapping, 1);
}

typedef int (*writepage_t)(struct page *page, struct writeback_control *wbc,
				void *data);

int generic_writepages(struct address_space *mapping,
		       struct writeback_control *wbc);
int write_cache_pages(struct address_space *mapping,
		      struct writeback_control *wbc, writepage_t writepage,
		      void *data);
int do_writepages(struct address_space *mapping, struct writeback_control *wbc);
void set_page_dirty_balance(struct page *page, int page_mkwrite);
void writeback_set_ratelimit(void);

/* pdflush.c */
extern int nr_pdflush_threads;	/* Global so it can be exported to sysctl
				   read-only. */


#endif		/* WRITEBACK_H */
