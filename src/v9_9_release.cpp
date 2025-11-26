// модификация handleController & handleGenerator решение проблемы отправки 1 раз а 10 мин. об изменении темп.
// Кольцевое логирование (Circular Logging)
// Добавлены проверки previousTemperatureController != 0 и previousHumidityController != 0 перед отправкой сообщений о росте/снижении в handleController
// Добавлены аналогичные проверки previousTemperatureGenerator != 0 и previousHumidityGenerator != 0 в handleGenerator
// Это предотвратит отправку сообщений о изменениях, когда предыдущие значения были нулевыми (например, при первом запуске системы)
// модифицированные функции handleController и handleGenerator с проверкой допустимых диапазонов:
// Добавлено сообщение "Появилось напряжение с города" отправляется когда:Напряжение сети появилось (>50V) Генератор работает (>50V) Независимо от состояния бензоклапана Сообщение еще не отправлялось
// Исправьте эти опечатки для грамотности сообщений!
// Добавлена проверка порогов вольтажа если аномалия длится 10 секунд с отправкой сообщений и проверка напряжения будет выполняться только когда есть актуальные данные, а не на каждой итерации loop()
// Порог 150 заменён на 50
// Оптимизация loop
// Глобальная оптимизация Заменяем векторы на статические массивы и переменные управления Полностью переписанная функция checkSensors Обновленная функция calculateMode для работы с массивами Обновленная функция calculateMedian для работы с массивами Вспомогательная функция для добавления значений в кольцевой буфер
// добавлено в loop currentMillis = millis() с изменением на currentMillis
// Слижком часто сообщения высокой температуры - Итоговый умный код с комбинированием всех функций Интеллектуальная отправка сообщений и Умный таймер с адаптивной задержкой и Комплексное умное собщение
// Увеличить минимальный интервал между сообщениями: Добавить фильтр по минимальной длительности аномалии: Увеличить гистерезис для восстановления: Добавить проверку стабильности напряжения: Увеличить базовую задержку: Добавить умную группировку сообщений: Добавить игнорирование одиночных скачков:
#include <WiFiClientSecure.h>
#include <UniversalTelegramBot.h>
#include <WiFi.h>
#include <AsyncTCP.h>
#include <ElegantOTA.h>
#include <ESPAsyncWebServer.h>
#include <SPIFFS.h>
#include <Arduino_JSON.h>
#include <ZMPT101B.h>
#include <Adafruit_NeoPixel.h>
#include <Button.h>
#include <Adafruit_BME280.h>
#include <Wire.h>
#include <vector>
#include <map>
#include <Secrets.h> // Переименовать lib/Secrets/Secrets.h_ в lib/Secrets/Secrets.h
#include <time.h>

#define WIFI_SSID SEC_WIFI_SSID
#define WIFI_PASS SEC_WIFI_PASS
#define BOT_TOKEN SEC_BOT_TOKEN
#define CHAT_ID SEC_CHAT_ID
#define NAME_BOT SEC_NAME_BOT
#define TG_NAMES SEC_TG_NAMES
#define PIN_Voltage_Network 1   // Pin для датчика напряжения ZMPT101B сети
#define PIN_Voltage_Generator 2 // Pin для датчика напряжения ZMPT101B генератора
#define PIN_Gas_Valve 35        // Контакт реле для бенинового клапан
#define PIN_Start_Reley 36      // Контакт реле для кнопки START
// Константы пороговых значений handle
#define TEMPERATURE_THRESHOLD 30        // Порог для температуры в °C
#define HUMIDITY_THRESHOLD 50           // Порог для влажности в %
#define TEMPERATURE_CHANGE_THRESHOLD 10 // Порог изменения температуры для сообщений
#define HUMIDITY_CHANGE_THRESHOLD 15    // Порог изменения влажности для сообщений
// Пороговые значения напряжения
#define VOLTAGE_HIGH_THRESHOLD 240 // Выше 240V - опасно
#define VOLTAGE_LOW_THRESHOLD 190  // Ниже 190V - опасно
#define SENSITIVITY 470.0f
#define NUM_OUTPUTS 2 // Указываем количество выходов для WEB
#define MAX_READINGS 10
#define VOLTAGE_HYSTERESIS 5.0f
#define MAX_HYSTERESIS 10.0
#define MIN_HYSTERESIS 3.0
const unsigned long VOLTAGE_ANOMALY_DURATION = 60000; // 10 секунд базовая задержка
const unsigned long MIN_ALERT_INTERVAL = 300000;      // 5 минут между сообщениями
const unsigned long MIN_ANOMALY_DURATION = 30000;     // 30 секунд минимальная длительность
unsigned long voltageHighStartTimeNetwork = 0;
unsigned long voltageLowStartTimeNetwork = 0;
unsigned long voltageHighStartTimeGenerator = 0;
unsigned long voltageLowStartTimeGenerator = 0;

Adafruit_BME280 bmeController;
Adafruit_BME280 bmeGenerator;
Adafruit_NeoPixel ledRGB(1, 48, NEO_RGB + NEO_KHZ800);
WiFiClientSecure client;
UniversalTelegramBot bot(BOT_TOKEN, client);

int botRequestDelay = 1000;
unsigned long lastTimeBotRan;

button PIN_Start_Button(37);
// #define VOLTmetr_1 = 34;
// #define VOLTmetr_2 = 39;

const char *ntpServer = "pool.ntp.org"; // NTP сервер
const long gmtOffsetSec = 3 * 3600;     // Смещение GMT для вашего часового пояса (например, +3 для Москвы)
const int daylightOffsetSec = 0;        // Смещение для зимнего/летнего времени

unsigned long start_time = 0;
int start_reley_count = 1;
unsigned long ledOnStartTime = millis();  // white led on
unsigned long ledOffStartTime = millis(); // white led off
bool isLedOn = false;                     // white led
unsigned long ledBlinkInterval = 200;     // interval for LED blinking (in milliseconds)
unsigned long previousMillis = 0;         // store last time LED was updated
int ledState = LOW;                       // current state of the LED
int blinkCount = 0;                       // how many times the LED has blinked
unsigned long noVoltageStartTime = 0;
const unsigned long NO_VOLTAGE_DURATION = 180000; // Старт генератора через 3 мин после падения города
// const unsigned long NO_VOLTAGE_DURATION = 5000; // 1 minutes in milliseconds
unsigned long yesVoltageStartTime = 0;
const unsigned long YES_VOLTAGE_DURATION = 60000; // Стоп генератора через 1 мин после появления города
// const unsigned long YES_VOLTAGE_DURATION = 5000; // 1 minutes in milliseconds
unsigned long lastConnectCheck = 0; // время последней проверки
bool stat_wifi = false;
String localWIFI_IP;
String message;
int vd;
static bool alertSent_NO_GOROD;      // Добавим переменную для отслеживания выполнения условия отправки разового сообщения после падения города
static bool alertSent_YES_GOROD;     // Добавим переменную для отслеживания выполнения условия отправки разового сообщения после появления города
static bool alertSent_GEN_STARTED;   // Добавим переменную для отслеживания выполнения условия отправки разового сообщения после запуска генератора
static bool alertSent_GEN_NOT_START; // Добавим переменную для отслеживания выполнения условия отправки разового сообщения после не запуска генератора
int alertSent_NO_NET = 0;
int icon;
int msg_network_on;
int start_from_tg = 0;

// Флаги для отслеживания состояния сообщений
bool messageSentTemperature = false;         // Флаг для высоких температур у контроллера и генератора
bool recoveryMessageSentTemperature = false; // Флаг для восстановления нормальной температуры у контроллера и генератора
bool messageSentHumidity = false;            // Флаг для высоких влажностей у контроллера и генератора
bool recoveryMessageSentHumidity = false;    // Флаг для восстановления нормальной влажности у контроллера и генератора

// Предыдущие значения температуры и влажности. Раздельные переменные для контроллера и генератора
int previousTemperatureController = 0;
int previousHumidityController = 0;
int previousTemperatureGenerator = 0;
int previousHumidityGenerator = 0;

// Последние отправленные значения
int lastReportedTemperatureController = 0;
int lastReportedHumidityController = 0;
int lastReportedTemperatureGenerator = 0;
int lastReportedHumidityGenerator = 0;

// Значения для сообщений
int highTemperatureValueController = 0;
int highHumidityValueController = 0;
int highTemperatureValueGenerator = 0;
int highHumidityValueGenerator = 0;

bool firstRiseFallTemperatureMessageController = true;
bool firstRiseFallHumidityMessageController = true;
bool firstRiseFallTemperatureMessageGenerator = true;
bool firstRiseFallHumidityMessageGenerator = true;
// Флаги для отслеживания сообщений о напряжении
bool voltageHighAlertSentNetwork = false;
bool voltageLowAlertSentNetwork = false;
bool voltageHighAlertSentGenerator = false;
bool voltageLowAlertSentGenerator = false;
// ФЛАГИ для отслеживания восстановления напряжения
bool voltageReturnedToNormalNetwork = true; // Изначально норма
bool voltageReturnedToNormalGenerator = true;
// checkSensors
unsigned long previousMillisCheckSensors = 0; // Время последнего вызова checkSensors
const unsigned long interval = 630000;        // Интервал 10.5 минут в миллисекундах
unsigned long previousMillisCheck = 0;        // Время последней проверки
const unsigned long checkInterval = 60000;    // 1 минута в миллисекундах
int checkCount = 0;                           // Счетчик проверок
int temperatureControllerReadings[MAX_READINGS];
int humidityControllerReadings[MAX_READINGS];
int temperatureGeneratorReadings[MAX_READINGS];
int humidityGeneratorReadings[MAX_READINGS];

int tempControllerIndex = 0, humControllerIndex = 0;
int tempGeneratorIndex = 0, humGeneratorIndex = 0;
int tempControllerCount = 0, humControllerCount = 0;
int tempGeneratorCount = 0, humGeneratorCount = 0;

int outputGPIOs[NUM_OUTPUTS] = {PIN_Gas_Valve, PIN_Start_Reley}; // Присваиваем каждому GPIO свой выход

ZMPT101B zmpt_network(PIN_Voltage_Network, 50.0);
ZMPT101B zmpt_generator(PIN_Voltage_Generator, 50.0);

AsyncWebServer server(80);
AsyncWebSocket ws("/ws");
JSONVar myArray;
unsigned long voltageDropTime = 0; // Время, когда напряжение пропало
bool voltageDropDetected = false;  // Флаг, указывающий, что отключение напряжения обнаружено

// Структура для умной группировки сообщений вольтажа
struct VoltageAlert
{
  float minVoltage;
  float maxVoltage;
  unsigned long startTime;
  int fluctuationCount;
  bool active;
  bool messageSent; // Флаг отправки сообщения
};
// Переменные для умной системы вольтажа
VoltageAlert voltageAlertNetwork = {0, 0, 0, 0, false};
VoltageAlert voltageAlertGenerator = {0, 0, 0, 0, false};
int voltageFluctuationCount = 0;
unsigned long lastFluctuationTime = 0;
unsigned long lastVoltageAlertTime = 0;
float dynamicHysteresis = VOLTAGE_HYSTERESIS;

#pragma pack(push, 1) // Без выравнивания для экономии места
struct SensorRecord
{
  uint32_t timestamp;      // UNIX-время (4 байта)
  int16_t temp_controller; // Температура *10 (например 23.5°C = 235)
  int16_t temp_generator;
  uint8_t hum_controller; // Влажность 0-100%
  uint8_t hum_generator;
  uint16_t press_controller; // Давление в hPa (например 1013.25 hPa = 101325/100)
  uint16_t press_generator;
  uint16_t voltage_network; // Напряжение сети *10 (230.5V = 2305)
  uint16_t voltage_generator;
  uint8_t gpio_states; // Битовая маска: 0b00000011 (бит 0: газ, бит 1: старт)
};
#pragma pack(pop) // Итого: 15 байт на запись

const char *SENSOR_DATA_FILE = "/sensor.dat";
const size_t MAX_RECORDS = 10080; // 7 дней × 1440 минут

// Структура для настроек сообщений
struct MessageSettings
{
  String voltageHigh;
  String voltageLow;
  String tempHigh;
  String tempNormal;
  String humidityHigh;
  String humidityNormal;
  String tempRising;
  String tempFalling;
  String humidityRising;
  String humidityFalling;
  String generatorStarted;
  String generatorStopped;
  String voltageLost;
  String voltageRestored;
  String startMessage;
  String location;
  String deviceName;
  String customAddress;
};

MessageSettings msgSettings = {
    "⚡️ Высокое напряжение {source}: Мин: {minV}V, Макс: {maxV}V, Колебаний: {fluct}",
    "⚡️ Низкое напряжение {source}: Мин: {minV}V, Макс: {maxV}V, Колебаний: {fluct}",
    "🔥 Температура у {device} высокая: {temp} °C",
    "✅ Температура у {device} вернулась в норму: {temp} °C",
    "💧 Влажность у {device} высокая: {hum} %",
    "✅ Влажность у {device} вернулась в норму: {hum} %",
    "📈 Температура у {device} продолжает расти: {temp} °C",
    "📉 Температура у {device} немного снизилась: {temp} °C",
    "📈 Влажность у {device} продолжает расти: {hum} %",
    "📉 Влажность у {device} немного снизилась: {hum} %",
    "🟢 Запустился генератор. Напряжение: {volt} V",
    "🔴 Генератор остановлен",
    "⚡️ Пропало напряжение с города! Запуск генератора через {min} мин.",
    "⚡️ Появилось напряжение с города",
    "🚀 Контроллер генератора запущен",
    "mytischi.gercena.1к3.uzel",
    "Контроллер генератора"
    "10.160.231.25:8080"};

// Структура для сохранения состояния
struct SystemState
{
  bool alertSent_NO_GOROD;
  bool alertSent_YES_GOROD;
  bool alertSent_GEN_STARTED;
  bool alertSent_GEN_NOT_START;
  int alertSent_NO_NET;
  int start_reley_count;
  bool messageSentTemperature;
  bool messageSentHumidity;
  int previousTemperatureController;
  int previousHumidityController;
  int previousTemperatureGenerator;
  int previousHumidityGenerator;
  int lastReportedTemperatureController;
  int lastReportedHumidityController;
  int lastReportedTemperatureGenerator;
  int lastReportedHumidityGenerator;
};

SystemState systemState;

const char *STATE_FILE = "/system_state.json";
const char *SETTINGS_FILE = "/message_settings.json";

// Прототипы функций
bool isInRange(time_t timestamp, const String &range);
String formatTime(time_t timestamp);
void saveSystemState();
void loadSystemState();
void saveMessageSettings();
void loadMessageSettings();
String getFooter();

// Реализация функции проверки диапазона
bool isInRange(time_t timestamp, const String &range)
{
  time_t now = time(NULL);
  if (range == "24h")
  {
    return timestamp >= (now - 86400); // 24 часа назад
  }
  else if (range == "1h")
  {
    return timestamp >= (now - 3600); // 1 час назад
  }
  return false;
}

// Реализация функции форматирования времени
String formatTime(time_t timestamp)
{
  struct tm *timeinfo;
  timeinfo = localtime(&timestamp);
  char buffer[6]; // Для HH:MM
  strftime(buffer, sizeof(buffer), "%H:%M", timeinfo);
  return String(buffer);
}

String getDateTime()
{
  struct tm timeInfo;
  if (!getLocalTime(&timeInfo))
  {
    return "Ошибка получения времени";
  }

  // Форматируем строку времени
  char dateTimeStr[20];
  snprintf(dateTimeStr, sizeof(dateTimeStr), "%02d-%02d-%04d %02d:%02d:%02d",
           timeInfo.tm_mday,
           timeInfo.tm_mon + 1,     // tm_mon начинается с 0
           timeInfo.tm_year + 1900, // tm_year начинается с 1900
           timeInfo.tm_hour,
           timeInfo.tm_min,
           timeInfo.tm_sec);
  return String(dateTimeStr);
}

String getFooter()
{
  String address = msgSettings.customAddress;
  if (address == "")
  {
    address = "http://" + localWIFI_IP;
  }
  return "\n📍 " + msgSettings.location + " | " + msgSettings.deviceName + "\n🌐 " + address;
}

void logEvent(const String &event)
{
  const char *LOG_FILE = "/event.log";
  const size_t MAX_LOG_SIZE = 1024000; // 500 КБ (максимальный размер для ESP32-S3)
  const size_t MAX_LINE_LENGTH = 256;  // Максимальная длина строки лога

  if (!SPIFFS.begin(true))
  {
    Serial.println("SPIFFS mount failed!");
    return;
  }

  // Если файла нет, создаём и записываем событие
  if (!SPIFFS.exists(LOG_FILE))
  {
    File file = SPIFFS.open(LOG_FILE, "a");
    if (!file)
    {
      Serial.println("Failed to create log file!");
      return;
    }
    file.println(getDateTime() + " - " + event);
    file.close();
    return;
  }

  // Открываем файл для чтения и записи
  File file = SPIFFS.open(LOG_FILE, "r+");
  if (!file)
  {
    Serial.println("Failed to open log file!");
    return;
  }

  // Если файл меньше MAX_LOG_SIZE, просто добавляем запись в конец
  if (file.size() < MAX_LOG_SIZE)
  {
    file.seek(file.size());
    file.println(getDateTime() + " - " + event);
    file.close();
    return;
  }

  // 🔄 Режим кольцевого логирования (файл заполнен)
  size_t fileSize = file.size();
  size_t writePos = 0;
  bool foundEmptyLine = false;

  // Ищем первую строку, которую можно перезаписать
  while (writePos < fileSize)
  {
    file.seek(writePos);
    String line = file.readStringUntil('\n');

    // Если строка пустая (например, после удаления), пишем сюда
    if (line.length() == 0)
    {
      foundEmptyLine = true;
      break;
    }

    // Иначе двигаемся к следующей строке
    writePos += line.length() + 1; // +1 для символа '\n'
  }

  // Если не нашли пустую строку, перезаписываем с начала файла
  if (!foundEmptyLine)
  {
    writePos = 0;
  }

  // Записываем новое событие
  String logEntry = getDateTime() + " - " + event + "\n";
  file.seek(writePos);
  file.print(logEntry);
  file.close();
}

void saveSystemState()
{
  File file = SPIFFS.open(STATE_FILE, "w");
  if (!file)
  {
    Serial.println("Failed to open state file for writing");
    return;
  }

  JSONVar state;
  state["alertSent_NO_GOROD"] = alertSent_NO_GOROD;
  state["alertSent_YES_GOROD"] = alertSent_YES_GOROD;
  state["alertSent_GEN_STARTED"] = alertSent_GEN_STARTED;
  state["alertSent_GEN_NOT_START"] = alertSent_GEN_NOT_START;
  state["alertSent_NO_NET"] = alertSent_NO_NET;
  state["start_reley_count"] = start_reley_count;
  state["messageSentTemperature"] = messageSentTemperature;
  state["messageSentHumidity"] = messageSentHumidity;
  state["previousTemperatureController"] = previousTemperatureController;
  state["previousHumidityController"] = previousHumidityController;
  state["previousTemperatureGenerator"] = previousTemperatureGenerator;
  state["previousHumidityGenerator"] = previousHumidityGenerator;
  state["lastReportedTemperatureController"] = lastReportedTemperatureController;
  state["lastReportedHumidityController"] = lastReportedHumidityController;
  state["lastReportedTemperatureGenerator"] = lastReportedTemperatureGenerator;
  state["lastReportedHumidityGenerator"] = lastReportedHumidityGenerator;

  String jsonString = JSON.stringify(state);
  file.print(jsonString);
  file.close();
}

void loadSystemState()
{
  if (!SPIFFS.exists(STATE_FILE))
  {
    Serial.println("State file not exists, using defaults");
    return;
  }

  File file = SPIFFS.open(STATE_FILE, "r");
  if (!file)
  {
    Serial.println("Failed to open state file for reading");
    return;
  }

  String jsonString = file.readString();
  file.close();

  JSONVar state = JSON.parse(jsonString);
  if (JSON.typeof(state) == "undefined")
  {
    Serial.println("Failed to parse state JSON");
    return;
  }

  alertSent_NO_GOROD = (bool)state["alertSent_NO_GOROD"];
  alertSent_YES_GOROD = (bool)state["alertSent_YES_GOROD"];
  alertSent_GEN_STARTED = (bool)state["alertSent_GEN_STARTED"];
  alertSent_GEN_NOT_START = (bool)state["alertSent_GEN_NOT_START"];
  alertSent_NO_NET = (int)state["alertSent_NO_NET"];
  start_reley_count = (int)state["start_reley_count"];
  messageSentTemperature = (bool)state["messageSentTemperature"];
  messageSentHumidity = (bool)state["messageSentHumidity"];
  previousTemperatureController = (int)state["previousTemperatureController"];
  previousHumidityController = (int)state["previousHumidityController"];
  previousTemperatureGenerator = (int)state["previousTemperatureGenerator"];
  previousHumidityGenerator = (int)state["previousHumidityGenerator"];
  lastReportedTemperatureController = (int)state["lastReportedTemperatureController"];
  lastReportedHumidityController = (int)state["lastReportedHumidityController"];
  lastReportedTemperatureGenerator = (int)state["lastReportedTemperatureGenerator"];
  lastReportedHumidityGenerator = (int)state["lastReportedHumidityGenerator"];
}

void saveMessageSettings()
{
  File file = SPIFFS.open(SETTINGS_FILE, "w");
  if (!file)
  {
    Serial.println("Failed to open settings file for writing");
    return;
  }

  JSONVar settings;
  settings["voltageHigh"] = msgSettings.voltageHigh;
  settings["voltageLow"] = msgSettings.voltageLow;
  settings["tempHigh"] = msgSettings.tempHigh;
  settings["tempNormal"] = msgSettings.tempNormal;
  settings["humidityHigh"] = msgSettings.humidityHigh;
  settings["humidityNormal"] = msgSettings.humidityNormal;
  settings["tempRising"] = msgSettings.tempRising;
  settings["tempFalling"] = msgSettings.tempFalling;
  settings["humidityRising"] = msgSettings.humidityRising;
  settings["humidityFalling"] = msgSettings.humidityFalling;
  settings["generatorStarted"] = msgSettings.generatorStarted;
  settings["generatorStopped"] = msgSettings.generatorStopped;
  settings["voltageLost"] = msgSettings.voltageLost;
  settings["voltageRestored"] = msgSettings.voltageRestored;
  settings["startMessage"] = msgSettings.startMessage;
  settings["location"] = msgSettings.location;
  settings["deviceName"] = msgSettings.deviceName;
  settings["customAddress"] = msgSettings.customAddress;

  String jsonString = JSON.stringify(settings);
  file.print(jsonString);
  file.close();
}

void loadMessageSettings()
{
  if (!SPIFFS.exists(SETTINGS_FILE))
  {
    Serial.println("Settings file not exists, using defaults");
    saveMessageSettings(); // Сохраняем настройки по умолчанию
    return;
  }

  File file = SPIFFS.open(SETTINGS_FILE, "r");
  if (!file)
  {
    Serial.println("Failed to open settings file for reading");
    return;
  }

  String jsonString = file.readString();
  file.close();

  JSONVar settings = JSON.parse(jsonString);
  if (JSON.typeof(settings) == "undefined")
  {
    Serial.println("Failed to parse settings JSON");
    return;
  }

  msgSettings.voltageHigh = (const char *)settings["voltageHigh"];
  msgSettings.voltageLow = (const char *)settings["voltageLow"];
  msgSettings.tempHigh = (const char *)settings["tempHigh"];
  msgSettings.tempNormal = (const char *)settings["tempNormal"];
  msgSettings.humidityHigh = (const char *)settings["humidityHigh"];
  msgSettings.humidityNormal = (const char *)settings["humidityNormal"];
  msgSettings.tempRising = (const char *)settings["tempRising"];
  msgSettings.tempFalling = (const char *)settings["tempFalling"];
  msgSettings.humidityRising = (const char *)settings["humidityRising"];
  msgSettings.humidityFalling = (const char *)settings["humidityFalling"];
  msgSettings.generatorStarted = (const char *)settings["generatorStarted"];
  msgSettings.generatorStopped = (const char *)settings["generatorStopped"];
  msgSettings.voltageLost = (const char *)settings["voltageLost"];
  msgSettings.voltageRestored = (const char *)settings["voltageRestored"];
  msgSettings.startMessage = (const char *)settings["startMessage"];
  msgSettings.location = (const char *)settings["location"];
  msgSettings.deviceName = (const char *)settings["deviceName"];
  msgSettings.customAddress = (const char *)settings["customAddress"];
}

void handleController(int temperature, int humidity)
{
  if (temperature < -30 || temperature > 80)
  {
    return; // Прекращаем обработку
  }
  if (humidity < 1 || humidity > 100)
  {
    return; // Прекращаем обработку
  }
  // Проверка на значительное изменение
  if (abs(temperature - lastReportedTemperatureController) >= TEMPERATURE_CHANGE_THRESHOLD ||
      abs(humidity - lastReportedHumidityController) >= HUMIDITY_CHANGE_THRESHOLD)
  {

    // Проверка температуры
    if (temperature > TEMPERATURE_THRESHOLD && !messageSentTemperature)
    {
      String tempMsg = msgSettings.tempHigh;
      tempMsg.replace("{device}", "контроллера");
      tempMsg.replace("{temp}", String(temperature));
      bot.sendMessage(CHAT_ID, tempMsg + getFooter(), "");
      highTemperatureValueController = temperature;
      messageSentTemperature = true;
      logEvent("Высокая температура у контроллера: " + String(temperature) + " °C (было " + String(previousTemperatureController) + " °C)");
      saveSystemState();
    }

    if (temperature <= TEMPERATURE_THRESHOLD && messageSentTemperature)
    {
      String tempMsg = msgSettings.tempNormal;
      tempMsg.replace("{device}", "контроллера");
      tempMsg.replace("{temp}", String(temperature));
      bot.sendMessage(CHAT_ID, tempMsg + getFooter(), "");
      recoveryMessageSentTemperature = true;
      messageSentTemperature = false;
      logEvent("Температура у контроллера восстановлена: " + String(temperature) + " °C (было " + String(previousTemperatureController) + " °C)");
      saveSystemState();
    }

    // Проверка влажности
    if (humidity > HUMIDITY_THRESHOLD && !messageSentHumidity)
    {
      String humMsg = msgSettings.humidityHigh;
      humMsg.replace("{device}", "контроллера");
      humMsg.replace("{hum}", String(humidity));
      bot.sendMessage(CHAT_ID, humMsg + getFooter(), "");
      highHumidityValueController = humidity;
      messageSentHumidity = true;
      logEvent("Высокая влажность у контроллера: " + String(humidity) + " % (было " + String(previousHumidityController) + " %)");
      saveSystemState();
    }

    if (humidity <= HUMIDITY_THRESHOLD && messageSentHumidity)
    {
      String humMsg = msgSettings.humidityNormal;
      humMsg.replace("{device}", "контроллера");
      humMsg.replace("{hum}", String(humidity));
      bot.sendMessage(CHAT_ID, humMsg + getFooter(), "");
      recoveryMessageSentHumidity = true;
      messageSentHumidity = false;
      logEvent("Влажность у контроллера восстановлена: " + String(humidity) + " % (было " + String(previousHumidityController) + " %)");
      saveSystemState();
    }

    // Сообщения о росте/снижении (с проверкой на нулевые значения)
    if (previousTemperatureController != 0 && temperature - previousTemperatureController >= TEMPERATURE_CHANGE_THRESHOLD)
    {
      String tempMsg = msgSettings.tempRising;
      tempMsg.replace("{device}", "контроллера");
      tempMsg.replace("{temp}", String(temperature));
      bot.sendMessage(CHAT_ID, tempMsg + getFooter(), "");
      logEvent("Температура у контроллера растет: " + String(temperature) + " °C (было " + String(previousTemperatureController) + " °C)");
    }
    else if (previousTemperatureController != 0 && previousTemperatureController - temperature >= TEMPERATURE_CHANGE_THRESHOLD)
    {
      String tempMsg = msgSettings.tempFalling;
      tempMsg.replace("{device}", "контроллера");
      tempMsg.replace("{temp}", String(temperature));
      bot.sendMessage(CHAT_ID, tempMsg + getFooter(), "");
      logEvent("Температура у контроллера снизилась: " + String(temperature) + " °C (было " + String(previousTemperatureController) + " °C)");
    }

    if (previousHumidityController != 0 && humidity - previousHumidityController >= HUMIDITY_CHANGE_THRESHOLD)
    {
      String humMsg = msgSettings.humidityRising;
      humMsg.replace("{device}", "контроллера");
      humMsg.replace("{hum}", String(humidity));
      bot.sendMessage(CHAT_ID, humMsg + getFooter(), "");
      logEvent("Влажность у контроллера растет: " + String(humidity) + " % (было " + String(previousHumidityController) + " %)");
    }
    else if (previousHumidityController != 0 && previousHumidityController - humidity >= HUMIDITY_CHANGE_THRESHOLD)
    {
      String humMsg = msgSettings.humidityFalling;
      humMsg.replace("{device}", "контроллера");
      humMsg.replace("{hum}", String(humidity));
      bot.sendMessage(CHAT_ID, humMsg + getFooter(), "");
      logEvent("Влажность у контроллера снизилась: " + String(humidity) + " % (было " + String(previousHumidityController) + " %)");
    }

    // Обновляем последние отправленные значения
    lastReportedTemperatureController = temperature;
    lastReportedHumidityController = humidity;
    saveSystemState();
  }

  // Обновление предыдущих значений
  previousTemperatureController = temperature;
  previousHumidityController = humidity;
}

void handleGenerator(int temperature, int humidity)
{
  if (temperature < -30 || temperature > 80)
  {
    return; // Прекращаем обработку
  }
  if (humidity < 1 || humidity > 100)
  {
    return; // Прекращаем обработку
  }
  // Проверка на значительное изменение
  if (abs(temperature - lastReportedTemperatureGenerator) >= TEMPERATURE_CHANGE_THRESHOLD ||
      abs(humidity - lastReportedHumidityGenerator) >= HUMIDITY_CHANGE_THRESHOLD)
  {

    // Проверка температуры
    if (temperature > TEMPERATURE_THRESHOLD && !messageSentTemperature)
    {
      String tempMsg = msgSettings.tempHigh;
      tempMsg.replace("{device}", "генератора");
      tempMsg.replace("{temp}", String(temperature));
      bot.sendMessage(CHAT_ID, tempMsg + getFooter(), "");
      highTemperatureValueGenerator = temperature;
      messageSentTemperature = true;
      logEvent("Высокая температура у генератора: " + String(temperature) + " °C (было " + String(previousTemperatureGenerator) + " °C)");
      saveSystemState();
    }

    if (temperature <= TEMPERATURE_THRESHOLD && messageSentTemperature)
    {
      String tempMsg = msgSettings.tempNormal;
      tempMsg.replace("{device}", "генератора");
      tempMsg.replace("{temp}", String(temperature));
      bot.sendMessage(CHAT_ID, tempMsg + getFooter(), "");
      recoveryMessageSentTemperature = true;
      messageSentTemperature = false;
      logEvent("Температура у генератора восстановлена: " + String(temperature) + " °C (было " + String(previousTemperatureGenerator) + " °C)");
      saveSystemState();
    }

    // Проверка влажности
    if (humidity > HUMIDITY_THRESHOLD && !messageSentHumidity)
    {
      String humMsg = msgSettings.humidityHigh;
      humMsg.replace("{device}", "генератора");
      humMsg.replace("{hum}", String(humidity));
      bot.sendMessage(CHAT_ID, humMsg + getFooter(), "");
      highHumidityValueGenerator = humidity;
      messageSentHumidity = true;
      logEvent("Высокая влажность у генератора: " + String(humidity) + "% (было " + String(previousHumidityGenerator) + "%)");
      saveSystemState();
    }

    if (humidity <= HUMIDITY_THRESHOLD && messageSentHumidity)
    {
      String humMsg = msgSettings.humidityNormal;
      humMsg.replace("{device}", "генератора");
      humMsg.replace("{hum}", String(humidity));
      bot.sendMessage(CHAT_ID, humMsg + getFooter(), "");
      recoveryMessageSentHumidity = true;
      messageSentHumidity = false;
      logEvent("Влажность у генератора восстановлена: " + String(humidity) + "% (было " + String(previousHumidityGenerator) + "%)");
      saveSystemState();
    }

    // Сообщения о росте/снижении (с проверкой на нулевые значения)
    if (previousTemperatureGenerator != 0 && temperature - previousTemperatureGenerator >= TEMPERATURE_CHANGE_THRESHOLD)
    {
      String tempMsg = msgSettings.tempRising;
      tempMsg.replace("{device}", "генератора");
      tempMsg.replace("{temp}", String(temperature));
      bot.sendMessage(CHAT_ID, tempMsg + getFooter(), "");
      logEvent("Температура у генератора растет: " + String(temperature) + " °C (было " + String(previousTemperatureGenerator) + " °C)");
    }
    else if (previousTemperatureGenerator != 0 && previousTemperatureGenerator - temperature >= TEMPERATURE_CHANGE_THRESHOLD)
    {
      String tempMsg = msgSettings.tempFalling;
      tempMsg.replace("{device}", "генератора");
      tempMsg.replace("{temp}", String(temperature));
      bot.sendMessage(CHAT_ID, tempMsg + getFooter(), "");
      logEvent("Температура у генератора снизилась: " + String(temperature) + " °C (было " + String(previousTemperatureGenerator) + " °C)");
    }

    if (previousHumidityGenerator != 0 && humidity - previousHumidityGenerator >= HUMIDITY_CHANGE_THRESHOLD)
    {
      String humMsg = msgSettings.humidityRising;
      humMsg.replace("{device}", "генератора");
      humMsg.replace("{hum}", String(humidity));
      bot.sendMessage(CHAT_ID, humMsg + getFooter(), "");
      logEvent("Влажность у генератора растет: " + String(humidity) + "% (было " + String(previousHumidityGenerator) + "%)");
    }
    else if (previousHumidityGenerator != 0 && previousHumidityGenerator - humidity >= HUMIDITY_CHANGE_THRESHOLD)
    {
      String humMsg = msgSettings.humidityFalling;
      humMsg.replace("{device}", "генератора");
      humMsg.replace("{hum}", String(humidity));
      bot.sendMessage(CHAT_ID, humMsg + getFooter(), "");
      logEvent("Влажность у генератора снизилась: " + String(humidity) + "% (было " + String(previousHumidityGenerator) + "%)");
    }

    // Обновляем последние отправленные значения
    lastReportedTemperatureGenerator = temperature;
    lastReportedHumidityGenerator = humidity;
    saveSystemState();
  }

  // Обновление предыдущих значений
  previousTemperatureGenerator = temperature;
  previousHumidityGenerator = humidity;
}

// Обновленная функция calculateMedian для работы с массивами
int calculateMedian(int *array, int count)
{
  if (count == 0)
    return 0;

  // Создаем временный массив для сортировки
  int temp[MAX_READINGS];
  memcpy(temp, array, count * sizeof(int));

  // Простая bubble sort (достаточно для 10 элементов)
  for (int i = 0; i < count - 1; i++)
  {
    for (int j = 0; j < count - i - 1; j++)
    {
      if (temp[j] > temp[j + 1])
      {
        int swap = temp[j];
        temp[j] = temp[j + 1];
        temp[j + 1] = swap;
      }
    }
  }

  if (count % 2 == 0)
  {
    return (temp[count / 2 - 1] + temp[count / 2]) / 2;
  }
  else
  {
    return temp[count / 2];
  }
}

// Обновленная функция calculateMode для работы с массивами
int calculateMode(int *array, int count)
{
  if (count == 0)
    return 0;

  int frequency[100] = {0}; // Предполагаем значения 0-99 для температуры/влажности
  int maxCount = 0;
  int mode = array[0];

  for (int i = 0; i < count; i++)
  {
    if (array[i] >= 0 && array[i] < 100)
    {
      frequency[array[i]]++;
      if (frequency[array[i]] > maxCount)
      {
        maxCount = frequency[array[i]];
        mode = array[i];
      }
    }
  }

  return mode;
}

void addReading(int *array, int &index, int &count, int value)
{
  array[index] = value;
  index = (index + 1) % MAX_READINGS;
  if (count < MAX_READINGS)
  {
    count++;
  }
}

String getOutputStates()
{
  myArray["temperature1"] = String(bmeController.readTemperature());
  myArray["humidity1"] = String(bmeController.readHumidity());
  myArray["pressure1"] = String(bmeController.readPressure() / 133.322);
  myArray["temperature2"] = String(bmeGenerator.readTemperature());
  myArray["humidity2"] = String(bmeGenerator.readHumidity());
  myArray["pressure2"] = String(bmeGenerator.readPressure() / 133.322);
  // Добавляем пороговые значения
  myArray["temp_threshold"] = TEMPERATURE_THRESHOLD;
  myArray["hum_threshold"] = HUMIDITY_THRESHOLD;

  for (int i = 0; i < NUM_OUTPUTS; i++)
  {
    myArray["gpios"][i]["output"] = String(outputGPIOs[i]);
    myArray["gpios"][i]["state"] = String(digitalRead(outputGPIOs[i]));
  }
  // Получаем напряжение генератора и проверяем его состояние
  float generatorVoltage = zmpt_generator.getRmsVoltage();
  myArray["generatorVoltage"] = String(generatorVoltage);
  myArray["generatorState"] = (generatorVoltage > 50) ? "1" : "0"; // "1" - запущен, "0" - остановлен
                                                                   // Получаем напряжение с города и проверяем его состояние
  float networkVoltage = zmpt_network.getRmsVoltage();             // Получаем напряжение с города
  myArray["networkVoltage"] = String(networkVoltage);
  myArray["networkState"] = (networkVoltage > 50) ? "В порядке" : "Отключено"; // Проверка состояния

  return JSON.stringify(myArray);
}

void notifyClients(String state)
{
  ws.textAll(state);
}

void checkSensors()
{
  unsigned long currentMillis = millis();

  if (currentMillis - previousMillisCheckSensors >= interval)
  {
    previousMillisCheckSensors = currentMillis;
    checkCount = 0;

    // Сбрасываем счетчики вместо очистки векторов
    tempControllerIndex = 0;
    humControllerIndex = 0;
    tempGeneratorIndex = 0;
    humGeneratorIndex = 0;
    tempControllerCount = 0;
    humControllerCount = 0;
    tempGeneratorCount = 0;
    humGeneratorCount = 0;

    notifyClients(getOutputStates()); // Отправляем текущие состояния всем клиентам
  }

  if (currentMillis - previousMillisCheck >= checkInterval && checkCount < MAX_READINGS)
  {
    previousMillisCheck = currentMillis;

    // Чтение данных с датчиков
    int temperatureController = (int)bmeController.readTemperature();
    int humidityController = (int)bmeController.readHumidity();
    int temperatureGenerator = (int)bmeGenerator.readTemperature();
    int humidityGenerator = (int)bmeGenerator.readHumidity();

    // Добавление данных в массивы вместо векторов
    addReading(temperatureControllerReadings, tempControllerIndex, tempControllerCount, temperatureController);
    addReading(humidityControllerReadings, humControllerIndex, humControllerCount, humidityController);
    addReading(temperatureGeneratorReadings, tempGeneratorIndex, tempGeneratorCount, temperatureGenerator);
    addReading(humidityGeneratorReadings, humGeneratorIndex, humGeneratorCount, humidityGenerator);

    if (checkCount == MAX_READINGS - 1)
    { // checkCount == 9 при MAX_READINGS = 10
      // Вычисление медианы и моды
      int medianControllerTemperature = calculateMedian(temperatureControllerReadings, tempControllerCount);
      int medianControllerHumidity = calculateMedian(humidityControllerReadings, humControllerCount);
      int modeControllerTemperature = calculateMode(temperatureControllerReadings, tempControllerCount);
      int modeControllerHumidity = calculateMode(humidityControllerReadings, humControllerCount);
      int medianGeneratorTemperature = calculateMedian(temperatureGeneratorReadings, tempGeneratorCount);
      int medianGeneratorHumidity = calculateMedian(humidityGeneratorReadings, humGeneratorCount);
      int modeGeneratorTemperature = calculateMode(temperatureGeneratorReadings, tempGeneratorCount);
      int modeGeneratorHumidity = calculateMode(humidityGeneratorReadings, humGeneratorCount);

      // Вычисление средних значений
      int averageControllerTemperature = (medianControllerTemperature + modeControllerTemperature) / 2;
      int averageControllerHumidity = (medianControllerHumidity + modeControllerHumidity) / 2;
      int averageGeneratorTemperature = (medianGeneratorTemperature + modeGeneratorTemperature) / 2;
      int averageGeneratorHumidity = (medianGeneratorHumidity + modeGeneratorHumidity) / 2;

      // Проверяем значимые изменения перед вызовом обработчиков
      if (abs(averageControllerTemperature - lastReportedTemperatureController) >= TEMPERATURE_CHANGE_THRESHOLD ||
          abs(averageControllerHumidity - lastReportedHumidityController) >= HUMIDITY_CHANGE_THRESHOLD)
      {
        handleController(averageControllerTemperature, averageControllerHumidity);
      }

      if (abs(averageGeneratorTemperature - lastReportedTemperatureGenerator) >= TEMPERATURE_CHANGE_THRESHOLD ||
          abs(averageGeneratorHumidity - lastReportedHumidityGenerator) >= HUMIDITY_CHANGE_THRESHOLD)
      {
        handleGenerator(averageGeneratorTemperature, averageGeneratorHumidity);
      }
    }
    checkCount++;
  }
}

// Запускаем SPIFFS
void initSPIFFS()
{
  if (!SPIFFS.begin(true))
  {
    message = "An error has occurred while mounting SPIFFS";
    bot.sendMessage(CHAT_ID, "Ошибка при монтировании SPIFFS", "");
    // return;
  }
}

void start_generator()
{
  if (digitalRead(PIN_Gas_Valve) == LOW)
  {
    ledRGB.fill(0x0000FF);
    ledRGB.show();
    digitalWrite(PIN_Gas_Valve, HIGH); // Открываем бензоклапан
    notifyClients(getOutputStates());
  }

  if (zmpt_generator.getRmsVoltage() < 50 && start_reley_count > 0)
  {
    start_reley_count++;
    unsigned long currentMillis = millis(); // Записывается время начала процесса мигания
    // Петля для управления двойным миганием светодиода
    while (blinkCount < 6)
    { // Каждый цикл включения-выключения считается одним миганием, поэтому 4 цикла означают 2 мигания
      if (currentMillis - previousMillis >= ledBlinkInterval)
      {
        previousMillis = currentMillis;
        if (ledState == HIGH)
        { // Мигаем синим при старте генератора
          ledState = LOW;
          ledRGB.fill(0x0000FF); // Blue
        }
        else
        {
          ledState = HIGH;
          ledRGB.fill(0x000000); // Off
        }
        ledRGB.show();
        blinkCount++;
      }
      currentMillis = millis(); // Обновление currentMillis
    }
    blinkCount = 0; // Сброс количества миганий при следующем вызове

    digitalWrite(PIN_Start_Reley, HIGH); // Включаем реле старта
    notifyClients(getOutputStates());
    delay(500);
    digitalWrite(PIN_Start_Reley, LOW); // Выключаем реле старта
    notifyClients(getOutputStates());
    // delay(10000);
    if (start_reley_count >= 5)
      start_reley_count = 0;
    if (start_from_tg == 1)
      start_from_tg = 0;
    logEvent("Генератор запущен.");
  }
}

void stop_generator()
{
  digitalWrite(PIN_Start_Reley, HIGH);
  notifyClients(getOutputStates());
  delay(500);
  digitalWrite(PIN_Start_Reley, LOW);
  notifyClients(getOutputStates());
  digitalWrite(PIN_Gas_Valve, LOW);
  notifyClients(getOutputStates());

  // Сброс всех флагов и таймеров напряжения генератора
  voltageHighAlertSentGenerator = false;
  voltageLowAlertSentGenerator = false;
  voltageReturnedToNormalGenerator = true;
  voltageHighStartTimeGenerator = 0;
  voltageLowStartTimeGenerator = 0;

  logEvent("Генератор остановлен.");
}

/*void handleWebSocketMessage(void *arg, uint8_t *data, size_t len) {
  AwsFrameInfo *info = (AwsFrameInfo*)arg;
  if (info->final && info->index == 0 && info->len == len && info->opcode == WS_TEXT) {
    data[len] = 0;
    if (strcmp((char *)data, "states") == 0) {
      notifyClients(getOutputStates());
    }
    else
    {
      int gpio = atoi((char *)data);
      digitalWrite(gpio, !digitalRead(gpio));
      notifyClients(getOutputStates());
    }
    notifyClients(getOutputStates());
  }
}*/

void handleWebSocketMessage(void *arg, uint8_t *data, size_t len)
{
  AwsFrameInfo *info = (AwsFrameInfo *)arg;
  if (info->final && info->index == 0 && info->len == len && info->opcode == WS_TEXT)
  {
    data[len] = 0; // Завершаем строку
    String message = String((char *)data);

    if (message.equals("states"))
    {
      notifyClients(getOutputStates());
    }
    else if (message.equals("start_generator"))
    {
      start_generator();
      notifyClients(getOutputStates()); // Отправка нового состояния
    }
    else if (message.equals("stop_generator"))
    {
      stop_generator();
      notifyClients(getOutputStates()); // Отправка нового состояния
    }
    else
    {
      int gpio = atoi((char *)data);
      digitalWrite(gpio, !digitalRead(gpio)); // Переключение состояния
      notifyClients(getOutputStates());       // Отправка нового состояния
    }
  }
}

void onEvent(AsyncWebSocket *server, AsyncWebSocketClient *client, AwsEventType type, void *arg, uint8_t *data, size_t len)
{
  switch (type)
  {
  case WS_EVT_CONNECT:
    // Serial.printf("WebSocket client #%u connected from %s\n", client->id(), client->remoteIP().toString().c_str());
    client->text(getOutputStates()); // При подключении нового клиента отправляем текущие состояния
    break;
  case WS_EVT_DISCONNECT:
    // Serial.printf("WebSocket client #%u disconnected\n", client->id());
    break;
  case WS_EVT_DATA:
    handleWebSocketMessage(arg, data, len);
    break;
  case WS_EVT_PONG:
  case WS_EVT_ERROR:
    break;
  }
}

void initWebSocket()
{
  ws.onEvent(onEvent);
  server.addHandler(&ws);
}

void send_bme_status()
{
  if (!bmeController.begin(0x76, &Wire))
  {
    message += "\U00002757 Не найден датчик \U0001F321\U0001F4A7\U0001F94A у контроллера, проверьте подключение!\n\n";
  }
  else
  {
    message += "\U0001F4C9 Датчик у контроллера\n\U0001F321 температура: ";
    message += (int)bmeController.readTemperature(); // Type casting to int to remove decimal
    message += " °C\n";
    message += "\U0001F4A7 влажность: ";
    message += (int)bmeController.readHumidity(); // Type casting to int to remove decimal
    message += " %\n";
    message += "\U0001F94A давление: ";
    message += (int)(bmeController.readPressure() / 133.322); // Type casting to int to remove decimal
    message += " мм.рт.ст.\n\n";
  }
  if (!bmeGenerator.begin(0x77, &Wire))
  {
    message += "\U00002757 Не найден датчик \U0001F321\U0001F4A7\U0001F94A у генератора, проверьте подключение!\n\n";
    String currentTime = getDateTime();
    message += "\u23F0 Дата время: " + currentTime;
  }
  else
  {
    message += "\U0001F4C9 Датчик у генератора\n\U0001F321 температура: ";
    message += (int)bmeGenerator.readTemperature(); // Type casting to int to remove decimal
    message += " °C\n";
    message += "\U0001F4A7 влажность: ";
    message += (int)bmeGenerator.readHumidity(); // Type casting to int to remove decimal
    message += " %\n";
    message += "\U0001F94A давление: ";
    message += (int)(bmeGenerator.readPressure() / 133.322); // Type casting to int to remove decimal
    message += " мм.рт.ст.\n\n";
    String currentTime = getDateTime();
    message += "\u23F0 Дата время: " + currentTime;
  }
  bot.sendMessage(CHAT_ID, message, "");
}

/*void hard_restart() {
  bot.sendMessage(CHAT_ID, "REBOOT", "");
  ESP.restart();
}*/

// Задаем действия при получении новых сообщений
void handleNewMessages(int numNewMessages)
{
  for (int i = 0; i < numNewMessages; i++)
  {
    if (bot.messages[i].type == "callback_query")
    {

      bot.sendMessage(bot.messages[i].from_id, bot.messages[i].text, "");
    }
    else
    {
      // Идентификатор чата запроса
      String chat_id = String(bot.messages[i].chat_id);
      if (chat_id != CHAT_ID)
      {
        bot.sendMessage(chat_id, "Неизвестный пользователь Telegram группы", "");
        continue;
      }
      // Выводим полученное сообщение
      String text = bot.messages[i].text;
      String from_name = bot.messages[i].from_name;
      if (from_name == "")
        from_name = "Guest";
      if (text == "/gen_start" || text == "/gen_start@" + String(NAME_BOT))
      {
        if (start_from_tg == 0)
        { // Проверка на случай двоного запуска
          if (!alertSent_GEN_NOT_START)
          {
            bot.sendMessage(CHAT_ID, "\U000026A1 Штатный запуск генератора с бота от " + String(from_name), "");
            start_reley_count = 1;
            start_from_tg = 1;
            start_generator();
          }
          else
          {
            bot.sendMessage(CHAT_ID, "\U0001F525 Нештатный запуск генератора с бота от " + String(from_name), "");
            start_reley_count = 1;
            start_from_tg = 1;
            start_generator();
          }
        }
        else
        {
          bot.sendMessage(CHAT_ID, String(from_name) + " прости, ты не успел", "");
        }
      }
      if (text == "/gen_stop" || text == "/gen_stop@" + String(NAME_BOT))
      {
        if (zmpt_generator.getRmsVoltage() > 50)
        {
          bot.sendMessage(CHAT_ID, "\U0001F4A4 Глушим генератор для " + String(from_name), "");
          stop_generator();
        }
        else
        {
          bot.sendMessage(CHAT_ID, "\U0001F4A4 Генератор остановлен. " + String(from_name) + " извини.", "");
        }
        if (digitalRead(PIN_Gas_Valve) == HIGH)
        {
          digitalWrite(PIN_Gas_Valve, LOW);
        }
      }
      /*if (text == "/hardreset@NAME_BOT"){
        bot.sendMessage(CHAT_ID, "ПЕРЕЗАГРУЖАЮСЬ...", "");  //отправляем сообщение в чат
        //ESP.restart();                                      //перезагружаем плату
        hard_restart();
      }*/
      if (text == "/status" || text == "/status@" + String(NAME_BOT))
      {
        if (zmpt_network.getRmsVoltage() > 50)
        {
          message = String(from_name) + " вот смотри\n\n";
          message += "\U00002705 Напряжение с города: ";
          int network_volt = zmpt_network.getRmsVoltage();
          message += network_volt;
          message += " V\n";
          icon = 1;
        }
        else
        {
          message = String(from_name) + " вот смотри\n\n";
          message += "\U0000274C Напряжения с города нет\n";
          icon = 0;
        }
        if (zmpt_generator.getRmsVoltage() > 50)
        {
          message += "\U0001F7E2 Генератор запущен: ";
          int generator_volt = zmpt_generator.getRmsVoltage();
          message += generator_volt;
          if (digitalRead(PIN_Gas_Valve))
          {
            message += " V\n\n";
          }
          else
          {
            message += " V \U00002757 Бензоклапан закрыт \U00002757\n\n";
          }
        }
        else
        {
          if (icon == 0)
          {
            message += "\U0000274C Генератор остановлен\n\n";
          }
          else if (icon == 1)
          {
            message += "\U0001F4A4 Генератор остановлен\n\n";
          }
        }
        send_bme_status();
        /*if (stat_wifi) {
              localWIFI_IP = WiFi.localIP().toString();
              message += "WiFI IP: " + localWIFI_IP;
              } else {
                message +=  "WiFi отключен";
                }*/
      }
      if (text == "/start")
      {
        message = String(from_name) + " привет!\n\nДобро пожаловать в Telegram Bot контроллера генератора ELP LH45iE.\n\n";
        message += "Логика автоматической работы: Генератор запускается(4 попытки) спустя ";
        vd = NO_VOLTAGE_DURATION / 60000;
        message += vd;
        message += " мин. после отключения напряжения с города и останавливается спустя ";
        vd = YES_VOLTAGE_DURATION / 60000;
        message += vd;
        message += " мин. после влкючения напряжения города.\n\n";
        message += "Описание состояния RGB индикатора:\n";
        message += "Белый мигает - есть напряжение с города и подключен WiFi\n";
        message += "Розовый мигает - есть напряжение с города, но не подключен WiFi\n";
        message += "Синий горит - автостарт генератора\n";
        message += "Жёлто-красный мигает - пропало напряжение с города\n";
        message += "Зеленый-красный мигает - если бензо-клапан закрыт, но генератор запущен\n\n";
        message += "Для управления контроллером используйте следующий команды:\n";
        message += "/gen_start : запустить генератор\n";
        message += "/gen_stop : заглушить генератор\n";
        // message += "/hardreset : перезакрузить ESP32\n";
        message += "/status : получить состояние генератора, WiFi, получить температуру/влажность/давление с датчиков у контроллера и генратора\n";
        bot.sendMessage(CHAT_ID, message, "");
      }
    }
  }
}

// 6. Функция интеллектуального решения об отправке
bool shouldSendAlert(const VoltageAlert &alert, unsigned long currentMillis, unsigned long adaptiveDuration)
{
  if (alert.fluctuationCount < 3)
  {
    return false;
  }

  // Минимальная длительность аномалии перед отправкой
  if (currentMillis - alert.startTime < MIN_ANOMALY_DURATION)
  {
    return false;
  }

  // Критическое превышение - отправляем сразу только после минимальной длительности
  if (alert.maxVoltage > VOLTAGE_HIGH_THRESHOLD + 20.0)
  {
    return true;
  }

  // Длительная аномалия - отправляем после адаптивной задержки
  if (currentMillis - alert.startTime >= adaptiveDuration)
  {
    return true;
  }

  // Сильные колебания - отправляем раньше, но только после минимальной длительности
  if (alert.maxVoltage - alert.minVoltage > 15.0 && alert.fluctuationCount > 5)
  {
    return true;
  }

  return false;
}

// 7. Функция отправки комплексного умного сообщения
void sendSmartAlert(const VoltageAlert &alert, const String &source, unsigned long duration)
{
  String message;

  if (alert.maxVoltage > VOLTAGE_HIGH_THRESHOLD)
  {
    message = msgSettings.voltageHigh;
    message.replace("{source}", source);
  }
  else
  {
    message = msgSettings.voltageLow;
    message.replace("{source}", source);
  }

  message.replace("{minV}", String(alert.minVoltage, 1));
  message.replace("{maxV}", String(alert.maxVoltage, 1));
  message.replace("{fluct}", String(alert.fluctuationCount));

  // Добавляем продолжительность
  if (duration == 0)
  {
    message += ", Обнаружено мгновенно";
  }
  else
  {
    message += ", Длительность: " + String(duration) + "с";
  }

  bot.sendMessage(CHAT_ID, message + getFooter(), "");
  logEvent("Аномалия напряжения " + source + ": " + String(alert.maxVoltage, 1) + "V");
}

bool isStableVoltage(float currentVoltage, float previousVoltage)
{
  return abs(currentVoltage - previousVoltage) < 5.0; // Изменение менее 5V
}

void checkVoltage(float networkVoltage, float generatorVoltage)
{
  // Используем УЖЕ СУЩЕСТВУЮЩИЕ константы:
  // VOLTAGE_ANOMALY_DURATION и MIN_ALERT_INTERVAL
  // которые определены выше в коде
  static float prevNetworkVoltage = 0;
  static float prevGeneratorVoltage = 0;
  unsigned long currentMillis = millis();

  // 1. Проверка стабильности напряжения - ИГНОРИРУЕМ кратковременные скачки
  if (isStableVoltage(networkVoltage, prevNetworkVoltage) &&
      isStableVoltage(generatorVoltage, prevGeneratorVoltage))
  {
    // Напряжение стабильное - сбрасываем счетчики колебаний
    voltageFluctuationCount = 0;
  }
  else
  {
    // Напряжение нестабильное - увеличиваем счетчик
    lastFluctuationTime = currentMillis;
  }

  // 1. Динамический гистерезис
  if (currentMillis - lastFluctuationTime < 60000)
  {
    voltageFluctuationCount++;
  }
  else
  {
    voltageFluctuationCount = 0;
  }

  dynamicHysteresis = constrain(VOLTAGE_HYSTERESIS + (voltageFluctuationCount - 3) * 0.5, MIN_HYSTERESIS, MAX_HYSTERESIS);

  // 2. Адаптивная задержка (используем существующую константу)
  unsigned long adaptiveAnomalyDuration = VOLTAGE_ANOMALY_DURATION;
  if (voltageFluctuationCount > 2)
  {
    adaptiveAnomalyDuration = VOLTAGE_ANOMALY_DURATION * (1 + voltageFluctuationCount / 2);
  }

  // 2.1 Сохраняем текущие значения для следующей проверки
  prevNetworkVoltage = networkVoltage;
  prevGeneratorVoltage = generatorVoltage;

  // 3. Проверка высокого напряжения СЕТИ
  if (networkVoltage > VOLTAGE_HIGH_THRESHOLD)
  {
    if (!voltageAlertNetwork.active)
    {
      voltageAlertNetwork = {networkVoltage, networkVoltage, currentMillis, 1, true};
    }
    else
    {
      voltageAlertNetwork.minVoltage = min(voltageAlertNetwork.minVoltage, networkVoltage);
      voltageAlertNetwork.maxVoltage = max(voltageAlertNetwork.maxVoltage, networkVoltage);
      voltageAlertNetwork.fluctuationCount++;
      lastFluctuationTime = currentMillis;
    }

    // Используем существующую MIN_ALERT_INTERVAL
    if (shouldSendAlert(voltageAlertNetwork, currentMillis, adaptiveAnomalyDuration) && currentMillis - lastVoltageAlertTime >= MIN_ALERT_INTERVAL && !voltageAlertNetwork.messageSent)
    {
      unsigned long duration = (currentMillis - voltageAlertNetwork.startTime) / 1000;
      sendSmartAlert(voltageAlertNetwork, "сети", duration);
      lastVoltageAlertTime = currentMillis;
      voltageAlertNetwork.messageSent = true; // Пометить как отправленное
    }
  }
  else if (networkVoltage <= (VOLTAGE_HIGH_THRESHOLD - dynamicHysteresis) && voltageAlertNetwork.active)
  {
    // Восстановление с динамическим гистерезисом
    voltageAlertNetwork = {0, 0, 0, 0, false};
  }

  // 4. Проверка низкого напряжения СЕТИ
  if (networkVoltage > 50 && networkVoltage < VOLTAGE_LOW_THRESHOLD)
  {
    if (!voltageAlertNetwork.active)
    {
      voltageAlertNetwork = {networkVoltage, networkVoltage, currentMillis, 1, true};
    }
    else
    {
      voltageAlertNetwork.minVoltage = min(voltageAlertNetwork.minVoltage, networkVoltage);
      voltageAlertNetwork.maxVoltage = max(voltageAlertNetwork.maxVoltage, networkVoltage);
      voltageAlertNetwork.fluctuationCount++;
      lastFluctuationTime = currentMillis;
    }

    if (shouldSendAlert(voltageAlertNetwork, currentMillis, adaptiveAnomalyDuration) &&
        currentMillis - lastVoltageAlertTime >= MIN_ALERT_INTERVAL)
    {
      unsigned long duration = (currentMillis - voltageAlertNetwork.startTime) / 1000;
      sendSmartAlert(voltageAlertNetwork, "сети", duration);
      lastVoltageAlertTime = currentMillis;
      voltageAlertNetwork = {0, 0, 0, 0, false};
    }
  }
  else if (networkVoltage >= (VOLTAGE_LOW_THRESHOLD + dynamicHysteresis) && voltageAlertNetwork.active)
  {
    // Восстановление с динамическим гистерезисом
    voltageAlertNetwork = {0, 0, 0, 0, false};
  }

  // 5. Проверка высокого напряжения ГЕНЕРАТОРА
  if (generatorVoltage > 50 && generatorVoltage > VOLTAGE_HIGH_THRESHOLD)
  {
    if (!voltageAlertGenerator.active)
    {
      voltageAlertGenerator = {generatorVoltage, generatorVoltage, currentMillis, 1, true};
    }
    else
    {
      voltageAlertGenerator.minVoltage = min(voltageAlertGenerator.minVoltage, generatorVoltage);
      voltageAlertGenerator.maxVoltage = max(voltageAlertGenerator.maxVoltage, generatorVoltage);
      voltageAlertGenerator.fluctuationCount++;
      lastFluctuationTime = currentMillis;
    }

    if (shouldSendAlert(voltageAlertGenerator, currentMillis, adaptiveAnomalyDuration) &&
        currentMillis - lastVoltageAlertTime >= MIN_ALERT_INTERVAL)
    {
      unsigned long duration = (currentMillis - voltageAlertGenerator.startTime) / 1000;
      sendSmartAlert(voltageAlertGenerator, "генератора", duration);
      lastVoltageAlertTime = currentMillis;
      voltageAlertGenerator = {0, 0, 0, 0, false};
    }
  }
  else if (generatorVoltage <= (VOLTAGE_HIGH_THRESHOLD - dynamicHysteresis) && voltageAlertGenerator.active)
  {
    // Восстановление с динамическим гистерезисом
    voltageAlertGenerator = {0, 0, 0, 0, false};
  }

  // 6. Проверка низкого напряжения ГЕНЕРАТОРА
  if (generatorVoltage > 50 && generatorVoltage < VOLTAGE_LOW_THRESHOLD)
  {
    if (!voltageAlertGenerator.active)
    {
      voltageAlertGenerator = {generatorVoltage, generatorVoltage, currentMillis, 1, true};
    }
    else
    {
      voltageAlertGenerator.minVoltage = min(voltageAlertGenerator.minVoltage, generatorVoltage);
      voltageAlertGenerator.maxVoltage = max(voltageAlertGenerator.maxVoltage, generatorVoltage);
      voltageAlertGenerator.fluctuationCount++;
      lastFluctuationTime = currentMillis;
    }

    if (shouldSendAlert(voltageAlertGenerator, currentMillis, adaptiveAnomalyDuration) &&
        currentMillis - lastVoltageAlertTime >= MIN_ALERT_INTERVAL)
    {
      unsigned long duration = (currentMillis - voltageAlertGenerator.startTime) / 1000;
      sendSmartAlert(voltageAlertGenerator, "генератора", duration);
      lastVoltageAlertTime = currentMillis;
      voltageAlertGenerator = {0, 0, 0, 0, false};
    }
  }
  else if (generatorVoltage >= (VOLTAGE_LOW_THRESHOLD + dynamicHysteresis) && voltageAlertGenerator.active)
  {
    // Восстановление с динамическим гистерезисом
    voltageAlertGenerator = {0, 0, 0, 0, false};
  }
}

// 8. Функция получения текущего часа для обучения
int getCurrentHour()
{
  time_t now = time(nullptr);
  struct tm *timeinfo = localtime(&now);
  return timeinfo->tm_hour;
}

void setup()
{
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  configTime(gmtOffsetSec, daylightOffsetSec, ntpServer); // Настройка NTP
  // Serial.begin(115200);
  // Serial.println("Serial START");
  //  WiFi.mode(WIFI_STA);
  client.setCACert(TELEGRAM_CERTIFICATE_ROOT); // Добавляем корневой сертификат для api.telegram.org
  initSPIFFS();
  initWebSocket();
  loadMessageSettings();
  loadSystemState();
  // pinMode(PIN_Gas_Valve, OUTPUT);
  // pinMode(PIN_Start_Reley, OUTPUT);
  //  Назначаем GPIO выходами
  for (int i = 0; i < NUM_OUTPUTS; i++)
  {
    pinMode(outputGPIOs[i], OUTPUT);
  }

  zmpt_network.getRmsVoltage(SENSITIVITY);   // zmpt_network.init();
  zmpt_generator.getRmsVoltage(SENSITIVITY); // zmpt_generator.init();
  // Начальная страница
  server.on("/", HTTP_GET, [](AsyncWebServerRequest *request)
            { request->send(SPIFFS, "/index.html", "text/html", false); });
  server.on("/getCurrentStates", HTTP_GET, [](AsyncWebServerRequest *request)
            {
              String jsonResponse = getOutputStates();                             // Получаем JSON ответ
              request->send(200, "application/json; charset=utf-8", jsonResponse); // Отправляем JSON с кодом 200
            });
  server.on("/event.log", HTTP_GET, [](AsyncWebServerRequest *request)
            { request->send(SPIFFS, "/event.log", "text/plain; charset=utf-8"); });
  server.on("/settings", HTTP_GET, [](AsyncWebServerRequest *request)
            { request->send(SPIFFS, "/settings.html", "text/html"); });
  // Добавляем маршруты для настроек
  server.on("/get-settings", HTTP_GET, [](AsyncWebServerRequest *request)
            {
    JSONVar settings;
    settings["voltageHigh"] = msgSettings.voltageHigh;
    settings["voltageLow"] = msgSettings.voltageLow;
    settings["tempHigh"] = msgSettings.tempHigh;
    settings["tempNormal"] = msgSettings.tempNormal;
    settings["humidityHigh"] = msgSettings.humidityHigh;
    settings["humidityNormal"] = msgSettings.humidityNormal;
    settings["tempRising"] = msgSettings.tempRising;
    settings["tempFalling"] = msgSettings.tempFalling;
    settings["humidityRising"] = msgSettings.humidityRising;
    settings["humidityFalling"] = msgSettings.humidityFalling;
    settings["generatorStarted"] = msgSettings.generatorStarted;
    settings["generatorStopped"] = msgSettings.generatorStopped;
    settings["voltageLost"] = msgSettings.voltageLost;
    settings["voltageRestored"] = msgSettings.voltageRestored;
    settings["startMessage"] = msgSettings.startMessage;
    settings["location"] = msgSettings.location;
    settings["deviceName"] = msgSettings.deviceName;
    settings["customAddress"] = msgSettings.customAddress;
    
    String jsonString = JSON.stringify(settings);
    request->send(200, "application/json", jsonString); });

  server.on("/save-settings", HTTP_POST, [](AsyncWebServerRequest *request)
            {
    if (request->hasParam("voltageHigh", true)) msgSettings.voltageHigh = request->getParam("voltageHigh", true)->value();
    if (request->hasParam("voltageLow", true)) msgSettings.voltageLow = request->getParam("voltageLow", true)->value();
    if (request->hasParam("tempHigh", true)) msgSettings.tempHigh = request->getParam("tempHigh", true)->value();
    if (request->hasParam("tempNormal", true)) msgSettings.tempNormal = request->getParam("tempNormal", true)->value();
    if (request->hasParam("humidityHigh", true)) msgSettings.humidityHigh = request->getParam("humidityHigh", true)->value();
    if (request->hasParam("humidityNormal", true)) msgSettings.humidityNormal = request->getParam("humidityNormal", true)->value();
    if (request->hasParam("tempRising", true)) msgSettings.tempRising = request->getParam("tempRising", true)->value();
    if (request->hasParam("tempFalling", true)) msgSettings.tempFalling = request->getParam("tempFalling", true)->value();
    if (request->hasParam("humidityRising", true)) msgSettings.humidityRising = request->getParam("humidityRising", true)->value();
    if (request->hasParam("humidityFalling", true)) msgSettings.humidityFalling = request->getParam("humidityFalling", true)->value();
    if (request->hasParam("generatorStarted", true)) msgSettings.generatorStarted = request->getParam("generatorStarted", true)->value();
    if (request->hasParam("generatorStopped", true)) msgSettings.generatorStopped = request->getParam("generatorStopped", true)->value();
    if (request->hasParam("voltageLost", true)) msgSettings.voltageLost = request->getParam("voltageLost", true)->value();
    if (request->hasParam("voltageRestored", true)) msgSettings.voltageRestored = request->getParam("voltageRestored", true)->value();
    if (request->hasParam("startMessage", true)) msgSettings.startMessage = request->getParam("startMessage", true)->value();
    if (request->hasParam("location", true)) msgSettings.location = request->getParam("location", true)->value();
    if (request->hasParam("deviceName", true)) msgSettings.deviceName = request->getParam("deviceName", true)->value();
    if (request->hasParam("customAddress", true)) msgSettings.customAddress = request->getParam("customAddress", true)->value();

    saveMessageSettings();
    
    // Правильный JSON ответ
    String jsonResponse = "{\"status\":\"success\",\"message\":\"Settings saved successfully\"}";
    request->send(200, "application/json", jsonResponse); });

  server.serveStatic("/", SPIFFS, "/");
  server.begin(); // Запускаем сервер

  Wire.begin(8, 9);
  // Запускаем ElegantOTA
  ElegantOTA.begin(&server); // Запускаем ElegantOTA
  ledRGB.begin();
  ledRGB.setBrightness(128);
  ledRGB.setPixelColor(0, ledRGB.Color(0, 255, 255));
  ledRGB.show();
  delay(222);
  ledRGB.fill(0x000000); // Выключить RGB_LED
  ledRGB.show();
  lastConnectCheck = millis();
  start_time = millis();
  alertSent_NO_GOROD = false;
  alertSent_YES_GOROD = false;
  alertSent_GEN_STARTED = false;
  alertSent_GEN_NOT_START = false;
  if (WiFi.status() == 3)
  { // 3 это WL_CONNECTED (3) - устройство подключено к сети
    localWIFI_IP = WiFi.localIP().toString();

    // Используем кастомный адрес если указан, иначе IP
    String address = msgSettings.customAddress;
    if (address == "")
    {
      address = "http://" + localWIFI_IP;
    }

    // Обновляем стартовое сообщение с учетом кастомного адреса
    String startMsg = msgSettings.startMessage + "\n\nДоступен по адресу: " + address + getFooter();
    bot.sendMessage(CHAT_ID, startMsg, "");
    logEvent("Контроллер запущен. IP: " + localWIFI_IP);
  }

  if (zmpt_network.getRmsVoltage() > 50)
  {
    message += "\U00002705 Напряжение с города: ";
    int network_volt = zmpt_network.getRmsVoltage();
    message += network_volt;
    message += " V\n";
    icon = 1;
  }
  else
  {
    message += "\U0000274C Напряжения с города нет\n";
    icon = 0;
  }
  if (zmpt_generator.getRmsVoltage() > 50)
  {
    message += "\U0001F7E2 Генератор запущен: ";
    int generator_volt = zmpt_generator.getRmsVoltage();
    message += generator_volt;
    if (digitalRead(PIN_Gas_Valve))
    {
      message += " V\n\n";
    }
    else
    {
      message += " V \U00002757 Бензоклапан закрыт \U00002757\n\n";
    }
  }
  else
  {
    if (icon == 0)
    {
      message += "\U0000274C Генератор остановлен\n\n";
    }
    else if (icon == 1)
    {
      message += "\U0001F4A4 Генератор остановлен\n\n";
    }
  }
  send_bme_status();
}

void loop()
{
  static float networkVoltage = 0;
  static float generatorVoltage = 0;
  static unsigned long lastVoltageRead = 0;
  static unsigned long lastSensorCheck = 0;
  unsigned long currentMillis = millis();
  // server.handleClient();
  ElegantOTA.loop();
  ws.cleanupClients();
  if (currentMillis - lastSensorCheck >= 30000)
  {
    checkSensors();
    lastSensorCheck = currentMillis;
  }

  // Чтение значений не чаще 1 раза в 100мс
  if (currentMillis - lastVoltageRead >= 100)
  {
    networkVoltage = zmpt_network.getRmsVoltage();
    generatorVoltage = zmpt_generator.getRmsVoltage();
    checkVoltage(networkVoltage, generatorVoltage);
    lastVoltageRead = currentMillis;
  }

  if (currentMillis - lastConnectCheck > 300000 || currentMillis - start_time < 120000)
  { // Проверять подключен ли WiFi 2 минуты при старте и каждые 5 минут далее
    if (WiFi.status() != WL_CONNECTED)
    {
      stat_wifi = false;
      WiFi.begin(WIFI_SSID, WIFI_PASS);
      delay(3000);
      localWIFI_IP = WiFi.localIP().toString();
      message = "\U0001F501 WiFi завис, но переподключился на " + localWIFI_IP;
      bot.sendMessage(CHAT_ID, message, "");
    }
    else
    {
      stat_wifi = true;
    }
    lastConnectCheck = currentMillis; // Update last check time
  }

  if (PIN_Start_Button.click())
  {
    if (generatorVoltage < 50)
    {
      bot.sendMessage(CHAT_ID, "\U0001FAF8 Нажата кнопка на контролере генератора.\n\U000026A1 Запускаем генератор!", "");
      start_generator();
    }
    else
    {
      bot.sendMessage(CHAT_ID, "\U0001FAF8 Нажата кнопка на контролере генератора.\n\U000026A1 Но генератор уже запущен, аккуратнее", "");
    }
  }
  /*if (PIN_Start_Button.isHeld()) {
    if (generatorVoltage > 50) {
      bot.sendMessage(CHAT_ID, "\U0001FAF8 Удерживалась кнопка на контролере генератора.\n\U000026A1 Глушим генератор!", "");
      stop_generator();
    } else {
        bot.sendMessage(CHAT_ID, "\U0001FAF8 Удерживалась кнопка на контролере генератора.\n\U000026A1 Но генератор уже заглушен, аккуратнее", "");
      }
  }*/

  // Запуск генеатора
  if (networkVoltage < 50)
  {
    if (generatorVoltage < 50)
    {
      if (start_reley_count > 0)
      {
        if (isLedOn)
        {
          // Если красный RGB_LED горит в течение 1 секунды
          if (currentMillis - ledOnStartTime >= 500)
          {
            ledRGB.fill(0xFFFF00); // Выключаем жёлтый
            ledRGB.show();
            ledOffStartTime = currentMillis; // Запишим время, когда RGB_LED был выключен
            isLedOn = false;
          }
        }
        else
        {
          // Если жёлтый горит в течение 1 секунды, включаем красный RGB_LED
          if (currentMillis - ledOffStartTime >= 500)
          {
            ledRGB.fill(0x00FF00);
            ledRGB.show();
            ledOnStartTime = currentMillis; // Запишим время, когда RGB_LED был включен
            isLedOn = true;
          }
        }
        if (!voltageDropDetected)
        {
          voltageDropTime = currentMillis; // Запоминаем время отключения напряжения
          voltageDropDetected = true;      // Устанавливаем флаг, что отключение обнаружено
        }

        // Проверяем, прошло ли 3 секунды с момента отключения
        if (voltageDropDetected && currentMillis - voltageDropTime >= 1000 && !alertSent_NO_GOROD)
        {
          String voltMsg = msgSettings.voltageLost;
          voltMsg.replace("{min}", String(NO_VOLTAGE_DURATION / 60000));
          bot.sendMessage(CHAT_ID, voltMsg + getFooter(), "");
          alertSent_NO_GOROD = true; // Установим флаг, указывающий на то, что сообщение уже отправлено
          logEvent("Пропало напряжение с города");
          saveSystemState();
        }

        if (noVoltageStartTime == 0)
        {
          noVoltageStartTime = currentMillis; // Start tracking the time
        }
        else
        {
          unsigned long currentTime_noVoltage = currentMillis;
          if (currentTime_noVoltage - noVoltageStartTime >= NO_VOLTAGE_DURATION)
          {
            if (start_reley_count == 7)
            {
              bot.sendMessage(CHAT_ID, "\U0001F6A8 Заглох генератор. Пробуем запустить.\nПопытка запуска генератора № 1", "");
              start_reley_count = 1;
              alertSent_GEN_STARTED = false;
              start_generator();
            }
            else
            {
              if (alertSent_NO_NET == 0)
              {
                alertSent_NO_NET = 1;
                message = "\U000026A1 Прошло ";
                vd = NO_VOLTAGE_DURATION / 60000;
                message += vd;
                message += " мин. Напряжение с города не появилось.\n";
                bot.sendMessage(CHAT_ID, message, "");
              }
              // Реализация 10-секундного интервала для попытки запуска генератора
              static unsigned long lastSentAttemptTime = 0; // Время последней отправки сообщения
              if (currentMillis - lastSentAttemptTime >= 10000)
              { // Если прошло 10 секунд
                message = "Попытка запуска генератора № ";
                message += start_reley_count;
                bot.sendMessage(CHAT_ID, message, "");
                start_generator();
                lastSentAttemptTime = currentMillis; // Обновляем время последней отправки сообщения
              }
            }
          }
        }
      }
      else
      {
        // Генератор НЕ запустился 4 раза, включаем мигаюший красный
        if (!alertSent_GEN_NOT_START)
        {
          vd = NO_VOLTAGE_DURATION / 60000;
          message = "\U0001F6A8 \U0001F6A8 \U0001F6A8 Генератор НЕ запустился 4 раза!\n\n";
          message += "\U0001F3C3 Надо срочно выезжать на узел!!\n\n";
          message += String(TG_NAMES) + " либо попробовать запустить генератор командой /gen_start\n\n";
          bot.sendMessage(CHAT_ID, message, "");
          alertSent_GEN_NOT_START = true; // Установим флаг, указывающий на то, что сообщение уже отправлено
        }
        ledRGB.fill(0x00FF00);
        ledRGB.show();
        delay(300);
        if (stat_wifi)
        {
          ledRGB.fill(0x000000);
        }
        else
        {
          ledRGB.fill(0xFFFFFF);
        }
        ledRGB.show();
      }
    }
    else
    { // Генератор запущен, включаем зелёный
      if (digitalRead(PIN_Gas_Valve) == HIGH)
      {
        ledRGB.fill(0xFF0000);
        ledRGB.show();
        if (!alertSent_GEN_STARTED)
        {
          start_reley_count = 7;
          String genMsg = msgSettings.generatorStarted; // ЗДЕСЬ меняем сообщение о запуске
          genMsg.replace("{volt}", String(generatorVoltage));
          bot.sendMessage(CHAT_ID, genMsg + getFooter(), "");
          alertSent_GEN_STARTED = true;
          logEvent("Генератор запущен");
          saveSystemState();
        }
      }
      else
      {
        // если бензо-клапан закрыт, но генератор запущен, то моргает зеленый-красный
        if (isLedOn)
        {
          // Если красный RGB_LED горит в течение 1 секунды
          if (currentMillis - ledOnStartTime >= 300)
          {
            ledRGB.fill(0xFF0000); // Выключаем RGB_LED
            ledRGB.show();
            ledOffStartTime = currentMillis; // Запишим время, когда RGB_LED был выключен
            isLedOn = false;
          }
        }
        else
        {
          // Если зелёеый горит в течение 1 секунды, включаем красный RGB_LED
          if (currentMillis - ledOffStartTime >= 300)
          {
            ledRGB.fill(0x00FF00);
            ledRGB.show();
            ledOnStartTime = currentMillis; // Запишим время, когда RGB_LED был включен
            isLedOn = true;
          }
        }
        if (!alertSent_GEN_STARTED)
        {
          start_reley_count = 7;
          String genMsg = msgSettings.generatorStarted; // И ЗДЕСЬ тоже
          genMsg.replace("{volt}", String(generatorVoltage));
          genMsg += " \U00002757 Бензоклапан закрыт \U00002757";
          bot.sendMessage(CHAT_ID, genMsg + getFooter(), "");
          alertSent_GEN_STARTED = true;
          logEvent("Генератор запущен. Бензоклапан закрыт!");
          saveSystemState();
        }
      }
    }
  }

  if (networkVoltage > 50)
  {
    if (generatorVoltage < 50)
    {
      if (digitalRead(PIN_Gas_Valve) == HIGH)
      {
        if (alertSent_NO_GOROD)
        {
          if (msg_network_on != 1)
          {
            if (networkVoltage > 50)
            {
              bot.sendMessage(CHAT_ID, msgSettings.voltageRestored + getFooter(), "");
              logEvent("Появилось напряжение с города");
            }
          }
          msg_network_on = 0;
          voltageDropDetected = false;
          alertSent_NO_GOROD = false; // Сбрасываем флаг отправки сообщения
          alertSent_YES_GOROD = false;
          alertSent_GEN_STARTED = false;
          alertSent_GEN_NOT_START = false;
          noVoltageStartTime = 0; // Сбрасываем для следующей проверки напряжения
          yesVoltageStartTime = 0;
          start_reley_count = 1;
          alertSent_NO_NET = 0;
          digitalWrite(PIN_Gas_Valve, LOW);
        }
      }
      else
      {
        if (start_reley_count > 1)
        {
          start_reley_count = 1;
          msg_network_on = 1;
          String stopMsg = msgSettings.generatorStopped + "\n\n\U00002705 Напряжение с города: " + String(networkVoltage) + " V\n\n";
          bot.sendMessage(CHAT_ID, stopMsg + getFooter(), "");
          send_bme_status();
          logEvent("Генератор остановлен");
          saveSystemState();
        }
        if (alertSent_NO_GOROD)
        {
          if (msg_network_on != 1)
          {
            bot.sendMessage(CHAT_ID, msgSettings.voltageRestored + getFooter(), "");
            logEvent("Появилось напряжение с города");
          }
          msg_network_on = 0;
          alertSent_NO_GOROD = false; // Сбрасываем флаг отправки сообщения
          alertSent_YES_GOROD = false;
          alertSent_GEN_STARTED = false;
          alertSent_GEN_NOT_START = false;
          noVoltageStartTime = 0; // Сбрасываем для следующей проверки напряжения
          yesVoltageStartTime = 0;
          start_reley_count = 1;
          alertSent_NO_NET = 0;
        }
        // Всё ОК, мигаем белым
        if (start_reley_count != 0)
        {
          if (isLedOn)
          {
            // Если белый RGB_LED горит в течение 2 секунд
            if (currentMillis - ledOnStartTime >= 2000)
            {
              ledRGB.fill(0x000000); // Выключаем RGB_LED
              ledRGB.show();
              ledOffStartTime = currentMillis; // Запишим время, когда RGB_LED был выключен
              isLedOn = false;
            }
          }
          else
          {
            // Если индикатор не горит в течение 2 секунд, включаем белый RGB_LED. Если WiFi отключен, то розовый
            if (currentMillis - ledOffStartTime >= 2000)
            {
              if (stat_wifi)
              {
                ledRGB.fill(0xFFFFFF);
              }
              else
              {
                ledRGB.fill(0x00FFFF);
              }
              ledRGB.show();
              ledOnStartTime = currentMillis; // Запишим время, когда RGB_LED был включен
              isLedOn = true;
            }
          }
        }
      }
    }
    else
    { // Остановка генератора если есть питание из сети и реле клапана бензина включено, иначе мигает белый
      if (!alertSent_NO_GOROD)
      { // Если еще не уведомляли о появлении напряжения
        bot.sendMessage(CHAT_ID, msgSettings.voltageRestored + getFooter(), "");
        logEvent("Появилось напряжение с города");
        alertSent_NO_GOROD = true; // Чтобы не дублировать
        saveSystemState();
      }
      if (digitalRead(PIN_Gas_Valve) == HIGH)
      {
        if (!alertSent_YES_GOROD)
        {
          vd = YES_VOLTAGE_DURATION / 60000;
          message = "\U00002705 Появилось напряжение с города, через ";
          message += vd;
          message += " мин. глушим генератор";
          bot.sendMessage(CHAT_ID, message, "");
          alertSent_YES_GOROD = true; // Установим флаг, указывающий на то, что сообщение уже отправлено
        }
        if (yesVoltageStartTime == 0)
        {
          yesVoltageStartTime = currentMillis; // Start tracking the time
        }
        else
        {
          if (currentMillis - yesVoltageStartTime >= YES_VOLTAGE_DURATION)
          {
            stop_generator();
            // ledOnStartTime = currentMillis;  // Initialize ledOnStartTime at the moment stop_generator() is called
            isLedOn = true;
            vd = YES_VOLTAGE_DURATION / 60000;
            message = "\U0001F4A4 Прошло ";
            message += vd;
            message += " мин. после появления напряжения с города. Глушим генератор!";
            bot.sendMessage(CHAT_ID, message, "");
          }
        }
      }
    }
  }

  if (currentMillis > lastTimeBotRan + botRequestDelay)
  {
    int numNewMessages = bot.getUpdates(bot.last_message_received + 1);
    while (numNewMessages)
    {
      handleNewMessages(numNewMessages);
      numNewMessages = bot.getUpdates(bot.last_message_received + 1);
    }
    lastTimeBotRan = currentMillis;
  }
}