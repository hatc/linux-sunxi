/*
 * multi.c -- Multifunction Composite driver
 *
 * Copyright (C) 2008 David Brownell
 * Copyright (C) 2008 Nokia Corporation
 * Copyright (C) 2009 Samsung Electronics
 * Author: Michal Nazarewicz (mina86@mina86.com)
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#include <linux/kernel.h>
#include <linux/utsname.h>
#include <linux/module.h>

#ifdef USB_ETH_RNDIS
#undef USB_ETH_RNDIS
#endif
#define USB_ETH_RNDIS y /* USB_ETH_RNDIS defined just for u_ether.h */
#include "u_ether.h"

#define DRIVER_DESC "Pico Composite Gadget"
#define PICO_DRIVER_VERSION "0.1.0 pre-alpha"

MODULE_DESCRIPTION(DRIVER_DESC);
MODULE_AUTHOR("Michal Nazarewicz and Ko");
MODULE_LICENSE("GPL");

/***************************** All the files... *****************************/

/*
 * kbuild is not very cooperative with respect to linking separately
 * compiled library objects into one module.  So for now we won't use
 * separate compilation ... ensuring init/exit sections work to shrink
 * the runtime footprint, and giving us at least some parts of what
 * a "gcc --combine ... part1.c part2.c part3.c ... " build would.
 */

#define ENABLE_PICO_DBG 1

#include "composite.c"
#include "usbstring.c"
#include "config.c"
#include "epclaim.c"
#include "epautoconf.c"

#define USE__CLAIM_EP_BY_NAME 1
#define USE_WIRELESS_RNDIS_CLASS 1
#include "f_rndis.c"
#include "rndis.c"
#include "f_mass_storage.c"

#include "u_ether.c" /* u_ether.c redefine *DBG macros, so include it after all */

/***************************** Device Descriptor ****************************/

#define MULTI_VENDOR_NUM	0x1d6b	/* Linux Foundation */
/* #define MULTI_PRODUCT_NUM	0x0104	Multifunction Composite Gadget */
#define MULTI_PRODUCT_NUM	0x0109	/* unknown composite device ^_^ */

enum {
	__MULTI_NO_CONFIG,
	MULTI_RNDIS_CONFIG_NUM, /* usb_configuration.bConfigurationValue : should be just a unique per usb_composite_dev, not zero, unsigned char value */
};

static struct usb_device_descriptor device_desc = {
	.bLength         = sizeof device_desc,
	.bDescriptorType = USB_DT_DEVICE,

	.bcdUSB          = cpu_to_le16(0x0200),

	.bDeviceClass    = 0xEF /* USB_CLASS_MISC */,
	.bDeviceSubClass = 2,
	.bDeviceProtocol = 1,

	/* Vendor and product id can be overridden by module parameters. */
	.idVendor        = cpu_to_le16(MULTI_VENDOR_NUM),
	.idProduct       = cpu_to_le16(MULTI_PRODUCT_NUM),
	
	.bNumConfigurations = 1,
};

static const struct usb_descriptor_header *otg_desc[] = {
	(struct usb_descriptor_header *) &(struct usb_otg_descriptor){
		.bLength         = sizeof(struct usb_otg_descriptor),
		.bDescriptorType = USB_DT_OTG,

		/*
		 * REVISIT SRP-only hardware is possible, although
		 * it would not be called "OTG" ...
		 */
		.bmAttributes    = USB_OTG_SRP | USB_OTG_HNP,
	},
	NULL,
};

enum {
	MULTI_STRING_RNDIS_CONFIG_IDX,
	MULTI_STRING_OS_STRING_DESC_IDX, /* actual OS string descriptor must returns hack in composite_setup(), but keep this entry just in case */
	MULTI_STRING_PRODUCT_IDX,
	MULTI_STRING_MANUFACTURER_IDX
};

/* usb_string strings_dev[] = { [IDX].s = "IDX text", } - strings_dev[IDX].s = "IDX text" 
 * i.e. IDX used as index in declared array */
/* struct usb_string { u8 id; const char *s; },
 * so strings_dev[] = { [MULTI_STRING_RNDIS_CONFIG_IDX].s = "Multifunction with RNDIS" }
 * just define strings_dev[1] = { { .id = default(u8) == '\0', .s = "Multifunction with RNDIS" } }
 * actual strings id will be allocated by usb_string_ids() call, BUT theirs array ids(IDX) will be the same */
static char MULTI_STRINGS_DEV_MANUFACTURER[0x100];
static struct usb_string strings_dev[] = {
	[MULTI_STRING_RNDIS_CONFIG_IDX].s   = "Pico configuration with RNDIS and Mass Storage",
	[MULTI_STRING_OS_STRING_DESC_IDX].s = "MSFT100\x06",
	[MULTI_STRING_PRODUCT_IDX].s        = DRIVER_DESC,
	[MULTI_STRING_MANUFACTURER_IDX].s   = MULTI_STRINGS_DEV_MANUFACTURER,
	{  } /* end of list */
};

static struct usb_gadget_strings *dev_strings[] = {
	&(struct usb_gadget_strings){
		.language	= 0x0409,	/* en-us */
		.strings	= strings_dev,
	},
	NULL,
};

/****************************** Configurations ******************************/

static struct fsg_module_parameters fsg_mod_data = { .stall = 1 };
FSG_MODULE_PARAMETERS(/* no prefix */, fsg_mod_data);

static struct fsg_common fsg_common;

static u8 hostaddr[ETH_ALEN];

/********** RNDIS **********/

static __init int rndis_do_config(struct usb_configuration *c)
{
	int ret;

	if (gadget_is_otg(c->cdev->gadget)) {
		c->descriptors = otg_desc;
		c->bmAttributes |= USB_CONFIG_ATT_WAKEUP;
	}
	
	/* rndis->port.func.bind = rndis_bind
	rndis_bind_config() calls usb_add_function(... &rndis->port.func); */
	ret = rndis_bind_config(c, hostaddr);
	if (ret < 0)
		return ret;
	/* fsg->function.bind = fsg_bind;
	fsg_bind_config() calls usb_add_function(... &fsg->function); */
	ret = fsg_bind_config(c->cdev, c, &fsg_common);
	if (ret < 0)
		return ret;

	return 0;
}

static int rndis_config_register(struct usb_composite_dev *cdev)
{
	/* config_desc() use speed from gadget only for gadget->speed == USB_SPEED_SUPER && gadget_is_dualspeed(gadget)
	 * in other cases, list_for_each_entry(c, &cdev->configs, list) - list through usb_configuration's */
	static struct usb_configuration config = {
		.bConfigurationValue = MULTI_RNDIS_CONFIG_NUM, /* 1 */
		.bmAttributes        = USB_CONFIG_ATT_SELFPOWER,
	};
	
	config.label          = strings_dev[MULTI_STRING_RNDIS_CONFIG_IDX].s;
	/* usb_string id's already allocated with usb_string_ids_tab() */
	config.iConfiguration = strings_dev[MULTI_STRING_RNDIS_CONFIG_IDX].id;

	return usb_add_config(cdev, &config, rndis_do_config);
}

/****************************** Gadget Bind ******************************/

static int __ref multi_bind(struct usb_composite_dev *cdev)
{
	struct usb_gadget *gadget = cdev->gadget;
	int status, gcnum;
	
	if (!can_support_ecm(cdev->gadget)) {
		dev_err(&gadget->dev, "controller '%s' not usable\n",
		        gadget->name);
		return -EINVAL;
	}

	/* set up network link layer */
	status = gether_setup(cdev->gadget, hostaddr);
	if (status < 0)
		return status;

	/* set up mass storage function */
	{
		void *retp;
		retp = fsg_common_from_params(&fsg_common, cdev, &fsg_mod_data);
		if (IS_ERR(retp)) {
			status = PTR_ERR(retp);
			gether_cleanup();
			return status;
		}
	}

	/* set bcdDevice */
	gcnum = usb_gadget_controller_number(gadget);
	if (gcnum >= 0) {
		device_desc.bcdDevice = cpu_to_le16(0x0300 | gcnum);
	} else {
	/*
	 * void dev_warn(const struct device *dev, const char *fmt, ...);
	 * 
	 * address-of(&) operator has lower precedence than member selection(->), so
	 * `&gadget->dev` or `&(d)->gadget->dev` it's &(gadget->dev) - address-of dev member
	 * 
	 * struct usb_gadget {
	 *      ...
	 *      struct device                   dev;
	 *      ...
	 * }
	 */ 
		dev_warn(&gadget->dev, "controller '%s' not recognized\n", gadget->name);
		device_desc.bcdDevice = cpu_to_le16(0x0300 | 0x0099);
	}
	
	/* allocate string IDs */
	status = usb_string_ids_tab(cdev, strings_dev);
	if (unlikely(status < 0)) {
		fsg_common_put(&fsg_common);
		gether_cleanup();
		return status;
	}
	
	strings_dev[MULTI_STRING_OS_STRING_DESC_IDX].id = 0xee;
	/* device descriptor strings: product, manufacturer
	 * iManufacturer : Specifies a device-defined index of the string descriptor that provides a string that contains the name of the manufacturer of this device.
	 * iProduct      : Specifies a device-defined index of the string descriptor that provides a string that contains a description of the device. */
	device_desc.iProduct = strings_dev[MULTI_STRING_PRODUCT_IDX].id;
	snprintf(MULTI_STRINGS_DEV_MANUFACTURER, sizeof(MULTI_STRINGS_DEV_MANUFACTURER) / sizeof(MULTI_STRINGS_DEV_MANUFACTURER[0]),
		"Pico and Ko with %s %s",
		init_utsname()->sysname, init_utsname()->release);
	device_desc.iManufacturer = strings_dev[MULTI_STRING_RNDIS_CONFIG_IDX].id;
	
	/* register configuration */
	status = rndis_config_register(cdev);
	if (unlikely(status < 0)) {
		fsg_common_put(&fsg_common);
		gether_cleanup();
		return status;
	}
	
	/* we're done */
	dev_info(&gadget->dev, "%s, version: " PICO_DRIVER_VERSION "\n", DRIVER_DESC);
	fsg_common_put(&fsg_common);
	return 0;
}

static int __exit multi_unbind(struct usb_composite_dev *cdev)
{
	gether_cleanup();
	return 0;
}

/* include/generated/autoconf.h:
 * #define CONFIG_USB_GADGET 1
 * #define CONFIG_USB_GADGET_STORAGE_NUM_BUFFERS 2
 * #define CONFIG_USB_GADGET_VBUS_DRAW 2
 * so, CONFIG_USB_GADGET_DUALSPEED || CONFIG_USB_GADGET_SUPERSPEED not defined,
 * i.e. gadget should works on FULLSPEED - usb 1.1, 12 Mbit/s */
static struct usb_composite_driver multi_driver = {
	.name		= "g_multi",
	.dev		= &device_desc,
	.strings	= dev_strings,
	/* usb_composite_probe(struct usb_composite_driver *driver, int (*bind)(struct usb_composite_dev *cdev)) {
	 ...
	 composite_driver.max_speed = min_t(u8, composite_driver.max_speed, driver->max_speed);
	 so, looks like .max_speed field actually used... */
	.max_speed	= USB_SPEED_HIGH, /* USB_SPEED_FULL, */
	.unbind		= __exit_p(multi_unbind),
	/* @needs_serial: set to 1 if the gadget needs userspace to provide a serial number.
	 * If one is not provided, warning will be printed. */
	.needs_serial	= 1,
};

static int __init multi_init(void)
{
	return usb_composite_probe(&multi_driver, multi_bind);
}
module_init(multi_init);

static void __exit multi_exit(void)
{
	usb_composite_unregister(&multi_driver);
}
module_exit(multi_exit);
