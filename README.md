# mcx

**mcx** (`v1.0.0`) to szybki, dwupanelowy menedżer plików dla macOS napisany w **C++20** z akcelerowanym przez **Metal** interfejsem **Dear ImGui**, łączący klasyczny układ **Total Commandera** i **Midnight Commandera** z paletą **Monokai**.

## Główne funkcje

- **Dwa panele plików z kartami** (`⌘T`, `⌘W`, `Ctrl+Tab`), przeciąganym podziałem i zapamiętywaniem stanu (otwarte karty, sortowanie, aktywny panel, rozmiar i pozycja okna).
- **Własny silnik w C++20**: szybkie listowanie katalogów (`opendir`/`lstat`), liczenie rozmiarów katalogów przez `fts_open`/`fts_read`, kopiowanie/przenoszenie z obsługą konfliktów, wykonywanie poleceń `/bin/zsh`.
- **Archiwa 7-Zip / ZIP / TAR.GZ**: pakowanie (`⌥F5`) z wyborem poziomu kompresji LZMA2 i opcjonalnym szyfrowaniem **AES-256** (w tym szyfrowaniem nazw plików `-mhe=on` dla `.7z`), rozpakowywanie (`⌥F9` lub `Enter`) oraz podgląd zawartości bez rozpakowania (`F3`, `Ctrl+Q`).
- **Narzędzia Total Commandera i Midnight Commandera**:
  - **Lister (`F3`)** – podgląd tekstu i zrzut HEX.
  - **Edytor (`F4`)** – wbudowany edytor plików tekstowych.
  - **Multi-Rename (`⌘M`)** – masowa zmiana nazw z maskami `[N]`, `[E]`, `[C]`, `[YMD]`, `[hms]` i wyrażeniami regularnymi.
  - **Wyszukiwarka (`⌥F7`)** – rekurencyjne szukanie po masce nazwy i treści plików.
  - **Synchronizacja katalogów** i **porównywanie plików (`diff -u`)**.
  - **Dowiązania POSIX** (Symlink, względny Symlink, Hardlink), **zmiana uprawnień (`chmod`)** i **sumy kontrolne (`SHA-256` / `MD5`)**.
  - **Szybki podgląd (`Ctrl+Q`)**, **Panel Info MC (`Ctrl+I`)** oraz **pełnoekranowa powłoka Subshell (`Ctrl+O`)**.

---

## Krótki manual (skróty i obsługa)

### Klawisze funkcyjne (`F1`–`F10`)

| Klawisz | Akcja |
| :--- | :--- |
| `F1` | Pomoc i lista skrótów |
| `F2` | Menu użytkownika Midnight Commander (`Shift+F2` – zmiana nazwy) |
| `F3` | Podgląd pliku (Lister Tekst/HEX) / zawartości archiwum / rozmiaru katalogu |
| `F4` | Wbudowany edytor pliku tekstowego |
| `F5` / `⌥F5` | Kopiuj do drugiego panelu / Pakuj do `.7z`, `.zip`, `.tar.gz` |
| `F6` / `Shift+F6` | Przenieś do drugiego panelu / Zmiana nazwy |
| `F7` / `⌥F7` | Nowy katalog / Wyszukiwarka plików i treści |
| `F8` / `Delete` | Usuń (do Kosza lub trwale) |
| `F9` / `⌥F9` | Górne menu / Rozpakuj archiwum |
| `F10` | Zakończ program |

### Zaznaczanie i nawigacja

| Akcja | Skrót / Mysz |
| :--- | :--- |
| Przełącz panel | `Tab` |
| Zaznacz pojedynczo (wielowybór) | `Alt (Option) + LPM` lub `⌘ + LPM` |
| Zaznacz zakres (pierwszy → ostatni) | `Shift + LPM` |
| Odznacz wszystko | Kliknięcie `LPM` poza zaznaczeniem (lub `⇧⌘A`) |
| Zaznacz pod kursorem | `Insert` (z przejściem niżej) lub `Spacja` (liczy rozmiar katalogu) |
| Zaznacz / odznacz maską / odwróć | `+` / `-` / `*` |
| Katalog nadrzędny / główny | `Backspace` lub `⌘↑` / `\` |
| Ulubione katalogi (Hotlist) / Historia | `⌘D` (`Ctrl+D`) / `⌥↓` |
| Otwórz katalog w drugim panelu | `⌥O` |
| Zamień panele / Wyrównaj | `⌘U` (`Ctrl+U`) / `⌥⌘=` |
| Ukryte pliki (dotfiles) | `⌥.` lub `⇧⌘.` |
| Tryb Krótki / Pełny | `⌘1` / `⌘2` (lub `⌥T`) |
| Powłoka Subshell / Quick View / Panel Info | `Ctrl+O` / `Ctrl+Q` / `Ctrl+I` |
| Wstaw nazwę / ścieżkę do wiersza poleceń | `⌘Enter` (`⌥Enter`) / `⇧⌘Enter` |

---

## Wymagania i kompilacja

- **macOS** (Apple Silicon / Intel) + **Xcode** (`clang++` z obsługą C++20 i Metal).
- Opcjonalnie dla archiwów `.7z`: `brew install sevenzip` (dostarcza `/opt/homebrew/bin/7zz`).

Budowanie i uruchomienie z terminala:

```zsh
xcodebuild -project MCommander.xcodeproj -scheme MCommander -configuration Release build
```

## Licencja

Projekt udostępniony na licencji **GNU General Public License v3.0 (GPL-3.0)** — szczegóły w pliku [LICENSE](LICENSE).
