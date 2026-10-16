#include <WiFi.h>
#include <WebServer.h>
#include <MD_MAX72xx.h>
#include <SPI.h>
#include "pixelcar-control.h"

// ====================== MOTOR PINS ======================
#define ENA 16
#define ENB 17
#define IN1 26
#define IN2 27
#define IN3 12
#define IN4 13

// ====================== IR OBSTACLE SENSOR ======================
#define IR_SENSOR_PIN 15
const uint8_t IR_OBSTACLE_ACTIVE_STATE = LOW;
const uint16_t AVOID_STOP_MS = 120;
const uint16_t AVOID_BACK_MS = 450;
const uint16_t AVOID_LEFT_MS = 450;
const uint8_t AVOID_MOTOR_SPEED = 180;

// ====================== WIFI ======================
const char *ssid = "MarsRover - 001";
const char *password = "12345678";

WebServer server(80);

// ====================== DOT MATRIX SETUP ======================
#define HARDWARE_TYPE MD_MAX72XX::FC16_HW
#define MAX_DEVICES 4

#define DATA_PIN 18
#define CLK_PIN 23
#define CS_PIN 19

MD_MAX72XX mx = MD_MAX72XX(HARDWARE_TYPE, DATA_PIN, CLK_PIN, CS_PIN, MAX_DEVICES);

// ====================== MODULE MAPPING FOR SQUARE FACE ======================
const int TOP_LEFT = 0;
const int TOP_RIGHT = 1;
const int BOTTOM_LEFT = 2;
const int BOTTOM_RIGHT = 3;

// ====================== TEXT MODE SETTINGS ======================
const uint8_t DISPLAY_COLUMNS = MAX_DEVICES * 8;
const uint16_t SCROLL_DELAY_MS = 60;
const bool MATRIX_MIRROR_H = true;
const bool MATRIX_FLIP_V = false;

String currentMessage = "HELLO FROM ESP32";
uint16_t scrollOffset = 0;
unsigned long lastScrollTick = 0;
int16_t loopResetPoint = DISPLAY_COLUMNS;

// ====================== MODE AND CAR STATE ======================
enum DisplayMode
{
  MODE_SELECT,
  MODE_FACE,
  MODE_TEXT
};

enum DriveMode
{
  DRIVE_STOPPED,
  DRIVE_FORWARD,
  DRIVE_BACKWARD,
  DRIVE_LEFT,
  DRIVE_RIGHT
};

enum AvoidanceStep
{
  AVOID_IDLE,
  AVOID_STOPPING,
  AVOID_BACKING,
  AVOID_TURNING_LEFT
};

DisplayMode currentDisplayMode = MODE_SELECT;
DriveMode currentDriveMode = DRIVE_STOPPED;
AvoidanceStep avoidanceStep = AVOID_IDLE;
bool obstacleDetectionEnabled = false;
bool customFaceActive = false;
bool lastReportedObstacleState = false;
bool wifiClientWasConnected = false;
unsigned long avoidanceStepStartedAt = 0;

const uint8_t DEFAULT_DRIVE_SPEED = 180;
const uint8_t MAX_PROGRAM_STEPS = 12;
uint8_t currentDriveSpeed = DEFAULT_DRIVE_SPEED;

struct ProgramStep
{
  DriveMode mode;
  uint8_t speed;
  uint16_t durationMs;
};

ProgramStep programSteps[MAX_PROGRAM_STEPS];
uint8_t programStepCount = 0;
uint8_t programStepIndex = 0;
bool programRunning = false;
unsigned long programStepStartedAt = 0;

// ====================== EYE FRAMES ======================
const uint8_t EYE_OPEN[8] = {
    B00111100,
    B01111110,
    B11100111,
    B11011011,
    B11011011,
    B11100111,
    B01111110,
    B00111100};

const uint8_t EYE_LEFT[8] = {
    B00111100,
    B01111110,
    B11100111,
    B11110011,
    B11110011,
    B11100111,
    B01111110,
    B00111100};

const uint8_t EYE_RIGHT[8] = {
    B00111100,
    B01111110,
    B11100111,
    B11001111,
    B11001111,
    B11100111,
    B01111110,
    B00111100};

const uint8_t EYE_HALF[8] = {
    B00000000,
    B00111100,
    B01111110,
    B11111111,
    B01111110,
    B00111100,
    B00000000,
    B00000000};

const uint8_t EYE_CLOSED[8] = {
    B00000000,
    B00000000,
    B11111111,
    B01111110,
    B00111100,
    B00000000,
    B00000000,
    B00000000};

// ====================== MOUTH FRAMES ======================
const uint16_t MOUTH_SMILE[8] = {
    0b0000000000000000,
    0b0000000000000000,
    0b0000000000000000,
    0b1000000000000001,
    0b0100000000000010,
    0b0011111111111100,
    0b0001111111111000,
    0b0000000000000000};

const uint16_t MOUTH_BIGSMILE[8] = {
    0b0000000000000000,
    0b0000000000000000,
    0b1100000000000011,
    0b0110000000000110,
    0b0011111111111100,
    0b0001111111111000,
    0b0000011111100000,
    0b0000000000000000};

const uint16_t MOUTH_TALK1[8] = {
    0b0000000000000000,
    0b0000000000000000,
    0b0000001111100000,
    0b0000111111111000,
    0b0001110000011100,
    0b0001110000011100,
    0b0000111111111000,
    0b0000001111100000};

const uint16_t MOUTH_TALK2[8] = {
    0b0000000000000000,
    0b0000001111100000,
    0b0000111111111000,
    0b0011110000111100,
    0b0011110000111100,
    0b0011110000111100,
    0b0000111111111000,
    0b0000001111100000};

unsigned long lastFaceUpdate = 0;
int faceState = 0;

// ====================== WEB PAGES ======================
const char pageStyle[] PROGMEM = R"rawliteral(
<style>
  * { box-sizing: border-box; }
  body {
    margin: 0;
    min-height: 100vh;
    text-align: center;
    font-family: Arial, sans-serif;
    background:
      radial-gradient(circle at top left, rgba(37, 99, 235, 0.22), transparent 34%),
      radial-gradient(circle at bottom right, rgba(76, 175, 80, 0.22), transparent 32%),
      #f2f2f2;
    color: #222;
    display: grid;
    place-items: center;
    padding: 20px;
  }
  .card {
    width: min(620px, 100%);
    background: white;
    border-radius: 12px;
    padding: 22px;
    box-shadow: 0 10px 28px rgba(0, 0, 0, 0.12);
  }
  h2 { margin: 0 0 16px; color: #333; }
  p { color: #555; margin: 0 0 16px; }
  button, .mode-link {
    min-width: 100px;
    height: 60px;
    font-size: 18px;
    margin: 8px;
    border: none;
    border-radius: 10px;
    background-color: #4CAF50;
    color: white;
    cursor: pointer;
    text-decoration: none;
    display: inline-flex;
    align-items: center;
    justify-content: center;
    padding: 0 18px;
  }
  button:active, .mode-link:active { background-color: #45a049; }
  .stop { background-color: red; }
  .secondary { background-color: #2563eb; }
  .row { margin: 6px 0; }
  input[type="text"] {
    width: min(360px, 100%);
    height: 46px;
    padding: 0 12px;
    font-size: 18px;
    border: 1px solid #bbb;
    border-radius: 8px;
    margin: 8px;
  }
  .toggle-row {
    margin-top: 14px;
    display: inline-flex;
    align-items: center;
    gap: 10px;
    font-size: 16px;
  }
  .status {
    min-height: 22px;
    margin-top: 10px;
    color: #2563eb;
  }
  .sensor-panel {
    margin: 16px auto 4px;
    display: grid;
    grid-template-columns: repeat(2, minmax(0, 1fr));
    gap: 10px;
    max-width: 420px;
  }
  .sensor-card {
    border: 1px solid #d7dde8;
    border-radius: 10px;
    padding: 12px;
    background: #f8fafc;
  }
  .sensor-label {
    font-size: 13px;
    color: #64748b;
    margin-bottom: 6px;
  }
  .sensor-value {
    font-size: 18px;
    font-weight: 700;
    color: #16a34a;
  }
  .sensor-card.alert .sensor-value {
    color: #dc2626;
  }
  .block-app {
    width: min(760px, 100%);
    background: white;
    border-radius: 12px;
    padding: 18px;
    box-shadow: 0 10px 28px rgba(0, 0, 0, 0.12);
  }
  .block-toolbar {
    display: grid;
    grid-template-columns: repeat(4, 1fr);
    gap: 8px;
    margin: 12px 0;
  }
  .block-toolbar button,
  .program-actions button {
    width: 100%;
    min-width: 0;
    margin: 0;
  }
  .program-list {
    display: grid;
    gap: 8px;
    margin: 12px 0;
  }
  .code-block {
    display: grid;
    grid-template-columns: 34px 1fr 86px 88px 38px;
    align-items: center;
    gap: 8px;
    min-height: 54px;
    padding: 8px;
    border: 1px solid #d7dde8;
    border-radius: 8px;
    background: #f8fafc;
  }
  .step-number {
    width: 28px;
    height: 28px;
    border-radius: 50%;
    display: grid;
    place-items: center;
    background: #2563eb;
    color: #fff;
    font-weight: 700;
  }
  .block-name {
    text-align: left;
    font-size: 18px;
    font-weight: 700;
  }
  .code-block select,
  .code-block input {
    width: 100%;
    height: 38px;
    border: 1px solid #b8c3d2;
    border-radius: 6px;
    padding: 0 8px;
    font-size: 16px;
  }
  .remove-block {
    min-width: 0;
    width: 38px;
    height: 38px;
    margin: 0;
    padding: 0;
    background: #dc2626;
  }
  .program-actions {
    display: grid;
    grid-template-columns: 1fr 1fr;
    gap: 8px;
    margin-top: 10px;
  }
  .hint {
    margin-top: 10px;
    color: #64748b;
    font-size: 14px;
  }
  @media (max-width: 640px) {
    .block-toolbar { grid-template-columns: repeat(2, 1fr); }
    .code-block { grid-template-columns: 30px 1fr 70px 72px 36px; gap: 6px; }
    .block-name { font-size: 15px; }
    .code-block select,
    .code-block input { font-size: 14px; padding: 0 4px; }
  }
</style>
)rawliteral";

const char setupPage[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
  <title>ESP32 Setup</title>
  <meta name="viewport" content="width=device-width, initial-scale=1">
)rawliteral";

const char setupBody[] PROGMEM = R"rawliteral(
</head>
<body>
  <div class="card">
    <h2>Connecting to ESP32</h2>
    <p>Keep this page open. It will move to the connected page automatically.</p>
    <div class="status" id="status">Checking connection...</div>
  </div>
  <script>
    async function checkConnection() {
      try {
        const res = await fetch('/wifi-status', { cache: 'no-store' });
        if (res.ok && (await res.text()).trim() === 'connected') {
          window.location.replace('/connected');
          return;
        }
      } catch (e) {}
      setTimeout(checkConnection, 800);
    }
    checkConnection();
  </script>
</body>
</html>
)rawliteral";

const char connectedPage[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
  <title>ESP32 Connected</title>
  <meta name="viewport" content="width=device-width, initial-scale=1">
)rawliteral";

const char connectedBody[] PROGMEM = R"rawliteral(
</head>
<body>
  <div class="card">
    <h2>Connected Successfully</h2>
    <p>Select a page for the car and display.</p>
    <a class="mode-link" href="/face">Face Mode</a>
    <a class="mode-link secondary" href="/text">Text Mode</a>
    <a class="mode-link secondary" href="/blocks">Block Code</a>
  </div>
</body>
</html>
)rawliteral";

String makeCarControls()
{
  return R"rawliteral(
    <div class="row">
      <button onclick="sendCmd('forward')">Forward</button>
    </div>
    <div class="row">
      <button onclick="sendCmd('left')">Left</button>
      <button class="stop" onclick="sendCmd('stop')">Stop</button>
      <button onclick="sendCmd('right')">Right</button>
    </div>
    <div class="row">
      <button onclick="sendCmd('backward')">Backward</button>
    </div>
    <label class="toggle-row">
      <input id="obstacleToggle" type="checkbox" onchange="setObstacle(this.checked)">
      Obstacle Detection
    </label>
    <div class="sensor-panel">
      <div class="sensor-card" id="modeCard">
        <div class="sensor-label">Obstacle Mode</div>
        <div class="sensor-value" id="modeValue">OFF</div>
      </div>
      <div class="sensor-card" id="irCard">
        <div class="sensor-label">IR Sensor GPIO15</div>
        <div class="sensor-value" id="irValue">CLEAR</div>
      </div>
    </div>
)rawliteral";
}

String makeSharedScript()
{
  return R"rawliteral(
  <script>
    const statusEl = document.getElementById('status');
    const obstacleToggle = document.getElementById('obstacleToggle');
    const modeCard = document.getElementById('modeCard');
    const modeValue = document.getElementById('modeValue');
    const irCard = document.getElementById('irCard');
    const irValue = document.getElementById('irValue');

    function setStatus(text) {
      if (statusEl) statusEl.textContent = text;
    }

    async function sendCmd(cmd) {
      const res = await fetch('/car/' + cmd);
      setStatus(await res.text());
    }

    async function setObstacle(enabled) {
      const res = await fetch('/obstacle/' + (enabled ? 'on' : 'off'));
      setStatus(await res.text());
      updateObstacleStatus();
    }

    async function updateObstacleStatus() {
      if (!modeValue || !irValue) return;
      try {
        const res = await fetch('/obstacle/status', { cache: 'no-store' });
        const parts = (await res.text()).split(',');
        const enabled = parts[0] === '1';
        const detected = parts[1] === '1';

        if (obstacleToggle) obstacleToggle.checked = enabled;
        modeValue.textContent = enabled ? 'ON' : 'OFF';
        irValue.textContent = detected ? 'OBJECT' : 'CLEAR';
        modeCard.classList.toggle('alert', enabled);
        irCard.classList.toggle('alert', detected);
      } catch (e) {}
    }

    updateObstacleStatus();
    setInterval(updateObstacleStatus, 600);
  </script>
)rawliteral";
}

String buildSetupPage()
{
  String page = FPSTR(setupPage);
  page += FPSTR(pageStyle);
  page += FPSTR(setupBody);
  return page;
}

String buildConnectedPage()
{
  String page = FPSTR(connectedPage);
  page += FPSTR(pageStyle);
  page += FPSTR(connectedBody);
  return page;
}

String buildFacePage()
{
  String page = F("<!DOCTYPE html><html><head><title>ESP32 Face Mode</title><meta name=\"viewport\" content=\"width=device-width, initial-scale=1\">");
  page += FPSTR(pageStyle);
  page += F("</head><body><div class=\"card\"><h2>Face Mode</h2><p>Square matrix face display is running.</p>");
  page += F("<a class=\"mode-link secondary\" href=\"/\">Change Mode</a>");
  page += makeCarControls();
  page += F("<div class=\"status\" id=\"status\"></div></div>");
  page += makeSharedScript();
  page += F("</body></html>");
  return page;
}

String buildTextPage()
{
  String page = F("<!DOCTYPE html><html><head><title>ESP32 Text Mode</title><meta name=\"viewport\" content=\"width=device-width, initial-scale=1\">");
  page += FPSTR(pageStyle);
  page += F("</head><body><div class=\"card\"><h2>Text Mode</h2><p>Straight matrix text display is running.</p>");
  page += F("<a class=\"mode-link secondary\" href=\"/\">Change Mode</a>");
  page += F("<div class=\"row\"><input id=\"msg\" type=\"text\" maxlength=\"120\" placeholder=\"Type text\"><button onclick=\"sendText()\">Send Text</button></div>");
  page += makeCarControls();
  page += F("<div class=\"status\" id=\"status\"></div></div>");
  page += R"rawliteral(
  <script>
    async function sendText() {
      const msg = document.getElementById('msg');
      const text = msg.value.trim() || ' ';
      const res = await fetch('/setText', {
        method: 'POST',
        headers: { 'Content-Type': 'text/plain' },
        body: text
      });
      setStatus('Current: ' + await res.text());
    }
  </script>
)rawliteral";
  page += makeSharedScript();
  page += F("</body></html>");
  return page;
}

String buildBlockPage()
{
  String page = F("<!DOCTYPE html><html><head><title>ESP32 Block Code</title><meta name=\"viewport\" content=\"width=device-width, initial-scale=1\">");
  page += FPSTR(pageStyle);
  page += R"rawliteral(
</head>
<body>
  <div class="block-app">
    <h2>Block Code</h2>
    <p>Add simple blocks, then press Start.</p>
    <div>
      <a class="mode-link secondary" href="/">Change Mode</a>
      <a class="mode-link" href="/face">Face</a>
      <a class="mode-link secondary" href="/text">Text</a>
    </div>

    <div class="block-toolbar">
      <button onclick="addBlock('forward')">Forward</button>
      <button onclick="addBlock('backward')">Backward</button>
      <button onclick="addBlock('left')">Left</button>
      <button onclick="addBlock('right')">Right</button>
    </div>
    <div class="block-toolbar">
      <button class="stop" onclick="addBlock('stop')">Stop</button>
      <button class="secondary" onclick="clearBlocks()">Clear</button>
      <button class="secondary" onclick="addExample()">Example</button>
      <button class="stop" onclick="stopProgram()">Emergency Stop</button>
    </div>

    <div class="program-list" id="program"></div>
    <div class="program-actions">
      <button onclick="startProgram()">Start</button>
      <button class="stop" onclick="stopProgram()">Stop</button>
    </div>
    <div class="status" id="status">Ready</div>
    <div class="hint">Each block runs from top to bottom. Speed is 0 to 255. Time is seconds.</div>
  </div>

  <script>
    const programEl = document.getElementById('program');
    const statusEl = document.getElementById('status');
    const steps = [];
    const names = {
      forward: 'Move Forward',
      backward: 'Move Backward',
      left: 'Turn Left',
      right: 'Turn Right',
      stop: 'Stop'
    };

    function setStatus(text) {
      statusEl.textContent = text;
    }

    function addBlock(type) {
      if (steps.length >= 12) {
        setStatus('Only 12 blocks allowed');
        return;
      }
      steps.push({ type, speed: type === 'stop' ? 0 : 180, time: type === 'stop' ? 0.5 : 1 });
      renderBlocks();
    }

    function removeBlock(index) {
      steps.splice(index, 1);
      renderBlocks();
    }

    function clearBlocks() {
      steps.length = 0;
      renderBlocks();
      setStatus('Blocks cleared');
    }

    function addExample() {
      steps.length = 0;
      steps.push(
        { type: 'forward', speed: 180, time: 1 },
        { type: 'left', speed: 160, time: 0.5 },
        { type: 'forward', speed: 180, time: 1 },
        { type: 'stop', speed: 0, time: 0.3 }
      );
      renderBlocks();
      setStatus('Example loaded');
    }

    function updateStep(index, field, value) {
      if (field === 'type') {
        steps[index].type = value;
        if (value === 'stop') steps[index].speed = 0;
      } else if (field === 'speed') {
        steps[index].speed = Math.max(0, Math.min(255, Number(value) || 0));
      } else if (field === 'time') {
        steps[index].time = Math.max(0.1, Math.min(10, Number(value) || 1));
      }
      renderBlocks();
    }

    function renderBlocks() {
      programEl.innerHTML = '';
      if (!steps.length) {
        const empty = document.createElement('div');
        empty.className = 'hint';
        empty.textContent = 'No blocks yet';
        programEl.appendChild(empty);
        return;
      }

      steps.forEach((step, index) => {
        const row = document.createElement('div');
        row.className = 'code-block';
        row.innerHTML = `
          <div class="step-number">${index + 1}</div>
          <div class="block-name">${names[step.type]}</div>
          <select onchange="updateStep(${index}, 'speed', this.value)" ${step.type === 'stop' ? 'disabled' : ''}>
            ${[90, 130, 180, 220, 255].map(v => `<option value="${v}" ${step.speed === v ? 'selected' : ''}>${v}</option>`).join('')}
          </select>
          <input type="number" min="0.1" max="10" step="0.1" value="${step.time}" onchange="updateStep(${index}, 'time', this.value)">
          <button class="remove-block" onclick="removeBlock(${index})">X</button>
        `;
        programEl.appendChild(row);
      });
    }

    function buildProgramText() {
      return steps.map(step => {
        const ms = Math.round(step.time * 1000);
        return `${step.type},${step.speed},${ms}`;
      }).join('\n');
    }

    async function startProgram() {
      if (!steps.length) {
        setStatus('Add blocks first');
        return;
      }
      const res = await fetch('/program/run', {
        method: 'POST',
        headers: { 'Content-Type': 'text/plain' },
        body: buildProgramText()
      });
      setStatus(await res.text());
    }

    async function stopProgram() {
      const res = await fetch('/program/stop');
      setStatus(await res.text());
    }

    renderBlocks();
  </script>
</body>
</html>
)rawliteral";
  return page;
}

// ====================== CAR MOVEMENT ======================
void setMotorRaw(int left, int right)
{
  left = constrain(left, -255, 255);
  right = constrain(right, -255, 255);

  // L298N/L293D direction inputs. ENA and ENB control motor speed.
  digitalWrite(IN1, left > 0 ? HIGH : LOW);
  digitalWrite(IN2, left < 0 ? HIGH : LOW);
  digitalWrite(IN3, right > 0 ? HIGH : LOW);
  digitalWrite(IN4, right < 0 ? HIGH : LOW);

  ledcWrite(ENA, abs(left));
  ledcWrite(ENB, abs(right));
}

void stopCar()
{
  setMotorRaw(0, 0);
}

void moveForward()
{
  setMotorRaw(currentDriveSpeed, currentDriveSpeed);
}

void moveBackward()
{
  setMotorRaw(-currentDriveSpeed, -currentDriveSpeed);
}

void turnLeft()
{
  setMotorRaw(-currentDriveSpeed, currentDriveSpeed);
}

void turnRight()
{
  setMotorRaw(currentDriveSpeed, -currentDriveSpeed);
}

void setDriveMode(DriveMode mode)
{
  currentDriveMode = mode;
  avoidanceStep = AVOID_IDLE;

  switch (mode)
  {
  case DRIVE_FORWARD:
    moveForward();
    break;
  case DRIVE_BACKWARD:
    moveBackward();
    break;
  case DRIVE_LEFT:
    turnLeft();
    break;
  case DRIVE_RIGHT:
    turnRight();
    break;
  case DRIVE_STOPPED:
  default:
    stopCar();
    break;
  }
}

void setDriveModeWithSpeed(DriveMode mode, uint8_t speed)
{
  currentDriveSpeed = constrain(speed, 0, 255);
  setDriveMode(mode);
}

DriveMode parseDriveMode(String value)
{
  value.trim();
  value.toLowerCase();

  if (value == "forward") return DRIVE_FORWARD;
  if (value == "backward") return DRIVE_BACKWARD;
  if (value == "left") return DRIVE_LEFT;
  if (value == "right") return DRIVE_RIGHT;
  return DRIVE_STOPPED;
}

void stopProgram()
{
  programRunning = false;
  programStepCount = 0;
  programStepIndex = 0;
  setDriveMode(DRIVE_STOPPED);
}

bool parseProgramPayload(const String &payload)
{
  stopProgram();

  int start = 0;
  while (start < payload.length() && programStepCount < MAX_PROGRAM_STEPS)
  {
    int end = payload.indexOf('\n', start);
    if (end < 0) end = payload.length();

    String line = payload.substring(start, end);
    line.trim();
    if (line.length() > 0)
    {
      int firstComma = line.indexOf(',');
      int secondComma = line.indexOf(',', firstComma + 1);
      if (firstComma <= 0 || secondComma <= firstComma)
      {
        return false;
      }

      String modeText = line.substring(0, firstComma);
      uint8_t speed = constrain(line.substring(firstComma + 1, secondComma).toInt(), 0, 255);
      uint16_t durationMs = constrain(line.substring(secondComma + 1).toInt(), 100, 10000);

      programSteps[programStepCount].mode = parseDriveMode(modeText);
      programSteps[programStepCount].speed = speed;
      programSteps[programStepCount].durationMs = durationMs;
      programStepCount++;
    }

    start = end + 1;
  }

  return programStepCount > 0;
}

void startProgram()
{
  programStepIndex = 0;
  programRunning = true;
  programStepStartedAt = millis();
  setDriveModeWithSpeed(programSteps[0].mode, programSteps[0].speed);
}

void updateProgramRunner()
{
  if (!programRunning)
  {
    return;
  }

  if (millis() - programStepStartedAt < programSteps[programStepIndex].durationMs)
  {
    return;
  }

  programStepIndex++;
  if (programStepIndex >= programStepCount)
  {
    stopProgram();
    return;
  }

  programStepStartedAt = millis();
  setDriveModeWithSpeed(programSteps[programStepIndex].mode, programSteps[programStepIndex].speed);
}

bool isObstacleDetected()
{
  return digitalRead(IR_SENSOR_PIN) == IR_OBSTACLE_ACTIVE_STATE;
}

bool shouldCheckObstacleForCurrentDrive()
{
  // Keep the IR guard active even while the car is stationary. Do not start
  // another avoidance manoeuvre while the user is intentionally reversing.
  return currentDriveMode != DRIVE_BACKWARD;
}

void handleObstacleAvoidance()
{
  bool detected = isObstacleDetected();
  if (detected != lastReportedObstacleState)
  {
    lastReportedObstacleState = detected;
    Serial.println(detected ? "IR GPIO15: OBJECT" : "IR GPIO15: CLEAR");
  }

  if (!obstacleDetectionEnabled)
  {
    return;
  }

  unsigned long now = millis();

  if (avoidanceStep == AVOID_IDLE)
  {
    if (shouldCheckObstacleForCurrentDrive() && detected)
    {
      // Briefly stop before reversing to protect the H-bridge and make sure
      // both motors accept the direction change.
      avoidanceStep = AVOID_STOPPING;
      avoidanceStepStartedAt = now;
      stopCar();
    }
    return;
  }

  if (avoidanceStep == AVOID_STOPPING && now - avoidanceStepStartedAt >= AVOID_STOP_MS)
  {
    avoidanceStep = AVOID_BACKING;
    avoidanceStepStartedAt = now;
    setMotorRaw(-AVOID_MOTOR_SPEED, -AVOID_MOTOR_SPEED);
    return;
  }

  if (avoidanceStep == AVOID_BACKING && now - avoidanceStepStartedAt >= AVOID_BACK_MS)
  {
    avoidanceStep = AVOID_TURNING_LEFT;
    avoidanceStepStartedAt = now;
    setMotorRaw(-AVOID_MOTOR_SPEED, AVOID_MOTOR_SPEED);
    return;
  }

  if (avoidanceStep == AVOID_TURNING_LEFT && now - avoidanceStepStartedAt >= AVOID_LEFT_MS)
  {
    avoidanceStep = AVOID_IDLE;
    // Resume the command that was active before obstacle avoidance.
    switch (currentDriveMode)
    {
    case DRIVE_FORWARD: moveForward(); break;
    case DRIVE_BACKWARD: moveBackward(); break;
    case DRIVE_LEFT: turnLeft(); break;
    case DRIVE_RIGHT: turnRight(); break;
    default: stopCar(); break;
    }
  }
}

// ====================== FACE FUNCTIONS ======================
void draw8x8(int module, const uint8_t pattern[8])
{
  for (int row = 0; row < 8; row++)
  {
    for (int col = 0; col < 8; col++)
    {
      bool pixel = bitRead(pattern[row], 7 - col);
      mx.setPoint(row, module * 8 + col, pixel);
    }
  }
}

void draw16x8(int leftModule, int rightModule, const uint16_t pattern[8])
{
  for (int row = 0; row < 8; row++)
  {
    uint8_t leftPart = (pattern[row] >> 8) & 0xFF;
    uint8_t rightPart = pattern[row] & 0xFF;

    for (int col = 0; col < 8; col++)
    {
      bool pxLeft = bitRead(leftPart, 7 - col);
      bool pxRight = bitRead(rightPart, 7 - col);

      mx.setPoint(row, leftModule * 8 + col, pxLeft);
      mx.setPoint(row, rightModule * 8 + col, pxRight);
    }
  }
}

void showFace(const uint8_t leftEye[8], const uint8_t rightEye[8], const uint16_t mouth[8])
{
  mx.clear();
  draw8x8(TOP_LEFT, leftEye);
  draw8x8(TOP_RIGHT, rightEye);
  draw16x8(BOTTOM_LEFT, BOTTOM_RIGHT, mouth);
  mx.update();
}

int hexValue(char value)
{
  if (value >= '0' && value <= '9') return value - '0';
  if (value >= 'a' && value <= 'f') return 10 + value - 'a';
  if (value >= 'A' && value <= 'F') return 10 + value - 'A';
  return -1;
}

bool showWebGrid(const String &hexFrame)
{
  if (hexFrame.length() != 64) return false;

  mx.clear();
  for (uint16_t pixel = 0; pixel < 256; pixel++)
  {
    int nibble = hexValue(hexFrame.charAt(pixel / 4));
    if (nibble < 0) return false;

    bool on = nibble & (1 << (3 - (pixel % 4)));
    uint8_t logicalRow = pixel / 16;
    uint8_t logicalCol = pixel % 16;
    bool top = logicalRow < 8;
    bool left = logicalCol < 8;
    uint8_t module = top ? (left ? TOP_LEFT : TOP_RIGHT)
                         : (left ? BOTTOM_LEFT : BOTTOM_RIGHT);
    mx.setPoint(logicalRow % 8, module * 8 + (logicalCol % 8), on);
  }

  mx.update();
  currentDisplayMode = MODE_FACE;
  customFaceActive = true;
  return true;
}

void updateFaceAnimation()
{
  if (currentDisplayMode != MODE_FACE || customFaceActive || millis() - lastFaceUpdate < 250)
  {
    return;
  }
  lastFaceUpdate = millis();

  switch (faceState)
  {
  case 0:
    showFace(EYE_OPEN, EYE_OPEN, MOUTH_SMILE);
    break;
  case 1:
    showFace(EYE_LEFT, EYE_LEFT, MOUTH_SMILE);
    break;
  case 2:
    showFace(EYE_OPEN, EYE_OPEN, MOUTH_BIGSMILE);
    break;
  case 3:
    showFace(EYE_RIGHT, EYE_RIGHT, MOUTH_SMILE);
    break;
  case 4:
    showFace(EYE_OPEN, EYE_OPEN, MOUTH_TALK1);
    break;
  case 5:
    showFace(EYE_OPEN, EYE_OPEN, MOUTH_TALK2);
    break;
  case 6:
    showFace(EYE_OPEN, EYE_OPEN, MOUTH_TALK1);
    break;
  case 7:
    showFace(EYE_HALF, EYE_HALF, MOUTH_SMILE);
    break;
  case 8:
    showFace(EYE_CLOSED, EYE_CLOSED, MOUTH_SMILE);
    break;
  case 9:
    showFace(EYE_HALF, EYE_HALF, MOUTH_SMILE);
    break;
  }

  faceState++;
  if (faceState > 9)
  {
    faceState = 0;
  }
}

// ====================== TEXT FUNCTIONS ======================
int16_t computeLoopResetPoint(const String &message)
{
  int16_t width = DISPLAY_COLUMNS;
  uint8_t buf[8];

  for (size_t i = 0; i < message.length(); i++)
  {
    uint8_t c = (uint8_t)message.charAt(i);
    width += mx.getChar(c, sizeof(buf), buf) + 1;
  }

  return width;
}

uint8_t reverseBits8(uint8_t value)
{
  value = ((value & 0xF0) >> 4) | ((value & 0x0F) << 4);
  value = ((value & 0xCC) >> 2) | ((value & 0x33) << 2);
  value = ((value & 0xAA) >> 1) | ((value & 0x55) << 1);
  return value;
}

void drawColumnMapped(int16_t logicalX, uint8_t data)
{
  if (logicalX < 0 || logicalX >= DISPLAY_COLUMNS)
  {
    return;
  }

  int16_t physicalX = MATRIX_MIRROR_H ? (DISPLAY_COLUMNS - 1 - logicalX) : logicalX;
  uint8_t physicalData = MATRIX_FLIP_V ? reverseBits8(data) : data;
  mx.setColumn(physicalX, physicalData);
}

void setMessage(String newMessage)
{
  newMessage.trim();
  if (newMessage.length() == 0)
  {
    newMessage = " ";
  }

  currentMessage = newMessage;
  scrollOffset = 0;
  loopResetPoint = computeLoopResetPoint(currentMessage);

  Serial.print("New message: ");
  Serial.println(currentMessage);
}

void renderScrollingText()
{
  if (currentDisplayMode != MODE_TEXT || millis() - lastScrollTick < SCROLL_DELAY_MS)
  {
    return;
  }
  lastScrollTick = millis();

  mx.clear();

  int16_t x = DISPLAY_COLUMNS - scrollOffset;
  uint8_t buf[8];

  for (size_t i = 0; i < currentMessage.length(); i++)
  {
    uint8_t c = (uint8_t)currentMessage.charAt(i);
    uint8_t charWidth = mx.getChar(c, sizeof(buf), buf);

    for (uint8_t col = 0; col < charWidth; col++)
    {
      drawColumnMapped(x + col, buf[col]);
    }

    x += charWidth;
    if (i < currentMessage.length() - 1)
    {
      drawColumnMapped(x, 0x00);
      x++;
    }
  }

  mx.update();

  scrollOffset++;
  if (scrollOffset > loopResetPoint)
  {
    scrollOffset = 0;
  }
}

void showIpWhenClientConnects()
{
  bool clientConnected = WiFi.softAPgetStationNum() > 0;
  if (clientConnected && !wifiClientWasConnected)
  {
    String ipMessage = "IP " + WiFi.softAPIP().toString();
    customFaceActive = false;
    setMessage(ipMessage);
    currentDisplayMode = MODE_TEXT;
    Serial.println("WiFi client connected - displaying " + ipMessage);
  }
  wifiClientWasConnected = clientConnected;
}

// ====================== WEB HANDLERS ======================
void handleSetText()
{
  String incoming = server.arg("plain");
  if (incoming.length() == 0)
  {
    incoming = server.arg("msg");
  }

  setMessage(incoming);
  currentDisplayMode = MODE_TEXT;
  server.send(200, "text/plain", currentMessage);
}

void setupRoutes()
{
  server.on("/", HTTP_GET, []()
            {
              currentDisplayMode = MODE_FACE;
              customFaceActive = false;
              faceState = 0;
              lastFaceUpdate = 0;
              server.send_P(200, "text/html; charset=utf-8", WEBPAGE);
            });

  server.on("/status", HTTP_GET, []()
            {
              server.send(200, "text/plain", "ESP32 car ready");
            });

  server.on("/api/move", HTTP_GET, []()
            {
              stopProgram();
              String direction = server.arg("dir");
              uint8_t speed = constrain(server.arg("speed").toInt(), 0, 255);
              if (direction == "forward") setDriveModeWithSpeed(DRIVE_FORWARD, speed);
              else if (direction == "back" || direction == "backward") setDriveModeWithSpeed(DRIVE_BACKWARD, speed);
              else if (direction == "left") setDriveModeWithSpeed(DRIVE_LEFT, speed);
              else if (direction == "right") setDriveModeWithSpeed(DRIVE_RIGHT, speed);
              else setDriveModeWithSpeed(DRIVE_STOPPED, 0);
              server.send(200, "text/plain", "move: " + direction);
            });

  server.on("/api/face", HTTP_GET, []()
            {
              String name = server.arg("name");
              String frame = server.arg("frame");
              if (!showWebGrid(frame))
              {
                customFaceActive = false;
                currentDisplayMode = MODE_FACE;
                faceState = 0;
                lastFaceUpdate = 0;
              }
              server.send(200, "text/plain", "face: " + name);
            });

  server.on("/api/text", HTTP_GET, []()
            {
              setMessage(server.arg("value"));
              customFaceActive = false;
              currentDisplayMode = MODE_TEXT;
              server.send(200, "text/plain", currentMessage);
            });

  server.on("/api/mode", HTTP_GET, []()
            {
              String mode = server.arg("value");
              customFaceActive = false;
              currentDisplayMode = mode == "text" ? MODE_TEXT : MODE_FACE;
              if (currentDisplayMode == MODE_FACE)
              {
                faceState = 0;
                lastFaceUpdate = 0;
              }
              server.send(200, "text/plain", "mode: " + mode);
            });

  server.on("/api/aux", HTTP_GET, []()
            {
              String name = server.arg("name");
              if (name == "obstacle")
              {
                obstacleDetectionEnabled = server.arg("on") == "1";
                if (!obstacleDetectionEnabled) avoidanceStep = AVOID_IDLE;
                server.send(200, "text/plain", obstacleDetectionEnabled ? "obstacle detection on" : "obstacle detection off");
                return;
              }
              server.send(200, "text/plain", "aux hardware not configured");
            });

  server.on("/legacy", HTTP_GET, []()
            {
              server.send(200, "text/html", buildConnectedPage());
            });

  server.on("/wifi-status", HTTP_GET, []()
            {
              server.send(200, "text/plain", WiFi.softAPgetStationNum() > 0 ? "connected" : "waiting");
            });

  server.on("/connected", HTTP_GET, []()
            {
              currentDisplayMode = MODE_SELECT;
              mx.clear();
              mx.update();
              server.send(200, "text/html", buildConnectedPage());
            });

  server.on("/face", HTTP_GET, []()
            {
              currentDisplayMode = MODE_FACE;
              customFaceActive = false;
              faceState = 0;
              lastFaceUpdate = 0;
              server.send(200, "text/html", buildFacePage());
            });

  server.on("/text", HTTP_GET, []()
            {
              currentDisplayMode = MODE_TEXT;
              setMessage(currentMessage);
              server.send(200, "text/html", buildTextPage());
            });

  server.on("/blocks", HTTP_GET, []()
            {
              server.send(200, "text/html", buildBlockPage());
            });

  server.on("/setText", HTTP_POST, handleSetText);
  server.on("/setText", HTTP_GET, handleSetText);

  server.on("/program/run", HTTP_POST, []()
            {
              if (!parseProgramPayload(server.arg("plain")))
              {
                server.send(400, "text/plain", "Program not valid");
                return;
              }

              startProgram();
              server.send(200, "text/plain", "Program started: " + String(programStepCount) + " blocks");
            });

  server.on("/program/stop", HTTP_GET, []()
            {
              stopProgram();
              server.send(200, "text/plain", "Program stopped");
            });

  server.on("/car/forward", HTTP_GET, []()
            {
              stopProgram();
              currentDriveSpeed = DEFAULT_DRIVE_SPEED;
              setDriveMode(DRIVE_FORWARD);
              server.send(200, "text/plain", "Moving Forward");
            });

  server.on("/car/backward", HTTP_GET, []()
            {
              stopProgram();
              currentDriveSpeed = DEFAULT_DRIVE_SPEED;
              setDriveMode(DRIVE_BACKWARD);
              server.send(200, "text/plain", "Moving Backward");
            });

  server.on("/car/left", HTTP_GET, []()
            {
              stopProgram();
              currentDriveSpeed = DEFAULT_DRIVE_SPEED;
              setDriveMode(DRIVE_LEFT);
              server.send(200, "text/plain", "Turning Left");
            });

  server.on("/car/right", HTTP_GET, []()
            {
              stopProgram();
              currentDriveSpeed = DEFAULT_DRIVE_SPEED;
              setDriveMode(DRIVE_RIGHT);
              server.send(200, "text/plain", "Turning Right");
            });

  server.on("/car/stop", HTTP_GET, []()
            {
              stopProgram();
              setDriveMode(DRIVE_STOPPED);
              server.send(200, "text/plain", "Stopped");
            });

  server.on("/obstacle/on", HTTP_GET, []()
            {
              obstacleDetectionEnabled = true;
              server.send(200, "text/plain", "Obstacle Detection ON");
            });

  server.on("/obstacle/off", HTTP_GET, []()
            {
              obstacleDetectionEnabled = false;
              avoidanceStep = AVOID_IDLE;
              setDriveMode(currentDriveMode);
              server.send(200, "text/plain", "Obstacle Detection OFF");
            });

  server.on("/obstacle/status", HTTP_GET, []()
            {
              String status = obstacleDetectionEnabled ? "1" : "0";
              status += ",";
              status += isObstacleDetected() ? "1" : "0";
              server.send(200, "text/plain", status);
            });
}

// ====================== SETUP ======================
void setup()
{
  Serial.begin(115200);

  pinMode(IN1, OUTPUT);
  pinMode(IN2, OUTPUT);
  pinMode(IN3, OUTPUT);
  pinMode(IN4, OUTPUT);
  ledcAttach(ENA, 1200, 8);
  ledcAttach(ENB, 1200, 8);
  pinMode(IR_SENSOR_PIN, INPUT_PULLUP);
  stopCar();

  mx.begin();
  mx.control(MD_MAX72XX::INTENSITY, 8);
  mx.clear();
  mx.update();
  setMessage(currentMessage);

  WiFi.softAP(ssid, password);
  Serial.println("WiFi Hotspot Started");
  Serial.print("IP Address: ");
  Serial.println(WiFi.softAPIP());

  setupRoutes();
  server.begin();
  Serial.println("Web Server Started");
}

// ====================== LOOP ======================
void loop()
{
  server.handleClient();
  showIpWhenClientConnects();
  updateProgramRunner();
  handleObstacleAvoidance();
  updateFaceAnimation();
  renderScrollingText();
}
