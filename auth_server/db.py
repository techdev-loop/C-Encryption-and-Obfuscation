# auth_server/db.py - SQLite schema and helpers for license/device binding
import sqlite3
import os
import hashlib
import secrets
from contextlib import contextmanager
from datetime import datetime, timedelta

DB_PATH = os.environ.get("AUTH_DB_PATH", os.path.join(os.path.dirname(__file__), "auth.db"))
TOKEN_BYTES = 32
TOKEN_TTL_DAYS = 90
IP_CHECK_STRICT = os.environ.get("AUTH_IP_STRICT", "0").lower() in ("1", "true", "yes")


def _hash_password(password: str) -> str:
    return hashlib.sha256(password.encode("utf-8")).hexdigest()


def init_db():
    conn = sqlite3.connect(DB_PATH)
    try:
        conn.executescript("""
            CREATE TABLE IF NOT EXISTS users (
                id INTEGER PRIMARY KEY AUTOINCREMENT,
                email TEXT UNIQUE NOT NULL,
                password_hash TEXT NOT NULL,
                created_at TEXT NOT NULL DEFAULT (datetime('now'))
            );
            CREATE TABLE IF NOT EXISTS tokens (
                id INTEGER PRIMARY KEY AUTOINCREMENT,
                user_id INTEGER NOT NULL REFERENCES users(id),
                token TEXT NOT NULL UNIQUE,
                created_at TEXT NOT NULL DEFAULT (datetime('now')),
                expires_at TEXT NOT NULL
            );
            CREATE TABLE IF NOT EXISTS devices (
                id INTEGER PRIMARY KEY AUTOINCREMENT,
                user_id INTEGER NOT NULL REFERENCES users(id),
                hwid TEXT NOT NULL,
                ip TEXT,
                first_seen TEXT NOT NULL DEFAULT (datetime('now')),
                last_seen TEXT NOT NULL DEFAULT (datetime('now')),
                UNIQUE(user_id)
            );
            CREATE INDEX IF NOT EXISTS idx_tokens_token ON tokens(token);
            CREATE INDEX IF NOT EXISTS idx_tokens_user_id ON tokens(user_id);
            CREATE INDEX IF NOT EXISTS idx_devices_user_id ON devices(user_id);
        """)
        conn.commit()
    finally:
        conn.close()


@contextmanager
def get_conn():
    conn = sqlite3.connect(DB_PATH)
    conn.row_factory = sqlite3.Row
    try:
        yield conn
        conn.commit()
    except Exception:
        conn.rollback()
        raise
    finally:
        conn.close()


def user_register(email: str, password: str) -> tuple[bool, str | None]:
    """Register a new user. Returns (ok, error_message)."""
    with get_conn() as c:
        cur = c.execute("SELECT id FROM users WHERE email = ?", (email.strip().lower(),))
        if cur.fetchone():
            return False, "Email already registered"
        pw_hash = _hash_password(password)
        c.execute("INSERT INTO users (email, password_hash) VALUES (?, ?)", (email.strip().lower(), pw_hash))
    return True, None


def user_login(email: str, password: str) -> tuple[bool, str | None, str | None]:
    """Login. Returns (ok, error_message, token). Token expires in TOKEN_TTL_DAYS."""
    with get_conn() as c:
        cur = c.execute("SELECT id, password_hash FROM users WHERE email = ?", (email.strip().lower(),))
        row = cur.fetchone()
        if not row:
            return False, "Invalid email or password", None
        uid, pw_hash = row["id"], row["password_hash"]
        if _hash_password(password) != pw_hash:
            return False, "Invalid email or password", None
        token = secrets.token_urlsafe(TOKEN_BYTES)
        expires = (datetime.utcnow() + timedelta(days=TOKEN_TTL_DAYS)).isoformat() + "Z"
        c.execute("INSERT INTO tokens (user_id, token, expires_at) VALUES (?, ?, ?)", (uid, token, expires))
    return True, None, token


def resolve_token(token: str) -> int | None:
    """Return user_id if token is valid and not expired, else None."""
    with get_conn() as c:
        cur = c.execute(
            "SELECT user_id FROM tokens WHERE token = ? AND datetime(expires_at) > datetime('now')",
            (token.strip(),),
        )
        row = cur.fetchone()
        return row["user_id"] if row else None


def device_bind(user_id: int, hwid: str, ip: str | None) -> tuple[bool, str]:
    """
    Bind device (HWID, IP) to user. First time: bind. Later: must match.
    Returns (success, message).
    """
    hwid = (hwid or "").strip()
    if not hwid:
        return False, "HWID required"
    with get_conn() as c:
        cur = c.execute("SELECT hwid, ip FROM devices WHERE user_id = ?", (user_id,))
        row = cur.fetchone()
        now = datetime.utcnow().isoformat() + "Z"
        if not row:
            c.execute(
                "INSERT INTO devices (user_id, hwid, ip, first_seen, last_seen) VALUES (?, ?, ?, ?, ?)",
                (user_id, hwid, ip or "", now, now),
            )
            return True, "Device bound"
        if row["hwid"] != hwid:
            return False, "This license is bound to another device. Contact support."
        c.execute("UPDATE devices SET ip = ?, last_seen = ? WHERE user_id = ?", (ip or "", now, user_id))
        return True, "OK"


def device_validate(user_id: int, hwid: str, ip: str | None) -> tuple[bool, str]:
    """
    Validate that this HWID (and optionally IP) is bound to this user.
    Returns (success, message).
    """
    hwid = (hwid or "").strip()
    if not hwid:
        return False, "HWID required"
    with get_conn() as c:
        cur = c.execute("SELECT hwid, ip FROM devices WHERE user_id = ?", (user_id,))
        row = cur.fetchone()
        if not row:
            return False, "No device bound. Run once with internet to bind."
        if row["hwid"] != hwid:
            return False, "This license is bound to another device."
        if IP_CHECK_STRICT and ip and row["ip"] and row["ip"] != ip:
            return False, "IP change detected. Use from your registered network or contact support."
        c.execute("UPDATE devices SET last_seen = ? WHERE user_id = ?", (datetime.utcnow().isoformat() + "Z", user_id))
        return True, "OK"
