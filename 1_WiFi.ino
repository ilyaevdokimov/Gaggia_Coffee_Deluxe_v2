void startWiFi() { // Запуск собственной точки доступа Wi-Fi и подключение (при возможности) к существующей Wi-Fi сети
  WiFi.hostname("GAGGIA_Coffee-" + String((uint32_t)(ESP.getEfuseMac() >> 32), HEX)); // Формируем имя хоста
  WiFi.mode(WIFI_AP_STA); // Работаем в смешанном режиме (роутер + собственная точка доступа)
  makeSoftAP();
  tryAvailableNetworks();
}

bool tryAvailableNetworks() { // Попытка соединения с какой-либо из сетей, реквизиты для доступа к которым сохранены пользователем во флеш-памяти контроллера
  String ssid, password;
  bool connectionResult = false;

  preferences.begin("knownNetworks", false);
  // Прежде чем перебирать все доступные сети, попробуем подключиться к последней использованной
  if (preferences.isKey("LastUsedSSID")) {
    ssid = preferences.getString("LastUsedSSID"); // Берём имя последней использованной сети
    if (preferences.isKey(ssid.c_str())) { // Если в сохранённых настройках есть такая сеть...
      password = preferences.getString(ssid.c_str(), ""); // ...берём её пароль...
      WiFi.begin(ssid.c_str(), password.c_str()); // ...и пробуем подконнектиться...
      if (WiFi.waitForConnectResult(13000) == WL_CONNECTED) { // ...в течение 13 секунд
        connectionResult = true; // при успехе выходим из цикла и возвращаем положительный результат
      }
    }
  }

  if (connectionResult != true) { // Если последней использованной сети нет, или к ней не удалось подключиться
    int n = WiFi.scanNetworks(); // Придётся использовать поиск доступных сетей
    for (uint8_t i = 0; i < n; ++i) {
      ssid = WiFi.SSID(i); // Перебираем все найденные сети
      if (preferences.isKey(ssid.c_str())) { // Если в сохранённых настройках есть такая сеть...
        password = preferences.getString(ssid.c_str(), ""); // ...берём её пароль...
        WiFi.begin(ssid.c_str(), password.c_str()); // ...и пробуем подконнектиться...
        if (WiFi.waitForConnectResult(13000) == WL_CONNECTED) { // ...в течение 13 секунд
          connectionResult = true; // при успехе выходим из цикла и возвращаем положительный результат
          preferences.putString("LastUsedSSID", ssid);
          break;
        }
      } // В противном случае продолжаем попытки, пока не кончится список сетей
    }
  }
  preferences.end(); // Закрываем настройки
  WiFi.scanDelete(); // Удаляем список найденных сетей
  return connectionResult;
}

void makeSoftAP() { // Создание собственной точки доступа с именем и паролем из нативных значений соответствующих параметров
  WiFi.softAP(SoftAP_SSID, SoftAP_password);
  dnsServer.start(53, "*", WiFi.softAPIP()); // Для обслуживания Captive Portal
}
