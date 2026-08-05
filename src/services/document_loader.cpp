/**
 * @file document_loader.cpp
 * @brief 知识库文档读取、清洗与分块实现。
 *
 * 主要模块：多格式解析、UTF-8 路径处理、文本清洗和滑动窗口分块。
 */

#include "services/document_loader.h"
#include "services/pdf_parser.h"
#include "common/logger.h"
#include <fstream>
#include <iterator>
#include <sstream>
#include <algorithm>
#include <filesystem>
#include <nlohmann/json.hpp>

namespace interview {
namespace services {

namespace fs = std::filesystem;

// ============================================================
// 辅助函数：获取文件扩展名（小写）
// ============================================================
static std::string GetExtension(const std::string& path) {
    std::string ext;
    size_t dot = path.rfind('.');
    if (dot != std::string::npos) {
        ext = path.substr(dot);
        // 转小写
        std::transform(ext.begin(), ext.end(), ext.begin(),
                       [](unsigned char c) { return std::tolower(c); });
    }
    return ext;
}

// ============================================================
// 辅助函数：从路径提取文件名（不含目录）
// ============================================================
static std::string GetFileName(const std::string& path) {
    size_t slash = path.find_last_of("/\\");
    if (slash != std::string::npos) {
        return path.substr(slash + 1);
    }
    return path;
}

// ============================================================
// 辅助函数：去除 UTF-8 BOM
// ============================================================
static std::string StripBOM(const std::string& content) {
    if (content.size() >= 3 &&
        static_cast<unsigned char>(content[0]) == 0xEF &&
        static_cast<unsigned char>(content[1]) == 0xBB &&
        static_cast<unsigned char>(content[2]) == 0xBF) {
        return content.substr(3);
    }
    return content;
}

// ============================================================
// 辅助函数：判断字符是否为中文句子结尾标点
// ============================================================
static bool IsUtf8ContinuationByte(char value) {
    return (static_cast<unsigned char>(value) & 0xC0) == 0x80;
}

static size_t AlignUtf8BoundaryBackward(const std::string& text, size_t pos) {
    if (pos >= text.size()) return text.size();
    while (pos > 0 && IsUtf8ContinuationByte(text[pos])) {
        --pos;
    }
    return pos;
}

static size_t AlignUtf8BoundaryForward(const std::string& text, size_t pos) {
    while (pos < text.size() && IsUtf8ContinuationByte(text[pos])) {
        ++pos;
    }
    return pos;
}

static bool IsSentenceEnd(char32_t c) {
    return c == U'。' || c == U'！' || c == U'？' ||
           c == U'\n' || c == U'.'  || c == U'!'  || c == U'?' ||
           c == U'；' || c == U'…';
}

// ============================================================
// 辅助函数：UTF-8 字符序列 → char32_t（简化实现）
// ============================================================
static char32_t Utf8ToChar32(const std::string& utf8, size_t& pos) {
    unsigned char c = static_cast<unsigned char>(utf8[pos]);
    char32_t result = 0;
    int len = 0;

    if ((c & 0x80) == 0) {
        result = c;
        len = 1;
    } else if ((c & 0xE0) == 0xC0) {
        result = c & 0x1F;
        len = 2;
    } else if ((c & 0xF0) == 0xE0) {
        result = c & 0x0F;
        len = 3;
    } else if ((c & 0xF8) == 0xF0) {
        result = c & 0x07;
        len = 4;
    } else {
        pos++;
        return 0xFFFD;  // 替换字符
    }

    for (int i = 1; i < len && (pos + i) < utf8.size(); i++) {
        unsigned char cc = static_cast<unsigned char>(utf8[pos + i]);
        if ((cc & 0xC0) != 0x80) {
            pos++;
            return 0xFFFD;
        }
        result = (result << 6) | (cc & 0x3F);
    }

    pos += len;
    return result;
}

// ============================================================
// 辅助函数：在文本中向前查找最近的句子边界
// ============================================================
static size_t FindLastSentenceBoundary(const std::string& text,
                                        size_t search_start) {
    // 在 search_start 之前找最近的句子结束符
    size_t boundary = std::string::npos;

    // 逐个字符向前扫描
    size_t pos = 0;
    while (pos < search_start && pos < text.size()) {
        char32_t ch = Utf8ToChar32(text, pos);
        if (IsSentenceEnd(ch)) {
            boundary = pos;  // pos 已指向当前字符的下一个位置
        }
    }

    return boundary;
}

// ============================================================
// 辅助函数：Markdown 清洗
// ============================================================
static std::string CleanMarkdown(const std::string& raw) {
    std::string result;
    result.reserve(raw.size());

    bool in_code_block = false;
    size_t i = 0;

    while (i < raw.size()) {
        // 检测代码块标记 ```
        if (i + 2 < raw.size() &&
            raw[i] == '`' && raw[i+1] == '`' && raw[i+2] == '`') {
            in_code_block = !in_code_block;
            i += 3;
            // 跳过语言标识符（如 ```cpp）
            while (i < raw.size() && raw[i] != '\n') i++;
            continue;
        }

        if (in_code_block) {
            // 代码块内容保留
            result += raw[i];
            i++;
            continue;
        }

        // 清洗行内格式符号
        char c = raw[i];

        // 跳过 ** 和 __（粗体标记）
        if (c == '*' || c == '_') {
            if (i + 1 < raw.size() && raw[i+1] == c) {
                i += 2;  // 跳过 ** 或 __
                continue;
            }
        }

        // 跳过 `（行内代码标记）
        if (c == '`') {
            i++;
            continue;
        }

        // 跳过链接的 [] 和 ()
        if (c == '[' || c == ']') {
            // 简单处理：保留文本，跳过括号本身
            // 对于 [text](url) 格式，保留 text 跳过括号
            i++;
            continue;
        }

        result += c;
        i++;
    }

    return result;
}

// ============================================================
// Pimpl 实现类
// ============================================================
class DocumentLoader::Impl {
public:
    ChunkConfig chunk_config_;

    // ========================================================
    // 加载文档（根据扩展名分发）
    // ========================================================
    std::vector<DocumentChunk> LoadDocument(
        const std::string& file_path,
        const std::string& source_name) {

        std::string ext = GetExtension(file_path);
        std::string name = source_name.empty()
            ? GetFileName(file_path) : source_name;

        std::string text;
        int page_count = 0;

        if (ext == ".txt") {
            text = ReadTextFile(file_path);
        } else if (ext == ".md") {
            text = ReadTextFile(file_path);
            if (text.empty()) {
                LOG_WARN("DocumentLoader: empty text from '{}'", file_path);
                return {};
            }
            LOG_INFO("DocumentLoader: loaded Markdown '{}', {} bytes",
                     file_path, text.size());
            return ChunkMarkdown(text, name);
        } else if (ext == ".pdf") {
            auto result = ReadPDFFile(file_path);
            text = result.first;
            page_count = result.second;
        } else if (ext == ".json") {
            return LoadJSONFile(file_path, name);
        } else {
            LOG_ERROR("DocumentLoader: unsupported format '{}'", ext);
            return {};
        }

        if (text.empty()) {
            LOG_WARN("DocumentLoader: empty text from '{}'", file_path);
            return {};
        }

        LOG_INFO("DocumentLoader: loaded '{}', {} chars, {} pages",
                 file_path, text.size(), page_count);

        // 分块
        auto chunks = ChunkText(text, name);

        // 为 PDF 标注页码
        if (page_count > 0) {
            AnnotatePDFPageNumbers(chunks, text, page_count);
        }

        return chunks;
    }

    // ========================================================
    // 加载目录
    // ========================================================
    std::vector<DocumentChunk> LoadDirectory(
        const std::string& dir_path,
        const std::string& kb_name) {

        std::vector<DocumentChunk> all_chunks;
        const fs::path directory_path = fs::u8path(dir_path);

        if (!fs::exists(directory_path) || !fs::is_directory(directory_path)) {
            LOG_ERROR("DocumentLoader: directory not found: '{}'", dir_path);
            return all_chunks;
        }

        int file_count = 0;
        for (const auto& entry : fs::directory_iterator(directory_path)) {
            if (!entry.is_regular_file()) continue;

            std::string file_path = entry.path().u8string();
            if (!IsSupportedFormat(file_path)) continue;

            std::string source_name = kb_name.empty()
                ? GetFileName(file_path)
                : kb_name + "/" + GetFileName(file_path);

            auto chunks = LoadDocument(file_path, source_name);
            all_chunks.insert(all_chunks.end(), chunks.begin(), chunks.end());
            file_count++;
        }

        LOG_INFO("DocumentLoader: loaded {} files from '{}', {} chunks total",
                 file_count, dir_path, all_chunks.size());
        return all_chunks;
    }

    // ========================================================
    // 格式判断
    // ========================================================
    static bool IsSupportedFormat(const std::string& file_path) {
        std::string ext = GetExtension(file_path);
        return ext == ".pdf" || ext == ".txt" ||
               ext == ".md"  || ext == ".json";
    }

    static std::vector<std::string> GetSupportedExtensions() {
        return {".pdf", ".txt", ".md", ".json"};
    }

private:
    // ========================================================
    // TXT 读取
    // ========================================================
    std::string ReadTextFile(const std::string& file_path) {
        std::ifstream file(fs::u8path(file_path),
                           std::ios::in | std::ios::binary);
        if (!file.is_open()) {
            LOG_ERROR("DocumentLoader: cannot open file '{}'", file_path);
            return "";
        }

        std::ostringstream buffer;
        buffer << file.rdbuf();
        std::string content = buffer.str();

        // 去除 BOM
        content = StripBOM(content);

        return content;
    }

    // ========================================================
    // PDF 读取（复用现有 PDFParser）
    // ========================================================
    std::pair<std::string, int> ReadPDFFile(const std::string& file_path) {
        try {
            PDFParser parser;
            std::string text = parser.ExtractText(file_path);

            // 估算页数（从文本中的分页标记推断）
            // PDFParser 在页之间插入 "\n\n"，通过空行分段估算
            int page_count = 1;
            size_t pos = 0;
            while ((pos = text.find("\n\n", pos)) != std::string::npos) {
                page_count++;
                pos += 2;
            }

            return {text, page_count};
        } catch (const std::exception& e) {
            LOG_ERROR("DocumentLoader: PDF parse error: {}", e.what());
            return {"", 0};
        }
    }

    // ========================================================
    // JSON 文件加载（结构化 Q&A 对）
    // ========================================================
    std::vector<DocumentChunk> LoadJSONFile(
        const std::string& file_path,
        const std::string& source_name) {

        std::vector<DocumentChunk> chunks;
        std::string raw = ReadTextFile(file_path);
        if (raw.empty()) return chunks;

        try {
            auto json = nlohmann::json::parse(raw);

            // 格式1：数组 [{question, answer}, ...]
            if (json.is_array()) {
                for (size_t i = 0; i < json.size(); i++) {
                    const auto& item = json[i];
                    DocumentChunk chunk;
                    chunk.chunk_id = source_name + "_qa" + std::to_string(i);

                    // 组合 Q&A 为完整内容
                    std::string q = item.value("question",
                        item.value("q", ""));
                    std::string a = item.value("answer",
                        item.value("a", ""));

                    if (!q.empty() && !a.empty()) {
                        chunk.content = "问：" + q + "\n答：" + a;
                    } else {
                        chunk.content = item.dump();  // 退一步：保存原始 JSON
                    }

                    chunk.metadata = nlohmann::json({
                        {"source", source_name},
                        {"chunk_index", i},
                        {"format", "json_qa"}
                    }).dump();

                    chunks.push_back(chunk);
                }
            }
            // 格式2：对象，扁平化为文本
            else if (json.is_object()) {
                DocumentChunk chunk;
                chunk.chunk_id = source_name + "_chunk0";
                chunk.content = json.dump(2);  // 格式化 JSON 文本
                chunk.metadata = nlohmann::json({
                    {"source", source_name},
                    {"chunk_index", 0},
                    {"format", "json_object"}
                }).dump();
                chunks.push_back(chunk);
            }
        } catch (const nlohmann::json::exception& e) {
            LOG_ERROR("DocumentLoader: JSON parse error: {}", e.what());
        }

        return chunks;
    }

    // ========================================================
    // 核心算法：滑动窗口分块
    // ========================================================
    std::vector<DocumentChunk> ChunkMarkdown(
        const std::string& markdown,
        const std::string& source_name) {

        struct MarkdownSection {
            std::string heading;
            std::string content;
        };

        std::vector<MarkdownSection> sections;
        MarkdownSection current;
        bool in_code_block = false;
        bool found_heading = false;
        size_t line_start = 0;

        auto flush_section = [&]() {
            if (!current.content.empty()) {
                sections.push_back(std::move(current));
                current = MarkdownSection{};
            }
        };

        while (line_start < markdown.size()) {
            size_t line_end = markdown.find('\n', line_start);
            if (line_end == std::string::npos) {
                line_end = markdown.size();
            } else {
                ++line_end;  // 保留换行，避免标题和正文粘连
            }

            const std::string line =
                markdown.substr(line_start, line_end - line_start);
            size_t marker = 0;
            while (marker < line.size() && marker < 3 &&
                   line[marker] == ' ') {
                ++marker;
            }

            const bool is_fence =
                line.compare(marker, 3, "```") == 0 ||
                line.compare(marker, 3, "~~~") == 0;

            size_t hash_end = marker;
            while (hash_end < line.size() &&
                   hash_end - marker < 6 &&
                   line[hash_end] == '#') {
                ++hash_end;
            }
            const bool is_heading =
                !in_code_block &&
                hash_end > marker &&
                hash_end - marker <= 6 &&
                (hash_end == line.size() ||
                 line[hash_end] == ' ' ||
                 line[hash_end] == '\t' ||
                 line[hash_end] == '\r' ||
                 line[hash_end] == '\n');

            if (is_heading) {
                flush_section();
                found_heading = true;

                size_t title_start = hash_end;
                while (title_start < line.size() &&
                       (line[title_start] == ' ' ||
                        line[title_start] == '\t')) {
                    ++title_start;
                }
                size_t title_end = line.find_last_not_of(" \t\r\n#");
                if (title_end != std::string::npos &&
                    title_end >= title_start) {
                    current.heading = line.substr(
                        title_start, title_end - title_start + 1);
                }
            }

            current.content += line;
            if (is_fence) {
                in_code_block = !in_code_block;
            }
            line_start = line_end;
        }
        flush_section();

        if (!found_heading) {
            return ChunkText(CleanMarkdown(markdown), source_name);
        }

        std::vector<DocumentChunk> chunks;
        int next_chunk_index = 0;
        for (const auto& section : sections) {
            std::string cleaned = CleanMarkdown(section.content);
            const size_t first = cleaned.find_first_not_of(" \t\r\n");
            if (first == std::string::npos) {
                continue;
            }
            const size_t last = cleaned.find_last_not_of(" \t\r\n");
            cleaned = cleaned.substr(first, last - first + 1);

            auto section_chunks = ChunkText(
                cleaned, source_name, &next_chunk_index, section.heading);
            chunks.insert(
                chunks.end(),
                std::make_move_iterator(section_chunks.begin()),
                std::make_move_iterator(section_chunks.end()));
        }

        LOG_INFO(
            "DocumentLoader: Markdown '{}' split into {} section(s), "
            "{} chunk(s)",
            source_name, sections.size(), chunks.size());
        return chunks;
    }

    std::vector<DocumentChunk> ChunkText(
        const std::string& text,
        const std::string& source_name,
        int* next_chunk_index = nullptr,
        const std::string& markdown_heading = "") {

        std::vector<DocumentChunk> chunks;
        if (text.empty()) return chunks;

        size_t text_len = text.size();
        size_t start = 0;
        int chunk_index = next_chunk_index ? *next_chunk_index : 0;
        const int chunk_size = chunk_config_.chunk_size;
        const int overlap = chunk_config_.chunk_overlap;

        // 预估块数，提前分配空间
        chunks.reserve(text_len / (chunk_size - overlap) + 2);

        while (start < text_len) {
            // 1. 确定理想结束位置
            size_t end = start + chunk_size;

            // 2. 最后一块：直接取到末尾
            if (end >= text_len) {
                end = text_len;
            }
            // 3. 尽量在句子边界切断
            else if (chunk_config_.preserve_sentences) {
                // 在 [start + chunk_size*40%, end] 范围内找边界
                size_t search_from = start + chunk_size * 40 / 100;
                if (search_from < start + 1) search_from = start + 1;

                // 向后搜索句子结束符（在中文字符范围内的安全搜索）
                size_t best_boundary = std::string::npos;
                for (size_t i = end; i > search_from && i > 0; i--) {
                    unsigned char c = static_cast<unsigned char>(text[i]);
                    // 只检查单字节字符和常见标点
                    if (c < 0x80) {
                        if (text[i] == '\n') {
                            best_boundary = i + 1;  // 包含换行符
                            break;
                        }
                        if (text[i] == '.' || text[i] == '!' || text[i] == '?') {
                            best_boundary = i + 1;
                            break;
                        }
                    }
                    // 中文标点（UTF-8 多字节）
                    if (c == 0xE3) {  // U+3000 范围开头（。！？在此范围内）
                        if (i + 2 < text_len) {
                            unsigned char c1 = static_cast<unsigned char>(text[i+1]);
                            unsigned char c2 = static_cast<unsigned char>(text[i+2]);
                            // 。= E3 80 82, ！= EF BC 81, ？= EF BC 9F
                            if ((c1 == 0x80 && c2 == 0x82) ||  // 。
                                (c1 == 0x80 && c2 == 0x81)) {   // 、
                                best_boundary = i + 3;
                                break;
                            }
                        }
                    }
                    if (c == 0xEF) {
                        if (i + 2 < text_len) {
                            unsigned char c1 = static_cast<unsigned char>(text[i+1]);
                            unsigned char c2 = static_cast<unsigned char>(text[i+2]);
                            if ((c1 == 0xBC && c2 == 0x81) ||  // ！
                                (c1 == 0xBC && c2 == 0x9F)) {  // ？
                                best_boundary = i + 3;
                                break;
                            }
                        }
                    }
                }

                if (best_boundary != std::string::npos &&
                    best_boundary > start + chunk_size / 3) {
                    end = best_boundary;
                }
                // 否则保持 end 不变（在 chunk_size 处硬切）
            }

            // 4. 提取文本并创建 chunk
            // Keep byte-based windows aligned to complete UTF-8 characters.
            end = AlignUtf8BoundaryBackward(text, end);
            if (end <= start) {
                end = AlignUtf8BoundaryForward(text, start + 1);
            }

            DocumentChunk chunk;
            chunk.chunk_id = source_name + "_chunk" + std::to_string(chunk_index);
            chunk.content = text.substr(start, end - start);
            nlohmann::json metadata = {
                {"source", source_name},
                {"chunk_index", chunk_index},
                {"char_start", start},
                {"char_end", end},
                {"char_count", end - start}
            };
            if (!markdown_heading.empty()) {
                metadata["format"] = "markdown_section";
                metadata["heading"] = markdown_heading;
            }
            chunk.metadata = metadata.dump();

            chunks.push_back(chunk);
            chunk_index++;

            // 已经覆盖当前文本（或当前 Markdown 标题段）的末尾时立即
            // 结束。否则 overlap 会把最后几十个字反复生成递减的碎片块。
            if (end >= text_len) {
                break;
            }

            // 5. 移动窗口
            size_t next_start = end > static_cast<size_t>(overlap)
                ? end - static_cast<size_t>(overlap)
                : 0;
            next_start = AlignUtf8BoundaryForward(text, next_start);

            // 防止死循环
            if (next_start <= start) {
                next_start = AlignUtf8BoundaryForward(text, start + 1);
            }
            if (next_start >= text_len) {
                break;
            }

            start = next_start;
        }

        LOG_DEBUG("DocumentLoader: chunked '{}' into {} chunks "
                  "(config: size={}, overlap={}, preserve_sentences={})",
                  source_name, chunks.size(), chunk_size, overlap,
                  chunk_config_.preserve_sentences);

        if (next_chunk_index) {
            *next_chunk_index = chunk_index;
        }

        return chunks;
    }

    // ========================================================
    // PDF 页码标注
    // ========================================================
    void AnnotatePDFPageNumbers(
        std::vector<DocumentChunk>& chunks,
        const std::string& /* full_text */,
        int page_count) {

        // 简化实现：按 chunk 数量均分页码
        if (chunks.empty() || page_count <= 1) return;

        float ratio = static_cast<float>(page_count) / chunks.size();

        for (size_t i = 0; i < chunks.size(); i++) {
            int page = static_cast<int>(i * ratio) + 1;
            if (page > page_count) page = page_count;

            try {
                auto meta = nlohmann::json::parse(chunks[i].metadata);
                meta["page"] = page;
                meta["total_pages"] = page_count;
                chunks[i].metadata = meta.dump();
            } catch (...) {
                // 保持原有 metadata
            }
        }
    }
};

// ============================================================
// 公开接口（Pimpl 转发）
// ============================================================

DocumentLoader::DocumentLoader()
    : impl_(std::make_unique<Impl>()) {
}

DocumentLoader::~DocumentLoader() = default;

void DocumentLoader::SetChunkConfig(const ChunkConfig& config) {
    impl_->chunk_config_ = config;
}

const ChunkConfig& DocumentLoader::GetChunkConfig() const {
    return impl_->chunk_config_;
}

std::vector<DocumentChunk> DocumentLoader::LoadDocument(
    const std::string& file_path,
    const std::string& source_name) {
    return impl_->LoadDocument(file_path, source_name);
}

std::vector<DocumentChunk> DocumentLoader::LoadDirectory(
    const std::string& dir_path,
    const std::string& kb_name) {
    return impl_->LoadDirectory(dir_path, kb_name);
}

bool DocumentLoader::IsSupportedFormat(const std::string& file_path) {
    return Impl::IsSupportedFormat(file_path);
}

std::vector<std::string> DocumentLoader::GetSupportedExtensions() {
    return Impl::GetSupportedExtensions();
}

} // namespace services
} // namespace interview
