package com.aiinterview.app.ui.navigation

enum class AppDestination(
    val route: String,
    val label: String,
    val shortLabel: String,
) {
    Interview("interview", "模拟面试", "面"),
    Knowledge("knowledge", "知识库", "库"),
    Assistant("assistant", "AI 助手", "问"),
}
