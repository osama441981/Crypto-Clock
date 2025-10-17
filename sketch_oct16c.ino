/*
 * CryptoClock - ساعة عرض أسعار العملات الرقمية
 * 
 * تم تطويره لـ LOLIN D1 mini مع شاشة LCD I2C GM12864-59N ver 2
 * يعرض أسعار العملات الرقمية: BTC, BCH, DOGE, XMR
 * يحتوي على صفحات إعدادات للساعة والواي فاي والعملات
 */

#include <ESP8266WiFi.h>          // مكتبة الواي فاي للـ ESP8266
#include <DNSServer.h>            // مكتبة خادم DNS
#include <ESP8266WebServer.h>     // مكتبة خادم الويب
#include <WiFiManager.h>          // مكتبة إدارة الواي فاي
#include <ArduinoJson.h>          // مكتبة للتعامل مع JSON
#include <TimeLib.h>              // مكتبة للتعامل مع الوقت
#include <NTPClient.h>            // مكتبة للحصول على الوقت من الإنترنت
#include <WiFiUdp.h>              // مكتبة UDP للاتصال بخادم NTP
#include <EEPROM.h>               // مكتبة للتخزين الدائم
#include <U8g2lib.h>              // مكتبة للتعامل مع الشاشة
#include <Wire.h>                 // مكتبة I2C
#include <ESP8266HTTPClient.h>    // مكتبة للاتصال HTTP
#include <WiFiClientSecure.h>     // For HTTPS

// تعريف الشاشة - JLX12864 with ST7567 controller
U8G2_ST7567_JLX12864_F_HW_I2C u8g2(U8G2_R0, U8X8_PIN_NONE);

// تعريف عناوين EEPROM
#define EEPROM_SIZE 512
#define TIMEZONE_ADDR 0
#define TIME_FORMAT_ADDR 1
#define CURRENCY_COUNT_ADDR 2
#define CURRENCIES_START_ADDR 10
#define WIFI_SSID_ADDR 200
#define WIFI_PASS_ADDR 250

// متغيرات الوقت
WiFiUDP ntpUDP;
NTPClient timeClient(ntpUDP, "pool.ntp.org");
int8_t timezone = 3;        // المنطقة الزمنية الافتراضية +3 (توقيت الخليج العربي)
bool timeFormat24 = true;   // تنسيق 24 ساعة

// متغيرات العملات
struct Currency {
  char code[8];            // رمز العملة
  float price;              // السعر الحالي
  float change24h;          // التغير خلال 24 ساعة
};

Currency currencies[10];    // مصفوفة للعملات (بحد أقصى 10 عملات)
int currencyCount = 4;      // عدد العملات الحالية

// متغيرات واجهة المستخدم
enum Screen { MAIN };
Screen currentScreen = MAIN;
int menuPosition = 0;

// متغيرات أخرى
unsigned long lastPriceUpdate = 0;
const unsigned long priceUpdateInterval = 60000;  // تحديث الأسعار كل دقيقة
unsigned long lastTimeUpdate = 0;
const unsigned long timeUpdateInterval = 1000;    // تحديث الوقت كل ثانية

// خادم الويب للإعدادات
ESP8266WebServer server(80);

// المتغيرات المؤقتة للإعدادات
int tempTimezone = 3;
bool tempTimeFormat24 = true;
char tempCurrencies[10][8];
int tempCurrencyCount = 4;

// Function prototypes
void showSplashScreen();
void loadSettings();
void saveSettings();
void clearEEPROM();
void updateTime();
void updateCryptoPrices();
void getCryptoPrice(const char* symbol, float* price, float* change24h);
void updateDisplay();
void drawMainScreen();
void setupWebServer();
void handleRoot();
void handleTimeSettings();
void handleTimeSettingsPost();
void handleWiFiSettings();
void handleWiFiSettingsPost();
void handleCurrencySettings();
void handleCurrencySettingsPost();

void setup() {
  Serial.begin(115200);
  
  // إعداد الشاشة
  u8g2.setI2CAddress(0x3F * 2); // عنوان I2C
  u8g2.begin();
  u8g2.setContrast(145); // ضبط التباين
  u8g2.setFont(u8g2_font_6x10_tf); // الخط الافتراضي للشاشة (6x10 بكسل)
  
  // عرض شاشة البداية
  showSplashScreen();
  
  // إعداد EEPROM
  EEPROM.begin(EEPROM_SIZE);
  
  // إعداد WiFiManager
  WiFiManager wifiManager;
  
  // محاولة الاتصال بالشبكة المحفوظة
  if (!wifiManager.autoConnect("CryptoClock")) {
    Serial.println("فشل الاتصال بالواي فاي وانتهت المهلة!");
    delay(3000);
    ESP.restart();
  }
  
  Serial.println("تم الاتصال بالواي فاي!");
  Serial.print("IP: ");
  Serial.println(WiFi.localIP());
  
  // إعداد عميل NTP
  timeClient.begin();
  
  // تحميل الإعدادات بعد تهيئة timeClient
  Serial.println("تحميل الإعدادات...");
  loadSettings();
  Serial.print("المنطقة الزمنية بعد التحميل: ");
  Serial.println(timezone);
  
  // إعداد خادم الويب
  setupWebServer();
  
  // تهيئة القيم الافتراضية للعملات
  for (int i = 0; i < currencyCount; i++) {
    currencies[i].price = 0.0;
    currencies[i].change24h = 0.0;
  }
  
  // تحديث الوقت والأسعار لأول مرة
  updateTime();
  updateCryptoPrices();
}

void loop() {
  // معالجة طلبات خادم الويب
  server.handleClient();
  
  // تحديث الوقت كل ثانية
  if (millis() - lastTimeUpdate > timeUpdateInterval) {
    updateTime();
    lastTimeUpdate = millis();
  }
  
  // تحديث أسعار العملات كل دقيقة
  if (millis() - lastPriceUpdate > priceUpdateInterval) {
    updateCryptoPrices();
    lastPriceUpdate = millis();
  }
  
  // تحديث الشاشة
  updateDisplay();
  
  // تغيير العملة المعروضة كل 5 ثوانٍ
  static unsigned long lastMenuChange = 0;
  if (millis() - lastMenuChange > 5000) {
    menuPosition = (menuPosition + 2) % currencyCount;
    lastMenuChange = millis();
  }
  
  delay(50);  // تأخير صغير
}

// عرض شاشة البداية
void showSplashScreen() {
  u8g2.clearBuffer();
  u8g2.setFont(u8g2_font_ncenB14_tr); // خط كبير لعنوان "CryptoClock" (14 بكسل)
  u8g2.drawStr(5, 25, "CryptoClock");
  u8g2.setFont(u8g2_font_6x10_tf); // خط صغير لاسم المطور (6x10 بكسل)
  u8g2.drawStr(30, 45, "By Osama");
  u8g2.sendBuffer();
  delay(3000);
}

// تحميل الإعدادات من EEPROM
void loadSettings() {
  Serial.println("بدء تحميل الإعدادات من EEPROM...");
  
  // قراءة المنطقة الزمنية من EEPROM
  EEPROM.get(TIMEZONE_ADDR, timezone);
  Serial.print("المنطقة الزمنية المحملة من EEPROM: ");
  Serial.println(timezone);
  
  // التحقق من صحة القيمة (0 تعتبر قيمة غير صالحة أيضاً)
  if (timezone < -12 || timezone > 14 || timezone == 0) {
    Serial.println("قيمة المنطقة الزمنية غير صالحة أو افتراضية، استخدام القيمة الافتراضية +3");
    timezone = 3;  // المنطقة الزمنية الافتراضية +3 (توقيت الخليج العربي)
    
    // حفظ القيمة الافتراضية في EEPROM
    EEPROM.put(TIMEZONE_ADDR, timezone);
    EEPROM.commit();
    Serial.println("تم حفظ المنطقة الزمنية الافتراضية +3 في EEPROM");
  }
  
  Serial.print("المنطقة الزمنية النهائية: ");
  Serial.println(timezone);
  
  timeFormat24 = EEPROM.read(TIME_FORMAT_ADDR) != 0;
  
  currencyCount = EEPROM.read(CURRENCY_COUNT_ADDR);
  if (currencyCount < 1 || currencyCount > 10) {
    // إذا كانت القيمة غير صالحة، استخدم القيم الافتراضية
    currencyCount = 4;
    strcpy(currencies[0].code, "BTC");
    strcpy(currencies[1].code, "BCH");
    strcpy(currencies[2].code, "DOGE");
    strcpy(currencies[3].code, "XMR");
  } else {
    // قراءة رموز العملات من EEPROM
    for (int i = 0; i < currencyCount; i++) {
      for (int j = 0; j < 7; j++) {
        currencies[i].code[j] = EEPROM.read(CURRENCIES_START_ADDR + (i * 8) + j);
      }
      currencies[i].code[7] = '\0';  // إضافة نهاية السلسلة
    }
  }
  
  // نسخ الإعدادات إلى المتغيرات المؤقتة
  tempTimezone = timezone;
  tempTimeFormat24 = timeFormat24;
  tempCurrencyCount = currencyCount;
  for (int i = 0; i < currencyCount; i++) {
    strcpy(tempCurrencies[i], currencies[i].code);
  }
  
  // تطبيق المنطقة الزمنية على timeClient
  timeClient.setTimeOffset(timezone * 3600);
  timeClient.forceUpdate(); // إجبار التحديث الفوري
}

// حفظ الإعدادات في EEPROM
void saveSettings() {
  Serial.println("بدء حفظ الإعدادات في EEPROM...");
  Serial.print("حفظ المنطقة الزمنية في EEPROM: ");
  Serial.println(timezone);
  
  // حفظ المنطقة الزمنية
  EEPROM.put(TIMEZONE_ADDR, timezone);
  Serial.println("تم حفظ المنطقة الزمنية في EEPROM");
  EEPROM.write(TIME_FORMAT_ADDR, timeFormat24 ? 1 : 0);
  EEPROM.write(CURRENCY_COUNT_ADDR, currencyCount);
  
  // حفظ رموز العملات
  for (int i = 0; i < currencyCount; i++) {
    for (int j = 0; j < 7 && currencies[i].code[j] != '\0'; j++) {
      EEPROM.write(CURRENCIES_START_ADDR + (i * 8) + j, currencies[i].code[j]);
    }
    EEPROM.write(CURRENCIES_START_ADDR + (i * 8) + 7, '\0');  // إضافة نهاية السلسلة
  }
  
  EEPROM.commit();
  Serial.println("تم حفظ جميع الإعدادات في EEPROM بنجاح");
}

// تحديث الوقت من خادم NTP
void updateTime() {
  // التأكد من تطبيق المنطقة الزمنية الحالية
  timeClient.setTimeOffset(timezone * 3600);
  timeClient.update();
  
  // طباعة الوقت الحالي للتشخيص
  static unsigned long lastDebugTime = 0;
  if (millis() - lastDebugTime > 10000) { // كل 10 ثوان
    Serial.print("الوقت الحالي: ");
    Serial.print(timeClient.getHours());
    Serial.print(":");
    Serial.print(timeClient.getMinutes());
    Serial.print(":");
    Serial.print(timeClient.getSeconds());
    Serial.print(" (المنطقة الزمنية: ");
    Serial.print(timezone);
    Serial.println(")");
    lastDebugTime = millis();
  }
}

// تحديث أسعار العملات الرقمية
void updateCryptoPrices() {
  Serial.println("بدء تحديث أسعار العملات...");
  for (int i = 0; i < currencyCount; i++) {
    Serial.print("تحديث سعر ");
    Serial.print(currencies[i].code);
    Serial.print("... ");
    
    float oldPrice = currencies[i].price;
    getCryptoPrice(currencies[i].code, &currencies[i].price, &currencies[i].change24h);
    
    Serial.print("السعر الجديد: ");
    Serial.print(currencies[i].price);
    Serial.print(" (السعر القديم: ");
    Serial.print(oldPrice);
    Serial.println(")");
    
    delay(1000);  // تأخير بين الطلبات لتجنب تجاوز حدود API
  }
  Serial.println("انتهى تحديث أسعار العملات");
}

// الحصول على سعر عملة رقمية من API
void getCryptoPrice(const char* symbol, float* price, float* change24h) {
  Serial.print("استعلام سعر ");
  Serial.print(symbol);
  Serial.print("... ");
  
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("لا يوجد اتصال بالإنترنت");
    return;
  }

  BearSSL::WiFiClientSecure client;
  client.setInsecure(); // Allow insecure connections for testing

  HTTPClient http;

  // تحويل رمز العملة إلى معرف CoinGecko
  String coinId;
  if (strcmp(symbol, "BTC") == 0) coinId = "bitcoin";
  else if (strcmp(symbol, "BCH") == 0) coinId = "bitcoin-cash";
  else if (strcmp(symbol, "DOGE") == 0) coinId = "dogecoin";
  else if (strcmp(symbol, "XMR") == 0) coinId = "monero";
  else if (strcmp(symbol, "ETH") == 0) coinId = "ethereum";
  else if (strcmp(symbol, "LTC") == 0) coinId = "litecoin";
  else if (strcmp(symbol, "XRP") == 0) coinId = "ripple";
  else if (strcmp(symbol, "ADA") == 0) coinId = "cardano";
  else if (strcmp(symbol, "DOT") == 0) coinId = "polkadot";
  else if (strcmp(symbol, "LINK") == 0) coinId = "chainlink";
  else {
    Serial.print("رمز غير معروف: ");
    Serial.println(symbol);
    return;  // رمز غير معروف
  }

  // استخدام CoinGecko API
  String url = "https://api.coingecko.com/api/v3/simple/price?ids=" + coinId + "&vs_currencies=usd&include_24hr_change=true";
  Serial.print("استعلام: ");
  Serial.println(url);
  
  if (http.begin(client, url)) {
    int httpCode = http.GET();
    Serial.print("رمز الاستجابة: ");
    Serial.println(httpCode);
    
    if (httpCode == HTTP_CODE_OK) {
      String payload = http.getString();
    Serial.println("الاستجابة: " + payload);
    
    // تحليل JSON
    DynamicJsonDocument doc(2048); // زيادة حجم الذاكرة المخصضة للـ JSON
    DeserializationError error = deserializeJson(doc, payload);
    
    if (!error) {
      // التحقق من وجود البيانات المطلوبة
      if (doc.containsKey(coinId)) {
        JsonObject coin = doc[coinId];
        if (coin.containsKey("usd")) {
          *price = coin["usd"].as<float>();
          Serial.print("تم استلام السعر: ");
          Serial.println(*price);
          
          if (coin.containsKey("usd_24h_change")) {
            *change24h = coin["usd_24h_change"].as<float>();
          } else {
            *change24h = 0;
          }
        } else {
          Serial.println("لا يوجد سعر USD في البيانات");
          *price = 0;
          *change24h = 0;
        }
      } else {
        Serial.print("لا توجد بيانات للعملة: ");
        Serial.println(coinId);
        *price = 0;
        *change24h = 0;
      }
    } else {
      Serial.print("خطأ في تحليل JSON: ");
      Serial.println(error.c_str());
      *price = 0;
      *change24h = 0;
    }
  } else {
    Serial.print("خطأ في HTTP: ");
    Serial.println(httpCode);
    *price = 0;
    *change24h = 0;
  }
  
  http.end();
  
  Serial.print("النتيجة النهائية: ");
  Serial.print(*price);
  Serial.print(" (التغيير: ");
  Serial.print(*change24h);
  Serial.println(")");
}
}

// تحديث الشاشة
void updateDisplay() {
  u8g2.clearBuffer();
  drawMainScreen();
  u8g2.sendBuffer();
}

// رسم الشاشة الرئيسية
void drawMainScreen() {
  // تقسيم الشاشة أفقياً
  u8g2.drawHLine(0, 32, 128);
  
  // رسالة تشخيص
  static unsigned long lastDebugDisplay = 0;
  if (millis() - lastDebugDisplay > 5000) { // كل 5 ثوان
    Serial.print("عرض الشاشة - عدد العملات: ");
    Serial.print(currencyCount);
    Serial.print(", الموضع: ");
    Serial.print(menuPosition);
    Serial.print(", سعر BTC: ");
    Serial.println(currencies[0].price);
    lastDebugDisplay = millis();
  }
  
  // عرض العملات في الجزء العلوي (عملتان في كل مرة)
  if (currencyCount > 0) {
    // العملة الأولى (الموضع الحالي)
    int pos1 = menuPosition % currencyCount;
    u8g2.setFont(u8g2_font_7x14B_tr); // خط أسماء العملات (BTC, DOGE, إلخ) - 7x14 بكسل بولد
    u8g2.drawStr(0, 10, currencies[pos1].code);
    
    char priceStr[20];
    if (currencies[pos1].price > 0) { // عرض السعر فقط إذا كان أكبر من صفر
      if (strcmp(currencies[pos1].code, "BTC") == 0) {
        sprintf(priceStr, "$%.0f", currencies[pos1].price); // حذف المرتبتين العشريتين لـ BTC
      } else if (strcmp(currencies[pos1].code, "DOGE") == 0) {
        sprintf(priceStr, "$%.4f", currencies[pos1].price); // زيادة المراتب لـ DOGE
      } else {
        sprintf(priceStr, "$%.2f", currencies[pos1].price);
      }
    } else {
      strcpy(priceStr, "Loading..."); // رسالة تحميل إذا كان السعر صفر
    }
    u8g2.drawStr(40, 10, priceStr); // عرض أسعار العملات (نفس خط أسماء العملات)
    
    // عرض سهم للإشارة إلًا من النسبة المئوية
    if (currencies[pos1].change24h > 0) {
      u8g2.drawStr(100, 10, "^"); // سهم للأعلى (نفس خط أسماء العملات)
    } else if (currencies[pos1].change24h < 0) {
      u8g2.drawStr(100, 10, "v"); // سهم للأسفل (نفس خط أسماء العملات)
    } else {
      u8g2.drawStr(100, 10, "-"); // خط للثبات (نفس خط أسماء العملات)
    }
    
    // العملة الثانية (الموضع التالي)
    if (currencyCount > 1) {
      int pos2 = (menuPosition + 1) % currencyCount;
      u8g2.drawStr(0, 25, currencies[pos2].code); // اسم العملة الثانية (نفس خط العملة الأولى - 7x14 بكسل بولد)
      
      if (currencies[pos2].price > 0) { // عرض السعر فقط إذا كان أكبر من صفر
        if (strcmp(currencies[pos2].code, "BTC") == 0) {
          sprintf(priceStr, "$%.0f", currencies[pos2].price); // حذف المرتبتين العشريتين لـ BTC
        } else if (strcmp(currencies[pos2].code, "DOGE") == 0) {
          sprintf(priceStr, "$%.4f", currencies[pos2].price); // زيادة المراتب لـ DOGE
        } else {
          sprintf(priceStr, "$%.2f", currencies[pos2].price);
        }
      } else {
        strcpy(priceStr, "Loading..."); // رسالة تحميل إذا كان السعر صفر
      }
      u8g2.drawStr(40, 25, priceStr); // سعر العملة الثانية (نفس خط العملة الأولى)
      
      // عرض سهم للإشارة إلى اتجاه التغيير
      if (currencies[pos2].change24h > 0) {
        u8g2.drawStr(100, 25, "^"); // سهم للأعلى (نفس خط العملة الأولى)
      } else if (currencies[pos2].change24h < 0) {
        u8g2.drawStr(100, 25, "v"); // سهم للأسفل (نفس خط العملة الأولى)
      } else {
        u8g2.drawStr(100, 25, "-"); // خط للثبات (نفس خط العملة الأولى)
      }
    }
  }
  
  // عرض الوقت في الجزء السفلي بتنسيق جديد
  char hourMinStr[10]; // للساعات والدقائق
  char secStr[5];      // للثواني
  char ampmStr[3];     // لـ AM/PM
  
  if (timeFormat24) {
    sprintf(hourMinStr, "%d:%02d", timeClient.getHours(), timeClient.getMinutes()); // إزالة الصفر قبل الساعة
    sprintf(secStr, "%02d", timeClient.getSeconds());
    ampmStr[0] = '\0'; // فارغ في حالة تنسيق 24 ساعة
  } else {
    int hour = timeClient.getHours() % 12;
    if (hour == 0) hour = 12;
    sprintf(hourMinStr, "%d:%02d", hour, timeClient.getMinutes()); // إزالة الصفر قبل الساعة
    sprintf(secStr, "%02d", timeClient.getSeconds());
    strcpy(ampmStr, timeClient.getHours() >= 12 ? "PM" : "AM");
  }
  
  // عرض الساعات والدقائق بخط كبير
  u8g2.setFont(u8g2_font_ncenB18_tr); // خط الساعة الأصغر والأكثر بولد (18 بكسل) - للساعات والدقائق
  int hourMinWidth = u8g2.getStrWidth(hourMinStr);
  u8g2.drawStr((128 - hourMinWidth - 20) / 2, 55, hourMinStr);
  
  // عرض الثواني بخط صغير
  u8g2.setFont(u8g2_font_6x10_tf); // خط الثواني الصغير (6x10 بكسل) - للثواني
  u8g2.drawStr((128 - hourMinWidth - 20) / 2 + hourMinWidth + 2, 55, secStr);
  
  // عرض AM/PM فوق الوقت إذا كان التنسيق 12 ساعة
  if (!timeFormat24) {
    u8g2.drawStr((128 - hourMinWidth - 20) / 2 + hourMinWidth + 2, 45, ampmStr); // خط AM/PM (نفس خط الثواني)
  }
}

// إعداد خادم الويب
void setupWebServer() {
  // الصفحة الرئيسية
  server.on("/", HTTP_GET, handleRoot);
  
  // صفحة إعدادات الوقت
  server.on("/time", HTTP_GET, handleTimeSettings);
  server.on("/time", HTTP_POST, handleTimeSettingsPost);
  
  // صفحة إعدادات الواي فاي
  server.on("/wifi", HTTP_GET, handleWiFiSettings);
  server.on("/wifi", HTTP_POST, handleWiFiSettingsPost);
  
  // صفحة إعدادات العملات
  server.on("/currency", HTTP_GET, handleCurrencySettings);
  server.on("/currency", HTTP_POST, handleCurrencySettingsPost);
  
  // بدء الخادم
  server.begin();
  Serial.println("تم بدء خادم الويب");
}

// معالجة الصفحة الرئيسية
void handleRoot() {
  String html = "<!DOCTYPE html>\n";
  html += "<html>\n";
  html += "<head>\n";
  html += "<meta name='viewport' content='width=device-width, initial-scale=1.0'>\n";
  html += "<title>CryptoClock Settings</title>\n";
  html += "<style>\n";
  html += "body { font-family: Arial, sans-serif; margin: 0; padding: 20px; }\n";
  html += "h1 { color: #333; }\n";
  html += ".menu { display: flex; flex-direction: column; gap: 10px; margin-top: 20px; }\n";
  html += ".menu a { display: block; padding: 10px; background-color: #4CAF50; color: white; text-decoration: none; border-radius: 5px; text-align: center; }\n";
  html += ".menu a:hover { background-color: #45a049; }\n";
  html += "</style>\n";
  html += "</head>\n";
  html += "<body>\n";
  html += "<h1>CryptoClock Settings</h1>\n";
  html += "<div class='menu'>\n";
  html += "<a href='/time'>Time Settings</a>\n";
  html += "<a href='/wifi'>WiFi Settings</a>\n";
  html += "<a href='/currency'>Currency Settings</a>\n";
  html += "</div>\n";
  html += "</body>\n";
  html += "</html>\n";
  
  server.send(200, "text/html", html);
}

// معالجة صفحة إعدادات الوقت
void handleTimeSettings() {
  String html = "<!DOCTYPE html>\n";
  html += "<html>\n";
  html += "<head>\n";
  html += "<meta name='viewport' content='width=device-width, initial-scale=1.0'>\n";
  html += "<title>Time Settings</title>\n";
  html += "<style>\n";
  html += "body { font-family: Arial, sans-serif; margin: 0; padding: 20px; }\n";
  html += "h1 { color: #333; }\n";
  html += "form { margin-top: 20px; }\n";
  html += ".form-group { margin-bottom: 15px; }\n";
  html += "label { display: block; margin-bottom: 5px; }\n";
  html += "select, input[type='checkbox'] { width: 100%; padding: 8px; box-sizing: border-box; }\n";
  html += "button { padding: 10px 15px; background-color: #4CAF50; color: white; border: none; border-radius: 5px; cursor: pointer; }\n";
  html += "button:hover { background-color: #45a049; }\n";
  html += ".back { display: inline-block; margin-top: 20px; color: #4CAF50; text-decoration: none; }\n";
  html += "</style>\n";
  html += "</head>\n";
  html += "<body>\n";
  html += "<h1>Time Settings</h1>\n";
  html += "<form method='post'>\n";
  html += "<div class='form-group'>\n";
  html += "<label for='timezone'>Timezone:</label>\n";
  html += "<select name='timezone' id='timezone'>\n";
  
  // خيارات المنطقة الزمنية
  for (int i = -12; i <= 12; i++) {
    html += "<option value='" + String(i) + "'" + (i == timezone ? " selected" : "") + ">UTC" + (i >= 0 ? "+" : "") + String(i) + "</option>\n";
  }
  
  html += "</select>\n";
  html += "</div>\n";
  html += "<div class='form-group'>\n";
  html += "<label for='timeFormat'>Time Format:</label>\n";
  html += "<select name='timeFormat' id='timeFormat'>\n";
  html += "<option value='24'" + String(timeFormat24 ? " selected" : "") + ">24 Hour</option>\n";
  html += "<option value='12'" + String(!timeFormat24 ? " selected" : "") + ">12 Hour</option>\n";
  html += "</select>\n";
  html += "</div>\n";
  html += "<button type='submit'>Save</button>\n";
  html += "</form>\n";
  html += "<div class='form-group' style='margin-top: 20px;'>\n";
  html += "<button type='submit' name='reset_timezone' value='1' style='background-color: #f44336;'>Reset Timezone to Default (+3)</button>\n";
  html += "</div>\n";
  html += "<a href='/' class='back'>Back to Main Menu</a>\n";
  html += "</body>\n";
  html += "</html>\n";
  
  server.send(200, "text/html", html);
}

// معالجة إرسال نموذج إعدادات الوقت
void handleTimeSettingsPost() {
  if (server.hasArg("reset_timezone") && server.arg("reset_timezone") == "1") {
    // إعادة تعيين المنطقة الزمنية إلى الافتراضية
    timezone = 3;
    timeClient.setTimeOffset(timezone * 3600);
    timeClient.forceUpdate();
    
    // حفظ الإعدادات
    saveSettings();
    
    server.send(200, "text/html", "<html><head><meta http-equiv='refresh' content='3;url=/time'></head><body><h1>Timezone Reset</h1><p>Timezone has been reset to +3 (Gulf Time). Redirecting...</p></body></html>");
  } else if (server.hasArg("timezone") && server.hasArg("timeFormat")) {
    timezone = server.arg("timezone").toInt();
    timeFormat24 = server.arg("timeFormat") == "24";
    
    Serial.print("تم تحديث المنطقة الزمنية إلى: ");
    Serial.println(timezone);
    
    // تحديث إعدادات الوقت
    timeClient.setTimeOffset(timezone * 3600);
    timeClient.forceUpdate(); // إجبار التحديث الفوري
    Serial.print("تم تطبيق المنطقة الزمنية على timeClient: ");
    Serial.println(timezone * 3600);
    
    // حفظ الإعدادات
    saveSettings();
    
    // إعادة التوجيه إلى الصفحة الرئيسية
    server.sendHeader("Location", "/", true);
    server.send(302, "text/plain", "");
  } else {
    server.send(400, "text/plain", "Bad Request");
  }
}

// معالجة صفحة إعدادات الواي فاي
void handleWiFiSettings() {
  String html = "<!DOCTYPE html>\n";
  html += "<html>\n";
  html += "<head>\n";
  html += "<meta name='viewport' content='width=device-width, initial-scale=1.0'>\n";
  html += "<title>WiFi Settings</title>\n";
  html += "<style>\n";
  html += "body { font-family: Arial, sans-serif; margin: 0; padding: 20px; }\n";
  html += "h1 { color: #333; }\n";
  html += "form { margin-top: 20px; }\n";
  html += ".form-group { margin-bottom: 15px; }\n";
  html += "label { display: block; margin-bottom: 5px; }\n";
  html += "button { padding: 10px 15px; background-color: #4CAF50; color: white; border: none; border-radius: 5px; cursor: pointer; }\n";
  html += "button:hover { background-color: #45a049; }\n";
  html += ".back { display: inline-block; margin-top: 20px; color: #4CAF50; text-decoration: none; }\n";
  html += ".info { margin-top: 20px; padding: 10px; background-color: #f2f2f2; border-radius: 5px; }\n";
  html += "</style>\n";
  html += "</head>\n";
  html += "<body>\n";
  html += "<h1>WiFi Settings</h1>\n";
  html += "<div class='info'>\n";
  html += "<p><strong>SSID:</strong> " + WiFi.SSID() + "</p>\n";
  html += "<p><strong>IP Address:</strong> " + WiFi.localIP().toString() + "</p>\n";
  html += "<p><strong>MAC Address:</strong> " + WiFi.macAddress() + "</p>\n";
  html += "<p><strong>Signal Strength:</strong> " + String(WiFi.RSSI()) + " dBm</p>\n";
  html += "</div>\n";
  html += "<form method='post'>\n";
  html += "<div class='form-group'>\n";
  html += "<button type='submit' name='reset' value='1'>Reset WiFi Settings</button>\n";
  html += "</div>\n";
  html += "<div class='form-group'>\n";
  html += "<button type='submit' name='configure' value='1'>Configure WiFi Networks</button>\n";
  html += "</div>\n";
  html += "<div class='form-group'>\n";
  html += "<button type='submit' name='clear_eeprom' value='1' style='background-color: #f44336;'>Clear All Settings (Reset)</button>\n";
  html += "</div>\n";
  html += "</form>\n";
  html += "<a href='/' class='back'>Back to Main Menu</a>\n";
  html += "</body>\n";
  html += "</html>\n";
  
  server.send(200, "text/html", html);
}

// معالجة إرسال نموذج إعدادات الواي فاي
void handleWiFiSettingsPost() {
  if (server.hasArg("reset") && server.arg("reset") == "1") {
    // إعادة تعيين إعدادات الواي فاي
    server.send(200, "text/html", "<html><head><meta http-equiv='refresh' content='10;url=/'></head><body><h1>WiFi Settings Reset</h1><p>The device will restart in WiFi configuration mode. Connect to the 'CryptoClock' access point to configure WiFi.</p></body></html>");
    delay(1000);
    
    // إعادة تعيين إعدادات الواي فاي وإعادة التشغيل
    WiFiManager wifiManager;
    wifiManager.resetSettings();
    ESP.restart();
  } else if (server.hasArg("configure") && server.arg("configure") == "1") {
    // فتح بوابة تكوين الواي فاي
    server.send(200, "text/html", "<html><head><meta http-equiv='refresh' content='10;url=/'></head><body><h1>WiFi Configuration Portal</h1><p>The device will start a configuration portal. Connect to the 'CryptoClock' access point to configure WiFi.</p></body></html>");
    delay(1000);
    
    // بدء بوابة التكوين
    WiFiManager wifiManager;
    wifiManager.startConfigPortal("CryptoClock");
    ESP.restart();
  } else if (server.hasArg("clear_eeprom") && server.arg("clear_eeprom") == "1") {
    // مسح جميع الإعدادات
    server.send(200, "text/html", "<html><head><meta http-equiv='refresh' content='5;url=/'></head><body><h1>Clearing All Settings</h1><p>All settings will be cleared and the device will restart with default settings.</p></body></html>");
    delay(1000);
    
    clearEEPROM();
    ESP.restart();
  } else {
    server.send(400, "text/plain", "Bad Request");
  }
}

// معالجة صفحة إعدادات العملات
void handleCurrencySettings() {
  String html = "<!DOCTYPE html>\n";
  html += "<html>\n";
  html += "<head>\n";
  html += "<meta name='viewport' content='width=device-width, initial-scale=1.0'>\n";
  html += "<title>Currency Settings</title>\n";
  html += "<style>\n";
  html += "body { font-family: Arial, sans-serif; margin: 0; padding: 20px; }\n";
  html += "h1 { color: #333; }\n";
  html += "form { margin-top: 20px; }\n";
  html += ".form-group { margin-bottom: 15px; }\n";
  html += "label { display: block; margin-bottom: 5px; }\n";
  html += "select { width: 100%; padding: 8px; box-sizing: border-box; }\n";
  html += "button { padding: 10px 15px; background-color: #4CAF50; color: white; border: none; border-radius: 5px; cursor: pointer; }\n";
  html += "button:hover { background-color: #45a049; }\n";
  html += ".back { display: inline-block; margin-top: 20px; color: #4CAF50; text-decoration: none; }\n";
  html += ".currency-list { margin-top: 20px; }\n";
  html += ".currency-item { display: flex; align-items: center; margin-bottom: 10px; }\n";
  html += ".currency-item select { flex: 1; margin-right: 10px; }\n";
  html += ".currency-item button { background-color: #f44336; }\n";
  html += ".currency-item button:hover { background-color: #d32f2f; }\n";
  html += ".add-button { margin-top: 10px; }\n";
  html += "</style>\n";
  html += "</head>\n";
  html += "<body>\n";
  html += "<h1>Currency Settings</h1>\n";
  html += "<form method='post' id='currencyForm'>\n";
  html += "<div class='currency-list' id='currencyList'>\n";
  
  // قائمة العملات المدعومة
  const char* supportedCurrencies[] = {"BTC", "BCH", "DOGE", "XMR", "ETH", "LTC", "XRP", "ADA", "DOT", "LINK"};
  const int numSupportedCurrencies = 10;
  
  // عرض العملات الحالية
  for (int i = 0; i < currencyCount; i++) {
    html += "<div class='currency-item'>\n";
    html += "<select name='currency" + String(i) + "'>\n";
    
    for (int j = 0; j < numSupportedCurrencies; j++) {
      html += "<option value='" + String(supportedCurrencies[j]) + "'" + (strcmp(currencies[i].code, supportedCurrencies[j]) == 0 ? " selected" : "") + ">" + String(supportedCurrencies[j]) + "</option>\n";
    }
    
    html += "</select>\n";
    html += "<button type='button' onclick='removeCurrency(" + String(i) + ")'>Remove</button>\n";
    html += "</div>\n";
  }
  
  html += "</div>\n";
  
  // زر إضافة عملة جديدة
  if (currencyCount < 10) {
    html += "<button type='button' class='add-button' onclick='addCurrency()'>Add Currency</button>\n";
  }
  
  html += "<input type='hidden' id='currencyCount' name='currencyCount' value='" + String(currencyCount) + "'>\n";
  html += "<button type='submit' style='margin-top: 20px;'>Save</button>\n";
  html += "</form>\n";
  html += "<a href='/' class='back'>Back to Main Menu</a>\n";
  
  // JavaScript لإضافة وإزالة العملات
  html += "<script>\n";
  html += "function addCurrency() {\n";
  html += "  var currencyList = document.getElementById('currencyList');\n";
  html += "  var currencyCount = parseInt(document.getElementById('currencyCount').value);\n";
  html += "  if (currencyCount >= 10) return;\n";
  
  html += "  var newItem = document.createElement('div');\n";
  html += "  newItem.className = 'currency-item';\n";
  html += "  var select = document.createElement('select');\n";
  html += "  select.name = 'currency' + currencyCount;\n";
  
  // إضافة خيارات العملات
  for (int j = 0; j < numSupportedCurrencies; j++) {
    html += "  var option = document.createElement('option');\n";
    html += "  option.value = '" + String(supportedCurrencies[j]) + "';\n";
    html += "  option.text = '" + String(supportedCurrencies[j]) + "';\n";
    html += "  select.appendChild(option);\n";
  }
  
  html += "  var button = document.createElement('button');\n";
  html += "  button.type = 'button';\n";
  html += "  button.textContent = 'Remove';\n";
  html += "  button.onclick = function() { removeCurrency(currencyCount); };\n";
  
  html += "  newItem.appendChild(select);\n";
  html += "  newItem.appendChild(button);\n";
  html += "  currencyList.appendChild(newItem);\n";
  
  html += "  document.getElementById('currencyCount').value = currencyCount + 1;\n";
  
  html += "  if (currencyCount + 1 >= 10) {\n";
  html += "    document.querySelector('.add-button').style.display = 'none';\n";
  html += "  }\n";
  html += "}\n";
  
  html += "function removeCurrency(index) {\n";
  html += "  var currencyItems = document.querySelectorAll('.currency-item');\n";
  html += "  if (currencyItems.length <= 1) return;\n";
  
  html += "  currencyItems[index].remove();\n";
  
  html += "  // إعادة تسمية العناصر\n";
  html += "  var newItems = document.querySelectorAll('.currency-item');\n";
  html += "  for (var i = 0; i < newItems.length; i++) {\n";
  html += "    newItems[i].querySelector('select').name = 'currency' + i;\n";
  html += "    newItems[i].querySelector('button').onclick = function(index) {\n";
  html += "      return function() { removeCurrency(index); };\n";
  html += "    }(i);\n";
  html += "  }\n";
  
  html += "  document.getElementById('currencyCount').value = newItems.length;\n";
  
  html += "  if (newItems.length < 10) {\n";
  html += "    document.querySelector('.add-button').style.display = 'block';\n";
  html += "  }\n";
  html += "}\n";
  html += "</script>\n";
  
  html += "</body>\n";
  html += "</html>\n";
  
  server.send(200, "text/html", html);
}

// معالجة إرسال نموذج إعدادات العملات
void handleCurrencySettingsPost() {
  if (server.hasArg("currencyCount")) {
    int newCurrencyCount = server.arg("currencyCount").toInt();
    
    if (newCurrencyCount > 0 && newCurrencyCount <= 10) {
      currencyCount = newCurrencyCount;
      
      // تحديث رموز العملات
      for (int i = 0; i < currencyCount; i++) {
        String argName = "currency" + String(i);
        if (server.hasArg(argName)) {
          String currencyCode = server.arg(argName);
          strcpy(currencies[i].code, currencyCode.c_str());
        }
      }
      
      // تحديث أسعار العملات
      updateCryptoPrices();
      
      // حفظ الإعدادات
      saveSettings();
      
      // إعادة التوجيه إلى الصفحة الرئيسية
      server.sendHeader("Location", "/", true);
      server.send(302, "text/plain", "");
    } else {
      server.send(400, "text/plain", "Invalid currency count");
    }
  } else {
    server.send(400, "text/plain", "Bad Request");
  }
}

// دالة لمسح EEPROM (للتشخيص)
void clearEEPROM() {
  Serial.println("مسح EEPROM...");
  for (int i = 0; i < EEPROM_SIZE; i++) {
    EEPROM.write(i, 0);
  }
  EEPROM.commit();
  Serial.println("تم مسح EEPROM بنجاح");
}

// دالة لمسح المنطقة الزمنية فقط
void clearTimezone() {
  Serial.println("مسح المنطقة الزمنية من EEPROM...");
  EEPROM.put(TIMEZONE_ADDR, 0);
  EEPROM.commit();
  Serial.println("تم مسح المنطقة الزمنية من EEPROM");
}