
#include "opl3_envelope_gen.h"

//
// Phase Generator
//

class OPL3_PhaseGenerator_t
{
public:
  double phase, phaseIncrement;

  OPL3_PhaseGenerator_t()
  {
    phase = phaseIncrement = 0;
  }

  inline void setFrequency(int f_number, int block, int mult)
  {
    // This frequency formula is derived from the following equation:
    // f_number = baseFrequency * pow(2,19) / sampleRate / pow(2,block-1);
    double baseFrequency =
        f_number * pow(2, block-1) * OPL3Data::sampleRate / pow(2, 19);
    double operatorFrequency = baseFrequency * OperatorData::multTable[mult];

    // phase goes from 0 to 1 at
    // period = (1/frequency) seconds ->
    // Samples in each period is (1/frequency)*sampleRate =
    // = sampleRate/frequency ->
    // So the increment in each sample, to go from 0 to 1, is:
    // increment = (1-0) / samples in the period ->
    // increment = 1 / (OPL3Data.sampleRate/operatorFrequency) ->
    phaseIncrement = operatorFrequency / OPL3Data::sampleRate;
  }

  inline double getPhase(int vib)
  {
    if (vib == 1)
    {
      // phaseIncrement = (operatorFrequency * vibrato) / sampleRate
      phase += phaseIncrement * OPL3Data::vibratoTable[OPL3.dvb][OPL3.vibratoIndex];
    }
    else
    {
      // phaseIncrement = operatorFrequency / sampleRate
      phase += phaseIncrement;
    }
    phase -= floor(phase);
    return phase;
  }

  inline void keyOn(void)
  {
    phase = 0.0;
  }

};


class OPL3_Operator_t
{
  OPL3_PhaseGenerator_t phaseGenerator;
  OPL3_EnvelopeGenerator_t envelopeGenerator;

  double envelope, phase;

  int operatorBaseAddress;
  int am, vib, ksr, egt, mult, ksl, tl, ar, dr, sl, rr, ws;
  int keyScaleNumber, f_number, block;

  const double noModulator = 0.0;

  OPL3_Operator_t(int baseAddress)
  {
    operatorBaseAddress = baseAddress;

    envelope = 0;
    am = vib = ksr = egt = mult = ksl = tl = ar = dr = sl = rr = ws = 0;
    keyScaleNumber = f_number = block = 0;
  }

  inline void update_AM1_VIB1_EGT1_KSR1_MULT4(void)
  {
    int am1_vib1_egt1_ksr1_mult4 = OPL3.registers[operatorBaseAddress + OperatorData::AM1_VIB1_EGT1_KSR1_MULT4_Offset];

    // Amplitude Modulation. This register is used int EnvelopeGenerator.getEnvelope();
    am  = (am1_vib1_egt1_ksr1_mult4 & 0x80) >> 7;
    // Vibrato. This register is used in PhaseGenerator.getPhase();
    vib = (am1_vib1_egt1_ksr1_mult4 & 0x40) >> 6;
    // Envelope Generator Type. This register is used in EnvelopeGenerator.getEnvelope();
    egt = (am1_vib1_egt1_ksr1_mult4 & 0x20) >> 5;
    // Key Scale Rate. Sets the actual envelope rate together with rate and keyScaleNumber.
    // This register os used in EnvelopeGenerator.setActualAttackRate().
    ksr = (am1_vib1_egt1_ksr1_mult4 & 0x10) >> 4;
    // Multiple. Multiplies the Channel.baseFrequency to get the Operator.operatorFrequency.
    // This register is used in PhaseGenerator.setFrequency().
    mult = am1_vib1_egt1_ksr1_mult4 & 0x0F;

    phaseGenerator.setFrequency(f_number, block, mult);
    envelopeGenerator.setActualAttackRate(ar, ksr, keyScaleNumber);
    envelopeGenerator.setActualDecayRate(dr, ksr, keyScaleNumber);
    envelopeGenerator.setActualReleaseRate(rr, ksr, keyScaleNumber);
  }

  inline void update_KSL2_TL6(void)
  {

    int ksl2_tl6 = OPL3.registers[operatorBaseAddress + OperatorData::KSL2_TL6_Offset];

    // Key Scale Level. Sets the attenuation in accordance with the octave.
    ksl = (ksl2_tl6 & 0xC0) >> 6;
    // Total Level. Sets the overall damping for the envelope.
    tl  =  ksl2_tl6 & 0x3F;

    envelopeGenerator.setAtennuation(f_number, block, ksl);
    envelopeGenerator.setTotalLevel(tl);
  }

  inline void update_AR4_DR4()
  {
    int ar4_dr4 = OPL3.registers[operatorBaseAddress+OperatorData.AR4_DR4_Offset];

    // Attack Rate.
    ar = (ar4_dr4 & 0xF0) >> 4;
    // Decay Rate.
    dr =  ar4_dr4 & 0x0F;

    envelopeGenerator.setActualAttackRate(ar, ksr, keyScaleNumber);
    envelopeGenerator.setActualDecayRate(dr, ksr, keyScaleNumber);
  }

  inline void update_SL4_RR4()
  {
    int sl4_rr4 = OPL3.registers[operatorBaseAddress+OperatorData.SL4_RR4_Offset];

    // Sustain Level.
    sl = (sl4_rr4 & 0xF0) >> 4;
    // Release Rate.
    rr =  sl4_rr4 & 0x0F;

    envelopeGenerator.setActualSustainLevel(sl);
    envelopeGenerator.setActualReleaseRate(rr, ksr, keyScaleNumber);
  }

  inline void update_5_WS3()
  {
    int _5_ws3 = OPL3.registers[operatorBaseAddress+OperatorData._5_WS3_Offset];
    ws =  _5_ws3 & 0x07;
  }

  inline double getOperatorOutput(double modulator)
  {
    if(envelopeGenerator.stage == EnvelopeGenerator.Stage.OFF) return 0;

    double envelopeInDB = envelopeGenerator.getEnvelope(egt, am);
    envelope = Math.pow(10, envelopeInDB/10.0);

    // If it is in OPL2 mode, use first four waveforms only:
    ws &= ((OPL3._new<<2) + 3);
    double[] waveform = OperatorData.waveforms[ws];

    phase = phaseGenerator.getPhase(vib);

    double operatorOutput = getOutput(modulator, phase, waveform);
    return operatorOutput;
  }

protected:

  inline double getOutput(double modulator, double outputPhase, double *waveform)
  {
    outputPhase = (outputPhase + modulator) % 1;
    if (outputPhase < 0)
    {
      outputPhase++;
      // If the double could not afford to be less than 1:
      outputPhase %= 1;
    }
    int sampleIndex = (int) (outputPhase * OperatorData.waveLength);
    return waveform[sampleIndex] * envelope;
  }

  inline void keyOn(void)
  {
    if (ar > 0)
    {
      envelopeGenerator.keyOn();
      phaseGenerator.keyOn();
    }
    else
    {
      envelopeGenerator.stage = EnvelopeGenerator.Stage.OFF;
    }
  }

  inline void keyOff(void)
  {
    envelopeGenerator.keyOff();
  }

  inline void updateOperator(int ksn, int f_num, int blk)
  {
    keyScaleNumber = ksn;
    f_number = f_num;
    block = blk;
    update_AM1_VIB1_EGT1_KSR1_MULT4();
    update_KSL2_TL6();
    update_AR4_DR4();
    update_SL4_RR4();
    update_5_WS3();
  }

}



//
// Rhythm
//

// The getOperatorOutput() method in TopCymbalOperator, HighHatOperator and SnareDrumOperator
// were made through purely empyrical reverse engineering of the OPL3 output.

class OPL3_RhythmChannel_t :public OPL3_Channel2op_t
{
public:
    RhythmChannel(int baseAddress, Operator o1, Operator o2) {
        super(baseAddress, o1, o2);
    }

    double[] getChannelOutput() {
        double channelOutput = 0, op1Output = 0, op2Output = 0;
        double[] output;

        // Note that, different from the common channel,
        // we do not check to see if the Operator's envelopes are Off.
        // Instead, we always do the calculations,
        // to update the publicly available phase.
        op1Output = op1.getOperatorOutput(Operator.noModulator);
        op2Output = op2.getOperatorOutput(Operator.noModulator);
        channelOutput = (op1Output + op2Output) / 2;

        output = getInFourChannels(channelOutput);
        return output;
    };

    // Rhythm channels are always running,
    // only the envelope is activated by the user.

    protected void keyOn() { };

    protected void keyOff() { };
}

class HighHatSnareDrumChannel extends RhythmChannel {
    final static int highHatSnareDrumChannelBaseAddress = 7;

    HighHatSnareDrumChannel() {
        super(highHatSnareDrumChannelBaseAddress,
                                OPL3.highHatOperator,
                                OPL3.snareDrumOperator);
    }
}

class TomTomTopCymbalChannel extends RhythmChannel {
    final static int tomTomTopCymbalChannelBaseAddress = 8;

    TomTomTopCymbalChannel() {
        super(tomTomTopCymbalChannelBaseAddress,
                                OPL3.tomTomOperator,
                                OPL3.topCymbalOperator);
    }
}

class TopCymbalOperator extends Operator {
    final static int topCymbalOperatorBaseAddress = 0x15;

    TopCymbalOperator(int baseAddress) {
        super(baseAddress);
    }

    TopCymbalOperator() {
        this(topCymbalOperatorBaseAddress);
    }


    double getOperatorOutput(double modulator) {
        double highHatOperatorPhase =
            OPL3.highHatOperator.phase * OperatorData.multTable[OPL3.highHatOperator.mult];
        // The Top Cymbal operator uses his own phase together with the High Hat phase.
        return getOperatorOutput(modulator, highHatOperatorPhase);
    }

    // This method is used here with the HighHatOperator phase
    // as the externalPhase.
    // Conversely, this method is also used through inheritance by the HighHatOperator,
    // now with the TopCymbalOperator phase as the externalPhase.
    protected double getOperatorOutput(double modulator, double externalPhase) {
        double envelopeInDB = envelopeGenerator.getEnvelope(egt, am);
        envelope = Math.pow(10, envelopeInDB/10.0);

        phase = phaseGenerator.getPhase(vib);

        int waveIndex = ws & ((OPL3._new<<2) + 3);
        double[] waveform = OperatorData.waveforms[waveIndex];

        // Empirically tested multiplied phase for the Top Cymbal:
        double carrierPhase = (8 * phase)%1;
        double modulatorPhase = externalPhase;
        double modulatorOutput = getOutput(Operator.noModulator,modulatorPhase, waveform);
        double carrierOutput = getOutput(modulatorOutput,carrierPhase, waveform);

        int cycles = 4;
        if( (carrierPhase*cycles)%cycles > 0.1) carrierOutput = 0;

        return carrierOutput*2;
    }
}

class HighHatOperator extends TopCymbalOperator {
    final static int highHatOperatorBaseAddress = 0x11;

    HighHatOperator() {
        super(highHatOperatorBaseAddress);
    }


    double getOperatorOutput(double modulator) {
        double topCymbalOperatorPhase =
            OPL3.topCymbalOperator.phase * OperatorData.multTable[OPL3.topCymbalOperator.mult];
        // The sound output from the High Hat resembles the one from
        // Top Cymbal, so we use the parent method and modifies his output
        // accordingly afterwards.
        double operatorOutput = super.getOperatorOutput(modulator, topCymbalOperatorPhase);
        if(operatorOutput == 0) operatorOutput = Math.random()*envelope;
        return operatorOutput;
    }

}

class SnareDrumOperator extends Operator {
    final static int snareDrumOperatorBaseAddress = 0x14;

    SnareDrumOperator() {
        super(snareDrumOperatorBaseAddress);
    }


    double getOperatorOutput(double modulator) {
        if(envelopeGenerator.stage == EnvelopeGenerator.Stage.OFF) return 0;

        double envelopeInDB = envelopeGenerator.getEnvelope(egt, am);
        envelope = Math.pow(10, envelopeInDB/10.0);

        // If it is in OPL2 mode, use first four waveforms only:
        int waveIndex = ws & ((OPL3._new<<2) + 3);
        double[] waveform = OperatorData.waveforms[waveIndex];

        phase = OPL3.highHatOperator.phase * 2;

        double operatorOutput = getOutput(modulator, phase, waveform);

        double noise = Math.random() * envelope;

        if(operatorOutput/envelope != 1 && operatorOutput/envelope != -1) {
            if(operatorOutput > 0)  operatorOutput = noise;
            else if(operatorOutput < 0) operatorOutput = -noise;
            else operatorOutput = 0;
        }

        return operatorOutput*2;
    }
}

class TomTomOperator extends Operator {
    final static int tomTomOperatorBaseAddress = 0x12;
    TomTomOperator() {
        super(tomTomOperatorBaseAddress);
    }
}

