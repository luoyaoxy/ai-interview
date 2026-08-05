#include "services/rag_comparison.h"

#include <chrono>
#include <condition_variable>
#include <iostream>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <string>

namespace {

using namespace interview::services;

struct ParallelGate {
    std::mutex mutex;
    std::condition_variable ready;
    int arrivals = 0;
    bool ran_in_parallel = false;

    void ArriveAndWait() {
        std::unique_lock<std::mutex> lock(mutex);
        ++arrivals;
        if (arrivals == 2) {
            ran_in_parallel = true;
            ready.notify_all();
            return;
        }
        ready.wait_for(lock, std::chrono::seconds(2), [this]() {
            return arrivals >= 2;
        });
    }
};

class FakeBackend final : public RagBackend {
public:
    FakeBackend(std::string name,
                std::string source_name,
                std::shared_ptr<ParallelGate> gate,
                bool throw_requests = false)
        : name_(std::move(name)),
          source_name_(std::move(source_name)),
          gate_(std::move(gate)),
          throw_requests_(throw_requests) {}

    std::string GetBackendName() const override { return name_; }

    RagQueryResponse Ask(const std::string& question,
                         const std::string& knowledge_base_id,
                         const std::string&) override {
        if (throw_requests_) throw std::runtime_error("service unavailable");
        last_question_ = question;
        last_knowledge_base_id_ = knowledge_base_id;
        RagQueryResponse response;
        response.answer = "中文回答：虚函数支持运行时多态。";
        response.sources.push_back(MakeSource());
        response.knowledge_found = true;
        response.success = true;
        return response;
    }

    RagSearchResponse Search(const std::string& question,
                             const std::string& knowledge_base_id,
                             int,
                             float) override {
        gate_->ArriveAndWait();
        if (throw_requests_) throw std::runtime_error("service unavailable");
        last_question_ = question;
        last_knowledge_base_id_ = knowledge_base_id;
        RagSearchResponse response;
        response.sources.push_back(MakeSource());
        response.knowledge_found = true;
        response.success = true;
        return response;
    }

    RagKnowledgeBaseListResponse ListKnowledgeBases() override { return {}; }
    RagKnowledgeBaseResponse CreateKnowledgeBase(
        const std::string&, const std::string&, const std::string&) override {
        return {};
    }
    RagOperationResponse DeleteKnowledgeBase(const std::string&) override {
        return {};
    }
    RagDocumentListResponse ListDocuments(const std::string&) override {
        return {};
    }
    RagDocumentResponse UploadDocument(
        const std::string&, const std::string&, const std::string&) override {
        return {};
    }
    RagOperationResponse DeleteDocument(
        const std::string&, const std::string&) override {
        return {};
    }
    void ResetConversation() override {}
    std::string GetConversationID() const override { return {}; }

    const std::string& LastKnowledgeBaseID() const {
        return last_knowledge_base_id_;
    }

private:
    RagSource MakeSource() const {
        RagSource source;
        source.document_name = source_name_;
        source.content = "虚函数允许派生类覆盖基类行为。";
        source.score = 0.92f;
        return source;
    }

    std::string name_;
    std::string source_name_;
    std::shared_ptr<ParallelGate> gate_;
    bool throw_requests_ = false;
    std::string last_question_;
    std::string last_knowledge_base_id_;
};

bool Check(bool condition, const char* message) {
    if (condition) return true;
    std::cerr << "FAILED: " << message << '\n';
    return false;
}

} // namespace

int main() {
    bool passed = true;
    auto gate = std::make_shared<ParallelGate>();
    auto local = std::make_shared<FakeBackend>(
        "local", "本地-C++基础.pdf", gate);
    auto remote = std::make_shared<FakeBackend>(
        "remote", "远程-C++基础.pdf", gate);

    RagComparisonRunner runner(local, remote);
    const auto report = runner.Compare(
        "什么是虚函数？", "local-kb-1", "remote-kb-a", 3, 0.7f);

    passed &= Check(gate->ran_in_parallel, "backends must run concurrently");
    passed &= Check(report.local.query.success, "local answer must succeed");
    passed &= Check(report.remote.query.success, "remote answer must succeed");
    passed &= Check(report.local.utf8_valid && report.remote.utf8_valid,
                    "Chinese UTF-8 must remain valid");
    passed &= Check(report.local.citations_valid && report.remote.citations_valid,
                    "complete citations must be valid");
    passed &= Check(report.citation_name_jaccard == 0.0f,
                    "different source names must not overlap");
    passed &= Check(local->LastKnowledgeBaseID() == "local-kb-1",
                    "local knowledge base ID must stay isolated");
    passed &= Check(remote->LastKnowledgeBaseID() == "remote-kb-a",
                    "remote knowledge base ID must stay isolated");

    auto failure_gate = std::make_shared<ParallelGate>();
    auto healthy_local = std::make_shared<FakeBackend>(
        "local", "local.pdf", failure_gate);
    auto unavailable_remote = std::make_shared<FakeBackend>(
        "remote", "remote.pdf", failure_gate, true);
    RagComparisonRunner failure_runner(healthy_local, unavailable_remote);
    const auto failure_report = failure_runner.Compare(
        "服务故障验证", "1", "remote-kb", 3, 0.7f);
    passed &= Check(failure_report.local.query.success,
                    "remote failure must not discard local answer");
    passed &= Check(!failure_report.remote.query.success &&
                        !failure_report.remote.query.error_message.empty(),
                    "remote failure must be reported instead of thrown");

    return passed ? 0 : 1;
}
