/*
  ESP32 บัตร NFC เปิดประตู — แตะบัตรที่อนุญาต รีเลย์ปลดกลอน 3 วินาที แล้วล็อกกลับเอง
  K iTech Review (TikTok) · Kham iTech Lab (YouTube / Facebook)

  ต่อสาย
    PN532 (ตั้งสวิตช์บนโมดูลเป็นโหมด I2C)
      VCC -> 3.3V   GND -> GND   SDA -> GPIO 21   SCL -> GPIO 22
    รีเลย์   VCC -> 5V / VIN   GND -> GND   IN -> GPIO 26
    กลอนไฟฟ้า 12V ต่อผ่าน COM / NO ของรีเลย์ ใช้อะแดปเตอร์ 12V แยกจากบอร์ด
    ปุ่มเปิดจากด้านใน (ไม่ใส่ก็ได้)   ขาหนึ่ง -> GPIO 27   อีกขา -> GND

  วิธีเพิ่มบัตร
    1. อัปโหลด แล้วเปิด Serial Monitor ที่ 115200
    2. แตะบัตร จะขึ้น "บัตรไม่อยู่ในรายชื่อ UID: 04:A1:B2:C3"
    3. ก๊อปเลขนั้นมาใส่ใน ALLOWED ด้านล่าง แล้วอัปโหลดใหม่

  ไลบรารี: Adafruit PN532 (ติดตั้งจาก Library Manager จะลง Adafruit BusIO ให้ด้วย)
  บอร์ด: ESP32 รุ่นไหนก็ได้
*/

#include <Wire.h>
#include <Adafruit_PN532.h>

// ---------- รายชื่อบัตรที่อนุญาต ----------
const char *ALLOWED[] = {
  "04:A1:B2:C3",          // ตัวอย่าง เปลี่ยนเป็นเลขบัตรของคุณ
  "DE:AD:BE:EF",
};
const int ALLOWED_COUNT = sizeof(ALLOWED) / sizeof(ALLOWED[0]);

// ---------- ตั้งค่า ----------
const int  SDA_PIN    = 21;
const int  SCL_PIN    = 22;
const int  RELAY_PIN  = 26;
const int  BUTTON_PIN = 27;          // ปุ่มเปิดจากด้านใน ต่อลง GND
const bool RELAY_ACTIVE_LOW = true;  // รีเลย์ส่วนใหญ่ทำงานเมื่อ IN = LOW ถ้ากลอนค้างให้เปลี่ยนเป็น false
const unsigned long UNLOCK_MS = 3000;     // ปลดกลอนกี่มิลลิวินาที
const unsigned long SAME_CARD_MS = 2000;  // บัตรใบเดิมแตะค้าง ไม่นับซ้ำ

// ขา IRQ / RESET ของ PN532 ไม่ต้องต่อ (โหมด I2C ใช้แค่ SDA SCL) ใส่ขาว่างไว้ให้ไลบรารี
const int PN532_IRQ = 32;
const int PN532_RST = 33;

Adafruit_PN532 nfc(PN532_IRQ, PN532_RST, &Wire);

// ---------- ตัวแปร ----------
bool unlocked = false;
unsigned long unlockAt = 0;
String lastUid = "";
unsigned long lastSeen = 0;

void relayWrite(bool on) {
  digitalWrite(RELAY_PIN, (on ^ RELAY_ACTIVE_LOW) ? HIGH : LOW);
}

void unlockDoor(const char *why) {
  Serial.printf("ปลดล็อก (%s)\n", why);
  relayWrite(true);
  unlocked = true;
  unlockAt = millis();
}

String uidToString(const uint8_t *uid, uint8_t len) {
  String s;
  for (uint8_t i = 0; i < len; i++) {
    if (i) s += ':';
    if (uid[i] < 0x10) s += '0';
    s += String(uid[i], HEX);
  }
  s.toUpperCase();
  return s;
}

bool isAllowed(const String &uid) {
  for (int i = 0; i < ALLOWED_COUNT; i++) {
    if (uid.equalsIgnoreCase(ALLOWED[i])) return true;
  }
  return false;
}

void setup() {
  Serial.begin(115200);
  delay(200);
  pinMode(RELAY_PIN, OUTPUT);
  relayWrite(false);                 // ล็อกไว้ก่อนทุกครั้งที่เปิดเครื่อง
  pinMode(BUTTON_PIN, INPUT_PULLUP);

  Wire.begin(SDA_PIN, SCL_PIN);
  nfc.begin();
  uint32_t ver = nfc.getFirmwareVersion();
  if (!ver) {
    Serial.println("หา PN532 ไม่เจอ เช็กสาย SDA/SCL และสวิตช์โหมด I2C บนโมดูล");
    while (true) delay(1000);
  }
  Serial.printf("เจอ PN532 เฟิร์มแวร์ %d.%d\n", (ver >> 16) & 0xFF, (ver >> 8) & 0xFF);
  nfc.SAMConfig();
  Serial.printf("พร้อมแล้ว มีบัตรที่อนุญาต %d ใบ แตะบัตรได้เลย\n", ALLOWED_COUNT);
}

void loop() {
  // ครบเวลาแล้วล็อกกลับ
  if (unlocked && millis() - unlockAt >= UNLOCK_MS) {
    relayWrite(false);
    unlocked = false;
    Serial.println("ล็อกแล้ว");
  }

  // ปุ่มเปิดจากด้านใน
  if (digitalRead(BUTTON_PIN) == LOW && !unlocked) {
    delay(30);
    if (digitalRead(BUTTON_PIN) == LOW) unlockDoor("กดปุ่มด้านใน");
  }

  // อ่านบัตร (รอสูงสุด 100 ms ต่อรอบ ปุ่มกับการล็อกกลับยังทำงานได้)
  uint8_t uid[7];
  uint8_t len = 0;
  if (nfc.readPassiveTargetID(PN532_MIFARE_ISO14443A, uid, &len, 100)) {
    String s = uidToString(uid, len);
    if (s == lastUid && millis() - lastSeen < SAME_CARD_MS) {
      lastSeen = millis();
      return;                        // บัตรใบเดิมยังแตะค้างอยู่
    }
    lastUid = s;
    lastSeen = millis();

    if (isAllowed(s)) {
      unlockDoor(s.c_str());
    } else {
      Serial.print("บัตรไม่อยู่ในรายชื่อ UID: ");
      Serial.println(s);
    }
  }
}
