/*
Sega Dumper Software

------------------------------------------------------
V2.1 06/2020
*Add USB Update System
------------------------------------------------------
V2.0 05/2020
*Add SSF2 mapper Support
*Add Serial EEPROM Game Detection "not finished yet"
*Add Extra Hardware Game Detection "not finished yet"
------------------------------------------------------
X-death
*/

#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <ctype.h>
#include <time.h>
#include <math.h>

#include <libusb.h>
#include <assert.h>


// Specific Win32 lib ( only used for debug )

// #include <conio.h>

// USB Special Command

#define WAKEUP  0x10  // WakeUP for first STM32 Communication
#define READ_MD 0x11
#define READ_MD_SAVE 0x12
#define WRITE_MD_SAVE 0x13
#define WRITE_MD_FLASH 	0x14
#define ERASE_MD_FLASH 0x15
#define READ_SMS   		0x16
#define INFOS_ID 0x18
#define DEBUG_MODE 0x19
#define MAPPER_SSF2 0x20
#define UPDATE_READ  0x50
#define UPDATE_WRITE 0x51
#define UPDATE_WRITE 	0x51
// Sega Dumper Specific Variable

char * game_rom = NULL;
char * game_name = NULL;
const char unk[] = {"unknown"};
const char * save_msg[] = {	"WRITE SMD save",  //0
                            "ERASE SMD save"
                          }; //1
const char * wheel[] = { "-","\\","|","/"}; //erase wheel

// Sega Dumper Specific Function

void pause(char const *message)
{
    int c;

    printf("%s", message);
    fflush(stdout);

    while ((c = getchar()) != '\n' && c != EOF)
    {
    }
}

int array_search(unsigned int find, const int * tab, int inc, int tab_size)
{
    int i=0;
    for(i=0; i<tab_size; (i+=inc))
    {
        if(tab[i] == find)
        {
#if defined(DEBUG)
            printf("\n tab:%X find:%X, i:%d, i/inc:%d", tab[i], find, i, (i/inc));
#endif
            return i;
        }
    }
    return -1; //nothing found
}


unsigned int trim(unsigned char * buf, unsigned char is_out)
{

    unsigned char i=0, j=0;
    unsigned char tmp[49] = {0}; //max
    unsigned char tmp2[49] = {0}; //max
    unsigned char next = 1;

    /*check ascii remove unwanted ones and transform upper to lowercase*/
    if(is_out)
    {
        while(i<48)
        {
            if(buf[i]<0x30 || buf[i]>0x7A || (buf[i]>0x29 && buf[i]<0x41) || (buf[i]>0x5A && buf[i]<0x61))
                buf[i] = 0x20; //remove shiiit
            if(buf[i]>0x40 && buf[i]<0x5B)
                buf[i] += 0x20; //to lower case A => a
            i++;
        }
        i=0;
    }

    while(i<48)
    {
        if(buf[i]!=0x20)
        {
            if(buf[i]==0x2F)
                buf[i] = '-';
            tmp[j]=buf[i];
            tmp2[j]=buf[i];
            next = 1;
            j++;
        }
        else
        {
            if(next)
            {
                tmp[j]=0x20;
                tmp2[j]='_';
                next = 0;
                j++;
            }
        }
        i++;
    }

    next=0;
    if(tmp2[0]=='_')
    {
        next=1;    //offset
    }
    if(tmp[(j-1)]==0x20)
    {
        tmp[(j-1)] = tmp2[(j-1)]='\0';
    }
    else
    {
        tmp[j] = tmp2[j]='\0';
    }

    if(is_out)  //+4 for extension
    {
        game_rom = (char *)malloc(j-next +4);
        memcpy((unsigned char *)game_rom, (unsigned char *)tmp2 +next, j-next); //stringed file
    }

    game_name = (char *)malloc(j-next);
    memcpy((unsigned char *)game_name, (unsigned char *)tmp +next, j-next); //trimmed
    return 0;
}

int main()
{

    // LibUSB Specific Var


    int res                      = 0;        /* return codes from libusb functions */
    libusb_device_handle* handle = 0;        /* handle for USB device */
    int kernelDriverDetached     = 0;        /* Set to 1 if kernel driver detached */
    int numBytes                 = 0;        /* Actual bytes transferred. */
    unsigned char usb_buffer_in[64] = {0};   /* 64 byte transfer buffer IN */
    unsigned char usb_buffer_out[64] = {0};  /* 64 byte transfer buffer OUT */
    unsigned long len            = 0;        /* Number of bytes transferred. */

    // Dumper Specific Var

    unsigned long i=0;
    unsigned long j=0;
    unsigned long address=0;
    unsigned long save_address = 0;
    unsigned char *buffer_rom = NULL;
    unsigned char *buffer_save = NULL;
    unsigned char buffer_8b[64];
    unsigned char *buffer_header = NULL;
    unsigned char region[5];
    unsigned char odd=0;
    char *game_region = NULL;
    unsigned char choixMenu=0;
    int checksum_header = 0;
    unsigned char manufacturer_id=0;
    unsigned char chip_id=0;
    unsigned char sms_bank=0;

    // File manipulation Specific Var

    FILE *myfile;
    unsigned char octetActuel=0;

    int game_size=0;
    unsigned long save_size1 = 0;
    unsigned long save_size2 = 0;
    unsigned long save_size = 0;

    // Debug Specific Var

    unsigned char DisplayDebug =0;
    unsigned char Control_Data =0;
    unsigned char Debug_Time =1;
    unsigned char Debug_Asel =1;
    unsigned char Debug_LWR =1;
    char DebugCommand[0];
    unsigned long DebugAddress = 0;
    unsigned long CurrentAddress = 0;
    unsigned short CurrentData = 0xF;
    unsigned char *BufferROM;
    char dump_name[64];

// Serial EEPROM Lookup Table by Skamann
// https://github.com/sanni/cartreader/blob/master/Cart_Reader/MD.ino#L27

//***********************************************
// EEPROM SAVE TYPES
// 1 = Acclaim Type 1    [24C02]
// 2 = Acclaim Type 2    [24C02/24C16/24C65]
// 3 = Capcom/SEGA       [24C01]
// 4 = EA                [24C01]
// 5 = Codemasters       [24C08/24C16/24C65]
//***********************************************
    unsigned char eepType;

//*********************************************************
// SERIAL EEPROM LOOKUP TABLE
// Format = {chksum, eepType | eepSize}
// chksum is located in ROM at 0x18E (0xC7)
// eepType and eepSize are combined to conserve memory
//*********************************************************
    static const unsigned short eepid [] =
    {
        // ACCLAIM TYPE 1
        0x5B9F, 0x101,  // NBA Jam (J)
        0x694F, 0x101,  // NBA Jam (UE) (Rev 0)
        0xBFA9, 0x101,  // NBA Jam (UE) (Rev 1)
        // ACCLAIM TYPE 2
        0x16B2, 0x102,  // Blockbuster World Videogame Championship II (U)   [NO HEADER SAVE DATA]
        0xCC3F, 0x102,  // NBA Jam Tournament Edition (W) (Rev 0)            [NO HEADER SAVE DATA]
        0x8AE1, 0x102,  // NBA Jam Tournament Edition (W) (Rev 1)            [NO HEADER SAVE DATA]
        0xDB97, 0x102,  // NBA Jam Tournament Edition 32X (W)
        0x7651, 0x102,  // NFL Quarterback Club (W)
        0xDFE4, 0x102,  // NFL Quarterback Club 32X (W)
        0x3DE6, 0x802,  // NFL Quarterback Club '96 (UE)
        0xCB78, 0x2002, // Frank Thomas Big Hurt Baseball (UE)
        0x6DD9, 0x2002, // College Slam (U)
        // CAPCOM
        0xAD23, 0x83,   // Mega Man:  The Wily Wars (E)
        0xEA80, 0x83,   // Rockman Megaworld (J)
        // SEGA
        0x760F, 0x83,   // Evander "Real Deal" Holyfield Boxing (JU)
        0x95E7, 0x83,   // Greatest Heavyweights of the Ring (E)
        0x7270, 0x83,   // Greatest Heavyweights of the Ring (U)
        0xBACC, 0x83,   // Honoo no Toukyuuji Dodge Danpei (J)
        0xB939, 0x83,   // MLBPA Sports Talk Baseball (U)                    [BAD HEADER SAVE DATA]
        0x487C, 0x83,   // Ninja Burai Densetsu (J)
        0x740D, 0x83,   // Wonder Boy in Monster World (B)
        0x0278, 0x83,   // Wonder Boy in Monster World (J)
        0x9D79, 0x83,   // Wonder Boy in Monster World (UE)
        // EA
        0x8512, 0x84,   // Bill Walsh College Football (UE)                  [BAD HEADER SAVE DATA]
        0xA107, 0x84,   // John Madden Football '93 (UE)                     [NO HEADER SAVE DATA]
        0x5807, 0x84,   // John Madden Football '93 Championship Edition (U) [NO HEADER SAVE DATA]
        0x2799, 0x84,   // NHLPA Hockey '93 (UE) (Rev 0)                     [NO HEADER SAVE DATA]
        0xFA57, 0x84,   // NHLPA Hockey '93 (UE) (Rev 1)                     [NO HEADER SAVE DATA]
        0x8B9F, 0x84,   // Rings of Power (UE)                               [NO HEADER SAVE DATA]
        // CODEMASTERS
        0x7E65, 0x405,  // Brian Lara Cricket (E)                            [NO HEADER SAVE DATA]
        0x9A5C, 0x2005, // Brian Lara Cricket 96 (E) (Rev 1.0)               [NO HEADER SAVE DATA]
        0xC4EE, 0x2005, // Brian Lara Cricket 96 (E) (Rev 1.1)               [NO HEADER SAVE DATA]
        0x7E50, 0x805,  // Micro Machines 2 (E) (J-Cart)                     [NO HEADER SAVE DATA]
        0x165E, 0x805,  // Micro Machines '96 (E) (J-Cart) (Rev 1.0/1.1)     [NO HEADER SAVE DATA]
        0x168B, 0x405,  // Micro Machines Military (E) (J-Cart)              [NO HEADER SAVE DATA]
        0x12C1, 0x2005, // Shane Warne Cricket (E)                           [NO HEADER SAVE DATA]
    };
    unsigned short eepdata=0;
    unsigned char eeptype;
    unsigned char eepsize;

// Extra Hardware Type

//*****************************************************
// Hardware TYPES
// 0 = No Mapper
// 1 = Sega Virtual Processor SVP Samsung DSP SSP1601
// 2 = Sega 315-5779 Mapper Super Street Fighter 2
// 3 = WaterMelon T-5740 Mapper Pier Solar
//*****************************************************

    unsigned char Hardwaretype=0;

// Extra Hardware Lookup Table
// Format = {chksum, Hardwaretype | CartrdigeSize}
// chksum is located in ROM at 0x18E (0xC7)
// Type and Size are combined to conserve memory

    static const unsigned short HardwareID [] =
    {

        0x345D, 0x0102,  // Virtua Racing EUR
        0x95AB, 0x0102,  // Virtua Racing USA
        0x3B99, 0x0102,  // Virtua Racing JAP
        0xE41D, 0x0205,  // Super Street Fighter 2 EUR
        0xE017, 0x0205,  // Super Street Fighter 2 USA
        0xCE25, 0x0205,  // Super Street Fighter 2 JAP
    };
    unsigned short Hardwaredata=0;
    unsigned long Hardwaresize=0;

    // Main Program

    printf("\n");
    printf(" ---------------------------------\n");
    printf("    Sega Dumper USB2 Software     \n");
    printf(" ---------------------------------\n");

    printf("Init LibUSB... \n");

    /* Initialise libusb. */

    res = libusb_init(0);
    if (res != 0)
    {
        fprintf(stderr, "Error initialising libusb.\n");
        return 1;
    }

    printf("LibUSB Init Sucessfully ! \n");


    printf("Detecting Sega Dumper... \n");

    /* Get the first device with the matching Vendor ID and Product ID. If
     * intending to allow multiple demo boards to be connected at once, you
     * will need to use libusb_get_device_list() instead. Refer to the libusb
     * documentation for details. */

    handle = libusb_open_device_with_vid_pid(0, 0x0483, 0x5740);

    if (!handle)
    {
        fprintf(stderr, "Unable to open device.\n");
        return 1;
    }

    /* Claim interface #0. */

    res = libusb_claim_interface(handle, 0);
    if (res != 0)
    {
        res = libusb_claim_interface(handle, 1);
        if (res != 0)
        {
            printf("Error claiming interface.\n");
            return 1;
        }
    }

// Clean Buffer
    for (i = 0; i < 64; i++)
    {
        usb_buffer_in[i]=0x00;
        usb_buffer_out[i]=0x00;
    }


    printf("Sega Dumper Found ! \n");
    printf("Reading cartridge type ...\n");



// At this step we can try to read the buffer first wake up Sega Dumper

    usb_buffer_out[0] = WAKEUP;// Affect request to  WakeUP Command

    libusb_bulk_transfer(handle, 0x01,usb_buffer_out, sizeof(usb_buffer_out), &numBytes, 0); // Send Packets to Sega Dumper

    libusb_bulk_transfer(handle, 0x82, usb_buffer_in, sizeof(usb_buffer_in), &numBytes, 0);


// Now try to detect cartridge type ( SMS or MD )

    // First try to read ROM MD Header

    buffer_header = (unsigned char *)malloc(0x200);
    i = 0;
    address = 0x80;

    // Cleaning header Buffer
    for (i=0; i<512; i++)
    {
        buffer_header[i]=0x00;
    }

    i = 0;


    while (i<8)
    {

        usb_buffer_out[0] = READ_MD;
        usb_buffer_out[1] = address&0xFF ;
        usb_buffer_out[2] = (address&0xFF00)>>8;
        usb_buffer_out[3]=(address & 0xFF0000)>>16;
        usb_buffer_out[4] = 0; // Slow Mode

        libusb_bulk_transfer(handle, 0x01,usb_buffer_out, sizeof(usb_buffer_out), &numBytes, 60000);
        libusb_bulk_transfer(handle, 0x82,buffer_header+(64*i),64, &numBytes, 60000);
        address+=32;
        i++;
    }


    if(memcmp((unsigned char *)buffer_header,"SEGA",4) == 0)
    {
        printf("\nMegadrive/Genesis/32X cartridge detected!\n");

        for(i=0; i<(256/16); i++)
        {
            printf("\n");
            printf(" %03lX", 0x100+(i*16));
            for(j=0; j<16; j++)
            {
                printf(" %02X", buffer_header[j+(i*16)]);
            }
            printf(" %.*s", 16, buffer_header +(i*16));
        }

        printf("\n");
        printf("\n --- HEADER ---\n");
        memcpy((unsigned char *)dump_name, (unsigned char *)buffer_header+32, 48);
        trim((unsigned char *)dump_name, 0);
        printf(" Domestic: %.*s\n", 48, (char *)game_name);
        memcpy((unsigned char *)dump_name, (unsigned char *)buffer_header+80, 48);
        trim((unsigned char *)dump_name, 0);

        printf(" International: %.*s\n", 48, game_name);
        printf(" Release date: %.*s\n", 16, buffer_header+0x10);
        printf(" Version: %.*s\n", 14, buffer_header+0x80);
        memcpy((unsigned char *)region, (unsigned char *)buffer_header +0xF0, 4);
        for(i=0; i<4; i++)
        {
            if(region[i]==0x20)
            {
                game_region = (char *)malloc(i);
                memcpy((unsigned char *)game_region, (unsigned char *)buffer_header +0xF0, i);
                game_region[i] = '\0';
                break;
            }
        }

        if(game_region[0]=='0')
        {
            game_region = (char *)malloc(4);
            memcpy((char *)game_region, (char *)unk, 3);
            game_region[3] = '\0';
        }

        printf(" Region: %s\n", game_region);

        checksum_header = (buffer_header[0x8E]<<8) | buffer_header[0x8F];
        printf(" Checksum: %X\n", checksum_header);

        game_size = 1 + ((buffer_header[0xA4]<<24) | (buffer_header[0xA5]<<16) | (buffer_header[0xA6]<<8) | buffer_header[0xA7])/1024;
        printf(" Game size: %dKB\n", game_size);

        // EEPROM detection Specific code

        // Search checksum cartridge in EEPROM game table

        for (i = 0; i < sizeof(eepid)/sizeof(short); i++)
        {
            if ( checksum_header == eepid[i] )
            {
                printf(" Extra Memory : Yes ");
                eepdata=eepid[i+1];
                //printf(" EEPROM data : %X\n",eepdata);
                eepsize = eepdata & 0x0F;
                eeptype = eepdata >> 8;
                if ( eepdata == 0x0101)
                {
                    printf(" EEPROM type 1 : ACCLAIM 24C01 \n");;
                    save_size=128;
                }
                if ( eepdata == 0x0102)
                {
                    printf(" EEPROM type 2 : ACCLAIM 24C02 \n");
                    save_size=256;
                }
                if ( eepdata == 0x0802)
                {
                    printf(" EEPROM type 2 : ACCLAIM 24C16 \n");
                    save_size=2048;
                }
                if ( eepdata == 0x2002)
                {
                    printf(" EEPROM type 2 : ACCLAIM 24C64 \n");
                    save_size=8192;
                }
                if ( eepdata == 0x83)
                {
                    printf(" EEPROM type 3 : CAPCOM/SEGA 24C01 \n");
                    save_size=128;
                }
                if ( eepdata == 0x84)
                {
                    printf(" EEPROM type 4 : ELECTRONIC ARTS 24C01 \n");
                    save_size=128;
                }
                if ( eepdata == 0x405)
                {
                    printf(" EEPROM type 5 : CODEMASTERS 24C08 \n");
                    save_size=1024;
                }
                if ( eepdata == 0x805)
                {
                    printf(" EEPROM type 5 : CODEMASTERS 24C16 \n");
                    save_size=2048;
                }
                if ( eepdata == 0x2005)
                {
                    printf(" EEPROM type 5 : CODEMASTERS 24C64 \n");
                    save_size=8192;
                }
                //printf(" EEPROM type : %d\n",eeptype);
                //printf(" EEPROM size : %d\n",eepsize);
                printf(" Save size : %d bytes \n",save_size);
            }
        }


        if ( eepdata ==0 )
        {
            if((buffer_header[0xB0] + buffer_header[0xB1])!=0x93)
            {
                printf(" Extra Memory : No\n");
            }
            else
            {
                printf(" Extra Memory : Yes ");

                switch(buffer_header[0xB2])
                {
                case 0xF0:
                    printf(" 8bit backup SRAM (even addressing)\n");
                    break;
                case 0xF8:
                    printf(" 8bit backup SRAM (odd addressing)\n");
                    break;
                case 0xB8:
                    printf(" 8bit volatile SRAM (odd addressing)\n");
                    break;
                case 0xB0:
                    printf(" 8bit volatile SRAM (even addressing)\n");
                    break;
                case 0xE0:
                    printf(" 16bit backup SRAM\n");
                    break;
                case 0xA0:
                    printf(" 16bit volatile SRAM\n");
                    break;
                }
                if ( buffer_header[0xB2] != 0xE0 | buffer_header[0xB2] != 0xA0 ) // 8 bit SRAM
                {
                    save_size2 = (buffer_header[0xB8]<<24) | (buffer_header[0xB9]<<16) | (buffer_header[0xBA] << 8) | buffer_header[0xBB];
                    save_size1 = (buffer_header[0xB4]<<24) | (buffer_header[0xB5]<<16) | (buffer_header[0xB6] << 8) | buffer_header[0xB7];

                    save_size = save_size2 - save_size1;
                    save_size = (save_size/1024); // Kb format
                    save_size=(save_size/2) + 1; // 8bit size
                }
                save_address = (buffer_header[0xB4]<<24) | (buffer_header[0xB5]<<16) | (buffer_header[0xB6] << 8) | buffer_header[0xB7];
                printf(" Save size: %dKb\n", save_size);
                printf(" Save address: %lX\n", save_address);
            }
        }

        // Extra Hardware detection Specific code

        // Search checksum cartridge in Custom Hardware game table

        for (i = 0; i < sizeof(HardwareID)/sizeof(short); i++)
        {
            if ( checksum_header == HardwareID[i] )
            {
                Hardwaredata=HardwareID[i+1];
                Hardwaretype = Hardwaredata >> 8;
                Hardwaresize = Hardwaredata & 0x0F;
                if ( Hardwaredata == 0x0205)
                {
                    printf(" Extra Hardware : Sega 315-5779 Mapper Super Street Fighter 2 \n");
                    game_size=1024*Hardwaresize*1024;
                }
            }
            // To Do Add Overdrive 2 detection support : https://plutiedev.com/beyond-4mb
        }
    }



    else   // Try to read in SMS mode
    {

        address = 0x7FF0;
        usb_buffer_out[0] = READ_SMS;
        usb_buffer_out[1] = address&0xFF ;
        usb_buffer_out[2] = (address&0xFF00)>>8;
        usb_buffer_out[3]=(address & 0xFF0000)>>16;
        usb_buffer_out[4] = 0; // Slow Mode

        libusb_bulk_transfer(handle, 0x01,usb_buffer_out, sizeof(usb_buffer_out), &numBytes, 0);
        libusb_bulk_transfer(handle, 0x82, usb_buffer_in, sizeof(usb_buffer_in), &numBytes, 0);

        //printf("\nDisplaying USB IN buffer\n\n");
        for (i = 0; i < 64; i++)
            //   {
            //    printf("%02X ",usb_buffer_in[i]);
            //	j++;
            //	if (j==16){printf("\n");j=0;}
            //   }

            if(memcmp((unsigned char *)usb_buffer_in,"TMR SEGA",8) == 0)
            {
                printf("\nMaster System/Mark3 cartridge detected !\n");
                printf("Region : ");
                if ( usb_buffer_in[15] >> 6 == 0x01 )
                {
                    printf("USA / EUR\n");
                }
                if ( usb_buffer_in[15] >> 4 == 0x03 )
                {
                    printf("Japan\n");
                }
                if ( usb_buffer_in[15] >> 4 == 0x03 )
                {
                    printf("Japan\n");
                }
                game_size = usb_buffer_in[15] & 0xF;
                //printf("Game Size : %ld Ko \n",game_size);
                if (game_size == 0x00)
                {
                    printf("Game Size : 256 Ko");
                    game_size = 256*1024;
                }
                if (game_size == 0x01)
                {
                    printf("Game Size : 512 Ko");
                    game_size = 512*1024;
                }
                if (game_size == 0x0c)
                {
                    printf("Game Size : 32 Ko");
                    game_size = 32*1024;
                }
                if (game_size == 0x0e)
                {
                    printf("Game Size : 64 Ko");
                    game_size = 64*1024;
                }
                if (game_size == 0x0f)
                {
                    printf("Game Size : 128 Ko");
                    game_size = 128*1024;
                }

            }

            else
            {
                printf(" \nUnknown cartridge type\n(erased flash eprom or bad connection,...)\n");
            }
    }

    printf("\n --- MENU ---\n");
    printf(" 1) Dump MD ROM\n");
    printf(" 2) Dump MD Save\n");
    printf(" 3) Write MD Save\n");
    printf(" 4) Erase MD Save\n");
    printf(" 5) Write MD Flash\n");
    printf(" 6) Erase MD Flash\n");
    printf(" 7) Master System Mode\n");
    printf(" 8) Flash Memory Detection \n");
    printf(" 9) Debug Mode \n");
    printf(" 10) Register Read \n");
    printf(" 12) Update Firmware \n");

    printf("\nYour choice: \n");
    scanf("%d", &choixMenu);

    switch(choixMenu)
    {

    case 1: // DUMP MD ROM
        choixMenu=0;
        //printf("La valeur de HardwareID est %d");
        if ( Hardwaretype ==0 )
        {
            printf(" 1) Auto (from header)\n");
            printf(" 2) Manual\n");
            printf(" Your choice: ");
            scanf("%d", &choixMenu);
            if(choixMenu==2)
            {
                printf(" Enter number of KB to dump: ");
                scanf("%d", &game_size);
            }

            printf("Sending command Dump ROM \n");
            printf("Dumping please wait ...\n");
            address=0;
            game_size *= 1024;
            printf("\nRom Size : %ld Ko \n",game_size/1024);
            BufferROM = (unsigned char*)malloc(game_size);
            // Cleaning ROM Buffer
            for (i=0; i<game_size; i++)
            {
                BufferROM[i]=0x00;
            }

            usb_buffer_out[0] = READ_MD;
            usb_buffer_out[1]=address & 0xFF;
            usb_buffer_out[2]=(address & 0xFF00)>>8;
            usb_buffer_out[3]=(address & 0xFF0000)>>16;
            usb_buffer_out[4]=1;

            libusb_bulk_transfer(handle, 0x01,usb_buffer_out, sizeof(usb_buffer_out), &numBytes, 60000);
            printf("ROM dump in progress...\n");
            res = libusb_bulk_transfer(handle, 0x82,BufferROM,game_size, &numBytes, 60000);
            if (res != 0)
            {
                printf("Error \n");
                return 1;
            }
            printf("\nDump ROM completed !\n");
            myfile = fopen("dump_smd.bin","wb");
            fwrite(BufferROM, 1,game_size, myfile);
            fclose(myfile);
        }
        if ( HardwareID != 0 && Hardwaretype == 2) // SSF2 Mapper + 5 MO ROM
        {
            game_size=Hardwaresize*1024*1024;
            BufferROM = (unsigned char*)malloc(game_size);
            printf("Rom Size : %ld Ko \n",game_size/1024);
            address=0;
            for(i=0; i<1024*512*2*5; i++)
            {
                BufferROM[i]=0x00;
            }

            // Dump the first 4MB of the ROM

            printf("Sending command Dump ROM \n");
            printf("Dumping please wait ...\n");
            address = 0;
            i=0;

            while ( i < 8192*2*4)
            {
                usb_buffer_out[0] = READ_MD;
                usb_buffer_out[1]=address & 0xFF;
                usb_buffer_out[2]=(address & 0xFF00)>>8;
                usb_buffer_out[3]=(address & 0xFF0000)>>16;
                usb_buffer_out[4]=0;

                libusb_bulk_transfer(handle, 0x01,usb_buffer_out, sizeof(usb_buffer_out), &numBytes, 60000);
                libusb_bulk_transfer(handle, 0x82,BufferROM+(i*64),64, &numBytes, 60000);
                address+=32;
                i++;
            }
            // Bankswitch Last MB

            // Send 0x08 to the bank 6

            address = 0xA130FD/2; // bank 6
            usb_buffer_out[0] = MAPPER_SSF2;
            usb_buffer_out[1]=address & 0xFF;
            usb_buffer_out[2]=(address & 0xFF00)>>8;
            usb_buffer_out[3]=(address & 0xFF0000)>>16;
            usb_buffer_out[4]=0;
            usb_buffer_out[5]=0x08;

            libusb_bulk_transfer(handle, 0x01,usb_buffer_out, sizeof(usb_buffer_out), &numBytes, 60000);

            // Send 0x09 to the bank 7

            address = 0xA130FF/2; // bank 7
            usb_buffer_out[0] = MAPPER_SSF2;
            usb_buffer_out[1]=address & 0xFF;
            usb_buffer_out[2]=(address & 0xFF00)>>8;
            usb_buffer_out[3]=(address & 0xFF0000)>>16;
            usb_buffer_out[4]=0;
            usb_buffer_out[5]=0x09;

            libusb_bulk_transfer(handle, 0x01,usb_buffer_out, sizeof(usb_buffer_out), &numBytes, 60000);

            address = (3072*1024)/2;

            // Dump last MB

            while ( i < 8192*2*5)
            {
                usb_buffer_out[0] = READ_MD;
                usb_buffer_out[1]=address & 0xFF;
                usb_buffer_out[2]=(address & 0xFF00)>>8;
                usb_buffer_out[3]=(address & 0xFF0000)>>16;
                usb_buffer_out[4]=0;

                libusb_bulk_transfer(handle, 0x01,usb_buffer_out, sizeof(usb_buffer_out), &numBytes, 60000);
                libusb_bulk_transfer(handle, 0x82,BufferROM+(i*64),64, &numBytes, 60000);
                address+=32;
                i++;
            }

            printf("\nDump ROM completed !\n");
            myfile = fopen("dump_smd.bin","wb");
            fwrite(BufferROM,1,game_size, myfile);
            fclose(myfile);
        }
        break;

    case 2: // DUMP MD Save
        choixMenu=0;
        printf(" 1) Auto (from header)\n");
        printf(" 2) Manual 64kb/8KB\n");
        printf(" 3) Manual 256kb/32KB\n");
        printf(" Your choice: ");
        scanf("%d", &choixMenu);

        if(choixMenu>3)
        {
            printf(" Wrong number!\n\n");
            return 0;
        }

        switch(choixMenu)
        {
        case 1:
            save_size *= 1024;
            break;
        case 2:
            save_size = 8192;
            break;
        case 3:
            save_size = 32768;
            break;
        default:
            save_size = 8192;
        }

        buffer_rom = (unsigned char*)malloc(save_size); // raw buffer
        buffer_save = (unsigned char*)malloc((save_size*2)); // raw in 16bit format

        for (i=0; i<(save_size*2); i++)
        {
            buffer_save[i]=0x00;
        }

        usb_buffer_out[0] = READ_MD_SAVE;
        address=(save_address/2);
        i=0;
        while ( i< save_size)
        {

            usb_buffer_out[1]=address & 0xFF;
            usb_buffer_out[2]=(address & 0xFF00)>>8;
            usb_buffer_out[3]=(address & 0xFF0000)>>16;
            usb_buffer_out[4]=0;
            libusb_bulk_transfer(handle, 0x01,usb_buffer_out, sizeof(usb_buffer_out), &numBytes, 60000);
            libusb_bulk_transfer(handle, 0x82,(buffer_rom+i),64, &numBytes, 60000);
            address +=64; //next adr
            i+=64;

            printf("\r SAVE dump in progress: %ld%%", ((100 * i)/save_size));
            fflush(stdout);
        }
        i=0;
        j=0;
        myfile = fopen("raw.srm","wb");
        fwrite(buffer_rom,1,save_size, myfile);

        for (i=0; i<save_size; i++)
        {
            j=j+1;
            buffer_save[i+j]=buffer_rom[i];
        }


        myfile = fopen("dump_smd.srm","wb");
        fwrite(buffer_save,1,save_size*2, myfile);
        fclose(myfile);
        break;

    case 3:  // WRITE SRAM

        printf(" ALL DATAS WILL BE ERASED BEFORE ANY WRITE!\n");
        printf(" Save file: ");
        scanf("%60s", dump_name);
        myfile = fopen(dump_name,"rb");
        fseek(myfile,0,SEEK_END);
        save_size = ftell(myfile);
        buffer_save = (unsigned char*)malloc(save_size);
        rewind(myfile);
        fread(buffer_save, 1, save_size, myfile);
        fclose(myfile);

        // Erase SRAM

        address=(save_address/2);
        usb_buffer_out[0] = WRITE_MD_SAVE; // Select write in 8bit Mode
        usb_buffer_out[1]=address & 0xFF;
        usb_buffer_out[2]=(address & 0xFF00)>>8;
        usb_buffer_out[3]=(address & 0xFF0000)>>16;
        usb_buffer_out[7] = 0xBB;  // SRAM Clean Flag
        libusb_bulk_transfer(handle, 0x01,usb_buffer_out, sizeof(usb_buffer_out), &numBytes, 60000);
        while ( usb_buffer_in[6] != 0xAA)
        {
            libusb_bulk_transfer(handle, 0x82, usb_buffer_in, sizeof(usb_buffer_in), &numBytes, 6000);
        }

        printf("SRAM Sucessfully Erased ...\n");

        // Write SRAM

        i=0;
        j=0;
        unsigned long k=1;
        // save_size=save_size/2; // 16 bit input to 8 bit out
        address=(save_address/2);
        while ( j < save_size)
        {

            // Fill buffer 8bit with save_data

            // Fill usb out buffer with save data in 8bit
            for (i=32; i<64; i++)
            {
                usb_buffer_out[i] = buffer_save[k];
                k=k+2;

            }
            i=0;
            j+=64;

            usb_buffer_out[0] = WRITE_MD_SAVE; // Select write in 8bit Mode
            usb_buffer_out[1]=address & 0xFF;
            usb_buffer_out[2]=(address & 0xFF00)>>8;
            usb_buffer_out[3]=(address & 0xFF0000)>>16;
            usb_buffer_out[7] = 0xCC;
            libusb_bulk_transfer(handle, 0x01,usb_buffer_out, sizeof(usb_buffer_out), &numBytes, 60000);
            while ( usb_buffer_in[6] != 0xAA)
            {
                libusb_bulk_transfer(handle, 0x82, usb_buffer_in, sizeof(usb_buffer_in), &numBytes, 6000);
            }
            address+=32;
        }

        printf("SRAM Sucessfully Writted ...\n");
        break;


    case 4:  // ERASE SRAM

        printf("ALL SRAM DATAS WILL BE ERASED ...\n");
        address=(save_address/2);
        usb_buffer_out[0] = WRITE_MD_SAVE; // Select write in 8bit Mode
        usb_buffer_out[1]=address & 0xFF;
        usb_buffer_out[2]=(address & 0xFF00)>>8;
        usb_buffer_out[3]=(address & 0xFF0000)>>16;
        usb_buffer_out[7] = 0xBB;  // SRAM Clean Flag
        libusb_bulk_transfer(handle, 0x01,usb_buffer_out, sizeof(usb_buffer_out), &numBytes, 60000);
        while ( usb_buffer_in[6] != 0xAA)
        {
            libusb_bulk_transfer(handle, 0x82, usb_buffer_in, sizeof(usb_buffer_in), &numBytes, 6000);
        }

        printf("SRAM Sucessfully Erased ...\n");
        break;


    case 5: // Write MD Flash

        printf(" ALL DATAS WILL BE ERASED BEFORE ANY WRITE!\n");
        printf(" ROM file: ");
        scanf("%60s", dump_name);
        myfile = fopen(dump_name,"rb");
        fseek(myfile,0,SEEK_END);
        game_size = ftell(myfile);
        buffer_rom = (unsigned char*)malloc(game_size);
        rewind(myfile);
        fread(buffer_rom, 1, game_size, myfile);
        fclose(myfile);
        i=0;
        address = 0;

        // First Erase Flash Memory

        usb_buffer_out[0] = ERASE_MD_FLASH;
        libusb_bulk_transfer(handle, 0x01,usb_buffer_out, sizeof(usb_buffer_out), &numBytes, 60000);
        i=0;
        while(usb_buffer_in[0]!=0xFF)
        {
            libusb_bulk_transfer(handle, 0x82, usb_buffer_in, sizeof(usb_buffer_in), &numBytes, 6000);   //wait status
        }

        printf("\r ERASE SMD flash completed\n");
        i=0;
        address = 0;

        while(i<game_size)
        {

            usb_buffer_out[0] = WRITE_MD_FLASH; // Select write in 16bit Mode
            usb_buffer_out[1] = address & 0xFF;
            usb_buffer_out[2] = (address & 0xFF00)>>8;
            usb_buffer_out[3] = (address & 0xFF0000)>>16;

            if((game_size - i)<54)
            {
                usb_buffer_out[4] = (game_size - i); //adjust last packet
            }
            else
            {
                usb_buffer_out[4] = 54; //max 58 bytes - must by pair (word)
            }

            memcpy((unsigned char *)usb_buffer_out +5, (unsigned char *)buffer_rom +i, usb_buffer_out[4]);

            libusb_bulk_transfer(handle, 0x01,usb_buffer_out, sizeof(usb_buffer_out), &numBytes, 60000);
            i += usb_buffer_out[4];
            address += (usb_buffer_out[4]>>1);
            printf("\r WRITE SMD flash in progress: %ld%%", ((100 * i)/game_size));
            fflush(stdout);
        }

        printf("\r SMD flash completed\n");
        free(buffer_save);
        break;

    case 6: //ERASE FLASH

        buffer_rom = (unsigned char*)malloc(64);
        usb_buffer_out[0] = ERASE_MD_FLASH;
        libusb_bulk_transfer(handle, 0x01,usb_buffer_out, sizeof(usb_buffer_out), &numBytes, 60000);
        i=0;
        while(usb_buffer_in[0]!=0xFF)
        {
            libusb_bulk_transfer(handle, 0x82, usb_buffer_in, sizeof(usb_buffer_in), &numBytes, 6000);   //wait status
            printf("\r ERASE SMD flash in progress: %s ", wheel[i]);
            fflush(stdout);
            i++;
            if(i==4)
            {
                i=0;
            }
        }

        printf("\r ERASE SMD flash in progress: 100%%");
        fflush(stdout);

        break;

    case 7: // Master System Mode

        choixMenu=0;
        printf(" 1) Auto (from header)\n");
        printf(" 2) Manual\n");
        printf(" Your choice: ");
        scanf("%d", &choixMenu);
        if(choixMenu==2)
        {
            printf(" Enter number of KB to dump: ");
            scanf("%d", &game_size);
            game_size *= 1024;
        }
        printf("Sending command Dump ROM \n");
        printf("Dumping please wait ...\n");
        printf("\nRom Size : %ld Ko \n",game_size/1024);
        buffer_rom = (unsigned char*)malloc(game_size); // raw buffer
        // Cleaning ROM Buffer
        for (i=0; i<game_size; i++)
        {
            buffer_rom[i]=0x00;
        }

        address = 0;
        i=0;
        j=0;
        sms_bank=0;
        while ( i< game_size)
        {
            usb_buffer_out[0] = READ_SMS;
            usb_buffer_out[1] = address&0xFF ;
            usb_buffer_out[2] = (address&0xFF00)>>8;
            usb_buffer_out[3]=(address & 0xFF0000)>>16;
            usb_buffer_out[4] = 0; // Slow Mode
            usb_buffer_out[5] = sms_bank; // Bank
            libusb_bulk_transfer(handle, 0x01,usb_buffer_out, sizeof(usb_buffer_out), &numBytes, 60000);
            libusb_bulk_transfer(handle, 0x82,(buffer_rom+i),64, &numBytes, 60000);
            address +=64; //next adr
            i+=64;
            j+=64;
            printf("\r ROM dump in progress: %ld%%", ((100 * i)/game_size));
            fflush(stdout);
        }

        myfile = fopen("dump_sms.sms","wb");
        fwrite(buffer_rom,1,game_size, myfile);
        fclose(myfile);
        break;

    case 8: // Vendor / ID Info


        printf("Detecting Flash...\n");
        usb_buffer_out[0] = INFOS_ID;
        libusb_bulk_transfer(handle, 0x01,usb_buffer_out, sizeof(usb_buffer_out), &numBytes, 60000);
        libusb_bulk_transfer(handle, 0x82, usb_buffer_in, sizeof(usb_buffer_in), &numBytes, 6000);

        manufacturer_id = usb_buffer_in[1];
        chip_id = usb_buffer_in[3];

        printf("Manufacturer ID : %02X \n",usb_buffer_in[1]);
        printf("Chip ID : %02X \n",usb_buffer_in[3]);

        scanf("%d");

        break;

    case 9: // Debug Mode
        CurrentAddress = 0;
        CurrentData =0;

        while (1)
        {
            system("@cls||clear");
            printf(" ---------------------------------\n");
            printf("      Sega Dumper Debug Mode      \n");
            printf(" ---------------------------------\n");

            printf("\n --- Address BUS ---\n");
            printf(" Value of the Address BUS : %ld \n",DebugAddress);
            printf("\n --- Data BUS ---\n");
            printf(" Value of the Data BUS : %ld \n",CurrentData);
            printf("\n --- Control Lines ---\n");
            printf(" [T] /Time : ' %d ' \n",Debug_Time);
            printf(" [A] /ASEL : ' %d ' \n",Debug_Asel);
            printf(" [L] /LWR  : ' %d ' \n",Debug_LWR);
            printf(" [S] Set Data Bus   ");
            printf(" [V] Value of Address Bus \n");
            //printf(" [T] /Time : '1' [A] /Asel : '1' [L] /LWR : '1' \n");


            printf("\n\n --- ROM Viewer ---\n");
            // Cleaning header Buffer
            for (i=0; i<512; i++)
            {
                buffer_header[i]=0x00;
            }
            j=0;
            i = 0;
            CurrentAddress = CurrentAddress/2;

            while (i<8)
            {

                usb_buffer_out[0] = READ_MD;
                usb_buffer_out[1] = CurrentAddress & 0xFF ;
                usb_buffer_out[2] = (CurrentAddress & 0xFF00)>>8;
                usb_buffer_out[3]=(CurrentAddress & 0xFF0000)>>16;
                usb_buffer_out[4] = 0; // Slow Mode

                libusb_bulk_transfer(handle, 0x01,usb_buffer_out, sizeof(usb_buffer_out), &numBytes, 60000);
                libusb_bulk_transfer(handle, 0x82,buffer_header+(64*i),64, &numBytes, 60000);
                CurrentAddress +=32;
                i++;
            }
            for(i=0; i<(256/16); i++)
            {
                printf("\n");
                for(j=0; j<16; j++)
                {
                    printf(" %02X", buffer_header[j+(i*16)]);
                }
                printf(" %.*s", 16, buffer_header +(i*16));
            }


            printf("\n\n --- Debug ---\n");
            printf("Enter Debug Command :\n");
            scanf("%c",&DebugCommand);
            switch(DebugCommand[0])
            {


            case 'v':
                printf("Enter decimal value of Address Bus :\n");
                scanf("%ld",&DebugAddress);
                CurrentAddress = DebugAddress*2 -512; // Word Read
                break;

            case 's':
                printf("Set decimal value of Data Bus :\n");
                scanf("%ld",&CurrentData);
                break;

            case 'l':
                printf("Set value of /LWR Pin :\n");
                scanf("%d",&Debug_LWR );
                usb_buffer_out[0] = DEBUG_MODE;
                usb_buffer_out[4] = Debug_LWR;
                libusb_bulk_transfer(handle, 0x01,usb_buffer_out, sizeof(usb_buffer_out), &numBytes, 0);
                break;

            case 'a':
                printf("Set value of /ASEL Pin :\n");
                scanf("%d",&Debug_Asel );
                usb_buffer_out[0] = DEBUG_MODE;
                usb_buffer_out[2] = Debug_Asel;
                libusb_bulk_transfer(handle, 0x01,usb_buffer_out, sizeof(usb_buffer_out), &numBytes, 0);

                break;

            case 't':
                printf("Set value of /Time Pin :\n");
                scanf("%d",&Debug_Time );
                usb_buffer_out[0] = DEBUG_MODE;
                usb_buffer_out[3] = Debug_Time;
                libusb_bulk_transfer(handle, 0x01,usb_buffer_out, sizeof(usb_buffer_out), &numBytes, 0);
                break;


            }
        }

    case 10: // Register Read


// Clean Buffer
        for (i = 0; i < 64; i++)
        {
            usb_buffer_in[i]=0x00;
            usb_buffer_out[i]=0x00;
        }

        choixMenu=0;
        printf("Special Register Read Mode...\n");
        /*printf("Please select register number : \n");
        scanf("%d", &choixMenu);
        usb_buffer_out[0] = 0xFA;
        usb_buffer_out[1]= choixMenu;*/
        usb_buffer_out[0] = 0xFA;
        libusb_bulk_transfer(handle, 0x01,usb_buffer_out, sizeof(usb_buffer_out), &numBytes, 0);
        libusb_bulk_transfer(handle, 0x82, usb_buffer_in, sizeof(usb_buffer_in), &numBytes, 0);
        printf("CPLD register value is 0x%02X\n",usb_buffer_in[1]);
        /*printf("\nDisplaying USB IN buffer\n\n");
        for (i = 0; i < 64; i++)
        {
            printf("%02X ",usb_buffer_in[i]);
        	j++;
        	if (j==16){printf("\n");j=0;}
        }  */

        break;

    case 11: // SSF2 Test

        choixMenu=0;
        game_size=2*1024*1024;
        BufferROM = (unsigned char*)malloc(game_size);

        address = 0xA130F5/2; // bank 2
        usb_buffer_out[0] = MAPPER_SSF2;
        usb_buffer_out[1]=address & 0xFF;
        usb_buffer_out[2]=(address & 0xFF00)>>8;
        usb_buffer_out[3]=(address & 0xFF0000)>>16;
        usb_buffer_out[4]=0;
        usb_buffer_out[5]=0x02;

        libusb_bulk_transfer(handle, 0x01,usb_buffer_out, sizeof(usb_buffer_out), &numBytes, 60000);

        address = 0;
        i=0;

        while ( i < 8192*2*2)
        {
            usb_buffer_out[0] = READ_MD;
            usb_buffer_out[1]=address & 0xFF;
            usb_buffer_out[2]=(address & 0xFF00)>>8;
            usb_buffer_out[3]=(address & 0xFF0000)>>16;
            usb_buffer_out[4]=0;

            libusb_bulk_transfer(handle, 0x01,usb_buffer_out, sizeof(usb_buffer_out), &numBytes, 60000);
            libusb_bulk_transfer(handle, 0x82,BufferROM+(i*64),64, &numBytes, 60000);
            address+=32;
            i++;
        }
        printf("\nDump ROM completed !\n");
        myfile = fopen("dump_ssf2.bin","wb");
        fwrite(BufferROM, 1,game_size, myfile);
        fclose(myfile);
        break;

   case 12: // Update Firmware

		printf("---=Sega Dumper Update mode =---\n\n");
		printf("Dumping current firmware ...\n");
		game_size=128*1024;
		BufferROM = (unsigned char*)malloc(game_size);

	// Clean Buffer
    for (i = 0; i < 64; i++)
    {
        usb_buffer_in[i]=0x00;
        usb_buffer_out[i]=0x00;
    }
		//address = 0x2000; // STM32 App address is after bootloader
		address = 0x0000;
        i=0;

        while ( i < game_size)
        {
            usb_buffer_out[0] = UPDATE_READ;
            usb_buffer_out[1]=address & 0xFF;
            usb_buffer_out[2]=(address & 0xFF00)>>8;
            usb_buffer_out[3]=(address & 0xFF0000)>>16;
            usb_buffer_out[4]=0;

            libusb_bulk_transfer(handle, 0x01,usb_buffer_out, sizeof(usb_buffer_out), &numBytes, 60000);
            libusb_bulk_transfer(handle, 0x82,BufferROM+i,64, &numBytes, 60000);
            address=address+64;
            i=i+64;
        }
       
		printf("Current Firmware readed sucessfully !\n");

		printf("Try to unlock STM32 Flash ...\n");
		usb_buffer_out[0] = UPDATE_WRITE;
		usb_buffer_out[1] = 0xCC;  // unlock flash flag
		libusb_bulk_transfer(handle, 0x01,usb_buffer_out, sizeof(usb_buffer_out), &numBytes, 60000);
        libusb_bulk_transfer(handle, 0x82,usb_buffer_in, sizeof(usb_buffer_in), &numBytes, 60000);
		if ( usb_buffer_in[2] == 0xAA) { printf("STM32 Flash unlocked sucessfully ! \n");}
		else { printf("Unlock flash error ! \n");break;}

		printf("Erase STM32 Flash page ...\n");
		usb_buffer_out[0] = UPDATE_WRITE;
		usb_buffer_out[1] = 0xCC;  // unlock flash flag
		usb_buffer_out[2] = 0x01;  // Erase page 9
		libusb_bulk_transfer(handle, 0x01,usb_buffer_out, sizeof(usb_buffer_out), &numBytes, 60000);
        libusb_bulk_transfer(handle, 0x82,usb_buffer_in, sizeof(usb_buffer_in), &numBytes, 60000);
		i=0;
		j=0;

      /*  printf("\nDisplaying USB IN buffer\n\n");
        for (i = 0; i < 64; i++)
        {
            printf("%02X ",usb_buffer_in[i]);
        	j++;
        	if (j==16){printf("\n");j=0;}
        }  
      */

        myfile = fopen("dump_fw.bin","wb");
        fwrite(BufferROM, 1,game_size, myfile);
        fclose(myfile);

		break;



    }

    return 0;

}





