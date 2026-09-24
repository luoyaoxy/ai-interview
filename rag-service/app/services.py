"""Composition root for concrete RAG service capabilities."""

from functools import lru_cache

from app.core.config import get_settings
from app.document.ingestion import DocumentIngestionService
from app.document.processor import LocalDocumentProcessor
from app.embedding.http_provider import HttpEmbeddingProvider
from app.interview.service import InterviewService
from app.llm.http_provider import HttpLlmProvider
from app.rag.conversations import ConversationStore
from app.rag.prompt_builder import RagPromptBuilder
from app.rag.service import RagService
from app.retrieval.service import SemanticRetriever
from app.speech.doubao import DoubaoSpeechTranscriber
from app.vector_store.sqlite_store import SqliteVectorStore


class ServiceContainer:
    def __init__(self) -> None:
        settings = get_settings()
        settings.data_dir.mkdir(parents=True, exist_ok=True)
        settings.upload_dir.mkdir(parents=True, exist_ok=True)

        self.settings = settings
        self.store = SqliteVectorStore(settings.vector_db_path)
        self.store.fail_incomplete_documents()
        self.document_processor = LocalDocumentProcessor(
            settings.chunk_size,
            settings.chunk_overlap,
        )
        self.embedding = HttpEmbeddingProvider(
            api_url=settings.embedding_api_url,
            api_key=settings.embedding_api_key,
            model=settings.embedding_model,
            provider=settings.embedding_provider,
            timeout_seconds=settings.embedding_timeout_seconds,
            verify_ssl=settings.verify_ssl,
        )
        self.ingestion = DocumentIngestionService(
            store=self.store,
            processor=self.document_processor,
            embedding=self.embedding,
            batch_size=settings.embedding_batch_size,
        )
        self.retriever = SemanticRetriever(
            self.embedding,
            self.store,
            mode=settings.retrieval_mode,
            candidate_k=settings.retrieval_candidate_k,
            lexical_threshold=settings.lexical_threshold,
        )
        self.llm = HttpLlmProvider(
            api_url=settings.llm_api_url,
            api_key=settings.llm_api_key,
            provider=settings.llm_provider,
            model=settings.llm_model,
            temperature=settings.llm_temperature,
            max_tokens=settings.llm_max_tokens,
            timeout_seconds=settings.llm_timeout_seconds,
            verify_ssl=settings.verify_ssl,
        )
        self.interviews = InterviewService(
            llm=self.llm,
            document_processor=self.document_processor,
            store_path=settings.interview_store_path,
        )
        self.speech = DoubaoSpeechTranscriber(
            ws_url=settings.speech_ws_url,
            app_id=settings.speech_app_id,
            access_key=settings.speech_access_key,
            app_key=settings.speech_app_key,
            resource_id=settings.speech_resource_id,
            timeout_seconds=settings.speech_timeout_seconds,
        )
        self.conversations = ConversationStore(settings.max_history_turns)
        self.prompt_builder = RagPromptBuilder()
        self.rag = RagService(
            store=self.store,
            retriever=self.retriever,
            llm=self.llm,
            prompt_builder=self.prompt_builder,
            conversations=self.conversations,
            top_k=settings.top_k,
            similarity_threshold=settings.similarity_threshold,
        )

    def close(self) -> None:
        self.store.close()


@lru_cache
def get_services() -> ServiceContainer:
    return ServiceContainer()
