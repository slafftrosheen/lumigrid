#!/usr/bin/env python3
"""Setup script for Lumigrid Display Node."""

from setuptools import setup, find_packages

with open("README.md", "r", encoding="utf-8") as fh:
    long_description = fh.read()

setup(
    name="lumigrid-display-node",
    version="0.1.0",
    author="AlgorhythmicDev",
    description="Raspberry Pi LED Matrix Information Display with Web Interface",
    long_description=long_description,
    long_description_content_type="text/markdown",
    url="https://github.com/algorhythmicdev/lumigrid_display_node",
    packages=find_packages(),
    classifiers=[
        "Programming Language :: Python :: 3",
        "License :: OSI Approved :: MIT License",
        "Operating System :: POSIX :: Linux",
        "Development Status :: 3 - Alpha",
        "Intended Audience :: Developers",
        "Topic :: System :: Hardware",
    ],
    python_requires=">=3.7",
    install_requires=[
        "Flask>=3.0.0",
        "Flask-CORS>=4.0.0",
        "PyYAML>=6.0.1",
        "Pillow>=10.0.0",
        "adafruit-blinka>=8.0.0",
        "w1thermsensor>=2.3.0",
        "adafruit-circuitpython-dht>=4.0.0",
        "adafruit-circuitpython-ds3231>=2.4.15",
        "pytz>=2023.3",
        "python-dateutil>=2.8.2",
    ],
    entry_points={
        "console_scripts": [
            "lumigrid=lumigrid.main:main",
        ],
    },
)
