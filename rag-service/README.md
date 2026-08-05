# AI Interview RAG Service

独立 RAG HTTP 服务骨架。服务边界和 API 契约分别位于：

- `../docs/architecture/rag_service_boundary.md`
- `../docs/api/rag-service-api.md`
- `../docs/api/rag-service.openapi.yaml`
- `../docs/deployment/rag-service.md`

## 当前状态

- 已完成 FastAPI 应用入口、配置、认证、请求追踪和统一错误响应。
- API 契约中的 13 个操作均已接入真实实现。
- 支持知识库和文档状态的 SQLite 持久化。
- 支持 PDF、Markdown、TXT、JSON 的解析和重叠切分。
- 支持 Ollama/OpenAI 兼容 Embedding、余弦相似度检索和来源返回。
- 支持 RAG Prompt、多轮会话及 OpenAI 兼容 LLM 回答。
- C++/Qt 客户端支持通过 `use_remote_rag=true` 切换到本服务，也可临时回退本地链路。

## 目录

```text
rag-service/
├─ app/
│  ├─ api/           # HTTP 路由、请求和响应模型
│  ├─ core/          # 配置、异常和中间件
│  ├─ document/      # 文档解析与切分端口
│  ├─ embedding/     # Embedding 端口
│  ├─ retrieval/     # 检索编排端口
│  ├─ vector_store/  # 知识库与向量存储端口
│  ├─ llm/           # 生成模型端口
│  └─ main.py        # FastAPI 应用入口
├─ tests/
├─ .env.example
├─ Dockerfile
└─ pyproject.toml
```

## 本地启动

推荐使用稳定版 Python 3.11–3.14：

```powershell
cd rag-service
py -3.14 -m venv .venv
.\.venv\Scripts\Activate.ps1
python -m pip install -e ".[dev]"
Copy-Item .env.example .env
uvicorn app.main:app --reload
```

本工作站登记的普通 `py -3.14` 路径已经失效，可改用已安装的 Astral
解释器创建环境：

```powershell
py -V:Astral/CPython3.14.4 -m venv .venv
```

访问：

- 健康检查：`http://127.0.0.1:8000/health`
- Swagger UI：`http://127.0.0.1:8000/docs`
- OpenAPI JSON：`http://127.0.0.1:8000/openapi.json`

除健康检查外，请求需要 Header：

```http
Authorization: Bearer change-me
```

生产环境必须修改 `RAG_API_KEY`，并通过密钥管理系统注入，不要提交 `.env`。

启动前还必须配置 `RAG_LLM_API_URL`、`RAG_LLM_API_KEY` 和
`RAG_LLM_MODEL`。默认 Embedding 地址对应本机 Ollama；如果使用其他
OpenAI 兼容服务，请同时设置 `RAG_EMBEDDING_API_URL`、
`RAG_EMBEDDING_API_KEY` 和 `RAG_EMBEDDING_MODEL`。

## 当前实现约束

- SQLite 相似度检索与原 C++ 实现一致，复杂度为 `O(N × D)`；数据量扩大后可在
  `VectorStore` 端口后替换为 FAISS、Chroma 或专用向量数据库。
- 文档处理任务当前使用 FastAPI 进程内 BackgroundTasks。服务重启时未完成任务会被
  标记为 `failed`；生产多实例部署前应换成持久化任务队列。
- RAG 对话历史当前保存在服务进程内，适用于单实例。多实例部署前应迁移到 Redis
  或数据库。
- `/health` 当前检查配置和本地存储是否就绪，不主动调用收费的 Embedding/LLM 接口。
- 本步骤迁移的是能力和后续新数据；原 C++ `knowledge_base/vectors.db` 中的历史数据
  尚未导入服务端数据库。

## 测试

```powershell
python -m pytest
ruff check app tests
```

测试覆盖 API 注册、认证、统一错误格式、健康检查、知识库 CRUD、文档上传与处理、
检索、问答、会话清理和资源隔离。

## Docker

```powershell
docker build -t ai-interview-rag-service .
docker run --rm -p 8000:8000 --env-file .env ai-interview-rag-service
```

也可以使用 Compose（数据保存在命名卷中）：

```powershell
docker compose up -d --build
docker compose ps
docker compose logs -f rag-service
```

容器以非 root 用户运行，并通过 `/health` 执行 Docker 健康检查。完整的配置、
HTTPS、备份、日志追踪和升级回滚说明见部署文档。
