"""AgInTi C12880MA host SDK."""

from .client import SpectrometerClient
from .protocol import SpectrumFrame

__all__ = ["SpectrometerClient", "SpectrumFrame"]
__version__ = "0.2.0"

