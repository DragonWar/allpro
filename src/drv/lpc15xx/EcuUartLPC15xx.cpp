/**
 * See the file LICENSE for redistribution information.
 *
 * Copyright (c) 2009-2017 ObdDiag.Net. All rights reserved.
 *
 */

#include <cstring>
#include "UartLPC15xx.h"
#include "EcuUart.h"
#include "GpioDrv.h"

using namespace std;

const int TxPin  = 8;
const int RxPin  = 7;
const int RxPort = 0;
const int TxPort = 0;
const uint32_t PinAssign = ((RxPin << 16) + (RxPort * 32)) | ((TxPin << 8)  + (TxPort * 32));


//#define INVERT_OUTPUT // Invert output for simple transistor-based K-line driver
#define OPEN_DRAIN      // Define open-drain for most of the board, 
                        // comment it out for simple transistor-based K-line driver

/**
 * EcuUart singleton
 * @return The pointer to EcuUart instance
 */
EcuUart* EcuUart::instance()
{
    static EcuUart instance;
    return &instance;;
}

/**
 * Configure UART1
 */
void EcuUart::configure()
{
#ifndef INVERT_OUTPUT
    GPIOPinConfig(RxPort, RxPin, 0);
#else    
    GPIOPinConfig(RxPort, RxPin, GPIO_HYSTERESIS);
#endif
    
#ifdef OPEN_DRAIN
    GPIOPinConfig(TxPort, TxPin, GPIO_OPEN_DRAIN);
#else
    GPIOPinConfig(TxPort, TxPin, 0);
#endif
    
    GPIOSetDir(RxPort, RxPin, GPIO_INPUT);
    GPIOSetDir(TxPort, TxPin, GPIO_OUTPUT);

    // Enable UART1 clock
    LPC_SYSCON->SYSAHBCLKCTRL1 |= (1 << 18);
    LPC_SYSCON->PRESETCTRL1 |=  (1 << 18);
    LPC_SYSCON->PRESETCTRL1 &= ~(1 << 18);
    LPC_SYSCON->UARTCLKDIV = 1;
    
    // Set the K-line to high
    instance()->setBit(1);
}

/**
 * Use UART ROM API to configuring speed and interrupt for UART1,
 * discard the allocated UART memory block afterwards
 * @parameter[in] speed EcuUart speed
 */
void EcuUart::init(uint32_t speed)
{
    setBitBang(false);
    
    const int UART_MEM_LEN = 40;

    // Allocate UART API block
    uint8_t uartMem[UART_MEM_LEN];

    NVIC_DisableIRQ(UART1_IRQn);

      // Setup the UART handle
    UART_HANDLE_T uartHandle =
        LPC_UARTD_API->uart_setup(reinterpret_cast<uint32_t>(LPC_USART1), uartMem);

    UART_CONFIG_T cfg = {
        SystemCoreClock, // U_PCLK frequency in Hz
        speed,           // Baud Rate in Hz
        1,               // 8N1
        0,               // Asynchronous Mode
        NO_ERR_EN        // Enable No Errors
    };

    // Initialize the UART with the configuration parameters
    LPC_UARTD_API->uart_init(uartHandle, &cfg);
    
    // Invert output for simple transistor-based K-line driver
#ifdef INVERT_OUTPUT
    LPC_USART1->CFG |= (0x1 << 23); // TXPOL flag
#endif
}

/**
 * Send byte, blocking call pending on UART1 send ready status
 * @parameter[in] byte Byte to sent
 */
void EcuUart::send(uint8_t byte)
{
    while (!(UARTGetStatus(LPC_USART1) & UART_STAT_TXRDY))
        ;
    UARTSendByte(LPC_USART1, byte);
}

/**
 * Checking EcuUart receive ready flag
 * @return The flag
 */
bool EcuUart::ready()
{
    return (UARTGetStatus(LPC_USART1) & UART_STAT_RXRDY);
}

/*
 * Reading a byte from USART
 * @return The byte received
 */
uint8_t EcuUart::get()
{
    return UARTReadByte(LPC_USART1);
}

/**
 * As USART TX and RX pins are interconnected thru MC33660, sending a byte will always echo it back
 * just wait and read it back.
 * @parameter[in] byte The already sent byte to compare echo with
 * @return The completion status
 */
bool EcuUart::getEcho(uint8_t byte)
{
    const uint32_t EchoTimeout = 20; // Using 20ms echo timeout

    // Use the SysTick to generate the timeout
    SysTick->LOAD = EchoTimeout * (SystemCoreClock / 1000);
    SysTick->VAL  = 0;
    SysTick->CTRL = SysTick_CTRL_CLKSOURCE_Msk | SysTick_CTRL_ENABLE_Msk;

    while (!ready()) {
        if (SysTick->CTRL & 0x10000) // check timeout
            return false;
    }
    uint8_t echo = get();
    return (echo == byte);
}

/*
 * Turn on/off bing bang-mode for ISO initialization
 * @parameter[in] val Bing-bang mode flag 
 */
void EcuUart::setBitBang(bool val)
{
    if (!val) {
        LPC_SWM->PINASSIGN1 &= 0xFF0000FF;
        LPC_SWM->PINASSIGN1 |= PinAssign;
    }
    else {
        LPC_SWM->PINASSIGN1 |= 0x00FFFF00;
    }
}

/*
 * Set the USART TX pin status
 * @parameter[in] bit USART TX pin value
 */
void EcuUart::setBit(uint32_t bit)
{
#ifndef INVERT_OUTPUT
    GPIOPinWrite(TxPort, TxPin, bit);
#else
    // Invert output for simple transistor-based K-line driver
    GPIOPinWrite(TxPort, TxPin, (bit ? 0 : 1)); 
#endif
}

/**
 * Read USART RX pin status
 * @return pin status, 1 if set, 0 otherwise
 */
uint32_t EcuUart::getBit()
{
    return GPIOPinRead(RxPort, RxPin);
}

/**
 * Clear Framing/Parity errors, if any, by reading RXDATA
 */
void EcuUart::clear()
{
    // Clear FRAMERR and PARITYERR by reading RXDATA
    if (LPC_USART1->STAT & 0x6000) {
        LPC_USART1->STAT |= 0x6000;
        UARTReadByte(LPC_USART1);
    }
}
