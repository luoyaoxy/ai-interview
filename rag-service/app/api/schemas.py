"""Pydantic request and response models for the public HTTP contract."""

from datetime import datetime
from enum import StrEnum
from typing import Any

from pydantic import BaseModel, ConfigDict, Field, model_validator


class StrictModel(BaseModel):
    model_config = ConfigDict(extra="forbid")


class DependencyStatus(StrEnum):
    HEALTHY = "healthy"
    UNHEALTHY = "unhealthy"
    UNKNOWN = "unknown"


class ServiceStatus(StrEnum):
    HEALTHY = "healthy"
    DEGRADED = "degraded"
    UNHEALTHY = "unhealthy"


class RoleType(StrEnum):
    DEFAULT = "default"
    INTERVIEWER = "interviewer"
    GENERAL_ASSISTANT = "general_assistant"
    CUSTOM = "custom"


class DocumentStatus(StrEnum):
    QUEUED = "queued"
    PROCESSING = "processing"
    READY = "ready"
    FAILED = "failed"


class HealthDependencies(BaseModel):
    embedding: DependencyStatus
    vector_store: DependencyStatus
    llm: DependencyStatus


class HealthResponse(BaseModel):
    status: ServiceStatus
    service: str
    version: str
    time: datetime
    dependencies: HealthDependencies


class ErrorBody(BaseModel):
    code: str
    message: str
    details: dict[str, Any] = Field(default_factory=dict)


class ErrorResponse(BaseModel):
    request_id: str
    error: ErrorBody


class CreateKnowledgeBaseRequest(StrictModel):
    name: str = Field(min_length=1, max_length=128)
    description: str = Field(default="", max_length=2000)
    role_type: RoleType = RoleType.DEFAULT


class UpdateKnowledgeBaseRequest(StrictModel):
    name: str | None = Field(default=None, min_length=1, max_length=128)
    description: str | None = Field(default=None, max_length=2000)
    role_type: RoleType | None = None

    @model_validator(mode="after")
    def require_at_least_one_field(self) -> "UpdateKnowledgeBaseRequest":
        if self.name is None and self.description is None and self.role_type is None:
            raise ValueError("at least one field must be provided")
        return self


class KnowledgeBase(BaseModel):
    id: str
    name: str
    description: str
    role_type: RoleType
    document_count: int = Field(ge=0)
    chunk_count: int = Field(ge=0)
    created_at: datetime
    updated_at: datetime


class KnowledgeBaseResponse(BaseModel):
    request_id: str
    knowledge_base: KnowledgeBase


class KnowledgeBasePage(BaseModel):
    request_id: str
    items: list[KnowledgeBase]
    next_cursor: str | None = None
    total: int = Field(ge=0)


class Document(BaseModel):
    id: str
    knowledge_base_id: str
    file_name: str
    status: DocumentStatus
    chunk_count: int = Field(ge=0)
    error_message: str = ""
    created_at: datetime
    updated_at: datetime


class DocumentResponse(BaseModel):
    request_id: str
    document: Document


class DocumentPage(BaseModel):
    request_id: str
    items: list[Document]
    next_cursor: str | None = None
    total: int = Field(ge=0)


class Source(BaseModel):
    document_id: str
    document_name: str
    chunk_id: str
    content: str
    score: float = Field(ge=0.0, le=1.0)
    page: int | None = Field(default=None, ge=1)


class SearchRequest(StrictModel):
    question: str = Field(min_length=1, max_length=16000)
    knowledge_base_id: str = Field(min_length=1, max_length=128)
    top_k: int = Field(default=3, ge=1, le=20)
    similarity_threshold: float = Field(default=0.7, ge=0.0, le=1.0)


class SearchResponse(BaseModel):
    request_id: str
    knowledge_found: bool
    sources: list[Source]


class RagQueryRequest(StrictModel):
    question: str = Field(min_length=1, max_length=16000)
    knowledge_base_id: str = Field(min_length=1, max_length=128)
    conversation_id: str | None = Field(default=None, min_length=1, max_length=128)
    role_type: RoleType | None = None


class RagQueryResponse(BaseModel):
    request_id: str
    conversation_id: str
    knowledge_found: bool
    answer: str
    sources: list[Source]
