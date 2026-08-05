/**
 * @file vector_store.h
 * @brief 向量存储与检索模块
 *
 * 基于 SQLite 的本地向量数据库。
 * 负责存储文档块的 Embedding 向量，并支持余弦相似度检索。
 *
 * 核心功能：
 * 1. 知识库管理（创建、列表、删除）
 * 2. 文档块存储（批量插入带向量的文本块）
 * 3. 语义检索（Top-K 余弦相似度搜索）
 *
 * 设计决策：
 * - 使用 SQLite 作为存储引擎（零配置、单文件、跨平台）
 * - 向量以 JSON 字符串格式存储（1536 维约 15KB/条）
 * - 检索使用暴力遍历 + 余弦相似度（5000 条以下足够快）
 * - WAL 模式提升并发读写性能
 *
 * 性能参考：
 * - 1536 维向量，5000 条 chunk 暴力检索约 30-50ms
 * - 插入 100 条 chunk（含向量）约 50ms（事务批量写入）
 *
 * 使用示例：
 * @code
 * VectorStore store;
 * store.Open("./knowledge_base.db");
 *
 * int kb_id = store.CreateKnowledgeBase("C++ 面试知识库", "", "interviewer");
 * store.SetActiveKnowledgeBaseID(kb_id);
 *
 * // 插入文档块（embedding 来自 EmbeddingClient）
 * std::vector<DocumentChunk> chunks = ...;
 * store.InsertChunks(kb_id, chunks);
 *
 * // 检索
 * auto results = store.Search(query_embedding, 3, 0.7f);
 * for (const auto& r : results) {
 *     std::cout << r.similarity << ": " << r.chunk.content << std::endl;
 * }
 * @endcode
 */

#pragma once

#include <string>
#include <vector>
#include <memory>
#include "services/embedding_client.h"

namespace interview {
namespace services {

/**
 * @brief 搜索结果结构体
 *
 * 包含匹配到的文档块及其相似度分数。
 */
struct SearchResult {
    DocumentChunk chunk;   ///< 匹配的文档块（含原文和元数据）
    float similarity = 0.0f;  ///< 余弦相似度，范围 0.0 ~ 1.0，越大越相关
};

/**
 * @brief 知识库元数据
 *
 * 描述一个知识库的基本信息（不含具体文档内容）。
 */
struct KnowledgeBaseInfo {
    int id = 0;                ///< 数据库主键 ID
    std::string name;          ///< 知识库名称，如 "产品手册 v2"
    std::string description;   ///< 知识库描述
    std::string role_type;     ///< 适用角色类型
    int chunk_count = 0;       ///< 包含的文档块数量
    std::string created_at;    ///< 创建时间（ISO 8601 格式）
};

/**
 * @brief 向量存储类（Pimpl 模式）
 *
 * 封装基于 SQLite 的向量存储与检索逻辑。
 *
 * 数据模型：
 * - knowledge_bases 表：知识库元数据（ID、名称、角色类型等）
 * - document_chunks 表：文档块（内容、向量、来源文件等）
 * - 两表通过 kb_id 外键关联，级联删除
 *
 * 线程安全：
 * - SQLite 使用 WAL 模式，支持多线程并发读
 * - 写操作（插入/删除）需外部加锁
 * - 本类不提供线程安全保证，调用方负责同步
 */
class VectorStore {
public:
    /// @brief 实现类前向声明（Pimpl 模式）
    struct Impl;

    /**
     * @brief 构造函数
     *
     * 创建未打开的 VectorStore 实例。
     * 使用前必须调用 Open() 打开数据库文件。
     */
    VectorStore();

    /**
     * @brief 析构函数
     *
     * 自动关闭数据库连接。
     */
    ~VectorStore();

    // ========================================================
    // 数据库生命周期
    // ========================================================

    /**
     * @brief 打开或创建数据库文件
     *
     * 如果文件不存在则自动创建，同时建表（CREATE TABLE IF NOT EXISTS）。
     * 自动启用 WAL 模式以提升并发性能。
     *
     * @param db_path 数据库文件路径，如 "./knowledge_base/vectors.db"
     * @return true 打开成功，false 失败
     */
    bool Open(const std::string& db_path);

    /**
     * @brief 关闭数据库连接
     *
     * 关闭后可再次调用 Open() 打开其他数据库文件。
     */
    void Close();

    /**
     * @brief 检查数据库是否已打开
     */
    bool IsOpen() const;

    // ========================================================
    // 知识库管理
    // ========================================================

    /**
     * @brief 创建新知识库
     *
     * 插入到 knowledge_bases 表，返回自增 ID。
     *
     * @param name 知识库名称（不可为空）
     * @param description 知识库描述
     * @param role_type 适用角色类型
     * @return 新知识库的 ID，失败返回 -1
     */
    int CreateKnowledgeBase(const std::string& name,
                            const std::string& description = "",
                            const std::string& role_type = "default");

    /**
     * @brief 列出所有知识库
     *
     * 查询 knowledge_bases 表并统计每个知识库的 chunk 数量。
     *
     * @return 知识库列表（按创建时间降序）
     */
    std::vector<KnowledgeBaseInfo> ListKnowledgeBases();

    /**
     * @brief 删除知识库及其所有文档块
     *
     * 利用外键 CASCADE 自动删除关联的 document_chunks。
     *
     * @param kb_id 知识库 ID
     * @return true 删除成功
     */
    bool DeleteKnowledgeBase(int kb_id);

    /**
     * @brief 获取/设置当前活跃的知识库 ID
     *
     * 活跃知识库是 Search() 检索的范围，
     * 以及 InsertChunks() 不指定 kb_id 时的默认目标。
     */
    int GetActiveKnowledgeBaseID() const;
    void SetActiveKnowledgeBaseID(int kb_id);

    // ========================================================
    // 文档块管理
    // ========================================================

    /**
     * @brief 批量插入文档块
     *
     * 将多个带 Embedding 向量的文档块写入 document_chunks 表。
     * 向量以 JSON 字符串格式存储在 embedding 列。
     *
     * 性能优化：
     * - 使用事务批量写入，每 100 条提交一次
     * - 同一知识库内重复的 chunk_id 会更新现有内容和向量
     *
     * @param kb_id 目标知识库 ID
     * @param chunks 文档块列表（需包含 embedding 向量）
     * @return 实际写入（新增或更新）的数量，失败返回 -1
     */
    int InsertChunks(int kb_id,
                     const std::vector<DocumentChunk>& chunks);

    /**
     * @brief 删除知识库中的所有文档块
     *
     * 与 DeleteKnowledgeBase 不同，此方法只删除块而不删除知识库本身。
     *
     * @param kb_id 知识库 ID
     * @return true 删除成功
     */
    bool DeleteChunksByKB(int kb_id);

    /**
     * @brief 获取知识库中的文档块数量
     *
     * @param kb_id 知识库 ID
     * @return 文档块数量
     */
    int GetChunkCount(int kb_id);

    // ========================================================
    // 检索
    // ========================================================

    /**
     * @brief 根据查询向量检索 Top-K 最相似的文档块
     *
     * 使用余弦相似度对所有匹配知识库的 chunk 进行暴力检索。
     * 过滤相似度低于 threshold 的结果，按相似度降序返回。
     *
     * 算法复杂度：O(N * D)，N=chunk 数量，D=向量维度
     *
     * @param query_embedding 查询文本的 Embedding 向量
     * @param top_k 返回前 K 个最相似的结果，默认 3
     * @param threshold 相似度阈值（0.0 ~ 1.0），默认 0.7
     * @return 按相似度降序排列的搜索结果（可能少于 top_k）
     */
    std::vector<SearchResult> Search(
        const std::vector<float>& query_embedding,
        int top_k = 3,
        float threshold = 0.7f);

    /// @brief 禁用拷贝
    VectorStore(const VectorStore&) = delete;
    VectorStore& operator=(const VectorStore&) = delete;

private:
    std::unique_ptr<Impl> impl_;  ///< 实现指针（Pimpl 模式）
};

} // namespace services
} // namespace interview
