`python`: require certifi 2026.10.19 or newer

The previous `certifi~=2025.4` pin capped the Python virtualenv at certifi 2025.x, which bundles the emSign root CAs
affected by GHSA-6q48-6c7w-9gw2.
