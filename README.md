# Lensless Camera

An ESP32-CAM based lensless imaging system using computational reconstruction techniques grounded in Fourier optics theory.

## Overview

This project explores lensless imaging — capturing diffraction patterns with a bare image sensor and reconstructing the original scene computationally. Built as a portfolio project .

## Project Structure
lensless-camera/

├── docs/theory/          # Fourier optics notes and derivations

├── hardware/esp32_cam/   # Arduino sketches for ESP32-CAM

├── src/

│   ├── capture/          # Image acquisition scripts

│   ├── simulation/       # Diffraction simulation (Fourier optics)

│   └── reconstruction/   # Phase retrieval algorithms

├── data/

│   ├── raw/              # Raw captures from ESP32-CAM

│   └── processed/        # Reconstructed images

├── notebooks/            # Jupyter notebooks for experiments

└── results/              # Output images and figures

## Setup

```bash
git clone <repo-url>
cd lensless-camera
source ~/robotics-env/bin/activate
pip install -r requirements.txt
```

## Dependencies

See `requirements.txt`

## Theory

Based on scalar diffraction theory following Goodman's *Introduction to Fourier Optics*. Key concepts:
- Helmholtz equation and monochromatic wave propagation
- Fresnel and Fraunhofer diffraction
- Phase retrieval and computational reconstruction

## Hardware

- ESP32-CAM (AI Thinker)
- Diffuser / aperture mask (lensless setup)

## Status

- [ ] Diffraction simulation
- [ ] ESP32-CAM capture pipeline
- [ ] Phase retrieval implementation
- [ ] End-to-end reconstruction
