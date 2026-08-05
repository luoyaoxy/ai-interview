"""Ports implemented by an OpenAI-compatible or other LLM adapter."""

from dataclasses import dataclass
from typing import Protocol


@dataclass(frozen=True, slots=True)
class ChatMessage:
    role: str
    content: str


class LlmProvider(Protocol):
    """Generate one assistant response from a complete message list."""

    async def generate(self, messages: list[ChatMessage]) -> str: ...
