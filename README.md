# Przenośny monitor drgań — Feather ESP32-S3 TFT + MPU6050

Pierwsza wersja do obserwacji przyspieszeń ramienia robota podczas ruchu.
Projekt PlatformIO znajduje się w tym katalogu. Płytka: Adafruit **5483**
ESP32-S3 TFT Feather, nie wersja Reverse TFT. Akcelerometr: MPU6050 (moduł
GY-521) przez I2C.

> **Stan obecny:** ekrany 1/8 — linia (stół, przejazd, odcisk Wi-Fi),
> 2/8 — przyspieszenie XYZ, 3/8 — żyroskop, 4/8 — przemieszczenie XYZ,
> 5/8 — wykres ruchu w czasie, 6/8 — tor ruchu, 7/8 — Wi-Fi i adres strony
> z wykresami, 8/8 — akumulator.

## Nawigacja na linii rolkowej

Detal z czujnikiem jedzie po rolkach między stołami (odstęp ok. 5–8 m):
rusza z rampą, hamuje i stoi ok. 120 s. Płytka sama wyznacza, na którym
stole jest (`include/LineTracker.h`).

**Obsługa:** postaw detal na pierwszym stole i na ekranie LINIA (pierwszy)
przytrzymaj BOOT ≥1 s — albo na stronie „Detal jest na stole 1”. Od tej
chwili pozycja liczy się sama. Liczbę stołów (domyślnie 8, do 64) ustawia
się na stronie.

Linia może biec w osi X i Y hali, stoły podnoszone unoszą karoserię
(ten ruch jest pomijany), a stoły obrotowe obracają ją o 90° w jedną lub
drugą stronę — firmware obsługuje wszystkie te przypadki.

**Jak to działa:**
- *Orientacja:* żyroskop (bez offsetu) cały czas — w postoju i w jeździe —
  obraca przewidywaną grawitację w osiach czujnika, a akcelerometr tylko
  powoli ją koryguje (filtr komplementarny, ok. 3 s). Dzięki temu przechył
  na stole podnoszonym ani obrót na stole obrotowym nie udają przyspieszenia.
  Offset żyroskopu uczy się tylko wtedy, gdy wygładzony (1 s) żyroskop jest
  bliski zera (< 0,23 °/s) — obrót i przechył go nie psują; „obrót” trwający
  ponad 30 s w postoju jest traktowany jako zmiana offsetu (np. temperatura).
- *Kurs i obroty:* obrót wokół pionu jest całkowany stale (0 = kierunek
  karoserii na stole 1). Obrót w postoju (powyżej 2 °/s) jest rozpoznawany
  jako obrót stołu i zaokrąglany do wielokrotności 90°, jeśli mieści się
  w ±20°. Strona pokazuje kompas na autku, a płytka — „OBROT W LEWO/PRAWO”.
- *Ruch:* od spoczynku do postoju przyspieszenie jest obracane żyroskopem
  do osi z chwili startu, pozbawione grawitacji i całkowane na bieżąco.
  W postoju prędkość musi być zerowa, więc reszta prędkości to dryf —
  usuwany w zamkniętej postaci. Pozioma część wyniku to przejechane metry.
- *Postój czy jazda (filtr drgań konstrukcji):* stoły wiszą na konstrukcji,
  która drga, więc samo przyspieszenie nie odróżnia postoju od jazdy.
  Wskaźnikiem jazdy jest pozioma „prędkość z przeciekiem” (przyspieszenie bez
  grawitacji całkowane ze stałą zaniku 1,5 s): drgania o amplitudzie A i
  częstotliwości f dają tylko A/(2πf) (1 m/s² przy 5 Hz → 0,03 m/s), a ruszenie
  z rampą 0,3–0,5 m/s; pionowe bujanie się nie liczy. W postoju płytka stale
  mierzy poziom drgań (RMS tej prędkości i przyspieszenia poniżej ok. 1 Hz,
  pamięć ok. 10 s) i dopasowuje progi: ruszenie powyżej 4× poziomu
  (0,25–0,30 m/s — próg nie schodzi poniżej 0,25 m/s, żeby drgania konstrukcji
  nie dawały fałszywych przejazdów), cisza poniżej 3,5× (0,05–0,4 m/s²).
  Hamowanie wystarczy na połowę progu ruszenia. Po włączeniu przez
  pierwsze 8 s tylko uczy się drgań. Zatrzymanie to impuls hamowania
  (prędkość z przeciekiem przeciwna do kierunku ruszenia), a po nim cisza —
  równa jazda po rolkach też bywa cicha, ale nie ma przed nią hamowania;
  awaryjnie wystarcza 45 s ciszy (dłużej niż najdłuższa równa jazda przez
  stoły PT: 24 m przy 0,6 m/s to ok. 38 s). Grawitacja i offset żyroskopu są w postoju
  śledzone dwustopniową średnią (tłumi drgania o ok. 4 rzędy), a grawitacja po
  zatrzymaniu jest średnią z całych 15 s ciszy. Poziom drgań liczony jest
  tylko ze zmienności (składowa stała odjęta), więc chwilowy rozjazd
  grawitacji nie podnosi progów. Ruch pionowy (stoły podnoszone) się nie
  liczy. Ruch bez impulsu hamowania (szarpnięcie, przechył) nie zmienia stołu.
  Poziom i progi widać na
  stronie („Filtr drgań konstrukcji”) i na ekranie LINIA (dolny wiersz).
- *Stół:* kierunek przejazdu na podłodze = kurs + kierunek w osiach
  czujnika. Każdy odstęp między stołami uczy się swojego kierunku przy
  pierwszym przejeździe (strzałki przy odstępach na stronie), więc odcinki
  w X i w Y oraz jazda po obrocie karoserii są rozpoznawane jako „do przodu”
  albo „wstecz”; bez wyuczonych kierunków przyjmowane jest „do przodu”.
  Przejazd przesuwa pozycję o tyle stołów, ile odstępów mieści się
  w odległości (zaokrąglając); odstępy uczą się same (domyślnie 6,5 m).
  Przesunięcie krótsze niż 35% odstępu (lub 1,5 m) to poprawka, nie przejazd.
  Po 2 s bezruchu strona pokazuje detal na nowym stole.
- *Odcisk Wi-Fi:* po **15 s** postoju stół jest zatwierdzany i płytka robi
  3 skany Wi-Fi (ok. 8 s; jej sieć działa dalej, strona może na chwilę się
  zatrzymać). Pierwszy odcisk na stole staje się jego wzorcem (potem
  uśredniany). Kolejne skany porównuje ze wzorcami (średnia różnica RSSI);
  gdy inny stół pasuje wyraźnie lepiej, pozycja jest poprawiana
  („poprawione przez Wi-Fi”). Po zgubieniu próbek w trakcie przejazdu,
  po przejeździe dłuższym niż 60 s (np. noszenie w ręku) i po włączeniu
  płytki stół jest nieznany — rozpoznaje go pierwszy odcisk.
- *Przeniesienie w inne miejsce:* jazda po rolkach jest pozioma, nie obraca
  się i kończy hamowaniem. Ruch z przechyłem ponad 8°, obrotem ponad 30° przed
  zatrzymaniem, bez hamowania (przy ≥1,5 m) albo dłuższy niż 60 s jest
  uznawany za przeniesienie (ręcznie, suwnicą): strona pokazuje
  „Przenoszony” już w trakcie, a po odstawieniu stół jest nieznany i po 15 s
  postoju rozpoznaje go sam odcisk Wi-Fi — bez względu na to, jak daleko od
  poprzedniego stołu. Stół jest przyjmowany tylko przy wyraźnym dopasowaniu
  (średnia różnica ≤12 dB i przewaga nad drugim stołem ≥0,8 dB oraz ≥25%);
  inaczej skan jest powtarzany co 45 s, a wyniki skanów z tego samego miejsca
  są uśredniane, aż sąsiednie stoły się rozdzielą. Tak samo po włączeniu
  płytki i po zgubieniu próbek. Przeniesienie jest na liście przejazdów
  („przeniesiony, stół z Wi-Fi”). Bezpiecznik: „jazda” dłuższa niż 120 s
  oznacza utratę orientacji — płytka uczy się grawitacji od nowa, a stół
  znajduje Wi-Fi.
- Wzorce, odstępy, kierunki odcinków, kurs karoserii na każdym stole i liczba
  stołów są zapisywane w pamięci nieulotnej (NVS) i przetrwają wyłączenie;
  po włączeniu stół rozpoznaje odcisk Wi-Fi, a kurs — zapamiętany kurs na
  tym stole. (Zmiana formatu w tej wersji: wcześniej zapisane dane są
  kasowane, linię trzeba raz przejechać od stołu 1.)

**Drgania przejazdu:** dla każdego przejazdu płytka zapisuje odległość,
czas, największe przyspieszenie rampy (start/hamowanie), RMS i szczyt
drgań w X/Y/Z (składowa powyżej ok. 0,6 Hz) oraz obwiednię przyspieszenia
(min–maks w czasie, do 160 punktów). Pamięta ostatnie 12. Strona pokazuje
je w tabeli z wykresem i pozwala pobrać obwiednię jako CSV, a jeśli była
otwarta przez cały przejazd — także próbki 200 Hz (CSV).
RMS, szczyt i obwiednia liczone są z próbek 200 Hz (filtr czujnika 94 Hz).

**Strona — linia:** widok z boku: stoły jako ramy, przez które biegnie
przenośnik rolkowy, detal jako autko jadące po rolkach (koła i rolki kręcą
się w ruchu). W ramie stołu — „kod kreskowy” jego odcisku Wi-Fi (sieci
ułożone wg stołu, przy którym są najsilniejsze), pod stołami — wyuczone
odstępy (linie wymiarowe) i podobieństwo ostatniego skanu do każdego stołu.
W postoju nad autkiem odlicza się czas do odcisku, a w trakcie skanu
pulsuje znak Wi-Fi. Kliknięcie stołu pokazuje jego sieci z porównaniem do
ostatniego skanu.

Sprawdzenie na komputerze: symulowana linia 8 stołów z odcinkami w X i Y,
stół podnoszony (0,4 m, przechył 1,5°), stoły obrotowe +90° i −90°, rampa,
drgania rolek, Wi-Fi z 12 punktów dostępowych, zgubione próbki, restart,
poprawka, przeniesienie w ręku ze stołu 8 na 3 i z powrotem (chwianie ±12°,
obrót 60°); czujnik zamocowany krzywo i obrócony względem linii. Cały
scenariusz przechodzi dwa razy: bez drgań konstrukcji i z drganiami
(0,8 m/s² 7 Hz i 0,5 m/s² 11 Hz w poziomie, 1 m/s² 5 Hz w pionie, kołysanie
0,15 m/s² 0,9 Hz, kołysanie dla żyroskopu) — w tym 120 s postoju bez
fałszywego przejazdu, obroty rozpoznane jako 90,0°, podnoszenie bez wpływu
na progi i stół; największy błąd odległości ok. 0,2 m bez drgań i ok. 0,4 m
z drganiami. Do tego: konfiguracja `config.csv` (odstępy z pliku, skan Wi-Fi
tylko tam, gdzie `wifi_scan`), przejazdy przez stoły PT (16 i 24 m bez
zatrzymania) oraz linia z windami (`Z-`/`Z+`, opis niżej):

```text
g++ -std=c++17 -O2 -Iinclude test/line_checks.cpp -o lc && ./lc
```

## Konfiguracja linii (`config.csv`) i logi

Płytka jest widoczna przez USB jako dysk **WIBROMTR SKID LOG** (obok portu
szeregowego). Plik `config.csv` na nim, wczytywany przy starcie, opisuje
linię (do 64 stacji); wtedy jest nadrzędny: odstępy i kierunki nie są
uczone, a Wi-Fi nie poprawia stołu. Bez pliku — samouczenie jak opisano
wyżej. Błędny wiersz odrzuca cały plik (log: `config.csv: not found or
invalid`). Przykłady: `docs/configs/`.

```csv
station_id,direction,distance_m,passthrough,wifi_scan,station_motion,motion_axis,motion_range,motion_speed
B1,X+,0,false,true,none,,,
B2,X+,6.5,false,false,none,,,
```

- `direction`, `distance_m`: odcinek od poprzedniej stacji (dla pierwszej
  pomijany). `X+ / X- / Y+ / Y-` — po rolkach na podłodze (kierunki względem
  karoserii na pierwszej stacji); `Z+ / Z-` — winda w górę / w dół między
  dwoma osobnymi miejscami postoju, `distance_m` to jej wysokość;
  `RZ+ / RZ-` — stół obrotowy między dwiema pozycjami (obrót w lewo / w
  prawo wokół pionu), `distance_m` to kąt w stopniach (np. 90). Obrót w
  postoju o co najmniej połowę tego kąta w tę stronę przenosi detal na
  następną pozycję, w przeciwną — na poprzednią (log np. `B026-B026o.csv`).
- `passthrough` — stół, przez który detal może przejechać bez zatrzymania.
- `wifi_scan` — tylko na tych stacjach robiony jest odcisk Wi-Fi (oraz
  zawsze, gdy stół jest nieznany).
- `station_motion` (`none / lift / rotation_z / z_transfer / compound`),
  `motion_axis`, `motion_range` (m albo °), `motion_speed` — ruch
  technologiczny na stacji; zakres służy do oznaczenia `over_range`.
- Nazwa stacji (`station_id`, do 11 znaków) jest pokazywana na ekranie
  LINIA i na stronie.

**Windy:** przy stacji sąsiadującej z windą płytka obserwuje też pionową
„prędkość z przeciekiem” (stała 3 s, własny próg 0,05–0,25 m/s z pionowych
drgań); ruch w pionie w stronę windy dłuższy niż 35% jej wysokości to
przejazd do następnego miejsca postoju. W innych miejscach ruch pionowy dalej
się nie liczy (stoły podnoszone). W symulacji windę widać od ok. 0,15 m/s,
przy silnych pionowych drganiach konstrukcji (1 m/s² przy 5 Hz) od ok.
0,3 m/s.

**Przeniesienie:** gdy ruch rozpoznano jako przeniesienie (przechył,
obrót w drodze), po odłożeniu wystarczą 3 s spokoju (także żyroskopu),
potem skan Wi-Fi i pozycja od razu przechodzi na najlepiej pasujący stół
(również z `config.csv`; liczą się tylko stoły z odciskiem).

**Logi** (na tym samym dysku): każdy przejazd dopisuje wiersz do pliku
nazwanego od stacji, np. `B2-B3.csv`, a ruch technologiczny — do
`B3_tech.csv` (bez `config.csv` — numery stołów, stół nieznany:
`nieznana`). Kolumny: `timestamp_ms, segment_type, from_station,
to_station, station_id, motion_type, axis, measured_distance_m,
measured_angle_deg, confidence, quality_flags`, a dla przejazdów dodatkowo
`duration_s, ramp_m_s2, rms_x, rms_y, rms_z, peak_x, peak_y, peak_z`
(m/s², drgania powyżej ok. 0,6 Hz). Gdy komputer ma dysk zamontowany, płytka
nie zapisuje (wysuń dysk przed jazdą).

Ograniczenia: drgania konstrukcji o bardzo niskiej częstotliwości (wolne
bujanie poniżej ok. 0,5 Hz o dużej amplitudzie) wyglądają podobnie jak
ruszenie; górny limit progu (0,25 m/s) gwarantuje, że przejazd nadal jest
wykrywany, ale takie bujanie może dać fałszywe krótkie „przejazdy” (krótsze
niż 35% odstępu nie zmieniają stołu). Wi-Fi rozróżnia stoły tylko wtedy, gdy sieci w hali
różnią się między nimi — na początku warto przejechać całą linię raz od
stołu 1, aby każdy stół dostał wzorzec. Dokładność metrów nie była jeszcze
sprawdzona na prawdziwej linii.
> Pozostałe funkcje są wyłączone flagami w `src/main.cpp`, a ich kod jest
> zachowany: ekrany analizy (przebieg czasowy, FFT, licznik ruchów —
> `imuEnabled`) oraz nawigacja po Wi-Fi (`ipsEnabled`; w praktyce działała
> słabo). Wyłączony moduł nie trafia do firmware. Bluetooth został usunięty.

## Połączenia

Czujnik pracuje na magistrali I2C płytki (400 kHz), tej samej co układ
pomiaru akumulatora (adres 0x36 — bez konfliktu z MPU6050 pod 0x68/0x69).
Ta magistrala jest też wyprowadzona na złączu STEMMA QT.

| MPU6050 (GY-521) | Feather |
|---|---|
| VCC | 3V (3,3 V) |
| GND | GND |
| SDA | SDA / GPIO42 |
| SCL | SCL / GPIO41 |
| INT | **A5 / GPIO8** |
| AD0 | GND albo niepodłączony → adres 0x68; 3V → 0x69 |
| XDA, XCL | niepodłączone |

- Firmware sam wybiera 0x68 lub 0x69.
- INT (aktywny stan wysoki, zatrzask do odczytu) zgłasza każdą nową próbkę;
  odczyt jest wtedy wyzwalany przerwaniem, a czas próbki to chwila
  przerwania — bez duplikatów i pominięć. Gdy INT nie jest podłączony,
  po ok. 100 ms firmware przechodzi na odpytywanie co 1 ms (komunikat na
  porcie szeregowym); pomiar działa, ale z mniej dokładnym czasem próbek.
- Konfiguracja: akcelerometr ±8 g (4096 LSB/g), żyroskop ±500 °/s
  (65,5 LSB/(°/s)), filtr dolnoprzepustowy 94/98 Hz, próbkowanie
  **200 Hz**; obie wielkości
  odczytywane jednym 14-bajtowym odczytem z tej samej próbki (ok. 0,4 ms
  na magistrali 400 kHz).
- Te same próbki 200 Hz dostają nawigacja na linii, miernik ruchu ręcznego,
  strumień na stronę i ekrany analizy. Kolejka próbek mieści ok. 5 s (na czas
  skanu Wi-Fi).
- Moduły GY-521 mają własne rezystory podciągające I2C; Feather ma swoje.
  Krótkie przewody i wspólna masa.
- Gdy czujnik nie odpowiada, ekran pokazuje `BRAK MPU6050 (I2C 0x68/0x69)`,
  a firmware ponawia próbę co 2 s (port szeregowy podpowiada, co sprawdzić).

## Ekran i obsługa

Ekrany przełączane krótkim naciśnięciem BOOT (numer w prawym górnym rogu):
1 — przyspieszenie XYZ, 2 — żyroskop, 3–5 — przemieszczenie (wartości,
wykres w czasie, tor), ostatni — akumulator. Z flagą `imuEnabled = true`
dochodzą ekrany analizy (opis niżej), wstawiane po ekranie 5.

![Ekran przyspieszenia: płasko, przechylony, uderzenie, tara, brak czujnika, łączenie](docs/accel-screens.png)

*Podgląd wyrenderowany na komputerze tym samym kodem co na urządzeniu (skala 2×).*

### 1. Przyspieszenie XYZ

- Trzy wiersze: X czerwony, Y zielony, Z niebieski; wartość w m/s² i w g
  oraz pasek od środka (zero) w skali ±20 m/s²; na dole moduł |a|.
- Odczyt jest lekko wygładzony (ok. 0,15 s), żeby cyfry dało się czytać;
  nagłówek podaje faktyczną liczbę próbek na sekundę (ok. 200 Hz).
- Czujnik w spoczynku mierzy reakcję podłoża, więc położony płasko pokazuje
  Z ≈ +9,81 m/s² (1 g), a X i Y bliskie zeru. Osie X i Y mają znak odwrócony
  względem nadruku modułu (czujnik zamocowany obrócony o 180° wokół Z).
- **BOOT:** krótko — następny ekran; przytrzymanie ≥1 s — **tara**:
  bieżący wektor jest odejmowany (np. grawitacja przy ustalonym ułożeniu),
  na ekranie pojawia się „TARA”; kolejne ≥1 s wyłącza tarę.
- Czujnik mierzy do ±8 g (ok. ±78 m/s²); mocniejsze uderzenia są obcinane
  przez czujnik. Pasek jest obcinany na końcach.

### 2. Żyroskop

- Prędkość kątowa wokół X, Y, Z w °/s (pasek ±200 °/s), lekko wygładzona,
  z odjętym offsetem zera zmierzonym w ostatnim spoczynku (ten sam co
  w mierniku przemieszczenia). Do pierwszego spoczynku ekran prosi
  o położenie czujnika nieruchomo.
- „kat” — kąt obrotu wokół każdej osi od zerowania (całka prędkości).
  Dokładny dla obrotu wokół jednej osi; przy obrotach wokół kilku osi naraz
  to tylko przybliżenie. Dryfuje powoli (resztkowy offset), mniej niż
  ok. 1°/min po pomiarze offsetu.
- **BOOT:** krótko — następny ekran; przytrzymanie ≥1 s — zerowanie kątów.

### 3–5. Przemieszczenie (miernik odległości w osiach X, Y, Z)

Mierzy, o ile przesunięto czujnik w każdej osi podczas jednego ruchu
**od spoczynku do spoczynku** (`include/Displacement.h`). Wynik jest
w osiach czujnika z chwili **początku** ruchu; obrót w trakcie ruchu jest
kompensowany żyroskopem.

**Jak mierzyć:** połóż czujnik nieruchomo (np. na stole), poczekaj na
„SPOCZYNEK”, przesuń go płynnie i znów zostaw nieruchomo. Wynik pojawia się
ok. 0,6 s po zatrzymaniu.

- **Ekran 3:** przemieszczenie ostatniego ruchu w cm dla X, Y, Z, obok
  suma wszystkich ruchów od zerowania; na dole długość wektora |d|, czas,
  największa prędkość i kąt obrotu w trakcie ruchu. W trakcie ruchu cyfry
  są żółte: to bieżący szacunek bez korekty dryfu, ostateczny wynik
  zastępuje go po zatrzymaniu.
- **Ekran 4:** położenie X/Y/Z w czasie dla ostatniego ruchu, skala
  pionowa dobierana automatycznie (1-2-5 cm).
- **Ekran 5:** tor ruchu w rzucie na płaszczyznę dwóch osi, w których ruch
  był największy (np. X-Y), w tej samej skali w obu osiach; kółko — start,
  kropka — koniec, krok siatki podany na dole.
- **BOOT:** krótko — następny ekran; przytrzymanie ≥1 s — zerowanie sumy.
- Port szeregowy po każdym ruchu:
  `move N: dx=… dy=… dz=… cm, … s, vmax … m/s, turn … deg`.

Algorytm: w spoczynku mierzona jest grawitacja razem z offsetem zera
akcelerometru oraz offset zera żyroskopu. Podczas ruchu żyroskop (bez
offsetu) śledzi orientację kwaternionem od początku ruchu, a każda próbka
przyspieszenia jest obracana do osi z tej chwili. Próbki są buforowane,
grawitacja odejmowana (z korektą interpolowaną między spoczynkiem przed
i po, która wchłania resztkowy dryf orientacji), a przyspieszenie całkowane
do prędkości. Prędkość w spoczynku musi być zerowa, więc prędkość
pozostała na końcu to dryf — usuwany liniowo przed drugim całkowaniem
(ZUPT). Na danych syntetycznych (szum jak w MPU6050, offset akcelerometru
0,3 m/s², offset żyroskopu ok. 3 °/s) błąd wynosi kilka mm dla ruchów
10–50 cm, także z obrotem o 30° w trakcie ruchu.

Ograniczenia:
- Gdy grawitacja po ruchu nie zgadza się z tą sprzed ruchu obróconą
  według żyroskopu (różnica ok. 3,5°), ekran pokazuje
  „BLAD ORIENTACJI: WYNIK NIEPEWNY” — np. obrót szybszy niż ±500 °/s.
- Spoczynek wymaga też nieruchomego żyroskopu (rozrzut < 3 °/s). Drżenie
  ręki może go uniemożliwić; najlepiej odkładać czujnik na powierzchnię.
- Bardzo wolny, równomierny ruch (stała prędkość przez ≥0,3 s) wygląda jak
  spoczynek i zostanie przerwany; zatrzymanie w połowie dzieli ruch na dwa.
- Ruch dłuższy niż 15 s jest porzucany („RUCH > 15 s”).
- Suma wielu ruchów kumuluje błędy; osie są osiami czujnika (nadruk).

### Wykresy w przeglądarce (Wi-Fi)

Płytka tworzy własną sieć Wi-Fi (punkt dostępowy, bez internetu):

- sieć **Wibrometr**, hasło **wibrometr**;
- strona: **http://192.168.4.1/** (każdy inny adres też przekierowuje na nią);
- ekran „WYKRESY W PRZEGLADARCE” na płytce (przedostatni) podaje te dane
  i liczbę otwartych stron.

Strona pokazuje na żywo próbki 200 Hz: przyspieszenie X/Y/Z
i prędkość kątową X/Y/Z jako przewijane wykresy (okno 5, 10 albo 30 s,
wstrzymanie, opcja „same drgania” odejmująca średnią z okna) z bieżącą
wartością przy końcu każdego śladu. Niżej ostatni ruch z miernika
przemieszczenia: wynik X/Y/Z w cm, sumy, wykres położenia w czasie, tor ruchu
oraz przycisk „Wyzeruj sumę” (działa jak przytrzymanie BOOT).

Technicznie: ESPAsyncWebServer, WebSocket `/ws`; co 50 ms płytka wysyła
paczkę JSON z nowymi próbkami i stanem miernika, po każdym ruchu — ruch
z torem (`src/WebLink.*`). Strona jest w `src/WebPage.h` (bez zewnętrznych
plików, działa offline). Z włączonym prototypem nawigacji Wi-Fi
(`ipsEnabled`) punkt dostępowy nie startuje.

### Ekrany analizy (`imuEnabled = true`)

Przebieg czasowy, FFT osi X/Y/Z i licznik ruchów korzystają z tych samych
próbek 200 Hz. Wyłączone domyślnie (nie liczą się wtedy w ogóle).

**Przebieg czasowy**
- X czerwony, Y zielony, Z niebieski; jednostka m/s².
- Ostatnie 4 s, skala stała ±20 m/s², nowe dane po prawej.
- Każdy przedział 40 ms pokazuje min/max oraz połączenie między próbkami.
  To obwiednia pików z odczytów, a nie prezentacja wszystkich punktów.
- POZA SKALA oznacza obcięcie widoku; dane nie są automatycznie skalowane.
- Przerwy w próbkach pozostawiają puste przedziały, bez sztucznych zer.
- UTRATA PROBEK: przepełnienie kolejki w dowolnym momencie od startu.

**FFT osi X, Y, Z**
- Widmo amplitudowe z ostatnich 512 próbek (ok. 2,6 s przy 200 Hz),
  okno Hanna, usunięta składowa stała (grawitacja i offset nie wpływają
  na wykres). Rozdzielczość ok. 0,4 Hz, zakres 0 Hz – połowa faktycznej
  częstotliwości próbek (100 Hz). Filtr czujnika ma pasmo 94 Hz, więc
  powyżej ok. 90 Hz sygnał jest już tłumiony. Oś Hz skaluje się według
  zmierzonego tempa próbek, a czas każdej próbki to chwila przerwania INT.
- Oś pionowa: amplituda sinusoidy w m/s² rysowana jako linia, skala sztywna
  0–5 (stała `fftRange`); większe prążki są obcinane. Wiersz statusu podaje
  częstotliwość i amplitudę największego prążka.
- ZBIERANIE n/512: okno jeszcze się wypełnia (po starcie, wznowieniu
  pauzy lub przerwie w próbkach dłuższej niż 100 ms).

**Licznik ruchów**

Licznik powtarzalnych ruchów posuwistych ręki trzymającej czujnik. Duża
liczba to **pełne ruchy** (tam i z powrotem), obok tempo w ruchach na minutę.
Dostrojony do 0,7–5 ruchów/s i skoku od kilku do kilkunastu cm.
- MPU6050 nie ma fuzji czujników, więc licznik pracuje na zwykłym
  przyspieszeniu z grawitacją, którą odfiltrowuje filtr górnoprzepustowy
  (ok. 1 s). **Przechylanie ręki przesuwa grawitację między osiami tak samo
  mocno jak sam ruch, więc wynik jest wyraźnie słabszy niż na czujniku
  z przyspieszeniem liniowym** (tego licznik był strojony). Nie testowano
  na prawdziwym ruchu.
- Kierunek ruchu wyznaczany jest automatycznie ze wszystkich trzech osi
  (dominujący kierunek z ostatnich ok. 0,5 s), więc ułożenie czujnika
  w dłoni nie ma znaczenia. Ekran pokazuje bieżący kierunek w osiach czujnika.
- Przyspieszenie jest wygładzane i całkowane do sygnału zbliżonego do
  prędkości: każde przesunięcie daje jeden wyraźny impuls; dwa impulsy
  w przeciwne strony to jeden ruch. Próg to 20% ostatniego szczytu
  (zanik w ok. 1 s, minimum 0,04 m/s).
- Pasek pokazuje bieżący sygnał względem progu (żółte znaczniki). Licznik
  działa niezależnie od pauzy na innych ekranach.
- **BOOT na tym ekranie:** krótkie naciśnięcie kasuje licznik, przytrzymanie
  ≥1 s przechodzi do następnego ekranu.

**BOOT na pozostałych ekranach analizy:**

| Naciśnięcie | Działanie |
|---|---|
| krócej niż 1 s | następny ekran |
| 1–3 s | pauza / wznowienie wykresu i FFT |
| od 3 s | ustawienie zera (odjęcie ostatniego odczytu XYZ) albo, gdy zero jest ustawione, powrót do pełnego przyspieszenia |

Pauza zatrzymuje tylko widok; odczyt i status działają dalej. Wznowienie
i zmiana offsetu czyszczą historię.

### Akumulator

- Procent naładowania, napięcie ogniwa i pasek stanu z układu pomiaru
  na płytce (I2C, ta sama magistrala co MPU6050). Odczyt co 2 s, w zadaniu
  czujnika — jedynym, które używa magistrali.
- Nowsze płytki 5483 mają MAX17048: stan ładowania wynika ze znaku
  szybkości zmiany naładowania (%/h): LADOWANIE, ROZLADOWANIE,
  NALADOWANY lub BEZ ZMIAN. Ładowarka nie ma wyjścia statusu na GPIO,
  więc to jedyna dostępna informacja; po starcie wartość ustala się
  przez pewien czas.
- Starsze płytki z LC709203F pokazują tylko procent i napięcie — ten
  układ nie odróżnia ładowania od rozładowania.
- BRAK ODCZYTU UKLADU POMIARU: układ nie odpowiada (np. brak akumulatora,
  bo MAX17048 jest zasilany z ogniwa). Ponowna próba co 5 s.

**BOOT (GPIO0), akcja przy puszczeniu**, na ekranach przyspieszenia i
akumulatora: krótko — następny ekran; na ekranie przyspieszenia ≥1 s — tara.
Na ekranie widać wskazówkę zależną od czasu przytrzymania. RESET płytki
restartuje firmware. Nie trzymaj BOOT podczas resetu, jeśli nie chcesz
uruchomić bootloadera. Płytka 5483 ma tylko jeden przycisk użytkownika;
nie używamy D1/D2.

## Nawigacja wewnętrzna (Wi-Fi RSSI) — prototyp

> Wyłączona (`ipsEnabled = false` w `src/main.cpp`): w praktyce działała
> słabo. Opis poniżej dotyczy kodu, który pozostał w repozytorium.

Eksperyment: szacowanie położenia w pomieszczeniu na podstawie siły
sygnałów okolicznych punktów dostępowych Wi-Fi (fingerprinting RSSI).
Działa autonomicznie — bez Internetu, serwera i bez łączenia się z siecią;
ESP32 jedynie skanuje eter. **Pozycja jest szacunkiem z interpolacji trzech
punktów odniesienia, a nie pomiarem w metrach.** Przy trzech punktach
spodziewaj się błędów rzędu metrów; to demonstrator, który pozwala ocenić,
na ile fingerprinting działa w danym miejscu.

![Ekran nawigacji: brak kalibracji, kalibracja, pomiar, błąd punktu, pozycja OK, niska jakość](docs/indoor-navigation-screens.png)

*Podgląd wyrenderowany na komputerze tym samym kodem i biblioteką GFX co
na urządzeniu (skala 2×).*

### Obsługa (ekran mapy)

| Stan | BOOT krótko | BOOT ≥1 s | BOOT ≥3 s |
|---|---|---|---|
| mapa (zwykły widok) | następny ekran | rozpocznij kalibrację | usuń kalibrację z pamięci |
| kalibracja — czekanie na punkt | zmierz punkt | anuluj (stara kalibracja zostaje) | — |
| kalibracja — pomiar | — | anuluj | — |

Kalibracja zapisuje się w pamięci nieulotnej (NVS) i przetrwa restart.
Nowa kalibracja zastępuje starą dopiero po pomiarze wszystkich trzech punktów.

### Kalibracja — trzy punkty

Trójkąt równoboczny o boku `CALIBRATION_SIDE_M` (domyślnie 5 m):
A (0, 0), B (5, 0), C (2,5, 4,33) — współrzędne liczone z długości boku.

1. Przytrzymaj BOOT 1 s na ekranie mapy → „Przejdź do punktu A”
   (punkt docelowy miga na mapie).
2. Stań w punkcie A, naciśnij BOOT. Urządzenie wykonuje 15 skanów
   (ok. 25 s, zależnie od czasu skanu) — ekran pokazuje np. „Kalibracja A: 7/15”. Stój w miejscu
   i nie zasłaniaj urządzenia.
3. To samo w B i C. Po C kalibracja zapisuje się i od razu włącza się
   lokalizacja.

Jeśli w punkcie zostanie mniej niż 3 użyteczne sieci, ekran pokaże „Za mało
sieci” — pomiar tego punktu można powtórzyć tym samym przyciskiem.

Agregacja punktu (dla każdego BSSID osobno; SSID i kanał są zapisywane
pomocniczo, bo SSID nie jest unikalny):
- odrzucenie wartości odstających: dalej niż max(6 dB, 3·1,4826·MAD)
  od mediany;
- zapis średniej, odchylenia, liczby wykryć i liczby skanów;
- pominięcie AP widocznych w mniej niż 50% skanów, słabszych niż
  −90 dBm albo z odchyleniem > 8 dB;
- najwyżej 24 najsilniejsze AP na punkt.

### Algorytm lokalizacji

Co ok. 2,5 s (skan aktywny 120 ms na kanał, ok. 1,6 s; w tle — interfejs
działa dalej):
1. Bieżący wektor RSSI po BSSID porównywany jest z każdym punktem jako
   ważony RMS różnic w dB (bez modelu tłumienia; RSSI nie jest liniowe
   względem odległości):
   - waga AP = wykrywalność / (σ² + 2²) — stabilne AP ważą więcej;
   - AP nieobecny w profilu punktu liczy się jako −100 dBm (brak też
     niesie informację);
   - AP niewidoczny w bieżącym skanie ma połowę wagi i liczy się tylko
     różnica między punktami (może to być chwilowy zanik albo wyłączony AP);
   - każda różnica jest obcięta do 12 dB, a najgorzej pasujący AP
     odrzucony — pojedynczy niestabilny AP nie przeważy wyniku;
   - nowe, nieznane BSSID są pomijane.
2. Wagi punktów `1 / (d + 1 dB)³`, pozycja = średnia ważona A, B, C.
   W tej wersji wynik leży zawsze w trójkącie.
3. Mniej niż 3 rozpoznane AP → `NO_POSITION`.
4. Confidence 0–100% to heurystyka (nie gwarancja dokładności): jakość
   najlepszego dopasowania × pokrycie znanych AP × rozróżnialność punktów
   × liczba AP. Poniżej 40% → `LOW_CONFIDENCE`.

Filtr pozycji: EMA (α = 0,35, mniejsza przy niskim confidence); skok
> 2 m wymaga potwierdzenia drugim pomiarem w tym samym miejscu (rzeczywisty
ruch pokaże się o jeden pomiar później, pojedynczy zły skan — wcale);
strefa martwa 0,15 m. Bieżąca pozycja nie jest zapisywana do flash.

Na mapie: zielona/żółta kropka — pozycja po filtrze, mały okrąg — ostatni
surowy wynik.

### Konfiguracja i diagnostyka

Parametry w `src/indoor_positioning/IpsConfig.h`; najważniejsze można
nadpisać w `platformio.ini` (`build_flags`, przykłady zakomentowane):
`CALIBRATION_SIDE_M`, `IPS_SCANS_PER_POINT`, `IPS_DEBUG`.

`-D IPS_DEBUG=1` wypisuje na Serial: listę BSSID/RSSI z każdego skanu,
zapisane fingerprinty A/B/C, odległości od nich, wagi interpolacji,
pozycję przed i po filtrze, confidence, czas skanu i czas obliczeń.

### Architektura

`src/indoor_positioning/`:
- `WifiFingerprint` — typy danych i agregacja kalibracji (czysty C++),
- `FingerprintLocator` — dopasowanie i interpolacja (czysty C++, dowolna
  liczba punktów ≥ 2 — gotowe na więcej punktów kalibracyjnych),
- `PositionFilter` — wygładzanie (czysty C++),
- `WifiScanner` — nieblokujące skanowanie bezpośrednio przez ESP-IDF
  (`esp_wifi`); biblioteka Arduino `WiFi` jest celowo pominięta, bo ciągnie
  źródła `Network`/lwIP, które na Windowsie nie kompilują się po skróceniu
  ścieżek include przez pioarduino (`lwip/sockets.h: No such file`),
- `FingerprintStore` — zapis w NVS (Preferences),
- `IndoorPositioning` — maszyna stanów i API: `startCalibration()`,
  `confirmCalibrationPoint()`, `cancelCalibration()`, `isCalibrated()`,
  `startPositioning()`, `stopPositioning()`, `getPosition()`,
  `getCalibrationProgress()`, `resetCalibration()`.

`src/NavigationScreen` rysuje ekran ze stanu (bez dostępu do radia).
Wszystkie bufory mają stały rozmiar (bez alokacji na skan). Po dodaniu
Wi-Fi firmware zajmuje 61% flash (+0,5 MB) i 27% statycznego RAM (+45 KB);
sterownik Wi-Fi rezerwuje dodatkowo pamięć dynamiczną w czasie działania.

### Testy na komputerze

```text
g++ -std=c++17 -Isrc/indoor_positioning test/ips_checks.cpp src/indoor_positioning/WifiFingerprint.cpp src/indoor_positioning/FingerprintLocator.cpp src/indoor_positioning/PositionFilter.cpp -o ips_checks && ./ips_checks
```

Syntetyczne pomieszczenie (AP, tłumienie, szum, zaniki) sprawdza:
agregację i odrzucanie wartości odstających, pozycję w A/B/C i pomiędzy,
brak części AP, nowe nieznane AP, duże wahania RSSI (8 dB), pojedynczy
szalejący AP, brak danych, nierozróżnialne punkty, dzielenie przez zero
i stabilność nieruchomego punktu z filtrem. Wynik nieruchomego punktu
w środku trójkąta (najbardziej niejednoznaczne miejsce) waha się
w symulacji w zakresie ok. ±0,65 m przy 2 dB szumu.

### Sprawdzenie na urządzeniu

1. Wgraj firmware (najlepiej z `-D IPS_DEBUG=1`) i otwórz monitor portu.
2. Odmierz trójkąt o boku 5 m (lub ustaw `CALIBRATION_SIDE_M`), zaznacz
   punkty. Pomieszczenie powinno „widzieć” co najmniej kilka sieci.
3. Wykonaj kalibrację A → B → C; po każdym punkcie Serial podaje liczbę
   użytecznych AP. Zrestartuj płytkę i sprawdź, czy kalibracja wczytuje się
   z NVS („IPS: calibration loaded from NVS”).
4. Stań kolejno w A, B, C i w środku boków; zanotuj pozycję po ustaleniu
   (kilka aktualizacji) i confidence. Postój 1–2 min w jednym miejscu
   pokaże rozrzut pozycji.
5. Przejdź z A do B: kropka powinna podążać z opóźnieniem 1–2 aktualizacji.
6. Sprawdź „BOOT ≥3 s” (usunięcie kalibracji) i anulowanie kalibracji.

Ograniczenia: trzy punkty dają tylko interpolację wewnątrz trójkąta;
ludzie, drzwi i zmiana położenia AP zmieniają odczyty (kalibrację trzeba
wtedy powtórzyć); telefony i hotspoty mogą się pojawiać i znikać; orientacja
urządzenia i ciało użytkownika tłumią sygnał o kilka dB.

## Budowanie i wgranie

PlatformIO Core lub rozszerzenie PlatformIO w VS Code:

```powershell
pio run
pio run -t upload --upload-port COM6
pio device monitor --port COM6 --baud 115200
```

COM6 to port wykryty podczas tworzenia projektu; może zmienić się po
resecie lub na innym komputerze. Sprawdzenie: `pio device list`.
Po wgraniu monitor podaje adres MPU6050 i `WHO_AM_I` (0x68 — oryginał; inna
wartość to prawdopodobnie klon, zwykle też działa), informację „INT works”,
a co sekundę `rate=` (oczekiwane ok. 200 Hz) i liczbę utraconych próbek.
Jeżeli automatyczny upload nie działa: przytrzymaj BOOT, naciśnij RESET,
puść BOOT, sprawdź port i ponów upload.

Platforma jest przypięta do pioarduino 55.03.36 (Arduino 3.3.6, IDF 5.5.2).
Biblioteki: ST7735/ST7789 1.11.0 i LC709203F 1.3.4; MPU6050 obsługuje własny
sterownik (`src/Mpu6050.*`), bez biblioteki.
Pakiety PlatformIO trafiają do ${sysenv.TEMP}/vibrationSensor-pio-core;
pliki kompilacji do ${sysenv.TEMP}/vibrationSensor-pio-build.
Oba katalogi znajdują się poza Dyskiem Google i ścieżką zawierającą polskie
znaki (linker nie obsługuje jej poprawnie). Pierwsza instalacja
pobiera kompilator i framework. Nie dodawać .pio do git.

## Testy i sprawdzenie na sprzęcie

Testy `test/signal_checks.cpp` wykonują rzeczywistą logikę jako
C++17 constexpr/static_assert: min/max, przerwy, kolejność po zawinięciu
bufora, offset, czyszczenie, rollover czasu, licznik ruchów oraz
dekodowanie próbek MPU6050 (kolejność bajtów, znak, skala). Można je
sprawdzić bez podłączonej płytki dowolnym kompilatorem C++17:

```text
g++ -std=c++17 -Iinclude -fsyntax-only test/signal_checks.cpp
```

Miernik przemieszczenia sprawdza `test/displacement_checks.cpp` (ruchy
w jednej i kilku osiach, wolny ruch, tam i z powrotem, obrót, ruch za długi):

```text
g++ -std=c++17 -O2 -Iinclude test/displacement_checks.cpp -o dc && ./dc
```

1. Po starcie potwierdź na porcie szeregowym `MPU6050: addr 0x68 ...` oraz
   `MPU6050: INT works`; `rate=` ok. 200 Hz, drops=0.
2. Połóż płytkę płasko: Z ≈ +9,8 m/s², X i Y bliskie zeru. MPU6050 bez
   kalibracji ma typowo ±0,5 m/s² przesunięcia zera — to normalne; tara
   (BOOT ≥1 s) je odejmuje.
3. Obracaj czujnik: składowa grawitacji powinna przechodzić między osiami,
   a moduł |a| zostać około 9,8 m/s² (1 g).
4. Lekkie stuknięcie powinno być widoczne jako szczyt na paskach.
5. Odłącz INT (A5) przy włączonym zasilaniu i zresetuj: powinien być
   komunikat „no INT … polling every 5 ms”, a odczyt dalej działać.
6. Odłącz SDA podczas pracy: ekran pokaże `BRAK MPU6050`, a po
   podłączeniu odczyt wróci w ciągu kilku sekund. Nie zwieraj pinów.
7. Sprawdź odczyt przez kilka minut w docelowym montażu na przewodzie
   docelowej długości; zanotuj spadki częstotliwości.

## Interpretacja i ograniczenia

Akcelerometr mierzy grawitację razem z przyspieszeniami ruchu robota.
Tara odejmuje stały wektor, a nie zmienną grawitację podczas obrotu
ramienia ani przyspieszenia trajektorii. MPU6050 ma tolerancje typowe dla
tanich modułów: przesunięcie zera rzędu ±50 mg (ok. ±0,5 m/s²), błąd
czułości kilka procent i dryf z temperaturą; ta wersja niczego nie
kalibruje.

Czujnik próbkuje 200 Hz z filtrem 94/98 Hz, więc drgania przejazdów są
mierzone do ok. 90 Hz (Nyquist 100 Hz). To nie wystarcza do szerokopasmowej
diagnostyki łożysk. FFT to podgląd do połowy częstotliwości próbek; nie ma
klasyfikacji usterek, zapisu ani oceny według norm przemysłowych.
Sztywność mocowania magnetycznego i przewód również wpływają na wynik.

I2C na ESP32-S3 pracuje tu z timeoutem 20 ms; po 20 kolejnych błędach
odczytu firmware inicjuje magistralę od nowa, co uwalnia ją po zakłóceniu.
Odczyt pracuje w osobnym zadaniu, aby ekran i przyciski nie opóźniały
próbek.

Dokumentacja:
- [Feather: piny, zasilanie, TFT i przyciski](https://learn.adafruit.com/adafruit-esp32-s3-tft-feather/pinouts)
- [Platforma 55.03.36](https://github.com/pioarduino/platform-espressif32/releases/tag/55.03.36)
