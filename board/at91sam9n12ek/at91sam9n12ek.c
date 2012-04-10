/* ----------------------------------------------------------------------------
 *         ATMEL Microcontroller Software Support
 * ----------------------------------------------------------------------------
 * Copyright (c) 2008, Atmel Corporation

 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * - Redistributions of source code must retain the above copyright notice,
 * this list of conditions and the disclaimer below.
 *
 * Atmel's name may not be used to endorse or promote products derived from
 * this software without specific prior written permission.
 *
 * DISCLAIMER: THIS SOFTWARE IS PROVIDED BY ATMEL "AS IS" AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NON-INFRINGEMENT ARE
 * DISCLAIMED. IN NO EVENT SHALL ATMEL BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA,
 * OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
 * LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
 * NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE,
 * EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 * ----------------------------------------------------------------------------
 * File Name           : at91sam9n12ek.c
 * Object              :
 * Creation            :
 *-----------------------------------------------------------------------------
 */
#include "common.h"
#include "hardware.h"
#include "at91sam9n12ek.h"
#include "arch/at91_matrix.h"
#include "arch/at91_wdt.h"
#include "arch/at91_rstc.h"
#include "arch/at91_pmc.h"
#include "arch/at91_slowclk.h"
#include "arch/at91_smc.h"
#include "arch/at91_pio.h"
#include "gpio.h"
#include "pmc.h"
#include "dbgu.h"
#include "debug.h"
#include "ddramc.h"

#ifdef CONFIG_NANDFLASH
#include "nandflash.h"
#endif

#include "onewire_info.h"

extern int get_cp15(void);
extern void set_cp15(unsigned int value);

#ifdef CONFIG_SCLK
static void slow_clk_enable(void)
{
	writel(readl(AT91C_SYS_SLCKSEL) | AT91C_SLCKSEL_OSC32EN, AT91C_SYS_SLCKSEL);
	/* must wait for slow clock startup time ~ 1000ms
	 * (~6 core cycles per iteration, core is at 400MHz: 66666000 min loops) */
	delay(66700000);

	writel(readl(AT91C_SYS_SLCKSEL) | AT91C_SLCKSEL_OSCSEL, AT91C_SYS_SLCKSEL);
	/* must wait 5 slow clock cycles = ~153 us
	 * (~6 core cycles per iteration, core is at 400MHz: min 10200 loops) */
	delay(10200);

	/* now disable the internal RC oscillator */
	writel(readl(AT91C_SYS_SLCKSEL) & ~AT91C_SLCKSEL_RCEN, AT91C_SYS_SLCKSEL);
}
#endif /* CONFIG_SCLK */

#ifdef CONFIG_HW_INIT
void hw_init(void)
{
	unsigned int cp15;

	/* Configure DBGU pin */
	const struct pio_desc dbgu_pins[] = {
#ifdef CONFIG_DEBUG
		{"RXD", AT91C_PIN_PA(9), 0, PIO_DEFAULT, PIO_PERIPH_A},
		{"TXD", AT91C_PIN_PA(10), 0, PIO_DEFAULT, PIO_PERIPH_A},
#endif
		{(char *)0, 0, 0, PIO_DEFAULT, PIO_PERIPH_A},
	};

	/* Disable watchdog */
	writel(AT91C_WDTC_WDDIS, AT91C_BASE_WDT + WDTC_MR);

	/* At this stage the main oscillator is supposed to be enabled PCK = MCK = MOSC */
	writel(0x00, AT91C_BASE_PMC + PMC_PLLICPR);

	/* Configure PLLA = MOSC * (PLL_MULA + 1) / PLL_DIVA */
	pmc_cfg_plla(PLLA_SETTINGS, PLL_LOCK_TIMEOUT);

	/* PCK = PLLA/2 = 3 * MCK */
	pmc_cfg_mck(BOARD_PRESCALER_MAIN_CLOCK, PLL_LOCK_TIMEOUT);

	/* Switch MCK on PLLA output */
	pmc_cfg_mck(BOARD_PRESCALER_PLLA, PLL_LOCK_TIMEOUT);

	/* Enable External Reset */
	writel(((0xA5 << 24) | AT91C_RSTC_URSTEN), AT91C_BASE_RSTC + RSTC_RMR);

	/* Configure CP15 */
	cp15 = get_cp15();
	cp15 |= I_CACHE;
	set_cp15(cp15);

#ifdef CONFIG_SCLK
	slow_clk_enable();
#endif
	/*  Configure the dbgu pins */
	writel((1 << AT91C_ID_PIOA_B), (PMC_PCER + AT91C_BASE_PMC));
	pio_configure(dbgu_pins);

#ifdef CONFIG_DEBUG
	/* Initialize dbgu */
	dbgu_init(BAUDRATE(MASTER_CLOCK, 115200));
#endif

#ifdef CONFIG_DDR2
	/* Initialize DDRAM Controller */
	ddramc_init();
#endif
}
#endif /* CONFIG_HW_INIT */

#if 0
#ifdef CONFIG_DDR2
static SDdramConfig ddram_config;

/*------------------------------------------------------------------------------*/
/* \fn    ddramc_hw_init							*/
/* \brief This function performs DDRAMC HW initialization			*/
/*										*/
/* Using the Micron MT47H64M16HR-3										*/
/*------------------------------------------------------------------------------*/
void ddramc_hw_init(void)
{
	unsigned long csa;

	ddram_config.ddramc_mdr =
	    (AT91C_DDRC2_DBW_16_BITS | AT91C_DDRC2_MD_DDR2_SDRAM);

	ddram_config.ddramc_cr = (AT91C_DDRC2_NC_DDR10_SDR9 |	// 10 column bits (1K)
				  AT91C_DDRC2_NR_13 |	// 13 row bits    (8K)
				  AT91C_DDRC2_CAS_3 |	// CAS Latency 3
				  AT91C_DDRC2_NB_BANKS_8 |	// 8 banks
				  AT91C_DDRC2_DLL_RESET_DISABLED |	// DLL not reset
				  AT91C_DDRC2_DECOD_INTERLEAVED);	// Interleaved decoding

	/* The DDR2-SDRAM device requires a refresh every 15.625 us or 7.81 us.
	 * With a 133 MHz frequency, the refresh timer count register must to be
	 * set with (15.625 x 133 MHz) ~ 2084 i.e. 0x824
	 * or (7.81 x 133 MHz) ~ 1040 i.e. 0x410.
	 */
	ddram_config.ddramc_rtr = 0x411;	/* Refresh timer: 7.8125us */

	/* One clock cycle @ 133 MHz = 7.5 ns */
	ddram_config.ddramc_t0pr = (AT91C_DDRC2_TRAS_6 |	//  6 * 7.5 = 45   ns
				    AT91C_DDRC2_TRCD_2 |	//  2 * 7.5 = 22.5 ns
				    AT91C_DDRC2_TWR_2 |	//  2 * 7.5 = 15   ns
				    AT91C_DDRC2_TRC_8 |	//  8 * 7.5 = 75   ns
				    AT91C_DDRC2_TRP_2 |	//  2 * 7.5 = 15   ns
				    AT91C_DDRC2_TRRD_2 |	//  2 * 7.5 = 15   ns (x16 memory)
				    AT91C_DDRC2_TWTR_2 |	//  2 clock cycles min
				    AT91C_DDRC2_TMRD_2);	//  2 clock cycles

	ddram_config.ddramc_t1pr = (AT91C_DDRC2_TXP_2 |	//  2 clock cycles
				    200 << 16 |	//  200 clock cycles
				    19 << 8 |	//  19 * 7.5 = 142.5 ns ( > 128 + 10 ns)
				    AT91C_DDRC2_TRFC_18);	//  18 * 7.5 = 135   ns (must be 128 ns for 1Gb DDR)

	ddram_config.ddramc_t2pr = (AT91C_DDRC2_TRTP_2 |	//  2 clock cycles min
				    AT91C_DDRC2_TRPA_3 |	//  3 * 7.5 = 22.5 ns
				    AT91C_DDRC2_TXARDS_7 |	//  7 clock cycles
				    AT91C_DDRC2_TXARD_2);	//  2 clock cycles

	// ENABLE DDR2 clock 
	writel(AT91C_PMC_DDR, AT91C_BASE_PMC + PMC_SCER);

	/*
	 * Chip select 1 is for DDR2/SDRAM
	 */
	csa = readl(AT91C_BASE_CCFG + CCFG_EBICSA);
	csa |= AT91C_EBI_CS1A_SDRAMC;
	csa &= ~AT91C_EBI_DBPUC;
	csa |= AT91C_EBI_DBPDC;
	csa |= AT91C_EBI_DRV_HD;

	writel(csa, AT91C_BASE_CCFG + CCFG_EBICSA);

	/*
	 * DDRAM2 Controller
	 */
	ddram_init(AT91C_BASE_DDR2C, AT91C_EBI_CS1, &ddram_config);
}
#endif /* CONFIG_DDR2 */
#endif

#ifdef CONFIG_DATAFLASH
/*---------------------------------------------------------------------------*/
/* \fn    df_hw_init                                                         */
/* \brief This function performs DataFlash HW initialization                 */
/*---------------------------------------------------------------------------*/
void df_hw_init(void)
{
	/*
	 * Configure PIOs for SPI0
	 */
	const struct pio_desc df_pio[] = {
		{"MISO", AT91C_PIN_PA(11), 0, PIO_DEFAULT, PIO_PERIPH_A},
		{"MOSI", AT91C_PIN_PA(12), 0, PIO_DEFAULT, PIO_PERIPH_A},
		{"SPCK", AT91C_PIN_PA(13), 0, PIO_DEFAULT, PIO_PERIPH_A},
		{"NPCS0", AT91C_PIN_PA(14), 1, PIO_DEFAULT, PIO_OUTPUT},	/* Using GPIO as cs pin, set 1 is disable */
		{(char *)0, 0, 0, PIO_DEFAULT, PIO_PERIPH_A},
	};

	/*
	 * Configure the PIO controller
	 */
	writel((1 << AT91C_ID_PIOA_B), (PMC_PCER + AT91C_BASE_PMC));
	pio_setup(df_pio);
}

void df_cs_active(int cs)
{
	switch (cs) {
	case 0:
		pio_set_value(AT91C_PIN_PA(14), 0);
		break;
	case 1:
		pio_set_value(AT91C_PIN_PA(7), 0);
		break;
	}
}

void df_cs_deactive(int cs)
{
	switch (cs) {
	case 0:
		pio_set_value(AT91C_PIN_PA(14), 1);
		break;
	case 1:
		pio_set_value(AT91C_PIN_PA(7), 1);
		break;
	}
}

#endif /* CONFIG_DATAFLASH */

#ifdef CONFIG_NANDFLASH
void nandflash_hw_init(void)
{
	unsigned int reg;

	/* Configure nand pins */
#if 0
	const struct pio_desc nand_pins_hi[] = {
		{"NANDOE", AT91C_PIN_PD(0), 0, PIO_PULLUP, PIO_PERIPH_A},
		{"NANDWE", AT91C_PIN_PD(1), 0, PIO_PULLUP, PIO_PERIPH_A},
		{"NANDALE", AT91C_PIN_PD(2), 0, PIO_PULLUP, PIO_PERIPH_A},
		{"NANDCLE", AT91C_PIN_PD(3), 0, PIO_PULLUP, PIO_PERIPH_A},
		{"NANDCS", AT91C_PIN_PD(4), 0, PIO_PULLUP, PIO_OUTPUT},
		{"RDY_BSY", AT91C_PIN_PD(5), 0, PIO_PULLUP, PIO_INPUT},
		{"D0", AT91C_PIN_PD(6), 0, PIO_PULLUP, PIO_PERIPH_A},
		{"D1", AT91C_PIN_PD(7), 0, PIO_PULLUP, PIO_PERIPH_A},
		{"D2", AT91C_PIN_PD(8), 0, PIO_PULLUP, PIO_PERIPH_A},
		{"D3", AT91C_PIN_PD(9), 0, PIO_PULLUP, PIO_PERIPH_A},
		{"D4", AT91C_PIN_PD(10), 0, PIO_PULLUP, PIO_PERIPH_A},
		{"D5", AT91C_PIN_PD(11), 0, PIO_PULLUP, PIO_PERIPH_A},
		{"D6", AT91C_PIN_PD(12), 0, PIO_PULLUP, PIO_PERIPH_A},
		{"D7", AT91C_PIN_PD(13), 0, PIO_PULLUP, PIO_PERIPH_A},
		{(char *)0, 0, 0, PIO_DEFAULT, PIO_PERIPH_A},
	};
#endif
	const struct pio_desc nand_pins_lo[] = {
		{"NANDOE", AT91C_PIN_PD(0), 0, PIO_PULLUP, PIO_PERIPH_A},
		{"NANDWE", AT91C_PIN_PD(1), 0, PIO_PULLUP, PIO_PERIPH_A},
		{"NANDALE", AT91C_PIN_PD(2), 0, PIO_PULLUP, PIO_PERIPH_A},
		{"NANDCLE", AT91C_PIN_PD(3), 0, PIO_PULLUP, PIO_PERIPH_A},
		{"NANDCS", AT91C_PIN_PD(4), 0, PIO_PULLUP, PIO_OUTPUT},
		{"RDY_BSY", AT91C_PIN_PD(5), 0, PIO_PULLUP, PIO_INPUT},
		{(char *)0, 0, 0, PIO_DEFAULT, PIO_PERIPH_A},
	};

	reg = readl(AT91C_BASE_CCFG + CCFG_EBICSA);
	reg |= AT91C_EBI_CS3A_SM;

	reg &= ~AT91C_EBI_NFD0_ON_D16;	/* nandflash connect to D0~D15 */

	reg |= AT91C_EBI_DRV;	/* according to IAR verification package */
	writel(reg, AT91C_BASE_CCFG + CCFG_EBICSA);

	/* Configure SMC CS3 */
	writel((AT91C_SMC_NWESETUP_(1)
		| AT91C_SMC_NCS_WRSETUP_(0)
		| AT91C_SMC_NRDSETUP_(2) 
		| AT91C_SMC_NCS_RDSETUP_(0)), 
		AT91C_BASE_SMC + SMC_SETUP3);

	writel((AT91C_SMC_NWEPULSE_(3)
		| AT91C_SMC_NCS_WRPULSE_(5) 
		| AT91C_SMC_NRDPULSE_(4) 
		| AT91C_SMC_NCS_RDPULSE_(6)), 
		AT91C_BASE_SMC + SMC_PULSE3);

	writel((AT91C_SMC_NWECYCLE_(5)
		|  AT91C_SMC_NRDCYCLE_(7)),
		AT91C_BASE_SMC + SMC_CYCLE3);

	writel((AT91C_SMC_READMODE 
		| AT91C_SMC_WRITEMODE 
		| AT91C_SMC_NWAITM_NWAIT_DISABLE 
		| AT91C_SMC_DBW_WIDTH_BITS_8
		| AT91_SMC_TDF_(1)), 
		AT91C_BASE_SMC + SMC_CTRL3);

	/* Configure the nand controller pins*/
	writel((1 << AT91C_ID_PIOC_D), (PMC_PCER + AT91C_BASE_PMC));
	pio_configure(nand_pins_lo);
}

void NAND_DISABLE_CE(void)
{
	pio_set_value(AT91C_PIN_PD(4), 1);
}

void nandflash_config_buswidth(unsigned char busw)
{
	unsigned long csa;

	csa = readl(AT91C_BASE_SMC + SMC_CTRL3);

	if (busw == 0)
		csa |= AT91C_SMC_DBW_WIDTH_BITS_8;
	else
		csa |= AT91C_SMC_DBW_WIDTH_BITS_16;

	writel(csa, AT91C_BASE_SMC + SMC_CTRL3);
}

#endif /* #ifdef CONFIG_NANDFLASH */
