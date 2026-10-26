# os/input.h


## 内容与边界

Input 保存输入订阅、服务连接、唤醒端点和状态快照。open 订阅成功后读取一次真实状态，receive 按完整事件更新缓存，refresh 主动重新读取快照；getter 只读取缓存，不发 IPC。输入服务目前只允许获得授权的 console/window 服务订阅原始输入；普通图形应用通过 osgui 接收自己窗口的事件。

Input::Event 嵌套在 Input 内，提供事件类型、时间、键码、修饰键、按下状态、相对位移、滚轮和按钮的 getter。仅当前事件类型对应的字段有效，wire 消息单独编码。

服务支持批量接收后可增加 receiveBatch。窗口坐标转换、焦点、光标图片和忙状态属于 osgui 或窗口服务。

## 枚举

### Input::Event::Type

底层类型：`uint32_t`。事件种类，决定该读取事件的哪些字段。

| 成员 | 数值 | 含义 |
| --- | --- | --- |
| `None` | `0` | 没有有效对象或事件 |
| `KeyDown` | `1` | 键按下 |
| `KeyUp` | `2` | 键释放 |
| `MouseMove` | `3` | 鼠标移动 |
| `MouseButtonDown` | `4` | 鼠标按钮按下 |
| `MouseButtonUp` | `5` | 鼠标按钮释放 |
| `MouseWheel` | `6` | 鼠标滚轮滚动 |

### Input::Key

底层类型：`uint32_t`。按键编号，不代表输入字符；大小写由修饰键和键盘布局解释。

| 成员 | 数值 | 含义 |
| --- | --- | --- |
| `Unknown` | `0` | 未知按键 |
| `Escape` | `1` | Esc 键 |
| `Digit1` | `2` | 主键区数字 1 键 |
| `Digit2` | `3` | 主键区数字 2 键 |
| `Digit3` | `4` | 主键区数字 3 键 |
| `Digit4` | `5` | 主键区数字 4 键 |
| `Digit5` | `6` | 主键区数字 5 键 |
| `Digit6` | `7` | 主键区数字 6 键 |
| `Digit7` | `8` | 主键区数字 7 键 |
| `Digit8` | `9` | 主键区数字 8 键 |
| `Digit9` | `10` | 主键区数字 9 键 |
| `Digit0` | `11` | 主键区数字 0 键 |
| `Backspace` | `12` | 退格键 |
| `Tab` | `13` | Tab 键 |
| `A` | `14` | A 键 |
| `B` | `15` | B 键 |
| `C` | `16` | C 键 |
| `D` | `17` | D 键 |
| `E` | `18` | E 键 |
| `F` | `19` | F 键 |
| `G` | `20` | G 键 |
| `H` | `21` | H 键 |
| `I` | `22` | I 键 |
| `J` | `23` | J 键 |
| `K` | `24` | K 键 |
| `L` | `25` | L 键 |
| `M` | `26` | M 键 |
| `N` | `27` | N 键 |
| `O` | `28` | O 键 |
| `P` | `29` | P 键 |
| `Q` | `30` | Q 键 |
| `R` | `31` | R 键 |
| `S` | `32` | S 键 |
| `T` | `33` | T 键 |
| `U` | `34` | U 键 |
| `V` | `35` | V 键 |
| `W` | `36` | W 键 |
| `X` | `37` | X 键 |
| `Y` | `38` | Y 键 |
| `Z` | `39` | Z 键 |
| `Enter` | `40` | 主键区回车键 |
| `LeftControl` | `41` | 左 Ctrl 键 |
| `RightControl` | `42` | 右 Ctrl 键 |
| `LeftShift` | `43` | 左 Shift 键 |
| `RightShift` | `44` | 右 Shift 键 |
| `LeftAlt` | `45` | 左 Alt 键 |
| `RightAlt` | `46` | 右 Alt 键 |
| `Space` | `47` | 空格键 |
| `CapsLock` | `48` | 大写锁定键 |
| `Minus` | `49` | 减号/下划线键 |
| `Equal` | `50` | 等号/加号键 |
| `LeftBracket` | `51` | 左方括号键 |
| `RightBracket` | `52` | 右方括号键 |
| `Semicolon` | `53` | 分号键 |
| `Apostrophe` | `54` | 单引号键 |
| `Grave` | `55` | 反引号键 |
| `Backslash` | `56` | 反斜杠键 |
| `Comma` | `57` | 逗号键 |
| `Period` | `58` | 句号键 |
| `Slash` | `59` | 斜杠键 |
| `Up` | `60` | 上方向键 |
| `Down` | `61` | 下方向键 |
| `Left` | `62` | 左方向键 |
| `Right` | `63` | 右方向键 |
| `F1` | `64` | F1 功能键 |
| `F2` | `65` | F2 功能键 |
| `F3` | `66` | F3 功能键 |
| `F4` | `67` | F4 功能键 |
| `F5` | `68` | F5 功能键 |
| `F6` | `69` | F6 功能键 |
| `F7` | `70` | F7 功能键 |
| `F8` | `71` | F8 功能键 |
| `F9` | `72` | F9 功能键 |
| `F10` | `73` | F10 功能键 |
| `F11` | `74` | F11 功能键 |
| `F12` | `75` | F12 功能键 |
| `NumLock` | `76` | 数字锁定键 |
| `ScrollLock` | `77` | 滚动锁定键 |
| `Insert` | `78` | 插入键 |
| `Delete` | `79` | 删除键 |
| `Home` | `80` | Home 键 |
| `End` | `81` | End 键 |
| `PageUp` | `82` | 向上翻页键 |
| `PageDown` | `83` | 向下翻页键 |
| `Keypad0` | `84` | 数字键区 0 键 |
| `Keypad1` | `85` | 数字键区 1 键 |
| `Keypad2` | `86` | 数字键区 2 键 |
| `Keypad3` | `87` | 数字键区 3 键 |
| `Keypad4` | `88` | 数字键区 4 键 |
| `Keypad5` | `89` | 数字键区 5 键 |
| `Keypad6` | `90` | 数字键区 6 键 |
| `Keypad7` | `91` | 数字键区 7 键 |
| `Keypad8` | `92` | 数字键区 8 键 |
| `Keypad9` | `93` | 数字键区 9 键 |
| `KeypadDecimal` | `94` | 数字键区小数点键 |
| `KeypadDivide` | `95` | 数字键区除号键 |
| `KeypadMultiply` | `96` | 数字键区乘号键 |
| `KeypadMinus` | `97` | 数字键区减号键 |
| `KeypadPlus` | `98` | 数字键区加号键 |
| `KeypadEnter` | `99` | 数字键区回车键 |

### InputRequest

底层类型：`uint64_t`。内部协议枚举，不进入 os/input.h。input 服务内部请求编号放在现有 include/service/common/requests.h 中，固定消息布局放在 include/service/common/records.h。调用者使用 Input 的具体成员函数。

| 成员 | 数值 | 含义 |
| --- | --- | --- |
| `GetEvent` | `512` | 取出输入事件 |
| `GetState` | `513` | 查询状态 |
| `Subscribe` | `514` | 登记输入订阅 |
| `Unsubscribe` | `515` | 按端点身份取消当前进程对应的订阅 |

### Input::Modifier

底层类型：`uint32_t`。可按位组合；它们表示状态位，不是按键编号。

| 成员 | 数值 | 含义 |
| --- | --- | --- |
| `Shift` | `1` | Shift 状态位 |
| `Control` | `2` | Ctrl 状态位 |
| `Alt` | `4` | Alt 状态位 |
| `CapsLock` | `8` | 大写锁定状态位 |
| `NumLock` | `16` | 数字锁定状态位 |
| `ScrollLock` | `32` | 滚动锁定状态位 |

## 常量与标志

表中列出当前声明和值，并明确它在推荐接口中是否保留。一般不把 namespace constexpr 变量当成新的共享状态；稳定选项优先用所属类型的枚举，容量等通过相关对象的 getter 查询。

| 当前名字 | 当前值或表达式 | 数据含义 | 推荐归属 |
| --- | --- | --- | --- |
| `keyModifierShift` | `1` | Shift 状态位 | 改为 Input::Modifier::Shift |
| `keyModifierControl` | `2` | Ctrl 状态位 | 改为 Input::Modifier::Control |
| `keyModifierAlt` | `4` | Alt 状态位 | 改为 Input::Modifier::Alt |
| `keyModifierCapsLock` | `8` | 大写锁定状态位 | 改为 Input::Modifier::CapsLock |
| `keyModifierNumLock` | `16` | 数字锁定状态位 | 改为 Input::Modifier::NumLock |
| `keyModifierScrollLock` | `32` | 滚动锁定状态位 | 改为 Input::Modifier::ScrollLock |
| `inputServiceName` | `"input"` | 输入服务注册名 | 输入连接实现内部使用，不在应用头文件导出字符串变量 |
| `inputEventNotification` | `0x494e505554` | 输入到达通知的消息标识 | 输入服务协议内部常量 |

## 全局变量

当前公共头文件没有导出可写全局变量。订阅引用与输入快照属于 Input，事件内容属于 Event。

## 其他类型与固定布局

除本页列出的类、嵌套类及必要参数类型外，不增加整份内部状态记录；服务 wire 记录留在服务头文件。

## 类与函数

### Input
这个类表示当前进程的一份输入订阅及唤醒端点。

#### 构造与析构

```cpp
~Input();
```

用途：如果调用者没有显式关闭，最后尝试清理仍由本对象拥有的引用或映射。析构不能返回清理错误，也不能当作业务操作成功的证明。

参数：无。

结果：析构无返回值，不抛出异常。需要确认清理成功时，先显式 close。


```cpp
Input();
```

用途：建立空对象并初始化本地字段，不查找服务、不创建资源。

参数：无。

结果：构造没有返回值；需要资源时显式调用 open 或 create，并检查它的返回值。


#### 操作

```cpp
bool receive(Input::Event& event);
```

用途：从输入服务取出一个输入事件并填写 event，尚无事件时返回相应无数据状态。

参数：

- `Input::Event& event`：接收输入事件的输出对象。

结果：取得一个事件返回 true，队列为空或操作失败返回 false；getError 为 WouldBlock 表示暂时没有事件，其他负值表示连接或协议错误。失败不覆盖 event。

```cpp
int wait(uint64_t deadline);
```

用途：等待本订阅的输入到达通知；超时或中断保留订阅和已缓存状态，允许继续等待。

参数：

- `uint64_t deadline`：本次等待的单调时钟绝对截止时间；约定的无限值表示不设截止时间。

结果：0 表示成功，负 ErrorCode 表示失败；输出参数仅按本函数说明在成功或部分完成时有效。

```cpp
int refresh();
```

用途：向对应服务查询当前属性；响应完整有效后更新本对象缓存，失败保留上一次完整结果。

参数：无。

结果：0 表示成功，负 ErrorCode 表示失败；输出参数仅按本函数说明在成功或部分完成时有效。

```cpp
int open();
```

用途：连接输入服务并建立事件订阅，成功后才允许等待和接收。

参数：无。

结果：成功返回 0，失败返回负 ErrorCode。对象状态按服务确认的实际结果更新，已释放的引用不再重复释放。


```cpp
int close();
```

用途：通过本订阅的唤醒端点身份取消订阅，再释放本地端点。失败时保留尚未释放的引用；已经取消订阅后重试 close，不再次取消其他订阅。

参数：无。

结果：成功返回 0，失败返回负 ErrorCode。对象状态按服务确认的实际结果更新，已释放的引用不再重复释放。


#### getter

```cpp
bool getConnected() const;
```

数据：是否已经连接到服务。

```cpp
bool getKeyPressed(Input::Key key) const;
```

数据：指定按键当前是否按下。

```cpp
uint32_t getModifiers() const;
```

数据：Shift、Ctrl 等修饰键状态。

```cpp
uint32_t getButtons() const;
```

数据：鼠标按钮状态。

```cpp
bool getValid() const;
```

数据：对象是否可用。

```cpp
int getError() const;
```

数据：最近一次申请或操作的错误码。

### Input::Event

这个类表示一次按键、鼠标移动、按钮或滚轮事件。

#### getter

```cpp
Input::Event::Type getType() const;
```

数据：输入事件种类。

```cpp
uint64_t getTimestamp() const;
```

数据：事件发生时间。

```cpp
Input::Key getKeyCode() const;
```

数据：按键编号。

```cpp
uint32_t getModifiers() const;
```

数据：Shift、Ctrl 等修饰键状态。

```cpp
bool getPressed() const;
```

数据：该按键事件是否表示按下。

```cpp
int32_t getDeltaX() const;
```

数据：鼠标水平移动量。

```cpp
int32_t getDeltaY() const;
```

数据：鼠标垂直移动量。

```cpp
int32_t getWheel() const;
```

数据：滚轮移动量。

```cpp
uint32_t getButtons() const;
```

数据：鼠标按钮状态。

## 实现文件

`lib/os/input.cc` 对应本头文件。相关普通实现不另起与头文件无关的根目录文件名。
