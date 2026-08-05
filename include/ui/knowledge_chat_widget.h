/**
 * @file knowledge_chat_widget.h
 * @brief 基于当前启用知识库的连续文字问答页面。
 */

#pragma once

#include <QWidget>
#include <memory>

class QLabel;
class QPushButton;
class QTextBrowser;
class QTextEdit;

namespace interview {
namespace services {
class RagBackend;
}

namespace ui {

class KnowledgeChatWidget : public QWidget {
    Q_OBJECT

public:
    explicit KnowledgeChatWidget(QWidget* parent = nullptr);
    ~KnowledgeChatWidget() override;

    void SetService(std::shared_ptr<services::RagBackend> rag_backend);

public slots:
    void OnActiveKnowledgeBaseChanged(
        const QString& kb_id, const QString& kb_name);
    void RefreshKnowledgeBaseStatus();

private slots:
    void OnSendQuestion();
    void OnClearConversation();

private:
    void SetupUI();
    void AppendUserMessage(const QString& text);
    void AppendAssistantMessage(const QString& text);
    void AppendSources(const QString& html);
    void SetRequestInProgress(bool in_progress);

    QLabel* kb_status_label_ = nullptr;
    QLabel* request_status_label_ = nullptr;
    QTextBrowser* conversation_view_ = nullptr;
    QTextEdit* question_input_ = nullptr;
    QPushButton* send_button_ = nullptr;
    QPushButton* clear_button_ = nullptr;

    std::shared_ptr<services::RagBackend> rag_backend_;

    QString active_knowledge_base_id_;
    QString active_knowledge_base_name_;
    bool request_in_progress_ = false;
    bool reset_history_after_request_ = false;
};

} // namespace ui
} // namespace interview
