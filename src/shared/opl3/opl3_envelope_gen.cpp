

#include "opl3_envelope_gen.h"
#include "opl3_data.h"

OPL3_EnvelopeGenerator_t::OPL3_EnvelopeGenerator_t()
{
  stage = STAGE_OFF;
  actualAttackRate = actualDecayRate = actualReleaseRate = 0;
  xAttackIncrement = xMinimumInAttack = 0;
  dBdecayIncrement = 0;
  dBreleaseIncrement = 0;
  attenuation = totalLevel = sustainLevel = 0;
  x = dBtoX(-96);
  envelope = -96;
}

OPL3_EnvelopeGenerator_t::~OPL3_EnvelopeGenerator_t()
{

}

void OPL3_EnvelopeGenerator_t::setActualSustainLevel(int sl)
{
    // If all SL bits are 1, sustain level is set to -93 dB:
   if(sl == 0x0F) {
       sustainLevel = -93;
       return;
   }
   // The datasheet states that the SL formula is
   // sustainLevel = -24*d7 -12*d6 -6*d5 -3*d4,
   // translated as:
   sustainLevel = -3*sl;
}

void OPL3_EnvelopeGenerator_t::setTotalLevel(int tl)
{
  // The datasheet states that the TL formula is
  // TL = -(24*d5 + 12*d4 + 6*d3 + 3*d2 + 1.5*d1 + 0.75*d0),
  // translated as:
  totalLevel = tl*-0.75;
}

void OPL3_EnvelopeGenerator_t::setAtennuation(int f_number, int block, int ksl)
{
  int hi4bits = (f_number>>6)&0x0F;
  switch(ksl)
  {
    case 0:
      attenuation = 0;
      break;
    case 1:
      // ~3 dB/Octave
      attenuation = OperatorData::ksl3dBtable[hi4bits][block];
      break;
    case 2:
      // ~1.5 dB/Octave
      attenuation = OperatorData::ksl3dBtable[hi4bits][block]/2;
      break;
    case 3:
      // ~6 dB/Octave
      attenuation = OperatorData::ksl3dBtable[hi4bits][block]*2;
  }
}

void OPL3_EnvelopeGenerator_t::setActualAttackRate(int attackRate, int ksr, int keyScaleNumber)
{
  // According to the YMF278B manual's OPL3 section, the attack curve is exponential,
  // with a dynamic range from -96 dB to 0 dB and a resolution of 0.1875 dB
  // per level.
  //
  // This method sets an attack increment and attack minimum value
  // that creates a exponential dB curve with 'period0to100' seconds in length
  // and 'period10to90' seconds between 10% and 90% of the curve total level.
  actualAttackRate = calculateActualRate(attackRate, ksr, keyScaleNumber);
  double period0to100inSeconds = EnvelopeGeneratorData::attackTimeValuesTable[actualAttackRate][0]/1000d;
  int period0to100inSamples = (int)(period0to100inSeconds * OPL3Data.sampleRate);
  double period10to90inSeconds = EnvelopeGeneratorData::attackTimeValuesTable[actualAttackRate][1]/1000d;
  int period10to90inSamples = (int)(period10to90inSeconds * OPL3Data.sampleRate);

  // The x increment is dictated by the period between 10% and 90%:
  xAttackIncrement = OPL3Data.calculateIncrement(percentageToX(0.1), percentageToX(0.9), period10to90inSeconds);

  // Discover how many samples are still from the top.
  // It cannot reach 0 dB, since x is a logarithmic parameter and would be
  // negative infinity. So we will use -0.1875 dB as the resolution
  // maximum.
  //
  // percentageToX(0.9) + samplesToTheTop*xAttackIncrement = dBToX(-0.1875); ->
  // samplesToTheTop = (dBtoX(-0.1875) - percentageToX(0.9)) / xAttackIncrement); ->
  // period10to100InSamples = period10to90InSamples + samplesToTheTop; ->
  int period10to100inSamples = (int) (period10to90inSamples + (dBtoX(-0.1875) - percentageToX(0.9)) / xAttackIncrement);

  // Discover the minimum x that, through the attackIncrement value, keeps
  // the 10%-90% period, and reaches 0 dB at the total period:
  xMinimumInAttack = percentageToX(0.1) - (period0to100inSamples-period10to100inSamples)*xAttackIncrement;
}


void OPL3_EnvelopeGenerator_t::setActualDecayRate(int decayRate, int ksr, int keyScaleNumber)
{
  actualDecayRate = calculateActualRate(decayRate, ksr, keyScaleNumber);
  double period10to90inSeconds = EnvelopeGeneratorData::decayAndReleaseTimeValuesTable[actualDecayRate][1]/1000d;

  // Differently from the attack curve, the decay/release curve is linear.
  // The dB increment is dictated by the period between 10% and 90%:
  dBdecayIncrement = OPL3Data::calculateIncrement(percentageToDB(0.1), percentageToDB(0.9), period10to90inSeconds);
}

void OPL3_EnvelopeGenerator_t::setActualReleaseRate(int releaseRate, int ksr, int keyScaleNumber)
{
  actualReleaseRate = calculateActualRate(releaseRate, ksr, keyScaleNumber);

  double period10to90inSeconds = EnvelopeGeneratorData::decayAndReleaseTimeValuesTable[actualReleaseRate][1]/1000d;
  dBreleaseIncrement = OPL3Data::calculateIncrement(percentageToDB(0.1), percentageToDB(0.9), period10to90inSeconds);
}

double OPL3_EnvelopeGenerator_t::getEnvelope(int egt, int am)
{
  // The datasheets attenuation values
  // must be halved to match the real OPL3 output.
  double envelopeSustainLevel = sustainLevel / 2;
  double envelopeTremolo =
          OPL3Data.tremoloTable[OPL3.dam][OPL3.tremoloIndex] / 2;
  double envelopeAttenuation = attenuation / 2;
  double envelopeTotalLevel = totalLevel / 2;

  double envelopeMinimum = -96;
  double envelopeResolution = 0.1875;

  double outputEnvelope;
  //
  // Envelope Generation
  //
  switch(stage)
  {
    case STAGE_ATTACK:
      // Since the attack is exponential, it will never reach 0 dB, so
      // we´ll work with the next to maximum in the envelope resolution.
      if ((envelope < -envelopeResolution) && (xAttackIncrement != -EnvelopeGeneratorData.INFINITY))
      {
        // The attack is exponential.
        envelope = -(1 << x);
        x += xAttackIncrement;
        break;
      }
      else
      {
        // It is needed here to explicitly set envelope = 0, since
        // only the attack can have a period of
        // 0 seconds and produce an infinity envelope increment.
        envelope = 0;
        stage = STAGE_DECAY;
      }
    case STAGE_DECAY:
      // The decay and release are linear.
      if (envelope > envelopeSustainLevel)
      {
        envelope -= dBdecayIncrement;
        break;
      }
      else
      {
        stage = STAGE_SUSTAIN;
      }

    case STAGE_SUSTAIN:
      // The Sustain stage is maintained all the time of the Key ON,
      // even if we are in non-sustaining mode.
      // This is necessary because, if the key is still pressed, we can
      // change back and forth the state of EGT, and it will release and
      // hold again accordingly.
      if (egt==1)
      {
        break;
      }
      else
      {
        if (envelope > envelopeMinimum)
        {
          envelope -= dBreleaseIncrement;
        }
        else
        {
          stage = STAGE_OFF;
        }
      }
      break;

    case STAGE_RELEASE:
      // If we have Key OFF, only here we are in the Release stage.
      // Now, we can turn EGT back and forth and it will have no effect,i.e.,
      // it will release inexorably to the Off stage.
      if (envelope > envelopeMinimum)
      {
          envelope -= dBreleaseIncrement;
      }
      else
      {
        stage = STAGE_OFF;
      }
      break;

  }

  // Ongoing original envelope
  outputEnvelope = envelope;

  //Tremolo
  if (am == 1) outputEnvelope += envelopeTremolo;

  //Attenuation
  outputEnvelope += envelopeAttenuation;

  //Total Level
  outputEnvelope += envelopeTotalLevel;

  return outputEnvelope;
}

void OPL3_EnvelopeGenerator_t::keyOn()
{
  // If we are taking it in the middle of a previous envelope,
  // start to rise from the current level:
  // envelope = - (2 ^ x); ->
  // 2 ^ x = -envelope ->
  // x = log2(-envelope); ->
  double xCurrent = OperatorData.log2(-envelope);
  x = (xCurrent < xMinimumInAttack) ? xCurrent : xMinimumInAttack;
  stage = STAGE_ATTACK;
}

void OPL3_EnvelopeGenerator_t::keyOff()
{
  if (stage != STAGEOFF) stage = STAGE_RELEASE;
}

//
// Private methods
//

int OPL3_EnvelopeGenerator_t::calculateActualRate(int rate, int ksr, int keyScaleNumber)
{
  int rof = EnvelopeGeneratorData::rateOffset[ksr][keyScaleNumber];
  int actualRate = rate*4 + rof;

  // If, as an example at the maximum, rate is 15 and the rate offset is 15,
  // the value would
  // be 75, but the maximum allowed is 63:
  if (actualRate > 63) actualRate = 63;

  return actualRate;
}

double OPL3_EnvelopeGenerator_t::dBtoX(double dB)
{
  return OperatorData.log2(-dB);
}

double OPL3_EnvelopeGenerator_t::percentageToDB(double percentage)
{
  return Math.log10(percentage)*10d;
}

double OPL3_EnvelopeGenerator_t::percentageToX(double percentage)
{
  return dBtoX(percentageToDB(percentage));
}
