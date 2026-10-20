# Przenośny monitor drgań — Feather ESP32-S3 TFT + GY-BNO085

Pierwsza wersja do obserwacji przyspieszeń ramienia robota podczas ruchu.
Projekt PlatformIO znajduje się w tym katalogu. Płytka: Adafruit **5483**
ESP32-S3 TFT Feather, nie wersja Reverse TFT.

## Połączenia

Czujnik pracuje przez SPI (tryb 3, 1 MHz), na wspólnej magistrali z TFT.

| GY-BNO085 | Feather |
|---|---|
| SCL (SCK) | SCK / GPIO36 |
| AD0 / DI (MOSI) | MOSI / GPIO35 |
| SDA (MISO) | MISO / GPIO37 |
| CS | A5 / GPIO8 |
| INT | A4 / GPIO14 |
| RST | A3 / GPIO15 |
| PS0 (WAKE) | A2 / GPIO16 |
| PS1 | 3V |
| GND | GND |
| Zasilanie modułu | 3V / 3,3 V, zgodnie z opisem wejścia Twojego GY-BNO085 |

Linie SPI pracują na poziomie 3,3 V. Sprawdź oznaczenia na swoim wariancie
GY; moduły mogą mieć różne regulatory i opisy pinów (MOSI bywa opisane
jako AD0, SA0 lub DI). INT, RST i PS0 są wymagane. PS0 nie może być
na stałe podpięty do 3V: firmware trzyma go wysoko podczas resetu (tryb
SPI), a przed każdym zapisem ściąga nisko (WAKE), żeby czujnik
zgłosił gotowość przez INT — bez tego czujnik nie przyjmuje poleceń.
Piny można zmienić w stałych `imuInt` / `imuRst` / `imuWake`
w `src/main.cpp`.

Gdy czujnik nie odpowiada, ekran pokazuje `BNO: BRAK INT` (brak reakcji
INT po resecie — sprawdź INT, RST, zasilanie) albo `BNO: BRAK SPI`
(INT działa, ale brak odpowiedzi — sprawdź PS0, PS1, SCK, MOSI, MISO, CS).
Port szeregowy podaje szczegóły każdej próby. Krótki przewód i wspólna masa.

## Ekran i obsługa

Sześć ekranów przełączanych krótkim naciśnięciem BOOT (numer w prawym górnym
rogu): 1 — przebieg czasowy XYZ, 2/3/4 — FFT osi X/Y/Z, 5 — licznik ruchów
posuwistych osi X, 6 — akumulator. Na ekranie 5 BOOT działa inaczej (niżej).

### 1. Przebieg czasowy

- X czerwony, Y zielony, Z niebieski; jednostka m/s².
- Ostatnie 4 s, skala stała ±20 m/s², nowe dane po prawej.
- Odczyt żądany 150 Hz (stała `reportUs` = 6667 µs w `src/main.cpp`);
  zmierzone maksimum tego czujnika w trybie SPI to ok. 180 Hz, więc 150 Hz
  ma zapas. FFT sięga ok. 75 Hz. Ekran docelowo 25 Hz. Nagłówek podaje faktyczną
  liczbę otrzymanych raportów na sekundę, nie zmierzone pasmo czujnika.
- Każdy przedział 40 ms pokazuje min/max oraz połączenie między próbkami.
  To obwiednia pików z odczytów, a nie prezentacja wszystkich punktów.
- POZA SKALA oznacza obcięcie widoku; dane nie są automatycznie skalowane.
- Przerwy w raportach pozostawiają puste przedziały, bez sztucznych zer.
- BRAK BNO085 / RETRY: ponowienie inicjalizacji co około 3 s po błędzie.
- UTRATA PROBEK: przepełnienie kolejki w dowolnym momencie od startu.

### 2–4. FFT osi X, Y, Z

- Widmo amplitudowe z ostatnich 512 próbek (ok. 3,4 s przy 150 Hz),
  okno Hanna, usunięta składowa stała (grawitacja i offset nie wpływają
  na wykres). Rozdzielczość ok. 0,3 Hz, zakres 0 Hz – połowa faktycznej
  częstotliwości raportów (ok. 75 Hz przy 150 Hz). Oś Hz skaluje się
  według zmierzonego tempa raportów, więc przy mniejszej częstotliwości
  pokazuje mniejszy zakres. Czas próbek pochodzi z zegara czujnika
  (znacznik czasu każdego raportu), a nie z chwili odbioru pakietu.
  Czujnik pakuje wiele raportów w jeden pakiet SPI; firmware odbiera
  każdy z nich własną funkcją zwrotną, bo biblioteka Adafruit zachowuje
  tylko ostatni raport z pakietu.
- Oś pionowa: amplituda sinusoidy w m/s² rysowana jako linia, skala sztywna 0–5 (stała
  `fftRange`); większe prążki są obcinane. Wiersz statusu podaje
  częstotliwość i amplitudę największego prążka.
- ZBIERANIE n/512: okno jeszcze się wypełnia (po starcie, wznowieniu
  pauzy lub przerwie w raportach dłuższej niż 100 ms).

### 5. Ruchy osi X (licznik)

Licznik pełnych cykli „tam i z powrotem” wzdłuż osi X czujnika, np. przy
ruchach posuwistych ręką trzymającą czujnik, oraz tempo w cyklach na minutę.
- Z sygnału X usuwana jest wolna składowa (filtr ok. 1 s), więc grawitacja
  i powolne przechylanie ręki nie wpływają na wynik.
- Cykl liczy się, gdy przyspieszenie przejdzie poniżej `-próg`, a potem powyżej
  `+próg`. Próg to 40% ostatniej amplitudy (minimum 0,5 m/s²), więc liczą się
  zarówno delikatne, jak i mocne ruchy; szum czujnika nie liczy się wcale.
- Pasek pokazuje bieżący przebieg X względem progu (żółte znaczniki).
- Licznik działa niezależnie od pauzy na innych ekranach.
- **BOOT na tym ekranie:** krótkie naciśnięcie kasuje licznik, przytrzymanie
  ≥1 s przechodzi do następnego ekranu.
- Licznik zlicza cykle w osi X czujnika, nie w osi ruchu ręki: trzymaj czujnik
  tak, aby kierunek ruchu pokrywał się z osią X.

### 6. Akumulator

- Procent naładowania, napięcie ogniwa i pasek stanu z układu pomiaru
  na płytce (I2C, GPIO42/41). Odczyt co 2 s.
- Nowsze płytki 5483 mają MAX17048: stan ładowania wynika ze znaku
  szybkości zmiany naładowania (%/h): LADOWANIE, ROZLADOWANIE,
  NALADOWANY lub BEZ ZMIAN. Ładowarka nie ma wyjścia statusu na GPIO,
  więc to jedyna dostępna informacja; po starcie wartość ustala się
  przez pewien czas.
- Starsze płytki z LC709203F pokazują tylko procent i napięcie — ten
  układ nie odróżnia ładowania od rozładowania.
- BRAK ODCZYTU UKLADU POMIARU: układ nie odpowiada (np. brak akumulatora,
  bo MAX17048 jest zasilany z ogniwa). Ponowna próba co 5 s.

**BOOT (GPIO0), akcja przy puszczeniu:**

| Naciśnięcie | Działanie |
|---|---|
| krócej niż 1 s | następny ekran |
| 1–3 s | pauza / wznowienie wykresu i FFT |
| od 3 s | ustawienie zera (odjęcie ostatniego odczytu XYZ) albo, gdy zero jest ustawione, powrót do pełnego przyspieszenia |

Pauza zatrzymuje tylko widok; odczyt i status działają dalej. Wznowienie
i zmiana offsetu czyszczą historię. Na ekranie pojawia się wskazówka
zależna od czasu przytrzymania. RESET płytki restartuje firmware.
Nie trzymaj BOOT podczas resetu, jeśli nie chcesz uruchomić bootloadera.
Płytka 5483 ma tylko jeden przycisk użytkownika; nie używamy D1/D2.

## Budowanie i wgranie

PlatformIO Core lub rozszerzenie PlatformIO w VS Code:

```powershell
pio run
pio run -t upload --upload-port COM6
pio device monitor --port COM6 --baud 115200
```

COM6 to port wykryty podczas tworzenia projektu; może zmienić się po
resecie lub na innym komputerze. Sprawdzenie: `pio device list`.
Po wgraniu monitor powinien podawać połączenie z IMU, częstotliwość raportów
i liczbę utraconych próbek. Jeżeli automatyczny upload nie działa:
przytrzymaj BOOT, naciśnij RESET, puść BOOT, sprawdź port i ponów upload.

Platforma jest przypięta do pioarduino 55.03.36 (Arduino 3.3.6, IDF 5.5.2).
Biblioteki: Adafruit BNO08x 1.2.5, ST7735/ST7789 1.11.0 i LC709203F 1.3.4.
Pakiety PlatformIO trafiają do ${sysenv.TEMP}/vibrationSensor-pio-core;
pliki kompilacji do ${sysenv.TEMP}/vibrationSensor-pio-build.
Oba katalogi znajdują się poza Dyskiem Google i ścieżką zawierającą polskie
znaki (linker nie obsługuje jej poprawnie). Pierwsza instalacja
pobiera kompilator i framework. Nie dodawać .pio do git.

## Testy i sprawdzenie na sprzęcie

Testy `test/signal_checks.cpp` wykonują rzeczywistą logikę jako
C++17 constexpr/static_assert: min/max, przerwy, kolejność po zawinięciu
bufora, offset, czyszczenie i rollover czasu. Można je sprawdzić bez
podłączonej płytki dowolnym kompilatorem C++17:

```text
g++ -std=c++17 -Iinclude -fsyntax-only test/signal_checks.cpp
```

1. Po starcie potwierdź odczyt ok. 150 Hz (nagłówek ekranu i `rate=` na porcie szeregowym) i poprawny wykres na TFT.
2. Obracaj czujnik w spoczynku: składowa grawitacji powinna przechodzić
   między osiami. Pełne przyspieszenie statyczne ma moduł około 9,81 m/s².
3. Sprawdź przełączanie ekranów, pauzę, zero i kasowanie offsetu; lekkie stuknięcie powinno
   być widoczne jako pik. Sprawdź też zachowanie poza skalą.
4. Po wyłączeniu zasilania zmień połączenia i sprawdź start bez czujnika,
   a następnie ponowny start z czujnikiem. Test zaniku podczas pracy
   wykonaj przez przełącznik testowy/odpowiednie złącze, bez zwierania pinów.
5. Sprawdź raporty przez kilka minut w docelowym montażu i na przewodzie
   docelowej długości. Oczekuj drops=0; zanotuj spadki częstotliwości.

## Interpretacja i ograniczenia

Raport SH2_ACCELEROMETER zawiera grawitację i przyspieszenia ruchu robota.
Ustawienie zera odejmuje stały wektor, a nie zmienną grawitację podczas
obrotu ramienia ani przyspieszenia trajektorii. Magnes mocujący może
zaburzać magnetometr; ta wersja nie korzysta z orientacji magnetycznej.

150 Hz daje częstotliwość Nyquista 75 Hz; użyteczne pasmo zależy
dodatkowo od filtrów wewnątrz czujnika i jego faktycznego tempa raportów
(sprawdź `rate=` na porcie szeregowym). Ten tryb nie
wystarcza do szerokopasmowej diagnostyki łożysk. FFT to podgląd do połowy
częstotliwości raportów liczony z czasów odbioru raportów; nie ma klasyfikacji usterek,
zapisu ani oceny według norm przemysłowych. Sztywność mocowania
magnetycznego i przewód również wpływają na wynik.

SPI omija znane problemy BNO08x z clock stretching I2C na ESP32-S3.
Każda próba połączenia resetuje czujnik przez RST; nie gwarantuje to
odzyskania ze wszystkich stanów i czasem konieczne jest odłączenie
zasilania samego modułu. Odczyt pracuje w osobnym zadaniu, aby
inicjalizacja IMU nie zatrzymywała TFT; transakcje SPI czujnika i ekranu
są szeregowane przez sterownik magistrali.

Dokumentacja:
- [Feather: piny, zasilanie, TFT i przyciski](https://learn.adafruit.com/adafruit-esp32-s3-tft-feather/pinouts)
- [BNO085: połączenie SPI](https://learn.adafruit.com/adafruit-9-dof-orientation-imu-fusion-breakout-bno085/arduino)
- [Platforma 55.03.36](https://github.com/pioarduino/platform-espressif32/releases/tag/55.03.36)

