from app.core.config import Settings


def test_llm_api_key_falls_back_to_deepseek_key(monkeypatch) -> None:
    monkeypatch.delenv("RAG_LLM_API_KEY", raising=False)
    monkeypatch.setenv("DEEPSEEK_API_KEY", "shared-deepseek-key")

    settings = Settings(_env_file=None)

    assert settings.llm_api_key == "shared-deepseek-key"


def test_rag_llm_api_key_takes_precedence(monkeypatch) -> None:
    monkeypatch.setenv("RAG_LLM_API_KEY", "rag-specific-key")
    monkeypatch.setenv("DEEPSEEK_API_KEY", "shared-deepseek-key")

    settings = Settings(_env_file=None)

    assert settings.llm_api_key == "rag-specific-key"
