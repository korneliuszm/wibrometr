# Rejestrator drgań zawieszek — Waveshare ESP32-S3-Touch-LCD-2

Osobny, prosty firmware (środowisko PlatformIO `waveshare_vibration`) do
pomiaru drgań przejazdów zawieszek. Czujnik QMI8658 mierzy stale (~492 Hz), a
**zapis na kartę TF rusza dopiero, gdy dalmierz VL53L0X zobaczy przejeżdżający
element bliżej niż ustawiona odległość**. Odległość, czas nagrania i pozostałe
parametry dalmierza ustawia się dotykiem na czwartej stronie (są pamiętane po
restarcie). Drgania nie wyzwalają nagrania. Każdy pomiar
to osobny plik CSV. Po włączeniu na ekranie trybu dysku USB pomiary się
zatrzymują, a karta pokazuje się jako dysk.

Sprzęt jest ten sam co w [docs/waveshare-README.md](waveshare-README.md)
(układ pinów, rewizje Rev 1.0 / 2.0, kamera) — tam jest tabela GPIO. Piny
są w `src/waveshare/hal/BoardPins.h`; ten firmware używa wyświetlacza, dotyku,
IMU i karty TF (SPI wspólne z ekranem). Linia rolkowa, Wi-Fi, WWW, FFT,
akumulator i kamera nie są w nim zawarte.

> **Stan:** logika niezależna od sprzętu jest przetestowana na komputerze.
> **Nie był jeszcze uruchamiany na płytce** — lista rzeczy do sprawdzenia
> jest w „Pierwsze włączenie — niesprawdzone na sprzęcie”.

## Obsługa

Pięć ekranów 320×240, przełączane przesunięciem palca w bok (fonty FreeSans;
podgląd na PC: `tools/render_vib.sh`). Ekran 1 (poniżej): wykres i statystyki;
ekran 2: maksima nagrań w sesji; ekran 4: ustawienia dalmierza; ekran 5: przełącznik trybu dysku USB; ekran 3: widma FFT ostatniego pomiaru
**osobno dla osi X, Y i Z** (lewe 2/3: trzy widma jedno pod drugim na wspólnej
skali amplitudy, numerowane znaczniki na najsilniejszych prążkach każdej osi)
i tabela trzech najsilniejszych prążków każdej osi (częstotliwość i amplituda,
interpolowane między binami) w prawej 1/3. Pozwala to przypisać prążek do
osi, czyli np. rolkom górnym (jedna oś) i bocznym (inna oś). Prążek wchodzi
do tabeli, gdy ma co najmniej 5 % najsilniejszego prążka dowolnej osi i 0,01
m/s², więc oś z samym szumem ma pustą tabelę. To osie czujnika (jak na wykresie),
nie osie zawieszki: która oś odpowiada któremu rolkom, wynika z montażu płytki.

```text
y 0    pasek stanu: [● CZUWAM] / [● REC 12.3 s] / [USB: KARTA W PC] ...   karta N   81% [bateria]
y 36   lewe 2/3: panel wykresu: XYZ dynamiczne przyspieszenie, 5,4 s
         dolny wiersz panelu: legenda X Y Z, poziom w m/s2
       prawa 1/3: statystyki ostatniego pomiaru (numer, czas, tabela X Y Z), pod nimi FFT na żywo
y 198  DALMIERZ  245 mm   [pasek z kreską progu]   PROG 300 mm   CZEKAM
```

Wykres pokazuje przyspieszenie z odjętą wolną średnią (grawitacja i przechył
nie są widoczne), osie czujnika (X czerwony, Y zielony, Z niebieski). Skala
pionowa dobiera się do największej wartości w oknie (±5, 10, 20, 40 lub 80
m/s²). Próbki trafiają do kolumn według czasu próbki (nie chwili odebrania),
więc linia jest ciągła także wtedy, gdy odświeżanie ekranu opóźni odbiór.
Podczas nagrywania ramka panelu jest czerwona.

Pasek u dołu pokazuje odległość z dalmierza (mm), a kreska w środku paska to
odległość wyzwalania (pasek: 0 do 2× odległość; czerwony, gdy obiekt jest
bliżej). Stan: `CZEKAM`, `NAGRYWAM`, `USB`, `BRAK ECHA`
(nic w zasięgu) albo `BRAK DALMIERZA` (czujnik nie odpowiedział przy starcie;
wtedy nic nie wyzwala nagrania, trzeba sprawdzić przewody i zrestartować).

Po prawej stronie paska stanu: ikona karty z liczbą plików `M*.CSV` w `/MEAS`
(ikona żółta, gdy od startu karta była naprawiana ponownym montowaniem) i
akumulator (procent z napięcia ogniwa; niebieska błyskawica = ładowanie, na
Rev 1.0 brak tej informacji; `USB` = brak ogniwa).

### Statystyki ostatniego pomiaru (prawy panel)

Po zamknięciu każdego pomiaru (także przy limicie 30 s) rejestrator liczy
statystyki z **tych samych próbek, które trafiły do pliku** (łącznie z 1 s
sprzed wyzwolenia) i pokazuje je po prawej, aż do następnego pomiaru. Po
starcie, zanim cokolwiek się nagra, panel pokazuje `BRAK POMIARU`.

| Wiersz | Znaczenie (m/s², osobno X, Y, Z) |
|---|---|
| `sr` | średnia, razem z grawitacją — pokazuje, jak czujnik jest ułożony |
| `RMS` | wartość skuteczna drgań (odchylenie od średniej) |
| `pik` | największe odchylenie od średniej w obie strony |
| `p-p` | rozstęp, max − min |

Nagłówek panelu: numer pliku (`M0012`) i czas trwania pomiaru, pod nim tabela.

**FFT NA ŻYWO** (dolna część panelu): widmo amplitudowe bieżącego sygnału, czyli
ostatnich 1024 próbek (ok. 2,1 s, okno Hanna), przeliczane ok. 3 razy na sekundę,
niezależnie od nagrywania. Trzy osie nałożone w kolorach X/Y/Z, oś częstotliwości
od 0 do połowy częstotliwości próbkowania (ok. 245 Hz), skala dobiera się do
najwyższego prążka. Liczba w nagłówku i żółty znacznik to najsilniejsza składowa
(powyżej ok. 1 Hz) długości wektora XYZ. Czujnik ma pasmo ok. 137 Hz, więc prążki
powyżej są już tłumione. Po starcie, po przerwie w danych i w trybie dysku USB
widmo zbiera najpierw 2 s próbek (`zbiera probki...`). Liczenie zajmuje ok. 13 ms
(log: `fft … us`) i nie kosztuje czujnika żadnej próbki. Widmo ostatniego pomiaru,
osobno dla każdej osi, jest na stronie 3.

Próbki idą do bufora 196 KB w PSRAM (`MeasCapture` w `include/MeasStats.h`);
bez PSRAM albo gdy poprzedni pomiar jest jeszcze liczony, pomiar zapisuje się
na kartę normalnie, tylko bez statystyk. Obliczenia (kilka milisekund) robi
pętla ekranu, nie zadanie czujnika.

### Pasek stanu

| Napis | Znaczenie |
|---|---|
| `● CZUWAM` (zielony) | czujnik działa, karta gotowa, czeka na obiekt w odległości wyzwalania |
| `● REC 3.2 / 30 s` (czerwony, kropka miga) | trwa zapis pomiaru; czas od wyzwolenia i limit, pod spodem pasek postępu |
| `USB: KARTA W PC` (niebieski) | komputer ma kartę (albo oddawanie jest w toku); nic się nie nagrywa, na wykresie `USB: ZAPIS WSTRZYMANY` |
| `BRAK KARTY` (żółty) | brak karty przy starcie; zapis rusza sam po włożeniu karty (ponowna próba co 1 s), ale dysk USB pojawi się dopiero po RESET |
| `BLAD KARTY` (czerwony) | błąd karty; przyczyna na wykresie, np. `blad zapisu na karte`, `nie mozna utworzyc /MEAS`, `numeracja pelna` |
| `BRAK CZUJNIKA` (żółty) | karta w porządku, ale czujnik nie daje próbek (komunikat jest też na wykresie) |

Kolejność: USB, potem REC (tylko gdy zapis działa), potem problemy z kartą,
potem brak czujnika, na końcu CZUWAM.

### Błędy karty w trakcie pomiaru

Krótkie zakłócenie karty (wstrząs gniazda TF przy mocnym uderzeniu, karta
zajęta zbyt długo) nie kończy już pomiaru błędem: gdy zapis się nie powiedzie,
rejestrator montuje kartę ponownie (do 3 prób) i dopisuje dalej do tego samego
pliku. W pliku pojawia się wtedy wiersz
`# card glitch: remounted, samples since the last flush may be missing`
(dane od ostatniego zrzutu, najwyżej ok. 2 s, mogą zniknąć; luka jest widoczna
w kolumnie `t_ms`). Podobnie gdy nowy plik nie daje się utworzyć: jedno
ponowne montowanie i druga próba. Dopiero gdy to nie pomoże, pojawia się
`BLAD KARTY`, a po 1 s karta jest montowana od nowa.

### Dalmierz (strona 4)

Cztery wiersze z przyciskami `-` / `+`; krok następuje od razu po dotknięciu,
po przytrzymaniu dłużej niż 500 ms powtarza się co 100 ms. Pod wierszami jest
odczyt odległości na żywo (także podczas nagrywania; w trybie dysku USB
dalmierz jest wyłączony i pasek pokazuje `-`), więc odległość można dobrać
„na oko”.

| Parametr | Domyślnie | Zakres i krok |
|---|---|---|
| odległość wyzwalania | 300 mm | 30–1200 mm; krok 10 mm do 500 mm, 50 mm powyżej |
| czas rejestracji po wyzwoleniu | 5 s | 1–30 s, krok 1 s |
| pre-roll (część przed wyzwoleniem) | 1000 ms | 0–1000 ms, krok 100 ms |
| tryb pomiaru dalmierza | NORMALNY | SZYBKI 20 ms / NORMALNY 33 ms / DOKLADNY 200 ms |

Wartości są zapisywane w NVS po puszczeniu przycisku (nie podczas nagrywania,
żeby zapis do flasha nie kosztował próbek) i odtwarzane po restarcie.

Podłączenie VL53L0X (adres I2C 0x29, ta sama magistrala co dotyk i IMU):
3V3, GND, SDA = GPIO48, SCL = GPIO47. Czujnik jest inicjowany raz, przy
starcie; po dołączeniu go później trzeba zrestartować płytkę.

### Start i koniec pomiaru

- Dalmierz mierzy cały czas (ok. 34 razy na s w trybie NORMALNYM), także podczas
  nagrywania; poza trybem dysku USB. Zmierzone: nie kosztuje to akcelerometru
  ani jednej próbki (500 Hz, `missed 0`, 0 błędów I2C).
- Pojedynczy odczyt nie wyzwala (czujnik czasem podaje błędną wartość).
  **Wyzwolenie: co najmniej 3 z ostatnich 5 odczytów poniżej odległości
  wyzwalania i zgodnych ze sobą** (najmniejszy i największy różnią się o
  najwyżej 40 mm albo 20 % większego). Rozproszone losowe odczyty się nie
  zgodzą; prawdziwy obiekt tak. Odczyt „poza zasięgiem” nigdy nie wyzwala.
  Obiekt musi więc być w polu widzenia przez 3 odczyty (ok. 100 ms w trybie
  NORMALNYM, 60 ms w SZYBKIM; dla szybkich przejazdów wybierz SZYBKI).
- Wyzwolenie podczas trwającego pomiaru jest ignorowane.
- Powstaje plik `/MEAS/Mnnnn.CSV`. Zawiera **pre-roll** przed wyzwoleniem
  (domyślnie 1 s; te próbki mają ujemny czas) i **czas rejestracji** po nim
  (domyślnie 5 s; maksimum 30 s). Potem plik dostaje stopkę i się zamyka;
  rejestrator wraca do `CZUWAM`.
- Kolejne wyzwolenie wymaga, żeby obiekt się oddalił (3 z 5 odczytów dalej niż
  odległość + 20 mm albo bez echa). Element stojący przed czujnikiem nie
  tworzy więc serii plików.
- Opóźnienie wykrycia to ok. 30 ms (czas pomiaru dalmierza); pokrywa je pre-roll.
- Gdy plik nie może zostać otwarty, kolejny Start jest ignorowany przez 1 s.

### USB

Po podłączeniu kabla do komputera rejestrator **dalej pracuje i trzyma kartę**;
komputer widzi tylko port szeregowy (log co sekundę). Karta trafia do komputera
dopiero w trybie dysku:

1. Podłącz kabel USB do komputera.
2. Przesuń palcem do ostatniej strony (5, „USB") i dotknij **WLACZ TRYB DYSKU
   USB**. Przycisk działa tylko przy podłączonym kablu i nie podczas nagrywania.
   Trwający pomiar nie jest przerywany; poczekaj na jego koniec.
3. Karta pojawia się jako dysk (producent `WIBROMTR`, produkt `VIBRACJE` w
   danych urządzenia; nazwa woluminu to etykieta samej karty). Pliki są w
   `/MEAS`. Pomiary są wstrzymane (pasek `USB: KARTA W PC`).
4. Skończ: dotknij **ZAKONCZ TRYB DYSKU** (najlepiej po wysunięciu dysku w
   systemie), wysuń dysk albo odłącz kabel. Płytka montuje kartę od nowa i
   wraca do czuwania. Po odłączeniu kabla następne podłączenie znów zaczyna od
   trybu szeregowego.

Przyciski reagują dopiero na stabilny dotyk (min. 60 ms, bez skoków
współrzędnych); krótkie „widmowe” dotknięcia kontrolera dotyku są ignorowane
(w logu: `gest ODRZUCONY`).

Port szeregowy działa równolegle (ta sama wtyczka USB): co sekundę wypisuje
stan (próbki/s, wartości dynamiczne, dalmierz i odległość, tryb, stan karty, liczniki utraconych
próbek). Brak karty przy starcie oznacza, że dysk USB nie jest zarejestrowany —
działa wtedy tylko port szeregowy.

## Format pliku

Pliki: `/MEAS/M0001.CSV`, `M0002.CSV`, … Numer = największy istniejący w
`/MEAS` + 1; nigdy nie nadpisuje istniejącego pliku. Płytka nie ma zegara
czasu rzeczywistego, więc czas w pliku jest względny: `t_ms` to milisekundy
od chwili wyzwolenia (ujemne dla pre-rollu), z trzema miejscami po przecinku.
Przyspieszenia w m/s² w osiach czujnika, trzy miejsca po przecinku.

```text
# wibrometr measurement v1
# device: ESP32-S3-Touch-LCD-2, QMI8658
# measurement: 7
# trigger_distance_mm: 300
# sample_rate_hz: 500.0
# range_g: 8
# pre_trigger_s: 1.0
# record_s: 5.0
# trigger_at: t_ms = 0
# columns: t_ms,ax_m_s2,ay_m_s2,az_m_s2
t_ms,ax_m_s2,ay_m_s2,az_m_s2
-1000.000,0.112,-0.087,9.791
-998.000,0.109,-0.090,9.803
-996.000,0.115,-0.085,9.798
...
# end: samples=2750 dropped=0
```

`sample_rate_hz` w nagłówku to **częstotliwość zmierzona** (po ok. 10 s od
startu), a nie nominalne 500: z samym akcelerometrem czujnik taktuje się
wewnętrznym oscylatorem, na tej płytce ok. **491,5 Hz**. Do analizy widma
używaj wartości z nagłówka albo kolumny `t_ms`, która ma krok równy
rzeczywistemu okresowi.

Czujnik numeruje swoje próbki (rejestr TIMESTAMP), więc każda utracona próbka
jest policzona dokładnie: gdy odczyt się spóźnił, przed stopką jest wiersz
`# missed_samples: N`, a w kolumnie `t_ms` w tym miejscu krok jest
wielokrotnością okresu. Przy podłączonym komputerze ginęło ok. 10% próbek
(pomiary są wtedy i tak wstrzymane); ile ginie na akumulatorze, pokaże stopka
pierwszych plików. Port szeregowy podaje licznik od startu (`missed`),
zmierzoną częstotliwość (`rate`) i liczbę napraw karty (`fix`).

(Wartości w przykładzie są ilustracyjne.) Wiersze z `#` to nagłówek i stopka, a zwykły wiersz `t_ms,ax_m_s2,…` to nazwy kolumn;
`dropped` to próbki utracone przy zapisie (kolejka pełna, karta za wolna).
Otwieranie w Pythonie:

```python
import pandas as pd
df = pd.read_csv("M0007.CSV", comment="#")  # kolumny: t_ms, ax_m_s2, ay_m_s2, az_m_s2
```

Arkusze kalkulacyjne pokażą wiersze `#` jako zwykły tekst; nazwy kolumn są w pierwszym wierszu bez `#`.

## Ustawienia stałe

Zmieniane w kodzie (nie na ekranie):

| Ustawienie | Domyślnie | Gdzie |
|---|---|---|
| częstotliwość próbek | 500 Hz nominalnie, zmierzone ~491,5 Hz (więcej się nie da: patrz niżej) | `vib::kSampleRateHz`, `src/vibration/VibConfig.h` |
| zakres | ±8 g (`kRange = 2`) | `vib::kRange`, `VibConfig.h` |
| filtr sprzętowy czujnika | wyłączony: pasmo 137,5 Hz (wbudowany filtr antyaliasingowy 27,5% ODR); z LPF 13,37% byłoby 66,9 Hz | `vib::kLowPass`, `vib::kLowPassMode`, `VibConfig.h` |
| pre-roll, czas rejestracji, odległość wyzwalania, tryb dalmierza | patrz tabela „Dalmierz (strona 4)” | `include/RangeUi.h` (`rangeui::Settings`), NVS: `rdist`, `rrec`, `rpre`, `rmode` |
| potwierdzenie wyzwolenia / histereza ponownego uzbrojenia | 3 z 5 odczytów w zgodzie (40 mm / 20 %) / 20 mm | `RangeTrigger::kWindow`, `kNeeded`, `kClusterMm`, `kHysteresisMm`, `include/RangeTrigger.h` |
| stała czasowa wolnej średniej (filtr tylko do wykresu) | 1 s | `src/vibration/main.cpp` (`TriggerDetector::Config`), domyślnie `highPassTauS` w `include/TriggerDetector.h` |
| przerwa po odmowie otwarcia pliku (holdoff) | 1 s | `holdoffUs` w konstruktorze `RecorderCore`, `include/RecorderCore.h` |

Pierścień pre-rollu ma 2048 próbek (ok. 4 s przy 500 Hz), `SampleRing<2048>`
w `include/RecorderCore.h`; pre-roll jest ograniczony do 1 s.

### Dlaczego nie więcej niż 500 Hz

Sprawdzone na tej płytce i w dokumentacji QMI8658A (Rev D):

- sam akcelerometr ma najwyżej **1000 Hz** (kody 2/4/8 kHz to „N/A”; wyższe
  częstotliwości istnieją tylko z żyroskopem, 1793,6 / 3587,2 / 7174,4 Hz);
- **FIFO czujnika gubi próbki** podczas odczytu (rozdz. 8.7: w trybie odczytu
  nowe próbki są odrzucane, a odczyt musi się zmieścić w jednym okresie); przez
  I2C 400 kHz to ~135 µs na próbkę, zmierzone straty 35–40% przy 1–2 kHz. Kod
  FIFO jest w repo wyłączony (`vib::kUseFifo`);
- sygnał „dane gotowe” jest tylko na pinie INT2, który nie jest podłączony,
  więc zostaje odpytywanie co 1 ms (tick systemu).

Zadanie czujnika ma priorytet 23 na rdzeniu 0 (tuż pod zadaniem USB), a dotyk
jest czytany tylko wtedy, gdy kontroler sygnalizuje dotknięcie na pinie INT
(GPIO46). Uśpiony kontroler dotyku trzymał magistralę I2C do ~6 ms przy każdym
odczycie.

Do pasma rzędu kilku kHz (wczesna diagnostyka łożysk) potrzebny jest osobny
czujnik drgań na SPI (np. IIS3DWB).

## Budowanie i wgranie

```powershell
pio run -e waveshare_vibration
pio run -e waveshare_vibration -t upload --upload-port COM6
pio device monitor -e waveshare_vibration --port COM6 --baud 115200
```

Środowisko jest zdefiniowane w `platformio.ini` (`[env:waveshare_vibration]`):
ta sama płytka i platforma co `waveshare`, z USB w trybie TinyUSB (CDC +
MSC). Gdy wgranie się nie udaje: przytrzymaj BOOT, podłącz USB, puść BOOT
(tryb pobierania). Pakiety PlatformIO trafiają do
`${sysenv.TEMP}/vibrationSensor-pio-core`, kompilacja do
`${sysenv.TEMP}/vibrationSensor-pio-build`, jak w
[docs/waveshare-README.md](waveshare-README.md).

## Testy

Bez płytki, na komputerze (g++ z C++17):

```text
g++ -std=c++17 -Wall -Wextra -Iinclude test/vibration_checks.cpp -o vc && ./vc
```

`vibration_checks` obejmuje sekcje: format plików (nagłówek, wiersze próbek
z ujemnym czasem, stopka, ścieżki i numeracja `M0001`–`M9999`), pierścień
próbek, wyzwalacz dalmierza (`RangeTrigger`: potwierdzenie, histereza, brak
echa) i dekodowanie wyniku VL53L0X, rdzeń rejestratora (wyzwolenie z zewnątrz,
pre-roll, czas nagrania, odmowa otwarcia, holdoff, przerwa w danych, tryb USB),
logikę strony ustawień (kroki, przyciski, zakres wykresu, przytrzymanie), własność karty (`UsbOwnership`, `CardGate`) i oddawanie karty
(`CardHandover`).

## Pierwsze włączenie — niesprawdzone na sprzęcie

Wszystko poniżej jest **niesprawdzone na sprzęcie**:

1. **Wykrywanie hosta USB i oddanie karty:** podłączenie podczas pomiaru →
   plik zamknięty ze stopką `# end: …`; odłączenie w trakcie kopiowania;
   wysunięcie dysku, a potem ponowne zamontowanie.
2. **`msc.begin` z liczbą sektorów karty:** dysk i pliki są widoczne w Windows
   i w Linuksie.
3. **Zapis 500 Hz bez utraconych próbek** (`dropped` w stopce i na porcie
   szeregowym) razem z odświeżaniem ekranu na wspólnym SPI.
4. **Orientacja dotyku:** przyciski −/+ na stronie 4 reagują tam, gdzie są narysowane.
5. **Dalmierz VL53L0X:** czy inicjuje się przy starcie (log `Dalmierz: VL53L0X
   gotowy`), czy odczyty na stronie 4 zgadzają się z linijką, czy odległość
   wyzwalania dobrana „na oko” działa na prawdziwym elemencie (połysk, ciemne
   lub przezroczyste powierzchnie i światło słoneczne pogarszają odczyt), czy
   przy pracy dalmierza `missed` czujnika drgań nie rośnie ponad zwykłe wartości
   oraz czy status odczytu (kod 11 = poprawny) jest właściwy dla tego egzemplarza.
   Diagnostyka: w logu co sekundę zakres odczytów, liczba odczytów, braków echa i
   błędów I2C; przy każdym wyzwoleniu ostatnie 16 odczytów. Na porcie szeregowym:
   `d` włącza/wyłącza wypisywanie każdego odczytu (status, sygnał, światło), `f`/`n`/`p`
   ustawia tryb dalmierza (szybki/normalny/dokładny), `k` uruchamia kalibrację dotyku.
6. **Uśpienie/autosuspend hosta z zamontowanym dyskiem:** host usypiający
   zamontowany wolumin oddaje kartę firmware'owi pod swoim buforowanym
   systemem plików (znane ryzyko; uszkodzenie systemu plików możliwe).
7. **Cały build:** `pio run` **nie był uruchomiony** w środowisku, w którym
   powstał kod (rejestr PlatformIO był niedostępny); z toolchainem ESP32
   skompilowano tylko nowe źródła. Potwierdź `pio run` (CI uruchamia go dla
   wszystkich trzech środowisk), zanim zaufasz binarce.

## Ograniczenia

- Czas względny w pliku to int32 w mikrosekundach (zawinąłby się po ok. 35,8 min;
  przy limicie 30 s nie ma to znaczenia).
- Numeracja kończy się na `M9999`: kolejny pomiar kończy się błędem
  `numeracja pelna`, nic nie jest nadpisywane.
- Szybkie odłączenie USB w trakcie pomiaru zamyka plik ze stopką
  `# end: samples=… dropped=…` (pomiar jest krótszy, ale poprawny).
- Dalmierz jest inicjowany przy starcie, po sprzętowym resecie samego układu
  (reset płytki go nie resetuje: czujnik mierzący w chwili restartu wracał
  „ślepy”, ze światłem otoczenia ok. 9 Mcps i samymi losowymi odczytami). Gdy
  mimo to światło otoczenia przez ok. 3 s jest wysokie (> 4 Mcps), inicjuje się
  ponownie (najwyżej co 30 s). Bez dalmierza przy starcie nie ma wyzwalania do
  restartu. Wyzwalanie drganiami nie istnieje (został tylko filtr do wykresu).
- Zasięg VL53L0X w trybach domyślnych to ok. 1,2 m (dlatego maksimum 1200 mm).
- Karta włożona po starcie: zapis wznawia się sam (próba co 1 s), napis `BRAK KARTY` znika. RESET jest potrzebny tylko dla dysku USB (rejestrowany przy starcie).
- Płytka nie ma zegara czasu rzeczywistego: w plikach nie ma daty.
