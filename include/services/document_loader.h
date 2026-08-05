/**
 * @file document_loader.h
 * @brief 文档加载与分块模块
 *
 * 从多种格式文件中提取文本，并按策略切割成固定大小的文档块（chunk），
 * 为后续 Embedding 向量化和向量存储做准备。
 *
 * 支持格式：
 * - PDF：使用 PoDoFo 逐页提取，自动标注页码
 * - TXT：自动检测 UTF-8 BOM，GBK 兼容
 * - Markdown：保留标题层级，清洗格式符号
 * - JSON：解析结构化 Q&A 对
 *
 * 分块策略：
 * - 滑动窗口：固定大小 + 重叠（overlap）
 * - 句子边界优化：尽量在 。！？\n 处切断，保持语义完整
 *
 * 核心概念（一句话理解）：
 * 把一篇长文章切成多个"小纸条"，每张纸条之间有少量重叠，
 * 确保 LLM 后续检索时不会因为切在句子中间而丢失上下文。
 *
 * 使用示例：
 * @code
 * DocumentLoader loader;
 * loader.SetChunkConfig({500, 50, true});
 *
 * // 加载单个文件
 * auto chunks = loader.LoadDocument("./knowledge_base/manual.pdf");
 *
 * // 加载整个目录
 * auto all_chunks = loader.LoadDirectory("./knowledge_base/");
 * @endcode
 */

#pragma once

#include <memory>
#include <string>
#include <vector>

#include "services/embedding_client.h"

namespace interview {
namespace services {

/**
 * @brief 文档分块配置
 *
 * 控制文本如何被切割成小块。
 *
 * 参数说明：
 * - chunk_size: 每块最大字符数。太小丢失上下文，太大检索不精准。
 *   中文建议 300-800，英文建议 500-1500。
 * - chunk_overlap: 相邻块重叠字符数。防止关键信息恰好在边界被切断。
 *   建议设为 chunk_size 的 10%-20%。
 * - preserve_sentences: 尽量在句子边界切断（。！？\n），
 *   避免在句子中间切断导致语义不完整。
 */
struct ChunkConfig {
    int chunk_size = 500;           ///< 每块最大字符数
    int chunk_overlap = 50;         ///< 相邻块重叠字符数
    bool preserve_sentences = true; ///< 是否在句子边界切断
};

/**
 * @brief 文档加载结果
 *
 * 用于报告加载过程的统计信息。
 */
struct LoadResult {
    std::string source_file;   ///< 源文件路径
    int total_chunks = 0;      ///< 生成的块数
    int total_chars = 0;       ///< 提取的总字符数
    bool success = false;      ///< 是否成功
    std::string error_message; ///< 失败原因
};

/**
 * @brief 文档加载与分块类（Pimpl 模式）
 *
 * 负责：
 * 1. 根据文件扩展名选择对应的解析器（PDF/TXT/MD/JSON）
 * 2. 将提取的文本按 ChunkConfig 切割成多个 DocumentChunk
 * 3. 每个 chunk 附带 metadata（来源文件、页码、字符位置等）
 *
 * 注意：
 * - 此阶段只生成文本块，不生成 Embedding 向量
 * - Embedding 向量由调用方结合 EmbeddingClient 单独生成
 * - 设计为无状态（除 ChunkConfig 外），可安全复用
 */
class DocumentLoader {
public:
    /// @brief 实现类前向声明（Pimpl 模式）
    struct Impl;

    DocumentLoader();
    ~DocumentLoader();

    // ========================================================
    // 分块配置
    // ========================================================

    /**
     * @brief 设置分块参数
     *
     * 在加载文档前调用，控制分块行为。
     * 默认：500 字符/块，50 字符重叠，句子边界优先。
     *
     * @param config 分块配置
     */
    void SetChunkConfig(const ChunkConfig& config);

    /**
     * @brief 获取当前分块配置
     */
    const ChunkConfig& GetChunkConfig() const;

    // ========================================================
    // 文档加载
    // ========================================================

    /**
     * @brief 加载单个文档并分块
     *
     * 根据文件扩展名自动选择解析器：
     * - .pdf → PoDoFo 逐页提取
     * - .txt → 直接读取（自动检测 BOM）
     * - .md  → 读取并清洗 Markdown 格式符号
     * - .json → 解析结构化数据
     *
     * @param file_path 文件完整路径
     * @param source_name 来源标识（用于 chunk_id 前缀），为空则用文件名
     * @return 文档块列表（不含 Embedding 向量）
     * @throws std::runtime_error 文件不存在或解析失败
     */
    std::vector<DocumentChunk> LoadDocument(
        const std::string& file_path,
        const std::string& source_name = "");

    /**
     * @brief 加载目录下的所有支持格式文档
     *
     * 遍历目录，对每个支持的文件调用 LoadDocument。
     * 子目录不递归（仅当前层级）。
     *
     * @param dir_path 目录路径
     * @param kb_name 知识库名称（用于 metadata），为空则不设
     * @return 所有文件的文档块汇总
     */
    std::vector<DocumentChunk> LoadDirectory(
        const std::string& dir_path,
        const std::string& kb_name = "");

    // ========================================================
    // 格式判断
    // ========================================================

    /**
     * @brief 判断文件格式是否支持
     *
     * 依据文件扩展名（不区分大小写）。
     *
     * @param file_path 文件路径
     * @return true 支持该格式
     */
    static bool IsSupportedFormat(const std::string& file_path);

    /**
     * @brief 获取所有支持的扩展名
     *
     * @return 扩展名列表，如 {".pdf", ".txt", ".md", ".json"}
     */
    static std::vector<std::string> GetSupportedExtensions();

    /// @brief 禁用拷贝
    DocumentLoader(const DocumentLoader&) = delete;
    DocumentLoader& operator=(const DocumentLoader&) = delete;

private:
    std::unique_ptr<Impl> impl_;
};

} // namespace services
} // namespace interview
