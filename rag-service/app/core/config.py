"""Environment-backed service configuration."""

from functools import lru_cache
from pathlib import Path

from pydantic import Field
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
    top_k: int = Field(default=3, ge=1, le=20)
    similarity_threshold: float = Field(default=0.7, ge=0.0, le=1.0)
    max_history_turns: int = Field(default=5, ge=0, le=100)
    verify_ssl: bool = True

    @property
    def supported_extensions(self) -> frozenset[str]:
        return frozenset({".pdf", ".txt", ".md", ".json"})


@lru_cache
def get_settings() -> Settings:
    """Return one immutable-by-convention settings instance per process."""

    return Settings()
