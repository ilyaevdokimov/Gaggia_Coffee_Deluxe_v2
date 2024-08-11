class CaptiveRequestHandler : public AsyncWebHandler { // Обработчик для Captive Portal
  public:
    CaptiveRequestHandler() {}
    virtual ~CaptiveRequestHandler() {}

    bool canHandle(AsyncWebServerRequest *request) {
      return true;
    }

    void handleRequest(AsyncWebServerRequest *request) {
      request->send(LittleFS, "/");
    }
};

void startWEBServer() { // Запуск HTTP-сервера с обработчиками чтения и загрузки файла

  // Обработчики обращений к WEB-серверу:

  server.on("/", HTTP_GET, [](AsyncWebServerRequest * request) { // Главная
    if (isEspressoHeatingOn) isAutoOFFneeded = true; // Принудительно выполняем процедуру автоотключения (только если нагрев был включен)
    currentState = Wait; // Всегда устанавливаем режим ожидания, вне зависимости от того, что там нажато на кофеварке
    newState = currentState;
    request->send(LittleFS, "/index.html");
  });

  server.serveStatic("/s1.css", LittleFS, "/s1.css"); // Стили

  server.serveStatic("/stngsSSE.html", LittleFS, "/stngsSSE.html").setTemplateProcessor(processor); // Настройки

  server.on("/cpSSE.html", HTTP_GET, [](AsyncWebServerRequest * request) { // Панель управления
    isEspressoHeatingOn = true; // Чтобы сменился режим и начался нагрев
    // Реинициализируем фэйдинг:
    isFadeEnded = false;
    isFadeOn = true;
    ledcFadeWithInterrupt(TANK_LED, LEDC_TARGET_DUTY, LEDC_START_DUTY, LEDC_FADE_TIME, LED_FADE_ISR); // Запускаем фэйдинг
    ledcWrite(WORKSPACE_LED, LEDC_TARGET_DUTY); // Врубаем на полную освещение в рабочей зоне

    timerStop(autoOFFtimer); // Отсанвливаем таймер автоотключения
    timerWrite(autoOFFtimer, 0); // Сбрасываем счётчик
    timerRestart(autoOFFtimer); // Перезапускаем таймер (на случай, если он уже срабатывал - он ведь однократный)
    timerStart(autoOFFtimer); // Запускаем таймер

    currentState = SteamValve; // Устанавливаем какой-нибудь странный режим, чтобы задача отслеживания могла заменить его на актуальный
    isStateChanged = true;
    request->send(LittleFS, "/cpSSE.html");
  });

  server.on("/check.html", HTTP_GET, [](AsyncWebServerRequest * request) { // Диагностика
    if (isEspressoHeatingOn) isAutoOFFneeded = true; // Принудительно выполняем процедуру автоотключения (только если нагрев был включен)
    currentState = Diagnostics; // Устанавливаем режим диагностики
    newState = currentState;
    request->send(LittleFS, "/check.html");
  });

  server.on("/updatedata", HTTP_GET, [] (AsyncWebServerRequest * request) { // GET-запрос на изменение состояние элемента управления на странице
    String controlID;
    String controlValue;
    String json = "";
    // Получаем значение из GET-запроса <ESP_IP>/updatedata?output=<controlID>&state=<controlValue>
    if (request->hasParam("output") && request->hasParam("state")) { // Пытаемся прочесть параметры
      controlID = request->getParam("output")->value();
      controlValue = request->getParam("state")->value();
    }
    else json = "[{\"error\": \"Bad request\",\"message\": \"Parameters missing\",}]"; // Если не удаётся, формируем сообщение об их отсутствии

    if (json != "") request->send(400, "application/json", json); // Отсылаем сообщение об ошибке в случае отсутствия нужных параметров
    else {
      if (controlID == "btnSteam") { // Программное (экранной кнопкой) переключение режима пара
        if (currentState != checkState(!digitalRead(PASS_BUTTON), controlValue == "true" ? true : false, !digitalRead(STEAM_VALVE_BUTTON), false)) changeState();
      }
      else if (controlID == "btnLivePass") { // Программное (экранной кнопкой) переключение пролива
        if (currentState == Diagnostics) digitalWrite(PUMP, controlValue == "true" ? HIGH : LOW); // В режиме диагностики просто переключаем состояние помпы, тут всё просто
        else { // Во всех остальных режимах вычисляем новое состояние и в случае несовпадения сразу же изменяем - мы же не в прерывании...
          if (currentState != checkState(controlValue == "true" ? true : false, !digitalRead(STEAM_BUTTON), !digitalRead(STEAM_VALVE_BUTTON), false)) changeState();
        }
      }
      else if (controlID == "btnTare") { // Тарируем весы
        scale.tare(1); // Сбрасываем значение на весах за 1 проход, чтобы побыстрее (при значении по-умолчанию выполняется больше секунды!)
      }
      else if (controlID == "btnPass") { // Проверяем помпу
        if (currentState == Diagnostics) digitalWrite(PUMP, controlValue == "true" ? HIGH : LOW);
      }
      else if (controlID == "btnByPass") { // Проверяем клапан
        if (currentState == Diagnostics) digitalWrite(PASS_VALVE, controlValue == "true" ? HIGH : LOW);
      }
      else if (controlID == "btnBuzzer") { // Проверяем динамик
        if (currentState == Diagnostics) digitalWrite(SOUND_INDICATION, controlValue == "true" ? LOW : HIGH);
      }
      else if (controlID == "btnHeating") { // Тестируем нагрев (осторожно! клнтроля температуры нет, можно перегреть!)
        if (currentState == Diagnostics) digitalWrite(HEATING, controlValue == "true" ? temperature < 150 : LOW); // Хоть какая-то (150 градусов) защита от перегрева
      }
      else if (controlID == "btnTankerLight") { // Проеряем работу подстветки танкера с водой
        ledcWrite(TANK_LED, controlValue == "true" ? LEDC_TARGET_DUTY : 0);
      }
      else if (controlID == "btnWorkspaceLight") { // Проеряем работу подстветки рабочей области
        ledcWrite(WORKSPACE_LED, controlValue == "true" ? LEDC_TARGET_DUTY : 0);
      }
      request->send(200, "text/plain", "OK");
    }
  });

  // Обработчик событий Web-сервера для отправки страницам-подписчикам
  events.onConnect([](AsyncEventSourceClient * client) {
    if (client->lastId()) { }
    client->send("hello!", NULL, millis(), 5000);
  });

  server.addHandler(&events);
  if (isSoftAP) server.addHandler(new CaptiveRequestHandler()).setFilter(ON_AP_FILTER); // Обработчик CaptivePortal. Действителен только в режиме AP

  server.on("/updatesettings", HTTP_GET, [] (AsyncWebServerRequest * request) { // Обновление параметров со страницы настроек
    preferences.begin("gSettings", false);
    // Получаем данные из GET-запроса <ESP_IP>/updatesettings?P1=<значение>&P2=<значение> и т.д.
    if (request->hasParam("p1")) { // Если в запросе на изменение был указан такой параметр...
      String newP1 = request->getParam("p1")->value(); // ...получаем его значение
      if (newP1 != P1) { // Если новое значение отличается от старого...
        preferences.putString("P1", newP1); // ...записываем его в параметр...
        P1 = newP1; // ...присваиваем глобальной переменной новое значение...
        isParamterChanges[0] = true; // ...и устанваливаем флаг изменения для этого параметра
      }
    }
    // Далее аналогично:
    if (request->hasParam("p2")) {
      String newP2 = request->getParam("p2")->value();
      if (newP2 != P2) {
        preferences.putString("P2", newP2);
        P2 = newP2;
        isParamterChanges[1] = true;
      }
    }
    if (request->hasParam("p3")) {
      String newP3 = request->getParam("p3")->value();
      if (newP3 != P3) {
        preferences.putString("P3", newP3);
        P3 = newP3;
        isParamterChanges[2] = true;
      }
    }
    if (request->hasParam("p4")) {
      String newP4 = request->getParam("p4")->value();
      if (newP4 != P4) {
        preferences.putString("P4", newP4);
        P4 = newP4;
        isParamterChanges[3] = true;
      }
    }
    if (request->hasParam("p5")) {
      float wgth = (request->getParam("p5")->value()).toFloat(); // Если не распарсится, будет 0, т.е. "вес не учитывать"
      String newP5 = String(wgth);
      if (newP5 != P5) {
        preferences.putString("P5", newP5);
        P5 = newP5;
        isParamterChanges[4] = true;
      }
    }
    if (request->hasParam("p6")) {
      String newP6 = request->getParam("p6")->value();
      if (newP6 != P6) {
        preferences.putString("P6", newP6);
        P6 = newP6;
        isParamterChanges[5] = true;
      }
    }
    if (request->hasParam("p7")) {
      String newP7 = request->getParam("p7")->value();
      if (newP7 != P7) {
        preferences.putString("P7", newP7);
        P7 = newP7;
        isParamterChanges[6] = true;
      }
    }
    if (request->hasParam("p8")) {
      String newP8 = request->getParam("p8")->value();
      bool mustChange = SoftAP_SSID != P7; // Если имя точки доступа изменилось, пароль нужно изменить обязательно, каким бы он ни был (даже заглушкой 12345678)!
      if (newP8 != "12345678" && newP8 != P8) mustChange = true; // Если прилетевший пароль не равен заглушке и отличается от текущего, его тоже нужно заменить
      if (mustChange) {
        preferences.putString("P8", newP8);
        P8 = newP8;
        isParamterChanges[7] = true;
      }
    }
    preferences.end(); // Закрываем настройки

    updateNativeParameterValues(); // Обновляем нативные значения параметров

    request->send(200, "text/plain", "OK");
  });

  server.on("/updateWiFi", HTTP_GET, [] (AsyncWebServerRequest * request) { // Обновление списка сохранённых WiFi-сетей со страницы настроек
    // Получаем данные из GET-запроса <ESP_IP>/updateWiFi?P1=<значение>&P2=<значение>
    if (request->hasParam("p1") && request->hasParam("p2")) { // Если в запросе на изменение были указаны SSID и пароль...
      String newSSID = request->getParam("p1")->value(); // ...получаем значение SSID
      String newPassword = request->getParam("p2")->value(); // ...получаем значение пароля
      newSSID.trim();
      newPassword.trim();

      if (newSSID.length() > 1 && newPassword.length() > 7) { // Валидация длины
        bool alreadyExistInFile = false; // Существует ли такая сеть уже в файле настроек сетей /knownNetworks.txt
        // Прверяем наличие новой сети в файле настроек:
        if (LittleFS.exists("/knownNetworks.txt")) { // Если файл настроек существует
          File file = LittleFS.open("/knownNetworks.txt"); // Пытаемся его открыть на чтение
          if (file) { // Если удалось
            while (file.available()) {
              String current = file.readStringUntil('\n'); // Построчно читаем его содержимое
              if (newSSID == current) alreadyExistInFile = true; // И сравниваем с переданным именем сети
            }
            file.close();
          }
          // Если строки ещё нет, добавляем её:
          if (alreadyExistInFile == false) {
            file = LittleFS.open("/knownNetworks.txt", FILE_APPEND); // Пытаемся его открыть на дозапись
            if (file) { // Если удалось, дописываем в конец новое значение SSID
              file.print(newSSID);
              file.print("\n");
            }
            file.close();
          }
        }
        // Теперь обновляем настойки:
        preferences.begin("knownNetworks", false);
        preferences.putString(newSSID.c_str(), newPassword);
        preferences.end(); // Закрываем настройки
        request->send(200, "text/plain", "OK");
      }
      // Тут бы надо отправить ответ об ошибке валидации длины пароля
    }
  });

  server.on("/deleteWiFi", HTTP_GET, [] (AsyncWebServerRequest * request) { // Удаление WiFi-сети из списка сохранённых со страницы настроек
    if (request->hasParam("p1")) {
      String SSID2delete = request->getParam("p1")->value() + "\n";
      if (LittleFS.exists("/knownNetworks.txt")) { // Если файл настроек существует
        String content = "";
        File file = LittleFS.open("/knownNetworks.txt"); // Пытаемся его открыть на чтение
        if (file) content = file.readString(); // Если удалось, читаем всё его содержимое
        file.close();
        content.replace(SSID2delete, ""); // Удаляем подстроку с именем WiFi-сети
        file = LittleFS.open("/knownNetworks.txt", FILE_WRITE); // Пытаемся его открыть на запись
        if (file) file.print(content); // Если удалось, записываем в него новое содержимое
        file.close();
      }
      // Теперь обновляем настойки:
      preferences.begin("knownNetworks", false);
      if (preferences.isKey(SSID2delete.c_str())) preferences.remove(SSID2delete.c_str());
      preferences.end(); // Закрываем настройки
      request->send(200, "text/plain", SSID2delete);
    }
  });

  ElegantOTA.begin(&server); // Запуск сервиса обновления по воздуху ElegantOTA
  server.begin(); // Запуск сервера
}
