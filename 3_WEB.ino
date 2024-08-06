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
  // Обработчики обращений к WEB-серверу

  server.on("/", HTTP_GET, [](AsyncWebServerRequest * request) {
    if (isEspressoHeatingOn) isAutoOFFneeded = true; // Принудительно выполняем процедуру автоотключения (только если нагрев был включен)
    request->send(LittleFS, "/index.html");
  });

  server.serveStatic("/s1.css", LittleFS, "/s1.css");
  
  // Временно!!!
  //server.serveStatic("/knownNetworks.txt", LittleFS, "/knownNetworks.txt");

  server.serveStatic("/stngsSSE.html", LittleFS, "/stngsSSE.html").setTemplateProcessor(processor);

  server.on("/cpSSE.html", HTTP_GET, [](AsyncWebServerRequest * request) {
    // Чтобы сменился режим и запустились задачи нагрева
    isEspressoHeatingOn = true; // Включаем нагрев
    // Реинициализируем фэйдинг:
    isFadeEnded = false;
    isFadeOn = true;
    ledcFadeWithInterrupt(TANK_LED, LEDC_TARGET_DUTY, LEDC_START_DUTY, LEDC_FADE_TIME, LED_FADE_ISR); // Запускаем фэйдинг
    ledcWrite(WORKSPACE_LED, LEDC_TARGET_DUTY); // Врубаем на полную освещение в рабочей зоне

    timerStop(autoOFFtimer);
    timerWrite(autoOFFtimer, 0); // Сбрасываем счётчик
    timerRestart(autoOFFtimer); // Перезапускаем таймер (на случай, если он уже срабатывал - он ведь однократный)
    timerStart(autoOFFtimer); // Запускаем таймер

    currentState = SteamValve; // Устанавливаем какой-нибудь странный режим, чтобы задача отслеживания могла заменить его на актуальный
    isStateChanged = true;
    request->send(LittleFS, "/cpSSE.html");
  });

  server.on("/updatedata", HTTP_GET, [] (AsyncWebServerRequest * request) {
    String controlID;
    String controlValue;
    // Получаем значение из GET-запроса <ESP_IP>/updatedata?output=<controlID>&state=<controlValue>
    if (request->hasParam("output") && request->hasParam("state")) {
      controlID = request->getParam("output")->value();
      controlValue = request->getParam("state")->value();
    }
    else { // Непонятно, зачем нам это может пригодиться...
      controlID = "No message sent";
      controlValue = "No message sent";
    }

    if (controlID == "btnValve") digitalWrite(PASS_VALVE, controlValue == "true");
    else if (controlID == "btnDummyPass") {
      /*
        preferences.begin("gSettings", false);
        preferences.putString("P5", "0.0");
        preferences.putString("P6", "1");
        preferences.putString("P7", "MyGaggia");
        preferences.putString("P8", "MySecretString");
        preferences.end(); // Закрываем настройки
      */
      scale.tare(); // Сбрасываем значение на весах
    }
    else if (controlID == "btnLivePass") {

    }
    request->send(200, "text/plain", "OK");
  });

  // Обработчик событий Web-сервера для отправки страницам-подписчикам
  events.onConnect([](AsyncEventSourceClient * client) {
    if (client->lastId()) { }
    client->send("hello!", NULL, millis(), 5000);
  });

  server.addHandler(&events);
  if (isSoftAP) server.addHandler(new CaptiveRequestHandler()).setFilter(ON_AP_FILTER); // Обработчик действителен только в режиме AP

  server.on("/updatesettings", HTTP_GET, [] (AsyncWebServerRequest * request) { // Обновление параметров со страницы настроек
    // /updatesettings?p1=112&p2=120&p3=9&p4=10

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

  server.on("/updateWiFi", HTTP_GET, [] (AsyncWebServerRequest * request) { // Обновление списка WiFi-сетей со страницы настроек
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
            file.close(); // Не уверен, что это безопасно в случае, если файл не открылся
          }
          // Если строки ещё нет, добавляем её:
          if (alreadyExistInFile == false) {
            file = LittleFS.open("/knownNetworks.txt", FILE_APPEND); // Пытаемся его открыть на дозапись
            if (file) { // Если удалось, дописываем в конец новое значение SSID
              file.print(newSSID);
              file.print("\n");
            }
            file.close(); // Не уверен, что это безопасно в случае, если файл не открылся
          }
        }
        // Теперь обновляем настойки:
        preferences.begin("knownNetworks", false);
        preferences.putString(newSSID.c_str(), newPassword);
        preferences.end(); // Закрываем настройки
        request->send(200, "text/plain", "OK");
      }
    }
  });

  server.on("/deleteWiFi", HTTP_GET, [] (AsyncWebServerRequest * request) { // Обновление списка WiFi-сетей со страницы настроек
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

  ElegantOTA.begin(&server);    // Start ElegantOTA
  server.begin();
}
