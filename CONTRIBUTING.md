# Branches

| | |
|---|---|
| **`uat`** | default. Work lands here first and gets used on a real device before it moves on. |
| **`prod`** | known-good. Only ever updated from `uat`, by pull request. |

Both are protected: a pull request with one approval, no force-pushes, no
deletions, and review approvals are dismissed when new commits arrive.

## Working on it

```bash
git switch uat
git switch -c my-change
# ...
git push -u origin my-change
gh pr create --base uat
```

## Promoting to prod

```bash
gh pr create --base prod --head uat --title "Promote uat to prod"
```

Do this only after the change has actually run on hardware. Firmware is not
like a web app: a bad `prod` is something you flash onto a device that then
has to be recovered, sometimes with the G0 button.

Before promoting, at minimum:

```bash
pio run -e cardputer-selftest -t upload && pio device monitor   # 201 checks
```

## A note on the rules

Protection deliberately does **not** apply to admins. With only one person on
the repository, GitHub will not let you approve your own pull request, so
enforcing the rules on admins would lock the owner out of merging entirely.
The rules stop everyone else; the owner keeps a way through.
