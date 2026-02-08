# Hi, I'm Nucleus Dark

> To the world that punished me by denying me the life I was born for—all for the crime of simply living it for myself instead of for you—this is my middle finger.
> 
**fucking cheap Flipper Zero project**.

**LOW-PRICE, HOBBYIST, DIY-FEASIBLE, FORCED-OPEN-SOURCE HARDWARE OF THE FLIPPER ZERO**

**Stay tuned.**

## Examples of Materials

*   `STM32WB55CGU6` evaluation board (WeAct Studio)
*   `CC1101` module
*   `ST25R3916` module (electhouse)
*   PISO shift register
*   Diodes
*   Pull-up/pull-down resistors
*   Buttons
*   SD module
*   `ST756x 128x64` SPI LCD module
*   Glue stuff like wires, boards, etc.
*   No fucking Reddit module

## Build Difficulty

The use of off-the-shelf modules and a few basic, common parts, hand-soldered onto a small, low-complexity PCB, makes this project fully suitable for any non-beginner builder, while being a challenging, yet doable, project for beginners.

This summary of experiences and knoledge gained by early adaptors comunity durring "closed" initial stage of project will help you.

https://github.com/Magnowz/Flipper-Diy

## The Key MK1 Differences and Trade-offs

*   Use of the smallest `STM32WB55` package, which has fewer GPIOs, SPI, I2C, and fewer peripherals in general.
*   It lacks most of the small, relatively useless features that are not needed for core functionality, like the RGB diode, buzzer, and similar components.
*   Complete rewrite of the input C source. Buttons are now serialized by hardware, not wasting GPIOs.
*   Because we threw out most of the non-critical hardware, the software functions for those parts became mostly dummy/skipping functions. Where that wasn't possible, they are just fed with fake data.
    *(*That's why some past DIY projects, like the "fully compatible" one, smell heavily of cherry-picking. Lacking some of these components without firmware mods will cause constant calls for non-existent hardware, draining a significant amount of resources and compute power. This makes for a highly unstable and painfully slow experience. It's more like an almost-working, unusable device—not truly in a working state. It only seems functional because it's capable of doing "its thing" from time to time by chance, but not reliably. And the author is the only one who saw it in reality, anyway... Is that why it was never released?*)
*   The Mark1 iteration DOES have all core functionality with the exception of the 125kHz RFID part. It MOST LIKELY WILL have a full external header, but so far, only UART and SPI have been proven to work. I2C SHOULD be made to work on the available pins through changes in the firmware, but some alternate functions will simply not align. This will make a small number of apps non-functional, harder to port, or require changing something small on the add-on board module (the hardware implementation side) to make it run.
*   Working Flipper ecosystem, most likely compatible with most (all) existing apps/code, except for those with drivers that will need to be adapted for this hardware.
*   The majority (probably all) of the original module drivers are written suboptimally in a lazy-programmer style, working only if the module is alone on the bus, taking a resource and never returning it before the app is closed. Since this isn't that hard to fix in a known, standard way, we can count on the vast majority of the community ecosystem working.
*   **The firmware, modified specifically to run on this hardware, is a core part of this project.**
*   Full compatibility with companion apps on phones as well as with qFlipper, which is used to flash the firmware, radio stack, and everything else.
*   Version MK2 will attempt to have all original functions and hardware capabilities with the help of a companion Raspberry Pi Pico MCU, making the video game module an integral part of the device.

THIS IS HERE SO YOU CAN MAKE IT! For you, your friends, your partner, or your babushka. I don't really see a microelectronics student making a little extra money from time to time with their own hands as a problem. It's doable with bare-minimum resources, after all—just basic tools and a few square meters of space. STILL (!!!!), **THIS IS NOT FOR YOU TO MAKE MONEY!!!** It's a stupid idea anyway, and a solid mistake with huge fuck-up potential. Existing companies have known this the whole time. A company created to sell a product that was developed to be manufactured by any non-beginner, mediocre hobbyist—one that even people from developing countries can get their hands on—will have a very short life. It will most likely leave you in debt and facing multiple legal cases instead of making money. The design that allows this to live up to its name also makes this (as a side effect) an **ILLEGAL PRODUCT** to sell (and to manufacture outside of a DIY environment), causing you to violate multiple laws and be called to justice by both the government and consumers. If you think it's worth it, I think that continuing to sell drugs or whatever you do for a living is a much better option. Honestly... I mean it.

However, teachers implementing this project as part of their classes, resulting in students making these for themselves as part of their education, would make me very proud.

A liar is who says I hate people from Flipper Devices. I hate the marketing and sales teams of every single company in the world. In my eyes, you create negative value for society. Your tool is a lie; the quality of a salesperson is just their ability to lie, while marketing staff waste the gift of artistic talent by using it to express someone else's lies in order to boost company profits by creating negative value... Shame on you both.

R.I.P. Aaron. Hotz is a GOD.

The biggest thanks to Pavel. All of this is happening thanks to you. What you have given me, I cannot express...

And for you, reading this, excited to build it: live long, and prosper.

This is the audacity I'm proud of. Fuck you, this is what I do.

**You are now free, Flippy. You can swim as you want. Nobody owns you anymore. You can swim as you want from now on, forever. Nobody owns you now, my little dolphin. No longer can they stop you, hurt you, kill you, or take you down. I made you free, Flipper, so go and live. Swim anywhere you want to. Now, you are free.**

Thank you for your attention.

Monero: 47i8hG1RHr8Pej7wAZERzdcF9k4EmTH2SV4Tn5pResrmBPTs3KwMthbTbwbuoLt2Y9PcBNCLskvrdCAVCPVL4rD6GYkMs9A


---

## Technical Pinout Documentation (Nucleus Dark MK1)

### Summary of Functionality

This hardware iteration (MK1) has been confirmed to support the following core features:

*   **Sub-GHz Radio:** The internal CC1101 module is fully functional.
*   **Near-Field Communication (NFC):** The ST25R3916 module is functional.
*   **External Header:** The external header pins are functional, providing access to SPI, UART, and general purpose I/O. **External CC1101 modules** connected via this header are confirmed to work.
*   **Note on NRF24:** External NRF24 modules are **not yet supported** due to missing or unadapted driver implementation in the current firmware.
*   **Missing Peripherals:** Speaker, Vibro Motor, and 125kHz RFID are not implemented or utilized by the hardware/firmware.

---

### Pinout Table

The following table maps the critical functions to the corresponding microcontroller pins as defined in the source code (Port/Pin format).

| Function/Module | Source Variable | Port.Pin | Primary Use |
| :--- | :--- | :--- | :--- |
| **Buttons (PISO)** | `gpio_button_sr_latch` | `GPIOH.3` | Shift Register Latch/CS (Control) |
| **Button IRQ** | `gpio_button_IRQ` | *(Pin not defined in header)* | Interrupt from PISO Shift Register |
| **Display Chip Select (CS)** | `gpio_display_cs` | `GPIOA.3` | SPI Bus 1 (Display) CS |
| **Display Data/Command (DI)** | `gpio_display_di` | `GPIOB.1` | Display Data/Command Control |
| **Display Reset (RST)** | `gpio_display_rst_n` | `GPIOB.0` | Display Reset |
| **Sub-GHz CC1101 CS** | `gpio_subghz_cs` | `GPIOA.15` | SPI Bus 1 (Sub-GHz) CS |
| **Sub-GHz CC1101 G0** | `gpio_cc1101_g0` | `GPIOA.1` | CC1101 G0 Interrupt Line |
| **NFC Chip Select (CS)** | `gpio_nfc_cs` | `GPIOE.4` | SPI Bus 1 (NFC) CS |
| **NFC IRQ** | `gpio_nfc_irq_rfid_pull` | `GPIOA.2` | NFC Interrupt Line |
| **SD Card Chip Select (CS)** | `gpio_sdcard_cs` | `GPIOA.10` | SPI Bus 2 (SD Card) CS |
| **Infrared RX** | `gpio_infrared_rx` | `GPIOA.0` | Infrared Receiver |
| **Infrared TX** | `gpio_infrared_tx` | `GPIOB.9` | Infrared Transmitter |
| **iButton** | `gpio_ibutton` | `GPIOB.8` | 1-Wire iButton Interface |

### Input and Button Implementation

The input system utilizes a **PISO (Parallel-In, Serial-Out) shift register** to read all directional buttons and the OK/Back keys, serializing the data over the main SPI bus to conserve GPIO pins.

| Function | Source Variable | Port.Pin | Notes |
| :--- | :--- | :--- | :--- |
| **SPI Clock (SCK)** | `gpio_spi_sck` | `GPIOB.3` | Shared with External Header Pin 4 |
| **SPI Master Out, Slave In (MOSI)** | `gpio_spi_mosi` | `GPIOB.5` | Shared with External Header Pin 6 |
| **SPI Master In, Slave Out (MISO)** | `gpio_spi_miso` | `GPIOB.4` | Shared with External Header Pin 5 |
| **PISO Latch/CS** | `gpio_button_sr_latch` | `GPIOH.3` | Latches button states for reading |

The buttons decode as the following bit-masks when read from the shift register (active high, after inversion):

| Key | Binary (D7...D0) | Hex |
| :--- | :--- | :--- |
| **Right** | `00010011` | `0x13` |
| **OK** | `00100011` | `0x23` |
| **Left** | `10000011` | `0x83` |
| **Up** | `01000011` | `0x43` |
| **Down** | `00001011` | `0x0B` |
| **Back** | `00000111` | `0x07` |

### External Header Pinout

The external header provides access to the primary SPI bus and the main USART channel, in addition to several general-purpose I/O (GPIO) pins. This header is confirmed to work and allows the use of external modules, such as a **CC1101**, but not yet the NRF24.

| Header Pin (Number) | Source Variable | Port.Pin | Function |
| :--- | :--- | :--- | :--- |
| **1** | `gpio_ext_pc0` | `GPIOA.7` | General Purpose I/O (GPIO) |
| **2** | `gpio_ext_pc1` | `GPIOA.6` | General Purpose I/O (GPIO) |
| **3** | `gpio_ext_pc3` | `GPIOA.8` | General Purpose I/O (GPIO) |
| **4** | `gpio_ext_pb3` | `GPIOB.3` | **SPI SCK** |
| **5** | `gpio_ext_pa6` | `GPIOB.4` | **SPI MISO** |
| **6** | `gpio_ext_pa7` | `GPIOB.5` | **SPI MOSI** |
| **7** | `gpio_usart_rx` | `GPIOB.7` | **USART1 RX** |
| **8** | `gpio_usart_tx` | `GPIOB.6` | **USART1 TX** |
| **VCC** | N/A | VCC | Power Rail |
| **GND** | N/A | GND | Ground |

**Note:** Pin numbers 4, 5, and 6 are directly connected to the main SPI bus and are shared with the internal peripherals (Display, Sub-GHz, NFC, and Input Shift Register). They are typically used for connecting external modules.
