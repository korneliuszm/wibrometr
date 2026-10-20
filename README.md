# Przenośny monitor drgań — Feather ESP32-S3 TFT + GY-BNO085

Pierwsza wersja do obserwacji przyspieszeń ramienia robota podczas ruchu.
Projekt PlatformIO znajduje się w tym katalogu. Płytka: Adafruit **5483**
ESP32-S3 TFT Feather, nie wersja Reverse TFT.

## Połączenia

| GY-BNO085 | Feather |
|---|---|
| SDA | GPIO42 |
| SCL | GPIO41 |
| GND | GND |
| Zasilanie modułu | 3V / 3,3 V, zgodnie z opisem wejścia Twojego GY-BNO085 |

Linie I2C pracują na poziomie 3,3 V. Sprawdź oznaczenia zasilania na swoim
wariancie GY; moduły mogą mieć różne regulatory i opisy pinów.
Moduł musi być ustawiony w tryb I2C według jego pinów PS0/PS1.
Firmware szuka adresów 0x4A i 0x4B. INT i RST nie są wymagane w tej wersji.
Nie sterujemy zasilaniem zewnętrznego modułu przy odzyskiwaniu połączenia.
Krótki przewód i wspólna masa; Feather ma już rezystory podciągające I2C.

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
Po wgraniu monitor powinien podawać adres IMU, częstotliwość raportów
i liczbę utraconych próbek. Jeżeli automatyczny upload nie działa:
przytrzymaj BOOT, naciśnij RESET, puść BOOT, sprawdź port i ponów upload.

Platforma jest przypięta do pioarduino 55.03.36 (Arduino 3.3.6, IDF 5.5.2).
Nowszy IDF wybrano ze względu na clock stretching na ESP32-S3.
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

BNO08x ma znane problemy czasowe I2C z ESP32/ESP32-S3. Nowy sterownik,
timeout i retry nie gwarantują odzyskania ze wszystkich stanów;
czasem konieczny jest reset lub odłączenie zasilania samego modułu.
Odczyt pracuje w osobnym zadaniu, aby inicjalizacja IMU nie zatrzymywała TFT.

Dokumentacja:
- [Feather: piny, zasilanie, TFT i przyciski](https://learn.adafruit.com/adafruit-esp32-s3-tft-feather/pinouts)
- [BNO085: ograniczenia I2C](https://learn.adafruit.com/i2c-addresses/troublesome-chips)
- [Platforma 55.03.36](https://github.com/pioarduino/platform-espressif32/releases/tag/55.03.36)

