#include "service/common/queries.h"
#include "service/common/records.h"
#include "service/common/runtime.h"
#include "service/common/kernel-requests.h"
#include "service/common/requests.h"
#include <os/ipc.h>
#include <os/task.h>
#include <os/mm.h>
#include <os/device.h>
#include <os/service.h>
#include <os/device.h>
#include <os/ipc.h>
#include <os/service.h>

namespace {
class PciServer {
 public:


uint64_t endpoint;

void respond(const os::IpcMessage& request,os::IpcMessage& reply) {
    os::detail::PciQuery service;
    reply={};reply.data[0]=static_cast<int64_t>(os::ErrorCode::PermissionDenied);reply.length=1;
    os::wire::TaskInfo caller{};
    if(!request.senderPid || request.handleCount || os::detail::TaskQuery().query(request.senderPid,caller) || !caller.trustedDeviceService)return;
    const auto operation=static_cast<os::PciServiceRequest>(request.type);
    if(operation==os::PciServiceRequest::Enumerate) {
        if(request.length!=1 || !request.data[0] || request.data[0]>64){reply.data[0]=static_cast<int64_t>(os::ErrorCode::InvalidArgument);return;}
        os::wire::PciFunctionInfo functions[64]{};const int count=service.enumerate(functions,request.data[0]);
        if(count<0){reply.data[0]=count;return;}
        reply.data[0]=0;reply.data[1]=count;os::Runtime::copyBytes(&reply.data[2],functions,count*sizeof(*functions));reply.length=2+(count*sizeof(*functions)+7)/8;return;
    }
    if(operation==os::PciServiceRequest::Query) {
        if(request.length!=4 || request.data[0]>UINT16_MAX || request.data[1]>255 || request.data[2]>=32 || request.data[3]>=8){reply.data[0]=static_cast<int64_t>(os::ErrorCode::InvalidArgument);return;}
        os::wire::PciFunctionInfo function{};const int status=service.query(request.data[0],request.data[1],request.data[2],request.data[3],function);
        reply.data[0]=status;if(!status){os::Runtime::copyBytes(&reply.data[1],&function,sizeof(function));reply.length=1+(sizeof(function)+7)/8;}return;
    }
    if((operation!=os::PciServiceRequest::ReadConfig && operation!=os::PciServiceRequest::WriteConfig) || request.length!=(operation==os::PciServiceRequest::ReadConfig?3U:4U) || request.data[0]>UINT16_MAX || (request.data[0]&3)){reply.data[0]=static_cast<int64_t>(os::ErrorCode::InvalidArgument);return;}
    os::wire::PciFunctionInfo function{};os::Runtime::copyBytes(&function,&request.data[1],sizeof(function));
    uint32_t value=0;int status;
    if(operation==os::PciServiceRequest::ReadConfig)status=service.read(function,request.data[0],value);
    else {if(request.data[3]>UINT32_MAX){reply.data[0]=static_cast<int64_t>(os::ErrorCode::InvalidArgument);return;}value=request.data[3];status=service.write(function,request.data[0],value,request.senderPid);}
    reply.data[0]=status;if(!status){reply.data[1]=value;reply.length=2;}
}


int run() {
    const uint64_t registry = os::getBootstrapHandle();
    endpoint = os::detail::EndpointControl().create();
    if (registry == 0 || endpoint == 0 ||
        !os::detail::RegistryClient(registry).registerName(os::pciServiceName, endpoint)) {
        os::Sys().log("[PCI] service registration failed\n");
        return 1;
    }
    os::Sys().log("[PCI] service ready\n");
    while (true) {
        os::IpcMessage request{};
        if (os::receiveMessage(endpoint, request) != 0) continue;
        os::IpcMessage reply{};
        respond(request, reply);
        os::replyMessage(endpoint, request.replyToken, reply);
    }
}
};
}  // namespace

int main(int, char**) {
    static PciServer server;
    return server.run();
}
