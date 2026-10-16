# Generated AI Teacher audio

41 spoken-English MP3 files matching the existing sketch:

- `welcome.mp3`: welcome message.
- `c00.mp3` through `c19.mp3`: 20 correct-answer encouragement clips.
- `w00.mp3` through `w19.mp3`: 20 supportive wrong-answer clips.

The synthetic voice is CMU Flite `slt`. Each file is mono MP3, 22,050 Hz, 48 kbit/s. All clips are under 7.5 seconds to fit the sketch's eight-second playback loop. The generated set totals 684,295 bytes (about 668 KiB), leaving room in the 2 MB filesystem layout described by the sketch.

## Upload to your ESP32

1. Extract `AI-Teacher-Audio.zip`.
2. Copy its **data** folder beside the `.ino` file in your actual Arduino sketch folder. Do not replace your working sketch or API key.
3. Keep any other filesystem assets you need inside that folder too. In particular, add your existing `logo.rgb565` if you want to retain the splash image. This audio package does not include or replace a logo.
4. In Arduino IDE, select the same ESP32 board, COM port and partition scheme you used for your working sketch. Its documented scheme is **No OTA (2MB APP / 2MB SPIFFS)**; do not change a working partition layout just to upload these files.
5. With the LittleFS uploader installed and Serial Monitor closed, press **Ctrl+Shift+P**, select **Upload LittleFS to Pico/ESP8266/ESP32**, and wait for completion.
6. Reset your board and try a question. The sketch selects `c00`–`c19` and `w00`–`w19` randomly, so retain every numbered file.

**Uploading LittleFS replaces its contents**, including any omitted logo or old audio files. It does not require a whole-flash erase. Files outside the `data` folder in this package are documentation and should not be uploaded to the device filesystem.

LittleFS uploader installation and usage: https://github.com/earlephilhower/arduino-littlefs-upload

## Contents and verification

`transcripts.csv` lists exactly what each clip says. `manifest.json` records duration, format, decoded mean volume, byte size and SHA-256. `SHA256SUMS.txt` contains the hashes of all 41 audio files. Every MP3 was decoded successfully and checked for non-silent audio, mono/22,050 Hz format, expected filenames and duration below the playback limit. Actual speaker/DAC playback was not tested on an ESP32 here.

`generate_audio.py` can regenerate or customize the messages on a Linux machine with Python 3 and FFmpeg built with `flite` and `libmp3lame`. Those tools are only needed for regeneration; the ESP32 uses the ready-made MP3 files.
