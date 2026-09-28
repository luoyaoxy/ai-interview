# Retrofit, OkHttp, Kotlin serialization, Compose, and Hilt publish consumer
# rules. Preserve generic signatures and runtime annotations used at the HTTP
# boundary while still allowing application code to be optimized.
-keepattributes Signature
-keepattributes RuntimeVisibleAnnotations,RuntimeVisibleParameterAnnotations
-keepattributes AnnotationDefault

# Serialized API models are intentionally retained by name to keep crash and
# production payload diagnostics readable. Their generated serializers remain
# reachable from the Kotlin serialization plugin.
-keepnames class com.aiinterview.app.data.remote.**
