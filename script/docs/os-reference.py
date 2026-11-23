"""从 OS 库声明整理枚举、常量、对象和函数目录。"""
from pathlib import Path
import argparse
import ast
import json
import re

root = Path(__file__).resolve().parents[2]
docs = root / "docs/lib/os"
work = root.parent / ".annotation-work"
parser = argparse.ArgumentParser()
parser.add_argument("--inspect", action="store_true")
args = parser.parse_args()

def masked(text):
    return re.sub(r'//[^\n]*|/\*[\s\S]*?\*/|"(?:\\.|[^"\\])*"|\'(?:\\.|[^\'\\])*\'',
                  lambda match: "".join("\n" if char == "\n" else " " for char in match[0]), text)

def spans(text):
    code = masked(text)
    stack, ends = [], {}
    depth = [0] * (len(code) + 1)
    for position, char in enumerate(code):
        depth[position] = len(stack)
        if char == "{": stack.append(position)
        elif char == "}" and stack: ends[stack.pop()] = position
    nodes = []
    pattern = r'\b(namespace|class|struct|enum(?:\s+class)?)\s*([\w:]*)[^;{}]*\{'
    for match in re.finditer(pattern, code):
        begin = code.index("{", match.start())
        if begin not in ends: continue
        kind, name = match[1], match[2]
        parent = [item for item in nodes if item["begin"] < match.start() < item["end"]]
        prefix = parent[-1]["full"] if parent else ""
        full = (prefix + "::" if prefix else "") + name
        if parent and "::" in name and parent[-1]["kind"] == "namespace":
            full = prefix + "::" + name
        public = True
        owner = next((item for item in reversed(parent) if item["kind"] in {"class", "struct"}), None)
        if owner:
            public = owner["kind"] == "struct"
            for access in re.finditer(r'\b(public|private|protected)\s*:', code[owner["begin"]+1:match.start()]):
                at = owner["begin"] + 1 + access.start()
                if depth[at] == depth[owner["begin"]] + 1: public = access[1] == "public"
            public = public and owner["public"]
        nodes.append({"kind":kind,"name":name,"full":full,"begin":begin,"end":ends[begin],
                      "start":match.start(),"public":public})
    return code, nodes

def owner_at(nodes, position):
    owners = [node for node in nodes if node["begin"] < position < node["end"]]
    return owners[-1] if owners else None

def short(value):
    value = re.sub(r'\s+', ' ', value.strip())
    return value.replace('|', '\\|')

def class_members(text, code, nodes, classes):
    members = {name: [] for name in classes}
    depths = [0] * (len(code) + 1)
    for position, char in enumerate(code):
        depths[position + 1] = depths[position] + (char == '{') - (char == '}')

    pattern = r'^[ \t]*([^\n;{}]*?(?<![\w~])(~?\w+|operator\s*(?:=|bool))\s*\()'
    for match in re.finditer(pattern, code, re.M):
        owner = owner_at(nodes, match.start(2))
        if not owner or owner['full'] not in members: continue
        if depths[match.start(2)] != depths[owner['begin']] + 1: continue
        public = owner['kind'] == 'struct'
        for access in re.finditer(r'\b(public|private|protected)\s*:', code[owner['begin']+1:match.start()]):
            at = owner['begin'] + 1 + access.start()
            if depths[at] == depths[owner['begin']] + 1: public = access[1] == 'public'
        if not public: continue
        opening = code.index('(', match.start())
        end, depth = opening + 1, 1
        while end < len(code) and depth:
            depth += (code[end] == '(') - (code[end] == ')')
            end += 1
        suffix = re.match(r'\s*(?:const\s*)?(?:noexcept\s*)?(?:override\s*)?(?:=\s*(?:default|delete|0)\s*)?;', code[end:])
        if not suffix or re.search(r'=\s*delete', suffix[0]): continue
        name = match[2]
        signature = short(text[match.start():end+suffix.end()].strip().removesuffix(';'))
        category = '成员函数'
        if re.search(r'\bstatic\b', code[match.start():opening]): category = '静态成员函数'
        elif name.lstrip('~') == owner['name']: category = '构造与析构'
        elif re.match(r'(?:get|is|has|can|supports|uses)[A-Z]', name): category = 'Getter'
        elif re.match(r'set[A-Z]', name): category = 'Setter'
        members[owner['full']].append((owner['full']+'::'+name, signature, category))
    return members

def parse_header(path):
    text = path.read_text()
    code, nodes = spans(text)
    classes = [node["full"] for node in nodes if node["kind"] == "class" and node["public"]
               and "::detail::" not in node["full"]]
    constants = []
    for match in re.finditer(r'^\s*#define\s+(OS_\w+)\s+([^\n]+)', text, re.M):
        constants.append((match[1],match[2].split('//')[0].strip()))
    for match in re.finditer(r'\b(?:inline\s+)?constexpr\s+[^;{}=]+?\b(\w+)\s*=\s*([^;]+);', code):
        owner = owner_at(nodes,match.start())
        if owner and owner["kind"] in {"class","struct"}: continue
        prefix = owner["full"]+"::" if owner else ""
        constants.append((prefix+match[1],text[match.start(2):match.end(2)].strip()))
    enums = []
    for node in nodes:
        if not node["kind"].startswith("enum") or not node["public"]: continue
        entries = []
        for item in text[node["begin"]+1:node["end"]].split(','):
            item = re.sub(r'//[^\n]*|/\*[\s\S]*?\*/','',item).strip()
            if not item: continue
            name, equal, value = item.partition('=')
            entries.append((name.strip(),value.strip() if equal else None))
        enums.append((node["full"] if node["name"] else "寄存器下标",entries))
    functions = []
    pattern = r'^[ \t]*(?:\[\[[^\]]*\]\]\s*)?([\w:<>,*& \t]+?)\s+(\w+)\s*\([^;{}]*\)\s*;'
    for match in re.finditer(pattern,code,re.M):
        owner = owner_at(nodes,match.start())
        if owner and owner["kind"] != "namespace": continue
        if match[1].strip().startswith(('typedef','using','return')) or match[2] == 'static_assert': continue
        signature = short(text[match.start():match.end()].strip().removesuffix(';'))
        full = (owner["full"]+"::" if owner else "")+match[2]
        functions.append((full,signature))
    return {"path":str(path.relative_to(root)),"classes":classes,"constants":constants,"enums":enums,"functions":functions,
            "members":class_members(text, code, nodes, classes)}

headers = sorted((root/'include/os-c++/os').glob('*.h'))
modules = {path.stem:parse_header(path) for path in headers}
(work/'os-reference-inventory.json').write_text(json.dumps(modules,ensure_ascii=False,indent=2))
if args.inspect:
    for name,module in modules.items():
        print(name, 'classes=',','.join(module['classes']), 'enums=',len(module['enums']), 'functions=',len(module['functions']))

class_text = {
 'Console':'控制台对象，提供终端输入、输出和显示设置。', 'Console::EscapeParser':'终端转义序列解析对象，保存解析进度和参数。',
 'Console::Style':'终端文字样式对象，保存前景色、背景色和显示属性。',
 'Device':'设备访问对象，持有设备连接。', 'Device::Pci':'PCI 功能对象，保存总线地址和设备属性。',
 'Device::Dma':'DMA 缓冲区对象，保存内存映射和设备访问地址。', 'Device::Mmio':'设备寄存器映射对象，保存 BAR 区域和访问地址。',
 'Device::Frame':'显示缓冲区对象，保存像素映射和显示参数。', 'Device::Irq':'硬件中断绑定对象，连接中断线路和消息端点。',
 'Dir':'目录对象，提供目录访问和约定路径解析。', 'Dir::Entry':'目录项对象，保存名称、类型、文件编号和枚举位置。',
 'Executable':'ELF 映像对象，保存文件头、程序段和加载信息。',
 'Mount':'挂载对象，保存设备、挂载点和文件系统属性。', 'File':'打开文件对象，持有文件连接、访问方式和位置。',
 'File::Opt':'文件打开选项对象，保存访问方式、创建属性和权限。', 'Fs':'文件系统连接对象，提供路径操作和挂载查询。',
 'Input':'输入设备连接对象，接收键盘和鼠标事件。', 'Input::Event':'输入事件对象，保存事件类型、按键、坐标和修饰键状态。',
 'Ipc':'消息端点对象，提供消息发送、接收和请求回复。', 'Ipc::Handle':'内核句柄对象，管理句柄的持有和关闭。',
 'Ipc::Msg':'消息对象，保存消息数据和附带句柄。', 'Ipc::Pipe':'管道对象，提供字节流读写。',
 'Ipc::MsgQueue':'消息队列对象，提供消息发送、接收和队列属性查询。',
 'Mem':'进程内存对象，查询内存占用和可用容量。', 'Mem::Page':'页对象，保存内核页面句柄和物理内存属性。',
 'Mem::Map':'地址映射对象，管理映射范围、访问权限和后备存储。', 'Mem::Share':'共享内存对象，保存名称、句柄和容量。',
 'Pty':'伪终端对象，连接主从端并管理终端属性。',
 'Sys':'系统信息对象，提供时间、日志和系统状态访问。', 'Sys::Transport':'核心服务通信对象，保存目标服务类别。',
 'Sys::Status':'服务状态对象，保存服务配置和运行状态。', 'Sys::Recovery':'服务恢复对象，保存恢复检查点和状态。',
 'Sys::Registry':'服务注册表对象，提供名称注册与查找。', 'Sys::Connection':'服务连接对象，保存服务端点和连接状态。',
 'Sys::Manager':'服务管理对象，提供启动、停止和配置操作。', 'Sys::Checkpoint':'服务检查点对象，保存恢复数据和资源令牌。',
 'Storage':'存储设备目录对象，枚举和查询块设备。', 'Storage::Statistics':'存储统计对象，保存请求数、传输字节数和错误计数。',
 'Storage::Number':'设备编号对象，保存主设备号和次设备号。', 'Storage::Part':'分区对象，保存起始块、容量、类型和标识。',
 'Storage::Device':'块设备连接对象，提供块读写、缓存刷新和属性查询。',
 'SpinLock':'自旋锁对象，管理短临界区的互斥访问。', 'SpinLock::Guard':'自旋锁作用域对象，管理作用域内的持锁时间。',
 'Mutex':'互斥锁对象，管理线程间的互斥访问。', 'Mutex::Guard':'互斥锁作用域对象，管理作用域内的持锁时间。',
 'AddressWait':'地址等待对象，保存等待序号并协调线程唤醒。',
 'Task':'进程对象，保存进程连接、身份和运行状态。', 'Task::Image':'进程映像对象，保存映像内容和启动参数。',
 'Task::Spawn':'进程创建对象，保存新进程的程序、参数和资源设置。', 'Task::Exec':'程序执行对象，保存映像替换所需的路径、参数和环境。',
 'Task::Core':'进程转储对象，保存转储结果和映像信息。', 'Task::Signal':'信号对象，保存信号动作和投递参数。',
 'Task::Credential':'进程身份对象，保存用户、组和附加组信息。', 'Task::SignalStack':'信号栈对象，保存备用栈地址、容量和状态。',
 'Task::Child':'子进程对象，保存等待结果和退出状态。', 'Task::ImageMapping':'映像映射对象，保存进程映像的地址范围和文件信息。',
 'Task::Thread':'线程对象，保存线程连接、入口、调度信息和执行状态。', 'Task::Session':'进程会话对象，保存会话编号、进程组和控制终端。',
 'User':'用户服务连接对象，提供账户、认证和登录会话访问。', 'User::Profile':'用户资料对象，保存用户名、身份、主目录和登录程序。',
 'Runtime':'进程运行环境对象，保存启动端点、环境变量、终端选择和退出处理。',
 'ConfigurationText':'配置文本对象，保存文本范围并检查字符和路径格式。',
 'BootConsole':'启动终端连接对象，持有进度报告和显示交接使用的端点。',
 'DisplayHandoff':'显示交接连接对象，持有窗口服务端点。',
 'GraphicalSession':'图形会话连接对象，持有会话查询和控制使用的端点。',
 'console::BootState':'控制台启动过程对象，保存启动显示阶段并检查交接请求。',
 'SessionIdentity':'任务认证身份连接对象，保存目标任务、任务服务通道和身份查询结果。',
 'SessionCleanup':'认证会话清理对象，保存待结束的认证会话编号。',
}

titles = {'capi':'C 接口','console':'控制台','device':'设备','dir':'目录','elf':'ELF 映像','file':'文件系统',
          'input':'输入事件','ipc':'进程通信','mm':'内存','pty':'伪终端','service':'系统服务','storage':'块存储',
          'sync':'线程同步','syscall':'系统调用','task':'进程与线程','user':'用户账户','runtime':'运行环境','boot':'启动与图形会话'}
titles['session'] = '认证身份与会话'
forbidden = re.compile(r'当前来源|推荐头文件|内容与边界|显式|隐式|不能|不应|不得|避免|尚未|不代表|不支持|不要|无需|不是|不再')

def brief(text):
    text = re.sub(r'`([^`]+)`',r'\1',text)
    text = re.sub(r'\s+','',text).strip('。； ')
    text = re.split(r'[，；。]',text)[0]
    return '' if forbidden.search(text) or re.match(r'(如果|仅在|需要|失败|成功|若|先)',text) else text

seed_path = work/'os-reference-seed.json'
if seed_path.exists():
    seed = json.loads(seed_path.read_text())
else:
    seed = {'functions':{},'members':{},'values':{}}
    for file in docs.glob('*.md'):
        original = file.read_text()
        for match in re.finditer(r'```cpp\s*\n([\s\S]*?)\n```([\s\S]*?)(?=\n```cpp|\n#{1,4} |\Z)',original):
            signature, body = match[1],match[2]
            name_match = re.search(r'([~\w]+)\s*\(',signature)
            meaning = re.search(r'(?:用途|数据)：([^\n]+)',body)
            if not name_match or not meaning: continue
            name, description = name_match[1],brief(meaning[1])
            if not description: continue
            if meaning[0].startswith('数据'):
                description = '读取'+description
            if name.startswith('os') or name in {'invokeSyscall','sendMessage','receiveMessage','callService','replyMessage','waitAddress','wakeAddress','notifyReadiness'}:
                seed['functions'][name] = description
            seed['members'].setdefault(name,description)
        for line in original.splitlines():
            cells = [cell.strip().strip('`') for cell in line.strip().strip('|').split('|')]
            if len(cells) >= 3 and re.fullmatch(r'[A-Za-z_]\w*(?:::\w+)*',cells[0]):
                description = brief(cells[2])
                if description: seed['values'].setdefault(cells[0].split('::')[-1],description)
    seed_path.write_text(json.dumps(seed,ensure_ascii=False,indent=2))

meanings = {'Readiness':'就绪状态通知','OS_SYS_READINESS':'就绪状态通知编号','OS_SYS_COUNT':'系统调用数量',
            'OS_SYS_BASE':'系统调用起始编号','Count':'项目数量','Ide':'IDE ATA 磁盘','Ahci':'AHCI SATA 磁盘',
            'Unknown':'未知类型','None':'空值','Success':'成功','NotSupported':'功能支持错误',
            'CapabilityUnknown':'设备能力未知','DeviceNotFound':'块设备查找失败','NotFound':'目标查找失败',
            'BadAddress':'地址访问错误','LeftSuper':'左 Win 键','RightSuper':'右 Win 键','Super':'Win 键状态位',
            'WouldBlock':'等待就绪','Nonblocking':'非阻塞方式',
            'Starting':'启动中','Greeter':'登录界面','Authenticating':'认证中','Desktop':'桌面',
            'Suspended':'已挂起','SetActive':'设置图形显示活动状态','GetActive':'查询图形显示活动状态',
            'Bind':'绑定会话控制进程','State':'查询会话状态','GreeterReady':'登录界面就绪',
            'Authenticated':'身份认证完成','DesktopReady':'桌面就绪','Reset':'重置会话',
            'Suspend':'挂起会话','Resume':'恢复会话'}

purpose = {
 'current':'取得进程运行环境对象','copyBytes':'复制字节数据','clearMemory':'将内存内容置零','copyString':'复制字符串',
 'stringsEqual':'比较两个字符串','stringLength':'计算字符串长度','prepare':'设置进程启动环境','runtimeInit':'初始化进程运行环境',
 'runtimeFini':'执行进程退出处理','kernelBootstrapHandle':'读取内核引导端点','registryBootstrapHandle':'读取服务注册表端点',
 'getBootstrapHandle':'读取服务注册表端点','getStandardHandle':'读取标准输入输出句柄','bindEnvironment':'绑定进程环境变量',
 'bindWorkingDirectory':'绑定工作目录查询函数','absolutePath':'解析绝对路径','environmentValue':'读取环境变量值',
 'initializeHeap':'初始化进程堆','allocateMemory':'分配进程堆内存','freeMemory':'释放进程堆内存',
 'prepareHeapFork':'准备进程堆的派生操作','finishHeapFork':'恢复父进程堆的访问','resetHeapAfterFork':'设置子进程堆状态',
 'addFinalizer':'登记退出处理函数','runFinalizers':'执行退出处理函数','lockFinalizers':'取得退出处理表的锁',
 'unlockFinalizers':'释放退出处理表的锁','heap':'取得进程堆对象','getConsoleId':'读取进程终端编号','setConsoleId':'设置进程终端编号',
 'printString':'向进程终端输出字符串','osRuntimeInit':'初始化程序运行环境','osRuntimeFini':'执行程序退出处理',
 'atexit':'登记程序退出处理函数','__osinfo_runtime_allocate':'分配运行库内存','__osinfo_runtime_free':'释放运行库内存',
 '__osinfo_runtime_fork_prepare':'准备运行库的进程派生状态','__osinfo_runtime_fork_parent':'恢复父进程运行库状态',
 '__osinfo_runtime_fork_child':'设置子进程运行库状态','notifyReadiness':'通知就绪状态变化',
 'exitTask':'结束进程','exitThread':'结束线程','createThread':'创建线程','forkTask':'派生进程',
 'replaceTaskImage':'替换进程映像','yield':'让出处理器','sleep':'等待指定时间','mapMemory':'建立内存映射',
 'unmapMemory':'解除内存映射','protectMemory':'设置内存访问权限','closeHandle':'关闭内核句柄',
 'valid':'检查配置文本格式','decode':'读取对象属性','encode':'编码对象属性','open':'打开对象连接','close':'关闭对象连接',
 'refresh':'更新对象属性','reset':'重置对象状态','release':'交出对象持有的资源','adopt':'接管资源句柄','borrow':'借用资源句柄',
 'lock':'取得互斥访问权','unlock':'释放互斥访问权','tryLock':'尝试取得互斥访问权',
}
seed['functions'].update(purpose)
purpose.update(dict(line.split('=', 1) for line in '''decodeStat=解码文件属性
fileSimple=提交文件句柄操作
fsResult=读取文件系统请求结果
pathRequest=编码路径请求
rawCall=发送服务请求并接收回复
rawOutput=转换系统调用结果
storageDeadline=计算块传输的截止时间
finish=完成启动终端交接
report=输出启动状态
state=读取会话状态
feed=解析输入的转义序列字节
getParameter=读取转义序列参数
getParameterCount=读取转义序列参数数量
apply=应用终端文字样式
getBackground=读取背景色
getForeground=读取前景色
setBackground=设置背景色
setForeground=设置前景色
portIo=提交端口输入输出请求
readPort=读取硬件端口
writePort=写入硬件端口
search=查找目录项
service=取得文件系统服务连接
userId=读取目录所属用户编号
validApplication=检查应用名称
setGraphicalActive=设置图形界面活动状态
submit=提交异步文件操作
transfer=执行数据传输
bind=绑定对象资源
command=执行图形会话命令
invoke=提交对象请求
accepted=读取消息接收状态
sent=读取消息发送状态
getCurrentMessages=读取队列中的消息数量
cancel=取消端点等待
refreshNumber=更新伪终端编号
request=提交伪终端请求
assign=记录分配结果
cache=缓存空闲内存块
cached=查询内存块缓存状态
capacity=读取内存块容量
discardCache=释放缓存内存块
released=查询内存块释放状态
allocate=分配堆内存
forkChild=恢复子进程锁状态
forkParent=释放父进程派生锁
forkPrepare=锁定进程派生所需状态
free=释放堆内存
initialize=初始化堆管理数据
flushStatus=读取写缓存刷新能力
openService=建立存储服务连接
statistics=读取块设备统计
checksum=计算检查点校验值
ensureStaging=准备检查点暂存内存
openSnapshot=打开检查点快照
publish=发布检查点
update=更新服务管理状态
lookupRaw=查询服务注册记录
putName=编码服务名称
registerRaw=登记服务端点
unregisterRaw=撤销服务端点登记
callOnce=执行一次服务消息调用
accessible=检查进程访问权限
coreLimit=读取核心转储大小限制
createSession=创建进程会话
execute=启动程序
executeRequest=构造程序执行请求
executeService=取得程序执行服务
executeWithIo=使用指定输入输出启动程序
getIdentifiers=读取父进程、进程组和会话编号
getPendingSignals=读取待处理信号
getThreadId=读取当前线程编号
id=读取进程编号
kill=发送进程信号
queryPendingSignals=查询待处理信号集合
recordAuxiliary=记录程序辅助向量
recordImage=记录执行映像信息
refreshExitTarget=更新进程退出端点
refreshTarget=更新进程访问端点
sendThreadSignal=向指定线程发送信号
setCredentials=设置进程身份
setGroupTarget=设置进程组目标
signalReturn=恢复信号处理前的执行状态
signalStack=读取备用信号栈
spawnImage=从执行映像创建进程
spawnProgram=按程序路径创建进程
waitChild=等待子进程状态变化
waitThread=等待线程退出
run=运行线程入口函数
exchange=交换用户服务消息
copyInput=复制用户请求文本
erase=清除认证数据
terminated=检查字符串结束字符
alignment=计算内存对齐量
bufferedRead=读取终端缓冲数据
callService=调用服务端点
closeEndpoint=关闭消息端点
allocateTyped=分配指定类型的内存对象
anonymous=建立匿名内存映射
file=建立文件内存映射
memoryOffset=计算映射对应的内存位置
openShared=打开命名共享内存
openTyped=打开指定类型的内存对象
owned=查询内存对象持有状态
policy=读取映射策略
protect=设置映射访问权限
shared=建立共享内存映射
typedInfo=读取内存对象属性
unlinkShared=移除共享内存名称
unmap=解除地址映射
allocatePages=分配物理页
mapPages=映射物理页
mapSharedMemory=映射共享内存
openSharedMemory=打开共享内存对象
protectTarget=设置目标映射权限
resizeSharedMemory=调整共享内存大小
unlinkSharedMemory=移除共享内存名称
unmapTarget=解除目标地址映射
pageInfo=读取物理页属性
usage=读取内存使用量
endpoint=读取服务端点
unregister=撤销服务登记
deviceCount=读取块设备数量
getSystemInfo=读取系统信息
credentials=读取进程身份
exitInfo=读取进程退出信息
threadInfo=读取线程信息
disconnected=查询连接断开状态
error=转换错误码
gone=查询资源失效状态
inputError=转换输入错误码
inputResult=读取输入请求结果
invalid=生成参数错误结果
invokeSyscall=执行系统调用
mappingProtection=转换内存映射权限
mappingSize=计算内存映射大小
memoryError=转换内存操作错误码
memoryRequest=构造内存服务请求
posixAttribute=转换文件属性
range=检查内存范围
receiveMessage=接收端点消息
releaseInputEndpoint=释放输入服务端点
replyMessage=回复端点消息
sendMessage=发送端点消息
startLibraryThread=启动运行库线程
taskAttribute=转换进程属性
wakeAddress=唤醒地址等待者'''.splitlines()))

c_subjects = {'Console':'控制台','Dir':'目录','File':'文件','Fs':'文件系统','Ipc':'进程通信',
              'Mem':'内存','Mount':'挂载','Pty':'伪终端','Sync':'同步','Sys':'系统','Task':'进程'}
c_actions = dict(line.split('=', 1) for line in '''GetId=读取编号
SetId=设置编号
Read=读取数据
Write=写入数据
Open=打开连接
Close=关闭连接
Create=创建对象
Remove=删除对象
Rename=重命名对象
Flush=刷新数据
Query=查询属性
ReadAsync=提交异步读取
ReadAt=读取指定位置的数据
Seek=设置读写位置
SetMode=设置权限
SetOwner=设置所属用户与组
SetTimes=设置时间属性
Truncate=调整文件长度
WaitOperation=等待异步操作完成
WriteAsync=提交异步写入
WriteAt=写入指定位置的数据
Format=创建文件系统
Link=创建链接
Call=发送请求并等待回复
HandleClose=关闭句柄
HandleDuplicate=复制句柄
HandleRestrict=设置句柄权限
HandleSetFlags=设置句柄标志
MsgQueueOpen=打开消息队列
MsgQueueQuery=查询消息队列属性
MsgQueueReceive=接收队列消息
MsgQueueSend=发送队列消息
MsgQueueSetFlags=设置消息队列标志
MsgQueueSetNotification=设置消息到达通知
MsgQueueUnlink=移除消息队列名称
PipeCreate=创建管道
PipeRead=读取管道
PipeWrite=写入管道
Receive=接收消息
Reply=回复消息
Send=发送消息
CheckRange=检查地址范围
MapAnonymous=建立匿名映射
MapFile=建立文件映射
MapSetProtection=设置映射权限
MapShared=建立共享映射
MapSync=同步映射数据
MapUnmap=解除映射
ShareOpen=打开共享内存
ShareResize=调整共享内存大小
ShareUnlink=移除共享内存名称
GetNumber=读取伪终端编号
SetForegroundGroup=设置前台进程组
SetNonblocking=设置非阻塞方式
WaitAddress=等待地址值变化
WakeAddress=唤醒地址等待者
ConnectionLookup=查询服务连接
ReadClockResolution=读取时钟分辨率
ReadMonotonicTime=读取单调时钟
ReadWallClockTime=读取系统时间
Execute=启动程序
Exit=退出进程
Fork=派生进程
GetCoreLimit=读取核心转储大小限制
GetCredentials=读取身份信息
GetGroupId=读取进程组编号
GetNice=读取调度优先级
GetParentId=读取父进程编号
GetPendingSignals=读取待处理信号
GetResourceLimit=读取资源限制
GetSessionId=读取会话编号
GetSignalAction=读取信号处理设置
GetSignalMask=读取信号屏蔽集合
GetSignalStack=读取备用信号栈
ReplaceImage=替换执行映像
SendSignal=发送信号
SessionCreate=创建会话
SetCoreLimit=设置核心转储大小限制
SetGroup=设置进程组
SetGroupIds=设置用户组身份
SetGroups=设置附加用户组
SetNice=设置调度优先级
SetResourceLimit=设置资源限制
SetSignalAction=设置信号处理方式
SetSignalMask=设置信号屏蔽集合
SetSignalStack=设置备用信号栈
SetUserIds=设置用户身份
SignalReturn=恢复信号处理前的状态
Sleep=等待指定时间
ThreadCreate=创建线程
ThreadExit=退出线程
ThreadGetId=读取线程编号
ThreadSendSignal=向线程发送信号
ThreadWait=等待线程退出
WaitChild=等待子进程状态变化
WaitSignal=等待信号
Yield=让出处理器'''.splitlines())

qualified_purpose = dict(line.split('=', 1) for line in '''os::Device::Dma::create=分配设备 DMA 缓冲区
os::Device::Irq::create=绑定设备中断与消息端点
os::Dir::create=创建目录
os::Mount::create=建立文件系统挂载
os::detail::EndpointControl::create=创建消息端点
os::Ipc::Pipe::create=创建管道读写端点
os::Ipc::create=创建消息端点
os::Mem::Page::create=分配物理内存页
os::Mem::Map::create=建立地址映射
os::Pty::create=创建伪终端主从通道
os::Task::create=创建进程
os::Task::Thread::create=创建线程
os::Task::Session::create=创建进程会话
os::Console::read=读取控制台输入
os::Console::write=输出控制台文字
os::detail::PciQuery::read=读取 PCI 配置寄存器
os::detail::PciQuery::write=写入 PCI 配置寄存器
os::File::read=读取文件数据
os::File::write=写入文件数据
os::Ipc::Pipe::read=读取管道数据
os::Ipc::Pipe::write=写入管道数据
os::Ipc::Msg::read=读取消息字段
os::Ipc::Msg::write=写入消息字段
os::Storage::Device::read=读取指定范围的磁盘块
os::Storage::Device::write=写入指定范围的磁盘块
os::Ipc::MsgQueue::receive=接收队列消息
os::Ipc::receive=接收端点消息
os::Fs::query=查询文件系统属性
os::Sys::Manager::query=查询服务状态
os::detail::TaskQuery::query=查询进程属性
os::User::lookup=按用户编号读取账户资料
os::User::list=按序号读取账户资料
os::User::changePassword=修改账户密码
os::Console::terminalColor=将终端颜色索引转换为 RGB 像素颜色
os::AddressWait::wait=等待地址值变化
os::SessionIdentity::establish=登记登录会话身份
services::Lifecycle::bootName=识别启动阶段服务名称
services::Lifecycle::bootstrap=启动基础服务
services::Lifecycle::configError=报告配置行错误
services::Lifecycle::control=处理服务管理命令
services::Lifecycle::copy=复制配置字段
services::Lifecycle::dependencies=检查服务依赖状态
services::Lifecycle::failed=记录服务启动或退出失败
services::Lifecycle::find=按名称查找服务记录
services::Lifecycle::init=初始化服务管理对象
services::Lifecycle::load=加载服务配置
services::Lifecycle::number=解析配置中的整数
services::Lifecycle::persist=保存服务启用状态
services::Lifecycle::readConfiguration=读取服务配置文件
services::Lifecycle::restoresState=查询服务状态恢复方式
services::Lifecycle::supervise=启动服务监护线程
services::Lifecycle::tick=检查退出状态并重启到期服务
services::Lifecycle::validArguments=检查服务启动参数
services::Lifecycle::validName=检查服务名称
services::Lifecycle::waitReady=等待服务登记就绪端点
services::RecoveryCapabilities::collect=回收本轮使用范围之外的恢复令牌
services::RecoveryCapabilities::discardOrphans=回收孤立的恢复令牌
services::RecoveryCapabilities::keep=记录本轮使用的恢复令牌
services::ReplayCache::begin=识别重试并准备请求记录
services::ReplayCache::digest=计算请求内容摘要
services::ReplayCache::references=查询记录引用的恢复令牌
services::ReplayCache::remember=保存请求回复
services::ReplayCache::restoreReply=恢复已保存的回复
services::ReplayCache::retain=保留回复中的资源令牌'''.splitlines())
qualified_purpose['console::BootState::active'] = '查询启动显示阶段'
qualified_purpose['console::BootState::validate'] = '检查启动请求的调用者、格式和当前阶段'
qualified_purpose['console::BootState::complete'] = '完成启动显示交接'
qualified_purpose['os::SessionIdentity::query'] = '查询目标任务的认证身份'
qualified_purpose['os::SessionCleanup::terminate'] = '结束认证会话所属的用户进程'
fallbacks = set()

def describe(name):
    if name in qualified_purpose: return qualified_purpose[name]
    simple = name.split('::')[-1]
    if simple.startswith('os'):
        for subject, label in c_subjects.items():
            if simple.startswith('os'+subject):
                action = simple[len(subject)+2:]
                if action in c_actions: return label+'：'+c_actions[action]
    if simple.startswith('~'): return '析构'+simple[1:]+'对象'
    parts = name.split('::')
    if len(parts)>1 and simple==parts[-2]: return '构造'+simple+'对象'
    if 'operator' in simple: return '赋值对象属性' if '=' in simple else '读取对象状态'
    if simple in purpose: return purpose[simple]
    if re.match(r'set[A-Z]', simple):
        getter = 'get' + simple[3:]
        meaning = purpose.get(getter) or seed['members'].get(getter) or seed['members'].get(simple)
        if meaning and meaning.startswith('读取'): return '设置' + meaning[2:]
    if simple in seed['functions']: return seed['functions'][simple]
    if simple in seed['members']: return seed['members'][simple]
    verbs={'get':'读取','set':'设置','is':'查询','has':'查询','read':'读取','write':'写入','open':'打开','close':'关闭',
           'create':'创建','query':'查询','find':'查找','send':'发送','receive':'接收','wait':'等待','signal':'通知',
           'initialize':'初始化','finish':'完成','validate':'检查','clear':'清除','update':'更新','remove':'删除',
           'register':'登记','unregister':'撤销登记','refresh':'更新','resize':'调整','take':'取得','attach':'附加'}
    nouns={'Handle':'资源句柄','Value':'对象值','Size':'容量','Id':'编号','Valid':'对象状态','Opened':'连接状态',
           'Owned':'资源持有状态','Error':'错误码','Name':'名称','Path':'路径','Count':'数量','Type':'类型',
           'Attributes':'属性','Flags':'标志','Address':'地址','Offset':'位置','Mapped':'映射状态','State':'状态',
           'Info':'属性信息','References':'引用数量','Generation':'状态代号','Command':'命令','Result':'操作结果'}
    for prefix,verb in verbs.items():
        if simple.startswith(prefix):
            rest=simple[len(prefix):]
            if rest in nouns: return verb+nouns[rest]
    fallbacks.add(name)
    return simple

def number(expression, values):
    expression=re.sub(r'\b(0[xX][0-9A-Fa-f]+|\d+)(?:ULL|LLU|LL|UL|LU|U|L)\b',r'\1',expression)
    try:
        tree=ast.parse(expression,mode='eval')
        if any(not isinstance(node,(ast.Expression,ast.Constant,ast.Name,ast.Load,ast.BinOp,ast.UnaryOp,
                                    ast.Add,ast.Sub,ast.Mult,ast.Div,ast.FloorDiv,ast.LShift,ast.RShift,
                                    ast.BitOr,ast.BitAnd,ast.BitXor,ast.USub,ast.UAdd,ast.Invert)) for node in ast.walk(tree)):
            return None
        return int(eval(compile(tree,'<constant>','eval'),{'__builtins__':{}},values))
    except (SyntaxError,NameError,TypeError,ValueError,ZeroDivisionError): return None

parameter_meanings = dict(line.split('=', 1) for line in '''backend=文件访问后端
device=设备对象或设备编号
program=接收程序段信息的对象
entry=目录项或线程入口
entries=接收目录项的数组
event=接收输入事件的对象
file=文件对象
handle=资源句柄
functions=接收 PCI 功能信息的数组
endpoint=消息端点句柄
message=消息内容
record=记录数据
reply=接收回复消息的对象
request=待发送的请求消息
snapshot=检查点快照消息
key=按键编号
kind=服务类型
mount=挂载对象
operation=文件操作对象
profile=接收账户资料的对象
status=操作状态
policy=核心转储策略
scope=目录所属范围
memory=共享内存对象
action=信号处理设置
stack=线程栈或备用信号栈
lock=作用域内持有的自旋锁
mutex=作用域内持有的互斥锁
number=系统调用编号
bright=使用高亮颜色
duplex=启用双向管道
enabled=功能开关
levelTriggered=使用电平触发中断
logRecord=将记录写入系统日志
owned=由对象管理句柄的释放
shared=使用共享映射
transfer=移交句柄所有权
arguments=程序启动参数数组
argument=传给线程入口的上下文
environment=进程环境变量数组
reader=读取当前工作目录的回调函数
application=应用名称
destination=接收复制结果的缓冲区
existing=现有文件或目录的路径
left=参与比较的左侧字符串
right=参与比较的右侧字符串
name=对象名称
password=认证密码
path=文件或目录路径
replacement=新密码
source=待复制的数据
string=待输出的字符串
target=目标路径
type=操作或对象类型
times=文件访问时间与修改时间
groups=附加用户组编号数组
address=内存地址
data=数据缓冲区
image=程序映像或执行选项
state=状态数据
gid=用户组编号
uid=用户编号
argumentCount=程序启动参数数量
exitCode=进程或线程退出码
flags=操作标志位
how=信号屏蔽集合的修改方式
mode=访问方式或权限位
noHang=启用立即查询退出状态
options=等待选项
resource=资源限制类型
whence=偏移量的计算起点
writable=允许写入
offset=数据起始偏移量
capacity=接收缓冲区容量
size=数据大小，单位为字节
stackSize=线程栈大小，单位为字节
count=项目数量
node=NUMA 节点编号
port=硬件端口编号
segment=PCI 段号
effective=有效身份编号
expected=等待地址的预期值
id=目标编号
index=项目索引
major=主设备号
minor=次设备号
maximumMessages=队列可容纳的消息数量
messageSize=单条消息的最大字节数
minimumId=枚举的起始编号
order=内存页分配阶数
priority=消息优先级
protection=内存访问权限
real=真实身份编号
rights=句柄访问权限
saved=保存的身份编号
signal=信号编号
taskId=进程编号
width=端口访问宽度，单位为字节
completed=接收实际完成的磁盘块数量
accessTime=文件访问时间
alignment=地址对齐字节数
bytes=数据字节数
completionEndpoint=异步操作完成通知端点
deadline=等待截止时间，单位为单调时钟纳秒
error=错误码
firstBlock=起始磁盘块编号
fs=文件系统服务端点
generation=检查点或就绪状态的版本号
group=进程组编号
hard=资源硬限制
input=标准输入句柄
length=数据长度
mask=信号集合
master=伪终端主端句柄
milliseconds=等待时长，单位为毫秒
modificationTime=文件修改时间
mountId=挂载编号
output=标准输出句柄
owner=所属进程编号
registry=服务注册中心端点
replyToken=请求回复令牌
requestId=异步请求编号
schema=检查点数据格式编号
sequence=记录序号
service=服务端点
slave=伪终端从端句柄
soft=资源软限制
stackPointer=线程栈顶地址
task=目标进程
threadId=线程编号
token=请求或资源令牌
readHandle=管道读取端句柄
writeHandle=管道写入端句柄
cursor=枚举位置
sessionTask=接收登录会话进程编号
nanoseconds=接收时钟读数，单位为纳秒
groupId=接收进程组编号
parentId=接收父进程编号
sessionId=接收会话编号
previous=接收修改前的设置
bar=PCI BAR 寄存器编号
bus=PCI 总线号
function=PCI 功能号
irq=硬件中断号
frame=显示缓冲区地址
pointer=待释放的内存地址
other=提供资源或属性的原对象
entryPoint=程序入口地址
out=接收解码后的文件属性
r=服务回复消息
a=请求的第一个数据字段
b=请求的第二个数据字段
c=请求的第三个数据字段
d=请求的第四个数据字段
terminal=目标终端编号
started=服务启动状态或启动时间
booting=接收系统启动状态
preferredTerminal=接收首选终端编号
text=待检查的文本
write=执行端口写入
base=基础目录路径
configuration=查找应用配置目录
active=图形界面活动状态
windowService=窗口服务端点
writing=执行数据写入
from=原路径
to=目标路径
window=窗口服务端点
phase=接收图形会话阶段
controller=接收会话控制端点
success=消息发送结果
identity=内存对象标识
inputSize=输入数据字节数
outputSize=输出缓冲区字节数
first=起始块编号或请求的第一个字段
second=请求的第二个数据字段
op=服务操作编号
total=所需总字节数
append=追加检查点记录
attempts=服务查询尝试次数
access=服务访问权限
images=共享映像数组
thread=线程编号
code=进程或线程退出码
set=执行限制值设置
standardErrorHandle=标准错误句柄
standardInputHandle=标准输入句柄
standardOutputHandle=标准输出句柄
identifiers=接收进程、进程组和会话编号
taskHandle=进程句柄
minimum=枚举的起始进程编号
headers=程序头表地址
loader=动态加载器地址
begin=映像起始地址
end=映像结束地址
effectiveOnly=更新有效身份
taskGroupId=进程组编号
oldAction=接收原信号处理设置
oldMask=接收原信号屏蔽集合
oldStack=接收原备用信号栈
bootstrapHandle=进程启动端点
resultReply=接收服务回复的消息
descriptor=文件描述符或文件服务句柄
lazy=使用按需分配的内存映射
hint=期望的映射地址
contiguous=接收连续可用的字节数
available=接收可用字节数
preserveLocks=保留地址范围的锁定状态
info=接收或提供操作所需的对象信息
requester=发起请求的进程编号
minimumTid=线程枚举的起始编号
aligned=接收按页对齐后的大小
storage=存放线程启动对象的内存
argv=程序启动参数数组
argc=程序启动参数数量
preserveCredentials=保留进程当前身份
groupCount=附加用户组数量
line=配置文件行号
unit=服务管理记录
runtime=检查运行阶段依赖
force=要求终止目标进程
checkpoint=服务检查点对象
capabilities=恢复令牌管理对象
slot=缓存记录位置
caller=发起启动请求的进程信息
session=认证会话编号'''.splitlines())

def parameters(signature):
    opening = signature.find('(')
    if opening < 0: return []
    depth, end = 1, opening + 1
    while end < len(signature) and depth:
        depth += (signature[end] == '(') - (signature[end] == ')')
        end += 1
    text = signature[opening+1:end-1]
    output, begin, depth = [], 0, 0
    for index, char in enumerate(text):
        if char in '(<[': depth += 1
        elif char in ')>]': depth -= 1
        elif char == ',' and depth == 0:
            output.append(text[begin:index].strip()); begin = index + 1
    output.append(text[begin:].strip())
    return [item for item in output if item and item != 'void']

parameter_missing = set()
parameter_overrides = {
    ('os::Console::EscapeParser::feed', 'value'): '待解析的输入字节',
    ('os::Console::Style::apply', 'value'): '编码颜色和文字属性的样式值',
    ('os::Console::terminalColor', 'index'): '终端颜色索引',
    ('os::Console::EscapeParser::getParameter', 'index'): '转义序列参数的索引',
    ('CapiEncoding::rawOutput', 'value'): '系统调用的原始返回值',
    ('CapiEncoding::rawOutput', 'result'): '接收成功时返回的数值',
    ('osTaskExecute', 'error'): '标准错误句柄',
    ('os::Runtime::stringLength', 'value'): '待计算长度的字符串',
    ('os::alignment', 'value'): '待检查对齐量的地址或大小',
    ('os::Sys::Checkpoint::queryGeneration', 'value'): '接收检查点版本号',
    ('services::Lifecycle::validName', 'value'): '待检查的服务名称',
}

def parameter_cell(function, signature):
    output = []
    simple = function.split('::')[-1]
    for declaration in parameters(signature):
        declaration = declaration.split('=', 1)[0].strip()
        callback = re.search(r'\(\s*\*\s*(\w+)\s*\)', declaration)
        names = re.findall(r'\b\w+\b', re.sub(r'\[[^]]*\]', '', declaration))
        name = callback[1] if callback else names[-1]
        if declaration.endswith('&&') or declaration.endswith('&'):
            name = 'other'
        meaning = parameter_meanings.get(name)
        if name == 'buffer':
            meaning = '待写入的数据缓冲区' if 'const ' in declaration else '接收读取结果的缓冲区'
        elif name == 'result':
            meaning = '接收' + describe(function).removeprefix('读取').removeprefix('查询') + '结果'
        elif name == 'value':
            if simple.startswith('set'):
                meaning = describe(function).removeprefix('设置')
            elif 'operator' in simple: meaning = '用于赋值或比较的值'
            elif 'Handle::' in function: meaning = '内核资源句柄值'
            elif 'Pci' in function: meaning = 'PCI 配置寄存器数值'
            elif 'Nice' in simple: meaning = '调度优先级数值'
            elif simple.lower().endswith('error'): meaning = '待转换的服务错误码'
            else: meaning = None
        elif re.fullmatch(r'arg[1-6]', name): meaning = '第' + name[-1] + '个系统调用参数'
        if callback:
            meaning = {'entry':'线程入口函数', 'function':'程序退出时执行的函数',
                       'reader':'读取当前工作目录的回调函数'}.get(name, meaning)
        if name in {'input', 'output'} and '*' in declaration:
            meaning = '待发送的数据缓冲区' if name == 'input' else '接收数据的缓冲区'
        if name == 'device':
            if 'char' in declaration: meaning = '块设备路径'
            elif 'uint8_t' in declaration: meaning = 'PCI 设备号'
            elif 'Storage' in function: meaning = '接收块设备连接的对象'
            else: meaning = '设备连接对象'
        if name == 'function' and 'Pci&' in declaration: meaning = 'PCI 功能对象'
        if name == 'type' and ('format' in simple.lower()): meaning = '文件系统类型名称'
        if name == 'id':
            if 'Console' in function or 'setConsoleId' == simple: meaning = '终端编号'
            elif 'Storage' in function: meaning = '块设备编号'
            elif 'Thread' in function: meaning = '线程编号'
            elif 'SessionCreate' in function: meaning = '接收新建会话编号'
            elif 'Task' in function: meaning = '进程编号'
            elif 'Device' in function: meaning = '设备编号'
            if '*' in declaration and meaning and not meaning.startswith('接收'): meaning = '接收' + meaning
        if name == 'count':
            if 'Storage' in function: meaning = '本次传输的磁盘块数量'
            elif 'Group' in simple: meaning = '附加用户组数量'
            elif 'wake' in simple.lower(): meaning = '最多唤醒的线程数量'
            elif 'Port' in simple or simple == 'portIo': meaning = '端口读写次数'
            elif 'Dir' in function: meaning = '接收实际读取的目录项数量'
            elif 'SharedImages' in simple: meaning = '共享映像数量'
            elif 'Arguments' in simple: meaning = '程序启动参数数量'
        if name == 'capacity' and 'Dir' in function: meaning = '目录项数组可容纳的项目数'
        if name == 'endpoint' and 'Ipc&' in declaration: meaning = '接收中断通知的消息端点对象'
        if name == 'result':
            result_types = {'OsFileStat':'文件属性', 'OsDirectoryEntry':'目录项',
                            'OsFileOperationResult':'异步文件操作结果', 'OsTaskInfo':'进程属性',
                            'OsCredentialInfo':'进程身份', 'OsMountStat':'挂载属性',
                            'OsSignalAction':'信号处理设置', 'OsSignalStack':'备用信号栈设置',
                            'OsMessageQueueAttributes':'消息队列属性', 'Statistics':'块设备统计',
                            'Pci':'PCI 功能信息', 'IpcMessage':'服务回复消息', 'Handle':'资源句柄'}
            for typename, text in result_types.items():
                if typename in declaration:
                    meaning = '接收' + text
                    break
        meaning = parameter_overrides.get((function, name), meaning)
        if not meaning:
            parameter_missing.add((function, declaration))
            meaning = name
        output.append(f'`{name}`：{meaning}')
    return '<br>'.join(output) if output else '—'

source_text = {
 'capi.cc':'C 接口及对象访问入口。','console.cc':'控制台输入输出、转义序列和文字样式。',
 'device.cc':'设备连接、PCI 配置、DMA、寄存器映射、显示和中断资源。','dir.cc':'目录访问和约定目录解析。',
 'elf.cc':'ELF 映像检查和加载信息读取。','file.cc':'文件访问、文件系统操作和挂载管理。',
 'input.cc':'键盘与鼠标事件读取。','ipc.cc':'句柄、消息、端点、管道和消息队列。',
 'mm.cc':'进程堆、内存对象和地址映射。','pty.cc':'伪终端连接及终端设置。',
 'service.cc':'系统信息、时间、日志和核心服务通信。','storage.cc':'块设备枚举、分区属性、块传输和统计。',
 'sync.cc':'自旋锁、互斥锁和地址等待。','syscall.cc':'系统调用桥接及就绪通知。',
 'task.cc':'进程、线程、身份、信号和执行映像。','user.cc':'用户账户、认证和登录会话。',
 'service/boot.cc':'启动终端报告及图形显示交接。','service/checkpoint.cc':'服务检查点数据与资源令牌。',
 'service/configuration.cc':'配置文本对象的构造与格式检查。','service/connection.cc':'服务连接的建立与消息调用。',
 'service/control.cc':'服务管理操作和服务状态查询。','service/recovery.cc':'服务恢复数据与恢复状态。',
 'service/runtime.cc':'进程运行环境、环境变量、终端选择和退出处理。','service/session.cc':'进程组、会话和控制终端。'
 ,'service/lifecycle.cc':'服务启动、依赖、监护和配置管理。'
 ,'service/recovery-cache.cc':'恢复令牌与已完成请求的缓存。'
 ,'service/identity.cc':'登录会话身份查询与设置。'
 ,'service/session_cleanup.cc':'认证会话所属用户进程的退出清理。'
}

def source_functions(path):
    text=path.read_text(); code,nodes=spans(text); output=[]
    pattern=r'^[ \t]*(?:(?:[\w:<>,*&]+)[ \t]+)*((?:\w+::)*(?:~?\w+|operator\s*(?:=|bool)))\s*\('
    for match in re.finditer(pattern,code,re.M):
        name=match[1]
        if code[match.start():match.start(1)].lstrip().startswith((':', ',')): continue
        if name in {'if','for','while','switch','catch','static_assert','service_','registry_'}: continue
        opening=code.index('(',match.start()); depth=1; end=opening+1
        while end<len(code) and depth:
            if code[end]=='(': depth+=1
            elif code[end]==')': depth-=1
            end+=1
        if not re.match(r'\s*(?:const\s*)?(?:noexcept\s*)?(?:override\s*)?(?:\{|:|=\s*default)',code[end:]): continue
        owner=owner_at(nodes,match.start()); prefix=owner['full']+'::' if owner else ''
        if name.startswith('os::'): prefix=''
        full=(prefix+name).replace('::::', '::')
        signature = short(text[match.start():end])
        if (full, signature) not in output: output.append((full, signature))
    return output

source_inventory = {path.relative_to(root).as_posix():source_functions(path) for path in sorted((root/'lib/os').rglob('*.cc'))}
for module in modules.values():
    for name,signature in module['functions']:
        describe(name); parameter_cell(name, signature)
    for members in module['members'].values():
        for name, signature, category in members:
            describe(name); parameter_cell(name, signature)
for functions in source_inventory.values():
    for name, signature in functions:
        describe(name); parameter_cell(name, signature)
(work/'os-reference-source-functions.json').write_text(json.dumps(source_inventory,ensure_ascii=False,indent=2))
if args.inspect:
    print('PURPOSES TO COMPLETE:')
    print('\n'.join(sorted(fallbacks)))
    print('PARAMETERS TO COMPLETE:')
    for entry in sorted(parameter_missing): print(entry)
    raise SystemExit(0)

assert not parameter_missing, sorted(parameter_missing)

for key,module in modules.items():
    lines=['# '+titles[key]]
    values={'UINT64_MAX':2**64-1,'UINT32_MAX':2**32-1}
    for name,expression in module['constants']:
        value=number(expression,values)
        if value is not None: values[name.split('::')[-1]]=value
    if module['enums']:
        lines+=['','## 枚举']
        for name,entries in module['enums']:
            lines+=['','### '+name,'','| 成员 | 值 | 说明 |','| --- | --- | --- |']
            previous=-1
            for member,expression in entries:
                value=number(expression,values) if expression else previous+1
                assert value is not None,(key,member,expression)
                previous=value; values[member]=value
                meaning=meanings.get(member) or seed['values'].get(member) or member
                lines.append(f'| `{member}` | `{value}` | {meaning} |')
    if module['constants']:
        lines+=['','## 常量','','| 名称 | 值 | 说明 |','| --- | --- | --- |']
        for name,expression in module['constants']:
            meaning=meanings.get(name) or seed['values'].get(name.split('::')[-1]) or name
            lines.append(f'| `{name}` | `{short(expression)}` | {meaning} |')
    if module['classes']:
        lines+=['','## 对象']
        for name in module['classes']:
            meaning=class_text[name.removeprefix('os::')]
            lines+=['','### '+name,'',meaning]
            for category in ('构造与析构', '成员函数', 'Getter', 'Setter', '静态成员函数'):
                entries = [entry for entry in module['members'][name] if entry[2] == category]
                if not entries: continue
                lines+=['','#### '+category,'','| 函数 | 用途 | 参数 |','| --- | --- | --- |']
                for function, signature, _ in entries:
                    lines.append(f'| `{signature}` | {describe(function)} | {parameter_cell(function, signature)} |')
    if module['functions']:
        lines+=['','## 函数']
        groups = {}
        for name, signature in module['functions']:
            group = ''
            if key == 'capi':
                group = next(label for prefix, label in c_subjects.items() if name.startswith('os'+prefix))
            groups.setdefault(group, []).append((name, signature))
        for group, functions in groups.items():
            if group: lines+=['','### '+group]
            lines+=['','| 函数 | 用途 | 参数 |','| --- | --- | --- |']
            for name, signature in functions:
                lines.append(f'| `{signature}` | {describe(name)} | {parameter_cell(name, signature)} |')
    text='\n'.join(lines)+'\n'
    assert not forbidden.search(text),(key,forbidden.search(text)[0])
    (docs/(key+'.md')).write_text(text)

index=['# OS 库','','## 对象','','| 类 | 说明 |','| --- | --- |']
for key,module in modules.items():
    for name in module['classes']:
        if name.count('::')==1:
            index.append(f'| [`{name}`]({key}.md) | {class_text[name.removeprefix("os::")]} |')
(docs/'libosc++.md').write_text('\n'.join(index)+'\n')
lines=['# OS 库源文件']
for file,functions in source_inventory.items():
    relative=file.removeprefix('lib/os/')
    lines+=['','## '+file,'',source_text[relative],'','| 函数 | 用途 | 参数 |','| --- | --- | --- |']
    for name, signature in functions:
        lines.append(f'| `{name}` | {describe(name)} | {parameter_cell(name, signature)} |')
text='\n'.join(lines)+'\n'
assert not forbidden.search(text),forbidden.search(text)[0]
(docs/'sources.md').write_text(text)
assert not fallbacks,sorted(fallbacks)
print('Wrote',len(modules)+2,'OS reference documents')
