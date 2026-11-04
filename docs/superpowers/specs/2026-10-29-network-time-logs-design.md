# Spec C — sieć klienta (WPA2 / WPA2-Enterprise), czas z NTP, katalog logów

Status: projekt zaakceptowany w rozmowie (2026-10-29), do przeglądu.

## Cel

1. Logi w osobnym katalogu na dysku USB.
2. Prawdziwy czas w logach (NTP; płytka nie ma RTC z baterią).
3. Płytka nie tworzy własnego AP — łączy się z siecią użytkownika: teraz
   WPA2-PSK (test: `xiaomi`), docelowo WPA2-Enterprise z certyfikatami
   z katalogu `cert/` na dysku USB.

## Decyzje

| Temat | Decyzja |
|---|---|
| Źródło czasu | tylko NTP z sieci (bez modułu RTC) |
| Dane Wi-Fi | plik `wifi.txt` na dysku USB (obok `config.csv`) |
| Metoda EAP | obie rodziny: PEAP/TTLS (login + hasło + CA) i TLS (cert + klucz + CA) |
| Strona WWW | zostaje, pod IP z DHCP i `wibrometr.local` (mDNS) |

## Dysk USB (partycja FFat)

```
config.csv          opis linii (Spec A)
wifi.txt            sieć, NTP, strefa czasowa
cert/               certyfikaty PEM (ca.pem, client.crt, client.key — nazwy dowolne, wskazane w wifi.txt)
logs/               wszystkie logi: <A>-<B>.csv, <A>_tech.csv, distances_last_run.csv
```

`logs/` jest zakładany przy starcie. Stare logi w katalogu głównym nie są
ruszane.

## `wifi.txt`

`klucz=wartość`, jedna para na wiersz; `#` zaczyna komentarz; spacje wokół
`=` i na końcach są obcinane; BOM i CRLF dozwolone; nieznany klucz albo
wiersz bez `=` odrzuca plik (z numerem wiersza, jak `config.csv`).

| Klucz | Znaczenie |
|---|---|
| `ssid` | wymagany |
| `password` | WPA2-PSK; przy `eap=peap/ttls` — hasło użytkownika |
| `eap` | brak = WPA2-PSK; `peap`, `ttls`, `tls` |
| `identity` | tożsamość zewnętrzna EAP (domyślnie = `username`) |
| `username` | peap/ttls: wymagany |
| `ca` | ścieżka do certyfikatu CA serwera RADIUS (PEM); bez niego serwer nie jest sprawdzany |
| `cert`, `key` | tls: wymagane, certyfikat klienta i klucz prywatny (PEM, klucz niezaszyfrowany) |
| `ntp` | serwer NTP, domyślnie `pool.ntp.org` |
| `tz` | strefa POSIX, domyślnie `CET-1CEST,M3.5.0,M10.5.0/3` |
| `hostname` | nazwa w sieci i mDNS, domyślnie `wibrometr` |

Ścieżki są względne wobec korzenia dysku (`cert/ca.pem`). Plik certyfikatu
do 8 KB. Walidacja: `peap`/`ttls` wymagają `username` i `password`, `tls`
wymaga `identity`, `cert` i `key`; `cert`/`key`/`ca` bez `eap` to błąd;
brakujący plik certyfikatu = błąd z nazwą pliku.

Parser (`include/WifiConfig.h`) jest czystym C++ (test na komputerze);
odczyt plików i łączenie — `src/Network.*` (moduł `net`).

## Sieć

- `WiFi.mode(WIFI_STA)`, bez `softAP`. PSK: `WiFi.begin(ssid, password)`;
  EAP: `WiFi.begin(ssid, WPA2_AUTH_PEAP/TTLS/TLS, identity, username,
  password, ca, cert, key)` (Arduino 3.3.6). Bufory PEM trzymane przez cały
  czas pracy (sterownik ich używa przy ponownym łączeniu).
- Automatyczne ponowne łączenie; stan dla ekranu i strony: brak pliku / błąd
  pliku / łączenie / połączono (SSID, IP) / rozłączono (powód z zdarzenia
  Wi-Fi, np. błąd uwierzytelnienia).
- mDNS `hostname.local` po uzyskaniu IP.
- Skany odcisków Wi-Fi (`WifiScan`) działają jak dotąd, w trybie STA.
- Bez prawidłowego `wifi.txt` płytka działa bez sieci (linia i logi tak,
  strona i czas nie).

## Czas

- `configTzTime(tz, ntp)` po połączeniu; czas uznany za ustawiony, gdy
  `time()` > 2024-01-01. SNTP odświeża co godzinę (domyślnie w IDF).
- Logi: nowa pierwsza kolumna `time` — `RRRR-MM-DD GG:MM:SS.mmm` czasu
  lokalnego, pusta, gdy czas nieustawiony. Czas wiersza = czas teraz −
  (zegar trackera teraz − `timestamp_ms` wiersza), więc zgadza się z chwilą
  zdarzenia, a nie zapisu. `timestamp_ms` (od startu) zostaje.
- Nagłówek się zmienia: pliki ze starym nagłówkiem idą do `…_old.csv`
  (mechanizm ze Spec A).

## Ekran 7 i strona

Ekran 7 (dawniej „WYKRESY W PRZEGLADARCE” z danymi AP): sieć, stan, IP,
`http://<hostname>.local`, stan zegara. Strona bez zmian poza tym, że nie ma
już mowy o sieci „Wibrometr”.

## Testy

- `test/wifi_config_checks.cpp`: PSK, PEAP, TTLS, TLS, domyślne wartości,
  komentarze/BOM/CRLF, błędy z numerem wiersza, brakujące pola.
- `test/distance_log_checks.cpp`: kolumna `time` (format, pusta bez czasu,
  przeliczenie chwili zdarzenia).
- Build firmware; na płytce: połączenie z `xiaomi`, IP na ekranie, NTP,
  `logs/`.

## Poza zakresem

Moduł RTC; zaszyfrowany klucz prywatny (`key_password` — do dodania przez
API ESP-IDF, jeśli będzie potrzebny); wysyłanie logów przez sieć.
