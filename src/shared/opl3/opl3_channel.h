
#include "opl3_data.h"
#include "opl3_operator.h"

class OPL3_Channel_t
{
public:

  int channelBaseAddress;

  double feedback[2];

  int fnuml, fnumh, kon, block, cha, chb, chc, chd, fb, cnt;

  // Factor to convert between normalized amplitude to normalized
  // radians. The amplitude maximum is equivalent to 8*Pi radians.
  const double toPhase = 4.0;

  OPL3_Channel_t(unsigned char *regs, int baseAddress)
  {
    registers = regs;
    channelBaseAddress = baseAddress;
    fnuml = fnumh = kon = block = cha = chb = chc = chd = fb = cnt = 0;

    feedback[0] = 0.0;
    feedback[1] = 0.0;
  }

  virtual ~OPL3_Channel_t()
  {
  }

  void update_2_KON1_BLOCK3_FNUMH2()
  {
    int _2_kon1_block3_fnumh2 = registers[channelBaseAddress + ChannelData::_2_KON1_BLOCK3_FNUMH2_Offset];

    // Frequency Number (hi-register) and Block. These two registers, together with fnuml,
    // sets the Channel´s base frequency;
    block = (_2_kon1_block3_fnumh2 & 0x1C) >> 2;
    fnumh = _2_kon1_block3_fnumh2 & 0x03;
    updateOperators();

    // Key On. If changed, calls Channel.keyOn() / keyOff().
    int newKon   = (_2_kon1_block3_fnumh2 & 0x20) >> 5;
    if (newKon != kon)
    {
      if (newKon == 1) keyOn();
      else keyOff();
      kon = newKon;
    }
  }

  void update_FNUML8()
  {
    int fnuml8 = registers[channelBaseAddress + ChannelData::FNUML8_Offset];
    // Frequency Number, low register.
    fnuml = fnuml8&0xFF;
    updateOperators();
  }

  void update_CHD1_CHC1_CHB1_CHA1_FB3_CNT1()
  {
    int chd1_chc1_chb1_cha1_fb3_cnt1 = registers[channelBaseAddress + ChannelData::CHD1_CHC1_CHB1_CHA1_FB3_CNT1_Offset];
    chd   = (chd1_chc1_chb1_cha1_fb3_cnt1 & 0x80) >> 7;
    chc   = (chd1_chc1_chb1_cha1_fb3_cnt1 & 0x40) >> 6;
    chb   = (chd1_chc1_chb1_cha1_fb3_cnt1 & 0x20) >> 5;
    cha   = (chd1_chc1_chb1_cha1_fb3_cnt1 & 0x10) >> 4;
    fb    = (chd1_chc1_chb1_cha1_fb3_cnt1 & 0x0E) >> 1;
    cnt   = chd1_chc1_chb1_cha1_fb3_cnt1 & 0x01;
    updateOperators();
  }

  void updateChannel()
  {
    update_2_KON1_BLOCK3_FNUMH2();
    update_FNUML8();
    update_CHD1_CHC1_CHB1_CHA1_FB3_CNT1();
  }

  virtual void getChannelOutput(double *output) = 0;

protected:

  unsigned char *registers;

  void getInFourChannels(double *output, double channelOutput)
  {
    if (OPL3._new==0)
    {
      output[0] = channelOutput;
      output[1] = channelOutput;
      output[2] = channelOutput;
      output[3] = channelOutput;
    }
    else
    {
      output[0] = (cha==1) ? channelOutput : 0;
      output[1] = (chb==1) ? channelOutput : 0;
      output[2] = (chc==1) ? channelOutput : 0;
      output[3] = (chd==1) ? channelOutput : 0;
    }
  }

  virtual void keyOn() = 0;
  virtual void keyOff() = 0;
  virtual void updateOperators() = 0;
};

class OPL3_Channel2op_t : public OPL3_Channel_t
{
public:

  OPL3_Channel2op_t(unsigned char *regs, int baseAddress, OPL3_Operator_t o1, OPL3_Operator_t o2) :
    OPL3_Channel_t(regs, baseAddress)
  {
    op1 = o1;
    op2 = o2;
  }

  virtual void getChannelOutput(double *output)
  {
    double channelOutput = 0, op1Output = 0, op2Output = 0;
    double[] output;
    // The feedback uses the last two outputs from
    // the first operator, instead of just the last one.
    double feedbackOutput = (feedback[0] + feedback[1]) / 2;


    switch(cnt)
    {
      // CNT = 0, the operators are in series, with the first in feedback.
      case 0:
        if(op2.envelopeGenerator.stage==EnvelopeGenerator.Stage.OFF)
            return getInFourChannels(0);
        op1Output = op1.getOperatorOutput(feedbackOutput);
        channelOutput = op2.getOperatorOutput(op1Output*toPhase);
        break;

      // CNT = 1, the operators are in parallel, with the first in feedback.
      case 1:
        if(op1.envelopeGenerator.stage==EnvelopeGenerator.Stage.OFF &&
            op2.envelopeGenerator.stage==EnvelopeGenerator.Stage.OFF)
                return getInFourChannels(0);
        op1Output = op1.getOperatorOutput(feedbackOutput);
        op2Output = op2.getOperatorOutput(Operator.noModulator);
        channelOutput = (op1Output + op2Output) / 2;
        break;
    }

    feedback[0] = feedback[1];
    feedback[1] = (op1Output * ChannelData.feedback[fb])%1;
    output = getInFourChannels(channelOutput);
    return output;
  }

protected:

  OPL3_Operator_t op1, op2;

  virtual inline void keyOn(void)
  {
    op1.keyOn();
    op2.keyOn();
    feedback[0] = feedback[1] = 0.0;
  }

  virtual inline void keyOff(void)
  {
    op1.keyOff();
    op2.keyOff();
  }

  virtual inline void updateOperators(void)
  {
    // Key Scale Number, used in EnvelopeGenerator.setActualRates().
    int keyScaleNumber = block*2 + ((fnumh>>OPL3.nts)&0x01);
    int f_number = (fnumh<<8) | fnuml;
    op1.updateOperator(keyScaleNumber, f_number, block);
    op2.updateOperator(keyScaleNumber, f_number, block);
  }
};


class OPL3_Channel4op_t : public OPL3_Channel_t
{
public:


  Channel4op (int baseAddress, OPL3_Operator_t o1, OPL3_Operator_t o2, OPL3_Operator_t o3, OPL3_Operator_t o4)
  {
    super(baseAddress);
    op1 = o1;
    op2 = o2;
    op3 = o3;
    op4 = o4;
  }

    double[] getChannelOutput() {
        double channelOutput = 0,
               op1Output = 0, op2Output = 0, op3Output = 0, op4Output = 0;

        double[] output;

        int secondChannelBaseAddress = channelBaseAddress+3;
        int secondCnt = OPL3.registers[secondChannelBaseAddress+ChannelData.CHD1_CHC1_CHB1_CHA1_FB3_CNT1_Offset] & 0x1;
        int cnt4op = (cnt << 1) | secondCnt;

        double feedbackOutput = (feedback[0] + feedback[1]) / 2;

        switch(cnt4op) {
            case 0:
                if(op4.envelopeGenerator.stage==EnvelopeGenerator.Stage.OFF)
                    return getInFourChannels(0);

                op1Output = op1.getOperatorOutput(feedbackOutput);
                op2Output = op2.getOperatorOutput(op1Output*toPhase);
                op3Output = op3.getOperatorOutput(op2Output*toPhase);
                channelOutput = op4.getOperatorOutput(op3Output*toPhase);

                break;
            case 1:
                if(op2.envelopeGenerator.stage==EnvelopeGenerator.Stage.OFF &&
                    op4.envelopeGenerator.stage==EnvelopeGenerator.Stage.OFF)
                       return getInFourChannels(0);

                op1Output = op1.getOperatorOutput(feedbackOutput);
                op2Output = op2.getOperatorOutput(op1Output*toPhase);

                op3Output = op3.getOperatorOutput(Operator.noModulator);
                op4Output = op4.getOperatorOutput(op3Output*toPhase);

                channelOutput = (op2Output + op4Output) / 2;
                break;
            case 2:
                if(op1.envelopeGenerator.stage==EnvelopeGenerator.Stage.OFF &&
                    op4.envelopeGenerator.stage==EnvelopeGenerator.Stage.OFF)
                       return getInFourChannels(0);

                op1Output = op1.getOperatorOutput(feedbackOutput);

                op2Output = op2.getOperatorOutput(Operator.noModulator);
                op3Output = op3.getOperatorOutput(op2Output*toPhase);
                op4Output = op4.getOperatorOutput(op3Output*toPhase);

                channelOutput = (op1Output + op4Output) / 2;
                break;
            case 3:
                if(op1.envelopeGenerator.stage==EnvelopeGenerator.Stage.OFF &&
                    op3.envelopeGenerator.stage==EnvelopeGenerator.Stage.OFF &&
                    op4.envelopeGenerator.stage==EnvelopeGenerator.Stage.OFF)
                       return getInFourChannels(0);

                op1Output = op1.getOperatorOutput(feedbackOutput);

                op2Output = op2.getOperatorOutput(Operator.noModulator);
                op3Output = op3.getOperatorOutput(op2Output*toPhase);

                op4Output = op4.getOperatorOutput(Operator.noModulator);

                channelOutput = (op1Output + op3Output + op4Output) / 3;
        }

        feedback[0] = feedback[1];
        feedback[1] = (op1Output * ChannelData.feedback[fb])%1;

        output = getInFourChannels(channelOutput);
        return output;
    }

protected:

  OPL3_Operator_t op1, op2, op3, op4;

  virtual inline void keyOn(void)
  {
    op1.keyOn();
    op2.keyOn();
    op3.keyOn();
    op4.keyOn();
    feedback[0] = feedback[1] = 0;
  }

  virtual inline void keyOff(void)
  {
    op1.keyOff();
    op2.keyOff();
    op3.keyOff();
    op4.keyOff();
  }

  virtual inline void updateOperators()
  {
    // Key Scale Number, used in EnvelopeGenerator.setActualRates().
    int keyScaleNumber = block*2 + ((fnumh>>OPL3.nts)&0x01);
    int f_number = (fnumh<<8) | fnuml;
    op1.updateOperator(keyScaleNumber, f_number, block);
    op2.updateOperator(keyScaleNumber, f_number, block);
    op3.updateOperator(keyScaleNumber, f_number, block);
    op4.updateOperator(keyScaleNumber, f_number, block);
  }

}

// There's just one instance of this class, that fills the eventual gaps in the Channel array;
class OPL3_DisabledChannel_t : public OPL3_Channel_t
{
public:

  OPL3_DisabledChannel_t() :
    OPL3_Channel_t(NULL, 0)
  {
  }

  virtual void getChannelOutput(double *output) { return getInFourChannels(output, 0); }

protected:

  virtual void keyOn(void) { }
  virtual void keyOff(void) { }
  virtual void updateOperators(void) { }
}

