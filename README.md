# mod-native-mounts

Generated account-wide native mount catalog and Collections interface for
AzerothCore WotLK 3.3.5a (build 12340).

The module records account ownership, grants only character-eligible native
mount spells, tracks module provenance before any removal, and contributes an
independent, scrollable Mounts page through `mod-content-manager`.

Start with [the account-wide architecture](docs/ACCOUNT_WIDE_ARCHITECTURE.md) and
[the source investigation](docs/TECHNICAL_INVESTIGATION.md). The implemented
event mapping, query-count comparison, and PTR checklist are in
[the event-driven refactor report](docs/EVENT_DRIVEN_REFACTOR.md).
Catalog discovery, statistics, artifacts, verification limits, and the Hipally
and Bullsha PTR plan are in
[the complete catalog report](docs/COMPLETE_CATALOG_REPORT.md).

Run automated checks:

```bash
python3 tests/run_checks.py
```
