/*
 * Web enabled FFT VU meter for a matrix, ESP32 and INMP441 digital mic.
 * The matrix width MUST be either 8 or a multiple of 16 but the height can
 * be any value. E.g. 8x8, 16x16, 8x10, 32x9 etc.
 * 
 * We are using the LEDMatrx library for easy setup of a matrix with various
 * wiring options. Options are:
 *  HORIZONTAL_ZIGZAG_MATRIX
 *  HORIZONTAL_MATRIX
 *  VERTICAL_ZIGZAG_MATRIX
 *  VERTICAL_MATRIX
 * If your matrix has the first pixel somewhere other than the bottom left
 * (default) then you can reverse the X or Y axis by writing -M_WIDTH and /
 * or -M_HEIGHT in the cLEDMatrix initialisation.
 * 
 * REQUIRED LIBRARIES
 * FastLED            Arduino libraries manager
 * ArduinoFFT         Arduino libraries manager
 * EEPROM             Built in
 * WiFi               Built in
 * AsyncTCP           https://github.com/me-no-dev/ESPAsyncWebServer
 * ESPAsyncWebServer  https://github.com/me-no-dev/AsyncTCP
 * LEDMatrix          https://github.com/AaronLiddiment/LEDMatrix
 * LEDText            https://github.com/AaronLiddiment/LEDText
 * 
 * WIRING
 * LED data     D2 via 470R resistor
 * GND          GND
 * Vin          5V
 * 
 * INMP441
 * VDD          3V3
 * GND          GND
 * L/R          GND
 * WS           D15
 * SCK          D14     
 * SD           D32
 * 
 * REFERENCES
 * Main code      Scott Marley            https://www.youtube.com/c/ScottMarley
 * Web server     Random Nerd Tutorials   https://randomnerdtutorials.com/esp32-web-server-slider-pwm/
 *                                  and   https://randomnerdtutorials.com/esp32-websocket-server-arduino/
 * Audio and mic  Andrew Tuline et al     https://github.com/atuline/WLED
 */

#include "main.h"

/*
  *************MACRO**************
*/
#define EEPROM_SIZE 5
#define LED_PIN     2
#define M_WIDTH     16
#define M_HEIGHT    16
#define NUM_LEDS    (M_WIDTH * M_HEIGHT)

#define EEPROM_BRIGHTNESS   0
#define EEPROM_GAIN         1
#define EEPROM_SQUELCH      2
#define EEPROM_PATTERN      3
#define EEPROM_DISPLAY_TIME 4

// BUTTON
#define BTN_UP              19
#define BTN_DOWN            18
#define BTN_SWITCH          5
#define BTN_SAVE            7

// MODE
#define NORMAL_MODE          0
#define SHOW_PEAK_MODE       1
#define ADJUST_TIME_MODE     2
#define DISPLAY_DATE_MODE    3
#define ADJUST_DATE_MODE     4
#define TOTAL_MODES          5  // Tổng số chế độ


#define MIN_YEAR 2000
#define MAX_YEAR 2099

int mode = NORMAL_MODE;


uint8_t numBands;
uint8_t barWidth;
uint8_t pattern;
uint8_t brightness;
uint16_t displayTime;
bool autoChangePatterns = false;

#include "web_server.h"

cLEDMatrix<M_WIDTH, M_HEIGHT, HORIZONTAL_ZIGZAG_MATRIX> leds;
cLEDText ScrollingMsg;

uint8_t peak[] = {0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0};
uint8_t prevFFTValue[] = {0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0};
uint8_t barHeights[] = {0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0};


struct Time
{
  uint8_t hours;
  uint8_t mins; 
  uint8_t seconds;
  uint8_t days;
  uint8_t months;
  uint16_t years;
};

enum ModeField { HOUR = 0, MINUTE, SECOND, DAY, MONTH, YEAR };
int currentTimeField = 0; // 0 = Giờ, 1 = Phút, 2 = Giây
int currentDateField = 0; // 0 = Ngày, 1 = Tháng, 2 = Năm

// DS1307 instance
RTC_DS1307  rtc;

int currentField = 0; // 0 = Giờ, 1 = Phút, 2 = Giây
bool blinkState = true; // Trạng thái nhấp nháy
unsigned long lastBlinkTime = 0;
unsigned long lastUpdateTime = 0;
unsigned long blinkInterval = 500; // Khoảng thời gian nhấp nháy (500ms)
unsigned long timeInterval = 1000;


/*Button*/
struct Button
{
  uint8_t buttonPin;
  uint8_t lastStatePin; 
};

enum { None, SingleClick, DoubleClick, YesSingle };


// Server NTP
const char* ntpServer = "pool.ntp.org";
const long  gmtOffset_sec = 7 * 3600;  // GMT +7 cho Việt Nam
const int   daylightOffset_sec = 0;    // Không có giờ tiết kiệm ánh sáng ngày

// Colors and palettes
DEFINE_GRADIENT_PALETTE( purple_gp ) {
  0,   0, 212, 255,   //blue
255, 179,   0, 255 }; //purple
DEFINE_GRADIENT_PALETTE( outrun_gp ) {
  0, 141,   0, 100,   //purple
127, 255, 192,   0,   //yellow
255,   0,   5, 255 };  //blue
DEFINE_GRADIENT_PALETTE( greenblue_gp ) {
  0,   0, 255,  60,   //green
 64,   0, 236, 255,   //cyan
128,   0,   5, 255,   //blue
192,   0, 236, 255,   //cyan
255,   0, 255,  60 }; //green
DEFINE_GRADIENT_PALETTE( redyellow_gp ) {
  0,   200, 200,  200,   //white
 64,   255, 218,    0,   //yellow
128,   231,   0,    0,   //red
192,   255, 218,    0,   //yellow
255,   200, 200,  200 }; //white
CRGBPalette16 purplePal = purple_gp;
CRGBPalette16 outrunPal = outrun_gp;
CRGBPalette16 greenbluePal = greenblue_gp;
CRGBPalette16 heatPal = redyellow_gp;
uint8_t colorTimer = 0;

Time currentTime;
Button buttonUp = {.buttonPin = 19, .lastStatePin = HIGH};
Button buttonDown = {.buttonPin = 18, .lastStatePin = HIGH};
Button buttonSwitch = {.buttonPin = 5, .lastStatePin = HIGH};
Button buttonSave = {.buttonPin = 17, .lastStatePin = HIGH};

int lastButtonState = HIGH; 
/*
    PROTOTYPE
*/
int isButtonPressed(Button buttonPin);
void showIP();
void getTime();
void showPeak();
void displayDay();
void displayTimeMode();
void updateTime();

void setup() {
  FastLED.addLeds<WS2812B, LED_PIN, GRB>(leds[0], NUM_LEDS);
  Serial.begin(115200);

  // button initalize
  pinMode(BTN_UP, INPUT_PULLUP);
  pinMode(BTN_DOWN, INPUT_PULLUP);
  pinMode(BTN_SWITCH, INPUT_PULLUP);
  pinMode(BTN_SAVE, INPUT_PULLUP);
  
  setupWebServer();
  setupAudio();
  getTime();

  if (M_WIDTH == 8) numBands = 8;
  else numBands = 16;
  barWidth = M_WIDTH / numBands;
  
  EEPROM.begin(EEPROM_SIZE);
  

  if (EEPROM.read(EEPROM_GAIN) == 255) {
    EEPROM.write(EEPROM_BRIGHTNESS, 50);
    EEPROM.write(EEPROM_GAIN, 0);
    EEPROM.write(EEPROM_SQUELCH, 0);
    EEPROM.write(EEPROM_PATTERN, 0);
    EEPROM.write(EEPROM_DISPLAY_TIME, 10);
    EEPROM.commit();
  }

  // Read saved values from EEPROM
  FastLED.setBrightness( EEPROM.read(EEPROM_BRIGHTNESS));
  brightness = FastLED.getBrightness();
  gain = EEPROM.read(EEPROM_GAIN);
  squelch = EEPROM.read(EEPROM_SQUELCH);
  pattern = EEPROM.read(EEPROM_PATTERN);
  displayTime = EEPROM.read(EEPROM_DISPLAY_TIME);

  if (WiFi.status() == WL_CONNECTED) showIP();
}  

void loop() {
  checkButtons(); // Kiểm tra trạng thái nút nhấn

  if (mode == NORMAL_MODE) {
    displayTimeMode();
  }
  else if (mode == SHOW_PEAK_MODE) {
    showPeak();
  }
  else if (mode == ADJUST_TIME_MODE) {
    displayBlinkingTime(); // Chế độ điều chỉnh thời gian
  }
  else if (mode == DISPLAY_DATE_MODE) {
    displayDay();
  }
  else if (mode == ADJUST_DATE_MODE) {
    displayBlinkingDate(); // Chế độ điều chỉnh ngày tháng
  }

  updateTime();
  ws.cleanupClients();
}


int isButtonPressed(Button buttonPin)
{
  const  unsigned long ButTimeout  = 250;
  static unsigned long msecLst;
  unsigned long msec = millis ();
  const int debDuration = 100;
  static unsigned long  debStartTime = 0;

  if (msecLst && (msec - msecLst) > ButTimeout)  {
    msecLst = 0;
	//GERRY MOD
    //return SingleClick;	
    return YesSingle;
  }

  byte but = digitalRead (buttonPin.buttonPin);
  if (buttonPin.lastStatePin != but)  {
    //GERRY MOD
    if (millis() - debStartTime < debDuration) {
      return None;
    }
    debStartTime = millis();
	
    buttonPin.lastStatePin = but;

    if (LOW == but)  {   // press
      if (msecLst)  { // 2nd press
        msecLst = 0;
        return DoubleClick;
      }
      else {
        msecLst = 0 == msec ? 1 : msec;
		//GERRY MOD
        return SingleClick; //SINGLE?
      }
    }
  }

  return None;
}

// Hàm để kiểm tra nút nhấn
void checkButtons() {

  // Kiểm tra nút chuyển đổi chế độ
  if (isButtonPressed(buttonSwitch) == SingleClick) {
    mode = (mode + 1) % 5; // Chuyển đổi giữa các chế độ 0 đến 4
  }

  /// Xử lý nút nhấn dựa trên chế độ hiện tại
  if (mode == ADJUST_TIME_MODE) {
    // Chế độ điều chỉnh giờ/phút/giây
    if (isButtonPressed(buttonUp) == SingleClick) {
      adjustCurrentField(1); // Tăng giá trị
    } else if (isButtonPressed(buttonDown) == SingleClick) {
      adjustCurrentField(-1); // Giảm giá trị
    } else if (isButtonPressed(buttonUp) == DoubleClick) {
      currentTimeField = (currentTimeField + 1) % 3; // Chuyển sang trường tiếp theo
    } else if (isButtonPressed(buttonDown) == DoubleClick) {
      currentTimeField = currentTimeField < 0? 2: currentTimeField - 1; // Chuyển sang trường tiếp theo
    }
  } else if (mode == ADJUST_DATE_MODE) {
    // Chế độ điều chỉnh ngày/tháng/năm
    if (isButtonPressed(buttonUp) == SingleClick) {
      adjustCurrentField(1); // Tăng giá trị
    } else if (isButtonPressed(buttonDown) == SingleClick) {
      adjustCurrentField(-1); // Giảm giá trị
    } else if (isButtonPressed(buttonUp) == DoubleClick) {
      currentDateField = (currentDateField + 1) % 3; // Chuyển sang trường tiếp theo
    } else if (isButtonPressed(buttonDown) == DoubleClick) {
      currentDateField = currentDateField < 0? 2: currentDateField - 1; // Chuyển sang trường tiếp theo
    }
  }
}

// Hàm để chỉnh giờ/phút/giây
void adjustCurrentField(int delta) {
  if (mode == ADJUST_TIME_MODE) {
    // Điều chỉnh giờ/phút/giây
    if (currentTimeField == 0) {
      // Điều chỉnh giờ
      currentTime.hours = (currentTime.hours + delta + 24) % 24;
    } else if (currentTimeField == 1) {
      // Điều chỉnh phút
      currentTime.mins = (currentTime.mins + delta + 60) % 60;
    } else if (currentTimeField == 2) {
      // Điều chỉnh giây
      currentTime.seconds = (currentTime.seconds + delta + 60) % 60;
    }
  } else if (mode == ADJUST_DATE_MODE) {
    // Điều chỉnh ngày/tháng/năm
    if (currentDateField == 0) {
      // Điều chỉnh ngày
      int maxDay = getMaxDayInMonth(currentTime.months, currentTime.years);
      currentTime.days = (currentTime.days + delta + maxDay - 1) % maxDay + 1;
    } else if (currentDateField == 1) {
      // Điều chỉnh tháng
      currentTime.months = (currentTime.months + delta + 12 - 1) % 12 + 1;
    } else if (currentDateField == 2) {
      // Điều chỉnh năm
      currentTime.years += delta;
      if (currentTime.years < MIN_YEAR) currentTime.years = MAX_YEAR;
      if (currentTime.years > MAX_YEAR) currentTime.years = MIN_YEAR;
    }
  }
}

void getTime() {
  struct tm timeinfo;
  if (!getLocalTime(&timeinfo)) {
    Serial.println("Failed to obtain time");
    return;
  }

  // Gán các giá trị thời gian vào struct Time
  currentTime.hours = timeinfo.tm_hour;
  currentTime.mins = timeinfo.tm_min;
  currentTime.seconds = timeinfo.tm_sec;
  currentTime.days = timeinfo.tm_mday;
  currentTime.months = timeinfo.tm_mon + 1; 
  currentTime.years = timeinfo.tm_year + 1900; 
  // Tạo đối tượng DateTime từ thời gian hiện tại
  DateTime now(currentTime.years, currentTime.months, currentTime.days, currentTime.hours, currentTime.mins, currentTime.seconds);

  // Đặt thời gian cho RTC
  rtc.adjust(now);
  rtc.begin();
  /*03:47:00 13.05.2022 //sec, min, hour, day, month, year*/
  if (!rtc.begin()) {
    Serial.println("Couldn't find RTC");
    while (1);  // Dừng chương trình nếu không tìm thấy RTC
  }

  // Nếu RTC chưa được thiết lập thời gian, tiến hành thiết lập
  if (!rtc.isrunning()) {
    Serial.println("RTC is NOT running, let's set the time!");
    getTime();  // Lấy thời gian từ NTP và đặt cho RTC
  }
  delay(1000);      
}

void drawPatterns(uint8_t band) {
  
  uint8_t barHeight = barHeights[band];
  
  // Draw bars
  switch (pattern) {
    case 0:
      rainbowBars(band, barHeight);
      break;
    case 1:
      // No bars on this one
      break;
    case 2:
      purpleBars(band, barHeight);
      break;
    case 3:
      centerBars(band, barHeight);
      break;
    case 4:
      changingBars(band, barHeight);
      EVERY_N_MILLISECONDS(10) { colorTimer++; }
      break;
    case 5:
      createWaterfall(band);
      EVERY_N_MILLISECONDS(30) { moveWaterfall(); }
      break;
  }

  // Draw peaks
  switch (pattern) {
    case 0:
      whitePeak(band);
      break;
    case 1:
      outrunPeak(band);
      break;
    case 2:
      whitePeak(band);
      break;
    case 3:
      // No peaks
      break;
    case 4:
      // No peaks
      break;
    case 5:
      // No peaks
      break;
  }
}

void showPeak()
{
  if (pattern != 5) FastLED.clear();
  
  uint8_t divisor = 1;                                                    // If 8 bands, we need to divide things by 2
  if (numBands == 8) divisor = 2;                                         // and average each pair of bands together
  
  for (int i = 0; i < 16; i += divisor) {
    uint8_t fftValue;
    
    if (numBands == 8) fftValue = (fftResult[i] + fftResult[i+1]) / 2;    // Average every two bands if numBands = 8
    else fftValue = fftResult[i];

    fftValue = ((prevFFTValue[i/divisor] * 3) + fftValue) / 4;            // Dirty rolling average between frames to reduce flicker
    barHeights[i/divisor] = fftValue / (255 / M_HEIGHT);                  // Scale bar height
    
    if (barHeights[i/divisor] > peak[i/divisor])                          // Move peak up
      peak[i/divisor] = min(M_HEIGHT, (int)barHeights[i/divisor]);
      
    prevFFTValue[i/divisor] = fftValue;                                   // Save prevFFTValue for averaging later
    
  }
  // Draw the patterns
  for (int band = 0; band < numBands; band++) {
    drawPatterns(band);
  }

  // Decay peak
  EVERY_N_MILLISECONDS(60) {
    for (uint8_t band = 0; band < numBands; band++)
      if (peak[band] > 0) peak[band] -= 1;
  }

  EVERY_N_SECONDS(30) {
    // Save values in EEPROM. Will only be commited if values have changed.
    EEPROM.write(EEPROM_BRIGHTNESS, brightness);
    EEPROM.write(EEPROM_GAIN, gain);
    EEPROM.write(EEPROM_SQUELCH, squelch);
    EEPROM.write(EEPROM_PATTERN, pattern);
    EEPROM.write(EEPROM_DISPLAY_TIME, displayTime);
    EEPROM.commit();
  }
  
  EVERY_N_SECONDS_I(timingObj, displayTime) {
    timingObj.setPeriod(displayTime);
    if (autoChangePatterns) pattern = (pattern + 1) % 6;
  }
  
  FastLED.setBrightness(brightness);
  FastLED.show();
}

void updateTime() {
  unsigned long currentTimeMillis = millis();
  if (currentTimeMillis - lastUpdateTime >= timeInterval) {
    lastUpdateTime = currentTimeMillis;

    if (mode != ADJUST_TIME_MODE && mode != ADJUST_DATE_MODE) {
      // Không ở chế độ chỉnh sửa, cập nhật thời gian từ RTC
      DateTime now = rtc.now();
      currentTime.seconds = now.second();
      currentTime.mins = now.minute();
      currentTime.hours = now.hour();
      currentTime.days = now.day();
      currentTime.months = now.month();
      currentTime.years = now.year();
    } else {
      // Ở chế độ chỉnh sửa, cập nhật thời gian vào RTC
      DateTime adjustedTime(currentTime.years, currentTime.months, currentTime.days, currentTime.hours, currentTime.mins, currentTime.seconds);
      rtc.adjust(adjustedTime);
    }
  }
}



int getMaxDayInMonth(int month, int year) {
  switch(month) {
    case 4: case 6: case 9: case 11:
      return 30;
    case 2:
      if (isLeapYear(year)) return 29;
      else return 28;
    default:
      return 31;
  }
}

bool isLeapYear(int year) {
  if (year % 400 == 0) return true;
  if (year % 100 == 0) return false;
  if (year % 4 == 0) return true;
  return false;
}


// Hàm hiển thị giờ, phút, giây trên ma trận LED
void displayTimeMode() {
  FastLED.clear();

  char timeStr[9];  // Buffer để chứa chuỗi thời gian HH:MM:SS
    /*get time from RTC*/
  snprintf(timeStr, sizeof(timeStr), "%02d:%02d:%02d", currentTime.hours, currentTime.mins, currentTime.seconds);

  // Sử dụng LEDText hoặc bất kỳ phương pháp nào để hiển thị chuỗi `timeStr` trên ma trận LED
  ScrollingMsg.SetFont(MatriseFontData);
  ScrollingMsg.Init(&leds, leds.Width(), ScrollingMsg.FontHeight() + 1, 0, 0);
  ScrollingMsg.SetText((unsigned char *)timeStr, strlen(timeStr));
  ScrollingMsg.SetTextColrOptions(COLR_RGB | COLR_SINGLE, 0xff, 0xff, 0xff);  // Màu trắng
  ScrollingMsg.SetFrameRate(160 / M_WIDTH);

  while (ScrollingMsg.UpdateText() == 0) {
    FastLED.show();
  }
}

// Hàm hiển thị giờ, phút, giây trên ma trận LED
void displayDay() {
  FastLED.clear();

  char timeStr[9];  // Buffer để chứa chuỗi thời gian HH:MM:SS
    /*get time from RTC*/

  snprintf(timeStr, sizeof(timeStr), "%02d/%02d/%04d", currentTime.days, currentTime.months, currentTime.years);

  // Sử dụng LEDText hoặc bất kỳ phương pháp nào để hiển thị chuỗi `timeStr` trên ma trận LED
  ScrollingMsg.SetFont(MatriseFontData);
  ScrollingMsg.Init(&leds, leds.Width(), ScrollingMsg.FontHeight() + 1, 0, 0);
  ScrollingMsg.SetText((unsigned char *)timeStr, strlen(timeStr));
  ScrollingMsg.SetTextColrOptions(COLR_RGB | COLR_SINGLE, 0xff, 0xff, 0xff);  // Màu trắng
  ScrollingMsg.SetFrameRate(160 / M_WIDTH);

  while (ScrollingMsg.UpdateText() == 0) {
    FastLED.show();
  }
}

void showIP(){
  char strIP[16] = "               ";
  IPAddress ip = WiFi.localIP();
  ip.toString().toCharArray(strIP, 16);
  Serial.println(strIP);
  ScrollingMsg.SetFont(MatriseFontData);
  ScrollingMsg.Init(&leds, leds.Width(), ScrollingMsg.FontHeight() + 1, 0, 0);
  ScrollingMsg.SetText((unsigned char *)strIP, sizeof(strIP) - 1);
  ScrollingMsg.SetTextColrOptions(COLR_RGB | COLR_SINGLE, 0xff, 0xff, 0xff);
  ScrollingMsg.SetScrollDirection(SCROLL_LEFT);
  ScrollingMsg.SetFrameRate(160 / M_WIDTH);       // Faster for larger matrices

  while(ScrollingMsg.UpdateText() == 0) {
    FastLED.show();  
  }
}

// Hàm hiển thị thời gian với hiệu ứng nhấp nháy
void displayBlinkingTime() {
  char timeStr[9];  // Chuỗi chứa định dạng HH:MM:SS
  snprintf(timeStr, sizeof(timeStr), "%02d:%02d:%02d", currentTime.hours, currentTime.mins, currentTime.seconds);

  // Tạo hiệu ứng nhấp nháy
  unsigned long currentTimeMillis = millis();
  if (currentTimeMillis - lastBlinkTime >= blinkInterval) {
    blinkState = !blinkState;  // Đổi trạng thái nhấp nháy (bật/tắt)
    lastBlinkTime = currentTimeMillis;
  }

  // Hiển thị thời gian lên ma trận LED
  FastLED.clear();

  for (int i = 0; i < 8; i++) {
    // Kiểm tra vị trí hiện tại để nhấp nháy
    if ((i < 2 && currentTimeField == 0 && blinkState) ||         // Nhấp nháy vị trí giờ
        (i >= 3 && i <= 4 && currentTimeField == 1 && blinkState) || // Nhấp nháy vị trí phút
        (i >= 6 && currentTimeField == 2 && blinkState)) {         // Nhấp nháy vị trí giây
      continue; // Bỏ qua hiển thị tại vị trí đang nhấp nháy
    }

    // Vẽ ký tự lên LED
    drawCharacter(timeStr[i], i);
  }

  FastLED.show();
}


void displayBlinkingDate() {
  char dateStr[11];  // Chuỗi chứa định dạng DD/MM/YYYY
  snprintf(dateStr, sizeof(dateStr), "%02d/%02d/%04d", currentTime.days, currentTime.months, currentTime.years);

  // Tạo hiệu ứng nhấp nháy
  unsigned long currentTimeMillis = millis();
  if (currentTimeMillis - lastBlinkTime >= blinkInterval) {
    blinkState = !blinkState;  // Đổi trạng thái nhấp nháy (bật/tắt)
    lastBlinkTime = currentTimeMillis;
  }

  // Hiển thị ngày tháng lên ma trận LED
  FastLED.clear();

  for (int i = 0; i < strlen(dateStr); i++) {
    // Kiểm tra vị trí hiện tại để nhấp nháy
    if ((i < 2 && currentDateField == 0 && blinkState) ||            // Nhấp nháy vị trí ngày
        (i >= 3 && i <= 4 && currentDateField == 1 && blinkState) || // Nhấp nháy vị trí tháng
        (i >= 6 && i <= 9 && currentDateField == 2 && blinkState)) { // Nhấp nháy vị trí năm
      continue; // Bỏ qua hiển thị tại vị trí đang nhấp nháy
    }

    // Vẽ ký tự lên LED
    drawCharacter(dateStr[i], i);
  }

  FastLED.show();
}

void drawCharacter(char c, int position) {
  // Xác định vị trí x trên ma trận LED dựa trên vị trí của ký tự
  int charWidth = 4; // Điều chỉnh độ rộng ký tự phù hợp với font
  int xPosition = position * charWidth;

  // Kiểm tra nếu vị trí vượt quá ma trận
  if (xPosition + charWidth > M_WIDTH) {
    return; // Không vẽ nếu vị trí vượt quá kích thước ma trận
  }

  // Tạo đối tượng cLEDText cho ký tự
  static cLEDText CharMsg;
  CharMsg.SetFont(MatriseFontData);
  CharMsg.Init(&leds, M_WIDTH, M_HEIGHT, xPosition, 0);
  char charStr[2] = { c, '\0' }; // Tạo chuỗi chứa ký tự cần vẽ
  CharMsg.SetText((unsigned char *)charStr, strlen(charStr));
  CharMsg.SetTextColrOptions(COLR_RGB | COLR_SINGLE, 255, 255, 255); // Màu trắng
  CharMsg.SetScrollDirection(SCROLL_LEFT);
  CharMsg.SetFrameRate(0); // Không cuộn

  // Vẽ ký tự lên ma trận LED
  while (CharMsg.UpdateText() == 0) {
    // Không cần làm gì trong vòng lặp này
  }
}



//////////// Patterns ////////////

void rainbowBars(uint8_t band, uint8_t barHeight) {
  int xStart = barWidth * band;
  for (int x = xStart; x < xStart + barWidth; x++) {
    for (int y = 0; y <= barHeight; y++) {
      leds(x,y) = CHSV((x / barWidth) * (255 / numBands), 255, 255);
    }
  }
}

void purpleBars(int band, int barHeight) {
  int xStart = barWidth * band;
  for (int x = xStart; x < xStart + barWidth; x++) {
    for (int y = 0; y < barHeight; y++) {
      leds(x,y) = ColorFromPalette(purplePal, y * (255 / barHeight));
    }
  }
}

void changingBars(int band, int barHeight) {
  int xStart = barWidth * band;
  for (int x = xStart; x < xStart + barWidth; x++) {
    for (int y = 0; y < barHeight; y++) {
      leds(x,y) = CHSV(y * (255 / M_HEIGHT) + colorTimer, 255, 255); 
    }
  }
}

void centerBars(int band, int barHeight) {
  int xStart = barWidth * band;
  for (int x = xStart; x < xStart + barWidth; x++) {
    if (barHeight % 2 == 0) barHeight--;
    int yStart = ((M_HEIGHT - barHeight) / 2 );
    for (int y = yStart; y <= (yStart + barHeight); y++) {
      int colorIndex = constrain((y - yStart) * (255 / barHeight), 0, 255);
      leds(x,y) = ColorFromPalette(heatPal, colorIndex);
    }
  }
}

void whitePeak(int band) {
  int xStart = barWidth * band;
  int peakHeight = peak[band];
  for (int x = xStart; x < xStart + barWidth; x++) {
    leds(x,peakHeight) = CRGB::White;
  }
}

void outrunPeak(int band) {
  int xStart = barWidth * band;
  int peakHeight = peak[band];
  for (int x = xStart; x < xStart + barWidth; x++) {
    leds(x,peakHeight) = ColorFromPalette(outrunPal, peakHeight * (255 / M_HEIGHT));
  }
}

void createWaterfall(int band) {
  int xStart = barWidth * band;
  // Draw bottom line
  for (int x = xStart; x < xStart + barWidth; x++) {
    leds(x,0) = CHSV(constrain(map(fftResult[band],0,254,160,0),0,160), 255, 255);
  }
}

void moveWaterfall() {
  // Move screen up starting at 2nd row from top
  for (int y = M_HEIGHT - 2; y >= 0; y--) {
    for (int x = 0; x < M_WIDTH; x++) {
      leds(x,y+1) = leds(x,y);
    }
  }
}