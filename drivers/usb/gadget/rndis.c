/*
 * RNDIS MSG parser
 *
 * Authors:	Benedikt Spranger, Pengutronix
 *		Robert Schwebel, Pengutronix
 *
 *              This program is free software; you can redistribute it and/or
 *              modify it under the terms of the GNU General Public License
 *              version 2, as published by the Free Software Foundation.
 *
 *		This software was originally developed in conformance with
 *		Microsoft's Remote NDIS Specification License Agreement.
 *
 * 03/12/2004 Kai-Uwe Bloem <linux-development@auerswald.de>
 *		Fixed message length bug in init_response
 *
 * 03/25/2004 Kai-Uwe Bloem <linux-development@auerswald.de>
 *		Fixed rndis_rm_hdr length bug.
 *
 * Copyright (C) 2004 by David Brownell
 *		updates to merge with Linux 2.6, better match RNDIS spec
 */

#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/init.h>
#include <linux/list.h>
#include <linux/proc_fs.h>
#include <linux/slab.h>
#include <linux/seq_file.h>
#include <linux/netdevice.h>

#include <linux/usb/cdc.h> /* USB_CDC_PACKET_TYPES */

#include <asm/io.h>
#include <asm/byteorder.h>
#include <asm/unaligned.h>

#undef	VERBOSE_DEBUG

#include "rndis.h"

/* The driver for your USB chip needs to support ep0 OUT to work with
 * RNDIS, plus all three CDC Ethernet endpoints (interrupt not optional).
 *
 * Windows hosts need an INF file like Documentation/usb/linux.inf
 * and will be happier if you provide the host_addr module parameter.
 */
#if 0
static int rndis_debug = 0;
module_param (rndis_debug, int, 0);
MODULE_PARM_DESC (rndis_debug, "enable debugging");
#else
#define rndis_debug		0
#endif

#define RNDIS_MAX_CONFIGS	1

static rndis_params rndis_per_dev_params[RNDIS_MAX_CONFIGS];

/* Driver Version */
static const __le32 rndis_driver_version = cpu_to_le32(1);

/* Function Prototypes */
static rndis_resp *rndis_add_response(int configNr, u32 length);

static int rndis_indicate_status_msg(int configNr, u32 status);
static int rndis_signal_connect_impl(int configNr);
int rndis_signal_connect(int configNr);
static int rndis_signal_disconnect_impl(int configNr);
int rndis_signal_disconnect(int configNr);

/* supported OIDs */
static const u32 oid_supported_list[] =
{
	/* the general stuff */
	OID_GEN_SUPPORTED_LIST,
	OID_GEN_HARDWARE_STATUS,
	OID_GEN_MEDIA_SUPPORTED,
	OID_GEN_MEDIA_IN_USE,
	OID_GEN_MAXIMUM_FRAME_SIZE,
	OID_GEN_LINK_SPEED,
	OID_GEN_TRANSMIT_BLOCK_SIZE,
	OID_GEN_RECEIVE_BLOCK_SIZE,
	OID_GEN_VENDOR_ID,
	OID_GEN_VENDOR_DESCRIPTION,
	OID_GEN_VENDOR_DRIVER_VERSION,
	OID_GEN_CURRENT_PACKET_FILTER,
	OID_GEN_MAXIMUM_TOTAL_SIZE,
	OID_GEN_MEDIA_CONNECT_STATUS,
	OID_GEN_PHYSICAL_MEDIUM,

	/* the statistical stuff */
	OID_GEN_XMIT_OK,
	OID_GEN_RCV_OK,
	OID_GEN_XMIT_ERROR,
	OID_GEN_RCV_ERROR,
	OID_GEN_RCV_NO_BUFFER,
#ifdef RNDIS_OPTIONAL_STATS
	OID_GEN_DIRECTED_BYTES_XMIT,
	OID_GEN_DIRECTED_FRAMES_XMIT,
	OID_GEN_MULTICAST_BYTES_XMIT,
	OID_GEN_MULTICAST_FRAMES_XMIT,
	OID_GEN_BROADCAST_BYTES_XMIT,
	OID_GEN_BROADCAST_FRAMES_XMIT,
	OID_GEN_DIRECTED_BYTES_RCV,
	OID_GEN_DIRECTED_FRAMES_RCV,
	OID_GEN_MULTICAST_BYTES_RCV,
	OID_GEN_MULTICAST_FRAMES_RCV,
	OID_GEN_BROADCAST_BYTES_RCV,
	OID_GEN_BROADCAST_FRAMES_RCV,
	OID_GEN_RCV_CRC_ERROR,
	OID_GEN_TRANSMIT_QUEUE_LENGTH,
#endif /* RNDIS_OPTIONAL_STATS */

	/* mandatory 802.3 */
	/* the general stuff */
	OID_802_3_PERMANENT_ADDRESS,
	OID_802_3_CURRENT_ADDRESS,
	OID_802_3_MULTICAST_LIST,
	OID_802_3_MAC_OPTIONS,
	OID_802_3_MAXIMUM_LIST_SIZE,

	/* the statistical stuff */
	OID_802_3_RCV_ERROR_ALIGNMENT,
	OID_802_3_XMIT_ONE_COLLISION,
	OID_802_3_XMIT_MORE_COLLISIONS,
#ifdef	RNDIS_OPTIONAL_STATS
	OID_802_3_XMIT_DEFERRED,
	OID_802_3_XMIT_MAX_COLLISIONS,
	OID_802_3_RCV_OVERRUN,
	OID_802_3_XMIT_UNDERRUN,
	OID_802_3_XMIT_HEARTBEAT_FAILURE,
	OID_802_3_XMIT_TIMES_CRS_LOST,
	OID_802_3_XMIT_LATE_COLLISIONS,
#endif	/* RNDIS_OPTIONAL_STATS */

#ifdef RNDIS_PM
	/* PM and wakeup are "mandatory" for USB, but the RNDIS specs
	 * don't say what they mean ... and the NDIS specs are often
	 * confusing and/or ambiguous in this context.  (That is, more
	 * so than their specs for the other OIDs.)
	 *
	 * FIXME someone who knows what these should do, please
	 * implement them!
	 */
	 
	/* if the miniport driver returns NDIS_STATUS_NOT_SUPPORTED in response to a query OID_PNP_CAPABILITIES,
	 * NDIS ignores the NDIS_ATTRIBUTE_NO_HALT_ON_SUSPEND flag and halts the network adapter 
	 * if the system goes into a low-power state. */

	/* power management */
	OID_PNP_CAPABILITIES,
	OID_PNP_QUERY_POWER,
	OID_PNP_SET_POWER,

#ifdef	RNDIS_WAKEUP
	/* wake up host */
	OID_PNP_ENABLE_WAKE_UP,
	OID_PNP_ADD_WAKE_UP_PATTERN,
	OID_PNP_REMOVE_WAKE_UP_PATTERN,
#endif	/* RNDIS_WAKEUP */
#endif	/* RNDIS_PM */
};

static u32 NDIS_PACKET_TYPES[]    = { NDIS_PACKET_TYPE_DIRECTED,    NDIS_PACKET_TYPE_MULTICAST,    NDIS_PACKET_TYPE_ALL_MULTICAST,    NDIS_PACKET_TYPE_PROMISCUOUS,    NDIS_PACKET_TYPE_BROADCAST };
static u16 USB_CDC_PACKET_TYPES[] = { USB_CDC_PACKET_TYPE_DIRECTED, USB_CDC_PACKET_TYPE_MULTICAST, USB_CDC_PACKET_TYPE_ALL_MULTICAST, USB_CDC_PACKET_TYPE_PROMISCUOUS, USB_CDC_PACKET_TYPE_BROADCAST };
static u16 NDIS2CDC_packet_filter(u32 v) {
	int i;
	u16 r = 0;
	
	for (i = 0; i < arraysize(NDIS_PACKET_TYPES); ++i) {
		if (NDIS_PACKET_TYPES[i] & v)
			r |= USB_CDC_PACKET_TYPES[i];
	}
	if (!!v & !r)
		r = USB_CDC_PACKET_TYPE_PROMISCUOUS; /* ^_^ */
	return r;
}

/* NDIS Functions */
static int gen_ndis_query_resp(int configNr, u32 OID, u8 *buf, unsigned buf_len,
	rndis_resp *r, u32 *resp_status)
{
	int retval = -ENOTSUPP;
	u32 length = 4;	/* usually */
	__le32 *outbuf;
	int i, count;
	rndis_query_cmplt_type *resp;
	struct net_device *net;
	struct rtnl_link_stats64 temp;
	const struct rtnl_link_stats64 *stats;
	
	if (!r)
		return -ENOMEM;
	resp = (rndis_query_cmplt_type *)r->buf;
	if (!resp)
		return -ENOMEM;

	/* response goes here, right after the header */
	outbuf = (__le32 *)&resp[1]; /* len(outbuf) == sizeof(oid_supported_list) i.e. quite a lot ... */
	resp->InformationBufferOffset = cpu_to_le32(16);

	net = rndis_per_dev_params[configNr].dev;
	stats = dev_get_stats(net, &temp);

	switch (OID) {

	/* general oids (table 4-1) */

	/* mandatory */
	case OID_GEN_SUPPORTED_LIST:
		pr_debug("%s: OID_GEN_SUPPORTED_LIST\n", __func__);
		length = sizeof(oid_supported_list);
		count  = length / sizeof(u32);
		for (i = 0; i < count; i++)
			outbuf[i] = cpu_to_le32(oid_supported_list[i]);
		retval = 0;
		break;

	/* mandatory */
	case OID_GEN_HARDWARE_STATUS:
		pr_debug("%s: OID_GEN_HARDWARE_STATUS\n", __func__);
		/* Bogus question!
		 * Hardware must be ready to receive high level protocols.
		 * BTW:
		 * reddite ergo quae sunt Caesaris Caesari
		 * et quae sunt Dei Deo!
		
		*outbuf = cpu_to_le32(0); */
		
		*outbuf = cpu_to_le32(rndis_per_dev_params[configNr].hw_state);
		retval = 0;
		break;

	/* mandatory */
	case OID_GEN_MEDIA_SUPPORTED:
		pr_debug("%s: OID_GEN_MEDIA_SUPPORTED\n", __func__);
		
		*outbuf = cpu_to_le32(rndis_per_dev_params[configNr].medium);
		retval = 0;
		break;

	/* mandatory */
	case OID_GEN_MEDIA_IN_USE:
		pr_debug("%s: OID_GEN_MEDIA_IN_USE\n", __func__);
		
		/* one medium, one transport... (maybe you do it better) */
		*outbuf = cpu_to_le32(rndis_per_dev_params[configNr].medium);
		retval = 0;
		break;

	/* mandatory */
	case OID_GEN_MAXIMUM_FRAME_SIZE:
		pr_debug("%s: OID_GEN_MAXIMUM_FRAME_SIZE\n", __func__);
		
		if (rndis_per_dev_params[configNr].dev) {
			*outbuf = cpu_to_le32(rndis_per_dev_params[configNr].dev->mtu);
			retval = 0;
		} else
			PICOERR("OID_GEN_MAXIMUM_FRAME_SIZE: no net_dev for config %d, returns RNDIS_STATUS_NOT_SUPPORTED\n",
				(int)configNr)
		break;

	/* mandatory */
	case OID_GEN_LINK_SPEED:
		if (rndis_debug > 1)
			pr_debug("%s: OID_GEN_LINK_SPEED\n", __func__);
		
		if (rndis_per_dev_params[configNr].media_state == NDIS_MEDIA_STATE_DISCONNECTED)
			*outbuf = cpu_to_le32(0);
		else
			*outbuf = cpu_to_le32(rndis_per_dev_params[configNr].speed);
		retval = 0;
		break;

	/* mandatory */
	case OID_GEN_TRANSMIT_BLOCK_SIZE:
		pr_debug("%s: OID_GEN_TRANSMIT_BLOCK_SIZE\n", __func__);
		
		if (rndis_per_dev_params[configNr].dev) {
			*outbuf = cpu_to_le32(rndis_per_dev_params[configNr].dev->mtu);
			retval = 0;
		} else
			PICOERR("OID_GEN_TRANSMIT_BLOCK_SIZE: no net_dev for config %d, returns RNDIS_STATUS_NOT_SUPPORTED\n",
				(int)configNr)
		break;

	/* mandatory */
	case OID_GEN_RECEIVE_BLOCK_SIZE:
		pr_debug("%s: OID_GEN_RECEIVE_BLOCK_SIZE\n", __func__);
		
		if (rndis_per_dev_params[configNr].dev) {
			*outbuf = cpu_to_le32(rndis_per_dev_params[configNr].dev->mtu);
			retval = 0;
		} else
			PICOERR("OID_GEN_RECEIVE_BLOCK_SIZE: no net_dev for config %d, returns RNDIS_STATUS_NOT_SUPPORTED\n",
				(int)configNr)
		break;

	/* mandatory */
	case OID_GEN_VENDOR_ID:
		pr_debug("%s: OID_GEN_VENDOR_ID\n", __func__);
		
		*outbuf = cpu_to_le32(rndis_per_dev_params[configNr].vendorID);
		retval = 0;
		break;

	/* mandatory */
	case OID_GEN_VENDOR_DESCRIPTION:
		if (rndis_per_dev_params[configNr].vendorDescr) {
			length = strlen(rndis_per_dev_params[configNr].vendorDescr);
			memcpy(outbuf, rndis_per_dev_params[configNr].vendorDescr, length);
		} else {
			length = 4;
			outbuf[0] = 0x74786574; /* "text" */
		}
		retval = 0;
		break;

	case OID_GEN_VENDOR_DRIVER_VERSION:		
		/* Created as LE */
		*outbuf = rndis_driver_version;
		retval = 0;
		break;

	/* mandatory */
	case OID_GEN_CURRENT_PACKET_FILTER:		
		*outbuf = cpu_to_le32(rndis_per_dev_params[configNr].saved_filter);
		retval = 0;
		break;

	/* mandatory */
	case OID_GEN_MAXIMUM_TOTAL_SIZE:		
		*outbuf = cpu_to_le32(RNDIS_MAX_TOTAL_SIZE);
		retval = 0;
		break;

	/* mandatory */
	case OID_GEN_MEDIA_CONNECT_STATUS:		
		*outbuf = cpu_to_le32(rndis_per_dev_params[configNr].media_state);
		retval = 0;
		break;

	case OID_GEN_PHYSICAL_MEDIUM:		
		/* *outbuf = 0; // NdisPhysicalMediumUnspecified */
		*outbuf = cpu_to_le32(14); /* NdisPhysicalMedium802_3 */
		retval = 0;
		break;

	/* The RNDIS specification is incomplete/wrong.   Some versions
	 * of MS-Windows expect OIDs that aren't specified there.  Other
	 * versions emit undefined RNDIS messages. DOCUMENT ALL THESE!
	 */
	case OID_GEN_MAC_OPTIONS: /* from WinME */		
		*outbuf = cpu_to_le32(NDIS_MAC_OPTION_RECEIVE_SERIALIZED | NDIS_MAC_OPTION_FULL_DUPLEX);
		retval = 0;
		break;

	/* statistics OIDs (table 4-2) */

	/* Frames transmitted without errors - Query Mandatory - General Statistics */
	case OID_GEN_XMIT_OK:		
		if (stats)
			*outbuf = cpu_to_le32(stats->tx_packets - stats->tx_errors - stats->tx_dropped);
		else
			*outbuf = 0;
		retval = 0;
		break;

	/* Frames received without errors - Query Mandatory - General Statistics */
	case OID_GEN_RCV_OK:
		if (stats)
			*outbuf = cpu_to_le32(stats->rx_packets - stats->rx_errors - stats->rx_dropped);
		else
			*outbuf = 0;
		retval = 0;
		break;

	/* mandatory */
	case OID_GEN_XMIT_ERROR:
		if (stats)
			*outbuf = cpu_to_le32(stats->tx_errors);
		else
			*outbuf = 0;
		retval = 0;
		break;

	/* mandatory */
	case OID_GEN_RCV_ERROR:
		if (stats)
			*outbuf = cpu_to_le32(stats->rx_errors);
		else
			*outbuf = 0;
		retval = 0;
		break;

	/* mandatory */
	case OID_GEN_RCV_NO_BUFFER:		
		if (stats)
			*outbuf = cpu_to_le32(stats->rx_dropped);
		else
			*outbuf = 0;
		retval = 0;
		break;

	/* ieee802.3 OIDs (table 4-3) */

	/* mandatory */
	case OID_802_3_PERMANENT_ADDRESS:		
		if (rndis_per_dev_params[configNr].dev) {
			length = ETH_ALEN;
			memcpy(outbuf, rndis_per_dev_params[configNr].host_mac, length);
			retval = 0;
		} else
			PICOERR("OID_802_3_PERMANENT_ADDRESS: no net_dev(0x%p)\n",
				rndis_per_dev_params[configNr].dev)
		break;

	/* mandatory */
	case OID_802_3_CURRENT_ADDRESS:		
		if (rndis_per_dev_params[configNr].dev) {
			length = ETH_ALEN;
			memcpy(outbuf, rndis_per_dev_params[configNr].host_mac, length);
			retval = 0;
		} else
			PICOERR("OID_802_3_CURRENT_ADDRESS: no net_dev(0x%p)\n",
				rndis_per_dev_params[configNr].dev)
		break;
	
	/* mandatory */
	case OID_802_3_MULTICAST_LIST:
		pr_debug("%s: OID_802_3_MULTICAST_LIST\n", __func__);
		
		/* Multicast base address only
		*outbuf = cpu_to_le32(0xE0000000); */
		if (rndis_per_dev_params[configNr].multicast_addr_set) {
			length = ETH_ALEN;
			memcpy(outbuf, &rndis_per_dev_params[configNr].multicast_addr[0], length);
		} else {
			length = 0;
			memset(&rndis_per_dev_params[configNr].multicast_addr[0], 0, ETH_ALEN);
		}
		retval = 0;
		break;

	/* mandatory */
	case OID_802_3_MAXIMUM_LIST_SIZE:
		pr_debug("%s: OID_802_3_MAXIMUM_LIST_SIZE\n", __func__);
		
		/* Multicast base address only */
		*outbuf = cpu_to_le32(1);
		retval = 0;
		break;

	case OID_802_3_MAC_OPTIONS:
		pr_debug("%s: OID_802_3_MAC_OPTIONS\n", __func__);
		
		*outbuf = cpu_to_le32(0);
		retval = 0;
		break;

	/* ieee802.3 statistics OIDs (table 4-4) */

	/* mandatory */
	case OID_802_3_RCV_ERROR_ALIGNMENT:
		pr_debug("%s: OID_802_3_RCV_ERROR_ALIGNMENT\n", __func__);
		
		if (stats)
			*outbuf = cpu_to_le32(stats->rx_frame_errors);
		else
			*outbuf = 0;
		retval = 0;
		break;

	/* mandatory */
	case OID_802_3_XMIT_ONE_COLLISION:
		pr_debug("%s: OID_802_3_XMIT_ONE_COLLISION\n", __func__);
		*outbuf = cpu_to_le32(0);
		retval = 0;
		break;

	/* mandatory */
	case OID_802_3_XMIT_MORE_COLLISIONS:
		pr_debug("%s: OID_802_3_XMIT_MORE_COLLISIONS\n", __func__);
		*outbuf = cpu_to_le32(0);
		retval = 0;
		break;

	default:
		PICOWRN("query unknown OID 0x%08X\n", OID)
	}
	if (retval < 0)
		length = 0;

	resp->InformationBufferLength = cpu_to_le32(length);
	r->length = length + sizeof(*resp);
	resp->MessageLength = cpu_to_le32(r->length);
	return retval;
}

static u32 gen_ndis_set_resp(u8 configNr, u32 OID, u8 *buf, u32 buf_len,
	rndis_resp *r)
{
	u32 saved_filter, filter;
	rndis_set_cmplt_type *resp;
	rndis_params *params = &rndis_per_dev_params[configNr];
	
	if (!r) {
		PICOERR("!r")
		return RNDIS_STATUS_FAILURE;
	}
	resp = (rndis_set_cmplt_type *)r->buf;
	if (!resp) {
		PICOERR("!resp")
		return RNDIS_STATUS_FAILURE;
	}
	
	switch (OID) {
	case OID_GEN_CURRENT_PACKET_FILTER:
		/* these NDIS_PACKET_TYPE_* bitflags are shared with
		 * cdc_filter; it's not RNDIS-specific
		 * NDIS_PACKET_TYPE_x == USB_CDC_PACKET_TYPE_x for x in:
		 *	PROMISCUOUS, DIRECTED,
		 *	MULTICAST, ALL_MULTICAST, BROADCAST
		 */
		filter = *params->filter;
		saved_filter = params->saved_filter;
		
		params->saved_filter = get_unaligned_le32(buf);
		*params->filter = NDIS2CDC_packet_filter(params->saved_filter);
		
		PICODBG("saved_filter(%08X), filter(%08X) -> saved_filter(%08X), filter(%08X)\n",
			saved_filter, filter, params->saved_filter, (u32)*params->filter)
		
		pr_debug("%s: OID_GEN_CURRENT_PACKET_FILTER %08x\n", __func__, *params->filter);

		/* this call has a significant side effect:
		 * it's what makes the packet flow start and stop,
		 * like activating the CDC Ethernet altsetting. */
		if (*params->filter) {
			params->state = RNDIS_DATA_INITIALIZED;
			netif_carrier_on(params->dev);
			if (netif_running(params->dev))
				netif_wake_queue(params->dev);
		} else {
			params->state = RNDIS_INITIALIZED;
			netif_carrier_off(params->dev);
			netif_stop_queue(params->dev);
		}
		return RNDIS_STATUS_SUCCESS;

	case OID_802_3_MULTICAST_LIST:
		/* I think we can ignore this */
		pr_debug("%s: OID_802_3_MULTICAST_LIST\n", __func__);
		
		/* buf_len == InformationBufferLength */
		if (buf_len > ETH_ALEN)
			return RNDIS_STATUS_INVALID_DATA; /* NDIS_STATUS_MULTICAST_FULL; */
		
		params->multicast_addr_set = !!buf_len;
		if (params->multicast_addr_set)
			memcpy(&rndis_per_dev_params[configNr].multicast_addr[0], buf, ETH_ALEN);
		return RNDIS_STATUS_SUCCESS;

	default:
		PICOWRN("unknown OID 0x%08X, size %u\n", OID, buf_len);
	}
	return RNDIS_STATUS_NOT_SUPPORTED;
}

/*
 * Response Functions
 */

static int rndis_init_response(int configNr, rndis_init_msg_type *buf)
{
	rndis_init_cmplt_type *resp;
	rndis_resp *r;
	rndis_params *params = rndis_per_dev_params + configNr;

	if (!params->dev) {
		PICOERR("REMOTE_NDIS_INITIALIZE_MSG: no net_dev for config %d\n",
			configNr);
		return -ENOTSUPP;
	}

	r = rndis_add_response(configNr, sizeof(rndis_init_cmplt_type));
	if (!r)
		return -ENOMEM;
	resp = (rndis_init_cmplt_type *)r->buf;

	resp->MessageType = cpu_to_le32(REMOTE_NDIS_INITIALIZE_CMPLT);
	resp->MessageLength = cpu_to_le32(52);
	resp->RequestID = buf->RequestID; /* Still LE in msg buffer */
	resp->Status = cpu_to_le32(RNDIS_STATUS_SUCCESS);
	resp->MajorVersion = cpu_to_le32(RNDIS_MAJOR_VERSION);
	resp->MinorVersion = cpu_to_le32(RNDIS_MINOR_VERSION);
	resp->DeviceFlags = cpu_to_le32(RNDIS_DF_CONNECTIONLESS);
	resp->Medium = cpu_to_le32(RNDIS_MEDIUM_802_3);
	resp->MaxPacketsPerTransfer = cpu_to_le32(1);
	resp->MaxTransferSize = cpu_to_le32(
		  params->dev->mtu
		+ sizeof(struct ethhdr)
		+ sizeof(struct rndis_packet_msg_type)
		+ 22);
	resp->PacketAlignmentFactor = cpu_to_le32(0);
	resp->AFListOffset = cpu_to_le32(0);
	resp->AFListSize = cpu_to_le32(0);

	params->resp_avail(params->v);	
	return 0;
}

static int rndis_query_response(int configNr, rndis_query_msg_type *buf)
{
	rndis_query_cmplt_type *resp;
	rndis_resp *r;
	rndis_params *params = rndis_per_dev_params + configNr;
	u32 resp_status = RNDIS_STATUS_NOT_SUPPORTED;
	
	if (!params->dev) {
		PICOERR("REMOTE_NDIS_QUERY_MSG: no net_dev for config %d\n",
			configNr)
		return -ENOTSUPP;
	}

	/*
	 * we need more memory:
	 * gen_ndis_query_resp expects enough space for
	 * rndis_query_cmplt_type followed by data.
	 * oid_supported_list is the largest data reply
	 */
	r = rndis_add_response(configNr, sizeof(oid_supported_list) + sizeof(rndis_query_cmplt_type));
	if (!r) {
		PICOERR("!r")
		return -ENOMEM;
	}
	resp = (rndis_query_cmplt_type *)r->buf;

	resp->MessageType = cpu_to_le32(REMOTE_NDIS_QUERY_CMPLT);
	resp->RequestID = buf->RequestID; /* Still LE in msg buffer */

	if (gen_ndis_query_resp(configNr, le32_to_cpu(buf->OID),
			le32_to_cpu(buf->InformationBufferOffset)
					+ 8 + (u8 *)buf,
			le32_to_cpu(buf->InformationBufferLength),
			r, &resp_status)) {
		/* OID not supported */
		resp->Status = cpu_to_le32(resp_status);
		resp->MessageLength = cpu_to_le32(sizeof *resp);
		resp->InformationBufferLength = cpu_to_le32(0);
		resp->InformationBufferOffset = cpu_to_le32(0);
	} else
		resp->Status = cpu_to_le32(RNDIS_STATUS_SUCCESS);

	params->resp_avail(params->v);
	return 0;
}

static int rndis_set_response(int configNr, rndis_set_msg_type *buf)
{
	u32 BufLength, BufOffset;
	rndis_set_cmplt_type *resp;
	rndis_resp *r;
	rndis_params *params = rndis_per_dev_params + configNr;

	r = rndis_add_response(configNr, sizeof(rndis_set_cmplt_type));
	if (!r) {
		PICOERR("!r\n")
		return -ENOMEM;
	}
	resp = (rndis_set_cmplt_type *)r->buf;

	BufLength = le32_to_cpu(buf->InformationBufferLength);
	BufOffset = le32_to_cpu(buf->InformationBufferOffset);

#ifdef	VERBOSE_DEBUG
	pr_debug("%s: Length: %d\n", __func__, BufLength);
	pr_debug("%s: Offset: %d\n", __func__, BufOffset);
	pr_debug("%s: InfoBuffer: ", __func__);

	for (i = 0; i < BufLength; i++) {
		pr_debug("%02x ", *(((u8 *) buf) + i + 8 + BufOffset));
	}

	pr_debug("\n");
#endif

	resp->MessageType = cpu_to_le32(REMOTE_NDIS_SET_CMPLT);
	resp->MessageLength = cpu_to_le32(16);
	resp->RequestID = buf->RequestID; /* Still LE in msg buffer */
	resp->Status = cpu_to_le32(gen_ndis_set_resp(configNr, le32_to_cpu(buf->OID), ((u8 *)buf) + 8 + BufOffset, BufLength, r));
	
	params->resp_avail(params->v);
	return 0;
}

static int rndis_reset_response(int configNr, rndis_reset_msg_type *buf)
{
	rndis_reset_cmplt_type *resp;
	rndis_resp *r;
	rndis_params *params = rndis_per_dev_params + configNr;

	r = rndis_add_response(configNr, sizeof(rndis_reset_cmplt_type));
	if (!r) {
		PICOERR("!r")
		return -ENOMEM;
	}
	resp = (rndis_reset_cmplt_type *)r->buf;

	resp->MessageType = cpu_to_le32(REMOTE_NDIS_RESET_CMPLT);
	resp->MessageLength = cpu_to_le32(16);
	resp->Status = cpu_to_le32(RNDIS_STATUS_SUCCESS);
	/* resent information */
	resp->AddressingReset = cpu_to_le32(0); /*  If the device requires the host to resend addressing information, set this field to one */

	params->resp_avail(params->v);
	return 0;
}

static int rndis_keepalive_response(int configNr, rndis_keepalive_msg_type *buf)
{
	rndis_keepalive_cmplt_type *resp;
	rndis_resp *r;
	rndis_params *params = rndis_per_dev_params + configNr;

	/* host "should" check only in RNDIS_DATA_INITIALIZED state */

	r = rndis_add_response(configNr, sizeof(rndis_keepalive_cmplt_type));
	if (!r) {
		PICOERR("!r")
		return -ENOMEM;
	}
	resp = (rndis_keepalive_cmplt_type *)r->buf;

	resp->MessageType = cpu_to_le32(REMOTE_NDIS_KEEPALIVE_CMPLT);
	resp->MessageLength = cpu_to_le32(16);
	resp->RequestID = buf->RequestID; /* Still LE in msg buffer */
	resp->Status = cpu_to_le32(RNDIS_STATUS_SUCCESS);

	params->resp_avail(params->v);
	return 0;
}

/*
 * Device to Host Comunication
 */
static int rndis_indicate_status_msg(int configNr, u32 status)
{
	rndis_indicate_status_msg_type *resp;
	rndis_resp *r;
	rndis_params *params = rndis_per_dev_params + configNr;

	if (params->state == RNDIS_UNINITIALIZED) {
		PICOWRN("REMOTE_NDIS_INDICATE_STATUS_MSG: params->state == RNDIS_UNINITIALIZED\n")
		return -ENOTSUPP;
	}

	r = rndis_add_response(configNr, sizeof(rndis_indicate_status_msg_type));
	if (!r) {
		PICOERR("!r")
		return -ENOMEM;
	}
	resp = (rndis_indicate_status_msg_type *)r->buf;

	resp->MessageType = cpu_to_le32(REMOTE_NDIS_INDICATE_STATUS_MSG);
	resp->MessageLength = cpu_to_le32(20);
	resp->Status = cpu_to_le32(status);
	resp->StatusBufferLength = cpu_to_le32(0);
	resp->StatusBufferOffset = cpu_to_le32(0);

	params->resp_avail(params->v);
	return 0;
}

static char const * RNDIS_STATE_NAMES[] = { "RNDIS_UNINITIALIZED", "RNDIS_INITIALIZED", "RNDIS_DATA_INITIALIZED" };
static inline char const * get_rndis_state_name(enum rndis_state v) {
	u32 i = (u32)v;
	return (i < (u32)arraysize(RNDIS_STATE_NAMES)) ? RNDIS_STATE_NAMES[i] : "<invalid rndis_state>";
}
static inline char const * get_ndis_media_state_name(u32 v) {
	return (NDIS_MEDIA_STATE_CONNECTED == v) ? "NDIS_MEDIA_STATE_CONNECTED" : "NDIS_MEDIA_STATE_DISCONNECTED";
}
static int rndis_signal_connect_impl(int configNr)
{
	int r;
	enum rndis_state params_state;
	u32 params_media_state;
	rndis_params *params = rndis_per_dev_params + configNr;
	r = -ENOTSUPP;
	params_state = params->state; params_media_state = params->media_state;
	
	/* if (RNDIS_UNINITIALIZED == params->state) {
		PICOWRN("RNDIS_STATUS_MEDIA_CONNECT requested, but params->state == RNDIS_UNINITIALIZED, set dev_state_open(%d) to 1\n",
			(int)dev_state_open)
	} else */ if (RNDIS_INITIALIZED == params->state || RNDIS_DATA_INITIALIZED == params->state) {
		if (NDIS_MEDIA_STATE_CONNECTED != params->media_state) {
			params->media_state = NDIS_MEDIA_STATE_CONNECTED;
			r = rndis_indicate_status_msg(configNr, RNDIS_STATUS_MEDIA_CONNECT);
		}
	}
	
	PICODBG("%s, %s, dev_state_open(%d) -> r(%d), %s, %s\n",
		get_rndis_state_name(params_state), get_ndis_media_state_name(params_media_state), params->dev_state_open,
		r, get_rndis_state_name(params->state), get_ndis_media_state_name(params->media_state));
	return r;
}
int rndis_signal_connect(int configNr) {
	rndis_params *params = rndis_per_dev_params + configNr;
	params->dev_state_open = 1;
	
	return rndis_signal_connect_impl(configNr);
}

static int rndis_signal_disconnect_impl(int configNr)
{
	int r;
	enum rndis_state params_state;
	u32 params_media_state;
	rndis_params *params = rndis_per_dev_params + configNr;
	r = -ENOTSUPP;
	params_state = params->state; params_media_state = params->media_state;
	
	/* if (RNDIS_UNINITIALIZED == params->state) {
		PICOWRN("RNDIS_STATUS_MEDIA_DISCONNECT requested, but params->state == RNDIS_UNINITIALIZED\n")
	} else */ if (NDIS_MEDIA_STATE_CONNECTED == params->media_state) {
		params->media_state = NDIS_MEDIA_STATE_DISCONNECTED;
		r = rndis_indicate_status_msg(configNr, RNDIS_STATUS_MEDIA_DISCONNECT);
	}
	
	PICODBG("%s, %s, dev_state_open(%d) -> r(%d), %s, %s\n",
		get_rndis_state_name(params_state), get_ndis_media_state_name(params_media_state), params->dev_state_open,
		r, get_rndis_state_name(params->state), get_ndis_media_state_name(params->media_state));
	return r;
}
int rndis_signal_disconnect(int configNr) {
	rndis_params *params = rndis_per_dev_params + configNr;
	params->dev_state_open = 0;
	
	return rndis_signal_disconnect_impl(configNr);
}

void rndis_uninit(int configNr)
{
	u8 *buf;
	u32 length;

	if (configNr >= RNDIS_MAX_CONFIGS)
		return;
	rndis_per_dev_params[configNr].state = RNDIS_UNINITIALIZED;

	/* drain the response queue */
	while ((buf = rndis_get_next_response(configNr, &length)))
		rndis_free_response(configNr, buf);
}

void rndis_set_host_mac(int configNr, const u8 *addr)
{
	rndis_per_dev_params[configNr].host_mac = addr;
}

/*
 * Message Parser
 */
int rndis_msg_parser(u8 configNr, u8 *buf)
{
	int r;
	u32 MsgType, MsgLength;
	__le32 *tmp;
	rndis_params *params;

	if (!buf) {
		PICOERR("!buf")
		return -ENOMEM;
	}

	tmp = (__le32 *)buf;
	MsgType   = get_unaligned_le32(tmp++);
	MsgLength = get_unaligned_le32(tmp++);

	if (configNr >= RNDIS_MAX_CONFIGS) {
		PICOERR("configNr(%d) >= RNDIS_MAX_CONFIGS(%d)\n",
			(int)configNr, (int)RNDIS_MAX_CONFIGS)
		return -ENOTSUPP;
	}
	params = &rndis_per_dev_params[configNr];

	/* NOTE: RNDIS is *EXTREMELY* chatty ... Windows constantly polls for
	 * rx/tx statistics and link status, in addition to KEEPALIVE traffic
	 * and normal HC level polling to see if there's any IN traffic.
	 */

	/* For USB: responses may take up to 10 seconds */
	switch (MsgType) {
	case REMOTE_NDIS_INITIALIZE_MSG:
		pr_debug("%s: REMOTE_NDIS_INITIALIZE_MSG\n", __func__);
		
		params->state = RNDIS_INITIALIZED;
		params->hw_state = NdisHardwareStatusReady;
		r = rndis_init_response(configNr, (rndis_init_msg_type *)buf);
		if (params->dev_state_open && RNDIS_INITIALIZED == params->state)
			rndis_signal_connect_impl(configNr); /* params->media_state = NDIS_MEDIA_STATE_CONNECTED; */
		
	case REMOTE_NDIS_HALT_MSG:
		PICODBG("REMOTE_NDIS_HALT_MSG, params->dev(0x%p)\n", params->dev)
		
		/* No response is send to host on receiving Halt Command */
		params->state = RNDIS_UNINITIALIZED;
		params->media_state = NDIS_MEDIA_STATE_DISCONNECTED;
		params->hw_state = NdisHardwareStatusNotReady;
		if (params->dev) {
			netif_carrier_off(params->dev);
			netif_stop_queue(params->dev);
		}
		return 0;

	case REMOTE_NDIS_QUERY_MSG:
		return rndis_query_response(configNr, (rndis_query_msg_type *)buf);

	case REMOTE_NDIS_SET_MSG:
		r = rndis_set_response(configNr, (rndis_set_msg_type *)buf);
		if (params->dev_state_open) {
			if (RNDIS_INITIALIZED == params->state)
				rndis_signal_disconnect(configNr); /* params->media_state = NDIS_MEDIA_STATE_DISCONNECTED; */
			else if (RNDIS_DATA_INITIALIZED == params->state)
				rndis_signal_connect_impl(configNr); /* params->media_state = NDIS_MEDIA_STATE_CONNECTED; */
		}
		// signal state

	case REMOTE_NDIS_RESET_MSG:
		pr_debug("%s: REMOTE_NDIS_RESET_MSG\n", __func__);
		
		return rndis_reset_response(configNr, (rndis_reset_msg_type *)buf);

	case REMOTE_NDIS_KEEPALIVE_MSG:
		/* For USB: host does this every 5 seconds */
		if (rndis_debug > 1)
			pr_debug("%s: REMOTE_NDIS_KEEPALIVE_MSG\n", __func__);
		
		return rndis_keepalive_response(configNr, (rndis_keepalive_msg_type *)buf);

	default:
		/* At least Windows XP emits some undefined RNDIS messages.
		 * In one case those messages seemed to relate to the host
		 * suspending itself.
		 */
		pr_warning("%s: unknown RNDIS message 0x%08X len %d\n",
			__func__, MsgType, MsgLength);
		{
			unsigned i;
			for (i = 0; i < MsgLength; i += 16) {
				pr_debug("%03d: "
					" %02x %02x %02x %02x"
					" %02x %02x %02x %02x"
					" %02x %02x %02x %02x"
					" %02x %02x %02x %02x"
					"\n",
					i,
					buf[i], buf [i+1],
						buf[i+2], buf[i+3],
					buf[i+4], buf [i+5],
						buf[i+6], buf[i+7],
					buf[i+8], buf [i+9],
						buf[i+10], buf[i+11],
					buf[i+12], buf [i+13],
						buf[i+14], buf[i+15]);
			}
		}
		break;
	}

	return -ENOTSUPP;
}

int rndis_register(void (*resp_avail)(void *v), void *v)
{
	u8 i;

	if (!resp_avail)
		return -EINVAL;

	for (i = 0; i < RNDIS_MAX_CONFIGS; i++) {
		if (!rndis_per_dev_params[i].used) {
			rndis_per_dev_params[i].used = 1;
			rndis_per_dev_params[i].resp_avail = resp_avail;
			rndis_per_dev_params[i].v = v;
			pr_debug("%s: configNr = %d\n", __func__, i);
			return i;
		}
	}
	pr_debug("failed\n");

	return -ENODEV;
}

void rndis_deregister(int configNr)
{
	pr_debug("%s:\n", __func__);

	if (configNr >= RNDIS_MAX_CONFIGS) return;
	rndis_per_dev_params[configNr].used = 0;
}

int rndis_set_param_dev(u8 configNr, struct net_device *dev, u16 *cdc_filter)
{
	if (!dev)
		return -EINVAL;
	if (configNr >= RNDIS_MAX_CONFIGS) return -1;

	rndis_per_dev_params[configNr].dev = dev;
	rndis_per_dev_params[configNr].filter = cdc_filter;

	return 0;
}

int rndis_set_param_vendor(u8 configNr, u32 vendorID, const char *vendorDescr)
{
	pr_debug("%s:\n", __func__);
	if (!vendorDescr) return -1;
	if (configNr >= RNDIS_MAX_CONFIGS) return -1;

	rndis_per_dev_params[configNr].vendorID = vendorID;
	rndis_per_dev_params[configNr].vendorDescr = vendorDescr;

	return 0;
}

int rndis_set_param_medium(u8 configNr, u32 medium, u32 speed)
{
	pr_debug("%s: %u %u\n", __func__, medium, speed);
	if (configNr >= RNDIS_MAX_CONFIGS) return -1;

	rndis_per_dev_params[configNr].medium = medium;
	rndis_per_dev_params[configNr].speed = speed;

	return 0;
}

void rndis_add_hdr(struct sk_buff *skb)
{
	struct rndis_packet_msg_type *header;

	if (!skb)
		return;
	header = (void *)skb_push(skb, sizeof(*header));
	memset(header, 0, sizeof *header);
	header->MessageType = cpu_to_le32(REMOTE_NDIS_PACKET_MSG);
	header->MessageLength = cpu_to_le32(skb->len);
	header->DataOffset = cpu_to_le32(36);
	header->DataLength = cpu_to_le32(skb->len - sizeof(*header));
}

void rndis_free_response(int configNr, u8 *buf)
{
	rndis_resp *r;
	struct list_head *act, *tmp;

	list_for_each_safe(act, tmp,
			&(rndis_per_dev_params[configNr].resp_queue))
	{
		r = list_entry(act, rndis_resp, list);
		if (r && r->buf == buf) {
			list_del(&r->list);
			kfree(r);
		}
	}
}

u8 *rndis_get_next_response(int configNr, u32 *length)
{
	rndis_resp *r;
	struct list_head *act, *tmp;

	if (!length)
		return NULL;

	list_for_each_safe(act, tmp,
			&(rndis_per_dev_params[configNr].resp_queue))
	{
		r = list_entry(act, rndis_resp, list);
		if (!r->send) {
			r->send = 1;
			*length = r->length;
			return r->buf;
		}
	}

	return NULL;
}

static rndis_resp *rndis_add_response(int configNr, u32 length)
{
	rndis_resp *r;

	/* NOTE: this gets copied into ether.c USB_BUFSIZ bytes ... */
	r = kmalloc(sizeof(rndis_resp) + length, GFP_ATOMIC);
	if (!r)
		return NULL;

	r->buf = (u8 *)(r + 1);
	r->length = length;
	r->send = 0;

	list_add_tail(&r->list,
		&(rndis_per_dev_params[configNr].resp_queue));
	return r;
}

int rndis_rm_hdr(struct gether *port,
			struct sk_buff *skb,
			struct sk_buff_head *list)
{
	/* tmp points to a struct rndis_packet_msg_type */
	__le32 *tmp = (void *)skb->data;

	/* MessageType, MessageLength */
	if (cpu_to_le32(REMOTE_NDIS_PACKET_MSG)
			!= get_unaligned(tmp++)) {
		dev_kfree_skb_any(skb);
		return -EINVAL;
	}
	tmp++;

	/* DataOffset, DataLength */
	if (!skb_pull(skb, get_unaligned_le32(tmp++) + 8)) {
		dev_kfree_skb_any(skb);
		return -EOVERFLOW;
	}
	skb_trim(skb, get_unaligned_le32(tmp++));

	skb_queue_tail(list, skb);
	return 0;
}

static bool rndis_initialized = 0;

int rndis_init(void)
{
	u8 i;

	if (rndis_initialized)
		return 0;

	for (i = 0; i < RNDIS_MAX_CONFIGS; i++) {
		rndis_per_dev_params[i].confignr = i;
		rndis_per_dev_params[i].used = 0;
		rndis_per_dev_params[i].state = RNDIS_UNINITIALIZED;
		rndis_per_dev_params[i].media_state = NDIS_MEDIA_STATE_DISCONNECTED;
		rndis_per_dev_params[i].hw_state = NdisHardwareStatusNotReady;
		rndis_per_dev_params[i].dev_state_open = 0;
		rndis_per_dev_params[i].multicast_addr_set = 0;
		memset(&rndis_per_dev_params[i].multicast_addr[0], 0, ETH_ALEN);
		INIT_LIST_HEAD(&(rndis_per_dev_params[i].resp_queue));
	}

	rndis_initialized = true;
	return 0;
}

void rndis_exit(void)
{
	if (!rndis_initialized)
		return;
	rndis_initialized = false;
}
