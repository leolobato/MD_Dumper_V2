/*
USB2 Sega Dumper
V2 05/2020
*Add SSF2 Support
*Preliminary Serial I2C EEPROM Support
X-death 
*/

#include <string.h>
#include <stdlib.h>

// include only used LibopenCM3 lib

#include <libopencm3/stm32/rcc.h>
#include <libopencm3/stm32/gpio.h>
#include <libopencm3/stm32/flash.h>
#include <libopencm3/usb/usbd.h>

// Define Dumper Pinout

#define D0 			GPIO9  // PB9
#define D1 			GPIO8  // PB8
#define D2 			GPIO7  // PB7
#define D3 			GPIO6  // PB6
#define D4 			GPIO4  // PB4
#define D5 			GPIO3  // PB3
#define D6 			GPIO15 // PA15
#define D7 			GPIO10 // PA10

#define D8 			GPIO9  // PA9
#define D9 			GPIO8  // PA8
#define D10 		GPIO15 // PB15
#define D11 		GPIO14 // PB14
#define D12 		GPIO13 // PB13
#define D13 		GPIO12 // dPB12
#define D14 		GPIO11 // PB11
#define D15			GPIO10 // PB10

#define OE 			GPIO1  // PB1
#define CE 			GPIO0  // PA0
#define MARK3 		GPIO1  // PA1
#define WE_FLASH	GPIO2  // PA2 	/ASEL B26
#define TIME 		GPIO3  // PA3	/TIME B31

#define CLK_CLEAR 	GPIO4  // PA4
#define CLK1 		GPIO7  // PA7
#define CLK2 		GPIO6  // PA6
#define CLK3 		GPIO5  // PA5

#define WE_SRAM 	GPIO15 // PC15	/LWR B28
#define LED_PIN 	GPIO13 // PC13

// USB Special Command

#define WAKEUP     		0x10
#define READ_MD         0x11
#define READ_MD_SAVE  	0x12
#define WRITE_MD_SAVE 	0x13
#define WRITE_MD_FLASH 	0x14
#define ERASE_MD_FLASH 	0x15
#define READ_SMS   		0x16
#define CFI_MODE   		0x17
#define INFOS_ID 		0x18
#define DEBUG_MODE 		0x19
#define MAPPER_SSF2 	0x20
#define UPDATE_READ	 	0x50
#define UPDATE_WRITE 	0x51
#define READ_REGISTER	0xFA
#define WRITE_REGISTER	0xFB

// USB Specific VAR

static char serial_no[25];
static uint8_t usb_buffer_IN[64];
static uint8_t usb_buffer_OUT[64];
static uint32_t len=0;

// Sega Dumper Specific Var

static const unsigned char stmReady[] = {'R','E','A','D','Y','!'};
static unsigned char dump_running = 0;
static unsigned long address = 0;
static unsigned char read8 = 0;
static unsigned int read16 = 0;
static unsigned int chipid = 0;

static unsigned int slotRegister = 0; //for SMS
static unsigned int slotAdr = 0; //for SMS

// Debug Mode
static unsigned char debug_mode = 0;
static unsigned char Debug_Time =0;
static unsigned char Debug_Asel =0;
static unsigned char Debug_LWR =0;

//  USB Specific Fonction ///// 

static const struct usb_device_descriptor dev = {
	.bLength = USB_DT_DEVICE_SIZE,
	.bDescriptorType = USB_DT_DEVICE,
	.bcdUSB = 0x0200,
	.bDeviceClass = 0xff,
	.bDeviceSubClass = 0,
	.bDeviceProtocol = 0,
	.bMaxPacketSize0 = 64,
	.idVendor = 0x0483,
	.idProduct = 0x5740,
	.bcdDevice = 0x0200,
	.iManufacturer = 1,
	.iProduct = 2,
	.iSerialNumber = 3,
	.bNumConfigurations = 1,
};

static const struct usb_endpoint_descriptor data_endp[] = {{
	.bLength = USB_DT_ENDPOINT_SIZE,
	.bDescriptorType = USB_DT_ENDPOINT,
	.bEndpointAddress = 0x01,
	.bmAttributes = USB_ENDPOINT_ATTR_BULK,
	.wMaxPacketSize = 64,
	.bInterval = 1,
}, {
	.bLength = USB_DT_ENDPOINT_SIZE,
	.bDescriptorType = USB_DT_ENDPOINT,
	.bEndpointAddress = 0x82,
	.bmAttributes = USB_ENDPOINT_ATTR_BULK,
	.wMaxPacketSize = 64,
	.bInterval = 1,
}};

static const struct usb_interface_descriptor data_iface[] = {{
	.bLength = USB_DT_INTERFACE_SIZE,
	.bDescriptorType = USB_DT_INTERFACE,
	.bInterfaceNumber = 1,
	.bAlternateSetting = 0,
	.bNumEndpoints = 2,
	.bInterfaceClass = 0xff,
	.bInterfaceSubClass = 0,
	.bInterfaceProtocol = 0,
	.iInterface = 0,

	.endpoint = data_endp,
}};

static const struct usb_interface ifaces[] = {{
	.num_altsetting = 1,
	.altsetting = data_iface,
}};

static const struct usb_config_descriptor config = {
	.bLength = USB_DT_CONFIGURATION_SIZE,
	.bDescriptorType = USB_DT_CONFIGURATION,
	.wTotalLength = 0,
	.bNumInterfaces = 1,
	.bConfigurationValue = 1,
	.iConfiguration = 0,
	.bmAttributes = 0x80,
	.bMaxPower = 0x32,

	.interface = ifaces,
};

static void usb_setup(void)
{
	/* Enable clocks for USB */
	//rcc_usb_prescale_1();
	rcc_periph_clock_enable(RCC_USB);

	// Cleaning USB Buffer

	int i=0;

	    for(i = 0; i < 64; i++)
    {
		usb_buffer_IN[i]=0x00;
		usb_buffer_OUT[i]=0x00;
	}
}

static const char *usb_strings[] = {
    "Ultimate-Consoles",
    "USB2 Sega Dumper",
    serial_no,
};

static char *get_dev_unique_id(char *s)
{
    volatile uint8_t *unique_id = (volatile uint8_t *)0x1FFFF7E8;
    int i;

    // Fetch serial number from chip's unique ID
    for(i = 0; i < 24; i+=2)
    {
        s[i] = ((*unique_id >> 4) & 0xF) + '0';
        s[i+1] = (*unique_id++ & 0xF) + '0';
    }
    for(i = 0; i < 24; i++)
        if(s[i] > '9')
            s[i] += 'A' - '9' - 1;

    return s;
}

//  Sega Dumper Specific Fonction ///// 

void wait(int nb){
    while(nb){ __asm__("nop"); nb--;}
}

void setDataInput(){
    GPIO_CRH(GPIOA) = 0x44444444;
    GPIO_CRL(GPIOB) = 0x44344434;
    GPIO_CRH(GPIOB) = 0x44444444;
}

void setDataOutput(){
    GPIO_CRH(GPIOA) = 0x34444333;
    GPIO_CRL(GPIOB) = 0x33333333;
    GPIO_CRH(GPIOB) = 0x33333333;
}

void buffer_checksum(unsigned char * buffer, unsigned char length){
	unsigned char i = 0;
	unsigned int check = 0;

	while( i < length){
		check += buffer[i];
		i++;
	}
	usb_buffer_OUT[0] = check & 0xff;
	usb_buffer_OUT[1] = (check >> 8)&0xff;
}

//precalc GPIOs states
const unsigned int lut_write8[] = {
0x0,0x200,0x100,0x300,0x80,0x280,0x180,0x380,0x40,0x240,0x140,0x340,0xC0,0x2C0,0x1C0,0x3C0,0x10,0x210,0x110,0x310,0x90,0x290,0x190,0x390,0x50,0x250,0x150,0x350,0xD0,0x2D0,0x1D0,0x3D0,0x8,0x208,0x108,0x308,0x88,0x288,0x188,0x388,0x48,0x248,0x148,0x348,0xC8,0x2C8,0x1C8,0x3C8,0x18,0x218,0x118,0x318,0x98,0x298,0x198,0x398,0x58,0x258,0x158,0x358,0xD8,0x2D8,0x1D8,0x3D8,0x8000,0x8200,0x8100,0x8300,0x8080,0x8280,0x8180,0x8380,0x8040,0x8240,0x8140,0x8340,0x80C0,0x82C0,0x81C0,0x83C0,0x8010,0x8210,0x8110,0x8310,0x8090,0x8290,0x8190,0x8390,0x8050,0x8250,0x8150,0x8350,0x80D0,0x82D0,0x81D0,0x83D0,0x8008,0x8208,0x8108,0x8308,0x8088,0x8288,0x8188,0x8388,0x8048,0x8248,0x8148,0x8348,0x80C8,0x82C8,0x81C8,0x83C8,0x8018,0x8218,0x8118,0x8318,0x8098,0x8298,0x8198,0x8398,0x8058,0x8258,0x8158,0x8358,0x80D8,0x82D8,0x81D8,0x83D8,0x400,0x600,0x500,0x700,0x480,0x680,0x580,0x780,0x440,0x640,0x540,0x740,0x4C0,0x6C0,0x5C0,0x7C0,0x410,0x610,0x510,0x710,0x490,0x690,0x590,0x790,0x450,0x650,0x550,0x750,0x4D0,0x6D0,0x5D0,0x7D0,0x408,0x608,0x508,0x708,0x488,0x688,0x588,0x788,0x448,0x648,0x548,0x748,0x4C8,0x6C8,0x5C8,0x7C8,0x418,0x618,0x518,0x718,0x498,0x698,0x598,0x798,0x458,0x658,0x558,0x758,0x4D8,0x6D8,0x5D8,0x7D8,0x8400,0x8600,0x8500,0x8700,0x8480,0x8680,0x8580,0x8780,0x8440,0x8640,0x8540,0x8740,0x84C0,0x86C0,0x85C0,0x87C0,0x8410,0x8610,0x8510,0x8710,0x8490,0x8690,0x8590,0x8790,0x8450,0x8650,0x8550,0x8750,0x84D0,0x86D0,0x85D0,0x87D0,0x8408,0x8608,0x8508,0x8708,0x8488,0x8688,0x8588,0x8788,0x8448,0x8648,0x8548,0x8748,0x84C8,0x86C8,0x85C8,0x87C8,0x8418,0x8618,0x8518,0x8718,0x8498,0x8698,0x8598,0x8798,0x8458,0x8658,0x8558,0x8758,0x84D8,0x86D8,0x85D8,0x87D8};

void directWrite8(unsigned char val){
	unsigned int invVal = 0;

	invVal = ~lut_write8[val] & 0x3D8;
	GPIOB_BSRR |= lut_write8[val] | (invVal << 16); //set and reset pins GPIOB

	invVal = ~lut_write8[val] & 0x8400;
	GPIOA_BSRR |= lut_write8[val] | (invVal << 16); //set and reset pins GPIOA
}

void directWrite16(unsigned int val){
	unsigned int invVal = 0;

	/*
	#define D0 			GPIO9  // PB9
	#define D1 			GPIO8  // PB8
	#define D2 			GPIO7  // PB7
	#define D3 			GPIO6  // PB6
	#define D4 			GPIO4  // PB4
	#define D5 			GPIO3  // PB3
	#define D10 		GPIO15 // PB15
	#define D11 		GPIO14 // PB14
	#define D12 		GPIO13 // PB13
	#define D13 		GPIO12 // PB12
	#define D14 		GPIO11 // PB11
	#define D15			GPIO10 // PB10
	#define D6 			GPIO15 // PA15
	#define D7 			GPIO10 // PA10
	#define D8 			GPIO9  // PA9
	#define D9 			GPIO8  // PA8
	busB = D0 - D5, D10 - D15 / mask FFD8
	busA = D6 - D9,  / mask 8700
	*/

	unsigned int busB = ((val&0x1)<<9) | ((val&0x2)<<7) | ((val&0x4)<<5) | ((val&0x8)<<3) | (val&0x10) | ((val&0x20)>>2) | ((val&0x400)<<5) | ((val&0x800)<<3) | ((val&0x1000)<<1) | ((val&0x2000)>>1) | ((val&0x4000)>>3) | ((val&0x8000)>>5); //D0 to D5 & D10 to D15
	unsigned int busA = ((val&0x40)<<9) | ((val&0x80)<<3) | ((val&0x100)<<1) | ((val&0x200)>>1);  //D6 to D9

	invVal = ~busB & 0xFFD8;
	GPIOB_BSRR |= busB | (invVal << 16); //set and reset pins GPIOB

	invVal = ~busA & 0x8700;
	GPIOA_BSRR |= busA | (invVal << 16); //set and reset pins GPIOA
}


void directRead8(){
	unsigned int busA = GPIOA_IDR;
	unsigned int busB = GPIOB_IDR;

    read8 = ((busB>>9)&0x1) | ((busB>>7)&0x2) | ((busB>>5)&0x4) | ((busB>>3)&0x8) | (busB&0x10) | ((busB<<2)&0x20) | ((busA>>9)&0x40) | ((busA>>3)&0x80); //Byte D0-7
}

void directRead16(){
	unsigned int busA = GPIOA_IDR;
	unsigned int busB = GPIOB_IDR;

	read16 = ((busB>>9)&0x1) | ((busB>>7)&0x2) | ((busB>>5)&0x4) | ((busB>>3)&0x8) | ((busB&0x10)) | ((busB<<2)&0x20) | ((busA>>9)&0x40) | ((busA>>3)&0x80) | ((busA>>1)&0x100) | ((busA<<1)&0x200) | ((busB>>5)&0x400) | ((busB>>3)&0x800) | ((busB>>1)&0x1000) | ((busB<<1)&0x2000) | ((busB<<3)&0x4000) | ((busB<<5)&0x8000); //Word D0-D15
}


void setAddress(unsigned long adr)
{

    setDataOutput();

    directWrite8(adr&0xFF);
    GPIOA_BRR  |= CLK1;
    GPIOA_BSRR |= CLK1;

    directWrite8((adr>>8)&0xFF);
    GPIOA_BRR  |= CLK2;
    GPIOA_BSRR |= CLK2;

    directWrite8((adr>>16)&0xFF);
    GPIOA_BRR  |= CLK3;
    GPIOA_BSRR |= CLK3;

}

void writeFlash8(int address, int byte)
{
    setAddress(address);
    setDataOutput();
    GPIOA_BRR  |= CE;
    GPIOC_BRR |= WE_SRAM;
    directWrite8(byte);
    GPIOC_BSRR |= WE_SRAM;
    GPIOA_BSRR  |= CE;
    setDataInput();
}

void readMd()  // 0.253 m/s
{

    unsigned char adr = 0;
    unsigned char adr16 = 0;

    setDataOutput(); // 1,3 µs

    GPIOA_BSRR |= CE | CLK1| CLK2 | CLK3 | (CLK_CLEAR<<16);
    GPIOB_BSRR |= OE;
    GPIOA_BSRR |= CLK_CLEAR;


    while(adr<64)  // 64bytes/32word 8,5 µs * 32
    {

        setAddress(address+adr16); //4,96µs
        setDataInput();

        GPIOA_BRR |= CE;
        GPIOB_BRR |= OE;

        wait(16);

        directRead16(); //save into read16 global
        usb_buffer_OUT[adr] = (read16 >> 8)&0xFF; //word to byte
        usb_buffer_OUT[(adr+1)] = read16 & 0xFF;

        GPIOB_BSRR |= OE;
        GPIOA_BSRR |= CE;

        adr+=2;  //buffer
        adr16++; //md adr word
    }


    /*if ( debug_mode == 1)
    {
    	if ( Debug_Time == 1 ){ GPIOA_BSRR |= TIME;}
    	if ( Debug_Time == 0 ){ GPIOA_BRR |= TIME;}
    	if ( Debug_LWR == 1 ){ GPIOC_BSRR |= WE_SRAM}
    	if ( Debug_LWR == 0 ){ GPIOC_BRR |= WE_SRAM}
    	if ( Debug_ASEL == 1 ){ GPIOA_BSRR |= WE_FLASH}
    	if ( Debug_ASEL == 0 ){ GPIOA_BRR |= WE_FLASH}

    }*/

}

void sms_mapper_register(unsigned char slot){

    setDataOutput();

    setAddress(slotRegister);
 	GPIOA_BSRR |= CE; //A15 hi
   	GPIOB_BRR  |= OE;
    GPIOC_BRR  |= WE_SRAM;

    directWrite8(slot); //slot 0..2

    GPIOC_BSRR |= WE_SRAM;
   	GPIOB_BSRR |= OE;
	GPIOA_BRR  |= CE; //A15 low

}

void readSMS()
{
    unsigned char adr = 0;
	unsigned char pageNum = 0;

    gpio_set_mode(GPIOA, GPIO_MODE_OUTPUT_50_MHZ, GPIO_CNF_OUTPUT_PUSHPULL, MARK3); //Mark as output (low)
	GPIOA_BSRR |= CLK1| CLK2 | CLK3 | TIME | WE_FLASH | ((CE | MARK3 | CLK_CLEAR)<<16);
	GPIOB_BSRR |= OE;
	GPIOA_BSRR |= CLK_CLEAR;

   	if(address < 0x4000){
  		slotRegister = 0xFFFD; 	// slot 0 sega
  		pageNum = 0;
   		slotAdr = address;
   	}else if(address < 0x8000){
  		slotRegister = 0xFFFE; 	// slot 1 sega
  		pageNum = 1;
   		slotAdr = address;
   	}else{
  		slotRegister = 0xFFFF; 	// slot 2 sega
  		pageNum = (address/0x4000); //page num max 0xFF - 32mbits
   		slotAdr = 0x8000 + (address & 0x3FFF);
   	}

	sms_mapper_register(pageNum);

	if(slotAdr > 0x7FFF){GPIOA_BSRR |= CE;} //CE md == A15 in SMS mode !

    while(adr<64){

		setAddress(slotAdr +adr);

	    setDataInput();
	    GPIOB_BRR |= OE;

	    wait(16);

	    directRead8(); //save into read8 global
        usb_buffer_OUT[adr] = read8;

	    GPIOB_BSRR |= OE;
		adr++;

	}
}

void readMdSave()
{

    unsigned char adr = 0;

    setDataOutput();

    GPIOA_BSRR |= CE | CLK1| CLK2 | CLK3 | WE_FLASH | (CLK_CLEAR<<16);
    GPIOB_BSRR |= OE;
    GPIOA_BSRR |= CLK_CLEAR;

    while(adr<64)
    {

        setAddress((address+adr));

        GPIOB_BSRR |= D0;
        GPIOA_BRR  |= TIME;
        GPIOA_BSRR  |= TIME;

        setDataInput();

        GPIOA_BRR  |= CE;
        GPIOB_BRR  |= OE;

        wait(16); //utile ?
        directRead8(); //save into read8 global
        usb_buffer_OUT[adr] = read8;

        //inhib OE
        GPIOA_BSRR |= CE;
        GPIOB_BSRR |= OE;

        adr++;
    }
}

void EraseMdSave()
{
    unsigned short adr = 0;
    setDataOutput();

    GPIOA_BSRR |= CE | CLK1| CLK2 | CLK3 | TIME | WE_FLASH | (CLK_CLEAR<<16);
    GPIOB_BSRR |= OE;
    GPIOA_BSRR |= CLK_CLEAR;

    // SRAM rom > 16Mbit
    GPIOB_BSRR |= D0;
    GPIOA_BRR  |= TIME;
    GPIOA_BSRR |= TIME;

    while(adr < 1024*32)
    {
        setAddress((address+adr));
        setDataOutput();
        directWrite8(0xFF);
        //byte = usb_buffer_IN[32+adr];
        //directWrite8(byte);
        GPIOA_BRR  |= CE;
        GPIOC_BRR  |= WE_SRAM;
        // wait(16); //utile ?
        //directWrite8(usb_buffer_IN[5+adr]);

        GPIOC_BSRR |= WE_SRAM;
        GPIOA_BSRR |= CE;
        setDataInput();
        adr++;
    }
}

void writeMdSave()
{

    gpio_clear(GPIOC, GPIO13); // LED on
    unsigned short adr = 0;
    unsigned char byte = 0;
    setDataOutput();

    GPIOA_BSRR |= CE | CLK1| CLK2 | CLK3 | TIME | WE_FLASH | (CLK_CLEAR<<16);
    GPIOB_BSRR |= OE;
    GPIOA_BSRR |= CLK_CLEAR;

    // SRAM rom > 16Mbit
    GPIOB_BSRR |= D0;
    GPIOA_BRR  |= TIME;
    GPIOA_BSRR |= TIME;

    while(adr < 32)
    {
        setAddress((address+adr));
        setDataOutput();
        //directWrite8(0xCC);
        byte = usb_buffer_IN[32+adr];
        directWrite8(byte);
        GPIOA_BRR  |= CE;
        GPIOC_BRR  |= WE_SRAM;
        // wait(16); //utile ?
        //directWrite8(usb_buffer_IN[5+adr]);

        GPIOC_BSRR |= WE_SRAM;
        GPIOA_BSRR |= CE;
        setDataInput();
        adr++;
    }
}

void writeFlash16(int address,unsigned short word)
{
   // GPIO_ODR(GPIOA) = GPIO_ODR(GPIOA) & ~(2 << 0); // WE 0 ( ASEL> WE Flash)
    GPIOC_BRR |= WE_SRAM;

    setAddress(address);
    setDataOutput();
    GPIO_ODR(GPIOA) = GPIO_ODR(GPIOA) & ~(1 << 0); // CE 0
    GPIO_ODR(GPIOB) = GPIO_ODR(GPIOB) | (1 << 1);  // OE 1
    directWrite16(word);
   // GPIO_ODR(GPIOA) = GPIO_ODR(GPIOA) | (2 << 0);  // WE 1 ( ASEL> WE Flash)

    GPIOC_BSRR |= WE_SRAM;

    GPIO_ODR(GPIOA) = GPIO_ODR(GPIOA) | (1 << 0);  // CE 1
    setDataInput();
}

void ResetFlash(void)
{
    writeFlash16(0x5555,0xAAAA);
    writeFlash16(0x2AAA,0x5555);
    writeFlash16(0x5555,0xF0F0);
}


void CFIWordProgram(void)
{
    writeFlash16(0x5555, 0xAAAA);
    writeFlash16(0x2AAA, 0x5555);
    writeFlash16(0x5555, 0xA0A0);
}

void commandMdFlash(unsigned long adr, unsigned int val)
{

    setAddress(adr);
    GPIOA_BRR  |= CE;
    GPIOA_BRR  |= WE_FLASH;

    GPIOC_BRR |= WE_SRAM;
    directWrite16(val);
    GPIOA_BSRR |= WE_FLASH;
    GPIOC_BSRR |= WE_SRAM;

    directWrite16(val);
    GPIOA_BSRR |= WE_FLASH;
    GPIOC_BSRR |= WE_SRAM;
    GPIOA_BSRR |= CE;
}

void reset_command()
{
    setDataOutput();

    commandMdFlash(0x5555, 0xAAAA);
    commandMdFlash(0x2AAA, 0x5555);
    commandMdFlash(0x5555, 0xF0F0);

    commandMdFlash(0x5555, 0xAA);
    commandMdFlash(0x2AAA, 0x55);
    commandMdFlash(0x5555, 0xF0);

    wait(16);
}


void EraseFlash()
{
    unsigned char poll_dq7=0;

    setDataOutput();

    GPIOA_BSRR |= CE | CLK1| CLK2 | CLK3 | TIME | WE_FLASH | (CLK_CLEAR<<16);
    GPIOB_BSRR |= OE;
    GPIOA_BSRR |= CLK_CLEAR;


    commandMdFlash(0x5555, 0xAAAA);
    commandMdFlash(0x2AAA, 0x5555);
    commandMdFlash(0x5555, 0x8080);
    commandMdFlash(0x5555, 0xAAAA);
    commandMdFlash(0x2AAA, 0x5555);
    commandMdFlash(0x5555, 0x1010);

    commandMdFlash(0x5555, 0xAA);
    commandMdFlash(0x2AAA, 0x55);
    commandMdFlash(0x5555, 0x80);
    commandMdFlash(0x5555, 0xAA);
    commandMdFlash(0x2AAA, 0x55);
    commandMdFlash(0x5555, 0x10);


    if(((chipid&0xFF00)>>8) == 0xBF)
    {
        //SST-MICROCHIP
        wait(2400000);
        gpio_clear(GPIOC, GPIO13);

    }
    else
    {

        reset_command();

        setAddress(0);
        setDataInput();
        GPIOA_BRR |= CE;
        GPIOB_BRR |= OE;
        wait(16);
        while(!poll_dq7)
        {
            poll_dq7 = (GPIOA_IDR >> 3)&0x80; //test only dq7
        }
        GPIOB_BSRR |= OE;
        GPIOA_BSRR |= CE;
    }

    reset_command();
    usb_buffer_OUT[0] = 0xFF;
}

void writeMdFlash()
{
    /*
    compatible
    29LV160 (amd)
    29LV320 (amd)
    29LV640 (amd)
    29W320 (st)
    29F800 (hynix)
    */

    //write in WORD
    unsigned char adr16=0;
    unsigned char j=5;
    unsigned char poll_dq7=0;
    unsigned char true_dq7=0;
    unsigned int val16=0;

    setDataOutput();

    GPIOA_BSRR |= CE | CLK1| CLK2 | CLK3 | TIME | WE_FLASH | (CLK_CLEAR<<16);
    GPIOB_BSRR |= OE;
    GPIOA_BSRR |= CLK_CLEAR;

    while(adr16 < (usb_buffer_IN[4]>>1))
    {

        val16 = ((usb_buffer_IN[j])<<8) | usb_buffer_IN[(j+1)];
        true_dq7 = (val16 & 0x80);
        poll_dq7 = ~true_dq7;

        if(val16!=0xFFFF)
        {
            setDataOutput();


            /*commandMdFlash(0x5555, 0xAAAA);
            commandMdFlash(0x2AAA, 0x5555);
            commandMdFlash(0x5555, 0xA0A0);

            commandMdFlash(0x5555, 0xAA);
            commandMdFlash(0x2AAA, 0x55);
            commandMdFlash(0x5555, 0xA0);*/
            //commandMdFlash((address+adr16), val16);
            CFIWordProgram();
            writeFlash16((address+adr16), val16);




            if(((chipid&0xFF00)>>8) == 0xBF)
            {
                wait(160); //SST Microchip
            }
            else
            {

                reset_command();

                GPIOA_BRR |= CE;
                GPIOB_BRR |= OE;
                setAddress((address+adr16));
                setDataInput();
                while(poll_dq7 != true_dq7)
                {
                    poll_dq7 = (GPIOA_IDR&0x400)>>3;
                }
                GPIOB_BSRR |= OE;
                GPIOA_BSRR |= CE;
            }
        }
        j+=2;
        adr16++;
    }
}
void infosId()
{
    //seems to be valid only for 29LVxxx ?
    setDataOutput();

    GPIOA_BSRR |= CE | CLK1| CLK2 | CLK3 | TIME | WE_FLASH | (CLK_CLEAR<<16);
    GPIOB_BSRR |= OE;
    GPIOA_BSRR |= CLK_CLEAR;

    commandMdFlash(0x5555, 0xAA);
    commandMdFlash(0x2AAA, 0x55);
    commandMdFlash(0x5555, 0x90);

    setAddress(0); //Manufacturer
    setDataInput();

    GPIOA_BRR |= CE;
    GPIOB_BRR |= OE;
    wait(16);
    directRead16();
    usb_buffer_OUT[0] = 0;
    usb_buffer_OUT[1] = read16&0xFF;
    GPIOB_BSRR |= OE;
    GPIOA_BSRR |= CE;

    reset_command();

    commandMdFlash(0x5555, 0xAA);
    commandMdFlash(0x2AAA, 0x55);
    commandMdFlash(0x5555, 0x90);

    setAddress(1); //Flash id
    setDataInput();
    GPIOA_BRR |= CE;
    GPIOB_BRR |= OE;
    wait(16);
    directRead16();
    usb_buffer_OUT[2] = 1;
    usb_buffer_OUT[3] = read16&0xFF;
    GPIOB_BSRR |= OE;
    GPIOA_BSRR |= CE;

    reset_command();

    chipid = (usb_buffer_OUT[1]<<8) | usb_buffer_OUT[3];
}

// Mapper SSF2 Specific Function


void WriteBankSSF2(unsigned long Address, unsigned char value)
{

	    setAddress(Address);
		setDataOutput();
        directWrite8(value);
		GPIOA_BRR |= TIME;
		GPIOC_BRR |= WE_SRAM;
		wait(16);
		GPIOC_BSRR |= WE_SRAM;
		GPIOA_BSRR |= TIME;	
		setDataInput();

		/*setAddress(0x50987E);
		setDataOutput();
        directWrite8(0x08);
		GPIOA_BRR |= TIME;
		GPIOC_BRR |= WE_SRAM;
		wait(16);
		GPIOC_BSRR |= WE_SRAM;
		GPIOA_BSRR |= TIME;	
		setDataInput();

		setAddress(0x50987F);
		setDataOutput();
        directWrite8(0x09);
		GPIOA_BRR |= TIME;
		GPIOC_BRR |= WE_SRAM;
		wait(16);
		GPIOC_BSRR |= WE_SRAM;
		GPIOA_BSRR |= TIME;	
		setDataInput();*/

}

// EEPROM Specific Function

void writeWord_MD(unsigned long myAddress, unsigned char myData) 
{
	GPIOA_BSRR |= OE; // OE 1
	setAddress(myAddress);
	setDataOutput();
	directWrite8(myData);
	wait(50);
	GPIOA_BRR |= CE; // CE 0
	GPIOC_BRR |= WE_SRAM; // LWR 0
	wait(50);
	GPIOC_BSRR |= WE_SRAM; // LWR 1
	GPIOA_BSRR |= CE; // CE 1
	wait(50);
	setDataInput();	
}

void SetSDA( unsigned char value)
{
	setAddress(0x100000);
	GPIOA_BRR |= CE;
	GPIOC_BRR |= WE_SRAM;
	GPIOB_BRR |= OE;
	directWrite8(value);
	wait(100);
	GPIOC_BSRR |= WE_SRAM;
}

void SetSCL( unsigned char value)
{
	writeWord_MD(0x100000,value);
}

// STM32 Update Specifc Function //

void STM32_Read_Flash() // Read 64 byte at specified address and send it to USB_buffer_out
{
	volatile uint32_t *pb; // STM32 Flash must be read in 32 bit mode
	unsigned char i=0;
	for(i=0; i<64; i=i+4)
	{
		pb = (volatile uint32_t *) address;
		usb_buffer_OUT[i]=*pb  & 0xFF;
		usb_buffer_OUT[i+1]=(*pb  & 0xFF00)>>8;
		usb_buffer_OUT[i+2]=(*pb  & 0xFF0000)>>16;
		usb_buffer_OUT[i+3]=(*pb  & 0xFF000000)>>24;
		address=address+4;
	}
}

void STM32_Unlock_Flash() // Unlock STM32 Flash Access
{
		flash_unlock();  // Unlock Write protection flag
		usb_buffer_OUT[2]=0xAA;
}

void STM32_Lock_Flash() // Unlock STM32 Flash Access
{
		flash_lock();  // Lock Write protection flag
		usb_buffer_OUT[2]=0xBB;
}

void STM32_Erase_Flash( unsigned char page_address) // Unlock STM32 Flash Access
{	
		uint32_t flash_status = 0;	
		flash_erase_page(2000); // Erase correct page address
		while ( flash_status != FLASH_SR_EOP){flash_status = flash_get_status_flags();} // Wait until operation is completed
}

/*
void STM32_Write_Flash() // Read 64 byte at specified address and send it to USB_buffer_out
{
	// STM32 page size is 1024 bytes

	flash_unlock();  // Unlock Write protection flag
	flash_erase_page(page_address); // Erase correct page address
	while ( flash_status != FLASH_SR_EOP){flash_status = flash_get_status_flags();}
	

}
*/


/// USB Specific Function


void SendNextPaquet(usbd_device *usbd_dev, uint8_t ep)
{

	unsigned char adr = 0;
	unsigned char adr16 = 0;

	GPIOA_BSRR |= CE | CLK1| CLK2 | CLK3 | TIME | WE_FLASH | (CLK_CLEAR<<16);
	GPIOB_BSRR |= OE;
	GPIOA_BSRR |= CLK_CLEAR;


    while(adr<64){ // 64bytes/32word

		//setAddress(address+adr16);

    GPIO_CRH(GPIOA) = 0x34444333; // Set Data OUT
    GPIO_CRL(GPIOB) = 0x33333333;
    GPIO_CRH(GPIOB) = 0x33333333;

    directWrite8((address+adr16) & 0xFF);
    GPIOA_BRR  |= CLK1;
    GPIOA_BSRR |= CLK1;

    directWrite8(((address+adr16)>>8)&0xFF);
    GPIOA_BRR  |= CLK2;
    GPIOA_BSRR |= CLK2;

    directWrite8(((address+adr16)>>16)&0xFF);
    GPIOA_BRR  |= CLK3;
    GPIOA_BSRR |= CLK3;

      GPIO_CRH(GPIOA) = 0x44444444; // Set Data IN
    GPIO_CRL(GPIOB) = 0x44344434;
    GPIO_CRH(GPIOB) = 0x44444444;


	    GPIOA_BRR |= CE;
	    GPIOB_BRR |= OE;

	    wait(16);

	    directRead16(); //save into read16 global
	    usb_buffer_OUT[adr] = (read16 >> 8)&0xFF; //word to byte
	    usb_buffer_OUT[(adr+1)] = read16 & 0xFF;

	    GPIOB_BSRR |= OE;
	    GPIOA_BSRR |= CE;

		adr+=2;  //buffer
		adr16++; //md adr word
	}       
usbd_ep_write_packet(usbd_dev,ep,usb_buffer_OUT,64);
address += 32;
len +=32;
}


/*
* This gets called whenever a new IN packet has arrived from PC to STM32
 */

static void usbdev_data_rx_cb(usbd_device *usbd_dev, uint8_t ep)
{
	(void)ep;
	(void)usbd_dev;

	usb_buffer_IN[0] = 0;
	usbd_ep_read_packet(usbd_dev, 0x01,usb_buffer_IN, 64); // Read Paquets from PC

	address = (usb_buffer_IN[3]<<16) | (usb_buffer_IN[2]<<8) | usb_buffer_IN[1];
	//dump_running = usb_buffer_IN[4];		

	if (usb_buffer_IN[0] == WAKEUP)   // Wake UP
   {
				memcpy((unsigned char *)usb_buffer_OUT, (unsigned char *)stmReady, sizeof(stmReady));
				usbd_ep_write_packet(usbd_dev, 0x82,usb_buffer_OUT,64);
	}

	if (usb_buffer_IN[0] == READ_MD && usb_buffer_IN[4] != 1 )   // READ MD Exchange mode ( Low Speed)
   {
		dump_running = 0;
		readMd();
		usbd_ep_write_packet(usbd_dev, 0x82,usb_buffer_OUT,64);
	}

	if (usb_buffer_IN[0] == READ_MD && usb_buffer_IN[4] == 1 )   // READ MD Streaming mode ( High Speed)
   {
		dump_running = 1;
		SendNextPaquet(usbd_dev,0x82);
	}

	if (usb_buffer_IN[0] == READ_MD_SAVE)  						 // READ MD Save
   {
		dump_running = 0;
		readMdSave();
		usbd_ep_write_packet(usbd_dev, 0x82,usb_buffer_OUT,64);
	}

	if (usb_buffer_IN[0] == WRITE_MD_SAVE)   					// WRITE MD Save
   {
		dump_running = 0;
		writeMdSave();
		usb_buffer_OUT[6]=0xAA;
		usbd_ep_write_packet(usbd_dev, 0x82,usb_buffer_OUT,64);
		usb_buffer_OUT[6]=0x00;
	}

	if (usb_buffer_IN[0] == ERASE_MD_FLASH)   					// ERASE MD Flash
   {
		dump_running = 0;
		infosId();
        EraseFlash();
		usb_buffer_OUT[0]=0xFF;
		usbd_ep_write_packet(usbd_dev, 0x82,usb_buffer_OUT,64);
		usb_buffer_OUT[6]=0x00;
   }

	if (usb_buffer_IN[0] == WRITE_MD_FLASH)   					// WRITE MD Flash
   {
		dump_running = 0;
		writeMdFlash();
		usbd_ep_write_packet(usbd_dev, 0x82,usb_buffer_OUT,64);
		usb_buffer_OUT[6]=0x00;
	}
	
	if (usb_buffer_IN[0] == READ_SMS)  						 // READ SMS
   {
		dump_running = 0;
		readSMS();
		usbd_ep_write_packet(usbd_dev, 0x82,usb_buffer_OUT,64);
	}

	if (usb_buffer_IN[0] == INFOS_ID)   // Chip Information
   {
		dump_running = 0;
		GPIOA_BRR  |= WE_FLASH;
		usb_buffer_OUT[6]=0xFF;
		infosId();
		usbd_ep_write_packet(usbd_dev, 0x82,usb_buffer_OUT,64);
		usb_buffer_OUT[6]=0x00;
   }

	if (usb_buffer_IN[0] == DEBUG_MODE)   // Debug Mode
    {
        debug_mode = 1;
        if (usb_buffer_IN[1] == 1)
        {
            Debug_LWR=1;
        }
        if (usb_buffer_IN[1] == 0)
        {
            Debug_LWR=0;
        }
        if (usb_buffer_IN[2] == 1)
        {
            Debug_Asel=1;
        }
        if (usb_buffer_IN[2] == 0)
        {
            Debug_Asel=0;
        }
        if (usb_buffer_IN[3] == 1)
        {
            Debug_Time=1;
        }
        if (usb_buffer_IN[3] == 0)
        {
            Debug_Time=0;
        }
    }

	if (usb_buffer_IN[0] == MAPPER_SSF2)   // Mapper SSF2
    {
		WriteBankSSF2(address,usb_buffer_IN[5]);
	}

	if (usb_buffer_IN[0] == 0xFA)   // Read Register
   {

     
// 05 = 0xFA 01=0xCA
		
		
	    GPIO_CRH(GPIOA) = 0x34444333; // Set Data OUT
        GPIO_CRL(GPIOB) = 0x33333333;
        GPIO_CRH(GPIOB) = 0x33333333;
		directWrite8(0x02);
        GPIOA_BRR  |= CLK1;
   	    GPIOA_BSRR |= CLK1;
		GPIOB_BSRR |= OE;
	    GPIOA_BSRR |= CE;
		GPIOA_BRR |= TIME;
		GPIOC_BRR |= WE_SRAM;
		wait(16);
		GPIOC_BSRR |= WE_SRAM;	
		setDataInput();
		gpio_clear(GPIOC, GPIO13); // LED on/off
		directRead16(); //save into read16 global
	    usb_buffer_OUT[(1)] = read16 & 0xFF;
		usbd_ep_write_packet(usbd_dev, 0x82,usb_buffer_OUT,64);
   }

	 if (usb_buffer_IN[0] == UPDATE_READ)   // Update Read Mode
    {
		gpio_clear(GPIOC, GPIO13);
		STM32_Read_Flash();
		usbd_ep_write_packet(usbd_dev, 0x82,usb_buffer_OUT,64);
	}

	if (usb_buffer_IN[0] == UPDATE_WRITE)   // Update Write Mode
    {
		if (usb_buffer_IN[1] == 0xCC ) {STM32_Unlock_Flash();}
		if (usb_buffer_IN[1] == 0xDD ) {STM32_Lock_Flash();}
		if (usb_buffer_IN[2] != 0x00 ) {STM32_Erase_Flash(usb_buffer_IN[2]);}
		usbd_ep_write_packet(usbd_dev, 0x82,usb_buffer_OUT,64);
	}

}

/*
* This gets called whenever a new OUT packet has been send from STM32 to PC
*/

static void usbdev_data_tx_cb(usbd_device *usbd_dev, uint8_t ep)
{
	(void)ep;
	(void)usbd_dev;


if ( dump_running == 1 ){
GPIOA_BRR |= WE_FLASH;
SendNextPaquet(usbd_dev,0x82);}
GPIOA_BSRR |= WE_FLASH;
}


static void usbdev_set_config(usbd_device *usbd_dev, uint16_t wValue)
{
	(void)wValue;
	(void)usbd_dev;

	usbd_ep_setup(usbd_dev, 0x01, USB_ENDPOINT_ATTR_BULK, 64, usbdev_data_rx_cb);
	usbd_ep_setup(usbd_dev, 0x82, USB_ENDPOINT_ATTR_BULK, 64, usbdev_data_tx_cb);
}



//  Main Fonction ///// 

uint8_t usbd_control_buffer[128];

int main(void)
{
	int i=0;
	usbd_device *usbd_dev;

    // Init Clock

	rcc_clock_setup_in_hse_8mhz_out_72mhz();
    rcc_periph_clock_enable(RCC_GPIOA);
    rcc_periph_clock_enable(RCC_GPIOB);
    rcc_periph_clock_enable(RCC_GPIOC);

   /* Enable alternate function peripheral clock. */
    rcc_periph_clock_enable(RCC_AFIO);

   // Led ON

   gpio_set_mode(GPIOC, GPIO_MODE_OUTPUT_10_MHZ,GPIO_CNF_OUTPUT_PUSHPULL, GPIO13);
   gpio_clear(GPIOC, GPIO13); // LED on/off

    // Force USB Re-enumeration after bootloader is executed

	gpio_set_mode(GPIOA, GPIO_MODE_OUTPUT_2_MHZ,
		GPIO_CNF_OUTPUT_PUSHPULL, GPIO12);
	gpio_clear(GPIOA, GPIO12);
for( i = 0; i < 0x800000; i++){ __asm__("nop"); } //1sec
	get_dev_unique_id(serial_no);

     // Init USB2 Sega Dumper

	usb_setup();
	usbd_dev = usbd_init(&st_usbfs_v1_usb_driver, &dev, &config, usb_strings,
			3, usbd_control_buffer, sizeof(usbd_control_buffer));
	usbd_register_set_config_callback(usbd_dev, usbdev_set_config);

	for (i = 0; i < 0x800000; i++)
		__asm__("nop");

	// GPIO Init

	  //Full GPIO
      AFIO_MAPR = AFIO_MAPR_SWJ_CFG_JTAG_OFF_SW_OFF;
      GPIO_CRL(GPIOA) = 0x33333313; //always ouput (ce, clear etc) expact MARK 3 
      GPIO_CRH(GPIOA) = 0x34444333;
	  GPIO_CRL(GPIOB) = 0x33333333;
      GPIO_CRH(GPIOB) = 0x33333333;

    gpio_set_mode(GPIOC, GPIO_MODE_OUTPUT_50_MHZ, GPIO_CNF_OUTPUT_PUSHPULL, LED_PIN);
    gpio_set_mode(GPIOC, GPIO_MODE_OUTPUT_50_MHZ, GPIO_CNF_OUTPUT_PUSHPULL, WE_SRAM);
    gpio_set_mode(GPIOA, GPIO_MODE_OUTPUT_50_MHZ, GPIO_CNF_OUTPUT_PUSHPULL, CLK_CLEAR);
	gpio_set_mode(GPIOA, GPIO_MODE_OUTPUT_50_MHZ, GPIO_CNF_OUTPUT_PUSHPULL, TIME);
  //  gpio_set_mode(GPIOA, GPIO_MODE_OUTPUT_50_MHZ, GPIO_CNF_OUTPUT_PUSHPULL, MARK3);
	GPIOA_BSRR |= CLK_CLEAR;
	GPIOA_BSRR |= TIME;
   // GPIOA_BSRR |= MARK3;
	GPIOC_BSRR |= WE_SRAM | (LED_PIN<<16); //inhib
	gpio_set(GPIOC, GPIO13); // Turn Led OFF

	dump_running = 0;


	while(1)
	{
       usbd_poll(usbd_dev);

	}

}



