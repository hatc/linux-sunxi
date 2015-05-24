/*
 * drivers/usb/sunxi_usb/include/sw_udc.h
 *
 * (C) Copyright 2007-2012
 * Allwinner Technology Co., Ltd. <www.allwinnertech.com>
 * javen <javen@allwinnertech.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of
 * the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.	 See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston,
 * MA 02111-1307 USA
 */

#ifndef  __SW_UDC_H__
#define  __SW_UDC_H__

#include <linux/usb.h>
#include <linux/usb/gadget.h>

#ifdef SW_UDC_DMA
#include <plat/dma.h>
#include <linux/dma-mapping.h>
#endif

/*  */
typedef struct sw_udc_ep {
	struct list_head		queue;
	unsigned long			last_io;	/* jiffies timestamp */
	struct usb_gadget		*gadget;
	struct sw_udc		    *dev;
	const struct usb_endpoint_descriptor *desc;
	struct usb_ep			ep;
	u8				        num;

	unsigned short			fifo_size;
	u8				        bEndpointAddress;
	u8				        bmAttributes;

	unsigned			    halted : 1;
	unsigned			    already_seen : 1;
	unsigned			    setup_stage : 1;

	__u32					dma_working;		/* flag. is dma busy? 		*/
	__u32 					dma_transfer_len;	/* dma want transfer length */
}sw_udc_ep_t;

/* 5.5.3 Control Transfer Packet Size Constraints
 * The allowable maximum control transfer data payload sizes
 * for full-speed devices is 8, 16, 32, or 64 bytes;
 * for high-speed devices is 64 bytes and for low-speed devices, it is 8 bytes
 * A Setup packet is always eight bytes.
 * A control pipe (including the Default Control Pipe) always uses its wMaxPacketSize value for data payloads.
 * 
 * All Host Controllers are required to have support for 8-, 16-, 32-, and 64-byte maximum data payload sizes
 * for full-speed control endpoints, only 8-byte maximum data payload sizes for low-speed control endpoints,
 * and only 64-byte maximum data payload size for high-speed control endpoints.
 * 
 * In order to determine the maximum packet size for the Default Control Pipe, the USB System Software
 * reads the device descriptor. The host will read the first eight bytes of the device descriptor.
 * The device always responds with at least these initial bytes in a single packet.
 * After the host reads the initial part of the device descriptor, it is guaranteed to have read this
 * default pipe’s wMaxPacketSize field (byte 7 of the device descriptor).
 * It will then allow the correct size for all subsequent transactions.
 * For all other control endpoints, the maximum data payload size is known after configuration
 * so that the USB System Software can ensure that no data payload will be sent to the endpoint that is larger than the supported size. */
 
/* Warning : ep0 has a fifo of 16 bytes */
/* Don't try to set 32 or 64            */
/* also testusb 14 fails  wit 16 but is */
/* fine with 8                          */
/* #define EP0_FIFO_SIZE       8        */
/* __u32 USBC_WritePacket(__hdle hUSB, __u32 fifo, __u32 cnt, void *buff) {
 __u32 *buf32 = buff;
 __u32 len = cnt;
 __u32 i32 = len >> 2;   // div 4 - int32 count in buff - i.e. cnt in bytes, div 4 - cnt in int's
 __u8  i8  = len & 0x03; // 11    - bytes tail
 while (i32--)
  USBC_Writel(*buf32++, fifo);

 buf8 = (__u8 *)buf32;
 while (i8--)
  USBC_Writeb(*buf8++, fifo);
}
* 
sw_udc_handle_ep0(struct sw_udc *dev) {
 case USB_REQ_SET_FEATURE:
   fifo = USBC_SelectFIFO(g_sw_udc_io.usb_bsp_hdle, 0); // __u32 USBC_SelectFIFO(__hdle hUSB, __u32 ep_index)
   USBC_WritePacket(g_sw_udc_io.usb_bsp_hdle, fifo, 54, (u32 *)TestPkt); // write 54 bytes
* 
* drivers/usb/sunxi_usb/include/sunxi_usb_bsp.h
* #define  USBC0_MAX_FIFO_SIZE   	(8 * 1024)
* #define  USBC_EP0_FIFOSIZE	  	64	// This is non-configurable
* */
#define EP0_FIFO_SIZE       64
/* ??? static int sw_udc_read_fifo_crq(struct usb_ctrlrequest *crq) {
 ...
 fifo_count = USBC_ReadLenFromFifo(g_sw_udc_io.usb_bsp_hdle, USBC_EP_TYPE_EP0);
 if (fifo_count != 8) { ... } */

/* drivers/usb/misc/usbtest.c:
 * case 14:        // short read; try to fill the last packet 
  req.wValue = cpu_to_le16((USB_DT_DEVICE << 8) | 0);
  // device descriptor size == 18 bytes
  len = udev->descriptor.bMaxPacketSize0;
  if (udev->speed == USB_SPEED_SUPER)
   len = 512;
  switch (len) {
   case 8:
    len = 24;
    break;
   case 16:
    len = 32;
    break;
  } */

/* composite.c:
 composite_setup():
   case USB_DT_DEVICE:
			cdev->desc.bMaxPacketSize0 = cdev->gadget->ep0->maxpacket; */

/* 5.8.3 Bulk Transfer Packet Size Constraints
 * The USB defines the allowable maximum bulk data payload sizes to be only
 * 8, 16, 32, or 64 bytes for full-speed endpoints and 512 bytes for high-speed endpoints.
 * A low-speed device must not have bulk endpoints. */
#define SW_UDC_EP_FIFO_SIZE 512
/* 5.7.3 Interrupt Transfer Packet Size Constraints
 * The maximum allowable interrupt data payload size is 64 bytes or less for full-speed.
 * High-speed endpoints are allowed maximum data payload sizes up to 1024 bytes. */
#define SW_UDC_EP_INT_FIFO_SIZE 64

/* The pipe that consists of the two endpoints with endpoint number zero is called the Default Control Pipe.
 * (it's pipe, so there always two endpoints - device && host?)
 * 
 * Each endpoint on a device is given at design time a unique device-determined identifier called the endpoint number.
 * Each endpoint has a device-determined direction of data flow.
 * The combination of the device address, endpoint number, and direction allows each endpoint to be uniquely referenced.
 * Each endpoint is a simplex connection that supports data flow in one direction: either input (from device to host) or output (from host to device).*/

#define	 SW_UDC_EP_CTRL_INDEX			0x00
#define  SW_UDC_EP_BULK_IN_INDEX		0x01
#define  SW_UDC_EP_BULK_OUT_INDEX		0x02

#ifdef  SW_UDC_DOUBLE_FIFO
#define  SW_UDC_FIFO_NUM			1
#else
#define  SW_UDC_FIFO_NUM			0
#endif

static const char ep0name [] = "ep0";

static const char *const ep_name[] = {
	ep0name,	/* everyone has ep0 */

	/* sw_udc four bidirectional bulk endpoints */
	"ep1-bulk",
	"ep2-bulk",
	"ep3-bulk",
	"ep4-bulk",
	"ep5-int"
};

#define SW_UDC_ENDPOINTS       ARRAY_SIZE(ep_name)

struct sw_udc_request {
	struct list_head		queue;		/* ep's requests */
	struct usb_request		req;

	__u32 is_queue;  /* flag. 是否已经压入队列? */
};

enum ep0_state {
        EP0_IDLE,
        EP0_IN_DATA_PHASE,
        EP0_OUT_DATA_PHASE,
        EP0_END_XFER,
        EP0_STALL,
};

/*
static const char *ep0states[]= {
        "EP0_IDLE",
        "EP0_IN_DATA_PHASE",
        "EP0_OUT_DATA_PHASE",
        "EP0_END_XFER",
        "EP0_STALL",
};
*/

#ifdef SW_UDC_DMA
//---------------------------------------------------------------
//  DMA
//---------------------------------------------------------------
typedef struct sw_udc_dma{
	char name[32];
	struct sw_dma_client dma_client;

	int dma_hdle;	/* dma 句柄 */
}sw_udc_dma_t;

/* dma 传输参数 */
typedef struct sw_udc_dma_parg{
	struct sw_udc *dev;
	struct sw_udc_ep *ep;
	struct sw_udc_request *req;
}sw_udc_dma_parg_t;
#endif


/* i/o 信息 */
typedef struct sw_udc_io{
	struct resource	*usb_base_res;   	/* USB  resources 		*/
	struct resource	*usb_base_req;   	/* USB  resources 		*/
	void __iomem	*usb_vbase;			/* USB  base address 	*/

	struct resource	*sram_base_res;   	/* SRAM resources 		*/
	struct resource	*sram_base_req;   	/* SRAM resources 		*/
	void __iomem	*sram_vbase;		/* SRAM base address 	*/

	struct resource	*clock_base_res;   	/* clock resources 		*/
	struct resource	*clock_base_req;   	/* clock resources 		*/
	void __iomem	*clock_vbase;		/* clock base address 	*/

	bsp_usbc_t usbc;					/* usb bsp config 		*/
	__hdle usb_bsp_hdle;				/* usb bsp handle 		*/

	__u32 clk_is_open;					/* is usb clock open? 	*/
	struct clk	*sie_clk;				/* SIE clock handle 	*/
	struct clk	*phy_clk;				/* PHY clock handle 	*/
	struct clk	*phy0_clk;				/* PHY0 clock handle 	*/

	long Drv_vbus_Handle;
}sw_udc_io_t;

//---------------------------------------------------------------
//
//---------------------------------------------------------------
typedef struct sw_udc {
	spinlock_t			        lock;

	struct sw_udc_ep		    ep[SW_UDC_ENDPOINTS];
	int				            address;
	struct usb_gadget		    gadget;
	struct usb_gadget_driver	*driver;
	struct sw_udc_request		fifo_req;
	u8				            fifo_buf[SW_UDC_EP_FIFO_SIZE];
	u16				            devstatus;

	u32				            port_status;
	int				            ep0state;

	unsigned			        got_irq : 1;

	unsigned			        req_std : 1;
	unsigned			        req_config : 1;
	unsigned			        req_pending : 1;
	u8				            vbus;
	struct dentry			    *regs_info;

	sw_udc_io_t					*sw_udc_io;
	char 						driver_name[32];
	__u32 						usbc_no;	/* 控制器端口号 	*/
#ifdef SW_UDC_DMA
	sw_udc_dma_t 			    sw_udc_dma;
#endif

	u32							stoped;		/* 控制器停止工作 	*/
	u32 						irq_no;		/* USB 中断号 		*/
}sw_udc_t;

enum sw_udc_cmd_e {
	SW_UDC_P_ENABLE	= 1,	/* Pull-up enable        */
	SW_UDC_P_DISABLE = 2,	/* Pull-up disable       */
	SW_UDC_P_RESET	= 3,	/* UDC reset, in case of */
};

typedef struct sw_udc_mach_info {
	struct usb_port_info *port_info;
	unsigned int usbc_base;
}sw_udc_mach_info_t;


int sw_usb_device_enable(void);
int sw_usb_device_disable(void);

#endif   //__SW_UDC_H__

