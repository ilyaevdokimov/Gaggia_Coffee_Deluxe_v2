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
    }
    vTaskDelay(PULSEWIDTH);
  }
}
