from app.interview.service import InterviewService


def auth() -> dict[str, str]:
    return {"Authorization": "Bearer change-me"}


def test_complete_text_interview_and_read_report(client):
    created = client.post(
        "/api/v1/interviews",
        headers=auth(),
        json={
            "candidate_name": "Test Candidate",
            "position": "Android Engineer",
            "question_count": 2,
        },
    )
    assert created.status_code == 201
    interview_id = created.json()["interview"]["id"]

    resume = client.post(
        f"/api/v1/interviews/{interview_id}/resume",
        headers=auth(),
        files={"file": ("resume.txt", "Kotlin, coroutines, Compose", "text/plain")},
    )
    assert resume.status_code == 200
    assert resume.json()["interview"]["resume_file_name"] == "resume.txt"

    started = client.post(
        f"/api/v1/interviews/{interview_id}/start", headers=auth()
    )
    assert started.status_code == 200
    duplicate_start = client.post(
        f"/api/v1/interviews/{interview_id}/start", headers=auth()
    )
    assert duplicate_start.status_code == 409
    assert started.json()["question_number"] == 1
    snapshot = client.get(f"/api/v1/interviews/{interview_id}", headers=auth())
    assert snapshot.json()["interview"]["current_question"] == started.json()["question"]
    assert snapshot.json()["interview"]["is_follow_up"] is False

    first = client.post(
        f"/api/v1/interviews/{interview_id}/answers",
        headers=auth(),
        json={"answer": "A parent scope waits for children and propagates cancellation."},
    )
    assert first.status_code == 200
    assert first.json()["evaluation"]["score"] == 82
    assert first.json()["question_number"] == 2

    second = client.post(
        f"/api/v1/interviews/{interview_id}/answers",
        headers=auth(),
        json={"answer": "The local database is the single source of truth."},
    )
    assert second.status_code == 200
    assert second.json()["question"] is None
    assert second.json()["interview"]["status"] == "completed"

    report = client.get(
        f"/api/v1/interviews/{interview_id}/report", headers=auth()
    )
    assert report.status_code == 200
    assert report.json()["report"]["overall_score"] == 82
    assert report.json()["report"]["strengths"] == ["Kotlin fundamentals"]

    history = client.get("/api/v1/interviews", headers=auth())
    assert history.status_code == 200
    assert history.json()["total"] == 1
    restored = history.json()["items"][0]
    assert restored["id"] == interview_id
    assert restored["status"] == "completed"
    assert restored["current_question"] is None


def test_interview_requires_auth_and_valid_state(client):
    assert client.post("/api/v1/interviews", json={}).status_code == 401

    created = client.post(
        "/api/v1/interviews",
        headers=auth(),
        json={"candidate_name": "A", "position": "Android", "question_count": 1},
    )
    interview_id = created.json()["interview"]["id"]
    response = client.post(
        f"/api/v1/interviews/{interview_id}/answers",
        headers=auth(),
        json={"answer": "Not started yet"},
    )
    assert response.status_code == 409
    assert response.json()["error"]["code"] == "INVALID_INTERVIEW_STATE"

    report = client.get(
        f"/api/v1/interviews/{interview_id}/report", headers=auth()
    )
    assert report.status_code == 409
    assert report.json()["error"]["code"] == "REPORT_NOT_READY"


def test_follow_up_survives_service_restart(client, services):
    services.llm.follow_up_once = True
    created = client.post(
        "/api/v1/interviews",
        headers=auth(),
        json={"candidate_name": "B", "position": "Android", "question_count": 1},
    )
    interview_id = created.json()["interview"]["id"]
    started = client.post(
        f"/api/v1/interviews/{interview_id}/start", headers=auth()
    )
    assert started.status_code == 200

    first = client.post(
        f"/api/v1/interviews/{interview_id}/answers",
        headers=auth(),
        json={"answer": "A very short answer"},
    )
    assert first.status_code == 200
    assert first.json()["is_follow_up"] is True
    assert first.json()["question"] == "Can you give a concrete example?"

    services.interviews = InterviewService(
        llm=services.llm,
        document_processor=services.document_processor,
        store_path=services.settings.interview_store_path,
    )
    restored = client.get(f"/api/v1/interviews/{interview_id}", headers=auth())
    assert restored.json()["interview"]["is_follow_up"] is True
    assert (
        restored.json()["interview"]["current_question"]
        == "Can you give a concrete example?"
    )

    completed = client.post(
        f"/api/v1/interviews/{interview_id}/answers",
        headers=auth(),
        json={"answer": "For example, viewModelScope cancels children on clear."},
    )
    assert completed.status_code == 200
    assert completed.json()["interview"]["status"] == "completed"
    assert completed.json()["interview"]["report"] is not None
