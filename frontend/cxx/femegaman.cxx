#include <stdio.h>
#include <zmq.hpp>
#include <xmlrpc-c/girerr.hpp>
#include <iostream>
#include <valarray>
#include <exception>

#include "midas.h"
#include "odbxx.h"
#include "msystem.h"
#include "mfe.h"
#include "evutils.h"
#include "register.h"
#include "rcclient.h"

//#define DEBUG

/*-- Globals -------------------------------------------------------*/

/* The frontend name (client name) as seen by other MIDAS clients   */
const char *frontend_name = "MegaManFE";
/* The frontend file name, don't change it */
const char *frontend_file_name = __FILE__;

/* frontend_loop is called periodically if this variable is TRUE    */
BOOL frontend_call_loop = FALSE;

/* a frontend status page is displayed with this frequency in ms */
INT display_period = 0;

/* maximum event size produced by this frontend */
INT max_event_size = 10000;

/* maximum event size for fragmented events (EQ_FRAGMENTED) */
INT max_event_size_frag = 5 * 1024 * 1024;

/* buffer size to hold events */
INT event_buffer_size = 100 * 10000;

/* number of Megamp modules */
unsigned short int nmod = 8;

/* number of samples per channel */
unsigned short int nsam = 5;

#define NUM_THREADS	4

struct _pdata {
   int index;
};

struct _pdata pdata[NUM_THREADS];

zmq::context_t context(1);

midas::odb o;
RuncontrolClient rc("http://lxconga01.na.infn.it:4242/RPC2");

/*-- Function declarations -----------------------------------------*/

INT proxy_thread(void *);
INT trigger_thread(void *);
INT handle_runcontrol(char *, INT);
void equip_data_init(void);
void equip_data_begin_of_run(void);
void equip_ctrl_init(void);
void equip_ctrl_begin_of_run(void);

/*-- Equipment list ------------------------------------------------*/

EQUIPMENT equipment[] = {
      {"MegampManager%02d-data",  /* equipment name */
      {  
         1, 0,                   /* event ID, trigger mask */
         "SYSTEM",               /* event buffer */
         EQ_USER,                /* equipment type */
         0,                      /* event source (not used) */
         "MIDAS",                /* format */
         TRUE,                   /* enabled */
         RO_RUNNING,             /* read only when running */
         500,                    /* poll for 500ms */
         0,                      /* stop run after this event limit */
         0,                      /* number of sub events */
         0,                      /* don't log history */
         "", "", "",},
         NULL,                   /* readout routine */
      },
      {"MegampManager%02d-ctrl",    /* equipment name */
      {  
         100, 0,                /* event ID, trigger mask */
         "SYSTEM",               /* event buffer */
         EQ_PERIODIC,            /* equipment type */
         0,                      /* event source (not used) */
         "MIDAS",                /* format */
         TRUE,                   /* enabled */
         RO_ALWAYS,              /* read always */
         2000,                   /* poll for 2000ms */
         0,                      /* stop run after this event limit */
         0,                      /* number of sub events */
         1,                      /* log history */
         "", "", "",},
         handle_runcontrol,      /* readout routine */
      },
    {""}
};

/* ZMQ proxy thread: frontend(SUB) - backend(PUSH) */

INT proxy_thread(void *param) {

   zmq::socket_t frontend(context, zmq::socket_type::sub);
   frontend.connect("tcp://lxconga01.na.infn.it:5000");
   frontend.setsockopt(ZMQ_SUBSCRIBE, "", 0);

   zmq::socket_t backend(context, zmq::socket_type::push);
   backend.bind("inproc://events");

   zmq_proxy(frontend, backend, NULL);

   return 0;
}

/* DATA equipment init */

void equip_data_init(void) {

   /* create ZMQ proxy thread */
   printf("Create ZMQ proxy thread...\n");
   ss_thread_create(proxy_thread, NULL);

   for (int i=0; i<NUM_THREADS; i++) {
      
      pdata[i].index = i;

      /* create a ring buffer for each thread */
      create_event_rb(i);

      /* create readout thread */
      ss_thread_create(trigger_thread, &pdata[i]);
   }
}

/* DATA equipment begin_of_run */

void equip_data_begin_of_run(void) {
   
   // read from ODB...
   nmod = 8;
   nsam = o["Settings"]["Number of samples per channel"];
}

/* CTRL equipment init */

void equip_ctrl_init(void) {
   
   std::string odb_base = std::string("/Equipment/") + std::string(equipment[1].name);

   o.connect(odb_base);

   // initialize ODB
   auto rlist = GetRegs();
   for(auto i=rlist.begin(); i != rlist.end(); i++)
      o[(*i).subtree][(*i).label] = 0;
   
   // read FPGA registers with SOF flag - write to ODB
   auto soflist = GetRegs(SOF);
   for(auto i=soflist.begin(); i != soflist.end(); i++) {
      try {
         int value = rc.readReg((*i).addr);
         o[(*i).subtree][(*i).label] = value;
      } catch (girerr::error &e) {
         cm_msg(MERROR, "rc", "Run Control RPC error");
      } 
   }

   // update number of samples per channel
   nsam = o["Settings"]["Number of samples per channel"];
}

/* CTRL equipment begin of run */

void equip_ctrl_begin_of_run(void) {

   // read from ODB - write to FPGA registers with BOR
   auto borlist = GetRegs(BOR);
   for(auto i=borlist.begin(); i != borlist.end(); i++) {
      try {
         rc.writeReg((*i).addr, o[(*i).subtree][(*i).label]);
      } catch (girerr::error &e) {
         cm_msg(MERROR, "rc", "Run Control RPC error");
      }
   }
}

/*-- Frontend Init -------------------------------------------------*/

INT frontend_init() {

   set_equipment_status(equipment[0].name, "Initialized", "#ffff00");
   set_equipment_status(equipment[1].name, "Running", "#00ff00");

   equip_ctrl_init();
   equip_data_init();

   if(run_state == STATE_RUNNING) 
      set_equipment_status(equipment[0].name, "Started run", "#00ff00");

   return SUCCESS;
}

/*-- Frontend Exit -------------------------------------------------*/

INT frontend_exit() {

   stop_readout_threads();

   return SUCCESS;
}

/*-- Begin of Run --------------------------------------------------*/

INT begin_of_run(INT run_number, char *error) {

   equip_ctrl_begin_of_run();
   equip_data_begin_of_run();

   set_equipment_status(equipment[0].name, "Started run", "#00ff00");

   return SUCCESS;
}

/*-- End of Run ----------------------------------------------------*/

INT end_of_run(INT run_number, char *error) {

   set_equipment_status(equipment[0].name, "Ended run", "#00ff00");

   return SUCCESS;
}

/*-- Pause Run -----------------------------------------------------*/

INT pause_run(INT run_number, char *error) {

   set_equipment_status(equipment[0].name, "Paused run", "#ffff00");

   return SUCCESS;
}

/*-- Resume Run ----------------------------------------------------*/

INT resume_run(INT run_number, char *error) {

   set_equipment_status(equipment[0].name, "Started run", "#00ff00");

   return SUCCESS;
}

/*-- Frontend Loop -------------------------------------------------*/

INT frontend_loop() {

   /* if frontend_call_loop is true, this routine gets called when
      the frontend is idle or once between every event */
   return SUCCESS;
}

/*-- Trigger event routines ----------------------------------------*/

INT poll_event(INT source, INT count, BOOL test) {

   /* Polling is not used */
   return 0;
}

/*-- Interrupt configuration ---------------------------------------*/

INT interrupt_configure(INT cmd, INT source, POINTER_T adr) {

   /* Interrupts are not used */
   return SUCCESS;
}

/*-- Event readout -------------------------------------------------*/

INT trigger_thread(void *param) {

   EVENT_HEADER *pevent;
   WORD *pdata, *pmegamp;
   int  index, status;
   INT rbh;

   struct _pdata *tdata = (struct _pdata *) param;

   zmq::socket_t s(context, zmq::socket_type::pull);
   s.connect("inproc://events");

   /* index of this thread */
   index = (int)tdata->index;

   /* tell framework that we are alive */
   signal_readout_thread_active(index, TRUE);
   
   /* Initialize hardware here ... */
   printf("Start readout thread %d\n", index);
   
   /* Obtain ring buffer for inter-thread data exchange */
   rbh = get_event_rbh(index);

   while (is_readout_thread_enabled()) {

      if(run_state == STATE_RUNNING) {

         /* obtain buffer space */
         status = rb_get_wp(rbh, (void **)&pevent, 0);
         if (!is_readout_thread_enabled())
            break;
         if (status == DB_TIMEOUT) {
            ss_sleep(10);
            continue;
         }
         if (status != DB_SUCCESS)
            break;
         
         /* check for new event (poll) */
         
         if (status) { // if event available, read it out
            
            if (!is_readout_thread_enabled())
               break;

            zmq::message_t message{};
            s.recv(message, zmq::recv_flags::none);

            RawEvent event_raw(reinterpret_cast<unsigned short int*>(message.data()), message.size()/2);

            // check event: size too short, EC mismatch, LEN error, CRC error

            bm_compose_event(pevent, 1, 0, 0, equipment[0].serial_number++);
            pdata = (WORD *)(pevent + 1);
            
            /* init bank structure */
            bk_init(pdata);
            
            // create a bank for each Megamp and fill each bank with channels and samples
            unsigned int time = GetTime(event_raw);
            for(int m=0; m<nmod; m++) {
               std::stringstream bankname; 
               bankname << "M00" << m;
               bk_create(pdata, bankname.str().c_str(), TID_WORD, (void **)&pmegamp);
               *pmegamp++ = GetEC(event_raw);

               *pmegamp++ = (time & 0xFFFF0000) >> 16;
               *pmegamp++ = (time & 0x0000FFFF);
               *pmegamp++ = nmod;
               *pmegamp++ = nsam;
               for(int c=0; c<16; c++) {
                  *pmegamp++ = c;
                  std::vector<unsigned short int> chdata = GetChannelSamples(event_raw, m, c, nsam);
                  std::copy(begin(chdata), end(chdata), pmegamp);
                  pmegamp += chdata.size();
               }
               bk_close(pdata, pmegamp);
            }  // end for

            pevent->data_size = bk_size(pdata);
            
            /* send event to ring buffer */
            rb_increment_wp(rbh, sizeof(EVENT_HEADER) + pevent->data_size);
         }

      } else {
         
         // sleep to relax CPU usage
         sleep(1);

      } // end if run_state
   }
   
   /* tell framework that we are finished */
   signal_readout_thread_active(index, FALSE);
   
   printf("Stop readout thread %d\n", index);

   return 0;
}

INT handle_runcontrol(char *pevent, INT off) {

   // read FPGA registers with SCAN flag - write to ODB
   auto scanlist = GetRegs(SCAN);
   for(auto i=scanlist.begin(); i != scanlist.end(); i++) {
      try {
         int value = rc.readReg((*i).addr);
         o[(*i).subtree][(*i).label] = value;
      } catch (girerr::error &e) {
         cm_msg(MERROR, "rc", "Run Control RPC error");
      }
   }

   // check SCLR registers: if value is 1 set again to 0
   // and set FPGA register (only when frontend is running)
   auto sclrlist = GetRegs(SCLR);
   for(auto i=sclrlist.begin(); i != sclrlist.end(); i++) {
      if(o[(*i).subtree][(*i).label] != 0) {
         if (run_state != STATE_RUNNING) {
            auto addr = GetRegAddress((*i).label);
            rc.writeReg(addr, 1);
            rc.writeReg(addr, 0);
         } else {
            cm_msg(MERROR, "rc", "Register not available in running mode");
         }
      }
      o[(*i).subtree][(*i).label] = 0;
   }

   return 0;
}
