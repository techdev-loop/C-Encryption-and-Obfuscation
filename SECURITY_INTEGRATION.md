# Security Integration Notes

Reproducible steps to build and deploy the protected Cat Clicker with license validation, anti-debug, and obfuscation.

## Overview

- **License / HWID**: User logs in (or uses stored token); client sends HWID + optional IP to cloud; first run binds device, subsequent runs validate. Wrong HWID or IP (if strict) → clean rejection message.
- **Anti-debug**: Startup and periodic checks for debuggers (x64dbg, WinDbg, Cheat Engine, etc.) and memory tools; process exits if detected.
- **Obfuscation**: Sensitive strings (auth URL, error messages) use compile-time XOR in `obfuscate.h`; they are decrypted only at runtime so they do not appear in plaintext in the executable.

## Build (CMake + MSVC)

1. **Enable license (default ON)**  
   - `-DCATCLICKER_ENABLE_LICENSE=ON` (default).  
   - To build without license/anti-debug: `-DCATCLICKER_ENABLE_LICENSE=OFF`.

2. **Configure and build**  
   ```bat
   cd build
   cmake .. -G "Visual Studio 17 2022" -A x64 -DCMAKE_BUILD_TYPE=Release
   cmake --build . --config Release
   ```

3. **Auth server URL in client**  
   - In `src/main.cpp`, replace the default URL inside `OBF("...")` with your auth API base URL (e.g. `https://auth.yourdomain.com`) and rebuild.  
   - The string is XOR-obfuscated at compile time; never ship the real URL in plaintext.

## Auth Server (Cloud)

- **Location**: `auth_server/` (Flask + SQLite).
- **Deploy**: See `auth_server/README.md`. Run behind HTTPS in production.
- **Flow**:  
  1. User registers/logs in on your website (or via API).  
  2. Client calls `POST /api/auth/login` with email/password → receives `token`.  
  3. First run: client calls `POST /api/auth/bind` with `token`, `hwid`, `ip` (optional).  
  4. Every run: client calls `POST /api/auth/validate` with `token`, `hwid`, `ip` (optional).  
  5. If HWID (or IP when strict) does not match stored → 403 and client shows rejection message.

## Rejection Messages

- **Wrong device**: "This license is bound to another device. Contact support."  
- **Not logged in**: "Not logged in. Please log in to continue."  
- **Invalid/expired token**: "Invalid or expired token" (client may prompt login again).  
- **No network**: "Cannot reach license server. Check internet and try again."

All of these can be passed through your load screen / GUI; the console fallback in `main.cpp` is for when no GUI is used.

**Integrating your load screen**: You can build the load screen for login. Flow: (1) On startup, call `license::validate_session(hwid, ip)`. (2) If `result.need_login` is true, show your login UI; when the user submits credentials, call `license::login_and_bind(email, password, hwid, ip)` then proceed. (3) If `result.success` is false and not need_login, show `result.error_message` and exit. (4) Otherwise continue. The token is stored automatically; next run will validate with the stored token. Set the auth base URL with `license::set_auth_base_url(...)` before any call (e.g. from your obfuscated default).

## Anti-Debug (Performance)

- Checks run **once at startup** and **every 30 seconds** in a separate, low-priority thread.  
- Main loop is not touched; FPS impact is designed to stay within ±10%.  
- Checks: `IsDebuggerPresent`, `CheckRemoteDebuggerPresent`, NtQueryInformationProcess (DebugPort, DebugObjectHandle, DebugFlags), debugger window titles, known process names, and a simple timing check.

## Obfuscation

- **Strings**: Use `OBF("literal")` in code; call `.decrypt()` at runtime to get `std::string`.  
- **Constants**: Use `OBF_CONST(type, value, key_byte)` for numeric constants if needed.  
- **Auth URL**: Set once at startup via `license::set_auth_base_url(OBF("https://...").decrypt());`.

## Polymorphic Downloads (Optional)

To change the executable hash/name per user or per download:

1. **Per-download build**: Build once, then run a script that patches a few bytes (e.g. a GUID or nonce in a dedicated section) and renames the file. Each download gets a different binary.

2. **Minimal patch script** (Python, run after build):  
   - Read the built EXE.  
   - Overwrite 4–16 bytes at a fixed offset (e.g. in a `.rdata` or custom section) with random bytes or a user ID.  
   - Write to a new file (e.g. `cat_clicker_<userid>.exe`).  
   - Serve this file to the user.

Example: `scripts/polymorphic_patch.py`. Run after build; it finds a 32-byte zero block in the EXE and overwrites it with random bytes, then writes a new file. Use a different output filename per user (e.g. `cat_clicker_<userid>.exe`) so hash and name vary per download. You can integrate this into your download pipeline.

## Reproducing for Future Builds

1. Keep `CATCLICKER_ENABLE_LICENSE=ON` in CMake.  
2. Keep `auth_server/` deployed and point the client URL to it.  
3. Replace the URL in `OBF("...")` in `main.cpp` for your environment and rebuild.  
4. Optionally run the polymorphic patch script as a post-build step for each download.
