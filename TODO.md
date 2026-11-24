# TODO — mcx

Sugestie dalszego rozwoju na podstawie dotychczasowych prac nad **mcx** (`v1.0.0.2`).

## 1. System plików i wydajność (C++20 / macOS)
- [x] **Płynny postęp kopiowania dużych plików (`copyfile`)** — zastąpić `std::filesystem::copy` w `FileSystemService::transferItemsSync` natywnym dla macOS API `<copyfile.h>` (`COPYFILE_PROGRESS`), aby pasek postępu aktualizował się bajt po bajcie w trakcie kopiowania pojedynczego dużego pliku.
- [x] **Podgląd obrazków w Quick View (`Ctrl+Q`) i Listerze (`F3`)** — ładować tekstury `MTLTexture` przez `MTKTextureLoader` w `MacBridge.mm`, aby pliki `.png`/`.jpg`/`.webp` wyświetlały się bezpośrednio w panelu podglądu.
- [x] **Wątek roboczy dla Quick View (`Ctrl+Q`) i `diff -u`** — przenieść odczyt większych plików i wywołanie `ToolsService::diffFilesSync` poza główny wątek UI (przez `postMain`), żeby przy większych plikach interfejs nigdy nie przyciął klatki.
- [x] **Bezpieczne zamykanie wątków przy wyjściu** — zastąpić odłączone wątki (`std::thread(...).detach()`) przez `std::jthread` / pulę wątków w `CommanderEngine`, aby uniknąć wyścigu z destruktorem przy zamykaniu aplikacji w trakcie operacji w tle.

## 2. Funkcjonalności Total Commander / Midnight Commander
- [x] **Przeglądanie wnętrza archiwów `.7z`/`.zip` jak zwykłych folderów (VFS)** — wejście przez `Enter` do środka archiwum w panelu (z nawigacją `..` i kopiowaniem wybranych plików `F5` ze środka), zamiast od razu okna rozpakowywania.
- [x] **Drag & Drop między panelami i z Findera** — obsługa `NSDraggingDestination` w `MTKView` (`main.mm`), aby przeciągnięte pliki trafiały do aktywnego/wskazanego panelu.
- [x] **Edytowalne Menu Użytkownika (`F2`)** — wczytywanie własnych pozycji użytkownika z `~/.config/mcx/menu.json` (analogicznie do lokalizacji).
- [x] **Szybkie tworzenie pustego pliku (`Shift+F4`)** — klasyczny skrót `mc`/`TC` do utworzenia nowego pliku i otwarcia go od razu w edytorze.

## 3. Lokalizacja i budowanie
- [x] **Dokończyć tłumaczenie treści okien modalnych** — przenieść pozostałe komunikaty z `CommanderEngine.cpp` (toasty, błędy) oraz opisy wewnątrz okien (`MultiRename`, `SearchFiles`, `SyncDirs`, `Help`) do kluczy w `pl.json` / `en.json` / `de.json`.
- [x] **Automatyczny skrypt budujący (`build.sh` / `Makefile`)** — jeden skrypt, który podbija `MCX_BUILD` po kropce, buduje wersję `Release`, aktualizuje `/Applications/mcx.app` i robi commit/push.
- [x] **Wypchnąć zaległy commit `f51c7ea` + `TODO.md` na GitHuba**, gdy serwer GitHub przestanie zwracać błąd 500.
