# SemCraft 2 — MODLOG

Журнал работы. Всё, чего нет здесь, теряется при сжатии контекста.

## Цель
Настоящий Minecraft внутри Serious Sam Classic (TFE 1.05 / TSE 1.07). Игрок играет в Sam, камера Sam.
Скрытый MC 26.3 рендерит по камере Sam; его кадр (цвет + depth + HUD/рука) вклеивается в кадр Sam с
окклюзией. Блоки ставятся/ломаются в уровнях Sam, мобы MC ходят по полам Sam, урон в обе стороны.
Готово = работает в реальной игре + клип 20-45 с.

## Пути
- Работа: `%USERPROFILE%\.local\bin\SemCraft2`
  - `Serious Sam Classic The First Encounter\` — копия Steam TFE 1.05 + Classics Patch 1.9.4
  - `Serious Sam Classic The Second Encounter\` — копия Steam TSE 1.07 + Classics Patch 1.9.4 (+ мод Warped в Mods)
  - `semcraft\` — этот репо (git)
  - `_deps\SuperProject` — SDK Classics Patch, тег 1.9.4 (b7f2ff4, API 2a98189, Includes 94ff8cd)
  - `_deps\ClassicsPatch\*.zip` — релиз 1.9.4 (sha256 сверены с GitHub release assets)
- MC: `%USERPROFILE%\curseforge\minecraft\Instances\SemCraft` (MC 26.3, Fabric loader 0.19.5, CurseForge)
  - в mods: `fabric-api.jar`, `semcraft-0.1.0.jar` (старая попытка — заменить/убрать при установке нового мода)
- Старая попытка (только знания): `%USERPROFILE%\.local\bin\SemCraft\SemCraftMod` (docs/*.md, git)
- SE1 исходники 1.10 (справка): `%USERPROFILE%\.local\bin\SeriousEngine-src\Sources`

## Бэкапы (restore path)
- `um backup restore semcraft2-tfe-bin` / `semcraft2-tse-bin` — Bin до Classics Patch
  (`--clean` удалит добавленные файлы). Также `semcraft2-tfe-mods`, `semcraft2-tse-mods`.
- Снимки: `%USERPROFILE%\.universal-modder\backups\semcraft2-*`

## Маршрут
Passthrough (как Portalcraft / MC-в-GTA V из базы знаний). Причины:
- нужен живой MC (мобы, блоки, редстоун) — порт контента не даёт;
- прошлая попытка (MC водит игрока Sam) застряла на камере/физике — здесь Sam владеет игроком и камерой,
  MC только следует (Portalcraft-схема, доказана).
Стороны:
- **Sam**: плагин Classics Patch (`SemCraft2.dll`, C++ v143, x86) — камера в `OnRenderView`, вклейка кадра
  MC сырым OpenGL (шейдер пишет `gl_FragDepth` в шкале Sam → depth-test против depth-буфера Sam),
  экспорт геометрии уровня, ввод, урон.
- **MC**: Fabric-мод `semcraft2` (Java 25, Loom) на основе MIT-примера universal-modder
  `examples/minecraft-gta5-passthrough` (FrameExporter, мискины камеры/тумана/неба).
- Связь: WebSocket 127.0.0.1 (JSON) + shared memory кадров `Local\SemCraft2Frame` + файл геометрии в %TEMP%.

## Факты движка (проверено по коду)
- `OnRenderView(wo, penViewer, apr, pdp)` вызывается сразу после `RenderView` мира (Classics Patch
  `Core/Patches/Rendering.cpp`), до оружия и HUD.
- Мир: `glFrustum(near, far)`; far<0 → 1e5. `glDepthRange(0, 0.9)` для мира, фон 0.9..1
  (`Engine/Rendering/Render.cpp:276,365`, `Graphics/DrawPort.cpp:366-380`).
- Оси Sam = оси MC (правые, y вверх, взгляд −z при heading 0). Sam forward = (−sin h, ·, −cos h).
  MC yaw = 180 − heading; MC pitch = −pitch. 1 юнит = 1 блок.
- MC 26.3 depth обратный (1 = near, 0 = пусто), линейная z = n·f/(n + d·(f−n)) (пример GTA).
- MC 26.3 баг: depth readback ставит GL_READ_BUFFER=NONE → мискин GlCommandEncoder (пример GTA).

## Тест-стенд (без мыши/клавиатуры пользователя)
`uv run -q --with pillow --with websockets python tools/dev.py <cmd>` (см. docstring):
`mc-start`/`mc-stop` (dev-клиент runClient, PID по KnotClient), `sam tfe 01_Hatshepsut` (SEMCRAFT2_DEV=1, ждёт
плагин, через 8 с `StartMap`), `cmd tfe "<консоль>"` (dev-хук плагина: `%TEMP%\SemCraft2\cmd.txt`),
`shot tfe out.png` (плагин читает back buffer после оверлея → BMP → PNG), `mc '{"t":"status"}'` (статус MC),
`log tfe`, `mclog`, `sam-stop`. Тестовые консольные команды плагина: `sc_Status()`, `sc_Pillar(4)`,
`sc_Spawn("zombie 3")`, `sc_Shot("C:/путь.bmp")`, `sc_Build()`, `sc_Send("{json}")`, `sc_iCompositeMode`.
Тест-хуки ввода (через `OnPlayerAction`, работают без фокуса окна): `sc_iTestFire=N` (тиков огня),
`sc_iTestSelect=K` (оружие, как клавиша K), `sc_iTestWalk=N` (тиков вперёд), `sc_iTestTurn=N` +
`sc_fTestTurnH/P` (градусов за тик). Ролики: `tools/director.py` (орда, ракета, стройка, Werebull, TNT),
`tools/director_build.py` (стена + Клир против зомби) — перезапуск уровня, чистка, запись `um win record`.

## Доказано в игре (TFE 1.05, 01_Hatshepsut, 2026-10-09)
- Кадр MC (мир + HUD) вклеивается в кадр Sam: хотбар/сердца поверх HUD Sam (`_shots/t1.png`).
- Золотой столб MC в точке прицела Sam, перекрыт объектами Sam по глубине (`_shots/t2.png`).
- Зомби MC стоят и ходят по полу и пандусу Sam (оболочка из блоков), дроп лежит на полу (`_shots/t5.png`).
- Урон: 50 зомби MC убили игрока Sam (hp 100 → −5) без крашей (после фикса инфликтора).

## Грабли (gotchas)
1. `OnRenderView` получает проекцию ДО `Prepare()`: `RenderView` копирует её (`re.re_prProjection = prProjection`)
   и готовит копию. Плоскости отсечения в apr — мусор. Решение: своя копия + `DepthBufferNear/Far = 0/0.9` +
   `Prepare()` (как Render.cpp:272-278). Результат: tan L/R ±1.333, B/T ±0.75 (90° гор., 16:9).
2. Bash-инструмент агента «съедает» обратные слэши в команде (`\\n` → перевод строки) — исходники с escape
   писать только через Write/Edit.
3. Строки консоли Sam: `\` — escape. Пути в `sc_Shot` — прямыми слэшами.
4. Shell-функция с аргументами `CTString, INDEX`: второй аргумент приходит мусором (n=50 вместо 3) → один
   строковый аргумент + sscanf. Одиночный `INDEX` работает (`sc_Pillar(4)`).
5. `InflictDirectDamage` с закэшированной брашевой сущностью как инфликтором → AV в `IsOfClass+0x19`
   (проверка friendly fire в Player.es). Решение: инфликтор = сам игрок, тип `DMT_IMPACT` (самоурон
   `DMT_CLOSERANGE` Player.es игнорирует), вызов под `__try/__except`.
6. Intro.wld ~8500×8200 м, пол — гигантские треугольники: решётка по всему треугольнику не заканчивается.
   Решение: ленивая вокселизация по чанкам вокруг игрока (бины 64×64), растеризация по доминантной оси
   с обрезкой по чанку.
7. Sam объявляет уровень раньше, чем MC открыл мир → запоминать последний и строить на SERVER_STARTED.
8. Серверный игрок MC не переносится клиентскими пакетами на 32768 блоков (регион уровня) → серверный
   `teleportTo`, если расхождение > 4 блоков.
9. Лог Sam содержит цветовые коды (`^c00ff00[SemCraft2]^r`) — учитывать при grep.
10. Через `SeriousSam_Custom.exe` грузится `Entities_Custom.dll` (Classics Patch), не ванильный Entities.dll.
11. Высота мира: Hatshepsut по Y −124..1184 → overworld `min_y −1024, height 2048` (датапак в моде) +
    сдвиг oy по центру bbox, если уровень выходит за ±1000.

12. Пересылать в Sam только урон «от мира MC» (сущность, взрыв, огонь/лава). Двойник MC стоит внутри оболочки у
    стен (удушье) и пролетает пустоту при входе в мир (out of world) — это убивало Сэма даже в god mode
    (`cht_bGod` действует только при `CheatsEnabled()`).
13. Глаза двойника MC = камера Sam (ноги = камера − eyeHeight, если камера у тела), иначе прицел MC на ~0.28 м
    ниже прицела Sam (рост Стива 1.62 против глаз Сэма).
14. Блок ставится в клетку с частичной оболочкой (подъём пандуса): миксин `BlockPlaceContext.canPlace` разрешает
    замену; при сломе оболочка возвращается.
15. Extension-пакеты Classics Patch (`CExtEntityCreate`) таскают C++-объекты (`CAnyValue`) через границу DLL —
    плагин со своим CRT падает. Сущности создавать напрямую: `CWorld::CreateEntity_t` + `Initialize()`.
16. Твёрдые блоки MC в Sam = `ModelHolder2` c `Models\Editor\CollisionBox.mdl` (collision box (0,0,0)-(1,1,1)),
    `m_bActive=FALSE` (только редактор рисует), `m_bColliding=TRUE`. Модели в SE1 сталкиваются сферами (края
    скруглены). `CWorld::CastRay` пропускает editor-модели вне редактора → пули Sam проходят сквозь блоки MC,
    `sc_Probe` их не видит; оракул — физика (`tools/test_blocks.py`: свободно 10.16 м, в стенку 2.14 м).
17. Классы монстров TFE: `Boneman` (Клир), `Werebull`, `Headman`, `Eyeman`, `Walker`, `Scorpman`, `Woman`,
    `Gizmo`, `Beast`, `BigHead`, `Fish`, `Devil`, `Elemental` (`Classes\<имя>.ecl`).

18. Запись Sam: `um win record --exe SeriousSam_Custom.exe` (gfxcapture) — из Python вызывать через
    `C:\Program Files\Git\bin\bash.exe -lc` (`um` — bash-скрипт, subprocess его не находит).
19. Повороты тест-хуком накапливаются в смещении к `pa_aRotation`; прибавлять только на свежих действиях
    (`iResent < 0`), иначе повторно отправленные пакеты крутят камеру лишний раз.
20. Шрифт `um video compile` не знает «·» (квадраты) — в титрах `|`.
21. `CCastRay` берёт editor-модели только при включённых editor-моделях (WED), а `ModelHolder2` TSE сталкивается
    только при `m_bActive` (`if (m_bColliding&&m_bActive)`). Блок = активная модель (`RT_MODEL`) с
    `mo_ColorMask = 0`: поверхности не рисуются (`_ulColorMask & ms_ulOnColor` = 0), а лучи `TT_COLLISIONBOX`
    (пули, `Bullet.es`) и физика его видят.
22. Лучи `TT_COLLISIONBOX` проверяют сферы коллизии: у куба одна сфера r=0.5, на шве двух блоков и у стыка
    четырёх — щель. Пули Sam имеют радиус (`m_fBulletSize` 0.1, дробь 0.3 в одиночной игре) → утечка только у
    углов (~4% площади). `sc_Probe` (радиус 0) проходит по шву — это не значит, что пули проходят.
23. Старт Hatshepsut x = 0 — ровно шов блоков MC (x = 32768). Тесты стрельбы сдвигают прицел с шва.
24. `sc_iTestSelect=4` при томмигане переключает на миниган (та же клавиша); у минигана раскрутка → короткий
    `sc_iTestFire` не стреляет. В оракулах кольты (клавиша 2) и подсчёт `weapons: N rays` из `sc_Status`.
25. Лог MC содержит эхо команды (`command: ... say X`) → искать `[Server] X`, а не `X`.
26. Отдача взрывов MC: `ServerExplosion.getHitPlayers()` после `explode()` (миксин на RETURN) → `push` в Sam →
    `CMovableEntity::GiveImpulseTranslationAbsolute`. ×20 гасилось трением (0.94 м), ×30 даёт ~4 м.
27. Пули Sam летят от ствола (`CalcWeaponPosition`: смещение от глаз, параллельно взгляду), прицел TSE рисуется в
    точке попадания луча оружия → на 5 м он на ~1 блок ниже центра экрана. MC-луч от глаз бил выше прицела.
    `CPlayerWeapons` хранит старт последней пули: свойство 35 `m_vBulletSource` (FLOAT3D) → выстрелы в MC от него.
28. TSE `LevelsMP/1_1_Palenque`: Сэм на старте падает в озеро и плавает — вид дрейфует, прицел гуляет, оракулы
    стрельбы врут (заметил пользователь). Тесты TSE — на `LevelsMP/1_2_Palenque` (стоит на траве).
29. TSE `ModelHolder2` не сталкивался при `m_bActive=FALSE` (gotcha 21) — до этого блоки MC в TSE были не твёрдыми;
    `tools/test_blocks.py --game tse`: свободно 7.57 м, в стену 1.43 м.

## Доказано дальше (2026-10-09)
- Оружие: кольт 20 → 0.8 HP зомби (`tools/test_shoot.py`), ракеты: 5 зомби 100 → 0 (снаряд «пойман» мобом).
- Монстры Sam ↔ мобы MC: 4 зомби убили Клира (счёт +1000), прокси-жители масштабированы по росту монстра.
- Блоки MC твёрдые для Сэма (`tools/test_blocks.py` PASS).
- TSE 1.07 Palenque: связь, вклейка, оболочка 474k блоков.
- Оружие ↔ блоки (`tools/test_wall.py`, TFE Hatshepsut): со стеной кольт не ранит Клира за ней, без стены та же
  очередь −20 HP; очереди ломают блоки и они исчезают в Sam; `sc_Probe` со стеной — ModelHolder2 в 4.7 м.
  `tools/test_tnt.py`: выстрел поджигает TNT, взрыв толкает Сэма на ~4 м. Трещины: `_shots/w2.png`.
  TSE 1.07 (`LevelsMP/1_2_Palenque`): стена 3/3, TNT 2/2, блоки твёрдые; трещины у прицела (`_shots/w4_tse.png`).
- Ролик: `_takes/SemCraft2_showcase.mp4` (27.7 s, 1920x1080) из take2 + take4 (`_takes/showcase_edl.json`).

## Журнал
- 2026-10-09: разведка; выбор концепции с пользователем (MC внутри Sam, новый репо, Classics Patch).
  Classics Patch 1.9.4 поставлен в обе копии. SDK склонирован.
- 2026-10-09: MC-мод (кадр, камера, оболочка, урон) + плагин Sam (камера, экспорт уровня, GL-компоновщик,
  режим стройки). Вертикальный срез доказан в TFE (см. выше).
- 2026-10-09: оружие (хитскан + снаряды), монстры Sam ↔ мобы MC, твёрдые блоки, TSE Palenque, README.
- 2026-10-09: тест-хуки поворота/ходьбы, режиссёрские скрипты, ролик 27.7 s.
