"""In-process RAG conversation history owned by the service."""

import asyncio
from collections import defaultdict

from app.llm.ports import ChatMessage


class ConversationStore:
    def __init__(self, max_turns: int) -> None:
        self._max_turns = max_turns
        self._history: dict[str, list[tuple[str, str]]] = defaultdict(list)
        self._locks: dict[str, asyncio.Lock] = {}

    def lock(self, conversation_id: str) -> asyncio.Lock:
        return self._locks.setdefault(conversation_id, asyncio.Lock())

    def messages(self, conversation_id: str) -> list[ChatMessage]:
        messages: list[ChatMessage] = []
        for user, assistant in self._history.get(conversation_id, []):
            messages.append(ChatMessage(role="user", content=user))
            messages.append(ChatMessage(role="assistant", content=assistant))
        return messages

    def append(self, conversation_id: str, question: str, answer: str) -> None:
        if self._max_turns == 0:
            return
        history = self._history[conversation_id]
        history.append((question, answer))
        del history[: max(0, len(history) - self._max_turns)]

    async def clear(self, conversation_id: str) -> None:
        async with self.lock(conversation_id):
            self._history.pop(conversation_id, None)
