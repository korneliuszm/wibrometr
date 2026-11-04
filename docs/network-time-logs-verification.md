# Network/time/logs — odbiór 2026-10-29

## Procedura i oczekiwania

- Parser: PSK, PEAP, TTLS, TLS, BOM/CRLF/komentarze, linie błędów, duplikaty, wymagane pola, limity i ścieżki; wadliwy plik odrzucany atomowo.
- Czas: UTC/lokalny, granica doby, DST, przepełnienie uint32, opóźnione wiersze, bez NTP pusta kolumna, brak uciętego CSV.
- Magazyn: pusty wolumin → logs/; istniejący katalog działa; plik o nazwie logs → diagnostyka, konfiguracja linii nadal działa. Stary nagłówek w logs/ → _old.csv; błąd rotacji/brak miejsca → diagnostyka, brak dopisania nowego formatu do starego. Korzeń bez zmian. Przy zamontowanym USB bez zapisu.
- Pliki sieci: wifi.txt 4096/4097 B; brak/błąd wiersza; skrócony odczyt; PEM brak/pusty/8192/8193 B/NUL/zaszyfrowany klucz → diagnostyka bez sekretów, bez fallback AP.
- Sieć: DHCP, mDNS, HTTP/WebSocket, NTP; zły PSK; brak AP, utrata AP i powrót, zmiana IP; NTP niedostępny → pomiary nadal działają, czas pusty do pierwszej synchronizacji; po synchronizacji działa offline.
- Ekran 7: wszystkie stany, długi SSID/hostname, błąd z wierszem, IP/URL, zegar, bez hasła. Strona: stan aktualizowany bez przeładowania.
- Odciski: STA także bez wifi.txt; uwzględniają podłączony AP; trzy skany, pomiary i kolejka pracują podczas skanów/reconnect.
- Enterprise: PEAP/TTLS/TLS, jawne/domniemane identity, CA poprawny/błędny/bez CA, odmowa auth, reconnect, zimny start bez NTP. Bez infrastruktury RADIUS oznaczyć niezweryfikowane.

## Wyniki

Wypełniane podczas wykonania; żaden opis procedury nie oznacza PASS.

### Testy hostowe — PASS

`bash tools/check-network-host.sh` uruchamia 9 zestawów C++, test rzeczywistego
`WifiScan.cpp` z kontrolowanymi odpowiedziami sterownika, sprawdzenie
`static_assert` w `signal_checks.cpp` oraz dwa testy JS strony. Wszystkie
zakończyły się kodem 0. Node v24.21.0 pochodzi z wbudowanego runtime VS Code
(`ELECTRON_RUN_AS_NODE=1`), ponieważ osobnego `node` nie ma w PATH.

Sprawdzono: PSK/PEAP/TTLS/TLS, błędy konfiguracji, BOM/CRLF, limity, parę
cert/key, ścieżki, pliki PEM 8192/8193 B, NUL/pusty/zaszyfrowany klucz,
escapowanie JSON, wiek zdarzenia, UTC/lokalny/DST, uint32 wrap, zachowanie
CSV, rotację bez utraty wcześniejszych kopii i nieudane operacje plikowe.
Dotychczasowe testy linii, klasyfikacji i przemieszczeń również przeszły.

### Płytka i USB — PASS

Adafruit Feather ESP32-S3 TFT (`239a:811d`), port `/dev/ttyACM0`, MPU6050
`WHO_AM_I=0x68`, przerwanie INT działa. Przed wgraniem wykonano kopię plików
korzenia w `/tmp/wibrometr-usb-before-network`; firmware wgrywany bez
kasowania partycji danych. Odczytano 4 stacje z istniejącego `config.csv`.

Kontrolowana wersja diagnostyczna dopisała dwa wiersze do
`logs/__network_selftest.csv` i `logs/distances_last_run.csv`. Odczyt przez
USB potwierdził:

- po nagłówku dokładnie dwa wiersze, po 22 kolumny;
- pierwsze kolumny `time,timestamp_ms`, timestampy 1000 i 1500;
- lokalny czas syntetycznej epoki różniący się o dokładnie 500 ms;
- identyczną zawartość pliku indywidualnego i wspólnego;
- zachowany oryginał `old_header\nhistoric_row\n` w `_old.csv`;
- wszystkie pierwotne pliki korzenia identyczne bajt po bajcie.

Epoka w tym teście była kontrolowanym wejściem do formattera, nie testem
NTP. Trzy pliki testowe po asercjach usunięto. Tymczasowy kod diagnostyczny
usunięto ze źródeł i zastąpiono firmware wersją produkcyjną. Podczas
montowania dysku diagnostyka potwierdza `usb_host=1`.

### Sieć — PSK

Płytka zgłaszała reason 201 (`NO_AP_FOUND`), gdy sieć była wyłączona,
zachowując pomiar 200 Hz bez zgubionych próbek. Po udostępnieniu sieci
`xiaomi` otrzymała DHCP `10.111.76.22`; strona HTTP została potwierdzona
przez użytkownika. Nie wymagało to przywracania własnego AP płytki.
Komputer pozostał w dotychczasowej sieci; próba odnalezienia SSID przez
NetworkManager nie znalazła go, a skrypt przywrócił poprzednie połączenie.
Na prośbę użytkownika nie podejmowano dalszego przełączania sieci.

### Zmiany po niezależnym przeglądzie

Naprawiono i sprawdzono testami regresji błędny rozmiar bufora nagłówka,
niekompletne pary cert/key, nadpisywanie wcześniejszej kopii przy rotacji,
liczenie nieudanych skanów jako pustych odcisków oraz odczyt konfiguracji
po udostępnieniu woluminu hostowi. Odczyt config/PEM odbywa się teraz przed
`storage::startUsb()`. Obsługa radia i HTTP są rozdzielone.

### Decyzje wykonawcze

- Praca w bieżącym katalogu: sandbox odmówił utworzenia worktree. Testy
  bazowe były poprawne; zmiany nie obejmowały wcześniejszej pracy użytkownika.
- Nagłówek modułu to `ClientNetwork.h`, implementacja `Network.cpp`:
  `Network.h` koliduje z nagłówkiem Arduino. Koszt: różnica nazwy względem planu.
- Dodano małe czyste helpery `NetworkFiles.h`, `NetworkJson.h`, `CsvLog.h`,
  aby testować rzeczywisty kod bez symulowania USB/FatFs.
- Enterprise ma limit biblioteki 64 B dla identity/username/password;
  raportowany błąd zamiast niekończącego się łączenia. Parser zachowuje
  ogólny limit wartości 255 B. Koszt: ograniczenie zgodne z aktualnym API.
- EAP jest jawnie ograniczane do skonfigurowanej metody przez ESP-IDF.
- Kopie rotacji są numerowane (`_old.csv`, `_old2.csv` itd.), aby nie usuwać
  wcześniejszych danych. Koszt: dodatkowe miejsce po zmianach formatu.
- Reconnect jest automatycznie wykonywany przez moduł poza skanami,
  zamiast przez niezależne autoReconnect Arduino. Jednorazowa wewnętrzna
  próba Arduino po pierwszej awarii pozostaje; skaner ponawia błędy i nie
  publikuje z nich pustych obserwacji.
- Strefa czasowa stosowana jest od razu po odczycie konfiguracji, także
  przed DHCP: reset programowy może zachować poprawną epokę zegara.
- Commity zgrupowano po testach zamiast po każdym zadaniu z powodu
  ograniczeń zapisu do metadanych Git w sandboxie.

### Niezweryfikowane sprzętowo

PEAP/TTLS/TLS z prawdziwym RADIUS, walidacja CA przy zimnym starcie,
zaszyfrowany/nieprawidłowy certyfikat po stronie serwera, mDNS z innego
klienta sieci, niezależny test WebSocket, pełny cykl zaniku/powrotu AP ze
zmianą adresu DHCP, wizualny odbiór wszystkich stanów ekranu 7 oraz
rzeczywisty przejazd na linii. Brak miejsca i błędy rotacji sprawdzono na
hostowym systemie plików, nie przez zapełnianie rzeczywistej pamięci płytki.
Nie deklarujemy tych prób jako PASS na podstawie builda lub połączenia PSK.

### Końcowy firmware produkcyjny — PASS

Ostatni upload: `SUCCESS`, hash zapisanych danych zweryfikowany przez
esptool. Flash 1424903 / 3145728 B (45,3%), statyczny RAM
117576 / 327680 B (35,9%). Wersja bez tymczasowego kodu testowego.
Po uruchomieniu odczyt USB potwierdził pusty `logs/` po usunięciu danych
syntetycznych oraz niezmienione wszystkie pierwotne pliki korzenia.

Fragment diagnostyki ostatniego uruchomienia:

```text
logs: /logs ready
config.csv: loaded, 4 stations
network: connected SSID=xiaomi IP=10.111.76.22 http://wibrometr.local/ NTP interval=3600000 ms
network: NTP synchronized
rate=200.0 Hz, drops=0, state=live
network=connected ip=10.111.76.22 time=2026-10-29 20:52:17.441 usb_host=1
```

`NTP synchronized` pochodzi z rzeczywistego callbacku SNTP, nie tylko z
warunku poprawnej epoki po programowym resecie. Interwał jest odczytany z
SNTP API, a strefa lokalna jest poprawna także przed uzyskaniem IP.

Surowe ślady tej sesji pozostawiono w `/tmp/network-final-upload.log`,
`/tmp/network-final-runtime.log`, `/tmp/network-host-tests.log` oraz
`/tmp/network-device-logtest-result.txt`. Nie zawierają haseł ani PEM.
