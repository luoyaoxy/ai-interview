# AI 语音面试与 RAG 知识问答系统

基于 C++17 和 Qt6 开发的桌面 AI 模拟面试系统。项目集成实时语音识别、语音合成和 DeepSeek 大语言模型，并通过 HTTP 接入独立的 Python RAG 服务，支持语音面试、动态追问、回答评分、面试总结，以及基于技术资料的连续文字问答。旧的 C++ 本地 RAG 链路仍保留用于回退和对比验证，但默认使用独立服务。

Android Kotlin 客户端位于 [`android/`](android/README.md)，已支持简历上传、语音或文字面试、动态追问、报告和历史恢复。模拟器、局域网真机及 HTTPS 服务的连接方法见 [`docs/deployment/android-device.md`](docs/deployment/android-device.md)。
正式 APK/AAB 的签名、版本和发布流程见 [`docs/deployment/android-release.md`](docs/deployment/android-release.md)。

## 当前功能

### 语音模拟面试

- 创建候选人面试会话并设置题目数量。
- 解析文本型 PDF 简历，生成个性化技术问题。
- 未提供简历时生成通用 C++ 面试题。
- 使用麦克风采集候选人回答，并通过云端 ASR 转写。（ASR automatic speech recognition 自动语音识别技术）
- 使用 DeepSeek 评估回答、决定是否追问并生成整体总结。
- 使用云端 TTS 播放开场白、问题、追问和总结。(TTS text to speech 文本转语音)
- 修复追问状态错乱、重复 ASR、总结尾音截断和音频卡顿问题。
- 面试结束后生成 JSON 报告。

### RAG 知识库

- 创建、删除和启用多个知识库。
- 上传单个或多个 PDF、TXT、Markdown、JSON 文档。
- 上传包含支持格式文档的文件夹。
- 默认由独立 RAG 服务完成文档解析、按标题和长度切分、向量化与持久化。
- RAG 服务默认调用 Ollama `qwen3-embedding:0.6b` 生成 1024 维向量。
- RAG 服务使用 SQLite 持久化知识库、文档块和向量；默认结合向量与关键词检索，对候选片段融合、重排后返回 Top-K。可切换到纯向量模式作对比。
- 专业面试官可以使用知识库内容辅助回答评估。

### AI 面试助手

- 独立的连续文字问答页面。
- 要求根据当前启用知识库回答技术问题，并在事实性结论后标注资料编号。
- RAG 服务调用已配置的生成模型（如 DeepSeek）生成答案，默认调用 Ollama 完成知识检索向量化。
- RAG 服务通过会话 ID 管理最近多轮对话上下文和指代型追问。
- 显示参考文件、检索排序分数和相关文档片段；混合模式的分数不是余弦相似度。
- 支持 Markdown 标题、列表、加粗、行内代码和代码块渲染。
- 没有检索到资料时不调用生成模型；模型表示证据不足、未提供引用或引用编号无效时，返回拒答提示。
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
└─ 知识库检索测试

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
Qt6 桌面客户端（C++）
├─ 面试页面 / DialogSession
│  ├─ PDFParser：解析候选人简历
│  ├─ LLMClient（DeepSeek）：出题、评估、追问和总结
│  ├─ RealtimeClient（WebSocket）：ASR、VAD 和 TTS
│  └─ AudioDeviceManager（PortAudio）：录音与播放
│
├─ KnowledgeBaseWidget：知识库与文档管理界面
├─ KnowledgeChatWidget：知识问答与来源展示界面
└─ RagBackend
   ├─ RagClient（默认）：通过 HTTP 调用独立 RAG 服务
   └─ LocalRagBackend（可选）：旧本地链路，用于回退和对比

独立 RAG 服务（Python / FastAPI，由 RagClient 调用）
├─ 知识库与文档管理
├─ 文档解析与切分
├─ Embedding（默认 Ollama）
├─ SQLite 向量存储、关键词与向量混合检索、候选重排
├─ RAG Prompt、引用编号校验与多轮会话管理
└─ LLM（需配置，如 DeepSeek）：生成带资料编号的答案
```

普通面试仍由 Qt 客户端负责，不经过 RAG 服务；知识库管理和知识问答默认由 RAG 服务负责。两条链路的 LLM 对话状态相互隔离，避免知识问答影响面试评分流程。更详细的服务边界和数据归属见 [`docs/architecture/rag_service_boundary.md`](docs/architecture/rag_service_boundary.md)。

## RAG 工作流程

### 上传文档

```text
PDF/TXT/Markdown/JSON
    → Qt 客户端通过 HTTP 上传文件
    → RAG 服务解析文本并按标题和长度分块
    → RAG 服务调用 Ollama 生成向量
    → RAG 服务写入自己的 SQLite 数据库
```

### AI 面试助手问答

```text
用户问题
    → Qt 客户端提交问题、知识库 ID 和会话 ID
    → RAG 服务调用 Ollama 生成查询向量
    → RAG 服务结合向量与关键词检索，融合并重排资料
    → RAG 服务调用已配置的生成模型，检查引用编号或拒答
    → Qt 客户端显示 Markdown 回答与来源
```

Ollama 只负责 Embedding，不负责生成最终答案。Qt 客户端中的 DeepSeek 负责面试出题、评估和总结；独立 RAG 服务需要另行配置生成模型的地址、密钥和模型名。引用编号校验只能确认回答引用了当前检索结果，是否真正受到原文支持仍需评测和人工复核。

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
- Python 3.11～3.14（独立 RAG 服务）
- FastAPI / Uvicorn
- Ollama `qwen3-embedding:0.6b`
- DeepSeek Chat Completions API

## 目录结构

```text
ai-interview/
├─ config/
│  └─ default_config.json
├─ docs/
│  ├─ architecture/
│  ├─ api/
│  └─ deployment/
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
├─ rag-service/
│  ├─ app/
│  ├─ evals/             # 带证据标注的评测集与评测脚本
│  ├─ tests/
│  ├─ Dockerfile
│  └─ pyproject.toml
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
- Python 3.11～3.14（使用独立 RAG 服务时）
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

默认配置 `use_remote_rag=true`。需要先按照 [`rag-service/README.md`](rag-service/README.md) 启动独立 RAG 服务，并确保客户端的 `service_url` 和 `api_key` 与服务端配置一致；如果只使用语音面试而不使用知识库功能，可以仅启动客户端。

然后从项目根目录启动 Qt 客户端：

```powershell
cd D:\Projects\ai-interview
.\build\Release\CppInterviewSystem.exe
```

远程模式下，知识库数据由 RAG 服务拥有，Qt 客户端不会直接读写 `knowledge_base/vectors.db`。服务端数据库位置和上传目录由 `rag-service/.env` 配置；客户端不应依赖服务端的本地文件路径。

只有显式设置 `use_remote_rag=false` 启用旧本地回退链路时，下面的客户端数据库路径才生效：

```text
./knowledge_base/vectors.db
```

该路径相对于程序启动时的工作目录解析，因此使用本地回退模式时应从项目根目录启动。

本地回退数据库应位于：

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

## RAG 效果评测

独立 RAG 服务提供 [带原文证据标注的评测集与运行说明](rag-service/evals/README.md)。当前样例包含 9 道可回答题和 3 道应拒答题，可计算 Hit@K、Recall@K、MRR，并记录逐题检索结果与耗时。样例规模较小，正式判断优化效果前应补充真实资料和独立留出的测试题。

先将服务端 `RAG_RETRIEVAL_MODE` 设为 `semantic` 并重启服务。在 `rag-service` 目录中执行基线评测：

```powershell
$env:RAG_API_KEY = "与服务端一致的密钥"
python -m evals.run --api-key $env:RAG_API_KEY --label semantic --report evals/reports/semantic.json
```

记录命令输出的知识库 ID，再将服务端模式改为 `hybrid` 并重启，使用同一批已入库文档比较：

```powershell
python -m evals.run --api-key $env:RAG_API_KEY --knowledge-base-id "上一步输出的知识库ID" --label hybrid --report evals/reports/hybrid.json
```

增加 `--answers` 可调用已配置的生成模型，检查拒答和引用编号，并生成待人工复核的答案报告。自动检查不能证明每个结论都受到原文支持；需按评测说明复核答案、引用与证据。评测报告存放在 `rag-service/evals/reports/`，不会提交到 Git。

## 自动化测试

编译后执行：

```powershell
ctest --test-dir build -C Release --output-on-failure
```

当前包含 Markdown 标题边界分块测试，用于防止相邻技术问题和答案被错误拼接到同一文档块。

独立 RAG 服务测试：

```powershell
cd rag-service
python -m pytest
ruff check app tests evals
```

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
- 默认 RAG 服务依赖可访问的 Ollama 服务和已下载的向量模型。
- DeepSeek 与实时语音能力依赖外部服务和有效鉴权信息。
- RAG 服务当前使用进程内后台任务和进程内对话历史；生产多实例部署前应改用持久化任务队列和共享会话存储。
- 当前关键词检索会遍历知识库文档块，较大知识库需要进一步优化索引和延迟。
- 旧本地 RAG 回退模式的数据库使用相对路径，启用该模式时应保持项目根目录为工作目录。

## 安全注意事项

- 不要提交真实 API Key、访问密钥或应用密钥。
- 不要提交本地日志、面试报告和包含隐私的简历。
- 对外部署时应增加身份认证、访问控制和请求频率限制。
- 正式发布时应完整验证 TLS 证书和外部服务安全策略。
