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

**Wyzwalacz:** długość wektora (a − wolna średnia, tau 1 s), czyli
dynamiczna część przyspieszenia (przechył płytki nie wyzwala, a poziome
drgania tak; patrz „Decyzje z wdrożenia”). Próg domyślnie 2,0 m/s², zakres
0,2–40, krok zależny od wartości przy przytrzymaniu, zapis w NVS.

**Ekran 320×240, jedna strona:** nagłówek (CZUWAM / ● NAGRYWAM czas / USB),
wykres XYZ w czasie z linią progu, pasek progu z przyciskami −/+, licznik
pomiarów, rozmiar karty w MB.

**CSV:** nagłówek `#` (numer, próg, częstotliwość, zakres, pre-trigger,
cisza), wiersz `# columns:` oraz zwykły wiersz nazw kolumn `t_ms,ax_m_s2,ay_m_s2,az_m_s2` (osie czujnika; pandas `comment='#'` i arkusze dostają nazwy kolumn), czas względem
początku pliku (ujemny przed wyzwoleniem). Numer pomiaru = największy w `/MEAS`
+ 1. Plik kończy stopka `# end: samples=… dropped=…`.

**Moje decyzje (stałe w `src/vibration/VibConfig.h`):** pre-trigger 1 s, cisza 3 s, filtr 1 s,
brak korekty osi.

## Dla wykonawcy
Logika niezależna od sprzętu (wyzwalacz, pierścień, formatowanie CSV, numeracja,
krok progu) w `include/` z testami na komputerze. Do sprawdzenia na sprzęcie:
MSC na SD przez wspólne SPI, wydajność zapisu 500 Hz, orientacja dotyku.

## Decyzje z wdrożenia
Zmiany i doprecyzowania względem tekstu powyżej, podjęte w trakcie realizacji
(opis dla użytkownika: `docs/vibration-README.md`).

1. **Metryka wyzwalacza:** długość wektora (a − wolna średnia, tau 1 s), a nie
   |a| − g po filtrze. Odporność na wolny przechył ta sama, ale wykrywa też
   drgania poziome. Rozgrzewka 1 s po włączeniu zasilania, po przerwie w
   danych z czujnika i po zwolnieniu USB; po odmowie otwarcia pliku Start jest
   ignorowany przez 1 s (holdoff).
2. **Ekran pokazuje rozmiar karty w MB,** a nie wolne miejsce (wolne miejsce
   wymaga wolnego skanu FAT).
3. **Przytrzymanie przycisku progu:** krok od razu, po 500 ms powtarzanie co
   100 ms; wielkość kroku 0,1 poniżej 5,0, 0,5 od 5,0 do 20,0, 1,0 powyżej
   (zamiast „przyspieszenia”).
4. **Brak karty przy starcie:** ekran pokazuje „BRAK KARTY - WLOZ I RESET”;
   napis dotyczy braku karty przy starcie. Zapis wznawia się sam po włożeniu
   karty (ponowna próba montowania co 3 s); RESET jest potrzebny tylko dla
   dysku USB (USB MSC jest rejestrowane przy starcie).
5. **„USB podłączone”** = host wyliczył urządzenie, a magistrala nie jest
   uśpiona (`tud_mounted() && !tud_suspended()`); niesprawdzone na sprzęcie.
   Znane ryzyko: host usypiający zamontowany wolumin (uśpienie/autosuspend)
   oddaje kartę firmware'owi pod buforowanym przez siebie systemem plików.
6. **Oddanie karty** następuje dopiero po zamknięciu trwającego pomiaru i
   zaparkowaniu zapisu; transfer USB w toku blokuje ponowne zamontowanie
   (`CardGate`/`CardHandover`), a po powrocie zapis montuje kartę od nowa
   (`SD.end`/`SD.begin`).
7. **Ograniczenia:** czas względny to int32 w mikrosekundach, więc jeden
   pomiar dłuższy niż ok. 35,8 min zawinąłby kolumnę czasu; numeracja kończy
   się na M9999 (błąd „numeracja pelna”, nic nie jest nadpisywane); szybkie
   odłączenie w trakcie pomiaru zamyka plik ze stopką `# end: samples=…
   dropped=…`.
