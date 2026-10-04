# Security policy

## Reporting a vulnerability

Please report security vulnerabilities privately through GitHub:
**[Report a vulnerability](https://github.com/ruvnet/RuView/security/advisories/new)**
(the repository's **Security** tab, then **Report a vulnerability**).

Do not open a public issue, pull request or discussion for a suspected
vulnerability. Public reports expose users before a fix exists.

A useful report includes:

- the affected component (sensing server, ESP32 firmware, npm harness, Python
  package, UI) and its version or commit;
- the steps to reproduce, or a proof of concept;
- the impact you expect (for example remote code execution, authentication
  bypass, or exposure of CSI or other person data).

Leave real credentials, Wi-Fi passwords, OTA keys and captured CSI out of the
report. If one of them is part of the problem, describe it and offer to
provide it privately.

## Related documents

- [`v2/crates/wifi-densepose-sensing-server/SECURITY.md`](v2/crates/wifi-densepose-sensing-server/SECURITY.md):
  the threat model and safe deployment of the sensing server's UDP CSI data
  plane (ADR-296).
- [`docs/security/`](docs/security/): published audits and threat models.
