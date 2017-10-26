/*
 * File: opl3_data.h
 *
 * Software implementation of the Yamaha YMF262 sound generator in C++.
 * Copyright (C) 2014 Julian Olds
 *
 * Developed from OPL3.java Copyright (C) 2008 Robson Cozendey <robson@cozendey.com>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 */

//
// OPL3 Data
//

#include <math.h>

namespace OPL3Data
{

  // OPL3-wide registers offsets:
  const int _1_NTS1_6_Offset = 0x08;
  const int DAM1_DVB1_RYT1_BD1_SD1_TOM1_TC1_HH1_Offset = 0xBD;
  const int _7_NEW1_Offset = 0x105;
  const int _2_CONNECTIONSEL6_Offset = 0x104;

  const int sampleRate = 49700;

  // Vibrato table
  // The first array is used when DVB=0 and the second array is used when DVB=1.
  extern double vibratoTable[2][8192];

  // Tremolo table
  // First array used when AM = 0 and second array used when AM = 1.
  extern double tremoloTable[2][13432];

  void loadVibratoTable(void);
  void loadTremoloTable(void);

  double calculateIncrement(double begin, double end, double period);

}


//
// Channel Data
//

namespace ChannelData
{
  const int _2_KON1_BLOCK3_FNUMH2_Offset = 0xB0;
  const int FNUML8_Offset = 0xA0;
  const int CHD1_CHC1_CHB1_CHA1_FB3_CNT1_Offset = 0xC0;

  // Feedback rate in fractions of 2*Pi, normalized to (0,1):
  // 0, Pi/16, Pi/8, Pi/4, Pi/2, Pi, 2*Pi, 4*Pi turns to be:
  const double feedback[8] =
  {
    0.0,
    1.0/32.0,
    1.0/16.0,
    1.0/8.0,
    1.0/4.0,
    1.0/2.0,
    1.0,
    2.0
  };
}


//
// Operator Data
//

namespace OperatorData
{
  const int AM1_VIB1_EGT1_KSR1_MULT4_Offset = 0x20;
  const int KSL2_TL6_Offset = 0x40;
  const int AR4_DR4_Offset = 0x60;
  const int SL4_RR4_Offset = 0x80;
  const int _5_WS3_Offset = 0xE0;

  enum type { NO_MODULATION, CARRIER, FEEDBACK };

  const int waveLength = 1024;

  const double multTable[26] = { 0.5,1,2,3,4,5,6,7,8,9,10,10,12,12,15,15 };

  const double ksl3dBtable[16][8] =
  {
    {0,0,0,0,0,0,0,0},
    {0,0,0,0,0,-3,-6,-9},
    {0,0,0,0,-3,-6,-9,-12},
    {0,0,0, -1.875, -4.875, -7.875, -10.875, -13.875},

    {0,0,0,-3,-6,-9,-12,-15},
    {0,0, -1.125, -4.125, -7.125, -10.125, -13.125, -16.125},
    {0,0, -1.875, -4.875, -7.875, -10.875, -13.875, -16.875},
    {0,0, -2.625, -5.625, -8.625, -11.625, -14.625, -17.625},

    {0,0,-3,-6,-9,-12,-15,-18},
    {0, -0.750, -3.750, -6.750, -9.750, -12.750, -15.750, -18.750},
    {0, -1.125, -4.125, -7.125, -10.125, -13.125, -16.125, -19.125},
    {0, -1.500, -4.500, -7.500, -10.500, -13.500, -16.500, -19.500},

    {0, -1.875, -4.875, -7.875, -10.875, -13.875, -16.875, -19.875},
    {0, -2.250, -5.250, -8.250, -11.250, -14.250, -17.250, -20.250},
    {0, -2.625, -5.625, -8.625, -11.625, -14.625, -17.625, -20.625},
    {0,-3,-6,-9,-12,-15,-18,-21}
  };

  extern double waveforms[8][1024];

  void loadWaveforms(void);

  double log2(double x);
}


//
// Envelope Generator Data
//

namespace EnvelopeGeneratorData
{
  // This table is indexed by the value of Operator.ksr
  // and the value of ChannelRegister.keyScaleNumber.
  const int rateOffset[2][16] =
  {
    {0,0,0,0,1,1,1,1,2,2,2,2,3,3,3,3},
    {0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15}
  };

  // These attack periods in milliseconds were taken from the YMF278B manual.
  // The attack actual rates range from 0 to 63, with different data for
  // 0%-100% and for 10%-90%:
  const double attackTimeValuesTable[64][2] =
  {
    {INFINITY,INFINITY},    {INFINITY,INFINITY},    {INFINITY,INFINITY},    {INFINITY,INFINITY},
    {2826.24,1482.75}, {2252.80,1155.07}, {1884.16,991.23}, {1597.44,868.35},
    {1413.12,741.38}, {1126.40,577.54}, {942.08,495.62}, {798.72,434.18},
    {706.56,370.69}, {563.20,288.77}, {471.04,247.81}, {399.36,217.09},

    {353.28,185.34}, {281.60,144.38}, {235.52,123.90}, {199.68,108.54},
    {176.76,92.67}, {140.80,72.19}, {117.76,61.95}, {99.84,54.27},
    {88.32,46.34}, {70.40,36.10}, {58.88,30.98}, {49.92,27.14},
    {44.16,23.17}, {35.20,18.05}, {29.44,15.49}, {24.96,13.57},

    {22.08,11.58}, {17.60,9.02}, {14.72,7.74}, {12.48,6.78},
    {11.04,5.79}, {8.80,4.51}, {7.36,3.87}, {6.24,3.39},
    {5.52,2.90}, {4.40,2.26}, {3.68,1.94}, {3.12,1.70},
    {2.76,1.45}, {2.20,1.13}, {1.84,0.97}, {1.56,0.85},

    {1.40,0.73}, {1.12,0.61}, {0.92,0.49}, {0.80,0.43},
    {0.70,0.37}, {0.56,0.31}, {0.46,0.26}, {0.42,0.22},
    {0.38,0.19}, {0.30,0.14}, {0.24,0.11}, {0.20,0.11},
    {0.00,0.00}, {0.00,0.00}, {0.00,0.00}, {0.00,0.00}
  };

  // These decay and release periods in milliseconds were taken from the YMF278B manual.
  // The rate index range from 0 to 63, with different data for
  // 0%-100% and for 10%-90%:
  const double decayAndReleaseTimeValuesTable[64][2] =
  {
    {INFINITY,INFINITY},    {INFINITY,INFINITY},    {INFINITY,INFINITY},    {INFINITY,INFINITY},
    {39280.64,8212.48}, {31416.32,6574.08}, {26173.44,5509.12}, {22446.08,4730.88},
    {19640.32,4106.24}, {15708.16,3287.04}, {13086.72,2754.56}, {11223.04,2365.44},
    {9820.16,2053.12}, {7854.08,1643.52}, {6543.36,1377.28}, {5611.52,1182.72},

    {4910.08,1026.56}, {3927.04,821.76}, {3271.68,688.64}, {2805.76,591.36},
    {2455.04,513.28}, {1936.52,410.88}, {1635.84,344.34}, {1402.88,295.68},
    {1227.52,256.64}, {981.76,205.44}, {817.92,172.16}, {701.44,147.84},
    {613.76,128.32}, {490.88,102.72}, {488.96,86.08}, {350.72,73.92},

    {306.88,64.16}, {245.44,51.36}, {204.48,43.04}, {175.36,36.96},
    {153.44,32.08}, {122.72,25.68}, {102.24,21.52}, {87.68,18.48},
    {76.72,16.04}, {61.36,12.84}, {51.12,10.76}, {43.84,9.24},
    {38.36,8.02}, {30.68,6.42}, {25.56,5.38}, {21.92,4.62},

    {19.20,4.02}, {15.36,3.22}, {12.80,2.68}, {10.96,2.32},
    {9.60,2.02}, {7.68,1.62}, {6.40,1.35}, {5.48,1.15},
    {4.80,1.01}, {3.84,0.81}, {3.20,0.69}, {2.74,0.58},
    {2.40,0.51}, {2.40,0.51}, {2.40,0.51}, {2.40,0.51}
  };

}
