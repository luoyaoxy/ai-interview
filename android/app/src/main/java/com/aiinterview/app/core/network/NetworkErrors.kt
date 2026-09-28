package com.aiinterview.app.core.network

import java.io.IOException
import retrofit2.HttpException

fun Throwable.toUserMessage(): String = when (this) {
    is HttpException -> when (code()) {
        400 -> "请求参数不正确"
        401 -> "访问令牌无效，请重新设置"
        404 -> "请求的资源不存在"
        409 -> "当前资源状态不允许执行该操作"
        413 -> "文档大小超过服务端限制"
        415 -> "不支持该文档格式"
        429 -> "请求过于频繁，请稍后重试"
        in 500..599 -> "服务暂时不可用（HTTP ${code()}）"
        else -> "请求失败（HTTP ${code()}）"
    }
    is IOException -> "无法连接服务器，请检查地址和网络"
    else -> message ?: "发生未知错误"
}
