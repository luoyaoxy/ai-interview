# AI Interview Android

Android client scaffold for the AI interview system.

## Requirements

- Android Studio compatible with Android Gradle Plugin 9.4
- JDK 17
- Android SDK Platform 37

## Run

Open this `android` directory in Android Studio, sync Gradle, and run the `app`
configuration. The initial debug address is `http://10.0.2.2:8000/`, but the
server address can be changed at runtime from the interview or knowledge-base
screen. Release builds initially show `https://api.example.com/` and should be
configured with the real HTTPS deployment address.

Never put DeepSeek, speech-provider, or RAG service secrets in this project.

## RAG features

The knowledge-base screen now supports service health checks, knowledge-base
creation/selection/deletion, document upload/status/deletion, and selection of
the active knowledge base. The assistant screen supports multi-turn RAG queries,
source display, and clearing a conversation.

The service bearer token can be entered on the interview or knowledge-base
screen. It is encrypted with an AES-GCM key held by Android Keystore and can be
restored after an app restart; it is never embedded in the APK. The non-secret
server address is also persisted locally.

## Interview flow

The interview screen is connected to the FastAPI service. It can create an
interview, optionally upload a PDF/TXT/Markdown/JSON resume, show generated
questions, submit text answers, display per-answer feedback and follow-up
questions, finish early, and render the final report.

The setup screen also loads interview history from the service. Completed
sessions can reopen their report, while created or in-progress sessions can be
continued from the exact pending question, including a pending follow-up.

The backend owns the LLM credentials, interview state, evaluation logic, and
the persisted report. Configure `RAG_LLM_API_URL`, `RAG_LLM_API_KEY`, and
`RAG_LLM_MODEL` in `rag-service/.env` before starting a real interview.

## Physical phone connection

1. Put the phone and development computer on the same trusted Wi-Fi network.
2. Start the service on all interfaces, for example:
   `uvicorn app.main:app --host 0.0.0.0 --port 8000`.
3. Find the computer's LAN IPv4 address with `ipconfig`, for example
   `192.168.1.20`.
4. Allow inbound TCP port 8000 on the Windows private-network firewall.
5. Verify `http://192.168.1.20:8000/health` in the phone browser.
6. In the app, open "配置服务器与令牌" and enter
   `http://192.168.1.20:8000/` plus the value of `RAG_API_KEY`.

The debug manifest permits cleartext HTTP for local testing. Release builds do
not permit cleartext traffic and must use a valid HTTPS endpoint. Do not expose
the development Uvicorn port directly to the public internet.

The client uses a 15-second connection timeout, 120-second read/write timeouts,
and a 150-second total call timeout so LLM-backed interview turns can complete
without hanging indefinitely. The interview setup screen includes a connection
test that calls `/health` and displays the service status and version.

Before publishing, run the real-device checklist in
[`../docs/testing/android-acceptance.md`](../docs/testing/android-acceptance.md).

## Release builds

Production URL, version code, version name, and signing credentials are supplied
through environment variables or Gradle properties. The release build refuses a
non-HTTPS production URL, enables R8 and resource shrinking, and can produce a
signed APK and AAB without storing credentials in the repository.

See [`../docs/deployment/android-release.md`](../docs/deployment/android-release.md)
for keystore creation, local build, GitHub Secrets, signature verification, and
store rollout instructions.

The Android client now uses the platform `SpeechRecognizer` and `TextToSpeech`
APIs. Questions are read aloud automatically, the candidate can dictate an
answer and edit the transcript before submitting, and the final report can be
read aloud. `RECORD_AUDIO` is requested only when the microphone button is used.

Speech recognition availability, supported languages, offline behavior, and
whether audio is sent to a device vendor depend on the recognition service
installed on the phone. Manual text input remains available as a fallback. This
implementation is turn-based; full-duplex streaming, custom cloud ASR/TTS, and
voice interruption are intentionally left for a later phase.
