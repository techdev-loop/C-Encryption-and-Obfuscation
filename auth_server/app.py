"""
Auth Server - HWID + IP verification for Cat Clicker.
Deploy to cloud (e.g. Heroku, Railway, AWS) or run locally.

Usage:
  pip install flask
  python app.py

  Add license: POST /admin/add  {"hwid": "...", "ip": "optional"}
  Check auth:  GET  /auth?hwid=...&ip=...
"""
import json
import os
from pathlib import Path

from flask import Flask, request, jsonify

app = Flask(__name__)
DB_PATH = Path(os.environ.get("AUTH_DB", "licenses.json"))


def load_db():
    if not DB_PATH.exists():
        return {}
    with open(DB_PATH, "r") as f:
        return json.load(f)


def save_db(data):
    with open(DB_PATH, "w") as f:
        json.dump(data, f, indent=2)


@app.route("/auth")
def auth():
    """Verify HWID + IP. Returns OK if authorized."""
    hwid = request.args.get("hwid", "")
    ip = request.args.get("ip", "")
    if not hwid:
        return jsonify({"ok": False, "reason": "missing hwid"}), 400

    db = load_db()
    entry = db.get(hwid)
    if not entry:
        return jsonify({"ok": False, "reason": "unknown hwid"}), 403

    # If IP is bound for this HWID, it must match
    bound_ip = entry.get("ip")
    if bound_ip and bound_ip != "0.0.0.0":
        if ip != bound_ip:
            return jsonify({"ok": False, "reason": "ip_mismatch"}), 403

    return "OK"  # or jsonify({"ok": True})


@app.route("/admin/add", methods=["POST"])
def admin_add():
    """Add a license. Body: {"hwid": "...", "ip": "optional"}"""
    data = request.get_json() or {}
    hwid = data.get("hwid", "").strip()
    ip = data.get("ip", "").strip() or "0.0.0.0"  # 0.0.0.0 = any IP
    if not hwid:
        return jsonify({"ok": False, "reason": "missing hwid"}), 400

    db = load_db()
    db[hwid] = {"ip": ip}
    save_db(db)
    return jsonify({"ok": True, "hwid": hwid, "ip": ip})


@app.route("/admin/remove", methods=["POST"])
def admin_remove():
    """Remove a license. Body: {"hwid": "..."}"""
    data = request.get_json() or {}
    hwid = data.get("hwid", "").strip()
    if not hwid:
        return jsonify({"ok": False, "reason": "missing hwid"}), 400

    db = load_db()
    if hwid in db:
        del db[hwid]
        save_db(db)
    return jsonify({"ok": True})


@app.route("/admin/list")
def admin_list():
    """List all licenses."""
    db = load_db()
    return jsonify({"licenses": [{"hwid": k, "ip": v.get("ip", "")} for k, v in db.items()]})


if __name__ == "__main__":
    port = int(os.environ.get("PORT", 5000))
    app.run(host="0.0.0.0", port=port)
