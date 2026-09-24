# Interview API

The interview workflow is hosted by the FastAPI service under `/api/v1`.
Every endpoint requires `Authorization: Bearer <RAG_API_KEY>`.

## Flow

1. `POST /interviews` creates a session with candidate name, target position,
   and 1–20 main questions.
2. `POST /interviews/{id}/resume` optionally uploads a PDF, TXT, Markdown, or
   JSON resume before the interview starts.
3. `POST /interviews/{id}/start` asks the configured LLM to create the question
   set and returns the first question.
4. `POST /interviews/{id}/answers` evaluates an answer and returns either one
   follow-up question, the next main question, or a completed interview report.
5. `POST /interviews/{id}/finish` ends an in-progress interview early and
   generates a report.
6. `GET /interviews/{id}` returns the session, and
   `GET /interviews/{id}/report` returns its completed report.
7. `GET /interviews?limit=50` returns the most recently updated sessions for
   history and recovery screens.

An interview response includes `current_question` and `is_follow_up`. A client
can therefore restore the exact pending turn after navigation, process restart,
or service restart without generating a duplicate question.

## Storage and security

Session state and reports are atomically persisted to
`RAG_INTERVIEW_STORE_PATH` (default `./data/interviews.json`). Uploaded resumes
are stored below `RAG_UPLOAD_DIR/interviews`. LLM credentials remain on the
server and must never be embedded in the Android APK.

The current JSON store is intended for a single FastAPI process. Use a database
with concurrency control before running multiple service instances.
