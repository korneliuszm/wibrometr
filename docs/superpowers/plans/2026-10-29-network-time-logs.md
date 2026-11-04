# Network, time and logs — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Połączyć wibrometr z siecią użytkownika WPA2-PSK lub WPA2-Enterprise, dodać lokalny czas NTP do logów i zapisywać nowe logi w `logs/` na dysku USB.

**Architecture:** `net` w `src/Network.*` przejmuje radio, konfigurację sieci, certyfikaty, reconnect, mDNS i SNTP; `weblink` zachowuje HTTP/WebSocket. Parser konfiguracji i przeliczanie czasu są czystym C++, testowanym na komputerze. Tracker zachowuje dotychczasowy CSV i kolejkę; warstwa integracji dodaje kolumnę `time` przed zapisem przez istniejący POSIX VFS `/ffat`.

**Tech Stack:** C++17, PlatformIO `feather`, Arduino-ESP32 3.3.6 / ESP-IDF dostarczony przez przypięty pioarduino 55.03.36, WiFi, ESPmDNS, SNTP, POSIX VFS/FatFs, ESPAsyncWebServer, hostowe testy `g++` i Node.js.

**Spec:** `docs/superpowers/specs/2026-10-29-network-time-logs-design.md` (zaakceptowany projekt z 2026-10-29).

## Global Constraints

- Źródło czasu: „tylko NTP z sieci (bez modułu RTC)”.
- „Płytka nie tworzy własnego AP” — tryb `WIFI_STA`, również bez poprawnego `wifi.txt`, aby skany odcisków działały offline.
- `wifi.txt`, `config.csv` i `cert/` pozostają w korzeniu dysku; „`logs/` jest zakładany przy starcie. Stare logi w katalogu głównym nie są ruszane.”
- Parser: „`#` zaczyna komentarz”, BOM i CRLF dozwolone; nieznany klucz albo brak `=` odrzuca cały plik z numerem wiersza.
- Metody `peap`, `ttls`, `tls`; brak `eap` oznacza WPA2-PSK. `identity` domyślnie równe `username`; TLS wymaga `identity`, `cert`, `key`; PEAP/TTLS wymagają `username`, `password`; `ca` jest opcjonalne, `cert`/`key`/`ca` bez `eap` są błędem.
- Certyfikaty: ścieżki względne do korzenia, PEM, „Plik certyfikatu do 8 KB”; klucz prywatny niezaszyfrowany. Bufory PEM żyją przez cały czas działania.
- Domyślne: `ntp=pool.ntp.org`, `tz=CET-1CEST,M3.5.0,M10.5.0/3`, `hostname=wibrometr`.
- Czas poprawny, gdy `time()` > 2024-01-01; pierwsza kolumna `time` ma format `RRRR-MM-DD GG:MM:SS.mmm`, pusta bez synchronizacji; `timestamp_ms` zostaje.
- Czas wiersza = czas teraz − (zegar trackera teraz − `timestamp_ms` wiersza). Nie podstawiać `millis()` w miejsce zegara trackera.
- NTP po uzyskaniu IP; odświeżanie co godzinę; strona pod DHCP IP i `<hostname>.local`.
- Nie zapisywać na woluminie, gdy należy do hosta USB; zachować dotychczasowe reguły `config.csv`, śledzenia linii i kolejkowania logów.
- Poza zakresem: RTC, zaszyfrowane klucze / `key_password`, wysyłanie logów przez sieć, aktualizacja bibliotek i przebudowa prototypu indoor positioning.

## Review Focus

1. Utrata sieci, nowe IP i wielokrotne reconnecty: aktualny stan, ponowne mDNS/NTP, jeden serwer WWW, brak AP i brak zatrzymania próbkowania (zadania 4–5, odbiór 7).
2. Wi-Fi fingerprint podczas łączenia i po rozłączeniu: skany nadal działają, uwzględniają także AP sieci klienta i nie uruchamiają konkurencyjnych prób reconnect w środku skanu (zadania 4–5, odbiór 7).
3. Opóźnione wiersze i kolejka próbek: wiek wiersza wynika z tego samego zegara trackera, z poprawnymi milisekundami przy przejściu przez sekundę/dobę/DST (zadania 2, 6).
4. Zbyt długi/uszkodzony plik lub PEM, niedozwolona ścieżka: odrzucenie całej konfiguracji, czytelny błąd, brak ucięcia i brak hasła/klucza w diagnostyce (zadania 1, 4, 7).
5. Zajęty USB, brak miejsca i kolizja `logs` z plikiem: brak zapisu w korzeniu jako obejścia; zachowanie starych logów i brak dopisania do błędnego nagłówka po nieudanej rotacji (zadanie 3, odbiór 7).

## Ustalenia z rozpoznania kodu

- `src/WebLink.cpp:84` uruchamia dziś AP+STA i serwer; `src/WebLink.h` publikuje stałe SSID/hasło; `drawWeb()` w `src/main.cpp:702` pokazuje te dane.
- `src/WifiScan.cpp:32` pomija SSID `Wibrometr`; usunąć to wraz z zależnością od `WebLink.h`. Nazwa produktu na stronie może pozostać.
- `storage::begin()` montuje partycję bezpośrednio przez `esp_vfs_fat_spiflash_mount`; nie używać równolegle `FFat.begin()` ani fikcyjnego obrazu USB z historycznego planu Spec A.
- `storage::appendLogRow()` zapisuje wspólny i indywidualny CSV; `appendTo()` porównuje nagłówek i wykonuje rotację `_old.csv`.
- `Tracker` formatuje surowe wiersze w kilku miejscach i kolejkuje osiem wpisów (`csv[256]`). `serviceLine()` odbiera je przez `takeDistanceRow()`. Najmniejsza zmiana: dodawać czas dopiero po odebraniu, zachowując surowy format i testy trackera.
- `clock_` to suma `dt` próbek, nie `millis()`; dodać accessor z identycznym przeliczeniem jak przy formatowaniu `timestamp_ms`.
- Lokalny `WiFiSTA.h` potwierdza overload Enterprise ze specyfikacji; `esp32s3/sdkconfig` ma `CONFIG_ESP_WIFI_ENTERPRISE_SUPPORT=y`.
- **Korekta założenia technicznego:** lokalny `sdkconfig` ma `CONFIG_LWIP_SNTP_UPDATE_DELAY=10800000` (3 h). Implementacja jawnie ustawi `3600000` ms, aby spełnić wymagane odświeżanie godzinne.

## Struktura plików i zależności

| Plik | Rola |
|---|---|
| nowy `include/WifiConfig.h` | typy konfiguracji, parser i walidacja bez Arduino |
| nowy `test/wifi_config_checks.cpp` | konfiguracje PSK/EAP i błędy |
| istniejący `include/DistanceLog.h` | czyste helpery epoch/wiek/format oraz dodanie pierwszej kolumny |
| istniejący `test/distance_log_checks.cpp` | testy przeliczenia i końcowego CSV |
| istniejący `include/LineTracker.h`, `test/line_checks.cpp` | accessor zegara, zachowanie kolejki i regresja logów |
| istniejący `src/UsbStorage.*` | `logs/`, wspólny nagłówek, bezpieczna rotacja |
| nowy `src/Network.h`, `src/Network.cpp` | pliki Wi-Fi/PEM, STA, zdarzenia, retry, DHCP, mDNS i NTP |
| istniejący `src/WebLink.*` | sam serwer i publikacja stanu sieci |
| istniejący `src/WifiScan.cpp`, `src/main.cpp` | skany, inicjalizacja, ekran 7, zapis czasu |
| nowe `docs/wifi.example.txt`, `docs/network-time-logs-verification.md`; istniejący `README.md` | konfiguracja i procedura odbioru |

Kolejność: 1 → 4 → 5; 2 i 3 → 6; całość → 7. To jeden plan integracyjny z oddzielnie sprawdzalnymi modułami, bez uruchamiania implementacji w ramach planowania.

## Decyzje wykonawcze uzupełniające specyfikację

- Parser jest atomowy: wynik podmieniany dopiero po pełnej walidacji. Duplikaty kluczy są błędem, klucze i wartości `eap` rozróżniają wielkość liter. Brakujące wymagane pole: wiersz 0; błędna wartość lub konflikt: wiersz deklaracji. Zapamiętać numery wierszy pól także dla diagnostyki plików PEM.
- Pierwszy `#` kończy linię, pierwsze `=` dzieli klucz od wartości; kolejne `=` zostają w wartości. Składnia nie obsługuje cytowania ani escape. Opisać ograniczenie `#` w haśle.
- Limity proponowane do implementacji: `wifi.txt` maks. 4096 bajtów, SSID 32 bajty, pozostałe wartości maks. 255 bajtów, hostname 1–63 znaków ASCII liter/cyfr/myślnika, bez myślnika na końcach. Przekroczenie odrzuca plik, nigdy nie ucina. Hasło PSK musi być niepuste; ostateczną zgodność ze sterownikiem sprawdza połączenie. Nie narzucać limitów PSK hasłom EAP.
- Ścieżki CA/cert/key nie mogą być absolutne ani zawierać `..`, `.` jako segmentu lub `\\`; poprawne ścieżki są rozwiązywane pod `/ffat/`. PEM może mieć dokładnie 8192 bajty plus dodatkowy NUL w buforze. Pusty plik / wewnętrzny NUL / brak właściwych znaczników PEM to błąd z nazwą pliku. Zaszyfrowany klucz odrzucić jawnie.
- `Network` czyta POSIX pliki pod `/ffat` wyłącznie przy starcie, zgodnie z dotychczasowym sposobem odczytu `config.csv`. Nie dodawać odczytu/reload po zmianie pliku przez USB; wymagany RESET. Obsługa pełnej koordynacji USB i VFS pozostaje istniejącym ograniczeniem projektu, sprawdzanym podczas odbioru.
- Bez poprawnego Wi-Fi STA pozostaje dostępne do skanów, ale nie ma połączenia ani fallback AP. Brak magazynu to stan błędu sieci, niezależny od błędu konfiguracji linii.
- Stan zegara zależy od ważności zegara systemowego, a nie bieżącego `Connected`; po synchronizacji zegar pracuje dalej po utracie Wi-Fi, do restartu.
- Zachować jeden plik `_old.csv` zgodnie z dotychczasową konwencją, ale sprawdzać błędy `remove/rename`; nie dopisywać nowego formatu do starego po błędzie rotacji. Nie zmieniać historycznych plików w korzeniu.

---

### Task 1: Czysty parser `wifi.txt`

**Files:** Create `include/WifiConfig.h`, `test/wifi_config_checks.cpp`, `docs/wifi.example.txt`.

**Interfaces:** Produces namespace `net`: `enum class Eap { None, Peap, Ttls, Tls };`, `struct WifiConfig`, `struct ConfigError { int line; const char *what; };`, `bool parseWifiConfig(const char *text, WifiConfig &out, ConfigError *err = nullptr)`.
`WifiConfig` ma `std::string ssid, password, identity, username, ca, cert, key, ntp, tz, hostname`, `Eap eap` i numery wierszy `caLine`, `certLine`, `keyLine`. Wartości domyślne zgodne z Global Constraints; `what` to stały opis bez sekretów.

- [ ] **Step 1: Napisać testy parsera.** Styl jak `station_config_checks.cpp`, bez Arduino. Nazwy/scenariusze i asercje:
  - `psk_defaults`: `ssid=xiaomi\npassword=test-password\n` → sukces, `Eap::None`, domyślne NTP/TZ/hostname.
  - `enterprise_modes`: osobne poprawne PEAP, TTLS (login/hasło/CA, identity domyślnie login), TLS (identity/cert/key, bez username/password), EAP bez CA też poprawne.
  - `syntax`: BOM, CRLF, puste wiersze, komentarze samodzielne i końcowe, trim; `password=a=b` → `a=b`; komentarz po wartości jest obcięty.
  - `error_lines`: nieznany klucz w fizycznym wierszu 3 → `line==3`; brak `=` → właściwy wiersz; nieznane `eap`, duplikat, przekroczenia limitów i hostname z odstępem odrzucone.
  - `required_fields`: brak/empty SSID; brak hasła PSK; PEAP/TTLS bez username lub password; TLS bez identity/cert/key; cert/key/ca bez eap → false; zachowane poprzednie `out` po błędzie.
  - `paths_and_limits`: `cert/ca.pem` poprawne, `/ca.pem`, `../ca.pem`, `cert/../ca.pem`, `cert\\ca.pem` błędne; SSID 32 bajty poprawne, 33 błędne; wartość 255 poprawna, 256 błędna.
- [ ] **Step 2: Uruchomić** `g++ -std=c++17 -Wall -Wextra -Iinclude test/wifi_config_checks.cpp -o /tmp/wifi_config_checks && /tmp/wifi_config_checks`. Najpierw FAIL (brak parsera).
- [ ] **Step 3: Zaimplementować interfejs i walidację** w `WifiConfig.h`; w pliku przykładowym aktywny PSK z hasłem zastępczym, pozostałe warianty w komentarzach. Nie wpisywać rzeczywistych danych dostępowych.
- [ ] **Step 4: Powtórzyć komendę** — wszystkie asercje PASS / `all ok`.
- [ ] **Step 5: Commit** `feat: parse USB client Wi-Fi configuration` (trzy pliki tego zadania).

### Task 2: Przeliczanie czasu i pierwsza kolumna CSV

**Files:** Modify `include/DistanceLog.h`, `test/distance_log_checks.cpp`, `include/LineTracker.h`, `test/line_checks.cpp`.

**Interfaces:** Produces `uint32_t line::Tracker::clockMs() const` = `uint32_t(clock_ * 1000)`; helpers w `line`:
`bool formatEventTime(char *out, size_t size, int64_t nowEpochMs, uint32_t trackerNowMs, uint32_t eventMs)`;
`bool prependLogTime(char *out, size_t size, const char *rawCsv, const char *localTime)`.
`formatEventTime` używa `localtime_r`, ważność epoki `nowEpochMs > 1704067200000LL`; nieważny czas daje pusty string i poprawne zakończenie NUL. `prependLogTime` dodaje `localTime + ',' + rawCsv`, zwraca false przy złym wejściu/braku miejsca. Surowy `formatDistanceRow` i kolejka trackera zachowują dotychczasowy układ.

- [ ] **Step 1: Dodać testy** do `distance_log_checks.cpp`, z `TZ=UTC0` / `tzset()`:
  - `event_time`: epoka `1735689600123`, tracker 10000, zdarzenie 8500 → `2024-12-31 23:59:58.623`.
  - `unsynced_time`: epoka 0 i dokładny próg `1704067200000` → pusty string; pierwszy ms ponad próg → czas.
  - `time_column`: surowy wiersz `1234,transport,...\n` → `2024-12-31 23:59:58.623,1234,transport,...\n`; bez czasu → `,1234,transport,...\n`; końcowy CSV ma jedną końcową nową linię i nie zmienia pozostałych kolumn.
  - `bounds`: bufor za mały → false, NUL w granicach, brak częściowego wiersza przekazanego do zapisu; cały najdłuższy istniejący wiersz mieści się w 288 bajtach.
  - `wrap`: tracker `0x100`, event `0xffffff00` → wiek 512 ms (odejmowanie modulo uint32_t).
  - `local_timezone`: POSIX TZ ze specyfikacji; UTC 2026-10-25 00:59:59 → lokalnie 02:59:59; UTC 01:00:00 → lokalnie 02:00:00. Odejmować wiek przed konwersją lokalną, także przez DST.
  W `line_checks.cpp`: `clockMs()` zgodne z sumą kontrolowanych próbek, czas nie zeruje się po home/breakSignal, najstarszy odebrany CSV ma `timestamp_ms <= clockMs()` przy zwykłej kolejności. Pozostawić regresję liczby/kolejności wierszy.
- [ ] **Step 2: Uruchomić** `g++ -std=c++17 -Iinclude test/distance_log_checks.cpp -o /tmp/distance_log_checks && /tmp/distance_log_checks`; oczekiwany FAIL dla nowych helperów.
- [ ] **Step 3: Dodać accessor i helpery.** Różnicę uint32_t przeliczyć na int64_t przed odejmowaniem od epoki. Podzielić epoch ms na sekundy/resztę po odjęciu wieku, formatować `.mmm` do trzech cyfr. Nie dodawać Arduino/WiFi ani zależności od czasu globalnego do trackera.
- [ ] **Step 4: Uruchomić testy czasu i** `g++ -std=c++17 -O2 -Iinclude test/line_checks.cpp -o /tmp/line_checks && /tmp/line_checks`; oczekiwane `all ok` obu zestawów.
- [ ] **Step 5: Commit** `feat: calculate local timestamps for queued log events`.

### Task 3: Katalog `logs/` i nowy nagłówek

**Files:** Modify `src/UsbStorage.cpp`, `src/UsbStorage.h`; Create `docs/network-time-logs-verification.md` (sekcja magazynu).

**Interfaces:** Preserves `storage::begin()`, `storage::readConfig(...)`, `storage::appendLogRow(const char *file, const char *csvRow)`, `storage::isMounted()`; `appendLogRow` od tej pory otrzymuje CSV z pierwszą kolumną `time`.

- [ ] **Step 1: Zapisać procedurę sprawdzenia** w dokumencie odbioru: pusty wolumin, istniejący `logs/`, plik o nazwie `logs`, nowy CSV, stary nagłówek wewnątrz `logs/`, `_old.csv` już obecny, pełny dysk, USB zajęty. Oczekiwane rezultaty w kolejnych krokach; nie budować symulatora USB/FatFs.
- [ ] **Step 2: Zmienić ścieżki i start.** Po zamontowaniu, przed `USB.begin()`, utworzyć `/ffat/logs` przez `mkdir` i sprawdzić przez `stat`, że to katalog. Istniejący katalog jest sukcesem. Błąd katalogu raportować oddzielnie od dostępności `config.csv` i USB — nie tracić konfiguracji linii przez awarię logowania. `kAllRowsPath=/ffat/logs/distances_last_run.csv`, pliki indywidualne `/ffat/logs/<file>.csv`.
- [ ] **Step 3: Zmienić nagłówek** na `time,timestamp_ms,segment_type,from_station,to_station,station_id,motion_type,axis,measured_distance_m,measured_angle_deg,confidence,quality_flags,declared,delta,duration_s,ramp_m_s2,rms_x,rms_y,rms_z,peak_x,peak_y,peak_z\n`. Zachować pojemność odczytu pierwszej linii większą niż ten nagłówek. W `appendTo` sprawdzić wyniki rotacji i zapisu, raportować ścieżkę i błąd, nie zapisywać nowego formatu przy błędzie rotacji. Wszystkie operacje zapisu pozostają za kontrolą `mounted` i dostępności katalogu.
- [ ] **Step 4: Build** `env -u TEMP /home/korneliusz/.local/share/wibrometr-pio/bin/pio run -e feather`; oczekiwane `SUCCESS`. Odbiór sprzętowy w zadaniu 7: logi tylko w `logs/`, pliki korzenia nietknięte, poprawna rotacja wewnątrz `logs/`, brak zapisu przy zajętym USB.
- [ ] **Step 5: Commit** `feat: store timestamped CSV logs in logs directory`.

### Task 4: Moduł `net` — konfiguracja, STA, Enterprise, NTP i mDNS

**Files:** Create `src/Network.h`, `src/Network.cpp`; Modify `docs/network-time-logs-verification.md` (sekcja sieci).

**Interfaces:** Consumes Task 1 `net::WifiConfig`, `storage::isMounted()` tylko do ochrony dostępu do woluminu. Produces:
- `enum class State { MissingConfig, ConfigError, Connecting, Connected, Disconnected };`
- `struct Status { State state; int errorLine; int disconnectReason; char message[160]; char ssid[33]; char ip[16]; char hostname[64]; bool clockReady; };`
- `void net::begin(bool storageReady)`, `void net::loop(uint32_t nowMs, bool scanning)`, `const net::Status &net::status()`, `int64_t net::epochMs()`.
`status()` czytane wyłącznie w zadaniu Arduino loop; `epochMs()` pobiera `gettimeofday` i oddaje ms (ważność ocenia helper czasu). `begin` nie czeka na DHCP/NTP.

- [ ] **Step 1: Spisać przypadki odbioru przed implementacją:** brak/błąd wifi.txt z wierszem, plik 4096/4097 bajtów, odczyt skrócony, brak PEM (nazwa), PEM 8192/8193 bajty, błędne znaczniki/zaszyfrowany klucz; cztery metody auth; restart bez sieci; odmowa auth; AP znika i wraca; nowy DHCP IP; NTP niedostępny. Każdy stan z właściwą diagnostyką, bez sekretów i bez AP. Nie deklarować testu sprzętowego jako wykonanego po samym buildzie.
- [ ] **Step 2: Zaimplementować odczyt** `/ffat/wifi.txt` i plików PEM przez `fopen/fread`, z kontrolą rozmiaru i pełnego odczytu. Konfigurację i NUL-terminated PEM przechowywać w trwałym stanie modułu (heap, nie stos/callback). Przekazać null dla brakujących opcjonalnych parametrów. `storageReady=false` → diagnostyka braku magazynu; brak `wifi.txt` → MissingConfig, pozostałe błędy → ConfigError; konfiguracja linii pozostaje niezależna.
- [ ] **Step 3: Uruchomić STA.** Wyłączyć utrwalanie konfiguracji sieci przez `WiFi.persistent(false)`, ustawić hostname przed połączeniem. Zarejestrować jeden callback zdarzeń. PSK → `WiFi.begin(ssid,password)`; EAP → overload z `WPA2_AUTH_PEAP/TTLS/TLS`, identity, username, password, ca, cert, key. Użyć `WiFi.setAutoReconnect(true)`. Callback przekazuje zdarzenie/reason przez kolejkę FreeRTOS do `net::loop`; nie modyfikuje współdzielonych `String`, nie czyta plików i nie wykonuje mDNS/NTP.
- [ ] **Step 4: Obsłużyć cykl życia w `net::loop`.** GOT_IP aktualizuje IP/SSID i Connected, rozpoczyna mDNS (`MDNS.begin(hostname)` i HTTP tcp/80) oraz `configTzTime(tz, ntp)`. Po inicjalizacji SNTP jawnie `esp_sntp_set_sync_interval(3600000)` (z `esp_sntp.h`). Disconnect usuwa nieaktualny IP, zapamiętuje reason i ustawia Disconnected; brak GOT_IP jeszcze nie oznacza sukcesu. Odnawiać mDNS po odzyskaniu IP, nie rejestrować usług wielokrotnie. Jeśli sterownik nie odzyska połączenia, własna próba `WiFi.reconnect()` co 10 s, tylko przy poprawnej konfiguracji, rozłączeniu i `!scanning`; nie wykonywać jej co iterację. Czas ważny po synchronizacji także bez Wi-Fi. Zdarzenia utracone przy pełnej kolejce uzupełniać odczytem bieżącego statusu/IP sterownika w loop.
- [ ] **Step 5: Build** jak w zadaniu 3; oczekiwane `SUCCESS`, w tym skompilowane wszystkie trzy gałęzie EAP. Zweryfikować lokalne kontrakty w `libraries/WiFi/src/WiFiSTA.h`, `STA.cpp`, `esp_sntp.h`; nie aktualizować pakietu. Błędy mDNS/NTP raportować bez blokowania pomiarów. Na rzeczywistym Enterprise sprawdzić walidację CA po zimnym starcie bez czasu; nie wyłączać walidacji jako obejścia problemu bootstrappingu NTP.
- [ ] **Step 6: Commit** `feat: connect configured client Wi-Fi with Enterprise and NTP`.

### Task 5: Integracja WWW, skanów i ekranu 7

**Files:** Modify `src/WebLink.h`, `src/WebLink.cpp`, `src/WifiScan.cpp`, `src/main.cpp`; Modify `src/WebPage.h` tylko jeśli zawiera instrukcje łączenia do dawnego AP.

**Interfaces:** Consumes Task 4 `net::*`; preserves wszystkie metody `weblink` poza usunięciem stałych `ssid/password`. `weblink::address()` deleguje do aktualnego `net::status().ip` (puste offline). `weblink::begin()` uruchamia sam HTTP/WebSocket, dokładnie raz. Produces stan sieci w istniejącym JSON wysyłanym do przeglądarki: obiekt `network` z `state`, `ssid`, `ip`, `hostname`, `clock_ready`, `error_line`, `message`, `disconnect_reason`.

- [ ] **Step 1: Przygotować przypadki sprawdzenia UI** w dokumencie odbioru: wszystkie pięć stanów sieci, długi SSID, błąd pliku z numerem wiersza, długi hostname, brak NTP przy działającym DHCP; ekran 240×135 bez hasła, z czytelnym IP/URL i zegarem. Strona zachowuje wykresy i polecenia; nowy klient dostaje aktualny stan, a otwarta strona aktualizacje.
- [ ] **Step 2: Usunąć własność radia z WebLink**: `WiFi.mode`, `softAP`, stałe poświadczenia, statyczny IP `192.168.4.1` i komunikat Serial z hasłem. Serwer uruchomić raz także przed uzyskaniem IP, nie restartować go na reconnect. Przygotowanie buforów/tras pozostaje jak obecnie. Stan sieci serializować z poprawnym escapowaniem znaków JSON SSID/komunikatów; nie dodawać password/PEM do protokołu.
- [ ] **Step 3: Spiąć w main**: zapamiętać wynik `storage::begin()` jako `storageReady`; po odczycie `config.csv` uruchomić `net::begin(storageReady)` oraz `weblink::begin()` dla zwykłego wariantu `!ipsEnabled`. W loop przed obsługą strony wywołać `net::loop(now,wifiscan::busy())`. Zachować wyłączony prototyp `ipsEnabled` jako osobnego właściciela radia — nie uruchamiać jednocześnie `positioning.begin()` i klienta net.
- [ ] **Step 4: Skany i ekran.** Usunąć zależność `WifiScan.cpp` od `WebLink.h` i pomijanie dawnego SSID. `drawWeb()` pokazuje sieć, stan/reason lub error line, IP, `http://<hostname>.local`, stan zegara. Długie ciągi zawijać/skracać do szerokości, pełne informacje zachować w Serial. Na stronie usunąć wyłącznie instrukcje dawnego AP, jeśli występują; nie zmieniać tytułu produktu „Wibrometr”.
- [ ] **Step 5: Build i kontrola źródeł**: komenda PlatformIO jak wyżej → `SUCCESS`; `rg -n 'softAP|WIFI_AP_STA|192\.168\.4\.1|weblink::ssid|weblink::password' src/WebLink.* src/main.cpp src/WifiScan.cpp` → brak dopasowań. Sprawdzić odciski i reconnect na płytce w zadaniu 7; działający klient WWW sieci z client isolation może być niedostępny mimo poprawnego DHCP — to wynik odbioru sieci, nie powód przywrócenia AP.
- [ ] **Step 6: Commit** `feat: show client network status and preserve Wi-Fi fingerprints`.

### Task 6: Czas zdarzeń przy zapisie kolejki logów

**Files:** Modify `src/main.cpp` (`serviceLine`), `test/distance_log_checks.cpp`; Consume zmiany z zadań 2–4.

**Interfaces:** `serviceLine` odbiera nadal `(file,row)` przez `takeDistanceRow`; `row` zaczyna się od surowego `timestamp_ms`. `formatEventTime`, `prependLogTime`, `clockMs`, `net::epochMs` z wcześniejszych zadań są jedyną ścieżką tworzenia nowej pierwszej kolumny.

- [ ] **Step 1: Dodać test integracji czystych helperów**: trzy surowe wiersze o różnych timestampach, jeden snapshot epoki/clock trackera → trzy różne lokalne czasy, identyczna zawartość za pierwszą kolumną; bez NTP każdy zaczyna się od przecinka. Wiersz z maksymalnie długimi flagami i ride columns nie jest ucięty. Test drugiego wiersza po zmianie epoki sprawdza, że nie jest używany stary snapshot z poprzedniej iteracji loop.
- [ ] **Step 2: Uruchomić test** `distance_log_checks` jak w zadaniu 2; nowy scenariusz musi wykrywać podstawienie tego samego czasu zapisu dla wszystkich zdarzeń.
- [ ] **Step 3: Zmienić `serviceLine`.** Przed opróżnieniem kolejki pobrać parę `net::epochMs()` / `tracker().clockMs()` bez oczekiwania. Z każdego surowego CSV odczytać pierwszy uint32_t (do przecinka, z kontrolą poprawności), wyliczyć czas i utworzyć końcowy CSV w buforze 288 bajtów. Do `storage::appendLogRow` i Serial przekazać ten sam końcowy CSV. Przy błędzie formatu/bufora pominąć wiersz z diagnostyką; nie zapisywać uszkodzonej liczby kolumn. Nie zmieniać czasu według `millis()` ani timestampu USB/Serial. Nie powiększać o kolumnę czasu wszystkich ośmiu buforów trackera.
- [ ] **Step 4: Uruchomić** `distance_log_checks`, `line_checks` i build PlatformIO; oczekiwane `all ok`, `all ok`, `SUCCESS`. Fizyczne sprawdzenie czasu podczas skanów i oczekiwania kolejki w zadaniu 7. Znane ograniczenie projektu: zaległość całej kolejki próbek względem czasu rzeczywistego może przesunąć mapowanie o tę zaległość; implementacja spełnia wzór specyfikacji, nie przebudowuje zegara trackera na monotoniczny zegar systemowy.
- [ ] **Step 5: Commit** `feat: prepend NTP event time when flushing CSV logs`.

### Task 7: Regresja, instrukcja użytkownika i odbiór sprzętowy

**Files:** Modify `README.md`, `docs/wifi.example.txt`, `docs/network-time-logs-verification.md`.

**Interfaces:** Brak nowych API. Dokumentuje finalny układ USB, diagnostykę i sprawdzone konfiguracje.

- [ ] **Step 1: Uzupełnić README**: drzewo dysku `config.csv`, `wifi.txt`, `cert/`, `logs/`; instrukcja kopiowania plików i RESET; PSK, PEAP, TTLS, TLS; opcjonalność CA i wynikający brak sprawdzania serwera; NTP/TZ/hostname; ograniczenie `#`; brak własnego AP, adres IP/mDNS; puste `time` bez NTP, stary `timestamp_ms`, stare pliki korzenia pozostają. Usunąć dawne instrukcje łączenia do AP i stare ścieżki nowych logów. Nie umieszczać rzeczywistych sekretów w repo.
- [ ] **Step 2: Pełna regresja hostowa** (binaria w `/tmp`):

```bash
for suite in wifi_config station_config motion_classifier distance_log line displacement; do
  g++ -std=c++17 -O2 -Iinclude "test/${suite}_checks.cpp" -o "/tmp/wibrometr_${suite}_checks" && "/tmp/wibrometr_${suite}_checks" || exit 1
done
g++ -std=c++17 -Iinclude -fsyntax-only test/signal_checks.cpp
node test/arr_changed.mjs
node test/scene_geometry.mjs
```

Oczekiwane: exit 0 wszystkich zestawów; osobno zachować wyniki asercji. Jeśli test ma dodatkowe wymagania wynikające z nagłówka pliku, użyć jego komendy zamiast pomijać test. Nie dopisywać testów implementacji do samych zmian dokumentacji.

- [ ] **Step 3: Build firmware**: `env -u TEMP /home/korneliusz/.local/share/wibrometr-pio/bin/pio run -e feather` → `SUCCESS`; zapisać rozmiar firmware i wynik. Nie utożsamiać kompilacji z działaniem WPA2-Enterprise.
- [ ] **Step 4: Odbiór PSK na płytce** z siecią `xiaomi` i hasłem przekazanym poza repo: uzyskać IP, sprawdzić IP i `<hostname>.local`, WebSocket/wykresy/polecenia, godzinę NTP i `.mmm`, logi w `logs/`. Wyłączyć/włączyć AP, potwierdzić reconnect bez restartu firmware, aktualizację IP, mDNS i stan strony; nie powstaje AP płytki. Testować też błędne hasło, brak/błąd `wifi.txt`, restart bez sieci i niedostępne NTP. Bez sieci linia i logowanie działają; przed synchronizacją puste `time`, po utracie sieci zegar nadal działa.
- [ ] **Step 5: Odbiór logów/USB**: wykonać przejazd, ruch technologiczny i skan, sprawdzić czas zdarzeń oraz nagłówek obu plików. Wysunąć USB przed pomiarem. Przy ponownym podłączeniu sprawdzić `logs/`; stare logi w korzeniu mają niezmienioną zawartość. Stary nagłówek w `logs/` → `_old.csv` plus nowy nagłówek, poprawny nagłówek → dopisanie. Sprawdzić brak zapisu przy zamontowanym USB, brak miejsca, kolizję `logs` z plikiem i błąd rotacji; nie uszkodzić istniejących danych (test na kopii/testowym woluminie).
- [ ] **Step 6: Odbiór Enterprise** z testową infrastrukturą: PEAP i TTLS (identity jawne i domyślne), TLS, z CA i bez CA; błędny CA ma powodować odmowę, błędne hasło/certyfikat mają dawać reason. Ponowić połączenie po utracie AP, aby sprawdzić żywotność buforów. Sprawdzić zimny start z CA przy nieustawionym czasie; udokumentować wynik bez osłabiania walidacji. Brak dostępu do RADIUS/certów oznaczyć jako **niezweryfikowane**, nie jako PASS. Reconnect+skany sprawdzić na PSK i dostępnej sieci Enterprise.
- [ ] **Step 7: Zapisać wynik odbioru**: data, wersja firmware, scenariusz, PASS/FAIL/niezweryfikowane, obserwowany stan i błąd (bez haseł/PEM). Wyraźnie oddzielić testy hostowe, build i testy urządzenia.
- [ ] **Step 8: Commit** `docs: explain client Wi-Fi, NTP timestamps and logs acceptance`.

## Przegląd planu i kryteria zakończenia

Pokrycie specyfikacji: `wifi.txt` → 1/4; certyfikaty i EAP → 4/7; reconnect/status/mDNS → 4/5/7; skany offline i STA → 4/5/7; czas i kolumna → 2/6/7; katalog i rotacja → 3/7; ekran 7 i WWW → 5/7. Wszystkie pozycje Review Focus mają przypisane sprawdzenia powyżej.

Plan kończy etap przygotowania. Implementacja jest osobnym zleceniem po przeglądzie planu. Warunkiem uznania implementacji za zweryfikowaną są wyniki testów hostowych/builda oraz jawny raport, które próby sprzętowe faktycznie wykonano. Przyszły wykonawca nie może deklarować walidacji Enterprise na podstawie samego połączenia PSK.
