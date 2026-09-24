package com.aiinterview.app.ui.interview

import android.Manifest
import android.content.pm.PackageManager
import android.net.Uri
import androidx.activity.compose.rememberLauncherForActivityResult
import androidx.activity.result.contract.ActivityResultContracts
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.material3.AlertDialog
import androidx.compose.material3.Button
import androidx.compose.material3.Card
import androidx.compose.material3.CircularProgressIndicator
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.OutlinedButton
import androidx.compose.material3.OutlinedTextField
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.runtime.Composable
import androidx.compose.runtime.DisposableEffect
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.collectAsState
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.text.input.PasswordVisualTransformation
import androidx.compose.ui.unit.dp
import androidx.core.content.ContextCompat
import androidx.hilt.lifecycle.viewmodel.compose.hiltViewModel

@Composable
fun InterviewScreen(
    sharedResumeUri: Uri? = null,
    onSharedResumeConsumed: () -> Unit = {},
    viewModel: InterviewViewModel = hiltViewModel(),
) {
    val state by viewModel.uiState.collectAsState()
    val context = LocalContext.current
    var showTokenDialog by remember { mutableStateOf(!state.tokenConfigured) }
    var isListening by remember { mutableStateOf(false) }
    val voiceController = remember(viewModel) {
        InterviewVoiceController(
            context = context,
            onVoiceError = viewModel::showError,
        )
    }
    val audioRecorder = remember(viewModel) {
        InterviewAudioRecorder(
            onRecordingChanged = { isListening = it },
            onAudioReady = viewModel::transcribeAudio,
            onError = viewModel::showError,
        )
    }
    val microphonePermission = rememberLauncherForActivityResult(
        ActivityResultContracts.RequestPermission(),
    ) { granted ->
        if (granted) {
            viewModel.clearError()
            voiceController.stopSpeaking()
            audioRecorder.start()
        } else {
            viewModel.showError("需要麦克风权限才能使用语音回答，仍可继续文字输入")
        }
    }
    val resumePicker = rememberLauncherForActivityResult(ActivityResultContracts.OpenDocument()) {
        uri -> uri?.let(viewModel::selectResume)
    }

    DisposableEffect(voiceController) {
        onDispose {
            audioRecorder.close()
            voiceController.close()
        }
    }
    LaunchedEffect(state.interviewId, state.question) {
        if (state.stage == InterviewStage.InProgress) {
            state.question?.let(voiceController::speak)
        }
    }
    LaunchedEffect(sharedResumeUri) {
        sharedResumeUri?.let {
            viewModel.selectResume(it)
            onSharedResumeConsumed()
        }
    }

    val startVoiceAnswer = {
        if (ContextCompat.checkSelfPermission(context, Manifest.permission.RECORD_AUDIO) ==
            PackageManager.PERMISSION_GRANTED
        ) {
            viewModel.clearError()
            voiceController.stopSpeaking()
            audioRecorder.start()
        } else {
            microphonePermission.launch(Manifest.permission.RECORD_AUDIO)
        }
    }

    LazyColumn(
        modifier = Modifier
            .fillMaxSize()
            .padding(16.dp),
        verticalArrangement = Arrangement.spacedBy(12.dp),
    ) {
        item { Text("AI 模拟面试", style = MaterialTheme.typography.headlineMedium) }
        state.error?.let { error ->
            item {
                Card(Modifier.fillMaxWidth()) {
                    Row(
                        Modifier
                            .fillMaxWidth()
                            .padding(12.dp),
                        horizontalArrangement = Arrangement.SpaceBetween,
                        verticalAlignment = Alignment.CenterVertically,
                    ) {
                        Text(
                            error,
                            color = MaterialTheme.colorScheme.error,
                            modifier = Modifier.weight(1f),
                        )
                        TextButton(onClick = viewModel::clearError) { Text("关闭") }
                    }
                }
            }
        }
        if (state.isBusy) {
            item {
                Card(Modifier.fillMaxWidth()) {
                    Row(
                        Modifier
                            .fillMaxWidth()
                            .padding(16.dp),
                        horizontalArrangement = Arrangement.spacedBy(12.dp),
                        verticalAlignment = Alignment.CenterVertically,
                    ) {
                        CircularProgressIndicator()
                        Column {
                            Text(
                                state.busyMessage ?: "处理中…",
                                style = MaterialTheme.typography.titleMedium,
                            )
                            if (state.stage == InterviewStage.InProgress) {
                                Text("完成后会自动进入下一题，请勿重复操作")
                            }
                        }
                    }
                }
            }
        }
        when (state.stage) {
            InterviewStage.Setup -> {
                item {
                    SetupCard(
                        state = state,
                        onConfigureToken = { showTokenDialog = true },
                        onPickResume = {
                            resumePicker.launch(arrayOf("*/*"))
                        },
                        viewModel = viewModel,
                    )
                }
                item {
                    InterviewHistory(
                        state = state,
                        onRefresh = viewModel::refreshHistory,
                        onOpen = viewModel::openInterview,
                    )
                }
            }
            InterviewStage.InProgress -> item {
                InterviewCard(
                    state = state,
                    isListening = isListening,
                    onStartListening = startVoiceAnswer,
                    onStopListening = audioRecorder::stop,
                    onSpeakQuestion = { state.question?.let(voiceController::speak) },
                    viewModel = viewModel,
                )
            }
            InterviewStage.Completed -> item {
                ReportCard(
                    state = state,
                    onSpeakReport = {
                        state.report?.let {
                            voiceController.speak(
                                "面试结束，综合评分 ${it.overallScore.toInt()} 分。${it.summary}。${it.recommendation}",
                            )
                        }
                    },
                    onReset = viewModel::reset,
                )
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
}

@Composable
private fun InterviewHistory(
    state: InterviewUiState,
    onRefresh: () -> Unit,
    onOpen: (String) -> Unit,
) {
    Column(verticalArrangement = Arrangement.spacedBy(8.dp)) {
        Row(
            modifier = Modifier.fillMaxWidth(),
            horizontalArrangement = Arrangement.SpaceBetween,
            verticalAlignment = Alignment.CenterVertically,
        ) {
            Text("面试历史", style = MaterialTheme.typography.titleLarge)
            TextButton(
                onClick = onRefresh,
                enabled = state.tokenConfigured && !state.isBusy,
            ) { Text("刷新") }
        }
        if (!state.tokenConfigured) {
            Text("配置访问令牌后可以查看历史记录。")
        } else if (state.history.isEmpty() && !state.isBusy) {
            Text("暂无面试记录。")
        }
        state.history.forEach { interview ->
            Card(Modifier.fillMaxWidth()) {
                Row(
                    modifier = Modifier
                        .fillMaxWidth()
                        .padding(12.dp),
                    horizontalArrangement = Arrangement.SpaceBetween,
                    verticalAlignment = Alignment.CenterVertically,
                ) {
                    Column(
                        modifier = Modifier.weight(1f),
                        verticalArrangement = Arrangement.spacedBy(4.dp),
                    ) {
                        Text(
                            "${interview.candidateName} · ${interview.position}",
                            style = MaterialTheme.typography.titleMedium,
                        )
                        Text(
                            when (interview.status) {
                                "created" -> "待开始"
                                "in_progress" ->
                                    "进行中 · 第 ${interview.currentQuestionIndex + 1}/${interview.questionCount} 题"
                                "completed" ->
                                    "已完成 · ${interview.report?.overallScore?.toInt() ?: 0} 分"
                                else -> interview.status
                            },
                        )
                        Text(
                            interview.updatedAt.replace('T', ' ').take(16),
                            style = MaterialTheme.typography.bodySmall,
                        )
                    }
                    OutlinedButton(
                        onClick = { onOpen(interview.id) },
                        enabled = !state.isBusy,
                    ) {
                        Text(if (interview.status == "completed") "查看" else "继续")
                    }
                }
            }
        }
    }
}

@Composable
private fun SetupCard(
    state: InterviewUiState,
    onConfigureToken: () -> Unit,
    onPickResume: () -> Unit,
    viewModel: InterviewViewModel,
) {
    Card(Modifier.fillMaxWidth()) {
        Column(
            Modifier.padding(16.dp),
            verticalArrangement = Arrangement.spacedBy(10.dp),
        ) {
            Text("创建语音面试", style = MaterialTheme.typography.titleLarge)
            Text("问题会自动朗读；回答可使用麦克风转写，也可直接输入文字。")
            Text("服务地址：${state.serverUrl}", style = MaterialTheme.typography.bodySmall)
            Text("服务状态：${state.serverStatus}")
            Text("访问令牌：${if (state.tokenConfigured) "已安全保存" else "未配置"}")
            Row(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
                TextButton(onClick = onConfigureToken) { Text("配置服务器与令牌") }
                TextButton(onClick = viewModel::checkConnection) { Text("测试连接") }
            }
            OutlinedTextField(
                state.candidateName,
                viewModel::updateCandidateName,
                label = { Text("姓名") },
                modifier = Modifier.fillMaxWidth(),
                singleLine = true,
            )
            OutlinedTextField(
                state.position,
                viewModel::updatePosition,
                label = { Text("目标岗位") },
                modifier = Modifier.fillMaxWidth(),
                singleLine = true,
            )
            OutlinedTextField(
                state.questionCount,
                viewModel::updateQuestionCount,
                label = { Text("主问题数量（1～20）") },
                modifier = Modifier.fillMaxWidth(),
                singleLine = true,
            )
            OutlinedButton(onClick = onPickResume, enabled = !state.isBusy) {
                Text(state.resumeName ?: "选择手机中的简历（可选）")
            }
            Text(
                "支持 PDF、DOCX、TXT、MD、JSON。微信文件可先下载到手机，或在微信中选择“用其他应用打开/发送到 AI 模拟面试”。",
                style = MaterialTheme.typography.bodySmall,
            )
            Button(
                onClick = viewModel::start,
                enabled = state.tokenConfigured && !state.isBusy,
                modifier = Modifier.fillMaxWidth(),
            ) { Text("开始面试") }
        }
    }
}

@Composable
private fun InterviewCard(
    state: InterviewUiState,
    isListening: Boolean,
    onStartListening: () -> Unit,
    onStopListening: () -> Unit,
    onSpeakQuestion: () -> Unit,
    viewModel: InterviewViewModel,
) {
    Column(verticalArrangement = Arrangement.spacedBy(12.dp)) {
        Text(
            if (state.isFollowUp) {
                "第 ${state.questionNumber} 题 · 追问"
            } else {
                "第 ${state.questionNumber}/${state.totalQuestions} 题"
            },
            style = MaterialTheme.typography.titleMedium,
        )
        Card(Modifier.fillMaxWidth()) {
            Text(
                state.question.orEmpty(),
                modifier = Modifier.padding(16.dp),
                style = MaterialTheme.typography.titleLarge,
            )
        }
        Row(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
            OutlinedButton(onClick = onSpeakQuestion, enabled = !isListening) {
                Text("朗读问题")
            }
            Button(
                onClick = if (isListening) onStopListening else onStartListening,
                enabled = !state.isBusy,
            ) {
                Text(if (isListening) "结束录音" else "语音回答")
            }
        }
        if (isListening) {
            Text("正在聆听…说完后点击“结束录音”，转写文字可继续修改。")
        }
        state.latestEvaluation?.let { evaluation ->
            Card(Modifier.fillMaxWidth()) {
                Column(Modifier.padding(12.dp)) {
                    Text("上一回答评分：${evaluation.score.toInt()}/100")
                    Text(evaluation.feedback)
                }
            }
        }
        OutlinedTextField(
            state.answer,
            viewModel::updateAnswer,
            label = { Text("输入你的回答") },
            modifier = Modifier.fillMaxWidth(),
            minLines = 5,
        )
        Row(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
            Button(
                onClick = viewModel::submitAnswer,
                enabled = state.answer.isNotBlank() && !state.isBusy && !isListening,
            ) { Text("提交回答") }
            OutlinedButton(onClick = viewModel::finishEarly, enabled = !state.isBusy) {
                Text("提前结束")
            }
        }
    }
}

@Composable
private fun ReportCard(
    state: InterviewUiState,
    onSpeakReport: () -> Unit,
    onReset: () -> Unit,
) {
    val report = state.report
    Card(Modifier.fillMaxWidth()) {
        Column(
            Modifier.padding(16.dp),
            verticalArrangement = Arrangement.spacedBy(10.dp),
        ) {
            Text("面试报告", style = MaterialTheme.typography.titleLarge)
            if (report == null) {
                Text("报告生成失败，请检查服务端日志。")
            } else {
                Text("综合评分：${report.overallScore.toInt()}/100")
                Text(report.summary)
                if (report.strengths.isNotEmpty()) {
                    Text("优势", style = MaterialTheme.typography.titleMedium)
                    report.strengths.forEach { Text("• $it") }
                }
                if (report.improvements.isNotEmpty()) {
                    Text("可改进项", style = MaterialTheme.typography.titleMedium)
                    report.improvements.forEach { Text("• $it") }
                }
                if (report.recommendation.isNotBlank()) {
                    Text("建议：${report.recommendation}")
                }
            }
            Row(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
                OutlinedButton(onClick = onSpeakReport, enabled = report != null) {
                    Text("朗读报告")
                }
                Button(onClick = onReset) { Text("开始新面试") }
            }
        }
    }
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
                Text("模拟器可使用 10.0.2.2；真机请填写电脑局域网 IP 或 HTTPS 地址。")
                OutlinedTextField(
                    serverUrl,
                    { serverUrl = it },
                    label = { Text("服务地址") },
                    singleLine = true,
                )
                Text("令牌使用 Android Keystore 加密保存，不会写入 APK。")
                OutlinedTextField(
                    token,
                    { token = it },
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
