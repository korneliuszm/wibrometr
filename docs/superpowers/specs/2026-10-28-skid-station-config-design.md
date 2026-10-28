# Spec A — konfiguracja stacji CSV, USB MSC, log odległości, klasyfikacja ruchu

Status: zatwierdzony do planu implementacji (2026-10-28).
Powiązane notatki (Obsidian): `10_Projekty/*/Notatki/2026-10-28-*.md`
(konfiguracja-stacji-urzadzenie-skid, skid-punkty-referencyjne-ruchu,
skid-log-odleglosci-kalibracja, skid-diagnostyka-transportu-logi).

## Cel

Zamiast samouczenia odstępów/kierunków między stołami (obecne zachowanie
w `include/LineTracker.h`), urządzenie ma czytać konfigurację linii z pliku
CSV dostarczonego przez USB Mass Storage, i na tej podstawie:
- rozróżniać ruch transportowy od technologicznego na stacjach, które mają
  zdefiniowany manewr (winda, obrót, zjazd),
- logować zmierzone odległości/kąty osobno od logów drganiowych, do
  późniejszej ręcznej korekty CSV przez użytkownika.

## Zakres (etap 1 — ten PR)

1. `StationConfig` — parser `config.csv`.
2. USB Mass Storage (`FatFsUsbStorage`) na partycji FFat.
3. `MotionClassifier` — ruch technologiczny vs transportowy.
4. `DistanceLog` — log odległości per przejazd/manewr.
5. Guard `configAuthoritative_` w `Tracker`.

**Poza zakresem (etap 2, osobny PR):** punkty referencyjne / rozpoznawanie
sekwencji manewrów-kotwic (`reference_point`, `motion_signature`).

## Format `config.csv`

```csv
station_id,direction,distance_m,passthrough,wifi_scan,station_motion,motion_axis,motion_range,motion_speed
ST01,X+,10,false,true,none,,,
ST02,X+,10,true,false,lift,Z,0.4,0.1
ST03,Y+,8,false,true,rotation_z,Z,90,
```

- `direction`: `X+ / X- / Y+ / Y-` (kierunek odcinka DO tej stacji od poprzedniej).
- `distance_m`: długość odcinka od poprzedniej stacji.
- `passthrough`: `true/false` — dopuszczony przejazd bez zatrzymania.
- `wifi_scan`: `true/false` — czy skanować Wi-Fi na tej stacji.
- `station_motion`: `none / lift / rotation_z / z_transfer / compound`.
- `motion_axis`: oś manewru (dla `rotation_z` zawsze `Z`).
- `motion_range`: zakres ruchu — metry (lift/z_transfer) albo stopnie (rotation_z).
- `motion_speed`: oczekiwana prędkość (m/s albo °/s) — opcjonalne, do oceny jakości.

Parser odrzuca wiersz z błędną liczbą kolumn lub nierozpoznaną wartością
enumeracji (`station_motion`, `direction`) — loguje numer wiersza na Serial,
nie wczytuje częściowej konfiguracji (brak CSV = brak konfiguracji, fallback
jak dotąd).

## CSV jako autorytet (decyzja)

Gdy `config.csv` wczytany poprawnie przy starcie:
- `learned.gap[i]`, `learned.gapDir[i]`, `learned.gapDirKnown[i]` ustawiane
  z CSV i **nieaktualizowane** przez przejazdy (auto-uczenie wyłączone).
- Korekta pozycji przez odcisk Wi-Fi (`arrived_` override z `score_`)
  **wyłączona** — Wi-Fi nadal skanowane (do logu/diagnostyki), ale nie
  zmienia rozpoznanej stacji.
- Jedna flaga `configAuthoritative_` w `Tracker`, guard w istniejących
  miejscach uczenia/korekty — bez równoległej hierarchii klas.

Brak `config.csv` → zachowanie bez zmian (obecne samouczenie + korekta Wi-Fi).

## USB Mass Storage

- Partycja FFat wydzielona w `partitions.csv` (niestandardowa tabela —
  **ryzyko**: płytka ma 4 MB flash, firmware już używa 61%; USB MSC + FFat
  może nie zmieścić się bez wyłączenia czegoś). Pierwszy krok planu:
  build-spike sprawdzający budżet flash przed pisaniem resztek logiki.
- `USBMSC` eksponuje partycję jako dysk. `config.csv` czytany **tylko przy
  starcie** urządzenia (decyzja: bez przeładowania w locie).
- `distances_last_run.csv` zapisywany bezpośrednio na FFat **po każdym
  zatrzymaniu** (koniec odcinka transportowego lub manewru technologicznego).
  USB jest odłączone w trakcie przejazdu, więc zapis nie koliduje z hostem.
  `onStartStop` ustawia `usbMounted_`; nowe zapisy pomijane, gdy host ma
  dysk zamontowany (urządzenie stoi podłączone, nie jedzie).
  `# ponytail: brak kolejki w RAM na czas mountu — upgrade, jeśli kiedyś
  trzeba pisać podczas podłączenia do USB.`

## `MotionClassifier`

Dla stacji z `station_motion != none`: rozpoznaje manewr wg skonfigurowanej
osi/zakresu/prędkości. Zmierzony ruch **nigdy nie zmienia indeksu stacji**,
nawet gdy przekracza `motion_range` — przekroczenie tylko ustawia
`quality_flags` w logu (wymóg: ruch technologiczny nie może udawać przejazdu).

## `DistanceLog` — `distances_last_run.csv`

Kolumny: `timestamp_ms,segment_type,from_station,to_station,station_id,motion_type,axis,measured_distance_m,measured_angle_deg,confidence,quality_flags`.
Ring-buffer jak istniejąca historia przejazdów (ostatni przejazd, bez
dodatkowej retencji — decyzja odłożona, jak dziś z `last_run.csv`).

## Znane ograniczenie (wykryte w finalnym review, 2026-10-28)

Pomiar dla `station_motion: lift` / `z_transfer` w praktyce **nie jest
jeszcze logowany**: `finish()` (miejsce wpięcia pomiaru) uruchamia się
tylko z fazy `Moving`, a wejście w `Moving` i trzymanie `calm` zależą
wyłącznie od sygnałów horyzontalnych (`hv_`/`ha_`, po odjęciu składowej
grawitacji) — czysto wertykalny ruch nigdy nie przerywa `Parked`, zgodnie
z już istniejącym w README stwierdzeniem „ruch pionowy jest pomijany".
`rotation_z` **działa poprawnie** (naprawione i zweryfikowane — wpięte w
`endTurn()`, osobny mechanizm detekcji obrotu w postoju, już istniejący).
Zamknięcie tej luki wymaga nowego detektora ruchu wertykalnego w fazie
`Parked` (analogicznego do `turning_`/`endTurn()`) — osobne zadanie,
rozmiarem porównywalne do detektora obrotu.

## Testy (styl istniejący, `g++ -std=c++17`, bez frameworków)

- `test/station_config_checks.cpp` — parsowanie, błędne wiersze, enumeracje.
- `test/motion_classifier_checks.cpp` — technologiczny vs transportowy,
  przekroczenie zakresu nie zmienia stacji.
- `test/distance_log_checks.cpp` — ring-buffer, format wiersza CSV.

## Flash budget (measured 2026-10-28)

Z custom `partitions.csv` (app0 = 0x300000, ffat = 0x80000) i probe USB MSC + FFat: Flash 39.4% (1240875 / 3145728 bytes) — fits comfortably, go.
