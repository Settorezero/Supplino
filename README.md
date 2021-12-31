# Supplino

THIS IS STILL A WORK IN PROGRESS

Supplino is a "Quick & Dirty" PSU (Power Supply Unit) made with some widely available breakout boards, an Arduino Nano and a graphical LCD.

Power output is given by a DC/DC buck converter based on the XL4016E1 IC. Regarding the power input, we prefer feeding it through an external 20÷30V brick PSU such as the ones used for laptops or old printers: this solution is safe, but you can choose to use a larger enclosure and then include internally your own transformer+diode bridge+capacitors. Also the power regulation module can be changed.

Arduino does not manage the buck converter module but only measures output voltage and current (through a current sensor) and then shows those values on a display. Power value is showed too. An additional analog gauge can be configured for showing voltage, current or power value giving some retro style to the graphics.

Power output is feed through a relay, so Arduino detaches the output power sensing an external pushbutton or on alarm events (short circuit, over-load, over-voltage).

We choosed this configuration since is cheap, simple and fully adaptable to other kinds of voltage converter designs, so the final user can choose whatever he want, also based on old-style linear voltage regulators such as the LM317 or LM338K.

We named it Supplino since we like a lot an italian snack typical of the roman cuisine, called [Supplì](https://en.wikipedia.org/wiki/Suppl%C3%AC) and because is the contraption of "Supply" and "Arduino".

### Enclosure

We've designed 2 different enclosures. They're both based on the [Ultimate Box Maker](https://www.thingiverse.com/thing:1264391) by "HeartMan" on Thingiverse and are made for modules we used and listed in the "required parts" paragraph.

You can download and customize the original enclosure project from Thingiverse if you want to make your own enclosure. See [/cad/stl folder](/cad/stl) for further informations on provided STLs to be 3Dprinted.

### Schematic

![schematic](/docs/supplino_schematic.png)
 
### Required Parts

Following links contains an affiliation code for Italian Users so we can earn money if you buy something from following links (only valid for Amazon.it)

Internal parts:
- [1.8" 128x160 Display](https://amzn.to/3pBmids)
- [8bit Level Shifter - TXS0108E](https://amzn.to/3DoPg4V)
- [Current sensor 20A - ACS712](https://amzn.to/3osdSWe)
- [DC/DC Buck converter - LM2596s](https://amzn.to/3Ghmcyd)
- [DC/DC Buck converter XH-M401 - XL4016E1](https://amzn.to/3doaTaZ)
- [Relay Module](https://amzn.to/31yBUpw)
- [Arduino Nano](https://amzn.to/3rADJxe)

External/Panel mount parts:
- [Pushbutton](https://amzn.to/31wBQ9O)
- [Multi-turn 50K potentiometer](https://amzn.to/3ps1PHH)(choose 50K from the list)
- [Toggle Switch](https://amzn.to/3lFTNtJ)
- [Banana socket](https://amzn.to/3opLQuq)
- [Panel Barrel jack socket 5.5x2.1](https://amzn.to/3IrCOW2)
- Panel Fuse-Holder + fuse based on your needs

Other parts
- [M3 Brass inserts](https://amzn.to/3EF1RlO)
