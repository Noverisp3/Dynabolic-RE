"""Module entrypoint so `python -m dynabolic_llm` works."""

import sys

from .cli import main

if __name__ == "__main__":
    sys.exit(main())
