# 输入事件

## 枚举

### os::Input::Key

| 成员 | 值 | 说明 |
| --- | --- | --- |
| `Unknown` | `0` | 未知类型 |
| `Escape` | `1` | Esc键 |
| `Digit1` | `2` | 主键区数字1键 |
| `Digit2` | `3` | 主键区数字2键 |
| `Digit3` | `4` | 主键区数字3键 |
| `Digit4` | `5` | 主键区数字4键 |
| `Digit5` | `6` | 主键区数字5键 |
| `Digit6` | `7` | 主键区数字6键 |
| `Digit7` | `8` | 主键区数字7键 |
| `Digit8` | `9` | 主键区数字8键 |
| `Digit9` | `10` | 主键区数字9键 |
| `Digit0` | `11` | 主键区数字0键 |
| `Backspace` | `12` | 退格键 |
| `Tab` | `13` | Tab键 |
| `A` | `14` | A键 |
| `B` | `15` | B键 |
| `C` | `16` | C键 |
| `D` | `17` | D键 |
| `E` | `18` | E键 |
| `F` | `19` | F键 |
| `G` | `20` | G键 |
| `H` | `21` | H键 |
| `I` | `22` | I键 |
| `J` | `23` | J键 |
| `K` | `24` | K键 |
| `L` | `25` | L键 |
| `M` | `26` | M键 |
| `N` | `27` | N键 |
| `O` | `28` | O键 |
| `P` | `29` | P键 |
| `Q` | `30` | Q键 |
| `R` | `31` | R键 |
| `S` | `32` | S键 |
| `T` | `33` | T键 |
| `U` | `34` | U键 |
| `V` | `35` | V键 |
| `W` | `36` | W键 |
| `X` | `37` | X键 |
| `Y` | `38` | Y键 |
| `Z` | `39` | Z键 |
| `Enter` | `40` | 主键区回车键 |
| `LeftControl` | `41` | 左Ctrl键 |
| `RightControl` | `42` | 右Ctrl键 |
| `LeftShift` | `43` | 左Shift键 |
| `RightShift` | `44` | 右Shift键 |
| `LeftAlt` | `45` | 左Alt键 |
| `RightAlt` | `46` | 右Alt键 |
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
| `F1` | `64` | F1功能键 |
| `F2` | `65` | F2功能键 |
| `F3` | `66` | F3功能键 |
| `F4` | `67` | F4功能键 |
| `F5` | `68` | F5功能键 |
| `F6` | `69` | F6功能键 |
| `F7` | `70` | F7功能键 |
| `F8` | `71` | F8功能键 |
| `F9` | `72` | F9功能键 |
| `F10` | `73` | F10功能键 |
| `F11` | `74` | F11功能键 |
| `F12` | `75` | F12功能键 |
| `NumLock` | `76` | 数字锁定键 |
| `ScrollLock` | `77` | 滚动锁定键 |
| `Insert` | `78` | 插入键 |
| `Delete` | `79` | 删除键 |
| `Home` | `80` | Home键 |
| `End` | `81` | End键 |
| `PageUp` | `82` | 向上翻页键 |
| `PageDown` | `83` | 向下翻页键 |
| `Keypad0` | `84` | 数字键区0键 |
| `Keypad1` | `85` | 数字键区1键 |
| `Keypad2` | `86` | 数字键区2键 |
| `Keypad3` | `87` | 数字键区3键 |
| `Keypad4` | `88` | 数字键区4键 |
| `Keypad5` | `89` | 数字键区5键 |
| `Keypad6` | `90` | 数字键区6键 |
| `Keypad7` | `91` | 数字键区7键 |
| `Keypad8` | `92` | 数字键区8键 |
| `Keypad9` | `93` | 数字键区9键 |
| `KeypadDecimal` | `94` | 数字键区小数点键 |
| `KeypadDivide` | `95` | 数字键区除号键 |
| `KeypadMultiply` | `96` | 数字键区乘号键 |
| `KeypadMinus` | `97` | 数字键区减号键 |
| `KeypadPlus` | `98` | 数字键区加号键 |
| `KeypadEnter` | `99` | 数字键区回车键 |
| `LeftSuper` | `100` | 左 Win 键 |
| `RightSuper` | `101` | 右 Win 键 |

### os::Input::Modifier

| 成员 | 值 | 说明 |
| --- | --- | --- |
| `Shift` | `1` | Shift状态位 |
| `Control` | `2` | Ctrl状态位 |
| `Alt` | `4` | Alt状态位 |
| `CapsLock` | `8` | 大写锁定键 |
| `NumLock` | `16` | 数字锁定键 |
| `ScrollLock` | `32` | 滚动锁定键 |
| `Super` | `64` | Win 键状态位 |

### os::Input::Event::Type

| 成员 | 值 | 说明 |
| --- | --- | --- |
| `None` | `0` | 空值 |
| `KeyDown` | `1` | 键按下 |
| `KeyUp` | `2` | 键释放 |
| `MouseMove` | `3` | 鼠标移动 |
| `MouseButtonDown` | `4` | 鼠标按钮按下 |
| `MouseButtonUp` | `5` | 鼠标按钮释放 |
| `MouseWheel` | `6` | 鼠标滚轮滚动 |

## 对象

### os::Input

输入设备连接对象，接收键盘和鼠标事件。

#### 构造与析构

| 函数 | 用途 | 参数 |
| --- | --- | --- |
| `Input()` | 构造Input对象 | — |
| `~Input()` | 析构Input对象 | — |

#### 成员函数

| 函数 | 用途 | 参数 |
| --- | --- | --- |
| `int open()` | 打开对象连接 | — |
| `int close()` | 关闭对象连接 | — |
| `int wait(uint64_t deadline)` | 等待本对象对应的文件请求完成 | `deadline`：等待截止时间，单位为单调时钟纳秒 |
| `bool receive(Event& event)` | 从输入服务取出一个输入事件并填写event | `event`：接收输入事件的对象 |
| `int refresh()` | 更新对象属性 | — |

#### Getter

| 函数 | 用途 | 参数 |
| --- | --- | --- |
| `bool getConnected() const` | 读取是否已经连接到服务 | — |
| `bool getKeyPressed(Key key) const` | 读取指定按键当前是否按下 | `key`：按键编号 |
| `uint32_t getModifiers() const` | 读取Shift、Ctrl等修饰键状态 | — |
| `uint32_t getButtons() const` | 读取鼠标按钮状态 | — |
| `bool getValid() const` | 读取对象是否可用 | — |
| `int getError() const` | 读取最近一次申请或操作的错误码 | — |

### os::Input::Event

输入事件对象，保存事件类型、按键、坐标和修饰键状态。

#### Getter

| 函数 | 用途 | 参数 |
| --- | --- | --- |
| `Type getType() const` | 读取目录项是文件、目录还是其他类型 | — |
| `uint64_t getTimestamp() const` | 读取事件发生时间 | — |
| `Key getKeyCode() const` | 读取按键编号 | — |
| `uint32_t getModifiers() const` | 读取Shift、Ctrl等修饰键状态 | — |
| `bool getPressed() const` | 读取该按键事件是否表示按下 | — |
| `int32_t getDeltaX() const` | 读取鼠标水平移动量 | — |
| `int32_t getDeltaY() const` | 读取鼠标垂直移动量 | — |
| `int32_t getWheel() const` | 读取滚轮移动量 | — |
| `uint32_t getButtons() const` | 读取鼠标按钮状态 | — |
