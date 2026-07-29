#define ENABLE_GxEPD2_GFX 1
#include <GxEPD2_BW.h>
#include <Adafruit_GFX.h>
#include <Wire.h>
#include <RTClib.h>
#include <Adafruit_VL53L0X.h> 
#include <DHT.h> 

#define EPD_CS     5
#define EPD_DC     17
#define EPD_RST    16
#define EPD_BUSY   4

#define BTN_UP     32
#define BTN_LEFT   33
#define BTN_RIGHT  25
#define BTN_DOWN   26  

#define BUZZER_PIN   27
#define BUZZER_FREQ  3000 
#define LEDC_RES     8    

#define LED_BACKLIGHT_PIN 14

#define DHTPIN 13
#define DHTTYPE DHT11

GxEPD2_BW<GxEPD2_213_B74, GxEPD2_213_B74::HEIGHT> display(GxEPD2_213_B74(EPD_CS, EPD_DC, EPD_RST, EPD_BUSY));

RTC_DS3231 rtc;
Adafruit_VL53L0X lox = Adafruit_VL53L0X();
DHT dht(DHTPIN, DHTTYPE); 

const char* days[] = {"SUNDAY", "MONDAY", "TUESDAY", "WEDNESDAY", "THURSDAY", "FRIDAY", "SATURDAY"};
const char* months[] = {
  "JANUARY", "FEBRUARY", "MARCH", "APRIL", "MAY", "JUNE", 
  "JULY", "AUGUST", "SEPTEMBER", "OCTOBER", "NOVEMBER", "DECEMBER"
};

enum AppState { SCREEN_CLOCK, SCREEN_MENU };
AppState currentState = SCREEN_CLOCK;

int alarmHour = 7;
int alarmMinute = 0;
bool alarmEnabled = false;
bool alarmActive = false; 
int activeSelection = 0;   

int lastMinute = -1;
unsigned long lastDebounceTime = 0;
const int debounceDelay = 200; 

unsigned long lastBuzzerToggle = 0;
bool buzzerState = false;

unsigned long ledTurnOnTime = 0;
const unsigned long ledDuration = 5000; 
bool ledIsOn = false;
unsigned long lastGestureTime = 0; 

float temperature = 0.0;
float humidity = 0.0;
unsigned long lastDHTReadTime = 0;
const unsigned long dhtReadInterval = 2000; 

void startBuzzer() {
  ledcWriteTone(BUZZER_PIN, BUZZER_FREQ);
}

void stopBuzzer() {
  ledcWriteTone(BUZZER_PIN, 0);
}

void drawAlarmIcon(int x, int y) {
  display.drawCircle(x, y, 6, GxEPD_BLACK);
  display.drawLine(x - 4, y + 5, x - 6, y + 8, GxEPD_BLACK);
  display.drawLine(x + 4, y + 5, x + 6, y + 8, GxEPD_BLACK);
  display.drawCircle(x - 5, y - 5, 2, GxEPD_BLACK);
  display.drawCircle(x + 5, y - 5, 2, GxEPD_BLACK);
  display.drawLine(x, y, x, y - 3, GxEPD_BLACK);       
  display.drawLine(x, y, x + 3, y + 1, GxEPD_BLACK);   
}

void drawShiftedCenteredText(const char* text, int y, int textSize) {
  display.setTextSize(textSize);
  int16_t x1, y1;
  uint16_t w, h;
  display.getTextBounds(text, 0, 0, &x1, &y1, &w, &h);
  int centerX = 130; 
  int x = centerX - (w / 2);
  display.setCursor(x, y);
  display.print(text);
}

void drawClockFace(DateTime now) {
  display.fillScreen(GxEPD_WHITE);
  display.setTextColor(GxEPD_BLACK);

  int blatX = 20; 
  int blatY = 0; 
  int blatW = 220; 
  int blatH = 120; 
  display.drawRect(blatX, blatY, blatW, blatH, GxEPD_BLACK);

  drawShiftedCenteredText(days[now.dayOfTheWeek()], 16, 1);

  char dateBuf[24];
  sprintf(dateBuf, "%d %s", now.day(), months[now.month() - 1]);
  drawShiftedCenteredText(dateBuf, 30, 1);

  char timeBuf[10];
  sprintf(timeBuf, "%02d:%02d", now.hour(), now.minute());
  drawShiftedCenteredText(timeBuf, 48, 2);

  char yearBuf[10];
  sprintf(yearBuf, "%04d", now.year());
  drawShiftedCenteredText(yearBuf, 72, 1);

  char weatherBuf[24];
  if (isnan(temperature) || isnan(humidity)) {
    sprintf(weatherBuf, "--C | --%%");
  } else {
    sprintf(weatherBuf, "%dC | %d%%", (int)temperature, (int)humidity);
  }
  drawShiftedCenteredText(weatherBuf, 86, 1);

  if (alarmEnabled) {
    drawAlarmIcon(220, 16); 
  }

  int barW = 100;
  int barX = 130 - (barW / 2);
  int barY = 104;
  int barH = 5;
  display.drawRect(barX, barY, barW, barH, GxEPD_BLACK);
  int fillW = (now.second() / 60.0) * (barW - 2); 
  if (fillW > 0) {
    display.fillRect(barX + 1, barY + 1, fillW, barH - 2, GxEPD_BLACK);
  }
}

void drawMenuFace() {
  display.fillScreen(GxEPD_WHITE);
  display.setTextColor(GxEPD_BLACK);

  drawShiftedCenteredText("ALARM SETTINGS", 10, 1);
  display.drawLine(70, 20, 190, 20, GxEPD_BLACK);

  char alarmTimeBuf[10];
  sprintf(alarmTimeBuf, "%02d:%02d", alarmHour, alarmMinute);
  drawShiftedCenteredText(alarmTimeBuf, 30, 3);

  if (alarmEnabled) {
    drawShiftedCenteredText("STATUS: ENABLED", 58, 1);
  } else {
    drawShiftedCenteredText("STATUS: DISABLED", 58, 1);
  }

  drawShiftedCenteredText("[ SAVE AND EXIT ]", 72, 1);

  if (activeSelection == 0) display.fillRect(92, 54, 30, 3, GxEPD_BLACK);       
  else if (activeSelection == 1) display.fillRect(138, 54, 30, 3, GxEPD_BLACK);  
  else if (activeSelection == 2) display.fillRect(100, 68, 60, 2, GxEPD_BLACK);  
  else if (activeSelection == 3) display.fillRect(78, 82, 104, 2, GxEPD_BLACK);  

  drawShiftedCenteredText("< >:Navigate   ^ v:Change", 88, 1);
}

void forceScreenUpdate(bool fullRefresh) {
  if (fullRefresh) {
    display.setFullWindow(); 
  } else {
    display.setPartialWindow(0, 0, display.width(), display.height()); 
  }
  
  display.firstPage();
  do {
    if (currentState == SCREEN_CLOCK) {
      DateTime now = rtc.now();
      drawClockFace(now);
    } else {
      drawMenuFace();
    }
  } while (display.nextPage());
  display.powerOff();
}

void setup() {
  Serial.begin(115200);
  delay(1000);

  pinMode(BTN_UP, INPUT_PULLUP);
  pinMode(BTN_LEFT, INPUT_PULLUP);
  pinMode(BTN_RIGHT, INPUT_PULLUP);
  pinMode(BTN_DOWN, INPUT_PULLUP);
  
  pinMode(LED_BACKLIGHT_PIN, OUTPUT);
  digitalWrite(LED_BACKLIGHT_PIN, LOW);

  ledcAttach(BUZZER_PIN, BUZZER_FREQ, LEDC_RES);
  stopBuzzer();

  Wire.begin();

  if (!rtc.begin()) {
    while (1) delay(10);
  }

  if (rtc.lostPower()) {
    rtc.adjust(DateTime(F(__DATE__), F(__TIME__)));
  }

  if (!lox.begin()) {
    Serial.println(F("VL53L0X init error!"));
  }

  dht.begin();

  display.init(115200);
  display.setRotation(1);
  
  display.setFullWindow();
  display.firstPage();
  do {
    display.fillScreen(GxEPD_WHITE);
    display.setTextColor(GxEPD_BLACK);
    
    drawShiftedCenteredText("*****************", 22, 1);
    drawShiftedCenteredText("* Clockie v 1.0 *", 36, 2); 
    drawShiftedCenteredText("*  loading...   *", 56, 1);
    drawShiftedCenteredText("*  by Michal:DD *", 70, 1);
    drawShiftedCenteredText("*****************", 84, 1);
  } while (display.nextPage());
  display.powerOff();
  delay(3000); 
  
  temperature = dht.readTemperature();
  humidity = dht.readHumidity();

  forceScreenUpdate(true); 
}

void loop() {
  if (millis() - lastDHTReadTime >= dhtReadInterval) {
    lastDHTReadTime = millis();
    float t = dht.readTemperature();
    float h = dht.readHumidity();
    if (!isnan(t) && !isnan(h)) {
      temperature = t;
      humidity = h;
    }
  }

  VL53L0X_RangingMeasurementData_t measure;
  lox.rangingTest(&measure, false); 

  if (measure.RangeStatus == 0) { 
    int distanceMm = measure.RangeMilliMeter;

    if (distanceMm > 45 && distanceMm <= 110 && (millis() - lastGestureTime > 500)) {
      lastGestureTime = millis(); 

      if (alarmActive) {
        alarmActive = false;
        alarmEnabled = false; 
        stopBuzzer();
        digitalWrite(LED_BACKLIGHT_PIN, LOW);
        ledIsOn = false;
        Serial.println("Alarm disabled by gesture!");
        forceScreenUpdate(true); 
      } 
      else {
        if (!ledIsOn) {
          digitalWrite(LED_BACKLIGHT_PIN, HIGH);
          ledIsOn = true;
          ledTurnOnTime = millis(); 
          Serial.println("Backlight ON");
        } else {
          digitalWrite(LED_BACKLIGHT_PIN, LOW);
          ledIsOn = false;
          Serial.println("Backlight OFF");
        }
      }
    }
  }

  if (ledIsOn && !alarmActive) {
    if (millis() - ledTurnOnTime >= ledDuration) {
      digitalWrite(LED_BACKLIGHT_PIN, LOW);
      ledIsOn = false;
      Serial.println("Backlight OFF (Timeout)");
    }
  }

  if ((millis() - lastDebounceTime) > debounceDelay) {
    if (alarmActive && (digitalRead(BTN_DOWN) == LOW || digitalRead(BTN_UP) == LOW || digitalRead(BTN_LEFT) == LOW || digitalRead(BTN_RIGHT) == LOW)) {
      lastDebounceTime = millis();
      alarmActive = false;
      alarmEnabled = false; 
      stopBuzzer();
      forceScreenUpdate(true);
      return;
    }

    if (currentState == SCREEN_CLOCK) {
      if (digitalRead(BTN_DOWN) == LOW) {
        lastDebounceTime = millis();
        currentState = SCREEN_MENU;
        activeSelection = 0; 
        forceScreenUpdate(true); 
      }
    } 
    else if (currentState == SCREEN_MENU) {
      bool menuChanged = false;

      if (digitalRead(BTN_LEFT) == LOW) {
        lastDebounceTime = millis();
        activeSelection--;
        if (activeSelection < 0) activeSelection = 3;
        menuChanged = true;
      }
      else if (digitalRead(BTN_RIGHT) == LOW) {
        lastDebounceTime = millis();
        activeSelection++;
        if (activeSelection > 3) activeSelection = 0;
        menuChanged = true;
      }
      else if (digitalRead(BTN_UP) == LOW) {
        lastDebounceTime = millis();
        switch(activeSelection) {
          case 0: alarmHour = (alarmHour + 1) % 24; break;
          case 1: alarmMinute = (alarmMinute + 5) % 60; break;
          case 2: alarmEnabled = !alarmEnabled; break;
          case 3: 
            currentState = SCREEN_CLOCK;
            lastMinute = -1; 
            forceScreenUpdate(true); 
            return;
        }
        menuChanged = true;
      }
      else if (digitalRead(BTN_DOWN) == LOW) {
        lastDebounceTime = millis();
        switch(activeSelection) {
          case 0: 
            alarmHour--;
            if (alarmHour < 0) alarmHour = 23;
            break;
          case 1: 
            alarmMinute -= 5;
            if (alarmMinute < 0) alarmMinute = 55;
            break;
          case 2: alarmEnabled = !alarmEnabled; break;
          case 3: 
            currentState = SCREEN_CLOCK;
            lastMinute = -1; 
            forceScreenUpdate(true); 
            return;
        }
        menuChanged = true;
      }

      if (menuChanged) {
        forceScreenUpdate(false); 
      }
    }
  }

  if (currentState == SCREEN_CLOCK) {
    DateTime now = rtc.now();

    if (alarmEnabled && now.hour() == alarmHour && now.minute() == alarmMinute) {
      if (!alarmActive && now.second() < 5) { 
        alarmActive = true;
      }
    } else {
      if (now.minute() != alarmMinute && alarmActive) {
        alarmActive = false;
        stopBuzzer();
      }
    }

    if (now.minute() != lastMinute) {
      lastMinute = now.minute();
      if (now.minute() == 0) display.setFullWindow(); 
      else display.setPartialWindow(0, 0, display.width(), display.height());
      
      display.firstPage();
      do {
        drawClockFace(now);
      } while (display.nextPage());
      display.powerOff();
    }
  }

  if (alarmActive) {
    if (millis() - lastBuzzerToggle >= 150) { 
      lastBuzzerToggle = millis();
      buzzerState = !buzzerState;
      if (buzzerState) {
        startBuzzer();
        digitalWrite(LED_BACKLIGHT_PIN, HIGH); 
      } else {
        stopBuzzer();
        digitalWrite(LED_BACKLIGHT_PIN, LOW);
      }
    }
  } else if (!ledIsOn) {
    stopBuzzer();
  }

  delay(10); 
}
