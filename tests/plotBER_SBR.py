import re
import matplotlib.pyplot as plt
import numpy as np

def parse_ber_snr(filename):
    snr_list = []
    ber_list = []
    with open(filename, 'r') as f:
        for line in f:
            match = re.search(r'snr:\s*([-+]?\d*\.?\d+),\s*BER:\s*([-+]?\d*\.?\d+)', line)
            if match:
                snr = float(match.group(1))
                ber = float(match.group(2))
                if ber > 0:
                    snr_list.append(snr)
                    ber_list.append(ber)
    return snr_list, ber_list

files = ['testHamming.csv', 'testPolar.csv']
fig, axes = plt.subplots(1, 2, figsize=(14, 5))

for ax, filename in zip(axes, files):
    snr, ber = parse_ber_snr(filename)
    ax.semilogy(snr, ber, 'o-', markersize=4, linewidth=1, label=filename)
    
    # Частая сетка по X
    ax.set_xticks(np.arange(-40, 41, 5))          # основные деления через 5 дБ
    ax.set_xticks(np.arange(-40, 41, 1), minor=True)  # вспомогательные через 1 дБ
    ax.grid(True, which='major', linestyle='-', linewidth=0.8, alpha=0.7)
    ax.grid(True, which='minor', linestyle=':', linewidth=0.5, alpha=0.5)
    
    ax.set_xlabel('SNR (dB)')
    ax.set_ylabel('BER')
    ax.set_title(filename)
    ax.legend()

plt.tight_layout()
plt.show()