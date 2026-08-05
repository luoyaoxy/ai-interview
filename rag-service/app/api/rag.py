"""RAG search, query, and conversation routes."""

from typing import Annotated

from fastapi import APIRouter, Depends, Request, Response, status

from app.api.dependencies import Authorized
from app.api.mappers import to_source
from app.api.schemas import RagQueryRequest, RagQueryResponse, SearchRequest, SearchResponse
from app.services import ServiceContainer, get_services

router = APIRouter(prefix="/rag", tags=["RAG"])
Services = Annotated[ServiceContainer, Depends(get_services)]


@router.post("/search", operation_id="searchKnowledgeBase", response_model=SearchResponse)
async def search_knowledge_base(
    request: SearchRequest,
    _authorization: Authorized,
    services: Services,
    http_request: Request,
) -> SearchResponse:
    sources = await services.retriever.search(
        request.question,
        request.knowledge_base_id,
        top_k=request.top_k,
        similarity_threshold=request.similarity_threshold,
    )
    return SearchResponse(
        request_id=http_request.state.request_id,
        knowledge_found=bool(sources),
        sources=[to_source(source) for source in sources],
    )


@router.post("/query", operation_id="queryRag", response_model=RagQueryResponse)
async def query_rag(
    request: RagQueryRequest,
    _authorization: Authorized,
    services: Services,
    http_request: Request,
) -> RagQueryResponse:
    result = await services.rag.query(
        question=request.question,
        knowledge_base_id=request.knowledge_base_id,
        conversation_id=request.conversation_id,
        role_type=request.role_type.value if request.role_type else None,
    )
    return RagQueryResponse(
        request_id=http_request.state.request_id,
        conversation_id=result.conversation_id,
        knowledge_found=result.knowledge_found,
        answer=result.answer,
        sources=[to_source(source) for source in result.sources],
    )


@router.delete(
    "/conversations/{conversation_id}",
    operation_id="clearRagConversation",
    status_code=status.HTTP_204_NO_CONTENT,
)
async def clear_rag_conversation(
    conversation_id: str,
    _authorization: Authorized,
    services: Services,
) -> Response:
    await services.rag.clear_conversation(conversation_id)
    return Response(status_code=status.HTTP_204_NO_CONTENT)
