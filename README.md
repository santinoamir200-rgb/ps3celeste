# Celeste Classic para PS3 (PSL1GHT)

Port del Celeste Classic de PICO-8 usando ccleste (lemon32767) como logica.
Frontend propio (sin SDL): renderizado por software 128x128 -> RSX framebuffer.

Controles: X/Cuadrado = saltar, O/Triangulo = dash, D-pad o stick izq = mover,
START = pausa, SELECT+START = reiniciar.

Compilar: `make && make pkg`  (PS3DEV y PSL1GHT definidos). El .pkg lleva el EBOOT.BIN.
Sin audio por ahora. Solo corre en PS3 con CFW/HEN.
Todo el credito del juego es de Maddy Thorson y Noel Berry.
