# AI Teacher: chapter-loading error diagnostics

The observed log `Gemini HTTP 403 bytes=155 attempt=1` means the API request was refused. It does not identify which credential/project restriction caused the refusal. The API's `error.message` is needed to distinguish a blocked/leaked key, application/API restrictions, or a disabled service.

A separate confirmed sketch bug hides that refusal: failed chapter/question requests switch to STATE_ERROR but do not redraw the display. The last Loading chapters screen remains visible even after the request has returned.

## Apply

1. Open `AI_Teacher/AI_Teacher.ino` in your existing Arduino IDE environment with the same ESP32 board, partition layout and library versions you used for the original sketch.
2. Set `GEMINI_API_KEY` to your key locally. This downloadable source deliberately contains only `YOUR_GEMINI_API_KEY`; never post your real key or commit it to GitHub.
3. Default Wi-Fi SSID/password are blank in this public copy. Existing saved device Wi-Fi is retained; otherwise use the sketch's existing setup portal. You may fill in your own defaults locally.
4. Upload the sketch. Keep the original LittleFS data files (logo and MP3 clips); they were not included in the pasted code and are not replaced by this fix.
5. Open Serial Monitor at 115200 baud and repeat the selection. Read the `[AI] Gemini HTTP 403` and following `[AI] ...` message. The configured key is redacted from that message. Share only those error lines, not the key or request headers.

## Changes

- Draw errors immediately instead of leaving a loading screen visible.
- Preserve the HTTP status and parse the API's error.message; show a short detail on the TFT and the longer detail in Serial Monitor.
- Put the API key in the x-goog-api-key header rather than the request URL.
- Fail immediately on 400/401/403/404/429; retry a transient network/server failure at most once.
- Set TCP connect timeout to 10 seconds, TLS handshake timeout to 12 seconds and HTTP read timeout to 20 seconds. These are separate phase timeouts, not one overall request deadline.
- Raise maxOutputTokens from 320 to 1536 to reduce truncated chapter-list JSON; detect MAX_TOKENS explicitly.
- Collect all non-thinking text parts from the first candidate and report empty/blocked/malformed API responses.

The original sketch's TLS trust setting, display pins, audio behavior, chapter prompt and model choice are retained. This is source for diagnosis, not a compiled firmware or a flasher EXE. The full Arduino sketch has not been compiled here because the installed board/core/library versions and LittleFS data from your working project were not supplied. No live Gemini request or ESP32 hardware test was run.

## Resolve 403

The source change makes the failure visible; it cannot change Google's authorization decision. Use the exact error.message:

- If it says the key was reported as leaked or blocked, replace the key in Google AI Studio and update it privately in the sketch.
- If it names an API/application restriction, correct that restriction for this ESP32 client. Browser HTTP-referrer restrictions do not match direct ESP32 requests.
- If it says the API is disabled, enable the Generative Language API in the project that owns the key.

Keep API restrictions appropriate to the Gemini API. A 403 is distinct from a 429 quota/rate-limit error.
