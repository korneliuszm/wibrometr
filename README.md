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
| PS0 (WAKE), PS1 | 3V (oba w stanie wysokim = SPI) |
| GND | GND |
| Zasilanie modułu | 3V / 3,3 V, zgodnie z opisem wejścia Twojego GY-BNO085 |

Linie SPI pracują na poziomie 3,3 V. Sprawdź oznaczenia na swoim wariancie
GY; moduły mogą mieć różne regulatory i opisy pinów (MOSI bywa opisane
jako AD0, SA0 lub DI). INT jest wymagany: sterownik czyta dane tylko przy
INT w stanie niskim. RST pozwala zresetować czujnik przy każdej próbie
połączenia. Piny INT/RST można zmienić w stałych `imuInt` / `imuRst`
w `src/main.cpp`. Krótki przewód i wspólna masa.

## Ekran i obsługa

- X czerwony, Y zielony, Z niebieski; jednostka m/s².
- Ostatnie 4 s, skala stała ±20 m/s², nowe dane po prawej.
- Odczyt żądany 100 Hz; ekran docelowo 25 Hz. Nagłówek podaje faktyczną
  liczbę otrzymanych raportów na sekundę, nie zmierzone pasmo czujnika.
- Każdy przedział 40 ms pokazuje min/max oraz połączenie między próbkami.
  To obwiednia pików z odczytów, a nie prezentacja wszystkich punktów.
- POZA SKALA oznacza obcięcie widoku; dane nie są automatycznie skalowane.
- Przerwy w raportach pozostawiają puste przedziały, bez sztucznych zer.
- BRAK BNO085 / RETRY: ponowienie inicjalizacji co około 3 s po błędzie.
- UTRATA PROBEK: przepełnienie kolejki w dowolnym momencie od startu.

**BOOT (GPIO0), akcja przy puszczeniu:**

| Naciśnięcie | Działanie |
|---|---|
| krócej niż 1 s | pauza / wznowienie wykresu |
| 1–3 s | odjęcie ostatniego aktualnego odczytu XYZ jako offsetu |
| od 3 s | skasowanie offsetu, pełne przyspieszenie |

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
Biblioteki: Adafruit BNO08x 1.2.5 i ST7735/ST7789 1.11.0.
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

1. Po starcie potwierdź odczyt około 100 Hz i poprawny wykres na TFT.
2. Obracaj czujnik w spoczynku: składowa grawitacji powinna przechodzić
   między osiami. Pełne przyspieszenie statyczne ma moduł około 9,81 m/s².
3. Sprawdź pauzę, zero i kasowanie offsetu; lekkie stuknięcie powinno
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

100 Hz ogranicza teoretyczną częstotliwość Nyquista do 50 Hz; użyteczne
pasmo zależy dodatkowo od filtrów i raportowania czujnika. Ten tryb nie
wystarcza do szerokopasmowej diagnostyki łożysk. Nie ma FFT, klasyfikacji
usterek, zapisu ani oceny według norm przemysłowych. Sztywność mocowania
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

