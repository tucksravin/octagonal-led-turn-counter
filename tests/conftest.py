"""Put scripts/ on sys.path so the bench scripts can be imported by name."""
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent.parent / "scripts"))
