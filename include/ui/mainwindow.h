/**
 * @file mainwindow.h
 * @brief 主窗口接口及其持有的会话、界面和 RAG 服务状态。
 *
 * 模块划分：界面构建、用户操作槽、状态更新和服务注入。
 */

#pragma once

#include <QMainWindow>
#include <QTabWidget>
#include <QThread>
#include <QTextEdit>
#include <QPushButton>
#include <QLabel>
#include <QProgressBar>
#include <memory>

#include "common/interview_state.h"

// 前向声明 RAG 服务类
namespace interview {
namespace session {
    class DialogSession;
}
namespace services {
    class RagBackend;
}
}

namespace interview {
namespace ui {

class KnowledgeBaseWidget;
class KnowledgeChatWidget;

using common::InterviewState;

class MainWindow : public QMainWindow {
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow();

    void SetRAGBackend(std::shared_ptr<services::RagBackend> rag_backend);

private slots:
    void OnNewSession();
    void OnStartSession();

    // 状态机回调触发的UI更新
    void OnStateChangedFromMachine(InterviewState old_state, InterviewState new_state);

private:
    void SetupUi();
    void SetupMenuBar();
    void SetupToolBar();
    void SetupCentralWidget();
    void UpdateUIState(InterviewState state);
    QString GetStateText(InterviewState state);
    void AppendMessage(const QString& text, const QString& color = "black");

    QTabWidget* tab_widget_ = nullptr;
    QTextEdit* message_area_ = nullptr;
    QLabel* status_label_ = nullptr;
    QProgressBar* progress_bar_ = nullptr;
    QPushButton* start_button_ = nullptr;
    QPushButton* new_session_button_ = nullptr;

    std::unique_ptr<session::DialogSession> session_;
    std::thread session_thread_;

    QString candidate_name_;
    QString resume_path_;
    int min_questions_ = 15;

    // RAG 服务（第七步新增）
    KnowledgeBaseWidget* kb_widget_ = nullptr;
    KnowledgeChatWidget* knowledge_chat_widget_ = nullptr;
    std::shared_ptr<services::RagBackend> rag_backend_;
};

} // namespace ui
} // namespace interview

