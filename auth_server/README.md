# Auth Server

HWID + IP verification backend for Cat Clicker. Host on your cloud or run locally.

## Setup

```bash
pip install -r requirements.txt
python app.py
```

## API

- **GET /auth?hwid=...&ip=...** - Verify access. Returns `OK` if authorized.
- **POST /admin/add** - Add license: `{"hwid": "...", "ip": "optional"}`
- **POST /admin/remove** - Remove: `{"hwid": "..."}`
- **GET /admin/list** - List all licenses

## Add a user

1. Run the client once to get its HWID (or add a test endpoint).
2. `curl -X POST http://localhost:5000/admin/add -H "Content-Type: application/json" -d '{"hwid":"YOUR_HWID","ip":"1.2.3.4"}'`
3. Use `"ip":"0.0.0.0"` to allow any IP for that HWID.

## Deploy

- **Heroku**: Add Procfile `web: python app.py`
- **Railway**: Set start command `python app.py`
- **Docker**: Use `FROM python:3.11-slim` + `pip install -r requirements.txt`

Set `AUTH_SERVER_URL` in your build: `https://yourapp.herokuapp.com/auth`
