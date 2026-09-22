"""SQLite persistence and cosine-similarity search implementation."""

import json
import math
import sqlite3
import threading
from datetime import UTC, datetime
from pathlib import Path
from uuid import uuid4

from app.core.errors import ServiceError
from app.retrieval.lexical import lexical_score
from app.vector_store.ports import (
    DocumentRecord,
    KnowledgeBaseRecord,
    SearchMatch,
    StoredChunk,
)


def _now() -> datetime:
    return datetime.now(UTC)


def _to_iso(value: datetime) -> str:
    return value.isoformat().replace("+00:00", "Z")


def _from_iso(value: str) -> datetime:
    return datetime.fromisoformat(value.replace("Z", "+00:00"))


def _cosine_similarity(left: list[float], right: list[float]) -> float:
    if not left or len(left) != len(right):
        return -1.0
    dot = sum(a * b for a, b in zip(left, right, strict=True))
    left_norm = math.sqrt(sum(value * value for value in left))
    right_norm = math.sqrt(sum(value * value for value in right))
    if left_norm == 0.0 or right_norm == 0.0:
        return -1.0
    return dot / (left_norm * right_norm)


class SqliteVectorStore:
    """Own all RAG metadata and vectors in a service-private SQLite database."""

    def __init__(self, database_path: Path) -> None:
        database_path.parent.mkdir(parents=True, exist_ok=True)
        self._connection = sqlite3.connect(database_path, check_same_thread=False)
        self._connection.row_factory = sqlite3.Row
        self._lock = threading.RLock()
        with self._lock:
            self._connection.execute("PRAGMA foreign_keys = ON")
            self._connection.execute("PRAGMA journal_mode = WAL")
            self._create_schema()

    def close(self) -> None:
        with self._lock:
            self._connection.close()

    def fail_incomplete_documents(self) -> int:
        """Make interrupted in-process background work visible after a restart."""

        with self._lock:
            cursor = self._connection.execute(
                """
                UPDATE documents
                SET status = 'failed',
                    error_message = 'Document processing was interrupted by a service restart',
                    updated_at = ?
                WHERE status IN ('queued', 'processing')
                """,
                (_to_iso(_now()),),
            )
            self._connection.commit()
            return cursor.rowcount

    def _create_schema(self) -> None:
        self._connection.executescript(
            """
            CREATE TABLE IF NOT EXISTS knowledge_bases (
                id TEXT PRIMARY KEY,
                name TEXT NOT NULL UNIQUE,
                description TEXT NOT NULL DEFAULT '',
                role_type TEXT NOT NULL DEFAULT 'default',
                created_at TEXT NOT NULL,
                updated_at TEXT NOT NULL
            );

            CREATE TABLE IF NOT EXISTS documents (
                id TEXT PRIMARY KEY,
                knowledge_base_id TEXT NOT NULL,
                file_name TEXT NOT NULL,
                storage_path TEXT NOT NULL,
                status TEXT NOT NULL,
                chunk_count INTEGER NOT NULL DEFAULT 0,
                error_message TEXT NOT NULL DEFAULT '',
                created_at TEXT NOT NULL,
                updated_at TEXT NOT NULL,
                FOREIGN KEY (knowledge_base_id) REFERENCES knowledge_bases(id)
                    ON DELETE CASCADE
            );

            CREATE TABLE IF NOT EXISTS document_chunks (
                id TEXT PRIMARY KEY,
                knowledge_base_id TEXT NOT NULL,
                document_id TEXT NOT NULL,
                document_name TEXT NOT NULL,
                sequence INTEGER NOT NULL,
                content TEXT NOT NULL,
                vector_json TEXT NOT NULL,
                page INTEGER,
                metadata_json TEXT NOT NULL DEFAULT '{}',
                FOREIGN KEY (knowledge_base_id) REFERENCES knowledge_bases(id)
                    ON DELETE CASCADE,
                FOREIGN KEY (document_id) REFERENCES documents(id)
                    ON DELETE CASCADE
            );

            CREATE INDEX IF NOT EXISTS idx_documents_kb
                ON documents(knowledge_base_id, created_at DESC);
            CREATE INDEX IF NOT EXISTS idx_chunks_kb
                ON document_chunks(knowledge_base_id);
            CREATE INDEX IF NOT EXISTS idx_chunks_document
                ON document_chunks(document_id);
            """
        )
        self._connection.commit()

    def create_knowledge_base(
        self,
        name: str,
        description: str,
        role_type: str,
    ) -> KnowledgeBaseRecord:
        knowledge_base_id = f"kb-{uuid4()}"
        now = _now()
        with self._lock:
            try:
                self._connection.execute(
                    """
                    INSERT INTO knowledge_bases
                        (id, name, description, role_type, created_at, updated_at)
                    VALUES (?, ?, ?, ?, ?, ?)
                    """,
                    (
                        knowledge_base_id,
                        name,
                        description,
                        role_type,
                        _to_iso(now),
                        _to_iso(now),
                    ),
                )
                self._connection.commit()
            except sqlite3.IntegrityError as exc:
                raise ServiceError(
                    status_code=409,
                    code="KNOWLEDGE_BASE_NAME_CONFLICT",
                    message=f"Knowledge base name '{name}' already exists",
                ) from exc
        return self.get_knowledge_base(knowledge_base_id)

    def list_knowledge_bases(
        self,
        offset: int,
        page_size: int,
    ) -> tuple[list[KnowledgeBaseRecord], int]:
        with self._lock:
            total = int(
                self._connection.execute("SELECT COUNT(*) FROM knowledge_bases").fetchone()[0]
            )
            rows = self._connection.execute(
                """
                SELECT k.*,
                       (SELECT COUNT(*) FROM documents d
                        WHERE d.knowledge_base_id = k.id) AS document_count,
                       (SELECT COUNT(*) FROM document_chunks c
                        WHERE c.knowledge_base_id = k.id) AS chunk_count
                FROM knowledge_bases k
                ORDER BY k.created_at DESC
                LIMIT ? OFFSET ?
                """,
                (page_size, offset),
            ).fetchall()
        return [self._knowledge_base_from_row(row) for row in rows], total

    def get_knowledge_base(self, knowledge_base_id: str) -> KnowledgeBaseRecord:
        with self._lock:
            row = self._connection.execute(
                """
                SELECT k.*,
                       (SELECT COUNT(*) FROM documents d
                        WHERE d.knowledge_base_id = k.id) AS document_count,
                       (SELECT COUNT(*) FROM document_chunks c
                        WHERE c.knowledge_base_id = k.id) AS chunk_count
                FROM knowledge_bases k
                WHERE k.id = ?
                """,
                (knowledge_base_id,),
            ).fetchone()
        if row is None:
            raise ServiceError(
                status_code=404,
                code="KNOWLEDGE_BASE_NOT_FOUND",
                message=f"Knowledge base '{knowledge_base_id}' does not exist",
            )
        return self._knowledge_base_from_row(row)

    def update_knowledge_base(
        self,
        knowledge_base_id: str,
        *,
        name: str | None,
        description: str | None,
        role_type: str | None,
    ) -> KnowledgeBaseRecord:
        current = self.get_knowledge_base(knowledge_base_id)
        with self._lock:
            try:
                self._connection.execute(
                    """
                    UPDATE knowledge_bases
                    SET name = ?, description = ?, role_type = ?, updated_at = ?
                    WHERE id = ?
                    """,
                    (
                        name if name is not None else current.name,
                        description if description is not None else current.description,
                        role_type if role_type is not None else current.role_type,
                        _to_iso(_now()),
                        knowledge_base_id,
                    ),
                )
                self._connection.commit()
            except sqlite3.IntegrityError as exc:
                raise ServiceError(
                    status_code=409,
                    code="KNOWLEDGE_BASE_NAME_CONFLICT",
                    message=f"Knowledge base name '{name}' already exists",
                ) from exc
        return self.get_knowledge_base(knowledge_base_id)

    def delete_knowledge_base(self, knowledge_base_id: str) -> list[str]:
        self.get_knowledge_base(knowledge_base_id)
        with self._lock:
            active_documents = int(
                self._connection.execute(
                    """
                    SELECT COUNT(*) FROM documents
                    WHERE knowledge_base_id = ? AND status IN ('queued', 'processing')
                    """,
                    (knowledge_base_id,),
                ).fetchone()[0]
            )
            if active_documents:
                raise ServiceError(
                    status_code=409,
                    code="KNOWLEDGE_BASE_PROCESSING",
                    message="A knowledge base cannot be deleted while documents are processing",
                )
            paths = [
                str(row[0])
                for row in self._connection.execute(
                    "SELECT storage_path FROM documents WHERE knowledge_base_id = ?",
                    (knowledge_base_id,),
                ).fetchall()
            ]
            self._connection.execute(
                "DELETE FROM knowledge_bases WHERE id = ?", (knowledge_base_id,)
            )
            self._connection.commit()
        return paths

    def create_document(
        self,
        knowledge_base_id: str,
        file_name: str,
        storage_path: str,
    ) -> DocumentRecord:
        self.get_knowledge_base(knowledge_base_id)
        document_id = f"doc-{uuid4()}"
        now = _now()
        with self._lock:
            self._connection.execute(
                """
                INSERT INTO documents
                    (id, knowledge_base_id, file_name, storage_path, status,
                     chunk_count, error_message, created_at, updated_at)
                VALUES (?, ?, ?, ?, 'queued', 0, '', ?, ?)
                """,
                (
                    document_id,
                    knowledge_base_id,
                    file_name,
                    storage_path,
                    _to_iso(now),
                    _to_iso(now),
                ),
            )
            self._connection.commit()
        return self.get_document(knowledge_base_id, document_id)

    def list_documents(
        self,
        knowledge_base_id: str,
        offset: int,
        page_size: int,
        status: str | None,
    ) -> tuple[list[DocumentRecord], int]:
        self.get_knowledge_base(knowledge_base_id)
        where = "knowledge_base_id = ?"
        parameters: list[object] = [knowledge_base_id]
        if status is not None:
            where += " AND status = ?"
            parameters.append(status)
        with self._lock:
            total = int(
                self._connection.execute(
                    f"SELECT COUNT(*) FROM documents WHERE {where}", parameters
                ).fetchone()[0]
            )
            rows = self._connection.execute(
                f"""
                SELECT * FROM documents WHERE {where}
                ORDER BY created_at DESC LIMIT ? OFFSET ?
                """,
                [*parameters, page_size, offset],
            ).fetchall()
        return [self._document_from_row(row) for row in rows], total

    def get_document(self, knowledge_base_id: str, document_id: str) -> DocumentRecord:
        with self._lock:
            row = self._connection.execute(
                "SELECT * FROM documents WHERE knowledge_base_id = ? AND id = ?",
                (knowledge_base_id, document_id),
            ).fetchone()
        if row is None:
            raise ServiceError(
                status_code=404,
                code="DOCUMENT_NOT_FOUND",
                message=f"Document '{document_id}' does not exist in this knowledge base",
            )
        return self._document_from_row(row)

    def get_document_by_id(self, document_id: str) -> DocumentRecord:
        with self._lock:
            row = self._connection.execute(
                "SELECT * FROM documents WHERE id = ?", (document_id,)
            ).fetchone()
        if row is None:
            raise ServiceError(
                status_code=404,
                code="DOCUMENT_NOT_FOUND",
                message=f"Document '{document_id}' does not exist",
            )
        return self._document_from_row(row)

    def update_document_status(
        self,
        document_id: str,
        status: str,
        *,
        chunk_count: int = 0,
        error_message: str = "",
    ) -> None:
        with self._lock:
            self._connection.execute(
                """
                UPDATE documents
                SET status = ?, chunk_count = ?, error_message = ?, updated_at = ?
                WHERE id = ?
                """,
                (status, chunk_count, error_message, _to_iso(_now()), document_id),
            )
            self._connection.commit()

    def delete_document(self, knowledge_base_id: str, document_id: str) -> str:
        document = self.get_document(knowledge_base_id, document_id)
        if document.status in {"queued", "processing"}:
            raise ServiceError(
                status_code=409,
                code="DOCUMENT_PROCESSING",
                message="A document cannot be deleted while it is processing",
            )
        with self._lock:
            self._connection.execute("DELETE FROM documents WHERE id = ?", (document_id,))
            self._connection.commit()
        return document.storage_path

    def upsert_chunks(self, chunks: list[StoredChunk]) -> int:
        if not chunks:
            return 0
        document_id = chunks[0].document_id
        with self._lock:
            self._connection.execute(
                "DELETE FROM document_chunks WHERE document_id = ?", (document_id,)
            )
            self._connection.executemany(
                """
                INSERT INTO document_chunks
                    (id, knowledge_base_id, document_id, document_name, sequence,
                     content, vector_json, page, metadata_json)
                VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?)
                """,
                [
                    (
                        chunk.id,
                        chunk.knowledge_base_id,
                        chunk.document_id,
                        chunk.document_name,
                        int(chunk.metadata.get("chunk_index", index)),
                        chunk.content,
                        json.dumps(chunk.vector),
                        chunk.metadata.get("page"),
                        json.dumps(chunk.metadata, ensure_ascii=False),
                    )
                    for index, chunk in enumerate(chunks)
                ],
            )
            self._connection.commit()
        return len(chunks)

    def search(
        self,
        knowledge_base_id: str,
        query_vector: list[float],
        *,
        top_k: int,
        similarity_threshold: float,
    ) -> list[SearchMatch]:
        self.get_knowledge_base(knowledge_base_id)
        with self._lock:
            rows = self._connection.execute(
                "SELECT * FROM document_chunks WHERE knowledge_base_id = ?",
                (knowledge_base_id,),
            ).fetchall()
        matches: list[SearchMatch] = []
        for row in rows:
            vector = json.loads(row["vector_json"])
            score = _cosine_similarity(query_vector, vector)
            if score < similarity_threshold:
                continue
            metadata = json.loads(row["metadata_json"])
            if row["page"] is not None:
                metadata["page"] = int(row["page"])
            matches.append(
                SearchMatch(
                    chunk=StoredChunk(
                        id=str(row["id"]),
                        knowledge_base_id=str(row["knowledge_base_id"]),
                        document_id=str(row["document_id"]),
                        document_name=str(row["document_name"]),
                        content=str(row["content"]),
                        vector=vector,
                        metadata=metadata,
                    ),
                    score=score,
                )
            )
        matches.sort(key=lambda match: match.score, reverse=True)
        return matches[:top_k]

    def search_keywords(
        self, knowledge_base_id: str, question: str, *, top_k: int
    ) -> list[SearchMatch]:
        """Find lexical candidates within one knowledge base, including Chinese bigrams."""
        self.get_knowledge_base(knowledge_base_id)
        with self._lock:
            rows = self._connection.execute(
                "SELECT * FROM document_chunks WHERE knowledge_base_id = ?",
                (knowledge_base_id,),
            ).fetchall()
        matches: list[SearchMatch] = []
        for row in rows:
            score = lexical_score(question, str(row["content"]))
            if score <= 0.0:
                continue
            metadata = json.loads(row["metadata_json"])
            if row["page"] is not None:
                metadata["page"] = int(row["page"])
            matches.append(
                SearchMatch(
                    chunk=StoredChunk(
                        id=str(row["id"]),
                        knowledge_base_id=str(row["knowledge_base_id"]),
                        document_id=str(row["document_id"]),
                        document_name=str(row["document_name"]),
                        content=str(row["content"]),
                        vector=[],
                        metadata=metadata,
                    ),
                    score=score,
                )
            )
        matches.sort(key=lambda match: (-match.score, match.chunk.id))
        return matches[:top_k]

    @staticmethod
    def _knowledge_base_from_row(row: sqlite3.Row) -> KnowledgeBaseRecord:
        return KnowledgeBaseRecord(
            id=str(row["id"]),
            name=str(row["name"]),
            description=str(row["description"]),
            role_type=str(row["role_type"]),
            document_count=int(row["document_count"]),
            chunk_count=int(row["chunk_count"]),
            created_at=_from_iso(str(row["created_at"])),
            updated_at=_from_iso(str(row["updated_at"])),
        )

    @staticmethod
    def _document_from_row(row: sqlite3.Row) -> DocumentRecord:
        return DocumentRecord(
            id=str(row["id"]),
            knowledge_base_id=str(row["knowledge_base_id"]),
            file_name=str(row["file_name"]),
            storage_path=str(row["storage_path"]),
            status=str(row["status"]),
            chunk_count=int(row["chunk_count"]),
            error_message=str(row["error_message"]),
            created_at=_from_iso(str(row["created_at"])),
            updated_at=_from_iso(str(row["updated_at"])),
        )
