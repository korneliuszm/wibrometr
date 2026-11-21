# Rejestrator drgań zawieszek — Waveshare ESP32-S3-Touch-LCD-2

Osobny, prosty firmware (środowisko PlatformIO `waveshare_vibration`) do
pomiaru drgań przejazdów zawieszek. Czujnik QMI8658 mierzy stale (500 Hz), a
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

Jeden ekran 320×240:

```text
y 0    nagłówek: stan rejestratora (rozmiar 2)
y 30   wykres XYZ: dynamiczne przyspieszenie, 5,4 s, linie progu (żółte)
y 180  "pomiarow: N   karta M MB"    osie X Y Z    5.4 s
y 196  [ - ]      PROG 2.0 m/s2      [ + ]
```

Wykres pokazuje przyspieszenie z odjętą wolną średnią (grawitacja i przechył
nie są widoczne), osie czujnika (X, Y, Z w kolorach z legendy). Skala pionowa
dobiera się do progu (±5, 10, 20, 40 lub 80 m/s², tak żeby próg mieścił się
w połowie skali); żółte poziome linie to próg i jego odbicie. Liczba
`pomiarow` to pliki `M*.CSV` w `/MEAS`, a `karta M MB` to rozmiar karty
(wolnego miejsca ekran nie liczy: wymagałoby to wolnego skanu FAT). Bez karty
widać `pomiarow: -   brak karty`.

### Nagłówek

| Napis | Znaczenie |
|---|---|
| `CZUWAM` (zielony) | czujnik działa, karta gotowa, czeka na przekroczenie progu |
| `● NAGRYWAM 3.2s M0007` (czerwona kropka) | trwa zapis pomiaru; czas od wyzwolenia i numer pliku |
| `USB: KARTA U KOMPUTERA` (niebieski) | komputer ma kartę (albo oddawanie jest w toku); nic się nie nagrywa, na wykresie `ZAPIS WSTRZYMANY` |
| `BRAK KARTY - WLOZ I RESET` (żółty) | brak karty przy starcie; zapis rusza sam po włożeniu karty (ponowna próba co 3 s), ale dysk USB pojawi się dopiero po RESET |
| `BLAD: <przyczyna>` (czerwony) | błąd karty, np. `blad zapisu na karte`, `nie mozna utworzyc /MEAS`, `numeracja pelna` |
| `BRAK CZUJNIKA` (żółty) | karta w porządku, ale czujnik nie daje próbek (komunikat jest też na wykresie) |

Kolejność: USB, potem NAGRYWAM (tylko gdy zapis działa), potem problemy z kartą, potem brak czujnika,
na końcu CZUWAM.

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
| częstotliwość próbek | 500 Hz (nie więcej: czujnik jest odpytywany co 1 ms) | `vib::kSampleRateHz`, `src/vibration/VibConfig.h` |
| zakres | ±8 g (`kRange = 2`) | `vib::kRange`, `VibConfig.h` |
| filtr sprzętowy czujnika | 13,37% ODR = 67 Hz | `vib::kLowPassMode`, `VibConfig.h` |
| pre-roll | 1 s | `vib::kPreRollUs`, `VibConfig.h` (domyślny argument `RecorderCore`: `include/RecorderCore.h`) |
| cisza kończąca pomiar | 3 s | `vib::kQuietUs`, `VibConfig.h` (domyślnie też w `TriggerDetector::Config`, `include/TriggerDetector.h`) |
| rozgrzewka wyzwalacza | 1 s | `vib::kWarmupUs`, `VibConfig.h` |
| stała czasowa wolnej średniej (filtr) | 1 s | `src/vibration/main.cpp` (`TriggerDetector::Config`), domyślnie `highPassTauS` w `include/TriggerDetector.h` |
| przerwa po odmowie otwarcia pliku (holdoff) | 1 s | `holdoffUs` w konstruktorze `RecorderCore`, `include/RecorderCore.h` |
| próg domyślny / zakres | 2,0 / 0,2–40,0 m/s² | `include/ThresholdUi.h` |

Pierścień pre-rollu ma 512 próbek (ok. 1 s przy 500 Hz), więc dłuższy pre-roll
wymaga też zmiany `SampleRing<512>` w `include/RecorderCore.h`.

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

- Czas względny w pliku to int32 w mikrosekundach, więc jeden pomiar dłuższy
  niż ok. 35,8 min zawinąłby kolumnę czasu.
- Numeracja kończy się na `M9999`: kolejny pomiar kończy się błędem
  `numeracja pelna`, nic nie jest nadpisywane.
- Szybkie odłączenie USB w trakcie pomiaru zamyka plik ze stopką
  `# end: samples=… dropped=…` (pomiar jest krótszy, ale poprawny).
- Żółte linie progu na wykresie dotyczą pojedynczych osi, a wyzwalacz liczy
  długość wektora, więc wyzwolenie może nastąpić, gdy żadna oś nie przekracza
  linii.
- Karta włożona po starcie: zapis wznawia się sam (próba co 3 s), napis `BRAK KARTY - WLOZ I RESET` znika. RESET jest potrzebny tylko dla dysku USB (rejestrowany przy starcie).
- Płytka nie ma zegara czasu rzeczywistego: w plikach nie ma daty.
