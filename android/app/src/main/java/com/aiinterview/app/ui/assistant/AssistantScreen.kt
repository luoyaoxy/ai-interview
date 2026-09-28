package com.aiinterview.app.ui.assistant

import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.lazy.items
import androidx.compose.material3.Button
import androidx.compose.material3.Card
import androidx.compose.material3.CardDefaults
import androidx.compose.material3.CircularProgressIndicator
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.OutlinedTextField
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.runtime.Composable
import androidx.compose.runtime.collectAsState
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Modifier
import androidx.compose.ui.text.font.FontFamily
import androidx.compose.ui.unit.dp
import androidx.hilt.lifecycle.viewmodel.compose.hiltViewModel

@Composable
fun AssistantScreen(viewModel: AssistantViewModel = hiltViewModel()) {
    val state by viewModel.uiState.collectAsState()
    var question by remember { mutableStateOf("") }

    Column(
        modifier = Modifier
            .fillMaxSize()
            .padding(16.dp),
        verticalArrangement = Arrangement.spacedBy(8.dp),
    ) {
        Row(
            modifier = Modifier.fillMaxWidth(),
            horizontalArrangement = Arrangement.SpaceBetween,
        ) {
            Column {
                Text("AI 面试助手", style = MaterialTheme.typography.headlineMedium)
                Text(
                    state.activeKnowledgeBase?.let { "当前知识库：${it.name}" }
                        ?: "尚未选择知识库",
                    style = MaterialTheme.typography.bodyMedium,
                )
            }
            TextButton(onClick = viewModel::clearConversation) { Text("清空会话") }
        }

        state.error?.let {
            Text(it, color = MaterialTheme.colorScheme.error)
        }

        LazyColumn(
            modifier = Modifier.weight(1f),
            verticalArrangement = Arrangement.spacedBy(8.dp),
        ) {
            if (state.messages.isEmpty()) {
                item {
                    Text("在下方输入技术问题，回答将只依据当前知识库生成。")
                }
            }
            items(state.messages, key = ChatMessage::id) { message ->
                MessageCard(message)
            }
            if (state.isSending) {
                item {
                    Row(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
                        CircularProgressIndicator()
                        Text("正在检索知识库并生成回答…")
                    }
                }
            }
        }

        Row(
            modifier = Modifier.fillMaxWidth(),
            horizontalArrangement = Arrangement.spacedBy(8.dp),
        ) {
            OutlinedTextField(
                value = question,
                onValueChange = { question = it },
                modifier = Modifier.weight(1f),
                label = { Text("输入问题") },
                minLines = 1,
                maxLines = 4,
            )
            Button(
                onClick = {
                    viewModel.send(question)
                    if (question.isNotBlank()) question = ""
                },
                enabled = question.isNotBlank() && !state.isSending,
            ) { Text("发送") }
        }
    }
}

@Composable
private fun MessageCard(message: ChatMessage) {
    val isUser = message.author == MessageAuthor.User
    Card(
        modifier = Modifier.fillMaxWidth(),
        colors = CardDefaults.cardColors(
            containerColor = if (isUser) {
                MaterialTheme.colorScheme.primaryContainer
            } else {
                MaterialTheme.colorScheme.surfaceVariant
            },
        ),
    ) {
        Column(
            modifier = Modifier.padding(12.dp),
            verticalArrangement = Arrangement.spacedBy(6.dp),
        ) {
            Text(if (isUser) "你" else "AI 助手", style = MaterialTheme.typography.labelLarge)
            MarkdownLikeText(message.text)
            if (message.sources.isNotEmpty()) {
                Text("参考资料", style = MaterialTheme.typography.titleSmall)
                message.sources.forEachIndexed { index, source ->
                    Card(colors = CardDefaults.cardColors(MaterialTheme.colorScheme.surface)) {
                        Column(Modifier.padding(8.dp)) {
                            Text(
                                "[${index + 1}] ${source.documentName}" +
                                    (source.page?.let { " · 第 $it 页" } ?: "") +
                                    " · ${(source.score * 100).toInt()}%",
                                style = MaterialTheme.typography.labelMedium,
                            )
                            Text(source.content, style = MaterialTheme.typography.bodySmall)
                        }
                    }
                }
            }
        }
    }
}

@Composable
private fun MarkdownLikeText(markdown: String) {
    Column(verticalArrangement = Arrangement.spacedBy(3.dp)) {
        markdown.lines().forEach { line ->
            when {
                line.startsWith("### ") -> Text(
                    line.removePrefix("### "),
                    style = MaterialTheme.typography.titleSmall,
                )
                line.startsWith("## ") -> Text(
                    line.removePrefix("## "),
                    style = MaterialTheme.typography.titleMedium,
                )
                line.startsWith("# ") -> Text(
                    line.removePrefix("# "),
                    style = MaterialTheme.typography.titleLarge,
                )
                line.startsWith("```") -> Text(line, fontFamily = FontFamily.Monospace)
                else -> Text(line)
            }
        }
    }
}
