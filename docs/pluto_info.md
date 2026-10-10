# Pluto SDR — полный дамп `iio_info -u usb:`

**Дата снятия:** 2026-09-27
**Источник:** вывод команды `iio_info -u usb:` (libiio 0.23), выполненной на подключённом устройстве.

> Примечание: в вашей вставке в начале был обрывок предыдущего запуска той же команды (частично прокрученный терминал, показывал только хвост `cf-ad9361-lpc`). Он дублирует данные из полного вывода ниже, поэтому в таблицу включён только полный, финальный запуск.

---

## 1. Идентификация устройства (context attributes)

| Атрибут | Значение |
|---|---|
| hw_model | Analog Devices PlutoSDR Rev.C (Z7010-AD9361) |
| hw_model_variant | 1 |
| hw_serial | 104000409f940014120037006ca8c6bcd2 |
| fw_version | v0.32-1-g7bdc-dirty |
| ad9361-phy,xo_correction | 40000000 (40 МГц опорный генератор) |
| ad9361-phy,model | ad9361 |
| local,kernel | 4.19.0-g8e9fc41dc7db |
| uri | usb:1.2.5 |
| usb,idVendor | 0456 |
| usb,idProduct | b673 |
| usb,release | 2.0 |
| usb,vendor | Analog Devices Inc. |
| usb,product | PlutoSDR (ADALM-PLUTO) |
| usb,serial | 104000409f940014120037006ca8c6bcd2 |
| usb,libusb | 1.0.30.12037 |

---

## 2. Устройство `iio:device0` — `ad9361-phy` (основной RF-чип)

### 2.1 Гетеродины (LO) — центральная частота

| Канал | Атрибут | Значение |
|---|---|---|
| TX_LO (altvoltage1) | frequency | 2 450 000 000 Гц (2.45 ГГц) — текущая |
| TX_LO (altvoltage1) | frequency_available | min 46 875 001 Гц, шаг 1 Гц, **max 6 000 000 000 Гц (6 ГГц)** |
| TX_LO (altvoltage1) | external | 0 |
| TX_LO (altvoltage1) | fastlock_load | 0 |
| TX_LO (altvoltage1) | fastlock_save | 0 (профиль: 201,201,201,201,201,201,201,201,158,201,201,201,201,201,8,201) |
| TX_LO (altvoltage1) | fastlock_store | 0 |
| TX_LO (altvoltage1) | powerdown | 0 |
| RX_LO (altvoltage0) | frequency | 2 400 000 000 Гц (2.4 ГГц) — текущая |
| RX_LO (altvoltage0) | frequency_available | min 70 000 000 Гц, шаг 1 Гц, **max 6 000 000 000 Гц (6 ГГц)** |
| RX_LO (altvoltage0) | external | 0 |
| RX_LO (altvoltage0) | fastlock_load | 0 |
| RX_LO (altvoltage0) | fastlock_save | 0 (профиль: 85,60,105,224,20,9,20,20,20,226,160,161,232,20,100,20) |
| RX_LO (altvoltage0) | fastlock_store | 0 |
| RX_LO (altvoltage0) | powerdown | 0 |

### 2.2 TX-тракт (voltage0 / voltage1, output — TX1/TX2)

| Атрибут | Значение (одинаково на обоих каналах) |
|---|---|
| hardwaregain (затухание TX) | -10.000000 дБ |
| hardwaregain_available | min -89.750000 дБ, шаг 0.250000 дБ, max 0.000000 дБ |
| rf_bandwidth | 18 000 000 Гц (18 МГц) — текущая |
| rf_bandwidth_available | min 200 000 Гц, шаг 1 Гц, **max 40 000 000 Гц (40 МГц)** |
| sampling_frequency | 30 720 000 (30.72 MSPS) — текущая |
| sampling_frequency_available | min 2 083 333, шаг 1, **max 30 720 000 (30.72 MSPS)** |
| rf_port_select | A |
| rf_port_select_available | A, B |
| rssi | 0.00 дБ |
| filter_fir_en | 0 |

### 2.3 RX-тракт (voltage0 / voltage1, input — RX1/RX2)

| Атрибут | RX1 (voltage0) | RX2 (voltage1) |
|---|---|---|
| hardwaregain | 71.000000 дБ | 71.000000 дБ |
| hardwaregain_available | min -3, шаг 1, max 71 | min -3, шаг 1, max 71 |
| gain_control_mode | slow_attack | slow_attack |
| gain_control_mode_available | manual, fast_attack, slow_attack, hybrid | (то же) |
| rf_bandwidth | 18 000 000 Гц | 18 000 000 Гц |
| rf_bandwidth_available | min 200 000, шаг 1, **max 56 000 000 (56 МГц)** | (то же) |
| sampling_frequency | 30 720 000 | 30 720 000 |
| sampling_frequency_available | min 2 083 333, шаг 1, max 30 720 000 | (то же) |
| rssi | 105.75 дБ | 116.25 дБ |
| rf_port_select | A_BALANCED | A_BALANCED |
| bb_dc_offset_tracking_en | 1 | 1 |
| rf_dc_offset_tracking_en | 1 | 1 |
| quadrature_tracking_en | 1 | 1 |
| filter_fir_en | 0 | 0 |

### 2.4 Вспомогательные каналы

| Канал | Атрибут | Значение |
|---|---|---|
| voltage2/voltage3 (output, aux) | raw | 306 |
| voltage2/voltage3 (output, aux) | scale | 1.000000 |
| voltage2/voltage3 (output, aux) | rf_bandwidth | 18 000 000 |
| voltage2 (input, aux monitor) | raw | 1152 |
| voltage2 (input, aux monitor) | offset | 57 |
| voltage2 (input, aux monitor) | scale | 0.305250 |
| temp0 (input) | input (сырое значение температуры) | 35965 |

### 2.5 Устройство в целом (device-specific, 18 атрибутов)

| Атрибут | Значение |
|---|---|
| calib_mode | auto |
| calib_mode_available | auto, manual, manual_tx_quad, tx_quad, rf_dc_offs, rssi_gain_step |
| dcxo_tune_coarse | ошибка: не поддерживается (19) |
| dcxo_tune_coarse_available | [0, 0, 0] |
| dcxo_tune_fine | ошибка: не поддерживается (19) |
| dcxo_tune_fine_available | [0, 0, 0] |
| **ensm_mode** | **fdd** (текущий аппаратный режим чипа) |
| ensm_mode_available | sleep, wait, alert, fdd, pinctrl, pinctrl_fdd_indep |
| filter_fir_config | FIR Rx: 0,0 Tx: 0,0 |
| gain_table_config | таблица калибровки усиления RX, 72 строки (служебные данные, не приводится целиком) |
| multichip_sync | ошибка: доступ запрещён (13) |
| rssi_gain_step_error | lna_error: всё 0; mixer_error: всё 0; gain_step_calib_reg_val: всё 0 |
| rx_path_rates | BBPLL: 983 040 000; ADC: 245 760 000; R2: 122 880 000; R1: 61 440 000; RF: 30 720 000; RXSAMP: 30 720 000 |
| trx_rate_governor | nominal |
| trx_rate_governor_available | nominal, highest_osr |
| tx_path_rates | BBPLL: 983 040 000; DAC: 122 880 000; T2: 122 880 000; T1: 61 440 000; TF: 30 720 000; TXSAMP: 30 720 000 |
| xo_correction | 40 000 000 |
| xo_correction_available | min 39 992 000, шаг 1, max 40 008 000 |

### 2.6 Debug-атрибуты (182 шт.) — низкоуровневые параметры калибровки/AGC

Основная масса — параметры автоматической регулировки усиления (AGC/FAGC), которые прошивка настраивает сама. Приведены полностью для справки; трогать вручную обычно не требуется.

| # | Атрибут | Значение |
|---|---|---|
| 0 | digital_tune | 0 |
| 1 | calibration_switch_control | 0 |
| 2 | multichip_sync | 0 |
| 3 | gaininfo_rx2 | 71 76 0 0 0 0 0 0 |
| 4 | gaininfo_rx1 | 71 76 0 0 0 0 0 0 |
| 5 | bist_timing_analysis | 0 |
| 6 | gpo_set | 0 |
| 7 | bist_tone | 0 |
| 8 | bist_prbs | 0 |
| 9 | loopback | 0 |
| 10 | initialize | 0 |
| 11 | adi,bb-clk-change-dig-tune-enable | 0 |
| 12 | adi,axi-half-dac-rate-enable | 0 |
| 13 | adi,txmon-2-lo-cm | 48 |
| 14 | adi,txmon-1-lo-cm | 48 |
| 15 | adi,txmon-2-front-end-gain | 2 |
| 16 | adi,txmon-1-front-end-gain | 2 |
| 17 | adi,txmon-duration | 8192 |
| 18 | adi,txmon-delay | 511 |
| 19 | adi,txmon-one-shot-mode-enable | 0 |
| 20 | adi,txmon-dc-tracking-enable | 0 |
| 21 | adi,txmon-high-gain | 24 |
| 22 | adi,txmon-low-gain | 0 |
| 23 | adi,txmon-low-high-thresh | 37000 |
| 24 | adi,gpo3-tx-delay-us | 0 |
| 25 | adi,gpo3-rx-delay-us | 0 |
| 26 | adi,gpo2-tx-delay-us | 0 |
| 27 | adi,gpo2-rx-delay-us | 0 |
| 28 | adi,gpo1-tx-delay-us | 0 |
| 29 | adi,gpo1-rx-delay-us | 0 |
| 30 | adi,gpo0-tx-delay-us | 0 |
| 31 | adi,gpo0-rx-delay-us | 0 |
| 32 | adi,gpo3-slave-tx-enable | 0 |
| 33 | adi,gpo3-slave-rx-enable | 0 |
| 34 | adi,gpo2-slave-tx-enable | 0 |
| 35 | adi,gpo2-slave-rx-enable | 0 |
| 36 | adi,gpo1-slave-tx-enable | 0 |
| 37 | adi,gpo1-slave-rx-enable | 0 |
| 38 | adi,gpo0-slave-tx-enable | 0 |
| 39 | adi,gpo0-slave-rx-enable | 0 |
| 40 | adi,gpo3-inactive-state-high-enable | 0 |
| 41 | adi,gpo2-inactive-state-high-enable | 0 |
| 42 | adi,gpo1-inactive-state-high-enable | 0 |
| 43 | adi,gpo0-inactive-state-high-enable | 0 |
| 44 | adi,gpo-manual-mode-enable-mask | 0 |
| 45 | adi,gpo-manual-mode-enable | 0 |
| 46 | adi,aux-dac2-tx-delay-us | 0 |
| 47 | adi,aux-dac2-rx-delay-us | 0 |
| 48 | adi,aux-dac2-active-in-alert-enable | 0 |
| 49 | adi,aux-dac2-active-in-tx-enable | 0 |
| 50 | adi,aux-dac2-active-in-rx-enable | 0 |
| 51 | adi,aux-dac2-default-value-mV | 0 |
| 52 | adi,aux-dac1-tx-delay-us | 0 |
| 53 | adi,aux-dac1-rx-delay-us | 0 |
| 54 | adi,aux-dac1-active-in-alert-enable | 0 |
| 55 | adi,aux-dac1-active-in-tx-enable | 0 |
| 56 | adi,aux-dac1-active-in-rx-enable | 0 |
| 57 | adi,aux-dac1-default-value-mV | 0 |
| 58 | adi,aux-dac-manual-mode-enable | 1 |
| 59 | adi,aux-adc-decimation | 256 |
| 60 | adi,aux-adc-rate | 40 000 000 |
| 61 | adi,temp-sense-decimation | 256 |
| 62 | adi,temp-sense-periodic-measurement-enable | 1 |
| 63 | adi,temp-sense-offset-signed | 206 |
| 64 | adi,temp-sense-measurement-interval-ms | 1000 |
| 65 | adi,elna-gaintable-all-index-enable | 0 |
| 66 | adi,elna-rx2-gpo1-control-enable | 0 |
| 67 | adi,elna-rx1-gpo0-control-enable | 0 |
| 68 | adi,elna-bypass-loss-mdB | 0 |
| 69 | adi,elna-gain-mdB | 0 |
| 70 | adi,elna-settling-delay-ns | 0 |
| 71 | adi,ctrl-outs-enable-mask | 255 |
| 72 | adi,ctrl-outs-index | 0 |
| 73 | adi,rssi-duration | 1000 |
| 74 | adi,rssi-wait | 1 |
| 75 | adi,rssi-delay | 1 |
| 76 | adi,rssi-unit-is-rx-samples-enable | 0 |
| 77 | adi,rssi-restart-mode | 3 |
| 78 | adi,fagc-adc-large-overload-inc-steps | 2 |
| 79 | adi,fagc-power-measurement-duration-in-state5 | 64 |
| 80 | adi,fagc-rst-gla-if-en-agc-pulled-high-mode | 0 |
| 81 | adi,fagc-rst-gla-en-agc-pulled-high-enable | 0 |
| 82 | adi,fagc-rst-gla-large-lmt-overload-enable | 1 |
| 83 | adi,fagc-rst-gla-large-adc-overload-enable | 1 |
| 84 | adi,fagc-energy-lost-stronger-sig-gain-lock-exit-cnt | 8 |
| 85 | adi,fagc-rst-gla-engergy-lost-sig-thresh-below-ll | 10 |
| 86 | adi,fagc-rst-gla-engergy-lost-goto-optim-gain-enable | 1 |
| 87 | adi,fagc-rst-gla-engergy-lost-sig-thresh-exceeded-enable | 1 |
| 88 | adi,fagc-rst-gla-stronger-sig-thresh-above-ll | 10 |
| 89 | adi,fagc-optimized-gain-offset | 5 |
| 90 | adi,fagc-rst-gla-stronger-sig-thresh-exceeded-enable | 1 |
| 91 | adi,fagc-use-last-lock-level-for-set-gain-enable | 1 |
| 92 | adi,fagc-gain-index-type-after-exit-rx-mode | 0 |
| 93 | adi,fagc-gain-increase-after-gain-lock-enable | 0 |
| 94 | adi,fagc-final-overrange-count | 3 |
| 95 | adi,fagc-lmt-final-settling-steps | 1 |
| 96 | adi,fagc-lpf-final-settling-steps | 1 |
| 97 | adi,fagc-lock-level-gain-increase-upper-limit | 5 |
| 98 | adi,fagc-lock-level-lmt-gain-increase-enable | 1 |
| 99 | adi,fagc-lp-thresh-increment-steps | 1 |
| 100 | adi,fagc-lp-thresh-increment-time | 5 |
| 101 | adi,fagc-allow-agc-gain-increase-enable | 0 |
| 102 | adi,fagc-state-wait-time-ns | 260 |
| 103 | adi,fagc-dec-pow-measurement-duration | 64 |
| 104 | adi,agc-immed-gain-change-if-large-lmt-overload-enable | 0 |
| 105 | adi,agc-immed-gain-change-if-large-adc-overload-enable | 0 |
| 106 | adi,agc-gain-update-interval-us | 1000 |
| 107 | adi,agc-sync-for-gain-counter-enable | 0 |
| 108 | adi,agc-dig-gain-step-size | 4 |
| 109 | adi,agc-dig-saturation-exceed-counter | 3 |
| 110 | adi,agc-lmt-overload-large-inc-steps | 2 |
| 111 | adi,agc-lmt-overload-small-exceed-counter | 10 |
| 112 | adi,agc-lmt-overload-large-exceed-counter | 10 |
| 113 | adi,agc-adc-lmt-small-overload-prevent-gain-inc-enable | 0 |
| 114 | adi,agc-adc-large-overload-inc-steps | 2 |
| 115 | adi,agc-adc-large-overload-exceed-counter | 10 |
| 116 | adi,agc-adc-small-overload-exceed-counter | 10 |
| 117 | adi,agc-outer-thresh-low-inc-steps | 2 |
| 118 | adi,agc-outer-thresh-low | 18 |
| 119 | adi,agc-inner-thresh-low-inc-steps | 1 |
| 120 | adi,agc-inner-thresh-low | 12 |
| 121 | adi,agc-inner-thresh-high-dec-steps | 1 |
| 122 | adi,agc-inner-thresh-high | 10 |
| 123 | adi,agc-outer-thresh-high-dec-steps | 2 |
| 124 | adi,agc-outer-thresh-high | 5 |
| 125 | adi,agc-attack-delay-extra-margin-us | 1 |
| 126 | adi,mgc-split-table-ctrl-inp-gain-mode | 0 |
| 127 | adi,mgc-dec-gain-step | 2 |
| 128 | adi,mgc-inc-gain-step | 2 |
| 129 | adi,mgc-rx2-ctrl-inp-enable | 0 |
| 130 | adi,mgc-rx1-ctrl-inp-enable | 0 |
| 131 | adi,gc-use-rx-fir-out-for-dec-pwr-meas-enable | 0 |
| 132 | adi,gc-max-dig-gain | 15 |
| 133 | adi,gc-dig-gain-enable | 0 |
| 134 | adi,gc-low-power-thresh | 24 |
| 135 | adi,gc-dec-pow-measurement-duration | 8192 |
| 136 | adi,gc-lmt-overload-low-thresh | 704 |
| 137 | adi,gc-lmt-overload-high-thresh | 800 |
| 138 | adi,gc-adc-large-overload-thresh | 58 |
| 139 | adi,gc-adc-small-overload-thresh | 47 |
| 140 | adi,gc-adc-ovr-sample-size | 4 |
| 141 | adi,gc-rx2-mode | 2 |
| 142 | adi,gc-rx1-mode | 2 |
| 143 | adi,update-tx-gain-in-alert-enable | 0 |
| 144 | adi,tx-attenuation-mdB | 10000 |
| 145 | adi,rf-tx-bandwidth-hz | 18 000 000 |
| 146 | adi,rf-rx-bandwidth-hz | 18 000 000 |
| 147 | adi,qec-tracking-slow-mode-enable | 0 |
| 148 | adi,dc-offset-count-low-range | 50 |
| 149 | adi,dc-offset-count-high-range | 40 |
| 150 | adi,dc-offset-attenuation-low-range | 5 |
| 151 | adi,dc-offset-attenuation-high-range | 6 |
| 152 | adi,dc-offset-tracking-update-event-mask | 5 |
| 153 | adi,clk-output-mode-select | 0 |
| 154 | adi,external-rx-lo-enable | 0 |
| 155 | adi,external-tx-lo-enable | 0 |
| 156 | adi,xo-disable-use-ext-refclk-enable | 1 |
| 157 | adi,tx-lo-powerdown-managed-enable | 1 |
| 158 | adi,trx-synthesizer-target-fref-overwrite-hz | 80 008 000 |
| 159 | adi,rx1-rx2-phase-inversion-enable | 0 |
| 160 | adi,tx-rf-port-input-select-lock-enable | 1 |
| 161 | adi,rx-rf-port-input-select-lock-enable | 1 |
| 162 | adi,tx-rf-port-input-select | 0 |
| 163 | adi,rx-rf-port-input-select | 0 |
| 164 | adi,split-gain-table-mode-enable | 0 |
| 165 | adi,1rx-1tx-mode-use-tx-num | 1 |
| 166 | adi,1rx-1tx-mode-use-rx-num | 1 |
| 167 | **adi,2rx-2tx-mode-enable** | **1** (устройство работает в режиме 2×2 MIMO) |
| 168 | adi,digital-interface-tune-fir-disable | 1 |
| 169 | adi,digital-interface-tune-skip-mode | 0 |
| 170 | adi,tx-fastlock-pincontrol-enable | 0 |
| 171 | adi,rx-fastlock-pincontrol-enable | 0 |
| 172 | adi,rx-fastlock-delay-ns | 0 |
| 173 | adi,tx-fastlock-delay-ns | 0 |
| 174 | adi,tdd-skip-vco-cal-enable | 0 |
| 175 | adi,tdd-use-dual-synth-mode-enable | 0 |
| 176 | adi,debug-mode-enable | 0 |
| 177 | adi,ensm-enable-txnrx-control-enable | 0 |
| 178 | adi,ensm-enable-pin-pulse-mode-enable | 0 |
| 179 | adi,frequency-division-duplex-independent-mode-enable | 0 |
| 180 | **adi,frequency-division-duplex-mode-enable** | **1** (чип аппаратно работает в FDD) |
| 181 | direct_reg_access | 0x0 |

---

## 3. Устройство `iio:device1` — `xadc` (телеметрия платы: питание и температура)

| Канал | raw | scale | offset |
|---|---|---|---|
| voltage0 (vccint) | 1396 | 0.732421875 | — |
| voltage1 (vccaux) | 2465 | 0.732421875 | — |
| voltage2 (vccbram) | 1394 | 0.732421875 | — |
| voltage3 (vccpint) | 1387 | 0.732421875 | — |
| voltage4 (vccpaux) | 2465 | 0.732421875 | — |
| voltage5 (vccoddr) | 2063 | 0.732421875 | — |
| voltage6 (vrefp) | 1706 | 0.732421875 | — |
| voltage7 (vrefn) | 5 | 0.732421875 | — |
| voltage8 | 3697 | 0.244140625 | — |
| temp0 | 2635 | 123.040771484 | -2219 |

Device-specific: `sampling_frequency` = 961 538

*(реальные значения напряжения/температуры считаются как `raw × scale` (+ `offset` для температуры); отдельно вычислять не стал, чтобы не приводить непроверенные цифры)*

---

## 4. Устройство `iio:device2` — `cf-ad9361-dds-core-lpc` (буфер передачи TX + встроенный генератор тонов DDS)

### 4.1 Буферные каналы I/Q (voltage0–voltage3, output)

| Атрибут | Значение (одинаково на всех 4 каналах) |
|---|---|
| calibphase | 0.000000 |
| calibscale | 1.000000 |
| sampling_frequency | 30 720 000 |
| sampling_frequency_available | 30 720 000 или 3 840 000 (только 2 дискретных значения, не диапазон) |

### 4.2 Встроенный генератор тонов DDS (altvoltage0–altvoltage7)

| Канал | frequency | phase | raw | sampling_frequency | scale |
|---|---|---|---|---|---|
| TX1_I_F1 | 9 279 985 | 90000 | 1 | 30 720 000 | 0.000000 |
| TX1_Q_F1 | 9 279 985 | 0 | 1 | 30 720 000 | 0.000000 |
| TX1_I_F2 | 9 279 985 | 90000 | 1 | 30 720 000 | 0.000000 |
| TX1_Q_F2 | 9 279 985 | 0 | 1 | 30 720 000 | 0.000000 |
| TX2_I_F1 | 9 279 985 | 90000 | 1 | 30 720 000 | 0.000000 |
| TX2_Q_F1 | 9 279 985 | 0 | 1 | 30 720 000 | 0.000000 |
| TX2_I_F2 | 9 279 985 | 90000 | 1 | 30 720 000 | 0.000000 |
| TX2_Q_F2 | 9 279 985 | 0 | 1 | 30 720 000 | 0.000000 |

### 4.3 Буфер и отладка

| Атрибут | Значение |
|---|---|
| data_available | 0 |
| length_align_bytes | 8 |
| watermark | 2048 |
| debug: direct_reg_access | 0x90162 |

---

## 5. Устройство `iio:device3` — `cf-ad9361-lpc` (буфер приёма RX)

### 5.1 Буферные каналы I/Q (voltage0–voltage3, input)

| Атрибут | Значение (одинаково на всех 4 каналах) |
|---|---|
| calibbias | 0 |
| calibphase | 0.000000 |
| calibscale | 1.000000 |
| samples_pps | ошибка: не поддерживается (19) |
| sampling_frequency | 30 720 000 |
| sampling_frequency_available | 30 720 000 или 3 840 000 |

### 5.2 Буфер и отладка

| Атрибут | Значение |
|---|---|
| data_available | 0 |
| length_align_bytes | 8 |
| watermark | 2048 |
| debug: pseudorandom_err_check | CH0–CH3: PN9, Out of Sync, PN Error (все 4 канала) |
| debug: direct_reg_access | 0x0 |

---

## Итог: ключевые цифры одной строкой

- **Диапазон центральной частоты (LO):** TX 46.875 МГц – 6 ГГц, RX 70 МГц – 6 ГГц (расширенный, не заводской 325 МГц – 3.8 ГГц)
- **Полоса (rf_bandwidth):** TX 200 кГц – 40 МГц, RX 200 кГц – 56 МГц
- **Частота дискретизации:** 2.083 – 30.72 MSPS (сейчас 30.72 MSPS — совпадает с LTE 20 МГц)
- **Усиление RX:** -3…71 дБ (сейчас 71 дБ, AGC slow_attack)
- **Затухание TX:** -89.75…0 дБ (сейчас -10 дБ)
- **Режим чипа:** FDD (аппаратно), 2×2 MIMO включен
- **Опорный генератор:** 40 МГц, подстройка ±8 кГц
