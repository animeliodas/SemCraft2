# SemCraft 2 — Minecraft внутри Serious Sam Classic

Настоящий Minecraft Java 26.3 работает рядом с Serious Sam Classic (The First Encounter 1.05 и The Second
Encounter 1.07) и рисуется прямо в кадре Sam, с глубиной:

- **блоки Minecraft стоят в уровнях Sam** и прячутся за его стенами и колоннами, а сами закрывают то, что
  за ними;
- **мобы Minecraft ходят по полам Sam**: уровень Sam превращается в невидимую оболочку из блоков;
- **урон идёт в обе стороны**: зомби и скелеты бьют Сэма, а оружие Сэма (кольт, дробовики, миниган, ракеты,
  гранаты, лазер, пушка, нож) бьёт мобов Minecraft;
- **режим стройки (B)**: мышь и клавиши 1–9 переходят Minecraft, и блоки ставятся туда, куда смотрит прицел Сэма
  (до 48 блоков); **построенное твёрдое и в Sam**: Сэм и монстры упираются в стены из блоков и стоят на них
  (углы скруглены — так движок Sam сталкивает модели);
- **оружие Сэма против блоков**: пули упираются в блоки Minecraft (монстр за стеной цел), блок трескается и
  ломается с дропом (камень держит ~9 пуль, стекло бьётся с одной), выстрел в TNT поджигает его, ракеты, гранаты и
  ядра выбивают воронки, огнемёт (TSE) поджигает дерево;
- **взрывы Minecraft бросают Сэма**: TNT и криперы отталкивают его так же, как игрока Minecraft;
- **монстры Sam в Minecraft**: мобы Minecraft охотятся на монстров Sam, меч и TNT их ранят, а монстры Sam бьют
  мобов в ответ;
- **HUD Minecraft** (хотбар, сердца) поверх HUD Sam; сердца показывают здоровье Сэма;
- Minecraft подкрашивается под освещение уровня Sam.

Ни одна игра не переписана. Sam остаётся игрой, в которую вы играете: он управляет игроком и камерой, а
Minecraft следует за ним. Плагин в Sam и мод в Minecraft только переводят данные друг другу (контракт —
[docs/CONTRACT.md](docs/CONTRACT.md)).

> Сделано с помощью Claude Code (ИИ). Проверено в реальных играх: TFE 1.05 (уровень Hatshepsut) и TSE 1.07
> (Sierra de Chiapas), Windows 11, NVIDIA RTX 5080. Подробности проверки и грабли — в [MODLOG.md](MODLOG.md).

## Что нужно

- Serious Sam Classic: The First Encounter и/или The Second Encounter (Steam), режим **OpenGL** (по умолчанию).
- [Serious Sam Classics Patch 1.9.4](https://github.com/SamClassicPatch/SuperProject/releases/tag/1.9.4)
  в папке каждой игры (загрузчик плагинов; игра запускается через `Bin\SeriousSam_Custom.exe`).
- Minecraft Java Edition 26.3 (своя лицензия), Fabric Loader 0.19.5+ и Fabric API 0.161.0+26.3.
  Удобно отдельной сборкой CurseForge (у автора — инстанс `SemCraft`).
- Windows 10/11 x64.

## Установка

1. Плагин для Sam: скопировать `SemCraft2.dll` из `sam/dist/Release_TFE105` в
   `Serious Sam Classic The First Encounter\Bin\Plugins\`, из `sam/dist/Release_TSE107` — в
   `Serious Sam Classic The Second Encounter\Bin\Plugins\`.
2. Мод для Minecraft: `mc/build/libs/semcraft2-0.1.0.jar` положить в папку `mods` сборки рядом с Fabric API.
3. Или всё сразу: `python tools/install.py` (пути по умолчанию — как на машине автора; есть `--tfe`, `--tse`,
   `--mc`, `--uninstall`).

> Мод делает мир Minecraft выше (высота 2048, от −1024): это датапак внутри мода, он действует на **новые** миры
> всей сборки. Используйте для SemCraft отдельную сборку Minecraft.

## Как играть

1. Запустить Minecraft (сборку с модом). Он сам создаст и откроет пустой мир `semcraft2`.
2. Запустить Sam: `SemCraft2 TFE.bat` или `SemCraft2 TSE.bat` (или `Bin\SeriousSam_Custom.exe`).
3. Начать уровень. Окно Minecraft спрячется, и Minecraft появится внутри Sam. При выходе из Sam окно
   Minecraft вернётся.

Порядок запуска не важен: плагин сам переподключается раз в секунду.

### Управление

| Клавиша | Что делает |
|---|---|
| **B** | режим стройки вкл/выкл: оружие Сэма убирается, в руке предмет Minecraft |
| ЛКМ (в режиме стройки) | атака / ломать блок Minecraft |
| ПКМ (в режиме стройки) | использовать / поставить блок (спавн-яйцо, TNT, огниво) |
| 1–9 (в режиме стройки) | слот хотбара Minecraft |
| Q (в режиме стройки) | выбросить предмет |
| всё остальное | как в Serious Sam |

Хотбар: алмазный меч, трава, каменный кирпич, доски, стекло, TNT, огниво, яйца зомби и крипера.
Блоки не кончаются (пополняются раз в 10 секунд).

### Консоль Sam (`~`)

| Команда | Что делает |
|---|---|
| `sc_Status()` | состояние связи, кадров, оружия |
| `sc_Build()` | то же, что B |
| `sc_Spawn("zombie 5")` | мобы Minecraft там, куда смотрит прицел (любой id моба: `creeper 3`, `skeleton 2`, …) |
| `sc_Pillar(4)` | золотой столб в точке прицела (проверка совмещения) |
| `sc_SpawnSam("Werebull")` | монстр Sam в точке прицела (`Boneman`, `Werebull`, `Headman`, `Eyeman`, `Walker`, …; только одиночная игра) |
| `sc_Send("{\"t\":\"cmd\",\"c\":\"time set night\"}")` | любая команда сервера Minecraft |
| `sc_fLightMatch = 0.85` | насколько Minecraft берёт освещение Sam (0 — свой полдень) |
| `sc_iCompositeMode = 0/1/2` | 0 — с глубиной, 1 — Minecraft поверх всего (отладка), 2 — выключить вклейку |

## Если что-то не так

- **Minecraft не появляется**: `sc_Status()`. «waiting for Minecraft» — мод не запущен или порт 25610 занят;
  «frames: not attached» — Minecraft ещё не открыл мир.
- **Sam в режиме Direct3D**: вклейка работает только в OpenGL (меню Options → Video → Graphics API).
- **Мобы проваливаются или висят**: оболочка строится вокруг игрока чанками за пару секунд; двери и лифты Sam
  (подвижные браши) в оболочку пока не входят.
- Логи: `Serious Sam ...\SeriousSam_Custom.log` и `logs\latest.log` сборки Minecraft.

## Как это устроено

```
SeriousSam_Custom.exe (TFE/TSE) + Classics Patch          javaw (Minecraft 26.3 + Fabric)
  SemCraft2.dll                                              semcraft2
    камера (OnRenderView) ──── WebSocket 127.0.0.1:25610 ──▶   камера Minecraft = камера Sam
    уровень (браши, терраин) ── файл %TEMP%\SemCraft2\*.tri ─▶   оболочка из невидимых блоков высотой 1/16
    выстрелы, взрывы, снаряды ─────────────────────────────▶   урон мобам, взрывы, «пойманные» ракеты
    урон игроку, толчки ◀──────────────────────────────────    удары мобов, отдача взрывов
    твёрдые блоки (невидимые модели) ◀─────────────────────    поставленные / сломанные блоки
    вклейка (OpenGL, gl_FragDepth) ◀── shared memory Local\SemCraft2Frame ── цвет + глубина + HUD
```

- Плагин рисует кадр Minecraft полноэкранным квадом сразу после мира Sam. Шейдер переводит глубину Minecraft в
  шкалу глубины Sam (`glFrustum`, диапазон 0..0.9), и обычный depth-test Sam сам решает, что кого закрывает.
  Картинка Minecraft перепроецируется с камеры, для которой она отрисована, на текущую камеру Sam.
- Уровень Sam вокселизируется в Minecraft лениво, по чанкам вокруг игрока; каждый уровень получает свой регион
  мира (блоки, построенные в уровне, остаются там).
- Пули Sam живут один тик внутри кода оружия, поэтому выстрелы считаются по убыванию патронов
  (`CPlayerWeapons`), а ракеты, гранаты и ядра отслеживаются как сущности до взрыва.
- Блок Minecraft в Sam — `ModelHolder2` с моделью-кубом редактора, включённой как обычная модель, но с нулевой
  маской цвета: его не видно, а пули и взгляды монстров в него упираются. Коллизия моделей в SE1 — сферы, поэтому
  у стыка четырёх блоков остаётся щель: тонкая пуля (радиус 0.1 м) изредка проходит там насквозь.

## Сборка из исходников

- Плагин: VS 2022 Build Tools (v143, x86), SDK Classics Patch 1.9.4:
  `git clone --branch 1.9.4 --recurse-submodules https://github.com/SamClassicPatch/SuperProject.git ../_deps/SuperProject`,
  затем `sam/build.sh` (Git Bash).
- Мод: JDK 25, `cd mc && ./gradlew build` (тесты вокселизатора запускаются там же).
- Тест-стенд без мыши и клавиатуры: `tools/dev.py` (запуск игр, консольные команды Sam, скриншоты из плагина),
  `tools/test_shoot.py` (оракул «оружие Sam ранит мобов Minecraft»).

## Благодарности

- [Serious Sam Classics Patch](https://github.com/SamClassicPatch/SuperProject) (Dreamy Cecil) — загрузчик плагинов
  и SDK; [Serious Engine 1](https://github.com/Croteam-official/Serious-Engine) (Croteam) — исходники движка как
  справочник.
- [Fabric](https://fabricmc.net) (Loader, API, Loom) и [Java-WebSocket](https://github.com/TooTallNate/Java-WebSocket).
- Экспорт кадра Minecraft, мискины камеры и тумана, WebSocket-клиент — адаптированы из MIT-примера
  `minecraft-gta5-passthrough` проекта [universal-modder](https://github.com/rehan-remade/universal-modder).
- Идея passthrough-модов: SkyCraft (chasm), Portalcraft, Minecraft в GTA V.
- Код написан Claude Code (Anthropic) по запросу автора.

Serious Sam — Croteam / Devolver Digital, Minecraft — Mojang Studios / Microsoft. Это фанатский проект; файлы игр в
нём не распространяются.

## Лицензия

GNU GPL v2 ([LICENSE](LICENSE)): плагин собирается с SDK Classics Patch и заголовками Serious Engine 1 (GPL v2).
Части мода Minecraft, взятые из примера universal-modder, — MIT; тексты лицензий — в
[THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md).
