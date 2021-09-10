/* Copyright (c) 2014 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "clock.h"
#include "common.h"
#include "config.h"
#include "console.h"
#include "gpio.h"
#include "hooks.h"
#include "link_defs.h"
#include "registers.h"
#include "task.h"
#include "timer.h"
#include "util.h"
#include "usb_descriptor.h"
#include "watchdog.h"

/****************************************************************************/
/* Debug output */

/* Console output macro */
#define CPRINTS(format, args...) cprints(CC_USB, format, ## args)
#define CPRINTF(format, args...) cprintf(CC_USB, format, ## args)

/* This is not defined anywhere else. Change it here to debug. */
#undef DEBUG_ME
#ifdef DEBUG_ME
/*
 * For debugging we want to print a bunch of things from within the interrupt
 * handlers, but if we try it'll 1) stop working, and 2) mess up the timing
 * that we're trying to measure. Instead we fill a circular buffer with things
 * to print when we get the chance. The number of args is fixed (a format
 * string and five uint32_t args), and will be printed a few at a time in a
 * HOOK_TICK handler.
 *
 */
#define MAX_ENTRIES 350				/* Chosen arbitrarily */
static struct {
	timestamp_t t;
	const char *fmt;
	int a0, a1, a2, a3, a4;
} stuff_to_print[MAX_ENTRIES];
static int stuff_in, stuff_out, stuff_overflow;

/* Call this only from within interrupt handler! */
void print_later(const char *fmt, int a0, int a1, int a2, int a3, int a4)
{
	int next;

	stuff_to_print[stuff_in].t = get_time();
	stuff_to_print[stuff_in].fmt = fmt;
	stuff_to_print[stuff_in].a0 = a0;
	stuff_to_print[stuff_in].a1 = a1;
	stuff_to_print[stuff_in].a2 = a2;
	stuff_to_print[stuff_in].a3 = a3;
	stuff_to_print[stuff_in].a4 = a4;

	next = (stuff_in + 1) % MAX_ENTRIES;
	if (next == stuff_out)
		stuff_overflow++;
	else
		stuff_in = next;
}

static void do_print_later(void)
{
	int lines_per_loop = 32;		/* too much at once fails */
	int copy_of_stuff_in;
	int copy_of_overflow;

	interrupt_disable();
	copy_of_stuff_in = stuff_in;
	copy_of_overflow = stuff_overflow;
	stuff_overflow = 0;
	interrupt_enable();

	if (copy_of_overflow)
		ccprintf("*** WARNING: %d MESSAGES WERE LOST ***\n",
			 copy_of_overflow);

	while (lines_per_loop && stuff_out != copy_of_stuff_in) {
		ccprintf("at %.6ld: ", stuff_to_print[stuff_out].t);
		ccprintf(stuff_to_print[stuff_out].fmt,
			 stuff_to_print[stuff_out].a0,
			 stuff_to_print[stuff_out].a1,
			 stuff_to_print[stuff_out].a2,
			 stuff_to_print[stuff_out].a3,
			 stuff_to_print[stuff_out].a4);
		ccprintf("\n");
		stuff_out = (stuff_out + 1) % MAX_ENTRIES;
		lines_per_loop--;
	}
}
DECLARE_HOOK(HOOK_TICK, do_print_later, HOOK_PRIO_DEFAULT);

/* Debugging stuff to display some registers and bits */
static const char const *deezbits[32] = {
	[0]     = "CURMOD",
	[1]     = "MODEMIS",
	[2]     = "OTGINT",
	[3]     = "SOF",
	[4]     = "RXFLVL",
	[6]     = "GINNAKEFF",
	[7]     = "GOUTNAKEFF",
	[10]    = "ERLYSUSP",
	[11]    = "USBSUSP",
	[12]    = "USBRST",
	[13]    = "ENUMDONE",
	[14]    = "ISOOUTDROP",
	[15]    = "EOPF",
	[17]    = "EPMIS",
	[18]    = "IEPINT",
	[19]    = "OEPINT",
	[20]    = "INCOMPISOIN",
	[21]    = "INCOMPLP",
	[22]    = "FETSUSP",
	[23]    = "RESETDET",
	[28]    = "CONIDSTSCHNG",
	[30]    = "SESSREQINT",
	[31]    = "WKUPINT",
};

static void showbits(uint32_t b)
{
	int i;

	for (i = 0; i < 32; i++)
		if (b & (1 << i)) {
			if (deezbits[i])
				ccprintf(" %s", deezbits[i]);
			else
				ccprintf(" %d", i);
		}
	ccprintf("\n");
}

static int command_usb(int argc, char **argv)
{
	ccprintf("GINTSTS:   0x%08x\n", GR_USB_GINTSTS);
	showbits(GR_USB_GINTSTS);
	ccprintf("GINTMSK:   0x%08x\n", GR_USB_GINTMSK);
	showbits(GR_USB_GINTMSK);
	ccprintf("DAINT:     0x%08x\n", GR_USB_DAINT);
	ccprintf("DAINTMSK:  0x%08x\n", GR_USB_DAINTMSK);
	ccprintf("DOEPMSK:   0x%08x\n", GR_USB_DOEPMSK);
	ccprintf("DIEPMSK:   0x%08x\n", GR_USB_DIEPMSK);
	ccprintf("DCFG:      0x%08x\n", GR_USB_DCFG);
	ccprintf("DOEPCTL0:  0x%08x\n", GR_USB_DOEPCTL(0));
	ccprintf("DIEPCTL0:  0x%08x\n", GR_USB_DIEPCTL(0));
	ccprintf("DOEPCTL1:  0x%08x\n", GR_USB_DOEPCTL(1));
	ccprintf("DIEPCTL1:  0x%08x\n", GR_USB_DIEPCTL(1));
	ccprintf("DOEPCTL2:  0x%08x\n", GR_USB_DOEPCTL(2));
	ccprintf("DIEPCTL2:  0x%08x\n", GR_USB_DIEPCTL(2));
	return EC_SUCCESS;
}
DECLARE_CONSOLE_COMMAND(usb, command_usb,
			"",
			"Show some USB regs",
			NULL);

/* When debugging, print errors as they occur */
#define report_error(dummy)					\
		print_later("USB ERROR at usb.c line %d",	\
			    __LINE__, 0, 0, 0, 0)

#else  /* Not debugging */
#define print_later(...)

/* TODO: Something unexpected happened. Figure out how to report & fix it. */
#define report_error(dummy)						\
		CPRINTS("Unhandled USB error at %s line %d",		\
			__FILE__, __LINE__)

#endif	/* DEBUG_ME */

/****************************************************************************/
/* Standard USB stuff */

#ifdef CONFIG_USB_BOS
/* v2.01 (vs 2.00) BOS Descriptor provided */
#define USB_DEV_BCDUSB 0x0201
#else
#define USB_DEV_BCDUSB 0x0200
#endif

#ifndef USB_DEV_CLASS
#define USB_DEV_CLASS USB_CLASS_PER_INTERFACE
#endif

#ifndef CONFIG_USB_BCD_DEV
#define CONFIG_USB_BCD_DEV 0x0100		/* 1.00 */
#endif

/* USB Standard Device Descriptor */
static const struct usb_device_descriptor dev_desc = {
	.bLength = USB_DT_DEVICE_SIZE,
	.bDescriptorType = USB_DT_DEVICE,
	.bcdUSB = USB_DEV_BCDUSB,
	.bDeviceClass = USB_DEV_CLASS,
	.bDeviceSubClass = 0x00,
	.bDeviceProtocol = 0x00,
	.bMaxPacketSize0 = USB_MAX_PACKET_SIZE,
	.idVendor = USB_VID_GOOGLE,
	.idProduct = CONFIG_USB_PID,
	.bcdDevice = CONFIG_USB_BCD_DEV,
	.iManufacturer = USB_STR_VENDOR,
	.iProduct = USB_STR_PRODUCT,
	.iSerialNumber = 0,
	.bNumConfigurations = 1
};

/* USB Configuration Descriptor */
const struct usb_config_descriptor USB_CONF_DESC(conf) = {
	.bLength = USB_DT_CONFIG_SIZE,
	.bDescriptorType = USB_DT_CONFIGURATION,
	.wTotalLength = 0x0BAD,	 /* number of returned bytes, set at runtime */
	.bNumInterfaces = USB_IFACE_COUNT,
	.bConfigurationValue = 1,
	.iConfiguration = USB_STR_VERSION,
	.bmAttributes = 0x80,			/* bus powered */
	.bMaxPower = 250,			/* MaxPower 500 mA */
};

const uint8_t usb_string_desc[] = {
	4,					/* Descriptor size */
	USB_DT_STRING,
	0x09, 0x04			    /* LangID = 0x0409: U.S. English */
};

/****************************************************************************/
/* Packet-handling stuff, specific to this SoC */

/* Some internal state to keep track of what's going on */
static enum {
	WAITING_FOR_SETUP_PACKET,
	DATA_STAGE_IN,
	NO_DATA_STAGE,
} what_am_i_doing;

/* Programmer's Guide, Table 10-7 */
enum table_case {
	BAD_0,
	TABLE_CASE_A,
	TABLE_CASE_B,
	TABLE_CASE_C,
	TABLE_CASE_D,
	TABLE_CASE_E,
	BAD_6,
	BAD_7,
};

/*
 * Table 10-7 in the Programmer's Guide decodes OUT endpoint interrupts:
 *
 *  Case  StatusPhseRecvd  SetUp  XferCompl   Description
 *
 *   A          0            0       1        Out descriptor is updated. Check
 *                                            its SR bit to see if we got
 *                                            a SETUP packet or an OUT packet.
 *   B          0            1       0        SIE has seen an IN or OUT packet
 *                                            following the SETUP packet.
 *   C          0            1       1        Both A & B at once, I think.
 *                                            Check the SR bit.
 *   D          1            0       0        SIE has seen the host change
 *                                            direction, implying Status phase.
 *   E          1            0       1        Out descriptor is updated, and
 *                                            SIE has seen an IN following it.
 *                                            This is probably the Status phase
 *                                            for a Control Write, but could be
 *                                            an early SETUP for a Control Read
 *                                            instead. Maybe. The documentation
 *                                            is unclear. Check the SR bit
 *                                            anyway.
 */
static enum table_case decode_table_10_7(uint32_t doepint)
{
	enum table_case val = BAD_0;

	/* Bits: SI, SPD, IOC */
	if (doepint & DOEPINT_XFERCOMPL)
		val += 1;
	if (doepint & DOEPINT_SETUP)
		val += 2;
	if (doepint & DOEPINT_STSPHSERCVD)
		val += 4;

	return val;
}

/* For STATUS/OUT: Use two DMA descriptors, each with one-packet buffers */
#define NUM_OUT_BUFFERS 2
static uint8_t ep0_out_buf[NUM_OUT_BUFFERS][USB_MAX_PACKET_SIZE];
static struct g_usb_desc ep0_out_desc[NUM_OUT_BUFFERS];
static int cur_out_idx;				/* latest with xfercompl=1 */
static const struct g_usb_desc *cur_out_desc;
static int next_out_idx;			/* next packet will go here */
static struct g_usb_desc *next_out_desc;

/* For IN: Several DMA descriptors, all pointing into one large buffer, so that
 * we can return the configuration descriptor as one big blob. */
#define NUM_IN_PACKETS_AT_ONCE 4
#define IN_BUF_SIZE (NUM_IN_PACKETS_AT_ONCE * USB_MAX_PACKET_SIZE)
static uint8_t ep0_in_buf[IN_BUF_SIZE];
static struct g_usb_desc ep0_in_desc[NUM_IN_PACKETS_AT_ONCE];
static struct g_usb_desc *cur_in_desc;

/* Reset all this to a good starting state. */
static void initialize_dma_buffers(void)
{
	int i;

	print_later("initialize_dma_buffers()", 0, 0, 0, 0, 0);

	for (i = 0; i < NUM_OUT_BUFFERS; i++) {
		ep0_out_desc[i].addr = ep0_out_buf[i];
		ep0_out_desc[i].flags = DOEPDMA_BS_HOST_BSY;
	}
	next_out_idx = 0;
	next_out_desc = ep0_out_desc + next_out_idx;
	GR_USB_DOEPDMA(0) = (uint32_t)next_out_desc;
	/* cur_out_* will be updated when we get the first RX packet */

	for (i = 0; i < NUM_IN_PACKETS_AT_ONCE; i++) {
		ep0_in_desc[i].addr = ep0_in_buf + i * USB_MAX_PACKET_SIZE;
		ep0_in_desc[i].flags = DIEPDMA_BS_HOST_BSY;
	}
	cur_in_desc = ep0_in_desc;
	GR_USB_DIEPDMA(0) = (uint32_t)(cur_in_desc);
};

/* Change the RX descriptors after each SETUP/OUT packet is received so we can
 * prepare to receive another without losing track of this one. */
static void got_RX_packet(void)
{
	cur_out_idx = next_out_idx;
	cur_out_desc = ep0_out_desc + cur_out_idx;
	next_out_idx = (next_out_idx + 1) % NUM_OUT_BUFFERS;
	next_out_desc = ep0_out_desc + next_out_idx;
	GR_USB_DOEPDMA(0) = (uint32_t)next_out_desc;
}

/* Load the EP0 IN FIFO buffer with some data (zero-length works too). Returns
 * len, or negative on error. */
int load_in_fifo(const void *source, uint32_t len)
{
	uint8_t *buffer = ep0_in_buf;
	int zero_packet = !len;
	int d, l;

	/* Copy the data into our FIFO buffer */
	if (len >= IN_BUF_SIZE) {
		report_error();
		return -1;
	}
	if (len)
		memcpy(buffer, source, len);

	/* Set up the descriptors */
	for (d = l = 0; len >= USB_MAX_PACKET_SIZE; d++) {
		ep0_in_desc[d].addr = buffer + d * USB_MAX_PACKET_SIZE;
		ep0_in_desc[d].flags = DIEPDMA_TXBYTES(USB_MAX_PACKET_SIZE);
		len -= USB_MAX_PACKET_SIZE;
		l = d;
	}
	/* Maybe one short packet left? */
	if (len || zero_packet) {
		ep0_in_desc[d].addr = buffer + d * USB_MAX_PACKET_SIZE;
		ep0_in_desc[d].flags = DIEPDMA_TXBYTES(len) | DIEPDMA_SP;
		l = d;
	}
	/* Mark the last descriptor as last. */
	ep0_in_desc[l].flags |= (DIEPDMA_LAST | DIEPDMA_IOC);

	/* Point to the first in the chain */
	cur_in_desc = ep0_in_desc;

	return len;
}

/* Prepare the EP0 OUT FIFO buffer to accept some data. Returns len, or
 * negative on error. */
int accept_out_fifo(uint32_t len)
{
	/* TODO: This is not yet implemented */
	report_error();
	return -1;
}

static void flush_in_fifo(void)
{
	/* TODO: Programmer's Guide p167 suggests lots more stuff */
	GR_USB_GRSTCTL = GRSTCTL_TXFNUM(0) | GRSTCTL_TXFFLSH;
	while (GR_USB_GRSTCTL & GRSTCTL_TXFFLSH)
		;				/* timeout? */
}

/* We're complaining about something by stalling both IN and OUT packets,
 * but a SETUP packet will get through anyway, so prepare for it. */
static void stall_both_fifos(void)
{
	print_later("stall_both_fifos()", 0, 0, 0, 0, 0);

	what_am_i_doing = WAITING_FOR_SETUP_PACKET;

	next_out_desc->flags =
		DOEPDMA_RXBYTES(USB_MAX_PACKET_SIZE)
		| DOEPDMA_IOC | DOEPDMA_LAST;

	/* We don't care about IN packets right now, only OUT. */
	GR_USB_DAINTMSK |= DAINT_OUTEP(0);
	GR_USB_DAINTMSK &= ~DAINT_INEP(0);

	/* Stall both IN and OUT. The hardware will reset them when the next
	 * SETUP comes along. */
	GR_USB_DOEPCTL(0) = DXEPCTL_STALL | DXEPCTL_EPENA;
	flush_in_fifo();
	GR_USB_DIEPCTL(0) = DXEPCTL_STALL | DXEPCTL_EPENA;
}

/* The next packet from the host should be a Setup packet. Get ready for it. */
static void expect_setup_packet(void)
{
	print_later("expect_setup_packet()", 0, 0, 0, 0, 0);

	what_am_i_doing = WAITING_FOR_SETUP_PACKET;

	next_out_desc->flags =
		DOEPDMA_RXBYTES(USB_MAX_PACKET_SIZE)
		| DOEPDMA_IOC | DOEPDMA_LAST;

	/* We don't care about IN packets right now, only OUT. */
	GR_USB_DAINTMSK |= DAINT_OUTEP(0);
	GR_USB_DAINTMSK &= ~DAINT_INEP(0);

	/* Let it run. We might need CNAK if we just got an OUT for status */
	GR_USB_DOEPCTL(0) = DXEPCTL_CNAK | DXEPCTL_EPENA;
}

/* The TX FIFO buffer is loaded. Start the Data phase. */
static void expect_data_phase_in(enum table_case tc)
{
	print_later("expect_data_phase_in(%c)", "0ABCDE67"[tc], 0, 0, 0, 0);

	what_am_i_doing = DATA_STAGE_IN;

	/* We apparently have to do this every time we transmit anything */
	flush_in_fifo();

	/* I don't think we have to do this every time, but the Programmer's
	 * Guide says to, so...	*/
	GR_USB_DIEPDMA(0) = (uint32_t)(cur_in_desc);

	/* Blindly following instructions here, too. */
	if (tc == TABLE_CASE_C)
		GR_USB_DIEPCTL(0) = DXEPCTL_CNAK | DXEPCTL_EPENA;
	else
		GR_USB_DIEPCTL(0) = DXEPCTL_EPENA;

	/*
	 * When the IN is done, we expect a zero-length OUT for the status
	 * phase but it could be an early SETUP instead. We'll have to deal
	 * with either one when it arrives.
	 */
	next_out_desc->flags =
		DOEPDMA_RXBYTES(USB_MAX_PACKET_SIZE)
		| DOEPDMA_IOC | DOEPDMA_LAST;

	/* And here's this jimmy rustler again... */
	if (tc == TABLE_CASE_C)
		GR_USB_DOEPCTL(0) = DXEPCTL_CNAK | DXEPCTL_EPENA;
	else
		GR_USB_DOEPCTL(0) = DXEPCTL_EPENA;

	/* Get an interrupt when either IN or OUT arrives */
	GR_USB_DAINTMSK |= (DAINT_OUTEP(0) | DAINT_INEP(0));

}

static void expect_data_phase_out(enum table_case tc)
{
	print_later("expect_data_phase_out(%c)", "0ABCDE67"[tc], 0, 0, 0, 0);
	/* TODO: This is not yet supported */
	report_error();
	expect_setup_packet();
}

/* No Data phase, just Status phase (which is IN, since Setup is OUT) */
static void expect_status_phase_in(enum table_case tc)
{
	print_later("expect_status_phase_in(%c)", "0ABCDE67"[tc], 0, 0, 0, 0);

	what_am_i_doing = NO_DATA_STAGE;

	/* Expect a zero-length IN for the Status phase */
	(void) load_in_fifo(0, 0);

	/* We apparently have to do this every time we transmit anything */
	flush_in_fifo();

	/* I don't think we have to do this every time, but the Programmer's
	 * Guide says to, so...	*/
	GR_USB_DIEPDMA(0) = (uint32_t)(cur_in_desc);

	/* Blindly following instructions here, too. */
	if (tc == TABLE_CASE_C)
		GR_USB_DIEPCTL(0) = DXEPCTL_CNAK | DXEPCTL_EPENA;
	else
		GR_USB_DIEPCTL(0) = DXEPCTL_EPENA;

	/* The Programmer's Guide instructions for the Normal Two-Stage Control
	 * Transfer leave this next bit out, so we only need it if we intend to
	 * process an Exceptional Two-Stage Control Transfer. Because obviously
	 * we always know in advance what the host is going to do. Idiots. */

	/* Be prepared to get a new Setup packet during the Status phase */
	next_out_desc->flags =
		DOEPDMA_RXBYTES(USB_MAX_PACKET_SIZE)
		| DOEPDMA_IOC | DOEPDMA_LAST;

	/* We've already set GR_USB_DOEPDMA(0), so just enable it. */
	if (tc == TABLE_CASE_C)
		GR_USB_DOEPCTL(0) = DXEPCTL_CNAK | DXEPCTL_EPENA;
	else
		GR_USB_DOEPCTL(0) = DXEPCTL_EPENA;

	/* Get an interrupt when either IN or OUT arrives */
	GR_USB_DAINTMSK |= (DAINT_OUTEP(0) | DAINT_INEP(0));
}

/* Handle a Setup packet that expects us to send back data in reply. Return the
 * length of the data we're returning, or negative to indicate an error. */
static int handle_setup_with_in_stage(enum table_case tc,
				      struct usb_setup_packet *req)
{
	const void *data = 0;
	uint32_t len = 0;
	int ugly_hack = 0;
	static const uint16_t zero;		/* == 0 */

	print_later("handle_setup_with_in_stage(%c)", "0ABCDE67"[tc],
		    0, 0, 0, 0);

	switch (req->bRequest) {
	case USB_REQ_GET_DESCRIPTOR: {
		uint8_t type = req->wValue >> 8;
		uint8_t idx = req->wValue & 0xff;

		switch (type) {
		case USB_DT_DEVICE:
			data = &dev_desc;
			len = sizeof(dev_desc);
			break;
		case USB_DT_CONFIGURATION:
			data = __usb_desc;
			len = USB_DESC_SIZE;
			ugly_hack = 1;		/* see below */
			break;
#ifdef CONFIG_USB_BOS
		case USB_DT_BOS:
			data = bos_ctx.descp;
			len = bos_ctx.size;
			break;
#endif
		case USB_DT_STRING:
			if (idx >= USB_STR_COUNT)
				return -1;
			data = usb_strings[idx];
			len = *(uint8_t *)data;
			break;
		case USB_DT_DEVICE_QUALIFIER:
			/* We're not high speed */
			return -1;
		default:
			report_error();
			return -1;
		}
		break;
	}
	case USB_REQ_GET_STATUS: {
		/* TODO: Device Status: Remote Wakeup? Self Powered? */
		data = &zero;
		len = sizeof(zero);
		break;
	}
	case USB_REQ_GET_CONFIGURATION:
		/* TODO: We might need this to handle USB suspend properly */
		return -1;

	case USB_REQ_SYNCH_FRAME:
		/* Unimplemented */
		return -1;

	default:
		report_error();
		return -1;
	}

	/* Don't send back more than we were asked for. */
	len = MIN(req->wLength, len);

	/* Prepare the TX FIFO. If we haven't preallocated enough room in the
	 * TX FIFO for the largest reply, we'll have to stall. This is a bug in
	 * our code, but detecting it easily at compile time is related to the
	 * ugly_hack directly below. */
	if (load_in_fifo(data, len) < 0)
		return -1;

	if (ugly_hack) {
		/*
		 * TODO: Somebody figure out how to fix this, please.
		 *
		 * The USB configuration descriptor request is unique in that
		 * it not only returns the configuration descriptor, but also
		 * all the interface descriptors and all their endpoint
		 * descriptors as one enormous blob. We've set up some macros
		 * so we can declare and implement separate interfaces in
		 * separate files just by compiling them, and all the relevant
		 * descriptors are sorted and bundled up by the linker. But the
		 * total length of the entire blob needs to appear in the first
		 * configuration descriptor struct and because we don't know
		 * that value until after linking, it can't be initialized as a
		 * constant. So we have to compute it at run-time and shove it
		 * in here, which also means that we have to copy the whole
		 * blob into our TX FIFO buffer so that it's mutable. Otherwise
		 * we could just point at it (or pretty much any other constant
		 * struct that we wanted to send to the host). Bah.
		 */
		struct usb_config_descriptor *cfg =
			(struct usb_config_descriptor *)ep0_in_buf;
		/* set the real descriptor size */
		cfg->wTotalLength = USB_DESC_SIZE;
	}

	return len;
}

/* Handle a Setup that comes with additional data for us. */
static int handle_setup_with_out_stage(enum table_case tc,
				       struct usb_setup_packet *req)
{
	print_later("handle_setup_with_out_stage(%c)", "0ABCDE67"[tc],
		    0, 0, 0, 0);

	/* TODO: We don't support any of these. We should. */
	return -1;
}

/* Some Setup packets don't have a data stage at all. */
static int handle_setup_with_no_data_stage(enum table_case tc,
					   struct usb_setup_packet *req)
{
	uint8_t set_addr;

	print_later("handle_setup_with_no_data_stage(%c)", "0ABCDE67"[tc],
		    0, 0, 0, 0);

	switch (req->bRequest) {
	case USB_REQ_SET_ADDRESS:
		/*
		 * Set the address after the IN packet handshake.
		 *
		 * From the USB 2.0 spec, section 9.4.6:
		 *
		 * As noted elsewhere, requests actually may result in
		 * up to three stages. In the first stage, the Setup
		 * packet is sent to the device. In the optional second
		 * stage, data is transferred between the host and the
		 * device. In the final stage, status is transferred
		 * between the host and the device. The direction of
		 * data and status transfer depends on whether the host
		 * is sending data to the device or the device is
		 * sending data to the host. The Status stage transfer
		 * is always in the opposite direction of the Data
		 * stage. If there is no Data stage, the Status stage
		 * is from the device to the host.
		 *
		 * Stages after the initial Setup packet assume the
		 * same device address as the Setup packet. The USB
		 * device does not change its device address until
		 * after the Status stage of this request is completed
		 * successfully. Note that this is a difference between
		 * this request and all other requests. For all other
		 * requests, the operation indicated must be completed
		 * before the Status stage
		 */
		set_addr = req->wValue & 0xff;
		/*
		 * NOTE: Now that we've said that, we don't do it. The
		 * hardware for this SoC knows that an IN packet will
		 * be following the SET ADDRESS, so it waits until it
		 * sees that happen before the address change takes
		 * effect. If we wait until after the IN packet to
		 * change the register, the hardware gets confused and
		 * doesn't respond to anything.
		 */
		GWRITE_FIELD(USB, DCFG, DEVADDR, set_addr);
		print_later("SETAD 0x%02x (%d)", set_addr, set_addr, 0, 0, 0);
		break;

	case USB_REQ_SET_CONFIGURATION:
		/* TODO: Sanity-check this? We only have one config, right? */
		print_later("SETCFG 0x%x", req->wValue, 0, 0, 0, 0);
		break;

	case USB_REQ_CLEAR_FEATURE:
	case USB_REQ_SET_FEATURE:
		/* TODO: Handle DEVICE_REMOTE_WAKEUP, ENDPOINT_HALT? */
		print_later("SET_FEATURE/CLEAR_FEATURE. Whatever...",
			    0, 0, 0, 0, 0);
		break;

	default:
		/* Anything else is unsupported */
		return -1;
	}

	/* No data to transfer, go straight to the Status phase. */
	return 0;
}

/* Dispatch an incoming Setup packet according to its type */
static void handle_setup(enum table_case tc)
{
	struct usb_setup_packet *req = cur_out_desc->addr;
	int data_phase_in = req->bmRequestType & USB_DIR_IN;
	int data_phase_out = !data_phase_in && req->wLength;
	int bytes = -1;				/* default is to stall */

	print_later("R: %02x %02x %04x %04x %04x",
		    req->bmRequestType, req->bRequest,
		    req->wValue, req->wIndex, req->wLength);

	if (0 == (req->bmRequestType & (USB_TYPE_MASK | USB_RECIP_MASK))) {
		/* Standard Device requests */
		if (data_phase_in)
			bytes = handle_setup_with_in_stage(tc, req);
		else if (data_phase_out)
			bytes = handle_setup_with_out_stage(tc, req);
		else
			bytes = handle_setup_with_no_data_stage(tc, req);
	} else if (USB_RECIP_INTERFACE ==
		   (req->bmRequestType & USB_RECIP_MASK)) {
		/* Interface-specific requests */
		uint8_t iface = req->wIndex & 0xff;

		print_later("iface %d request (vs %d)",
			    iface, USB_IFACE_COUNT, 0, 0, 0);
		if (iface < USB_IFACE_COUNT) {
			bytes = usb_iface_request[iface](req);
			print_later("  iface returned %d", bytes, 0, 0, 0, 0);
		}
	} else {
		/* Something we need to add support for? */
		report_error();
	}

	print_later("data_phase_in %d data_phase_out %d bytes %d",
		    !!data_phase_in, !!data_phase_out, bytes, 0, 0);

	/* We say "no" to unsupported and intentionally unhandled requests by
	 * stalling the Data and/or Status stage. */
	if (bytes < 0) {
		/* Stall both IN and OUT. SETUP will come through anyway. */
		stall_both_fifos();
	} else {
		if (data_phase_in)
			expect_data_phase_in(tc);
		else if (data_phase_out)
			expect_data_phase_out(tc);
		else
			expect_status_phase_in(tc);
	}
}

/* This handles both IN and OUT interrupts for EP0 */
static void ep0_interrupt(uint32_t intr_on_out, uint32_t intr_on_in)
{
	uint32_t doepint, diepint;
	enum table_case tc;
	int sr;

	/* Determine the interrupt cause and clear the bits quickly, but only
	 * if they really apply. I don't think they're trustworthy if we didn't
	 * actually get an interrupt. */
	doepint = GR_USB_DOEPINT(0);
	if (intr_on_out)
		GR_USB_DOEPINT(0) = doepint;
	diepint = GR_USB_DIEPINT(0);
	if (intr_on_in)
		GR_USB_DIEPINT(0) = diepint;

	print_later("doepint%c 0x%08x diepint%c 0x%08x what %d",
		    intr_on_out ? '!' : '_', doepint,
		    intr_on_in ? '!' : '_', diepint,
		    what_am_i_doing);

	/* Update current and pending RX FIFO buffers */
	if (intr_on_out && (doepint & DOEPINT_XFERCOMPL))
		got_RX_packet();

	/* Decode the situation according to Table 10-7 */
	tc = decode_table_10_7(doepint);
	sr = cur_out_desc->flags & DOEPDMA_SR;

	print_later("cur_out_idx %d flags 0x%08x case=%c SR=%d",
		    cur_out_idx, cur_out_desc->flags,
		    "0ABCDE67"[tc], !!sr, 0);

	switch (what_am_i_doing) {
	case WAITING_FOR_SETUP_PACKET:
		if (tc == TABLE_CASE_A || tc == TABLE_CASE_C) {
			if (sr) {
				handle_setup(tc);
			} else {
				report_error();
				print_later("next_out_idx %d flags 0x%08x",
					    next_out_idx, next_out_desc->flags,
					    0, 0, 0);
				expect_setup_packet();
			}
		}
		/* This only happens if we're stalling, so keep doing it. */
		if (tc == TABLE_CASE_B) {
			print_later("Still waiting for Setup...",
				    0, 0, 0, 0, 0);
			stall_both_fifos();
		}
		break;

	case DATA_STAGE_IN:
		if (intr_on_in && (diepint & DIEPINT_XFERCOMPL)) {
			print_later("IN is complete? Maybe? How do we know?",
				    0, 0, 0, 0, 0);
			/* I don't *think* we need to do this, unless we need
			 * to transfer more data. Customer support agrees and
			 * it shouldn't matter if the host is well-behaved, but
			 * seems like we had issues without it.
			 * TODO: Test this case until we know for sure. */
			GR_USB_DIEPCTL(0) = DXEPCTL_EPENA;

			/*
			 * The Programmer's Guide says (p291) to stall any
			 * further INs, but that's stupid because it'll destroy
			 * the packet we just tranferred to SPRAM, so don't do
			 * that (we tried it anyway, and Bad Things happened).
			 * Also don't stop here, but keep looking at stuff.
			 */
		}

		/* But we should ignore the OUT endpoint if we didn't actually
		 * get an OUT interrupt. */
		if (!intr_on_out)
			break;

		if (tc == TABLE_CASE_B) {
			print_later("IN has been detected...", 0, 0, 0, 0, 0);
			/* The first IN packet has been seen. Keep going. */
			GR_USB_DIEPCTL(0) = DXEPCTL_CNAK | DXEPCTL_EPENA;
			GR_USB_DOEPCTL(0) = DXEPCTL_CNAK | DXEPCTL_EPENA;
			break;
		}
		if (tc == TABLE_CASE_A) {
			if (!sr) {
				/* We've handled the Status phase. All done. */
				print_later("Status phase complete",
					    0, 0, 0, 0, 0);
				expect_setup_packet();
				break;
			}
			/* We expected an OUT, but got a Setup. Deal with it. */
			print_later("Early Setup", 0, 0, 0, 0, 0);
			handle_setup(tc);
			break;
		}
		/* From the Exceptional Control Read Transfer section ... */
		if (tc == TABLE_CASE_C) {
			if (sr) {
				print_later("Early Setup w/Data packet seen",
					    0, 0, 0, 0, 0);
				handle_setup(tc);
				break;
			}
			print_later("Status phase complete. I think...",
				    0, 0, 0, 0, 0);
			expect_setup_packet();
			break;
		}

		/* Anything else should be ignorable. Right? */
		break;

	case NO_DATA_STAGE:
		if (intr_on_in && (diepint & DIEPINT_XFERCOMPL)) {
			print_later("Status phase complete", 0, 0, 0, 0, 0);
			/* Let the IN proceed */
			GR_USB_DIEPCTL(0) = DXEPCTL_EPENA;
			/* We've already prepared the OUT descriptor. */
			what_am_i_doing = WAITING_FOR_SETUP_PACKET;
		}

		/* Done unless we got an OUT interrupt */
		if (!intr_on_out)
			break;

		if (tc == TABLE_CASE_B) {
			print_later("IN has been detected...", 0, 0, 0, 0, 0);
			/* Reenable the previously prepared OUT descriptor. */
			GR_USB_DOEPCTL(0) = DXEPCTL_CNAK | DXEPCTL_EPENA;
			break;
		}

		if (tc == TABLE_CASE_A || tc == TABLE_CASE_C) {
			if (sr) {
				/* We expected an IN, but got a Setup. */
				print_later("Early Setup", 0, 0, 0, 0, 0);
				handle_setup(tc);
				break;
			}
		}

		/* Some other kind of OUT interrupt instead. */
		report_error();
		expect_setup_packet();
		break;
	}
}

/* Endpoint-specific callback for when the USB device recognizes a reset */
static void ep0_reset(void)
{
	/* Reset EP0 address */
	GWRITE_FIELD(USB, DCFG, DEVADDR, 0);
	initialize_dma_buffers();
	expect_setup_packet();
}

/****************************************************************************/
/* USB device initialization and shutdown routines */

/*
 * DATA FIFO Setup. There is an internal SPRAM used to buffer the IN/OUT
 * packets and track related state without hammering the AHB and system RAM
 * during USB transactions. We have to specify where and how much of that SPRAM
 * to use for what.
 *
 * See Programmer's Guide chapter 2, "Calculating FIFO Size".
 * We're using Dedicated TxFIFO Operation, without enabling thresholding.
 *
 * Section 2.1.1.2, page 30: RXFIFO size is the same as for Shared FIFO, which
 * is Section 2.1.1.1, page 28. This is also the same as Method 2 on page 45.
 *
 * We support up to 3 control EPs, no periodic IN EPs, up to 16 TX EPs. Max
 * data packet size is 64 bytes. Total SPRAM available is 1024 slots.
 */
#define MAX_CONTROL_EPS   3
#define MAX_NORMAL_EPS    16
#define FIFO_RAM_DEPTH    1024
/*
 * Device RX FIFO size is thus:
 *   (4 * 3 + 6) + 2 * ((64 / 4) + 1) + (2 * 16) + 1 == 85
 */
#define RXFIFO_SIZE  ((4 * MAX_CONTROL_EPS + 6) + \
		      2 * ((USB_MAX_PACKET_SIZE / 4) + 1) + \
		      (2 * MAX_NORMAL_EPS) + 1)
/*
 * Device TX FIFO size is 2 * (64 / 4) == 32 for each IN EP (Page 46).
 */
#define TXFIFO_SIZE  (2 * (USB_MAX_PACKET_SIZE / 4))
/*
 * We need 4 slots per endpoint direction for endpoint status stuff (Table 2-1,
 * unconfigurable).
 */
#define EP_STATUS_SIZE (4 * MAX_NORMAL_EPS * 2)
/*
 * Make sure all that fits.
 */
BUILD_ASSERT(RXFIFO_SIZE + TXFIFO_SIZE * MAX_NORMAL_EPS + EP_STATUS_SIZE <
	     FIFO_RAM_DEPTH);

/* Now put those constants into the correct registers */
static void setup_data_fifos(void)
{
	int i;

	print_later("setup_data_fifos()", 0, 0, 0, 0, 0);

	/* Programmer's Guide, p31 */
	GR_USB_GRXFSIZ = RXFIFO_SIZE;			      /* RXFIFO */
	GR_USB_GNPTXFSIZ = (TXFIFO_SIZE << 16) | RXFIFO_SIZE; /* TXFIFO 0 */

	/* TXFIFO 1..15 */
	for (i = 1; i < MAX_NORMAL_EPS; i++)
		GR_USB_DIEPTXF(i) = ((TXFIFO_SIZE << 16) |
				     (RXFIFO_SIZE + i * TXFIFO_SIZE));

	/*
	 * TODO: The Programmer's Guide is confusing about when or whether to
	 * flush the FIFOs. Section 2.1.1.2 (p31) just says to flush. Section
	 * 2.2.2 (p55) says to stop all the FIFOs first, then flush. Section
	 * 7.5.4 (p162) says that flushing the RXFIFO at reset is not
	 * recommended at all.
	 *
	 * I'm also unclear on whether or not the individual EPs are expected
	 * to be disabled already (DIEPCTLn/DOEPCTLn.EPENA == 0), and if so,
	 * whether by firmware or hardware.
	 */

	/* Flush all FIFOs according to Section 2.1.1.2 */
	GR_USB_GRSTCTL = GRSTCTL_TXFNUM(0x10) | GRSTCTL_TXFFLSH
		| GRSTCTL_RXFFLSH;
	while (GR_USB_GRSTCTL & (GRSTCTL_TXFFLSH | GRSTCTL_RXFFLSH))
		;				/* TODO: timeout 100ms */
}

static void usb_reset(void)
{
	int ep;

	print_later("usb_reset()", 0, 0, 0, 0, 0);

	ep0_reset();
	for (ep = 1; ep < USB_EP_COUNT; ep++)
		usb_ep_reset[ep]();
}

static void usb_enumdone(void)
{
	print_later("usb_enumdone()", 0, 0, 0, 0, 0);
}

static void usb_wakeup(void)
{
	print_later("usb_wakeup()", 0, 0, 0, 0, 0);
}

static void usb_early_suspend(void)
{
	print_later("usb_early_suspend()", 0, 0, 0, 0, 0);
}
static void usb_suspend(void)
{
	print_later("usb_suspend()", 0, 0, 0, 0, 0);
}

void usb_interrupt(void)
{
	uint32_t status = GR_USB_GINTSTS;
	uint32_t oepint = status & GINTSTS(OEPINT);
	uint32_t iepint = status & GINTSTS(IEPINT);

	int ep;

	print_later("interrupt: GINTSTS 0x%08x", status, 0, 0, 0, 0);

	if (status & GINTSTS(RESETDET))
		usb_wakeup();

	if (status & GINTSTS(ERLYSUSP))
		usb_early_suspend();

	if (status & GINTSTS(USBSUSP))
		usb_suspend();

	if (status & GINTSTS(USBRST))
		usb_reset();

	if (status & GINTSTS(ENUMDONE))
		usb_enumdone();

	/* Endpoint interrupts */
	if (oepint || iepint) {
		/* Note: It seems that the DAINT bits are only trustworthy for
		 * identifying interrupts when selected by the corresponding
		 * OEPINT and IEPINT bits from GINTSTS. */
		uint32_t daint = GR_USB_DAINT;

		print_later("  oepint%c iepint%c daint 0x%08x",
			    oepint ? '!' : '_', iepint ? '!' : '_',
			    daint, 0, 0);

		/* EP0 has a combined IN/OUT handler. Only call it once, but
		 * let it know which direction(s) had an interrupt. */
		if (daint & (DAINT_OUTEP(0) | DAINT_INEP(0))) {
			uint32_t intr_on_out = (oepint &&
						(daint & DAINT_OUTEP(0)));
			uint32_t intr_on_in = (iepint &&
					       (daint & DAINT_INEP(0)));
			ep0_interrupt(intr_on_out, intr_on_in);
		}

		/* Invoke the unidirectional IN and OUT functions for the other
		 * endpoints. Each handler must clear their own bits in
		 * DIEPINTn/DOEPINTn. */
		for (ep = 1; ep < USB_EP_COUNT; ep++) {
			if (oepint && (daint & DAINT_OUTEP(ep)))
				usb_ep_rx[ep]();
			if (iepint && (daint & DAINT_INEP(ep)))
				usb_ep_tx[ep]();
		}
	}

	if (status & GINTSTS(GOUTNAKEFF))
		GR_USB_DCTL = DCTL_CGOUTNAK;

	if (status & GINTSTS(GINNAKEFF))
		GR_USB_DCTL = DCTL_CGNPINNAK;

	GR_USB_GINTSTS = status;

	print_later("end of interrupt", 0, 0, 0, 0, 0);
}
DECLARE_IRQ(GC_IRQNUM_USB0_USBINTR, usb_interrupt, 1);

static void usb_softreset(void)
{
	int timeout;

	GR_USB_GRSTCTL = GRSTCTL_CSFTRST;
	timeout = 10000;
	while ((GR_USB_GRSTCTL & GRSTCTL_CSFTRST) && timeout-- > 0)
		;
	if (GR_USB_GRSTCTL & GRSTCTL_CSFTRST) {
		CPRINTF("USB: reset failed\n");
		return;
	}

	timeout = 10000;
	while (!(GR_USB_GRSTCTL & GRSTCTL_AHBIDLE) && timeout-- > 0)
		;
	if (!timeout) {
		CPRINTF("USB: reset timeout\n");
		return;
	}
	/* TODO: Wait 3 PHY clocks before returning */
}

void usb_connect(void)
{
	print_later("usb_connect()", 0, 0, 0, 0, 0);
	GR_USB_DCTL &= ~DCTL_SFTDISCON;
}

void usb_disconnect(void)
{
	print_later("usb_disconnect()", 0, 0, 0, 0, 0);
	GR_USB_DCTL |= DCTL_SFTDISCON;
}

void usb_init(void)
{
	int i;

	/* TODO: Take this out if USB is known to always be present */
	if (!(GREG32(SWDP, FPGA_CONFIG) &
	      GC_CONST_SWDP_FPGA_CONFIG_USB_8X8CRYPTO)) {
		CPRINTF("This FPGA image has no USB support\n");
		return;
	}

	print_later("usb_init()", 0, 0, 0, 0, 0);

	/* TODO(crosbug.com/p/46813): Clean this up. Do only what's needed, and
	 * use meaningful constants instead of magic numbers. */
	GREG32(GLOBALSEC, DDMA0_REGION0_CTRL) = 0xffffffff;
	GREG32(GLOBALSEC, DDMA0_REGION1_CTRL) = 0xffffffff;
	GREG32(GLOBALSEC, DDMA0_REGION2_CTRL) = 0xffffffff;
	GREG32(GLOBALSEC, DDMA0_REGION3_CTRL) = 0xffffffff;
	GREG32(GLOBALSEC, DUSB0_REGION0_CTRL) = 0xffffffff;
	GREG32(GLOBALSEC, DUSB0_REGION1_CTRL) = 0xffffffff;
	GREG32(GLOBALSEC, DUSB0_REGION2_CTRL) = 0xffffffff;
	GREG32(GLOBALSEC, DUSB0_REGION3_CTRL) = 0xffffffff;

	/* Enable clocks */
	clock_enable_module(MODULE_USB, 1);

	/* TODO(crbug.com/496888): set up pinmux */
	gpio_config_module(MODULE_USB, 1);

	/* Make sure interrupts are disabled */
	GR_USB_GINTMSK = 0;
	GR_USB_DAINTMSK = 0;
	GR_USB_DIEPMSK = 0;
	GR_USB_DOEPMSK = 0;

	/* Select the correct PHY */
	GR_USB_GGPIO = GGPIO_WRITE(USB_CUSTOM_CFG_REG,
				   (USB_PHY_ACTIVE | USB_SEL_PHY0));

	/* Full-Speed Serial PHY */
	GR_USB_GUSBCFG = GUSBCFG_PHYSEL_FS | GUSBCFG_FSINTF_6PIN
		| GUSBCFG_TOUTCAL(7)
		/* FIXME: Magic number! 14 is for 15MHz! Use 9 for 30MHz */
		| GUSBCFG_USBTRDTIM(14);

	usb_softreset();

	GR_USB_GUSBCFG = GUSBCFG_PHYSEL_FS | GUSBCFG_FSINTF_6PIN
		| GUSBCFG_TOUTCAL(7)
		/* FIXME: Magic number! 14 is for 15MHz! Use 9 for 30MHz */
		| GUSBCFG_USBTRDTIM(14);

	/* Global + DMA configuration */
	/* TODO: What about the AHB Burst Length Field? It's 0 now. */
	GR_USB_GAHBCFG = GAHBCFG_DMA_EN | GAHBCFG_GLB_INTR_EN |
			 GAHBCFG_NP_TXF_EMP_LVL;

	/* Be in disconnected state until we are ready */
	usb_disconnect();

	/* Max speed: USB2 FS */
	GR_USB_DCFG = DCFG_DEVSPD_FS48 | DCFG_DESCDMA;

	/* Setup FIFO configuration */
	 setup_data_fifos();

	/* Device registers have been setup */
	GR_USB_DCTL |= DCTL_PWRONPRGDONE;
	udelay(10);
	GR_USB_DCTL &= ~DCTL_PWRONPRGDONE;

	/* Clear global NAKs */
	GR_USB_DCTL |= DCTL_CGOUTNAK | DCTL_CGNPINNAK;

	/* Clear any pending interrupts */
	for (i = 0; i < 16; i++) {
		GR_USB_DIEPINT(i) = 0xffffffff;
		GR_USB_DOEPINT(i) = 0xffffffff;
	}
	GR_USB_GINTSTS = 0xFFFFFFFF;

	/* Unmask some endpoint interrupt causes */
	GR_USB_DIEPMSK = DIEPMSK_EPDISBLDMSK | DIEPMSK_XFERCOMPLMSK;
	GR_USB_DOEPMSK = DOEPMSK_EPDISBLDMSK | DOEPMSK_XFERCOMPLMSK |
		DOEPMSK_SETUPMSK;

	/* Enable interrupt handlers */
	task_enable_irq(GC_IRQNUM_USB0_USBINTR);

	/* Allow USB interrupts to come in */
	GR_USB_GINTMSK =
		/* NAK bits that must be cleared by the DCTL register */
		GINTMSK(GOUTNAKEFF) | GINTMSK(GINNAKEFF) |
		/* Initialization events */
		GINTMSK(USBRST) | GINTMSK(ENUMDONE) |
		/* Endpoint activity, cleared by the DOEPINT/DIEPINT regs */
		GINTMSK(OEPINT) | GINTMSK(IEPINT) |
		/* Reset detected while suspended. Need to wake up. */
		GINTMSK(RESETDET) |
		/* Idle, Suspend detected. Should go to sleep. */
		GINTMSK(ERLYSUSP) | GINTMSK(USBSUSP);

#ifndef CONFIG_USB_INHIBIT_CONNECT
	/* Indicate our presence to the USB host */
	usb_connect();
#endif

	print_later("usb_init() done", 0, 0, 0, 0, 0);
}
#ifndef CONFIG_USB_INHIBIT_INIT
DECLARE_HOOK(HOOK_INIT, usb_init, HOOK_PRIO_DEFAULT);
#endif

void usb_release(void)
{
	/* signal disconnect to host */
	usb_disconnect();

	/* disable interrupt handlers */
	task_disable_irq(GC_IRQNUM_USB0_USBINTR);

	/* disable clocks */
	clock_enable_module(MODULE_USB, 0);
	/* TODO: pin-mux */
}