# RAG 服务 HTTP API 设计

## 1. 状态

本文件定义 AI 面试项目与独立 RAG 服务之间的 HTTP 接口。机器可读契约见同目录下的 `rag-service.openapi.yaml`。

独立服务已创建在 `../../rag-service`。知识库管理、文档解析切分、Embedding、
向量检索、Prompt 构建和 RAG LLM 问答均已迁入服务端。当前 C++/Qt 客户端默认
通过 `RagClient` 调用该服务，并保留 `LocalRagBackend` 用于显式回退和结果对比。

## 2. 协议约定

- API 根路径：`/api/v1`。
- 除文件上传外，请求和响应均使用 `application/json; charset=utf-8`。
- 文件上传使用 `multipart/form-data`，一次请求上传一个文件。
- 除健康检查外，接口使用 `Authorization: Bearer <token>`。
- 知识库 ID、文档 ID 和会话 ID 都是服务端定义的、不透明的字符串；客户端不得解析其格式。
- 时间使用 UTC ISO 8601，例如 `2026-08-02T14:30:00Z`。
- 相似度分数范围为 `0.0` 到 `1.0`，越大表示越相关。
- 客户端可发送 `X-Request-ID`；若未发送则由服务端生成。响应体中的 `request_id` 用于日志追踪。
- 成功与失败以 HTTP 状态码区分，不在每个响应中重复增加 `success`。失败信息统一放在 `error.code` 和 `error.message` 中；文档对象的 `error_message` 只表示异步文档处理失败原因。

## 3. 接口一览

| 方法 | 路径 | 用途 |
|---|---|---|
| `GET` | `/health` | 服务及依赖健康检查 |
| `POST` | `/api/v1/knowledge-bases` | 创建知识库 |
| `GET` | `/api/v1/knowledge-bases` | 分页列出知识库 |
| `GET` | `/api/v1/knowledge-bases/{knowledge_base_id}` | 获取知识库详情 |
| `PATCH` | `/api/v1/knowledge-bases/{knowledge_base_id}` | 更新知识库元数据 |
| `DELETE` | `/api/v1/knowledge-bases/{knowledge_base_id}` | 删除知识库及其文档和向量 |
| `POST` | `/api/v1/knowledge-bases/{knowledge_base_id}/documents` | 上传并异步处理文档 |
| `GET` | `/api/v1/knowledge-bases/{knowledge_base_id}/documents` | 分页列出文档及处理状态 |
| `GET` | `/api/v1/knowledge-bases/{knowledge_base_id}/documents/{document_id}` | 获取单个文档处理状态 |
| `DELETE` | `/api/v1/knowledge-bases/{knowledge_base_id}/documents/{document_id}` | 删除文档、文档块和向量 |
| `POST` | `/api/v1/rag/search` | 只检索来源，不调用生成模型 |
| `POST` | `/api/v1/rag/query` | 检索并生成最终回答 |
| `DELETE` | `/api/v1/rag/conversations/{conversation_id}` | 清空 RAG 对话历史 |

## 4. 健康检查

### `GET /health`

健康检查不要求认证。`status` 有三种状态：

- `healthy`：服务和关键依赖均可用。
- `degraded`：服务可响应，但 Embedding、向量库或 LLM 中至少一个依赖异常。
- `unhealthy`：服务无法正常处理请求。

示例：

```json
{
  "status": "healthy",
  "service": "rag-service",
  "version": "0.2.0",
  "time": "2026-08-02T14:30:00Z",
  "dependencies": {
    "embedding": "healthy",
    "vector_store": "healthy",
    "llm": "healthy"
  }
}
```

## 5. 知识库接口

### 创建知识库

```http
POST /api/v1/knowledge-bases
```

```json
{
  "name": "C++ 基础知识",
  "description": "面试使用的 C++ 知识资料",
  "role_type": "interviewer"
}
```

成功返回 `201 Created`。知识库名称为空返回 `400`；名称冲突返回 `409`。

### 获取知识库列表

```http
GET /api/v1/knowledge-bases?page_size=20&cursor=opaque-cursor
```

列表响应包含：

- `items`：知识库列表。
- `next_cursor`：下一页游标；没有下一页时为 `null`。
- `total`：当前权限范围内的总数量。

客户端选择的“活跃知识库”属于 Qt 界面状态，不是服务端知识库属性，因此不提供“设置活跃知识库”接口。

### 更新知识库

```http
PATCH /api/v1/knowledge-bases/{knowledge_base_id}
```

请求可包含 `name`、`description` 和 `role_type` 中的任意字段，但至少包含一个字段。

### 删除知识库

```http
DELETE /api/v1/knowledge-bases/{knowledge_base_id}
```

成功返回 `204 No Content`。删除必须级联清理该知识库下的文档、文档块和向量。

## 6. 文档管理接口

### 上传文档

```http
POST /api/v1/knowledge-bases/{knowledge_base_id}/documents
Content-Type: multipart/form-data
```

表单字段：

- `file`：必填，文档二进制内容。
- `display_name`：可选，界面展示名称；默认使用上传文件名。

上传成功返回 `202 Accepted`，表示服务端已经接收任务，但解析和向量化不一定完成：

```json
{
  "request_id": "req-01",
  "document": {
    "id": "doc-01",
    "knowledge_base_id": "kb-cpp",
    "file_name": "C++基础.pdf",
    "status": "queued",
    "chunk_count": 0,
    "error_message": "",
    "created_at": "2026-08-02T14:30:00Z",
    "updated_at": "2026-08-02T14:30:00Z"
  }
}
```

文档状态：

- `queued`：已接收，等待处理。
- `processing`：正在解析、切分或向量化。
- `ready`：处理完成，可以检索。
- `failed`：处理失败，`error_message` 包含可展示原因。

客户端通过获取文档详情或列表轮询状态。后续若需要实时进度，可单独增加 SSE/WebSocket，不纳入第一版接口。

### 列出与查询文档

```http
GET /api/v1/knowledge-bases/{knowledge_base_id}/documents
GET /api/v1/knowledge-bases/{knowledge_base_id}/documents/{document_id}
```

只有 `ready` 文档计入知识库可检索内容。文档列表使用与知识库列表相同的游标分页方式。

### 删除文档

```http
DELETE /api/v1/knowledge-bases/{knowledge_base_id}/documents/{document_id}
```

成功返回 `204 No Content`，同时删除该文档产生的所有文档块和向量。

## 7. 检索测试接口

### `POST /api/v1/rag/search`

该接口对应当前知识库界面的“检索测试”，只调用 Embedding 和向量检索，不调用 LLM。

```json
{
  "question": "智能指针有哪些类型？",
  "knowledge_base_id": "kb-cpp",
  "top_k": 3,
  "similarity_threshold": 0.7
}
```

响应：

```json
{
  "request_id": "req-02",
  "knowledge_found": true,
  "sources": [
    {
      "document_id": "doc-01",
      "document_name": "C++基础.pdf",
      "chunk_id": "chunk-12",
      "content": "C++ 智能指针包括 unique_ptr、shared_ptr……",
      "score": 0.92,
      "page": 12
    }
  ]
}
```

`top_k` 和 `similarity_threshold` 只在检索测试接口开放，便于调试。正式 RAG 查询默认使用服务端配置，避免客户端决定服务端检索策略。

## 8. RAG 查询接口

### `POST /api/v1/rag/query`

请求：

```json
{
  "question": "什么是虚函数？",
  "knowledge_base_id": "kb-cpp",
  "conversation_id": "session-001",
  "role_type": "general_assistant"
}
```

规则：

- `question` 和 `knowledge_base_id` 必填。
- `conversation_id` 可选。未提供时由服务端创建并在响应中返回。
- `role_type` 可选，默认使用知识库配置的角色。
- 会话历史、查询改写、检索、Prompt 构建和 LLM 调用全部由服务端完成。

成功响应：

```json
{
  "request_id": "req-03",
  "conversation_id": "session-001",
  "knowledge_found": true,
  "answer": "虚函数是通过动态绑定实现运行时多态的成员函数……",
  "sources": [
    {
      "document_id": "doc-01",
      "document_name": "C++基础.pdf",
      "chunk_id": "chunk-33",
      "content": "虚函数允许派生类重写基类行为……",
      "score": 0.92,
      "page": 18
    }
  ]
}
```

没有检索到相关知识时仍返回 `200 OK`：

```json
{
  "request_id": "req-04",
  "conversation_id": "session-001",
  "knowledge_found": false,
  "answer": "当前知识库中没有检索到与该问题相关的内容。",
  "sources": []
}
```

这属于正常业务结果，不应返回 `404` 或 `500`。

## 9. 清空对话历史

```http
DELETE /api/v1/rag/conversations/{conversation_id}
```

成功返回 `204 No Content`。该操作设计为幂等：会话已经不存在时也返回 `204`。

## 10. 统一错误格式

所有非 `2xx` JSON 响应使用统一格式：

```json
{
  "request_id": "req-05",
  "error": {
    "code": "KNOWLEDGE_BASE_NOT_FOUND",
    "message": "Knowledge base 'kb-missing' does not exist",
    "details": {}
  }
}
```

`code` 用于客户端稳定判断错误类型，`message` 用于日志或界面提示。客户端不得通过匹配英文 `message` 判断错误。

每个响应同时返回 `X-Request-ID` Header。发生错误时，把响应体中的
`request_id` 与服务端 JSON 日志关联，即可定位同一次请求的访问日志、业务错误和
异常堆栈；不要把 API Key、文档正文或完整 Prompt 写入日志。

常用状态码：

| 状态码 | 含义 |
|---|---|
| `400` | 字段缺失、格式错误或参数超出范围 |
| `401` | 缺少或无效的认证信息 |
| `404` | 知识库或文档不存在 |
| `409` | 名称、文件或状态冲突 |
| `413` | 上传文件过大 |
| `415` | 不支持的文件类型 |
| `429` | 请求频率超限 |
| `500` | RAG 服务内部错误 |
| `502` | Embedding 或 LLM 上游返回无效响应 |
| `503` | 向量库或关键依赖暂时不可用 |
| `504` | Embedding、检索或 LLM 调用超时 |

## 11. C++ 客户端映射

当前项目的 `RagClient` 已按以下业务边界实现：

```cpp
HealthStatus GetHealth();
KnowledgeBase CreateKnowledgeBase(const CreateKnowledgeBaseRequest& request);
KnowledgeBasePage ListKnowledgeBases(const PageRequest& page);
void DeleteKnowledgeBase(const std::string& knowledge_base_id);
Document UploadDocument(const std::string& knowledge_base_id,
                        const std::string& file_path);
DocumentPage ListDocuments(const std::string& knowledge_base_id,
                           const PageRequest& page);
void DeleteDocument(const std::string& knowledge_base_id,
                    const std::string& document_id);
SearchResponse Search(const SearchRequest& request);
RagQueryResponse Ask(const RagQueryRequest& request);
void ClearConversation(const std::string& conversation_id);
```

`RagClient` 负责 URL 拼接、JSON/Multipart 转换、认证 Header 和错误码映射；底层仍复用 `common::HttpClient`。

当前 `HttpClient` 已提供统一请求入口以及 GET、JSON POST、PATCH、DELETE 和 Multipart POST，`RagClient` 不再直接依赖 libcurl 细节。

## 12. 当前版本暂不包含

- 流式回答和 SSE/WebSocket。
- 文档批量上传接口；客户端可逐个并发上传。
- 跨多个知识库联合检索。
- 直接暴露文档块、Embedding 向量或数据库文件。
- 由客户端上传完整 Prompt 或 Embedding 向量。
- 服务端实现语言和具体向量数据库选型。
