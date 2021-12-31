###############################################################################################################################
ECE 5414G: Advanced Linux Kernel Programming
Efficient Page Table Migration on Heterogeneous Memory Systems 
###############################################################################################################################

###############################################################################################################################
SETUP
###############################################################################################################################

1. .config contains the kernel parameters used to emulate NUMA and support Persistent memory on Linux Kernel v5.12
2. The <Pmem_Setup.sh> contains the script we have used to set up the persistent memory using the "ndctl" and "daxctl" 
   framework.
3. <grub> has the NUMA configuration and reserves 8Gb of 16Gb (Total) to use later as Persistent memory. 
4. <Heterogenous_Memory.patch> has the kernel level modifications to support PTE migration.
5. The <Source> folder has a copy of the linux kernel files. 


###############################################################################################################################
IMAGES
###############################################################################################################################
1. lsmem: Output for the lsmem command showing the physical address ranges for each NUMA node and persistent memory.
2. Numactl_With_Pmem_Script: First section shows the current emulated NUMA architecture with its CPUs and memory. The Script 
   is then run which setups up and binds Persistent Memory to NUMA node 1. Numactl -H is run again to validate the binding.
3. numastat_before_migration: Displays the memory status for each NUMA node. We can see the memory is allocated on node 1 
   originally.
4. numastat_after_migration: Displays the memory status for each node. We can see the memroy allocated on node 0 after the 
   migratepages call has completed.
5. Page_allocator: Output for the general_alloc application used for validation by allocating 1Gb physical pages along with 
   the PID of the process.
6. Page_Map_before_migration: Output of the page_map application which displays pysical pages for a given PID before the 
   migratepages is executed. 4 layers of page tables are represented in a tab seperated format.
7. Page_Map_after_migration: Output of the page_map application after the migration application has been executed. 
   Note the constant PGD,PUD,PMD entries and difference in the PTE addresses.
8. PageMigration_Results: Cumulative results showing the memory status before and after the page migration is executed.
9. hashjoin_No_Migration: Results of the hashjoin operation running without page migration on an unmodified kernel.
10.hashjoin_Migration: Results of hashjoin running with all pages being migrated from NUMA 1 to NUMA 0 on the modified kernel.

###############################################################################################################################
VALIDATION AND EVALUATION
###############################################################################################################################

1. Alloc_Memory directory contains a userspace validation application that allocates physical pages in multiples of 1Gb to 
   test the Page Migration between nodes. 
2. Pagemapper is utility provided by Lorenzo Stokes that displays the details of the Page Table highlighting it 4 levels and 
   size taken by each entry.
