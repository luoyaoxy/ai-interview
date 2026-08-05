"""Top-level API router composition."""

from fastapi import APIRouter

from app.api import documents, knowledge_bases, rag

api_router = APIRouter(prefix="/api/v1")
api_router.include_router(knowledge_bases.router)
api_router.include_router(documents.router)
api_router.include_router(rag.router)
