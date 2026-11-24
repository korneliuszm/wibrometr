# Wyzwalanie rejestracji dalmierzem VL53L0X — projekt

## Cel
Rejestrację drgań (env `waveshare_vibration`) wyzwala dalmierz VL53L0X patrzący na przejeżdżający element: start, gdy odległość spadnie poniżej progu. Próg drgań z akcelerometru przestaje wyzwalać. Dalmierz służy wyłącznie do startu; w trakcie nagrywania nie jest odpytywany (nie obciąża magistrali I2C używanej przez IMU).

## Zakres
- Usunięcie przycisków −/+ progu drgań ze strony 1 (i linii progu / paska poziomu względem progu).
- Nowa strona 4 „Dalmierz" z parametrami, zapisywanymi w NVS.
- Nie zmieniamy: wykresu XYZ, statystyk, FFT, maksimów, zapisu na kartę, USB, pre-rollu jako mechanizmu.
- Poza zakresem: wyzwalanie drganiami jako zapas, sprzętowy próg dalmierza (GPIO1).

## Podejścia
A. (wybrane) Odpytywanie w trybie czuwania: task czyta VL53L0X ~30 Hz tylko w stanie Armed, przy starcie pomiaru zatrzymuje ranging. 4 przewody, opóźnienie ~30 ms pokryte pre-rollem.
B. (odrzucone) Sprzętowy próg na GPIO1: brak wolnego pinu, przeprogramowanie przy każdej zmianie, brak odczytu na żywo.

## Projekt
- **Sterownik** `hal/Vl53l0x.{h,cpp}` na `hal::I2cBus` (mutex), własny (biblioteki używają `Wire` bezpośrednio). Czysta logika (dekodowanie, wyzwalacz) w `include/` i testy na PC.
- **Wyzwalacz** `include/RangeTrigger.h`: odległość < próg przez N=2 kolejne ważne odczyty → Start. Ponowne uzbrojenie dopiero po odległości > próg + histereza. Nieważne odczyty (poza zasięgiem) = „daleko".
- **RecorderCore**: zewnętrzne wyzwolenie (`requestStart()` atomowo z tasku dalmierza); koniec nagrania po ustawionym czasie od wyzwolenia (limit 30 s zostaje); pre-roll konfigurowalny 0–1 s; `TriggerDetector` zostaje tylko do filtra „dynamic" dla wykresu.
- **Task dalmierza**: ranging tylko gdy tryb = Armed; przy Recording/Usb zatrzymany; wznowienie po zakończeniu nagrania.
- **Parametry (NVS, `vib`)**: odległość wyzwalania (mm), czas rejestracji (s, 1–30), pre-roll (0–1 s), tryb pomiaru (szybki 20 ms / normalny 33 ms / dokładny 200 ms). Zapis do NVS po puszczeniu palca i nie w trakcie nagrywania (jak dotąd).
- **UI**: strona 4 z wierszami −/+ i odczytem odległości na żywo (podczas nagrywania „—"); strona 1: pasek „odległość / próg" zamiast przycisków; „BRAK DALMIERZA" gdy sensor nie odpowiada. `kPages` = 4.

## Decyzje własne (do sprawdzenia)
Wartości domyślne (próg 300 mm, czas 5 s, pre-roll 1 s, tryb normalny), N=2, histereza, zakresy parametrów.
