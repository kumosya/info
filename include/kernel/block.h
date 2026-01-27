#ifndef INFO_KERNEL_BLOCK_H_
#define INFO_KERNEL_BLOCK_H_

#include <cstdint>
#include <cstring>

namespace block {

enum DeviceType {
    DEVICE_TYPE_UNKNOWN = 0,
    DEVICE_TYPE_IDE     = 1,
    DEVICE_TYPE_SCSI    = 2,
    DEVICE_TYPE_VIRTIO  = 3,
    DEVICE_TYPE_ATA     = 4
};

enum RequestType { REQ_TYPE_READ = 0, REQ_TYPE_WRITE = 1, REQ_TYPE_FLUSH = 2 };

class Bio {
   public:
    Bio(std::uint64_t sec, std::uint32_t cnt, void *buffer, RequestType type)
        : sector(sec), count(cnt), buf(buffer), rw(type), next(nullptr) {}
    ~Bio() = default;

    std::uint64_t sector;
    std::uint32_t count;
    void *buf;
    RequestType rw;
    Bio *next;
};

class Request {
   public:
    Request(Bio *b)
        : sector(b->sector),
          count(b->count),
          buf(b->buf),
          rw(b->rw),
          next(nullptr),
          bio_list(b) {}
    ~Request() {
        Bio *b = bio_list;
        while (b) {
            Bio *next = b->next;
            delete b;
            b = next;
        }
    }

    std::uint64_t sector;
    std::uint32_t count;
    void *buf;
    RequestType rw;
    Request *next;
    Bio *bio_list;
};

class RequestQueue {
   public:
    RequestQueue()
        : head(nullptr),
          tail(nullptr),
          request_fn(nullptr),
          max_sectors(0),
          max_hw_sectors(0),
          plugged(false),
          count(0) {}
    ~RequestQueue() {
        while (!empty()) {
            Request *req = pop();
            delete req;
        }
    }

    bool empty() const { return head == nullptr; }

    void push(Request *req) {
        if (empty()) {
            head = tail = req;
        } else {
            tail->next = req;
            tail       = req;
        }
        tail->next = nullptr;
        count++;
    }

    Request *pop() {
        if (empty()) return nullptr;
        Request *req = head;
        head         = head->next;
        if (head == nullptr) tail = nullptr;
        count--;
        return req;
    }

    void set_request_fn(void (*fn)(Request *)) { request_fn = fn; }
    void (*get_request_fn())(Request *) { return request_fn; }

    Request *head;
    Request *tail;
    void (*request_fn)(Request *);
    std::uint16_t max_sectors;
    std::uint16_t max_hw_sectors;
    bool plugged;
    int count;
};

class HdStruct {
   public:
    HdStruct(std::uint64_t start, std::uint64_t num, const char *nm)
        : start_sect(start), nr_sects(num), next(nullptr) {
        strncpy(name, nm, sizeof(name) - 1);
        name[sizeof(name) - 1] = '\0';
    }
    ~HdStruct() {
        if (next) delete next;
    }

    std::uint64_t start_sect;
    std::uint64_t nr_sects;
    char name[32];
    HdStruct *next;
};

class BlockDevice {
   public:
    BlockDevice(const char *dev_name, DeviceType dev_type)
        : queue(new RequestQueue()),
          capacity(0),
          sector_size(512),
          type(dev_type),
          partitions(nullptr),
          part_count(0),
          next(nullptr) {
        strncpy(disk_name, dev_name, sizeof(disk_name) - 1);
        disk_name[sizeof(disk_name) - 1] = '\0';
    }
    virtual ~BlockDevice() {
        if (queue) delete queue;
        if (partitions) delete partitions;
    }

    virtual int Read(std::uint64_t sector, std::uint32_t count, void *buf) = 0;
    virtual int Write(std::uint64_t sector, std::uint32_t count,
                      const void *buf)                                     = 0;
    virtual int Ioctl(std::uint32_t cmd, void *arg)                        = 0;

    void flush_plug() {
        if (queue->plugged) {
            queue->plugged = false;
            while (!queue->empty() && queue->request_fn) {
                Request *req = queue->pop();
                if (req) {
                    queue->request_fn(req);
                }
            }
        }
    }

    char disk_name[32];
    RequestQueue *queue;
    std::uint64_t capacity;
    std::uint16_t sector_size;
    DeviceType type;
    HdStruct *partitions;
    int part_count;
    BlockDevice *next;
};

class IDEBlockDevice : public BlockDevice {
   public:
    IDEBlockDevice(const char *dev_name, std::uint16_t io_base,
                   std::uint8_t drive)
        : BlockDevice(dev_name, DEVICE_TYPE_IDE),
          io_base_(io_base),
          drive_(drive) {
        queue->max_sectors    = 256;
        queue->max_hw_sectors = 256;
    }
    virtual ~IDEBlockDevice() = default;

    int Read(std::uint64_t sector, std::uint32_t count, void *buf) override;
    int Write(std::uint64_t sector, std::uint32_t count,
              const void *buf) override;
    int Ioctl(std::uint32_t cmd, void *arg) override;

   private:
    std::uint16_t io_base_;
    std::uint8_t drive_;
};

int Service(int argc, char *argv[]);
void RegisterDevice(BlockDevice *dev);
BlockDevice *FindDevice(const char *name);
int submit_bio(Bio *bio);
void blk_queue_make_request(RequestQueue *q, void (*fn)(Request *));
void add_partition(BlockDevice *dev, std::uint64_t start, std::uint64_t num,
                   int minor);

}  // namespace block

#endif  // INFO_KERNEL_BLOCK_H_
