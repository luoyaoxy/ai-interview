package com.aiinterview.app.ui.knowledge

import androidx.activity.compose.rememberLauncherForActivityResult
import androidx.activity.result.contract.ActivityResultContracts
import androidx.compose.foundation.BorderStroke
import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.lazy.items
import androidx.compose.material3.AlertDialog
import androidx.compose.material3.Button
import androidx.compose.material3.Card
import androidx.compose.material3.CardDefaults
import androidx.compose.material3.CircularProgressIndicator
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.OutlinedButton
import androidx.compose.material3.OutlinedTextField
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.runtime.Composable
import androidx.compose.runtime.collectAsState
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.text.input.PasswordVisualTransformation
import androidx.compose.ui.unit.dp
import androidx.hilt.lifecycle.viewmodel.compose.hiltViewModel
import com.aiinterview.app.domain.model.DocumentStatus
import com.aiinterview.app.domain.model.KnowledgeBase
import com.aiinterview.app.domain.model.KnowledgeDocument

@Composable
fun KnowledgeScreen(viewModel: KnowledgeViewModel = hiltViewModel()) {
    val state by viewModel.uiState.collectAsState()
    var showTokenDialog by remember { mutableStateOf(!state.tokenConfigured) }
    var showCreateDialog by remember { mutableStateOf(false) }
    val filePicker = rememberLauncherForActivityResult(ActivityResultContracts.OpenDocument()) { uri ->
        uri?.let(viewModel::uploadDocument)
    }

    Column(
        modifier = Modifier
            .fillMaxSize()
            .padding(16.dp),
    ) {
        Text("知识库", style = MaterialTheme.typography.headlineMedium)
        Spacer(Modifier.height(8.dp))
        ConnectionCard(
            serverUrl = state.serverUrl,
            status = state.serverStatus,
            tokenConfigured = state.tokenConfigured,
            onConfigure = { showTokenDialog = true },
        )
        state.error?.let {
            Notice(text = it, isError = true, onDismiss = viewModel::clearNotice)
        }
        state.message?.let {
            Notice(text = it, isError = false, onDismiss = viewModel::clearNotice)
        }
        Row(
            modifier = Modifier
                .fillMaxWidth()
                .padding(vertical = 12.dp),
            horizontalArrangement = Arrangement.spacedBy(8.dp),
            verticalAlignment = Alignment.CenterVertically,
        ) {
            Button(
                onClick = { showCreateDialog = true },
                enabled = state.tokenConfigured && !state.isBusy,
            ) { Text("新建知识库") }
            OutlinedButton(
                onClick = viewModel::refreshKnowledgeBases,
                enabled = state.tokenConfigured && !state.isBusy,
            ) { Text("刷新") }
            if (state.isBusy) CircularProgressIndicator()
        }

        LazyColumn(
            modifier = Modifier.fillMaxSize(),
            verticalArrangement = Arrangement.spacedBy(8.dp),
        ) {
            item { Text("知识库列表", style = MaterialTheme.typography.titleMedium) }
            if (state.knowledgeBases.isEmpty()) {
                item { Text("暂无知识库，请先配置令牌并创建知识库。") }
            }
            items(state.knowledgeBases, key = KnowledgeBase::id) { item ->
                KnowledgeBaseCard(
                    item = item,
                    selected = state.selectedKnowledgeBase?.id == item.id,
                    onSelect = { viewModel.selectKnowledgeBase(item) },
                )
            }

            state.selectedKnowledgeBase?.let { selected ->
                item {
                    Row(
                        modifier = Modifier
                            .fillMaxWidth()
                            .padding(top = 12.dp),
                        horizontalArrangement = Arrangement.spacedBy(8.dp),
                    ) {
                        Button(
                            onClick = {
                                filePicker.launch(
                                    arrayOf(
                                        "application/pdf",
                                        "text/plain",
                                        "text/markdown",
                                        "application/json",
                                    ),
                                )
                            },
                            enabled = !state.isBusy,
                        ) { Text("上传文档") }
                        OutlinedButton(onClick = viewModel::refreshDocuments) {
                            Text("刷新状态")
                        }
                        TextButton(onClick = viewModel::deleteSelectedKnowledgeBase) {
                            Text("删除知识库")
                        }
                    }
                }
                item {
                    Text(
                        "${selected.name} 的文档",
                        style = MaterialTheme.typography.titleMedium,
                    )
                }
                if (state.documents.isEmpty()) item { Text("暂无文档") }
                items(state.documents, key = KnowledgeDocument::id) { document ->
                    DocumentCard(
                        document = document,
                        onDelete = { viewModel.deleteDocument(document.id) },
                    )
                }
            }
        }
    }

    if (showTokenDialog) {
        TokenDialog(
            canDismiss = state.tokenConfigured,
            currentServerUrl = state.serverUrl,
            onDismiss = { showTokenDialog = false },
            onConfirm = { serverUrl, token ->
                if (viewModel.configureConnection(serverUrl, token)) {
                    showTokenDialog = false
                }
            },
        )
    }
    if (showCreateDialog) {
        CreateKnowledgeBaseDialog(
            onDismiss = { showCreateDialog = false },
            onConfirm = { name, description ->
                viewModel.createKnowledgeBase(name, description)
                showCreateDialog = false
            },
        )
    }
}

@Composable
private fun ConnectionCard(
    serverUrl: String,
    status: String,
    tokenConfigured: Boolean,
    onConfigure: () -> Unit,
) {
    Card(modifier = Modifier.fillMaxWidth()) {
        Column(Modifier.padding(12.dp)) {
            Text("服务地址：$serverUrl", style = MaterialTheme.typography.bodySmall)
            Text("服务状态：$status")
            Text("访问令牌：${if (tokenConfigured) "已安全保存" else "未设置"}")
            TextButton(onClick = onConfigure) { Text("配置服务器与令牌") }
        }
    }
}

@Composable
private fun KnowledgeBaseCard(
    item: KnowledgeBase,
    selected: Boolean,
    onSelect: () -> Unit,
) {
    Card(
        modifier = Modifier
            .fillMaxWidth()
            .clickable(onClick = onSelect),
        border = if (selected) BorderStroke(2.dp, MaterialTheme.colorScheme.primary) else null,
        colors = CardDefaults.cardColors(
            containerColor = if (selected) {
                MaterialTheme.colorScheme.primaryContainer
            } else {
                MaterialTheme.colorScheme.surfaceVariant
            },
        ),
    ) {
        Column(Modifier.padding(12.dp)) {
            Text(item.name, style = MaterialTheme.typography.titleMedium)
            if (item.description.isNotBlank()) Text(item.description)
            Text("文档 ${item.documentCount} · 片段 ${item.chunkCount}")
        }
    }
}

@Composable
private fun DocumentCard(document: KnowledgeDocument, onDelete: () -> Unit) {
    Card(modifier = Modifier.fillMaxWidth()) {
        Row(
            modifier = Modifier
                .fillMaxWidth()
                .padding(12.dp),
            horizontalArrangement = Arrangement.SpaceBetween,
            verticalAlignment = Alignment.CenterVertically,
        ) {
            Column(Modifier.weight(1f)) {
                Text(document.fileName, style = MaterialTheme.typography.titleSmall)
                Text("状态：${document.status.displayName()} · 片段 ${document.chunkCount}")
                if (document.errorMessage.isNotBlank()) {
                    Text(document.errorMessage, color = MaterialTheme.colorScheme.error)
                }
            }
            TextButton(onClick = onDelete) { Text("删除") }
        }
    }
}

private fun DocumentStatus.displayName() = when (this) {
    DocumentStatus.Queued -> "排队中"
    DocumentStatus.Processing -> "处理中"
    DocumentStatus.Ready -> "可用"
    DocumentStatus.Failed -> "失败"
    DocumentStatus.Unknown -> "未知"
}

@Composable
private fun TokenDialog(
    canDismiss: Boolean,
    currentServerUrl: String,
    onDismiss: () -> Unit,
    onConfirm: (String, String) -> Unit,
) {
    var serverUrl by remember(currentServerUrl) { mutableStateOf(currentServerUrl) }
    var token by remember { mutableStateOf("") }
    AlertDialog(
        onDismissRequest = { if (canDismiss) onDismiss() },
        title = { Text("服务器连接") },
        text = {
            Column(verticalArrangement = Arrangement.spacedBy(8.dp)) {
                Text("真机局域网地址示例：http://192.168.1.20:8000/")
                OutlinedTextField(
                    value = serverUrl,
                    onValueChange = { serverUrl = it },
                    label = { Text("服务地址") },
                    singleLine = true,
                )
                Text("令牌使用 Android Keystore 加密保存，不会写入 APK。")
                OutlinedTextField(
                    value = token,
                    onValueChange = { token = it },
                    label = { Text("Bearer Token") },
                    visualTransformation = PasswordVisualTransformation(),
                    singleLine = true,
                )
            }
        },
        confirmButton = {
            TextButton(
                onClick = { onConfirm(serverUrl, token) },
                enabled = serverUrl.isNotBlank() && token.isNotBlank(),
            ) {
                Text("保存")
            }
        },
        dismissButton = if (canDismiss) {
            { TextButton(onClick = onDismiss) { Text("取消") } }
        } else null,
    )
}

@Composable
private fun CreateKnowledgeBaseDialog(
    onDismiss: () -> Unit,
    onConfirm: (String, String) -> Unit,
) {
    var name by remember { mutableStateOf("") }
    var description by remember { mutableStateOf("") }
    AlertDialog(
        onDismissRequest = onDismiss,
        title = { Text("新建知识库") },
        text = {
            Column(verticalArrangement = Arrangement.spacedBy(8.dp)) {
                OutlinedTextField(name, { name = it }, label = { Text("名称") })
                OutlinedTextField(
                    description,
                    { description = it },
                    label = { Text("描述（可选）") },
                )
            }
        },
        confirmButton = {
            TextButton(onClick = { onConfirm(name, description) }, enabled = name.isNotBlank()) {
                Text("创建")
            }
        },
        dismissButton = { TextButton(onClick = onDismiss) { Text("取消") } },
    )
}

@Composable
private fun Notice(text: String, isError: Boolean, onDismiss: () -> Unit) {
    Card(
        modifier = Modifier
            .fillMaxWidth()
            .padding(top = 8.dp),
        colors = CardDefaults.cardColors(
            containerColor = if (isError) {
                MaterialTheme.colorScheme.errorContainer
            } else {
                MaterialTheme.colorScheme.secondaryContainer
            },
        ),
    ) {
        Row(
            modifier = Modifier
                .fillMaxWidth()
                .padding(8.dp),
            horizontalArrangement = Arrangement.SpaceBetween,
            verticalAlignment = Alignment.CenterVertically,
        ) {
            Text(text, modifier = Modifier.weight(1f))
            TextButton(onClick = onDismiss) { Text("关闭") }
        }
    }
}
