#define _GNU_SOURCE
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#if __SIZEOF_POINTER__ != 8
#error 64-bit only
#endif

#define _PAGE_PRESENT  (1UL<<0)
#define _PAGE_RW       (1UL<<1)
#define _PAGE_USER     (1UL<<2)
#define _PAGE_ACCESSED (1UL<<5)
#define _PAGE_DIRTY    (1UL<<6)
#define _PAGE_PSE      (1UL<<7)
#define _PAGE_GLOBAL   (1UL<<8)
#define _PAGE_NX       (1UL<<63)
#define PTRS_PER_PGD   512
#define PTRS_PER_PUD   512
#define PTRS_PER_PMD   512
#define PTRS_PER_PTE   512
#define PGDIR_SHIFT    39
#define PUD_SHIFT      30
#define PMD_SHIFT      21
#define PAGE_SHIFT     12
#define PAGE_SIZE      (1UL<<PAGE_SHIFT)
#define MAX_PHYS_MASK  ((1UL<<46)-1)
#define HUGE_PAGE_SIZE (1UL<<PMD_SHIFT)
#define GIGA_PAGE_SIZE (1UL<<PUD_SHIFT)

#define KERNEL_PGD_BOUNDARY 272

#define WORD_SIZE       ((int)sizeof(unsigned long))
#define DEBUGFS_PATH    "/sys/kernel/debug/pagetables/"
#define VADDR_PATH      DEBUGFS_PATH "vaddr"
#define TARGET_PID_PATH DEBUGFS_PATH "pid"
#define FLAGS_MIN_BITS  PAGE_SHIFT

#define HIDE_KERNEL 1
#define HIDE_USER   0
#define STATS_ONLY  0
#define MAX_LEVEL   PTE_LEVEL

#if HIDE_KERNEL && HIDE_USER
#error Choose HIDE_KERNEL _or_ HIDE_USER.
#endif

enum pgtable_level {
	PGD_LEVEL,
	PUD_LEVEL,
	PMD_LEVEL,
	PTE_LEVEL,
	PHYS_4K_LEVEL,
	LEVEL_COUNT
};

enum flag {
	RW_FLAG,
	USER_FLAG,
	ACCESSED_FLAG,
	DIRTY_FLAG,
	GLOBAL_FLAG,
	NX_FLAG,
	FLAG_COUNT
};

static unsigned long flag_mapping[FLAG_COUNT] = {
	_PAGE_RW,
	_PAGE_USER,
	_PAGE_ACCESSED,
	_PAGE_DIRTY,
	_PAGE_GLOBAL,
	_PAGE_NX
};

static char *flag_name[FLAG_COUNT] = {
	"R/W",
	"User",
	"Accessed",
	"Dirty",
	"Global",
	"NX"
};

static char *level_name[LEVEL_COUNT] = {
	"PGD",
	"PUD",
	"PMD",
	"PTE",
	"4K Phys"
};

static char *level_path[LEVEL_COUNT] = {
	DEBUGFS_PATH "pgd",
	DEBUGFS_PATH "pud",
	DEBUGFS_PATH "pmd",
	DEBUGFS_PATH "pte",
	""
};

static int level_size[LEVEL_COUNT] = {
	PTRS_PER_PGD,
	PTRS_PER_PUD,
	PTRS_PER_PMD,
	PTRS_PER_PTE,
	0
};

static int level_shift[LEVEL_COUNT] = {
	PGDIR_SHIFT,
	PUD_SHIFT,
	PMD_SHIFT,
	PAGE_SHIFT,
	0
};

static unsigned long level_mask[LEVEL_COUNT] = {
	(PTRS_PER_PGD-1UL)<<PGDIR_SHIFT,
	(PTRS_PER_PUD-1UL)<<PUD_SHIFT,
	(PTRS_PER_PMD-1UL)<<PMD_SHIFT,
	(PTRS_PER_PTE-1UL)<<PAGE_SHIFT,
	((1UL<<PAGE_SHIFT)-1UL)
};

static char *human_suffix[] = {
	"",
	"KiB",
	"MiB",
	"GiB",
	"TiB"
};

static unsigned long vaddr;
static unsigned long page_count[LEVEL_COUNT], pte_count[FLAG_COUNT];
static unsigned long gigantic_page_count, huge_page_count;
static unsigned long invalid_entry_count;

static void set_target_pid(char *pid_str)
{
	FILE *file = fopen(TARGET_PID_PATH, "r+");
	unsigned int len = strlen(pid_str);

	if (!file) {
		fprintf(stderr,
			"pagetables: set_target_pid: error opening %s: %s\n",
			TARGET_PID_PATH, strerror(errno));
		exit(1);
	}

	if (fwrite(pid_str, 1, len, file) != len) {
		fprintf(stderr, "pagetables: set_target_pid: write error at %s\n",
			TARGET_PID_PATH);
		exit(1);
	}

	fclose(file);
}

static void print_human_bytes(unsigned long bytes)
{
	int suffix_ind = 0;
	double bytesf = (double)bytes;

	while (bytesf > 1024 && suffix_ind < 5) {
		suffix_ind++;
		bytesf /= 1024;
	}

	printf("%6.1f %s", bytesf, human_suffix[suffix_ind]);
}

static void print_bin(unsigned long val, int len)
{
	int i;
	char buf[65];

	buf[len] = '\0';
	for (i = 0; i < len; i++)
		buf[len-i-1] = val&(1UL<<i) ? '1' : '0';

	printf("%s", buf);
}

static void print_indent(int level)
{
	int i;

	for (i = 0; i < level; i++)
		printf("   ");
}

static void update_vaddr(enum pgtable_level level, int index)
{
	vaddr &= ~level_mask[level];
	vaddr |= ((unsigned long)index << level_shift[level]);
}

static void sync_vaddr(void)
{
	char buf[sizeof("0xdeadbeefdeadbeef")];
	FILE *file = fopen(VADDR_PATH, "r+");
	unsigned int len;

	if (!file) {
		fprintf(stderr,
			"pagetables: sync_vaddr: error opening %s: %s\n",
			VADDR_PATH, strerror(errno));
		exit(1);
	}

	len = snprintf(buf, sizeof(buf), "0x%lx", vaddr);
	if (len >= sizeof(buf)) {
		fprintf(stderr,
			"pagetables: sync_vaddr: attempted write %d, >= %lu.\n",
			len, sizeof(buf));
		exit(1);
	}

	if (fwrite(buf, 1, len, file) != len) {
		fprintf(stderr, "pagetables: write error at %s\n", VADDR_PATH);
		exit(1);
	}

	fclose(file);
}

static void update_sync_vaddr(enum pgtable_level level, int index)
{
	update_vaddr(level, index);
	sync_vaddr();
}

static void print_entry(int index, enum pgtable_level level, unsigned long entry)
{
	unsigned long phys_addr, flags, present;
	int count = level_size[level];
	unsigned long phys_addr_mask = (~(count - 1)) & MAX_PHYS_MASK;
	unsigned long flags_mask = ~phys_addr_mask;

	phys_addr = entry&phys_addr_mask;
	flags = entry&flags_mask;
	present = flags&_PAGE_PRESENT;

	print_indent(level);
	printf("%03d ", index < 0 ? -index : index);

	if (index < 0) {
		printf("       <INVALID>\n");
		return;
	}

	if (!present)
		printf("   <not present> ");
	else
		printf("%016lx ", phys_addr);

	print_bin(flags, FLAGS_MIN_BITS);

	if (flags&_PAGE_NX)
		printf(" NX");
	if (flags&_PAGE_PSE)
		printf(" H");

	printf("\n");
}

static void update_pte_counts(unsigned long entry)
{
	int i;

	for (i = 0; i < FLAG_COUNT; i++)
		if (entry&flag_mapping[i])
			pte_count[i]++;
}

static void update_stats(enum pgtable_level level, unsigned long entry, int valid)
{
	int huge = entry&_PAGE_PSE;

	if (!(entry&_PAGE_PRESENT))
		return;

	if (!valid) {
		invalid_entry_count++;
		return;
	}

	if (huge) {
		switch (level) {
		case PUD_LEVEL:
			gigantic_page_count++;

			break;
		case PMD_LEVEL:
			huge_page_count++;

			break;
		default:
			fprintf(stderr, "WARNING: Unrecognised huge page at %s level\n",
				level_name[level]);

			break;
		}
	} else {
		/* Each entry is a page of the next level. */
		page_count[level+1]++;
	}

	if (level == PTE_LEVEL || huge)
		update_pte_counts(entry);
}

static void print_pagetable(enum pgtable_level level)
{
	int i, start = 0;
	unsigned long entry;
	int count = level_size[level];

	FILE *file = fopen(level_path[level], "r");

	if (!file) {
		fprintf(stderr, "pagetables: error opening %s: %s\n",
			level_path[level], strerror(errno));
		exit(1);
	}

	if (level == PGD_LEVEL) {
		if (HIDE_KERNEL)
			count = KERNEL_PGD_BOUNDARY-1;
		else if (HIDE_USER && level == PGD_LEVEL) {
			/* fseek() seems not to work for sysfs files. */
			for (i = 0; i < KERNEL_PGD_BOUNDARY; i++)
				if (fread(&entry, 1, WORD_SIZE, file) != WORD_SIZE) {
					fprintf(stderr, "pagetables: error: seek error: %s\n",
						strerror(errno));
					exit(1);
				}

			start = KERNEL_PGD_BOUNDARY;
		}
	}

	for (i = start; i < count; i++) {
		int valid = 1;
		int present, huge;

		if (fread(&entry, 1, WORD_SIZE, file) != WORD_SIZE) {
			if (errno == EINVAL) {
				entry = 0;
				valid = 0;
			} else {
				fprintf(stderr,
					"pagetables: error: read error: %s\n",
					strerror(errno));
				exit(1);
			}
		}

		/* Skip empty entries. */
		if (valid && !entry)
			continue;

		present = entry&_PAGE_PRESENT;
		huge = entry&_PAGE_PSE;

		if (!STATS_ONLY)
			print_entry(valid ? i : -i, level, entry);

		update_stats(level, entry, valid);

		if (present && !huge && level < MAX_LEVEL) {
			update_sync_vaddr(level, i);

			print_pagetable(level + 1);
		}
	}

	fclose(file);
}

static void print_counts(void)
{
	int i;
	unsigned long count, ptes, total = 0, total_bytes = 0;

	puts("\n== Page Counts ==\n");

	/* Pedantry to the absolute maximum. */
	printf("PGD pages:\t       1 (   4.0 KiB)\n");
	total++;
	total_bytes += PAGE_SIZE;

	for (i = 1; i < LEVEL_COUNT; i++) {
		count = page_count[i];

		printf("%s pages:\t%8lu (", level_name[i], count);
		print_human_bytes(count * PAGE_SIZE);
		printf(")\n");

		total += count;
		total_bytes += count*PAGE_SIZE;
	}

	if (gigantic_page_count > 0) {
		printf("Giga Phys pages:%8lu (", gigantic_page_count);
		print_human_bytes(gigantic_page_count * GIGA_PAGE_SIZE);
		printf(")\n");

		total += gigantic_page_count;
		total_bytes += gigantic_page_count * GIGA_PAGE_SIZE;
	}

	if (huge_page_count > 0) {
		printf("Huge Phys pages:%8lu (", huge_page_count);
		print_human_bytes(huge_page_count * HUGE_PAGE_SIZE);
		printf(")\n");

		total += huge_page_count;
		total_bytes += huge_page_count * HUGE_PAGE_SIZE;
	}

	printf("TOTAL:\t\t%8lu ", total);

	printf("(");
	print_human_bytes(total_bytes);
	printf(")\n\n");

	if (invalid_entry_count > 0)
		printf("!!! Invalid (unreadable) entries: %lu\n\n",
			invalid_entry_count);

	ptes = page_count[PHYS_4K_LEVEL] + huge_page_count + gigantic_page_count;
	for (i = 0; i < FLAG_COUNT; i++) {
		count = pte_count[i];

		if (count == 0)
			continue;

		printf("%s PTEs:\t%8lu/%lu (", flag_name[i], pte_count[i], ptes);
		print_human_bytes((unsigned long)pte_count[i] * PAGE_SIZE);
		printf(")\n");
	}
}

int main(int argc, char *argv[])
{
	if (argc > 1)
		set_target_pid(argv[1]);
	else
		set_target_pid("0");

	print_pagetable(PGD_LEVEL);
	print_counts();

	return EXIT_SUCCESS;
}
