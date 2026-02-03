# auth_server/app.py - License/device-binding API for Cat Clicker (~1000 users)
# Run: set FLASK_APP=app.py && flask run --host=0.0.0.0 --port=5000
# Or: python app.py (if __main__ block used)

import os
from flask import Flask, request, jsonify
from flask_cors import CORS

from db import (
    init_db,
    user_register,
    user_login,
    resolve_token,
    device_bind,
    device_validate,
)

app = Flask(__name__)
CORS(app)

# Optional: restrict CORS in production to your domain
# CORS(app, origins=["https://yourdomain.com"])


def client_ip():
    return request.headers.get("X-Forwarded-For", request.remote_addr or "").split(",")[0].strip()


@app.route("/api/auth/register", methods=["POST"])
def register():
    data = request.get_json() or {}
    email = data.get("email", "").strip()
    password = data.get("password", "")
    if not email or not password:
        return jsonify({"ok": False, "error": "Email and password required"}), 400
    ok, err = user_register(email, password)
    if not ok:
        return jsonify({"ok": False, "error": err}), 400
    return jsonify({"ok": True, "message": "Registered. Please log in."})


@app.route("/api/auth/login", methods=["POST"])
def login():
    data = request.get_json() or {}
    email = data.get("email", "").strip()
    password = data.get("password", "")
    if not email or not password:
        return jsonify({"ok": False, "error": "Email and password required"}), 400
    ok, err, token = user_login(email, password)
    if not ok:
        return jsonify({"ok": False, "error": err}), 401
    return jsonify({"ok": True, "token": token})


@app.route("/api/auth/bind", methods=["POST"])
def bind():
    """First run: bind this HWID (and IP) to the user. Requires valid token."""
    data = request.get_json() or {}
    token = (data.get("token") or request.headers.get("Authorization") or "").strip()
    if token.startswith("Bearer "):
        token = token[7:]
    hwid = (data.get("hwid") or "").strip()
    ip = data.get("ip") or client_ip()
    if not token:
        return jsonify({"ok": False, "error": "Token required"}), 401
    user_id = resolve_token(token)
    if not user_id:
        return jsonify({"ok": False, "error": "Invalid or expired token"}), 401
    ok, msg = device_bind(user_id, hwid, ip)
    if not ok:
        return jsonify({"ok": False, "error": msg}), 403
    return jsonify({"ok": True, "message": msg})


@app.route("/api/auth/validate", methods=["POST"])
def validate():
    """Every run: validate HWID (and optionally IP) for this token."""
    data = request.get_json() or {}
    token = (data.get("token") or request.headers.get("Authorization") or "").strip()
    if token.startswith("Bearer "):
        token = token[7:]
    hwid = (data.get("hwid") or "").strip()
    ip = data.get("ip") or client_ip()
    if not token:
        return jsonify({"ok": False, "error": "Token required"}), 401
    user_id = resolve_token(token)
    if not user_id:
        return jsonify({"ok": False, "error": "Invalid or expired token"}), 401
    ok, msg = device_validate(user_id, hwid, ip)
    if not ok:
        return jsonify({"ok": False, "error": msg}), 403
    return jsonify({"ok": True, "message": msg})


@app.route("/api/health", methods=["GET"])
def health():
    return jsonify({"ok": True, "service": "auth"})


if __name__ == "__main__":
    init_db()
    port = int(os.environ.get("PORT", 5000))
    app.run(host="0.0.0.0", port=port, debug=os.environ.get("FLASK_DEBUG", "0") == "1")
