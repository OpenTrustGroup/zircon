// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD_style license that can be
// found in the LICENSE file.

#pragma once

// MMIO regions
#define MMIO_CCI_CFG_BASE                       0xE8100000
#define MMIO_CCI_CFG_LENGTH                     0x100000
#define MMIO_GIC400_BASE                        0xE82B0000
#define MMIO_GIC400_LENGTH                      0x8000
#define MMIO_HKADC_SSI_BASE                     0xE82B8000
#define MMIO_HKADC_SSI_LENGTH                   0x1000
#define MMIO_CODEC_SSI_BASE                     0xE82B9000
#define MMIO_CODEC_SSI_LENGTH                   0x1000
#define MMIO_G3D_BASE                           0xE82C0000
#define MMIO_G3D_LENGTH                         0x4000
#define MMIO_DSI_BASE                           0xE8601000
#define MMIO_DSI_LENGTH                         0x7F000
#define MMIO_IPC_BASE                           0xE896A000
#define MMIO_IPC_LENGTH                         0x1000
#define MMIO_IPC_NS_BASE                        0xE896B000
#define MMIO_IPC_NS_LENGTH                      0x1000
#define MMIO_IOC_BASE                           0xE896C000
#define MMIO_IOC_LENGTH                         0x1000
#define MMIO_TIMER9_BASE                        0xE8A00000
#define MMIO_TIMER9_LENGTH                      0x1000
#define MMIO_TIMER10_BASE                       0xE8A01000
#define MMIO_TIMER10_LENGTH                     0x1000
#define MMIO_TIMER11_BASE                       0xE8A02000
#define MMIO_TIMER11_LENGTH                     0x1000
#define MMIO_TIMER12_BASE                       0xE8A03000
#define MMIO_TIMER12_LENGTH                     0x1000
#define MMIO_PWM_BASE                           0xE8A04000
#define MMIO_PWM_LENGTH                         0x1000
#define MMIO_WD0_BASE                           0xE8A06000
#define MMIO_WD0_LENGTH                         0x1000
#define MMIO_WD1_BASE                           0xE8A07000
#define MMIO_WD1_LENGTH                         0x1000
#define MMIO_PCTRL_BASE                         0xE8A09000
#define MMIO_PCTRL_LENGTH                       0x1000
#define MMIO_GPIO0_SE_BASE                      0xE8A0A000
#define MMIO_GPIO0_SE_LENGTH                    0x1000
#define MMIO_GPIO0_BASE                         0xE8A0B000
#define MMIO_GPIO0_LENGTH                       0x1000
#define MMIO_GPIO1_BASE                         0xE8A0C000
#define MMIO_GPIO1_LENGTH                       0x1000
#define MMIO_GPIO2_BASE                         0xE8A0D000
#define MMIO_GPIO2_LENGTH                       0x1000
#define MMIO_GPIO3_BASE                         0xE8A0E000
#define MMIO_GPIO3_LENGTH                       0x1000
#define MMIO_GPIO4_BASE                         0xE8A0F000
#define MMIO_GPIO4_LENGTH                       0x1000
#define MMIO_GPIO5_BASE                         0xE8A10000
#define MMIO_GPIO5_LENGTH                       0x1000
#define MMIO_GPIO6_BASE                         0xE8A11000
#define MMIO_GPIO6_LENGTH                       0x1000
#define MMIO_GPIO7_BASE                         0xE8A12000
#define MMIO_GPIO7_LENGTH                       0x1000
#define MMIO_GPIO8_BASE                         0xE8A13000
#define MMIO_GPIO8_LENGTH                       0x1000
#define MMIO_GPIO9_BASE                         0xE8A14000
#define MMIO_GPIO9_LENGTH                       0x1000
#define MMIO_GPIO10_BASE                        0xE8A15000
#define MMIO_GPIO10_LENGTH                      0x1000
#define MMIO_GPIO11_BASE                        0xE8A16000
#define MMIO_GPIO11_LENGTH                      0x1000
#define MMIO_GPIO12_BASE                        0xE8A17000
#define MMIO_GPIO12_LENGTH                      0x1000
#define MMIO_GPIO13_BASE                        0xE8A18000
#define MMIO_GPIO13_LENGTH                      0x1000
#define MMIO_GPIO14_BASE                        0xE8A19000
#define MMIO_GPIO14_LENGTH                      0x1000
#define MMIO_GPIO15_BASE                        0xE8A1A000
#define MMIO_GPIO15_LENGTH                      0x1000
#define MMIO_GPIO16_BASE                        0xE8A1B000
#define MMIO_GPIO16_LENGTH                      0x1000
#define MMIO_GPIO17_BASE                        0xE8A1C000
#define MMIO_GPIO17_LENGTH                      0x1000
#define MMIO_GPIO20_BASE                        0xE8A1F000
#define MMIO_GPIO20_LENGTH                      0x1000
#define MMIO_GPIO21_BASE                        0xE8A20000
#define MMIO_GPIO21_LENGTH                      0x1000
#define MMIO_CFGBUS_SERVICE_TARGET_BASE         0xE9800000
#define MMIO_CFGBUS_SERVICE_TARGET_LENGTH       0x10000
#define MMIO_UFSBUS_SERVICE_TARGET_BASE         0xE9810000
#define MMIO_UFSBUS_SERVICE_TARGET_LENGTH       0x10000
#define MMIO_DMA_NOC_SERVICE_TARGET_BASE        0xE9860000
#define MMIO_DMA_NOC_SERVICE_TARGET_LENGTH      0x10000
#define MMIO_AOBUS_SERVICE_TARGET_BASE          0xE9870000
#define MMIO_AOBUS_SERVICE_TARGET_LENGTH        0x10000
#define MMIO_MMC1_NOC_SERVICE_TARGET_BASE       0xE9880000
#define MMIO_MMC1_NOC_SERVICE_TARGET_LENGTH     0x10000
#define MMIO_MMC0_NOC_SERVICE_TARGET_BASE       0xE9890000
#define MMIO_MMC0_NOC_SERVICE_TARGET_LENGTH     0x10000
#define MMIO_CSSYS_APB_BASE                     0xEC000000
#define MMIO_CSSYS_APB_LENGTH                   0x1800000
#define MMIO_IOMCU_TCM_BASE                     0xF0000000
#define MMIO_IOMCU_TCM_LENGTH                   0xC00000
#define MMIO_PCIEPHY_BASE                       0xF3F00000
#define MMIO_PCIEPHY_LENGTH                     0x40000
#define MMIO_PCIECTRL_BASE                      0xF4000000
#define MMIO_PCIECTRL_LENGTH                    0x8000000
#define MMIO_UART1_BASE                         0xFDF00000
#define MMIO_UART1_LENGTH                       0x1000
#define MMIO_UART4_BASE                         0xFDF01000
#define MMIO_UART4_LENGTH                       0x1000
#define MMIO_UART0_BASE                         0xFDF02000
#define MMIO_UART0_LENGTH                       0x1000
#define MMIO_UART2_BASE                         0xFDF03000
#define MMIO_UART2_LENGTH                       0x1000
#define MMIO_UART5_BASE                         0xFDF05000
#define MMIO_UART5_LENGTH                       0x1000
#define MMIO_SPI4_BASE                          0xFDF06000
#define MMIO_SPI4_LENGTH                        0x1000
#define MMIO_SPI1_BASE                          0xFDF08000
#define MMIO_SPI1_LENGTH                        0x1000
#define MMIO_I2C7_BASE                          0xFDF0B000
#define MMIO_I2C7_LENGTH                        0x1000
#define MMIO_I2C3_BASE                          0xFDF0C000
#define MMIO_I2C3_LENGTH                        0x1000
#define MMIO_I2C4_BASE                          0xFDF0D000
#define MMIO_I2C4_LENGTH                        0x1000
#define MMIO_PERF_STAT_BASE                     0xFDF10000
#define MMIO_PERF_STAT_LENGTH                   0x1000
#define MMIO_PERI_DMAC_BASE                     0xFDF30000
#define MMIO_PERI_DMAC_LENGTH                   0x1000
#define MMIO_IPC_MDM_S_BASE                     0xFF010000
#define MMIO_IPC_MDM_S_LENGTH                   0x1000
#define MMIO_IPC_MDM_NS_BASE                    0xFF011000
#define MMIO_IPC_MDM_NS_LENGTH                  0x1000
#define MMIO_USB3OTG_BASE                       0xFF100000
#define MMIO_USB3OTG_LENGTH                     0x100000
#define MMIO_USB3OTG_BC_BASE                    0xFF200000
#define MMIO_USB3OTG_BC_LENGTH                  0x1000
#define MMIO_IOC_MMC0_BASE                      0xFF37E000
#define MMIO_IOC_MMC0_LENGTH                    0x1000
#define MMIO_SD3_BASE                           0xFF37F000
#define MMIO_SD3_LENGTH                         0x1000
#define MMIO_UFS_CFG_BASE                       0xFF3B0000
#define MMIO_UFS_CFG_LENGTH                     0x1000
#define MMIO_UFS_SYS_CTRL_BASE                  0xFF3B1000
#define MMIO_UFS_SYS_CTRL_LENGTH                0x1000
#define MMIO_SPI3_BASE                          0xFF3B3000
#define MMIO_SPI3_LENGTH                        0x1000
#define MMIO_GPIO18_BASE                        0xFF3B4000
#define MMIO_GPIO18_LENGTH                      0x1000
#define MMIO_GPIO19_BASE                        0xFF3B5000
#define MMIO_GPIO19_LENGTH                      0x1000
#define MMIO_IOC_FIX_BASE                       0xFF3B6000
#define MMIO_IOC_FIX_LENGTH                     0x1000
#define MMIO_GPIO0_MMC1_BASE                    0xFF3E0000
#define MMIO_GPIO0_MMC1_LENGTH                  0x1000
#define MMIO_GPIO1_MMC1_BASE                    0xFF3E1000
#define MMIO_GPIO1_MMC1_LENGTH                  0x1000
#define MMIO_EMMC_BASE                          0xFF3FB000
#define MMIO_EMMC_LENGTH                        0x1000
#define MMIO_IOC_MMC1_BASE                      0xFF3FD000
#define MMIO_IOC_MMC1_LENGTH                    0x1000
#define MMIO_PCIE_APB_CFG_BASE                  0xFF3FE000
#define MMIO_PCIE_APB_CFG_LENGTH                0x1000
#define MMIO_SDIO0_BASE                         0xFF3FF000
#define MMIO_SDIO0_LENGTH                       0x1000
#define MMIO_IOMCU_BASE                         0xFFD00000
#define MMIO_IOMCU_LENGTH                       0x80000
#define MMIO_I2C0_BASE                          0xFFD71000
#define MMIO_I2C0_LENGTH                        0x1000
#define MMIO_I2C1_BASE                          0xFFD72000
#define MMIO_I2C1_LENGTH                        0x1000
#define MMIO_I2C2_BASE                          0xFFD73000
#define MMIO_I2C2_LENGTH                        0x1000
#define MMIO_IOMCU_CONFIG_BASE                  0xFFD7E000
#define MMIO_IOMCU_CONFIG_LENGTH                0x1000
#define MMIO_RTC0_BASE                          0xFFF04000
#define MMIO_RTC0_LENGTH                        0x1000
#define MMIO_RTC1_BASE                          0xFFF05000
#define MMIO_RTC1_LENGTH                        0x1000
#define MMIO_SYS_CNT_BASE                       0xFFF08000
#define MMIO_SYS_CNT_LENGTH                     0x2000
#define MMIO_SCTRL_BASE                         0xFFF0A000
#define MMIO_SCTRL_LENGTH                       0x1000
#define MMIO_GPIO22_BASE                        0xFFF0B000
#define MMIO_GPIO22_LENGTH                      0x1000
#define MMIO_GPIO23_BASE                        0xFFF0C000
#define MMIO_GPIO23_LENGTH                      0x1000
#define MMIO_GPIO24_BASE                        0xFFF0D000
#define MMIO_GPIO24_LENGTH                      0x1000
#define MMIO_GPIO25_BASE                        0xFFF0E000
#define MMIO_GPIO25_LENGTH                      0x1000
#define MMIO_GPIO26_BASE                        0xFFF0F000
#define MMIO_GPIO26_LENGTH                      0x1000
#define MMIO_GPIO27_BASE                        0xFFF10000
#define MMIO_GPIO27_LENGTH                      0x1000
#define MMIO_AO_IOC_BASE                        0xFFF11000
#define MMIO_AO_IOC_LENGTH                      0x1000
#define MMIO_IOMG_PMX4_BASE                     0xFFF11000
#define MMIO_IOMG_PMX4_LENGTH                   0x1000
#define MMIO_TIMER0_BASE                        0xFFF14000
#define MMIO_TIMER0_LENGTH                      0x1000
#define MMIO_TIMER1_BASE                        0xFFF15000
#define MMIO_TIMER1_LENGTH                      0x1000
#define MMIO_TIMER2_BASE                        0xFFF16000
#define MMIO_TIMER2_LENGTH                      0x1000
#define MMIO_TIMER3_BASE                        0xFFF17000
#define MMIO_TIMER3_LENGTH                      0x1000
#define MMIO_TIMER4_BASE                        0xFFF18000
#define MMIO_TIMER4_LENGTH                      0x1000
#define MMIO_TIMER5_BASE                        0xFFF19000
#define MMIO_TIMER5_LENGTH                      0x1000
#define MMIO_TIMER6_BASE                        0xFFF1A000
#define MMIO_TIMER6_LENGTH                      0x1000
#define MMIO_TIMER7_BASE                        0xFFF1B000
#define MMIO_TIMER7_LENGTH                      0x1000
#define MMIO_TIMER8_BASE                        0xFFF1C000
#define MMIO_TIMER8_LENGTH                      0x1000
#define MMIO_GPIO28_BASE                        0xFFF1D000
#define MMIO_GPIO28_LENGTH                      0x1000
#define MMIO_TSENSORC_BASE                      0xFFF30000
#define MMIO_TSENSORC_LENGTH                    0x1000
#define MMIO_PMCTRL_BASE                        0xFFF31000
#define MMIO_PMCTRL_LENGTH                      0x1000
#define MMIO_UART6_BASE                         0xFFF32000
#define MMIO_UART6_LENGTH                       0x1000
#define MMIO_PMU_I2C_BASE                       0xFFF33000
#define MMIO_PMU_I2C_LENGTH                     0x1000
#define MMIO_PMU_SSI0_BASE                      0xFFF34000
#define MMIO_PMU_SSI0_LENGTH                    0x1000
#define MMIO_PERI_CRG_BASE                      0xFFF35000
#define MMIO_PERI_CRG_LENGTH                    0x1000
#define MMIO_PMU_SSI1_BASE                      0xFFF36000
#define MMIO_PMU_SSI1_LENGTH                    0x1000
#define MMIO_PMU_SSI2_BASE                      0xFFF38000
#define MMIO_PMU_SSI2_LENGTH                    0x1000

// Interrupts
#define IRQ_A73_INTERR                          32
#define IRQ_A73_EXTERR                          33
#define IRQ_A73_PMU0                            34
#define IRQ_A73_PMU1                            35
#define IRQ_A73_PMU2                            36
#define IRQ_A73_PMU3                            37
#define IRQ_A73_CTI0                            38
#define IRQ_A73_CTI1                            39
#define IRQ_A73_CTI2                            40
#define IRQ_A73_CTI3                            41
#define IRQ_A73_COMMRX0                         42
#define IRQ_A73_COMMRX1                         43
#define IRQ_A73_COMMRX2                         44
#define IRQ_A73_COMMRX3                         45
#define IRQ_A73_COMMTX0                         46
#define IRQ_A73_COMMTX1                         47
#define IRQ_A73_COMMTX2                         48
#define IRQ_A73_COMMTX3                         49
#define IRQ_A73_COMMIRQ0                        50
#define IRQ_A73_COMMIRQ1                        51
#define IRQ_A73_COMMIRQ2                        52
#define IRQ_A73_COMMIRQ3                        53
#define IRQ_A53_INTERR                          54
#define IRQ_A53_EXTERR                          55
#define IRQ_A53_PMU0                            56
#define IRQ_A53_PMU1                            57
#define IRQ_A53_PMU2                            58
#define IRQ_A53_PMU3                            59
#define IRQ_A53_CTI0                            60
#define IRQ_A53_CTI1                            61
#define IRQ_A53_CTI2                            62
#define IRQ_A53_CTI3                            63
#define IRQ_A53_COMMRX0                         64
#define IRQ_A53_COMMRX1                         65
#define IRQ_A53_COMMRX2                         66
#define IRQ_A53_COMMRX3                         67
#define IRQ_A53_COMMTX0                         68
#define IRQ_A53_COMMTX1                         69
#define IRQ_A53_COMMTX2                         70
#define IRQ_A53_COMMTX3                         71
#define IRQ_A53_COMMIRQ0                        72
#define IRQ_A53_COMMIRQ1                        73
#define IRQ_A53_COMMIRQ2                        74
#define IRQ_A53_COMMIRQ3                        75
#define IRQ_WATCHDOG0                           76
#define IRQ_WATCHDOG1                           77
#define IRQ_RTC0                                78
#define IRQ_RTC1                                79
#define IRQ_TIME00                              80
#define IRQ_TIME01                              81
#define IRQ_TIME10                              82
#define IRQ_TIME11                              83
#define IRQ_TIME20                              84
#define IRQ_TIME21                              85
#define IRQ_TIME30                              86
#define IRQ_TIME31                              87
#define IRQ_TIME40                              88
#define IRQ_TIME41                              89
#define IRQ_TIME50                              90
#define IRQ_TIME51                              91
#define IRQ_TIME60                              92
#define IRQ_TIME61                              93
#define IRQ_TIME70                              94
#define IRQ_TIME71                              95
#define IRQ_TIME80                              96
#define IRQ_TIME81                              97
#define IRQ_TIME90                              98
#define IRQ_TIME91                              99
#define IRQ_TIME100                             100
#define IRQ_TIME101                             101
#define IRQ_TIME110                             102
#define IRQ_TIME111                             103
#define IRQ_TIME120                             104
#define IRQ_TIME121                             105
#define IRQ_UART0                               106
#define IRQ_UART1                               107
#define IRQ_UART2                               108
#define IRQ_UART4                               109
#define IRQ_UART5                               110
#define IRQ_UART6                               111
#define IRQ_SPI1                                112
#define IRQ_I2C3                                113
#define IRQ_I2C4                                114
#define IRQ_I2C5                                115
#define IRQ_GPIO0_INTR1                         116
#define IRQ_GPIO1_INTR1                         117
#define IRQ_GPIO2_INTR1                         118
#define IRQ_GPIO3_INTR1                         119
#define IRQ_GPIO4_INTR1                         120
#define IRQ_GPIO5_INTR1                         121
#define IRQ_GPIO6_INTR1                         122
#define IRQ_GPIO7_INTR1                         123
#define IRQ_GPIO8_INTR1                         124
#define IRQ_GPIO9_INTR1                         125
#define IRQ_GPIO10_INTR1                        126
#define IRQ_GPIO11_INTR1                        127
#define IRQ_GPIO12_INTR1                        128
#define IRQ_GPIO13_INTR1                        129
#define IRQ_GPIO14_INTR1                        130
#define IRQ_GPIO15_INTR1                        131
#define IRQ_GPIO16_INTR1                        132
#define IRQ_GPIO17_INTR1                        133
#define IRQ_GPIO18_INTR1                        134
#define IRQ_GPIO19_INTR1                        135
#define IRQ_GPIO20_INTR1                        136
#define IRQ_GPIO21_INTR1                        137
#define IRQ_GPIO22_INTR1                        138
#define IRQ_GPIO23_INTR1                        139
#define IRQ_GPIO24_INTR1                        140
#define IRQ_GPIO25_INTR1                        141
#define IRQ_GPIO26_INTR1                        142
#define IRQ_GPIO27_INTR1                        143
#define IRQ_IOMCU_WD                            144
#define IRQ_IOMCU_SPI                           145
#define IRQ_IOMCU_UART3                         146
#define IRQ_IOMCU_UART8                         147
#define IRQ_IOMCU_SPI2                          148
#define IRQ_IOMCU_I2C3                          149
#define IRQ_IOMCU_I2C0                          150
#define IRQ_IOMCU_I2C1                          151
#define IRQ_IOMCU_I2C2                          152
#define IRQ_IOMCU_GPIO0_INT1                    153
#define IRQ_IOMCU_GPIO1_INT1                    154
#define IRQ_IOMCU_GPIO2_INT1                    155
#define IRQ_IOMCU_GPIO3_INT1                    156
#define IRQ_IOMCU_DMAC_INT0                     157
#define IRQ_IOMCU_DMAC_NS_INT0                  158
#define IRQ_PERF_STAT                           159
#define IRQ_IOMCU_COMB                          160
#define IRQ_IOMCU_BLPWM                         161
#define IRQ_NOC_COMB                            162
#define IRQ_INTR_DMSS                           163
#define IRQ_INTR_DDRC0_ERR                      164
#define IRQ_INTR_DDRC1_ERR                      165
#define IRQ_PMCTRL                              166
#define IRQ_SECENG_P                            167
#define IRQ_SECENG_S                            168
#define IRQ_EMMC51                              169
#define IRQ_ASP_IPC_MODEM_CBBE                  170
#define IRQ_SD3                                 171
#define IRQ_SDIO                                172
#define IRQ_GPIO28_INTR1                        173
#define IRQ_PERI_DMAC_INT0                      174
#define IRQ_PERI_DMAC_NS_INT0                   175
#define IRQ_CLK_MONITOR                         176
#define IRQ_TSENSOR_A73                         177
#define IRQ_TSENSOR_A53                         178
#define IRQ_TSENSOR_G3D                         179
#define IRQ_TSENSOR_MODEM                       180
#define IRQ_ASP_ARM_SECURE                      181
#define IRQ_ASP_ARM                             182
#define IRQ_VDM_INT2                            183
#define IRQ_VDM_INT0                            184
#define IRQ_VDM_INT1                            185
#define IRQ_MODEM_IPC0                          186
#define IRQ_MODEM_IPC1                          187
#define IRQ_MDM_BUS_ERR                         188
#define IRQ_MDM_EDMAC0_INTR_NS_0                190
#define IRQ_USB3                                191
#define IRQ_USB3_OTG                            193
#define IRQ_USB3_BC                             194
#define IRQ_GPIO1_SE_INTR1                      195
#define IRQ_GPIO0_SE_INTR1                      196
#define IRQ_PMC_DVFS_A73                        197
#define IRQ_PMC_DVFS_A53                        198
#define IRQ_PMC_DVFS_G3D                        199
#define IRQ_PMC_AVS_A73                         200
#define IRQ_PMC_AVS_A53                         201
#define IRQ_PMC_AVS_G3D                         202
#define IRQ_PMC_AVS_IDLE_A73                    203
#define IRQ_PMC_AVS_IDLE_A53                    204
#define IRQ_PMC_AVS_IDLE_G3D                    205
#define IRQ_M3_LP_WD                            206
#define IRQ_CCI400_ERR                          207
#define IRQ_CCI400_OVERFLOW_6_0                 208
#define IRQ_CCI400_OVERFLOW_7                   209
#define IRQ_IPC_S_INT0                          210
#define IRQ_IPC_S_INT1                          211
#define IRQ_IPC_S_INT4                          212
#define IRQ_IPC_S_MBX0                          213
#define IRQ_IPC_S_MBX1                          214
#define IRQ_IPC_S_MBX2                          215
#define IRQ_IPC_S_MBX3                          216
#define IRQ_IPC_S_MBX4                          217
#define IRQ_IPC_S_MBX5                          218
#define IRQ_IPC_S_MBX6                          219
#define IRQ_IPC_S_MBX7                          220
#define IRQ_IPC_S_MBX8                          221
#define IRQ_IPC_S_MBX9                          222
#define IRQ_IPC_S_MBX18                         223
#define IRQ_IPC_NS_INT0                         224
#define IRQ_IPC_NS_INT1                         225
#define IRQ_IPC_NS_INT4                         226
#define IRQ_IPC_NS_INT5                         227
#define IRQ_IPC_NS_INT6                         228
#define IRQ_IPC_NS_MBX0                         229
#define IRQ_IPC_NS_MBX1                         230
#define IRQ_IPC_NS_MBX2                         231
#define IRQ_IPC_NS_MBX3                         232
#define IRQ_IPC_NS_MBX4                         233
#define IRQ_IPC_NS_MBX5                         234
#define IRQ_IPC_NS_MBX6                         235
#define IRQ_IPC_NS_MBX7                         236
#define IRQ_IPC_NS_MBX8                         237
#define IRQ_IPC_NS_MBX9                         238
#define IRQ_IPC_NS_MBX18                        239
#define IRQ_MDM_AXIMON_INTR                     240
#define IRQ_MDM_WDOG_INTR                       241
#define IRQ_ASP_IPC_ARM                         242
#define IRQ_ASP_IPC_MCPU                        243
#define IRQ_ASP_IPC_BBE16                       244
#define IRQ_ASP_WD                              245
#define IRQ_ASP_AXI_DLOCK                       246
#define IRQ_ASP_DMA_SECURE                      247
#define IRQ_ASP_DMA_SECURE_N                    248
#define IRQ_SCI0                                249
#define IRQ_SCI1                                250
#define IRQ_SOCP0                               251
#define IRQ_SOCP1                               252
#define IRQ_MDM_IPF_INTR0                       253
#define IRQ_MDM_IPF_INTR1                       254
#define IRQ_IDDRC_FATAL_INT_3_0                 255
#define IRQ_MDM_AXI_DLOCK_INT                   256
#define IRQ_MDM_WDT1_INTR                       257
#define IRQ_GIC_IRQ_OUT_0                       258
#define IRQ_GIC_IRQ_OUT_1                       259
#define IRQ_GIC_IRQ_OUT_2                       260
#define IRQ_GIC_IRQ_OUT_3                       261
#define IRQ_GIC_IRQ_OUT_4                       262
#define IRQ_GIC_IRQ_OUT_5                       263
#define IRQ_GIC_IRQ_OUT_6                       264
#define IRQ_GIC_IRQ_OUT_7                       265
#define IRQ_GIC_FIQ_OUT_0                       266
#define IRQ_GIC_FIQ_OUT_1                       267
#define IRQ_GIC_FIQ_OUT_2                       268
#define IRQ_GIC_FIQ_OUT_3                       269
#define IRQ_GIC_FIQ_OUT_4                       270
#define IRQ_GIC_FIQ_OUT_5                       271
#define IRQ_GIC_FIQ_OUT_6                       272
#define IRQ_GIC_FIQ_OUT_7                       273
#define IRQ_NANDC                               274
#define IRQ_CORESIGHT_ETR_FULL                  275
#define IRQ_CORESIGHT_ETF_FULL                  276
#define IRQ_DSS_PDP                             277
#define IRQ_DSS_SDP                             278
#define IRQ_DSS_OFFLINE                         279
#define IRQ_DSS_MCU_PDP                         280
#define IRQ_DSS_MCU_SDP                         281
#define IRQ_DSS_MCU_OFFLINE                     282
#define IRQ_DSS_DSI0                            283
#define IRQ_DSS_DSI1                            284
#define IRQ_IVP32_SMMU_IRPT_S                   285
#define IRQ_IVP32_SMMU_IRPT_NS                  286
#define IRQ_IVP32_WATCH_DOG                     287
#define IRQ_ATGC                                288
#define IRQ_G3D_IRQEVENT                        289
#define IRQ_G3D_JOB                             290
#define IRQ_G3D_MMU                             291
#define IRQ_G3D_GPU                             292
#define IRQ_ISP_IRQ_0                           293
#define IRQ_ISP_IRQ_1                           294
#define IRQ_ISP_IRQ_2                           295
#define IRQ_ISP_IRQ_3                           296
#define IRQ_ISP_IRQ_4                           297
#define IRQ_ISP_IRQ_5                           298
#define IRQ_ISP_IRQ_6                           299
#define IRQ_ISP_IRQ_7                           300
#define IRQ_ISP_A7_TO_GIC_MBX_INT_0             301
#define IRQ_ISP_A7_TO_GIC_MBX_INT_1             302
#define IRQ_ISP_A7_TO_GIC_IPC_INT               303
#define IRQ_ISP_A7_WATCHDOG_INT                 304
#define IRQ_ISP_AXI_DLCOK                       305
#define IRQ_ISP_A7_IRQ_OUT                      306
#define IRQ_IVP32_DWAXI_DLOCK_IRQ               307
#define IRQ_MMBUF_ASC0                          308
#define IRQ_MMBUF_ASC1                          309
#define IRQ_UFS                                 310
#define IRQ_PCIE_LINK_DOWN_INT                  311
#define IRQ_PCIE_EDMA_INT                       312
#define IRQ_PCIE_PM_INT                         313
#define IRQ_PCIE_RADM_INTA                      314
#define IRQ_PCIE_RADM_INTB                      315
#define IRQ_PCIE_RADM_INTC                      316
#define IRQ_PCIE_RADM_INTD                      317
#define IRQ_PSAM_INTR_0                         318
#define IRQ_PSAM_INTR_1                         319
#define IRQ_OCBC_PE_NPINT_0                     320
#define IRQ_INTR_WDOG_OCBC                      321
#define IRQ_INTR_VDEC_MFDE_NORM                 322
#define IRQ_INTR_VDEC_SCD_NORM                  323
#define IRQ_INTR_VDEC_BPD_NORM                  324
#define IRQ_INTR_VDEC_MMU_NORM                  325
#define IRQ_INTR_VDEC_MFDE_SAFE                 326
#define IRQ_INTR_VDEC_SCD_SAFE                  327
#define IRQ_INTR_VDEC_BPD_SAFE                  328
#define IRQ_INTR_VDEC_MMU_SAFE                  329
#define IRQ_INTR_VENC_VEDU_NORM                 330
#define IRQ_INTR_VENC_MMU_NORM                  331
#define IRQ_INTR_VENC_VEDU_SAFE                 332
#define IRQ_INTR_VENC_MMU_SAFE                  333
#define IRQ_INTR_QOSBUF0                        334
#define IRQ_INTR_QOSBUF1                        335
#define IRQ_INTR_DDRC2_ERR                      336
#define IRQ_INTR_DDRC3_ERR                      337
#define IRQ_INTR_DDRPHY_0                       338
#define IRQ_INTR_DDRPHY_1                       339
#define IRQ_INTR_DDRPHY_2                       340
#define IRQ_INTR_DDRPHY_3                       341
#define IRQ_INTR0_MDM_IPC_GIC_S                 342
#define IRQ_INTR1_MDM_IPC_GIC_S                 343
#define IRQ_SPI3                                344
#define IRQ_SPI4                                345
#define IRQ_I2C7                                346
#define IRQ_INTR_UCE0_WDOG                      347
#define IRQ_INTR_UCE1_WDOG                      348
#define IRQ_INTR_UCE2_WDOG                      349
#define IRQ_INTR_UCE3_WDOG                      350
#define IRQ_INTR_EXMBIST                        351
#define IRQ_INTR_HISEE_WDOG                     352
#define IRQ_INTR_HISEE_IPC_MBX_GIC_0            353
#define IRQ_INTR_HISEE_IPC_MBX_GIC_1            354
#define IRQ_INTR_HISEE_IPC_MBX_GIC_2            355
#define IRQ_INTR_HISEE_IPC_MBX_GIC_3            356
#define IRQ_INTR_HISEE_IPC_MBX_GIC_4            357
#define IRQ_INTR_HISEE_IPC_MBX_GIC_5            358
#define IRQ_INTR_HISEE_IPC_MBX_GIC_6            359
#define IRQ_INTR_HISEE_IPC_MBX_GIC_7            360
#define IRQ_INTR_HISEE_ALARM_0                  361
#define IRQ_INTR_HISEE_ALARM_1                  362
#define IRQ_INTR_HISEE_EH2H_SLV                 365
#define IRQ_INTR_HISEE_AS2AP_IRQ                366
#define IRQ_INTR_HISEE_DS2AP_IRQ                367
#define IRQ_INTR_HISEE_SENC2AP_IRQ              368
#define IRQ_GPIO0_EMMC                          369
#define IRQ_GPIO1_EMMC                          370
#define IRQ_AONOC_TIMEOUT                       371
#define IRQ_INTR_HISEE_TSENSOR_0                372
#define IRQ_INTR_HISEE_TSENSOR_1                373
#define IRQ_INTR_HISEE_LOCKUP                   374
#define IRQ_INTR_HISEE_DMA                      375

// I2C ports
enum {
    DW_I2C_0,
    DW_I2C_1,
    DW_I2C_2,
    DW_I2C_COUNT,
};

typedef enum hisi_3660_sep_gate_clk_idx {
	HI3660_PERI_VOLT_HOLD = 0,
	HI3660_HCLK_GATE_SDIO0,
	HI3660_HCLK_GATE_SD,
	HI3660_CLK_GATE_AOMM,
	HI3660_PCLK_GPIO0,
	HI3660_PCLK_GPIO1,
	HI3660_PCLK_GPIO2,
	HI3660_PCLK_GPIO3,
	HI3660_PCLK_GPIO4,
	HI3660_PCLK_GPIO5,
	HI3660_PCLK_GPIO6,
	HI3660_PCLK_GPIO7,
	HI3660_PCLK_GPIO8,
	HI3660_PCLK_GPIO9,
	HI3660_PCLK_GPIO10,
	HI3660_PCLK_GPIO11,
	HI3660_PCLK_GPIO12,
	HI3660_PCLK_GPIO13,
	HI3660_PCLK_GPIO14,
	HI3660_PCLK_GPIO15,
	HI3660_PCLK_GPIO16,
	HI3660_PCLK_GPIO17,
	HI3660_PCLK_GPIO18,
	HI3660_PCLK_GPIO19,
	HI3660_PCLK_GPIO20,
	HI3660_PCLK_GPIO21,
	HI3660_CLK_GATE_SPI3,
	HI3660_CLK_GATE_I2C7,
	HI3660_CLK_GATE_I2C3,
	HI3660_CLK_GATE_SPI1,
	HI3660_CLK_GATE_UART1,
	HI3660_CLK_GATE_UART2,
	HI3660_CLK_GATE_UART4,
	HI3660_CLK_GATE_UART5,
	HI3660_CLK_GATE_I2C4,
	HI3660_CLK_GATE_DMAC,
	HI3660_CLK_GATE_VENC,
	HI3660_CLK_GATE_VDEC,
	HI3660_PCLK_GATE_DSS,
	HI3660_ACLK_GATE_DSS,
	HI3660_CLK_GATE_LDI1,
	HI3660_CLK_GATE_LDI0,
	HI3660_CLK_GATE_VIVOBUS,
	HI3660_CLK_GATE_EDC0,
	HI3660_CLK_GATE_TXDPHY0_CFG,
	HI3660_CLK_GATE_TXDPHY0_REF,
	HI3660_CLK_GATE_TXDPHY1_CFG,
	HI3660_CLK_GATE_TXDPHY1_REF,
	HI3660_ACLK_GATE_USB3OTG,
	HI3660_CLK_GATE_SPI4,
	HI3660_CLK_GATE_SD,
	HI3660_CLK_GATE_SDIO0,
	HI3660_CLK_GATE_ISP_SNCLK0,
	HI3660_CLK_GATE_ISP_SNCLK1,
	HI3660_CLK_GATE_ISP_SNCLK2,
	HI3660_CLK_GATE_UFS_SUBSYS,
	HI3660_PCLK_GATE_DSI0,
	HI3660_PCLK_GATE_DSI1,
	HI3660_ACLK_GATE_PCIE,
	HI3660_PCLK_GATE_PCIE_SYS,
	HI3660_CLK_GATE_PCIEAUX,
	HI3660_PCLK_GATE_PCIE_PHY,

	HI3660_PCLK_MMBUF_ANDGT,
	HI3660_CLK_MMBUF_PLL_ANDGT,
	HI3660_CLK_FLL_MMBUF_ANDGT,
	HI3660_CLK_SYS_MMBUF_ANDGT,
	HI3660_CLK_GATE_PCIEPHY_GT,

	// Must be last.
	HI3660_SEP_CLK_GATE_COUNT,
} hisi_3660_sep_gate_clk_idx_t;