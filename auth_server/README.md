# License / Device-Binding Auth Server

Flask API for Cat Clicker: user registration, login, and HWID (+ optional IP) binding. ~1000 users, SQLite backend.

## Endpoints

| Method | Path | Description |
|--------|------|-------------|
| POST | `/api/auth/register` | Register: `{ "email", "password" }` → `{ "ok", "message" }` |
| POST | `/api/auth/login` | Login: `{ "email", "password" }` → `{ "ok", "token" }` |
| POST | `/api/auth/bind` | Bind device (first run): `{ "token", "hwid", "ip" }` → `{ "ok", "message" }` |
| POST | `/api/auth/validate` | Validate session (every run): `{ "token", "hwid", "ip" }` → `{ "ok", "message" }` or 403 |
| GET | `/api/health` | Health check |

- **Token**: From login; valid 90 days. Client stores it (e.g. registry) and sends with bind/validate.
- **HWID**: Client-generated hardware ID (volume serial + computer name + CPU).
- **IP**: Optional. Server uses `X-Forwarded-For` or `request.remote_addr` if not sent. Set `AUTH_IP_STRICT=1` to enforce IP match.

## Setup

```bash
cd auth_server
python -m venv venv
venv\Scripts\activate   # Windows
pip install -r requirements.txt
set AUTH_DB_PATH=.\auth.db
python -c "from db import init_db; init_db()"
python app.py
```

- Default: `http://0.0.0.0:5000`. Override with `PORT=8080`.
- DB path: `AUTH_DB_PATH` (default: `auth.db` in this folder).
- Strict IP: `AUTH_IP_STRICT=1` to require IP match on validate.

## Production

- Use HTTPS (reverse proxy: nginx, Caddy, or cloud load balancer).
- Run with gunicorn: `gunicorn -w 4 -b 0.0.0.0:5000 app:app`.
- Back up `auth.db` regularly.
- In the client, set the auth base URL to your deployed API (e.g. `https://auth.yourdomain.com`) and rebuild, or configure via obfuscated default in code.

## Client configuration

In the C++ client, the auth base URL is set at startup. Replace the default (e.g. `https://auth.example.com`) in `main.cpp` with your URL and rebuild. Use the same OBF macro so the URL is not stored in plaintext in the executable.
