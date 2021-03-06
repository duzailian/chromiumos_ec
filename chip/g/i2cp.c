/* Copyright 2016 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/*
 * This is a driver for the I2C peripheral (i2cp) of the g chip.
 *
 * The driver has two register files, 64 bytes each, one for storing data
 * received from the controller, and one for storing data to be transmitted to
 * the controller. Both files are accessed only as 4 byte quantities, so the
 * driver must provide adaptation to concatenate messages with sizes not
 * divisible by 4 and or not properly aligned.
 *
 * The file holding data written by the controller has associated with it a
 * register showing where the driver accessed the file last, comparing it
 * with its previous value tells the driver how many bytes recently written by
 * the controller are there.
 *
 * The file holding data to be read by the controller has a register associated
 * with it showing where was the latest BIT the driver transmitted.
 *
 * The driver can generate interrupts on three different conditions:
 *  - beginning of a read cycle
 *  - end of a read cycle
 *  - end of a write cycle
 *
 * Since this driver's major role is to serve as a TPM interface, it is safe
 * to assume that the controller will always write first, even when it needs to
 * read data from the device.
 *
 * Each write or read access will be started by the controller writing the one
 * byte address of the TPM register to access.
 *
 * If the controller needs to read this register, the originating write
 * transaction will be limited to a single byte payload, a read transaction
 * would follow immediately.
 *
 * If the controller needs to write into this register, the data to be written
 * will be included in the same i2c transaction immediately following the one
 * byte register address.
 *
 * This protocol allows to keep the driver simple: the only interrupt the
 * driver enables is the 'end a write cycle'. The number of bytes received
 * from the controller gives the callback function a hint as of what the
 * controller intention is, to read or to write.
 *
 * In both cases the same callback function is called. On write accesses the
 * callback function converts the data as necessary and passes it to the TPM.
 * On read accesses the callback function retrieves data from the TPM and puts
 * it into the read register file to be available to the controller to retrieve
 * in the following read access. In both cases the callback function completes
 * processing on the invoking interrupt context.
 *
 * The driver API consists of two functions, one to register the callback to
 * process interrupts, another one - to add a byte to the congroller read
 * register file. See the accompanying .h file for details.
 *
 * TODO:
 * - figure out flow control - clock stretching can be challenging with this
 *   driver.
 * - detect and recover from overflow/underflow situations
 */

#include "common.h"
#include "console.h"
#include "flash_log.h"
#include "gpio.h"
#include "hooks.h"
#include "i2cp.h"
#include "pmu.h"
#include "registers.h"
#include "system.h"
#include "task.h"

#define REGISTER_FILE_SIZE BIT(6) /* 64 bytes. */
#define REGISTER_FILE_MASK (REGISTER_FILE_SIZE - 1)

/* Console output macros */
#define CPUTS(outstr) cputs(CC_I2C, outstr)
#define CPRINTF(format, args...) cprints(CC_I2C, format, ## args)

/* Pointer to the function to invoke on the write complete interrupts. */
static wr_complete_handler_f write_complete_handler_;

/* A buffer to normalize the received data to pass it to the user. */
static uint8_t i2cp_buffer[REGISTER_FILE_SIZE];

/*
 * Pointer where the CPU stopped retrieving the write data sent by the
 * controller last time the write access was processed.
 */
static uint16_t last_write_pointer;

/*
 * Pointer where the CPU stopped writing data for the controller to read last
 * time the read data was prepared.
 */
static uint16_t last_read_pointer;

/*
 * Keep track number of times the "hosed periph" condition was encountered.
 */
static uint16_t i2cp_read_recovery_count;
static uint16_t i2cp_sda_low_count;

/* Maximum number of times to log i2c errors each boot. */
#define FLOG_I2C_MAX_ERRORS 2

static void check_i2cp_state(void)
{
	if (gpio_get_level(GPIO_MONITOR_I2CP_SDA))
		return;

	/*
	 * The bus might be stuck;
	 * Generate a stop sequence to unwedge.
	 */
	board_unwedge_i2cp();
}

static void i2cp_init(void)
{

	/* First decide if i2c is even needed for this platform. */
	/* if (i2cp is not needed) return; */
	if (!board_tpm_uses_i2c())
		return;

	pmu_clock_en(PERIPH_I2CP);

	memset(i2cp_buffer, 0, sizeof(i2cp_buffer));

	i2cp_set_pinmux();

	check_i2cp_state();

	/* Reset read and write pointers. */
	last_write_pointer = 0;
	last_read_pointer = 0;
	i2cp_sda_low_count = 0;
	GWRITE(I2CS, READ_PTR, 0);
	GWRITE(I2CS, WRITE_PTR, 0);

	/* Just in case we were wedged and the controller starts with a read. */
	*GREG32_ADDR(I2CS, READ_BUFFER0) = ~0;

	/* Enable I2CP interrupt */
	GWRITE_FIELD(I2CS, INT_ENABLE, INTR_WRITE_COMPLETE, 1);

	/* periph address is hardcoded to 0x50. */
	GWRITE(I2CS, SLAVE_DEVADDRVAL, 0x50);
}

/* Forward declaration of the hook function. */
static void poll_read_state(void);
DECLARE_DEFERRED(poll_read_state);

/* Interval to poll SDA line when detecting the "hosed" condition. This value
 * must be larger then the maximum i2c transaction time. They are normally less
 * than 1 ms. The value multiplied by the threshold must also be larger than
 * the ap_is_on debounce time, which is 2 seconds.
 */
#define READ_STATUS_CHECK_INTERVAL (700 * MSEC)

/* Number of times SDA must be low between i2c writes before the i2cp driver
 * is restarted.
 *
 * Three was chosen because we can have two i2c transactions in between write
 * complete interrupts.
 *
 * Consider the following timeline:
 * 1) START <i2c_addr|W> <reg> STOP
 * 2) Write complete handler runs (i2cp_sda_low_count = 0)
 * 3) START <i2c_addr|R> <data>+ STOP (i2cp_sda_low_count++)
 * 4) START <i2c_addr|W> <reg> <data>+ STOP (i2cp_sda_low_count++)
 * 5) Write complete handler runs
 *
 * If the poller happened to run during time 3 and time 4 while SDA was low,
 * i2cp_sda_low_count would = 2. This is not considered an error case. If we
 * were to see a third low value before time 5, we can assume the bus is stuck,
 * or the controller performed multiple reads between writes (which is not
 * expected).
 *
 * If we were to enable the read complete interrupt and use it to clear
 * i2cp_sda_low_count we could get away with a threshold of two. This would also
 * support multiple reads after a write.
 *
 * We could in theory use the FIFO read/write pointers to determine if the bus
 * is stuck. This was not chosen because we would need to take the following
 * into account:
 * 1) The poller could run at time 3 between the final ACK bit being asserted
 *    and the stop condition happening. This would not increment any pointers.
 * 2) The poller could run at time 4 between the start condition and the first
 *    data byte being ACKed. The write pointer can only address full bytes,
 *    unlike the read pointer.
 * These two edge cases would force us to poll at least three times.
 */
#define READ_STATUS_CHECK_THRESHOLD 3

/*
 * Restart the i2cp driver if the driver gets stuck transmitting a 0 on
 * SDA.
 *
 * This can happen anytime the i2cp driver has control of SDA and the controller
 * happens to fail and stops clocking.
 *
 * For example when the i2cp driver is:
 * 1) Transmitting an ACK for the peripheral address byte.
 * 2) Transmitting an ACK for a write transaction.
 * 3) Transmitting byte data for a read transaction.
 *
 * The reason this is problematic is because the i2cc can't recover the bus
 * by issuing a new transaction. A start condition is defined as the i2cc
 * pulling SDA low while SCL is high. The i2cc can only initiate the start
 * condition when the bus is free (i.e., SDA is high), otherwise the i2cc
 * thinks that it lost arbitration.
 *
 * We don't have to deal with the scenario where the driver gets stuck
 * transmitting a 1 on SDA since the controller can recover the bus by issuing a
 * normal transaction. The controller will at minimum clock 9 times on any
 * transaction. This is enough for the peripheral to complete its current
 * operation and NACK.
 */
static void poll_read_state(void)
{
	if (!ap_is_on() || gpio_get_level(GPIO_I2CP_SDA)) {
		/*
		 * When the AP is off, the SDA line might drop low since the
		 * pull ups might not be powered.
		 *
		 * If the AP is on, the bus is either idle, the controller has
		 * stopped clocking while SDA is high, or we have polled in the
		 * middle of a transaction where SDA happens to be high.
		 */
		i2cp_sda_low_count = 0;
	} else {
		/*
		 * The controller has stopped clocking while the periph is
		 * holding SDA low, or we have polled in the middle of a
		 * transaction where SDA happens to be low.
		 */
		i2cp_sda_low_count++;

		/*
		 * SDA line has been stuck low without any write transactions
		 * occurring. We will assume the driver is stuck.
		 * Reinitialize the i2c driver (which will also restart this
		 * polling function).
		 */
		if (i2cp_sda_low_count == READ_STATUS_CHECK_THRESHOLD) {
			i2cp_sda_low_count = 0;
			i2cp_read_recovery_count++;
			CPRINTF("I2CP bus is stuck");
			/*
			 * i2cp_register_write_complete_handler will call
			 * hook_call_deferred.
			 */
			i2cp_register_write_complete_handler(
				write_complete_handler_);

#ifdef CONFIG_FLASH_LOG
			if (i2cp_read_recovery_count <= FLOG_I2C_MAX_ERRORS)
				flash_log_add_event(FE_TPM_I2C_ERROR, 0, NULL);
#endif
			return;
		}
	}

	hook_call_deferred(&poll_read_state_data, READ_STATUS_CHECK_INTERVAL);
}

/* Process the 'end of a write cycle' interrupt. */
void __attribute__((used)) _i2cp_write_complete_int(void)
{
	/* Reset the IRQ condition. */
	GWRITE_FIELD(I2CS, INT_STATE, INTR_WRITE_COMPLETE, 1);

	/* We're receiving some bytes, so don't sleep */
	disable_sleep(SLEEP_MASK_I2C_PERIPH);

	if (write_complete_handler_) {
		uint16_t bytes_written;
		uint16_t bytes_processed;
		uint32_t word_in_value = 0;

		/* How many bytes has the controller just written. */
		bytes_written = ((uint16_t)GREAD(I2CS, WRITE_PTR) -
				 last_write_pointer) & REGISTER_FILE_MASK;

		/* How many have been processed yet. */
		bytes_processed = 0;

		/* Make sure we start with something. */
		if (last_write_pointer & 3)
			word_in_value = *(GREG32_ADDR(I2CS, WRITE_BUFFER0) +
					  (last_write_pointer >> 2));
		while (bytes_written != bytes_processed) {
			/*
			 * This loop iterates over bytes retrieved from the
			 * controller write register file in 4 byte quantities.
			 * Each time the ever incrementing last_write_pointer
			 * is aligned at 4 bytes, a new value needs to be
			 * retrieved from the next register, indexed by
			 * last_write_pointer/4.
			 */

			if (!(last_write_pointer & 3))
				/* Time to get a new value. */
				word_in_value = *(GREG32_ADDR(
					I2CS, WRITE_BUFFER0) +
						  (last_write_pointer >> 2));

			/* Save the next byte in the adaptation buffer. */
			i2cp_buffer[bytes_processed] =
				word_in_value >> (8 * (last_write_pointer & 3));

			/* The pointer wraps at the register file size. */
			last_write_pointer = (last_write_pointer + 1) &
				REGISTER_FILE_MASK;
			bytes_processed++;
		}

		/* Invoke the callback to process the message. */
		write_complete_handler_(i2cp_buffer, bytes_processed);
	}

	/* The transaction is complete so the periph has released SDA. */
	i2cp_sda_low_count = 0;

	/*
	 * Could be the end of a TPM trasaction. Set sleep to be reenabled in 1
	 * second. If this is not the end of a TPM response, then sleep will be
	 * disabled again in the next I2CP interrupt.
	 */
	delay_sleep_by(1 * SECOND);
	enable_sleep(SLEEP_MASK_I2C_PERIPH);
}
DECLARE_IRQ(GC_IRQNUM_I2CS0_INTR_WRITE_COMPLETE_INT,
	    _i2cp_write_complete_int, 1);

void i2cp_post_read_data(uint8_t byte_to_read)
{
	volatile uint32_t *value_addr;
	uint32_t word_out_value;
	uint32_t shift;

	/*
	 * Find out which register of the register file the byte needs to go
	 * to.
	 */
	value_addr = GREG32_ADDR(I2CS, READ_BUFFER0) + (last_read_pointer >> 2);

	/* Read-modify-write the register adding the new byte there. */
	word_out_value = *value_addr;
	shift = (last_read_pointer & 3) * 8;
	word_out_value = (word_out_value & ~(0xff << shift)) |
		(((uint32_t)byte_to_read) << shift);
	*value_addr = word_out_value;
	last_read_pointer = (last_read_pointer + 1) & REGISTER_FILE_MASK;
}

void i2cp_post_read_fill_fifo(uint8_t *buffer, size_t len)
{
	volatile uint32_t *value_addr;
	uint32_t word_out_value;
	uint32_t addr_offset;
	uint32_t remainder_bytes;
	uint32_t start_offset;
	uint32_t num_words;
	int i, j;

	/* Get offset into 1st fifo word*/
	start_offset = last_read_pointer & 0x3;
	/* Number of bytes to fill out 1st word */
	remainder_bytes = (4 - start_offset) & 0x3;
	/* Get pointer to base of fifo and offset */
	addr_offset = last_read_pointer >> 2;
	value_addr = GREG32_ADDR(I2CS, READ_BUFFER0);
	/* Update read_pointer to reflect final value */
	last_read_pointer = (last_read_pointer + len) &
		REGISTER_FILE_MASK;

	/* Insert bytes until fifo is word aligned */
	if (remainder_bytes && len) {
		if (len < remainder_bytes)
			remainder_bytes = len;
		/* mask the bytes to be kept */
		word_out_value = value_addr[addr_offset];
		word_out_value &= (1 << (8 * start_offset)) - 1;
		/* Write in remainder bytes */
		for (i = 0; i < remainder_bytes; i++)
			word_out_value |= *buffer++ << (8 * (start_offset + i));
		/* Write to fifo register */
		value_addr[addr_offset] = word_out_value;
		addr_offset = (addr_offset + 1) & (REGISTER_FILE_MASK >> 2);
		/* Account for bytes consumed */
		len -= remainder_bytes;
	}

	/* HW fifo is now word aligned */
	num_words = len >> 2;
	for (i = 0; i < num_words; i++) {
		word_out_value = 0;
		for (j = 0; j < 4; j++)
			word_out_value |= *buffer++ << (j * 8);
		/* Write word to fifo and update fifo offset */
		value_addr[addr_offset] = word_out_value;
		addr_offset = (addr_offset + 1) & (REGISTER_FILE_MASK >> 2);
	}
	len -= (num_words << 2);

	/* Now process remaining bytes (if any), will be <= 3 at this point */
	remainder_bytes = len;
	if (remainder_bytes) {
		/* read from HW fifo */
		word_out_value = value_addr[addr_offset];
		/* Mask bytes that need to be kept */
		word_out_value &= (0xffffffff << (8 * remainder_bytes));
		for (i = 0; i < remainder_bytes; i++)
			word_out_value |= *buffer++ << (8 * i);
		value_addr[addr_offset] = word_out_value;
	}
}

int i2cp_register_write_complete_handler(wr_complete_handler_f wc_handler)
{
	task_disable_irq(GC_IRQNUM_I2CS0_INTR_WRITE_COMPLETE_INT);

	if (!wc_handler)
		return 0;

	i2cp_init();
	write_complete_handler_ = wc_handler;
	task_enable_irq(GC_IRQNUM_I2CS0_INTR_WRITE_COMPLETE_INT);

	/*
	 * Start a self perpetuating polling function to check for 'hosed'
	 * condition periodically.
	 */
	hook_call_deferred(&poll_read_state_data, READ_STATUS_CHECK_INTERVAL);

	return 0;
}

size_t i2cp_zero_read_fifo_buffer_depth(void)
{
	uint32_t hw_read_pointer;
	size_t depth;

	/*
	 * Get the current value of the HW I2CS read pointer. Note that the read
	 * pointer is b8:b3 of the I2CS_READ_PTR register. The lower 3 bits of
	 * this register are used to support bit accesses by a host.
	 */
	hw_read_pointer = GREAD(I2CS, READ_PTR) >> 3;
	/* Determine the number of bytes buffered in the HW fifo */
	depth = (last_read_pointer - hw_read_pointer) & REGISTER_FILE_MASK;
	/*
	 * If queue depth is not zero, force it to 0 by adjusting
	 * last_read_pointer to where the hw read pointer is.
	 */
	if (depth)
		last_read_pointer = (uint16_t)hw_read_pointer;
	/*
	 * Return number of bytes queued when this funciton is called so it can
	 * be tracked or logged by caller if desired.
	 */
	return depth;
}

void i2cp_get_status(struct i2cp_status *status)
{
	status->read_recovery_count = i2cp_read_recovery_count;
}
