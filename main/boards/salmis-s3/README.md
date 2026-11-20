# Salmis S3

Variante de `spotpear/sp-esp32-s3-1.28-box` (robot XiaoZhi ESP32-S3 N16R8 con LCD redonda de 1.28", touch y batería) con montaje de la microSD. La board original no se modifica.

## microSD (SDSPI, bus SPI2)

| Señal | GPIO |
|---|---|
| CS (D3) | 13 |
| MOSI (CMD) | 18 |
| SCK (CLK) | 17 |
| MISO (D0) | 21 |

Punto de montaje: `/sdcard`. Si no hay tarjeta, el arranque continúa y se registra el aviso.

## Compilar

```bash
python3 scripts/build.py salmis-s3 --name salmis-s3
```
