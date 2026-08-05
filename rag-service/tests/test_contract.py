from fastapi.testclient import TestClient

from app.main import app

EXPECTED_OPERATIONS = {
    "getHealth",
    "createKnowledgeBase",
    "listKnowledgeBases",
    "getKnowledgeBase",
    "updateKnowledgeBase",
    "deleteKnowledgeBase",
    "uploadDocument",
    "listDocuments",
    "getDocument",
    "deleteDocument",
    "searchKnowledgeBase",
    "queryRag",
    "clearRagConversation",
}


def test_all_designed_operations_are_registered() -> None:
    schema = app.openapi()
    operations = {
        operation["operationId"]
        for path_item in schema["paths"].values()
        for method, operation in path_item.items()
        if method in {"get", "post", "patch", "delete"}
    }

    assert operations == EXPECTED_OPERATIONS


def test_private_endpoint_requires_bearer_token(client: TestClient) -> None:
    response = client.post(
        "/api/v1/rag/query",
        json={"question": "什么是虚函数？", "knowledge_base_id": "kb-cpp"},
    )

    assert response.status_code == 401
    assert response.json()["error"]["code"] == "UNAUTHORIZED"


def test_custom_validation_error_uses_the_unified_error_format(
    client: TestClient,
) -> None:
    response = client.patch(
        "/api/v1/knowledge-bases/kb-cpp",
        headers={"Authorization": "Bearer change-me"},
        json={},
    )

    assert response.status_code == 400
    payload = response.json()
    assert payload["error"]["code"] == "VALIDATION_ERROR"
    assert payload["error"]["details"]["errors"]
