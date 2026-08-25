# Contributing

One branch: **`main`**. It is protected — changes arrive by pull request with
one approval, and force-pushes and deletion are blocked.

```bash
git switch main && git pull
git switch -c my-change
# ...
git push -u origin my-change
gh pr create --base main
```

## Before merging anything that touches firmware

This is not a web app. A bad `main` is something you flash onto a device that
then has to be physically recovered, sometimes by holding G0 while plugging the
cable in. So run it on hardware first:

```bash
pio run -e cardputer -t upload            # does it still boot
pio device monitor                        # read the boot report
pio run -e cardputer-selftest -t upload   # 201 checks, PASS/FAIL per line
```

## A note on the rules

Protection deliberately does **not** apply to admins. GitHub will not let anyone
approve their own pull request, so with a single collaborator, enforcing the
rules on admins would lock the owner out of merging entirely rather than adding
any safety. The rules stop everyone else; the owner keeps a way through.
