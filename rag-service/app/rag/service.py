"""Complete RAG query pipeline."""

import asyncio
import re
from dataclasses import dataclass
from uuid import uuid4

from app.llm.ports import ChatMessage, LlmProvider
from app.rag.conversations import ConversationStore
from app.rag.prompt_builder import RagPromptBuilder
from app.retrieval.ports import RetrievedSource, Retriever
from app.vector_store.sqlite_store import SqliteVectorStore


@dataclass(frozen=True, slots=True)
class RagAnswer:
    conversation_id: str
    knowledge_found: bool
    answer: str
    sources: list[RetrievedSource]


class RagService:
    def __init__(
        self,
        *,
        store: SqliteVectorStore,
        retriever: Retriever,
        llm: LlmProvider,
        prompt_builder: RagPromptBuilder,
        conversations: ConversationStore,
        top_k: int,
        similarity_threshold: float,
    ) -> None:
        self._store = store
        self._retriever = retriever
        self._llm = llm
        self._prompt_builder = prompt_builder
        self._conversations = conversations
        self._top_k = top_k
        self._similarity_threshold = similarity_threshold

    async def query(
        self,
        *,
        question: str,
        knowledge_base_id: str,
        conversation_id: str | None,
        role_type: str | None,
    ) -> RagAnswer:
        knowledge_base = await asyncio.to_thread(self._store.get_knowledge_base, knowledge_base_id)
        selected_role = role_type or knowledge_base.role_type
        if selected_role in {"default", "custom"}:
            selected_role = "general_assistant"
        selected_conversation = conversation_id or f"conversation-{uuid4()}"

        async with self._conversations.lock(selected_conversation):
            sources = await self._retriever.search(
                question,
                knowledge_base_id,
                top_k=self._top_k,
                similarity_threshold=self._similarity_threshold,
            )
            if not sources:
                return RagAnswer(
                    conversation_id=selected_conversation,
                    knowledge_found=False,
                    answer=self._prompt_builder.get_role(selected_role).fallback,
                    sources=[],
                )

            messages = [
                ChatMessage(
                    role="system",
                    content=self._prompt_builder.build_system_prompt(selected_role, sources),
                ),
                *self._conversations.messages(selected_conversation),
                ChatMessage(role="user", content=question),
            ]
            answer = await self._llm.generate(messages)
            citations = [int(value) for value in re.findall(r"\[(\d+)\]", answer)]
            if (
                "INSUFFICIENT_EVIDENCE" in answer
                or not citations
                or any(index < 1 or index > len(sources) for index in citations)
            ):
                return RagAnswer(
                    conversation_id=selected_conversation,
                    knowledge_found=False,
                    answer=self._prompt_builder.get_role(selected_role).fallback,
                    sources=[],
                )
            self._conversations.append(selected_conversation, question, answer)
            return RagAnswer(
                conversation_id=selected_conversation,
                knowledge_found=True,
                answer=answer,
                sources=sources,
            )

    async def clear_conversation(self, conversation_id: str) -> None:
        await self._conversations.clear(conversation_id)
