/*
 * Bootloader.c
 *
 *  Created on: Sep 7, 2024
 *      Author: ahmed
 */

/*********************************** Includes *********************************/
#include "Bootloader.h"
/******************************************************************************/

/*********************************** Defines **********************************/
#define bootloader_spi                  (&hspi1)
#define bootloader_crc                  (&hcrc)

#define BOOT_FLAG_ADD                   (0x0803FFFFU)
#define VERSION_BASE_ADD                (0x0803FFFCU)
#define MAJOR_ADD                       (VERSION_BASE_ADD)
#define MINOR_ADD                       (VERSION_BASE_ADD + 1)
#define PATCH_ADD                       (VERSION_BASE_ADD + 2)

#define COMMAND_LENGTH_INDEX            (0)
#define COMMAND_TYPE_INDEX              (1)

#define ERASE_SUCCESS                   (0xFFFFFFFFU)
#define NUMBER_FLASH_SECTORS            (6)

#define ALLOWED_PROGRAM_START_ADD       (0x0800C000U)
#define ALLOWED_PROGRAM_END_ADD         (0x0803FFFFU - 8)

#define LAST_FLASHED_PROGRAM_ADD        (0x0803FFF8U)
#define JUMP_MAIN_APP_COMMAND           (0xFFFFFFFFU)

#define PROGRAM_NOT_FOUND_FLAG          (0xFFFFFFFFU)
/******************************************************************************/

/*********************************** Macro functions **************************/
#define GET_4BYTES(buffer, Start)      ((buffer[Start + 3])            |\
                                        (buffer[Start + 2] << (1*8))   |\
                                        (buffer[Start + 1] << (2*8))   |\
                                        (buffer[Start    ] << (3*8))   )     
/******************************************************************************/

/*********************************** Static Function declaration **************/
static BOOT_status_t bl_check_boot_need(void);
static BL_status_t bootloader_command_execute(uint8_t *buffer,
                                              uint8_t length,
                                              uint8_t command);
static CRC_check_t bl_crc_check(uint8_t *buffer, uint8_t length);
static BL_status_t Send_ACK(void);
static BL_status_t Send_NACK(void);
static BL_status_t bl_get_version(uint8_t *buffer, uint8_t length);
static BL_status_t bl_jump_to_address(uint8_t *buffer, uint8_t length);
static BL_status_t jump_main_app(BOOT_status_t next_boot);
static BL_status_t jump_add(uint32_t add);
static BL_status_t bl_deinit(void);
static BL_status_t bl_erase_sectors(uint8_t *buffer, uint8_t length);
static BL_status_t bl_write_program(uint8_t *buffer, uint8_t length);
static BL_status_t mass_erase_execute();
static BL_status_t sector_erase_execute(erase_sectors_t start, uint8_t num);
static BL_status_t write_program(uint32_t add, uint32_t size);
static BL_status_t write_version(uint8_t major, uint8_t minor, uint8_t patch);
static void jump_main_app_without_boot_edit(void);
/******************************************************************************/

/*********************************** Global Objects ***************************/
static uint8_t BL_buffer[BOOTLOADER_BUFFER_SIZE];
static uint8_t BL_Buffer_send[BOOTLOADER_BUFFER_SIZE];
static uint8_t BL_Buffer_temp[BOOTLOADER_BUFFER_SIZE];
/******************************************************************************/

/*********************************** Function definition **********************/
void bootloader_app(void)
{
    BOOT_status_t boot_status = BOOT_NEEDED;
    HAL_StatusTypeDef hal_status = HAL_ERROR;
    CRC_check_t crc_status = CRC_NOT_OK;
    BL_status_t bl_status = BL_OK;
    uint8_t length = 0;
    uint8_t command = 0;
    /* Check the need of bootloader or not */
    boot_status = bl_check_boot_need();

    /* If so */
    if (BOOT_NEEDED == boot_status)
    {
#if (BOOTLOADER_DEBUG_PROTOCOL != BOOTLOADER_STOP)
        printf("Bootloader Started !!\n");
#endif
        /* Receive the Command from the host */
        do
        {
            /* Reset the buffers to get ready for new command */
            memset(BL_buffer,      0x00, BOOTLOADER_BUFFER_SIZE);
            memset(BL_Buffer_send, 0x00, BOOTLOADER_BUFFER_SIZE);
            memset(BL_Buffer_temp, 0x00, BOOTLOADER_BUFFER_SIZE);
            HAL_Delay(10);
            hal_status = HAL_SPI_TransmitReceive(bootloader_spi,
                                                 BL_Buffer_send,
                                                 BL_buffer,
                                                 BOOTLOADER_BUFFER_SIZE,
                                                 HAL_MAX_DELAY);
            if (0 == memcmp(BL_buffer, BL_Buffer_temp, BOOTLOADER_BUFFER_SIZE))
            {
                HAL_Delay(15);
                continue;
            }
            if (HAL_OK != hal_status)
            {

#if (BOOTLOADER_DEBUG_PROTOCOL != BOOTLOADER_STOP)
                printf("HAL_SPI_Receive ===> HAL_ERROR !!\n");
#endif
                bl_status = BL_ERROR;
            }
            else
            {
                /* Process the Full packet received */
                length = BL_buffer[COMMAND_LENGTH_INDEX];
                command = BL_buffer[COMMAND_TYPE_INDEX];
                if (0 != length)
                {
                    /* CRC check */
                    crc_status = bl_crc_check(BL_buffer, length);
                    if (crc_status != CRC_OK)
                    {
                        /* Send NOT ACK in case of CRC error*/
                        bl_status = Send_NACK();
#if (BOOTLOADER_DEBUG_PROTOCOL != BOOTLOADER_STOP)
                        printf("CRC ===> ERROR !!\n");
#endif
                    }
                    else
                    {
                        /* Send NOT ACK in case of CRC OK*/
                        bl_status = Send_ACK();
                        /* check for command execution status */
                        if (BL_OK != bl_status)
                        {
#if (BOOTLOADER_DEBUG_PROTOCOL != BOOTLOADER_STOP)
                            printf("Bootloader ===> ERROR !!\n");
#endif
                        }
                        else
                        {
                            /* Call the command execution function */
                            bl_status = bootloader_command_execute(BL_buffer,
                                                                   length,
                                                                   command);
                        }
                    }
                }
            }
        } while (0 == BL_buffer[COMMAND_LENGTH_INDEX]);
    }
    /* If not needed*/
    else
    {
        /* Jump to main function */
        jump_main_app_without_boot_edit();
    }
}

void bootloader_init(void)
{
    MX_GPIO_Init();
    MX_CRC_Init();
    MX_USB_DEVICE_Init();
    MX_SPI1_Init();
}
/******************************************************************************/

/*********************************** Static Function definition ***************/

static BOOT_status_t bl_check_boot_need(void)
{
    BOOT_status_t boot_status = 0;
    /* Read the boot flag from flash memory */
    volatile uint8_t *boot_flag = (volatile uint8_t *)(BOOT_FLAG_ADD);
    boot_status = (BOOT_status_t)*boot_flag;
    /* return the flag */
    return boot_status;
}

static BL_status_t bootloader_command_execute(uint8_t *buffer,
                                              uint8_t length,
                                              uint8_t command)
{
    BL_status_t BL_status = BL_OK;
    switch (command)
    {
    /* If the host wants to get the version of the main program version */
    case BL_GET_VERSION:
#if (BOOTLOADER_DEBUG_PROTOCOL != BOOTLOADER_STOP)
        printf("Command received : BL_GET_VERSION !!\n");
#endif
        /* Call the execute function of this command */
        BL_status = bl_get_version(buffer, length);
        break;

    /* If the host wants to erase some sectors in the flash memory */
    case BL_ERASE_SECTORS:
#if (BOOTLOADER_DEBUG_PROTOCOL != BOOTLOADER_STOP)
        printf("Command received : BL_ERASE_SECTORS !!\n");
#endif
        /* Call the execute function of this command */
        BL_status = bl_erase_sectors(buffer, length);
        break;

    /* If the host wants to write a program to flash memory */
    case BL_WRITE_PROGRAM:
#if (BOOTLOADER_DEBUG_PROTOCOL != BOOTLOADER_STOP)
        printf("Command received : BL_WRITE_PROGRAM !!\n");
#endif
        /* Call the execute function of this command */
        BL_status = bl_write_program(buffer, length);
        break;

    /* If the host wants to jump to specific address */
    case BL_JUMP_TO_ADDRESS:
#if (BOOTLOADER_DEBUG_PROTOCOL != BOOTLOADER_STOP)
        printf("Command received : BL_JUMP_TO_ADDRESS !!\n");
#endif
        /* Call the execute function of this command */
        BL_status = bl_jump_to_address(buffer, length);
        break;

    default:
        BL_status = BL_ERROR;
        break;
    }
    return BL_status;
}

static BL_status_t Send_ACK(void)
{
    uint32_t waited_cycles = 0;
    BL_status_t bl_status = BL_OK;
    HAL_StatusTypeDef status = HAL_ERROR;
    do
    {
        /* Reset the buffer to get ready for ACK signal */
        memset(BL_Buffer_send, 0x00, BOOTLOADER_BUFFER_SIZE);
        memset(BL_Buffer_temp, 0x00, BOOTLOADER_BUFFER_SIZE);
        HAL_Delay(10);
        BL_Buffer_send[0] = (uint8_t)ACK_SIGNAL;
        status = HAL_SPI_TransmitReceive(bootloader_spi,
                                         BL_Buffer_send,
                                         BL_Buffer_temp,
                                         BOOTLOADER_BUFFER_SIZE,
                                         HAL_MAX_DELAY);
        HAL_Delay(10);
        waited_cycles++;
        if(waited_cycles >= 2000)
        {
            bl_status = BL_ERROR;
            break;
        }
    } while ((status != HAL_OK) || (BL_Buffer_temp[0] != WAIT_FOR_ACK_SIGNAL));
    return bl_status;
}

static BL_status_t Send_NACK(void)
{
    uint32_t waited_cycles = 0;
    BL_status_t bl_status = BL_OK;
    HAL_StatusTypeDef status = HAL_ERROR;
    do
    {
        /* Reset the buffer to get ready for ACK signal */
        memset(BL_Buffer_temp, 0x00, BOOTLOADER_BUFFER_SIZE);
        memset(BL_Buffer_send, 0x00, BOOTLOADER_BUFFER_SIZE);
        HAL_Delay(10);
        BL_Buffer_send[0] = (uint8_t)NACK_SIGNAL;
        status = HAL_SPI_TransmitReceive(bootloader_spi,
                                         BL_Buffer_send,
                                         BL_Buffer_temp,
                                         BOOTLOADER_BUFFER_SIZE,
                                         HAL_MAX_DELAY);
        HAL_Delay(10);
        if(waited_cycles >= 2000)
        {
            bl_status = BL_ERROR;
            break;
        }
    } while ((status != HAL_OK) || (BL_Buffer_temp[0] != WAIT_FOR_ACK_SIGNAL));
    return bl_status;
}

static CRC_check_t bl_crc_check(uint8_t *buffer, uint8_t length)
{
    CRC_check_t status = CRC_NOT_OK;
    uint32_t crc_val = 0;
    uint32_t counter = 0;
    uint32_t data_buffer = 0;
    /* Get the host CRC from the buffer */
    uint32_t host_crc_val = 0;
    host_crc_val |= GET_4BYTES(buffer, length - 3);

    /* Reset CRC Unit */
    __HAL_CRC_DR_RESET(bootloader_crc);

    /* Calculate local CRC */
    for (counter = 0; counter <= length - 4; counter++)
    {
        data_buffer = 0;
        data_buffer |= (uint32_t)(buffer[counter]);
        //printf("data_buffer: %lx\n", data_buffer);
        crc_val = HAL_CRC_Accumulate(bootloader_crc, &data_buffer, 1);
    }

    /* Check if they are typical or not */
    if (host_crc_val == crc_val)
    {
        status = CRC_OK;
    }
#if (BOOTLOADER_DEBUG_PROTOCOL != BOOTLOADER_STOP)
    printf("crc---->%lX\n", crc_val);
    printf("hcrc---->%lX\n", host_crc_val);
#endif
    return status;
}

static BL_status_t bl_get_version(uint8_t *buffer, uint8_t length)
{
    UNUSED(buffer);
    UNUSED(length);
    BL_status_t status = BL_ERROR;
    HAL_StatusTypeDef hal_status = HAL_ERROR;
    uint8_t spi_send_fail = 0;
    /* Read the addresses which store the version in flash memory */
    uint8_t major = *(uint8_t *)MAJOR_ADD;
    HAL_Delay(2);
    uint8_t minor = *(uint8_t *)MINOR_ADD;
    HAL_Delay(2);
    uint8_t patch = *(uint8_t *)PATCH_ADD;
    HAL_Delay(2);
    /* Update the buffer to get ready for sending it */
    memset(BL_Buffer_send, 0x00, BOOTLOADER_BUFFER_SIZE);
    BL_Buffer_send[0] = major;
    BL_Buffer_send[1] = minor;
    BL_Buffer_send[2] = patch;
    /* Send the buffer including the version to the host */
    do
    {
        hal_status = HAL_SPI_TransmitReceive(bootloader_spi, BL_Buffer_send,
                                             BL_Buffer_temp,
                                             BOOTLOADER_BUFFER_SIZE,
                                             HAL_MAX_DELAY);
        spi_send_fail++;
        if (spi_send_fail >= 100)
            break;
        HAL_Delay(50);
    } while ((HAL_OK != hal_status) || (BL_Buffer_temp[0] != REPEATED_SIGNAL));
    /* check if the spi sends the data */
    if ((HAL_OK == hal_status) && (spi_send_fail < 100))
    {
#if (BOOTLOADER_DEBUG_PROTOCOL != BOOTLOADER_STOP)
        printf("Version Sent Successfully!!\n");
#endif
        status = BL_OK;
    }
    else
    {
#if (BOOTLOADER_DEBUG_PROTOCOL != BOOTLOADER_STOP)
        printf("Error in Sending Version!!\n");
#endif
        status = BL_ERROR;
    }
    /* Return the status of the get version execution function */
    return status;
}

static BL_status_t bl_jump_to_address(uint8_t *buffer, uint8_t length)
{
    UNUSED(length);
    BL_status_t status = BL_ERROR;
    /* Get the Wanted Address from the buffer */
    uint32_t program_add  = (uint32_t)GET_4BYTES(buffer, 2);
    /* Get the next time boot status */
    BOOT_status_t next_boot = (BOOT_status_t)buffer[6];
    /* Validate the Program add */
    if (JUMP_MAIN_APP_COMMAND == program_add)
        status = jump_main_app(next_boot);
    else
        status = jump_add(program_add);
    return status;
}

static void jump_main_app_without_boot_edit(void)
{
    /* Get the address of main application */
    uint32_t *addPtr = (uint32_t *)(LAST_FLASHED_PROGRAM_ADD);
    uint32_t *program_ptr_check = (uint32_t *)(*addPtr);
    appPtr main_app = (appPtr)(*(program_ptr_check + 1));
    bl_deinit();
    /* Set the MSP */
    __set_MSP(program_ptr_check[0]);
    /* Jump to Main Application */
    main_app();
}

static BL_status_t jump_main_app(BOOT_status_t next_boot)
{
    BL_status_t status = BL_ERROR;
    HAL_StatusTypeDef hal_status = HAL_ERROR;
    /* Get the address of main application */
    uint32_t *addPtr = (uint32_t *)(LAST_FLASHED_PROGRAM_ADD);
    uint32_t *program_ptr_check = (uint32_t *)(*addPtr);
    appPtr main_app = (appPtr)(*(program_ptr_check + 1));
    /* Check for Program found */
    if (addPtr[0] != PROGRAM_NOT_FOUND_FLAG)
    {
        if ((next_boot == BOOT_NEEDED) || (next_boot == BOOT_NOT_NEEDED))
        {
            /* Unlock the Flash memory */
            hal_status = HAL_FLASH_Unlock();
            if (HAL_OK != hal_status)
            {
                status = BL_ERROR;
            }
            else
            {
                /* Boot Update status for next boot */
                status = HAL_FLASH_Program(FLASH_TYPEPROGRAM_BYTE,
                                           BOOT_FLAG_ADD,
                                           next_boot);
                /* Check for Erasing success */
                if (HAL_OK == hal_status)
                    status = BL_OK;
                else
                    status = BL_ERROR;
#if (BOOTLOADER_DEBUG_PROTOCOL != BOOTLOADER_STOP)
                printf("next boot DONE ...\n");
#endif

                /* Lock the Flash memory */
                do
                {
                    hal_status = HAL_FLASH_Lock();
                } while (HAL_OK != hal_status);
            }   
        }
#if (BOOTLOADER_DEBUG_PROTOCOL != BOOTLOADER_STOP)
        printf("Jumping to Main Application ...\n");
#endif
        /* BootLoader DeInit */
        status = bl_deinit();
        if (status != BL_OK)
        {
#if (BOOTLOADER_DEBUG_PROTOCOL != BOOTLOADER_STOP)
            printf("ERROR in Deinitialization of the BootLoader !!\n");
#endif
        }
        else
        {
            /* Set the MSP */
            __set_MSP(program_ptr_check[0]);
            /* Jump to Main Application */
            main_app();
        }
    }
    else
    {
#if (BOOTLOADER_DEBUG_PROTOCOL != BOOTLOADER_STOP)
        printf("Jumping to Main Application failed due to no Program Found\n");
#endif
        status = BL_ERROR;  
    }
    return status;
}

static BL_status_t bl_deinit(void)
{
    BL_status_t status = BL_ERROR;
    HAL_StatusTypeDef hal_status = HAL_OK;
    /* Deinitialization of BootLoader Used Modules */
    HAL_SPI_MspDeInit(bootloader_spi);
    HAL_CRC_MspDeInit(bootloader_crc);
    HAL_GPIO_DeInit(GPIOA, GPIO_PIN_4);
    hal_status |= HAL_RCC_DeInit();
    hal_status |= HAL_DeInit();
    /* Check for Success of Deinitialization */
    if (hal_status == HAL_OK)
        status = BL_OK;
    return status;
}

static BL_status_t jump_add(uint32_t add)
{
    BL_status_t status = BL_ERROR;
    /*  Validate the address wanted to jump to */
    if ((LAST_FLASHED_PROGRAM_ADD <= add) && (ALLOWED_PROGRAM_END_ADD >= add))
    {
        /* Getting ready the address */
        uint32_t *addPtr = (uint32_t *)(add);
        appPtr application = (appPtr)(addPtr);
        /* Validate what the address contain to no fail the program */
        if (PROGRAM_NOT_FOUND_FLAG != *addPtr)
        {
            status = BL_OK;
#if (BOOTLOADER_DEBUG_PROTOCOL != BOOTLOADER_STOP)
            printf("Jumping to Address 0x%lX\n", add);
#endif
            /* Jump to address */
            application();
        }
    }
    return status;
}

static BL_status_t bl_erase_sectors(uint8_t *buffer, uint8_t length)
{
    UNUSED(length);
    BL_status_t status = BL_ERROR;
    /* Get the Start of erasing and length of erasing */
    erase_sectors_t start = (erase_sectors_t)buffer[2];
    uint8_t num_sectors = buffer[3];
    if (MASS_ERASE == start)
    {
#if (BOOTLOADER_DEBUG_PROTOCOL != BOOTLOADER_STOP)
        printf("WARNING: Mass Erase Wanted!!\n");
#endif
        status = mass_erase_execute();
    }
    else
    {
#if (BOOTLOADER_DEBUG_PROTOCOL != BOOTLOADER_STOP)
        printf("Start Erasing %d Sectors Starting from Sector %d !!\n",
               num_sectors, start);
#endif
        status = sector_erase_execute(start, num_sectors);
    }

#if (BOOTLOADER_DEBUG_PROTOCOL != BOOTLOADER_STOP)
    if (BL_OK != status)
    {
        printf("ERROR: Erasing Flash Memory !!\n");
    }
#endif

    return status;
}

static BL_status_t mass_erase_execute()
{
    BL_status_t status = BL_ERROR;
    HAL_StatusTypeDef hal_status = HAL_ERROR;
    /* Determine the Configuration of the Erase */
    FLASH_EraseInitTypeDef mass_erase_configurations =
        {
            .TypeErase = FLASH_TYPEERASE_MASSERASE, // Mass Erase Operation
            .NbSectors = 7,                         // Number of Flash memory Sectors
            .Banks = FLASH_BANK_1,                  // Bank 1
            .Sector = FLASH_SECTOR_0,               // Start Sector
            .VoltageRange = FLASH_VOLTAGE_RANGE_3,  // Device operating range: 2.7V to 3.6V
        };
    uint32_t erase_status = 0; // Store the status of Erasing operation
    /* Unlock the Flash memory */
    hal_status = HAL_FLASH_Unlock();
    if (HAL_OK != hal_status)
    {
        status = BL_ERROR;
    }
    else
    {
        /* Perfome Erasing */
        hal_status = HAL_FLASHEx_Erase(&mass_erase_configurations, &erase_status);

        /* Check for Erasing success */
        if ((HAL_OK == hal_status) && (ERASE_SUCCESS == erase_status))
            status = BL_OK;
        else
            status = BL_ERROR;

        /* Lock the Flash memory */
        do
        {
            hal_status = HAL_FLASH_Lock();
        } while (HAL_OK != hal_status);
    }
    return status;
}

static BL_status_t sector_erase_execute(erase_sectors_t start, uint8_t num)
{
    BL_status_t status = BL_ERROR;
    HAL_StatusTypeDef hal_status = HAL_ERROR;
    /* Determine the Configuration of the Erase */
    FLASH_EraseInitTypeDef erase_configurations =
        {
            .TypeErase = FLASH_TYPEERASE_SECTORS,  // Mass Erase Operation
            .Banks = FLASH_BANK_1,                 // Bank 1
            .VoltageRange = FLASH_VOLTAGE_RANGE_3, // Device operating range: 2.7V to 3.6V
        };
    uint32_t erase_status = 0; // Store the status of Erasing operation
    /* Check The Validity of start Sector and number of sectors wanted to be erased */
    if ((start <= SECTOR_5) && ((start + num) <= NUMBER_FLASH_SECTORS))
    {
        erase_configurations.NbSectors = num;
        erase_configurations.Sector = start;
        /* Unlock the Flash memory */
        hal_status = HAL_FLASH_Unlock();
        if (HAL_OK != hal_status)
        {
            status = BL_ERROR;
        }
        else
        {
            /* Perfome Erasing */
            hal_status = HAL_FLASHEx_Erase(&erase_configurations, &erase_status);

            /* Check for Erasing success */
            if ((HAL_OK == hal_status) && (ERASE_SUCCESS == erase_status))
                status = BL_OK;
            else
                status = BL_ERROR;

            /* Lock the Flash memory */
            do
            {
                hal_status = HAL_FLASH_Lock();
            } while (HAL_OK != hal_status);
        }
    }
    return status;
}

static BL_status_t bl_write_program(uint8_t *buffer, uint8_t length)
{
    UNUSED(length);
    BL_status_t status = BL_ERROR;
    HAL_StatusTypeDef hal_status = HAL_ERROR;
    /* Get the Version of Program, Start Address of Program, and size of program */
    uint8_t Major = buffer[2];      // Major Version
    uint8_t Minor = buffer[3];      // Minor Version
    uint8_t Patch = buffer[4];      // Patch Version
    uint32_t program_add  = (uint32_t)GET_4BYTES(buffer, 5); // Get program address
    uint32_t program_size = (uint32_t)GET_4BYTES(buffer, 9); // Get program size

#if (BOOTLOADER_DEBUG_PROTOCOL != BOOTLOADER_STOP)
    printf("program_add: %li\n",program_add);
    printf("program_size: %li\n",program_size);
    printf("Major: %d\n",Major);
    printf("Minor: %d\n",Minor);
    printf("Patch: %d\n",Patch);
#endif

    /* Check Validity of Start of the Program and its size */
    if (((uint32_t)ALLOWED_PROGRAM_START_ADD <= program_add) && 
        ((uint32_t)ALLOWED_PROGRAM_END_ADD   > program_add) &&
        ((uint32_t)ALLOWED_PROGRAM_END_ADD   > (program_add + program_size)))
    {
        /* Unlock the Flash memory */
        hal_status = HAL_FLASH_Unlock();
        if (HAL_OK != hal_status)
        {
            status = BL_ERROR;
        }
        else
        {
            /* Performe Writing Program */
            status = write_program(program_add, program_size);
            /* Check for Error */
            if (BL_OK == status)
            {   
                /* Performe Writing Version */
                do
                {
                    status = write_version(Major, Minor, Patch);
                } while(BL_OK != status);
            }
            else
                status = sector_erase_execute(SECTOR_3, 3);

            /* Lock the Flash memory */
            do
            {
                hal_status = HAL_FLASH_Lock();
            } while (HAL_OK != hal_status);
        }
    }

#if (BOOTLOADER_DEBUG_PROTOCOL != BOOTLOADER_STOP)
    if (BL_OK != status)
    {
        printf("ERROR: Writing Program !!\n");
    }
#endif
    return status;
}

static BL_status_t write_program(uint32_t add, uint32_t size)
{
    BL_status_t status = BL_ERROR;
    HAL_StatusTypeDef hal_status = HAL_ERROR;
    uint32_t counter = size;
    uint8_t buf_counter = 0;
    uint32_t l_add = add;
    /* Start Receiving Packets which contains the program */
    do 
    {
        
#if (BOOTLOADER_DEBUG_PROTOCOL != BOOTLOADER_STOP)
        printf("Remaining Bytes: %li\n", counter);
#endif
        do 
        {
            HAL_Delay(50);
            /* Reset the buffers */
            memset(BL_Buffer_send, 0x00, BOOTLOADER_BUFFER_SIZE);
            memset(BL_buffer, 0x00, BOOTLOADER_BUFFER_SIZE);
            /* Receive a packet contains bytes of the program */
            hal_status = HAL_SPI_TransmitReceive (bootloader_spi,
                                              BL_Buffer_send,
                                              BL_buffer,
                                              BOOTLOADER_BUFFER_SIZE,
                                              HAL_MAX_DELAY);
        }while((memcmp(BL_buffer, BL_Buffer_send, BOOTLOADER_BUFFER_SIZE) == 0) 
               && ((HAL_OK == hal_status)));
        /* Check the receiving */
        if (HAL_OK != hal_status)
        {
#if (BOOTLOADER_DEBUG_PROTOCOL != BOOTLOADER_STOP)
        printf("ERROR Flash: SPI ERROR\n");
#endif
            status = BL_ERROR;
            break;
        }
        else
        {
            status = BL_OK;
            /* CRC check */
            if (counter >= 252)
            {
                status = bl_crc_check(BL_buffer, 255);
                buf_counter = 252;
                counter -= 252;
            }
            else
            {
                status = bl_crc_check(BL_buffer, counter - 1 + 4);
                buf_counter = counter;
                counter -= counter;
            }
            /* Send ACK or NACK */
            if (BL_OK == status)
            {
#if (BOOTLOADER_DEBUG_PROTOCOL != BOOTLOADER_STOP)
                printf("Flash Program: CRC Verified\n");
#endif
                status = Send_ACK();
                if (BL_OK == status)
                {
                    /* Writing to Flash */
                    for (uint8_t index = 0; index < buf_counter; index++)
                    {
                        hal_status = HAL_FLASH_Program(FLASH_TYPEPROGRAM_BYTE,
                                                add, BL_buffer[index]);
                        /* Check for ERROR */
                        if (hal_status != HAL_OK)
                            break;
                        add++;
                    }
                }
            }
            else 
            {
#if (BOOTLOADER_DEBUG_PROTOCOL != BOOTLOADER_STOP)
                printf("Flash Program: CRC ERROR\n");
#endif
                BL_status_t temp_status = BL_ERROR;
                for (uint8_t attempts = 0; 
                     ((temp_status != BL_OK) && (attempts < 5)); 
                     attempts++)
                    temp_status = Send_NACK();
                counter += buf_counter;
                status = BL_OK;
            }
        }
    } while((counter > 0) && (BL_OK == status) && (HAL_OK == hal_status));
    /* Edit The Last Flash Programed Address */
    hal_status = HAL_FLASH_Program(FLASH_TYPEPROGRAM_WORD, 
                                   LAST_FLASHED_PROGRAM_ADD, l_add);
    /* Determine the Return of the function depending on the last writing */
    if ((HAL_OK != hal_status) || (BL_OK != status))
    {
#if (BOOTLOADER_DEBUG_PROTOCOL != BOOTLOADER_STOP)
        printf("Flash Program: ERROR in write_program\n");
#endif
        Send_NACK();
        status = BL_ERROR;
    }
    return status;
}

static BL_status_t write_version(uint8_t major, uint8_t minor, uint8_t patch)
{
    BL_status_t status = BL_ERROR;
    HAL_StatusTypeDef hal_status = HAL_ERROR;
    /* perform Writing Version to Flash */
    hal_status = HAL_FLASH_Program(FLASH_TYPEPROGRAM_BYTE, MAJOR_ADD, major);
    if (HAL_OK == hal_status)
    {
        hal_status = HAL_FLASH_Program(FLASH_TYPEPROGRAM_BYTE, MINOR_ADD, minor);
        if (HAL_OK == hal_status)
        {
            hal_status = HAL_FLASH_Program(FLASH_TYPEPROGRAM_BYTE, PATCH_ADD, patch);
            if (HAL_OK == hal_status)
            {
                status = BL_OK;
            }
        }
    }
    return status;
}

/******************************************************************************/
