/*****************************************************************************
 *   Copyright(C)2009-2019 by VSF Team                                       *
 *                                                                           *
 *  Licensed under the Apache License, Version 2.0 (the "License");          *
 *  you may not use this file except in compliance with the License.         *
 *  You may obtain a copy of the License at                                  *
 *                                                                           *
 *     http://www.apache.org/licenses/LICENSE-2.0                            *
 *                                                                           *
 *  Unless required by applicable law or agreed to in writing, software      *
 *  distributed under the License is distributed on an "AS IS" BASIS,        *
 *  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied. *
 *  See the License for the specific language governing permissions and      *
 *  limitations under the License.                                           *
 *                                                                           *
 ****************************************************************************/
/*============================ INCLUDES ======================================*/

#include "vsf.h"
#include "spi.h"
#include "sysctrl_api.h"
#include "../vendor/plf/aic8800/src/driver/iomux/reg_iomux.h"
#include "../vendor/plf/aic8800/src/driver/dma/dma_api.h"
#include "../vendor/plf/aic8800/src/driver/gpio/gpio_api.h"
#include "./hal/driver/AIC/AIC8800/port/rtos/rtos_al.h"
/*============================ MACROS ========================================*/

#define SPI0_RXDMA_CH_IDX       DMA_CHANNEL_SPI0_RX
#define SPI0_TXDMA_CH_IDX       DMA_CHANNEL_SPI0_TX
#define SPI0_RXDMA_IRQ_IDX      DMA08_IRQn
#define SPI0_TXDMA_IRQ_IDX      DMA09_IRQn
#define SPI0_DMA_BYTE_CNT_MAX   (64)
/*============================ MACROFIED FUNCTIONS ===========================*/

#define ____vsf_hw_spi_imp_lv0(__count, __dont_care)                            \
    vsf_spi_t vsf_spi##__count = {                                              \
        .param = SPI##__count,                                                  \
        .is_able = false,                                                       \
        .cfg = NULL,                                                            \
    };

#define __vsf_hw_spi_imp_lv0(__count)                                           \
    VSF_MREPEAT(__count, ____vsf_hw_spi_imp_lv0, __count)

#define aic8800_spi_def(__count)                                                \
    __vsf_hw_spi_imp_lv0(__count)
/*============================ TYPES =========================================*/
/*============================ GLOBAL VARIABLES ==============================*/
/*============================ LOCAL VARIABLES ===============================*/

aic8800_spi_def(1)
/*============================ PROTOTYPES ====================================*/

static vsf_err_t __vsf_spi_init(vsf_spi_t *spi_ptr)
{
    uint32_t pclk;
    uint32_t cr0;
    cpusysctrl_pclkme_set(CSC_PCLKME_SPI_EN_BIT);
    cpusysctrl_oclkme_set(CSC_OCLKME_SPI_EN_BIT);
    cpusysctrl_hclkme_set(CSC_HCLKME_DMA_EN_BIT);

    pclk = sysctrl_clock_get(SYS_PCLK);
    spi_dr_div_setf(pclk / 2 / spi_ptr->cfg.clock_hz - 1);

    iomux_gpio_config_sel_setf(10, 3);
    if (spi_ptr->cfg.mode & SPI_AUTO_SLAVE_SELECTION_ENABLE) {
        iomux_gpio_config_sel_setf(11, 3);
    } else {
        //todo
        gpio_init2(11);
        gpio_dir_out(11);
        gpio_set(11);
    }

    iomux_gpio_config_sel_setf(12, 3);
    iomux_gpio_config_sel_setf(13, 3);

    if (SPI_MODE_0 == (spi_ptr->cfg.mode & SPI_MODE_3)) {
        cr0 = (0x01ul << 0) |(0x01ul << 1) | (0x00ul << 13);
    } else if (SPI_MODE_1 == (spi_ptr->cfg.mode & SPI_MODE_3)) {
        cr0 = (0x00ul << 0) |(0x00ul << 1) | (0x00ul << 13);
    } else if (SPI_MODE_2 == (spi_ptr->cfg.mode & SPI_MODE_3)) {
        cr0 = (0x01ul << 0) |(0x01ul << 1) | (0x01ul << 13);
    } else if (SPI_MODE_3 == (spi_ptr->cfg.mode & SPI_MODE_3)) {
        cr0 = (0x00ul << 0) |(0x00ul << 1) | (0x01ul << 13);
    }
    spi_ptr->param->CR[0] = ((0x0EUL <<  8) | (8 << 2) | cr0);

    return VSF_ERR_NONE;
}


static vsf_err_t __vsf_spi_request_transfer(vsf_spi_t *spi_ptr, void *out_buffer_ptr, void *in_buffer_ptr, uint_fast32_t count)
{
    uint32_t cr0;
    int ch = SPI0_TXDMA_CH_IDX;
    if (count > SPI0_DMA_BYTE_CNT_MAX) {
        spi_ptr->__off_set.count = count;
        spi_ptr->__off_set.off_set_64 = 0;
        spi_ptr->__off_set.buff.out_buff = out_buffer_ptr;
        spi_ptr->__off_set.buff.in_buff = out_buffer_ptr;
        count = SPI0_DMA_BYTE_CNT_MAX;
    } else {
        spi_ptr->__off_set.count = 0;
    }
    dma_erqcsr_set(REQ_CID_SPI_TX, ch);
    dma_ch_rqr_erqm_clrb(ch);
    dma_ch_rqr_erql_setb(ch);
    dma_ch_dar_set(ch, (unsigned int)(&spi_ptr->param->IOR));
    dma_ch_sar_set(ch, (unsigned int)out_buffer_ptr);
    dma_ch_tbl0cr_set(ch, ((1) | (REQ_FRAG << DMA_CH_RQTYP_LSB) | (AHB_BYTE << DMA_CH_DBUSU_LSB) |
                             (AHB_WORD << DMA_CH_SBUSU_LSB) | DMA_CH_CONSTDA_BIT));
    dma_ch_tbl1cr_set(ch, count);
    dma_ch_tbl2cr_set(ch, count);
    dma_ch_tsr_set(ch, ((4 << DMA_CH_STRANSZ_LSB) | (1 << DMA_CH_DTRANSZ_LSB)));
    dma_ch_wmar_set(ch, 0);
    dma_ch_wjar_set(ch, 0);
    dma_ch_lnar_set(ch, 0);
    dma_ch_tbl0sr_set(ch, ((0 << DMA_CH_STBL0SZ_LSB) | (0 << DMA_CH_DTBL0SZ_LSB)));
    dma_ch_tbl1ssr_set(ch, 0);
    dma_ch_tbl1dsr_set(ch, 0);
    dma_ch_icsr_set(ch, (DMA_CH_TBL2_IENA_BIT | DMA_CH_CE_IENA_BIT));
    dma_ch_ctlr_set(ch, (DMA_CH_CHENA_BIT | (0x01UL << DMA_CH_BUSBU_LSB)));

    ch = SPI0_RXDMA_CH_IDX;
    dma_erqcsr_set(REQ_CID_SPI_RX, ch);
    dma_ch_rqr_erqm_clrb(ch);
    dma_ch_rqr_erql_setb(ch);
    dma_ch_dar_set(ch, (unsigned int)in_buffer_ptr);
    dma_ch_sar_set(ch, (unsigned int)(&spi_ptr->param->IOR));
    dma_ch_tbl0cr_set(ch, ((1) | (REQ_FRAG << DMA_CH_RQTYP_LSB) | (AHB_WORD << DMA_CH_DBUSU_LSB) |
                             (AHB_BYTE << DMA_CH_SBUSU_LSB) | DMA_CH_CONSTSA_BIT));
    dma_ch_tbl1cr_set(ch, count);
    dma_ch_tbl2cr_set(ch, count);
    dma_ch_tsr_set(ch, ((1 << DMA_CH_STRANSZ_LSB) | (4 << DMA_CH_DTRANSZ_LSB)));
    dma_ch_wmar_set(ch, 0);
    dma_ch_wjar_set(ch, 0);
    dma_ch_lnar_set(ch, 0);
    dma_ch_tbl0sr_set(ch, ((0 << DMA_CH_STBL0SZ_LSB) | (0 << DMA_CH_DTBL0SZ_LSB)));
    dma_ch_tbl1ssr_set(ch, 0);
    dma_ch_tbl1dsr_set(ch, 0);

    dma_ch_icsr_set(ch, (DMA_CH_TBL2_IENA_BIT | DMA_CH_CE_IENA_BIT));

    dma_ch_ctlr_set(ch, (DMA_CH_CHENA_BIT | (0x01UL << DMA_CH_BUSBU_LSB)));

    spi_ptr->param->OCR   = count;
    spi_ptr->param->ICR   = count;
    spi_ptr->param->MR0   = ((0x01UL << 11) | (0x00UL <<  3)); // slave, generic mode
    if (SPI_MODE_0 == (spi_ptr->cfg.mode & SPI_MODE_3)) {
        cr0 = (0x01ul << 0) |(0x01ul << 1) | (0x00ul << 13);
    } else if (SPI_MODE_1 == (spi_ptr->cfg.mode & SPI_MODE_3)) {
        cr0 = (0x00ul << 0) |(0x00ul << 1) | (0x00ul << 13);
    } else if (SPI_MODE_2 == (spi_ptr->cfg.mode & SPI_MODE_3)) {
        cr0 = (0x01ul << 0) |(0x01ul << 1) | (0x01ul << 13);
    } else if (SPI_MODE_3 == (spi_ptr->cfg.mode & SPI_MODE_3)) {
        cr0 = (0x00ul << 0) |(0x00ul << 1) | (0x01ul << 13);
    }
    spi_ptr->param->CR[0] = ((0x0EUL <<  8) | (8 << 2) | cr0);
    spi_ptr->param->CR[1] = ((0x03UL << 12) | (0x01UL <<  6));
    spi_ptr->param->CR[2] = (0x01UL <<  6);
    spi_ptr->param->CR[3] = ((0x02UL << 8) | (0x01UL << 0));
    spi_ptr->param->TCR   = (0x01UL << 1);

    return VSF_ERR_NONE;
}

void SPI0_IRQHandler(void)
{
    uint32_t int_st = vsf_spi0.param->IMSR;
    vsf_spi0.param->ICLR = int_st;
}

void DMA08_IRQHandler(void)
{
    const int ch = SPI0_RXDMA_CH_IDX;
    uint32_t cr0;
    dma_ch_icsr_set(ch, (dma_ch_icsr_get(ch) | DMA_CH_TBL2_ICLR_BIT | DMA_CH_CE_ICLR_BIT));
    if (0 != vsf_spi0.__off_set.count) {
        vsf_spi0.__off_set.count -= SPI0_DMA_BYTE_CNT_MAX;
        __vsf_spi_request_transfer(&vsf_spi0,
                                   (void *)((uint8_t *)vsf_spi0.__off_set.buff.out_buff + SPI0_DMA_BYTE_CNT_MAX),
                                   (void *)((uint8_t *)vsf_spi0.__off_set.buff.in_buff + SPI0_DMA_BYTE_CNT_MAX),
                                   vsf_spi0.__off_set.count);
        return;
    }
    gpio_clr(15);
    if ((vsf_spi0.irq_msk & SPI_IRQ_MASK_CPL) && NULL != vsf_spi0.cfg.isr.handler_fn) {
        vsf_spi0.cfg.isr.handler_fn(vsf_spi0.cfg.isr.target_ptr, &vsf_spi0, SPI_IRQ_MASK_CPL);
    }
    if (SPI_MODE_0 == (vsf_spi0.cfg.mode & SPI_MODE_3)) {
        cr0 = (0x01ul << 0) |(0x01ul << 1) | (0x00ul << 13);
    } else if (SPI_MODE_1 == (vsf_spi0.cfg.mode & SPI_MODE_3)) {
        cr0 = (0x00ul << 0) |(0x00ul << 1) | (0x00ul << 13);
    } else if (SPI_MODE_2 == (vsf_spi0.cfg.mode & SPI_MODE_3)) {
        cr0 = (0x01ul << 0) |(0x01ul << 1) | (0x01ul << 13);
    } else if (SPI_MODE_3 == (vsf_spi0.cfg.mode & SPI_MODE_3)) {
        cr0 = (0x00ul << 0) |(0x00ul << 1) | (0x01ul << 13);
    }
    vsf_spi0.param->CR[0] = ((0x0EUL <<  8) | cr0);
    vsf_spi0.param->CR[2] = 0x00UL;
    vsf_spi0.param->TCR   = 0x00UL;
    vsf_spi0.is_busy = false;
}

void DMA09_IRQHandler(void)
{
    const int ch = SPI0_TXDMA_CH_IDX;
    dma_ch_icsr_set(ch, (dma_ch_icsr_get(ch) | DMA_CH_TBL2_ICLR_BIT | DMA_CH_CE_ICLR_BIT));
    if ((vsf_spi0.irq_msk & SPI_IRQ_MASK_TX_CPL) && NULL != vsf_spi0.cfg.isr.handler_fn) {
        vsf_spi0.cfg.isr.handler_fn(vsf_spi0.cfg.isr.target_ptr, &vsf_spi0, SPI_IRQ_MASK_TX_CPL);
        if (!(vsf_spi0.irq_msk & SPI_IRQ_MASK_CPL)) {
            vsf_spi0.is_busy = false;
        }
    }
}

vsf_err_t vsf_spi_init(vsf_spi_t *spi_ptr, spi_cfg_t *cfg_ptr)
{
    vsf_err_t ret;
    VSF_HAL_ASSERT(spi_ptr != NULL);
    VSF_HAL_ASSERT(cfg_ptr != NULL);
    gpio_init2(15);
    gpio_dir_out(15);
    spi_ptr->cfg = *cfg_ptr;
    ret = __vsf_spi_init(spi_ptr);
    spi_ptr->param->MR0 = (((spi_ptr->cfg.mode & SPI_MASTER) << 11) | (0x00ul <<  3));
    return (ret == VSF_ERR_NONE) ? VSF_ERR_NONE : VSF_ERR_FAIL;
}

fsm_rt_t vsf_spi_enable(vsf_spi_t *spi_ptr)
{
    spi_ptr->is_able = true;
    return fsm_rt_cpl;
}

fsm_rt_t vsf_spi_disable(vsf_spi_t *spi_ptr)
{
    spi_ptr->is_able = false;
    return fsm_rt_cpl;
}

void vsf_spi_irq_enable(vsf_spi_t *spi_ptr, em_spi_irq_mask_t irq_mask)
{
    if (!spi_ptr->is_able) {
        return;
    }
    spi_ptr->irq_msk = irq_mask;
    if (irq_mask & SPI_IRQ_MASK_TX_CPL) {
        NVIC_SetPriority(SPI0_TXDMA_IRQ_IDX, __NVIC_PRIO_LOWEST);
        NVIC_EnableIRQ(SPI0_TXDMA_IRQ_IDX);
    }
    if (irq_mask & SPI_IRQ_MASK_CPL) {
        spi_ptr->param->IER = (0x01UL << 8);
        NVIC_SetPriority(SPI0_RXDMA_IRQ_IDX, __NVIC_PRIO_LOWEST);
        NVIC_EnableIRQ(SPI0_RXDMA_IRQ_IDX);
    }
}

void vsf_spi_irq_disable(vsf_spi_t *spi_ptr, em_spi_irq_mask_t irq_mask)
{
    if (!spi_ptr->is_able) {
        return;
    }
    spi_ptr->irq_msk = irq_mask;
    if (irq_mask & SPI_IRQ_MASK_TX_CPL) {
        NVIC_DisableIRQ(SPI0_TXDMA_IRQ_IDX);
    }
    if (irq_mask & SPI_IRQ_MASK_CPL) {
        NVIC_DisableIRQ(SPI0_RXDMA_IRQ_IDX);
    }
}

vsf_err_t vsf_spi_request_transfer(vsf_spi_t *spi_ptr, void *out_buffer_ptr, void *in_buffer_ptr, uint_fast32_t count)
{
    uint_fast32_t ret = 0;
    VSF_HAL_ASSERT((NULL != out_buffer_ptr) || (NULL != in_buffer_ptr));
    if (!spi_ptr->is_able) {
        return VSF_ERR_NOT_READY;
    }
    if (spi_ptr->is_busy) {
        return VSF_ERR_ALREADY_EXISTS;
    }
    spi_ptr->is_busy = true;
    gpio_set(15);
    ret = __vsf_spi_request_transfer(spi_ptr, out_buffer_ptr, in_buffer_ptr, count);
    return (ret == VSF_ERR_NONE) ? VSF_ERR_NONE : VSF_ERR_NOT_READY;
}

vsf_err_t vsf_spi_cancel_transfer(vsf_spi_t *spi_ptr)
{
    //todo:
    VSF_HAL_ASSERT(false);
}

void vsf_spi_cs_active(vsf_spi_t *spi_ptr, uint_fast8_t index)
{
    //todo:
    if (spi_ptr->cfg.mode & SPI_AUTO_SLAVE_SELECTION_ENABLE) {
        return;
    }
    gpio_clr(11);
}

void vsf_spi_cs_inactive(vsf_spi_t *spi_ptr, uint_fast8_t index)
{
    //todo:
    if (spi_ptr->cfg.mode & SPI_AUTO_SLAVE_SELECTION_ENABLE) {
        return;
    }
    gpio_set(11);
}

spi_status_t vsf_spi_status(vsf_spi_t *spi_ptr)
{
    return spi_ptr->status;
}
void vsf_spi_fifo_transfer(vsf_spi_t *spi_ptr, void *out_buffer_ptr, uint_fast32_t* out_count_ptr,
                                               void *in_buffer_ptr,  uint_fast32_t* in_count_ptr)
{
    //todo:
    VSF_HAL_ASSERT(false);
}

bool vsf_spi_fifo_flush(vsf_spi_t *spi_ptr)
{
    //todo:
    VSF_HAL_ASSERT(false);
}

int_fast32_t vsf_spi_get_transfered_count(vsf_spi_t *spi_ptr)
{
    //todo:
    VSF_HAL_ASSERT(false);
}
/*============================ IMPLEMENTATION ================================*/
