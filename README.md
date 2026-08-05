# AI 实时语音面试系统

基于 C++17 和 Qt6 开发的桌面 AI 模拟面试系统。项目集成实时语音识别、语音合成、DeepSeek 大语言模型和本地 RAG 知识库，支持语音面试、动态追问、回答评分、面试总结，以及基于技术资料的连续文字问答。

## 当前功能

### 语音模拟面试

- 创建候选人面试会话并设置题目数量。
- 解析文本型 PDF 简历，生成个性化技术问题。
- 未提供简历时生成通用 C++ 面试题。
- 使用麦克风采集候选人回答，并通过云端 ASR 转写。
- 使用 DeepSeek 评估回答、决定是否追问并生成整体总结。
- 使用云端 TTS 播放开场白、问题、追问和总结。
- 修复追问状态错乱、重复 ASR、总结尾音截断和音频卡顿问题。
- 面试结束后生成 JSON 报告。

### RAG 知识库

- 创建、删除和启用多个知识库。
- 上传单个或多个 PDF、TXT、Markdown、JSON 文档。
- 上传包含支持格式文档的文件夹。
- 按 Markdown 标题边界切分文档，避免相邻问答串块。
- 使用本地 Ollama `qwen3-embedding:0.6b` 生成 1024 维向量。
- 使用 SQLite 持久化知识库、文档块和向量。
- 对当前启用知识库执行 Top-K 相似度检索。
- 专业面试官可以使用知识库内容辅助回答评估。

### AI 面试助手

- 独立的连续文字问答页面。
- 严格根据当前启用知识库回答技术问题。
- 使用 DeepSeek 生成答案，使用 Ollama 完成知识检索向量化。
- 支持最近多轮对话上下文和指代型追问。
- 显示参考文件、相似度和相关文档片段。
- 支持 Markdown 标题、列表、加粗、行内代码和代码块渲染。
- 没有检索到相关资料时不调用 DeepSeek，避免脱离知识库编造答案。
- 切换知识库时自动清空上一知识库的对话历史。

## 客户端页面

程序包含三个标签页：

```text
面试
├─ 新建会话
├─ 语音面试
├─ 问答记录与状态
└─ 面试进度

知识库
├─ 知识库列表
├─ 创建、删除和启用
├─ 文档与文件夹上传
└─ 向量检索测试

AI 面试助手
├─ 当前知识库状态
├─ 连续对话记录
├─ Markdown 回答
└─ 参考资料来源
```

系统不再提供“智能客服助手”。当前业务角色只有：

- 专业面试官：负责语音面试、追问、评分和总结。
- AI 面试助手：负责根据技术知识库进行面试复习问答。

## 系统架构

```text
Qt6 MainWindow
├─ 面试页面
│  └─ DialogSession
│     ├─ InterviewSession
│     │  ├─ PDFParser
│     │  └─ LLMClient（DeepSeek）
│     ├─ RealtimeClient（WebSocket）
│     │  ├─ ASR
│     │  ├─ VAD
│     │  └─ TTS
│     └─ AudioDeviceManager（PortAudio）
│
├─ KnowledgeBaseWidget
│  ├─ DocumentLoader
│  ├─ EmbeddingClient（Ollama）
│  └─ VectorStore（SQLite）
│
└─ KnowledgeChatWidget
   ├─ RAGPromptBuilder
   ├─ EmbeddingClient（Ollama）
   ├─ VectorStore（SQLite）
   └─ 独立 LLMClient（DeepSeek）
```

面试与 AI 面试助手使用独立的 RAG 角色实例和 LLM 对话历史，避免知识问答影响面试评分流程。

## RAG 工作流程

### 上传文档

```text
PDF/TXT/Markdown/JSON
    → 解析文本
    → 按标题和长度分块
    → Ollama 生成向量
    → 写入 SQLite 知识库
```

### AI 面试助手问答

```text
用户问题
    → Ollama 生成查询向量
    → 检索当前启用知识库
    → RAGPromptBuilder 组织参考资料
    → DeepSeek 生成回答
    → 显示 Markdown 回答与来源
```

Ollama 只负责 Embedding，不负责生成最终答案。DeepSeek 负责面试出题、评估、总结和 AI 面试助手回答。

## 技术栈

- C++17
- Qt6 Core / Gui / Widgets / Concurrent
- CMake
- vcpkg
- Boost.Asio / Boost.Beast
- OpenSSL
- PortAudio
- libcurl
- PoDoFo
- SQLite
- nlohmann/json
- spdlog
- Ollama `qwen3-embedding:0.6b`
- DeepSeek Chat Completions API

## 目录结构

```text
ai-interview/
├─ config/
│  └─ default_config.json
├─ include/
│  ├─ common/
│  ├─ interview/
│  ├─ services/
│  └─ ui/
├─ src/
│  ├─ common/
│  ├─ interview/
│  ├─ services/
│  ├─ ui/
│  └─ main_qt.cpp
├─ tests/
├─ knowledge_base/
│  └─ vectors.db
├─ CMakeLists.txt
├─ vcpkg.json
└─ README.md
```

## 环境要求

- Windows 10/11
- Visual Studio 2022 Build Tools 与 MSVC
- 支持 C++17 的编译器
- CMake 3.20 或更高版本；当前开发环境已验证 CMake 4.4
- vcpkg
- Qt6 与 `vcpkg.json` 中声明的依赖
- PowerShell 7
- Ollama
- 可用的麦克风和扬声器
- 可访问 DeepSeek 和实时语音服务的网络

## 环境变量

不要把真实密钥写入源码、配置文件或 README。当前终端可使用：

```powershell
$env:DOUBAO_APP_ID="你的应用编号"
$env:DOUBAO_ACCESS_KEY="你的访问密钥"
$env:DOUBAO_APP_KEY="你的应用密钥"
$env:DEEPSEEK_API_KEY="你的 DeepSeek API Key"
```

检查 DeepSeek 密钥是否存在：

```powershell
[bool]$env:DEEPSEEK_API_KEY
```

以上 `$env:` 设置只对当前 PowerShell 会话有效。需要长期保存时可以使用 Windows 用户环境变量，但不要在日志或提交记录中暴露真实值。

## 配置 Ollama

确认 Ollama 已安装：

```powershell
ollama --version
```

如需把模型保存在 D 盘：

```powershell
New-Item -ItemType Directory -Force "D:\OllamaModels"
setx OLLAMA_MODELS "D:\OllamaModels"
$env:OLLAMA_MODELS = "D:\OllamaModels"
```

下载向量模型：

```powershell
ollama pull qwen3-embedding:0.6b
ollama list
```

验证 Embedding 接口：

```powershell
$response = Invoke-RestMethod `
  -Uri "http://127.0.0.1:11434/api/embed" `
  -Method Post `
  -ContentType "application/json" `
  -Body '{"model":"qwen3-embedding:0.6b","input":["测试向量"]}'

$response.embeddings[0].Count
```

正常结果应为：

```text
1024
```

## 编译项目

在项目根目录执行：

```powershell
cd D:\Projects\ai-interview

cmake -S . -B build `
  -DCMAKE_TOOLCHAIN_FILE="D:\DevTools\vcpkg\scripts\buildsystems\vcpkg.cmake"

cmake --build build --config Release -j 4
```

生成文件：

```text
D:\Projects\ai-interview\build\Release\CppInterviewSystem.exe
```

Qt 翻译目录、ICU 模块探测和 `VCINSTALLDIR` 相关的 `windeployqt` 警告目前不影响可执行文件生成。

## 运行项目

请从项目根目录启动：

```powershell
cd D:\Projects\ai-interview
.\build\Release\CppInterviewSystem.exe
```

不要直接在其他工作目录双击或启动 EXE。当前配置中的数据库路径是：

```text
./knowledge_base/vectors.db
```

它是相对于程序启动时的工作目录解析的。如果从其他目录启动，程序可能打开另一份 `knowledge_base/vectors.db`，界面上会表现为原有知识库或文档块“消失”。数据通常没有被删除，只是程序打开了不同路径下的数据库。

当前项目根目录中的知识库数据库应位于：

```text
D:\Projects\ai-interview\knowledge_base\vectors.db
```

## 使用方法

### 模拟面试

1. 打开“面试”页面。
2. 点击“新建会话”。
3. 输入候选人姓名和题目数量。
4. 根据需要选择 PDF 简历。
5. 点击“开始面试”。
6. 等待问题生成并完成语音面试。
7. 面试结束后查看总结和 JSON 报告。

### 创建知识库

1. 打开“知识库”页面。
2. 点击“新建知识库”。
3. 输入知识库名称和描述。
4. 在“目标知识库”中选择它。
5. 点击“上传文档”或“上传文件夹”。
6. 等待解析、向量化和数据库写入全部完成。
7. 在知识库列表中选中它并点击“启用选中”。

本地 Markdown 文件修改后不会自动同步。修改源文件后，需要重新上传，系统会重新分块并更新向量数据。

### 使用 AI 面试助手

1. 先在“知识库”页面启用一个包含文档块的知识库。
2. 打开“AI 面试助手”页面。
3. 输入问题并点击“发送问题”，或按 `Ctrl+Enter`。
4. 查看回答以及下方的参考资料。
5. 可以继续使用“它”“这个机制”等方式追问。
6. 切换知识库或点击“清空对话”后，将开始新的对话上下文。

## 自动化测试

编译后执行：

```powershell
ctest --test-dir build -C Release --output-on-failure
```

当前包含 Markdown 标题边界分块测试，用于防止相邻技术问题和答案被错误拼接到同一文档块。

## 输出文件

运行日志：

```text
interview_qt.log
```

面试报告：

```text
interview_report_YYYYMMDD_HHMMSS.json
```

日志和面试报告可能包含个人信息与面试内容，不应提交到公共仓库。

## 当前限制

- PDF 解析主要面向文本型 PDF，暂不提供扫描图片 OCR。
- 知识库不会自动监控本地源文件变化。
- AI 面试助手是文字问答，不进入语音面试状态机。
- RAG 依赖本机 Ollama 服务和已下载的向量模型。
- DeepSeek 与实时语音能力依赖外部服务和有效鉴权信息。
- 当前知识库数据库使用相对路径，启动程序时应保持项目根目录为工作目录。

## 安全注意事项

- 不要提交真实 API Key、访问密钥或应用密钥。
- 不要提交本地日志、面试报告和包含隐私的简历。
- 对外部署时应增加身份认证、访问控制和请求频率限制。
- 正式发布时应完整验证 TLS 证书和外部服务安全策略。
