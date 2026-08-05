from fastapi.testclient import TestClient


def test_health_reports_configured_migrated_services(client: TestClient) -> None:
    response = client.get("/health", headers={"X-Request-ID": "test-health"})

    assert response.status_code == 200
    assert response.headers["X-Request-ID"] == "test-health"
    payload = response.json()
    assert payload["status"] == "healthy"
    assert payload["service"] == "rag-service"
    assert payload["dependencies"] == {
        "embedding": "healthy",
        "vector_store": "healthy",
        "llm": "healthy",
    }
