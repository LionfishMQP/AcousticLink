/**
 */



//--------------------------        PIN ASSIGNMENT       --------------------------//
/* Port L: PL0:PL3: Hardware Debug Pin
 * Port PF0:PF3 : PWM module 0 generator 0 and 1 // note:PF0 is not available on booster back breakout.
 * Port PK4:PK5 : PWM module 0 generator 3
 */

//--------------------------        TIMER ASSIGNMENT     --------------------------//
/* TIMER 1 : TX bit modulation
 * TIMER 2 : ADC trigger timer
 *
 */



/* Port Assigement:
 * Port L : Debug
 * Port PF0:PF3 : PWM
 *
 *
 */

// ��C:\ti\\TivaWare_C_Series2.1.1.71b�� set as ${SW_ROOT} needs to be added to compiler path for headers

// "${SW_ROOT}/driverlib/ccs/Debug/driverlib.lib" Pre Compiled TivaWare libs needs to exist in ARM Linker file search Path

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <math.h>
#include <float.h>
#include <string.h>
#include "driverlib/fpu.h"
#include "driverlib/sysctl.h"
#include "driverlib/interrupt.h"
#include "driverlib/gpio.h"
#include "driverlib/udma.h"
#include "driverlib/adc.h"
#include "driverlib/timer.h"
#include "inc/tm4c1294ncpdt.h"
#include "inc/hw_memmap.h"

#include "sampling.h"
#include "hwDebug.h"
#include "pwmDriver.h"
#include "ALinkProtocal.h"
#include "TimerTX.h"
#include "crc-8.h"

#define VIN_RANGE 3.3 // 3.3 V on board
#define ADC_BITS 12 // 12 bit ADC
#define SAMPLE_RATE 5000 //Samples Per Second

#define START_BITS 0xFA //1111 1010
#define ID_COMMAND 0x88 //1000 1000
#define ID_INFO 0x8F    //1000 1111
#define COMMAND_ARM 1   //xxxx xx01
#define COMMAND_DISARM 0//xxxx xx00
#define COMMAND_RECALL 3//xxxx xx11


//GLOBALS
uint32_t gSystemClock; // [Hz] system clock frequency
uint32_t gTime = 8345; // time in hundredths of a second

int samples_per_bit = (int) ADC_SAMPLING_RATE / BitRate;

uint8_t data_buffer[16];
int schedule_index = 0;//keep track of what variable we are logging
int schedule_max = SchedulerLength;

ID id; //used to find 8 bit values for ID's
COMMAND command; //used to find 8-bit values for commands

bool READING = false; //boolean flag for READING in bits
bool AUV_RECALL = false; //flags for Jetson
bool AUV_ARM = false; //flags for Jetson

//FUNC DEFINITIONS
void process_raw();
void process_request(uint8_t payload);
void process_command(uint8_t payload);
void process_info(uint8_t payload);
bool crc_check(uint32_t packet);
bool check_raw_start();
void delayMS(int ms);
int * message_to_binary(char *input, int input_length);
void init_keys(ID * id, COMMAND * command);


int main(void)
{
    IntMasterDisable();

    // Enable the Floating Point Unit, and permit ISRa`````11qqqqqqqqqqqqqq``ws to use it
    FPUEnable();
    FPULazyStackingEnable();

    // Initialize the system clock to 120 MHz
    gSystemClock = SysCtlClockFreqSet(SYSCTL_XTAL_25MHZ | SYSCTL_OSC_MAIN | SYSCTL_USE_PLL | SYSCTL_CFG_VCO_480, 120000000);

    // Initialize
    SamplingInit();
    debugPinsInit();
    pwm1Init();
    pwm3Init();
//    timer1Init();

    volatile int temp=0;
    while(1)
    {
        int i ;
        for( i=0;i<10000;i++)
        {
            temp +=1;

        }
    }

    uint32_t packet = 0x88EECE;
    uint32_t crc_packet = generate_packet_crc(packet);

    printf("packet: %04x\ncrc_packet: %04x\n", packet, crc_packet);

    if (verify_packet(crc_packet)){
        printf("TRUE!\n");
    }else{
        printf("FALSE!\n");
    }

    IntMasterEnable();

    //RX VARIABLES
    uint32_t raw_rx = 0; //variable to store received messages
    uint32_t buffer_log[1024]; //store rx_bits in 32 bit chunks
    int buffer_index = 0; //buffer index
    int ADC_OFFSET = 515; //threshold for 0/1 TODO: measure and change
    uint16_t crc_mask = (1 << 9) - 1; //binary = 0000 0000 0000 0000 0000 0000 1111 1111
    init_keys(&id, &command);


    //Main Loop
    while(1)
    {
        int i, on_count, off_count, buffer_avg, tx_count; //variables for ADC processing

        //ADC SAMPLING ROUTINE
        for(i = 0; i < ADC_BUFFER_SIZE; i++)
        {
            buffer_avg += gADCBuffer[i]; //sum up all samples

            //OFFSET PROCESSING
            if(gADCBuffer[i] < ADC_OFFSET){// counts as a 0
                off_count++;
            }else if(gADCBuffer[i] > ADC_OFFSET){//counts as a 1
                on_count++;
            }

            //SAMPLE COUNT ROUTINE
            if(off_count >= samples_per_bit){ //found enough samples to process a 0 read
                raw_rx = raw_rx << 1; //shift bits left 1
                off_count = 0;
                tx_count++;
            }else if(on_count >= samples_per_bit){ //found enough samples to process a 1 read
                raw_rx = raw_rx << 1; //shift bits left 1
                raw_rx += 1; //add 1 to LSB
                on_count = 0;
                tx_count++;
            }

            //START BIT CHECKING (only if not reading)
            if((!READING) && check_raw_start(raw_rx)){//current rx has just read the start bits
                raw_rx &= crc_mask; //everything but the start bits are now 0
                tx_count = 8; //reset count, adjusted for 8 start bits
            }

            //LOGGING + PACKET PROCESS
            if(tx_count >= 32){
                READING = false; //done reading packet

                if(crc_check(raw_rx)){ //check the packed for errors
                    process_raw(raw_rx); //look at packet frame as whole
                }

                buffer_log[buffer_index] = raw_rx; //add to log
                buffer_index++; //update index
                if(buffer_index == 1024) buffer_index=0; //wrap index back if at last element
            }
        }

        buffer_avg = buffer_avg / ADC_BUFFER_SIZE; //average buffer values (Use for Threshold Control?)
    }
}

//uses scheduler to log payload appropriately
void process_info(uint8_t payload) //TODO: this + Scheduler
{
    data_buffer[schedule_index] = payload;
    schedule_index++;

    if(schedule_index > SchedulerLength){
        schedule_index = 0;
    }
}

//processes command payload and set appropriate pins
void process_command(uint8_t payload) //TODO: add code to write GPIO HIGH OR LOW + define ARM/RECALL PINS
{
    int mask = (1 << 3) - 1; // 0011 mask for LS 2 bits
    payload = (uint8_t) (mask && payload);

    if((int) payload == (int) command.ARM){
        AUV_ARM = true;
        //set ARM pin to HIGH
    }
    if((int) payload == (int) command.DISARM){
        AUV_ARM = false;
        //set ARM pin to LOW
    }
    if((int) payload == (int) command.RECALL){
        AUV_RECALL = true;
        //set RECALL pin to HIGH
    }
}

//use payload to find and resend the information based on the ID
void process_request(uint8_t payload) //TODO: add code to write GPIO HIGH OR LOW + define ARM/RECALL PINS
{

}

//uses raw_rx (contains a frame)
void process_raw(uint32_t packet)
{
    uint32_t payload_mask = (1 << 17) - 1; //binary =  0000 0000 0000 0000 1111 1111 0000 0000
    uint32_t id_mask = (1 << 25) - 1; //binary =       0000 0000 1111 1111 0000 0000 0000 0000

    uint32_t packet_id = (id_mask && packet) >> 16; //shift id to most significant 8 bit
    uint32_t payload = (payload_mask && packet) >> 8; //shift payload to most significant 8 bit

    if((int) packet_id == (int) id.COMMAND_STATUS){
        process_command(payload); //perform appropriate command
    }else if((int) packet_id == (int) id.REQUEST){
        process_request(payload); //resend data provided by the payload being the ID
    }else{ //log packet
        process_info(payload);
    }
}

bool crc_check(uint32_t packet) //TODO: this
{
    bool errors = false;
    return errors;
}

//check raw rx for start bits
bool check_raw_start(uint32_t packet)
{
    uint8_t bit_xor = (packet ^ (uint8_t) START_BITS);
    return (bit_xor == 0); //if bit_xor == 0, then all bits in rx == start bits
}


void delayMS(int ms) {
    SysCtlDelay( (SysCtlClockGet()/(3*1000))*ms ) ;
}

/**
 * Function: message_to_binary
 * Arguments:
 *      -char *input: pointer to array of message characters
 *      -int input_length: size of the char array
 *
 * Returns: a integer array of the characters message in binary
 * **/
int * message_to_binary(char *input, int input_length)
{
    int * output = malloc(input_length * 8 * sizeof(int));
    int output_index = 0;

    int i;
    for (i = 0; i < input_length; i++) // loop through input characters
    {
        int dec = (int) input[i]; // decimal ASCII representation
        int remainder, j = 1; // binary rep of the current character
        int temp_byte[8] = {0,0,0,0,0,0,0,0};
        int temp_index = 7;

        while(dec != 0){ // convert to binary loop
            remainder = dec % 2; //LSB
            temp_byte[temp_index] = remainder; temp_index--; //temp store bit (reversed)
            dec = dec / 2; j *= 10; //iterate
        }

        int k;
        for(k = 0; k < 8; k++){
            output[output_index] = temp_byte[j]; //store in correct order
            output_index++;
        }
    }
    return output;
}

//init values for COMMAND and ID lookups
void init_keys(ID * id_key, COMMAND * command_key){
    id_key->COMMAND_STATUS = 0x01;
    id_key->REQUEST = 0x02;

    int i, num = 0x03;
    for(i = 0; i < 14; i++){
        id_key->SCHEDULER_INFO[i] = num;
        num ++;
    }

    command_key->ARM = 0x01;
    command_key->DISARM = 0x02;
    command_key->RECALL = 0x03;
}
