// Подпрограмма выполнения аппаратного сброса
void doHardReset() {
  // Код сброса параметров на значения по умолчанию
  preferences.begin("gSettings", false); // Открываем настройки на запись
  // Сбрасываем значения текстовых параметров:
  if (preferences.isKey("P1")) preferences.putString("P1", String(ESPRESSO_TEMPERATURE));
  if (preferences.isKey("P2")) preferences.putString("P2", String(STEAM_TEMPERATURE));
  if (preferences.isKey("P3")) preferences.putString("P3", String(PASS_VALVE_OPEN_TIME));
  if (preferences.isKey("P4")) preferences.putString("P4", String(AUTO_OFF_TIMEOUT));
  if (preferences.isKey("P5")) preferences.putString("P5", "0");
  if (preferences.isKey("P6")) preferences.putString("P6", "0");
  if (preferences.isKey("P7")) preferences.putString("P7", String(SOFT_AP_SSID));
  if (preferences.isKey("P8")) preferences.putString("P8", String(SOFT_AP_PASSWORD));
  preferences.end(); // Закрываем настройки
  delay(500);
}

// Функция проверки состояния. Должна работать очбыстро, поскольку вызывается из прерывания (измеренное значение порядка 20 мкс)
uint8_t checkState() {
  if (currentState == Diagnostics) return Diagnostics; // Если мы в режиме диагностки, то какие бы аппаратные кнопки мы не нажимали, всё равно остаёмся в ржиме диагностики
  
  // Читаем состояние органов управления:
  bool isPassButtonPressed = !digitalRead(PASS_BUTTON);
  bool isSteamButtonPressed = !digitalRead(STEAM_BUTTON);
  bool isSteamValveOpen = !digitalRead(STEAM_VALVE_BUTTON);

  // Определяем новое состояние:
  // Только кнопка "Пролив". При включённой кнопке пара режим работать не будет!
  if (isPassButtonPressed & !isSteamButtonPressed & !isSteamValveOpen) newState = Pass;
  // Только кнопка "Пар" - используется для нагрева бойлера до заданной температуры и её поддержания
  else if (isSteamButtonPressed & !isSteamValveOpen & !isPassButtonPressed) newState = Steam;
  // Только кран пара - ну открыли и открыли, ничего не надо делать
  else if (isSteamValveOpen & !isSteamButtonPressed & !isPassButtonPressed) newState = SteamValve;
  // Кнопка "Пролив" и кран пара - делаем дренаж в стимер. Только при отключенном режиме пара!
  else if (isPassButtonPressed & isSteamValveOpen & !isSteamButtonPressed) newState = Drain;
  // Кнопка "Пар" и кран пара - мы взбиваем молоко, нам нужен бустер
  else if (isSteamButtonPressed & isSteamValveOpen & !isPassButtonPressed) newState = Booster;
  // Во всех остальных случаях работает режим "Ожидание"
  else newState = Wait;

  if (preferWEB) { // При использовании WEB-управления учитываем состояние клавиатуры (нас интересует кран пара, ибо больше нам этот сигнал взять негде)
    if (isSteamValveOpen && WEBState == Steam) newState = Booster; // При открытом кране пара включаем бустер, если текущий WEB-режим - это Пар
    else newState = WEBState; // При закрытом - используем текущий режим, установленный через WEB-интерфейс
  }
  return newState;
}

void changeState() {
  timerWrite(autoOFFtimer, 0);  // Сброс таймера автоотключения
  digitalWrite(PUMP, LOW); // Переход из одного состояния в другое всегда должен происходить через отключение помпы. Новый режим включит её при необходимости

  // Проверяем, был ли пролив боевым
  isLivePass = passTime > 13 && waterStreamValue > 0 && passTimeInMillis / waterStreamValue > PASS_VALVE_LIVE_TRESHOLD;

  if (currentState == Pass && isLivePass) { // Если пролив завершён (newState ведь не равен currentState), и он был боевым
    digitalWrite(PASS_VALVE, HIGH); // Открываем клапан. А закроет его обработчик прерывания тайммера
    timerAlarm(flashTimer, passValveOpenTime * 1000, false, 0); // Устанавливаем время однократного срабатывания таймера сброса давления
    timerStop(flashTimer);
    timerWrite(flashTimer, 0); // Сбрасываем счётчик
    timerRestart(flashTimer); // Перезапускаем таймер сброса давления
    timerStart(flashTimer); // Запускаем таймер
  }
  // Пролив проверен, теперь можно запомнить новое состояние:
  currentState = newState;

  // Приводим состояния исполнительных устройств в соответствие с новым режимом. Помпа:
  if (currentState == Pass || currentState == Drain) {
    if (waterLevel > 10) digitalWrite(PUMP, HIGH); // Включаем только при достаточном уровне воды. Выключится она сама при следующей смене состояния
    else { // Если воды мало, сигнализируем
      // Длинным гудком:
      digitalWrite(SOUND_INDICATION, LOW);
      // И светом:
      ledcWrite(TANK_LED, 4095);
      vTaskDelay(500);
      ledcWrite(TANK_LED, 0);
      vTaskDelay(500);
      digitalWrite(SOUND_INDICATION, HIGH);
      ledcWrite(TANK_LED, 4095);
      vTaskDelay(500);
      ledcWrite(TANK_LED, 0);
      vTaskDelay(500);
    }
    if (currentState == Pass) { // При начале пролива (но не дренажа!)
      passTime = 0; // Сбрасываем счётчик времени пролива
      passBegin = millis(); // И временнУю метку
      passTimeInMillis = 0; // Да и время пролива в милисекундах - тоже, на всякий случай
      waterStreamValue = 0; // Сбрасываем счётчик объёма воды, пролитой через группу
      scale.tare(1); // Сбрасываем значение на весах за 1 проход, чтобы побыстрее (при значении по-умолчанию выполняется больше секунды!)
    }
  }
  // Бустер. Если мы переключаемся в этот режим, сбрасываем флаг 10-секундной задержки включения циклической подкачки воды
  if (currentState == Booster) {
    isPumpTimeOut = true;
    boosterTimer = 0;
    boosterSwapTimer = 0;
  }
}

void updateControlPanel() {
  uint8_t steamLowerTreshold = steamTemperature - 4; // Нижний предел температуры пара для индикации
  uint8_t steamUpperTreshold = steamTemperature + 10; // Верхний предел температуры пара для индикации

  // Инкрементируем служебные переменные:
  if (currentState == Booster) { // Временные интервалы не точные, но для такой цели сойдёт
    if (boosterTimer < BOOSTER_SWAP_TIMEOUT) boosterTimer += PAGE_REFRESH_INTERVAL;
    else isPumpTimeOut = false;
    if (boosterSwapTimer < BOOSTER_SWAP_INTERVAL) boosterSwapTimer += PAGE_REFRESH_INTERVAL;
  }

  if (waterLevel == 0) changeState(); // При недостаточном уровне воды во время пролива выключаем помпу и сигнализируем звуком и светом о низком уровне
  else passTime = passTimeInMillis / 1000; // Если помпа работает, считаем время пролива

  // Подготавливаем и отсылаем страничке значения с датчиков:
  temperature = getNTCtemperature(); // Обновляем значение глобальной переменной, чтобы ПИД мог брать оттуда актуальные данные
  boilerTemperature = kTCboiler.getTempInt(); // Температура бойлера, отображаемая в интерфейсе
  groupTemperature = kTCgroup.getTempInt(); // Температура группы
  waterLevel = getWaterLevel(loopCounter++); // Актуализируем уровень воды в танкере (это занимает порядка 15 мс):
  if (loopCounter >= WATER_LEVEL_BUFFER_SIZE) loopCounter = 0; // Сбрасываем счётчик при переполнении
  currentWeight = myRound(scale.get_units(1)); // Вес напитка
  // Сигнализируем о попадании температуры в заданный диапазон при нахождении:
  uint16_t targetTemperature = (boilerTemperature + groupTemperature) / 2; // Усреднённая температура бойлера и группы
  // в режиме пара или бустера
  if (currentState == Steam || currentState == Booster) isTemperatureReached = temperature > steamLowerTreshold && temperature < steamUpperTreshold;
  // в режиме эспрессо
  else if (currentState == Wait  || currentState == Pass  || currentState == SteamValve) isTemperatureReached = groupTemperature > 90 && groupTemperature < 105;
  // Формируем пакет данных для отправки на страницу по SSE
  String ss;
  makeSendString(ss); // Пихаем все значения в одну строку через разделитель
  events.send(ss.c_str(), "values", millis()); // Отправляем данные на страницы, подписанные на наше событие SSE
}

uint8_t getWaterLevel(uint8_t counter) { // Определяет усреднённый уровень воды в процентах. Возвращает корректное значение после WATER_LEVEL_BUFFER_SIZE вызовов, когда накопит данные
  uint8_t result = 200; // Индикация проблемы с индексацией буфера. Так получиться не должно, просто на всякий случай
  if (counter < WATER_LEVEL_BUFFER_SIZE) { // Если мы в границах буфера
    waterLevels[counter] = waterLevelSensor.readRangeSingleMillimeters(); // Читаем очередное значение (оно колеблется в пределах пары миллиметров) и записываем его в буфер для усреднения
    // Усредняем:
    uint16_t summ = 0;
    for (uint8_t i = 0; i < WATER_LEVEL_BUFFER_SIZE; i++) {
      summ += waterLevels[i];
    }
    uint8_t mmResult = summ / WATER_LEVEL_BUFFER_SIZE; // Стабильное усреднённое значение
    // Приводим его из миллиметрового к процентному виду:
    result = 130 - mmResult; // 130 - это эмпирическое значение пустого танкера
    if (result < 0) result = 0; // Может получиться меньше 0, прям совсем до дна никто не мерял
    if (result > 100) result = 100; // Больше максимума не показываем
  }
  return result; // Если вернётся 200, значит что-то не так с индексацией буфера
}

void makeSendString(String& s) { // Формирование строки для объекта AsyncEventSource
  s += String(temperature, 1); // Темпрература с датчика регулирования (сейчас это NTC-термистор) 0
  s += "¿";
  s += groupTemperature; // Температура группы (с термопары) 1
  s += "¿";
  s += waterLevel; // Уровень воды в танкере 2
  s += "¿";
  s += stateStrings[currentState]; // Строковое описание текущего состояния 3
  s += "¿";
  s += String(passTime, 1); // Время пролива 4
  s += "¿";
  s += waterStreamValue; // Объём пролива 5
  if (waterStreamValue > 0) {
    s += "\/";
    s += passTimeInMillis / waterStreamValue; // Время пролива, делённое на объём 5
  }
  s += "¿";
  s += ESP.getFreeHeap(); // Объём свободной памяти 6
  s += "¿";
  s += boilerTemperature; // Температура бойлера по термопаре 7
  s += "¿";
  String diagnosticString = "..."; // String(deugValueChangesMillis);
  s += diagnosticString; // Строка диагностики 8
  s += "¿";
  s += String(currentWeight, 1); // Вес напитка 9
  s += "¿";
  s += String(isLivePass); // Индикатор боевого пролива 10
  s.trim();
}

float myRound(float var) { // Функция округления до десятых долей. Я её списал, если что
  float value = (int)(var * 10 + .5);
  return (float)value / 10;
}

void lightIndication() { // Используем аппаратный фэйдинг
  if (currentState != Diagnostics) { // В режиме диагностики автоматическое управление светом должно быть выключено, иначе мы ничего не заметим
    if (isEspressoHeatingOn) { // Если нагрев включен,
      if (isTemperatureReached) ledcWrite(TANK_LED, LEDC_TARGET_DUTY); // и температура в приемлемом диапазоне, светим на постоянку
      else { // В противном случае "дышим":
        if (isFadeEnded) { // Проверяем, не закончилось ли затухание/зажигание
          isFadeEnded = false; // Сбрасываем флаг
          // В зависимости от того, в каком состоянии переключатель...
          if (isFadeOn) ledcFadeWithInterrupt(TANK_LED, LEDC_START_DUTY, LEDC_TARGET_DUTY, LEDC_FADE_TIME, LED_FADE_ISR); // ...делаем затухание...
          else ledcFadeWithInterrupt(TANK_LED, LEDC_TARGET_DUTY, LEDC_START_DUTY, LEDC_FADE_TIME, LED_FADE_ISR); // ...или зажигание
          isFadeOn = !isFadeOn; // Переключаем режим
        }
      }
    }
    else { // Выключили нагрев
      ledcWrite(TANK_LED, LEDC_AUTOOFF_DUTY); // Светим вполсилы в танкере
      ledcWrite(WORKSPACE_LED, LOW); // Выключаем подсветку рабочей зоны
    }
  }
}

// Подстановка значений в плейсхолдеры на страницах, этого требующих (например, на странице настроек)
String processor(const String& var) {
  if (var == "P1") return P1;
  if (var == "P2") return P2;
  if (var == "P3") return P3;
  if (var == "P4") return P4;
  if (var == "P5") return P5;
  if (var == "P5C") return P5.toInt() == 0 ? "" : "checked";
  if (var == "P6") return P6 == "0" ? "" : "checked";
  if (var == "P7") return P7;
  if (var == "P8") return "12345678"; // ну не отправлять же на страницу настоящий пароль...
  if (var == "WiFiNetworks") {
    String htmlResult = ""; // Итоговая HTML-разметка
    preferences.begin("knownNetworks", false);
    if (LittleFS.exists("/knownNetworks.txt")) { // Если файл настроек существует
      File file = LittleFS.open("/knownNetworks.txt"); // Пытаемся его открыть
      if (file) { // Если удалось
        while (file.available()) {
          String current = file.readStringUntil('\n'); // Построчно читаем его содержимое
          if (preferences.isKey(current.c_str())) { // Если очередная строка есть в соответствующем разделе настроек, формиируем очередную порцию разметки:
            htmlResult += WiFi.SSID() == current ? "<li class='onAir'>" : "<li>";
            htmlResult += current;
            if (WiFi.SSID() == current) {
              htmlResult += "<span>🛜 (";
              htmlResult += WiFi.localIP().toString();
              htmlResult += ")</span></li>";
            }
            else htmlResult += "<span></span></li>";
          }
        }
        file.close();
      }
    }
    preferences.end(); // Закрываем настройки
    return htmlResult;
  }
}

// Вычисление усреднённой температуры NTC-термистора
double getNTCtemperature() {
  uint32_t sumMV = 0;
  for (uint8_t i = 0; i < WATER_LEVEL_BUFFER_SIZE; i++) {
    sumMV += rawNTCvalues[i];
  }
  float readingMV = sumMV / WATER_LEVEL_BUFFER_SIZE; // Стабильное усреднённое значение
  readingMV = (readingMV * 10000.0 / 3300.0) / (1 - readingMV / 3300.0); // Это должно быть сопротивление термистора
  // Теперь переводим сопротивление в температуру по уравнению Стейнхарта с B-параметром:
  readingMV = readingMV / 10000.0; // (R/Ro)
  readingMV = log(readingMV); // ln(R/Ro)
  readingMV /= 3435.0; // 1/B * ln(R/Ro)
  readingMV += 1.0 / (25.0 + 273.15); // + (1/To)
  readingMV = 1.0 / readingMV; // инвертируем
  readingMV -= 273.15; // конвертируем в градусы по Цельсию
  return (double)readingMV;
}

void initParams() { // Актуализируем глобальные переменные, хранящие текстовые значения параметров:
  preferences.begin("gSettings", true); // Открываем настройки на чтение
  if (preferences.isKey("P1")) P1 = preferences.getString("P1");
  if (preferences.isKey("P2")) P2 = preferences.getString("P2");
  if (preferences.isKey("P3")) P3 = preferences.getString("P3");
  if (preferences.isKey("P4")) P4 = preferences.getString("P4");
  if (preferences.isKey("P5")) P5 = preferences.getString("P5");
  if (preferences.isKey("P6")) P6 = preferences.getString("P6");
  if (preferences.isKey("P7")) P7 = preferences.getString("P7");
  if (preferences.isKey("P8")) P8 = preferences.getString("P8");
  preferences.end(); // Закрываем настройки
  // Теперь в переменных P1...Pn либо текстовые значения параметров, либо значения зашитые в прошивке
}

void updateNativeParameterValues() {
  if (isParamterChanges[0] == true) { // Температура эспрессо
    setTemp = P1.toDouble(); // Берём значение из переменной, хранящей значение параметра...
    if (setTemp == 0) setTemp = ESPRESSO_TEMPERATURE; // ...если не распарсилось, будет величина по-умолчанию из прошивки
    isParamterChanges[0] == false;
  }
  if (isParamterChanges[1] == true) { // Температура пара
    steamTemperature = P2.toInt();
    if (steamTemperature == 0) steamTemperature = STEAM_TEMPERATURE;
    // Теперь обновляем производные:
    isParamterChanges[1] == false;
  }
  if (isParamterChanges[2] == true) { // Продолжительность открытия клапана сброса давления
    passValveOpenTime = P3.toInt();
    if (passValveOpenTime == 0) passValveOpenTime = PASS_VALVE_OPEN_TIME;
    isParamterChanges[2] == false;
  }
  if (isParamterChanges[3] == true) { // Таймаут автоотключения
    autoOffTimeout = P4.toInt();
    if (autoOffTimeout == 0) autoOffTimeout = AUTO_OFF_TIMEOUT;
    timerAlarm(autoOFFtimer, autoOffTimeout * 60000, false, 0); // Переустанавливаем время однократного срабатывания таймера в соответствии со значением параметра
    timerWrite(autoOFFtimer, 0); // Сбрасываем счётчик
    isParamterChanges[3] == false;
  }
  if (isParamterChanges[4] == true) { // Целевая масса напитка
    targetWeight = P5.toFloat(); // Если не распарсится, будет 0, т.е. масса напитка при проливе не учитывается
    isParamterChanges[4] == false;
  }
  if (isParamterChanges[5] == true) { // Начинать ли нагрев сразу после включения кофеварки
    beginHeatingOnPoweringOn = P6.toInt(); // Если не распарсится, будет false
    isParamterChanges[5] == false;
  }
  if (isParamterChanges[6] == true) { // SSID SoftAP
    SoftAP_SSID = P7;
    if (SoftAP_SSID == "") SoftAP_SSID = SOFT_AP_SSID;
    isParamterChanges[6] == false;
  }
  if (isParamterChanges[7] == true) { // пароль SoftAP
    SoftAP_password = P8;
    if (SoftAP_password == "") SoftAP_password = SOFT_AP_PASSWORD;
    isParamterChanges[7] == false;
  }
}
