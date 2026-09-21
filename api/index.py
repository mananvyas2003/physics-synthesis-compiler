"""
Vercel Serverless Function entrypoint for physics-synthesis-compiler.
Routes /api/chat, /api/runs/*, and /api/health.
"""

from __future__ import annotations

import os
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
if str(ROOT) not in sys.path:
    sys.path.insert(0, str(ROOT))

os.environ["VERCEL"] = "1"

from web.server import Handler as BaseHandler, load_dotenv, load_gemini_key_local, ensure_user_data

load_dotenv(ROOT / ".env")
load_gemini_key_local(ROOT / "GEMINI_API_KEY.local")
try:
    ensure_user_data()
except Exception:
    pass


class handler(BaseHandler):
    """Vercel Python Serverless Function handler."""
    pass
