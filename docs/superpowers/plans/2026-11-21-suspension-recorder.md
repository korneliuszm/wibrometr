# Rejestrator drgań zawieszek — plan wdrożenia

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Nowe środowisko `waveshare_vibration`: firmware, który mierzy QMI8658, zapisuje pomiar na kartę SD po przekroczeniu progu (ustawianego dotykiem), rysuje wykres XYZ i oddaje kartę komputerowi przez USB.

**Architecture:** Cała logika niezależna od sprzętu (format CSV, wyzwalacz, bufor wstępny, automat zapisu, krok progu, własność karty) mieszka w nagłówkach `include/` i ma testy na komputerze (g++). Warstwa sprzętowa w `src/vibration/` (zadanie IMU z automatem, zapis na SD w tle, USB MSC na SD, ekran) korzysta ze sterowników `src/waveshare/hal/`. Stare środowiska `feather` i `waveshare` nie zmieniają się.

**Tech Stack:** C++17, Arduino-ESP32 (pioarduino 55.03.36), Adafruit ST7789/GFX, USBMSC + biblioteka `SD`, FreeRTOS. Testy: g++ na komputerze.

**Spec:** `docs/superpowers/specs/2026-11-21-suspension-recorder-design.md`

## Global Constraints

- Środowisko PlatformIO: `waveshare_vibration`; `build_src_filter` bierze tylko `src/vibration/` oraz `waveshare/hal/{Display,Cst816,Qmi8658,I2cBus}.cpp`; flagi jak w `waveshare` (`-std=gnu++17`, `ARDUINO_USB_CDC_ON_BOOT=1`, `ARDUINO_USB_MODE=0`, `-Isrc/waveshare`). Dodać do `default_envs` (CI buduje `pio run`).
- Próbkowanie 500 Hz, zakres ±8 g (`kRange = 2`), filtr sprzętowy jak w `src/waveshare/AppConfig.h`.
- Bufor wstępny 1 s, cisza kończąca pomiar 3 s, stała filtra górnoprzepustowego 1 s, rozgrzewka 1 s po starcie/przerwie.
- Próg: domyślnie 2,0 m/s², zakres 0,2–40,0, przechowywany jako liczba całkowita dziesiątych (`int deci`, 2..400), zapis w NVS (`Preferences`, przestrzeń `"vib"`, klucz `"thr"`).
- Pliki: `/MEAS/M0001.CSV` … `M9999.CSV`; kolumny `t_ms,ax_m_s2,ay_m_s2,az_m_s2`; czas względem próbki wyzwalającej (ujemny przed nią), osie czujnika bez korekty.
- Ekran 320×240 poziomo, teksty po polsku bez ogonków (jak w reszcie projektu).
- Testy hosta: `g++ -std=c++17 -Wall -Wextra -Iinclude test/vibration_checks.cpp -o vc && ./vc` (bez ostrzeżeń); wpis w `.github/workflows/ci.yml`.
- Praca na gałęzi `ccr-54681c6e-mahd3z`; komunikaty commitów kończą się liniami atrybucji z konwersacji.

## Review Focus

1. USB podłączone w trakcie pomiaru: plik zamknięty ze stopką, karta nietknięta, po odłączeniu czuwanie dopiero po rozgrzewce (Task 3).
2. Zapis odmówiony (brak karty, błąd, karta pełna): brak pętli otwierania pliku na każdej próbce, wyzwalacz milczy przez 1 s (Task 3).
3. Przerwa w danych z czujnika w trakcie pomiaru (ponowne łączenie I2C): plik zamknięty, bufor wstępny i filtr wyczyszczone (Task 3).
4. Ciągła wibracja o przebiegu sinusoidalnym ma przejścia przez zero poniżej progu: pomiar nie może się rozpaść na kilka plików (Task 2).
5. Wyczerpanie numeracji (M9999): czytelny błąd zamiast nadpisania istniejących plików (Task 1).

---

### Task 1: Format plików pomiarów

**Files:**
- Create: `include/MeasFormat.h`, `test/vibration_checks.cpp`
- Modify: `.github/workflows/ci.yml`

**Interfaces:**
- Produces (namespace `measfmt`, wzorowane na `include/LogFormat.h`, które można włączyć dla `logfmt::formatFixed3`):
  - `constexpr const char *kDirectory = "/MEAS";`
  - `std::size_t measPath(unsigned number, char *out, std::size_t size)`
  - `bool parseMeasNumber(const char *name, unsigned &number)` — `M0007.CSV`, wielkość liter dowolna, z katalogiem lub bez; `S0007.CSV`, `M007.CSV`, `M0007.TXT` odrzucane
  - `constexpr unsigned nextMeasNumber(unsigned highestExisting)` — `highest+1`, a po 9999 zwraca 0 (= brak miejsca w numeracji)
  - `struct Header { unsigned number; int thresholdDeci; float sampleRateHz, rangeG, preRollS, quietS; }`, `std::size_t formatHeader(char*, std::size_t, const Header&)`
  - `std::size_t formatSample(char *out, std::size_t size, int32_t relUs, float ax, float ay, float az)`
  - `std::size_t formatFooter(char *out, std::size_t size, uint32_t samples, uint32_t dropped)`

- [ ] **Step 1: Napisać testy** w nowym `test/vibration_checks.cpp` (makro `CHECK(cond)` liczące porażki, funkcje sekcji wywoływane z `main`, wynik niezerowy przy porażce), sekcja `formatChecks()`:
  - `formatSample(.., -1000000, 0.5f, -1.25f, 9.8066f)` daje `"-1000.000,0.500,-1.250,9.807\n"`; `(2000, 0,0,0)` daje `"2.000,0.000,0.000,0.000\n"`; `(-2, 0,0,0)` daje `"-0.002,0.000,0.000,0.000\n"`; `(0, ...)` zaczyna się od `"0.000,"`.
  - `parseMeasNumber("/MEAS/m0042.csv", n)` jest prawdą z `n==42`; `"S0007.CSV"`, `"M007.CSV"`, `"M0007.TXT"` są fałszem. `measPath(7, ..)` daje `"/MEAS/M0007.CSV"`.
  - `nextMeasNumber(0)==1`, `(41)==42`, `(9998)==9999`, `(9999)==0`.
  - `formatHeader` dla `{7, 20, 500.f, 8.f, 1.f, 3.f}`: każda linia zaczyna się od `#`; zawiera `"# measurement: 7\n"`, `"# threshold_m_s2: 2.0\n"`, `"# sample_rate_hz: 500.0\n"`, `"# range_g: 8\n"`, `"# pre_trigger_s: 1.0\n"`, `"# quiet_s: 3.0\n"`, `"# trigger_at: t_ms = 0\n"`; kończy się `"# columns: t_ms,ax_m_s2,ay_m_s2,az_m_s2\n"`.
  - `formatFooter(.., 1234, 5)` daje `"# end: samples=1234 dropped=5\n"`.
- [ ] **Step 2: Uruchomić testy** (polecenie z Global Constraints). Oczekiwane: błąd kompilacji (brak `MeasFormat.h`).
- [ ] **Step 3: Zaimplementować `include/MeasFormat.h`.** Czas: znak + `|µs|/1000` + `.` + trzy cyfry (`|µs|%1000`), bez `printf` na floatach; wartości przez `logfmt::formatFixed3`.
- [ ] **Step 4: Uruchomić testy.** Oczekiwane: `all ok`.
- [ ] **Step 5: Dodać do CI** (job `host-tests`) linię `- run: g++ -std=c++17 -Wall -Wextra -Iinclude test/vibration_checks.cpp -o vc && ./vc`. Commit: `feat: format plików pomiarów rejestratora drgań`.

### Task 2: Bufor wstępny i wyzwalacz

**Files:**
- Create: `include/SampleRing.h`, `include/TriggerDetector.h`
- Modify: `test/vibration_checks.cpp`

**Interfaces:**
- Produces:
  - `template <std::size_t N> class SampleRing` (`struct Entry { uint32_t tUs; Accel a; }`): `void push(uint32_t tUs, const Accel&)`, `std::size_t size() const`, `Entry at(std::size_t i) const` (0 = najstarsza), `void clear()`. `Accel` z `include/SignalHistory.h`.
  - `class TriggerDetector`: `struct Config { float sampleRateHz = 500; float highPassTauS = 1.0f; uint32_t warmupUs = 1000000; uint32_t quietUs = 3000000; }`; `enum class Event : uint8_t { None, Start, Stop }`; `explicit TriggerDetector(Config = {})`; `void reset()`; `Event update(uint32_t tUs, const Accel &a, float thresholdMs2)`; `Accel dynamic() const` (a − wolna średnia); `float deviation() const` (|dynamic|); `bool recording() const`; `void cancelRecording()`.

- [ ] **Step 1: Napisać testy** `ringChecks()` i `triggerChecks()` (próbki co 2000 µs, generator pomocniczy `feed(detector, tStartUs, seconds, lambda(t)->Accel, thresholdMs2)` zwracający listę `{tUs, Event}` różnych od `None`):
  - Pierścień o N=4: po 6 `push` ma `size()==4`, `at(0)` to trzecia wstawiona, `at(3)` szósta; `clear()` zeruje `size()`.
  - Stała grawitacja `(0,0,9.81)` przez 5 s przy progu 2,0: brak zdarzeń.
  - Wolny przechył 0–90° w 30 s (grawitacja obraca się w XZ): brak zdarzeń przy progu 2,0.
  - Sinus 10 Hz, amplituda 5 m/s² na X, od t=3 s przez 5 s, potem cisza (grawitacja): `Start` przy t w [3,0 s; 3,1 s]; w trakcie sinusa żadnego `Stop`, mimo przejść przez zero (Review Focus 4); `Stop` w [8,0+3,0 s; 8,0+3,1 s] po jego końcu; dokładnie jedno `Start` i jedno `Stop`.
  - Rozgrzewka: ten sam sinus od t=0 nie daje `Start` przed t=1,0 s; `Start` pojawia się zaraz po rozgrzewce (≤ 1,1 s).
  - `reset()` w trakcie nagrywania zeruje `recording()` i restartuje rozgrzewkę (brak `Start` przez kolejną sekundę nawet przy sinusie).
  - `cancelRecording()` w stanie nagrywania daje `recording()==false`, a kolejny próg przekroczony od razu znów zwraca `Start`.
- [ ] **Step 2: Uruchomić testy.** Oczekiwane: błąd kompilacji.
- [ ] **Step 3: Zaimplementować oba nagłówki.** Filtr: `lp += alpha*(a-lp)`, `alpha = dt/(tau+dt)`, `dt = 1/sampleRateHz`, `lp` inicjowane pierwszą próbką po `reset()`. `Start` tylko po rozgrzewce (czas od pierwszej próbki po `reset()` ≥ `warmupUs`, liczony z `tUs` odejmowanych jako `uint32_t`), gdy `deviation() >= threshold` i nie nagrywa; w nagrywaniu licznik ciszy zeruje się przy każdej próbce `>= threshold`, a `Stop` wychodzi, gdy próbki poniżej progu trwają `quietUs`.
- [ ] **Step 4: Uruchomić testy.** Oczekiwane: `all ok`.
- [ ] **Step 5: Commit** `feat: bufor wstępny i wyzwalacz progiem wibracji`.

### Task 3: Automat zapisu (RecorderCore)

**Files:**
- Create: `include/RecorderCore.h`
- Modify: `test/vibration_checks.cpp`

**Interfaces:**
- Consumes: `SampleRing<512>`, `TriggerDetector` (Task 2).
- Produces:
  - `struct MeasSink { virtual bool open(int thresholdDeci) = 0; virtual void write(int32_t relUs, const Accel &a) = 0; virtual void close() = 0; virtual ~MeasSink() = default; };` — `open` zwraca `false`, gdy nie można zacząć pomiaru; nigdy nie blokuje.
  - `class RecorderCore`: `enum class Mode : uint8_t { Armed, Recording, Usb }`; `explicit RecorderCore(TriggerDetector::Config = {}, uint32_t preRollUs = 1000000, uint32_t holdoffUs = 1000000)`; `void setThresholdDeci(int)` i `int thresholdDeci() const` (atomowe — wołane z innego wątku); `void requestUsb(bool active)` (atomowe, z dowolnego wątku); `void service(MeasSink&)` (przetwarza żądanie USB; wołana z wątku czujnika co ~1 ms, także bez próbek); `void onSample(uint32_t tUs, const Accel&, MeasSink&)` (najpierw `service`); `void onGap(MeasSink&)`; `Mode mode() const` (atomowe); `Accel dynamic() const`.
  - Zasady: `onSample` wstawia próbkę do pierścienia, potem ocenia wyzwalacz (próg = `thresholdDeci/10.0f`). `Start`: `sink.open(thresholdDeci)`; przy `false` → `cancelRecording`, tryb `Armed`, zdarzenia ignorowane przez `holdoffUs`. Przy `true` → `write` dla wpisów pierścienia z `tUs >= trigger − preRollUs` (od najstarszych, `relUs = int32_t(tUs − trigger)`, wyzwalająca ma 0), potem każda kolejna próbka jest zapisywana. `Stop` → `sink.close()`, `Armed`. `onGap` i `requestUsb(true)`: zamyka pomiar, jeśli trwa, czyści pierścień, `detector.reset()`; `requestUsb(true)` → `Usb` (próbki ignorowane); `requestUsb(false)` → `Armed` z rozgrzewką.

- [ ] **Step 1: Napisać testy** `coreChecks()` z `FakeSink` (zapamiętuje wywołania `open/write/close`, flaga `acceptOpen`):
  - Cisza 5 s: zero wywołań.
  - Impuls (sinus 10 Hz, 5 m/s², 2 s po 3 s ciszy), potem 4 s ciszy: `open` raz z `thresholdDeci==20`; pierwszy `write` ma `relUs` w [−1 000 000; −998 000], dokładnie jeden zapis z `relUs==0`, `relUs` rosną o 2000; `close` raz, po ≥3 s ciszy od końca impulsu; żaden zapis nie następuje po `close`.
  - Odmowa (`acceptOpen=false`) przy ciągłym sinusie przez 3 s: `open` wołane co najwyżej raz na 1 s (≤ 3 razy), zero `write`; po `acceptOpen=true` następny `open` udaje się dopiero po holdoffie (Review Focus 2).
  - `requestUsb(true)` w trakcie pomiaru + `service`: `close` dokładnie raz, tryb `Usb`, dalsze `onSample` nic nie wywołują; `requestUsb(false)`: tryb `Armed`, sinus od razu po tym nie daje `open` przez 1 s (Review Focus 1).
  - `onGap` w trakcie pomiaru: `close` raz; następny pomiar po przerwie nie zawiera próbek sprzed przerwy (żaden `relUs` poniżej −preRoll liczonych od nowego wyzwolenia, a pierwszy `write` ≥ czasu pierwszej próbki po przerwie) (Review Focus 3).
  - Podniesienie progu w trakcie pomiaru (`setThresholdDeci(400)` przy sinusie 5 m/s²): `close` po 3 s.
- [ ] **Step 2: Uruchomić testy.** Oczekiwane: błąd kompilacji.
- [ ] **Step 3: Zaimplementować `include/RecorderCore.h`** wg zasad z Interfaces.
- [ ] **Step 4: Uruchomić testy.** Oczekiwane: `all ok`.
- [ ] **Step 5: Commit** `feat: automat zapisu pomiarów wyzwalany progiem`.

### Task 4: Logika ekranu i własność karty

**Files:**
- Create: `include/ThresholdUi.h`, `include/UsbOwnership.h`
- Modify: `test/vibration_checks.cpp`

**Interfaces:**
- Consumes: `Rect` z `include/TouchGestures.h`.
- Produces (namespace `thrui`): `kMinDeci=2`, `kMaxDeci=400`, `kDefaultDeci=20`; `constexpr int clampDeci(int)`; `constexpr int stepDeci(int deci, int dir)` — krok 1 poniżej 5,0, 5 od 5,0 do 20,0, 10 powyżej; granica: w górę krok z zakresu `deci`, w dół z zakresu `deci` włącznie z górną granicą (50 w dół → 49, 200 w dół → 195); wynik przycięty; `constexpr Rect kMinus{0,196,84,44}, kPlus{236,196,84,44}, kChart{26,30,294,146}`; `enum class Button : uint8_t { None, Minus, Plus }`; `constexpr Button hit(int x, int y)`; `constexpr float chartRangeMs2(int deci)` (najmniejsza z {5,10,20,40,80} ≥ 2×próg); `class HoldRepeater { int update(Button pressed, uint32_t nowMs); }` — zwraca −1/0/+1: krok od razu przy naciśnięciu (także przy zmianie przycisku), po 500 ms trzymania co 100 ms, 0 po puszczeniu.
- Produces (`UsbOwnership`, atomowa): `void setEnumerated(bool)`, `void onEject()`, `void onAccess()`, `bool hostOwns() const`. Podłączony host → `true`; eject → `false`; dostęp do sektorów po ejekcie → `true`; odłączenie → `false` i wyczyszczony eject (ponowne podłączenie → `true`).

- [ ] **Step 1: Napisać testy** `uiChecks()`: `stepDeci(20,+1)==21`, `(2,-1)==2`, `(400,+1)==400`, `(50,+1)==55`, `(55,-1)==50`, `(50,-1)==49`, `(200,+1)==210`, `(210,-1)==200`, `(200,-1)==195`; `chartRangeMs2(20)==5`, `(25)==5`, `(26)==10`, `(400)==80`; `hit(10,210)==Minus`, `hit(300,210)==Plus`, `hit(160,210)==None`, `hit(10,100)==None`; `HoldRepeater`: Minus przy 0 ms → −1, przy 100 → 0, 499 → 0, 500 → −1, 550 → 0, 600 → −1, `None` → 0, ponowne Minus → −1 od razu, zmiana na Plus w trakcie trzymania → +1 od razu. `UsbOwnership`: sekwencja ze specyfikacji powyżej (początkowo `false`).
- [ ] **Step 2–4:** uruchomić (błąd kompilacji), zaimplementować nagłówki, uruchomić (`all ok`).
- [ ] **Step 5: Commit** `feat: krok progu, układ ekranu i własność karty`.

### Task 5: Środowisko, zadanie czujnika i zapis na SD

**Files:**
- Create: `src/vibration/VibConfig.h`, `src/vibration/VibImuTask.{h,cpp}`, `src/vibration/MeasWriter.{h,cpp}`, `src/vibration/main.cpp` (na razie szkielet: `setup()` + pusta pętla)
- Modify: `platformio.ini` (nowe `[env:waveshare_vibration]`, `default_envs`)

**Interfaces:**
- Consumes: `RecorderCore`, `MeasSink`, `measfmt::*`.
- Produces:
  - `VibConfig.h` (namespace `vib`): `kSampleRateHz=500`, `kRange=2`, `kLowPassMode=3`, `kPreRollUs`, `kQuietUs`, `kWarmupUs`, `kFrameMs=50`, `kBins=136`, `kBinMs=40`.
  - `struct VibEvent { enum class Kind : uint8_t { Sample, Connecting, Waiting } kind; uint32_t ms; Accel dynamic; };` oraz `class VibImuTask { void begin(QueueHandle_t events, RecorderCore *core, MeasSink *sink); float sampleRateHz() const; float rangeG() const; uint32_t dropped() const; }`. Logika połączenia/ponawiania/odczytu jak `src/waveshare/ImuTask.cpp` (celowa kopia: tamten jest sprzęgnięty z `SdLogger` i żyroskopem). Zamiast wpychania do loggera: w każdej iteracji `core->service(*sink)`, po próbce `core->onSample(stamp.us, a, *sink)` i `VibEvent` z `core->dynamic()` do kolejki; `core->onGap(*sink)` przy błędach odczytu i ponownym łączeniu.
  - `struct MeasStatus { enum class State : uint8_t { NoCard, Idle, Recording, Error, Parked } state; char message[32]; unsigned number; uint32_t samples, fileBytes, recordingMs, cardMb, dropped, measurements; };`
  - `class MeasWriter : public MeasSink`: `void begin(float sampleRateHz, float rangeG)` (uruchamia zadanie na rdzeniu 0 priorytet 1, wzór: `src/waveshare/hal/SdLogger.cpp` — te same stałe buforowania: 3584 B, zapis co ≤250 ms, `flush` co 2 s, ponowny mount co 3 s, kolejka ~1024 rekordów); `open/write/close` zgodnie z `MeasSink` (nieblokujące; `open` daje `false`, gdy stan ≠ `Idle` lub numeracja wyczerpana); `void park()` (asynchronicznie: zamyka plik, po opróżnieniu kolejki stan `Parked`, karta nietykana), `void unpark()` (przy powrocie: `SD.end()` + ponowny `SD.begin` i skan numeracji), `bool parked() const`, `MeasStatus status() const`. Mount: katalog `/MEAS`, numer = największy `Mnnnn.CSV` + 1 (`nextMeasNumber`; 0 → stan `Error` „numeracja pelna”); plik z nagłówkiem na początku, stopka `formatFooter` przy zamknięciu, błąd zapisu → `Error` (plik zamknięty), ponawianie mountu co 3 s.

- [ ] **Step 1: Dodać środowisko do `platformio.ini`** (wg Global Constraints) i `waveshare_vibration` do `default_envs`.
- [ ] **Step 2: Napisać `VibConfig.h`, `VibImuTask`, `MeasWriter` i szkielet `main.cpp`** wg Interfaces.
- [ ] **Step 3: Zbudować.** `pip install platformio` (jeśli brak), potem `TEMP=/tmp pio run -e waveshare_vibration`. Oczekiwane: `SUCCESS`. Jeśli sieć blokuje pobranie toolchaina, weryfikacją jest CI po wypchnięciu (powiedzieć o tym wprost w raporcie).
- [ ] **Step 4: Commit** `feat: środowisko waveshare_vibration, zadanie czujnika i zapis na SD`.

### Task 6: Dysk USB z kartą SD

**Files:**
- Create: `src/vibration/UsbSd.{h,cpp}`

**Interfaces:**
- Consumes: `UsbOwnership`, `MeasWriter::parked()`.
- Produces (namespace `usbsd`): `bool begin(UsbOwnership &own, MeasWriter &writer)` — wołane po próbie zamontowania karty (czeka do ~2 s na `status().state != NoCard`): przy karcie rejestruje `USBMSC` (`vendorID "WIBROMTR"`, `productID "VIBRACJE"`, `onRead/onWrite/onStartStop`, `msc.begin(SD.numSectors(), SD.sectorSize())`), zawsze `USB.begin()`; bez karty tylko `USB.begin()` (port szeregowy), zwraca `false`. `void service(uint32_t nowMs)` — co ≥100 ms: `own.setEnumerated(host obecny)`.
- Zasady: `onRead/onWrite` przez `SD.readRAW/writeRAW` sektor po sektorze; gdy `!writer.parked()` zwracają `0` (host ma ponowić), po `parked()` wołają `own.onAccess()` i obsługują transfer; `onStartStop(eject)` woła `own.onEject()`. „Host obecny” = `tud_mounted() && !tud_suspended()` (`#include "tusb.h"`; jeśli nagłówek niedostępny przez `USB.h`, zastąpić zdarzeniami `USB.onEvent`: `ARDUINO_USB_STARTED/STOPPED/SUSPEND/RESUME_EVENT`).

- [ ] **Step 1: Zaimplementować `UsbSd`** wg Interfaces; komentarz w pliku: niesprawdzone na sprzęcie (obecność hosta, `msc.begin` z liczbą sektorów karty).
- [ ] **Step 2: Zbudować** (`pio run -e waveshare_vibration`, albo CI). Oczekiwane: `SUCCESS`.
- [ ] **Step 3: Commit** `feat: karta SD jako dysk USB`.

### Task 7: Ekran, dotyk i pętla główna

**Files:**
- Create: `src/vibration/VibUi.{h,cpp}`
- Modify: `src/vibration/main.cpp`

**Interfaces:**
- Consumes: wszystko powyżej; `hal::Display`, `hal::Cst816`, `ui::` z `src/waveshare/ui/Draw.h` i `include/UiTheme.h`, `SignalHistory<vib::kBins>`.
- Produces: `struct VibModel { RecorderCore::Mode mode; MeasStatus sd; int thresholdDeci; bool sensorLive; const SignalHistory<vib::kBins> *history; bool hostOwns; uint32_t nowMs; };` i `void drawVibScreen(GFXcanvas16 &c, const VibModel &m)`.

Układ (320×240):
```text
y0    nagłówek:  CZUWAM | ● NAGRYWAM 12.3s M0042 | USB: KARTA U KOMPUTERA | BRAK KARTY - WLOZ I RESET | BLAD: <msg> | BRAK CZUJNIKA
y30   wykres XYZ (dynamika, kChart): siatka, linie progu ±próg (żółte), skala ±chartRangeMs2, okno 5,4 s (136 kolumn po 40 ms, rysowanie jak drawWave w ui/Screens.cpp)
y180  "pomiarow: 41   karta 7580 MB"
y196  [ - ]      PROG 2.0 m/s2      [ + ]
```

- [ ] **Step 1: Napisać `VibUi`** (nagłówek i wykres jak wyżej; kolory osi `ui::kAxisColor`; przyciski ciemne, wciśnięty jaśniejszy; wartość progu `%.1f`).
- [ ] **Step 2: Rozbudować `main.cpp`:** `setup`: serial, `I2cBus::begin`, `Display`, canvas, dotyk, odczyt progu z NVS (`clampDeci`, domyślnie `kDefaultDeci`), `core.setThresholdDeci`, `MeasWriter::begin`, `VibImuTask::begin`, krótka próba `usbsd::begin`, zadanie dotyku (jak `touchTask` w `src/waveshare/main.cpp`, publikuje surowy stan palca). `loop`: opróżnić kolejkę zdarzeń do `SignalHistory` (`add(dynamic)`, `tick` co 40 ms), `HoldRepeater` na surowym dotyku → `stepDeci` → `core.setThresholdDeci`; zapis do NVS dopiero po puszczeniu przycisku; `usbsd::service`; zmiana `own.hostOwns()` na `true`: `core.requestUsb(true)`, gdy `core.mode()==Usb` → `writer.park()`; na `false`: `writer.unpark()`, `core.requestUsb(false)`; rysowanie co `kFrameMs`. Zdarzenie `Connecting/Waiting` → `sensorLive=false`.
- [ ] **Step 3: Zbudować** (`pio run -e waveshare_vibration`, albo CI) i uruchomić wszystkie testy hosta z CI. Oczekiwane: `SUCCESS`, `all ok`.
- [ ] **Step 4: Commit** `feat: ekran z wykresem XYZ, ustawianie progu dotykiem, pętla główna`.

### Task 8: Dokumentacja

**Files:**
- Create: `docs/vibration-README.md`
- Modify: `docs/superpowers/specs/2026-11-21-suspension-recorder-design.md`, `README.md` (jedna linia odsyłacza)

- [ ] **Step 1: Poprawić spec** wg decyzji z planu: wyzwalacz to **wartość wektora po filtrze górnoprzepustowym** (nie |a|−g: ta sama odporność na przechył, ale łapie też drgania poziome); brak karty przy starcie → komunikat i restart po włożeniu; „USB podłączone” = host obecny (do potwierdzenia na sprzęcie).
- [ ] **Step 2: Napisać `docs/vibration-README.md`:** obsługa (ekran, próg, USB), format pliku (przykład 3 linii), ustawienia stałych, budowanie/wgranie (`pio run -e waveshare_vibration -t upload`), oraz listę „pierwsze włączenie — niesprawdzone na sprzęcie”: (1) host USB wykrywany i oddawana karta, plik zamknięty przy podłączeniu w trakcie pomiaru; (2) `msc.begin` z liczbą sektorów karty i widoczność plików w Windows/Linux; (3) zapis 500 Hz bez utraconych próbek razem z odświeżaniem ekranu na wspólnym SPI; (4) orientacja dotyku; (5) próg dobrany do realnych przejazdów.
- [ ] **Step 3: Dodać do `README.md`** jedną linię odsyłacza do `docs/vibration-README.md`.
- [ ] **Step 4: Commit** `docs: rejestrator drgań zawieszek`.

---

## Self-review

- **Pokrycie specyfikacji:** stany i USB (Task 3, 5–7), wyzwalacz i filtr (2), próg i NVS (4, 7), ekran (4, 7), CSV (1), numeracja (1, 5), stałe (Global Constraints), sprawdzenie na sprzęcie (8).
- **Spójność typów:** `MeasSink`, `RecorderCore::Mode`, `MeasStatus`, `thrui::*` są zdefiniowane w zadaniach wcześniejszych niż użycie.
- **Proporcje:** logika jest w testowanych nagłówkach, a kod sprzętowy opisany sygnaturami i zasadami; ciała funkcji zostawione wykonawcy.
