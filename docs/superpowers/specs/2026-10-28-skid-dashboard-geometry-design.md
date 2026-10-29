# Spec B — dashboard: geometria trasy z CSV i mapa drgań

Status: zatwierdzony do planu implementacji (2026-10-28).
Zależy od Spec A tylko przez istniejące pola `lineInfo.gaps[]` / `gdir[]`
(backend już je wysyła; Spec A tylko czyni je autorytatywnymi z CSV).
Powiązana notatka (Obsidian): `2026-10-28-dashboard-skid-mapa-drgan.md`.

## Cel

Zamienić stały układ liniowy stołów (`src/WebPage.h`, `buildScene()`,
`sx(s) = M + s*U`) na rzeczywistą geometrię 2D (X/Y) wynikającą z
kierunków i odległości odcinków, i dodać wizualizację drgań per stół.

## Zakres (odchudzony — bez historii przejazdów i live-wykresów)

**Wchodzi:** mapa geometryczna, odcisk Wi-Fi (jak dotąd), fakty linii,
mini-słupki drgań X/Y/Z per stół, kolor tła kafelka = max drgań.

**Wychodzi z tej iteracji (usunięte z dashboardu, nie tylko "poza
zakresem"):** tabela historii przejazdów (`tr-table`/`trBody`/sparklines),
live-wykresy przyspieszenia/żyroskopu (`#accel`, `#gyro` canvas). Mniej
JS do przepisania, mniej kodu do utrzymania.

## Geometria

`lineInfo.gaps[i]` (metry) i `lineInfo.gdir[i]` (stopnie) **już istnieją**
w payloadzie WS (dziś zasilają tylko tekst "Kierunki odcinków") — żadnego
nowego formatu danych nie trzeba.

```
pos[0] = (0, 0)
pos[s+1] = pos[s] + gaps[s] * SCALE * unit(gdir[s])
```

`gdir[s] == null` → kierunek 0° (X+) — to jest dokładnie dzisiejsza linia
prosta, bez specjalnej ścieżki fallback (ten sam kod dla CSV i auto-uczenia).

`viewBox` liczony z bounding-boxa wszystkich `pos[s]` + margines na ramki
stołów (nie ze sztywnego wzoru szerokości jak dziś).

Autko: interpolacja 2D między `pos[floor(t)]` i `pos[ceil(t)]`, obrót
sprite'u = `gdir` aktualnego segmentu. Pas rolkowy i linie wymiarowe:
po jednym krótkim segmencie na odcinek, obracane pod kątem `gdir[i]`
(dziś jeden ciągły prostokąt na całą linię).

## Mapa drgań

- Nowa tablica `vib: [[x,y,z], ...]` w payloadzie WS — jedno pole
  `float stationVibMax[maxStations][3]` w `Tracker`, aktualizowane przy
  przyjeździe z już liczonych RMS/peak przejazdu (dane istnieją, tylko
  nie trzymane per-stacja dziś).
- Mini-słupki X (czerwony) / Y (zielony) / Z (niebieski) na kafelku,
  skala **wspólna stała** `VIB_SCALE` (hardkodowana wartość startowa —
  progi nie są jeszcze ustalone z rzeczywistych przejazdów, zgodnie
  z notatką).
- Kolor tła kafelka = `max(x,y,z) / VIB_SCALE`, **oddzielnie** od klas
  `.here` / `.sel` (te zostają obramowaniem/pierścieniem stanu, nie tłem) —
  żeby kolor tła jednoznacznie oznaczał intensywność drgań.

## Test

Geometria to pętla z trygonometrią (branch + loop) — jeden mały check,
nie framework: czysta funkcja `pos(gaps, gdir)` wydzielona, sprawdzona
`node -e` asercjami (node jest już w toolchainie PlatformIO), analogicznie
do istniejących `g++ ... && ./test` w README.

## Aktualizacja 2026-10-29

Wykresy przyspieszenia i żyroskopu zostały na stronie, ale pokazują ostatni
przejazd (od ruszenia do zatrzymania), nie przebieg na żywo. Tabela
przejazdów została usunięta — firmware nie wysyła już komunikatów `tr`.
Strona pokazuje też brak prawidłowego `config.csv` (pola `cfg`, `cfgLine`,
`cfgErr` w komunikacie `ln`); wybór liczby stołów usunięty (liczba z pliku).
