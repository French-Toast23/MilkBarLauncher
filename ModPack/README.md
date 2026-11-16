# ModPack

Self-contained Breath of the Wild Multiplayer graphic pack used by the launcher installer
(no BCML/UKMM needed).

- `rules.txt`, `patch_*.asm`, `content/` are copied as-is into
  `<Cemu data>/graphicPacks/BreathOfTheWild_Multiplayer/`.
- `merge/*.pack` contain only the files the mod changes. At install time they are layered on top
  of the player's own `Pack/Bootup.pack` / `Pack/TitleBG.pack` from the v208 update, so no full
  Nintendo archives are shipped.
