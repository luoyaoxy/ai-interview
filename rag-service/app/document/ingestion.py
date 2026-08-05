"""Asynchronous document parsing, embedding, and persistence pipeline."""

import asyncio
import logging
from pathlib import Path

from app.core.errors import ServiceError
from app.document.ports import DocumentProcessor
from app.embedding.ports import EmbeddingProvider
from app.vector_store.ports import StoredChunk
from app.vector_store.sqlite_store import SqliteVectorStore

logger = logging.getLogger(__name__)


def _embedding_text(content: str, metadata: dict[str, object]) -> str:
    """Include a structured heading in retrieval vectors without changing citations."""

    heading = metadata.get("heading")
    if isinstance(heading, str) and heading.strip():
        return f"{heading.strip()}\n\n{content}"
    return content


class DocumentIngestionService:
    def __init__(
        self,
        *,
        store: SqliteVectorStore,
        processor: DocumentProcessor,
        embedding: EmbeddingProvider,
        batch_size: int,
    ) -> None:
        self._store = store
        self._processor = processor
        self._embedding = embedding
        self._batch_size = batch_size

    async def process(self, document_id: str) -> None:
        document = await asyncio.to_thread(self._store.get_document_by_id, document_id)
        await asyncio.to_thread(self._store.update_document_status, document_id, "processing")
        try:
            parsed = await self._processor.process(Path(document.storage_path))
            texts = [
                _embedding_text(chunk.content, chunk.metadata)
                for chunk in parsed.chunks
            ]
            vectors: list[list[float]] = []
            for start in range(0, len(texts), self._batch_size):
                vectors.extend(await self._embedding.embed(texts[start : start + self._batch_size]))

            if len(vectors) != len(parsed.chunks):
                raise ServiceError(
                    status_code=502,
                    code="EMBEDDING_COUNT_MISMATCH",
                    message="Embedding count does not match parsed chunk count",
                )

            stored_chunks = []
            for chunk, vector in zip(parsed.chunks, vectors, strict=True):
                metadata = dict(chunk.metadata)
                metadata["source"] = document.file_name
                stored_chunks.append(
                    StoredChunk(
                        id=f"{document.id}-chunk-{chunk.sequence}",
                        knowledge_base_id=document.knowledge_base_id,
                        document_id=document.id,
                        document_name=document.file_name,
                        content=chunk.content,
                        vector=vector,
                        metadata=metadata,
                    )
                )
            written = await asyncio.to_thread(self._store.upsert_chunks, stored_chunks)
            await asyncio.to_thread(
                self._store.update_document_status,
                document_id,
                "ready",
                chunk_count=written,
            )
            logger.info("Document %s indexed with %d chunks", document_id, written)
        except Exception as exc:
            message = exc.message if isinstance(exc, ServiceError) else str(exc)
            await asyncio.to_thread(
                self._store.update_document_status,
                document_id,
                "failed",
                error_message=message[:2000],
            )
            logger.exception("Document %s ingestion failed", document_id)
