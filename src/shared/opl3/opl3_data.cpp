

#include <math.h>
#include "opl3_data.h"

namespace OPL3Data
{
double vibratoTable[2][8192];
double tremoloTable[2][13432];

void loadVibratoTable(void)
{
  // According to the YMF262 datasheet, the OPL3 vibrato repetition rate is 6.1 Hz.
  // According to the YMF278B manual, it is 6.0 Hz.
  // The information that the vibrato table has 8 levels standing 1024 samples each
  // was taken from the emulator by Jarek Burczynski and Tatsuyuki Satoh,
  // with a frequency of 6,06689453125 Hz, what  makes sense with the difference
  // in the information on the datasheets.

  const double semitone = pow(2, 1.0 / 12.0);
  // A cent is 1/100 of a semitone:
  const double cent = pow(semitone, 1.0 / 100.0);

  // When dvb=0, the depth is 7 cents, when it is 1, the depth is 14 cents.
  const double DVB0 = pow(cent, 7);
  const double DVB1 = pow(cent, 14);

  int i;
  for (i = 0 ; i < 1024 ; i++)
  {
    vibratoTable[0][i] = 1.0;
    vibratoTable[1][i] = 1.0;
  }
  for (; i < 2048; i++)
  {
    vibratoTable[0][i] = sqrt(DVB0);
    vibratoTable[1][i] = sqrt(DVB1);
  }
  for (; i < 3072 ; i++)
  {
    vibratoTable[0][i] = DVB0;
    vibratoTable[1][i] = DVB1;
  }
  for(; i < 4096; i++) {
    vibratoTable[0][i] = sqrt(DVB0);
    vibratoTable[1][i] = sqrt(DVB1);
  }
  for (; i < 5120; i++)
  {
    vibratoTable[0][i] = vibratoTable[1][i] = 1;
  }
  for (; i < 6144; i++)
  {
    vibratoTable[0][i] = 1.0 / sqrt(DVB0);
    vibratoTable[1][i] = 1.0 / sqrt(DVB1);
  }
  for (; i < 7168; i++)
  {
    vibratoTable[0][i] = 1.0 / DVB0;
    vibratoTable[1][i] = 1.0 / DVB1;
  }
  for (; i < 8192; i++)
  {
    vibratoTable[0][i] = 1.0 / sqrt(DVB0);
    vibratoTable[1][i] = 1.0 / sqrt(DVB1);
  }

}

void loadTremoloTable(void)
{
  // The OPL3 tremolo repetition rate is 3.7 Hz.
  const double tremoloFrequency = 3.7;

  // The tremolo depth is -1 dB when DAM = 0, and -4.8 dB when DAM = 1.
  const double tremoloDepth[2] = { -1, -4.8};

  //  According to the YMF278B manual's OPL3 section graph,
  //              the tremolo waveform is not
  //   \      /   a sine wave, but a single triangle waveform.
  //    \    /    Thus, the period to achieve the tremolo depth is T/2, and
  //     \  /     the increment in each T/2 section uses a frequency of 2*f.
  //      \/      Tremolo varies from 0 dB to depth, to 0 dB again, at frequency*2:
  const double tremoloIncrement[2] =
  {
    calculateIncrement(tremoloDepth[0], 0, 1 / (2 * tremoloFrequency)),
    calculateIncrement(tremoloDepth[1], 0, 1 / (2 * tremoloFrequency))
  };

  int tremoloTableLength = (int)(sampleRate / tremoloFrequency);

  // This is undocumented. The tremolo starts at the maximum attenuation,
  // instead of at 0 dB:
  tremoloTable[0][0] = tremoloDepth[0];
  tremoloTable[1][0] = tremoloDepth[1];

  int counter = 0;
  // The first half of the triangle waveform:
  while (tremoloTable[0][counter] < 0)
  {
    counter++;
    tremoloTable[0][counter] = tremoloTable[0][counter - 1] + tremoloIncrement[0];
    tremoloTable[1][counter] = tremoloTable[1][counter - 1] + tremoloIncrement[1];
  }

  // The second half of the triangle waveform:
  while ((tremoloTable[0][counter] > tremoloDepth[0]) &&
         (counter < tremoloTableLength - 1))
  {
    counter++;
    tremoloTable[0][counter] = tremoloTable[0][counter - 1] - tremoloIncrement[0];
    tremoloTable[1][counter] = tremoloTable[1][counter - 1] - tremoloIncrement[1];
  }

}

double calculateIncrement(double begin, double end, double period)
{
  return (end - begin) / sampleRate * (1 / period);
}

}


namespace OperatorData
{
double waveforms[8][1024];

void loadWaveforms(void)
{
  int i;
  // 1st waveform: sinusoid.
  double theta = 0, thetaIncrement = 2 * M_PI / 1024;

  for (i = 0, theta = 0; i < 1024 ; i++, theta += thetaIncrement)
  {
    waveforms[0][i] = sin(theta);
  }

  double *sineTable = waveforms[0];
  // 2nd: first half of a sinusoid.
  for (i = 0 ; i < 512 ; i++)
  {
    waveforms[1][i] = sineTable[i];
    waveforms[1][512 + i] = 0;
  }
  // 3rd: double positive sinusoid.
  for (i = 0 ; i < 512 ; i++)
  {
    waveforms[2][i] = waveforms[2][512 + i] = sineTable[i];
  }
  // 4th: first and third quarter of double positive sinusoid.
  for(i = 0 ; i < 256 ; i++)
  {
    waveforms[3][i] = waveforms[3][512 + i] = sineTable[i];
    waveforms[3][256 + i] = waveforms[3][768 + i] = 0;
  }
  // 5th: first half with double frequency sinusoid.
  for(i = 0 ; i < 512 ; i++)
  {
    waveforms[4][i] = sineTable[i * 2];
    waveforms[4][512 + i] = 0;
  }
  // 6th: first half with double frequency positive sinusoid.
  for(i = 0 ; i < 256 ; i++)
  {
    waveforms[5][i] = waveforms[5][256 + i] = sineTable[i * 2];
    waveforms[5][512 + i] = waveforms[5][768 + i] = 0;
  }
  // 7th: square wave
  for(i = 0 ; i < 512 ; i++)
  {
    waveforms[6][i] = 1;
    waveforms[6][512 + i] = -1;
  }
  // 8th: exponential
  double x;
  double xIncrement = 1.00 * 16.0 / 256.0;
  for(i = 0, x = 0 ; i < 512 ; i++, x += xIncrement)
  {
    waveforms[7][i] = pow(2, -x);
    waveforms[7][1023 - i] = -pow(2, -(x + 1.0 / 16.0));
  }

}

double log2(double x)
{
  return log(x) / log(2);
}

}
