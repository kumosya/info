"""遍历工作区自有头文件，检查独立包含与重复包含；不扫描标准库、OS 库或生成副本。"""
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path
import json
import os
import re
import subprocess
import tempfile

root = Path(__file__).resolve().parents[2]
workspace = root.parent
output = root / "build/header-audit"
output.mkdir(parents=True, exist_ok=True)
excluded = {".git", ".agents", ".codex", ".annotation-work", ".work-notes",
            "build", "obj", "packages", "resources", "vendor", "third_party",
            "stdc", "stdc++", "posix", "os-c++", "osgui-c++", "libc", "libc++"}
headers = []
for folder, directories, files in os.walk(workspace):
    directories[:] = [name for name in directories if name not in excluded]
    for name in files:
        if Path(name).suffix in {".h", ".hpp", ".hxx"}:
            headers.append(Path(folder) / name)
headers.sort()

includes = [root / "include", root / "include/stdc", root / "include/stdc++",
            root / "include/posix", root / "include/os-c++", root / "build/generated",
            workspace / "guiw/include", workspace / "guiw/lib/osgui-c++/include",
            workspace / "guiw/lib/guiw-font/include", workspace / "guiw/lib/guiw-login/include"]
base = ["g++", "-std=c++20", "-fsyntax-only", "-fno-exceptions", "-fno-rtti"]

def definitions(text):
    # 仅用于生成待人工复核的清单，不把正则匹配结果当作 C++ 语义结论。
    masked = re.sub(r'//[^\n]*|/\*[\s\S]*?\*/|"(?:\\.|[^"\\])*"|\'(?:\\.|[^\'\\])*\'',
                    lambda match: "".join("\n" if char == "\n" else " " for char in match[0]), text)
    stack = []
    ends = {}
    for position, char in enumerate(masked):
        if char == "{":
            stack.append(position)
        elif char == "}" and stack:
            ends[stack.pop()] = position
    classes = []
    for match in re.finditer(r'\b(?:class|struct)\s+(\w+)[^;{}]*\{', masked):
        begin = masked.index("{", match.start())
        if begin in ends:
            classes.append((begin, ends[begin], match[1]))
    found = []
    pattern = re.compile(r'^[ \t]*(?:[\w:<>,*&]+[ \t]+)+([\w~]+)\([^;{}]*?\)\s*(?:const\s*)?(?:override\s*)?(?:noexcept\s*)?\{', re.M)
    for match in pattern.finditer(masked):
        if match[1] in {"if", "for", "while", "switch", "catch"}:
            continue
        begin = masked.index("{", match.start())
        if begin not in ends:
            continue
        end = ends[begin]
        owners = [(last-first, name) for first, last, name in classes if first < begin < last]
        found.append({"name": match[1], "line": masked.count("\n", 0, match.start()) + 1,
                      "body_lines": masked[begin:end].count("\n"),
                      "owner": min(owners)[1] if owners else None})
    return found

def inspect(path):
    relative = path.relative_to(workspace).as_posix()
    host = relative.startswith(("info/tools/host/", "infoide/", "inpkg/"))
    flags = [] if host else ["-ffreestanding", "-fno-builtin", "-nostdinc", "-nostdinc++", "-mno-red-zone"]
    for include in ([workspace / "infoide/build"] if host else includes):
        flags += ["-I", str(include)]
    flags += ["-I", str(path.parent)]
    with tempfile.NamedTemporaryFile(suffix=".cc", mode="w") as unit:
        fragment = relative in {"infoide/src/debug.h", "infoide/src/disk.h",
                                "infoide/src/distribution.h", "infoide/src/linux.h"}
        if fragment:
            # 这些文件是 main.cc 在 namespace infoide 内拼接的实现片段，并非独立公共接口。
            unit.write(f'#include "{workspace / "infoide/src/main.cc"}"\n')
        else:
            unit.write(f'#include "{path}"\n#include "{path}"\n')
        unit.flush()
        # 宿主工具使用异常，目标系统代码沿用项目的禁异常配置。
        command = [item for item in base if not (host and item in {"-fno-exceptions", "-fno-rtti"})]
        result = subprocess.run([*command, *flags, unit.name], text=True, capture_output=True, timeout=60)
    text = path.read_text(errors="replace")
    return {"path": relative, "host": host, "context_fragment": fragment,
            "passed": result.returncode == 0,
            "diagnostics": result.stderr, "lines": len(text.splitlines()),
            "templates": len(re.findall(r"\btemplate\s*<", text)),
            "classes": re.findall(r"\bclass\s+(\w+)", text),
            "definitions": definitions(text),
            "review": "类职责、所有权及独立函数归属需人工审核；语法通过不代表设计已验证"}

with ThreadPoolExecutor(max_workers=4) as pool:
    results = list(pool.map(inspect, headers))
report = {"excluded_directories": sorted(excluded), "total": len(results),
          "passed": sum(item["passed"] for item in results), "headers": results}
report["design_review"] = {
    "limits": "C++20 编译与定向实例化能发现具体错误，不能证明所有模板实参、并发交错或全部类职责都正确。",
    "fixed": [
        {"area": "头文件依赖", "paths": ["info/include/service/console/boot.h", "info/include/service/init/boot.h", "info/include/service/fs/ext3/internal.h"], "result": "补齐直接使用的类型定义，不再依赖源文件的包含顺序"},
        {"area": "资源所有权", "paths": ["info/include/kernel/device.h"], "result": "设备映射、DMA、中断绑定和资源域禁止复制，资源域回收后显式重新构造空槽"},
        {"area": "模板约束", "paths": ["info/include/kernel/atomic.h", "info/include/service/device/storage/request_lifecycle.h"], "result": "限制原子值和请求槽范围，修复 CAS 默认失败内存序，补充实际类型与并发测试"},
        {"area": "析构接口", "paths": ["info/include/service/fs/vfs.h"], "result": "由工厂按具体类型析构，保护非虚析构以禁止通过基类指针误删"},
        {"area": "类职责与实现位置", "paths": ["info/include/service/device/storage/block_backend.h", "info/include/service/device/storage/boot_image.h", "info/include/service/device/storage/partition.h"], "result": "能力判断归入 BlockInfo，分区解析归入 PartitionTable；长实现移到源文件"},
        {"area": "标准数组布局", "paths": ["guiw/include/service/guiw/textures.h"], "result": "移除越过 data[1] 的尾随数组用法，增加容量溢出检查，长实现移到源文件"}
    ],
    "retain": [
        "消息、磁盘及寄存器布局中的 POD 记录保留简单数据类型，不能为改成类而破坏布局和 ABI。",
        "纯数学、字节编码和 C ABI 入口不强行归入没有状态的类；与对象能力或生命周期相关的操作才归入类。",
        "通用模板的定义需要对实例化点可见；仅把定义藏到 .cc 会造成其他实参的链接错误。"
    ],
    "remaining": [
        {"path": "guiw/app/common/clock-views.h", "issue": "Clock 包含长布局、交互和线程逻辑，适合拆出源文件及独立视图控制职责；不属于 C++ 语法错误"},
        {"path": "guiw/app/games/common/game-ui.h", "issue": "长绘制和事件循环仍在头文件，应结合三个游戏工程的构建输入统一迁移"},
        {"path": "info/include/service/common/lifecycle.h", "issue": "配置解析、持久化和服务监护集中，应按职责迁移实现，避免只拆文件却保留过大接口"},
        {"path": "info/include/service/init", "issue": "启动报告、会话监督等仍有长非模板方法，已定位在逐文件 definitions 清单中"},
        {"path": "infoide/src", "issue": "四个头文件是 main.cc 在命名空间内包含的实现片段；上下文编译合法，但不适合作为独立公共头使用"}
    ]
}
(output / "report.json").write_text(json.dumps(report, ensure_ascii=False, indent=2))
summary = [f'自有头文件 C++20 检查：{report["passed"]}/{report["total"]}',
           '标准库、POSIX、OS 库、第三方及生成副本不在扫描范围。',
           'InfoIDE 的四个实现片段按其实际 main.cc 包含上下文检查。', '', '本轮已修正：']
for finding in report["design_review"]["fixed"]:
    summary.append(f'- {finding["area"]}：{finding["result"]}')
summary += ['', '保留原则：', *['- ' + item for item in report["design_review"]["retain"]],
            '', '仍需后续整理（不算作已完成）：']
for finding in report["design_review"]["remaining"]:
    summary.append(f'- {finding["path"]}：{finding["issue"]}')
summary += ['', '全部文件及长定义位置：']
for item in results:
    summary.append(f'{item["path"]}：{"通过" if item["passed"] else "失败"}，模板声明 {item["templates"]} 个')
    for definition in item["definitions"]:
        if definition["body_lines"] > 12:
            summary.append(f'  {definition["line"]}: {definition["owner"] or "命名空间"}::{definition["name"]}，函数体约 {definition["body_lines"]} 行')
summary += ['', report["design_review"]["limits"]]
(output / "summary.txt").write_text("\n".join(summary) + "\n")
for item in results:
    if not item["passed"]:
        print(item["path"] + "\n" + item["diagnostics"], flush=True)
print(f'Headers: {report["passed"]}/{report["total"]}; report: {output / "report.json"}', flush=True)
raise SystemExit(0 if report["passed"] == report["total"] else 1)
