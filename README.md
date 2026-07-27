# AI 实时语音面试系统

基于 C++17 和 Qt6 开发的桌面实时语音技术面试系统。系统通过 WebSocket（全双工网络通信协议）连接云端实时语音服务，集成 ASR（自动语音识别）、VAD（语音活动检测）、TTS（文本转语音）和 LLM（大语言模型），支持简历驱动出题、回答评分、动态追问、面试总结及报告生成。

## 主要功能

- 支持创建和配置候选人面试会话。
- 支持设置面试题目数量。
- 支持解析文本型 PDF（便携式文档格式）简历。
- 支持根据候选人简历生成个性化技术问题。
- 未提供简历时，可生成通用 C++ 技术面试题。
- 支持麦克风实时录音和候选人语音识别。
- 支持面试题及总结的实时语音播报。
- 支持根据候选人回答进行智能评分和动态追问。
- 支持在图形界面中实时展示面试状态、问答内容和面试进度。
- 面试结束后自动生成 JSON（结构化数据格式）报告。

## 系统架构

项目采用分层架构，划分为通用层、服务层、业务层和 UI（用户界面）层：

```text
Qt6 图形界面
    │
    ▼
DialogSession（对话会话调度）
    │
    ├── InterviewSession（面试业务管理）
    │       ├── PDFParser（简历解析）
    │       └── LLMClient（大语言模型客户端）
    │               └── DeepSeek（出题、评分、追问、总结）
    │
    ├── AudioDeviceManager（音频设备管理）
    │       ├── 麦克风采集
    │       └── 扬声器播放
    │
    ├── RealtimeClient（实时语音客户端）
    │       └── 云端实时对话服务
    │               ├── ASR（自动语音识别）
    │               ├── VAD（语音活动检测）
    │               └── TTS（文本转语音）
    │
    └── Protocol（二进制协议编解码）
```

系统中的主要职责分工如下：

- 云端实时语音服务负责“听”和“说”，即语音识别、语音活动检测和语音合成。
- DeepSeek（深度求索大语言模型）负责“思考”，即生成问题、评估回答、决定追问和生成总结。
- 本地 C++ 程序负责面试流程、状态管理、音频调度、界面展示和报告保存。

## 面试流程

```text
创建面试会话
    → 解析简历或生成通用问题
    → 连接实时语音服务
    → 播放开场白
    → 候选人语音回答
    → ASR识别为文字
    → LLM评分并决定追问或下一题
    → TTS播报新问题
    → 所有问题完成
    → 生成并播报面试总结
    → 保存面试报告
```

## 技术栈

- C++17
- Qt6（跨平台图形界面框架）
- Boost.Asio / Boost.Beast（异步网络与 WebSocket 通信库）
- OpenSSL（加密通信库）
- PortAudio（跨平台音频输入输出库）
- libcurl（HTTP 网络请求库）
- PoDoFo（PDF 文档处理库）
- nlohmann/json（JSON 数据处理库）
- zlib / GZIP（压缩库与压缩格式）
- spdlog（日志库）
- CMake（跨平台构建系统）
- vcpkg（C++ 依赖管理工具）
- 多线程、状态机、生产者—消费者模式和 Pimpl（实现隐藏模式）

## 目录结构

```text
ai-interview/
├── config/
│   └── default_config.json       # 默认配置
├── include/
│   ├── common/                   # 配置、日志、状态机、通信协议和工具
│   ├── interview/                # 面试业务和会话调度接口
│   ├── services/                 # 音频、网络、大语言模型和PDF服务接口
│   └── ui/                       # Qt界面接口
├── src/
│   ├── common/                   # 通用层实现
│   ├── interview/                # 面试业务层实现
│   ├── services/                 # 服务层实现
│   ├── ui/                       # 图形界面实现
│   └── main_qt.cpp               # 当前程序入口
├── build.py                      # 构建辅助脚本
├── CMakeLists.txt                # CMake构建配置
├── vcpkg.json                    # 第三方依赖清单
└── README.md                     # 项目说明
```

## 核心模块

### 对话会话调度

`DialogSession`（对话会话）是系统的核心调度模块，负责：

- 初始化音频设备和实时网络连接。
- 启动麦克风线程和音频播放线程。
- 接收并处理服务端事件。
- 管理候选人回答、问题切换和最终总结。
- 协调语音识别、大语言模型和语音合成模块。
- 面试结束后保存报告并释放资源。

### 面试业务管理

`InterviewSession`（面试业务会话）负责：

- 生成和管理面试问题。
- 记录候选人回答、分数及反馈。
- 判断是否需要追问。
- 判断面试是否完成。
- 生成面试总结和结构化报告。

### 实时语音通信

`RealtimeClient`（实时语音客户端）基于 Boost.Beast 和 OpenSSL 建立 TLS（传输层安全协议）加密的 WebSocket 长连接，连接过程包括：

```text
解析服务地址
    → DNS（域名解析）
    → TCP（传输控制协议）连接
    → TLS握手
    → WebSocket握手
    → 初始化逻辑连接
    → 初始化语音会话
    → 启动接收线程
```

### 二进制通信协议

`Protocol`（通信协议模块）负责：

- 构建和解析 4 字节协议头。
- 处理消息类型、事件号和序列号。
- 处理连接编号和会话编号。
- 处理 JSON 数据和原始音频数据。
- 处理大端字节序。
- 支持 GZIP 数据压缩和解压。
- 解析服务端错误响应。

### 音频管理

`AudioDeviceManager`（音频设备管理器）基于 PortAudio 实现：

- 16000 Hz、单声道、16 位 PCM 麦克风采集。
- 24000 Hz、单声道、32 位浮点音频播放。
- 通过生产者—消费者队列播放服务端返回的语音。
- 通过预缓冲、队列排空检测和超时恢复减少音频卡顿。

### 大语言模型

`LLMClient`（大语言模型客户端）基于 libcurl 调用兼容 OpenAI 接口格式的聊天补全服务，实现：

- 根据简历生成技术面试题。
- 生成通用 C++ 技术面试题。
- 评估候选人回答。
- 决定是否需要追问。
- 生成面试总结和录用建议。

### 图形界面

Qt6 图形界面支持：

- 候选人姓名和面试题目数量配置。
- PDF 简历选择。
- 面试状态、问答内容和题目进度展示。
- 后台线程事件安全切换至 UI 主线程更新。

## 配置说明

默认配置文件位于：

```text
config/default_config.json
```

主要配置项包括：

- 麦克风和扬声器音频参数。
- 实时语音服务地址和请求头。
- ASR、VAD 和 TTS 参数。
- 对话角色及播报规则。
- 大语言模型接口地址、模型名称、生成参数和超时时间。

## 环境变量

为避免将密钥提交到代码仓库，鉴权信息应通过环境变量设置：

```text
DOUBAO_APP_ID          豆包应用编号
DOUBAO_ACCESS_KEY      豆包访问密钥
DOUBAO_APP_KEY         豆包应用密钥
DEEPSEEK_API_KEY       DeepSeek接口密钥
```

Windows PowerShell 示例：

```powershell
$env:DOUBAO_APP_ID="你的应用编号"
$env:DOUBAO_ACCESS_KEY="你的访问密钥"
$env:DOUBAO_APP_KEY="你的应用密钥"
$env:DEEPSEEK_API_KEY="你的大语言模型接口密钥"
```

以上命令只对当前终端会话生效。请勿将真实密钥写入 README、源代码、日志或提交记录。

## 环境要求

- 支持 C++17 的编译器。
- CMake 3.20 或更高版本。
- vcpkg 及项目依赖。
- Qt5 或 Qt6，推荐 Qt6。
- 可用的麦克风和扬声器。
- 能够访问实时语音服务和大语言模型服务的网络环境。

Windows 推荐使用 Visual Studio 2022 和 MSVC（微软 C++ 编译器）。

## 安装依赖

项目使用 vcpkg 清单模式管理依赖。在已配置 `VCPKG_ROOT` 环境变量的情况下，CMake 会自动使用 vcpkg 工具链。

主要依赖已定义在：

```text
vcpkg.json
```

## 编译项目

### 使用构建脚本

```powershell
python build.py --config Release
```

### 使用 CMake

```powershell
cmake -S . -B build `
  -DCMAKE_TOOLCHAIN_FILE="$env:VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake"

cmake --build build --config Release
```

构建成功后，Windows 可执行文件通常位于：

```text
build/Release/CppInterviewSystem.exe
```

## 运行项目

启动程序前先设置鉴权环境变量，然后运行：

```powershell
.\build\Release\CppInterviewSystem.exe
```

操作步骤：

1. 点击“新建会话”。
2. 输入候选人姓名。
3. 设置面试题目数量。
4. 根据需要选择 PDF 简历。
5. 点击“开始面试”。
6. 等待题目生成并开始语音面试。
7. 面试完成后查看自动生成的报告。

## 输出文件

### 日志

程序运行日志默认保存为：

```text
interview_qt.log
```

### 面试报告

面试报告默认保存为：

```text
interview_report_YYYYMMDD_HHMMSS.json
```

报告内容包括：

- 候选人姓名。
- 面试时间和时长。
- 问题、回答、类别及难度。
- 每题分数和反馈。
- 平均分和总分。
- 面试总结和录用建议。

日志和面试报告可能包含个人隐私及面试内容，不应提交到代码仓库。

## 当前说明

- 当前 CMake 构建目标使用 `src/main_qt.cpp` 作为图形界面入口。
- `src/main.cpp` 是早期命令行入口，当前未加入构建目标。
- PDF 解析针对文本型简历，扫描图片型简历暂不支持 OCR（光学字符识别）。
- 当前实时语音和大语言模型能力依赖外部服务及有效鉴权信息。
- 项目后续可扩展为使用 FunASR 部署本地 ASR 服务，以及使用 CosyVoice 部署本地 TTS 服务。

## 安全注意事项

- 不要在配置文件或源码中保存真实接口密钥。
- 不要提交日志、面试报告和本地环境文件。
- 正式部署时应启用完整的 TLS 证书验证。
- 对外提供服务时应增加身份认证、访问控制和请求频率限制。
