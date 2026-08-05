from fastapi.testclient import TestClient

from app.services import ServiceContainer

AUTHORIZATION = {"Authorization": "Bearer change-me"}


def test_complete_document_search_and_query_pipeline(
    client: TestClient,
    services: ServiceContainer,
) -> None:
    create_response = client.post(
        "/api/v1/knowledge-bases",
        headers=AUTHORIZATION,
        json={
            "name": "C++ 知识库",
            "description": "用于迁移测试",
            "role_type": "general_assistant",
        },
    )
    assert create_response.status_code == 201
    knowledge_base_id = create_response.json()["knowledge_base"]["id"]

    upload_response = client.post(
        f"/api/v1/knowledge-bases/{knowledge_base_id}/documents",
        headers=AUTHORIZATION,
        files={
            "file": (
                "cpp.json",
                '[{"question":"什么是虚函数？","answer":"虚函数通过动态绑定实现运行时多态。"}]',
                "application/json",
            )
        },
    )
    assert upload_response.status_code == 202
    document_id = upload_response.json()["document"]["id"]

    document_response = client.get(
        f"/api/v1/knowledge-bases/{knowledge_base_id}/documents/{document_id}",
        headers=AUTHORIZATION,
    )
    assert document_response.status_code == 200
    assert document_response.json()["document"]["status"] == "ready"
    assert document_response.json()["document"]["chunk_count"] == 1

    knowledge_base_response = client.get(
        f"/api/v1/knowledge-bases/{knowledge_base_id}",
        headers=AUTHORIZATION,
    )
    assert knowledge_base_response.json()["knowledge_base"]["document_count"] == 1
    assert knowledge_base_response.json()["knowledge_base"]["chunk_count"] == 1

    search_response = client.post(
        "/api/v1/rag/search",
        headers=AUTHORIZATION,
        json={
            "question": "什么是虚函数？",
            "knowledge_base_id": knowledge_base_id,
            "top_k": 3,
            "similarity_threshold": 0.7,
        },
    )
    assert search_response.status_code == 200
    assert search_response.json()["knowledge_found"] is True
    assert search_response.json()["sources"][0]["document_name"] == "cpp.json"

    query_response = client.post(
        "/api/v1/rag/query",
        headers=AUTHORIZATION,
        json={
            "question": "什么是虚函数？",
            "knowledge_base_id": knowledge_base_id,
            "conversation_id": "test-conversation",
        },
    )
    assert query_response.status_code == 200
    payload = query_response.json()
    assert payload["knowledge_found"] is True
    assert "动态绑定" in payload["answer"]
    assert payload["sources"]
    assert services.llm.requests
    assert "参考知识库" in services.llm.requests[0][0].content

    clear_response = client.delete(
        "/api/v1/rag/conversations/test-conversation",
        headers=AUTHORIZATION,
    )
    assert clear_response.status_code == 204

    delete_document_response = client.delete(
        f"/api/v1/knowledge-bases/{knowledge_base_id}/documents/{document_id}",
        headers=AUTHORIZATION,
    )
    assert delete_document_response.status_code == 204

    delete_kb_response = client.delete(
        f"/api/v1/knowledge-bases/{knowledge_base_id}",
        headers=AUTHORIZATION,
    )
    assert delete_kb_response.status_code == 204


def test_query_without_matching_sources_uses_role_fallback(
    client: TestClient,
    services: ServiceContainer,
) -> None:
    create_response = client.post(
        "/api/v1/knowledge-bases",
        headers=AUTHORIZATION,
        json={"name": "空知识库", "role_type": "general_assistant"},
    )
    knowledge_base_id = create_response.json()["knowledge_base"]["id"]

    query_response = client.post(
        "/api/v1/rag/query",
        headers=AUTHORIZATION,
        json={"question": "没有资料的问题", "knowledge_base_id": knowledge_base_id},
    )

    assert query_response.status_code == 200
    assert query_response.json()["knowledge_found"] is False
    assert query_response.json()["sources"] == []
    assert "没有检索到" in query_response.json()["answer"]
    assert services.llm.requests == []


def test_markdown_heading_participates_in_embedding_but_not_citation_content(
    client: TestClient,
    services: ServiceContainer,
) -> None:
    create_response = client.post(
        "/api/v1/knowledge-bases",
        headers=AUTHORIZATION,
        json={"name": "Markdown 问答库", "role_type": "general_assistant"},
    )
    knowledge_base_id = create_response.json()["knowledge_base"]["id"]

    upload_response = client.post(
        f"/api/v1/knowledge-bases/{knowledge_base_id}/documents",
        headers=AUTHORIZATION,
        files={
            "file": (
                "cpp.md",
                "# 什么是虚函数？\n虚函数通过动态绑定实现运行时多态。",
                "text/markdown",
            )
        },
    )
    assert upload_response.status_code == 202
    embedded_texts = [
        text
        for request in services.embedding.requests
        for text in request
    ]
    assert "什么是虚函数？\n\n虚函数通过动态绑定实现运行时多态。" in embedded_texts

    search_response = client.post(
        "/api/v1/rag/search",
        headers=AUTHORIZATION,
        json={
            "question": "什么是虚函数？",
            "knowledge_base_id": knowledge_base_id,
            "similarity_threshold": 0.0,
        },
    )
    assert search_response.status_code == 200
    source = search_response.json()["sources"][0]
    assert source["content"] == "虚函数通过动态绑定实现运行时多态。"
