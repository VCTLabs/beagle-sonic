/*
Copyright 2015 Google Inc. All Rights Reserved.

Licensed under the Apache License, Version 2.0 (the "License");
you may not use this file except in compliance with the License.
You may obtain a copy of the License at

   http://www.apache.org/licenses/LICENSE-2.0

Unless required by applicable law or agreed to in writing, software
distributed under the License is distributed on an "AS IS" BASIS,
WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or
implied.  See the License for the specific language governing
permissions and limitations under the License.
*/

/*
Example code for capturing samples from PRUDAQ ADC cape.
Loads .bin files into both PRUs, then reads from the shared
buffer in main memory.
*/

#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <inttypes.h>
#include <libgen.h>
#include <string.h>
#include <fcntl.h>
#include <prussdrv.h>
#include <pruss_intc_mapping.h>
#include <zmq.h>
#include <sys/poll.h>
#include <signal.h>
#include <time.h>

// Header for sharing info between PRUs and application processor
#include "shared_header.h"


// Used by sig_handler to tell us when to shutdown
static int bCont = 1;

// The PRUs run at 200MHz
#define PRU_CLK 200e6

#define ZMQ_HOST                "tcp://*:5555"


//Set freq in pwm module
void set_freq(float freq_hz){
   FILE *fp;
   fp=fopen("/sys/devices/platform/ocp/48300000.epwmss/48300200.ehrpwm/pwm/pwmchip0/pwm0/period","w+");
   if (fp == NULL){
     perror("GPIO: write failed to open file set-freq");
     return;
   }
   float period_s = 1.0f/freq_hz;
   unsigned int period_ns=(unsigned int)period_s*1000000000;
   fprintf(fp,"%u",period_ns);
   fclose(fp);
   return;
}
//------set duty cycle in nanoseconds,should not be greater than period----
void set_DutyCycle(unsigned int duty_ns){
   FILE *fp;
   fp=fopen("/sys/devices/platform/ocp/48300000.epwmss/48300200.ehrpwm/pwm/pwmchip0/pwm0/duty","w+");
   if (fp == NULL){
     perror("GPIO: write failed to open file set_duty");
     return;
   }
   fprintf(fp,"%u",duty_ns);
   fclose(fp);
   return;
}
//works only when freq is 40kHz
void set_DutyCycle_40(float percentage){
   FILE *fp;
   fp=fopen("/sys/devices/platform/ocp/48300000.epwmss/48300200.ehrpwm/pwm/pwmchip0/pwm0/duty","w+");
   if (fp == NULL){
     perror("GPIO: write failed to open file  freq");
     return;
   }
   if ((percentage>100.0f)||(percentage<0.0f)){
     perror("enter value between 0 to 100");
     return;
   }
   float duty=25000*(percentage/100.0f);
   unsigned int duty_ns=(unsigned int)duty;
   fprintf(fp,"%u",duty_ns);
   fclose(fp);
   return;
}

void pwm_enable(){
  FILE *fp;
   fp=fopen("/sys/devices/platform/ocp/48300000.epwmss/48300200.ehrpwm/pwm/pwmchip0/pwm0/enable","w+");
   if (fp == NULL){
     perror("GPIO: write failed to open file enable pwm");
     return;
   }
  fprintf(fp,"%d",1);
  fclose(fp);
  return;

}
void pwm_disable(){
  FILE *fp;
   fp=fopen("/sys/devices/platform/ocp/48300000.epwmss/48300200.ehrpwm/pwm/pwmchip0/pwm0/enable","w+");
   if (fp == NULL){
     perror("GPIO: write failed to open file disable pwm ");
     return;
   }
  fprintf(fp,"%d",0);
  fclose(fp);
  return;

}

void sig_handler (int sig) {
  // break out of reading loop
  bCont = 0;
  return;
}

void usage (char* arg0) {
  fprintf(stderr, "\nUsage: %s [flags] pru0_code.bin pru1_code.bin\n",
          basename(arg0));

  fprintf(stderr, "\n"
          "  -f freq\t gpio based clock frequency (default: 1000)\n"
          "  -i [0-3]\t channel 0 input select\n"
          "  -q [4-7]\t channel 1 input select\n"
          "  -o output\t output filename (default: stdout)\n\n"
         );
  exit(EXIT_FAILURE);
}


int main (int argc, char **argv) {
  int ch = -1;
  double gpiofreq = 1000;
  int channel0_input = 0;
  int channel1_input = 4;
  char* fname = "-";
  FILE* fout = stdout;
  void *context = zmq_ctx_new ();
  void* publisher = zmq_socket( context, ZMQ_PUB );
  zmq_bind( publisher, ZMQ_HOST );

  // Make sure we're root
  if (geteuid() != 0) {
    fprintf(stderr, "Must be root.  Try again with sudo.\n");
    return EXIT_FAILURE;
  }

  // Process command line flags
  while (-1 != (ch = getopt(argc, argv, "f:i:q:o:"))) {
    switch (ch) {
    case 'f':
      gpiofreq = strtod(optarg, NULL);
      break;
    case 'i':
      channel0_input = strtol(optarg, NULL, 0);
      if (channel0_input < 0 || channel0_input > 3) {
        fprintf(stderr, "\n-i value must be between 0 and 3\n");
        usage(argv[0]);
      }
      break;
    case 'q':
      channel1_input = strtol(optarg, NULL, 0);
      if (channel1_input < 4 || channel1_input > 7) {
        fprintf(stderr, "\n-q value must be between 4 and 7\n");
        usage(argv[0]);
      }
      break;
    case 'o':
      fname = optarg;
      break;
    default:
      usage(argv[0]);
      break;
    }
  }

  if (argc - optind != 2) {
    usage(argv[0]);
    return EXIT_FAILURE;
  }

  argc -= optind;
  argv += optind;

  if (0 != strcmp(fname, "-")) {
    fout = fopen(fname, "w");
    if (NULL == fout) {
      perror("unable to open output file");
    }
  }

  // Install signal handler to catch ctrl-C
  if (SIG_ERR == signal(SIGINT, sig_handler)) {
    perror("Warn: signal handler not installed %d\n");
  }

  

  //start pwm module
  //P9_42 MUST be loaded as a slot before use
  set_freq(40000);//supplied in Hz
  //supplied in percentage value of period defined by frequency set above
  set_DutyCycle_40(50.00);
  //run pwm
  pwm_enable();







  // This segfaults if we're not root.
  prussdrv_init();
  if (0 != prussdrv_open(PRU_EVTOUT_0)) {
    fprintf(stderr,
            "prussdrv_open() failed. (Did you forget to run setup.sh?)\n");
    return EXIT_FAILURE;
  }

  tpruss_intc_initdata pruss_intc_initdata = PRUSS_INTC_INITDATA;
  prussdrv_pruintc_init(&pruss_intc_initdata);

  // Get pointer into the 8KB of shared PRU DRAM where prudaq expects
  // to share params with prus and the main cpu
  volatile pruparams_t *pparams = NULL;
  prussdrv_map_prumem(PRUSS0_SHARED_DATARAM, (void**)&pparams);

  // Pointer into the DDR RAM mapped by the uio_pruss kernel module.
  volatile uint32_t *shared_ddr = NULL;
  prussdrv_map_extmem((void**)&shared_ddr);
  unsigned int shared_ddr_len = prussdrv_extmem_size();
  unsigned int physical_address = prussdrv_get_phys_addr((void*)shared_ddr);

  // Accessing the shared memory is slow, so later we'll efficiently copy it out
  // into this local buffer.
  uint32_t *local_buf = (uint32_t *) malloc(shared_ddr_len);
  if (!local_buf) {
    fprintf(stderr, "Couldn't allocate memory.\n");
    return EXIT_FAILURE;
  }

  fprintf(stderr,
          "%uB of shared DDR available.\n Physical (PRU-side) address:%x\n",
         shared_ddr_len, physical_address);
  fprintf(stderr, "Virtual (linux-side) address: %p\n\n", shared_ddr);
  if (shared_ddr_len < 1e6) {
    fprintf(stderr, "Shared buffer length is unexpectedly small.  Buffer overruns"
            " are likely at higher sample rates.  (Perhaps extram_pool_sz didn't"
            " get set when uio_pruss kernel module loaded.  See setup.sh)\n");
  }

  // We'll use the first 8 bytes of PRU memory to tell it where the
  // shared segment of system memory is.
  pparams->physical_addr = physical_address;
  pparams->ddr_len       = shared_ddr_len;

  // Calculate the GPIO clock high and low cycle counts.
  // Adding 0.5 and truncating is equivalent to rounding
  int cycles = (PRU_CLK/gpiofreq + 0.5);
  fprintf(stderr, "Actual GPIO clock speed is %.2fHz\n", PRU_CLK/((float)cycles));

  if (cycles < 12) {
    fprintf(stderr, "Requested frequency too high (max: 16666666)\n");
    return EXIT_FAILURE;
  }
  pparams->high_cycles = cycles/2;
  pparams->low_cycles  = cycles - pparams->high_cycles;

  if (gpiofreq > 5e6) {
    fprintf(stderr, "Sampling both channels faster than 5MSPS with prudaq_capture"
            " is likely to cause buffer overruns due to limited DMA bandwidth."
            " Consider using BeagleLogic's PRUDAQ support instead.\n");
  }

  // Decide the value that'll get written to PRU0's register r30
  // See the docs for how bits in r30 correspond to the INPUT0A/
  // INPUT0B/INPUT1A/INPUT1B control lines on the analog switches.
  uint32_t pru0r30 = 0;
  switch (channel0_input) {
    case 0: break;
    case 1: pru0r30 |= (1 << 1); break;
    case 2: pru0r30 |= (1 << 2); break;
    case 3: pru0r30 |= (1 << 1) | (1 << 2); break;
  }
  switch (channel1_input) {
    case 4: break;
    case 5: pru0r30 |= (1 << 3); break;
    case 6: pru0r30 |= (1 << 5); break;
    case 7: pru0r30 |= (1 << 3) | (1 << 5); break;
  }
  pparams->input_select = pru0r30;

  // Load the .bin files into PRU0 and PRU1
  prussdrv_exec_program(0, argv[0]);
  prussdrv_exec_program(1, argv[1]);

  uint32_t max_index = shared_ddr_len / sizeof(shared_ddr[0]);
  uint32_t read_index = 0;
  time_t now = time(NULL);
  time_t start_time = now;
  uint32_t bytes_read = 0;
  uint32_t bytes_read_temp = 0;
  int loops = 0;
  while (bCont) {
    // Reading from shared memory and PRU RAM is significantly slower than normal
    // memory, so we loop below rather than checking shared_ptr every time, and
    // we only check bytes_written once in a while.
    uint32_t *write_pointer_virtual = prussdrv_get_virt_addr(pparams->shared_ptr);
    uint32_t write_index = write_pointer_virtual - shared_ddr;

    if (read_index == write_index) {
      // We managed to loop all the way back before PRU1 wrote even a single sample.
      // Do nothing.

    } else if (read_index < write_index) {
      // Copy from the slow DMA coherent buffer to fast normal RAM
      int bytes = (write_index - read_index) * sizeof(*shared_ddr);
      memcpy(local_buf, (void *) &(shared_ddr[read_index]), bytes);
      bytes_read += bytes;

      // Each 32-bit word holds a pair of samples, one from each channel.
      // Samples are 10 bits, and the remaining bits record the clock and
      // input select state.  (See doc/InputOutput.md for details)

      // Mask off the clock and input select bits so that we output just
      // the sample data.
      for (int i = 0; i < (write_index - read_index); i++) {
        // Keep just the lower 10 bits from each 16-bit half of the 32-bit word
        local_buf[i] &= 0x03ff03ff;
      }

      fwrite(local_buf, bytes, 1, fout);
      zmq_send( publisher, local_buf, bytes, 0 );

    } else {
      // The write pointer has wrapped around, so we'll copy out the data
      // in two chunks
      int tail_words = max_index - read_index;
      int tail_bytes = tail_words * sizeof(*shared_ddr);

      memcpy(local_buf, (void *) &(shared_ddr[read_index]), tail_bytes);
      bytes_read += tail_bytes;

      for (int i = 0; i < tail_words; i++) {
        local_buf[i] &= 0x03ff03ff;
      }

      int head_bytes = write_index * sizeof(*shared_ddr);
      memcpy(&(local_buf[tail_words]), (void *) shared_ddr, head_bytes);
      bytes_read += head_bytes;

      for (int i = 0; i < write_index; i++) {
        local_buf[tail_words + i] &= 0x03ff03ff;
      }

      fwrite(local_buf, tail_bytes + head_bytes, 1, fout);
      zmq_send( publisher, local_buf, tail_bytes + head_bytes , 0 );
      
    }
    read_index = write_index;

    if (loops++ % 100 == 0) {
      time_t current_time = time(NULL);
      if (now != current_time) {
        now = current_time;
        // There's a race condition here where the PRU will often update bytes_written
        // after we checked write_index, so don't worry about small differences. If the
        // buffer overruns, we'll end up being off by an entire buffer worth.
        uint32_t bytes_written = pparams->bytes_written;
        int64_t difference = ((int64_t) bytes_written) - bytes_read;
        if (difference < 0) {
          difference = ((uint32_t) bytes_written + shared_ddr_len) -
                       ((uint32_t) bytes_read + shared_ddr_len);
        }
  
        fprintf(stderr, "\t%ld bytes / second. %uB written, %uB read.\n",
                bytes_written / (now - start_time), bytes_written, bytes_read);
      }
    }
    //since pwm module is always running, there may be condition after some time that 
    //waveforms of tx and rx starts to match since waveform is periodic and consists
    //of only one frequency, therefore to separate samples, we disable pwm and then enable again 
    //after every 4000 samples  
    if (((bytes_read/4)%2000)==0 ){
      bytes_read_temp=bytes_read + 80;
      fprintf(stderr, "enabling PWM module  %u %u",bytes_read, bytes_read_temp );
      pwm_enable();}
    
    if(bytes_read>=bytes_read_temp ){
      fprintf(stderr, "disabling PWM module \n");
      pwm_disable();}
    usleep(100);
  }

  // Wait for the PRU to let us know it's done
  //prussdrv_pru_wait_event(PRU_EVTOUT_0);
  fprintf(stderr, "All done\n");

  prussdrv_pru_disable(0);
  prussdrv_pru_disable(1);
  prussdrv_exit();

  if (stdout != fout) {
    fclose(fout);
  }

  return 0;
}
