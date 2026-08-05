# RAG 客户端配置

当前 Qt 项目默认通过 HTTP 调用独立 RAG 服务，同时保留旧本地 RAG 链路用于
兼容回退与结果对比，可通过 `use_remote_rag` 切换。默认配置位于
`config/default_config.json`：

```json
{
  "rag": {
    "enabled": true,
    "use_remote_rag": true,
    "service_url": "http://127.0.0.1:8000",
    "api_key": "",
    "timeout_seconds": 60,
    "verify_ssl": true,
    "max_retries": 2,
    "retry_delay_ms": 500,
    "top_k": 3,
    "similarity_threshold": 0.7
  }
}
```

## 开发与生产环境

配置文件提供开发默认值，部署时使用环境变量覆盖，不需要修改或提交密钥：

| 环境变量 | 作用 |
|---|---|
| `RAG_USE_REMOTE` | `true` 使用独立服务，`false` 使用旧本地 RAG 链路 |
| `RAG_SERVICE_URL` | 覆盖 RAG 服务根地址 |
| `RAG_API_KEY` | 覆盖 Bearer Token |
| `RAG_TIMEOUT_SECONDS` | 覆盖单次 HTTP 请求超时 |
| `RAG_VERIFY_SSL` | `true/false`，控制 HTTPS 证书与主机名校验 |
| `RAG_MAX_RETRIES` | 安全请求的最大额外重试次数，范围 `0~5` |
| `RAG_RETRY_DELAY_MS` | 第一次重试等待时间，后续按指数退避 |

开发环境示例：

```powershell
$env:RAG_SERVICE_URL = "http://127.0.0.1:8000"
$env:RAG_API_KEY = "local-dev-key"
```

客户端的 `RAG_API_KEY` 必须与 `rag-service/.env` 中的 `RAG_API_KEY` 完全一致，
否则除 `/health` 外的请求会返回 `401 Unauthorized`。示例值仅用于本机开发，不能
作为生产密钥。

生产环境示例：

```powershell
$env:RAG_SERVICE_URL = "https://rag.example.com"
$env:RAG_API_KEY = "从密钥管理系统注入"
$env:RAG_VERIFY_SSL = "true"
```

生产环境必须使用 HTTPS 并保持 `verify_ssl=true`。只有使用本地自签名证书调试时，
才可以临时关闭验证。

RAG 服务可以通过 OpenAI 兼容的 Chat Completions 协议调用 DeepSeek。配置中的
`RAG_LLM_PROVIDER=openai` 表示使用兼容协议，并不表示必须使用 OpenAI 的模型或
API Key；实际服务地址、模型和密钥分别由 `RAG_LLM_API_URL`、`RAG_LLM_MODEL` 和
`RAG_LLM_API_KEY` 决定。真实密钥应从系统环境变量或密钥管理系统注入，不能写入
Git。

## 超时、错误与重试

- 网络不可达、超时、HTTP 错误和无效 JSON 都转换为 `success=false` 与
  `error_message`，Qt 页面会显示“RAG 服务不可用”或具体操作失败原因。
- 知识库列表、文档列表和 `/rag/search` 是安全读取操作，可对网络错误、
  `429`、`502`、`503`、`504` 自动重试。
- 创建知识库、上传文档、删除资源和 `/rag/query` 不自动重试，避免请求已经被
  服务端处理但响应丢失时发生重复创建、重复上传或重复对话。
- 默认最多额外重试 2 次，等待时间为 500 ms、1000 ms。

旧配置字段 `service_api_key`、`service_timeout_seconds` 和
`service_verify_ssl` 仍可读取，但新配置应使用本文所列字段。

## 新旧链路切换

- `use_remote_rag=true`：界面调用 `RagClient -> HttpClient -> 独立 RAG 服务`。
- `use_remote_rag=false`：界面使用保留的 `EmbeddingClient -> VectorStore ->
  RAGPromptBuilder -> LLMClient` 本地链路。
- 环境变量 `RAG_USE_REMOTE` 优先级高于配置文件，适合部署和临时回退。

本地模式还会读取 `embedding_provider`、`embedding_api_url`、
`embedding_model`、`vector_db_path`、`chunk_size`、`chunk_overlap` 和
`max_history_turns`。这些字段仅用于对照验证与临时回退，不属于远程服务配置。
