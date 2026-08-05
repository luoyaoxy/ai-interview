/**
 * @file knowledge_base_widget.cpp
 * @brief 知识库管理页面的界面与交互实现。
 *
 * 主要模块：知识库增删启用、文档批量处理和检索测试。
 */

#include "ui/knowledge_base_widget.h"
#include "common/config.h"
#include "common/logger.h"
#include <QDir>
#include <QFileInfo>
#include <QFutureWatcher>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFileDialog>
#include <QMessageBox>
#include <QInputDialog>
#include <QScrollBar>
#include <QThreadPool>
#include <QtConcurrent/QtConcurrentRun>
#include <algorithm>
#include <sstream>
#include <iomanip>

namespace interview {
namespace ui {

// ============================================================
// 常用样式常量
// ============================================================
static const char* kGroupStyle = R"(
    QGroupBox {
        font-weight: bold;
        font-size: 11pt;
        border: 1px solid #ddd;
        border-radius: 6px;
        margin-top: 12px;
        padding-top: 16px;
    }
    QGroupBox::title {
        subcontrol-origin: margin;
        left: 12px;
        padding: 0 6px;
    }
)";

static const char* kButtonStyle = R"(
    QPushButton {
        padding: 6px 16px;
        border: 1px solid #ccc;
        border-radius: 4px;
        background-color: #f8f8f8;
        font-size: 10pt;
    }
    QPushButton:hover {
        background-color: #e8e8e8;
        border-color: #999;
    }
    QPushButton:pressed {
        background-color: #ddd;
    }
    QPushButton:disabled {
        color: #999;
        background-color: #f0f0f0;
    }
)";

static const char* kPrimaryButtonStyle = R"(
    QPushButton {
        padding: 6px 16px;
        border: 1px solid #1976d2;
        border-radius: 4px;
        background-color: #1976d2;
        color: white;
        font-size: 10pt;
        font-weight: bold;
    }
    QPushButton:hover {
        background-color: #1565c0;
    }
    QPushButton:pressed {
        background-color: #0d47a1;
    }
    QPushButton:disabled {
        background-color: #90caf9;
        border-color: #90caf9;
    }
)";

static const char* kDangerButtonStyle = R"(
    QPushButton {
        padding: 6px 16px;
        border: 1px solid #d32f2f;
        border-radius: 4px;
        background-color: #fff;
        color: #d32f2f;
        font-size: 10pt;
    }
    QPushButton:hover {
        background-color: #ffebee;
    }
    QPushButton:pressed {
        background-color: #ffcdd2;
    }
)";

// ============================================================
// 构造函数
// ============================================================
KnowledgeBaseWidget::KnowledgeBaseWidget(QWidget* parent)
    : QWidget(parent) {
    SetupUI();
    SetupStyles();
}

KnowledgeBaseWidget::~KnowledgeBaseWidget() = default;

// ============================================================
// 设置服务依赖
// ============================================================
void KnowledgeBaseWidget::SetService(
    std::shared_ptr<services::RagBackend> rag_backend) {
    rag_backend_ = std::move(rag_backend);
    RefreshKBList();
}

QString KnowledgeBaseWidget::GetActiveKnowledgeBaseID() const {
    return active_knowledge_base_id_;
}

// ============================================================
// UI 构建
// ============================================================
void KnowledgeBaseWidget::SetupUI() {
    QVBoxLayout* main_layout = new QVBoxLayout(this);
    main_layout->setSpacing(8);
    main_layout->setContentsMargins(16, 12, 16, 12);

    // ========================================================
    // 二、知识库列表区
    // ========================================================
    QGroupBox* kb_group = new QGroupBox("知识库列表", this);
    QVBoxLayout* kb_layout = new QVBoxLayout(kb_group);

    kb_list_ = new QListWidget(this);
    kb_list_->setMinimumHeight(100);
    kb_list_->setStyleSheet(
        "QListWidget { border: 1px solid #ddd; border-radius: 4px; }"
        "QListWidget::item { padding: 6px 8px; }"
        "QListWidget::item:selected { background-color: #e3f2fd; color: #1565c0; }");

    QHBoxLayout* kb_btn_layout = new QHBoxLayout();
    new_kb_btn_   = new QPushButton("新建知识库", this);
    delete_kb_btn_ = new QPushButton("删除选中", this);
    use_kb_btn_   = new QPushButton("启用选中", this);

    kb_btn_layout->addWidget(new_kb_btn_);
    kb_btn_layout->addWidget(delete_kb_btn_);
    kb_btn_layout->addWidget(use_kb_btn_);
    kb_btn_layout->addStretch();

    kb_layout->addWidget(kb_list_);
    kb_layout->addLayout(kb_btn_layout);

    connect(new_kb_btn_,   &QPushButton::clicked, this, &KnowledgeBaseWidget::onCreateKB);
    connect(delete_kb_btn_, &QPushButton::clicked, this, &KnowledgeBaseWidget::onDeleteKB);
    connect(use_kb_btn_,   &QPushButton::clicked, this, &KnowledgeBaseWidget::onUseKB);

    main_layout->addWidget(kb_group);

    // ========================================================
    // 三、文档上传区
    // ========================================================
    QGroupBox* upload_group = new QGroupBox("文档上传", this);
    QVBoxLayout* upload_layout = new QVBoxLayout(upload_group);

    QHBoxLayout* upload_top = new QHBoxLayout();
    upload_top->addWidget(new QLabel("目标知识库：", this));

    target_kb_combo_ = new QComboBox(this);
    target_kb_combo_->setMinimumWidth(180);
    upload_top->addWidget(target_kb_combo_);

    upload_btn_ = new QPushButton("上传文档", this);
    upload_folder_btn_ = new QPushButton("上传文件夹", this);
    delete_document_btn_ = new QPushButton("删除选中文档", this);
    refresh_documents_btn_ = new QPushButton("刷新状态", this);
    upload_top->addWidget(upload_btn_);
    upload_top->addWidget(upload_folder_btn_);
    upload_top->addWidget(delete_document_btn_);
    upload_top->addWidget(refresh_documents_btn_);
    upload_top->addStretch();

    doc_list_ = new QListWidget(this);
    doc_list_->setMinimumHeight(80);
    doc_list_->setStyleSheet(
        "QListWidget { border: 1px solid #ddd; border-radius: 4px; }");

    progress_bar_ = new QProgressBar(this);
    progress_bar_->setRange(0, 100);
    progress_bar_->setValue(0);
    progress_bar_->setTextVisible(false);
    progress_bar_->setMinimumHeight(16);
    progress_bar_->setVisible(false);

    progress_label_ = new QLabel("", this);
    progress_label_->setStyleSheet("QLabel { color: #666; font-size: 9pt; }");
    progress_label_->setVisible(false);

    upload_layout->addLayout(upload_top);
    upload_layout->addWidget(doc_list_);
    upload_layout->addWidget(progress_bar_);
    upload_layout->addWidget(progress_label_);

    connect(upload_btn_,        &QPushButton::clicked, this, &KnowledgeBaseWidget::onUploadDocument);
    connect(upload_folder_btn_, &QPushButton::clicked, this, &KnowledgeBaseWidget::onUploadFolder);
    connect(delete_document_btn_, &QPushButton::clicked,
            this, &KnowledgeBaseWidget::onDeleteDocument);
    connect(refresh_documents_btn_, &QPushButton::clicked,
            this, &KnowledgeBaseWidget::RefreshDocList);
    connect(target_kb_combo_, &QComboBox::currentIndexChanged,
            this, [this](int) { RefreshDocList(); });

    main_layout->addWidget(upload_group);

    // ========================================================
    // 四、检索测试区
    // ========================================================
    QGroupBox* search_group = new QGroupBox("检索测试", this);
    QVBoxLayout* search_layout = new QVBoxLayout(search_group);

    QHBoxLayout* search_input_layout = new QHBoxLayout();
    search_input_layout->addWidget(new QLabel("测试问题：", this));

    test_query_input_ = new QTextEdit(this);
    test_query_input_->setMaximumHeight(60);
    test_query_input_->setPlaceholderText("输入一个问题，测试知识库检索效果...");
    test_query_input_->setStyleSheet(
        "QTextEdit { border: 1px solid #ddd; border-radius: 4px; padding: 6px; }");
    search_input_layout->addWidget(test_query_input_, 1);

    test_search_btn_ = new QPushButton("测试检索", this);
    search_input_layout->addWidget(test_search_btn_);

    test_result_output_ = new QTextEdit(this);
    test_result_output_->setReadOnly(true);
    test_result_output_->setMinimumHeight(120);
    test_result_output_->setStyleSheet(
        "QTextEdit { "
        "  background-color: #fafafa; "
        "  border: 1px solid #ddd; "
        "  border-radius: 4px; "
        "  padding: 8px; "
        "  font-family: 'Microsoft YaHei', sans-serif; "
        "  font-size: 10pt; "
        "  color: #333; "
        "}");

    search_layout->addLayout(search_input_layout);
    search_layout->addWidget(test_result_output_);

    connect(test_search_btn_, &QPushButton::clicked, this, &KnowledgeBaseWidget::onTestSearch);

    main_layout->addWidget(search_group, 1);
}

// ============================================================
// 样式设置
// ============================================================
void KnowledgeBaseWidget::SetupStyles() {
    // 分组框
    for (QGroupBox* gb : findChildren<QGroupBox*>()) {
        gb->setStyleSheet(kGroupStyle);
    }

    // 按钮
    for (QPushButton* btn : findChildren<QPushButton*>()) {
        btn->setStyleSheet(kButtonStyle);
    }

    // 主要操作按钮特殊样式
    upload_btn_->setStyleSheet(kPrimaryButtonStyle);
    new_kb_btn_->setStyleSheet(kPrimaryButtonStyle);
    test_search_btn_->setStyleSheet(kPrimaryButtonStyle);

    // 危险操作按钮
    delete_kb_btn_->setStyleSheet(kDangerButtonStyle);
}

// ============================================================
// 知识库管理
// ============================================================
void KnowledgeBaseWidget::RefreshKBList() {
    if (!rag_backend_) return;
    kb_list_->setEnabled(false);
    const auto client = rag_backend_;
    auto* watcher = new QFutureWatcher<services::RagKnowledgeBaseListResponse>(this);
    connect(watcher, &QFutureWatcher<services::RagKnowledgeBaseListResponse>::finished,
            this, [this, watcher]() {
        const auto response = watcher->result();
        watcher->deleteLater();
        kb_list_->setEnabled(true);
        if (!response.success) {
            kb_list_->clear();
            kb_list_->addItem(QStringLiteral("RAG 服务不可用：%1")
                .arg(QString::fromStdString(response.error_message)));
            return;
        }
        knowledge_bases_ = response.items;
        bool active_still_exists = false;
        for (const auto& kb : knowledge_bases_) {
            if (QString::fromStdString(kb.id) == active_knowledge_base_id_) {
                active_still_exists = true;
                break;
            }
        }
        if (!active_still_exists && !active_knowledge_base_id_.isEmpty()) {
            active_knowledge_base_id_.clear();
            emit activeKnowledgeBaseChanged(QString(), QString());
        }
        UpdateKnowledgeBaseViews();
        RefreshDocList();
    });
    watcher->setFuture(QtConcurrent::run(
        [client]() { return client->ListKnowledgeBases(); }));
}

void KnowledgeBaseWidget::RefreshKBCombo() {
    target_kb_combo_->clear();
    for (const auto& kb : knowledge_bases_) {
        target_kb_combo_->addItem(
            QString::fromStdString(kb.name), QString::fromStdString(kb.id));
    }
}

void KnowledgeBaseWidget::RefreshDocList() {
    doc_list_->clear();
    if (!rag_backend_) return;
    const QString kb_id = target_kb_combo_->currentData().toString();
    if (kb_id.isEmpty()) return;

    doc_list_->addItem(QStringLiteral("正在从 RAG 服务读取文档状态……"));
    const auto client = rag_backend_;
    auto* watcher = new QFutureWatcher<services::RagDocumentListResponse>(this);
    connect(watcher, &QFutureWatcher<services::RagDocumentListResponse>::finished,
            this, [this, watcher, kb_id]() {
        const auto response = watcher->result();
        watcher->deleteLater();
        if (target_kb_combo_->currentData().toString() != kb_id) return;
        doc_list_->clear();
        if (!response.success) {
            doc_list_->addItem(QStringLiteral("读取失败：%1")
                .arg(QString::fromStdString(response.error_message)));
            return;
        }
        if (response.items.empty()) {
            doc_list_->addItem(QStringLiteral("尚未上传文档"));
            return;
        }
        for (const auto& document : response.items) {
            QString text = QStringLiteral("%1  [%2]  %3 个文档块")
                .arg(QString::fromStdString(document.file_name),
                     QString::fromStdString(document.status))
                .arg(document.chunk_count);
            if (!document.error_message.empty()) {
                text += QStringLiteral("  错误：%1")
                    .arg(QString::fromStdString(document.error_message));
            }
            auto* item = new QListWidgetItem(text, doc_list_);
            item->setData(Qt::UserRole, QString::fromStdString(document.id));
        }
    });
    watcher->setFuture(QtConcurrent::run([client, id = kb_id.toStdString()]() {
        return client->ListDocuments(id);
    }));
}

void KnowledgeBaseWidget::UpdateKnowledgeBaseViews() {
    kb_list_->clear();
    for (const auto& kb : knowledge_bases_) {
        const QString id = QString::fromStdString(kb.id);
        QString text = QStringLiteral("%1（%2 个文档，%3 个文档块）[%4]")
            .arg(QString::fromStdString(kb.name))
            .arg(kb.document_count)
            .arg(kb.chunk_count)
            .arg(QString::fromStdString(kb.role_type));
        auto* item = new QListWidgetItem(text, kb_list_);
        item->setData(Qt::UserRole, id);
        const bool is_active = id == active_knowledge_base_id_;
        QFont font = item->font();
        font.setBold(is_active);
        item->setFont(font);
        item->setForeground(is_active ? QColor("#1565c0") : QColor("#333"));
    }
    const QString selected_id = target_kb_combo_->currentData().toString();
    RefreshKBCombo();
    const int selected_index = target_kb_combo_->findData(selected_id);
    if (selected_index >= 0) target_kb_combo_->setCurrentIndex(selected_index);
}

void KnowledgeBaseWidget::onCreateKB() {
    bool ok = false;
    QString name = QInputDialog::getText(
        this, "新建知识库", "知识库名称：",
        QLineEdit::Normal, "", &ok);

    if (!ok || name.trimmed().isEmpty()) return;

    QString desc = QInputDialog::getText(
        this, "新建知识库", "描述（可选）：",
        QLineEdit::Normal, "", &ok);

    if (!rag_backend_) {
        QMessageBox::warning(this, "错误", "RAG 服务客户端未初始化");
        return;
    }

    const auto client = rag_backend_;
    const std::string kb_name = name.trimmed().toUtf8().toStdString();
    const std::string description = desc.trimmed().toUtf8().toStdString();
    new_kb_btn_->setEnabled(false);
    auto* watcher = new QFutureWatcher<services::RagKnowledgeBaseResponse>(this);
    connect(watcher, &QFutureWatcher<services::RagKnowledgeBaseResponse>::finished,
            this, [this, watcher]() {
        const auto response = watcher->result();
        watcher->deleteLater();
        new_kb_btn_->setEnabled(true);
        if (!response.success) {
            QMessageBox::warning(this, "错误", QStringLiteral("创建知识库失败：%1")
                .arg(QString::fromStdString(response.error_message)));
            return;
        }
        RefreshKBList();
    });
    watcher->setFuture(QtConcurrent::run([client, kb_name, description]() {
        return client->CreateKnowledgeBase(kb_name, description, "interviewer");
    }));
}

void KnowledgeBaseWidget::onDeleteKB() {
    QListWidgetItem* item = kb_list_->currentItem();
    if (!item) {
        QMessageBox::information(this, "提示", "请先选择要删除的知识库");
        return;
    }

    const QString kb_id = item->data(Qt::UserRole).toString();
    QString name = item->text();

    int ret = QMessageBox::question(
        this, "确认删除",
        QString("确定要删除知识库「%1」吗？\n所有文档块将同时被删除，不可恢复。").arg(name),
        QMessageBox::Yes | QMessageBox::No);

    if (ret != QMessageBox::Yes) return;

    if (!rag_backend_) return;
    const bool was_active = active_knowledge_base_id_ == kb_id;
    const auto client = rag_backend_;
    delete_kb_btn_->setEnabled(false);
    auto* watcher = new QFutureWatcher<services::RagOperationResponse>(this);
    connect(watcher, &QFutureWatcher<services::RagOperationResponse>::finished,
            this, [this, watcher, was_active]() {
        const auto response = watcher->result();
        watcher->deleteLater();
        delete_kb_btn_->setEnabled(true);
        if (!response.success) {
            QMessageBox::warning(this, "错误", QStringLiteral("删除失败：%1")
                .arg(QString::fromStdString(response.error_message)));
            return;
        }
        if (was_active) {
            active_knowledge_base_id_.clear();
            emit activeKnowledgeBaseChanged(QString(), QString());
        }
        RefreshKBList();
    });
    watcher->setFuture(QtConcurrent::run([client, id = kb_id.toStdString()]() {
        return client->DeleteKnowledgeBase(id);
    }));
}

void KnowledgeBaseWidget::onUseKB() {
    QListWidgetItem* item = kb_list_->currentItem();
    if (!item) {
        QMessageBox::information(this, "提示", "请先选择要启用的知识库");
        return;
    }

    const QString kb_id = item->data(Qt::UserRole).toString();
    QString kb_name;
    for (const auto& kb : knowledge_bases_) {
        if (QString::fromStdString(kb.id) == kb_id) {
                kb_name = QString::fromStdString(kb.name);
                break;
        }
    }
    active_knowledge_base_id_ = kb_id;
    rag_backend_->ResetConversation();
    LOG_INFO("Switched to remote knowledge base id={}", kb_id.toStdString());

    // 高亮选中的知识库
    for (int i = 0; i < kb_list_->count(); i++) {
        QListWidgetItem* it = kb_list_->item(i);
        bool is_active = (it->data(Qt::UserRole).toString() == kb_id);
        QFont font = it->font();
        font.setBold(is_active);
        it->setFont(font);
        if (is_active) {
            it->setForeground(QColor("#1565c0"));
        } else {
            it->setForeground(QColor("#333"));
        }
    }
    emit activeKnowledgeBaseChanged(kb_id, kb_name);
}

// ============================================================
// 文档上传
// ============================================================
void KnowledgeBaseWidget::onUploadDocument() {
    QStringList files = QFileDialog::getOpenFileNames(
        this, "选择文档",
        QString(),
        "支持格式 (*.pdf *.txt *.md *.json);;所有文件 (*.*)");

    if (files.isEmpty()) return;

    const QString kb_id = target_kb_combo_->currentData().toString();
    if (kb_id.isEmpty()) {
        QMessageBox::information(this, "提示", "请先创建并选择一个知识库");
        return;
    }

    ProcessFiles(files, kb_id);
}

void KnowledgeBaseWidget::onUploadFolder() {
    QString dir = QFileDialog::getExistingDirectory(
        this, "选择文档文件夹");

    if (dir.isEmpty()) return;

    const QString kb_id = target_kb_combo_->currentData().toString();
    if (kb_id.isEmpty()) {
        QMessageBox::information(this, "提示", "请先创建或选择一个知识库");
        return;
    }

    // 收集目录下所有支持格式的文件
    QStringList files;
    QDir qdir(dir);
    QStringList name_filters = {"*.pdf", "*.txt", "*.md", "*.json"};
    for (const auto& entry : qdir.entryInfoList(name_filters, QDir::Files)) {
        files.append(entry.absoluteFilePath());
    }

    if (files.isEmpty()) {
        QMessageBox::information(this, "提示", "文件夹中没有支持格式的文档");
        return;
    }

    ProcessFiles(files, kb_id);
}

void KnowledgeBaseWidget::ProcessFiles(
    const QStringList& files, const QString& kb_id) {
    if (!rag_backend_) {
        QMessageBox::warning(this, "错误", "RAG 服务客户端未初始化");
        return;
    }

    // 禁用按钮
    upload_btn_->setEnabled(false);
    upload_folder_btn_->setEnabled(false);
    progress_bar_->setVisible(true);
    progress_bar_->setValue(0);
    progress_label_->setVisible(true);

    int total_files = files.size();

    const auto client = rag_backend_;
    QThreadPool::globalInstance()->start(
        [this, client, files, kb_id, total_files]() {
        int accepted_files = 0;
        int failed_files = 0;
        std::string last_error;

        for (int i = 0; i < files.size(); i++) {
            QString file = files[i];
            QString file_name = QFileInfo(file).fileName();

            // 更新进度文字
            QMetaObject::invokeMethod(this, [this, file_name, i, total_files]() {
                progress_label_->setText(
                    QString("正在处理 (%1/%2)：%3 ...")
                        .arg(i + 1).arg(total_files).arg(file_name));
            }, Qt::QueuedConnection);

            try {
                const std::string file_path =
                    file.toUtf8().toStdString();
                const auto response = client->UploadDocument(
                    kb_id.toStdString(), file_path,
                    file_name.toUtf8().toStdString());
                if (response.success) {
                    ++accepted_files;
                } else {
                    ++failed_files;
                    last_error = response.error_message;
                }
            } catch (const std::exception& e) {
                ++failed_files;
                last_error = e.what();
            }

            // 更新进度条
            int percent = (i + 1) * 100 / total_files;
            QMetaObject::invokeMethod(this, [this, percent]() {
                progress_bar_->setValue(percent);
            }, Qt::QueuedConnection);
        }

        // 完成
        QMetaObject::invokeMethod(this, [this, kb_id, accepted_files,
                                        failed_files, last_error]() {
            upload_btn_->setEnabled(true);
            upload_folder_btn_->setEnabled(true);
            progress_bar_->setVisible(false);
            progress_label_->setVisible(false);

            RefreshKBList();
            RefreshDocList();
            emit knowledgeBaseUpdated(kb_id);

            const QString summary = QStringLiteral(
                "服务端已接收：%1 个文档\n上传失败：%2 个文档\n\n"
                "解析、切分、Embedding 和索引构建将在 RAG 服务端后台完成，"
                "可点击“刷新状态”查看进度。")
                .arg(accepted_files).arg(failed_files);
            if (accepted_files == 0) {
                QString details = summary;
                if (!last_error.empty()) {
                    details += "\n\n原因：" +
                        QString::fromStdString(last_error);
                }
                QMessageBox::warning(
                    this, "文档上传失败", details);
            } else {
                QMessageBox::information(
                    this, "文档已提交", summary);
            }
        }, Qt::QueuedConnection);
        });
}

void KnowledgeBaseWidget::onDeleteDocument() {
    auto* item = doc_list_->currentItem();
    const QString document_id = item
        ? item->data(Qt::UserRole).toString() : QString();
    const QString kb_id = target_kb_combo_->currentData().toString();
    if (document_id.isEmpty() || kb_id.isEmpty()) {
        QMessageBox::information(this, "提示", "请先选择要删除的文档");
        return;
    }
    if (QMessageBox::question(this, "确认删除",
            "确定删除选中文档及其服务端索引吗？",
            QMessageBox::Yes | QMessageBox::No) != QMessageBox::Yes) {
        return;
    }

    const auto client = rag_backend_;
    delete_document_btn_->setEnabled(false);
    auto* watcher = new QFutureWatcher<services::RagOperationResponse>(this);
    connect(watcher, &QFutureWatcher<services::RagOperationResponse>::finished,
            this, [this, watcher, kb_id]() {
        const auto response = watcher->result();
        watcher->deleteLater();
        delete_document_btn_->setEnabled(true);
        if (!response.success) {
            QMessageBox::warning(this, "错误", QStringLiteral("删除文档失败：%1")
                .arg(QString::fromStdString(response.error_message)));
            return;
        }
        RefreshKBList();
        RefreshDocList();
        emit knowledgeBaseUpdated(kb_id);
    });
    watcher->setFuture(QtConcurrent::run([
        client, kb = kb_id.toStdString(), doc = document_id.toStdString()]() {
        return client->DeleteDocument(kb, doc);
    }));
}

// ============================================================
// 检索测试
// ============================================================
void KnowledgeBaseWidget::onTestSearch() {
    QString query = test_query_input_->toPlainText().trimmed();
    if (query.isEmpty()) {
        QMessageBox::information(this, "提示", "请输入测试问题");
        return;
    }

    const QString kb_id = target_kb_combo_->currentData().toString();
    if (!rag_backend_ || kb_id.isEmpty()) {
        QMessageBox::warning(this, "错误", "请先连接 RAG 服务并选择知识库");
        return;
    }

    const auto& rag = common::Config::Instance().GetRAGConfig();
    const auto client = rag_backend_;
    test_search_btn_->setEnabled(false);
    test_result_output_->setPlainText("正在调用 RAG 服务检索……");
    auto* watcher = new QFutureWatcher<services::RagSearchResponse>(this);
    connect(watcher, &QFutureWatcher<services::RagSearchResponse>::finished,
            this, [this, watcher]() {
        const auto response = watcher->result();
        watcher->deleteLater();
        test_search_btn_->setEnabled(true);
        if (!response.success) {
            test_result_output_->setPlainText(QStringLiteral("检索失败：%1")
                .arg(QString::fromStdString(response.error_message)));
            return;
        }
        QString result_html = "<html><body style='font-family: Microsoft YaHei;'>";
        if (response.knowledge_found) {
            result_html += QString("<p style='color:#4caf50;font-weight:bold;'>"
                "检索到 %1 条相关结果：</p>").arg(response.sources.size());
            for (size_t i = 0; i < response.sources.size(); ++i) {
                const auto& source = response.sources[i];
                result_html += QString(
                    "<div style='margin:8px 0;padding:8px;background:#f5f5f5;"
                    "border-left:3px solid #1976d2'>"
                    "<p style='margin:0;color:#1976d2;font-weight:bold'>"
                    "#%1 [%2%] 来源：%3</p><p>%4</p></div>")
                    .arg(i + 1)
                    .arg(static_cast<int>(source.score * 100.0f))
                    .arg(QString::fromStdString(source.document_name).toHtmlEscaped())
                    .arg(QString::fromStdString(source.content).left(300).toHtmlEscaped());
            }
        } else {
            result_html += "<p style='color:#ff9800'>未检索到相关知识。</p>";
        }
        result_html += "</body></html>";
        test_result_output_->setHtml(result_html);
    });
    watcher->setFuture(QtConcurrent::run([
        client, question = query.toUtf8().toStdString(),
        id = kb_id.toStdString(), top_k = rag.top_k,
        threshold = rag.similarity_threshold]() {
        return client->Search(question, id, top_k, threshold);
    }));
}

} // namespace ui
} // namespace interview
