# CryptoClock - ساعة عرض أسعار العملات الرقمية

## 📱 وصف المشروع
ساعة ذكية تعرض أسعار العملات الرقمية في الوقت الفعلي مع إمكانية التحكم عبر واجهة ويب.

## 🛠️ المكونات المطلوبة
- **المعالج:** LOLIN D1 mini (ESP8266)
- **الشاشة:** LCD I2C GM12864-59N ver 2 (JLX12864 with ST7567 controller)
- **الاتصال:** WiFi
- **الطاقة:** USB أو بطارية

## ⚡ المميزات
- عرض أسعار العملات الرقمية في الوقت الفعلي
- دعم 10 عملات مختلفة (BTC, ETH, DOGE, XMR, إلخ)
- واجهة ويب للتحكم في الإعدادات
- إعدادات المنطقة الزمنية
- تنسيق 12/24 ساعة
- تحديث تلقائي للأسعار كل دقيقة
- عرض اتجاه التغيير (أسهم)

## 🔧 الإعدادات

### المكتبات المطلوبة
```cpp
#include <ESP8266WiFi.h>
#include <DNSServer.h>
#include <ESP8266WebServer.h>
#include <WiFiManager.h>
#include <ArduinoJson.h>
#include <TimeLib.h>
#include <NTPClient.h>
#include <WiFiUdp.h>
#include <EEPROM.h>
#include <U8g2lib.h>
#include <Wire.h>
#include <ESP8266HTTPClient.h>
#include <WiFiClientSecure.h>
```

### التوصيلات
- **VCC** → 3.3V
- **GND** → GND
- **SDA** → D2 (GPIO4)
- **SCL** → D1 (GPIO5)

## 🌐 واجهة الويب
بعد الاتصال بالواي فاي، يمكنك الوصول للإعدادات عبر:
- **الصفحة الرئيسية:** `http://[IP_ADDRESS]/`
- **إعدادات الوقت:** `http://[IP_ADDRESS]/time`
- **إعدادات الواي فاي:** `http://[IP_ADDRESS]/wifi`
- **إعدادات العملات:** `http://[IP_ADDRESS]/currency`

## 📊 العملات المدعومة
- Bitcoin (BTC)
- Bitcoin Cash (BCH)
- Dogecoin (DOGE)
- Monero (XMR)
- Ethereum (ETH)
- Litecoin (LTC)
- Ripple (XRP)
- Cardano (ADA)
- Polkadot (DOT)
- Chainlink (LINK)

## ⚙️ الإعدادات الافتراضية
- **المنطقة الزمنية:** +3 (توقيت الخليج العربي)
- **تنسيق الوقت:** 24 ساعة
- **العملات الافتراضية:** BTC, BCH, DOGE, XMR
- **مصدر البيانات:** CoinGecko API

## 🔄 التحديثات
- **الوقت:** كل ثانية
- **الأسعار:** كل دقيقة
- **العملات المعروضة:** كل 5 ثوانٍ

## 🎨 التخصيص
يمكن تخصيص:
- الخطوط المستخدمة
- ألوان الشاشة
- ترتيب العملات
- المنطقة الزمنية
- تنسيق الوقت

## 📝 الترخيص
هذا المشروع مفتوح المصدر ومتاح للاستخدام الشخصي والتعليمي.

## 👨‍💻 المطور
تم تطويره بواسطة Osama

## 🔗 الروابط المفيدة
- [LOLIN D1 mini](https://www.wemos.cc/en/latest/d1/d1_mini.html)
- [U8g2 Library](https://github.com/olikraus/u8g2)
- [CoinGecko API](https://www.coingecko.com/en/api)

---
**ملاحظة:** تأكد من تحديث المكتبات إلى أحدث إصدار قبل التجميع.
