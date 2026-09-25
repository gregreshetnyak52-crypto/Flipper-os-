# FlipperOS

Кастомная прошивка для [Flipper Zero](https://flipperzero.one) на базе
[Unleashed](https://github.com/DarkFlippers/unleashed-firmware) (релиз
`unlshd-093`). Внутри всё, что есть в Unleashed, плюс собственное приложение
**Flipper OS Toolkit** прямо в главном меню.

Что добавлено поверх Unleashed:

- **Пункт «Flipper OS» в главном меню** с анимированной иконкой-чипом.
- **Flipper OS Toolkit v1.1** входит в прошивку (`apps/Tools/flipper_os.fap`).
- **Брендинг**: прошивка называется `FlipperOS`, файлы сборки — `flipperos-<версия>`.
- **CI**: GitHub Actions собирает пакет обновления на каждый пуш, а для тега
  `v*` публикует релиз.

## Flipper OS Toolkit

| Модуль | Что делает | Управление |
|---|---|---|
| **Stopwatch** | Секундомер с сотыми долями и двумя последними кругами | OK — старт/стоп, ← — круг (идёт) / сброс (стоит) |
| **Timer** | Обратный отсчёт до 99:59 с прогресс-баром и сигналом (звук, вибро, LED) | ←/→ — минуты/секунды, ↑/↓ — значение, OK — старт/пауза, ← — сброс, любая кнопка глушит сигнал |
| **Flashlight** | Белый светодиод: постоянно, стробоскоп или маяк; 4 уровня яркости; подсветка экрана не гаснет | OK — вкл/выкл, ↑/↓ — яркость, ←/→ — режим |
| **Dice Roller** | От 1 до 4 костей d4…d100 с анимацией и вибро | ↑/↓ — грани, ←/→ — количество, OK — бросок |
| **Morse Beacon** | Сообщение азбукой Морзе через LED, LED+вибро или LED+звук | ←/→ — сообщение, ↑ — выход, ↓ — повтор, OK — старт/стоп |
| **Snake** | «Змейка» 32×14 с ускорением и рекордом | Стрелки — поворот, OK — старт/пауза |
| **Tally Counter** | Счётчик с шагом 1…100 | OK — плюс, ← — минус, удержание ← — сброс, ↑/↓ — шаг |
| **System Info** | Имя, заряд, напряжение, ток и температура АКБ, свободная память, аптайм, дата и время | — |

Рекорд в «Змейке», значение счётчика, выбор костей, настройки Морзе, фонарика
и таймера сохраняются на SD-карте в `apps_data/flipper_os/settings.dat`.

Приложение совместимо и с официальной прошивкой: там оно ставится как обычный
`.fap` в **Apps → Tools**.

## Установка прошивки

1. Скачайте `flipper-z-f7-update-flipperos-*.tgz` из артефактов workflow
   **Build FlipperOS firmware** (вкладка *Actions*) или из *Releases*.
2. В [qFlipper](https://flipperzero.one/update) нажмите
   **Install from file** и выберите этот `.tgz`.

   Без компьютера: распакуйте архив в `update/` на SD-карте и на Flipper
   откройте `update/f7-update-flipperos-*/update.fuf` через **Archive → Run in App**.

Вернуться на официальную прошивку или Unleashed можно тем же способом.

## Сборка

Прошивка (нужны Linux/macOS, git и Python 3; тулчейн `fbt` скачает сам):

```sh
git clone https://github.com/gregreshetnyak52-crypto/flipper-os-
cd flipper-os-
firmware/build.sh                  # → firmware/unleashed/dist/f7-C/*.tgz
firmware/build.sh flash_usb_full   # собрать и сразу прошить по USB
```

Только приложение, под любую прошивку, через
[ufbt](https://github.com/flipperdevices/flipperzero-ufbt):

```sh
pip install --upgrade ufbt
cd flipper_os
ufbt            # → dist/flipper_os.fap
ufbt launch     # собрать, загрузить и запустить по USB
```

## Как устроен репозиторий

```
flipper_os/                 приложение Flipper OS Toolkit
├── application.fam         манифест (MENUEXTERNAL: пункт главного меню)
├── flipper_os.c            точка входа, ViewDispatcher, меню
├── settings.c/.h           сохранение состояния на SD
└── modules/                по одному View на модуль
firmware/
├── unleashed/              git-субмодуль Unleashed, закреплён на unlshd-093
├── overlay/                файлы, копируемые поверх Unleashed
│   ├── fbt_options_local.py            имя прошивки и состав меню
│   └── assets/icons/MainMenu/FlipperOS_14/   иконка главного меню
├── patches/                *.patch к исходникам Unleashed (см. README там)
└── build.sh                сброс → патчи → overlay → приложение → ./fbt
```

Unleashed не копируется в репозиторий целиком, а подключён субмодулем.
Поэтому переход на новую версию сводится к одной команде:

```sh
cd firmware/unleashed && git fetch --depth 1 origin tag unlshd-XXX && git checkout unlshd-XXX
cd ../.. && git add firmware/unleashed && git commit -m "Bump Unleashed to unlshd-XXX"
```

Unleashed распространяется под лицензией GPL-3.0, поэтому собранная прошивка FlipperOS подпадает под ту же лицензию.
