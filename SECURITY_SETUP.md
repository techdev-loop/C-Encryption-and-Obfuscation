# Security & Obfuscation Setup

## Overview

- **User auth**: HWID + IP verified against your auth server
- **Anti-debug**: Exits if debugger detected
- **Source obfuscation**: Compile-time string encryption, polymorphic binary
- **Task manager**: Optional generic window title (configurable)

## 1. Auth Server

### Run locally
```bash
cd auth_server
pip install -r requirements.txt
python app.py
```

### Add a user
1. Run client: `cat_clicker.exe --print-hwid` → copy HWID
2. Add license: `curl -X POST http://localhost:5000/admin/add -H "Content-Type: application/json" -d "{\"hwid\":\"YOUR_HWID\",\"ip\":\"0.0.0.0\"}"`
   - Use `"ip":"0.0.0.0"` for any IP, or a specific IP to bind

### Deploy (Heroku/Railway/AWS)
- Deploy `auth_server/` (see auth_server/README.md)
- Note your URL, e.g. `https://yourapp.herokuapp.com/auth`

## 2. Build with Auth

```batch
cmake -B build -S . -DAUTH_SERVER_URL="https://yourapp.herokuapp.com/auth" ...
cmake --build build --config Release
```

Or pass at runtime: `cat_clicker.exe --auth https://yourserver.com/auth`

## 3. Polymorphic Builds

Each build produces a different binary (hash, size) via:
- Timestamp-based junk padding
- Per-build obfuscation key
- Run `cmake --build` to get a fresh variant

## 4. Obfuscating Strings

Use the `OBF()` macro for sensitive strings:

```cpp
#include "obfuscate.h"
std::string msg = OBF("secret message");
```

## 5. Task Manager Stealth

In `main.cpp`, set `stealth::init_stealth(true)` to use a generic window title.
