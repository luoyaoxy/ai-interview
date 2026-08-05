import asyncio
import json

import httpx

from app.embedding.http_provider import HttpEmbeddingProvider
from app.llm.http_provider import HttpLlmProvider
from app.llm.ports import ChatMessage


def test_embedding_adapter_accepts_ollama_and_openai_responses() -> None:
    responses = [
        httpx.Response(200, json={"embeddings": [[1, 2], [3, 4]]}),
        httpx.Response(
            200,
            json={
                "data": [
                    {"index": 1, "embedding": [3, 4]},
                    {"index": 0, "embedding": [1, 2]},
                ]
            },
        ),
    ]

    def handler(_request: httpx.Request) -> httpx.Response:
        return responses.pop(0)

    provider = HttpEmbeddingProvider(
        api_url="https://embedding.test/v1/embed",
        api_key="",
        model="test-embedding",
        provider="ollama",
        timeout_seconds=1,
        verify_ssl=True,
        transport=httpx.MockTransport(handler),
    )

    ollama_vectors = asyncio.run(provider.embed(["one", "two"]))
    openai_vectors = asyncio.run(provider.embed(["one", "two"]))

    assert ollama_vectors == [[1.0, 2.0], [3.0, 4.0]]
    assert openai_vectors == ollama_vectors
    assert asyncio.run(provider.dimension()) == 2


def test_llm_adapter_uses_standard_openai_payload_by_default() -> None:
    captured_payload: dict[str, object] = {}

    def handler(request: httpx.Request) -> httpx.Response:
        captured_payload.update(json.loads(request.content))
        return httpx.Response(
            200,
            json={"choices": [{"message": {"content": "测试回答"}}]},
        )

    provider = HttpLlmProvider(
        api_url="https://llm.test/v1/chat/completions",
        api_key="key",
        provider="openai",
        model="test-model",
        temperature=0.3,
        max_tokens=100,
        timeout_seconds=1,
        verify_ssl=True,
        transport=httpx.MockTransport(handler),
    )

    answer = asyncio.run(provider.generate([ChatMessage(role="user", content="你好")]))

    assert answer == "测试回答"
    assert captured_payload["model"] == "test-model"
    assert "model_config" not in captured_payload
