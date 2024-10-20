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
#include <Secrets.h>  // Переименовать lib/Secrets/Secrets.h_ в lib/Secrets/Secrets.h

#define WIFI_SSID SEC_WIFI_SSID
#define WIFI_PASS SEC_WIFI_PASS
#define BOT_TOKEN SEC_BOT_TOKEN
#define CHAT_ID SEC_CHAT_ID
#define NAME_BOT SEC_NAME_BOT
#define TG_NAMES SEC_TG_NAMES

Adafruit_BME280 bmeController;
Adafruit_BME280 bmeGenerator;
Adafruit_NeoPixel ledRGB(1, 48, NEO_RGB + NEO_KHZ800);
WiFiClientSecure client;
UniversalTelegramBot bot(BOT_TOKEN, client);

int botRequestDelay = 1000;
unsigned long lastTimeBotRan;
 
const int PIN_Voltage_Network = 1;   // Pin для датчика напряжения ZMPT101B сети
const int PIN_Voltage_Generator = 2; // Pin для датчика напряжения ZMPT101B генератора
const int PIN_Gas_Valve = 35;        // Контакт реле для бенинового клапан
const int PIN_Start_Reley = 36;      // Контакт реле для кнопки START
const int PIN_Reserv = 38;
button PIN_Start_Button(37);

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
//const unsigned long NO_VOLTAGE_DURATION = 5000; // 1 minutes in milliseconds
unsigned long yesVoltageStartTime = 0;
const unsigned long YES_VOLTAGE_DURATION = 60000; // Стоп генератора через 1 мин после появления города
//const unsigned long YES_VOLTAGE_DURATION = 5000; // 1 minutes in milliseconds
unsigned long lastConnectCheck = 0; // время последней проверки
bool stat_wifi = false;
String localWIFI_IP;
String message;
int vd;
static bool alertSent_NO_GOROD; // Добавим переменную для отслеживания выполнения условия отправки разового сообщения после падения города
static bool alertSent_YES_GOROD; // Добавим переменную для отслеживания выполнения условия отправки разового сообщения после появления города
static bool alertSent_GEN_STARTED; // Добавим переменную для отслеживания выполнения условия отправки разового сообщения после не запуска генератора
static bool alertSent_GEN_NOT_START; // Добавим переменную для отслеживания выполнения условия отправки разового сообщения после не запуска генератора 
int alertSent_NO_NET = 0;
int icon;
int msg_network_on;
int hand_start = 0;

// Константы пороговых значений handle 
const int TEMPERATURE_THRESHOLD = 30; // Порог для температуры в °C
const int HUMIDITY_THRESHOLD = 50;     // Порог для влажности в %
const int TEMPERATURE_CHANGE_THRESHOLD = 10; // Порог изменения температуры для сообщений
const int HUMIDITY_CHANGE_THRESHOLD = 15; // Порог изменения влажности для сообщений

// Флаги для отслеживания состояния сообщений
bool messageSentTemperature = false; // Флаг для высоких температур у контроллера и генератора
bool recoveryMessageSentTemperature = false; // Флаг для восстановления нормальной температуры у контроллера и генератора
bool messageSentHumidity = false; // Флаг для высоких влажностей у контроллера и генератора
bool recoveryMessageSentHumidity = false; // Флаг для восстановления нормальной влажности у контроллера и генератора

// Предыдущие значения температуры и влажности
int previousTemperature = 0; // Предыдущее значение температуры у контроллера и генератора
int previousHumidity = 0; // Предыдущее значение влажности у контроллера и генератора
int highTemperatureValue = 0; // Значение, отправленное в сообщении о высокой температуре
int highHumidityValue = 0; // Значение, отправленное в сообщении о высокой влажности
bool firstRiseFallTemperatureMessage = true; // Флаг для первой отправки сообщения о росте/снижении температуры
bool firstRiseFallHumidityMessage = true; // Флаг для первой отправки сообщения о росте/снижении влажности
// checkSensors
unsigned long previousMillisCheckSensors = 0; // Время последнего вызова checkSensors
const unsigned long interval = 300000; // Интервал 5 минут в миллисекундах
unsigned long previousMillisCheck = 0; // Время последней проверки
const unsigned long checkInterval = 60000; // 1 минута в миллисекундах
int checkCount = 0; // Счетчик проверок
std::vector<int> temperatureControllerReadings; // Вектор для хранения показаний температуры контроллера
std::vector<int> humidityControllerReadings; // Вектор для хранения показаний влажности контроллера
std::vector<int> temperatureGeneratorReadings; // Вектор для хранения показаний температуры генератора
std::vector<int> humidityGeneratorReadings; // Вектор для хранения показаний влажности генератора

#define NUM_OUTPUTS 3 // Указываем количество выходов для WEB
// Присваиваем каждому GPIO свой выход
int outputGPIOs[NUM_OUTPUTS] = {PIN_Gas_Valve, PIN_Start_Reley, PIN_Reserv};

#define SENSITIVITY 470.0f

// bool flag = false; //button
// uint32_t btnTimer = 0; //button

ZMPT101B zmpt_network(PIN_Voltage_Network, 50.0);
ZMPT101B zmpt_generator(PIN_Voltage_Generator, 50.0);

AsyncWebServer server(80);
AsyncWebSocket ws("/ws");
JSONVar myArray;

void handleController(int temperature, int humidity) {
    // Проверка температуры
    if (temperature > TEMPERATURE_THRESHOLD && !messageSentTemperature) {
        bot.sendMessage(CHAT_ID, "\U0001F525 Температура у контроллера высокая: " + String(temperature) + " °C", ""); 
        highTemperatureValue = temperature; // Сохраняем значение
        messageSentTemperature = true; 
    }
    
    if (temperature <= TEMPERATURE_THRESHOLD) {
        if (!recoveryMessageSentTemperature) {
            bot.sendMessage(CHAT_ID, "\U00002705 Температура у контроллера вернулась в норму: " + String(temperature) + " °C", ""); 
            recoveryMessageSentTemperature = true; 
            messageSentTemperature = false; // Сбрасываем флаг для высокой температуры
        }
    }

    // Проверка влажности
    if (humidity > HUMIDITY_THRESHOLD && !messageSentHumidity) {
        bot.sendMessage(CHAT_ID, "\U0001F6A8 Влажность у контроллера высокая: " + String(humidity) + "%", ""); 
        highHumidityValue = humidity; // Сохраняем значение
        messageSentHumidity = true; 
    }
    
    if (humidity <= HUMIDITY_THRESHOLD) {
        if (!recoveryMessageSentHumidity) {
            bot.sendMessage(CHAT_ID, "\U00002705 Влажность у контроллера вернулась в норму: " + String(humidity) + "%", ""); 
            recoveryMessageSentHumidity = true; 
            messageSentHumidity = false; // Сбрасываем флаг для высокой влажности
        }
    }

    // Проверка на изменения в пределах порогов и отправка сообщения о росте или снижении температуры
    if (firstRiseFallTemperatureMessage) {
        if (previousTemperature - temperature >= TEMPERATURE_CHANGE_THRESHOLD && previousTemperature != highTemperatureValue) {
            bot.sendMessage(CHAT_ID, "\U0001F53D Температура у контроллера немного снизилась: " + String(temperature) + " °C", ""); 
            firstRiseFallTemperatureMessage = false; // Сбрасываем флаг после первой отправки
        }
        
        if (temperature - previousTemperature >= TEMPERATURE_CHANGE_THRESHOLD && previousTemperature != highTemperatureValue) {
            bot.sendMessage(CHAT_ID, "\U0001F53A Температура у контроллера продолжает расти: " + String(temperature) + " °C", ""); 
            firstRiseFallTemperatureMessage = false; // Сбрасываем флаг после первой отправки
        }
    } else {
        // Вторичные проверки роста и снижения без проверки на highTemperatureValue
        if (previousTemperature - temperature >= TEMPERATURE_CHANGE_THRESHOLD) {
            bot.sendMessage(CHAT_ID, "\U0001F53D Температура у контроллера немного снизилась: " + String(temperature) + " °C", ""); 
        }
        
        if (temperature - previousTemperature >= TEMPERATURE_CHANGE_THRESHOLD) {
            bot.sendMessage(CHAT_ID, "\U0001F53A Температура у контроллера продолжает расти: " + String(temperature) + " °C", ""); 
        }
    }

    // Проверка на изменения в пределах порогов и отправка сообщения о росте или снижении влажности
    if (firstRiseFallHumidityMessage) {
        if (previousHumidity - humidity >= HUMIDITY_CHANGE_THRESHOLD && previousHumidity != highHumidityValue) {
            bot.sendMessage(CHAT_ID, "\U0001F53D Влажность у контроллера немного снизилась: " + String(humidity) + "%", ""); 
            firstRiseFallHumidityMessage = false; // Сбрасываем флаг после первой отправки
        }

        if (humidity - previousHumidity >= HUMIDITY_CHANGE_THRESHOLD && previousHumidity != highHumidityValue) {
            bot.sendMessage(CHAT_ID, "\U0001F53A Влажность у контроллера продолжает расти: " + String(humidity) + "%", ""); 
            firstRiseFallHumidityMessage = false; // Сбрасываем флаг после первой отправки
        }
    } else {
        // Вторичные проверки роста и снижения без проверки на highHumidityValue
        if (previousHumidity - humidity >= HUMIDITY_CHANGE_THRESHOLD) {
            bot.sendMessage(CHAT_ID, "\U0001F53D Влажность у контроллера немного снизилась: " + String(humidity) + "%", ""); 
        }

        if (humidity - previousHumidity >= HUMIDITY_CHANGE_THRESHOLD) {
            bot.sendMessage(CHAT_ID, "\U0001F53A Влажность у контроллера продолжает расти: " + String(humidity) + "%", ""); 
        }
    }

    // Обновление предыдущих значений
    previousTemperature = temperature; 
    previousHumidity = humidity; 
}

void handleGenerator(int temperature, int humidity) {
    // Проверка температуры
    if (temperature > TEMPERATURE_THRESHOLD && !messageSentTemperature) {
        bot.sendMessage(CHAT_ID, "\U0001F525 Температура у генератора высокая: " + String(temperature) + " °C", ""); 
        highTemperatureValue = temperature; // Сохраняем значение
        messageSentTemperature = true; 
    }

    if (temperature <= TEMPERATURE_THRESHOLD) {
        if (!recoveryMessageSentTemperature) {
            bot.sendMessage(CHAT_ID, "\U00002705 Температура у генератора вернулась в норму: " + String(temperature) + " °C", ""); 
            recoveryMessageSentTemperature = true; 
            messageSentTemperature = false; // Сбрасываем флаг для высокой температуры
        }
    }
    
    // Проверка влажности
    if (humidity > HUMIDITY_THRESHOLD && !messageSentHumidity) {
        bot.sendMessage(CHAT_ID, "\U0001F6A8 Влажность у генератора высокая: " + String(humidity) + "%", ""); 
        highHumidityValue = humidity; // Сохраняем значение
        messageSentHumidity = true; 
    }

    if (humidity <= HUMIDITY_THRESHOLD) {
        if (!recoveryMessageSentHumidity) {
            bot.sendMessage(CHAT_ID, "\U00002705 Влажность у генератора вернулась в норму: " + String(humidity) + "%", ""); 
            recoveryMessageSentHumidity = true; 
            messageSentHumidity = false; // Сбрасываем флаг для высокой влажности
        }
    }

    // Проверка на изменения в пределах порогов и отправка сообщения о росте или снижении температуры
    if (firstRiseFallTemperatureMessage) {
        if (previousTemperature - temperature >= TEMPERATURE_CHANGE_THRESHOLD && previousTemperature != highTemperatureValue) {
            bot.sendMessage(CHAT_ID, "\U0001F53D Температура у генератора немного снизилась: " + String(temperature) + " °C", ""); 
            firstRiseFallTemperatureMessage = false; // Сбрасываем флаг после первой отправки
        }
        
        if (temperature - previousTemperature >= TEMPERATURE_CHANGE_THRESHOLD && previousTemperature != highTemperatureValue) {
            bot.sendMessage(CHAT_ID, "\U0001F53A Температура у генератора продолжает расти: " + String(temperature) + " °C", ""); 
            firstRiseFallTemperatureMessage = false; // Сбрасываем флаг после первой отправки
        }
    } else {
        // Вторичные проверки роста и снижения без проверки на highTemperatureValue
        if (previousTemperature - temperature >= TEMPERATURE_CHANGE_THRESHOLD) {
            bot.sendMessage(CHAT_ID, "\U0001F53D Температура у генератора немного снизилась: " + String(temperature) + " °C", ""); 
        }
        
        if (temperature - previousTemperature >= TEMPERATURE_CHANGE_THRESHOLD) {
            bot.sendMessage(CHAT_ID, "\U0001F53A Температура у генератора продолжает расти: " + String(temperature) + " °C", ""); 
        }
    }

    // Проверка на изменения в пределах порогов и отправка сообщения о росте или снижении влажности
    if (firstRiseFallHumidityMessage) {
        if (previousHumidity - humidity >= HUMIDITY_CHANGE_THRESHOLD && previousHumidity != highHumidityValue) {
            bot.sendMessage(CHAT_ID, "\U0001F53D Влажность у генератора немного снизилась: " + String(humidity) + "%", ""); 
            firstRiseFallHumidityMessage = false; // Сбрасываем флаг после первой отправки
        }

        if (humidity - previousHumidity >= HUMIDITY_CHANGE_THRESHOLD && previousHumidity != highHumidityValue) {
            bot.sendMessage(CHAT_ID, "\U0001F53A Влажность у генератора продолжает расти: " + String(humidity) + "%", ""); 
            firstRiseFallHumidityMessage = false; // Сбрасываем флаг после первой отправки
        }
    } else {
        // Вторичные проверки роста и снижения без проверки на highHumidityValue
        if (previousHumidity - humidity >= HUMIDITY_CHANGE_THRESHOLD) {
            bot.sendMessage(CHAT_ID, "\U0001F53D Влажность у генератора немного снизилась: " + String(humidity) + "%", ""); 
        }

        if (humidity - previousHumidity >= HUMIDITY_CHANGE_THRESHOLD) {
            bot.sendMessage(CHAT_ID, "\U0001F53A Влажность у генератора продолжает расти: " + String(humidity) + "%", ""); 
        }
    }

    // Обновление предыдущих значений
    previousTemperature = temperature; 
    previousHumidity = humidity; 
}

int calculateMedian(std::vector<int>& readings) {
    if (readings.empty()) return 0;
    std::sort(readings.begin(), readings.end());
    int middle = readings.size() / 2;
    if (readings.size() % 2 == 0) {
        return (readings[middle - 1] + readings[middle]) / 2; 
    } else {
        return readings[middle]; 
    }
}

int calculateMode(std::vector<int>& readings) {
    std::map<int, int> frequencyMap;
    for (int value : readings) {
        frequencyMap[value]++;
    }
    int mode = readings[0];
    int maxCount = 0;
    for (const auto& entry : frequencyMap) {
        if (entry.second > maxCount) {
            maxCount = entry.second;
            mode = entry.first;
        }
    }
    return mode;
}

void checkSensors() {
    unsigned long currentMillis = millis(); 
    if (currentMillis - previousMillisCheckSensors >= interval) {
        previousMillisCheckSensors = currentMillis; 
        checkCount = 0; 
        temperatureControllerReadings.clear(); 
        humidityControllerReadings.clear();
        temperatureGeneratorReadings.clear();
        humidityGeneratorReadings.clear();
    }
    if (currentMillis - previousMillisCheck >= checkInterval && checkCount < 10) {
        previousMillisCheck = currentMillis; 
        int temperatureController = (int)bmeController.readTemperature();
        int humidityController = (int)bmeController.readHumidity();
        int temperatureGenerator = (int)bmeGenerator.readTemperature();
        int humidityGenerator = (int)bmeGenerator.readHumidity();
        temperatureControllerReadings.push_back(temperatureController);
        humidityControllerReadings.push_back(humidityController);
        temperatureGeneratorReadings.push_back(temperatureGenerator);
        humidityGeneratorReadings.push_back(humidityGenerator);
        if (checkCount == 9) {
            int medianControllerTemperature = calculateMedian(temperatureControllerReadings);
            int medianControllerHumidity = calculateMedian(humidityControllerReadings);
            int modeControllerTemperature = calculateMode(temperatureControllerReadings);
            int modeControllerHumidity = calculateMode(humidityControllerReadings);
            int medianGeneratorTemperature = calculateMedian(temperatureGeneratorReadings);
            int medianGeneratorHumidity = calculateMedian(humidityGeneratorReadings);
            int modeGeneratorTemperature = calculateMode(temperatureGeneratorReadings);
            int modeGeneratorHumidity = calculateMode(humidityGeneratorReadings);

            int averageControllerTemperature = (medianControllerTemperature + modeControllerTemperature) / 2;
            int averageControllerHumidity = (medianControllerHumidity + modeControllerHumidity) / 2;
            handleController(averageControllerTemperature, averageControllerHumidity);
            int averageGeneratorTemperature = (medianGeneratorTemperature + modeGeneratorTemperature) / 2;
            int averageGeneratorHumidity = (medianGeneratorHumidity + modeGeneratorHumidity) / 2;
            handleGenerator(averageGeneratorTemperature, averageGeneratorHumidity);
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
    bot.sendMessage(CHAT_ID, message, "");
  }
}

String getOutputStates() {
  myArray["temperature1"] = String(bmeController.readTemperature());
  myArray["humidity1"] = String(bmeController.readHumidity());
  myArray["pressure1"] = String(bmeController.readPressure()/133.322);
  myArray["temperature2"] = String(bmeGenerator.readTemperature());
  myArray["humidity2"] = String(bmeGenerator.readHumidity());
  myArray["pressure2"] = String(bmeGenerator.readPressure()/133.322);
  for (int i = 0; i < NUM_OUTPUTS; i++)
  {
    myArray["gpios"][i]["output"] = String(outputGPIOs[i]);
    myArray["gpios"][i]["state"] = String(digitalRead(outputGPIOs[i]));
  }
  String jsonString = JSON.stringify(myArray);
  return jsonString;
}

void notifyClients(String state) {
  ws.textAll(state);
}

void handleWebSocketMessage(void *arg, uint8_t *data, size_t len) {
  AwsFrameInfo *info = (AwsFrameInfo*)arg;
  if (info->final && info->index == 0 && info->len == len && info->opcode == WS_TEXT) {
    data[len] = 0;
    if (strcmp((char *)data, "states") == 0)
    {
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
}

void onEvent(AsyncWebSocket *server, AsyncWebSocketClient *client, AwsEventType type, void *arg, uint8_t *data, size_t len) {
  switch (type) {
  case WS_EVT_CONNECT:
    // Serial.printf("WebSocket client #%u connected from %s\n", client->id(), client->remoteIP().toString().c_str());
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

void initWebSocket() {
  ws.onEvent(onEvent);
  server.addHandler(&ws);
}

void send_bme_status() {
  if (!bmeController.begin(0x76, &Wire)) {
    message += "\U00002757 Не найден датчик \U0001F321\U0001F4A7\U0001F94A у контроллера, проверьте подключение!\n\n";
  } else {
    message += "\U0001F4C9 Датчик у контроллера\n\U0001F321 температура: ";
    message += (int)bmeController.readTemperature();  // Type casting to int to remove decimal
    message += " °C\n";
    message += "\U0001F4A7 влажность: ";
    message += (int)bmeController.readHumidity();  // Type casting to int to remove decimal
    message += " %\n";
    message += "\U0001F94A давление: ";
    message += (int)(bmeController.readPressure()/133.322);  // Type casting to int to remove decimal
    message += " мм.рт.ст.\n\n";
  }
  if (!bmeGenerator.begin(0x77, &Wire)) {
    message += "\U00002757 Не найден датчик \U0001F321\U0001F4A7\U0001F94A у генератора, проверьте подключение!";
  } else {
    message += "\U0001F4C9 Датчик у генератора\n\U0001F321 температура: ";
    message += (int)bmeGenerator.readTemperature();  // Type casting to int to remove decimal
    message += " °C\n";
    message += "\U0001F4A7 влажность: ";
    message += (int)bmeGenerator.readHumidity();  // Type casting to int to remove decimal
    message += " %\n";
    message += "\U0001F94A давление: ";
    message += (int)(bmeGenerator.readPressure()/133.322);  // Type casting to int to remove decimal
    message += " мм.рт.ст.";
  }
  bot.sendMessage(CHAT_ID, message, "");
}

void start_generator() {
  if (digitalRead(PIN_Gas_Valve) == LOW) {
    ledRGB.fill(0x0000FF);
    ledRGB.show();
    digitalWrite(PIN_Gas_Valve, HIGH);
    notifyClients(getOutputStates());
  }

  if (zmpt_generator.getRmsVoltage() < 150 && start_reley_count > 0) {
    start_reley_count++;
    unsigned long currentMillis = millis(); //Записывается время начала процесса мигания
    // Петля для управления двойным миганием светодиода
    while (blinkCount < 6) { // Каждый цикл включения-выключения считается одним миганием, поэтому 4 цикла означают 2 мигания
      if (currentMillis - previousMillis >= ledBlinkInterval) {
        previousMillis = currentMillis;
        if (ledState == HIGH) { //Мигаем синим при старте генератора
          ledState = LOW;
          ledRGB.fill(0x0000FF); // Blue
        } else {
          ledState = HIGH;
          ledRGB.fill(0x000000); // Off
        }
        ledRGB.show();
        blinkCount++;
      }
      currentMillis = millis(); // Обновление currentMillis
    }
    blinkCount = 0;  // Сброс количества миганий при следующем вызове

    // Продолжаем работу с остальной частью функции start_generator
    digitalWrite(PIN_Start_Reley, HIGH);
    notifyClients(getOutputStates());
    delay(500);
    digitalWrite(PIN_Start_Reley, LOW);
    notifyClients(getOutputStates());
    //delay(10000);
    if (start_reley_count >= 5) start_reley_count = 0;
    if (hand_start == 1) hand_start = 0;
  }
}

void stop_generator() {
  digitalWrite(PIN_Start_Reley, HIGH);
  notifyClients(getOutputStates());
  delay(500);
  digitalWrite(PIN_Start_Reley, LOW);
  notifyClients(getOutputStates());
  digitalWrite(PIN_Gas_Valve, LOW);
  notifyClients(getOutputStates());
}

/*void hard_restart() {
  bot.sendMessage(CHAT_ID, "REBOOT", "");
  ESP.restart();
}*/

// Задаем действия при получении новых сообщений 
void handleNewMessages(int numNewMessages) {
  for (int i=0; i<numNewMessages; i++) {
    if (bot.messages[i].type == "callback_query") {

      bot.sendMessage(bot.messages[i].from_id, bot.messages[i].text, "");
    } else {
        // Идентификатор чата запроса
        String chat_id = String(bot.messages[i].chat_id);
        if (chat_id != CHAT_ID){
          bot.sendMessage(chat_id, "Неизвестный пользователь Telegram группы", "");
          continue;
        }
        String text = bot.messages[i].text;
        String from_name = bot.messages[i].from_name;
        if (from_name == "")
            from_name = "Guest";
        if (text == "/gen_start" || text == "/gen_start@" + String(NAME_BOT)) {
          if (hand_start == 0) { // Проверка на случай двоного запуска
            if (!alertSent_GEN_NOT_START) {
              bot.sendMessage(CHAT_ID, "\U000026A1 Штатный запуск генератора с бота от " + String(from_name), "");
              start_reley_count = 1;
              hand_start = 1;
              start_generator();
            } else {
                bot.sendMessage(CHAT_ID, "\U0001F525 Нештатный запуск генератора с бота от " + String(from_name), "");
                start_reley_count = 1;
                hand_start = 1;
                start_generator();
              }
          } else {
              bot.sendMessage(CHAT_ID, String(from_name) + " прости, ты не успел", "");
            }
        }
        if (text == "/gen_stop" || text == "/gen_stop@" + String(NAME_BOT)) {
          if (zmpt_generator.getRmsVoltage() > 150) {
            bot.sendMessage(CHAT_ID, "\U0001F4A4 Глушим генератор для " + String(from_name), "");
            stop_generator();
          } else {
            bot.sendMessage(CHAT_ID, "\U0001F4A4 Генератор не работает. " + String(from_name) + " извини.", "");
            }
          if (digitalRead(PIN_Gas_Valve) == HIGH) {
            digitalWrite(PIN_Gas_Valve, LOW);
          }
        }
        /*if (text == "/hardreset@NAME_BOT"){
          bot.sendMessage(CHAT_ID, "ПЕРЕЗАГРУЖАЮСЬ...", "");  //отправляем сообщение в чат
          //ESP.restart();                                      //перезагружаем плату
          hard_restart();
        }*/
        if (text == "/status" || text == "/status@" + String(NAME_BOT)) {
          if (zmpt_network.getRmsVoltage() > 150){
            message = String(from_name) + " вот смотри\n\n";
            message += "\U00002705 Напряжение с города: ";
            int network_volt = zmpt_network.getRmsVoltage();
            message += network_volt;
            message += " V\n";
            icon = 1;
          } else {
              message = String(from_name) + " вот смотри\n\n";
              message += "\U0000274C Напряжения с города нет\n";
              icon = 0;
            }
          if (zmpt_generator.getRmsVoltage() > 150){
            message += "\U0001F7E2 Генератор работает: ";
            int generator_volt = zmpt_generator.getRmsVoltage();
            message += generator_volt;
            if (digitalRead(PIN_Gas_Valve)) {
              message += " V\n\n";
            } else {
                message += " V \U00002757 Бензоклапан закрыт \U00002757\n\n";
              }
          } else {
              if (icon == 0) {
                message += "\U0000274C Генератор не работает\n\n";
              } else if (icon == 1) {
                  message += "\U0001F4A4 Генератор не работает\n\n";
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
        if (text == "/start") {
          message = String(from_name) + " привет!\n\nДобро пожаловать в Telegram Bot контроллера генератора ELP LH45iE.\n\n";
          message += "Логика автоматической работы: Генератор запускается(4 попытки) спустя ";
          vd = NO_VOLTAGE_DURATION/60000;
          message += vd;
          message += " мин. после отключения напряжения с города и остнавливается спустя ";
          vd = YES_VOLTAGE_DURATION/60000;
          message += vd;
          message += " мин. после влючения напряжения города.\n\n" ;
          message += "Описание состояния RGB индикатора:\n";
          message += "Белый мигает - есть напряжение с города и подключен WiFi\n";
          message += "Розовый мигает - есть напряжение с города, но не подключен WiFi\n";
          message += "Синий горит - автостарт генератора\n";
          message += "Жёлто-красный мигает - пропало напряжение с города\n";
          message += "Зеленый-красный мигает - если бензо-клапан закрыт, но генератор работает\n\n";
          message += "Для управления контроллером используйте следующий команды:\n";
          message += "/gen_start : запустить генератор\n";
          message += "/gen_stop : заглушить генератор\n";
          //message += "/hardreset : перезакрузить ESP32\n";
          message += "/status : получить сотояние генератора, WiFi, получить температуру/влажность/давление с датчиков у контроллера и генратора\n";
          bot.sendMessage(CHAT_ID, message, "");
        }
      }
  }
}

void setup() {
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  //Serial.begin(115200);
  //Serial.println("Serial START");
  // WiFi.mode(WIFI_STA);
  client.setCACert(TELEGRAM_CERTIFICATE_ROOT); // Добавляем корневой сертификат для api.telegram.org
  initSPIFFS();
  initWebSocket();
  // pinMode(PIN_Gas_Valve, OUTPUT);
  // pinMode(PIN_Start_Reley, OUTPUT);
  //  Назначаем GPIO выходами
  for (int i = 0; i < NUM_OUTPUTS; i++) {
    pinMode(outputGPIOs[i], OUTPUT);
  }

  zmpt_network.getRmsVoltage(SENSITIVITY);   // zmpt_network.init();
  zmpt_generator.getRmsVoltage(SENSITIVITY); // zmpt_generator.init();
  // Начальная страница
  server.on("/", HTTP_GET, [](AsyncWebServerRequest *request){
    request->send(SPIFFS, "/index.html", "text/html", false); 
  });
  server.serveStatic("/", SPIFFS, "/");
  server.begin();  // Запускаем сервер
  Wire.begin(8, 9);
  // Запускаем ElegantOTA
  ElegantOTA.begin(&server); // Запускаем ElegantOTA
  ledRGB.begin();
  ledRGB.setBrightness(128);
  ledRGB.setPixelColor(0, ledRGB.Color(0, 255, 255));
  ledRGB.show();
  delay(222);
  ledRGB.fill(0x000000);  // Выключить RGB_LED
  ledRGB.show();
  lastConnectCheck = millis();
  start_time = millis();
  alertSent_NO_GOROD = false;
  alertSent_YES_GOROD = false;
  alertSent_GEN_STARTED = false;
  alertSent_GEN_NOT_START = false;
  if (WiFi.status() == 3) { //3 это WL_CONNECTED (3) - устройство подключено к сети
    localWIFI_IP = WiFi.localIP().toString();
    message = "\U00002600 Запустился контроллер генератора на http://" + localWIFI_IP + "\nДоступен по адресу: http://10.160.231.25:8080\n\n";
  }
  if (zmpt_network.getRmsVoltage() > 150) {
      message += "\U00002705 Напряжение с города: ";
      int network_volt = zmpt_network.getRmsVoltage();
      message += network_volt;
      message += " V\n";
      icon = 1;
    } else {
        message += "\U0000274C Напряжения с города нет\n";
        icon = 0;
      }
    if (zmpt_generator.getRmsVoltage() > 150) {
      message += "\U0001F7E2 Генератор работает: ";
      int generator_volt = zmpt_generator.getRmsVoltage();
      message += generator_volt;
      if (digitalRead(PIN_Gas_Valve)) {
        message += " V\n\n";
      } else {
          message += " V \U00002757 Бензоклапан закрыт \U00002757\n\n";
        }
    } else {
        if (icon == 0) {
          message += "\U0000274C Генератор не работает\n\n";
        } else if (icon == 1) {
            message += "\U0001F4A4 Генератор не работает\n\n";
          }
      }
  send_bme_status();
}

void loop() {
  // server.handleClient();
  ElegantOTA.loop();
  ws.cleanupClients();
  checkSensors();

  if (millis() - lastConnectCheck > 300000  || millis() - start_time < 120000) {  // Проверять подключен ли WiFi 2 минуты при старте и каждые 5 минут далее
      if (WiFi.status() != WL_CONNECTED) {
          stat_wifi = false;
          WiFi.begin(WIFI_SSID, WIFI_PASS);
          delay(3000); 
          localWIFI_IP = WiFi.localIP().toString();
          message = "\U0001F501 WiFi зависал но переподключился на " + localWIFI_IP;
          bot.sendMessage(CHAT_ID, message, "");
      } else {
          stat_wifi = true;
      }
      lastConnectCheck = millis();  // Update last check time
  }

  if (PIN_Start_Button.click() && zmpt_generator.getRmsVoltage() < 150) {
    bot.sendMessage(CHAT_ID, "\U0001FAF8 Нажата кнопка старта на контролере генератора.\n\U000026A1 Запускаем генератор!", "");
    start_generator();
  }

// Запуск генеатора
  if (zmpt_network.getRmsVoltage() < 150) {
    if (zmpt_generator.getRmsVoltage() < 150) {
    if (start_reley_count > 0) {
      if (isLedOn) {
        // Если красный RGB_LED горит в течение 1 секунды
        if (millis() - ledOnStartTime >= 500) {
          ledRGB.fill(0xFFFF00);  // Выключаем жёлтый
          ledRGB.show();
          ledOffStartTime = millis();  // Запишим время, когда RGB_LED был выключен
          isLedOn = false;
        }
      } else {
          // Если жёлтый горит в течение 1 секунды, включаем красный RGB_LED
          if (millis() - ledOffStartTime >= 500) {
          ledRGB.fill(0x00FF00);
          ledRGB.show();
          ledOnStartTime = millis(); // Запишим время, когда RGB_LED был включен
          isLedOn = true;
          }
        }
      if (!alertSent_NO_GOROD) {
        message = "\U0000274C Пропало напряжение с города!\nЗапуск генератора через ";
        vd = NO_VOLTAGE_DURATION/60000;
        message += vd;
        message += " мин.";
        bot.sendMessage(CHAT_ID, message, "");
        alertSent_NO_GOROD = true; // Установим флаг, указывающий на то, что сообщение уже отправлено
      }
      
      if (noVoltageStartTime == 0) {
        noVoltageStartTime = millis(); // Start tracking the time
      } else {
        unsigned long currentTime_noVoltage = millis();
          if (currentTime_noVoltage - noVoltageStartTime >= NO_VOLTAGE_DURATION) {
            if (start_reley_count == 7) {
              bot.sendMessage(CHAT_ID, "\U0001F6A8 Заглох генератор. Пробуем запустить.\nПопытка запуска генератора № 1", "");
              start_reley_count = 1;
              alertSent_GEN_STARTED = false;
              start_generator();
            } else {
                if (alertSent_NO_NET == 0) {
                  alertSent_NO_NET = 1;
                  message = "\U000026A1 Прошло ";
                  vd = NO_VOLTAGE_DURATION/60000;
                  message += vd;
                  message += " мин. Напряжение с города не появилось.\n";
                  bot.sendMessage(CHAT_ID, message, "");
                }
                // Реализация 10-секундного интервала для попытки запуска генератора
                static unsigned long lastSentAttemptTime = 0; // Время последней отправки сообщения
                if (millis() - lastSentAttemptTime >= 10000) { // Если прошло 10 секунд
                    message = "Попытка запуска генератора № ";
                    message += start_reley_count;
                    bot.sendMessage(CHAT_ID, message, "");
                    start_generator();
                    lastSentAttemptTime = millis(); // Обновляем время последней отправки сообщения
                }
              }
          }
        }
    } else {
        // Генератор НЕ запустился 4 раза, включаем мигаюший красный
        if (!alertSent_GEN_NOT_START) {
          vd = NO_VOLTAGE_DURATION/60000;
          message = "\U0001F6A8 \U0001F6A8 \U0001F6A8 Генератор НЕ запустился 4 раза!\n\n";
          message += "\U0001F3C3 Надо срочно выезжать на узел!!\n\n";
          message += String(TG_NAMES) + " либо попробовать запустить генератор командой /gen_start\n\n";
          bot.sendMessage(CHAT_ID, message, "");
          alertSent_GEN_NOT_START = true; // Установим флаг, указывающий на то, что сообщение уже отправлено
        }
        ledRGB.fill(0x00FF00);
        ledRGB.show();
        delay(300);
        if (stat_wifi) {
        ledRGB.fill(0x000000);
        } else {
          ledRGB.fill(0xFFFFFF);
          }
        ledRGB.show();
      }
    } else {   // Генератор работает, включаем зелёный
        if (digitalRead(PIN_Gas_Valve) == HIGH) {
          ledRGB.fill(0xFF0000);
          ledRGB.show();
          if (!alertSent_GEN_STARTED) {
            start_reley_count = 7;
            int generator_volt = zmpt_generator.getRmsVoltage();
            message = "\U0001F7E2 Запустился генератор.\nНапряжение генератора: ";
            message += generator_volt;
            message += " V";
            bot.sendMessage(CHAT_ID, message, "");
            alertSent_GEN_STARTED = true;
          }
        } else {
            // если бензо-клапан закрыт, но генератор работает, то моргает зеленый-красный
            if (isLedOn) {
              // Если красный RGB_LED горит в течение 1 секунды
              if (millis() - ledOnStartTime >= 300) {
                ledRGB.fill(0xFF0000);  // Выключаем RGB_LED
                ledRGB.show();
                ledOffStartTime = millis();  // Запишим время, когда RGB_LED был выключен
                isLedOn = false;
              }
            } else {
                // Если зелёеый горит в течение 1 секунды, включаем красный RGB_LED
                if (millis() - ledOffStartTime >= 300) {
                  ledRGB.fill(0x00FF00);
                  ledRGB.show();
                  ledOnStartTime = millis(); // Запишим время, когда RGB_LED был включен
                  isLedOn = true;
                }
              }
            if (!alertSent_GEN_STARTED) {
              start_reley_count = 7;
              int generator_volt = zmpt_generator.getRmsVoltage();
              message = "\U0001F7E2 Запустился генератор.\nНапряжение генератора: ";
              message += generator_volt;
              message += " V \U00002757 Бензоклапан закрыт \U00002757";
              bot.sendMessage(CHAT_ID, message, "");
              alertSent_GEN_STARTED = true;
            }
          }
    }
  } 

  if (zmpt_network.getRmsVoltage() > 150) {
    if (zmpt_generator.getRmsVoltage() < 150) {
      if (start_reley_count > 1) {
        start_reley_count = 1;
        msg_network_on = 1;
        message = "\U0001F4A4 Генератор успешно заглушен\n\n\U00002705 Напряжение с города: ";
        int network_volt = zmpt_network.getRmsVoltage();
        message += network_volt;
        message += " V\n\n";
        send_bme_status();
      }
      if (digitalRead(PIN_Gas_Valve) == HIGH) {
        if (alertSent_NO_GOROD) {
          if (msg_network_on != 1) {
            bot.sendMessage(CHAT_ID, "\U00002705 Появилось напряжение с города", "");
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
          digitalWrite(PIN_Gas_Valve, LOW);
        }
      } else {
          if (alertSent_NO_GOROD) {
            if (msg_network_on != 1) {
              bot.sendMessage(CHAT_ID, "\U00002705 Появилось напряжение с города", "");
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
          if (start_reley_count != 0) {
            if (isLedOn) {
              // Если белый RGB_LED горит в течение 2 секунд
              if (millis() - ledOnStartTime >= 2000) {
                ledRGB.fill(0x000000);  // Выключаем RGB_LED
                ledRGB.show();
                ledOffStartTime = millis();  // Запишим время, когда RGB_LED был выключен
                isLedOn = false;
              }
            } else {
                // Если индикатор не горит в течение 2 секунд, включаем белый RGB_LED. Если WiFi отключен, то розовый
                if (millis() - ledOffStartTime >= 2000) {
                  if (stat_wifi) {
                    ledRGB.fill(0xFFFFFF);
                    } else {
                      ledRGB.fill(0x00FFFF);
                      }
                  ledRGB.show();
                  ledOnStartTime = millis(); // Запишим время, когда RGB_LED был включен
                  isLedOn = true;
                }
              }
          }
        }
    } else { // Остановка генеатора если есть питание из сети и реле клапана бензина включено, иначе мигает белый
        if (digitalRead(PIN_Gas_Valve) == HIGH) {
          if (!alertSent_YES_GOROD) {
            vd = YES_VOLTAGE_DURATION/60000;
            message = "\U00002705 Появилось напряжение с города, через ";
            message += vd;
            message += " мин. глушим генераатор";
            bot.sendMessage(CHAT_ID, message, "");
            alertSent_YES_GOROD = true; // Установим флаг, указывающий на то, что сообщение уже отправлено
          if (yesVoltageStartTime == 0) {
            yesVoltageStartTime = millis(); // Start tracking the time
          } else {
              if (millis() - yesVoltageStartTime >= YES_VOLTAGE_DURATION) {
                stop_generator();
                // ledOnStartTime = millis();  // Initialize ledOnStartTime at the moment stop_generator() is called
                isLedOn = true;
                vd = YES_VOLTAGE_DURATION/60000;
                message = "\U0001F4A4 Прошло ";
                message += vd;
                message += " мин. после появиления напряжения с города. Глушим генераатор!" ;
                bot.sendMessage(CHAT_ID, message, "");
              }
            }
          }
        }
      }

    }
  
  if (millis() > lastTimeBotRan + botRequestDelay) {
    int numNewMessages = bot.getUpdates(bot.last_message_received + 1);
    while(numNewMessages) {
      handleNewMessages(numNewMessages);
      numNewMessages = bot.getUpdates(bot.last_message_received + 1);
    }
    lastTimeBotRan = millis();
  }
}