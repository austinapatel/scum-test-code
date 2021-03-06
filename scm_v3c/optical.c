#include <stdio.h>
#include <string.h>

#include "memory_map.h"
#include "scm3c_hw_interface.h"

#include "radio.h"
#include "scum_defs.h"

//=========================== defines =========================================

#define OPTICAL_CALIBRATION_ITERATION_COUNT 25
// Flag set to 1 if SCuM should still process optical interrupts after OPTICAL_CALIBRATION_ITERATION_COUNT
// iterations have occured. This could be useful for logging clock counts using the optical interrupts from
// the optical programmer. If set set 1, then SCuM will NEVER leave the optical calibration phase. If set to
// 0, then calibration will occur normally.
#define POST_OPTICAL_CALIBRATION_LOGGING 0

//=========================== variables =======================================

typedef struct {
    uint8_t     optical_cal_iteration;
    uint8_t     optical_cal_finished;
    
    uint32_t    num_32k_ticks_in_100ms;
    uint32_t    num_2MRC_ticks_in_100ms;
    uint32_t    num_IFclk_ticks_in_100ms;
    uint32_t    num_LC_ch11_ticks_in_100ms;
    uint32_t    num_HFclock_ticks_in_100ms;

    // reference to calibrate
    uint32_t    LC_target;
    uint32_t    LC_code;
} optical_vars_t;

optical_vars_t optical_vars;

//=========================== prototypes ======================================

//=========================== public ==========================================

void optical_init(void) {
    
    memset(&optical_vars, 0, sizeof(optical_vars_t));
    
    // Target radio LO freq = 2.4025G
    // Divide ratio is currently 480*2
    // Calibration counts for 100ms
    optical_vars.LC_target  = REFERENCE_LC_TARGET;
    optical_vars.LC_code    = DEFUALT_INIT_LC_CODE;
}

uint8_t optical_getCalibrationFinshed(void) {
    return optical_vars.optical_cal_finished;
}

void optical_enable(void){
    ISER = 0x1800; // 1 is for enabling GPIO8 ext interrupt and 8 is for enabling optical interrupt // TODO 3wb
}

//=========================== interrupt =======================================

// This interrupt goes off every time 32 new bits of data have been shifted into the optical register
// Do not recommend trying to do any CPU intensive actions while trying to receive optical data
// ex, printf will mess up the received data values
void optical_32_isr(){
    //printf("Optical 32-bit interrupt triggered\r\n");
    
    //unsigned int LSBs, MSBs, optical_shiftreg;
    //int t;
    
    // 32-bit register is analog_rdata[335:304]
    //LSBs = ANALOG_CFG_REG__19; //16 LSBs
    //MSBs = ANALOG_CFG_REG__20; //16 MSBs
    //optical_shiftreg = (MSBs << 16) + LSBs;    
    
    // Toggle GPIO 0
    //GPIO_REG__OUTPUT ^= 0x1;
    
}

// This interrupt goes off when the optical register holds the value {221, 176, 231, 47}
// This interrupt can also be used to synchronize to the start of an optical data transfer
// Need to make sure a new bit has been clocked in prior to returning from this ISR, or else it will immediately execute again
void optical_sfd_isr(){
    int32_t t;
    uint32_t rdata_lsb, rdata_msb; 
    uint32_t count_LC, count_32k, count_2M, count_HFclock, count_IF;
    
    uint32_t HF_CLOCK_fine;
    uint32_t HF_CLOCK_coarse;
    uint32_t RC2M_coarse;
    uint32_t RC2M_fine;
    uint32_t RC2M_superfine;
    uint32_t IF_clk_target;
    uint32_t IF_coarse;
    uint32_t IF_fine;
	    
    HF_CLOCK_fine       = scm3c_hw_interface_get_HF_CLOCK_fine();
    HF_CLOCK_coarse     = scm3c_hw_interface_get_HF_CLOCK_coarse();
    RC2M_coarse         = scm3c_hw_interface_get_RC2M_coarse();
    RC2M_fine           = scm3c_hw_interface_get_RC2M_fine();
    RC2M_superfine      = scm3c_hw_interface_get_RC2M_superfine();
    IF_clk_target       = scm3c_hw_interface_get_IF_clk_target();
    IF_coarse           = scm3c_hw_interface_get_IF_coarse();
    IF_fine             = scm3c_hw_interface_get_IF_fine();
           
		// Read in all the clock counts
		read_counters();
    count_32k = scm3c_hw_interface_get_count_32k();
    count_HFclock = scm3c_hw_interface_get_count_HF();
    count_2M = scm3c_hw_interface_get_count_2M();
    count_LC = scm3c_hw_interface_get_count_LC_div();
    count_IF = scm3c_hw_interface_get_count_IF();
		
		// Keep track of how many calibration iterations have been completed
    optical_vars.optical_cal_iteration++;
    
		// Reset the counters in preparation of next iteration
    reset_counters();
		enable_counters();
        
    // Don't make updates on the first two executions of this ISR
		// only make updates until the number of desired iterations is reached
    if(optical_vars.optical_cal_iteration > 2 && optical_vars.optical_cal_iteration <= OPTICAL_CALIBRATION_ITERATION_COUNT){
        
        // Do correction on HF CLOCK
        // Fine DAC step size is about 6000 counts
        if(count_HFclock < 1997000) {
            HF_CLOCK_fine--;
        }
        if(count_HFclock > 2003000) {
            HF_CLOCK_fine++;
        }
        
        set_sys_clk_secondary_freq(HF_CLOCK_coarse, HF_CLOCK_fine);
        scm3c_hw_interface_set_HF_CLOCK_coarse(HF_CLOCK_coarse);
        scm3c_hw_interface_set_HF_CLOCK_fine(HF_CLOCK_fine);
        
        // Do correction on LC
        if(count_LC > (optical_vars.LC_target + 0)) {
            optical_vars.LC_code -= 1;
        }
        if(count_LC < (optical_vars.LC_target - 0)) {
            optical_vars.LC_code += 1;
        }
        LC_monotonic(optical_vars.LC_code);
				
        // Do correction on 2M RC
        // Coarse step ~1100 counts, fine ~150 counts, superfine ~25
        // Too fast
        if(count_2M > (200600)) {
            RC2M_coarse += 1;
        } else {
            if(count_2M > (200080)) {
                RC2M_fine += 1;
            } else {
                if(count_2M > (200015)) {
                    RC2M_superfine += 1;
                }
            }
        } 
        
        // Too slow
        if(count_2M < (199400)) {
            RC2M_coarse -= 1;
        } else {
            if(count_2M < (199920)) {
                RC2M_fine -= 1;
            } else {
                if(count_2M < (199985)) {
                    RC2M_superfine -= 1;
                }
            }
        }
        
        set_2M_RC_frequency(31, 31, RC2M_coarse, RC2M_fine, RC2M_superfine);
        scm3c_hw_interface_set_RC2M_coarse(RC2M_coarse);
        scm3c_hw_interface_set_RC2M_fine(RC2M_fine);
        scm3c_hw_interface_set_RC2M_superfine(RC2M_superfine);

        // Do correction on IF RC clock
        // Fine DAC step size is ~2800 counts
				// Coarse DAC step size is ~25000 counts
				
				// if very far away then change coarse, otherwise change fine
				
				if (count_IF > (1600000+12500)) {
					IF_coarse += 1;
				} else if (count_IF < (1600000-12500)) {
					IF_coarse -= 1;
				} else {
					// in this case close enough to change the fine codes
					if(count_IF > (1600000+1400)) {
							IF_fine += 1;
					} else if(count_IF < (1600000-1400)) {
							IF_fine -= 1;
					} else {
					}
				}
        
        set_IF_clock_frequency(IF_coarse, IF_fine, 0);
        scm3c_hw_interface_set_IF_coarse(IF_coarse);
        scm3c_hw_interface_set_IF_fine(IF_fine);
        
        analog_scan_chain_write();
        analog_scan_chain_load();
				
				// Debugging output
				printf("HF=%d-%d   2M=%d-%d,%d,%d   LC=%d-%d   IF=%d-%d,%d\r\n",count_HFclock,HF_CLOCK_fine,count_2M,RC2M_coarse,RC2M_fine,RC2M_superfine,count_LC,optical_vars.LC_code,count_IF,IF_coarse,IF_fine); 
    }
    
		// on the very last iteration of optical calibration, store the final counts
    if(optical_vars.optical_cal_iteration == OPTICAL_CALIBRATION_ITERATION_COUNT){
				printf("#define HF_COARSE %u\n#define HF_FINE %u\n#define RC2M_COARSE %u\n#define RC2M_FINE %u\n#define RC2M_SUPERFINE %u\n#define IF_COARSE %u\n#define IF_FINE %u\n",
							HF_CLOCK_coarse, HF_CLOCK_fine, RC2M_coarse, RC2M_fine, RC2M_superfine, IF_coarse, IF_fine);
        // Store the last count values
        optical_vars.num_32k_ticks_in_100ms = count_32k;
        optical_vars.num_2MRC_ticks_in_100ms = count_2M;
        optical_vars.num_IFclk_ticks_in_100ms = count_IF;
        optical_vars.num_LC_ch11_ticks_in_100ms = count_LC;
        optical_vars.num_HFclock_ticks_in_100ms = count_HFclock;
    
        // Debug prints
        //printf("LC_code=%d\r\n", optical_vars.LC_code);
        //printf("IF_fine=%d\r\n", IF_fine);
                        
        // This was an earlier attempt to build out a complete table of LC_code for TX/RX on each channel
        // It doesn't really work well yet so leave it commented
        //printf("Building channel table...");
        
        //radio_build_channel_table(LC_code);
        
        //printf("done\r\n");
        
        //radio_disable_all();
        
        // Halt all counters only if we aren't going to log afterwards
				if (!POST_OPTICAL_CALIBRATION_LOGGING) {
						ANALOG_CFG_REG__0 = 0x0000;
					
						// Disable this ISR
						ICER = 0x1800; // TODO 3WB
						//optical_vars.optical_cal_iteration = 0;
						optical_vars.optical_cal_finished = 1;					
				}
    }
		
		// Start the logging process if enabled and we have finished the normal optical calibration phase
		if (POST_OPTICAL_CALIBRATION_LOGGING &&
			optical_vars.optical_cal_iteration > OPTICAL_CALIBRATION_ITERATION_COUNT) {
				printf("32kHz: %d 2MHz %d\n", count_32k, count_2M);
		}
		
}