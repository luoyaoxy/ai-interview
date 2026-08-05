/**
 * @file main_qt.cpp
 * @brief Qt 图形界面程序入口。
 *
 * 主要模块：应用初始化、配置加载、RAG 服务装配、主窗口启动与退出清理。
 */

#include <QApplication>
#include "ui/mainwindow.h"
#include "common/logger.h"
#include "common/config.h"
#include "services/document_loader.h"
#include "services/embedding_client.h"
#include "services/llm_client.h"
#include "services/local_rag_backend.h"
#include "services/rag_client.h"
#include "services/rag_prompt_builder.h"
#include "services/vector_store.h"
#include <filesystem>
#include <iostream>

int main(int argc, char *argv[]) {
    QApplication app(argc, argv);

    // 设置应用信息
    QApplication::setApplicationName("C++ Interview System");
    QApplication::setApplicationVersion("2.0");
    QApplication::setOrganizationName("Interview");

    // 初始化日志系统
    interview::common::Logger::Init("interview_qt.log", true);

    LOG_INFO("========================================");
    LOG_INFO("C++ Interview System (Qt GUI) v2.0");
    LOG_INFO("========================================");

    // 加载配置文件
    try {
        auto& cfg = interview::common::Config::Instance();

        // 尝试从多个位置加载配置
        std::vector<std::string> config_paths = {
            "config/default_config.json",
            "../config/default_config.json",
            "../../config/default_config.json"
        };

        bool config_loaded = false;
        for (const auto& path : config_paths) {
            try {
                cfg.LoadFromFile(path);
                LOG_INFO("Configuration loaded from: {}", path);
                config_loaded = true;
                break;
            } catch (...) {
                continue;
            }
        }

        if (!config_loaded) {
            LOG_WARNING("Could not load config file, using default values");
            std::cerr << "警告: 无法加载配置文件，将使用默认配置" << std::endl;
        }

    } catch (const std::exception& e) {
        LOG_ERROR("Configuration error: {}", e.what());
        std::cerr << "配置错误: " << e.what() << std::endl;
    }

    // 创建主窗口
    interview::ui::MainWindow window;

    // ========================================================
    // RAG 服务初始化（第七步集成）
    // ========================================================
    auto& cfg = interview::common::Config::Instance();
    if (cfg.IsRAGEnabled()) {
        const auto& rag = cfg.GetRAGConfig();
        LOG_INFO("========================================");
        LOG_INFO("Initializing {} RAG backend...",
                 rag.use_remote_rag ? "remote" : "local");
        LOG_INFO("========================================");

        try {
            std::shared_ptr<interview::services::RagBackend> rag_backend;
            if (rag.use_remote_rag) {
                auto remote =
                    std::make_shared<interview::services::RagClient>();
                if (remote->Initialize(
                        rag.service_url,
                        rag.api_key,
                        rag.timeout_seconds,
                        rag.verify_ssl,
                        rag.max_retries,
                        rag.retry_delay_ms)) {
                    rag_backend = std::move(remote);
                }
            } else {
                auto embedding =
                    std::make_shared<interview::services::EmbeddingClient>();
                if (embedding->Initialize(
                        rag.embedding_api_url,
                        rag.embedding_provider == "ollama"
                            ? std::string{} : cfg.llm_config.api_key,
                        rag.embedding_model)) {
                    const auto db_path = std::filesystem::u8path(
                        rag.vector_db_path);
                    if (db_path.has_parent_path()) {
                        std::filesystem::create_directories(db_path.parent_path());
                    }
                    auto store =
                        std::make_shared<interview::services::VectorStore>();
                    if (store->Open(rag.vector_db_path)) {
                        auto loader = std::make_shared<
                            interview::services::DocumentLoader>();
                        loader->SetChunkConfig({
                            rag.chunk_size, rag.chunk_overlap, true});
                        auto prompt_builder = std::make_shared<
                            interview::services::RAGPromptBuilder>();
                        prompt_builder->Initialize(embedding, store);
                        prompt_builder->SetSimilarityThreshold(
                            rag.similarity_threshold);
                        auto llm = std::make_shared<
                            interview::services::LLMClient>();
                        llm->SetMaxHistoryTurns(rag.max_history_turns);
                        auto local = std::make_shared<
                            interview::services::LocalRagBackend>();
                        if (local->Initialize(
                                embedding, store, loader, prompt_builder, llm)) {
                            rag_backend = std::move(local);
                        }
                    }
                }
            }

            if (rag_backend) {
                LOG_INFO("RAG backend initialized successfully: {}",
                         rag_backend->GetBackendName());
                window.SetRAGBackend(std::move(rag_backend));
            } else {
                LOG_WARNING("Failed to initialize selected RAG backend");
            }
        } catch (const std::exception& e) {
            LOG_ERROR("RAG initialization failed: {}", e.what());
        }
    } else {
        LOG_INFO("RAG disabled in config, using legacy interview mode");
    }

    window.show();

    LOG_INFO("Qt GUI started");

    // 运行应用
    int result = app.exec();

    LOG_INFO("Application exiting with code: {}", result);
    return result;
}

