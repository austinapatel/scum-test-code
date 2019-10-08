#include <stdio.h>
#include <time.h>
#include <rt_misc.h>
#include <stdlib.h>

#include <math.h>
#include "scum_radio_bsp.h"
#include "scm3_hardware_interface.h"
#include "lighthouse.h"
#include "Memory_map.h"

extern char send_packet[127];

//functions//
//put this in senddone (in inthandlers.h)////
// set radio frequency for rx
//run radio rx enable 
//call radio rx_now
//if you hear a packet, rx_done runs and thats where things could be found (acks) 
void send_lh_packet(unsigned int sync_time, unsigned int laser_time, lh_id_t lighthouse, angle_type_t angle_type){
					int i;
						//turn on radio (radio_txenable)
					radio_txEnable();
					//enable radio interrupts (radio_enable_interrupts) (do this somewhere; only needs to be done once)
					//place data into a "send_packet" global variable array 
					send_packet[0]= (lighthouse <<1) & angle_type;
					send_packet[1] = sync_time & 0xFF;
					send_packet[2] = laser_time & 0xFF;
					//call "radio_loadpacket", which puts array into hardware fifo (takes time)
					radio_loadPacket(3);
					
					//set radio frequency (radio_setfrequency). This needs to be figured out
					LC_FREQCHANGE(22&0x1F, 21&0x1F, 4&0x1F); //for set frequency
					
					
					for(i = 0; i<2500; i++){
						
					}
					//transmit packet (radio_txnow) (wait 50 us between tx enable and tx_now)
					radio_txNow();
}
pulse_type_t classify_pulse(unsigned int timestamp_rise, unsigned int timestamp_fall){
  pulse_type_t pulse_type;
	unsigned int pulse_width;
	
	pulse_width = timestamp_fall - timestamp_rise;
	pulse_type = INVALID;

	// Identify what kind of pulse this was

	if(pulse_width < 585 + WIDTH_BIAS && pulse_width > 100 + WIDTH_BIAS) pulse_type = LASER; // Laser sweep (THIS NEEDS TUNING)
	if(pulse_width < 675 + WIDTH_BIAS && pulse_width > 585 + WIDTH_BIAS) pulse_type = AZ; // Azimuth sync, data=0, skip = 0
	if(pulse_width >= 675 + WIDTH_BIAS && pulse_width < 781 + WIDTH_BIAS) pulse_type = EL; // Elevation sync, data=0, skip = 0
	if(pulse_width >= 781 + WIDTH_BIAS && pulse_width < 885 + WIDTH_BIAS) pulse_type = AZ; // Azimuth sync, data=1, skip = 0
	if(pulse_width >= 885 + WIDTH_BIAS && pulse_width < 989 + WIDTH_BIAS) pulse_type = EL; // Elevation sync, data=1, skip = 0
	if(pulse_width >= 989 + WIDTH_BIAS && pulse_width < 1083 + WIDTH_BIAS) pulse_type = AZ_SKIP; //Azimuth sync, data=0, skip = 1
	if(pulse_width >= 1083 + WIDTH_BIAS && pulse_width < 1200 + WIDTH_BIAS) pulse_type = EL_SKIP; //elevation sync, data=0, skip = 1
	if(pulse_width >= 1200 + WIDTH_BIAS && pulse_width < 1300 + WIDTH_BIAS) pulse_type = AZ_SKIP; //Azimuth sync, data=1, skip = 1
	if(pulse_width >= 1300 + WIDTH_BIAS && pulse_width < 1400 + WIDTH_BIAS) pulse_type = EL_SKIP; //Elevation sync, data=1, skip = 1

	

	return pulse_type;
}


//This function takes the current gpio state as input returns a debounced version of
//of the current gpio state. It keeps track of the previous gpio states in order to add
//hysteresis to the system. The return value includes the current gpio state and the time
//that the first transition ocurred, which should help glitches from disrupting legitamate
//pulses. This is called 
void debounce_gpio(unsigned short gpio, unsigned short * gpio_out, unsigned int * trans_out){
	//keep track of number of times this gpio state has been measured since most recent transistion
	static int count;
	static gpio_tran_t deb_gpio; //current debounced state
	
	static unsigned int tran_time;
	static unsigned short target_state;
	// two states: debouncing and not debouncing
	static enum state{NOT_DEBOUNCING = 0,DEBOUNCING = 1} state = NOT_DEBOUNCING;
	
	switch(state){
		
		
		case NOT_DEBOUNCING: {
			//if not debouncing, compare current gpio state to previous debounced gpio state
			if(gpio != deb_gpio.gpio){
				//record start time of this transition
				tran_time = RFTIMER_REG__COUNTER;
				//if different, initiate debounce procedure
				state = DEBOUNCING;
				target_state = gpio;

				//increment counter for averaging
				count++;
			}
			//otherwise just break without changing curr_state
			break;
		}
		case DEBOUNCING: {
	//if debouncing, compare current gpio state to target transition state
			if(gpio == target_state){
				//if same as target transition state, increment counter 
				count++;
			}
			else{
				//if different from target transition state, decrement counter
				count--;
			}

		//if count is high enough
			if(count>=DEB_THRESH){

				deb_gpio.timestamp_tran = tran_time;
				deb_gpio.gpio = target_state;
				state=NOT_DEBOUNCING;
				count = 0;
				
			}else if(count == 0){
				count = 0;
				state = NOT_DEBOUNCING;
			}
			break;
		}
	}
	*gpio_out = deb_gpio.gpio;
	*trans_out = deb_gpio.timestamp_tran;
	
	
}




//keeps track of the current state and will print out pulse train information when it's done.
void update_state(pulse_type_t pulse_type, unsigned int timestamp_rise){
	

	
	if(pulse_type == INVALID){
		return;
	}
	// FSM which searches for the four pulse sequence
			// An output will only be printed if four pulses are found and the sync pulse widths
			// are within the bounds listed above.
		update_state_azimuth(pulse_type,timestamp_rise);
		update_state_elevation(pulse_type,timestamp_rise);

}


void update_state_elevation(pulse_type_t pulse_type, unsigned int timestamp_rise){
		static unsigned int elevation_a_sync;
	static unsigned int elevation_b_sync;

	static unsigned int elevation_a_laser;
	static unsigned int elevation_b_laser;	
	
	static unsigned int elevation_unknown_sync;
	int i;
	static int state = 0;
	
	int nextstate;
	
	//TODO: keep track of last delta time measurement for filtering
	static unsigned int last_delta_a;
	static unsigned int last_delta_b;
	
	
	if(pulse_type == INVALID){
		return;
	}
			switch(state)
	{
		// Search for an azimuth sync pulse, assume its A 
		case 0: {
			if(pulse_type == EL || pulse_type == EL_SKIP){
				if(pulse_type == EL){
					elevation_a_sync = timestamp_rise;
					nextstate = 1;

				}
				else{
					//go to elevation b state
					nextstate = 2;
					//printf("state transition: %d to %d\n",state,nextstate);
				}
			}
			else{
				if(DEBUG_STATE){
					printf("el state fail. State %d, Pulse Type: %d \n",state,pulse_type);
				}
				nextstate = 0;
			}
			
			break;
		}
		
		// Waiting for another consecutive elevation skip sync from B, this should be a skip sync pulse 
		case 1: {
			if(pulse_type == EL_SKIP) {
				
				nextstate = 3;
			}else if (pulse_type == EL){
				//return to this state (skip 0)
				nextstate = 1;
				elevation_a_sync = timestamp_rise;
			}				
			else{
				nextstate = 0;
				if(DEBUG_STATE){
					printf("el state fail. State %d, Pulse Type: %d \n",state,pulse_type);
				}
			}
			break;
		}
		
		// Azimuth B sync state 
		case 2: {
			if(pulse_type == EL) {
				//the last pulse was an azimuth sync from lighthouse B
				elevation_b_sync = timestamp_rise;
				//go to azimuth b laser detect
				nextstate = 4;
				//printf("state transition: %d to %d\n",state,nextstate);
			}else if (pulse_type == EL_SKIP){
				//return to this state (skipping 0)
				nextstate = 2;
			}
			else{
				nextstate = 0;
				//printf("state fail. State %d, Pulse Type: %d \n",state,pulse_type);
			}
			break;
		}
	
		// Elevation A laser sweep
		case 3: {
			if(pulse_type == LASER) {
				//lighthouse a laser
				elevation_a_laser = timestamp_rise;
			
				nextstate = 0;
				if(last_delta_a > 0 && abs(((int)(elevation_a_laser-elevation_a_sync))-(int)last_delta_a)<4630){
					printf("el A: %d, %d\n",elevation_a_sync,elevation_a_laser);
					send_lh_packet(elevation_a_sync,elevation_a_laser,A,ELEVATION);
				}
				last_delta_a = elevation_a_laser - elevation_a_sync;
			}else if(pulse_type == EL){
				//skip straight to state 1
				nextstate = 1;
				elevation_a_sync = timestamp_rise;
			}else if (pulse_type == EL_SKIP){
				//skip straight to state 2
				nextstate = 2;
			}
			else{
				nextstate = 0;
				if(DEBUG_STATE){
					printf("el state fail. State: %d, Pulse Type: %d \n",state,pulse_type);
				}
			}
			break;
		}
		
		// elevation B laser sweep
		case 4: {
			if(pulse_type == LASER) {
				//lighthouse b laser
				elevation_b_laser = timestamp_rise;
				//go to azimuth b laser detect
				nextstate = 0;
				if(last_delta_b > 0 && abs(((int)(elevation_b_laser-elevation_b_sync))-(int)last_delta_b)<4630){
					printf("el B: %d, %d\n",elevation_b_sync,elevation_b_laser);
					send_lh_packet(elevation_b_sync,elevation_b_laser,B,ELEVATION);
				}
				last_delta_b = elevation_b_laser - elevation_b_sync;
			}else if(pulse_type == EL){
				//skip straight to state 1
				nextstate = 1;
				elevation_a_sync = timestamp_rise;
			}else if (pulse_type == EL_SKIP){
				//skip straight to state 2
				nextstate = 2;
			}
			else{
				nextstate = 0;
				if(DEBUG_STATE){
					printf("el state fail. State: %d, Pulse Type: %d \n",state,pulse_type);
				}
			}
			break;
		}		

	}
	
	state = nextstate;

}

//keeps track of the current state and will print out pulse train information when it's done.
void update_state_azimuth(pulse_type_t pulse_type, unsigned int timestamp_rise){
	
	static unsigned int azimuth_unknown_sync; 
	static unsigned int azimuth_a_sync;
	static unsigned int azimuth_b_sync;

	static unsigned int azimuth_a_laser;
	static unsigned int azimuth_b_laser;

	static int state = 0;
	
	int nextstate;
	
	//TODO: keep track of last delta time measurement for filtering
	static unsigned int last_delta_a;
	static unsigned int last_delta_b;
	
	if(pulse_type == INVALID){
		return;
	}
	// FSM which searches for the four pulse sequence
			// An output will only be printed if four pulses are found and the sync pulse widths
			// are within the bounds listed above.
	switch(state)
	{
		// Search for an azimuth A sync pulse, we don't know if it's A or B yet
		case 0: {
			if(pulse_type == AZ || pulse_type == AZ_SKIP){
				if(pulse_type == AZ){
									azimuth_a_sync = timestamp_rise;
					nextstate = 1;
					//printf("state transition: %d to %d\n",state,nextstate);
				}
				else{
					//go to azimuth b state
					nextstate = 2;
					//printf("state transition: %d to %d\n",state,nextstate);
				}
			}
			else
				nextstate = 0;
			
			break;
		}
		
		// Waiting for another consecutive azimuth skip sync from B, this should be a skip sync pulse 
		case 1: {
			if(pulse_type == AZ_SKIP) {
				
				//lighthouse A sweep pulse

				
				nextstate = 3;
				//printf("state transition: %d to %d\n",state,nextstate);
			}
			else if (pulse_type == AZ){
				
					//staty in this state 
					nextstate = 1;
					azimuth_a_sync = timestamp_rise;
			}
			else{
				nextstate = 0;
			//printf("state fail. State %d, Pulse Type: %d \n",state,pulse_type);
			}
			break;
		}
		
		// Azimuth B sync state 
		case 2: {
			if(pulse_type == AZ) {
				//the last pulse was an azimuth sync from lighthouse B
				azimuth_b_sync = timestamp_rise;
				//go to azimuth b laser detect
				nextstate = 4;
				//printf("state transition: %d to %d\n",state,nextstate);
			}
			else if (pulse_type == AZ_SKIP){
				//stay in this state 
				nextstate = 2;
			}
			else{
				nextstate = 0;
				//printf("state fail. State %d, Pulse Type: %d \n",state,pulse_type);
			}
			break;
		}
	
		// Azimuth A laser sweep
		case 3: {
			if(pulse_type == LASER) {
				//lighthouse a laser
				azimuth_a_laser = timestamp_rise;
				//go to azimuth b laser detect
				nextstate = 0;
				//filter out pulses that have changed by more than 10 degrees (4630 ticks)
				if(last_delta_a > 0 && abs(((int)(azimuth_a_laser-azimuth_a_sync))-(int)last_delta_a)<4630){
					printf("az A: %d, %d\n",azimuth_a_sync,azimuth_a_laser);
					send_lh_packet(azimuth_a_sync,azimuth_a_laser,A,AZIMUTH);
				}
				last_delta_a = azimuth_a_laser-azimuth_a_sync;
			}else if (pulse_type == AZ) {
				//go to first pulse AZ state
				nextstate = 1;
				azimuth_a_sync = timestamp_rise;
			}else if (pulse_type == AZ_SKIP){
				//go to az b first pulse state
				nextstate = 2;
			}
			else{
				nextstate = 0;
				//printf("state fail. State %d, Pulse Type: %d \n",state,pulse_type);
			}
			break;
		}
		
		// Azimuth B laser sweep
		case 4: {
			if(pulse_type == LASER) {
				//lighthouse b laser
				azimuth_b_laser = timestamp_rise;
				//go to azimuth b laser detect
				nextstate = 0;
				
				//filter out pulses that have changed by more than 10 degrees (4630 ticks)
				if(last_delta_b > 0 && abs(((int)(azimuth_b_laser-azimuth_b_sync))-(int)last_delta_b)<4630){
					printf("az B: %d, %d, %d\n",azimuth_b_sync,azimuth_b_laser,azimuth_b_laser-azimuth_b_sync );
					send_lh_packet(azimuth_b_sync,azimuth_b_laser,B,AZIMUTH);
				}
				last_delta_b = azimuth_b_laser-azimuth_b_sync;
			}
			else if (pulse_type == AZ) {
				//go to first pulse AZ state
				nextstate = 1;
				azimuth_a_sync = timestamp_rise;
			}else if (pulse_type == AZ_SKIP){
				//go to az b first pulse state
				nextstate = 2;
			}
			else{
				nextstate = 0;
				//printf("state fail. State %d, Pulse Type: %d \n",state,pulse_type);
			}
			break;
		}		
	}
	
	state = nextstate;

}

unsigned int sync_pulse_width_compensate(unsigned int pulse_width){
	static unsigned int sync_widths[60];
	unsigned int avg; //average sync pulse width in 10 MHz ticks
	int i;
	unsigned int sum = 0;
	
	static unsigned int sync_count = 0;
	
	//if it's a sync pulse, add to average pulse width
	if(pulse_width > 585 && pulse_width < 700){
		sync_widths[sync_count%60] = pulse_width;
		sync_count++;
	}
	
	for(i = 0; i < 60; i++){
		sum+=sync_widths[i];
	}
	
	avg = sum/60;
	
	printf("avg: %d\n",avg);
	return 	avg;
}
