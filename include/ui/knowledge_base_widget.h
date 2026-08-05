/**
 * @file knowledge_base_widget.h
 * @brief 知识库管理页面（RAG 第五步）
 *
 * 提供知识库管理的完整 UI 界面，包括：
 * 1. 知识库列表（创建、切换、删除）
 * 2. 文档上传（PDF/TXT/MD/JSON）+ 处理进度显示
 * 3. 检索测试（输入问题 → 查看匹配结果）
 */

#pragma once

#include <QWidget>
#include <QComboBox>
#include <QListWidget>
#include <QPushButton>
#include <QTextEdit>
#include <QLabel>
#include <QProgressBar>
#include <QGroupBox>
#include <QLineEdit>
#include <memory>

#include "services/rag_client.h"

namespace interview {
namespace ui {

/**
 * @brief 知识库管理页面
 *
 * 嵌入主窗口的标签页，提供知识库全生命周期管理。
 *
 * 布局结构：
 * ┌─ 角色选择区 ───────────────────────────────┐
 * │ [下拉框: 角色类型]  [角色描述]              │
 * ├─ 知识库列表区 ─────────────────────────────┤
 * │ [列表: 名称 + 块数]  [新建] [删除] [使用]  │
 * ├─ 文档上传区 ───────────────────────────────┤
 * │ [下拉框: 目标知识库]  [上传] [上传文件夹]   │
 * │ [列表: 已上传文档状态]  [进度条]            │
 * ├─ 检索测试区 ───────────────────────────────┤
 * │ [输入: 问题]  [搜索]                       │
 * │ [结果: Top-K 相似文档 + 相似度]            │
 * └───────────────────────────────────────────┘
 */
class KnowledgeBaseWidget : public QWidget {
    Q_OBJECT

public:
    explicit KnowledgeBaseWidget(QWidget* parent = nullptr);
    ~KnowledgeBaseWidget() override;

    /**
     * @brief 注入服务依赖
     *
     * 外部创建并管理这些服务的生命周期。
     * 必须在窗口显示前调用。
     */
    void SetService(std::shared_ptr<services::RagBackend> rag_backend);

    QString GetActiveKnowledgeBaseID() const;

signals:
    /// @brief 当前启用的知识库发生变化，供 AI 面试助手刷新状态和清空历史。
    void activeKnowledgeBaseChanged(
        const QString& kb_id, const QString& kb_name);

    /// @brief 文档处理完成，供 AI 面试助手刷新文档块数量。
    void knowledgeBaseUpdated(const QString& kb_id);

private slots:
    void onCreateKB();
    void onDeleteKB();
    void onUseKB();
    void onUploadDocument();
    void onUploadFolder();
    void onDeleteDocument();
    void onTestSearch();

private:
    void SetupUI();
    void SetupStyles();
    void RefreshKBList();
    void RefreshDocList();
    void RefreshKBCombo();
    void ProcessFiles(const QStringList& files, const QString& kb_id);
    void UpdateKnowledgeBaseViews();

    // === 知识库列表区 ===
    QListWidget* kb_list_ = nullptr;
    QPushButton* new_kb_btn_ = nullptr;
    QPushButton* delete_kb_btn_ = nullptr;
    QPushButton* use_kb_btn_ = nullptr;

    // === 文档上传区 ===
    QComboBox* target_kb_combo_ = nullptr;
    QListWidget* doc_list_ = nullptr;
    QPushButton* upload_btn_ = nullptr;
    QPushButton* upload_folder_btn_ = nullptr;
    QPushButton* delete_document_btn_ = nullptr;
    QPushButton* refresh_documents_btn_ = nullptr;
    QProgressBar* progress_bar_ = nullptr;
    QLabel* progress_label_ = nullptr;

    // === 检索测试区 ===
    QTextEdit* test_query_input_ = nullptr;
    QPushButton* test_search_btn_ = nullptr;
    QTextEdit* test_result_output_ = nullptr;

    // === 服务引用 ===
    std::shared_ptr<services::RagBackend> rag_backend_;
    std::vector<services::RagKnowledgeBase> knowledge_bases_;
    QString active_knowledge_base_id_;

};

} // namespace ui
} // namespace interview
