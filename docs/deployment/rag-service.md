# RAG 服务部署与运维

## 发布内容

- 服务目录：`rag-service/`
- 服务版本：`0.2.0`
- API 前缀：`/api/v1`
- 健康检查：`GET /health`
- Swagger：`GET /docs`
- 静态 OpenAPI 契约：`docs/api/rag-service.openapi.yaml`

服务版本由 `app.__version__` 提供，并显示在 `/health`、FastAPI OpenAPI 元数据和
结构化日志中。发布时还应同步更新 `pyproject.toml`、Compose 镜像标签和静态
OpenAPI 文档；自动测试会检查主要版本来源没有漂移。

## 本地启动

本机开发、调试和老师验收不要求安装 Docker，直接使用 Python 虚拟环境即可：

```powershell
cd rag-service
py -3.13 -m venv .venv
./.venv/Scripts/Activate.ps1
python -m pip install -e ".[dev]"
Copy-Item .env.example .env
python -m app.cli
```

开发时可以使用：

```powershell
uvicorn app.main:app --host 127.0.0.1 --port 8000 --reload
```

启动后验证：

```powershell
Invoke-RestMethod http://127.0.0.1:8000/health
```

## 本机完整验收启动顺序

完整使用知识库功能需要依次启动 Ollama、RAG 服务和 Qt 客户端。这三个进程职责
独立，建议分别保留一个 PowerShell 终端以便查看日志。

终端 1，启动 Ollama Embedding 服务：

```powershell
$env:OLLAMA_MODELS = [Environment]::GetEnvironmentVariable(
    "OLLAMA_MODELS",
    "User"
)
ollama serve
```

可以在另一个终端确认模型存在：

```powershell
ollama list
```

终端 2，启动 RAG 服务。以下示例只把系统用户级 DeepSeek 密钥映射到当前进程，
不会把真实密钥写入代码或 `.env`：

```powershell
cd D:\Projects\ai-interview\rag-service
$env:RAG_LLM_API_KEY = [Environment]::GetEnvironmentVariable(
    "DEEPSEEK_API_KEY",
    "User"
)
.\.venv\Scripts\python.exe -m app.cli
```

启动前应确保 `rag-service/.env` 中的 LLM 地址和模型已经配置，并且服务端
`RAG_API_KEY` 与 Qt 进程读取的 `RAG_API_KEY` 相同。DeepSeek 使用 OpenAI 兼容
协议时，provider 名称表示协议类型，不代表使用 OpenAI 的密钥。

终端 3，启动 Qt 客户端：

```powershell
cd D:\Projects\ai-interview
$env:RAG_API_KEY = "local-dev-key"
.\build\Release\CppInterviewSystem.exe
```

若只验证不依赖知识库的普通面试功能，可以关闭 RAG 功能；知识库上传、检索、问答
或启用了 RAG 评分时，需要保持 Ollama 和 RAG 服务运行。

## Docker Compose 部署

Docker Compose 是生产化或容器化部署选项，不是本机验收的前置条件。

先复制并修改配置：

```powershell
cd rag-service
Copy-Item .env.example .env
docker compose up -d --build
docker compose ps
```

Compose 使用 `rag-data` 命名卷持久化 SQLite 数据库和上传文件。容器以非 root
用户运行，并每 30 秒访问一次 `/health`。查看日志：

```powershell
docker compose logs --tail 200 -f rag-service
```

如果 Ollama 运行在宿主机，容器中的 `127.0.0.1` 指向容器自身。Docker Desktop
环境应将配置改为：

```dotenv
RAG_EMBEDDING_API_URL=http://host.docker.internal:11434/api/embed
```

## 必需配置

生产环境至少需要设置：

| 配置 | 说明 |
|---|---|
| `RAG_API_KEY` | 客户端 Bearer Token，必须替换默认值 |
| `RAG_LLM_API_URL` | OpenAI 兼容 Chat Completions 地址 |
| `RAG_LLM_API_KEY` | LLM 密钥 |
| `RAG_LLM_MODEL` | LLM 模型名 |
| `RAG_EMBEDDING_API_URL` | Ollama 或 OpenAI 兼容 Embedding 地址 |
| `RAG_EMBEDDING_MODEL` | Embedding 模型名 |
| `RAG_VERIFY_SSL` | 生产环境保持 `true` |

完整示例见 `rag-service/.env.example`。密钥应由部署平台的 Secret 功能注入，
不要写入镜像、Compose 文件、日志或 Git。

Qt 客户端配置示例：

```json
{
  "rag": {
    "enabled": true,
    "use_remote_rag": true,
    "service_url": "https://rag.example.com",
    "api_key": "由密钥系统注入",
    "timeout_seconds": 60,
    "verify_ssl": true,
    "max_retries": 2,
    "retry_delay_ms": 500
  }
}
```

## 健康检查与流量接入

`GET /health` 不需要认证，响应包含服务名、版本、时间及 Embedding、SQLite、LLM
状态。它只做非收费的就绪性检查，不会主动请求外部模型。

- `healthy`：配置和关键本地依赖均已就绪。
- `degraded`：进程仍存活，但至少一个模型配置或存储依赖不可用。
- HTTP 连接失败：进程不可用，应由容器平台重启。

当前 `/health` 对 `degraded` 仍返回 HTTP 200，因此 Docker 将其视为进程存活。
负载均衡器若要求严格就绪，可解析响应中的 `status`，只向 `healthy` 实例转发。

## 日志与错误追踪

默认 `RAG_LOG_FORMAT=json`，每行日志包含：

- UTC 时间、日志级别、服务名和版本
- `request_id`
- HTTP 方法、路径、状态码和耗时
- 业务错误码或异常堆栈

客户端可以传入安全格式的 `X-Request-ID`；未提供或格式非法时服务端自动生成。
响应 Header 和错误响应体会返回同一 ID。排障时以该 ID 查询容器日志即可串联完整
请求。`RAG_LOG_FORMAT=text` 可用于本地阅读，`RAG_LOG_LEVEL` 支持常规 Python
日志级别。

日志默认输出 stdout，由 Docker、Kubernetes 或日志平台负责采集、保留和告警。
建议至少对以下事件告警：连续 HTTP 500、`/health` 长期 degraded、请求超时、文档
处理 failed，以及磁盘空间不足。

## HTTPS 与网络

生产环境不要直接暴露 Uvicorn。应在反向代理或云负载均衡器终止 HTTPS，仅允许 Qt
客户端和运维网络访问服务。限制请求体大小，并使代理超时大于 RAG 服务及 Qt 客户端
配置的生成超时。

## 备份、升级与回滚

备份必须同时包含：

- `/service/data/rag.db`
- `/service/data/uploads/`

升级前停止写入或停止容器，再对整个数据卷做一致性快照。镜像使用明确版本标签，
不要只依赖 `latest`。升级后先检查 `/health`，再执行接口集成测试；失败时恢复上一
镜像和同一份数据快照。

当前对话历史和后台文档任务保存在单进程内，SQLite 也不适合多个实例同时承担高
写入。因此当前版本按单实例部署。扩展到多实例前，应把会话迁移到 Redis、任务迁移
到持久化队列，并把向量存储迁移到支持并发的数据库。

## 发布验证

```powershell
cd rag-service
python -m pytest
ruff check app tests
docker compose config
docker compose build
docker compose up -d
docker compose ps
```

`tests/test_rag_pipeline.py` 是接口集成测试，使用替身模型完整覆盖知识库创建、文档
上传与处理、检索、RAG 回答、会话清理及删除，不消耗真实模型额度。
