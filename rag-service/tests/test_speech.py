from fastapi.testclient import TestClient


def test_transcribe_pcm_audio(client: TestClient) -> None:
    response = client.post(
        "/api/v1/speech/transcribe",
        headers={"Authorization": "Bearer change-me", "Content-Type": "audio/pcm"},
        content=b"\x00\x01" * 1600,
    )

    assert response.status_code == 200
    assert response.json()["text"] == "这是手机录音的测试转写"


def test_transcribe_requires_audio(client: TestClient) -> None:
    response = client.post(
        "/api/v1/speech/transcribe",
        headers={"Authorization": "Bearer change-me", "Content-Type": "audio/pcm"},
        content=b"",
    )

    assert response.status_code == 422
    assert response.json()["error"]["code"] == "EMPTY_AUDIO"
