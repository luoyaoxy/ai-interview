"""OpenAI-compatible HTTP LLM adapter used only by RAG answers."""

from typing import Any

import httpx

from app.core.errors import ServiceError
from app.llm.ports import ChatMessage


class HttpLlmProvider:
    def __init__(
        self,
        *,
        api_url: str,
        api_key: str,
        provider: str,
        model: str,
        temperature: float,
        max_tokens: int,
        timeout_seconds: float,
        verify_ssl: bool,
        transport: httpx.AsyncBaseTransport | None = None,
    ) -> None:
        self._api_url = api_url
        self._api_key = api_key
        self._provider = provider.lower()
        self._model = model
        self._temperature = temperature
        self._max_tokens = max_tokens
        self._timeout_seconds = timeout_seconds
        self._verify_ssl = verify_ssl
        self._transport = transport

    async def generate(self, messages: list[ChatMessage]) -> str:
        if not self._api_url or not self._api_key or not self._model:
            raise ServiceError(
                status_code=503,
                code="LLM_NOT_CONFIGURED",
                message="RAG LLM API URL, API key, and model must be configured",
            )

        payload: dict[str, Any] = {
            "model": self._model,
            "temperature": self._temperature,
            "max_tokens": self._max_tokens,
            "messages": [
                {"role": message.role, "content": message.content} for message in messages
            ],
        }
        if self._provider == "token_pony":
            payload["model_config"] = {
                "name": self._model,
                "temperature": self._temperature,
                "max_tokens": self._max_tokens,
            }
        headers = {
            "Content-Type": "application/json; charset=utf-8",
            "Authorization": f"Bearer {self._api_key}",
        }
        try:
            async with httpx.AsyncClient(
                timeout=self._timeout_seconds,
                verify=self._verify_ssl,
                transport=self._transport,
            ) as client:
                response = await client.post(self._api_url, headers=headers, json=payload)
        except httpx.TimeoutException as exc:
            raise ServiceError(
                status_code=504,
                code="LLM_TIMEOUT",
                message="RAG LLM request timed out",
            ) from exc
        except httpx.HTTPError as exc:
            raise ServiceError(
                status_code=503,
                code="LLM_UNAVAILABLE",
                message=f"RAG LLM service is unavailable: {exc}",
            ) from exc

        if response.status_code >= 400:
            raise ServiceError(
                status_code=502,
                code="LLM_UPSTREAM_ERROR",
                message=f"RAG LLM returned HTTP {response.status_code}",
                details={"response": response.text[:2000]},
            )
        try:
            body = response.json()
            content = body["choices"][0]["message"]["content"]
        except (ValueError, KeyError, IndexError, TypeError) as exc:
            raise ServiceError(
                status_code=502,
                code="INVALID_LLM_RESPONSE",
                message="RAG LLM response has no choices[0].message.content",
            ) from exc
        if not isinstance(content, str) or not content.strip():
            raise ServiceError(
                status_code=502,
                code="EMPTY_LLM_RESPONSE",
                message="RAG LLM returned an empty answer",
            )
        return content
