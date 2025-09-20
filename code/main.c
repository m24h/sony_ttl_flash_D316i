#include "main.h"

#include "stc/mem.h"
#include "stc/pio.h"
#include "stc/tmr.h"
#include "stc/sys.h"
#include "stc/ser.h"
#include "stc/adc.h"
#include "flash.h"

/*
this is a small flash with small Xenon tube (GN16), so uses nearly a 1/8 full power (at flash_time_of_level[0]) as base/pre-flash power.
higher power levels are stable and easy to control and measure.
lower power levels appears unstable and not easy to control, requiring practical attempts to obtain specific flashing time.
*/
const uint16_t flash_time_of_level[64] = {
76,81,86,105,122,137,150,170,
196,251,340,580,1724,1959,1986,1986,
1986,1986,1986,1986,1986,1986,1986,1986,
1986,1986,1986,1986,1986,1986,1986,1986,
16,16,16,16,16,16,16,16,
22,23,23,24,25,25,26,27,
28,29,29,30,31,32,34,37,
41,45,49,53,56,61,66,72
};


uint16_t volt_vdd_mv = 3300;
uint16_t volt_high = 0;

// ADC ref voltage, normally 1190mV
static uint16_t adc_ref=1190;

#pragma save
#pragma disable_warning 126 // unreachable code by some setup macros
static void setup(void)
{
    sys_wdt_enable(1000, 0);
            
    // set ports (pins connected to camera are excluded)
    pio_disable_all();
    pio_reset(0);   // not to use reset pin
    pio_mclk(0, 0); // not to output sys clock
    P10=0;     pio_config(PIO_P10, PIO_ANALOG); // VHSAMP
    RXD=1;     pio_config(pio_pnn(RXD), PIO_BIDIR | PIO_SCHMITT); 
    TXD=1;     pio_config(pio_pnn(TXD), PIO_BIDIR | PIO_SCHMITT); 
    MA=0;      pio_config(pio_pnn(MA), PIO_OUTPUT); 
    MB=0;      pio_config(pio_pnn(MB), PIO_OUTPUT); 
    POSA_N=1;  pio_config(pio_pnn(POSA_N), PIO_BIDIR | PIO_SCHMITT); 
    POSB_N=1;  pio_config(pio_pnn(POSB_N), PIO_BIDIR | PIO_SCHMITT); 
    VHCTL=0;   pio_config(pio_pnn(VHCTL), PIO_OUTPUT); 
    ILLU=0;    pio_config(pio_pnn(ILLU), PIO_OUTPUT);
    LED=0;     pio_config(pio_pnn(LED), PIO_OUTPUT);
    PON_N=1;   pio_config(pio_pnn(PON_N), PIO_BIDIR | PIO_SCHMITT); 
    PHOLD=0;   pio_config(pio_pnn(PHOLD), PIO_OUTPUT);

    // serial port 1 as debug port
    ser1_setup(BAUDRATE, 1, SER1_P30P31, 0); // use timer 1 to drive it, not to use intr.
    debug(1, "Starting\n");
        
    // LVD level
    sys_lvd_setup(3000, 0, 0);
    
    // timer 2 ticks every 10ms, started, will raise intr.
    tmr2_setup_reload24(100, 1, 1, 0, 0);

    // setup ADC
    adc_setup(ADC_RATE, 0, 0); // not to raise intr.
    debug(1, "ADC Rate = %u kHz\n", (uint16_t)(adc_real_speed(ADC_RATE)/1000));
#ifdef ADC_REF_MV
    adc_ref = ADC_REF_MV;
#else
    adc_ref = adc_ref_mv();
    if (adc_ref<1000 || adc_ref>1300) adc_ref=1190; // maybe VRef is not writen into ROM, so use the default
#endif
    debug(1, "ADC Ref = %u mV\n", adc_ref);
}
#pragma restore

// timer 2 as system tick
static __data volatile uint16_t tick_10ms_isr = 0;
void tmr2_isr(void) __interrupt (TMR2_VECTOR) __naked
{
    // _tick_10ms_isr++
    __asm
    push    acc
    push    psw
    inc _tick_10ms_isr
    clr a
    cjne a, _tick_10ms_isr, 00010$
    inc (_tick_10ms_isr + 1)
00010$:
    pop psw
    pop acc
    reti
    __endasm;
}

uint16_t main_tick_get(void) __naked
{
    __asm
    anl _IE2, #(~ET2)
    nop
    nop
    mov dpl, (_tick_10ms_isr)
    mov dph, (_tick_10ms_isr+1)
    orl _IE2, #(ET2)
    ret
    __endasm;
}

static void shutdown(void)
{
    VHCTL = 0; // turn off high voltage
    ILLU=0;
    LED=0;
    FIRE_N=1;
    
    sys_delay_ms(50); // wait for IGBT driver power off (1uF & 4.7kOhm, tRC=4.7ms)
}

// main entry
void main(void)
{
    static __bit HV_disable = 0;
    
    EA = 0;
    nop();
    nop();
    mem_xram_enable();
    setup();
    flash_setup();
    EA = 1;
    
    // main loop
    while(1) {
        // feed the dog
        sys_wdt_clear();
        
        static uint16_t loops=0;
        ++loops;

        // on/off and motor/HV
        if ((PCON & LVDF) || PON_N) { // low voltage or switch off
            if (VHCTL) shutdown();
            if (POSB_N) { 
                MB = 1;
                MA = 0;
            } 
            else {
                sys_delay_ms(100); // wait for the mechanics to get more into place
                MB = 0;
                MA = 0;
                if (!POSB_N) { // check again
                    PHOLD = 0; // try to power off
                }
            }
            continue; // not to go donwn
        } else {
            if (!PHOLD) PHOLD = 1; // keep power on
            if (HV_disable) { // power is not turned on, maybe in DEBUG mode
                VHCTL = 0;
            } else if (!VHCTL) {
                if (volt_high<(uint16_t)(VH_DEST)-5) VHCTL=1;
            } else {
                /* Due to the leakage inductance of transformer, the HV is not the main negative feedback provider for the step-up power supply,
                   which may lead to excessively high voltage, so it needs to be shut down.
                   When flashing is needed, Sony will notify at least 10ms in advance, 
                   enough time (nealy 1ms tested) to restart the HV power supply,
                   and provide the driving power required by the IGBT. */
                register uint8_t t=flash_data.state; // flash_data.state maybe changed between multiple judgments
                if (t!=FLASH_SONY_COMM_FLASH0 && t!=FLASH_SONY_COMM_FLASH1 && t!=FLASH_SONY_COMM_SYNC
                	&& volt_high>(uint16_t)(VH_DEST)+5) {
                	VHCTL = 0;
                }
            }           
            if (POSA_N) { // turn up flash window
                MA = 1;
                MB = 0;
            } else if (MA) {
                sys_delay_ms(20); // wait for the mechanics to get into place
                MA = 0;
                MB = 0;
            }
        }

        //To speed up the cycle turnaround, the tasks are spreaded using task number, which should be at 1-255
        static __data uint8_t taskno = 0;
        ++taskno;
        if (taskno == 1) {
            // get device voltages
            uint16_t t16 = adc_get(ADC_VREF_CH);
            t16 = adc_get(ADC_VREF_CH); // double get to avoid cross-interference
            if (t16<=4096) t16 = adc_get(ADC_VREF_CH);
            if (t16<=4096) continue; // sometimes get 0, seems to be the "zero value" bug of STC ADC, many have encountered it too
            // adc_value/65536*Vdd = adc_ref
            // Vdd = adc_ref * 65536 / adc_value
            t16 = (uint16_t)((((uint32_t)adc_ref<<16)+(t16>>1))/t16);
            t16 = ((volt_vdd_mv<<1)+volt_vdd_mv+t16+2)>>2; // a simple filter
            EA = 0; // guarantee actomic data
            nop();
            nop();
            volt_vdd_mv=t16;
            EA = 1;
        } else if (taskno == 2) {
            // get high voltage
            uint16_t t16 = adc_get(ADC_VHSAMP_CH);
            t16 = adc_get(ADC_VHSAMP_CH); // double get to avoid cross-interference
            if (t16<=4096) t16 = adc_get(ADC_VHSAMP_CH);
            if (t16<=4096) continue; // sometimes get 0 or noises, seems to be the "zero value" bug of STC ADC, many have encountered it too
            // adc_value/65536*volt_vdd_mv/1000 = volt_high*VH_RATIO+VH_BIAS
            // volt_high = (adc_value/65536*volt_vdd_mv/1000 - VH_BIAS)/VH_RATIO
            // volt_high = (1049*(adc_value/64)*volt_vdd_mv/65536 - VH_BIAS*16384)*(4/VH_RATIO)/65536
            t16 = (1049UL*volt_vdd_mv*(t16>>6)+32768)>>16;
            t16 = t16>(uint32_t)(VH_BIAS*16384) ? ((t16-(uint32_t)(VH_BIAS*16384))*(uint32_t)(4/VH_RATIO)+32768)>>16 : 0;
            t16 = ((volt_high<<1)+volt_high+t16+2)>>2; // a simple filter
            EA = 0; // guarantee actomic data
            nop();
            nop();
            volt_high = t16;
            EA = 1;
        } else if (taskno == 3) {
            // show LED
            if (VHCTL && volt_high<(uint16_t)(VH_LEAST)) {
            	flash_can_fire = 0;
            	LED = (uint8_t)main_tick_get() & 0x10;
            } else {
            	flash_can_fire = 1;
            	LED = 1;
            } 
            // just play with illuminator
            ILLU = !(flash_data.write.func5 & FLASH_SONY_WRITE_FUNC5_NO_AF);
        } else if (taskno == 4) {
            // incoming message (1 byte only) from default serial port
            switch ((unsigned char)tolower(getchar())) {
                case 'b': printf("SP = %x, ticks = %u, loops=%u\n", SP, main_tick_get(), loops);
                          break;
                case 'd': shutdown();
                          pio_disable_all(); 
                          sys_boot_ISP();
                          break;
                case 'r': shutdown();
                          pio_disable_all();
                          sys_boot();
                          break;
                case 'a': printf("VHCTL = %u, VDD = %u mV, VH = %u V\n", VHCTL, volt_vdd_mv, volt_high);
                          break;
                case 'p': printf("PON_N = %u, PHOLD = %u, VHCTL = %u, LVD=%u\n", PON_N, PHOLD, VHCTL, PCON & LVDF);
                          printf("ILLU = %u, LED = %u\n", ILLU, LED);
                          printf("MA = %u, MB = %u, POSA_N = %u, POSB_N = %u\n", MA, MB, POSA_N, POSB_N);
                          printf("F1SYNC = %u, F2DAT = %u, F3CLK = %u, FIRE_N = %u\n", FLASH_F1SYNC, FLASH_F2DAT, FLASH_F3CLK, FLASH_FIRE_N);
                          break;
                case 'c': PON_N = 0;
                          PHOLD = 1;
                          VHCTL = 0;
                          HV_disable = 1;
                          sys_delay_ms(1);
                          printf("PON_N = %u, PHOLD = %u, VHCTL = %u\n", PON_N, PHOLD, VHCTL);
                          break;
                case 'x': PON_N=1;
                          PHOLD = 0;
                          VHCTL = 0;
                          HV_disable = 0;
                          sys_delay_ms(1);
                          printf("PON_N = %u, PHOLD = %u, VHCTL = %u\n", PON_N, PHOLD, VHCTL);
                          break;
                case 'f': VHCTL = 1;
                          sys_delay_ms(10);
                          FIRE_N=0;
                          sys_delay_us(30);
                          FIRE_N=1;
                          sys_delay_ms(20);
                          FIRE_N=0;
                          sys_delay_us(100);
                          FIRE_N=1;
                          VHCTL = 0;
                          break;
                case 'i': ILLU=!ILLU;
                          sys_delay_ms(1);
                          printf("ILLU = %u, LED = %u\n", ILLU, LED);
                          break;
                case 's': printf("Sony read : ");
                		  for (uint8_t i=0; i<flash_data.rlen; i++) printf("%x ", flash_data.rbytes[i]);
                		  printf("\nSony write : ");
                		  for (uint8_t i=0; i<flash_data.wlen; i++) printf("%x ", flash_data.wbytes[i]);
                		  printf("\nPower pre=%d formal=%d\n", flash_power_pre, flash_power_formal);
                          printf("Actual pre=%d formal=%d\n", flash_power_pre_act, flash_power_formal_act);
                          break;
                case 't': static int8_t power=-12;
                          power++;
                          flash_data.read.mode &= ~FLASH_SONY_READ_MODE_CONTROLLED; // manual mode
                          flash_data.read.power = ((FLASH_POWER_LEVEL_MAX-power)<<2)+15;
                          printf("Manual mode, power=%d\n", power);
						  break;
                case 'm': 
                case '?': printf("bdrapcxfi\n");
                          break;
            } 
        }  else taskno = 0;
    }
} 

void flash_prepare(void) __using(FLASH_ISR_BANK)
{
	VHCTL = 1; // needs to turn on HV for IGBT driver
}
