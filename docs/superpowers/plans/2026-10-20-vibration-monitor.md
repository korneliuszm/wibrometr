# Vibration Monitor Implementation Plan

> **For agentic workers:** Use superpowers:executing-plans inline.

**Goal:** Pierwsza wersja monitora według zatwierdzonego zakresu.
**Architecture:** Odczyt SH2 niezależny od renderowania; historia min/max.
**Tech Stack:** PlatformIO, pioarduino, Adafruit BNO08x, ST7789, GFX.
**Spec:** ../specs/2026-10-20-vibration-monitor-design.md

## Global Constraints
SDA 42 / SCL 41; 100 Hz / 25 Hz; trzy osie; aktualny katalog.

## Review Focus
Brak czujnika: retry. Brak próbek: przerwa. Reset: odtworzenie raportów.
Pauza/offset: usunięcie nieciągłej historii. Piki: min/max i clipping.

## Task 1
- [ ] Konfiguracja i test/signal_checks.cpp: compile-time testy rzeczywistej logiki.
- [ ] Sprawdzić RED przed dodaniem include/SignalHistory.h.
- [ ] Dodać SignalHistory.h i potwierdzić GREEN.

## Task 2
- [ ] src/main.cpp: I2C, TFT, debounce, status, retry.
- [ ] README: połączenia, obsługa, ograniczenia, testy sprzętowe.
- [ ] pio run: SUCCESS; przegląd zgodności i ograniczeń.

Pusty katalog bez git: nie tworzyć repo ani commitów.

