#include "optical.h" 
 
// ISRs for external interrupts
void ext_gpio3_activehigh_debounced_isr(){
	printf("External Interrupt GPIO3 triggered\r\n");
}
void ext_gpio8_activehigh_isr(){
	//printf("External Interrupt GPIO8 triggered\r\n");
	optical_sfd_isr(); // call the optical interrupt for 3wb programming // TODO 3wb
}
void ext_gpio9_activelow_isr(){
	printf("External Interrupt GPIO9 triggered\r\n");
}
void ext_gpio10_activelow_isr(){
	printf("External Interrupt GPIO10 triggered\r\n");
}
