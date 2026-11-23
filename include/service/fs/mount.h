/* 文件系统探测、设备目录与挂载配置。 */

#pragma once
#include <os/storage.h>

#include <cstddef>
#include <cstdint>

#include "service/common/protocol.h"
#include "service/common/records.h"
#include "service/fs/vfs.h"

namespace fs {

    enum class Format : uint8_t { Unknown, Ext3, Ext4, Fat32, ExFat, Iso9660 };

    /* 根据磁盘内容识别文件系统格式，向挂载流程提供探测结果。 */
    class FormatProbe {
        private:
            const uint8_t* bootData_;
            size_t bootSize_;
            const uint8_t* isoDescriptor_;
            size_t isoSize_;

        public:
            FormatProbe(const uint8_t* bootData = nullptr, size_t bootSize = 0,
                        const uint8_t* isoDescriptor = nullptr,
                        size_t isoSize = 0);
            ~FormatProbe() = default;
            const char* formatName(Format format);
            Format detectFormat();
    };

}  // namespace fs

namespace fs::server {

    /* 保存一条挂载配置的设备、路径、格式及选项。 */
    class MountSpec {
        public:
            MountSpec() = default;
            ~MountSpec() = default;

            const char* getSource() const {
                return source;
            }

            const char* getTarget() const {
                return target;
            }

            const char* getType() const {
                return type;
            }

            bool isReadOnly() const {
                return readOnly;
            }

        private:
            friend class Fstab;
            friend class FsServer;
            char source[128]{};
            char target[128]{};
            char type[16]{};
            char options[64]{};
            uint32_t dump = 0;
            uint32_t pass = 0;
            bool readOnly = false;
            bool automatic = true;
            bool required = true;
    };

    /* 解析挂载配置并提供挂载项列表。 */
    class Fstab {
        public:
            // Advances by one line. A blank/comment line has present == false.
            static bool canonicalTarget(char* target);

            explicit Fstab(const char*& text) : cursor_(text) {
            }

            ~Fstab() = default;
            bool next(MountSpec& spec, bool& present);

        private:
            const char*& cursor_;
    };

    // The catalog capacity belongs to the shared storage service contract.

    /* 保存设备身份、类别和服务访问句柄。 */
    class DeviceRecord {
        public:
            DeviceRecord() = default;
            ~DeviceRecord() = default;

            const services::wire::StorageDeviceIdentity& getIdentity() const {
                return identity;
            }

            Format getFormat() const {
                return format;
            }

            const char* getUuid() const {
                return uuid;
            }

            const char* getLabel() const {
                return label;
            }

        private:
            friend class DeviceCatalog;
            friend class FsServer;
            services::wire::StorageDeviceIdentity identity{};
            Format format = Format::Unknown;
            char uuid[40]{};
            char label[40]{};
            char opticalName[16]{};
    };

    /* 枚举和保存可用设备，把设备标识转换为文件系统使用的记录。 */
    class DeviceCatalog {
        public:
            static constexpr int missing = -1;
            static constexpr int ambiguous = -2;
            bool add(const services::wire::StorageDeviceIdentity& identity,
                     SectorDevice& device);
            int resolve(const char* source) const;
            const DeviceRecord* get(uint32_t index) const;

            uint32_t count() const {
                return count_;
            }

            int list(const char* directory, uint32_t index,
                     DirectoryEntry& entry) const;

        private:
            DeviceRecord records_[services::maximumStorageDevices]{};
            uint32_t count_ = 0;
            uint32_t opticalCount_ = 0;
    };

}  // namespace fs::server

namespace fs::ext3 {

    /* 检查磁盘内容是否符合该文件系统格式，并提供实例创建入口。 */
    class Probe {
        public:
            Probe(const uint8_t* data = nullptr, size_t size = 0)
                : data_(data), size_(size) {
            }

            ~Probe() = default;
            bool detect();

        private:
            const uint8_t* data_;
            size_t size_;
    };
}  // namespace fs::ext3

namespace fs::ext4 {

    /* 检查磁盘内容是否符合该文件系统格式，并提供实例创建入口。 */
    class Probe {
        public:
            Probe(const uint8_t* data = nullptr, size_t size = 0)
                : data_(data), size_(size) {
            }

            ~Probe() = default;
            bool detect();

        private:
            const uint8_t* data_;
            size_t size_;
    };
}  // namespace fs::ext4

namespace fs::fat {

    /* 检查磁盘内容是否符合该文件系统格式，并提供实例创建入口。 */
    class Probe {
        public:
            Probe(const uint8_t* data = nullptr, size_t size = 0)
                : data_(data), size_(size) {
            }

            ~Probe() = default;
            bool detect();
            uint64_t blockOffset(uint32_t cluster, uint32_t sectorsPerCluster,
                                 uint32_t firstDataSector,
                                 uint32_t bytesPerSector);

        private:
            const uint8_t* data_;
            size_t size_;
    };
}  // namespace fs::fat

namespace fs::iso {

    /* 检查磁盘内容是否符合该文件系统格式，并提供实例创建入口。 */
    class Probe {
        public:
            Probe(const uint8_t* data = nullptr, size_t size = 0)
                : data_(data), size_(size) {
            }

            ~Probe() = default;
            bool detect();
            uint64_t blockOffset(uint32_t block);

        private:
            const uint8_t* data_;
            size_t size_;
    };
}  // namespace fs::iso

namespace fs::exfat {

    /* 检查磁盘内容是否符合该文件系统格式，并提供实例创建入口。 */
    class Probe {
        public:
            Probe(const uint8_t* data = nullptr, size_t size = 0)
                : data_(data), size_(size) {
            }

            ~Probe() = default;
            bool detect();

        private:
            const uint8_t* data_;
            size_t size_;
    };
}  // namespace fs::exfat
