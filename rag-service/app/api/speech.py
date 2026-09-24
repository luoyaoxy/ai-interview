"""Speech-to-text HTTP endpoint."""

from typing import Annotated

from fastapi import APIRouter, Depends, Request
from pydantic import BaseModel

from app.api.dependencies import Authorized
from app.core.errors import ServiceError
from app.services import ServiceContainer, get_services

router = APIRouter(prefix="/speech", tags=["Speech"])


class SpeechTranscriptionResponse(BaseModel):
    text: str


@router.post(
    "/transcribe",
    operation_id="transcribeSpeech",
    response_model=SpeechTranscriptionResponse,
)
async def transcribe_speech(
    request: Request,
    _: Authorized,
    services: Annotated[ServiceContainer, Depends(get_services)],
) -> SpeechTranscriptionResponse:
    audio = await request.body()
    if not audio:
        raise ServiceError(status_code=422, code="EMPTY_AUDIO", message="Audio payload is empty")
    if len(audio) > 16 * 1024 * 1024:
        raise ServiceError(
            status_code=413,
            code="AUDIO_TOO_LARGE",
            message="Audio payload exceeds 16 MiB",
        )
    text = await services.speech.transcribe(audio)
    return SpeechTranscriptionResponse(text=text)
