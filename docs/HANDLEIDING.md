# Handleiding Hacker Touchbar

> [!WARNING]
> Deze code en documentatie zijn geschreven door AI en maar kort getest op één
> machine. Gebruik is op eigen risico, zeker omdat het programma als root draait.
> Dit is een persoonlijk project: verwacht niet veel opvolging van issues of pull
> requests.

Hacker Touchbar verandert de Touch Bar van een Intel MacBook met T2-chip onder
Linux in een kleine retroterminal: amber of groen, met rustige animaties en
gewone knoppen onder de Fn-toets.

Getest op een MacBookPro16,2 (13 inch, 2020) met Ubuntu 26.04 van
[t2linux](https://wiki.t2linux.org), GNOME op Wayland en kernel 7.3.1-2-t2.
Andere modellen kunnen werken, maar zijn niet getest.

## Installeren

### 1. Zorg dat de Touch Bar werkt

Installeer tiny-dfr en start de laptop opnieuw op:

```bash
sudo apt install tiny-dfr
```

Na de herstart moet de balk knoppen tonen. Gebeurt dat niet, los dat dan eerst
op met de [t2linux-wiki](https://wiki.t2linux.org/guides/postinstall/).

### 2. Installeer wat nodig is om te bouwen

```bash
sudo apt install git cmake pkg-config libdrm-dev libpulse-dev libdbus-1-dev libfftw3-dev libevdev-dev zlib1g-dev
```

### 3. Haal de code op en installeer

```bash
git clone https://github.com/sebastius/hacker-touchbar
```

```bash
cd hacker-touchbar && ./install.sh
```

Zet er geen `sudo` voor: het script vraagt daar zelf om. Het bouwt het programma,
installeert de service en maakt een configuratiebestand aan als dat er nog niet
is. Daarna verschijnt de animatie op de balk.

## Bediening

| Wat je doet | Wat er gebeurt |
|---|---|
| Tik op de play/pauze-knop | Muziek of video start of pauzeert |
| Sleep over de volumebalk | Volume verandert |
| Tik op de luidspreker | Geluid uit of aan |
| Druk lang op de animatie | Volgende animatie |
| Tik op de animatie | Extra druppels, cellen of een vallende ster |
| Houd Fn ingedrukt | F1 t/m F12 |
| Tik kort op Fn | Je eigen knoppenrij aan of uit |

In de knoppenrij zitten ook twee speciale knoppen:

- **`AMBER` of `GREEN`** wisselt de kleur.
- **`BAR:2` of `BAR:1`** wisselt tussen een volle en een gedimde balk.

Beide keuzes blijven bewaard. De balk uitzetten kan niet met de knop, omdat je
hem dan niet meer ziet.

## Animaties

- **matrix:** langzaam vallende code.
- **life:** Conway's Game of Life, live berekend. Op de beat, bij een tik en af
  en toe willekeurig komen er cellen bij, zodat het patroon niet stilvalt.
- **stars:** een trage sterrenhemel met af en toe een vallende ster.

Alle animaties lopen ook zonder muziek door. Muziek geeft alleen kleine accenten.

## Instellingen aanpassen

Alle instellingen staan in `~/.config/touchbar-fx/config.toml`. Je hebt geen
`sudo` nodig om dat bestand te bewerken. Herstart daarna het programma:

```bash
sudo systemctl restart touchbar-fx
```

### Knoppen

Hoe je knoppen aanpast, staat in [Zelf knoppen maken](#zelf-knoppen-maken).

### Kleur en animaties

```toml
[ui]
theme = "amber"

[visualizer]
preset = "matrix"
cycle = ["matrix", "life", "stars"]
```

- **`theme`:** `amber` of `green`. Een keuze via de knop op de balk gaat hier
  boven.
- **`preset`:** de animatie waarmee het programma start. Wisselen met lang drukken
  wordt niet bewaard.
- **`cycle`:** de animaties waar je met lang drukken langs gaat.

## Zelf knoppen maken

De rij onder een korte tik op Fn staat in `~/.config/touchbar-fx/config.toml`.
Elke knop is een blok `[[controls]]`, in dezelfde volgorde als op de balk. Zodra
er een blok in het bestand staat, vervangt het bestand de hele standaardrij.

```toml
[[controls]]
label = "LOCK"
keys = ["LEFTMETA", "L"]
width = 2
```

| Instelling | Betekenis |
|---|---|
| `label` | Tekst op de knop |
| `keys` | Toets of toetscombinatie, zonder `KEY_` ervoor |
| `width` | Relatieve breedte, standaard 2 |
| `show` | Speciale knop: `bar`, `theme`, `clock` of `battery` |

Een knop met `show` heeft geen `label` of `keys` nodig. Herstart na elke wijziging:

```bash
sudo systemctl restart touchbar-fx
```

### Breedte en tekst

De breedtes zijn verhoudingen: de balk wordt verdeeld over alle knoppen samen. Met
meer knoppen wordt dus elke knop smaller. Elke letter is 24 pixels breed en de
balk is 2008 pixels, dus reken zo: bij een totale breedte van 28 is `width = 2`
ongeveer 143 pixels, genoeg voor vijf letters.

### Veelgebruikte toetsnamen

Alle namen staan in `/usr/include/linux/input-event-codes.h`. Hoofdletters zijn
niet nodig.

| Soort | Namen |
|---|---|
| Media | `PLAYPAUSE`, `NEXTSONG`, `PREVIOUSSONG`, `STOPCD` |
| Geluid | `MUTE`, `VOLUMEUP`, `VOLUMEDOWN`, `MICMUTE` |
| Licht | `BRIGHTNESSUP`, `BRIGHTNESSDOWN`, `KBDILLUMUP`, `KBDILLUMDOWN` |
| Modificatie | `LEFTCTRL`, `LEFTALT`, `LEFTSHIFT`, `LEFTMETA` (de Super-toets) |
| Navigatie | `ESC`, `TAB`, `ENTER`, `SPACE`, `BACKSPACE`, `DELETE`, `HOME`, `END`, `PAGEUP`, `PAGEDOWN`, `UP`, `DOWN`, `LEFT`, `RIGHT` |
| Letters en cijfers | `A` t/m `Z`, `0` t/m `9` |

### Voorbeelden

Scherm vergrendelen. `Super+L` is de standaardsneltoets in GNOME:

```toml
[[controls]]
label = "LOCK"
keys = ["LEFTMETA", "L"]
```

Schermafbeelding maken met de Print-toets, die in Linux `SYSRQ` heet:

```toml
[[controls]]
label = "SHOT"
keys = ["SYSRQ"]
```

Kopiëren en plakken:

```toml
[[controls]]
label = "COPY"
keys = ["LEFTCTRL", "C"]

[[controls]]
label = "PASTE"
keys = ["LEFTCTRL", "V"]
width = 3
```

Microfoon dempen:

```toml
[[controls]]
label = "MIC"
keys = ["MICMUTE"]
```

### Een programma starten

Een knop kan alleen toetsen versturen, dus je gebruikt een GNOME-sneltoets als
tussenstap. Kies een combinatie die nergens anders voor wordt gebruikt.

1. Open **Instellingen**, dan **Toetsenbord**.
2. Kies **Sneltoetsen bekijken en aanpassen**, dan **Aangepaste sneltoetsen**.
3. Voeg een sneltoets toe met het programma als opdracht, bijvoorbeeld
   `firefox`, en stel `Ctrl+Alt+Shift+F` in.
4. Zet dezelfde combinatie in je configuratie:

```toml
[[controls]]
label = "WEB"
keys = ["LEFTCTRL", "LEFTALT", "LEFTSHIFT", "F"]
```

### Werkt een knop niet?

Een knop met een verkeerde toetsnaam wordt overgeslagen, de rest blijft werken.
Zoek de reden in de log:

```bash
journalctl -u touchbar-fx -b --no-pager | grep controls
```

Staat er niets in de log, dan klopt de knop wel maar reageert GNOME of het
programma niet op die toets. Probeer de combinatie dan eerst op je gewone
toetsenbord.

De rij met F1 t/m F12 onder Fn vasthouden staat vast in de code, in
`src/mode/keys_mode.cpp`.

## Zelf een animatie toevoegen

Een animatie heet in de code een preset. Je hebt er één C++-bestand voor nodig in
`src/viz/presets/`; het bouwsysteem vindt het vanzelf. Je werkt daarvoor in de map
met de code.

### Stap 1: maak het bestand

Maak `src/viz/presets/golf.cpp`. Dit voorbeeld tekent een golvende lijn die op de
beat groter wordt, en een tik wisselt de snelheid:

```cpp
#include "viz/preset.hpp"
#include "viz/preset_registry.hpp"
#include "viz/viz_renderer.hpp"
#include "gfx/framebuffer.hpp"
#include "gfx/color.hpp"
#include <cmath>

// "golf": een golvende lijn die op de beat groter wordt.
class GolfPreset : public VizPreset {
public:
    const char* name() const override { return "golf"; }
    bool clearsFrame() const override { return true; }

    void onTap(int, int) override { speed_ = speed_ > 0.05f ? 0.03f : 0.08f; }

    void draw(const VizRenderer& viz, Framebuffer& fb) override {
        fb.clear(0xFF000000);
        tijd_ += speed_;

        const int midden = fb.height() / 2;
        const float hoogte = 6.0f + 18.0f * viz.pulse();
        for (int x = 0; x < fb.width(); x += 3) {
            int y = midden + static_cast<int>(std::sin(x * 0.02f + tijd_) * hoogte);
            fb.fillRect(x, y, 2, 2, rgb(255, 255, 255));
        }
    }

private:
    float tijd_ = 0.0f;
    float speed_ = 0.03f;
};

REGISTER_PRESET("golf", GolfPreset);
```

### Stap 2: bekijk het zonder de balk

Bouw het programma:

```bash
cmake -B build && cmake --build build -j"$(nproc)"
```

Maak een testconfiguratie, zodat je eigen instellingen ongemoeid blijven:

```bash
printf '[ui]\ntheme = "amber"\n[modes]\norder = ["deck"]\n[visualizer]\npreset = "golf"\nrotate_seconds = 0\n' > /tmp/test.toml
```

Render daarna twee seconden aan beelden:

```bash
./build/touchbar-fx --config /tmp/test.toml --dump-mode deck --dump-frames 120 --null-audio --out /tmp/frames --width 2008 --height 60
```

De beelden staan nu als `.bmp`-bestanden in `/tmp/frames`. Zo zie je fouten voordat
je iets installeert. `--null-audio` gebruikt een testsignaal in plaats van je
echte geluid.

### Stap 3: installeer en gebruik het

Installeer de nieuwe versie:

```bash
./install.sh
```

Zet de naam daarna in `cycle`, dan kun je er met lang drukken naartoe:

```toml
[visualizer]
cycle = ["matrix", "life", "stars", "golf"]
```

Na het herstarten van de service kun je er ook meteen naartoe springen:

```bash
touchbar-fxctl preset golf
```

### Wat je kunt gebruiken

`draw()` wordt ongeveer 60 keer per seconde aangeroepen. Het tekengebied is 60
pixels hoog en op de 13-inch balk ongeveer 1600 pixels breed. Gebruik altijd
`fb.width()` en `fb.height()` in plaats van vaste getallen.

| Tekenen in `fb` | Doet |
|---|---|
| `clear(kleur)` | Maakt alles één kleur |
| `setPixel(x, y, kleur)` | Tekent één pixel |
| `fillRect(x, y, b, h, kleur)` | Tekent een gevuld vlak |
| `drawHLine` / `drawVLine` / `drawLine` | Tekent lijnen |
| `fillCircle(x, y, r, kleur)` | Tekent een gevulde cirkel |
| `drawText(x, y, tekst, kleur, FontSize::Small)` | Tekent tekst, alleen ASCII |

| Muziek uit `viz` | Betekenis |
|---|---|
| `pulse()` | 1,0 op de beat, zakt daarna langzaam terug |
| `bars()` | Spectrum: een lijst waarden van 0 tot 1, lage tonen eerst |
| `silent()` | `true` als er niets speelt |

Handige overrides op de preset:

| Functie | Doel |
|---|---|
| `clearsFrame()` | `true` als je elk beeld zelf wist; anders blijft een spoor achter |
| `onTap(x, y)` | Reageren op een tik |

### Tips

- **Kleur:** in amber en groen telt alleen hoe helder een pixel is, want de balk
  kleurt alles in één tint. Werk dus met helderheid, bijvoorbeeld
  `rgb(255, 255, 255)` voor fel en `rgb(80, 80, 80)` voor gedimd.
- **Rustig houden:** beweeg een paar pixels per seconde, niet per beeld, en laat
  muziek alleen kleine accenten geven.
- **Geen bibliotheken:** matrix, life en stars in `src/viz/presets/` zijn korte
  voorbeelden om van te leren.
- **Rechten:** gebruik geen beelden of figuren van anderen zonder toestemming als
  je je versie publiceert.
- **Tests:** `./build/touchbar-fx-test` controleert de volgorde van alle presets.
  Met een nieuwe preset falen daar controles op `presetName()`. Dat is alleen die
  lijst, en niet de animatie zelf. Voeg de naam op alfabetische volgorde toe in
  `src/tests/test_main.cpp` om dat op te lossen.

## Starten en stoppen

Het programma start vanzelf als je inlogt. In het inlogscherm en na het uitloggen
laat tiny-dfr gewone knoppen zien.

Tijdelijk stoppen:

```bash
sudo systemctl stop touchbar-fx
```

Weer starten:

```bash
sudo systemctl start touchbar-fx
```

Automatisch starten uitzetten:

```bash
sudo systemctl disable --now touchbar-fx
```

## Problemen oplossen

Bekijk eerst de status en de log:

```bash
systemctl status touchbar-fx
```

```bash
journalctl -u touchbar-fx -b --no-pager -n 40
```

| Probleem | Wat je kunt doen |
|---|---|
| `No Touch Bar DRM device found` | Controleer of tiny-dfr los werkt, en start de laptop opnieuw op |
| Balk blijft zwart na de slaapstand | `sudo systemctl restart touchbar-fx` |
| Fn doet niets | Zoek in de log naar `Fn key: watching internal keyboard` |
| Volume of play/pauze reageert niet | Controleer of je bent ingelogd; audio en mediaspeler horen bij je sessie |
| Een knop ontbreekt | Zoek in de log naar `[[controls]]`, waarschijnlijk klopt een toetsnaam niet |

## Verwijderen

Ga naar de map met de code en voer dit uit:

```bash
./install.sh uninstall
```

tiny-dfr krijgt de balk dan terug. Je instellingen in `~/.config/touchbar-fx`
blijven staan.

## Goed om te weten

- Het programma draait als root, omdat dat nodig is om de balk aan te sturen. Het
  leest ook je toetsenbord om Fn te herkennen. Andere toetsen worden niet bewaard.
- Het originele project heeft geen licentie. Zie de [README](../README.md#license)
  voor wat dat betekent.
