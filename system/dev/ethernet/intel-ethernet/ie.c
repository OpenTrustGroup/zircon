// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <inttypes.h>
#include <stdint.h>
#include <zircon/listnode.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <threads.h>

#if _KERNEL
//TODO: proper includes/defines kernel driver
#else
// includes and defines for userspace driver
#include <zircon/types.h>
#include <zircon/syscalls.h>
#include <ddk/driver.h>
typedef int status_t;
#define nanosleep(x) zx_nanosleep(zx_deadline_after(x));
#define usleep(x)    nanosleep((x) * 1000)
#define REG32(addr)  ((volatile uint32_t *)(uintptr_t)(addr))
#define writel(v, a) (*REG32(eth->iobase + (a)) = (v))
#define readl(a)     (*REG32(eth->iobase + (a)))
#endif

#include "ie.h"

void eth_dump_regs(ethdev_t* eth) {
    printf("STAT %08x CTRL %08x EXT %08x IMS %08x\n",
           readl(IE_STATUS), readl(IE_CTRL), readl(IE_CTRL_EXT), readl(IE_IMS));
    printf("RCTL %08x RDLN %08x RDH %08x RDT %08x\n",
           readl(IE_RCTL), readl(IE_RDLEN), readl(IE_RDH), readl(IE_RDT));
    printf("RXDC %08x RDTR %08x RBH %08x RBL %08x\n",
           readl(IE_RXDCTL), readl(IE_RDTR), readl(IE_RDBAH), readl(IE_RDBAL));
    printf("TCTL %08x TDLN %08x TDH %08x TDT %08x\n",
           readl(IE_TCTL), readl(IE_TDLEN), readl(IE_TDH), readl(IE_TDT));
    printf("TXDC %08x TIDV %08x TBH %08x TBL %08x\n",
           readl(IE_TXDCTL), readl(IE_TIDV), readl(IE_TDBAH), readl(IE_TDBAL));
}

unsigned eth_handle_irq(ethdev_t* eth) {
    // clears irqs on read
    return readl(IE_ICR);
}

bool eth_status_online(ethdev_t* eth) {
    return readl(IE_STATUS) & IE_STATUS_LU;
}

status_t eth_rx(ethdev_t* eth, void** data, size_t* len) {
    uint32_t n = eth->rx_rd_ptr;
    uint64_t info = eth->rxd[n].info;

    if (!(info & IE_RXD_DONE)) {
        return ZX_ERR_SHOULD_WAIT;
    }

    // copy out packet
    zx_status_t r = IE_RXD_LEN(info);

    *data = eth->rxb + ETH_RXBUF_SIZE * n;
    *len = r;

    return ZX_OK;
}

void eth_rx_ack(ethdev_t* eth) {
    uint32_t n = eth->rx_rd_ptr;

    // make buffer available to hw
    eth->rxd[n].info = 0;
    writel(n, IE_RDT);
    n = (n + 1) & (ETH_RXBUF_COUNT - 1);
    eth->rx_rd_ptr = n;
}

void eth_enable_rx(ethdev_t* eth) {
    uint32_t rctl = readl(IE_RCTL);
    writel(rctl | IE_RCTL_EN, IE_RCTL);
}

void eth_disable_rx(ethdev_t* eth) {
    uint32_t rctl = readl(IE_RCTL);
    writel(rctl & ~IE_RCTL_EN, IE_RCTL);
}

static void reap_tx_buffers(ethdev_t* eth) {
    uint32_t n = eth->tx_rd_ptr;
    for (;;) {
        uint64_t info = eth->txd[n].info;
        if (!(info & IE_TXD_DONE)) {
            break;
        }
        framebuf_t* frame = list_remove_head_type(&eth->busy_frames, framebuf_t, node);
        if (frame == NULL) {
            panic();
        }
        // TODO: verify that this is the matching buffer to txd[n] addr?
        list_add_tail(&eth->free_frames, &frame->node);
        eth->txd[n].info = 0;
        n = (n + 1) & (ETH_TXBUF_COUNT - 1);
    }
    eth->tx_rd_ptr = n;
}

status_t eth_tx(ethdev_t* eth, const void* data, size_t len) {
    if (len > ETH_TXBUF_DSIZE) {
        printf("intel-eth: unsupported packet length %zu\n", len);
        return ZX_ERR_INVALID_ARGS;
    }

    zx_status_t status = ZX_OK;

    mtx_lock(&eth->send_lock);

    reap_tx_buffers(eth);

    // obtain buffer, copy into it, setup descriptor
    framebuf_t *frame = list_remove_head_type(&eth->free_frames, framebuf_t, node);
    if (frame == NULL) {
        status = ZX_ERR_NO_RESOURCES;
        goto out;
    }

    uint32_t n = eth->tx_wr_ptr;
    memcpy(frame->data, data, len);
    // Pad out short packets.
    if (len < 60) {
      memset(frame->data + len, 0, 60 - len);
      len = 60;
    }
    eth->txd[n].addr = frame->phys;
    eth->txd[n].info = IE_TXD_LEN(len) | IE_TXD_EOP | IE_TXD_IFCS | IE_TXD_RS;
    list_add_tail(&eth->busy_frames, &frame->node);

    // inform hw of buffer availability
    n = (n + 1) & (ETH_TXBUF_COUNT - 1);
    eth->tx_wr_ptr = n;
    writel(n, IE_TDT);

out:
    mtx_unlock(&eth->send_lock);
    return status;
}

// Returns the number of Tx packets in the hw queue
size_t eth_tx_queued(ethdev_t* eth) {
    reap_tx_buffers(eth);
    return ((eth->tx_wr_ptr + ETH_TXBUF_COUNT) - eth->tx_rd_ptr) & (ETH_TXBUF_COUNT - 1);
}

void eth_enable_tx(ethdev_t* eth) {
    uint32_t tctl = readl(IE_TCTL);
    writel(tctl | IE_TCTL_EN, IE_TCTL);
}

void eth_disable_tx(ethdev_t* eth) {
    uint32_t tctl = readl(IE_TCTL);
    writel(tctl & ~IE_TCTL_EN, IE_TCTL);
}

void eth_start_promisc(ethdev_t* eth) {
    uint32_t rctl = readl(IE_RCTL);
    writel(rctl | IE_RCTL_UPE, IE_RCTL);
}

void eth_stop_promisc(ethdev_t* eth) {
    uint32_t rctl = readl(IE_RCTL);
    writel(rctl & ~IE_RCTL_UPE, IE_RCTL);
}

static zx_status_t wait_for_mdic(ethdev_t* eth, uint32_t* reg_value) {
    uint32_t mdic;
    uint32_t iterations = 0;
    do {
        nanosleep(50);
        mdic = readl(IE_MDIC);
        if (mdic & IE_MDIC_R) {
            goto success;
        }
        iterations++;
    } while (!(mdic & IE_MDIC_R) && (iterations < 100));
    printf("intel-eth: timed out waiting for MDIC to be ready\n");
    return ZX_ERR_TIMED_OUT;

success:
    if (reg_value) {
        *reg_value = mdic;
    }
    return ZX_OK;
}

static zx_status_t phy_read(ethdev_t* eth, uint8_t phyadd, uint8_t regadd, uint16_t* result) {
    uint32_t mdic = IE_MDIC_PUT_PHYADD(phyadd) |
                    IE_MDIC_PUT_REGADD(regadd) |
                    IE_MDIC_OP_READ;
    writel(mdic, IE_MDIC);
    zx_status_t status = wait_for_mdic(eth, &mdic);
    if (status == ZX_OK) {
        *result = IE_MDIC_GET_DATA(mdic);
    }
    return status;
}

static zx_status_t phy_write(ethdev_t* eth, uint8_t phyadd, uint8_t regadd, uint16_t value) {
    uint32_t mdic = IE_MDIC_PUT_DATA(value) |
                    IE_MDIC_PUT_PHYADD(phyadd) |
                    IE_MDIC_PUT_REGADD(regadd) |
                    IE_MDIC_OP_WRITE;
    writel(mdic, IE_MDIC);
    return wait_for_mdic(eth, NULL);
}

static zx_status_t get_phy_addr(ethdev_t* eth, uint8_t* phy_addr) {
    if (eth->phy_addr != 0) {
        *phy_addr = eth->phy_addr;
    }
    for (uint8_t addr = 1; addr <= IE_MAX_PHY_ADDR; addr++) {
        uint16_t pid;
        zx_status_t status = phy_read(eth, addr, IE_PHY_PID, &pid);
        // TODO: Identify the PHY more precisely
        if (status == ZX_OK && pid != 0) {
            *phy_addr = pid;
            return ZX_OK;
        }
    }
    printf("intel-eth: unable to identify valid PHY address\n");
    return ZX_ERR_NOT_FOUND;
}

zx_status_t eth_enable_phy(ethdev_t* eth) {
    uint8_t phy_addr;
    zx_status_t status = get_phy_addr(eth, &phy_addr);
    if (status != ZX_OK) {
        return status;
    }

    uint16_t phy_ctrl;
    status = phy_read(eth, phy_addr, IE_PHY_PCTRL, &phy_ctrl);
    if (status != ZX_OK) {
        return status;
    }

    if (phy_ctrl & IE_PHY_PCTRL_POWER_DOWN) {
        return phy_write(eth, phy_addr, IE_PHY_PCTRL, phy_ctrl & ~IE_PHY_PCTRL_POWER_DOWN);
    }
    return ZX_OK;
}

zx_status_t eth_disable_phy(ethdev_t* eth) {
    uint8_t phy_addr;
    zx_status_t status = get_phy_addr(eth, &phy_addr);
    if (status != ZX_OK) {
        return status;
    }

    uint16_t phy_ctrl;
    status = phy_read(eth, phy_addr, IE_PHY_PCTRL, &phy_ctrl);
    if (status != ZX_OK) {
        return status;
    }
    return phy_write(eth, phy_addr, IE_PHY_PCTRL, phy_ctrl | IE_PHY_PCTRL_POWER_DOWN);
}

status_t eth_reset_hw(ethdev_t* eth) {
    // TODO: don't rely on bootloader having initialized the
    // controller in order to obtain the mac address
    uint32_t n;
    n = readl(IE_RAL(0));
    memcpy(eth->mac + 0, &n, 4);
    n = readl(IE_RAH(0));
    memcpy(eth->mac + 4, &n, 2);
    printf("eth: mac: %02x:%02x:%02x:%02x:%02x:%02x\n",
           eth->mac[0],eth->mac[1],eth->mac[2],
           eth->mac[3],eth->mac[4],eth->mac[5]);

    // disable all interrupts
    if (eth->pci_did == IE_DID_I211_AT) {
        writel(0, IE_IAM);
    }
    writel(0xffffffff, IE_IMC);

    // disable tx/rx
    writel(0, IE_RCTL);
    writel(IE_TCTL_PSP, IE_TCTL);

    // global reset
    uint32_t reg = readl(IE_CTRL);
    writel(reg | IE_CTRL_RST, IE_CTRL);

    if (eth->pci_did == IE_DID_I211_AT) {
        usleep(20);
        reg = readl(IE_STATUS);
        if (!(reg & IE_STATUS_PF_RST_DONE)) {
            printf("eth: reset failed (1)\n");
            return ZX_ERR_BAD_STATE;
        }
        reg = readl(IE_EEC);
        if (!(reg & IE_EEC_AUTO_RD)) {
            printf("eth: reset failed (2)\n");
            return ZX_ERR_BAD_STATE;
        }
    } else {
        usleep(5);

        if (readl(IE_CTRL) & IE_CTRL_RST) {
            printf("eth: reset failed\n");
            return ZX_ERR_BAD_STATE;
        }
    }

    // disable all interrupts
    if (eth->pci_did == IE_DID_I211_AT) {
        writel(0, IE_IAM);
    }
    writel(0xffffffff, IE_IMC);

    // clear any pending interrupts
    readl(IE_ICR);

    return ZX_OK;
}

void eth_init_hw(ethdev_t* eth) {
    //TODO: tune RXDCTL and TXDCTL settings
    //TODO: TCTL COLD should be based on link state
    //TODO: use address filtering for multicast

    // set link up (Must be set to enable communications between MAC and PHY.)
    uint32_t reg = readl(IE_CTRL);
    writel(reg | IE_CTRL_SLU, IE_CTRL);

    usleep(15);

    // setup rx ring
    eth->rx_rd_ptr = 0;
    writel(eth->rxd_phys, IE_RDBAL);
    writel(eth->rxd_phys >> 32, IE_RDBAH);
    writel(ETH_RXBUF_COUNT * 16, IE_RDLEN);

    reg = IE_RXDCTL_PTHRESH(12) | IE_RXDCTL_HTHRESH(10) | IE_RXDCTL_WTHRESH(1);
    if (eth->pci_did == IE_DID_I211_AT) {
        reg |= IE_RXDCTL_ENABLE;
    } else {
        reg |= IE_RXDCTL_GRAN;
    }
    writel(reg, IE_RXDCTL);

    // wait for enable to complete
    if (eth->pci_did == IE_DID_I211_AT) {
        while (!(readl(IE_RXDCTL) & IE_RXDCTL_ENABLE)) {
        }
    }

    writel(ETH_RXBUF_COUNT - 1, IE_RDT);
    writel(IE_RCTL_BSIZE2048 | IE_RCTL_DPF | IE_RCTL_SECRC |
           IE_RCTL_BAM | IE_RCTL_MPE | IE_RCTL_EN,
           IE_RCTL);

    // setup tx ring
    eth->tx_wr_ptr = 0;
    eth->tx_rd_ptr = 0;
    writel(eth->txd_phys, IE_TDBAL);
    writel(eth->txd_phys >> 32, IE_TDBAH);
    writel(ETH_TXBUF_COUNT * 16, IE_TDLEN);

    reg = IE_TXDCTL_WTHRESH(1);
    if (eth->pci_did == IE_DID_I211_AT) {
        reg |= IE_TXDCTL_ENABLE;
    } else {
        reg |= IE_TXDCTL_GRAN;
    }
    writel(reg, IE_TXDCTL);

    // wait for enable to complete
    if (eth->pci_did == IE_DID_I211_AT) {
        while (!(readl(IE_TXDCTL) & IE_TXDCTL_ENABLE)) {
        }
    }

    if (eth->pci_did == IE_DID_I211_AT) {
        reg = IE_TCTL_CT(15) | IE_TCTL_BST(64) | IE_TCTL_PSP | IE_TCTL_EN;
    } else {
        reg = readl(IE_TCTL) & IE_TCTL_RESERVED;
        reg |= IE_TCTL_CT(15) | IE_TCTL_COLD_FD | IE_TCTL_EN;
    }
    writel(reg, IE_TCTL);

    // enable interrupts
    if (eth->pci_did == IE_DID_I211_AT) {
        // Receiver Descriptor Write Back & Link Status Change interrupts
        writel(IE_INT_RXDW | IE_INT_LSC, IE_IMS);
    } else {
        // enable rx & link status change irqs
        writel(IE_INT_RXT0 | IE_INT_LSC, IE_IMS);
    }
}

void eth_setup_buffers(ethdev_t* eth, void* iomem, zx_paddr_t iophys) {
    printf("eth: iomem @%p (phys %" PRIxPTR ")\n", iomem, iophys);

    list_initialize(&eth->free_frames);
    list_initialize(&eth->busy_frames);

    eth->rxd = iomem;
    eth->rxd_phys = iophys;
    iomem += ETH_DRING_SIZE;
    iophys += ETH_DRING_SIZE;
    memset(eth->rxd, 0, ETH_DRING_SIZE);

    eth->txd = iomem;
    eth->txd_phys = iophys;
    iomem += ETH_DRING_SIZE;
    iophys += ETH_DRING_SIZE;
    memset(eth->txd, 0, ETH_DRING_SIZE);

    eth->rxb = iomem;
    eth->rxb_phys = iophys;
    iomem += ETH_RXBUF_SIZE * ETH_RXBUF_COUNT;
    iophys += ETH_RXBUF_SIZE * ETH_RXBUF_COUNT;

    for (int n = 0; n < ETH_RXBUF_COUNT; n++) {
        eth->rxd[n].addr = eth->rxb_phys + ETH_RXBUF_SIZE * n;
    }
    for (int n = 0; n < ETH_TXBUF_COUNT - 1; n++) {
        framebuf_t *txb = iomem;
        txb->phys = iophys + ETH_TXBUF_HSIZE;
        txb->size = ETH_TXBUF_SIZE - ETH_TXBUF_HSIZE;
        txb->data = iomem + ETH_TXBUF_HSIZE;
        list_add_tail(&eth->free_frames, &txb->node);

        iomem += ETH_TXBUF_SIZE;
        iophys += ETH_TXBUF_SIZE;
    }
}
