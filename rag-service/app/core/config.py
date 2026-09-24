"""Environment-backed service configuration."""

import os
from functools import lru_cache
from pathlib import Path
from typing import Self

from pydantic import Field, model_validator
from pydantic_settings import BaseSettings, SettingsConfigDict

from app import __version__


class Settings(BaseSettings):
    """Runtime configuration loaded from RAG_* environment variables."""

    model_config = SettingsConfigDict(
        env_file=".env",
        env_prefix="RAG_",
        case_sensitive=False,
        extra="ignore",
    )

    service_name: str = "rag-service"
    service_version: str = __version__
    api_key: str = Field(default="change-me", min_length=1)
    host: str = "127.0.0.1"
    port: int = Field(default=8000, ge=1, le=65535)
    log_level: str = "INFO"
    log_format: str = Field(default="json", pattern="^(json|text)$")
    data_dir: Path = Path("./data")
    vector_db_path: Path = Path("./data/rag.db")
    upload_dir: Path = Path("./data/uploads")
    interview_store_path: Path = Path("./data/interviews.json")
    max_upload_bytes: int = Field(default=50 * 1024 * 1024, ge=1)
    chunk_size: int = Field(default=500, ge=50)
    chunk_overlap: int = Field(default=50, ge=0)
    embedding_batch_size: int = Field(default=32, ge=1, le=256)

    embedding_provider: str = "ollama"
    embedding_api_url: str = "http://127.0.0.1:11434/api/embed"
    embedding_api_key: str = ""
    embedding_model: str = "qwen3-embedding:0.6b"
    embedding_timeout_seconds: float = Field(default=30.0, gt=0)
    llm_api_url: str = ""
    llm_api_key: str = ""
    llm_provider: str = "openai"
    llm_model: str = ""
    llm_temperature: float = Field(default=0.3, ge=0.0, le=2.0)
    llm_max_tokens: int = Field(default=32000, ge=1)
    llm_timeout_seconds: float = Field(default=60.0, gt=0)
    speech_ws_url: str = "wss://openspeech.bytedance.com/api/v3/realtime/dialogue"
    speech_app_id: str = ""
    speech_access_key: str = ""
    speech_app_key: str = ""
    speech_resource_id: str = "volc.speech.dialog"
    speech_timeout_seconds: float = Field(default=45.0, gt=0)
    top_k: int = Field(default=3, ge=1, le=20)
    similarity_threshold: float = Field(default=0.7, ge=0.0, le=1.0)
    retrieval_mode: str = Field(default="hybrid", pattern="^(semantic|hybrid)$")
    retrieval_candidate_k: int = Field(default=20, ge=1, le=200)
    lexical_threshold: float = Field(default=0.3, ge=0.0, le=1.0)
    max_history_turns: int = Field(default=5, ge=0, le=100)
    verify_ssl: bool = True

    @model_validator(mode="after")
    def use_deepseek_api_key_fallback(self) -> Self:
        """Reuse the desktop client's key when no RAG-specific key is configured."""
        if not self.llm_api_key:
            self.llm_api_key = os.environ.get("DEEPSEEK_API_KEY", "")
        if not self.speech_app_id:
            self.speech_app_id = os.environ.get("DOUBAO_APP_ID", "")
        if not self.speech_access_key:
            self.speech_access_key = os.environ.get("DOUBAO_ACCESS_KEY", "")
        if not self.speech_app_key:
            self.speech_app_key = os.environ.get("DOUBAO_APP_KEY", "")
        return self

    @property
    def supported_extensions(self) -> frozenset[str]:
        return frozenset({".pdf", ".docx", ".txt", ".md", ".json"})


@lru_cache
def get_settings() -> Settings:
    """Return one immutable-by-convention settings instance per process."""

    return Settings()
