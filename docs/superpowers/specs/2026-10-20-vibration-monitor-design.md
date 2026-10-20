# Monitor drgań — zatwierdzona pierwsza wersja

Feather ESP32-S3 TFT 5483, GY-BNO085 przez I2C: SDA 42, SCL 41.
Mocowanie magnetyczne na ramieniu robota podczas ruchu. PlatformIO / C++ / Arduino.
Wykres X czerwony, Y zielony, Z niebieski, m/s². Cel: odczyt 100 Hz,
odświeżanie 25 Hz. BOOT: krótko pauza widoku, 1–3 s odejmowanie bieżącego offsetu.
Komunikat utraty danych i ponowne próby inicjalizacji.

Decyzje implementacyjne: 4 s historii, stała skala ±20 m/s², min/max
każdej osi w przedziałach 40 ms, aby zachować krótkie piki.
BOOT przytrzymany od 3 s kasuje offset (płytka 5483 ma jeden przycisk użytkownika). Pauza nie zatrzymuje odczytu; wznowienie czyści historię.
Zmiana offsetu również czyści historię. Brak danych pozostawia przerwę.
Piki poza skalą są sygnalizowane. Częstotliwość raportów mierzona na żywo.

Offset nie rozdziela drgań od przyspieszeń trajektorii ani zmiennej grawitacji.
BNO085 ma znane problemy I2C z ESP32-S3. Nowszy ESP-IDF pomaga w obsłudze
clock stretching, ale wymaga sprawdzenia na sprzęcie. Bez FFT, WiFi i zapisu.
To pierwsza wersja funkcjonalna, bez kwalifikacji przemysłowej.

Walidacja: testy C++ logiki historii, offsetu, braków i min/max; kompilacja;
na urządzeniu przyciski, TFT, odczyt, zanik danych i ponowne podłączenie.


