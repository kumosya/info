#ifndef _FS_EXT2_H_
#define _FS_EXT2_H_

#include <stdint.h>

#include <cstddef>

#include "block.h"

namespace ext2 {

// Ext2 superblock (512 bytes)
struct Superblock {
    std::uint32_t s_inodes_count;       // Total inodes
    std::uint32_t s_blocks_count;       // Total blocks
    std::uint32_t s_r_blocks_count;     // Reserved blocks count
    std::uint32_t s_free_blocks_count;  // Free blocks count
    std::uint32_t s_free_inodes_count;  // Free inodes count
    std::uint32_t s_first_data_block;   // First data block
    std::uint32_t s_log_block_size;     // Block size = 1024 << s_log_block_size
    std::uint32_t s_log_frag_size;      // Fragment size = 1024 << s_log_frag_size
    std::uint32_t s_blocks_per_group;   // Blocks per group
    std::uint32_t s_frags_per_group;    // Fragments per group
    std::uint32_t s_inodes_per_group;   // Inodes per group
    std::uint32_t s_mtime;              // Mount time
    std::uint32_t s_wtime;              // Write time
    std::uint16_t s_mnt_count;          // Mount count
    std::uint16_t s_max_mnt_count;      // Max mount count
    std::uint16_t s_magic;              // Magic signature (0xEF53)
    std::uint16_t s_state;              // File system state
    std::uint16_t s_errors;             // Error handling behavior
    std::uint16_t s_minor_rev_level;    // Minor revision level
    std::uint32_t s_lastcheck;          // Last check time
    std::uint32_t s_checkinterval;      // Check interval
    std::uint32_t s_creator_os;         // Creator OS
    std::uint32_t s_rev_level;          // Revision level
    std::uint16_t s_def_resuid;         // Default reserved UID
    std::uint16_t s_def_resgid;         // Default reserved GID

    // Extended features
    std::uint32_t s_first_ino;          // First non-reserved inode
    std::uint16_t s_inode_size;         // Inode size
    std::uint16_t s_block_group_nr;     // Block group number
    std::uint32_t s_feature_compat;     // Compatible feature set
    std::uint32_t s_feature_incompat;   // Incompatible feature set
    std::uint32_t s_feature_ro_compat;  // Read-only compatible feature set
    std::uint8_t s_uuid[16];            // Volume ID
    char s_volume_name[16];             // Volume name
    char s_last_mounted[64];            // Last mounted directory
    std::uint32_t s_algo_bitmap;        // Compression algorithm bitmap

    // Additional fields (for 2.0+)
    std::uint8_t s_prealloc_blocks;      // Preallocate blocks for directories
    std::uint8_t s_prealloc_dir_blocks;  // Preallocate blocks for directories
    std::uint16_t s_padding1;            // Padding
    std::uint8_t s_journal_uuid[16];     // Journal UUID
    std::uint32_t s_journal_inum;        // Journal inode
    std::uint32_t s_journal_dev;         // Journal device
    std::uint32_t s_last_orphan;         // Last orphan inode
    std::uint32_t s_hash_seed[4];        // Hash seed
    std::uint8_t s_def_hash_version;     // Default hash version
    std::uint8_t s_reserved_char_pad;    // Reserved padding
    std::uint16_t s_reserved_word_pad;   // Reserved padding
    std::uint32_t s_default_mount_opts;  // Default mount options
    std::uint32_t s_first_meta_bg;       // First meta block group
    std::uint32_t s_reserved[190];       // Padding to 512 bytes
} __attribute__((packed));

// Ext2 block group descriptor (32 bytes)
struct GroupDescriptor {
    std::uint32_t bg_block_bitmap;       // Block bitmap block
    std::uint32_t bg_inode_bitmap;       // Inode bitmap block
    std::uint32_t bg_inode_table;        // Inode table block
    std::uint16_t bg_free_blocks_count;  // Free blocks count
    std::uint16_t bg_free_inodes_count;  // Free inodes count
    std::uint16_t bg_used_dirs_count;    // Used directories count
    std::uint16_t bg_pad;                // Padding
    std::uint32_t bg_reserved[3];        // Reserved
} __attribute__((packed));

// Ext2 inode (128 bytes)
struct Inode {
    std::uint16_t i_mode;         // File mode
    std::uint16_t i_uid;          // Low 16 bits of owner UID
    std::uint32_t i_size;         // Size in bytes
    std::uint32_t i_atime;        // Access time
    std::uint32_t i_ctime;        // Creation time
    std::uint32_t i_mtime;        // Modification time
    std::uint32_t i_dtime;        // Deletion time
    std::uint16_t i_gid;          // Low 16 bits of group ID
    std::uint16_t i_links_count;  // Links count
    std::uint32_t i_blocks;       // Blocks count (512-byte units)
    std::uint32_t i_flags;        // File flags
    std::uint32_t i_osd1;         // OS dependent 1
    std::uint32_t i_block[15];    // Block pointers
    std::uint32_t i_generation;   // File version (for NFS)
    std::uint32_t i_file_acl;     // File ACL
    std::uint32_t i_dir_acl;      // Directory ACL
    std::uint32_t i_faddr;        // Fragment address
    std::uint8_t i_osd2[12];      // OS dependent 2
} __attribute__((packed));

// Ext2 directory entry (variable size)
struct DirEntry {
    std::uint32_t inode;     // Inode number
    std::uint16_t rec_len;   // Record length
    std::uint8_t name_len;   // Name length
    std::uint8_t file_type;  // File type
    char name[256];          // File name (variable length)
} __attribute__((packed));

// File types
#define EXT2_FT_UNKNOWN 0   // Unknown file type
#define EXT2_FT_REG_FILE 1  // Regular file
#define EXT2_FT_DIR 2       // Directory
#define EXT2_FT_CHRDEV 3    // Character device
#define EXT2_FT_BLKDEV 4    // Block device
#define EXT2_FT_FIFO 5      // FIFO
#define EXT2_FT_SOCK 6      // Socket
#define EXT2_FT_SYMLINK 7   // Symbolic link

// Lseek whence values
#define SEEK_SET 0  // Seek from beginning
#define SEEK_CUR 1  // Seek from current position
#define SEEK_END 2  // Seek from end

// MBR Partition Entry
struct PartitionEntry {
    std::uint8_t boot_flag;       // Bootable flag (0x80 = bootable, 0x00 = non-bootable)
    std::uint8_t start_head;      // Start head
    std::uint8_t start_sector;    // Start sector (bits 0-5), cylinder (bits 6-7)
    std::uint8_t start_cylinder;  // Start cylinder (bits 0-7)
    std::uint8_t system_id;       // System ID (0x83 = Linux)
    std::uint8_t end_head;        // End head
    std::uint8_t end_sector;      // End sector (bits 0-5), cylinder (bits 6-7)
    std::uint8_t end_cylinder;    // End cylinder (bits 0-7)
    std::uint32_t start_lba;      // Start sector (LBA)
    std::uint32_t sectors_count;  // Number of sectors
} __attribute__((packed));

// MBR (Master Boot Record)
struct MBR {
    std::uint8_t bootstrap[446];   // Bootstrap code
    PartitionEntry partitions[4];  // Partition entries
    std::uint16_t signature;       // MBR signature (0xAA55)
} __attribute__((packed));

// Mounted file system
struct MountFs {
    block::Device *device;           // Underlying block device
    std::uint64_t partition_offset;  // Partition offset in sectors
    Superblock superblock;           // Superblock
    GroupDescriptor *group_desc;     // Group descriptors
    std::uint32_t block_size;        // Block size
    std::uint32_t blocks_per_group;  // Blocks per group
    std::uint32_t inodes_per_group;  // Inodes per group
    std::uint32_t groups_count;      // Number of block groups
    MountFs *next;                     // Next mounted filesystem
};

// File descriptor
struct File {
    MountFs *mount;             // Mount point
    std::uint32_t inode_num;  // Inode number
    Inode inode;              // Inode data
    std::uint64_t offset;     // Current file offset
    std::uint32_t flags;      // Open flags
    File *next;               // Next file in list
};

// Function prototypes
MountFs *Mount(block::Device *device);
int Umount(MountFs *mount);
int ReadInode(MountFs *mount, std::uint32_t inode_num, Inode *inode);
int WriteInode(MountFs *mount, std::uint32_t inode_num, const Inode *inode);
int ReadBlock(MountFs *mount, std::uint32_t block_num, void *buf);
int WriteBlock(MountFs *mount, std::uint32_t block_num, const void *buf);
std::uint32_t AllocBlock(MountFs *mount);
int FreeBlock(MountFs *mount, std::uint32_t block_num);
std::uint32_t AllocInode(MountFs *mount);
int FreeInode(MountFs *mount, std::uint32_t inode_num);

// File operations
File *Open(MountFs *mount, std::uint32_t inode_num, std::uint32_t flags);
int Close(File *file);
ssize_t Read(File *file, void *buf, size_t count);
ssize_t Write(File *file, const void *buf, size_t count);
int Lseek(File *file, std::uint64_t offset, int whence);

// Helper function to get block number from inode
std::uint32_t GetBlock(Inode *inode, std::uint32_t block_idx, MountFs *mount, bool allocate = false);

// Path resolution function
std::uint32_t ResolvePath(MountFs *mount, const char *path, std::uint32_t start_inode = 2);

}  // namespace ext2

#endif  // _FS_EXT2_H_
