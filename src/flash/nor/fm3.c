// SPDX-License-Identifier: GPL-2.0-or-later

/***************************************************************************
 *   Copyright (C) 2011 by Marc Willam, Holger Wech                        *
 *       openOCD.fseu(AT)de.fujitsu.com                                    *
 *   Copyright (C) 2011 Ronny Strutz                                       *
 *                                                                         *
 *   Copyright (C) 2013 Nemui Trinomius                                    *
 *   nemuisan_kawausogasuki@live.jp                                        *
 ***************************************************************************/

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "imp.h"
#include <helper/binarybuffer.h>
#include <target/algorithm.h>
#include <target/armv7m.h>
#include <target/cortex_m.h>
#include <target/register.h>
#include <helper/time_support.h>
#include <target/arm_adi_v5.h>
#include <target/target_type.h>

#define FLASH_DQ6 0x40		/* Data toggle flag bit (TOGG) position */
#define FLASH_DQ5 0x20		/* Time limit exceeding flag bit (TLOV) position */

enum fm3_variant {
	MB9BFXX1,	/* Flash Type '1' */
	MB9BFXX2,
	MB9BFXX3,
	MB9BFXX4,
	MB9BFXX5,
	MB9BFXX6,
	MB9BFXX7,
	MB9BFXX8,
	MB9BFXX9,	/* "Flash macro type 9-12" parts (e.g. MB9BF324L):
		     * FLASH I/F has no FASZR; status via FSTR bit0,
		     * program cmd AA/55/A0 at 0xAA8/0x554 ports. */

	MB9AFXX1,	/* Flash Type '2' */
	MB9AFXX2,
	MB9AFXX3,
	MB9AFXX4,
	MB9AFXX5,
	MB9AFXX6,
	MB9AFXX7,
	MB9AFXX8,
};

enum fm3_flash_type {
	FM3_NO_FLASH_TYPE = 0,
	FM3_FLASH_TYPE1   = 1,
	FM3_FLASH_TYPE2   = 2,
	FM3_FLASH_TYPE9   = 9
};

struct fm3_flash_bank {
	enum fm3_variant variant;
	enum fm3_flash_type flashtype;
	bool probed;
};

static int fm3_type9_reset(struct target *target);

FLASH_BANK_COMMAND_HANDLER(fm3_flash_bank_command)
{
	struct fm3_flash_bank *fm3_info;

	if (CMD_ARGC < 6)
		return ERROR_COMMAND_SYNTAX_ERROR;

	fm3_info = malloc(sizeof(struct fm3_flash_bank));
	bank->driver_priv = fm3_info;

	/* Flash type '1' */
	if (strcmp(CMD_ARGV[5], "mb9bfxx1.cpu") == 0) {
		fm3_info->variant = MB9BFXX1;
		fm3_info->flashtype = FM3_FLASH_TYPE1;
	} else if (strcmp(CMD_ARGV[5], "mb9bfxx2.cpu") == 0) {
		fm3_info->variant = MB9BFXX2;
		fm3_info->flashtype = FM3_FLASH_TYPE1;
	} else if (strcmp(CMD_ARGV[5], "mb9bfxx3.cpu") == 0) {
		fm3_info->variant = MB9BFXX3;
		fm3_info->flashtype = FM3_FLASH_TYPE1;
	} else if (strcmp(CMD_ARGV[5], "mb9bfxx4.cpu") == 0) {
		fm3_info->variant = MB9BFXX4;
		fm3_info->flashtype = FM3_FLASH_TYPE1;
	} else if (strcmp(CMD_ARGV[5], "mb9bfxx5.cpu") == 0) {
		fm3_info->variant = MB9BFXX5;
		fm3_info->flashtype = FM3_FLASH_TYPE1;
	} else if (strcmp(CMD_ARGV[5], "mb9bfxx6.cpu") == 0) {
		fm3_info->variant = MB9BFXX6;
		fm3_info->flashtype = FM3_FLASH_TYPE1;
	} else if (strcmp(CMD_ARGV[5], "mb9bfxx7.cpu") == 0) {
		fm3_info->variant = MB9BFXX7;
		fm3_info->flashtype = FM3_FLASH_TYPE1;
	} else if (strcmp(CMD_ARGV[5], "mb9bfxx8.cpu") == 0) {
		fm3_info->variant = MB9BFXX8;
		fm3_info->flashtype = FM3_FLASH_TYPE1;
	} else if (strcmp(CMD_ARGV[5], "mb9afxx1.cpu") == 0) {	/* Flash type '2' */
		fm3_info->variant = MB9AFXX1;
		fm3_info->flashtype = FM3_FLASH_TYPE2;
	} else if (strcmp(CMD_ARGV[5], "mb9afxx2.cpu") == 0) {
		fm3_info->variant = MB9AFXX2;
		fm3_info->flashtype = FM3_FLASH_TYPE2;
	} else if (strcmp(CMD_ARGV[5], "mb9afxx3.cpu") == 0) {
		fm3_info->variant = MB9AFXX3;
		fm3_info->flashtype = FM3_FLASH_TYPE2;
	} else if (strcmp(CMD_ARGV[5], "mb9afxx4.cpu") == 0) {
		fm3_info->variant = MB9AFXX4;
		fm3_info->flashtype = FM3_FLASH_TYPE2;
	} else if (strcmp(CMD_ARGV[5], "mb9afxx5.cpu") == 0) {
		fm3_info->variant = MB9AFXX5;
		fm3_info->flashtype = FM3_FLASH_TYPE2;
	} else if (strcmp(CMD_ARGV[5], "mb9afxx6.cpu") == 0) {
		fm3_info->variant = MB9AFXX6;
		fm3_info->flashtype = FM3_FLASH_TYPE2;
	} else if (strcmp(CMD_ARGV[5], "mb9afxx7.cpu") == 0) {
		fm3_info->variant = MB9AFXX7;
		fm3_info->flashtype = FM3_FLASH_TYPE2;
	} else if (strcmp(CMD_ARGV[5], "mb9afxx8.cpu") == 0) {
		fm3_info->variant = MB9AFXX8;
		fm3_info->flashtype = FM3_FLASH_TYPE2;
	} else if (strcmp(CMD_ARGV[5], "mb9bfxx9.cpu") == 0) {
		/* e.g. MB9BF324L: newer FICR/FISR/FSTR macro without FASZR;
		 * geometry overridden for the real TYPE9 SA layout below. */
		fm3_info->variant = MB9BFXX6;
		fm3_info->flashtype = FM3_FLASH_TYPE9;
	}

	/* unknown Flash type */
	else {
		LOG_ERROR("unknown fm3 variant: %s", CMD_ARGV[5]);
		free(fm3_info);
		return ERROR_FLASH_BANK_INVALID;
	}

	fm3_info->probed = false;

	return ERROR_OK;
}


/* FSTR bit1 polling via full-byte reads (flash macro type 9-12).
 * Per flash programming manual 002-04795 (figure 1-6, section 1.3.4):
 * - the first FSTR read after issuing a command is undefined, always
 *   discard one dummy read first,
 * - bit1 HNG latches when the on-chip algorithm exceeds its internal
 *   time limit and RDY then never returns: report it instead of
 *   timing out silently (sector erase is specified up to 2.7 s). */
static int fm3_fstr_wait(struct target *target, int timeout_ms)
{
	int ms = 0;
	uint8_t fstr;
	int retval;

	/* dummy read - see manual caution above */
	retval = target_read_u8(target, 0x40000008, &fstr);
	if (retval != ERROR_OK)
		return retval;

	while (1) {
		/* RDY = bit0 of FSTR @ 0x40000008 (bit-band alias kept as
		 * reference: 0x42000100) */
		retval = target_read_u8(target, 0x40000008, &fstr);
		if (retval != ERROR_OK)
			return retval;
		if (fstr & BIT(1)) {
			LOG_ERROR("FSTR HNG set: flash automatic algorithm "
				  "time limit exceeded (issue read/reset 0xF0)");
			return ERROR_FLASH_OPERATION_FAILED;
		}
		if (fstr & BIT(0))
			return ERROR_OK;
		keep_alive();
		usleep(1000);
		if (++ms > timeout_ms) {
			LOG_ERROR("FSTR poll timed out after %d ms (FSTR=%02" PRIx8 ")",
				  ms, fstr);
			return ERROR_FLASH_OPERATION_FAILED;
		}
	}
}

/* Manual-defined recovery from a HANG state: the Read/Reset command is a
 * single 0xF0 write to any address inside the target ROM's aperture; it
 * returns the macro to read mode and clears HNG. */
static void fm3_type9_read_reset(struct target *target, uint32_t rom_base)
{
	if (target_write_u16(target, rom_base + 0x1000, 0x00F0) == ERROR_OK)
		LOG_INFO("fm3 type9: read/reset command issued to recover macro");
}

/* Data polling algorithm */
static int fm3_busy_wait(struct target *target, uint32_t offset, int timeout_ms)
{
	int retval = ERROR_OK;
	uint8_t state1, state2;
	int ms = 0;

	/* While(1) loop exit via "break" and "return" on error */
	while (1) {
		/* dummy-read - see flash manual */
		retval = target_read_u8(target, offset, &state1);
		if (retval != ERROR_OK)
			return retval;

		/* Data polling 1 */
		retval = target_read_u8(target, offset, &state1);
		if (retval != ERROR_OK)
			return retval;

		/* Data polling 2 */
		retval = target_read_u8(target, offset, &state2);
		if (retval != ERROR_OK)
			return retval;

		/* Flash command finished via polled data equal? */
		if ((state1 & FLASH_DQ6) == (state2 & FLASH_DQ6))
			break;
		/* Timeout Flag? */
		else if (state1 & FLASH_DQ5) {
			/* Retry data polling */

			/* Data polling 1 */
			retval = target_read_u8(target, offset, &state1);
			if (retval != ERROR_OK)
				return retval;

			/* Data polling 2 */
			retval = target_read_u8(target, offset, &state2);
			if (retval != ERROR_OK)
				return retval;

			/* Flash command finished via polled data equal? */
			if ((state1 & FLASH_DQ6) != (state2 & FLASH_DQ6))
				return ERROR_FLASH_OPERATION_FAILED;

			/* finish anyway */
			break;
		}
		usleep(1000);
		++ms;

		/* Polling time exceeded? */
		if (ms > timeout_ms) {
			LOG_ERROR("Polling data reading timed out!");
			return ERROR_FLASH_OPERATION_FAILED;
		}
	}

	if (retval == ERROR_OK)
		LOG_DEBUG("fm3_busy_wait(%" PRIx32 ") needs about %d ms", offset, ms);

	return retval;
}

/* Drive a locked-up/halted FM3 core into a CLEAN debug halt.
 * DHCSR bits: 16 S_HALT, 18 S_LOCKUP. DCRSR register access is
 * dead during lockup; BIT(7)(7) exits it. */
/* Arm a throwaway vector table + wipe pending IRQs right before a stub
 * handoff. A halted-but-still-connected USB peripheral keeps setting
 * NVIC pendings; on resume they vector through the app table at the
 * trashed island -> wild code -> it REWRITES flash under our feet.
 * Every entry points at an immediate bkpt inside our own footprint,
 * so a stray exception halts cleanly where the producer can see it. */
static int fm3_type9_arm_vectors(struct target *target, uint32_t wabase)
{
	struct adiv5_ap *dap = target_to_armv7m(target)->debug_ap;
	uint8_t vtab[128 * 4];
	uint32_t h = (wabase + 128 * 4) | 1u;	/* handler = bkpt after table */
	uint32_t sp = wabase + 128 * 4 + 0x100;
	for (unsigned int v = 0; v < 128; v++) {
		vtab[v * 4 + 0] = (v ? h : sp) & 0xff;
		vtab[v * 4 + 1] = ((v ? h : sp) >> 8) & 0xff;
		vtab[v * 4 + 2] = ((v ? h : sp) >> 16) & 0xff;
		vtab[v * 4 + 3] = ((v ? h : sp) >> 24) & 0xff;
	}
	int retval = target_write_buffer(target, wabase, sizeof(vtab), vtab);
	if (retval == ERROR_OK)
		retval = target_write_u16(target, wabase + 128 * 4, 0xbe01);
	if (retval == ERROR_OK)
		retval = mem_ap_write_atomic_u32(dap, 0xE000ED08 /* VTOR */, wabase);
	if (retval == ERROR_OK) {
		mem_ap_write_atomic_u32(dap, 0xE000E100, 0xFFFFFFFF);	/* ICER0 */
		mem_ap_write_atomic_u32(dap, 0xE000E180, 0xFFFFFFFF);	/* ICER1 */
		mem_ap_write_atomic_u32(dap, 0xE000E300, 0xFFFFFFFF);	/* ICPR0 */
		mem_ap_write_atomic_u32(dap, 0xE000E380, 0xFFFFFFFF);	/* ICPR1 */
		mem_ap_write_atomic_u32(dap, 0xE000EF00, 0xFFFFFFFF);	/* ICABR0 */
		mem_ap_write_atomic_u32(dap, 0xE000E280, 0xFFFFFFFF);	/* ICABR? */
	}
	return retval;
}

static int fm3_type9_force_halt(struct target *target)
{
	struct adiv5_ap *dap = target_to_armv7m(target)->debug_ap;
	unsigned int clean = 0;
	/* trap early vector fetches so a reset-window core can never run */
	mem_ap_write_atomic_u32(dap, DCB_DEMCR,
		DBGKEY | TRCENA | VC_HARDERR | VC_BUSERR | VC_CORERESET);
	for (unsigned int i = 0; i < 400; i++) {
		uint32_t dhcsr;
		keep_alive();
		if (mem_ap_read_atomic_u32(dap, DCB_DHCSR, &dhcsr) != ERROR_OK)
			continue;
		if (!(dhcsr & S_HALT)) {
			/* running: plain debug halt, never clear S_REGRDY
			 * (bit 0) in a request write — it can bounce. */
			mem_ap_write_atomic_u32(dap, DCB_DHCSR,
					 DBGKEY | C_HALT | C_DEBUGEN);
			clean = 0;
			continue;
		}
		if (dhcsr & S_LOCKUP) {
			mem_ap_write_atomic_u32(dap, DCB_DHCSR,
					 DBGKEY | C_HALT | C_DEBUGEN |
					 BIT(7));
			clean = 0;
			continue;
		}
		if (dhcsr & S_SLEEP) {
			/* eject sleep so openocd's start-path can't hand the
			 * core over while it sits in WFI */
			mem_ap_write_atomic_u32(dap, DCB_DHCSR,
					 DBGKEY | C_DEBUGEN);
			for (unsigned int w = 0; w < 30; w++) {
				uint32_t idle;
				mem_ap_read_atomic_u32(dap, 0xE000EDCC, &idle);
			}
			mem_ap_write_atomic_u32(dap, DCB_DHCSR,
					 DBGKEY | C_HALT | C_DEBUGEN);
			clean = 0;
			continue;
		}
		if (++clean < 3)
			continue;
		/* stable clean halt: disarm MPU + fault state */
		mem_ap_write_atomic_u32(dap, 0xE000ED94, 0);	/* MPU_CTRL */
		mem_ap_write_atomic_u32(dap, 0xE000ED2C, 0xFFFFFFFF);	/* HFSR */
		mem_ap_write_atomic_u32(dap, 0xE000ED28, 0xFFFFFFFF);	/* CFSR */
		mem_ap_write_atomic_u32(dap, 0xE000ED30, 0xFFFFFFFF);	/* DFSR */
		mem_ap_write_atomic_u32(dap, 0xE000EF00, 0xFFFFFFFF);	/* ICER */
		mem_ap_write_atomic_u32(dap, 0xE000E280, 0xFFFFFFFF);	/* ICPR */
		return ERROR_OK;
	}
	return ERROR_FAIL;
}

static int fm3_erase(struct flash_bank *bank, unsigned int first,
		unsigned int last)
{
	struct fm3_flash_bank *fm3_info = bank->driver_priv;
	struct target *target = bank->target;
	int retval = ERROR_OK;
	uint32_t u32_dummy_read;
	int odd;
	uint32_t u32_flash_type;
	uint32_t u32_flash_seq_address1;
	uint32_t u32_flash_seq_address2;

	struct working_area *write_algorithm;
	struct reg_param reg_params[3];
	struct armv7m_algorithm armv7m_info;

	u32_flash_type = (uint32_t) fm3_info->flashtype;

	if (u32_flash_type == FM3_FLASH_TYPE9) {
		/* Pure host-side sequence (mirrors vendor bootloader). */
		/* disable HW watchdog: a WDT reset mid-erase corrupts the
		 * target sector (manual caution: "prevent an unexpected reset
		 * like Watchdog Timer from occurring during writing/erasing")
		 * and the core being halt-ed does not stop the counter */
		retval = target_write_u32(target, 0x40011C00, 0x1ACCE551);
		if (retval == ERROR_OK)
			retval = target_write_u32(target, 0x40011C00, 0xE5331AAE);
		if (retval == ERROR_OK)
			retval = target_write_u32(target, 0x40011008, 0x00000000);
		if (retval != ERROR_OK) {
			LOG_ERROR("fm3 type9: watchdog disable failed");
			return retval;
		}
		if (fm3_type9_force_halt(target) != ERROR_OK) {
			LOG_ERROR("fm3 type9: cannot force halt before erase");
			return ERROR_FAIL;
		}
		retval = fm3_type9_reset(target);
		if (retval != ERROR_OK)
			return retval;
		for (unsigned int sector = first; sector <= last; sector++) {
			uint32_t sector_base = bank->base + bank->sectors[sector].offset;
			retval = fm3_fstr_wait(target, 1500);
			if (retval != ERROR_OK) {
				fm3_type9_read_reset(target, bank->base);
				return retval;
			}
			/* sector erase setup */
			retval  = target_write_u16(target, 0x00000AA8, 0x00AA);
			if (retval == ERROR_OK) retval = target_write_u16(target, 0x00000554, 0x0055);
			if (retval == ERROR_OK) retval = target_write_u16(target, 0x00000AA8, 0x0080);
			/* sector erase confirm */
			if (retval == ERROR_OK) retval = target_write_u16(target, 0x00000AA8, 0x00AA);
			if (retval == ERROR_OK) retval = target_write_u16(target, 0x00000554, 0x0055);
			if (retval == ERROR_OK) retval = target_write_u16(target, sector_base, 0x0030);
			if (retval != ERROR_OK) {
				LOG_ERROR("fm3 type9 erase command fault sector %u", sector);
				return retval;
			}
			/* datasheet 002-05652: sector erase typ 1.1 s / max 2.7 s
			 * (large), max 0.9 s (small) */
			retval = fm3_fstr_wait(target, 3000);
			if (retval != ERROR_OK) {
				fm3_type9_read_reset(target, bank->base);
				return retval;
			}
			bank->sectors[sector].is_erased = 1;
		}
		return ERROR_OK;
	}

	if (u32_flash_type == FM3_FLASH_TYPE1) {
		u32_flash_seq_address1 = 0x00001550;
		u32_flash_seq_address2 = 0x00000AA8;
	} else if (u32_flash_type == FM3_FLASH_TYPE2
		|| u32_flash_type == FM3_FLASH_TYPE9) {
		u32_flash_seq_address1 = 0x00000AA8;
		u32_flash_seq_address2 = 0x00000554;
	} else {
		LOG_ERROR("Flash/Device type unknown!");
		return ERROR_FLASH_OPERATION_FAILED;
	}

	if (target->state != TARGET_HALTED) {
		LOG_ERROR("Target not halted");
		return ERROR_TARGET_NOT_HALTED;
	}

	/* RAMCODE used for fm3 Flash sector erase:				   */
	/* R0 keeps Flash Sequence address 1     (u32FlashSeq1)    */
	/* R1 keeps Flash Sequence address 2     (u32FlashSeq2)    */
	/* R2 keeps Flash Offset address         (ofs)			   */
	static const uint8_t fm3_flash_erase_sector_code[] = {
						/*    *(uint16_t*)u32FlashSeq1 = 0xAA; */
		0xAA, 0x24,		/*        MOVS  R4, #0xAA              */
		0x04, 0x80,		/*        STRH  R4, [R0, #0]           */
						/*    *(uint16_t*)u32FlashSeq2 = 0x55; */
		0x55, 0x23,		/*        MOVS  R3, #0x55              */
		0x0B, 0x80,		/*        STRH  R3, [R1, #0]           */
						/*    *(uint16_t*)u32FlashSeq1 = 0x80; */
		0x80, 0x25,		/*        MOVS  R5, #0x80              */
		0x05, 0x80,		/*        STRH  R5, [R0, #0]           */
						/*    *(uint16_t*)u32FlashSeq1 = 0xAA; */
		0x04, 0x80,		/*        STRH  R4, [R0, #0]           */
						/*    *(uint16_t*)u32FlashSeq2 = 0x55; */
		0x0B, 0x80,		/*        STRH  R3, [R1, #0]           */
						/* Sector_Erase Command (0x30)         */
						/*    *(uint16_t*)ofs = 0x30;          */
		0x30, 0x20,		/*        MOVS  R0, #0x30              */
		0x10, 0x80,		/*        STRH  R0, [R2, #0]           */
						/* End Code                            */
		0x00, 0xBE,		/*        BKPT  #0                     */
	};

	LOG_INFO("Fujitsu MB9[A/B]FXXX: Sector Erase ... (%u to %u)", first, last);

	/* disable HW watchdog */
	retval = target_write_u32(target, 0x40011C00, 0x1ACCE551);
	if (retval != ERROR_OK)
		return retval;

	retval = target_write_u32(target, 0x40011C00, 0xE5331AAE);
	if (retval != ERROR_OK)
		return retval;

	retval = target_write_u32(target, 0x40011008, 0x00000000);
	if (retval != ERROR_OK)
		return retval;

	if (fm3_info->flashtype != FM3_FLASH_TYPE9) {
		/* FASZR = 0x01, Enables CPU Programming Mode (16-bit Flash access) */
		retval = target_write_u32(target, 0x40000000, 0x0001);
		if (retval != ERROR_OK)
			return retval;
	}

	if (fm3_info->flashtype != FM3_FLASH_TYPE9) {
		/* dummy read of FASZR */
		retval = target_read_u32(target, 0x40000000, &u32_dummy_read);
		if (retval != ERROR_OK)
			return retval;
	}

	/* allocate working area with flash sector erase code */
	if (target_alloc_working_area(target, sizeof(fm3_flash_erase_sector_code),
			&write_algorithm) != ERROR_OK) {
		LOG_WARNING("no working area available, can't do block memory writes");
		return ERROR_TARGET_RESOURCE_NOT_AVAILABLE;
	}
	retval = target_write_buffer(target, write_algorithm->address,
		sizeof(fm3_flash_erase_sector_code), fm3_flash_erase_sector_code);
	if (retval != ERROR_OK)
		return retval;

	armv7m_info.common_magic = ARMV7M_COMMON_MAGIC;
	armv7m_info.core_mode = ARM_MODE_THREAD;

	init_reg_param(&reg_params[0], "r0", 32, PARAM_OUT); /* u32_flash_seq_address1 */
	init_reg_param(&reg_params[1], "r1", 32, PARAM_OUT); /* u32_flash_seq_address2 */
	init_reg_param(&reg_params[2], "r2", 32, PARAM_OUT); /* offset				*/

	/* write code buffer and use Flash sector erase code within fm3				*/
	for (unsigned int sector = first ; sector <= last ; sector++) {
		uint32_t offset = bank->sectors[sector].offset;

		for (odd = 0; odd < (fm3_info->flashtype == FM3_FLASH_TYPE9 ? 1 : 2) ; odd++) {
			if (odd)
				offset += 4;

			buf_set_u32(reg_params[0].value, 0, 32, u32_flash_seq_address1);
			buf_set_u32(reg_params[1].value, 0, 32, u32_flash_seq_address2);
			buf_set_u32(reg_params[2].value, 0, 32, offset);

			retval = target_run_algorithm(target, 0, NULL, 3, reg_params,
					write_algorithm->address, 0, 100000, &armv7m_info);
			if (retval != ERROR_OK) {
				LOG_ERROR("Error executing flash erase programming algorithm");
				retval = ERROR_FLASH_OPERATION_FAILED;
				return retval;
			}

			if (fm3_info->flashtype == FM3_FLASH_TYPE9)
				retval = fm3_fstr_wait(target, 1500);
			else
				retval = fm3_busy_wait(target, offset, 500);
			if (retval != ERROR_OK)
				return retval;
		}
	}

	target_free_working_area(target, write_algorithm);
	destroy_reg_param(&reg_params[0]);
	destroy_reg_param(&reg_params[1]);
	destroy_reg_param(&reg_params[2]);

	if (fm3_info->flashtype != FM3_FLASH_TYPE9) {
		/* FASZR = 0x02, Enables CPU Run Mode (32-bit Flash access) */
		retval = target_write_u32(target, 0x40000000, 0x0002);
		if (retval != ERROR_OK)
			return retval;

		retval = target_read_u32(target, 0x40000000, &u32_dummy_read); /* dummy read of FASZR */
	}

	return retval;
}

/* Batched word writes through the debug AP: one CSW/TAR per transfer
 * instead of mem_ap_write_u32's re-push per word. Works with the core
 * RUNNING (plain AHB debug accesses). len multiple of 4, both aligned. */
static int t9_bulk_write(struct adiv5_ap *ap, uint32_t addr,
			 const uint8_t *buf, uint32_t len)
{
	struct adiv5_dap *dap = ap->dap;
	int retval = dap_queue_ap_write(ap, MEM_AP_REG_CSW(dap),
			ap->csw_default | CSW_32BIT | CSW_ADDRINC_SINGLE);
	if (retval == ERROR_OK)
		retval = dap_queue_ap_write(ap, MEM_AP_REG_TAR(dap), addr);
	for (uint32_t i = 0; retval == ERROR_OK && i < len; i += 4) {
		uint32_t val = buf[i] | (buf[i + 1] << 8) |
			(buf[i + 2] << 16) | ((uint32_t)buf[i + 3] << 24);
		retval = dap_queue_ap_write(ap, MEM_AP_REG_DRW(dap), val);
	}
	uint32_t dummy;
	if (retval == ERROR_OK)
		retval = dap_queue_dp_read(dap, DP_RDBUFF, &dummy);
	if (retval == ERROR_OK)
		retval = dap_run(dap);
	ap->tar_valid = false;	/* auto-inc moved TAR under the cache */
	/* RDBUFF roundtrip flushes the queue and surfaces wire faults in
	 * retval; content faults are caught by the final flash verify. */
	return retval;
}

/* Reset macro error state before an operation: clear sticky interrupt
 * flags (FICLR) and make sure the macro reports RDY.  Manual 002-04795
 * also defines a Read/Reset command (single 0xF0 write inside the ROM
 * aperture) for escaping a HANG; see fm3_type9_read_reset() - note the
 * vendor-bootloader-derived field experience that a *wrong* command
 * (not 0xF0) can wedge this macro generation until power cycle, so we
 * only ever issue the defined one. */
static int fm3_type9_reset(struct target *target)
{
	int retval = target_write_u16(target, 0x40000028, 0xFFFF); /* FICLR: W1C all */
	if (retval == ERROR_OK)
		retval = fm3_fstr_wait(target, 1000);
	return retval;
}

/* Type 9-12 RAM-island ring-consumer stub.
 *
 * Reassemble with: arm-none-eabi-as -mthumb -o t9_stub.o t9_stub.S
 *                  arm-none-eabi-objcopy -O binary t9_stub.o t9_stub.bin
 * (source kept verbatim below; 94 bytes, bkpt at +0x58).
 *
 * Must run from 0x1FFFC100, the vendor bootloader's own RAM-code
 * island: the 0x20000000 RAM is not executable on these parts, and
 * running from RAM block 0 faults on the flash aperture while the
 * macro is busy.  r0 = dst flash addr, r1 = 8 KB ring base,
 * r3 = ctrl block (u32 wr / u32 rd / u32 stop).  The loop pops one
 * halfword from the ring and runs the vendor per-halfword program
 * sequence (FSTR poll, AA/55/A0, strh); empty ring plus stop flag
 * exits via bkpt.  The host refills the ring through the debug AP
 * WHILE THIS STUB RUNS, so SWD upload overlaps macro programming.
 *
 * 	.syntax unified
 * 	.thumb
 * 	.thumb_func
 * # r0=dst flash, r1=ring base (8KB, mask 0x1FFF), r3=ctrl block
 * # ctrl+0 wr(bytes) ctrl+4 rd(bytes) ctrl+8 stop
 * 	.global t9_ring
 * t9_ring:
 * 	cpsid i
 * 	push {r4-r9,r12, lr}
 * 	movw r7, #0x0100
 * 	movt r7, #0x4200	@ FSTR bit-band alias 0x42000100
 * 	movw r8, #0x0AA8
 * 	movw r9, #0x0554
 * 	movw r12, #0x1FFF
 * loop:
 * 	ldr r4, [r3, #0]	@ wr
 * 	ldr r5, [r3, #4]	@ rd
 * 	cmp r4, r5
 * 	bne have
 * 	ldr r2, [r3, #8]	@ stop
 * 	cmp r2, #0
 * 	bne done
 * 	b loop
 * have:
 * waitf:
 * 	ldrb r6, [r7]	@ dummy: first FSTR read after a command is
 * 			@ undefined (manual 002-04795 fig 1-6)
 * 	ldrb r6, [r7]
 * 	cmp r6, #0
 * 	beq waitf
 * 	movs r6, #0xAA
 * 	strh r6, [r8]
 * 	movs r6, #0x55
 * 	strh r6, [r9]
 * 	movs r6, #0xA0
 * 	strh r6, [r8]
 * 	mov r6, r5
 * 	ands r6, r12
 * 	adds r6, r1
 * 	ldrh r4, [r6]
 * 	strh r4, [r0]
 * 	adds r0, #2
 * 	adds r5, #2
 * 	str r5, [r3, #4]
 * 	b loop
 * done:
 * 	bkpt #0
 * 	pop {r4-r9,r12, pc}
 */
static const uint8_t t9_stub[] = {
	0x72, 0xb6, 0x2d, 0xe9, 0xf0, 0x53, 0x40, 0xf2, 0x00, 0x17, 0xc4, 0xf2,
	0x00, 0x27, 0x40, 0xf6, 0xa8, 0x28, 0x40, 0xf2, 0x54, 0x59, 0x41, 0xf6,
	0xff, 0x7c, 0x1c, 0x68, 0x5d, 0x68, 0xac, 0x42, 0x03, 0xd1, 0x9a, 0x68,
	0x00, 0x2a, 0x17, 0xd1, 0xf7, 0xe7, 0x3e, 0x78, 0x3e, 0x78, 0x00, 0x2e,
	0xfb, 0xd0, 0xaa, 0x26, 0xa8, 0xf8, 0x00, 0x60, 0x55, 0x26, 0xa9, 0xf8,
	0x00, 0x60, 0xa0, 0x26, 0xa8, 0xf8, 0x00, 0x60, 0x2e, 0x46, 0x16, 0xea,
	0x0c, 0x06, 0x76, 0x18, 0x34, 0x88, 0x04, 0x80, 0x02, 0x30, 0x02, 0x35,
	0x5d, 0x60, 0xe0, 0xe7, 0x00, 0xbe, 0xbd, 0xe8, 0xf0, 0x93,
};

/* Force the core into a *verified* halt through the debug AP. A core
 * suspended on a flash-bus transaction (read-while-write in the bank
 * aperture) is invisible to cached openocd state and to a single DHCSR
 * read; C_HALT+timeout-poll is the only trustworthy check. */
/* Force the core into a *verified* halt through the debug AP. A core
 * suspended on a flash-bus transaction (read-while-write in the bank
 * aperture) is invisible to cached openocd state; request halt and
 * poll DHCSR through the AP, letting the AP transaction cost itself
 * pace the loop (no sleeps). */


static int fm3_write_block(struct flash_bank *bank, const uint8_t *buffer,
		uint32_t offset, uint32_t count)
{
	struct fm3_flash_bank *fm3_info = bank->driver_priv;
	struct target *target = bank->target;
	uint32_t buffer_size = 2048;		/* Default minimum value */
	struct working_area *write_algorithm;
	struct working_area *source;
	uint32_t address = bank->base + offset;
	struct reg_param reg_params[6];
	struct armv7m_algorithm armv7m_info;
	int retval = ERROR_OK;
	uint32_t u32_flash_type;
	uint32_t u32_flash_seq_address1;
	uint32_t u32_flash_seq_address2;

	/* Increase buffer_size if needed */
	if (buffer_size < (target->working_area_size / 2))
		buffer_size = (target->working_area_size / 2);

	u32_flash_type = (uint32_t) fm3_info->flashtype;

	if (u32_flash_type == FM3_FLASH_TYPE1) {
		u32_flash_seq_address1 = 0x00001550;
		u32_flash_seq_address2 = 0x00000AA8;
	} else if (u32_flash_type == FM3_FLASH_TYPE2
		|| u32_flash_type == FM3_FLASH_TYPE9) {
		u32_flash_seq_address1 = 0x00000AA8;
		u32_flash_seq_address2 = 0x00000554;
	} else {
		LOG_ERROR("Flash/Device type unknown!");
		return ERROR_FLASH_OPERATION_FAILED;
	}

	/* RAMCODE used for fm3 Flash programming:                 */
	/* R0 keeps source start address         (u32Source)       */
	/* R1 keeps target start address         (u32Target)       */
	/* R2 keeps number of halfwords to write (u32Count)        */
	/* R3 keeps Flash Sequence address 1     (u32FlashSeq1)    */
	/* R4 keeps Flash Sequence address 2     (u32FlashSeq2)    */
	/* R5 returns result value               (u32FlashResult)  */

	static const uint8_t fm3_flash_write_code[] = {
								/*    fm3_FLASH_IF->FASZ &= 0xFFFD;           */
	0x5F, 0xF0, 0x80, 0x45,		/*        MOVS.W   R5, #(fm3_FLASH_IF->FASZ)  */
	0x2D, 0x68,					/*        LDR      R5, [R5]                   */
	0x4F, 0xF6, 0xFD, 0x76,		/*        MOVW     R6, #0xFFFD                */
	0x35, 0x40,					/*        ANDS     R5, R5, R6                 */
	0x5F, 0xF0, 0x80, 0x46,		/*        MOVS.W   R6, #(fm3_FLASH_IF->FASZ)  */
	0x35, 0x60,					/*        STR      R5, [R6]                   */
								/*    fm3_FLASH_IF->FASZ |= 1;                */
	0x5F, 0xF0, 0x80, 0x45,		/*        MOVS.W   R5, #(fm3_FLASH_IF->FASZ)  */
	0x2D, 0x68,					/*        LDR      R5, [R3]                   */
	0x55, 0xF0, 0x01, 0x05,		/*        ORRS.W   R5, R5, #1                 */
	0x5F, 0xF0, 0x80, 0x46,		/*        MOVS.W   R6, #(fm3_FLASH_IF->FASZ)  */
	0x35, 0x60,					/*        STR      R5, [R6]                   */
								/*    u32_dummy_read = fm3_FLASH_IF->FASZ;      */
	0x28, 0x4D,					/*        LDR.N    R5, ??u32_dummy_read         */
	0x5F, 0xF0, 0x80, 0x46,		/*        MOVS.W   R6, #(fm3_FLASH_IF->FASZ)  */
	0x36, 0x68,					/*        LDR      R6, [R6]                   */
	0x2E, 0x60,					/*        STR      R6, [R5]                   */
								/*    u32FlashResult = FLASH_WRITE_NO_RESULT  */
	0x26, 0x4D,					/*        LDR.N    R5, ??u32FlashResult       */
	0x00, 0x26,					/*        MOVS     R6, #0                     */
	0x2E, 0x60,					/*        STR      R6, [R5]                   */
								/*    while ((u32Count > 0 )                  */
								/*      && (u32FlashResult                    */
								/*          == FLASH_WRITE_NO_RESULT))        */
	0x01, 0x2A,					/* L0:    CMP      R2, #1                     */
	0x2C, 0xDB,					/*        BLT.N    L1                         */
	0x24, 0x4D,					/*        LDR.N    R5, ??u32FlashResult       */
	0x2D, 0x68,					/*        LDR      R5, [R5]                   */
	0x00, 0x2D,					/*        CMP      R5, #0                     */
	0x28, 0xD1,					/*        BNE.N    L1                         */
								/*    *u32FlashSeq1 = FLASH_WRITE_1;          */
	0xAA, 0x25,					/*        MOVS     R5, #0xAA                  */
	0x1D, 0x60,					/*        STR      R5, [R3]                   */
								/*    *u32FlashSeq2 = FLASH_WRITE_2;          */
	0x55, 0x25,					/*        MOVS     R5, #0x55                  */
	0x25, 0x60,					/*        STR      R5, [R4]                   */
								/*    *u32FlashSeq1 = FLASH_WRITE_3;          */
	0xA0, 0x25,					/*        MOVS     R5, #0xA0                  */
	0x1D, 0x60,					/*        STRH     R5, [R3]                   */
								/*    *(volatile uint16_t*)u32Target          */
								/*      = *(volatile uint16_t*)u32Source;     */
	0x05, 0x88,					/*        LDRH     R5, [R0]                   */
	0x0D, 0x80,					/*        STRH     R5, [R1]                   */
								/*    while (u32FlashResult                   */
								/*           == FLASH_WRITE_NO_RESTULT)       */
	0x1E, 0x4D,					/* L2:    LDR.N    R5, ??u32FlashResult       */
	0x2D, 0x68,					/*        LDR      R5, [R5]                   */
	0x00, 0x2D,					/*        CMP      R5, #0                     */
	0x11, 0xD1,					/*        BNE.N    L3                         */
								/*    if ((*(volatile uint16_t*)u32Target     */
								/*        & FLASH_DQ5) == FLASH_DQ5)          */
	0x0D, 0x88,					/*        LDRH     R5, [R1]                   */
	0xAD, 0x06,					/*        LSLS     R5, R5, #0x1A              */
	0x02, 0xD5,					/*        BPL.N    L4                         */
								/*    u32FlashResult = FLASH_WRITE_TIMEOUT    */
	0x1A, 0x4D,					/*        LDR.N    R5, ??u32FlashResult       */
	0x02, 0x26,					/*        MOVS     R6, #2                     */
	0x2E, 0x60,					/*        STR      R6, [R5]                   */
								/*    if ((*(volatile uint16_t *)u32Target    */
								/*         & FLASH_DQ7)                       */
								/*        == (*(volatile uint16_t*)u32Source  */
								/*            & FLASH_DQ7))                   */
	0x0D, 0x88,					/* L4:    LDRH     R5, [R1]                   */
	0x15, 0xF0, 0x80, 0x05,		/*        ANDS.W   R5, R5, #0x80              */
	0x06, 0x88,					/*        LDRH     R6, [R0]                   */
	0x16, 0xF0, 0x80, 0x06,		/*        ANDS.W   R6, R6, #0x80              */
	0xB5, 0x42,					/*        CMP      R5, R6                     */
	0xED, 0xD1,					/*        BNE.N    L2                         */
								/*    u32FlashResult = FLASH_WRITE_OKAY       */
	0x15, 0x4D,					/*        LDR.N    R5, ??u32FlashResult       */
	0x01, 0x26,					/*        MOVS     R6, #1                     */
	0x2E, 0x60,					/*        STR      R6, [R5]                   */
	0xE9, 0xE7,					/*        B.N      L2                         */
								/*    if (u32FlashResult                      */
								/*        != FLASH_WRITE_TIMEOUT)             */
	0x13, 0x4D,					/*        LDR.N    R5, ??u32FlashResult       */
	0x2D, 0x68,					/*        LDR      R5, [R5]                   */
	0x02, 0x2D,					/*        CMP      R5, #2                     */
	0x02, 0xD0,					/*        BEQ.N    L5                         */
								/*    u32FlashResult = FLASH_WRITE_NO_RESULT  */
	0x11, 0x4D,					/*        LDR.N    R5, ??u32FlashResult       */
	0x00, 0x26,					/*        MOVS     R6, #0                     */
	0x2E, 0x60,					/*        STR      R6, [R5]                   */
								/*    u32Count--;                             */
	0x52, 0x1E,					/* L5:    SUBS     R2, R2, #1                 */
								/*    u32Source += 2;                         */
	0x80, 0x1C,					/*        ADDS     R0, R0, #2                 */
								/*    u32Target += 2;                         */
	0x89, 0x1C,					/*        ADDS     R1, R1, #2                 */
	0xD0, 0xE7,					/*        B.N      L0                         */
								/*    fm3_FLASH_IF->FASZ &= 0xFFFE;           */
	0x5F, 0xF0, 0x80, 0x45,		/* L1:    MOVS.W   R5, #(fm3_FLASH_IF->FASZ)  */
	0x2D, 0x68,					/*        LDR      R5, [R5]                   */
	0x4F, 0xF6, 0xFE, 0x76,		/*        MOVW     R6, #0xFFFE                */
	0x35, 0x40,					/*        ANDS     R5, R5, R6                 */
	0x5F, 0xF0, 0x80, 0x46,		/*        MOVS.W   R6, #(fm3_FLASH_IF->FASZ)  */
	0x35, 0x60,					/*        STR      R5, [R6]                   */
								/*    fm3_FLASH_IF->FASZ |= 2;                */
	0x5F, 0xF0, 0x80, 0x45,		/*        MOVS.W   R5, #(fm3_FLASH_IF->FASZ)  */
	0x2D, 0x68,					/*        LDR      R5, [R5]                   */
	0x55, 0xF0, 0x02, 0x05,		/*        ORRS.W   R5, R5, #2                 */
	0x5F, 0xF0, 0x80, 0x46,		/*        MOVS.W   R6, #(fm3_FLASH_IF->FASZ)  */
	0x35, 0x60,					/*        STR      R5, [R6]                   */
								/*    u32_dummy_read = fm3_FLASH_IF->FASZ;      */
	0x04, 0x4D,					/*        LDR.N    R5, ??u32_dummy_read         */
	0x5F, 0xF0, 0x80, 0x46,		/*        MOVS.W   R6, #(fm3_FLASH_IF->FASZ)  */
	0x36, 0x68,					/*        LDR      R6, [R6]                   */
	0x2E, 0x60,					/*        STR      R6, [R5]                   */
								/*    copy u32FlashResult to R3 for return    */
								/*      value                                 */
	0xDF, 0xF8, 0x08, 0x50,		/*        LDR.W    R5, ??u32FlashResult       */
	0x2D, 0x68,					/*        LDR      R5, [R5]                   */
								/*    Breakpoint here                         */
	0x00, 0xBE,					/*        BKPT     #0                         */

	/* The following address pointers assume, that the code is running from   */
	/* SRAM basic-address + 8.These address pointers will be patched, if a    */
	/* different start address in RAM is used (e.g. for Flash type 2)!        */
	/* Default SRAM basic-address is 0x20000000.                              */
	0x00, 0x00, 0x00, 0x20,     /* u32_dummy_read address in RAM (0x20000000)   */
	0x04, 0x00, 0x00, 0x20      /* u32FlashResult address in RAM (0x20000004) */
	};

	LOG_INFO("Fujitsu MB9[A/B]FXXX: FLASH Write ...");

	/* disable HW watchdog */
	retval = target_write_u32(target, 0x40011C00, 0x1ACCE551);
	if (retval != ERROR_OK)
		return retval;

	retval = target_write_u32(target, 0x40011C00, 0xE5331AAE);
	if (retval != ERROR_OK)
		return retval;

	retval = target_write_u32(target, 0x40011008, 0x00000000);
	if (retval != ERROR_OK)
		return retval;

	/* check code alignment */
	if (offset & 0x1) {
		LOG_WARNING("offset 0x%" PRIx32 " breaks required 2-byte alignment", offset);
		return ERROR_FLASH_DST_BREAKS_ALIGNMENT;
	}

	if (u32_flash_type == FM3_FLASH_TYPE9) {
		if (fm3_type9_force_halt(target) != ERROR_OK) {
			LOG_ERROR("fm3 type9: cannot force core into verified halt "
				  "(bus-suspended?) — power-cycle required");
			return ERROR_FAIL;
		}
		retval = fm3_type9_reset(target);
		if (retval != ERROR_OK) {
			LOG_ERROR("fm3 type9: macro reset failed");
			return retval;
		}
		/* Pipelined ring program: the on-chip stub consumes an 8 KB ring
		 * while we refill it through the debug AP with the core RUNNING.
		 * Overrun is impossible (refill gated on the live rd pointer);
		 * starvation is rate-impossible (SWD >> ~91 KB/s macro drain). */
		struct armv7m_algorithm armv7m_algo;
		struct reg_param rp[3];
		struct adiv5_ap *dap = target_to_armv7m(target)->debug_ap;
		/* The app's idle loop leaves SLEEPDEEP armed: a RESUME_SLEEP
		 * re-sleeps the core instantly and our preload races it. Kill
		 * deep-sleep and force a plain halt before arming the stub. */
		mem_ap_write_atomic_u32(dap, 0xE000ED10, 0);	/* SCR */
		mem_ap_write_atomic_u32(dap, 0xE000EDF0, 0xA05F0003);	/* C_HALT */

		const uint32_t t9_code = 0x1FFFC100;
		const uint32_t t9_ctrl = 0x1FFFC400;
		const uint32_t t9_ring = 0x1FFFC800;
		const uint32_t t9_size = 8192;
		const uint32_t t9_exit = t9_code + 0x58;
		uint32_t total = count & ~3u;	/* whole words via ring */
		uint32_t base_addr = address, base_done = 0;
		const uint8_t *base_buf = buffer;
		unsigned int cp_restarts = 0;
		uint32_t wr = 0, rd = 0, rd_seen = 0xFFFFFFFF;
t9_producer_start:
		wr = 0; rd = 0; rd_seen = 0xFFFFFFFF;
		address = base_addr + base_done;
		buffer = base_buf + base_done;
		total = (count - base_done) & ~3u;
		(void)wr; (void)rd;
		int64_t t_noprog = timeval_ms();
		int64_t t_start;
		bool started = false;

		memset(&armv7m_algo, 0, sizeof(armv7m_algo));
		armv7m_algo.common_magic = ARMV7M_COMMON_MAGIC;
		armv7m_algo.core_mode = ARM_MODE_THREAD;

		static const uint8_t zeros[12];	/* ctrl: wr rd stop */
		retval = target_write_buffer(target, t9_ctrl, sizeof(zeros), zeros);
		/* BKPT during the algorithm lands in the VTOR table armed
		 * by start_algorithm at the work area: b . + bkpt. Ensure
		 * the island's reset-vector slot can't trap us: write a
		 * plain bx lr sentinel trampoline at 0x1FFFC000 (harmless
		 * dead word for the stub flow, rescue for strays). */
		if (retval == ERROR_OK)
			retval = target_write_u16(target, 0x1FFFC000, 0x4770);
		if (retval == ERROR_OK)
			retval = target_write_buffer(target, t9_code, sizeof(t9_stub), t9_stub);
		if (retval == ERROR_OK && total > 0) {
			wr = total < t9_size ? total : t9_size;
			retval = target_write_buffer(target, t9_ring, wr, buffer);
		}
		if (retval == ERROR_OK)
			retval = target_write_u32(target, t9_ctrl, wr);
		if (retval != ERROR_OK) {
			LOG_ERROR("fm3 type9: pipeline preload failed");
			return retval;
		}

		init_reg_param(&rp[0], "r0", 32, PARAM_OUT);
		init_reg_param(&rp[1], "r1", 32, PARAM_OUT);
		init_reg_param(&rp[2], "r3", 32, PARAM_OUT);
		buf_set_u32(rp[0].value, 0, 32, address);
		buf_set_u32(rp[1].value, 0, 32, t9_ring);
		buf_set_u32(rp[2].value, 0, 32, t9_ctrl);

		/* OpenOCD's start_algorithm no longer arms any vector
		 * table; the app's VTOR (island @0x1FFFC000) is trashed by
		 * our preload, so any fault/bkpt-driven fetch would lock us
		 * up. Arm a throwaway table in RAM1 (data-accessible, not
		 * executable): every exception = immediate bkpt, which
		 * halts the core for our watchdog to report. */
		{
			const uint32_t t9_vectors = 0x20002000;
			uint8_t vtab[64 * 4];
			for (unsigned int v = 0; v < 64; v++) {
				uint32_t h = (t9_code + 0x62) | 1u;
				vtab[v * 4 + 0] = h & 0xff;
				vtab[v * 4 + 1] = (h >> 8) & 0xff;
				vtab[v * 4 + 2] = (h >> 16) & 0xff;
				vtab[v * 4 + 3] = (h >> 24) & 0xff;
			}
			vtab[0] = 0; vtab[1] = 0; vtab[2] = 0; vtab[3] = 0; /* SP unused path */
			/* handler: bkpt #1 at stub end + 0x62 */
			uint8_t hk[2] = { 0x01, 0xbe };
			retval = target_write_buffer(target, t9_code + 0x62, 2, hk);
			if (retval == ERROR_OK)
				retval = target_write_buffer(target, t9_vectors,
						       sizeof(vtab), vtab);
			if (retval == ERROR_OK)
				retval = mem_ap_write_atomic_u32(dap, 0xE000ED08,
							     t9_vectors);
			if (retval != ERROR_OK) {
				LOG_ERROR("fm3 type9: vector arming failed");
				return retval;
			}
		}
		/* Right before the handoff: make certain the core sits in a
		 * plain debug-halt (clear sleep+sleepdeep, force halt) so the
		 * algorithm resume can't bounce into app code or WFI. */
		mem_ap_write_atomic_u32(dap, 0xE000EDF0, 0xA05F0003);
		mem_ap_write_atomic_u32(dap, 0xE000ED10, 0);	/* SCR: no SLEEPDEEP */
		/* Target may have slept (app WFI) since our entry halt; use
		 * OpenOCD's own halt+wait so start_algorithm sees a plain
		 * halted core and its resume doesn't bounce off SLEEP state. */
		if (fm3_type9_force_halt(target) != ERROR_OK ||
		    target_wait_state(target, TARGET_HALTED, 2000) != ERROR_OK) {
			LOG_ERROR("fm3 type9: pre-start halt wait failed");
			retval = ERROR_FAIL;
			goto t9_err;
		}
		retval = fm3_type9_arm_vectors(target, t9_code + 0x80);
		if (retval != ERROR_OK) {
			LOG_ERROR("fm3 type9: vector arm failed");
			goto t9_err;
		}
		retval = target_start_algorithm(target, 0, NULL, ARRAY_SIZE(rp), rp,
						t9_code | 1, t9_exit, &armv7m_algo);
		if (retval != ERROR_OK) {
			LOG_ERROR("fm3 type9: pipeline stub start failed");
			goto t9_err;
		}
		started = true;
		t_start = timeval_ms();
		while (wr < total) {
			retval = mem_ap_read_atomic_u32(dap, t9_ctrl + 4, &rd);
			if (retval != ERROR_OK)
				goto t9_err;
			if (rd != rd_seen) {
				rd_seen = rd;
				t_noprog = timeval_ms();
			}
			if (wr - rd > t9_size - 2048) {
				/* ring near full; occupancy of monotonic counters.
				 * Liveness from the LIVE DHCSR.S_HALT (cached openocd
				 * state lies after lockup-clear) plus rd progress:
				 * the consumer advances rd every ~12 us, so 5000 AP
				 * reads (~0.5 s) without movement is a real fault. */
				/* Only judge liveness after sustained zero
				 * progress; DHCSR sampled from the running
				 * core is unreliable and spawned phantom
				 * "exited early" reports. */
				if (timeval_ms() - t_noprog > 3000) {
					uint32_t dhcsr = 0;
					if (mem_ap_read_atomic_u32(dap, 0xE000EDF0,
								   &dhcsr) != ERROR_OK) {
						LOG_ERROR("fm3 type9: AP read failed (chip stuck?)");
						retval = ERROR_FLASH_OPERATION_FAILED;
						goto t9_err;
					}
if (!(dhcsr & S_HALT)) {
	/* alive but slow: keep waiting */
	continue;
}
if (cp_restarts < 2) {
					/* Checkpoint restart: rd is the exact
					 * byte offset the consumer finished
					 * programming; re-running from there
					 * only re-programs FF cells. Tear the
					 * algorithm down and relaunch. */
					cp_restarts++;
					LOG_INFO("t9 checkpoint restart at +%u (attempt %u)",
						 rd, cp_restarts);
					base_done += rd;
					if (started) {
						mem_ap_write_atomic_u32(dap, DCB_DHCSR,
							 DBGKEY | C_HALT | C_DEBUGEN);
						target_wait_algorithm(target, 0, NULL, 0,
								  NULL, t9_exit, 1000,
								  &armv7m_algo);
						started = false;
					}
					rd_seen = 0xFFFFFFFF;
					t_start = timeval_ms();
					goto t9_producer_start;
				} else {
						uint32_t macro_fstr = 0xDEAD, macro_fisr = 0xDEAD;
						mem_ap_read_atomic_u32(dap, 0x40000008, &macro_fstr);
						mem_ap_read_atomic_u32(dap, 0x40000024, &macro_fisr);
						LOG_INFO("t9 macro at watchdog: FSTR=%08x FISR=%08x",
							 macro_fstr, macro_fisr);
						uint32_t dfsr = 0, hfsr = 0,
							 cfsr = 0, mmfar = 0, bfar = 0;
						uint32_t pc = 0, lr = 0, sp = 0, xpsr = 0;
						uint32_t regs[4] = { 15, 14, 13, 16 };
						uint32_t *outp[4] = { &pc, &lr, &sp, &xpsr };
						for (unsigned rr = 0; rr < 4; rr++) {
							mem_ap_write_atomic_u32(dap, 0xE000EDF0,
									  (1u << 16) | regs[rr]);
							for (unsigned pp = 0; pp < 50; pp++) {
								uint32_t d, v = 0;
								if (mem_ap_read_atomic_u32(dap, 0xE000EDF0, &d)
								    != ERROR_OK)
									break;
								if (!(d & 1u))
									continue;
								if (mem_ap_read_atomic_u32(dap, 0xE000EDF4, &v)
								    == ERROR_OK) {
									*outp[rr] = v;
									break;
								}
							}
						}
						mem_ap_read_atomic_u32(dap, 0xE000ED30, &dfsr);
						mem_ap_read_atomic_u32(dap, 0xE000ED2C, &hfsr);
						mem_ap_read_atomic_u32(dap, 0xE000ED28, &cfsr);
						mem_ap_read_atomic_u32(dap, 0xE000ED34, &mmfar);
						mem_ap_read_atomic_u32(dap, 0xE000ED38, &bfar);
						uint32_t dhcsr_now = 0;
						mem_ap_read_atomic_u32(dap, 0xE000EDF0,
							     &dhcsr_now);
						LOG_ERROR("fm3 type9: stub exited early (rd=%" PRIu32
							  " of %" PRIu32 ") DHCSR=%08x PC=%08x "
							  "LR=%08x SP=%08x xPSR=%08x HFSR=%08x CFSR=%08x",
							  rd, wr, dhcsr_now, pc, lr, sp, xpsr,
							  hfsr, cfsr);
							uint32_t cw = 0, cr = 0, cs = 0, rh0 = 0, rh1 = 0;
						mem_ap_read_atomic_u32(dap, t9_ctrl + 0, &cw);
						mem_ap_read_atomic_u32(dap, t9_ctrl + 4, &cr);
						mem_ap_read_atomic_u32(dap, t9_ctrl + 8, &cs);
						mem_ap_read_atomic_u32(dap, t9_ring + 0, &rh0);
						mem_ap_read_atomic_u32(dap, t9_ring + 4, &rh1);
						LOG_ERROR("t9 ring state: ctrl wr=%u rd=%u stop=%u ring[0]=%08x ring[4]=%08x (expect wr=8192)",
							  cw, cr, cs, rh0, rh1);
						retval = ERROR_FLASH_OPERATION_FAILED;
						goto t9_err;
					}
				}
				if (timeval_ms() - t_start > 60000) {
					uint32_t fisr = 0, fstr = 0, dh2 = 0, pcv = 0;
					target_read_u32(target, 0x40000024, &fisr);
					target_read_u32(target, 0x40000008, &fstr);
					mem_ap_write_atomic_u32(dap, 0xE000EDF0,
						0xA05F0003);	/* halt to inspect */
					mem_ap_write_atomic_u32(dap, 0xE000EDF0,
						0xA05F0000 | (1u << 16) | 15);
					for (unsigned pp = 0; pp < 40; pp++) {
						if (mem_ap_read_atomic_u32(dap, 0xE000EDF0,
						    &dh2) == ERROR_OK && (dh2 & 1u)) {
							mem_ap_read_atomic_u32(dap, 0xE000EDF4,
								&pcv);
							break;
						}
					}
					LOG_ERROR("fm3 type9: pipeline stalled (wr=%" PRIu32 " rd=%" PRIu32
						  " FSTR=%08" PRIx32 " FISR=%08" PRIx32 " PC=%08x DHCSR=%08x)",
						  wr, rd, fstr, fisr, pcv, dh2);
					target_halt(target);
					retval = ERROR_FLASH_OPERATION_FAILED;
					goto t9_err;
				}
				keep_alive();
				continue;
			}
			uint32_t slot = wr & (t9_size - 1);
			uint32_t seg = 2048;
			if (slot + seg > t9_size)
				seg = t9_size - slot;
			if (seg > total - wr)
				seg = total - wr;
			seg &= ~3u;
			if (seg == 0)
				seg = 4;	/* single word across wrap */
			retval = t9_bulk_write(dap, t9_ring + slot, buffer + wr, seg);
			if (retval != ERROR_OK)
				goto t9_err;
			wr += seg;
			retval = mem_ap_write_atomic_u32(dap, t9_ctrl, wr);
			if (retval != ERROR_OK)
				goto t9_err;
		}

		{
			uint32_t one = 1;
			retval = mem_ap_write_atomic_u32(dap, t9_ctrl + 8, one);
			if (retval != ERROR_OK)
				goto t9_err;
		}
		retval = target_wait_algorithm(target, 0, NULL, ARRAY_SIZE(rp), rp,
					     t9_exit, 30000, &armv7m_algo);
		if (retval != ERROR_OK) {
			LOG_ERROR("fm3 type9: ring stub did not complete cleanly");
			started = false;
			goto t9_err;
		}
		started = false;
		base_done = count;
		destroy_reg_param(&rp[0]);
		destroy_reg_param(&rp[1]);
		destroy_reg_param(&rp[2]);

		/* program any remaining 2-byte halfword directly (host side) */
		for (uint32_t off = total; off < count; off += 2) {
			uint16_t val = buffer[off] | (buffer[off + 1] << 8);
			retval = fm3_fstr_wait(target, 500);
			if (retval == ERROR_OK) retval = target_write_u16(target, 0x00000AA8, 0x00AA);
			if (retval == ERROR_OK) retval = target_write_u16(target, 0x00000554, 0x0055);
			if (retval == ERROR_OK) retval = target_write_u16(target, 0x00000AA8, 0x00A0);
			if (retval == ERROR_OK) retval = target_write_u16(target, address + off, val);
			if (retval != ERROR_OK) {
				LOG_ERROR("fm3 type9: tail halfword failed");
				return retval;
			}
			retval = fm3_fstr_wait(target, 1000);
			if (retval != ERROR_OK)
				return retval;
		}
		/* The island RAM we occupied may have been the app's live
		 * vectors/code; never leave the core running after this. */
		target_halt(target);
		target_write_u32(target, 0xE000ED0C, 0x05FA0004);
		return ERROR_OK;

t9_err:
		if (started) {
			if (target->state == TARGET_RUNNING)
				target_halt(target);
			/* best-effort context restore; session ends with chip reset */
			target_wait_algorithm(target, 0, NULL, 0, NULL, t9_exit, 1000, &armv7m_algo);
		}
		destroy_reg_param(&rp[0]);
		destroy_reg_param(&rp[1]);
		destroy_reg_param(&rp[2]);
		return retval != ERROR_OK ? retval : ERROR_FLASH_OPERATION_FAILED;
	}

	/* legacy FM3 algorithms count halfwords */
	count = count / 2;

	/* allocate working area and variables with flash programming code */
	if (target_alloc_working_area(target, sizeof(fm3_flash_write_code) + 8,
			&write_algorithm) != ERROR_OK) {
		LOG_WARNING("no working area available, can't do block memory writes");
		return ERROR_TARGET_RESOURCE_NOT_AVAILABLE;
	}

	retval = target_write_buffer(target, write_algorithm->address + 8,
		sizeof(fm3_flash_write_code), fm3_flash_write_code);
	if (retval != ERROR_OK)
		return retval;

	/* Patching 'local variable address' */
	/* Algorithm: u32_dummy_read: */
	retval = target_write_u32(target, (write_algorithm->address + 8)
			+ sizeof(fm3_flash_write_code) - 8, (write_algorithm->address));
	if (retval != ERROR_OK)
		return retval;
	/* Algorithm: u32FlashResult: */
	retval = target_write_u32(target, (write_algorithm->address + 8)
			+ sizeof(fm3_flash_write_code) - 4, (write_algorithm->address) + 4);
	if (retval != ERROR_OK)
		return retval;



	/* memory buffer */
	while (target_alloc_working_area(target, buffer_size, &source) != ERROR_OK) {
		buffer_size /= 2;
		if (buffer_size <= 256) {
			/* free working area, write algorithm already allocated */
			target_free_working_area(target, write_algorithm);

			LOG_WARNING("No large enough working area available, can't do block memory writes");
			return ERROR_TARGET_RESOURCE_NOT_AVAILABLE;
		}
	}

	armv7m_info.common_magic = ARMV7M_COMMON_MAGIC;
	armv7m_info.core_mode = ARM_MODE_THREAD;

	init_reg_param(&reg_params[0], "r0", 32, PARAM_OUT); /* source start address */
	init_reg_param(&reg_params[1], "r1", 32, PARAM_OUT); /* target start address */
	init_reg_param(&reg_params[2], "r2", 32, PARAM_OUT); /* number of halfwords to program */
	init_reg_param(&reg_params[3], "r3", 32, PARAM_OUT); /* Flash Sequence address 1 */
	init_reg_param(&reg_params[4], "r4", 32, PARAM_OUT); /* Flash Sequence address 1 */
	init_reg_param(&reg_params[5], "r5", 32, PARAM_IN);  /* result */

	/* write code buffer and use Flash programming code within fm3           */
	/* Set breakpoint to 0 with time-out of 1000 ms                          */
	while (count > 0) {
		uint32_t thisrun_count = (count > (buffer_size / 2)) ? (buffer_size / 2) : count;

		retval = target_write_buffer(target, source->address, thisrun_count * 2, buffer);
		if (retval != ERROR_OK)
			break;

		buf_set_u32(reg_params[0].value, 0, 32, source->address);
		buf_set_u32(reg_params[1].value, 0, 32, address);
		buf_set_u32(reg_params[2].value, 0, 32, thisrun_count);
		buf_set_u32(reg_params[3].value, 0, 32, u32_flash_seq_address1);
		buf_set_u32(reg_params[4].value, 0, 32, u32_flash_seq_address2);

		retval = target_run_algorithm(target, 0, NULL, 6, reg_params,
				(write_algorithm->address + 8), 0, 1000, &armv7m_info);
		if (retval != ERROR_OK) {
			LOG_ERROR("Error executing fm3 Flash programming algorithm");
			retval = ERROR_FLASH_OPERATION_FAILED;
			break;
		}

		if (buf_get_u32(reg_params[5].value, 0, 32) != ERROR_OK) {
			LOG_ERROR("Fujitsu MB9[A/B]FXXX: Flash programming ERROR (Timeout) -> Reg R3: %" PRIx32,
				buf_get_u32(reg_params[5].value, 0, 32));
			retval = ERROR_FLASH_OPERATION_FAILED;
			break;
		}

		buffer  += thisrun_count * 2;
		address += thisrun_count * 2;
		count   -= thisrun_count;
	}

	target_free_working_area(target, source);
	target_free_working_area(target, write_algorithm);

	destroy_reg_param(&reg_params[0]);
	destroy_reg_param(&reg_params[1]);
	destroy_reg_param(&reg_params[2]);
	destroy_reg_param(&reg_params[3]);
	destroy_reg_param(&reg_params[4]);
	destroy_reg_param(&reg_params[5]);

	return retval;
}

static int fm3_probe(struct flash_bank *bank)
{
	struct fm3_flash_bank *fm3_info = bank->driver_priv;
	uint16_t num_pages;

	if (bank->target->state != TARGET_HALTED) {
		LOG_ERROR("Target not halted");
		return ERROR_TARGET_NOT_HALTED;
	}

/*
 -- page-- start -- blocksize - mpu - totalFlash --
	page0 0x00000	16k
	page1 0x04000	16k
	page2 0x08000	96k		___ fxx3  128k Flash
	page3 0x20000  128k		___ fxx4  256k Flash
	page4 0x40000  128k		___ fxx5  384k Flash
	page5 0x60000  128k		___ fxx6  512k Flash
-----------------------
	page6 0x80000  128k
	page7 0xa0000  128k		___ fxx7  256k Flash
	page8 0xc0000  128k
	page9 0xe0000  128k		___ fxx8  256k Flash
 */

	num_pages = 10;				/* max number of Flash pages for malloc */
	fm3_info->probed = false;

	bank->sectors = malloc(sizeof(struct flash_sector) * num_pages);
	bank->base = 0x00000000;
	bank->size = 32 * 1024;		/* bytes */

	bank->sectors[0].offset = 0;
	bank->sectors[0].size = 16 * 1024;
	bank->sectors[0].is_erased = -1;
	bank->sectors[0].is_protected = -1;

	bank->sectors[1].offset = 0x4000;
	bank->sectors[1].size = 16 * 1024;
	bank->sectors[1].is_erased = -1;
	bank->sectors[1].is_protected = -1;

	if ((fm3_info->variant == MB9BFXX1)
	    || (fm3_info->variant == MB9AFXX1)) {
		num_pages = 3;
		bank->size = 64 * 1024; /* bytes */
		bank->num_sectors = num_pages;

		bank->sectors[2].offset = 0x8000;
		bank->sectors[2].size = 32 * 1024;
		bank->sectors[2].is_erased = -1;
		bank->sectors[2].is_protected = -1;
	}

	if ((fm3_info->variant == MB9BFXX2)
		|| (fm3_info->variant == MB9BFXX4)
		|| (fm3_info->variant == MB9BFXX5)
		|| (fm3_info->variant == MB9BFXX6)
		|| (fm3_info->variant == MB9BFXX7)
		|| (fm3_info->variant == MB9BFXX8)
		|| (fm3_info->variant == MB9AFXX2)
		|| (fm3_info->variant == MB9AFXX4)
		|| (fm3_info->variant == MB9AFXX5)
		|| (fm3_info->variant == MB9AFXX6)
		|| (fm3_info->variant == MB9AFXX7)
		|| (fm3_info->variant == MB9AFXX8)) {
		num_pages = 3;
		bank->size = 128 * 1024; /* bytes */
		bank->num_sectors = num_pages;

		bank->sectors[2].offset = 0x8000;
		bank->sectors[2].size = 96 * 1024;
		bank->sectors[2].is_erased = -1;
		bank->sectors[2].is_protected = -1;
	}

	if ((fm3_info->variant == MB9BFXX4)
		|| (fm3_info->variant == MB9BFXX5)
		|| (fm3_info->variant == MB9BFXX6)
		|| (fm3_info->variant == MB9BFXX7)
		|| (fm3_info->variant == MB9BFXX8)
		|| (fm3_info->variant == MB9AFXX4)
		|| (fm3_info->variant == MB9AFXX5)
		|| (fm3_info->variant == MB9AFXX6)
		|| (fm3_info->variant == MB9AFXX7)
		|| (fm3_info->variant == MB9AFXX8)) {
		num_pages = 4;
		bank->size = 256 * 1024; /* bytes */
		bank->num_sectors = num_pages;

		bank->sectors[3].offset = 0x20000;
		bank->sectors[3].size = 128 * 1024;
		bank->sectors[3].is_erased = -1;
		bank->sectors[3].is_protected = -1;
	}

	if ((fm3_info->variant == MB9BFXX5)
		|| (fm3_info->variant == MB9BFXX6)
		|| (fm3_info->variant == MB9BFXX7)
		|| (fm3_info->variant == MB9BFXX8)
		|| (fm3_info->variant == MB9AFXX5)
		|| (fm3_info->variant == MB9AFXX6)
		|| (fm3_info->variant == MB9AFXX7)
		|| (fm3_info->variant == MB9AFXX8)) {
		num_pages = 5;
		bank->size = 384 * 1024; /* bytes */
		bank->num_sectors = num_pages;

		bank->sectors[4].offset = 0x40000;
		bank->sectors[4].size = 128 * 1024;
		bank->sectors[4].is_erased = -1;
		bank->sectors[4].is_protected = -1;
	}

	if ((fm3_info->variant == MB9BFXX6)
		|| (fm3_info->variant == MB9BFXX7)
		|| (fm3_info->variant == MB9BFXX8)
		|| (fm3_info->variant == MB9AFXX6)
		|| (fm3_info->variant == MB9AFXX7)
		|| (fm3_info->variant == MB9AFXX8)) {
		num_pages = 6;
		bank->size = 512 * 1024; /* bytes */
		bank->num_sectors = num_pages;

		bank->sectors[5].offset = 0x60000;
		bank->sectors[5].size = 128 * 1024;
		bank->sectors[5].is_erased = -1;
		bank->sectors[5].is_protected = -1;
	}

	if ((fm3_info->variant == MB9BFXX7)
		|| (fm3_info->variant == MB9BFXX8)
		|| (fm3_info->variant == MB9AFXX7)
		|| (fm3_info->variant == MB9AFXX8)) {
		num_pages = 8;
		bank->size = 768 * 1024; /* bytes */
		bank->num_sectors = num_pages;

		bank->sectors[6].offset = 0x80000;
		bank->sectors[6].size = 128 * 1024;
		bank->sectors[6].is_erased = -1;
		bank->sectors[6].is_protected = -1;

		bank->sectors[7].offset = 0xa0000;
		bank->sectors[7].size = 128 * 1024;
		bank->sectors[7].is_erased = -1;
		bank->sectors[7].is_protected = -1;
	}

	if ((fm3_info->variant == MB9BFXX8)
		|| (fm3_info->variant == MB9AFXX8)) {
		num_pages = 10;
		bank->size = 1024 * 1024; /* bytes */
		bank->num_sectors = num_pages;

		bank->sectors[8].offset = 0xc0000;
		bank->sectors[8].size = 128 * 1024;
		bank->sectors[8].is_erased = -1;
		bank->sectors[8].is_protected = -1;

		bank->sectors[9].offset = 0xe0000;
		bank->sectors[9].size = 128 * 1024;
		bank->sectors[9].is_erased = -1;
		bank->sectors[9].is_protected = -1;
	}

	if (fm3_info->flashtype == FM3_FLASH_TYPE9) {
		/* Real TYPE9 erase-unit map (MB9B320M datasheet 002-05652,
		 * memory map 2): main area = 256K as
		 *   SA2 8K @0x00000, SA3 8K @0x02000, SA8 48K @0x04000,
		 *   SA9 64K @0x10000, SA10 64K @0x20000, SA11 64K @0x30000.
		 * Reads above 0x40000 alias the main area; the 32K "work
		 * area" lives at 0x200000+ behind its own aperture and is
		 * not part of this bank. */
		bank->size = 256 * 1024;
		bank->num_sectors = 6;
		bank->sectors[0].offset = 0x00000;	bank->sectors[0].size =  8 * 1024;
		bank->sectors[1].offset = 0x02000;	bank->sectors[1].size =  8 * 1024;
		bank->sectors[2].offset = 0x04000;	bank->sectors[2].size = 48 * 1024;
		bank->sectors[3].offset = 0x10000;	bank->sectors[3].size = 64 * 1024;
		bank->sectors[4].offset = 0x20000;	bank->sectors[4].size = 64 * 1024;
		bank->sectors[5].offset = 0x30000;	bank->sectors[5].size = 64 * 1024;
		for (unsigned int i = 0; i < 6; i++) {
			bank->sectors[i].is_erased = -1;
			bank->sectors[i].is_protected = 0;
		}
	}

	if (fm3_info->flashtype == FM3_FLASH_TYPE9) {
		/* Real TYPE9 erase-unit map (MB9B320M datasheet 002-05652,
		 * memory map 2): main area = 256K as
		 *   SA2 8K @0x00000, SA3 8K @0x02000, SA8 48K @0x04000,
		 *   SA9 64K @0x10000, SA10 64K @0x20000, SA11 64K @0x30000.
		 * Reads above 0x40000 alias the main area; the 32K "work
		 * area" lives at 0x200000+ behind its own aperture and is
		 * not part of this bank. */
		bank->size = 256 * 1024;
		bank->num_sectors = 6;
		bank->sectors[0].offset = 0x00000;	bank->sectors[0].size =  8 * 1024;
		bank->sectors[1].offset = 0x02000;	bank->sectors[1].size =  8 * 1024;
		bank->sectors[2].offset = 0x04000;	bank->sectors[2].size = 48 * 1024;
		bank->sectors[3].offset = 0x10000;	bank->sectors[3].size = 64 * 1024;
		bank->sectors[4].offset = 0x20000;	bank->sectors[4].size = 64 * 1024;
		bank->sectors[5].offset = 0x30000;	bank->sectors[5].size = 64 * 1024;
		for (unsigned int i = 0; i < 6; i++) {
			bank->sectors[i].is_erased = -1;
			bank->sectors[i].is_protected = 0;
		}
	}

	fm3_info->probed = true;

	return ERROR_OK;
}

static int fm3_auto_probe(struct flash_bank *bank)
{
	struct fm3_flash_bank *fm3_info = bank->driver_priv;
	if (fm3_info->probed)
		return ERROR_OK;
	return fm3_probe(bank);
}

/* Chip erase */
static int fm3_chip_erase(struct flash_bank *bank)
{
	struct target *target = bank->target;
	struct fm3_flash_bank *fm3_info2 = bank->driver_priv;
	int retval = ERROR_OK;
	uint32_t u32_dummy_read;
	uint32_t u32_flash_type;
	uint32_t u32_flash_seq_address1;
	uint32_t u32_flash_seq_address2;

	struct working_area *write_algorithm;
	struct reg_param reg_params[3];
	struct armv7m_algorithm armv7m_info;

	u32_flash_type = (uint32_t) fm3_info2->flashtype;

	if (u32_flash_type == FM3_FLASH_TYPE1) {
		LOG_INFO("*** Erasing mb9bfxxx type");
		u32_flash_seq_address1 = 0x00001550;
		u32_flash_seq_address2 = 0x00000AA8;
	} else if (u32_flash_type == FM3_FLASH_TYPE2
		|| u32_flash_type == FM3_FLASH_TYPE9) {
		LOG_INFO("*** Erasing mb9afxxx type");
		u32_flash_seq_address1 = 0x00000AA8;
		u32_flash_seq_address2 = 0x00000554;
	} else {
		LOG_ERROR("Flash/Device type unknown!");
		return ERROR_FLASH_OPERATION_FAILED;
	}

	if (target->state != TARGET_HALTED) {
		LOG_ERROR("Target not halted");
		return ERROR_TARGET_NOT_HALTED;
	}

	if (u32_flash_type == FM3_FLASH_TYPE9) {
		/* TYPE9 generation (002-04795 Table 1-2): flash erase is the
		 * six-write sequence ending with 0x10 at the port1 aperture;
		 * status via FSTR (legacy data polling & FASZR do not exist
		 * on this macro generation).  Datasheet 002-05652: chip erase
		 * typ 6.8 s / max 18 s.  Manual: flash erase ALSO erases the
		 * security code and the factory CR trimming data, and the
		 * CRTRMM mirror goes blank after the internal reset the
		 * manual's note recommends - save/restore the trim around it
		 * (TRM 002-05586 ch. 2-2: "save the data before erasing"). */
		uint32_t port1 = bank->base + 0xAA8, port2 = bank->base + 0x554;
		uint32_t crtrmm = 0;
		uint16_t sec = 0xFFFF;

		/* disable HW watchdog (branch precedes the legacy shared
		 * block; a WDT bite mid-erase corrupts the flash) */
		retval = target_write_u32(target, 0x40011C00, 0x1ACCE551);
		if (retval == ERROR_OK)
			retval = target_write_u32(target, 0x40011C00, 0xE5331AAE);
		if (retval == ERROR_OK)
			retval = target_write_u32(target, 0x40011008, 0);
		if (retval != ERROR_OK) {
			LOG_ERROR("fm3 type9: watchdog disable failed");
			return retval;
		}
		if (fm3_type9_force_halt(target) != ERROR_OK) {
			LOG_ERROR("fm3 type9: cannot force halt before chip erase");
			return ERROR_FAIL;
		}
		/* factory state before we wipe it */
		retval = target_read_u32(target, 0x40000100 /* CRTRMM */, &crtrmm);
		if (retval == ERROR_OK)
			retval = target_read_u16(target, 0x00100000, &sec);
		if (retval != ERROR_OK) {
			LOG_ERROR("fm3 type9: cannot read CRTRMM/security state");
			return retval;
		}
		if (sec == 0x0001)
			LOG_WARNING("fm3 type9: flash security was ENABLED; chip "
				    "erase releases it (manual chapter 2)");
		retval = fm3_type9_reset(target);
		if (retval != ERROR_OK)
			return retval;
		/* erase ROM0 (main): unlock AA/55/80/AA/55 then 0x10 */
		retval  = target_write_u16(target, port1, 0x00AA);
		if (retval == ERROR_OK) retval = target_write_u16(target, port2, 0x0055);
		if (retval == ERROR_OK) retval = target_write_u16(target, port1, 0x0080);
		if (retval == ERROR_OK) retval = target_write_u16(target, port1, 0x00AA);
		if (retval == ERROR_OK) retval = target_write_u16(target, port2, 0x0055);
		if (retval == ERROR_OK) retval = target_write_u16(target, port1, 0x0010);
		if (retval != ERROR_OK) {
			LOG_ERROR("fm3 type9 chip erase command fault");
			return retval;
		}
		retval = fm3_fstr_wait(target, 20000);
		if (retval != ERROR_OK) {
			fm3_type9_read_reset(target, bank->base);
			return retval;
		}
		for (unsigned int i = 0; i < bank->num_sectors; i++)
			bank->sectors[i].is_erased = 1;
		if (bank->base == 0x00000000 &&
		    crtrmm != 0 && crtrmm != 0xFFFFFFFF) {
			/* reprogram the CR trimming halves (TYPE9
			 * geometry per 002-05652: CR area @0x0010_2000:
			 * +0 freq trim (10 bit), +2 temp trim (5 bit)) */
			uint16_t ftrim = crtrmm & 0x3FF;
			uint16_t ttrim = (crtrmm >> 16) & 0x1F;
			for (int half = 0; half < 2; half++) {
				uint32_t craddr = 0x00102000 + half * 2;
				uint16_t val = half ? ttrim : ftrim;
				if (half && ttrim == 0)
					break;	/* no temp data to restore */
				retval = fm3_fstr_wait(target, 1500);
				if (retval == ERROR_OK) retval = target_write_u16(target, port1, 0x00AA);
				if (retval == ERROR_OK) retval = target_write_u16(target, port2, 0x0055);
				if (retval == ERROR_OK) retval = target_write_u16(target, port1, 0x00A0);
				if (retval == ERROR_OK) retval = target_write_u16(target, craddr, val);
				if (retval == ERROR_OK) retval = fm3_fstr_wait(target, 500);
				if (retval != ERROR_OK) {
					fm3_type9_read_reset(target, bank->base);
					LOG_ERROR("fm3 type9: CR trimming restore "
						  "FAILED (trim=%03x/%01x lost!)",
						  ftrim, ttrim);
					return retval;
				}
			}
			LOG_INFO("fm3 type9: CR trimming data restored "
				 "(freq=%03x temp=%01x)", ftrim, ttrim);
		} else if (bank->base == 0x00000000) {
			LOG_WARNING("fm3 type9: no CR trimming mirror data "
				    "found; chip erase leaves HOCO uncalibrated");
		}
		LOG_INFO("fm3 type9: chip erase complete");
		return ERROR_OK;
	}

	/* RAMCODE used for fm3 Flash chip erase:				   */
	/* R0 keeps Flash Sequence address 1     (u32FlashSeq1)    */
	/* R1 keeps Flash Sequence address 2     (u32FlashSeq2)    */
	static const uint8_t fm3_flash_erase_chip_code[] = {
						/*    *(uint16_t*)u32FlashSeq1 = 0xAA; */
		0xAA, 0x22,		/*        MOVS  R2, #0xAA              */
		0x02, 0x80,		/*        STRH  R2, [R0, #0]           */
						/*    *(uint16_t*)u32FlashSeq2 = 0x55; */
		0x55, 0x23,		/*        MOVS  R3, #0x55              */
		0x0B, 0x80,		/*        STRH  R3, [R1, #0]           */
						/*    *(uint16_t*)u32FlashSeq1 = 0x80; */
		0x80, 0x24,		/*        MOVS  R4, #0x80              */
		0x04, 0x80,		/*        STRH  R4, [R0, #0]           */
						/*    *(uint16_t*)u32FlashSeq1 = 0xAA; */
		0x02, 0x80,		/*        STRH  R2, [R0, #0]           */
						/*    *(uint16_t*)u32FlashSeq2 = 0x55; */
		0x0B, 0x80,		/*        STRH  R3, [R1, #0]           */
						/* Chip_Erase Command 0x10             */
						/*    *(uint16_t*)u32FlashSeq1 = 0x10; */
		0x10, 0x21,		/*        MOVS  R1, #0x10              */
		0x01, 0x80,		/*        STRH  R1, [R0, #0]           */
						/* End Code                            */
		0x00, 0xBE,		/*        BKPT  #0                      */
	};

	LOG_INFO("Fujitsu MB9[A/B]xxx: Chip Erase ... (may take several seconds)");

	/* disable HW watchdog */
	retval = target_write_u32(target, 0x40011C00, 0x1ACCE551);
	if (retval != ERROR_OK)
		return retval;

	retval = target_write_u32(target, 0x40011C00, 0xE5331AAE);
	if (retval != ERROR_OK)
		return retval;

	retval = target_write_u32(target, 0x40011008, 0x00000000);
	if (retval != ERROR_OK)
		return retval;

	if (fm3_info2->flashtype != FM3_FLASH_TYPE9) {
		/* FASZR = 0x01, Enables CPU Programming Mode (16-bit Flash access) */
		retval = target_write_u32(target, 0x40000000, 0x0001);
		if (retval != ERROR_OK)
			return retval;
	}

	if (fm3_info2->flashtype != FM3_FLASH_TYPE9) {
		/* dummy read of FASZR */
		retval = target_read_u32(target, 0x40000000, &u32_dummy_read);
		if (retval != ERROR_OK)
			return retval;
	}

	/* allocate working area with flash chip erase code */
	if (target_alloc_working_area(target, sizeof(fm3_flash_erase_chip_code),
			&write_algorithm) != ERROR_OK) {
		LOG_WARNING("no working area available, can't do block memory writes");
		return ERROR_TARGET_RESOURCE_NOT_AVAILABLE;
	}
	retval = target_write_buffer(target, write_algorithm->address,
		sizeof(fm3_flash_erase_chip_code), fm3_flash_erase_chip_code);
	if (retval != ERROR_OK)
		return retval;

	armv7m_info.common_magic = ARMV7M_COMMON_MAGIC;
	armv7m_info.core_mode = ARM_MODE_THREAD;

	init_reg_param(&reg_params[0], "r0", 32, PARAM_OUT); /* u32_flash_seq_address1 */
	init_reg_param(&reg_params[1], "r1", 32, PARAM_OUT); /* u32_flash_seq_address2 */

	buf_set_u32(reg_params[0].value, 0, 32, u32_flash_seq_address1);
	buf_set_u32(reg_params[1].value, 0, 32, u32_flash_seq_address2);

	retval = target_run_algorithm(target, 0, NULL, 2, reg_params,
			write_algorithm->address, 0, 100000, &armv7m_info);
	if (retval != ERROR_OK) {
		LOG_ERROR("Error executing flash erase programming algorithm");
		retval = ERROR_FLASH_OPERATION_FAILED;
		return retval;
	}

	target_free_working_area(target, write_algorithm);

	destroy_reg_param(&reg_params[0]);
	destroy_reg_param(&reg_params[1]);

	retval = fm3_busy_wait(target, u32_flash_seq_address2, 20000);	/* 20s timeout */
	if (retval != ERROR_OK)
		return retval;

	/* FASZR = 0x02, Re-enables CPU Run Mode (32-bit Flash access) */
	retval = target_write_u32(target, 0x40000000, 0x0002);
	if (retval != ERROR_OK)
		return retval;

	retval = target_read_u32(target, 0x40000000, &u32_dummy_read); /* dummy read of FASZR */

	return retval;
}

COMMAND_HANDLER(fm3_handle_chip_erase_command)
{
	if (CMD_ARGC < 1)
		return ERROR_COMMAND_SYNTAX_ERROR;

	struct flash_bank *bank;
	int retval = CALL_COMMAND_HANDLER(flash_command_get_bank, 0, &bank);
	if (retval != ERROR_OK)
		return retval;

	if (fm3_chip_erase(bank) == ERROR_OK) {
		command_print(CMD, "fm3 chip erase complete");
	} else {
		command_print(CMD, "fm3 chip erase failed");
	}

	return ERROR_OK;
}

static const struct command_registration fm3_exec_command_handlers[] = {
	{
		.name = "chip_erase",
		.usage = "<bank>",
		.handler = fm3_handle_chip_erase_command,
		.mode = COMMAND_EXEC,
		.help = "Erase entire Flash device.",
	},
	COMMAND_REGISTRATION_DONE
};

static const struct command_registration fm3_command_handlers[] = {
	{
		.name = "fm3",
		.mode = COMMAND_ANY,
		.help = "fm3 Flash command group",
		.usage = "",
		.chain = fm3_exec_command_handlers,
	},
	COMMAND_REGISTRATION_DONE
};

/* flash verify_bank / read path: code flash is memory mapped */
static int fm3_read(struct flash_bank *bank, uint8_t *buffer,
		    uint32_t offset, uint32_t count)
{
	return target_read_buffer(bank->target, bank->base + offset, count, buffer);
}

const struct flash_driver fm3_flash = {
	.name = "fm3",
	.commands = fm3_command_handlers,
	.flash_bank_command = fm3_flash_bank_command,
	.erase = fm3_erase,
	.write = fm3_write_block,
	.probe = fm3_probe,
	.auto_probe = fm3_auto_probe,
	.erase_check = default_flash_blank_check,
	.read = fm3_read,
	.free_driver_priv = default_flash_free_driver_priv,
};
