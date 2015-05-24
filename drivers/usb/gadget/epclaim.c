/*
 * epclaim -- claim endpoint by name
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/types.h>
#include <linux/device.h>

#include <linux/ctype.h>
#include <linux/string.h>

#include <linux/usb/ch9.h>
#include <linux/usb/gadget.h>

/* #ifdef DEBUG
#error DEBUG actually defined
#else
#error DEBUG not defined
#endif
* 
by default, DEBUG not defined */

static struct usb_ep * __claim_ep_by_name(struct usb_gadget *gadget, struct usb_endpoint_descriptor *desc,
	char const *name, void *driver_data)
{
	u8 num, type, ep_type;
	struct usb_ep *ep;
	list_for_each_entry(ep, &gadget->ep_list, ep_list) {
		if (strcmp(name, ep->name) == 0) {
			num = ep->name[2] - '0';
			if (num < 1 || num > 5) {
				printk(KERN_ERR "__claim_ep_by_name(): invalid endpoint number %d from name \"%s\"\n", (int)num, ep->name);
				return (struct usb_ep*)0;
			}
			type = desc->bmAttributes & USB_ENDPOINT_XFERTYPE_MASK;
			ep_type = ('b' == ep->name[4]) ? USB_ENDPOINT_XFER_BULK : USB_ENDPOINT_XFER_INT;
			if (ep_type != type) {
				printk(KERN_ERR "__claim_ep_by_name(): endpoint type mismatch: type(%d) != ep_type(%d)\n", (int)type, (int)ep_type);
				return (struct usb_ep*)0;
			}
			
			if (!!ep->driver_data) {
				printk(KERN_ERR "__claim_ep_by_name(): ep(0x%p, \"%s\") already claimed by 0x%p, trying driver_data(0x%p)\n",
					ep, ep->name, ep->driver_data, driver_data);
				return (struct usb_ep*)0;
			}
			ep->driver_data = driver_data; /* claim the endpoint */
			
			/* assume USB_SPEED_FULL */
			switch (type) {
				case USB_ENDPOINT_XFER_BULK:
					desc->wMaxPacketSize = 64;
					break;
				case USB_ENDPOINT_XFER_INT:
					if (!desc->wMaxPacketSize) /* f_rndis set it to STATUS_BYTECOUNT */
						desc->wMaxPacketSize = 64;
					break;
				default:
					desc->wMaxPacketSize = cpu_to_le16(ep->maxpacket);
			}
			
			/* bEndpointAddress 1 byte
			 * Endpoint The address of the endpoint on the USB device described by this descriptor.
			 * The address is encoded as follows:
			 * * Bit 3...0: The endpoint number
			 * * Bit 6...4: Reserved, reset to zero
			 * * Bit 7: Direction, ignored for control endpoints
			 * * * 0 = OUT endpoint
			 * * * 1 = IN endpoint */
			/* reset bitset, keep direction(USB_DIR_IN : 0x80 : 10000000) if it set */
			desc->bEndpointAddress &= USB_DIR_IN;
			/* set endpoint number */
			desc->bEndpointAddress |= num;
			ep->address = desc->bEndpointAddress;
/* #ifdef DEBUG */
			printk(KERN_INFO "__claim_ep_by_name(): ep(0x%p, \"%s\") successfully claimed by driver_data(0x%p)\n",
				ep, ep->name, ep->driver_data);
/* #endif */
			return ep;
		}
	}
	printk(KERN_ERR "__claim_ep_by_name(): endpoint \"%s\" not found\n", name);
	return (struct usb_ep*)0;
	
	/* include/generated/autoconf.h:
	 * #define CONFIG_USB_GADGET 1
	 * #define CONFIG_USB_GADGET_STORAGE_NUM_BUFFERS 2
	 * #define CONFIG_USB_GADGET_VBUS_DRAW 2
	 * so, CONFIG_USB_GADGET_DUALSPEED || CONFIG_USB_GADGET_SUPERSPEED not defined,
	 * i.e. gadget should works on FULLSPEED - usb 1.1, 12 Mbit/s
	 * 
	 * USB_ENDPOINT_XFER_INT:
	 * 5.7.3 Interrupt Transfer Packet Size Constraints
	 * An endpoint for an interrupt pipe specifies the maximum size data payload that it will transmit or receive.
	 * The maximum allowable interrupt data payload size is 64 bytes or less for full-speed.
	 * High-speed endpoints are allowed maximum data payload sizes up to 1024 bytes.
	 * A high speed, high bandwidth endpoint specifies whether it requires two or three transactions per microframe.
	 * Low-speed devices are limited to eight bytes or less maximum data payload size.
	 * This maximum applies to the data payloads of the data packets; i.e., the size specified is for the data field of the packet 
	 * as defined in Chapter 8, not including other protocol-required information.
	 * The USB does not require that data packets be exactly the maximum size;
	 * i.e., if a data packet is less than the maximum, it does not need to be padded to the maximum size.
	 * INT: limit 64 bytes full speed, 1024 high/super speed
	 * 
	 * USB_ENDPOINT_XFER_BULK:
	 * 5.8.3 Bulk Transfer Packet Size Constraints
	 * An endpoint for bulk transfers specifies the maximum data payload size that the endpoint can accept from
	 * or transmit to the bus. The USB defines the allowable maximum bulk data payload sizes to be only 8, 16,
	 * 32, or 64 bytes for full-speed endpoints and 512 bytes for high-speed endpoints.
	 * A low-speed device must not have bulk endpoints.
	 * This maximum applies to the data payloads of the data packets; i.e., the size specified is for the data field of the packet
	 * as defined in Chapter 8, not including other protocol-required information.
	 * A bulk endpoint is designed to support a maximum data payload size.
	 * A bulk endpoint reports in its configuration information the value for its maximum data payload size.
	 * The USB does not require that data payloads transmitted be exactly the maximum size;
	 * i.e., if a data payload is less than the maximum, it does not need to be padded to the maximum size. */
}

#if 0

/*
 * This should work with endpoints from controller drivers sharing the
 * same endpoint naming convention.  By example:
 *
 *	- ep1, ep2, ... address is fixed, not direction or type
 *	- ep1in, ep2out, ... address and direction are fixed, not type
 *	- ep1-bulk, ep2-bulk, ... address and type are fixed, not direction
 *	- ep1in-bulk, ep2out-iso, ... all three are fixed
 *	- ep-* ... no functionality restrictions
 *
 * Type suffixes are "-bulk", "-iso", or "-int".  Numbers are decimal.
 * Less common restrictions are implied by gadget_is_*().
 *
 * NOTE:  each endpoint is unidirectional, as specified by its USB
 * descriptor; and isn't specific to a configuration or altsetting.
 */
static int
ep_matches (
	struct usb_gadget		*gadget,
	struct usb_ep			*ep,
	struct usb_endpoint_descriptor	*desc,
	struct usb_ss_ep_comp_descriptor *ep_comp
)
{
	u8		type;
	const char	*tmp;
	u16		max;

	int		num_req_streams = 0;

	/* endpoint already claimed? */
	if (NULL != ep->driver_data) {
	    printk("%s, wrn: endpoint already claimed, ep(0x%p, 0x%p, %s)\n", __func__,
	           ep, ep->driver_data, ep->name);
		return 0;
	}

	/* only support ep0 for portable CONTROL traffic */
	type = desc->bmAttributes & USB_ENDPOINT_XFERTYPE_MASK;
	if (USB_ENDPOINT_XFER_CONTROL == type)
		return 0;

	/* some other naming convention */
	if ('e' != ep->name[0])
		return 0;

	/* type-restriction:  "-iso", "-bulk", or "-int".
	 * direction-restriction:  "in", "out".
	 */
	if ('-' != ep->name[2]) {
		tmp = strrchr (ep->name, '-');
		if (tmp) {
			switch (type) {
			case USB_ENDPOINT_XFER_INT:
				/* bulk endpoints handle interrupt transfers,
				 * except the toggle-quirky iso-synch kind
				 */
				if ('s' == tmp[2]){	// == "-iso"
				    printk("-iso\n");
					return 0;
				}

				/* for now, avoid PXA "interrupt-in";
				 * it's documented as never using DATA1.
				 */
				if (gadget_is_pxa (gadget) && 'i' == tmp [1]){
				    printk("11111\n");
					return 0;
				}

				break;
			case USB_ENDPOINT_XFER_BULK:
				if ('b' != tmp[1])	// != "-bulk"
					return 0;
				break;
			case USB_ENDPOINT_XFER_ISOC:
				if ('s' != tmp[2])	// != "-iso"
					return 0;
			}
		} else {
			tmp = ep->name + strlen (ep->name);
		}

		/* direction-restriction:  "..in-..", "out-.." */
		tmp--;
		if (!isdigit (*tmp)) {
			if (desc->bEndpointAddress & USB_DIR_IN) {
				if ('n' != *tmp)
					return 0;
			} else {
				if ('t' != *tmp)
					return 0;
			}
		}
	}

	/*
	 * Get the number of required streams from the EP companion
	 * descriptor and see if the EP matches it
	 */
	if (usb_endpoint_xfer_bulk(desc)) {
		if (ep_comp && gadget->max_speed >= USB_SPEED_SUPER) {
			num_req_streams = ep_comp->bmAttributes & 0x1f;
			if (num_req_streams > ep->max_streams)
				return 0;
		}

	}

	/*
	 * If the protocol driver hasn't yet decided on wMaxPacketSize
	 * and wants to know the maximum possible, provide the info.
	 */
	if (desc->wMaxPacketSize == 0)
		desc->wMaxPacketSize = cpu_to_le16(ep->maxpacket);

	/* endpoint maxpacket size is an input parameter, except for bulk
	 * where it's an output parameter representing the full speed limit.
	 * the usb spec fixes high speed bulk maxpacket at 512 bytes.
	 */
	max = 0x7ff & usb_endpoint_maxp(desc);
	switch (type) {
	case USB_ENDPOINT_XFER_INT:
		/* INT:  limit 64 bytes full speed, 1024 high/super speed */
		if (!gadget_is_dualspeed(gadget) && max > 64)
			return 0;
		/* FALLTHROUGH */

	case USB_ENDPOINT_XFER_ISOC:
		/* ISO:  limit 1023 bytes full speed, 1024 high/super speed */
		if (ep->maxpacket < max)
			return 0;
		if (!gadget_is_dualspeed(gadget) && max > 1023)
			return 0;

		/* BOTH:  "high bandwidth" works only at high speed */
		if ((desc->wMaxPacketSize & cpu_to_le16(3<<11))) {
			if (!gadget_is_dualspeed(gadget))
				return 0;
			/* configure your hardware with enough buffering!! */
		}
		break;
	}

	/* MATCH!! */

	/* report address */
	desc->bEndpointAddress &= USB_DIR_IN;
	if (isdigit (ep->name [2])) {
		u8	num = simple_strtoul (&ep->name [2], NULL, 10);
		desc->bEndpointAddress |= num;
#ifdef	MANY_ENDPOINTS
	} else if (desc->bEndpointAddress & USB_DIR_IN) {
		if (++in_epnum > 15)
			return 0;
		desc->bEndpointAddress = USB_DIR_IN | in_epnum;
#endif
	} else {
		if (++epnum > 15)
			return 0;
		desc->bEndpointAddress |= epnum;
	}

	/* report (variable) full speed bulk maxpacket */
	if ((USB_ENDPOINT_XFER_BULK == type) && !ep_comp) {
		int size = ep->maxpacket;

		/* min() doesn't work on bitfields with gcc-3.5 */
		if (size > 64)
			size = 64;
		desc->wMaxPacketSize = cpu_to_le16(size);
	}
	ep->address = desc->bEndpointAddress;
	return 1;
}

#endif
