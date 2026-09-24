"""Doubao realtime-dialogue speech transcription adapter."""

import asyncio
import gzip
import json
import logging
import struct
import uuid
from typing import Any

import websockets

from app.core.errors import ServiceError

logger = logging.getLogger(__name__)

START_CONNECTION = 1
START_SESSION = 100
TASK_REQUEST = 200
ASR_RESULT = 451


def _full_request(event: int, session_id: str, payload: dict[str, Any]) -> bytes:
    body = json.dumps(payload, ensure_ascii=False, separators=(",", ":")).encode()
    message = bytearray((0x11, 0x14, 0x10, 0x00))
    message.extend(struct.pack(">I", event))
    if event not in {1, 2, 50, 51, 52}:
        session = session_id.encode()
        message.extend(struct.pack(">I", len(session)))
        message.extend(session)
    message.extend(struct.pack(">I", len(body)))
    message.extend(body)
    return bytes(message)


def _audio_request(session_id: str, audio: bytes) -> bytes:
    session = session_id.encode()
    return b"".join(
        (
            bytes((0x11, 0x24, 0x00, 0x00)),
            struct.pack(">I", TASK_REQUEST),
            struct.pack(">I", len(session)),
            session,
            struct.pack(">I", len(audio)),
            audio,
        ),
    )


def _parse_response(data: bytes) -> tuple[int, dict[str, Any]]:
    if len(data) < 8:
        raise ValueError("Speech response is too short")
    header_size = (data[0] & 0x0F) * 4
    message_type = data[1] >> 4
    flags = data[1] & 0x0F
    compression = data[2] & 0x0F
    offset = header_size
    if message_type == 0x0F:
        code = struct.unpack_from(">I", data, offset)[0]
        size = struct.unpack_from(">I", data, offset + 4)[0]
        detail = data[offset + 8 : offset + 8 + size].decode(errors="replace")
        raise ValueError(f"Speech provider error {code}: {detail}")
    if flags & 0x03:
        offset += 4
    event = 0
    if flags & 0x04:
        event = struct.unpack_from(">I", data, offset)[0]
        offset += 4
        if event not in {1, 2, 50, 51, 52}:
            session_size = struct.unpack_from(">I", data, offset)[0]
            offset += 4 + session_size
        if event in {50, 51, 52}:
            connect_size = struct.unpack_from(">I", data, offset)[0]
            offset += 4 + connect_size
    size = struct.unpack_from(">I", data, offset)[0]
    payload = data[offset + 4 : offset + 4 + size]
    if compression == 1:
        payload = gzip.decompress(payload)
    if not payload:
        return event, {}
    try:
        return event, json.loads(payload)
    except (UnicodeDecodeError, json.JSONDecodeError):
        return event, {}


class DoubaoSpeechTranscriber:
    def __init__(
        self,
        *,
        ws_url: str,
        app_id: str,
        access_key: str,
        app_key: str,
        resource_id: str,
        timeout_seconds: float,
    ) -> None:
        self._ws_url = ws_url
        self._app_id = app_id
        self._access_key = access_key
        self._app_key = app_key
        self._resource_id = resource_id
        self._timeout_seconds = timeout_seconds

    async def transcribe(self, pcm: bytes) -> str:
        if not self._app_id or not self._access_key or not self._app_key:
            raise ServiceError(
                status_code=503,
                code="SPEECH_NOT_CONFIGURED",
                message="Speech service is not configured",
            )
        if not pcm:
            raise ServiceError(
                status_code=422,
                code="EMPTY_AUDIO",
                message="Audio payload is empty",
            )

        session_id = str(uuid.uuid4())
        headers = {
            "X-Api-App-ID": self._app_id,
            "X-Api-Access-Key": self._access_key,
            "X-Api-App-Key": self._app_key,
            "X-Api-Resource-Id": self._resource_id,
            "X-Api-Connect-Id": str(uuid.uuid4()),
        }
        session_payload = {
            "asr": {"extra": {
                "end_smooth_window_ms": 500,
                "vad_silence_duration": 800,
                "vad_speech_trigger_duration": 400,
            }},
            "tts": {
                "speaker": "zh_male_yunzhou_jupiter_bigtts",
                "audio_config": {"channel": 1, "format": "pcm", "sample_rate": 24000},
            },
            "dialog": {
                "bot_name": "AI面试官",
                "system_role": "只进行语音转写，不要主动提问。",
                "speaking_style": "简洁",
                "location": {"city": "北京"},
                "extra": {
                    "strict_audit": False,
                    "audit_response": "",
                    "recv_timeout": 10,
                    "input_mod": "audio",
                },
            },
        }
        try:
            async with asyncio.timeout(self._timeout_seconds):
                async with websockets.connect(
                    self._ws_url,
                    additional_headers=headers,
                    ping_interval=None,
                    max_size=8 * 1024 * 1024,
                ) as websocket:
                    await websocket.send(_full_request(START_CONNECTION, "", {}))
                    first = await websocket.recv()
                    if isinstance(first, bytes):
                        _parse_response(first)
                    await websocket.send(_full_request(START_SESSION, session_id, session_payload))
                    for offset in range(0, len(pcm), 3200):
                        chunk = pcm[offset : offset + 3200]
                        await websocket.send(_audio_request(session_id, chunk))
                        await asyncio.sleep(0.01)
                    silence = bytes(3200)
                    latest = ""
                    while True:
                        await websocket.send(_audio_request(session_id, silence))
                        try:
                            response = await asyncio.wait_for(websocket.recv(), timeout=0.1)
                        except TimeoutError:
                            continue
                        if not isinstance(response, bytes):
                            continue
                        event, payload = _parse_response(response)
                        if event != ASR_RESULT:
                            continue
                        results = payload.get("results") or []
                        if not results:
                            continue
                        result = results[0]
                        text = str(result.get("text", "")).strip()
                        if text:
                            latest = text
                        if text and not result.get("is_interim", True):
                            return text
                    return latest
        except TimeoutError as exc:
            raise ServiceError(
                status_code=504,
                code="SPEECH_TIMEOUT",
                message="Speech transcription timed out",
            ) from exc
        except ServiceError:
            raise
        except Exception as exc:
            logger.exception("speech_transcription_failed")
            raise ServiceError(
                status_code=502,
                code="SPEECH_UPSTREAM_ERROR",
                message=f"Speech transcription failed: {type(exc).__name__}",
            ) from exc
