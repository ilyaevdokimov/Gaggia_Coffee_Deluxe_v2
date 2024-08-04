#include <DNSServer.h> // DNS-сервер
#include <LittleFS.h> // Файловая система
#include <WiFi.h> // Wi-Fi сервисы
#include <ESPAsyncWebServer.h> // WEB-сервисы
#include <ElegantOTA.h>  // Обновление прошивки по воздуху через WEB-интерфейс
#include <Preferences.h> // Сервис настроек
#include <AutoPID.h> // ПИД-регулятор с релейным выходом
#include "HX710B.h" // Весы
#include <SPI.h>
#include <GyverMAX6675_SPI.h> // Термопары
#include <Wire.h> // I2C, необходима для работы VL6180X
#include <VL6180X.h> // Лазерный дальномер для определения уровня воды в танкере (библиотека POLOLU)
#include <Ticker.h> // Таймер для сбора данных с термистора для усреднения
//#include <Adafruit_MAX31865.h> // Термистор PT100

#define BOOT_PIN 0 // Кнопка жёсткого сброса и принудительного входа в режим прошивки
#define PASS_BUTTON 3 // Кнопка включения пролива 2
#define STEAM_BUTTON 2 // Кнопка включения режима пара 3
#define STEAM_VALVE_BUTTON 4 // Кран пара
#define WORKSPACE_LED 14 // Подсветка рабочей области
#define TANK_LED 5 // Подсветка ёмкости с водой, сигнализирует о достижении целевой температуры режима и критическом уровне воды в баке
#define SOUND_INDICATION 11 // Динамик
#define FLOWMETER 38 // Флоуметр, работает по прерыванию
#define PASS_VALVE 10 // Клапан сброса давления
#define SCALES_CLOCK 12 // Весы clock
#define SCALES_DATA 13 // Весы data
#define HEATING 21 // Нагрев
#define PUMP 6 // Помпа
//#define PT100_CS_PIN 33 // Термистор PT100 CS
#define BTC_CS_PIN 18 // CS термопары бойлера тип К
#define GTC_CS_PIN 34 // CS термопары группы тип К
#define NTC_PIN 1 // Аналоговый пин NTC-термистора
#define SDI_PIN 35 // SDI
#define CLK_PIN 36 // SCK (CLK)
#define DATA_PIN 37 // SO (SDO)

// Настройки по умолчанию
#define ESPRESSO_TEMPERATURE 113 // Целевая температура для эспрессо-пролива 
#define STEAM_TEMPERATURE 135 // Целевая температура парообразования
#define SCALE_CALIBRATION_FACTOR 2404 // Калибровочный коэффициент весов
#define PULSEWIDTH 500 // Интервал обновления ПИД-регулятора
#define PAGE_REFRESH_INTERVAL 500 // Интервал обновления интерфейса
#define PASS_VALVE_LIVE_TRESHOLD 87 // Пороговое значение боевого пролива (время делённое на объём)
#define PASS_VALVE_OPEN_TIME 7 // Время открытия клапана сброса давления в секундах
#define AUTO_OFF_TIMEOUT 30 // Время автоматического отключения при простое (в минутах)
#define BOOSTER_SWAP_INTERVAL 3000 // Интервал включения подкачки микрообъёмов воды в режиме бустера пара
#define BOOSTER_SWAP_TIMEOUT 10000 // Задержка включения подкачки микрообъёмов воды в режиме бустера пара
#define LEDC_TIMER_12_BIT 12 // Используем 12 bit точность для таймера LEDC
#define LEDC_BASE_FREQ 5000 // Используем 5000 Hz в качестве базовой частоты LEDC
#define LEDC_START_DUTY   (400) // Начальное заполнение при дыхании
#define LEDC_TARGET_DUTY  (4095) // Целевое заполнение при дыхании
#define LEDC_AUTOOFF_DUTY  (200) // Заполнение в режиме экономии энергии
#define LEDC_FADE_TIME    (2000) // Максимальное время затухания
#define WATER_LEVEL_BUFFER_SIZE 16 // Размер буфера для усреднения показаний дальномера (уровня воды в танкере)
#define SOFT_AP_SSID "CoffeeAP"
#define SOFT_AP_PASSWORD "Up2DownAP"

bool waitingAfterReset = false;
bool isSoftAP = false; // Флаг работы в режиме точки доступа
bool preferWEB = false;; // Предпочитать удалённое управление через WEB-интерфейс
bool isLivePass = false;; // Индикатор боевого пролива
bool isPumpTimeOut = true; // Действует ли задержка включения циклической подкачки для режима бустера пара
uint16_t boosterTimer = 0; // Счётчик задержки включения подкачки в режиме бустера пара
uint16_t boosterSwapTimer = 0; // Счётчик задержки цикла подкачки

// Глобальные переменные, хранящие нативные значения параметров. Позже они сменят свои значения на те, которые записаны в текстовых параметрах:
double setTemp = ESPRESSO_TEMPERATURE; // Температура эспрессо (переменная для ПИД-регулятора). Натив от P1
uint8_t steamTemperature = STEAM_TEMPERATURE; // Температура пара. Натив от P2
uint8_t passValveOpenTime = PASS_VALVE_OPEN_TIME; // Продолжительность открытия клапана сброса давления. Натив от P3
uint8_t autoOffTimeout = AUTO_OFF_TIMEOUT; // Таймаут автоотключения. Натив от P4
// Ещё не реализованные:
float targetWeight = 0.0; // Целевая масса напитка. При значении 0 не учитывается. Натив от P5
bool beginHeatingOnPoweringOn = false; // Начинать ли нагрев сразу при включении кофеварки. Натив от P6
String SoftAP_SSID = SOFT_AP_SSID; // Имя встроенной точки доступа. Натив от P7
String SoftAP_password = SOFT_AP_PASSWORD; // Пароль встроенной точки доступа. Натив от P8

// Инициализируем переменные, хранящие текстовые параметры, значениями по-умолчанию, зашитыми в прошивке:
String P1 = String(ESPRESSO_TEMPERATURE);
String P2 = String(STEAM_TEMPERATURE);
String P3 = String(PASS_VALVE_OPEN_TIME);
String P4 = String(AUTO_OFF_TIMEOUT);
String P5 = "0";
String P6 = "0";
String P7 = String(SOFT_AP_SSID);
String P8 = String(SOFT_AP_PASSWORD);
bool isParamterChanges[] = {true, true, true, true, true, true, true, true}; // Флаги необходимости обновления. Нам нужно обновить параметры в самое ближайшее время

// Переменные для хранения значений датчиков
double temperature; // Температура бойлера для ПИД-управления
uint16_t rawNTCvalues[WATER_LEVEL_BUFFER_SIZE]; // Массив для усреднения показаний NTC-термистора
uint16_t groupTemperature; // Температура группы по термопаре
uint16_t boilerTemperature = 0; // Температура бойлера по термопаре

float passTime; // Время пролива в секундах
uint64_t passBegin; // Отметка времени начала пролива
volatile float passTimeInMillis; // Время пролива в милисекундах
volatile uint16_t waterStreamValue; // Величина объёма воды, пролитой через группу
uint8_t waterLevel = 100; // Уровень воды в танкере
uint8_t waterLevels[WATER_LEVEL_BUFFER_SIZE]; // Массив для усреднения показаний дальномера (уровнемера)
float weight; // Вес на весах

// Перечисление состояний кофеварки
enum {
  Wait, // Ожидание
  Pass, // Пролив
  Drain, // Дренаж (пролив + кран пара)
  Steam, // Пар
  Booster, // Пар + Кран пара
  SteamValve, // Кран пара
  Reset // Сброс
} states;
volatile uint8_t currentState = Wait; // Изначально находмся в состоянии ожидания
volatile uint8_t newState = currentState; // Новое состояние
volatile uint64_t lastStateChangeMillis; // Отметка millis последнего изменения состояния
volatile uint64_t checkStateMicros; // Отметка micros для вычисления длительности выполнения процедуры проверки изменения состояния checkState()
volatile uint32_t checkStateDurationInMicros = 1000; // Переменная для вывода длительности выполнения процедуры проверки изменения состояния checkState()
uint8_t WEBState; // Состояние, установленное через WEB-интерфейс
volatile bool isStateChanged = false; // Индикатор изменения состояния
volatile bool StateWasChanged = false;

// Для инициализации пинов при первом loop'е, чтобы  иметь возможность засечь bootloop
bool runOnce = true;

// Переменные для управления исполнительными устройствами
bool isEspressoHeatingOn = false; // Включен ли нагрев
bool isTemperatureReached = false; // Достигнута ли температура текущего режима

// Переменные для управления "дыханием"
bool isFadeEnded = false; // Статус зажигания/затухания LED (закончилось ли оно)
bool isFadeOn = true; // Переключатель режима затухание/зажигание

Preferences preferences; // Объект настроек
DNSServer dnsServer; // DNS для переадресации запросов Captive Portal
AsyncEventSource events("/events"); // Объект SSE
AsyncWebServer server(80); // Веб-сервер, прослушивающий HTTP-запросы на порту 80
//Adafruit_MAX31865 pt100 = Adafruit_MAX31865(PT100_CS_PIN); // Термистор PT100 на аппаратном SPI
GyverMAX6675_SPI<BTC_CS_PIN> kTCboiler; // Термопара бойлера на аппаратном SPI
GyverMAX6675_SPI<GTC_CS_PIN> kTCgroup; // Термопара группы на аппаратном SPI
VL6180X waterLevelSensor; // Лазерный дальномер на I2C, измеряет уровень воды в танкере
HX710B scale; // Весы

hw_timer_t *autoOFFtimer = NULL; // Таймер автоотключения
bool isAutoOFFneeded = false; // Флаг необходимости процедуры автоотключения
hw_timer_t *flashTimer = NULL; // Таймер сброса давления
Ticker dataGrabber; // Таймер сбора данных с NTC-термистора для усреднения
uint8_t dataGrabberLoopCounter = 0; // Счётчик для обслуживания буфера значений NTC-термистора

// Задачи:
void HeaterControl(void *pvParameters); // Задача управления нагревом
void RefreshControlPanel(void *pvParameters); // Задача для актуализации панели управления (странички)

// Обработчики прерываний:
void ARDUINO_ISR_ATTR checkButtonState() { // Обработчик прерывания от кнопок управления и крана пара (дребезг подавлен аппаратно)
  lastStateChangeMillis = millis();
  checkStateMicros = micros();
  isStateChanged = currentState != checkState(); // Фиксируем факт изменения состояния (если таковой имеет место)
  digitalWrite(SOUND_INDICATION, !isStateChanged); // В целях отладки: включаем звуковой сигнал, чтобы засечь, а было ли прерывание
  StateWasChanged = isStateChanged;
  checkStateDurationInMicros = 1000;
  lastStateChangeMillis = millis(); // чтобы засечь, давно ли последний раз менялось состояние (нет ли дребезга или залипания)
}

void ARDUINO_ISR_ATTR countWaterValue() { // Подпрограмма подсчёта импульсов флоуметра
  if (currentState == Pass) passTimeInMillis = millis() - passBegin;
  waterStreamValue++;
}

void ARDUINO_ISR_ATTR LED_FADE_ISR() { // Обработчик прерывания на достижение максимума/минимума затухания
  isFadeEnded = true;
}

void ARDUINO_ISR_ATTR autoOFF() { // Обработчик прерывания на таймаут простоя
  isAutoOFFneeded = true; // Сигнализируем о необходимости выолнения процедуры автоотключения
}

void ARDUINO_ISR_ATTR closePassValve() { // Обработчик прерывания на таймаут открытия клапана сброса давления
  digitalWrite(PASS_VALVE, LOW); // Закрываем клапан
}

void getNTCvalue() {
  if (dataGrabberLoopCounter < WATER_LEVEL_BUFFER_SIZE) { // Если мы в границах буфера
    rawNTCvalues[dataGrabberLoopCounter] = analogReadMilliVolts(NTC_PIN); // Записываем значение НАПРЯЖЕНИЯ в милливольтах с указанного пина в кольцевой буфер
    dataGrabberLoopCounter++;
    if (dataGrabberLoopCounter == WATER_LEVEL_BUFFER_SIZE) dataGrabberLoopCounter = 0; // Сбрасываем счётчик при переполнении
  }
}

void setup() {
  // Кнопки управления приходится активировать здесь, поскольку они задействованы при проверке необходимости аппаратного сброса
  delay(1);
  pinMode(PASS_BUTTON, INPUT_PULLUP);
  delay(1);
  pinMode(STEAM_BUTTON, INPUT_PULLUP);
  delay(1);
  pinMode(STEAM_VALVE_BUTTON, INPUT_PULLUP);
  delay(1);
  // Динамик тоже нужен уже сейчас, поэтому инициализируем его здесь
  pinMode(SOUND_INDICATION, OUTPUT);
  delay(1);
  digitalWrite(SOUND_INDICATION, HIGH); // Утихомириваем
  // 0 пин загрузку уже отработал, теперь он нам понадобится как индикатор сброса
  pinMode(BOOT_PIN, INPUT_PULLUP);
  delay(1);

  // Проверяем, не активирован ли режим аппаратного сброса
  if (!digitalRead(PASS_BUTTON) && !digitalRead(STEAM_BUTTON) && digitalRead(STEAM_VALVE_BUTTON)) { // Кнопки пара и пролива нажаты, кран пара открыт до срабатывания микрика
    pinMode(WORKSPACE_LED, OUTPUT); // Будем сигнализировать подсветкой рабочего стола о  ходе сброса параметров
    digitalWrite(WORKSPACE_LED, HIGH);
    uint8_t resetTimer; // Счётчик времени нажатия кнопки принудительного сброса настроек
    bool isHardResetNeeded = true; // Взводим флаг необходимости аппаратного сброса
    // Бибкаем, приглашая нажать кнопку сброса
    digitalWrite(SOUND_INDICATION, LOW);
    delay(1000);
    digitalWrite(SOUND_INDICATION, HIGH);
    delay(1);

    while (isHardResetNeeded) { // Пока флаг необходимости сброса не сброшен
      digitalWrite(WORKSPACE_LED, !digitalRead(WORKSPACE_LED)); // Переключаем подсветку рабочего стола
      delay(1000); // Ждём секунду
      if (!digitalRead(BOOT_PIN)) resetTimer++; // Если кнопка аппаратного сброса нажата, увеличиваем счётчик на время, прошедшее с момента нажатия
      else resetTimer = 0; // в противном случае сбрасываем счётчик
      if (resetTimer >= 5) { // Если кнопка была нажата 5 секунд или более
        doHardReset(); // Делаем аппаратный сброс
        isHardResetNeeded = false; // Выходим из цикла ожидания
      }
    }
    // Сигнализируем включённой подсветкой об удачном сбросе
    digitalWrite(WORKSPACE_LED, HIGH);
    // Дополнительно  сигнализируем об удачном сбросе звуком
    digitalWrite(SOUND_INDICATION, LOW);
    delay(200);
    digitalWrite(SOUND_INDICATION, HIGH);
    delay(1);
    waitingAfterReset = true; // Выставляем флаг невыполнения содержимого loop и отправляемся в вечное ожидание
    return;
  }

  // Запускаем  термистор PT100:
  //delay(1000);
  //pt100.begin(MAX31865_2WIRE);

  // Запускаем I2C и лазерный дальномер:
  Wire.begin();
  waterLevelSensor.init();
  waterLevelSensor.configureDefault();
  waterLevelSensor.setTimeout(500);

  // Запускаем и калибруем весы:
  scale.begin(SCALES_DATA, SCALES_CLOCK);
  scale.set_scale(SCALE_CALIBRATION_FACTOR);

  // Таймер автоотключения:
  autoOFFtimer = timerBegin(1000); // Создаём и запускаем аппаратный таймер с разрешением 1Khz
  timerAttachInterrupt(autoOFFtimer, &autoOFF); // Вешаем прерывание на его срабатывание
  timerStop(autoOFFtimer); // Останавливаем, ибо нагрев ещё не включён

  // Таймер сброса давления:
  flashTimer = timerBegin(1000); // Создаём и запускаем аппаратный таймер с разрешением 1Khz
  timerAttachInterrupt(flashTimer, &closePassValve); // Вешаем прерывание на его срабатывание
  timerStop(flashTimer); // Останавливаем. Запустим, когда сделаем боевой пролив
  timerWrite(flashTimer, 0); // Сбрасываем счётчик

  initParams(); // Инициализируем значения текстовых параметров
  updateNativeParameterValues(); // Обновляем значения нативных параметров, сбрасываем таймеры и устанавливаем им время и параметры срабатывания

  // Запускаем файловую систему LittleFS, при неудаче сигнализируем длинным гудком:
  if (!LittleFS.begin()) {
    digitalWrite(SOUND_INDICATION, LOW);
    delay(5000);
    digitalWrite(SOUND_INDICATION, HIGH);
    delay(1);
  }
  else {
    startWiFi(); // Подсоединяемся к Wi-Fi точке доступа (или создаем свою точку доступа)
    startWEBServer(); // Запускаем HTTP-сервер с обработчиками
  }

  dataGrabber.attach_ms(10, getNTCvalue); // Срабатываем каждые 10 мс
}

void loop() {
  if (!waitingAfterReset) { // Выполняем loop только если не находимся в режиме аппаратного сброса
    if (runOnce) {
      analogReadResolution(12); // Без этого почему-то работает 13 битное разрешение...
      pinInitialization(); // Переназначаем режимы GPIO и приводим их в начальное состояние

      // Определяемся с типом урпавления
      //preferWEB = false;

      // Создаём и запускаем задачи
      xTaskCreate(RefreshControlPanel, "RCP", 4096, NULL, 1, NULL); // Запуск задачи актуализации данных на панели управления
      xTaskCreate(HeaterControl, "HC", 4096, NULL, 1, NULL); // Запуск задачи управления нагревом

      // Сигнализируем об успешной загрузке
      digitalWrite(SOUND_INDICATION, LOW);
      delay(25);
      digitalWrite(SOUND_INDICATION, HIGH);
      delay(1);

      // Включаем (или нет) нагрев при включении кофеварки
      isEspressoHeatingOn = beginHeatingOnPoweringOn;
      
      runOnce = false; // Было хорошо, но больше так не надо
    }

    if (isStateChanged) { // Если в результате нажатия кнопок или поворота крана пара изменилось состояние...
      changeState(); // ...выполняем необходимые при этом действия
      isStateChanged = false; // Изменения внесены, флаг нужно сбросить
      int tempDuration = micros() - checkStateMicros;
      checkStateDurationInMicros = tempDuration > 0 ? tempDuration : 0;
    }

    if (isSoftAP) dnsServer.processNextRequest(); // В режиме Soft AP обрабатываем DNS-запросы
    ElegantOTA.loop(); // Обновляемся по воздуху
    lightIndication(); // Сигнализируем светом о выходе на режим при включённом нагреве
  }
}

void pinInitialization() {
  // NTC-термистор
  pinMode(NTC_PIN, INPUT);
  // Кнопки управления. Обязательно нужен INPUT_PULLUP, без этого они не работают!
  // Кнопка включения пролива
  //pinMode(PASS_BUTTON, INPUT_PULLUP);
  attachInterrupt(PASS_BUTTON, checkButtonState, CHANGE); // срабатываем на изменение сигнала
  // Кнопка включения пролива
  //pinMode(STEAM_BUTTON, INPUT_PULLUP);
  attachInterrupt(STEAM_BUTTON, checkButtonState, CHANGE); // срабатываем на изменение сигнала
  // Кнопка включения пролива
  //pinMode(STEAM_VALVE_BUTTON, INPUT_PULLUP);
  attachInterrupt(STEAM_VALVE_BUTTON, checkButtonState, CHANGE); // срабатываем на изменение сигнала
  // Флоуметр
  pinMode(FLOWMETER, INPUT_PULLUP); // Чтобы ловить сигнал от флоуметра, а не шумы
  attachInterrupt(FLOWMETER, countWaterValue, FALLING); // срабатываем на падение сигнала
  // Нагрев
  pinMode(HEATING, OUTPUT);
  // Помпа
  pinMode(PUMP, OUTPUT);
  // Клапан сброса давления
  pinMode(PASS_VALVE, OUTPUT);
  // LED-подсветка рабочей области
  ledcAttach(WORKSPACE_LED, 5000, 12); // Конфигурируем автоматически выбранный канал и вешаем на него наш MOSFET
  // LED-подсветка ёмкости с водой
  ledcAttach(TANK_LED, 5000, 12); // Конфигурируем автоматически выбранный канал и вешаем на него наш MOSFET
  isFadeEnded = true;
}
