/* tuxctl-ioctl.c
 *
 * Driver (skeleton) for the mp2 tuxcontrollers for ECE391 at UIUC.
 *
 * Mark Murphy 2006
 * Andrew Ofisher 2007
 * Steve Lumetta 12-13 Sep 2009
 * Puskar Naha 2013
 */

#include <asm/current.h>
#include <asm/uaccess.h>

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/fs.h>
#include <linux/sched.h>
#include <linux/file.h>
#include <linux/miscdevice.h>
#include <linux/kdev_t.h>
#include <linux/tty.h>
#include <linux/spinlock.h>


#include "tuxctl-ld.h"
#include "tuxctl-ioctl.h"
#include "mtcp.h"


#define BUTTON_LEFT 0x02
#define BUTTON_DOWN 0x04
#define UP_AND_RIGHT 0x09



#define debug(str, ...) \
	printk(KERN_DEBUG "%s: " str, __FUNCTION__, ## __VA_ARGS__)





 // Array of map from given index to bits that represent that index on the tux LED
unsigned char globalbuttons;
unsigned long previousled;
int handled = 0;





static spinlock_t my_lock = SPIN_LOCK_UNLOCKED;
static spinlock_t reset_lock = SPIN_LOCK_UNLOCKED;



//Declare functions used in this file

int tuxctl_ioctl_init(struct tty_struct *tty);
int tuxctl_ioctl_setled(struct tty_struct *tty, unsigned long arg);
int tuxctl_ioctl_buttons(struct tty_struct *tty, unsigned long arg);

/************************ Protocol Implementation *************************/

/* tuxctl_handle_packet()
 * IMPORTANT : Read the header for tuxctl_ldisc_data_callback() in 
 * tuxctl-ld.c. It calls this function, so all warnings there apply 
 * here as well.
 */
void tuxctl_handle_packet (struct tty_struct* tty, unsigned char* packet)
{
    unsigned a, b, c;
    int left, down, l_bits, r_bits;
  

    a = packet[0];  //Avoid printk() sign extending the 8-bit 
    b = packet[1];  //values when printing them. 
    c = packet[2];


    // printk("packet : %x %x %x\n", a, b, c); 


    switch(a){



    	case MTCP_BIOC_EVENT:

    		
    		// change the format of the indicated button pushed
    		l_bits = (UP_AND_RIGHT & c);
    		l_bits = l_bits << 4;

    		left = (BUTTON_LEFT & c);
    		left = left << 5;

    		down = (BUTTON_DOWN& c);
    		down = down << 3;

    		r_bits = (0x0F & b);

    		globalbuttons = left | down | l_bits | r_bits;

			    

    		break;

    		// handle reset by reinitializng and restoring the previous led settings
    	case MTCP_RESET:

    		

    			spin_lock(&reset_lock);
    			
    			tuxctl_ioctl_init(tty);
    			tuxctl_ioctl_setled(tty, previousled);
    			
    			spin_unlock(&my_lock);
    		
    		break;


    		// handles ack responses, does allow handled flag to enable SET LED until an ACK is received
    	case MTCP_ACK: 

    		spin_lock(&reset_lock);

    		handled = 0;

    		spin_unlock(&my_lock);
    		
    		break;
    	default: 

    		break;









    }

    // printk("packet : %x %x %x\n", a, b, c); 

   return;
}

/******** IMPORTANT NOTE: READ THIS BEFORE IMPLEMENTING THE IOCTLS ************
 *                                                                            *
 * The ioctls should not spend any time waiting for responses to the commands *
 * they send to the controller. The data is sent over the serial line at      *
 * 9600 BAUD. At this rate, a byte takes approximately 1 millisecond to       *
 * transmit; this means that there will be about 9 milliseconds between       *
 * the time you request that the low-level serial driver send the             *
 * 6-byte SET_LEDS packet and the time the 3-byte ACK packet finishes         *
 * arriving. This is far too long a time for a system call to take. The       *
 * ioctls should return immediately with success if their parameters are      *
 * valid.                                                                     *
 *                                                                            *
 ******************************************************************************/
int 
tuxctl_ioctl (struct tty_struct* tty, struct file* file, 
	      unsigned cmd, unsigned long arg)
{
    switch (cmd) {
	case TUX_INIT:
		return tuxctl_ioctl_init(tty);
	
	case TUX_BUTTONS:
		return tuxctl_ioctl_buttons(tty, arg);
		
	case TUX_SET_LED:

		if(handled == 0){
		 
		 handled = 1;
		 return tuxctl_ioctl_setled(tty, arg);
		 
		}
		else
			return -EINVAL;
		
	/*case TUX_LED_ACK:
		
	case TUX_LED_REQUEST:
		
	case TUX_READ_LED:*/
	
	default:
	    return -EINVAL;
    }
}

/*
 * tuxctl_ioctl_init
 *   DESCRIPTION: Enables interrupts and sets LED to accept User input
 *   INPUTS: none
 *   OUTPUTS: MTCP_BIOC_ON and MTCP_LED_USR
 *   RETURN VALUE: Zero
 *   SIDE EFFECTS: none
 */


int 
tuxctl_ioctl_init(struct tty_struct *tty){
	int n, m;
    char p[1];
	char t[1];
	

	
	p[0] = MTCP_BIOC_ON;
	t[0] = MTCP_LED_USR;
	n = tuxctl_ldisc_put(tty, p, 1);

	m = tuxctl_ldisc_put(tty, t, 1);

	//printk("Initialized");
	

	return 0;
 

}

/*
 * tuxctl_ioctl_setled
 *   DESCRIPTION: Takes a 32 bit value and converts it to 7 segment display
 *   INPUTS: 32 bit value with specifications for the TUX 7 segment display
 *   OUTPUTS: Puts number on the tux controller led display
 *   RETURN VALUE: none
 *   SIDE EFFECTS: none
 */

int
tuxctl_ioctl_setled(struct tty_struct *tty, unsigned long arg){

	unsigned int number, decimals;
	int hexnum[4];
	char leds;
	int i, count;
	char packet[6];

	char temp;

	char numbermap[16] = {0xE7, 0x06, 0xCB, 0x8F, 0x2E, 0xAD, 0xED, 0x86 ,0xEF, 0xAF, 0xEE, 0x6D, 0xE1, 0x4F, 0xE9, 0xE8};

	previousled = arg;


	

	number = (0xFFFF & arg);


	// push hex values for leds into an array
	// printk("%x \n", number);
	hexnum[0] = (0xF & number);
	hexnum[1] = (0xF & (number >> 4));
	hexnum[2] = (0xF & (number >> 8));
	hexnum[3] = (0xF & (number >> 12));

	// printk("%x \n %x \n %x \n %x \n", hexnum[0], hexnum[1], hexnum[2], hexnum[3]);
	






	leds = ((0x0F << 16) & arg) >> 16;

	
	

	decimals = ((0x0F << 24) & arg) >> 24;

	packet[0]= MTCP_LED_SET;
	//previousled[0] = MTCP_LED_SET;	

	if(leds == 0x07){

		packet[1] = 0x07;

		count = 5;

		
	}
	else{
		packet[1] = 0xFF;
		//previousled[1] = 0xFF;
		count = 6;
	}
	
	

	
	// printk("%x \n %x \n %x \n %x \n", leds, decimals, packet[0], packet[1]);

	for( i = 2; i < count ; i++){

		temp = numbermap[hexnum[i - 2]];
		packet[i] = temp;
		//previousled[i] = temp;
		if(decimals % 2){
			packet[i] = (packet[i] | 0x10);
			//previousled[i] = (previousled[i]| 0x10);
		}

		decimals = decimals >> 1;

	}


	




	

		

	

    tuxctl_ldisc_put(tty, packet, count);


	
	
	return 0;

}








/*
 * tuxctl_ioctl_buttons
 *   DESCRIPTION: Acquires which buttons are being pressed
 *   INPUTS: Pointer to an integer value
 *   OUTPUTS: bits of which button was pushed
 *   RETURN VALUE: zero on success, -EINVAL on failure
 *   SIDE EFFECTS: none
 */



int
tuxctl_ioctl_buttons(struct tty_struct *tty, unsigned long arg){
	int ret;
	int buttons;
	if ( (int*)arg == NULL)
		return -EINVAL;

	buttons = globalbuttons;

	globalbuttons = 0x00; // reset global buttons so it doesn't affect anything uninentionally

	

	ret = copy_to_user((int*)arg, &buttons, sizeof(buttons));

	if(ret > 0)
		return -EINVAL;
	else
		return 0;










}

