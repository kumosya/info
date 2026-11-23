/* 文件服务中打开文件与进程描述符的关联。 */
#include "service/fs/server.h"

namespace fs::server {

    /* 新描述符引用一个打开文件描述，并保存调用者的访问标志。 */
    FileDescriptor::FileDescriptor(uint64_t owner,
                                   OpenFileDescription* fileDescription,
                                   int openFlags)
        : ownerPid(owner), description(fileDescription), flags(openFlags) {
    }

}  // namespace fs::server
