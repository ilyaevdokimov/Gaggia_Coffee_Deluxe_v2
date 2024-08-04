void RefreshControlPanel(void *pvParameters) { // Вывод в WEB-интерфейс обновлённых значений
  uint8_t const espressoLowerTreshold = 90; // Нихний предел температуры группы для OK-сигнализации
  uint8_t const espressoUpperTreshold = 105; // Верхний предел температуры группы для OK-сигнализации
  uint8_t loopCounter = 0; // Счётчик для обслуживания буфера значений дальномера, изменяется в пределах 0 ... WATER_LEVEL_BUFFER_SIZE

  while (1) {
    uint8_t steamLowerTreshold = steamTemperature - 4; // Нижний предел температуры пара для индикации
    uint8_t steamUpperTreshold = steamTemperature + 10; // Верхний предел температуры пара для индикации

    // Инкрементируем служебные переменные:
    if (currentState == Booster) { // Временные интервалы не точные, но для такой цели сойдёт
      if (boosterTimer < BOOSTER_SWAP_TIMEOUT) boosterTimer += PAGE_REFRESH_INTERVAL;
      else isPumpTimeOut = false;
      if (boosterSwapTimer < BOOSTER_SWAP_INTERVAL) boosterSwapTimer += PAGE_REFRESH_INTERVAL;
    }

    if (waterLevel <= 0) changeState(); // При недостаточном уровне воды выключаем помпу и сигнализируем звуком и светом о низком уровне
    else passTime = passTimeInMillis / 1000; // Если помпа работает, считаем время пролива

    // Подготавливаем и отсылаем страничке значения с датчиков:
    temperature = getNTCtemperature(); // Обновляем значение глобальной переменной, чтобы ПИД мог брать оттуда актуальные данные
    boilerTemperature = kTCboiler.getTempInt(); // Температура бойлера, отображаемая в интерфейсе
    groupTemperature = kTCgroup.getTempInt(); // Температура группы
    waterLevel = getWaterLevel(loopCounter++); // Актуализируем уровень воды в танкере (это занимает порядка 15 мс):
    if (loopCounter >= WATER_LEVEL_BUFFER_SIZE) loopCounter = 0; // Сбрасываем счётчик при переполнении
    // Сигнализируем о попадании температуры в заданный диапазон при нахождении:
    uint16_t targetTemperature = (boilerTemperature + groupTemperature) / 2; // Усреднённая температура бойлера и группы
    // в режиме пара или бустера
    if (currentState == Steam || currentState == Booster) isTemperatureReached = temperature > steamLowerTreshold && temperature < steamUpperTreshold;
    // в режиме эспрессо
    else if (currentState == Wait  || currentState == Pass  || currentState == SteamValve) isTemperatureReached = groupTemperature > espressoLowerTreshold && groupTemperature < espressoUpperTreshold;
    // Формируем пакет данных для отправки на страницу по SSE
    String ss;
    //ss.reserve(50);
    makeSendString(ss); // Пихаем все значения в одну строку через разделитель
    events.send(ss.c_str(), "values", millis()); // Отправляем данные на страницы, подписанные на наше событие SSE
    vTaskDelay(PAGE_REFRESH_INTERVAL);
  }
}

void HeaterControl(void *pvParameters) { // Единая задача для управления всеми видами нагрева
  // Вспомогательные переменные ПИД-регулятора:
  uint64_t timeAtTemp;
  bool relayControl;
  AutoPIDRelay espressoPID(&temperature, &setTemp, &relayControl, 5000, .53, .0003, 0); // Переменные temperature и steamTemperature являются глобальными, т.к. используются в других задачах
  espressoPID.setBangBang(2); // Ускорение нагрева до +- 2 градусов
  espressoPID.setTimeStep(PULSEWIDTH); // Задержка регулирования

  while (1) {
    if (isAutoOFFneeded) isEspressoHeatingOn = false; // Если сработал таймер автоотключения, переводим кофеварку в экономичный режим
    if (isEspressoHeatingOn) { // Если нагрев включён...
      // ...действуем в зависимости от состояния:
      if (currentState == Steam) { // Режим пара
        // Деактивируем ПИД
        timeAtTemp = 0;
        espressoPID.stop();
        // Начинаем регулировать в режиме "ВКЛ-ВЫКЛ":
        digitalWrite(HEATING, temperature <= steamTemperature); // Греемся вплоть до достижения установленной температуры парообразования
        //vTaskDelay(PULSEWIDTH * 2);
      }
      else if (currentState == Booster) { // Режим бустера пара
        // Начинаем регулировать в режиме "ВКЛ-ВЫКЛ":
        digitalWrite(HEATING, temperature <= steamTemperature + 10); // врубаем нагрев на полную (ну, не совсем... чтобы выше steamTemperature + 10 не поднялось)
        if (!isPumpTimeOut) { // Если таймаут задержки включения циклической подкачки истёк, начинаем подкачивать микрообъёмы воды в бойлер:
          if (boosterSwapTimer >= BOOSTER_SWAP_INTERVAL) {
            digitalWrite(PUMP, HIGH);
            vTaskDelay(130);
            digitalWrite(PUMP, LOW);
            boosterSwapTimer = 0;
          }
        }
      }
      else { // Режим эспрессо
        // Начинаем регулировать
        if (currentState == Pass) { // Если начали пролив, врубаем нагрев на постоянку, ибо стынет не по-детски
          timeAtTemp = 0;
          espressoPID.stop();
          digitalWrite(HEATING, groupTemperature < 95); // Пределом является температура группы, равная температуре идеального пролива эспрессо
        }
        else { // Если поддерживаем температуру в режиме ожилания, регулируем ПИД'ом
          // Целевую температуру меняем в зависимости от прогрева группы:
          switch (groupTemperature) {
            case 0 ... 80:
              setTemp = P1.toDouble() + 3.0; // Увеличиваем целевую температуру для более скоростного нагрева
              break;
            case 81 ... 85:
              setTemp = P1.toDouble(); // Греемся в нормальном темпе
              break;
            case 86 ... 94:
              setTemp = P1.toDouble() - 3.0; // Притормарживаем нагрев
              break;
            default: // Держим сотню
              setTemp = 100.0;
              break;
          }
          espressoPID.run();
          digitalWrite(HEATING, relayControl);
          if (espressoPID.atSetPoint(2)) {
            if (!timeAtTemp) timeAtTemp = millis();
          }
          else timeAtTemp = 0;
        }
        //vTaskDelay(PULSEWIDTH);
      }
    }
    else {
      if (isAutoOFFneeded) {
        timerStop(autoOFFtimer); // Выключаем таймер автоотключения
        timerWrite(autoOFFtimer, 0); // Сбрасываем счётчик
        // Выключаем нагрев:
        timeAtTemp = 0;
        espressoPID.stop();
        digitalWrite(HEATING, LOW);
        // Сигнализируем об истечении таймаута автоотключения
        digitalWrite(SOUND_INDICATION, LOW);
        vTaskDelay(600);
        digitalWrite(SOUND_INDICATION, HIGH);
        vTaskDelay(300);
        digitalWrite(SOUND_INDICATION, LOW);
        vTaskDelay(100);
        digitalWrite(SOUND_INDICATION, HIGH);
        vTaskDelay(1);
        isAutoOFFneeded = false; // Сбрасываем флаг необходимости автоотключения
      }
      //vTaskDelay(PULSEWIDTH);
    }
    vTaskDelay(PULSEWIDTH);
  }
}
