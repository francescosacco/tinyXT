

#include "opl3_emulator.h"

// =============================================================================
// Class: OPL3_Emulator_t
//

OPL3_Emulator_t::OPL3_Emulator_t()
{
  nts = 0;
  dam = 0;
  dvb = 0;
  ryt = 0;
  bd = 0;
  sd = 0;
  tom = 0;
  tc = 0;
  hh = 0;
  _new = 0;
  connectionsel = 0;

  vibratoIndex = 0;
  tremoloIndex = 0;
  channels = new Channel[2][9];

  initOperators();
  initChannels2op();
  initChannels4op();
  initRhythmChannels();
  initChannels();
}

OPL3_Emulator_t::~OPL3_Emulator_t()
{

}

unsigned char OPL3_Emulator_t::ReadRegister(int array, int address)
{
  // The OPL3 has two registers arrays, each with addresses ranging
  // from 0x00 to 0xF5.
  // This emulator uses one array, with the two original register arrays
  // starting at 0x00 and at 0x100.

  int registerAddress = (array<<8) | address;

  // If the address is out of the OPL3 memory map, returns.
  if ((registerAddress < 0) || (registerAddress >= 0x200)) return 0xff;

  return Registers[registerAddress];
}

void OPL3_Emulator_t::WriteRegister(int array, int address, unsigned char data)
{
  // The OPL3 has two registers arrays, each with addresses ranging
  // from 0x00 to 0xF5.
  // This emulator uses one array, with the two original register arrays
  // starting at 0x00 and at 0x100.

  int registerAddress = (array<<8) | address;

  // If the address is out of the OPL3 memory map, returns.
  if ((registerAddress < 0) || (registerAddress >= 0x200)) return;

  Registers[registerAddress] = data;

  switch (address & 0xE0)
  {
    // The first 3 bits masking gives the type of the register by using its base address:
    // 0x00, 0x20, 0x40, 0x60, 0x80, 0xA0, 0xC0, 0xE0
    // When it is needed, we further separate the register type inside each base address,
    // which is the case of 0x00 and 0xA0.

    // Through out this emulator we will use the same name convention to
    // reference a byte with several bit registers.
    // The name of each bit register will be followed by the number of bits
    // it occupies inside the byte.
    // Numbers without accompanying names are unused bits.
    case 0x00:
      // Unique registers for the entire OPL3:
      if (array == 1)
      {
        if (address == 0x04)
          update_2_CONNECTIONSEL6();
        else if (address == 0x05)
          update_7_NEW1();
      }
      else if(address==0x08) update_1_NTS1_6();
      break;

    case 0xA0:
      // 0xBD is a control register for the entire OPL3:
      if (address==0xBD)
      {
        if (array == 0) update_DAM1_DVB1_RYT1_BD1_SD1_TOM1_TC1_HH1();
        break;
      }

      // Registers for each channel are in A0-A8, B0-B8, C0-C8, in both register arrays.
      // 0xB0...0xB8 keeps kon,block,fnum(h) for each channel.
      if ((address & 0xF0) == 0xB0 && address <= 0xB8)
      {
        // If the address is in the second register array, adds 9 to the channel number.
        // The channel number is given by the last four bits, like in A0,...,A8.
        channels[array][address&0x0F].update_2_KON1_BLOCK3_FNUMH2();
        break;
      }

      // 0xA0...0xA8 keeps fnum(l) for each channel.
      if ((address & 0xF0) == 0xA0 && address <= 0xA8)
          channels[array][address&0x0F].update_FNUML8();
      break;

    // 0xC0...0xC8 keeps cha,chb,chc,chd,fb,cnt for each channel:
    case 0xC0:
      if (address <= 0xC8) channels[array][address&0x0F].update_CHD1_CHC1_CHB1_CHA1_FB3_CNT1();
      break;

    // Registers for each of the 36 Operators:
    default:
      int operatorOffset = address&0x1F;
      if (operators[array][operatorOffset] == null) break;
      switch (address & 0xE0)
      {
        // 0x20...0x35 keeps am,vib,egt,ksr,mult for each operator:
        case 0x20:
          operators[array][operatorOffset].update_AM1_VIB1_EGT1_KSR1_MULT4();
          break;
        // 0x40...0x55 keeps ksl,tl for each operator:
        case 0x40:
          operators[array][operatorOffset].update_KSL2_TL6();
          break;
        // 0x60...0x75 keeps ar,dr for each operator:
        case 0x60:
          operators[array][operatorOffset].update_AR4_DR4();
          break;
        // 0x80...0x95 keeps sl,rr for each operator:
        case 0x80:
          operators[array][operatorOffset].update_SL4_RR4();
          break;
        // 0xE0...0xF5 keeps ws for each operator:
        case 0xE0:
          operators[array][operatorOffset].update_5_WS3();
          break;
      }
  }

}

void OPL3_Emulator_t::GetSamples(unsigned char *Data, int SampleRate, int NSamples)
{
  short[] output = new short[4];
  double[] outputBuffer = new double[4];
  double[] channelOutput;

  for (int outputChannelNumber = 0 ; outputChannelNumber < 4 ; outputChannelNumber++)
  {
    outputBuffer[outputChannelNumber] = 0;
  }

  // If _new = 0, use OPL2 mode with 9 channels. If _new = 1, use OPL3 18 channels;
  for (int array = 0 ; array < (_new + 1) ; array++)
  {
    for(int channelNumber=0; channelNumber < 9; channelNumber++)
    {
      // Reads output from each OPL3 channel, and accumulates it in the output buffer:
      channelOutput = channels[array][channelNumber].getChannelOutput();
      for (int outputChannelNumber = 0 ; outputChannelNumber < 4 ; outputChannelNumber++)
      {
        outputBuffer[outputChannelNumber] += channelOutput[outputChannelNumber];
      }
    }
  }

  // Normalizes the output buffer after all channels have been added,
  // with a maximum of 18 channels,
  // and multiplies it to get the 16 bit signed output.
  for (int outputChannelNumber = 0 ; outputChannelNumber < 4 ; outputChannelNumber++)
  {
    output[outputChannelNumber] =
      (short)(outputBuffer[outputChannelNumber] / 18 * 0x7FFF);
  }

  // Advances the OPL3-wide vibrato index, which is used by
  // PhaseGenerator.getPhase() in each Operator.
  vibratoIndex++;
  if (vibratoIndex >= OPL3Data.vibratoTable[dvb].length) vibratoIndex = 0;

  // Advances the OPL3-wide tremolo index, which is used by
  // EnvelopeGenerator.getEnvelope() in each Operator.
  tremoloIndex++;
  if (tremoloIndex >= OPL3Data.tremoloTable[dam].length) tremoloIndex = 0;

  return output;
}

//
// Private functions
//

void OPL3_Emulator_t::initOperators(void)
{
  int baseAddress;

  // The YMF262 has 36 operators:
  operators = new Operator[2][0x20];
  for (int array = 0 ; array < 2 ; array++)
  {
    for (int group = 0 ; group <= 0x10 ; group+=8)
    {
      for (int offset = 0 ; offset < 6 ; offset++)
      {
        baseAddress = (array<<8) | (group+offset);
        operators[array][group+offset] = new Operator(baseAddress);
      }
  }

  // Create specific operators to switch when in rhythm mode:
  highHatOperator = new HighHatOperator();
  snareDrumOperator = new SnareDrumOperator();
  tomTomOperator = new TomTomOperator();
  topCymbalOperator = new TopCymbalOperator();

  // Save operators when they are in non-rhythm mode:
  // Channel 7:
  highHatOperatorInNonRhythmMode = operators[0][0x11];
  snareDrumOperatorInNonRhythmMode = operators[0][0x14];

  // Channel 8:
  tomTomOperatorInNonRhythmMode = operators[0][0x12];
  topCymbalOperatorInNonRhythmMode = operators[0][0x15];

}

void OPL3_Emulator_t::initChannels2op(void)
{

}

void OPL3_Emulator_t::initChannels4op(void)
{

}

void OPL3_Emulator_t::initRhythmChannels(void)
{

}

void OPL3_Emulator_t::initChannels(void)
{

}
