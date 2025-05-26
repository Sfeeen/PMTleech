# PMT leech
#### A Parellel Memory Transactions monitor

PMT leech is a tool which can help you to view the memory addresses and values accessed by a CPU to an external parallel memory chip. 
By attaching the PMTLeech with a clipper to your memorychip and providing our own clock signal to the CPU we can live stream the transactions
that happen. Being in control over the clock signal allows us to also pause and step through the firmware. 

Generally this tool can be mainly used for older electronics because of the two main requirements:
- A parallel memory chip must be used
- The CPU must run on an external clock source

# Working concept:
todo add reset connection
<img src="/PMTLeech_concept.drawio.png?raw=true" alt="3D view of PMT leech PCB" title="PMT leech PCB" width="500"/>

# Software:
<img src="/PMTLeech_software.png?raw=true" alt="3D view of PMT leech PCB" title="PMT leech PCB" width="1000"/>

### Software features:
- Configuration of your parallel memory chip
- Live recording of all transactions at up to 70Khz
- Stepping through a recording with color highlighting
- Perform Instruction step(s) or Clock step(s) (An instruction can be 1 or more clock cycles)
- Also view/record foreign packets: reads/write when the /CS of the chip you attach to was not low.
- Read/Write your whole memory chip out circuit (with the socket on PMT Leech)

# Demo:
(to be added)

# Total requirements:
- A parallel memory chip is used
  - with max. 8 data bits
  - with max. 18 address lines
  - works on 5V (330R series resistance protection if not the case)
- CPU
  - Uses external clock signal as its main clock source
  - works on 5V (330R series resistance protection if not the case)
- PCB
  - Has no external interactions which are time critical. The CPU will be underclocked. Example: if the CPU has a 8Mhz clock and uses UART at 9600baud, the PMT Leech is configured to provide a 50Khz clock so the actual UART is at 60baud or doesn't work
  
# In circuit clippers:
(add link)

<img src="clipper.jpg?raw=true" alt="3D view of PMT leech PCB" title="PMT leech PCB" width="500"/>


# External Parallel memory types:
- EEPROM
- EPROM
- SRAM : PMT Leech is extra usefull PCB's where the firmware is programmed in SRAM. This was done often in the early days because SRAM is much faster than (E)EPROM but it is volatile so you can not (easily) read this memory using a chip reader
- ...

# Example use cases
- An old HMI has a password in its UI to use the program. The firmware inside the HMI is programmed in a SRAM memory chip. You attach the PMT Leech, record the transactions during the input and verification of the password. You might see the password appear in the hexviewer or if you know the CPU type you might decode the instructions and interpret the perform logic. (add image)
- Controlled brownout glitching during critical steps of firmware execution (follow: https://github.com/Sfeeen/Siemens-Advanced-Operator-Panel) 

# Licensing:
(to be added)

# Buying / Building the hardware
(to be added) / mailto: svenonderbeke %at% hotmail %dot% com
# Development: PMT Leech dev. board 
<img src="/hardware_development/3Dview_PMT_leech.png?raw=true" alt="3D view of PMT leech PCB" title="PMT leech PCB" width="1000"/>
