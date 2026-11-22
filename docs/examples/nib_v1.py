"""Compatibility entry for the NIB wire reference; the public SDK is sdk/nijin_bus.py."""
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "sdk"))
from nib_wire import main  # noqa: E402

if __name__ == "__main__":
    main()
