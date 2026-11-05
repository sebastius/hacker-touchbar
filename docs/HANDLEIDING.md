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

De knoppenrij staat in dezelfde volgorde in het bestand als op de balk. Een knop
ziet er zo uit:

```toml
[[controls]]
label = "LOCK"
keys = ["LEFTMETA", "L"]
width = 2
```

- **`label`:** de tekst op de knop. Elke letter is 24 pixels breed, dus houd het
  kort of maak de knop breder.
- **`keys`:** de toets of toetscombinatie, zonder `KEY_` ervoor. Alle namen staan
  in `/usr/include/linux/input-event-codes.h`.
- **`width`:** de relatieve breedte, standaard 2.
- **`show`:** gebruik `bar`, `theme`, `clock` of `battery` in plaats van `label`
  en `keys` voor de speciale knoppen.

Een knop kan alleen toetsen versturen. Wil je een programma starten, koppel het
dan in GNOME aan een eigen sneltoets en laat de knop die sneltoets sturen.

Een typefout laat de rest van de rij intact: alleen die knop wordt overgeslagen,
en de reden staat in de log.

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
