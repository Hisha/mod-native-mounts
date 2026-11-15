# mod-native-mounts

Account-wide native mount architecture and isolated proof of concept for
AzerothCore WotLK 3.3.5a (build 12340).

The PoC records account ownership, grants only character-eligible native mount
spells, tracks module provenance before any removal, and contributes an
independent Mounts page through `mod-content-manager`. It is disabled by default
and is not a complete Mount Journal.

Start with [the account-wide architecture](docs/ACCOUNT_WIDE_ARCHITECTURE.md) and
[the source investigation](docs/TECHNICAL_INVESTIGATION.md). The implemented
event mapping, query-count comparison, and PTR checklist are in
[the event-driven refactor report](docs/EVENT_DRIVEN_REFACTOR.md).

Run automated checks:

```bash
python3 tests/run_checks.py
```
