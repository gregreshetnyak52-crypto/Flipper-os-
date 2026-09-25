# Патчи к Unleashed

Сюда кладутся изменения исходников Unleashed в формате `git diff`.
`firmware/build.sh` применяет все `*.patch` по алфавиту поверх
закреплённого коммита, поэтому называйте их `0001-…`, `0002-…`.

Как сделать патч:

```sh
cd firmware/unleashed
# ...правите файлы...
git diff > ../patches/0001-short-description.patch
git reset --hard
```

Новые файлы (иконки, свои приложения, настройки) лучше класть не в патч,
а в `firmware/overlay/` — эта папка копируется поверх дерева Unleashed
как есть.
