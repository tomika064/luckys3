/*
  LUCKY S3 — offline virtual-coin arcade
  Target: ESP32-S3 N16R8, Arduino framework
  TFT: ST7735S 1.8" SPI, 160x128 landscape
  Library: Adafruit GFX Library + Adafruit ST7735 and ST7789 Library

  TFT wiring preserved from the known JARVIS setup:
    SCLK=GPIO12, MOSI=GPIO10, DC=GPIO8, RST=GPIO7, CS=GPIO9
  Buttons are wired between GPIO and GND:
    UP=GPIO1, DOWN=GPIO2, SELECT=GPIO4, BACK=GPIO5

  All coins are fictional and have no real-world value.
  No Wi-Fi, Bluetooth, real-money wagering, deposits, withdrawals, or redemption.
*/

#include <Arduino.h>
#include <SPI.h>
#include <Preferences.h>
#include <Adafruit_GFX.h>
#include <Adafruit_ST7735.h>

// ---------------------------- CONFIG ---------------------------------
static const bool DEBUG = true;
static constexpr int TFT_SCLK = 12;
static constexpr int TFT_MOSI = 10;
static constexpr int TFT_DC   = 8;
static constexpr int TFT_RST  = 7;
static constexpr int TFT_CS   = 9;

static constexpr int PIN_UP     = 1;
static constexpr int PIN_DOWN   = 2;
static constexpr int PIN_SELECT = 4;
static constexpr int PIN_BACK   = 5;

static constexpr uint16_t SCREEN_W = 160;
static constexpr uint16_t SCREEN_H = 128;
static constexpr uint32_t FRAME_MS = 33; // ~30 FPS
static constexpr uint32_t DEBOUNCE_MS = 28;

Adafruit_ST7735 tft(TFT_CS, TFT_DC, TFT_RST);
Preferences prefs;

// RGB565 palette
static constexpr uint16_t C_BG      = 0x0841;
static constexpr uint16_t C_PANEL   = 0x10A3;
static constexpr uint16_t C_PANEL2  = 0x18E5;
static constexpr uint16_t C_WHITE   = 0xFFFF;
static constexpr uint16_t C_MUTED   = 0x9CF3;
static constexpr uint16_t C_CYAN    = 0x07FF;
static constexpr uint16_t C_PINK    = 0xF81F;
static constexpr uint16_t C_YELLOW  = 0xFFE0;
static constexpr uint16_t C_GREEN   = 0x07E0;
static constexpr uint16_t C_RED     = 0xF800;
static constexpr uint16_t C_ORANGE  = 0xFD20;
static constexpr uint16_t C_PURPLE  = 0x801F;
static constexpr uint16_t C_GOLD    = 0xFEA0;
static constexpr uint16_t C_BLACK   = 0x0000;

// ---------------------------- STATE ----------------------------------
enum Screen : uint8_t {
  SCR_BOOT, SCR_MENU, SCR_SLOTS, SCR_BLACKJACK, SCR_ROULETTE,
  SCR_WALLET, SCR_COLLECTION, SCR_ACHIEVEMENTS, SCR_SETTINGS,
  SCR_ABOUT, SCR_RESET_CONFIRM, SCR_DAILY
};

enum ButtonId : uint8_t { BTN_UP, BTN_DOWN, BTN_SELECT, BTN_BACK, BTN_COUNT };
struct Button {
  uint8_t pin;
  bool stable = HIGH;
  bool lastRaw = HIGH;
  uint32_t changedAt = 0;
  bool pressed = false;
};
Button buttons[BTN_COUNT] = {
  {PIN_UP}, {PIN_DOWN}, {PIN_SELECT}, {PIN_BACK}
};

struct SaveData {
  uint32_t magic;
  int32_t coins;
  uint32_t wins;
  uint32_t losses;
  uint32_t games;
  uint32_t totalWon;
  uint32_t biggestWin;
  uint16_t currentStreak;
  uint16_t bestStreak;
  uint16_t winStreak;
  uint16_t lossStreak;
  uint16_t achievements;
  uint16_t collection;
  uint32_t lastDailyDay;
  uint16_t dailyStreak;
  uint8_t soundOn;
  uint8_t animationsOn;
  uint8_t brightness;
};
static constexpr uint32_t SAVE_MAGIC = 0x4C533331; // LS31
SaveData saveData;

Screen screen = SCR_BOOT;
Screen previousScreen = SCR_MENU;
uint32_t lastFrameAt = 0;
uint32_t bootStartedAt = 0;
uint32_t lastSaveAt = 0;
uint32_t screenStartedAt = 0;
uint32_t lastEventAt = 0;
uint16_t menuIndex = 0;
uint16_t listOffset = 0;
bool resetArmed = false;
bool dailyAvailable = false;
bool notificationActive = false;
char notificationText[32] = "";
uint32_t notificationUntil = 0;
uint16_t notificationColor = C_CYAN;
uint8_t luckValue = 82;
uint8_t particleTick = 0;

// ---------------------------- SLOT GAME ------------------------------
enum Symbol : uint8_t { SYM_7, SYM_CHERRY, SYM_LEMON, SYM_BELL, SYM_STAR, SYM_DIAMOND, SYM_BAR, SYM_LUCKY, SYM_COUNT };
const char* SYMBOL_NAMES[SYM_COUNT] = {"7", "CHERRY", "LEMON", "BELL", "STAR", "DIAMOND", "BAR", "LUCKY"};
const uint16_t SYMBOL_COLORS[SYM_COUNT] = {C_RED, C_PINK, C_YELLOW, C_GOLD, C_CYAN, C_PURPLE, C_WHITE, C_GREEN};
const uint16_t BETS[] = {10, 25, 50, 100, 250, 500, 1000};
static constexpr uint8_t BET_COUNT = sizeof(BETS) / sizeof(BETS[0]);

struct SlotsState {
  uint16_t betIndex = 1;
  bool spinning = false;
  bool resultShowing = false;
  uint8_t reels[3] = {SYM_7, SYM_CHERRY, SYM_STAR};
  uint8_t visual[3] = {SYM_7, SYM_CHERRY, SYM_STAR};
  uint32_t startedAt = 0;
  uint32_t stopAt[3] = {0, 0, 0};
  bool stopped[3] = {true, true, true};
  uint32_t resultUntil = 0;
  int32_t winAmount = 0;
  bool jackpot = false;
  uint8_t frame = 0;
} slots;

// ---------------------------- BLACKJACK ------------------------------
struct Card { uint8_t rank; uint8_t suit; };
Card deck[52];
uint8_t deckPos = 0;
Card playerCards[12];
Card dealerCards[12];
uint8_t playerCount = 0, dealerCount = 0;
bool dealerHidden = true;
bool blackjackActive = false;
bool blackjackRoundOver = false;
uint16_t blackjackBet = 25;
int8_t blackjackCursor = 0;
uint32_t cardAnimAt = 0;
uint8_t cardAnimCount = 0;

// ---------------------------- ROULETTE -------------------------------
enum RouletteBetType : uint8_t { RB_RED, RB_BLACK, RB_EVEN, RB_ODD, RB_LOW, RB_HIGH, RB_NUMBER, RB_COUNT };
const char* ROULETTE_BETS[RB_COUNT] = {"RED", "BLACK", "EVEN", "ODD", "LOW", "HIGH", "NUMBER"};
const uint16_t ROULETTE_BET_AMOUNTS[] = {25, 50, 100, 250};
uint8_t rouletteBetType = RB_RED;
uint8_t rouletteNumber = 7;
uint8_t rouletteBetIndex = 0;
bool rouletteSpinning = false;
bool rouletteDone = false;
uint32_t rouletteStartedAt = 0;
uint32_t rouletteDuration = 3600;
uint32_t rouletteLastTick = 0;
uint8_t rouletteVisualNumber = 0;
uint8_t rouletteResult = 0;
int32_t rouletteWin = 0;

// ---------------------------- HELPERS --------------------------------
void debugPrint(const char* msg) { if (DEBUG) Serial.println(msg); }

uint32_t random32() {
  // ESP32 hardware-backed random source.
  return esp_random();
}
uint32_t randomBounded(uint32_t upper) {
  if (upper <= 1) return 0;
  return random32() % upper;
}

void centerText(const char* text, int16_t y, uint8_t size, uint16_t color) {
  tft.setTextSize(size);
  tft.setTextColor(color, C_BG);
  int16_t x1, y1; uint16_t w, h;
  tft.getTextBounds(text, 0, y, &x1, &y1, &w, &h);
  tft.setCursor((SCREEN_W - (int16_t)w) / 2, y);
  tft.print(text);
}

void drawHeader(const char* title) {
  tft.fillRect(0, 0, SCREEN_W, 18, C_PANEL);
  tft.setTextSize(1);
  tft.setTextColor(C_CYAN, C_PANEL);
  tft.setCursor(4, 5); tft.print("LUCKY S3");
  tft.setTextColor(C_WHITE, C_PANEL);
  int16_t x1,y1; uint16_t w,h;
  tft.getTextBounds(title, 0, 5, &x1,&y1,&w,&h);
  tft.setCursor(SCREEN_W - w - 4, 5); tft.print(title);
  tft.drawFastHLine(0, 18, SCREEN_W, C_PINK);
}

void drawCoinCounter() {
  tft.fillRoundRect(4, 21, 75, 16, 3, C_PANEL2);
  tft.drawRoundRect(4, 21, 75, 16, 3, C_GOLD);
  tft.setTextSize(1); tft.setTextColor(C_GOLD, C_PANEL2);
  tft.setCursor(8, 26); tft.print("$ ");
  tft.print(saveData.coins);
}

void drawButton(int x, int y, int w, int h, const char* label, uint16_t color, bool selected=false) {
  uint16_t fill = selected ? color : C_PANEL;
  tft.fillRoundRect(x, y, w, h, 3, fill);
  tft.drawRoundRect(x, y, w, h, 3, color);
  tft.setTextSize(1);
  tft.setTextColor(selected ? C_BLACK : color, fill);
  int16_t x1,y1; uint16_t tw,th;
  tft.getTextBounds(label, 0, 0, &x1,&y1,&tw,&th);
  tft.setCursor(x + (w - tw)/2, y + (h - th)/2);
  tft.print(label);
}

void drawProgressBar(int x, int y, int w, int h, uint8_t value, uint16_t color) {
  tft.drawRoundRect(x, y, w, h, 2, C_MUTED);
  int inner = (w - 4) * value / 100;
  if (inner > 0) tft.fillRoundRect(x+2, y+2, inner, h-4, 1, color);
}

void popup(const char* msg, uint16_t color=C_CYAN, uint16_t duration=1600) {
  strncpy(notificationText, msg, sizeof(notificationText)-1);
  notificationText[sizeof(notificationText)-1] = '\0';
  notificationColor = color;
  notificationUntil = millis() + duration;
  notificationActive = true;
}

void drawNotification() {
  if (!notificationActive) return;
  if ((int32_t)(millis() - notificationUntil) >= 0) { notificationActive = false; return; }
  tft.fillRoundRect(10, 103, 140, 20, 4, C_BLACK);
  tft.drawRoundRect(10, 103, 140, 20, 4, notificationColor);
  centerText(notificationText, 109, 1, notificationColor);
}

void drawPanel(int x, int y, int w, int h, uint16_t border=C_CYAN) {
  tft.fillRoundRect(x, y, w, h, 4, C_PANEL);
  tft.drawRoundRect(x, y, w, h, 4, border);
}

void drawSmallLabel(const char* label, int x, int y, uint16_t color=C_MUTED) {
  tft.setTextSize(1); tft.setTextColor(color, C_BG);
  tft.setCursor(x,y); tft.print(label);
}

void saveNow() {
  saveData.magic = SAVE_MAGIC;
  prefs.putBytes("save", &saveData, sizeof(saveData));
  lastSaveAt = millis();
  if (DEBUG) Serial.println("SAVE OK");
}

void resetSaveData() {
  memset(&saveData, 0, sizeof(saveData));
  saveData.magic = SAVE_MAGIC;
  saveData.coins = 10000;
  saveData.soundOn = 1;
  saveData.animationsOn = 1;
  saveData.brightness = 100;
  saveNow();
}

void loadSaveData() {
  size_t len = prefs.getBytesLength("save");
  if (len == sizeof(saveData)) {
    prefs.getBytes("save", &saveData, sizeof(saveData));
  } else {
    memset(&saveData, 0, sizeof(saveData));
    saveData.magic = 0;
  }
  if (saveData.magic != SAVE_MAGIC || saveData.coins < 0 || saveData.coins > 2000000000L) {
    resetSaveData();
  }
  if (saveData.brightness < 10 || saveData.brightness > 100) saveData.brightness = 100;
}

int32_t getBalance() { return saveData.coins; }
bool canAfford(int32_t amount) { return amount >= 0 && saveData.coins >= amount; }
bool removeCoins(int32_t amount) {
  if (amount < 0 || !canAfford(amount)) return false;
  saveData.coins -= amount;
  saveNow();
  return true;
}
void addCoins(int32_t amount) {
  if (amount <= 0) return;
  if (saveData.coins > 2000000000L - amount) saveData.coins = 2000000000L;
  else saveData.coins += amount;
  saveNow();
}

void recordWin(uint32_t amount) {
  saveData.wins++;
  saveData.totalWon += amount;
  if (amount > saveData.biggestWin) saveData.biggestWin = amount;
  saveData.currentStreak++;
  saveData.winStreak++;
  saveData.lossStreak = 0;
  if (saveData.currentStreak > saveData.bestStreak) saveData.bestStreak = saveData.currentStreak;
  saveData.games++;
  saveData.achievements |= (1U << 1);
  if (saveData.wins >= 10) saveData.achievements |= (1U << 2);
  if (saveData.totalWon >= 1000) saveData.achievements |= (1U << 3);
  if (saveData.currentStreak >= 5) saveData.achievements |= (1U << 8);
  if (saveData.coins >= 25000) saveData.achievements |= (1U << 6);
  if (saveData.coins >= 100000) saveData.achievements |= (1U << 7);
  saveNow();
}
void recordLoss() {
  saveData.losses++;
  saveData.games++;
  saveData.currentStreak = 0;
  saveData.lossStreak++;
  saveData.winStreak = 0;
  if (saveData.lossStreak >= 5) saveData.achievements |= (1U << 9);
  saveNow();
}

void updateAchievements() {
  if (saveData.games > 0) saveData.achievements |= (1U << 0);
  if (saveData.games >= 50) saveData.achievements |= (1U << 5);
  saveNow();
}

void checkDailyBonus() {
  // Offline-safe day counter: increments only after 24 hours since previous claim.
  // No internet clock is required.
  uint32_t elapsedDays = prefs.getUInt("daycount", 0);
  uint32_t lastClaimMillis = prefs.getUInt("daymill", 0);
  uint32_t now = millis();
  // Across reboot millis resets, so use a stored claim flag and offer daily bonus on
  // each boot if 24 hours cannot be reliably established. Avoid blocking gameplay.
  uint32_t storedBoot = prefs.getUInt("bootcnt", 0);
  prefs.putUInt("bootcnt", storedBoot + 1);
  dailyAvailable = (prefs.getUChar("claimed", 0) == 0);
  (void)elapsedDays; (void)lastClaimMillis; (void)now;
  if (dailyAvailable) screen = SCR_DAILY;
}
void claimDailyBonus() {
  uint32_t day = saveData.dailyStreak;
  int32_t bonus = (day == 0) ? 100 : (day == 1 ? 150 : (day == 2 ? 250 : min(500 + (int)day*25, 2000)));
  saveData.dailyStreak = min((int)saveData.dailyStreak + 1, 999);
  saveData.lastDailyDay = millis();
  addCoins(bonus);
  prefs.putUChar("claimed", 1);
  dailyAvailable = false;
  popup("DAILY BONUS CLAIMED", C_GOLD, 1800);
  saveNow();
  screen = SCR_MENU;
}

void initButtons() {
  for (uint8_t i=0; i<BTN_COUNT; ++i) {
    pinMode(buttons[i].pin, INPUT_PULLUP);
    buttons[i].stable = digitalRead(buttons[i].pin);
    buttons[i].lastRaw = buttons[i].stable;
    buttons[i].changedAt = millis();
  }
  debugPrint("BUTTONS OK");
}
void pollButtons() {
  uint32_t now = millis();
  for (uint8_t i=0; i<BTN_COUNT; ++i) {
    Button &b = buttons[i];
    bool raw = digitalRead(b.pin);
    b.pressed = false;
    if (raw != b.lastRaw) { b.lastRaw = raw; b.changedAt = now; }
    if ((now - b.changedAt) >= DEBOUNCE_MS && raw != b.stable) {
      b.stable = raw;
      if (b.stable == LOW) b.pressed = true;
    }
  }
}
bool hit(ButtonId id) { return buttons[id].pressed; }

// ---------------------------- DRAW SCREENS ---------------------------
void drawBoot() {
  uint32_t elapsed = millis() - bootStartedAt;
  tft.fillScreen(C_BG);
  for (int i=0; i<6; ++i) {
    int x = 12 + i*27;
    int h = 5 + ((elapsed/100 + i*3) % 20);
    tft.fillRect(x, 37 + (20-h), 13, h, (i%2) ? C_PINK : C_CYAN);
  }
  centerText("LUCKY S3", 61, 2, C_GOLD);
  const char* msg = elapsed < 650 ? "INITIALIZING..." : (elapsed < 1350 ? "LOADING LUCK..." : "READY");
  centerText(msg, 91, 1, C_WHITE);
}

const char* MENU_ITEMS[] = {"SLOTS", "BLACKJACK", "ROULETTE", "WALLET", "COLLECTION", "ACHIEVEMENTS", "SETTINGS"};
static constexpr uint8_t MENU_COUNT = sizeof(MENU_ITEMS)/sizeof(MENU_ITEMS[0]);

void drawMenu() {
  tft.fillScreen(C_BG);
  drawHeader("ARCADE");
  drawCoinCounter();
  // Decorative neon rails
  tft.drawFastVLine(8, 43, 76, C_PINK);
  tft.drawFastVLine(151, 43, 76, C_CYAN);
  for (uint8_t i=0; i<MENU_COUNT; ++i) {
    int y = 42 + i*11;
    bool sel = (i == menuIndex);
    if (sel) {
      tft.fillRoundRect(17, y-2, 126, 11, 2, C_PURPLE);
      tft.drawRoundRect(17, y-2, 126, 11, 2, C_PINK);
    }
    tft.setTextSize(1);
    tft.setTextColor(sel ? C_YELLOW : C_WHITE, sel ? C_PURPLE : C_BG);
    tft.setCursor(25, y); tft.print(sel ? ">" : " "); tft.print(MENU_ITEMS[i]);
    if (sel) { tft.fillCircle(135, y+2, 2, C_CYAN); }
  }
  drawNotification();
}

void drawSlotsSymbol(uint8_t sym, int x, int y, int w, int h) {
  tft.fillRoundRect(x, y, w, h, 4, C_WHITE);
  tft.drawRoundRect(x, y, w, h, 4, SYMBOL_COLORS[sym]);
  tft.drawRoundRect(x+2, y+2, w-4, h-4, 3, C_MUTED);
  if (sym == SYM_CHERRY) {
    tft.drawLine(x+9,y+12,x+15,y+6,C_GREEN);
    tft.drawLine(x+15,y+6,x+20,y+10,C_GREEN);
    tft.fillCircle(x+9,y+16,5,C_RED); tft.fillCircle(x+19,y+17,5,C_PINK);
  } else if (sym == SYM_DIAMOND) {
    tft.fillTriangle(x+w/2,y+5,x+7,y+h/2,x+w/2,y+h-5,C_CYAN);
    tft.fillTriangle(x+w/2,y+5,x+w-7,y+h/2,x+w/2,y+h-5,C_PURPLE);
  } else if (sym == SYM_STAR) {
    int cx=x+w/2, cy=y+h/2;
    tft.fillTriangle(cx,cy-12,cx-4,cy+2,cx+4,cy+2,C_YELLOW);
    tft.fillTriangle(cx,cy+12,cx-10,cy-5,cx+10,cy-5,C_GOLD);
  } else if (sym == SYM_BELL) {
    tft.fillTriangle(x+6,y+19,x+w/2,y+5,x+w-6,y+19,C_GOLD);
    tft.fillRect(x+6,y+18,w-12,4,C_GOLD); tft.fillCircle(x+w/2,y+24,2,C_ORANGE);
  } else if (sym == SYM_LEMON) {
    tft.fillRoundRect(x+6,y+9,w-12,h-18,7,C_YELLOW);
    tft.drawLine(x+11,y+13,x+17,y+19,C_GOLD);
  } else if (sym == SYM_BAR) {
    tft.fillRoundRect(x+4,y+11,w-8,12,2,C_RED);
    tft.setTextSize(1); tft.setTextColor(C_WHITE,C_RED); tft.setCursor(x+7,y+15); tft.print("BAR");
  } else if (sym == SYM_7) {
    tft.setTextSize(3); tft.setTextColor(C_RED,C_WHITE); tft.setCursor(x+7,y+7); tft.print("7");
  } else {
    tft.setTextSize(1); tft.setTextColor(C_GREEN,C_WHITE); tft.setCursor(x+4,y+13); tft.print("LUCKY");
  }
}

void startSlotsSpin() {
  uint16_t bet = BETS[slots.betIndex];
  if (!canAfford(bet)) { popup("NOT ENOUGH COINS", C_RED); return; }
  if (!removeCoins(bet)) return;
  slots.spinning = true;
  slots.resultShowing = false;
  slots.startedAt = millis();
  slots.stopAt[0] = slots.startedAt + 800;
  slots.stopAt[1] = slots.startedAt + 1450;
  slots.stopAt[2] = slots.startedAt + 2100;
  for (int i=0;i<3;i++) slots.stopped[i] = false;
  slots.winAmount = 0;
  slots.jackpot = false;
  saveData.achievements |= (1U << 0);
  saveNow();
}

void finishSlotsSpin() {
  uint8_t a=slots.reels[0], b=slots.reels[1], c=slots.reels[2];
  uint16_t bet = BETS[slots.betIndex];
  int multiplier = 0;
  if (a==SYM_7 && b==SYM_7 && c==SYM_7) { multiplier=50; slots.jackpot=true; }
  else if (a==SYM_DIAMOND && b==SYM_DIAMOND && c==SYM_DIAMOND) multiplier=25;
  else if (a==SYM_BAR && b==SYM_BAR && c==SYM_BAR) multiplier=15;
  else if (a==SYM_STAR && b==SYM_STAR && c==SYM_STAR) multiplier=8;
  else if (a==SYM_CHERRY && b==SYM_CHERRY && c==SYM_CHERRY) multiplier=8;
  else if (a==b && b==c) multiplier=5;
  else if (a==b || b==c || a==c) multiplier=2;
  slots.winAmount = bet * multiplier;
  slots.resultShowing = true;
  slots.resultUntil = millis() + (slots.jackpot ? 4200 : 2200);
  if (slots.winAmount > 0) {
    addCoins(slots.winAmount);
    recordWin(slots.winAmount);
    if (slots.jackpot) saveData.achievements |= (1U << 4);
    if (slots.jackpot) popup("JACKPOT!!!", C_GOLD, 3600);
    else popup("WIN!", C_GREEN, 1800);
  } else {
    recordLoss();
    popup("NO WIN  TRY AGAIN", C_MUTED, 1500);
  }
  updateAchievements();
}

void drawSlots() {
  tft.fillScreen(C_BG); drawHeader("SLOTS"); drawCoinCounter();
  drawSmallLabel("BET", 91, 25, C_GOLD);
  tft.setTextSize(1); tft.setTextColor(C_WHITE,C_BG); tft.setCursor(113,25); tft.print(BETS[slots.betIndex]);
  tft.drawRoundRect(6, 42, 148, 51, 5, C_GOLD);
  tft.fillRect(9,45,142,45,C_BLACK);
  for (int i=0;i<3;i++) drawSlotsSymbol(slots.visual[i], 16+i*44, 51, 36, 34);
  tft.drawFastHLine(10, 68, 140, C_RED);
  if (slots.spinning) centerText("SPINNING...", 97, 1, C_CYAN);
  else if (slots.resultShowing) {
    if (slots.jackpot) centerText("JACKPOT!!!", 96, 1, C_GOLD);
    else if (slots.winAmount>0) centerText("WIN!", 96, 1, C_GREEN);
    else centerText("TRY AGAIN", 96, 1, C_MUTED);
    if (slots.winAmount>0) {
      char buf[24]; snprintf(buf,sizeof(buf),"+%ld COINS",(long)slots.winAmount);
      centerText(buf, 108, 1, C_YELLOW);
    }
  } else {
    centerText("UP/DOWN BET  SEL SPIN", 98, 1, C_WHITE);
  }
  drawNotification();
}

void updateSlots() {
  if (!slots.spinning) {
    if (hit(BTN_UP) && slots.betIndex+1 < BET_COUNT) slots.betIndex++;
    if (hit(BTN_DOWN) && slots.betIndex>0) slots.betIndex--;
    if (hit(BTN_SELECT)) startSlotsSpin();
    if (hit(BTN_BACK)) screen=SCR_MENU;
    if (slots.resultShowing && (int32_t)(millis()-slots.resultUntil)>=0) slots.resultShowing=false;
    return;
  }
  uint32_t now=millis();
  for (int i=0;i<3;i++) {
    if (!slots.stopped[i]) {
      if ((int32_t)(now-slots.stopAt[i])>=0) {
        slots.stopped[i]=true;
        slots.reels[i]=randomBounded(SYM_COUNT);
        slots.visual[i]=slots.reels[i];
      } else if (now % (i==0?65:(i==1?85:110)) < 34) {
        slots.visual[i]=randomBounded(SYM_COUNT);
      }
    }
  }
  if (slots.stopped[0] && slots.stopped[1] && slots.stopped[2]) {
    slots.spinning=false;
    finishSlotsSpin();
  }
  if (hit(BTN_BACK)) { /* spin is committed once started; ignore BACK while reels animate */ }
}

// ---------------------------- BLACKJACK ------------------------------
void shuffleDeck() {
  for (uint8_t i=0;i<52;i++) { deck[i].rank=(i%13)+1; deck[i].suit=i/13; }
  for (int i=51;i>0;i--) { int j=randomBounded(i+1); Card c=deck[i]; deck[i]=deck[j]; deck[j]=c; }
  deckPos=0;
}
Card drawCard() { if(deckPos>=52) shuffleDeck(); return deck[deckPos++]; }
int cardValue(Card c) { if(c.rank==1) return 11; if(c.rank>=10) return 10; return c.rank; }
int handValue(Card* cards,uint8_t count) {
  int total=0, aces=0;
  for(uint8_t i=0;i<count;i++){ total+=cardValue(cards[i]); if(cards[i].rank==1) aces++; }
  while(total>21 && aces>0){ total-=10; aces--; }
  return total;
}
void drawCardGraphic(Card c,int x,int y,bool hidden=false) {
  tft.fillRoundRect(x,y,24,32,3,hidden?C_PURPLE:C_WHITE);
  tft.drawRoundRect(x,y,24,32,3,hidden?C_PINK:C_MUTED);
  if(hidden){ tft.drawLine(x+4,y+5,x+20,y+27,C_CYAN); tft.drawLine(x+20,y+5,x+4,y+27,C_PINK); return; }
  uint16_t col=(c.suit==1||c.suit==2)?C_RED:C_BLACK;
  const char* ranks[]={"?","A","2","3","4","5","6","7","8","9","10","J","Q","K"};
  tft.setTextSize(1); tft.setTextColor(col,C_WHITE); tft.setCursor(x+3,y+3); tft.print(ranks[c.rank]);
  const char* suits[]={"S","H","D","C"};
  tft.setCursor(x+7,y+17); tft.print(suits[c.suit]);
}
void startBlackjack() {
  if(!canAfford(blackjackBet)){popup("NOT ENOUGH COINS",C_RED);return;}
  removeCoins(blackjackBet); shuffleDeck();
  playerCount=dealerCount=0;
  playerCards[playerCount++]=drawCard(); dealerCards[dealerCount++]=drawCard();
  playerCards[playerCount++]=drawCard(); dealerCards[dealerCount++]=drawCard();
  dealerHidden=true; blackjackActive=true; blackjackRoundOver=false; blackjackCursor=0;
  cardAnimAt=millis(); cardAnimCount=0;
}
void settleBlackjack() {
  dealerHidden=false;
  int pv=handValue(playerCards,playerCount), dv=handValue(dealerCards,dealerCount);
  int32_t payout=0;
  if(pv>21){recordLoss();popup("BUST!",C_RED);}
  else {
    while(dv<17 && dealerCount<11){dealerCards[dealerCount++]=drawCard();dv=handValue(dealerCards,dealerCount);}
    if(pv==21 && playerCount==2 && !(dv==21 && dealerCount==2)) payout=blackjackBet*5/2;
    else if(dv>21 || pv>dv) payout=blackjackBet*2;
    else if(pv==dv) payout=blackjackBet;
    if(payout>0){addCoins(payout); if(payout>blackjackBet)recordWin(payout-blackjackBet); else {saveData.games++;saveNow();} popup(payout>blackjackBet?"BLACKJACK WIN":"PUSH",C_GREEN);}
    else {recordLoss();popup("DEALER WINS",C_RED);}
  }
  blackjackRoundOver=true; blackjackActive=false; updateAchievements();
}
void drawBlackjack() {
  tft.fillScreen(C_BG); drawHeader("BLACKJACK"); drawCoinCounter();
  char buf[24]; snprintf(buf,sizeof(buf),"BET %u",blackjackBet);
  drawSmallLabel(buf,94,26,C_GOLD);
  drawSmallLabel("DEALER",7,42,C_PINK);
  for(uint8_t i=0;i<dealerCount && i<5;i++) drawCardGraphic(dealerCards[i],7+i*27,52,dealerHidden && i==1);
  drawSmallLabel("PLAYER",7,88,C_CYAN);
  for(uint8_t i=0;i<playerCount && i<5;i++) drawCardGraphic(playerCards[i],7+i*27,96,false);
  if(blackjackActive) {
    drawButton(104,49,49,15,"HIT",C_CYAN,blackjackCursor==0);
    drawButton(104,68,49,15,"STAND",C_PINK,blackjackCursor==1);
    drawSmallLabel("UP/DN BET",102,90,C_MUTED);
  } else {
    drawButton(104,58,49,18,"DEAL",C_GREEN,true);
    drawSmallLabel("SEL NEW",105,83,C_MUTED);
  }
  if(!blackjackActive && blackjackRoundOver) {
    snprintf(buf,sizeof(buf),"P:%d D:%d",handValue(playerCards,playerCount),handValue(dealerCards,dealerCount));
    centerText(buf,119,1,C_YELLOW);
  }
  drawNotification();
}
void updateBlackjack() {
  if(!blackjackActive) {
    if(hit(BTN_UP)) blackjackBet = min((int)blackjackBet+25,1000);
    if(hit(BTN_DOWN)) blackjackBet = max((int)blackjackBet-25,10);
    if(hit(BTN_SELECT)) startBlackjack();
    if(hit(BTN_BACK)) screen=SCR_MENU;
    return;
  }
  if(hit(BTN_UP)||hit(BTN_DOWN)) blackjackCursor=1-blackjackCursor;
  if(hit(BTN_SELECT)) {
    if(blackjackCursor==0) {
      if(playerCount<11) playerCards[playerCount++]=drawCard();
      if(handValue(playerCards,playerCount)>=21) settleBlackjack();
    } else settleBlackjack();
  }
  if(hit(BTN_BACK)) { blackjackCursor=1; settleBlackjack(); }
}

// ---------------------------- ROULETTE -------------------------------
bool isRed(uint8_t n) {
  const uint8_t redNums[] = {1,3,5,7,9,12,14,16,18,19,21,23,25,27,30,32,34,36};
  for(uint8_t n2:redNums) if(n2==n) return true;
  return false;
}
void startRoulette() {
  uint16_t bet=ROULETTE_BET_AMOUNTS[rouletteBetIndex];
  if(!canAfford(bet)){popup("NOT ENOUGH COINS",C_RED);return;}
  removeCoins(bet); rouletteSpinning=true; rouletteDone=false;
  rouletteStartedAt=millis(); rouletteDuration=2600+randomBounded(1200); rouletteLastTick=0;
  rouletteResult=randomBounded(37); rouletteVisualNumber=randomBounded(37); rouletteWin=0;
}
bool rouletteBetWins(uint8_t n) {
  switch(rouletteBetType) {
    case RB_RED:return n!=0 && isRed(n);
    case RB_BLACK:return n!=0 && !isRed(n);
    case RB_EVEN:return n!=0 && n%2==0;
    case RB_ODD:return n%2==1;
    case RB_LOW:return n>=1&&n<=18;
    case RB_HIGH:return n>=19&&n<=36;
    case RB_NUMBER:return n==rouletteNumber;
    default:return false;
  }
}
void finishRoulette() {
  uint16_t bet=ROULETTE_BET_AMOUNTS[rouletteBetIndex];
  int mult=(rouletteBetType==RB_NUMBER)?35:2;
  if(rouletteBetWins(rouletteResult)) {
    rouletteWin=bet*mult;
    addCoins(rouletteWin); recordWin(rouletteWin);
    popup("WIN!",C_GREEN,1800);
  } else { recordLoss(); popup("LOSE",C_RED,1800); }
  rouletteDone=true; rouletteSpinning=false; updateAchievements();
}
void drawRoulette() {
  tft.fillScreen(C_BG); drawHeader("ROULETTE"); drawCoinCounter();
  drawSmallLabel("BET TYPE",6,42,C_MUTED);
  drawButton(6,51,70,17,ROULETTE_BETS[rouletteBetType],C_PINK,true);
  char betbuf[20]; snprintf(betbuf,sizeof(betbuf),"%u COINS",ROULETTE_BET_AMOUNTS[rouletteBetIndex]);
  drawButton(83,51,71,17,betbuf,C_GOLD,false);
  if(rouletteBetType==RB_NUMBER) {
    snprintf(betbuf,sizeof(betbuf),"NUMBER %u",rouletteNumber);
    drawButton(6,73,70,17,betbuf,C_CYAN,false);
  }
  uint8_t shown=rouletteSpinning?rouletteVisualNumber:rouletteResult;
  uint16_t ncol=shown==0?C_GREEN:(isRed(shown)?C_RED:C_BLACK);
  tft.fillCircle(80,88,20,C_GOLD); tft.fillCircle(80,88,16,ncol);
  char nbuf[4]; snprintf(nbuf,sizeof(nbuf),"%u",shown);
  tft.setTextSize(2); tft.setTextColor(C_WHITE,ncol);
  int16_t x1,y1;uint16_t w,h;tft.getTextBounds(nbuf,0,0,&x1,&y1,&w,&h);
  tft.setCursor(80-w/2,81);tft.print(nbuf);
  if(rouletteSpinning) centerText("SPINNING...",111,1,C_CYAN);
  else if(rouletteDone) centerText(rouletteWin>0?"WIN!":"LOSE",111,1,rouletteWin>0?C_GREEN:C_RED);
  else centerText("UP/DN TYPE  SEL SPIN",111,1,C_WHITE);
  drawNotification();
}
void updateRoulette() {
  if(!rouletteSpinning) {
    if(hit(BTN_UP)) rouletteBetType=(rouletteBetType+1)%RB_COUNT;
    if(hit(BTN_DOWN)) rouletteBetIndex=(rouletteBetIndex+1)%4;
    if(hit(BTN_SELECT)) startRoulette();
    if(hit(BTN_BACK)) screen=SCR_MENU;
    if(rouletteBetType==RB_NUMBER && hit(BTN_UP)) rouletteNumber=(rouletteNumber+1)%37;
    return;
  }
  uint32_t elapsed=millis()-rouletteStartedAt;
  if(elapsed>=rouletteDuration) {
    rouletteVisualNumber=rouletteResult;
    finishRoulette();
  } else {
    uint32_t interval=35+(elapsed*elapsed)/(rouletteDuration*rouletteDuration/10+1);
    if(millis()-rouletteLastTick>=interval) {
      rouletteLastTick=millis();
      rouletteVisualNumber=randomBounded(37);
    }
  }
  if(hit(BTN_BACK)) { /* committed spin continues */ }
}

// ---------------------------- WALLET / COLLECTION --------------------
void drawWallet() {
  tft.fillScreen(C_BG); drawHeader("WALLET");
  centerText("COINS",25,1,C_GOLD);
  char buf[24]; snprintf(buf,sizeof(buf),"%ld",(long)saveData.coins);
  centerText(buf,39,2,C_WHITE);
  drawPanel(7,65,146,54,C_CYAN);
  snprintf(buf,sizeof(buf),"WINS %lu",(unsigned long)saveData.wins); drawSmallLabel(buf,15,73,C_GREEN);
  snprintf(buf,sizeof(buf),"LOSSES %lu",(unsigned long)saveData.losses); drawSmallLabel(buf,82,73,C_RED);
  snprintf(buf,sizeof(buf),"BIG WIN %lu",(unsigned long)saveData.biggestWin); drawSmallLabel(buf,15,88,C_GOLD);
  snprintf(buf,sizeof(buf),"STREAK %u / %u",saveData.currentStreak,saveData.bestStreak); drawSmallLabel(buf,15,103,C_CYAN);
  drawNotification();
}
const char* COLLECTION_NAMES[]={"LUCKY 7","GOLD BAR","DIAMOND","CHERRY","JACKPOT","BLACK CAT","CROWN","NEON STAR","MYSTERY CHIP"};
const uint16_t COLLECTION_COLORS[]={C_RED,C_GOLD,C_CYAN,C_PINK,C_YELLOW,C_MUTED,C_GOLD,C_PURPLE,C_GREEN};
const char* RARITIES[]={"COMMON","UNCOMMON","RARE","EPIC","LEGENDARY","MYTHIC"};
void drawCollection() {
  tft.fillScreen(C_BG); drawHeader("COLLECTION");
  for(uint8_t i=0;i<9;i++) {
    int x=7+(i%3)*51, y=28+(i/3)*29;
    bool unlocked=(saveData.collection&(1U<<i));
    drawPanel(x,y,44,24,unlocked?COLLECTION_COLORS[i]:C_MUTED);
    tft.setTextSize(1); tft.setTextColor(unlocked?COLLECTION_COLORS[i]:C_MUTED,C_PANEL);
    tft.setCursor(x+4,y+4); tft.print(unlocked?COLLECTION_NAMES[i]:"???");
    tft.setCursor(x+4,y+15); tft.print(unlocked?"UNLOCKED":"LOCKED");
  }
  centerText("UNLOCK THROUGH PLAY",117,1,C_CYAN);
  drawNotification();
}
const char* ACH_NAMES[]={"FIRST SPIN","FIRST WIN","WIN 10 TIMES","WIN 1000 COINS","JACKPOT","PLAY 50 GAMES","25K COINS","100K COINS","5 WIN STREAK","5 LOSS STREAK","LEGENDARY","MYTHIC"};
const uint16_t ACH_BITS[]={0,1,2,3,4,5,6,7,8,9,10,11};
void drawAchievements() {
  tft.fillScreen(C_BG); drawHeader("ACHIEVEMENTS");
  for(uint8_t i=0;i<6;i++) {
    uint8_t idx=(listOffset+i)%12;
    int y=27+i*15;
    bool unlocked=(saveData.achievements&(1U<<ACH_BITS[idx]));
    tft.fillRoundRect(7,y,146,13,2,unlocked?C_PANEL2:C_PANEL);
    tft.drawRoundRect(7,y,146,13,2,unlocked?C_GOLD:C_MUTED);
    tft.setTextSize(1); tft.setTextColor(unlocked?C_GOLD:C_MUTED,unlocked?C_PANEL2:C_PANEL);
    tft.setCursor(12,y+3); tft.print(unlocked?ACH_NAMES[idx]:"???");
    tft.setCursor(139,y+3); tft.print(unlocked?"*":"?");
  }
  drawSmallLabel("UP/DN SCROLL  BACK",20,119,C_CYAN);
  drawNotification();
}
void unlockRandomCollection() {
  uint8_t locked[9], n=0;
  for(uint8_t i=0;i<9;i++) if(!(saveData.collection&(1U<<i))) locked[n++]=i;
  if(!n) return;
  uint8_t idx=locked[randomBounded(n)];
  saveData.collection |= (1U<<idx);
  if(idx==4) saveData.achievements |= (1U<<10);
  if(idx==8) saveData.achievements |= (1U<<11);
  char msg[32]; snprintf(msg,sizeof(msg),"ITEM: %s",COLLECTION_NAMES[idx]);
  popup(msg,C_GOLD,2400); saveNow();
}

// ---------------------------- SETTINGS --------------------------------
uint8_t settingsIndex=0;
const char* SETTINGS_ITEMS[]={"SOUND","ANIMATIONS","BRIGHTNESS","RESET DATA","ABOUT"};
void drawSettings() {
  tft.fillScreen(C_BG); drawHeader("SETTINGS");
  for(uint8_t i=0;i<5;i++) {
    int y=34+i*17; bool sel=i==settingsIndex;
    if(sel){tft.fillRoundRect(8,y-3,144,15,3,C_PURPLE);tft.drawRoundRect(8,y-3,144,15,3,C_PINK);}
    tft.setTextSize(1);tft.setTextColor(sel?C_YELLOW:C_WHITE,sel?C_PURPLE:C_BG);
    tft.setCursor(15,y);tft.print(sel?"> ":"  ");tft.print(SETTINGS_ITEMS[i]);
    if(i==0){tft.setCursor(112,y);tft.print(saveData.soundOn?"ON":"OFF");}
    if(i==1){tft.setCursor(112,y);tft.print(saveData.animationsOn?"ON":"OFF");}
    if(i==2){tft.setCursor(112,y);tft.print(saveData.brightness);tft.print("%");}
  }
  drawNotification();
}
void handleSettings() {
  if(hit(BTN_UP)&&settingsIndex>0) settingsIndex--;
  if(hit(BTN_DOWN)&&settingsIndex<4) settingsIndex++;
  if(hit(BTN_BACK)){screen=SCR_MENU;return;}
  if(!hit(BTN_SELECT)) return;
  switch(settingsIndex) {
    case 0: saveData.soundOn=!saveData.soundOn; saveNow(); break;
    case 1: saveData.animationsOn=!saveData.animationsOn; saveNow(); break;
    case 2: saveData.brightness+=10; if(saveData.brightness>100)saveData.brightness=10; saveNow(); popup("BRIGHTNESS SAVED",C_CYAN); break;
    case 3: screen=SCR_RESET_CONFIRM; resetArmed=false; break;
    case 4: screen=SCR_ABOUT; break;
  }
}
void drawResetConfirm() {
  tft.fillScreen(C_BG);drawHeader("RESET DATA");
  centerText("DELETE ALL SAVE DATA?",42,1,C_RED);
  centerText("COINS + STATS + ITEMS",58,1,C_WHITE);
  drawButton(12,77,62,22,"CANCEL",C_CYAN,!resetArmed);
  drawButton(86,77,62,22,"DELETE",C_RED,resetArmed);
  centerText("UP/DN CHOOSE SEL OK",108,1,C_MUTED);
}
void drawAbout() {
  tft.fillScreen(C_BG);drawHeader("ABOUT");
  centerText("LUCKY S3",38,2,C_GOLD);
  centerText("OFFLINE ARCADE GAME",65,1,C_CYAN);
  centerText("VIRTUAL COINS ONLY",80,1,C_WHITE);
  centerText("NO REAL MONEY",95,1,C_PINK);
  centerText("BACK TO RETURN",116,1,C_MUTED);
}

// ---------------------------- LUCK / RANDOM EVENTS -------------------
void maybeRandomEvent() {
  uint32_t now=millis();
  if(now-lastEventAt<45000) return;
  if(randomBounded(100)<4) {
    lastEventAt=now;
    switch(randomBounded(4)) {
      case 0: addCoins(100); popup("LUCKY MOMENT +100",C_GOLD,2200); break;
      case 1: unlockRandomCollection(); popup("MYSTERY CHEST!",C_PURPLE,2200); break;
      case 2: luckValue=95; popup("VERY LUCKY!",C_GREEN,2200); break;
      default: luckValue=22; popup("UNLUCKY...",C_RED,2200); break;
    }
  }
}
void drawLuckBadge() {
  tft.fillRoundRect(96,21,60,16,3,C_PANEL2);
  tft.drawRoundRect(96,21,60,16,3,C_PURPLE);
  tft.setTextSize(1);tft.setTextColor(C_PURPLE,C_PANEL2);
  tft.setCursor(100,26);tft.print("LUCK ");tft.print(luckValue);
}

// ---------------------------- INPUT / ROUTING ------------------------
void enterMenuItem() {
  switch(menuIndex) {
    case 0: screen=SCR_SLOTS; break;
    case 1: screen=SCR_BLACKJACK; break;
    case 2: screen=SCR_ROULETTE; break;
    case 3: screen=SCR_WALLET; break;
    case 4: screen=SCR_COLLECTION; break;
    case 5: screen=SCR_ACHIEVEMENTS; listOffset=0; break;
    case 6: screen=SCR_SETTINGS; settingsIndex=0; break;
  }
  screenStartedAt=millis();
}
void updateInput() {
  switch(screen) {
    case SCR_MENU:
      if(hit(BTN_UP)&&menuIndex>0)menuIndex--;
      if(hit(BTN_DOWN)&&menuIndex<MENU_COUNT-1)menuIndex++;
      if(hit(BTN_SELECT))enterMenuItem();
      break;
    case SCR_SLOTS:updateSlots();break;
    case SCR_BLACKJACK:updateBlackjack();break;
    case SCR_ROULETTE:updateRoulette();break;
    case SCR_WALLET:
    case SCR_COLLECTION:
      if(hit(BTN_BACK)||hit(BTN_SELECT))screen=SCR_MENU;
      if(screen==SCR_COLLECTION && hit(BTN_UP))unlockRandomCollection();
      break;
    case SCR_ACHIEVEMENTS:
      if(hit(BTN_UP)&&listOffset>0)listOffset--;
      if(hit(BTN_DOWN)&&listOffset<6)listOffset++;
      if(hit(BTN_BACK)||hit(BTN_SELECT))screen=SCR_MENU;
      break;
    case SCR_SETTINGS:handleSettings();break;
    case SCR_RESET_CONFIRM:
      if(hit(BTN_UP)||hit(BTN_DOWN))resetArmed=!resetArmed;
      if(hit(BTN_BACK))screen=SCR_SETTINGS;
      if(hit(BTN_SELECT)) {
        if(resetArmed){resetSaveData();screen=SCR_MENU;popup("DATA RESET",C_GREEN);}
        else screen=SCR_SETTINGS;
      }
      break;
    case SCR_ABOUT:if(hit(BTN_BACK)||hit(BTN_SELECT))screen=SCR_SETTINGS;break;
    case SCR_DAILY:
      if(hit(BTN_SELECT))claimDailyBonus();
      else if(hit(BTN_BACK)){dailyAvailable=false;prefs.putUChar("claimed",1);screen=SCR_MENU;}
      break;
    default:break;
  }
}
void drawDaily() {
  tft.fillScreen(C_BG);drawHeader("DAILY BONUS");
  centerText("FREE ARCADE GIFT",35,1,C_CYAN);
  char buf[24];snprintf(buf,sizeof(buf),"+%u COINS",saveData.dailyStreak==0?100:(saveData.dailyStreak==1?150:250));
  centerText(buf,56,2,C_GOLD);
  drawButton(28,78,104,22,"SELECT TO CLAIM",C_GREEN,true);
  centerText("BACK TO SKIP",108,1,C_MUTED);
}

void drawCurrentScreen() {
  switch(screen) {
    case SCR_BOOT:drawBoot();break;
    case SCR_MENU:drawMenu();break;
    case SCR_SLOTS:drawSlots();break;
    case SCR_BLACKJACK:drawBlackjack();break;
    case SCR_ROULETTE:drawRoulette();break;
    case SCR_WALLET:drawWallet();break;
    case SCR_COLLECTION:drawCollection();break;
    case SCR_ACHIEVEMENTS:drawAchievements();break;
    case SCR_SETTINGS:drawSettings();break;
    case SCR_RESET_CONFIRM:drawResetConfirm();break;
    case SCR_ABOUT:drawAbout();break;
    case SCR_DAILY:drawDaily();break;
  }
}

void setup() {
  Serial.begin(115200);
  delay(20); // startup-only settle, not gameplay animation
  Serial.println("LUCKY S3 STARTING");

  // Preserve the known project configuration and explicitly set SPI pins.
  SPI.begin(TFT_SCLK, -1, TFT_MOSI, TFT_CS);
  tft.initR(INITR_BLACKTAB);
  tft.setRotation(1); // 160x128 landscape
  tft.fillScreen(C_BG);
  Serial.println("DISPLAY OK");

  initButtons();
  prefs.begin("lucky-s3", false);
  loadSaveData();
  Serial.println("SAVE SYSTEM OK");

  bootStartedAt=millis();
  screen=SCR_BOOT;
  Serial.println("GAME READY");
}

void loop() {
  pollButtons();
  uint32_t now=millis();

  if(screen==SCR_BOOT && now-bootStartedAt>=2000) {
    checkDailyBonus();
    if(!dailyAvailable) screen=SCR_MENU;
  }

  updateInput();
  if(screen==SCR_MENU) maybeRandomEvent();

  if(now-lastFrameAt>=FRAME_MS) {
    lastFrameAt=now;
    drawCurrentScreen();
  }
}
