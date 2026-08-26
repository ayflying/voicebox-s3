"""
Faster-Whisper 离线/私有 ASR 服务（纯 CPU）。

- 接收 ESP32 网关转发来的 16k/16bit/单声道「裸 PCM」字节流（application/octet-stream）
- 包成 float32 音频交给 faster-whisper 转写（中文）
- 返回 {"text": "..."}

模型首次启动会从 HuggingFace 下载并缓存（需要服务器能联网一次）。
可通过环境变量 WHISPER_MODEL 切换：tiny / base / small（base 在普通 CPU 上短句接近实时）。
"""

import os

import numpy as np
from fastapi import FastAPI, HTTPException, Request
from fastapi.responses import JSONResponse
from faster_whisper import WhisperModel

MODEL_SIZE = os.getenv("WHISPER_MODEL", "base")
# cpu + int8 量化，普通 x86 即可实时/近实时
print(f"[asr] loading faster-whisper model={MODEL_SIZE} (device=cpu, compute_type=int8) ...")
MODEL = WhisperModel(MODEL_SIZE, device="cpu", compute_type="int8")
print("[asr] model loaded.")

APP = FastAPI()


@APP.get("/health")
async def health():
    return {"status": "ok"}


@APP.post("/transcribe")
async def transcribe(request: Request):
    data = await request.body()
    if not data:
        raise HTTPException(status_code=400, detail="empty body")

    # 裸 PCM -> float32 [-1,1]，采样率固定 16k
    audio = np.frombuffer(data, dtype=np.int16).astype(np.float32) / 32768.0

    if audio.shape[0] < 800:  # 太短（<50ms）直接返回空
        return JSONResponse({"text": ""})

    segments, _ = MODEL.transcribe(
        audio,
        language="zh",
        beam_size=5,
        vad_filter=True,
    )
    text = "".join(seg.text for seg in segments).strip()
    return JSONResponse({"text": text})
