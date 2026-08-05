#include "common/logger.h"
#include "services/document_loader.h"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>

int main() {
    namespace fs = std::filesystem;
    using interview::services::DocumentLoader;

    const fs::path markdown_path =
        fs::current_path() / fs::u8path(u8"markdown_标题分块测试.md");
    const fs::path log_path =
        fs::current_path() / "document_loader_markdown_test.log";

    interview::common::Logger::Init(log_path.string(), false);

    const std::string first_heading =
        "C++成员变量的初始化顺序是固定的吗？";
    const std::string second_heading =
        "C++ 多线程开发需要注意些什么？线程同步有哪些手段？";

    {
        std::ofstream output(markdown_path, std::ios::binary);
        output
            << "### " << first_heading << "\n\n"
            << "C++成员变量的初始化顺序只与成员变量在类中的声明顺序有关，"
               "与构造函数初始化列表中的书写顺序无关。\n\n"
            << "### " << second_heading << "\n\n"
            << "C++多线程开发需要关注线程安全问题，主要包括数据竞争、"
               "死锁、内存可见性以及生命周期管理。\n";
    }

    DocumentLoader loader;
    loader.SetChunkConfig({500, 50, true});
    const auto chunks = loader.LoadDocument(markdown_path.u8string());

    bool ok = chunks.size() == 2;
    if (ok) {
        ok =
            chunks[0].content.find(first_heading) != std::string::npos &&
            chunks[0].content.find(second_heading) == std::string::npos &&
            chunks[1].content.find(second_heading) != std::string::npos &&
            chunks[1].content.find(first_heading) == std::string::npos;
    }

    std::error_code remove_error;
    fs::remove(markdown_path, remove_error);

    if (!ok) {
        std::cerr << "Markdown heading boundary test failed. chunk_count="
                  << chunks.size() << '\n';
        for (size_t i = 0; i < chunks.size(); ++i) {
            std::cerr << "--- chunk " << i << " ---\n"
                      << chunks[i].content << '\n';
        }
        return 1;
    }

    std::cout << "Markdown heading boundary test passed: "
              << chunks.size() << " independent chunks\n";
    return 0;
}
