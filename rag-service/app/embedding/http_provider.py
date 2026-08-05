"""HTTP adapter for Ollama and OpenAI-compatible Embedding APIs."""

import asyncio
from typing import Any

import httpx

from app.core.errors import ServiceError


class HttpEmbeddingProvider:
    def __init__(
        self,
        *,
        api_url: str,
        api_key: str,
        model: str,
        provider: str,
        timeout_seconds: float,
        verify_ssl: bool,
        max_attempts: int = 3,
        transport: httpx.AsyncBaseTransport | None = None,
    ) -> None:
        self._api_url = api_url
        self._api_key = api_key
        self._model = model
        self._provider = provider.lower()
        self._timeout_seconds = timeout_seconds
        self._verify_ssl = verify_ssl
        self._max_attempts = max_attempts
        self._transport = transport
        self._dimension = 0

    async def embed(self, texts: list[str]) -> list[list[float]]:
        if not texts:
            return []
        if not self._api_url or not self._model:
            raise ServiceError(
                status_code=503,
                code="EMBEDDING_NOT_CONFIGURED",
                message="Embedding API URL and model must be configured",
            )

        headers = {"Content-Type": "application/json; charset=utf-8"}
        if self._api_key:
            headers["Authorization"] = f"Bearer {self._api_key}"
        payload: dict[str, Any] = {"model": self._model, "input": texts}

        response: httpx.Response | None = None
        async with httpx.AsyncClient(
            timeout=self._timeout_seconds,
            verify=self._verify_ssl,
            transport=self._transport,
        ) as client:
            for attempt in range(self._max_attempts):
                try:
                    response = await client.post(self._api_url, headers=headers, json=payload)
                except httpx.TimeoutException as exc:
                    if attempt + 1 == self._max_attempts:
                        raise ServiceError(
                            status_code=504,
                            code="EMBEDDING_TIMEOUT",
                            message="Embedding service timed out",
                        ) from exc
                    await asyncio.sleep(2**attempt)
                    continue
                except httpx.HTTPError as exc:
                    if attempt + 1 == self._max_attempts:
                        raise ServiceError(
                            status_code=503,
                            code="EMBEDDING_UNAVAILABLE",
                            message=f"Embedding service is unavailable: {exc}",
                        ) from exc
                    await asyncio.sleep(2**attempt)
                    continue

                if response.status_code < 400:
                    break
                if response.status_code < 500 or attempt + 1 == self._max_attempts:
                    raise ServiceError(
                        status_code=502,
                        code="EMBEDDING_UPSTREAM_ERROR",
                        message=f"Embedding service returned HTTP {response.status_code}",
                        details={"response": response.text[:2000]},
                    )
                await asyncio.sleep(2**attempt)

        if response is None:
            raise ServiceError(
                status_code=503,
                code="EMBEDDING_UNAVAILABLE",
                message="Embedding service did not return a response",
            )
        try:
            body = response.json()
        except ValueError as exc:
            raise ServiceError(
                status_code=502,
                code="INVALID_EMBEDDING_RESPONSE",
                message="Embedding service returned invalid JSON",
            ) from exc

        vectors = self._extract_vectors(body)
        if len(vectors) != len(texts):
            raise ServiceError(
                status_code=502,
                code="EMBEDDING_COUNT_MISMATCH",
                message=f"Expected {len(texts)} vectors but received {len(vectors)}",
            )
        dimensions = {len(vector) for vector in vectors if vector}
        if len(dimensions) != 1 or not dimensions:
            raise ServiceError(
                status_code=502,
                code="INVALID_EMBEDDING_DIMENSION",
                message="Embedding vectors are empty or have inconsistent dimensions",
            )
        self._dimension = dimensions.pop()
        return vectors

    async def dimension(self) -> int:
        return self._dimension

    @staticmethod
    def _extract_vectors(body: dict[str, Any]) -> list[list[float]]:
        if isinstance(body.get("embeddings"), list):
            return [[float(value) for value in vector] for vector in body["embeddings"]]
        if isinstance(body.get("data"), list):
            ordered = sorted(body["data"], key=lambda item: int(item.get("index", 0)))
            return [[float(value) for value in item.get("embedding", [])] for item in ordered]
        error = body.get("error")
        message = error.get("message") if isinstance(error, dict) else None
        raise ServiceError(
            status_code=502,
            code="INVALID_EMBEDDING_RESPONSE",
            message=message or "Embedding response has no embeddings or data array",
        )
