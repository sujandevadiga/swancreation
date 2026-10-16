// ============================================================
// AI Grade Quiz – ESP32 DevKit v1.2
// Audio : built-in 8-bit DAC on GPIO25  →  PAM8403 amp
// TFT   : ST7796 240x320 panel, driven via Adafruit_ST7789 class
//         on hardware SPI (safe DevKit pins)
// Libs  : ESP8266Audio (Earle Philhower), Adafruit ST7789/GFX
//         ArduinoJson, LittleFS
// Partition: "No OTA (2MB APP / 2MB SPIFFS)" in Arduino IDE
//            so LittleFS has enough room for all MP3 clips
// v1.3 : pins corrected to CS=27 DC=33 RST=32 (confirmed working
//        wiring); MADCTL changed to 0x20 (MV only, BGR bit
//        cleared) after colour-bar test showed RED/BLUE and
//        CYAN/YELLOW swapped with the BGR bit set.
//        NOTE: this panel is actually ST7796, not ST7789 --
//        Adafruit_ST7789::init() is close enough for most panels
//        but if colours look washed out or contrast is off,
//        switch to the Adafruit_ST7796S_kbv library instead.
// ============================================================

#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include <DNSServer.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <Preferences.h>
#include <ArduinoJson.h>
#include <SPI.h>
#include <Adafruit_GFX.h>
#include <Adafruit_ST7789.h>
#include <LittleFS.h>
#include "AudioGeneratorMP3.h"
#include "AudioFileSourceLittleFS.h"
#include "AudioOutputInternalDAC.h"

// ── Layout constants (were missing in original) ──────────────
#define HEADER_H               36
#define FOOTER_H               30
#define QUESTION_HISTORY_LIMIT 10
#define MAX_CHAPTER_OPTIONS    20
#define GEMINI_MAX_RETRIES      2

// ── WiFi / Gemini ────────────────────────────────────────────
const char    *WIFI_SSID             = "";
const char    *WIFI_PASS             = "";
const char    *WIFI_PREF_NAMESPACE   = "examwifi";
const char    *WIFI_SETUP_AP_SSID    = "AI_TEACHER_SETUP";
const uint8_t  WIFI_SETUP_DNS_PORT   = 53;
const char    *GEMINI_API_KEY        = "YOUR_GEMINI_API_KEY";
const char    *GEMINI_MODEL          = "gemini-2.5-flash-lite";

// ── Local MP3 clips ──────────────────────────────────────────
const char    *WELCOME_CLIP     = "/welcome.mp3";
const uint8_t  VOICE_CLIP_COUNT = 20;

// ── TFT pins – CONFIRMED WORKING WIRING ───────────────────────
#define TFT_CS    12
#define TFT_DC    33
#define TFT_RST   32
#define TFT_MOSI  23
#define TFT_MISO  -1
#define TFT_SCLK  18
#define TFT_BL   -1

// ── Buttons (active LOW via INPUT_PULLUP) ─────────────────────
#define NEXT_BUTTON_PIN    4
#define SELECT_BUTTON_PIN  5
#define HOME_BUTTON_PIN    19
#define BUTTON_DEBOUNCE_MS 220

// ── Screen ───────────────────────────────────────────────────
#define SCREEN_W          320
#define SCREEN_H          240
#define WIFI_SAVED_RETRY_MS 30000UL

// ── Objects ──────────────────────────────────────────────────
Adafruit_ST7789 tft(TFT_CS, TFT_DC, TFT_RST);
Preferences     wifiPrefs;
DNSServer       dnsServer;
WebServer       setupServer(80);
IPAddress       setupPortalIp(192, 168, 4, 1);

// ESP8266Audio objects – created on heap, deleted after each clip
AudioGeneratorMP3       *mp3gen   = nullptr;
AudioFileSourceLittleFS *audioSrc = nullptr;
AudioOutputInternalDAC  *audioOut = nullptr;

// ── App state ────────────────────────────────────────────────
enum AppState {
  STATE_SELECT_BOARD,
  STATE_SELECT_CLASS,
  STATE_SELECT_SUBJECT,
  STATE_SELECT_CHAPTER,
  STATE_QUESTION,
  STATE_RESULT,
  STATE_SCORE,
  STATE_ERROR
};

struct QuestionData {
  String question;
  String options[4];
  int    correctIndex = 0;
  String explanation;
};

AppState    state         = STATE_SELECT_BOARD;
String      studentName   = "Student";
String      boardName;
String      studentClass;
String      subject;
String      chapter;
QuestionData currentQuestion;
String      askedQuestions[QUESTION_HISTORY_LIMIT];
int         questionNo    = 0;
int         score         = 0;
String      lastError;
String      apiErrorDetail;
int         boardIndex    = 0;
int         classIndex    = 0;
int         subjectIndex  = 0;
int         chapterIndex  = 0;
int         selectedAnswerIndex  = 0;
int         lastSelectedIndex    = -1;
bool        lastAnswerCorrect    = false;
unsigned long lastSetupButtonMs  = 0;
bool        wifiPortalSubmitted  = false;
bool        wifiRetryRequested   = false;
String      submittedWifiSsid;
String      submittedWifiPass;

const char *boardOptions[] = {
  "CBSE",
  "ICSE",
  "Karnataka State Board",
  "Maharashtra State Board",
  "Tamil Nadu State Board"
};
const int boardOptionCount = sizeof(boardOptions) / sizeof(boardOptions[0]);

const int classOptions[]    = {2, 3, 4, 5, 6, 7, 8, 9, 10};
const int classOptionCount  = sizeof(classOptions) / sizeof(classOptions[0]);

const char *primarySubjects[] = {"English","Maths","EVS","GK","Computer"};
const char *middleSubjects[]  = {"English","Maths","Science","Social","Computer","GK"};
const char *highSubjects[]    = {"English","Maths","Physics","Chemistry","Biology","History","Geography","Computer"};

String chapterOptions[MAX_CHAPTER_OPTIONS];
int chapterOptionCount = 0;

uint16_t bg      = 0x0083;
uint16_t panel   = 0x1166;
uint16_t cyan    = 0x07FF;
uint16_t yellow  = ST77XX_YELLOW;
uint16_t ink     = ST77XX_WHITE;
uint16_t muted   = 0xBDF7;
uint16_t accent  = 0x05FF;
uint16_t success = 0x05E8;
uint16_t danger  = 0xD1A6;

void drawCurrentState();
void showLogoSplash();
bool verifyVoiceFiles();

// ── Colour helpers ───────────────────────────────────────────
uint16_t rgb565(uint8_t r, uint8_t g, uint8_t b) {
  return ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3);
}

uint16_t blendColor(uint16_t c1, uint16_t c2, uint8_t amount) {
  uint8_t r1=(c1>>11)&0x1F, g1=(c1>>5)&0x3F, b1=c1&0x1F;
  uint8_t r2=(c2>>11)&0x1F, g2=(c2>>5)&0x3F, b2=c2&0x1F;
  return (((r1*(255-amount)+r2*amount)/255)<<11) |
         (((g1*(255-amount)+g2*amount)/255)<<5)  |
         ((b1*(255-amount)+b2*amount)/255);
}

String shortText(const String &text, uint8_t maxChars) {
  if (text.length() <= maxChars) return text;
  return text.substring(0, maxChars - 1) + ".";
}

// ── Drawing helpers ──────────────────────────────────────────
void drawBackground() {
  uint16_t top    = rgb565(6, 13, 22);
  uint16_t bottom = rgb565(12, 34, 42);
  for (int y = 0; y < SCREEN_H; y++)
    tft.drawFastHLine(0, y, SCREEN_W, blendColor(top, bottom, map(y, 0, SCREEN_H-1, 0, 255)));
  for (int x = 0; x < SCREEN_W; x += 32)
    tft.drawFastVLine(x, HEADER_H, SCREEN_H-HEADER_H-FOOTER_H, rgb565(10, 35, 45));
  tft.fillCircle(278, 56, 22, blendColor(accent, ST77XX_BLACK, 155));
}

void drawButtonFooter(const String &left, const String &right, uint16_t lc, uint16_t rc) {
  int16_t y = SCREEN_H - FOOTER_H;
  int16_t w = (SCREEN_W - 18) / 2;
  tft.fillRect(0, y, SCREEN_W, FOOTER_H, rgb565(8, 18, 24));
  tft.fillRoundRect(6,     y+3, w, FOOTER_H-6, 4, lc);
  tft.fillRoundRect(12+w,  y+3, w, FOOTER_H-6, 4, rc);
  tft.setTextSize(1);
  tft.setTextColor(ST77XX_WHITE, lc); tft.setCursor(18, y+10);   tft.print(left);
  tft.setTextColor(ST77XX_WHITE, rc); tft.setCursor(24+w, y+10); tft.print(right);
}

void drawProgressStrip(uint8_t current, uint8_t total) {
  int16_t x=12, y=HEADER_H+6, w=SCREEN_W-24;
  tft.fillRoundRect(x, y, w, 7, 3, rgb565(20, 35, 43));
  int16_t fw = map(constrain(current,0,total), 0, total, 0, w);
  if (fw > 0) tft.fillRoundRect(x, y, fw, 7, 3, accent);
}

void drawHeader(const String &title) {
  drawBackground();
  uint16_t header = rgb565(14, 24, 34);
  tft.fillRect(0, 0, SCREEN_W, HEADER_H, header);
  tft.drawFastHLine(0, HEADER_H-1, SCREEN_W, accent);
  bool connected = WiFi.status() == WL_CONNECTED;
  uint16_t wbg = connected ? rgb565(8,78,48) : rgb565(75,32,34);
  tft.fillRoundRect(250, 7, 58, 20, 5, wbg);
  tft.fillCircle(262, 17, 4, connected ? success : danger);
  tft.setTextColor(muted, wbg); tft.setTextSize(1); tft.setCursor(271, 13);
  tft.print(connected ? "ONLINE" : "WIFI");
  tft.setTextColor(ST77XX_WHITE, header); tft.setTextSize(2); tft.setCursor(10, 9);
  tft.print(shortText(title, 18));
}

void drawFooter(const String &text) {
  int16_t fy = SCREEN_H - FOOTER_H;
  tft.fillRect(0, fy, SCREEN_W, FOOTER_H, rgb565(8, 18, 24));
  tft.drawFastHLine(0, fy, SCREEN_W, rgb565(45, 72, 84));
  tft.setTextColor(yellow, rgb565(8,18,24)); tft.setTextSize(1); tft.setCursor(8, fy+9);
  tft.print(shortText(text, 50));
}

void printWrapped(const String &text, int16_t x, int16_t y, uint8_t sz, uint8_t maxChars, uint16_t color, uint16_t back) {
  tft.setTextSize(sz); tft.setTextColor(color, back);
  int16_t cx=x, cy=y;
  String word;
  for (uint16_t i = 0; i <= text.length(); i++) {
    char c = i < text.length() ? text[i] : ' ';
    if (c == '\n') c = ' ';
    if (c == ' ' || i == text.length()) {
      if (word.length() > 0) {
        int16_t step = 8*sz+4;
        if ((int)word.length() > maxChars) {
          if (cx != x) { cx=x; cy+=step; }
          while ((int)word.length() > maxChars) {
            tft.setCursor(cx, cy); tft.print(word.substring(0, maxChars-1)); tft.print("-");
            word = word.substring(maxChars-1); cy += step;
          }
        }
        if ((cx-x)/(6*sz)+(int)word.length() > maxChars) { cx=x; cy+=step; }
        tft.setCursor(cx, cy); tft.print(word);
        cx += (word.length()+1)*6*sz;
        word = "";
      }
    } else { word += c; }
  }
}

void drawMessage(const String &title, const String &line1, const String &line2 = "") {
  drawHeader(title);
  tft.fillRoundRect(18, 62, 284, 106, 7, panel);
  tft.drawRoundRect(18, 62, 284, 106, 7, accent);
  int16_t margin=30, chars2=max(10,(SCREEN_W-margin*2)/12), chars1=max(18,(SCREEN_W-margin*2)/6);
  printWrapped(line1, margin, 82, 2, chars2, ST77XX_WHITE, panel);
  if (line2.length()) printWrapped(line2, margin, 126, 1, chars1, muted, panel);
}

// ── Subject helpers ──────────────────────────────────────────
void getSubjectList(const char ***subjects, int *count) {
  int cls = classOptions[classIndex];
  if (cls <= 5)      { *subjects = primarySubjects; *count = 5; }
  else if (cls <= 8) { *subjects = middleSubjects;  *count = 6; }
  else               { *subjects = highSubjects;    *count = 8; }
}

// ── Button debounce ──────────────────────────────────────────
bool setupButtonPressed(uint8_t pin) {
  if (digitalRead(pin) == HIGH) return false;
  if (millis() - lastSetupButtonMs < BUTTON_DEBOUNCE_MS) return false;
  delay(20);
  if (digitalRead(pin) == HIGH) return false;
  lastSetupButtonMs = millis();
  return true;
}

// ── Splash ───────────────────────────────────────────────────
void showLogoSplash() {
  File logo = LittleFS.open("/logo.rgb565", "r");
  if (!logo || logo.size() != (uint32_t)SCREEN_W * SCREEN_H * 2) {
    Serial.println("[LOGO] Missing /logo.rgb565. Upload the data folder.");
    drawMessage("AI Teacher", "Press any button", "Logo file missing");
  } else {
    uint16_t row[SCREEN_W];
    for (int y = 0; y < SCREEN_H; y++) {
      if (logo.read((uint8_t *)row, SCREEN_W*2) != SCREEN_W*2) break;
      tft.drawRGBBitmap(0, y, row, SCREEN_W, 1);
    }
    logo.close();
  }
  tft.fillRect(0, SCREEN_H-28, SCREEN_W, 28, rgb565(6,13,22));
  tft.drawFastHLine(0, SCREEN_H-29, SCREEN_W, rgb565(80,220,235));
  tft.setTextSize(1); tft.setTextColor(ST77XX_WHITE, rgb565(6,13,22));
  tft.setCursor(52, SCREEN_H-18); tft.print("Press any button to start");
  while (digitalRead(NEXT_BUTTON_PIN)==HIGH && digitalRead(SELECT_BUTTON_PIN)==HIGH) delay(20);
  delay(60);
  while (digitalRead(NEXT_BUTTON_PIN)==LOW || digitalRead(SELECT_BUTTON_PIN)==LOW) delay(20);
  lastSetupButtonMs = millis();
}

// ── Screen draw states ───────────────────────────────────────
void drawSetupSelect(const String &title, const String &value, const String &hint) {
  drawHeader(title);
  tft.setTextSize(1); tft.setTextColor(muted, bg); tft.setCursor(18, 48); tft.print("SELECTED PROFILE");
  int16_t boxX=18, boxY=66, boxW=SCREEN_W-36, boxH=92;
  tft.fillRoundRect(boxX, boxY, boxW, boxH, 7, panel);
  tft.drawRoundRect(boxX, boxY, boxW, boxH, 7, accent);
  tft.drawFastHLine(boxX+12, boxY+12, 78, yellow);
  tft.setTextColor(ST77XX_WHITE, panel);
  if (value.length() <= 10) {
    tft.setTextSize(4);
    tft.setCursor(boxX + max(10,(boxW-(int)value.length()*24)/2), boxY+34);
    tft.print(value);
  } else if (value.length() <= 22) {
    tft.setTextSize(2);
    tft.setCursor(boxX + max(10,(boxW-(int)value.length()*12)/2), boxY+38);
    tft.print(value);
  } else {
    int16_t maxChars = max(14,(boxW-28)/12);
    printWrapped(shortText(value, maxChars*2), boxX+14, boxY+26, 2, maxChars, ST77XX_WHITE, panel);
  }
  printWrapped(hint, 18, boxY+boxH+16, 1, 47, muted, bg);
  drawButtonFooter("NEXT  Change","SELECT  Confirm", rgb565(19,82,108), rgb565(18,105,70));
}

void drawClassSelect() {
  drawSetupSelect("Select Grade","Grade "+String(classOptions[classIndex]),
                  "Board: "+boardName+" | NEXT changes grade");
}

void drawBoardSelect() {
  boardIndex = constrain(boardIndex, 0, boardOptionCount-1);
  drawSetupSelect("Select Board", String(boardOptions[boardIndex]),
                  "NEXT changes board | SELECT confirms");
}

void drawSubjectSelect() {
  const char **subjects; int count;
  getSubjectList(&subjects, &count);
  subjectIndex = constrain(subjectIndex, 0, count-1);
  drawSetupSelect("Choose Subject", String(subjects[subjectIndex]),
                  boardName+" Grade "+String(classOptions[classIndex]));
}

void drawChapterSelect() {
  if (chapterOptionCount <= 0) {
    drawMessage("No Chapters", "Chapter list empty", "SELECT goes back");
    return;
  }
  chapterIndex = constrain(chapterIndex, 0, chapterOptionCount-1);
  drawSetupSelect("Choose Chapter", chapterOptions[chapterIndex],
                  subject+" | "+boardName+" Grade "+studentClass);
}

void drawQuestion() {
  drawHeader(subject+" Q"+String(questionNo+1));
  drawProgressStrip((questionNo%10)+1, 10);
  tft.setTextSize(1); tft.setTextColor(muted, bg);
  tft.setCursor(12, 46); tft.print(boardName); tft.print(" G"); tft.print(studentClass);
  tft.print(" "); tft.print(shortText(chapter, 10));
  tft.setCursor(224,46); tft.print("SCORE "); tft.print(score); tft.print("/"); tft.print(questionNo);
  int16_t margin=10, maxChars=max(18,(SCREEN_W-margin*2-14)/6);
  tft.fillRoundRect(8,54,304,42,6,rgb565(12,30,40));
  tft.drawRoundRect(8,54,304,42,6,rgb565(36,84,100));
  printWrapped(currentQuestion.question,16,64,1,maxChars,ST77XX_WHITE,rgb565(12,30,40));
  int16_t optionY=102, optionW=SCREEN_W-margin*2, optionH=24, optionGap=5;
  for (int i=0; i<4; i++) {
    int16_t y=optionY+i*(optionH+optionGap);
    bool sel=(i==selectedAnswerIndex);
    uint16_t fill=sel?rgb565(177,113,24):panel, outline=sel?yellow:rgb565(44,75,88);
    tft.fillRoundRect(margin,y,optionW,optionH,5,fill);
    tft.drawRoundRect(margin,y,optionW,optionH,5,outline);
    tft.fillCircle(margin+15,y+12,9,sel?yellow:rgb565(35,61,72));
    tft.setTextColor(ST77XX_WHITE,fill); tft.setTextSize(1);
    tft.setCursor(margin+12,y+9); tft.print((char)('A'+i));
    tft.setCursor(margin+32,y+9); tft.print(". "); tft.print(shortText(currentQuestion.options[i],maxChars-6));
  }
  drawButtonFooter("NEXT  Choose","SELECT  Answer",rgb565(19,82,108),rgb565(18,105,70));
}

void drawResult(bool correct, int selected) {
  drawHeader(correct?"Correct":"Learn This");
  drawProgressStrip(questionNo%10,10);
  uint16_t statusColor = correct?success:yellow;
  tft.fillRoundRect(12,48,102,28,6,blendColor(statusColor,ST77XX_BLACK,70));
  tft.drawRoundRect(12,48,102,28,6,statusColor);
  tft.setTextSize(2); tft.setTextColor(ST77XX_WHITE,blendColor(statusColor,ST77XX_BLACK,70));
  tft.setCursor(22,55); tft.print(correct?"GREAT":"LEARN");
  String ans=String((char)('A'+currentQuestion.correctIndex))+". "+currentQuestion.options[currentQuestion.correctIndex];
  int16_t maxChars=max(18,(SCREEN_W-34)/6);
  tft.fillRoundRect(12,84,296,44,6,panel);
  tft.drawRoundRect(12,84,296,44,6,accent);
  printWrapped(ans,22,96,1,maxChars,ST77XX_WHITE,panel);
  tft.fillRoundRect(12,134,296,42,6,rgb565(12,30,40));
  printWrapped(currentQuestion.explanation,22,144,1,maxChars,muted,rgb565(12,30,40));
  String picked=selected>=0?String((char)('A'+selected)):"?";
  tft.setTextSize(1); tft.setTextColor(muted,bg); tft.setCursor(122,57);
  tft.print("PICKED "); tft.print(picked);
  tft.print("   SCORE "); tft.print(score); tft.print("/"); tft.print(questionNo);
  drawButtonFooter("NEXT  Question","SELECT  End",rgb565(20,105,45),rgb565(130,35,35));
}

void drawScore() {
  drawHeader("Final Score");
  tft.fillRoundRect(44,54,232,102,8,panel);
  tft.drawRoundRect(44,54,232,102,8,accent);
  tft.setTextColor(ST77XX_WHITE,panel); tft.setTextSize(5); tft.setCursor(88,82);
  tft.print(score); tft.print("/"); tft.print(questionNo);
  String summary = score*100/max(1,questionNo)>=70?"Strong performance":"Keep practicing";
  printWrapped(summary,64,166,2,18,muted,bg);
  drawButtonFooter("NEXT  New Test","SELECT  New Test",rgb565(19,82,108),rgb565(18,105,70));
}

void updateSelectBox(const String &value) {
  const int16_t boxX=18, boxY=66, boxW=SCREEN_W-36, boxH=92;
  GFXcanvas16 canvas(boxW, boxH);
  canvas.fillScreen(panel);
  canvas.drawRoundRect(0,0,boxW,boxH,7,accent);
  canvas.drawFastHLine(12,12,78,yellow);
  canvas.setTextColor(ST77XX_WHITE);
  if (value.length() <= 10) {
    canvas.setTextSize(4);
    canvas.setCursor(max(10,(boxW-(int)value.length()*24)/2), 34);
    canvas.print(value);
  } else if (value.length() <= 22) {
    canvas.setTextSize(2);
    canvas.setCursor(max(10,(boxW-(int)value.length()*12)/2), 38);
    canvas.print(value);
  } else {
    canvas.setTextSize(2);
    int16_t mc = max(14,(boxW-28)/12);
    canvas.setCursor(14, 26);
    canvas.print(shortText(value, mc*2));
  }
  tft.drawRGBBitmap(boxX, boxY, canvas.getBuffer(), boxW, boxH);
}

void drawOneOptionRow(int i) {
  const int16_t margin=10, optionW=SCREEN_W-margin*2, optionH=24, optionGap=5;
  const int16_t maxChars=max(18,(SCREEN_W-margin*2-14)/6);
  int16_t y = 102 + i*(optionH+optionGap);
  bool sel = (i==selectedAnswerIndex);
  uint16_t fill    = sel ? rgb565(177,113,24) : panel;
  uint16_t outline = sel ? yellow             : rgb565(44,75,88);
  uint16_t circCol = sel ? yellow             : rgb565(35,61,72);
  GFXcanvas16 canvas(optionW, optionH);
  canvas.fillScreen(fill);
  canvas.fillRoundRect(0,0,optionW,optionH,5,fill);
  canvas.drawRoundRect(0,0,optionW,optionH,5,outline);
  canvas.fillCircle(15,12,9,circCol);
  canvas.setTextColor(ST77XX_WHITE); canvas.setTextSize(1);
  canvas.setCursor(12,9); canvas.print((char)('A'+i));
  canvas.setCursor(32,9); canvas.print(". ");
  canvas.print(shortText(currentQuestion.options[i], maxChars-6));
  tft.drawRGBBitmap(margin, y, canvas.getBuffer(), optionW, optionH);
}

void updateQuestionOptions() {
  for (int i=0; i<4; i++) drawOneOptionRow(i);
}

void drawCurrentState() {
  if      (state==STATE_SELECT_BOARD)   drawBoardSelect();
  else if (state==STATE_SELECT_CLASS)   drawClassSelect();
  else if (state==STATE_SELECT_SUBJECT) drawSubjectSelect();
  else if (state==STATE_SELECT_CHAPTER) drawChapterSelect();
  else if (state==STATE_QUESTION)       drawQuestion();
  else if (state==STATE_SCORE)          drawScore();
  else if (state==STATE_ERROR)          drawMessage("AI Teacher error", lastError,
    apiErrorDetail.length() ? shortText(apiErrorDetail, 50)+" | SELECT: back" : "SELECT goes back");
}

bool showQuizError(const String &message) {
  lastError = message;
  state = STATE_ERROR;
  drawCurrentState();
  Serial.println("[AI] " + lastError);
  return false;
}

// ── JSON / Gemini helpers ────────────────────────────────────
String cleanJsonText(String text) {
  text.trim();
  if (text.startsWith("```")) {
    int first=text.indexOf('{'), last=text.lastIndexOf('}');
    if (first>=0 && last>first) text=text.substring(first,last+1);
  }
  text.trim();
  return text;
}

String extractGeminiText(const String &payload) {
  DynamicJsonDocument doc(16384);
  DeserializationError error = deserializeJson(doc, payload);
  if (error) { lastError="API response JSON error"; apiErrorDetail=error.c_str(); return ""; }
  String reason = doc["candidates"][0]["finishReason"] | "";
  if (reason == "MAX_TOKENS") {
    lastError="AI response truncated"; apiErrorDetail="Output token limit reached"; return "";
  }
  String text;
  JsonArray parts=doc["candidates"][0]["content"]["parts"].as<JsonArray>();
  for (JsonObject part : parts) {
    if (!(part["thought"] | false)) text += String(part["text"] | "");
  }
  if (!text.length()) {
    lastError="No AI text returned";
    apiErrorDetail=doc["promptFeedback"]["blockReason"] | "";
    if (!apiErrorDetail.length()) apiErrorDetail=reason;
    return "";
  }
  return cleanJsonText(text);
}

String jsonEscape(const String &text) {
  String out; out.reserve(text.length()+20);
  for (uint16_t i=0; i<text.length(); i++) {
    char c=text[i];
    if (c=='"'||c=='\\') out+='\\';
    if (c=='\n'||c=='\r') out+=' '; else out+=c;
  }
  return out;
}

// ── Audio (built-in DAC via ESP8266Audio) ────────────────────
void initSpeakerAudio() {
  audioOut = new AudioOutputInternalDAC();   // port 0 = GPIO25
  audioOut->SetGain(1.0);
  Serial.println("[AUDIO] DAC on GPIO25 via DMA (AudioOutputInternalDAC)");

  if (!LittleFS.begin(false)) {
    Serial.println("[VOICE] LittleFS mount failed. Upload data folder.");
    drawMessage("Voice Error","LittleFS failed","Upload data folder");
    delay(1600);
  } else {
    Serial.printf("[VOICE] LittleFS total=%u used=%u\n",
                  (unsigned)LittleFS.totalBytes(), (unsigned)LittleFS.usedBytes());
    verifyVoiceFiles();
  }
}

bool playLocalMp3(const char *path, const String &title, const String &line1) {
  if (mp3gen) {
    if (mp3gen->isRunning()) mp3gen->stop();
    delete mp3gen;  mp3gen  = nullptr;
  }
  if (audioSrc) { delete audioSrc; audioSrc = nullptr; }
  delay(60);

  if (!LittleFS.exists(path)) {
    Serial.print("[VOICE] Missing clip: "); Serial.println(path);
    drawMessage("Voice Missing", String(path), "Upload data folder");
    delay(1200);
    return false;
  }

  drawMessage(title, line1, String(path));
  Serial.print("[VOICE] Playing: "); Serial.println(path);

  audioSrc = new AudioFileSourceLittleFS(path);
  mp3gen   = new AudioGeneratorMP3();

  if (!mp3gen->begin(audioSrc, audioOut)) {
    Serial.println("[VOICE] MP3 begin failed");
    drawMessage("Voice Error","Clip play failed",String(path));
    delete mp3gen;  mp3gen  = nullptr;
    delete audioSrc; audioSrc = nullptr;
    delay(1000);
    return false;
  }

  unsigned long start = millis();
  while (millis() - start < 8000UL) {
    if (!mp3gen->isRunning()) break;
    if (!mp3gen->loop())      break;
    yield();
  }

  if (mp3gen->isRunning()) mp3gen->stop();
  delete mp3gen;  mp3gen  = nullptr;
  delete audioSrc; audioSrc = nullptr;
  return true;
}

bool verifyVoiceFiles() {
  bool ok = true;
  const char *required[] = {"/welcome.mp3", "/c00.mp3", "/w00.mp3"};
  for (uint8_t i=0; i<3; i++) {
    File f = LittleFS.open(required[i],"r");
    if (!f) { Serial.print("[VOICE] Missing: "); Serial.println(required[i]); ok=false; continue; }
    Serial.printf("[VOICE] Found %s bytes=%u\n", required[i], (unsigned)f.size());
    f.close();
  }
  if (!ok) { drawMessage("Voice Missing","Upload LittleFS","Use the data folder"); delay(1800); }
  return ok;
}

void makeClipPath(bool correct, uint8_t index, char *out, size_t outSize) {
  snprintf(out, outSize, correct ? "/c%02u.mp3" : "/w%02u.mp3", index);
}

bool playAnswerVoice(bool correct) {
  char path[12];
  makeClipPath(correct, random(0, VOICE_CLIP_COUNT), path, sizeof(path));
  return playLocalMp3(path, "Speaking", correct ? "Correct" : "Wrong");
}

bool playWelcomeVoice() {
  return playLocalMp3(WELCOME_CLIP, "AI Teacher", "Welcome!");
}

// ── Gemini API ───────────────────────────────────────────────
String postGeminiJson(const String &body, const String &statusTitle,
                      const String &statusLine1, const String &statusLine2 = "") {
  lastError=""; apiErrorDetail="";
  if (!strlen(GEMINI_API_KEY) || String(GEMINI_API_KEY)=="YOUR_GEMINI_API_KEY") {
    lastError="API key not configured"; return "";
  }
  if (WiFi.status() != WL_CONNECTED) {
    WiFi.reconnect(); delay(1500);
    if (WiFi.status() != WL_CONNECTED) { lastError="WiFi disconnected"; return ""; }
  }
  WiFiClientSecure client;
  client.setInsecure(); // Existing sketch TLS behavior; unchanged by this diagnostic fix.
  client.setHandshakeTimeout(12); // Seconds, unlike HTTPClient timeouts below.
  String url = "https://generativelanguage.googleapis.com/v1beta/models/" +
               String(GEMINI_MODEL) + ":generateContent";
  for (int attempt=1; attempt<=GEMINI_MAX_RETRIES; attempt++) {
    String tryText="Try "+String(attempt)+"/"+String(GEMINI_MAX_RETRIES);
    drawMessage(statusTitle,statusLine1,statusLine2.length()?statusLine2+" "+tryText:tryText);
    HTTPClient http;
    http.setConnectTimeout(10000);
    http.setTimeout(20000);
    if (!http.begin(client,url)) { lastError="HTTPS start failed"; return ""; }
    http.addHeader("Content-Type","application/json");
    http.addHeader("x-goog-api-key",GEMINI_API_KEY); // Never put the key in loggable URLs.
    int code=http.POST(body);
    String payload=code>0?http.getString():"";
    http.end();
    Serial.printf("Gemini HTTP %d bytes=%u attempt=%d\n",code,(unsigned)payload.length(),attempt);
    if (code==200) {lastError="";apiErrorDetail="";return extractGeminiText(payload);}
    lastError=code>0?"Gemini HTTP "+String(code):"Network/TLS error "+String(code);
    apiErrorDetail="";
    if (code>0) {
      DynamicJsonDocument errorDoc(2048);
      if (!deserializeJson(errorDoc,payload)) {
        apiErrorDetail=errorDoc["error"]["message"] | "";
      }
    } else {apiErrorDetail=HTTPClient::errorToString(code);}
    if (strlen(GEMINI_API_KEY)) apiErrorDetail.replace(GEMINI_API_KEY,"[REDACTED]");
    apiErrorDetail=apiErrorDetail.substring(0,300);
    Serial.println("[AI] "+lastError);
    if (apiErrorDetail.length()) Serial.println("[AI] "+apiErrorDetail);
    bool temporary=(code<=0 || code==500 || code==502 || code==503 || code==504);
    if (!temporary || attempt==GEMINI_MAX_RETRIES) return "";
    drawMessage(statusTitle,"Temporary API error","Retrying once...");
    delay(1200);
  }
  return "";
}

String askGeminiText(const String &prompt, const String &statusTitle,
                     const String &statusLine1, const String &statusLine2 = "") {
  String body; body.reserve(prompt.length()+350);
  body += "{\"contents\":[{\"parts\":[{\"text\":\"";
  body += jsonEscape(prompt);
  body += "\"}]}],\"generationConfig\":{\"temperature\":0.45,\"maxOutputTokens\":1536,"
          "\"responseMimeType\":\"application/json\","
          "\"thinkingConfig\":{\"thinkingBudget\":0}}}";
  return postGeminiJson(body, statusTitle, statusLine1, statusLine2);
}

String previousQuestionsText() {
  String history;
  int count = min(questionNo, QUESTION_HISTORY_LIMIT);
  for (int i=0; i<count; i++) {
    if (askedQuestions[i].length()==0) continue;
    history += String(i+1)+". "+askedQuestions[i]+" ";
  }
  return history.length() ? history : "None.";
}

bool loadChapterList() {
  chapterOptionCount = 0;
  chapterIndex = 0;
  drawMessage("AI Teacher", "Loading chapters...", subject);

  String prompt =
    "Find the exact chapter names for "+boardName+" Grade "+studentClass+
    " subject "+subject+". Use official online textbook or syllabus sources where possible: "
    "NCERT/ePathshala for CBSE, CISCE official syllabus/prescribed texts for ICSE, "
    "and the official state textbook portal for state boards. "
    "Return only real chapter titles used by the textbook or syllabus. "
    "Do not invent, shorten, translate, or return generic names like Chapter 1. "
    "If exact chapter names are unavailable, return an empty chapters array. "
    "Return ONLY valid JSON: {\"chapters\":[\"...\"]}.";

  String json = askGeminiText(prompt, "AI Teacher", "Loading chapters...", subject);
  if (json.length()==0) return showQuizError(lastError.length()?lastError:"Chapter request failed");

  DynamicJsonDocument doc(4096);
  if (deserializeJson(doc,json)) {
    return showQuizError("Bad chapter JSON");
  }

  JsonArray chapters = doc["chapters"].as<JsonArray>();
  if (chapters.isNull()) return showQuizError("No chapter list");

  for (JsonVariant v : chapters) {
    if (chapterOptionCount >= MAX_CHAPTER_OPTIONS) break;
    if (!v.is<const char*>()) continue;
    String title = String(v.as<const char*>());
    title.trim();
    if (title.length()==0) continue;
    chapterOptions[chapterOptionCount++] = title;
  }

  if (chapterOptionCount <= 0) {
    return showQuizError("Exact chapters not found");
  }

  state=STATE_SELECT_CHAPTER;
  drawChapterSelect();
  return true;
}

bool loadQuestion() {
  drawMessage("AI Teacher","Creating question...",subject+" "+chapter);
  int qNumber = questionNo+1;
  String prompt =
    "You are a friendly AI teacher. Create ONE multiple-choice question exactly for "+boardName+
    " Grade "+studentClass+", subject "+subject+", "+chapter+". "
    "Use the normal "+boardName+" school syllabus level for this grade, subject, and chapter. "
    "Do not ask from another board, subject, chapter, or higher grade. "
    "Keep the question and options short for a 320x240 TFT display. "
    "This is question number "+String(qNumber)+". There is no fixed test limit. "
    "Start simple and make later questions slowly more challenging, but keep them age-appropriate for Grade "+studentClass+". "
    "Do not repeat or rephrase any previous question from this test. Previous questions: "+previousQuestionsText()+" "
    "The explanation must be less than 4 short display lines, maximum 28 words. "
    "Return ONLY valid JSON: {\"question\":\"...\",\"options\":[\"A\",\"B\",\"C\",\"D\"],\"answer\":0,\"explanation\":\"...\"}. "
    "answer must be 0,1,2,or 3.";

  String json = askGeminiText(prompt,"AI Teacher","Creating question...",subject+" "+chapter);
  if (json.length()==0) return showQuizError(lastError.length()?lastError:"Question request failed");

  DynamicJsonDocument doc(4096);
  if (deserializeJson(doc,json)) {
    return showQuizError("Bad question JSON");
  }

  currentQuestion.question = String(doc["question"]|"");
  for (int i=0; i<4; i++) currentQuestion.options[i]=String(doc["options"][i]|"");
  currentQuestion.correctIndex = constrain((int)(doc["answer"]|0),0,3);
  currentQuestion.explanation  = String(doc["explanation"]|"");

  if (!currentQuestion.question.length()||!currentQuestion.options[0].length()) {
    return showQuizError("Empty question");
  }

  if (questionNo < QUESTION_HISTORY_LIMIT) {
    askedQuestions[questionNo] = currentQuestion.question;
  } else {
    for (int i=1; i<QUESTION_HISTORY_LIMIT; i++) askedQuestions[i-1]=askedQuestions[i];
    askedQuestions[QUESTION_HISTORY_LIMIT-1]=currentQuestion.question;
  }
  selectedAnswerIndex=0; state=STATE_QUESTION; drawQuestion();
  return true;
}

// ── WiFi portal ──────────────────────────────────────────────
void drawWifiSetupInstructions() {
  tft.fillScreen(ST77XX_BLACK);
  tft.drawRect(0,0,SCREEN_W,SCREEN_H,ST77XX_CYAN);
  tft.drawRect(3,3,SCREEN_W-6,SCREEN_H-6,ST77XX_YELLOW);
  tft.setTextWrap(false); tft.setTextSize(2);
  tft.setTextColor(ST77XX_YELLOW,ST77XX_BLACK); tft.setCursor(14,16); tft.print("AI LENS WIFI");
  tft.setTextColor(ST77XX_WHITE,ST77XX_BLACK);  tft.setCursor(14,54); tft.print("1 Connect WiFi:");
  tft.setTextColor(ST77XX_CYAN,ST77XX_BLACK);   tft.setCursor(26,82); tft.print(WIFI_SETUP_AP_SSID);
  tft.setTextColor(ST77XX_WHITE,ST77XX_BLACK);  tft.setCursor(14,118); tft.print("2 Open browser:");
  tft.setTextColor(ST77XX_GREEN,ST77XX_BLACK);  tft.setCursor(26,146); tft.print("192.168.4.1");
  tft.setTextColor(ST77XX_WHITE,ST77XX_BLACK);
  tft.setCursor(14,184); tft.print("Enter your WiFi");
  tft.setCursor(14,210); tft.print("Then exam starts");
}

void drawWifiConnecting(const String &ssid) {
  tft.fillScreen(ST77XX_BLACK);
  tft.drawRect(0,0,SCREEN_W,SCREEN_H,ST77XX_CYAN);
  tft.setTextWrap(false); tft.setTextSize(2);
  tft.setTextColor(ST77XX_YELLOW,ST77XX_BLACK); tft.setCursor(14,46); tft.print("Connecting WiFi");
  tft.setTextColor(ST77XX_WHITE,ST77XX_BLACK);  tft.setCursor(14,88); tft.print("Network:");
  tft.setTextColor(ST77XX_CYAN,ST77XX_BLACK);   tft.setCursor(14,118);
  String shown=ssid; if (shown.length()>24) shown=shown.substring(0,24); tft.print(shown);
  tft.setTextColor(ST77XX_WHITE,ST77XX_BLACK);  tft.setCursor(14,170); tft.print("Please wait...");
}

String htmlEscape(const String &value) {
  String esc; esc.reserve(value.length()+8);
  for (uint16_t i=0; i<value.length(); i++) {
    char c=value[i];
    if      (c=='&')  esc+="&amp;";
    else if (c=='<')  esc+="&lt;";
    else if (c=='>')  esc+="&gt;";
    else if (c=='"')  esc+="&quot;";
    else              esc+=c;
  }
  return esc;
}

bool loadStoredWifi(String &ssid, String &pass) {
  wifiPrefs.begin(WIFI_PREF_NAMESPACE,true);
  ssid=wifiPrefs.getString("ssid",""); pass=wifiPrefs.getString("pass","");
  wifiPrefs.end(); ssid.trim(); return ssid.length()>0;
}

void saveStoredWifi(const String &ssid, const String &pass) {
  wifiPrefs.begin(WIFI_PREF_NAMESPACE,false);
  wifiPrefs.putString("ssid",ssid); wifiPrefs.putString("pass",pass);
  wifiPrefs.end();
}

String buildWifiNetworkOptions() {
  String options;
  int n=WiFi.scanNetworks(false,true,false,160);
  if (n<=0) { options+=F("<option value=''>No networks found</option>"); WiFi.scanDelete(); return options; }
  for (int i=0; i<n; i++) {
    String ssid=WiFi.SSID(i); if (ssid.length()==0) ssid="(hidden)";
    String safe=htmlEscape(ssid);
    options+=String("<option value='")+safe+"'>"+safe+" "+WiFi.RSSI(i)+" dBm";
    if (WiFi.encryptionType(i)!=WIFI_AUTH_OPEN) options+=" locked";
    options+="</option>";
  }
  WiFi.scanDelete(); return options;
}

void sendWifiSetupPage(const String &message="", bool isError=false) {
  String savedSsid,savedPass;
  bool hasSaved=loadStoredWifi(savedSsid,savedPass);
  String networkOptions=buildWifiNetworkOptions();
  String page; page.reserve(4200+networkOptions.length());
  page+=F("<!doctype html><html><head><meta name='viewport' content='width=device-width,initial-scale=1'>");
  page+=F("<title>AI Lens WiFi</title><style>");
  page+=F("body{margin:0;font-family:Arial,sans-serif;background:#071015;color:#f5fbff}");
  page+=F("main{max-width:420px;margin:0 auto;padding:28px 18px}");
  page+=F("h1{font-size:26px;margin:0 0 8px;color:#72f7ff}");
  page+=F("p{line-height:1.45;color:#cfe6ee}.box{border:1px solid #2d7180;padding:16px;border-radius:8px;background:#0d1b22}");
  page+=F("label{display:block;margin:14px 0 6px;font-weight:bold}");
  page+=F("input,select{box-sizing:border-box;width:100%;padding:12px;border-radius:6px;border:1px solid #6fa5b0;background:#061014;color:#fff;font-size:16px}");
  page+=F("button{width:100%;margin-top:18px;padding:13px;border:0;border-radius:6px;background:#ffd84d;color:#111;font-size:17px;font-weight:bold}");
  page+=F(".retry{background:#36d17c}.small{font-size:13px;color:#9fbac2}");
  page+=F(".msg{padding:10px;border-radius:6px;margin:12px 0;background:#123925}.err{background:#4b1212}");
  page+=F("</style></head><body><main><h1>AI Lens WiFi Setup</h1>");
  page+=F("<p>Connect this device to your home or school WiFi.</p>");
  if (message.length()>0)
    page+=String("<div class='msg ")+(isError?"err":"")+">"+htmlEscape(message)+"</div>";
  if (hasSaved) {
    page+=F("<div class='box'><b>Saved WiFi:</b> ");
    page+=htmlEscape(savedSsid);
    page+=F("<p class='small'>Device retries this automatically.</p>");
    page+=F("<form method='post' action='/retry'><button class='retry' type='submit'>Retry saved WiFi</button></form></div><br>");
  }
  page+=F("<div class='box'><form method='post' action='/save'>");
  page+=F("<label>Available WiFi</label><select onchange=\"document.getElementById('ssid').value=this.value\"><option value=''>Choose scanned network</option>");
  page+=networkOptions;
  page+=F("</select><label>WiFi name</label><input id='ssid' name='ssid' maxlength='32' required autofocus>");
  page+=F("<label>WiFi password</label><input name='pass' type='password' maxlength='64' placeholder='Leave blank for open WiFi'>");
  page+=F("<button type='submit'>Save and connect</button></form></div>");
  page+=F("<p>Keep your phone on <b>AI_LENS_SETUP</b> until TFT says connected.</p>");
  page+=F("</main></body></html>");
  setupServer.send(200,"text/html",page);
}

void handleWifiRoot()  { sendWifiSetupPage(); }

void handleWifiSave() {
  submittedWifiSsid=setupServer.arg("ssid"); submittedWifiPass=setupServer.arg("pass");
  submittedWifiSsid.trim();
  if (submittedWifiSsid.length()==0) { sendWifiSetupPage("WiFi name cannot be empty.",true); return; }
  wifiPortalSubmitted=true; sendWifiSetupPage("Trying to connect. Watch the TFT screen now.");
}

void handleWifiRetry() { wifiRetryRequested=true; sendWifiSetupPage("Retrying saved WiFi. Watch the TFT."); }

void redirectToWifiPortal() {
  setupServer.sendHeader("Location",String("http://")+setupPortalIp.toString(),true);
  setupServer.send(302,"text/plain","");
}

void configureWifiPortalServer() {
  static bool configured=false; if (configured) return;
  setupServer.on("/",HTTP_GET,handleWifiRoot);
  setupServer.on("/save",HTTP_POST,handleWifiSave);
  setupServer.on("/retry",HTTP_POST,handleWifiRetry);
  setupServer.on("/generate_204",HTTP_GET,redirectToWifiPortal);
  setupServer.on("/fwlink",HTTP_GET,redirectToWifiPortal);
  setupServer.on("/hotspot-detect.html",HTTP_GET,handleWifiRoot);
  setupServer.onNotFound(redirectToWifiPortal);
  configured=true;
}

bool connectToWifiNetwork(const String &ssid, const String &pass, bool keepPortalAp) {
  if (ssid.length()==0) return false;
  drawWifiConnecting(ssid);
  WiFi.disconnect(false,false);
  WiFi.mode(keepPortalAp?WIFI_AP_STA:WIFI_STA);
  delay(150); WiFi.begin(ssid.c_str(),pass.c_str());
  Serial.print("Connecting to WiFi: "); Serial.println(ssid);
  unsigned long startMs=millis();
  while (WiFi.status()!=WL_CONNECTED && millis()-startMs<22000UL) delay(250);
  if (WiFi.status()==WL_CONNECTED) {
    Serial.print("WiFi connected, IP: "); Serial.println(WiFi.localIP());
    drawMessage("WiFi connected",WiFi.localIP().toString()); delay(900); return true;
  }
  Serial.println("WiFi connection failed"); return false;
}

bool runWifiSetupPortal() {
  wifiPortalSubmitted=false; wifiRetryRequested=false;
  submittedWifiSsid=""; submittedWifiPass="";
  String savedSsid,savedPass;
  bool hasSavedWifi=loadStoredWifi(savedSsid,savedPass);
  unsigned long lastSavedRetryMs=millis();
  WiFi.disconnect(false,false); WiFi.mode(WIFI_AP_STA); delay(150);
  WiFi.softAPConfig(setupPortalIp,setupPortalIp,IPAddress(255,255,255,0));
  if (!WiFi.softAP(WIFI_SETUP_AP_SSID)) { drawMessage("WiFi setup failed","Restart device"); return false; }
  dnsServer.start(WIFI_SETUP_DNS_PORT,"*",setupPortalIp);
  configureWifiPortalServer(); setupServer.begin();
  Serial.println("WiFi setup portal started"); drawWifiSetupInstructions();

  while (true) {
    dnsServer.processNextRequest(); setupServer.handleClient();
    if (hasSavedWifi && (wifiRetryRequested || millis()-lastSavedRetryMs>WIFI_SAVED_RETRY_MS)) {
      wifiRetryRequested=false; lastSavedRetryMs=millis();
      if (connectToWifiNetwork(savedSsid,savedPass,true)) {
        dnsServer.stop(); setupServer.stop(); WiFi.softAPdisconnect(true); WiFi.mode(WIFI_STA); delay(300); return true;
      }
      WiFi.disconnect(false,false); WiFi.mode(WIFI_AP_STA);
      WiFi.softAPConfig(setupPortalIp,setupPortalIp,IPAddress(255,255,255,0)); WiFi.softAP(WIFI_SETUP_AP_SSID);
      drawMessage("Saved WiFi not","available yet"); delay(1200); drawWifiSetupInstructions();
    }
    if (wifiPortalSubmitted) {
      wifiPortalSubmitted=false;
      String ssid=submittedWifiSsid, pass=submittedWifiPass;
      if (connectToWifiNetwork(ssid,pass,true)) {
        saveStoredWifi(ssid,pass); dnsServer.stop(); setupServer.stop();
        WiFi.softAPdisconnect(true); WiFi.mode(WIFI_STA); delay(300); return true;
      }
      WiFi.disconnect(false,false); WiFi.mode(WIFI_AP_STA);
      WiFi.softAPConfig(setupPortalIp,setupPortalIp,IPAddress(255,255,255,0)); WiFi.softAP(WIFI_SETUP_AP_SSID);
      drawMessage("WiFi failed","Try again page"); delay(1400); drawWifiSetupInstructions();
    }
    delay(5);
  }
  return false;
}

void connectWiFi() {
  if (WiFi.status()==WL_CONNECTED) return;
  WiFi.persistent(false);
  String ssid,pass;
  if (loadStoredWifi(ssid,pass)) {
    if (connectToWifiNetwork(ssid,pass,false)) return;
    drawMessage("Saved WiFi failed","Starting setup"); delay(1000);
  } else if (strlen(WIFI_SSID)>0) {
    if (connectToWifiNetwork(WIFI_SSID,WIFI_PASS,false)) { saveStoredWifi(WIFI_SSID,WIFI_PASS); return; }
    drawMessage("WiFi failed","Starting setup"); delay(1000);
  }
  runWifiSetupPortal();
}

// ── Setup ────────────────────────────────────────────────────
void setup() {
  pinMode(19,OUTPUT);
  digitalWrite(19,HIGH);
  Serial.begin(115200);
  delay(3000);

  Serial.println("STEP 1: Serial OK"); Serial.flush();

  randomSeed((uint32_t)esp_random());
  Serial.println("STEP 2: Random seed OK"); Serial.flush();

  pinMode(NEXT_BUTTON_PIN,   INPUT_PULLUP);
  pinMode(SELECT_BUTTON_PIN, INPUT_PULLUP);
  pinMode(HOME_BUTTON_PIN,   INPUT_PULLUP);
  Serial.println("STEP 3: Pins OK"); Serial.flush();

  SPI.begin(TFT_SCLK, TFT_MISO, TFT_MOSI, TFT_CS);
  Serial.println("STEP 4: SPI OK"); Serial.flush();

  tft.init(240, 320);
  Serial.println("STEP 5: TFT init OK"); Serial.flush();

  tft.setRotation(3);
  // MV only, BGR bit cleared -- confirmed correct colour order for this
  // panel from the colour-bar diagnostic (RED/BLUE and CYAN/YELLOW were
  // swapped with the BGR bit set).
  { uint8_t m = 0x20; tft.sendCommand(0x36, &m, 1); }
  tft.invertDisplay(true);     // test both true/false if colours look inverted on your panel
  tft.setTextWrap(false);
  tft.fillScreen(ST77XX_BLACK);
  Serial.println("STEP 6: TFT fill OK"); Serial.flush();

  drawMessage("AI Exam","Booting...");
  Serial.println("STEP 7: drawMessage OK"); Serial.flush();

  initSpeakerAudio();
  Serial.println("STEP 8: initSpeakerAudio OK"); Serial.flush();

  connectWiFi();
  Serial.println("STEP 9: connectWiFi OK"); Serial.flush();

  showLogoSplash();
  Serial.println("STEP 10: logoSplash OK"); Serial.flush();

  drawMessage("AI Teacher","Welcome!","I am your AI teacher.");
  playWelcomeVoice();
  Serial.println("STEP 11: welcome voice OK"); Serial.flush();

  boardName = String(boardOptions[boardIndex]);
  state = STATE_SELECT_BOARD;
  drawBoardSelect();
  Serial.println("STEP 12: setup complete"); Serial.flush();
}

// ── Loop ─────────────────────────────────────────────────────
void loop() {
  if (setupButtonPressed(HOME_BUTTON_PIN)) {
    if (mp3gen) { if (mp3gen->isRunning()) mp3gen->stop(); delete mp3gen; mp3gen=nullptr; }
    if (audioSrc) { delete audioSrc; audioSrc=nullptr; }
    showLogoSplash();
    state=STATE_SELECT_BOARD; drawBoardSelect(); delay(20); return;
  }

  if (state==STATE_SELECT_BOARD) {
    if (setupButtonPressed(NEXT_BUTTON_PIN)) {
      boardIndex=(boardIndex+1)%boardOptionCount;
      updateSelectBox(String(boardOptions[boardIndex]));
    }
    if (setupButtonPressed(SELECT_BUTTON_PIN)) {
      boardName=String(boardOptions[boardIndex]);
      state=STATE_SELECT_CLASS; drawClassSelect();
    }
    delay(20); return;
  }

  if (state==STATE_SELECT_CLASS) {
    if (setupButtonPressed(NEXT_BUTTON_PIN)) {
      classIndex=(classIndex+1)%classOptionCount;
      updateSelectBox("Grade "+String(classOptions[classIndex]));
    }
    if (setupButtonPressed(SELECT_BUTTON_PIN)) {
      studentClass=String(classOptions[classIndex]); subjectIndex=0;
      state=STATE_SELECT_SUBJECT; drawSubjectSelect();
    }
    delay(20); return;
  }

  if (state==STATE_SELECT_SUBJECT) {
    const char **subjects; int count; getSubjectList(&subjects,&count);
    if (setupButtonPressed(NEXT_BUTTON_PIN)) {
      subjectIndex=(subjectIndex+1)%count;
      updateSelectBox(String(subjects[subjectIndex]));
    }
    if (setupButtonPressed(SELECT_BUTTON_PIN)) {
      subject=String(subjects[subjectIndex]); chapterIndex=0;
      loadChapterList();
    }
    delay(20); return;
  }

  if (state==STATE_SELECT_CHAPTER) {
    if (chapterOptionCount <= 0) {
      state=STATE_ERROR; lastError="No chapters loaded"; drawMessage("Error", lastError, "SELECT goes back");
      delay(20); return;
    }
    if (setupButtonPressed(NEXT_BUTTON_PIN)) {
      chapterIndex=(chapterIndex+1)%chapterOptionCount;
      updateSelectBox(chapterOptions[chapterIndex]);
    }
    if (setupButtonPressed(SELECT_BUTTON_PIN)) {
      chapter=chapterOptions[chapterIndex];
      questionNo=0; score=0; selectedAnswerIndex=0;
      for (int i=0;i<QUESTION_HISTORY_LIMIT;i++) askedQuestions[i]="";
      drawMessage("Selected", boardName+" Grade "+studentClass,
                  subject+" "+chapter); delay(900); loadQuestion();
    }
    delay(20); return;
  }

  if (state==STATE_SCORE) {
    if (setupButtonPressed(NEXT_BUTTON_PIN)||setupButtonPressed(SELECT_BUTTON_PIN)) {
      state=STATE_SELECT_BOARD; drawBoardSelect();
    }
    delay(20); return;
  }

  if (state==STATE_QUESTION) {
    if (setupButtonPressed(NEXT_BUTTON_PIN)) {
      int8_t prev = selectedAnswerIndex;
      selectedAnswerIndex = (selectedAnswerIndex+1) % 4;
      drawOneOptionRow(prev);
      drawOneOptionRow(selectedAnswerIndex);
    }
    if (setupButtonPressed(SELECT_BUTTON_PIN)) {
      lastSelectedIndex   = selectedAnswerIndex;
      lastAnswerCorrect   = (lastSelectedIndex==currentQuestion.correctIndex);
      if (lastAnswerCorrect) score++;
      questionNo++; state=STATE_RESULT;
      drawResult(lastAnswerCorrect,lastSelectedIndex);
      playAnswerVoice(lastAnswerCorrect);
      drawResult(lastAnswerCorrect,lastSelectedIndex);
    }
    delay(20); return;
  }

  if (state==STATE_RESULT) {
    if (setupButtonPressed(NEXT_BUTTON_PIN))   loadQuestion();
    if (setupButtonPressed(SELECT_BUTTON_PIN)) { state=STATE_SCORE; drawScore(); }
    delay(20); return;
  }

  if (state==STATE_ERROR && setupButtonPressed(SELECT_BUTTON_PIN)) {
    state=STATE_SELECT_BOARD; drawBoardSelect(); delay(20); return;
  }

  delay(20);
}