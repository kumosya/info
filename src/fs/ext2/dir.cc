#include "fs/ext2.h"

#include <cstdint>
#include <cstdlib>
#include <cstring>

#include "block.h"
#include "tty.h"
#include "vfs.h"

namespace ext2 {
// Helper function to find a directory entry by name
static std::uint32_t FindDirEntry(MountFs *mount, std::uint32_t dir_inode_num, const char *name) {
    if (!mount || !name || dir_inode_num == 0) {
        return 0;
    }

    // Read directory inode
    Inode dir_inode;
    if (ReadInode(mount, dir_inode_num, &dir_inode) != 0) {
        return 0;
    }

    // Check if it's a directory
    if (!(dir_inode.i_mode & 0x4000)) {  // S_IFDIR
        return 0;
    }

    // Allocate buffer for directory blocks
    uint8_t *buffer = new uint8_t[mount->block_size];
    if (!buffer) {
        return 0;
    }

    uint32_t found_inode    = 0;
    uint64_t current_offset = 0;

    // Iterate through directory blocks
    while (current_offset < dir_inode.i_size) {
        // Calculate block number and offset within block
        uint32_t block_idx    = current_offset / mount->block_size;
        uint32_t block_offset = current_offset % mount->block_size;

        // Get block number from inode
        uint32_t block_num = GetBlock(&dir_inode, block_idx, mount);
        if (block_num == 0) {
            break;
        }

        // Read block
        if (ReadBlock(mount, block_num, buffer) != 0) {
            break;
        }

        // Iterate through directory entries in this block
        uint32_t entry_offset = block_offset;
        while (entry_offset < mount->block_size && current_offset < dir_inode.i_size) {
            // Check if we have at least enough space for the minimum directory entry
            if (entry_offset + 8 > mount->block_size) {
                break;
            }

            // Get pointer to directory entry
            DirEntry *entry = reinterpret_cast<DirEntry *>(buffer + entry_offset);

            // Check for invalid entry
            if (entry->rec_len == 0) {
                break;
            }

            // Check if this entry is beyond the block boundary
            if (entry_offset + entry->rec_len > mount->block_size) {
                break;
            }

            // Skip deleted entries (inode == 0)
            if (entry->inode != 0 && entry->name_len > 0) {
                // Compare names properly
                if (strncmp(entry->name, name, entry->name_len) == 0 &&
                    name[entry->name_len] == '\0') {
                    found_inode = entry->inode;
                    goto cleanup;
                }
            }

            // Move to next entry
            entry_offset += entry->rec_len;
            current_offset += entry->rec_len;
        }

        // Move to next block
        current_offset = (block_idx + 1) * mount->block_size;
    }

cleanup:
    delete[] buffer;
    return found_inode;
}

// Resolve path to inode number
std::uint32_t ResolvePath(MountFs *mount, const char *path, std::uint32_t start_inode) {
    if (!mount || !path) {
        return 0;
    }

    // Start with the root inode or the specified start inode
    std::uint32_t current_inode = start_inode;

    // Skip leading slashes
    while (*path == '/') {
        path++;
    }

    // If path is empty, return the current inode
    if (*path == '\0') {
        return current_inode;
    }

    // Allocate buffer for path component
    char component[256];

    while (*path != '\0') {
        // Extract next path component
        uint32_t i = 0;
        while (*path != '/' && *path != '\0' && i < sizeof(component) - 1) {
            component[i++] = *path++;
        }
        component[i] = '\0';

        // Skip any trailing slashes
        while (*path == '/') {
            path++;
        }

        // Handle special cases
        if (strcmp(component, ".") == 0) {
            // Current directory, do nothing
            continue;
        } else if (strcmp(component, "..") == 0) {
            // Parent directory
            // For root directory, parent is itself
            if (current_inode != 2) {
                // Get parent directory inode by looking up ".." entry
            current_inode = FindDirEntry(mount, current_inode, "..");
                if (current_inode == 0) {
                    return 0;
                }
            }
        } else {
            // Regular directory entry
            current_inode = FindDirEntry(mount, current_inode, component);
            if (current_inode == 0) {
                return 0;
            }
        }
    }

    return current_inode;
}
}