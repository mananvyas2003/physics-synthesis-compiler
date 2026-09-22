#!/usr/bin/env python3
"""Seed user_data/catalogue.db from DEMO parts in fixtures/seed + fixtures/schematics.

Run from repo root:
  python scripts/seed_catalogue.py

Same logic as web.server.ensure_user_data (catalogue-owned generate needs these MPNs).
"""
from __future__ import annotations

import json
import sqlite3
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
USER_DATA = ROOT / "user_data"
CATALOGUE_DB = USER_DATA / "catalogue.db"
DEFAULT_DFM = ROOT / "fixtures" / "dfm" / "standard.json"
DFM_PROFILE = USER_DATA / "dfm_profile.json"

TYPE_MAP = {
    "resistor": 0,
    "capacitor": 1,
    "inductor": 2,
    "diode": 3,
    "led": 3,
    "transistor": 4,
    "bjt": 4,
    "mosfet": 4,
    "opamp": 5,
    "regulator": 5,
    "ldo": 5,
    "switch": 6,
    "connector": 6,
    "battery": 7,
}


def seed() -> int:
    USER_DATA.mkdir(parents=True, exist_ok=True)
    if not DFM_PROFILE.is_file() and DEFAULT_DFM.is_file():
        DFM_PROFILE.write_text(
            DEFAULT_DFM.read_text(encoding="utf-8"), encoding="utf-8"
        )

    con = sqlite3.connect(str(CATALOGUE_DB))
    con.execute(
        "CREATE TABLE IF NOT EXISTS Parts ("
        "id INTEGER PRIMARY KEY AUTOINCREMENT, mpn TEXT UNIQUE NOT NULL, "
        "type INTEGER NOT NULL, value REAL NOT NULL, package TEXT NOT NULL, "
        "v_rating REAL, i_rating REAL, esr_ohms REAL, power_rating_w REAL, "
        "tolerance_class INTEGER)"
    )
    seen: set[str] = set()
    inserted = 0
    for pattern in ("fixtures/seed/*.json", "fixtures/schematics/*.json"):
        for path in ROOT.glob(pattern):
            try:
                data = json.loads(path.read_text(encoding="utf-8"))
            except (OSError, json.JSONDecodeError):
                continue
            for part in data.get("parts") or []:
                mpn = part.get("mpn")
                if not mpn or mpn in seen:
                    continue
                seen.add(mpn)
                cur = con.execute(
                    "INSERT OR IGNORE INTO Parts "
                    "(mpn, type, value, package, v_rating, i_rating, "
                    "esr_ohms, power_rating_w, tolerance_class) "
                    "VALUES (?,?,?,?,?,?,?,?,?)",
                    (
                        mpn,
                        TYPE_MAP.get(str(part.get("type", "")).lower(), 7),
                        float(part.get("value") or 0),
                        str(part.get("package") or "0603"),
                        float(part.get("v_rating") or 0),
                        float(part.get("i_rating") or 0),
                        float(part.get("esr_ohms") or 0),
                        float(part.get("power_rating_w") or 0),
                        2,
                    ),
                )
                inserted += cur.rowcount
    con.commit()
    total = con.execute("SELECT COUNT(*) FROM Parts").fetchone()[0]
    con.close()
    print(f"catalogue={CATALOGUE_DB}")
    print(f"unique_fixture_mpns={len(seen)} newly_inserted={inserted} total_parts={total}")
    return 0 if total > 0 else 1


if __name__ == "__main__":
    sys.exit(seed())
