# 新旧 RAG 并行验证

项目保留两条可运行路径，并提供 `RagComparisonTool` 对同一问题并行调用本地和
远程后端，用于迁移结果对比和故障回退验证。正式界面仍只选择一条主路径，避免
一次用户操作产生两次回答。

## 1. 准备知识库

本地知识库 ID 是 SQLite 中的数字 ID，远程知识库 ID 由独立服务返回，二者通常
不同。先在两边创建对应知识库，并尽量上传同一份文档。

也可以在对比输入中填写 `document_path`。工具会在查询前把同一文件分别上传到
两个知识库，并把两边上传结果写入报告。旧本地 `VectorStore` 不支持按单文档
删除；需要清理时请删除整个本地知识库后重建。

## 2. 配置输入

复制 `config/rag_comparison.example.json`，至少替换：

- `local_knowledge_base_id`
- `remote_knowledge_base_id`
- `question`

问题放在 UTF-8 JSON 文件中，而不是直接放在 Windows 命令行参数中，可避免中文
代码页导致的乱码。

## 3. 构建并运行

```powershell
cmake --build build --config Release --target RagComparisonTool
./build/Release/RagComparisonTool.exe ./config/rag_comparison.example.json
```

如果填写了 `output_path`，报告写入该文件；否则输出到终端。报告保留两边完整的
检索来源、答案、错误与耗时，并自动给出：

- `utf8_valid`：中文及返回文本是否为有效 UTF-8
- `citations_valid`：命中知识时是否有完整且合法的引用
- `knowledge_found_matches`：两边是否同时命中或同时未命中
- `citation_name_jaccard`：两边引用文档名的重合程度，范围 `0~1`

回答质量需要人工阅读完整答案判断，报告中的
`manual_answer_quality_review_required=true` 会明确提示这一点。

## 4. 验收清单

建议用同一批问题逐项记录：

- 检索结果是否与问题相关，远程结果是否明显退化
- 最终回答是否正确、完整且没有脱离引用内容
- 文档名、内容、页码与相关度是否可信
- 设置 `document_path` 后，两边是否都上传成功并能检索到
- 停止远程服务后，本地结果是否仍完整，远程错误是否单独记录
- 中文问题、答案和来源的 `utf8_valid` 是否为 `true`
- 使用两个不同知识库分别测试，确认来源没有串库

自动测试 `RagBackendParallelComparison` 覆盖并行执行、中文 UTF-8、引用结构、
本地/远程知识库 ID 隔离，以及远程异常不影响本地结果。真实检索相关性和回答
质量仍需在实际 Embedding、LLM 与远程 RAG 服务运行时验收。
