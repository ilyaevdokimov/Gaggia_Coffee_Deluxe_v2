// Перечисление состояний кофеварки
typedef enum {
  Wait, // Ожидание
  Pass, // Пролив
  Steam, // Пар
  SteamValve, // Кран пара
  Drain, // Дренаж (пролив + кран пара)
  Booster, // Бустер (пар + кран пара)
  Diagnostics // Диагностика
} State;

const char* stateStrings[] = {"Ожидание", "Пролив", "Пар", "Кран", "Дренаж", "Бустер", "Диагностика"}; // Текстовые описания состояний

// Перечисление источников изменения состояния кофеварки
typedef enum {
  HardPassButton, // Аппаратная кнопка пролива
  HardSteamButton, // Аппаратная кнопка пара
  SteamValveButton, // Кран пара
  SoftPassButtonOn, // Экранная кнопка пролива включена
  SoftPassButtonOff, // Экранная кнопка пролива выключена
  SoftSteamButtonOn, // Экранная кнопка пара включена
  SoftSteamButtonOff, // Экранная кнопка пара выключена
  None // Изменение состояния отсутствует
} StateChangeSource;
