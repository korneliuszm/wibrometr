# Rejestrator drgań zawieszek — Waveshare ESP32-S3-Touch-LCD-2

Osobny, prosty firmware (środowisko PlatformIO `waveshare_vibration`) do
pomiaru drgań przejazdów zawieszek. Czujnik QMI8658 mierzy stale (~492 Hz), a
**zapis na kartę TF rusza dopiero po przekroczeniu progu wibracji**, który
ustawia się dotykiem na ekranie (i jest pamiętany po restarcie). Każdy pomiar
to osobny plik CSV. Po podłączeniu kabla USB do komputera pomiary się
zatrzymują, a karta pokazuje się jako dysk.

Sprzęt jest ten sam co w [docs/waveshare-README.md](waveshare-README.md)
(układ pinów, rewizje Rev 1.0 / 2.0, kamera) — tam jest tabela GPIO. Piny
są w `src/waveshare/hal/BoardPins.h`; ten firmware używa wyświetlacza, dotyku,
IMU i karty TF (SPI wspólne z ekranem). Linia rolkowa, Wi-Fi, WWW, FFT,
akumulator i kamera nie są w nim zawarte.

> **Stan:** logika niezależna od sprzętu jest przetestowana na komputerze.
> **Nie był jeszcze uruchamiany na płytce** — lista rzeczy do sprawdzenia
> jest w „Pierwsze włączenie — niesprawdzone na sprzęcie”.

## Obsługa

Jeden ekran 320×240 (fonty FreeSans; podgląd na PC: `tools/render_vib.sh`):

```text
y 0    pasek stanu: [● CZUWAM] / [● REC 12.3 s] / [USB: KARTA W PC] ...   karta N   81% [bateria]
y 36   panel wykresu: XYZ dynamiczne przyspieszenie, 5,4 s, pas ciszy między liniami progu
       dolny wiersz panelu: legenda X Y Z, pasek poziomu (próg w środku), poziom w m/s2
y 192  [ - ]      PROG WYZWALANIA  2.0 m/s2      [ + ]
```

Wykres pokazuje przyspieszenie z odjętą wolną średnią (grawitacja i przechył
nie są widoczne), osie czujnika (X czerwony, Y zielony, Z niebieski). Skala
pionowa dobiera się do progu (±5, 10, 20, 40 lub 80 m/s²); przerywane żółte
linie to próg i jego odbicie, jaśniejszy pas między nimi to „cisza”. Próbki
trafiają do kolumn według czasu próbki (nie chwili odebrania), więc linia jest
ciągła także wtedy, gdy odświeżanie ekranu opóźni odbiór. Podczas nagrywania
ramka panelu jest czerwona.

Pasek poziomu pokazuje bieżącą długość wektora (to, co porównuje wyzwalacz)
względem progu: żółta kreska w środku to próg, pasek zielony poniżej, czerwony
od progu. Pomaga dobrać próg do szumu w spoczynku.

Po prawej stronie paska stanu: ikona karty z liczbą plików `M*.CSV` w `/MEAS`
(ikona żółta, gdy od startu karta była naprawiana ponownym montowaniem) i
akumulator (procent z napięcia ogniwa; niebieska błyskawica = ładowanie, na
Rev 1.0 brak tej informacji; `USB` = brak ogniwa).

### Pasek stanu

| Napis | Znaczenie |
|---|---|
| `● CZUWAM` (zielony) | czujnik działa, karta gotowa, czeka na przekroczenie progu |
| `● REC 3.2 / 30 s` (czerwony, kropka miga) | trwa zapis pomiaru; czas od wyzwolenia i limit, pod spodem pasek postępu |
| `USB: KARTA W PC` (niebieski) | komputer ma kartę (albo oddawanie jest w toku); nic się nie nagrywa, na wykresie `ZAPIS WSTRZYMANY (USB)` |
| `BRAK KARTY` (żółty) | brak karty przy starcie; zapis rusza sam po włożeniu karty (ponowna próba co 1 s), ale dysk USB pojawi się dopiero po RESET |
| `BLAD KARTY` (czerwony) | błąd karty; przyczyna na wykresie, np. `blad zapisu na karte`, `nie mozna utworzyc /MEAS`, `numeracja pelna` |
| `BRAK CZUJNIKA` (żółty) | karta w porządku, ale czujnik nie daje próbek (komunikat jest też na wykresie) |

Kolejność: USB, potem REC (tylko gdy zapis działa), potem problemy z kartą,
potem brak czujnika, na końcu CZUWAM.

### Błędy karty w trakcie pomiaru

Krótkie zakłócenie karty (wstrząs gniazda TF przy mocnym uderzeniu, karta
zajęta zbyt długo) nie kończy już pomiaru błędem: gdy zapis się nie powiedzie,
rejestrator montuje kartę ponownie (do 3 prób) i dopisuje dalej do tego samego
pliku. W pliku pojawia się wtedy wiersz
`# card glitch: remounted, samples since the last flush may be missing`
(dane od ostatniego zrzutu, najwyżej ok. 2 s, mogą zniknąć; luka jest widoczna
w kolumnie `t_ms`). Podobnie gdy nowy plik nie daje się utworzyć: jedno
ponowne montowanie i druga próba. Dopiero gdy to nie pomoże, pojawia się
`BLAD KARTY`, a po 1 s karta jest montowana od nowa.

### Próg

Przyciski `-` i `+` zmieniają próg w m/s² (domyślnie 2,0; zakres 0,2–40,0).
Krok następuje od razu po dotknięciu; po przytrzymaniu dłużej niż 500 ms
powtarza się co 100 ms. Wielkość kroku zależy od wartości: **0,1** poniżej
5,0, **0,5** od 5,0 do 20,0, **1,0** powyżej. Wartość jest zapisywana w NVS po
puszczeniu przycisku (nie podczas nagrywania, żeby zapis do flasha nie
kosztował próbek) i odtwarzana po restarcie.

Mierzona jest **długość wektora (a − wolna średnia, tau 1 s)**: to samo
przyspieszenie co na wykresie, bez grawitacji, więc wolny przechył płytki nie
wyzwala, a drgania poziome tak. Próg stosuje się do tej długości.

### Start i koniec pomiaru

- Po włączeniu zasilania, po przerwie w danych z czujnika i po zwolnieniu USB
  wyzwalacz jest wyłączony przez 1 s (rozgrzewka filtra).
- Gdy długość wektora osiągnie próg, powstaje plik `/MEAS/Mnnnn.CSV`. Zawiera
  **1 s przed wyzwoleniem** (pre-roll) — te próbki mają ujemny czas.
- Pomiar kończy się, gdy sygnał jest **poniżej progu przez 3 s** (cisza).
  Plik dostaje stopkę i się zamyka; rejestrator wraca do `CZUWAM`.
- **Maksymalny czas pomiaru: 30 s** od wyzwolenia (plik ma więc najwyżej
  ok. 31 s razem z pre-rollem). Gdy drgania trwają dłużej, plik jest zamykany
  po 30 s, a kolejny pomiar rusza dopiero po zwykłej ciszy (3 s poniżej progu),
  więc ciągła wibracja nie tworzy serii plików jeden po drugim.
- Gdy plik nie może zostać otwarty, kolejny Start jest ignorowany przez 1 s.

### USB

1. Podłącz kabel USB do komputera. Gdy komputer wyliczy urządzenie (i magistrala
   nie jest uśpiona), pomiary się zatrzymują; trwający pomiar jest najpierw
   zamykany ze stopką, dopiero potem karta jest oddawana.
2. Karta pojawia się jako dysk (producent `WIBROMTR`, produkt `VIBRACJE` w
   danych urządzenia; nazwa woluminu to etykieta samej karty). Pliki są w
   `/MEAS`.
3. **Wysuń** dysk w systemie (albo odłącz kabel). Płytka montuje kartę od
   nowa i wraca do czuwania; pomiary ruszają po ok. 1 s rozgrzewki.

Port szeregowy działa równolegle (ta sama wtyczka USB): co sekundę wypisuje
stan (próbki/s, wartości dynamiczne, próg, tryb, stan karty, liczniki utraconych
próbek). Brak karty przy starcie oznacza, że dysk USB nie jest zarejestrowany —
działa wtedy tylko port szeregowy.

## Format pliku

Pliki: `/MEAS/M0001.CSV`, `M0002.CSV`, … Numer = największy istniejący w
`/MEAS` + 1; nigdy nie nadpisuje istniejącego pliku. Płytka nie ma zegara
czasu rzeczywistego, więc czas w pliku jest względny: `t_ms` to milisekundy
od chwili wyzwolenia (ujemne dla pre-rollu), z trzema miejscami po przecinku.
Przyspieszenia w m/s² w osiach czujnika, trzy miejsca po przecinku.

```text
# wibrometr measurement v1
# device: ESP32-S3-Touch-LCD-2, QMI8658
# measurement: 7
# threshold_m_s2: 2.0
# sample_rate_hz: 500.0
# range_g: 8
# pre_trigger_s: 1.0
# quiet_s: 3.0
# trigger_at: t_ms = 0
# columns: t_ms,ax_m_s2,ay_m_s2,az_m_s2
t_ms,ax_m_s2,ay_m_s2,az_m_s2
-1000.000,0.112,-0.087,9.791
-998.000,0.109,-0.090,9.803
-996.000,0.115,-0.085,9.798
...
# end: samples=2750 dropped=0
```

`sample_rate_hz` w nagłówku to **częstotliwość zmierzona** (po ok. 10 s od
startu), a nie nominalne 500: z samym akcelerometrem czujnik taktuje się
wewnętrznym oscylatorem, na tej płytce ok. **491,5 Hz**. Do analizy widma
używaj wartości z nagłówka albo kolumny `t_ms`, która ma krok równy
rzeczywistemu okresowi.

Czujnik numeruje swoje próbki (rejestr TIMESTAMP), więc każda utracona próbka
jest policzona dokładnie: gdy odczyt się spóźnił, przed stopką jest wiersz
`# missed_samples: N`, a w kolumnie `t_ms` w tym miejscu krok jest
wielokrotnością okresu. Przy podłączonym komputerze ginęło ok. 10% próbek
(pomiary są wtedy i tak wstrzymane); ile ginie na akumulatorze, pokaże stopka
pierwszych plików. Port szeregowy podaje licznik od startu (`missed`),
zmierzoną częstotliwość (`rate`) i liczbę napraw karty (`fix`).

(Wartości w przykładzie są ilustracyjne.) Wiersze z `#` to nagłówek i stopka, a zwykły wiersz `t_ms,ax_m_s2,…` to nazwy kolumn;
`dropped` to próbki utracone przy zapisie (kolejka pełna, karta za wolna).
Otwieranie w Pythonie:

```python
import pandas as pd
df = pd.read_csv("M0007.CSV", comment="#")  # kolumny: t_ms, ax_m_s2, ay_m_s2, az_m_s2
```

Arkusze kalkulacyjne pokażą wiersze `#` jako zwykły tekst; nazwy kolumn są w pierwszym wierszu bez `#`.

## Ustawienia stałe

Zmieniane w kodzie (nie na ekranie):

| Ustawienie | Domyślnie | Gdzie |
|---|---|---|
| częstotliwość próbek | 500 Hz nominalnie, zmierzone ~491,5 Hz (więcej się nie da: patrz niżej) | `vib::kSampleRateHz`, `src/vibration/VibConfig.h` |
| zakres | ±8 g (`kRange = 2`) | `vib::kRange`, `VibConfig.h` |
| filtr sprzętowy czujnika | wyłączony: pasmo 137,5 Hz (wbudowany filtr antyaliasingowy 27,5% ODR); z LPF 13,37% byłoby 66,9 Hz | `vib::kLowPass`, `vib::kLowPassMode`, `VibConfig.h` |
| pre-roll | 1 s | `vib::kPreRollUs`, `VibConfig.h` (domyślny argument `RecorderCore`: `include/RecorderCore.h`) |
| maksymalny czas pomiaru | 30 s | `vib::kMaxRecordUs`, `VibConfig.h` (0 w `RecorderCore` = bez limitu) |
| cisza kończąca pomiar | 3 s | `vib::kQuietUs`, `VibConfig.h` (domyślnie też w `TriggerDetector::Config`, `include/TriggerDetector.h`) |
| rozgrzewka wyzwalacza | 1 s | `vib::kWarmupUs`, `VibConfig.h` |
| stała czasowa wolnej średniej (filtr) | 1 s | `src/vibration/main.cpp` (`TriggerDetector::Config`), domyślnie `highPassTauS` w `include/TriggerDetector.h` |
| przerwa po odmowie otwarcia pliku (holdoff) | 1 s | `holdoffUs` w konstruktorze `RecorderCore`, `include/RecorderCore.h` |
| próg domyślny / zakres | 2,0 / 0,2–40,0 m/s² | `include/ThresholdUi.h` |

Pierścień pre-rollu ma 2048 próbek (ok. 4 s przy 500 Hz), `SampleRing<2048>`
w `include/RecorderCore.h`.

### Dlaczego nie więcej niż 500 Hz

Sprawdzone na tej płytce i w dokumentacji QMI8658A (Rev D):

- sam akcelerometr ma najwyżej **1000 Hz** (kody 2/4/8 kHz to „N/A”; wyższe
  częstotliwości istnieją tylko z żyroskopem, 1793,6 / 3587,2 / 7174,4 Hz);
- **FIFO czujnika gubi próbki** podczas odczytu (rozdz. 8.7: w trybie odczytu
  nowe próbki są odrzucane, a odczyt musi się zmieścić w jednym okresie); przez
  I2C 400 kHz to ~135 µs na próbkę, zmierzone straty 35–40% przy 1–2 kHz. Kod
  FIFO jest w repo wyłączony (`vib::kUseFifo`);
- sygnał „dane gotowe” jest tylko na pinie INT2, który nie jest podłączony,
  więc zostaje odpytywanie co 1 ms (tick systemu).

Zadanie czujnika ma priorytet 23 na rdzeniu 0 (tuż pod zadaniem USB), a dotyk
jest czytany tylko wtedy, gdy kontroler sygnalizuje dotknięcie na pinie INT
(GPIO46). Uśpiony kontroler dotyku trzymał magistralę I2C do ~6 ms przy każdym
odczycie.

Do pasma rzędu kilku kHz (wczesna diagnostyka łożysk) potrzebny jest osobny
czujnik drgań na SPI (np. IIS3DWB).

## Budowanie i wgranie

```powershell
pio run -e waveshare_vibration
pio run -e waveshare_vibration -t upload --upload-port COM6
pio device monitor -e waveshare_vibration --port COM6 --baud 115200
```

Środowisko jest zdefiniowane w `platformio.ini` (`[env:waveshare_vibration]`):
ta sama płytka i platforma co `waveshare`, z USB w trybie TinyUSB (CDC +
MSC). Gdy wgranie się nie udaje: przytrzymaj BOOT, podłącz USB, puść BOOT
(tryb pobierania). Pakiety PlatformIO trafiają do
`${sysenv.TEMP}/vibrationSensor-pio-core`, kompilacja do
`${sysenv.TEMP}/vibrationSensor-pio-build`, jak w
[docs/waveshare-README.md](waveshare-README.md).

## Testy

Bez płytki, na komputerze (g++ z C++17):

```text
g++ -std=c++17 -Wall -Wextra -Iinclude test/vibration_checks.cpp -o vc && ./vc
```

`vibration_checks` obejmuje sekcje: format plików (nagłówek, wiersze próbek
z ujemnym czasem, stopka, ścieżki i numeracja `M0001`–`M9999`), pierścień
próbek, wyzwalacz (rozgrzewka, próg, cisza, przechył bez wyzwolenia),
rdzeń rejestratora (pre-roll, otwarcie/zamknięcie, holdoff, przerwa w danych,
tryb USB), logikę ekranu progu (kroki, przyciski, zakres wykresu,
przytrzymanie), własność karty (`UsbOwnership`, `CardGate`) i oddawanie karty
(`CardHandover`).

## Pierwsze włączenie — niesprawdzone na sprzęcie

Wszystko poniżej jest **niesprawdzone na sprzęcie**:

1. **Wykrywanie hosta USB i oddanie karty:** podłączenie podczas pomiaru →
   plik zamknięty ze stopką `# end: …`; odłączenie w trakcie kopiowania;
   wysunięcie dysku, a potem ponowne zamontowanie.
2. **`msc.begin` z liczbą sektorów karty:** dysk i pliki są widoczne w Windows
   i w Linuksie.
3. **Zapis 500 Hz bez utraconych próbek** (`dropped` w stopce i na porcie
   szeregowym) razem z odświeżaniem ekranu na wspólnym SPI.
4. **Orientacja dotyku:** przyciski −/+ reagują tam, gdzie są narysowane.
5. **Próg dobrany do prawdziwych przejazdów:** zmierz poziom szumu w spoczynku
   (płytka leży nieruchomo; szum QMI8658 to rzędu 0,05–0,1 m/s²) i ustaw
   próg wyraźnie wyżej.
6. **Uśpienie/autosuspend hosta z zamontowanym dyskiem:** host usypiający
   zamontowany wolumin oddaje kartę firmware'owi pod swoim buforowanym
   systemem plików (znane ryzyko; uszkodzenie systemu plików możliwe).
7. **Cały build:** `pio run` **nie był uruchomiony** w środowisku, w którym
   powstał kod (rejestr PlatformIO był niedostępny); z toolchainem ESP32
   skompilowano tylko nowe źródła. Potwierdź `pio run` (CI uruchamia go dla
   wszystkich trzech środowisk), zanim zaufasz binarce.

## Ograniczenia

- Czas względny w pliku to int32 w mikrosekundach (zawinąłby się po ok. 35,8 min;
  przy limicie 30 s nie ma to znaczenia).
- Numeracja kończy się na `M9999`: kolejny pomiar kończy się błędem
  `numeracja pelna`, nic nie jest nadpisywane.
- Szybkie odłączenie USB w trakcie pomiaru zamyka plik ze stopką
  `# end: samples=… dropped=…` (pomiar jest krótszy, ale poprawny).
- Żółte linie progu na wykresie dotyczą pojedynczych osi, a wyzwalacz liczy
  długość wektora, więc wyzwolenie może nastąpić, gdy żadna oś nie przekracza
  linii.
- Karta włożona po starcie: zapis wznawia się sam (próba co 1 s), napis `BRAK KARTY` znika. RESET jest potrzebny tylko dla dysku USB (rejestrowany przy starcie).
- Płytka nie ma zegara czasu rzeczywistego: w plikach nie ma daty.
