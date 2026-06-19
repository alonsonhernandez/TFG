#!/usr/bin/env python3
"""
generar_graficas_avanzado.py
──────────────────────────────────────────────────────────────────────────────
Genera todas las gráficas de las simulaciones sintéticas para la v2.55 (Avanzado)
con el algoritmo LIMERIC corregido (dead-band + recuperación).

Salidas:
  tests_simulaciones_avanzado/15_coches/carga_baja/*.png
  tests_simulaciones_avanzado/15_coches/carga_media/*.png
  tests_simulaciones_avanzado/15_coches/caudal_alto/*.png
"""

import numpy as np
import matplotlib
matplotlib.use('Agg')
import matplotlib.pyplot as plt
import os

# ── Parámetros LIMERIC ────────────────────────────────────────────────────────
ALPHA            = 0.016
BETA             = 0.0012
DELTA_MAX        = 0.030
DELTA_MIN        = 0.0006
DELTA_OFFSET_MAX =  0.0005
DELTA_OFFSET_MIN = -0.00025
CBR_TARGET       = 0.68
MCO_INTERVAL_S   = 0.2
CBR_THRESHOLD    = 0.68
CBR_RECOVERY     = 0.58

def limeric_step(adapt_delta, active_cbr, traffic_diverted=False):
    if active_cbr < CBR_TARGET:
        return min(adapt_delta + DELTA_OFFSET_MAX, DELTA_MAX)
    else:
        delta_offset = BETA * (CBR_TARGET - active_cbr)
        delta_offset = max(DELTA_OFFSET_MIN, min(DELTA_OFFSET_MAX, delta_offset))
        if traffic_diverted:
            delta_offset *= 0.5
        new_delta = (1 - ALPHA) * adapt_delta + delta_offset
        return max(DELTA_MIN, min(DELTA_MAX, new_delta))


def simulate_scenario(mode, n_nodes, safety_offered, app_offered, t_max=100.0, noise_sigma=0.010, n_seeds=5):
    steps = int(t_max / MCO_INTERVAL_S) + 1
    time_arr = np.arange(steps) * MCO_INTERVAL_S

    delta_acum = np.zeros(steps)
    cbr_cch_acum = np.zeros(steps)
    cbr_sch_acum = np.zeros(steps)

    for seed in range(n_seeds):
        np.random.seed(seed * 7)
        delta_nodes = np.full((n_nodes, steps), DELTA_MAX)
        cbr_cch_nodes = np.zeros((n_nodes, steps))
        cbr_sch_nodes = np.zeros((n_nodes, steps))

        for ni in range(n_nodes):
            n_safety = max(0.001, safety_offered + np.random.normal(0, 0.008))
            n_app    = max(0.001, app_offered + np.random.normal(0, 0.008))

            for i in range(1, steps):
                t = time_arr[i]
                throttle = delta_nodes[ni, i-1] / DELTA_MAX

                # Distribución de tráfico según el modo
                traffic_diverted = False
                n_total = n_safety + n_app
                
                if mode == "cch_only":
                    cch_off = n_total
                    sch_off = 0.0
                elif mode == "sch_only":
                    cch_off = 0.0
                    sch_off = n_total
                elif mode == "mco_static":
                    cch_off = n_safety
                    sch_off = n_app
                elif mode == "mco_dynamic":
                    # Hasta que se llene (0.68), todo va por CCH. El resto se desborda a SCH.
                    if n_total * throttle > CBR_THRESHOLD:
                        # Calculamos la carga que CCH puede asumir hasta llegar a 0.68 real
                        # (si throttle < 1, la carga ofrecida debe ser mayor para llegar a 0.68 real)
                        cch_max_offered = CBR_THRESHOLD / throttle if throttle > 0 else 0
                        cch_off = min(n_total, cch_max_offered)
                        sch_off = max(0.0, n_total - cch_off)
                        traffic_diverted = True
                    else:
                        cch_off = n_total
                        sch_off = 0.0

                # Aplicar acelerador (throttle) del LIMERIC y añadir ruido
                cbr_cch_val = np.clip(cch_off * throttle + np.random.normal(0, noise_sigma), 0.0, 1.0) if cch_off > 0.001 else 0.0
                cbr_sch_val = np.clip(sch_off * throttle + np.random.normal(0, noise_sigma), 0.0, 1.0) if sch_off > 0.001 else 0.0

                cbr_cch_nodes[ni, i] = cbr_cch_val
                cbr_sch_nodes[ni, i] = cbr_sch_val

                # Selección del CBR activo para el algoritmo
                if mode == "cch_only":
                    active_cbr = cbr_cch_val
                elif mode == "sch_only":
                    active_cbr = cbr_sch_val
                else: # mco_static, mco_dynamic
                    active_cbr = max(cbr_cch_val, cbr_sch_val)

                delta_nodes[ni, i] = limeric_step(delta_nodes[ni, i-1], active_cbr, traffic_diverted)

        delta_acum += delta_nodes.mean(axis=0)
        cbr_cch_acum += cbr_cch_nodes.mean(axis=0)
        cbr_sch_acum += cbr_sch_nodes.mean(axis=0)

    return (time_arr, delta_acum / n_seeds, cbr_cch_acum / n_seeds, cbr_sch_acum / n_seeds)


def plot_graph(time_arr, delta, cbr_cch, cbr_sch, title, out_path, mode):
    fig, ax1 = plt.subplots(figsize=(13, 6))

    ax1.set_xlabel('Tiempo de simulación (s)', fontsize=11)
    ax1.set_ylabel('CBR Medio (Congestión Red Global)', fontsize=11)
    
    ax1.plot(time_arr, cbr_cch, label='CBR Medio CCH', color='royalblue', linewidth=2, alpha=0.85)
    ax1.plot(time_arr, cbr_sch, label='CBR Medio SCH', color='seagreen',  linewidth=2, alpha=0.85)
    
    ax1.axhline(y=CBR_THRESHOLD, color='crimson', linestyle='--', linewidth=1.5, label=f'Umbral Activación ({CBR_THRESHOLD})')
    ax1.axhline(y=CBR_RECOVERY,  color='tomato',  linestyle=':', linewidth=1.5, label=f'Umbral Recuperación ({CBR_RECOVERY})')
    ax1.set_ylim(0, 1.05)
    ax1.set_xlim(0, time_arr[-1])

    ax2 = ax1.twinx()
    ax2.set_ylabel('Parámetro Delta Medio (todos los nodos)', color='darkorange', fontsize=11)
    d_min, d_max = delta.min(), delta.max()
    margin = max((d_max - d_min) * 0.20, 0.0005)
    ax2.set_ylim(max(0.0, d_min - margin), DELTA_MAX + margin * 2)
    ax2.plot(time_arr, delta, label=f'Delta Medio ({mode})', color='darkorange', linewidth=2.5, alpha=0.9)
    ax2.tick_params(axis='y', labelcolor='darkorange')

    # Cartelito de modo
    ax1.text(0.01, 0.98, f'Modo activo: {mode}', transform=ax1.transAxes, fontsize=8.5, verticalalignment='top',
             bbox=dict(boxstyle='round,pad=0.3', facecolor='lightcyan', edgecolor='steelblue', alpha=0.75))

    lines1, labels1 = ax1.get_legend_handles_labels()
    lines2, labels2 = ax2.get_legend_handles_labels()
    ax1.legend(lines1 + lines2, labels1 + labels2, loc='upper right', frameon=True, shadow=True, fontsize=9)

    plt.title(title, fontsize=13, fontweight='bold')
    plt.tight_layout()
    plt.savefig(out_path, dpi=300)
    plt.close()
    print(f"  Guardado: {out_path}")


def main():
    BASE_OUT = "/home/usuario/mcoVanetza/tests_dinamic/15_coches"
    
    # Cargas ofrecidas basadas en simulaciones previas
    # scale2 (Baja), scale5 (Media), scale8 (Alta)
    cargas = [
        {"nombre": "baja",  "scale": 2, "safety": 0.055, "app": 0.215, "carpeta": f"{BASE_OUT}/carga_baja"},
        {"nombre": "media", "scale": 5, "safety": 0.140, "app": 0.545, "carpeta": f"{BASE_OUT}/carga_media"},
        {"nombre": "alta",  "scale": 8, "safety": 0.180, "app": 0.700, "carpeta": f"{BASE_OUT}/caudal_alto"},
    ]
    
    modos = ["cch_only", "sch_only", "mco_static", "mco_dynamic"]

    print("========================================================================")
    print("  GENERANDO GRÁFICAS AVANZADO (12 casos: 3 cargas x 4 modos)")
    print("========================================================================")

    for c in cargas:
        os.makedirs(c['carpeta'], exist_ok=True)
        print(f"\n--- Carga {c['nombre'].upper()} (Scale={c['scale']}) ---")
        
        for m in modos:
            print(f" Simulating {m}...")
            t, d, cch, sch = simulate_scenario(m, 15, c['safety'], c['app'], t_max=100.0)
            
            # Nombres sin prefijos numéricos
            fname = f"{m}_scale{c['scale']}_15cars_{c['nombre']}_6Mbps.png"
            out_path = os.path.join(c['carpeta'], fname)
            
            title = f"Congestión Red Vehicular - {m.upper()} (Scale={c['scale']}, 15 coches, 6 Mbps)"
            plot_graph(t, d, cch, sch, title, out_path, m)

if __name__ == "__main__":
    main()
