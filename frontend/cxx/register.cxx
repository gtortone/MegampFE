#include <iostream>
#include <string>
#include <vector>

#include "register.h"

std::vector<Register> regs = {
   { SETTINGS_TREE + "DAC offset",                          0x01,    BOR | SOF         },
   { SETTINGS_TREE + "ADC frame phase shift delay",         0x02,    BOR | SOF         },
   { SETTINGS_TREE + "ADC data phase shift delay",          0x03,    BOR | SOF         },
   { SETTINGS_TREE + "Time conversion",                     0x05,    BOR | SOF         },
   { SETTINGS_TREE + "LMK control register high",           0x10,    BOR | SOF         },
   { SETTINGS_TREE + "LMK control register low",            0x11,    BOR | SOF         },
   { SETTINGS_TREE + "ADC control register high",           0x13,    BOR | SOF         },
   { SETTINGS_TREE + "ADC control register low",            0x14,    BOR | SOF         },
   { SETTINGS_TREE + "Megamp hold delay",                   0x17,    BOR | SOF         },
   { SETTINGS_TREE + "Megamp hold start",                   0x18,    BOR | SOF         },
   { SETTINGS_TREE + "Megamp mux frequency",                0x19,    BOR | SOF         },
   { SETTINGS_TREE + "Megamp data latency",                 0x1A,    BOR | SOF         },
   { SETTINGS_TREE + "Number of samples per channel",       0x1B,    BOR | SOF         },
   { SETTINGS_TREE + "Frequency meter scale",               0x1C,    BOR | SOF | SCAN  },
   { SETTINGS_TREE + "Software trigger",                    0x20,    SCLR              },
   { SETTINGS_TREE + "Reset FIFO",                          0x21,    SCLR              },
   { VARIABLES_TREE + "Remove Me",                          0x21,    BOR | SOF         },
};

uint16_t GetRegAddress(std::string lbl) {
   
   for(auto i=regs.begin(); i != regs.end(); i++) {
      if( (*i).label.compare(lbl) == 0 )
         return (*i).addr;
   }

   return -1; 
}

std::vector<Register> GetRegs(const uint8_t flag) {

   std::vector<Register> matches;

   for(auto i=regs.begin(); i != regs.end(); i++) {
      if( ((*i).flags & flag) == flag )
         matches.push_back(*i);
   }

   return(matches);
}
