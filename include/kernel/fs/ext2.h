#ifndef INFO_KERNEL_FS_EXT2_H_
#define INFO_KERNEL_FS_EXT2_H_

#include <cstddef>
#include <stdint.h>

#include "kernel/block.h"
#include "kernel/vfs.h"

#define EXT2_SUPER_MAGIC 0xEF53
#define EXT2_ROOT_INO 2
#define EXT2_GOOD_OLD_FIRST_INO 11
#define EXT2_GOOD_OLD_INODE_SIZE 128
#define EXT2_NDIR_BLOCKS 12
#define EXT2_IND_BLOCK EXT2_NDIR_BLOCKS
#define EXT2_DIND_BLOCK (EXT2_IND_BLOCK + 1)
#define EXT2_TIND_BLOCK (EXT2_DIND_BLOCK + 1)
#define EXT2_N_BLOCKS (EXT2_TIND_BLOCK + 1)

#define EXT2_S_IFDIR 0x4000
#define EXT2_S_IFREG 0x8000
#define EXT2_S_IFLNK 0xA000

#define PARTITION_TYPE_LINUX 0x83

struct MBRPartitionEntry {
    std::uint8_t boot_indicator;
    std::uint8_t starting_chs[3];
    std::uint8_t partition_type;
    std::uint8_t ending_chs[3];
    std::uint32_t starting_lba;
    std::uint32_t total_sectors;
} __attribute__((packed));

struct MBR {
    std::uint8_t boot_code[446];
    MBRPartitionEntry partitions[4];
    std::uint16_t signature;
} __attribute__((packed));

struct Ext2SuperBlock {
    std::uint32_t s_inodes_count;
    std::uint32_t s_blocks_count;
    std::uint32_t s_r_blocks_count;
    std::uint32_t s_free_blocks_count;
    std::uint32_t s_free_inodes_count;
    std::uint32_t s_first_data_block;
    std::uint32_t s_log_block_size;
    std::uint32_t s_log_frag_size;
    std::uint32_t s_blocks_per_group;
    std::uint32_t s_frags_per_group;
    std::uint32_t s_inodes_per_group;
    std::uint32_t s_mtime;
    std::uint32_t s_wtime;
    std::uint16_t s_mnt_count;
    std::uint16_t s_max_mnt_count;
    std::uint16_t s_magic;
    std::uint16_t s_state;
    std::uint16_t s_errors;
    std::uint16_t s_minor_rev_level;
    std::uint32_t s_lastcheck;
    std::uint32_t s_checkinterval;
    std::uint32_t s_creator_os;
    std::uint32_t s_rev_level;
    std::uint16_t s_def_resuid;
    std::uint16_t s_def_resgid;
    std::uint32_t s_first_ino;
    std::uint16_t s_inode_size;
    std::uint16_t s_block_group_nr;
    std::uint32_t s_feature_compat;
    std::uint32_t s_feature_incompat;
    std::uint32_t s_feature_ro_compat;
    std::uint8_t s_uuid[16];
    std::uint8_t s_volume_name[16];
    char s_last_mounted[64];
    std::uint32_t s_algorithm_usage_bitmap;
    std::uint8_t s_prealloc_blocks;
    std::uint8_t s_prealloc_dir_blocks;
    std::uint16_t s_padding1;
    std::uint32_t s_reserved[204];
} __attribute__((packed));

struct Ext2GroupDesc {
    std::uint32_t bg_block_bitmap;
    std::uint32_t bg_inode_bitmap;
    std::uint32_t bg_inode_table;
    std::uint16_t bg_free_blocks_count;
    std::uint16_t bg_free_inodes_count;
    std::uint16_t bg_used_dirs_count;
    std::uint16_t bg_pad;
    std::uint32_t bg_reserved[3];
} __attribute__((packed));

struct Ext2Inode {
    std::uint16_t i_mode;
    std::uint16_t i_uid;
    std::uint32_t i_size;
    std::uint32_t i_atime;
    std::uint32_t i_ctime;
    std::uint32_t i_mtime;
    std::uint32_t i_dtime;
    std::uint16_t i_gid;
    std::uint16_t i_links_count;
    std::uint32_t i_blocks;
    std::uint32_t i_flags;
    std::uint32_t i_osd1;
    std::uint32_t i_block[EXT2_N_BLOCKS];
    std::uint32_t i_generation;
    std::uint32_t i_file_acl;
    std::uint32_t i_dir_acl;
    std::uint32_t i_faddr;
    std::uint8_t i_osd2[12];
} __attribute__((packed));

struct Ext2DirEntry {
    std::uint32_t inode;
    std::uint16_t rec_len;
    std::uint8_t name_len;
    std::uint8_t file_type;
    char name[256];
} __attribute__((packed));

struct Ext2FsInfo {
    block::BlockDevice *device;
    Ext2SuperBlock super;
    std::uint32_t block_size;
    std::uint32_t group_count;
    std::uint32_t inode_size;
    std::uint32_t inodes_per_group;
    std::uint32_t blocks_per_group;
};

struct Ext2FileInfo {
    std::uint32_t inode_num;
    Ext2Inode inode;
    std::uint32_t block_pointers[EXT2_N_BLOCKS];
};

namespace ext2 {

int ReadSuperBlock(block::BlockDevice *dev, Ext2SuperBlock *sb,
                   std::uint32_t ext2_lba);
int WriteSuperBlock(block::BlockDevice *dev, Ext2SuperBlock *sb);
int ReadGroupDesc(block::BlockDevice *dev, Ext2SuperBlock *sb,
                  std::uint32_t group, Ext2GroupDesc *gd,
                  std::uint32_t ext2_lba);
int WriteGroupDesc(block::BlockDevice *dev, Ext2SuperBlock *sb,
                   std::uint32_t group, Ext2GroupDesc *gd,
                   std::uint32_t ext2_lba);
int ReadInode(block::BlockDevice *dev, Ext2SuperBlock *sb,
              std::uint32_t inode_num, Ext2Inode *inode,
              std::uint32_t ext2_lba);
int WriteInode(block::BlockDevice *dev, Ext2SuperBlock *sb,
               std::uint32_t inode_num, Ext2Inode *inode,
               std::uint32_t ext2_lba);
std::uint32_t GetBlockNum(Ext2Inode *inode, std::uint32_t block,
                          Ext2SuperBlock *sb, block::BlockDevice *dev,
                          std::uint32_t ext2_lba);
int SetBlockNum(Ext2Inode *inode, std::uint32_t block, std::uint32_t num,
                Ext2SuperBlock *sb, block::BlockDevice *dev,
                std::uint32_t ext2_lba);
int ReadBlock(block::BlockDevice *dev, Ext2SuperBlock *sb,
              std::uint32_t block_num, void *buf, std::uint32_t ext2_lba);
int WriteBlock(block::BlockDevice *dev, Ext2SuperBlock *sb,
               std::uint32_t block_num, const void *buf,
               std::uint32_t ext2_lba);
std::uint32_t AllocBlock(block::BlockDevice *dev, Ext2SuperBlock *sb,
                         std::uint32_t ext2_lba);
void FreeBlock(block::BlockDevice *dev, Ext2SuperBlock *sb, std::uint32_t block,
               std::uint32_t ext2_lba);
std::uint32_t AllocInode(block::BlockDevice *dev, Ext2SuperBlock *sb,
                         std::uint32_t ext2_lba);
void FreeInode(block::BlockDevice *dev, Ext2SuperBlock *sb, std::uint32_t inode,
               std::uint32_t ext2_lba);
int FindEntry(block::BlockDevice *dev, Ext2SuperBlock *sb, Ext2Inode *dir_inode,
              const char *name, Ext2DirEntry *entry, std::uint32_t ext2_lba);
int AddEntry(block::BlockDevice *dev, Ext2SuperBlock *sb, Ext2Inode *dir_inode,
             std::uint32_t inode_num, const char *name, std::uint8_t file_type,
             std::uint32_t ext2_lba);
int RemoveEntry(block::BlockDevice *dev, Ext2SuperBlock *sb,
                Ext2Inode *dir_inode, const char *name, std::uint32_t ext2_lba);
int TruncateInode(block::BlockDevice *dev, Ext2SuperBlock *sb, Ext2Inode *inode,
                  std::uint32_t ext2_lba);
std::uint32_t Inode2Block(block::BlockDevice *dev, Ext2SuperBlock *sb,
                          std::uint32_t inode_num, std::uint32_t ext2_lba);
int LookupPath(block::BlockDevice *dev, Ext2SuperBlock *sb, const char *path,
               std::uint32_t *inode_num, std::uint32_t ext2_lba);
vfs::DirEntry *Readdir(block::BlockDevice *dev, Ext2SuperBlock *sb,
                       std::uint32_t dir_inode_num, std::uint32_t index,
                       std::uint32_t ext2_lba);
ssize_t ReadFile(block::BlockDevice *dev, Ext2SuperBlock *sb, Ext2Inode *inode,
                 void *buf, size_t count, std::uint64_t offset,
                 std::uint32_t ext2_lba);
ssize_t WriteFile(block::BlockDevice *dev, Ext2SuperBlock *sb, Ext2Inode *inode,
                  const void *buf, size_t count, std::uint64_t offset,
                  std::uint32_t ext2_lba);
int CreateFile(block::BlockDevice *dev, Ext2SuperBlock *sb, const char *path,
               std::uint16_t mode, std::uint32_t ext2_lba);
int DeleteFile(block::BlockDevice *dev, Ext2SuperBlock *sb, const char *path,
               std::uint32_t ext2_lba);
std::int64_t Lseek(block::BlockDevice *dev, Ext2SuperBlock *sb,
                   Ext2Inode *inode, std::int64_t offset, int whence);
int Truncate(block::BlockDevice *dev, Ext2SuperBlock *sb,
             std::uint32_t inode_num, std::uint64_t new_size,
             std::uint32_t ext2_lba);
int GetFileSize(block::BlockDevice *dev, Ext2SuperBlock *sb, const char *path,
                std::uint64_t *size, std::uint32_t ext2_lba);
std::uint32_t FindExt2Partition(block::BlockDevice *dev);

}  // namespace ext2

#endif  // INFO_KERNEL_FS_EXT2_H_
