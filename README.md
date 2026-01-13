# Generator Serijskih Številk

Projekt za generiranje UUID v4 serijskih številk z zaporednimi številkami in CRC32 kontrolnimi vsotami. Naloga je razdeljena na tri dele.

## Naloga 1: Samostojni Generator UUID

**Program:** [`uuid.c`](./uuid.c)

Generator poljubnega števila UUID v4 serijskih številk z zaporednimi številkami in CRC32 kontrolnimi vsotami.

**Kompilacija:**
```bash
gcc -o uuid uuid.c
```

## Naloga 2: Strežnik in odjemalec

**Programi:** [`aserver`](./aserver.c), [`aclient`](./aclient.c)

Testiranje komunikacije med strežnikom in odjemalcem ter generiranje UUID v4 serijskih številk z zaporednimi številkami.

**Zagon strežnika:**
```bash
./aserver
```

**Zagon odjemalca:**
```bash
./aclient
```


## Naloga 3: Analiza in poročilo

Upravljanje z več nitmi, [`server`](./server.c), procesira več uporabnikov (niti) enega za drugim tako da zaklepa dostop do programa. Na server pa se povezujemo s [`client`](./client.c).