#include "fs/ext2.h"

#include <cstdint>
#include <cstdlib>
#include <cstring>

#include "block.h"
#include "tty.h"
#include "vfs.h"

namespace ext2 {
    
MountFs *Mount(block::Device *device) {
    if (!device) {
        return nullptr;
    }

    // Allocate mount structure
    MountFs *mount = new MountFs();
    if (!mount) {
        return nullptr;
    }

    mount->device = device;
    mount->next   = nullptr;  // No longer managed by EXT2

    // Default to no partition offset (whole device is EXT2)
    mount->partition_offset = 0;

    // Read MBR to check for partition table
    char mbr_buf[512];
    if (block::Read(device, 0, 1, mbr_buf) == 0) {
        // Check MBR signature directly from buffer
        uint16_t signature = ((std::uint8_t)mbr_buf[510]) | ((std::uint8_t)mbr_buf[511] << 8);

        if (signature == 0xAA55) {
            // Look for Linux EXT2 partitions (system_id = 0x83)
            for (int i = 0; i < 4; i++) {
                int offset = 446 + i * sizeof(PartitionEntry);

                // Parse partition entry directly from buffer
                uint8_t system_id  = (std::uint8_t)mbr_buf[offset + 4];
                uint32_t start_lba = ((std::uint8_t)mbr_buf[offset + 8]) |
                                     ((std::uint8_t)mbr_buf[offset + 9] << 8) |
                                     ((std::uint8_t)mbr_buf[offset + 10] << 16) |
                                     ((std::uint8_t)mbr_buf[offset + 11] << 24);
                uint32_t sectors_count = ((std::uint8_t)mbr_buf[offset + 12]) |
                                         ((std::uint8_t)mbr_buf[offset + 13] << 8) |
                                         ((std::uint8_t)mbr_buf[offset + 14] << 16) |
                                         ((std::uint8_t)mbr_buf[offset + 15] << 24);

                if (system_id == 0x83 && sectors_count > 0) {
                    // Set partition offset with correct value
                    mount->partition_offset = start_lba;
                    break;
                }
            }
        }
    }

    // Try reading superblock from common locations
    char superblock_buf[512];
    int superblock_found = 0;

    // Try standard superblock locations first
    std::uint64_t common_sectors[] = {
        mount->partition_offset + 2,      // Standard superblock (block 1)
        mount->partition_offset + 32768,  // Alternate superblock for large disks
        mount->partition_offset + 1,      // Just in case it's at sector 1
        mount->partition_offset + 0       // Very beginning of partition
    };

    for (int i = 0; i < sizeof(common_sectors) / sizeof(common_sectors[0]); i++) {
        std::uint64_t sector = common_sectors[i];

        if (block::Read(device, sector, 1, superblock_buf) == 0) {
            mount->superblock = *(Superblock *)superblock_buf;

            if (mount->superblock.s_magic == 0xEF53) {
                superblock_found = 1;
                break;
            }
        }
    }

    // If no superblock found yet, do a linear search through the first 1000 sectors
    if (!superblock_found) {
        for (std::uint64_t i = 0; i < 1000; i++) {
            std::uint64_t sector = mount->partition_offset + i;

            if (block::Read(device, sector, 1, superblock_buf) == 0) {
                // Check magic number directly from buffer
                std::uint16_t magic =
                    ((std::uint8_t)superblock_buf[56]) | ((std::uint8_t)superblock_buf[57] << 8);

                if (magic == 0xEF53) {
                    mount->superblock = *(Superblock *)superblock_buf;
                    superblock_found  = 1;
                    break;
                }
            }
        }
    }

    // Check if we found a valid superblock
    if (!superblock_found) {
        delete mount;
        return nullptr;
    }

    // Calculate block size
    mount->block_size       = 1024 << mount->superblock.s_log_block_size;
    mount->blocks_per_group = mount->superblock.s_blocks_per_group;
    mount->inodes_per_group = mount->superblock.s_inodes_per_group;

    // Calculate number of block groups
    mount->groups_count =
        (mount->superblock.s_blocks_count + mount->blocks_per_group - 1) / mount->blocks_per_group;

    // Allocate group descriptors
    mount->group_desc = new GroupDescriptor[mount->groups_count];
    if (!mount->group_desc) {
        delete mount;
        return nullptr;
    }

    // Read group descriptors
    std::uint32_t group_desc_block  = mount->superblock.s_first_data_block + 1;
    std::uint32_t group_desc_size   = mount->groups_count * sizeof(GroupDescriptor);
    std::uint32_t group_desc_blocks = (group_desc_size + mount->block_size - 1) / mount->block_size;

    char *group_desc_buf = new char[group_desc_blocks * mount->block_size];
    if (!group_desc_buf) {
        delete[] mount->group_desc;
        delete mount;
        return nullptr;
    }

    // Read all group descriptor blocks
    for (std::uint32_t i = 0; i < group_desc_blocks; i++) {
        if (ReadBlock(mount, group_desc_block + i, group_desc_buf + i * mount->block_size) != 0) {
            delete[] group_desc_buf;
            delete[] mount->group_desc;
            delete mount;
            return nullptr;
        }
    }

    // Copy to group descriptor array
    for (std::uint32_t i = 0; i < mount->groups_count; i++) {
        mount->group_desc[i] = *(GroupDescriptor *)(group_desc_buf + i * sizeof(GroupDescriptor));
    }

    delete[] group_desc_buf;

    return mount;
}

int Umount(MountFs *mount) {
    if (!mount) {
        return -1;
    }

    // Free resources
    delete[] mount->group_desc;
    delete mount;

    return 0;
}
}