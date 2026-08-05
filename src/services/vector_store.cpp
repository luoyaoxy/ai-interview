/**
 * @file vector_store.cpp
 * @brief SQLite 向量库及相似度检索实现。
 *
 * 主要模块：数据库生命周期、知识库管理、文档块存储和余弦相似度搜索。
 */

#include "services/vector_store.h"
#include "common/logger.h"
#include <sqlite3.h>
#include <nlohmann/json.hpp>
#include <algorithm>
#include <cmath>
#include <queue>
#include <set>
#include <sstream>

namespace interview {
namespace services {

// ============================================================
// 辅助函数：余弦相似度计算
// ============================================================
//
// 公式：cos(θ) = (A·B) / (|A| * |B|)
// 返回值范围 [0, 1]，越大表示两个向量越"相近"
//
// 性能：1536 维向量约 3-5 微秒（现代 CPU）

static float CosineSimilarity(
    const std::vector<float>& a,
    const std::vector<float>& b) {

    if (a.size() != b.size() || a.empty()) {
        return 0.0f;
    }

    float dot = 0.0f, norm_a = 0.0f, norm_b = 0.0f;

    for (size_t i = 0; i < a.size(); i++) {
        dot   += a[i] * b[i];
        norm_a += a[i] * a[i];
        norm_b += b[i] * b[i];
    }

    if (norm_a == 0.0f || norm_b == 0.0f) {
        return 0.0f;
    }

    return dot / (std::sqrt(norm_a) * std::sqrt(norm_b));
}

// ============================================================
// 辅助函数：将 vector<float> 序列化为 JSON 字符串
// ============================================================
static std::string SerializeEmbedding(const std::vector<float>& vec) {
    nlohmann::json j = vec;
    return j.dump();
}

// ============================================================
// 辅助函数：从 JSON 字符串反序列化为 vector<float>
// ============================================================
static std::vector<float> DeserializeEmbedding(const std::string& json_str) {
    try {
        auto j = nlohmann::json::parse(json_str);
        return j.get<std::vector<float>>();
    } catch (const nlohmann::json::exception& e) {
        LOG_ERROR("VectorStore: failed to deserialize embedding: {}", e.what());
        return {};
    }
}

// ============================================================
// 辅助函数：SQLite 错误日志
// ============================================================
static void LogSqliteError(sqlite3* db, const char* context) {
    LOG_ERROR("VectorStore SQLite error [{}]: {}",
              context, sqlite3_errmsg(db));
}

// ============================================================
// Pimpl 实现类
// ============================================================
class VectorStore::Impl {
public:
    sqlite3* db_ = nullptr;
    int active_kb_id_ = -1;
    bool is_open_ = false;

    // 建表 SQL
    static constexpr const char* kCreateKBTable = R"SQL(
        CREATE TABLE IF NOT EXISTS knowledge_bases (
            id          INTEGER PRIMARY KEY AUTOINCREMENT,
            name        TEXT NOT NULL,
            description TEXT DEFAULT '',
            role_type   TEXT DEFAULT 'default',
            created_at  TEXT DEFAULT (datetime('now')),
            updated_at  TEXT DEFAULT (datetime('now'))
        );
    )SQL";

    static constexpr const char* kCreateChunksTable = R"SQL(
        CREATE TABLE IF NOT EXISTS document_chunks (
            id          INTEGER PRIMARY KEY AUTOINCREMENT,
            kb_id       INTEGER NOT NULL,
            chunk_id    TEXT NOT NULL,
            source_file TEXT NOT NULL,
            page_number INTEGER DEFAULT 0,
            content     TEXT NOT NULL,
            embedding   TEXT NOT NULL,
            metadata    TEXT DEFAULT '{}',
            char_count  INTEGER DEFAULT 0,
            created_at  TEXT DEFAULT (datetime('now')),
            updated_at  TEXT DEFAULT (datetime('now')),
            FOREIGN KEY (kb_id) REFERENCES knowledge_bases(id)
                ON DELETE CASCADE,
            UNIQUE(kb_id, chunk_id)
        );
    )SQL";

    static constexpr const char* kCreateSettingsTable = R"SQL(
        CREATE TABLE IF NOT EXISTS app_settings (
            key   TEXT PRIMARY KEY,
            value TEXT NOT NULL
        );
    )SQL";

    static constexpr const char* kIndexKBID =
        "CREATE INDEX IF NOT EXISTS idx_chunks_kb_id "
        "ON document_chunks(kb_id);";

    static constexpr const char* kIndexChunkID =
        "CREATE INDEX IF NOT EXISTS idx_chunks_chunk_id "
        "ON document_chunks(chunk_id);";

    // ========================================================
    // 打开数据库
    // ========================================================
    bool Open(const std::string& db_path) {
        if (is_open_) {
            LOG_WARN("VectorStore: database already open, closing first");
            Close();
        }

        int rc = sqlite3_open_v2(
            db_path.c_str(), &db_,
            SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE | SQLITE_OPEN_FULLMUTEX,
            nullptr);

        if (rc != SQLITE_OK) {
            LOG_ERROR("VectorStore: failed to open database: {}",
                      db_ ? sqlite3_errmsg(db_) : "unknown error");
            if (db_) { sqlite3_close(db_); db_ = nullptr; }
            return false;
        }

        // 启用 WAL 模式（Write-Ahead Logging）：提升并发性能
        char* err_msg = nullptr;
        rc = sqlite3_exec(db_, "PRAGMA journal_mode=WAL;", nullptr, nullptr, &err_msg);
        if (rc != SQLITE_OK) {
            LOG_WARN("VectorStore: failed to enable WAL mode: {}",
                     err_msg ? err_msg : "unknown");
            sqlite3_free(err_msg);
        }

        // 启用外键约束
        rc = sqlite3_exec(db_, "PRAGMA foreign_keys=ON;", nullptr, nullptr, &err_msg);
        if (rc != SQLITE_OK) {
            LOG_WARN("VectorStore: failed to enable foreign keys: {}",
                     err_msg ? err_msg : "unknown");
            sqlite3_free(err_msg);
        }

        // 建表
        if (!CreateTables()) {
            sqlite3_close(db_);
            db_ = nullptr;
            return false;
        }

        is_open_ = true;
        RestoreActiveKnowledgeBase();
        LOG_INFO("VectorStore: opened database '{}'", db_path);
        return true;
    }

    // ========================================================
    // 关闭数据库
    // ========================================================
    void Close() {
        if (db_) {
            sqlite3_close(db_);
            db_ = nullptr;
        }
        is_open_ = false;
        active_kb_id_ = -1;
        LOG_INFO("VectorStore: database closed");
    }

    // ========================================================
    // 建表
    // ========================================================
    bool CreateTables() {
        char* err_msg = nullptr;

        const char* statements[] = {
            kCreateKBTable,
            kCreateSettingsTable
        };

        for (const auto& sql : statements) {
            int rc = sqlite3_exec(db_, sql, nullptr, nullptr, &err_msg);
            if (rc != SQLITE_OK) {
                LOG_ERROR("VectorStore: table creation failed: {}",
                          err_msg ? err_msg : "unknown error");
                sqlite3_free(err_msg);
                return false;
            }
        }

        if (!EnsureChunksSchema()) {
            return false;
        }

        const char* index_statements[] = {
            kIndexKBID,
            kIndexChunkID
        };

        for (const auto& sql : index_statements) {
            int rc = sqlite3_exec(db_, sql, nullptr, nullptr, &err_msg);
            if (rc != SQLITE_OK) {
                LOG_ERROR("VectorStore: table creation failed: {}",
                          err_msg ? err_msg : "unknown error");
                sqlite3_free(err_msg);
                return false;
            }
        }

        return true;
    }

    bool EnsureChunksSchema() {
        sqlite3_stmt* stmt = nullptr;
        int rc = sqlite3_prepare_v2(
            db_,
            "SELECT sql FROM sqlite_master "
            "WHERE type='table' AND name='document_chunks';",
            -1, &stmt, nullptr);
        if (rc != SQLITE_OK) {
            LogSqliteError(db_, "Inspect document_chunks schema");
            return false;
        }

        std::string schema;
        if (sqlite3_step(stmt) == SQLITE_ROW) {
            const auto* text = sqlite3_column_text(stmt, 0);
            if (text) {
                schema = reinterpret_cast<const char*>(text);
            }
        }
        sqlite3_finalize(stmt);

        if (schema.empty()) {
            return sqlite3_exec(
                       db_, kCreateChunksTable, nullptr, nullptr, nullptr) ==
                   SQLITE_OK;
        }

        const bool has_composite_unique =
            schema.find("UNIQUE(kb_id, chunk_id)") != std::string::npos ||
            schema.find("UNIQUE (kb_id, chunk_id)") != std::string::npos;
        const bool has_updated_at =
            schema.find("updated_at") != std::string::npos;
        if (has_composite_unique && has_updated_at) {
            return true;
        }

        LOG_INFO("VectorStore: migrating document_chunks schema");
        char* err_msg = nullptr;
        const char* begin_sql =
            "BEGIN IMMEDIATE;"
            "ALTER TABLE document_chunks RENAME TO document_chunks_legacy;";
        rc = sqlite3_exec(db_, begin_sql, nullptr, nullptr, &err_msg);
        if (rc != SQLITE_OK) {
            LOG_ERROR("VectorStore: migration start failed: {}",
                      err_msg ? err_msg : "unknown error");
            sqlite3_free(err_msg);
            return false;
        }

        rc = sqlite3_exec(db_, kCreateChunksTable, nullptr, nullptr, &err_msg);
        if (rc == SQLITE_OK) {
            rc = sqlite3_exec(
                db_,
                "INSERT OR REPLACE INTO document_chunks "
                "(id, kb_id, chunk_id, source_file, page_number, content, "
                " embedding, metadata, char_count, created_at, updated_at) "
                "SELECT id, kb_id, chunk_id, source_file, page_number, content, "
                " embedding, metadata, char_count, created_at, created_at "
                "FROM document_chunks_legacy;"
                "DROP TABLE document_chunks_legacy;"
                "COMMIT;",
                nullptr, nullptr, &err_msg);
        }

        if (rc != SQLITE_OK) {
            LOG_ERROR("VectorStore: migration failed: {}",
                      err_msg ? err_msg : "unknown error");
            sqlite3_free(err_msg);
            sqlite3_exec(db_, "ROLLBACK;", nullptr, nullptr, nullptr);
            return false;
        }
        return true;
    }

    void SaveActiveKnowledgeBase() {
        if (!db_) return;
        sqlite3_stmt* stmt = nullptr;
        if (sqlite3_prepare_v2(
                db_,
                "INSERT INTO app_settings(key, value) VALUES('active_kb_id', ?) "
                "ON CONFLICT(key) DO UPDATE SET value=excluded.value;",
                -1, &stmt, nullptr) != SQLITE_OK) {
            return;
        }
        sqlite3_bind_text(
            stmt, 1, std::to_string(active_kb_id_).c_str(),
            -1, SQLITE_TRANSIENT);
        sqlite3_step(stmt);
        sqlite3_finalize(stmt);
    }

    int FindFirstKnowledgeBase() {
        sqlite3_stmt* stmt = nullptr;
        int id = -1;
        if (sqlite3_prepare_v2(
                db_, "SELECT id FROM knowledge_bases ORDER BY id LIMIT 1;",
                -1, &stmt, nullptr) == SQLITE_OK) {
            if (sqlite3_step(stmt) == SQLITE_ROW) {
                id = sqlite3_column_int(stmt, 0);
            }
        }
        sqlite3_finalize(stmt);
        return id;
    }

    bool KnowledgeBaseExists(int kb_id) {
        sqlite3_stmt* stmt = nullptr;
        bool exists = false;
        if (sqlite3_prepare_v2(
                db_, "SELECT 1 FROM knowledge_bases WHERE id=?;",
                -1, &stmt, nullptr) == SQLITE_OK) {
            sqlite3_bind_int(stmt, 1, kb_id);
            exists = sqlite3_step(stmt) == SQLITE_ROW;
        }
        sqlite3_finalize(stmt);
        return exists;
    }

    void RestoreActiveKnowledgeBase() {
        sqlite3_stmt* stmt = nullptr;
        int saved_id = -1;
        if (sqlite3_prepare_v2(
                db_,
                "SELECT value FROM app_settings WHERE key='active_kb_id';",
                -1, &stmt, nullptr) == SQLITE_OK) {
            if (sqlite3_step(stmt) == SQLITE_ROW) {
                const auto* value = sqlite3_column_text(stmt, 0);
                if (value) {
                    try {
                        saved_id = std::stoi(
                            reinterpret_cast<const char*>(value));
                    } catch (...) {
                        saved_id = -1;
                    }
                }
            }
        }
        sqlite3_finalize(stmt);

        active_kb_id_ = KnowledgeBaseExists(saved_id)
            ? saved_id
            : FindFirstKnowledgeBase();
        SaveActiveKnowledgeBase();
    }

    void SetActiveKnowledgeBaseID(int kb_id) {
        if (!is_open_ || !KnowledgeBaseExists(kb_id)) {
            LOG_WARN("VectorStore: cannot activate missing kb_id={}", kb_id);
            return;
        }
        active_kb_id_ = kb_id;
        SaveActiveKnowledgeBase();
    }

    // ========================================================
    // 知识库 CRUD
    // ========================================================

    int CreateKnowledgeBase(const std::string& name,
                            const std::string& description,
                            const std::string& role_type) {
        if (!is_open_ || !db_) {
            LOG_ERROR("VectorStore: database not open");
            return -1;
        }
        if (name.empty()) {
            LOG_ERROR("VectorStore: knowledge base name cannot be empty");
            return -1;
        }

        const char* sql =
            "INSERT INTO knowledge_bases (name, description, role_type) "
            "VALUES (?, ?, ?);";

        sqlite3_stmt* stmt = nullptr;
        int rc = sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr);
        if (rc != SQLITE_OK) {
            LogSqliteError(db_, "CreateKnowledgeBase prepare");
            return -1;
        }

        sqlite3_bind_text(stmt, 1, name.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 2, description.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 3, role_type.c_str(), -1, SQLITE_TRANSIENT);

        rc = sqlite3_step(stmt);
        sqlite3_finalize(stmt);

        if (rc != SQLITE_DONE) {
            LogSqliteError(db_, "CreateKnowledgeBase insert");
            return -1;
        }

        int new_id = static_cast<int>(sqlite3_last_insert_rowid(db_));
        LOG_INFO("VectorStore: created knowledge base '{}' (id={})", name, new_id);

        // 如果还没有活跃知识库，自动设为当前
        if (active_kb_id_ < 0) {
            SetActiveKnowledgeBaseID(new_id);
        }

        return new_id;
    }

    std::vector<KnowledgeBaseInfo> ListKnowledgeBases() {
        std::vector<KnowledgeBaseInfo> result;
        if (!is_open_ || !db_) return result;

        const char* sql =
            "SELECT k.id, k.name, k.description, k.role_type, k.created_at, "
            "       COUNT(d.id) AS chunk_count "
            "FROM knowledge_bases k "
            "LEFT JOIN document_chunks d ON k.id = d.kb_id "
            "GROUP BY k.id "
            "ORDER BY k.created_at DESC;";

        sqlite3_stmt* stmt = nullptr;
        int rc = sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr);
        if (rc != SQLITE_OK) {
            LogSqliteError(db_, "ListKnowledgeBases");
            return result;
        }

        while (sqlite3_step(stmt) == SQLITE_ROW) {
            KnowledgeBaseInfo info;
            info.id = sqlite3_column_int(stmt, 0);
            info.name = reinterpret_cast<const char*>(
                sqlite3_column_text(stmt, 1));
            info.description = reinterpret_cast<const char*>(
                sqlite3_column_text(stmt, 2));
            info.role_type = reinterpret_cast<const char*>(
                sqlite3_column_text(stmt, 3));
            info.created_at = reinterpret_cast<const char*>(
                sqlite3_column_text(stmt, 4));
            info.chunk_count = sqlite3_column_int(stmt, 5);
            result.push_back(info);
        }

        sqlite3_finalize(stmt);
        return result;
    }

    bool DeleteKnowledgeBase(int kb_id) {
        if (!is_open_ || !db_) return false;

        const char* sql = "DELETE FROM knowledge_bases WHERE id = ?;";
        sqlite3_stmt* stmt = nullptr;
        int rc = sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr);
        if (rc != SQLITE_OK) {
            LogSqliteError(db_, "DeleteKnowledgeBase");
            return false;
        }

        sqlite3_bind_int(stmt, 1, kb_id);
        rc = sqlite3_step(stmt);
        sqlite3_finalize(stmt);

        if (rc == SQLITE_DONE) {
            LOG_INFO("VectorStore: deleted knowledge base id={}", kb_id);
            if (active_kb_id_ == kb_id) {
                active_kb_id_ = FindFirstKnowledgeBase();
                SaveActiveKnowledgeBase();
            }
            return true;
        }

        LogSqliteError(db_, "DeleteKnowledgeBase delete");
        return false;
    }

    // ========================================================
    // 文档块管理
    // ========================================================

    int InsertChunks(int kb_id,
                     const std::vector<DocumentChunk>& chunks) {
        if (!is_open_ || !db_) return -1;
        if (chunks.empty()) return 0;

        const char* sql =
            "INSERT INTO document_chunks "
            "(kb_id, chunk_id, source_file, page_number, "
            " content, embedding, metadata, char_count) "
            "VALUES (?, ?, ?, ?, ?, ?, ?, ?) "
            "ON CONFLICT(kb_id, chunk_id) DO UPDATE SET "
            "source_file=excluded.source_file, "
            "page_number=excluded.page_number, "
            "content=excluded.content, "
            "embedding=excluded.embedding, "
            "metadata=excluded.metadata, "
            "char_count=excluded.char_count, "
            "updated_at=datetime('now');";

        sqlite3_stmt* stmt = nullptr;
        int rc = sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr);
        if (rc != SQLITE_OK) {
            LogSqliteError(db_, "InsertChunks prepare");
            return -1;
        }

        if (sqlite3_exec(
                db_, "BEGIN IMMEDIATE;", nullptr, nullptr, nullptr) !=
            SQLITE_OK) {
            sqlite3_finalize(stmt);
            LogSqliteError(db_, "InsertChunks begin");
            return -1;
        }

        // 同一源文档重新上传时，在事务内先清理旧块，防止文档变短后
        // 留下已经不存在的尾部块。
        std::set<std::string> source_files;
        for (const auto& chunk : chunks) {
            try {
                const auto meta = nlohmann::json::parse(chunk.metadata);
                source_files.insert(meta.value("source", chunk.chunk_id));
            } catch (...) {
                source_files.insert(chunk.chunk_id);
            }
        }

        sqlite3_stmt* delete_stmt = nullptr;
        rc = sqlite3_prepare_v2(
            db_,
            "DELETE FROM document_chunks "
            "WHERE kb_id=? AND source_file=?;",
            -1, &delete_stmt, nullptr);
        if (rc != SQLITE_OK) {
            sqlite3_finalize(stmt);
            sqlite3_exec(db_, "ROLLBACK;", nullptr, nullptr, nullptr);
            LogSqliteError(db_, "InsertChunks cleanup prepare");
            return -1;
        }
        for (const auto& source_file : source_files) {
            sqlite3_reset(delete_stmt);
            sqlite3_bind_int(delete_stmt, 1, kb_id);
            sqlite3_bind_text(
                delete_stmt, 2, source_file.c_str(), -1, SQLITE_TRANSIENT);
            if (sqlite3_step(delete_stmt) != SQLITE_DONE) {
                sqlite3_finalize(delete_stmt);
                sqlite3_finalize(stmt);
                sqlite3_exec(db_, "ROLLBACK;", nullptr, nullptr, nullptr);
                LogSqliteError(db_, "InsertChunks cleanup");
                return -1;
            }
        }
        sqlite3_finalize(delete_stmt);

        int inserted = 0;

        for (const auto& chunk : chunks) {
            // 跳过没有向量的 chunk
            if (chunk.embedding.empty()) {
                LOG_WARN("VectorStore: skipping chunk '{}' (no embedding)",
                         chunk.chunk_id);
                continue;
            }

            // 序列化向量为 JSON
            std::string emb_json = SerializeEmbedding(chunk.embedding);

            // 提取文件名（从 metadata JSON 中，或直接用 chunk_id）
            std::string source_file = "unknown";
            try {
                auto meta = nlohmann::json::parse(chunk.metadata);
                source_file = meta.value("source", chunk.chunk_id);
            } catch (...) {
                source_file = chunk.chunk_id;
            }

            // 提取页码
            int page_number = 0;
            try {
                auto meta = nlohmann::json::parse(chunk.metadata);
                page_number = meta.value("page", 0);
            } catch (...) {
                page_number = 0;
            }

            sqlite3_reset(stmt);
            sqlite3_bind_int(stmt,    1, kb_id);
            sqlite3_bind_text(stmt,   2, chunk.chunk_id.c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_bind_text(stmt,   3, source_file.c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_bind_int(stmt,    4, page_number);
            sqlite3_bind_text(stmt,   5, chunk.content.c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_bind_text(stmt,   6, emb_json.c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_bind_text(stmt,   7, chunk.metadata.c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_bind_int(stmt,    8, static_cast<int>(chunk.content.length()));

            rc = sqlite3_step(stmt);
            if (rc == SQLITE_DONE) {
                inserted++;
            } else {
                sqlite3_finalize(stmt);
                sqlite3_exec(db_, "ROLLBACK;", nullptr, nullptr, nullptr);
                LogSqliteError(db_, "InsertChunks write");
                return -1;
            }
        }

        if (sqlite3_exec(db_, "COMMIT;", nullptr, nullptr, nullptr) !=
            SQLITE_OK) {
            sqlite3_finalize(stmt);
            LogSqliteError(db_, "InsertChunks commit");
            return -1;
        }
        sqlite3_finalize(stmt);

        LOG_INFO("VectorStore: inserted {}/{} chunks into kb_id={}",
                 inserted, chunks.size(), kb_id);
        return inserted;
    }

    bool DeleteChunksByKB(int kb_id) {
        if (!is_open_ || !db_) return false;

        const char* sql = "DELETE FROM document_chunks WHERE kb_id = ?;";
        sqlite3_stmt* stmt = nullptr;
        int rc = sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr);
        if (rc != SQLITE_OK) {
            LogSqliteError(db_, "DeleteChunksByKB");
            return false;
        }

        sqlite3_bind_int(stmt, 1, kb_id);
        rc = sqlite3_step(stmt);
        sqlite3_finalize(stmt);

        LOG_INFO("VectorStore: deleted all chunks for kb_id={}", kb_id);
        return rc == SQLITE_DONE;
    }

    int GetChunkCount(int kb_id) {
        if (!is_open_ || !db_) return 0;

        const char* sql = "SELECT COUNT(*) FROM document_chunks WHERE kb_id = ?;";
        sqlite3_stmt* stmt = nullptr;
        int rc = sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr);
        if (rc != SQLITE_OK) return 0;

        sqlite3_bind_int(stmt, 1, kb_id);
        int count = 0;
        if (sqlite3_step(stmt) == SQLITE_ROW) {
            count = sqlite3_column_int(stmt, 0);
        }
        sqlite3_finalize(stmt);
        return count;
    }

    // ========================================================
    // 检索
    // ========================================================

    std::vector<SearchResult> Search(
        const std::vector<float>& query_embedding,
        int top_k,
        float threshold) {

        std::vector<SearchResult> results;
        if (!is_open_ || !db_ || query_embedding.empty()) {
            return results;
        }
        if (active_kb_id_ < 0) {
            LOG_WARN("VectorStore: no active knowledge base set");
            return results;
        }
        if (top_k <= 0) {
            return results;
        }

        // 查询当前活跃知识库的所有 chunk
        const char* sql =
            "SELECT chunk_id, content, embedding, metadata, source_file, page_number "
            "FROM document_chunks WHERE kb_id = ?;";

        sqlite3_stmt* stmt = nullptr;
        int rc = sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr);
        if (rc != SQLITE_OK) {
            LogSqliteError(db_, "Search prepare");
            return results;
        }

        sqlite3_bind_int(stmt, 1, active_kb_id_);

        // 使用最小堆维护 Top-K 结果
        // pair<similarity, index>，按 similarity 升序（最小堆）
        using HeapEntry = std::pair<float, size_t>;
        auto cmp = [](const HeapEntry& a, const HeapEntry& b) {
            return a.first > b.first;  // 最小堆
        };
        std::priority_queue<HeapEntry, std::vector<HeapEntry>, decltype(cmp)> heap(cmp);

        // 临时存储所有通过阈值的 chunk 索引
        struct TempResult {
            SearchResult sr;
            size_t idx;
        };
        std::vector<TempResult> temp;

        int total_checked = 0;
        int dim_mismatch = 0;

        while (sqlite3_step(stmt) == SQLITE_ROW) {
            total_checked++;

            // 反序列化向量
            std::string emb_json = reinterpret_cast<const char*>(
                sqlite3_column_text(stmt, 2));
            auto chunk_embedding = DeserializeEmbedding(emb_json);

            // 检查维度匹配
            if (chunk_embedding.size() != query_embedding.size()) {
                dim_mismatch++;
                continue;
            }

            // 计算相似度
            float sim = CosineSimilarity(query_embedding, chunk_embedding);

            // 过滤低于阈值的
            if (sim < threshold) continue;

            // 构造结果
            SearchResult sr;
            sr.similarity = sim;
            sr.chunk.chunk_id = reinterpret_cast<const char*>(
                sqlite3_column_text(stmt, 0));
            sr.chunk.content = reinterpret_cast<const char*>(
                sqlite3_column_text(stmt, 1));
            sr.chunk.metadata = reinterpret_cast<const char*>(
                sqlite3_column_text(stmt, 3));

            size_t idx = temp.size();
            temp.push_back({sr, idx});

            // 维护 Top-K 堆
            heap.push({sim, idx});
            if (static_cast<int>(heap.size()) > top_k) {
                heap.pop();
            }
        }

        sqlite3_finalize(stmt);

        // 从堆中提取 Top-K 结果，按相似度降序
        std::vector<HeapEntry> top_entries;
        while (!heap.empty()) {
            top_entries.push_back(heap.top());
            heap.pop();
        }
        // 反转：堆是最小堆，pop 出来是升序，反转后降序
        std::reverse(top_entries.begin(), top_entries.end());

        for (const auto& entry : top_entries) {
            results.push_back(temp[entry.second].sr);
        }

        LOG_INFO("VectorStore: searched {} chunks ({} dim-mismatch skipped), "
                 "returned {} results (top_k={}, threshold={:.2f})",
                 total_checked, dim_mismatch, results.size(), top_k, threshold);

        return results;
    }
};

// ============================================================
// 公开接口（Pimpl 转发）
// ============================================================

VectorStore::VectorStore()
    : impl_(std::make_unique<Impl>()) {
}

VectorStore::~VectorStore() = default;

bool VectorStore::Open(const std::string& db_path) {
    return impl_->Open(db_path);
}

void VectorStore::Close() {
    impl_->Close();
}

bool VectorStore::IsOpen() const {
    return impl_->is_open_;
}

int VectorStore::CreateKnowledgeBase(
    const std::string& name,
    const std::string& description,
    const std::string& role_type) {
    return impl_->CreateKnowledgeBase(name, description, role_type);
}

std::vector<KnowledgeBaseInfo> VectorStore::ListKnowledgeBases() {
    return impl_->ListKnowledgeBases();
}

bool VectorStore::DeleteKnowledgeBase(int kb_id) {
    return impl_->DeleteKnowledgeBase(kb_id);
}

int VectorStore::GetActiveKnowledgeBaseID() const {
    return impl_->active_kb_id_;
}

void VectorStore::SetActiveKnowledgeBaseID(int kb_id) {
    impl_->SetActiveKnowledgeBaseID(kb_id);
}

int VectorStore::InsertChunks(
    int kb_id,
    const std::vector<DocumentChunk>& chunks) {
    return impl_->InsertChunks(kb_id, chunks);
}

bool VectorStore::DeleteChunksByKB(int kb_id) {
    return impl_->DeleteChunksByKB(kb_id);
}

int VectorStore::GetChunkCount(int kb_id) {
    return impl_->GetChunkCount(kb_id);
}

std::vector<SearchResult> VectorStore::Search(
    const std::vector<float>& query_embedding,
    int top_k,
    float threshold) {
    return impl_->Search(query_embedding, top_k, threshold);
}

} // namespace services
} // namespace interview
