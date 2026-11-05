# damietta: Damietta, Egypt

The prayer times under this key (`docs/athantimes/damietta/<year>.json`) are for **Damietta (دمياط), Egypt**
(Egyptian time, `EET-2EEST,M4.5.5/0,M10.5.4/24`). Clocks show it as **"Damietta, Egypt"**; owners never see the
key.

The V2 repository ([et7ad/esp_athan](https://github.com/et7ad/esp_athan)) keeps the same city under `masjid7`,
one of the placeholder slots of the list V2 clocks have built in. `"v2_key": "masjid7"` in `mosques.json` tells
`scripts/make_yearly_json.py` to read the daily files there.

## Where the data comes from

The same source as Cairo: the Aladhan API's method 5, "Egyptian General Authority of Survey", the calculation
behind Dar al-Ifta's published times (<https://www.dar-alifta.org/ar/prayer>), at Damietta's coordinates
(31.416569, 31.813316):

```bash
# in ../esp_athan: fetch the daily files
python3 scripts/aladhan/fetch_aladhan_daily.py --key masjid7 --year 2026 --year 2027 --year 2028
# in this repository: one yearly file per year, converted from the daily files
python3 scripts/make_yearly_json.py --key damietta --year 2026 --year 2027 --year 2028
```

The check against dar-alifta.org (every time within 1 minute), the Doha and iqama offsets (estimates, not a
mosque's schedule) and what to do if Egypt changes its daylight saving are in [cairo/README.md](../cairo/README.md)
and apply here unchanged.
