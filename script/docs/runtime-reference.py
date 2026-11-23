"""整理 libc 和 libc++ 自有运行时接口。"""
from pathlib import Path
import importlib.util
import re
import sys

root = Path(__file__).resolve().parents[2]
inspect = '--inspect' in sys.argv
sys.argv = [sys.argv[0]]
spec = importlib.util.spec_from_file_location('os_reference', Path(__file__).with_name('os-reference.py'))
api = importlib.util.module_from_spec(spec)
spec.loader.exec_module(api)

purposes = dict(line.split('=', 1) for line in '''libcFsService=取得文件系统服务连接
libcCopyPath=复制文件路径
libcDescriptorSlot=取得文件描述符表项
libcDescriptor=取得打开文件描述
libcAllocateDescriptor=登记打开文件并分配描述符
libcReleaseDescriptor=释放文件描述符
libcPinDescriptor=保留描述符引用供就绪查询使用
libcPinnedDescriptorValid=检查保留引用对应的描述符版本
libcUnpinDescriptor=释放就绪查询保留的引用
libcReplaceDescriptor=以已有描述符替换目标描述符
libcCloseAllDescriptors=关闭进程的全部文件描述符
libcDuplicateDescriptor=复制文件描述符
libcLockFileIo=取得文件描述符表的锁
libcUnlockFileIo=释放文件描述符表的锁
libcSetError=设置当前线程错误码
libcSetCurrentDirectory=设置进程工作目录
libcGetCurrentDirectory=读取进程工作目录
libcPathAt=按目录描述符解析路径
libcCreationMode=应用进程文件创建权限掩码
libcRegisterChild=登记子进程句柄
libcAfterForkChild=设置派生后的子进程运行库状态
libcExportExecState=保存程序替换时继承的进程状态
__libc_import_exec_state=恢复程序替换时继承的进程状态
libcWaitLock=等待并取得运行库锁
libcWaitUnlock=释放运行库锁并唤醒等待线程
libcDeadlineWait=按指定时钟的绝对期限等待地址变化
__libc_stream_state=取得文件流内部状态
__libc_stream_mode=将流打开方式转换为文件标志
__libc_stream_flush=写出文件流缓冲数据
__libc_getc_unlocked=读取一个字节
__libc_putc_unlocked=写入一个字节
__libc_stream_write_wchar=向文件流写入宽字符
__libc_popen_descriptors=收集管道流持有的文件描述符
__libc_wide_string_stream=创建宽字符串文件流
__cxa_atexit=登记对象析构函数
__cxa_finalize=执行已登记的对象析构函数
__cxa_deleted_virtual=处理已删除虚函数调用
__cxa_pure_virtual=处理纯虚函数调用'''.splitlines())
extra_parameters = dict(line.split('=', 1) for line in '''fileDescriptor=文件描述符
descriptor=文件描述符或文件服务句柄
pin=就绪查询保留的描述符引用
minimum=分配新描述符的最小编号
closeOnExec=执行新程序时关闭描述符
directory=解析相对路径的目录描述符
absolute=接收绝对路径的缓冲区
object=运行库持有的文件对象
destructor=对象析构回调函数
dso=动态库标识
clock=计算等待期限的时钟
limit=绝对等待期限
stream=文件流对象
descriptors=接收文件描述符的数组
child=子进程编号
pid=目标进程编号
parentTaskId=父进程编号
oldDescriptors=继承前的文件句柄数组
newDescriptors=接收复制后的文件句柄数组
newPath=目标文件路径
operation=操作编号
third=请求的第三个数据字段
which=优先级目标类型
who=优先级目标编号
attributes=接收消息队列属性的对象
oldFlags=接收原有标志位
noHang=启用立即查询退出状态
effectiveOnly=修改有效身份
argumentCount=程序启动参数数量
standardInput=标准输入句柄
standardOutput=标准输出句柄
standardError=标准错误句柄
terminalId=终端编号
completionEndpoint=异步完成通知端点
requestId=异步请求编号
oldAction=接收原信号处理方式
oldMask=接收原信号屏蔽集合
oldStack=接收原备用信号栈
exitCode=接收退出码
handles=接收管道读取端和写入端句柄'''.splitlines())

def argument_cell(function, signature):
    result = []
    for item in api.parameters(signature):
        declaration = item.split('=', 1)[0].strip()
        callback = re.search(r'\(\s*\*\s*(\w+)\s*\)', declaration)
        names = re.findall(r'\b\w+\b', re.sub(r'\[[^]]*\]', '', declaration))
        name = callback[1] if callback else names[-1]
        camel = re.sub(r'_([a-z])', lambda match: match[1].upper(), name)
        meaning = extra_parameters.get(camel) or api.parameter_meanings.get(camel)
        if name == 'value':
            meaning = '写入的字节或字符' if 'putc' in function or 'wchar' in function else '待设置的数值'
        if name == 'result': meaning = '接收操作结果的对象或缓冲区'
        if name == 'source': meaning = '被复制的文件描述符' if 'Descriptor' in function else '待复制的数据'
        if name == 'destination': meaning = '接收替换的文件描述符' if 'Descriptor' in function else '接收复制结果的缓冲区'
        if name == 'buffer': meaning = '待写入的数据' if 'const ' in declaration else '接收读取数据的缓冲区'
        if name == 'count': meaning = '传输字节数' if 'file_' in function else '项目数量'
        if name == 'state': meaning = '就绪状态记录' if 'readiness' in function else '进程继承状态' if 'exec' in function else '文件流内部状态'
        if name == 'input': meaning = '使用读取方向' if 'int ' in declaration else '输入数据'
        if name == 'thread': meaning = '查询线程的处理器时间'
        if name == 'nanoseconds': meaning = '时钟时间，单位为纳秒'
        if name == 'set': meaning = '使用设置操作'
        if name == 'flags': meaning = '文件打开标志' if 'file' in function.lower() or 'Descriptor' in function else '操作标志'
        if name == 'length': meaning = '内存或文件长度'
        if not meaning: missing_parameters.add((function, declaration)); meaning = name
        result.append('`'+name+'`：'+meaning)
    return '<br>'.join(result) if result else '—'

def declarations(path):
    text = path.read_text(encoding='utf-8')
    code = api.masked(text)
    pattern = r'^[ \t]*(?:[\w*]+[ \t]+)+?(\w+)\s*\([^;{}]*\)\s*(?:__attribute__\s*\(\([^;]*?\)\)\s*)?;'
    output = []
    for match in re.finditer(pattern, code, re.M):
        signature = api.short(text[match.start():match.end()].strip().removesuffix(';'))
        output.append((match[1], signature))
    return output

def group(name):
    if name.startswith('libc') and name not in {'libcWaitLock','libcWaitUnlock','libcDeadlineWait'}:
        return '文件描述符与进程继承'
    if name.startswith('__libc_stream') or name in {'__libc_getc_unlocked', '__libc_putc_unlocked', '__libc_popen_descriptors', '__libc_wide_string_stream'}:
        return '文件流'
    if any(word in name for word in ('readiness', 'wait_address', 'wake_address', 'WaitLock', 'WaitUnlock', 'DeadlineWait')): return '同步与就绪等待'
    if any(word in name for word in ('memory', '_mm_', '_typed_', 'accessible')): return '内存'
    if any(word in name for word in ('message_queue', '_pipe', '_endpoint', 'close_handle')): return '进程通信'
    if any(word in name for word in ('terminal', 'standard_handle')): return '终端'
    if any(word in name for word in ('time', 'clock', 'cpu_time')) and 'runtime' not in name: return '时间'
    if any(word in name for word in ('file', 'directory', '_path')): return '文件与目录'
    if any(word in name for word in ('task', 'thread', 'signal', 'fork', 'child', 'credentials', 'priority', 'session', 'resource_limit', 'core_limit', 'wait_id')): return '进程、线程与身份'
    return '运行环境'

missing_parameters = set()
entries = []
for filename in ('internal.h', 'stdio_internal.h'):
    entries.extend((name, signature) for name, signature in declarations(root/'lib/libc'/filename)
                   if not name.startswith(('__libc_os_', '__libc_file_')))
entries.extend([
    ('libcWaitLock', 'void libcWaitLock(volatile uint32_t* lock)'),
    ('libcWaitUnlock', 'void libcWaitUnlock(volatile uint32_t* lock)'),
    ('libcDeadlineWait', 'int libcDeadlineWait(volatile uint32_t* address, uint32_t expected, clockid_t clock, const struct timespec* limit)'),
])
for name, signature in entries: argument_cell(name, signature)
missing_purposes = sorted({name for name, _ in entries if name not in purposes})
if inspect:
    print('PURPOSES:', missing_purposes)
    print('PARAMETERS:', sorted(missing_parameters))
    raise SystemExit(0)
assert not missing_purposes, missing_purposes
assert not missing_parameters, sorted(missing_parameters)

lines = ['# libc', '', '## 枚举', '', '### DescriptorKind', '', '| 成员 | 值 | 说明 |', '| --- | --- | --- |']
for value, (name, meaning) in enumerate([
    ('DESCRIPTOR_FILE','文件'), ('DESCRIPTOR_PIPE_READ','管道读取端'), ('DESCRIPTOR_PIPE_WRITE','管道写入端'),
    ('DESCRIPTOR_TERMINAL_READ','终端读取端'), ('DESCRIPTOR_TERMINAL_WRITE','终端写入端'),
    ('DESCRIPTOR_SHARED_MEMORY','共享内存'), ('DESCRIPTOR_TYPED_MEMORY','指定类型的内存')]):
    lines.append(f'| `{name}` | `{value}` | {meaning} |')
lines += ['', '## 常量', '', '| 名称 | 值 | 说明 |', '| --- | --- | --- |',
          '| `LIBC_DESCRIPTOR_CAPACITY` | `32` | 进程文件描述符数量 |',
          '| `LIBC_PATH_CAPACITY` | `384` | 路径缓冲区字节数 |',
          '| `LIBC_EXEC_STATE_MAGIC` | `0x4f53455845435354` | 程序继承状态标记 |',
          '', '## 全局变量', '', '| 名称 | 说明 |', '| --- | --- |',
          '| `libcDescriptors` | 当前进程的文件描述符表 |', '', '## 对象', '', '| 名称 | 说明 |', '| --- | --- |']
for name, meaning in [
    ('OpenDescription','打开文件描述，保存文件对象、偏移、标志和引用数量。'),
    ('FileDescriptor','文件描述符记录，保存打开文件描述的引用与继承设置。'),
    ('LibcPollDescriptor','就绪查询引用，保存描述符对象与版本。'),
    ('LibcExecDescriptorState','文件描述符继承记录，保存程序替换前的打开状态。'),
    ('LibcExecState','进程继承状态，保存描述符表、工作目录和终端编号。'),
    ('FileState','文件流状态，保存缓冲区、读写位置、锁和内存流数据。')]:
    lines.append(f'| `{name}` | {meaning} |')
lines += ['', '## 函数']
groups = {}
for entry in entries: groups.setdefault(group(entry[0]), []).append(entry)
for title, functions in groups.items():
    lines += ['', '### '+title, '', '| 函数 | 用途 | 参数 |', '| --- | --- | --- |']
    for name, signature in functions:
        lines.append(f'| `{signature}` | {purposes[name]} | {argument_cell(name, signature)} |')
text = '\n'.join(lines)+'\n'
assert not api.forbidden.search(text)
(root/'docs/lib/libc/libc.md').write_text(text, encoding='utf-8')

lines = ['# libc++ 运行时', '', '## 常量', '', '| 名称 | 值 | 说明 |', '| --- | --- | --- |',
         '| `destructorCapacity` | `32` | 对象析构记录数量 |', '', '## 全局变量', '', '| 名称 | 说明 |', '| --- | --- |',
         '| `__dso_handle` | 当前动态库的对象析构标识 |', '', '## 对象', '', '| 名称 | 说明 |', '| --- | --- |',
         '| `DestructorEntry` | 析构记录，保存析构函数与关联对象。 |', '', '## 函数', '', '### 对象生命周期', '',
         '| 函数 | 用途 | 参数 |', '| --- | --- | --- |']
for name, signature in [
    ('__cxa_atexit', 'int __cxa_atexit(void (*destructor)(void*), void* object, void* dso)'),
    ('__cxa_finalize', 'void __cxa_finalize(void* dso)'),
    ('__cxa_deleted_virtual', 'void __cxa_deleted_virtual()'),
    ('__cxa_pure_virtual', 'void __cxa_pure_virtual()')]:
    lines.append(f'| `{signature}` | {purposes[name]} | {argument_cell(name, signature)} |')
(root/'docs/lib/libc++/libc++.md').write_text('\n'.join(lines)+'\n', encoding='utf-8')
print('Wrote libc and libc++ runtime references:', len(entries), 'libc functions')

source_lines = ['# libc 源文件']
for path in sorted((root/'lib/libc').glob('*')):
    if path.suffix not in {'.c', '.cc'}: continue
    code = api.masked(path.read_text(encoding='utf-8'))
    functions = [(name, signature) for name, signature in entries
                 if re.search(r'^[ \t]*(?:[\w*]+[ \t]+)+'+re.escape(name)+r'\s*\([^;{}]*\)\s*\{', code, re.M)]
    if not functions: continue
    source_lines += ['', '## '+path.relative_to(root).as_posix(), '', '| 函数 | 用途 | 参数 |', '| --- | --- | --- |']
    for name, signature in functions:
        source_lines.append(f'| `{signature}` | {purposes[name]} | {argument_cell(name, signature)} |')
(root/'docs/lib/libc/sources.md').write_text('\n'.join(source_lines)+'\n', encoding='utf-8')

lines = ['# libc++ 运行时源文件', '', '| 文件 | 功能 | 函数 |', '| --- | --- | --- |',
         '| `lib/libc++/abi.cc` | 对象析构登记与运行时调用入口。 | `__cxa_atexit`：登记析构函数；`__cxa_finalize`：执行析构函数；`__cxa_deleted_virtual`：处理已删除虚函数调用。 |',
         '| `lib/libc++/new.cc` | 分配、释放对象内存及虚函数调用入口。 | `__cxa_pure_virtual`：处理纯虚函数调用。 |']
(root/'docs/lib/libc++/sources.md').write_text('\n'.join(lines)+'\n', encoding='utf-8')

fallback_modules = {'aio':'异步文件操作', 'arpa_inet':'网络地址转换', 'dlfcn':'动态库访问', 'fmtmsg':'诊断消息',
                    'grp':'用户组信息', 'netdb':'网络主机与服务查询', 'pthread':'线程接口',
                    'pwd':'用户账户信息', 'sched':'调度接口', 'signal':'信号接口', 'stdio':'文件流',
                    'stdlib':'程序运行与数值转换', 'sys_mman':'内存映射', 'sys_socket':'套接字',
                    'sys_stat':'文件属性', 'sys_time':'时间接口', 'sys_wait':'子进程等待',
                    'termios':'终端属性', 'unistd':'进程与文件操作', 'utmpx':'登录记录'}
lines = ['# libc 接口入口文件', '', '| 文件 | 模块 |', '| --- | --- |']
for path in sorted((root/'lib/libc/fallback').glob('*.c')):
    includes = re.findall(r'#include\s*<([^>]+)>', path.read_text(encoding='utf-8'))
    module = fallback_modules.get(path.stem) or '、'.join(includes[:1])
    lines.append(f'| `lib/libc/fallback/{path.name}` | {module} |')
(root/'docs/lib/libc/fallback.md').write_text('\n'.join(lines)+'\n', encoding='utf-8')
