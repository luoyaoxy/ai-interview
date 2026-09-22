from collections.abc import Iterator

import pytest
from fastapi.testclient import TestClient

from app.api.dependencies import get_settings
from app.document.ingestion import DocumentIngestionService
from app.llm.ports import ChatMessage
from app.main import app
from app.rag.conversations import ConversationStore
from app.rag.prompt_builder import RagPromptBuilder
from app.rag.service import RagService
from app.retrieval.service import SemanticRetriever
from app.services import ServiceContainer, get_services


class FakeEmbeddingProvider:
    def __init__(self) -> None:
        self.requests: list[list[str]] = []

    async def embed(self, texts: list[str]) -> list[list[float]]:
        self.requests.append(list(texts))
        return [self._vector(text) for text in texts]

    async def dimension(self) -> int:
        return 3

    @staticmethod
    def _vector(text: str) -> list[float]:
        lowered = text.lower()
        if "虚函数" in text or "virtual" in lowered:
            return [1.0, 0.0, 0.0]
        if "智能指针" in text or "smart pointer" in lowered:
            return [0.0, 1.0, 0.0]
        return [0.0, 0.0, 1.0]


class FakeLlmProvider:
    def __init__(self) -> None:
        self.requests: list[list[ChatMessage]] = []

    async def generate(self, messages: list[ChatMessage]) -> str:
        self.requests.append(messages)
        return "根据知识库，虚函数通过动态绑定实现运行时多态。[1]"


@pytest.fixture
def services(tmp_path, monkeypatch) -> Iterator[ServiceContainer]:
    monkeypatch.setenv("RAG_API_KEY", "change-me")
    monkeypatch.setenv("RAG_DATA_DIR", str(tmp_path))
    monkeypatch.setenv("RAG_VECTOR_DB_PATH", str(tmp_path / "rag.db"))
    monkeypatch.setenv("RAG_UPLOAD_DIR", str(tmp_path / "uploads"))
    monkeypatch.setenv("RAG_LLM_API_URL", "https://llm.test/v1/chat/completions")
    monkeypatch.setenv("RAG_LLM_API_KEY", "test-llm-key")
    monkeypatch.setenv("RAG_LLM_MODEL", "test-model")
    get_settings.cache_clear()
    get_services.cache_clear()

    container = ServiceContainer()
    container.embedding = FakeEmbeddingProvider()
    container.ingestion = DocumentIngestionService(
        store=container.store,
        processor=container.document_processor,
        embedding=container.embedding,
        batch_size=container.settings.embedding_batch_size,
    )
    container.retriever = SemanticRetriever(container.embedding, container.store)
    container.llm = FakeLlmProvider()
    container.conversations = ConversationStore(container.settings.max_history_turns)
    container.prompt_builder = RagPromptBuilder()
    container.rag = RagService(
        store=container.store,
        retriever=container.retriever,
        llm=container.llm,
        prompt_builder=container.prompt_builder,
        conversations=container.conversations,
        top_k=container.settings.top_k,
        similarity_threshold=container.settings.similarity_threshold,
    )

    yield container

    container.close()
    get_services.cache_clear()
    get_settings.cache_clear()


@pytest.fixture
def client(services: ServiceContainer) -> Iterator[TestClient]:
    app.dependency_overrides[get_services] = lambda: services
    with TestClient(app) as test_client:
        yield test_client
    app.dependency_overrides.clear()
