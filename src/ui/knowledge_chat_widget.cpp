/**
 * @file knowledge_chat_widget.cpp
 * @brief AI 面试助手页面的界面、异步检索和连续 RAG 问答实现。
 */

#include "ui/knowledge_chat_widget.h"

#include "common/logger.h"
#include "services/rag_client.h"

#include <QFutureWatcher>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QScrollBar>
#include <QShortcut>
#include <QTextBrowser>
#include <QTextDocument>
#include <QTextEdit>
#include <QVBoxLayout>
#include <QtConcurrent/QtConcurrentRun>
#include <exception>
#include <string>
#include <utility>
#include <vector>

namespace interview {
namespace ui {

namespace {

struct SourceView {
    std::string name;
    std::string content;
    int similarity_percent = 0;
};

struct ChatResult {
    std::string knowledge_base_id;
    std::string answer;
    std::string error;
    std::vector<SourceView> sources;
    bool knowledge_found = false;
};

QString PlainTextToHtml(const QString& text) {
    QString escaped = text.toHtmlEscaped();
    escaped.replace("\n", "<br>");
    return escaped;
}

QString MarkdownToHtml(const QString& markdown) {
    QTextDocument document;
    document.setDefaultStyleSheet(
        "p { margin: 5px 0; line-height: 1.55; }"
        "h1, h2, h3 { color: #163a5f; margin: 10px 0 5px 0; }"
        "h1 { font-size: 15pt; } h2 { font-size: 13pt; } h3 { font-size: 11.5pt; }"
        "ul, ol { margin: 5px 0 5px 20px; } li { margin: 3px 0; }"
        "code { background-color: #eef2f6; color: #b42318; padding: 1px 3px; "
        "font-family: Consolas, monospace; }"
        "pre { background-color: #202b38; color: #f4f7fa; padding: 9px; "
        "font-family: Consolas, monospace; white-space: pre-wrap; }"
        "blockquote { color: #555; border-left: 3px solid #90a4ae; "
        "margin-left: 4px; padding-left: 9px; }");
    document.setMarkdown(markdown);

    const QString full_html = document.toHtml();
    const int body_tag = full_html.indexOf("<body");
    const int body_start = body_tag >= 0
        ? full_html.indexOf('>', body_tag) + 1
        : -1;
    const int body_end = body_start > 0
        ? full_html.indexOf("</body>", body_start)
        : -1;
    if (body_start > 0 && body_end > body_start) {
        return full_html.mid(body_start, body_end - body_start);
    }
    return PlainTextToHtml(markdown);
}

QString CompactSourceSnippet(QString text) {
    text.replace("\r", " ");
    text.replace("\n", " ");
    text.replace("**", "");
    text.replace("###", "");
    text.replace("##", "");
    text.replace("#", "");
    text.replace('`', QString());
    while (text.contains("  ")) {
        text.replace("  ", " ");
    }
    return text.trimmed().left(190);
}

} // namespace

KnowledgeChatWidget::KnowledgeChatWidget(QWidget* parent)
    : QWidget(parent) {
    SetupUI();
}

KnowledgeChatWidget::~KnowledgeChatWidget() = default;

void KnowledgeChatWidget::SetService(
    std::shared_ptr<services::RagBackend> rag_backend) {
    rag_backend_ = std::move(rag_backend);
    RefreshKnowledgeBaseStatus();
}

void KnowledgeChatWidget::SetupUI() {
    auto* main_layout = new QVBoxLayout(this);
    main_layout->setContentsMargins(16, 12, 16, 12);
    main_layout->setSpacing(10);

    auto* status_group = new QGroupBox(QStringLiteral("当前知识库"), this);
    auto* status_layout = new QHBoxLayout(status_group);
    kb_status_label_ = new QLabel(QStringLiteral("RAG 服务尚未初始化"), this);
    kb_status_label_->setWordWrap(true);
    kb_status_label_->setStyleSheet(
        "QLabel { color: #555; padding: 4px; font-size: 10pt; }");
    status_layout->addWidget(kb_status_label_, 1);
    main_layout->addWidget(status_group);

    conversation_view_ = new QTextBrowser(this);
    conversation_view_->setOpenExternalLinks(false);
    conversation_view_->setStyleSheet(
        "QTextBrowser { background: white; border: 1px solid #ddd; "
        "border-radius: 4px; padding: 10px; font-family: 'Microsoft YaHei'; "
        "font-size: 11pt; }");
    conversation_view_->document()->setDefaultStyleSheet(
        "a { color:#1565c0; text-decoration:none; }"
        "p { margin:4px 0; } li { margin:2px 0; }");
    conversation_view_->setHtml(
        QStringLiteral("<p style='color:#666'>你好，我是 AI 面试助手。"
                       "启用知识库后，可以在下方持续提问。</p>"));
    main_layout->addWidget(conversation_view_, 1);

    auto* input_group = new QGroupBox(QStringLiteral("输入问题"), this);
    auto* input_layout = new QVBoxLayout(input_group);
    question_input_ = new QTextEdit(this);
    question_input_->setPlaceholderText(
        QStringLiteral("请输入技术面试问题，例如：shared_ptr 的引用计数线程安全吗？"));
    question_input_->setMaximumHeight(100);
    input_layout->addWidget(question_input_);

    auto* button_layout = new QHBoxLayout();
    request_status_label_ = new QLabel(QStringLiteral("就绪"), this);
    request_status_label_->setStyleSheet("QLabel { color: #666; }");
    send_button_ = new QPushButton(QStringLiteral("发送问题"), this);
    clear_button_ = new QPushButton(QStringLiteral("清空对话"), this);
    send_button_->setDefault(true);
    send_button_->setStyleSheet(
        "QPushButton { background:#1976d2; color:white; padding:7px 18px; "
        "border:none; border-radius:4px; font-weight:bold; }"
        "QPushButton:disabled { background:#9e9e9e; }"
        "QPushButton:hover:!disabled { background:#1565c0; }");
    clear_button_->setStyleSheet(
        "QPushButton { padding:7px 18px; border:1px solid #aaa; "
        "border-radius:4px; background:white; }"
        "QPushButton:hover { background:#f5f5f5; }");
    button_layout->addWidget(request_status_label_, 1);
    button_layout->addWidget(clear_button_);
    button_layout->addWidget(send_button_);
    input_layout->addLayout(button_layout);
    main_layout->addWidget(input_group);

    connect(send_button_, &QPushButton::clicked,
            this, &KnowledgeChatWidget::OnSendQuestion);
    connect(clear_button_, &QPushButton::clicked,
            this, &KnowledgeChatWidget::OnClearConversation);

    auto* send_shortcut = new QShortcut(QKeySequence(Qt::CTRL | Qt::Key_Return), this);
    connect(send_shortcut, &QShortcut::activated,
            this, &KnowledgeChatWidget::OnSendQuestion);
}

void KnowledgeChatWidget::RefreshKnowledgeBaseStatus() {
    if (!rag_backend_) {
        kb_status_label_->setText(QStringLiteral("RAG 服务不可用"));
        send_button_->setEnabled(false);
        return;
    }
    const QString backend_label = rag_backend_->GetBackendName() == "remote"
        ? QStringLiteral("远程 RAG")
        : QStringLiteral("本地 RAG");
    if (active_knowledge_base_id_.isEmpty()) {
        kb_status_label_->setText(
            QStringLiteral("当前路径：%1。尚未启用知识库，请先到“知识库”页面选择并启用。")
                .arg(backend_label));
        send_button_->setEnabled(false);
        return;
    }
    send_button_->setEnabled(false);
    kb_status_label_->setText(
        QStringLiteral("正在读取%1知识库状态……").arg(backend_label));
    const auto client = rag_backend_;
    const QString expected_id = active_knowledge_base_id_;
    auto* watcher = new QFutureWatcher<services::RagKnowledgeBaseListResponse>(this);
    connect(watcher, &QFutureWatcher<services::RagKnowledgeBaseListResponse>::finished,
            this, [this, watcher, expected_id, backend_label]() {
        const auto response = watcher->result();
        watcher->deleteLater();
        if (expected_id != active_knowledge_base_id_) return;
        if (!response.success) {
            kb_status_label_->setText(QStringLiteral("RAG 服务不可用：%1")
                .arg(QString::fromStdString(response.error_message)));
            send_button_->setEnabled(false);
            return;
        }
        for (const auto& kb : response.items) {
            if (QString::fromStdString(kb.id) == expected_id) {
                active_knowledge_base_name_ = QString::fromStdString(kb.name);
                kb_status_label_->setText(
                    QStringLiteral("当前路径：%1；已启用：%2（%3 个文档块）")
                        .arg(backend_label)
                        .arg(active_knowledge_base_name_)
                        .arg(kb.chunk_count));
                send_button_->setEnabled(
                    !request_in_progress_ && kb.chunk_count > 0);
                return;
            }
        }
        kb_status_label_->setText(QStringLiteral("当前知识库已不存在，请重新选择。"));
        send_button_->setEnabled(false);
    });
    watcher->setFuture(QtConcurrent::run(
        [client]() { return client->ListKnowledgeBases(); }));
}

void KnowledgeChatWidget::OnActiveKnowledgeBaseChanged(
    const QString& kb_id, const QString& kb_name) {
    active_knowledge_base_id_ = kb_id;
    active_knowledge_base_name_ = kb_name;
    if (request_in_progress_) {
        reset_history_after_request_ = true;
    } else if (rag_backend_) {
        rag_backend_->ResetConversation();
    }
    conversation_view_->setHtml(
        QStringLiteral("<p style='color:#666'>知识库已切换，对话上下文已清空。"
                       "现在可以基于新知识库提问。</p>"));
    RefreshKnowledgeBaseStatus();
}

void KnowledgeChatWidget::OnSendQuestion() {
    if (request_in_progress_) {
        return;
    }

    const QString question = question_input_->toPlainText().trimmed();
    if (question.isEmpty()) {
        request_status_label_->setText(QStringLiteral("请先输入问题"));
        return;
    }
    if (!rag_backend_ || active_knowledge_base_id_.isEmpty()) {
        request_status_label_->setText(QStringLiteral("请先连接 RAG 服务并启用知识库"));
        return;
    }

    AppendUserMessage(question);
    question_input_->clear();
    SetRequestInProgress(true);

    const auto client = rag_backend_;
    const std::string active_id = active_knowledge_base_id_.toStdString();
    const std::string user_text = question.toUtf8().toStdString();

    auto* watcher = new QFutureWatcher<ChatResult>(this);
    connect(watcher, &QFutureWatcher<ChatResult>::finished, this,
            [this, watcher]() {
        const ChatResult result = watcher->result();
        watcher->deleteLater();
        SetRequestInProgress(false);

        if (reset_history_after_request_) {
            if (rag_backend_) rag_backend_->ResetConversation();
            reset_history_after_request_ = false;
        }

        if (active_knowledge_base_id_.toStdString() != result.knowledge_base_id) {
            AppendAssistantMessage(
                QStringLiteral("知识库在回答生成期间发生了切换，本次结果已丢弃，请重新提问。"));
            RefreshKnowledgeBaseStatus();
            return;
        }
        if (!result.error.empty()) {
            AppendAssistantMessage(
                QStringLiteral("回答生成失败：%1")
                    .arg(QString::fromStdString(result.error)));
            request_status_label_->setText(QStringLiteral("请求失败"));
            return;
        }

        AppendAssistantMessage(QString::fromStdString(result.answer));
        if (!result.sources.empty()) {
            QString source_html = QStringLiteral(
                "<div style='margin:4px 0 16px 22px; padding:9px 12px; "
                "background-color:#fafafa; border-left:3px solid #90a4ae; "
                "color:#455a64'><b>参考资料</b>");
            int source_number = 0;
            for (const auto& source : result.sources) {
                ++source_number;
                source_html += QStringLiteral(
                    "<div style='margin-top:7px'><span style='color:#1565c0; "
                    "font-weight:bold'>%1. %2</span>"
                    "<span style='color:#78909c'> · 相关度 %3%</span>"
                    "<br><span style='font-size:9.5pt; color:#607d8b'>%4</span></div>")
                    .arg(source_number)
                    .arg(QString::fromStdString(source.name).toHtmlEscaped())
                    .arg(source.similarity_percent)
                    .arg(PlainTextToHtml(
                        CompactSourceSnippet(
                            QString::fromStdString(source.content))));
            }
            source_html += QStringLiteral("</div>");
            AppendSources(source_html);
        }
        request_status_label_->setText(
            result.knowledge_found ? QStringLiteral("回答完成")
                                   : QStringLiteral("未检索到相关资料"));
        question_input_->setFocus();
    });

    watcher->setFuture(QtConcurrent::run([client, active_id, user_text]() {
            ChatResult result;
            result.knowledge_base_id = active_id;
            try {
                const auto response = client->Ask(
                    user_text, active_id, "general_assistant");
                if (!response.success) {
                    result.error = response.error_message;
                    return result;
                }
                result.answer = response.answer;
                result.knowledge_found = response.knowledge_found;
                for (const auto& source : response.sources) {
                    SourceView view;
                    view.content = source.content;
                    view.similarity_percent =
                        static_cast<int>(source.score * 100.0f);
                    view.name = source.document_name;
                    result.sources.push_back(std::move(view));
                }
            } catch (const std::exception& e) {
                result.error = e.what();
                LOG_ERROR("AI interview assistant request failed: {}", e.what());
            }
            return result;
        }));
}

void KnowledgeChatWidget::OnClearConversation() {
    if (request_in_progress_) {
        request_status_label_->setText(
            QStringLiteral("当前回答生成完成后才能清空对话"));
        return;
    }
    if (rag_backend_) rag_backend_->ResetConversation();
    conversation_view_->setHtml(
        QStringLiteral("<p style='color:#666'>对话已清空，可以开始新的知识库问答。</p>"));
    request_status_label_->setText(QStringLiteral("就绪"));
}

void KnowledgeChatWidget::AppendUserMessage(const QString& text) {
    conversation_view_->append(
        QStringLiteral(
            "<div style='margin:12px 0 8px 70px; padding:8px 12px; "
            "background-color:#e8f5e9; border-left:4px solid #43a047'>"
            "<b style='color:#2e7d32'>你</b><br>%1</div>")
            .arg(PlainTextToHtml(text)));
    conversation_view_->verticalScrollBar()->setValue(
        conversation_view_->verticalScrollBar()->maximum());
}

void KnowledgeChatWidget::AppendAssistantMessage(const QString& text) {
    conversation_view_->append(
        QStringLiteral(
            "<div style='margin:8px 45px 4px 0; padding:9px 13px; "
            "background-color:#f2f7fd; border-left:4px solid #1976d2'>"
            "<b style='color:#1565c0'>AI 面试助手</b>%1</div>")
            .arg(MarkdownToHtml(text)));
    conversation_view_->verticalScrollBar()->setValue(
        conversation_view_->verticalScrollBar()->maximum());
}

void KnowledgeChatWidget::AppendSources(const QString& html) {
    conversation_view_->append(html);
    conversation_view_->verticalScrollBar()->setValue(
        conversation_view_->verticalScrollBar()->maximum());
}

void KnowledgeChatWidget::SetRequestInProgress(bool in_progress) {
    request_in_progress_ = in_progress;
    send_button_->setEnabled(!in_progress);
    clear_button_->setEnabled(!in_progress);
    question_input_->setEnabled(!in_progress);
    const bool is_remote = rag_backend_ &&
        rag_backend_->GetBackendName() == "remote";
    request_status_label_->setText(in_progress
        ? (is_remote ? QStringLiteral("正在调用远程 RAG 服务……")
                     : QStringLiteral("正在调用本地 RAG 链路……"))
        : QStringLiteral("就绪"));
}

} // namespace ui
} // namespace interview
