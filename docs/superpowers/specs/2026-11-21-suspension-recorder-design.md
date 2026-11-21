# Rejestrator drgań zawieszek (Waveshare ESP32-S3-Touch-LCD-2) — projekt

## Cel
Osobny, prosty firmware do pomiaru drgań przejazdów zawieszek. IMU QMI8658
mierzy stale; zapis na kartę SD rusza dopiero po przekroczeniu progu wibracji,
ustawianego dotykiem na LCD. Każdy pomiar to osobny plik CSV. Na ekranie jest
wykres przyspieszeń XYZ. Po podłączeniu do komputera widać zawartość karty SD.

## Cele / nie-cele
- Cele: zapis wyzwalany progiem, jeden plik na pomiar, wykres XYZ, próg z
  ekranu (pamiętany po restarcie), karta SD widoczna jako dysk USB.
- Nie-cele: linia rolkowa, Wi-Fi, WWW, FFT, akumulator, kamera, numer zawieszki.
- Anty-cel: uszkodzenie systemu plików karty przez równoczesny dostęp płytki i
  komputera.

## Ograniczenia
- Kabel USB służy wyłącznie do odbioru plików (decyzja użytkownika), więc
  pomiary i dysk USB wykluczają się.
- Karta SD jest na SPI współdzielonym z ekranem (SCLK 39, MOSI 38, MISO 40,
  CS 41); sterowniki w `src/waveshare/hal/`.
- Istniejące środowiska `feather` i `waveshare` nie mogą się zmienić.

## Podejścia
1. **Nowe środowisko `waveshare_vibration` na gotowych sterownikach — wybrane.**
2. Nowy tryb w środowisku `waveshare` — odrzucone: splątanie z linią rolkową i
   dyskiem FFat.
3. Od zera — odrzucone: sterowniki działają, brak powodu.

## Projekt
**Stany:** CZUWANIE (odczyt 500 Hz, pierścień ok. 1 s, wykres na żywo) →
NAGRYWANIE (po przekroczeniu progu; zapis do `/MEAS/Mnnnn.CSV` wraz z
buforem sprzed wyzwolenia; koniec po 3 s poniżej progu) → CZUWANIE.
USB: po wykryciu hosta pomiary stają, a komputer dostaje kartę przez MSC; po
wysunięciu lub odłączeniu płytka montuje kartę ponownie i wraca do czuwania.
Trwający pomiar jest zamykany przed oddaniem karty.

**Wyzwalacz:** | |a| − g | po filtrze górnoprzepustowym ok. 1 s (przechył
płytki nie wyzwala). Próg domyślnie 2,0 m/s², zakres 0,2–40, krok z
przyspieszeniem przy przytrzymaniu, zapis w NVS.

**Ekran 320×240, jedna strona:** nagłówek (CZUWAM / ● NAGRYWAM czas / USB),
wykres XYZ w czasie z linią progu, pasek progu z przyciskami −/+, licznik
pomiarów, wolne miejsce na karcie.

**CSV:** nagłówek `#` (numer, próg, częstotliwość, zakres, pre-trigger,
cisza), kolumny `t_ms,ax_m_s2,ay_m_s2,az_m_s2` (osie czujnika), czas względem
początku pliku (ujemny przed wyzwoleniem). Numer pomiaru = największy w `/MEAS`
+ 1.

**Moje decyzje (stałe w `AppConfig`):** pre-trigger 1 s, cisza 3 s, filtr 1 s,
brak korekty osi.

## Dla wykonawcy
Logika niezależna od sprzętu (wyzwalacz, pierścień, formatowanie CSV, numeracja,
krok progu) w `include/` z testami na komputerze. Do sprawdzenia na sprzęcie:
MSC na SD przez wspólne SPI, wydajność zapisu 500 Hz, orientacja dotyku.
