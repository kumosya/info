#ifndef _FS_EXT2_H_
#define _FS_EXT2_H_

#include "block.h"

#include <cstddef>
#include <stdint.h>
using namespace std;

namespace ext2 {

// Ext2 superblock (512 bytes)
struct Superblock {
    uint32_t s_inodes_count;      // Total inodes
    uint32_t s_blocks_count;      // Total blocks
    uint32_t s_r_blocks_count;    // Reserved blocks count
    uint32_t s_free_blocks_count; // Free blocks count
    uint32_t s_free_inodes_count; // Free inodes count
    uint32_t s_first_data_block;  // First data block
    uint32_t s_log_block_size;    // Block size = 1024 << s_log_block_size
    uint32_t s_log_frag_size;     // Fragment size = 1024 << s_log_frag_size
    uint32_t s_blocks_per_group;  // Blocks per group
    uint32_t s_frags_per_group;   // Fragments per group
    uint32_t s_inodes_per_group;  // Inodes per group
    uint32_t s_mtime;             // Mount time
    uint32_t s_wtime;             // Write time
    uint16_t s_mnt_count;         // Mount count
    uint16_t s_max_mnt_count;     // Max mount count
    uint16_t s_magic;             // Magic signature (0xEF53)
    uint16_t s_state;             // File system state
    uint16_t s_errors;            // Error handling
    uint16_t s_minor_rev_level;   // Minor revision level
    uint32_t s_lastcheck;         // Last check time
    uint32_t s_checkinterval;     // Check interval
    uint32_t s_creator_os;        // Creator OS
    uint32_t s_rev_level;         // Revision level
    uint16_t s_def_resuid;        // Default reserved UID
    uint16_t s_def_resgid;        // Default reserved GID

    // Extended features
    uint32_t s_first_ino;         // First non-reserved inode
    uint16_t s_inode_size;        // Inode size
    uint16_t s_block_group_nr;    // Block group number
    uint32_t s_feature_compat;    // Compatible feature set
    uint32_t s_feature_incompat;  // Incompatible feature set
    uint32_t s_feature_ro_compat; // Read-only compatible feature set
    uint8_t s_uuid[16];           // Volume ID
    char s_volume_name[16];       // Volume name
    char s_last_mounted[64];      // Last mounted directory
    uint32_t s_algo_bitmap;       // Compression algorithm bitmap

    // Additional fields (for 2.0+)
    uint8_t s_prealloc_blocks;     // Preallocate blocks for directories
    uint8_t s_prealloc_dir_blocks; // Preallocate blocks for directories
    uint16_t s_padding1;           // Padding
    uint8_t s_journal_uuid[16];    // Journal UUID
    uint32_t s_journal_inum;       // Journal inode
    uint32_t s_journal_dev;        // Journal device
    uint32_t s_last_orphan;        // Last orphan inode
    uint32_t s_hash_seed[4];       // Hash seed
    uint8_t s_def_hash_version;    // Default hash version
    uint8_t s_reserved_char_pad;   // Reserved padding
    uint16_t s_reserved_word_pad;  // Reserved padding
    uint32_t s_default_mount_opts; // Default mount options
    uint32_t s_first_meta_bg;      // First meta block group
    uint32_t s_reserved[190];      // Padding to 512 bytes
} __attribute__((packed));

// Ext2 block group descriptor (32 bytes)
struct GroupDescriptor {
    uint32_t bg_block_bitmap;      // Block bitmap block
    uint32_t bg_inode_bitmap;      // Inode bitmap block
    uint32_t bg_inode_table;       // Inode table block
    uint16_t bg_free_blocks_count; // Free blocks count
    uint16_t bg_free_inodes_count; // Free inodes count
    uint16_t bg_used_dirs_count;   // Used directories count
    uint16_t bg_pad;               // Padding
    uint32_t bg_reserved[3];       // Reserved
} __attribute__((packed));

// Ext2 inode (128 bytes)
struct Inode {
    uint16_t i_mode;        // File mode
    uint16_t i_uid;         // Low 16 bits of owner UID
    uint32_t i_size;        // Size in bytes
    uint32_t i_atime;       // Access time
    uint32_t i_ctime;       // Creation time
    uint32_t i_mtime;       // Modification time
    uint32_t i_dtime;       // Deletion time
    uint16_t i_gid;         // Low 16 bits of group ID
    uint16_t i_links_count; // Links count
    uint32_t i_blocks;      // Blocks count (512-byte units)
    uint32_t i_flags;       // File flags
    uint32_t i_osd1;        // OS dependent 1
    uint32_t i_block[15];   // Block pointers
    uint32_t i_generation;  // File version (for NFS)
    uint32_t i_file_acl;    // File ACL
    uint32_t i_dir_acl;     // Directory ACL
    uint32_t i_faddr;       // Fragment address
    uint8_t i_osd2[12];     // OS dependent 2
} __attribute__((packed));

// Ext2 directory entry (variable size)
struct DirEntry {
    uint32_t inode;    // Inode number
    uint16_t rec_len;  // Record length
    uint8_t name_len;  // Name length
    uint8_t file_type; // File type
    char name[256];    // File name (variable length)
} __attribute__((packed));

// File types
#define EXT2_FT_UNKNOWN 0  // Unknown file type
#define EXT2_FT_REG_FILE 1 // Regular file
#define EXT2_FT_DIR 2      // Directory
#define EXT2_FT_CHRDEV 3   // Character device
#define EXT2_FT_BLKDEV 4   // Block device
#define EXT2_FT_FIFO 5     // FIFO
#define EXT2_FT_SOCK 6     // Socket
#define EXT2_FT_SYMLINK 7  // Symbolic link

// Lseek whence values
#define SEEK_SET 0 // Seek from beginning
#define SEEK_CUR 1 // Seek from current position
#define SEEK_END 2 // Seek from end

// MBR Partition Entry
struct PartitionEntry {
    uint8_t boot_flag;      // Bootable flag (0x80 = bootable, 0x00 = non-bootable)
    uint8_t start_head;     // Start head
    uint8_t start_sector;   // Start sector (bits 0-5), cylinder (bits 6-7)
    uint8_t start_cylinder; // Start cylinder (bits 0-7)
    uint8_t system_id;      // System ID (0x83 = Linux)
    uint8_t end_head;       // End head
    uint8_t end_sector;     // End sector (bits 0-5), cylinder (bits 6-7)
    uint8_t end_cylinder;   // End cylinder (bits 0-7)
    uint32_t start_lba;     // Start sector (LBA)
    uint32_t sectors_count; // Number of sectors
} __attribute__((packed));

// MBR (Master Boot Record)
struct MBR {
    uint8_t bootstrap[446];       // Bootstrap code
    PartitionEntry partitions[4]; // Partition entries
    uint16_t signature;           // MBR signature (0xAA55)
} __attribute__((packed));

// Mounted file system
struct Mount {
    block::Device *device;       // Underlying block device
    uint64_t partition_offset;   // Partition offset in sectors
    Superblock superblock;       // Superblock
    GroupDescriptor *group_desc; // Group descriptors
    uint32_t block_size;         // Block size
    uint32_t blocks_per_group;   // Blocks per group
    uint32_t inodes_per_group;   // Inodes per group
    uint32_t groups_count;       // Number of block groups
    Mount *next;                 // Next mounted filesystem
};

// File descriptor
struct File {
    Mount *mount;       // Mount point
    uint32_t inode_num; // Inode number
    Inode inode;        // Inode data
    uint64_t offset;    // Current file offset
    uint32_t flags;     // Open flags
    File *next;         // Next file in list
};

// Function prototypes
Mount *mount(block::Device *device);
int umount(Mount *mount);
int read_inode(Mount *mount, uint32_t inode_num, Inode *inode);
int write_inode(Mount *mount, uint32_t inode_num, const Inode *inode);
int read_block(Mount *mount, uint32_t block_num, void *buf);
int write_block(Mount *mount, uint32_t block_num, const void *buf);
uint32_t alloc_block(Mount *mount);
int free_block(Mount *mount, uint32_t block_num);
uint32_t alloc_inode(Mount *mount);
int free_inode(Mount *mount, uint32_t inode_num);

// File operations
File *open(Mount *mount, uint32_t inode_num, uint32_t flags);
int close(File *file);
ssize_t read(File *file, void *buf, size_t count);
ssize_t write(File *file, const void *buf, size_t count);
int lseek(File *file, uint64_t offset, int whence);

// Helper function to get block number from inode
uint32_t get_block(Inode *inode, uint32_t block_idx, Mount *mount, bool allocate = false);

// Path resolution function
uint32_t resolve_path(Mount *mount, const char *path, uint32_t start_inode = 2);

} // namespace ext2

#endif // _FS_EXT2_H_
